/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn
 * The lizard oscillator core. */
/* lizard oscillator modes for the FM-1, after the Vinicius Electrik Lizard v2.
 *
 * the Lizard runs naive (not band-limited) waves at 1 MHz. the FM-1's DAC runs at
 * 44.1 kHz in 64-sample blocks, so every edge here gets a polyBLEP correction.
 * phases are uint32 accumulators (2^32 = one cycle), the rest is float, no doubles: the
 * pi32v2's FPU is unknown, and a fixed-point pass (Teensy 3.2, no FPU) may follow.
 */
#ifndef LIZARD_H
#define LIZARD_H

#include <stdint.h>

#define LZ_BLOCK 64
#define LZ_MAX_STEPS 16
#define LZ_MAX_VOICES 5
#define LZ_HB_N 4                 /* HQ mode's halfband: allpass sections */

/* o->blep: 0 naive (the Lizard's own method, aliasing at 44.1 kHz), 1 2-point polyBLEP,
 * LZ_BLEP_HQ 2x oversampled polyBLEP through an IIR halfband (about 2x the cost, flat with
 * pitch; antialias/README.md) */
#define LZ_BLEP_HQ 2

/* how a cycle walks the 8 steps (the Lizard's Type key in WaveDrawer) */
enum lz_sweep { LZ_UP, LZ_DOWN, LZ_UPDOWN, LZ_RANDOM };

/* what a step plays (the digital stand-in for the Lizard's 8-input switcher) */
enum lz_src { LZ_DC, LZ_SINE, LZ_TRI, LZ_SAW };

typedef struct {
    enum lz_src src;
    float level;                  /* DC level, or gain on the wave */
} lz_step;

typedef struct {
    float sr;
    uint32_t phase;               /* pattern position, 2^32 = one pattern */
    float freq;
    int blep;                     /* 0 naive, 1 polyBLEP, LZ_BLEP_HQ */
    float hb[2 * LZ_HB_N];        /* HQ halfband state */

    /* wavedrawer / switcher */
    lz_step steps[8];
    enum lz_sweep sweep;
    int whole_cycles;             /* 1: each step plays a whole source cycle (CZ style) */
    int order[LZ_MAX_STEPS], next_order[LZ_MAX_STEPS], n;
    int prev_last;                /* last step played by the previous pattern */
    uint32_t rng;

    /* phasedsaw / superwave */
    int voices;
    float detune[LZ_MAX_VOICES];  /* ratio offset from the center, e.g. +0.01 */
    float mix[LZ_MAX_VOICES];
    uint32_t vphase[LZ_MAX_VOICES];
    uint32_t start[LZ_MAX_VOICES];  /* each voice's phase at note-on */
} lz_osc;

void lz_init(lz_osc *o, float sr, uint32_t seed);
void lz_set_sweep(lz_osc *o, enum lz_sweep s);

/* start a note: every consumer (plugin, firmware) retriggers through here */
void lz_note(lz_osc *o, float hz);

/* multisaw setups. phasedsaw: 2..5 saws, amount 0..1 spreads detune and start phase.
 * superwave: center + 2 down + 2 up, detune 0..1 (up to about +-2.5 %), mix 0..1 per slave */
void lz_phasedsaw(lz_osc *o, int voices, float amount);
void lz_superwave(lz_osc *o, float detune, const float mix[4]);

/* one block of output, freq may change between blocks */
void lz_wavedrawer(lz_osc *o, float *out, int n);
/* the drawer's phase step per sample at the current freq: what lz_wavedrawer adds to
 * o->phase each sample of the next block. a synced consumer rebuilds per-sample phase
 * with this instead of copying the formula */
uint32_t lz_phase_inc(const lz_osc *o);

/* the same, also writing which of the 8 steps (0..7, after the sweep order) each sample
 * plays, so a switcher can follow the drawer's edges exactly. step_out may be NULL */
void lz_wavedrawer_steps(lz_osc *o, float *out, int *step_out, int n);
void lz_multisaw(lz_osc *o, float *out, int n);

#endif
