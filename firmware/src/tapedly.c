/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * tapedly: see tapedly.h */
#include "tapedly.h"

static const uint8_t TD_HEAD_SET[TD_NHEADS] = {1, 2, 4, 3, 6, 5, 7};   /* bit h - 1: head h plays */
static const int32_t TD_SUM_FB[4] = {0, 32767, 16384, 10923};           /* the loop takes the heads' mean */
static const int32_t TD_SUM_OUT[4] = {0, 32767, 23170, 18919};          /* the output their sum / sqrt(n) */

#define TD_WOW_HZ_INC 61347u                /* 0.63 Hz, 2^32 a cycle a sample at 44.1 kHz */
#define TD_FLT_HZ_INC 652517u               /* 6.7 Hz */
#define TD_WOW_DEPTH 12288                  /* at WOW 127, Q8 samples: 48 a side, flutter 2, scrape 0.25; together the */
#define TD_FLT_DEPTH 512                    /* pitch swings ~+-0.95 % (a worn machine); WOW 10 is ~+-0.07 % */
#define TD_NZ_DEPTH 128                     /* 0.5, the scrape */
#define TD_MOD_MAX (2 * (TD_WOW_DEPTH + TD_FLT_DEPTH + TD_NZ_DEPTH))
#define TD_SLEW_SH 12                       /* the tape's speed settles with a 4096-sample time constant .. */
#define TD_SLEW_MAX 128                     /* .. at most half a sample a sample (Q8): pitch within 0.5x .. 1.5x */

