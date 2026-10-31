/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* 64-bit division for the compiler's helper calls: the speech engines (TALK, SYLLABLE, VODER) divide 64-bit
 * values at control rate and at the oscillator's edges, and the freestanding build links no libgcc. Restoring
 * shift-subtract, at most 64 steps: fine where it runs, never per sample of a whole block. A divide by 0 gives
 * all ones (the firmware does not trap on one: hal/fm1_irq.h). Host builds use the compiler's own. */
#if !defined(__x86_64__) && !defined(__aarch64__) && !defined(__i386__)
static unsigned long long div64_u(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
    unsigned long long q = 0, r = 0;
    int i;
    if (!d) {
        if (rem)
            *rem = n;
        return ~0ull;
    }
    if (!(d >> 32) && !(n >> 32)) {                    /* both fit 32 bits: the hardware divide */
        unsigned a = (unsigned)n, b = (unsigned)d;
        if (rem)
            *rem = a % b;
        return a / b;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return div64_u(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    div64_u(n, d, &r);
    return r;
}
long long __divdi3(long long n, long long d)
{
    int neg = (n < 0) != (d < 0);
    unsigned long long q = div64_u(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n,
                                   d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, 0);
    return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    div64_u(n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n,
            d < 0 ? 0ull - (unsigned long long)d : (unsigned long long)d, &r);
    return n < 0 ? -(long long)r : (long long)r;   /* C: the remainder takes the dividend's sign */
}
#endif
