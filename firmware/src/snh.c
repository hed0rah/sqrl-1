/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * S+H: sample and hold into the scale, after Ornament & Crime's Quantermain and its internal sources (Patrick
 * Dowling, Tim Churches, Max Stadler, MIT), with stepped bytebeats as Viznutcracker plays them; written fresh for
 * the FM-1's note-engine API.
 *
 * Each step samples a source and holds it as a degree of the shared SCALE over RANGE octaves, counted up from the
 * key last played, so a key transposes it along the scale. While a key is held the sample is held too: the same
 * note again each step, CopierMaschine's HOLD. A step sounds with chance DENS.
 *
 *   SRC   NOISE  a new random sample with chance P (0: 1 in 16, 15: every step)
 *         WALK   a random walk of 1 to 1 + P/4 degrees a step, turned back at RANGE's ends (brown noise)
 *         LOGIS  the logistic map x' = r x (1 - x), r from 3.5 (P 0, a few notes round) to 4.0 (P 15, chaos)
 *         BYTE   a bytebeat stepped once a step, 125 of its 8 kHz samples on (250 for P 8..15): P & 7 the
 *                equation (0..2 viznut's, 3 stephth's, 4..7 new)
 *         INT    an integer sequence: P & 3 picks PI's digits, VAN ECK's, DRESS's (the 1 bits of n) or
 *                NORGARD's infinity sequence; 1, 3, 5 or 7 terms a step (P / 4)
 *   RANGE 1..4 octaves, free; then 1..4 octaves, LOCKed
 *   P     0..15, the source's own (above)
 *   DENS  0..100 %: how many steps sound
 *
 * LOCK (RANGE's right half, "2 LOCK"): at the top of each cycle (the part's LEN, idx 0) every source starts again
 * from its seed, so S+H repeats after LEN steps, as a locked Turing machine does. Unlocked it runs free.
 *
 * FLUX (NE_P_LINE, the LINE page's) changes a locked loop as a Turing machine's knob does: each step, with chance
 * FLUX, it takes a new value (half the time a degree or two from the one it had, half anywhere in RANGE) or its
 * rest turns to a note or back, and keeps it pass after pass. FLUX 0 freezes the loop as it is now; turning SRC, P or
 * RANGE, or starting from stop, goes back to the source's own loop. Unlocked, FLUX does nothing.
 *
 * LEAP (NE_P_LINE + 2): the largest move from one note to the next, 1..7 degrees (0: any); a sample further away
 * lands that far toward it, so noise sings. CLK (NE_P_LINE + 1) is the shell's: what steps S+H (shell_ne_run).
 *
 * The sample (0..255) goes out as the token (no sound reads it yet: note_engine.h). */
#include "note_engine.h"

enum { SNH_NOISE, SNH_WALK, SNH_LOGIS, SNH_BYTE, SNH_INT };

typedef struct {
    uint8_t src, range, p, dens;
    uint8_t root;
    uint16_t mask;
    int16_t key;                         /* the last key played (a MIDI note), the transposition */
    int16_t sounding;                    /* the note on now, -1 = none */
    int16_t deg;                         /* the held sample, degrees above the key */
    uint8_t val;                         /* .. as the source gave it, 0..255 */
    uint8_t held;                        /* keys down: the sample holds */
    uint8_t lock;                        /* each LEN from the seed again */
    uint16_t n;                          /* the sources' step count */
    uint16_t x;                          /* LOGIS, Q12 */
    uint8_t eck, eck_n, eck_last[64];    /* VAN ECK: the term, its index, where each value was last (+1) */
    uint32_t rng;
    uint8_t flux;                        /* FLUX, % */
    uint8_t leap;                        /* LEAP, degrees (0: any) */
    int8_t mut[64];                      /* each step's FLUX value, degrees (-1: the source's own) */
    uint32_t rflip[2];                   /* the steps whose rest FLUX turned (a note, or a rest); words, not a
                                         * uint64_t: the host gives 4-byte aligned memory */
    uint32_t frng;                       /* FLUX's own dice, never reseeded */
} snh_t;

static const char SNH_PI[] = "3141592653589793238462643383279502884197169399375105820974944592";

static uint32_t snh_rand(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

static void snh_seed(snh_t *s)          /* every source from its start: the top of a cycle */
{
    unsigned i;
    s->rng = 0x2545F491u;
    s->n = 0;
    s->x = 1229;                         /* 0.3 */
    s->eck = s->eck_n = 0;
    for (i = 0; i < sizeof s->eck_last; i++)
        s->eck_last[i] = 0;
}

static void snh_unflux(snh_t *s)         /* back to the source's own loop */
{
    unsigned i;
    for (i = 0; i < sizeof s->mut; i++)
        s->mut[i] = -1;
    s->rflip[0] = s->rflip[1] = 0;
}

static void snh_init(void *st)
{
    snh_t *s = (snh_t *)st;
    snh_seed(s);
    snh_unflux(s);
    s->frng = 0x9E3779B9u;
    s->src = SNH_NOISE;
    s->range = 2;
    s->p = 12;
    s->dens = 75;
    s->mask = 0xAB5;                     /* major */
    s->key = 60;
    s->sounding = -1;
}

static void snh_param(void *st, int id, int16_t v)
{
    snh_t *s = (snh_t *)st;
    switch (id) {
    case 0: s->src = (uint8_t)(v < 0 ? 0 : v > SNH_INT ? SNH_INT : v); snh_unflux(s); break;
    case 1:
        v = v < 1 ? 1 : v > 8 ? 8 : v;
        s->range = (uint8_t)((v - 1) % 4 + 1);
        s->lock = (uint8_t)(v > 4);
        snh_unflux(s);
        break;
    case 2: s->p = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v); snh_unflux(s); break;
    case 3: s->dens = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    case NE_P_LINE: s->flux = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    case NE_P_LINE + 2: s->leap = (uint8_t)(v < 0 ? 0 : v > 7 ? 7 : v); break;
    }
}

static void snh_scale(void *st, uint8_t root, uint16_t mask)
{
    snh_t *s = (snh_t *)st;
    s->root = (uint8_t)(root % 12u);
    s->mask = mask & 0xFFFu ? mask & 0xFFFu : 0xFFFu;
}

static void snh_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    snh_t *s = (snh_t *)st;
    (void)o;
    if (key >= NE_KEYS)
        return;
    if (down) {
        s->key = (int16_t)(NE_KEY_F3 + key);
        s->held++;
    } else if (s->held)
        s->held--;
}

