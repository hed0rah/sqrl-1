/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * tapedly: a tape echo for a shared delay bus, and a pre-delay line for a reverb's send. Integer only, for the
 * FM-1's audio interrupt; the caller owns the buffers (int16, so they can live in whatever memory the platform
 * has spare).
 *
 * The echo is three playback heads on one loop of tape, evenly spaced as on a Space Echo: head 2 twice head 1's
 * distance from the record head, head 3 three times. TIME is where the furthest head that is switched on sits, so
 * the tape's speed follows it: 1+2 gives TIME/2 and TIME, 1+3 gives TIME/3 and TIME, 1+2+3 under a 1/4 gives
 * triplet eighths up to the quarter, and the longest repeat always lands on the tempo's division. The
 * repeats are the heads' sum, fed back through a low-pass (COLOR) and, with SAT up, a soft clip that holds them
 * under a ceiling that falls as SAT rises: loud input squashes, and high feedback sings instead of running away.
 * A change of TIME moves the tape's speed, not the heads: the repeats bend in pitch on the way (0.5x .. 1.5x),
 * ~0.1 s to settle.
 * WOW moves the read point on a slow sine, a faster one and a little filtered noise (wow, flutter, scrape).
 * REV reads the tape backwards in windows of TIME, two readers half a window apart under triangles that sum to 1:
 * REV reverses every repeat (the loop still runs forwards through the heads), ALT feeds the reversed sound back, so
 * the repeats turn round each time. A REV window is at most half the tape (0.74 s on 64K samples).
 *
 * Cost on a PC (tests/test_tapedly.c): one head ~2.5x a plain one-head delay, all three heads with WOW and SAT
 * ~6x, REV on top ~7x; the device's numbers come from its own bench. */
#ifndef TAPEDLY_H
#define TAPEDLY_H
#include <stdint.h>

#ifndef TD_FS
#define TD_FS 44100u
#endif
#define TD_NHEADS 7u                     /* HEADS: 1, 2, 3, 1+2, 2+3, 1+3, 1+2+3 */
enum { TD_FWD, TD_REV, TD_ALT };

typedef struct {
    uint32_t time;                       /* the furthest active head's distance, samples (the tempo's division) */
    uint8_t heads;                       /* 0 .. TD_NHEADS - 1 */
    int32_t fb;                          /* feedback, Q15, 0 .. 32767 */
    int32_t color;                       /* the loop's low-pass, Q15 coefficient (32767: open) */
    uint8_t wow;                         /* 0 .. 127 */
    uint8_t sat;                         /* 0 .. 127; 0 leaves the loop linear */
    uint8_t rev;                         /* TD_FWD, TD_REV, TD_ALT */
} td_param_t;

typedef struct {
    int16_t *buf;
    uint32_t mask, w;
    int32_t dist;                        /* the furthest active head's distance as the tape runs, Q8 (-1: not yet) */
    int32_t hd[3];                       /* each head's distance at the end of the last block, Q8 */
    int32_t mod;                         /* the read point's offset at the end of the last block, Q8, >= 0 */
    int32_t lp, lpe;                     /* the loop's low-pass and the remainder its last step left */
    uint32_t wph, fph, rng;              /* wow's phase, flutter's, the scrape's noise */
    int32_t nz;
    uint8_t hsel, fade;                  /* the heads playing; 1 while fading out to switch them */
    uint8_t rmode;                       /* the REV mode playing (a change fades like HEADS) */
    uint32_t rk[2], rl[2];               /* REV: each reader's place in its window, and that window's length */
    uint32_t rinv[2];                    /* 2^24 / that length: the triangle without a divide a sample */
} td_t;

/* buf: len int16s, len a power of 2 */
void td_init(td_t *t, int16_t *buf, uint32_t len);
/* in: the bus at int16 scale; wet: the echo alone, at int16 scale (the caller sets its level) */
void td_block(td_t *t, const td_param_t *p, const int32_t *in, int32_t *wet, uint32_t n);

/* the pre-delay: a plain line of any length; a change of distance glides (a quarter sample a sample at most) */
typedef struct {
    int16_t *buf;
    uint32_t len, w;
    int32_t d;                           /* distance, Q8 (-1: not yet) */
} tpd_t;

void tpd_init(tpd_t *p, int16_t *buf, uint32_t len);
/* dist in samples, at most len - 2; in and out may be the same array */
void tpd_block(tpd_t *p, uint32_t dist, const int32_t *in, int32_t *out, uint32_t n);

#endif
