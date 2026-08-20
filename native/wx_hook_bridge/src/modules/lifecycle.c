#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_outbox.h"
#include "../include/wx_bridge_owner.h"
#include "../include/wx_bridge_paths.h"
#include "../include/wx_bridge_protocol.h"
#include "../include/wx_bridge_receive.h"
#include "../include/wx_bridge_runtime.h"
#include "../include/wx_bridge_text.h"

static volatile LONG g_worker_active = 0;

static int nonce_valid(const WCHAR *nonce) {
    if (!nonce || lstrlenW(nonce) != WXB_STARTUP_NONCE_CHARS) return 0;
    for (DWORD i = 0; i < WXB_STARTUP_NONCE_CHARS; i++) {
        WCHAR c = nonce[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
              (c >= L'A' && c <= L'F'))) return 0;
    }
    return 1;
}

static int copy_nonce(WCHAR *out, LPVOID param) {
    const WxbStartupConfig *config = (const WxbStartupConfig *)param;
    if (!out) return 0;
    out[0] = 0;
    if (!config || config->magic != WXB_STARTUP_CONFIG_MAGIC ||
        config->size != sizeof(WxbStartupConfig)) return 0;
    if (!nonce_valid(config->startup_nonce)) return 0;
    CopyMemory(out, config->startup_nonce,
               (WXB_STARTUP_NONCE_CHARS + 1) * sizeof(WCHAR));
    return 1;
}

static void startup_path(WCHAR *out, const WCHAR *nonce, const WCHAR *suffix) {
    if (!out || !nonce || !suffix) return;
    wsprintfW(out, L"%s\\worker_start_%s.%s", CONTROL_DIR, nonce, suffix);
}

static void startup_write(const WCHAR *nonce, const WCHAR *suffix, const char *text) {
    WCHAR path[MAX_PATH];
    startup_path(path, nonce, suffix);
    write_file_bytes_atomic(path, text, (DWORD)lstrlenA(text));
}

static int startup_exists(const WCHAR *nonce, const WCHAR *suffix) {
    WCHAR path[MAX_PATH];
    startup_path(path, nonce, suffix);
    return file_exists(path);
}

static int wait_for_ack(const WCHAR *nonce) {
    DWORD started = GetTickCount();
    if (!nonce || !nonce[0]) return 1;
    startup_write(nonce, L"ready", "worker ready\n");
    while (GetTickCount() - started < 30000) {
        if (startup_exists(nonce, L"cancel")) return 0;
        if (startup_exists(nonce, L"ack")) return 1;
        Sleep(50);
    }
    return 0;
}

static void cleanup_startup_files(const WCHAR *nonce) {
    WCHAR path[MAX_PATH];
    if (!nonce || !nonce[0]) return;
    startup_path(path, nonce, L"ready"); DeleteFileW(path);
    startup_path(path, nonce, L"ack"); DeleteFileW(path);
    startup_path(path, nonce, L"active"); DeleteFileW(path);
    startup_path(path, nonce, L"cancel"); DeleteFileW(path);
}

static int wait_for_hook_site(void) {
    DWORD started = GetTickCount();
    while (GetTickCount() - started < 10000) {
        if (recv_hook_site_ready()) return 1;
        Sleep(50);
    }
    return 0;
}

static int bridge_init(void) {
    ensure_dirs();
    wxb_ensure_weixin_module();
    if (wxb_weixin_module() && !recv_hook_installed()) {
        if (!install_recv_hook()) {
            write_status("receive hook install failed\n");
            return 0;
        }
    }
    return wxb_weixin_module() && recv_hook_installed();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wxb_set_bridge_instance(instance);
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

__declspec(dllexport) DWORD WINAPI WxBridgeWorker(LPVOID param) {
    WCHAR nonce[WXB_STARTUP_NONCE_CHARS + 1] = {0};
    char owner_token[128] = {0};
    int has_nonce = copy_nonce(nonce, param);
    if (!wxb_paths_init(wxb_bridge_instance(), param)) return 1;
    if (!wait_for_ack(has_nonce ? nonce : NULL)) {
        cleanup_startup_files(nonce);
        return 2;
    }
    if (InterlockedCompareExchange(&g_worker_active, 1, 0) != 0) {
        cleanup_startup_files(nonce);
        return 3;
    }
    ensure_dirs();
    wxb_ensure_weixin_module();
    if (!claim_worker_owner(owner_token, sizeof(owner_token))) {
        InterlockedExchange(&g_worker_active, 0);
        cleanup_startup_files(nonce);
        return 4;
    }
    /* A previous cache-busted basic DLL sees the owner token change, removes
     * its hook, and exits. Never overwrite a foreign/full bridge hook. */
    if (!wait_for_hook_site()) {
        write_status("receive hook site busy; restart Weixin before injecting basic bridge\n");
        release_worker_owner(owner_token);
        InterlockedExchange(&g_worker_active, 0);
        cleanup_startup_files(nonce);
        return 5;
    }
    if (!bridge_init()) {
        release_worker_owner(owner_token);
        InterlockedExchange(&g_worker_active, 0);
        cleanup_startup_files(nonce);
        return 6;
    }
    write_status("worker started\n");
    if (has_nonce) startup_write(nonce, L"active", "worker active\n");

    for (;;) {
        if (file_exists(STOP_FILE)) break;
        if (!worker_owner_current(owner_token)) break;
        if (!wxb_weixin_module()) wxb_ensure_weixin_module();
        if (wxb_weixin_module() && !recv_hook_installed()) install_recv_hook();
        if (wxb_weixin_module()) poll_outbox();
        Sleep(100);
    }

    uninstall_recv_hook();
    release_worker_owner(owner_token);
    InterlockedExchange(&g_worker_active, 0);
    write_status("worker stopped\n");
    cleanup_startup_files(nonce);
    return 0;
}

__declspec(dllexport) LRESULT CALLBACK WxGetMsgHook(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && wxb_paths_ready() && InterlockedCompareExchange(&g_worker_active, 0, 0)) {
        if (!file_exists(STOP_FILE)) {
            if (wxb_weixin_module() && !recv_hook_installed()) install_recv_hook();
            if (wxb_weixin_module()) poll_outbox();
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

__declspec(dllexport) DWORD WINAPI WxBridgeVersion(void) {
    return 0x04192301;
}
