#include "../include/wx_bridge_paths.h"

static WxbPaths g_paths;
static WxbPaths g_empty_paths;
static volatile LONG g_paths_state = 0;

static int copy_root(WCHAR *out, const WCHAR *root) {
    DWORD len;
    if (!out || !root || !root[0]) return 0;
    len = (DWORD)lstrlenW(root);
    if (len == 0 || len >= MAX_PATH) return 0;
    while (len > 3 && (root[len - 1] == L'\\' || root[len - 1] == L'/')) len--;
    CopyMemory(out, root, len * sizeof(WCHAR));
    out[len] = 0;
    return 1;
}

static int module_directory(HMODULE module, WCHAR *out) {
    DWORD len;
    WCHAR *cursor;
    if (!module || !out) return 0;
    len = GetModuleFileNameW(module, out, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return 0;
    cursor = out + len;
    while (cursor > out && cursor[-1] != L'\\' && cursor[-1] != L'/') cursor--;
    if (cursor == out) return 0;
    if (cursor - out > 3) cursor--;
    *cursor = 0;
    return out[0] != 0;
}

static int join_root(WCHAR *out, const WCHAR *root, const WCHAR *suffix) {
    DWORD root_len;
    DWORD suffix_len;
    if (!out || !root || !suffix) return 0;
    root_len = (DWORD)lstrlenW(root);
    suffix_len = (DWORD)lstrlenW(suffix);
    if (root_len + suffix_len >= MAX_PATH) return 0;
    CopyMemory(out, root, root_len * sizeof(WCHAR));
    CopyMemory(out + root_len, suffix, (suffix_len + 1) * sizeof(WCHAR));
    return 1;
}

static int populate_paths(WxbPaths *paths, const WCHAR *root) {
#define SET_PATH(field, suffix) if (!join_root(paths->field, root, suffix)) return 0
    if (!copy_root(paths->work_root, root)) return 0;
    root = paths->work_root;
    SET_PATH(bridge_dir, L"\\bridge");
    SET_PATH(inbox_dir, L"\\bridge\\inbox");
    SET_PATH(outbox_dir, L"\\bridge\\outbox");
    SET_PATH(control_dir, L"\\bridge\\control");
    SET_PATH(outbox_file, L"\\bridge\\outbox\\next.txt");
    SET_PATH(stop_file, L"\\bridge\\control\\stop.txt");
    SET_PATH(status_file, L"\\bridge\\control\\status.txt");
    SET_PATH(recv_log, L"\\bridge\\control\\recv_debug.txt");
    SET_PATH(worker_owner_file, L"\\bridge\\control\\worker_owner.txt");
#undef SET_PATH
    return 1;
}

int wxb_paths_init(HMODULE module, LPVOID startup_param) {
    const WxbStartupConfig *config = (const WxbStartupConfig *)startup_param;
    const WCHAR *requested_root = NULL;
    DWORD free_after_read = 0;
    WCHAR fallback_root[MAX_PATH] = {0};
    WxbPaths next = {0};
    LONG state;
    int ok;

    if (config && config->magic == WXB_STARTUP_CONFIG_MAGIC &&
        config->size == sizeof(WxbStartupConfig)) {
        free_after_read = config->flags & WXB_STARTUP_CONFIG_FREE_AFTER_READ;
        if (config->work_root[0]) requested_root = config->work_root;
    }
    if (!requested_root) {
        if (!module_directory(module, fallback_root)) return 0;
        requested_root = fallback_root;
    }
    ok = populate_paths(&next, requested_root);
    if (free_after_read) VirtualFree(startup_param, 0, MEM_RELEASE);
    if (!ok) return 0;

    state = InterlockedCompareExchange(&g_paths_state, 1, 0);
    if (state != 0) {
        while (InterlockedCompareExchange(&g_paths_state, 0, 0) == 1) Sleep(1);
        return wxb_paths_ready();
    }
    CopyMemory(&g_paths, &next, sizeof(g_paths));
    MemoryBarrier();
    InterlockedExchange(&g_paths_state, 2);
    return 1;
}

int wxb_paths_ready(void) {
    return InterlockedCompareExchange(&g_paths_state, 0, 0) == 2;
}

const WxbPaths *wxb_paths(void) {
    return wxb_paths_ready() ? &g_paths : &g_empty_paths;
}
