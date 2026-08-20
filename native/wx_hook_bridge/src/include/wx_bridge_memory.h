#ifndef WX_BRIDGE_MEMORY_H
#define WX_BRIDGE_MEMORY_H

#include "wx_bridge_platform.h"

int64_t wxb_r64(const void *p);
int32_t wxb_r32(const void *p);
int wxb_is_sane_unix_time_i64(int64_t ts);
int32_t wxb_read_msg_createtime(int64_t msg_base);
void *wxb_alloc_near(void *target, SIZE_T size);
int wxb_is_readable_ptr(int64_t addr, SIZE_T need);
void wxb_w64(void *p, int64_t v);
void wxb_w32(void *p, int32_t v);
void wxb_w8(void *p, BYTE v);
void wxb_copy_bytes(void *dst, const void *src, int len);
int wxb_bytes_equal(const void *a, const void *b, int len);

#define r64 wxb_r64
#define r32 wxb_r32
#define is_sane_unix_time_i64 wxb_is_sane_unix_time_i64
#define read_msg_createtime wxb_read_msg_createtime
#define alloc_near wxb_alloc_near
#define is_readable_ptr wxb_is_readable_ptr
#define w64 wxb_w64
#define w32 wxb_w32
#define w8 wxb_w8
#define copy_bytes wxb_copy_bytes
#define bytes_equal wxb_bytes_equal

#endif /* WX_BRIDGE_MEMORY_H */
