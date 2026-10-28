/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * CANON: an analogue shift register for the keys, after Ornament & Crime's CopierMaschine (Patrick Dowling, Tim
 * Churches, Max Stadler, MIT); written fresh for the FM-1's note-engine API.
 *
 * What you play sounds as you play it and goes into a ring of 64 steps, one note a step (the last key down; a key
 * still held ties on). VOX voices answer it, voice v DELAY * v steps later and DEG * v degrees of the shared SCALE
 * away, so DEG 2 stacks thirds and DEG 0 is a round. HOLD stops the writing and loops the last LEN steps (the
 * part's): the ring plays itself, its answers behind it, and the keys play over it ("play, then let it play
 * behind").
 *
 *   DELAY 1..16 steps between voices
 *   VOX   1..3 answering voices
 *   DEG   -7..+7 scale degrees between voices
 *   HOLD  OFF, or LEN: the part's LEN of steps looped */
#include "note_engine.h"

#define CANON_RING 64u
#define CANON_VOX 3

typedef struct {
    uint8_t delay, vox, hold, len;      /* hold: the steps looped (0: writing), len: the part's LEN */
    int8_t deg;
    uint8_t root;
    uint16_t mask;
    uint8_t ring[CANON_RING];            /* each step's note + 1, 0 = none */
    uint8_t tie[CANON_RING / 8u];        /* bit: the step goes on with the note before it */
    uint8_t w;                           /* the step written last */
    uint8_t h, rp;                       /* HOLD: the step it took hold at, the loop's place */
    int16_t live;                        /* the key held now (a MIDI note), -1 = none */
    int16_t sounding[1 + CANON_VOX];     /* voice 0 the ring's own (HOLD), 1.. the answers; -1 = none */
} canon_t;

static void canon_init(void *st)
{
    canon_t *c = (canon_t *)st;
    int v;
    c->delay = 4;
    c->len = 16;
    c->vox = 1;
    c->deg = 2;
    c->mask = 0xAB5;                     /* major */
    c->live = -1;
    for (v = 0; v <= CANON_VOX; v++)
        c->sounding[v] = -1;
}

static void canon_param(void *st, int id, int16_t v)
{
    canon_t *c = (canon_t *)st;
    switch (id) {
    case 0: c->delay = (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v); break;
    case 1: c->vox = (uint8_t)(v < 1 ? 1 : v > CANON_VOX ? CANON_VOX : v); break;
    case 2: c->deg = (int8_t)(v < -7 ? -7 : v > 7 ? 7 : v); break;
    case 3: {
        uint8_t h = v > 0 ? c->len : 0u;
        if (h && !c->hold) {             /* taking hold: the loop ends at the step just written */
            c->h = (uint8_t)((c->w + 1u) % CANON_RING);
            c->rp = (uint8_t)(h - 1u);
        }
        c->hold = h;
        if (h)
            c->rp %= h;
        break;
    }
    case NE_P_LEN:                       /* holding: the loop is the new LEN from where it is */
        c->len = (uint8_t)(v < 1 ? 1 : v > (int16_t)CANON_RING ? CANON_RING : v);
        if (c->hold) {
            c->hold = c->len;
            c->rp %= c->hold;
        }
        break;
    }
}

static void canon_scale(void *st, uint8_t root, uint16_t mask)
{
    canon_t *c = (canon_t *)st;
    c->root = (uint8_t)(root % 12u);
    c->mask = mask & 0xFFFu ? mask & 0xFFFu : 0xFFFu;
}

static void canon_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    canon_t *c = (canon_t *)st;
    int n = NE_KEY_F3 + key;
    if (key >= NE_KEYS)
        return;
    if (down) {
        if (c->live >= 0)
            ne_emit(o, 0, c->live, 0, 0);
        ne_emit(o, 1, n, 104, NE_NO_TOKEN);
        c->live = (int16_t)n;
        if (!c->hold) {                  /* into the step playing now */
            c->ring[c->w] = (uint8_t)(n + 1);
            c->tie[c->w / 8u] &= (uint8_t)~(1u << (c->w % 8u));
        }
    } else if (n == c->live) {
        ne_emit(o, 0, n, 0, 0);
        c->live = -1;
    }
}

