/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * ensemble: see ensemble.h */
#include "ensemble.h"

#define EN_THIRD 1431655765u                /* a third of a cycle, 2^32 / 3 */
#define EN_VIB_INC 613566u                  /* ENS's vibrato, 6.3 Hz at 44.1 kHz */
#define EN_CH_BASE (400 << 8)               /* CHOR: 9 ms, Q8, swept up to 762 samples more at 127 (the bus as it was) */
#define EN_ENS_BASE (309 << 8)               /* ENS: 7 ms, +-175 samples slow at 127 (+-4 ms), +-4 vibrato */
#define EN_DM_BASE (441 << 8)               /* DIM: 10 ms, +-24 samples at 127 */

/* sin of a uint32 phase, Q15, a parabola (a sweep: its harmonics are inaudible as motion) */
static inline int32_t en_psin(uint32_t ph)
{
    int32_t y = (int32_t)ph >> 16, a = y < 0 ? -y : y;
    return (y * (32768 - a)) >> 13;
}
static inline int32_t en_tap(const en_t *e, int32_t d)
{
    uint32_t i = (uint32_t)d >> 8;
    int32_t f = d & 255, s0 = e->buf[(e->w - i) & e->mask], s1 = e->buf[(e->w - i - 1u) & e->mask];
    return s0 + (((s1 - s0) * f) >> 8);
}

void en_init(en_t *e, int16_t *buf, uint32_t len)
{
    uint32_t i;
    e->buf = buf;
    e->mask = len - 1u;
    for (i = 0; i < len; i++)
        buf[i] = 0;
    e->w = 0;
    e->ph = 0;
    e->vph = 0;
    e->mode = EN_CHOR;
    e->fade = 0;
}

void en_block(en_t *e, uint8_t mode, uint32_t inc, uint8_t depth, const int32_t *in, int32_t *mid, int32_t *side,
              uint32_t n)
{
    uint32_t i;
    int32_t g = 32767, gstep = 0, d = depth > 127 ? 127 : depth;
    int32_t chd = d * 6, ens = d * 352, vib = d * 8, dim = d * 48;
    mode = mode < EN_NMODES ? mode : EN_CHOR;
    if (e->fade) {                       /* a change of mode: out over one block, switch, in over the next */
        e->fade = 0;
        e->mode = mode;
        g = 0;
        gstep = 32767 / (int32_t)n;
    } else if (mode != e->mode) {
        e->fade = 1;
        gstep = -32767 / (int32_t)n;
    }
    for (i = 0; i < n; i++) {
        int32_t m, s = 0, x = in[i];
        e->buf[e->w & e->mask] = (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
        e->ph += inc;
        if (e->mode == EN_ENS) {
            int32_t t0, t1, t2;
            e->vph += EN_VIB_INC;
            t0 = en_tap(e, EN_ENS_BASE + ((en_psin(e->ph) * ens) >> 15) + ((en_psin(e->vph) * vib) >> 15));
            t1 = en_tap(e, EN_ENS_BASE + ((en_psin(e->ph + EN_THIRD) * ens) >> 15) + ((en_psin(e->vph + EN_THIRD) * vib) >> 15));
            t2 = en_tap(e, EN_ENS_BASE + ((en_psin(e->ph + 2u * EN_THIRD) * ens) >> 15) + ((en_psin(e->vph + 2u * EN_THIRD) * vib) >> 15));
            m = ((t0 + t1 + t2) * 10923) >> 15;
            s = (t0 - t1) >> 1;
        } else if (e->mode == EN_DIM) {
            int32_t o = (en_psin(e->ph) * dim) >> 15, t0 = en_tap(e, EN_DM_BASE + o), t1 = en_tap(e, EN_DM_BASE - o);
            m = (t0 + t1) >> 1;
            s = (t0 - t1) >> 1;
        } else
            m = en_tap(e, EN_CH_BASE + (((en_psin(e->ph) + 32768) * chd) >> 8));
        if (gstep) {
            m = (m * g) >> 15;
            s = (s * g) >> 15;
            g += gstep;
            g = g < 0 ? 0 : g > 32767 ? 32767 : g;
        }
        mid[i] = m;
        side[i] = s;
        e->w++;
    }
}
