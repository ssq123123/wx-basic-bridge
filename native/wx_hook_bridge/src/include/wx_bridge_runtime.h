#ifndef WX_BRIDGE_RUNTIME_H
#define WX_BRIDGE_RUNTIME_H

#include "wx_bridge_platform.h"

HINSTANCE wxb_bridge_instance(void);
void wxb_set_bridge_instance(HINSTANCE instance);

HMODULE wxb_weixin_module(void);
HMODULE wxb_ensure_weixin_module(void);

const char *wxb_self_wxid(void);
int wxb_self_wxid_known(void);
void wxb_learn_self_wxid(const char *wxid);

#endif /* WX_BRIDGE_RUNTIME_H */
