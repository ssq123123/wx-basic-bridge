#ifndef WX_BRIDGE_WECHAT_STRING_H
#define WX_BRIDGE_WECHAT_STRING_H

#include "wx_bridge_platform.h"

int wxb_wx_read_str(int64_t header, char *out, int max);
void wxb_wx_set_str_arena(int64_t header, const char *utf8, int len,
                          BYTE **pool, BYTE *pool_end);

#define wx_read_str wxb_wx_read_str
#define wx_set_str_arena wxb_wx_set_str_arena

#endif /* WX_BRIDGE_WECHAT_STRING_H */
