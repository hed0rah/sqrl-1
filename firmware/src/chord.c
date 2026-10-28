/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (SCL > CHORD: P_CHRD, P_VOIC): one key, MIDI note or arp input plays a chord. Included by seq.c
 * after kb_map; runs in the audio ISR with the rest of the note input.
 *   CHRD OFF; DIA3 / DIA7: the triad / seventh of the track's scale (ROOT, SCALE) built on the key, every tone
 *        in key (a key outside the scale takes the scale note below it, as QNT SNAP does; CHR builds on the
 *        major scale of ROOT); MAJ MIN DOM7 MAJ7 MIN7 SUS4 POW: that shape on the key, whatever the scale.
 *   VOIC CLOSE (root position); OPEN (the second tone an octave up: 1-5-3); INV1 / INV2 (the lowest tone, then
 *        the next, an octave up); +OCT (the root an octave down added; a seventh drops its fifth). At most
 *        CHORD_MAX notes, notes outside 0..127 left out.
 * The key is mapped first (kb_map: QNT SNAP / WHITE, TRN, the octave), the chord is built on the note it gives.
 * MONO / LEGATO / UNISON parts play the chord's root only; a kit (the DRUM engine, slices: an
 * engine that maps the keys itself) ignores CHRD. Every source keeps the notes it started (kb_chord for a
 * key, mchord for a MIDI note) and its release ends exactly those, so CHRD / VOIC changed while it is held
 * leave nothing hanging. A note several sources hold sounds once and ends with the last of them. */
enum { CH_OFF, CH_DIA3, CH_DIA7, CH_MAJ, CH_MIN, CH_DOM7, CH_MAJ7, CH_MIN7, CH_SUS4, CH_POW };
enum { VC_CLOSE, VC_OPEN, VC_INV1, VC_INV2, VC_BASS, VC_DROP2, VC_DROP3, VC_WIDE, VC_VAR };
#define CHORD_TONES 4u                   /* a chord's tones (a shape, a seventh / ninth chord) */
#if FELUCCA_SHELL
#define CHORD_MAX 8u                     /* the notes a key starts: the tones, the added ones and the BASS (two) */
#else
#define CHORD_MAX 4u
#endif
#define MCHORD_N 24u                     /* MIDI notes held as chords at once (more: their root alone) */

static const int8_t CHORD_SHAPE[CH_POW - CH_MAJ + 1][CHORD_TONES] = {
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1},
};

static uint8_t kb_chord[27][CHORD_MAX], kb_chn[27];   /* the notes key k started (kb_chn 0: none) */
typedef struct { uint8_t id, ch, src, n, note[CHORD_MAX]; } mchord_t;   /* id: track + 1, 0 = free */
static mchord_t mchord[MCHORD_N];
/* the last chord played per track (the CHORD page shows it): its root, notes, and the shape's tones above the
 * root (bit i = i semitones, 0..11) */
static struct { uint8_t root, n, note[CHORD_MAX]; uint16_t mask; uint8_t gen; } chord_last[NTRK];

#if FELUCCA_SHELL
/* SQRL-1's CHORD mode (shell.c), on part shell_chord_part - 1, in the Orchid / ChoralRoot layout: the keys F3..C#4 are
 * the chord block (they never sound), D4..G5 the roots.
 *   black  F#3 DIM, G#3 MIN, A#3 MAJ, C#4 SUS (DIM + MAJ: AUG; MIN + SUS: SUS2)
 *   white  F3 6, G3 m7, A3 M7, C4 9 (a seventh and the 9: the fifth goes)
 *   B3     LATCH (shell.c; seq.c keeps the chord sounding until the next root)
 * No quality held: the key's own chord (DIA3 on the shared scale, QNT SNAP) or MAJ (QNT OFF), set by the shell.
 * BASS (shell_chord_bass): the root an octave down (1), and its fifth (2). */
static volatile uint8_t shell_chord_part, shell_chord_bass, shell_chord_latch;
/* B3 held (shell.c sh_chord_poll): + a key adds that note to every chord (bit pc: the pitch class, 0 C .. 11 B; each
 * chord puts it above its own root, an octave up where it is a chord tone already);
 * + SELECT picks the chord a root plays with no block key held (0 KEY: the key's own, as before). A long hold resets */
static volatile uint32_t shell_chord_add;
static volatile uint8_t shell_chord_qual;
/* CHORD PLAY (shell.c, the ARP button's first page in CHORD): how a chord's notes are struck (chordplay.h, databurn):
 * STRM staggers their starts (seq.c strum_q), TOP lifts the top note's velocity over the inner ones, TUNE JUST tunes
 * the tones to just ratios above the chord's root */
