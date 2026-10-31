/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * HARP: an Omnichord's chord buttons and strum plate on the FM-1's keys. The twelve keys F3..E4 pick the chord's
 * root; the fifteen keys F4..G5 are the plate's strings, each a tone of the chord, climbing through the octaves:
 * a finger run across them strums it. Holding a root key can also sound the chord under the plate (PAD), as the
 * chord buttons do. A string keeps the note it started with when the chord changes under it, so nothing hangs.
 *
 *   QUAL  KEY (the root's own chord in SCALE, stacked thirds) MAJ MIN 7 m7 MAJ7 SUS4 DIM
 *   REG   the plate's lowest octave, 1 .. 5 (2: the root from C2; a triad's fifteen strings climb five octaves)
 *   PAD   the held chord's velocity, an octave under the plate's first string, 0 = off
 *   BASS  OFF, or the root an octave below the pad
 *
 * The LINE page's (NE_P_LINE): STRUM, a held root key strums the plate by itself, one string a step (UP, DOWN,
 * UP+DN, RAND; the shell steps it on the part's DIV while the transport runs, in 16ths at the tempo while it does
 * not), over SPAN strings (2..15) from the first. OFF: the plate is played by hand only.
 *
 * The token is unused (NE_NO_TOKEN). */
#include "note_engine.h"

#define HP_NROOT 12
#define HP_NSTR (NE_KEYS - HP_NROOT)
#define HP_VEL 100

typedef struct {
    uint8_t qual, reg, pad, bass;
    uint8_t sroot;
    uint16_t smask;
    int8_t root;                         /* the chord's root pc, -1: none picked yet (SCALE's root) */
    int8_t held;                         /* the root key held, -1: none */
    int8_t str[HP_NSTR];                 /* each string's sounding note, -1: silent */
    int8_t padn[6];                      /* the pad's sounding notes */
    uint8_t npad;
    uint8_t strum, span, scnt;           /* STRUM, SPAN, the strum's count since the root went down */
    int8_t snote;                        /* the strummed note sounding, -1: none */
} hp_t;

static const char *const HP_QUAL_NAMES[] = {"KEY", "MAJ", "MIN", "7", "m7", "MAJ7", "SUS4", "DIM", 0};
static const char *const HP_BASS_NAMES[] = {"OFF", "ON", 0};
static const int8_t HP_IV[8][4] = {{0}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 3, 7, 10}, {0, 4, 7, 11},
                                   {0, 5, 7, -1}, {0, 3, 6, -1}};

static const ne_param_t HP_PARAMS[4] = {
    {"QUAL", NE_ENUM, 0, 7, 0, HP_QUAL_NAMES},
    {"REG", NE_INT, 1, 5, 2, 0},
    {"PAD", NE_PCT, 0, 127, 90, 0},
    {"BASS", NE_ENUM, 0, 1, 0, HP_BASS_NAMES},
};

static int hp_pc(int n) { return ((n % 12) + 12) % 12; }

/* the chord's intervals above its root (ascending, 3 or 4) -> n */
static int hp_chord(const hp_t *h, int root, int iv[4])
{
    int n = 0, i;
    if (h->qual == 0) {                  /* KEY: thirds stacked in SCALE from the root, if the root is in it */
        int rel = hp_pc(root - h->sroot), d = -1, k = 0;
        for (i = 0; i < 12; i++)
            if (h->smask & (1u << i)) {
                if (i == rel)
                    d = k;
                k++;
            }
        if (d >= 0 && k >= 5) {
            int r0 = ne_degree(h->sroot, h->smask, d);
            for (i = 0; i < 3; i++)
                iv[n++] = ne_degree(h->sroot, h->smask, d + 2 * i) - r0;
            return n;
        }
        iv[0] = 0, iv[1] = 4, iv[2] = 7;  /* (outside the scale: major) */
        return 3;
    }
    for (i = 0; i < 4 && HP_IV[h->qual][i] >= 0; i++)
        iv[n++] = HP_IV[h->qual][i];
    return n;
}

static int hp_root(const hp_t *h) { return h->root >= 0 ? h->root : h->sroot; }

static void hp_pad_off(hp_t *h, ne_out_t *o)
{
    uint32_t i;
    for (i = 0; i < h->npad; i++)
        ne_emit(o, 0, h->padn[i], 0, NE_NO_TOKEN);
    h->npad = 0;
}

static void hp_pad_on(hp_t *h, ne_out_t *o)
{
    /* an octave under the plate's first string, so no string shares a note with the pad (a string's off would end
     * the pad's note) */
    int iv[4], n = hp_chord(h, hp_root(h), iv), base = 12 * h->reg + hp_root(h), i;
    if (!h->pad)
        return;
    if (h->bass)
        ne_emit(o, 1, h->padn[h->npad++] = (int8_t)(base - 12), h->pad, NE_NO_TOKEN);
    for (i = 0; i < n; i++)
        ne_emit(o, 1, h->padn[h->npad++] = (int8_t)(base + iv[i]), h->pad, NE_NO_TOKEN);
}

