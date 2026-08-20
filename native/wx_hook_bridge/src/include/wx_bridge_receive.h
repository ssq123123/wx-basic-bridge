#ifndef WX_BRIDGE_RECEIVE_H
#define WX_BRIDGE_RECEIVE_H

int wxb_install_recv_hook(void);
void wxb_uninstall_recv_hook(void);
int wxb_recv_hook_installed(void);
int wxb_recv_hook_site_ready(void);

#define install_recv_hook wxb_install_recv_hook
#define uninstall_recv_hook wxb_uninstall_recv_hook
#define recv_hook_installed wxb_recv_hook_installed
#define recv_hook_site_ready wxb_recv_hook_site_ready

#endif /* WX_BRIDGE_RECEIVE_H */
