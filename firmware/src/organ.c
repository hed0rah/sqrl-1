/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * organ: see organ.h */
#include "organ.h"

static const uint8_t ORG_H[ORG_NBARS] = {1, 3, 2, 4, 6, 8, 10, 12, 16};   /* each bar's harmonic of 16' */
static const int32_t ORG_G[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};   /* 3 dB a step, Q15 */
static const uint8_t ORG_LIM[ORG_NLEV] = {16, 8, 4};                        /* each table's top harmonic */
#define ORG_SUM_STEP 128u               /* cells summed a call: the rebuild's work spread over blocks */
#define ORG_SCALE_STEP 512u             /* cells scaled a call */
#define ORG_FLOOR 65534                  /* the scale's floor: two bars' peak (a lone bar plays at half) */
/* the note's (8') increment under which a table's top harmonic stays below 18 kHz at 44.1 kHz:
 * 8 x f < 18000 for [0], 4 x f for [1] */
#define ORG_INC0 219130984u
#define ORG_INC1 438261968u
#define ORG_PERC0 20000                  /* the percussion's start, Q15 of a full bar */
#define ORG_PFAST 32648                  /* its decay every 32 samples: ~0.2 s */
#define ORG_PSLOW 32743                  /* ~1 s */
#define ORG_CLK0 9000                    /* the click's start */
#define ORG_CLKD 32034                   /* its decay a sample: ~1 ms */
#define ORG_OUT 13107                    /* table + percussion into the int16 scale: 0.4 */

/* a bar's harmonic in table l: halved while above the table's top (the foldback), dropped if odd there */
static uint32_t org_fold(uint32_t h, uint32_t l)
{
    while (h > ORG_LIM[l])
        h = h & 1u ? 0u : h >> 1;
    return h;
}

/* one cell of table l: the bars' sum, before the scale */
static int32_t org_sum(const uint8_t *bars, uint32_t l, uint32_t i)
{
    uint32_t b;
    int32_t a = 0;
    for (b = 0; b < ORG_NBARS; b++) {
        uint32_t h = org_fold(ORG_H[b], l);
        if (h)
            a += (ORG_G[bars[b]] * ORG_SINE[(i * h) & (ORG_TLEN - 1u)]) >> 15;
    }
    return a;
}

int org_tables_set(org_tables_t *o, const uint8_t bars[ORG_NBARS])
{
    uint32_t b, n, back = o->front ^ 1u, all = ORG_NLEV * ORG_TLEN;
    int same_front = 1, same_want = 1;
    uint8_t w[ORG_NBARS];
    for (b = 0; b < ORG_NBARS; b++) {
        w[b] = bars[b] > 8u ? 8u : bars[b];
        same_front &= o->bars[b] == w[b];
        same_want &= o->want[b] == w[b];
    }
    if (!o->busy || !same_want) {        /* nothing under way, or the bars moved again: (re)start the build */
        if (same_front) {
            o->busy = 0;
            return 0;
        }
        for (b = 0; b < ORG_NBARS; b++)
            o->want[b] = w[b];
        o->busy = 1;
        o->at = 0;
        o->peak = 0;
    }
    /* first the sums, kept at 1/16 (they reach 294903) with the peak noted; then the scale from the three
     * tables' highest peak (a fold can add to a peak), the same for all three so the keyboard stays even */
    for (n = 0; n < (o->at < all ? ORG_SUM_STEP : ORG_SCALE_STEP) && o->at < 2u * all; n++, o->at++) {
        uint32_t c = o->at % all, l = c / ORG_TLEN, i = c % ORG_TLEN;
        if (o->at < all) {
            int32_t a = org_sum(o->want, l, i), m = a < 0 ? -a : a;
            o->peak = m > o->peak ? m : o->peak;
            o->t[back][l][i] = (int16_t)(a >> 4);
        } else {
            uint32_t k = 536854528u / (uint32_t)(o->peak > ORG_FLOOR ? o->peak : ORG_FLOOR);   /* 32767 << 14 over it */
            int32_t a = (int32_t)(((int64_t)o->t[back][l][i] * 16 * (int64_t)k) >> 14);
            o->t[back][l][i] = (int16_t)(a > 32767 ? 32767 : a < -32767 ? -32767 : a);
        }
    }
    if (o->at < 2u * all)
        return 0;
    o->front = (uint8_t)back;            /* the new registration sounds from the next sample */
    for (b = 0; b < ORG_NBARS; b++)
        o->bars[b] = o->want[b];
    o->busy = 0;
    return 1;
}

void org_on(org_voice_t *v, int perc, int retrig)
{
    if (!retrig) {                       /* the wheels never stop: a voice starts wherever its wheel is */
        v->rng = v->rng * 1664525u + 1013904223u;
        v->ph = v->rng;
    }
    v->perc = perc ? ORG_PERC0 : 0;
    v->clk = ORG_CLK0;
    v->nlp = 0;
}

void org_render(const org_tables_t *o, org_voice_t *v, uint32_t inc, uint8_t perc, int32_t *out, uint32_t n)
{
    const int16_t *t = o->t[o->front][inc < ORG_INC0 ? 0 : inc < ORG_INC1 ? 1 : 2];
    uint32_t i, ph = v->ph, hi = inc >> 1, pm = perc == ORG_P3 || perc == ORG_P3S ? 6u : 4u;
    int32_t pd = perc == ORG_P2S || perc == ORG_P3S ? ORG_PSLOW : ORG_PFAST, pl = perc ? v->perc : 0;
    for (i = 0; i < n; i++) {
        uint32_t x = ph >> (32 - ORG_TBITS);
        int32_t f = (int32_t)(ph >> (24 - ORG_TBITS)) & 255, s0 = t[x], s1 = t[(x + 1u) & (ORG_TLEN - 1u)];
        int32_t y = s0 + (((s1 - s0) * f) >> 8);
        if (pl)
            y += (pl * ORG_SINE[((ph * pm) >> (32 - ORG_TBITS))]) >> 15;
        if (v->clk) {                    /* the contacts: a few ms of bright noise */
            int32_t r;
            v->rng = v->rng * 1664525u + 1013904223u;
            r = (int32_t)(v->rng >> 17) - 16384;
            y += ((r - v->nlp) * v->clk) >> 14;
            v->nlp += (r - v->nlp) >> 2;
            v->clk = (v->clk * ORG_CLKD) >> 15;
        }
        out[i] += (y * ORG_OUT) >> 15;
        ph += hi;
        if ((i & 31u) == 31u && pl)
            pl = (pl * pd) >> 15;
    }
    v->ph = ph;
    v->perc = pl;
}
