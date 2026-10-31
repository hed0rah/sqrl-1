/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * rotary: a rotating-speaker cabinet as a part's insert, integer, for the FM-1's audio interrupt.
 *
 * A crossover (one pole, ~800 Hz) splits the sound between the horn (the highs) and the drum (the lows). Each
 * rotor turns at its own speed: SLOW (chorale: horn 0.83 Hz, drum 0.67 Hz), FAST (tremolo: 6.8 and 5.8 Hz) or
 * STOP (the brake). They change speed with their own inertia, the light horn in about a second, the heavy drum
 * in about five, so a SLOW-FAST switch swirls up and the two drift apart on the way. Each rotor moves its sound
 * toward and away from the listener (a delay swept by the rotation: the Doppler shift, ~+-2.4 % for the horn at
 * FAST) and swings its loudness (the horn's mouth faces the listener once a turn). DRIVE is the cabinet's
 * preamp, a soft clip before the crossover. The output is mono, the listener at the horn's front, with a side
 * signal (the same rotors heard from 90 degrees round) for a stereo path.
 *
 * The caller owns one short line (int16, a power of 2, at least 128) and passes the rotors' state. */
#ifndef ROTARY_H
#define ROTARY_H
#include <stdint.h>

enum { ROT_STOP, ROT_SLOW, ROT_FAST };

typedef struct {
    int16_t *buf;
    uint32_t mask, w;
    uint32_t hph, dph;                   /* the horn's angle, the drum's (2^32 a turn) */
    int32_t hf, df;                      /* their speeds, phase increments a sample, Q8 (inertia's state) */
    int32_t lp[4];                       /* the crossovers: horn and drum paths, heard at 0 and 90 degrees */
} rot_t;

void rot_init(rot_t *r, int16_t *buf, uint32_t len);
/* in at int16 scale; out (mono) and side (may be 0) at int16 scale. speed ROT_*; drive 0 .. 127; bal 0 (drum)
 * .. 127 (horn), 64 even */
void rot_block(rot_t *r, uint8_t speed, uint8_t drive, uint8_t bal, const int32_t *in, int32_t *out, int32_t *side,
               uint32_t n);

#endif
