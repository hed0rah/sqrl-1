/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * syllable: the port of a reference model (not published; syllable.h names the model and the sources).
 * Tracks: integers, the same as the reference bit for bit. Filters: Q30 coefficients computed once
 * a tick by polynomial exp and cos (no floats, no tables), Q16 signals, int64 products.
 * Signed right shifts are arithmetic, as on every compiler we build with. */
#include "syllable.h"

#define SYL_Q 32767
#define SYL_V_LEVEL (SYL_Q * 7 / 10)
#define SYL_TRANS_MS 50
#define SYL_K_LOCUS 6
#define SYL_OQ32 2576980378u             /* the open quotient 0.6 as a fraction of 2^32 */
#define SYL_OQ_RECIP 109227u             /* 2^16 / 0.6: the open phase's t in Q16 from the phase's top 16 bits */
#define SYL_SRC_GAIN_Q15 49152           /* 1.5 */
#define SYL_NOISE_ASP_Q15 19661          /* 0.6 */
#define SYL_PAR_GAIN_X2 3                /* 1.5, as x3 >> 1 */
#define SYL_AB_LP_Q15 11469              /* 0.35 */
#define SYL_OUT_GAIN_Q15 6554            /* 0.2 */
#define SYL_K_BW 76489                   /* pi / FS, Q30 */
#define SYL_K_F 152979                   /* 2 pi / FS, Q30 */

const char *const SYL_VOWEL_NAMES[SYL_NVOWEL] = {"i", "I", "e", "E", "ae", "a", "V", "O", "o", "U", "u", "er",
                                                 "ai", "au", "ei", "oi", "ou", "iau"};
const char *const SYL_CONS_NAMES[SYL_NCONS] = {"-", "b", "d", "g", "p", "t", "k", "m", "n", "s", "z", "sh",
                                               "f", "v", "h", "l", "r", "w", "y"};

/* hillenbrand et al. 1995, men, in SYL_VOWEL_NAMES order */
static const int16_t syl_vowel_f[12][3] = {
    {342, 2322, 3000}, {427, 2034, 2684}, {476, 2089, 2691}, {580, 1799, 2605}, {588, 1952, 2601},
    {768, 1333, 2522}, {623, 1200, 2550}, {652, 997, 2538}, {497, 910, 2459}, {469, 1122, 2434},
    {378, 997, 2343}, {474, 1379, 1710}};
static const int16_t syl_b123[3] = {60, 90, 150};
/* the diphthongs (vowels 12..16): the first vowel, then a glide to the second; the triphthong (17, the
 * cat's iau) glides on to a third, its first glide quicker. 255 = no third */
static const uint8_t syl_diph[SYL_NVOWEL - 12][3] = {{5, 1, 255}, {5, 9, 255}, {2, 1, 255}, {7, 1, 255},
                                                     {8, 9, 255}, {0, 5, 9}};
#define SYL_DIPH_HOLD_MS 60
#define SYL_DIPH_GLIDE_MS 160
#define SYL_TRIPH_GLIDE_MS 80

enum { SYL_K_NONE, SYL_K_STOP, SYL_K_NASAL, SYL_K_FRIC, SYL_K_H, SYL_K_APPROX };
typedef struct {
    uint8_t kind, voiced;
    int16_t f2l, f3l;
    int16_t bands[6];                /* P2 P3 P4 P5 P6 AB, Q15 */
    uint8_t d1, d2;                  /* ms: burst / frication, VOT / murmur / glide */
    int16_t approx_f1;
} syl_cons_t;

