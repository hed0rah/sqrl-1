/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * mouth: phonemes in sequence, sung through syllable.c (read mouth.h first).
 *
 * Each phoneme is a segment with a body (its targets and sources, which may change inside it: a
 * stop's closure then burst, a diphthong's glide) and two boundaries it shares with its neighbours.
 * Near each end a linear correction bends the body onto the boundary, so every parameter is
 * continuous across phonemes and each body is reached whole between its transitions. Formants move
 * in log2 Hz; a consonant meets a vowel at its locus (onset = L + 3/4 (V - L), syllable.c's K_LOCUS,
 * after Delattre, Liberman and Cooper 1955), anything else meets halfway.
 * Per sample this file adds nothing to syllable.c but GRIT; per tick (64 samples) it does one body,
 * three exp2 and the track writes. Integer only. Signed right shifts are arithmetic. */
#include "mouth.h"

#define MTH_Q 32767
#define MTH_AV_VOWEL 22937               /* 0.7 Q: syllable.c's vowel level, Klatt's AV 60 dB */
#define MTH_AV_SONOR 7253                /* 10 dB under the vowel: Klatt (1980) for prevocalic w y r l */
#define MTH_AV_NASAL 13107               /* 0.4 Q, syllable.c's murmur */
#define MTH_AV_VFRIC 5135                /* 13 dB under the vowel: Klatt's AV 47 against 60 */
#define MTH_AV_BAR 5461                  /* the voice bar under a voiced closure, syllable.c's Q / 6 */
#define MTH_AF_VOICED 10362              /* 10 dB under the voiceless: Klatt's AF 50 against 60 */
#define MTH_AH_HH 22937                  /* h: aspiration at the vowel's level (syllable.c) */
#define MTH_AH_ASP 16384                 /* aspiration after p t k, until voicing starts */
#define MTH_LOCUS 6                      /* eighths: onset = L + 6/8 (V - L) */
#define MTH_TR_MS 30                     /* formant transition each side of a boundary */
#define MTH_AMP_MS 6                     /* amplitude transition; fricatives twice that */
#define MTH_TAIL 14u                     /* ticks: the unit's last values fade out (20 ms) */
#define MTH_POST 56u                     /* ticks of silence for the filters to ring down (81 ms) */
#define MTH_RAMP_MIN 3u                  /* ticks: no amplitude transition shorter, whatever SPEED (4.4 ms) */
#define MTH_TR_MIN 8u                    /* ticks: no formant transition shorter, whatever SPEED (11.6 ms) */
/* so no phoneme is shorter than its two glides and a tick of target between them (24.7 ms); a
 * diphthong's glides fit in its first 3/10 and last 2/10 (65.3 ms). SPEED shrinks durations down to
 * these and no further, as Klatt's (1979) duration rules stop at a minimum duration */
#define MTH_D_MIN (2u * MTH_TR_MIN + 1u)
#define MTH_D_MIN_DIPH (5u * (MTH_TR_MIN + 1u))
#define MTH_SRC_SHIFT 1                  /* AV AH AF go to the synthesizer at half the levels above: 6 dB of
                                          * headroom, so a harmonic on a formant moved by THROAT or MOUTH at
                                          * 127 does not saturate syllable.c's 16-bit output (tests: range) */

enum { MTH_K_PAUSE, MTH_K_VOWEL, MTH_K_DIPH, MTH_K_SONOR, MTH_K_NASAL, MTH_K_FRIC, MTH_K_STOP, MTH_K_AFFR,
       MTH_K_HH };

typedef struct {
    uint8_t kind, voiced;
    uint16_t dur;                        /* ms at SPEED 64 */
    int16_t f[3], b[3];                  /* Hz: F1 F2 F3, B1 B2 B3 */
    int16_t av, af;                      /* Q15: voicing, frication */
    int16_t band[6];                     /* Q15: P2 P3 P4 P5 P6 AB (P2 at F2, P3 at F3, then 3500 5000 6500 Hz) */
    uint8_t burst;                       /* ms: a stop's burst, an affricate's frication */
    uint8_t vot;                         /* ms: aspiration a voiceless stop puts into the next sound */
    uint8_t glide;                       /* diphthongs: 1 + the MTH_GLIDE row of the second target */
} mth_ph_t;

typedef struct { int16_t f[3], b[3]; } mth_target_t;

/* Sources.
 * Vowel F1 F2 F3: Peterson and Barney (1952), table II, men's averages. OW and the diphthongs, and
 * every vowel bandwidth: Klatt (1980) table II (first row; the second row is the glide's target).
 * Consonant formants and bandwidths: Klatt (1980) table III, consonants before front vowels; the
 * frequencies serve as loci. Its frication amplitudes A2..A6 AB (dB) become Q15 gains relative to each
 * consonant's strongest band, which takes syllable.c's level for its nearest consonant; Klatt's
 * F4..F6 sit near 3300 3750 4900 Hz, our P4..P6 at 3500 5000 6500, so the bands here lie higher.
 * Not in Klatt's table, ours: ZH (SH's formants, JH's voiced bandwidths), NG (the velar loci, n's
 * bandwidths), HH (the neighbouring vowel's formants with B1 300, as Klatt describes it).
 * Durations: Klatt's (1979) inherent durations as used by MITalk (Allen, Hunnicutt and Klatt 1987);
 * consonants and most vowels cross-checked with Festival's klatt_durs.scm, AH UH UW from the MITalk
 * table as remembered (unchecked). Bursts as syllable.c; VOT: Lisker and Abramson (1964), English. */
