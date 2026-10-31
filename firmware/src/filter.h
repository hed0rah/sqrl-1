/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * filter: a state-variable filter and a tuned comb, as inserts, for the FM-1's audio interrupt (its single-precision
 * FPU; no libm, no double).
 *
 * FILTER is a topology-preserving (trapezoidal, Zavalishin / Simper) state-variable filter: stable at any cutoff,
 * any resonance and any rate of change, so CUT can be swept by a knob, an envelope or the keys without the zipper or
 * blow-up a direct-form biquad has. MORPH turns its output continuously LP (0) -> BP (42) -> HP (85) -> NOTCH (127)
 * by crossfading the three outputs it computes at once. RES runs the Q from 0.5 (k 2) up to 128 (k 1/128), and the
 * band integrator's state is linear to +12 dB and soft-bound at +18, so the top of RES rings near self-oscillation without running
 * away. DRIVE puts the input through the same saturator first, 1x to 16x, with a quarter-scale input's level held, so it is
 * the filter's character rather than its volume. KTRK moves CUT with the newest note the part holds (0 none, 127 a
 * semitone a semitone, about C4), so at KTRK 127 a resonant filter plays the melody's pitch. ENV is a follower on the
 * input (instant attack, 60 ms release), bipolar around 64: up to 3 octaves up (127) or down (0) at a loud input.
 *
 * COMB is a delay tuned to a pitch, fed back: FDBK bipolar around 64. Positive feedback rings every harmonic of the
 * pitch (a plucked string, a flanger frozen); negative rings the odd ones only (a hollow tube) and the delay is
 * halved so the pitch stays where TUNE puts it. DAMP is a low-pass in the loop, so the ring darkens as it decays.
 * TUNE, KTRK and ENV as the FILTER's CUT, KTRK and ENV; DRIVE saturates what goes round the loop. The loop runs on
 * the host's int16 line (FLANG's, 256 samples: the lowest pitch 172 Hz with positive feedback, 86 Hz negative).
 *
 * Levels: in holds int16 values at the host's half scale, in an int32 block as RESON's does (no staging copy), out
 * int32 at the same scale, clamped at 8x (+18 dB): a resonant peak or a comb ring comes back louder than its input,
 * as RESON's wet does, and the host mixes in int32.
 * Coefficients are worked out once a block (n samples, the host's CTL), so modulation is block-rate.
 * Both fall to exact silence when the input stops (states under the int16 output's last bit are zeroed). */
#ifndef FILTER_H
#define FILTER_H
#include <stdint.h>

typedef struct {
    uint8_t cut;                         /* FILTER: CUT, 0 .. 127: 20 Hz .. 18.5 kHz (0.93 semitone a step); COMB: TUNE */
    uint8_t res;                         /* FILTER: RES, 0 .. 127: Q 0.5 .. 128; COMB: FDBK, 64 none, 0 -0.97, 127 +0.97 */
    uint8_t morph;                       /* FILTER: MORPH, LP BP HP NOTCH; COMB: DAMP, 0 bright .. 127 dark */
    uint8_t drive;                       /* 0 .. 127: 1x .. 16x into the saturator, level held */
    uint8_t ktrk;                        /* 0 .. 127: CUT / TUNE follows the newest note, none .. a semitone a semitone */
    uint8_t env;                         /* 0 .. 127, 64 none: the follower moves CUT / TUNE down .. up, 3 octaves */
} fl_param_t;

typedef struct {
    float ic1, ic2;                      /* FILTER: the two integrators' states */
    float env;                           /* the follower */
    float lp;                            /* COMB: the loop's low-pass */
    float dly;                           /* COMB: the delay at the last block's end (samples; 0 before the first) */
    float note;                          /* the note KTRK follows (MIDI, fractional), 60 until one is held */
    uint32_t wp;                         /* COMB: the line's next write */
} fl_t;

void fl_init(fl_t *f);
/* the newest note the part holds, in 1/16 semitone (MIDI x 16), or < 0 for none (the last one stays) */
void fl_note(fl_t *f, int32_t p16);
void fl_filter(fl_t *f, const fl_param_t *p, const int32_t *in, int32_t *out, uint32_t n);
/* line: the host's int16 line, len a power of 2 (256) */
void fl_comb(fl_t *f, const fl_param_t *p, int16_t *line, uint32_t len, const int32_t *in, int32_t *out, uint32_t n);
/* the FILTER's magnitude at hz for p, as it stands (the follower and the note it holds included): for the screen's
 * response curve; the analog prototype the filter is bilinear from (the same curve below ~5 kHz, then squeezed
 * toward fs / 2) */
float fl_mag(const fl_t *f, const fl_param_t *p, float hz);
/* the cutoff (FILTER) or pitch (COMB) in Hz the next block runs at, for the screen */
float fl_freq(const fl_t *f, const fl_param_t *p);

#endif
