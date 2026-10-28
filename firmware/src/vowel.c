/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * vowel: see vowel.h */
#include "vowel.h"

#define VW_FS 44100
#define VW_NV 8
#define VW_END ((VW_NV - 1) << 12)          /* the path's end, Q12 vowels */
#define VW_OPEN (4 << 12)                   /* AA, where TRACK pulls */
#define VW_LFMAX 822526                     /* log2 6000 Hz, Q16: no formant higher (SIZE 127 tops out at 4.2 kHz) */
#define VW_TH 1223858                       /* pi 2^34 / VW_FS: Hz Q4 to the angle pi F / fs, Q30 after >> 8 */
#define VW_SZ_DN 33723                      /* log2(1 / 0.7), Q16 */
#define VW_SZ_UP 31813                      /* log2(1.4) */

/* per vowel: log2 F1 F2 F3 (Q16 of log2 Hz), the damping B / F of each (Q14) and the vowel's gain (Q15), worked
 * out from the Hz in the comments: mouth.c's MTH_PH rows, F1..F3 Peterson and
 * Barney (1952) table II men's averages, B1..B3 Klatt (1980) table II. The gain puts every vowel at one level on
 * pink noise, ~15 dB under the input: the headroom a full-scale saw needs (vowel.h). Pink falls 3 dB an octave, half
 * way between white noise and a saw, so on those the vowels spread ~2 dB each way, the low ones louder on a saw */
typedef struct {
    int32_t lf[3];
    int16_t q[3], g;
} vw_vowel_t;

static const vw_vowel_t VW_V[VW_NV] = {
    {{529322, 731456, 757304}, {2731, 1431, 2177}, 26344},   /* IY  270 2290 3010,  45 200 400 */
    {{564090, 718180, 741624}, {2101, 823, 900}, 31614},     /* IH  390 1990 2550,  50 100 140 */
    {{593091, 710770, 738992}, {1855, 801, 1321}, 32459},    /* EH  530 1840 2480,  60  90 200 */
    {{613831, 704394, 736285}, {1738, 1429, 2175}, 29414},   /* AE  660 1720 2410,  70 150 320 */
    {{623362, 661266, 737455}, {2918, 1052, 1074}, 27981},   /* AA  730 1090 2440, 130  70 160 */
    {{599970, 636633, 736285}, {2587, 1950, 544}, 27079},    /* AO  570  840 2410,  90 100  80 */
    {{575495, 654990, 729369}, {2979, 1606, 585}, 25858},    /* UH  440 1020 2240,  80 100  80 */
    {{539284, 639951, 729369}, {3550, 2072, 1024}, 23215},   /* UW  300  870 2240,  65 110 140 */
};

/* m >> sh, rounded toward zero: what a filter left alone still holds is under the output's last bit */
#define VW_RZ(m, sh) (((m) + (((m) >> 31) & ((1 << (sh)) - 1))) >> (sh))
/* a b >> 32: the product's high word, one instruction (SMMUL) on a Cortex-M4 or M7 */
#define VW_MH(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 32))

static inline int32_t vw_clamp16(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }

/* x toward t over a time constant of 2^sh samples, n samples at once; lands exactly */
static int32_t vw_glide(int32_t x, int32_t t, uint32_t n, int sh)
{
    int32_t d;
    if (n >= (1u << sh))
        return t;
    d = ((t - x) * (int32_t)n) >> sh;
    if (!d)
        d = t > x ? 1 : t < x ? -1 : 0;
    return x + d;
}

/* a formant's log2 Hz (Q16) to the filter's frequency coefficient 2 sin(pi F / fs), Q31 */
static int32_t vw_coef(int32_t lf)
{
    int32_t e, x, p, fq4;
    int64_t th, t2, s;
    lf = lf > VW_LFMAX ? VW_LFMAX : lf;
    e = lf >> 16;
    x = (lf & 0xFFFF) >> 1;              /* the octave's fraction, Q15; 2^x by a cubic, within 0.01 % */
    p = 32768 + ((x * (22778 + ((x * (7460 + ((x * 2525) >> 15))) >> 15))) >> 15);
    fq4 = (p << e) >> 11;                /* Hz, Q4 */
    th = ((int64_t)fq4 * VW_TH) >> 8;    /* pi F / fs, Q30 (at most 0.43) */
    t2 = (th * th) >> 30;
    s = th - ((th * ((t2 * ((1 << 30) - t2 / 20) >> 30) / 6)) >> 30);   /* sin: th (1 - th^2/6 (1 - th^2/20)) */
    return (int32_t)(s << 2);
}

void vw_init(vw_t *v)
{
    uint32_t k;
    for (k = 0; k < 3; k++)
        v->lp[k] = v->bp[k] = 0;
    v->env = 0;
    v->pos = -1;
    v->size = 0;
    v->mix = 0;
}

