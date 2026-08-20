#include "../include/wx_bridge_runtime.h"
#include "../include/wx_bridge_text.h"

static HINSTANCE g_instance = NULL;
static HMODULE g_weixin = NULL;
static char g_self_wxid[128] = {0};

HINSTANCE wxb_bridge_instance(void) {
    return g_instance;
}

void wxb_set_bridge_instance(HINSTANCE instance) {
    g_instance = instance;
}

HMODULE wxb_weixin_module(void) {
    return g_weixin;
}

HMODULE wxb_ensure_weixin_module(void) {
    if (!g_weixin) g_weixin = GetModuleHandleW(L"Weixin.dll");
    return g_weixin;
}

const char *wxb_self_wxid(void) {
    return g_self_wxid;
}

int wxb_self_wxid_known(void) {
    return g_self_wxid[0] != 0;
}

void wxb_learn_self_wxid(const char *wxid) {
    if (g_self_wxid[0] || !is_wxid_like(wxid) || is_chatroom_id(wxid)) return;
    safe_copy(g_self_wxid, sizeof(g_self_wxid), wxid);
}
