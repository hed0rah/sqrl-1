/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * SYLLABLE: consonant-vowel syllables from the keys, for SQRL-1. Each note sings a
 * consonant into a vowel (Klatt-style cascade/parallel formants, syllable.c); the vowel holds while
 * the key does. VOICE (eng_formant.c) sings the vowels; this is the consonants VOICE does not have.
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs syllable.c/.h
 * next to it. State: one syl_part_t per part in ENG_MEM (engine_t.mem).
 *
 *   CONS    the consonant: - b d g p t k m n s z sh f v h l r w y
 *   VOWEL   the vowel: i I e E ae a V O o U u er (Hillenbrand 1995's twelve), diphthongs ai au ei oi ou,
 *           and iau, the cat's (m + iau: mee-ow, the NIBS preset)
 *   LEN     the consonant's length, 25 % .. 200 %
 *   SHIFT   every formant up or down, -12 .. +12 semitones (+ SHP modulation): smaller or larger mouths
 *   BRTH    breath under the whole syllable
 *   PICK    FIX (as set), C>> (the next consonant each note), V>> (the next vowel), BOTH, RAND, or a
 *           phrase: each note sings its next syllable (HELLO: hel-lo maze-mind, DATABN, FM ONE, ROBOT)
 *   FALL    the pitch falls over the syllable, 0 .. 12 semitones in 150 ms, as speech does
 *   END     the consonant that closes the syllable when the key is let go (- = none): ba + t = bat.
 *           It plays under the release, so give the sound a release of 150 ms or more */
#include "syllable.c"

#define SYL_POLY 4u
enum { SYL_FIX, SYL_CNEXT, SYL_VNEXT, SYL_BOTH, SYL_RAND, SYL_PHRASE };
#define SYL_NPHRASE 4u
#define SYL_NO_PHRASE 255u
#define SYL_NWALK 17u                /* the vowels V>> and RAND walk: all but iau, the cat's */

/* the phrases, a syllable a note: consonant, vowel, closing consonant (indices of SYL_CONS_NAMES,
 * SYL_VOWEL_NAMES; 0 = none) */
typedef struct { uint8_t c, v, end; } syl_step_t;
static const syl_step_t SYL_HELLO[] = {{14, 3, 0}, {15, 16, 0}, {7, 14, 10}, {7, 12, 8}};    /* hel lo maze mind */
static const syl_step_t SYL_DATABURN[] = {{2, 14, 0}, {5, 6, 0}, {1, 11, 8}};                /* da ta burn */
static const syl_step_t SYL_FMONE[] = {{0, 3, 12}, {0, 3, 7}, {17, 6, 8}};                   /* ef em one */
static const syl_step_t SYL_ROBOT[] = {{16, 16, 0}, {1, 5, 5}};                             /* ro bot */
static const struct { const syl_step_t *s; uint8_t n; } SYL_PHRASES[SYL_NPHRASE] = {
    {SYL_HELLO, 4}, {SYL_DATABURN, 3}, {SYL_FMONE, 3}, {SYL_ROBOT, 2}};
static const char *const SYL_PICK_NAMES[9] = {"FIX", "C>>", "V>>", "BOTH", "RAND", "HELLO", "DATABN", "FM ONE", "ROBOT"};

typedef struct {
    syl_voice_t v[SYL_POLY];
    uint32_t age[SYL_POLY];          /* samples since the note-on (FALL) */
    uint8_t closed[SYL_POLY];        /* END has started on this voice's release */
    uint8_t ov_c, ov_v;              /* syllable_next(): the next note's consonant and vowel + 1, 0 = none */
    uint8_t ov_note;                 /* syllable_next_note(): only for this note number, 255 = any note */
    uint8_t c_next, v_next;
    uint8_t phr_pos;                 /* the next syllable of a phrase */
    uint8_t sung, now_c, now_v, now_end, now_phr, now_pos;   /* syl_now(): the latest note's syllable */
    uint8_t end[SYL_POLY];           /* a phrase's closing consonant for this voice + 1, 0 = END's */
    int32_t rnd;
} syl_part_t;
ENG_MEM_FITS("SYLLABLE", sizeof(syl_part_t));

static syl_part_t *syl_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (syl_part_t *)ENG_MEM(t - trk) : 0;
}

