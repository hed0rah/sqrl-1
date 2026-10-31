/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * SHOCK: a sine steepened into a shock wave, the way a loud note steepens down a brass bore, for
 * Felucca 1.5.6 / SQRL-1 (shock.c; the physics and its reference model are databurn's phase-distortion research).
 * sigma, the distortion, is the whole timbre: 0 a sine, 1 the shock forming, past it a sawtooth that
 * decays like brass. As on a horn, the breath (the envelope) and the pitch drive it: brass brightens
 * as it gets louder and as it goes up (the opposite of a CZ's key follow).
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs shock.c and
 * shock_table.h next to it. State: a shock_t per voice in ENG_MEM.
 *
 *   DRIVE  sigma at the envelope's top: 0 .. MAXS
 *   ENV    how much of sigma the envelope gives: 0 a fixed timbre (a CZ preset), 127 all of it
 *   KEYF   key follow: 0 none, 127 the physics (sigma proportional to pitch, about middle C)
 *   LOSS   the bore's damping: a low-pass that follows the note, open at 0
 *   SUB    a sine an octave down
 *   BRTH   breath noise through the same bore
 *   EDGE   RAW, the shock unfiltered (aliasing), or BLEP
 *   MAXS   the ceiling on sigma: 0.5 .. 3 */
#include "shock.c"

typedef struct {
    shock_t v[NVOICE];
} shk_part_t;
ENG_MEM_FITS("SHOCK", sizeof(shk_part_t));

static shock_t *shk_of(const track_t *t, const voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk) % NPART, vi = (uint32_t)(v - t->v) % NVOICE;
    return &((shk_part_t *)ENG_MEM(p))->v[vi];
}

/* 2^x for x in -8 .. 8: the integer part by shifting, the fraction by a cubic (0.1 % at worst) */
static float shk_exp2(float x)
{
    int32_t i;
    float f, r;
    x = x < -8.0f ? -8.0f : x > 8.0f ? 8.0f : x;
    i = (int32_t)(x + 8.0f) - 8;
    f = x - (float)i;
    r = 1.0f + f * (0.6951786f + f * (0.2261684f + f * 0.0781633f));
    return i >= 0 ? r * (float)(1 << i) : r / (float)(1 << -i);
}

static void shk_note_on(track_t *t, voice_t *v)
{
    shock_t *s = shk_of(t, v);
    if (v->env)                          /* a retrigger keeps the phase, as LIZARD */
        return;
    shock_init(s, 0x9E3779B9u ^ (uint32_t)(v - t->v) ^ ((uint32_t)(t - trk) << 8));   /* (each part and voice its own noise) */
    shock_note(s);
}

static __attribute__((noinline)) void shk_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    shock_t *s = shk_of(t, v);
    shock_par sp;
    float buf[64], f0 = (float)m->inc * ((float)FS / 4294967296.0f), maxs, sg, env, mult, fc;
    uint32_t i, j;
    /* sigma: DRIVE under the envelope, times the key follow, under the ceiling */
    maxs = 0.5f + 2.5f * (float)p[P_E7] * (1.0f / 127.0f);
    env = (float)m->envq15 * (1.0f / 32767.0f);
    sg = 3.0f * (float)p[P_E0] * (1.0f / 127.0f);
    sg *= 1.0f - (float)p[P_E1] * (1.0f / 127.0f) * (1.0f - env);
    sg *= shk_exp2(((float)m->pitch16 * (1.0f / 16.0f) - 60.0f) * (1.0f / 12.0f) * (float)p[P_E2] * (1.0f / 127.0f));
    sp.sigma = sg > maxs ? maxs : sg;
    /* LOSS: the low-pass at 64 harmonics open, down to 1.5 shut, as a fraction of the sample rate */
    mult = 1.5f * shk_exp2(5.415f * (1.0f - (float)p[P_E3] * (1.0f / 127.0f)));
    fc = f0 * mult * (6.2831853f / (float)FS);
    sp.loss = fc > 1.0f ? 1.0f : fc;
    sp.sub = (float)p[P_E4] * (1.0f / 127.0f);
    sp.breath = (float)p[P_E5] * (0.5f / 127.0f);
    sp.blep = p[P_E6] != 0;
    for (i = 0; i < n; i += 64u) {
        uint32_t k = n - i < 64u ? n - i : 64u;
        shock_render(s, &sp, m->inc, buf, (int)k);
        for (j = 0; j < k; j++) {        /* +-1 with room for SUB: about half scale, << 1 as VOICE */
            float y = buf[j] * 11000.0f;
            out[i + j] += voice_amp((int32_t)(y > 32767.0f ? 32767.0f : y < -32767.0f ? -32767.0f : y), m, i + j) << 1;
        }
    }
}

static const char *const N_SHK_EDGE[] = {"RAW", "BLEP"};

static const preset_t SHK_PRESETS[] = {
    /* DRIVE ENV KEYF LOSS | SUB BRTH EDGE MAXS */
    {"BRASS", {80, 110, 100, 40, 0, 6, 1, 127}, {6, 90, 100, 60}, 0, 0, FX(0, 10, 10, 30), PAT(0)},
    {"CZ SAW", {64, 0, 0, 0, 0, 0, 1, 127}, {0, 80, 120, 30}, 0, 0, FX(0, 20, 15, 20), PAT(0)},
    {"BLOOM", {9, 0, 0, 20, 0, 0, 1, 127}, {20, 90, 110, 80}, 0, 0, FX(0, 30, 20, 60), PAT(0)},
    {"SHOCK BASS", {70, 120, 0, 60, 60, 0, 1, 127}, {0, 60, 60, 20}, 0, 1, FX(10, 0, 0, 10), PAT(0)},
    {"HORN SECTION", {60, 100, 127, 50, 0, 10, 1, 100}, {10, 90, 110, 70}, 0, 0, FX(0, 40, 20, 40), PAT(0)},
    {"TUBA", {50, 110, 127, 70, 80, 8, 1, 90}, {8, 80, 100, 50}, 0, 1, FX(0, 0, 10, 20), PAT(0)},
    {"BREATH", {20, 80, 60, 50, 0, 70, 1, 60}, {30, 100, 100, 90}, 0, 0, FX(0, 20, 30, 60), PAT(0)},
    {"RAW SHOCK", {110, 60, 0, 0, 0, 0, 0, 127}, {0, 60, 100, 30}, 0, 0, FX(20, 0, 20, 10), PAT(0)},
};

static const engine_t ENG_SHOCK = {
    .name = "SHOCK",
    .page_title = {"SHOCK", "AIR"},
    .edit = {
        {"DRIVE", F_PCT, 0, 127, 80, 0, 0},
        {"ENV", F_PCT, 0, 127, 110, 0, 0},
        {"KEYF", F_PCT, 0, 127, 100, 0, 0},
        {"LOSS", F_PCT, 0, 127, 40, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"BRTH", F_PCT, 0, 127, 6, 0, 0},
        {"EDGE", F_ENUM, 0, 1, 1, N_SHK_EDGE, 0},
        {"MAXS", F_PCT, 0, 127, 127, 0, 0},
    },
    .presets = SHK_PRESETS,
    .npresets = NELEM(SHK_PRESETS),
    .note_on = shk_note_on,
    .render = shk_render,
    .knob = {P_E0, P_E1, P_E3, P_E4},
    .mem = sizeof(shk_part_t),
};
