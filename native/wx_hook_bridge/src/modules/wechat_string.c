#include "../include/wx_bridge_wechat_string.h"
#include "../include/wx_bridge_memory.h"

int wxb_wx_read_str(int64_t header, char *out, int max) {
    int len;
    int tag;
    int count;
    const char *source;
    if (!header || !out || max <= 0) return 0;
    out[0] = 0;
    len = r32((void *)(header + 0x10));
    tag = r32((void *)(header + 0x18));
    if (len <= 0 || tag <= 0) return 0;
    source = tag == 15 ? (const char *)header : (const char *)r64((void *)header);
    if (!source) return 0;
    count = len < max - 1 ? len : max - 1;
    __try {
        for (int i = 0; i < count; i++) out[i] = source[i];
        out[count] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return 0;
    }
    return count;
}

void wxb_wx_set_str_arena(int64_t header, const char *utf8, int len,
                          BYTE **pool, BYTE *pool_end) {
    if (!utf8) utf8 = "";
    if (len < 0) len = lstrlenA(utf8);
    if (len <= 15) {
        copy_bytes((void *)header, utf8, len);
        w32((void *)(header + 0x10), len);
        w32((void *)(header + 0x18), 15);
        return;
    }
    if (*pool + len + 1 >= pool_end) len = (int)(pool_end - *pool - 1);
    if (len < 0) len = 0;
    copy_bytes(*pool, utf8, len);
    (*pool)[len] = 0;
    w64((void *)header, (int64_t)(*pool));
    w32((void *)(header + 0x10), len);
    w32((void *)(header + 0x18), len + 5);
    *pool += len + 16;
}