/* the reference's consonant table, in SYL_CONS_NAMES order */
static const syl_cons_t syl_cons[SYL_NCONS] = {
    {SYL_K_NONE, 1, 0, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},
    {SYL_K_STOP, 1, 900, 2200, {0, 0, 0, 0, 0, 9000}, 6, 8, 0},
    {SYL_K_STOP, 1, 1700, 2600, {0, 0, 3000, 9000, 9000, 0}, 6, 12, 0},
    {SYL_K_STOP, 1, 0, 2500, {12000, 9000, 0, 0, 0, 0}, 10, 18, 0},
    {SYL_K_STOP, 0, 900, 2200, {0, 0, 0, 0, 0, 12000}, 8, 50, 0},
    {SYL_K_STOP, 0, 1700, 2600, {0, 0, 4000, 12000, 12000, 0}, 10, 60, 0},
    {SYL_K_STOP, 0, 0, 2500, {16000, 12000, 0, 0, 0, 0}, 14, 70, 0},
    {SYL_K_NASAL, 1, 1000, 2200, {0, 0, 0, 0, 0, 0}, 0, 70, 0},
    {SYL_K_NASAL, 1, 1700, 2600, {0, 0, 0, 0, 0, 0}, 0, 70, 0},
    {SYL_K_FRIC, 0, 1700, 2600, {0, 0, 0, 14000, 16000, 0}, 110, 0, 0},
    {SYL_K_FRIC, 1, 1700, 2600, {0, 0, 0, 9000, 10000, 0}, 90, 0, 0},
    {SYL_K_FRIC, 0, 1900, 2400, {0, 14000, 12000, 3000, 0, 0}, 110, 0, 0},
    {SYL_K_FRIC, 0, 900, 2200, {0, 0, 0, 0, 2500, 5000}, 90, 0, 0},
    {SYL_K_FRIC, 1, 900, 2200, {0, 0, 0, 0, 1500, 3000}, 70, 0, 0},
    {SYL_K_H, 0, 0, 0, {0, 0, 0, 0, 0, 0}, 70, 0, 0},
    {SYL_K_APPROX, 1, 1000, 2800, {0, 0, 0, 0, 0, 0}, 0, 60, 360},
    {SYL_K_APPROX, 1, 1100, 1500, {0, 0, 0, 0, 0, 0}, 0, 70, 420},
    {SYL_K_APPROX, 1, 700, 2200, {0, 0, 0, 0, 0, 0}, 0, 70, 300},
    {SYL_K_APPROX, 1, 2100, 2900, {0, 0, 0, 0, 0, 0}, 0, 60, 260},
};

static int32_t syl_ms(int32_t t) { return (t * SYL_FS / 1000 + SYL_CTL / 2) / SYL_CTL; }

static void syl_put(syl_tracks_t *T, uint32_t k, uint32_t n, const int32_t *tv)
{
    uint32_t i;
    T->n[k] = (uint8_t)n;
    for (i = 0; i < n; i++) {
        T->t[k][i] = (int16_t)tv[2 * i];
        T->v[k][i] = (int16_t)tv[2 * i + 1];
    }
}
#define SYL_PUT(T, k, ...) do { int32_t a_[] = {__VA_ARGS__}; syl_put(T, k, sizeof a_ / sizeof a_[0] / 2, a_); } while (0)

static void syl_glide(syl_tracks_t *T, int32_t t0, int32_t t1, int32_t f1, int32_t f2, int32_t f3,
                  const int16_t *v, int32_t tr)
{
    SYL_PUT(T, SYL_F1, 0, f1, t1, f1, t1 + syl_ms(tr / 2), v[0]);
    SYL_PUT(T, SYL_F2, 0, f2, t0, f2, t0 + syl_ms(tr), v[1]);
    SYL_PUT(T, SYL_F3, 0, f3, t0, f3, t0 + syl_ms(tr), v[2]);
}

static void syl_bands_on(syl_tracks_t *T, const syl_cons_t *c, int32_t t_off, int32_t fade)
{
    uint32_t i;
    for (i = 0; i < 6; i++)
        if (c->bands[i])
            SYL_PUT(T, SYL_P2 + i, 0, c->bands[i], t_off, c->bands[i], t_off + fade, 0);
        else
            SYL_PUT(T, SYL_P2 + i, 0, 0);
}

