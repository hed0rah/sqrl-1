/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references; fixed capacity independent of optional engines. */
typedef struct {
    uint8_t factory[16][32]; /* engine 0..15, preset 0..255. SQRL-1 (shell.c SH_ST): row 13's bytes 16..31, SLICE's
                              * presets 128..255 that no engine has (shell_test asserts every engine under 128),
                              * hold the shell's own settings, and row 12's (FM6's) the note engines' values: every
                              * reader asks for a real preset, so none sees them */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
#ifndef ENGI_LIZARD
#define ENGI_LIZARD 14u          /* (core.h; a host test that brings its own engine count) */
#endif
/* the byte holding factory preset p of engine e, 0 = none. Row 14 is MIDI LEARN's map (midi_learn.c) and row 15 the
 * settings' (settings_persist.c), so SQRL-1's engines 14.. keep their presets' bits in bytes of row 15 no setting uses
 * (0..9 and 12..13, all taken now: a next engine needs a place first): every stock 1.5 setting stays where it was */
static const uint8_t FAV_EXTRA[][2] = {          /* engine 14 + i: {first byte in factory[15], bytes} */
    {0, 2},                      /* LIZARD: 16 presets */
    {2, 1},                      /* TALK: 8 */
    {3, 2},                      /* SYLLABLE: 16 */
    {5, 1},                      /* VODER: 8 */
    {6, 1},                      /* SHOCK: 8 */
    {7, 2},                      /* ORGAN: 9 (SQRL-1) */
    {12, 2},                     /* MOUTH: 11 (SQRL-1; bytes 12..13: no setting uses them) */
    {9, 1},                      /* LOOP: 5 (SQRL-1; byte 9, the last free one before 10) */
};
static uint8_t *fav_byte(uint32_t e, uint32_t p)
{
    if (e >= 14u && e - 14u < NELEM(FAV_EXTRA) && e < NENGINES)
        return p < 8u * FAV_EXTRA[e - 14u][1] ? &favorites.factory[15][FAV_EXTRA[e - 14u][0] + p / 8u] : 0;
    return e < NENGINES && e < 14u && p < 256u ? &favorites.factory[e][p / 8u] : 0;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    const uint8_t *b;
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    b = fav_byte(engine, preset);
    return b && ((*b >> (preset % 8u)) & 1u);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    if (engine > NENGINES || (engine == NENGINES ? preset >= UP_SLOTS : !fav_byte(engine, preset)))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == NENGINES) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        uint8_t *b = fav_byte(engine, preset);
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}
