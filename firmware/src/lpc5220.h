/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * lpc5220: the TMS5220 speech generator as a playable voice. Integer only, no allocation, all
 * state in one caller-owned struct (arena-ready). See lpc5220.c for the tables' BSD-3 notice.
 *
 * With every control at its default the output is sample-for-sample the generator of
 * a reference model of the chip (not published).
 * The controls bend it the ways a player wants and the chip could not:
 *   period   the voiced excitation period from a key, fractional, instead of the frame's pitch
 *   rate     frames per second without a pitch change (timing steps per sample, Q8)
 *   interp   the parameter interpolation: the chip's shift table, a hard step, or a slow slur
 *   glitch   bit errors on the frame stream as it is read
 * lpc5220_out_t runs the 8 kHz chip at any internal rate (the clock bend) under a 44.1 kHz host. */
#ifndef LPC5220_H
#define LPC5220_H
#include <stdint.h>

#define LPC5220_SR 8000
#define LPC5220_FRAME 200          /* samples per frame at rate 1: 25 ms */

enum { LPC_INTERP_CHIP, LPC_INTERP_STEP, LPC_INTERP_SLUR, LPC_INTERP_N };

typedef struct {
    /* the frame stream */
    const uint8_t *data;
    uint32_t nbits, pos;
    /* the frame being interpolated toward, and the current parameters */
    uint8_t new_e, new_p, new_k[10];
    int32_t e_cur, p_cur, k_cur[10];
    /* the chip's flags */
    uint8_t zpar, uv_zpar, olde, oldp, inhibit, pitch_zero, done, stopping, ended;
    /* timing: interpolation period, parameter counter, sub-cycle */
    uint8_t ip, pc, sub;
    uint16_t rng;                  /* 13-bit noise LFSR */
    int32_t pitch_count;
    int32_t prev_e;
    int32_t u[11], x[10];          /* the lattice */
    /* controls */
    uint32_t period_q8;            /* 0 = the frame's pitch (the chip); else samples * 256 */
    uint32_t pcount_q8;
    uint32_t rate_q8;              /* 256 = the chip */
    uint32_t rate_acc;
    uint8_t interp;
    uint16_t glitch_q16;           /* probability that a read bit flips, Q16 */
    uint32_t grng;
} lpc5220_t;

void lpc5220_init(lpc5220_t *s);
/* start a packed frame stream (the TMS5220's bit packing); controls are kept */
void lpc5220_start(lpc5220_t *s, const uint8_t *data, uint32_t nbytes);
/* one 8 kHz sample, the chip's 12-bit output in 16-sample steps (-2048 .. 2032); 0 once ended */
int32_t lpc5220_tick(lpc5220_t *s);
static inline int lpc5220_ended(const lpc5220_t *s) { return s->ended; }

/* the chip under a host rate: ZOH (the staircase, as the chip's DAC) or linear */
typedef struct {
    lpc5220_t chip;
    uint32_t acc, inc;             /* chip ticks per host sample, Q32 (below 1) */
    int32_t hold, prev;
    uint8_t linear;
} lpc5220_out_t;

void lpc5220_out_init(lpc5220_out_t *o, uint32_t host_sr);
/* the chip's clock as a fraction of 8 kHz (Q16, 65536 = 8 kHz): the circuit-bend knob. Formants
 * and the frame's own pitch move together; a key period (period_q8) moves with it too */
void lpc5220_out_clock(lpc5220_out_t *o, uint32_t host_sr, uint32_t clock_q16);
/* n host samples, Q15 (the chip's full scale = 32767), added to out */
void lpc5220_out_render(lpc5220_out_t *o, int32_t *out, uint32_t n);

#endif
