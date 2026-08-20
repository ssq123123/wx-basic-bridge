#include "../include/wx_bridge_owner.h"
#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_paths.h"
#include "../include/wx_bridge_runtime.h"
#include "../include/wx_bridge_text.h"

static HANDLE owner_lock(void) {
    HANDLE handle = CreateMutexW(NULL, FALSE, L"Local\\wx_basic_bridge_owner_4_1_9_23");
    DWORD wait_result;
    if (!handle) return NULL;
    wait_result = WaitForSingleObject(handle, 10000);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
        CloseHandle(handle);
        return NULL;
    }
    return handle;
}

static void owner_unlock(HANDLE handle) {
    if (!handle) return;
    ReleaseMutex(handle);
    CloseHandle(handle);
}

int wxb_claim_worker_owner(char *token, int max) {
    HANDLE lock;
    if (!token || max < 32) return 0;
    wsprintfA(token, "%lu:%I64x", (unsigned long)GetCurrentProcessId(),
              (unsigned __int64)(int64_t)wxb_bridge_instance());
    lock = owner_lock();
    if (!lock) return 0;
    if (!write_file_bytes_atomic(WORKER_OWNER_FILE, token, (DWORD)lstrlenA(token))) {
        owner_unlock(lock);
        return 0;
    }
    owner_unlock(lock);
    return 1;
}

int wxb_worker_owner_current(const char *token) {
    HANDLE lock;
    char current[128] = {0};
    int size;
    int same;
    if (!token || !token[0]) return 0;
    lock = owner_lock();
    if (!lock) return 0;
    size = read_file_bytes(WORKER_OWNER_FILE, current, sizeof(current));
    if (size > 0) trim_trailing_crlf(current);
    same = size > 0 && str_eq(current, token);
    owner_unlock(lock);
    return same;
}

void wxb_release_worker_owner(const char *token) {
    HANDLE lock;
    char current[128] = {0};
    int size;
    if (!token || !token[0]) return;
    lock = owner_lock();
    if (!lock) return;
    size = read_file_bytes(WORKER_OWNER_FILE, current, sizeof(current));
    if (size > 0) trim_trailing_crlf(current);
    if (size > 0 && str_eq(current, token)) {
        write_file_bytes_atomic(WORKER_OWNER_FILE, "", 0);
    }
    owner_unlock(lock);
}
