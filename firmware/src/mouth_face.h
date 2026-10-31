/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * mouth_face: the lips a vowel makes, drawn from its formants, for MOUTH's screen. After the plaster casts of the
 * mouth Marage made around 1900, one per vowel (I, E, A, O, OU).
 *
 * F1 follows the jaw: low (~270 Hz) nearly closed, high (~850 Hz) wide open. F2 follows the lips and the tongue:
 * high (~2300 Hz) spread, the corners pulled back and the upper teeth showing; low (~850 Hz) rounded, the lips
 * pushed forward into a small round hole. So the picture moves with the sound, and a vowel knob turning while a
 * note holds opens and rounds the mouth as it goes.
 *
 * Out: two closed contours in a box of +-1 wide (y down, as on the screen): the lips' outline and the opening
 * inside it, MF_N points each, and the teeth's band (top edge and depth, 0 when hidden). Floats: the shell draws
 * outside the audio interrupt. Header only. */
#ifndef MOUTH_FACE_H
#define MOUTH_FACE_H

#define MF_N 48

typedef struct {
    float ox[MF_N], oy[MF_N];            /* the lips' outline */
    float ix[MF_N], iy[MF_N];            /* the opening */
    float teeth_y, teeth_h;              /* the upper teeth: their top edge and how far they hang, 0: hidden */
    float open, spread;                  /* 0 .. 1, what the formants asked for */
} mf_shape_t;

static inline float mf_clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* sin and cos of a turn's fraction u (0 .. 1), a quarter-wave polynomial: enough for a drawing */
static inline void mf_sincos(float u, float *s, float *c)
{
    float x = u - (float)(int)u, q;
    int k;
    x = x < 0.0f ? x + 1.0f : x;
    k = (int)(x * 4.0f);
    q = (x * 4.0f - (float)k) * 1.5707963f;
    {
        float q2 = q * q, sq = q * (1.0f - q2 / 6.0f * (1.0f - q2 / 20.0f * (1.0f - q2 / 42.0f))),
              cq = 1.0f - q2 / 2.0f * (1.0f - q2 / 12.0f * (1.0f - q2 / 30.0f));
        *s = k == 0 ? sq : k == 1 ? cq : k == 2 ? -sq : -cq;
        *c = k == 0 ? cq : k == 1 ? -sq : k == 2 ? -cq : sq;
    }
}

static inline void mf_shape(float f1, float f2, mf_shape_t *m)
{
    /* rounding is F2 below where an unrounded vowel with this F1 has it (the line from I's 270/2290 Hz to A's
     * 730/1090): OU and O sit far under it, I, E and A on or over it */
    float o = mf_clamp01((f1 - 250.0f) / 550.0f), ref = 2290.0f - (f1 - 270.0f) * 2.6f;
    float rd = mf_clamp01((ref - f2) / 700.0f), sp = mf_clamp01((f2 - 1150.0f) / 1100.0f) * (1.0f - rd);
    float wi = 0.42f + 0.40f * sp - 0.24f * rd + 0.16f * o * (1.0f - rd);   /* the opening's half width */
    float hi = 0.03f + 0.36f * o;                     /* its half height: the jaw */
    float tu = 0.11f + 0.10f * rd - 0.03f * o, tl = 0.15f + 0.12f * rd - 0.03f * o;   /* lips: pursed fuller,
                                                       * stretched thinner */
    float wo = wi + 0.08f + 0.10f * rd;               /* the outline's half width at the corners */
    int i;
    m->open = o;
    m->spread = sp;
    for (i = 0; i < MF_N; i++) {
        float s, c, u = (float)i / MF_N, lens, bow;
        mf_sincos(u, &s, &c);
        lens = 1.0f - 0.45f * sp * c * c;            /* spread lips meet in pointed corners, rounded ones do not */
        m->ix[i] = wi * c;
        m->iy[i] = -hi * s * lens;
        bow = s > 0.0f ? 0.07f * (1.0f - (c * c) * 4.0f) : 0.0f;   /* the cupid's bow at the top of the upper lip */
        bow = bow < 0.0f ? 0.0f : bow;
        m->ox[i] = wo * c;
        m->oy[i] = s > 0.0f ? -(hi + tu) * s * (1.0f - 0.25f * sp * c * c) + bow * s
                            : -(hi + tl) * s * (1.0f - 0.20f * sp * c * c);
    }
    /* the upper teeth show when the lips spread and the jaw has dropped a little */
    m->teeth_h = sp > 0.45f && o > 0.08f ? (0.05f + 0.10f * o) * (sp - 0.45f) / 0.55f * 2.0f : 0.0f;
    m->teeth_y = -hi * 0.9f;
}

#endif
