/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* LOOP: a tape loop you record onto and play from the keys (tapeloop.c, from databurn, MIT). The tape is the tape
 * echo's own (fx.c dly_buf): while LOOP is the SOUND in front it holds the tape and the echo is off; the delay send
 * bus is what the record head hears, so LOOP records the parts sending to the delay (a pair running behind, the
 * sequencer, MIDI in). Its keys play the tape through heads of their own at the key's pitch (C4 the tape's own):
 * TAPE runs the loop at that speed, GRAIN scatters short grains from it.
 *
 *   SPEED  the tape's speed in semitones (-24 .. 24): everything on it changes pitch, and time with it
 *   REC    the record head on: new sound on top of the loop, FDBK of the old surviving under it
 *   FDBK   how much of the old loop survives a pass under the record head
 *   MOTOR  PLAY, STOP (the tape winds down, silent), REV (it runs backwards)
 *   QUAL   HI MID LO CASS: the tape's speed against its length, fidelity for time; a change clears the tape
 *   INERT  the motor and reels' weight: how long a stop dives or a start spins up
 *   SPLCE  the loop's length, the whole tape at the top
 *   WOW    the tape's wobble
 * The keys' side (MODE POSITION GRAIN SPRAY SCAN) is the shell's (shell.c, LOOP's KEYS page), copied to loop_kp. */

#include "tapeloop.c"

#define LOOP_POLY 4u

/* the tape itself and who holds it: written here and by the shell, read by fx.c's buses (included after this file) */
static tl_t loop_tl;                    /* the loop on dly_buf, set up when LOOP takes the tape */
static tl_param_t loop_tp;              /* its knobs, from the part playing LOOP (loop_block, once a block) */
static tl_play_t loop_kp = {TPL_TAPE, 0, 40, 0, 64};   /* the keys' side: MODE POSITION GRAIN SPRAY SCAN (shell.c) */
static volatile uint8_t loop_hold;      /* 1: LOOP is the SOUND in front and holds the tape (shell.c) */

typedef struct {
    tl_voice_t v[LOOP_POLY];
} loop_part_t;
ENG_MEM_FITS("LOOP", sizeof(loop_part_t));

static loop_part_t *loop_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (loop_part_t *)ENG_MEM(t - trk) : 0;
}

/* the tape's parameters from the part's knobs, each block (fx.c reads loop_tp in its buses) */
static void loop_block(track_t *t)
{
    const int16_t *p = t->p;
    loop_tp.speed = (int8_t)clamp(p[P_E0], -24, 24);
    loop_tp.rec = (uint8_t)(p[P_E1] != 0);
    loop_tp.feedback = (uint8_t)clamp(p[P_E2], 0, 127);
    loop_tp.motor = (uint8_t)clamp(p[P_E3], TPL_PLAY, TPL_REV);
    loop_tp.quality = (uint8_t)clamp(p[P_E4], TPL_HI, TPL_CASS);
    loop_tp.inertia = (uint8_t)clamp(p[P_E5], 0, 127);
    loop_tp.splice = (uint8_t)clamp(p[P_E6], 1, 127);
    loop_tp.wow = (uint8_t)clamp(p[P_E7], 0, 127);
}

static void loop_note_on(track_t *t, voice_t *v)
{
    loop_part_t *lp = loop_of(t);
    uint32_t i = (uint32_t)(v - t->v);
    if (!lp || i >= LOOP_POLY)
        return;
    tl_voice_on(&loop_tl, &lp->v[i], &loop_kp, 0x9E3779B9u ^ (i * 0x85EBCA6Bu) ^ (uint32_t)(t - trk) << 24);
}

static __attribute__((noinline)) void loop_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    loop_part_t *lp = loop_of(t);
    uint32_t i = (uint32_t)(v - t->v), j, ratio;
    int32_t buf[64];
    if (!lp || i >= LOOP_POLY || !loop_tl.mem)
        return;
    ratio = st16_ratio(v->pitch_cur - 60 * 16);       /* (the key against C4, which plays the tape as it is) */
    for (i = 0; i < n; i += 64u) {
        uint32_t k = n - i < 64u ? n - i : 64u;
        for (j = 0; j < k; j++)
            buf[j] = 0;
        tl_voice_render(&loop_tl, &lp->v[(uint32_t)(v - t->v)], &loop_kp, ratio, buf, k);
        for (j = 0; j < k; j++)
            out[i + j] += voice_amp(buf[j], m, i + j) << 1;   /* (the tape holds the send at half scale, as the echo) */
    }
}

static const char *const N_LOOP_REC[] = {"OFF", "ON"};
static const char *const N_LOOP_MOTOR[] = {"PLAY", "STOP", "REV"};
static const char *const N_LOOP_QUAL[] = {"HI", "MID", "LO", "CASS"};

static const preset_t LOOP_PRESETS[] = {
    /* SPEED REC FDBK MOTOR | QUAL INERT SPLCE WOW; no delay send: the loop would record itself */
    {"TAPE LOOP", {0, 0, 100, 0, 0, 40, 127, 10}, {0, 40, 127, 60}, 0, 0, FX(0, 0, 0, 20), PAT(0)},
    {"OVERDUB", {0, 1, 110, 0, 1, 40, 127, 10}, {0, 40, 127, 60}, 0, 0, FX(0, 0, 0, 20), PAT(0)},
    {"HALF SPEED", {-12, 0, 100, 0, 1, 60, 127, 20}, {0, 40, 127, 80}, 0, 0, FX(0, 0, 0, 30), PAT(0)},
    {"CASSETTE", {0, 0, 100, 0, 3, 50, 127, 60}, {0, 40, 127, 60}, 0, 0, FX(0, 0, 0, 20), PAT(0)},
    {"SHORT SPLICE", {0, 0, 110, 0, 0, 20, 24, 0}, {0, 20, 127, 40}, 0, 0, FX(0, 0, 0, 10), PAT(0)},
};

static const engine_t ENG_LOOP = {
    .name = "LOOP",
    .page_title = {"TAPE", "DECK"},
    .edit = {
        {"SPEED", F_SEMI, -24, 24, 0, 0, 0},
        {"REC", F_ENUM, 0, 1, 0, N_LOOP_REC, 0},
        {"FDBK", F_PCT, 0, 127, 100, 0, 0},
        {"MOTOR", F_ENUM, 0, 2, 0, N_LOOP_MOTOR, 0},
        {"QUAL", F_ENUM, 0, 3, 0, N_LOOP_QUAL, 0},
        {"INERT", F_PCT, 0, 127, 40, 0, 0},
        {"SPLCE", F_PCT, 1, 127, 127, 0, 0},
        {"WOW", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = LOOP_PRESETS,
    .npresets = NELEM(LOOP_PRESETS),
    .note_on = loop_note_on,
    .render = loop_render,
    .block = loop_block,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = LOOP_POLY,
    .mem = sizeof(loop_part_t),
};
