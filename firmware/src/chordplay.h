/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * chordplay: how a struck chord's notes are played, apart from which notes they are. Integer, header only, for the
 * audio interrupt. The notes come in ascending, the lowest first.
 *   STRM  -127 .. 127: the notes start one after another across up to CP_STRUM_MS, low to high when positive, high
 *         to low when negative; 0 starts them together
 *   TOP   0 .. 127: the voicing's balance; the top note rises toward 127 and the inner notes fall (to half at 127),
 *         the lowest keeps the played velocity: the melody rides the chord on its bass
 *   JUST  the chord's tones tuned to just ratios above its root, in 1/16 semitone (the FM-1's pitch step): 5-limit,
 *         the harmonic 7th 7:4 in a chord with a major third, 9:5 otherwise; the 1/16 step leaves at most 3.1 cents
 *         (6:5, 5:3), against equal temperament's 13.7 on the major third and 31 on the harmonic 7th */
#ifndef NE_CHORDPLAY_H
#define NE_CHORDPLAY_H
#include <stdint.h>

#define CP_STRUM_MS 250u

/* the delay of note i of n (ascending), ms */
static inline uint32_t cp_strum_ms(uint32_t i, uint32_t n, int32_t strm)
{
    uint32_t a = (uint32_t)(strm < 0 ? -strm : strm), k;
    if (!a || n < 2u)
        return 0;
    a = a > 127u ? 127u : a;
    k = strm > 0 ? i : n - 1u - i;
    return k * (a * CP_STRUM_MS / 127u) / (n - 1u);
}

/* note i of n's velocity (1 .. 127) under TOP */
static inline uint8_t cp_top_vel(uint32_t i, uint32_t n, uint8_t vel, uint8_t top)
{
    int32_t v = vel, t = top > 127 ? 127 : top;
    if (!t || n < 2u)
        return vel;
    if (i == n - 1u)
        v += (127 - v) * t / 127;
    else if (i)
        v -= v * t / 254;
    return (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}

/* the just offset, 1/16 semitone, of a tone `iv` semitones above the chord's root; mask: the chord's tones (bit i:
 * i semitones above the root, mod 12), which tells a dominant's 7th from a minor one */
static inline int8_t cp_just16(int32_t iv, uint16_t mask)
{
    static const int8_t J[12] = {
        0,                               /* 1:1 */
        2,                               /* 16:15, +11.7 c */
        1,                               /* 9:8, +3.9 */
        3,                               /* 6:5, +15.6 */
        -2,                              /* 5:4, -13.7 */
        0,                               /* 4:3, -2.0 */
        -3,                              /* 7:5, -17.5 (the diminished chords' 5:6:7) */
        0,                               /* 3:2, +2.0 */
        2,                               /* 8:5, +13.7 */
        -3,                              /* 5:3, -15.6 */
        3,                               /* 9:5, +17.6 (a minor chord's 7th) */
        -2,                              /* 15:8, -11.7 */
    };
    int32_t pc = ((iv % 12) + 12) % 12;
    if (pc == 10 && ((mask >> 4) & 1u))
        return -5;                       /* 7:4, -31.2: the dominant's 7th */
    return J[pc];
}

#endif
