#include "../include/wx_bridge_receive.h"
#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_memory.h"
#include "../include/wx_bridge_offsets.h"
#include "../include/wx_bridge_paths.h"
#include "../include/wx_bridge_protocol.h"
#include "../include/wx_bridge_runtime.h"
#include "../include/wx_bridge_text.h"
#include "../include/wx_bridge_wechat_string.h"

#define RECEIVE_BUFFER 65536
#define TRAMPOLINE_SIZE 256

typedef struct ReceiveState {
    volatile LONG installed;
    BYTE original[7];
    void *trampoline;
} ReceiveState;

static ReceiveState g_receive = {0};
static volatile LONG64 g_last_event_key = 0;

static ULONGLONG hash_text(const char *value) {
    ULONGLONG hash = 14695981039346656037ULL;
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    while (*cursor) {
        hash ^= (ULONGLONG)*cursor++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static ULONGLONG message_event_key(int64_t msg_id, int32_t local_id,
                                   int32_t create_time, const char *from,
                                   const char *to) {
    ULONGLONG hash = hash_text(from);
    hash ^= hash_text(to); hash *= 1099511628211ULL;
    hash ^= (ULONGLONG)msg_id; hash *= 1099511628211ULL;
    hash ^= (ULONGLONG)(uint32_t)local_id; hash *= 1099511628211ULL;
    hash ^= (ULONGLONG)(uint32_t)create_time;
    return hash;
}

static int append_escaped_bounded(char **out, char *limit, const char *value) {
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    while (*cursor) {
        unsigned char c = *cursor++;
        int needed = (c == '\\' || c == '"' || c == '\n' || c == '\r' || c == '\t') ? 2 :
                     (c < 0x20 ? 6 : 1);
        if (limit - *out < needed) return 1;
        if (c == '\\' || c == '"') {
            *(*out)++ = '\\'; *(*out)++ = (char)c;
        } else if (c == '\n') {
            *(*out)++ = '\\'; *(*out)++ = 'n';
        } else if (c == '\r') {
            *(*out)++ = '\\'; *(*out)++ = 'r';
        } else if (c == '\t') {
            *(*out)++ = '\\'; *(*out)++ = 't';
        } else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            *(*out)++ = '\\'; *(*out)++ = 'u'; *(*out)++ = '0'; *(*out)++ = '0';
            *(*out)++ = hex[(c >> 4) & 0xF]; *(*out)++ = hex[c & 0xF];
        } else {
            *(*out)++ = (char)c;
        }
    }
    return 0;
}

static void classify_message(const char *from, const char *to, const char *content,
                             char *peer, int peer_max, char *sender, int sender_max,
                             char *direction, int direction_max,
                             int *incoming, int *outgoing) {
    int from_room = is_chatroom_id(from);
    int to_room = is_chatroom_id(to);
    *incoming = 0;
    *outgoing = 0;
    safe_copy(peer, peer_max, "");
    safe_copy(sender, sender_max, from);
    safe_copy(direction, direction_max, "unknown");

    if (from_room || to_room) {
        const char *room = from_room ? from : to;
        safe_copy(peer, peer_max, room);
        if (from_room) extract_group_sender(content, sender, sender_max);
        else safe_copy(sender, sender_max, from);
        if (wxb_self_wxid_known() && str_eq(sender, wxb_self_wxid())) {
            *outgoing = 1;
            safe_copy(direction, direction_max, "outgoing");
        } else if (sender[0]) {
            *incoming = 1;
            safe_copy(direction, direction_max, "incoming");
        }
        return;
    }

    if (wxb_self_wxid_known() && str_eq(from, wxb_self_wxid())) {
        *outgoing = 1;
        safe_copy(peer, peer_max, to);
        safe_copy(direction, direction_max, "outgoing");
        return;
    }
    if (wxb_self_wxid_known() && str_eq(to, wxb_self_wxid())) {
        *incoming = 1;
        safe_copy(peer, peer_max, from);
        safe_copy(direction, direction_max, "incoming");
        return;
    }
    /* First event fallback: the endpoint that looks like wxid_ is usually self
     * for an outgoing event, while a non-wxid endpoint is usually self for an
     * incoming event. Keep the identity conservative after this first guess. */
    if (is_wxid_like(from) && (!is_wxid_like(to) || str_eq(to, "filehelper"))) {
        wxb_learn_self_wxid(from);
        *outgoing = 1;
        safe_copy(peer, peer_max, to);
        safe_copy(direction, direction_max, "outgoing");
    } else if (is_wxid_like(to) && !is_wxid_like(from)) {
        wxb_learn_self_wxid(to);
        *incoming = 1;
        safe_copy(peer, peer_max, from);
        safe_copy(direction, direction_max, "incoming");
    } else {
        safe_copy(peer, peer_max, from);
    }
}

static void process_message(int64_t msg_base) {
    int64_t msg_id;
    int32_t local_id;
    int32_t create_time;
    int msg_type;
    int msg_subtype;
    char id_s[32], local_s[32], type_s[32], subtype_s[32], time_s[32];
    char from[512] = {0}, to[512] = {0};
    char content[RECEIVE_BUFFER] = {0}, signature[2048] = {0};
    char peer[512] = {0}, sender[512] = {0}, direction[16] = {0};
    int incoming = 0, outgoing = 0;
    int content_truncated;
    int content_original_bytes;
    ULONGLONG event_key;
    char json[RECEIVE_BUFFER + 8192];
    char *cursor = json;
    char *end = json + sizeof(json) - 1;
    WCHAR path[1024];
    int content_read;

    if (!msg_base) return;
    msg_id = r64((void *)(msg_base + WX_MSG_ID_OFF));
    local_id = r32((void *)(msg_base + WX_MSG_LOCAL_ID_OFF));
    msg_type = r32((void *)(msg_base + WX_MSG_RUNTIME_TYPE_OFF));
    msg_subtype = r32((void *)(msg_base + WX_MSG_SUBTYPE_OFF));
    create_time = read_msg_createtime(msg_base);
    i64toa10(msg_id, id_s);
    i64toa10(local_id, local_s);
    i64toa10(msg_type, type_s);
    i64toa10(msg_subtype, subtype_s);
    i64toa10(create_time, time_s);
    wx_read_str(msg_base + WX_MSG_FROM_OFF, from, sizeof(from));
    wx_read_str(msg_base + WX_MSG_TO_OFF, to, sizeof(to));
    content_read = wx_read_str(msg_base + WX_MSG_CONTENT_OFF, content, sizeof(content));
    content_original_bytes = r32((void *)(msg_base + WX_MSG_CONTENT_OFF + 0x10));
    wx_read_str(msg_base + WX_MSG_SIGNATURE_OFF, signature, sizeof(signature));
    event_key = message_event_key(msg_id, local_id, create_time, from, to);
    if ((LONG64)event_key == InterlockedCompareExchange64(&g_last_event_key, 0, 0)) return;
    InterlockedExchange64(&g_last_event_key, (LONG64)event_key);

    classify_message(from, to, content, peer, sizeof(peer), sender, sizeof(sender),
                     direction, sizeof(direction), &incoming, &outgoing);

    append_raw(&cursor, end, "{\"msgId\":\""); append_escaped(&cursor, end, id_s);
    append_raw(&cursor, end, "\",\"localId\":\""); append_escaped(&cursor, end, local_s);
    append_raw(&cursor, end, "\",\"msgLocalId\":\""); append_escaped(&cursor, end, local_s);
    append_raw(&cursor, end, "\",\"type\":\""); append_escaped(&cursor, end, type_s);
    append_raw(&cursor, end, "\",\"subType\":\""); append_escaped(&cursor, end, subtype_s);
    append_raw(&cursor, end, "\",\"subtype\":\""); append_escaped(&cursor, end, subtype_s);
    append_raw(&cursor, end, "\",\"timestamp\":\""); append_escaped(&cursor, end, time_s);
    append_raw(&cursor, end, "\",\"from\":\""); append_escaped(&cursor, end, from);
    append_raw(&cursor, end, "\",\"fromWxId\":\""); append_escaped(&cursor, end, to);
    append_raw(&cursor, end, "\",\"rawFrom\":\""); append_escaped(&cursor, end, from);
    append_raw(&cursor, end, "\",\"rawTo\":\""); append_escaped(&cursor, end, to);
    append_raw(&cursor, end, "\",\"isSendMsg\":"); append_raw(&cursor, end, outgoing ? "true" : "false");
    append_raw(&cursor, end, ",\"isIncoming\":"); append_raw(&cursor, end, incoming ? "true" : "false");
    append_raw(&cursor, end, ",\"direction\":\""); append_escaped(&cursor, end, direction);
    append_raw(&cursor, end, "\",\"peerWxId\":\""); append_escaped(&cursor, end, peer);
    append_raw(&cursor, end, "\",\"roomId\":\"");
    append_escaped(&cursor, end, is_chatroom_id(peer) ? peer : "");
    append_raw(&cursor, end, "\",\"senderWxId\":\""); append_escaped(&cursor, end, sender);
    append_raw(&cursor, end, "\",\"selfWxId\":\""); append_escaped(&cursor, end, wxb_self_wxid());
    append_raw(&cursor, end, "\",\"content\":\"");
    content_truncated = append_escaped_bounded(&cursor, end - 16384, content);
    append_raw(&cursor, end, "\",\"signature\":\"");
    append_escaped_bounded(&cursor, end - 1024, signature);
    append_raw(&cursor, end, "\",\"contentBytes\":");
    { char n[32]; i64toa10(content_read, n); append_raw(&cursor, end, n); }
    append_raw(&cursor, end, ",\"contentOriginalBytes\":");
    { char n[32]; i64toa10(content_original_bytes, n); append_raw(&cursor, end, n); }
    append_raw(&cursor, end, ",\"contentTruncated\":");
    append_raw(&cursor, end,
               (content_truncated || content_original_bytes > content_read) ? "true" : "false");
    append_raw(&cursor, end, "}");
    *cursor = 0;

    wsprintfW(path, L"%s\\%S_%S_%S_%016I64x.json", INBOX_DIR,
              id_s, local_s, time_s, event_key);
    if (!wxb_write_file_bytes_atomic(path, json, (DWORD)(cursor - json))) {
        /* A repeated/invalid id should not prevent the next message. */
        wsprintfW(path, L"%s\\%S_%lu.json", INBOX_DIR, id_s,
                  (unsigned long)GetTickCount());
        wxb_write_file_bytes_atomic(path, json, (DWORD)(cursor - json));
    }
    {
        char log_line[1024];
        wsprintfA(log_line, "recv id=%s local=%s type=%d/%d from=%s to=%s peer=%s in=%d out=%d\r\n",
                  id_s, local_s, msg_type, msg_subtype, from, to, peer, incoming, outgoing);
        append_file_bytes(RECV_LOG, log_line, (DWORD)lstrlenA(log_line));
    }
}

static int build_trampoline(BYTE *p, int64_t return_addr) {
    BYTE *start = p;
    BYTE save[] = {
        0x9C, 0x50,0x51,0x52, 0x41,0x50,0x41,0x51, 0x41,0x52,0x41,0x53,
        0x48,0x81,0xEC,0x80,0x00,0x00,0x00,
        0xF3,0x0F,0x7F,0x44,0x24,0x20, 0xF3,0x0F,0x7F,0x4C,0x24,0x30,
        0xF3,0x0F,0x7F,0x54,0x24,0x40, 0xF3,0x0F,0x7F,0x5C,0x24,0x50,
        0xF3,0x0F,0x7F,0x64,0x24,0x60, 0xF3,0x0F,0x7F,0x6C,0x24,0x70
    };
    BYTE load[] = {0x48,0x8B,0x46,0x18, 0x48,0x85,0xC0, 0x74,0x0F,
                   0x48,0x89,0xC1, 0x48,0xB8};
    BYTE restore[] = {
        0xF3,0x0F,0x6F,0x44,0x24,0x20, 0xF3,0x0F,0x6F,0x4C,0x24,0x30,
        0xF3,0x0F,0x6F,0x54,0x24,0x40, 0xF3,0x0F,0x6F,0x5C,0x24,0x50,
        0xF3,0x0F,0x6F,0x64,0x24,0x60, 0xF3,0x0F,0x6F,0x6C,0x24,0x70,
        0x48,0x81,0xC4,0x80,0x00,0x00,0x00,
        0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58, 0x5A,0x59,0x58,0x9D
    };
    copy_bytes(p, save, sizeof(save)); p += sizeof(save);
    copy_bytes(p, load, sizeof(load)); p += sizeof(load);
    *(int64_t *)p = (int64_t)process_message; p += 8;
    *p++ = 0xFF; *p++ = 0xD0;
    copy_bytes(p, restore, sizeof(restore)); p += sizeof(restore);
    copy_bytes(p, g_receive.original, sizeof(g_receive.original)); p += sizeof(g_receive.original);
    *p++ = 0x48; *p++ = 0xB8; *(int64_t *)p = return_addr; p += 8;
    *p++ = 0xFF; *p++ = 0xE0;
    return (int)(p - start);
}

int wxb_install_recv_hook(void) {
    BYTE expected[7] = {0x48,0x89,0x85,0xF8,0x01,0x00,0x00};
    BYTE *target;
    int64_t rel;
    BYTE patch[7];
    DWORD old = 0;
    if (InterlockedCompareExchange(&g_receive.installed, 0, 0) || !wxb_weixin_module()) return 1;
    target = (BYTE *)((int64_t)wxb_weixin_module() + WX_OFF_DOADDMSG);
    for (int i = 0; i < 7; i++) if (target[i] != expected[i]) return 0;
    copy_bytes(g_receive.original, target, sizeof(g_receive.original));
    g_receive.trampoline = alloc_near(target, TRAMPOLINE_SIZE);
    if (!g_receive.trampoline) return 0;
    {
        int trampoline_len = build_trampoline((BYTE *)g_receive.trampoline,
                                              (int64_t)(target + 7));
        FlushInstructionCache(GetCurrentProcess(), g_receive.trampoline,
                              (SIZE_T)trampoline_len);
    }
    rel = (int64_t)g_receive.trampoline - ((int64_t)target + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        VirtualFree(g_receive.trampoline, 0, MEM_RELEASE);
        g_receive.trampoline = NULL;
        return 0;
    }
    patch[0] = 0xE9; *(int32_t *)(patch + 1) = (int32_t)rel; patch[5] = 0x90; patch[6] = 0x90;
    VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &old);
    copy_bytes(target, patch, sizeof(patch));
    VirtualProtect(target, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    InterlockedExchange(&g_receive.installed, 1);
    write_status("receive hook active\n");
    return 1;
}

void wxb_uninstall_recv_hook(void) {
    BYTE *target;
    DWORD old = 0;
    if (!InterlockedCompareExchange(&g_receive.installed, 0, 0) || !wxb_weixin_module()) return;
    target = (BYTE *)((int64_t)wxb_weixin_module() + WX_OFF_DOADDMSG);
    VirtualProtect(target, sizeof(g_receive.original), PAGE_EXECUTE_READWRITE, &old);
    copy_bytes(target, g_receive.original, sizeof(g_receive.original));
    VirtualProtect(target, sizeof(g_receive.original), old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(g_receive.original));
    InterlockedExchange(&g_receive.installed, 0);
    if (g_receive.trampoline) VirtualFree(g_receive.trampoline, 0, MEM_RELEASE);
    g_receive.trampoline = NULL;
}

int wxb_recv_hook_installed(void) {
    return InterlockedCompareExchange(&g_receive.installed, 0, 0) != 0;
}

int wxb_recv_hook_site_ready(void) {
    static const BYTE expected[7] = {0x48,0x89,0x85,0xF8,0x01,0x00,0x00};
    BYTE *target;
    if (!wxb_weixin_module()) return 0;
    target = (BYTE *)((int64_t)wxb_weixin_module() + WX_OFF_DOADDMSG);
    return bytes_equal(target, expected, sizeof(expected));
}
