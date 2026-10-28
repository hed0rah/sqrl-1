/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * TALK: a TMS5220 (Speak & Spell family) voice for SQRL-1. Keys play the pitch,
 * each note speaks a word from a bank; the frames can be sped up, the chip's clock bent, bits
 * flipped as they are read. The generator is lpc5220.c, bit-exact at its defaults to a reference
 * model (not published).
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs lpc5220.c/.h
 * and banks/bank_*.h next to it. State: one talk_part_t per part in ENG_MEM (engine_t.mem).
 *
 *   WORD    the word of the bank a note speaks (its names come from the bank: desc())
 *   BANK    DIGITS WORDS SYLLS
 *   RATE    frames per second, 0.25x .. 4x (64 = the chip's 40), no pitch change
 *   BEND    the chip's clock, -24 .. +24 semitones: formants (and with INTON the pitch) move
 *   GLTCH   bit errors as the frames are read, 0 .. about 2 % per bit
 *   SMTH    the parameter interpolation: CHIP (its shift table), STEP, SLUR
 *   INTON   OFF: the key's pitch, monotone; ON: the word's own pitch (the key picks the word only)
 *   PLAY    ONCE, LOOP (again while held), NEXT (the next word each note), RAND */
#include "lpc5220.c"
/* the word banks are prototypes, rendered from a desktop text-to-speech voice and not cleared for distribution:
 * private builds only. A release build (tools/build.py --release) sets FELUCCA_PRIVATE_BANKS=0 and stops here
 * until the banks are re-recorded (with a bank encoder, not yet published) and this guard goes */
#ifndef FELUCCA_PRIVATE_BANKS
#define FELUCCA_PRIVATE_BANKS 1
#endif
#if !FELUCCA_PRIVATE_BANKS
#error "TALK's word banks are prototypes, not cleared for distribution: re-record them before a release build"
#endif
#include "banks/bank_digits.h"
#include "banks/bank_words.h"
#include "banks/bank_syllables.h"

#define TALK_POLY 4u
#define TALK_NBANK 3u
enum { TALK_ONCE, TALK_LOOP, TALK_NEXT, TALK_RAND };

typedef struct {
    const uint8_t *data;
    const uint16_t *ofs;
    const char *const *names;
    uint8_t n;
} talk_bank_t;

static const talk_bank_t TALK_BANKS[TALK_NBANK] = {
    {TALK_DIGITS_DATA, TALK_DIGITS_OFS, TALK_DIGITS_NAMES, (uint8_t)NELEM(TALK_DIGITS_NAMES)},
    {TALK_WORDS_DATA, TALK_WORDS_OFS, TALK_WORDS_NAMES, (uint8_t)NELEM(TALK_WORDS_NAMES)},
    {TALK_SYLLABLES_DATA, TALK_SYLLABLES_OFS, TALK_SYLLABLES_NAMES, (uint8_t)NELEM(TALK_SYLLABLES_NAMES)},
};

typedef struct {
    lpc5220_out_t v[TALK_POLY];
    uint8_t word[TALK_POLY];     /* the word each voice speaks (LOOP restarts it) */
    uint8_t next;                /* NEXT: the word the next note speaks */
    uint8_t spoken, last_bank, last_word;     /* talk_now(): the latest note's word */
    int32_t dcx[TALK_POLY], dcy[TALK_POLY];   /* the DC blocker after the chip (the lattice has an offset) */
    int32_t rnd;                 /* RAND */
} talk_part_t;
ENG_MEM_FITS("TALK", sizeof(talk_part_t));

static talk_part_t *talk_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (talk_part_t *)ENG_MEM(t - trk) : 0;
}

static const talk_bank_t *talk_bank(const track_t *t)
{
    return &TALK_BANKS[(uint32_t)clamp(t->p[P_E1], 0, TALK_NBANK - 1)];
}

static void talk_start(lpc5220_t *c, const talk_bank_t *b, uint32_t w)
{
    w = w < b->n ? w : 0u;
    lpc5220_start(c, b->data + b->ofs[w], (uint32_t)(b->ofs[w + 1] - b->ofs[w]));
}

