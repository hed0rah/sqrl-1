/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * vowel: puts a part's sound through a vowel, as a talk box or a formant filter does. An insert, integer, for the
 * FM-1's audio interrupt; it pairs with MOUTH (mouth.c) and uses its vowels.
 *
 * Three resonant band-passes sit on a vowel's first three formants, and their sum is the wet sound. VOWEL walks
 * the eight plain vowels of MOUTH's table in its order, IY IH EH AE AA AO UH UW: from closed-front, round the open
 * AA, to closed-back (the plaster casts' I E A O U sit at 0, 36, 73, 91 and 127). Between two vowels the formants
 * move in log frequency and the bandwidths in proportion. The frequencies are Peterson and Barney's (1952) men's
 * averages and the bandwidths Klatt's (1980), as mouth.c has them.
 * TRACK lets the part's own loudness open the mouth: an envelope follower (~6 ms up, ~46 ms down) pulls the vowel
 * toward AA, all the way at TRACK 127 once the part's mean level reaches about -12 dBFS, so a note that swells
 * says "ee-ah" from IY or "oo-ah" from UW (a wah that talks). At AA it has nothing to open; 0 is off.
 * SIZE scales every formant by 0.7x (0) .. 1x (64) .. 1.4x (127), as MOUTH's THROAT and MOUTH knobs scale F1 and
 * F2: a longer or shorter vocal tract, a giant's to a child's, with the bandwidths scaled alike (Q kept).
 * MIX crossfades the dry sound (0, bit-exact) to the wet (127).
 *
 * Each formant is a Chamberlin state-variable filter with its input scaled by its damping, so its peak gain is 1
 * whatever its bandwidth. F2 enters with its sign flipped: between two formants their band-passes are half a turn
 * apart in phase, so a plain sum would notch the valley, and the flip fills it as a cascade would. Each vowel has
 * its own gain, which keeps the wet level within ~1 dB on pink noise along the whole path. That level is ~15 dB
 * under the dry: the headroom a full-scale saw needs when a harmonic lands on a formant (its peak then stays over a
 * dB under full scale). The coefficients are worked out once a block from a position that glides (~6 ms), so a
 * jump of VOWEL or SIZE sweeps instead of clicking. The states keep 12 bits under the output's last one, so quiet
 * sounds keep their formants and what they hold after the input stops rounds to exact silence.
 *
 * Cost on a PC (tests/test_vowel.c): ~5.7 ns a sample, the coefficients included; 40 bytes of state. */
#ifndef VOWEL_H
#define VOWEL_H
#include <stdint.h>

typedef struct {
    uint8_t vowel;                       /* 0 .. 127: IY .. UW */
    uint8_t track;                       /* 0 .. 127: how far loudness opens the vowel toward AA */
    uint8_t size;                        /* 0 .. 127: formants 0.7x .. 1.4x, 64 = 1x */
    uint8_t mix;                         /* 0 .. 127: dry .. wet */
} vw_param_t;

typedef struct {
    int32_t lp[3], bp[3];                /* the three filters' states, Q12 on int16 scale */
    int32_t env;                         /* the input's mean level, int16 scale */
    int32_t pos;                         /* the path position as it glides, Q12 vowels (-1: not yet) */
    int32_t size;                        /* the size as it glides, log2 Q16 */
    int32_t mix;                         /* MIX at the end of the last block, 0 .. 128 */
} vw_t;

void vw_init(vw_t *v);
/* in and out at int16 scale (they may be the same array) */
void vw_block(vw_t *v, const vw_param_t *p, const int32_t *in, int32_t *out, uint32_t n);

#endif
