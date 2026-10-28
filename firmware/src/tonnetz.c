/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * TONNETZ: a triad walking Euler's Tonnetz by the three neo-Riemannian moves, each of which keeps
 * two notes and moves one by a step or two, so any walk voice-leads smoothly. After the idea of
 * Ornament & Crime's Automatonnetz and Harrington 1200 (Patrick Dowling, Tim Churches, MIT);
 * written fresh for the FM-1's note-engine API.
 *
 *   P  parallel: C major <-> C minor (the third moves a semitone)
 *   R  relative: C major <-> A minor (the fifth up a whole tone / the root down one)
 *   L  leading-tone exchange: C major <-> E minor (the root down a semitone / the fifth up one)
 *
 * The walk ignores SCALE on purpose: it is chromatic, and that is its sound. The last key played
 * sets the triad's root (major) and register.
 *
 *   MOVE  the moves in turn: P L R, L R, P L, P R, R, L, P, or RAND
 *   EVERY 1..16 steps between moves
 *   PLAY  CHORD (the triad on each move), or one note a step: UP, DOWN, RAND
 *   VOIC  the voicing: CLOSE, OPEN (the root an octave down), WIDE (root down, top up), then CHORD's own by pitch:
 *         INV1 (the lowest up an octave), INV2 (the lowest two up), DROP2 (the middle down), VAR (each chord its own)
 *
 * It walks on for as long as it plays: the part's LEN is only the length FREEZE takes of it.
 *
 * The token is the triad (root * 2 + minor, 0..23), for a speaking sound to give each chord its own word (not
 * read yet: note_engine.h). */
#include "note_engine.h"

typedef struct {
    int8_t root, minor;                  /* the triad: root 0..11 */
    int8_t voice[3];                     /* the voices' notes, offsets from base, voice-led */
    int16_t base;                        /* the register: the last key's octave */
    uint8_t move, every, play, spread;
    uint8_t pos;                         /* the next move in the pattern */
    uint8_t arp;                         /* the next voice for UP / DOWN */
    uint16_t since;                      /* steps since the last move */
    uint8_t fresh;                       /* the triad is new: CHORD plays it on this step */
    uint8_t var;                         /* VAR: this chord's voicing (0..5), drawn as the triad changes */
    int16_t sounding[3];
    uint32_t rng;
} tz_t;

enum { TZ_P, TZ_L, TZ_R };
static const char *const TZ_MOVE_NAMES[] = {"P L R", "L R", "P L", "P R", "R", "L", "P", "RAND", 0};
static const uint8_t TZ_PATTERN[7][4] = {{TZ_P, TZ_L, TZ_R, 3}, {TZ_L, TZ_R, 0, 2}, {TZ_P, TZ_L, 0, 2},
                                         {TZ_P, TZ_R, 0, 2}, {TZ_R, 0, 0, 1}, {TZ_L, 0, 0, 1}, {TZ_P, 0, 0, 1}};
static const char *const TZ_PLAY_NAMES[] = {"CHORD", "UP", "DOWN", "RAND", 0};
static const char *const TZ_SPREAD_NAMES[] = {"CLOSE", "OPEN", "WIDE", "INV1", "INV2", "DROP2", "VAR", 0};   /* (0..2 as
                                                         * before: a saved VOIC sounds the same) */
enum { TZ_CLOSE, TZ_OPEN, TZ_WIDE, TZ_INV1, TZ_INV2, TZ_DROP2, TZ_VAR };

