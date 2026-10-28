/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SAVE > PHRASES) and the parts' sounds at power-on. */
#include "dsp.c"

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with large per-part state
 * (engine_t.mem: PHYS's slots, GRAIN's grains and index, DRUM's lanes, FM6's notes, SLICE's reverse windows)
 * keep it in one region per part instead of an array each. The region holds the state of the engine that
 * last claimed it (eng_mem_owner); a switch to another such engine zeroes its engine_t.mem bytes, which is
 * the state each had at power-on, so a switch sounds as it did with the arrays (tests/union_test.c). State
 * that runs on across notes and switches stays outside (WHEEL's wheels, GRAIN's random numbers).
 * Sized by the largest, PHYS (3 x SYMP's slot); an engine that needs more than ENG_MEM_PART grows it for
 * every part (x NPART of the pool). Zero-initialised pool section: main.c fm1_cstart clears it. */
#define ENG_MEM_PART 12888u
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
#define ENG_MEM(p) ((void *)eng_mem[p].b)  /* part p's region (p < NPART) */
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")

/* an engine left out of the build (core.h FELUCCA_x): no presets, never offered (eng_ok), silent; its number stays
 * reserved, and a sound on it plays on its sibling (shell.c sh_eng_sibling) */
static void gone_note_on(track_t *t, voice_t *v) { (void)t; (void)v; }
static void gone_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
#define GONE_P {"-", F_INT, 0, 0, 0, 0, 0}
static const engine_t ENG_GONE __attribute__((unused)) = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {GONE_P, GONE_P, GONE_P, GONE_P, GONE_P, GONE_P, GONE_P, GONE_P},
    .note_on = gone_note_on,
    .render = gone_render,
    .knob = {P_E0, P_E1, P_E2, P_REL},
};

#include "eng_analog.c"
#if FELUCCA_PHASE
#include "eng_phase.c"
#endif
#if FELUCCA_LOFI
#include "eng_lofi.c"
#endif
#include "eng_sample.c"         /* (the sets and the user slots always; the SAMPLE engine with FELUCCA_SAMPLE) */
#if FELUCCA_VOICE
#include "eng_formant.c"
#endif
#if FELUCCA_TRIO
#include "eng_trio.c"
#endif
#if FELUCCA_WHEEL
#include "eng_wheel.c"
#endif
#if FELUCCA_GRAIN
#include "eng_grain.c"
#endif
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#if FELUCCA_NOISE
#include "eng_noise.c"
#endif
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if FELUCCA_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; FELUCCA_FM4=1 builds it) */
#endif
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#include "eng_lizard.c"          /* LIZARD (SQRL-1): the Electrik Lizard v2's oscillator modes (lizard.c) */
#if FELUCCA_TALK
#include "eng_talk.c"            /* TALK: a TMS5220 LPC voice (lpc5220.c, banks/) */
#endif
#if FELUCCA_SYLLABLE
#include "eng_syllable.c"        /* SYLLABLE: Klatt-style consonant-vowel syllables (syllable.c; MOUTH sings on it too) */
#endif
#if FELUCCA_VODER
#include "eng_voder.c"           /* VODER: Dudley's 1939 Voder, keys as bands (voder.c) */
#endif
#include "eng_shock.c"           /* SHOCK: shock-wave phase distortion from a table (shock.c) */
#if FELUCCA_SHELL
#if FELUCCA_ORGAN
#include "eng_organ.c"           /* ORGAN: a tonewheel organ's drawbars (organ.c) */
#endif
#include "eng_mouth.c"           /* MOUTH: a phoneme singer on syllable.c (mouth.c; after eng_syllable.c) */
#if FELUCCA_LOOP
#include "eng_loop.c"            /* LOOP: a tape loop on the echo's tape (tapeloop.c; after eng_mouth.c) */
#endif
static void shell_loop_top(track_t *t);  /* shell.c: step 1 of a part's pattern (seq.c seq_enter), MOUTH's and SYLLABLE's
                                          * phrase from its top */
