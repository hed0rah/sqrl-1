/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * syllable: a consonant-vowel syllable from a Klatt (1980) style cascade/parallel formant
 * synthesizer, integer only, no allocation, one caller-owned struct per voice. The port of a
 * reference model (not published): the same breakpoint tracks bit for bit, the same filters in
 * Q30 / Q16 fixed point. */
#ifndef SYLLABLE_H
#define SYLLABLE_H
#include <stdint.h>

#define SYL_FS 44100
#define SYL_CTL 64                   /* samples per tick (1.45 ms): the tracks' time unit */
#define SYL_NBP 6                    /* breakpoints per track at most */
#define SYL_NVOWEL 18                /* 12 vowels, 5 diphthongs, then the triphthong iau */
#define SYL_NCONS 19

enum { SYL_AV, SYL_AH, SYL_AF, SYL_F1, SYL_F2, SYL_F3, SYL_B1, SYL_B2, SYL_B3, SYL_NAS,
       SYL_P2, SYL_P3, SYL_P4, SYL_P5, SYL_P6, SYL_AB, SYL_NP };

extern const char *const SYL_VOWEL_NAMES[SYL_NVOWEL];   /* i I e E ae a V O o U u er ai au ei oi ou iau */
extern const char *const SYL_CONS_NAMES[SYL_NCONS];     /* - b d g p t k m n s z sh f v h l r w y */

typedef struct {
    int16_t t[SYL_NP][SYL_NBP];
    int16_t v[SYL_NP][SYL_NBP];
    uint8_t n[SYL_NP];
} syl_tracks_t;

typedef struct { int32_t a, b, c; } syl_res_t;          /* y = a x + b y1 + c y2, Q30 */

typedef struct {
    syl_tracks_t tr;
    uint32_t tick, sub;              /* the tick, the sample within it */
    uint32_t ph, inc;                /* glottal phase (a fraction of the period, Q32) */
    uint32_t rng;
    int32_t p[SYL_NP];               /* this tick's parameters */
    syl_res_t r[4], rp[5], rn;       /* F1..F4, P2..P6, the nasal pole */
    int32_t nz0, nz1, nz2;           /* the nasal zero, Q30 (FIR) */
    int32_t y[4][2], yp[5][2], yn[2], xn[2];
    int32_t ablp;
    /* the player's controls; at their defaults the output is the reference's */
    uint32_t fmul, bmul;             /* formants and bandwidths x, Q16 (65536 = as the tracks) */
    int32_t breath;                  /* aspiration under the whole syllable, Q15 (0 = none) */
} syl_voice_t;

/* the tracks of consonant c into vowel v; scale: the consonant's length in eighths (8 = as the table) */
void syl_tracks(syl_tracks_t *t, uint32_t c, uint32_t v, uint32_t scale);
/* the value of track k at tick i */
int32_t syl_at(const syl_tracks_t *t, uint32_t k, uint32_t i);
/* a new syllable on voice s at pitch f0 (Hz, Q16); keeps the filter state when retrig is 0 */
void syl_start(syl_voice_t *s, uint32_t c, uint32_t v, uint32_t scale, int retrig);
void syl_pitch(syl_voice_t *s, uint32_t f0_q16);
/* the closing consonant c from where the syllable is now (a key let go): its tick 0 starts with the
 * next sample, the filters and the glottal phase run on */
void syl_coda(syl_voice_t *s, uint32_t c, uint32_t scale);
void syl_coda_tracks(syl_tracks_t *t, uint32_t c, uint32_t scale, uint32_t at);
/* n samples, Q15, added to out */
void syl_render(syl_voice_t *s, int32_t *out, uint32_t n);

#endif