/* ring step i's note, moved d degrees in the scale; tie: it goes on from the step before */
static int canon_note(const canon_t *c, uint32_t i, int d, int *tie)
{
    int base, deg, n = c->ring[i % CANON_RING] - 1;
    *tie = (c->tie[(i % CANON_RING) / 8u] >> (i % 8u)) & 1;
    if (n < 0)
        return -1;
    if (!d)
        return n;
    ne_anchor(c->root, c->mask, n, &base, &deg);
    n = ne_degree(base, c->mask, deg + d);
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

static void canon_voice(canon_t *c, int v, uint32_t i, ne_out_t *o)
{
    int tie, n = canon_note(c, i, c->deg * v, &tie);
    if (tie && n == c->sounding[v])
        return;                          /* (held on) */
    if (c->sounding[v] >= 0)
        ne_emit(o, 0, c->sounding[v], 0, 0);
    c->sounding[v] = (int16_t)n;
    if (n >= 0)
        ne_emit(o, 1, n, v ? 96 - 8 * v : 104, NE_NO_TOKEN);
}

static void canon_step(void *st, uint32_t idx, ne_out_t *o)
{
    canon_t *c = (canon_t *)st;
    int v;
    (void)idx;
    if (c->hold) {                       /* the loop: the ring's last `hold` steps before h, round */
        uint32_t start = c->h + CANON_RING - c->hold;
        c->rp = (uint8_t)((c->rp + 1u) % c->hold);
        canon_voice(c, 0, start + c->rp, o);
        for (v = 1; v <= CANON_VOX; v++) {
            if (v <= c->vox)
                canon_voice(c, v, start + (c->rp + (uint32_t)c->hold * 16u - (uint32_t)(c->delay * v)) % c->hold, o);
            else if (c->sounding[v] >= 0) {
                ne_emit(o, 0, c->sounding[v], 0, 0);
                c->sounding[v] = -1;
            }
        }
        return;
    }
    if (c->sounding[0] >= 0) {           /* (the loop's own voice, let go) */
        ne_emit(o, 0, c->sounding[0], 0, 0);
        c->sounding[0] = -1;
    }
    c->w = (uint8_t)((c->w + 1u) % CANON_RING);
    c->ring[c->w] = (uint8_t)(c->live + 1);
    if (c->live >= 0)
        c->tie[c->w / 8u] |= (uint8_t)(1u << (c->w % 8u));
    else
        c->tie[c->w / 8u] &= (uint8_t)~(1u << (c->w % 8u));
    for (v = 1; v <= CANON_VOX; v++) {
        if (v <= c->vox)
            canon_voice(c, v, c->w + CANON_RING * 2u - (uint32_t)(c->delay * v), o);
        else if (c->sounding[v] >= 0) {
            ne_emit(o, 0, c->sounding[v], 0, 0);
            c->sounding[v] = -1;
        }
    }
}

static void canon_stop(void *st, ne_out_t *o)
{
    canon_t *c = (canon_t *)st;
    int v;
    for (v = 0; v <= CANON_VOX; v++) {
        if (c->sounding[v] >= 0)
            ne_emit(o, 0, c->sounding[v], 0, 0);
        c->sounding[v] = -1;
    }
    if (c->live >= 0)
        ne_emit(o, 0, c->live, 0, 0);
    c->live = -1;
}

static const char *const CANON_HOLD_NAMES[] = {"OFF", "LEN"};
static const ne_param_t CANON_PARAMS[4] = {
    {"DELAY", NE_INT, 1, 16, 4, 0},
    {"VOX", NE_INT, 1, CANON_VOX, 1, 0},
    {"DEG", NE_INT, -7, 7, 2, 0},
    {"HOLD", NE_ENUM, 0, 1, 0, CANON_HOLD_NAMES},
};

typedef char canon_fits[sizeof(canon_t) <= NE_MEM_MAX ? 1 : -1];

const note_engine_t NE_CANON = {
    "CANON", sizeof(canon_t), CANON_PARAMS, canon_init, canon_param, canon_scale, canon_key, canon_step, canon_stop,
};
