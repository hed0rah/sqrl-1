/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * lzshape: see lzshape.h. */
#include "lzshape.h"

#define LZS_SPREAD 0.025f                /* UNI's widest detune at DTN 127: +-2.5 % (SUPR's) */
#define LZS_DC_R 0.9986f                 /* the DC blocker's pole: about 10 Hz at 44.1 kHz */

/* the copies' places in the spread: centre, then out and uneven, so the beats never line up (SUPR's) */
static const float LZS_SPREADS[LZS_MAX_UNI] = {0.0f, -1.0f, 1.06f, -0.42f, 0.47f};

static uint32_t lzs_rng(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

void lzs_note(lzs_t *s, uint32_t seed)
{
    int c;
    uint32_t r = seed ? seed : 0x2545F491u;
    s->ph[0] = 0;
    for (c = 1; c < LZS_MAX_UNI; c++)
        s->ph[c] = lzs_rng(&r);          /* each copy somewhere else in the cycle: no phasey attack */
    s->hold = 0.0f;
}

void lzs_init(lzs_t *s, uint32_t seed)
{
    uint8_t *b = (uint8_t *)s;
    unsigned i;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    lzs_note(s, seed);
}

int lzs_draws(const lzs_par *p)
{
    return p->uni > 1 || p->warp > 0.0f;
}

/* where step boundary j (of n) falls in the unwarped cycle: the first half of the steps squeezed
 * into [0, knee), the second half stretched over [knee, 1) */
static float lzs_bound(int j, int n, float knee)
{
    float u = (float)j / (float)n;
    return u <= 0.5f ? u * 2.0f * knee : knee + (u - 0.5f) * 2.0f * (1.0f - knee);
}

void lzs_draw(lzs_t *s, const lzs_par *p, const float levels[8], const int *order, int n_steps,
              float freq, float sr, float *out, int n)
{
    uint32_t start[LZS_MAX_STEPS], inc[LZS_MAX_UNI];
    float lv[LZS_MAX_STEPS], g[LZS_MAX_UNI], norm = 0.0f, knee, y;
    int c, i, j, k[LZS_MAX_UNI], uni = p->uni < 1 ? 1 : p->uni > LZS_MAX_UNI ? LZS_MAX_UNI : p->uni;
    if (n_steps < 1)
        n_steps = 1;
    if (n_steps > LZS_MAX_STEPS)
        n_steps = LZS_MAX_STEPS;
    knee = 0.5f - 0.42f * (p->warp < 0.0f ? 0.0f : p->warp > 1.0f ? 1.0f : p->warp);
    for (j = 0; j < n_steps; j++) {
        start[j] = (uint32_t)(lzs_bound(j, n_steps, knee) * 4294967296.0f);
        lv[j] = levels[order[j] & 7];
    }
    for (c = 0; c < uni; c++) {
        float cyc = freq * (1.0f + LZS_SPREAD * p->detune * LZS_SPREADS[c]) / sr;
        cyc = cyc < 0.0f ? 0.0f : cyc > 0.45f ? 0.45f : cyc;
        inc[c] = (uint32_t)(cyc * 4294967296.0f);
        g[c] = c ? 0.8f : 1.0f;
        norm += g[c] * g[c];
        for (k[c] = 0; k[c] < n_steps - 1 && s->ph[c] >= start[k[c] + 1]; k[c]++)
            ;                            /* the step each copy is in (WARP may have moved the edges) */
    }
    /* the copies sum uncorrelated: 1 / sqrt(norm), Newton from a fair guess (no libm) */
    y = 1.0f;
    for (j = 0; j < 4; j++)
        y = y * (1.5f - 0.5f * norm * y * y);
    for (c = 0; c < uni; c++)
        g[c] *= y;
    for (i = 0; i < n; i++) {
        float now = 0.0f, before = 0.0f;
        for (c = 0; c < uni; c++) {
            uint32_t ph = s->ph[c] + inc[c];
            int kk = k[c], wrap = ph < s->ph[c];
            float rinc = inc[c] ? 1.0f / (float)inc[c] : 0.0f;
            /* every edge this sample, each d samples ago (WARP squeezes steps under a sample at high notes):
             * 2-point polyBLEP per edge, half its step each side of it */
            for (;;) {
                int nx = kk + 1 < n_steps ? kk + 1 : 0;
                /* the next edge is behind us: a wrap passes every edge to the cycle's end and that end;
                 * otherwise an edge ahead of the old phase is passed once the phase reaches it */
                if (!(nx ? wrap || ph >= start[nx] : wrap))
                    break;
                if (!nx)
                    wrap = 0;            /* the cycle's end crossed: the rest is in the new cycle */
                if (p->blep && inc[c]) {
                    float h = g[c] * 0.5f * (lv[nx] - lv[kk]), d = (float)(ph - start[nx]) * rinc, a;
                    d = d > 1.0f ? 1.0f : d;
                    a = 1.0f - d;
                    now -= h * a * a;
                    before += h * d * d;
                }
                kk = nx;
            }
            s->ph[c] = ph;
            k[c] = kk;
            now += g[c] * lv[kk];
        }
        out[i] = s->hold + before;       /* one sample late: the edge corrects the sample before it */
        s->hold = now;
    }
}

/* the triangle folder: x within +-1 passes, beyond it reflects. f(x) = 1 - 4 |frac((x + 1) / 4) - 1/2| */
static float lzs_frac(float u)
{
    float w = u - (float)(int32_t)u;
    return w < 0.0f ? w + 1.0f : w;
}

static float lzs_tri(float x)
{
    float w = lzs_frac((x + 1.0f) * 0.25f) - 0.5f;
    return 1.0f - 4.0f * (w < 0.0f ? -w : w);
}

/* its antiderivative (periodic, mean zero): 4 G(w), G = 2w^2 - w below a half, 3w - 2w^2 - 1 above */
static float lzs_tri_int(float x)
{
    float w = lzs_frac((x + 1.0f) * 0.25f);
    return 4.0f * (w <= 0.5f ? (2.0f * w - 1.0f) * w : (3.0f - 2.0f * w) * w - 1.0f);
}

void lzs_fold(lzs_t *s, const lzs_par *p, float *buf, int n)
{
    float gain, bias, x1 = s->fx1, F1 = s->fF1, dcx = s->dcx, dcy = s->dcy;
    int i;
    if (p->fold <= 0.0f)
        return;
    gain = 1.0f + 7.0f * (p->fold > 1.0f ? 1.0f : p->fold);
    bias = p->sym < -1.0f ? -1.0f : p->sym > 1.0f ? 1.0f : p->sym;
    for (i = 0; i < n; i++) {
        float x = buf[i] * gain + bias, F = lzs_tri_int(x), dx = x - x1, y;
        /* first-order ADAA: the folder averaged over the sample, (F(x) - F(x1)) / (x - x1) */
        y = dx > 1e-3f || dx < -1e-3f ? (F - F1) / dx : lzs_tri(0.5f * (x + x1));
        x1 = x;
        F1 = F;
        dcy = y - dcx + LZS_DC_R * dcy;  /* SYM leaves DC: blocked here, before the filter */
        dcx = y;
        buf[i] = dcy;
    }
    s->fx1 = x1;
    s->fF1 = F1;
    s->dcx = dcx;
    s->dcy = dcy;
}

/* states that decay toward zero turn denormal after a second of exact silence, and a denormal costs a slow path on
 * most FPUs: flushed once a block, outside lzs_fold so its loop stays as the compiler had it */
static float lzs_flush(float x) { return x > -1e-20f && x < 1e-20f ? 0.0f : x; }

int lzs_shapes(const lzs_par *p)
{
    return p->fold > 0.0f || p->crush > 0.0f || p->down > 0.0f;
}

void lzs_shape(lzs_t *s, const lzs_par *p, float *buf, int n)
{
    int i;
    lzs_fold(s, p, buf, n);
    s->fx1 = lzs_flush(s->fx1);
    s->fF1 = lzs_flush(s->fF1);
    s->dcx = lzs_flush(s->dcx);
    s->dcy = lzs_flush(s->dcy);
    if (p->crush > 0.0f) {
        /* 2^(16 - 13 crush) levels each side: 2^x for x in 3..16 as an exponent built by hand (no libm) */
        float bits = 16.0f - 13.0f * (p->crush > 1.0f ? 1.0f : p->crush), q, rq;
        int whole = (int)bits;
        float fr = bits - (float)whole;
        q = (float)(1 << whole) * (1.0f + fr * (0.6565f + 0.3435f * fr));   /* 2^fr, exact at 0 and 1 */
        rq = 1.0f / q;
        for (i = 0; i < n; i++) {
            float x = buf[i] * q;
            buf[i] = (float)(int32_t)(x < 0.0f ? x - 0.5f : x + 0.5f) * rq;
        }
    }
    if (p->down > 0.0f) {
        /* a new value every 1 / rate samples: rate 1 .. 1/32, sliding (the hold's phase carries over) */
        float rate = 1.0f - 0.969f * (p->down > 1.0f ? 1.0f : p->down), acc = s->dacc, held = s->dheld;
        for (i = 0; i < n; i++) {
            acc += rate;
            if (acc >= 1.0f) {
                acc -= 1.0f;
                held = buf[i];
            }
            buf[i] = held;
        }
        s->dacc = acc;
        s->dheld = held;
    }
}

void lzs_shape_i32(lzs_t *s, const lzs_par *p, int32_t *buf, int n)
{
    float f[64];
    int at, i, m;
    for (at = 0; at < n; at += 64) {
        m = n - at < 64 ? n - at : 64;
        for (i = 0; i < m; i++)
            f[i] = (float)buf[at + i] * (1.0f / 32768.0f);
        lzs_shape(s, p, f, m);
        for (i = 0; i < m; i++)          /* (no clamp: a folded, biased wave runs past full scale for a moment, */
            buf[at + i] = (int32_t)(f[i] * 32768.0f);   /* as any voice can; the mix after it has the headroom) */
    }
}
