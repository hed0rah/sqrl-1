/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * ensemble: the chorus bus in three characters, on one short line the caller owns.
 *   CHOR  one tap swept by a sine (9 .. 26 ms), the plain chorus, mono
 *   ENS   three taps around 7 ms, each swept by a slow sine (RATE) plus a faster vibrato, a third of a cycle apart:
 *         the string machine's ensemble that makes held chords shimmer; tap 1 left, tap 2 right, tap 3 centre
 *   DIM   two taps around 10 ms swept in opposition, shallow: wide and still, after the rack doublers
 * The output is mid (both channels) and side (added to the left, taken from the right), so a mono bus can carry the
 * stereo of ENS and DIM. A change of MODE fades out and back in over two blocks. Integer only. */
#ifndef ENSEMBLE_H
#define ENSEMBLE_H
#include <stdint.h>

enum { EN_CHOR, EN_ENS, EN_DIM, EN_NMODES };

typedef struct {
    int16_t *buf;
    uint32_t mask, w;
    uint32_t ph, vph;                    /* the slow sweep's phase, the vibrato's */
    uint8_t mode, fade;
} en_t;

/* buf: len int16s, len a power of 2, at least 2048 (46 ms) */
void en_init(en_t *e, int16_t *buf, uint32_t len);
/* in at int16 scale; mid and side out at int16 scale. inc: the sweep's phase step a sample (2^32 a cycle);
 * depth 0 .. 127 */
void en_block(en_t *e, uint8_t mode, uint32_t inc, uint8_t depth, const int32_t *in, int32_t *mid, int32_t *side,
              uint32_t n);

#endif
