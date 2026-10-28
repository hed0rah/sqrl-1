/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * resonator: a bank of tuned resonators that ring at the notes the part is playing, so noise, speech, plucks or
 * drums sound as the held chord. An insert, integer, for the FM-1's audio interrupt; after the Quadraverb's
 * resonator and the Disting EX filter bank, our own code.
 *
 * The host tells the bank the part's sounding notes once a block, up to eight, in 1/16 semitone with their fine
 * tuning, so a just-tuned chord rings just. Each note gets a slot of three resonators: its fundamental, its octave
 * and its twelfth (2x and 3x the frequency). DECAY is how long they ring, 50 ms (0) to 4 s (127) to -60 dB, as the
 * Q. BRIGHT brings in the octave and the twelfth, up to -3 and -6 dB under the fundamental at 127; at 0 only the
 * fundamentals ring. SHIFT moves the whole bank, an octave down (0) to an octave up (127) in semitones, so the
 * middle of the knob is the chord as played and the ends ring its octaves, with its fourths and fifths on
 * the way. MIX crossfades the dry sound (0, bit-exact) to the wet (127).
 *
 * A note that goes on keeps its slot and its ringing; a note that ends keeps ringing out, its slot the first to be
 * taken when a new note needs one, and a bent note retunes its own slot. A retune glides the resonator's
 * frequency across one block from where it was, so the ringing bends instead of clicking. With no notes held the
 * bank keeps the last chord, still fed, so a pad's release rings on; while notes are held, the slots that ended
 * only ring out. New frequencies arrive at most six resonators a block (the fundamentals first), so an eight-note
 * chord with BRIGHT up is fully tuned within four blocks.
 *
 * RZ_MAX_ACTIVE resonators run a block (12 unless the build says otherwise: the device's audio interrupt is near
 * its limit with four parts). The budget goes to every slot's fundamental first, then the octaves, then the
 * twelfths, held notes before ended ones and the newest first within each, so with 12 an eight-note chord rings
 * every fundamental and the newest four notes' octaves, and BRIGHT past that is not run. A ringing resonator the
 * budget no longer covers fades out over one block; none is cut.
 *
 * Each resonator is a coupled pair of states rotated by 2 sin(pi f / fs) (the "magic circle" oscillator, with
 * damping on one state), which keeps its frequency exact at the lowest notes where a direct-form biquad's
 * coefficients run out of bits. Its input gain is the inverse of its peak gain, worked out from the transfer
 * function at the note, so every resonator peaks at the same gain however long it rings: 2 (+6 dB) at DECAY 0
 * rising to 4 (+12 dB) at DECAY 127. On white noise that puts a single resonator's wet 19 dB under the dry at
 * DECAY 0 and 30 dB under at DECAY 127 (a long ring takes a narrow band of a noise), an eight-note chord with
 * BRIGHT 127 under the budget of 12 at -4 .. -22 dB; a chord's own notes come back at the bank's peak gain. The
 * wet is not held to
 * int16: a sine on a resonator comes back at up to 4x, and up to 6.8x when a note's octave partial sits on
 * another note's fundamental; it is clamped at 8x int16 (+18 dB), and the dry mix is done in int32. The states
 * keep 13 bits under the output's last one, the damping rounds toward zero so a ring never freezes, and a
 * resonator that can no longer move the output is set to exact silence at the block's end: the bank is silent a
 * few seconds after the input stops, whatever DECAY.
 *
 * Cost on a PC (tests/test_resonator.c): ~2 ns a sample a resonator, one note at BRIGHT 0 ~6 ns, the budget's 12
 * ~26 ns (24 without one ~50 ns); the device's numbers come from its own bench. 380 bytes of state. */
#ifndef RESONATOR_H
#define RESONATOR_H
#include <stdint.h>

#define RZ_NOTES 8                       /* slots: notes the bank rings at once */
#define RZ_PART 3                        /* resonators a note: fundamental, octave, twelfth */
#define RZ_N (RZ_NOTES * RZ_PART)
#ifndef RZ_MAX_ACTIVE
#define RZ_MAX_ACTIVE 12                 /* resonators run a block: the budget (RZ_N for none) */
#endif

typedef struct {
    uint8_t decay;                       /* 0 .. 127: 50 ms .. 4 s to -60 dB */
    uint8_t bright;                      /* 0 .. 127: the octave and twelfth, none .. -3 and -6 dB */
    uint8_t shift;                       /* 0 .. 127: an octave down .. as played (62 .. 66) .. an octave up */
    uint8_t mix;                         /* 0 .. 127: dry .. wet */
} rz_param_t;

typedef struct {
    int32_t s[RZ_N][2];                  /* each resonator's pair of states, Q13 on int16 scale; j = k * 8 + slot */
    int32_t e[RZ_N];                     /* its rotation 2 sin(pi f / fs) as it glides, Q30 */
    int16_t kg[RZ_N];                    /* its input gain over (1 - r): the peak gain's inverse at its note, Q14 */
    int16_t pitch[RZ_NOTES];             /* each slot's note, 1/16 semitone (-1: never used) */
    uint8_t held[RZ_NOTES];              /* 0 once the note ends, else its stamp from seq: higher is newer */
    uint32_t dirty;                      /* bit j: resonator j waits for its new frequency */
    int32_t r2;                          /* the damping a sample on one state, Q30: the pole radius squared */
    int32_t dm;                          /* 1 - the pole radius, Q30 */
    int32_t g;                           /* the peak gain's inverse at this DECAY, Q20 */
    uint8_t decay, shift;                /* the knobs the damping and the tuning were worked out for (255: none) */
    uint8_t nheld, seq;                  /* notes held; the last stamp given (1 .. 255) */
} rz_t;

void rz_init(rz_t *z);
/* the part's sounding notes, up to RZ_NOTES, MIDI note x 16 with the fine tuning in (clamped to 0 .. 2032);
 * once a block, before rz_block; n 0 keeps the last chord ringing and fed */
void rz_notes(rz_t *z, const int32_t *pitch16, uint32_t n);
/* in at int16 scale (the injection has headroom to 2x); out at int16 scale, the wet within 8x of it; out is not
 * in (the wet is summed there first) */
void rz_block(rz_t *z, const rz_param_t *p, const int32_t *in, int32_t *out, uint32_t n);

#endif
