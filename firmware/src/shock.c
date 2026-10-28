/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * shock: see shock.h. */
#include "shock.h"
#include "shock_table.h"

#define SHOCK_Q (1.0f / 32767.0f)

/* the row pair around sigma and the fraction between them */
static void shock_rows(float sigma, int *r0, float *fr)
{
    int r = 0;
    if (sigma <= 0.0f) {
        *r0 = 0;
        *fr = 0.0f;
        return;
    }
    if (sigma >= SHOCK_SIG[SHOCK_ROWS - 1]) {
        *r0 = SHOCK_ROWS - 2;
        *fr = 1.0f;
        return;
    }
    while (r < SHOCK_ROWS - 2 && SHOCK_SIG[r + 1] <= sigma)
        r++;
    *r0 = r;
    *fr = (sigma - SHOCK_SIG[r]) / (SHOCK_SIG[r + 1] - SHOCK_SIG[r]);
}

/* the wave at phase ph from rows r0, r0 + 1 (fr between): the second half mirrored, W(2pi - x) = -W(x) */
static inline float shock_read(int r0, float fr, uint32_t ph)
{
    uint32_t half = ph >> 31, u = half ? 0u - ph : ph, i = u >> (31 - SHOCK_BITS);   /* u in [0, 2^31]: 0 .. pi */
    float f = (float)(u & ((1u << (31 - SHOCK_BITS)) - 1u)) * (1.0f / (float)(1u << (31 - SHOCK_BITS))), a, b;
    const int16_t *t0 = SHOCK_TAB[r0], *t1 = SHOCK_TAB[r0 + 1];
    if (i >= SHOCK_N) {                  /* exactly pi */
        i = SHOCK_N - 1;
        f = 1.0f;
    }
    a = (float)t0[i] + f * (float)(t0[i + 1] - t0[i]);
    b = (float)t1[i] + f * (float)(t1[i + 1] - t1[i]);
    a = (a + fr * (b - a)) * SHOCK_Q;
    return half ? -a : a;
}

float shock_wave(float sigma, uint32_t ph)
{
    int r0;
    float fr;
    shock_rows(sigma, &r0, &fr);
    return shock_read(r0, fr, ph);
}

void shock_init(shock_t *s, uint32_t seed)
{
    s->ph = s->sub = 0;
    s->lp = 0.0f;
    s->rng = seed ? seed : 0x2545F491u;
}

void shock_note(shock_t *s)
{
    s->ph = s->sub = 0;
}

void shock_render(shock_t *s, const shock_par *p, uint32_t inc, float *out, int n)
{
    int r0, i;
    float fr, w, dt = (float)inc * (1.0f / 4294967296.0f), rdt = dt > 0.0f ? 1.0f / dt : 0.0f;
    float lp = s->lp, a = p->loss < 0.0f ? 0.0f : p->loss > 1.0f ? 1.0f : p->loss;
    uint32_t ph = s->ph, sub = s->sub, rng = s->rng, sinc = inc >> 1;
    shock_rows(p->sigma, &r0, &fr);
    /* the seam's half-step, sin(phi_s): the BLEP's weight (0 below the shock) */
    w = p->blep ? ((float)SHOCK_BLEP[r0] + fr * (float)(SHOCK_BLEP[r0 + 1] - SHOCK_BLEP[r0])) * SHOCK_Q : 0.0f;
    for (i = 0; i < n; i++) {
        float y = shock_read(r0, fr, ph);
        if (w > 0.0f) {                  /* the shock at the seam, band-limited as the reference does */
            float t = (float)ph * (1.0f / 4294967296.0f);
            if (t < dt) {
                float x = t * rdt;
                y += w * (x + x - x * x - 1.0f);
            } else if (t > 1.0f - dt) {
                float x = (t - 1.0f) * rdt;
                y += w * (x * x + x + x + 1.0f);
            }
        }
        if (p->sub > 0.0f)               /* the sigma 0 row is the sine */
            y += p->sub * shock_read(0, 0.0f, sub);
        if (p->breath > 0.0f) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            y += p->breath * (float)(int32_t)rng * (1.0f / 2147483648.0f);
        }
        lp += a * (y - lp);
        out[i] = lp;
        ph += inc;
        sub += sinc;
    }
    s->ph = ph;
    s->sub = sub;
    s->lp = lp;
    s->rng = rng;
}