static void talk_note_on(track_t *t, voice_t *v)
{
    talk_part_t *tp = talk_of(t);
    const talk_bank_t *b = talk_bank(t);
    uint32_t i = (uint32_t)(v - t->v), w = (uint32_t)clamp(t->p[P_E0], 0, b->n - 1);
    if (!tp || i >= TALK_POLY)
        return;
    if (t->p[P_E7] == TALK_NEXT) {
        w = tp->next % b->n;
        tp->next = (uint8_t)((w + 1u) % b->n);
    } else if (t->p[P_E7] == TALK_RAND) {
        if (!tp->rnd)
            tp->rnd = 0x2545F491;
        w = (noise32(&tp->rnd) >> 16) % b->n;
    }
    tp->word[i] = (uint8_t)w;
    tp->spoken = 1;
    tp->last_bank = (uint8_t)(b - TALK_BANKS);
    tp->last_word = (uint8_t)w;
    if (!v->env && !v->env_out) {                        /* a fresh voice: from rest, ZOH held at 0 */
        lpc5220_out_init(&tp->v[i], 44100u);
        tp->dcx[i] = tp->dcy[i] = 0;
    }
    talk_start(&tp->v[i].chip, b, w);
}

/* for the screen: the word the most recently started voice on part t speaks, its bank (TALK_BANKS:
 * its names in .names, the bank's own name in TALK_BANK_NAMES). 0 = none since the engine was claimed */
static int talk_now(const track_t *t, uint32_t *bank, uint32_t *word)
{
    const talk_part_t *tp = talk_of(t);
    if (!tp || !tp->spoken)
        return 0;
    *bank = tp->last_bank;
    *word = tp->last_word;
    return 1;
}

