#ifndef WX_BRIDGE_FILE_IO_H
#define WX_BRIDGE_FILE_IO_H

#include "wx_bridge_platform.h"

int wxb_file_exists(const WCHAR *path);
void wxb_ensure_dirs(void);
void wxb_write_file_bytes(const WCHAR *path, const char *data, DWORD len);
int wxb_write_file_bytes_atomic(const WCHAR *path, const char *data, DWORD len);
void wxb_append_file_bytes(const WCHAR *path, const char *data, DWORD len);
int wxb_read_file_bytes(const WCHAR *path, char *out, DWORD max);

#define file_exists wxb_file_exists
#define ensure_dirs wxb_ensure_dirs
#define write_file_bytes wxb_write_file_bytes
#define write_file_bytes_atomic wxb_write_file_bytes_atomic
#define append_file_bytes wxb_append_file_bytes
#define read_file_bytes wxb_read_file_bytes

#endif /* WX_BRIDGE_FILE_IO_H */