static const mth_ph_t MTH_PH[MTH_NPH] = {
    /* kind voiced  ms   F1 F2 F3            B1 B2 B3        AV AF  P2 P3 P4 P5 P6 AB      burst vot glide */
    {MTH_K_PAUSE, 0, 100, {0, 0, 0}, {0, 0, 0}, 0, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},
    {MTH_K_VOWEL, 1, 155, {270, 2290, 3010}, {45, 200, 400}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},   /* IY */
    {MTH_K_VOWEL, 1, 135, {390, 1990, 2550}, {50, 100, 140}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},   /* IH */
    {MTH_K_VOWEL, 1, 150, {530, 1840, 2480}, {60, 90, 200}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* EH */
    {MTH_K_VOWEL, 1, 230, {660, 1720, 2410}, {70, 150, 320}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},   /* AE */
    {MTH_K_VOWEL, 1, 240, {730, 1090, 2440}, {130, 70, 160}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},   /* AA */
    {MTH_K_VOWEL, 1, 240, {570, 840, 2410}, {90, 100, 80}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},     /* AO */
    {MTH_K_DIPH, 1, 220, {540, 1100, 2300}, {80, 70, 70}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 1},     /* OW */
    {MTH_K_VOWEL, 1, 160, {440, 1020, 2240}, {80, 100, 80}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* UH */
    {MTH_K_VOWEL, 1, 210, {300, 870, 2240}, {65, 110, 140}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* UW */
    {MTH_K_VOWEL, 1, 140, {520, 1190, 2390}, {80, 50, 140}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* AH */
    {MTH_K_VOWEL, 1, 180, {490, 1350, 1690}, {100, 60, 110}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},   /* ER */
    {MTH_K_DIPH, 1, 250, {660, 1200, 2550}, {100, 70, 200}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 2},   /* AY */
    {MTH_K_DIPH, 1, 240, {640, 1230, 2550}, {80, 70, 140}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 3},    /* AW */
    {MTH_K_DIPH, 1, 280, {550, 960, 2400}, {80, 50, 130}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 4},     /* OY */
    {MTH_K_DIPH, 1, 180, {480, 1720, 2520}, {70, 100, 200}, MTH_AV_VOWEL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 5},   /* EY */
    {MTH_K_STOP, 0, 90, {400, 1100, 2150}, {300, 150, 220}, 0, MTH_Q, {0, 0, 0, 0, 0, 12000}, 8, 58, 0},    /* P */
    {MTH_K_STOP, 1, 85, {200, 1100, 2150}, {60, 110, 130}, MTH_AV_BAR, MTH_Q, {0, 0, 0, 0, 0, 9000}, 6, 0, 0},   /* B */
    {MTH_K_STOP, 0, 75, {400, 1600, 2600}, {300, 120, 250}, 0, MTH_Q, {0, 269, 1511, 6014, 12000, 0}, 10, 70, 0},   /* T */
    {MTH_K_STOP, 1, 75, {200, 1600, 2600}, {60, 100, 170}, MTH_AV_BAR, MTH_Q, {0, 1600, 7149, 9000, 7149, 0}, 6, 0, 0},   /* D */
    {MTH_K_STOP, 0, 80, {300, 1990, 2850}, {250, 160, 330}, 0, MTH_Q, {0, 16000, 5060, 6370, 6370, 0}, 14, 80, 0},  /* K */
    {MTH_K_STOP, 1, 80, {200, 1990, 2850}, {60, 150, 280}, MTH_AV_BAR, MTH_Q, {0, 12000, 3795, 4777, 4777, 0}, 10, 0, 0},  /* G */
    {MTH_K_FRIC, 0, 100, {340, 1100, 2080}, {200, 120, 150}, 0, MTH_Q, {0, 0, 0, 0, 0, 5000}, 0, 0, 0},     /* F */
    {MTH_K_FRIC, 1, 60, {220, 1100, 2080}, {60, 90, 120}, MTH_AV_VFRIC, MTH_AF_VOICED, {0, 0, 0, 0, 0, 3000}, 0, 0, 0},   /* V */
    {MTH_K_FRIC, 0, 90, {320, 1290, 2540}, {200, 90, 200}, 0, MTH_Q, {0, 0, 0, 0, 500, 5000}, 0, 0, 0},     /* TH */
    {MTH_K_FRIC, 1, 50, {270, 1290, 2540}, {60, 80, 170}, MTH_AV_VFRIC, MTH_AF_VOICED, {0, 0, 0, 0, 300, 3000}, 0, 0, 0},  /* DH */
    {MTH_K_FRIC, 0, 105, {320, 1390, 2530}, {200, 80, 200}, 0, MTH_Q, {0, 0, 0, 0, 16000, 0}, 0, 0, 0},     /* S */
    {MTH_K_FRIC, 1, 75, {240, 1390, 2530}, {70, 60, 180}, MTH_AV_VFRIC, MTH_AF_VOICED, {0, 0, 0, 0, 10000, 0}, 0, 0, 0},  /* Z */
    {MTH_K_FRIC, 0, 105, {300, 1840, 2750}, {200, 100, 300}, 0, MTH_Q, {0, 14000, 4967, 4967, 3946, 0}, 0, 0, 0},   /* SH */
    {MTH_K_FRIC, 1, 70, {300, 1840, 2750}, {60, 80, 270}, MTH_AV_VFRIC, MTH_AF_VOICED, {0, 9000, 3193, 3193, 2537, 0}, 0, 0, 0},  /* ZH */
    {MTH_K_HH, 0, 80, {0, 0, 0}, {300, 0, 0}, 0, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},                            /* HH */
    {MTH_K_AFFR, 0, 70, {350, 1800, 2820}, {200, 90, 300}, 0, MTH_Q, {0, 2219, 14000, 6254, 6254, 0}, 40, 0, 0},   /* CH */
    {MTH_K_AFFR, 1, 70, {260, 1800, 2820}, {60, 80, 270}, MTH_AV_VFRIC, MTH_AF_VOICED, {0, 1426, 9000, 4020, 4020, 0}, 40, 0, 0},  /* JH */
    {MTH_K_NASAL, 1, 70, {480, 1270, 2130}, {40, 200, 200}, MTH_AV_NASAL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* M */
    {MTH_K_NASAL, 1, 60, {480, 1340, 2470}, {40, 300, 300}, MTH_AV_NASAL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* N */
    {MTH_K_NASAL, 1, 95, {480, 1990, 2850}, {40, 300, 300}, MTH_AV_NASAL, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* NG */
    {MTH_K_SONOR, 1, 80, {310, 1050, 2880}, {50, 100, 280}, MTH_AV_SONOR, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* L */
    {MTH_K_SONOR, 1, 80, {310, 1060, 1380}, {70, 100, 120}, MTH_AV_SONOR, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* R */
    {MTH_K_SONOR, 1, 80, {290, 610, 2150}, {50, 80, 60}, MTH_AV_SONOR, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},       /* W */
    {MTH_K_SONOR, 1, 80, {260, 2070, 3020}, {40, 250, 500}, MTH_AV_SONOR, 0, {0, 0, 0, 0, 0, 0}, 0, 0, 0},    /* Y */
};

/* the diphthongs' second targets, Klatt (1980) table II's second rows: OW AY AW OY EY */
static const mth_target_t MTH_GLIDE[5] = {
    {{450, 900, 2300}, {80, 70, 70}}, {{400, 1880, 2500}, {70, 100, 200}}, {{420, 940, 2350}, {80, 70, 80}},
    {{360, 1820, 2450}, {60, 50, 160}}, {{330, 2020, 2600}, {55, 100, 200}}};
static const mth_target_t MTH_NEUTRAL = {{500, 1500, 2500}, {300, 90, 150}};   /* HH with no vowel near */

const char *const MTH_PH_NAMES[MTH_NPH] = {
    "_", "IY", "IH", "EH", "AE", "AA", "AO", "OW", "UH", "UW", "AH", "ER", "AY", "AW", "OY", "EY",
    "P", "B", "T", "D", "K", "G", "F", "V", "TH", "DH", "S", "Z", "SH", "ZH", "HH", "CH", "JH",
    "M", "N", "NG", "L", "R", "W", "Y"};

/* the phrases, ours */
#define P_(x) MTH_##x
#define E_(x) (MTH_##x | MTH_EOW)
static const uint8_t MTH_P_HELLO[] = {P_(HH), P_(EH), P_(L), E_(OW)};
static const uint8_t MTH_P_COUNT[] = {P_(W), P_(AH), E_(N), P_(T), E_(UW), P_(TH), P_(R), E_(IY), P_(F), P_(AO), E_(R)};
static const uint8_t MTH_P_LALA[] = {P_(L), E_(AA), P_(L), E_(AA), P_(L), E_(AA)};
static const uint8_t MTH_P_OOHAH[] = {E_(UW), E_(AA)};
static const uint8_t MTH_P_DATABURN[] = {P_(D), P_(EY), P_(T), E_(AH), P_(B), P_(ER), E_(N)};
static const uint8_t MTH_P_ROBOT[] = {E_(AY), P_(AE), E_(M), E_(AH), P_(R), P_(OW), P_(B), P_(AA), E_(T)};
static const uint8_t MTH_P_SING[] = {P_(S), P_(IH), E_(NG), P_(T), E_(UW), P_(M), E_(IY)};
static const uint8_t MTH_P_DORE[] = {P_(D), E_(OW), P_(R), E_(EY), P_(M), E_(IY), P_(F), E_(AA), P_(S), E_(OW),
                                     P_(L), E_(AA), P_(T), E_(IY), P_(D), E_(OW)};
static const uint8_t MTH_P_MOON[] = {P_(DH), E_(AH), P_(M), P_(UW), E_(N), P_(IH), E_(Z), P_(M), P_(EY), E_(D),
                                     P_(AH), E_(V), P_(N), P_(OY), E_(Z)};
static const uint8_t MTH_P_FIVE8[] = {P_(F), P_(AY), E_(V), P_(S), P_(IH), P_(K), E_(S), P_(S), P_(EH), P_(V),
                                      P_(AH), E_(N), P_(EY), E_(T)};
#undef P_
#undef E_
static const struct { const uint8_t *p; uint8_t n; } MTH_PHRASES[MTH_NPHRASE] = {
    {MTH_P_HELLO, sizeof MTH_P_HELLO}, {MTH_P_COUNT, sizeof MTH_P_COUNT}, {MTH_P_LALA, sizeof MTH_P_LALA},
    {MTH_P_OOHAH, sizeof MTH_P_OOHAH}, {MTH_P_DATABURN, sizeof MTH_P_DATABURN}, {MTH_P_ROBOT, sizeof MTH_P_ROBOT},
    {MTH_P_SING, sizeof MTH_P_SING}, {MTH_P_DORE, sizeof MTH_P_DORE}, {MTH_P_MOON, sizeof MTH_P_MOON},
    {MTH_P_FIVE8, sizeof MTH_P_FIVE8}};
/* hello / one two three four / la la la / ooh ah / data burn / I am a robot / sing to me /
 * do re mi fa so la ti do / the moon is made of noise / five six seven eight */
const char *const MTH_PHRASE_NAMES[MTH_NPHRASE] = {"HELLO", "COUNT", "LALALA", "OOH AH", "DATABN", "ROBOT",
                                                   "SING", "DO RE", "MOON", "FIVE8"};
const char *const MTH_PLAY_NAMES[3] = {"SYLL", "WORD", "PHRASE"};

/* 2^(i/32) and log2(1 + i/32), Q16, i = 0..32: linear between, 6e-5 off at worst */
static const int32_t MTH_X2[33] = {65536, 66971, 68438, 69936, 71468, 73032, 74632, 76266, 77936, 79642, 81386,
                                   83169, 84990, 86851, 88752, 90696, 92682, 94711, 96785, 98905, 101070, 103283,
                                   105545, 107856, 110218, 112631, 115098, 117618, 120194, 122825, 125515, 128263,
                                   131072};
static const int32_t MTH_L2[33] = {0, 2909, 5732, 8473, 11136, 13727, 16248, 18704, 21098, 23433, 25711, 27936,
                                   30109, 32234, 34312, 36346, 38336, 40286, 42196, 44068, 45904, 47705, 49472,
                                   51207, 52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047, 65536};

/* log2 x, Q16, x >= 1 and below 2^16 */
static int32_t mth_log2(int32_t x)
{
    int32_t e = 15, m, f, i;
    x = x < 1 ? 1 : x;
    while (!(x >> e))
        e--;
    m = x << (16 - e);                                   /* [65536, 131072) */
    f = m - 65536;
    i = f >> 11;
    return (e << 16) + MTH_L2[i] + (((MTH_L2[i + 1] - MTH_L2[i]) * (f & 2047)) >> 11);
}

/* 2^(l / 65536), l in 0 .. 15 octaves, rounded */
static int32_t mth_exp2(int32_t l)
{
    int32_t e = l >> 16, f = l & 0xFFFF, i = f >> 11;
    int32_t m = MTH_X2[i] + (((MTH_X2[i + 1] - MTH_X2[i]) * (f & 2047)) >> 11);
    return e >= 16 ? m << (e - 16) : (m + (1 << (15 - e))) >> (16 - e);
}

/* ms at the voice's SPEED, in ticks: ms x 44.1 / 64 x fac, at least 1 */
static uint32_t mth_ms(const mth_voice_t *m, uint32_t ms)
{
    uint32_t t = (ms * m->fac_q8 * 441u / 640u + 128u) >> 8;
    return t ? t : 1u;
}

uint32_t mth_ticks(const mth_voice_t *m, uint32_t code)
{
    const mth_ph_t *P = &MTH_PH[code < MTH_NPH ? code : 0];
    uint32_t t = mth_ms(m, P->dur);
    uint32_t lo = P->kind == MTH_K_PAUSE ? 3u : P->kind == MTH_K_DIPH ? MTH_D_MIN_DIPH : MTH_D_MIN;
    return t < lo ? lo : t;
}

static uint32_t mth_kind(const mth_voice_t *m, int32_t seg)
{
    return seg >= 0 && seg < m->n ? MTH_PH[MTH_CODE(m->ph[seg])].kind : MTH_K_PAUSE;
}

static int mth_vowelish(uint32_t k) { return k == MTH_K_VOWEL || k == MTH_K_DIPH || k == MTH_K_SONOR || k == MTH_K_HH; }
static int mth_locus(uint32_t k) { return k == MTH_K_STOP || k == MTH_K_AFFR || k == MTH_K_FRIC || k == MTH_K_NASAL; }

/* the formant targets of segment seg at its start (end 0) or end (end 1); 0 for a pause or past the
 * unit. HH takes the next sound's, else the previous one's, else MTH_NEUTRAL */
static int mth_target(const mth_voice_t *m, int32_t seg, int end, mth_target_t *o, int depth)
{
    const mth_ph_t *P;
    uint32_t k;
    if (seg < 0 || seg >= m->n)
        return 0;
    P = &MTH_PH[MTH_CODE(m->ph[seg])];
    if (P->kind == MTH_K_PAUSE)
        return 0;
    if (P->kind == MTH_K_HH) {
        if (depth)
            return 0;
        if (!mth_target(m, seg + 1, 0, o, 1) && !mth_target(m, seg - 1, 1, o, 1))
            *o = MTH_NEUTRAL;
        o->b[0] = 300;                                   /* Klatt: B1 widened to about 300 Hz */
        return 1;
    }
    if (end && P->glide) {
        *o = MTH_GLIDE[P->glide - 1];
        return 1;
    }
    for (k = 0; k < 3; k++) {
        o->f[k] = P->f[k];
        o->b[k] = P->b[k];
    }
    return 1;
}

/* segment seg's shape. left: the boundary values on its left (a pause holds their formants), 0 = none */
static void mth_geom(const mth_voice_t *m, int32_t seg, const int32_t *left, mth_geom_t *g)
{
    uint32_t code = MTH_CODE(m->ph[seg]), k, d, tr, am, half;
    const mth_ph_t *P = &MTH_PH[code];
    mth_target_t a, b;
    g->code = (uint8_t)code;
    g->kind = P->kind;
    d = mth_ticks(m, code);
    if (m->vlen && m->vlen[seg] != 1u && (P->kind == MTH_K_VOWEL || P->kind == MTH_K_DIPH)) {   /* SPELL's LEN */
        static const uint16_t LEN_Q8[3] = {154, 256, 410};
        uint32_t lo = P->kind == MTH_K_DIPH ? MTH_D_MIN_DIPH : MTH_D_MIN;
        d = d * LEN_Q8[m->vlen[seg] > 2u ? 2u : m->vlen[seg]] >> 8;
        d = d < lo ? lo : d;
    }
    g->d = (uint16_t)d;
    half = (d - 1u) / 2u;
    if (mth_target(m, seg, 0, &a, 0)) {
        mth_target(m, seg, 1, &b, 0);
        for (k = 0; k < 3; k++) {
            g->ls[k] = mth_log2(a.f[k]);
            g->le[k] = mth_log2(b.f[k]);
            g->bs[k] = a.b[k];
            g->be[k] = b.b[k];
        }
    } else
        for (k = 0; k < 3; k++) {                        /* a pause: hold what came before */
            g->ls[k] = g->le[k] = left ? left[SYL_F1 + k] : mth_log2(MTH_NEUTRAL.f[k]);
            g->bs[k] = g->be[k] = (int16_t)(left ? left[SYL_B1 + k] : MTH_NEUTRAL.b[k]);
        }
    g->rel = 0;
    if (P->kind == MTH_K_STOP || P->kind == MTH_K_AFFR) {
        uint32_t r = mth_ms(m, P->burst);
        g->rel = (uint16_t)(d - (r > d - 2u ? d - 2u : r));
    }
    g->asp = 0;
    if (seg > 0 && (P->kind == MTH_K_VOWEL || P->kind == MTH_K_DIPH || P->kind == MTH_K_SONOR)) {
        const mth_ph_t *Q = &MTH_PH[MTH_CODE(m->ph[seg - 1])];
        if (Q->kind == MTH_K_STOP && !Q->voiced) {
            uint32_t a2 = mth_ms(m, Q->vot);
            g->asp = (uint16_t)(a2 < half ? a2 : half);
        }
    }
    g->g0 = 0;
    g->g1 = (uint16_t)(d - 1u);
    tr = mth_ms(m, MTH_TR_MS);
    tr = tr < MTH_TR_MIN ? MTH_TR_MIN : tr;
    tr = tr < half ? tr : half;
    g->tin = g->tout = (uint16_t)tr;
    am = mth_ms(m, P->kind == MTH_K_FRIC || P->kind == MTH_K_AFFR ? 2u * MTH_AMP_MS : MTH_AMP_MS);
    am = am < MTH_RAMP_MIN ? MTH_RAMP_MIN : am;
    am = am < half ? am : half;
    g->ain = g->aout = (uint16_t)am;
    if (P->kind == MTH_K_DIPH) {                         /* hold the first target, glide, hold the second */
        g->g0 = (uint16_t)(3u * d / 10u);
        g->g1 = (uint16_t)(8u * d / 10u);
        if (g->tin > g->g0)
            g->tin = g->g0;
        if (g->ain > g->g0)
            g->ain = g->g0;
        if (g->tout > d - 1u - g->g1)
            g->tout = (uint16_t)(d - 1u - g->g1);
    }
    if (g->asp && g->ain > g->asp)
        g->ain = g->asp;
    if (!g->tin) g->tin = 1;
    if (!g->tout) g->tout = 1;
    if (!g->ain) g->ain = 1;
    if (!g->aout) g->aout = 1;
}

/* the body at tick t: every parameter as syllable.c's tracks take it, F1..F3 in log2 Hz Q16 */
static void mth_body(const mth_geom_t *g, uint32_t t, int32_t *v)
{
    const mth_ph_t *P = &MTH_PH[g->code];
    uint32_t k;
    for (k = 0; k < SYL_NP; k++)
        v[k] = 0;
    for (k = 0; k < 3; k++) {
        if (t <= g->g0) {
            v[SYL_F1 + k] = g->ls[k];
            v[SYL_B1 + k] = g->bs[k];
        } else if (t >= g->g1) {
            v[SYL_F1 + k] = g->le[k];
            v[SYL_B1 + k] = g->be[k];
        } else {
            int32_t u = (int32_t)(t - g->g0), w = (int32_t)(g->g1 - g->g0);
            v[SYL_F1 + k] = g->ls[k] + (g->le[k] - g->ls[k]) * u / w;
            v[SYL_B1 + k] = g->bs[k] + (g->be[k] - g->bs[k]) * u / w;
        }
    }
    switch (P->kind) {
    case MTH_K_VOWEL:
    case MTH_K_DIPH:
    case MTH_K_SONOR:
        if (t < g->asp)
            v[SYL_AH] = MTH_AH_ASP;
        else if (g->asp && t < g->asp + MTH_RAMP_MIN) {  /* voicing takes over from the aspiration */
            v[SYL_AV] = P->av * (int32_t)(t - g->asp + 1u) / (int32_t)(MTH_RAMP_MIN + 1u);
            v[SYL_AH] = MTH_AH_ASP * (int32_t)(g->asp + MTH_RAMP_MIN - t) / (int32_t)(MTH_RAMP_MIN + 1u);
        } else
            v[SYL_AV] = P->av;
        break;
    case MTH_K_NASAL:
        v[SYL_AV] = P->av;
        v[SYL_NAS] = MTH_Q;                              /* syllable.c: pole 270 Hz, zero 450 (Klatt's FNP FNZ) */
        break;
    case MTH_K_HH:
        v[SYL_AH] = MTH_AH_HH;
        break;
    case MTH_K_FRIC:                                     /* rel 0: frication throughout */
    case MTH_K_STOP:                                     /* closure, then the burst from rel */
    case MTH_K_AFFR:                                     /* closure, then frication from rel */
        if (P->kind == MTH_K_FRIC)
            v[SYL_AV] = P->av;
        else if (P->voiced)
            v[SYL_AV] = P->kind == MTH_K_AFFR && t >= g->rel ? P->av : MTH_AV_BAR;
        if (t >= g->rel) {
            v[SYL_AF] = P->af;
            for (k = 0; k < 6; k++)
                v[SYL_P2 + k] = P->band[k];
        }
        break;
    default:
        break;
    }
}

static int mth_is_amp(uint32_t k) { return !(k >= SYL_F1 && k <= SYL_B3); }

/* the boundary between the current segment (g, at its end e1) and the next (gn, at its start e0) */
static void mth_boundary(const mth_geom_t *g, const int32_t *e1, const mth_geom_t *gn, const int32_t *e0, int32_t *o)
{
    uint32_t k;
    int lv = mth_locus(g->kind) && mth_vowelish(gn->kind), vl = mth_vowelish(g->kind) && mth_locus(gn->kind);
    for (k = 0; k < SYL_NP; k++)
        o[k] = mth_is_amp(k) ? (e1[k] + e0[k]) / 2 : gn->kind == MTH_K_PAUSE ? e1[k] : g->kind == MTH_K_PAUSE ? e0[k]
             : (e1[k] + e0[k]) / 2;
    if (!lv && !vl)
        return;
    for (k = 0; k < 3; k++) {                            /* the locus: L + 3/4 (V - L), in Hz */
        const mth_geom_t *c = lv ? g : gn;
        int32_t L = mth_exp2(lv ? g->le[k] : gn->ls[k]), V = mth_exp2(lv ? gn->ls[k] : g->le[k]);
        if (k == 1 && (c->code == MTH_K || c->code == MTH_G || c->code == MTH_NG)) {
            int32_t vel = V + 300 > 1500 ? V + 300 : 1500;   /* velars follow the vowel's F2 (syllable.c) */
            L = vel < L ? vel : L;
        }
        o[SYL_F1 + k] = mth_log2(L + MTH_LOCUS * (V - L) / 8);
    }
}

static void mth_enter(mth_voice_t *m, int32_t seg, const int32_t *left_in)
{
    int32_t e0[SYL_NP], e1[SYL_NP], left[SYL_NP];
    uint32_t k;
    m->seg = (int16_t)seg;
    m->t = 0;
    if (seg >= m->n) {
        if (seg == m->n)                                 /* the tail fades from the last values */
            for (k = 0; k < SYL_NP; k++)
                m->bout[k] = m->cur[k];
        return;
    }
    if (left_in)
        for (k = 0; k < SYL_NP; k++)
            left[k] = left_in[k];
    mth_geom(m, seg, left_in ? left : 0, &m->g);
    mth_body(&m->g, 0, e0);
    mth_body(&m->g, m->g.d - 1u, e1);
    if (!left_in) {                                      /* from silence: the sources start at 0 */
        for (k = 0; k < SYL_NP; k++)
            left[k] = mth_is_amp(k) ? 0 : e0[k];
    }
    for (k = 0; k < SYL_NP; k++)
        m->cin[k] = left[k] - e0[k];
    if (seg + 1 < m->n) {
        mth_geom_t gn;
        int32_t n0[SYL_NP];
        mth_geom(m, seg + 1, e1, &gn);
        mth_body(&gn, 0, n0);
        mth_boundary(&m->g, e1, &gn, n0, m->bout);
    } else
        for (k = 0; k < SYL_NP; k++)
            m->bout[k] = e1[k];
    for (k = 0; k < SYL_NP; k++)
        m->cout[k] = m->bout[k] - e1[k];
    if (seg == m->hold) {                                /* the middle of what is reached whole */
        uint32_t a = m->g.tin > m->g.ain ? m->g.tin : m->g.ain, b = m->g.tout > m->g.aout ? m->g.tout : m->g.aout;
        a = a > m->g.asp ? a : m->g.asp;
        m->hold_t = (uint16_t)(m->g.kind == MTH_K_DIPH ? (m->g.g0 > a ? m->g.g0 : a) : (a + (m->g.d - 1u - b)) / 2u);
    }
}

void mth_set(mth_voice_t *m, uint32_t speed, uint32_t throat, uint32_t mouth, uint32_t crunch)
{
    speed = speed > 127u ? 127u : speed;
    throat = throat > 127u ? 127u : throat;
    mouth = mouth > 127u ? 127u : mouth;
    crunch = crunch > 127u ? 127u : crunch;
    /* 2^((64 - speed) / 32) in Q8: 4x as long at 0, 0.26x at 127 */
    m->fac_q8 = (uint32_t)mth_exp2((int32_t)((64 - (int32_t)speed) * 2048 + (8 << 16)));
    /* 0 -> 0.7, 64 -> 1, 127 -> 1.4, linear each side */
    m->thr_q16 = throat < 64u ? 45875u + throat * 19661u / 64u : 65536u + (throat - 64u) * 26214u / 63u;
    m->mou_q16 = mouth < 64u ? 45875u + mouth * 19661u / 64u : 65536u + (mouth - 64u) * 26214u / 63u;
    m->crunch = (uint8_t)crunch;
    /* hold rate 44100 -> 8000 Hz (step 11889 / 65536), 16 -> 6 bits, the RC pole from 0.957 to 0.434
     * (1 - exp(-2 pi fc / fs) for fc 22 kHz .. 4 kHz, half the hold rate) */
    m->cstep = 65536u - crunch * (65536u - 11889u) / 127u;
    m->csh = (uint8_t)(crunch * 10u / 127u);
    m->ca = (int32_t)(31360u - crunch * (31360u - 14220u) / 127u);
}

void mth_init(mth_voice_t *m)
{
    uint8_t *p = (uint8_t *)m;
    uint32_t i;
    for (i = 0; i < sizeof *m; i++)
        p[i] = 0;
    syl_start(&m->syl, 0, 0, 8, 0);
    m->n = 0;
    m->seg = 2;                                          /* done */
    m->hold = 255;
    mth_set(m, 64, 64, 64, 0);
}

void mth_pitch(mth_voice_t *m, uint32_t f0_q16) { syl_pitch(&m->syl, f0_q16); }

int mth_active(const mth_voice_t *m) { return m->seg < (int32_t)m->n + 2; }

void mth_say(mth_voice_t *m, const uint8_t *ph, uint32_t n) { mth_say_len(m, ph, 0, n); }
void mth_say_len(mth_voice_t *m, const uint8_t *ph, const uint8_t *vlen, uint32_t n)
{
    int legato = mth_active(m);
    uint32_t i;
    if (!legato) {
        syl_start(&m->syl, 0, 0, 8, 0);
        m->cph = 0;
        m->held = m->lp1 = m->lp2 = 0;
    }
    m->ph = ph;
    m->vlen = vlen;
    m->n = (uint8_t)(n < 255u ? n : 255u);
    m->gate = 1;
    m->hold = 255;
    for (i = 0; i < m->n; i++)                           /* the last vowel holds, else the last continuant */
        if (mth_kind(m, (int32_t)i) == MTH_K_VOWEL || mth_kind(m, (int32_t)i) == MTH_K_DIPH)
            m->hold = (uint8_t)i;
    if (m->hold == 255)
        for (i = 0; i < m->n; i++) {
            uint32_t k = mth_kind(m, (int32_t)i);
            if (k == MTH_K_SONOR || k == MTH_K_NASAL || k == MTH_K_FRIC || k == MTH_K_HH)
                m->hold = (uint8_t)i;
        }
    if (!m->n) {
        m->seg = 2;
        return;
    }
    mth_enter(m, 0, legato ? m->cur : 0);
}

void mth_release(mth_voice_t *m) { m->gate = 0; }

void mth_control(mth_voice_t *m)
{
    int32_t v[SYL_NP];
    uint32_t k, t = m->t, len;
    int32_t n = m->n;
    syl_tracks_t *T = &m->syl.tr;
    if (m->seg >= n + 2)
        return;
    if (m->seg < n) {
        const mth_geom_t *g = &m->g;
        mth_body(g, t, v);
        for (k = 0; k < SYL_NP; k++) {
            uint32_t ti = mth_is_amp(k) ? g->ain : g->tin, to = mth_is_amp(k) ? g->aout : g->tout;
            if (t < ti)
                v[k] += m->cin[k] * (int32_t)(ti - t) / (int32_t)ti;
            if (t + to >= g->d)
                v[k] += m->cout[k] * (int32_t)(t + to + 1u - g->d) / (int32_t)to;
            if (mth_is_amp(k))
                v[k] = v[k] < 0 ? 0 : v[k] > MTH_Q ? MTH_Q : v[k];
        }
        if (v[SYL_AH] < m->breath)
            v[SYL_AH] = m->breath;
        len = g->d;
    } else {
        for (k = 0; k < SYL_NP; k++)                     /* the tail fades, the silence holds the formants */
            v[k] = !mth_is_amp(k) ? m->bout[k] : m->seg == n ? m->bout[k] * (int32_t)(MTH_TAIL - 1u - t) / (int32_t)(MTH_TAIL - 1u) : 0;
        len = m->seg == n ? MTH_TAIL : MTH_POST;
    }
    for (k = 0; k < SYL_NP; k++) {
        int32_t x = v[k];
        m->cur[k] = x;
        if (k >= SYL_F1 && k <= SYL_F3) {
            x = mth_exp2(x);
            if (k == SYL_F1)
                x = (int32_t)(((uint32_t)x * m->thr_q16) >> 16);
            else if (k == SYL_F2)
                x = (int32_t)(((uint32_t)x * m->mou_q16) >> 16);
            x = x > 7000 ? 7000 : x;
        } else if (k == SYL_AV || k == SYL_AH || k == SYL_AF)
            x >>= MTH_SRC_SHIFT;
        T->n[k] = 1;
        T->t[k][0] = 0;
        T->v[k][0] = (int16_t)x;
    }
    if (!(m->gate && m->seg == m->hold && t == m->hold_t))
        t++;
    m->t = (uint16_t)t;
    if (t >= len)
        mth_enter(m, m->seg + 1, m->seg + 1 < n ? m->bout : 0);
}

void mth_render(mth_voice_t *m, int32_t *out, uint32_t n)
{
    int32_t buf[SYL_CTL];
    while (n) {
        uint32_t k, j;
        if (m->syl.sub == 0) {
            if (!mth_active(m))
                return;
            mth_control(m);
        }
        k = SYL_CTL - m->syl.sub;
        k = k < n ? k : n;
        if (!m->crunch)
            syl_render(&m->syl, out, k);
        else {
            int32_t sh = m->csh;
            for (j = 0; j < k; j++)
                buf[j] = 0;
            syl_render(&m->syl, buf, k);
            for (j = 0; j < k; j++) {                    /* hold, truncate to the DAC's bits, two RC poles */
                m->cph += m->cstep;
                if (m->cph >= 65536u) {
                    m->cph -= 65536u;
                    m->held = (buf[j] >> sh) * (1 << sh);
                }
                m->lp1 += (int32_t)(((int64_t)(m->held - m->lp1) * m->ca) >> 15);
                m->lp2 += (int32_t)(((int64_t)(m->lp1 - m->lp2) * m->ca) >> 15);
                out[j] += m->lp2;
            }
        }
        out += k;
        n -= k;
    }
}

const uint8_t *mth_phrase(uint32_t i, uint32_t *n)
{
    i = i < MTH_NPHRASE ? i : 0u;
    *n = MTH_PHRASES[i].n;
    return MTH_PHRASES[i].p;
}

static int mth_nucleus(uint8_t b)
{
    uint32_t k = MTH_PH[MTH_CODE(b) < MTH_NPH ? MTH_CODE(b) : 0].kind;
    return k == MTH_K_VOWEL || k == MTH_K_DIPH;
}

uint32_t mth_unit(const uint8_t *ph, uint32_t n, uint32_t mode, uint32_t *cursor, uint32_t *len)
{
    uint32_t s = *cursor < n ? *cursor : 0u, we = s, e, v1, v2;
    if (mode == MTH_PHRASE || !n) {
        *len = n;
        *cursor = 0;
        return 0;
    }
    while (we < n && !(ph[we] & MTH_EOW))
        we++;
    we = we < n ? we + 1u : n;
    e = we;
    for (v1 = s; mode == MTH_SYLL && v1 < we && !(ph[v1] & MTH_EOS); v1++)
        ;
    if (mode == MTH_SYLL && v1 < we) {                  /* the phrase marks its syllables: up to the mark */
        e = v1 + 1u;
    } else if (mode == MTH_SYLL) {                      /* split between nuclei, the odd consonant to the onset */
        for (v1 = s; v1 < we && !mth_nucleus(ph[v1]); v1++)
            ;
        for (v2 = v1 + 1u; v1 < we && v2 < we && !mth_nucleus(ph[v2]); v2++)
            ;
        if (v1 < we && v2 < we)
            e = v1 + 1u + (v2 - v1 - 1u) / 2u;
    }
    *len = e - s;
    *cursor = e >= n ? 0u : e;
    return s;
}

uint32_t mth_parse(const char *s, uint8_t *out, uint32_t max)
{
    uint32_t n = 0, i, k;
    while (*s && n < max) {
        while (*s == ' ')
            s++;
        if (*s == '/') {
            if (n)
                out[n - 1] |= MTH_EOW;
            s++;
            continue;
        }
        if (!*s)
            break;
        for (i = 0; s[i] && s[i] != ' ' && s[i] != '/'; i++)
            ;
        for (k = 0; k < MTH_NPH; k++) {
            uint32_t j;
            for (j = 0; j < i && MTH_PH_NAMES[k][j] == s[j]; j++)
                ;
            if (j == i && MTH_PH_NAMES[k][i] == 0)
                break;
        }
        if (k == MTH_NPH)
            break;
        out[n++] = (uint8_t)k;
        s += i;
    }
    if (n)
        out[n - 1] |= MTH_EOW;
    return n;
}