static uint32_t tz_rand(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

static int tz_pc(int n) { return ((n % 12) + 12) % 12; }

/* the triad's three pitch classes */
static void tz_pcs(const tz_t *t, int pc[3])
{
    pc[0] = t->root;
    pc[1] = tz_pc(t->root + (t->minor ? 3 : 4));
    pc[2] = tz_pc(t->root + 7);
}

/* each voice to the nearest note of the new triad, no two on one pitch class: the walk's moves change
 * one pitch class at most, so the voices move by a tone at most */
static void tz_lead(tz_t *t)
{
    int pc[3], used[3] = {0, 0, 0}, v, j;
    tz_pcs(t, pc);
    for (v = 0; v < 3; v++) {
        int best = -1, bd = 99, bn = 0;
        for (j = 0; j < 3; j++) {
            int d, n;
            if (used[j])
                continue;
            d = tz_pc(pc[j] - t->voice[v]);       /* up 0..11, or down 12 - d */
            n = d <= 6 ? t->voice[v] + d : t->voice[v] - (12 - d);
            d = d <= 6 ? d : 12 - d;
            if (d < bd) {
                bd = d;
                best = j;
                bn = n;
            }
        }
        used[best] = 1;
        t->voice[v] = (int8_t)bn;
    }
    /* a long walk round the circle drifts: when the chord's middle leaves the register, its outer voice
     * goes an octave the other way (a new inversion, as a player would revoice) */
    j = t->voice[0] + t->voice[1] + t->voice[2];
    if (j > 45 || j < -9) {
        int x = 0;
        for (v = 1; v < 3; v++)
            if (j > 45 ? t->voice[v] > t->voice[x] : t->voice[v] < t->voice[x])
                x = v;
        t->voice[x] = (int8_t)(t->voice[x] + (j > 45 ? -12 : 12));
    }
    if (t->spread == TZ_VAR)                             /* (drawn only under VAR: RAND walks as before) */
        t->var = (uint8_t)(tz_rand(&t->rng) % 6u);
}

static void tz_set(tz_t *t, int root, int minor)
{
    int pc[3], v;
    t->root = (int8_t)tz_pc(root);
    t->minor = (int8_t)(minor != 0);
    tz_pcs(t, pc);
    for (v = 0; v < 3; v++)              /* close position from the root, as a fresh start */
        t->voice[v] = (int8_t)(pc[0] + tz_pc(pc[v] - pc[0]));
    if (t->spread == TZ_VAR)                             /* (drawn only under VAR: RAND walks as before) */
        t->var = (uint8_t)(tz_rand(&t->rng) % 6u);
    t->fresh = 1;
}

static void tz_apply(tz_t *t, int m)
{
    if (m == TZ_P)
        t->minor ^= 1;
    else if (m == TZ_R) {
        t->root = (int8_t)tz_pc(t->root + (t->minor ? 3 : 9));
        t->minor ^= 1;
    } else {
        t->root = (int8_t)tz_pc(t->root + (t->minor ? 8 : 4));
        t->minor ^= 1;
    }
    tz_lead(t);
    t->fresh = 1;
}

static void tz_init(void *st)
{
    tz_t *t = (tz_t *)st;
    int v;
    t->rng = 0x9E3779B9u;
    t->base = 48;
    t->move = 0;
    t->every = 4;
    t->play = 0;
    t->spread = 1;
    for (v = 0; v < 3; v++)
        t->sounding[v] = -1;
    tz_set(t, 0, 0);
}

static void tz_param(void *st, int id, int16_t v)
{
    tz_t *t = (tz_t *)st;
    switch (id) {
    case 0: t->move = (uint8_t)(v < 0 ? 0 : v > 7 ? 7 : v); t->pos = 0; break;
    case 1: t->every = (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v); break;
    case 2: t->play = (uint8_t)(v < 0 ? 0 : v > 3 ? 3 : v); t->fresh = 1; break;   /* (the next step ends the last note) */
    case 3: t->spread = (uint8_t)(v < 0 ? 0 : v > TZ_VAR ? TZ_VAR : v); break;
    }
}

static void tz_scale(void *st, uint8_t root, uint16_t mask)
{
    (void)st;                            /* chromatic on purpose (see above) */
    (void)root;
    (void)mask;
}

static int tz_note(const tz_t *t, int v)
{
    int n = t->base + t->voice[v], s = t->spread == TZ_VAR ? t->var : t->spread, rk = 0, k;
    for (k = 0; k < 3; k++)                              /* its place by pitch: 0 the lowest */
        rk += t->voice[k] < t->voice[v] || (t->voice[k] == t->voice[v] && k < v);
    if ((s == TZ_OPEN || s == TZ_WIDE) && v == 0)
        n -= 12;
    if (s == TZ_WIDE && v == 2)
        n += 12;
    if ((s == TZ_INV1 && rk == 0) || (s == TZ_INV2 && rk < 2))
        n += 12;
    if (s == TZ_DROP2 && rk == 1)
        n -= 12;
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

static void tz_off(tz_t *t, ne_out_t *o)
{
    int v;
    for (v = 0; v < 3; v++)
        if (t->sounding[v] >= 0) {
            ne_emit(o, 0, t->sounding[v], 0, 0);
            t->sounding[v] = -1;
        }
}

static void tz_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    tz_t *t = (tz_t *)st;
    int n = NE_KEY_F3 + key;
    (void)o;
    if (!down || key >= NE_KEYS)
        return;
    t->base = (int16_t)(n - tz_pc(n) - 12);   /* the key's octave, one down: the chord sits under the hand */
    tz_set(t, n, 0);
    t->since = 0;
    t->pos = 0;
}

static void tz_step(void *st, uint32_t idx, ne_out_t *o)
{
    tz_t *t = (tz_t *)st;
    int token, v;
    (void)idx;
    if (t->since >= t->every) {
        int m = t->move == 7 ? (int)(tz_rand(&t->rng) % 3u)
                             : TZ_PATTERN[t->move][t->pos % TZ_PATTERN[t->move][3]];
        t->pos++;
        tz_apply(t, m);
        t->since = 0;
    }
    t->since++;
    token = t->root * 2 + t->minor;
    if (t->play == 0) {                  /* CHORD: the triad when it changes, held between */
        if (!t->fresh)
            return;
        tz_off(t, o);
        for (v = 0; v < 3; v++) {
            t->sounding[v] = (int16_t)tz_note(t, v);
            ne_emit(o, 1, t->sounding[v], v ? 92 : 104, token);
        }
    } else {                             /* one voice a step */
        tz_off(t, o);
        v = t->play == 1 ? (int)(t->arp % 3u) : t->play == 2 ? 2 - (int)(t->arp % 3u) : (int)(tz_rand(&t->rng) % 3u);
        t->arp++;
        t->sounding[0] = (int16_t)tz_note(t, v);
        ne_emit(o, 1, t->sounding[0], t->fresh ? 110 : 96, token);
    }
    t->fresh = 0;
}

static void tz_stop(void *st, ne_out_t *o)
{
    tz_t *t = (tz_t *)st;
    tz_off(t, o);
    t->fresh = 1;                        /* the next start plays the chord again */
}

static const ne_param_t TZ_PARAMS[4] = {
    {"MOVE", NE_ENUM, 0, 7, 0, TZ_MOVE_NAMES},
    {"EVERY", NE_INT, 1, 16, 4, 0},
    {"PLAY", NE_ENUM, 0, 3, 0, TZ_PLAY_NAMES},
    {"VOIC", NE_ENUM, 0, TZ_VAR, 1, TZ_SPREAD_NAMES},
};

typedef char tz_fits[sizeof(tz_t) <= NE_MEM_MAX ? 1 : -1];

const note_engine_t NE_TONNETZ = {
    "TONNETZ", sizeof(tz_t), TZ_PARAMS, tz_init, tz_param, tz_scale, tz_key, tz_step, tz_stop,
};
