#include "../include/wx_bridge_memory.h"

#define WX_MSG_TS_OFF 0x110

int64_t wxb_r64(const void *p) {
    int64_t v = 0;
    if (!p) return 0;
    __try { v = *(volatile const int64_t *)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

int32_t wxb_r32(const void *p) {
    int32_t v = 0;
    if (!p) return 0;
    __try { v = *(volatile const int32_t *)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

int wxb_is_sane_unix_time_i64(int64_t ts) {
    return ts >= 946684800LL && ts <= 2147483647LL;
}

int32_t wxb_read_msg_createtime(int64_t msg_base) {
    int32_t ts32;
    int64_t ts64;
    if (!msg_base) return 0;

    ts32 = wxb_r32((void *)(msg_base + 0x124));
    if (wxb_is_sane_unix_time_i64((int64_t)ts32)) return ts32;

    ts64 = wxb_r64((void *)(msg_base + WX_MSG_TS_OFF));
    if (wxb_is_sane_unix_time_i64(ts64)) return (int32_t)ts64;

    ts32 = (int32_t)(uint32_t)ts64;
    if (wxb_is_sane_unix_time_i64((int64_t)ts32)) return ts32;

    ts32 = (int32_t)((uint64_t)ts64 >> 32);
    if (wxb_is_sane_unix_time_i64((int64_t)ts32)) return ts32;
    return 0;
}

void *wxb_alloc_near(void *target, SIZE_T size) {
    SYSTEM_INFO si;
    uint64_t gran;
    uint64_t t;
    uint64_t dist;
    GetSystemInfo(&si);
    gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    t = (uint64_t)target;
    for (dist = gran; dist < 0x7FFF0000ULL; dist += gran) {
        uint64_t down = (t > dist) ? ((t - dist) & ~(gran - 1)) : 0;
        if (down) {
            void *p = VirtualAlloc((void *)down, size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
        {
            uint64_t up = (t + dist) & ~(gran - 1);
            void *p = VirtualAlloc((void *)up, size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return NULL;
}

int wxb_is_readable_ptr(int64_t addr, SIZE_T need) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = (uintptr_t)addr;
    uintptr_t end;
    if (a < 0x10000 || need == 0) return 0;
    if (VirtualQuery((void *)a, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || mbi.Protect == PAGE_NOACCESS) return 0;
    end = a + need;
    if (end < a) return 0;
    return end <= (uintptr_t)mbi.BaseAddress + (uintptr_t)mbi.RegionSize;
}

void wxb_w64(void *p, int64_t v) {
    if (!p) return;
    __try { *(volatile int64_t *)p = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void wxb_w32(void *p, int32_t v) {
    if (!p) return;
    __try { *(volatile int32_t *)p = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void wxb_w8(void *p, BYTE v) {
    if (!p) return;
    __try { *(volatile BYTE *)p = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void wxb_copy_bytes(void *dst, const void *src, int len) {
    __try {
        BYTE *d = (BYTE *)dst;
        const BYTE *s = (const BYTE *)src;
        for (int i = 0; i < len; i++) d[i] = s[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

int wxb_bytes_equal(const void *a, const void *b, int len) {
    int ok = 1;
    __try {
        const BYTE *pa = (const BYTE *)a;
        const BYTE *pb = (const BYTE *)b;
        for (int i = 0; i < len; i++) {
            if (pa[i] != pb[i]) {
                ok = 0;
                break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    return ok;
}
