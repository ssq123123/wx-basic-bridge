#ifndef WX_BRIDGE_SEND_TEXT_H
#define WX_BRIDGE_SEND_TEXT_H

int wxb_send_text(const char *wxid, const char *content);

#define send_text wxb_send_text

#endif /* WX_BRIDGE_SEND_TEXT_H */
