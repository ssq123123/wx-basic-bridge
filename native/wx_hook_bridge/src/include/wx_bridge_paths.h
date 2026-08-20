#ifndef WX_BRIDGE_PATHS_H
#define WX_BRIDGE_PATHS_H

#include "wx_bridge_platform.h"

#define WXB_STARTUP_CONFIG_MAGIC 0x42584257u
#define WXB_STARTUP_CONFIG_FREE_AFTER_READ 0x00000001u
#define WXB_STARTUP_NONCE_CHARS 32u
#define WXB_PATH_MAX_SUFFIX_CHARS 69u

typedef struct WxbStartupConfig {
    DWORD magic;
    DWORD size;
    DWORD flags;
    DWORD reserved;
    WCHAR work_root[MAX_PATH];
    WCHAR startup_nonce[WXB_STARTUP_NONCE_CHARS + 1];
} WxbStartupConfig;

typedef struct WxbPaths {
    WCHAR work_root[MAX_PATH];
    WCHAR bridge_dir[MAX_PATH];
    WCHAR inbox_dir[MAX_PATH];
    WCHAR outbox_dir[MAX_PATH];
    WCHAR control_dir[MAX_PATH];
    WCHAR outbox_file[MAX_PATH];
    WCHAR stop_file[MAX_PATH];
    WCHAR status_file[MAX_PATH];
    WCHAR recv_log[MAX_PATH];
    WCHAR worker_owner_file[MAX_PATH];
} WxbPaths;

int wxb_paths_init(HMODULE module, LPVOID startup_param);
int wxb_paths_ready(void);
const WxbPaths *wxb_paths(void);

#define WORK_ROOT   (wxb_paths()->work_root)
#define BRIDGE_DIR  (wxb_paths()->bridge_dir)
#define INBOX_DIR   (wxb_paths()->inbox_dir)
#define OUTBOX_DIR  (wxb_paths()->outbox_dir)
#define CONTROL_DIR (wxb_paths()->control_dir)
#define OUTBOX_FILE (wxb_paths()->outbox_file)
#define STOP_FILE   (wxb_paths()->stop_file)
#define STATUS_FILE (wxb_paths()->status_file)
#define RECV_LOG    (wxb_paths()->recv_log)
#define WORKER_OWNER_FILE (wxb_paths()->worker_owner_file)

#endif /* WX_BRIDGE_PATHS_H */