void syl_tracks(syl_tracks_t *T, uint32_t ci, uint32_t vi, uint32_t scale)
{
    const syl_cons_t *c = &syl_cons[ci < SYL_NCONS ? ci : 0];
    const int16_t *v, *w = 0, *x = 0;
    int32_t d1 = c->d1 * (int32_t)scale / 8, d2 = c->d2 * (int32_t)scale / 8, tr = SYL_TRANS_MS * (int32_t)scale / 8;
    int32_t f2l = c->f2l, f3l = c->f3l, on2, on3;
    uint32_t k;
    vi = vi < SYL_NVOWEL ? vi : 0u;
    if (vi >= 12u) {
        const uint8_t *d = syl_diph[vi - 12u];
        v = syl_vowel_f[d[0]];
        w = syl_vowel_f[d[1]];
        if (d[2] != 255u)
            x = syl_vowel_f[d[2]];
    } else
        v = syl_vowel_f[vi];
    if (f2l == 0)                                        /* velars: the locus follows the vowel's F2 */
        f2l = v[1] + 300 > 1500 ? v[1] + 300 : 1500;
    on2 = f2l + SYL_K_LOCUS * (v[1] - f2l) / 8;
    on3 = f3l + SYL_K_LOCUS * (v[2] - f3l) / 8;
    for (k = 0; k < SYL_NP; k++)
        SYL_PUT(T, k, 0, 0);
    SYL_PUT(T, SYL_B1, 0, syl_b123[0]);
    SYL_PUT(T, SYL_B2, 0, syl_b123[1]);
    SYL_PUT(T, SYL_B3, 0, syl_b123[2]);
    switch (c->kind) {
    case SYL_K_NONE:
        syl_glide(T, 0, 0, v[0], v[1], v[2], v, tr);
        SYL_PUT(T, SYL_AV, 0, 0, syl_ms(12), SYL_V_LEVEL);
        break;
    case SYL_K_STOP: {
        int32_t rel = syl_ms(d1);
        syl_bands_on(T, c, rel, 1);
        SYL_PUT(T, SYL_AF, 0, SYL_Q, rel, SYL_Q, rel + 1, 0);
        if (c->voiced) {
            syl_glide(T, rel, rel, 250, on2, on3, v, tr);
            SYL_PUT(T, SYL_AV, 0, SYL_Q / 5, rel, SYL_Q / 4, rel + syl_ms(12), SYL_V_LEVEL);
        } else {
            int32_t t0 = rel + syl_ms(d2);
            SYL_PUT(T, SYL_AH, 0, 0, rel, SYL_Q * 6 / 10, t0, SYL_Q * 3 / 10, t0 + 2, 0);
            syl_glide(T, rel, t0, 250, on2, on3, v, tr);
            SYL_PUT(T, SYL_AV, 0, 0, t0, 0, t0 + syl_ms(10), SYL_V_LEVEL);
        }
        break;
    }
    case SYL_K_NASAL: {
        int32_t mur = syl_ms(d2);
        SYL_PUT(T, SYL_NAS, 0, SYL_Q, mur, SYL_Q, mur + 2, 0);
        SYL_PUT(T, SYL_B1, 0, 100, mur, 100, mur + 2, syl_b123[0]);
        syl_glide(T, mur, mur, 250, on2, on3, v, tr);
        SYL_PUT(T, SYL_AV, 0, SYL_Q * 4 / 10, mur, SYL_Q * 4 / 10, mur + syl_ms(8), SYL_V_LEVEL);
        break;
    }
    case SYL_K_FRIC: {
        int32_t fr = syl_ms(d1), av0 = c->voiced ? SYL_Q * 3 / 10 : 0;
        syl_bands_on(T, c, fr, syl_ms(15));
        SYL_PUT(T, SYL_AF, 0, 0, syl_ms(15), SYL_Q, fr, SYL_Q, fr + syl_ms(15), 0);
        syl_glide(T, fr, fr, 300, on2, on3, v, tr);
        SYL_PUT(T, SYL_AV, 0, av0, fr, av0, fr + syl_ms(15), SYL_V_LEVEL);
        break;
    }
    case SYL_K_H: {
        int32_t hh = syl_ms(d1);
        SYL_PUT(T, SYL_AH, 0, 0, syl_ms(10), SYL_Q * 7 / 10, hh, SYL_Q * 6 / 10, hh + syl_ms(20), 0);
        SYL_PUT(T, SYL_F1, 0, v[0]);
        SYL_PUT(T, SYL_F2, 0, v[1]);
        SYL_PUT(T, SYL_F3, 0, v[2]);
        SYL_PUT(T, SYL_AV, 0, 0, hh - syl_ms(10), 0, hh + syl_ms(10), SYL_V_LEVEL);
        break;
    }
    default: {                                           /* approximants: a glide from their own formants */
        int32_t gl = syl_ms(d2), a1 = c->approx_f1;
        SYL_PUT(T, SYL_F1, 0, a1, syl_ms(20), a1, gl, v[0]);
        SYL_PUT(T, SYL_F2, 0, f2l, syl_ms(20), f2l, gl, v[1]);
        SYL_PUT(T, SYL_F3, 0, f3l, syl_ms(20), f3l, gl, v[2]);
        SYL_PUT(T, SYL_AV, 0, SYL_Q * 4 / 10, syl_ms(20), SYL_Q * 5 / 10, gl, SYL_V_LEVEL);
        break;
    }
    }
    if (w) {                                             /* a diphthong: hold, then glide to the second vowel */
        int32_t g0 = T->t[SYL_F2][T->n[SYL_F2] - 1] + syl_ms(SYL_DIPH_HOLD_MS);
        int32_t g1 = g0 + syl_ms(x ? SYL_TRIPH_GLIDE_MS : SYL_DIPH_GLIDE_MS), g2 = g1 + syl_ms(SYL_DIPH_GLIDE_MS);
        for (k = 0; k < 3; k++) {                        /* (3 + 3 breakpoints at most: SYL_NBP) */
            uint32_t n = T->n[SYL_F1 + k];
            T->t[SYL_F1 + k][n] = (int16_t)g0;
            T->v[SYL_F1 + k][n] = v[k];
            T->t[SYL_F1 + k][n + 1] = (int16_t)g1;
            T->v[SYL_F1 + k][n + 1] = w[k];
            if (x) {
                T->t[SYL_F1 + k][n + 2] = (int16_t)g2;
                T->v[SYL_F1 + k][n + 2] = x[k];
            }
            T->n[SYL_F1 + k] = (uint8_t)(n + (x ? 3u : 2u));
        }
    }
}

