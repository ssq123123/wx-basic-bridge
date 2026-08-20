#include "../include/wx_bridge_outbox.h"
#include "../include/wx_bridge_file_io.h"
#include "../include/wx_bridge_paths.h"
#include "../include/wx_bridge_protocol.h"
#include "../include/wx_bridge_send_text.h"
#include "../include/wx_bridge_text.h"

static volatile LONG g_processing = 0;

static char *next_field(char **cursor) {
    char *field;
    if (!cursor || !*cursor) return NULL;
    field = *cursor;
    while (**cursor && **cursor != '\t' && **cursor != '\r' && **cursor != '\n') (*cursor)++;
    if (**cursor == '\t') {
        **cursor = 0;
        (*cursor)++;
    } else if (**cursor) {
        **cursor = 0;
        (*cursor)++;
    }
    return field;
}

void wxb_poll_outbox(void) {
    char data[65536];
    char *cursor;
    char *seq;
    char *type;
    char *to;
    char *content;
    int n;
    int ok;

    if (InterlockedCompareExchange(&g_processing, 1, 0) != 0) return;
    n = read_file_bytes(OUTBOX_FILE, data, sizeof(data));
    if (n <= 0) {
        InterlockedExchange(&g_processing, 0);
        return;
    }
    cursor = data;
    seq = next_field(&cursor);
    type = next_field(&cursor);
    to = next_field(&cursor);
    content = cursor;
    trim_trailing_crlf(content);

    if (!seq || !type || !to || !content || !seq[0]) {
        write_status("malformed outbox command\n");
        if (seq) write_result(seq, 0, "unknown", "malformed outbox command");
    } else if (!str_eq(type, "1")) {
        write_status("unsupported msg type\n");
        write_result(seq, 0, "unknown", "only type 1 (send_text) is supported");
    } else {
        ok = send_text(to, content);
        write_status(ok ? "send_text ok\n" : "send_text failed\n");
        write_result(seq, ok, "send_text", ok ? "send_text ok" : "send_text failed");
    }
    write_file_bytes_atomic(OUTBOX_FILE, "", 0);
    InterlockedExchange(&g_processing, 0);
}
