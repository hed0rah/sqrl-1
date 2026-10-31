/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* The shell: the FM-1's user interface when the firmware is built with FELUCCA_SHELL=1. It replaces Felucca's
 * ui_input, ui_leds and ui_draw (main.c) with a wheel of "nuts". Each nut pairs a SOUND (an engine with its own
 * remembered sound) with a NOTE engine (what plays it: the keys as they are, CHORD, ALIVE, or a note_engine.h
 * engine). Everything under the UI stays Felucca's: the keys still play in the audio ISR (seq.c keyboard_block) on
 * the selected part, with its arp, chords, recording and MIDI OUT. The shell takes the keys only while a button that
 * owns them is held (kb_mask: HOME for the wheel, GLO as SHIFT, SEQ for steps). The user manual is docs/shell.md and
 * docs/wheel.md.
 *
 * Button gestures:
 *   press  PLAY, REC, OCT-, OCT+ act when they go down
 *   tap    the other buttons act on release, under SH_TAP_MS
 *   hold   SH_TAP_MS down, or at once when another control moves while the button is down (it is then a modifier)
 *   long   SH_LONG_MS down (REC: clear; a ring fills on screen from SH_TAP_MS)
 *
 * The screen is drawn in two bands of 120 rows (the canvas holds 240 x 124), each only when the scene's signature
 * changed. Geometry is float (the AC791N has an FPU, -mfprev1): antialiased lines blended into the canvas.
 *
 * Name prefixes:
 *   sh_ / SH_   the shell's own state, helpers and tables
 *   SHP_        pages (SHP_HOME .. SHP_SEQ); SHP_FAM is each page's button family, SHP_IDS its four knobs
 *   SHK_        kinds: what a mode or NOTE engine plays as (SHK_ENGINE, SHK_CHORD, SHK_RUDIMENT, SHK_ALIVE, SHK_NE, SHK_ACID)
 *   SHG_        the shell's own pages that belong to no mode (SCALE, SYSTEM, ABOUT, NIBS)
 *   SHL_        LED levels
 *   SP_         the shell's own parameters (ids from SP_FIRST, kept in sh_sp, not in a part)
 *   SC_         screen colours
 *   AL_         ALIVE, the cellular automaton
 *   BR_         the brand pictures, generated into shell_brand.h by tools/gen_brand.py
 *   CB_         CHORD's key block, from chord.c (CB_KEYS, CB_LATCH, CB_ROOT0)
 *
 * The audio ISR and the main loop share these variables. Each is volatile and lives in the file named.
 *   the shell writes, the ISR reads:
 *     kb_mask (perform.c)                  button bits whose hold gives the keys to the shell (seq.c keyboard_block)
 *     shell_kb_skip (seq.c)                keys an instrument of the shell owns; pressed, they never sound
 *     shell_ne_idx (seq.c)                 the note_engine.h engine in front + 1 (0: none); it gets the keys it owns
 *     shell_ne_on[], shell_ne_part[] (shell.c)    each host channel's engine + 1 and the part it plays on
 *     shell_lanes_part[], shell_lane_len_i[] (seq.c)    each lanes engine's part + 1 (RUDIMENT, LINES), and its lanes'
 *                                          own cycle lengths
 *     shell_chord_part, _bass, _latch, _add, _qual, _strm, _top, _tune (chord.c)    CHORD's settings for chord.c and seq.c
 *     shell_role[], shell_reserve[] (voice.c)       each part's role in a voice steal, and the reserve of a track left
 *     fx_wow, fx_pre, fx_cmode (fx.c)      the shell's settings for the tape echo, the reverb and the chorus
 *   the ISR writes, the shell reads:
 *     shell_lane_pos_i[] (seq.c)           the step each lane of each lanes engine plays next
 *     fx_wet_peak (fx.c)                   the buses' peak since the shell last looked; the shell zeroes it after reading
 *   The ISR also reads sh_sp and sh_ne_val directly (shell_block, shell_ne_block). A single variable is written as
 *   is. A change of several fields that must land together (a pair switch, a lane's steps, a note engine's stop, a
 *   reseed) is made under fm1_irq_off / fm1_irq_on. */

#define SH_TAP_MS 300u
#define SH_LONG_MS 1200u
#define SH_TOAST_MS 1800u
#define SH_POP_MS 1500u
#define SH_SPLASH_MS 1500u             /* the wordmark at power-on (any control skips it) */
#define SH_PEEK_MS 180000u             /* this long with nothing touched and no note held: the peek */
#define SH_SAVE_MS 8000u               /* SAVE's question is dropped if unanswered this long */
#define SH_DSP_RED 90u                 /* SYSTEM's DSP meter turns red at this peak (%), or after a late block */
#define SH_SLOTS 8u                     /* the wheel: one octave of white keys, C4 .. C5 */
#define SH_Y_PIC 20
#define SH_Y_LEG 196
#include "shell_brand.h"                /* the brand pictures; Nibs's face is also ALIVE's NIBS seed */
static int up_store(uint32_t k, const char *name);     /* upreset.c comes after this file in the unity build;
                                                         * ui.c declares the others */

/* ------------------------------------------------------------------------------------------- the modes --- */
/* the shell's own parameters, ids from SP_FIRST. They live in sh_sp, not in a part's p[] */
enum { SP_FIRST = 160, SP_RULE = SP_FIRST, SP_SEED, SP_RATE, SP_RANGE, SP_MUTATE, SP_LANE, SP_BASS, SP_RHY, SP_ELEN, SP_EFILL, SP_EROT,
       SP_WOW, SP_PRE, SP_CMODE, SP_STRM, SP_TOP, SP_TUNE, SP_NE0, SP_NE1, SP_NE2, SP_NE3,
       SP_LMODE, SP_LPOS, SP_LGRN, SP_LSPR, SP_LSCN, SP_SCONS, SP_SVOW, SP_SCODA, SP_SLEN, SP_RPT, SP_GROOVE, SP_CLICK, SP_CIN,
       SP_LNOTE, SP_LOCT, SP_LOOP, SP_DTUNE, SP_DDECY, SP_DTONE, SP_DSNAP, SP_FDRV, SP_FKTRK, SP_FENV,
       SP_MEVERY, SP_MHOW, SP_MBACK, SP_MALT, SP_DENS, SP_SPAN, SP_EEVERY, SP_EAMT, SP_EWHAT, SP_EBACK,
       SP_MX, SP_MY, SP_MCHAOS, SP_MACC, SP_MKICK, SP_MSNARE, SP_MHATS, SP_PROG, SP_PEVERY, SP_PDIR, SP_PLEN,
       SP_ASEED, SP_AROT, SP_ATRN, SP_AOCT, SP_LTRN, SP_LSTY, SP_LPAD, SP_LLOOP, SP_LWAKE, SP_LNAP, SP_FLUX, SP_AFLUX, SP_SCLK, SP_SLEAP, SP_HSTRUM, SP_HSPAN,
       SP_HMTYPE, SP_HMVOIC, SP_HMLEAD, SP_HMVEL, SP_END };   /* (SP_L*: LOOP's keys, eng_loop.c loop_kp) */

/* what a mode is: an engine played from the keys, or one of the shell's own instruments over an engine */
enum { SHK_ENGINE, SHK_CHORD, SHK_RUDIMENT, SHK_ALIVE, SHK_NE, SHK_ACID };   /* SHK_NE: a note_engine.h engine (TONNETZ, ENIGMA,
                                                                            * HARP, S+H, CANON); SHK_ACID: the 303 */
#include "note_engine.h"
#define SH_PINGPONG 0xFFu               /* part 1 or 2, alternating at each switch of the pair in front */
typedef struct {
    const char *name, *about;
    uint16_t color;                     /* the mode's identity (RGB565) */
    uint8_t sides;                      /* its glyph: a regular polygon, 0 = a circle */
    uint8_t engine;                     /* ENGINES[] index (the sound) */
    uint8_t hero;                       /* the parameter ALGORITHM turns */
    uint8_t kind;                       /* SHK_* */
    uint8_t part;                       /* its own part (0..3), or SH_PINGPONG */
    uint8_t bg;                         /* 1: it may keep running behind the mode in front (PLAY on its slot in the wheel) */
    uint8_t lib;                        /* 1: LIBRARY, every other engine (PRESETS walks them, GLO + PRESETS jumps) */
} sh_mode_t;

/* the SOUND catalogue. The wheel's white keys hold a default stash of it (sh_wslot) and the slot picker lets the
 * player change that (docs/wheel.md). DRUMS is the drum kits; RUDIMENT, the lanes that play them, is a NOTE engine.
 * The order is a stored format: the wheel saves catalogue indexes (SH_ST3, SH_ST_PAIR), so entries are only appended */
static const sh_mode_t SH_MODES[] = {
    {"FM6", "plays six-operator FM", RGB(216, 172, 79), 6, ENGI_FM6, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"LIZARD", "draw the wave", RGB(95, 184, 168), 4, ENGI_LIZARD, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"LIBRARY", "walks most engines on one key", RGB(201, 160, 120), 3, 0, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 1},
    {"DRUMS", "plays drum kits", RGB(224, 137, 74), 8, ENGI_DRUM, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"TALK", "speaks words from a chip", RGB(232, 140, 168), 5, ENGI_TALK, P_E1, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"SYLLABLE", "sings syllables", RGB(176, 160, 236), 7, ENGI_SYLLABLE, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"VODER", "plays the 1939 Voder", RGB(232, 168, 96), 9, ENGI_VODER, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    /* the rest of the catalogue, for the slot picker (GLO + a white key in the wheel): every engine on its own */
    {"ORGAN", "pulls tonewheel drawbars", RGB(198, 120, 72), 9, ENGI_ORGAN, P_E7, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"SHOCK", "steepens a sine into brass", RGB(222, 112, 96), 5, ENGI_SHOCK, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"ANALOG", "filters two oscillators", RGB(214, 196, 120), 4, 0, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"PHASE", "bends a cosine, CZ style", RGB(150, 200, 120), 5, 2, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"LOFI", "plays chip waves", RGB(120, 210, 170), 4, 3, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"SAMPLE", "plays sampled instruments", RGB(200, 170, 140), 6, ENGI_SAMPLE, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"VOICE", "sings vowels", RGB(220, 150, 190), 7, 5, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"TRIO", "stacks three oscillators", RGB(130, 170, 220), 3, 6, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"WHEEL", "plays Felucca's organ", RGB(186, 140, 96), 8, 7, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"GRAIN", "scatters grains of a sample", RGB(170, 150, 210), 0, 8, P_E1, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"PHYS", "rings bars and strings", RGB(140, 200, 200), 6, ENGI_PHYS, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"NOISE", "filters noise", RGB(170, 170, 170), 0, 11, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"MOUTH", "sings words, SAM style", RGB(240, 132, 186), 7, ENGI_MOUTH, P_E4, SHK_ENGINE, SH_PINGPONG, 0, 0},
    {"LOOP", "loops a tape you play from the keys", RGB(150, 196, 236), 0, ENGI_LOOP, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
};
#define SH_NMODES NELEM(SH_MODES)
#ifndef FELUCCA_LIBRARY
#define FELUCCA_LIBRARY 0                   /* LIBRARY, every other engine on one key: SQRL-1's sounds are the wheel's eight */
#endif
#define SH_LIB(m) (FELUCCA_LIBRARY && SH_MODES[m].lib)
#define SH_EMPTY 0xFFu                  /* a wheel slot with nothing in it */
/* the wheel as the player set it: which SOUND sits on each white key, which NOTE engine on each black key. GLO + a
 * key in the open wheel picks; the settings block keeps it (SH_ST3). The default stash: */
#define SH_E4 8u                            /* SHOCK: brass for lines, databurn's own (LIBRARY's key before) */
#define SH_G4 (FELUCCA_TALK ? 4u : 19u)     /* G4's SOUND: TALK, or without it MOUTH (SH_MODES 19), SAM-style speech */
#define SH_A4 9u                            /* ANALOG: LINES's acid on a key (its FILTER page) */
#define SH_B4 (FELUCCA_SAMPLE ? 12u : 10u)  /* PHASE: CZ-style bent cosines (SAMPLE's piano, flute and sax with it built) */
#define SH_C5 17u                           /* PHYS: bars and strings, for the lanes as for KEYS and CHORD */
static uint8_t sh_wslot[SH_SLOTS] = {0, 1, SH_E4, 3, SH_G4, SH_A4, SH_B4, SH_C5};
/* the NOTE engines, on the wheel's black keys C#4 D#4 F#4 G#4 A#4: what plays the SOUND. KEYS is the keys as they
 * are. Those on part 2 (LINE) or 3 (BEAT) play by themselves and run when left (SH_LINE, SH_BEAT); the others play
 * on PLAY's parts, 0 or 1 */
typedef struct {
    const char *name, *about;
    uint16_t color;
    uint8_t kind, hero, part, bg;
    const note_engine_t *ne;            /* SHK_NE: the engine; its four parameters fill the MAIN page, its notes go to the part */
    uint8_t clocked;                    /* SHK_NE: 1 plays on the clock (PLAY starts it), 0 from the keys */
} sh_note_t;
#include "tonnetz.c"
#include "enigma.c"
#include "snh.c"
#include "canon.c"
#include "harp.c"
#include "spell.c"                      /* MOUTH's SPELL page: a phrase built syllable by syllable */
/* the order is a stored format: the wheel saves catalogue indexes (SH_ST3, SH_ST_PAIR), so entries are only appended */
static const sh_note_t SH_NOTES[] = {
    {"KEYS", "you play", RGB(196, 196, 186), SHK_ENGINE, 0, SH_PINGPONG, 0, 0},
    {"CHORD", "plays a chord per key", RGB(215, 122, 147), SHK_CHORD, P_VOIC, SH_PINGPONG, 0, 0},
    {"ALIVE", "grows notes from an automaton", RGB(154, 139, 224), SHK_ALIVE, SP_RULE, 2, 1, 0},
    {"TONNETZ", "walks a triad round the Tonnetz", RGB(120, 190, 225), SHK_NE, SP_NE0, 2, 1, &NE_TONNETZ, 1},
    {"ENIGMA", "loops a phrase and mutates it", RGB(225, 196, 110), SHK_NE, SP_NE0, 2, 1, &NE_ENIGMA, 1},
    {"HARP", "strums a chord on the keys", RGB(170, 210, 150), SHK_NE, SP_NE0, SH_PINGPONG, 0, &NE_HARP, 0},
    {"RUDIMENT", "builds a beat on the keys", RGB(224, 137, 74), SHK_RUDIMENT, SP_RHY, 3, 1, 0, 1},   /* the lanes, on
                                                                  * part 4: a kit under DRUMS, notes under any other */
    {"LINES", "plays a line in lanes", RGB(120, 206, 176), SHK_RUDIMENT, SP_RHY, 2, 1, 0, 1},   /* RUDIMENT's lanes
                                                                  * again, its own, on part 3 (LINE): an acid line
                                                                  * over RUDIMENT's drums on BEAT */
    {"S+H", "samples a source into the scale", RGB(214, 150, 210), SHK_NE, SP_NE0, 2, 1, &NE_SNH, 1},
    {"CANON", "answers what you play", RGB(150, 200, 120), SHK_NE, SP_NE0, 2, 1, &NE_CANON, 1},
    {"ACID", "plays a 303 line", RGB(236, 200, 96), SHK_ACID, SP_ASEED, 2, 1, 0, 1},   /* the part's own steps, one
                                                                  * note each with ACC SLD TIE, on LINE */
};
#define SH_NE_FIRST 3u                  /* the first SHK_NE entry */
#define SH_N_RUDIMENT 6u                /* RUDIMENT's index: the lanes engine */
#define SH_N_LINES 7u                   /* LINES: the second lanes engine */
#define SH_N_ACID 10u                   /* ACID: the 303 */
/* the tracks that play themselves: LINE on part 3 (LINES, ALIVE, TONNETZ, ENIGMA, S+H, CANON, one at a time) and BEAT on part 4
 * (RUDIMENT). Each runs whenever the transport does and it has something to play, in front or left; HOME + PLAY in
 * the wheel mutes it. Parts 1 and 2 are PLAY's: what you play, in turn, with takes */
#define SH_LINE 2u
#define SH_BEAT 3u
static const uint8_t SH_WNOTE_DEF[5] = {0, 1, SH_N_LINES, 3, SH_N_RUDIMENT};   /* the black keys' default stash */
static uint8_t sh_wnoteslot[5] = {0, 1, SH_N_LINES, 3, SH_N_RUDIMENT};   /* the black keys' NOTE engines */
/* the note_engine.h engines, by their index i here: SH_NOTES[SH_NE_AT[i]] (appended ones come after LINES) */
static const uint8_t SH_NE_AT[] = {SH_NE_FIRST, SH_NE_FIRST + 1u, SH_NE_FIRST + 2u, 8u, 9u};
#define SH_NNE NELEM(SH_NE_AT)
static uint32_t sh_ne_i(uint32_t n)     /* catalogue entry n's engine index (SH_NNE: not one) */
{
    uint32_t i;
    for (i = 0; i < SH_NNE && SH_NE_AT[i] != n; i++)
        ;
    return i;
}
/* the note_engine.h host. Each engine has its state memory (zeroed, then init, on its first visit), its four values
 * as the shell holds them (an enum counts from 0, since the shell's F_ENUM reads names[v]), and what the ISR last
 * gave it. The engine runs in the audio ISR (shell_ne_block, shell_ne_key); its parameters and the part's SCALE reach
 * it there, between blocks */
#define SH_NE_SZ(t) ((sizeof(t) + 3u) & ~3u)        /* each engine's state, as large as its own (SH_NE_AT's order) */
enum { SH_NEM1 = SH_NE_SZ(tz_t), SH_NEM2 = SH_NEM1 + SH_NE_SZ(eg_t), SH_NEM3 = SH_NEM2 + SH_NE_SZ(hp_t),
       SH_NEM4 = SH_NEM3 + SH_NE_SZ(snh_t), SH_NEM_END = SH_NEM4 + SH_NE_SZ(canon_t) };
static const uint16_t SH_NE_MEMAT[SH_NNE + 1u] = {0, SH_NEM1, SH_NEM2, SH_NEM3, SH_NEM4, SH_NEM_END};
static uint8_t sh_ne_buf[SH_NEM_END] __attribute__((aligned(4), section(".pool")));
#define sh_ne_mem(i) ((void *)(sh_ne_buf + SH_NE_MEMAT[i]))
static int16_t sh_ne_val[SH_NNE][4];
static int16_t sh_sp[SP_END - SP_FIRST];                /* (the shell's own parameters: defined below) */
/* each engine's LINE page (SHP_LINE), the shell's own knobs given to it as NE_P_LINE + k each block: S+H's FLUX, its
 * CLK (the shell's: what steps it) and LEAP; HARP's STRUM and SPAN. SH_NONE: no knob */
#define SH_NE_NO 0xFFu                  /* (SH_NONE, a knob the page leaves empty, before it is defined) */
static const uint8_t SH_NE_LINE[SH_NNE][4] = {{SH_NE_NO, SH_NE_NO, SH_NE_NO, SH_NE_NO}, {SH_NE_NO, SH_NE_NO, SH_NE_NO, SH_NE_NO},
                                              {SP_HSTRUM, SP_HSPAN, SH_NE_NO, SH_NE_NO},
                                              {SP_FLUX, SP_SCLK, SP_SLEAP, SH_NE_NO},
                                              {SH_NE_NO, SH_NE_NO, SH_NE_NO, SH_NE_NO}};
#define SH_NE_SNH 3u                    /* S+H's index here; HARP's is 2 */
#define SH_NE_HARP 2u
static int16_t sh_ne_sent[SH_NNE][5];                   /* the value + 1 last given to param() (4: NE_P_LEN, the
                                                         * part's LEN); 0: not yet given */
static uint8_t sh_ne_ready[SH_NNE];
static param_desc_t sh_ne_pd[SH_NNE][4];
/* two channels, so the LINE track's engine runs under the PLAY pair's: 0 the PLAY pair's (HARP, on the keys), 1 the
 * LINE track's (TONNETZ, ENIGMA, S+H, CANON, on part 3 while it is left too). An engine has one, by its part (sh_ne_ch) */
#define SH_NE_CH 2u
static volatile uint8_t shell_ne_on[SH_NE_CH];          /* the engine each channel runs + 1 (0: none); the ISR reads it */
static volatile uint8_t shell_ne_part[SH_NE_CH];        /* .. and the part it plays on */
static volatile uint8_t shell_ne_poly;                  /* the parts HARP, or TONNETZ on PLAY CHORD, play: POLY there,
                                                         * as CHORD's (voice.c trk_vmode), so a LEGATO SOUND plays the
                                                         * whole triad */
static struct { uint8_t root, playing; uint16_t mask; uint32_t idx, down; } sh_ne_isr[SH_NE_CH];   /* down: S+H's CLK, the
                                                         * newest BEAT voice seen; HARP's STRUM stopped, its clock */
/* S+H's CLK: a voice BEAT started since the last look (ISR), any (CLK BEAT) or lane clk - 2's note (L1 .. L8: under
 * DRUMS the kick, snare, clap, closed hat ..); *seen keeps the newest age looked at (voice.c's ages count up across
 * every part) */
static int sh_clk_hit(uint32_t clk, uint32_t *seen)
{
    const track_t *b = &trk[SH_BEAT];
    uint32_t v, top = *seen, hit = 0, n = clk >= 2u ? lane_note(b, clk - 2u) : 0u;
    for (v = 0; v < NVOICE; v++) {
        const voice_t *x = &b->v[v];
        if (!x->active || x->age <= *seen)
            continue;
        top = x->age > top ? x->age : top;
        hit |= clk == 1u || x->note == n;
    }
    *seen = top;
    return (int)hit;
}
static int16_t sh_ne_off(uint32_t i, uint32_t k)        /* the engine's value is the shell's plus this */
{
    const ne_param_t *q = &SH_NOTES[SH_NE_AT[i]].ne->params[k];
    return q->fmt == NE_ENUM ? q->min : 0;
}
static uint32_t sh_ne_ch(uint32_t i);
static void sh_ne_prepare(uint32_t i)                   /* main loop, with the engine not playing */
{
    const note_engine_t *ne = SH_NOTES[SH_NE_AT[i]].ne;
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        const ne_param_t *q = &ne->params[k];
        param_desc_t *d = &sh_ne_pd[i][k];
        int16_t o = q->fmt == NE_ENUM ? q->min : 0;
        d->label = q->label;
        d->fmt = q->fmt == NE_ENUM ? F_ENUM : q->fmt == NE_PCT && q->max == 127 ? F_PCT : F_INT;   /* (a 0..127 share as %) */
        d->min = (int16_t)(q->min - o);
        d->max = (int16_t)(q->max - o);
        d->def = (int16_t)(q->def - o);
        d->names = q->fmt == NE_ENUM ? q->names : 0;
        d->unit = q->fmt == NE_PCT && q->max != 127 ? "%" : 0;   /* (a percent already: CHNC, DENS) */
        if (!sh_ne_ready[i])
            sh_ne_val[i][k] = d->def;
    }
    if (!sh_ne_ready[i]) {
        memset(sh_ne_mem(i), 0, SH_NE_MEMAT[i + 1u] - SH_NE_MEMAT[i]);
        ne->init(sh_ne_mem(i));
        sh_ne_ready[i] = 1;
    }
    for (k = 0; k < 5u; k++)
        sh_ne_sent[i][k] = 0;                           /* the ISR then gives it all again */
}
/* the generators (the NOTE engines, ALIVE) are live sources: an armed part records what they play, as it does the
 * keys (seq.c rec_note). Once the part's pattern holds notes and plays, it is the take: the generator gives way, silent
 * unless the part is recording over it, so a take never plays twice. A note-off lands for a note the generator
 * started, always, and for no other: a quiet generator's offs would end the take's notes on those pitches. Going
 * quiet (REC let go while the take loops), it ends the notes it still has on at once (sh_gen_give_way) */
/* (sh_pat_notes and sh_gen_quiet live in seq.c: a latched arp gives way by the same rule) */
static int sh_pat_any(const track_t *t)                 /* a note or hit on any of the 64 steps, past LEN too */
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (t->step[k].time == ST_NOTE && (t->step[k].n || t->step[k].hit))
            return 1;
    return 0;
}
static uint32_t sh_gen_on[NTRK][4];                     /* per part, the notes its generator started and has not ended */
static void sh_gen_mark(const track_t *t, uint32_t note)
{
    sh_gen_on[trk_index(t)][(note >> 5) & 3u] |= 1u << (note & 31u);
}
static int sh_gen_give_way(track_t *t)                  /* ISR: sh_gen_quiet; quiet, its notes on end (punch-out) */
{
    uint32_t *w = sh_gen_on[trk_index(t)], n;
    if (trk_index(t) == SH_LINE || !sh_gen_quiet(t))   /* (LINE's generators are its content: never quiet) */
        return 0;
    for (n = 0; n < 128u; n += 32u)
        while (w[n >> 5]) {
            uint32_t b = (uint32_t)__builtin_ctz(w[n >> 5]);
            w[n >> 5] &= ~(1u << b);
            trk_note_off(t, n + b);
        }
    return 1;
}
/* FREEZE (REC + OCT+): what LINE's generator played on each step of the loop, the last time round (sh_frz_poll empties
 * a step just before it plays), for REC + OCT+ to write into LINES' lanes */
static struct { uint8_t note[NSTEP][4], n[NSTEP], vel[NSTEP]; } sh_frz;
static void sh_live_on(track_t *t, uint32_t note, uint32_t vel)   /* ISR */
{
    if (trk_index(t) == SH_LINE) {                      /* LINE's generators record nothing: they are its content */
        uint32_t i = t->seq_idx % NSTEP;
        if (sh_frz.n[i] < 4u)
            sh_frz.note[i][sh_frz.n[i]++] = (uint8_t)note;
        if (vel > sh_frz.vel[i])
            sh_frz.vel[i] = (uint8_t)vel;
    }
    else if (rec_on(t) && shell_gen_part == trk_index(t) + 1u)
        rec_note_machine(t, note, vel);                 /* (a generator: a pass rewrites the steps it plays into) */
    else if (rec_on(t))
        rec_note(t, note, vel);                         /* (HARP: the keys', as REC MODE says) */
    sh_gen_mark(t, note);
    trk_note_on(t, note, vel);
}
static void sh_live_off(track_t *t, uint32_t note)      /* ISR */
{
    uint32_t *w = &sh_gen_on[trk_index(t)][(note >> 5) & 3u], b = 1u << (note & 31u);
    if (!(*w & b))
        return;                                         /* not one it started: a take's note on that pitch plays on */
    *w &= ~b;
    trk_note_off(t, note);
    if (rec_on(t) && trk_index(t) != SH_LINE)
        rec_release(t, note);
}
static void sh_ne_out(track_t *t, const ne_out_t *o)    /* ISR: its events to the part */
{
    int i, quiet = sh_gen_give_way(t);
    for (i = 0; i < o->n; i++) {
        if (o->ev[i].on) {
            if (!quiet)
                sh_live_on(t, o->ev[i].note, o->ev[i].vel ? o->ev[i].vel : 1u);
        } else
            sh_live_off(t, o->ev[i].note);
    }
}
/* the audio ISR, once a block for each channel: it hands over the parameters and SCALE that changed, steps the engine
 * on each new sequencer step of its part while playing (as ALIVE does), and stops it when the transport stops */
static void shell_ne_run(uint32_t c)
{
    uint32_t i = shell_ne_on[c], k;
    const note_engine_t *ne;
    track_t *t;
    ne_out_t o;
    uint16_t mask;
    if (!i--)
        return;
    ne = SH_NOTES[SH_NE_AT[i]].ne;
    t = &trk[shell_ne_part[c] % NTRK];
    for (k = 0; k < 4u; k++)                            /* (the LINE page's, each block: plain values) */
        if (SH_NE_LINE[i][k] != SH_NE_NO)
            ne->param(sh_ne_mem(i), (int)(NE_P_LINE + k), sh_sp[SH_NE_LINE[i][k] - SP_FIRST]);
    for (k = 0; k < 5u; k++) {                          /* (the four knobs, a LEN knob the part's; the part's LEN) */
        int16_t v = k == NE_P_LEN ? (int16_t)clamp(t->p[P_SLEN], 1, NSTEP) : (int16_t)(sh_ne_val[i][k] + sh_ne_off(i, k));
        if (k < 4u && ne->params[k].fmt == NE_LEN)
            continue;
        if (sh_ne_sent[i][k] != v + 1) {
            ne->param(sh_ne_mem(i), (int)k, v);
            sh_ne_sent[i][k] = (int16_t)(v + 1);
        }
    }
    {   /* (TONNETZ is SH_NE_AT's first, its PLAY param 2: 0 CHORD; HARP strums chords always) */
        uint8_t b = (uint8_t)(1u << (shell_ne_part[c] % NTRK));
        int poly = SH_NE_AT[i] == 5u || (i == 0u && sh_ne_val[0][2] + sh_ne_off(0, 2) == 0);
        shell_ne_poly = (uint8_t)(poly ? shell_ne_poly | b : shell_ne_poly & ~b);
    }
    mask = (uint16_t)scale_mask(t);
    if (mask != sh_ne_isr[c].mask || (uint8_t)(t->p[P_ROOT] % 12) != sh_ne_isr[c].root) {
        sh_ne_isr[c].mask = mask;
        sh_ne_isr[c].root = (uint8_t)(t->p[P_ROOT] % 12);
        ne->scale(sh_ne_mem(i), sh_ne_isr[c].root, mask);
    }
    o.n = 0;
    k = i == SH_NE_SNH ? (uint32_t)sh_sp[SP_SCLK - SP_FIRST] % 10u : 0u;   /* (S+H's CLK: BEAT's hits step it) */
    if (song.playing && k) {
        if (sh_ne_isr[c].playing != 1u)
            sh_ne_isr[c].down = vage;               /* (the hits from now on) */
        if (sh_clk_hit(k, &sh_ne_isr[c].down) || sh_ne_isr[c].playing != 1u) {
            sh_ne_isr[c].playing = 1;
            sh_ne_isr[c].idx = t->seq_idx;
            ne->step(sh_ne_mem(i), sh_ne_isr[c].idx, &o);
        }
    } else if (song.playing) {
        if (sh_ne_isr[c].playing != 1u || t->seq_idx != sh_ne_isr[c].idx) {
            sh_ne_isr[c].playing = 1;
            sh_ne_isr[c].idx = t->seq_idx;
            ne->step(sh_ne_mem(i), sh_ne_isr[c].idx, &o);
        }
    } else if (kb_ne_down && !shell_kb_scl && shell_ne_idx == i + 1u && i != SH_NE_HARP) {   /* stopped, a key held on the engine in
                                                         * front: it plays as the transport would, at the part's DIV
                                                         * from step 1, until the last key is up (playing 2) */
        uint32_t per = div_samples((uint32_t)t->p[P_SDIV]);
        if (sh_ne_isr[c].playing != 2u) {
            sh_ne_isr[c].playing = 2;
            sh_ne_isr[c].idx = 0;
            sh_ne_isr[c].down = 0;
            ne->step(sh_ne_mem(i), 0, &o);
        } else if ((sh_ne_isr[c].down += HALF_FRAMES) >= per) {
            sh_ne_isr[c].down -= per;
            sh_ne_isr[c].down = sh_ne_isr[c].down < per ? sh_ne_isr[c].down : 0u;
            sh_ne_isr[c].idx = (sh_ne_isr[c].idx + 1u) % (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
            ne->step(sh_ne_mem(i), sh_ne_isr[c].idx, &o);
        }
    } else if (sh_ne_isr[c].playing) {
        sh_ne_isr[c].playing = 0;
        ne->stop(sh_ne_mem(i), &o);
    } else if (i == SH_NE_HARP && sh_sp[SP_HSTRUM - SP_FIRST]) {   /* STRUM, stopped: 16ths at the tempo */
        sh_ne_isr[c].down += HALF_FRAMES;
        if (sh_ne_isr[c].down >= div_samples(2)) {
            sh_ne_isr[c].down -= div_samples(2);
            sh_ne_isr[c].down = sh_ne_isr[c].down < div_samples(2) ? sh_ne_isr[c].down : 0u;
            ne->step(sh_ne_mem(i), 0, &o);
        }
    }
    sh_ne_out(t, &o);
}
static void shell_ne_block(void)
{
    uint32_t c;
    for (c = 0; c < SH_NE_CH; c++)
        shell_ne_run(c);
}
/* the audio ISR (seq.c keyboard_block): a key the engine in front owns, down or up */
static uint32_t sh_snh_tapped;                          /* S+H's keys given as a tap while stopped: their up is done */
static void shell_ne_key(uint32_t key, int down)
{
    uint32_t i = shell_ne_idx, c;
    ne_out_t o;
    if (!i--)
        return;
    c = sh_ne_ch(i);
    if (shell_ne_on[c] != i + 1u)
        return;
    o.n = 0;
    if (i == SH_NE_SNH && down && !song.playing) {      /* S+H stopped: a key held is the preview (shell_ne_run),
                                                         * so it is a tap, not a held sample: the line moves */
        SH_NOTES[SH_NE_AT[i]].ne->key(sh_ne_mem(i), (uint8_t)key, 1, &o);
        SH_NOTES[SH_NE_AT[i]].ne->key(sh_ne_mem(i), (uint8_t)key, 0, &o);
        sh_ne_out(&trk[shell_ne_part[c] % NTRK], &o);
        sh_snh_tapped |= 1u << (key & 31u);
        return;
    }
    if (!down && (sh_snh_tapped >> (key & 31u)) & 1u) {
        sh_snh_tapped &= ~(1u << (key & 31u));
        if (i == SH_NE_SNH)
            return;
    }
    SH_NOTES[SH_NE_AT[i]].ne->key(sh_ne_mem(i), (uint8_t)key, down, &o);
    sh_ne_out(&trk[shell_ne_part[c] % NTRK], &o);
}
/* channel c's engine leaves (a switch, a mute): every note it has on goes off at once */
static void sh_ne_stop(uint32_t c)
{
    uint32_t i;
    ne_out_t o;
    fm1_irq_off();
    if ((i = shell_ne_on[c]) != 0u) {
        o.n = 0;
        SH_NOTES[SH_NE_AT[i - 1u]].ne->stop(sh_ne_mem(i - 1u), &o);
        sh_ne_out(&trk[shell_ne_part[c] % NTRK], &o);
        shell_ne_on[c] = 0;
        shell_ne_poly &= (uint8_t)~(1u << (shell_ne_part[c] % NTRK));
        if (shell_ne_idx == i)
            shell_ne_idx = 0;
    }
    fm1_irq_on();
}
static uint32_t sh_ne_ch(uint32_t i) { return SH_NOTES[SH_NE_AT[i % SH_NNE]].part == SH_PINGPONG ? 0u : 1u; }
/* engine i onto channel c, playing on part `part` (main loop) */
static void sh_ne_start(uint32_t c, uint32_t i, uint32_t part)
{
    sh_ne_stop(c);
    sh_ne_prepare(i);
    sh_ne_isr[c].playing = 0;
    sh_ne_isr[c].mask = 0xFFFFu;                        /* SCALE is given again */
    shell_ne_part[c] = (uint8_t)part;
    shell_ne_on[c] = (uint8_t)(i + 1u);
}
#define SH_NNOTES NELEM(SH_NOTES)
#define SH_NOTE_SLOTS 5u                /* the black keys of the wheel's octave */
typedef char sh_modes_fit[SH_NMODES < SH_EMPTY ? 1 : -1];   /* a catalogue index must fit a byte */

/* the pages every engine mode has: HOME (the engine's own four), its two EDIT pages and SHAPE (FOLD SYM CRUSH DOWN;
 * LIZARD's: UNI WARP FOLD SYM), ENV, LFO, FX; LIZARD's EDIT also walks DRAW 1-4 and DRAW 5-8 while its MODE is DRAW
 * (sh_page_ok) */
enum { SHP_HOME, SHP_EDIT1, SHP_EDIT2, SHP_FMOP, SHP_FMOP2, SHP_FMENV, SHP_FILTER, SHP_SHAPE, SHP_DRAW1, SHP_DRAW2,
       SHP_LANES1, SHP_LANES2, SHP_VOICE, SHP_GLIDE, SHP_ENV, SHP_ENVD, SHP_LFO, SHP_LFOD, SHP_LFO2, SHP_FX, SHP_INS, SHP_INS2,
       SHP_DELAY, SHP_DELAY2, SHP_REVERB, SHP_CHORUS, SHP_NOTE, SHP_NOTE2, SHP_LANE2, SHP_LINE, SHP_MAP, SHP_MAP2, SHP_CPLAY, SHP_PROG, SHP_ARP, SHP_ARP2, SHP_SEQ,
       SHP_LKEYS,
       SHP_LKEYS2, SHP_SPELL, SHP_DRUM, SHP_EVOLVE, SHP_N };   /* in walk order per family. FMOP..FMENV are FM6's
                                                                  * operator pages (ui_fm6op.c), FILTER is ANALOG's, LANES
                                                                  * DRUM's; the rest are Felucca's stock pages (params.c) */
#define SH_NONE 0xFFu                   /* a knob a page leaves empty */
enum { SHG_NONE, SHG_SCALE, SHG_SYSTEM, SHG_ABOUT, SHG_NIBS, SHG_PROJECT, SHG_SYSTEM2, SHG_PAIR, SHG_LEGAL, SHG_MOVE, SHG_HARM };   /* the shell's own pages (not a mode's); GLO taps
                                                                    * walk SYSTEM .. LEGAL; NIBS hides on ABOUT */
static const uint8_t SHP_FAM[SHP_N] = {B_HOME, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT,
                                       B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_ENV, B_ENV, B_LFO, B_LFO, B_LFO, B_FX, B_FX, B_FX,
                                       B_FX, B_FX, B_FX, B_FX, B_ARP, B_ARP, B_ARP, B_ARP, B_ARP, B_ARP, B_ARP, B_ARP, B_ARP,
                                       B_ARP, B_SEQ,
                                       B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_SEQ};
#define SHP_GLOBAL(p) ((p) >= SHP_DELAY && (p) <= SHP_CHORUS)   /* FX pages of the shared effects (song.g): every part */
static const uint8_t SHP_LZSHAPE[4] = {P_LZUNI, P_LZWARP, P_FOLD, P_SYM};
static const uint8_t SHP_FLT2[4] = {P_IMIX, SP_FDRV, SP_FKTRK, SP_FENV};   /* INSERT 2 under FILTER and COMB */
static const uint8_t SHP_RUDSEQ[4] = {SP_LOOP, P_SDIV, P_SSWING, P_SGATE};   /* RUDIMENT's SEQ: the part's LEN (the
                                                                             * lanes' own cycles are on HOME) */
static const uint8_t SHP_IDS[SHP_N][4] = {
    {0, 0, 0, 0},                                   /* HOME: the engine's own four (engine_t.knob) */
    {P_E0, P_E1, P_E2, P_E3},
    {P_E4, P_E5, P_E6, P_E7},
    {0}, {0}, {0},                                      /* FM6's operator pages turn the patch's bytes, not ids */
    {P_FTYPE, P_E4, P_E5, P_E7},                        /* FILTER: ANALOG's TYPE CUT RES KTR */
    {P_FOLD, P_SYM, P_CRUSH, P_DOWN},                   /* SHAPE; LIZARD uses SHP_LZSHAPE instead */
    {P_LZD0, P_LZD1, P_LZD2, P_LZD3},
    {P_LZD4, P_LZD5, P_LZD6, P_LZD7},
    {P_LN0, P_LN1, P_LN2, P_LN3},                       /* LANES: DRUM's lane levels */
    {P_LN4, P_LN5, P_LN6, P_LN7},
    {P_VOICE, P_GLIDE, P_DETUNE, P_SPRD},               /* VOICE: POLY MONO LEG UNI, the glide, a lead's four */
    {P_GLIDE, P_GLMODE, P_GSPR, P_GORD},                /* GLIDE: its time, RATE / TIME, each note's own (voice.c) */
    {P_ATK, P_DEC, P_SUS, P_REL},
    {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC},            /* ENV DEST */
    {P_LRATE, P_LWAVE, P_LD_PIT, P_LD_FLT},
    {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP},           /* LFO DEST */
    {P_LSYNC, P_LTRIG, P_LPOL, P_LPHASE},               /* LFO 2 (and its PHASE, which LFO 1 here does not show) */
    {P_DIST, P_CHOR, P_DLY, P_REV},
    {P_ITYPE, P_IA, P_IB, P_IC},                        /* INSERT */
    {P_IMIX, SH_NONE, SH_NONE, SH_NONE},
    {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX},               /* DELAY, REVERB, CHORUS: the shared effects (SHP_GLOBAL) */
    {G_TDHEADS, SP_WOW, G_TDSAT, G_TDREV},              /* DELAY 2: the tape echo's heads, wow, saturation, reverse */
    {G_RTYPE, G_RSIZE, G_RDAMP, SP_PRE},
    {G_CRATE, G_CDEPTH, SP_CMODE, SH_NONE},
    {0, 0, 0, 0},                                       /* NOTE: the NOTE engine's own four (SHK_HOME by kind) */
    {SP_RHY, SP_LANE, SP_LNOTE, SP_LOCT},               /* NOTE 2: RUDIMENT's and LINES' rhythm, the lane, its note */
    {SP_LPAD, SP_LLOOP, SP_LWAKE, SP_LNAP},             /* LANE 2: the lane in front's PAD LOOP WAKE NAP (OCT+: LOOP's
                                                         * next seed) */
    {SP_DENS, SP_SPAN, SP_LTRN, SP_LSTY},               /* LINE: LINES' line writer, after ALGORITHM's seed (ACID's
                                                         * TRN is on its MAIN, in degrees: no LINE TRN) */
    {SP_MX, SP_MY, SP_MCHAOS, SP_MACC},                 /* RUDIMENT's MAP (ALGORITHM MAP): where on Grids' map */
    {SP_MKICK, SP_MSNARE, SP_MHATS, SH_NONE},           /* .. MAP 2: each map lane's density */
    {SP_STRM, SP_TOP, SP_TUNE, SP_RPT},                /* CHORD PLAY: CHORD's own, before its arp (the NOTE half) */
    {SP_PROG, SP_PEVERY, SP_PDIR, SP_PLEN},             /* PROG: CHORD's progression under the held chord (chordprog.h) */
    {P_AMODE, P_ARATE, P_AOCT, P_AGATE},                /* the part's arp (seq.c); ARP held: HOLD */
    {P_ASWING, P_APROB, P_AHOLD, P_AORDER},             /* ARP 2 */
    {P_SLEN, P_SDIV, P_SSWING, P_SGATE},                /* the part's pattern; SEQ held: the white keys its steps */
    {SP_LMODE, SP_LPOS, SP_LGRN, SP_LSPR},              /* LOOP's keys */
    {SP_LSCN, SH_NONE, SH_NONE, SH_NONE},
    {SP_SCONS, SP_SVOW, SP_SCODA, SP_SLEN},             /* MOUTH's SPELL */
    {SP_DTUNE, SP_DDECY, SP_DTONE, SP_DSNAP},           /* DRUMS' DRUM: the lane in front's own sound */
    {SP_EEVERY, SP_EAMT, SP_EWHAT, SP_EBACK},           /* EVOLVE: SEQ tapped again (the part's, sh_ev) */
};
typedef char sh_sp_fits[SP_END <= 256 ? 1 : -1];        /* (page ids are bytes) */
static const uint8_t SHG_IDS[3][4] = {{0}, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}, {G_BPM, G_SWING, G_TUNE, G_CLOCK}};
static const uint8_t SHG_SYS2_IDS[4] = {SP_GROOVE, SP_CLICK, SP_CIN, G_ROUTE};   /* SYSTEM 2 (ROUT: the project's MIDI IN) */
static const uint8_t SHG_MOVE_IDS[4] = {SP_MEVERY, SP_MHOW, SP_MBACK, SP_MALT};   /* MOVE, SELECT on from SCALE */
static const uint8_t SHG_HARM_IDS[4] = {SP_HMTYPE, SP_HMVOIC, SP_HMLEAD, SP_HMVEL};   /* HARMONY, SCL again from SCALE */

static const char *const N_SP_RULE[] = {"90", "150", "30", "110", "184", 0};
static const uint8_t SP_RULES[5] = {90, 150, 30, 110, 184};
static const char *const N_SP_RATE[] = {"1/4", "1/8", "1/16", "1/32", 0};   /* the same values as the sequencer's DIV 0..3 */
static const char *const N_SP_BASS[] = {"OFF", "ROOT", "R+5", 0};

/* RUDIMENT's rhythms, turned by ALGORITHM. Each lane has its own cycle, a Euclidean pattern of k hits spread over n
 * steps and turned by rot, so lanes of different lengths turn against each other (a polyrhythm, after POLY 2's idea;
 * the names are moons). The pattern is every lane over the cycles' common length, at most NSTEP 16ths */
typedef struct {
    const char *name, *meter;
    uint8_t lane[8][3];                                 /* n, k, rot per lane (n 0: silent): KICK SNARE CLAP HATCL
                                                         * HATOP TOM RIM BELL */
} sh_rhy_t;
static const sh_rhy_t SH_RHY[] = {
    {"EMPTY", "--", {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}}},     /* a blank grid: LEN 16, every lane =LEN, FILL 0 */
    {"IO", "4/4", {{16, 4, 0}, {16, 2, 4}, {0}, {16, 8, 0}, {0}, {0}, {0}, {0}}},
    {"EUROPA", "4/4", {{16, 5, 0}, {16, 2, 4}, {0}, {12, 9, 0}, {0}, {0}, {0}, {0}}},
    {"TITAN", "4/4", {{16, 3, 0}, {0}, {16, 2, 4}, {16, 7, 1}, {0}, {0}, {12, 5, 2}, {0}}},
    {"GANYMEDE", "4/4", {{16, 4, 0}, {16, 2, 4}, {0}, {0}, {16, 4, 2}, {6, 2, 0}, {0}, {0}}},
    {"CALLISTO", "4/4", {{16, 5, 0}, {0}, {0}, {16, 11, 0}, {0}, {0}, {16, 3, 2}, {8, 3, 0}}},
    {"ENCELADUS", "4/4", {{16, 4, 0}, {0}, {16, 2, 4}, {8, 5, 0}, {0}, {0}, {0}, {12, 4, 1}}},
    {"RHEA", "4/4", {{16, 2, 0}, {16, 1, 8}, {0}, {16, 8, 1}, {0}, {12, 3, 0}, {0}, {0}}},
    {"OBERON", "4/4", {{16, 4, 0}, {0}, {0}, {16, 12, 0}, {16, 4, 2}, {0}, {16, 5, 1}, {0}}},
    {"TETHYS", "3/4", {{12, 2, 0}, {12, 2, 3}, {0}, {12, 6, 0}, {0}, {0}, {0}, {0}}},
    {"DIONE", "3/4", {{12, 4, 0}, {0}, {12, 3, 2}, {12, 6, 1}, {0}, {0}, {0}, {8, 3, 0}}},
    {"MIRANDA", "3/4", {{12, 5, 0}, {0}, {0}, {4, 2, 0}, {0}, {0}, {12, 7, 0}, {0}}},
    {"PHOBOS", "5/4", {{20, 5, 0}, {20, 2, 6}, {0}, {20, 10, 1}, {0}, {0}, {0}, {0}}},
    {"DEIMOS", "5/4", {{20, 7, 0}, {0}, {20, 3, 5}, {10, 7, 0}, {0}, {0}, {0}, {0}}},
    {"TRITON", "7/8", {{14, 4, 0}, {14, 2, 4}, {0}, {14, 7, 0}, {0}, {0}, {0}, {0}}},
    {"CHARON", "7/8", {{14, 5, 0}, {0}, {0}, {7, 4, 0}, {0}, {0}, {14, 3, 2}, {0}}},
    {"HYPERION", "9/8", {{18, 4, 0}, {0}, {18, 2, 6}, {18, 9, 0}, {0}, {0}, {0}, {6, 2, 1}}},
};
#define SH_NRHY (sizeof SH_RHY / sizeof SH_RHY[0])
#define SH_MAP SH_NRHY                   /* ALGORITHM past the moons: Grids' map plays three lanes (seq.c map_hit) */
static const char *const N_SP_RHY[SH_NRHY + 2u] = {"EMPTY", "IO", "EUROPA", "TITAN", "GANYMEDE", "CALLISTO",
                                                   "ENCELADUS", "RHEA", "OBERON", "TETHYS", "DIONE", "MIRANDA",
                                                   "PHOBOS", "DEIMOS", "TRITON", "CHARON", "HYPERION", "MAP", 0};
static const char *const N_SP_CMODE[] = {"CHOR", "ENS", "DIM", 0};
static const char *const N_SP_LSTY[] = {"WALK", "OCTAVE", "PEDAL", "ARP", "CALL", 0};   /* the LINE writer's (sh_lwr_draw) */
static const char *const N_SP_SCLK[] = {"STEP", "BEAT", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", 0};   /* S+H's CLK */
static const char *const N_SP_SLEAP[] = {"ANY", "1", "2", "3", "4", "5", "6", "7", 0};   /* S+H's LEAP (snh.c) */
static const char *const N_SP_HSTRUM[] = {"OFF", "UP", "DOWN", "UP+DN", "RAND", 0};   /* HARP's STRUM (harp.c) */
static const char *const N_SP_HMTYPE[] = {"OFF", "3RD", "5TH", "TRIAD", "7TH", "SUS2", "SUS4", "6TH", "ADD9", "OCT", 0};   /* HARMONY */
static const char *const N_SP_HMVOIC[] = {"CLOSE", "OPEN", "DROP2", "WIDE", 0};
static const char *const N_SP_HMLEAD[] = {"ABOVE", "BELOW", 0};
static const char *const N_SP_LLOOP[] = {"OFF", "1 BAR", "2 BAR", "4 BAR", "8 BAR", 0};
static const char *const N_SP_LMODE[] = {"TAPE", "GRAIN", 0};
static const char *const N_SP_TUNE[] = {"EQUAL", "JUST", 0};
static const char *const N_SP_RPT[] = {"OFF", "1/4", "1/8", "1/8T", "1/16", "1/4.", "1/8.", 0};   /* seq.c RPT_RATE */
static const char *const N_SP_GROOVE[] = {"SWING", "SHUFFLE", "PUSH", "LAID", "HUMAN", 0};   /* core.h GR_* */
static const char *const N_SP_CLICK[] = {"OFF", "REC", "ON", 0};               /* click.c CLICK_* */
static const char *const N_SP_CIN[] = {"OFF", "1 BAR", "2 BARS", 0};           /* seq.c cin_bars */
static const char *N_SP_LANE[9] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL", 0};   /* the lanes'
                                                                  * names: the pieces under DRUMS, else their notes */
static const char *const N_SP_MEVERY[] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS", 0};   /* MOVE (sh_move_poll) */
static const char *const N_SP_MHOW[] = {"MODE", "5TH", "ALT", "RAND", 0};
static const char *const N_SP_EWHAT[] = {"NOTES", "RHYTHM", "BOTH", 0};   /* EVOLVE on LINES' lanes */
static const char *const N_SP_EMOVE[] = {"+3RD", "+5TH", "OCT", "I-IV-V-I", "I-VI-IV-V", 0};   /* .. on a take */
static const char *const N_SP_EDRIFT[] = {"DRIFT", 0};   /* .. on RUDIMENT's beat */
#define SH_FILL0_MS 400u                    /* FILL held at 0 this long before a turn left makes a logic lane */
static const char *const N_SP_LOGIC[] = {"AND", "OR", "XOR", "NOR", "A-B", "FLIP", "LATCH"};   /* FILL -1 .. -7 */
static const param_desc_t SP_DESC[SP_END - SP_FIRST] = {
    {"RULE", F_ENUM, 0, 4, 3, N_SP_RULE, 0},
    {"SEED", F_PCT, -1, 100, 0, 0, 0},              /* -1 is NIBS, a hidden value: his face becomes the history */
    {"RATE", F_ENUM, 0, 3, 2, N_SP_RATE, 0},
    {"RANGE", F_INT, 1, 4, 2, 0, 0},
    {"MUTAT", F_PCT, 0, 100, 0, 0, 0},
    {"LANE", F_ENUM, 0, 7, 0, N_SP_LANE, 0},
    {"BASS", F_ENUM, 0, 2, 0, N_SP_BASS, 0},           /* CHORD (chord.c shell_chord_bass) */
    {"RHY", F_ENUM, 0, SH_MAP, 0, N_SP_RHY, 0},         /* RUDIMENT (ALGORITHM): SH_RHY, EMPTY to start, as the grid is;
                                                         * then MAP */
    {"LEN", F_INT, 1, NSTEP, 0, 0, 0},                  /* the lane in front's (sh_eu): 0 =LEN, else its own (sh_elen) */
    {"FILL", F_INT, -7, NSTEP, 0, 0, 0},                /* left of 0 (lanes 3 to 8): a logic lane (N_SP_LOGIC) */
    {"ROT", F_INT, 0, NSTEP - 1, 0, 0, 0},
    {"WOW", F_PCT, 0, 127, 10, 0, 0},                   /* the tape echo's wow and flutter (fx.c fx_wow) */
    {"PRE", F_INT, 0, 120, 0, 0, "ms"},                    /* the reverb's pre-delay, ms (fx_pre) */
    {"MODE", F_ENUM, 0, 2, 0, N_SP_CMODE, 0},           /* the chorus: CHOR (the stock one), ENS, DIM (fx_cmode) */
    {"STRM", F_INT, -127, 127, 0, 0, 0},                /* CHORD PLAY (chord.c, seq.c key_on): the strum, up / down */
    {"TOP", F_PCT, 0, 127, 0, 0, 0},                    /* the top note over the inner ones */
    {"TUNE", F_ENUM, 0, 1, 0, N_SP_TUNE, 0},            /* EQUAL, JUST */
    {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},   /* SP_NE0..3: the NOTE */
    {"-", F_INT, 0, 0, 0, 0, 0},                                                             /* engine's (sh_ne_desc) */
    {"MODE", F_ENUM, 0, 1, 0, N_SP_LMODE, 0},           /* LOOP's keys: a TAPE head at the key's speed, or GRAINs */
    {"POS", F_PCT, 0, 127, 0, 0, 0},                    /* where on the loop a key starts */
    {"GRAIN", F_PCT, 0, 127, 40, 0, 0},                 /* a grain's length */
    {"SPRAY", F_PCT, 0, 127, 0, 0, 0},                  /* how far grain starts scatter */
    {"SCAN", F_INT, 0, 127, 64, 0, 0},                  /* GRAIN's starts move through the loop, 64 at the tape's speed */
    {"CONS", F_ENUM, 0, SPL_NCONS - 1, 0, N_SPL_CONS, 0},   /* SPELL: the cursor's syllable (spell.c), its onset's */
    {"VOWEL", F_ENUM, 0, SPL_NVOW - 1, 6, N_SPL_VOW, 0},    /* consonant nearest the vowel, the vowel, the coda's first */
    {"CODA", F_ENUM, 0, SPL_NCONS - 1, 0, N_SPL_CONS, 0},   /* consonant, the vowel's length */
    {"LEN", F_ENUM, 0, 2, 1, N_SPL_LEN, 0},
    {"RPT", F_ENUM, 0, 6, 0, N_SP_RPT, 0},              /* CHORD PLAY: the held chord strums again (seq.c rstrum_block) */
    {"GROOV", F_ENUM, 0, GR_N - 1, 0, N_SP_GROOVE, 0},  /* SYSTEM 2: the song's GROOVE (core.h groove_off), SWG its depth */
    {"CLICK", F_ENUM, 0, 2, 0, N_SP_CLICK, 0},          /* .. the metronome (ui.c RP_CLICK, kept with the settings) */
    {"C-IN", F_ENUM, 0, 2, 0, N_SP_CIN, 0},             /* .. the count-in from stop with REC armed (RP_COUNTIN) */
    {"NOTE", F_LNOTE, 0, 127, 0, 0, 0},                 /* RUDIMENT: the lane in front's note (sh_lnote), AUTO 0 */
    {"OCT", F_INT, -2, 2, 0, 0, 0},                     /* .. and the AUTO notes' octave */
    {"LEN", F_STEPS, 1, NSTEP, 16, 0, 0},               /* .. its SEQ: the part's LEN (P_SLEN), the lanes' that follow it */
    {"TUNE", F_INT, -64, 63, 0, 0, 0},                  /* DRUMS' DRUM page: the lane in front's own, from the kit's */
    {"DECY", F_INT, -64, 63, 0, 0, 0},                  /* design (eng_drum.c drum_lane_d), the part's */
    {"TONE", F_INT, -64, 63, 0, 0, 0},
    {"SNAP", F_INT, -64, 63, 0, 0, 0},
    {"DRV", F_PCT, 0, 127, 0, 0, 0},                    /* FILTER's and COMB's extras on INSERT 2 (fx.c ins_ext), the */
    {"KTRK", F_PCT, 0, 127, 0, 0, 0},                   /* part's */
    {"ENV", F_BIPCT, -64, 63, 0, 0, 0},
    {"EVERY", F_ENUM, 0, 4, 0, N_SP_MEVERY, 0},         /* MOVE: the scale moves every so many bars while it plays */
    {"MOVE", F_ENUM, 0, 3, 0, N_SP_MHOW, 0},
    {"BACK", F_INT, 0, 8, 0, 0, 0},                     /* home after that many moves (0: NEVER) */
    {"ALT", F_ENUM, 0, (int16_t)(NELEM(N_SCALE) - 1u), 2, N_SCALE, 0},   /* MOVE ALT's other scale (MIN) */
    {"DENS", F_INT, 1, 32, 10, 0, 0},                   /* LINES' line writer (LINE): how many of the line's steps play */
    {"SPAN", F_INT, 1, 8, 5, 0, 0},                     /* .. how many lanes (scale degrees) the line walks */
    {"EVERY", F_ENUM, 0, 4, 0, N_SP_MEVERY, 0},         /* EVOLVE (sh_ev, the part's): every so many bars it changes */
    {"AMT", F_INT, 1, 8, 2, 0, 0},                      /* .. how many steps or hits change */
    {"WHAT", F_ENUM, 0, 2, 0, N_SP_EWHAT, 0},           /* .. what (a take: MOVE, RUDIMENT: DRIFT; sh_desc) */
    {"BACK", F_INT, 0, 8, 0, 0, 0},                     /* .. home after that many moves (0: NEVER) */
    {"X", F_INT, 0, 127, 64, 0, 0},                     /* RUDIMENT's MAP: Grids' X and Y (seq.c shell_map_x) */
    {"Y", F_INT, 0, 127, 64, 0, 0},
    {"CHAOS", F_PCT, 0, 127, 0, 0, 0},                  /* .. its randomness, the same four cycles over */
    {"ACC", F_PCT, 0, 127, 63, 0, 0},                   /* .. how loud a hit must be to accent (63: Grids') */
    {"KICK", F_PCT, 0, 127, 64, 0, 0},                  /* .. the map lanes' densities */
    {"SNARE", F_PCT, 0, 127, 48, 0, 0},
    {"HATS", F_PCT, 0, 127, 64, 0, 0},
    {"PROG", F_ENUM, 0, (int16_t)(CPG_N - 1u), 0, N_CPG, 0},   /* CHORD's PROG (chordprog.h): the progression */
    {"EVERY", F_ENUM, 0, 3, 1, N_CPG_EVERY, 0},         /* .. playing, the next chord every HALF, 1, 2, 4 bars */
    {"DIR", F_ENUM, 0, 4, 0, N_CPG_DIR, 0},             /* .. which way: FWD REV PEND RAND BROWN */
    {"LEN", F_ENUM, 0, CPG_MAX, 0, N_CPG_LEN, 0},       /* .. ALL, or that many of its chords, round */
    {"LINE", F_INT, 0, 255, 0, 0, 0},                   /* ACID's ALGORITHM: the LINE writer's seed (0: by hand) */
    {"ROT", F_INT, 0, NSTEP - 1, 0, 0, 0},              /* .. the line turned round, steps (sh_acid_rot) */
    {"TRN", F_INT, -7, 7, 0, 0, 0},                     /* .. moved along the scale, degrees, as it plays */
    {"OCT", F_INT, -2, 2, 0, 0, 0},                     /* .. and by octaves */
    {"TRN", F_INT, -12, 12, 0, 0, 0},                   /* LINES' LINE: the line moved by semitones, as it plays */
    {"STYLE", F_ENUM, 0, 4, 0, N_SP_LSTY, 0},           /* .. how the LINE writer draws (LINES' and ACID's) */
    {"PAD", F_INT, 0, 16, 0, 0, 0},                     /* LANE 2, the lane in front's (sh_lx): empty steps after FILL's */
    {"LOOP", F_ENUM, 0, 4, 0, N_SP_LLOOP, 0},           /* .. its chances repeat every 1 2 4 8 bars */
    {"WAKE", F_INT, 1, 8, 1, 0, 0},                     /* .. it plays this many bars (loops with LOOP) */
    {"NAP", F_INT, 0, 8, 0, 0, 0},                      /* .. then rests this many (0: never) */
    {"FLUX", F_PCT, 0, 100, 0, 0, 0},                   /* S+H's LINE: a locked loop's chance a step changes (snh.c) */
    {"FLUX", F_PCT, 0, 100, 0, 0, 0},                   /* ACID's LINE: the chance a step's note moves as it plays */
    {"CLK", F_ENUM, 0, 9, 0, N_SP_SCLK, 0},             /* S+H's LINE: what steps it, its own steps or BEAT's hits */
    {"LEAP", F_ENUM, 0, 7, 0, N_SP_SLEAP, 0},           /* .. the largest move from one note to the next, degrees */
    {"STRUM", F_ENUM, 0, 4, 0, N_SP_HSTRUM, 0},         /* HARP's LINE: a held root strums the plate by itself */
    {"SPAN", F_INT, 2, 15, 8, 0, 0},                    /* .. over this many strings */
    {"TYPE", F_ENUM, 0, HM_N - 1, 0, N_SP_HMTYPE, 0},   /* HARMONY (the NOTE engine's own, sh_hm): what each note */
    {"VOIC", F_ENUM, 0, 3, 0, N_SP_HMVOIC, 0},          /* brings, by the scale's degrees; how it is spread; the */
    {"LEAD", F_ENUM, 0, 1, 0, N_SP_HMLEAD, 0},          /* played note under it or over it; the harmony's level */
    {"VEL", F_INT, 0, 127, 100, 0, 0},
};
static const param_desc_t SH_EV_MOVE = {"MOVE", F_ENUM, 0, 4, 1, N_SP_EMOVE, 0};
static const param_desc_t SH_EV_DRIFT = {"WHAT", F_ENUM, 0, 0, 0, N_SP_EDRIFT, 0};
/* EVOLVE's settings, the part's own: EVERY AMT WHAT BACK (WHAT: a take's MOVE, +5TH to start) */
static int16_t sh_ev[NTRK][4] = {{0, 2, 1, 0}, {0, 2, 1, 0}, {0, 2, 0, 0}, {0, 2, 0, 0}};
/* LINES' ALGORITHM: the LINE writer's seed, in the lanes set's RHY slot (sh_lsp keeps it while RUDIMENT is in front), so
 * UNDO BEAT and a project bring it back as they do a moon. 0 is an empty grid */
static const param_desc_t SH_LWR_DESC = {"LINE", F_INT, 0, 255, 0, 0, 0};
static int16_t sh_sp[SP_END - SP_FIRST] = {3, 0, 2, 2, 0, 0, 0, 0, 16, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 0, 40, 0, 64, 0, 6, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           [SP_MALT - SP_FIRST] = 2,   /* (MOVE ALT: MIN) */
                                           [SP_DENS - SP_FIRST] = 10, [SP_SPAN - SP_FIRST] = 5,
                                           [SP_MX - SP_FIRST] = 64, [SP_MY - SP_FIRST] = 64, [SP_MACC - SP_FIRST] = 63,
                                           [SP_MKICK - SP_FIRST] = 64, [SP_MSNARE - SP_FIRST] = 48,
                                           [SP_MHATS - SP_FIRST] = 64, [SP_PEVERY - SP_FIRST] = 1,
                                           [SP_HSPAN - SP_FIRST] = 8};
/* the lanes engines' lanes, after Euclidean Circles: each lane's LEN, FILL, ROT. LEN 0 (=LEN) follows the part's LEN,
 * the pattern's; 1..64 is the lane's own cycle, the part's LEN at most (seq.c lane_at). Two sets, RUDIMENT's (0) and
 * LINES' (1); sh_li is the one the knobs and the screen use, the lanes engine last in front, and its RHY, LANE and OCT
 * are in sh_sp while the other set's wait in sh_lsp */
#define SH_NLI 2u
static int16_t sh_eu_i[SH_NLI][3][8];
/* each lane's DIV: its step against the part's, -3..5 (x2 3/2 4/3 x1 3/4 2/3 1/2 1/3 1/4; 0 x1, the part's step); seq.c
 * shell_lane_div_i takes it in twelfths (SH_DIV_TW), every frame. A logic lane and MAP's lanes play at x1 */
static int16_t sh_ediv_i[SH_NLI][8];
#define sh_ediv (sh_ediv_i[sh_li])
#define SH_DIV_MIN (-3)
#define SH_DIV_MAX 5
static const uint8_t SH_DIV_TW[9] = {24, 18, 16, 0, 9, 8, 6, 4, 3};
static const char *const SH_DIV_NAME[9] = {"x2", "3/2", "4/3", "x1", "3/4", "2/3", "1/2", "1/3", "1/4"};
static int16_t sh_lnote_i[SH_NLI][8];           /* each lane's note: 0 AUTO (DRUMS: its piece; else the scale's degree) */
/* LANE 2, each lane's: PAD (0..16 empty steps after its FILL's, the cycle LEN + PAD), LOOP (OFF 1 2 4 8 bars: its
 * chances repeat), WAKE (1..8 bars, or loops, it plays) and NAP (0..8 it then rests; 0 never). seq.c plays the last
 * three (shell_lane_loop_i ..), every frame; LOOP's seed is seq.c's shell_lane_seed_i */
enum { SH_LX_PAD, SH_LX_LOOP, SH_LX_WAKE, SH_LX_NAP, SH_LX_N };
static int16_t sh_lx_i[SH_NLI][SH_LX_N][8] = {{[SH_LX_WAKE] = {1, 1, 1, 1, 1, 1, 1, 1}},
                                              {[SH_LX_WAKE] = {1, 1, 1, 1, 1, 1, 1, 1}}};
#define sh_lx (sh_lx_i[sh_li])
static const uint8_t SH_LOOP_BARS[5] = {0, 1, 2, 4, 8};
static uint8_t sh_li;
static int16_t sh_lsp[SH_NLI][3];               /* the set not in front's RHY, LANE, OCT */
static const uint8_t SH_LSP_IDS[3] = {SP_RHY, SP_LANE, SP_LOCT};
#define sh_eu (sh_eu_i[sh_li])
#define sh_lnote (sh_lnote_i[sh_li])
#define shell_lane_len (shell_lane_len_i[sh_li])
#define shell_lane_pos (shell_lane_pos_i[sh_li])
#define shell_lane_note (shell_lane_note_i[sh_li])
static void sh_lanes_use(uint32_t k);
/* CHORD's B3 key: a tap toggles LATCH; held, it edits the chord (keys add notes, SELECT walks the chord list); held
 * long and let go untouched, it resets the standard chords. Tap and reset act on release, and only when nothing else
 * happened during the hold */
#define SH_RING_CHORD 0xFEu                 /* sh.ring during B3's long hold, which is no button's */
#define SH_RING_RESET 0xF0u                 /* sh.ring 0xF0..0xF3: a reset's ring (a family + OCT-, sh_reset) */
#define SH_UNDO_RESET 0x10u                 /* undo.what (ui.c's UNDO_* are 1 2 4): a reset kept in ui.c's undo copy */
/* the shell's own settings, kept over a power cycle in the settings object (favorites.c: row 13's bytes 16..31).
 * [0] is 0x51 once written; then each value + 1 (0: not set, the default). They are written as a settings change is,
 * but only once the change has settled (its page left, or SH_ST_IDLE_MS passed), the transport is stopped and no
 * voice sounds, so the erase's ~40 ms of silence cuts nothing, not even an echo or a reverb still ringing (the buses'
 * peak, fx.c). A held drone or a singing feedback loop can keep it waiting; the header shows a dot meanwhile */
#define SH_ST (favorites.factory[ENGI_SLICE] + 16)
#define SH_ST_MARK 0x51u
#define SH_ST_IDLE_MS 10000u
static const uint8_t SH_ST_IDS[] = {SP_WOW, SP_PRE, SP_CMODE, SP_STRM, SP_TOP, SP_TUNE, SP_RPT};   /* bytes 1.. in this order; a stored format, append only */
#define SH_ST_PAIR 8u                       /* [8] the SOUND slot in front + 1, [9] its NOTE engine + 1 (0: never saved) */
typedef char sh_st_fits[1u + sizeof SH_ST_IDS <= SH_ST_PAIR ? 1 : -1];   /* (the values end before the pair) */
#define SH_ST_SND 10u                       /* [10] the SOUND in front's factory preset + 1, [11] its user slot + 1 (0:
                                             * neither: LIBRARY, or never saved); [12] REC MODE + 1: 1 ADD, 2 ADD +
                                             * KNOBS (NOTES AND KNOBS before), 3 REPLACE, 4 REPLACE + KNOBS, 0 (never
                                             * set) ADD; bytes 13..15 free */
#define SH_ST_KNOBS 12u
#define SH_ST3 (favorites.factory[11] + 16)   /* the wheel, in NOISE's row (presets 128.. no engine has): [0] 0x54
                                              * once saved, [1..8] each white key's SOUND (catalogue index + 1, 0xFE
                                              * empty), [9..13] each black key's NOTE engine + 1 */
#define SH_ST3_MARK 0x54u
#define SH_ST3_MARK_V1 0x52u                  /* (RUDIMENT a SOUND, index 3: its white key kept as DRUMS, the black
                                               * keys back to the default, a saved pair on 3 given RUDIMENT) */
#define SH_ST3_MARK_V2 0x53u                  /* (before LINES: the white keys kept, the black keys back to the
                                               * default, so LINES is on F#4) */
static uint8_t sh_st3_v1;                     /* the settings were saved so */
#define SH_ST2 (favorites.factory[12] + 16)   /* the note engines' four values each, in FM6's row (presets 128.. no
                                              * engine has): value - min + 1 (0: the default); 16 bytes, four engines */
#define SH_ST4 (favorites.factory[ENGI_DRUM] + 16)   /* .. and from the fifth on (S+H, CANON), in DRUM's row likewise */
static uint8_t *sh_st2(uint32_t i) { return i < 4u ? SH_ST2 + 4u * i : SH_ST4 + 4u * (i - 4u); }
typedef char sh_st4_fits[SH_NNE <= 8u ? 1 : -1];
static struct { uint8_t pending, page, mode; uint32_t t; } sh_st;
/* recording's takes (sh_take_note, below) */
static step_t sh_undo[NSTEP];
static motion_event_t sh_mundo[MOTION_MAX];      /* the part's knob moves before the take (or the clear), for UNDO */
static volatile uint8_t sh_mundo_n, sh_mundo_ok;   /* (ok: they are kept; a rhythm load keeps none) */
/* REC MODE (REC + SELECT): what an armed REC records. ADD: notes in with what a step holds; REPLACE: a step's
 * earlier notes give way to this pass's (seq.c rec_replace); + KNOBS: knob moves too (motion_knobs) */
static const char *const N_SH_RECM[] = {"ADD", "ADD + KNOBS", "REPLACE", "REPLACE + KNOBS"};
static uint32_t sh_rec_mode(void) { return motion_knobs | (uint32_t)rec_replace << 1; }
static void sh_rec_mode_set(uint32_t m)
{
    motion_knobs = (uint8_t)(m & 1u);
    rec_replace = (uint8_t)((m >> 1) & 1u);
}
static uint8_t sh_undo_ls[NLANE][NSTEP] __attribute__((section(".pool")));   /* .. and its lanes' own bytes */
static int16_t sh_undo_lx[SH_LX_N][8];          /* (a rhythm's undo: LANE 2 with the lanes) */
static int16_t sh_undo_eu[3][8], sh_undo_rhy;   /* a rhythm's undo: the lanes' LEN FILL ROT and the RHYTHM it replaced */
static int16_t sh_undo_div[8];                  /* .. and their DIV */
static uint32_t sh_rhy_sum_i[SH_NLI];           /* the beat the last rhythm load left (sh_beat_sum), 0: none yet */
#define sh_rhy_sum (sh_rhy_sum_i[sh_li])
static volatile struct {
    uint8_t live;                       /* the take under way has kept its steps (ISR sets, the shell clears) */
    uint8_t have, part, n;              /* an undo is kept, for that part; takes since the pattern was empty */
    uint8_t rhy;                        /* the undo kept is the beat a rhythm load replaced (UNDO BEAT) */
    uint8_t note;                       /* .. and the NOTE engine it was kept under (LINES and ALIVE share a part) */
    uint8_t rec, first;                 /* last frame: recording, a first take under way (LEN open) */
    uint8_t fpart;                      /* the part a first take is on */
    uint8_t dbl;                        /* the undo kept is the pattern before a DOUBLE (1) or a HALF (2) (no take in it) */
    int16_t len0, ulen;                 /* LEN before a first take; LEN the undo puts back */
    uint16_t idx;                       /* the step seen last frame */
    uint16_t start;                     /* the step a first take began on */
    uint8_t mid;                        /* .. with the transport already running (armed while playing) */
    uint8_t ran;                        /* last frame: the transport ran */
    uint32_t steps;                     /* a first take's whole steps so far */
    uint8_t frac;                       /* .. and the one running, in 1/256 */
    uint8_t empty;                      /* last frame, not recording: the pattern in front was empty */
} sh_take;
static void sh_take_poll(void);
static void sh_move_poll(void);
static void sh_ev_poll(void);
static void sh_move_home(const track_t *t, uint32_t id);
static void sh_take_undo(void);
static uint32_t sh_reset_family(void);              /* (the resets: a family's button held) */
static int sh_take_here(void);
/* a sound load on a part by PRESETS, LIBRARY's jump or a Program Change: REC held undoes it (ui.c kept the copy from
 * before the first load of the browse, undo_swap); a pair switch or a project load ends it */
static volatile struct { uint8_t on, part, newer; } sh_sndu;   /* newer: the load came after the last take or beat change */
static int sh_sound_undoable(void);
#if FELUCCA_FLASH
#include "phrase_rec.c"                         /* MOUTH's phrases U1..U8 over power-off and in the backup */
#endif
static void sh_store_load(void);
static int sh_enter_saved(void);
static uint8_t sh_restored;             /* a backup put the settings back (editor_backup.c): the shell's own reload */
static int autosave_boot(int allowed);   /* (project.c, after this file) */
static int autosave_held(void);         /* .. an unrestored autosave kept: none written this session until a save or load */
static uint8_t sh_as_boot;              /* main.c at power-on: the autosave waits for the shell to come up (1 a boot after
                                         * a crash, hang or failed boot: not restored; 2 a clean boot); 3 the music came
                                         * back (the splash's toast says so); 0 done */
static void sh_store_poll(void);
static void sh_disc(float cx, float cy, float r, uint16_t c);
static struct { uint8_t down, used; uint32_t t0; } sh_b3;
/* NOTE MAIN of the shell's instruments, by kind (KEYS has none: its NOTE view is ARP and SEQ) */
static const uint8_t SHK_HOME[6][4] = {
    {0, 0, 0, 0},
    {SP_BASS, P_VOIC, P_QUANT, P_AMODE},                /* CHORD: BASS, the voicing, KEY (QNT SNAP: the scale's
                                                         * chords; OFF: any root, MAJ), the arp as a strum */
    {SP_LANE, SP_ELEN, SP_EFILL, SP_EROT},              /* RUDIMENT's MAIN: the lane in front and its cycle, LEN FILL
                                                         * ROT (ALGORITHM the rhythm; NOTES its note, the AUTO notes'
                                                         * octave) */
    {SP_SEED, SP_RATE, SP_RANGE, SP_MUTATE},            /* ALIVE */
    {SP_NE0, SP_NE1, SP_NE2, SP_NE3},                   /* a note_engine.h engine: its own four */
    {SP_LOOP, SP_AROT, SP_ATRN, SP_AOCT},               /* ACID's MAIN: LEN, ROT, TRANSPOSE, OCT */
};

/* a mode's own sound, kept while another mode plays: its parameters, its preset and user slot. What lives per part
 * outside p[] (FM6's patch, fm6_patch; DRUMS' sound per drum, drum_lane_d) is kept only for the modes that can play
 * that engine (sh_snapx_of: FM6, LIBRARY, DRUMS), not in every mode's record */
typedef struct {
    int16_t p[P_COUNT];                 /* only its SOUND ids are put back (sh_half) */
    uint8_t preset, user, valid, eng;   /* eng: the engine it was on (LIBRARY walks several) */
    int16_t iext[IX_N];                 /* FILTER's and COMB's DRV KTRK ENV (fx.c ins_ext: per part, outside p[]) */
} sh_snap_t;
typedef struct {
    uint8_t fm6[FP_SIZE + 1u], fm6_slot;
    int16_t drl[DRL_N][DV_NLANE];
} sh_snapx_t;
/* only the wheel's SOUNDs keep one (sh_wheel_avail): FM6 LIZARD DRUMS SHOCK ANALOG PHYS MOUTH always, the rest by their
 * build flags; the last record is a sink for a SOUND not built in (never kept, never saved) */
#define SH_NSNAP (7u + FELUCCA_PHASE + FELUCCA_TALK + FELUCCA_WHEEL + FELUCCA_LOFI + FELUCCA_VOICE + FELUCCA_TRIO + \
                  FELUCCA_GRAIN + FELUCCA_NOISE + FELUCCA_SYLLABLE + FELUCCA_VODER + FELUCCA_SAMPLE + FELUCCA_ORGAN + \
                  FELUCCA_LOOP + FELUCCA_LIBRARY)
static sh_snap_t sh_snap[SH_NSNAP + 1u] __attribute__((section(".pool")));
static int sh_wheel_avail(uint32_t m);
static sh_snap_t *sh_snap_of(uint32_t m)
{
    uint32_t i, k = 0;
    if (!sh_wheel_avail(m)) {
        sh_snap[SH_NSNAP].valid = 0;
        return &sh_snap[SH_NSNAP];
    }
    for (i = 0; i < m; i++)
        k += (uint32_t)sh_wheel_avail(i);
    return &sh_snap[k < SH_NSNAP ? k : SH_NSNAP];
}
static sh_snapx_t sh_snapx[3] __attribute__((section(".pool")));
static sh_snapx_t *sh_snapx_of(uint32_t m)
{
    const sh_mode_t *md = &SH_MODES[m % SH_NMODES];
    return md->lib ? &sh_snapx[1] : md->engine == ENGI_FM6 ? &sh_snapx[0] : md->engine == ENGI_DRUM ? &sh_snapx[2] : 0;
}
/* a NOTE engine's own, kept while another plays: its NOTE ids (sh_half) and the pattern. They follow it from part to part.
 * LINE's generators (ALIVE and the SHK_NE engines on part 3) record nothing, so they share one pattern: LINE's */
typedef struct {
    int16_t p[P_COUNT];
    uint8_t valid;
} sh_nrec_t;
static sh_nrec_t sh_nrec[SH_NNOTES] __attribute__((section(".pool")));
#define SH_NREC_GEN 5u                  /* the generators' shared pattern */
static const uint8_t SH_NREC_BANK[] = {0, 1, SH_NREC_GEN, SH_NREC_GEN, SH_NREC_GEN, 2, 3, 4, SH_NREC_GEN, SH_NREC_GEN, 6};
typedef char sh_nrec_banks[NELEM(SH_NREC_BANK) == SH_NNOTES ? 1 : -1];   /* (a new NOTE engine: its bank here too) */
static step_t sh_nstep_bank[SH_NREC_GEN + 2u][NSTEP] __attribute__((section(".pool")));   /* (+ ACID's own) */
#define sh_nsteps(r) (sh_nstep_bank[SH_NREC_BANK[r]])
/* a PLAY take (KEYS, CHORD, HARP: banks 0..2) is the pair's, not its NOTE engine's: FM6's take on KEYS and ANALOG's
 * are two. Those three banks park the takes of pairs left off their part, each keyed by its pair (m | n << 8); a new
 * one goes in a free slot, else one whose pair still plays on a part (its steps are there), else the oldest */
#define SH_NPK 3u
#define SH_PK_NONE 0xFFFFu
static uint16_t sh_pk_pair[SH_NPK] = {SH_PK_NONE, SH_PK_NONE, SH_PK_NONE};
static uint32_t sh_pk_age[SH_NPK], sh_pk_n;
static int sh_pk_kind(uint32_t n) { return SH_NREC_BANK[n % SH_NNOTES] < SH_NPK; }
static int32_t sh_pk_find(uint32_t m, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < SH_NPK; i++)
        if (sh_pk_pair[i] == (uint16_t)(m | n << 8))
            return (int32_t)i;
    return -1;
}

/* ------------------------------------------------------------------------------------------- the state --- */
static struct {
    uint8_t ready, mode, prev, page[SH_NMODES], global;
    uint8_t sclback;                    /* the shell page SCL left (a second SCL goes back to it) */
    uint8_t note, wnote, prevnote;      /* the NOTE engine in front, the wheel's black key picked (0..4), the one before */
    uint8_t pick, pickpos, pickcur;     /* the slot picker: 1 a white key's SOUND, 2 a black key's NOTE; its slot, the
                                         * catalogue entry under the cursor */

    uint8_t lastedit[SH_NMODES], lastarp[SH_NNOTES];   /* the flip: each half's page as it was left */
    uint8_t lastsnd[SH_NMODES], lastnv[SH_NNOTES];     /* TAB (HOME tapped): each view's page as it was left */
    uint8_t tab_page;                   /* the page before the last TAB (a double tap undoes it) */
    uint8_t part;                       /* the foreground mode's part */
    uint8_t pp;                         /* the ping-pong part used last (0 or 1) */
    uint8_t relmask;                    /* parts left and still ringing, a bit per part; they are SR_REL in a steal */
    uint8_t wheel, wsel;                /* the wheel open, its white key picked (0..7; SH_SLOTS = SYSTEM, the centre) */
    uint32_t wheel_t0;
    uint8_t hot;                        /* the knob card turned last (4 = none) */
    uint32_t hot_t;
    char toast[48];
    uint8_t toast_undo;                 /* the toast's news can be undone: HOLD REC: UNDO under it */
    uint32_t toast_t;
    uint8_t pop;                        /* 1 PRESETS, 2 ALGORITHM */
    uint32_t pop_t;
    uint8_t mom, mom_keep;              /* MOMENTARY (SAVE held): the sound as it was, engine, preset and FM6 patch too */
    sh_snap_t mom_s;
    sh_snapx_t mom_x;
    uint8_t ring;                       /* a destructive hold filling (the button label + 1) */
    uint8_t ring2;                      /* REC's: 0 the first ring (UNDO TAKE), 1 the second (CLEAR: notes and knob moves) */
    uint32_t ring_t0;
    uint32_t last_home;                 /* fm1_ms of the last HOME tap (a double tap: the pair before) */
    uint32_t sig;                       /* the screen's signature */
    uint8_t help;                       /* GLO + a button: that button's card (label + 1) */
    uint32_t splash_t;                  /* the power-on wordmark: when it went up (0: gone) */
    uint32_t idle_t;                    /* the last button, key or knob (Nibs's peek) */
    uint8_t save, save_slot;            /* SAVE tapped: the question is up (OCT+ yes, OCT- no); the user slot it would write */
    uint8_t pslot, pq, pgen;            /* PROJECTS: the slot under the cursor (4: NEW), its question (1 save, 2 load, 3
                                         * NEW), names read */
    uint8_t dsp, dsp_pk;                /* SYSTEM's meter: the audio ISR's load, its average and last second's peak
                                         * (% of a 2.9 ms half; audio.c) */
    uint32_t dsp_t;
    uint8_t rec_erase;                  /* REC held as ERASE after a key was touched: its arm is undone, and it will not clear */
    uint8_t rec_sel;                    /* REC held with SELECT turned: the same, its arm undone once */
    uint32_t save_t;
} sh;
static struct {
    uint32_t t0;
    uint8_t down, hold, lng, combo;
} sh_b[NB];

static int sh_sound_undoable(void)
{
    return sh_sndu.on && sh_sndu.part == sh.part && undo.trk == sh.part + 1u && (undo.what & (UNDO_SOUND | SH_UNDO_RESET));
}
static int sh_take_here(void)                           /* the undo kept is the pair in front's (its part, its NOTE) */
{
    return sh_take.have && sh_take.part == sh.part && sh_take.note == sh.note;
}
static int sh_sound_first(void)                         /* REC held undoes the sound: it was replaced last, or no take */
{
    return sh_sound_undoable() && (sh_sndu.newer || !sh_take_here());
}
static const uint8_t SH_PRESS[] = {B_PLAY, B_REC, B_OCTDN, B_OCTUP};
static int sh_on_press(uint32_t b)
{
    uint32_t i;
    for (i = 0; i < NELEM(SH_PRESS); i++)
        if (SH_PRESS[i] == b)
            return 1;
    return 0;
}
static int sh_held(uint32_t b) { return sh_b[b].down; }
/* the FX hold layer (perform.c): FX held with neither GLO, HOME nor SEQ, the wheel closed. The keys play the master's
 * effects (seq.c keyboard_block), KNOB 1..4 are its macros; the map shows once FX is a hold. A tap still walks the
 * FX pages */
static int sh_fx_ok(void) { return !sh.wheel && !sh_held(B_GLO) && !sh_held(B_HOME) && !sh_held(B_SEQ); }
static int sh_fx_on(void) { return sh_held(B_FX) && sh_fx_ok(); }
static int sh_fx_shown(void) { return sh_fx_on() && sh_b[B_FX].hold; }
/* a stamp (fm1_ms | 1, 0 = none) younger than ms: signed, so a stamp from this very frame counts (age -1) */
static int sh_fresh(uint32_t t, uint32_t ms) { return t && (int32_t)(fm1_ms - t) < (int32_t)ms; }
static const sh_mode_t *sh_cur(void) { return &SH_MODES[sh.mode]; }
/* the kind the pair in front plays as: its NOTE engine's */
static uint32_t sh_kind(void) { return SH_NOTES[sh.note].kind; }
static const sh_note_t *sh_ncur(void) { return &SH_NOTES[sh.note]; }
static uint32_t sh_eng(void) { return trk[sh.part].eng_req % NENGINES; }   /* the SOUND's engine (LIBRARY: any) */
/* a page of the NOTE half (ARP, SEQ) in front, for a pair with a NOTE engine of its own */
static int sh_note_half(void);
/* what ALGORITHM turns: the NOTE engine's own in the NOTE view, the SOUND's in the SOUND view */
static uint32_t sh_hero_id(void)
{
    return sh_note_half() && sh.note ? SH_NOTES[sh.note].hero : SH_MODES[sh.mode].hero;
}
static track_t *sh_trk(void) { return &trk[sh.part]; }

static void sh_toast(const char *a, const char *b)
{
    uint32_t n = 0;
    str_cpy(sh.toast, a, sizeof sh.toast);
    if (b) {
        n = str_len(sh.toast);
        str_cpy(sh.toast + n, b, sizeof sh.toast - n);
    }
    sh.toast_t = fm1_ms | 1u;
    sh.toast_undo = 0;
}
static void sh_toast_undo(const char *a, const char *b)   /* news that REC held takes back */
{
    sh_toast(a, b);
    sh.toast_undo = 1;
}

/* the white keys from C4 (key 7: F3 + 7 = C4) hold the wheel's slots */
static int32_t sh_slot_key(uint32_t slot)
{
    static const uint8_t K[SH_SLOTS] = {7, 9, 11, 12, 14, 16, 18, 19};   /* C D E F G A B C */
    return slot < SH_SLOTS ? K[slot] : -1;
}
/* a catalogue entry's white key in the wheel (SH_SLOTS: none), a NOTE engine's black key (5: none) */
static uint32_t sh_mode_pos(uint32_t m)
{
    uint32_t i;
    for (i = 0; i < SH_SLOTS && sh_wslot[i] != m; i++)
        ;
    return i;
}
static uint32_t sh_note_pos(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 5u && sh_wnoteslot[i] != n; i++)
        ;
    return i;
}
/* the NOTE engine on black key i (KEYS on an empty one) */
static uint32_t sh_note_at(uint32_t i) { return i < 5u && sh_wnoteslot[i] < SH_NNOTES ? sh_wnoteslot[i] : 0u; }

/* the black keys C#4 D#4 F#4 G#4 A#4 hold the NOTE engines */
static int32_t sh_note_key(uint32_t n)
{
    static const uint8_t K[SH_NOTE_SLOTS] = {8, 10, 13, 15, 17};
    return n < SH_NOTE_SLOTS ? K[n] : -1;
}
static int32_t sh_key_note(uint32_t k)
{
    uint32_t n;
    for (n = 0; n < SH_NOTE_SLOTS; n++)
        if ((uint32_t)sh_note_key(n) == k)
            return (int32_t)n;
    return -1;
}
static int32_t sh_key_slot(uint32_t k)
{
    uint32_t s;
    for (s = 0; s < SH_SLOTS; s++)
        if ((uint32_t)sh_slot_key(s) == k)
            return (int32_t)s;
    return -1;
}

/* ------------------------------------------------------------------------------------- modes and pages --- */
/* Parts: the foreground mode alternates between parts 1 and 2. The mode left keeps its part while its notes release
 * (their own REL and the sends), then that part is muted; the new mode starts at once on the other. A switch makes
 * no undo entry (undo is the player's last edit, not the mode before). */
/* which half of the pair a track parameter belongs to: NOTE is what is played (the chord keys, the arp, the pattern),
 * SHARED is the part's own (the key, the mix), SOUND is everything else. shell_test walks every id */
enum { SH_SOUND, SH_NOTE, SH_SHARED };
static const uint8_t SH_NOTE_IDS[] = {P_CHRD, P_VOIC, P_QUANT, P_AMODE, P_ARATE, P_AOCT, P_AGATE, P_ASWING, P_APROB,
                                      P_AHOLD, P_AORDER, P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_SQNT};
static const uint8_t SH_SHARED_IDS[] = {P_ROOT, P_SCALE, P_TRANS, P_LEVEL, P_PAN, P_MUTE};
static uint32_t sh_half(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < NELEM(SH_NOTE_IDS); i++)
        if (SH_NOTE_IDS[i] == id)
            return SH_NOTE;
    for (i = 0; i < NELEM(SH_SHARED_IDS); i++)
        if (SH_SHARED_IDS[i] == id)
            return SH_SHARED;
    return SH_SOUND;
}
/* the ids of half h from a to t */
static void sh_put_half(int16_t *t, const int16_t *a, uint32_t h)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (sh_half(i) == h)
            t[i] = a[i];
}

/* the SOUND slot m's sound, as part `part` has it now */
static void sh_snap_fill(sh_snap_t *o, sh_snapx_t *x, uint32_t part)
{
    track_t *t = &trk[part];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)                       /* the sound's own values, not where a knob move has them now */
        o->p[i] = motion_base_value(t, i);
    o->preset = t->preset;
    o->user = t->user;
    o->eng = (uint8_t)t->eng_req;
    memcpy(o->iext, ins_ext[part % NTRK], sizeof o->iext);
    if (x && t->eng_req == ENGI_FM6) {
        memcpy(x->fm6, fm6_patch[part], sizeof x->fm6);
        x->fm6_slot = fm6_slot[part];
    }
    if (x)
        memcpy(x->drl, drum_lane_d[part % NTRK], sizeof x->drl);
    o->valid = 1;
}
static void sh_snap_save(uint32_t m, uint32_t part) { sh_snap_fill(sh_snap_of(m), sh_snapx_of(m), part); }
/* the NOTE record r (a NOTE engine, or RUDIMENT's grid), as part `part` plays it now */
static struct { uint8_t on, used; uint16_t prev; } sh_lgen;   /* a generator visiting LINE (sh_enter_pair) */
static void sh_alive_new(void);
static void sh_q_drop(void);
static void sh_new_takes(void)                          /* PROJECTS > NEW: the takes parked with pairs go too, the */
{                                                       /* rhythms EMPTY, the generators fresh, EVOLVE and MOVE off */
    uint32_t r, i;
    for (r = 0; r < SH_NNOTES; r++)
        for (i = 0; i < NSTEP; i++)
            step_clear(&sh_nsteps(r)[i]);
    for (r = 0; r < SH_NPK; r++)
        sh_pk_pair[r] = SH_PK_NONE;
    memset(shell_lstep, 0, sizeof shell_lstep);
    sh_q_drop();                                        /* (a switch queued from the old project's slot: none) */
    sh_take.have = sh_take.rhy = 0;
    sh_sp[SP_RHY - SP_FIRST] = 0;                       /* (RUDIMENT's and LINES' ALGORITHM: EMPTY, no line) */
    for (i = 0; i < SH_NLI; i++)
        sh_lsp[i][0] = 0;
    for (i = 0; i < SH_NE_CH; i++)                      /* the note engines from init on their next start: CANON's */
        sh_ne_stop(i);                                  /* ring and HOLD, ENIGMA's lock, S+H */
    memset(sh_ne_ready, 0, sizeof sh_ne_ready);
    for (i = 0; i < NTRK; i++)
        sh_ev[i][0] = 0;                                /* (EVOLVE EVERY OFF) */
    sh_sp[SP_MEVERY - SP_FIRST] = 0;                    /* (MOVE EVERY OFF) */
    shell_chord_latch = 0;
    sh_lgen.on = 0;
    sh_alive_new();                                     /* ALIVE's rows from its seed again */
}
static uint16_t sh_partpair[NPART];
/* the PLAY pair (m, n)'s take as part `part` plays it, to its slot. evict 0: the pair keeps its part (its steps are
 * there), so only its own slot or a free one takes them, never a take parked; 1: the pair leaves its part, so a slot
 * is found as the comment above says */
static void sh_pk_store(uint32_t m, uint32_t n, uint32_t part, int evict)
{
    uint32_t i, j, best = 0;
    for (i = 0, j = 0; i < NSTEP && !j; i++)            /* (no take: nothing parked, a slot it had freed) */
        j = trk[part].step[i].n || trk[part].step[i].hit || trk[part].step[i].time == ST_TIE;
    if (!j) {
        if (sh_pk_find(m, n) >= 0)
            sh_pk_pair[sh_pk_find(m, n)] = SH_PK_NONE;
        return;
    }
    if (sh_pk_find(m, n) >= 0)
        best = (uint32_t)sh_pk_find(m, n);
    else
        for (i = 0, best = SH_NPK; i < SH_NPK && best == SH_NPK; i++)
            if (sh_pk_pair[i] == SH_PK_NONE)
                best = i;
    if (best == SH_NPK && !evict)
        return;
    if (best == SH_NPK) {
        uint32_t score = 0xFFFFFFFFu;
        for (i = 0; i < SH_NPK; i++) {
            int live = 0;
            for (j = 0; j < NPART; j++)
                live |= sh_partpair[j] == sh_pk_pair[i] && j != part;
            if ((live ? 0u : 0x80000000u) + (sh_pk_age[i] & 0x7FFFFFFFu) < score) {
                score = (live ? 0u : 0x80000000u) + (sh_pk_age[i] & 0x7FFFFFFFu);
                best = i;
            }
        }
    }
    sh_pk_pair[best] = (uint16_t)(m | n << 8);
    sh_pk_age[best] = ++sh_pk_n;
    memcpy(sh_nstep_bank[best], trk[part].step, sizeof sh_nstep_bank[0]);
}
static void sh_nrec_save(uint32_t r, uint32_t m, uint32_t part)   /* NOTE engine r's record, and the steps of the pair
                                                                   * (m, r) as part `part` plays them (it keeps it) */
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)                       /* (as sh_snap_fill: the values without the knob moves) */
        sh_nrec[r].p[i] = motion_base_value(&trk[part], i);
    sh_nrec[r].valid = 1;
    if (!sh_pk_kind(r))
        memcpy(sh_nsteps(r), trk[part].step, sizeof sh_nstep_bank[0]);
    else
        sh_pk_store(m, r, part, 0);
}
static uint32_t sh_rec_of(uint32_t m, uint32_t n) { (void)m; return n; }   /* a NOTE engine's record: its grid too */
/* a preset into t: the SOUND half only. apply_preset_to already keeps every NOTE and SHARED id (ui.c param_kept
 * covers the arp, the key, the pattern, the chord keys and the mix; up_load does the same under irq-off), so nothing
 * of the NOTE half changes, not even for a block. shell_test checks that param_kept covers both halves */
static void sh_apply_preset(track_t *t, uint32_t k)
{
    apply_preset_to(t, k);
}

static void sh_mode_off(uint32_t kind, uint32_t part);    /* the shell's own instruments, further down */
static void sh_inst_key(uint32_t k);
static void sh_mode_on(uint32_t m, track_t *t, int first);

/* a part stops sounding as a mode leaves it: note-offs (the voices release with their tails and sends) and muted
 * at once (P_MUTE only refuses new notes: no arp, ratchet or step of the old mode starts during the release) */
static void sh_part_quiet(uint32_t part)
{
    panic_req |= 1u << part;
    trk[part].p[P_MUTE] = 1;
    sh.relmask |= (uint8_t)(1u << part);
}

static int sh_track_live(uint32_t part);
static uint32_t sh_track_note(uint32_t part);
/* the parts' roles for voice.c's steals, every frame: the front FG, a track left playing BG with its reserve (2 for
 * RUDIMENT's short hits on DRUMS, else 3: lanes on a pad hold their voices), a part left REL until none sounds */
static void sh_roles(void)
{
    uint32_t p, i, busy;
    for (p = 0; p < NPART; p++) {
        if ((sh.relmask >> p) & 1u) {
            for (busy = 0, i = 0; i < NVOICE; i++)
                busy |= trk[p].v[i].active;
            if (!busy || p == sh.part)
                sh.relmask &= (uint8_t)~(1u << p);
        }
        shell_role[p] = (uint8_t)((sh.relmask >> p) & 1u ? SR_REL : SR_NONE);
    }
    for (p = SH_LINE; p <= SH_BEAT; p++)               /* the tracks left: BG, with a reserve (2 for a kit's short */
        if (p != sh.part && sh_track_live(p)) {         /* hits, else 3: lanes on a pad hold their voices) */
            shell_role[p] = SR_BG;
            shell_reserve[p] = (uint8_t)(SH_NOTES[sh_track_note(p)].kind == SHK_RUDIMENT && trk[p].eng_req == ENGI_DRUM ?
                                         2u : SR_RESERVE_MAX);
        }
    shell_role[sh.part] = SR_FG;
}

/* the part a pair plays on: its NOTE engine's track (BEAT, LINE); a PLAY pair (parts 1 and 2) the one it loops on, if
 * one holds it, else sh_play_pick's */
static uint16_t sh_partpair[NPART];
static uint32_t sh_play_pick(void);
static uint32_t sh_pair_part(uint32_t m, uint32_t n)
{
    uint32_t p;
    if (SH_MODES[m].part != SH_PINGPONG)
        return SH_MODES[m].part;
    if (SH_NOTES[n].part != SH_PINGPONG)
        return SH_NOTES[n].part;
    for (p = 0; p < 2u; p++)
        if (sh_partpair[p] == (uint16_t)(m | n << 8))
            return p;
    return sh.ready ? sh_play_pick() : 0u;
}

/* the pair each part holds now (SOUND | NOTE << 8; SH_NOPAIR: none put there, as at power-on): a track's pair brought
 * to the front plays as it is only if its part still holds it, else it is loaded there first */
#define SH_NOPAIR 0xFFFFu
static void sh_lane_notes(void);
/* LINES' octave pages (and RUDIMENT's under a SOUND but DRUMS): OCT- and OCT+ move the lanes in view by octaves,
 * -2 .. +2, and a tap puts its hit in the octave in view (seq.c step_loct: each hit its own octave). sh_grid_o: the
 * octave a hit turned on takes; a hit turned off keeps none */
#define SH_LO_MIN (-2)
#define SH_LO_MAX 2
static int8_t sh_lopg[2];                               /* each lanes engine's octave in view */
static int8_t sh_grid_o;
static void sh_grid_hit(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *st = &t->step[i % NSTEP];
    uint32_t was = (step_lanes(st) >> (l % NLANE)) & 1u, now;
    grid_hit(t, i, l, on);
    now = (step_lanes(st) >> (l % NLANE)) & 1u;
    if (rec_lo_part(t) && (!now || !was))              /* (a lanes part only: elsewhere just[] is TUNE JUST) */
        step_set_loct(st, l % NLANE, now ? sh_grid_o : 0);
}
static int sh_lo_on(void)                               /* the octave pages apply: a lanes engine under a SOUND but DRUMS */
{
    return sh_kind() == SHK_RUDIMENT && sh_trk()->eng_req != ENGI_DRUM;
}
static int32_t sh_lo(void) { return sh_lo_on() ? sh_lopg[sh_li % 2u] : 0; }   /* the octave in view */
static void sh_lo_clear(step_t *st)                     /* a new beat or line: every hit in octave 0 */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_just_clear(&st[i]);
}
static uint16_t sh_partpair[NPART] = {SH_NOPAIR, SH_NOPAIR, SH_NOPAIR, SH_NOPAIR};
static uint32_t sh_left_t[2];                           /* when each PLAY part's pair was last left (fm1_ms | 1) */
static char sh_stopped[48];                             /* a looping take a new PLAY pair stopped: said after the switch */
static uint8_t sh_tmute;                                /* the tracks muted (HOME + PLAY in the wheel), a bit per part */
static int sh_is_track(uint32_t part) { return part == SH_LINE || part == SH_BEAT; }
static uint32_t sh_track_note(uint32_t part)            /* the NOTE engine a track holds (SH_NNOTES: none yet) */
{
    return sh_partpair[part % NPART] == SH_NOPAIR ? SH_NNOTES : sh_partpair[part % NPART] >> 8;
}
static int sh_track_live(uint32_t part)                 /* it holds a pair and is not muted: it plays with the transport */
{
    return sh.ready && sh_track_note(part) < SH_NNOTES && !((sh_tmute >> part) & 1u);
}
/* what part p's pair does with the transport: 0 nothing (no pair, or PLAY's with no take), 1 plays, 2 muted */
static uint32_t sh_part_state(uint32_t p)
{
    if (!sh.ready || sh_partpair[p] == SH_NOPAIR || (!sh_is_track(p) && !sh_pat_any(&trk[p])))
        return 0;
    return (sh_tmute >> p) & 1u ? 2u : 1u;
}
/* the wheel's keys of the pairs that play (bit 0..7 white, 8..12 black), the ones on their beat, the muted */
static void sh_part_keys(uint32_t *on, uint32_t *beat, uint32_t *mute)
{
    uint32_t p, st, k, w, n;
    *on = *beat = *mute = 0;
    for (p = 0; p < NPART; p++) {
        if (!(st = sh_part_state(p)))
            continue;
        w = sh_mode_pos(sh_partpair[p] & 0xFFu);
        n = sh_note_pos(sh_partpair[p] >> 8);
        k = (w < SH_SLOTS ? 1u << w : 0u) | (n < SH_NOTE_SLOTS ? 1u << (8u + n) : 0u);   /* (not on the wheel: none) */
        if (st == 2u) {
            *mute |= k;
            continue;
        }
        *on |= k;
        if (song.playing && trk[p].seq_idx % 4u == 0u)
            *beat |= k;
    }
    *mute &= ~*on;
}
/* a new PLAY pair's part: one holding nothing, or an empty pattern, first; then a muted one (the front's own only so);
 * then the one left longest ago. Its take stops but stays with its pair (sh_nrec), for when it comes back */
static uint32_t sh_play_pick(void)
{
    uint32_t p, best = 2u, bs = 9u;
    for (p = 0; p < 2u; p++) {
        uint32_t sc = sh_partpair[p] == SH_NOPAIR ? 0u : !sh_pat_any(&trk[p]) ? 1u : ((sh_tmute >> p) & 1u) ? 2u : 3u;
        if (p == sh.part && sc == 3u)
            continue;                                   /* (the front's own loop: never) */
        if (sc < bs || (sc == bs && (p != sh.part && (best == sh.part ||
                                                       (int32_t)(sh_left_t[p] - sh_left_t[best]) < 0)))) {
            bs = sc;
            best = p;
        }
    }
    return best < 2u ? best : sh.part ^ 1u;
}
/* knob moves belong to the pair that recorded them, as its notes do: a pair leaving its part takes them along, parked
 * here with its NOTE record and SOUND, and they come back on whichever part it lands on, under the same SOUND. One
 * store for every pair (the motion store's size: a knob move is in one or the other); full, the oldest parked go */
static motion_event_t sh_park_ev[MOTION_MAX] __attribute__((section(".pool")));
static uint16_t sh_park_own[MOTION_MAX];        /* NOTE record << 8 | SOUND, each (.bss: the pool's room went to the
                                                 * lanes' LANE 2 in the project) */
static uint32_t sh_park_n;
static void sh_park_drop(uint32_t own)
{
    uint32_t i, n = 0;
    for (i = 0; i < sh_park_n; i++)
        if (sh_park_own[i] != own) {
            sh_park_ev[n] = sh_park_ev[i];
            sh_park_own[n++] = sh_park_own[i];
        }
    sh_park_n = n;
}
static void sh_motion_park(uint32_t part)
{
    uint16_t pp = sh_partpair[part % NPART];
    uint32_t own, n, i, room, c = motion_count(&trk[part % NTRK]);
    if (pp == SH_NOPAIR || !c)
        return;
    own = sh_rec_of(pp & 0xFFu, pp >> 8) << 8 | (pp & 0xFFu);
    sh_park_drop(own);
    if (sh_park_n + c > MOTION_MAX) {                   /* (full: the oldest go) */
        room = sh_park_n + c - MOTION_MAX;
        for (i = room; i < sh_park_n; i++) {            /* (no memmove in the firmware's libc) */
            sh_park_ev[i - room] = sh_park_ev[i];
            sh_park_own[i - room] = sh_park_own[i];
        }
        sh_park_n -= room;
    }
    n = motion_take_auto(&trk[part % NTRK], sh_park_ev + sh_park_n);   /* (locks stay with the part) */
    for (i = 0; i < n; i++)
        sh_park_own[sh_park_n++] = (uint16_t)own;
}
static void sh_motion_unpark(uint32_t r, uint32_t m, uint32_t part)
{
    uint32_t own = r << 8 | (m & 0xFFu), i;
    for (i = 0; i < sh_park_n; i++)
        if (sh_park_own[i] == own) {
            sh_park_ev[i].place = (uint8_t)((part % NTRK) << 6 | (sh_park_ev[i].place & 63u));
            motion_give_back(&sh_park_ev[i], 1u);
        }
    sh_park_drop(own);
}
static int sh_part_holds(uint32_t part, uint32_t m, uint32_t n) { return sh_partpair[part % NPART] == (m | n << 8); }

/* the pair (SOUND slot m, NOTE engine n) onto part `to`: the SOUND as it was left (its slot's snapshot), the NOTE
 * engine too (its record), each half on its own; a first visit starts fresh. The key (the shared ids) comes from part
 * `from` (NPART: kept). 1: both halves were first visits */
static int sh_load_pair(uint32_t m, uint32_t n, uint32_t to, uint32_t from)
{
    const sh_mode_t *md = &SH_MODES[m];
    uint32_t firsts, firstn, r = sh_rec_of(m, n), i;
    track_t *t = &trk[to];
    sh_snap_t *o = sh_snap_of(m);
    firsts = !o->valid;
    firstn = !sh_nrec[r].valid;
    sh_motion_park(to);                                 /* (the knob moves of the pair that played here last go with it) */
    if (!firsts && o->eng == ENGI_FM6 && sh_snapx_of(m)) {   /* the patch first, so no FM6 note sounds on the last one's */
        fm6_set_patch(to, sh_snapx_of(m)->fm6);
        fm6_slot[to] = sh_snapx_of(m)->fm6_slot;
    }
    if (firsts) {
        set_engine_of(t, md->engine);
        sh_apply_preset(t, 0);
        if (md->engine == ENGI_LIZARD)                  /* LIZARD starts POLY (its first preset is a MONO lead), so */
            t->p[P_VOICE] = V_POLY;                     /* CHORD and the keys play chords on it at once */
    }
    fm1_irq_off();                                      /* both halves land in one step; the ISR never sees a half copy */
    if (firsts)                                         /* (the INSERT's extras: none, or as the SOUND kept them) */
        memset(ins_ext[to % NTRK], 0, sizeof ins_ext[0]);
    else
        memcpy(ins_ext[to % NTRK], o->iext, sizeof ins_ext[0]);
    if (firsts || !sh_snapx_of(m))                      /* (DRUMS' drums as designed, or as the SOUND kept them) */
        memset(drum_lane_d[to % NTRK], 0, sizeof drum_lane_d[0]);
    else
        memcpy(drum_lane_d[to % NTRK], sh_snapx_of(m)->drl, sizeof drum_lane_d[0]);
    if (!firsts) {
        t->eng_req = o->eng;
        sh_put_half(t->p, o->p, SH_SOUND);
        t->preset = o->preset;
        t->user = o->user;
    }
    if (!firstn) {
        sh_put_half(t->p, sh_nrec[r].p, SH_NOTE);
        if (!sh_pk_kind(r))
            memcpy(t->step, sh_nsteps(r), sizeof t->step);
        else if (sh_pk_find(m, r) >= 0)                 /* (a PLAY pair: its own take, or none yet) */
            memcpy(t->step, sh_nstep_bank[sh_pk_find(m, r)], sizeof t->step);
        else
            memset(t->step, 0, sizeof t->step);
    } else {
        memset(t->step, 0, sizeof t->step);             /* a NOTE engine's first visit starts with an empty pattern */
        if (SH_NOTES[n].kind == SHK_CHORD) {            /* the roots on the scale */
            t->p[P_QUANT] = 1;                          /* (QNT SNAP) */
            t->p[P_CHRD] = CH_DIA3;
            t->p[P_VOIC] = VC_CLOSE;
            t->p[P_VOICE] = V_POLY;                     /* CHORD's first visit plays chords, as it always began; a
                                                         * MONO voice picked later plays the root alone */
        }
        if (SH_NOTES[n].kind == SHK_RUDIMENT || SH_NOTES[n].kind == SHK_ACID)
            t->p[P_SLEN] = 16;
    }
    if (from < NPART && to != from)                     /* the key goes with you, part to part */
        for (i = 0; i < NELEM(SH_SHARED_IDS); i++)
            if (SH_SHARED_IDS[i] != P_LEVEL && SH_SHARED_IDS[i] != P_PAN && SH_SHARED_IDS[i] != P_MUTE)
                t->p[SH_SHARED_IDS[i]] = trk[from].p[SH_SHARED_IDS[i]];
    t->p[P_MUTE] = 0;
    for (i = 0; i < P_COUNT; i++)                       /* a knob move playing on the part: its base is now this pair's */
        if ((motion_active[to % NTRK][i / 32u] >> (i % 32u)) & 1u)
            motion_base[to % NTRK][i] = t->p[i];
    fm1_irq_on();
    if (firstn && SH_NOTES[n].kind == SHK_ACID && md->engine == 0u && !md->lib && ENGINES[0]->presets &&
        str_eq(ENGINES[0]->presets[4].name, "ACID")) {   /* ACID's first visit on ANALOG: ANALOG's ACID, LEGATO, */
        sh_apply_preset(t, 4u);                         /* so a slide glides without a new attack */
        t->p[P_VOICE] = V_LEGATO;
    }
    sh_motion_unpark(r, m, to);
    o->valid = 1;
    sh_snap_save(m, to);
    sh_nrec_save(r, m, to);
    sh_partpair[to % NPART] = (uint16_t)(m | n << 8);
    return (int)(firsts && firstn);
}

/* the pair (SOUND slot m, NOTE engine n) to the front. The pair left keeps its part while its notes release, then
 * that part is muted; the pair comes back as it was left (sh_load_pair). No undo entry for a switch */
static void sh_take_close(track_t *t);
/* a generator on LINE (ALIVE, a clocked note engine) plays behind only once it was used: a key played, a knob or
 * ALGORITHM turned, or PLAY pressed while it was in front. One only visited gives LINE back, as it left, to what LINE
 * had before (nothing, or the pair it held) */
static void sh_lgen_use(void)
{
    if (sh_lgen.on && sh.part == SH_LINE)
        sh_lgen.used = 1;
}
static int sh_lgen_kind(uint32_t n) { return SH_NOTES[n % SH_NNOTES].kind == SHK_ALIVE || SH_NOTES[n % SH_NNOTES].kind == SHK_NE; }
static void sh_enter_pair(uint32_t m, uint32_t n)
{
    const sh_mode_t *md = &SH_MODES[m];
    uint32_t to, from = sh.part, first = 0, back = 0;
    uint16_t prev = SH_NOPAIR;
    track_t *t;
    if (sh.ready && sh_lgen.on && from == SH_LINE) {   /* a generator left: used, it stays; only visited, LINE back */
        back = !sh_lgen.used;
        prev = sh_lgen.prev;
        sh_lgen.on = 0;
    }
    if (sh.ready) {
        if (sh_take.first && sh_take.fpart == from) {   /* a first take still open: closed at its bars first, so */
            sh_take_close(&trk[from]);                  /* the pair's record keeps its LEN, not the open 64 */
            sh_take.rec = 0;
        }
        sh_snap_save(sh.mode, from);
        sh_nrec_save(sh_rec_of(sh.mode, sh.note), sh.mode, from);
        if (!sh_is_track(from)) {                       /* a PLAY pair left loops on, as a track does (HARP's */
            sh_mode_off(sh_kind(), from);               /* keys stop) */
            sh_left_t[from] = fm1_ms | 1u;
        }
    }
    to = sh_pair_part(m, n);
    if (sh.ready && to == SH_LINE && sh_lgen_kind(n) && sh_partpair[SH_LINE] != (uint16_t)(m | n << 8)) {
        sh_lgen.on = 1;                                 /* a generator's visit: what LINE had before it (another */
        sh_lgen.used = 0;                               /* visit only, before: what that one found) */
        sh_lgen.prev = back ? prev : sh_partpair[SH_LINE];
        back = 0;
    } else if (to == SH_LINE)
        back = 0;
    if (md->part == SH_PINGPONG && SH_NOTES[n].part == SH_PINGPONG)
        sh.pp = (uint8_t)to;
    if (sh.ready && !sh_part_holds(to, m, n) && sh_track_note(to) < SH_NNOTES) {
        uint16_t old = sh_partpair[to];                 /* another pair on the part: this one replaces it */
        if (!sh_is_track(to) && to != from && sh_pat_any(&trk[to]) && !((sh_tmute >> to) & 1u)) {
            str_cpy(sh_stopped, SH_MODES[old & 0xFFu].name, sizeof sh_stopped);   /* (a looping take stops: said) */
            str_cpy(sh_stopped + str_len(sh_stopped), " ", sizeof sh_stopped - str_len(sh_stopped));
            str_cpy(sh_stopped + str_len(sh_stopped), SH_NOTES[old >> 8].name, sizeof sh_stopped - str_len(sh_stopped));
            str_cpy(sh_stopped + str_len(sh_stopped), " STOPS: PART ", sizeof sh_stopped - str_len(sh_stopped));
            fmt_int(sh_stopped + str_len(sh_stopped), (int32_t)to + 1);
        }
        sh_mode_off(SH_NOTES[old >> 8].kind, to);
        panic_req |= 1u << to;
        if (sh_pk_kind(old >> 8))                       /* a PLAY take loses its part: parked now */
            sh_pk_store(old & 0xFFu, old >> 8, to, 1);
    }
    t = &trk[to];
    sh.mom = 0;                                         /* a try or SAVE's question belongs to the pair left */
    sh.save = 0;
    sh_sndu.on = 0;                                     /* (no undo entry for a switch) */
    undo_depth++;                                       /* as load_begin does: no undo entry for a switch */
    if (sh.ready && sh_part_holds(to, m, n))
        ;                                               /* a pair left looping, to the front: as it plays, in time */
    else
        first = (uint32_t)sh_load_pair(m, n, to, sh.ready ? from : NPART);
    t->p[P_MUTE] = (int16_t)((sh_tmute >> to) & 1u);
    undo_depth--;
    song.sel = (uint8_t)to;
    sh.part = (uint8_t)to;
    if (song.rec)                                       /* an arm follows the pair: REC records what is in front */
        song.rec = (uint8_t)(1u << to);
    if (m != sh.mode || n != sh.note) {                 /* the pair before, as a pair: HOME twice goes back to it */
        sh.prev = sh.mode;
        sh.prevnote = sh.note;
    }
    sh.mode = (uint8_t)m;
    sh.note = (uint8_t)n;
    sh.global = SHG_NONE;
    if (SH_NOTES[n].kind == SHK_RUDIMENT)               /* (its own lanes to the knobs and the screen) */
        sh_lanes_use(n == SH_N_LINES);
    sh_lane_notes();                                    /* (the lanes' notes and parts now, not a frame late) */
    sh_mode_on(m, t, (int)first);
    if (back && sh_partpair[SH_LINE] != prev) {         /* the generator only visited: LINE as it was */
        sh_mode_off(SH_NOTES[(sh_partpair[SH_LINE] >> 8) % SH_NNOTES].kind, SH_LINE);
        if (prev == SH_NOPAIR) {
            sh_partpair[SH_LINE] = SH_NOPAIR;
            sh_part_quiet(SH_LINE);
        } else
            (void)sh_load_pair(prev & 0xFFu, prev >> 8, SH_LINE, NPART);
        sh_lane_notes();
    }
}
static void sh_enter(uint32_t m) { sh_enter_pair(m, sh.note); }

/* an engine the build leaves out (core.h FELUCCA_x) plays on its sibling: the voices on MOUTH, the sampled ones on
 * SAMPLE (or, without it, PHYS), the organs and oscillators on ANALOG */
static uint32_t sh_eng_sibling(uint32_t e)
{
    switch (e) {
    case 5: case ENGI_TALK: case ENGI_SYLLABLE: case ENGI_VODER:   /* VOICE TALK SYLLABLE VODER */
        return ENGI_MOUTH;
    case 8: case ENGI_SLICE: case ENGI_LOOP:            /* GRAIN SLICE LOOP */
        return FELUCCA_SAMPLE ? ENGI_SAMPLE : ENGI_PHYS;
    case ENGI_SAMPLE:
        return ENGI_PHYS;
    default:                                            /* PHASE LOFI TRIO WHEEL NOISE ORGAN */
        return 0;                                       /* (ANALOG) */
    }
}

/* part k's sound is on an engine the build lacks: its sibling plays. A pair on the part goes to the sibling's SOUND,
 * as that SOUND was left, so the SOUND it named keeps its own sound; no pair (or the sibling's pair already on another
 * part), the sibling's first sound on the part */
static int sh_wheel_avail(uint32_t m);
static void sh_to_sibling(uint32_t k)
{
    uint32_t e = sh_eng_sibling(trk[k].eng_req), m2, n = sh_partpair[k] >> 8, p;
    for (m2 = 0; m2 < SH_NMODES && (SH_MODES[m2].lib || SH_MODES[m2].engine != e || !sh_wheel_avail(m2)); m2++)
        ;
    for (p = 0; p < NPART && sh_partpair[p] != (uint16_t)(m2 | n << 8); p++)
        ;
    if (k >= NPART || sh_partpair[k] == SH_NOPAIR || m2 >= SH_NMODES || p < NPART) {
        set_engine_of(&trk[k], e);
        apply_preset_to(&trk[k], 0);
        return;
    }
    sh_nrec_save(sh_rec_of(m2, n), m2, k);              /* (the pattern as it is on the part) */
    sh_load_pair(m2, n, k, NPART);
    trk[k].p[P_MUTE] = (int16_t)((sh_tmute >> k) & 1u);
    if (k == sh.part) {
        sh.mode = (uint8_t)m2;
        sh_mode_on(m2, &trk[k], 0);
    }
}

/* the wheel let go, or GLO + a slot: the pair to the front */
static int sh_view_note(void);
static uint8_t sh_walk;                                 /* HOME twice in a row: the parts this walk visited */
static uint16_t sh_walk_at = 0xFFFFu;                   /* the pair it stands on (none: 0xFFFF) */
static uint8_t sh_wrec;                                 /* REC went down on a pair in the wheel: a short press arms nothing */
static uint32_t sh_front_t[NPART];                      /* fm1_ms + 1 each part's pair was last left from the front */
static uint8_t sh_keep_view;                            /* (the double tap's switch: the view in front stays) */
static void sh_view_keep(void);
static uint32_t sh_view_page(int note);
static int sh_bg_any(void)                              /* a pair plays behind the one in front */
{
    uint32_t p;
    for (p = 0; p < NPART; p++)
        if (p != sh.part && sh_part_state(p) == 1u && sh_partpair[p] != (uint16_t)(sh.mode | sh.note << 8))
            return 1;
    return 0;
}
static void sh_switch_pair(uint32_t m, uint32_t n)
{
    if (m >= SH_NMODES || n >= SH_NNOTES || (m == sh.mode && n == sh.note))
        return;
    if (key_latch(sh_trk())) {                          /* (a latch of the keys never outlives its pair) */
        fm1_irq_off();
        latch_end(sh_trk());
        sh_trk()->arp_phys = 0;
        fm1_irq_on();
    }
    {   /* the view: another NOTE engine opens its NOTE view, the SOUND alone changed (or KEYS, no engine of its own)
         * its SOUND view; the double tap (sh_keep_view) keeps the one in front. Each opens where it was left */
        int note = sh_keep_view ? sh_view_note() : n != sh.note && n != 0u;
        sh_front_t[sh.part % NPART] = fm1_ms | 1u;
        sh_view_keep();
        sh_enter_pair(m, n);
        sh.page[sh.mode] = (uint8_t)sh_view_page(note);
    }
    if (n == 0u)
        sh_toast(SH_MODES[m].name, "");
    else {
        static char b[24];
        str_cpy(b, SH_NOTES[n].name, sizeof b);
        str_cpy(b + str_len(b), " > ", sizeof b - str_len(b));
        sh_toast(b, SH_LIB(m) ? ENGINES[sh_trk()->eng_req % NENGINES]->name : SH_MODES[m].name);
    }
    if (sh_stopped[0]) {                                /* a looping take it stopped says so instead */
        sh_toast(sh_stopped, 0);
        sh_stopped[0] = 0;
    }
}
static void sh_switch(uint32_t m) { sh_switch_pair(m, sh.note); }

/* PLAY on a slot in the wheel: that pair's part muted, or back: BEAT or LINE, or a PLAY pair looping on part 1 or 2.
 * Muted, it is silent and its generator stops; HOME glows while one is */
static void sh_line_ne(void);
static void sh_track_mute(uint32_t m, uint32_t n)
{
    uint32_t p = NPART, k;
    const char *name;
    if (m >= SH_NMODES || n >= SH_NNOTES)
        return;
    if (SH_MODES[m].part != SH_PINGPONG || SH_NOTES[n].part != SH_PINGPONG)
        p = sh_pair_part(m, n);
    else
        for (k = 0; k < 2u; k++)
            if (sh_partpair[k] == (uint16_t)(m | n << 8))
                p = k;
    if (p >= NPART) {
        sh_toast("NOTHING TO MUTE", 0);                 /* (a PLAY pair no part holds) */
        return;
    }
    name = p == SH_BEAT ? "BEAT" : p == SH_LINE ? "LINE" : SH_NOTES[n].name;
    sh_tmute ^= (uint8_t)(1u << p);
    if ((sh_tmute >> p) & 1u) {
        if (sh_track_note(p) < SH_NNOTES)
            sh_mode_off(SH_NOTES[sh_track_note(p)].kind, p);
        sh_part_quiet(p);                               /* (its tails the first to go in a steal) */
        sh_toast(name, " MUTED");
    } else {
        trk[p].p[P_MUTE] = (int16_t)(sh_track_note(p) >= SH_NNOTES);   /* (nothing on it yet: silent still) */
        sh_toast(name, " BACK");
    }
    sh_line_ne();
}

/* SOUND MAIN: the engine's own four, but where the engine has a switch that makes it another instrument (a MODE, a
 * MODEL, a KIT, a WAVE, a PHRASE), MAIN starts with it; what it pushes off stays on its EDIT page */
static const struct { uint8_t eng, ids[4]; } SH_MAIN[] = {
    {0u, {P_E0, P_E4, P_E5, P_REL}},                    /* ANALOG: WAVE CUT RES REL (ATK on ENV) */
    {2u, {P_E0, P_E2, P_E3, P_E4}},                     /* PHASE: WAVE DCW ENV DTN (REL on ENV) */
    {ENGI_PHYS, {P_E0, P_E1, P_E2, P_E3}},              /* MODEL STRC BRIT DAMP (POS on EXCT) */
    {ENGI_DRUM, {P_E0, P_E1, P_E2, P_E3}},              /* KIT TUNE TONE DECY (SNAP on HIT) */
    {ENGI_LIZARD, {P_E0, P_E2, P_E6, P_REL}},           /* MODE WAVE CUT REL (DTN on OSC) */
    {ENGI_MOUTH, {P_E4, P_E0, P_E1, P_E2}},             /* PHRASE SPEED THROAT MOUTH (GRIT on MOUTH) */
};
static uint32_t sh_main_id(uint32_t e, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < NELEM(SH_MAIN); i++)
        if (SH_MAIN[i].eng == e)
            return SH_MAIN[i].ids[k & 3u];
    return ENGINES[e % NENGINES]->knob[k & 3u];
}
/* the page in front: its four parameter ids, whether they are global (song.g) */
static void sh_page_ids(uint8_t ids[4], int *glob)
{
    uint32_t p = sh.page[sh.mode], k;
    *glob = sh.global >= SHG_SYSTEM || (!sh.global && SHP_GLOBAL(p));
    if (sh.global) {
        for (k = 0; k < 4u; k++)
            ids[k] = sh.global == SHG_SYSTEM2 ? SHG_SYS2_IDS[k] : sh.global == SHG_MOVE ? SHG_MOVE_IDS[k] :
                     sh.global == SHG_HARM ? SHG_HARM_IDS[k] :
                     SHG_IDS[sh.global < SHG_SYSTEM ? sh.global : SHG_SYSTEM][k];
        return;
    }
    for (k = 0; k < 4u; k++)
        ids[k] = p == SHP_SHAPE && sh_trk()->eng_req == ENGI_LIZARD ? SHP_LZSHAPE[k] :
                 p == SHP_SEQ && sh_kind() == SHK_RUDIMENT ? SHP_RUDSEQ[k] :
                 p == SHP_INS2 && (sh_trk()->p[P_ITYPE] == IT_FILTER || sh_trk()->p[P_ITYPE] == IT_COMB) ? SHP_FLT2[k] :
                 p == SHP_NOTE ? SHK_HOME[sh_kind()][k] :      /* (the NOTE engine's own four) */
                 p == SHP_LINE && sh_kind() == SHK_NE ? SH_NE_LINE[sh_ne_i(sh.note) % SH_NNE][k] :   /* (S+H's, HARP's) */
                 p == SHP_LINE && k == 2u && sh_kind() == SHK_ACID ? SP_AFLUX :
                 p == SHP_INS && k && sh_trk()->p[P_ITYPE] <= 0 ? SH_NONE :   /* (INSERT OFF: A B C mean nothing yet) */
                 p != SHP_HOME ? SHP_IDS[p][k] :
                 sh_main_id(sh_eng(), k);         /* HOME: SOUND MAIN, the engine's own four */
}

static const char *sh_page_name(void)
{
    uint32_t p = sh.page[sh.mode];
    static const char *const N[SHP_N] = {"MAIN", 0, 0, "OPERATOR", "OPERATOR 2", "OP ENV", "FILTER", "SHAPE", "DRAW 1-4",
                                         "DRAW 5-8", "LANES", "LANES 2", "VOICE", "GLIDE", "ENV", "ENV DEST", "LFO",
                                         "LFO DEST", "LFO 2", "FX", "INSERT", "INSERT 2", "DELAY", "DELAY 2", "REVERB", "CHORUS",
                                         "MAIN", "NOTES", "LANE 2", "LINE", "MAP", "MAP 2", "CHORD PLAY", "PROG", "ARP", "ARP 2", "SEQ", "KEYS", "KEYS 2", "SPELL", "DRUM", "EVOLVE"};
    if (sh.global)
        return sh.global == SHG_SCALE ? "SCALE" : sh.global == SHG_MOVE ? "MOVE" : sh.global == SHG_HARM ? "HARMONY" :
               sh.global == SHG_SYSTEM ? "SYSTEM" :
               sh.global == SHG_SYSTEM2 ? "SYSTEM 2" : "ABOUT";
    if (p == SHP_EDIT1 || p == SHP_EDIT2)
        return ENGINES[sh_trk()->eng_req % NENGINES]->page_title[p - SHP_EDIT1];   /* the part's engine's own titles */
    return N[p];
}

/* parameter id of the page in front: its descriptor and where its value lives (a shell parameter, the song's, the
 * part's) */
/* the drum the DRUM page sets: the piece the lane in front strikes (its NOTE may be another drum's), or under
 * KEYS the drum struck last (its own, so a lanes engine's lane in front is left alone) */
static uint8_t sh_kdrum = DV_SNARE;
static uint32_t sh_drum_piece(void)
{
    if (sh_kind() == SHK_RUDIMENT)
        return drum_lane(shell_lane_note[(uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u]);
    return sh_kdrum % 8u;
}
/* HARMONY, each NOTE engine's own (it follows the engine from part to part): TYPE VOIC LEAD VEL. CHORD, TONNETZ and
 * HARP play chords already, and DRUMS has no harmony: the page says so (sh_harm_why) */
static int16_t sh_hm[SH_NNOTES][4] = {[0 ... SH_NNOTES - 1] = {0, 0, 0, 100}};
static const char *sh_harm_why(uint32_t part)
{
    uint32_t pp = sh_partpair[part % NPART], m = pp & 0xFFu, n = pp >> 8;
    if (pp == SH_NOPAIR)
        return 0;
    if (n == 1u || n == 3u || n == 5u)
        return n == 1u ? "CHORD PLAYS ITS OWN CHORDS" : n == 3u ? "TONNETZ PLAYS ITS OWN TRIADS" :
               "HARP PLAYS ITS OWN CHORDS";
    if (m < SH_NMODES && SH_MODES[m].engine == ENGI_DRUM && !SH_MODES[m].lib)
        return "DRUMS: NO HARMONY";
    if (trk[part % NTRK].p[P_VOICE] != V_POLY)
        return "ONE VOICE: SET VOICE MODE POLY";
    return 0;
}
static void sh_harm_poll(void)                          /* each part's HARMONY: its NOTE engine's, or none */
{
    uint32_t p, k;
    for (p = 0; p < NTRK; p++) {
        uint32_t n = sh_partpair[p] >> 8;
        int on = sh_partpair[p] != SH_NOPAIR && n < SH_NNOTES && !sh_harm_why(p) && sh_hm[n][0];
        for (k = 0; k < 4u; k++)
            harm_cfg[p][k] = on ? (uint8_t)sh_hm[n][k] : 0u;
    }
}
typedef char sh_harm_size[SH_NNOTES * 4u == P11_HARM_N ? 1 : -1];   /* (a new NOTE engine: P11_HARM_N grows) */
static void sh_harm_get(uint8_t *m)                     /* project.c tag 14 */
{
    uint32_t n, k;
    for (n = 0; n < SH_NNOTES; n++)
        for (k = 0; k < 4u; k++)
            m[n * 4u + k] = (uint8_t)sh_hm[n][k];
}
static void sh_harm_set(const uint8_t *m)
{
    uint32_t n, k;
    for (n = 0; n < SH_NNOTES; n++)
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = &SP_DESC[SP_HMTYPE + k - SP_FIRST];
            sh_hm[n][k] = (int16_t)clamp(m[n * 4u + k], d->min, d->max);
        }
}
static const param_desc_t *sh_desc(uint32_t id, int glob, int16_t **vp)
{
    if (id >= SP_HMTYPE && id <= SP_HMVEL) {            /* HARMONY: the NOTE engine in front's */
        *vp = &sh_hm[sh.note % SH_NNOTES][id - SP_HMTYPE];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id >= SP_EEVERY && id <= SP_EBACK) {            /* EVOLVE: the part's own */
        *vp = &sh_ev[sh.part % NTRK][id - SP_EEVERY];
        if (id == SP_EWHAT && sh_kind() != SHK_RUDIMENT)
            return &SH_EV_MOVE;
        if (id == SP_EWHAT && sh.note == SH_N_RUDIMENT)
            return &SH_EV_DRIFT;
        return &SP_DESC[id - SP_FIRST];
    }
    if (id == SP_RHY && sh_li == 1u) {                  /* LINES: the LINE writer's seed */
        *vp = &sh_sp[SP_RHY - SP_FIRST];
        return &SH_LWR_DESC;
    }
    if (id >= SP_ELEN && id <= SP_EROT) {               /* the lane in front's cycle */
        static const char *const N_FOLLOW[] = {"=LEN", 0};
        static const param_desc_t FOLLOW = {"LEN", F_ENUM, 0, 0, 0, N_FOLLOW, 0};
        *vp = &sh_eu[id - SP_ELEN][(uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u];
        return id == SP_ELEN && **vp <= 0 ? &FOLLOW : &SP_DESC[id - SP_FIRST];
    }
    if (id >= SP_LPAD && id <= SP_LNAP) {               /* .. its LANE 2 */
        *vp = &sh_lx[id - SP_LPAD][(uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id == SP_LNOTE) {                               /* .. and its note */
        *vp = &sh_lnote[(uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id >= SP_FDRV && id <= SP_FENV) {               /* FILTER's and COMB's extras, the part's */
        *vp = &ins_ext[sh.part % NTRK][id - SP_FDRV];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id >= SP_DTUNE && id <= SP_DSNAP) {             /* DRUMS: the drum in front's own sound, on the part */
        *vp = &drum_lane_d[sh.part % NTRK][id - SP_DTUNE][sh_drum_piece()];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id == SP_LOOP) {                                /* RUDIMENT's and LINES' SEQ LEN: the part's */
        *vp = &sh_trk()->p[P_SLEN];
        return &SP_DESC[id - SP_FIRST];
    }
    if (id >= SP_NE0 && id <= SP_NE3 && sh_ne_i(sh.note) < SH_NNE &&
        SH_NOTES[sh.note].ne->params[id - SP_NE0].fmt == NE_LEN) {   /* a NOTE engine's LEN: the part's, as on SEQ */
        *vp = &sh_trk()->p[P_SLEN];
        return track_desc(sh_trk(), P_SLEN);
    }
    if (id >= SP_NE0 && id <= SP_NE3 && sh_ne_i(sh.note) < SH_NNE) {   /* the NOTE engine's */
        *vp = &sh_ne_val[sh_ne_i(sh.note)][id - SP_NE0];
        return &sh_ne_pd[sh_ne_i(sh.note)][id - SP_NE0];
    }
    if (id >= SP_FIRST && id < SP_END) {
        *vp = &sh_sp[id - SP_FIRST];
        return &SP_DESC[id - SP_FIRST];
    }
    if (glob) {
        *vp = &song.g[id];
        return &GP[id];
    }
    *vp = &sh_trk()->p[id];
    if (id == P_SPRD) {                                 /* the voices' stereo spread: WIDTH here (GSPR, the glide's, and */
        static param_desc_t w;                          /* a NOTE engine's SPRD are others), the stock label kept */
        w = *track_desc(sh_trk(), id);
        w.label = "WIDTH";
        return &w;
    }
    return track_desc(sh_trk(), id);
}

static int sh_note_half(void)
{
    uint32_t f = SHP_FAM[sh.page[sh.mode]];
    return !sh.global && (f == B_ARP || f == B_SEQ);
}

/* the two views of the pair in front: SOUND (HOME's MAIN, EDIT, ENV, LFO, FX) and NOTE (the NOTE engine's MAIN, ARP,
 * SEQ). The page in front says which; a shell page keeps the view it was opened from */
static int sh_page_ok(uint32_t p);
static int sh_page_note(uint32_t p) { return SHP_FAM[p] == B_ARP || SHP_FAM[p] == B_SEQ; }
static int sh_view_note(void) { return sh_page_note(sh.page[sh.mode]); }
static void sh_view_keep(void)                          /* the page in front, as its view's page left */
{
    uint32_t p = sh.page[sh.mode];
    if (sh_page_note(p))
        sh.lastnv[sh.note] = (uint8_t)p;
    else
        sh.lastsnd[sh.mode] = (uint8_t)p;
}
static uint32_t sh_view_page(int note)                  /* where view note opens for the pair in front */
{
    uint32_t q = note ? sh.lastnv[sh.note] : sh.lastsnd[sh.mode];
    if (q < SHP_N && sh_page_note(q) == note && sh_page_ok(q))   /* (0 is HOME: a fresh NOTE view opens MAIN) */
        return q;
    return note ? (sh_page_ok(SHP_NOTE) ? SHP_NOTE : SHP_ARP) : SHP_HOME;
}

static int16_t sh_map_rhy(void);
static void sh_map_sync(void);
static void sh_freeze(void);
static int sh_freeze_ok(void);
static void sh_frz_poll(void);
/* page p is there for the part in front: LIZARD's DRAW pages while its MODE is DRAW (under any NOTE engine: they
 * are the SOUND view's) */
static int sh_page_ok(uint32_t p)
{
    const track_t *t = sh_trk();
    if (p == SHP_DRAW1 || p == SHP_DRAW2)
        return t->eng_req == ENGI_LIZARD && t->p[P_E0] == LZM_DRAW;
    if (p >= SHP_FMOP && p <= SHP_FMENV)
        return t->eng_req == ENGI_FM6;
    if (p == SHP_LKEYS || p == SHP_LKEYS2)
        return t->eng_req == ENGI_LOOP;
    if (p == SHP_SPELL)
        return t->eng_req == ENGI_MOUTH;                /* (under any NOTE engine: its notes sing the cursor's syllable) */
    if (p == SHP_FILTER)
        return t->eng_req == 0u;                        /* engine 0 is ANALOG */
    if (p == SHP_LANES1 || p == SHP_LANES2 || p == SHP_DRUM)
        return t->eng_req == ENGI_DRUM;
    if (p == SHP_CPLAY || p == SHP_PROG)
        return sh_kind() == SHK_CHORD;
    if (p == SHP_NOTE2 || p == SHP_LANE2)
        return sh_kind() == SHK_RUDIMENT;
    if (p == SHP_LINE)                                  /* the LINE writer: LINES' and ACID's; S+H's FLUX */
        return sh.note == SH_N_LINES || sh.note == SH_N_ACID || SH_NOTES[sh.note].ne == &NE_SNH ||
               SH_NOTES[sh.note].ne == &NE_HARP;
    if (p == SHP_MAP || p == SHP_MAP2)                  /* RUDIMENT's ALGORITHM on MAP */
        return sh.note == SH_N_RUDIMENT && sh_map_rhy() == (int16_t)SH_MAP;
    if (p == SHP_EVOLVE)                                /* (ALIVE has no steps to evolve) */
        return sh_kind() != SHK_ALIVE;
    if (p == SHP_NOTE)                                  /* KEYS has none, so ARP opens ARP */
        return sh_kind() == SHK_CHORD || sh_kind() == SHK_ALIVE || sh_kind() == SHK_NE || sh_kind() == SHK_RUDIMENT ||
               sh_kind() == SHK_ACID;
    return p < SHP_N;
}

/* a page button tapped: its family's first page; tapped again on that family, its next page (wrapping; a SOUND
 * family through SOUND MAIN, the NOTE view's ARP through NOTE MAIN, its first) */
#define SH_DTAP_MS 350u                 /* a double tap: ARP's HOLD, REC's knobs */
static uint32_t sh_arp_t, sh_rec_t;     /* the last ARP and REC taps (fm1_ms | 1) */
static void sh_arp_hold(track_t *t)     /* HOLD on or off: the arp keeps the notes let go; with no arp mode, the keys latch */
{
    t->p[P_AHOLD] = (int16_t)!t->p[P_AHOLD];
    sh_toast(t->p[P_AHOLD] ? (t->p[P_AMODE] ? "ARP HOLD ON" : "HOLD ON: THE KEYS LATCH") :
                             (t->p[P_AMODE] ? "ARP HOLD OFF" : "HOLD OFF"), 0);
}
static void sh_page_button(uint32_t b)
{
    uint32_t p = sh.page[sh.mode], i;
    int here = !sh.global && SHP_FAM[p] == b;
    if (!sh.global && SHP_FAM[p] == B_EDIT)             /* each half remembers the page it was left on: the flip */
        sh.lastedit[sh.mode] = (uint8_t)p;
    sh.global = SHG_NONE;
    if (b == B_ARP) {                                   /* ARP: the arp's own page, MODE first, from anywhere; again: ARP 2,
                                                         * and back (the NOTE engine's pages: HOME, SELECT) */
        sh.page[sh.mode] = here && p == SHP_ARP ? SHP_ARP2 : SHP_ARP;
        return;
    }
    if (!here && b == B_EDIT) {                         /* EDIT, ARP, EDIT, ARP: back where the SOUND half was */
        uint32_t q = sh.lastedit[sh.mode];
        if (q && q < SHP_N && SHP_FAM[q] == b && sh_page_ok(q)) {
            sh.page[sh.mode] = (uint8_t)q;
            return;
        }
    }
    for (i = 1; here && i < SHP_N; i++) {
        if (b != B_ARP && b != B_SEQ && p + i >= SHP_N) {   /* a SOUND family walked past its last: SOUND MAIN (the */
            sh.page[sh.mode] = SHP_HOME;                /* family's next tap its first page) */
            if (b == B_EDIT)
                sh.lastedit[sh.mode] = 0;
            return;
        }
        if (SHP_FAM[(p + i) % SHP_N] == b && sh_page_ok((p + i) % SHP_N)) {
            sh.page[sh.mode] = (uint8_t)((p + i) % SHP_N);
            return;
        }
    }
    if (here)
        return;
    for (i = 0; i < SHP_N; i++)
        if (SHP_FAM[i] == b && sh_page_ok(i)) {         /* the family's first page this part has */
            sh.page[sh.mode] = (uint8_t)i;
            return;
        }
    sh_toast(sh_cur()->name, " HAS NO SUCH PAGE");
}

/* FM6's operator pages in front (not a global page) */
static int sh_fmop_page(void)
{
    return !sh.global && sh.page[sh.mode] >= SHP_FMOP && sh.page[sh.mode] <= SHP_FMENV;
}

/* LIZARD's DRAW pages: the drawing WAVE shows (factory) as the eight levels (-64..63), so a first turn edits it; own:
 * from then on the part plays its own drawing (eng_lizard.c lizard_own) */
static void sh_lz_take(track_t *t, int own)
{
    const lz_set_t *st = &LZ_DRAW[((uint32_t)t->p[P_E2] >> 3) & 15u];
    uint32_t i;
    if (t->p[P_LZUSER]) {                               /* the player's drawing is never overwritten by a factory one; a
                                                         * DRAW knob turned takes it back under the WAVE now showing */
        if (own)
            t->p[P_LZWAT] = t->p[P_E2];
        return;
    }
    for (i = 0; i < 8u; i++)
        t->p[P_LZD0 + i] = (int16_t)(st->lv[i] < 0 ? st->lv[i] * 64 / 100 : st->lv[i] * 63 / 100);
    if (own) {
        t->p[P_LZWAT] = t->p[P_E2];
        t->p[P_LZUSER] = 1;
    }
}

static void sh_euclid_lane(track_t *t, uint32_t l);   /* defined below, with the rhythms */
static void sh_lwr_load(track_t *t, uint32_t seed);
static uint32_t sh_lane_len(uint32_t l);
static uint32_t sh_lane_own(uint32_t l);
static int16_t sh_elen_turn(int16_t v, int32_t s);
static void sh_lane_knob(track_t *t, uint32_t l, uint32_t w, int32_t s);
static void sh_lane2_knob(track_t *t, uint32_t l, uint32_t w, int32_t s);
static void sh_lane2_seed(void);
static uint32_t sh_lanes_held(void);
static int sh_hold_view(void);
static int sh_hold_move(int32_t oct, int32_t semi);
static void sh_hold_poll(void);
/* the lights' moments (shell_leds draws them over the rest): a detent dips the page's button, a click under the
 * finger; HOME's TAB dips it, its double tap blinks it; a pattern landing blinks SEQ */
enum { LFX_DIP = 1, LFX_BLINK, LFX_FLASH };
static struct { uint8_t id, kind; uint32_t t; } sh_lfx[4];
static uint8_t sh_led_pg = 0xFF;                        /* the page's button as shell_leds lit it last (the click's) */
static uint32_t sh_key_up[27];                          /* fm1_ms a key was let go (its trail), 0: none */
static void sh_lfx_add(uint32_t id, uint32_t kind)
{
    uint32_t i, o = 0;
    for (i = 0; i < 4u; i++)                            /* its own slot again, else the oldest */
        if (sh_lfx[i].kind && sh_lfx[i].id == id)
            break;
    if (i == 4u) {
        for (i = o = 0; i < 4u; i++)
            if (!sh_lfx[i].kind || fm1_ms - sh_lfx[i].t > fm1_ms - sh_lfx[o].t)
                o = i;
    } else
        o = i;
    sh_lfx[o].id = (uint8_t)id;
    sh_lfx[o].kind = (uint8_t)kind;
    sh_lfx[o].t = fm1_ms;
}
static void sh_click(void)                              /* a detent (at most every 70 ms: a steady turn ticks) */
{
    static uint32_t t;
    if (sh_led_pg != 0xFF && fm1_ms - t >= 70u) {
        t = fm1_ms;
        sh_lfx_add(sh_led_pg, LFX_DIP);
    }
}
static int sh_q_waits(uint32_t part);                   /* (a pattern switch queued for the part) */
static void sh_q_turn(int32_t s);                      /* (switching patterns live, by sh_move_isr) */
static void sh_q_commit(void);
static void sh_q_poll(void);
static void sh_lanes_put(uint32_t li, const uint8_t *l);
/* FILL (the steps' FILL / !FILL conditions, seq.c seq_fill): SCL held past its tap while it plays, and only SCL: another
 * control with it (OCT-'s reset ring, SELECT) is a combination, no FILL; let go, FILL off and no SCALE page */
static uint8_t sh_fill_no;                              /* another button with SCL since its press (a knob is fine:
                                                         * a turn during a fill is a live move) */
static void sh_fill_poll(void)
{
    uint8_t f = (uint8_t)(song.playing && sh_b[B_SCL].down && sh_b[B_SCL].hold && !sh_fill_no &&
                          !sh.wheel && !sh_held(B_GLO));
    if (seq_fill != f)
        seq_fill = f;
}
static void sh_acid_turn(track_t *t, int16_t was, uint32_t seed);
static void sh_rhythm_turn(track_t *t, int16_t was, uint32_t r);
static void sh_lwr_turn(track_t *t, int16_t was, uint32_t seed);
static void sh_acid_rot(track_t *t, int32_t s);
static void sh_acid_poll(void);
static void sh_acid_seq(void);
static int sh_acid_entry(void);
static int sh_acid_keys(uint32_t k);
static uint32_t sh_acid_deg(const track_t *t, uint32_t d);
static void sh_acid_load(track_t *t, uint32_t seed);
static void sh_acid_keep(track_t *t);
static void sh_hold_down(uint32_t k, uint32_t w);
static int sh_hold_key(uint32_t k);
static int sh_step_knob(uint32_t k, int32_t s);
static int sh_step_card(uint32_t k, const char **label, char val[12], int32_t *frac);
static int sh_beat_keep(track_t *t, int16_t rhy, int always);
static uint32_t sh_beat_sum(const track_t *t);
/* a knob turned on the page in front */
/* the INSERT's three knobs after a change of type: each takes the new type's meaning, so a value outside its new
 * range (ROTOR's SPEED is an enum of three) goes to the new type's default */
static void sh_ins_fit(track_t *t, int16_t was)
{
    uint32_t j;
    int fl = t->p[P_ITYPE] == IT_FILTER || t->p[P_ITYPE] == IT_COMB;
    for (j = 0; j < 3u; j++) {
        const param_desc_t *d = track_desc(t, P_IA + j);
        if (t->p[P_IA + j] < d->min || t->p[P_IA + j] > d->max || (fl && was != t->p[P_ITYPE]))
            t->p[P_IA + j] = d->def;                    /* (FILTER and COMB start where they sound: a CUT, a pitch) */
    }
}

/* SPELL: the knobs show the cursor's syllable, and MOUTH sings it from a key (eng_mouth.c mouth_spell_*). The
 * syllable is built into the next of four buffers, so a voice still singing the last one keeps its bytes; resing:
 * the voices whose key is down sing the new one at once, gliding (VOWEL turned under a held key: the talk box) */
static struct { uint8_t open, slot; } sh_splsv;    /* SPELL's SAVE card: open, the slot it would write */
static void sh_spell_sync(int resing)
{
    static uint8_t ph[4][8], len[4][8], b;
    track_t *t = sh_trk();
    mth_part_t *mp = mouth_of(t);
    uint32_t n, i;
    spl_knobs(&sh_sp[SP_SCONS - SP_FIRST], &sh_sp[SP_SVOW - SP_FIRST], &sh_sp[SP_SCODA - SP_FIRST],
              &sh_sp[SP_SLEN - SP_FIRST]);
    b = (uint8_t)((b + 1u) & 3u);
    n = spl_syl_bytes(spl.cur, ph[b], len[b]);
    fm1_irq_off();
    mouth_spell_ph = ph[b];
    mouth_spell_len = len[b];
    mouth_spell_n = (uint8_t)n;
    mouth_spell_part = (uint8_t)(sh.part + 1u);
    for (i = 0; resing && mp && i < MOUTH_POLY; i++)
        if (t->v[i].active && t->v[i].gate)
            mth_say_len(&mp->v[i], ph[b], len[b], n);
    fm1_irq_on();
}
/* every frame: SPELL in front loads the phrase PHRASE names into the editor; leaving it, the keys sing the phrase again */
static void sh_spell_poll(void)
{
    static uint8_t was;
    static int16_t from = -1;                           /* the PHRASE the editor was loaded from (-1: never) */
    int on = sh.ready && !sh.wheel && !sh.global && sh_page_ok(SHP_SPELL) && sh.page[sh.mode] == SHP_SPELL;
    if (on && !was) {
        const uint8_t *ph, *len;
        uint32_t n;
        int16_t cur = sh_trk()->p[P_E4];
        if (cur != from || !spl.n) {                    /* back on the page: the phrase as it was left, unless PHRASE moved */
            ph = mouth_phrase((uint32_t)clamp(cur, 0, MOUTH_NPHRASE - 1), &len, &n);
            spl_load(ph, len, n);
            from = cur;
        }
        sh_spell_sync(0);
    } else if (!on && was) {
        fm1_irq_off();
        mouth_spell_n = 0;
        fm1_irq_on();
        sh_splsv.open = 0;                              /* (a SAVE card left open goes with the page) */
    }
    was = (uint8_t)on;
}
static int sh_on_spell(void) { return !sh.global && !sh.wheel && sh.page[sh.mode] == SHP_SPELL && mouth_spell_n; }
/* SAVE on SPELL: the phrase to a user slot U1..U8 (SELECT picks it, OCT+ writes, anything else leaves it), kept in
 * flash with the settings once the sound is quiet (sh_store_poll) */
static void sh_spell_save(int yes)
{
    char l[4] = {'U', (char)('1' + sh_splsv.slot), 0, 0};
    sh_splsv.open = 0;
    if (!yes) {
        sh_toast("NOT SAVED", 0);
        return;
    }
    {
        mouth_user_t *u = &mouth_user[sh_splsv.slot % MOUTH_NUSER];
        uint8_t ph[SPL_MAX + 8], len[SPL_MAX + 8];
        uint32_t n = spl_bytes(ph, len);
        fm1_irq_off();                                  /* (a voice may be singing this slot) */
        memcpy(u->ph, ph, n);
        memcpy(u->len, len, n);
        u->n = (uint8_t)n;
        fm1_irq_on();
    }
#if FELUCCA_FLASH
    phr_pack();                                         /* the record, then a settings write to carry it */
    st_tail_set(&phr_rec, sizeof phr_rec, 1);
    sh_st.pending = 1;
    sh_st.t = fm1_ms;
    sh_st.page = sh.page[sh.mode];
    sh_st.mode = sh.mode;
#endif
    sh_trk()->p[P_E4] = (int16_t)(MTH_NPHRASE + sh_splsv.slot);   /* PHRASE now names it */
    sh_toast("SAVED AS ", l);
}

static void sh_pair_knob(uint32_t k, int32_t s);     /* (the setup page, with the wheel's helpers below) */
static void sh_proj_read(void);
/* SELECT on the shell's pages: one page on, or back, in GLO's order (round) */
static void sh_global_step(int32_t s)
{
    static const uint8_t G[] = {SHG_SCALE, SHG_MOVE, SHG_HARM, SHG_SYSTEM, SHG_SYSTEM2, SHG_PROJECT, SHG_ABOUT, SHG_LEGAL};
    int32_t i, n = (int32_t)NELEM(G);
    for (i = 0; i < n && G[i] != sh.global; i++)
        ;
    sh.global = G[((i < n ? i : 0) + (s > 0 ? 1 : -1) + n) % n];
    if (sh.global == SHG_PROJECT)
        sh_proj_read();
}
static void sh_knob(uint32_t k, int32_t s)
{
    uint8_t ids[4];
    int glob;
    int16_t *vp, was;
    const param_desc_t *d;
    track_t *t = sh_trk();
    uint32_t i;
    if (sh.global == SHG_PAIR) {
        sh_pair_knob(k, s);
        return;
    }
    if (sh_step_knob(k, s))                             /* a step held: its CHNC RATCH NUDGE VEL, or a lock */
        return;
    if (!sh.global)
        sh_lgen_use();                                  /* (a knob turned: the generator in front stays) */
    if (!sh.global && (i = sh_lanes_held()) != 0u) {   /* a lane key held, any page: KNOB 1-4 its DIV LEN FILL ROT */
        uint32_t l;
        for (l = 0; l < 8u; l++)
            if ((i >> l) & 1u)
                sh_lane_knob(t, l, k ? k - 1u : 3u, s);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    if (sh.global == SHG_PROJECT && k == 0u && !sh.pq) {   /* PROJECTS: the first knob walks the slots, as SELECT */
        sh.pslot = (uint8_t)((sh.pslot + (s > 0 ? 1u : 4u)) % 5u);
        return;
    }
    if (sh.global == SHG_ABOUT || sh.global == SHG_LEGAL || sh.global == SHG_NIBS || sh.global == SHG_PROJECT)   /* nothing to turn */
        return;
    if (sh_fmop_page()) {                               /* FM6's operators: the patch's bytes (ui_fm6op.c) */
        fop_knob_f(FO_PAGE[sh.page[sh.mode] - SHP_FMOP][k], k, s);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    sh_page_ids(ids, &glob);
    if (ids[k] == SH_NONE)                              /* a knob the page leaves empty */
        return;
    if (ids[k] == SP_AROT) {                           /* ACID's ROT: the line turned round, live */
        sh_acid_rot(t, s);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    if (ids[k] >= SP_LPAD && ids[k] <= SP_LNAP) {      /* LANE 2, live (sh_lane2_knob) */
        sh_lane2_knob(t, (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, ids[k] - SP_LPAD, s);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    if (ids[k] >= SP_ELEN && ids[k] <= SP_EROT) {      /* the lane in front's LEN FILL ROT, live (sh_lane_knob) */
        sh_lane_knob(t, (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, ids[k] - SP_ELEN, s);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    if (!glob && ids[k] >= P_LZD0 && ids[k] <= P_LZD7 && !lizard_own(t->p))
        sh_lz_take(t, 1);                               /* the drawing it plays becomes its own */
    d = sh_desc(ids[k], glob, &vp);
    was = *vp;
    *vp = (int16_t)param_turn(d, *vp, s);
    if (!glob && ids[k] == P_AMODE && sh_kind() == SHK_CHORD && (*vp == AM_ORD || *vp == AM_REPEAT))
        *vp = (int16_t)(s > 0 ? AM_DNUP : AM_RND);    /* CHORD: no REPEAT (the top note alone), no ORD (UP there) */
    if ((ids[k] == SP_RHY || ids[k] == SP_ASEED) && *vp != was) {   /* RHY or LINE on a page's knob: as ALGORITHM */
        if (ids[k] == SP_ASEED)
            sh_acid_turn(t, was, (uint32_t)*vp);
        else if (sh_li == 1u)
            sh_lwr_turn(t, was, (uint32_t)*vp);
        else
            sh_rhythm_turn(t, was, (uint32_t)*vp);
        sh.hot = (uint8_t)k;
        sh.hot_t = fm1_ms | 1u;
        return;
    }
    if ((ids[k] == SP_DENS || ids[k] == SP_SPAN || ids[k] == SP_LSTY) && *vp != was && sh_kind() == SHK_ACID) {
        if (sh_sp[SP_ASEED - SP_FIRST]) {               /* ACID: the same line, denser or wider */
            int16_t now = *vp;
            *vp = was;
            sh_acid_keep(t);
            *vp = now;
            sh_acid_load(t, (uint32_t)sh_sp[SP_ASEED - SP_FIRST]);
            sh_rhy_sum = sh_beat_sum(t);
        }
    } else if ((ids[k] == SP_DENS || ids[k] == SP_SPAN || ids[k] == SP_LSTY) &&
        *vp != was && sh_sp[SP_RHY - SP_FIRST]) {       /* LINES: the same line, denser or wider (a run of turns: one
                                                         * undo). LEN never writes it: longer, the new steps are empty */
        int16_t now = *vp;
        *vp = was;
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 0);
        *vp = now;
        sh_lwr_load(t, (uint32_t)sh_sp[SP_RHY - SP_FIRST]);
        sh_rhy_sum = sh_beat_sum(t);
    }
    if (!glob && ids[k] == P_SLEN && *vp != was && (sh_kind() == SHK_ALIVE || sh.note == SH_NE_FIRST))
        sh_toast("LEN: FREEZE'S LENGTH, IT RUNS FREE", 0);   /* (ALIVE and TONNETZ evolve on: LEN is what FREEZE takes) */
    if (!glob && ids[k] == P_VOICE && (shell_chord_play >> sh.part) & 1u)
        sh_toast("VCE IS FOR KEYS: CHORD PLAYS POLY", 0);
    if (!glob && ids[k] == P_ITYPE)
        sh_ins_fit(t, was);
    if (ids[k] == SP_CLICK || ids[k] == SP_CIN) {      /* SYSTEM 2: the stock settings' fields, kept with them */
        rp_put(ids[k] == SP_CLICK ? RP_CLICK : RP_COUNTIN, (uint32_t)*vp);
        sh_st.pending = 1;
        sh_st.t = fm1_ms;
        sh_st.page = sh.page[sh.mode];
        sh_st.mode = sh.mode;
    }
    if (!glob && ids[k] == P_AMODE && !was && *vp && sh.page[sh.mode] != SHP_ARP)   /* an arp turned on away from its */
        sh_toast("ARP RATE ON THE ARP PAGE: ", TP[P_ARATE].names[t->p[P_ARATE] % (TP[P_ARATE].max + 1)]);   /* page */
    if (!glob && ids[k] == P_AMODE && !was && *vp && sh_kind() == SHK_CHORD && sh_sp[SP_RPT - SP_FIRST]) {
        sh_sp[SP_RPT - SP_FIRST] = 0;                   /* one clock: the arp or RPT, never both on CHORD's part */
        sh_toast("RPT OFF: THE ARP PLAYS", 0);
    }
    if (ids[k] == SP_RPT && !was && *vp && t->p[P_AMODE]) {
        t->p[P_AMODE] = 0;
        t->p[P_AHOLD] = 0;                              /* (LATCH stays: LATCH with RPT is a rhythm part) */
        sh_toast("ARP OFF: RPT STRUMS", 0);
    }
    if (!glob && ids[k] == P_AMODE && was && !*vp) {   /* MODE OFF: every arp-like thing off in one move. HOLD and */
        t->p[P_AHOLD] = 0;                              /* LATCH go, and the notes they held (seq.c: the arp's list */
        if (sh_kind() == SHK_CHORD)                     /* empties on MODE's edge, keyboard_block lets a latched */
            shell_chord_latch = 0;                      /* root go) */
        sh_toast("ARP OFF", 0);
    }
    if (ids[k] >= SP_SCONS && ids[k] <= SP_SLEN) {     /* SPELL: the syllable takes the edit, sung again if held */
        spl_edit(ids[k] - SP_SCONS, *vp);
        sh_spell_sync(1);
    }
    if (!glob && ids[k] < SP_FIRST) {
        motion_capture(t, ids[k], t->p[ids[k]]);
        ml_knob(t, ids[k]);
        fm1_irq_off();                                  /* (no MOVE in the ISR between the two) */
        if (sh.global == SHG_SCALE)                     /* the scale: every part plays in it */
            for (i = 0; i < NTRK; i++)
                trk[i].p[ids[k]] = t->p[ids[k]];
        if (sh.global == SHG_SCALE && (ids[k] == P_ROOT || ids[k] == P_SCALE))
            sh_move_home(t, ids[k]);                    /* (a scale set by hand is MOVE's home now) */
        fm1_irq_on();
    }
    sh.hot = (uint8_t)k;
    sh.hot_t = fm1_ms | 1u;
}

/* LIBRARY's engines (PRESETS walks their sounds as one list): every engine that plays notes, not a kit, not VODER's
 * bands; SAMPLE's retired aliases skipped, as browsing does */
static const uint8_t CH_ENGINES[] = {0 /* ANALOG */, ENGI_FM6, 2 /* PHASE */, 3 /* LOFI */, ENGI_SAMPLE, 5 /* VOICE */,
                                     6 /* TRIO */, 7 /* WHEEL */, 8 /* GRAIN */, 9 /* PHYS */, ENGI_LIZARD, ENGI_SHOCK,
#if FELUCCA_TALK
                                     ENGI_TALK,
#endif
                                     ENGI_SYLLABLE, ENGI_ORGAN, ENGI_MOUTH};
/* the r-th preset of engine e that browsing shows (ui.c preset_orig: SAMPLE's retired aliases skipped) */
static uint32_t sh_preset_nth(uint32_t e, uint32_t r)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (preset_orig(ENGINES[e], i) == i && !r--)
            return i;
    return 0;
}
/* PRESETS over several engines' factory sounds as one list, round (LIBRARY's) */
static void sh_preset_across(track_t *t, int32_t s, const uint8_t *list, uint32_t ln)
{
    uint32_t e = list[0], i, n = 0, at = 0;
    for (i = 0; i < ln; i++) {                          /* the place of the sound it plays (the presets browsing shows) */
        if (!eng_ok(list[i]))
            continue;
        if (list[i] == t->eng_req)
            at = n + preset_rank(ENGINES[list[i]], t->preset);
        n += preset_shown(list[i]);
    }
    if (!n)
        return;
    at = (uint32_t)(((int32_t)at + s % (int32_t)n + (int32_t)n) % (int32_t)n);
    for (i = 0; i < ln; i++) {
        if (!eng_ok(list[i]))
            continue;
        e = list[i];
        if (at < preset_shown(e))
            break;
        at -= preset_shown(e);
    }
    undo_depth++;
    if (t->eng_req != e)
        set_engine_of(t, e);
    sh_apply_preset(t, sh_preset_nth(e, at));
    undo_depth--;
}

/* GLO + PRESETS in LIBRARY, which walks several engines: the next (s > 0) or previous engine's first
 * sound, instead of every sound in between */
static void sh_preset_jump(int32_t s)
{
    track_t *t = sh_trk();
    const uint8_t *list = CH_ENGINES;
    uint32_t ln = NELEM(CH_ENGINES), i, at = 0, k;
    for (i = 0; i < ln; i++)
        if (list[i] == t->eng_req)
            at = i;
    for (k = 1; k <= ln; k++) {
        i = (at + (s > 0 ? k : ln - k)) % ln;
        if (eng_ok(list[i]) && preset_shown(list[i]))
            break;
    }
    if (k > ln)
        return;
    undo_depth++;
    if (t->eng_req != list[i])
        set_engine_of(t, list[i]);
    sh_apply_preset(t, sh_preset_nth(list[i], 0));
    undo_depth--;
    sh_sndu.on = sh_sndu.newer = 1;
    sh_sndu.part = sh.part;
    sh.pop = 1;
    sh.pop_t = fm1_ms | 1u;
}

/* the user slots (upreset.c) holding engine e's sounds: how many, the n-th, a slot's place among them */
static int sh_user_of(uint32_t e, uint32_t k) { return up_used(k) && up_engine(k) == e; }
static uint32_t sh_user_n(uint32_t e)
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)sh_user_of(e, k);
    return n;
}
static uint32_t sh_user_nth(uint32_t e, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (sh_user_of(e, k) && !n--)
            return k;
    return 0;
}
static uint32_t sh_user_rank(uint32_t e, uint32_t slot)
{
    uint32_t k, n = 0;
    for (k = 0; k < slot; k++)
        n += (uint32_t)sh_user_of(e, k);
    return n;
}

/* PRESETS: the engine's factory sounds, then the player's own of that engine (from SAVE), round */
static void sh_preset(int32_t s)
{
    track_t *t = sh_trk();
    uint32_t e = sh_eng(), nf = ENGINES[e]->npresets, nu = sh_user_n(e), at;
    int32_t n = (int32_t)(nf + nu), p;
    if (SH_LIB(sh.mode)) {                        /* LIBRARY: every other engine's sounds as one list */
        sh_preset_across(t, s, CH_ENGINES, NELEM(CH_ENGINES));
        sh_sndu.on = sh_sndu.newer = 1;
        sh_sndu.part = sh.part;
        sh.pop = 1;
        sh.pop_t = fm1_ms | 1u;
        return;
    }
    if (n <= 0)
        return;
    at = t->user && sh_user_of(e, t->user - 1u) ? nf + sh_user_rank(e, t->user - 1u) : t->preset;
    p = ((int32_t)at + s % n + n) % n;
    if ((uint32_t)p < nf)
        sh_apply_preset(t, (uint32_t)p);
    else
        up_load(sh_user_nth(e, (uint32_t)p - nf));     /* up_load writes the part song.sel selects, the one in front; param_kept keeps the NOTE and SHARED ids */
    sh_sndu.on = sh_sndu.newer = 1;
    sh_sndu.part = sh.part;
    sh.pop = 1;
    sh.pop_t = fm1_ms | 1u;
}

/* SAVE tapped: the slot the sound came from if it is a user slot of this engine, else the first free one */
/* PROJECTS (GLO's third page): the four project slots A..D, the music saved whole with its pairs (project.c FUN11)
 * and loaded back (sh_project_loaded). OCT+ asks to save to the slot under the cursor, OCT- to load it; OCT+ again is
 * yes, anything else no. Their names are read on the page's way in and after a save (each read decodes a slot) */
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n);   /* (project.c, after */
static int project_save(uint32_t slot);                 /* this file) */
static void project_load(uint32_t slot);
static void project_new(void);
static int project_new_kept(void);
static void project_new_undo(void);
static char sh_pname[4][13];                            /* (project.c PROJ_NAME_LEN + 1) */
static uint8_t sh_pused;                                /* bit k: slot k holds a project */
static void sh_proj_read(void)
{
    uint32_t k;
    sh_pused = 0;
    for (k = 0; k < 4u; k++)
        if (project_info(k, sh_pname[k], 0, 0, 0))
            sh_pused |= (uint8_t)(1u << k);
    sh.pgen++;
}
static void sh_proj_ask(uint32_t q)
{
    if (sh.pslot == 4u)                                 /* NEW: either OCT asks */
        q = 3u;
    if (q == 2u && !((sh_pused >> sh.pslot) & 1u)) {
        sh_toast("THAT SLOT IS EMPTY", 0);
        return;
    }
    sh.pq = (uint8_t)q;
}
/* a save asked for while it plays waits for the stop (a flash write while playing: never), then writes what is there
 * then; a load or NEW before it takes it back. 0: none, else the slot + 1 */
static uint8_t sh_psave;
static void sh_psave_poll(void)
{
    char b[16];
    uint32_t k = sh_psave - 1u;
    if (!sh_psave || song.playing || transport_req)
        return;
    sh_psave = 0;
    ui.msg[0] = 0;
    project_save(k);
    sh_proj_read();
    str_cpy(b, "SAVED TO ", 10);
    b[9] = (char)('A' + k % 4u);
    b[10] = 0;
    sh_toast(ui.msg[0] ? ui.msg : b, 0);
}
static void sh_proj_answer(int yes)
{
    uint32_t q = sh.pq, k = sh.pslot;
    sh.pq = 0;
    if (!yes) {
        sh_toast(q == 1u ? "NOT SAVED" : q == 3u ? "KEPT AS IT IS" : "NOT LOADED", 0);
        return;
    }
    if (q == 3u) {
        sh_psave = 0;
        project_new();
        sh_toast_undo("NEW: EMPTY PROJECT", 0);
        return;
    }
    ui.msg[0] = 0;
    if (q == 1u && (song.playing || transport_req == 1u)) {
        char b[20];
        sh_psave = (uint8_t)(k + 1u);
        str_cpy(b, "SAVES TO ", 10);
        b[9] = (char)('A' + k % 4u);
        str_cpy(b + 10, " AT STOP", 9);
        sh_toast(b, 0);
        return;
    }
    if (q != 1u)
        sh_psave = 0;
    if (q == 1u) {
        project_save(k);
        sh_proj_read();
    } else
        project_load(k);
    sh_toast(ui.msg[0] ? ui.msg : (q == 1u ? "SAVED" : "LOADED"), 0);
}

static void sh_save_ask(void)
{
    track_t *t = sh_trk();
    uint32_t k, e = t->eng_req;
    if (song.playing || transport_req == 1u) {          /* upreset.c up_put refuses too: no flash write while playing */
        sh_toast("STOP TO SAVE", 0);
        return;
    }
    if (t->user && up_used(t->user - 1u) && up_engine(t->user - 1u) == (e == ENGI_DIGITAL ? ENGI_FM6 : e)) {
        k = t->user - 1u;
    } else {
        for (k = 0; k < UP_SLOTS && up_used(k); k++)
            ;
        if (k >= UP_SLOTS) {
            sh_toast("USER SOUNDS FULL", 0);
            return;
        }
    }
    sh.save = 1;
    sh.save_slot = (uint8_t)k;
    sh.save_t = fm1_ms | 1u;
}

static void sh_save_answer(int yes)
{
    track_t *t = sh_trk();
    char l[4], name[13];
    uint32_t k = sh.save_slot;
    int rc;
    sh.save = 0;
    if (!yes) {
        sh_toast("NOT SAVED", 0);
        return;
    }
    up_slot_label(l, k);
    name[0] = 0;
    if (up_used(k))
        up_name(k, name);                               /* an overwrite keeps the slot's name */
    rc = up_store(k, name);
    if (rc == 0 || rc == 3)
        t->user = (uint8_t)(k + 1u);
    sh_toast(rc == 0 ? "STASHED " : rc == 3 ? "STASHED (RAM) " : "SAVE ERROR ", l);
}

static uint32_t sh_lanes_len(uint32_t k)               /* lanes engine k's part's LEN, the pattern's */
{
    const track_t *t = &trk[SH_NOTES[k % SH_NLI ? SH_N_LINES : SH_N_RUDIMENT].part % NTRK];
    return t->p[P_SLEN] > 0 ? (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP) : 1u;
}
static uint32_t sh_lane_own(uint32_t l) { return (uint32_t)clamp(sh_eu[0][l % 8u], 0, NSTEP); }   /* 0: =LEN */
static int16_t sh_elen_turn(int16_t v, int32_t s)       /* a lane's LEN knob: =LEN at the top, its own below the part's */
{
    int32_t n = (int32_t)sh_lanes_len(sh_li), at = v > 0 && v < n ? v : n;
    at = clamp(at + s, 1, n);
    return (int16_t)(at >= n ? 0 : at);
}
static uint32_t sh_lane_pad(uint32_t l) { return (uint32_t)clamp(sh_lx[SH_LX_PAD][l % 8u], 0, 16); }
static uint32_t sh_lane_ownc(uint32_t l)                /* seq.c's own cycle for lane l: its LEN + PAD (0: the part's) */
{
    uint32_t o = sh_lane_own(l);
    return o ? (o + sh_lane_pad(l) < NSTEP ? o + sh_lane_pad(l) : NSTEP) : 0u;
}
static uint32_t sh_lane_len(uint32_t l)                 /* the steps lane l plays round: its own LEN + PAD, or the part's */
{
    uint32_t n = sh_lanes_len(sh_li), o = sh_lane_ownc(l);
    return o && o < n ? o : n;
}
static uint32_t sh_lane_span(uint32_t l)                /* the steps its FILL spreads over: the cycle less PAD */
{
    uint32_t n = sh_lane_len(l), p = sh_lane_pad(l);
    return p < n ? n - p : 1u;
}
/* lane l's cycle (sh_eu: LEN, FILL hits spread evenly over it, turned by ROT) written into its bits, the steps past
 * its LEN cleared; seq.c plays it at its LEN */
static int sh_euclid_at(uint32_t l, uint32_t i)        /* lane l's Euclid hit on step i of its cycle (PAD: none past its
                                                         * span, turned by ROT with the rest) */
{
    uint32_t n = sh_lane_len(l), m = sh_lane_span(l), k = (uint32_t)clamp(sh_eu[1][l], 0, (int32_t)m),
             rot = (uint32_t)clamp(sh_eu[2][l], 0, NSTEP - 1) % n, j = (i + n - rot) % n;
    return i < n && j < m && (j * k) % m < k;           /* Bresenham's Euclid: k hits evenly over m */
}
static void sh_euclid_lane(track_t *t, uint32_t l)
{
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        sh_grid_hit(t, i, l, (uint32_t)sh_euclid_at(l, i));
    shell_lane_len[l] = (uint8_t)sh_lane_ownc(l);
    fm1_irq_on();
}
static int sh_lane_logic(uint32_t l) { return sh_eu[1][l % 8u] < 0 && l % 8u >= 2u; }   /* (FILL left of 0) */
/* the lanes in front's columns as they play, the logic lanes worked out (each lane at column c of its own LEN, FLIP and
 * LATCH walked from column 1): what the screen and the step LEDs show for a logic lane */
static void sh_lane_cols(const track_t *t, uint8_t hit[NSTEP], uint8_t acc[NSTEP])
{
    uint8_t ff[NLANE] = {0};
    uint32_t c, l;
    for (c = 0; c < NSTEP; c++) {
        uint8_t h = 0, a = 0;
        for (l = 0; l < 8u; l++) {
            const step_t *st = &t->step[c % sh_lane_len(l)];
            h |= (uint8_t)(((step_lanes(st) >> l) & 1u) << l);
            a |= (uint8_t)(((step_accents(st) >> l) & 1u) << l);
        }
        lane_logic(sh_li, &h, &a, ff);
        hit[c] = h;
        acc[c] = a;
    }
}
static uint8_t sh_lc_hit[NSTEP], sh_lc_acc[NSTEP];      /* (sh_lane_cols, for the screen and the LEDs) */
/* the lanes engine k (0 RUDIMENT, 1 LINES) to the knobs and the screen: its RHY, LANE and OCT into sh_sp */
static void sh_lanes_use(uint32_t k)
{
    uint32_t i;
    k %= SH_NLI;
    if (k == sh_li)
        return;
    for (i = 0; i < 3u; i++) {
        sh_lsp[sh_li][i] = sh_sp[SH_LSP_IDS[i] - SP_FIRST];
        sh_sp[SH_LSP_IDS[i] - SP_FIRST] = sh_lsp[k][i];
    }
    sh_li = (uint8_t)k;
}
static int16_t sh_lanes_sp(uint32_t k, uint32_t i)      /* engine k's RHY / LANE / OCT, wherever it is kept */
{
    return k == sh_li ? sh_sp[SH_LSP_IDS[i] - SP_FIRST] : sh_lsp[k][i];
}
/* what each lane plays (seq.c shell_lane_note_i), every frame, for both lanes engines: on its part, a lane's NOTE,
 * or AUTO: DRUMS its piece (eng_drum.c DRUM_LANE_NOTE), any other SOUND degree l of the part's SCALE up from its
 * ROOT, in octave 3 (MIDI 48 + ROOT) + OCT (CHROMATIC: MAJOR's degrees). The names of the set in front follow: the
 * pieces, or the notes. LINES' part counts as a lanes part only while LINES holds it (ALIVE shares that part) */
static void sh_lane_notes(void)
{
    static char nm[8][5];
    uint32_t k, lp = SH_NOTES[SH_N_LINES].part + 1u;
    uint8_t on = (uint8_t)(sh_track_note(lp - 1u) == SH_N_LINES ? lp : 0u);
    if (on && !shell_lanes_part[1]) {                   /* LINES back on its part: its lanes from the part's own step, */
        const track_t *t = &trk[lp - 1u];               /* in time with the loop and RUDIMENT, not where they stopped */
        fm1_irq_off();
        for (k = 0; k < NLANE; k++) {
            uint32_t n = shell_lane_len_i[1][k] ? shell_lane_len_i[1][k] : sh_lanes_len(1u);
            shell_lane_pos_i[1][k] = (uint8_t)((t->seq_idx + 1u) % n);
        }
        shell_lanes_part[1] = on;
        fm1_irq_on();
    }
    shell_lanes_part[1] = on;
    for (k = 0; k < SH_NLI * 8u; k++) {                 /* the logic lanes (FILL left of 0) and the DIVs to seq.c */
        int32_t f = sh_eu_i[k / 8u][1][k % 8u];
        shell_lane_op_i[k / 8u][k % 8u] = (uint8_t)(f < 0 && k % 8u >= 2u ? clamp(-f, 1, LOP_N - 1) : 0);
        shell_lane_div_i[k / 8u][k % 8u] = SH_DIV_TW[clamp(sh_ediv_i[k / 8u][k % 8u], SH_DIV_MIN, SH_DIV_MAX) - SH_DIV_MIN];
        shell_lane_loop_i[k / 8u][k % 8u] = SH_LOOP_BARS[clamp(sh_lx_i[k / 8u][SH_LX_LOOP][k % 8u], 0, 4)];
        shell_lane_wake_i[k / 8u][k % 8u] = (uint8_t)clamp(sh_lx_i[k / 8u][SH_LX_WAKE][k % 8u], 1, 8);
        shell_lane_nap_i[k / 8u][k % 8u] = (uint8_t)clamp(sh_lx_i[k / 8u][SH_LX_NAP][k % 8u], 0, 8);
    }
    sh_lane_cols(&trk[SH_NOTES[sh_li ? SH_N_LINES : SH_N_RUDIMENT].part % NTRK], sh_lc_hit, sh_lc_acc);   /* (the screen's) */
    for (k = 0; k < SH_NLI; k++) {
        const track_t *t = &trk[SH_NOTES[k ? SH_N_LINES : SH_N_RUDIMENT].part % NTRK];
        uint32_t mask = scale_mask(t), pcs[12], n = 0, i, drums = t->eng_req == ENGI_DRUM, root = (uint32_t)t->p[P_ROOT] % 12u;
        int32_t oct = sh_lanes_sp(k, 2);
        if (mask == 0xFFFu || !mask)
            mask = SCALE_MASK[1];                       /* (MAJOR) */
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u)
                pcs[n++] = i;
        for (i = 0; i < 8u; i++) {
            int32_t v = sh_lnote_i[k][i];
            if (!v)
                v = drums ? (int32_t)DRUM_LANE_NOTE[i] : 48 + (int32_t)root + (int32_t)pcs[i % n] + 12 * ((int32_t)(i / n) + oct);
            if (k == 1u && !drums)
                v += sh_sp[SP_LTRN - SP_FIRST];         /* LINES' TRN, as it plays (the steps stay) */
            v = clamp(v, 0, 127);
            shell_lane_note_i[k][i] = (uint8_t)v;
            if (k != sh_li)
                continue;
            if (drums && !sh_lnote_i[k][i]) {
                N_SP_LANE[i] = drum_lane_name(t, i);        /* (the kit's own pieces: DIGI's POP TICK BLIP) */
            } else {
                int32_t w = clamp(v + (drums ? 0 : 12 * sh_lopg[k % 2u]), 0, 127);   /* (named in the octave in view) */
                str_cpy(nm[i], N_NOTE[w % 12], 3);
                fmt_int(nm[i] + str_len(nm[i]), w / 12 - 1);
                N_SP_LANE[i] = nm[i];
            }
        }
    }
}
static uint32_t sh_lane_now(uint32_t l)               /* the step lane l plays now (seq.c: pos is the next) */
{
    uint32_t n = sh_lane_len(l);
    return (shell_lane_pos[l % 8u] + n - 1u) % n;
}

/* the step pages: the white keys are 16 steps of a pattern up to 64 long, the left of the two black keys past C5
 * (C#5) a page back, the right (D#5) a page forward, whatever their print: with nothing held on the lanes (RUDIMENT,
 * LINES) and on KEYS' and CHORD's SEQ page, with SEQ held on any step track (there those keys play notes). The view
 * stays where it is put, as a MiniBrute's: the playhead is drawn only on its own page, the page key toward it pulsing
 * on the beat; PLAY from stop, another pair or mode: page 1. A LEN that loses the page in view moves it to the last.
 * F#5 where the page keys page, nothing held: FOLLOW, the view on the playhead's page while it plays (a setting, off) */
#define SH_PG_LEFT 20u
#define SH_PG_RIGHT 22u
#define SH_PG_FOLLOW 25u
#define SH_PREF_FOLLOW 0x80u                            /* (ui_prefs2's bit 7: FOLLOW, kept with the settings) */
static uint8_t sh_pg_hand;                              /* the page in view - 1 (0: page 1) */
static int sh_pg_kind(void)
{
    return sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD || sh_kind() == SHK_RUDIMENT || sh_kind() == SHK_ACID;
}
static uint32_t sh_pg_len(void)                         /* the steps the pages cover: the part's LEN */
{
    const track_t *t = sh_trk();
    return t->p[P_SLEN] > 0 ? (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP) : 16u;
}
static uint32_t sh_pg_n(void) { return (sh_pg_len() + 15u) / 16u; }
static uint32_t sh_pg_now(void) { return sh_trk()->seq_idx % sh_pg_len(); }   /* the step that plays now */
static uint32_t sh_pg(void)                             /* the first step of the page in view */
{
    uint32_t n = sh_pg_n();
    return 16u * (sh_pg_hand < n ? sh_pg_hand : n - 1u);
}
static int sh_pg_free(void)                             /* the page keys page now (above) */
{
    if (!sh_pg_kind() || sh.wheel || sh.global || sh_held(B_GLO) || sh_held(B_HOME))
        return 0;
    return sh_held(B_SEQ) || sh_kind() == SHK_RUDIMENT || sh.page[sh.mode] == SHP_SEQ ||
           (sh_kind() == SHK_ACID && !song.playing);
}
static void sh_pg_step(int32_t d)                       /* a page back or forward, and say which */
{
    char b[24];
    uint32_t n = sh_pg_n(), p = (uint32_t)clamp((int32_t)(sh_pg() / 16u) + d, 0, (int32_t)n - 1), e;
    sh_pg_hand = (uint8_t)p;
    e = 16u * p + 16u < sh_pg_len() ? 16u * p + 16u : sh_pg_len();
    str_cpy(b, n < 2u ? "ONE PAGE: STEPS " : "STEPS ", 18);
    fmt_int(b + str_len(b), (int32_t)(16u * p + 1u));
    str_cpy(b + str_len(b), "-", 2);
    fmt_int(b + str_len(b), (int32_t)e);
    sh_toast(b, 0);
}
static void sh_pg_keys(void);
static void sh_pg_poll(void)                            /* PLAY from stop, another pair or mode: page 1 */
{
    static uint8_t was, mode, note;
    if ((song.playing && !was) || sh.mode != mode || sh.note != note)
        sh_pg_hand = 0;
    if (sh_pg_hand >= sh_pg_n())
        sh_pg_hand = (uint8_t)(sh_pg_n() - 1u);
    if ((ui_prefs2 & SH_PREF_FOLLOW) && song.playing && sh_pg_kind())
        sh_pg_hand = (uint8_t)(sh_pg_now() / 16u);
    was = song.playing;
    mode = sh.mode;
    note = sh.note;
    sh_pg_keys();
}

/* rhythm r onto the part: its LEN the longest lane's (a bar of the metre, 16 for EMPTY), the lanes that long
 * following it, the others their own cycle (LEN FILL ROT); the old beat gone */
static void sh_rhythm_load(track_t *t, uint32_t r)
{
    const sh_rhy_t *y = &SH_RHY[r % SH_NRHY];
    uint32_t l, len = 0;
    for (l = 0; l < 8u; l++)
        len = y->lane[l][0] > len ? y->lane[l][0] : len;
    t->p[P_SLEN] = (int16_t)(len ? len : 16u);
    memset(shell_lstep[sh_li % 2u], 0, sizeof shell_lstep[0]);   /* (a new beat: no chance or ratchet of the last) */
    sh_lo_clear(t->step);                               /* (nor octaves) */
    for (l = 0; l < 8u; l++) {
        sh_eu[0][l] = (int16_t)(y->lane[l][0] == len ? 0u : y->lane[l][0]);
        sh_eu[1][l] = (int16_t)(y->lane[l][0] ? y->lane[l][1] : 0u);
        sh_eu[2][l] = (int16_t)y->lane[l][2];
        sh_ediv[l] = 0;                                 /* (a rhythm is written at the part's step) */
        sh_lx[SH_LX_PAD][l] = 0;
        sh_euclid_lane(t, l);
    }
}
static uint32_t sh_beat_sum(const track_t *t)            /* the part's steps, lanes and LEN, FNV-1a (never 0) */
{
    const uint8_t *b = (const uint8_t *)t->step;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof t->step; i++)
        h = (h ^ b[i]) * 16777619u;
    for (b = (const uint8_t *)sh_eu, i = 0; i < sizeof sh_eu; i++)
        h = (h ^ b[i]) * 16777619u;
    for (b = (const uint8_t *)sh_lx, i = 0; i < sizeof sh_lx; i++)
        h = (h ^ b[i]) * 16777619u;
    return (h ^ (uint16_t)t->p[P_SLEN]) | 1u;
}
/* a beat the shell is about to rewrite (a rhythm load, a lane's LEN FILL ROT, an erase, a clear): kept for REC held
 * (UNDO BEAT), with the lanes and the RHYTHM it showed. A knob's rewrite (always 0) keeps nothing when the beat is
 * the one the last rewrite left, so a run of turns keeps the beat from before the run; an erase or a clear always keeps */
static struct { uint8_t on, note; int16_t lnote[8]; } sh_frzu;   /* a generator's FREEZE to undo: LINES' notes before */
static int sh_beat_keep(track_t *t, int16_t rhy, int always)   /* 1: kept now */
{
    sh_sndu.newer = 0;                                  /* (the beat changed last: REC held undoes it first) */
    sh_frzu.on = 0;
    if (!always && sh_beat_sum(t) == sh_rhy_sum)
        return 0;
    fm1_irq_off();
    memcpy(sh_undo, t->step, sizeof sh_undo);
    memcpy(sh_undo_ls, shell_lstep[sh_li % 2u], sizeof sh_undo_ls);
    sh_mundo_n = sh_mundo_ok = 0;
    memcpy(sh_undo_eu, sh_eu, sizeof sh_undo_eu);
    memcpy(sh_undo_lx, sh_lx, sizeof sh_undo_lx);
    memcpy(sh_undo_div, sh_ediv, sizeof sh_undo_div);
    sh_undo_rhy = rhy;
    sh_take.ulen = t->p[P_SLEN];
    sh_take.part = (uint8_t)trk_index(t);
    sh_take.note = sh.note;
    sh_take.have = sh_take.rhy = 1;
    sh_take.dbl = 0;
    fm1_irq_on();
    return 1;
}
/* ALGORITHM turned to another rhythm: the beat it replaces is kept (UNDO BEAT). MAP: the map takes KICK, SNARE and
 * HATCL (seq.c MAP_LANE), emptied and following LEN, which goes to 32, the map's length (as a moon sets its bar);
 * the other lanes keep their beat */
static void sh_rhythm_turn(track_t *t, int16_t was, uint32_t r)
{
    static const uint8_t ML[3] = {0, 1, 3};
    uint32_t i, k;
    sh_beat_keep(t, was, 0);
    if (r == SH_MAP) {
        fm1_irq_off();
        for (k = 0; k < 3u; k++) {
            sh_eu[0][ML[k]] = sh_eu[1][ML[k]] = sh_eu[2][ML[k]] = 0;
            shell_lane_len[ML[k]] = 0;
            t->p[P_SLEN] = 32;                          /* (the map's length: all of it plays) */
            for (i = 0; i < NSTEP; i++)
                sh_grid_hit(t, i, ML[k], 0u);
        }
        fm1_irq_on();
        sh_toast_undo("MAP: KICK SNARE HATS ON THE MAP", 0);
    } else {
        sh_rhythm_load(t, r);
        sh_toast_undo("NEW BEAT: ", N_SP_RHY[r % SH_NRHY]);
    }
    sh_take.n = 0;
    sh_rhy_sum = sh_beat_sum(t);
}

/* LINE, LINES' line writer: a seeded line on its lanes, after TB-3PO's idea (Hemisphere; written fresh), as long as the part's LEN
 * when it is drawn (32 at most; LEN never draws it again: a longer LEN's new steps are empty). Lane l is the scale's
 * degree l, so the line stays in SCALE. Every draw is made first, two a step for all 32 in a fixed order, so DENS,
 * SPAN and the LEN it was drawn at only choose from the same line: the DENS steps
 * with the highest priority play (step 1 always), so one more DENS adds one note and moves none; the degree walks every
 * step, sounding or not, in a fixed 0..7 (mostly by step, sometimes home to the root, sometimes a leap), and SPAN
 * stretches that contour over its lanes. Drawn at a longer LEN, it plays in the steps the shorter one had only notes
 * the shorter one played, on the same lanes (the DENS notes spread over the new steps too). A quarter of the notes
 * accented, some sliding into the next, a silent step after a note sometimes holding it (TIE). The same seed, DENS,
 * SPAN and LEN give the same line */
#define SH_LWR_MAX 32u
static uint32_t sh_lwr_rng(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}
/* the LINE writer's draw for a seed: each step's bits (SH_LW_*) and its degree 0..7 (<< 4) over len steps */
enum { SH_LW_ON = 1, SH_LW_ACC = 2, SH_LW_SLD = 4, SH_LW_TIE = 8 };
/* STYLE: how the degrees go over the same draws and the same DENS steps. WALK (TB-3PO's: by step, home, a leap);
 * OCTAVE (the 303 bounce: the root and its octave, a fifth now and then, sliding on the jumps); PEDAL (the root under
 * every other note, the walk between, Berlin school); ARP (the chord broken up, down or both, the octave on top);
 * CALL (the first half walks, the second answers it: the same notes, its last quarter varied and ending home) */
enum { SH_LS_WALK, SH_LS_OCTAVE, SH_LS_PEDAL, SH_LS_ARP, SH_LS_CALL };
static void sh_lwr_draw(uint32_t seed, uint32_t len, uint32_t dens, uint32_t style, uint8_t out[SH_LWR_MAX])
{
    static const uint8_t CH[6] = {0, 2, 4, 7, 4, 2};   /* ARP: the triad and the octave, up and back */
    uint32_t x = seed * 2654435761u ^ 0x2C1B3C6Du, prio[SH_LWR_MAX], r[SH_LWR_MAX], on = 1u, i, j, n, d8 = 0, held = 0;
    uint32_t shape, c = 0, half = len / 2u;
    uint8_t dw[SH_LWR_MAX];
    for (i = 0; i < SH_LWR_MAX; i++) {                 /* every draw, in order, whatever the length */
        prio[i] = sh_lwr_rng(&x);
        r[i] = sh_lwr_rng(&x);
        out[i] = 0;
    }
    shape = sh_lwr_rng(&x) % 3u;                        /* (ARP: up, down, both; after WALK's draws, so WALK's lines
                                                         * are the ones a seed always gave) */
    for (n = 1; n < dens; n++) {                        /* the next step by priority (ties: the earlier) */
        for (j = 0, i = 1; i < len; i++)
            if (!((on >> i) & 1u) && (!j || prio[i] > prio[j]))
                j = i;
        on |= 1u << j;
    }
    for (i = 0; i < len; i++) {                         /* the walk, every step, sounding or not */
        uint32_t m = r[i] % 8u;
        if (i && m < 2u)
            d8 = 0;                                     /* home */
        else if (i && m < 6u)
            d8 = (uint32_t)clamp((int32_t)d8 + ((r[i] >> 3) & 1u ? 1 : -1), 0, 7);   /* a step */
        else if (i)
            d8 = (r[i] >> 8) % 8u;                      /* a leap */
        dw[i] = (uint8_t)d8;
    }
    for (i = 0; i < len; i++) {
        uint32_t m = r[i] % 8u, nx = i + 1u < len && ((on >> (i + 1u)) & 1u), acc = (r[i] >> 12) % 4u == 0u,
                 sld = nx && (r[i] >> 16) % 5u == 0u;
        if (!((on >> i) & 1u)) {
            if (held && (r[i] >> 4) % 3u == 0u)
                out[i] = SH_LW_TIE;                     /* the note before holds on */
            else
                held = 0;
            continue;
        }
        switch (style) {
        case SH_LS_OCTAVE:
            d8 = m < 4u ? 0u : m < 7u ? 7u : 4u;
            acc = d8 == 7u ? (r[i] >> 12) % 2u == 0u : acc;
            sld = nx && (r[i] >> 16) % 3u == 0u;
            break;
        case SH_LS_PEDAL:
            d8 = c % 2u ? (dw[i] ? dw[i] : 2u) : 0u;
            sld = nx && c % 2u && (r[i] >> 16) % 6u == 0u;
            break;
        case SH_LS_ARP:
            d8 = shape == 0u ? CH[c % 4u] : shape == 1u ? CH[3u - c % 4u] : CH[c % 6u];
            acc = c % 4u == 0u;
            sld = 0;
            break;
        case SH_LS_CALL:
            d8 = i < half || len < 4u ? dw[i] : dw[i - half];
            if (len >= 4u && i >= len - len / 4u)       /* the answer's tail: moved a step or two, home at the end */
                d8 = (uint32_t)clamp((int32_t)d8 + ((r[i] >> 3) & 1u ? 1 : -1) * (int32_t)(1u + (r[i] >> 5) % 2u), 0, 7);
            break;
        default:
            d8 = dw[i];
            break;
        }
        out[i] = (uint8_t)(SH_LW_ON | d8 << 4 | (acc ? SH_LW_ACC : 0) | (sld ? SH_LW_SLD : 0));
        held = 1;
        c++;
    }
    if (style == SH_LS_CALL && len >= 4u)               /* (the answer ends home: its last note the root) */
        for (i = len; i-- > half;)
            if (out[i] & SH_LW_ON) {
                out[i] = (uint8_t)(out[i] & 0x0Fu);
                break;
            }
}
static uint32_t sh_lwr_lane(uint8_t w, uint32_t span)  /* a drawn degree 0..7 spread over SPAN lanes (OCTAVE's top:
                                                         * the octave, lane 7, whatever SPAN) */
{
    if ((w >> 4) == 7u && sh_sp[SP_LSTY - SP_FIRST] == SH_LS_OCTAVE)
        return 7u;
    return ((uint32_t)(w >> 4) * (span - 1u) + 3u) / 7u;
}
static void sh_lwr_load(track_t *t, uint32_t seed)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, (int32_t)SH_LWR_MAX),
             dens = (uint32_t)clamp(sh_sp[SP_DENS - SP_FIRST], 1, (int32_t)len),
             span = (uint32_t)clamp(sh_sp[SP_SPAN - SP_FIRST], 1, 8), i, l;
    uint8_t w[SH_LWR_MAX];
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    memset(shell_lstep[1], 0, sizeof shell_lstep[1]);   /* (a new line: LINES' bytes with its columns) */
    for (l = 0; l < 8u; l++) {
        sh_eu[0][l] = sh_eu[1][l] = sh_eu[2][l] = 0;
        sh_ediv[l] = 0;
        sh_lx[SH_LX_PAD][l] = 0;
        shell_lane_len[l] = 0;
    }
    fm1_irq_on();
    if (!seed)
        return;
    sh_lwr_draw(seed, len, dens, (uint32_t)sh_sp[SP_LSTY - SP_FIRST], w);
    fm1_irq_off();
    for (i = 0; i < len; i++) {
        if (w[i] & SH_LW_TIE)
            t->step[i].time = ST_TIE;
        if (!(w[i] & SH_LW_ON))
            continue;
        l = sh_lwr_lane(w[i], span);
        sh_grid_hit(t, i, l, 1u);
        if (w[i] & SH_LW_ACC)
            grid_acc(t, i, l, 1u);
        if (w[i] & SH_LW_SLD)
            t->step[i].flags |= SF_SLIDE;               /* into the next note */
        sh_eu[1][l]++;                                  /* (FILL shows the lane's hits) */
    }
    fm1_irq_on();
}
/* ACID's line from the LINE writer: the same draws, one note a step, its degree of SCALE (CHR: MAJOR's) up from ROOT
 * in octave 3, spread over SPAN degrees; ACC, SLD and TIE as drawn */
static uint8_t sh_acid_last = 48;                       /* the last note entered on ACID: a new step takes it */
static uint32_t sh_acid_deg(const track_t *t, uint32_t d)   /* degree d of the part's scale up from ROOT, octave 3 */
{
    uint32_t mask = scale_mask(t), pcs[12], n = 0, i;
    if (mask == 0xFFFu || !mask)
        mask = SCALE_MASK[1];
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            pcs[n++] = i;
    return (uint32_t)clamp(48 + t->p[P_ROOT] % 12 + (int32_t)pcs[d % n] + 12 * (int32_t)(d / n), 0, 127);
}
static void sh_acid_load(track_t *t, uint32_t seed)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, (int32_t)SH_LWR_MAX),
             dens = (uint32_t)clamp(sh_sp[SP_DENS - SP_FIRST], 1, (int32_t)len),
             span = (uint32_t)clamp(sh_sp[SP_SPAN - SP_FIRST], 1, 8), i;
    uint8_t w[SH_LWR_MAX];
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    fm1_irq_on();
    sh_sp[SP_AROT - SP_FIRST] = 0;
    if (!seed)
        return;
    sh_lwr_draw(seed, len, dens, (uint32_t)sh_sp[SP_LSTY - SP_FIRST], w);
    fm1_irq_off();
    for (i = 0; i < len; i++) {
        step_t *st = &t->step[i];
        if (w[i] & SH_LW_TIE)
            st->time = ST_TIE;
        if (!(w[i] & SH_LW_ON))
            continue;
        step_just_clear(st);
        st->note[0] = (uint8_t)sh_acid_deg(t, sh_lwr_lane(w[i], span));
        st->time = ST_NOTE;
        st->flags = (uint8_t)((w[i] & SH_LW_ACC ? SF_ACCENT : 0) | (w[i] & SH_LW_SLD ? SF_SLIDE : 0));
        st->n = 1;
    }
    fm1_irq_on();
}
/* LINES' or ACID's line, edited by hand: the line is kept as it is from now (its seed 0), so DENS and SPAN never
 * write over the edit; said once */
static void sh_lines_kept(void)
{
    int16_t *sd = sh.note == SH_N_ACID ? &sh_sp[SP_ASEED - SP_FIRST] :
                  sh_li == 1u && sh.note == SH_N_LINES ? &sh_sp[SP_RHY - SP_FIRST] : 0;
    if (!sd || !*sd)
        return;
    *sd = 0;
    sh_toast("KEPT: THE LINE IS YOURS", 0);
}
/* ALGORITHM on LINES: another line; the one it replaces kept (UNDO BEAT) */
static void sh_lwr_turn(track_t *t, int16_t was, uint32_t seed)
{
    static char b[12];
    sh_beat_keep(t, was, 0);
    sh_lwr_load(t, seed);
    if (seed) {
        str_cpy(b, "LINE ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)seed);
    }
    sh_toast_undo("NEW LINE: ", seed ? b : "EMPTY");
    sh_take.n = 0;
    sh_rhy_sum = sh_beat_sum(t);
}

/* RUDIMENT with a lane key held: the lanes held, as a mask (0: none, and the knobs act as ever) */
static uint32_t sh_lanes_held(void)
{
    uint32_t l, m = 0;
    if (sh_kind() != SHK_RUDIMENT)
        return 0;
    for (l = 0; l < 8u; l++)
        m |= ((fm1_in.notes >> SH_BLACK_KEYS[l]) & 1u) << l;
    return m;
}
/* NUDGE (ALGORITHM, a lane held): the lane's hits and accents s steps later (earlier: s < 0), round the pattern */
static void sh_nudge(track_t *t, uint32_t l, int32_t s)
{
    uint32_t len = sh_lane_len(l), i, hit[NSTEP], acc[NSTEP];
    int8_t oc[NSTEP];
    if (sh_lane_logic(l))                               /* (a logic lane has no hits of its own to move) */
        return;
    for (i = 0; i < len; i++) {
        hit[i] = (step_lanes(&t->step[i]) >> l) & 1u;
        acc[i] = (step_accents(&t->step[i]) >> l) & 1u;
        oc[i] = (int8_t)step_loct(&t->step[i], l);
    }
    fm1_irq_off();
    for (i = 0; i < len; i++) {
        uint32_t j = (uint32_t)(((int32_t)i - s % (int32_t)len + (int32_t)len) % (int32_t)len);
        sh_grid_hit(t, i, l, hit[j]);
        if (hit[j] && rec_lo_part(t))
            step_set_loct(&t->step[i], l, oc[j]);       /* (in its own octave) */
        if (hit[j])
            grid_acc(t, i, l, acc[j]);                  /* (its own accent, not the one the step had) */
        else
            t->step[i].acc &= (uint8_t)~(1u << l);      /* (none left under an empty step for a later hit) */
    }
    fm1_irq_on();
}
/* MUTATE (PRESETS, a lane held): one of the lane's hits moves to an empty neighbour (the density kept) */
static void sh_mutate(track_t *t, uint32_t l)
{
    static int32_t rnd = 0x5EED;
    uint32_t len = sh_lane_len(l), i, n = 0, pick, at[NSTEP], to;
    if (sh_lane_logic(l))
        return;
    for (i = 0; i < len; i++)
        if ((step_lanes(&t->step[i]) >> l) & 1u)
            at[n++] = i;
    if (!n || n >= len)
        return;
    pick = at[((uint32_t)noise32(&rnd) >> 8) % n];
    to = ((uint32_t)noise32(&rnd) >> 31) ? (pick + 1u) % len : (pick + len - 1u) % len;
    if ((step_lanes(&t->step[to]) >> l) & 1u)
        to = (to == (pick + 1u) % len) ? (pick + len - 1u) % len : (pick + 1u) % len;
    if ((step_lanes(&t->step[to]) >> l) & 1u)
        return;                                         /* both neighbours taken: this turn moves nothing */
    fm1_irq_off();
    sh_grid_o = (int8_t)step_loct(&t->step[pick], l);  /* (the hit moves in its octave) */
    sh_grid_hit(t, pick, l, 0u);
    sh_grid_hit(t, to, l, 1u);
    sh_grid_o = 0;
    fm1_irq_on();
}

/* lane l is as its LEN FILL ROT write it (a Euclid lane), not placed by hand: its hits over its LEN are FILL's */
static int sh_lane_euclid(const track_t *t, uint32_t l)
{
    uint32_t n = sh_lane_len(l), i;
    if (sh_lane_logic(l))
        return 0;
    for (i = 0; i < n; i++)
        if (((step_lanes(&t->step[i]) >> l) & 1u) != (uint32_t)sh_euclid_at(l, i))
            return 0;
    return 1;
}
static uint32_t sh_lane_hits(const track_t *t, uint32_t l)   /* lane l's hits over its LEN */
{
    uint32_t n = sh_lane_len(l), i, c = 0;
    for (i = 0; i < n; i++)
        c += (step_lanes(&t->step[i]) >> l) & 1u;
    return c;
}
/* lane l's LEN (w 0), FILL (1) or ROT (2) turned s detents, live, from HOME (the lane in front) or any page (a lane key
 * held). LEN: a Euclid lane spreads its FILL over the new LEN, a hand lane keeps its hits where they are (trimmed, or
 * the steps past a shorter LEN back). ROT turns whatever is in the lane, hand hits and accents too (as NUDGE). FILL
 * writes the lane from FILL: on a hand lane the first detent only says HAND n, a second within SH_HAND_MS writes it
 * (UNDO BEAT). FILL's and ROT's ranges are the lane's LEN */
#define SH_HAND_MS 1500u
static void sh_lane_knob(track_t *t, uint32_t l, uint32_t w, int32_t s)
{
    static uint32_t fill0_t, hand_t;
    static uint8_t hand_l = 0xFFu;
    static char b[24];
    uint32_t n = sh_lane_len(l), eu = sh_lane_euclid(t, l);
    int16_t was = sh_eu[w % 3u][l];
    l %= 8u;
    if (w == 3u) {                                      /* DIV: the lane's step against the part's */
        static char db[32];
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 0);
        sh_ediv[l] = (int16_t)clamp(sh_ediv[l] + (s > 0 ? 1 : -1), SH_DIV_MIN, SH_DIV_MAX);
        str_cpy(db, N_SP_LANE[l], sizeof db);
        str_cpy(db + str_len(db), " DIV ", sizeof db - str_len(db));
        str_cpy(db + str_len(db), SH_DIV_NAME[sh_ediv[l] - SH_DIV_MIN], sizeof db - str_len(db));
        if (sh_lane_logic(l) || (!sh_li && shell_map_on && (l == 0u || l == 1u || l == 3u)))   /* (a logic lane, */
            str_cpy(db + str_len(db), ": AT x1 HERE", sizeof db - str_len(db));         /* MAP's: the part's step) */
        sh_toast(db, 0);
        sh_rhy_sum = sh_beat_sum(t);
        return;
    }
    if (w == 1u && !eu && was >= 0 && sh_lane_hits(t, l) && !(hand_l == l && fm1_ms - hand_t < SH_HAND_MS)) {
        hand_l = (uint8_t)l;                            /* a hand lane: FILL's first detent asks */
        hand_t = fm1_ms | 1u;
        str_cpy(b, "HAND ", 8);
        fmt_int(b + str_len(b), (int32_t)sh_lane_hits(t, l));
        str_cpy(b + str_len(b), ": TURN AGAIN", 14);
        sh_toast(b, 0);
        return;
    }
    if (w == 1u)
        hand_t = fm1_ms | 1u;                           /* (a run of turns goes on writing) */
    sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 0);
    if (w == 0u) {
        sh_eu[0][l] = sh_elen_turn(was, s);
        fm1_irq_off();
        shell_lane_len[l] = (uint8_t)sh_lane_ownc(l);
        fm1_irq_on();
        if (eu && sh_eu[1][l] > 0)
            sh_euclid_lane(t, l);                       /* (its hits spread over the new LEN, heard at once) */
        n = sh_lane_len(l);
        str_cpy(b, N_SP_LANE[l], 8);
        str_cpy(b + str_len(b), " LEN ", 6);
        if (sh_eu[0][l])
            fmt_int(b + str_len(b), (int32_t)n);
        else
            str_cpy(b + str_len(b), "=LEN", 6);
        sh_toast(b, 0);
    } else if (w == 1u) {
        int16_t v = (int16_t)clamp(was + s, -7, (int32_t)sh_lane_span(l));
        if (was > 0 && v <= 0) {                        /* emptying a lane stops at 0: a logic op takes a fresh turn, */
            v = 0;                                      /* after a pause */
            fill0_t = fm1_ms | 1u;
        } else if (was == 0 && v < 0 && fill0_t && fm1_ms - fill0_t < SH_FILL0_MS) {
            v = 0;
            fill0_t = fm1_ms | 1u;
        }
        if (v < 0 && l < 2u)
            v = 0;                                      /* (lanes 1 and 2 have no two lanes above them) */
        sh_eu[1][l] = v;
        sh_euclid_lane(t, l);
        sh_toast_undo("FILL WRITES THE LANE", 0);
    } else {
        sh_eu[2][l] = (int16_t)((was + s % (int32_t)n + 2 * (int32_t)n) % (int32_t)n);
        sh_nudge(t, l, s);                              /* (the lane as it is, turned: ROT is NUDGE) */
        str_cpy(b, N_SP_LANE[l], 8);
        str_cpy(b + str_len(b), " ROT ", 6);
        fmt_int(b + str_len(b), sh_eu[2][l]);
        sh_toast(b, 0);
    }
    sh_rhy_sum = sh_beat_sum(t);
}

/* LANE 2's knobs for lane l (w: PAD LOOP WAKE NAP), each kept for REC's first ring as the lane's other edits are. PAD
 * on a lane FILL wrote spreads its hits over the cycle less PAD (Pam's padding: they group at its start); on a hand
 * lane it only lengthens the cycle. LOOP on keeps the rolls the lane made from this bar's start: they repeat */
static void sh_lane2_knob(track_t *t, uint32_t l, uint32_t w, int32_t s)
{
    static char b[32];
    int eu = sh_lane_euclid(t, l);
    int16_t *v = &sh_lx[w % SH_LX_N][l % 8u], was = *v;
    const param_desc_t *d = &SP_DESC[SP_LPAD + w - SP_FIRST];
    l %= 8u;
    sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 0);
    *v = (int16_t)param_turn(d, *v, s);
    if (w == SH_LX_PAD) {                               /* (the cycle stays within 64 steps, and a span of one at least) */
        int32_t room = sh_lane_own(l) ? NSTEP - (int32_t)sh_lane_own(l) : (int32_t)sh_lanes_len(sh_li) - 1;
        *v = (int16_t)clamp(*v, 0, room);
        fm1_irq_off();
        shell_lane_len[l] = (uint8_t)sh_lane_ownc(l);
        fm1_irq_on();
        if (eu && sh_eu[1][l] > 0)
            sh_euclid_lane(t, l);
    } else if (w == SH_LX_LOOP && *v && !was) {         /* on: this bar's rolls are the loop's */
        shell_lane_seed_i[sh_li][l] = lane_rs_bar[sh_li][l] ? lane_rs_bar[sh_li][l] : lane_rs[sh_li][l] | 1u;
    }
    str_cpy(b, N_SP_LANE[l], sizeof b);
    str_cpy(b + str_len(b), w == SH_LX_PAD ? " PAD " : w == SH_LX_LOOP ? " LOOP " : " ", sizeof b - str_len(b));
    if (w == SH_LX_PAD)
        fmt_int(b + str_len(b), *v);
    else if (w == SH_LX_LOOP)
        str_cpy(b + str_len(b), N_SP_LLOOP[clamp(*v, 0, 4)], sizeof b - str_len(b));
    else if (!sh_lx[SH_LX_NAP][l])
        str_cpy(b + str_len(b), "PLAYS ON: NAP 0", sizeof b - str_len(b));
    else {
        str_cpy(b + str_len(b), "PLAYS ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), sh_lx[SH_LX_WAKE][l]);
        str_cpy(b + str_len(b), ", RESTS ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), sh_lx[SH_LX_NAP][l]);
    }
    sh_toast_undo(b, 0);
    sh_rhy_sum = sh_beat_sum(t);
}
/* OCT+ on LANE 2: the lane in front's LOOP plays new rolls from its next loop */
static void sh_lane2_seed(void)
{
    uint32_t l = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u;
    shell_lane_seed_i[sh_li][l] = rng() | 1u;
    sh_toast(N_SP_LANE[l], sh_lx[SH_LX_LOOP][l] ? ": NEW ROLLS NEXT LOOP" : ": NEW ROLLS (LOOP IS OFF)");
}

static void sh_hero(int32_t s)
{
    track_t *t = sh_trk();
    sh_lgen_use();
    uint32_t id = sh_hero_id();
    int16_t *vp, was;
    const param_desc_t *d = sh_desc(id, 0, &vp);
    was = *vp;
    *vp = (int16_t)param_turn(d, *vp, s);
    if (id < SP_FIRST)
        motion_capture(t, id, t->p[id]);
    if (id == SP_ASEED && *vp != was)                  /* ACID: another line */
        sh_acid_turn(t, was, (uint32_t)*vp);
    else if (id == SP_RHY && *vp != was && sh_li == 1u)   /* (an end of the list turned past loads nothing) */
        sh_lwr_turn(t, was, (uint32_t)*vp);
    else if (id == SP_RHY && *vp != was)
        sh_rhythm_turn(t, was, (uint32_t)*vp);
    sh.pop = 2;
    sh.pop_t = fm1_ms | 1u;
}

/* ------------------------------------------------------------------------------------ the slot picker --- */
/* the wheel open, GLO + a key: the catalogue for that key's slot, SELECT walks it, GLO let go puts it there, OCT-
 * leaves the slot as it was. An entry already on another key of its ring is skipped, an engine the build lacks is
 * never offered. A picked SOUND starts at its first preset (its snapshot dropped),
 * a picked NOTE engine fresh; the shell's settings keep the wheel */
static int sh_pick_ok(uint32_t what, uint32_t c, uint32_t pos)
{
    if (what == 1u) {
        uint32_t at = sh_mode_pos(c);
        return c < SH_NMODES && (SH_MODES[c].lib ? FELUCCA_LIBRARY : eng_ok(SH_MODES[c].engine)) &&
               (at >= SH_SLOTS || at == pos);
    }
    {
        uint32_t at = sh_note_pos(c);
        return c < SH_NNOTES && (at >= 5u || at == pos);
    }
}
static void sh_pick_open(uint32_t what, uint32_t pos)
{
    uint32_t cur = what == 1u ? sh_wslot[pos % SH_SLOTS] : sh_wnoteslot[pos % 5u], n = what == 1u ? SH_NMODES : SH_NNOTES;
    if (what == 1u)
        sh.wsel = (uint8_t)pos;
    else
        sh.wnote = (uint8_t)pos;
    if (cur >= n)                                       /* an empty slot starts at the first entry free */
        for (cur = 0; cur < n && !sh_pick_ok(what, cur, pos); cur++)
            ;
    sh.pick = (uint8_t)what;
    sh.pickpos = (uint8_t)pos;
    sh.pickcur = (uint8_t)(cur < n ? cur : 0u);
}
static void sh_pick_turn(int32_t s)
{
    uint32_t n = sh.pick == 1u ? SH_NMODES : SH_NNOTES, k, c = sh.pickcur;
    int32_t d = s > 0 ? 1 : -1, steps = s > 0 ? s : -s;
    while (steps--)
        for (k = 0; k < n; k++) {
            c = (uint32_t)(((int32_t)c + d + (int32_t)n) % (int32_t)n);
            if (sh_pick_ok(sh.pick, c, sh.pickpos))
                break;
        }
    sh.pickcur = (uint8_t)c;
}
static void sh_lanes_set(uint32_t li, const uint8_t *l);
/* NOTE c, new to the wheel: it starts fresh, and a lanes engine with its lanes as a new project's (=LEN, no hits,
 * no NOTE or OCT) and RHY EMPTY */
static void sh_note_fresh(uint32_t c)
{
    static const uint8_t def[SH_LANES_N] = {[32] = 2, [41] = 12};
    sh_nrec[c].valid = 0;
    sh.lastarp[c] = sh.lastnv[c] = 0;
    if (SH_NOTES[c].kind == SHK_RUDIMENT) {
        uint32_t li = c == SH_N_LINES, i;
        sh_lanes_set(li, def);
        for (i = 0; i < 2u; i++)                        /* (RHY and LANE) */
            if (li == sh_li)
                sh_sp[SH_LSP_IDS[i] - SP_FIRST] = 0;
            else
                sh_lsp[li][i] = 0;
    }
}

static void sh_pick_put(void)
{
    uint32_t pos = sh.pickpos, c = sh.pickcur, old;
    if (!sh_pick_ok(sh.pick, c, pos)) {
        sh.pick = 0;
        return;
    }
    if (sh.pick == 1u) {
        old = sh_wslot[pos];
        sh.pick = 0;
        if (old == c)
            return;
        sh_wslot[pos] = (uint8_t)c;
        sh_snap_of(c)->valid = 0;                       /* it starts at its first preset */
        sh.lastedit[c] = sh.lastsnd[c] = 0;
        sh.page[c] = SHP_HOME;
        if (old == sh.mode)                             /* the slot in front: the new SOUND plays at once */
            sh_switch_pair(c, sh.note);
        sh_toast(SH_MODES[c].name, " ON THIS KEY");
    } else {
        old = sh_wnoteslot[pos];
        sh.pick = 0;
        if (old == c)
            return;
        sh_wnoteslot[pos] = (uint8_t)c;
        sh_note_fresh(c);
        if (old == sh.note)
            sh_switch_pair(sh.mode, c);
        sh_toast(SH_NOTES[c].name, " ON THIS KEY");
    }
}

/* the whole wheel at once, from the editor or the installer (editor.c ED_WHEEL): w[0..7] each white key's SOUND
 * (catalogue index, SH_WHEEL_NONE empty), w[8..12] each black key's NOTE engine. The white keys are the build's eight
 * SOUNDs and stay (w[0..7] is read and ignored, so an old installer's layout still sets the black keys); the black
 * keys follow the picker's rules: each once. A NOTE engine new to the wheel starts as a picked one does; one moved to
 * another key keeps its state. The pair in front stays unless its NOTE engine left the wheel: then what took its key
 * plays. The shell's settings keep the black keys */
#define SH_WHEEL_N (SH_SLOTS + 5u)
#define SH_WHEEL_NONE 127u
static void sh_wheel_get(uint8_t *w)
{
    uint32_t i;
    for (i = 0; i < SH_SLOTS; i++)
        w[i] = (uint8_t)(sh_wslot[i] == SH_EMPTY ? SH_WHEEL_NONE : sh_wslot[i]);
    for (i = 0; i < 5u; i++)
        w[SH_SLOTS + i] = sh_wnoteslot[i];
}
static int sh_wheel_avail(uint32_t m) { return m < SH_NMODES && (SH_MODES[m].lib ? FELUCCA_LIBRARY : eng_ok(SH_MODES[m].engine)); }
static uint32_t sh_wheel_set(const uint8_t *w)          /* 0 set, 1 refused (the wheel as it was) */
{
    uint32_t i, j, m = sh.mode, n = sh.note, nfront = sh_note_pos(sh.note);
    for (i = SH_SLOTS; i < SH_WHEEL_N; i++) {
        if (w[i] >= SH_NNOTES)
            return 1;
        for (j = SH_SLOTS; j < i; j++)
            if (w[j] == w[i])
                return 1;
    }
    sh.pick = 0;
    for (i = 0; i < 5u; i++)
        if (sh_note_pos(w[SH_SLOTS + i]) >= 5u)
            sh_note_fresh(w[SH_SLOTS + i]);
    for (i = 0; i < 5u; i++)
        sh_wnoteslot[i] = w[SH_SLOTS + i];
    if (nfront < 5u && sh_note_pos(n) >= 5u)
        n = sh_wnoteslot[nfront];
    sh_switch_pair(m, n);                               /* (nothing when both stay) */
    return 0;
}
/* the catalogue for the editor: half 0 the SOUNDs (bit 0 the build has it; bit 1, welded, is never set now), 1 the
 * NOTE engines */
static uint32_t sh_wheel_count(uint32_t half) { return half ? SH_NNOTES : SH_NMODES; }
static uint32_t sh_wheel_cat(uint32_t half, uint32_t i, const char **name)
{
    if (half) {
        *name = SH_NOTES[i].name;
        return 1u;
    }
    *name = SH_MODES[i].name;
    return (uint32_t)sh_wheel_avail(i);
}
static void sh_wheel_factory(void)                      /* the default stash, less what the build lacks */
{
    uint8_t w[SH_WHEEL_N] = {0, 1, SH_E4, 3, SH_G4, SH_A4, SH_B4, SH_C5, 0, 1, SH_N_LINES, 3, SH_N_RUDIMENT};
    uint32_t i;
    for (i = 0; i < SH_SLOTS; i++)
        if (w[i] != SH_WHEEL_NONE && !sh_wheel_avail(w[i]))
            w[i] = SH_WHEEL_NONE;
    (void)sh_wheel_set(w);
}

/* the setup page (HOME held, GLO tapped; HOME or OCT- leaves): the pair chosen by knobs, for anyone the wheel's
 * keys confuse. KNOB 1 the SOUND (the whole catalogue, not only the wheel's), 2 the NOTE engine, 3 the SOUND's
 * preset (as PRESETS), 4 the pair's track muted (right) or back (left). Each turn plays at once */
static struct { uint8_t on, m, n; } sh_ppend;           /* PAIR: the pair the knobs picked, put in front on leaving */
static void sh_pair_commit(void)                        /* the pair picked to the front, the page as it is */
{
    uint8_t g = sh.global;
    if (!sh_ppend.on)
        return;
    sh_ppend.on = 0;
    if (sh_ppend.m != sh.mode || sh_ppend.n != sh.note) {
        sh_switch_pair(sh_ppend.m, sh_ppend.n);
        sh.global = g;                                  /* (a switch shows the pair's HOME: the page stays) */
    }
}
static void sh_pair_knob(uint32_t k, int32_t s)
{
    int32_t d = s > 0 ? 1 : -1, steps = s > 0 ? s : -s;
    uint32_t c, i;
    sh.hot = (uint8_t)k;
    sh.hot_t = fm1_ms | 1u;
    if (!sh_ppend.on && k < 2u) {
        sh_ppend.m = sh.mode;
        sh_ppend.n = sh.note;
    }
    if (k == 0u) {                                      /* SOUND and NOTE picked, put there on leaving (nothing parks */
        for (c = sh_ppend.m; steps--;)                  /* on a track as they scroll past) */
            for (i = 0; i < SH_NMODES; i++) {
                c = (uint32_t)(((int32_t)c + d + (int32_t)SH_NMODES) % (int32_t)SH_NMODES);
                if (sh_wheel_avail(c))
                    break;
            }
        sh_ppend.m = (uint8_t)c;
        sh_ppend.on = 1;
    } else if (k == 1u) {
        sh_ppend.n = (uint8_t)(((int32_t)sh_ppend.n + s % (int32_t)SH_NNOTES + (int32_t)SH_NNOTES) % (int32_t)SH_NNOTES);
        sh_ppend.on = 1;
    } else if (k == 2u) {
        sh_pair_commit();                               /* (the preset is the picked pair's) */
        sh_preset(s);
    } else {
        sh_pair_commit();
        if ((s > 0) != ((sh_tmute >> sh.part) & 1u))
            sh_track_mute(sh.mode, sh.note);            /* (right: muted, left: back) */
    }
}

/* ------------------------------------------------------------------------------------------- gestures --- */
static void sh_touch(uint32_t except);
/* DOUBLE's patterns: every step track's. KEYS' and CHORD's, or a take recorded from ALIVE or a note engine, need
 * notes to copy; a lanes engine's LEN doubles empty too */
static int sh_double_ok(void)
{
    return !sh.wheel && (sh_kind() == SHK_RUDIMENT || sh_pat_notes(sh_trk()));
}
/* the pattern a HALF left: DOUBLE straight after gives back what was there (LEN, the lanes' own) */
static struct { uint8_t on, part, note, len; int16_t own[8], pad[8]; uint32_t sum; } sh_halfk;
static void sh_len_toast(const char *what, uint32_t n)  /* "DOUBLE: 32 STEPS" */
{
    static char b[24];
    str_cpy(b, what, 8);
    str_cpy(b + str_len(b), ": ", 3);
    fmt_int(b + str_len(b), (int32_t)n);
    str_cpy(b + str_len(b), n == 1u ? " STEP" : " STEPS", 7);
    sh_toast(b, 0);
}
/* DOUBLE (1) or HALF (2) about to change the pattern in front: REC held to its first ring undoes it, the knob moves as
 * they were. A lanes engine's as a beat (its lanes' LENs with it); in a take still recording, its undo from before it
 * covers this */
static void sh_len_keep(track_t *t, uint32_t what)
{
    if (sh_kind() == SHK_RUDIMENT) {
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
    } else if (!(sh_take.live && sh_take.rec && sh_take.part == trk_index(t))) {
        fm1_irq_off();
        memcpy(sh_undo, t->step, sizeof sh_undo);
        sh_take.ulen = t->p[P_SLEN];
        sh_take.part = (uint8_t)trk_index(t);
        sh_take.note = sh.note;
        sh_take.have = 1;
        sh_take.rhy = 0;
        fm1_irq_on();
    } else {
        return;
    }
    fm1_irq_off();
    sh_mundo_n = (uint8_t)motion_copy_auto(t, sh_mundo);
    sh_mundo_ok = 1;
    sh_take.dbl = (uint8_t)what;
    fm1_irq_on();
}
/* SEQ held + OCT+: DOUBLE (as an MPC's Double Seq). The pattern in front once more after itself and LEN x2, 64 steps
 * at most, the steps whole with their knob moves (ui_tools.c, SEQ TOOLS' DOUBLE): notes, hits, accents, slides, ties.
 * On the lanes, a lane following LEN copies with the steps; a lane with its own LEN doubles that LEN and copies too, so
 * a polyrhythm keeps its ratio. Straight after a HALF, nothing changed since: what the HALF left out comes back. While
 * it plays and records, the part behind as it was. Not in a first take, whose LEN is still open */
static void sh_double(void)
{
    track_t *t = sh_trk();
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, nl = tl_nl(len), lanes = sh_kind() == SHK_RUDIMENT,
             back, i, l;
    if (!sh_double_ok()) {
        sh_toast("DOUBLE: RECORD A TAKE FIRST", 0);
        return;
    }
    if (sh_take.first && rec_on(t)) {
        sh_toast("DOUBLE: TAP REC TO CLOSE THE TAKE", 0);
        return;
    }
    back = sh_halfk.on && sh_halfk.part == trk_index(t) && sh_halfk.note == sh.note && sh_halfk.sum == sh_beat_sum(t);
    if (back)
        nl = sh_halfk.len;
    else if (len >= NSTEP) {
        sh_toast("64 STEPS: NO ROOM", 0);
        return;
    }
    if (!back && !tl_motion_plan(t, len, nl, nl - len, len)) {
        sh_toast("DOUBLE: TOO MANY KNOB MOVES", 0);
        return;
    }
    sh_len_keep(t, 1u);
    sh_sndu.newer = 0;
    fm1_irq_off();
    if (back) {
        for (l = 0; lanes && l < 8u; l++) {
            sh_eu[0][l] = sh_halfk.own[l];
            sh_lx[SH_LX_PAD][l] = sh_halfk.pad[l];
            shell_lane_len[l] = (uint8_t)sh_lane_ownc(l);
        }
    } else {
        for (i = len; i < nl; i++)
            t->step[i] = t->step[i - len];
        for (l = 0; lanes && l < 8u; l++) {
            uint32_t o = sh_lane_ownc(l);               /* (its cycle, LEN + PAD: doubled whole, PAD into LEN) */
            if (o >= len)
                o = 0;                                  /* (as long as LEN: it follows from now) */
            if (2u * o > NSTEP)                         /* (no room for twice its own LEN: it stays as it is) */
                continue;
            for (i = o ? o : len; i < (o ? 2u * o : nl); i++)   /* (its own bytes go with its steps) */
                shell_lstep[sh_li % 2u][l][i] = shell_lstep[sh_li % 2u][l][i - (o ? o : len)];
            for (i = o; i < 2u * o; i++) {
                sh_grid_hit(t, i, l, (step_lanes(&t->step[i - o]) >> l) & 1u);
                if ((step_lanes(&t->step[i]) >> l) & 1u)
                    step_set_loct(&t->step[i], l, step_loct(&t->step[i - o], l));   /* (the copy in its octave) */
                grid_acc(t, i, l, (step_accents(&t->step[i - o]) >> l) & 1u);
            }
            sh_eu[0][l] = (int16_t)(2u * o);
            sh_lx[SH_LX_PAD][l] = 0;
            shell_lane_len[l] = (uint8_t)(2u * o);
        }
    }
    t->p[P_SLEN] = (int16_t)nl;
    fm1_irq_on();
    if (!back)
        (void)motion_replace_track(t, &tl_mo);
    sh_halfk.on = 0;
    if (lanes)
        sh_rhy_sum = sh_beat_sum(t);
    sh_len_toast("DOUBLE", nl);
}
/* SEQ held + a tap of OCT- (held to the ring: SEQ's reset): HALF. LEN halves, 1 at least, and the lanes' own LENs with
 * it; the steps past the end are kept, so a DOUBLE straight after gives them back */
static void sh_acid_entry_say(void);
static void sh_halve(void)
{
    track_t *t = sh_trk();
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, nl = len / 2u ? len / 2u : 1u,
             lanes = sh_kind() == SHK_RUDIMENT, l;
    if (sh.wheel)
        return;
    if (len < 2u) {
        sh_toast("HALF: 1 STEP, NO SHORTER", 0);
        return;
    }
    if (sh_take.first && rec_on(t)) {
        sh_toast("HALF: TAP REC TO CLOSE THE TAKE", 0);
        return;
    }
    sh_len_keep(t, 2u);
    sh_sndu.newer = 0;
    sh_halfk.len = (uint8_t)len;
    sh_halfk.part = (uint8_t)trk_index(t);
    sh_halfk.note = sh.note;
    fm1_irq_off();
    for (l = 0; l < 8u; l++) {
        uint32_t o = lanes ? sh_lane_ownc(l) : 0u;     /* (its cycle, LEN + PAD: halved whole, PAD into LEN) */
        sh_halfk.own[l] = lanes ? sh_eu[0][l] : 0;
        sh_halfk.pad[l] = lanes ? sh_lx[SH_LX_PAD][l] : 0;
        if (!lanes)
            continue;
        sh_eu[0][l] = (int16_t)(o && o < len ? (o / 2u ? o / 2u : 1u) : 0u);
        if (o && o < len)
            sh_lx[SH_LX_PAD][l] = 0;
        shell_lane_len[l] = (uint8_t)sh_lane_ownc(l);
    }
    t->p[P_SLEN] = (int16_t)nl;
    fm1_irq_on();
    sh_halfk.sum = sh_beat_sum(t);
    sh_halfk.on = 1;
    if (lanes)
        sh_rhy_sum = sh_halfk.sum;
    sh_len_toast("HALF", nl);
}

/* ACID ------------------------------------------------------------------------------------------------------------
 * the 303 on LINE: the part's own steps, one note each with ACCENT, SLIDE and TIE. Stopped, the white keys are its steps
 * (hold one to edit it: sh_stp); stopped and armed, the keys write it as a 303's pitch mode: each key the next step's
 * note (OCT shifts the keyboard), SEQ a rest, SEQ with a key still down a TIE, REC again the end. Playing, a key
 * transposes it from ROOT (SEQ held: the steps again). MAIN: LEN, ROT (the whole line turned round), TRN (degrees of
 * SCALE), OCT; ALGORITHM writes a line (the LINE writer, DENS and SPAN on the LINE page), the first hand edit keeps it */
static uint8_t sh_acid_cur;                             /* pitch mode: the step the next key writes */
static int8_t sh_acid_key;                              /* the keys' transposition while it plays, semitones */
static int sh_acid_entry(void)
{
    return sh_kind() == SHK_ACID && !song.playing && ((song.rec >> sh.part) & 1u) && !sh.wheel && !sh.global;
}
static int16_t sh_acid_useed;                           /* the LINE the kept line came from: REC's ring puts it back */
static void sh_acid_keep(track_t *t)                    /* the line before a write (a run of turns: one), REC's ring */
{
    if (sh_take.have && sh_take.part == trk_index(t) && sh_beat_sum(t) == sh_rhy_sum)
        return;
    sh_len_keep(t, 0u);
    sh_acid_useed = sh_sp[SP_ASEED - SP_FIRST];
}
static void sh_acid_turn(track_t *t, int16_t was, uint32_t seed)   /* ALGORITHM: another line */
{
    static char b[12];
    int16_t now = sh_sp[SP_ASEED - SP_FIRST];
    sh_sp[SP_ASEED - SP_FIRST] = was;                   /* (the line kept is the one drawn from was) */
    sh_acid_keep(t);
    sh_sp[SP_ASEED - SP_FIRST] = now;
    sh_acid_load(t, seed);
    if (seed) {
        str_cpy(b, "LINE ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)seed);
    }
    sh_toast_undo("NEW LINE: ", seed ? b : "EMPTY");
    sh_rhy_sum = sh_beat_sum(t);
}
static void sh_turn(void *base, uint32_t sz, uint32_t n, uint32_t k);
static void sh_acid_rot(track_t *t, int32_t s)          /* ROT: the line's LEN steps turned round s, notes and marks */
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    int32_t d = s % (int32_t)len;
    if (!d)
        return;
    sh_acid_keep(t);
    fm1_irq_off();                                      /* (step i to i + d: turned left by LEN - d, in place) */
    sh_turn(t->step, sizeof t->step[0], len, len - (uint32_t)((d + (int32_t)len) % (int32_t)len));
    fm1_irq_on();
    sh_sp[SP_AROT - SP_FIRST] = (int16_t)((sh_sp[SP_AROT - SP_FIRST] + d + 2 * (int32_t)len) % (int32_t)len);
    sh_rhy_sum = sh_beat_sum(t);
}
static void sh_acid_hear(track_t *t, uint32_t n)        /* stopped: a note entered, heard for a step's GATE */
{
    fm1_irq_off();
    seq_release(t);
    trk_note_on(t, n, 96u);
    t->seq_notes[0] = (uint8_t)n;
    t->seq_n = 1;
    t->seq_off = div_samples((uint32_t)t->p[P_SDIV]) * (uint32_t)t->p[P_SGATE] / 128u;
    fm1_irq_on();
}
static void sh_acid_entry_say(void)
{
    static char b[24];
    str_cpy(b, "PITCH: STEP ", 14);
    fmt_int(b + str_len(b), sh_acid_cur + 1);
    sh_toast(b, 0);
}
static void sh_acid_seq(void)                           /* pitch mode, SEQ: a rest, or (a key still down) a TIE */
{
    track_t *t = sh_trk();
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, tie = (fm1_in.notes & 0x07FFFFFFu) != 0u;
    step_t *st = &t->step[sh_acid_cur % len];
    sh_lines_kept();
    fm1_irq_off();
    step_clear(st);
    st->time = (uint8_t)(tie ? ST_TIE : ST_REST);
    fm1_irq_on();
    sh_acid_cur = (uint8_t)((sh_acid_cur + 1u) % len);
    sh_toast(tie ? "TIE" : "REST", 0);
}
static int sh_acid_keys(uint32_t k)                     /* a key on ACID, nothing held: pitch mode or transposition */
{
    static char b[24];
    track_t *t = sh_trk();
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    int32_t note = clamp(NE_KEY_F3 + (int32_t)k + 12 * song.octave, 0, 127);
    if (sh_kind() != SHK_ACID || sh_held(B_SEQ))
        return 0;
    if (sh_acid_entry()) {                              /* the next step's note, and on */
        step_t *st = &t->step[sh_acid_cur % len];
        sh_lines_kept();
        fm1_irq_off();
        step_clear(st);
        st->note[0] = (uint8_t)note;
        st->time = ST_NOTE;
        st->n = 1;
        fm1_irq_on();
        sh_acid_last = (uint8_t)note;
        sh_acid_hear(t, (uint32_t)note);
        str_cpy(b, "STEP ", 6);
        fmt_int(b + str_len(b), sh_acid_cur + 1);
        str_cpy(b + str_len(b), ": ", 3);
        str_cpy(b + str_len(b), N_NOTE[note % 12], 3);
        fmt_int(b + str_len(b), note / 12 - 1);
        sh_toast(b, 0);
        sh_acid_cur = (uint8_t)((sh_acid_cur + 1u) % len);
        return 1;
    }
    if (song.playing) {                                 /* the line moved from ROOT to the key, a 303's transpose */
        sh_acid_key = (int8_t)clamp(note - (60 + t->p[P_ROOT] % 12), -24, 24);
        str_cpy(b, "TRANSPOSE ", 12);
        if (sh_acid_key > 0)
            str_cpy(b + str_len(b), "+", 2);
        fmt_int(b + str_len(b), sh_acid_key);
        sh_toast(b, 0);
        return 1;
    }
    return 0;
}
static void sh_acid_poll(void)                          /* every frame: pitch mode starts at step 1; to seq.c */
{
    static uint8_t was, play;
    uint32_t e = (uint32_t)sh_acid_entry();
    if (e && !was) {
        sh_acid_cur = 0;
        sh_acid_entry_say();
    }
    was = (uint8_t)e;
    if (!song.playing && play)
        sh_acid_key = 0;                                /* (stopped: the line as it is written) */
    play = song.playing;
    shell_acid_part = (uint8_t)(sh_track_note(SH_LINE) == SH_N_ACID ? SH_LINE + 1u : 0u);
    shell_acid_deg = (int8_t)sh_sp[SP_ATRN - SP_FIRST];
    shell_acid_semi = (int8_t)(12 * sh_sp[SP_AOCT - SP_FIRST] + sh_acid_key);
    shell_acid_flux = (uint8_t)sh_sp[SP_AFLUX - SP_FIRST];
}

/* OCT+ would DOUBLE now, in sh_tap's order: no question or MOMENTARY, GLO not held, not PROJECTS or SPELL, room left,
 * no first take open (the LED's hint) */
static int sh_double_now(void)
{
    const track_t *t = sh_trk();
    return sh_held(B_SEQ) && !sh_held(B_GLO) && sh_double_ok() && !sh.mom && !sh.save && !sh.pq && !sh_splsv.open &&
           sh.global != SHG_PROJECT && !sh_on_spell() && (t->p[P_SLEN] < NSTEP || sh_halfk.on) &&
           !(sh_take.first && rec_on(t));
}

static void sh_tap(uint32_t b)
{
    track_t *t = sh_trk();
    uint32_t i;
    if (sh.wheel) {
        if (b == B_OCTDN && sh.pick) {
            sh.pick = 0;                                /* the pick is dropped; the slot stays as it was */
            sh_toast("SLOT LEFT AS IT WAS", 0);
        } else if (b == B_OCTDN) {
            sh.wheel = 0;
            sh_toast("STAYED IN ", sh_cur()->name);
        } else if (b == B_PLAY && sh.wsel < SH_SLOTS) {
            sh_track_mute(sh_wslot[sh.wsel], sh_note_at(sh.wnote));
        } else if (b == B_REC && sh.wsel < SH_SLOTS && sh_wslot[sh.wsel] != SH_EMPTY && !sh.pick) {
            sh.wheel = 0;                               /* REC on a pair: that pair in front, REC's rings over it */
            sh_switch_pair(sh_wslot[sh.wsel], sh_note_at(sh.wnote));
            song.rec |= (uint8_t)(1u << sh.part);       /* (armed as REC's press arms it; let go early: disarmed) */
            sh_wrec = 1;
        } else if (b == B_PLAY) {                       /* PLAY on the wheel's centre: everything stops */
            if (song.playing || seq_counting())
                transport_req = 2u;
            sh_toast("ALL STOPPED", 0);
        } else if (b == B_GLO && !sh.pick) {            /* GLO tapped in the wheel: the setup page, the pair by knobs */
            sh.wheel = 0;
            sh.global = SHG_PAIR;
        }
        return;
    }
    if (sh_splsv.open) {                                /* SPELL's SAVE: OCT+ yes, anything else no */
        sh_spell_save(b == B_OCTUP);
        return;
    }
    if (sh.save) {                                      /* SAVE's question: OCT+ yes, OCT- no, anything else no */
        sh_save_answer(b == B_OCTUP);
        return;
    }
    if (sh.pq) {                                        /* PROJECTS' question: the same */
        sh_proj_answer(b == B_OCTUP);
        return;
    }
    if (sh_held(B_GLO) && b != B_GLO) {                 /* GLO + a button: what it does */
        sh.help = (uint8_t)(b + 1u);
        return;
    }
    if (sh.mom && b == B_OCTUP) {
        sh.mom_keep = 1;
        sh_toast("KEPT", 0);
        return;
    }
    if ((b == B_REC && !((song.rec >> sh.part) & 1u)) || (b == B_PLAY && sh_held(B_REC))) {
        if (sh_q_waits(sh.part)) {                      /* a switch waiting at the bar would land over the take */
            sh_toast("A PATTERN WAITS: SEQ + PRESETS TO OFF", 0);
            if (b == B_PLAY)
                sh_b[B_REC].combo = 1;
            return;
        }
    }
    switch (b) {
    case B_PLAY:
        sh_lgen_use();                                  /* (a generator in front, played: it stays on LINE) */
        if (sh_held(B_REC) && !song.playing && !seq_counting()) {   /* REC + PLAY: armed, a bar counted in, then the take */
            song.rec |= (uint8_t)(1u << sh.part);
            rec_cin = 1;
            transport_req = 1u;
            sh_toast("COUNT-IN", 0);
            return;
        }
        if (sh_held(B_REC) && song.playing && !seq_counting()) {   /* REC + PLAY while it runs: armed, it goes on */
            song.rec |= (uint8_t)(1u << sh.part);
            sh_b[B_REC].combo = 1;                      /* (REC's hold is this gesture's: no ring, no undo) */
            sh.ring = sh.ring2 = 0;
            sh_toast(sh_rec_mode() ? "RECORDING: " : "RECORDING", sh_rec_mode() ? N_SH_RECM[sh_rec_mode()] : 0);
            return;
        }
        transport_req = song.playing || seq_counting() ? 2u : 1u;   /* (PLAY in the count-in stops it) */
        return;
    case B_REC:
        if (fm1_ms - sh_rec_t < SH_DTAP_MS) {           /* REC twice quickly: the arm as it was, knob moves recorded */
            sh_rec_t = 0;                               /* or not (REC MODE's + KNOBS, flipped) */
            song.rec ^= (uint8_t)(1u << sh.part);
            sh_rec_mode_set(sh_rec_mode() ^ 1u);
            sh_toast(sh_rec_mode() & 1u ? "KNOBS RECORD" : "NOTES ONLY", 0);
            return;
        }
        sh_rec_t = fm1_ms | 1u;
        song.rec ^= (uint8_t)(1u << sh.part);           /* the pair's own part; ping-pong puts it on 1 or 2 */
        if ((song.rec >> sh.part) & 1u)
            sh_toast(sh_rec_mode() ? "REC ARMED: " : "REC ARMED", sh_rec_mode() ? N_SH_RECM[sh_rec_mode()] : 0);
        else
            sh_toast("REC OFF", 0);
        return;
    case B_OCTDN:
    case B_OCTUP:
        if (b == B_OCTDN && sh_reset_family() < 4u && !sh.wheel) {   /* a family held + OCT- held: its reset's ring */
            sh.ring = (uint8_t)(SH_RING_RESET + sh_reset_family());
            sh.ring2 = 0;
            sh.ring_t0 = fm1_ms;
            sh_b[B_OCTDN].hold = sh_b[B_OCTDN].combo = 1;   /* (its let-go is no tap) */
            return;
        }
        if (sh.global == SHG_ABOUT || sh.global == SHG_NIBS) {   /* ABOUT: OCT- and OCT+ down together, and Nibs */
            if (sh_held(b == B_OCTUP ? B_OCTDN : B_OCTUP)) {
                sh.global = SHG_NIBS;
                sh_b[B_OCTDN].hold = sh_b[B_OCTDN].combo = sh_b[B_OCTUP].hold = sh_b[B_OCTUP].combo = 1;
            }
            return;
        }
        if (sh.global == SHG_PAIR && b == B_OCTDN) {     /* the setup page: OCT- leaves, as HOME */
            sh.global = SHG_NONE;
            return;
        }
        if (sh.global == SHG_PROJECT) {                 /* PROJECTS: OCT+ saves to the slot, OCT- loads it (asked) */
            sh_proj_ask(b == B_OCTUP ? 1u : 2u);
            return;
        }
        if (b == B_OCTUP && !sh.global && sh.page[sh.mode] == SHP_LANE2 && sh_page_ok(SHP_LANE2)) {
            sh_lane2_seed();                            /* LANE 2: OCT+ new rolls for LOOP */
            return;
        }
        if (sh_on_spell()) {                            /* SPELL: OCT+ a copy of the cursor's syllable, OCT- deletes it */
            if (!(b == B_OCTUP ? spl_insert() : spl_delete()))
                sh_toast(b == B_OCTUP ? "THE PHRASE IS FULL" : "ONE SYLLABLE STAYS", 0);
            sh_spell_sync(1);
            return;
        }
        if (sh_held(B_SEQ)) {                           /* SEQ held: OCT+ DOUBLE (OCT-: HALF or the reset, above) */
            sh_touch(b);
            if (b == B_OCTUP)
                sh_double();
            return;
        }
        if (sh_held(B_REC) && b == B_OCTUP) {          /* REC held: OCT+ FREEZE */
            if (!sh.rec_sel && !sh.rec_erase && !sh.ring2)   /* (the press armed it; past the first ring, sh_long */
                song.rec ^= (uint8_t)(1u << sh.part);   /* undid that already) */
            sh.rec_sel = 1;                             /* (REC a modifier now: no ring) */
            sh_b[B_REC].combo = 1;
            sh.ring = sh.ring2 = 0;
            sh_freeze();
            return;
        }
        if (sh_hold_move(b == B_OCTUP ? 1 : -1, 0))    /* a KEYS step held: its octave */
            return;
        if (sh_lo_on() && !sh.global && !sh.wheel) {   /* the lanes: the octave in view, a page of eight notes */
            static char ob[16];
            int8_t *o = &sh_lopg[sh_li % 2u];
            *o = (int8_t)clamp(*o + (b == B_OCTUP ? 1 : -1), SH_LO_MIN, SH_LO_MAX);
            str_cpy(ob, "OCTAVE ", sizeof ob);
            if (*o > 0)
                str_cpy(ob + str_len(ob), "+", 2);
            fmt_int(ob + str_len(ob), *o);
            sh_toast(ob, 0);
            sh_lane_notes();                            /* (the names now) */
            return;
        }
        song.octave = (int8_t)clamp(song.octave + (b == B_OCTUP ? 1 : -1), -3, 3);
        return;
    case B_HOME:                                        /* TAB: the other view of the pair, where it was left */
        if (fm1_ms - sh.last_home < 320u && (sh.prev != sh.mode || sh.prevnote != sh.note || sh_bg_any())) {
            sh.last_home = 0;                           /* a double tap: the first tap's flip undone, then the pair
                                                         * before, in the view that was in front */
            uint32_t p, bp = NPART, pass;               /* the background: the pair left last that still plays behind
                                                         * (BEAT, LINE, a PLAY take) and this walk has not visited, so
                                                         * double taps in a row go round them all; none: the pair before */
            if (sh_walk_at != (uint16_t)(sh.mode | sh.note << 8))   /* (a walk ends when the front changes some other way) */
                sh_walk = 0;
            sh_walk |= (uint8_t)(1u << sh.part);
            for (pass = 0; pass < 2u && bp == NPART; pass++) {
                for (p = 0; p < NPART; p++)
                    if (p != sh.part && !((sh_walk >> p) & 1u) && sh_part_state(p) == 1u &&
                        sh_partpair[p] != (uint16_t)(sh.mode | sh.note << 8) &&
                        (bp == NPART || sh_front_t[p] > sh_front_t[bp]))
                        bp = p;
                if (bp == NPART)
                    sh_walk = (uint8_t)(1u << sh.part); /* (all visited: round again) */
            }
            if (bp < NPART)
                sh_walk |= (uint8_t)(1u << bp);
            sh_walk_at = 0xFFFFu;
            sh_lfx_add(panel.btn[B_HOME], LFX_BLINK);
            sh.page[sh.mode] = sh.tab_page;             /* (a shell page left: the pair before opens on its view) */
            sh.global = SHG_NONE;
            sh_keep_view = 1;
            if (bp < NPART)
                sh_switch_pair(sh_partpair[bp] & 0xFFu, sh_partpair[bp] >> 8);
            else
                sh_switch_pair(sh.prev, sh.prevnote);
            sh_keep_view = 0;
            sh_walk_at = (uint16_t)(sh.mode | sh.note << 8);
            return;
        }
        sh.last_home = fm1_ms;
        sh.tab_page = sh.page[sh.mode];
        sh_lfx_add(panel.btn[B_HOME], LFX_DIP);
        if (sh.global) {                                /* a shell page: back to the view it was opened from */
            sh.global = SHG_NONE;
            return;
        }
        sh_view_keep();
        sh.page[sh.mode] = (uint8_t)sh_view_page(!sh_view_note());
        return;
    case B_GLO:                                         /* SYSTEM, SYSTEM 2, PROJECTS, ABOUT, LEGAL, then SYSTEM */
        sh.global = sh.global == SHG_SYSTEM ? SHG_SYSTEM2 : sh.global == SHG_SYSTEM2 ? SHG_PROJECT :
                    sh.global == SHG_PROJECT ? SHG_ABOUT : sh.global == SHG_ABOUT ? SHG_LEGAL : SHG_SYSTEM;
        if (sh.global == SHG_PROJECT)
            sh_proj_read();
        return;
    case B_SCL:                                         /* SCALE; again: HARMONY; again (or from MOVE): back */
        if (sh.global == SHG_SCALE) {
            sh.global = SHG_HARM;
            return;
        }
        if (sh.global == SHG_HARM || sh.global == SHG_MOVE) {
            sh.global = sh.sclback;
            if (sh.global == SHG_PROJECT)
                sh_proj_read();
        } else {
            sh.sclback = sh.global;
            sh.global = SHG_SCALE;
        }
        return;
    case B_SAVE:
        if (sh_on_spell()) {                            /* SPELL: the phrase to a user slot; it starts at PHRASE's */
            int32_t ph = sh_trk()->p[P_E4];
            if (song.playing || transport_req == 1u) {  /* (as SAVE: the write waits for quiet, so it would only be said) */
                sh_toast("STOP TO SAVE", 0);
                return;
            }
            sh_splsv.slot = (uint8_t)(ph >= (int32_t)MTH_NPHRASE ? (uint32_t)ph - MTH_NPHRASE : 0u);
            sh_splsv.open = 1;
            return;
        }
        sh_save_ask();
        return;
    case B_EDIT:
    case B_ENV:
    case B_LFO:
    case B_FX:
    case B_ARP:
    case B_SEQ:
        if (b == B_ARP && fm1_ms - sh_arp_t < SH_DTAP_MS) {   /* ARP twice quickly: HOLD on or off, the page as the */
            sh_arp_t = 0;                                       /* first tap left it */
            sh_arp_hold(t);
            return;
        }
        if (b == B_ARP)
            sh_arp_t = fm1_ms | 1u;
        if (b == B_SEQ && sh_acid_entry()) {            /* ACID's pitch mode: a rest (a key down: a TIE) */
            sh_acid_seq();
            return;
        }
        sh_page_button(b);
        return;
    default:
        sh_toast(sh_cur()->name, " HAS NONE YET");
        (void)t;
        (void)i;
        return;
    }
}

/* the wheel opened with the transport running: what plays behind the pair in front, by name (SOUND and NOTE; too long
 * for a line, the NOTE engines; then how many) */
static void sh_wheel_news(void)
{
    char b[48];
    uint32_t p, n = 0, k, how;
    for (how = 0; how < 3u; how++) {                    /* 0: SOUND NOTE + ..., 1: PLAYING: NOTE, ..., 2: how many */
        b[0] = 0;
        for (p = 0, n = 0; p < NPART && song.playing; p++)
            if (p != sh.part && sh_part_state(p) == 1u) {
                const char *w[4] = {n ? (how ? ", " : " + ") : (how ? "PLAYING: " : ""),
                                    how ? "" : SH_MODES[(sh_partpair[p] & 0xFFu) % SH_NMODES].name, how ? "" : " ",
                                    SH_NOTES[(sh_partpair[p] >> 8) % SH_NNOTES].name};
                for (k = 0; k < 4u; k++)
                    str_cpy(b + str_len(b), w[k], sizeof b - str_len(b));
                n++;
            }
        if (!n)
            return;
        if (how == 2u) {
            str_cpy(b, "0 PAIRS PLAY: THEIR KEYS PULSE", sizeof b);
            b[0] = (char)('0' + n);
        }
        if (text_w(&AF_S, b) + 18 <= 232)
            break;
    }
    sh_toast(b, 0);
}

static void sh_hold(uint32_t b)
{
    track_t *t = sh_trk();
    uint32_t i;
    if (b == B_HOME && !sh.wheel) {
        sh.wheel = 1;
        sh.wsel = (uint8_t)sh_mode_pos(sh.mode);
        sh.wnote = (uint8_t)sh_note_pos(sh.note);
        sh.wheel_t0 = fm1_ms;
        sh_wheel_news();
    } else if (b == B_SAVE && !sh.mom) {
        sh_snap_fill(&sh.mom_s, &sh.mom_x, sh.part);
        sh.mom = 1;
        sh.mom_keep = 0;
    } else if (b == B_REC && !sh_b[b].combo) {          /* REC as ERASE shows no ring */
        sh.ring = (uint8_t)(b + 1u);
        sh.ring2 = 0;
        sh.ring_t0 = fm1_ms;
    } else if (b == B_ARP && !sh_b[b].combo) {          /* ARP held by itself through a long hold: HOLD, as it is let
                                                         * go (sh_release), so a short press or ARP + another control
                                                         * never toggles it */
        sh_toast(t->p[P_AHOLD] ? "KEEP HOLDING: HOLD OFF" : "KEEP HOLDING: HOLD ON", 0);
    }
}

/* REC held through its second ring: clear. RUDIMENT clears the whole beat, every lane (one lane: REC + its lane key);
 * every other NOTE engine clears the part's pattern: what KEYS and CHORD played, and what ALIVE and the note engines
 * recorded (a take silences them, sh_gen_quiet; cleared, they play again). The pattern's knob moves go with it (the
 * whole beat's too), and UNDO brings both back */
static void sh_clear(void)
{
    track_t *t = sh_trk();
    uint32_t i, any = 0;
    switch (sh_kind()) {
    case SHK_RUDIMENT: {
        uint32_t beat = 0;
        for (i = 0; i < NSTEP; i++)
            beat |= step_lanes(&t->step[i]) | t->step[i].n | (t->step[i].flags & SF_SLIDE) | (t->step[i].time == ST_TIE);
        beat |= motion_count(t) > motion_lock_count(t);
        if (!beat) {                                    /* nothing to clear: the undo stays as it was */
            sh_toast("THE BEAT IS EMPTY", 0);
            break;
        }
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);  /* the beat before the clear (UNDO BEAT) */
        fm1_irq_off();                                  /* (the sequencer reads the steps from the ISR) */
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        memset(shell_lstep[sh_li % 2u], 0, sizeof shell_lstep[0]);   /* (the lanes' CHNC and RATCH with them) */
        fm1_irq_on();
        sh_mundo_n = (uint8_t)motion_take_auto(t, sh_mundo);
        sh_mundo_ok = 1;
        sh_rhy_sum = sh_beat_sum(t);
        sh_toast_undo("CLEARED ALL LANES", 0);
        break;
    }
    default:                                            /* KEYS, CHORD, ALIVE, the note engines */
        for (i = 0; i < NSTEP; i++)
            any |= t->step[i].n != 0u;
        any |= motion_count(t) > motion_lock_count(t);
        if (!any) {                                     /* nothing to clear: the undo stays (REC held past UNDO */
            sh_toast("THE PATTERN IS EMPTY", 0);        /* on a cleared pattern must not lose it) */
            break;
        }
        fm1_irq_off();
        memcpy(sh_undo, t->step, sizeof sh_undo);       /* the pattern before the clear (UNDO TAKE) */
        sh_mundo_n = sh_mundo_ok = 0;
        sh_sndu.newer = 0;
        sh_take.ulen = t->p[P_SLEN];
        sh_take.part = sh.part;
        sh_take.note = sh.note;
        sh_take.have = sh_take.live = 1;                /* (live: a note while the knob moves go keeps no undo over */
        sh_take.rhy = sh_take.dbl = 0;                  /* this one; the next frame not recording ends it) */
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        fm1_irq_on();
        sh_mundo_n = (uint8_t)motion_take_auto(t, sh_mundo);
        sh_mundo_ok = 1;
        sh_take.n = 0;
        sh_toast_undo("PATTERN CLEARED", 0);
        break;
    }
}

static void sh_long(uint32_t b)
{
    if (b == B_REC && sh.ring == b + 1u && !sh.ring2) { /* the first ring full: let go to undo the take, or hold on */
        song.rec ^= (uint8_t)(1u << sh.part);           /* the press armed it, and an undo or a clear is not an arm */
        sh.ring2 = 1;
        sh.ring_t0 = fm1_ms;
    }
}
/* the resets: a family's button held, then OCT- held through a ring of SH_LONG_MS. ARP: the NOTE engine in front
 * (the arp; CHORD's MAIN, CHORD PLAY and B3's chord; ALIVE's or a note engine's own four); SEQ: the pattern's SWG,
 * GATE and QUANTIZE (LEN and DIV too on an empty pattern); EDIT: the SOUND as its preset or user slot holds it (a
 * load: UNDO SOUND); SCL: ROOT SCL QNT TRN on every part. Never the steps, the knob moves or the other half. The
 * values it changed wait in ui.c's undo copy (its steps' room: a reset changes no step), so REC's first ring undoes
 * it as it undoes a sound */
static const uint8_t SH_RST_BTN[4] = {B_ARP, B_SEQ, B_EDIT, B_SCL};
static const char *const SH_RST_WORD[4] = {"RESET NOTE ENGINE", "RESET SEQ", "SOUND TO PRESET", "RESET SCALE"};
static const uint8_t SH_RST_ARP[8] = {P_AMODE, P_ARATE, P_AOCT, P_AGATE, P_ASWING, P_APROB, P_AHOLD, P_AORDER};
static const uint8_t SH_RST_CHORD[11] = {P_VOIC, P_QUANT, SP_BASS, SP_STRM, SP_TOP, SP_TUNE, SP_RPT, SP_PROG, SP_PEVERY,
                                         SP_PDIR, SP_PLEN};
#define SH_RST_N 20u
typedef struct {
    uint8_t fam, mode, note, n, id[SH_RST_N];
    int16_t v[SH_RST_N];
    uint32_t add;
    uint8_t qual, latch;
} sh_rst_t;
typedef char sh_rst_fits[sizeof(sh_rst_t) <= sizeof undo.step ? 1 : -1];
static uint32_t sh_reset_family(void)                   /* the family whose button is down (4: none) */
{
    uint32_t f;
    for (f = 0; f < 4u; f++)
        if (sh_held(SH_RST_BTN[f]))
            return f;
    return 4u;
}
static uint32_t sh_rst_ids(uint32_t f, const track_t *t, uint8_t *id)   /* what family f resets here */
{
    uint32_t n = 0, i, kind = sh_kind();
    if (f == 0u) {
        for (i = 0; i < NELEM(SH_RST_ARP); i++)
            id[n++] = SH_RST_ARP[i];
        if (kind == SHK_CHORD)
            for (i = 0; i < NELEM(SH_RST_CHORD); i++)
                id[n++] = SH_RST_CHORD[i];
        if (kind == SHK_ALIVE || (kind == SHK_NE && sh.note >= SH_NE_FIRST))
            for (i = 0; i < 4u; i++)
                id[n++] = SHK_HOME[kind][i];
    } else if (f == 1u) {
        id[n++] = P_SSWING;
        id[n++] = P_SGATE;
        id[n++] = P_SQNT;
        if (!sh_pat_notes(t)) {                         /* (LEN and DIV are a take's frame: only with none) */
            id[n++] = P_SLEN;
            id[n++] = P_SDIV;
        }
    } else if (f == 3u) {
        id[n++] = P_ROOT;
        id[n++] = P_SCALE;
        id[n++] = P_QUANT;
        id[n++] = P_TRANS;
    }
    return n;
}
static void sh_rst_put(const sh_rst_t *r, uint32_t i, int16_t v)   /* (SCL: every part plays in the scale) */
{
    int16_t *vp;
    uint32_t j;
    (void)sh_desc(r->id[i], 0, &vp);
    *vp = v;
    if (r->fam == 3u && r->id[i] < SP_FIRST)
        for (j = 0; j < NTRK; j++)
            trk[j].p[r->id[i]] = v;
}
static void sh_reset(uint32_t f)
{
    track_t *t = sh_trk();
    sh_rst_t r;
    uint32_t i;
    int16_t *vp;
    if (f == 2u) {                                      /* EDIT: the SOUND as its slot holds it */
        uint32_t e = sh_eng();
        if (t->user && sh_user_of(e, t->user - 1u))
            up_load(t->user - 1u);
        else
            sh_apply_preset(t, t->preset);
        sh_sndu.on = sh_sndu.newer = 1;
        sh_sndu.part = sh.part;
        sh_toast("SOUND BACK TO ITS PRESET", 0);
        return;
    }
    memset(&r, 0, sizeof r);
    r.fam = (uint8_t)f;
    r.mode = sh.mode;
    r.note = sh.note;
    r.n = (uint8_t)sh_rst_ids(f, t, r.id);
    for (i = 0; i < r.n; i++) {
        (void)sh_desc(r.id[i], 0, &vp);
        r.v[i] = *vp;
    }
    r.add = shell_chord_add;
    r.qual = shell_chord_qual;
    r.latch = shell_chord_latch;
    motion_snapshot_track(t, &undo.motion_backup);      /* (the copy whole, as a load leaves it) */
    undo.trk = (uint8_t)(sh.part + 1u);
    undo.what = SH_UNDO_RESET;
    undo.keep = 0;
    memcpy((void *)undo.step, &r, sizeof r);
    sh_sndu.on = sh_sndu.newer = 1;
    sh_sndu.part = sh.part;
    fm1_irq_off();
    for (i = 0; i < r.n; i++) {
        const param_desc_t *d = sh_desc(r.id[i], 0, &vp);
        sh_rst_put(&r, i, (int16_t)(f == 0u && r.id[i] == P_QUANT ? 1 : d->def));   /* (CHORD's KEY: the scale's) */
    }
    if (f == 0u && sh_kind() == SHK_CHORD) {            /* B3's chord: STANDARD CHORDS, LATCH off */
        shell_chord_add = 0;
        shell_chord_qual = 0;
        shell_chord_latch = 0;
    }
    fm1_irq_on();
    if (f == 3u) {
        sh_move_home(t, P_ROOT);
        sh_move_home(t, P_SCALE);
    }
    sh_toast(f == 0u ? "NOTE ENGINE RESET" : f == 1u ? "SEQ RESET" : "SCALE RESET", 0);
}
static void sh_reset_swap(void)                         /* REC's first ring: the reset undone (again: redone) */
{
    sh_rst_t r;
    uint32_t i, a;
    int16_t *vp, v;
    memcpy(&r, (const void *)undo.step, sizeof r);
    if (r.mode != sh.mode || r.note != sh.note || r.n > SH_RST_N) {
        sh_toast("NO RESET TO UNDO HERE", 0);
        return;
    }
    fm1_irq_off();
    for (i = 0; i < r.n; i++) {
        (void)sh_desc(r.id[i], 0, &vp);
        v = *vp;
        sh_rst_put(&r, i, r.v[i]);
        r.v[i] = v;
    }
    if (r.fam == 0u && sh_kind() == SHK_CHORD) {
        a = shell_chord_add;
        shell_chord_add = r.add;
        r.add = a;
        a = shell_chord_qual;
        shell_chord_qual = r.qual;
        r.qual = (uint8_t)a;
        a = shell_chord_latch;
        shell_chord_latch = r.latch;
        r.latch = (uint8_t)a;
    }
    fm1_irq_on();
    memcpy((void *)undo.step, &r, sizeof r);
    if (r.fam == 3u) {
        sh_move_home(sh_trk(), P_ROOT);
        sh_move_home(sh_trk(), P_SCALE);
    }
    sh_toast("RESET UNDONE", 0);
}

static void sh_ring_poll(void)                          /* every frame: REC's second ring full, the clear */
{
    if (sh.ring >= SH_RING_RESET && sh.ring < SH_RING_RESET + 4u) {   /* a reset's ring: let go early, nothing */
        uint32_t f = sh.ring - SH_RING_RESET;
        if (!sh_held(SH_RST_BTN[f]) || !sh_held(B_OCTDN)) {
            sh.ring = 0;
            if (SH_RST_BTN[f] == B_SEQ && sh_held(B_SEQ) && fm1_ms - sh.ring_t0 < SH_TAP_MS)
                sh_halve();                             /* SEQ + a tap of OCT-: HALF (held to the ring: the reset) */
            else
                sh_toast("LET GO EARLY: NOTHING RESET", 0);
        } else if (fm1_ms - sh.ring_t0 >= SH_LONG_MS) {
            sh.ring = 0;
            sh_reset(f);
        }
        return;
    }
    if (sh.ring == B_REC + 1u && sh.ring2 && fm1_ms - sh.ring_t0 >= SH_LONG_MS - SH_TAP_MS) {
        sh_clear();
        sh.ring = sh.ring2 = 0;
    }
}

static void sh_release(uint32_t b)
{
    track_t *t = sh_trk();
    uint32_t i;
    if (b == B_SEQ)
        sh_q_commit();                                  /* (SEQ held, PRESETS turned: the NEXT picked) */
    if (b == B_HOME && sh.wheel) {
        if (sh.pick) {                                  /* let go mid-pick: nothing is picked, and no switch */
            sh.pick = 0;
            sh_toast("SLOT LEFT AS IT WAS", 0);
        } else if ((sh.wsel >= SH_SLOTS || sh_wslot[sh.wsel] == SH_EMPTY) && sh_note_at(sh.wnote) != sh.note)
                                                        /* a black key alone, the cursor on no SOUND: */
            sh_switch_pair(sh.mode, sh_note_at(sh.wnote));   /* that NOTE engine under the SOUND in front */
        else if (sh.wsel >= SH_SLOTS)
            sh.global = SHG_SYSTEM;
        else if (sh_wslot[sh.wsel] == SH_EMPTY)
            sh_toast("EMPTY: GLO + ITS KEY FILLS IT", 0);
        else
            sh_switch_pair(sh_wslot[sh.wsel], sh_note_at(sh.wnote));
        sh.wheel = 0;
    }
    if (b == B_SAVE && sh.mom) {
        if (!sh.mom_keep) {                             /* back as it was: the patch first, then the rest in one step */
            const sh_snap_t *o = &sh.mom_s;
            if (o->eng == ENGI_FM6) {
                fm6_set_patch(sh.part, sh.mom_x.fm6);
                fm6_slot[sh.part] = sh.mom_x.fm6_slot;
            }
            fm1_irq_off();
            memcpy(drum_lane_d[sh.part % NTRK], sh.mom_x.drl, sizeof drum_lane_d[0]);
            memcpy(ins_ext[sh.part % NTRK], o->iext, sizeof ins_ext[0]);
            t->eng_req = o->eng;
            for (i = 0; i < P_COUNT; i++)               /* (a knob move playing keeps its value, its base back: o */
                if (!((motion_active[sh.part % NTRK][i / 32u] >> (i % 32u)) & 1u))   /* has the base) */
                    t->p[i] = o->p[i];
                else
                    motion_base[sh.part % NTRK][i] = o->p[i];
            t->preset = o->preset;
            t->user = o->user;
            fm1_irq_on();
            sh_toast("BACK", 0);
        }
        sh.mom = 0;
    }
    if (b == B_ARP && !sh_b[b].combo && fm1_ms - sh_b[b].t0 < SH_LONG_MS) {
        sh_toast("HOLD ARP LONGER FOR HOLD", 0);        /* (no latch from a short press) */
    } else if (b == B_ARP && !sh_b[b].combo) {          /* ARP held by itself, long, and let go: HOLD (the arp keeps
                                                         * the notes let go; with no arp mode, the keys latch) */
        sh_arp_hold(t);
    }
    if (sh.ring == b + 1u) {
        sh.ring = 0;
        if (b == B_REC && sh.ring2 == 1u)               /* let go in the second ring: the take undone */
            sh_take_undo();
        else if (b == B_REC && sh_wrec) {               /* REC from the wheel, let go early: only the pair in front */
            song.rec &= (uint8_t)~(1u << sh.part);
            sh_toast("HOLD REC: UNDO, THEN CLEAR", 0);
        } else
            sh_toast(b == B_REC && (song.rec >> sh.part) & 1u ? "REC ARMED, NOTHING UNDONE" : "LET GO EARLY: NOTHING UNDONE", 0);
        sh.ring2 = 0;
    } else if (b == B_REC && sh_wrec && !sh.ring2 && !sh_b[b].lng) {
        song.rec &= (uint8_t)~(1u << sh.part);          /* (REC from the wheel tapped, or made a modifier: no arm) */
        if (!sh_b[b].hold)
            sh_toast("HOLD REC: UNDO, THEN CLEAR", 0);
    }
    if (b == B_REC)
        sh_wrec = 0;
    if (b == B_GLO && spl_tn && sh_on_spell()) {        /* the letters typed: spoken into the phrase */
        char w[8];
        uint32_t n = spl_speak();
        fmt_int(w, (int32_t)n);
        sh_toast(w, n == 1u ? " WORD" : " WORDS");
        sh_spell_sync(0);
    }
    if (b == B_FX && !perf_latch_on)                    /* the FX layer let go: its macros snap back (FX LATCH: they stay) */
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    if (b == B_GLO) {
        sh.help = 0;
        if (sh.wheel && sh.pick)                        /* GLO let go: the pick goes on its key */
            sh_pick_put();
    }
}

static struct { uint8_t was, leave; uint32_t idle, t, leave_t, seed; } sh_pk;   /* the peek (sh_peek_poll): up last frame,
                                                         * its time then; sqrl leaving and since when */
static void sh_peek_poll(void);
/* another control moved: the held buttons become modifiers (hold now, no tap, no long) */
static void sh_touch(uint32_t except)
{
    uint32_t b;
    sh.idle_t = fm1_ms;
    if (except != NB && except != B_SCL && sh_b[B_SCL].down)   /* (a button with SCL: no FILL; a knob, NB: still) */
        sh_fill_no = 1;
    for (b = 0; b < NB; b++)
        if (sh_b[b].down && b != except && !sh_b[b].combo) {
            sh_b[b].combo = 1;
            if (b == B_REC)                             /* (REC held as a modifier: no tap for its double tap) */
                sh_rec_t = 0;
            if (!sh_b[b].hold) {
                sh_b[b].hold = 1;
                sh_hold(b);
            }
        }
}

static void sh_buttons(uint32_t pressed)
{
    uint32_t b;
    for (b = 0; b < NB; b++) {
        uint32_t m = 1u << panel.btn[b];
        if (pressed & m) {
            sh_touch(b);
            sh_b[b].down = 1;
            sh_b[b].hold = sh_b[b].lng = sh_b[b].combo = 0;
            sh_b[b].t0 = fm1_ms;
            if (b == B_SCL)
                sh_fill_no = 0;
            if (b == B_OCTDN && sh_held(B_GLO) && sh_on_spell()) {   /* SPELL's letters: OCT- a backspace */
                spl_backspace();
                sh_b[b].hold = sh_b[b].combo = 1;
            } else if (b != B_GLO && sh_held(B_GLO) && !sh.wheel) {   /* GLO + a button: its help card, now; holding */
                sh_tap(b);                                      /* it on to read never reaches its hold or long (REC's clear) */
                sh_b[b].hold = sh_b[b].combo = 1;
            } else if (sh_on_press(b))
                sh_tap(b);
        }
        if (sh_b[b].down && !sh_b[b].hold && fm1_ms - sh_b[b].t0 >= SH_TAP_MS) {
            sh_b[b].hold = 1;
            sh_hold(b);
        }
        if (sh_b[b].down && !sh_b[b].lng && !sh_b[b].combo && fm1_ms - sh_b[b].t0 >= SH_LONG_MS) {
            sh_b[b].lng = 1;
            sh_long(b);
        }
        if (sh_b[b].down && !(fm1_in.buttons & m) && !(pressed & m)) {   /* let go: judged from the held state, every frame */
            sh_b[b].down = 0;
            if (!sh_b[b].hold && !sh_b[b].combo && !(b == B_REC && sh_wrec)) {
                if (!sh_on_press(b))
                    sh_tap(b);
            } else {
                sh_release(b);
            }
        }
    }
}

static void sh_encoders(void)
{
    static const uint8_t KR[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    int32_t s;
    uint32_t k, held, l;
    if (sh_pk.was) {                                    /* the peek up: a knob's or an encoder's first detent only wakes
                                                         * (its card was not on the screen); the next ones act */
        uint32_t any = 0;
        for (k = 0; k < 4u; k++)
            any |= panel_enc(KR[k]) != 0;
        any |= (panel_enc(EN_SELECT) | panel_enc(EN_PRESET) | panel_enc(EN_ALGO)) != 0;
        if (any) {
            sh_touch(NB);                               /* (the next frame's sh_peek_poll: sqrl leaves) */
            sh_click();
        }
        return;
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(KR[k])) != 0) {
            sh_touch(NB);
            sh_click();
            if (sh.wheel)
                continue;
            if (sh_fx_on()) {                           /* the FX layer's macros, as the stock layer's (not recorded) */
                if (k == 3u && !perf_harm_on())
                    s = -s;                             /* (DEPTH, 100 - perf_k[3], rises to the right) */
                perf_k[k] = (int8_t)clamp(perf_k[k] + accel(KR[k], s, k ? 100 : 200), k ? 0 : -100, 100);
                continue;
            }
            sh_knob(k, sh_held(B_GLO) ? s : s * (s > 1 || s < -1 ? 2 : 1));
        }
    if (sh_fx_on()) {                                   /* the FX layer: the big encoders change nothing unseen */
        if (panel_enc(EN_SELECT) | panel_enc(EN_PRESET) | panel_enc(EN_ALGO))
            sh_touch(NB);
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {
        sh_touch(NB);
        sh_click();
        if (sh.wheel && sh.pick)
            sh_pick_turn(s);
        else if (sh.wheel)
            sh.wsel = (uint8_t)(((int32_t)sh.wsel + s + (int32_t)(SH_SLOTS + 1u) * 8) % (int32_t)(SH_SLOTS + 1u));
        else if (sh_hold_move(0, s))                    /* a KEYS step held: its semitone */
            ;
        else if (sh_held(B_REC)) {                      /* REC held: REC MODE, what an armed REC records */
            if (!sh.rec_sel && !sh.rec_erase && !sh.ring2)   /* (the press armed it; past the first ring, sh_long */
                song.rec ^= (uint8_t)(1u << sh.part);   /* undid that already) */
            sh.rec_sel = 1;
            sh_rec_mode_set((uint32_t)clamp((int32_t)sh_rec_mode() + (s > 0 ? 1 : -1), 0, 3));
            sh.ring = sh.ring2 = 0;
            sh_toast("REC: ", N_SH_RECM[sh_rec_mode()]);
        } else if (sh_b3.down && sh_kind() == SHK_CHORD) {   /* B3 held: the chord a root plays */
            shell_chord_qual = (uint8_t)(((int32_t)shell_chord_qual + s % (int32_t)CQ_N + (int32_t)CQ_N) % (int32_t)CQ_N);
            sh_b3.used = 1;
            sh.ring = 0;
            sh_toast(shell_chord_qual ? "CHORD " : "CHORD: THE KEY'S OWN", shell_chord_qual ? N_CQUAL[shell_chord_qual] : 0);
        } else if (sh_held(B_GLO)) {
            song.g[G_BPM] = (int16_t)param_turn(&GP[G_BPM], song.g[G_BPM], s);
            sh.hot = 4;
            sh_toast("TEMPO", 0);
        } else if (sh_splsv.open) {                     /* SPELL's SAVE: SELECT picks the slot */
            sh_splsv.slot = (uint8_t)clamp((int32_t)sh_splsv.slot + s, 0, (int32_t)MOUTH_NUSER - 1);
        } else if (sh_on_spell()) {                     /* SPELL: SELECT moves the cursor along the phrase */
            spl.cur = (uint8_t)clamp((int32_t)spl.cur + s, 0, (int32_t)spl.n - 1);
            sh_spell_sync(1);
        } else if (!sh.global) {
            int note = sh_view_note();
            do                                          /* skip the pages this part lacks, and the other view's */
                sh.page[sh.mode] = (uint8_t)(((int32_t)sh.page[sh.mode] + (s > 0 ? 1 : -1) + SHP_N) % SHP_N);
            while (!sh_page_ok(sh.page[sh.mode]) || sh_page_note(sh.page[sh.mode]) != note);
        } else if (sh.global == SHG_PAIR) {
            sh_pair_knob(0, s);                         /* PAIR: SELECT turns the SOUND, as KNOB 1 */
        } else {
            sh_global_step(s);
        }
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && !sh.wheel && (sh.global == SHG_ABOUT || sh.global == SHG_LEGAL || sh.global == SHG_NIBS ||
                                                       sh.global == SHG_PROJECT)) {
        sh_touch(NB);                                   /* (ABOUT and Nibs show no sound: nothing to change unseen) */
        if (sh.global == SHG_PROJECT && !sh.pq)         /* PROJECTS: PRESETS walks the slots, as the first knob */
            sh.pslot = (uint8_t)((sh.pslot + (s > 0 ? 1u : 4u)) % 5u);
    } else if (s != 0) {
        sh_touch(NB);
        sh_click();
        if (sh.global == SHG_PAIR)
            sh_pair_commit();                           /* (PAIR: the preset is the picked pair's) */
        if (sh.wheel && sh.pick)
            sh_pick_turn(s);
        else if (sh.wheel)
            sh.wsel = (uint8_t)(((int32_t)sh.wsel + s + (int32_t)(SH_SLOTS + 1u) * 8) % (int32_t)(SH_SLOTS + 1u));
        else if ((held = sh_lanes_held()) != 0) {        /* RUDIMENT, a lane held: MUTATE */
            for (l = 0; l < 8u; l++)
                if ((held >> l) & 1u) {
                    int32_t k;
                    for (k = 0; k < (s < 0 ? -s : s); k++)
                        sh_mutate(sh_trk(), l);
                }
            sh_toast(sh_lane_logic(__builtin_ctz(held)) ? "LOGIC LANE: NOTHING TO MOVE" : "MUTATE ",
                     sh_lane_logic(__builtin_ctz(held)) ? 0 : N_SP_LANE[__builtin_ctz(held)]);
        } else if (sh_held(B_SEQ) && !sh.global) {
            sh_q_turn(s);                               /* SEQ held: the part's NEXT pattern (a lane held: MUTATE) */
        } else if (sh_held(B_GLO) && SH_LIB(sh.mode)) {
            sh_preset_jump(s);                          /* GLO held, LIBRARY: engine to engine, each one's first sound */
        } else
            sh_preset(s);
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !sh.wheel && (sh.global == SHG_ABOUT || sh.global == SHG_LEGAL || sh.global == SHG_NIBS ||
                                                     sh.global == SHG_PROJECT)) {   /* (no sound shown: none changed) */
        sh_touch(NB);
    } else if (s != 0) {
        sh_touch(NB);
        sh_click();
        if (sh.wheel)
            ;
        else if ((held = sh_lanes_held()) != 0) {       /* the lanes, a lane held: NUDGE, which is its ROT */
            for (l = 0; l < 8u; l++)
                if ((held >> l) & 1u)
                    sh_lane_knob(sh_trk(), l, 2u, s);
            if (sh_lane_logic(__builtin_ctz(held)))
                sh_toast("LOGIC LANE: NOTHING TO MOVE", 0);
        } else
            sh_hero(s);
    }
}

/* keys the shell owns: the wheel's slots (HOME held), SHIFT's jumps (GLO held) */
static int32_t sh_key_white(uint32_t k);
static int32_t sh_key_black(uint32_t k);
/* SEQ held, a white key: its step on (the last note played) or off; the patterns of the note modes (ALIVE grows its
 * own). RUDIMENT: the column's mark, plain, SLIDE (into the next: a 303's slide under MONO or LEGATO with GLIDE), TIE
 * (it holds what sounds), round again; on a hit of the lane in front, ACCENT (that hit's, velocity 127) first and
 * ACCENT SLIDE after SLIDE; one undo (UNDO BEAT) */
static void sh_note_step(track_t *t, uint32_t w);
static void sh_step_key(uint32_t k)
{
    track_t *t = sh_trk();
    int32_t w = sh_key_white(k);
    step_t *st;
    if (sh_kind() == SHK_ACID && sh_hold_key(k))       /* ACID, SEQ held: its steps, as with nothing held stopped */
        return;
    if (sh_kind() == SHK_ACID && w >= 0 && sh_hold_view()) {
        sh_hold_down(k, (uint32_t)w);
        return;
    }
    if ((k == SH_PG_LEFT || k == SH_PG_RIGHT) && sh_pg_kind()) {
        sh_pg_step(k == SH_PG_LEFT ? -1 : 1);
        return;
    }
    if (w >= 0 && w < 16)                               /* the step on the page in front */
        w += (int32_t)sh_pg();
    if (w >= 0 && w < NSTEP && sh_kind() == SHK_RUDIMENT && t->eng_req == ENGI_DRUM) {   /* a kit: ACCENT only (a */
        uint32_t l = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u;                       /* TIE would only silence the step) */
        if (sh_lane_logic(l)) {
            sh_toast("LOGIC LANE: ACCENTS COME FROM ABOVE", 0);
            return;
        }
        if (!((step_lanes(&t->step[w]) >> l) & 1u)) {
            sh_toast("ACCENT: ON A HIT OF THE LANE", 0);
            return;
        }
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
        fm1_irq_off();
        grid_acc(t, (uint32_t)w, l, 2u);
        fm1_irq_on();
        sh_toast((step_accents(&t->step[w]) >> l) & 1u ? "ACCENT" : "PLAIN STEP", 0);
        return;
    }
    if (w >= 0 && w < NSTEP && sh_kind() == SHK_RUDIMENT) {
        uint32_t l = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, b = 1u << l, hit, acc;
        st = &t->step[w];
        hit = step_lanes(st) & b;
        acc = step_accents(st) & b;
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
        fm1_irq_off();
        if ((st->flags & SF_SLIDE) && (acc || !hit)) {  /* (ACCENT) SLIDE > TIE: a tied step starts nothing to accent */
            st->flags &= (uint8_t)~SF_SLIDE;
            if (acc)
                grid_acc(t, (uint32_t)w, l, 0u);
            st->time = ST_TIE;
        } else if (st->flags & SF_SLIDE) {              /* SLIDE > ACCENT SLIDE */
            grid_acc(t, (uint32_t)w, l, 1u);
        } else if (st->time == ST_TIE) {
            st->time = ST_NOTE;
        } else if (acc) {                               /* ACCENT > SLIDE */
            grid_acc(t, (uint32_t)w, l, 0u);
            st->flags |= SF_SLIDE;
        } else if (hit) {
            grid_acc(t, (uint32_t)w, l, 1u);
        } else {
            st->flags |= SF_SLIDE;
        }
        fm1_irq_on();
        acc = step_accents(st) & b;
        sh_toast(st->flags & SF_SLIDE ? (acc ? "ACCENT SLIDE" : "SLIDE") : st->time == ST_TIE ? "TIE" :
                 acc ? "ACCENT" : "PLAIN STEP", 0);
        return;
    }
    if (w >= 0 && (sh_kind() == SHK_ALIVE || sh_kind() == SHK_NE)) {
        sh_toast("NO STEPS HERE: RECORD A TAKE", 0);    /* (a generator's pattern is what REC keeps of it) */
        return;
    }
    if (w < 0 || w >= NSTEP || (sh_kind() != SHK_ENGINE && sh_kind() != SHK_CHORD))
        return;
    (void)st;
    sh_note_step(t, (uint32_t)w);
}
/* KEYS' and CHORD's step w on or off: on, the last note played (C4 before one), under CHORD the last chord whole */
static void sh_note_step(track_t *t, uint32_t w)
{
    step_t *st = &t->step[w % NSTEP];
    if (st->n) {
        step_clear(st);
        return;
    }
    step_just_clear(st);                                /* (an earlier take's JUST would detune it) */
    st->note[0] = sh_kind() == SHK_ACID ? sh_acid_last : last_note ? last_note : 60u;   /* (ACID: its own last) */
    st->time = ST_NOTE;
    if (sh_kind() == SHK_CHORD && chord_last[sh.part].n > 1u) {   /* CHORD: the last chord whole, as it sounded */
        uint32_t i, n = 0, k;
        uint8_t all[STEP_NOTES + 1u];
        for (i = 0; i < chord_last[sh.part].n && i < CHORD_MAX; i++) {
            uint32_t x = chord_last[sh.part].note[i];
            if (n < STEP_NOTES)
                k = n++;
            else {                                      /* full: chord.c's rule gives a note up, as REC does */
                memcpy(all, st->note, STEP_NOTES);
                all[STEP_NOTES] = (uint8_t)x;
                k = chord_drop(all, STEP_NOTES + 1u, chord_last[sh.part].root);
            }
            if (k < STEP_NOTES) {
                st->note[k] = (uint8_t)x;
                step_set_just(st, k, shell_chord_tune ? cp_just16((int32_t)x - chord_last[sh.part].root,
                                                                  chord_last[sh.part].mask) : 0);
            }
        }
        st->n = (uint8_t)n;
        return;
    }
    st->n = 1;                                          /* n last: the ISR plays a step once n is set, so the note must be there first */
}

/* RUDIMENT, REC held as ERASE (as on an MPC): REC's press does not stay an arm, and its ring does not clear */
static int sh_rec_key_erases(void);
static void sh_erase_mark(void)
{
    sh_touch(NB);
    sh.ring = 0;
    if (!sh.rec_erase) {
        sh.rec_erase = 1;
        song.rec ^= (uint8_t)(1u << sh.part);           /* (the press armed it) */
        if (sh_kind() == SHK_RUDIMENT)
            sh_beat_keep(sh_trk(), sh_sp[SP_RHY - SP_FIRST], 1);   /* the beat before the hold's first erase (UNDO BEAT) */
        else {                                          /* KEYS, CHORD: the pattern before it (UNDO TAKE) */
            track_t *t = sh_trk();
            fm1_irq_off();
            memcpy(sh_undo, t->step, sizeof sh_undo);
            sh_mundo_n = (uint8_t)motion_copy_auto(t, sh_mundo);
            sh_mundo_ok = 1;
            sh_sndu.newer = 0;
            sh_take.ulen = t->p[P_SLEN];
            sh_take.part = sh.part;
            sh_take.note = sh.note;
            sh_take.have = sh_take.live = 1;
            sh_take.rhy = sh_take.dbl = 0;
            fm1_irq_on();
        }
        sh_toast_undo("ERASING", 0);                    /* (a step key's erase says which) */
    }
}
/* with a key: a step key erases its step on the page in front, on every lane (KEYS, CHORD: its notes); a lane key
 * erases its lane when stopped (playing: sh_erase_poll) */
static void sh_erase_key(uint32_t k)
{
    track_t *t = sh_trk();
    int32_t w = sh_key_white(k), b = sh_key_black(k);
    uint32_t i;
    sh_erase_mark();
    if (song.playing && (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD))
        return;                                         /* (playing: the key's notes go as the playhead passes them) */
    if (w >= 0 && w < 16) {
        {   /* the step's hits and notes, every lane; its SLIDE or TIE mark stays (a clear takes those) */
            step_t *s = &t->step[sh_pg() + (uint32_t)w];
            uint8_t f = s->flags & SF_SLIDE, tie = s->time == ST_TIE;
            fm1_irq_off();
            step_clear(s);
            s->flags = f;
            if (tie)
                s->time = ST_TIE;
            fm1_irq_on();
        }
        sh_toast_undo("ERASED A STEP", 0);
    } else if (b >= 0 && b < 8 && sh_kind() == SHK_RUDIMENT) {
        sh_sp[SP_LANE - SP_FIRST] = (int16_t)b;
        if (!song.playing) {
            for (i = 0; i < NSTEP; i++)
                sh_grid_hit(t, i, (uint32_t)b, 0u);
            if (sh_lane_logic((uint32_t)b))             /* a logic lane: its op off, so it is silent too */
                sh_eu[1][b] = 0;
            sh_toast_undo("ERASED ", N_SP_LANE[b]);
        }
    }
}
/* playing, every frame: the hits of the held lanes go as the playhead passes them */
static void sh_erase_poll(void)
{
    track_t *t;
    uint32_t l, len;
    if (!sh_held(B_REC)) {
        sh.rec_erase = sh.rec_sel = 0;
        return;
    }
    if (!song.playing || sh.wheel || sh_wrec)           /* (REC from the wheel: the pair's keys may still be down) */
        return;
    t = sh_trk();
    if (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD) {   /* KEYS, CHORD: a held key's notes (its chord under
                                                         * CHORD) leave the step under the playhead, pass by pass */
        uint8_t ch[CHORD_MAX];
        uint32_t k, n, i, j, q, hit = 0;
        step_t *st;
        len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
        st = &t->step[t->seq_idx % len];
        for (k = 0; k < 27u; k++) {
            if (!((fm1_in.notes >> k) & 1u))
                continue;
            n = chord_build(t, kb_map(t, k), ch);
            for (i = 0; i < st->n;) {
                for (q = 0; q < n && ch[q] != st->note[i]; q++)
                    ;
                if (q == n) {
                    i++;
                    continue;
                }
                if (!hit)
                    fm1_irq_off();
                hit = 1;
                for (j = i; j + 1u < st->n; j++) {      /* (the notes after it, and their JUST, move down one) */
                    st->note[j] = st->note[j + 1u];
                    step_set_just(st, j, step_just(st, j + 1u));
                }
                st->n--;
                step_set_just(st, st->n, 0);
            }
        }
        if (hit) {
            if (!st->n)
                step_clear(st);
            fm1_irq_on();
        }
        return;
    }
    if (sh_kind() != SHK_RUDIMENT)
        return;
    for (l = 0; l < 8u; l++)
        if ((fm1_in.notes >> SH_BLACK_KEYS[l]) & 1u && sh_lane_logic(l)) {
            sh_erase_mark();                            /* a logic lane held: its op off, silent from now */
            sh_eu[1][l] = 0;
        } else if ((fm1_in.notes >> SH_BLACK_KEYS[l]) & 1u && (step_lanes(&t->step[sh_lane_now(l)]) >> l) & 1u) {
            sh_erase_mark();
            sh_grid_hit(t, sh_lane_now(l), l, 0u);
        }
}

/* MASK: the SCALE page's keys draw the scale every part plays in (SCALE USER; the keys are silent there). Keys
 * pressed together set it on the last one's release, the lowest the ROOT; a lone tap puts its note in or takes it
 * out, from the scale playing (a named one is copied first). It lands as a turn of SCALE would: every part, MOVE's
 * home */
static struct { uint8_t on, n, low; uint16_t pcs; } sh_mk;
static void sh_mask_key(uint32_t k)
{
    uint32_t n = 53u + k;
    if (!sh_mk.on || n < sh_mk.low)
        sh_mk.low = (uint8_t)n;
    sh_mk.on = 1;
    sh_mk.n++;
    sh_mk.pcs |= (uint16_t)(1u << (n % 12u));
}
static void sh_mask_poll(void)
{
    track_t *t = sh_trk();
    uint32_t root = (uint32_t)t->p[P_ROOT] % 12u, rel, i, cnt;
    char b[24];
    shell_kb_scl = sh.global == SHG_SCALE && !sh.wheel ? 0x07FFFFFFu : 0u;   /* (every key: SH_ALL_KEYS) */
    if (!sh_mk.on || (fm1_in.notes & 0x07FFFFFFu))
        return;
    if (sh_mk.n == 1u) {                                /* a tap: that note in or out of the scale playing */
        rel = scale_mask(t) & 0xFFFu;
        rel ^= 1u << ((__builtin_ctz(sh_mk.pcs) + 12u - root) % 12u);
    } else {                                            /* together: those notes, the lowest the root */
        root = sh_mk.low % 12u;
        rel = ((uint32_t)sh_mk.pcs >> root | (uint32_t)sh_mk.pcs << (12u - root)) & 0xFFFu;
    }
    memset(&sh_mk, 0, sizeof sh_mk);
    if (!rel) {
        sh_toast("A SCALE NEEDS A NOTE", 0);
        return;
    }
    fm1_irq_off();
    scale_user = (uint16_t)rel;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_ROOT] = (int16_t)root;
        trk[i].p[P_SCALE] = (int16_t)SCALE_USER;
    }
    sh_move_home(t, P_ROOT);
    sh_move_home(t, P_SCALE);
    fm1_irq_on();
    cnt = (uint32_t)__builtin_popcount(rel);
    str_cpy(b, N_NOTE[root], 4);
    str_cpy(b + str_len(b), " USER: ", 8);
    fmt_int(b + str_len(b), (int32_t)cnt);
    str_cpy(b + str_len(b), cnt == 1u ? " NOTE" : " NOTES", 7);
    sh_toast(b, 0);
}
static void sh_keys(uint32_t notes)
{
    uint32_t k;
    int32_t s;
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u))
            continue;
        if (shell_kb_scl && !sh_held(B_GLO) && !sh_held(B_HOME) && !sh_held(B_REC) && !sh_held(B_SEQ) && !sh_fx_on()) {
            sh_touch(NB);
            sh_mask_key(k);                             /* SCALE: the key draws the scale */
            continue;
        }
        if (sh_fx_on()) {                               /* FX held: the key is an effect's (perform.c), FX a hold */
            sh_touch(NB);
            continue;
        }
        if (!sh.wheel && sh_held(B_GLO) && sh_on_spell()) {   /* SPELL, GLO held: the key is a letter */
            sh_touch(NB);
            spl_type(k);
            continue;
        }
        if (!sh.wheel && sh_held(B_HOME))               /* HOME and a key at once: the wheel opens first, the key in it */
            sh_touch(NB);
        s = sh_key_slot(k);
        if (sh.wheel && s >= 0)                         /* (the white keys are the build's eight: no picker) */
            sh.wsel = (uint8_t)s;
        else if (sh_held(B_GLO) && !sh.wheel && s >= 0 && sh_wslot[s] != SH_EMPTY)
            sh_switch(sh_wslot[s]);
        s = sh_key_note(k);                             /* a black key: the NOTE half */
        if (sh.wheel && sh_held(B_GLO) && s >= 0)       /* GLO + a black key: that slot's NOTE engine */
            sh_pick_open(2u, (uint32_t)s);
        else if (sh.wheel && s >= 0)
            sh.wnote = (uint8_t)s;
        else if (sh_held(B_GLO) && !sh.wheel && s >= 0 && sh_wnoteslot[s] < SH_NNOTES)
            sh_switch_pair(sh.mode, sh_wnoteslot[s]);
        if (sh_held(B_REC) && sh_rec_key_erases()) {
            sh_erase_key(k);
        } else if (sh_held(B_SEQ) && !sh.wheel) {
            sh_touch(B_SEQ);
            sh_step_key(k);
        } else if (sh_held(B_GLO) || sh_held(B_HOME))
            sh_touch(NB);
        else {
            sh_lgen_use();                              /* (a key played: the generator in front stays) */
            if ((shell_kb_skip >> k) & 1u)
                sh_inst_key(k);
        }
    }
}

static void sh_alive_poll(void);
static void sh_chord_poll(void);
static void shell_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), k;
    midi_take();                                        /* as ui_input does: MIDI IN, programs, FM6 patches, settings */
    for (k = 0; k < NTRK; k++)
        if (ui.mpc[k]) {                                /* a Program Change: a sound load REC held can undo */
            sh_sndu.on = sh_sndu.newer = 1;
            sh_sndu.part = (uint8_t)k;
        }
    midi_pc_poll();
    fm6_poll();
#if !FELUCCA_FM4
    for (k = 0; k < NTRK; k++)
        if (trk[k].eng_req == ENGI_DIGITAL)
            fm4_track(&trk[k]);
#endif
    for (k = 0; k < NTRK; k++)                          /* a sound on an engine the build lacks (an old project, a */
        if (trk[k].eng_req < NENGINES && trk[k].eng_req != ENGI_DIGITAL && !eng_ok(trk[k].eng_req))   /* preset, the */
            sh_to_sibling(k);                                                                       /* editor) */
    sh_map_sync();                                      /* (RUDIMENT's MAP to seq.c, whatever is in front) */
    perf_latch_on = fx_latch & 1u;
    fx_map_sync();
    fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;
    scope_mix = (ui_prefs2 & (PREF_SCOPE_MIX >> 16)) != 0u;
    rp_apply();
    usb_serial_apply();
    settings_later_poll();
    if (scr_input(pressed, notes))                      /* SCREEN OFF: dark, or the wake gesture */
        return;
    if (!sh.ready) {
        ux.mono = 0;                                    /* the shell has its own colours, no GREY / MONO text ramps */
        ux.gen++;
        sh.hot = 4;
        for (k = 0; k < NTRK; k++)                      /* the parts no mode holds are silent */
            trk[k].p[P_MUTE] = 1;
        sh_tmute = 0;
        shell_lanes_part[0] = (uint8_t)(SH_NOTES[SH_N_RUDIMENT].part + 1u);   /* RUDIMENT's part: its lanes at their
                                                                               * own LEN (seq.c); LINES': sh_lane_notes */
        for (k = 0; k < SH_NLI * 8u; k++)
            shell_lane_len_i[k / 8u][k % 8u] = (uint8_t)clamp(sh_eu_i[k / 8u][0][k % 8u] ?
                                                              sh_eu_i[k / 8u][0][k % 8u] + sh_lx_i[k / 8u][SH_LX_PAD][k % 8u] : 0, 0, NSTEP);
        sh_store_load();
        if (!sh_enter_saved())
            sh_enter(0);
        sh.ready = 1;
        qn_chord_src = (uint8_t)((1u << NPART) - 1u) & ~(uint8_t)(1u << SH_LINE | 1u << SH_BEAT);   /* QNT CHORD hears PLAY */
        if (sh_as_boot == 1u || sh_as_boot == 2u) {    /* the autosave over the settings' pair, as a PROJECTS load:
                                                         * its pairs to the front and on the tracks (autosave_boot) */
            sh_as_boot = (uint8_t)(autosave_boot(sh_as_boot == 2u) ? 3u : 0u);
        }
        sh.splash_t = fm1_ms | 1u;
        sh.idle_t = fm1_ms;
    }
    if (pressed || notes || fm1_in.notes)               /* (a key held is not idle: the peek waits for its let-go) */
        sh.idle_t = fm1_ms;
    sh_peek_poll();                                     /* (a touch since: sqrl leaves; the knobs' come next frame) */
    if (sh.save && !sh_fresh(sh.save_t, SH_SAVE_MS))
        sh_save_answer(0);
    if (sh.splash_t && (pressed || notes || !sh_fresh(sh.splash_t, SH_SPLASH_MS))) {   /* a control that skips it
                                                                                         * still does its job */
        sh.splash_t = 0;
        sh_toast(sh_as_boot == 3u ? "LAST SESSION RESTORED" : autosave_held() ? "LAST SESSION KEPT: AUTOSAVE OFF" :
                 "SQRL-1  HOLD HOME FOR THE WHEEL", 0);
        sh_as_boot = 0;
    }
    /* the keys are the shell's while HOME (the wheel), GLO (SHIFT) or SEQ (steps) is held, and perform.c's while FX
     * is: the ISR plays no note with them */
    kb_mask = (1u << panel.btn[B_HOME]) | (1u << panel.btn[B_GLO]) | (1u << panel.btn[B_SEQ]) | (1u << panel.btn[B_FX]) |
              (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD ? 1u << panel.btn[B_REC] : 0u);   /* (REC held: ERASE) */
    sh_roles();
    sh_buttons(pressed);
    perf_mask = sh_fx_ok() ? 1u << panel.btn[B_FX] : 0u;   /* FX held: the effects (the ISR reads FX itself; after the
                                                         * buttons, so a modifier let go this frame no longer blocks) */
    sh_ring_poll();
    sh_take_poll();
    sh_ev_poll();                                       /* (first: a run from the top is MOVE's to clear) */
    sh_frz_poll();
    sh_move_poll();
    sh_encoders();
    sh_pg_poll();
    sh_hold_poll();
    sh_fill_poll();
    sh_q_poll();
    sh_acid_poll();
    sh_harm_poll();
    if (sh.global != SHG_PAIR)                          /* PAIR left: the pair its knobs picked to the front */
        sh_pair_commit();
    sh_keys(notes);
    sh_mask_poll();
    sh_psave_poll();
    sh_alive_poll();
    sh_chord_poll();
    if (sh.ready && sh_restored) {                      /* (before the store below writes the live values over them) */
        sh_restored = 0;
        sh_store_load();
        (void)sh_enter_saved();
        sh_toast("SETTINGS RESTORED", 0);
    }
    if (sh.ready)
        sh_store_poll();
    sh_lane_notes();
    {   /* DRUMS played from the keys (KEYS, MIDI): the DRUM page sets the drum struck last */
        static uint8_t seen = 60;                       /* (last_note at power-on: nothing struck yet) */
        if (sh_kind() != SHK_RUDIMENT && sh_trk()->eng_req == ENGI_DRUM && last_note != seen) {
            seen = last_note;
            sh_kdrum = (uint8_t)drum_lane(last_note);
        }
    }
    sh_erase_poll();
    if (fm1_ms - sh.dsp_t >= 1000u) {                   /* once a second; audio_max_us is the shell's to reset */
        sh.dsp_t = fm1_ms;
        sh.dsp = (uint8_t)clamp((int32_t)(song.cpu_q8 * 100u / 256u), 0, 255);
        sh.dsp_pk = (uint8_t)clamp((int32_t)(audio_max_us * 100u / (HALF_FRAMES * 1000000u / FS)), 0, 255);
        audio_max_us = 0;
    }
    if (!sh_page_ok(sh.page[sh.mode]))                  /* a page this pair lacks (LIZARD left DRAW; a NOTE engine */
        sh.page[sh.mode] = (uint8_t)(SHP_FAM[sh.page[sh.mode]] == B_ARP ?   /* without MAIN or CHORD PLAY): the */
                                     (sh_page_ok(SHP_NOTE) ? SHP_NOTE : SHP_ARP) : SHP_EDIT1);   /* same half's first */
    if (!sh.global)
        sh_view_keep();
    if ((sh.page[sh.mode] == SHP_DRAW1 || sh.page[sh.mode] == SHP_DRAW2) && !lizard_own(sh_trk()->p))
        sh_lz_take(sh_trk(), 0);                        /* the knobs show the drawing WAVE plays */
}

/* ------------------------------------------------------------------------------- the shell's instruments --- */
/* the keys from F3: white key w's key number, black key b's (chord.c SH_BLACK_KEYS) */
static int32_t sh_white_key(uint32_t w)
{
    static const uint8_t W[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    return w < 16u ? W[w] : -1;
}
static int32_t sh_key_white(uint32_t k)
{
    uint32_t w;
    for (w = 0; w < 16u; w++)
        if ((uint32_t)sh_white_key(w) == k)
            return (int32_t)w;
    return -1;
}
static int32_t sh_key_black(uint32_t k)
{
    uint32_t b;
    for (b = 0; b < 11u; b++)
        if (SH_BLACK_KEYS[b] == k)
            return (int32_t)b;
    return -1;
}
static uint32_t sh_black_mask(void)                     /* the black keys' bits (from SH_BLACK_KEYS) */
{
    uint32_t b, m = 0;
    for (b = 0; b < 11u; b++)
        m |= 1u << SH_BLACK_KEYS[b];
    return m;
}
#define SH_ALL_KEYS 0x07FFFFFFu

/* ALIVE: a ring of AL_W cells under an elementary rule. Each step of the audio clock makes a row (shell_block, in
 * the ISR, because a row made from the UI loop would wobble in time). Its centre cell plays: when it is alive, the
 * eight cells around it are read as a number, and that number picks a degree of the part's scale over RANGE octaves;
 * the note lasts one step. It keeps its own list of the notes it holds, so a stop, a switch or going quiet never
 * leaves one hanging. */
#define AL_W 47u
#define AL_ROWS 38u
#define AL_C 23u
static struct {
    uint32_t rows[AL_ROWS][2];          /* a row's cells as bits, cell i at bit i & 31 of word i >> 5 */
    volatile uint8_t head;              /* the newest row */
    volatile uint32_t gen;              /* rows made (the screen's signature) */
    uint16_t idx;                       /* the part's step the last row was made on */
    uint8_t note, playing, seeded, key; /* the note it holds (0 none); last block's transport */
    int32_t rnd;
} al;
static uint32_t al_cell(uint32_t r, uint32_t i) { return al.rows[r][i >> 5] >> (i & 31u) & 1u; }
static void al_put(uint32_t r, uint32_t i, uint32_t v)
{
    al.rows[r][i >> 5] = (al.rows[r][i >> 5] & ~(1u << (i & 31u))) | (v & 1u) << (i & 31u);
}

static void al_seed(void)
{
    int32_t sd = sh_sp[SP_SEED - SP_FIRST];
    uint32_t i, r, d = sd < 0 ? 0u : (uint32_t)sd;
    memset(al.rows, 0, sizeof al.rows);
    al.head = 0;
    if (!al.rnd)
        al.rnd = 0x1234567;
    if (sd < 0) {                                       /* NIBS: his face is the history, the rule grows from his chin */
        for (r = 0; r < AL_ROWS; r++)
            for (i = 0; i < AL_W; i++)
                al_put(r, i, BR_NIBS_CELLS[r][i >> 3] >> (i & 7u));
        al.head = AL_ROWS - 1u;
    } else if (!d)
        al_put(0, AL_C, 1u);
    else
        for (i = 0; i < AL_W; i++)
            al_put(0, i, ((uint32_t)noise32(&al.rnd) >> 25) < d * 64u / 100u);
    al.seeded = 1;
    al.key = (uint8_t)(sh_sp[SP_RULE - SP_FIRST] * 128 + sd);
    al.gen++;
}

/* ALIVE's notes bypass the keys' path: they do not set last_note (that is the player's), and the off of a note it
 * started always lands (input_off would wait for a MIDI IN note of the same number). They record as a generator's
 * (sh_live_on), and give way to a recorded pattern; the part's arp still applies (and records its own) */
static void al_on(track_t *t, uint32_t note)
{
    if (sh_gen_quiet(t))
        return;
    if (t->p[P_AMODE]) {
        sh_gen_mark(t, note);                           /* (al_off ends it as before) */
        arp_add(t, note);
    } else
        sh_live_on(t, note, 100u);
}
static void al_off(track_t *t)                          /* ISR (or IRQs off) */
{
    if (al.note) {
        arp_remove(t, al.note);
        sh_live_off(t, al.note);
        al.note = 0;
    }
}

static void al_step(track_t *t)                         /* ISR: the next row, and its note */
{
    uint32_t pr = al.head, nw = (al.head + 1u) % AL_ROWS;
    uint32_t rule = SP_RULES[clamp(sh_sp[SP_RULE - SP_FIRST], 0, 4)], i, v = 0, mask, deg[12], n = 0, span, d;
    int32_t mut = sh_sp[SP_MUTATE - SP_FIRST];
    for (i = 0; i < AL_W; i++) {
        uint32_t q = al_cell(pr, (i + AL_W - 1u) % AL_W) << 2 | al_cell(pr, i) << 1 | al_cell(pr, (i + 1u) % AL_W);
        al_put(nw, i, (rule >> q) ^ (mut && ((uint32_t)noise32(&al.rnd) >> 22) < (uint32_t)mut));
    }
    al.head = (uint8_t)((al.head + 1u) % AL_ROWS);
    al.gen++;
    al_off(t);
    if (!al_cell(nw, AL_C))
        return;
    for (i = 0; i < 8u; i++)
        v = v << 1 | al_cell(nw, AL_C - 4u + i);
    mask = scale_mask(t);
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            deg[n++] = i;
    if (!n)
        return;
    span = n * (uint32_t)clamp(sh_sp[SP_RANGE - SP_FIRST], 1, 4);
    d = v % span;
    al.note = (uint8_t)clamp(48 + t->p[P_ROOT] + 12 * (int32_t)(d / n) + (int32_t)deg[d % n] + 12 * song.octave, 1, 127);
#if FELUCCA_SYLLABLE
    if (t->eng_req == ENGI_SYLLABLE)                    /* SYLLABLE: the row's cells are the syllable (babble) */
        syllable_next(t, 1u + (v * 7u) % (SYL_NCONS - 1u), (v >> 2) % 12u);
#endif
    al_on(t, al.note);
}

static int sh_running(uint32_t kind);
/* the audio ISR, once a block after the keys (seq.c events_block). ALIVE's clock is its part's sequencer: RATE sets
 * that part's DIV and a row is made as its seq_idx moves, so it swings, follows the tempo and an external clock, and
 * stays in phase with RUDIMENT's steps */
static void shell_block(uint32_t n)
{
    track_t *t;
    (void)n;
    shell_ne_block();                                   /* a note_engine.h engine in front */
    t = &trk[SH_NOTES[2].part];                         /* ALIVE's own part */
    if (!sh_running(SHK_ALIVE) || (!song.playing && !(kb_ne_down && shell_al_front && !shell_kb_scl))) {
        al_off(t);
        al.playing = 0;
        return;
    }
    t->p[P_SDIV] = (int16_t)clamp(sh_sp[SP_RATE - SP_FIRST], 0, 3);
    (void)sh_gen_give_way(t);
    if (!al.seeded || al.key != (uint8_t)(sh_sp[SP_RULE - SP_FIRST] * 128 + sh_sp[SP_SEED - SP_FIRST]))
        al_seed();
    if (!song.playing) {                                /* stopped, a key held in front: it grows as if playing, at
                                                         * RATE, until the last key is up (playing 2) */
        static uint32_t down;
        uint32_t per = div_samples((uint32_t)t->p[P_SDIV]);
        if (al.playing != 2u) {
            al.playing = 2;
            down = 0;
            al_step(t);
        } else if ((down += HALF_FRAMES) >= per) {
            down -= per;
            down = down < per ? down : 0u;
            al_step(t);
        }
    } else if (al.playing != 1u || t->seq_idx != al.idx) {   /* the transport started, or the part's next step */
        al.playing = 1;
        al.idx = t->seq_idx;
        al_step(t);
    }
}

static void sh_alive_new(void)                          /* NEW: ALIVE's rows from its seed again */
{
    fm1_irq_off();
    al_seed();
    fm1_irq_on();
}
/* stopped: a new seed on screen at once (NIBS: his face); playing, the ISR reseeds on its next step */
static void sh_alive_poll(void)
{
    if (sh_running(SHK_ALIVE) && !song.playing &&
        al.key != (uint8_t)(sh_sp[SP_RULE - SP_FIRST] * 128 + sh_sp[SP_SEED - SP_FIRST])) {
        fm1_irq_off();
        al_seed();
        fm1_irq_on();
    }
}

/* the mode of kind k in front, or on a track left playing */
static int sh_running(uint32_t kind)
{
    uint32_t p;
    if (sh_kind() == kind && sh.ready && !(sh_is_track(sh.part) && ((sh_tmute >> sh.part) & 1u)))
        return 1;
    for (p = SH_LINE; p <= SH_BEAT; p++)               /* a track left, still playing */
        if (p != sh.part && sh_track_live(p) && SH_NOTES[sh_track_note(p)].kind == kind)
            return 1;
    return 0;
}
/* LINE's note_engine.h engine (TONNETZ, ENIGMA, S+H, CANON) on host channel 1 while the track holds it and is not muted, in front
 * or left */
static void sh_line_ne(void)
{
    uint32_t n = sh_track_note(SH_LINE), i;
    if (sh_track_live(SH_LINE) && (i = sh_ne_i(n)) < SH_NNE && sh_ne_ch(i) == 1u) {
        if (shell_ne_on[1] != i + 1u || shell_ne_part[1] != SH_LINE)
            sh_ne_start(1u, i, SH_LINE);
    } else if (shell_ne_on[1])
        sh_ne_stop(1u);
}

/* the keys the instrument in front owns (they never sound; seq.c), and CHORD's part for chord.c */
static void sh_apply_keys(void)
{
    uint32_t kind = sh_kind();
    shell_chord_part = (uint8_t)(kind == SHK_CHORD ? sh.part + 1u : 0u);
    shell_chord_play = (uint8_t)(kind == SHK_CHORD ? 1u << sh.part : 0u);
    {
        uint32_t p;
        for (p = 0; p < 2u; p++)                        /* a CHORD take looping on: through CHORD PLAY still */
            if (p != sh.part && sh_track_note(p) < SH_NNOTES && SH_NOTES[sh_track_note(p)].kind == SHK_CHORD)
                shell_chord_play |= (uint8_t)(1u << p);
    }
    shell_kb_skip = kind == SHK_CHORD ? CB_KEYS : kind == SHK_ENGINE ? 0u : SH_ALL_KEYS;
    shell_al_front = (uint8_t)(kind == SHK_ALIVE);
    if (kind != SHK_CHORD)
        shell_chord_latch = 0;
    if (kind == SHK_NE && sh.note >= SH_NE_FIRST) {     /* a note_engine.h engine: the keys and the clock are its */
        uint32_t i = sh_ne_i(sh.note), c = sh_ne_ch(i);
        if (shell_ne_on[c] != i + 1u || shell_ne_part[c] != sh.part)
            sh_ne_start(c, i, sh.part);
        shell_ne_idx = (uint8_t)(i + 1u);
    } else
        shell_ne_idx = 0;
    if (shell_ne_on[0] && shell_ne_idx != shell_ne_on[0])
        sh_ne_stop(0u);                                 /* (the PLAY pair's engine goes with it) */
    sh_line_ne();
}

/* takes. A take is one pass of recording on the front pair's part, from arming (or the start) to disarming (or the
 * stop). Its first note keeps the steps as they were (sh_take_note, from rec_note: the ISR), for REC held: UNDO TAKE,
 * one level. On an empty pattern, from stop or while the transport runs, the first take sets LEN when it ends: the bars
 * from the loop's step 1 to its end, rounded to the nearest, one at least, the pattern's 64 steps at most; until then
 * the pattern is all 64 steps long */
static void sh_take_note(track_t *t)                    /* ISR: rec_note, before the note goes in */
{
    uint32_t p = trk_index(t);
    if (sh_take.live && sh_take.part == p)
        return;
    if (sh.ring == B_REC + 1u && sh.ring2)              /* REC held past its first ring, to undo or clear: a note */
        return;                                         /* the arm records meanwhile keeps the undo it is about to use */
    sh_take.n = (uint8_t)(sh_take.part == p && sh_pat_notes(t) ? sh_take.n + 1u : 1u);
    memcpy(sh_undo, t->step, sizeof sh_undo);
    sh_mundo_n = (uint8_t)motion_copy_auto(t, sh_mundo);   /* (and its knob moves: UNDO takes back those it made) */
    sh_mundo_ok = 1;
    sh_take.ulen = sh_take.first && sh_take.fpart == p ? sh_take.len0 : t->p[P_SLEN];   /* (a first take left for */
                                                                                    /* another part closes a frame late) */
    sh_take.part = (uint8_t)p;
    sh_take.note = sh.note;                             /* (a take is the pair in front's) */
    sh_take.live = sh_take.have = 1;
    sh_take.rhy = sh_take.dbl = 0;
    sh_sndu.newer = 0;                                  /* (a take after a sound load: REC held undoes the take) */
}
/* step 1 of part t's pattern, the transport running (seq.c seq_enter; ISR). MOUTH and SYLLABLE sing the next piece of
 * a phrase each note, a position no step records: on a part that holds a take or is recording one, every pass starts
 * the phrase again from its top, so each pass of a take recorded from step 1 sings what was sung live. (Not the
 * position at the take's first note: a take that began mid-phrase is rare, and that position would need keeping per
 * part and in projects; a live part with an empty pattern walks on as before) */
static void shell_loop_top(track_t *t)
{
    mth_part_t *mp;
#if FELUCCA_SYLLABLE
    syl_part_t *sp;
#endif
    if (t->engine != ENGI_MOUTH && t->engine != ENGI_SYLLABLE)
        return;
    if (!rec_on(t) && !sh_pat_notes(t))
        return;
    if (t->engine == ENGI_MOUTH && (mp = mouth_of(t)) != 0)
        mp->cursor = 0;
#if FELUCCA_SYLLABLE
    else if (t->engine == ENGI_SYLLABLE && (sp = syl_of(t)) != 0)
        sp->phr_pos = sp->c_next = sp->v_next = 0;
#endif
}
static uint32_t sh_bar_steps(const track_t *t)           /* the part's steps in a 4/4 bar (one at least) */
{
    uint32_t d = div_samples((uint32_t)t->p[P_SDIV]);
    return d ? clamp((int32_t)((4u * click_beat_len() + d / 2u) / d), 1, NSTEP) : 16u;
}
/* MOVE (after O_C's Meta-Q): while the transport runs, every EVERY bars the scale every part plays in moves: MODE to
 * the next darker mode on the same root (LYD MAJ MIX DOR MIN PHRY LOC, round), 5TH the root a fifth up, ALT between
 * the scale set by hand and ALT, RAND the root somewhere new. BACK: home after that many moves (0: never). The bars are
 * the sequencer's own (seq.c clk_abs, its 1/16s since PLAY), and a move lands in the block before the bar's first step
 * (sh_move_isr), so the downbeat plays in it. A stop, or a run from the top, puts each part's own scale back, so what
 * is saved (only while stopped) is never a moved one. A take recorded under a moving scale keeps its notes; QNT SEQ,
 * NEAR or DROP make the pattern follow the move */
static struct {
    uint8_t on, moved, n;                   /* running, a move made since PLAY, moves since home */
    uint8_t root[NTRK], scale[NTRK];        /* each part's own (home) */
    uint32_t bar, rnd;                      /* the bar a move was weighed for last */
} sh_mv;
static uint32_t sh_move_bar(void) { return clk_pos == CLK_START ? 0u : clk_abs >> 4; }   /* (1/16s, 16 a bar) */
static void sh_move_set(uint32_t root, uint32_t scale)  /* every part, as the SCALE page sets them */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_ROOT] = (int16_t)root;
        trk[i].p[P_SCALE] = (int16_t)scale;
    }
}
static void sh_move_go_home(void)                       /* each part's own back (irq off) */
{
    uint32_t i;
    if (sh_mv.moved)
        for (i = 0; i < NTRK; i++) {
            trk[i].p[P_ROOT] = sh_mv.root[i];
            trk[i].p[P_SCALE] = sh_mv.scale[i];
        }
    sh_mv.moved = sh_mv.n = 0;
}
/* the SCALE page set id by hand (every part the same): that is home now; the other of ROOT and SCALE stays as it was */
static void sh_move_home(const track_t *t, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        if (id == P_ROOT)
            sh_mv.root[i] = (uint8_t)t->p[P_ROOT];
        if (id == P_SCALE)
            sh_mv.scale[i] = (uint8_t)t->p[P_SCALE];
    }
}
/* bar `bar` begins: the move it brings, if any (ISR, or the frame under irq off) */
static void sh_move_bar_begins(uint32_t bar)
{
    static const uint8_t EVERY[5] = {0, 1, 2, 4, 8}, MODES[7] = {9, 1, 4, 3, 2, 8, 10};   /* (N_SCALE indexes) */
    const track_t *t = &trk[sh.part % NTRK];
    uint32_t every = EVERY[clamp(sh_sp[SP_MEVERY - SP_FIRST], 0, 4)], root, scale, i;
    if (!sh_mv.on || bar <= sh_mv.bar)
        return;
    sh_mv.bar = bar;
    if (!every || bar % every)
        return;
    if (sh_sp[SP_MBACK - SP_FIRST] && sh_mv.n >= (uint32_t)sh_sp[SP_MBACK - SP_FIRST]) {
        sh_move_go_home();                              /* BACK: home, and the moves begin again */
        return;
    }
    root = (uint32_t)t->p[P_ROOT] % 12u;
    scale = (uint32_t)t->p[P_SCALE];
    switch (sh_sp[SP_MHOW - SP_FIRST]) {
    case 0:                                             /* MODE */
        for (i = 0; i < 7u && MODES[i] != scale; i++)
            ;
        scale = MODES[i < 7u ? (i + 1u) % 7u : 1u];
        break;
    case 1:                                             /* 5TH */
        root = (root + 7u) % 12u;
        break;
    case 2:                                             /* ALT */
        scale = scale == sh_mv.scale[sh.part % NTRK] ? (uint32_t)sh_sp[SP_MALT - SP_FIRST] : sh_mv.scale[sh.part % NTRK];
        root = sh_mv.root[sh.part % NTRK];
        break;
    default:                                            /* RAND: somewhere new */
        sh_mv.rnd = sh_mv.rnd * 1103515245u + 12345u + bar;
        root = (root + 1u + (sh_mv.rnd >> 16) % 11u) % 12u;
        break;
    }
    sh_mv.n++;
    sh_mv.moved = 1;
    sh_move_set(root, scale);
}
/* ISR (seq.c events_block, after the clock's advance is known, before the parts step): the bar line falls in this
 * block (click_tick will cross it by the same adv), so its move lands first */
static int sh_ev_any(void);
static void sh_ev_bar_begins(uint32_t bar);
static void sh_move_isr(uint32_t adv)
{
    uint32_t p;
    if (!(sh_mv.on || sh_ev_any()) || !song.playing || shell_from_top || clk_pos == CLK_START || (clk_step & 15u) != 15u)
        return;
    p = (uint32_t)(grid_at(div_q16(2), clk_abs + 1u) - grid_at(div_q16(2), clk_abs));
    if (clk_pos + adv >= p) {
        sh_move_bar_begins((clk_abs + 1u) >> 4);
        sh_ev_bar_begins((clk_abs + 1u) >> 4);          /* (EVOLVE's moves land with MOVE's, before the downbeat) */
    }
}
/* switching patterns live (sequencing.md): the project slots are pattern banks. SEQ held, PRESETS walks NEXT for the
 * part in front (A B C D OFF); SEQ let go queues it. At the next bar line (16 steps of the clock, as MOVE's) the part
 * plays that slot's part: its steps, LEN DIV SWG GATE QNT, its knob moves and locks and (RUDIMENT, LINES) its lanes,
 * from its step 1. Its SOUND, every other part and the generators' state stay. One source slot at a time, for any
 * parts; stopped, it switches at once. The staging is the song chain's source 0 (project.c proj_q_stage: SQRL-1 runs
 * no chain; one the editor arms takes the sources back, and the queue goes) */
#define SH_Q_OFF 4u
static struct { uint8_t pick, picking, slot, mask, done, ln[2][SH_LANES_N]; } sh_q = {SH_Q_OFF, 0, SH_Q_OFF, 0, 0, {{0}}};
static uint8_t sh_q_dmask;                              /* the parts the last switch landed on */
static int sh_q_waits(uint32_t part) { return (sh_q.mask >> (part % NTRK)) & 1u; }
static void sh_q_drop(void)                             /* nothing queued (a stop, a project loaded, NEW) */
{
    fm1_irq_off();
    sh_q.mask = 0;
    sh_q.slot = SH_Q_OFF;
    sh_q.picking = 0;
    fm1_irq_on();
}
static int proj_q_stage(uint32_t slot, uint32_t k, uint8_t ln[2][SH_LANES_N]);   /* (project.c) */
static void sh_q_apply(uint32_t remain)                 /* ISR at the bar (remain: samples to it), or irq-off stopped */
{
    const chain_pattern_t *c = &chain.source[0];
    uint32_t k, i, n;
    for (k = 0; k < NPART; k++) {
        track_t *t = &trk[k];
        int32_t lk;
        if (!((sh_q.mask >> k) & 1u))
            continue;
        seq_release(t);
        motion_restore(t);
        memcpy(t->step, c->step[k], sizeof t->step);
        memcpy(&t->p[P_SLEN], c->timing[k], sizeof c->timing[k]);
        t->p[P_SQNT] = c->qnt[k];
        for (i = 0, n = 0; i < motion.count; i++)       /* its knob moves and locks: the slot's, the others' kept */
            if ((motion.event[i].place >> 6) != k)
                motion.event[n++] = motion.event[i];
        for (i = 0; i < c->motion.count && n < MOTION_MAX; i++)
            if ((c->motion.event[i].place >> 6) == k)
                motion.event[n++] = c->motion.event[i];
        for (i = n; i < motion.count; i++)
            memset(&motion.event[i], 0, sizeof motion.event[0]);
        motion.count = (uint8_t)n;
        for (i = 0; i < n && (motion.event[i].place >> 6) != k; i++)
            ;
        motion.on = (uint8_t)(i < n ? motion.on | 1u << k : motion.on & ~(1u << k));
        if ((lk = lanes_of(t)) >= 0) {
            sh_lanes_put((uint32_t)lk, sh_q.ln[lk]);
            for (i = 0; i < NLANE; i++)
                shell_lane_pos_i[lk][i] = 0;
            memset(shell_lstep[lk], 0, sizeof shell_lstep[lk]);   /* (no chance or ratchet of the pattern before
                                                                      * lands on the slot's hits) */
        }
        t->nd_late = t->nd_early = ND_NONE;
        t->nd_fired = 0;
        t->rat_left = 0;
        t->rh_n = t->rskip_n = 0;
        t->cond_loop = COND_LOOPS - 1u;                 /* (its step 1 is its loop 1) */
        {
            uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, period = div_samples((uint32_t)t->p[P_SDIV]);
            uint64_t pq = div_q16((uint32_t)t->p[P_SDIV]);
            uint32_t ka = seq_abs[k], cur = step_samples(t, period, len - 1u) - period +
                                            (grid_at(pq, ka + 1u) - grid_at(pq, ka));   /* (seq_tick's cur_len) */
            t->seq_idx = (uint16_t)(len - 1u);          /* its step 1 falls on the bar line */
            t->seq_pos = !remain ? 0x7FFFFFFFu : cur > remain ? cur - remain : 0u;
        }
    }
    sh_q.done = (uint8_t)(sh_q.slot + 1u);
    sh_q_dmask = sh_q.mask;
    sh_q.mask = 0;
    sh_q.slot = SH_Q_OFF;
}
static void sh_q_isr(uint32_t adv)                      /* ISR, as sh_move_isr: the bar line falls in this block */
{
    uint32_t p;
    if (!sh_q.mask)
        return;
    if (chain.armed || chain.running) {                 /* (a song took the sources) */
        sh_q.mask = 0;
        sh_q.slot = SH_Q_OFF;
        return;
    }
    if (!song.playing || shell_from_top || clk_pos == CLK_START || (clk_step & 15u) != 15u)
        return;
    p = (uint32_t)(grid_at(div_q16(2), clk_abs + 1u) - grid_at(div_q16(2), clk_abs));
    if (clk_pos + adv >= p)
        sh_q_apply(p > clk_pos ? p - clk_pos : 1u);
}
static int sh_q_kind(void)                              /* the part in front has a pattern to switch */
{
    uint32_t k = sh_kind();
    return k == SHK_ENGINE || k == SHK_CHORD || k == SHK_RUDIMENT || k == SHK_ACID ||
           (k == SHK_NE && sh.note == SH_NE_FIRST + 2u);   /* (HARP's take) */
}
static void sh_q_turn(int32_t s)                        /* SEQ held, PRESETS: NEXT for the part in front */
{
    static char b[40];
    if (!sh_q_kind()) {
        sh_toast("NO PATTERN TO SWITCH HERE", 0);
        return;
    }
    if (sh_take.first && sh_take.fpart == sh.part) {   /* (a first take still open: its LEN is not set yet) */
        sh_toast("CLOSE THE TAKE FIRST: TAP REC", 0);
        return;
    }
    if (!sh_q.picking) {
        sh_q.picking = 1;
        sh_q.pick = (uint8_t)((sh_q.mask >> sh.part) & 1u ? sh_q.slot : SH_Q_OFF);
    }
    sh_q.pick = (uint8_t)(((int32_t)sh_q.pick + (s > 0 ? 1 : -1) + 5) % 5);
    if (sh_q.pick == SH_Q_OFF) {
        sh_toast("NEXT: OFF", 0);
        return;
    }
    str_cpy(b, "NEXT ", sizeof b);
    b[5] = (char)('A' + sh_q.pick);
    b[6] = 0;
    str_cpy(b + 6, song.playing ? " AT THE BAR: SOUND STAYS" : " NOW: SOUND STAYS", sizeof b - 6u);
    sh_toast(b, 0);
}
static void sh_q_commit(void)                           /* SEQ let go after PRESETS: queued (or OFF) */
{
    static char b[40];
    uint32_t bit = 1u << sh.part;
    if (!sh_q.picking)
        return;
    sh_q.picking = 0;
    if (sh_q.pick == SH_Q_OFF) {
        fm1_irq_off();
        sh_q.mask &= (uint8_t)~bit;
        if (!sh_q.mask)
            sh_q.slot = SH_Q_OFF;
        fm1_irq_on();
        sh_toast("NEXT: OFF", 0);
        return;
    }
    if ((sh_q.mask & ~bit) && sh_q.slot != sh_q.pick) {
        str_cpy(b, "ONE SLOT AT A TIME: ", sizeof b);
        b[20] = (char)('A' + sh_q.slot);
        str_cpy(b + 21, " IS QUEUED", sizeof b - 21u);
        sh_toast(b, 0);
        return;
    }
    if (chain.armed || chain.running) {
        sh_toast("A SONG HAS THE SLOTS", 0);
        return;
    }
    fm1_irq_off();                                      /* (another part's staging stays; this one's goes in) */
    sh_q.mask &= (uint8_t)~bit;
    fm1_irq_on();
    if (!proj_q_stage(sh_q.pick, sh.part, sh_q.ln)) {
        str_cpy(b, "SLOT ", sizeof b);
        b[5] = (char)('A' + sh_q.pick);
        str_cpy(b + 6, " IS EMPTY", sizeof b - 6u);
        sh_toast(b, 0);
        return;
    }
    sh_len_keep(sh_trk(), 0u);                          /* (REC's first ring undoes the switch: the pattern as it was
                                                         * when queued, its lanes, their bytes and knob moves) */
    fm1_irq_off();
    sh_q.slot = sh_q.pick;
    sh_q.mask |= (uint8_t)bit;
    if (!song.playing)
        sh_q_apply(0);                                  /* stopped: at once, from step 1 */
    fm1_irq_on();
}
static void sh_q_poll(void)                             /* every frame: a switch made, said; stopped, none waits */
{
    static char b[24];
    if (!song.playing && sh_q.mask && !sh_q.picking)
        sh_q_drop();                                    /* (a stop before the bar: the switch goes) */
    if (!sh_q.done)
        return;
    str_cpy(b, "PATTERN ", sizeof b);
    b[8] = (char)('A' + sh_q.done - 1u);
    str_cpy(b + 9, ": SOUND STAYS", sizeof b - 9u);
    sh_q.done = 0;
    sh_toast(b, 0);
    sh_lfx_add(panel.btn[B_SEQ], LFX_BLINK);
    if ((sh_q_dmask >> sh.part) & 1u)
        sh_rhy_sum = sh_beat_sum(sh_trk());             /* (the lanes in front as loaded: no hand edit) */
}
static void sh_move_poll(void)                          /* every frame */
{
    uint32_t bar = sh_move_bar(), i;
    if (!song.playing || !sh_sp[SP_MEVERY - SP_FIRST]) {
        fm1_irq_off();
        if (sh_mv.on)                                   /* stopped (or MOVE OFF): each part's own */
            sh_move_go_home();
        sh_mv.on = 0;
        fm1_irq_on();
        return;
    }
    fm1_irq_off();
    if (!sh_mv.on) {                                    /* PLAY: each part's scale as it is is its home */
        for (i = 0; i < NTRK; i++) {
            sh_mv.root[i] = (uint8_t)trk[i].p[P_ROOT];
            sh_mv.scale[i] = (uint8_t)trk[i].p[P_SCALE];
        }
        sh_mv.moved = sh_mv.n = 0;
        sh_mv.bar = bar;
        sh_mv.rnd ^= fm1_ms;
        sh_mv.on = 1;
        shell_from_top = 0;
    } else if (shell_from_top) {                        /* a run from the top (GLO + PLAY, seq_start): home, the */
        sh_move_go_home();                              /* bars counted again */
        sh_mv.bar = bar;
        shell_from_top = 0;
    } else
        sh_move_bar_begins(bar);                        /* (a bar the ISR's look-ahead missed: an external clock) */
    fm1_irq_on();
}

/* EVOLVE (SEQ tapped again from SEQ): while the transport runs, every EVERY bars the part's pattern changes as it
 * plays, never in its stored steps: seq.c plays sh_ev_buf through shell_ev_steps. LINES' lanes: AMT hits move a degree
 * up or down (NOTES), to an empty step beside them (RHYTHM) or either (BOTH), from what the last move left; RUDIMENT's
 * beat: AMT hits drift to an empty step beside; a take: its notes moved whole by MOVE's degrees in SCALE from its own
 * steps (+3RD and +5TH climb, folding within an octave; OCT up and back; I-IV-V-I and I-VI-IV-V walk a progression).
 * BACK: its own steps again after that many moves (0: never). Seeded by the part and the move's number, so a loop
 * evolves the same way each run. A stop, EVERY OFF or a change to the stored steps plays them as they are again */
static step_t sh_ev_buf[NTRK][NSTEP];
static struct { uint8_t n; uint32_t sum; } sh_evs[NTRK];
static int sh_ev_any(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (sh_ev[k][0])
            return 1;
    return 0;
}
static uint32_t sh_ev_sum(const track_t *t)             /* the stored steps, FNV-1a */
{
    const uint8_t *b = (const uint8_t *)t->step;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof t->step; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static uint32_t sh_ev_mask(const track_t *t)            /* the part's scale (CHR: MAJOR, as the lanes take it) */
{
    uint32_t mask = scale_mask(t);
    return mask == 0xFFFu || !mask ? SCALE_MASK[1] : mask;
}
/* note moved deg degrees up the part's scale (a note between degrees keeps its distance from the one below) */
static uint32_t sh_ev_degree(const track_t *t, uint32_t note, uint32_t deg)
{
    uint32_t mask = sh_ev_mask(t), pcs[12], n = 0, i, root = (uint32_t)t->p[P_ROOT] % 12u, rel, oct, d = 0;
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            pcs[n++] = i;
    rel = (note + 120u - root) % 12u;
    oct = (note + 120u - root) / 12u;                   /* (+10 octaves, taken off below) */
    for (i = 0; i < n; i++)
        if (pcs[i] <= rel)
            d = i;
    i = d + deg;
    return (uint32_t)clamp((int32_t)(root + 12u * (oct + i / n) + pcs[i % n] + rel - pcs[d]) - 120, 0, 127);
}
/* move n of part k (ISR, or the frame under irq off) */
static void sh_ev_move(uint32_t k, uint32_t n)
{
    static const uint8_t PROG[2][4] = {{0, 3, 4, 0}, {0, 5, 3, 4}};
    const track_t *t = &trk[k];
    step_t *b = sh_ev_buf[k];
    uint32_t i, j, lanes = 0, x = (k + 1u) * 0x9E3779B9u ^ n * 2654435761u, amt = (uint32_t)clamp(sh_ev[k][1], 1, 8),
             what = (uint32_t)clamp(sh_ev[k][2], 0, 4);
    for (i = 0; i < NSTEP; i++)
        lanes |= t->step[i].hit;
    if (!lanes) {                                       /* a take: moved whole, from its own steps */
        uint32_t nd = (uint32_t)__builtin_popcount(sh_ev_mask(t)), up = what == 2u && (n & 1u) ? 12u : 0u;
        uint32_t deg = what == 0u ? 2u * n % nd : what == 1u ? 4u * n % nd : what == 2u ? 0u : PROG[what - 3u][n % 4u];
        memcpy(b, t->step, sizeof sh_ev_buf[k]);
        sh_evs[k].sum = sh_ev_sum(t);
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < b[i].n && j < STEP_NOTES; j++)
                b[i].note[j] = (uint8_t)clamp((int32_t)(sh_ev_degree(t, b[i].note[j], deg) + up), 0, 127);
        return;
    }
    if (!shell_ev_steps[k]) {                           /* lanes: the first move starts from the stored steps */
        memcpy(b, t->step, sizeof sh_ev_buf[k]);
        sh_evs[k].sum = sh_ev_sum(t);
    }
    while (amt--) {
        uint32_t hits = 0, pick, l, len, to, bit;
        for (i = 0; i < NSTEP; i++)
            hits += (uint32_t)__builtin_popcount(b[i].hit);
        if (!hits)
            return;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        pick = x % hits;
        for (i = 0; i < NSTEP && pick >= (uint32_t)__builtin_popcount(b[i].hit); i++)
            pick -= (uint32_t)__builtin_popcount(b[i].hit);
        for (l = 0; l < NLANE; l++)
            if (((b[i].hit >> l) & 1u) && !pick--)
                break;
        bit = 1u << l;
        if (k == 3u || what == 1u || (what == 2u && ((x >> 20) & 1u))) {   /* RHYTHM (RUDIMENT's part: its beat drifts) */
            len = shell_lane_len_i[k == 3u ? 0u : 1u][l];
            len = len ? len : 16u;
            to = (x >> 8) & 1u ? (i + 1u) % len : (i + len - 1u) % len;
            if ((b[to].hit & bit) || b[to].time == ST_TIE)
                continue;                               /* (taken: that change is skipped this move) */
            if (b[to].time == ST_REST)
                b[to].time = ST_NOTE;
            b[to].hit |= (uint8_t)bit;
            b[to].acc |= (uint8_t)(b[i].acc & bit);
            step_set_loct(&b[to], l, step_loct(&b[i], l));   /* (in its octave) */
        } else {                                        /* NOTES: a degree up or down */
            to = l == 0u || ((x >> 8) & 1u) ? l + 1u : l - 1u;
            if (to >= NLANE || ((b[i].hit >> to) & 1u))
                continue;
            b[i].hit |= (uint8_t)(1u << to);
            if (b[i].acc & bit)
                b[i].acc |= (uint8_t)(1u << to);
            step_set_loct(&b[i], to, step_loct(&b[i], l));
        }
        b[i].hit &= (uint8_t)~bit;
        b[i].acc &= (uint8_t)~bit;
        step_set_loct(&b[i], l, 0);
    }
}
/* RUDIMENT's MAP under EVOLVE: each move takes X or Y AMT x 4 one way or the other, seeded as the moves are; a stop,
 * BACK or EVERY OFF puts them back. The frame adds them to the knobs (sh_map_sync) */
static int8_t sh_map_dx, sh_map_dy;
static void sh_map_drift(uint32_t n)                    /* ISR */
{
    uint32_t x = (SH_BEAT + 1u) * 0x9E3779B9u ^ n * 2654435761u, amt = (uint32_t)clamp(sh_ev[SH_BEAT][1], 1, 8) * 4u;
    int8_t *d;
    x ^= x >> 13;
    x *= 0x5BD1E995u;
    d = (x >> 7) & 1u ? &sh_map_dx : &sh_map_dy;
    *d = (int8_t)clamp(*d + ((x >> 9) & 1u ? (int32_t)amt : -(int32_t)amt), -64, 64);
}
/* every frame: the MAP's knobs (and EVOLVE's drift) to seq.c, which plays them */
static int16_t sh_map_rhy(void) { return sh_li == 0u ? sh_sp[SP_RHY - SP_FIRST] : sh_lsp[0][0]; }   /* RUDIMENT's */
static void sh_map_sync(void)
{
    static const uint8_t DENS[3] = {SP_MKICK, SP_MSNARE, SP_MHATS};
    uint32_t k;
    if (!song.playing || !sh_ev[SH_BEAT][0])
        sh_map_dx = sh_map_dy = 0;
    shell_map_x = (uint8_t)(clamp(sh_sp[SP_MX - SP_FIRST] + sh_map_dx, 0, 127) * 2);
    shell_map_y = (uint8_t)(clamp(sh_sp[SP_MY - SP_FIRST] + sh_map_dy, 0, 127) * 2);
    shell_map_chaos = (uint8_t)sh_sp[SP_MCHAOS - SP_FIRST];
    shell_map_acc = (uint8_t)sh_sp[SP_MACC - SP_FIRST];
    for (k = 0; k < 3u; k++)
        shell_map_dens[k] = (uint8_t)(sh_sp[DENS[k] - SP_FIRST] * 2);
    shell_map_on = sh_map_rhy() == (int16_t)SH_MAP;
}
/* RUDIMENT's lane l at step i on the map (0 none, 2 a hit, 3 an accented hit), as it plays this cycle: the lanes in
 * front are RUDIMENT's (the keys and the screen show it), MAP on, l a map lane */
static uint32_t sh_map_at(uint32_t l, uint32_t i)
{
    if (sh_li || !shell_map_on || (l != 0u && l != 1u && l != 3u))
        return 0;
    return map_hit(l == 3u ? 2u : l, i, map_cyc ? map_cyc : 1u);
}
static void sh_ev_bar_begins(uint32_t bar)              /* ISR, with MOVE's */
{
    static const uint8_t EVERY[5] = {0, 1, 2, 4, 8};
    uint32_t k, every;
    if (!song.playing)
        return;
    for (k = 0; k < NTRK; k++) {
        every = EVERY[clamp(sh_ev[k][0], 0, 4)];
        if (!every || !bar || bar % every)
            continue;
        if (sh_ev[k][3] && sh_evs[k].n >= (uint32_t)sh_ev[k][3]) {
            shell_ev_steps[k] = 0;                      /* BACK: its own steps, and the moves begin again */
            sh_evs[k].n = 0;
            sh_map_dx = sh_map_dy = 0;
            continue;
        }
        if (k == SH_BEAT && shell_map_on) {             /* RUDIMENT's MAP: X and Y drift, the steps stay */
            sh_map_drift(sh_evs[k].n + 1u);
            sh_evs[k].n++;
            continue;
        }
        sh_ev_move(k, sh_evs[k].n + 1u);
        sh_evs[k].n++;
        shell_ev_steps[k] = sh_ev_buf[k];
    }
}
static void sh_ev_poll(void)                            /* every frame: stopped, OFF or edited, the steps as they are */
{
    uint32_t k;
    if (shell_from_top && song.playing) {               /* a run from the top (seq_start): each part's own steps */
        fm1_irq_off();
        for (k = 0; k < NTRK; k++) {
            shell_ev_steps[k] = 0;
            sh_evs[k].n = 0;
        }
        if (!sh_sp[SP_MEVERY - SP_FIRST])
            shell_from_top = 0;                         /* (MOVE off: nothing else clears it) */
        fm1_irq_on();
    }
    for (k = 0; k < NTRK; k++)
        if (shell_ev_steps[k] ? !song.playing || !sh_ev[k][0] || sh_ev_sum(&trk[k]) != sh_evs[k].sum : !song.playing && sh_evs[k].n) {
            fm1_irq_off();
            shell_ev_steps[k] = 0;
            sh_evs[k].n = 0;
            fm1_irq_on();
        }
}

/* a first take armed while the transport runs: its steps turned so the bar holding its first note is step
 * 1, with its knob moves, locks and recording marks, and the playhead with them, so it plays on in time. The steps
 * before the take were empty (a first take's), so nothing is lost. 0: nothing to turn (from step 1, no note, or the
 * lanes, whose own LENs keep their own steps) */
static void sh_rev(uint8_t *a, uint32_t sz, uint32_t i, uint32_t j)   /* elements i..j of a (size sz) reversed */
{
    uint32_t b;
    for (; i < j; i++, j--)
        for (b = 0; b < sz; b++) {
            uint8_t x = a[i * sz + b];
            a[i * sz + b] = a[j * sz + b];
            a[j * sz + b] = x;
        }
}
static void sh_turn(void *base, uint32_t sz, uint32_t n, uint32_t k)   /* n elements turned k to the left, in place
                                                                        * (three reversals: no copy to hold) */
{
    if (!(k %= n))
        return;
    sh_rev(base, sz, 0, k - 1u);
    sh_rev(base, sz, k, n - 1u);
    sh_rev(base, sz, 0, n - 1u);
}
static uint32_t sh_take_turn(track_t *t, uint32_t spb)
{
    uint32_t d, i, p = trk_index(t), b0, sh;
    for (d = 0; d < NSTEP; d++) {                       /* the first note after the take began */
        const step_t *st = &t->step[(sh_take.start + d) % NSTEP];
        if (st->time == ST_NOTE && (st->n || st->hit))
            break;
    }
    if (d == NSTEP || lanes_of(t) >= 0 || !sh_take.mid)
        return 0;
    b0 = (sh_take.start + d) / spb * spb;               /* (in steps from the loop's step 1, not wrapped) */
    if (b0 > sh_take.steps)
        b0 = sh_take.steps / spb * spb;
    sh = b0 % NSTEP;
    if (!sh)
        return 0;
    sh_turn(t->step, sizeof t->step[0], NSTEP, sh);
    sh_turn(rec_tag[p], sizeof rec_tag[p][0], NSTEP, sh);
    sh_turn(rec_mnote[p], sizeof rec_mnote[p][0], NSTEP, sh);
    for (i = 0; i < motion.count; i++)                  /* (knob moves and locks: their steps turn too) */
        if ((motion.event[i].place >> 6) == p)
            motion.event[i].place = (uint8_t)(p << 6 | (((motion.event[i].place & 63u) + NSTEP - sh) % NSTEP));
    t->seq_idx = (uint16_t)((t->seq_idx + NSTEP - sh) % NSTEP);
    sh_take.steps -= b0;
    return sh;
}
/* a first take ends (REC off, or its pair left the front): LEN in bars from the bar of its first note (sh_take_turn),
 * what ran past it gone; nothing played: LEN back as it was. (Knob moves recorded past the LEN stay, unplayed:
 * dropping them would leave a knob where its last move put it) */
static void sh_take_close(track_t *t)
{
    uint32_t spb = sh_bar_steps(t), bars, len, k;
    sh_take.first = 0;
    fm1_irq_off();
    (void)sh_take_turn(t, spb);
    bars = (sh_take.steps * 256u + sh_take.frac + spb * 128u) / (spb * 256u);
    len = clamp((int32_t)bars, 1, (int32_t)(NSTEP / spb)) * spb;
    if (sh_pat_any(t)) {
        for (k = len; k < NSTEP && len + spb <= NSTEP; k++)   /* a note past the bar it rounds to: a bar more, so */
            if (t->step[k].time == ST_NOTE && (t->step[k].n || t->step[k].hit))   /* nothing played is lost */
                len = (k / spb + 1u) * spb, k = len - 1u;
        len = len < NSTEP ? len : NSTEP;
        t->p[P_SLEN] = (int16_t)len;
        for (k = len; k < NSTEP; k++)                   /* (what ran past it with nothing played: rests) */
            step_clear(&t->step[k]);
        if (t->seq_idx >= len)                          /* (the playhead past the new end: the next step is a new */
            rec_pass_next(trk_index(t));                /* pass, so REPLACE replaces what this take played) */
    } else
        t->p[P_SLEN] = sh_take.len0;
    fm1_irq_on();
}
static void sh_take_poll(void)                          /* every frame */
{
    track_t *t = &trk[sh.part];
    uint32_t on;
    if (sh_take.first && sh_take.fpart != sh.part) {     /* its pair left the front (an arm follows the pair): */
        sh_take_close(&trk[sh_take.fpart % NTRK]);       /* the take ends on its own part */
        sh_take.rec = 0;
    }
    on = rec_on(t);
    if (on && !sh_take.rec) {                           /* a take begins: its first note keeps the steps */
        sh_take.first = !sh_pat_any(t) ||               /* (from stop, or while a pair behind keeps the transport going; */
                        (sh_take.live && sh_take.part == sh.part && sh_take.empty);   /* the count-in's notes went */
                                                        /* in before this frame) */
        if (sh_take.first) {
            sh_take.fpart = sh.part;
            sh_take.len0 = t->p[P_SLEN];
            t->p[P_SLEN] = NSTEP;
            sh_take.steps = t->seq_idx;
            sh_take.idx = t->seq_idx;
            sh_take.start = t->seq_idx;
            sh_take.mid = sh_take.ran;                  /* (from stop, its silent bars are the player's: kept) */
        }
    } else if (on && sh_take.first) {                   /* the first take's steps, counted as they pass */
        uint32_t c = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
        uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;   /* (the LEN in force: one turned meanwhile */
        sh_take.steps += (t->seq_idx % len + len - sh_take.idx % len) % len;   /* wraps there, not at 64) */
        sh_take.idx = t->seq_idx;
        sh_take.frac = (uint8_t)(c ? clamp((int32_t)(t->seq_pos * 256u / c), 0, 255) : 0);
    } else if (!on && sh_take.rec && sh_take.first) {   /* the first take ends: LEN in bars */
        sh_take_close(t);
    }
    if (!on) {                                          /* (a take's first note may come before the frame sees it begin) */
        fm1_irq_off();
        if (!rec_on(t))
            sh_take.live = 0;
        fm1_irq_on();
        sh_take.empty = (uint8_t)!sh_pat_any(t);
    }
    sh_take.rec = (uint8_t)on;
    sh_take.ran = song.playing;
}
/* REC held through its first ring: the last take undone (its steps and LEN as they were before it), or the beat a
 * rhythm load replaced (its lanes and RHYTHM too) */
static void sh_take_undo(void)
{
    track_t *t = sh_trk();
    uint32_t rhy = sh_take.rhy, k;
    if (project_new_kept()) {                           /* NEW, nothing changed since: the music before */
        project_new_undo();
        sh_toast("NEW UNDONE", 0);
        return;
    }
    if (sh_sound_first() && (undo.what & SH_UNDO_RESET)) {
        sh_reset_swap();                                /* a reset, the last thing done */
        return;
    }
    if (sh_sound_first()) {
        undo_swap();                                    /* the sound a load replaced, the last thing done (held again: */
        sh_toast("SOUND UNDONE", 0);                    /* the load again) */
        return;
    }
    if (!sh_take_here()) {                              /* (kept under another NOTE engine on this part: not this one's) */
        sh_toast("NO TAKE TO UNDO", 0);
        return;
    }
    fm1_irq_off();
    memcpy(t->step, sh_undo, sizeof sh_undo);
    t->p[P_SLEN] = sh_take.ulen;
    if (rhy) {
        memcpy(sh_eu, sh_undo_eu, sizeof sh_eu);
        memcpy(sh_lx, sh_undo_lx, sizeof sh_undo_lx);
        memcpy(sh_ediv, sh_undo_div, sizeof sh_undo_div);
        memcpy(shell_lstep[sh_li % 2u], sh_undo_ls, sizeof sh_undo_ls);
        for (k = 0; k < 8u; k++)
            shell_lane_len[k] = (uint8_t)sh_lane_ownc(k);
        sh_sp[SP_RHY - SP_FIRST] = sh_undo_rhy;
    }
    if (SH_NOTES[sh_take.note % SH_NNOTES].kind == SHK_ACID)   /* ACID: the LINE its kept line came from (a hand line:
                                                                * none), so DENS and SPAN never write over it */
        sh_sp[SP_ASEED - SP_FIRST] = sh_acid_useed;
    if (!sh_take.dbl)
        sh_take.n = (uint8_t)(sh_pat_notes(t) && sh_take.n ? sh_take.n - 1u : 0u);
    sh_take.have = sh_take.first = sh_take.rhy = 0;
    sh_take.live = 1;                                   /* (until the knob moves are back: no note keeps a new undo */
    sh_take.part = (uint8_t)trk_index(t);               /* over them meanwhile) */
    fm1_irq_on();
    if (sh_mundo_ok) {                                  /* the part's knob moves as they were: the take's own go */
        static motion_event_t gone[MOTION_MAX];
        (void)motion_take_auto(t, gone);
        motion_give_back(sh_mundo, sh_mundo_n);
    }
    fm1_irq_off();
    sh_mundo_n = sh_mundo_ok = 0;
    sh_take.live = 0;
    fm1_irq_on();
    sh_toast(sh_take.dbl == 2u ? "HALF UNDONE" : sh_take.dbl ? "DOUBLE UNDONE" : rhy ? "BEAT UNDONE" : "TAKE UNDONE", 0);
    sh_take.dbl = 0;
    if (rhy && sh_frzu.on && sh.note == SH_N_LINES) {   /* a generator's FREEZE: its notes and the generator back */
        sh_frzu.on = 0;
        memcpy(sh_lnote_i[1], sh_frzu.lnote, sizeof sh_frzu.lnote);
        sh_switch_pair(sh.mode, sh_frzu.note);
        sh_toast("FREEZE UNDONE", 0);
    }
}

/* FREEZE's capture, every frame: the step after the one LINE plays is emptied before it plays, so each step holds what
 * the generator played on it last time round; sh_frz_seen counts the steps since the transport started */
static uint16_t sh_frz_seen, sh_frz_at = 0xFFFFu;
static void sh_frz_poll(void)
{
    static uint32_t gen = SH_NNOTES;
    const track_t *t = &trk[SH_LINE];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u, i;
    if (sh_track_note(SH_LINE) != gen) {                /* another NOTE engine on LINE: nothing of it heard yet */
        gen = sh_track_note(SH_LINE);
        sh_frz_seen = 0;
        sh_frz_at = 0xFFFFu;
    }
    if (!song.playing) {
        sh_frz_at = 0xFFFFu;
        return;
    }
    if (t->seq_idx == sh_frz_at)
        return;
    fm1_irq_off();
    if (sh_frz_at == 0xFFFFu) {                         /* a new run: nothing heard yet but the step playing */
        for (i = 0; i < NSTEP; i++)
            if (i != t->seq_idx % NSTEP)
                sh_frz.n[i] = sh_frz.vel[i] = 0;
        sh_frz_seen = 0;
    }
    i = (t->seq_idx + 1u) % len;
    sh_frz.n[i] = sh_frz.vel[i] = 0;
    fm1_irq_on();
    sh_frz_seen = (uint16_t)(sh_frz_seen < 0xFFFFu ? sh_frz_seen + 1u : sh_frz_seen);
    sh_frz_at = t->seq_idx;
}
/* REC + OCT+: FREEZE, what a generator plays written into lanes to edit, with one undo (REC held: UNDO BEAT). MAP: its
 * hits into KICK SNARE HATCL as they play this cycle, and ALGORITHM leaves MAP; LINE: the line is already the lanes,
 * so DENS SPAN and LOOP stop rewriting it; ALIVE TONNETZ ENIGMA S+H CANON on LINE: the last loop heard into LINES' lanes, a pitch
 * a lane (lowest first, the eight most played; another note on the nearest), and LINE plays LINES under the same SOUND */
static int sh_freeze_ok(void)                           /* a generator in front, for FREEZE */
{
    return (sh.note == SH_N_RUDIMENT && shell_map_on) || (sh.note == SH_N_LINES && sh_sp[SP_RHY - SP_FIRST]) ||
           (sh.part == SH_LINE && (sh_kind() == SHK_ALIVE || sh_kind() == SHK_NE));
}
static void sh_freeze(void)
{
    track_t *t = sh_trk();
    uint32_t i, l, k;
    if (sh.note == SH_N_RUDIMENT && shell_map_on) {
        sh_beat_keep(t, (int16_t)SH_MAP, 1);
        fm1_irq_off();
        for (i = 0; i < 32u; i++)
            for (l = 0; l < 4u; l++) {
                uint32_t h = l == 2u ? 0u : sh_map_at(l, i);
                if (h)
                    sh_grid_hit(t, i, l, 1u);
                if (h & 1u)
                    t->step[i].acc |= (uint8_t)(1u << l);
            }
        sh_sp[SP_RHY - SP_FIRST] = 0;
        fm1_irq_on();
        sh_rhy_sum = sh_beat_sum(t);
        sh_toast_undo("FROZEN: THE MAP'S BEAT IS YOURS", 0);
        return;
    }
    if (sh.note == SH_N_LINES && sh_sp[SP_RHY - SP_FIRST]) {
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
        sh_sp[SP_RHY - SP_FIRST] = 0;
        sh_rhy_sum = sh_beat_sum(t);
        sh_toast_undo("FROZEN: THE LINE IS YOURS", 0);
        return;
    }
    if (sh.part == SH_LINE && (sh_kind() == SHK_ALIVE || sh_kind() == SHK_NE)) {
        uint8_t note[32][4], n[32], vel[32], pitch[16], cnt[16], keep[8];
        uint32_t len = (uint32_t)clamp(sh_take.first && sh_take.fpart == sh.part ? sh_take.len0 : t->p[P_SLEN], 1, 32),
                 np = 0, nk = 0, gen = sh.note;   /* (REC's press opened a first take: the LEN from before it) */
        if (sh_frz_seen < len) {
            sh_toast("PLAY A WHOLE LOOP, THEN FREEZE", 0);
            return;
        }
        fm1_irq_off();
        memcpy(note, sh_frz.note, sizeof note);
        memcpy(n, sh_frz.n, sizeof n);
        memcpy(vel, sh_frz.vel, sizeof vel);
        fm1_irq_on();
        for (i = 0; i < len; i++)                       /* the pitches it played, and how often */
            for (k = 0; k < n[i]; k++) {
                for (l = 0; l < np && pitch[l] != note[i][k]; l++)
                    ;
                if (l == np && np < 16u) {
                    pitch[np] = note[i][k];
                    cnt[np++] = 0;
                }
                if (l < np)
                    cnt[l]++;
            }
        if (!np) {
            sh_toast("IT PLAYED NOTHING TO FREEZE", 0);
            return;
        }
        while (nk < 8u && nk < np) {                    /* the eight most played */
            uint32_t b = 0;
            for (l = 1; l < np; l++)
                if (cnt[l] > cnt[b])
                    b = l;
            keep[nk++] = pitch[b];
            cnt[b] = 0;
        }
        for (i = 1; i < nk; i++)                        /* lowest first */
            for (l = i; l && keep[l - 1u] > keep[l]; l--) {
                uint8_t x = keep[l];
                keep[l] = keep[l - 1u];
                keep[l - 1u] = x;
            }
        sh_switch_pair(sh.mode, SH_N_LINES);
        t = sh_trk();
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
        sh_frzu.on = 1;
        sh_frzu.note = (uint8_t)gen;
        memcpy(sh_frzu.lnote, sh_lnote_i[1], sizeof sh_frzu.lnote);
        fm1_irq_off();
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        memset(shell_lstep[1], 0, sizeof shell_lstep[1]);   /* (LINES' bytes with the columns FREEZE writes) */
        t->p[P_SLEN] = (int16_t)len;
        for (l = 0; l < 8u; l++) {
            sh_eu_i[1][0][l] = sh_eu_i[1][1][l] = sh_eu_i[1][2][l] = 0;
            sh_ediv_i[1][l] = 0;
            sh_lx_i[1][SH_LX_PAD][l] = 0;
            shell_lane_len_i[1][l] = 0;
            sh_lnote_i[1][l] = (int16_t)(l < nk ? keep[l] : 0);
        }
        for (i = 0; i < len; i++)
            for (k = 0; k < n[i]; k++) {
                uint32_t b = 0;
                for (l = 1; l < nk; l++)                /* (a note not kept: the nearest kept) */
                    if ((keep[l] > note[i][k] ? keep[l] - note[i][k] : note[i][k] - keep[l]) <
                        (keep[b] > note[i][k] ? keep[b] - note[i][k] : note[i][k] - keep[b]))
                        b = l;
                sh_grid_hit(t, i, b, 1u);
                if (vel[i] > 100u)
                    t->step[i].acc |= (uint8_t)(1u << b);
            }
        sh_sp[SP_RHY - SP_FIRST] = 0;
        fm1_irq_on();
        sh_lane_notes();
        sh_rhy_sum = sh_beat_sum(t);
        sh_toast_undo("FROZEN INTO LINES: ", SH_NOTES[gen].name);
        return;
    }
    sh_toast("FREEZE: PUT A GENERATOR IN FRONT", 0);
}

/* the pairing a project keeps (project.c FUN11, project_t.pair): [0] the SOUND slot in front, [1] its NOTE engine,
 * [2] the part they play on, [3..5] the same for the pair behind ([3] SH_NMODES: none), [6] the song's GROOVE
 * (0 SWING: every FUN11 before it), [7] 0 */
/* a project's pairing (project.c pair[8]): [0..2] the pair in front and its part, [3..5] the pair behind, [6] GROOVE,
 * [7] bit 0: [3] says "none" with SH_PAIR_NONE. Projects before it (bit 0 clear) said it with SH_PAIR_NONE_FUN11, the
 * catalogue's length then: read so for them, whatever the catalogue holds now. Bit 1: RUDIMENT is a NOTE engine;
 * before it (clear), SOUND 3 was RUDIMENT, so a half on 3 plays RUDIMENT */
#define SH_PAIR_NONE 127u
#define SH_PAIR_NONE_FUN11 21u
typedef char sh_pair_none_free[SH_NMODES < SH_PAIR_NONE ? 1 : -1];
/* RUDIMENT's lanes in a project (project.c project_t.lanes, FUN11's tag 6): LEN[8] FILL[8] ROT[8] NOTE[8] OCT (+ 2)
 * DIV[8] (signed, 0 x1: an older project's 33 bytes leave it at x1) */
static void sh_lanes_get(uint32_t li, uint8_t *l)      /* lanes engine li: 0 RUDIMENT, 1 LINES */
{
    uint32_t i, k;
    li %= SH_NLI;
    for (k = 0; k < 3u; k++)
        for (i = 0; i < 8u; i++)                        /* (a logic lane's FILL -op as 64 + op) */
            l[k * 8u + i] = (uint8_t)(k == 1u && sh_eu_i[li][k][i] < 0 ? 64 - sh_eu_i[li][k][i] : sh_eu_i[li][k][i]);
    for (i = 0; i < 8u; i++) {
        l[24u + i] = (uint8_t)sh_lnote_i[li][i];
        l[33u + i] = (uint8_t)(int8_t)sh_ediv_i[li][i];
    }
    l[32] = (uint8_t)(sh_lanes_sp(li, 2) + 2);
    l[41] = (uint8_t)(li == 1u ? sh_sp[SP_LTRN - SP_FIRST] + 12 : 12);   /* (LINES' TRN and STYLE) */
    l[42] = (uint8_t)(li == 1u ? sh_sp[SP_LSTY - SP_FIRST] : 0);
    for (i = 0; i < 8u; i++) {                          /* LANE 2: PAD LOOP WAKE - 1 NAP, LOOP's seeds (little end first) */
        l[43u + i] = (uint8_t)sh_lx_i[li][SH_LX_PAD][i];
        l[51u + i] = (uint8_t)sh_lx_i[li][SH_LX_LOOP][i];
        l[59u + i] = (uint8_t)(sh_lx_i[li][SH_LX_WAKE][i] - 1);
        l[67u + i] = (uint8_t)sh_lx_i[li][SH_LX_NAP][i];
        for (k = 0; k < 4u; k++)
            l[75u + 4u * i + k] = (uint8_t)(shell_lane_seed_i[li][i] >> (8u * k));
    }
}
static void sh_lanes_put(uint32_t li, const uint8_t *l)   /* (the ISR too: sh_q_apply) */
{
    uint32_t i;
    int16_t (*eu)[8];
    li %= SH_NLI;
    eu = sh_eu_i[li];
    for (i = 0; i < 8u; i++) {                          /* (a LEN the part's: =LEN, as a project before =LEN had it) */
        eu[0][i] = (int16_t)(l[i] == sh_lanes_len(li) ? 0 : clamp(l[i], 0, NSTEP));
        eu[1][i] = (int16_t)(l[8u + i] > 64u && l[8u + i] < 64u + LOP_N && i >= 2u ? 64 - (int32_t)l[8u + i] :
                             clamp(l[8u + i], 0, eu[0][i] ? eu[0][i] : NSTEP));
        eu[2][i] = (int16_t)clamp(l[16u + i], 0, NSTEP - 1);
        shell_lane_len_i[li][i] = (uint8_t)eu[0][i];
        sh_lnote_i[li][i] = (int16_t)clamp(l[24u + i], 0, 127);
        sh_ediv_i[li][i] = (int16_t)clamp((int8_t)l[33u + i], SH_DIV_MIN, SH_DIV_MAX);
    }
    if (li == sh_li)
        sh_sp[SP_LOCT - SP_FIRST] = (int16_t)clamp((int32_t)l[32] - 2, -2, 2);
    else
        sh_lsp[li][2] = (int16_t)clamp((int32_t)l[32] - 2, -2, 2);
    if (li == 1u) {
        sh_sp[SP_LTRN - SP_FIRST] = (int16_t)clamp((int32_t)l[41] - 12, -12, 12);
        sh_sp[SP_LSTY - SP_FIRST] = (int16_t)clamp(l[42], 0, 4);
    }
    for (i = 0; i < 8u; i++) {
        sh_lx_i[li][SH_LX_PAD][i] = (int16_t)clamp(l[43u + i], 0, 16);
        sh_lx_i[li][SH_LX_LOOP][i] = (int16_t)clamp(l[51u + i], 0, 4);
        sh_lx_i[li][SH_LX_WAKE][i] = (int16_t)clamp(l[59u + i] + 1, 1, 8);
        sh_lx_i[li][SH_LX_NAP][i] = (int16_t)clamp(l[67u + i], 0, 8);
        shell_lane_seed_i[li][i] = (uint32_t)l[75u + 4u * i] | (uint32_t)l[76u + 4u * i] << 8 |
                                   (uint32_t)l[77u + 4u * i] << 16 | (uint32_t)l[78u + 4u * i] << 24;
        if (eu[0][i])                                   /* (its own cycle: LEN + PAD) */
            shell_lane_len_i[li][i] = (uint8_t)clamp(eu[0][i] + sh_lx_i[li][SH_LX_PAD][i], 0, NSTEP);
    }
}
static void sh_lanes_set(uint32_t li, const uint8_t *l)
{
    fm1_irq_off();
    sh_lanes_put(li, l);
    fm1_irq_on();
}
/* a part's FILTER / COMB extras in a project (project.c project_t.iext, FUN11's tag 5): DRV KTRK ENV + 64 */
static void sh_iext_get(uint32_t part, uint8_t *d)
{
    d[0] = (uint8_t)clamp(ins_ext[part % NTRK][IX_DRV], 0, 127);
    d[1] = (uint8_t)clamp(ins_ext[part % NTRK][IX_KTRK], 0, 127);
    d[2] = (uint8_t)clamp(ins_ext[part % NTRK][IX_ENV] + 64, 0, 127);
}
static void sh_iext_set(uint32_t part, const uint8_t *d)
{
    ins_ext[part % NTRK][IX_DRV] = (int16_t)clamp(d[0], 0, 127);
    ins_ext[part % NTRK][IX_KTRK] = (int16_t)clamp(d[1], 0, 127);
    ins_ext[part % NTRK][IX_ENV] = (int16_t)(clamp(d[2], 0, 127) - 64);
}
/* a part's DRUMS sound per drum in a project (project.c project_t.drl, FUN11's tag 7): 32 bytes + 64 */
static void sh_drl_get(uint32_t part, uint8_t *d)
{
    uint32_t k, i;
    for (k = 0; k < DRL_N; k++)
        for (i = 0; i < DV_NLANE; i++)
            d[k * DV_NLANE + i] = (uint8_t)(drum_lane_d[part % NTRK][k][i] + 64);
}
static void sh_drl_set(uint32_t part, const uint8_t *d)
{
    uint32_t k, i;
    for (k = 0; k < DRL_N; k++)
        for (i = 0; i < DV_NLANE; i++)
            drum_lane_d[part % NTRK][k][i] = (int16_t)clamp((int32_t)d[k * DV_NLANE + i] - 64, -64, 63);
}
static void sh_pair_get(uint8_t *p)
{
    p[0] = sh.mode;
    p[1] = sh.note;
    p[2] = sh.part;
    p[3] = SH_PAIR_NONE;                                /* (behind: no more; the tracks are tag 9, sh_tracks_get) */
    p[4] = p[5] = 0;
    p[6] = (uint8_t)sh_sp[SP_GROOVE - SP_FIRST];       /* the song's GROOVE */
    p[7] = 3u;
}

/* RUDIMENT's MAP for a project (project.c tag 10): on, X Y CHAOS ACC KICK SNARE HATS */
static void sh_map_get(uint8_t *m)
{
    uint32_t k;
    m[0] = sh_map_rhy() == (int16_t)SH_MAP;
    for (k = 0; k < 7u; k++)
        m[1u + k] = (uint8_t)sh_sp[SP_MX + k - SP_FIRST];
}
static void sh_map_set(const uint8_t *m)
{
    int16_t *rhy = sh_li == 0u ? &sh_sp[SP_RHY - SP_FIRST] : &sh_lsp[0][0];
    uint32_t k;
    for (k = 0; k < 7u; k++)
        sh_sp[SP_MX + k - SP_FIRST] = (int16_t)clamp(m[1u + k], 0, 127);
    if (m[0])
        *rhy = (int16_t)SH_MAP;
    else if (*rhy == (int16_t)SH_MAP)
        *rhy = 0;
}
/* CHORD's PROG for a project (project.c tag 11): PROG EVERY DIR LEN */
static void sh_prog_get(uint8_t *m)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        m[k] = (uint8_t)sh_sp[SP_PROG + k - SP_FIRST];
}
static void sh_prog_set(const uint8_t *m)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        const param_desc_t *d = &SP_DESC[SP_PROG + k - SP_FIRST];
        sh_sp[SP_PROG + k - SP_FIRST] = (int16_t)clamp(m[k], d->min, d->max);
    }
}
/* ACID's line for a project (project.c tag 12): its LINE seed, ROT, TRN and OCT (+ 64), so it plays as it was made;
 * then the LINE pages' own knobs (SH_LINE_SP) */
static const uint8_t SH_LINE_SP[6] = {SP_AFLUX, SP_FLUX, SP_SCLK, SP_SLEAP, SP_HSTRUM, SP_HSPAN};
static void sh_acid_get(uint8_t *m)
{
    uint32_t k;
    m[0] = (uint8_t)sh_sp[SP_ASEED - SP_FIRST];
    m[1] = (uint8_t)sh_sp[SP_AROT - SP_FIRST];
    m[2] = (uint8_t)(sh_sp[SP_ATRN - SP_FIRST] + 64);
    m[3] = (uint8_t)(sh_sp[SP_AOCT - SP_FIRST] + 64);
    for (k = 0; k < 6u; k++)
        m[4u + k] = (uint8_t)sh_sp[SH_LINE_SP[k] - SP_FIRST];
}
static void sh_acid_set(const uint8_t *m)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        const param_desc_t *d = &SP_DESC[SP_ASEED + k - SP_FIRST];
        sh_sp[SP_ASEED + k - SP_FIRST] = (int16_t)clamp((int32_t)m[k] - (k >= 2u ? 64 : 0), d->min, d->max);
    }
    for (k = 0; k < 6u; k++) {
        const param_desc_t *d = &SP_DESC[SH_LINE_SP[k] - SP_FIRST];
        sh_sp[SH_LINE_SP[k] - SP_FIRST] = (int16_t)clamp(m[4u + k], d->min, d->max);
    }
}
/* the tracks for a project (project.c tag 9): LINE's SOUND and NOTE, BEAT's (127: none), the mutes */
static void sh_tracks_get(uint8_t *b)                   /* [0..3] LINE's and BEAT's, [4] the mutes, [5..8] parts 1 and 2's */
{
    static const uint8_t AT[4] = {0, 2, 5, 7}, PART[4] = {SH_LINE, SH_BEAT, 0, 1};
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint16_t pp = sh_partpair[PART[i]];
        b[AT[i]] = (uint8_t)(pp == SH_NOPAIR ? 127u : pp & 0xFFu);
        b[AT[i] + 1u] = (uint8_t)(pp == SH_NOPAIR ? 127u : pp >> 8);
    }
    b[4] = sh_tmute;
}

/* a project load (project.c project_restore_runtime). Before it overwrites the parts, the pairs playing keep what
 * they have in their slots; after it, the project's pairs come to the front and behind, each taking its loaded part
 * as its state (the SOUND slot's snapshot, the NOTE engine's record). Slots the project does not name keep theirs. A
 * project without pairs (FUN10 and older) lands on the pair in front, on the part it selected (a ping-pong pair: part
 * 1 or 2) */
/* the companion (project.c PROJ_CMP): what a project leaves out, kept beside it so a power-off loses nothing. The
 * takes parked off their parts (the PLAY slots, LINE's and BEAT's banks), the NOTE engines' records, the SOUNDs'
 * snapshots and the knob moves parked with their pairs. A tagged stream: tag, length (2 bytes, LSB first), payload;
 * tag 0 ends it. A tag this firmware does not know is skipped, so a section can be added later without a new format.
 * What a part plays now is in the project itself: its slot or bank is left out. Written while stopped (a save, the
 * autosave), put back by a load before its pairs come to the front (sh_project_loaded) */
enum { SHC_END, SHC_PK, SHC_BANK, SHC_NREC, SHC_SNAP, SHC_SNAPX, SHC_PARK, SHC_EXTRA = 32 };
typedef void (*sh_put_f)(uint32_t b);
typedef int (*sh_get_f)(void);
static uint32_t shc_n;                                  /* a section's length, counted */
static void shc_count(uint32_t b) { (void)b; shc_n++; }
static void shc_u16(sh_put_f put, uint32_t v) { put(v & 0xFFu); put(v >> 8 & 0xFFu); }
static void shc_u32(sh_put_f put, uint32_t v) { shc_u16(put, v & 0xFFFFu); shc_u16(put, v >> 16); }
static int shc_live_bank(uint32_t b)                    /* a part plays a NOTE engine of bank b now */
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (sh_track_note(k) < SH_NNOTES && SH_NREC_BANK[sh_track_note(k)] == b)
            return 1;
    return 0;
}
static int shc_live_pair(uint16_t pp)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (sh_partpair[k] == pp)
            return 1;
    return 0;
}
static void shc_steps(sh_put_f put, const step_t *st)  /* a bitmap of the steps in use, then each of those whole */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i += 8u) {
        uint32_t m = 0;
        for (j = 0; j < 8u; j++)
            m |= (uint32_t)!!memcmp(&st[i + j], &(const step_t){0}, sizeof(step_t)) << j;
        put(m);
    }
    for (i = 0; i < NSTEP; i++)
        if (memcmp(&st[i], &(const step_t){0}, sizeof(step_t)))
            for (j = 0; j < sizeof(step_t); j++)
                put(((const uint8_t *)&st[i])[j]);
}
static void shc_sec(sh_put_f put, uint32_t tag, void (*body)(sh_put_f))   /* a section, its length counted first */
{
    shc_n = 0;
    body(shc_count);
    if (!shc_n)
        return;
    put(tag);
    shc_u16(put, shc_n);
    body(put);
}
static void shc_pk(sh_put_f put)                        /* the PLAY slots: their pairs, ages and takes */
{
    uint32_t i, any = 0;
    for (i = 0; i < SH_NPK; i++)
        any |= sh_pk_pair[i] != SH_PK_NONE && !shc_live_pair(sh_pk_pair[i]);
    if (!any)
        return;
    shc_u32(put, sh_pk_n);
    for (i = 0; i < SH_NPK; i++) {
        uint16_t pp = sh_pk_pair[i] != SH_PK_NONE && !shc_live_pair(sh_pk_pair[i]) ? sh_pk_pair[i] : SH_PK_NONE;
        shc_u16(put, pp);
        shc_u32(put, sh_pk_age[i]);
        if (pp != SH_PK_NONE)
            shc_steps(put, sh_nstep_bank[i]);
    }
}
static void shc_bank(sh_put_f put)                      /* the other banks no part plays now: index, steps */
{
    uint32_t b, i;
    for (b = SH_NPK; b < NELEM(sh_nstep_bank); b++) {
        for (i = 0; i < NSTEP && !memcmp(&sh_nstep_bank[b][i], &(const step_t){0}, sizeof(step_t)); i++)
            ;
        if (i == NSTEP || shc_live_bank(b))
            continue;
        put(b);
        shc_steps(put, sh_nstep_bank[b]);
    }
}
static void shc_nrec(sh_put_f put)                      /* each NOTE engine's record: its NOTE ids, as 16 bits */
{
    uint32_t r, i, n = 0;
    for (i = 0; i < P_COUNT; i++)
        n += sh_half(i) == SH_NOTE;
    for (r = 0; r < SH_NNOTES && !sh_nrec[r].valid; r++)
        ;
    if (r == SH_NNOTES)
        return;
    put(n);
    for (r = 0; r < SH_NNOTES; r++)
        if (sh_nrec[r].valid) {
            put(r);
            for (i = 0; i < P_COUNT; i++)
                if (sh_half(i) == SH_NOTE)
                    shc_u16(put, (uint16_t)sh_nrec[r].p[i]);
        }
}
static void shc_snap(sh_put_f put)                      /* the SOUNDs' snapshots, whole (their size first) */
{
    uint32_t m, i;
    for (m = 0; m < SH_NMODES && !(sh_wheel_avail(m) && sh_snap_of(m)->valid); m++)
        ;
    if (m == SH_NMODES)
        return;
    shc_u16(put, sizeof(sh_snap_t));
    for (m = 0; m < SH_NMODES; m++)
        if (sh_wheel_avail(m) && sh_snap_of(m)->valid) {
            put(m);
            for (i = 0; i < sizeof(sh_snap_t); i++)
                put(((const uint8_t *)sh_snap_of(m))[i]);
        }
}
static void shc_snapx(sh_put_f put)                     /* FM6's patch, LIBRARY's, DRUMS' sound per drum */
{
    uint32_t i;
    if (!sh_snap_of(0)->valid && !sh_snap_of(3)->valid && !sh_snap_of(2)->valid)   /* (FM6, DRUMS, LIBRARY: none visited) */
        return;
    shc_u16(put, sizeof sh_snapx);
    for (i = 0; i < sizeof sh_snapx; i++)
        put(((const uint8_t *)sh_snapx)[i]);
}
static void shc_park(sh_put_f put)                      /* the knob moves parked with their pairs */
{
    uint32_t i;
    if (!sh_park_n)
        return;
    put(sh_park_n);
    for (i = 0; i < sh_park_n; i++) {
        put(sh_park_ev[i].place);
        put(sh_park_ev[i].param);
        put((uint8_t)sh_park_ev[i].value);
        shc_u16(put, sh_park_own[i]);
    }
}
/* later sections (per-lane step bytes for RUDIMENT and LINES, round 4's item 2) write here, as tag SHC_EXTRA + i,
 * through shc_sec, and read in sh_cmp_get_extra: 1 it took the section (len bytes read), 0 an unknown tag */
static uint8_t sh_ls_b(uint32_t v, uint32_t r);
static uint8_t sh_ls_seen;                              /* the companion loaded held the lanes' own bytes */
static void shc_ls(sh_put_f put)                        /* the lanes' own bytes: a version, then each lane's 64-step
                                                         * map of the steps it holds a byte on, then those bytes */
{
    uint32_t k, l, i, j;
    put(1u);
    for (k = 0; k < 2u; k++)
        for (l = 0; l < NLANE; l++) {
            for (i = 0; i < NSTEP; i += 8u) {
                uint32_t m = 0;
                for (j = 0; j < 8u; j++)
                    m |= (uint32_t)(shell_lstep[k][l][i + j] != 0u) << j;
                put(m);
            }
            for (i = 0; i < NSTEP; i++)
                if (shell_lstep[k][l][i])
                    put(shell_lstep[k][l][i]);
        }
}
static void shc_lo(sh_put_f put)                        /* the lanes' octaves (the project keeps a step's JUST only for
                                                         * its notes): each lanes part's 64-step map, then theirs */
{
    uint32_t p, i, j;
    for (p = 0; p < NPART; p++) {
        const step_t *st = trk[p].step;
        if (!rec_lo_part(&trk[p]))
            continue;                                   /* (a lanes part's steps, whatever notes they hold) */
        for (i = 0; i < NSTEP && !step_lo_w(&st[i]); i++)
            ;
        if (i == NSTEP)
            continue;
        put(p);
        for (i = 0; i < NSTEP; i += 8u) {
            uint32_t m = 0;
            for (j = 0; j < 8u; j++)
                m |= (uint32_t)(step_lo_w(&st[i + j]) != 0u) << j;
            put(m);
        }
        for (i = 0; i < NSTEP; i++)
            if (step_lo_w(&st[i]))
                for (j = 0; j < 3u; j++)
                    put(st[i].just[j]);
    }
}
/* a queued pattern switch (project.c proj_q_stage): the slot's companion read for the lanes' octaves of part k alone,
 * onto st, its staged steps; every other section skipped */
static void sh_cmp_lo_stage(sh_get_f get, uint32_t k, step_t *st)
{
    for (;;) {
        int tag = get(), lo = get(), hi = get(), v;
        uint32_t len, i, j;
        uint8_t m[NSTEP / 8u];
        if (tag < 0 || tag == SHC_END || lo < 0 || hi < 0)
            return;
        len = (uint32_t)lo | (uint32_t)hi << 8;
        if (tag != (int)(SHC_EXTRA + 1u)) {
            while (len--)
                if (get() < 0)
                    return;
            continue;
        }
        while (len > sizeof m) {                        /* a part, its map, each mapped step's three bytes */
            uint32_t pt = (uint32_t)get();
            len--;
            for (i = 0; i < sizeof m; i++, len--)
                if ((v = get()) < 0)
                    return;
                else
                    m[i] = (uint8_t)v;
            for (i = 0; i < NSTEP; i++)
                if ((m[i / 8u] >> (i % 8u)) & 1u)
                    for (j = 0; j < 3u; j++, len--) {
                        if ((v = get()) < 0)
                            return;
                        if (pt == k)
                            st[i].just[j] = (uint8_t)v;
                    }
        }
        return;
    }
}
static void sh_cmp_put_extra(sh_put_f put)
{
    shc_sec(put, SHC_EXTRA, shc_ls);
    shc_sec(put, SHC_EXTRA + 1u, shc_lo);
}
static int sh_cmp_get_extra(uint32_t tag, uint32_t len, sh_get_f get)
{
    uint8_t m[NSTEP / 8u];
    uint32_t k, l, i;
    int v;
    (void)len;
    if (tag == SHC_EXTRA + 1u) {                        /* the lanes' octaves, onto the steps of hits loaded */
        while ((v = get()) >= 0 && v < (int)NPART) {
            step_t *st = trk[v].step;
            for (i = 0; i < sizeof m; i++) {
                if ((v = get()) < 0)
                    return 1;
                m[i] = (uint8_t)v;
            }
            for (i = 0; i < NSTEP; i++)
                if ((m[i / 8u] >> (i % 8u)) & 1u)
                    for (l = 0; l < 3u; l++) {
                        if ((v = get()) < 0)
                            return 1;
                        st[i].just[l] = (uint8_t)v;
                    }
        }
        return 1;
    }
    if (tag != SHC_EXTRA)
        return 0;
    if (get() != 1)                                     /* (a later version's: skipped, the columns migrate) */
        return 1;
    for (k = 0; k < 2u; k++)
        for (l = 0; l < NLANE; l++) {
            for (i = 0; i < sizeof m; i++) {
                if ((v = get()) < 0)
                    return 1;
                m[i] = (uint8_t)v;
            }
            for (i = 0; i < NSTEP; i++) {
                v = (m[i / 8u] >> (i % 8u)) & 1u ? get() : 0;
                if (v < 0)
                    return 1;
                shell_lstep[k][l][i] = (uint8_t)v;
            }
        }
    sh_ls_seen = 1;
    return 1;
}
/* a project saved before the lanes had their own bytes: each column's CHNC and RATCH goes to every lane's byte on that
 * step (as it sounded), and leaves the column, where a lanes engine no longer reads it */
static void sh_ls_migrate(void)
{
    uint32_t k, p, i, l;
    memset(shell_lstep, 0, sizeof shell_lstep);
    for (k = 0; k < 2u; k++) {
        step_t *st = sh_nsteps(k ? SH_N_LINES : SH_N_RUDIMENT);
        for (p = 0; p < NPART; p++)
            if (lanes_of(&trk[p]) == (int32_t)k)
                st = trk[p].step;
        fm1_irq_off();
        for (i = 0; i < NSTEP; i++) {
            if (!st[i].probability && step_ratchet(&st[i]) == 1u)
                continue;
            for (l = 0; l < NLANE; l++)
                shell_lstep[k][l][i] = sh_ls_b(step_chance_v(&st[i]), step_ratchet(&st[i]));
            st[i].probability = 0;
            step_set_ratchet(&st[i], 1u);
        }
        fm1_irq_on();
    }
}
static void sh_cmp_pack(sh_put_f put)
{
    shc_sec(put, SHC_PK, shc_pk);
    shc_sec(put, SHC_BANK, shc_bank);
    shc_sec(put, SHC_NREC, shc_nrec);
    shc_sec(put, SHC_SNAP, shc_snap);
    shc_sec(put, SHC_SNAPX, shc_snapx);
    shc_sec(put, SHC_PARK, shc_park);
    sh_cmp_put_extra(put);
    put(SHC_END);
}
static sh_get_f shc_get;
static uint32_t shc_left;                               /* the section's bytes still to read */
static int shc_b(void)                                  /* a byte of the section, -1 past its end */
{
    if (!shc_left)
        return -1;
    shc_left--;
    return shc_get();
}
static int shc_rd(void *dst, uint32_t n)
{
    uint32_t i;
    int c;
    for (i = 0; i < n; i++) {
        if ((c = shc_b()) < 0)
            return -1;
        ((uint8_t *)dst)[i] = (uint8_t)c;
    }
    return 0;
}
static int shc_r16(uint32_t *v)
{
    uint8_t b[2];
    if (shc_rd(b, 2u))
        return -1;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8;
    return 0;
}
static int shc_r32(uint32_t *v)
{
    uint32_t lo, hi;
    if (shc_r16(&lo) || shc_r16(&hi))
        return -1;
    *v = lo | hi << 16;
    return 0;
}
static int shc_rsteps(step_t *st)
{
    uint8_t m[NSTEP / 8u];
    uint32_t i;
    if (shc_rd(m, sizeof m))
        return -1;
    memset(st, 0, NSTEP * sizeof(step_t));
    for (i = 0; i < NSTEP; i++)
        if (((m[i / 8u] >> (i % 8u)) & 1u) && shc_rd(&st[i], sizeof(step_t)))
            return -1;
    for (i = 0; i < NSTEP; i++)                         /* (a step out of range: as empty) */
        if (st[i].n > STEP_NOTES || st[i].time > ST_REST)
            memset(&st[i], 0, sizeof st[i]);
    return 0;
}
/* the companion put back (project.c, from sh_project_loaded): 0 taken, -1 malformed (what was read stays: every
 * section is checked on its own) */
static int sh_cmp_unpack(sh_get_f get)
{
    uint32_t i;
    int tag;
    shc_get = get;
    for (i = 0; i < SH_NPK; i++)                        /* (the companion is the parked music: none but its own) */
        sh_pk_pair[i] = SH_PK_NONE;
    for (i = SH_NPK; i < NELEM(sh_nstep_bank); i++)      /* (the pairs the project plays fill theirs after this) */
        memset(sh_nstep_bank[i], 0, sizeof sh_nstep_bank[i]);
    for (;;) {
        uint32_t len, v, k;
        uint8_t hb[3];
        shc_left = 3u;
        if ((tag = get()) < 0)
            return -1;
        if (tag == SHC_END)
            return 0;
        shc_left = 2u;
        if (shc_r16(&len))
            return -1;
        shc_left = len;
        if (tag == SHC_PK) {
            uint32_t pn;
            if (shc_r32(&pn))
                return -1;
            for (i = 0; i < SH_NPK; i++) {
                uint32_t pp, age;
                if (shc_r16(&pp) || shc_r32(&age))
                    return -1;
                if (pp != SH_PK_NONE) {
                    if (shc_rsteps(sh_nstep_bank[i]))
                        return -1;
                    if ((pp & 0xFFu) < SH_NMODES && (pp >> 8) < SH_NNOTES && sh_pk_kind(pp >> 8)) {
                        sh_pk_pair[i] = (uint16_t)pp;
                        sh_pk_age[i] = age;
                    }
                }
            }
            sh_pk_n = pn;
        } else if (tag == SHC_BANK) {
            while (shc_left) {
                int b = shc_b();
                if (b < (int)SH_NPK || b >= (int)NELEM(sh_nstep_bank))
                    return -1;
                if (shc_rsteps(sh_nstep_bank[b]))
                    return -1;
            }
        } else if (tag == SHC_NREC) {
            uint32_t n = 0;
            int c = shc_b();
            for (i = 0; i < P_COUNT; i++)
                n += sh_half(i) == SH_NOTE;
            if (c != (int)n)                            /* (another firmware's NOTE ids: skipped) */
                goto skip;
            while (shc_left) {
                int r = shc_b();
                if (r < 0 || r >= (int)SH_NNOTES)
                    return -1;
                for (i = 0; i < P_COUNT; i++)
                    if (sh_half(i) == SH_NOTE) {
                        if (shc_r16(&v))
                            return -1;
                        sh_nrec[r].p[i] = (int16_t)param_fit(&TP[i], (int16_t)v);
                    }
                sh_nrec[r].valid = 1;
            }
        } else if (tag == SHC_SNAP) {
            if (shc_r16(&v))
                return -1;
            if (v != sizeof(sh_snap_t))
                goto skip;
            while (shc_left) {
                int m = shc_b();
                sh_snap_t o;
                if (m < 0 || m >= (int)SH_NMODES || shc_rd(&o, sizeof o))
                    return -1;
                if (o.valid && sh_wheel_avail((uint32_t)m) && o.eng < NENGINES) {
                    for (k = 0; k < P_COUNT; k++)
                        o.p[k] = (int16_t)param_fit(param_desc_of(eng_idx(o.eng), k), o.p[k]);
                    *sh_snap_of((uint32_t)m) = o;
                }
            }
        } else if (tag == SHC_SNAPX) {
            if (shc_r16(&v))
                return -1;
            if (v != sizeof sh_snapx)
                goto skip;
            if (shc_rd(sh_snapx, sizeof sh_snapx))
                return -1;
        } else if (tag == SHC_PARK) {
            int n = shc_b();
            if (n < 0 || n > (int)MOTION_MAX)
                return -1;
            sh_park_n = 0;
            for (i = 0; i < (uint32_t)n; i++) {
                uint32_t own;
                if (shc_rd(hb, 3u) || shc_r16(&own))
                    return -1;
                sh_park_ev[sh_park_n].place = hb[0];
                sh_park_ev[sh_park_n].param = hb[1];
                sh_park_ev[sh_park_n].value = (int8_t)hb[2];
                sh_park_own[sh_park_n] = (uint16_t)own;
                if (motion_param(MOTION_ID(&sh_park_ev[sh_park_n])))
                    sh_park_n++;
            }
        } else if (sh_cmp_get_extra((uint32_t)tag, len, shc_b)) {
            ;
        } else {
skip:
            ;
        }
        while (shc_left)                                /* (the rest of a section: skipped) */
            if (shc_b() < 0)
                return -1;
    }
}
static void proj_cmp_pending(void);                    /* (project.c: a companion that came with the project) */
static void sh_project_before(void)
{
    if (!sh.ready)
        return;
    sh_q_drop();                                        /* (the slot it would have switched from is another song's) */
    fm1_irq_off();                                      /* each part's own scale back first, so the slots keep it; */
    sh_move_go_home();                                  /* then nothing is put back over the project loaded */
    sh_mv.on = 0;
    fm1_irq_on();
    sh_take.first = sh_take.rec = 0;                    /* (an open first take ends with the music it was on, not on the
                                                         * pattern loaded over it) */
    {
        uint32_t p;
        for (p = 0; p < NPART; p++)                     /* the parts left playing keep theirs too (first: a SOUND */
            if (p != sh.part && sh_track_note(p) < SH_NNOTES) {   /* on one and in front keeps the front's) */
                sh_snap_save(sh_partpair[p] & 0xFFu, p);
                sh_nrec_save(sh_rec_of(sh_partpair[p] & 0xFFu, sh_track_note(p)), sh_partpair[p] & 0xFFu, p);
                if (sh_pk_kind(sh_track_note(p)))       /* (the project takes the part: the take parked) */
                    sh_pk_store(sh_partpair[p] & 0xFFu, sh_track_note(p), p, 1);
                sh_mode_off(SH_NOTES[sh_track_note(p)].kind, p);
            }
    }
    sh_snap_save(sh.mode, sh.part);
    sh_nrec_save(sh_rec_of(sh.mode, sh.note), sh.mode, sh.part);
    if (sh_pk_kind(sh.note))
        sh_pk_store(sh.mode, sh.note, sh.part, 1);
    sh_mode_off(sh_kind(), sh.part);
}
static void sh_project_loaded(const uint8_t *pair, int has, const uint8_t *tracks)
{
    uint32_t fm = sh.mode, fn = sh.note, fp, k;
    uint16_t pp[NPART] = {SH_NOPAIR, SH_NOPAIR, SH_NOPAIR, SH_NOPAIR};
    if (!sh.ready)
        return;
    sh_park_n = 0;                                      /* (the project's knob moves are on its parts: none parked) */
    sh_ls_seen = 0;
    proj_cmp_pending();                                 /* (its companion: the parked takes, records and snapshots) */
    sh_sp[SP_GROOVE - SP_FIRST] = (int16_t)(has && pair[6] < GR_N ? pair[6] : 0u);   /* (the song's; none: SWING) */
    if (has && sh_wheel_avail(pair[0]) && pair[1] < SH_NNOTES && pair[2] < NPART) {   /* (a SOUND left out: as none) */
        uint32_t bm, bn;
        fm = pair[0];
        fn = pair[1];
        fp = pair[2];
        if (!(pair[7] & 2u) && fm == 3u)               /* (RUDIMENT, saved as a SOUND) */
            fn = SH_N_RUDIMENT;
        bm = pair[3];                                   /* a project from before the tracks: its pair behind, if it */
        bn = pair[4];                                   /* is a track's, is that track's */
        if (!(pair[7] & 2u) && bm == 3u)
            bn = SH_N_RUDIMENT;
        if (sh_wheel_avail(bm) && ((pair[7] & 1u) || bm != SH_PAIR_NONE_FUN11) && bn < SH_NNOTES &&
            sh_is_track(sh_pair_part(bm, bn)))
            pp[sh_pair_part(bm, bn)] = (uint16_t)(bm | bn << 8);
    } else
        fp = SH_MODES[fm].part != SH_PINGPONG ? SH_MODES[fm].part : SH_NOTES[fn].part != SH_PINGPONG ? SH_NOTES[fn].part :
             song.sel < 2u ? song.sel : 0u;
    if (sh_is_track(fp) && sh_pair_part(fm, fn) != fp)  /* (a pair saved where it no longer plays: its own part) */
        fp = sh_pair_part(fm, fn);
    for (k = 0; k < 2u; k++)                            /* the tracks the project names (tag 9) */
        if (sh_wheel_avail(tracks[2u * k]) && tracks[2u * k + 1u] < SH_NNOTES &&
            sh_pair_part(tracks[2u * k], tracks[2u * k + 1u]) == SH_LINE + k)
            pp[SH_LINE + k] = (uint16_t)(tracks[2u * k] | tracks[2u * k + 1u] << 8);
    for (k = 0; k < 2u; k++)                            /* .. and the PLAY pairs looping on parts 1 and 2 */
        if (sh_wheel_avail(tracks[5u + 2u * k]) && tracks[6u + 2u * k] < SH_NNOTES &&
            SH_MODES[tracks[5u + 2u * k]].part == SH_PINGPONG && SH_NOTES[tracks[6u + 2u * k]].part == SH_PINGPONG)
            pp[k] = (uint16_t)(tracks[5u + 2u * k] | tracks[6u + 2u * k] << 8);
    pp[fp] = (uint16_t)(fm | fn << 8);
    for (k = 0; k < NPART; k++)                         /* (one pair, one part: the front's wins) */
        if (k != fp && pp[k] == pp[fp])
            pp[k] = SH_NOPAIR;
    sh_tmute = (uint8_t)(tracks[4] & 15u);
    for (k = 0; k < NPART; k++) {                       /* each pair takes its loaded part as its state (the front's
                                                         * last: a SOUND on a track too keeps the front's); the parts no
                                                         * pair holds are silent, as ever */
        if (pp[k] != SH_NOPAIR && k != fp) {
            sh_snap_save(pp[k] & 0xFFu, k);
            sh_nrec_save(sh_rec_of(pp[k] & 0xFFu, pp[k] >> 8), pp[k] & 0xFFu, k);
        }
        sh_partpair[k] = pp[k];
        trk[k].p[P_MUTE] = (int16_t)(pp[k] == SH_NOPAIR || ((sh_tmute >> k) & 1u));
    }
    sh_snap_save(fm, fp);
    sh_nrec_save(sh_rec_of(fm, fn), fm, fp);
    if (SH_MODES[fm].part == SH_PINGPONG && SH_NOTES[fn].part == SH_PINGPONG)
        sh.pp = (uint8_t)fp;
    sh.mode = (uint8_t)fm;
    sh.note = (uint8_t)fn;
    if (SH_NOTES[fn].kind == SHK_RUDIMENT)
        sh_lanes_use(fn == SH_N_LINES);
    sh.part = (uint8_t)fp;
    song.sel = (uint8_t)fp;
    sh.relmask = 0;
    sh.global = SHG_NONE;
    sh.mom = sh.save = 0;
    sh_sndu.on = 0;
    if (song.rec)
        song.rec = (uint8_t)(1u << fp);
    sh_lane_notes();
    sh_mode_on(fm, &trk[fp], 0);
    if (shell_lstep_back)                               /* NEW's undo: the lanes' bytes from before NEW */
        memcpy(shell_lstep, shell_lstep_new, sizeof shell_lstep);
    else if (!sh_ls_seen)
        sh_ls_migrate();
}

/* power-on: the shell's settings as they were saved; an unset byte means the default */
/* the pair the settings name (the one played last), if this build offers its SOUND: entered, 1 */
static int sh_enter_saved(void)
{
    if (SH_ST[0] != SH_ST_MARK || !SH_ST[SH_ST_PAIR] || SH_ST[SH_ST_PAIR] > SH_NMODES || !SH_ST[SH_ST_PAIR + 1u] ||
        SH_ST[SH_ST_PAIR + 1u] > SH_NNOTES || !sh_wheel_avail(SH_ST[SH_ST_PAIR] - 1u))
        return 0;
    sh.note = (uint8_t)(SH_ST[SH_ST_PAIR + 1u] - 1u);
    if (sh_st3_v1 && SH_ST[SH_ST_PAIR] == 4u)           /* (RUDIMENT, saved as a SOUND) */
        sh.note = SH_N_RUDIMENT;
    if (SH_NOTES[sh.note].kind == SHK_NE || SH_NOTES[sh.note].kind == SHK_ALIVE)
        sh.note = 0;                                    /* (a generator comes back only with the music it played: the
                                                         * autosave's pairs, after this) */
    sh_enter(SH_ST[SH_ST_PAIR] - 1u);
    if (!SH_LIB(sh.mode)) {                       /* the sound it was on: a user slot still of its engine, or the */
        track_t *t = sh_trk();                          /* preset (else the first, as a pair's first visit plays) */
        uint32_t e = t->eng_req;                        /* (the engine asked for: the ISR may not have switched yet) */
        if (SH_ST[SH_ST_SND + 1u] && sh_user_of(e, SH_ST[SH_ST_SND + 1u] - 1u))
            up_load(SH_ST[SH_ST_SND + 1u] - 1u);
        else if (SH_ST[SH_ST_SND] && SH_ST[SH_ST_SND] - 1u < ENGINES[e]->npresets && SH_ST[SH_ST_SND] - 1u != t->preset)
            sh_apply_preset(t, SH_ST[SH_ST_SND] - 1u);
    }
    return 1;
}

static void sh_store_load(void)
{
    uint32_t i;
#if FELUCCA_FLASH
    phr_load();                                         /* (every settings write carries it on) */
#endif
    sh_st3_v1 = SH_ST3[0] == SH_ST3_MARK_V1;
    if (SH_ST3[0] == SH_ST3_MARK || sh_st3_v1 || SH_ST3[0] == SH_ST3_MARK_V2) {   /* the wheel as the player set it */
        /* (its white keys are not read: they are the build's eight) */
        for (i = 0; i < 5u && SH_ST3[0] == SH_ST3_MARK; i++)
            if (SH_ST3[9u + i] && SH_ST3[9u + i] <= SH_NNOTES)
                sh_wnoteslot[i] = (uint8_t)(SH_ST3[9u + i] - 1u);
    }
    if (SH_ST[0] != SH_ST_MARK) {                       /* never saved: the defaults go in unmarked, so nothing is written for them */
        for (i = 0; i < NELEM(SH_ST_IDS); i++)
            SH_ST[1u + i] = (uint8_t)(sh_sp[SH_ST_IDS[i] - SP_FIRST] - SP_DESC[SH_ST_IDS[i] - SP_FIRST].min + 1);
        return;
    }
    sh_rec_mode_set(SH_ST[SH_ST_KNOBS] >= 1u && SH_ST[SH_ST_KNOBS] <= 4u ? SH_ST[SH_ST_KNOBS] - 1u : 0u);   /* (2: the
                                                        * NOTES AND KNOBS before REC MODE, ADD + KNOBS now) */
    for (i = 0; i < NELEM(SH_ST_IDS); i++)
        if (SH_ST[1u + i]) {
            const param_desc_t *d = &SP_DESC[SH_ST_IDS[i] - SP_FIRST];
            sh_sp[SH_ST_IDS[i] - SP_FIRST] = (int16_t)clamp(SH_ST[1u + i] - 1 + d->min, d->min, d->max);
        }
    for (i = 0; i < SH_NNE; i++) {                      /* the note engines' four, once prepared (their ranges) */
        uint32_t k;
        sh_ne_prepare(i);
        for (k = 0; k < 4u; k++)
            if (sh_st2(i)[k])
                sh_ne_val[i][k] = (int16_t)clamp(sh_st2(i)[k] - 1 + sh_ne_pd[i][k].min,
                                                 sh_ne_pd[i][k].min, sh_ne_pd[i][k].max);
    }
}

/* nothing to hear a flash write in (an erase stops the audio ~40 ms): the transport stopped, no voice sounding on any
 * part, the echo's tail gone (under about -60 dB of the bus). The gate of the settings' write (who 0: sh_store_poll)
 * and the autosave's (who 1: project.c autosave_quiet). fx_wet_peak starts again from each look; a tail one of them
 * takes is kept for the other's next look (sh_wet: bit per reader), so each sees the tail since its own last look */
static uint8_t sh_wet;
static int sh_quiet(uint32_t who)
{
    uint32_t i, k, quiet = !transport_busy();
    for (k = 0; k < NTRK && quiet; k++)
        for (i = 0; i < NVOICE; i++)
            quiet &= !trk[k].v[i].active;
    if (fx_wet_peak >= 64)
        sh_wet = 3u;
    fx_wet_peak = 0;
    quiet &= !((sh_wet >> who) & 1u);
    sh_wet &= (uint8_t)~(1u << who);
    return (int)quiet;
}

/* every frame: a changed setting into the settings object (RAM), written once settled and quiet */
static void sh_store_poll(void)
{
    uint32_t i, changed = 0;
    for (i = 0; i < NELEM(SH_ST_IDS); i++) {
        const param_desc_t *d = &SP_DESC[SH_ST_IDS[i] - SP_FIRST];
        uint8_t b = (uint8_t)(sh_sp[SH_ST_IDS[i] - SP_FIRST] - d->min + 1);
        if (SH_ST[1u + i] != b) {
            SH_ST[1u + i] = b;
            changed = 1;
        }
    }
    for (i = 0; i < SH_NNE; i++) {
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            uint8_t b = sh_ne_ready[i] ? (uint8_t)(sh_ne_val[i][k] - sh_ne_pd[i][k].min + 1) : 0u;
            if (b && sh_st2(i)[k] != b) {
                sh_st2(i)[k] = b;
                changed = 1;
            }
        }
    }
    {   /* the wheel: once changed from the default stash, all of it */
        uint8_t b[14];
        b[0] = SH_ST3_MARK;
        for (i = 0; i < SH_SLOTS; i++)
            b[1u + i] = sh_wslot[i] == SH_EMPTY ? 0xFEu : (uint8_t)(sh_wslot[i] + 1u);
        for (i = 0; i < 5u; i++)
            b[9u + i] = (uint8_t)(sh_wnoteslot[i] + 1u);
        if (SH_ST3[0] == SH_ST3_MARK ? memcmp(SH_ST3, b, sizeof b) != 0 :
            SH_ST3[0] == SH_ST3_MARK_V1 || SH_ST3[0] == SH_ST3_MARK_V2 ||   /* (an older mark: written anew once, so a
                                                                             * pair saved since reads as saved) */
            (sh_wslot[0] != 0u || sh_wslot[1] != 1u || sh_wslot[2] != SH_E4 || sh_wslot[3] != 3u || sh_wslot[4] != SH_G4 ||
             sh_wslot[5] != SH_A4 || sh_wslot[6] != SH_B4 || sh_wslot[7] != SH_C5 ||
             memcmp(sh_wnoteslot, SH_WNOTE_DEF, sizeof SH_WNOTE_DEF))) {
            memcpy(SH_ST3, b, sizeof b);
            changed = 1;
        }
    }
    if (SH_ST[SH_ST_KNOBS] != sh_rec_mode() + 1u) {
        SH_ST[SH_ST_KNOBS] = (uint8_t)(sh_rec_mode() + 1u);
        changed |= SH_ST[0] == SH_ST_MARK || sh_rec_mode();   /* (ADD, the default, alone: nothing to write) */
    }
    if (SH_ST[SH_ST_PAIR] != sh.mode + 1u || SH_ST[SH_ST_PAIR + 1u] != sh.note + 1u) {
        SH_ST[SH_ST_PAIR] = (uint8_t)(sh.mode + 1u);
        SH_ST[SH_ST_PAIR + 1u] = (uint8_t)(sh.note + 1u);
        changed |= SH_ST[0] == SH_ST_MARK || sh.mode || sh.note;   /* the power-on pair alone is nothing to write */
    }
    {
        const track_t *t = sh_trk();
        uint8_t pr = 0, us = 0;
        if (!SH_LIB(sh.mode)) {
            us = t->user;
            pr = t->user ? 0u : (uint8_t)(t->preset + 1u);
        }
        if (SH_ST[SH_ST_SND] != pr || SH_ST[SH_ST_SND + 1u] != us) {
            SH_ST[SH_ST_SND] = pr;
            SH_ST[SH_ST_SND + 1u] = us;
            changed |= SH_ST[0] == SH_ST_MARK || us || pr > 1u;   /* (the first preset alone: nothing to write) */
        }
    }
    if (changed) {
        SH_ST[0] = SH_ST_MARK;
        sh_st.pending = 1;
        sh_st.t = fm1_ms;
        sh_st.page = sh.page[sh.mode];
        sh_st.mode = sh.mode;
    }
    if (!sh_st.pending)
        return;
    if (fm1_ms - sh_st.t < SH_ST_IDLE_MS && sh_st.page == sh.page[sh.mode] && sh_st.mode == sh.mode)
        return;                                         /* still being turned: not settled */
    if (!sh_quiet(0u))
        return;
    sh_st.pending = 0;
    settings_save();                                    /* settings_poll writes it now, unless unchanged */
}

/* every frame: the shell's FX settings to the buses, CHORD PLAY to chord.c and seq.c, then CHORD's own: BASS to
 * chord.c, the chord a root plays when no quality is held (QNT SNAP: the scale's, DIA3; QNT OFF: a major one), and
 * the B3 key */
static void sh_chord_poll(void)
{
    fx_wow = (uint8_t)sh_sp[SP_WOW - SP_FIRST];            /* the shell's FX settings to the buses (fx.c) */
    fx_pre = (uint8_t)sh_sp[SP_PRE - SP_FIRST];
    fx_cmode = (uint8_t)sh_sp[SP_CMODE - SP_FIRST];
    shell_chord_strm = (int8_t)sh_sp[SP_STRM - SP_FIRST];  /* CHORD PLAY to chord.c and seq.c */
    shell_chord_rpt = (uint8_t)sh_sp[SP_RPT - SP_FIRST];
    shell_chord_prog = (uint8_t)sh_sp[SP_PROG - SP_FIRST];   /* PROG to chord.c and seq.c */
    shell_chord_pevery = (uint8_t)sh_sp[SP_PEVERY - SP_FIRST];
    shell_chord_pdir = (uint8_t)sh_sp[SP_PDIR - SP_FIRST];
    shell_chord_plen = (uint8_t)sh_sp[SP_PLEN - SP_FIRST];
    shell_gen_part = (uint8_t)(sh.part != SH_LINE && (sh_kind() == SHK_ALIVE || (sh_kind() == SHK_NE && SH_NOTES[sh.note].clocked)) ?
                               sh.part + 1u : 0u);   /* (seq.c seq_play; HARP, played from the keys, is not one) */
    song_groove = (uint8_t)sh_sp[SP_GROOVE - SP_FIRST];   /* (the song's GROOVE to core.h) */
    sh_sp[SP_CLICK - SP_FIRST] = (int16_t)rp_get(RP_CLICK);   /* (SYSTEM 2 shows the settings as they are) */
    sh_sp[SP_CIN - SP_FIRST] = (int16_t)rp_get(RP_COUNTIN);
    shell_chord_top = (uint8_t)sh_sp[SP_TOP - SP_FIRST];
    shell_chord_tune = (uint8_t)sh_sp[SP_TUNE - SP_FIRST];
    sh_spell_poll();
#if FELUCCA_LOOP
    {   /* LOOP: it holds the echo's tape while it is the SOUND in front (fx.c hands it over); its keys' side. Leaving
         * it gives the tape to the echo, which clears it (no RAM for a second 128 KiB line): a tape REC has been on
         * says so as LOOP goes */
        static int16_t q = -1;                          /* QUAL while LOOP holds the tape (-1: not), + 256: REC was on */
        int on = sh_eng() == ENGI_LOOP && !sh.splash_t, taped = q >= 256;
        loop_kp.mode = (uint8_t)sh_sp[SP_LMODE - SP_FIRST];
        loop_kp.position = (uint8_t)sh_sp[SP_LPOS - SP_FIRST];
        loop_kp.grain = (uint8_t)sh_sp[SP_LGRN - SP_FIRST];
        loop_kp.spray = (uint8_t)sh_sp[SP_LSPR - SP_FIRST];
        loop_kp.scan = (uint8_t)sh_sp[SP_LSCN - SP_FIRST];
        if (loop_hold && !on && taped)
            sh_toast("LOOP: TAPE CLEARED", 0);
        loop_hold = (uint8_t)on;
        if (on && q >= 0 && (sh_trk()->p[P_E4] & 255) != (q & 255)) {   /* a new tape speed changes the cells' format:
                                                                         * it is wiped */
            sh_toast("TAPE CLEARED", 0);
            taped = 0;
        }
        q = on ? (int16_t)((sh_trk()->p[P_E4] & 255) | (taped || sh_trk()->p[P_E1] ? 256 : 0)) : -1;
    }
#endif
    track_t *t;
    if (sh_kind() != SHK_CHORD) {
        sh_b3.down = 0;                                 /* CHORD was left with B3 down: no release to act on */
        return;
    }
    t = sh_trk();
    if (t->p[P_AMODE] && sh_sp[SP_RPT - SP_FIRST])     /* (an arp mode and RPT both on, as an old project or a knob */
        sh_sp[SP_RPT - SP_FIRST] = 0;                   /* move left them: the arp, as the ISR plays it) */
    shell_chord_bass = (uint8_t)sh_sp[SP_BASS - SP_FIRST];
    t->p[P_CHRD] = (int16_t)(t->p[P_QUANT] ? CH_DIA3 : CH_MAJ);
    if ((fm1_in.notes >> CB_LATCH) & 1u) {
        if ((fm1_in.buttons & (kb_mask | 1u << panel.btn[B_REC])) || sh_hold_view())
            sh_b3.used = 1;                             /* a modifier held (SEQ's step 4, FX's REVERSE, ..), or the
                                                         * SEQ page (B3 a step): the key is its, not LATCH's or the
                                                         * chord reset's */
        if (!sh_b3.down) {
            sh_b3.down = 1;
            sh_b3.used = (uint8_t)((fm1_in.buttons & (kb_mask | 1u << panel.btn[B_REC])) != 0u || sh_hold_view());
            sh_b3.t0 = fm1_ms;
            shell_kb_skip = SH_ALL_KEYS;                /* held: the keys add notes and none sounds */
        } else if (!sh_b3.used && !sh.ring && fm1_ms - sh_b3.t0 >= SH_TAP_MS &&
                   (shell_chord_add || shell_chord_qual)) {
            sh.ring = SH_RING_CHORD;                    /* a reset is on offer: the ring fills */
            sh.ring_t0 = fm1_ms;
        }
    } else if (sh_b3.down) {
        uint32_t d = fm1_ms - sh_b3.t0;
        sh_b3.down = 0;
        shell_kb_skip = CB_KEYS;
        if (sh.ring == SH_RING_CHORD)
            sh.ring = 0;
        if (!sh_b3.used && d < SH_TAP_MS) {
            shell_chord_latch ^= 1u;
            sh_toast(shell_chord_latch ? "LATCH ON" : "LATCH OFF", 0);
        } else if (!sh_b3.used && d >= SH_LONG_MS && (shell_chord_add || shell_chord_qual)) {
            shell_chord_add = 0;
            shell_chord_qual = 0;
            sh_toast("STANDARD CHORDS", 0);
        }
    }
}

static void sh_mode_on(uint32_t m, track_t *t, int first)
{
    (void)m;
    (void)t;
    (void)first;                                        /* a first visit is handled in sh_enter_pair */
    sh_apply_keys();
}

static void sh_mode_off(uint32_t kind, uint32_t part)
{
    uint32_t c;
    if (kind == SHK_NE)
        for (c = 0; c < SH_NE_CH; c++)
            if (shell_ne_on[c] && shell_ne_part[c] == part)
                sh_ne_stop(c);
    if (kind == SHK_ALIVE) {                            /* its note goes off now; the ISR may be between steps */
        fm1_irq_off();
        al_off(&trk[part]);
        fm1_irq_on();
    }
}

/* the lanes, stopped: lane l once on the lanes' part, as its hit plays (seq.c seq_step): its note at 96, let go after
 * a step's GATE by seq_tick. Playing, the pattern is the audition; REC held (ERASE) or counting in, nothing */
static void sh_lane_hear(uint32_t l)
{
    track_t *t = &trk[SH_NOTES[sh.note].part % NTRK];
    uint32_t n = (uint32_t)clamp((int32_t)shell_lane_note_i[sh_li % SH_NLI][l % 8u] + 12 * sh_lo(), 0, 127);
    if (song.playing || cin_left || sh_held(B_REC))
        return;
    fm1_irq_off();
    seq_release(t);                                     /* (the one before, if it still sounds) */
    trk_note_on(t, n, 96u);
    t->seq_notes[0] = (uint8_t)n;
    t->seq_n = 1;
    t->seq_off = div_samples((uint32_t)t->p[P_SDIV]) * (uint32_t)t->p[P_SGATE] / 128u;
    fm1_irq_on();
}
/* a key the instrument in front owns, pressed */
static void sh_pg_keys(void)                            /* KEYS' and CHORD's SEQ page: every key the shell's (steps) */
{
    uint32_t kind = sh_kind();
    if (kind != SHK_ENGINE && kind != SHK_CHORD)
        return;
    shell_kb_skip = sh.page[sh.mode] == SHP_SEQ && !sh.global && !sh.wheel ? SH_ALL_KEYS :
                    kind == SHK_CHORD ? (sh_b3.down ? SH_ALL_KEYS : CB_KEYS) : 0u;
}

/* hold a step to edit it (fundamentals.md), where the white keys are steps and nothing else is held: the lanes
 * (RUDIMENT, LINES) and KEYS' and CHORD's SEQ page. A tap is the step on or off, on its release; held, the step is the
 * one being edited: C#5 ACCENT, D#5 SLIDE, F#5 TIE (each a toggle; on the lanes ACCENT is every hit's in the column);
 * on the lanes a black key its lane's hit there (a column of hits in one hold); on KEYS a black key the scale's degree
 * 1..8, OCT an octave, SELECT a semitone. SEQ held + a white key is as before */
static struct { uint8_t on, key, used, mode, note, page; uint16_t w; } sh_stp;   /* the step held, and where */
static int sh_notes_kind(void) { return sh_kind() == SHK_ENGINE || sh_kind() == SHK_ACID; }   /* steps of notes */
static int sh_hold_view(void)
{
    if (sh.wheel || sh.global || sh_held(B_REC) || sh_held(B_GLO) || sh_held(B_HOME) || sh_fx_on())
        return 0;
    if (sh_kind() == SHK_ACID)                          /* ACID: stopped (not in pitch mode), or SEQ held as it plays */
        return song.playing ? sh_held(B_SEQ) : !sh_acid_entry();
    if (sh_held(B_SEQ))
        return 0;
    return sh_kind() == SHK_RUDIMENT ||
           ((sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD) && sh.page[sh.mode] == SHP_SEQ);
}
static int sh_keys_steps(void)                          /* the white keys are steps now (SEQ lit) */
{
    return sh_hold_view() || (sh_held(B_SEQ) && !sh.global && !sh.wheel &&
                              (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD));
}
static void sh_hold_say(const track_t *t)               /* the held step, its note and marks */
{
    static char b[32];
    const step_t *st = &t->step[sh_stp.w % NSTEP];
    str_cpy(b, "STEP ", 6);
    fmt_int(b + str_len(b), sh_stp.w + 1);
    if (sh_kind() != SHK_RUDIMENT && st->n && st->time != ST_TIE) {
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), N_NOTE[st->note[0] % 12u], 3);
        fmt_int(b + str_len(b), st->note[0] / 12 - 1);
    }
    if (sh_kind() == SHK_RUDIMENT ? step_accents(st) != 0u : (st->flags & SF_ACCENT) != 0u)
        str_cpy(b + str_len(b), " ACC", 5);
    if (st->flags & SF_SLIDE)
        str_cpy(b + str_len(b), " SLD", 5);
    if (st->time == ST_TIE)
        str_cpy(b + str_len(b), " TIE", 5);
    sh_toast(b, 0);
}
static void sh_lines_kept(void);
/* a tap on lane l's step w: on in the octave in view; on in another, it comes to this one; on in this one, off */
static void sh_lo_tap(track_t *t, uint32_t w, uint32_t l)
{
    step_t *st = &t->step[w % NSTEP];
    if ((step_lanes(st) >> l) & 1u && step_loct(st, l) != sh_lo()) {
        step_set_loct(st, l, sh_lo());
        return;
    }
    sh_grid_o = (int8_t)sh_lo();
    sh_grid_hit(t, w, l, 2u);
    sh_grid_o = 0;
}
static void sh_hold_tap(track_t *t, uint32_t w)         /* a tap: the step on or off */
{
    if (sh_kind() == SHK_RUDIMENT) {
        uint32_t l = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u;
        if (sh_lane_logic(l)) {
            sh_toast("LOGIC LANE: ITS HITS COME FROM ABOVE", 0);   /* (FILL right of 0 gives it steps again) */
            return;
        }
        sh_lines_kept();
        sh_lo_tap(t, w, l);
        if ((step_lanes(&t->step[w % NSTEP]) >> l) & 1u)
            sh_lane_hear(l);                            /* (a hit turned on is heard; off, silent) */
    } else {
        sh_lines_kept();
        sh_note_step(t, w);
    }
    if (!t->step[w % NSTEP].n && !step_lanes(&t->step[w % NSTEP]))
        motion_clear_locks(t, w % NSTEP);               /* (an empty step keeps no locks) */
}
static void sh_hold_mark(track_t *t, uint32_t m)        /* the held step's ACCENT (0), SLIDE (1), TIE (2) */
{
    step_t *st = &t->step[sh_stp.w % NSTEP];
    uint32_t l, on;
    sh_lines_kept();
    if (sh_kind() == SHK_RUDIMENT)
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
    fm1_irq_off();
    if (m == 0u && sh_kind() == SHK_RUDIMENT) {
        on = !step_accents(st);
        for (l = 0; l < 8u; l++)
            if ((step_lanes(st) >> l) & 1u)
                grid_acc(t, sh_stp.w, l, on);
    } else if (m == 0u)
        st->flags ^= SF_ACCENT;
    else if (m == 1u)
        st->flags ^= SF_SLIDE;
    else if (st->time == ST_TIE)
        st->time = (uint8_t)(st->n || step_lanes(st) ? ST_NOTE : ST_REST);
    else
        st->time = ST_TIE;
    fm1_irq_on();
}
static void sh_hold_pitch(track_t *t, int32_t d, int32_t semi)   /* KEYS: the held step to degree d (-1: as it is),
                                                                  * then semi semitones */
{
    step_t *st = &t->step[sh_stp.w % NSTEP];
    uint32_t mask = scale_mask(t), pcs[12], n = 0, i, root = (uint32_t)t->p[P_ROOT] % 12u;
    int32_t note = st->n ? st->note[0] : sh_kind() == SHK_ACID ? sh_acid_last : (last_note ? last_note : 60), base;
    if (mask == 0xFFFu || !mask)
        mask = SCALE_MASK[1];                           /* (CHR: MAJOR's degrees) */
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            pcs[n++] = i;
    base = note - (note + 120 - (int32_t)root) % 12;   /* the root at or below the note */
    if (d >= 0)
        note = base + (int32_t)pcs[(uint32_t)d % n] + 12 * (d / (int32_t)n);
    note = clamp(note + semi, 0, 127);
    fm1_irq_off();
    if (!st->n) {
        step_just_clear(st);
        st->note[0] = (uint8_t)note;
        st->time = ST_NOTE;
        st->n = 1;                                      /* (n last: the ISR plays a step once n is set) */
    } else
        st->note[0] = (uint8_t)note;
    fm1_irq_on();
    if (sh_kind() == SHK_ACID)
        sh_acid_last = (uint8_t)note;
    if (!song.playing)
        sh_acid_hear(t, (uint32_t)note);                /* (stopped: each note set is heard) */
}
static void sh_hold_down(uint32_t k, uint32_t w)        /* a white key down where steps are: its step held */
{
    if (sh_stp.on && !sh_stp.used)
        sh_hold_tap(sh_trk(), sh_stp.w);               /* (another key while one waited: that one was a tap) */
    sh_stp.on = 1;
    sh_stp.key = (uint8_t)k;
    sh_stp.used = 0;
    sh_stp.mode = sh.mode;
    sh_stp.note = sh.note;
    sh_stp.page = sh.page[sh.mode];
    sh_stp.w = (uint16_t)((sh_pg() + w) % NSTEP);
}
static void sh_hold_poll(void)                          /* every frame: the held key let go is a tap if nothing used it */
{
    if (sh_stp.on && (sh.mode != sh_stp.mode || sh.note != sh_stp.note || sh.wheel || sh.global)) {
        sh_stp.on = 0;                                  /* (another pair: the step held is let go, no tap) */
        return;
    }
    if (sh_stp.on && sh.page[sh.mode] != sh_stp.page) { /* another page, the key still down: the step stays held, */
        sh_stp.page = sh.page[sh.mode];                 /* so a SOUND page's knobs lock on it; never a tap */
        sh_stp.used = 1;
    }
    if (!sh_stp.on || (fm1_in.notes >> sh_stp.key) & 1u)
        return;
    sh_stp.on = 0;
    if (!sh_stp.used)
        sh_hold_tap(sh_trk(), sh_stp.w);
}
static int sh_hold_key(uint32_t k)                      /* a key while a step is held: 1, it edited the step */
{
    track_t *t = sh_trk();
    int32_t b = sh_key_black(k);
    if (!sh_stp.on || k == sh_stp.key || sh_key_white(k) >= 0)
        return 0;
    if (k == SH_PG_LEFT || k == SH_PG_RIGHT || k == SH_PG_FOLLOW)
        sh_hold_mark(t, k == SH_PG_LEFT ? 0u : k == SH_PG_RIGHT ? 1u : 2u);
    else if (b >= 0 && b < 8 && sh_kind() == SHK_RUDIMENT && sh_lane_logic((uint32_t)b)) {
        sh_toast("LOGIC LANE: ITS HITS COME FROM ABOVE", 0);   /* (its bits play nothing) */
        sh_stp.used = 1;
        return 1;
    } else if (b >= 0 && b < 8 && sh_kind() == SHK_RUDIMENT) {
        sh_lines_kept();
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
        fm1_irq_off();
        sh_lo_tap(t, sh_stp.w, (uint32_t)b);
        fm1_irq_on();
        if ((step_lanes(&t->step[sh_stp.w]) >> b) & 1u)
            sh_lane_hear((uint32_t)b);
    } else if (b >= 0 && b < 8 && sh_notes_kind()) {
        sh_lines_kept();
        sh_hold_pitch(t, b, 0);
    }
    else
        return 0;
    sh_stp.used = 1;
    sh_hold_say(t);
    return 1;
}
static int sh_hold_move(int32_t oct, int32_t semi)      /* OCT or SELECT while a KEYS step is held: 1, it moved */
{
    track_t *t = sh_trk();
    if (!sh_stp.on || !sh_notes_kind() || !t->step[sh_stp.w].n)
        return 0;
    sh_lines_kept();
    sh_hold_pitch(t, -1, 12 * oct + semi);
    sh_stp.used = 1;
    sh_hold_say(t);
    return 1;
}
/* a step held, the knobs edit it. On a page of the SOUND (EDIT ENV LFO FX, a SOUND's HOME) a knob locks its value on the
 * step (motion.c MOTION_LOCK): it sounds on that step only, and turned back to the sound's own value the lock goes.
 * On any other page: KNOB 1 CHNC, the chance it plays, then the conditions 1:2 .. 4:4 (loop a of every b); 2 RATCH,
 * x1..x4 in the step; 3 NUDGE, -8..+7 sixteenths of the step late or early; 4 VEL. The cards show the held step's
 * values (sh_step_card). FILL and !FILL play with SCL held (sh_fill_poll) */
#define SH_NCOND NCOND
static const char *const SH_COND[SH_NCOND] = {"1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL",
                                              "!FILL"};
static const char *const SH_STEP_L[4] = {"CHNC", "RATCH", "NUDGE", "VEL"};
/* a lane's own byte (seq.c shell_lstep) seen as a step's CHNC and RATCH: on RUDIMENT and LINES the held step's KNOB 1
 * and 2, its cards and its marks are the lane in front's */
static uint32_t sh_ls_v(uint32_t b)                     /* the byte's CHNC as step_chance_v */
{
    uint32_t c = b & 63u;
    return !c ? 100u : c <= 20u ? 100u - 5u * c : 100u + (c - 20u);
}
static uint8_t sh_ls_b(uint32_t v, uint32_t r)          /* .. and back, with RATCH r */
{
    uint32_t c = v >= 100u ? v - 100u + (v > 100u ? 20u : 0u) : (100u - v + 4u) / 5u;
    return (uint8_t)((c > 31u ? 31u : c) | ((r - 1u) & 3u) << 6);
}
static uint8_t *sh_ls_at(uint32_t w) { return &shell_lstep[sh_li % 2u][sh_sp[SP_LANE - SP_FIRST] % 8u][w % NSTEP]; }
static const step_t *sh_ls_view(step_t *x, const step_t *st, uint32_t w)   /* st with the lane's CHNC and RATCH */
{
    uint32_t b = *sh_ls_at(w);
    *x = *st;
    step_set_chance_v(x, sh_ls_v(b));
    step_set_ratchet(x, (b >> 6) + 1u);
    return x;
}
static int sh_step_on(void)                             /* a step held whose knobs edit it */
{
    return sh_stp.on && !sh.global && !sh.wheel && !sh_fx_on();
}
static int sh_step_fam(void)                            /* the page's family, for a held step: 1 a SOUND page (its
                                                         * knobs lock; SOUND MAIN too), 2 the mix (DELAY REVERB CHORUS:
                                                         * the knobs as ever), 0 the NOTE view (the step's four) */
{
    uint8_t ids[4];
    int glob;
    if (sh_page_note(sh.page[sh.mode]))
        return 0;
    sh_page_ids(ids, &glob);
    return glob ? 2 : 1;
}
#define sh_step_sound() (sh_step_fam() == 1)
static void sh_step_val(const step_t *st, uint32_t k, char val[12])
{
    uint32_t c = step_chance_v(st);
    int32_t n = step_nudge(st);
    val[0] = 0;
    if (k == 0u && c > 100u)
        str_cpy(val, SH_COND[(c - 101u) % SH_NCOND], 12u);
    else if (k == 0u) {
        fmt_int(val, (int32_t)c);
        str_cpy(val + str_len(val), "%", 2);
    } else if (k == 1u) {
        str_cpy(val, "x", 2);
        fmt_int(val + 1, (int32_t)step_ratchet(st));
    } else if (k == 2u) {
        if (n > 0)
            str_cpy(val, "+", 2);
        fmt_int(val + str_len(val), n);
    } else
        fmt_int(val, st->vel ? st->vel : 96);
}
static int sh_step_knob(uint32_t k, int32_t s)          /* 1: knob k edited the held step */
{
    track_t *t = sh_trk();
    step_t *st, lx;
    uint32_t w = sh_stp.w % NSTEP;
    if (!sh_step_on() || sh_step_fam() == 2)
        return 0;
    st = &t->step[w];
    sh_stp.used = 1;
    sh.hot = (uint8_t)k;
    sh.hot_t = fm1_ms | 1u;
    if (sh_fmop_page()) {                               /* (FM6's operators: the patch's bytes, no P_ id to lock) */
        sh_toast("NO LOCKS ON THE OPERATORS", 0);
        return 1;
    }
    if (sh_step_sound()) {                              /* a lock */
        uint8_t ids[4];
        int glob;
        int16_t *vp, base, v;
        const param_desc_t *d;
        int r;
        sh_page_ids(ids, &glob);
        if (ids[k] == SH_NONE)
            return 1;
        if (ids[k] >= SP_FIRST) {                       /* (a setting of the shell's own: no lock) */
            sh_toast("NO LOCK HERE", 0);
            return 1;
        }
        d = sh_desc(ids[k], 0, &vp);
        if (!motion_param(ids[k])) {
            sh_toast(d->label, ": NO LOCKS");
            return 1;
        }
        base = motion_base_value(t, ids[k]);
        if (!motion_lock_get(t, w, ids[k], &v))
            v = base;
        v = (int16_t)clamp(param_turn(d, v, s), d->min > -64 ? d->min : -64, d->max < 127 ? d->max : 127);
        sh_lines_kept();
        if (v == base) {
            motion_delete_event(t, w, ids[k]);
            sh_toast(d->label, " LOCK OFF");
            return 1;
        }
        r = motion_set_lock(t, w, ids[k], v);
        if (r == 2)
            sh_toast("LOCKS FULL", 0);
        return 1;
    }
    if (!st->n && !step_lanes(st)) {
        sh_toast("AN EMPTY STEP", 0);
        return 1;
    }
    sh_lines_kept();
    if (sh_kind() == SHK_RUDIMENT)
        sh_beat_keep(t, sh_sp[SP_RHY - SP_FIRST], 1);
    if (sh_kind() == SHK_RUDIMENT && k < 2u)            /* the lanes: CHNC and RATCH the lane in front's own */
        st = (step_t *)sh_ls_view(&lx, st, w);
    fm1_irq_off();
    if (k == 0u) {                                      /* 5 % a detent, then the conditions one by one */
        int32_t c = (int32_t)step_chance_v(st);
        c = c < 100 || (c == 100 && s < 0) ? clamp((c + 4) / 5 * 5 + 5 * s, 0, 100) : c + s;
        step_set_chance_v(st, (uint32_t)clamp(c, 0, 100 + (int32_t)SH_NCOND));
    }
    else if (k == 1u)
        step_set_ratchet(st, (uint32_t)clamp((int32_t)step_ratchet(st) + (s > 0 ? 1 : -1), 1, 4));
    else if (k == 2u)
        step_set_nudge(st, step_nudge(st) + (s > 0 ? 1 : -1));
    else
        st->vel = (uint8_t)clamp((st->vel ? st->vel : 96) + 4 * s, 1, 127);
    if (st == &lx)
        *sh_ls_at(w) = sh_ls_b(step_chance_v(&lx), step_ratchet(&lx));
    fm1_irq_on();
    if (k == 2u && step_nudge(st) && step_ratchet(st) > 1u)
        sh_toast("NUDGE WAITS: A RATCHET PLAYS ON TIME", 0);
    else if (k == 1u && step_nudge(st) && step_ratchet(st) > 1u)
        sh_toast("RATCH PLAYS ON TIME: NUDGE WAITS", 0);
    return 1;
}
/* the card of knob k while a step is held: 1 and its label, value and fill (0..256); a lock's label is starred */
static int sh_step_card(uint32_t k, const char **label, char val[12], int32_t *frac)
{
    static char lb[4][10];
    track_t *t = sh_trk();
    const step_t *st;
    step_t lx;
    uint32_t w = sh_stp.w % NSTEP;
    if (!sh_step_on() || sh_fmop_page() || sh_step_fam() == 2)
        return 0;
    st = &t->step[w];
    if (sh_step_sound()) {
        uint8_t ids[4];
        int glob;
        int16_t *vp, v;
        const param_desc_t *d;
        const char *unit = 0;
        uint32_t n;
        int on;
        sh_page_ids(ids, &glob);
        if (ids[k] == SH_NONE || ids[k] >= SP_FIRST)
            return 0;
        d = sh_desc(ids[k], 0, &vp);
        on = motion_lock_get(t, w, ids[k], &v);
        if (!on)
            v = motion_base_value(t, ids[k]);
        lb[k][0] = on ? '*' : 0;
        lb[k][1] = 0;
        str_cpy(lb[k] + str_len(lb[k]), d->label, sizeof lb[k] - 1u);
        *label = lb[k];
        param_format(d, v, val, &unit);
        if (unit && unit[0] && (n = str_len(val)) < 8u)
            str_cpy(val + n, unit, 12u - n);
        *frac = d->max > d->min ? (v - d->min) * 256 / (d->max - d->min) : 0;
        return 1;
    }
    *label = SH_STEP_L[k];
    if (sh_kind() == SHK_RUDIMENT && k < 2u)            /* (the lane in front's own CHNC and RATCH) */
        st = sh_ls_view(&lx, st, w);
    sh_step_val(st, k, val);
    *frac = k == 0u ? (int32_t)(step_chance_v(st) > 100u ? 256u : step_chance_v(st) * 256u / 100u) :
            k == 1u ? (int32_t)(step_ratchet(st) - 1u) * 256 / 3 : k == 2u ? (step_nudge(st) + 8) * 256 / 15 :
            (st->vel ? st->vel : 96) * 256 / 127;
    return 1;
}
static void sh_inst_key(uint32_t k)
{
    track_t *t = sh_trk();
    int32_t w = sh_key_white(k), b = sh_key_black(k);
    if (sh_hold_key(k))                                 /* a step held: the key edits it */
        return;
    if (w >= 0 && sh_hold_view()) {                     /* a white key where steps are: held, or a tap on release */
        sh_hold_down(k, (uint32_t)w);
        return;
    }
    if (sh_acid_keys(k))                                /* ACID: pitch mode, or playing, its transposition */
        return;
    if ((k == SH_PG_LEFT || k == SH_PG_RIGHT) && sh_pg_free()) {   /* nothing held: the step pages (sh_pg) */
        sh_pg_step(k == SH_PG_LEFT ? -1 : 1);
        return;
    }
    if (k == SH_PG_FOLLOW && sh_pg_free() && !sh_held(B_SEQ)) {   /* .. and FOLLOW */
        ui_prefs2 = (uint8_t)(ui_prefs2 ^ SH_PREF_FOLLOW);
        settings_later();
        sh_toast(ui_prefs2 & SH_PREF_FOLLOW ? "FOLLOW ON" : "FOLLOW OFF", 0);
        return;
    }
    switch (sh_kind()) {
    case SHK_RUDIMENT:                                  /* white keys: steps (sh_stp, above); black: the lanes */
        if (b >= 0 && b < 8) {
            sh_sp[SP_LANE - SP_FIRST] = (int16_t)b;
            sh_lane_hear((uint32_t)b);
        }
        break;
    case SHK_CHORD:                                     /* chord.c reads the block's keys; B3 is sh_chord_poll's */
        if (sh_b3.down && k >= CB_ROOT0) {              /* B3 held, a root key: that note added to every chord, the */
            uint32_t pc = (5u + k) % 12u;               /* same note in each (chord.c puts it over the root); key 0 is F */
            shell_chord_add ^= 1u << pc;
            sh_b3.used = 1;
            sh.ring = 0;
            sh_toast((shell_chord_add >> pc) & 1u ? "ADDED " : "TAKEN OUT ", N_NOTE[pc]);
        }
        break;
    case SHK_ALIVE:                                     /* a key flips the cells under it in the newest row */
        fm1_irq_off();
        al_put(al.head, (k * (AL_W - 1u)) / 26u, al_cell(al.head, (k * (AL_W - 1u)) / 26u) ^ 1u);
        al.gen++;
        fm1_irq_on();
        break;
    default:
        break;
    }
}

static uint32_t sh_inst_sig(void)
{
    const track_t *t = sh_trk();
    uint32_t h = 0, i;
    switch (sh_kind()) {
    case SHK_CHORD:
        return chord_last[sh.part].gen;
    case SHK_RUDIMENT:
        for (i = 0; i < NSTEP; i++)                     /* (every page: an edit on page 2 redraws too) */
            h = h * 31u + t->step[i].hit;
        return h ^ (song.playing ? (uint32_t)t->seq_idx * 7919u : 0u);
    case SHK_ALIVE:
        return al.gen;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------------------------------------- lights --- */
/* the LED ladder on the hardware's levels: LIT is fm1_led, MID fm1_led_mid (lit 1 frame in 8), LOW fm1_led_dim (the
 * glow), and the one motion, breathing (fm1_led_breath), means waiting for you. Only REC armed blinks. */
enum { SHL_OFF, SHL_LOW, SHL_MID, SHL_LIT, SHL_BREATH };
static void sh_led(uint8_t *L[4], uint32_t id, uint32_t lv)
{
    if (lv == SHL_LIT)
        led_put(L[0], id, 1);
    else if (lv == SHL_MID)
        led_put(L[1], id, 1);
    else if (lv == SHL_LOW)
        led_put(L[2], id, 1);
    else if (lv == SHL_BREATH)
        led_put(L[3], id, 1);
}

/* what step i of part t holds besides its notes (a held step's knobs, sequencing.md): SHX_CHNC a chance or a
 * condition, SHX_RATCH x2..x4, SHX_NUDGE early or late, SHX_LOCK a parameter lock. locks: motion_lock_steps of the part */
enum { SHX_CHNC = 1, SHX_RATCH = 2, SHX_NUDGE = 4, SHX_LOCK = 8 };
static void sh_step_marks(int32_t x, int32_t y, uint32_t e, uint16_t c);
static uint32_t sh_step_extras(const track_t *t, uint32_t i, uint64_t locks)
{
    const step_t *st = &t->step[i % NSTEP];
    step_t lx;
    if (lanes_of(t) >= 0)                               /* (a lanes engine: the lane in front's own) */
        st = sh_ls_view(&lx, st, i);
    return (st->probability ? SHX_CHNC : 0u) | (step_ratchet(st) > 1u ? SHX_RATCH : 0u) |
           (step_nudge(st) ? SHX_NUDGE : 0u) | ((locks >> (i % NSTEP)) & 1u ? SHX_LOCK : 0u);
}

/* the lanes' keys while it plays: each hit in the octave in view lifts its lane's glow, which falls away over ~0.7 s,
 * so a lane that hits often stays up and one that hits now and then blinks. Polled each frame (sh_lane_now moving
 * on); a hit in another octave pulses OCT+ or OCT- instead */
#define SH_LG_HIT 110u                                  /* a hit's lift, of 255 */
#define SH_LG_ON 40u                                    /* above this the key is mid */
static uint8_t sh_lg[8], sh_lg_at[8];
static uint32_t sh_lg_t, sh_lg_up, sh_lg_dn;           /* the last poll; the last hit above, below the octave in view */
static int sh_lane_napping(const track_t *t, uint32_t l);
static void sh_lane_glow(void)
{
    const track_t *t = sh_trk();
    uint32_t l, dt = fm1_ms - sh_lg_t, d;
    sh_lg_t = fm1_ms;
    d = dt * 255u / 700u;
    for (l = 0; l < 8u; l++) {
        uint32_t now, hit;
        sh_lg[l] = (uint8_t)(sh_lg[l] > d ? sh_lg[l] - d : 0u);
        if (sh_kind() != SHK_RUDIMENT || !song.playing) {
            sh_lg[l] = 0;
            sh_lg_at[l] = 0xFF;
            continue;
        }
        now = sh_lane_now(l);
        if (now == sh_lg_at[l])
            continue;
        sh_lg_at[l] = (uint8_t)now;
        hit = sh_lane_logic(l) ? (sh_lc_hit[now] >> l) & 1u : ((step_lanes(&t->step[now]) >> l) & 1u) || sh_map_at(l, now);
        if (!hit || sh_lane_napping(t, l))
            continue;
        if (!sh_lane_logic(l) && step_loct(&t->step[now], l) > sh_lo())
            sh_lg_up = fm1_ms | 1u;
        else if (!sh_lane_logic(l) && step_loct(&t->step[now], l) < sh_lo())
            sh_lg_dn = fm1_ms | 1u;
        else
            sh_lg[l] = (uint8_t)(sh_lg[l] + SH_LG_HIT > 255u ? 255u : sh_lg[l] + SH_LG_HIT);
    }
}

/* key k's level for the instrument in front (lv: the engine modes' default) */
static uint32_t sh_inst_led(uint32_t k, uint32_t lv)
{
    const track_t *t = sh_trk();
    int32_t w = sh_key_white(k), b = sh_key_black(k);
    switch (sh_kind()) {
    case SHK_CHORD:                                     /* the block: qualities mid, extensions low, LATCH breathing
                                                         * while on; the roots: the key's tonic and its fifth low */
        if (k == CB_LATCH)
            return shell_chord_latch ? SHL_BREATH : SHL_LOW;
        if (k < CB_ROOT0)
            return b >= 0 ? SHL_MID : SHL_LOW;
        {
            int32_t d = ((int32_t)k + 17 - t->p[P_ROOT]) % 12;   /* key 0 is F */
            return d == 0 || d == 7 ? SHL_LOW : SHL_OFF;
        }
    case SHK_RUDIMENT: {                                /* the lane's steps lit; the playhead inverts; the lanes with
                                                         * hits mid, the empty ones low. The lane in front empty while
                                                         * others play: their hits low in place of the beats */
        uint32_t lane = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, len = sh_lane_len(lane), used = 0, i;
        for (i = 0; i < NSTEP; i++)
            used |= step_lanes(&t->step[i]);
        if (shell_map_on && !sh_li)                     /* (MAP: its lanes play) */
            used |= 0x0Bu;
        if (w >= 0) {
            uint32_t on, ph;
            w += (int32_t)sh_pg();                      /* the step on the page in front */
            on = (step_lanes(&t->step[w]) >> lane) & 1u;
            if (on && sh_lo_on() && step_loct(&t->step[w], lane) != sh_lo() && (uint32_t)w < len)
                return SHL_LOW;                         /* (a hit in another octave: low) */
            ph = song.playing && (uint32_t)w == sh_lane_now(lane);
            if (sh_lane_logic(lane))                    /* a logic lane: what its op plays, no playhead of its own */
                return (sh_lc_hit[w] >> lane) & 1u ? SHL_MID : SHL_OFF;
            if ((uint32_t)w >= len)
                return SHL_OFF;
            if (ph)
                return on || sh_map_at(lane, (uint32_t)w) ? SHL_LOW : SHL_LIT;
            if (!on && sh_map_at(lane, (uint32_t)w))   /* the map's hit: faint, yours bright */
                return SHL_LOW;
            if (!((used >> lane) & 1u) && used)
                return step_lanes(&t->step[w]) ? SHL_LOW : SHL_OFF;
            if (on && sh_step_extras(t, (uint32_t)w, motion_lock_steps(trk_index(t))))
                return SHL_MID;                         /* a hit with a CHNC, RATCH, NUDGE or a lock: mid */
            return on ? SHL_LIT : (w % 4 == 0 ? SHL_LOW : SHL_OFF);
        }
        if (b >= 0 && b < 8 && song.playing && (uint32_t)b != lane)   /* playing: the lanes glow as they hit */
            return sh_lg[b] > SH_LG_ON ? SHL_MID : SHL_LOW;
        return b >= 0 && b < 8 ? ((uint32_t)b == lane ? SHL_LIT : (used >> b) & 1u ? SHL_MID : SHL_LOW) : SHL_OFF;
    }
    case SHK_ALIVE:                                     /* the newest row under the keys */
        return al_cell(al.head, (k * (AL_W - 1u)) / 26u) ? SHL_LIT : (k == 13u ? SHL_LOW : SHL_OFF);
    default:
        return lv;
    }
}

/* SEQ: white key k as the part's step */
static uint32_t sh_step_led(uint32_t k)
{
    const track_t *t = sh_trk();
    int32_t w = sh_key_white(k);
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u, on, ph;
    if (w < 0 || w >= 16)
        return SHL_OFF;
    w += (int32_t)sh_pg();                              /* the step on the page in front */
    if ((uint32_t)w >= len)
        return SHL_OFF;
    on = t->step[w].n != 0u;
    ph = song.playing && (uint32_t)w == t->seq_idx % len;
    if (ph)
        return on ? SHL_LOW : SHL_LIT;
    if (on && sh_step_extras(t, (uint32_t)w, motion_lock_steps(trk_index(t))))
        return SHL_MID;                                 /* a step with a CHNC, RATCH, NUDGE or a lock: mid */
    return on ? SHL_LIT : (w % 4 == 0 ? SHL_LOW : SHL_OFF);
}

/* a wheel key's level: picked lit, filled mid; playing, inverted on its beat; muted, breathing */
static uint32_t sh_wheel_led(int picked, uint32_t bit, uint32_t beat, uint32_t mute)
{
    if ((beat >> bit) & 1u)
        return picked ? SHL_OFF : SHL_LIT;
    if (((mute >> bit) & 1u) && !picked)
        return SHL_BREATH;
    return picked ? SHL_LIT : SHL_MID;
}

static uint32_t sh_pg_led(uint32_t k)                   /* a page key's level: a page that way glows, the playhead's
                                                         * way pulses on the beat */
{
    uint32_t v = sh_pg() / 16u, n = sh_pg_n(), at = sh_pg_now() / 16u, back = k == SH_PG_LEFT;
    if (back ? !v : v + 1u >= n)
        return SHL_OFF;
    if (song.playing && (back ? at < v : at > v) && sh_trk()->seq_idx % 4u == 0u)
        return SHL_LIT;
    return SHL_LOW;
}

static uint32_t sh_peek_ms(void);
static void sh_sq_at(uint32_t t, int32_t *x, int32_t *y, uint32_t *bounces, uint32_t *since);
static void sh_led_off(uint8_t *L[4], uint32_t id)     /* dark, whatever else lit it */
{
    uint8_t q = led_pos[id], i;
    if (q != 0xFF)
        for (i = 0; i < 4u; i++)
            L[i][q >> 3] &= (uint8_t)~(1u << (q & 7u));
}
/* a moment's level at a ms in (SHL_OFF: dark now; SHL_BREATH: over, the light as it was) */
static uint32_t sh_lfx_lv(uint32_t kind, uint32_t a)
{
    if (kind == LFX_DIP)
        return a < 45u ? SHL_OFF : SHL_BREATH;
    if (kind == LFX_BLINK)
        return a < 60u ? SHL_OFF : a < 140u ? SHL_LIT : a < 200u ? SHL_OFF : SHL_BREATH;
    return a < 110u ? SHL_LIT : a < 380u ? SHL_MID : SHL_BREATH;
}
static const uint8_t SH_KEY_X[27] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19,   /* (FM1_ANIM_X: */
                                     20, 22, 23, 24, 25, 26, 28, 29, 30};   /* each key's place, half white keys) */
/* the keys as a strip, in 1/16 half white keys (SH_KEY_X), lit round a place hx: on it at lv, about it at LOW
 * (w its half width); dir: only behind it, a trail (dir -1, 1), else both sides */
static void sh_keys_glow(uint8_t *L[4], int32_t hx, int32_t w, uint32_t lv, int32_t dir)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        int32_t d = hx - 16 * (int32_t)SH_KEY_X[k], e = d < 0 ? -d : d;
        if (e < 12)
            sh_led(L, 14u + k, lv);
        else if (e < w && (!dir || d * dir > 0))
            sh_led(L, 14u + k, dir && e < w / 2 ? SHL_MID : SHL_LOW);
    }
}

static void shell_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nm[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint8_t *L[4] = {nl, nm, nd, nb};
    static uint8_t ready;
    static uint32_t held;                               /* (the keys down last frame: their trails) */
    uint32_t b, k, c, p = sh.page[sh.mode], pk = sh_peek_ms(), since = 0xFFFFFFFFu;
    int32_t s, hx = 0, lo;
    if (pk) {                                           /* the peek: sqrl over the keys; stopped it has the LEDs, the idle
                                                         * animation's drift gives way */
        int32_t sy;
        uint32_t t = pk - 1u, bn;
        sh_sq_at(t - t % 40u, &s, &sy, &bn, &since);
        hx = (s + BR_SQRL52_W / 2) * 2;                 /* (240 px over 30 half white keys) */
        if (fm1_led_idle_on())
            fm1_led_anim_stop();
    }
    if ((!pk && idle_leds()) || fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < 27u; k++)                           /* a key let go: when, for its trail */
        if ((held >> k) & 1u && !((fm1_in.notes >> k) & 1u))
            sh_key_up[k] = fm1_ms ? fm1_ms : 1u;
    held = fm1_in.notes;
    sh_led_pg = 0xFF;
    sh_lane_glow();
    for (b = 0; b < NB; b++)                            /* landmarks: every button at rest */
        sh_led(L, panel.btn[b], SHL_LOW);
    if (pk && !song.playing) {                          /* sqrl's light under it, mid on its middle */
        sh_keys_glow(L, hx, BR_SQRL52_W, SHL_MID, 0);
    } else if (sh.wheel) {
        uint32_t on, beat, mute;                        /* the pairs that play: their keys flip on each beat (the
                                                         * playhead's way); the muted breathe */
        sh_part_keys(&on, &beat, &mute);
        (void)on;
        for (s = 0; s < (int32_t)SH_SLOTS; s++)
            if (sh_wslot[s] != SH_EMPTY || s == sh.wsel)
                sh_led(L, 14u + (uint32_t)sh_slot_key((uint32_t)s), sh_wheel_led(s == sh.wsel, (uint32_t)s, beat, mute));
        for (s = 0; s < 5; s++)                         /* the NOTE engines, on the black keys */
            if (sh_wnoteslot[s] < SH_NNOTES)
                sh_led(L, 14u + (uint32_t)sh_note_key((uint32_t)s),
                       sh_wheel_led(s == sh.wnote, 8u + (uint32_t)s, beat, mute));
        sh_led(L, panel.btn[B_HOME], SHL_LIT);
        sh_led(L, panel.btn[B_OCTDN], SHL_LIT);
        sh_led_pg = (uint8_t)panel.btn[B_HOME];
    } else if (sh_held(B_GLO)) {
        for (s = 0; s < (int32_t)SH_SLOTS; s++)
            if (sh_wslot[s] != SH_EMPTY)
                sh_led(L, 14u + (uint32_t)sh_slot_key((uint32_t)s), sh_wslot[s] == sh.mode ? SHL_LIT : SHL_MID);
        for (s = 0; s < 5; s++)
            if (sh_wnoteslot[s] < SH_NNOTES)
                sh_led(L, 14u + (uint32_t)sh_note_key((uint32_t)s), sh_wnoteslot[s] == sh.note ? SHL_LIT : SHL_MID);
        sh_led(L, panel.btn[B_GLO], SHL_LIT);
        sh_led_pg = (uint8_t)panel.btn[B_GLO];
    } else {
        sh_led(L, panel.btn[B_EDIT], SHL_MID);           /* the families this mode has */
        sh_led(L, panel.btn[B_ENV], SHL_MID);
        sh_led(L, panel.btn[B_LFO], SHL_MID);
        sh_led(L, panel.btn[B_FX], SHL_MID);
        sh_led(L, panel.btn[B_ARP], arp_alone(sh_trk()) ? SHL_BREATH :   /* playing by itself; armed (a mode, HOLD */
               sh_trk()->p[P_AMODE] || sh_trk()->p[P_AHOLD] || (shell_chord_latch && sh_kind() == SHK_CHORD) ?
               SHL_MID : SHL_LOW);                                          /* or LATCH waiting on you); off */
        sh_led(L, panel.btn[B_SCL], SHL_MID);
        sh_led(L, panel.btn[B_GLO], SHL_MID);
        if (sh.global == SHG_SCALE || sh.global == SHG_MOVE || sh.global == SHG_HARM)
            sh_led(L, sh_led_pg = (uint8_t)panel.btn[B_SCL], SHL_LIT);
        else if (sh.global == SHG_PAIR) {               /* PAIR, the wheel's sibling: its ways out lit */
            sh_led(L, sh_led_pg = (uint8_t)panel.btn[B_HOME], SHL_LIT);
            sh_led(L, panel.btn[B_OCTDN], SHL_LIT);
        } else if (sh.global >= SHG_SYSTEM)
            sh_led(L, sh_led_pg = (uint8_t)panel.btn[B_GLO], SHL_LIT);
        else                                            /* (SEQ's own pages with no steps under the keys: mid) */
            sh_led(L, sh_led_pg = (uint8_t)panel.btn[SHP_FAM[p]], SHP_FAM[p] == B_SEQ && !sh_keys_steps() ? SHL_MID : SHL_LIT);
        if (sh_keys_steps())                            /* SEQ lit: the white keys are steps, on any page */
            sh_led(L, panel.btn[B_SEQ], SHL_LIT);
        if (sh_mv.on && sh.global != SHG_SCALE && sh.global != SHG_MOVE && sh.global != SHG_HARM)
            sh_led(L, panel.btn[B_SCL], SHL_BREATH);    /* MOVE runs: the scale is moving */
        if (seq_fill)
            sh_led(L, panel.btn[B_SCL], SHL_BREATH);    /* FILL held */
        for (k = 0; k < 27u; k++) {                     /* held keys lit, the Cs as landmarks; VODER: its map */
            uint32_t lv = (k == 7u || k == 19u) ? SHL_LOW : SHL_OFF, note = sh_kind() != SHK_RUDIMENT;   /* (a key
                                                         * that plays: its trail) */
#if FELUCCA_VODER
            if (sh_eng() == ENGI_VODER) {           /* VODER: bands glow, stops mid, HISS breathes */
                int32_t n = voder_keys(sh_trk(), k);
                lv = n >= VODER_NOTE_BAND && n < VODER_NOTE_BAND + VODER_NBAND ? SHL_LOW :
                     n >= VODER_NOTE_STOP && n < VODER_NOTE_HISS ? SHL_MID : n == VODER_NOTE_HISS ? SHL_BREATH : SHL_OFF;
            }
#endif
            lv = sh_inst_led(k, lv);
            if (sh_kind() == SHK_ACID || ((p == SHP_SEQ || sh_held(B_SEQ)) && (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD)))
                lv = sh_step_led(k), note = 0;          /* the pattern: steps lit, beats low, the playhead inverted */
            if (sh_acid_entry() && sh_key_white(k) >= 0 &&
                sh_pg() + (uint32_t)sh_key_white(k) == sh_acid_cur)
                lv = SHL_BREATH;                        /* ACID's pitch mode: the step the next key writes */
            if ((k == SH_PG_LEFT || k == SH_PG_RIGHT) && sh_pg_free())
                lv = sh_pg_led(k), note = 0;            /* another page that way; the playhead's, on the beat */
            if (k == SH_PG_FOLLOW && sh_pg_free() && !sh_held(B_SEQ))
                lv = ui_prefs2 & SH_PREF_FOLLOW ? SHL_MID : SHL_LOW, note = 0;   /* FOLLOW on: mid */
            if (sh_stp.on) {
                note = 0;                            /* a step held: its marks, and on the lanes its hits */
                const step_t *st = &sh_trk()->step[sh_stp.w % NSTEP];
                int32_t bk = sh_key_black(k);
                if (k == SH_PG_LEFT || k == SH_PG_RIGHT || k == SH_PG_FOLLOW)
                    lv = (k == SH_PG_LEFT ? (sh_kind() == SHK_RUDIMENT ? step_accents(st) != 0u : (st->flags & SF_ACCENT) != 0u) :
                          k == SH_PG_RIGHT ? (st->flags & SF_SLIDE) != 0u : st->time == ST_TIE) ? SHL_LIT : SHL_LOW;
                else if (bk >= 0 && bk < 8 && sh_kind() == SHK_RUDIMENT)
                    lv = (step_lanes(st) >> bk) & 1u ? SHL_LIT : SHL_LOW;
                else if ((uint32_t)k == sh_stp.key)
                    lv = SHL_LIT;
            }
            if (sh_key_up[k] && note && lv < SHL_MID) {   /* let go: mid, fading to the glow */
                uint32_t a = fm1_ms - sh_key_up[k];
                if (a < 450u && lv < (a < 140u ? SHL_MID : SHL_LOW))
                    lv = a < 140u ? SHL_MID : SHL_LOW;
                else if (a >= 450u)
                    sh_key_up[k] = 0;
            }
            if (shell_kb_scl) {                         /* SCALE: the scale on the keys, ROOT lit, the rest mid */
                const track_t *st = sh_trk();
                uint32_t rel = (53u + k + 120u - (uint32_t)st->p[P_ROOT] % 12u) % 12u;
                lv = !rel ? SHL_LIT : (scale_mask(st) >> rel) & 1u ? SHL_MID : SHL_OFF;
                note = 1;
            }
            sh_led(L, 14u + k, (fm1_in.notes >> k) & 1u && sh_kind() != SHK_RUDIMENT ? SHL_LIT : lv);
        }
        if (!sh.global)                                 /* HOME, the view: steady SOUND, breathing NOTE; a track */
            sh_led(L, panel.btn[B_HOME], sh_view_note() ? SHL_BREATH : sh_tmute ? SHL_MID : SHL_LIT);   /* muted: mid */
        else if (sh_tmute)                              /* (the wheel shows which) */
            sh_led(L, panel.btn[B_HOME], SHL_MID);
        if (sh_fx_on())                                 /* the FX layer: what is held is lit */
            sh_led(L, panel.btn[B_FX], SHL_LIT);
    }
    if (sh.mom || sh.save || sh.pq || sh_splsv.open) {   /* a question (SAVE's, PROJECTS', SPELL's SAVE) or MOMENTARY: */
        if (sh.mom || sh.save)                          /* OCT+ breathes for yes / keep, OCT- is no */
            sh_led(L, panel.btn[B_SAVE], SHL_LIT);
        sh_led(L, panel.btn[B_OCTUP], SHL_BREATH);
        if (!sh.mom || sh.save)
            sh_led(L, panel.btn[B_OCTDN], SHL_MID);
    }
    lo = !sh.wheel && !sh.global && !sh.mom && !sh.save && !sh.pq && !sh_splsv.open ? sh_lo() : 0;
    if (song.octave > 0 || lo > 0)                      /* (the lanes: their octave in view, no question asked) */
        sh_led(L, panel.btn[B_OCTUP], SHL_LIT);
    if (sh_double_now())
        sh_led(L, panel.btn[B_OCTUP], SHL_BREATH);      /* SEQ held: OCT+ is DOUBLE */
    if (sh_reset_family() < 4u && sh_b[SH_RST_BTN[sh_reset_family()]].hold && !sh.wheel)
        sh_led(L, panel.btn[B_OCTDN], SHL_BREATH);      /* a family held: OCT- held resets it */
    if (song.octave < 0 || lo < 0)
        sh_led(L, panel.btn[B_OCTDN], SHL_LIT);
    if (sh_lg_up && fm1_ms - sh_lg_up < 150u && sh_lo_on() && !sh.wheel && !sh.global)
        sh_led(L, panel.btn[B_OCTUP], SHL_MID);         /* a hit above the octave in view: a faint pulse that way */
    if (sh_lg_dn && fm1_ms - sh_lg_dn < 150u && sh_lo_on() && !sh.wheel && !sh.global)
        sh_led(L, panel.btn[B_OCTDN], SHL_MID);
    if (song.rec && (fm1_ms / 250u) & 1u)
        sh_led(L, panel.btn[B_REC], SHL_LIT);
    if (song.playing)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    if (since < 400u) {                                 /* a corner: the panel flashes, the keys glow round sqrl */
        for (b = 0; b < NB; b++)
            sh_led(L, panel.btn[b], since < 120u ? SHL_LIT : SHL_MID);
        if (since < 240u)
            sh_keys_glow(L, hx, 2 * BR_SQRL52_W, SHL_LIT, 0);
    }
    for (c = 0; c < 4u; c++)                            /* the moments, over all of it */
        if (sh_lfx[c].kind) {
            uint32_t lv = sh_lfx_lv(sh_lfx[c].kind, fm1_ms - sh_lfx[c].t);
            if (lv == SHL_BREATH)
                sh_lfx[c].kind = 0;
            else if (lv == SHL_OFF)
                sh_led_off(L, sh_lfx[c].id);
            else
                sh_led(L, sh_lfx[c].id, lv);
        }
    for (c = 0; c < FM1_NCOL; c++) {                    /* a level wins over the ones below it: lit, breathing (it waits */
        nb[c] &= (uint8_t)~nl[c];                       /* on you, so it beats a page's mid and the landmark glow), mid, */
        nm[c] &= (uint8_t)~(nl[c] | nb[c]);             /* low */
        nd[c] &= (uint8_t)~(nl[c] | nm[c] | nb[c]);
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
    }
    for (c = 0; c < FM1_NCOL; c++)                      /* lit last: no dark frame */
        fm1_led[c] = nl[c];
}

/* ------------------------------------------------------------------------------------------- drawing --- */
/* The scene is drawn twice in a frame it changed, once per band into a 240 x 120 canvas (sh_y0 is the band's top
 * row); every primitive takes screen coordinates. The colours are the shell's own geometric palette */
#define SC_BG RGB(7, 8, 10)
#define SC_FAINT RGB(29, 34, 40)
#define SC_MID RGB(74, 82, 91)
#define SC_DIM RGB(125, 133, 141)
#define SC_BONE RGB(230, 225, 211)
#define SC_PANEL RGB(18, 20, 24)
static const uint16_t SC_KNOB[4] = {RGB(106, 169, 220), RGB(143, 191, 106), RGB(230, 225, 211), RGB(224, 137, 74)};
static int32_t sh_y0;
#define SH_TAU 6.2831853f

/* a + (b - a) * pct / 100 per channel: the shell's colours are its own, never the stock palette's GREY / MONO */
static uint16_t sh_mix(uint16_t a, uint16_t b, int32_t pct)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << SH[k];
    }
    return (uint16_t)out;
}

/* sine and cosine for drawing (no libm): x wrapped to -pi..pi, a 7th-order odd polynomial (error < 2e-4) */
static float sh_sin(float x)
{
    float x2;
    int32_t n = (int32_t)(x * (1.0f / SH_TAU) + (x >= 0.0f ? 0.5f : -0.5f));
    x -= (float)n * SH_TAU;
    if (x > 1.5707963f)
        x = 3.1415927f - x;
    else if (x < -1.5707963f)
        x = -3.1415927f - x;
    x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
}
static float sh_cos(float x) { return sh_sin(x + 1.5707963f); }

static void sh_px(int32_t x, int32_t y, uint16_t c, int32_t a)   /* a: 0 .. 256 */
{
    uint32_t i;
    y -= sh_y0;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || a <= 0)
        return;
    i = (uint32_t)y * cv_w + (uint32_t)x;
    cv_px[i] = swap16(a >= 256 ? c : sh_mix(swap16(cv_px[i]), c, a * 100 / 256));
}

/* an antialiased line (Xiaolin Wu), float endpoints */
static void sh_line(float x0, float y0, float x1, float y1, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, g, x, y, t;
    int steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx), i, n;
    if (steep) {
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    if ((steep ? (x1 < sh_y0 - 2 || x0 > sh_y0 + (int32_t)cv_h + 2) : (y0 < sh_y0 - 2 && y1 < sh_y0 - 2) ||
         (y0 > sh_y0 + (int32_t)cv_h + 2 && y1 > sh_y0 + (int32_t)cv_h + 2)))
        return;                                         /* outside this band */
    dx = x1 - x0;
    dy = y1 - y0;
    g = dx < 0.001f ? 0.0f : dy / dx;
    n = (int)(x1 + 0.5f) - (int)(x0 + 0.5f);
    x = (float)(int)(x0 + 0.5f);
    y = y0 + g * (x - x0);
    for (i = 0; i <= n; i++, x += 1.0f, y += g) {
        int32_t yi = (int32_t)y - (y < 0.0f);
        int32_t a = (int32_t)((y - (float)yi) * 256.0f);
        if (steep) {
            sh_px(yi, (int32_t)x, c, 256 - a);
            sh_px(yi + 1, (int32_t)x, c, a);
        } else {
            sh_px((int32_t)x, yi, c, 256 - a);
            sh_px((int32_t)x, yi + 1, c, a);
        }
    }
}

static void sh_poly(float cx, float cy, float r, uint32_t n, float rot, uint16_t c)
{
    uint32_t i;
    if (!n) {                                           /* a circle: 48 chords */
        for (i = 0; i < 48u; i++) {
            float a = SH_TAU * (float)i / 48.0f, b = SH_TAU * (float)(i + 1u) / 48.0f;
            sh_line(cx + r * sh_cos(a), cy + r * sh_sin(a), cx + r * sh_cos(b), cy + r * sh_sin(b), c);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        float a = rot + SH_TAU * (float)i / (float)n, b = rot + SH_TAU * (float)(i + 1u) / (float)n;
        sh_line(cx + r * sh_cos(a), cy + r * sh_sin(a), cx + r * sh_cos(b), cy + r * sh_sin(b), c);
    }
}

static void sh_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t c)
{
    if (y + (int32_t)f->h < sh_y0 || y > sh_y0 + (int32_t)cv_h)
        return;
    cv_text(x, y - sh_y0, f, s, c);
}
static int32_t sh_text_x(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t c)   /* returns its end x */
{
    sh_text(x, y, f, s, c);
    return x + text_w(f, s);
}
static void sh_text_c(int32_t cx, int32_t y, const aafont_t *f, const char *s, uint16_t c)
{
    sh_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void sh_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t c)
{
    sh_text(xr - text_w(f, s), y, f, s, c);
}
static void sh_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x, y - sh_y0, w, h, c); }

/* the parameter id k of the page in front: its label and formatted value */
static void sh_param_text(uint32_t k, const char **label, char val[12])
{
    uint8_t ids[4];
    int glob;
    const param_desc_t *d;
    const char *unit = 0;
    int32_t v;
    uint32_t n;
    int16_t *vp;
    if (sh_step_card(k, label, val, &v))                /* a step held: its own values */
        return;
    if (sh_fmop_page()) {                               /* ui_fm6op.c's cards: OP, RATIO, STAGE ... */
        int32_t r;
        uint32_t icon;
        fop_card_f(FO_PAGE[sh.page[sh.mode] - SHP_FMOP][k], label, val, &unit, &r, &icon);
        if (str_eq(*label, "STAGE")) {                  /* the stage by its name: ATK, not "1ATK" */
            str_cpy(val, unit, 12);
            return;
        }
        if (unit && unit[0] && (n = str_len(val)) < 8u)
            str_cpy(val + n, unit, 12u - n);
        return;
    }
    sh_page_ids(ids, &glob);
    if (ids[k] == SH_NONE) {
        *label = "";
        val[0] = 0;
        return;
    }
    d = sh_desc(ids[k], glob, &vp);
    v = *vp;
    *label = d->label;
    param_format(d, v, val, &unit);
    if (ids[k] == SP_SEED && v < 0) {
        str_cpy(val, "NIBS", 12u);
        unit = 0;
    }
    if (ids[k] == SP_MBACK && !v) {
        str_cpy(val, "NEVER", 12u);
        unit = 0;
    }
    if (ids[k] == SP_EFILL && v < 0) {                  /* a logic lane: its op */
        str_cpy(val, N_SP_LOGIC[clamp(-v - 1, 0, 6)], 12u);
        unit = 0;
    }
    if (unit && unit[0] && (n = str_len(val)) < 8u)
        str_cpy(val + n, unit, 12u - n);
}

static int32_t sh_param_frac(uint32_t k)                /* 0 .. 256 of the knob's range */
{
    uint8_t ids[4];
    int glob;
    const param_desc_t *d;
    int32_t v;
    int16_t *vp;
    {
        const char *l;
        char val[12];
        int32_t f;
        if (sh_step_card(k, &l, val, &f))               /* a step held: its own values */
            return f;
    }
    if (sh_fmop_page()) {
        const char *l, *u;
        char val[12];
        int32_t r;
        uint32_t icon;
        fop_card_f(FO_PAGE[sh.page[sh.mode] - SHP_FMOP][k], &l, val, &u, &r, &icon);
        return r < 0 ? 0 : r * 256 / 1000;
    }
    sh_page_ids(ids, &glob);
    if (ids[k] == SH_NONE)
        return 0;
    d = sh_desc(ids[k], glob, &vp);
    v = *vp;
    return d->max > d->min ? (v - d->min) * 256 / (d->max - d->min) : 0;
}

/* the SOUND's name: its slot's, LIBRARY's the engine it is on */
static const char *sh_sound_name(void)
{
    return SH_LIB(sh.mode) ? ENGINES[sh_eng()]->name : SH_MODES[sh.mode].name;
}

/* the header's right end: the tempo, or AUTOSAVE OFF while an unrestored autosave is held (project.c as_hold), so a
 * session that is not being kept is never quiet about it */
static void sh_hdr_right(char *b)
{
    if (autosave_held())
        str_cpy(b, "AUTOSAVE OFF", 13);
    else
        fmt_int(b, song.g[G_BPM]);
}
static void sh_draw_head(void)
{
    const sh_mode_t *m = sh_cur();
    char b[16];
    int32_t x;
    const char *sn = sh_sound_name();
    uint32_t f = SHP_FAM[sh.page[sh.mode]];
    sh_poly(10.0f, 10.0f, 5.0f, m->sides, -SH_TAU / 4.0f, m->color);
    x = 20;
    if (sh.global) {                                    /* the shell's own pages: the name alone */
        x = sh_text_x(x, 3, &AF_S, sn, m->color) + 8;
    } else if (f == B_ARP || f == B_SEQ) {              /* the NOTE half: NOTE CHORD */
        x = sh_text_x(x, 3, &AF_S, "NOTE", sh_ncur()->color) + 4;
        x = sh_text_x(x, 3, &AF_S, sh_ncur()->name, SC_MID) + 8;
    } else if (f == B_HOME) {                           /* HOME: the pair, CHORD > FM6 (KEYS: the SOUND alone) */
        if (sh.note) {
            x = sh_text_x(x, 3, &AF_S, sh_ncur()->name, sh_ncur()->color);
            x = sh_text_x(x + 4, 3, &AF_S, ">", SC_DIM) + 4;
        }
        x = sh_text_x(x, 3, &AF_S, sn, m->color) + 8;
    } else {                                            /* the SOUND half: SOUND FM6 */
        x = sh_text_x(x, 3, &AF_S, "SOUND", m->color) + 4;
        x = sh_text_x(x, 3, &AF_S, sn, SC_MID) + 8;
    }
    x = sh_text_x(x, 3, &AF_S, sh_page_name(), SC_DIM) + 8;
    if (!sh.global) {                                   /* the page's place in its family ("3/7"): nobody lost */
        uint32_t p = sh.page[sh.mode], i, n = 0, at = 0;
        for (i = 0; i < SHP_N; i++)
            if (SHP_FAM[i] == SHP_FAM[p] && sh_page_ok(i)) {
                n++;
                if (i == p)
                    at = n;
            }
        if (n > 1u) {
            fmt_int(b, (int32_t)at);
            str_cpy(b + str_len(b), "/", 2);
            fmt_int(b + str_len(b), (int32_t)n);
            x = sh_text_x(x, 3, &AF_S, b, SC_MID) + 8;
        }
        if (sh_pg_kind() && sh_pg_n() > 1u) {          /* a pattern past 16: the page in view of its pages */
            b[0] = 'P';
            fmt_int(b + 1, (int32_t)(sh_pg() / 16u + 1u));
            str_cpy(b + str_len(b), "/", 2);
            fmt_int(b + str_len(b), (int32_t)sh_pg_n());
            x = sh_text_x(x, 3, &AF_S, b, sh_ncur()->color) + 8;
        }
        if (seq_fill)                                   /* SCL held: FILL */
            x = sh_text_x(x, 3, &AF_S, "FILL", SC_BONE) + 8;
        if (sh_q.mask) {                                /* a switch queued: its slot breathes until the bar */
            char q[8] = "NEXT ";
            q[5] = (char)('A' + sh_q.slot % 4u);
            q[6] = 0;
            x = sh_text_x(x, 3, &AF_S, q, (fm1_ms / 250u) & 1u ? SC_BONE : SC_DIM) + 8;
        }
        if (SHP_GLOBAL(p))                              /* the shared effects: a turn here is heard on every part */
            x = sh_text_x(x, 3, &AF_S, "ALL", m->color) + 8;
        if (p == SHP_NOTE && SH_NOTES[sh.note].clocked)   /* a NOTE engine on the clock: its step, the part's DIV
                                                         * (the SEQ page turns it) */
            x = sh_text_x(x, 3, &AF_S, TP[P_SDIV].names[sh_trk()->p[P_SDIV] % (TP[P_SDIV].max + 1)], SC_MID) + 8;
    }
#if FELUCCA_TALK
    if (sh_eng() == ENGI_TALK && !sh.global)           /* TALK shows its bank beside the page */
        sh_text(x, 3, &AF_S, TALK_BANK_NAMES[(uint32_t)clamp(sh_trk()->p[P_E1], 0, TALK_NBANK - 1)], m->color);
#endif
    {   /* recording: the count-in's beats left, then REC, the bar (of the loop's, once a first take has set it) and the
         * take, left of the tempo where the line has room */
        char r[20];
        const track_t *t = sh_trk();
        int32_t xr;
        r[0] = 0;
        if (seq_counting() && (song.rec >> sh.part) & 1u) {
            str_cpy(r, "IN ", 4);
            fmt_int(r + 3, (int32_t)cin_left);
        } else if (rec_on(t)) {
            uint32_t spb = sh_bar_steps(t), len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
            str_cpy(r, rec_replace ? "RPL " : "REC ", 5);   /* (RPL: REC MODE REPLACE) */
            fmt_int(r + 4, (int32_t)((t->seq_idx % len) / spb + 1u));
            str_cpy(r + str_len(r), "/", 2);
            if (sh_take.first)                          /* (a first take: its length still open) */
                str_cpy(r + str_len(r), "--", 3);
            else
                fmt_int(r + str_len(r), (int32_t)((len + spb - 1u) / spb));
            if (sh_take.n && sh_take.part == sh.part && sh_take.note == sh.note) {
                str_cpy(r + str_len(r), " T", 3);
                fmt_int(r + str_len(r), sh_take.n);
            }
        }
        if (r[0]) {
            sh_hdr_right(b);
            xr = 232 - (int32_t)text_w(&AF_S, b) - 12 - (int32_t)text_w(&AF_S, r);
            if (xr < x)                                 /* (no room: REC alone) */
                r[3] = 0, xr = 232 - (int32_t)text_w(&AF_S, b) - 12 - (int32_t)text_w(&AF_S, r);
            sh_text(xr, 3, &AF_S, r, RGB(224, 90, 79));
        } else if (!sh.global && sh_pat_any(t)) {       /* not recording, the part holds notes or hits: a loop mark and
                                                         * its bars, in both views, so a take is never hidden */
            uint32_t spb = sh_bar_steps(t), len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
            fmt_int(r, (int32_t)((len + spb - 1u) / spb));
            str_cpy(r + str_len(r), " BAR", 5);
            sh_hdr_right(b);
            xr = 232 - (int32_t)text_w(&AF_S, b) - 10 - (int32_t)text_w(&AF_S, r);
            if (xr - 8 < x)                             /* (no room: the bars alone, "2B") */
                r[str_len(r) - 3] = 0, r[str_len(r) - 1] = 'B',
                xr = 232 - (int32_t)text_w(&AF_S, b) - 10 - (int32_t)text_w(&AF_S, r);
            if (xr - 8 >= x) {
                sh_poly((float)xr - 5.0f, 9.5f, 3.0f, 0, 0.0f, sh_ncur()->color);   /* (a ring) */
                sh_text(xr, 3, &AF_S, r, sh_ncur()->color);
            }
        }
    }
    sh_hdr_right(b);
    sh_text_r(232, 3, &AF_S, b, autosave_held() ? RGB(224, 90, 79) : song.playing ? RGB(127, 209, 139) : SC_DIM);
    if (sh_st.pending)                                  /* a setting not yet saved (waiting for silence) */
        sh_disc(232.0f - (float)text_w(&AF_S, b) - 6.0f, 9.5f, 2.0f, SC_MID);
    sh_line(0.0f, 19.5f, 240.0f, 19.5f, sh_mix(SC_BG, m->color, 55));
}

/* ------------------------------------------------------------------------------- each engine's picture --- */
/* the picture band, y SH_Y_PIC .. SH_Y_LEG. Each picture is the engine's own data drawn as geometry: the picture is
 * the patch. A faint flower of life stands in for the engines that have no picture of their own. */
static void sh_flower(float cx, float cy, float R)
{
    uint32_t k;
    sh_poly(cx, cy, R, 0, 0.0f, SC_FAINT);
    for (k = 0; k < 6u; k++) {
        float a = SH_TAU * (float)k / 6.0f;
        sh_poly(cx + R * sh_cos(a), cy + R * sh_sin(a), R, 0, 0.0f, SC_FAINT);
    }
}
static void sh_ring(float cx, float cy, float r, uint16_t c) { sh_poly(cx, cy, r, 0, 0.0f, c); }
static void sh_disc(float cx, float cy, float r, uint16_t c)
{
    float y;
    for (y = -r; y <= r; y += 1.0f) {
        float w = r * r - y * y, x = 0.0f;
        if (w > 0.0f) {                                 /* a square root by three Newton steps from r */
            x = r;
            x = 0.5f * (x + w / x); x = 0.5f * (x + w / x); x = 0.5f * (x + w / x);
        }
        sh_rect((int32_t)(cx - x + 0.5f), (int32_t)(cy + y + 0.5f), (int32_t)(2.0f * x + 0.5f), 1, c);
    }
}

/* FM6: the six operators on a circle (operator 1 at the top, clockwise), each as large as its output level, the
 * carriers in the mode's colour, the modulators bone; the modulation as lines, derived from the algorithm's bus
 * table exactly as fm6_core_render reads it; feedback as a small loop; each operator's frequency ratio under it */
/* what sounds now on the part in front, for the SOUND pictures: each voice's note and level 0..1 (env_out, the
 * amplitude the voice renders at), the loudest; read from the main loop as the ISR moves them (a picture a frame late
 * at worst). sh_live_sig is what of it redraws the screen: the levels in 12 steps, every 40 ms at most */
typedef struct { uint32_t n; float top, vel; uint8_t note[NVOICE], vi[NVOICE]; float lv[NVOICE]; } sh_live_t;   /* vel:
                                                         * the loudest's velocity 0..1, what its level starts from */
static void sh_live_of(const track_t *t, sh_live_t *L)
{
    uint32_t i;
    L->n = 0;
    L->top = L->vel = 0.0f;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        float l;
        if (!v->active)
            continue;
        l = (float)(v->env_out > 0 ? v->env_out : 0) * (1.0f / 32767.0f);
        l = l > 1.0f ? 1.0f : l;
        L->note[L->n] = v->note;
        L->vi[L->n] = (uint8_t)i;
        L->lv[L->n++] = l;
        if (l >= L->top) {
            L->top = l;
            L->vel = (float)v->vel * (1.0f / 127.0f);
        }
    }
}
static uint32_t sh_live_sig(void)
{
    static uint32_t h, t0;
    const track_t *t = sh_trk();
    sh_live_t L;
    uint32_t i;
    if (sh.global || sh.wheel || sh_view_note())
        return 0;
    if (fm1_ms - t0 < 40u)
        return h;
    t0 = fm1_ms;
    sh_live_of(t, &L);
    h = L.n;
    for (i = 0; i < L.n; i++)
        h = h * 31u + L.note[i] * 16u + (uint32_t)(L.lv[i] * 11.99f);
    return h;
}

/* SOUND MAIN, the knob turned last (1.3 s): its id, its colour in c, so the picture lights what it sets; SH_NONE */
static uint32_t sh_hot_main(uint16_t *c)
{
    if (sh.global || sh.wheel || sh.page[sh.mode] != SHP_HOME || sh.hot >= 4u || !sh_fresh(sh.hot_t, 1300u))
        return SH_NONE;
    *c = SC_KNOB[sh.hot];
    return sh_main_id(sh_eng(), sh.hot);
}
/* the ADSR as a sketch on a panel at (x0, y0), w by h: each stage as long as it is set (SUS: its level), the one
 * turned in its knob's colour */
static void sh_pk_adsr(const track_t *t, int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t hot, uint16_t hc)
{
    static const uint8_t ID[4] = {P_ATK, P_DEC, P_SUS, P_REL};
    float a = (float)t->p[P_ATK] / 127.0f, d = (float)t->p[P_DEC] / 127.0f, su = (float)t->p[P_SUS] / 127.0f;
    float r = (float)t->p[P_REL] / 127.0f, W = (float)(w - 8), seg[4], x = (float)x0 + 4.0f, sum, yb = (float)(y0 + h - 4);
    float ys[5], top = (float)(y0 + 4), hh = (float)(h - 8);
    uint32_t i;
    seg[0] = 0.04f + 0.30f * a;
    seg[1] = 0.04f + 0.22f * d;
    seg[2] = 0.20f;
    seg[3] = 0.04f + 0.30f * r;
    sum = seg[0] + seg[1] + seg[2] + seg[3];
    ys[0] = yb; ys[1] = top; ys[2] = ys[3] = yb - hh * su; ys[4] = yb;
    sh_rect(x0, y0, w, h, SC_PANEL);
    for (i = 0; i < 4u; i++) {
        float nx = x + W * seg[i] / sum;
        sh_line(x, ys[i], nx, ys[i + 1u], ID[i] == hot ? hc : SC_DIM);
        if (ID[i] == hot)                               /* (the stage turned: a second line, bolder) */
            sh_line(x, ys[i] - 1.0f, nx, ys[i + 1u] - 1.0f, hc);
        x = nx;
    }
}
static void sh_live_notes(const sh_live_t *L, int32_t x, int32_t y)   /* the notes sounding, centred on x (four) */
{
    char b[24];
    uint32_t i;
    b[0] = 0;
    for (i = 0; i < L->n && i < 4u; i++) {
        if (i)
            str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), N_NOTE[L->note[i] % 12u], sizeof b - str_len(b));
        fmt_int(b + str_len(b), L->note[i] / 12 - 1);
    }
    if (b[0])
        sh_text_c(x, y, &AF_S, b, SC_BONE);
}
static uint16_t fm6_hot, fm6_hot_alg;                   /* (sh_pic_fm6: MRAT's colour on the ratios, SLOT's on ALG) */
static void sh_pic_fm6(const sh_mode_t *m, const track_t *t)
{
    const uint8_t *pt = fm6_patch[sh.part];
    uint32_t alg = t->p[P_E0] >= 1 && t->p[P_E0] <= 32 ? (uint32_t)t->p[P_E0] - 1u : pt[FP_ALG] & 31u, k, j;
    const uint8_t *a = FM6_ALG[alg];
    float cx = 120.0f, cy = 104.0f, R = 58.0f, ox[6], oy[6], orr[6];
    uint8_t con[3] = {0, 0, 0}, has[3] = {0, 0, 0}, car = 0, fb = 0;   /* con[b]: bit k, operator k feeds bus b */
    uint8_t edge[6] = {0};                                  /* edge[k]: bit j, operator j modulates k */
    char b[8];
    for (k = 0; k < 6u; k++) {                              /* k = 0 is operator 6 */
        uint32_t f = a[k], in = (f >> 4) & 3u, out = f & 3u, n = 6u - k;
        float ang = -SH_TAU / 4.0f + SH_TAU * (float)(n - 1u) / 6.0f;
        int32_t ol = pt[k * FP_OP + FP_OL];
        ox[k] = cx + R * sh_cos(ang);
        oy[k] = cy + R * sh_sin(ang);
        orr[k] = 4.0f + (float)(ol > 99 ? 99 : ol) * (9.0f / 99.0f);
        if (in && has[in])
            edge[k] |= con[in];
        if ((f & 0xc0u) == 0xc0u)
            fb |= (uint8_t)(1u << k);
        if (!out)
            car |= (uint8_t)(1u << k);
        else {
            if (!(f & FM6_OADD) || !has[out])
                con[out] = 0;
            con[out] |= (uint8_t)(1u << k);
            has[out] = 1;
        }
    }
    sh_ring(cx, cy, R, SC_FAINT);
    sh_poly(cx, cy, R, 6, -SH_TAU / 4.0f, SC_FAINT);
    for (k = 0; k < 6u; k++)
        for (j = 0; j < 6u; j++)
            if ((edge[k] >> j) & 1u) {                      /* j modulates k: from rim to rim, a dot at the target */
                float dx = ox[k] - ox[j], dy = oy[k] - oy[j], d = dx * dx + dy * dy, l = 1.0f, inv;
                l = d > 1.0f ? d : 1.0f;
                inv = 1.0f / (0.5f * (l / 30.0f + 30.0f));  /* a rough 1/sqrt, refined once */
                inv = inv * (1.5f - 0.5f * d * inv * inv);
                sh_line(ox[j] + dx * inv * orr[j], oy[j] + dy * inv * orr[j], ox[k] - dx * inv * (orr[k] + 2.0f),
                        oy[k] - dy * inv * (orr[k] + 2.0f), SC_DIM);
                sh_disc(ox[k] - dx * inv * (orr[k] + 2.0f), oy[k] - dy * inv * (orr[k] + 2.0f), 1.5f, SC_DIM);
            }
    {   /* what sounds: the operators lit by the voices' level now, each by its own LEVEL; the notes under ALG */
        sh_live_t L;
        uint32_t i;
        sh_live_of(t, &L);
        for (k = 0; k < 6u && L.n; k++) {
            float g = L.top * (float)(pt[k * FP_OP + FP_OL] > 99 ? 99 : pt[k * FP_OP + FP_OL]) * (1.0f / 99.0f);
            if (g > 0.02f)
                sh_disc(ox[k], oy[k], orr[k] - 1.0f, sh_mix(SC_BG, (car >> k) & 1u ? m->color : SC_BONE, (int32_t)(10.0f + 60.0f * g)));
        }
        sh_live_notes(&L, 120, 124);
    }
    {   /* the knob turned on MAIN: MLVL and MEG light the modulators, MRAT their ratios, SLOT the algorithm */
        uint16_t hc = 0;
        uint32_t hot = sh_hot_main(&hc);
        if (hot == P_E2 || hot == P_E4)
            for (k = 0; k < 6u; k++)
                if (!((car >> k) & 1u)) {
                    sh_ring(ox[k], oy[k], orr[k] + 2.0f, hc);
                    sh_ring(ox[k], oy[k], orr[k] + 3.0f, hc);
                }
        fm6_hot = hot == P_E3 ? hc : 0u;
        fm6_hot_alg = hot == P_E7 ? hc : 0u;
    }
    for (k = 0; k < 6u; k++) {
        uint16_t c = (car >> k) & 1u ? m->color : SC_BONE;
        int32_t fc = pt[k * FP_OP + FP_FC], ff = pt[k * FP_OP + FP_FF];
        uint32_t r100 = (uint32_t)((fc ? fc * 100 : 50) * (100 + ff) / 100);   /* ratio x100: coarse 0 is 0.5 */
        sh_ring(ox[k], oy[k], orr[k], c);
        if ((car >> k) & 1u)
            sh_ring(ox[k], oy[k], orr[k] - 2.0f, c);
        if ((fb >> k) & 1u)
            sh_ring(ox[k] + (ox[k] - cx) * 0.22f, oy[k] + (oy[k] - cy) * 0.22f, 5.0f, SC_MID);
        fmt_fix(b, (int32_t)r100, 2);
        sh_text_c((int32_t)ox[k], (int32_t)(oy[k] + orr[k]) + 2, &AF_S, b, fm6_hot && !((car >> k) & 1u) ? fm6_hot : SC_MID);
    }
    if (sh_fmop_page()) {                               /* the operator KNOB 1 picked, lit */
        k = 5u - fop_op % 6u;
        sh_ring(ox[k], oy[k], orr[k] + 3.0f, SC_BONE);
        sh_ring(ox[k], oy[k], orr[k] + 4.0f, SC_BONE);
    }
    if (sh_fmop_page() && sh.page[sh.mode] == SHP_FMENV) {   /* OP ENV: its envelope, the stage picked lit */
        const uint8_t *op = pt + (5u - fop_op % 6u) * FP_OP;
        float x = 84.0f, y0 = 122.0f, px = 84.0f, py = y0, w;
        sh_rect(80, 84, 80, 42, SC_PANEL);
        for (j = 0; j < 4u; j++) {
            w = 4.0f + (float)(99 - (op[FP_R1 + j] > 99 ? 99 : op[FP_R1 + j])) * 0.12f;   /* a faster rate draws shorter */
            if (j == 2u)
                w += 10.0f;                             /* the sustain is held a moment */
            x = px + w;
            sh_line(px, py, x, y0 - (float)(op[FP_L1 + j] > 99 ? 99 : op[FP_L1 + j]) * 0.34f,
                    j == (uint32_t)(fop_stg & 3u) ? m->color : SC_DIM);
            px = x;
            py = y0 - (float)(op[FP_L1 + j] > 99 ? 99 : op[FP_L1 + j]) * 0.34f;
        }
        return;
    }
    fmt_int(b, (int32_t)alg + 1);
    sh_text_c(120, 92, &AF_S, "ALG", fm6_hot_alg ? fm6_hot_alg : SC_DIM);
    sh_text_c(120, 104, &AF_M, b, fm6_hot_alg ? fm6_hot_alg : SC_BONE);
}

/* LIZARD: the wave the keys play as the edge of an Escher tile: its 8 steps (a drawing, a switcher or a CZ set),
 * smoothed between, along opposite edges, so the tiles lock; PSAW / SUPR: the saws fanned by DTN */
static float sh_lz_edge(const lz_set_t *st, float u)
{
    float x = u * 8.0f, f;
    uint32_t i = (uint32_t)x % 8u, j = (i + 1u) % 8u;
    f = x - (float)(int32_t)x;
    f = f * f * (3.0f - 2.0f * f);
    return ((float)st->lv[i] * (1.0f - f) + (float)st->lv[j] * f) / 100.0f;
}
static void sh_pic_lizard(const sh_mode_t *m, const track_t *t)
{
    uint32_t mode = (uint32_t)clamp(t->p[P_E0], 0, 4), q;
    int32_t i, j;
    if (mode <= LZM_CZ) {
        const lz_set_t *st = &LZ_SETS[mode][((uint32_t)t->p[P_E2] >> 3) & 15u];
        lz_set_t own;
        sh_live_t L;
        float s = 50.0f, A = 11.0f;
        uint16_t lit = m->color;
        int32_t hot = -1;                               /* DRAW 1-4 / 5-8: the step a knob is turning */
        if (mode == LZM_DRAW && lizard_own(t->p)) {
            for (q = 0; q < 8u; q++) {
                own.src[q] = LZ_DC;
                own.lv[q] = lizard_lv(t->p[P_LZD0 + q]);
            }
            st = &own;
        }
        if ((sh.page[sh.mode] == SHP_DRAW1 || sh.page[sh.mode] == SHP_DRAW2) && sh.hot < 4u &&
            sh_fresh(sh.hot_t, 1300u))
            hot = (sh.page[sh.mode] == SHP_DRAW2 ? 4 : 0) + sh.hot;
        sh_live_of(t, &L);
        if (L.n) {                                      /* sounding: the tiles swell with the level, the lit ones bone */
            A = 11.0f + 8.0f * L.top;
            lit = sh_mix(m->color, SC_BONE, (int32_t)(70.0f * L.top));
        }
        for (j = -1; j < 5; j++)
            for (i = -1; i < 6; i++) {
                float x = (float)i * s, y = (float)SH_Y_PIC + 2.0f + (float)j * s, px = 0, py = 0, nx, ny;
                for (q = 0; q <= 32u; q++) {            /* the top edge, and the same edge one tile down: they lock */
                    float u = (float)q / 32.0f;
                    nx = x + u * s;
                    ny = y + A * sh_lz_edge(st, u);
                    if (q && ny > SH_Y_PIC + 1 && ny < SH_Y_LEG - 2)   /* (its start held a row inside the band:
                                                         * sh_line's ends round half a pixel out, and nothing may
                                                         * reach the header's rows) */
                        sh_line(px, py < (float)(SH_Y_PIC + 1) ? (float)(SH_Y_PIC + 1) : py, nx, ny,
                                (i + j) & 1 ? lit : SC_MID);
                    px = nx;
                    py = ny;
                }
                for (q = 0; q <= 32u; q++) {            /* the left edge: the wave a quarter on, turned */
                    float u = (float)q / 32.0f;
                    nx = x + A * 0.8f * sh_lz_edge(st, u + 0.25f);
                    ny = y + u * s;
                    if (q && ny > SH_Y_PIC + 1 && ny < SH_Y_LEG - 2 && py > SH_Y_PIC + 1)
                        sh_line(px, py, nx, ny, SC_FAINT);
                    px = nx;
                    py = ny;
                }
            }
        {   /* the knob turned on MAIN: WAVE lights the cycle, REL shows the envelope */
            uint16_t hc = 0;
            uint32_t hot = sh_hot_main(&hc);
            sh_rect(64, 150, 112, 40, hot == P_E2 ? sh_mix(SC_PANEL, hc, 25) : SC_PANEL);   /* the cycle itself, to scale */
            if (hot == P_REL)
                sh_pk_adsr(t, 64, 104, 112, 40, hot, hc);
        }
        for (q = 0; q < 8u; q++) {
            int32_t x0 = 70 + (int32_t)q * 12, y = 170 - st->lv[q] * 14 / 100;
            if ((int32_t)q == hot)                      /* the step under the turning knob is lit */
                sh_rect(x0, 152, 12, 36, sh_mix(SC_PANEL, SC_KNOB[sh.hot], 30));
            sh_rect(x0, y, 12, 1, (int32_t)q == hot ? SC_BONE : m->color);
            if (q)
                sh_rect(x0, y < 170 - st->lv[q - 1] * 14 / 100 ? y : 170 - st->lv[q - 1] * 14 / 100, 1,
                        (y > 170 - st->lv[q - 1] * 14 / 100 ? y - (170 - st->lv[q - 1] * 14 / 100)
                                                             : 170 - st->lv[q - 1] * 14 / 100 - y) + 1, m->color);
        }
        sh_rect(70, 170, 96, 1, SC_FAINT);
    } else {                                            /* PSAW / SUPR: a fan of saws, spread by DTN */
        uint32_t n = mode == LZM_SUPR ? 5u : (uint32_t)clamp(t->p[P_E4], 2, 5);
        float dtn = (float)t->p[P_E3] / 127.0f;
        sh_flower(120.0f, 104.0f, 30.0f);
        for (q = 0; q < n; q++) {
            float rot = ((float)q - (float)(n - 1u) / 2.0f) * (0.05f + 0.35f * dtn), ph;
            uint16_t c = q == (n - 1u) / 2u ? m->color : SC_DIM;
            for (ph = 0.0f; ph < 3.0f; ph += 1.0f) {   /* three teeth of each saw, turned about the centre */
                float x0 = -90.0f + ph * 60.0f, x1 = x0 + 60.0f;
                float ax = x0, ay = 30.0f, bx = x1, by = -30.0f, cr = sh_cos(rot), sr = sh_sin(rot);
                sh_line(120.0f + ax * cr - ay * sr, 104.0f + ax * sr + ay * cr, 120.0f + bx * cr - by * sr,
                        104.0f + bx * sr + by * cr, c);
                sh_line(120.0f + bx * cr - by * sr, 104.0f + bx * sr + by * cr, 120.0f + bx * cr - ay * sr,
                        104.0f + bx * sr + ay * cr, c);
            }
        }
    }
}

#if FELUCCA_VODER
/* VODER: its ten bands on a log frequency axis, lit while their keys are held; the stops p t k and HISS beside */
static void sh_pic_voder(const sh_mode_t *m, const track_t *t)
{
    uint32_t k, held = 0, lit = 0;
    int32_t x0 = 16, x1 = 224, base = 160;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes >> k) & 1u) {
            int32_t n = voder_keys(t, k);
            if (n >= VODER_NOTE_BAND && n < VODER_NOTE_BAND + VODER_NBAND)
                lit |= 1u << (n - VODER_NOTE_BAND);
            else if (n >= VODER_NOTE_STOP && n <= VODER_NOTE_HISS)
                held |= 1u << (n - VODER_NOTE_STOP);
        }
    sh_rect(x0, base, x1 - x0, 1, SC_MID);
    for (k = 0; k < VODER_NBAND; k++) {                 /* band k: from its low to high edge, log spaced */
        float l = k ? (float)VODER_LO[k] : 100.0f, h = (float)VODER_HI[k], fl = 0, fh = 0;
        int32_t bx0, bx1, hgt = 40;
        while (l > 100.0f * 1.25f) { l /= 1.25f; fl += 1.0f; }
        while (h > 100.0f * 1.25f) { h /= 1.25f; fh += 1.0f; }
        bx0 = x0 + (int32_t)(fl * (float)(x1 - x0) / 19.0f);
        bx1 = x0 + (int32_t)(fh * (float)(x1 - x0) / 19.0f) - 2;
        if ((lit >> k) & 1u)
            sh_rect(bx0, base - hgt, bx1 - bx0, hgt, sh_mix(SC_BG, m->color, 70));
        sh_rect(bx0, base - hgt, bx1 - bx0, 1, (lit >> k) & 1u ? m->color : SC_DIM);
        sh_rect(bx0, base - hgt, 1, hgt, SC_FAINT);
    }
    sh_text(x0, base + 4, &AF_S, "0", SC_MID);
    sh_text_r(x1, base + 4, &AF_S, "7.5 kHz", SC_MID);
    {
        static const char *const S[4] = {"P", "T", "K", "HISS"};
        for (k = 0; k < 4u; k++)
            sh_text_c(48 + (int32_t)k * 48, 36, &AF_M, S[k], (held >> k) & 1u ? m->color : SC_DIM);
    }
}
#endif

#if FELUCCA_TALK
/* TALK: the word in large type over a ten-rung lattice (the 5220's reflection stages) */
static void sh_pic_talk(const sh_mode_t *m, const track_t *t)
{
    const talk_bank_t *b = talk_bank(t);
    uint32_t w = (uint32_t)clamp(t->p[P_E0], 0, b->n - 1), k, bank;
    if (talk_now(t, &bank, &w))                         /* the word just spoken (NEXT and RAND pick it) */
        b = &TALK_BANKS[bank % TALK_NBANK];
    w %= b->n;
    for (k = 0; k < 10u; k++) {
        int32_t x = 30 + (int32_t)k * 20;
        sh_rect(x, 60, 1, 90, SC_FAINT);
        sh_rect(x - 6, 60 + (int32_t)((k * 37u) % 80u), 13, 1, SC_MID);
    }
    sh_rect(20, 150, 200, 1, SC_FAINT);
    sh_rect(20, 60, 200, 1, SC_FAINT);
    sh_text_c(120, 86, &AF_L, b->names[w], m->color);
}
#endif

#if FELUCCA_SYLLABLE
/* SYLLABLE: the syllable spelled (CONS . VOWEL . END) over Hillenbrand's F1 / F2 chart, the twelve vowels faint, the
 * one sung a dot, a diphthong an arrow from its first vowel to its second */
static void sh_pic_syllable(const sh_mode_t *m, const track_t *t)
{
    uint32_t c = (uint32_t)clamp(t->p[P_E0], 0, SYL_NCONS - 1), v = (uint32_t)clamp(t->p[P_E1], 0, SYL_NVOWEL - 1),
             e = (uint32_t)clamp(t->p[P_E7], 0, SYL_NCONS - 1), k, ph = SYL_NO_PHRASE, pos = 0, n;
    const syl_step_t *steps;
    char sp[32];
    if (syl_now(t, &c, &v, &e, &ph, &pos)) {            /* the syllable just sung (PICK may have chosen it) */
        c %= SYL_NCONS;
        v %= SYL_NVOWEL;
        e %= SYL_NCONS;
    }
    float X[12], Y[12];
    for (k = 0; k < 12u; k++) {                         /* F2 2400 .. 800 left to right, F1 300 .. 800 down */
        X[k] = 24.0f + (2400.0f - (float)syl_vowel_f[k][1]) * (192.0f / 1600.0f);
        Y[k] = 60.0f + ((float)syl_vowel_f[k][0] - 300.0f) * (100.0f / 500.0f);
        sh_ring(X[k], Y[k], 2.0f, SC_MID);
        sh_text((int32_t)X[k] + 4, (int32_t)Y[k] - 6, &AF_S, SYL_VOWEL_NAMES[k], SC_DIM);
    }
    sh_rect(24, 160, 192, 1, SC_FAINT);
    sh_rect(24, 56, 1, 104, SC_FAINT);
    if (v < 12u) {
        sh_disc(X[v], Y[v], 4.0f, m->color);
    } else {
        uint32_t a = syl_diph[v - 12u][0], z = syl_diph[v - 12u][1];
        sh_line(X[a], Y[a], X[z], Y[z], m->color);
        sh_disc(X[a], Y[a], 3.0f, m->color);
        sh_ring(X[z], Y[z], 5.0f, m->color);
    }
    str_cpy(sp, c ? SYL_CONS_NAMES[c] : "", sizeof sp);
    if (c)
        str_cpy(sp + str_len(sp), " . ", sizeof sp - str_len(sp));
    str_cpy(sp + str_len(sp), SYL_VOWEL_NAMES[v], sizeof sp - str_len(sp));
    if (e) {
        str_cpy(sp + str_len(sp), " . ", sizeof sp - str_len(sp));
        str_cpy(sp + str_len(sp), SYL_CONS_NAMES[e], sizeof sp - str_len(sp));
    }
    for (k = 0; sp[k]; k++)                             /* AF_L has capitals and digits only */
        if (sp[k] >= 'a' && sp[k] <= 'z')
            sp[k] = (char)(sp[k] - 32);
    sh_text_c(120, 22, &AF_L, sp, SC_BONE);
    if (ph != SYL_NO_PHRASE && syl_phrase(ph, &steps, &n) && n) {   /* the phrase, the syllable sung lit */
        int32_t x = 0, w = 0;
        for (k = 0; k < n; k++)
            w += text_w(&AF_S, SYL_CONS_NAMES[steps[k].c % SYL_NCONS]) + text_w(&AF_S, SYL_VOWEL_NAMES[steps[k].v % SYL_NVOWEL]) + 8;
        x = 120 - w / 2;
        for (k = 0; k < n; k++) {
            uint16_t col = k == pos ? m->color : SC_DIM;
            x = cv_text(x, 166 - sh_y0, &AF_S, steps[k].c ? SYL_CONS_NAMES[steps[k].c % SYL_NCONS] : "", col);
            x = cv_text(x, 166 - sh_y0, &AF_S, SYL_VOWEL_NAMES[steps[k].v % SYL_NVOWEL], col) + 8;
        }
    }
}
#endif

/* MOUTH: the lips the vowel being sung makes, from its F1 and F2 (mouth_face.h, after Marage's plaster casts of the
 * vowels): the jaw opens with F1, the lips spread with a high F2 and round with a low one. Silent: a mouth at rest */
#include "mouth_face.h"
static float sh_exp2(float x)                           /* 2^x, a drawing's precision */
{
    int32_t i = (int32_t)x - (x < 0.0f);
    float f = x - (float)i, r = 1.0f + f * (0.6565f + 0.3435f * f);
    return i >= 0 ? r * (float)(1u << (i > 30 ? 30 : i)) : r / (float)(1u << (-i > 30 ? 30 : -i));
}
/* a closed polygon of n points filled by rows (even-odd), the rows y_lo..y_hi only (the teeth: inside the opening) */
static void sh_fill_poly(const float *x, const float *y, uint32_t n, float y_lo, float y_hi, uint16_t c)
{
    float lo = y[0], hi = y[0], xs[8], yy;
    uint32_t i, k, m;
    for (i = 1; i < n; i++) {
        lo = y[i] < lo ? y[i] : lo;
        hi = y[i] > hi ? y[i] : hi;
    }
    lo = lo < y_lo ? y_lo : lo;
    hi = hi > y_hi ? y_hi : hi;
    for (yy = (float)(int32_t)lo + 0.5f; yy < hi; yy += 1.0f) {
        for (i = 0, m = 0; i < n && m < 8u; i++) {      /* (a mouth's contour crosses a row twice, at most four times) */
            uint32_t j = (i + 1u) % n;
            if ((y[i] <= yy) != (y[j] <= yy))
                xs[m++] = x[i] + (yy - y[i]) * (x[j] - x[i]) / (y[j] - y[i]);
        }
        for (i = 1; i < m; i++)                         /* (sorted, the few there are) */
            for (k = i; k > 0 && xs[k - 1u] > xs[k]; k--) {
                float tq = xs[k];
                xs[k] = xs[k - 1u];
                xs[k - 1u] = tq;
            }
        for (i = 0; i + 1u < m; i += 2u)
            sh_rect((int32_t)(xs[i] + 0.5f), (int32_t)yy, (int32_t)(xs[i + 1u] - xs[i] + 0.5f), 1, c);
    }
}
/* the F1 and F2 of the newest MOUTH voice that sounds on t, log2 Hz Q16 (0: none sounds) */
static uint32_t sh_mouth_f(const track_t *t, int32_t *f1, int32_t *f2)
{
    const mth_part_t *mp = mouth_of(t);
    uint32_t i, best = 0xFFFFFFFFu, any = 0;
    for (i = 0; mp && i < MOUTH_POLY; i++)
        if (t->v[i].active && mth_active(&mp->v[i]) && mp->age[i] < best) {
            best = mp->age[i];
            *f1 = mp->v[i].cur[SYL_F1];
            *f2 = mp->v[i].cur[SYL_F2];
            any = 1;
        }
    return any;
}
/* the lips at F1 / F2 (Hz) on a plaster-cast arch: the arch's rows y0 to y1, its half circle r wide about row yc,
 * the mouth at (120, cy) k wide */
static void sh_lips(const sh_mode_t *m, float f1, float f2, int32_t y0, int32_t y1, int32_t yc, int32_t r, float cy,
                    float k)
{
    mf_shape_t f;
    float cx = 120.0f, ox[MF_N], oy[MF_N], ix[MF_N], iy[MF_N];
    uint32_t i;
    int32_t y;
    mf_shape(f1, f2, &f);
    for (y = y0; y < y1; y++) {                         /* the plaster cast: an arch the mouth sits in */
        int32_t dy = y - yc, hw = r;
        while (dy < 0 && hw * hw > r * r - dy * dy)     /* (the half circle's width at this row) */
            hw--;
        sh_rect(120 - hw, y, 2 * hw, 1, SC_PANEL);
    }
    for (i = 0; i < MF_N; i++) {
        ox[i] = cx + f.ox[i] * k;
        oy[i] = cy + f.oy[i] * k;
        ix[i] = cx + f.ix[i] * k;
        iy[i] = cy + f.iy[i] * k;
    }
    sh_fill_poly(ox, oy, MF_N, 0.0f, 240.0f, m->color);
    sh_fill_poly(ix, iy, MF_N, 0.0f, 240.0f, SC_BG);
    if (f.teeth_h > 0.0f)                               /* the upper teeth, inside the opening only */
        sh_fill_poly(ix, iy, MF_N, cy + f.teeth_y * k, cy + (f.teeth_y + f.teeth_h) * k, SC_BONE);
}
static void sh_pic_mouth(const sh_mode_t *m, const track_t *t)
{
    float f1 = 300.0f, f2 = 1400.0f;
    uint32_t ph = (uint32_t)clamp(t->p[P_E4], 0, MOUTH_NPHRASE - 1);
    int32_t q1, q2;
    if (sh_mouth_f(t, &q1, &q2)) {                      /* (silent: a mouth at rest) */
        f1 = sh_exp2((float)q1 * (1.0f / 65536.0f));
        f2 = sh_exp2((float)q2 * (1.0f / 65536.0f));
    }
    sh_lips(m, f1, f2, 36, 170, 92, 76, 112.0f, 86.0f);
    sh_text_c(120, 22, &AF_S, MOUTH_PHRASE_NAMES[ph], SC_DIM);
}
/* SPELL: the phrase as a row of syllable chips, the cursor's lit (the row slides to keep it in view), a gap at each
 * word's end; under it the lips the cursor's vowel makes, from its formants in MOUTH's table */
static void sh_pic_spell(const sh_mode_t *m, const track_t *t)
{
    char txt[SPL_MAX][16];
    int32_t w[SPL_MAX], x0 = 0, x, tot = 0;
    uint32_t i, k;
    const mth_ph_t *v = &MTH_PH[spl.s[spl.cur].v % MTH_NPH];
    (void)t;
    for (i = 0; i < spl.n; i++) {                       /* each chip: its phonemes' names run together */
        const spl_syl_t *y = &spl.s[i];
        txt[i][0] = 0;
        for (k = 0; k < y->non; k++)
            str_cpy(txt[i] + str_len(txt[i]), MTH_PH_NAMES[y->on[k]], sizeof txt[i] - str_len(txt[i]));
        str_cpy(txt[i] + str_len(txt[i]), MTH_PH_NAMES[y->v], sizeof txt[i] - str_len(txt[i]));
        for (k = 0; k < y->nco; k++)
            str_cpy(txt[i] + str_len(txt[i]), MTH_PH_NAMES[y->co[k]], sizeof txt[i] - str_len(txt[i]));
        w[i] = text_w(&AF_S, txt[i]) + 8 + (y->eow ? 6 : 0);
        if (i < spl.cur)
            x0 += w[i];
        tot += w[i];
    }
    x = tot <= 232 ? 120 - tot / 2 : 120 - x0 - w[spl.cur] / 2;   /* (all of it centred, or the cursor's chip) */
    for (i = 0; i < spl.n; i++) {
        int32_t cw = w[i] - (spl.s[i].eow ? 6 : 0) - 2;
        if (x + cw > 0 && x < 240) {
            if (i == spl.cur)
                sh_rect(x, 26, cw, 18, m->color);
            else
                sh_rect(x, 43, cw, 1, SC_MID);
            cv_text(x + 3, 29 - sh_y0, &AF_S, txt[i], i == spl.cur ? SC_BG : SC_BONE);
        }
        x += w[i];
    }
    sh_lips(m, (float)v->f[0], (float)v->f[1], 54, 180, 110, 62, 128.0f, 74.0f);
}

#if FELUCCA_LOOP
/* LOOP: the tape as a reel: the whole tape a faint ring, the loop (SPLICE) the arc in LOOP's colour, the heads a dot
 * at their place on it; the loop's length in seconds, worked out from its cells and the tape's speed, and the deck's
 * state (REC lit red while the record head is on) */
static void sh_pic_loop(const sh_mode_t *m, const track_t *t)
{
    const tl_t *tl = &loop_tl;
    float cx = 120.0f, cy = 100.0f, r = 54.0f, a0 = -SH_TAU / 4.0f, fl, fp, a;
    uint32_t i, ms = 0, q = (uint32_t)clamp(t->p[P_E4], TPL_HI, TPL_CASS);
    char b[24], n[8];
    sh_ring(cx, cy, r, SC_FAINT);
    if (tl->mem && tl->cap) {
        fl = (float)tl->len / (float)tl->cap;
        fp = tl->len ? (float)((uint32_t)tl->pos >> 12) / (float)tl->len : 0.0f;
        for (i = 0; i < 48u && (float)i < 48.0f * fl; i++) {   /* the loop: an arc of the tape */
            float u = a0 + SH_TAU * (float)i / 48.0f, v = a0 + SH_TAU * ((float)i + 1.0f > 48.0f * fl ? fl : ((float)i + 1.0f) / 48.0f);
            sh_line(cx + r * sh_cos(u), cy + r * sh_sin(u), cx + r * sh_cos(v), cy + r * sh_sin(v), m->color);
        }
        a = a0 + SH_TAU * fl * fp;
        sh_disc(cx + r * sh_cos(a), cy + r * sh_sin(a), 4.0f, SC_BONE);
        ms = (uint32_t)((uint64_t)tl->len * 1000u * 4096u / (44100u * (uint32_t)TPL_VB[tl->q % TPL_NQ]));
    } else {                                            /* (not threaded yet: the length the tape will hold) */
        ms = (uint32_t)((uint64_t)DLY_LEN * 2u / (q >= TPL_LO ? 1u : 2u) * 1000u * 4096u / (44100u * (uint32_t)TPL_VB[q]));
    }
    fmt_int(b, (int32_t)(ms / 1000u));
    str_cpy(b + str_len(b), ".", 2);
    fmt_int(n, (int32_t)(1000u + ms % 1000u));
    str_cpy(b + str_len(b), n + 1, 4);                  /* (three decimals: 1000 + ms, its first digit dropped) */
    str_cpy(b + str_len(b), " S", 3);
    sh_text_c(120, 92, &AF_M, b, SC_BONE);
    sh_text_c(120, 166, &AF_S, t->p[P_E1] ? "REC" : t->p[P_E3] == TPL_STOP ? "STOPPED" : t->p[P_E3] == TPL_REV ? "REVERSE" : "PLAY",
              t->p[P_E1] ? RGB(224, 90, 79) : SC_DIM);
}
#endif

/* CHORD: the chord on the circle of fifths, its triangle coloured by what it does in the key (tension: it holds the
 * leading tone or a tritone; preparation: the fourth degree; else rest), its name, and its notes on a keyboard line */
static void sh_pic_chord(const sh_mode_t *m, const track_t *t)
{
    float cx = 120.0f, cy = 100.0f, R = 56.0f, px[12], py[12];
    uint32_t k = trk_index(t), root, mask, pc, n = 0, rel = 0, i, key = (uint32_t)clamp(t->p[P_ROOT], 0, 11);
    uint16_t fc;
    char b[16];
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (pc = 0; pc < 12u; pc++) {
        float a = -SH_TAU / 4.0f + SH_TAU * (float)((pc * 7u) % 12u) / 12.0f;
        px[pc] = cx + R * sh_cos(a);
        py[pc] = cy + R * sh_sin(a);
        sh_text_c((int32_t)(cx + (R + 13.0f) * sh_cos(a)), (int32_t)(cy + (R + 13.0f) * sh_sin(a)) - 6, &AF_S, NN[pc],
                  SC_DIM);
    }
    sh_ring(cx, cy, R, SC_FAINT);
    if (chord_last[k].gen)
        sh_ring(cx, cy, R * 0.5f, SC_FAINT);
    if (!chord_last[k].gen) {                           /* before the first chord: the key's notes on the ring, the hint */
        uint32_t sm = scale_mask(t);
        if (sm == 0xFFFu || !sm)
            sm = SCALE_MASK[1];
        for (pc = 0; pc < 12u; pc++)
            if ((sm >> ((pc + 12u - key) % 12u)) & 1u)
                sh_disc(px[pc], py[pc], pc == key ? 3.0f : 2.0f, pc == key ? m->color : SC_MID);
        sh_text_c(120, 86, &AF_S, "A ROOT: D4 UP", SC_DIM);
        sh_text_c(120, 100, &AF_S, "ITS CHORD: LEFT", SC_MID);
        sh_text_c(120, 181, &AF_S, "HOLD B3: + KEYS ADD, + SELECT CHORDS", SC_DIM);
        return;
    }
    root = chord_last[k].root;
    mask = chord_last[k].mask;
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            rel |= 1u << ((root + i + 120u - key) % 12u);
    fc = (rel >> 11) & 1u || ((rel & (rel >> 6)) & 0x3Fu) ? RGB(224, 137, 74) :   /* a tritone: a and a + 6 both in */
         (rel >> 5) & 1u ? RGB(143, 191, 106) : RGB(106, 169, 220);
    {
        float qx[12], qy[12];
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                qx[n] = px[(root + i) % 12u];
                qy[n] = py[(root + i) % 12u];
                n++;
            }
        for (i = 0; i < n; i++)
            sh_line(qx[i], qy[i], qx[(i + 1u) % n], qy[(i + 1u) % n], i < 3u ? fc : SC_BONE);
        for (i = 0; i < n; i++)
            sh_disc(qx[i], qy[i], i ? 2.0f : 3.0f, i ? SC_BONE : fc);
    }
    chord_name(b, root, mask);
    sh_text_c(120, 92, &AF_M, b, SC_BONE);
    if (chord_kit(t) || trk_vmode(t) != V_POLY)         /* a SOUND that plays one note (MONO .., a kit): the root */
        sh_text_c(120, 124, &AF_S, chord_kit(t) ? "ROOT ONLY: A KIT" : "ROOT ONLY: VCE TO POLY", SC_DIM);   /* (how out:
                                                         * EDIT's VOICE page) */
    if (shell_chord_qual || shell_chord_add) {          /* not the standard chords: say so */
        uint32_t na = (uint32_t)__builtin_popcount(shell_chord_add);
        char c[24];
        str_cpy(c, shell_chord_qual ? N_CQUAL[shell_chord_qual] : "KEY", sizeof c);
        if (na) {
            str_cpy(c + str_len(c), " +", sizeof c - str_len(c));
            fmt_int(c + str_len(c), (int32_t)na);
        }
        sh_text_c(120, 110, &AF_S, c, m->color);
    }
    for (i = 0; i < chord_last[k].n; i++) {             /* its notes, C3 .. C6 */
        int32_t x = 24 + ((int32_t)chord_last[k].note[i] - 48) * 192 / 36;
        sh_disc((float)x, 178.0f, 2.5f, i ? SC_BONE : fc);
    }
    sh_rect(24, 178, 192, 1, SC_FAINT);
}

#if FELUCCA_ORGAN
/* ORGAN: its nine drawbars as the console shows them, pulled down by their value (0..8), in their colours: the subs
 * brown, the octaves white, the mutations black (TOP's three move as one); PERC lit when on */
static void sh_pic_organ(const track_t *t)
{
    static const uint8_t SRC[9] = {0, 1, 2, 3, 4, 5, 6, 6, 6};          /* bar -> its knob (TOP: the last three) */
    static const uint8_t KIND[9] = {0, 0, 1, 1, 2, 1, 2, 2, 1};         /* 0 brown, 1 white, 2 black */
    static const uint16_t COL[3] = {RGB(150, 96, 60), RGB(220, 214, 200), RGB(70, 70, 76)};
    static const char *const LAB[9] = {"16", "5", "8", "4", "2", "2", "1", "1", "1"};
    uint32_t i;
    for (i = 0; i < 9u; i++) {
        int32_t x = 18 + (int32_t)i * 21, v = clamp(t->p[P_E0 + SRC[i]], 0, 8), h = 18 + v * 14;
        sh_rect(x, 34, 14, h, COL[KIND[i]]);
        sh_rect(x - 2, 34 + h, 18, 6, COL[KIND[i]]);    /* the bar's handle */
        sh_text_c(x + 7, 176, &AF_S, LAB[i], SC_DIM);
    }
    sh_text_r(236, 24, &AF_S, t->p[P_E7] ? "PERC" : "", RGB(224, 137, 74));
}
#endif

/* a note_engine.h engine: what it plays now, the part's sounding notes on the circle of fifths (TONNETZ's triads
 * walk it as triangles), the lowest in its colour */
static void sh_pic_ne(const track_t *t)
{
    const sh_note_t *n = sh_ncur();
    float cx = 120.0f, cy = 104.0f, R = 58.0f, qx[NVOICE], qy[NVOICE];
    uint32_t pc, i, k = 0, low = 128;
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (pc = 0; pc < 12u; pc++) {
        float a = -SH_TAU / 4.0f + SH_TAU * (float)((pc * 7u) % 12u) / 12.0f;
        sh_text_c((int32_t)(cx + (R + 13.0f) * sh_cos(a)), (int32_t)(cy + (R + 13.0f) * sh_sin(a)) - 6, &AF_S, NN[pc],
                  SC_DIM);
    }
    sh_ring(cx, cy, R, SC_FAINT);
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate) {
            float a = -SH_TAU / 4.0f + SH_TAU * (float)(((t->v[i].note % 12u) * 7u) % 12u) / 12.0f;
            qx[k] = cx + R * sh_cos(a);
            qy[k] = cy + R * sh_sin(a);
            low = t->v[i].note < low ? t->v[i].note : low;
            k++;
        }
    for (i = 0; i + 1u < k; i++)
        sh_line(qx[i], qy[i], qx[i + 1u], qy[i + 1u], n->color);
    if (k > 2u)
        sh_line(qx[k - 1u], qy[k - 1u], qx[0], qy[0], n->color);
    for (i = 0; i < k; i++)
        sh_disc(qx[i], qy[i], 3.0f, SC_BONE);
    sh_text_c(120, 96, &AF_M, n->name, n->color);
    sh_text_c(120, 114, &AF_S, !n->clocked ? "THE KEYS PLAY IT" : song.playing ? "PLAYING ON THE CLOCK" : "PLAY STARTS IT",
              SC_DIM);
    (void)low;
}

#define SH_ACCENT RGB(255, 236, 180)
/* ACID: the page in view as a 303's notes, each step a bar at its pitch (an octave and a half round the line's own
 * range), an accent pale, a slide a stroke to the next, a tie the bar held on; the playhead under its step, pitch
 * mode's cursor a frame. Under them the step held or written last, the scale and the LEN */
static void sh_pic_acid(const sh_mode_t *m, const track_t *t)
{
    const int32_t x0 = 16, cw = 13, top = 30, h = 120;
    uint32_t i, pg = sh_pg(), len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u, lo = 127, hi = 0, last = 0;
    uint64_t locks = motion_lock_steps(trk_index(t));
    uint16_t c = sh_ncur()->color;
    char b[24];
    (void)m;
    for (i = 0; i < len; i++)                           /* the line's range: its pitches fill the height */
        if (t->step[i].n && t->step[i].time == ST_NOTE) {
            lo = t->step[i].note[0] < lo ? t->step[i].note[0] : lo;
            hi = t->step[i].note[0] > hi ? t->step[i].note[0] : hi;
        }
    if (lo > hi)
        lo = hi = 48;
    if (hi - lo < 12u)
        lo = lo > (12u - (hi - lo)) / 2u ? lo - (12u - (hi - lo)) / 2u : 0u, hi = lo + 12u;
    for (i = 0; i < 16u; i += 4u)                       /* the beats */
        sh_rect(x0 + (int32_t)i * cw, top, 1, h, sh_mix(SC_BG, SC_FAINT, 80));
    for (i = 0; i < 16u; i++) {
        const step_t *st = &t->step[(pg + i) % NSTEP];
        int32_t x = x0 + (int32_t)i * cw + 1, y;
        if (pg + i >= len) {
            sh_rect(x + 4, top + h / 2, 2, 2, SC_FAINT);
            continue;
        }
        if (st->time == ST_TIE && last) {               /* held on from the step before */
            sh_rect(x - 1, (int32_t)last, cw, 3, sh_mix(c, SC_BG, 50));
        } else if (st->n && st->time == ST_NOTE) {
            y = top + h - 4 - (int32_t)((st->note[0] - lo) * (uint32_t)(h - 8) / (hi - lo));
            sh_rect(x, y, cw - 3, 4, st->flags & SF_ACCENT ? SH_ACCENT : c);
            if (st->flags & SF_SLIDE)
                sh_line((float)(x + cw - 3), (float)(y + 2), (float)(x + cw + 1), (float)(y - 2), c);
            last = (uint32_t)y;
        } else {
            sh_rect(x + 4, top + h - 2, 2, 2, i % 4u == 0u ? SC_MID : SC_FAINT);
            last = 0;
        }
        if (song.playing && pg + i == t->seq_idx % len)
            sh_rect(x, top + h + 3, cw - 3, 2, SC_BONE);
        if (st->n)
            sh_step_marks(x, top + h + 6, sh_step_extras(t, pg + i, locks), SC_DIM);
        if (sh_acid_entry() && pg + i == sh_acid_cur)
            sh_rect(x - 1, top + h + 3, cw - 1, 2, c);
    }
    str_cpy(b, N_NOTE[t->p[P_ROOT] % 12], 4);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1)], 6);
    sh_text(x0, top + h + 10, &AF_S, sh_acid_entry() ? "PITCH: PLAY THE LINE" :
            sh.page[sh.mode] == SHP_LINE ? "DENS SPAN: SHARED WITH LINES" : song.playing ? "KEYS TRANSPOSE" :
            "HOLD A STEP: C#5 ACC D#5 SLD F#5 TIE", SC_DIM);
    sh_text(236 - text_w(&AF_S, b), top - 14, &AF_S, b, SC_MID);
}
/* RUDIMENT: the pattern as a necklace, a ring a lane (the lane being edited bright, its hits joined into a polygon,
 * an accent pale), the playhead a spoke; past 16 steps the hits off the page in view are fainter, and that page's
 * arc is marked outside the rings */
/* LANE 2 on the lanes' pictures: step i is in lane l's PAD (turned with the rest by ROT); the lane naps this bar */
static int sh_lane_pad_at(uint32_t l, uint32_t i)
{
    uint32_t n = sh_lane_len(l), rot = (uint32_t)clamp(sh_eu[2][l], 0, NSTEP - 1) % n;
    return sh_lane_pad(l) && i < n && (i + n - rot) % n >= sh_lane_span(l) && !sh_lane_logic(l);
}
static int sh_lane_napping(const track_t *t, uint32_t l) { return song.playing && lane_naps(t, sh_li, l % 8u); }
#define SH_WARM RGB(200, 140, 90)                       /* a lane's note off the scale */
static void sh_pic_rudiment(const sh_mode_t *m, const track_t *t)
{
    float cx = 120.0f, cy = 104.0f;
    uint32_t lane = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, len, l, i, pg = sh_pg(), paged = sh_pg_n() > 1u;
    uint64_t locks = motion_lock_steps(trk_index(t));
    if (paged) {                                        /* the page in view: its arc of the pattern */
        uint32_t plen = sh_pg_len(), e = pg + 16u < plen ? pg + 16u : plen;
        for (i = pg; i < e; i++) {
            float a = -SH_TAU / 4.0f + SH_TAU * ((float)i + 0.5f) / (float)plen;
            sh_disc(cx + 82.0f * sh_cos(a), cy + 82.0f * sh_sin(a), 1.0f, m->color);
        }
    }
    for (l = 0; l < 8u; l++) {
        float R = 76.0f - (float)l * 8.0f, hx[NSTEP], hy[NSTEP];
        uint32_t n = 0, cur = l == lane, nap = (uint32_t)sh_lane_napping(t, l);
        len = sh_lane_len(l);                           /* each ring has its own LEN, so they phase */
        sh_ring(cx, cy, R, cur && !nap ? SC_MID : SC_FAINT);
        if (sh_lx[SH_LX_LOOP][l])                       /* LOOP: a small loop at the ring's top */
            sh_ring(cx - 5.0f, cy - R, 2.0f, cur ? m->color : SC_MID);
        for (i = 0; i < len; i++) {
            float a = -SH_TAU / 4.0f + SH_TAU * (float)i / (float)len, x = cx + R * sh_cos(a), y = cy + R * sh_sin(a);
            if (sh_lane_logic(l) ? (sh_lc_hit[i] >> l) & 1u : (step_lanes(&t->step[i]) >> l) & 1u) {
                uint32_t e = cur && !sh_lane_logic(l) ? sh_step_extras(t, i, locks) : 0u;
                uint16_t c = t->step[i].time == ST_TIE ? SC_MID :
                             cur && ((step_accents(&t->step[i]) >> l) & 1u) ? SH_ACCENT : cur ? m->color : SC_DIM;
                if (nap)                                /* NAP: the lane resting, its hits dim */
                    c = sh_mix(c, SC_BG, 65);
                hx[n] = x;
                hy[n] = y;
                n++;
                sh_disc(x, y, cur ? 2.5f : 1.5f, paged && (i < pg || i >= pg + 16u) ? sh_mix(c, SC_BG, 45) : c);
                if (e)                                  /* a CHNC, RATCH, NUDGE (mid) or a lock (bone): a dot outside */
                    sh_disc(cx + (R + 5.0f) * sh_cos(a), cy + (R + 5.0f) * sh_sin(a), 1.0f,
                            e & SHX_LOCK ? SC_BONE : SC_MID);
            } else if (sh_lane_pad_at(l, i)) {          /* PAD: its empty steps, a faint tick */
                sh_rect((int32_t)x - 1, (int32_t)y - 1, 2, 2, SC_MID);
            } else if (sh_map_at(l, i)) {               /* the map's hit: smaller, fainter */
                hx[n] = x;
                hy[n] = y;
                n++;
                sh_disc(x, y, cur ? 2.0f : 1.0f, (sh_map_at(l, i) & 1u) && cur ? sh_mix(SH_ACCENT, SC_BG, 40) :
                        sh_mix(cur ? m->color : SC_DIM, SC_BG, 45));
            }
        }
        if (cur && n > 1u)
            for (i = 0; i < n; i++)
                sh_line(hx[i], hy[i], hx[(i + 1u) % n], hy[(i + 1u) % n], m->color);
        if (song.playing) {                             /* each ring's playhead */
            float a = -SH_TAU / 4.0f + SH_TAU * (float)sh_lane_now(l) / (float)len;
            sh_disc(cx + R * sh_cos(a), cy + R * sh_sin(a), 1.5f, SC_BONE);
        }
    }
    {                                                   /* the lane in front: its drum, or its note (the rows' colours:
                                                         * the root the nut's, off the scale warm) */
        uint16_t c = m->color;
        if (t->eng_req != ENGI_DRUM) {
            uint32_t mask = scale_mask(t), root = (uint32_t)t->p[P_ROOT] % 12u, note = shell_lane_note[lane];
            if (mask == 0xFFFu || !mask)
                mask = SCALE_MASK[1];
            c = !((mask >> ((note + 120u - root) % 12u)) & 1u) ? SH_WARM : note % 12u == root ? m->color : SC_BONE;
        }
        sh_text_c(120, 98, &AF_S, N_SP_LANE[lane % 8u], c);
    }
    if (sh_lane_logic(lane))                            /* a logic lane: its op under its name */
        sh_text_c(120, 110, &AF_S, N_SP_LOGIC[clamp(-sh_eu[1][lane] - 1, 0, 6)], SC_MID);
    sh_text(4, 182, &AF_S, t->eng_req == ENGI_DRUM ? "HOLD SEQ: ACCENTS" : "NOTES: THE ROWS", SC_DIM);
}

/* RUDIMENT on any SOUND but DRUMS: the lanes as rows of notes, the lowest at the bottom (Stepwise's view). A row
 * stops at its LEN (dots past it, the LEN at its end); every row shows the page in view (sh_pg), a lane past 16 its
 * pages as marks under its LEN; a hit sits low in its cell, an accent fills it; each row's playhead is a line under
 * its step, on its own page, and a row sounding now lights its name. The root's rows wear the nut's
 * colour, a lane set off the scale a warm one. Under the rows, each column's slide (a rising stroke) or tie (a
 * bar); under those, the scale and the step */
static void sh_pic_lines(const sh_mode_t *m, const track_t *t)
{
    const int32_t x0 = 36, cw = 11, top = 26, rh = 17;
    uint32_t lane = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, mask = scale_mask(t), root = (uint32_t)t->p[P_ROOT] % 12u;
    uint32_t r, i;
    uint64_t locks = motion_lock_steps(trk_index(t));
    int32_t yb = top + 8 * rh + 1;
    char b[24], v[8];
    if (mask == 0xFFFu || !mask)
        mask = SCALE_MASK[1];                           /* (CHR: MAJOR's degrees, as the lanes take them) */
    for (i = 0; i < 16u; i += 4u)                       /* the beats, faint behind the rows */
        sh_rect(x0 + (int32_t)i * cw, top - 3, 1, rh * 8 + 2, sh_mix(SC_BG, SC_FAINT, 80));
    for (r = 0; r < 8u; r++) {
        uint32_t l = 7u - r, len = sh_lane_len(l), now = sh_lane_now(l), note = shell_lane_note[l];
        uint32_t pg = sh_pg();                          /* (the page in view, every row) */
        int32_t y = top + (int32_t)r * rh;
        int cur = l == lane, nap = sh_lane_napping(t, l), sounding = song.playing && !nap && ((step_lanes(&t->step[now]) >> l) & 1u);
        int off = !((mask >> ((note + 120u - root) % 12u)) & 1u);
        uint16_t nc = nap ? SC_FAINT : sounding ? SC_BONE : off ? SH_WARM : note % 12u == root ? m->color : SC_DIM;
        if (cur)
            sh_rect(4, y - 1, x0 + 16 * cw + 20, rh - 2, sh_mix(SC_BG, m->color, 10));
        if (sounding)
            sh_rect(4, y + 3, 2, 9, m->color);
        sh_text(x0 - 5 - text_w(&AF_S, N_SP_LANE[l]), y + 1, &AF_S, N_SP_LANE[l], nc);
        for (i = 0; i < 16u; i++) {
            int32_t x = x0 + (int32_t)i * cw + 1;
            const step_t *s = &t->step[pg + i];
            uint32_t on = (step_lanes(s) >> l) & 1u, acc = (step_accents(s) >> l) & 1u, lg = (uint32_t)sh_lane_logic(l);
            if (lg) {                                        /* a logic lane: what its op plays */
                on = (sh_lc_hit[pg + i] >> l) & 1u;
                acc = (sh_lc_acc[pg + i] >> l) & 1u;
            }
            if (!lg && pg + i >= len) {                      /* past the lane's LEN */
                sh_rect(x + 4, y + 6, 2, 2, SC_FAINT);
                continue;
            }
            if (!on && sh_map_at(l, pg + i)) {          /* the map's hit: faint */
                sh_rect(x + 2, y + 4, cw - 6, rh - 9, sh_mix(cur ? m->color : SC_DIM, SC_BG, 45));
            } else if (!on && sh_lane_pad_at(l, pg + i)) {   /* PAD: a faint dash, no beat */
                sh_rect(x + 2, y + 7, cw - 6, 1, SC_FAINT);
            } else if (!on) {
                sh_rect(x + 4, y + 6, 2, 2, i % 4u == 0u ? SC_MID : SC_FAINT);
            } else if (!lg && step_loct(s, l) != sh_lo()) {   /* in another octave: a tick at the top or the bottom */
                int32_t up = step_loct(s, l) > sh_lo();
                sh_rect(x + 1, up ? y + 1 : y + rh - 6, cw - 4, 2, sh_mix(cur ? m->color : SC_DIM, SC_BG, 30));
            } else {
                int32_t h = acc ? rh - 4 : rh - 8;
                uint16_t c = acc ? SH_ACCENT : cur ? m->color : sh_mix(m->color, SC_BG, 35);
                if (nap)                                /* NAP: resting, dim */
                    c = sh_mix(c, SC_BG, 65);
                sh_rect(x + 1, y + 1 + (rh - 4 - h), cw - 4, h,
                        s->time == ST_TIE ? sh_mix(c, SC_BG, 60) : c);   /* (under a TIE: held, faint) */
            }
            if (song.playing && pg + i == now)
                sh_rect(x, y + rh - 3, cw - 2, 1, SC_BONE);
        }
        fmt_int(v, (int32_t)len);
        if (sh_eu[1][l] < 0 && l >= 2u)                 /* a logic lane: its op where its LEN would be (25 px: LAT) */
            str_cpy(v, sh_eu[1][l] <= -7 ? "LAT" : N_SP_LOGIC[clamp(-sh_eu[1][l] - 1, 0, 6)], 8);
        sh_text(x0 + 16 * cw + 3, y + 1, &AF_S, v, cur ? m->color : SC_MID);
        if (sh_lx[SH_LX_LOOP][l])                       /* LOOP: a small loop after its LEN */
            sh_ring((float)(x0 + 16 * cw + 7 + text_w(&AF_S, v)), (float)(y + 7), 2.0f, cur ? m->color : SC_MID);
        if (len > 16u) {                                /* (its pages, the one in view bright) */
            uint32_t q;
            for (q = 0; q < (len + 15u) / 16u; q++)
                sh_rect(x0 + 16 * cw + 3 + 4 * (int32_t)q, y + rh - 3, 3, 1, q == pg / 16u ? SC_BONE : SC_FAINT);
        }
    }
    for (i = 0; i < 16u; i++) {                         /* each column's slide or tie, on the page in front; under */
        const step_t *s = &t->step[sh_pg() + i];        /* it, its CHNC RATCH NUDGE and locks */
        float x = (float)(x0 + (int32_t)i * cw + 1);
        if (s->flags & SF_SLIDE)
            sh_line(x + 1.0f, (float)yb + 5.0f, x + (float)cw - 2.0f, (float)yb + 1.0f, m->color);
        else if (s->time == ST_TIE)
            sh_rect((int32_t)x, yb + 3, cw, 2, SC_DIM);
        if (step_lanes(s))
            sh_step_marks((int32_t)x, yb + 7, sh_step_extras(t, sh_pg() + i, locks), SC_DIM);
    }
    str_cpy(b, N_NOTE[root], 4);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1)], 6);
    str_cpy(b + str_len(b), "  ", 3);
    str_cpy(b + str_len(b), N_DIV[clamp(t->p[P_SDIV], 0, (int32_t)NELEM(N_DIV) - 1)], 6);
    sh_text(x0 + 16 * cw + 18 - text_w(&AF_S, b), yb + 12, &AF_S, b, SC_MID);
    if (sh_lo()) {                                     /* the octave in view, when not the lanes' own */
        str_cpy(b, "OCTAVE ", sizeof b);
        if (sh_lo() > 0)
            str_cpy(b + str_len(b), "+", 2);
        fmt_int(b + str_len(b), sh_lo());
        sh_text(4, yb + 12, &AF_S, b, m->color);
    } else
        sh_text(4, yb + 12, &AF_S, sh.page[sh.mode] == SHP_LINE ? "DENS SPAN: SHARED WITH ACID" : "HOLD SEQ: MARKS",
                SC_DIM);                                /* (the marks have no page: holding SEQ shows them) */
}

/* ALIVE: its rows as a space-time diagram of triangles, the newest at the bottom, the cell that plays gilt */
static void sh_pic_alive(const sh_mode_t *m, const track_t *t)
{
    uint32_t r, i, h = al.head;
    float w = 5.0f, hh = 4.3f, x0 = 120.0f - (float)AL_W * 2.5f;
    for (r = 0; r < AL_ROWS; r++) {
        uint32_t row = (h + 1u + r) % AL_ROWS;
        float y = 24.0f + (float)r * hh;
        for (i = 0; i < AL_W; i++)
            if (al_cell(row, i)) {
                float x = x0 + (float)i * w;
                uint16_t c = i == AL_C ? m->color : r == AL_ROWS - 1u ? SC_BONE : SC_MID;
                sh_line(x, y, x + w, y, c);
                sh_line(x, y, x + w * 0.5f, y + hh, c);
                sh_line(x + w, y, x + w * 0.5f, y + hh, c);
            }
    }
    if (!song.playing)
        sh_text_c(120, 182, &AF_S, "PLAY: IT GROWS ON THE CLOCK", SC_MID);
    (void)t;
}

/* the SEQ page: the 16 steps of the page in front under the picture, their notes' heights, the playhead */
/* a step's extras as marks in a 10 x 3 strip from x, y: a dot CHNC, a tick RATCH, a dash NUDGE, a diamond a lock */
static void sh_step_marks(int32_t x, int32_t y, uint32_t e, uint16_t c)
{
    if (e & SHX_CHNC)
        sh_rect(x, y + 1, 2, 2, c);
    if (e & SHX_RATCH)
        sh_rect(x + 3, y, 1, 3, c);
    if (e & SHX_NUDGE)
        sh_rect(x + 5, y + 1, 2, 1, c);
    if (e & SHX_LOCK) {
        sh_rect(x + 8, y, 1, 1, SC_BONE);
        sh_rect(x + 7, y + 1, 1, 1, SC_BONE);
        sh_rect(x + 9, y + 1, 1, 1, SC_BONE);
        sh_rect(x + 8, y + 2, 1, 1, SC_BONE);
    }
}
static void sh_draw_steps(const sh_mode_t *m, const track_t *t)
{
    uint32_t i, len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u, pg = sh_pg();
    uint64_t locks = motion_lock_steps(trk_index(t));
    char b[32];
    sh_rect(8, 132, 224, 58, SC_PANEL);
    if (sh_pg_n() > 1u) {                               /* more than one page: which, and how to turn it */
        str_cpy(b, "STEPS ", 8);
        fmt_int(b + str_len(b), (int32_t)pg + 1);
        str_cpy(b + str_len(b), "-", 2);
        fmt_int(b + str_len(b), (int32_t)(pg + 16u < len ? pg + 16u : len));
        str_cpy(b + str_len(b), "  C#5/D#5", 16);
        sh_text_c(120, 135, &AF_S, b, SC_DIM);
    } else
        sh_text_c(120, 135, &AF_S, "HOLD SEQ: KEYS ARE STEPS", SC_DIM);
    for (i = 0; i < 16u; i++) {
        int32_t x = 12 + (int32_t)i * 14;
        const step_t *st = &t->step[pg + i];
        uint16_t c = pg + i >= len ? SC_FAINT : st->n ? m->color : (i % 4u == 0 ? SC_MID : SC_FAINT);
        if (song.playing && pg + i == t->seq_idx % len)
            sh_rect(x, 152, 12, 36, sh_mix(SC_PANEL, SC_BONE, 18));
        if (st->n && pg + i < len) {
            int32_t h = clamp((int32_t)st->note[0] - 36, 0, 48) * 30 / 48;
            sh_rect(x + 2, 184 - h, 8, 2, c);
            sh_step_marks(x + 1, 187, sh_step_extras(t, pg + i, locks), SC_DIM);
        }
        sh_rect(x, 186, 12, 1, c);
    }
}

/* log2 for the screen (no libm): the exponent's bits and a cubic on the mantissa, within 0.01 */
static float sh_log2(float x)
{
    union { float f; uint32_t u; } v = {x > 1e-30f ? x : 1e-30f};
    float e = (float)((int32_t)((v.u >> 23) & 0xFFu) - 127), m;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;           /* the mantissa, 1 .. 2 */
    m = v.f - 1.0f;
    return e + m * (1.4425449f + m * (-0.7181452f + m * 0.2729713f));
}
/* the INSERT pages under FILTER: its response, 20 Hz .. 20 kHz across, +24 .. -32 dB down, from the coefficients as
 * they stand (KTRK and the follower included, so it moves with the playing); the cutoff in bone. Under COMB: its
 * teeth, every harmonic of its pitch, or the odd ones with FDBK under 64, fading up the spectrum */
static void sh_pic_filter(const sh_mode_t *m, const track_t *t)
{
    const int32_t x0 = 16, x1 = 224, y0 = 104;
    const ins_t *s = &ins[(uint32_t)(t - trk) % NTRK];
    int comb = t->p[P_ITYPE] == IT_COMB;
    fl_t rest;
    const fl_t *f = &s->u.fl;
    fl_param_t p = ins_flp(t);
    float hz, fc, px = 0.0f, py = 0.0f;
    uint32_t i;
    static const uint16_t GRID[3] = {100, 1000, 10000};
    if (s->type != t->p[P_ITYPE]) {                     /* (not running yet: as it starts) */
        fl_init(&rest);
        f = &rest;
    }
#define SH_FX(h) ((float)x0 + (float)(x1 - x0) * sh_log2((h) / 20.0f) * (1.0f / 9.9658f))
    for (i = 0; i < 3u; i++)
        sh_line(SH_FX((float)GRID[i]), 34.0f, SH_FX((float)GRID[i]), 172.0f, SC_FAINT);
    sh_line((float)x0, (float)y0, (float)x1, (float)y0, SC_FAINT);   /* 0 dB */
    fc = fl_freq(f, &p);
    if (!comb) {
        for (i = 0; i <= 60u; i++) {
            float db, x, y;
            hz = 20.0f * sh_exp2((float)i * (9.9658f / 60.0f));
            db = 6.0206f * sh_log2(fl_mag(f, &p, hz));
            db = db > 24.0f ? 24.0f : db < -32.0f ? -32.0f : db;
            x = SH_FX(hz);
            y = (float)y0 - db * 2.4f;
            if (i)
                sh_line(px, py, x, y, m->color);
            px = x;
            py = y;
        }
    } else {
        uint32_t k, odd = p.res < 64u;
        for (k = 1; k < 64u && (float)k * fc < 20000.0f; k += odd ? 2u : 1u) {
            float x = SH_FX((float)k * fc), h = 56.0f / (1.0f + 0.25f * (float)k);
            sh_line(x, (float)y0, x, (float)y0 - h, k == 1u ? SC_BONE : m->color);
        }
    }
    if (fc > 20.0f && fc < 20000.0f)                    /* the cutoff, or the pitch */
        sh_line(SH_FX(fc), 38.0f, SH_FX(fc), 168.0f, SC_BONE);
#undef SH_FX
    {
        char b[16];
        fmt_int(b, (int32_t)(fc + 0.5f));
        str_cpy(b + str_len(b), " HZ", 4);
        sh_text_c(120, 176, &AF_S, b, SC_MID);
    }
}

/* MOVE: the scale every part plays in now, large, and home under it */
static void sh_pic_move(const sh_mode_t *m, const track_t *t)
{
    char b[24];
    uint32_t r = (uint32_t)t->p[P_ROOT] % 12u, sc = (uint32_t)clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1);
    str_cpy(b, N_NOTE[r], 4);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), N_SCALE[sc], 8);
    sh_text_c(120, 70, &AF_S, sh_mv.on ? "PLAYING IN" : !sh_sp[SP_MEVERY - SP_FIRST] ? "MOVE IS OFF" : "PLAY STARTS IT", SC_DIM);
    sh_text_c(120, 90, &AF_M, b, m->color);
    str_cpy(b, "HOME ", 6);
    str_cpy(b + str_len(b), N_NOTE[(sh_mv.on ? sh_mv.root[sh.part % NTRK] : r) % 12u], 4);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), N_SCALE[clamp(sh_mv.on ? sh_mv.scale[sh.part % NTRK] : sc, 0, (int32_t)NELEM(N_SCALE) - 1)], 8);
    sh_text_c(120, 128, &AF_S, b, SC_MID);
    if (!sc)                                            /* (the warnings: a move that would be heard as none) */
        sh_text_c(120, 150, &AF_S, "CHR: PICK A SCALE ON SCALE", SH_WARM);
    else if (sh_sp[SP_MHOW - SP_FIRST] == 2 && sh_sp[SP_MALT - SP_FIRST] == (sh_mv.on ? sh_mv.scale[sh.part % NTRK] : (uint8_t)sc))
        sh_text_c(120, 150, &AF_S, "ALT IS HOME: PICK ANOTHER", SH_WARM);
}

/* HARMONY: what a note brings, shown on the scale's root (the NOTE engine in front's settings), or why it plays none */
static void sh_pic_harm(const sh_mode_t *m, const track_t *t)
{
    char b[40];
    int16_t *hm = sh_hm[sh.note % SH_NNOTES];
    uint8_t c[4];
    int32_t h[3];
    uint32_t root = 60u + (uint32_t)t->p[P_ROOT] % 12u, n, i, k;
    const char *why = sh_harm_why(sh.part);
    str_cpy(b, "FOR ", 5);
    str_cpy(b + str_len(b), SH_NOTES[sh.note % SH_NNOTES].name, 12);
    sh_text_c(120, 64, &AF_S, b, SC_DIM);
    sh_text_c(120, 90, &AF_M, N_SP_HMTYPE[clamp(hm[0], 0, HM_N - 1)], hm[0] ? m->color : SC_MID);
    for (k = 0; k < 4u; k++)
        c[k] = (uint8_t)hm[k];
    n = hm[0] ? harm_notes(t, c, root, h) : 0u;
    str_cpy(b, N_NOTE[root % 12u], 4);                  /* (the played note first, then what it brings, low to high) */
    for (i = 0; i < n; i++) {
        uint32_t x = (uint32_t)clamp(h[i], 0, 127);
        str_cpy(b + str_len(b), x < root ? " < " : " + ", 4);
        str_cpy(b + str_len(b), N_NOTE[x % 12u], 4);
    }
    sh_text_c(120, 128, &AF_S, hm[0] ? b : "EACH NOTE PLAYS ALONE", SC_MID);
    if (why)
        sh_text_c(120, 150, &AF_S, why, SH_WARM);
}

#include "shell_pics.c"                  /* the pictures of the engines the flower stood in for */

static uint8_t sh_pic_eng;                              /* the engine whose picture was drawn last (SH_NONE: a NOTE
                                                         * engine's or a page's own) */
static void sh_draw_picture(void)
{
    const sh_mode_t *m = sh_cur();
    const track_t *t = sh_trk();
    uint32_t f = SHP_FAM[sh.page[sh.mode]];
    int sound = !sh.global && !sh_page_note(sh.page[sh.mode]);   /* the SOUND view shows the engine's picture */
    (void)f;
    sh_pic_eng = SH_NONE;

    if (sh.global == SHG_MOVE) {
        sh_pic_move(m, t);
        return;
    }
    if (sh.global == SHG_HARM) {
        sh_pic_harm(m, t);
        return;
    }

    if (!sh.global && (sh.page[sh.mode] == SHP_INS || sh.page[sh.mode] == SHP_INS2) &&
        (t->p[P_ITYPE] == IT_FILTER || t->p[P_ITYPE] == IT_COMB)) {
        sh_pic_filter(m, t);
        return;
    }
    if (sh.page[sh.mode] == SHP_DRUM && sh_page_ok(SHP_DRUM)) {   /* DRUM: the drum it sets, large */
        sh_text_c(120, 70, &AF_S, "THE DRUM IN FRONT", SC_DIM);
        sh_text_c(120, 90, &AF_M, drum_lane_name(sh_trk(), sh_drum_piece()), m->color);
        sh_text_c(120, 128, &AF_S, sh_kind() == SHK_RUDIMENT ? "A LANE KEY PICKS ANOTHER" : "STRIKE ANOTHER TO PICK IT",
                  SC_MID);
        return;
    }
    if (!sound && sh_kind() == SHK_RUDIMENT) {          /* rings: DRUMS, and RUDIMENT under any SOUND but on its NOTES
                                                         * page, where the pitches are set; rows of notes for LINES */
        if (t->eng_req == ENGI_DRUM || (sh.note == SH_N_RUDIMENT && sh.page[sh.mode] != SHP_NOTE2))
            sh_pic_rudiment(m, t);
        else
            sh_pic_lines(m, t);
        return;
    }
    if (sh.page[sh.mode] == SHP_SPELL && sh_page_ok(SHP_SPELL)) { sh_pic_spell(m, t); return; }
    if (!sound && sh_kind() == SHK_CHORD) { sh_pic_chord(m, t); return; }
    if (!sound && sh_kind() == SHK_ALIVE) { sh_pic_alive(m, t); return; }
    if (!sound && sh_kind() == SHK_NE) { sh_pic_ne(t); return; }
    if (!sound && sh_kind() == SHK_ACID) { sh_pic_acid(m, t); return; }
    sh_pic_eng = (uint8_t)sh_eng();
    switch (sh_eng()) {
    case ENGI_FM6: sh_pic_fm6(m, t); break;
    case ENGI_LIZARD: sh_pic_lizard(m, t); break;
#if FELUCCA_VODER
    case ENGI_VODER: sh_pic_voder(m, t); break;
#endif
#if FELUCCA_TALK
    case ENGI_TALK: sh_pic_talk(m, t); break;
#endif
#if FELUCCA_SYLLABLE
    case ENGI_SYLLABLE: sh_pic_syllable(m, t); break;
#endif
#if FELUCCA_ORGAN
    case ENGI_ORGAN: sh_pic_organ(t); break;
#endif
    case ENGI_MOUTH: sh_pic_mouth(m, t); break;
#if FELUCCA_LOOP
    case ENGI_LOOP: sh_pic_loop(m, t); break;
#endif
    case ENGI_PHYS: sh_pic_phys(m, t); break;
    case 0: sh_pic_analog(m, t); break;              /* ANALOG */
#if FELUCCA_PHASE
    case 2: sh_pic_phase(m, t); break;               /* PHASE */
#endif
#if FELUCCA_TRIO
    case 6: sh_pic_trio(m, t); break;                /* TRIO */
#endif
    case ENGI_SHOCK: sh_pic_shock(m, t); break;
#if FELUCCA_LOFI
    case 3: sh_pic_lofi(m, t); break;                /* LOFI */
#endif
#if FELUCCA_SAMPLE
    case ENGI_SAMPLE: sh_pic_sample(m, t); break;
#endif
#if FELUCCA_GRAIN
    case 8: sh_pic_grain(m, t); break;               /* GRAIN */
#endif
#if FELUCCA_VOICE
    case 5: sh_pic_voice(m, t); break;               /* VOICE */
#endif
#if FELUCCA_WHEEL
    case 7: sh_pic_wheel(m, t); break;               /* WHEEL */
#endif
#if FELUCCA_NOISE
    case 11: sh_pic_noise(m, t); break;              /* NOISE */
#endif
#if FELUCCA_SLICE
    case ENGI_SLICE: sh_pic_slice(m, t); break;
#endif
    case ENGI_DRUM: sh_pic_drums(m, t); break;
    default: sh_flower(120.0f, 104.0f, 34.0f); break;
    }
}

/* hold-to-see: SEQ or REC held (past the tap, sh_b's hold) puts a map of what that button + a control does for the
 * pair in front in the knob cards' band, only what applies now; let go, the cards are back. A knob turned while it
 * is held shows the cards again for as long as a card stays lit, so the turn is seen. Each item asks the code that
 * acts (the predicates below mirror sh_keys, sh_step_key, sh_encoders and sh_tap; tests/shell_test.c checks them
 * against what the keys do, so a change there that is not made here fails) */
#define SH_HS_N 8u
#define SH_HS_GAP 14                                    /* between items on a row */
typedef struct {
    uint32_t n;
    uint8_t row[SH_HS_N];
    const char *key[SH_HS_N], *act[SH_HS_N];
} sh_hs_t;
static char sh_hs_pg[12];                               /* "PAGE 2/4" */
static void sh_hs_add(sh_hs_t *h, uint32_t row, const char *key, const char *act)
{
    if (h->n < SH_HS_N) {
        h->row[h->n] = (uint8_t)row;
        h->key[h->n] = key;
        h->act[h->n++] = act;
    }
}
/* REC held: a white key erases its step (sh_keys -> sh_erase_key), a lane key its lane */
static int sh_rec_key_erases(void)
{
    return (sh_kind() == SHK_RUDIMENT || sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD || sh_kind() == SHK_ACID) &&
           !sh.wheel;
}
static int sh_rec_lane_erases(void) { return sh_rec_key_erases() && sh_kind() == SHK_RUDIMENT; }
/* REC held: SELECT walks REC MODE (sh_encoders), PLAY counts in (sh_tap) */
static int sh_rec_sel_mode(void) { return !sh.wheel; }
static int sh_rec_counts_in(void) { return !song.playing && !seq_counting(); }
static int sh_rec_play_arms(void) { return song.playing && !seq_counting(); }   /* REC + PLAY running: armed (sh_tap) */
/* SEQ held: what a white key marks or toggles (sh_step_key) */
static const char *sh_seq_white(void)
{
    if (sh_kind() == SHK_RUDIMENT && sh_trk()->eng_req == ENGI_DRUM)   /* a kit: ACCENT on a hit of the lane in front */
        return sh_lane_logic((uint32_t)sh_sp[SP_LANE - SP_FIRST]) ? "NO ACCENT: LOGIC LANE" : "ACCENT";
    if (sh_kind() == SHK_RUDIMENT)
        return "ACC SLD TIE";
    if (sh_kind() == SHK_CHORD && chord_last[sh.part].n > 1u)   /* CHORD: the chord last played, whole */
        return "LAST CHORD ON/OFF";
    return sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD || sh_kind() == SHK_ACID ? "STEP ON/OFF" : 0;   /* (the
                                                                                                      * last key's note) */
}
/* what REC's first ring undoes, as its ring says it */
static const char *sh_ring_word(void)
{
    return project_new_kept() ? "UNDO NEW" : sh_sound_first() ? (undo.what & SH_UNDO_RESET ? "UNDO RESET" : "UNDO SOUND") : sh_take_here() ? (sh_take.rhy ? "UNDO BEAT" : "UNDO TAKE") : "NO TAKE";
}
/* the button whose map shows: B_SEQ, B_REC, or NB (the knob cards). REC first where its keys erase, as sh_keys
 * takes REC before SEQ there; elsewhere SEQ's keys are the steps */
static uint32_t sh_hs_which(void)
{
    int seq = sh_b[B_SEQ].down && sh_b[B_SEQ].hold, rec = sh_b[B_REC].down && sh_b[B_REC].hold;
    if (sh.wheel || sh_held(B_GLO) || sh.help || sh.save || sh.pq || sh_splsv.open || sh.mom ||
        (sh.hot < 4u && sh_fresh(sh.hot_t, 1300u)))
        return NB;
    if (rec && (sh_rec_key_erases() || !seq))
        return B_REC;
    return seq ? B_SEQ : NB;
}
static int sh_hs_page(sh_hs_t *h, uint32_t row)         /* SEQ + C#5 / D#5: only past 16 steps */
{
    if (!sh_pg_kind() || sh_pg_n() < 2u)
        return 0;
    str_cpy(sh_hs_pg, "PAGE ", 6);
    fmt_int(sh_hs_pg + str_len(sh_hs_pg), (int32_t)(sh_pg() / 16u + 1u));
    str_cpy(sh_hs_pg + str_len(sh_hs_pg), "/", 2);
    fmt_int(sh_hs_pg + str_len(sh_hs_pg), (int32_t)sh_pg_n());
    sh_hs_add(h, row, "C#5/D#5", sh_hs_pg);
    return 1;
}
static void sh_seq_map(sh_hs_t *h)                     /* a row each: the white keys, the pages, OCT+ and OCT- */
{
    const track_t *t = sh_trk();
    const char *w = sh_seq_white();
    uint32_t r = 0;
    if (w)
        sh_hs_add(h, r++, "WHITE", w);
    if (sh_hs_page(h, r))
        r++;
    if (sh_double_now() && t->p[P_SLEN] > 1)
        sh_hs_add(h, r, "OCT+/-", "DOUBLE/HALF");
    else if (sh_double_now())
        sh_hs_add(h, r, "OCT+", "DOUBLE");
    else if (!sh_double_ok())
        sh_hs_add(h, r, "OCT+", "NOTHING TO DOUBLE");
    else if (sh_take.first && rec_on(t))
        sh_hs_add(h, r, "OCT+", "DOUBLE: TAP REC FIRST");
    else if (t->p[P_SLEN] >= NSTEP)
        sh_hs_add(h, r, "OCT+/-", "FULL/HALF");
}
/* the width of a row's items so far, and with one more (key, act) */
static int32_t sh_hs_row_w(const sh_hs_t *h, uint32_t row, const char *key, const char *act)
{
    uint32_t i;
    int32_t w = text_w(&AF_S, key) + 5 + text_w(&AF_S, act);
    for (i = 0; i < h->n; i++)
        if (h->row[i] == row)
            w += text_w(&AF_S, h->key[i]) + 5 + text_w(&AF_S, h->act[i]) + SH_HS_GAP;
    return w;
}
static void sh_rec_map(sh_hs_t *h)                     /* a row each: ERASE, the rings, SELECT (PLAY beside it, room left) */
{
    uint32_t r = 0;
    if (sh_rec_key_erases()) {                          /* REC held as ERASE (playing, a lane held loses its hits as */
        sh_hs_add(h, r, "WHITE", "ERASE STEP");         /* the playhead passes them: sh_erase_poll) */
        if (sh_rec_lane_erases())
            sh_hs_add(h, r, "LANE", song.playing ? "ERASE HITS" : "ERASE LANE");
        r++;
    }
    if (sh.ring == B_REC + 1u) {                        /* the rings run (none once a key or SELECT made REC a modifier) */
        sh_hs_add(h, r, "RING 1", sh_ring_word());
        sh_hs_add(h, r++, "RING 2", "CLEAR");
    }
    if (sh_freeze_ok() && r < 3u)
        sh_hs_add(h, r++, "OCT+", "FREEZE");
    if (sh_rec_sel_mode())
        sh_hs_add(h, r++, "SELECT", N_SH_RECM[sh_rec_mode()]);
    if (sh_rec_counts_in() && r && sh_hs_row_w(h, r - 1u, "PLAY", "COUNT IN") <= 228)
        sh_hs_add(h, r - 1u, "PLAY", "COUNT IN");
    else if (sh_rec_counts_in() && r < 3u)
        sh_hs_add(h, r, "PLAY", "COUNT IN");
    else if (sh_rec_play_arms() && r && sh_hs_row_w(h, r - 1u, "PLAY", "REC ON") <= 228)
        sh_hs_add(h, r - 1u, "PLAY", "REC ON");
    else if (sh_rec_play_arms() && r < 3u)
        sh_hs_add(h, r, "PLAY", "REC ON");
}
static void sh_hs_map(sh_hs_t *h, uint32_t b)
{
    h->n = 0;
    if (b == B_SEQ)
        sh_seq_map(h);
    else if (b == B_REC)
        sh_rec_map(h);
}
/* where each item goes: up to three rows, left to right; x < 0 for one that would not fit. Returns those left out */
static uint32_t sh_hs_layout(const sh_hs_t *h, int32_t *x, int32_t *y)
{
    uint32_t i, rows = 0, cut = 0;
    int32_t px[3] = {6, 6, 6};
    for (i = 0; i < h->n; i++)
        rows = h->row[i] + 1u > rows ? h->row[i] + 1u : rows;
    for (i = 0; i < h->n; i++) {
        uint32_t r = h->row[i] < 3u ? h->row[i] : 2u;
        int32_t w = text_w(&AF_S, h->key[i]) + 5 + text_w(&AF_S, h->act[i]);
        y[i] = rows > 2u ? SH_Y_LEG + 2 + 14 * (int32_t)r : SH_Y_LEG + 6 + 18 * (int32_t)r;
        if (px[r] + w > 234) {
            x[i] = -1;
            cut++;
            continue;
        }
        x[i] = px[r];
        px[r] += w + SH_HS_GAP;
    }
    return cut;
}
/* nothing held, the lanes empty (no hit on any lane, RUDIMENT or LINES), the NOTE view in front (its ALGORITHM is the
 * rhythm): the band says how a beat starts, until a knob is turned (its card shows) or a hit is placed */
static int sh_lanes_hint(void)
{
    const track_t *t = sh_trk();
    uint32_t i;
    if (sh_kind() != SHK_RUDIMENT || sh.wheel || sh.global || !sh_view_note() || sh_held(B_GLO) ||
        (sh.hot < 4u && sh_fresh(sh.hot_t, 1300u)))
        return 0;
    for (i = 0; i < NSTEP; i++)
        if (step_lanes(&t->step[i]))
            return 0;
    return 1;
}
static void sh_draw_lanes_hint(void)
{
    sh_line(0.0f, SH_Y_LEG - 0.5f, 240.0f, SH_Y_LEG - 0.5f, SC_FAINT);
    sh_text_c(120, SH_Y_LEG + 15, &AF_S, "ALGORITHM: A BEAT  WHITE: STEPS", SC_DIM);
}
static uint32_t sh_hs_sig(void)                         /* what the band shows, for sh_signature */
{
    sh_hs_t h;
    uint32_t b = sh_hs_which(), s = b, i, k;
    if (b == NB)
        return sh_lanes_hint() ? s + 1u : s;
    sh_hs_map(&h, b);
    for (i = 0; i < h.n; i++)
        for (k = 0; h.act[i][k]; k++)
            s = (s ^ (uint8_t)h.act[i][k]) * 16777619u;
    return s ^ h.n << 24;
}
/* the map in the cards' band: each item its control (the pair's colour) and what it does (bone) */
static void sh_draw_holdsee(uint32_t b)
{
    const sh_mode_t *m = sh_cur();
    sh_hs_t h;
    int32_t x[SH_HS_N], y[SH_HS_N];
    uint32_t i;
    sh_hs_map(&h, b);
    (void)sh_hs_layout(&h, x, y);
    sh_line(0.0f, SH_Y_LEG - 0.5f, 240.0f, SH_Y_LEG - 0.5f, SC_FAINT);
    for (i = 0; i < h.n; i++)
        if (x[i] >= 0) {
            sh_text(x[i], y[i], &AF_S, h.key[i], m->color);
            sh_text(x[i] + text_w(&AF_S, h.key[i]) + 5, y[i], &AF_S, h.act[i], SC_BONE);
        }
}

static int sh_vce_ruled(uint32_t k)                     /* knob k is VCE on a part CHORD plays: POLY there */
{
    uint8_t ids[4];
    int glob;
    if (sh_fmop_page())
        return 0;
    sh_page_ids(ids, &glob);
    return !glob && ids[k] == P_VOICE && (shell_chord_play >> sh.part) & 1u;
}
static void sh_draw_legend(void)
{
    uint32_t k;
    char val[12];
    const char *label;
    int hot;
    sh_line(0.0f, SH_Y_LEG - 0.5f, 240.0f, SH_Y_LEG - 0.5f, SC_FAINT);
    for (k = 0; k < 4u; k++) {
        int32_t x = (int32_t)k * 60, f = sh_param_frac(k);
        hot = sh.hot == k && sh_fresh(sh.hot_t, 1300u);
        if (hot)
            sh_rect(x + 1, SH_Y_LEG + 1, 58, 43, sh_mix(SC_BG, SC_KNOB[k], 14));
        sh_param_text(k, &label, val);
        sh_poly((float)x + 8.0f, (float)SH_Y_LEG + 9.0f, 2.0f, 0, 0.0f, SC_KNOB[k]);
        sh_text(x + 13, SH_Y_LEG + 3, &AF_S, label, hot ? SC_KNOB[k] : SC_DIM);
        sh_text_c(x + 30, SH_Y_LEG + 17, &AF_M, val, sh_vce_ruled(k) ? SC_DIM : SC_BONE);   /* (CHORD: POLY) */
        sh_rect(x + 8, SH_Y_LEG + 38, 44, 1, SC_FAINT);
        sh_rect(x + 8, SH_Y_LEG + 38, 44 * f / 256, 1, hot ? SC_KNOB[k] : SC_MID);
        if (k)
            sh_rect(x, SH_Y_LEG + 4, 1, 36, SC_FAINT);
    }
}

static void sh_card(const char *title, const char *const *rows, uint32_t n, uint16_t col)
{
    int32_t h = 26 + (int32_t)n * 15, y = 108 - h / 2;
    uint32_t i;
    sh_rect(14, y, 212, h, SC_PANEL);
    sh_rect(14, y, 212, 1, col);
    sh_text(22, y + 5, &AF_S, title, col);
    for (i = 0; i < n; i++)
        sh_text(22, y + 22 + (int32_t)i * 15, &AF_S, rows[i], SC_BONE);
}

/* ------------------------------------------------------------------------------------------- the brand --- */
/* the brand pictures (tools/gen_brand.py): Nibs, acidcat's mascot, and the DATABURN wordmark, run-length coded over
 * a palette with alpha. The brand rules: the wordmark keeps its own 4.69:1 ratio, cream on #14140f, alone; Nibs sits
 * on acidcat's paper, on a screen of his own, never with the wordmark; neither appears on a boot-recovery, error or
 * performance screen. The art is CC-BY-SA-4.0 (assets/brand/LICENSE.txt); the names are not licensed as marks */
#define SC_PAPER RGB(243, 241, 236)          /* acidcat's paper: Nibs's own ground */

/* a picture at x, y; ink != 0: every pixel that colour (a one-ink mark), its coverage kept */
static void sh_brand(int32_t x, int32_t y, const uint8_t *rle, uint32_t n, const uint16_t *col, const uint16_t *a,
                     int32_t w, int32_t rows, uint16_t ink)
{
    int32_t i = 0;
    uint32_t k, r;
    for (k = 0; k < n && i < rows * w; k++) {
        uint32_t ix = rle[k] & 31u, run = (rle[k] >> 5) + 1u;
        for (r = 0; r < run; r++, i++)
            if (a[ix])
                sh_px(x + i % w, y + i / w, ink ? ink : col[ix], a[ix]);
    }
}
#define SH_BRAND_ROWS(x, y, P, rows, ink) \
    sh_brand(x, y, BR_##P##_RLE, sizeof BR_##P##_RLE, BR_##P##_COL, BR_##P##_A, BR_##P##_W, rows, ink)
#define SH_BRAND(x, y, P, ink) SH_BRAND_ROWS(x, y, P, BR_##P##_H, ink)

/* a mask (gen_brand.py, BR_*_MASK 1) at x, y in ink: bits 0..3 the coverage, 4..7 the run - 1; 0xF0, n: 16 + n clear */
static void sh_mask(int32_t x, int32_t y, const uint8_t *rle, uint32_t n, int32_t w, int32_t rows, uint16_t ink)
{
    int32_t i = 0, end = rows * w;
    uint32_t k;
    for (k = 0; k < n && i < end; k++) {
        uint32_t v = rle[k] & 15u, run = (rle[k] >> 4) + 1u, r;
        if (rle[k] == 0xF0u && k + 1u < n) {
            i += 16 + rle[++k];                         /* a clear run: nothing to draw */
            continue;
        }
        for (r = 0; r < run; r++, i++)
            if (v && i < end)
                sh_px(x + i % w, y + i / w, ink, (int32_t)(v * 256u + 7u) / 15);
    }
}
#define SH_MASK_ROWS(x, y, P, rows, ink) sh_mask(x, y, BR_##P##_RLE, sizeof BR_##P##_RLE, BR_##P##_W, rows, ink)
#define SH_MASK(x, y, P, ink) SH_MASK_ROWS(x, y, P, BR_##P##_H, ink)

/* the peek: after SH_PEEK_MS of silence on a mode's screen (no key held), sqrl bounces round the screen as the DVD
 * logo did, a colour each bounce, over the page dimmed. A corner hit: a sparkle. The first touch sends sqrl off the
 * screen the way it was going (sh_pk.leave), over the screen as it is: that touch acts as ever, but a knob's or an
 * encoder's first detent, whose card was not on the screen, only wakes it. All of it is worked out from the time,
 * drawn only while it shows, at most every 40 ms */
#define SH_SQ_RX (240 - BR_SQRL52_W)                    /* the bounce's room, px: x 0.., y SH_Y_PIC.. */
#define SH_SQ_RY (240 - SH_Y_PIC - BR_SQRL52_H)
#define SH_SQ_VX 41u                                    /* px a second: over 181 and 168, where y is at each x bounce */
#define SH_SQ_VY 47u                                    /* moves on by 0.62 of its round (the golden ratio's): the
                                                         * corners come evenly, every few minutes, no loop to learn */
#define SH_SQ_NEAR 1500u                                /* a corner: both within 1.5 px of an edge (at 40 ms a sample) */
#define SH_LEAVE_MS 400u
#define SH_LEAVE_V 700                                  /* px a second: off the screen from anywhere in SH_LEAVE_MS */
static uint32_t sh_peek_ms(void)                        /* ms since the peek began, + 1; 0: none */
{
    uint32_t t = fm1_ms - sh.idle_t;
    if (sh.splash_t || sh.wheel || sh.global || sh.help || sh.mom || fm1_in.notes || fm1_in.buttons ||
        t < SH_PEEK_MS)
        return 0;
    return t - SH_PEEK_MS + 1u;
}
static void sh_dim_band(void)                           /* the band drawn so far at a quarter: the page as a screensaver */
{
    uint32_t i, n = cv_w * cv_h;
    for (i = 0; i < n; i++) {
        uint16_t c = swap16(cv_px[i]);
        cv_px[i] = swap16((uint16_t)((c >> 2) & 0x39E7u));
    }
}
/* one axis of the bounce at t ms, in thousandths of a px: its place (0 .. r) and its bounces so far. The speed v
 * (px a second) carries a slow wobble of a px or so a second (a px at most 6 out over period w ms), so the corners
 * come irregularly, from any start */
static uint32_t sh_sq_axis(uint32_t p0, uint32_t v, uint32_t r, uint32_t w, uint32_t t, uint32_t *k)
{
    int32_t wob = (int32_t)(6000.0f * sh_sin(SH_TAU * (float)(t % w) / (float)w));   /* (32 bits: no 64-bit float
                                                         * conversion on the device) */
    uint64_t p = (uint64_t)p0 + (uint64_t)(6000 + wob) + (uint64_t)v * t;
    *k = (uint32_t)(p / r);
    p %= r;
    return *k & 1u ? r - (uint32_t)p : (uint32_t)p;
}
#define SH_SQ_WX 37000u
#define SH_SQ_WY 53000u
/* sqrl t ms in (thousandths of a px), its bounces, and its way on each axis (+1, -1) */
static void sh_sq_pos(uint32_t t, uint32_t *px, uint32_t *py, uint32_t *b, int32_t *wx, int32_t *wy)
{
    uint32_t h = (sh_pk.leave ? sh_pk.seed : sh.idle_t) * 2654435761u, kx, ky;   /* (leaving: the peek's start) */
    *px = sh_sq_axis((h >> 7) % (SH_SQ_RX * 1000u), SH_SQ_VX, SH_SQ_RX * 1000u, SH_SQ_WX, t, &kx);   /* (each peek */
    *py = sh_sq_axis((h >> 17) % (SH_SQ_RY * 1000u), SH_SQ_VY, SH_SQ_RY * 1000u, SH_SQ_WY, t, &ky);  /* elsewhere) */
    *b = kx + ky;
    *wx = kx & 1u ? -1 : 1;
    *wy = ky & 1u ? -1 : 1;
}
static int sh_sq_corner(uint32_t px, uint32_t py)      /* both axes at an edge at once */
{
    uint32_t dx = px < SH_SQ_RX * 500u ? px : SH_SQ_RX * 1000u - px, dy = py < SH_SQ_RY * 500u ? py : SH_SQ_RY * 1000u - py;
    return dx <= SH_SQ_NEAR && dy <= SH_SQ_NEAR;
}
/* sqrl t ms in: its place, its bounces, and how long since a corner (the last 900 ms, by 40; 0xFFFFFFFF: none) */
static void sh_sq_at(uint32_t t, int32_t *x, int32_t *y, uint32_t *bounces, uint32_t *since)
{
    uint32_t px, py, d, qx, qy, qb;
    int32_t wx, wy;
    sh_sq_pos(t, &px, &py, bounces, &wx, &wy);
    *x = (int32_t)(px / 1000u);
    *y = SH_Y_PIC + (int32_t)(py / 1000u);
    *since = 0xFFFFFFFFu;
    for (d = 0; d <= 880u && d <= t; d += 40u) {
        sh_sq_pos(t - d, &qx, &qy, &qb, &wx, &wy);
        if (sh_sq_corner(qx, qy)) {
            *since = d;
            return;
        }
    }
}
/* a frame of the peek or of sqrl leaving, for sh_signature (40 ms a frame) */
static uint32_t sh_pk_frame(void)
{
    uint32_t t = sh_peek_ms();
    if (t)
        return (t - 1u) / 40u + 1u;
    return sh_pk.leave ? 0x80000000u | (fm1_ms - sh_pk.leave_t) / 40u : 0u;
}
/* each frame, before the controls: a touch since last frame while the peek was up sends sqrl off (from where it
 * was); a touch while it leaves, or SH_LEAVE_MS, ends that */
static void sh_peek_poll(void)
{
    if (sh_pk.leave && (sh.idle_t != sh_pk.idle || fm1_ms - sh_pk.leave_t >= SH_LEAVE_MS))
        sh_pk.leave = 0;
    else if (sh_pk.was && sh.idle_t != sh_pk.idle) {
        sh_pk.leave = 1;
        sh_pk.leave_t = fm1_ms;
    }
    if (sh_peek_ms()) {
        sh_pk.t = sh_peek_ms() - 1u;
        sh_pk.seed = sh.idle_t;
        sh_pk.was = 1;
    } else
        sh_pk.was = 0;
    sh_pk.idle = sh.idle_t;
}
static void sh_draw_peek(void)                          /* the peek's scene, over the mode's picture */
{
    uint32_t t = sh_peek_ms() - 1u, b, since;
    int32_t sx, sy;
    t -= t % 40u;
    sh_sq_at(t, &sx, &sy, &b, &since);
    sh_rect(0, SH_Y_LEG - 1, 240, 240 - SH_Y_LEG + 1, SC_BG);   /* (the legend steps aside) */
    sh_dim_band();                                      /* the page behind at a quarter: a screensaver */
    SH_MASK(sx, sy, SQRL52, since < 900u && (t / 80u) & 1u ? SC_BONE : SH_MODES[b % 8u].color);
    if (since < 900u) {                                 /* the corner's sparkle, a burst round sqrl */
        float cx = (float)(sx + BR_SQRL52_W / 2), cy = (float)(sy + BR_SQRL52_H / 2), r = 30.0f + (float)since * 0.03f;
        uint32_t k;
        for (k = 0; k < 12u; k++) {
            float a = SH_TAU * (float)k / 12.0f + (float)since * 0.004f;
            if (cy + (r + 10.0f) * sh_sin(a) < (float)SH_Y_PIC + 1.0f || cy + r * sh_sin(a) < (float)SH_Y_PIC + 1.0f)
                continue;                               /* (none over the header) */
            sh_line(cx + r * sh_cos(a), cy + r * sh_sin(a), cx + (r + 10.0f) * sh_cos(a), cy + (r + 10.0f) * sh_sin(a),
                    k & 1u ? SC_BONE : SH_MODES[(b + k) % 8u].color);
        }
    }
}
/* woken: sqrl t ms in, off the screen the way it was going, fast */
static void sh_leave_at(uint32_t t, int32_t *x, int32_t *y, uint32_t *b)
{
    uint32_t px, py;
    int32_t wx, wy;
    sh_sq_pos(sh_pk.t, &px, &py, b, &wx, &wy);
    *x = (int32_t)(px / 1000u) + wx * (int32_t)(t * (uint32_t)SH_LEAVE_V / 1000u);
    *y = SH_Y_PIC + (int32_t)(py / 1000u) + wy * (int32_t)(t * (uint32_t)SH_LEAVE_V / 1000u);
}
static void sh_draw_leave(void)
{
    uint32_t t = fm1_ms - sh_pk.leave_t, b;
    int32_t sx, sy;
    sh_leave_at(t - t % 40u, &sx, &sy, &b);
    if (sx > -BR_SQRL52_W && sx < 240 && sy > SH_Y_PIC - BR_SQRL52_H && sy < 240)
        SH_MASK(sx, sy, SQRL52, SH_MODES[b % 8u].color);
}
/* a toast: a line of news for SH_TOAST_MS, on a panel at row y (the wheel draws it under its centre) */
static void sh_draw_toast(int32_t y, uint16_t col)
{
    int32_t w = text_w(&AF_S, sh.toast) + 18, h = sh.toast_undo ? 32 : 18;
    if (w < 120 && sh.toast_undo)
        w = 120;
    if (w > 232)
        w = 232;
    if (y + h > 240)
        y = 240 - h;
    sh_rect(120 - w / 2, y, w, h, SC_PANEL);
    sh_rect(120 - w / 2, y, w, 1, col);
    sh_text_c(120, y + 3, &AF_S, sh.toast, SC_BONE);
    if (sh.toast_undo)
        sh_text_c(120, y + 17, &AF_S, "HOLD REC: UNDO", SC_DIM);
}


static void sh_draw_splash(void)                       /* the power-on splash: sqrl's lockup, the maker's name small under it */
{
    SH_MASK((240 - BR_SQRLLOCKUP_W) / 2, 8, SQRLLOCKUP, SC_BONE);
    sh_text_c(120, 225, &AF_S, "databurn", SC_MID);
}

static void sh_draw_about(void)
{
    static const char *const ROWS[][2] = {
        {"SQRL-1", SQRL_VERSION_ID}, {"BASE", "Felucca " FELUCCA_VERSION}, {"NUTS", 0}, {"SPEECH", "databurn, MIT"},
        {"FM6", "msfa, Apache-2.0"}, {"LICENCE", "GPL-3.0"},
    };
    char n[12];
    uint32_t i, k;
    SH_MASK((240 - BR_DATABURN150_W) / 2, 30, DATABURN150, SC_BONE);
    sh_rect(20, 78, 200, 1, SC_FAINT);
    for (i = 0, k = 0; i < SH_SLOTS; i++)                /* how many of the wheel's white keys are filled */
        k += sh_wslot[i] != SH_EMPTY;
    fmt_int(n, (int32_t)k);
    str_cpy(n + str_len(n), " of 8", 12u - str_len(n));
    for (i = 0; i < 6u; i++) {
        sh_text(20, 88 + (int32_t)i * 17, &AF_S, ROWS[i][0], SC_DIM);
        sh_text(92, 88 + (int32_t)i * 17, &AF_S, ROWS[i][1] ? ROWS[i][1] : n, SC_BONE);
    }
    sh_rect(20, 194, 200, 1, SC_FAINT);
    sh_text_c(120, 204, &AF_S, "Art CC BY-SA 4.0, databurn", SC_MID);
}

/* LEGAL (GLO's page after ABOUT): the notices GPL-3.0 section 5(d) asks an interactive program to show */
static void sh_draw_legal(void)
{
    static const char *const L[] = {"SQRL-1 " SQRL_VERSION, "(C) 2026 databurn", "", "Based on Felucca " FELUCCA_VERSION,
                                    "(C) 2026 Leo Kuroshita,", "H\xFCgelton Instruments", "",
                                    "GPL-3.0-only, NO WARRANTY", "github.com/hed0rah/sqrl-1"};
    uint32_t i;
    sh_text_c(120, 14, &AF_M, "LEGAL", SC_BONE);
    sh_rect(20, 40, 200, 1, SC_FAINT);
    for (i = 0; i < NELEM(L); i++)
        sh_text_c(120, 50 + (int32_t)i * 18, &AF_S, L[i], i == 7u ? SC_BONE : SC_DIM);
}

/* PROJECTS: the four slots, the cursor's lit; the question over them when one is asked */
static void sh_draw_project(void)
{
    uint32_t k;
    const sh_mode_t *m = sh_cur();
    sh_text_c(120, 14, &AF_M, "PROJECTS", SC_BONE);
    sh_rect(20, 40, 200, 1, SC_FAINT);
    for (k = 0; k < 5u; k++) {                          /* A..D, then NEW */
        char l[2] = {(char)('A' + k), 0};
        int32_t y = 48 + (int32_t)k * 25, on = k == sh.pslot, used = (sh_pused >> k) & 1u;
        if (on)
            sh_rect(16, y - 3, 208, 23, SC_PANEL);
        if (k == 4u) {
            sh_text(26, y + 3, &AF_S, "NEW", on ? m->color : SC_DIM);
            sh_text(64, y + 3, &AF_S, "START EMPTY", on ? SC_BONE : SC_MID);
            continue;
        }
        sh_text(26, y + 1, &AF_M, l, on ? m->color : SC_DIM);
        sh_text(56, y + 3, &AF_S, !used ? "EMPTY" : sh_pname[k][0] ? sh_pname[k] : "UNNAMED", used ? SC_BONE : SC_MID);
    }
    sh_rect(20, 176, 200, 1, SC_FAINT);
    if (sh.pq) {
        char q[24];
        str_cpy(q, sh.pq == 1u ? "SAVE TO " : "LOAD ", sizeof q);
        q[str_len(q) + 1u] = 0;
        q[str_len(q)] = (char)('A' + sh.pslot);
        str_cpy(q + str_len(q), "?", sizeof q - str_len(q));
        if (sh.pq == 3u)
            str_cpy(q, "START EMPTY?", sizeof q);
        sh_text_c(120, 186, &AF_S, q, SC_BONE);
        sh_text_c(120, 206, &AF_S, sh.pq == 1u ? "OCT+ YES   ANY OTHER: NO" : "THE MUSIC NOW GOES   OCT+ YES", SC_DIM);
    } else if (sh_fresh(sh.toast_t, SH_TOAST_MS)) {    /* (news in place of the hints, clear of the slots) */
        sh_draw_toast(196, m->color);
    } else {
        sh_text_c(120, 186, &AF_S, sh.pslot == 4u ? "OCT+ OR OCT-: START EMPTY" : "OCT+ SAVE   OCT- LOAD", SC_DIM);
        sh_text_c(120, 206, &AF_S, "PRESETS OR KNOB 1: THE SLOT", SC_MID);
    }
}

/* the sound in front's preset as PRESETS names it: a user slot "U07 NAME", else the factory sound */
static const char *sh_preset_label(void)
{
    const engine_t *pe = ENGINES[sh_trk()->eng_req % NENGINES];
    static char un[18];
    if (sh_trk()->user && up_used(sh_trk()->user - 1u)) {
        up_slot_label(un, sh_trk()->user - 1u);
        un[3] = ' ';
        up_name(sh_trk()->user - 1u, un + 4);
        return un;
    }
    return pe->npresets ? pe->presets[sh_trk()->preset % pe->npresets].name : "-";   /* (an engine left out) */
}

static void sh_draw_pair(void)
{
    static const char *const L[4] = {"SOUND", "NOTE", "PRESET", "MUTE"};
    const sh_mode_t *m = sh_ppend.on ? &SH_MODES[sh_ppend.m] : sh_cur();
    uint32_t k, behind = (sh_tmute >> sh.part) & 1u, n = sh_ppend.on ? sh_ppend.n : sh.note;
    uint32_t able = 1;                                  /* (every pair in front holds its part) */
    sh_text_c(120, 14, &AF_M, "PAIR", SC_BONE);
    sh_rect(20, 40, 200, 1, SC_FAINT);
    for (k = 0; k < 4u; k++) {
        int32_t y = 50 + (int32_t)k * 30;
        const char *v = k == 0u ? (sh_ppend.on ? m->name : sh_sound_name()) : k == 1u ? SH_NOTES[n].name :
                        k == 2u ? sh_preset_label() : !able ? "BEAT OR LINE" : behind ? "ON" : "OFF";
        uint16_t c = k == 0u ? m->color : k == 1u ? SH_NOTES[n].color : k == 3u && !able ? SC_MID : SC_BONE;
        if (sh.hot == k && sh_fresh(sh.hot_t, 1300u))
            sh_rect(16, y - 4, 208, 26, SC_PANEL);
        if (k == 0u)
            sh_poly(30.0f, (float)(y + 9), 6.0f, m->sides, -SH_TAU / 4.0f, m->color);
        sh_text(42, y + 3, &AF_S, L[k], SC_DIM);
        if (text_w(&AF_M, v) <= 124)                    /* (a long name: the small face, as it fits) */
            sh_text(110, y + 2, &AF_M, v, c);
        else
            sh_text(110, y + 5, &AF_S, v, c);
    }
    sh_rect(20, 176, 200, 1, SC_FAINT);
    if (sh_fresh(sh.toast_t, SH_TOAST_MS))
        sh_draw_toast(196, m->color);
    else {
        sh_text_c(120, 186, &AF_S, sh_ppend.on ? "LEAVING PUTS THIS PAIR IN FRONT" : "KNOBS 1-4 PICK", SC_DIM);
        sh_text_c(120, 206, &AF_S, "HOME OR OCT- LEAVES", SC_MID);
    }
}

/* the FX layer, FX held: the white keys F3 .. G5 four a row with the effect each holds (lit while it plays, dim
 * where it has none or cannot run), the black keys' part mutes, then the four macros */
static void sh_draw_fx(void)
{
    static const char W[16] = {'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    static const char *const ML[4] = {"FILTER", "CRUSH", "THROW", "DEPTH"};
    const sh_mode_t *m = sh_cur();
    uint32_t held = perf_held | perf_latched, act = perf_act, ok = perf_avail(), p, e;
    char n[12];
    sh_text_c(120, 4, &AF_M, perf_latch_on ? "FX LATCH" : "FX", SC_BONE);
    for (p = 0; p < PF_KEYS; p++) {
        int32_t x = 6 + 58 * (int32_t)(p % 4u), y = 30 + 28 * (int32_t)(p / 4u);
        uint32_t none = (e = perf_map[p]) >= PF_NFX || !((ok >> e) & 1u), on = !none && ((held >> e) & 1u);
        uint16_t ink = on ? SC_BG : none ? SC_MID : SC_BONE;
        sh_rect(x, y, 54, 24, on ? ((act >> e) & 1u ? m->color : SC_DIM) : none ? SC_BG : SC_PANEL);
        if (none)
            sh_rect(x, y + 23, 54, 1, SC_FAINT);
        n[0] = W[p];
        n[1] = 0;
        sh_text(x + 4, y + 5, &AF_S, n, on ? SC_BG : SC_MID);
        if (e < PF_NFX)
            sh_text_c(x + 31, y + 5, &AF_S, PF_CELL[e], ink);
    }
    for (p = 0; p < 4u; p++) {                          /* the first four black keys, F#3 .. C#4: part 1 .. 4 muted */
        int32_t x = 6 + 58 * (int32_t)p;
        uint32_t on = (held >> (PF_M1 + p)) & 1u;
        sh_rect(x, 146, 54, 22, on ? RGB(224, 90, 79) : SC_PANEL);
        n[0] = 'M'; n[1] = 'U'; n[2] = 'T'; n[3] = 'E'; n[4] = ' '; n[5] = (char)('1' + p); n[6] = 0;
        sh_text_c(x + 27, 150, &AF_S, n, on ? SC_BG : SC_DIM);
    }
    for (p = 0; p < 4u; p++) {                          /* KNOB 1..4: FILTER (- LP, + HP), CRUSH, THROW, DEPTH or SHIMR */
        int32_t x = 6 + 58 * (int32_t)p, v = perf_k[p], a = v < 0 ? -v : v, w;
        int harm = p == 3u && perf_harm_on();
        if (p == 3u && !harm)
            a = 100 - v;
        sh_text_c(x + 27, 180, &AF_S, harm ? "SHIMR" : ML[p], SC_DIM);
        if (!a && p < 3u)
            str_cpy(n, "OFF", sizeof n);
        else {
            fmt_int(n, a);
            str_cpy(n + str_len(n), p == 0u ? (v < 0 ? " LP" : " HP") : "%", sizeof n - str_len(n));
        }
        sh_text_c(x + 27, 198, &AF_M, n, v ? SC_KNOB[p] : SC_MID);   /* (lit once turned) */
        w = 54 * a / 100;
        sh_rect(x, 224, 54, 2, SC_FAINT);
        sh_rect(x, 224, w, 2, SC_KNOB[p]);
    }
}

static void sh_draw_nibs(void)
{
    sh_rect(0, 0, 240, 240, SC_PAPER);
    SH_BRAND((240 - BR_NIBS_W) / 2, (240 - BR_NIBS_H) / 2, NIBS, 0);
}

/* the wheel's last line takes turns: what letting go does, then the way to the PAIR page, two seconds each */
static uint32_t sh_wheel_hint(void) { return ((fm1_ms - sh.wheel_t0) / 2000u) % 3u; }   /* the last line's turn */

static void sh_draw_wheel(void)
{
    float cx = 120.0f, cy = 102.0f, R = 66.0f;   /* room under it for the NOTE row */
    uint32_t i, on, beat, mute;
    sh_part_keys(&on, &beat, &mute);
    sh_rect(0, 0, 240, 240, SC_BG);
    sh_poly(cx, cy, R, SH_SLOTS, -SH_TAU / 4.0f, SC_FAINT);
    sh_poly(cx, cy, R * 0.38f, SH_SLOTS, -SH_TAU / 4.0f + SH_TAU / 16.0f, sh.wsel >= SH_SLOTS ? SC_BONE : SC_FAINT);
    for (i = 0; i < SH_SLOTS; i++) {
        float a = -SH_TAU / 4.0f + SH_TAU * (float)i / SH_SLOTS, x = cx + R * sh_cos(a), y = cy + R * sh_sin(a);
        int below = sh_sin(a) > 0.3f;
        uint32_t m = sh_wslot[i];
        sh_line(cx + R * 0.38f * sh_cos(a), cy + R * 0.38f * sh_sin(a), x, y, SC_FAINT);
        if (m == SH_EMPTY) {                            /* an empty slot: GLO + its key fills it */
            sh_poly(x, y, i == sh.wsel ? 6.0f : 3.0f, 0, 0.0f, i == sh.wsel ? SC_MID : SC_FAINT);
            continue;
        }
        if (i == sh.wsel) {                             /* the nut picked: its ring an acorn, a cap and a stem (sqrl) */
            float k, px = 0.0f, py = 0.0f;
            sh_poly(x, y, 15.0f, 0, 0.0f, SH_MODES[m].color);
            for (k = 0.0f; k <= 1.0f; k += 0.125f) {        /* the cap: a flatter arc over the ring's top */
                float t = SH_TAU * (0.56f + 0.38f * k), qx = x + 19.0f * sh_cos(t), qy = y - 4.0f + 14.0f * sh_sin(t);
                if (k > 0.0f)
                    sh_line(px, py, qx, qy, SH_MODES[m].color);
                px = qx;
                py = qy;
            }
            sh_line(x, y - 18.0f, x + 2.0f, y - 23.0f, SH_MODES[m].color);
        }
        if (m == sh.mode)
            sh_poly(x, y, 12.0f, 0, 0.0f, SC_MID);
        if ((on >> i) & 1u) {                           /* a pair on it plays: a small green play mark */
            sh_line(x + 12.0f, y - 16.0f, x + 12.0f, y - 8.0f, RGB(127, 209, 139));
            sh_line(x + 12.0f, y - 16.0f, x + 18.0f, y - 12.0f, RGB(127, 209, 139));
            sh_line(x + 12.0f, y - 8.0f, x + 18.0f, y - 12.0f, RGB(127, 209, 139));
        } else if ((mute >> i) & 1u) {                  /* one muted: a dim pause mark */
            sh_line(x + 12.0f, y - 16.0f, x + 12.0f, y - 8.0f, SC_DIM);
            sh_line(x + 16.0f, y - 16.0f, x + 16.0f, y - 8.0f, SC_DIM);
        }
        sh_poly(x, y, i == sh.wsel ? 9.0f : 7.0f, SH_MODES[m].sides, -SH_TAU / 4.0f,
                i == sh.wsel ? SH_MODES[m].color : SC_DIM);
        sh_text_c((int32_t)x, (int32_t)y + (below ? 15 : i == sh.wsel ? -37 : -30), &AF_S, SH_MODES[m].name,   /* (over
                                                                                  * the acorn's cap and stem when picked) */
                  i == sh.wsel ? SH_MODES[m].color : SC_DIM);
    }
    sh_rect(84, 90, 72, 34, SC_BG);                     /* the centre's words are kept clear of the spokes */
    sh_rect(102, 52, 36, 36, SC_BG);                    /* sqrl's glyph over the centre */
    SH_MASK(104, 54, SQRL32, sh.wsel >= SH_SLOTS ? SC_BONE : SC_MID);
    if (sh.wsel >= SH_SLOTS) {
        sh_text_c(120, 92, &AF_M, "SYSTEM", SC_BONE);
        sh_text_c(120, 110, &AF_S, "SETTINGS", SC_DIM);
    } else if (sh_wslot[sh.wsel] == SH_EMPTY) {
        sh_text_c(120, 92, &AF_M, "EMPTY", SC_MID);
        sh_text_c(120, 110, &AF_S, "GLO + ITS KEY: FILL", SC_DIM);
    } else {
        const sh_mode_t *wm = &SH_MODES[sh_wslot[sh.wsel]];
        sh_text_c(120, 92, &AF_M, wm->name, wm->color);
        sh_text_c(120, 110, &AF_S, sh_note_at(sh.wnote) ? SH_NOTES[sh_note_at(sh.wnote)].about : wm->about, SC_DIM);
    }
    {   /* the NOTE engines (the black keys), a row under the octagon: what plays the SOUND picked */
        uint32_t n;
        int32_t wd[SH_NOTE_SLOTS], sum = 0, gap, x0 = 4;
        for (n = 0; n < SH_NOTE_SLOTS; n++) {           /* laid out by their widths, so TONNETZ and ENIGMA fit side by side */
            uint32_t e = sh_wnoteslot[n];
            wd[n] = text_w(&AF_S, e < SH_NNOTES ? SH_NOTES[e].name : "-");
            sum += wd[n];
        }
        gap = (232 - sum) / (int32_t)(SH_NOTE_SLOTS - 1u);
        for (n = 0; n < SH_NOTE_SLOTS; n++) {
            int32_t x = x0 + wd[n] / 2;
            uint32_t e = sh_wnoteslot[n];
            x0 += wd[n] + gap;
            uint16_t c = e >= SH_NNOTES ? SC_FAINT : n == sh.wnote ? SH_NOTES[e].color : SC_DIM;
            if (n == sh.wnote)
                sh_rect(x - wd[n] / 2, 218, wd[n], 1, c);
            sh_text_c(x, 205, &AF_S, e < SH_NNOTES ? SH_NOTES[e].name : "-", c);
            if (e < SH_NNOTES && ((on | mute) >> (8u + n)) & 1u)   /* (playing: a green dot; muted: a dim one) */
                sh_disc((float)(x + wd[n] / 2 + 3), 210.0f, 2.0f, ((on >> (8u + n)) & 1u) ? RGB(127, 209, 139) : SC_DIM);
        }
    }
    sh_text_c(120, 226, &AF_S, sh.pick ? "SELECT, THEN LET GO OF GLO" : sh_held(B_GLO) ? "GLO + A KEY: PICK ITS ENGINE" :
              sh.wsel < SH_SLOTS && sh_wslot[sh.wsel] == SH_EMPTY ? "GLO + THIS KEY: PUT A SOUND HERE" :
              sh_wheel_hint() == 1u ? "TAP GLO: PICK BY KNOBS (PAIR PAGE)" :   /* (for anyone the keys confuse) */
              sh_wheel_hint() == 2u ? "HOLD GLO + A BLACK KEY: CHANGE IT" :
              "LET GO: GO  OCT-: STAY  PLAY: MUTE", SC_MID);
    if (sh.pick) {                                      /* the slot picker: the catalogue around the cursor */
        int32_t r, n = sh.pick == 1u ? (int32_t)SH_NMODES : (int32_t)SH_NNOTES;
        sh_rect(44, 40, 152, 136, SC_PANEL);
        sh_rect(44, 40, 152, 1, sh.pick == 1u ? SC_BONE : SC_MID);
        sh_text_c(120, 46, &AF_S, sh.pick == 1u ? "THIS WHITE KEY'S SOUND" : "THIS BLACK KEY'S NOTE", SC_DIM);
        for (r = -2; r <= 2; r++) {
            int32_t c = ((int32_t)sh.pickcur + r + n * 4) % n;
            const char *nm = sh.pick == 1u ? SH_MODES[c].name : SH_NOTES[c].name;
            uint16_t col = sh.pick == 1u ? SH_MODES[c].color : SH_NOTES[c].color;
            int ok = sh_pick_ok(sh.pick, (uint32_t)c, sh.pickpos);
            if (!r)
                sh_rect(52, 102, 136, 22, SC_BG);
            sh_text_c(120, 106 + r * 22, r ? &AF_S : &AF_M, nm, !ok ? SC_FAINT : r ? SC_DIM : col);
        }
    }
}

static int sh_dsp_shown(void)                           /* SYSTEM's DSP meter: not under a card that is still up */
{
    return sh.global == SHG_SYSTEM && !(sh.pop && sh_fresh(sh.pop_t, SH_POP_MS));
}
static void sh_draw_overlays(void)
{
    const sh_mode_t *m = sh_cur();
    const engine_t *e = ENGINES[sh_eng()];
    if (sh.pop && sh_fresh(sh.pop_t, SH_POP_MS)) {
        if (sh.pop == 1) {
            const char *rows[2];
            rows[0] = sh_preset_label();                /* (a user slot's sound: U07 and its name) */
            rows[1] = ENGINES[sh_trk()->eng_req % NENGINES]->name;
            sh_card("PRESETS", rows, FELUCCA_LIBRARY && m->lib ? 2u : 1u, m->color);   /* LIBRARY shows the engine too */
        } else {
            char val[12];
            const char *unit = 0, *rows[2];
            int16_t *vp;
            uint32_t id = sh_hero_id();                 /* (on a NOTE page, the NOTE engine's setting) */
            const param_desc_t *d = sh_desc(id, 0, &vp);
            param_format(d, *vp, val, &unit);
            rows[0] = val;
            if (id == SP_RHY)                           /* a rhythm shows its meter too, a line LINE */
                rows[1] = sh_li == 1u ? (*vp ? "LINE" : "EMPTY") : *vp == SH_MAP ? "GRIDS" :
                          SH_RHY[(uint32_t)*vp % SH_NRHY].meter;
            sh_card(d->label, (const char *const *)rows, id == SP_RHY ? 2u : 1u, m->color);
        }
    }
    if (sh_dsp_shown()) {                               /* the DSP meter: what the sound costs, measured */
        char b[48], n[8];
        str_cpy(b, "DSP ", 5);
        fmt_int(n, sh.dsp);
        str_cpy(b + str_len(b), n, 8);
        str_cpy(b + str_len(b), "%  PEAK ", 9);
        fmt_int(n, sh.dsp_pk);
        str_cpy(b + str_len(b), n, 8);
        str_cpy(b + str_len(b), "%  SHED ", 9);
        fmt_int(n, (int32_t)(shed_count > 999u ? 999u : shed_count));
        str_cpy(b + str_len(b), n, 8);
        str_cpy(b + str_len(b), "  LATE ", 8);
        fmt_int(n, (int32_t)(felucca_dbg.late > 999u ? 999u : felucca_dbg.late));
        str_cpy(b + str_len(b), n, 8);
        sh_text_c(120, 23, &AF_S, b, sh.dsp_pk >= SH_DSP_RED || felucca_dbg.late ? RGB(224, 110, 90) : SC_DIM);
    }
    if (sh_splsv.open) {                                /* SPELL's SAVE */
        static char t[16];
        static const char *const rows[2] = {"SELECT: THE SLOT", "OCT+ YES     OCT- NO"};
        str_cpy(t, "PHRASE TO U", 12);
        t[11] = (char)('1' + sh_splsv.slot);
        t[12] = 0;
        sh_card(t, rows, 2, m->color);
    }
    if (sh.save) {                                      /* SAVE's question */
        static char t[12], r0[24];
        const char *rows[2];
        str_cpy(t, "SAVE ", 6);
        up_slot_label(t + 5, sh.save_slot);
        if (up_used(sh.save_slot)) {
            str_cpy(r0, "OVER ", 6);
            up_name(sh.save_slot, r0 + 5);
        } else {
            str_cpy(r0, "A NEW SOUND", 12);
        }
        rows[0] = r0;
        rows[1] = "OCT+ YES     OCT- NO";
        sh_card(t, rows, 2, m->color);
    }
    if (sh.help) {
        static const char *const H[NB][4] = {
            [B_HOME] = {"TAP: SOUND / NOTE, THE OTHER VIEW", "HOLD: WHEEL, KEYS PICK", "HOLD + TAP GLO: PICK BY KNOBS",
                        "TWICE: EACH PAIR BEHIND IN TURN"},
            [B_GLO] = {"TAP: SYSTEM, SYSTEM 2, PROJECTS", "SYS 2: GROOVE CLICK COUNT-IN", "ROUT: MIDI IN, SEL = IN FRONT",
                       "HOLD: SHIFT"},
            [B_SCL] = {"TAP: SCALE, EVERY NUT SHARES IT", "AGAIN: HARMONY; AGAIN: BACK", "SELECT ON: MOVE, A SCALE THAT MOVES",
                       "HOLD WHILE IT PLAYS: FILL"},
            [B_SAVE] = {"TAP: SAVE THE SOUND", "HOLD: TRY, LET GO: BACK", "OCT+ WHILE HELD: KEEP"},
            [B_OCTDN] = {"PRESS: OCTAVE DOWN", "A QUESTION: NO; PAIR, WHEEL: LEAVE", "PROJECTS: LOAD (ASKED)"},
            [B_OCTUP] = {"PRESS: OCTAVE UP", "A QUESTION: YES", "PROJECTS: SAVE (ASKED)"},
            [B_ARP] = {"TAP: THE ARP; TWICE: HOLD ON/OFF", "HOLD: HOLD ON/OFF; NO ARP: KEYS LATCH",
                       "ARP RATE, SWING: ITS OWN PAGES", "PRESETS STAYS THE SOUND"},
            [B_SEQ] = {"TAP: THE PATTERN", "HOLD + WHITE KEYS: STEPS", "C#5 / D#5: STEP PAGES",
                       "HOLD + OCT+/-: DOUBLE/HALF"},
            [B_REC] = {"TAP: ARM  TWICE: KNOBS  HOLD: UNDO", "KEEP HOLDING: CLEAR ALL", "RUDIMENT: HOLD + A KEY: ERASE",
                       "HOLD + OCT+: FREEZE, + SELECT: MODE"},
            [B_PLAY] = {"PRESS: START / STOP", "REC HELD: COUNT IN, RECORD", 0},
        };
        uint32_t b = sh.help - 1u, n = 0, p;
        const char *rows[SHP_N];
        static const char *const PN[SHP_N] = {0, "EDIT 1", "EDIT 2", "OPERATOR", "OPERATOR 2", "OP ENV", "FILTER", "SHAPE",
                                              "DRAW 1-4", "DRAW 5-8", "LANES", "LANES 2", "VOICE", "GLIDE", "ENV", "ENV DEST",
                                              "LFO", "LFO DEST", "LFO 2", "FX", "INSERT", "INSERT 2", "DELAY", "DELAY 2",
                                              "REVERB", "CHORUS", "MAIN", "NOTES", "LANE 2", "LINE", "MAP", "MAP 2", "CHORD PLAY", "PROG", "ARP", "ARP 2", "SEQ", "KEYS", "KEYS 2", "SPELL", "DRUM", "EVOLVE"};
        while (n < 4u && H[b][n])
            rows[n] = H[b][n], n++;
        if (!n)                                         /* a page family: its pages here, in the order a tap walks */
            for (p = 1; p < SHP_N; p++)
                if (SHP_FAM[p] == b && sh_page_ok(p))
                    rows[n++] = p == SHP_EDIT1 || p == SHP_EDIT2 ? ENGINES[sh_trk()->eng_req % NENGINES]->page_title
                                                                       [p - SHP_EDIT1] : PN[p];
        if (b == B_FX)                                  /* (and the FX layer) */
            rows[n++] = "HOLD + KEYS: FX ON THE MIX";
        if (!n)
            rows[n++] = "TAP: ITS PAGE, AGAIN: THE NEXT";
        sh_card("GLO + BUTTON", rows, n, SC_DIM);
    } else if (sh_held(B_GLO) && !sh.wheel && sh_on_spell()) {   /* SPELL's letters: the text, the keyboard lettered */
        uint32_t k, w = 0;
        int32_t x;
        sh_rect(0, 54, 240, 142, SC_BG);
        sh_text_c(120, 64, &AF_M, spl_tn ? spl_txt : "TYPE A WORD", spl_tn ? SC_BONE : SC_DIM);
        for (k = 0; k < 27u; k++) {                     /* the white keys a row of letters, the black ones above */
            char c[2] = {k < 26u ? (char)('A' + k) : '_', 0};
            if (sh_key_white(k) >= 0) {
                x = 1 + (int32_t)w * 15;
                sh_rect(x, 140, 13, 34, SC_PANEL);
                sh_text_c(x + 6, 158, &AF_S, c, SC_BONE);
                w++;
            } else {
                x = 1 + (int32_t)w * 15 - 6;
                sh_rect(x, 112, 11, 26, SC_FAINT);
                sh_text_c(x + 5, 117, &AF_S, c, SC_DIM);
            }
        }
        sh_text_c(120, 92, &AF_S, "OCT- BACK   LET GO: SPEAK", SC_DIM);
    } else if (sh_held(B_GLO) && !sh.wheel) {
        static const char *const S[3] = {"C4-C5: WHITE SOUND, BLACK NOTE", "SELECT: TEMPO", "ANY BUTTON: WHAT IT DOES"};
        sh_card("SHIFT", S, 3, SC_DIM);
    }
    if (sh.mom)
        sh_text_c(120, 24, &AF_S, "TRYING: LET GO OF SAVE", SC_BONE);
    if (sh.ring) {
        int rst = sh.ring >= SH_RING_RESET && sh.ring < SH_RING_RESET + 4u;
        float pr = (float)(fm1_ms - sh.ring_t0) / (float)(rst ? SH_LONG_MS : SH_LONG_MS - SH_TAP_MS), a0 = -SH_TAU / 4.0f, a;
        uint32_t i;
        sh_rect(60, 70, 120, 76, SC_PANEL);
        sh_poly(120.0f, 102.0f, 18.0f, 0, 0.0f, SC_FAINT);
        for (i = 0; i < 48u && (float)i / 48.0f < pr; i++) {
            a = a0 + SH_TAU * (float)i / 48.0f;
            sh_line(120.0f + 18.0f * sh_cos(a), 102.0f + 18.0f * sh_sin(a),
                    120.0f + 18.0f * sh_cos(a + SH_TAU / 48.0f), 102.0f + 18.0f * sh_sin(a + SH_TAU / 48.0f),
                    RGB(224, 90, 79));
        }
        sh_text_c(120, 128, &AF_S, rst ? SH_RST_WORD[(sh.ring - SH_RING_RESET) & 3u] :
                  sh.ring == SH_RING_CHORD ? "STANDARD CHORDS" :
                  sh.ring2 ? (sh_kind() == SHK_RUDIMENT ? "CLEAR ALL LANES" : "CLEAR PATTERN") :
                  sh_ring_word(), SC_BONE);
    }
    if (sh_fresh(sh.toast_t, SH_TOAST_MS) && !sh.wheel)
        sh_draw_toast(sh.toast_undo ? 158 : 172, m->color);   /* (two lines: clear of the band) */
}

static uint32_t sh_signature(void)
{
    uint32_t h = 2166136261u, k;
    uint8_t ids[4];
    int glob;
#define SH_H(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    SH_H(sh.splash_t != 0u); SH_H(sh_peek_ms() != 0u); SH_H(sh_pk_frame()); SH_H(sh.save ? sh.save_slot + 1u : 0u); SH_H(sh_trk()->user);
    SH_H(sh.global == SHG_MOVE ? (uint32_t)sh_trk()->p[P_ROOT] << 8 | (uint32_t)sh_trk()->p[P_SCALE] | (uint32_t)sh_mv.on << 16 : 0u);
    SH_H(sh.global == SHG_SYSTEM ? sh.dsp_t : 0u); SH_H(sh.mode); SH_H(sh.page[sh.mode]); SH_H(sh.global); SH_H(sh.wheel); SH_H(sh.wsel); SH_H(sh.wnote); SH_H(sh.note); SH_H(sh_tmute); SH_H(autosave_held()); SH_H(shell_map_on ? shell_map_x | shell_map_y << 8 | (uint32_t)shell_map_dens[0] << 16 | (uint32_t)shell_map_dens[1] << 24 : 0u); SH_H(shell_map_dens[2] | shell_map_acc << 8 | shell_map_chaos << 16 | (uint32_t)map_cyc << 24); { uint32_t o, bt, mu; sh_part_keys(&o, &bt, &mu); SH_H(o | mu << 16); } SH_H(sh.help); SH_H(sh.pick); SH_H(sh.pickcur);
    for (k = 0; k < SH_SLOTS; k++) SH_H(sh_wslot[k]);
    for (k = 0; k < 5u; k++) SH_H(sh_wnoteslot[k]);
    SH_H(sh.pslot); SH_H(sh.pq); SH_H(sh.pgen); SH_H(sh.wheel ? sh_wheel_hint() : 0u);
    SH_H(sh_ppend.on ? 1u << 16 | (uint32_t)sh_ppend.m << 8 | sh_ppend.n : 0u);   /* PAIR: the pair the knobs picked */
    SH_H(seq_fill);
    {   /* the extras of the page in view (its marks), every page of steps */
        uint64_t lk = motion_lock_steps(trk_index(sh_trk()));
        for (k = 0; k < 16u; k++)
            SH_H(sh_step_extras(sh_trk(), sh_pg() + k, lk));
    }
    SH_H(sh_q.mask ? (uint32_t)sh_q.mask << 8 | sh_q.slot << 1 | ((fm1_ms / 250u) & 1u) : 0u);
    if (sh_stp.on) {                                    /* a step held: its values on the cards, its locks */
        const step_t *st = &sh_trk()->step[sh_stp.w % NSTEP];
        SH_H(sh_stp.w + 1u); SH_H(st->flags); SH_H(st->probability); SH_H(st->vel); SH_H(motion.count);
        for (k = 0; k < motion.count; k++)
            SH_H(motion.event[k].value);
    }
    SH_H(sh.mom); SH_H(sh_st.pending); SH_H(sh.ring ? (fm1_ms - sh.ring_t0) / 40u : 0u); SH_H(song.playing); SH_H(song.g[G_BPM]);
    SH_H(cin_left); SH_H(sh_take.n); SH_H(sh.ring2); SH_H(rec_on(sh_trk()) ? sh_trk()->seq_idx / 4u + 1u : 0u);
    SH_H(sh.hot == 4u ? 4u : (sh_fresh(sh.hot_t, 1300u) ? sh.hot : 9u));
    SH_H(sh_fresh(sh.toast_t, SH_TOAST_MS) ? sh.toast_t : 0u);
    SH_H(sh.pop && sh_fresh(sh.pop_t, SH_POP_MS) ? sh.pop_t : 0u);
    SH_H(sh_held(B_GLO)); SH_H(sh_trk()->preset); SH_H(sh_hero_id() < SP_FIRST ? sh_trk()->p[sh_hero_id()] : 0);
    sh_page_ids(ids, &glob);
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        if (ids[k] == SH_NONE)
            continue;
        (void)sh_desc(ids[k], glob, &vp);
        SH_H(*vp);
    }
    SH_H(sh_sp[sh_hero_id() >= SP_FIRST ? sh_hero_id() - SP_FIRST : 0]);
    SH_H(sh_partpair[SH_LINE]); SH_H(sh_partpair[SH_BEAT]);
    SH_H(sh_inst_sig());
    if (sh_fx_shown()) {                                /* the FX layer: what plays, the map, the macros */
        SH_H(perf_held | perf_latched); SH_H(perf_act); SH_H(perf_avail()); SH_H(perf_latch_on); SH_H(perf_harm_on());
        for (k = 0; k < PF_KEYS; k++)
            SH_H(perf_map[k]);
        for (k = 0; k < 4u; k++)
            SH_H((uint8_t)perf_k[k]);
    }
    SH_H(sh_fx_shown());
    SH_H(sh_hs_sig());
    if (sh.page[sh.mode] == SHP_SEQ) {                  /* the steps and the playhead */
        for (k = 0; k < 16u; k++)
            SH_H(sh_trk()->step[k].n * 128u + sh_trk()->step[k].note[0]);
        SH_H(song.playing ? sh_trk()->seq_idx : 99u);
    }
    for (k = P_E0; k <= P_E7; k++)                      /* the pictures read the engine's own eight */
        SH_H(sh_trk()->p[k]);
    SH_H(sh_eng() == ENGI_VODER ? fm1_in.notes : 0u);
    SH_H(sh_eng() == ENGI_FM6 ? fm6_pgen[sh.part] : 0u);
    SH_H(sh_live_sig());                                /* the SOUND's picture: what sounds now */
    if (mouth_spell_n) {                                /* SPELL: the phrase and the cursor */
        SH_H(spl.cur); SH_H(spl.n); SH_H(spl_count()); SH_H(spl.s[spl.cur].v); SH_H(spl_tn);
        SH_H(sh_splsv.open); SH_H(sh_splsv.slot);
    }
#if FELUCCA_LOOP
    if (sh_eng() == ENGI_LOOP && !sh.wheel) {           /* LOOP's heads move round the reel */
        SH_H((uint32_t)loop_tl.pos >> 20); SH_H(loop_tl.len); SH_H(loop_tl.q);
    }
#endif
    if (sh_eng() == ENGI_MOUTH && !sh.wheel) {          /* MOUTH's lips follow its formants: redraw as they move */
        int32_t q1 = 0, q2 = 0;
        sh_mouth_f(sh_trk(), &q1, &q2);
        SH_H((uint32_t)(q1 >> 12)); SH_H((uint32_t)(q2 >> 12));
    }
    if (sh_fmop_page()) {                               /* the operator, the stage, its bytes */
        SH_H(fop_op);
        SH_H(fop_stg);
        for (k = 0; k < FP_OP; k++)
            SH_H(fm6_patch[sh.part][(5u - fop_op % 6u) * FP_OP + k]);
    }
#if FELUCCA_TALK
    if (sh_eng() == ENGI_TALK) {                /* the word spoken changes in the ISR on a note-on */
        uint32_t a = 0, w = 0;
        SH_H(talk_now(sh_trk(), &a, &w) ? a * 256u + w + 1u : 0u);
    }
#endif
#if FELUCCA_SYLLABLE
    if (sh_eng() == ENGI_SYLLABLE) {
        uint32_t c = 0, v = 0, e = 0, ph = 0, pos = 0;
        SH_H(syl_now(sh_trk(), &c, &v, &e, &ph, &pos) ? ((c * 32u + v) * 32u + e) * 512u + ph * 16u + pos + 1u : 0u);
    }
#endif
#undef SH_H
    return h;
}

static void shell_draw(void)
{
    uint32_t sig, band;
    if (!sh.ready || !scr_frame())                      /* SCREEN OFF: scr_frame is 0 while dark, and nothing is drawn */
        return;
    sig = sh_signature();
    if (sig == sh.sig && !ui.force)
        return;
    sh.sig = sig;
    ui.force = 0;
    cv_oy = 0;
    for (band = 0; band < 2u; band++) {
        sh_y0 = (int32_t)band * 120;
        cv_begin(240, 120, SC_BG);
        if (sh.splash_t) {
            sh_draw_splash();
        } else if (sh.wheel) {
            sh_draw_wheel();
            if (sh_fresh(sh.toast_t, SH_TOAST_MS))     /* (the picker's and the mutes' news, said in the wheel) */
                sh_draw_toast(130, sh_cur()->color);
        } else if (sh_fx_shown()) {
            sh_draw_fx();
        } else if (sh.global == SHG_NIBS) {
            sh_draw_nibs();
        } else if (sh.global == SHG_LEGAL) {
            sh_draw_legal();
        } else if (sh.global == SHG_ABOUT) {
            sh_draw_about();
        } else if (sh.global == SHG_PROJECT) {
            sh_draw_project();
        } else if (sh.global == SHG_PAIR) {
            sh_draw_pair();
        } else {
            sh_draw_head();
            sh_draw_picture();
            if (sh.page[sh.mode] == SHP_SEQ && (sh_kind() == SHK_ENGINE || sh_kind() == SHK_CHORD))
                sh_draw_steps(sh_cur(), sh_trk());
            if (sh_peek_ms()) {                          /* idle: sqrl bouncing (the legend steps aside) */
                sh_draw_peek();
            } else if (sh_hs_which() != NB) {
                sh_draw_holdsee(sh_hs_which());
            } else if (sh_lanes_hint()) {
                sh_draw_lanes_hint();
            } else {
                sh_draw_legend();
            }
            sh_draw_overlays();
            if (sh_pk.leave && !sh_peek_ms())            /* woken: sqrl off the screen, over it all */
                sh_draw_leave();
        }
        cv_blit(0, (uint32_t)sh_y0);
    }
    scr_shown();
}