/* 2^(st16 / 192) as Q16, st16 in 1/16 semitone, -24 .. +24 semitones */
static uint32_t syl_ratio(int32_t st16)
{
    static const uint32_t SEMI[13] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032,
                                      110218, 116772, 123715, 131072};
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -24 * 16, 24 * 16) + 24 * 16;
    o = st16 / 192;
    k = st16 - o * 192;
    r = SEMI[k >> 4] + (((SEMI[(k >> 4) + 1] - SEMI[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 2 ? r << (o - 2) : r >> (2 - o);
}

static void syl_note_on(track_t *t, voice_t *v)
{
    syl_part_t *sp = syl_of(t);
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), c = (uint32_t)clamp(p[P_E0], 0, SYL_NCONS - 1);
    uint32_t vw = (uint32_t)clamp(p[P_E1], 0, SYL_NVOWEL - 1);
    uint32_t len = 2u + (uint32_t)clamp(p[P_E2], 0, 127) * 14u / 127u;   /* eighths: 2 .. 16 */
    if (!sp || i >= SYL_POLY)
        return;
    switch (p[P_E5]) {
    case SYL_CNEXT:
        c = sp->c_next++ % SYL_NCONS;
        break;
    case SYL_VNEXT:
        vw = sp->v_next++ % SYL_NWALK;
        break;
    case SYL_BOTH:
        c = sp->c_next++ % SYL_NCONS;
        vw = sp->v_next++ % SYL_NWALK;
        break;
    case SYL_RAND:
        if (!sp->rnd)
            sp->rnd = 0x2545F491;
        c = (noise32(&sp->rnd) >> 16) % SYL_NCONS;
        vw = (noise32(&sp->rnd) >> 16) % SYL_NWALK;
        break;
    }
    sp->end[i] = 0;
    sp->now_phr = SYL_NO_PHRASE;
    sp->now_pos = 0;
    if (p[P_E5] >= SYL_PHRASE && p[P_E5] < SYL_PHRASE + (int)SYL_NPHRASE) {
        uint32_t ph = (uint32_t)(p[P_E5] - SYL_PHRASE), at = sp->phr_pos % SYL_PHRASES[ph].n;
        const syl_step_t *st = &SYL_PHRASES[ph].s[at];
        c = st->c;
        vw = st->v;
        sp->end[i] = (uint8_t)(st->end + 1u);
        sp->phr_pos = (uint8_t)((at + 1u) % SYL_PHRASES[ph].n);
        sp->now_phr = (uint8_t)ph;
        sp->now_pos = (uint8_t)at;
    }
    if (sp->ov_c && (sp->ov_note == 255u || sp->ov_note == v->note)) {   /* a mode chose this note's syllable */
        c = sp->ov_c - 1u;
        vw = sp->ov_v - 1u;
        sp->ov_c = sp->ov_v = 0;
    }
    sp->c_next %= SYL_NCONS;
    sp->v_next %= SYL_NWALK;
    syl_start(&sp->v[i], c, vw, len, v->env || v->env_out);   /* a sounding voice keeps its filters */
    sp->age[i] = 0;
    sp->closed[i] = 0;
    sp->sung = 1;
    sp->now_c = (uint8_t)c;
    sp->now_v = (uint8_t)vw;
    sp->now_end = (uint8_t)(sp->end[i] ? sp->end[i] - 1u : (uint32_t)clamp(p[P_E7], 0, SYL_NCONS - 1));
}

/* for the screen: the latest note's syllable on part t. cons and end index SYL_CONS_NAMES (0 = none),
 * vowel SYL_VOWEL_NAMES; phrase indexes SYL_PHRASES (syl_phrase()) or is SYL_NO_PHRASE, pos is the
 * syllable within it. 0 = no note since the engine was claimed */
static int syl_now(const track_t *t, uint32_t *cons, uint32_t *vowel, uint32_t *end, uint32_t *phrase, uint32_t *pos)
{
    const syl_part_t *sp = syl_of(t);
    if (!sp || !sp->sung)
        return 0;
    *cons = sp->now_c;
    *vowel = sp->now_v;
    *end = sp->now_end;
    *phrase = sp->now_phr;
    *pos = sp->now_pos;
    return 1;
}

/* phrase ph: its name (as PICK shows it), its syllables (c, v, end each) and how many */
static const char *syl_phrase(uint32_t ph, const syl_step_t **steps, uint32_t *n)
{
    if (ph >= SYL_NPHRASE)
        return 0;
    *steps = SYL_PHRASES[ph].s;
    *n = SYL_PHRASES[ph].n;
    return SYL_PICK_NAMES[SYL_PHRASE + ph];
}

/* the next note on part t sings consonant c into vowel v, whatever PICK says: a mode (ALIVE, a
 * sequencer of syllables) calls this, then plays the note */
void syllable_next(track_t *t, uint32_t c, uint32_t v)
{
    syl_part_t *sp = syl_of(t);
    if (!sp)
        return;
    sp->ov_c = (uint8_t)((c < SYL_NCONS ? c : 0u) + 1u);
    sp->ov_v = (uint8_t)((v < SYL_NVOWEL ? v : 0u) + 1u);
    sp->ov_note = 255u;
}

/* as syllable_next, for one note number only: a key played meanwhile keeps PICK's syllable, and the
 * override waits for that note */
void syllable_next_note(track_t *t, uint32_t note, uint32_t c, uint32_t v)
{
    syl_part_t *sp = syl_of(t);
    if (!sp)
        return;
    syllable_next(t, c, v);
    sp->ov_note = (uint8_t)(note < 255u ? note : 254u);
}

static __attribute__((noinline)) void syl_render_v(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    syl_part_t *sp = syl_of(t);
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), j, fall;
    syl_voice_t *s;
    int32_t buf[64];
    if (!sp || i >= SYL_POLY)
        return;
    s = &sp->v[i];
    s->fmul = syl_ratio(p[P_E3] * 16 + ((m->shape - (64 << 8)) * 3 >> 7));
    s->breath = p[P_E4] * 180;                               /* up to 0.7 */
    /* FALL: semitones over the first 150 ms, then held */
    fall = sp->age[i] < 6615u ? sp->age[i] : 6615u;
    s->inc = (uint32_t)(((uint64_t)m->inc * syl_ratio(-(int32_t)(p[P_E6] * 16 * fall / 6615u))) >> 16);
    sp->age[i] += n;
    if (!v->gate && !sp->closed[i]) {                        /* the key is let go: END closes the syllable */
        int32_t end = sp->end[i] ? sp->end[i] - 1 : p[P_E7];
        sp->closed[i] = 1;
        if (end > 0)
            syl_coda(s, (uint32_t)clamp(end, 0, SYL_NCONS - 1), 2u + (uint32_t)clamp(p[P_E2], 0, 127) * 14u / 127u);
    }
    for (i = 0; i < n; i += 64u) {                           /* i: the amplitude ramp's sample */
        uint32_t k = n - i < 64u ? n - i : 64u;
        for (j = 0; j < k; j++)
            buf[j] = 0;
        syl_render(s, buf, k);
        for (j = 0; j < k; j++)                              /* the vowels peak near half scale: << 1, as VOICE */
            out[i + j] += voice_amp(buf[j], m, i + j) << 1;
    }
}


static const preset_t SYL_PRESETS[] = {
    /* CONS VOWEL LEN SHIFT | BRTH PICK FALL END */
    {"CYCLE", {1, 5, 64, 0, 0, 1, 2, 0}, {0, 90, 120, 40}, 0, 1, FX(0, 0, 15, 20), PAT(0)},
    {"BAT BIT BUT", {1, 5, 55, 0, 0, 2, 1, 5}, {0, 90, 120, 75}, 0, 1, FX(0, 0, 15, 20), PAT(0)},
    {"MAMA", {7, 5, 80, 0, 10, 0, 1, 0}, {4, 90, 120, 60}, 0, 0, FX(0, 40, 20, 50), PAT(0)},
    {"SING SONG", {9, 1, 60, 0, 5, 2, 1, 8}, {0, 90, 120, 80}, 0, 1, FX(0, 30, 25, 40), PAT(0)},
    {"SHUSH", {11, 10, 110, 0, 20, 0, 0, 11}, {0, 90, 110, 80}, 0, 1, FX(0, 0, 10, 30), PAT(0)},
    {"BABBLE", {0, 0, 50, 2, 8, 4, 3, 0}, {0, 80, 110, 40}, 0, 1, FX(0, 20, 35, 30), PAT(0)},
    {"LALA CHOIR", {15, 5, 90, -2, 30, 0, 0, 0}, {30, 100, 127, 80}, 0, 0, FX(0, 80, 20, 90), PAT(0)},
    {"SMALL MOUTH", {4, 0, 40, 9, 0, 3, 4, 6}, {0, 70, 100, 70}, 0, 1, FX(10, 0, 30, 20), PAT(0)},
    {"HELLO", {0, 0, 55, 0, 6, 5, 2, 0}, {0, 90, 120, 80}, 0, 1, FX(0, 20, 25, 30), PAT(0)},
    {"DATA BURN", {0, 0, 55, -2, 4, 6, 3, 0}, {0, 90, 120, 80}, 0, 1, FX(10, 0, 30, 20), PAT(0)},
    /* acidcat's nibs: m into iau, a small mouth, breath, the pitch falling off the end. Play it high (C5 and up) */
    {"NIBS", {7, 17, 70, 5, 14, 0, 4, 0}, {0, 100, 100, 60}, 0, 1, FX(0, 0, 10, 15), PAT(0)},
};

static const engine_t ENG_SYLLABLE = {
    .name = "SYLLABLE",
    .page_title = {"SYLL", "MOUTH"},
    .edit = {
        {"CONS", F_ENUM, 0, SYL_NCONS - 1, 1, SYL_CONS_NAMES, 0},
        {"VOWEL", F_ENUM, 0, SYL_NVOWEL - 1, 5, SYL_VOWEL_NAMES, 0},
        {"LEN", F_PCT, 0, 127, 55, 0, 0},
        {"SHIFT", F_SEMI, -12, 12, 0, 0, 0},
        {"BRTH", F_PCT, 0, 127, 0, 0, 0},
        {"PICK", F_ENUM, 0, 8, 0, SYL_PICK_NAMES, 0},
        {"FALL", F_SEMI, 0, 12, 1, 0, 0},
        {"END", F_ENUM, 0, SYL_NCONS - 1, 0, SYL_CONS_NAMES, 0},
    },
    .presets = SYL_PRESETS,
    .npresets = NELEM(SYL_PRESETS),
    .note_on = syl_note_on,
    .render = syl_render_v,
    .knob = {P_E0, P_E1, P_E3, P_E4},
    .poly = SYL_POLY,
    .mem = sizeof(syl_part_t),
};