#include "chordplay.h"
static volatile int8_t shell_chord_strm;
static volatile uint8_t shell_chord_top, shell_chord_tune, shell_chord_rpt;
static volatile uint8_t shell_chord_play;               /* the parts CHORD plays, in front or behind (bits): their
                                                         * steps play through STRM, TOP and RPT too (seq.c seq_cp) */
/* PROG (shell.c, the ARP button's page after CHORD PLAY): a progression under the held chord (chordprog.h, databurn).
 * chord_build moves the root of CHORD's part by the degree the progression is on; seq.c steps it (prog_block, key_on) */
#include "chordprog.h"
static volatile uint8_t shell_chord_prog, shell_chord_pevery, shell_chord_pdir, shell_chord_plen;
static cpg_t chord_cpg;
static const char *const N_CQUAL[] = {"KEY", "MAJ", "MIN", "DIM", "AUG", "SUS2", "SUS4", "6", "m6", "7", "MAJ7", "m7",
                                      "m7b5", "dim7", "7sus4", "add9", "9", "MAJ9", "m9", "11", "13"};
#define CQ_N (sizeof N_CQUAL / sizeof N_CQUAL[0])
static const int8_t CQ_IV[CQ_N][5] = {
    {0, -1, -1, -1, -1}, {0, 4, 7, -1, -1}, {0, 3, 7, -1, -1}, {0, 3, 6, -1, -1}, {0, 4, 8, -1, -1},
    {0, 2, 7, -1, -1}, {0, 5, 7, -1, -1}, {0, 4, 7, 9, -1}, {0, 3, 7, 9, -1}, {0, 4, 7, 10, -1},
    {0, 4, 7, 11, -1}, {0, 3, 7, 10, -1}, {0, 3, 6, 10, -1}, {0, 3, 6, 9, -1}, {0, 5, 7, 10, -1},
    {0, 4, 7, 14, -1}, {0, 4, 10, 14, -1}, {0, 4, 11, 14, -1}, {0, 3, 10, 14, -1}, {0, 7, 10, 14, 17},
    {0, 4, 10, 14, 21},
};
static const uint8_t SH_BLACK_KEYS[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};   /* (shell.c: RUDIMENT's lanes) */
enum { CB_6, CB_DIM, CB_M7, CB_MIN, CB_MAJ7, CB_MAJ, CB_LATCH, CB_9, CB_SUS, CB_N };
#define CB_KEYS ((1u << CB_N) - 1u)                     /* keys 0..8 (F3..C#4) */
#define CB_ROOT0 CB_N                                   /* the first root key, D4 */
static uint32_t chord_shape_held(int32_t *iv, uint32_t n)
{
    uint32_t h = fm1_in.notes & CB_KEYS & ~(1u << CB_LATCH);
    int32_t third = n >= 3u ? iv[1] : 4, fifth = n >= 3u ? iv[2] : 7, sev = -1;
    if (!h)                                             /* (no block key: the chord as it is) */
        return n;
#define CB(k) ((h >> (k)) & 1u)
    if (CB(CB_DIM) && CB(CB_MAJ)) {
        third = 4; fifth = 8;
    } else if (CB(CB_MIN) && CB(CB_SUS)) {
        third = 2; fifth = 7;
    } else if (CB(CB_DIM)) {
        third = 3; fifth = 6;
    } else if (CB(CB_SUS)) {
        third = 5; fifth = 7;
    } else if (CB(CB_MIN)) {
        third = 3; fifth = 7;
    } else if (CB(CB_MAJ)) {
        third = 4; fifth = 7;
    }
    sev = CB(CB_M7) ? 10 : CB(CB_MAJ7) ? 11 : CB(CB_6) ? 9 : -1;
    iv[0] = 0;
    iv[1] = third;
    iv[2] = fifth;
    n = 3u;
    if (CB(CB_9)) {                                     /* 9: with a seventh (or 6) the fifth goes */
        if (sev >= 0)
            iv[2] = sev;
        iv[3] = 14;
        n = 4u;
    } else if (sev >= 0) {
        iv[3] = sev;
        n = 4u;
    }
#undef CB
    return n;
}
#endif

/* 1 = the track's keys are a kit: no chords */
static int chord_kit(const track_t *t)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    return e->oneshot || (e->keys && e->keys(t, 0) >= 0);
}

/* the tones of the chord (semitones above *root, ascending, iv[0] = 0) -> their number; DIA may move *root
 * down onto the scale */
