#ifndef WX_BRIDGE_NATIVE_CALL_H
#define WX_BRIDGE_NATIVE_CALL_H

#include "wx_bridge_platform.h"

typedef int64_t (*FnCall1)(int64_t rcx);
typedef int64_t (*FnCall2)(int64_t rcx, int64_t rdx, int64_t r8, int64_t r9, int64_t stack5);
typedef int64_t (*FnCall6)(int64_t rcx, int64_t rdx, int64_t r8, int64_t r9, int64_t stack5, int64_t stack6);
typedef int64_t (*FnCall3)(int64_t rcx, int64_t rdx);
typedef int64_t (*FnCall3Args)(int64_t rcx, int64_t rdx, int64_t r8);
typedef int64_t (*FnCall4)(int64_t rcx, int64_t rdx, int64_t r8, int64_t r9);
typedef int64_t (__fastcall *FnRTDynamicCast)(int64_t inptr,
                                              int32_t vfdelta,
                                              int64_t src_type,
                                              int64_t dst_type,
                                              int32_t is_reference);

#endif /* WX_BRIDGE_NATIVE_CALL_H */
