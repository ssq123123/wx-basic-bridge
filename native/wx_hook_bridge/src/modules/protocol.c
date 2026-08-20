#include "../include/wx_bridge_protocol.h"
#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_paths.h"
#include "../include/wx_bridge_text.h"

void wxb_write_status(const char *text) {
    if (!text || !wxb_paths_ready()) return;
    wxb_write_file_bytes_atomic(STATUS_FILE, text, (DWORD)lstrlenA(text));
}

void wxb_write_result(const char *seq, int ok, const char *command, const char *message) {
    WCHAR path[1024];
    char json[4096];
    char *cursor = json;
    char *end = json + sizeof(json) - 1;
    if (!seq || !seq[0] || !wxb_paths_ready()) return;
    wsprintfW(path, L"%s\\result_%S.json", CONTROL_DIR, seq);
    append_raw(&cursor, end, "{\"ok\":");
    append_raw(&cursor, end, ok ? "true" : "false");
    append_raw(&cursor, end, ",\"seq\":\"");
    append_escaped(&cursor, end, seq);
    append_raw(&cursor, end, "\",\"command\":\"");
    append_escaped(&cursor, end, command ? command : "");
    append_raw(&cursor, end, "\",\"message\":\"");
    append_escaped(&cursor, end, message ? message : "");
    append_raw(&cursor, end, "\"}");
    *cursor = 0;
    wxb_write_file_bytes_atomic(path, json, (DWORD)(cursor - json));
}
