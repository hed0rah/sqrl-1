/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* SPELL, MOUTH's third EDIT page (shell.c): a phrase built syllable by syllable. A syllable is an onset of up to three
 * consonants, one vowel and a coda of up to three; the phrase is at most SPL_MAX phonemes, MOUTH's bytes with
 * MTH_EOS on each syllable's last phoneme and MTH_EOW on each word's. The knobs edit the syllable under the cursor:
 * CONS the onset's consonant nearest the vowel, VOWEL, CODA the coda's first consonant, LEN the vowel's length. */

#include "lts.c"                        /* English text to phonemes, for letter entry (main loop only) */

#define SPL_MAX MOUTH_UMAX              /* phonemes in a phrase */
#define SPL_NCONS 25u                   /* none, then the consonants by where the mouth makes them */
#define SPL_NVOW 15u

typedef struct {
    uint8_t on[3], non;                 /* the onset, outermost first */
    uint8_t v;                          /* the vowel (a MTH_ code) */
    uint8_t co[3], nco;                 /* the coda */
    uint8_t len;                        /* 0 SHORT, 1 NORMAL, 2 LONG */
    uint8_t eow;                        /* 1: the word ends with this syllable */
} spl_syl_t;

static struct {
    spl_syl_t s[SPL_MAX];
    uint8_t n, cur;                     /* syllables; the one under the cursor */
} spl;

static const uint8_t SPL_CONS[SPL_NCONS] = {
    MTH_PAUSE, MTH_B, MTH_P, MTH_M, MTH_F, MTH_V, MTH_TH, MTH_DH, MTH_D, MTH_T, MTH_N, MTH_S, MTH_Z, MTH_SH, MTH_ZH,
    MTH_CH, MTH_JH, MTH_L, MTH_R, MTH_K, MTH_G, MTH_NG, MTH_HH, MTH_W, MTH_Y};
static const char *const N_SPL_CONS[SPL_NCONS + 1] = {
    "-", "B", "P", "M", "F", "V", "TH", "DH", "D", "T", "N", "S", "Z", "SH", "ZH", "CH", "JH", "L", "R", "K", "G", "NG",
    "HH", "W", "Y", 0};
static const uint8_t SPL_VOW[SPL_NVOW] = {MTH_IY, MTH_IH, MTH_EY, MTH_EH, MTH_AE, MTH_AH, MTH_AA, MTH_AO, MTH_OW, MTH_UH,
                                         MTH_UW, MTH_ER, MTH_AY, MTH_AW, MTH_OY};
static const char *const N_SPL_VOW[SPL_NVOW + 1] = {"IY", "IH", "EY", "EH", "AE", "AH", "AA", "AO", "OW", "UH", "UW",
                                                    "ER", "AY", "AW", "OY", 0};
static const char *const N_SPL_LEN[] = {"SHORT", "NORM", "LONG", 0};

static int spl_vowel(uint32_t code)
{
    uint32_t k = MTH_PH[code < MTH_NPH ? code : 0].kind;
    return k == MTH_K_VOWEL || k == MTH_K_DIPH;
}
static uint32_t spl_count(void)                         /* the phonemes the phrase has */
{
    uint32_t i, n = 0;
    for (i = 0; i < spl.n; i++)
        n += spl.s[i].non + 1u + spl.s[i].nco;
    return n;
}
static uint32_t spl_index(const uint8_t *list, uint32_t n, uint32_t code)
{
    uint32_t i;
    for (i = 0; i < n && list[i] != code; i++)
        ;
    return i < n ? i : 0u;
}

/* syllable i as MOUTH's bytes into ph / len (at most 7 of each); returns how many */
static uint32_t spl_syl_bytes(uint32_t i, uint8_t *ph, uint8_t *len)
{
    const spl_syl_t *y = &spl.s[i];
    uint32_t n = 0, k;
    for (k = 0; k < y->non; k++)
        ph[n++] = y->on[k];
    ph[n++] = y->v;
    for (k = 0; k < y->nco; k++)
        ph[n++] = y->co[k];
    for (k = 0; k < n; k++)
        len[k] = y->len;
    ph[n - 1u] |= (uint8_t)(MTH_EOS | (y->eow || i + 1u == spl.n ? MTH_EOW : 0u));
    return n;
}
/* the whole phrase as bytes; returns how many */
static uint32_t spl_bytes(uint8_t *ph, uint8_t *len)
{
    uint32_t i, n = 0;
    for (i = 0; i < spl.n; i++)
        n += spl_syl_bytes(i, ph + n, len + n);
    return n;
}

