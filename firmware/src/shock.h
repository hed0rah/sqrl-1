/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * shock: the shock-wave phase-distortion oscillator for the FM-1, a sine steepened the way a loud
 * tone steepens down a brass bore. The physics, the claims and the reference model are databurn's
 * phase-distortion research (its reference oscillator is tests/test_shock.cpp's yardstick): the sample read at
 * phase theta is
 * sin(phi) with theta = phi - sigma sin(phi), Earnshaw's simple wave, which is Kepler's equation.
 * sigma is the distance travelled in shock-formation lengths: 0 a sine, 1 the shock forming, above it
 * a decaying sawtooth.
 *
 * Here the wave comes from a table (shock_table.h, make_shock_table.py) rather than a Newton solve
 * a sample: a row per sigma, half a period each, read bilinearly. The one step past sigma = 1, at the
 * seam, gets the reference's polyBLEP of weight sin(phi_s). Around it: a sine an octave down (SUB),
 * breath noise (BRTH), and a one-pole low-pass for the bore's loss (LOSS). Float, no libm, no double;
 * the phase is a uint32. */
#ifndef SHOCK_H
#define SHOCK_H
#include <stdint.h>

typedef struct {
    uint32_t ph, sub;                    /* the wave's phase and the sub's (half its rate) */
    float lp;                            /* the loss filter */
    uint32_t rng;
} shock_t;

typedef struct {
    float sigma;                         /* 0 .. 3 */
    float loss;                          /* the low-pass's coefficient per sample, 0 (shut) .. 1 (open) */
    float sub;                           /* 0 .. 1 */
    float breath;                        /* 0 .. 1 */
    int blep;                            /* 0: the raw shock (EDGE RAW) */
} shock_par;

void shock_init(shock_t *s, uint32_t seed);
void shock_note(shock_t *s);             /* a note from the start of the period */
/* n samples at phase increment inc (2^32 = a cycle a sample) into out, overwritten, about +-1 */
void shock_render(shock_t *s, const shock_par *p, uint32_t inc, float *out, int n);
/* one period's sample at phase ph for sigma, without the BLEP: for drawing and tests */
float shock_wave(float sigma, uint32_t ph);

#endif
