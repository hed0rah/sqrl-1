/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* LIZARD: the oscillator modes of the Vinicius Electrik Lizard v2 (lizard.c, the lizard core:
 * uint32 phases, float, polyBLEP edges), one core per voice, into Felucca's low-pass.
 *
 * MODE picks what the core plays:
 *   DRAW  WaveDrawer: a cycle is 8 held levels; WAVE one of 16 drawings, SWP the order the steps
 *         are walked (UP / DOWN / UPDN / RAND: the Lizard's Type key)
 *   SWCH  the 8-input switcher in VCO form: each of the 8 steps is a slice of a source (sine,
 *         tri, saw, DC) running at the note's own phase; WAVE one of 16 source sets
 *   CZ    whole-cycle switching: each step plays one full cycle of its source, so two
 *         alternating steps give the Casio CZ trick; the sources run an octave up so a pair
 *         sounds the played note (sets with longer periods fall below it)
 *   PSAW  PhasedSaw: VCS saws (2..5), DTN spreads their detune and start phases
 *   SUPR  SuperWave: a centre saw, two down and two up; DTN the spread, VCS the slaves' level
 * EDGE: BLEP, the 2-point polyBLEP at every step edge (the default); HQ, the same at 2x through
 * the core's halfband (about twice the cost, flat with pitch); RAW, no correction: the Lizard's
 * own naive waves, aliasing and all. The value is the core's blep setting.
 * A drawing's mean is taken out (as LOFI does with its wave RAM): the Lizard's DC goes through
 * an AC-coupled output, here it would stack up with the chord.
 * CUT / RES: the trapezoidal low-pass of dsp.c, moved by the track's ENV / LFO -> FLT.
 *
 * The core keeps far more state than voice_t has (step tables, sweep orders, 5 slave phases):
 * lz_part_t in the part's engine memory (1.5's shared region, as FM6 and PHYS). The ISR gives m->inc per block; the core wants Hz, one
 * float multiply. Every block re-applies the parameters (the knobs move while a note sounds);
 * a sweep change rebuilds the step order, the rest is a few stores. */
#include "lizard.h"
#include "lizard.c"
#if FELUCCA_SHELL
#include "lzshape.c"                    /* the SHAPE stage (from databurn, MIT): every engine's page 3 */

/* the SHAPE page's values for lzshape: UNI, WARP and DTN are LIZARD's own (1 and 0 elsewhere) */
static void shape_par(const track_t *t, lzs_par *sp)
{
    const int16_t *p = t->p;
    int lz = t->eng_req == ENGI_LIZARD;
    sp->uni = lz ? (int)clamp(p[P_LZUNI], 1, LZS_MAX_UNI) : 1;
    sp->detune = lz ? (float)p[P_E3] * (1.0f / 127.0f) : 0.0f;
    sp->warp = lz ? (float)p[P_LZWARP] * (1.0f / 127.0f) : 0.0f;
    sp->fold = (float)p[P_FOLD] * (1.0f / 127.0f);
    sp->sym = (float)p[P_SYM] * (1.0f / 64.0f);
    sp->crush = (float)p[P_CRUSH] * (1.0f / 127.0f);
    sp->down = (float)p[P_DOWN] * (1.0f / 127.0f);
    sp->blep = lz ? p[P_E5] != 0 : 1;
}
/* any of FOLD CRUSH DOWN on (lzs_shapes, in integers: the render loop asks it every block) */
static int shape_on(const int16_t *p) { return p[P_FOLD] | p[P_CRUSH] | p[P_DOWN]; }

/* every engine but LIZARD (which shapes inside its render): the voice rendered alone, shaped, then added, so a fold
 * stays per note (a folded chord through one stage turns to mud). Its state per part and voice, new with each note
 * (the voice's age) */