static uint32_t chord_tones(const track_t *t, uint32_t mode, int32_t *root, int32_t *iv)
{
    uint32_t n = 0, i;
    if (mode >= CH_MAJ) {
        for (i = 0; i < CHORD_TONES && CHORD_SHAPE[mode - CH_MAJ][i] >= 0; i++)
            iv[n++] = CHORD_SHAPE[mode - CH_MAJ][i];
        return n;
    }
    {
        uint32_t mask = scale_mask(t), deg[12], c = 0, at = 0, guard = 12;
        int32_t r = t->p[P_ROOT];
        if (mask == 0xFFFu)
            mask = SCALE_MASK[1];                       /* CHR: no key of its own, the major scale of ROOT */
        while (guard-- && !((mask >> (uint32_t)((*root - r + 120) % 12)) & 1u))
            (*root)--;                                  /* outside the scale: the scale note below */
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (i == (uint32_t)((*root - r + 120) % 12))
                    at = c;
                deg[c++] = i;
            }
        for (i = 0; i < (mode == CH_DIA7 ? 4u : 3u); i++) {   /* every other note of the scale */
            uint32_t k = at + 2u * i;
            iv[n++] = (int32_t)deg[k % c] + 12 * (int32_t)(k / c) - (int32_t)deg[at];
        }
        return n;
    }
}

/* the voicings after +OCT, on the tones (iv, n; iv[0] = 0 the root):
 *   DROP2 / DROP3: the second / third tone from the top an octave down (DROP3 of a triad: DROP2)
 *   WIDE: every other tone above the root an octave up (1 5 3 7 spread over two octaves)
 *   VAR: each chord struck a voicing of its own (CLOSE INV1 INV2 DROP2 WIDE)
 * then the pull: the tones above the root move an octave toward the last chord this part played, when they sit more
 * than a fifth away from it, so a voicing follows the hand instead of jumping (the root stays where it was played) */
static uint32_t chord_voice_more(const track_t *t, int32_t *iv, uint32_t n, int32_t r)
{
    uint32_t v = (uint32_t)t->p[P_VOIC], i, j, k = trk_index(t);
    int32_t x, c = 0, last = 0;
    for (i = 1; i < n; i++)                             /* ascending */
        for (j = i; j > 0 && iv[j - 1u] > iv[j]; j--) {
            x = iv[j]; iv[j] = iv[j - 1u]; iv[j - 1u] = x;
        }
    if (v == VC_VAR) {
        static uint32_t seed = 0x9E3779B9u;
        static const uint8_t PICK[5] = {VC_CLOSE, VC_INV1, VC_INV2, VC_DROP2, VC_WIDE};
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        v = PICK[(seed >> 8) % 5u];
        for (j = 0; j < (v == VC_INV2 ? 2u : v == VC_INV1 ? 1u : 0u) && n > 1u; j++) {
            x = iv[0] + 12;
            for (i = 1; i < n; i++)
                iv[i - 1u] = iv[i];
            iv[n - 1u] = x;
        }
    }
    if (v == VC_DROP3 && n < 4u)
        v = VC_DROP2;
    if ((v == VC_DROP2 || v == VC_DROP3) && n >= 3u)
        iv[n - (v == VC_DROP2 ? 2u : 3u)] -= 12;
    else if (v == VC_WIDE)
        for (i = 1; i < n; i += 2u)
            iv[i] += 12;
    if (chord_last[k].n > 1u && n > 1u) {               /* the pull */
        for (i = 0; i < chord_last[k].n; i++)
            last += chord_last[k].note[i];
        last /= (int32_t)chord_last[k].n;
        for (i = 0; i < n; i++)
            c += iv[i];
        c /= (int32_t)n;
        x = r + c - last;                               /* (where this chord sits against the last) */
        if (x > 7 || x < -7)
            for (i = 0; i < n; i++)
                if (iv[i] > 0 && iv[i] + (x > 0 ? -12 : 12) >= -12)
                    iv[i] += x > 0 ? -12 : 12;
    }
    return n;
}

/* the chord on note `root` as track t plays it now -> out (ascending), the number of notes (1..CHORD_MAX);
 * *rp its root, *maskp its tones above the root (bit i: i semitones; 0 = no chord). The root alone when CHRD
 * is OFF, on a kit, or for MONO / LEGATO / UNISON (their chord's root) */
