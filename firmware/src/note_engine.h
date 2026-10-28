/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * note_engine: what makes the notes, apart from what makes the sound. A note engine takes keys and
 * the clock and gives note events; a sound engine (FM6, LIZARD, TALK, SYLLABLE...) plays them. On
 * the FM-1 both run in the audio interrupt, so everything here is integer, bounded and allocation
 * free.
 *
 *   keys    key 0..26 = F3..G5, down or up, as the player presses them
 *   step    a step of the part's sequencer clock, with its index (accents, lengths)
 *   stop    transport stop or a mode switch: the engine sends an off for every note it has on.
 *           An engine owns its offs; the host never guesses them
 *   scale   the shared SCALE (root 0..11, a 12-bit mask, bit 0 = the root): engines that pick
 *           pitches quantise to it
 *   param   its page's four knobs, described by params[4] (label, range, default, names); and NE_P_LEN, the
 *           part's LEN (1..64): the pattern's cycle, which every engine repeats after (the step's idx wraps there
 *           too, so idx 0 is the top of a cycle)
 *   token   a note may carry a token (0..254, NE_NO_TOKEN = none) meant for a speaking sound: SYLLABLE
 *           a syllable, TALK a word. Not wired yet: the host (shell.c sh_ne_out) drops it
 *
 * State lives in a region the host gives each part (mem bytes, at most NE_MEM_MAX), zeroed before
 * init. */
#ifndef NOTE_ENGINE_H
#define NOTE_ENGINE_H
#include <stdint.h>

#define NE_MEM_MAX 512
#define NE_MAX_EV 16
#define NE_NO_TOKEN 255u
#define NE_KEYS 27
#define NE_KEY_F3 53                     /* key 0's MIDI note */

typedef struct { uint8_t on, note, vel, token; } ne_ev_t;       /* on 0 = off (vel, token unused) */
typedef struct { ne_ev_t ev[NE_MAX_EV]; int n; } ne_out_t;

enum { NE_INT, NE_ENUM, NE_PCT, NE_LEN };   /* how the shell shows a value; NE_LEN: the knob is the part's LEN (the
                                             * host turns that, and gives it as NE_P_LEN, not as the knob's id) */
#define NE_P_LEN 4                       /* param()'s id for the part's LEN */
#define NE_P_LINE 5                      /* .. and NE_P_LINE + 0..3 for the shell's LINE page, the knobs it shows for
                                         * the engine there (S+H: FLUX CLK LEAP; HARP: STRUM SPAN); the others ignore them */
typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;            /* NE_ENUM: one per value, names[v - min] */
} ne_param_t;

typedef struct {
    const char *name;
    uint16_t mem;
    const ne_param_t *params;            /* 4 */
    void (*init)(void *st);
    void (*param)(void *st, int id, int16_t v);
    void (*scale)(void *st, uint8_t root, uint16_t mask);
    void (*key)(void *st, uint8_t key, int down, ne_out_t *o);
    void (*step)(void *st, uint32_t idx, ne_out_t *o);
    void (*stop)(void *st, ne_out_t *o);
} note_engine_t;

static inline void ne_emit(ne_out_t *o, int on, int note, int vel, int token)
{
    if (o->n < NE_MAX_EV && note >= 0 && note < 128) {
        ne_ev_t *e = &o->ev[o->n++];
        e->on = (uint8_t)(on != 0);
        e->note = (uint8_t)note;
        e->vel = (uint8_t)vel;
        e->token = (uint8_t)token;
    }
}

/* the scale's degree d (any integer, octaves past 0..count-1) above root note base; count = the
 * mask's notes. Used by every engine that picks pitches */
static inline int ne_degree(int base, uint16_t mask, int d)
{
    int pcs[12], n = 0, i, oct;
    for (i = 0; i < 12; i++)
        if (mask & (1u << i))
            pcs[n++] = i;
    if (!n)
        return base;
    oct = d >= 0 ? d / n : -((-d + n - 1) / n);
    return base + 12 * oct + pcs[d - oct * n];
}

/* where MIDI note k sits in the scale: the root's octave at or below it (base) and how many scale notes up from
 * there (deg), so ne_degree(base, mask, deg) is k, or the next scale note above it when k is off the scale. The
 * engines count from the last key so, so a key transposes along the scale */
static inline void ne_anchor(int root, uint16_t mask, int k, int *base, int *deg)
{
    int b = root + 12 * ((k - root) / 12), d = 0, i;
    if (k < b)
        b -= 12;
    for (i = 0; i < k - b; i++)
        d += (mask >> i) & 1;
    *base = b;
    *deg = d;
}

/* (the engines themselves are declared by each host, for the ones it links: this header is only the contract) */

#endif