void vw_block(vw_t *v, const vw_param_t *p, const int32_t *in, int32_t *out, uint32_t n)
{
    int32_t vo = p->vowel > 127 ? 127 : p->vowel, tr = p->track > 127 ? 127 : p->track;
    int32_t sz = p->size > 127 ? 127 : p->size, mx = p->mix > 127 ? 128 : p->mix + (p->mix >> 6);
    int32_t tgt, a, ls, s, t, g, f0, f1, f2, q0, q1, q2, c0, c1, c2, m, mstep;
    int32_t lp0 = v->lp[0], lp1 = v->lp[1], lp2 = v->lp[2], bp0 = v->bp[0], bp1 = v->bp[1], bp2 = v->bp[2];
    const vw_vowel_t *A, *B;
    uint32_t i, acc = 0;

    if (!n)
        return;
    /* where on the path: VOWEL, pulled toward AA by the level the last block left */
    tgt = (vo * VW_END + 63) / 127;
    a = (v->env * tr) >> 8;              /* Q12, all the way from a mean level of 8256 at TRACK 127 */
    a = a > 4096 ? 4096 : a;
    tgt += ((VW_OPEN - tgt) * a) >> 12;
    ls = sz < 64 ? -((64 - sz) * VW_SZ_DN) / 64 : ((sz - 64) * VW_SZ_UP) / 63;
    if (v->pos < 0) {                    /* the first block starts where the knobs are */
        v->pos = tgt;
        v->size = ls;
        v->mix = mx;
    } else {
        v->pos = vw_glide(v->pos, tgt, n, 8);
        v->size = vw_glide(v->size, ls, n, 8);
    }

    /* the formants there: log frequencies and dampings interpolated between the two vowels either side */
    s = v->pos >> 12;
    t = v->pos & 4095;
    if (s >= VW_NV - 1) {
        s = VW_NV - 2;
        t = 4096;
    }
    A = &VW_V[s];
    B = &VW_V[s + 1];
    g = A->g + (((B->g - A->g) * t) >> 12);
    f0 = vw_coef(A->lf[0] + (((B->lf[0] - A->lf[0]) * t) >> 12) + v->size);
    f1 = vw_coef(A->lf[1] + (((B->lf[1] - A->lf[1]) * t) >> 12) + v->size);
    f2 = vw_coef(A->lf[2] + (((B->lf[2] - A->lf[2]) * t) >> 12) + v->size);
    q0 = A->q[0] + (((B->q[0] - A->q[0]) * t) >> 12);
    q1 = A->q[1] + (((B->q[1] - A->q[1]) * t) >> 12);
    q2 = A->q[2] + (((B->q[2] - A->q[2]) * t) >> 12);
    /* each input scaled by its damping (peak gain 1) and the vowel's gain, Q15; F2 flipped, F3 at half */
    c0 = (q0 * g) >> 14;
    c1 = -((q1 * g) >> 14);
    c2 = (q2 * g) >> 15;
    q0 <<= 18;                           /* the dampings, Q32 (at most 0.22) */
    q1 <<= 18;
    q2 <<= 18;

    m = v->mix << 5;                     /* MIX, Q12, ramped across the block */
    mstep = ((mx - v->mix) << 5) / (int32_t)n;
    v->mix = mx;

    /* Chamberlin's filter: lp += f bp; hp = c x - q bp - lp; bp += f hp. The states are Q12 on int16 scale; each
     * stays within ~1.3x the input's peak (peak gain at most 1), hp within ~2x: 2^29 at full scale, under int32 */
    for (i = 0; i < n; i++) {
        int32_t x = in[i], xc = vw_clamp16(x), h, w;
        acc += (uint32_t)(xc < 0 ? -xc : xc);
        lp0 += 2 * VW_MH(f0, bp0);
        h = ((c0 * xc) >> 3) - VW_MH(q0, bp0) - lp0;
        bp0 += 2 * VW_MH(f0, h);
        lp1 += 2 * VW_MH(f1, bp1);
        h = ((c1 * xc) >> 3) - VW_MH(q1, bp1) - lp1;
        bp1 += 2 * VW_MH(f1, h);
        lp2 += 2 * VW_MH(f2, bp2);
        h = ((c2 * xc) >> 3) - VW_MH(q2, bp2) - lp2;
        bp2 += 2 * VW_MH(f2, h);
        w = bp0 + bp1 + bp2;
        w = vw_clamp16(VW_RZ(w, 12));
        out[i] = x + (((w - x) * m) >> 12);
        m += mstep;
    }
    v->lp[0] = lp0;
    v->lp[1] = lp1;
    v->lp[2] = lp2;
    v->bp[0] = bp0;
    v->bp[1] = bp1;
    v->bp[2] = bp2;
    v->env = vw_glide(v->env, (int32_t)(acc / n), n, (int32_t)(acc / n) > v->env ? 8 : 11);
}
