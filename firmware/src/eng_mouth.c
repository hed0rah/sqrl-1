/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * MOUTH: a phoneme voice that sings, for SQRL-1. Each note sings the next piece of a
 * phrase (mouth.c: 40 phonemes gliding through syllable.c's formant synthesizer); its last vowel holds
 * while the key does, and the rest of the piece plays when the key is let go.
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs mouth.c/.h and
 * syllable.c/.h next to it. syllable.c comes in once: here, unless SYLLABLE's adapter (eng_syllable.c)
 * brought it in first, so engines.c must include eng_syllable.c before this file, not after.
 * State: one mth_part_t per part in ENG_MEM (engine_t.mem).
 *
 *   SPEED   the phonemes' durations, 64 = Klatt's; 0 = 4x as long, 127 = about a quarter (each phoneme
 *           keeps the time its glides need)
 *   THROAT  F1 x 0.7 .. 1.4 (64 = 1x)
 *   MOUTH   F2 x 0.7 .. 1.4 (+ SHP modulation)
 *   GRIT    0 clean; up: sample and hold down to 8 kHz, 16 -> 6 bits, the 1982 grit
 *   PHRASE  HELLO COUNT LALALA OOH AH DATABN ROBOT SING DO RE MOON FIVE8
 *   PLAY    SYLL (each note the next syllable), WORD (the next word), PHRASE (all of it)
 *   BRTH    breath under the whole phrase
 *   FALL    the pitch falls over the first 150 ms, 0 .. 12 semitones, as SYLLABLE's
 * What is left of a piece plays under the key's release: give the sound a long release (presets 90
 * and up) or the end of the word fades with it. */
#ifndef SYLLABLE_H
#include "syllable.c"
#endif
#include "mouth.c"

#define MOUTH_POLY 4u
#define MOUTH_GAIN 369411u            /* Q16: 2 x 10^(9/20), the output's scale */

typedef struct {
    mth_voice_t v[MOUTH_POLY];
    uint32_t age[MOUTH_POLY];        /* samples since the note-on (FALL) */
    uint8_t cursor;                  /* the next note's piece starts here in the phrase */
    uint8_t phrase;                  /* the phrase the cursor walks; another resets it */
} mth_part_t;
ENG_MEM_FITS("MOUTH", sizeof(mth_part_t));

static mth_part_t *mouth_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (mth_part_t *)ENG_MEM(t - trk) : 0;
}

/* the player's phrases U1..U8, after the built-ins in PHRASE (SPELL writes them): phonemes with MTH_EOW at word ends
 * and MTH_EOS at syllable ends, and each phoneme's length (its vowel SHORT NORMAL LONG). n 0: never written, sings AA */
#define MOUTH_NUSER 8u
#define MOUTH_UMAX 24u
#define MOUTH_NPHRASE (MTH_NPHRASE + MOUTH_NUSER)
typedef struct {
    uint8_t ph[MOUTH_UMAX], len[MOUTH_UMAX], n;
} mouth_user_t;
static mouth_user_t mouth_user[MOUTH_NUSER];
static const char *const MOUTH_PHRASE_NAMES[MOUTH_NPHRASE + 1] = {
    "HELLO", "COUNT", "LALALA", "OOH AH", "DATABN", "ROBOT", "SING", "DO RE", "MOON", "FIVE8",
    "U1", "U2", "U3", "U4", "U5", "U6", "U7", "U8", 0};
typedef char mouth_names_fit[MTH_NPHRASE == 10u ? 1 : -1];   /* (the built-ins' names copied above) */
static const uint8_t MOUTH_AA[1] = {MTH_AA | MTH_EOW | MTH_EOS}, MOUTH_AA_LEN[1] = {1};
/* phrase i: its phonemes, their lengths (0 for the built-ins: every vowel as the table has it), how many */
static const uint8_t *mouth_phrase(uint32_t i, const uint8_t **len, uint32_t *n)
{
    const mouth_user_t *u;
    if (i < MTH_NPHRASE) {
        *len = 0;
        return mth_phrase(i, n);
    }
    u = &mouth_user[(i - MTH_NPHRASE) % MOUTH_NUSER];
    if (!u->n) {
        *len = MOUTH_AA_LEN;
        *n = 1;
        return MOUTH_AA;
    }
    *len = u->len;
    *n = u->n;
    return u->ph;
}

/* SPELL (shell.c): while its page is in front, a key on part mouth_spell_part - 1 sings these phonemes, the
 * syllable under the cursor, instead of the phrase's next piece. Written with the interrupts off */
static const uint8_t *mouth_spell_ph, *mouth_spell_len;
static volatile uint8_t mouth_spell_n, mouth_spell_part;


static void mouth_note_on(track_t *t, voice_t *v)
{
    mth_part_t *mp = mouth_of(t);
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), ph = (uint32_t)clamp(p[P_E4], 0, MOUTH_NPHRASE - 1);
    uint32_t mode = (uint32_t)clamp(p[P_E5], 0, 2), n, s, len, cur;
    const uint8_t *bytes, *lens;
    mth_voice_t *s_;
    if (!mp || i >= MOUTH_POLY)
        return;
    if (ph != mp->phrase) {
        mp->phrase = (uint8_t)ph;
        mp->cursor = 0;
    }
    if (mouth_spell_n && mouth_spell_part == (uint32_t)(t - trk) + 1u) {   /* SPELL: the cursor's syllable */
        bytes = mouth_spell_ph;
        lens = mouth_spell_len;
        s = 0;
        len = mouth_spell_n;
    } else {
        bytes = mouth_phrase(ph, &lens, &n);
        cur = mp->cursor;
        s = mth_unit(bytes, n, mode, &cur, &len);
        mp->cursor = (uint8_t)cur;
    }
    s_ = &mp->v[i];
    if (!v->env && !v->env_out)                          /* a fresh voice; a sounding one glides on */
        mth_init(s_);
    mth_set(s_, (uint32_t)clamp(p[P_E0], 0, 127), (uint32_t)clamp(p[P_E1], 0, 127), (uint32_t)clamp(p[P_E2], 0, 127),
            (uint32_t)clamp(p[P_E3], 0, 127));
    mth_say_len(s_, bytes + s, lens ? lens + s : 0, len);
    mp->age[i] = 0;
}

