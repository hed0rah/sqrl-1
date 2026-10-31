/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * ORGAN: a tonewheel organ's drawbars, for SQRL-1 (organ.c). Nine drawbars in eight knobs: the
 * console's first six, then TOP for its three highest together (1 3/5', 1 1/3', 1'), then PERC.
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs organ.c, organ.h and
 * organ_table.h next to it. State: the part's tables and an org_voice_t per voice in ENG_MEM.
 *
 *   16'  5-1/3  8'  4'      an octave under the note, a fifth over it, the note, an octave over it (0 .. 8, 3 dB a
 *                           step)
 *   2-2/3  2'  TOP  PERC    the twelfth, two octaves, the mixture; percussion OFF 2ND 3RD 2ND S 3RD S */
#include "organ.c"

typedef struct {
    org_tables_t tb;
    org_voice_t v[NVOICE];
} org_part_t;
ENG_MEM_FITS("ORGAN", sizeof(org_part_t));

static org_part_t *org_of(const track_t *t) { return (org_part_t *)ENG_MEM((uint32_t)(t - trk) % NPART); }

/* once a block per part: the tables follow the drawbars */
static void org_block(track_t *t)
{
    const int16_t *p = t->p;
    uint8_t b[ORG_NBARS];
    uint32_t i;
    for (i = 0; i < 6u; i++)
        b[i] = (uint8_t)clamp(p[P_E0 + i], 0, 8);
    b[6] = b[7] = b[8] = (uint8_t)clamp(p[P_E6], 0, 8);
    org_tables_set(&org_of(t)->tb, b);
}

static void org_note_on(track_t *t, voice_t *v)
{
    uint32_t i, alone = 1, vi = (uint32_t)(v - t->v) % NVOICE;
    for (i = 0; i < NVOICE; i++)         /* single trigger: the percussion only on a note struck alone */
        if (&t->v[i] != v && t->v[i].active && t->v[i].gate)
            alone = 0;
    org_on(&org_of(t)->v[vi], alone && t->p[P_E7] != ORG_POFF, v->env != 0);
}

static __attribute__((noinline)) void org_render_v(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static int32_t buf[64];
    org_part_t *o = org_of(t);
    uint32_t i, j, vi = (uint32_t)(v - t->v) % NVOICE;
    for (i = 0; i < n; i += 64u) {
        uint32_t k = n - i < 64u ? n - i : 64u;
        for (j = 0; j < k; j++)
            buf[j] = 0;
        org_render(&o->tb, &o->v[vi], m->inc, (uint8_t)clamp(t->p[P_E7], 0, ORG_NPERC - 1), buf, k);
        for (j = 0; j < k; j++)
            out[i + j] += voice_amp(buf[j], m, i + j) << 1;
    }
}

static const char *const N_ORG_PERC[] = {"OFF", "2ND", "3RD", "2ND S", "3RD S"};

static const preset_t ORG_PRESETS[] = {
    /* 16' 5-1/3 8' 4' | 2-2/3 2' TOP PERC */
    {"JIMMY", {8, 8, 8, 0, 0, 0, 0, ORG_P3}, {0, 0, 127, 8}, 0, 0, FX(0, 30, 0, 25), PAT(0)},
    {"GOSPEL", {8, 8, 8, 8, 0, 0, 0, 0}, {0, 0, 127, 10}, 0, 0, FX(0, 40, 0, 35), PAT(0)},
    {"FULL ORGAN", {8, 8, 8, 8, 8, 8, 8, 0}, {0, 0, 127, 12}, 0, 0, FX(0, 30, 0, 40), PAT(0)},
    {"BOOKER", {8, 8, 6, 0, 0, 0, 0, 0}, {0, 0, 127, 8}, 0, 0, FX(0, 30, 0, 20), PAT(0)},
    {"FLUTE", {0, 0, 8, 0, 0, 0, 0, 0}, {0, 0, 127, 14}, 0, 0, FX(0, 20, 10, 40), PAT(0)},
    {"MELLOW", {0, 0, 8, 4, 0, 0, 0, ORG_P2S}, {0, 0, 127, 14}, 0, 0, FX(0, 40, 0, 45), PAT(0)},
    {"THEATRE", {8, 0, 8, 8, 0, 8, 8, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 50, 0, 50), PAT(0)},
    {"PIPES", {6, 0, 8, 6, 4, 6, 3, 0}, {10, 0, 127, 40}, 0, 0, FX(0, 0, 0, 70), PAT(0)},
    {"CLICKY", {0, 0, 8, 8, 0, 0, 0, ORG_P3}, {0, 0, 127, 6}, 0, 0, FX(0, 20, 0, 15), PAT(0)},
};

static const engine_t ENG_ORGAN = {
    .name = "ORGAN",
    .page_title = {"BARS LOW", "BARS HIGH"},
    .edit = {
        {"16'", F_INT, 0, 8, 8, 0, 0},
        {"5-1/3", F_INT, 0, 8, 8, 0, 0},
        {"8'", F_INT, 0, 8, 8, 0, 0},
        {"4'", F_INT, 0, 8, 0, 0, 0},
        {"2-2/3", F_INT, 0, 8, 0, 0, 0},
        {"2'", F_INT, 0, 8, 0, 0, 0},
        {"TOP", F_INT, 0, 8, 0, 0, 0},
        {"PERC", F_ENUM, 0, ORG_NPERC - 1, ORG_P3, N_ORG_PERC, 0},
    },
    .presets = ORG_PRESETS,
    .npresets = NELEM(ORG_PRESETS),
    .note_on = org_note_on,
    .render = org_render_v,
    .block = org_block,
    .knob = {P_E0, P_E2, P_E3, P_E7},
    .mem = sizeof(org_part_t),
};
