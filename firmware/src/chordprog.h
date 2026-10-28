/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * chordprog: CHORD's PROG, a progression under the held chord. After Ornament & Crime's Acid Curds (Patrick Dowling,
 * Tim Churches, Max Stadler, MIT), whose progressions of scale degrees step on a clock in a direction; written fresh
 * for SQRL-1's CHORD (chord.c, seq.c).
 *
 * The root you hold or latch is the key: the progression's degree 0. Playing, the chord moves to the next degree every
 * EVERY (on the click's grid, from the transport's start); stopped, each root pressed plays the next. A degree moves
 * the root along the part's scale (CHR: the major scale of ROOT) by the scale's own intervals, so the chord built on it
 * is the scale's chord on that degree, and a root off the scale keeps its distance.
 *
 *   PROG  OFF, or a progression (CPG below)
 *   EVERY HALF, 1, 2, 4 bars
 *   DIR   FWD, REV, PEND (to the end and back), RAND, BROWN (a step either way, or stay)
 *   LEN   ALL (the progression's own length) or 1..12 chords of it, round */
#ifndef CHORDPROG_H
#define CHORDPROG_H
#include <stdint.h>

#define CPG_MAX 12
static const char *const N_CPG[] = {"OFF", "POP", "50S", "JAZZ", "BLUES", "ANDAL", "PACH", "5THS", "SAD", "ROYAL", 0};
#define CPG_N (sizeof N_CPG / sizeof N_CPG[0] - 1u)
static const int8_t CPG[CPG_N][CPG_MAX] = {
    {0},
    {0, 4, 5, 3},                                       /* I V vi IV */
    {0, 5, 3, 4},                                       /* I vi IV V */
    {1, 4, 0, 0},                                       /* ii V I I */
    {0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4},               /* the twelve bars */
    {0, -1, -2, -3},                                    /* i VII VI V in a minor scale (the Andalusian cadence) */
    {0, 4, 5, 2, 3, 0, 3, 4},                           /* Pachelbel's: I V vi iii IV I IV V */
    {0, 3, 6, 2, 5, 1, 4, 0},                           /* down the scale's fifths: I IV vii iii vi ii V I */
    {5, 3, 0, 4},                                       /* vi IV I V */
    {3, 4, 2, 5},                                       /* IV V iii vi, the "royal road" */
};
static const uint8_t CPG_LEN[CPG_N] = {1, 4, 4, 4, 12, 4, 8, 8, 4, 4};
static const char *const N_CPG_EVERY[] = {"HALF", "1 BAR", "2 BAR", "4 BAR", 0};
static const char *const N_CPG_DIR[] = {"FWD", "REV", "PEND", "RAND", "BROWN", 0};
static const char *const N_CPG_LEN[] = {"ALL", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", 0};
enum { CPG_FWD, CPG_REV, CPG_PEND, CPG_RAND, CPG_BROWN };

typedef struct {
    uint8_t i;                                          /* the chord now, 0..len-1 */
    int8_t way;                                         /* PEND: +1 or -1 */
    uint8_t started;                                    /* stopped: the first press plays chord 0 */
    uint32_t rng;
} cpg_t;

static uint32_t cpg_len(uint32_t prog, uint32_t len)
{
    prog %= CPG_N;
    return len ? (len > CPG_MAX ? CPG_MAX : len) : CPG_LEN[prog];
}
static int32_t cpg_degree(uint32_t prog, uint32_t i)
{
    prog %= CPG_N;
    return CPG[prog][i % CPG_LEN[prog]];
}
static void cpg_start(cpg_t *s)
{
    s->i = 0;
    s->way = 1;
    s->started = 1;
}
/* the next chord's place, in DIR among the first len */
static void cpg_next(cpg_t *s, uint32_t dir, uint32_t len)
{
    uint32_t r;
    if (len < 2u) {
        s->i = 0;
        return;
    }
    s->rng = s->rng * 1664525u + 1013904223u;
    r = s->rng >> 16;
    switch (dir) {
    case CPG_REV:
        s->i = (uint8_t)((s->i + len - 1u) % len);
        break;
    case CPG_PEND:
        if ((s->way > 0 && s->i + 1u >= len) || (s->way < 0 && s->i == 0u))
            s->way = (int8_t)-s->way;
        s->i = (uint8_t)(s->i + s->way);
        break;
    case CPG_RAND:
        s->i = (uint8_t)((s->i + 1u + r % (len - 1u)) % len);   /* (never the same chord twice) */
        break;
    case CPG_BROWN:
        s->i = (uint8_t)((s->i + len + r % 3u - 1u) % len);
        break;
    default:
        s->i = (uint8_t)((s->i + 1u) % len);
        break;
    }
    if (s->i >= len)
        s->i = 0;
}
/* root r moved d degrees along the scale (mask over 12 from key, CHR: give the major mask), by the scale's intervals
 * from the degree at or below r */
static int32_t cpg_root(uint32_t mask, int32_t key, int32_t r, int32_t d)
{
    int32_t pcs[12], n = 0, i, rel = ((r - key) % 12 + 12) % 12, at = 0, o, to;
    for (i = 0; i < 12; i++)
        if ((mask >> i) & 1u)
            pcs[n++] = i;
    if (!n || !d)
        return r;
    for (i = 0; i < n; i++)
        if (pcs[i] <= rel)
            at = i;
    to = at + d;
    o = to >= 0 ? to / n : -((-to + n - 1) / n);
    return r + pcs[to - o * n] + 12 * o - pcs[at];
}
#endif
