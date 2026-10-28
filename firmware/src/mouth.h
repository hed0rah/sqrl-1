/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * mouth: a phoneme speech engine that sings, in the spirit of the 1982 software mouths and their
 * singing mode, written from published phonetics only. 40 ARPAbet-like phonemes (Peterson and Barney
 * 1952 vowels, Klatt 1980 consonants and bandwidths) glide into each other at the control rate and
 * play through syllable.c's cascade/parallel formant synthesizer, which this file drives by rewriting
 * its tracks every tick (syllable.c is unchanged). Integer only, no allocation, one caller-owned
 * struct per voice. */
#ifndef MOUTH_H
#define MOUTH_H
#include <stdint.h>
#include "syllable.h"

/* the phonemes; a phrase stores one byte each: the index, | MTH_EOW on a word's last phoneme */
enum { MTH_PAUSE,
       MTH_IY, MTH_IH, MTH_EH, MTH_AE, MTH_AA, MTH_AO, MTH_OW, MTH_UH, MTH_UW, MTH_AH, MTH_ER,
       MTH_AY, MTH_AW, MTH_OY, MTH_EY,
       MTH_P, MTH_B, MTH_T, MTH_D, MTH_K, MTH_G, MTH_F, MTH_V, MTH_TH, MTH_DH, MTH_S, MTH_Z, MTH_SH, MTH_ZH,
       MTH_HH, MTH_CH, MTH_JH, MTH_M, MTH_N, MTH_NG, MTH_L, MTH_R, MTH_W, MTH_Y,
       MTH_NPH };
#define MTH_EOW 0x80u
#define MTH_EOS 0x40u                    /* on a syllable's last phoneme, where a phrase marks its syllables (SPELL's):
                                          * PLAY SYLL splits there instead of between the nuclei */
#define MTH_CODE(b) ((uint32_t)(b) & 0x3Fu)

enum { MTH_SYLL, MTH_WORD, MTH_PHRASE };    /* PLAY: what one note sings */
#define MTH_NPHRASE 10u

extern const char *const MTH_PH_NAMES[MTH_NPH];         /* "_" for the pause, then "IY" .. "Y" */
extern const char *const MTH_PHRASE_NAMES[MTH_NPHRASE];
extern const char *const MTH_PLAY_NAMES[3];

/* one segment's shape, worked out when the sequencer enters it (ticks of SYL_CTL samples) */
typedef struct {
    uint8_t code, kind;
    uint16_t d;                      /* length */
    uint16_t rel;                    /* stops, affricates: where the closure ends */
    uint16_t asp;                    /* aspiration ticks at the start (after p t k) */
    uint16_t g0, g1;                 /* diphthongs: the glide from the first target to the second */
    uint16_t tin, tout, ain, aout;   /* formant and amplitude transitions in and out */
    int32_t ls[3], le[3];            /* F1..F3 at the start and the end, log2 Hz Q16 */
    int16_t bs[3], be[3];            /* B1..B3 at the start and the end, Hz */
} mth_geom_t;

typedef struct {
    syl_voice_t syl;                 /* the synthesizer: one-breakpoint tracks rewritten every tick */
    const uint8_t *ph;               /* the unit being sung */
    const uint8_t *vlen;             /* each phoneme's length, 0 SHORT 1 NORMAL 2 LONG on its vowel (0: none given) */
    int16_t seg;                     /* -1 onset, 0..n-1 the phonemes, n tail, n+1 silence, n+2 done */
    uint8_t n, gate, hold;           /* hold: the segment that sustains while the key is down (255 none) */
    uint16_t t, hold_t;              /* the tick within the segment; the tick that sustains */
    mth_geom_t g;
    int32_t cin[SYL_NP], cout[SYL_NP];   /* corrections that bend the segment into its boundaries */
    int32_t bout[SYL_NP];            /* the right boundary (onset and tail: their start values) */
    int32_t cur[SYL_NP];             /* the last tick's values, formants as log2 Hz Q16 */
    /* the knobs, through mth_set() */
    uint32_t fac_q8;                 /* durations x, Q8 (256 = the table's) */
    uint32_t thr_q16, mou_q16;       /* F1, F2 x, Q16 */
    int32_t breath;                  /* aspiration floor under the unit, Q15 */
    uint8_t crunch;
    /* GRIT: sample and hold, bit depth, the output's RC filters */
    uint32_t cph, cstep;
    int32_t held, lp1, lp2, ca;
    uint8_t csh;
} mth_voice_t;

/* a silent voice; mth_pitch, mth_set before the first note */
void mth_init(mth_voice_t *m);
/* f0 in Hz, Q16 (as syl_pitch) */
void mth_pitch(mth_voice_t *m, uint32_t f0_q16);
/* the knobs, 0..127 each: SPEED (64 = the table's durations, 0 = 4x as long, 127 = 0.26x, but no
 * phoneme under 17 ticks (24.7 ms) or diphthong under 45 (65.3 ms), so its glides keep their time),
 * THROAT (F1 0.7x .. 1.4x, 64 = 1x), MOUTH (F2 likewise), GRIT (0 = clean) */
void mth_set(mth_voice_t *m, uint32_t speed, uint32_t throat, uint32_t mouth, uint32_t crunch);
/* sing phonemes ph[0..n) (the caller keeps them): the key is down until mth_release. A voice still
 * sounding glides from where it is */
void mth_say(mth_voice_t *m, const uint8_t *ph, uint32_t n);
/* as mth_say, with each phoneme's length: vlen[i] 0 / 1 / 2 makes a vowel's duration x0.6 / x1 / x1.6 */
void mth_say_len(mth_voice_t *m, const uint8_t *ph, const uint8_t *vlen, uint32_t n);
void mth_release(mth_voice_t *m);
int mth_active(const mth_voice_t *m);
/* one control tick: the parameters into the tracks, then the sequencer steps (mth_render calls it) */
void mth_control(mth_voice_t *m);
/* n samples, Q15, added to out */
void mth_render(mth_voice_t *m, int32_t *out, uint32_t n);

/* the duration of phoneme code at the voice's SPEED, in ticks */
uint32_t mth_ticks(const mth_voice_t *m, uint32_t code);
/* the built-in phrase i: its bytes, and how many */
const uint8_t *mth_phrase(uint32_t i, uint32_t *n);
/* the next unit of ph[0..n) for PLAY mode: returns its start, sets its length, moves *cursor on
 * (wrapping to 0 at the end) */
uint32_t mth_unit(const uint8_t *ph, uint32_t n, uint32_t mode, uint32_t *cursor, uint32_t *len);
/* "HH EH L OW / W ER L D": phoneme names, spaces between, "/" ends a word; "_" is a pause.
 * Returns the count written, at most max; an unknown name stops it */
uint32_t mth_parse(const char *s, uint8_t *out, uint32_t max);

#endif