#else
#define shell_loop_top(t) ((void)(t))
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder */
/* each slot: the engine, or the stub when its flag leaves it out */
#if FELUCCA_PHASE
#define FELUCCA_PHASE_E (&ENG_PHASE)
#else
#define FELUCCA_PHASE_E (&ENG_GONE)
#endif
#if FELUCCA_LOFI
#define FELUCCA_LOFI_E (&ENG_LOFI)
#else
#define FELUCCA_LOFI_E (&ENG_GONE)
#endif
#if FELUCCA_VOICE
#define FELUCCA_VOICE_E (&ENG_FORMANT)
#else
#define FELUCCA_VOICE_E (&ENG_GONE)
#endif
#if FELUCCA_TRIO
#define FELUCCA_TRIO_E (&ENG_TRIO)
#else
#define FELUCCA_TRIO_E (&ENG_GONE)
#endif
#if FELUCCA_WHEEL
#define FELUCCA_WHEEL_E (&ENG_WHEEL)
#else
#define FELUCCA_WHEEL_E (&ENG_GONE)
#endif
#if FELUCCA_SAMPLE
#define FELUCCA_SAMPLE_E (&ENG_SAMPLE)
#else
#define FELUCCA_SAMPLE_E (&ENG_GONE)
#endif
#if FELUCCA_GRAIN
#define FELUCCA_GRAIN_E (&ENG_GRAIN)
#else
#define FELUCCA_GRAIN_E (&ENG_GONE)
#endif
#if FELUCCA_NOISE
#define FELUCCA_NOISE_E (&ENG_NOISE)
#else
#define FELUCCA_NOISE_E (&ENG_GONE)
#endif
#if FELUCCA_SLICE
#define FELUCCA_SLICE_E (&ENG_SLICE)
#else
#define FELUCCA_SLICE_E (&ENG_GONE)
#endif
#if FELUCCA_TALK
#define FELUCCA_TALK_E (&ENG_TALK)
#else
#define FELUCCA_TALK_E (&ENG_GONE)
#endif
#if FELUCCA_SYLLABLE
#define FELUCCA_SYLLABLE_E (&ENG_SYLLABLE)
#else
#define FELUCCA_SYLLABLE_E (&ENG_GONE)
#endif
#if FELUCCA_VODER
#define FELUCCA_VODER_E (&ENG_VODER)
#else
#define FELUCCA_VODER_E (&ENG_GONE)
#endif
#if FELUCCA_ORGAN
#define FELUCCA_ORGAN_E (&ENG_ORGAN)
#else
#define FELUCCA_ORGAN_E (&ENG_GONE)
#endif
#if FELUCCA_LOOP
#define FELUCCA_LOOP_E (&ENG_LOOP)
#else
#define FELUCCA_LOOP_E (&ENG_GONE)
#endif
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 */
#if FELUCCA_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
    FELUCCA_PHASE_E,             /* 2 */
    FELUCCA_LOFI_E,              /* 3 */
    FELUCCA_SAMPLE_E,            /* 4 */
    FELUCCA_VOICE_E,             /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    FELUCCA_TRIO_E,              /* 6 */
    FELUCCA_WHEEL_E,             /* 7 */
    FELUCCA_GRAIN_E,             /* 8 */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    FELUCCA_NOISE_E,             /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
    FELUCCA_SLICE_E,             /* 13 (ENGI_SLICE) */
    &ENG_LIZARD,                 /* 14 (ENGI_LIZARD, SQRL-1) */
    FELUCCA_TALK_E,              /* 15 (ENGI_TALK) */
    FELUCCA_SYLLABLE_E,          /* 16 (ENGI_SYLLABLE) */
    FELUCCA_VODER_E,             /* 17 (ENGI_VODER) */
    &ENG_SHOCK,                  /* 18 (ENGI_SHOCK) */
