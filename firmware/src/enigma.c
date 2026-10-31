/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * ENIGMA: a Turing Machine, the looping random sequencer of Tom Whitwell's Music Thing module, as a
 * note engine. The idea as Hemisphere Suite's TM applet and ENIGMA app play it (Jason Justian, MIT);
 * written fresh here for the FM-1's note-engine API.
 *
 * A shift register turns once a step. The bit that falls off the loop's end (bit LEN-1, the part's
 * LEN) comes back in at bit 0, flipped with chance CHNC: at 0 % the loop repeats every LEN steps
 * forever, at 50 % it is new every time, in between it drifts. The low byte is the pitch: a degree
 * of the shared SCALE over RANGE octaves, counted up from the key last played (which transposes
 * along the scale). The high byte is the gate: a step sounds when it is under DENS, so the rests
 * loop with the notes. The low byte also goes out as the token (no sound reads it yet: note_engine.h).
 *
 *   LEN   the part's LEN (1..64), the steps in the loop: the same knob as the SEQ page's
 *   CHNC  0..50 %: how often a bit flips coming round (the Thonk knob's left half; its right half
 *         is the same loop doubled and inverted, which a flip rate past 50 % is)
 *   RANGE 1..4 octaves
 *   DENS  0..100 %: how many steps sound */
#include "note_engine.h"

typedef struct {
    uint32_t reg, hi;                    /* the register, bits 0..31 and 32..63 (4-byte state: no 64-bit word) */
    uint8_t len, chance, range, dens;
    uint8_t root;                        /* the scale's root, 0..11 */
    uint16_t mask;
    int16_t key;                         /* the last key played (a MIDI note), the transposition */
    int16_t sounding;                    /* the note on now, -1 = none */
    uint32_t rng;
} eg_t;

static uint32_t eg_rand(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

static void eg_init(void *st)
{
    eg_t *e = (eg_t *)st;
    e->rng = 0x6D2B79F5u;
    e->reg = eg_rand(&e->rng) | 1u;
    e->hi = 0;
    e->len = 16;
    e->chance = 4;
    e->range = 2;
    e->dens = 75;
    e->root = 0;
    e->mask = 0xAB5;                     /* major: C D E F G A B */
    e->key = 60;
    e->sounding = -1;
}

static void eg_param(void *st, int id, int16_t v)
{
    eg_t *e = (eg_t *)st;
    switch (id) {
    case NE_P_LEN: e->len = (uint8_t)(v < 1 ? 1 : v > 64 ? 64 : v); break;
    case 1: e->chance = (uint8_t)(v < 0 ? 0 : v > 50 ? 50 : v); break;
    case 2: e->range = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v); break;
    case 3: e->dens = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    }
}

static void eg_scale(void *st, uint8_t root, uint16_t mask)
{
    eg_t *e = (eg_t *)st;
    e->root = (uint8_t)(root % 12u);
    e->mask = mask & 0xFFFu ? mask & 0xFFFu : 0xFFFu;
}

static int eg_count(uint16_t mask)
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        n += (mask >> i) & 1;
    return n;
}

static void eg_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    eg_t *e = (eg_t *)st;
    (void)o;
    if (down && key < NE_KEYS)
        e->key = (int16_t)(NE_KEY_F3 + key);     /* the next step plays from here: no sound of its own */
}

static void eg_step(void *st, uint32_t idx, ne_out_t *o)
{
    eg_t *e = (eg_t *)st;
    unsigned bit = (e->len <= 32u ? e->reg >> (e->len - 1u) : e->hi >> (e->len - 33u)) & 1u;
    int base, deg, n, note, lo;
    if (eg_rand(&e->rng) % 100u < e->chance)
        bit ^= 1u;
    e->hi = (e->hi << 1) | (e->reg >> 31);
    e->reg = (e->reg << 1) | bit;
    if (e->sounding >= 0)
        ne_emit(o, 0, e->sounding, 0, 0);
    e->sounding = -1;
    if (((e->reg >> 8) & 0xFFu) * 100u >= (unsigned)e->dens * 256u)
        return;                          /* a rest, which loops with the rest of the pattern */
    lo = e->reg & 0xFF;
    n = eg_count(e->mask) * e->range;
    ne_anchor(e->root, e->mask, e->key, &base, &deg);
    note = ne_degree(base, e->mask, deg + (lo * (n + 1)) / 256);
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    ne_emit(o, 1, note, idx % 4u == 0u ? 112 : 96, lo < 255 ? lo : 254);
    e->sounding = (int16_t)note;
}

static void eg_stop(void *st, ne_out_t *o)
{
    eg_t *e = (eg_t *)st;
    if (e->sounding >= 0)
        ne_emit(o, 0, e->sounding, 0, 0);
    e->sounding = -1;
}

static const char *const EG_RANGE_NAMES[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
static const ne_param_t EG_PARAMS[4] = {
    {"LEN", NE_LEN, 1, 64, 16, 0},
    {"CHNC", NE_PCT, 0, 50, 4, 0},
    {"RANGE", NE_ENUM, 1, 4, 2, EG_RANGE_NAMES},
    {"DENS", NE_PCT, 0, 100, 75, 0},
};

typedef char eg_fits[sizeof(eg_t) <= NE_MEM_MAX ? 1 : -1];

const note_engine_t NE_ENIGMA = {
    "ENIGMA", sizeof(eg_t), EG_PARAMS, eg_init, eg_param, eg_scale, eg_key, eg_step, eg_stop,
};
