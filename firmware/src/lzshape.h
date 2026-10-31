/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * lzshape: what shapes a voice after its oscillator, for the FM-1 (the SHAPE pages). LIZARD's
 * page is UNI WARP FOLD SYM; every other engine's is FOLD SYM CRUSH DOWN, the same stage on its voice.
 *
 *   UNI   the drawing as a supersaw: 1 to 5 copies of the 8-step drawing, spread in pitch by
 *         DTN (up to +-2.5 %), each its own start phase. DRAW mode only: the other modes'
 *         steps are waves, not levels, and PSAW / SUPR are unison already
 *   WARP  bends the cycle's time: the first half of the steps squeezed into less of the
 *         period, the rest stretched (Casio's phase distortion, applied to a drawing). With
 *         UNI it replaces the core's drawer, so it is DRAW only too
 *   FOLD  a triangle wavefolder: the wave driven 1x to 8x into it, folding back at +-1.
 *         Antiderivative antialiasing (ADAA, first order), so the folds alias little
 *         without oversampling
 *   SYM   the folder's bias: asymmetric folds and even harmonics; a DC blocker follows
 *   CRUSH the voice quantised: 16 bits down to 3
 *   DOWN  the voice's sample rate divided: each value held 1x to about 32x (a phase accumulator,
 *         so the rate moves smoothly, not in integer jumps)
 *
 * The drawer here is lizard.c's in a more general form: any step widths (WARP) and any
 * number of phases (UNI), every step edge band-limited by the same 2-point polyBLEP. It
 * plays one sample late so a crossing can correct the sample before it. Float only, no
 * libm, no double; phases are uint32. At UNI 1, WARP 0 and FOLD CRUSH DOWN 0 nothing runs:
 * the voice is the engine's, bit for bit. */
#ifndef LZSHAPE_H
#define LZSHAPE_H
#include <stdint.h>

#define LZS_MAX_UNI 5
#define LZS_MAX_STEPS 16

typedef struct {
    uint32_t ph[LZS_MAX_UNI];        /* each copy's pattern phase */
    float hold;                      /* the drawer's sample held back for the next one's edge */
    float fx1, fF1;                  /* the folder: last input and its antiderivative */
    float dcx, dcy;                  /* the DC blocker */
    float dacc, dheld;               /* DOWN: the hold's phase and the held value */
} lzs_t;

typedef struct {
    int uni;                         /* 1..5 */
    float detune;                    /* 0..1: the spread, up to +-2.5 % */
    float warp;                      /* 0..1 */
    float fold;                      /* 0..1: 0 = off, else 1x .. 8x drive */
    float sym;                       /* -1..1 */
    float crush;                     /* 0..1: 0 = off, 1 = 3 bits */
    float down;                      /* 0..1: 0 = off, 1 = held about 32 samples */
    int blep;                        /* 0: raw edges (EDGE RAW), else polyBLEP */
} lzs_par;

void lzs_init(lzs_t *s, uint32_t seed);
/* a note: every copy back to its own start (the centre at 0, as lz_note) */
void lzs_note(lzs_t *s, uint32_t seed);
/* the drawer runs (UNI > 1 or WARP > 0): the caller renders with lzs_draw instead of lz_wavedrawer */
int lzs_draws(const lzs_par *p);
/* n samples of the drawing into out: levels[order[k]] for step k of n_steps, at freq / sr cycles a sample
 * (a pattern of n_steps is one cycle, as lz_wavedrawer) */
void lzs_draw(lzs_t *s, const lzs_par *p, const float levels[8], const int *order, int n_steps,
              float freq, float sr, float *out, int n);
/* the folder and its DC blocker, in place (does nothing at FOLD 0) */
void lzs_fold(lzs_t *s, const lzs_par *p, float *buf, int n);
/* FOLD, then CRUSH, then DOWN, in place: a SHAPE page's whole stage (each skipped at 0) */
void lzs_shape(lzs_t *s, const lzs_par *p, float *buf, int n);
/* the same on an engine's integer voice (full scale +-32768): what the shell runs on a voice it rendered alone */
void lzs_shape_i32(lzs_t *s, const lzs_par *p, int32_t *buf, int n);
/* any of FOLD, CRUSH, DOWN on: the stage has work to do */
int lzs_shapes(const lzs_par *p);

#endif