int32_t syl_at(const syl_tracks_t *T, uint32_t k, uint32_t i)
{
    uint32_t j, n = T->n[k];
    const int16_t *t = T->t[k], *v = T->v[k];
    if ((int32_t)i <= t[0])
        return v[0];
    for (j = 0; j + 1 < n; j++)
        if ((int32_t)i < t[j + 1])
            return v[j] + (v[j + 1] - v[j]) * ((int32_t)i - t[j]) / (t[j + 1] - t[j]);
    return v[n - 1];
}

/* the closing consonant's tracks from the syllable's own values at tick `at`: read the start first,
 * then overwrite T */
void syl_coda_tracks(syl_tracks_t *T, uint32_t ci, uint32_t scale, uint32_t at)
{
    const syl_cons_t *c = &syl_cons[ci < SYL_NCONS ? ci : 0];
    int32_t d1 = c->d1 * (int32_t)scale / 8, d2 = c->d2 * (int32_t)scale / 8, tr = SYL_TRANS_MS * (int32_t)scale / 8;
    int32_t s1 = syl_at(T, SYL_F1, at), s2 = syl_at(T, SYL_F2, at), s3 = syl_at(T, SYL_F3, at), a0 = syl_at(T, SYL_AV, at);
    int32_t f2l = c->f2l, f3l = c->f3l, on2, on3, tc = syl_ms(tr);
    uint32_t k;
    if (f2l == 0)
        f2l = s2 + 300 > 1500 ? s2 + 300 : 1500;
    on2 = f2l + SYL_K_LOCUS * (s2 - f2l) / 8;
    on3 = f3l + SYL_K_LOCUS * (s3 - f3l) / 8;
    for (k = 0; k < SYL_NP; k++)
        SYL_PUT(T, k, 0, 0);
    SYL_PUT(T, SYL_B1, 0, syl_b123[0]);
    SYL_PUT(T, SYL_B2, 0, syl_b123[1]);
    SYL_PUT(T, SYL_B3, 0, syl_b123[2]);
#define SYL_CLOSE(t, f1, f2, f3) do { SYL_PUT(T, SYL_F1, 0, s1, t, f1); SYL_PUT(T, SYL_F2, 0, s2, t, f2); \
    SYL_PUT(T, SYL_F3, 0, s3, t, f3); } while (0)