static void hp_init(void *st)
{
    hp_t *h = st;
    int i;
    h->qual = 0;
    h->reg = 2;
    h->pad = 90;                         /* (a root key sounds its chord at once) */
    h->bass = 0;
    h->span = 8;
    h->snote = -1;
    h->sroot = 0;
    h->smask = 0xAB5;                    /* (major until the host says) */
    h->root = -1;
    h->held = -1;
    for (i = 0; i < HP_NSTR; i++)
        h->str[i] = -1;
    h->npad = 0;
}

static void hp_param(void *st, int id, int16_t v)
{
    hp_t *h = st;
    if (id == 0)
        h->qual = (uint8_t)(v < 0 ? 0 : v > 7 ? 7 : v);
    else if (id == 1)
        h->reg = (uint8_t)(v < 1 ? 1 : v > 5 ? 5 : v);
    else if (id == 2)
        h->pad = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    else if (id == 3)
        h->bass = v != 0;
    else if (id == NE_P_LINE)
        h->strum = (uint8_t)(v < 0 ? 0 : v > 4 ? 4 : v);
    else if (id == NE_P_LINE + 1)
        h->span = (uint8_t)(v < 2 ? 2 : v > HP_NSTR ? HP_NSTR : v);
}

static int hp_string(const hp_t *h, int s)  /* string s's note (-1: past 127) */
{
    int iv[4], n = hp_chord(h, hp_root(h), iv), note = 12 * (h->reg + 1) + hp_root(h) + iv[s % n] + 12 * (s / n);
    return note > 127 ? -1 : note;
}

static void hp_strum_off(hp_t *h, ne_out_t *o)
{
    if (h->snote >= 0)
        ne_emit(o, 0, h->snote, 0, NE_NO_TOKEN);
    h->snote = -1;
}

static void hp_scale(void *st, uint8_t root, uint16_t mask)
{
    hp_t *h = st;
    h->sroot = root % 12u;
    mask &= 0xFFFu;
    h->smask = mask && mask != 0xFFFu ? mask : 0xAB5u;   /* CHR: no key of its own, the major scale of ROOT, as
                                                         * CHORD's (thirds stacked on twelve notes are clusters) */
}

static void hp_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    hp_t *h = st;
    if (key >= NE_KEYS)
        return;
    if (key < HP_NROOT) {                /* a chord button */
        if (down) {
            h->root = (int8_t)hp_pc(NE_KEY_F3 + key);
            h->held = (int8_t)key;
            h->scnt = 0;                 /* (a new chord: the strum from its start) */
            hp_pad_off(h, o);
            hp_pad_on(h, o);
        } else if (h->held == (int8_t)key) {
            h->held = -1;
            hp_pad_off(h, o);
            hp_strum_off(h, o);
        }
        return;
    }
    {
        int s = key - HP_NROOT, iv[4], n, note;
        if (!down) {
            if (h->str[s] >= 0)
                ne_emit(o, 0, h->str[s], 0, NE_NO_TOKEN);
            h->str[s] = -1;
            return;
        }
        if (h->str[s] >= 0)              /* (a string struck again: its old note ends first) */
            ne_emit(o, 0, h->str[s], 0, NE_NO_TOKEN);
        (void)iv, (void)n;
        note = hp_string(h, s);
        if (note < 0) {
            h->str[s] = -1;
            return;
        }
        h->str[s] = (int8_t)note;
        ne_emit(o, 1, note, HP_VEL, NE_NO_TOKEN);
    }
}

static void hp_step(void *st, uint32_t idx, ne_out_t *o)
{
    hp_t *h = st;
    int span = h->span, k = h->scnt, s, note;
    (void)idx;
    hp_strum_off(h, o);
    if (!h->strum || h->held < 0)        /* (STRUM OFF: the plate is played by hand, the ARP after it clocks it) */
        return;
    switch (h->strum) {
    case 1: s = k % span; break;
    case 2: s = span - 1 - k % span; break;
    case 3: s = k % (2 * span - 2); s = s < span ? s : 2 * span - 2 - s; break;
    default: s = (int)(((uint32_t)k * 2654435761u >> 16) % (uint32_t)span); break;
    }
    h->scnt++;
    if ((note = hp_string(h, s)) < 0 || h->str[s] == note)
        return;                          /* (a string held by hand rings on its own) */
    h->snote = (int8_t)note;
    ne_emit(o, 1, note, HP_VEL, NE_NO_TOKEN);
}

static void hp_stop(void *st, ne_out_t *o)
{
    hp_t *h = st;
    int i;
    for (i = 0; i < HP_NSTR; i++)
        if (h->str[i] >= 0) {
            ne_emit(o, 0, h->str[i], 0, NE_NO_TOKEN);
            h->str[i] = -1;
        }
    hp_pad_off(h, o);
    hp_strum_off(h, o);
    h->held = -1;
}

const note_engine_t NE_HARP = {"HARP", sizeof(hp_t), HP_PARAMS, hp_init, hp_param, hp_scale, hp_key, hp_step, hp_stop};
