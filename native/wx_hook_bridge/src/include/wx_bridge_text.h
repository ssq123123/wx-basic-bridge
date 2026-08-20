#ifndef WX_BRIDGE_TEXT_H
#define WX_BRIDGE_TEXT_H

#include "wx_bridge_platform.h"

void wxb_trim_trailing_crlf(char *s);
void wxb_i64toa10(int64_t v, char *out);
void wxb_append_raw(char **p, char *end, const char *s);
void wxb_append_escaped(char **p, char *end, const char *s);
int wxb_str_eq(const char *a, const char *b);
int wxb_is_chatroom_id(const char *s);
int wxb_is_wxid_like(const char *s);
void wxb_safe_copy(char *dst, int max, const char *src);
int wxb_extract_group_sender(const char *content, char *sender, int max);

#define trim_trailing_crlf wxb_trim_trailing_crlf
#define i64toa10 wxb_i64toa10
#define append_raw wxb_append_raw
#define append_escaped wxb_append_escaped
#define str_eq wxb_str_eq
#define is_chatroom_id wxb_is_chatroom_id
#define is_wxid_like wxb_is_wxid_like
#define safe_copy wxb_safe_copy
#define extract_group_sender wxb_extract_group_sender

#endif /* WX_BRIDGE_TEXT_H */