#define SYL_BANDS_AT(t_on, t_off, fade) do { for (k = 0; k < 6; k++) if (c->bands[k]) \
    SYL_PUT(T, SYL_P2 + k, 0, 0, t_on, 0, (t_on) + 1, c->bands[k], t_off, c->bands[k], (t_off) + (fade), 0); } while (0)
    switch (c->kind) {
    case SYL_K_NONE:
        SYL_CLOSE(tc, s1, s2, s3);
        SYL_PUT(T, SYL_AV, 0, a0, syl_ms(60), 0);
        break;
    case SYL_K_STOP: {
        int32_t rel = tc + syl_ms(40), bar = c->voiced ? SYL_Q / 6 : 0, e = rel + syl_ms(d1);
        SYL_CLOSE(tc, 250, on2, on3);
        SYL_PUT(T, SYL_AV, 0, a0, tc, bar, rel, bar / 2, rel + 1, 0);
        SYL_BANDS_AT(rel, e, 1);
        SYL_PUT(T, SYL_AF, 0, 0, rel, 0, rel + 1, SYL_Q, e, SYL_Q, e + 1, 0);
        if (!c->voiced)
            SYL_PUT(T, SYL_AH, 0, 0, e, 0, e + 1, SYL_Q * 4 / 10, e + syl_ms(30), 0);
        break;
    }
    case SYL_K_NASAL: {
        int32_t tn = syl_ms(tr / 2);
        SYL_CLOSE(tc, 250, on2, on3);
        SYL_PUT(T, SYL_NAS, 0, 0, tn, 0, tn + 2, SYL_Q);
        SYL_PUT(T, SYL_B1, 0, syl_b123[0], tn, syl_b123[0], tn + 2, 100);
        SYL_PUT(T, SYL_AV, 0, a0, tn, SYL_Q * 4 / 10, tn + syl_ms(d2), SYL_Q * 4 / 10, tn + syl_ms(d2) + syl_ms(30), 0);
        break;
    }
    case SYL_K_FRIC: {
        int32_t e = tc + syl_ms(d1), av1 = c->voiced ? SYL_Q * 3 / 10 : 0;
        SYL_CLOSE(tc, 300, on2, on3);
        SYL_BANDS_AT(tc, e, syl_ms(30));
        SYL_PUT(T, SYL_AF, 0, 0, tc, SYL_Q, e, SYL_Q, e + syl_ms(30), 0);
        SYL_PUT(T, SYL_AV, 0, a0, tc, av1, e, av1, e + syl_ms(30), 0);
        break;
    }
    case SYL_K_H:
        SYL_CLOSE(tc, s1, s2, s3);
        SYL_PUT(T, SYL_AH, 0, 0, syl_ms(20), SYL_Q * 6 / 10, syl_ms(20) + syl_ms(d1), 0);
        SYL_PUT(T, SYL_AV, 0, a0, syl_ms(30), 0);
        break;
    default: {
        int32_t gl = syl_ms(d2);
        SYL_CLOSE(gl, c->approx_f1, f2l, f3l);
        SYL_PUT(T, SYL_AV, 0, a0, gl, SYL_Q * 3 / 10, gl + syl_ms(40), 0);
        break;
    }
    }
#undef SYL_CLOSE
#undef SYL_BANDS_AT
}

void syl_coda(syl_voice_t *s, uint32_t c, uint32_t scale)
{
    syl_coda_tracks(&s->tr, c, scale ? scale : 8, s->tick);
    s->tick = 0;
    s->sub = 0;
}

/* exp(-x) and cos(x), x in Q30 (x <= 0.1 and x <= 1.1): Taylor to the 4th and 8th power */
static int32_t syl_exp_neg(int32_t x)
{
    int64_t x2 = ((int64_t)x * x) >> 30, x3 = (x2 * x) >> 30, x4 = (x3 * x) >> 30;
    return (int32_t)((1LL << 30) - x + (x2 >> 1) - ((x3 * 178956971) >> 30) + ((x4 * 44739243) >> 30));   /* /6 /24 */
}

static int32_t syl_cos_q30(int32_t x)
{
    int64_t x2 = ((int64_t)x * x) >> 30, x4 = (x2 * x2) >> 30, x6 = (x4 * x2) >> 30, x8 = (x4 * x4) >> 30;
    return (int32_t)((1LL << 30) - (x2 >> 1) + ((x4 * 44739243) >> 30) - ((x6 * 1491308) >> 30) + ((x8 * 26631) >> 30));
}

