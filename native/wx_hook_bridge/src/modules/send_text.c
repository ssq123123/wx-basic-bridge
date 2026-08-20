#include "../include/wx_bridge_send_text.h"
#include "../include/wx_bridge_memory.h"
#include "../include/wx_bridge_native_call.h"
#include "../include/wx_bridge_offsets.h"
#include "../include/wx_bridge_runtime.h"
#include "../include/wx_bridge_wechat_string.h"

void wxb_retain_send_arena(void *arena);

int wxb_send_text(const char *wxid, const char *content) {
    const SIZE_T arena_size = 0x30000;
    BYTE *arena;
    BYTE *pool;
    BYTE *pool_end;
    int64_t base;
    int64_t msg;
    int64_t rcxs;
    BYTE *buf1;
    BYTE *buf2;
    BYTE *buf3;
    BYTE *buf4;
    BYTE *buf5;
    BYTE *buf6;
    int ok = 0;

    if (!wxid || !content || !wxb_weixin_module()) return 0;
    arena = (BYTE *)VirtualAlloc(NULL, arena_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!arena) return 0;
    ZeroMemory(arena, arena_size);

    base = (int64_t)wxb_weixin_module();
    msg = (int64_t)(arena + 0x0000);
    rcxs = (int64_t)(arena + 0x1000);
    buf1 = arena + 0x1100;
    buf2 = arena + 0x1200;
    buf3 = arena + 0x1300;
    buf4 = arena + 0x1400;
    buf5 = arena + 0x1500;
    buf6 = arena + 0x1600;
    pool = arena + 0x2000;
    pool_end = arena + arena_size;

    __try {
        /* Construct the same lightweight text message model used by 4.1.9.23. */
        w64((void *)(msg + 0x8), 0x100000001LL);
        w64((void *)msg, base + WX_OFF_SEND_HC1_TEXT);
        ((FnCall1)(base + WX_OFF_SEND_CALL1_TEXT))(msg + 0x10);
        w64((void *)(msg + 0xE8), 1);
        w64((void *)(msg + 0x18), msg + 0x10);
        w64((void *)(msg + 0x20), msg);
        wx_set_str_arena(msg + 0xC0, wxid, lstrlenA(wxid), &pool, pool_end);
        wx_set_str_arena(msg + 0x670, content, lstrlenA(content), &pool, pool_end);

        w64((void *)rcxs, base + WX_OFF_SEND_CALL2);
        w64(buf1, msg + 0x10);
        w64(buf1 + 8, r64((void *)(msg + 0x18)));
        w64((void *)(rcxs + 8), (int64_t)buf1);
        w64((void *)(rcxs + 16), (int64_t)buf1 + 16);
        w64((void *)(rcxs + 24), (int64_t)buf1 + 16);

        w64((void *)buf3, base + WX_OFF_SEND_CALL2_HC1);
        w64((void *)(buf3 + 56), (int64_t)buf3);
        w64((void *)buf4, base + WX_OFF_SEND_CALL2_HC3);
        w64((void *)(buf4 + 56), (int64_t)buf4);
        w64((void *)buf5, base + WX_OFF_SEND_CALL2_HC2);
        w64((void *)(buf5 + 56), (int64_t)buf5);
        (void)buf6;

        {
            int64_t result = ((FnCall2)(base + WX_OFF_SEND_CALL2))(
                (int64_t)buf2, (int64_t)buf3, (int64_t)buf4,
                (int64_t)buf5, (int64_t)buf6);
            if (result) {
                ((FnCall3)(base + WX_OFF_SEND_CALL3))(rcxs, result);
                ok = 1;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }

    /* The WeChat queue consumes the object asynchronously. Keep its arena alive. */
    wxb_retain_send_arena(arena);
    return ok;
}