/* 2^(st16 / 192) as Q16 for st16 in 1/16 semitone, -24 .. +24 semitones */
static uint32_t talk_ratio(int32_t st16)
{
    static const uint32_t SEMI[13] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032,
                                      110218, 116772, 123715, 131072};
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -24 * 16, 24 * 16) + 24 * 16;    /* 0 .. 4 octaves */
    o = st16 / 192;
    k = st16 - o * 192;
    r = SEMI[k >> 4] + (((SEMI[(k >> 4) + 1] - SEMI[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 2 ? r << (o - 2) : r >> (2 - o);
}

static __attribute__((noinline)) void talk_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    talk_part_t *tp = talk_of(t);
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), j, clk, rate, vi;
    lpc5220_out_t *o;
    int32_t buf[64];
    if (!tp || i >= TALK_POLY)
        return;
    o = &tp->v[i];
    /* BEND (+ SHP modulation, +-12 semitones over its range) moves the chip's clock */
    clk = talk_ratio(p[P_E3] * 16 + ((m->shape - (64 << 8)) * 3 >> 7));
    lpc5220_out_clock(o, 44100u, clk);
    /* RATE: 64 = 1x, 0 = 0.25x, 127 = 4x (two octaves each way) */
    rate = talk_ratio((p[P_E2] - 64) * 6) >> 8;
    o->chip.rate_q8 = rate < 1u ? 1u : rate;
    o->chip.interp = (uint8_t)clamp(p[P_E5], 0, LPC_INTERP_N - 1);
    o->chip.glitch_q16 = (uint16_t)((p[P_E4] * p[P_E4]) / 12);   /* 0 .. 1344 / 65536 = 2 % */
    if (p[P_E6])
        o->chip.period_q8 = 0;                           /* INTON: the word's own pitch */
    else {
        uint64_t fq16 = ((uint64_t)m->inc * 44100u) >> 16;   /* the key's frequency, Hz Q16 */
        uint64_t per = fq16 ? ((uint64_t)8000u * clk * 256u) / fq16 : 0;
        o->chip.period_q8 = per < 256u ? 256u : per > (1u << 24) ? 1u << 24 : (uint32_t)per;
    }
    if (p[P_E7] == TALK_LOOP && lpc5220_ended(&o->chip))
        talk_start(&o->chip, talk_bank(t), tp->word[i]);
    vi = i;                                              /* the voice; i now walks the block */
    for (i = 0; i < n; i += 64u) {                       /* i: the amplitude ramp's sample */
        uint32_t k = n - i < 64u ? n - i : 64u;
        for (j = 0; j < k; j++)
            buf[j] = 0;
        lpc5220_out_render(o, buf, k);
        for (j = 0; j < k; j++) {                        /* DC blocker: one-pole high-pass, R = 1 - 2^-9 (13.7 Hz), shifts only */
            int32_t y = buf[j] - tp->dcx[vi] + tp->dcy[vi] - (tp->dcy[vi] >> 9);
            tp->dcx[vi] = buf[j];
            tp->dcy[vi] = y;
            out[i + j] += voice_amp(y, m, i + j);
        }
    }
}

static const param_desc_t *talk_desc(const track_t *t, uint32_t k)
{
    static param_desc_t d[TALK_NBANK];
    const talk_bank_t *b = talk_bank(t);
    uint32_t i = (uint32_t)(b - TALK_BANKS);
    if (k != 0)
        return 0;
    d[i].label = "WORD";
    d[i].fmt = F_ENUM;
    d[i].min = 0;
    d[i].max = (int16_t)(b->n - 1);
    d[i].def = 0;
    d[i].names = b->names;
    d[i].unit = 0;
    return &d[i];
}

static const char *const TALK_BANK_NAMES[TALK_NBANK] = {"DIGITS", "WORDS", "SYLLS"};
static const char *const TALK_SMTH_NAMES[LPC_INTERP_N] = {"CHIP", "STEP", "SLUR"};
static const char *const TALK_PLAY_NAMES[4] = {"ONCE", "LOOP", "NEXT", "RAND"};

static const preset_t TALK_PRESETS[] = {
    /* WORD BANK RATE BEND | GLTCH SMTH INTON PLAY */
    {"SPELL", {0, 1, 64, 0, 0, 0, 1, 0}, {0, 127, 127, 20}, 0, 1, FX(0, 0, 10, 15), PAT(0)},
    {"COUNTER", {0, 0, 64, 0, 0, 0, 0, 2}, {0, 127, 127, 20}, 0, 1, FX(0, 0, 20, 10), PAT(0)},
    {"ROBOT CHOIR", {4, 1, 50, -5, 0, 2, 0, 1}, {20, 127, 127, 60}, 0, 0, FX(0, 70, 25, 80), PAT(0)},
    {"BENT", {3, 1, 80, 9, 40, 1, 1, 3}, {0, 127, 127, 20}, 0, 1, FX(20, 0, 30, 20), PAT(0)},
    {"BABBLE", {0, 2, 72, 0, 0, 0, 0, 3}, {0, 127, 127, 30}, 0, 1, FX(0, 20, 35, 30), PAT(0)},
};

static const engine_t ENG_TALK = {
    .name = "TALK",
    .page_title = {"WORD", "CHIP"},
    .edit = {
        {"WORD", F_INT, 0, 10, 0, 0, 0},
        {"BANK", F_ENUM, 0, TALK_NBANK - 1, 1, TALK_BANK_NAMES, 0},
        {"RATE", F_INT, 0, 127, 64, 0, 0},
        {"BEND", F_SEMI, -24, 24, 0, 0, 0},
        {"GLTCH", F_PCT, 0, 127, 0, 0, 0},
        {"SMTH", F_ENUM, 0, LPC_INTERP_N - 1, 0, TALK_SMTH_NAMES, 0},
        {"INTON", F_ONOFF, 0, 1, 0, 0, 0},
        {"PLAY", F_ENUM, 0, 3, 0, TALK_PLAY_NAMES, 0},
    },
    .presets = TALK_PRESETS,
    .npresets = NELEM(TALK_PRESETS),
    .note_on = talk_note_on,
    .render = talk_render,
    .desc = talk_desc,
    .knob = {P_E0, P_E2, P_E3, P_E4},
    .poly = TALK_POLY,
    .mem = sizeof(talk_part_t),
};