/* a phrase's bytes into syllables: its MTH_EOS marks where it has them, else the onset takes the consonant between
 * two vowels (and the later half of a longer cluster); a word with no vowel sings its consonants before AH */
static void spl_load(const uint8_t *ph, const uint8_t *len, uint32_t n)
{
    uint32_t i = 0, w0, w1, k, nv, vpos[SPL_MAX], marks = 0;
    for (k = 0; k < n; k++)
        marks |= ph[k] & MTH_EOS;
    spl.n = 0;
    spl.cur = 0;
    while (i < n && spl.n < SPL_MAX) {
        w0 = i;
        while (i < n && !(ph[i] & MTH_EOW))
            i++;
        w1 = i < n ? i + 1u : n;                        /* the word: [w0, w1) */
        i = w1;
        for (k = w0, nv = 0; k < w1; k++)
            if (spl_vowel(MTH_CODE(ph[k])))
                vpos[nv++] = k;
        if (!nv) {                                      /* (no vowel: AH carries the consonants) */
            spl_syl_t *y = &spl.s[spl.n++];
            y->non = 0;
            for (k = w0; k < w1 && y->non < 3u; k++)
                if (MTH_CODE(ph[k]) != MTH_PAUSE)
                    y->on[y->non++] = (uint8_t)MTH_CODE(ph[k]);
            y->v = MTH_AH;
            y->nco = 0;
            y->len = 1;
            y->eow = 1;
            continue;
        }
        for (k = 0; k < nv && spl.n < SPL_MAX; k++) {
            spl_syl_t *y = &spl.s[spl.n++];
            uint32_t a = k ? vpos[k - 1u] + 1u : w0, b = k + 1u < nv ? vpos[k + 1u] : w1, j, s0, s1;
            if (k) {                                    /* the onset: where the last syllable's coda ended */
                uint32_t m;
                for (m = a; m < vpos[k] && !(marks && (ph[m - 1u] & MTH_EOS)); m++)
                    ;
                s0 = marks ? m : a + (vpos[k] - a) / 2u;
            } else {
                s0 = w0;
            }
            if (k + 1u < nv) {                          /* the coda: up to the next syllable's onset */
                uint32_t m;
                for (m = vpos[k] + 1u; m < b && !(ph[m - 1u] & MTH_EOS); m++)
                    ;
                s1 = marks ? m : vpos[k] + 1u + (b - vpos[k] - 1u) / 2u;
            } else {
                s1 = w1;
            }
            y->non = y->nco = 0;
            for (j = s0; j < vpos[k]; j++)              /* (the last three, nearest the vowel, if more) */
                if (MTH_CODE(ph[j]) != MTH_PAUSE) {
                    if (y->non == 3u) {
                        y->on[0] = y->on[1];
                        y->on[1] = y->on[2];
                        y->non = 2;
                    }
                    y->on[y->non++] = (uint8_t)MTH_CODE(ph[j]);
                }
            y->v = (uint8_t)MTH_CODE(ph[vpos[k]]);
            for (j = vpos[k] + 1u; j < s1 && y->nco < 3u; j++)
                if (MTH_CODE(ph[j]) != MTH_PAUSE)
                    y->co[y->nco++] = (uint8_t)MTH_CODE(ph[j]);
            y->len = len ? (uint8_t)(len[vpos[k]] > 2u ? 2u : len[vpos[k]]) : 1u;
            y->eow = k + 1u == nv;
        }
    }
    if (!spl.n) {                                       /* (never empty) */
        spl.s[0] = (spl_syl_t){{0}, 0, MTH_AA, {0}, 0, 1, 1};
        spl.n = 1;
    }
    while (spl_count() > SPL_MAX) {                     /* (too long: the last syllables go) */
        spl.n--;
        spl.s[spl.n - 1u].eow = 1;
    }
}