static uint32_t snh_byte(uint32_t eq, uint32_t t)
{
    switch (eq & 7u) {
    case 0: return t * (t >> 5 | t >> 8);
    case 1: return t * ((t >> 12 | t >> 8) & 63u & t >> 4);
    case 2: return (t >> 6 | t | t >> (t >> 16 & 31u)) * 10u + ((t >> 11) & 7u);
    case 3: return (t * 5u & t >> 7) | (t * 3u & t >> 10);
    case 4: return t ^ t >> 3 ^ t >> 7;
    case 5: return (t * t >> 9) & t >> 4;
    case 6: return t * (t >> 9 & 3u) + (t >> 5);
    default: return (t >> 4) * (t >> 2 & 5u) ^ t >> 1;
    }
}

static int32_t snh_norgard(uint32_t n)   /* a(2n) = -a(n), a(2n+1) = a(n) + 1 */
{
    int32_t v = 0;
    int b;
    for (b = 15; b >= 0; b--)
        if (n >> b)
            v = (n >> b) & 1u ? v + 1 : -v;
    return v;
}

static void snh_eck(snh_t *s)            /* the next van Eck term: how far back the term was last, else 0 */
{
    uint8_t prev;
    if (s->eck_n >= 63u) {               /* (64 terms, then again from 0) */
        uint32_t i;
        for (i = 0; i < 64u; i++)
            s->eck_last[i] = 0;
        s->eck = 0;
        s->eck_n = 0;
        return;
    }
    prev = s->eck_last[s->eck & 63u];
    s->eck_last[s->eck & 63u] = (uint8_t)(s->eck_n + 1u);
    s->eck = prev ? (uint8_t)(s->eck_n + 1u - prev) : 0u;
    s->eck_n++;
}

