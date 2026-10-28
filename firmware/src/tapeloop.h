/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * tapeloop: a loop of tape you record onto and play from the keys, integer, for the FM-1's audio interrupt. After
 * the tape loops of the 1960s and Expert Sleepers' Augustus Loop; our own code.
 *
 * The tape is a block of memory the caller owns (on SQRL-1 the tape echo's: one tape, two machines). Its speed
 * trades length for fidelity, as a real machine's does:
 *   HI    16-bit cells at the full rate (on a 128 KiB tape at 44.1 kHz: 1.486 s)
 *   MID   16-bit at half the rate (2.972 s)
 *   LO    8-bit mu-law at half the rate (5.944 s)
 *   CASS  8-bit mu-law at a quarter of the rate (11.89 s, dark)
 * (a tape's length in seconds is cells / rate: the caller shows it from the bytes it gives, never a round figure)
 * The slower speeds filter what goes on the tape and what comes off it at their own bandwidth.
 *
 * The record and play heads sit together; the play head reads each cell just before the record head reaches it,
 * so what plays is what went on one turn of the loop ago. REC lays new sound on top of the old, FEEDBACK is how
 * much of the old survives (the erase head's pressure), REC off holds the loop as it is (FREEZE). The record head
 * fades in and out over ~10 ms, so the loop's join is a crossfade, not a click. SPLICE cuts the
 * loop shorter; a new length takes effect when the loop next comes round, so it never clicks.
 *
 * SPEED is the tape's speed in semitones: everything on it changes pitch together, and time with it. STOP and
 * REVERSE are speeds too, so they go through INERTIA (the motor and reels' weight): a stop dives, a start spins up.
 * The sound fades as the tape slows to a standstill, as a playback head's does, so a stopped tape is silent.
 * WOW moves the play head on a slow sine and a little noise.
 *
 * The keys play the tape through voices of their own (tl_voice_*), at the key's pitch:
 *   TAPE   a head that runs the loop at the key's speed: pitch and time move together
 *   GRAIN  two overlapping grains under triangles at the key's pitch, their starts moving through the loop at SCAN's
 *          speed (64: the tape's own; 0: held at POSITION, a frozen spot), scattered by SPRAY */
#ifndef TAPELOOP_H
#define TAPELOOP_H
#include <stdint.h>

enum { TPL_HI, TPL_MID, TPL_LO, TPL_CASS, TPL_NQ };
enum { TPL_PLAY, TPL_STOP, TPL_REV };
enum { TPL_TAPE, TPL_GRAIN };

typedef struct {
    uint8_t quality;                     /* TPL_HI .. TPL_CASS */
    int8_t speed;                        /* semitones, -24 .. 24 */
    uint8_t motor;                       /* TPL_PLAY, TPL_STOP, TPL_REV */
    uint8_t inertia;                     /* 0 (instant) .. 127 (~2 s) */
    uint8_t rec;                         /* 1: the record head is on */
    uint8_t feedback;                    /* 0 .. 127: how much of the old loop survives under the record head */
    uint8_t splice;                      /* 1 .. 127: the loop's length, 127 the whole tape */
    uint8_t wow;                         /* 0 .. 127 */
} tl_param_t;

typedef struct {
    void *mem;
    uint32_t bytes;
    uint8_t q, fade;                     /* the speed the tape holds; 1 while fading out to change it */
    uint32_t cap, len;                   /* cells at that speed; the loop's length in cells */
    int32_t pos;                         /* the heads' place on the loop, Q12 cells */
    int32_t v;                           /* the tape's speed, Q12 cells a sample, signed */
    int32_t wprev;                       /* the record head's last input, for the cells it crosses */
    int32_t lpi[2], lpo[2];              /* the tape's bandwidth, in and out */
    int32_t dcx, dcy;                    /* the output's DC blocker */
    uint32_t wph, rng;                   /* the wobble's phase, its noise */
    int32_t nz, mod;                     /* the noise low-passed; the wobble at the last block's end, Q12 */
    int32_t recg;                        /* the record head's level, Q15: it fades in and out at a punch */
} tl_t;

typedef struct {
    uint8_t mode;                        /* TPL_TAPE, TPL_GRAIN */
    uint8_t position;                    /* 0 .. 127: where on the loop a key starts */
    uint8_t grain;                       /* 0 .. 127: a grain's length, ~10 .. 300 ms */
    uint8_t spray;                       /* 0 .. 127: how far grain starts scatter */
    uint8_t scan;                        /* 0 .. 127: GRAIN's starts move through the loop, 64 at the tape's speed */
} tl_play_t;

typedef struct {
    int32_t head;                        /* TAPE: the voice's head; GRAIN: where the grains start, Q12 cells */
    int32_t g[2];                        /* GRAIN: each grain's read place, Q12 cells */
    uint32_t gk[2];                      /* each grain's place in its window, samples */
    uint32_t glen;                       /* the grain length the window started with */
    uint32_t rng;
} tl_voice_t;

/* mem: bytes of tape (even); cleared */
void tl_init(tl_t *t, void *mem, uint32_t bytes);
/* in: what goes to the record head, int16 scale; out: what the play head plays, int16 scale */
void tl_block(tl_t *t, const tl_param_t *p, const int32_t *in, int32_t *out, uint32_t n);
void tl_voice_on(const tl_t *t, tl_voice_t *v, const tl_play_t *pp, uint32_t seed);
/* add a key's voice to out; ratio: its pitch against the tape's, Q16 (65536: the tape's own) */
void tl_voice_render(const tl_t *t, tl_voice_t *v, const tl_play_t *pp, uint32_t ratio, int32_t *out, uint32_t n);

#endif
