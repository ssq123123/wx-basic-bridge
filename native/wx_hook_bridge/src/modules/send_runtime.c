#include "../include/wx_bridge_platform.h"

#define RETAINED_ARENAS 16

static void *g_arenas[RETAINED_ARENAS] = {0};
static int g_arena_index = 0;

void wxb_retain_send_arena(void *arena) {
    void *old;
    if (!arena) return;
    old = g_arenas[g_arena_index];
    if (old) VirtualFree(old, 0, MEM_RELEASE);
    g_arenas[g_arena_index] = arena;
    g_arena_index = (g_arena_index + 1) % RETAINED_ARENAS;
}