/* a new sample, as a degree 0..span */
static int snh_sample(snh_t *s, int span)
{
    uint32_t r = snh_rand(&s->rng), k;
    int32_t v;
    switch (s->src) {
    case SNH_NOISE:
        if ((r & 15u) <= s->p || s->n == 0u)
            s->val = (uint8_t)(r >> 8);
        s->n++;
        return (int)((s->val * (uint32_t)(span + 1)) >> 8);
    case SNH_WALK: {
        int d = s->deg, by = 1 + (int)((r >> 4) % (1u + s->p / 4u));
        d += (r >> 8) & 1u ? by : -by;
        if (d < 0)
            d = -d;
        if (d > span)
            d = 2 * span - d;
        d = d < 0 ? 0 : d;
        s->val = (uint8_t)(span ? d * 255 / span : 0);
        return d;
    }
    case SNH_LOGIS: {
        uint32_t rq = 14336u + s->p * 2048u / 15u, x = s->x;      /* r in Q12: 3.5 .. 4.0 */
        x = ((rq * x) >> 12) * (4096u - x) >> 12;
        if (x == 0u || x >= 4096u)
            x = 256u + (r & 3583u);      /* (stuck at an end: a fresh start) */
        s->x = (uint16_t)x;
        s->val = (uint8_t)(x >> 4);
        return (int)((s->val * (uint32_t)(span + 1)) >> 8);
    }
    case SNH_BYTE:
        s->val = (uint8_t)snh_byte(s->p, 4096u + (uint32_t)s->n * (s->p >> 3 ? 250u : 125u));
        s->n++;
        return (int)((s->val * (uint32_t)(span + 1)) >> 8);
    default:
        for (k = 0; k <= s->p / 4u * 2u; k++) {
            s->n++;
            if ((s->p & 3u) == 1u)
                snh_eck(s);
        }
        switch (s->p & 3u) {
        case 0: v = SNH_PI[s->n % 64u] - '0'; break;
        case 1: v = s->eck; break;
        case 2: v = __builtin_popcount(s->n); break;
        default: v = snh_norgard(s->n) + span / 2; break;
        }
        v %= span + 1;
        v = v < 0 ? v + span + 1 : v;
        s->val = (uint8_t)(span ? v * 255 / span : 0);
        return (int)v;
    }
}

static void snh_step(void *st, uint32_t idx, ne_out_t *o)
{
    snh_t *s = (snh_t *)st;
    int base, deg, span = __builtin_popcount(s->mask) * s->range, note, rest;
    if (s->sounding >= 0)
        ne_emit(o, 0, s->sounding, 0, 0);
    s->sounding = -1;
    if (!idx && s->lock) {
        snh_seed(s);
        if (!s->held)
            s->deg = 0;                  /* (WALK from its start; a held sample holds on) */
    }
    if (!s->held) {
        int was = s->deg, d = snh_sample(s, span);
        if (s->leap)                     /* LEAP: no further than that from the note before */
            d = d > was + s->leap ? was + s->leap : d < was - s->leap ? was - s->leap : d;
        s->deg = (int16_t)d;
    }
    rest = snh_rand(&s->rng) % 100u >= s->dens;
    if (s->lock && !s->held) {          /* FLUX: this step's own value and rest, changed with chance FLUX */
        uint32_t i = idx % 64u, r;
        if (s->flux && snh_rand(&s->frng) % 100u < s->flux) {
            r = snh_rand(&s->frng);
            if ((r & 7u) == 0u)
                s->rflip[i / 32u] ^= 1u << (i % 32u);
            else {
                int d = s->mut[i] >= 0 ? s->mut[i] : s->deg, by = 1 + (int)((r >> 4) & 1u);
                d = (r >> 3) & 1u ? d + ((r >> 5) & 1u ? by : -by) : (int)((r >> 8) % (uint32_t)(span + 1));
                s->mut[i] = (int8_t)(d < 0 ? -d : d > span ? 2 * span - d : d);
            }
        }
        if (s->mut[i] >= 0)
            s->deg = (int16_t)(s->mut[i] > span ? span : s->mut[i]);
        rest ^= (int)((s->rflip[i / 32u] >> (i % 32u)) & 1u);
    }
    if (rest)
        return;
    ne_anchor(s->root, s->mask, s->key, &base, &deg);
    note = ne_degree(base, s->mask, deg + s->deg);
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    ne_emit(o, 1, note, idx % 4u == 0u ? 112 : 96, s->val < 255 ? s->val : 254);
    s->sounding = (int16_t)note;
}

static void snh_stop(void *st, ne_out_t *o)
{
    snh_t *s = (snh_t *)st;
    if (s->sounding >= 0)
        ne_emit(o, 0, s->sounding, 0, 0);
    s->sounding = -1;
    s->held = 0;
    snh_unflux(s);                       /* (the next start plays the source's own loop) */
}

static const char *const SNH_SRC_NAMES[] = {"NOISE", "WALK", "LOGIS", "BYTE", "INT"};
static const char *const SNH_RANGE_NAMES[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT", "1 LOCK", "2 LOCK", "3 LOCK", "4 LOCK"};
static const ne_param_t SNH_PARAMS[4] = {
    {"SRC", NE_ENUM, 0, 4, 0, SNH_SRC_NAMES},
    {"RANGE", NE_ENUM, 1, 8, 2, SNH_RANGE_NAMES},
    {"P", NE_INT, 0, 15, 12, 0},
    {"DENS", NE_PCT, 0, 100, 75, 0},
};

typedef char snh_fits[sizeof(snh_t) <= NE_MEM_MAX ? 1 : -1];

const note_engine_t NE_SNH = {
    "S+H", sizeof(snh_t), SNH_PARAMS, snh_init, snh_param, snh_scale, snh_key, snh_step, snh_stop,
};
