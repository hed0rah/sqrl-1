/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * voder: Homer Dudley's Voder (Bell Labs, 1939) as a playable filter bank. One source per part, a
 * buzz (a band-limited sawtooth: the relaxation oscillator) or a hiss, chosen by the wrist bar
 * (here: the HISS key and a mix), at a pitch from the foot pedal (here: a knob). Ten band-pass
 * filters, one per key; three stop keys that sound a short burst through fixed bands. Integer only,
 * no allocation.
 *
 * Bands, Hz: 0-225 225-450 450-700 700-1000 1000-1400 1400-2000 2000-2700 2700-3800 3800-5400
 * 5400-7500 (Dudley, Riesz and Watkins, "A synthetic speaker", J. Franklin Inst. 227, 1939). */
#ifndef VODER_H
#define VODER_H
#include <stdint.h>

#define VODER_FS 44100
#define VODER_NBAND 10
#define VODER_NSTOP 3                /* p t k */

typedef struct { int32_t b0, a1, a2; } voder_bp_t;   /* y = b0 (x - x2) - a1 y1 - a2 y2, Q30 */

typedef struct {                     /* one part's source, shared by its keys */
    uint32_t ph, inc;                /* sawtooth phase and increment (Q32) */
    uint32_t rng;
    int32_t hiss;                    /* the mix: 0 buzz .. 32767 hiss, Q15 */
    int32_t tilt;                    /* one-pole lowpass on the buzz, Q15 (32767 = none) */
    int32_t lp;
    voder_bp_t bp[VODER_NBAND];      /* the bands' coefficients, from voder_bands() */
    voder_bp_t stop[VODER_NSTOP];    /* the stop keys' bands */
} voder_src_t;

typedef struct {                     /* one held key */
    int32_t band;                    /* 0..9 a band, 10..12 a stop (p t k) */
    int32_t level;                   /* Q15, from the key's velocity */
    int32_t x[2][2], y[2][2];        /* two cascaded band-passes: 4th order */
    uint32_t age;                    /* samples since the key went down (stops) */
    int32_t env;                     /* the stop's burst envelope, Q15 */
} voder_key_t;

void voder_init(voder_src_t *s);
/* the band-pass coefficients for a sharpness q (Q8: 256 = the band's own width, 512 = half as wide) */
void voder_bands(voder_src_t *s, uint32_t q8);
void voder_pitch(voder_src_t *s, uint32_t f0_q16);
/* n samples of the source into src (Q16); hiss_held: the HISS key is down */
void voder_source(voder_src_t *s, int32_t *src, uint32_t n, int hiss_held);
void voder_key_start(voder_key_t *k, int32_t band, int32_t level);
/* a key's band (or stop) of the source, n samples, Q15, added to out */
void voder_key_render(const voder_src_t *s, voder_key_t *k, const int32_t *src, int32_t *out, uint32_t n);

#endif
