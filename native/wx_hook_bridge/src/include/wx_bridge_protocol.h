#ifndef WX_BRIDGE_PROTOCOL_H
#define WX_BRIDGE_PROTOCOL_H

void wxb_write_status(const char *text);
void wxb_write_result(const char *seq, int ok, const char *command, const char *message);

#define write_status wxb_write_status
#define write_result wxb_write_result

#endif /* WX_BRIDGE_PROTOCOL_H */
