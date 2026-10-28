/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * voder: see voder.h. Band-passes are RBJ's (0 dB peak), two in cascade per key; coefficients from
 * polynomial sin and cos, Q30. The buzz is a sawtooth with a polyBLEP at its wrap. */
#include "voder.h"

#define VODER_K_F 152979             /* 2 pi / FS, Q30 */

static const int16_t VODER_LO[VODER_NBAND] = {0, 225, 450, 700, 1000, 1400, 2000, 2700, 3800, 5400};
static const int16_t VODER_HI[VODER_NBAND] = {225, 450, 700, 1000, 1400, 2000, 2700, 3800, 5400, 7500};
/* the stop keys: a decaying noise burst through one band each, centre Hz and Q8 (p diffuse and
 * low, t high, k compact in the middle, as the bursts of labial, alveolar and velar stops) */
static const int16_t VODER_STOP_F[VODER_NSTOP] = {700, 4500, 2000};
static const int16_t VODER_STOP_Q8[VODER_NSTOP] = {180, 256, 400};
#define VODER_STOP_DECAY 32743      /* e^(-1 / 1323) in Q15: the burst decays with 30 ms */

static int64_t voder_cos(int64_t x)
{
    int64_t x2 = (x * x) >> 30, x4 = (x2 * x2) >> 30, x6 = (x4 * x2) >> 30, x8 = (x4 * x4) >> 30;
    return (1LL << 30) - (x2 >> 1) + ((x4 * 44739243) >> 30) - ((x6 * 1491308) >> 30) + ((x8 * 26631) >> 30);
}

static int64_t voder_sin(int64_t x)
{
    int64_t x2 = (x * x) >> 30, x3 = (x2 * x) >> 30, x5 = (x3 * x2) >> 30, x7 = (x5 * x2) >> 30;
    return x - ((x3 * 178956971) >> 30) + ((x5 * 8947849) >> 30) - ((x7 * 213044) >> 30);   /* /6 /120 /5040 */
}

/* RBJ band-pass, constant 0 dB peak gain, centre f Hz, q in Q8 */
static void voder_bp(voder_bp_t *b, int32_t f, int32_t q8)
{
    int64_t w = (int64_t)f * VODER_K_F, sn = voder_sin(w), cs = voder_cos(w);
    int64_t alpha = sn * 128 / (q8 < 16 ? 16 : q8), a0 = (1LL << 30) + alpha;
    b->b0 = (int32_t)((alpha << 30) / a0);
    b->a1 = (int32_t)((-2 * cs * (1LL << 30)) / a0);
    b->a2 = (int32_t)((((1LL << 30) - alpha) << 30) / a0);
}

void voder_bands(voder_src_t *s, uint32_t q8)
{
    uint32_t i;
    for (i = 0; i < VODER_NBAND; i++) {
        int32_t lo = VODER_LO[i] ? VODER_LO[i] : 90, hi = VODER_HI[i], fc = 1;
        while ((fc + 1) * (fc + 1) <= lo * hi)          /* the geometric centre, an integer square root */
            fc++;
        voder_bp(&s->bp[i], fc, (int32_t)((int64_t)fc * 256 / (hi - lo) * q8 / 256));
    }
    for (i = 0; i < VODER_NSTOP; i++)
        voder_bp(&s->stop[i], VODER_STOP_F[i], VODER_STOP_Q8[i]);
}

void voder_init(voder_src_t *s)
{
    uint8_t *p = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        p[i] = 0;
    s->rng = 0x2545F491u;
    s->tilt = 32767;
    voder_bands(s, 256);
}

void voder_pitch(voder_src_t *s, uint32_t f0_q16)
{
    s->inc = (uint32_t)((((uint64_t)f0_q16) << 16) / VODER_FS);
}

static inline int32_t voder_noise(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return (int32_t)*r >> 15;                            /* Q16, [-1, 1) */
}

void voder_source(voder_src_t *s, int32_t *src, uint32_t n, int hiss_held)
{
    uint32_t i, rinc = (s->inc >> 16) ? 0xFFFFFFFFu / (s->inc >> 16) : 0u;   /* the polyBLEP's 1 / inc */
    int32_t hiss = hiss_held ? 32767 : s->hiss;
    for (i = 0; i < n; i++) {
        int32_t saw = (int32_t)(s->ph >> 15) - 65536, t, nz = voder_noise(&s->rng);   /* Q16, [-1, 1) */
        if (rinc) {                                      /* polyBLEP at the wrap (a drop of 2) */
            if (s->ph < s->inc) {
                t = (int32_t)(((s->ph >> 16) * rinc) >> 16);
                saw -= 2 * t - (int32_t)(((int64_t)t * t) >> 16) - 65536;
            } else if (s->ph > 0u - s->inc) {
                t = -(int32_t)((((0u - s->ph) >> 16) * rinc) >> 16);
                saw -= (int32_t)(((int64_t)t * t) >> 16) + 2 * t + 65536;
            }
        }
        s->ph += s->inc;
        s->lp += (int32_t)(((int64_t)(saw - s->lp) * s->tilt) >> 15);
        src[i] = (int32_t)(((int64_t)s->lp * (32767 - hiss) + (int64_t)nz * hiss) >> 15);
    }
}

void voder_key_start(voder_key_t *k, int32_t band, int32_t level)
{
    uint8_t *p = (uint8_t *)k;
    uint32_t i;
    for (i = 0; i < sizeof *k; i++)
        p[i] = 0;
    k->band = band;
    k->level = level;
    k->env = 32767;
}

static inline int32_t voder_bp_tick(const voder_bp_t *b, int32_t x, int32_t *xs, int32_t *ys)
{
    int32_t y = (int32_t)(((int64_t)b->b0 * (x - xs[1]) - (int64_t)b->a1 * ys[0] - (int64_t)b->a2 * ys[1] + (1 << 29)) >> 30);
    xs[1] = xs[0];
    xs[0] = x;
    ys[1] = ys[0];
    ys[0] = y;
    return y;
}

void voder_key_render(const voder_src_t *s, voder_key_t *k, const int32_t *src, int32_t *out, uint32_t n)
{
    uint32_t i;
    if (k->band < VODER_NBAND) {
        const voder_bp_t *b = &s->bp[k->band];
        for (i = 0; i < n; i++) {
            int32_t y = voder_bp_tick(b, src[i], k->x[0], k->y[0]);
            y = voder_bp_tick(b, y, k->x[1], k->y[1]);
            out[i] += (int32_t)(((int64_t)y * k->level) >> 16);   /* Q16 x Q15 -> Q15 */
        }
    } else if (k->band < VODER_NBAND + VODER_NSTOP) {
        const voder_bp_t *b = &s->stop[k->band - VODER_NBAND];
        uint32_t r = 0x9E3779B9u ^ (k->age * 2654435761u);
        for (i = 0; i < n; i++, k->age++) {
            /* the burst: noise under an exponential decay */
            int32_t x = (int32_t)(((int64_t)voder_noise(&r) * k->env) >> 15) * 3;
            int32_t y = voder_bp_tick(b, x, k->x[0], k->y[0]);
            k->env = (k->env * VODER_STOP_DECAY) >> 15;
            y = voder_bp_tick(b, y, k->x[1], k->y[1]);
            out[i] += (int32_t)(((int64_t)y * k->level) >> 16);
        }
    }
}