static void syl_reson(syl_res_t *r, int32_t f, int32_t bw)
{
    int32_t e, cs;
    f = f < 20 ? 20 : f > 7000 ? 7000 : f;
    bw = bw < 10 ? 10 : bw > 2000 ? 2000 : bw;
    e = syl_exp_neg(bw * SYL_K_BW);
    cs = syl_cos_q30(f * SYL_K_F);
    r->c = (int32_t)(-(((int64_t)e * e) >> 30));
    r->b = (int32_t)(((int64_t)e * cs) >> 29);
    r->a = (int32_t)((1LL << 30) - r->b - r->c);
}

static inline int32_t syl_biq(const syl_res_t *r, int32_t x, int32_t *y)
{
    int32_t o = (int32_t)(((int64_t)r->a * x + (int64_t)r->b * y[0] + (int64_t)r->c * y[1] + (1 << 29)) >> 30);
    y[1] = y[0];
    y[0] = o;
    return o;
}

static void syl_coefs(syl_voice_t *s)
{
    static const int16_t PF[5] = {0, 0, 3500, 5000, 6500}, PB[5] = {150, 250, 400, 700, 1200};
    uint32_t k;
    for (k = 0; k < SYL_NP; k++)
        s->p[k] = syl_at(&s->tr, k, s->tick);
    if (s->fmul != 65536u || s->bmul != 65536u) {        /* SHIFT, Q: every resonance moves together */
        static const uint8_t FK[3] = {SYL_F1, SYL_F2, SYL_F3}, BK[3] = {SYL_B1, SYL_B2, SYL_B3};
        for (k = 0; k < 3; k++) {
            s->p[FK[k]] = (int32_t)(((int64_t)s->p[FK[k]] * s->fmul) >> 16);
            s->p[BK[k]] = (int32_t)(((int64_t)s->p[BK[k]] * s->bmul) >> 16);
        }
    }
    if (s->breath > s->p[SYL_AH])
        s->p[SYL_AH] = s->breath;
    syl_reson(&s->r[0], s->p[SYL_F1], s->p[SYL_B1]);
    syl_reson(&s->r[1], s->p[SYL_F2], s->p[SYL_B2]);
    syl_reson(&s->r[2], s->p[SYL_F3], s->p[SYL_B3]);
    syl_reson(&s->r[3], 3300, 250);
    for (k = 0; k < 5; k++)
        syl_reson(&s->rp[k], k == 0 ? s->p[SYL_F2] : k == 1 ? s->p[SYL_F3] : PF[k], PB[k]);
    if (s->p[SYL_NAS]) {                                 /* the zero, normalised by the pole's gain */
        syl_res_t z;
        int32_t fnz = 270 + (450 - 270) * s->p[SYL_NAS] / SYL_Q;
        syl_reson(&s->rn, 270, 100);
        syl_reson(&z, fnz, 100);
        s->nz0 = (int32_t)(((int64_t)s->rn.a << 30) / z.a);  /* A_p / A_z, below 1 */
        s->nz1 = (int32_t)(-(((int64_t)s->nz0 * z.b) >> 30));
        s->nz2 = (int32_t)(-(((int64_t)s->nz0 * z.c) >> 30));
    }
}

void syl_pitch(syl_voice_t *s, uint32_t f0_q16)
{
    s->inc = (uint32_t)((((uint64_t)f0_q16) << 16) / SYL_FS);
}

void syl_start(syl_voice_t *s, uint32_t c, uint32_t v, uint32_t scale, int retrig)
{
    if (!retrig) {
        uint8_t *p = (uint8_t *)s;
        uint32_t i, inc = s->inc;
        for (i = 0; i < sizeof *s; i++)
            p[i] = 0;
        s->inc = inc;
        s->rng = 0x2545F491u;
        s->fmul = s->bmul = 65536u;
    }
    syl_tracks(&s->tr, c, v, scale ? scale : 8);
    s->tick = s->sub = 0;
}

