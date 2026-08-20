#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_paths.h"

int wxb_file_exists(const WCHAR *path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void wxb_ensure_dirs(void) {
    CreateDirectoryW(BRIDGE_DIR, NULL);
    CreateDirectoryW(INBOX_DIR, NULL);
    CreateDirectoryW(OUTBOX_DIR, NULL);
    CreateDirectoryW(CONTROL_DIR, NULL);
}

void wxb_write_file_bytes(const WCHAR *path, const char *data, DWORD len) {
    HANDLE handle;
    DWORD written = 0;
    if (!path || !path[0]) return;
    handle = CreateFileW(path, GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return;
    if (len) WriteFile(handle, data, len, &written, NULL);
    CloseHandle(handle);
}

int wxb_write_file_bytes_atomic(const WCHAR *path, const char *data, DWORD len) {
    WCHAR temp_path[1024];
    HANDLE handle;
    DWORD written = 0;
    BOOL ok;
    if (!path || !path[0]) return 0;
    wsprintfW(temp_path, L"%s.%lu.%lu.tmp", path,
              (unsigned long)GetCurrentProcessId(),
              (unsigned long)GetCurrentThreadId());
    handle = CreateFileW(temp_path, GENERIC_WRITE, FILE_SHARE_READ,
                         NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    ok = len == 0 || WriteFile(handle, data, len, &written, NULL);
    if (ok && written == len) ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ok && written == len) {
        ok = MoveFileExW(temp_path, path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) DeleteFileW(temp_path);
    return ok ? 1 : 0;
}

void wxb_append_file_bytes(const WCHAR *path, const char *data, DWORD len) {
    HANDLE handle;
    DWORD written = 0;
    if (!path || !path[0]) return;
    handle = CreateFileW(path, FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return;
    if (len) WriteFile(handle, data, len, &written, NULL);
    CloseHandle(handle);
}

int wxb_read_file_bytes(const WCHAR *path, char *out, DWORD max) {
    HANDLE handle;
    DWORD size;
    DWORD read = 0;
    BOOL ok;
    if (!path || !out || max < 2) return -1;
    handle = CreateFileW(path, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    size = GetFileSize(handle, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size >= max) {
        CloseHandle(handle);
        return -1;
    }
    ok = ReadFile(handle, out, size, &read, NULL);
    CloseHandle(handle);
    if (!ok || read == 0 || read >= max) return -1;
    out[read] = 0;
    return (int)read;
}