#if FELUCCA_SHELL
/* the note of note[0..n) a full chord gives up first: one rule for a chord played (more notes than CHORD_MAX) and a
 * chord recorded (more than a step's STEP_NOTES, seq.c rec_note). Kept longest: the bass (the lowest note), then the
 * root, the third and the seventh. Given up first: the plain fifth (an altered one, b5 or #5, defines the chord and
 * stays), then a note doubling a lower one's pitch class, then the highest extension; of two alike, the higher */
static uint32_t chord_drop(const uint8_t *note, uint32_t n, int32_t root)
{
    uint32_t i, j, lo = 0, best = 0;
    int32_t br = 99;
    for (i = 1; i < n; i++)
        if (note[i] < note[lo])
            lo = i;
    for (i = 0; i < n; i++) {
        int32_t iv = (((int32_t)note[i] - root) % 12 + 12) % 12, r;
        uint32_t dbl = 0;
        for (j = 0; j < n; j++)
            dbl |= note[j] < note[i] && (note[i] - note[j]) % 12u == 0u;
        r = i == lo ? 6 : dbl ? 1 : iv == 0 ? 5 : iv == 3 || iv == 4 || iv == 10 || iv == 11 ? 4 :
            iv == 6 || iv == 8 ? 3 : iv == 7 ? 0 : 2;
        if (r < br || (r == br && note[i] > note[best])) {
            br = r;
            best = i;
        }
    }
    return best;
}
#endif

