/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * tapeloop: see tapeloop.h */
#include "tapeloop.h"

#define TPL_FS 44100
static const int32_t TPL_VB[TPL_NQ] = {4096, 2048, 2048, 1024};   /* each speed's cells a sample at pitch 0, Q12 */
static const int32_t TPL_LP[TPL_NQ] = {0, 22282, 22282, 14221};   /* its bandwidth: one pole, Q15 (~8 kHz, ~4 kHz) */
static const int32_t TPL_SEMI[12] = {65536, 69433, 73562, 77936, 82570, 87480,
                                    92682, 98193, 104032, 110218, 116772, 123715};   /* 2^(i/12), Q16 */
#define TPL_MINLEN 64u                    /* the shortest splice, cells */
#define TPL_WOW_INC 48696u                /* 0.5 Hz, 2^32 a cycle a sample */
#define TPL_PUNCH 74                      /* the record head fades in and out over ~10 ms: 32767 / 441, a sample */

static inline int32_t tl_clamp16(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
static inline int32_t tl_mulq15(int32_t a, int32_t b)   /* rounded towards zero: a loop of these dies out */
{
    int32_t m = a * b;
    return (m + ((m >> 31) & 32767)) >> 15;
}
static inline int32_t tl_sin(uint32_t ph)               /* Q15, a parabola: a wobble's sweep */
{
    int32_t y = (int32_t)ph >> 16, a = y < 0 ? -y : y;
    return (y * (32768 - a)) >> 13;
}

/* G.711 mu-law, the 8-bit speeds' cells */
static uint8_t tl_mu_enc(int32_t x)
{
    int32_t s = x < 0 ? 0x80 : 0, e = 7, m;
    uint32_t mask;
    x = x < 0 ? -x : x;
    x = (x > 32635 ? 32635 : x) + 132;
    for (mask = 0x4000u; !((uint32_t)x & mask) && e > 0; e--, mask >>= 1)
        ;
    m = (x >> (e + 3)) & 0x0F;
    return (uint8_t)~(s | (e << 4) | m);
}
static int32_t tl_mu_dec(uint8_t u)
{
    int32_t x = (uint8_t)~u, e = (x >> 4) & 7, m = x & 0x0F, y = (((m << 3) + 132) << e) - 132;
    return x & 0x80 ? -y : y;
}

static inline int32_t tl_get(const tl_t *t, uint32_t c)
{
    return t->q >= TPL_LO ? tl_mu_dec(((const uint8_t *)t->mem)[c]) : ((const int16_t *)t->mem)[c];
}
static inline void tl_put(tl_t *t, uint32_t c, int32_t x)
{
    if (t->q >= TPL_LO)
        ((uint8_t *)t->mem)[c] = tl_mu_enc(x);
    else
        ((int16_t *)t->mem)[c] = (int16_t)tl_clamp16(x);
}
/* the loop at r (Q12 cells, any value within one loop's length either side), between cells */
static inline int32_t tl_read(const tl_t *t, int32_t r)
{
    int32_t l = (int32_t)(t->len << 12), f, s0, s1;
    uint32_t c0, c1;
    while (r < 0)
        r += l;
    while (r >= l)
        r -= l;
    c0 = (uint32_t)r >> 12;
    c1 = c0 + 1u < t->len ? c0 + 1u : 0u;
    f = r & 4095;
    s0 = tl_get(t, c0);
    s1 = tl_get(t, c1);
    return s0 + (((s1 - s0) * f) >> 12);
}

static void tl_clear(tl_t *t)
{
    uint32_t i;
    for (i = 0; i < t->bytes; i++)
        ((uint8_t *)t->mem)[i] = t->q >= TPL_LO ? 0xFFu : 0u;   /* (0xFF: mu-law's zero) */
}

static void tl_set_q(tl_t *t, uint8_t q)
{
    t->q = q;
    t->cap = q >= TPL_LO ? t->bytes : t->bytes / 2u;
    t->len = t->cap;
    t->pos = 0;
    t->v = TPL_VB[q];                     /* (a tape threaded at a new speed runs at it) */
    t->wprev = 0;
    t->lpi[0] = t->lpi[1] = t->lpo[0] = t->lpo[1] = 0;
    t->dcx = t->dcy = 0;
    t->recg = 32767;                     /* (a fresh tape starts with the record head on it; REC 0 lifts it in ~10 ms) */
    tl_clear(t);
}

void tl_init(tl_t *t, void *mem, uint32_t bytes)
{
    t->mem = mem;
    t->bytes = bytes & ~1u;
    t->fade = 0;
    t->wph = 0;
    t->rng = 0x2545F491u;
    t->nz = 0;
    t->mod = 0;
    tl_set_q(t, TPL_HI);
}

/* the tape's target speed: SPEED's semitones on the speed's own, then the motor */
static int32_t tl_target(const tl_t *t, const tl_param_t *p)
{
    int32_t s = p->speed < -24 ? -24 : p->speed > 24 ? 24 : p->speed, o = (s + 24) / 12 - 2, v;
    v = (int32_t)(((int64_t)TPL_VB[t->q] * TPL_SEMI[(s + 24) % 12]) >> 16);
    v = o >= 0 ? v << o : v >> -o;
    return p->motor == TPL_STOP ? 0 : p->motor == TPL_REV ? -v : v;
}

void tl_block(tl_t *t, const tl_param_t *p, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i, want, q = p->quality < TPL_NQ ? p->quality : TPL_HI;
    int32_t v0 = t->v, v1, vs, m0, m1, ms, g = 32767, gs = 0, fb = (int32_t)(p->feedback > 127 ? 127 : p->feedback) * 258;
    int32_t vb = TPL_VB[q], lp = TPL_LP[q], vt;
    int32_t lim = vb / 4;                /* the speed under which the play head's level falls to nothing */

    /* a change of speed class: fade out, clear the tape for its new format, fade in */
    if (t->fade) {
        t->fade = 0;
        tl_set_q(t, (uint8_t)q);
        v0 = t->v;
        g = 0;
        gs = 32767 / (int32_t)n;
    } else if (q != t->q) {
        t->fade = 1;
        gs = -32767 / (int32_t)n;
        q = t->q;
        vb = TPL_VB[q];
        lp = TPL_LP[q];
        lim = vb / 4;
    }

    /* the motor: the speed follows its target through INERTIA (0: at once; 127: ~2 s), the way a heavy reel does:
     * it eases in, but a brake never takes for ever: the step never falls below the full speed per tau, so a stop
     * ends within tau */
    vt = tl_target(t, p);
    if (!p->inertia)
        v1 = vt;
    else {
        int64_t tau = (int64_t)p->inertia * (2 * TPL_FS) / 127;
        int32_t d = (int32_t)(((int64_t)(vt - v0) * (int64_t)n) / tau), dmin = (int32_t)((int64_t)vb * n / tau);
        dmin = dmin < 1 ? 1 : dmin;
        if (vt > v0 && d < dmin)
            d = dmin;
        else if (vt < v0 && d > -dmin)
            d = -dmin;
        v1 = v0 + d;
        if ((d > 0 && v1 > vt) || (d < 0 && v1 < vt))
            v1 = vt;
    }
    t->v = v1;
    vs = (v1 - v0) / (int32_t)n;

    /* the splice: the length a new SPLICE asks for, taken when the loop comes round */
    want = p->splice >= 127 ? t->cap : t->cap / 127u * (p->splice ? p->splice : 1u);
    want = want < TPL_MINLEN ? TPL_MINLEN : want > t->cap ? t->cap : want;

    /* wow: the play head ahead of the record head by a cell and a wobble (Q12, at the speed's scale) */
    t->wph += TPL_WOW_INC * n;
    t->rng = t->rng * 1664525u + 1013904223u;
    t->nz += (((int32_t)(t->rng >> 17) - 16384) - t->nz) >> 4;
    m0 = t->mod;
    m1 = (((tl_sin(t->wph) + 32768) * (int32_t)p->wow) >> 5) + ((t->nz + 16384) * (int32_t)p->wow >> 9);
    m1 = (int32_t)(((int64_t)m1 * vb) >> 12);
    t->mod = m1;
    ms = (m1 - m0) / (int32_t)n;

    for (i = 0; i < n; i++) {
        int32_t v = v0 + vs * (int32_t)i, x = in[i], y, np, dir = v < 0 ? -1 : 1, a = v < 0 ? -v : v, l = (int32_t)(t->len << 12);
        /* what goes on the tape, at the tape's bandwidth */
        if (lp) {
            t->lpi[0] += tl_mulq15(x - t->lpi[0], lp);
            t->lpi[1] += tl_mulq15(t->lpi[0] - t->lpi[1], lp);
            x = t->lpi[1];
        }
        /* the play head: one cell ahead of the record head, and the wobble */
        y = tl_read(t, t->pos + dir * (4096 + m0 + ms * (int32_t)i));
        /* the record head: every cell it crosses this sample gets the input, laid over what survives of the old */
        np = t->pos + v;
        /* punch in and out over ~10 ms, so a frozen loop's splice is a crossfade of new into old, not a click */
        t->recg += p->rec ? (t->recg < 32767 - TPL_PUNCH ? TPL_PUNCH : 32767 - t->recg)
                          : (t->recg > TPL_PUNCH ? -TPL_PUNCH : -t->recg);
        if (t->recg && v) {
            int32_t keep = fb + tl_mulq15(32767 - fb, 32767 - t->recg);   /* the old: fb when recording, 1 when not */
            int32_t c = t->pos >> 12, ce = (np >= 0 ? np : np - 4095) >> 12, k = ce - c, j, kk = k < 0 ? -k : k;
            for (j = 1; j <= kk; j++) {
                int32_t cc = c + (k < 0 ? -j : j), val = t->wprev + (x - t->wprev) * j / kk;
                uint32_t cu;
                while (cc < 0)
                    cc += (int32_t)t->len;
                while (cc >= (int32_t)t->len)
                    cc -= (int32_t)t->len;
                cu = (uint32_t)cc;
                tl_put(t, cu, tl_mulq15(tl_get(t, cu), keep) + tl_mulq15(val, t->recg));
            }
        }
        t->wprev = x;
        /* round the loop: the splice's new length lands here */
        if (np >= l || np < 0) {
            np = np >= l ? np - l : np + l;
            if (want != t->len) {
                t->len = want;
                l = (int32_t)(t->len << 12);
                while (np >= l)
                    np -= l;
            }
        }
        t->pos = np;
        /* what comes off: the bandwidth again, the level falling as the tape stops, the DC taken out */
        if (lp) {
            t->lpo[0] += tl_mulq15(y - t->lpo[0], lp);
            t->lpo[1] += tl_mulq15(t->lpo[0] - t->lpo[1], lp);
            y = t->lpo[1];
        }
        if (a < lim)
            y = (int32_t)(((int64_t)y * a) / lim);
        t->dcy = y - t->dcx + tl_mulq15(t->dcy, 32604);
        t->dcx = y;
        y = t->dcy;
        if (gs) {
            y = tl_mulq15(y, g);
            g += gs;
            g = g < 0 ? 0 : g > 32767 ? 32767 : g;
        }
        out[i] = y;
    }
}

void tl_voice_on(const tl_t *t, tl_voice_t *v, const tl_play_t *pp, uint32_t seed)
{
    int32_t at = (int32_t)(((int64_t)(t->len << 12) * (pp->position > 127 ? 127 : pp->position)) / 128);
    v->head = at;
    v->g[0] = v->g[1] = at;
    v->glen = 441u + 101u * (pp->grain > 127 ? 127u : pp->grain);
    v->gk[0] = 0;
    v->gk[1] = v->glen / 2u;             /* (the second grain half a window behind: the triangles sum to 1) */
    v->rng = seed | 1u;
}

void tl_voice_render(const tl_t *t, tl_voice_t *v, const tl_play_t *pp, uint32_t ratio, int32_t *out, uint32_t n)
{
    uint32_t i, r;
    int32_t step = (int32_t)(((int64_t)TPL_VB[t->q] * ratio) >> 16), l = (int32_t)(t->len << 12);
    if (pp->mode == TPL_TAPE) {
        for (i = 0; i < n; i++) {
            out[i] += tl_read(t, v->head);
            v->head += step;
            if (v->head >= l)
                v->head -= l;
        }
        return;
    }
    {
        int32_t scan = (int32_t)(((int64_t)TPL_VB[t->q] * (pp->scan > 127 ? 127 : pp->scan)) / 64);
        int32_t spray = (int32_t)(((int64_t)l / 2 * (pp->spray > 127 ? 127 : pp->spray)) / 127);
        for (i = 0; i < n; i++) {
            int32_t y = 0;
            for (r = 0; r < 2u; r++) {
                uint32_t k = v->gk[r], L = v->glen, h = L / 2u;
                int32_t w;
                if (k >= L) {            /* a grain ends: the next starts at the scan's place, scattered */
                    int32_t off = 0;
                    L = v->glen = 441u + 101u * (pp->grain > 127 ? 127u : pp->grain);
                    h = L / 2u;
                    if (spray) {
                        v->rng = v->rng * 1664525u + 1013904223u;
                        off = (int32_t)(((int64_t)((int32_t)(v->rng >> 16) - 32768) * spray) >> 15);
                    }
                    v->g[r] = v->head + off;
                    k = 0;
                }
                w = (int32_t)((k < h ? k : L - k) * 32768u / h);   /* Q15 triangle */
                y += tl_mulq15(tl_read(t, v->g[r]), w);
                v->g[r] += step;
                v->gk[r] = k + 1u;
            }
            v->head += scan;
            while (v->head >= l)
                v->head -= l;
            out[i] += y;
        }
    }
}
