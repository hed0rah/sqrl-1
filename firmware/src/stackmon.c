/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* The stacks' high-water marks, for the console's `stack` (SQRL-1). At power-on, with the interrupts still off,
 * stack_paint fills both stacks with STACK_PAINT (the one it runs on only below the live sp), never
 * their guard bands (app.ld _guard0, _guard1). stack_unused scans from a stack's far end for the first word that is
 * not the pattern: what is above it has been used at some time since. Nothing runs after boot */

extern char _ustack_top[];

#define STACK_PAINT 0xA5C3E17Bu                 /* (no address, small integer or sample a stack would hold) */

static inline __attribute__((always_inline)) void stack_fill(char *lo, char *top, uintptr_t live)   /* [lo, top) */
{
    uint32_t *p = (uint32_t *)(void *)lo, *e = (uint32_t *)(void *)top;
    if (live >= (uintptr_t)lo && live < (uintptr_t)top)
        e = (uint32_t *)((live & ~3u) - 256u);          /* (below the live sp, with room: this frame) */
    for (; p < e; p++)
        *p = STACK_PAINT;
}

static __attribute__((noinline)) void stack_paint(void)
{
    uint32_t here;
    stack_fill(_ustack_lo, _ustack_top, (uintptr_t)&here);   /* (whichever stack the boot runs on keeps its frame) */
    stack_fill(_sstack_lo, _sstack_top, (uintptr_t)&here);
}

static uint32_t stack_unused(const char *lo, const char *top)   /* bytes never written, from the far end */
{
    const uint32_t *p = (const uint32_t *)(const void *)lo, *e = (const uint32_t *)(const void *)top;
    while (p < e && *p == STACK_PAINT)
        p++;
    return (uint32_t)((const char *)(const void *)p - lo);
}