static lzs_t shape_st[NPART][NVOICE] __attribute__((section(".pool")));
static uint32_t shape_age[NPART][NVOICE];
static __attribute__((noinline)) void shape_voice(const engine_t *e, track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                  const vmod_t *m)
{
    int32_t tmp[HALF_FRAMES];
    uint32_t p = (uint32_t)(t - trk) % NPART, vi = (uint32_t)(v - t->v) % NVOICE, i;
    lzs_par sp;
    if (n > HALF_FRAMES)
        n = HALF_FRAMES;
    if (shape_age[p][vi] != v->age) {
        lzs_init(&shape_st[p][vi], p * NVOICE + vi + 1u);
        shape_age[p][vi] = v->age;
    }
    for (i = 0; i < n; i++)
        tmp[i] = 0;
    e->render(t, v, tmp, n, m);
    shape_par(t, &sp);
    lzs_shape_i32(&shape_st[p][vi], &sp, tmp, (int)n);
    for (i = 0; i < n; i++)
        out[i] += tmp[i];
}
#endif

enum { LZM_DRAW, LZM_SWCH, LZM_CZ, LZM_PSAW, LZM_SUPR };
static const char *const N_LZ_MODE[] = {"DRAW", "SWCH", "CZ", "PSAW", "SUPR"};
static const char *const N_LZ_SWP[] = {"UP", "DOWN", "UPDN", "RAND"};
static const char *const N_LZ_EDGE[] = {"RAW", "BLEP", "HQ"};   /* = o->blep: 0, 1, LZ_BLEP_HQ */

/* a step set: the source and level (-100..100 %) of each of the 8 steps */
typedef struct {
    uint8_t src[8];
    int8_t lv[8];
} lz_set_t;
#define DC8 {LZ_DC, LZ_DC, LZ_DC, LZ_DC, LZ_DC, LZ_DC, LZ_DC, LZ_DC}
#define ALT(a, b) {a, b, a, b, a, b, a, b}
#define L100 {100, 100, 100, 100, 100, 100, 100, 100}

/* DRAW: 16 drawings (all DC: the WaveDrawer's 8 pots) */
static const char *const N_LZ_DRAW[] = {"LZRD", "SQR", "PLS25", "STAIR", "RAMP", "TRI8", "SIN8", "BUMP",
                                        "HOLW", "SPIKE", "ZIG", "DBL", "FORM", "BASS", "GLAS", "WOB", 0};
static const lz_set_t LZ_DRAW[16] = {
    {DC8, {100, 60, 20, -30, -80, -40, 30, 70}},          /* LZRD: the lopsided demo shape */
    {DC8, {100, 100, 100, 100, -100, -100, -100, -100}},  /* SQR */
    {DC8, {100, 100, -100, -100, -100, -100, -100, -100}},/* PLS25 */
    {DC8, {-100, -71, -43, -14, 14, 43, 71, 100}},        /* STAIR: a saw in 8 steps */
    {DC8, {100, 71, 43, 14, -14, -43, -71, -100}},        /* RAMP: down */
    {DC8, {-100, -50, 0, 50, 100, 50, 0, -50}},           /* TRI8 */
    {DC8, {0, 71, 100, 71, 0, -71, -100, -71}},           /* SIN8 */
    {DC8, {-80, -40, 40, 100, 100, 40, -40, -80}},        /* BUMP */
    {DC8, {100, 20, 20, 20, -100, -20, -20, -20}},        /* HOLW: odd harmonics, thin */
    {DC8, {100, -14, -14, -14, -14, -14, -14, -16}},      /* SPIKE */
    {DC8, {100, -100, 60, -60, 30, -30, 0, 0}},           /* ZIG: decaying zigzag */
    {DC8, {100, 100, -100, -100, 100, 100, -100, -100}},  /* DBL: the octave */
    {DC8, {100, 30, 70, 0, -20, -70, -30, -80}},          /* FORM */
    {DC8, {100, 100, 40, -20, -100, -100, -40, 20}},      /* BASS: rounded square */
    {DC8, {60, 100, 20, -60, -100, -20, 60, -60}},        /* GLAS */
    {DC8, {100, 0, -100, 0, 50, 0, -50, 0}},              /* WOB */
};

