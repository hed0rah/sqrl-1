/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * organ: a tonewheel organ's drawbars, percussion and key click, integer, for the FM-1's audio interrupt.
 *
 * The nine drawbars are harmonics of the 16' pitch: 16' 1, 5 1/3' 3, 8' 2 (the note), 4' 4, 2 2/3' 6, 2' 8,
 * 1 3/5' 10, 1 1/3' 12, 1' 16. Each is 0 .. 8, 3 dB a step as on the console. They are summed into a wavetable
 * of one 16' cycle, rebuilt (over a dozen blocks, never in one) only when a drawbar moves, so a voice costs one
 * table read a sample whatever the
 * registration. Three tables fold the top drawbars back an octave, as the tonewheel generator does above its
 * last wheel: a note reads the brightest one whose top harmonic stays under 18 kHz, so nothing aliases.
 * The tables are scaled by the registration's own peak, floored at two bars' worth: a full organ is louder
 * than a lone flute (twice), but not nine times. Memory that starts zeroed holds the empty registration's
 * (silent) tables, so a caller needs no init.
 *
 * PERC adds the 2nd (4') or 3rd (2 2/3') harmonic under a fast (~0.2 s) or slow (~1 s) decay. As on the
 * console it is single trigger: only a note struck with no other key held gets it (the caller says which).
 * Each note starts with a ~3 ms click, the key contacts closing. */
#ifndef ORGAN_H
#define ORGAN_H
#include <stdint.h>
#include "organ_table.h"

#define ORG_NBARS 9
#define ORG_NLEV 3
enum { ORG_POFF, ORG_P2, ORG_P3, ORG_P2S, ORG_P3S, ORG_NPERC };

typedef struct {
    int16_t t[2][ORG_NLEV][ORG_TLEN];    /* two sets: voices read the front while a rebuild fills the back; in
                                          * each, one 16' cycle, [1] and [2] folded back */
    uint8_t front;                       /* the set the voices read */
    uint8_t bars[ORG_NBARS];             /* the front's registration */
    uint8_t want[ORG_NBARS];             /* the registration being built */
    uint8_t busy;                        /* 1 while a rebuild is under way */
    uint16_t at;                         /* its next cell: sums first, then the scale */
    int32_t peak;                        /* the highest sum so far */
} org_tables_t;

typedef struct {
    uint32_t ph;                         /* the 16' phase */
    int32_t perc;                        /* the percussion's level, Q15, decaying */
    int32_t clk;                         /* the click's level, Q15, decaying */
    uint32_t rng;
    int32_t nlp;                         /* the click's noise, high-passed */
} org_voice_t;

/* once a block: when bars differ from what sounds, build them into the back set a slice a call (~12 calls) and
 * swap; 1 on the call that swapped. The voices keep the old registration until then, a few ms, as a drawbar's
 * move reaches the tonewheels' busbars */
int org_tables_set(org_tables_t *o, const uint8_t bars[ORG_NBARS]);
/* a note: perc 1 when it gets the percussion (struck with no other key held) */
void org_on(org_voice_t *v, int perc, int retrig);
/* add n samples at int16 scale to out. inc: the note's (8') phase increment, 2^32 a cycle a sample, at 44.1 kHz */
void org_render(const org_tables_t *o, org_voice_t *v, uint32_t inc, uint8_t perc, int32_t *out, uint32_t n);

#endif