/* the cursor's syllable as the knobs show it: CONS VOWEL CODA LEN (indexes into the lists) */
static void spl_knobs(int16_t *cons, int16_t *vow, int16_t *coda, int16_t *len)
{
    const spl_syl_t *y = &spl.s[spl.cur];
    *cons = (int16_t)(y->non ? spl_index(SPL_CONS, SPL_NCONS, y->on[y->non - 1u]) : 0u);
    *vow = (int16_t)spl_index(SPL_VOW, SPL_NVOW, y->v);
    *coda = (int16_t)(y->nco ? spl_index(SPL_CONS, SPL_NCONS, y->co[0]) : 0u);
    *len = y->len;
}
/* a knob turned: k 0 CONS, 1 VOWEL, 2 CODA, 3 LEN to value v. A consonant added only while the phrase has room;
 * none takes the consonant away */
static void spl_edit(uint32_t k, int32_t v)
{
    spl_syl_t *y = &spl.s[spl.cur];
    uint32_t room = spl_count() < SPL_MAX;
    if (k == 0u) {
        v = v < 0 ? 0 : v >= (int32_t)SPL_NCONS ? (int32_t)SPL_NCONS - 1 : v;
        if (!v && y->non)
            y->non--;
        else if (v && y->non)
            y->on[y->non - 1u] = SPL_CONS[v];
        else if (v && room)
            y->on[y->non++] = SPL_CONS[v];
    } else if (k == 1u) {
        y->v = SPL_VOW[v < 0 ? 0 : v >= (int32_t)SPL_NVOW ? SPL_NVOW - 1u : (uint32_t)v];
    } else if (k == 2u) {
        v = v < 0 ? 0 : v >= (int32_t)SPL_NCONS ? (int32_t)SPL_NCONS - 1 : v;
        if (!v && y->nco) {
            y->co[0] = y->co[1];
            y->co[1] = y->co[2];
            y->nco--;
        } else if (v && y->nco) {
            y->co[0] = SPL_CONS[v];
        } else if (v && room) {
            y->co[0] = SPL_CONS[v];
            y->nco = 1;
        }
    } else {
        y->len = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
    }
}
/* OCT+: a copy of the cursor's syllable after it, in the same word (the word now ends with the copy); 0: no room */
static int spl_insert(void)
{
    spl_syl_t *y = &spl.s[spl.cur];
    uint32_t i;
    if (spl.n >= SPL_MAX || spl_count() + y->non + 1u + y->nco > SPL_MAX)
        return 0;
    for (i = spl.n; i > spl.cur + 1u; i--)
        spl.s[i] = spl.s[i - 1u];
    spl.s[spl.cur + 1u] = *y;
    y->eow = 0;
    spl.n++;
    spl.cur++;
    return 1;
}
/* OCT-: the cursor's syllable goes; a word's end moves to the syllable before; 0: the last one stays */
static int spl_delete(void)
{
    uint32_t i;
    if (spl.n <= 1u)
        return 0;
    if (spl.s[spl.cur].eow && spl.cur)
        spl.s[spl.cur - 1u].eow = 1;
    for (i = spl.cur; i + 1u < spl.n; i++)
        spl.s[i] = spl.s[i + 1u];
    spl.n--;
    if (spl.cur >= spl.n)
        spl.cur = (uint8_t)(spl.n - 1u);
    return 1;
}

/* letter entry: GLO held on SPELL makes the keys letters (F3 A .. F5 Z, G5 a space), OCT- a backspace; letting go
 * of GLO speaks the text into the phrase */
#define SPL_TXT 32u
static char spl_txt[SPL_TXT + 1];
static uint8_t spl_tn;
static void spl_type(uint32_t key)
{
    if (spl_tn < SPL_TXT && key < 27u)
        spl_txt[spl_tn++] = key < 26u ? (char)('A' + key) : ' ';
    spl_txt[spl_tn] = 0;
}
static void spl_backspace(void)
{
    if (spl_tn)
        spl_txt[--spl_tn] = 0;
}
/* the typed text into the phrase: its words' phonemes (lts.c), syllables by their vowels; returns the words, 0 when
 * the text held none (the phrase is left as it was) */
static uint32_t spl_speak(void)
{
    uint8_t ph[SPL_MAX];
    uint32_t n = lts_text(spl_txt, ph, SPL_MAX), i, words = 0;
    spl_tn = 0;
    spl_txt[0] = 0;
    if (!n)
        return 0;
    ph[n - 1u] |= MTH_EOW;                              /* (a word cut at the phrase's end still ends) */
    for (i = 0; i < n; i++)
        words += (ph[i] & MTH_EOW) != 0;
    spl_load(ph, 0, n);
    return words;
}