/* SWCH: 16 source sets, slice mode (each source at the note's phase) */
static const char *const N_LZ_SWCH[] = {"MIXED", "SNSAW", "SWTRI", "HALF", "SAWCH", "TRISQ", "SINDC", "QUAD",
                                        "RING", "SAWGT", "TRIGT", "FLIP", "SINE", "SAW", "TRI", "FADE", 0};
static const lz_set_t LZ_SWCH[16] = {
    {{LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC, LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC}, {100, 80, 100, 50, -100, 80, -100, -50}},
    {ALT(LZ_SINE, LZ_SAW), L100},
    {ALT(LZ_SAW, LZ_TRI), L100},
    {{LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_DC, LZ_DC, LZ_DC, LZ_DC}, {100, 100, 100, 100, 0, 0, 0, 0}},
    {{LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW}, {100, -100, 100, -100, 100, -100, 100, -100}},
    {{LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI}, {100, 100, 100, 100, -100, -100, -100, -100}},
    {ALT(LZ_SINE, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {{LZ_SINE, LZ_SINE, LZ_SAW, LZ_SAW, LZ_TRI, LZ_TRI, LZ_DC, LZ_DC}, {100, 100, 100, 100, 100, 100, 0, 0}},
    {{LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE}, {100, -100, 100, -100, 100, -100, 100, -100}},
    {ALT(LZ_SAW, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {ALT(LZ_TRI, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {{LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW}, {100, -50, 100, -50, 100, -50, 100, -50}},
    {{LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE}, L100},
    {{LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW, LZ_SAW}, L100},
    {{LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI, LZ_TRI}, L100},
    {{LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE}, {100, 80, 60, 40, 20, 0, -40, -80}},
};

/* CZ: 16 whole-cycle sets. Pairs repeat 4 times over the 8 steps and sound the note (the
 * sources run an octave up); 4-step sets sound an octave below it, 8-step sets two */
static const char *const N_LZ_CZ[] = {"SNSAW", "SINDC", "SAWDC", "TRIDC", "SNTRI", "SWTRI", "SIN-", "SAW-",
                                      "QUAD", "PLUCK", "OCT8", "AM", "SAWAM", "WALTZ", "SKIP", "SINE", 0};
static const lz_set_t LZ_CZ[16] = {
    {ALT(LZ_SINE, LZ_SAW), L100},
    {ALT(LZ_SINE, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {ALT(LZ_SAW, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {ALT(LZ_TRI, LZ_DC), {100, 0, 100, 0, 100, 0, 100, 0}},
    {ALT(LZ_SINE, LZ_TRI), L100},
    {ALT(LZ_SAW, LZ_TRI), L100},
    {ALT(LZ_SINE, LZ_SINE), {100, -100, 100, -100, 100, -100, 100, -100}},
    {ALT(LZ_SAW, LZ_SAW), {100, -100, 100, -100, 100, -100, 100, -100}},
    {{LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC, LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC}, {100, 100, 100, 0, 100, 100, 100, 0}},
    {{LZ_SINE, LZ_DC, LZ_DC, LZ_DC, LZ_SINE, LZ_DC, LZ_DC, LZ_DC}, {100, 0, 0, 0, 100, 0, 0, 0}},
    {{LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC, LZ_SINE, LZ_SAW, LZ_TRI, LZ_DC}, {100, 80, 100, 0, -100, 80, -100, 0}},
    {ALT(LZ_SINE, LZ_SINE), {100, 50, 100, 50, 100, 50, 100, 50}},
    {ALT(LZ_SAW, LZ_SAW), {100, 50, 100, 50, 100, 50, 100, 50}},
    {{LZ_SINE, LZ_TRI, LZ_SINE, LZ_DC, LZ_SINE, LZ_TRI, LZ_SINE, LZ_DC}, {100, 100, 100, 0, 100, 100, 100, 0}},
    {{LZ_SAW, LZ_SAW, LZ_SAW, LZ_DC, LZ_SAW, LZ_SAW, LZ_SAW, LZ_DC}, {100, 100, 100, 0, 100, 100, 100, 0}},
    {{LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE, LZ_SINE}, L100},
};
static const lz_set_t *const LZ_SETS[3] = {LZ_DRAW, LZ_SWCH, LZ_CZ};

/* WAVE and VCS read differently per MODE: the same range and default, other names (lofi_desc) */
static const param_desc_t LZ_WAVE_SWCH = {"WAVE", F_INT, 0, 127, 0, N_LZ_SWCH, 0};
static const param_desc_t LZ_WAVE_CZ = {"WAVE", F_INT, 0, 127, 0, N_LZ_CZ, 0};
static const param_desc_t LZ_WAVE_NONE = {"WAVE", F_INT, 0, 127, 0, 0, 0};
static const param_desc_t LZ_MIX = {"MIX", F_INT, 2, 5, 3, 0, 0};

#if FELUCCA_SHELL
/* LIZARD's own drawing is in force: drawn (P_LZUSER), and WAVE not turned since (P_LZWAT) */
static int lizard_own(const int16_t *p) { return p[P_LZUSER] && p[P_LZWAT] == p[P_E2]; }
/* a drawn level, -64..63 (F_BIPCT) as the drawings' -100..100, each side over its own range */
static int8_t lizard_lv(int32_t v) { return (int8_t)(v < 0 ? clamp(v, -64, 0) * 100 / 64 : clamp(v, 0, 63) * 100 / 63); }
#endif
#if FELUCCA_SHELL
static const char *const N_LZ_OWN[] = {"OWN", 0};
static const param_desc_t LZ_WAVE_OWN = {"WAVE", F_INT, 0, 127, 0, N_LZ_OWN, 0};
#endif
static const param_desc_t *lizard_desc(const track_t *t, uint32_t k)
{
    int32_t mode = t->p[P_E0];
#if FELUCCA_SHELL
    if (k == 2u && mode == LZM_DRAW && lizard_own(t->p))   /* its own drawing: WAVE says so (a turn: the presets) */
        return &LZ_WAVE_OWN;
#endif
    if (k == 2u)
        return mode == LZM_SWCH ? &LZ_WAVE_SWCH : mode == LZM_CZ ? &LZ_WAVE_CZ : mode >= LZM_PSAW ? &LZ_WAVE_NONE : 0;
    if (k == 4u && mode == LZM_SUPR)
        return &LZ_MIX;
    return 0;
}

/* a part's LIZARD state in its engine memory (engines.c eng_mem, 1.5): one core per voice. A switch to LIZARD
 * zeroes the region (eng_mem_claim), so up[] says which cores still need lz_init */
typedef struct {
    lz_osc v[NVOICE];
    uint8_t up[NVOICE];                                 /* lz_init done */
#if FELUCCA_SHELL
    lzs_t sh[NVOICE];                                   /* each voice's SHAPE stage (lzshape.c) */
#endif
} lz_part_t;
ENG_MEM_FITS("LIZARD", sizeof(lz_part_t));

static lz_osc *lz_of(const track_t *t, const voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk) % NPART, vi = (uint32_t)(v - t->v) % NVOICE;
    lz_part_t *s = (lz_part_t *)ENG_MEM(p);
    lz_osc *o = &s->v[vi];
    if (!s->up[vi]) {                                   /* its own RAND seed: renders repeat */
        lz_init(o, (float)FS, 0x9E3779B9u ^ (p * NVOICE + vi + 1u));
#if FELUCCA_SHELL
        lzs_init(&s->sh[vi], 0x9E3779B9u ^ (p * NVOICE + vi + 1u));
#endif
        s->up[vi] = 1;
    }
    return o;
}
#if FELUCCA_SHELL
static lzs_t *lz_shape_of(const track_t *t, const voice_t *v)
{
    return &((lz_part_t *)ENG_MEM((uint32_t)(t - trk) % NPART))->sh[(uint32_t)(v - t->v) % NVOICE];
}
#endif

/* the parameters into the core (once a block, and before lz_note so the slaves' start phases hold) */
static void lz_setup(lz_osc *o, const int16_t *p)
{
    uint32_t mode = (uint32_t)p[P_E0], i;
    float amt = (float)p[P_E3] * (1.0f / 127.0f);
    o->blep = p[P_E5] == 2 ? LZ_BLEP_HQ : p[P_E5] ? 1 : 0;
    if (mode <= LZM_CZ) {
        const lz_set_t *s = &LZ_SETS[mode][((uint32_t)p[P_E2] >> 3) & 15u];
#if FELUCCA_SHELL
        lz_set_t own;                                   /* DRAW with its own drawing (the shell's DRAW pages): while
                                                         * WAVE stays where it was drawn */
        if (mode == LZM_DRAW && lizard_own(p)) {
            for (i = 0; i < 8u; i++) {
                own.src[i] = LZ_DC;
                own.lv[i] = lizard_lv(p[P_LZD0 + i]);
            }
            s = &own;
        }
#endif
        enum lz_sweep sw = (enum lz_sweep)(p[P_E1] & 3);
        int32_t dc = 0;
        if (mode == LZM_DRAW)                           /* the drawing's mean, taken out below */
            for (i = 0; i < 8u; i++)
                dc += s->lv[i];
        for (i = 0; i < 8u; i++) {
            o->steps[i].src = (enum lz_src)s->src[i];
            o->steps[i].level = (float)(s->lv[i] * 8 - dc) * (0.01f / 8.0f);
        }
        o->whole_cycles = mode == LZM_CZ;
        if (o->sweep != sw)
            lz_set_sweep(o, sw);
    } else if (mode == LZM_PSAW) {
        lz_phasedsaw(o, p[P_E4], amt);
    } else {
        static const float shape[4] = {0.75f, 0.9f, 0.9f, 0.75f};   /* the outer pair a little lower */
        float mix[4], g = (float)(p[P_E4] - 1) * 0.25f;            /* MIX 2..5: a quarter .. all */
        for (i = 0; i < 4u; i++)
            mix[i] = shape[i] * g;
        lz_superwave(o, amt, mix);
    }
}

static inline float lz_hz(uint32_t inc) { return (float)inc * ((float)FS / 4294967296.0f); }

static void lizard_note_on(track_t *t, voice_t *v)
{
    lz_osc *o = lz_of(t, v);
    lz_setup(o, t->p);
    if (v->env)                                         /* retrigger / steal of a sounding voice (voice_start keeps
                                                         * its envelope): keep the phases and the filter, as ANALOG */
        return;
    lz_note(o, lz_hz(pitch_inc(clamp(v->pitch16, 0, 2047))));
#if FELUCCA_SHELL
    lzs_note(lz_shape_of(t, v), 0x9E3779B9u ^ ((uint32_t)(t - trk) * NVOICE + (uint32_t)(v - t->v) + 1u));
#endif
    v->s[0] = v->s[1] = 0;                              /* filter */
}

static __attribute__((noinline)) void lizard_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    lz_osc *o = lz_of(t, v);
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)p[P_E0], i;
    float buf[CTL];
    /* a wave is +-1; the SuperWave sums to about 1.7 with MIX 5 */
    float gain = mode == LZM_SUPR ? 19000.0f : 32000.0f;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    tsvf_t flt;
    if (n > CTL)
        n = CTL;
    lz_setup(o, p);
    o->freq = lz_hz(m->inc) * (mode == LZM_CZ ? 2.0f : 1.0f);
#if FELUCCA_SHELL
    lzs_par sp;
    shape_par(t, &sp);
    if (mode == LZM_DRAW && lzs_draws(&sp)) {            /* UNI, WARP: lzshape's drawer in place of the core's */
        float lv[8];
        for (i = 0; i < 8u; i++)
            lv[i] = o->steps[i].level;
        lzs_draw(lz_shape_of(t, v), &sp, lv, o->order, o->n, o->freq, (float)FS, buf, (int)n);
    } else
#endif
    if (mode >= LZM_PSAW)
        lz_multisaw(o, buf, (int)n);
    else
        lz_wavedrawer(o, buf, (int)n);
#if FELUCCA_SHELL
    if (shape_on(p))                                    /* FOLD SYM CRUSH DOWN, before the filter */
        lzs_shape(lz_shape_of(t, v), &sp, buf, (int)n);
#endif
    tsvf_coef(&flt, (p[P_E6] << 8) + m->cutoff, p[P_E7]);
    for (i = 0; i < n; i++) {
        int32_t s = clamp((int32_t)(buf[i] * gain), -32767, 32767), y, a;
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);          /* as ANALOG: linear to half scale, then a soft knee */
        a = y < 0 ? -y : y;
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS);
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static const preset_t LIZARD_PRESETS[] = {
    /* name, {MODE, SWP, WAVE, DTN, VCS, EDGE, CUT, RES}, {A D S R}, fenv, mono */
    {"LIZARD LEAD", {LZM_DRAW, 0, 0, 32, 3, 1, 100, 20}, {0, 60, 100, 40}, 20, 1, FX(0, 0, 30, 20), PAT(4)},
    {"STAIR BASS", {LZM_DRAW, 0, 24, 32, 3, 1, 60, 30}, {0, 50, 80, 25}, 40, 1, FX(10, 0, 10, 10), PAT(2)},
    {"RAND DRAW", {LZM_DRAW, 3, 0, 32, 3, 1, 90, 20}, {20, 80, 110, 70}, 0, 0, FX(0, 30, 30, 50), PAT(5)},
    {"SWITCHER", {LZM_SWCH, 0, 0, 32, 3, 1, 110, 10}, {0, 60, 100, 40}, 10, 1, FX(0, 0, 35, 25), PAT(4)},
    {"CHOP SINE", {LZM_SWCH, 0, 48, 32, 3, 1, 127, 0}, {2, 70, 100, 50}, 0, 0, FX(0, 20, 40, 40), PAT(3)},
    {"CZ ALT", {LZM_CZ, 0, 0, 32, 3, 1, 100, 10}, {0, 70, 100, 50}, 10, 1, FX(0, 0, 30, 30), PAT(4)},
    {"CZ RAND", {LZM_CZ, 3, 64, 32, 3, 1, 96, 10}, {40, 80, 110, 90}, 0, 0, FX(0, 40, 30, 60), PAT(7)},
    {"PHASED SAW", {LZM_PSAW, 0, 0, 32, 3, 1, 100, 20}, {0, 60, 100, 40}, 20, 1, FX(0, 10, 30, 30), PAT(4)},
    {"SUPER SAW", {LZM_SUPR, 0, 0, 64, 5, 1, 110, 10}, {2, 80, 110, 55}, 10, 0, FX(0, 30, 40, 60), PAT(5)},
    {"RAW LIZARD", {LZM_DRAW, 0, 0, 32, 3, 0, 127, 0}, {0, 60, 100, 40}, 0, 1, FX(0, 0, 20, 20), PAT(4)},
    {"HQ DRAW", {LZM_DRAW, 0, 112, 32, 3, 2, 127, 0}, {2, 70, 110, 60}, 0, 0, FX(0, 20, 30, 40), PAT(6)},
};

static const engine_t ENG_LIZARD = {
    .name = "LIZARD",
    .page_title = {"OSC", "TONE"},
    .edit = {
        {"MODE", F_ENUM, 0, 4, 0, N_LZ_MODE, 0},
        {"SWP", F_ENUM, 0, 3, 0, N_LZ_SWP, 0},
        {"WAVE", F_INT, 0, 127, 0, N_LZ_DRAW, 0},        /* the set, WAVE / 8 (lizard_desc per MODE) */
        {"DTN", F_PCT, 0, 127, 32, 0, 0},                /* PSAW / SUPR */
        {"VCS", F_INT, 2, 5, 3, 0, 0},                   /* PSAW saws; SUPR: MIX (lizard_desc) */
        {"EDGE", F_ENUM, 0, 2, 1, N_LZ_EDGE, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = LIZARD_PRESETS,
    .npresets = NELEM(LIZARD_PRESETS),
    .note_on = lizard_note_on,
    .render = lizard_render,
    .desc = lizard_desc,
    .knob = {P_E2, P_E3, P_E6, P_REL},
    .keep = 0x03,                /* the filter, as ANALOG */
    .mem = sizeof(lz_part_t),
};