static inline int32_t td_clamp16(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
/* a * b >> 15, rounded towards zero: a loop of these dies out in exact silence */
static inline int32_t td_mulq15z(int32_t a, int32_t b)
{
    int32_t m = a * b;
    return (m + ((m >> 31) & 32767)) >> 15;
}
/* sin of a uint32 phase, Q15, a parabola (an LFO: its harmonics are inaudible as motion) */
static inline int32_t td_psin(uint32_t ph)
{
    int32_t y = (int32_t)ph >> 16, a = y < 0 ? -y : y;
    return (y * (32768 - a)) >> 13;
}
static inline int32_t td_tap(const td_t *t, uint32_t w, int32_t d)
{
    uint32_t i = (uint32_t)d >> 8;
    int32_t f = d & 255, s0 = t->buf[(w - i) & t->mask], s1 = t->buf[(w - i - 1u) & t->mask];
    return s0 + (((s1 - s0) * f) >> 8);
}
/* the soft clip: u - 4/27 u^3 up to 1.5, then 1; unity slope at 0, so the loop's gain for quiet repeats does not
 * depend on SAT. g_q8 the drive (1 .. ~4), ig_q15 its inverse: the ceiling falls to 1/g of full scale */
static inline int32_t td_sat(int32_t v, int32_t g_q8, int32_t ig_q15)
{
    int32_t a = v < 0 ? -v : v, u = (a * g_q8) >> 11, u3;   /* Q12 of full scale; on |v|, so it rounds towards 0 */
    u = u > 6144 ? 6144 : u;
    u3 = (((u * u) >> 12) * u) >> 12;
    u -= (u3 * 4855) >> 15;
    u = ((u << 3) * ig_q15) >> 15;
    return v < 0 ? -u : u;
}

void td_init(td_t *t, int16_t *buf, uint32_t len)
{
    uint32_t i;
    t->buf = buf;
    t->mask = len - 1u;
    for (i = 0; i < len; i++)
        buf[i] = 0;
    t->w = 0;
    t->dist = -1;
    t->hd[0] = t->hd[1] = t->hd[2] = 0;
    t->mod = 0;
    t->lp = 0;
    t->lpe = 0;
    t->wph = 0;
    t->fph = 0x40000000u;
    t->rng = 0x9E3779B9u;
    t->nz = 0;
    t->hsel = 0;
    t->fade = 0;
    t->rmode = TD_FWD;
    t->rk[0] = 0;
    t->rk[1] = 0;
    t->rl[0] = t->rl[1] = 0;
    t->rinv[0] = t->rinv[1] = 0;
}

void td_block(td_t *t, const td_param_t *p, const int32_t *in, int32_t *wet, uint32_t n)
{
    uint32_t len = t->mask + 1u, i, h, set, nh = 0, hm = 1, rmax, rlmax = (len - 4u) / 2u;
    int32_t tgt, d0 = t->dist, d1, m0 = t->mod, m1, hs[3], hstep[3], mstep, gf = 32767, gstep = 0, reseat;
    int32_t col = p->color, fb = p->fb, sfb, sout, gq8 = 256 + 6 * (int32_t)p->sat, ig = (32768 * 256) / gq8;
    uint32_t want = p->heads < TD_NHEADS ? p->heads : 0u;
    uint8_t rmode = p->rev <= TD_ALT ? p->rev : TD_FWD;

    /* a change of heads or of REV: fade this block out, switch, fade the next one in */
    if (t->fade) {
        t->fade = 0;
        t->hsel = (uint8_t)want;
        t->rmode = rmode;
        gf = 0;
        gstep = 32767 / (int32_t)n;
        d0 = -1;                         /* the heads re-seat at their new places */
    } else if (want != t->hsel || rmode != t->rmode) {
        t->fade = 1;
        gstep = -32767 / (int32_t)n;
    }
    rmode = t->rmode;
    set = TD_HEAD_SET[t->hsel];
    for (h = 0; h < 3; h++)
        if (set >> h & 1u) {
            nh++;
            hm = h + 1u;
        }
    sfb = TD_SUM_FB[nh];
    sout = TD_SUM_OUT[nh];

    /* the tape's speed: the furthest head glides to TIME */
    rmax = len - 4u - (uint32_t)(TD_MOD_MAX >> 8);
    tgt = (int32_t)((p->time < 1u ? 1u : p->time > rmax ? rmax : p->time) << 8);
    reseat = d0 < 0;
    if (reseat)
        d0 = tgt;
    {
        int32_t lim = TD_SLEW_MAX * (int32_t)n, e = tgt - d0;
        int32_t mv = (int32_t)(((int64_t)e * (int32_t)n) >> TD_SLEW_SH);
        if (mv > -(int32_t)n && mv < (int32_t)n)   /* the last stretch: 1/256 sample a sample, then land */
            mv = e > (int32_t)n ? (int32_t)n : e < -(int32_t)n ? -(int32_t)n : e;
        d1 = d0 + (mv > lim ? lim : mv < -lim ? -lim : mv);
    }
    t->dist = d1;

    /* wow, flutter and scrape, as an offset >= 0 on every read (the same tape under every head) */
    t->wph += TD_WOW_HZ_INC * n;
    t->fph += TD_FLT_HZ_INC * n;
    t->rng = t->rng * 1664525u + 1013904223u;
    t->nz += (((int32_t)(t->rng >> 17) - 16384) - t->nz) >> 4;   /* the noise, low-passed once a block (~14 Hz) */
    m1 = ((td_psin(t->wph) + 32768) * TD_WOW_DEPTH + (td_psin(t->fph) + 32768) * TD_FLT_DEPTH + (t->nz + 16384) * TD_NZ_DEPTH) >> 15;
    m1 = (m1 * (int32_t)p->wow) / 127;
    t->mod = m1;
    mstep = (m1 - m0) / (int32_t)n;

    /* each head's distance, ramped across the block from where the last one left it */
    for (h = 0; h < 3; h++) {
        int32_t e = (int32_t)(((int64_t)d1 * (int32_t)(h + 1u)) / (int32_t)hm);
        hs[h] = reseat ? (int32_t)(((int64_t)d0 * (int32_t)(h + 1u)) / (int32_t)hm) : t->hd[h];
        hstep[h] = (e - hs[h]) / (int32_t)n;
        t->hd[h] = e;
    }

    {
        uint32_t k = 0;                  /* the heads playing, packed first */
        for (h = 0; h < 3; h++)
            if (set >> h & 1u) {
                hs[k] = hs[h];
                hstep[k] = hstep[h];
                k++;
            }
    }
    for (i = 0; i < n; i++) {
        int32_t sum = 0, fsig, out, rv = 0, mo = m0 + mstep * (int32_t)i;
        for (h = 0; h < nh; h++)
            sum += td_tap(t, t->w, hs[h] + hstep[h] * (int32_t)i + mo);
        if (rmode != TD_FWD) {
            /* reader 0 sets the pace: a window of TIME, then the next. Reader 1 starts its own window each time
             * reader 0 is half way through, so the two triangles stay half a window apart and sum to 1, and after a
             * TIME change they fall back into step within one window */
            uint32_t k0 = t->rk[0], L0 = t->rl[0], k1, L1;
            int32_t g0, g1;
            if (k0 >= L0) {
                uint32_t nl = p->time < 2u ? 2u : p->time > rlmax ? rlmax : p->time;
                if (!L0) {               /* (the first window: reader 1 begins half way through one) */
                    t->rk[1] = nl / 2u;
                    t->rl[1] = nl;
                    t->rinv[1] = (1u << 24) / nl;
                }
                k0 = 0;
                t->rl[0] = L0 = nl;
                t->rinv[0] = (1u << 24) / nl;
            }
            if (k0 == L0 / 2u) {
                t->rk[1] = 0;
                t->rl[1] = L0;
                t->rinv[1] = t->rinv[0];
            }
            k1 = t->rk[1];
            L1 = t->rl[1];
            k1 = k1 > L1 ? L1 : k1;      /* (a window cut longer than the new pace waits at its end, silent) */
            g0 = (int32_t)(((k0 < L0 / 2u ? k0 : L0 - k0) * t->rinv[0]) >> 8);   /* Q15 triangles (k * 2^24 / L, Q16) */
            g1 = (int32_t)(((k1 < L1 / 2u ? k1 : L1 - k1) * t->rinv[1]) >> 8);
            rv = td_mulq15z(t->buf[(t->w - 1u - 2u * k0) & t->mask], g0) +
                 td_mulq15z(t->buf[(t->w - 1u - 2u * k1) & t->mask], g1);
            t->rk[0] = k0 + 1u;
            t->rk[1] = k1 < L1 ? k1 + 1u : k1;
        }
        fsig = rmode == TD_ALT ? rv : nh > 1u ? td_mulq15z(sum, sfb) : sum;
        out = rmode == TD_FWD ? (nh > 1u ? td_mulq15z(sum, sout) : sum) : rv;
        if (gstep) {                     /* (only while HEADS or REV switch) */
            fsig = td_mulq15z(fsig, gf);
            out = td_mulq15z(out, gf);
            gf += gstep;
            gf = gf < 0 ? 0 : gf > 32767 ? 32767 : gf;
        }
        {                                /* (the remainder kept: it settles exactly, from below as from above) */
            int32_t acc = (td_clamp16(fsig) - t->lp) * col + t->lpe;
            t->lp += acc >> 15;
            t->lpe = acc & 32767;
        }
        fsig = td_mulq15z(t->lp, fb);
        if (p->sat)
            fsig = td_sat(fsig, gq8, ig);
        t->buf[t->w & t->mask] = (int16_t)td_clamp16(in[i] + fsig);
        t->w++;
        wet[i] = out;
    }
}

void tpd_init(tpd_t *p, int16_t *buf, uint32_t len)
{
    uint32_t i;
    p->buf = buf;
    p->len = len;
    for (i = 0; i < len; i++)
        buf[i] = 0;
    p->w = 0;
    p->d = -1;
}

void tpd_block(tpd_t *p, uint32_t dist, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t tgt = (int32_t)((dist > p->len - 2u ? p->len - 2u : dist) << 8);
    if (p->d < 0)
        p->d = tgt;
    for (i = 0; i < n; i++) {
        int32_t x = in[i], s0, s1, f;
        uint32_t k, r0, r1;
        p->d += tgt > p->d ? (tgt - p->d < 64 ? tgt - p->d : 64) : -(p->d - tgt < 64 ? p->d - tgt : 64);
        p->buf[p->w] = (int16_t)td_clamp16(x);
        k = (uint32_t)p->d >> 8;
        f = p->d & 255;
        r0 = p->w >= k ? p->w - k : p->w + p->len - k;
        r1 = r0 ? r0 - 1u : p->len - 1u;
        s0 = p->buf[r0];
        s1 = p->buf[r1];
        out[i] = s0 + (((s1 - s0) * f) >> 8);
        if (++p->w == p->len)
            p->w = 0;
    }
}