static __attribute__((noinline)) void mouth_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    mth_part_t *mp = mouth_of(t);
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), j, fall;
    mth_voice_t *s;
    int32_t buf[64];
    if (!mp || i >= MOUTH_POLY)
        return;
    s = &mp->v[i];
    /* MOUTH + SHP modulation (+-64 over its range) */
    mth_set(s, (uint32_t)clamp(p[P_E0], 0, 127), (uint32_t)clamp(p[P_E1], 0, 127),
            (uint32_t)clamp(p[P_E2] + ((m->shape - (64 << 8)) >> 8), 0, 127), (uint32_t)clamp(p[P_E3], 0, 127));
    s->breath = p[P_E6] * 180;                               /* up to 0.7, as SYLLABLE's */
    /* FALL: semitones over the first 150 ms, then held */
    fall = mp->age[i] < 6615u ? mp->age[i] : 6615u;
    s->syl.inc = (uint32_t)(((uint64_t)m->inc * st16_ratio(-(int32_t)(p[P_E7] * 16 * fall / 6615u))) >> 16);
    mp->age[i] += n;
    if (!v->gate && s->gate)                                 /* the key is let go: the piece plays on */
        mth_release(s);
    if (!mth_active(s))
        return;
    for (i = 0; i < n; i += 64u) {                           /* i: the amplitude ramp's sample */
        uint32_t k = n - i < 64u ? n - i : 64u;
        for (j = 0; j < k; j++)
            buf[j] = 0;
        mth_render(s, buf, k);
        for (j = 0; j < k; j++)                              /* x 5.64: x 2 as VOICE, then +9 dB make-up (the median */
            out[i + j] += mulq16(voice_amp(buf[j], m, i + j), MOUTH_GAIN);   /* preset's C4 peak at LIM_T) */
    }
}

static const preset_t MOUTH_PRESETS[] = {
    /* SPEED THROAT MOUTH GRIT | PHRASE PLAY BRTH FALL */
    {"HELLO", {64, 64, 64, 0, 0, 0, 0, 1}, {0, 90, 120, 100}, 0, 1, FX(0, 20, 25, 30), PAT(0)},
    {"COUNT IN", {72, 64, 64, 0, 1, 1, 0, 2}, {0, 90, 120, 100}, 0, 1, FX(0, 0, 15, 20), PAT(0)},
    {"LA LA LA", {64, 64, 64, 0, 2, 0, 10, 0}, {10, 100, 127, 100}, 0, 0, FX(0, 80, 20, 90), PAT(0)},
    {"OOH AH", {50, 64, 64, 0, 3, 1, 15, 0}, {20, 100, 127, 110}, 0, 0, FX(0, 70, 25, 80), PAT(0)},
    {"DATA BURN", {64, 58, 70, 30, 4, 0, 0, 1}, {0, 90, 120, 100}, 0, 1, FX(10, 0, 30, 20), PAT(0)},
    {"ROBOT 82", {80, 50, 50, 100, 5, 2, 0, 0}, {0, 90, 120, 110}, 0, 1, FX(0, 0, 15, 20), PAT(0)},
    {"DO RE MI", {64, 64, 64, 0, 7, 0, 5, 0}, {0, 90, 120, 100}, 0, 1, FX(0, 40, 20, 50), PAT(0)},
    {"GIANT", {40, 0, 0, 40, 8, 2, 0, 3}, {0, 90, 120, 100}, 0, 1, FX(0, 30, 25, 40), PAT(0)},   /* (REL 100: the slowest SPEED already makes the tail long) */
    {"TINY", {100, 127, 127, 0, 6, 0, 0, 1}, {0, 70, 100, 90}, 0, 1, FX(10, 0, 30, 20), PAT(0)},
    {"WHISPER", {64, 64, 64, 0, 0, 1, 100, 0}, {0, 90, 110, 100}, 0, 1, FX(0, 0, 10, 30), PAT(0)},
    {"GRIT", {64, 64, 64, 127, 9, 0, 0, 0}, {0, 90, 120, 100}, 0, 1, FX(0, 0, 15, 20), PAT(0)},
};

static const engine_t ENG_MOUTH = {
    .name = "MOUTH",
    .page_title = {"MOUTH", "PHRASE"},
    .edit = {
        {"SPEED", F_INT, 0, 127, 64, 0, 0},
        {"THROAT", F_INT, 0, 127, 64, 0, 0},
        {"MOUTH", F_INT, 0, 127, 64, 0, 0},
        {"GRIT", F_PCT, 0, 127, 0, 0, 0},
        {"PHRASE", F_ENUM, 0, MOUTH_NPHRASE - 1, 0, MOUTH_PHRASE_NAMES, 0},
        {"PLAY", F_ENUM, 0, 2, 0, MTH_PLAY_NAMES, 0},
        {"BRTH", F_PCT, 0, 127, 0, 0, 0},
        {"FALL", F_SEMI, 0, 12, 1, 0, 0},
    },
    .presets = MOUTH_PRESETS,
    .npresets = NELEM(MOUTH_PRESETS),
    .note_on = mouth_note_on,
    .render = mouth_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = MOUTH_POLY,
    .mem = sizeof(mth_part_t),
};
