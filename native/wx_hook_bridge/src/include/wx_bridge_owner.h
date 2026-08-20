#ifndef WX_BRIDGE_OWNER_H
#define WX_BRIDGE_OWNER_H

int wxb_claim_worker_owner(char *token, int max);
int wxb_worker_owner_current(const char *token);
void wxb_release_worker_owner(const char *token);

#define claim_worker_owner wxb_claim_worker_owner
#define worker_owner_current wxb_worker_owner_current
#define release_worker_owner wxb_release_worker_owner

#endif /* WX_BRIDGE_OWNER_H */
