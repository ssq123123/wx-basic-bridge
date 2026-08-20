#ifndef WX_BRIDGE_OUTBOX_H
#define WX_BRIDGE_OUTBOX_H

void wxb_poll_outbox(void);

#define poll_outbox wxb_poll_outbox

#endif /* WX_BRIDGE_OUTBOX_H */