static uint32_t chord_make(const track_t *t, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    int32_t iv[16], r = (int32_t)root, x;
    uint32_t mode = (uint32_t)t->p[P_CHRD], n, i, j, m = 0;
    uint16_t mask = 0;
    *rp = r;
    *maskp = 0;
    if (!mode || mode > CH_POW || chord_kit(t)) {
        out[0] = (uint8_t)root;
        return 1;
    }
    n = chord_tones(t, mode, &r, iv);
#if FELUCCA_SHELL
    if (shell_chord_part == trk_index(t) + 1u) {
        uint32_t q = shell_chord_qual, a = shell_chord_add;
        if (q && q < CQ_N) {                            /* SELECT's chord in place of the key's own */
            for (n = 0; n < 5u && CQ_IV[q][n] >= 0; n++)
                iv[n] = CQ_IV[q][n];
        }
        n = chord_shape_held(iv, n);
        if (a) {                                        /* the added notes, each its pitch class above this root */
            uint32_t tones = 0, rp = (uint32_t)((r % 12 + 12) % 12), d;
            for (i = 0; i < n; i++)
                tones |= 1u << ((iv[i] % 12 + 12) % 12);
            for (i = 0; i < 12u && n < 12u; i++)
                if ((a >> i) & 1u) {
                    d = (i + 12u - rp) % 12u;
                    iv[n++] = (int32_t)((tones >> d) & 1u ? d + 12u : d);
                }
        }
    }
#endif
    for (i = 0; i < n; i++)
        mask |= (uint16_t)(1u << (iv[i] % 12));
    *rp = r;
    *maskp = mask;
    if (trk_vmode(t) != V_POLY) {                       /* one voice: the root */
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    switch (t->p[P_VOIC]) {
    case VC_OPEN:                                       /* 1-5-3(-7): the second tone an octave up */
        if (n >= 3u)
            iv[1] += 12;
        break;
    case VC_INV2:
    case VC_INV1:
        for (j = 0; j < (t->p[P_VOIC] == VC_INV2 ? 2u : 1u); j++) {   /* the lowest tone an octave up */
            x = iv[0] + 12;
            for (i = 1; i < n; i++)
                iv[i - 1u] = iv[i];
            iv[n - 1u] = x;
        }
        break;
    case VC_BASS:                                       /* the root an octave down; a seventh drops its fifth */
        if (n == CHORD_TONES) {
            iv[2] = iv[3];
            n--;
        }
        for (i = n; i > 0; i--)
            iv[i] = iv[i - 1u];
        iv[0] = -12;
        n++;
        break;
    default:
        break;
    }
    if (t->p[P_VOIC] >= VC_DROP2)
        n = chord_voice_more(t, iv, n, r);
#if FELUCCA_SHELL
    if (shell_chord_part == trk_index(t) + 1u && shell_chord_bass) {   /* BASS: the root, (its fifth,) an octave down */
        iv[n++] = -12;
        if (shell_chord_bass > 1u)
            iv[n++] = -5;
    }
#endif
    for (i = 1; i < n; i++)                             /* ascending */
        for (j = i; j > 0 && iv[j - 1u] > iv[j]; j--) {
            x = iv[j];
            iv[j] = iv[j - 1u];
            iv[j - 1u] = x;
        }
#if FELUCCA_SHELL
    {   /* inside 0..127, each note once; more than CHORD_MAX: the ones chord_drop gives up go */
        uint8_t all[16];
        uint32_t na = 0;
        for (i = 0; i < n && na < 16u; i++) {
            x = r + iv[i];
            if (x < 0 || x > 127 || (na && all[na - 1u] == (uint8_t)x))
                continue;
            all[na++] = (uint8_t)x;
        }
        while (na > CHORD_MAX) {
            j = chord_drop(all, na, r);
            for (i = j + 1u; i < na; i++)
                all[i - 1u] = all[i];
            na--;
        }
        for (m = 0; m < na; m++)
            out[m] = all[m];
    }
#else
    for (i = 0; i < n && m < CHORD_MAX; i++) {          /* inside 0..127, each note once */
        x = r + iv[i];
        if (x < 0 || x > 127 || (m && out[m - 1u] == (uint8_t)x))
            continue;
        out[m++] = (uint8_t)x;
    }
#endif
    if (!m) {
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    return m;
}

/* chord_make for a note played now: a chord is kept in chord_last (the CHORD page shows it) */
static uint32_t chord_build(track_t *t, uint32_t root, uint8_t *out)
{
    int32_t r;
    uint16_t mask;
    uint32_t m, i, k = trk_index(t);
#if FELUCCA_SHELL
    if (shell_chord_prog && shell_chord_part == k + 1u) {   /* PROG: the root on the progression's degree */
        uint32_t sm = scale_mask(t);
        root = (uint32_t)clamp(cpg_root(sm == 0xFFFu ? SCALE_MASK[1] : sm, t->p[P_ROOT], (int32_t)root,
                                        cpg_degree(shell_chord_prog, chord_cpg.i)), 0, 127);
    }
#endif
    m = chord_make(t, root, out, &r, &mask);
    if (m > 1u) {
        chord_last[k].root = (uint8_t)clamp(r, 0, 127);
        chord_last[k].mask = mask;
        chord_last[k].n = (uint8_t)m;
        for (i = 0; i < CHORD_MAX; i++)
            chord_last[k].note[i] = i < m ? out[i] : 0u;
        chord_last[k].gen++;
    }
    return m;
}

/* a chord's name from its root and tones (bit i: i semitones above the root): "C", "Cm7", "F#m7b5", "Gsus4",
 * "A5" -> b (12 bytes) */
static void chord_name(char *b, uint32_t root, uint32_t mask)
{
    uint32_t mi = (mask >> 3) & 1u, ma = (mask >> 4) & 1u, b5 = (mask >> 6) & 1u, p5 = (mask >> 7) & 1u;
    uint32_t s5 = (mask >> 8) & 1u, d7 = (mask >> 9) & 1u, m7 = (mask >> 10) & 1u, M7 = (mask >> 11) & 1u;
    const char *q, *x = "";
    str_cpy(b, N_NOTE[root % 12u], 12);
    if (mi && !ma && b5 && !p5)
        q = d7 ? "dim7" : m7 ? "m7b5" : "dim";
    else if (ma && !mi && s5 && !p5)
        q = m7 ? "+7" : M7 ? "+M7" : "aug";
    else if (ma && !mi)
        q = m7 ? "7" : M7 ? "maj7" : d7 ? "6" : "";
    else if (mi)
        q = m7 ? "m7" : M7 ? "mM7" : d7 ? "m6" : "m";
    else if ((mask >> 5) & 1u) {                        /* no third: suspended */
        q = m7 ? "7" : "";
        x = "sus4";
    } else if ((mask >> 2) & 1u) {
        q = m7 ? "7" : "";
        x = "sus2";
    } else
        q = "5";
    str_cpy(b + str_len(b), q, 12 - str_len(b));
    str_cpy(b + str_len(b), x, 12 - str_len(b));
}

/* the MIDI note (ch, src) of track id - 1 that plays a chord, 0 = none */
static mchord_t *mchord_of(uint32_t ch, uint32_t src, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id && mchord[i].ch == ch && mchord[i].src == src)
            return &mchord[i];
    return 0;
}

/* a MIDI-held chord of track id - 1 sounds note */
static int mchord_held(uint32_t id, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id)
            for (j = 0; j < mchord[i].n; j++)
                if (mchord[i].note[j] == note)
                    return 1;
    return 0;
}

static void mchord_forget(uint32_t track)               /* panic: the track's MIDI chords are gone */
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == track + 1u)
            mchord[i].id = 0;
}