#if FELUCCA_SHELL
    FELUCCA_ORGAN_E,             /* 19 (ENGI_ORGAN, SQRL-1) */
    &ENG_MOUTH,                  /* 20 (ENGI_MOUTH, SQRL-1) */
    FELUCCA_LOOP_E,              /* 21 (ENGI_LOOP, SQRL-1) */
#endif
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* the sample source a track's sound plays that has no data (SAMPLE's SET, GRAIN's SRC, SLICE's SRC: an empty or
 * invalid user slot, a set or PIANO this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
#if FELUCCA_SLICE
    if (e == &ENG_SLICE) {
        si = slc_src_of(t->p);
        *code = 64u + si;
        return slc_get(si) ? 0 : N_SLC_SRC[si];
    }
#endif
    int grain = FELUCCA_GRAIN && e == FELUCCA_GRAIN_E;
    if (!(FELUCCA_SAMPLE && e == FELUCCA_SAMPLE_E) && !grain)
        return 0;
    *code = (grain ? 32u : 1u) + si % 32u;
    return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with FELUCCA_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
#if FELUCCA_FM4
    1,                           /* DIGITAL */
#endif
#if FELUCCA_PHASE
    2,                           /* PHASE */
#endif
#if FELUCCA_LOFI
    3,                           /* LOFI */
#endif
#if FELUCCA_SAMPLE
    4,                           /* SAMPLE */
#endif
#if FELUCCA_VOICE
    5,                           /* VOICE */
#endif
#if FELUCCA_TRIO
    6,                           /* TRIO */
#endif
#if FELUCCA_WHEEL
    7,                           /* WHEEL */
#endif
#if FELUCCA_GRAIN
    8,                           /* GRAIN */
#endif
    9,                           /* PHYS */
#if FELUCCA_NOISE
    11,                          /* NOISE */
#endif
    14,                          /* LIZARD */
#if FELUCCA_SLICE
    13,                          /* SLICE */
#endif
    10,                          /* DRUM */
#if FELUCCA_TALK
    15,                          /* TALK */
#endif
#if FELUCCA_SYLLABLE
    16,                          /* SYLLABLE: past the stock EDIT layer's 14 keys, by PRESETS */
#endif
#if FELUCCA_VODER
    17,                          /* VODER */
#endif
    18,                          /* SHOCK */
#if FELUCCA_SHELL && FELUCCA_ORGAN
    19,                          /* ORGAN */
#endif
#if FELUCCA_SHELL
    20,                          /* MOUTH */
#endif
#if FELUCCA_SHELL && FELUCCA_LOOP
    21,                          /* LOOP */
#endif
};

/* the engines one can pick (engine 1 only with FELUCCA_FM4), in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES && ENGINES[e] != &ENG_GONE && (FELUCCA_FM4 || e != ENGI_DIGITAL); }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without FELUCCA_FM4: its sounds play as FM6) */
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* factory sequence patterns: SAVE > PHRASES loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (DRUM plays them as GM drums, SLICE as slices; SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* SLICE (eng_slice.c): note = C4 + slice */
    {"CHOP", {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64},     /* 9 16 slices re-ordered */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"STUTTER", {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67},    /* 10 8 slices, repeats */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"SLICES", {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},   /* 11 in order */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* DRUM (General MIDI: 36 kick, 38 snare, 42 closed / 46 open hi-hat) */
    {"BEAT", {36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},     /* 12 */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP presets (RAVE, ARP 8BIT, ARP LEAD) suggest
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 13 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty: all are): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][3] = {{0, 4, 0}, {ENGI_FM6, 4, 0}, {3, 0, 0}, {ENGI_DRUM, 0, 0}}; /* ANALOG ACID,
                                                                       * FM6 PAD (was DIGITAL PAD), LOFI PULSE LD, DRUM KIT */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