void syl_render(syl_voice_t *s, int32_t *out, uint32_t n)
{
    /* the polyBLEP's 1 / inc, once a call: a 32-bit divide, none per sample (the device has no 64-bit one) */
    uint32_t rinc = (s->inc >> 16) ? 0xFFFFFFFFu / (s->inc >> 16) : 0u;
    while (n--) {
        int32_t u = 0, nz, x, y, fx, par = 0, o;
        uint32_t k;
        if (s->sub == 0)
            syl_coefs(s);
        if (s->ph < SYL_OQ32) {                              /* KLGLOTT88: 2t - 3t^2 over the open phase */
            int32_t t = (int32_t)(((s->ph >> 16) * SYL_OQ_RECIP) >> 16);
            u = (int32_t)((((int64_t)(2 * t - (int32_t)(((int64_t)3 * t * t) >> 16))) * SYL_SRC_GAIN_Q15) >> 15);
        }
        if (rinc) {                                      /* the closure's step, band-limited (polyBLEP) */
            uint32_t q = s->ph - SYL_OQ32;
            int32_t t, r = 0;
            if (q < s->inc) {
                t = (int32_t)(((q >> 16) * rinc) >> 16);
                r = 2 * t - (int32_t)(((int64_t)t * t) >> 16) - 65536;
            } else if (q > 0u - s->inc) {
                t = -(int32_t)((((0u - q) >> 16) * rinc) >> 16);
                r = (int32_t)(((int64_t)t * t) >> 16) + 2 * t + 65536;
            }
            u += (int32_t)(((int64_t)r * SYL_SRC_GAIN_Q15) >> 16);   /* r x SRC_GAIN / 2 */
        }
        s->ph += s->inc;
        s->rng ^= s->rng << 13;
        s->rng ^= s->rng >> 17;
        s->rng ^= s->rng << 5;
        nz = (int32_t)s->rng >> 15;                      /* Q16, [-1, 1) */
        x = (int32_t)(((int64_t)s->p[SYL_AV] * u) >> 15) + (int32_t)(((int64_t)s->p[SYL_AH] * nz * SYL_NOISE_ASP_Q15) >> 30);
        if (s->p[SYL_NAS]) {
            int32_t w = (int32_t)(((int64_t)s->nz0 * x + (int64_t)s->nz1 * s->xn[0] + (int64_t)s->nz2 * s->xn[1] + (1 << 29)) >> 30);
            syl_res_t pole = {1 << 30, s->rn.b, s->rn.c};
            s->xn[1] = s->xn[0];
            s->xn[0] = x;
            y = syl_biq(&pole, w, s->yn);
        } else {
            s->xn[1] = s->xn[0];
            s->xn[0] = x;
            s->yn[1] = s->yn[0];
            s->yn[0] = x;
            y = x;
        }
        for (k = 0; k < 4; k++)
            y = syl_biq(&s->r[k], y, s->y[k]);
        fx = (int32_t)(((int64_t)s->p[SYL_AF] * nz) >> 15);
        if (!(s->p[SYL_AF] || s->p[SYL_AB] || s->p[SYL_P2] || s->p[SYL_P3] || s->p[SYL_P4] || s->p[SYL_P5] || s->p[SYL_P6])) {
            if (s->ablp || s->yp[0][0] || s->yp[4][0]) {   /* the branch idles, its state cleared */
                uint8_t *z = (uint8_t *)s->yp;
                for (k = 0; k < sizeof s->yp; k++)
                    z[k] = 0;
                s->ablp = 0;
            }
        } else {
            int32_t sign = 1;
            s->ablp += (int32_t)(((int64_t)SYL_AB_LP_Q15 * (fx - s->ablp)) >> 15);
            par = (int32_t)(((int64_t)s->p[SYL_AB] * s->ablp) >> 15);
            for (k = 0; k < 5; k++) {
                int32_t b = syl_biq(&s->rp[k], fx, s->yp[k]);
                par += sign * (int32_t)((((int64_t)s->p[SYL_P2 + k] * b) >> 15) * SYL_PAR_GAIN_X2 >> 1);
                sign = -sign;
            }
        }
        o = (int32_t)(((int64_t)(y + par) * SYL_OUT_GAIN_Q15) >> 16);   /* Q16 x 0.2 -> Q15 */
        *out++ += o < -32768 ? -32768 : o > 32767 ? 32767 : o;
        if (++s->sub == SYL_CTL) {
            s->sub = 0;
            s->tick++;
        }
    }
}
