/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by MENU > MIDI > MIDI IN (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track; 1.2: CH5-8, CH9-12, CH13-16 as CH1-4 on those four channels (midi_base). Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
#if FELUCCA_SHELL                            /* Braids / Yarns' scales Felucca lacks (Emilie Gillet, MIT):
                                              * append-only, as N_SCALE */
    0x699, 0x5BB, 0x1A3, 0x18B, 0x9CD, 0x9B3, 0x5B3,   /* BLMAJ FOLK JAPAN GAMEL GYPSY ARAB FLAM */
    0xA53, 0x9D3, 0xEB5, 0xEAD, 0x62B, 0x8AD, 0x5B1,   /* MARWA SHREE KHMAJ MIMAL PARMS RANGS GANGS */
    0x6C5, 0x9B5, 0x535, 0x4A3, 0x48B, 0x169, 0x639,   /* KAMES NATBH KAUNS BAIRG BTODI KTODI JOGES */
    0xFFF,                                   /* USER: scale_user's */
#endif
};
_Static_assert(sizeof SCALE_MASK / sizeof SCALE_MASK[0] == sizeof N_SCALE / sizeof N_SCALE[0], "a mask per SCALE name");

#if FELUCCA_SHELL
/* SCALE USER: the scale drawn on the keys of the shell's SCALE page (shell.c sh_mask_poll), from ROOT; a project's */
#define SCALE_USER (sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1u)
#define SCALE_USER_DEF 0xAB5u                  /* (major, until one is drawn) */
static volatile uint16_t scale_user = SCALE_USER_DEF;
#endif
#define KB_SILENT 255u
static uint32_t kb_prev;
#if FELUCCA_SHELL
static volatile uint32_t shell_kb_skip;   /* main (shell.c): keys an instrument of the shell owns; pressed, they never sound */
static volatile uint32_t shell_kb_scl;    /* .. and every key while the SCALE page draws a scale on them */
#if FELUCCA_SHELL
static volatile uint8_t shell_ne_idx;      /* shell.c: a note_engine.h engine in front (+ 1): it gets the keys it owns */
static volatile uint8_t shell_al_front;    /* shell.c: ALIVE in front: its keys are counted too, for its preview */
static void shell_ne_key(uint32_t key, int down);
static uint32_t kb_ne_down;                /* the keys it was given down (only those get their up) */
#endif
static void shell_block(uint32_t n);       /* shell.c: once a block after the keys (ALIVE's clock) */
static void sh_move_isr(uint32_t adv);      /* shell.c: MOVE, before the parts step (adv: the clock's advance) */
static void sh_q_isr(uint32_t adv);         /* shell.c: a queued pattern switch at the bar line, before the parts step */
static volatile uint8_t shell_from_top;     /* seq_start sets it, the shell's MOVE clears it */
#endif
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t in_key;                   /* the key (+ 1) whose notes go into input_on now (key_on), 0: none (MIDI IN) */
static uint8_t kb_vel = 100;             /* 1.5.6 (#84): the keys' velocity (MENU > SELECT VEL, ui_input.c sel_play) */
/* #191: where each track's held[i] came from, so the arp maps it again as it plays (arp_list: TRN, ROOT, SCALE, QNT as
 * they are now). held_key: the key + 1 (bits 0..4) and the octave buttons then + 3 (bits 5..7); 0: not a key's own
 * mapping (MIDI IN, the DRUM grid, an engine's own key map), held[i] plays as it is. held_dt: held[i] above the key's
 * note (a CHRD chord's other notes). Out of track_t: its size stays as the audio ISR's trk[] indexing wants it */
static uint8_t held_key[NTRK][16];
static int8_t held_dt[NTRK][16];
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */
static volatile uint8_t midi_seen;       /* 1.5.5 (#208): bit per track, a MIDI IN note-on played it since the UI last
                                          * looked (ui_input.c midi_follow: the track's light, AUTO SELECT;
                                          * ui.c scr_input: MENU > MIDI > SCREEN WAKE +MIDI) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

/* MENU > MIDI > MIDI IN (G_ROUTE, up to 1.1.5 GLO > SYSTEM ROUT): the first channel (0..15) of the block of four that
 * plays tracks 1..4, -1 with SEL, -2 with TRACK (each track its own: song.mch). Stored
 * 0 CH1-4, 1 SEL, then (1.2, Discussion #143) 2 CH5-8, 3 CH9-12, 4 CH13-16, (1.5.5, #202) 5 TRACK */
static int32_t midi_base(void)
{
    uint32_t r = (uint32_t)song.g[G_ROUTE];
    return r == ROUTE_SEL ? -1 : r == ROUTE_TRACK ? -2 : r ? (int32_t)(r - 1u) * 4 : 0;
}
/* MIDI channel 0..15 of track i (keys -> MIDI out): its channel in ROUT's block (SEL, CH1-4: 1..4); TRACK its own (OMNI,
 * OFF: as CH1-4) */
static uint32_t trk_midi_ch(uint32_t i)
{
    int32_t b = midi_base();
#if FELUCCA_SHELL
    if (b == -1)                           /* SQRL-1, SEL: one channel, whichever part the pair in front is on */
        return 0u;
#endif
    if (b == -2)
        return mch_get(i) < 16u ? mch_get(i) : i % NTRK;
    return (b > 0 ? (uint32_t)b : 0u) + i % NTRK;
}

static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }
#if FELUCCA_SHELL
/* the lanes engines' lanes (shell.c, after Euclidean Circles), RUDIMENT's and LINES', each on a part of its own:
 * on part shell_lanes_part[k] - 1 a lane follows the part's LEN (len 0: it plays the part's step), or wraps at its
 * own (1..64, the part's LEN at most), so lanes of different lengths phase against each other; lane l plays bit l of
 * its step, and its note note[l] (the shell's table: a drum piece under DRUMS, a note under any other SOUND) */
static volatile uint8_t shell_lanes_part[NLANES], shell_lane_len_i[NLANES][NLANE];
/* ACID (shell.c) on part shell_acid_part - 1: its notes moved as they play, shell_acid_deg degrees along the part's
 * scale (CHR: semitones), then shell_acid_semi semitones (OCT, and the keys' transposition while it plays) */
static volatile uint8_t shell_acid_part;
static volatile int8_t shell_acid_deg, shell_acid_semi;
/* .. and FLUX (shell_acid_flux, %): as a step plays, the chance it changes: its note moves a degree or two along
 * the scale or an octave (or back to as written), or its ACCENT or SLIDE flips (one change in eight each), and stays
 * so pass after pass; FLUX 0 freezes the line as it plays now, PLAY from stop starts from the line as written. The
 * steps are never changed */
static volatile uint8_t shell_acid_flux;
static uint8_t acid_mut[NSTEP / 2u];            /* each step's move, a signed nibble: degrees (-2..2) + 5 * octaves */
static uint32_t acid_fl[2][NSTEP / 32u];       /* each step's ACCENT ([0]) and SLIDE ([1]) flipped by FLUX */
static uint32_t acid_frng = 0x6D2B79F5u;
static int32_t acid_mv(uint32_t q)              /* step q's move: degrees + 5 * octaves (0: as written) */
{
    return (int32_t)((((acid_mut[q / 2u] >> (q & 1u) * 4u) & 15u) ^ 8u)) - 8;
}
static void acid_mv_set(uint32_t q, int32_t m)
{
    uint32_t sh = (q & 1u) * 4u;
    acid_mut[q / 2u] = (uint8_t)((acid_mut[q / 2u] & ~(15u << sh)) | ((uint32_t)m & 15u) << sh);
}
/* a logic lane (shell.c: FILL turned left past 0, lanes 3 to 8): it plays the two lanes above it, A (l - 1) and B
 * (l - 2), through its op, as a Cold Mac or a logic module would; 0: a lane of its own */
enum { LOP_NONE, LOP_AND, LOP_OR, LOP_XOR, LOP_NOR, LOP_ANOTB, LOP_FLIP, LOP_LATCH, LOP_N };
static volatile uint8_t shell_lane_op_i[NLANES][NLANE];
static uint8_t lane_ff[NLANES][NLANE];                  /* FLIP's and LATCH's state (ISR) */
static volatile uint8_t shell_lane_note_i[NLANES][NLANE] = {{36, 38, 39, 42, 46, 45, 37, 56},
                                                            {36, 38, 39, 42, 46, 45, 37, 56}};   /* (DRUM_LANE_NOTE) */
/* a lane's DIV (shell.c): its step's length in twelfths of the part's step (x2 24, 3/2 18, 4/3 16, 3/4 9, 2/3 8, 1/2 6,
 * 1/3 4, 1/4 3); 0 the part's own, lane_at's. A lane with a DIV runs a clock of its own on the part's swung grid
 * (seq_lanes_div): its steps start at multiples of the DIV counted in twelfths from the transport's start (lane_dnext),
 * at its own pos, wrapping at its own LEN or the part's, and it plays and ends its own notes, so the column's notes
 * are never cut. A logic lane, and a lane MAP plays, run at the part's DIV */
static volatile uint8_t shell_lane_div_i[NLANES][NLANE];
static uint32_t lane_dnext[NLANES][NLANE], lane_doff[NLANES][NLANE];   /* (ISR) the next step's twelfth; its gate left */
static uint8_t lane_dnote[NLANES][NLANE], lane_dwas[NLANES][NLANE];    /* .. the note sounding + 1; the DIV last seen */
static uint32_t seq_abs[NTRK];                      /* each part's steps since the transport started (the grid's k) */
static inline uint32_t lane_div(int32_t lk, uint32_t i);
/* LANE 2 (shell.c): a lane's LOOP (bars, 0: off), WAKE (bars or loops it plays, 1..8) and NAP (then rests, 0: never).
 * Each lane rolls its CHNC from its own state (lane_rs); with LOOP on, each loop starts the rolls again from its seed
 * (shell_lane_seed_i), so its chances repeat bar for bar. A bar is 16 of the part's steps from the transport's start
 * (seq_abs), so a pattern switch keeps the count and PLAY starts it again */
static volatile uint8_t shell_lane_loop_i[NLANES][NLANE], shell_lane_wake_i[NLANES][NLANE], shell_lane_nap_i[NLANES][NLANE];
static uint32_t shell_lane_seed_i[NLANES][NLANE];
static uint32_t lane_rs[NLANES][NLANE], lane_rs_bar[NLANES][NLANE];   /* (ISR) the rolls; as they were at the bar's start */
static uint32_t lane_bar_at[NLANES];                    /* (ISR) the bar step done + 1 */
static inline uint32_t lane_dlen(const track_t *t, int32_t lk, uint32_t i);
static inline int32_t lanes_of(const track_t *t)        /* the lanes engine t plays, or -1 */
{
    uint32_t p = trk_index(t) + 1u;
    return p == shell_lanes_part[0] ? 0 : p == shell_lanes_part[1] ? 1 : -1;
}
#endif
/* the note lane i's hit plays on t: a lanes engine's table on its part, else the DRUM map */
static inline uint32_t lane_note(const track_t *t, uint32_t i);
/* .. on step s: a lanes engine's hit in its own octave (not under DRUMS: a kit's pieces stay) */
static inline uint32_t lane_note_at(const track_t *t, const step_t *s, uint32_t i)
{
    int32_t n = (int32_t)lane_note(t, i);
#if FELUCCA_SHELL
    if (t->eng_req != ENGI_DRUM && lanes_of(t) >= 0)
        n += 12 * step_loct(s, i);
#endif
    (void)s;
    return (uint32_t)(n < 0 ? 0 : n > 127 ? 127 : n);
}
static inline uint32_t lane_note(const track_t *t, uint32_t i)
{
#if FELUCCA_SHELL
    int32_t k = lanes_of(t);
    if (k >= 0)
        return shell_lane_note_i[k][i % NLANE];
#endif
    (void)t;
    return DRUM_LANE_NOTE[i % NLANE];
}
/* the lane a recorded note goes into: on RUDIMENT's part the lane playing that note (NLANE: none, a note), on a DRUM
 * part the lane of its piece; -1 elsewhere (a part that keeps notes) */
#if FELUCCA_SHELL
static int8_t rec_loct;                         /* rec_lane's last: the octave of the lane's note it found (LINES) */
static int rec_lo_part(const track_t *t)        /* a lanes part whose just[] holds its hits' octaves (not DRUMS) */
{
    return lanes_of(t) >= 0 && t->eng_req != ENGI_DRUM;
}
#else
#define rec_lo_part(t) 0
#endif
static int32_t rec_lane(const track_t *t, uint32_t note)
{
#if FELUCCA_SHELL
    int32_t k = lanes_of(t);
    rec_loct = 0;
    if (k >= 0) {
        static const int8_t O[5] = {1, -1, 2, -2, 3};   /* (another octave of a lane's note: that lane, that octave) */
        uint32_t i, j;
        for (i = 0; i < NLANE && shell_lane_note_i[k][i] != note; i++)
            ;
        for (j = 0; i == NLANE && !drum_track(t) && j < 5u; j++)
            for (i = 0; i < NLANE && (int32_t)shell_lane_note_i[k][i] + 12 * O[j] != (int32_t)note; i++)
                ;
        if (i < NLANE && j)
            rec_loct = O[j - 1u];
        if (i < NLANE || drum_track(t))
            return (int32_t)i;
        return -1;
    }
#endif
    return drum_track(t) ? (int32_t)drum_lane(note) : -1;
}

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10). The DRUM grid: white keys are steps, black keys 1..8 lanes, 9 ACC, 10 / 11 the page */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}
enum { GK_ACC = NLANE, GK_PGDN, GK_PGUP };       /* black keys 9..11 on the grid */
/* the DRUM grid: key k's lane note (black keys 1..8), KB_SILENT for the rest. Out of line (1.5): called from the audio
 * ISR's key loop only on a key change; inlined there, its % 12 and key_place's loop sat in the ISR's static cost
 * (tests/target_budget.py) for a path that runs once per key press */
static __attribute__((noinline)) uint8_t grid_key_note(uint32_t k)
{
    return key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
}
/* GLO > SONG (song.grid 3): key k's slot, the white keys A3 B3 C4 D4 (white places 2..5: keys 4 6 7 9 from F) A..D; -1 = a
 * key that plays. A table, not key_place (a loop); the audio ISR's key loop reads SONG_KEYS, the same keys */
#define SONG_KEYS (1u << 4 | 1u << 6 | 1u << 7 | 1u << 9)
static int32_t song_key(uint32_t k)
{
    static const int8_t S[10] = {-1, -1, -1, -1, 0, -1, 1, 2, -1, 3};
    return k < 10u ? S[k] : -1;
}

enum { QN_OFF, QN_SNAP, QN_WHITE, QN_SEQ, QN_SEQT, QN_NEAR, QN_DROP, QN_CHORD };   /* P_QUANT (SEQ+T, 1.5.6 #218:
                                                                         * appended; older firmware loads SEQ. NEAR,
                                                                         * DROP, CHORD: SQRL-1) */
/* QNT CHORD: the scale is the chord the source parts sound (the shell's PLAY parts), a part never its own source.
 * qn_chord_block takes it once a block: the pitch classes of the notes held, with the ones ringing out when fewer
 * than two are held (an arp's), and keeps the last while nothing sounds; 0, none yet: the part's own scale */
static volatile uint8_t qn_chord_src;
static uint16_t qn_chord_pc[NTRK];
static void qn_chord_block(void)
{
    uint32_t p, s, i, any = 0;
    for (p = 0; p < NTRK; p++)
        any |= trk[p].p[P_QUANT] == QN_CHORD;
    if (!any || !qn_chord_src)
        return;
    for (p = 0; p < NTRK; p++) {
        uint32_t held = 0, ring = 0;
        for (s = 0; s < NTRK; s++) {
            if (s == p || !((qn_chord_src >> s) & 1u))
                continue;
            for (i = 0; i < NVOICE; i++) {
                const voice_t *v = &trk[s].v[i];
                if (v->active && v->stage != 4u) {
                    if (v->gate)
                        held |= 1u << (v->note % 12u);
                    else
                        ring |= 1u << (v->note % 12u);
                }
            }
        }
        if (__builtin_popcount(held) < 2)
            held |= ring;
        if (held)
            qn_chord_pc[p] = (uint16_t)held;
    }
}
static uint32_t scale_mask(const track_t *t)
{
    uint32_t pc = t->p[P_QUANT] == QN_CHORD ? qn_chord_pc[trk_index(t) % NTRK] : 0u, r;
    if (pc) {                                           /* (the chord's pitch classes, counted from ROOT) */
        r = (uint32_t)t->p[P_ROOT] % 12u;
        return ((pc >> r) | (pc << (12u - r))) & 0xFFFu;
    }
#if FELUCCA_SHELL
    if ((uint32_t)t->p[P_SCALE] == SCALE_USER)
        return scale_user & 0xFFFu ? scale_user & 0xFFFu : 0xFFFu;
#endif
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}
/* NEAR's rounding: NEAR, and CHORD (to the nearest chord tone) */
static int qn_near(const track_t *t) { return t->p[P_QUANT] == QN_NEAR || t->p[P_QUANT] == QN_CHORD; }

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys and QNT SEQ's sequenced notes */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

static int scale_has(const track_t *t, int32_t n)      /* n is on the track's scale */
{
    return (int)((scale_mask(t) >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u);
}
static uint8_t qn_last[NTRK], qn_klast[NTRK];          /* the note NEAR gave each track's pattern and keys last (0: none) */
/* QNT NEAR: n onto the nearest note of the scale; a tie (a note a semitone from both, as most are in a scale of
 * steps of 1 and 2) goes toward the last note, so a line stays close to where it came from (Braids' hysteresis, in
 * whole notes). Both NEAR and DROP act as SEQ does: on the keys and on the pattern as it plays */
static int32_t scale_near(const track_t *t, int32_t n, uint32_t last)
{
    int32_t d, r = n;
    for (d = 0; d < 12; d++) {
        int dn = n - d >= 0 && scale_has(t, n - d), up = n + d <= 127 && scale_has(t, n + d);
        if (dn && up)
            r = last && (int32_t)last > n ? n + d : n - d;
        else if (dn || up)
            r = dn ? n - d : n + d;
        else
            continue;
        break;
    }
    return clamp(r, 0, 127);
}

/* the engine's own key map (DRUM's GM map, slices) for key k, -1 none */
static int32_t kb_engine(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    return e->keys ? e->keys(t, k) : -1;
}

/* key k's note with the octave buttons at oct: QNT, ROOT, SCALE, TRN (KB_SILENT: a black key under WHITE). The keys
 * (kb_map) and the arp's latched keys (arp_list, #191: as the keys play now, the octave they were pressed in) */
static uint32_t kb_pitch(const track_t *t, uint32_t k, int32_t oct)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (qn_near(t))                                     /* NEAR: every key to the nearest; DROP: one off the scale is */
        return (uint32_t)scale_near(t, n + 12 * oct + t->p[P_TRANS], qn_klast[trk_index(t) % NTRK]);   /* silent */
    if (t->p[P_QUANT] == QN_DROP)
        return scale_has(t, n + 12 * oct + t->p[P_TRANS]) ? (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127) : KB_SILENT;
    if (t->p[P_QUANT] == QN_SNAP || t->p[P_QUANT] >= QN_SEQ)   /* SNAP (and SEQ, SEQ+T): every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * oct + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], wo;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        wo = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            wo--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * wo + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    int32_t n = kb_engine(t, k);
    return n >= 0 ? (uint32_t)n : kb_pitch(t, k, song.octave);
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
#if FELUCCA_SHELL
static uint8_t arp_vel = 100;                       /* the velocity of the arp's next note (GROOVE's HUMAN: arp_step) */
#define ARP_VEL arp_vel
#else
#define ARP_VEL 100
#endif
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i, k = in_key - 1u;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u) {
        /* #191: a key the track's scale keyboard maps (not the DRUM grid, not an engine's own map) is kept as that
         * key, so TRN, ROOT, SCALE and QNT reach the notes the arp holds (arp_list); the octave stays the one it was
         * pressed in. Anything else (MIDI IN) plays as it came, as before */
        uint8_t *hk = held_key[trk_index(t)];
        hk[t->nheld] = in_key && song.grid != 1u && kb_engine(t, k) < 0
                           ? (uint8_t)(in_key | (uint32_t)(song.octave + 3) << 5) : 0u;
        held_dt[trk_index(t)][t->nheld] = (int8_t)(hk[t->nheld] ? (int32_t)note - kb_note[k] : 0);
        t->held[t->nheld++] = (uint8_t)note;
    }
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    uint8_t *hk = held_key[trk_index(t)];
    int8_t *dt = held_dt[trk_index(t)];
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note) {
            hk[k] = hk[i];
            dt[k] = dt[i];
            t->held[k++] = t->held[i];
        }
    t->nheld = (uint8_t)k;
}

#define ND_NONE 0u                      /* track_t nd_late / nd_early: nothing latched (seq_tick; else its seq_pos + 1) */
static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing && !chain.running; }
/* REC MODE (the shell's REC + SELECT): ADD puts a note in with what the step holds, REPLACE clears what an earlier pass
 * left there with the first note this pass puts in. The arp and the generators always record as REPLACE, so a pass
 * rewrites the steps it plays into and a latched source left armed never piles up (rec_machine). rec_tag: the pass
 * each step was last written in (rec_pass counts them, at step 1: seq_enter); a note pressed in a step's second half
 * goes into the next step, and from the last step into the next pass */
#if FELUCCA_SHELL
static uint8_t rec_replace, rec_machine;
static uint8_t rh_rep[NTRK];                /* the hold under way was recorded as REPLACE */
static volatile uint8_t shell_gen_part;     /* shell.c: the part a generator plays on in front (+ 1), 0 none */
#else
#define rec_replace 0u
#define rec_machine 0u
#define rh_rep_of(p) 0u
#endif
static uint16_t rec_pass[NTRK], rec_tag[NTRK][NSTEP];   /* (16 bits: a round is a day and more, never mid-take) */
#if FELUCCA_SHELL
/* ADD with the arp (or a generator) recording: its notes go in with the chord the step holds, and the next pass takes
 * back only its own (rec_mnote: bit j, note j came from a machine pass), so a latched arp never piles up and never
 * eats the chords played by hand. Cleared at a part's step 1 while it is not armed */
static uint8_t rec_mnote[NTRK][NSTEP], rec_many;
#endif
static uint32_t rec_pitch(const track_t *t, uint32_t note);
static void rec_pass_next(uint32_t p)       /* a new pass on part p (step 1, or a LEN that ends behind the playhead) */
{
#if FELUCCA_SHELL
    if (((rec_many >> (p % NTRK)) & 1u) && !((song.rec >> (p % NTRK)) & 1u)) {
        memset(rec_mnote[p % NTRK], 0, sizeof rec_mnote[0]);
        rec_many &= (uint8_t)~(1u << (p % NTRK));
    }
#endif
    if (!++rec_pass[p % NTRK]) {            /* (round: the old passes' marks go, 0 is none) */
        memset(rec_tag[p % NTRK], 0, sizeof rec_tag[p % NTRK]);
        rec_pass[p % NTRK] = 1u;
    }
}
#if FELUCCA_SHELL
#define rh_rep_of(p) rh_rep[(p) % NTRK]
static void rec_note_machine(track_t *t, uint32_t note, uint32_t vel)   /* the arp's, a generator's (ISR) */
{
    rec_machine = 1;
    rec_note(t, note, vel);
    rec_machine = 0;
}
#else
#define rec_note_machine rec_note
#endif

#if FELUCCA_SHELL
static void sh_take_note(track_t *t);           /* (shell.c: a take's first note keeps the steps before it, for UNDO) */
#endif
#include "motion.c"

/* the ARP's modes (P_AMODE). Append-only: a stored value keeps its meaning. ORD (1.2, it played as UP before) plays
 * the held notes in the order they were pressed, whatever ARP 2's ORD switch says (that switch orders the list of the
 * other modes). 1.2 (Discussion #95), over the note list (the held notes low to high, or as played,
 * across OCT octaves), C E G held: DNUP G E C E; UP+8 C E G C' (the first note again an octave above the top);
 * CONV C G E (outside in); DIVG E G C (inside out); PINKY C G E G (the top note between the others); THUMB
 * C E C G (the bottom one); WALK a random step up or down the list (bouncing off its ends); CHORD all the held
 * notes together (the first 4), an octave higher each step through OCT */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static void arp_forget(track_t *t)               /* (its notes ended otherwise: trk_all_off) */
{
    t->arp_note = t->arp_ch[0] = t->arp_ch[1] = t->arp_ch[2] = 0;
}

/* CHORD's notes. Not inlined: they run once an arp step, and kept out of the audio ISR's loop over the tracks
 * (tests/target_budget.py) */
static __attribute__((noinline)) void arp_chord_off(track_t *t, int rec)   /* the other notes end */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (t->arp_ch[i]) {
            if (rec)
                rec_release(t, t->arp_ch[i]);
            trk_note_off(t, t->arp_ch[i]);
            t->arp_ch[i] = 0;
        }
}

static __attribute__((noinline)) void arp_chord(track_t *t, const uint32_t *list, uint32_t n)
{
    uint32_t i, nt, top = 0, o = t->arp_idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    if (n > 4u)
        n = 4u;
    for (i = 0; i < n; i++) {                       /* the held notes (sorted or as played), o octaves up */
        nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (nt == 127u && top++)
            continue;                               /* (notes clamped onto the top one: once) */
        if (!t->arp_note)
            t->arp_note = (uint8_t)nt;
        else
            t->arp_ch[i - 1u] = (uint8_t)nt;
        if (rec_on(t))
            rec_note_machine(t, nt, ARP_VEL);
        trk_note_on(t, nt, ARP_VEL);
    }
}

static void arp_silence(track_t *t)              /* the sounding arp note(s) end */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, 0);
    trk_note_off(t, t->arp_note);
    t->arp_note = 0;
}

static void arp_off(track_t *t)                  /* the sounding arp note(s) end (a recorded one too) */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, rec_on(t));
    trk_note_off(t, t->arp_note);
    if (rec_on(t))
        rec_release(t, t->arp_note);
    t->arp_note = 0;
}

/* the place in the note list (len notes) the step arp_idx plays (not inlined, as arp_chord); UP+8 adds its note */
static __attribute__((noinline)) uint32_t arp_pick(track_t *t, uint32_t *list, uint32_t len)
{
    uint32_t i = t->arp_idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8 && len < 64u)                   /* UP+8: the first note again over the top */
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = t->arp_walk;
        if (!i)
            w = 0;                                  /* (from the bottom) */
        else if (w >= len)
            w = len - 1u;                           /* (notes let go) */
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        t->arp_walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

/* n: samples the arp advances by (the external clock's while it runs, 0 stopped); gate_n: samples its
 * gate counts down by (real time when the external transport stops: a tapped or latched note still ends) */
static volatile uint32_t beat_pos, beat_n;      /* samples into the beat, the beat of the bar (0..3) */

/* the metronome (click.c) and the count-in (1.1, Discussion #131). The click follows the song tempo's quarter notes
 * (BPM, or the external clock's 24 pulses), 4 to the bar, the first accented, whatever a track's DIV or SWING: a beat is
 * four 1/16 steps as the sequencer counts them (click_beat_len: 4 x div_samples(1/16), which may be a few samples
 * shorter than beat_samples, so the click never drifts from the 1/16 tracks it is recorded against). clk_pos and
 * clk_step count the 1/16 steps as seq_tick counts a 1/16 track's (and midi_clock_pulse rescales clk_pos with them when
 * an external clock's tempo moves): CLK_START at seq_start clicks the first beat in the block step 0 starts in, and
 * beat k in the block a 1/16 track's step 4k starts in (every 4th step; step 0 of 16, the bar's first, accented).
 * The count-in: with MENU > COUNT-IN on, PLAY from stop with a track armed (REC while stopped starts PLAY too) on
 * the internal clock counts cin_total beats first (1 or 2 bars, clicked whatever CLICK is), then
 * starts the sequencer at step 1 exactly that many beats later (song.playing stays 0 until then: nothing plays or
 * records; the keys sound). A note into an armed track in the count-in's last eighth (the second half of its last
 * beat) is recorded onto step 1 when it starts (cin_keep, cin_flush), held on or let go as it was. An external clock
 * never counts in (its Start starts). STOP (seq_stop) cancels it. */
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* samples into the 1/16 step, the step of the bar (0..15) */
static volatile uint8_t cin_left, cin_total;    /* the count-in's beats still to come (0: none), of how many */
static volatile uint32_t cin_pos;               /* samples into its beat playing */
static uint8_t cin_flush;                       /* the count-in ended in this block: record what was kept */
static uint8_t cin_note[NTRK][STEP_NOTES], cin_vel[NTRK][STEP_NOTES], cin_n[NTRK], cin_up[NTRK];   /* kept notes; cin_up bit k: let go */
#if FELUCCA_SHELL
static int8_t cin_just[NTRK][STEP_NOTES];      /* .. and each one's TUNE JUST */
#endif
static int seq_counting(void) { return cin_left != 0u; }
#if FELUCCA_SHELL
/* SQRL-1's REC + PLAY (shell.c): a one-bar count-in for this start whatever COUNT-IN says, and the click through the
 * count-in and the take whatever CLICK says; the transport's stop ends it */
static volatile uint8_t rec_cin;
#else
#define rec_cin 0u
#endif
static uint32_t click_beat_len(void) { return 4u * div_samples(2); }   /* DIV 2: 1/16 */
static int click_wanted(void)                  /* CLICK REC: while a track is armed (not in a song: no recording) */
{
    return click_mode == CLICK_ON || ((click_mode == CLICK_REC || rec_cin) && song.rec && !chain.running);
}
/* the arp's note list (list, its length returned; 0: nothing to play): the held notes (sorted or as played) over OCT
 * octaves, *cnt of them. #191: the notes as they play now, a key's mapped again through kb_pitch (TRN, ROOT, SCALE,
 * QNT now, the octave it was pressed in; a chord's note as far above it), the rest (MIDI IN) as they came; each once
 * (two keys may meet on one note now), a key silent now (a black key, QNT WHITE since) left out. With nothing changed
 * since the keys were pressed, exactly the list of held[] as before */
static uint32_t arp_list(const track_t *t, uint32_t *list, uint32_t *cnt)
{
    uint32_t i, j, n = 0, x, s, last = 0, len = 0, o;
    const uint8_t *hk = held_key[trk_index(t)];
    const int8_t *dt = held_dt[trk_index(t)];
    for (i = 0; i < t->nheld; i++) {
        x = t->held[i];
        if ((s = hk[i]) != 0u) {
            x = kb_pitch(t, (s & 31u) - 1u, (int32_t)(s >> 5) - 3);
            if (x == KB_SILENT)
                continue;
            x = (uint32_t)clamp((int32_t)x + dt[i], 0, 127);
        }
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!t->p[P_AORDER] && t->p[P_AMODE] != AM_ORD)   /* (ORD: as pressed, held[] keeps that order) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    if (n && t->p[P_AMODE] == AM_REPEAT) { list[0] = last; len = 1; } /* REPEAT: last played note, no octave traversal */
    return len;
}

#if FELUCCA_SHELL
/* the part's pattern holds a note within LEN; a live source over it, the part not armed while it plays, gives way:
 * the generators (shell.c sh_gen_give_way) and a latched arp (arp_fire) */
static int sh_pat_notes(const track_t *t)
{
    uint32_t k, len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    for (k = 0; k < len && k < NSTEP; k++)
        if (t->step[k].time == ST_NOTE && (t->step[k].n || t->step[k].hit))
            return 1;
    return 0;
}
static int sh_gen_quiet(const track_t *t) { return song.playing && !rec_on(t) && sh_pat_notes(t); }
static uint32_t kb_latched;                             /* (CHORD's LATCH, below) */
/* the arp, or the keys' latch, plays by itself: HOLD's notes with no key down, or CHORD's LATCH holding a root with
 * no key of the part down (the ARP light breathes; over its own take the arp goes quiet) */
static int arp_alone(const track_t *t)
{
    uint32_t k, p = trk_index(t);
    if (t->p[P_AHOLD])
        return t->nheld && !t->arp_phys;
    if (!kb_latched || shell_chord_part != p + 1u)
        return 0;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes >> k) & 1u && kb_trk[k] == p)
            return 0;
    return 1;
}
#endif
/* an arp step starts (period: its length): the next note of the list. Not inlined: once a step, kept out of the audio
 * ISR's loop over the tracks (tests/target_budget.py) */
static __attribute__((noinline)) void arp_fire(track_t *t, uint32_t period)
{
    uint32_t cnt, list[64], len, j;
    if (!(len = arp_list(t, list, &cnt))) {        /* (every key silent now: QNT WHITE's black keys) */
        if (t->arp_note)
            arp_off(t);
        return;
    }
    t->arp_idx++;
    j = t->p[P_AMODE] == AM_CHORD ? 0u : arp_pick(t, list, len);
    if (t->arp_note)
        arp_off(t);
#if FELUCCA_SHELL
    if (arp_alone(t) && sh_gen_quiet(t))            /* latched over its own take, not armed: quiet, as a generator */
        return;
#endif
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (t->p[P_AMODE] == AM_CHORD) {            /* CHORD: the first 4 notes, an octave up each step */
            arp_chord(t, list, cnt);
            return;
        }
        t->arp_note = (uint8_t)list[j];
        if (rec_on(t))
            rec_note_machine(t, t->arp_note, ARP_VEL);       /* recording: what the arp plays */
        trk_note_on(t, t->arp_note, ARP_VEL);
    }
}

#if FELUCCA_SHELL
static uint32_t rs_t;                               /* samples since the transport started (RPT: or the chord was
                                                     * pressed, stopped); the arp's grid while playing */
#endif
#if FELUCCA_SHELL
/* a grid point in (a, b]: point k at floor(k x pq) (fx.c grid_at: pq the period in Q16) + its GROOVE offset (core.h
 * groove_off, depth, the part's own HUMAN); *k its index. The offsets stay under half a period, so the points keep
 * their order */
static int grid_due(uint32_t a, uint32_t b, uint64_t pq, int32_t depth, uint32_t part, uint32_t *k)
{
    uint32_t period = (uint32_t)(pq >> 16), j = (uint32_t)(((uint64_t)a << 16) / pq), e;
    for (e = j + 2u, j = j ? j - 1u : 0u; j <= e; j++) {
        int64_t at = (int64_t)grid_at(pq, j) + groove_off(depth, period, j, part);
        if (at > (int64_t)a && at <= (int64_t)b) {
            *k = j;
            return 1;
        }
    }
    return 0;
}
#endif
static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period;
    int32_t sw;
    if (t->arp_note) {
        if (t->arp_off <= gate_n)
            arp_off(t);
        else
            t->arp_off -= gate_n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_off(t);
        return;
    }
#if FELUCCA_SHELL
    period = rate_samples((uint32_t)t->p[P_ARATE]);   /* (the dotted ones, 4T and 32T too) */
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    if (t->arp_pos == 0xFFFFFFF) {                 /* a first note (arp_add): now; the grid takes over after it */
        t->arp_pos = 0;
        if (song.playing && !song.g[G_CLOCK]) {     /* playing: now in a RATE cell's first half, else on the next */
            uint64_t pq = rate_q16((uint32_t)t->p[P_ARATE]);   /* grid point, so a take holds a note a cell */
            if (rs_t - grid_at(pq, (uint32_t)(((uint64_t)rs_t << 16) / pq)) >= period / 2u)
                return;
        }
        arp_vel = 100;
        arp_fire(t, period);
        return;
    }
    if (song.playing && !song.g[G_CLOCK]) {         /* playing: on the RATE grid from the transport's start (rs_t, this
                                                     * block's end), shaped by GROOVE with the arp's own SWG */
        uint32_t k;
        if (grid_due(rs_t - n, rs_t, rate_q16((uint32_t)t->p[P_ARATE]), t->p[P_ASWING], trk_index(t), &k)) {
            arp_vel = (uint8_t)groove_vel(100u, t->p[P_ASWING], period, k, trk_index(t));
            arp_fire(t, period);
        }
        return;
    }
    {   /* stopped, or another clock: free, each step's overshoot kept (none lost to the block it fell in) */
        uint64_t pq = rate_q16((uint32_t)t->p[P_ARATE]);   /* (the Q16 period: its steps floor and ceiling) */
        uint32_t wait = grid_at(pq, t->arp_idx + 1u) - grid_at(pq, t->arp_idx) +
                        (uint32_t)(groove_off(t->p[P_ASWING], period, t->arp_idx + 1u, trk_index(t)) -
                                   groove_off(t->p[P_ASWING], period, t->arp_idx, trk_index(t)));
        (void)sw;
        t->arp_pos += n;
        if (t->arp_pos < wait)
            return;
        t->arp_pos -= wait;
        if (t->arp_pos >= period)                   /* (a rate turned shorter meanwhile: from now) */
            t->arp_pos = 0;
        arp_vel = (uint8_t)groove_vel(100u, t->p[P_ASWING], period, t->arp_idx + 1u, trk_index(t));
    }
    arp_fire(t, period);
#else
    period = div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    t->arp_pos = 0;
    arp_fire(t, period);
#endif
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

/* SQRL-1 has no QUANTIZE key: a step's NUDGE always plays (set by hand on a held step, or by the editor), and
 * QUANTIZE ON keeps a live take on the grid by recording none. Felucca records the timing and lets QUANTIZE decide */
#if FELUCCA_SHELL
#define SEQ_QNT_PLAY(t) ((void)(t), 0)
#define SEQ_QNT_REC(t) ((t)->p[P_SQNT] != 0)
#else
#define SEQ_QNT_PLAY(t) ((t)->p[P_SQNT] != 0)
#define SEQ_QNT_REC(t) 0
#endif
/* NUDGE (1.2, #162): a note `off` samples from the start of the step it goes into, in 1/NUDGE_DIV of the step (period),
 * rounded (step_set_nudge keeps -8..+7): the first note of a fresh step sets it; QUANTIZE (P_SQNT) decides whether it
 * plays (not inlined: its divide stays out of the audio ISR's loops) */
static __attribute__((noinline)) int32_t rec_nudge(int32_t off, uint32_t period)
{
    return (off * NUDGE_DIV + (off < 0 ? -(int32_t)period / 2 : (int32_t)period / 2)) / (int32_t)(period ? period : 1u);
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only), and so does a step that already plays (REPLACE: one this
 * pass wrote), and the loop come round to its start. */
#if FELUCCA_SHELL
#endif
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len, idx, k, period, cur, next, sounds, p = trk_index(t), rep, heard = note;
#if FELUCCA_SHELL
    uint32_t added;
#endif
    uint16_t wp;
    int32_t off;
    step_t *s;
    note = rec_pitch(t, note);
#if FELUCCA_SHELL
    sh_take_note(t);                                /* (it may set LEN: the first take's) */
#endif
    len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    idx = t->seq_idx % len;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    cur = step_samples(t, period, t->seq_idx);
    next = t->seq_pos > cur / 2u;
    off = rec_nudge(t->seq_pos >= cur || SEQ_QNT_REC(t) ? 0 : next ? -(int32_t)(cur - t->seq_pos) : (int32_t)t->seq_pos,
                    period);
    if (next)
        idx = (idx + 1u) % len;
    s = &t->step[idx];
    /* the step's notes play later in this pass (it is the next one, not played early yet, or the one playing whose
     * nudge has not come yet): what sounds now must not play again there (rskip) */
    sounds = next ? t->nd_fired != idx + 1u : t->nd_late != ND_NONE;
    if (rec_lane(t, note) >= 0) {                   /* DRUM, or a lane's note on RUDIMENT's part: into the grid, no
                                                     * holds (hits) */
        uint32_t l = (uint32_t)rec_lane(t, note);
#if FELUCCA_SHELL
        int32_t lk = lanes_of(t);
        if (lk >= 0 && l < NLANE && lane_div(lk, l)) {           /* a DIV lane: the step of its own sounding now */
            uint32_t ln = lane_dlen(t, lk, l);
            s = &t->step[(shell_lane_pos_i[lk][l] + ln - 1u) % ln];
        } else if (lk >= 0 && l < NLANE && shell_lane_len_i[lk][l] && shell_lane_len_i[lk][l] < len) {   /* a lane of
                                                                         * its own */
            uint32_t own = shell_lane_len_i[lk][l];                    /* LEN: the hit on the step it plays, not */
            s = &t->step[(shell_lane_pos_i[lk][l] + own - 1u + next) % own];   /* the column's */
        }
        if (s->time == ST_REST || (s->time == ST_NOTE && !s->n && !s->hit)) {   /* (SLIDE and a TIE column stay) */
            s->n = s->hit = s->acc = 0;
            s->flags &= SF_SLIDE;
            s->vel = 0;
            step_just_clear(s);
            step_set_nudge(s, off);
        }
        if (s->time != ST_TIE)
            s->time = ST_NOTE;
        if (!s->vel && (s->n || s->hit))
            s->vel = 96u;                           /* (a step entered by hand plays 96: an overdub keeps the louder) */
#else
        if (s->time != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
            step_just_clear(s);
            step_set_nudge(s, off);
        }
        s->time = ST_NOTE;
#endif
#if FELUCCA_SHELL
        if (l < NLANE && rec_lo_part(t) && rec_loct && note == (uint32_t)((int32_t)lane_note(t, l) + 12 * rec_loct)) {
            s->hit |= (uint8_t)(1u << l);               /* (its note an octave or more away: the hit, in its octave) */
            step_set_loct(s, l, rec_loct);
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else
#endif
        if (l < NLANE && note == lane_note(t, l)) { /* a lane's note: its hit */
            s->hit |= (uint8_t)(1u << l);
#if FELUCCA_SHELL
            if (rec_lo_part(t))
                step_set_loct(s, l, 0);
#endif
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else {                                    /* another GM drum (a low tom, a crash): a note */
            for (k = 0; k < s->n && s->note[k] != note; k++)
                ;
            if (k == s->n) {
                if (s->n < STEP_NOTES)
                    s->n++;
                s->note[s->n - 1u] = (uint8_t)note;
            }
            if (vel >= 127u)
                s->flags |= SF_ACCENT;
        }
        if (vel > s->vel)
            s->vel = (uint8_t)vel;
        t->seq_active = 1;
        if (sounds) {
            if (t->rskip_idx != idx)
                t->rskip_n = 0;
            t->rskip_idx = (uint8_t)idx;
            if (t->rskip_n < NLANE)
                t->rskip[t->rskip_n++] = (uint8_t)note;
        }
        return;
    }
    rep = rec_replace || rec_machine;
    wp = (uint16_t)(rec_pass[p] + (next && !idx));  /* (the pass the note goes into) */
    if (wp == 0u)
        wp = 1u;
#if FELUCCA_SHELL
    if (rec_machine && !rec_replace && s->time == ST_NOTE && s->n && trk_vmode(t) == V_POLY) {
        rep = 0;                                    /* ADD: the step's chord stays */
        if (rec_tag[p][idx] != wp && rec_mnote[p][idx]) {   /* a new pass: the earlier passes' arp notes go */
            uint32_t j, n = 0;
            for (j = 0; j < s->n; j++)
                if (!((rec_mnote[p][idx] >> j) & 1u)) {
                    if (!rec_lo_part(t))
                        step_set_just(s, n, step_just(s, j));
                    s->note[n++] = s->note[j];
                }
            s->n = (uint8_t)n;
            rec_mnote[p][idx] = 0;
        }
    }
#endif
    if (s->time != ST_NOTE || !s->n || trk_vmode(t) != V_POLY || (rep && rec_tag[p][idx] != wp)) {
        s->n = 0;                                   /* a fresh step (REPLACE: an earlier pass's) */
        s->flags = 0;
        s->vel = 0;
        if (!rec_lo_part(t))                        /* (a lanes part's just[]: its hits' octaves, kept) */
            step_just_clear(s);
        step_set_nudge(s, off);
#if FELUCCA_SHELL
        rec_mnote[p][idx] = 0;
#endif
    }
    if (!s->vel && (s->n || s->hit))
        s->vel = 96u;                               /* (a step entered by hand plays 96: an overdub keeps the louder) */
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
#if FELUCCA_SHELL
    added = k == s->n;
#endif
    if (k == s->n) {
#if FELUCCA_SHELL
        if (s->n == STEP_NOTES) {                   /* full: chord.c's rule gives a note up, maybe this one */
            uint8_t all[STEP_NOTES + 1u];
            uint32_t p = trk_index(t), j, ch = shell_chord_part == p + 1u && chord_last[p].n > 1u;
            int32_t root = ch ? (int32_t)chord_last[p].root : 127;
            for (j = 0; j < STEP_NOTES; j++) {
                all[j] = s->note[j];
                if (!ch && (int32_t)s->note[j] < root)
                    root = s->note[j];              /* (CHORD not on the part: the bass as the root) */
            }
            all[STEP_NOTES] = (uint8_t)note;
            k = chord_drop(all, STEP_NOTES + 1u, root);
        } else
#endif
        {
            if (s->n < STEP_NOTES)
                s->n++;
            k = s->n - 1u;
        }
        if (k < STEP_NOTES)
            s->note[k] = (uint8_t)note;
    }
    if (k < STEP_NOTES && !rec_lo_part(t))
        step_set_just(s, k, voice_just);            /* (CHORD's TUNE JUST on this note, as it sounds; 0 else) */
#if FELUCCA_SHELL
    if (k < STEP_NOTES && rec_machine && added) {   /* (one the step held already stays its own) */
        rec_mnote[p][idx] |= (uint8_t)(1u << k);
        rec_many |= (uint8_t)(1u << p);
    } else if (k < STEP_NOTES && !rec_machine)
        rec_mnote[p][idx] &= (uint8_t)~(1u << k);  /* (a note played by hand is the step's own) */
#endif
    s->time = ST_NOTE;
    if (vel >= 127u)                                /* (only a full strike is an ACCENT: 111..126 play as played) */
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    t->seq_active = 1;
    if (sounds) {                                   /* it sounds now: the step must not trigger it again */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < STEP_NOTES)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    rec_tag[p][idx] = wp;
    if (t->rh_n && t->rh_start != idx && (trk_vmode(t) == V_LEGATO || trk_vmode(t) == V_UNISON))
        t->step[(idx + len - 1u) % len].flags |= SF_SLIDE;   /* (played legato: the step before slides into it) */
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
#if FELUCCA_SHELL
        rh_rep[p] = (uint8_t)rep;
#endif
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != heard; k++)
        ;
    if (k == t->rh_n && t->rh_n < STEP_NOTES)
        t->rh_note[t->rh_n++] = (uint8_t)heard;     /* a chord: held until its last key is up (the note as it was */
                                                    /* heard: its key-up is, whatever TRN does meanwhile) */
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start) {                       /* (recorded ahead into the step now starting; round the */
        if (t->rh_ties)                             /* loop to its own start: it ends, never laps) */
            t->rh_n = 0;
        return;
    }
    s = &t->step[idx];
    if (rh_rep_of(trk_index(t)) ? rec_tag[trk_index(t)][idx] == rec_pass[trk_index(t)] :
                                  s->time == ST_NOTE && (s->n || s->hit)) {
        t->rh_n = 0;                                /* ADD: at a step that plays the hold ends there, so an overdub */
        return;                                     /* never erases (a latched HOLD chord tied around the loop did); */
    }                                               /* REPLACE: it ties over an earlier pass's, to this pass's next */
    rec_tag[trk_index(t)][idx] = rec_pass[trk_index(t)];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < STEP_NOTES; k++)
        s->note[k] = 0;
    step_just_clear(s);
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = s->acc = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

/* a note into armed track t in the count-in's last eighth: kept for step 1 (cin_flush); once each, 4 per track */
static __attribute__((noinline)) void cin_keep(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t), k;
    if (cin_left != 1u || cin_pos < click_beat_len() / 2u || !((song.rec >> i) & 1u) || t->p[P_AMODE])
        return;
    for (k = 0; k < cin_n[i] && cin_note[i][k] != note; k++)
        ;
    if (k == cin_n[i]) {
        if (k == STEP_NOTES)
            return;
        cin_n[i]++;
    }
    cin_note[i][k] = (uint8_t)note;
    cin_vel[i][k] = (uint8_t)vel;
#if FELUCCA_SHELL
    cin_just[i][k] = voice_just;
#endif
    cin_up[i] &= (uint8_t)~(1u << k);
}

#if FELUCCA_SHELL
/* HOLD with no arp mode (SQRL-1): the keys latch. A note let go keeps sounding; a note pressed with no key down starts
 * a new latch (the last one ends first). The arp's own lists keep it (held[], arp_phys: the keys down). It ends with
 * HOLD off, an arp mode on (the arp takes the notes) and a switch of pairs (shell.c); a latched note under REC is a
 * held one until then */
static int key_latch(const track_t *t) { return t->p[P_AHOLD] && !t->p[P_AMODE]; }
static void latch_end(track_t *t)                   /* the latched notes off (ISR, or IRQs off) */
{
    uint32_t i;
    for (i = 0; i < t->nheld; i++) {
        rec_release(t, t->held[i]);
        trk_note_off(t, t->held[i]);
    }
    t->nheld = 0;
}
#endif
#if FELUCCA_SHELL
static uint8_t in_norec;                            /* input_on: sound it only (a strum's, recorded at the press: key_on) */
#else
#define in_norec 0u
#endif
/* the note to record for one heard at note (rec_note; a hold keeps the heard one): under QNT SEQ+T the pattern follows TRN as it
 * plays (seq_step), so TRN comes out of it, and what plays back is what was heard */
static uint32_t rec_pitch(const track_t *t, uint32_t note)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    if (t->p[P_QUANT] != QN_SEQT || (e->keys && e->keys(t, 0) >= 0))
        return note;
    return (uint32_t)clamp((int32_t)note - t->p[P_TRANS], 0, 127);
}
static void input_rec(track_t *t, uint32_t note, uint32_t vel)   /* a played note into the take (the count-in's too) */
{
    if (cin_left)
        cin_keep(t, note, vel);
    if (rec_on(t) && !t->p[P_AMODE])               /* (ARP on: arp_tick records its notes) */
        rec_note(t, note, vel);
}
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    if (!in_norec)
        input_rec(t, note, vel);
#if FELUCCA_SHELL
    if (key_latch(t)) {
        if (!t->arp_phys)
            latch_end(t);                           /* (a new latch: the last one ends) */
        arp_add(t, note);                           /* (its lists: the note and the key down) */
        trk_note_on(t, note, vel);
        return;
    }
#endif
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
#if FELUCCA_SHELL
static uint8_t rs_in;                               /* (CHORD's RPT striking: rstrum_block, below) */
#endif
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    if (cin_left) {                                 /* a kept note let go before step 1: recorded, then let go */
        uint32_t i = trk_index(t), k;
        for (k = 0; k < cin_n[i]; k++)
            if (cin_note[i][k] == note)
                cin_up[i] |= (uint8_t)(1u << k);
    }
#if FELUCCA_SHELL
    if (key_latch(t)) {                             /* latched: it sounds on (only the key is up) */
        arp_remove(t, note);
        return;
    }
#endif
#if FELUCCA_SHELL
    if (!rs_in)                                     /* (an RPT strike's note-off: the recorded hold goes on) */
#endif
        rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the white keys F3 .. G5 the map's (perf_map: the settings,
 * ui_layer.c; by default 1.1.5's ten, FLANGER, PHASER, REV / ECHO THROW), black keys 1..4 the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

/* a key of the FX layer down (FX held) / up: the effect it holds. kb_fx: the one it took (+ 1, 0 none), so a key
 * remapped while held lets go of what it holds; an effect two keys hold ends with the last of them. noinline: out
 * of keyboard_block's loop (the audio ISR) */
static uint8_t kb_fx[27];
static __attribute__((noinline)) void perf_key_press(uint32_t k, int down)
{
    uint32_t e, j;
    if (down) {
        e = perf_key(k);
        kb_fx[k] = (uint8_t)(e + 1u);
        perf_press(e, 1);
        return;
    }
    if (!kb_fx[k])
        return;
    e = kb_fx[k] - 1u;
    kb_fx[k] = 0;
    for (j = 0; j < 27u; j++)
        if (kb_fx[j] == e + 1u)
            return;
    perf_press(e, 0);
}

/* the map changed (ui_layer.c: PRESETS with a key held, EDIT, a settings load): a white key held takes its new
 * effect at once (the old one let go); FX LATCH: a latched effect of the key's becomes the new one */
static __attribute__((noinline)) void perf_key_remap(void)
{
    uint32_t k, e, o;
    perf_remap = 0;
    for (k = 0; k < 27u; k++) {
        if (!kb_fx[k] || key_black(k) || (o = kb_fx[k] - 1u) == (e = perf_key(k)))
            continue;
        if (!perf_latch_on) {
            perf_key_press(k, 0);
            perf_key_press(k, 1);
            continue;
        }
        kb_fx[k] = (uint8_t)(e + 1u);
        if (o < PF_N && ((perf_latched >> o) & 1u)) {
            perf_latched &= ~PF_BIT(o);
            if (e < PF_N) {
                perf_latched |= PF_BIT(e);
                perf_ord[e] = ++perf_seq;
            }
        }
    }
}

/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
#if FELUCCA_SHELL
/* CHORD's STRM: the chord's later notes wait here for their time (fm1_ms), started by keyboard_block. The key let go
 * (key_off) or a new chord on the part drops what is still waiting, so nothing starts after it should have ended */
static const uint8_t RPT_RATE[7] = {0, 0, 1, 4, 2, 12, 10};   /* OFF 1/4 1/8 1/8T 1/16 1/4. 1/8.: rate_samples' */
static uint8_t rs_flip;                             /* this strum the other way (rs_in: rstrum_block is striking) */
static uint8_t rs_vel;                              /* .. at this velocity (GROOVE's HUMAN) */
#define STRUM_N 16u
#define STRUM_SEQ 28u                                   /* strum_q's key for a step's note (seq_cp) */
static struct { uint8_t on, part, key, note, vel; int8_t just; uint32_t due; } strum_q[STRUM_N];
static void strum_drop(uint32_t part, uint32_t key)     /* key 27: every key of the part; STRUM_SEQ: its steps' */
{
    uint32_t i;
    for (i = 0; i < STRUM_N; i++)
        if (strum_q[i].on && strum_q[i].part == part &&
            (key == 27u ? strum_q[i].key < 27u : strum_q[i].key == key))
            strum_q[i].on = 0;
}
static void strum_start(track_t *t, uint32_t x, uint32_t vel, int32_t just, uint32_t norec)
{
    voice_just = (int8_t)just;
    in_norec = (uint8_t)norec;
    input_on(t, x, vel);
    in_norec = 0;
    voice_just = 0;
    midi_out_event(0x09u | (0x90u | trk_midi_ch(trk_index(t))) << 8 | x << 16 | vel << 24);
}
static void strum_tick(void)
{
    uint32_t i;
    for (i = 0; i < STRUM_N; i++)
        if (strum_q[i].on && (int32_t)(fm1_ms - strum_q[i].due) >= 0) {
            strum_q[i].on = 0;
            if (strum_q[i].key == STRUM_SEQ) {          /* a step's note: as the sequencer plays it */
                voice_just = strum_q[i].just;
                trk_note_on(&trk[strum_q[i].part % NTRK], strum_q[i].note, strum_q[i].vel);
                voice_just = 0;
            } else
                strum_start(&trk[strum_q[i].part % NTRK], strum_q[i].note, strum_q[i].vel, strum_q[i].just, 1u);
        }
}

/* a step on a part CHORD plays (shell_chord_play) plays through CHORD PLAY as a pressed chord does: its notes struck
 * low to high (STRM's spread and direction), TOP over its velocity, and RPT striking it again while it sounds. The
 * take holds the notes as played, so a later turn of STRM, TOP or RPT changes how it plays. TUNE's JUST is each
 * note's own (step_just), as it was played */
static struct { uint8_t n, note[STEP_NOTES], vel[STEP_NOTES]; int8_t just[STEP_NOTES]; uint32_t age; } cp_seq[NTRK];
/* RPT may strike t's chord step again now (r: its period): not within r / 4 of the step's own strike or of the next
 * step's notes, which strike it themselves (a loop's first step on RPT's grid would strike twice) */
static int seq_cp_due(const track_t *t, uint32_t r)
{
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u, at = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]),
             t->seq_idx);
    const step_t *nx = &seq_steps(t)[(t->seq_idx + 1u) % len];
    if (cp_seq[trk_index(t)].age < r / 4u)
        return 0;
    return !(at - (t->seq_pos < at ? t->seq_pos : at) < r / 4u && nx->time == ST_NOTE && nx->n);
}
static void seq_cp_strike(track_t *t, uint32_t flip)    /* cp_seq's chord, struck (again: its notes ended first) */
{
    uint32_t p = trk_index(t), n = cp_seq[p].n, i, q;
    int32_t strm = flip ? -shell_chord_strm : shell_chord_strm;
    strum_drop(p, STRUM_SEQ);
    for (i = 0; i < n; i++)
        trk_note_off(t, cp_seq[p].note[i]);
    for (i = 0; i < n; i++) {
        uint32_t x = cp_seq[p].note[i], v = cp_top_vel(i, n, cp_seq[p].vel[i], shell_chord_top);
        uint32_t d = cp_strum_ms(i, n, strm);
        for (q = 0; d && q < STRUM_N && strum_q[q].on; q++)
            ;
        if (d && q < STRUM_N) {                     /* (full: at once) */
            strum_q[q].on = 1;
            strum_q[q].part = (uint8_t)p;
            strum_q[q].key = STRUM_SEQ;
            strum_q[q].note = (uint8_t)x;
            strum_q[q].vel = (uint8_t)v;
            strum_q[q].just = cp_seq[p].just[i];
            strum_q[q].due = fm1_ms + d;
        } else {
            voice_just = cp_seq[p].just[i];
            trk_note_on(t, x, v);
            voice_just = 0;
        }
    }
}
#endif

#if FELUCCA_SHELL
static uint8_t pg_in;                               /* (PROG striking its next chord: prog_block, below) */
#endif
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n, i, mc = trk_midi_ch(trk_index(t)), vel = kb_vel;
#if FELUCCA_SHELL
    if (shell_chord_prog && shell_chord_part == trk_index(t) + 1u && !rs_in && !pg_in && !song.playing) {
        if (chord_cpg.started)                      /* PROG, stopped: each root pressed plays the next chord */
            cpg_next(&chord_cpg, shell_chord_pdir, cpg_len(shell_chord_prog, shell_chord_plen));
        else
            cpg_start(&chord_cpg);
    }
#endif
    n = chord_build(t, kb_note[k], kb_chord[k]);
#if FELUCCA_SHELL
    uint32_t part = trk_index(t), cp = shell_chord_part == part + 1u && n > 1u;
    if (rs_in)
        vel = rs_vel;                               /* (an RPT strike: its velocity, GROOVE's HUMAN) */
    int32_t root = chord_last[part].root;
    uint16_t mask = chord_last[part].mask;
    if (cp)
        strum_drop(part, 27u);                      /* (a new chord: the last one's waiting notes go) */
    if (cp && !rs_in && !song.playing) {            /* (RPT, stopped: its count starts with the chord) */
        rs_t = 0;
        rs_flip = 0;
    }
#endif
    kb_chn[k] = 0;
    in_key = (uint8_t)(k + 1u);                     /* (the arp keeps the key: arp_add) */
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x)) {
            input_rec(t, x, vel);                   /* (sounding already: recorded, not struck again) */
            continue;
        }
#if FELUCCA_SHELL
        if (cp) {
            uint32_t v = cp_top_vel(i, n, (uint8_t)vel, shell_chord_top), q;
            uint32_t d = cp_strum_ms(i, n, rs_in && rs_flip ? -shell_chord_strm : shell_chord_strm);
            int32_t j = shell_chord_tune ? cp_just16((int32_t)x - root, mask) : 0;
            for (q = 0; d && q < STRUM_N && strum_q[q].on; q++)
                ;
            voice_just = (int8_t)j;                 /* the chord into the step it was pressed in, whole, at the */
            if (!rs_in)                             /* played velocity (TOP and STRM act as the step plays); an */
                input_rec(t, x, vel);               /* RPT strike is never recorded (the step repeats itself) */
            voice_just = 0;
            if (d && q < STRUM_N) {                 /* (full: at once) */
                strum_q[q].on = 1;
                strum_q[q].part = (uint8_t)part;
                strum_q[q].key = (uint8_t)k;
                strum_q[q].note = (uint8_t)x;
                strum_q[q].vel = (uint8_t)v;
                strum_q[q].just = (int8_t)j;
                strum_q[q].due = fm1_ms + d;
            } else
                strum_start(t, x, v, j, 1u);
            continue;
        }
#endif
        input_on(t, x, vel);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | vel << 24);
    }
    in_key = 0;
    kb_chn[k] = (uint8_t)n;
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
#if FELUCCA_SHELL
    strum_drop(trk_index(t), k);                    /* (its notes still waiting: never started) */
#endif
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#if FELUCCA_SHELL
/* CHORD PLAY's RPT: a chord held or latched on CHORD's part strums again every RPT, on the grid counted from the
 * transport's start while it plays, else from the chord's press at the song's tempo. Each strum goes the other way
 * (STRM's spread; at 0 the chord is struck again), TOP and TUNE as ever. REC keeps the chord once: a step on CHORD's
 * part repeats itself as it plays (seq_cp) */
static void rstrum_block(uint32_t n)
{
    uint32_t r, k, was;
    track_t *t;
    for (k = 0; k < NTRK; k++)                          /* (each chord step's time since its strike) */
        cp_seq[k].age = cp_seq[k].age < 0x40000000u ? cp_seq[k].age + n : cp_seq[k].age;
    if (!shell_chord_rpt || shell_chord_rpt >= NELEM(RPT_RATE) || (!shell_chord_part && !shell_chord_play)) {
        rs_t += n;
        return;
    }
    r = rate_samples(RPT_RATE[shell_chord_rpt]);
    t = &trk[shell_chord_part ? (shell_chord_part - 1u) % NTRK : (uint32_t)__builtin_ctz(shell_chord_play) % NTRK];
    was = rs_t;
    rs_t += n;
    if (!r || !grid_due(was, rs_t, rate_q16(RPT_RATE[shell_chord_rpt]), track_swing(t), trk_index(t), &k))   /* (GROOVE:
                                                                                     * the part's SWG its depth) */
        return;
    rs_vel = (uint8_t)groove_vel(kb_vel, track_swing(t), r, k, trk_index(t));
    rs_flip ^= 1u;
    rs_in = 1;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] + 1u == shell_chord_part && !t->p[P_AMODE]) {   /* (an arp mode: the arp is the
                                                                                         * part's one clock) */
            key_off(k, t);
            key_on(k, t);
        }
    rs_in = 0;
    if (song.playing)                                   /* a chord step still sounding strikes again too */
        for (k = 0; k < NTRK; k++)
            if ((shell_chord_play >> k) & 1u && trk[k].seq_n && cp_seq[k].n && !trk[k].p[P_AMODE] &&
                seq_cp_due(&trk[k], r))
                seq_cp_strike(&trk[k], rs_flip);
}
#endif

#if FELUCCA_SHELL
/* PROG: playing, a chord held or latched on CHORD's part moves to the progression's next every EVERY on the click's
 * grid (clk_abs: 1/16s from the transport's start), struck as a chord pressed (recorded under REC, the arp following
 * it); the transport's start begins at chord 0 */
static uint32_t clk_abs;                            /* (the click's, below) */
static uint32_t pg_unit = 0xFFFFFFFFu;
static void prog_block(void)
{
    static const uint8_t SHIFT[4] = {3, 4, 5, 6};       /* HALF, 1, 2, 4 bars of 16 1/16s */
    uint32_t u, k, was = pg_unit;
    track_t *t;
    if (!shell_chord_prog || !shell_chord_part || !song.playing || clk_pos == CLK_START) {
        pg_unit = 0xFFFFFFFFu;
        if (!shell_chord_prog || song.playing)
            chord_cpg.started = 0;
        return;
    }
    u = clk_abs >> SHIFT[shell_chord_pevery & 3u];
    if (u == was)
        return;
    pg_unit = u;
    k = chord_cpg.i;
    if (was == 0xFFFFFFFFu)
        cpg_start(&chord_cpg);
    else
        cpg_next(&chord_cpg, shell_chord_pdir, cpg_len(shell_chord_prog, shell_chord_plen));
    if (chord_cpg.i == k && was != 0xFFFFFFFFu)
        return;                                         /* (the same chord: it sounds on) */
    t = &trk[(shell_chord_part - 1u) % NTRK];
    pg_in = 1;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] + 1u == shell_chord_part) {
            key_off(k, t);
            key_on(k, t);
        }
    pg_in = 0;
}
#endif

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

#if FELUCCA_SHELL
/* SQRL-1 CHORD's LATCH (chord.c): the roots let go while it is on keep sounding until the next root, or LATCH off */
static uint32_t kb_latched;
static void chord_unlatch(void)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if ((kb_latched >> k) & 1u && kb_note[k] != KB_SILENT)
            key_off(k, &trk[kb_trk[k] % NTRK]);
    kb_latched = 0;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    uint32_t lay, fx, mute;
    ch = cur ^ kb_prev;                          /* keys also sound while entering steps */
#if FELUCCA_SHELL
    strum_tick();
#endif
    if (perf_remap)
        perf_key_remap();
#if FELUCCA_SHELL
    if (kb_latched && !shell_chord_latch)
        chord_unlatch();
#endif
    if (!ch)
        return;
    if (kb_boot_hold) {                           /* #137: under the splash; their release is silent too */
        for (k = 0; k < 27u; k++)
            if ((ch & cur) >> k & 1u)
                kb_note[k] = KB_SILENT;
        kb_prev = cur;
        return;
    }
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
    mute = song.grid == 2u ? ~0u : song.grid == 3u ? SONG_KEYS : 0u;   /* the keys that never sound here */
#if FELUCCA_SHELL
    mute |= shell_kb_skip | shell_kb_scl;
#endif
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
#if FELUCCA_SHELL
            if (kb_latched && k >= CB_ROOT0 && song.sel + 1u == shell_chord_part)
                chord_unlatch();                  /* (a new root: the latched chord ends first) */
#endif
            kb_trk[k] = song.sel;
            if (kb_asleep) {                      /* (SCREEN OFF: the wake press is swallowed) */
                kb_note[k] = KB_SILENT;
                continue;
            }
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_key_press(k, 1);
                continue;
            }
            if ((mute >> k) & 1u) {               /* NAME (ui_name.c): every key types, none sounds; SONG: A..D */
                kb_note[k] = KB_SILENT;
#if FELUCCA_SHELL
                if ((shell_ne_idx || shell_al_front) && song.grid < 2u) {   /* (the shell's NOTE engine: the key is its) */
                    kb_ne_down |= 1u << k;
                    shell_ne_key(k, 1);
                }
#endif
            }
            else if (song.grid == 1u)             /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = grid_key_note(k);
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            if (qn_near(&trk[kb_trk[k]]))              /* NEAR's tie follows the key pressed last (only a press: */
                qn_klast[kb_trk[k] % NTRK] = (uint8_t)(kb_note[k] ? kb_note[k] : 1u);   /* the arp's re-maps don't) */
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_key_press(k, 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT) {
#if FELUCCA_SHELL
                if ((kb_ne_down >> k) & 1u) {
                    kb_ne_down &= ~(1u << k);
                    shell_ne_key(k, 0);
                }
#endif
                continue;
            }
#if FELUCCA_SHELL
            if (shell_chord_latch && k >= CB_ROOT0 && kb_trk[k] + 1u == shell_chord_part) {
                kb_latched |= 1u << k;            /* LATCH: it keeps sounding */
                continue;
            }
#endif
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

/* -------------------------------------------------------- sequencer --- */

static void seq_start(void)
{
#if FELUCCA_SHELL
    rs_t = 0;                                       /* (RPT's grid from the transport's start) */
    rs_flip = 0;
    memset(acid_mut, 0, sizeof acid_mut);           /* (ACID's FLUX: from the line as written) */
    memset(acid_fl, 0, sizeof acid_fl);
#endif
    uint32_t i;
#if FELUCCA_SHELL
    for (i = 0; i < NLANES * NLANE; i++) {
        shell_lane_pos_i[i / NLANE][i % NLANE] = 0;
        lane_ff[i / NLANE][i % NLANE] = 0;
        lane_dnext[i / NLANE][i % NLANE] = 0;
        lane_dwas[i / NLANE][i % NLANE] = 0;
    }
    map_cyc = 0;
#endif
    memset(rec_pass, 0, sizeof rec_pass);
    memset(rec_tag, 0, sizeof rec_tag);
    motion_begin();
    chain_start();
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->rat_left = 0;
        t->nd_late = t->nd_early = ND_NONE;
        t->nd_fired = 0;
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
#if FELUCCA_SHELL
    shell_from_top = 1;                            /* (MOVE: a run from the top goes home) */
#endif
    clk_pos = CLK_START;                           /* the metronome's first beat with step 0 */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

static void seq_release(track_t *t)
{
    uint32_t i;
#if FELUCCA_SHELL
    strum_drop(trk_index(t), STRUM_SEQ);                /* (a strum's later notes not started yet: never) */
    cp_seq[trk_index(t)].n = 0;
#endif
    for (i = 0; i < t->seq_n; i++)
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
#if FELUCCA_SHELL
    rec_cin = 0;
#endif
    cin_left = 0;                                  /* (a count-in: cancelled, nothing recorded) */
    cin_flush = 0;
    memset(cin_n, 0, sizeof cin_n);
    if (song.g[G_CLOCK])                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
#if FELUCCA_SHELL
    for (i = 0; i < NLANES * NLANE; i++)           /* a DIV lane's note: its own to end */
        if (lane_dnote[i / NLANE][i % NLANE] && shell_lanes_part[i / NLANE]) {
            trk_note_off(&trk[(shell_lanes_part[i / NLANE] - 1u) % NTRK], lane_dnote[i / NLANE][i % NLANE] - 1u);
            lane_dnote[i / NLANE][i % NLANE] = 0;
        }
#endif
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        trk[i].rat_left = 0;
        trk[i].nd_late = trk[i].nd_early = ND_NONE;   /* (a nudged step not played yet: not after the stop) */
        trk[i].nd_fired = 0;
    }
    chain_stop();
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here); SEQ_REP: a RATCH repeat (seq_ratchet).
 * RATCH x2..x4 (core.h step_ratchet): a NOTE step plays all of it again in equal parts of its swung length, each
 * part retriggered with GATE of the part; the chance is rolled once for the whole step (a failed roll: no part),
 * a slide into it glides into the first part only, and it never slides or ties out (its last part ends at its
 * gate). x1, what every older pattern holds, is the step exactly as before. */
/* 1.5.5 (#209): a step's CONDITION (core.h step_cond). a:b plays on loop a of every b of its track: cond_loop counts the
 * passes through the track's LEN, 0 the first (seq_tick: each wrap to step 1 adds one, modulo COND_LOOPS, a multiple
 * of 2, 3 and 4). It starts at 0 on PLAY (and RESTART) and on every SONG section (each section's pattern from its
 * first loop: 4:4 plays on a section's 4th repeat); a pattern loaded or LEN changed while playing keeps counting. Each
 * track counts its own LEN. A step played early by its NUDGE (step 1 at the end of the loop before) is the next
 * loop's. FILL / !FILL: seq_fill, set by the UI while GLO + E4 is held (ui_layer.c). Read where the chance is rolled
 * (seq_play): a step whose condition fails plays as a failed roll (nothing, none of its locks). Every step with no
 * condition (every older pattern) plays as before, its roll untouched */
#define COND_LOOPS 12u
static volatile uint8_t seq_fill;
static const uint8_t COND_A[9] = {1, 2, 1, 2, 3, 1, 2, 3, 4}, COND_B[9] = {2, 2, 3, 3, 3, 4, 4, 4, 4};
static __attribute__((noinline)) int seq_cond_miss(const track_t *t, const step_t *s, uint32_t idx)
{
    uint32_t c = step_cond(s), loop;
    if (c >= COND_FILL)
        return (c == COND_FILL) != (seq_fill != 0u);
    if (!c)
        return 0;
    loop = t->cond_loop + (idx < t->seq_idx);       /* (an early play of step 1: the next loop's) */
    return loop % COND_B[c - 1u] != COND_A[c - 1u] - 1u;
}
/* the step does not play: its condition fails, or its chance's roll */
static int seq_miss(const track_t *t, const step_t *s, uint32_t idx)
{
    if (s->probability >= COND_FIRST)
        return seq_cond_miss(t, s, idx);
    return step_chance(s) < 100u && rng() % 100u >= step_chance(s);
}
#if FELUCCA_SHELL
/* a lane's own step (RUDIMENT 0, LINES 1): its CHNC in bits 0..5 (0: 100 %, 1..20: 95 .. 0 % by 5, 21..31: the
 * conditions 1:2 .. !FILL) and RATCH - 1 in bits 6..7. Each lane plays its own byte at the step it plays (lane_at, its
 * DIV clock), so a hat's x3 is the hat's only; the column's CHNC and RATCH no longer reach a lanes engine (shell.c
 * sh_ls_*, kept in the project's companion) */
static uint8_t shell_lstep[2][NLANE][NSTEP] __attribute__((section(".pool")));   /* (.pool: zeroed at power-on) */
static uint8_t shell_lstep_new[2][NLANE][NSTEP];        /* .. as they were before NEW, for NEW's undo (project.c) */
static uint8_t shell_lstep_back;                        /* the project loading is NEW's undo: those come back */
static struct { uint8_t m[3], a[3], done[3], idx, oct[3]; } lane_rat[2];   /* this step's RATCH x2..x4 lanes: hits,
                                                         * accents, the repeats played (seq_lane_ratchet); the octaves */
static uint32_t lane_rng(uint32_t lk, uint32_t i)       /* lane i's own roll */
{
    uint32_t *x = &lane_rs[lk % NLANES][i % NLANE];
    if (!*x)
        *x = 0x9E3779B9u * (lk * NLANE + i + 1u);
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}
static int lane_naps(const track_t *t, uint32_t lk, uint32_t i)   /* 1: NAP has the lane resting this bar */
{
    uint32_t nap = shell_lane_nap_i[lk][i], wake = shell_lane_wake_i[lk][i], loop = shell_lane_loop_i[lk][i];
    if (!nap)
        return 0;
    wake = wake ? wake : 1u;
    return seq_abs[trk_index(t)] / 16u / (loop ? loop : 1u) % (wake + nap) >= wake;
}
static void lane_bar(const track_t *t, uint32_t lk)     /* a bar's first step: each LOOP lane back to its seed at a loop's */
{
    uint32_t a = seq_abs[trk_index(t)], i;
    if (a % 16u || (a && lane_bar_at[lk] == a + 1u))   /* (step 1 always: PLAY again starts the count again) */
        return;
    lane_bar_at[lk] = a + 1u;
    for (i = 0; i < NLANE; i++) {
        uint32_t loop = shell_lane_loop_i[lk][i];
        if (loop && a / 16u % loop == 0u)
            lane_rs[lk][i] = shell_lane_seed_i[lk][i] ? shell_lane_seed_i[lk][i] : 1u;
        lane_rs_bar[lk][i] = lane_rs[lk][i];
    }
}
static int lane_miss(const track_t *t, uint32_t b, uint32_t idx, uint32_t lk, uint32_t i)   /* 1: the lane's byte b says
                                                                   * its step rests (idx: the part's step, which the
                                                                   * conditions count by); lane i's own roll */
{
    uint32_t c = b & 63u;
    step_t x;
    if (!c)
        return 0;
    if (c <= 20u)
        return lane_rng(lk, i) % 100u >= 100u - 5u * c;
    x.probability = (uint8_t)(COND_FIRST + (c - 21u < NCOND ? c - 21u : NCOND - 1u));
    return seq_cond_miss(t, &x, idx);
}
#endif
/* a step's ACCENT and SLIDE as they play: ACID's with FLUX's flips */
static uint32_t seq_sflags(const track_t *t, const step_t *s, uint32_t idx)
{
#if FELUCCA_SHELL
    uint32_t q = idx % NSTEP;
    if (trk_index(t) + 1u == shell_acid_part)
        return s->flags ^ ((acid_fl[0][q >> 5] >> (q & 31u) & 1u) ? SF_ACCENT : 0u) ^
               ((acid_fl[1][q >> 5] >> (q & 31u) & 1u) ? SF_SLIDE : 0u);
#else
    (void)t, (void)idx;
#endif
    return s->flags;
}
#define SEQ_REP (1u << 16)
#define SEQ_ROLLED (1u << 17)   /* seq_tick rolled the chance already (before the step's parameter locks): it passed */
#define SEQ_MISS (1u << 18)     /* .. it failed: nothing plays */
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t idx, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u, sf = seq_sflags(t, s, idx);
    uint32_t vel = (sf & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
#if FELUCCA_SHELL
    uint32_t vel_h = groove_vel(vel, track_swing(t), period, idx, trk_index(t));   /* (GROOVE's HUMAN) */
#endif
    uint32_t slide_in = t->seq_hold && t->seq_n && !(skip & SEQ_REP);
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = seq_steps(t)[(idx + 1u) % len].time == ST_TIE;
    uint32_t hits = step_ratchet(s);
    uint8_t nn[STEP_NOTES + NLANE], vv[STEP_NOTES + NLANE];   /* the notes it plays: its notes, then its hits */
#if FELUCCA_SHELL
    int8_t jj[STEP_NOTES + NLANE];                  /* .. each one's JUST (voice_just as it starts) */
#endif
    uint32_t m = 0, sk = 0, mn, cps = 0;               /* cps: the notes CHORD PLAY struck (seq_cp_strike) */
    /* QNT SEQ: the step's notes (not the lane hits) snap to the scale as they play; never on a drum kit, the slices
     * or another engine that maps the keys itself. SEQ+T (#218): moved by TRN first, as the keys are. seq_notes keeps
     * the notes that sound: their note-offs match */
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t qseq = t->p[P_QUANT] >= QN_SEQ && !(e->keys && e->keys(t, 0) >= 0);
    int32_t qtrn = qseq && t->p[P_QUANT] == QN_SEQT ? t->p[P_TRANS] : 0;
    if (!(skip & SEQ_REP)) {
        t->rat_left = 0;
        if ((skip & SEQ_MISS) || (!(skip & SEQ_ROLLED) && seq_miss(t, s, idx))) {
            seq_release(t);
            return;
        }
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (sf & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (s->time == ST_REST || (!s->n && !s->hit)) {
        seq_release(t);
        return;
    }
    if (hits > 1u) {                                /* RATCH: the gate of a part, no slide or tie out */
        gate = step_samples(t, period, idx) / hits * (uint32_t)t->p[P_SGATE] / 128u;
        if (!(skip & SEQ_REP))
            t->rat_left = (uint8_t)(hits - 1u);
    }
#if FELUCCA_SHELL
    if (trk_index(t) + 1u == shell_acid_part && shell_acid_flux && !(skip & SEQ_REP)) {   /* ACID's FLUX, once a step */
        uint32_t r, q = idx % NSTEP;
        acid_frng ^= acid_frng << 13;
        acid_frng ^= acid_frng >> 17;
        acid_frng ^= acid_frng << 5;
        r = acid_frng;
        if (r % 100u < shell_acid_flux) {
            r >>= 8;
            if (((r >> 5) & 7u) < 2u)                   /* (an ACCENT or a SLIDE flips) */
                acid_fl[(r >> 5) & 1u][q >> 5] ^= 1u << (q & 31u);
            else if (acid_mv(q) && (r & 1u))
                acid_mv_set(q, 0);                  /* (back to as written) */
            else if (((r >> 1) & 3u) == 0u)
                acid_mv_set(q, (r >> 3) & 1u ? 5 : -5);
            else
                acid_mv_set(q, ((r >> 3) & 1u ? 1 : -1) * (1 + (int32_t)((r >> 4) & 1u)));
        }
    }
#endif
    for (i = 0; i < s->n && i < STEP_NOTES; i++) {
        uint32_t x = s->note[i];
#if FELUCCA_SHELL
        if (trk_index(t) + 1u == shell_acid_part) {     /* ACID: TRN along the scale, OCT and the keys' transposition */
            int32_t mv = acid_mv(idx % NSTEP), mo = (mv + 12) / 5 - 2;
            int32_t y = (int32_t)x, d = shell_acid_deg + mv - 5 * mo, g = 0;
            while (d && g++ < 96) {
                y += d > 0 ? 1 : -1;
                if (scale_has(t, y))
                    d += d > 0 ? -1 : 1;
            }
            x = (uint32_t)clamp(y + shell_acid_semi + 12 * mo, 0, 127);
        }
#endif
        if (qseq && t->p[P_QUANT] == QN_DROP && !scale_has(t, (int32_t)x))
            continue;                               /* QNT DROP: a note off the scale now is a rest */
        if (qseq) {                                 /* QNT SEQ, NEAR: onto the scale now; two notes landing together
                                                     * play once */
            x = (uint32_t)clamp(qn_near(t) ? scale_near(t, (int32_t)x, qn_last[trk_index(t) % NTRK]) :
                                t->p[P_QUANT] == QN_DROP ? (int32_t)x : scale_snap(t, (int32_t)x + qtrn), 0, 127);
            if (qn_near(t))                         /* (the next tie leans toward it) */
                qn_last[trk_index(t) % NTRK] = (uint8_t)(x ? x : 1u);
            for (j = 0; j < m && nn[j] != x; j++)
                ;
            if (j < m)
                continue;
        }
        nn[m] = (uint8_t)x;
#if FELUCCA_SHELL
        vv[m] = (uint8_t)vel_h;
#else
        vv[m] = (uint8_t)vel;
#endif
#if FELUCCA_SHELL                                   /* (QNT SEQ moving a note off its pitch: its interval to the
                                                     * chord is another, so no JUST; TRN alone keeps it) */
        jj[m] = (int8_t)(!rec_lo_part(t) && (!qseq || (int32_t)x == (int32_t)s->note[i] + qtrn) ? step_just(s, i) : 0);
                                                    /* (a lanes step's just[]: its hits' octaves) */
#endif
        sk |= ((skip >> i) & 1u) << m;
        m++;
    }
    mn = m;                                         /* (the step's notes; its lane hits after them) */
    for (i = 0; i < NLANE; i++)
        if ((s->hit >> i) & 1u) {
            nn[m] = (uint8_t)lane_note_at(t, s, i);
#if FELUCCA_SHELL
            jj[m] = 0;
#endif
#if FELUCCA_SHELL
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel_h);
#else
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel);
#endif
            sk |= ((skip >> (8u + i)) & 1u) << m;
            m++;
        }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
#if FELUCCA_SHELL
    if ((shell_chord_play >> trk_index(t)) & 1u && mn > 1u && !t->p[P_AMODE]) {   /* a chord on CHORD's part: CHORD
                                                    * PLAY strikes it (an arp mode on: its chords play as it struck them) */
        uint32_t c = 0, a;
        for (i = 0; i < mn; i++)
            if (!((sk >> i) & 1u)) {
                for (a = c; a > 0u && cp_seq[trk_index(t)].note[a - 1u] > nn[i]; a--) {   /* (ascending) */
                    cp_seq[trk_index(t)].note[a] = cp_seq[trk_index(t)].note[a - 1u];
                    cp_seq[trk_index(t)].vel[a] = cp_seq[trk_index(t)].vel[a - 1u];
                    cp_seq[trk_index(t)].just[a] = cp_seq[trk_index(t)].just[a - 1u];
                }
                cp_seq[trk_index(t)].note[a] = nn[i];
                cp_seq[trk_index(t)].vel[a] = vv[i];
                cp_seq[trk_index(t)].just[a] = jj[i];
                c++;
            }
        cp_seq[trk_index(t)].n = (uint8_t)c;
        cp_seq[trk_index(t)].age = 0;
        seq_cp_strike(t, 0u);
        cps = (1u << mn) - 1u;                      /* (struck: the loop below starts only the hits) */
    }
#endif
    for (i = 0; i < m; i++)
        if (!(((sk | cps) >> i) & 1u)) {
#if FELUCCA_SHELL
            voice_just = jj[i];
#endif
            trk_note_on(t, nn[i], vv[i]);
        }
#if FELUCCA_SHELL
    voice_just = 0;
#endif
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < m && nn[j] != t->seq_notes[i]; j++)
                ;
            if (j == m)
                trk_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u))
            t->seq_notes[t->seq_n++] = nn[i];
    t->seq_off = gate;
    t->seq_hold = hits == 1u && ((sf & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
}

/* MENU > CONTROL > STEP PREVIEW (1.2, Discussion #169): the UI asks for a step of a track to sound once (aud_req: step
 * << 8 | track + 1; ui_input.c cursor_move); with the transport stopped it plays here as the sequencer would play it,
 * through the track's sound: its notes or hits, accent, QNT SEQ (not its chance, ratchet repeats or locks), for a 1/16
 * at the track's GATE (seq_tick lets it go, stopped too). A SLIDE or TIE after it does not hold it. Nothing is recorded:
 * the step is only read */
static volatile uint16_t aud_req;
static __attribute__((noinline)) void seq_audition(void)
{
    uint32_t r = aud_req, i = (r & 0xFFu) - 1u, idx = r >> 8;
    track_t *t;
    uint16_t at;
    aud_req = 0;
    if (song.playing || cin_left || i >= NTRK || idx >= NSTEP)
        return;
    t = &trk[i];
    at = t->seq_idx;
    t->seq_idx = (uint16_t)idx;
    seq_step(t, &t->step[idx], idx, div_samples(2), SEQ_ROLLED);
    t->seq_idx = at;
    t->seq_hold = 0;
    t->rat_left = 0;
}

/* RATCH: the next part of the playing step once the sequencer is that far into it (seq_step played the first).
 * The step is read again: an edit to fewer parts (or another source in a song) ends the repeats */
#if FELUCCA_SHELL
/* a lanes engine's RATCH: each x2..x4 group of lanes plays its repeats in equal parts of the playing step, those lanes
 * only, at the column's VEL (not inlined: off the audio ISR's loop) */
static __attribute__((noinline)) void seq_lane_ratchet(track_t *t, uint32_t period)
{
    int32_t lk = lanes_of(t);
    uint32_t r, len;
    step_t x;
    if (lk < 0 || lane_rat[lk].idx != (uint8_t)t->seq_idx)
        return;
    len = step_samples(t, period, t->seq_idx);
    for (r = 0; r < 3u; r++) {
        uint32_t hits = r + 2u;
        if (!lane_rat[lk].m[r] || lane_rat[lk].done[r] + 1u >= hits ||
            t->seq_pos < (lane_rat[lk].done[r] + 1u) * (len / hits))
            continue;
        lane_rat[lk].done[r]++;
        x = seq_steps(t)[t->seq_idx];
        x.n = 0;
        x.hit = lane_rat[lk].m[r];
        x.acc = lane_rat[lk].a[r];
        memcpy(x.just, lane_rat[lk].oct, sizeof x.just);   /* (the hits' octaves, as the step played them) */
        x.time = ST_NOTE;
        x.probability = 0;
        step_set_ratchet(&x, hits);
        seq_step(t, &x, t->seq_idx, period, SEQ_REP);
    }
}
#endif
static __attribute__((noinline)) void seq_ratchet(track_t *t, uint32_t period)
{
    const step_t *s = &seq_steps(t)[t->seq_idx];
    uint32_t hits = step_ratchet(s);
    if (t->rat_left >= hits) {
        t->rat_left = 0;
        return;
    }
    if (t->seq_pos < (hits - t->rat_left) * (step_samples(t, period, t->seq_idx) / hits))
        return;
    t->rat_left--;
    seq_step(t, s, t->seq_idx, period, SEQ_REP);
}

/* the nudge of step s in 1/NUDGE_DIV of the step (NUDGE, core.h) when it plays off its start: the track's QUANTIZE OFF,
 * a NOTE step that plays something, x1 (a ratcheted step's parts divide the step: on its start); else 0 (SEQ_QNT_PLAY) */
static int32_t seq_nudge(const track_t *t, const step_t *s)
{
    if (SEQ_QNT_PLAY(t) || s->time != ST_NOTE || (!s->n && !s->hit) || step_ratchet(s) != 1u)
        return 0;
    return step_nudge(s);
}

/* 1.5.6 (#213): the matrix's TIME on step idx: how late it plays (samples, 0: on time). A NOTE step that plays
 * something, x1 (a ratcheted step's parts divide the step, as NUDGE); its VEL and first note are the sources' VEL
 * and KEY (mod.c mod_time). Not inlined: once a step, off the audio ISR's loop */
static __attribute__((noinline)) uint32_t seq_time(track_t *t, uint32_t idx, uint32_t period)
{
    const step_t *s = &seq_steps(t)[idx];
    uint32_t vel = (seq_sflags(t, s, idx) & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    if (s->time != ST_NOTE || (!s->n && !s->hit) || step_ratchet(s) != 1u)
        return 0;
    return mod_time(t, s->n ? s->note[0] : lane_note(t, __builtin_ctz(s->hit) & (NLANE - 1u)), vel, period);
}

/* the automation a track plays: its slot's while a song plays (song_chain.c lanes; 0: a silent lane, none) */
static const motion_store_t *chain_motion(const track_t *t)
{
    uint32_t s = chain.lane[trk_index(t)];
    return !chain.running ? &motion : s < CHAIN_NSRC ? &chain.source[s].motion : 0;
}

#if FELUCCA_SHELL
#include "grids_map.h"
/* RUDIMENT's MAP (shell.c, ALGORITHM turned to MAP): Grids' drum map plays lanes KICK, SNARE and HATCL (MAP_LANE), its
 * hits ORed with the lanes' own steps, live: X and Y (0..255) pick the place on the map, each lane's DENS how many of
 * its levels play (Grids' threshold: a level over 255 - DENS), ACC the accents (a hit's level over 255 - ACC; 63:
 * Grids' 192). CHAOS: Grids' randomness, a boost to each lane's levels for a whole cycle of the KICK lane, the same
 * four cycles over from the transport's start (map_cyc), so a loop repeats */
static const uint8_t MAP_LANE[3] = {0, 1, 3};
static volatile uint8_t shell_map_on, shell_map_x, shell_map_y, shell_map_chaos, shell_map_acc, shell_map_dens[3];
static inline uint32_t map_mix(uint32_t a, uint32_t b, uint32_t w) { return (a * (255u - w) + b * w) >> 8; }
static uint32_t map_level(uint32_t step, uint32_t inst, uint32_t x, uint32_t y)   /* Grids' ReadDrumMap */
{
    uint32_t i = x >> 6, j = y >> 6, o = inst * 32u + step % 32u, wx = (x << 2) & 255u;
    return map_mix(map_mix(GRIDS_NODE[GRIDS_MAP[i][j]][o], GRIDS_NODE[GRIDS_MAP[i + 1u][j]][o], wx),
                   map_mix(GRIDS_NODE[GRIDS_MAP[i][j + 1u]][o], GRIDS_NODE[GRIDS_MAP[i + 1u][j + 1u]][o], wx),
                   (y << 2) & 255u);
}
static uint32_t map_boost(uint32_t inst, uint32_t cyc)   /* CHAOS's boost to lane inst's levels in cycle cyc */
{
    uint32_t h = (inst + 1u) * 0x9E3779B9u ^ (cyc % 4u + 1u) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return ((h >> 24) * (uint32_t)(shell_map_chaos >> 1)) >> 8;
}
/* map lane inst's hit and accent at its step (2 hit, 1 accent too: as bits) */
static __attribute__((noinline)) uint32_t map_hit(uint32_t inst, uint32_t step, uint32_t cyc)
{
    uint32_t level = map_level(step, inst, shell_map_x, shell_map_y), b = map_boost(inst, cyc);
    level = level + b > 255u ? 255u : level + b;
    if (level <= 255u - shell_map_dens[inst])
        return 0;
    return 2u | (level > 255u - shell_map_acc);
}
/* the logic lanes of lanes engine lk over one column's bits, in lane order (so one can take another); ff: FLIP's and
 * LATCH's state, the ISR's (lane_ff) or a copy the screen walks */
static void lane_logic(uint32_t lk, uint8_t *hit, uint8_t *acc, uint8_t *ff_l)
{
    uint32_t i;
    for (i = 2; i < NLANE; i++) {
        uint32_t op = shell_lane_op_i[lk][i], a = (*hit >> (i - 1u)) & 1u, b = (*hit >> (i - 2u)) & 1u, o;
        uint32_t aa = a & (*acc >> (i - 1u)), ba = b & (*acc >> (i - 2u));
        uint8_t *ff = &ff_l[i];
        if (!op || op >= LOP_N)
            continue;
        switch (op) {
        case LOP_AND:   o = a & b; break;
        case LOP_OR:    o = a | b; break;
        case LOP_XOR:   o = a ^ b; break;
        case LOP_NOR:   o = !(a | b); break;
        case LOP_ANOTB: o = a & !b; break;
        case LOP_FLIP:                                  /* every other A hit; B starts it over */
            if (b) *ff = 0;
            o = a && (*ff ^= 1u);
            break;
        default:                                        /* LATCH: every step from an A hit until a B hit (both on one
                                                         * step: B wins) */
            if (a) *ff = 1;
            if (b) *ff = 0;
            o = *ff;
            break;
        }
        *hit = (uint8_t)((*hit & ~(1u << i)) | (o << i));
        *acc = (uint8_t)((*acc & ~(1u << i)) | ((o & (op == LOP_AND ? aa & ba : aa | ba) & 1u) << i));
    }
}
#endif

/* the step a lane plays at the part's step idx, its pos moved on. The lanes' phase rule, all of it: a lane following
 * LEN (own 0, or its own LEN at the part's or more) is the part's step, so it moves with LEN at once (and back to
 * =LEN, it is in the pattern's phase again); a lane of its own counts its steps from the transport's start, a longer
 * LEN going on from where it is, a shorter one past it wrapping on the next step (no jump) */
#if FELUCCA_SHELL
static inline uint32_t lane_div(int32_t lk, uint32_t i)   /* lane i's DIV in twelfths, 0: the part's */
{
    if (shell_lane_op_i[lk][i] || (!lk && shell_map_on && (i == 0u || i == 1u || i == 3u)))
        return 0;
    return shell_lane_div_i[lk][i];
}
static inline uint32_t lane_dlen(const track_t *t, int32_t lk, uint32_t i)   /* the steps a DIV lane wraps at */
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, own = shell_lane_len_i[lk][i];
    return own && own < len ? own : len;
}
/* each block, a lanes part's DIV lanes: their notes ended at their gate, the steps due by now (the part's step k and
 * how far into it, in twelfths) played. One divide a block, none per sample (not inlined: off the ISR's loops) */
static __attribute__((noinline)) void seq_lanes_div(track_t *t, uint32_t n, uint32_t cur_len)
{
    int32_t lk = lanes_of(t);
    uint32_t i, now = 0, any = 0;
    if (lk < 0)
        return;
    for (i = 0; i < NLANE; i++) {
        if (lane_dnote[lk][i]) {
            if (lane_doff[lk][i] <= n) {
                trk_note_off(t, lane_dnote[lk][i] - 1u);
                lane_dnote[lk][i] = 0;
            } else
                lane_doff[lk][i] -= n;
        }
        any |= lane_div(lk, i);
    }
    if (!any || !song.playing || !cur_len)
        return;
    now = seq_abs[trk_index(t)] * 12u + (uint32_t)((uint64_t)(t->seq_pos < cur_len ? t->seq_pos : cur_len) * 12u / cur_len);
    for (i = 0; i < NLANE; i++) {
        uint32_t d = lane_div(lk, i), g = 0;
        if (!d) {
            lane_dwas[lk][i] = 0;
            continue;
        }
        if (lane_dwas[lk][i] != d) {                    /* a DIV turned (or a fresh start): the next of its own steps */
            lane_dnext[lk][i] = (now + d - 1u) / d * d;
            lane_dwas[lk][i] = (uint8_t)d;
        }
        while (lane_dnext[lk][i] <= now && g++ < 4u) {
            uint32_t ln = lane_dlen(t, lk, i), at = shell_lane_pos_i[lk][i] < ln ? shell_lane_pos_i[lk][i] : 0u;
            const step_t *ls = &seq_steps(t)[at];
            shell_lane_pos_i[lk][i] = (uint8_t)((at + 1u) % ln);
            lane_dnext[lk][i] += d;
            if (((ls->hit >> i) & 1u) && ls->time != ST_TIE &&   /* (the conditions count the part's loops: its */
                !lane_miss(t, shell_lstep[lk][i][at], t->seq_idx, (uint32_t)lk, i) && !lane_naps(t, (uint32_t)lk, i)) {
                uint32_t note = lane_note_at(t, ls, i), vel = ((ls->acc >> i) & 1u) || (ls->flags & SF_ACCENT) ? 127u :
                                                      (ls->vel ? ls->vel : 96u);
                if (lane_dnote[lk][i])
                    trk_note_off(t, lane_dnote[lk][i] - 1u);
                trk_note_on(t, note, vel);
                lane_dnote[lk][i] = (uint8_t)(note + 1u);
                lane_doff[lk][i] = cur_len * d / 12u * (uint32_t)t->p[P_SGATE] / 128u + 1u;
            }
        }
    }
}
#endif
static inline uint32_t lane_at(const track_t *t, uint32_t own, volatile uint8_t *pos, uint32_t idx)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, at;
    if (!own || own >= len) {
        *pos = (uint8_t)((idx + 1u) % len);
        return idx % len;
    }
    at = *pos < own ? *pos : 0u;
    *pos = (uint8_t)((at + 1u) % own);
    return at;
}
/* step idx plays: its chance (the one roll, as seq_step made it: a step that does not play applies no lock), the
 * automation and the locks before the notes (so a note-on reads them: eng_drum's KIT, ..), the notes live recording
 * already sounds left out (rskip), then its notes. On its start, or (NUDGE) at its start + its nudge */
static __attribute__((noinline)) void seq_play(track_t *t, uint32_t idx, uint32_t period)
{
    const step_t *s = &seq_steps(t)[idx];
    const motion_store_t *m = chain_motion(t);
    uint32_t skip = SEQ_ROLLED, i, k;
#if FELUCCA_SHELL
    step_t lanes;
    int32_t lk = lanes_of(t);
    if (lk >= 0) {                                      /* a lanes engine: each lane its own step of its own cycle */
        volatile uint8_t *len = shell_lane_len_i[lk], *pos = shell_lane_pos_i[lk];
        uint32_t dm = 0;
        lanes = *s;
        lanes.hit = lanes.acc = 0;
        step_just_clear(&lanes);                        /* (each hit's octave from its own lane's step, below) */
        lanes.probability = 0;                          /* (each lane rolls its own byte, below) */
        lanes.flags &= (uint8_t)~SF_RATCH;
        memset(&lane_rat[lk], 0, sizeof lane_rat[lk]);
        lane_rat[lk].idx = (uint8_t)idx;
        lane_bar(t, (uint32_t)lk);
        for (i = 0; i < NLANE; i++) {
            uint32_t at, hb, ab, lb;
            const step_t *ls;
            if (lane_div(lk, i)) {                      /* (its own clock: here only what a logic lane reads, the step
                                                         * starting with this one's, if one does) */
                dm |= 1u << i;
                if (lane_dnext[lk][i] == seq_abs[trk_index(t)] * 12u) {
                    ls = &seq_steps(t)[pos[i] % lane_dlen(t, lk, i)];
                    lanes.hit |= (uint8_t)(((ls->hit >> i) & 1u) << i);
                    step_set_loct(&lanes, i, step_loct(ls, i));
                    lanes.acc |= (uint8_t)(((ls->acc >> i) & 1u) << i);
                }
                continue;
            }
            at = lane_at(t, len[i], &pos[i], idx);
            ls = &seq_steps(t)[at];
            hb = (ls->hit >> i) & 1u;
            ab = (ls->acc >> i) & 1u;
            lb = shell_lstep[lk][i][at];
            if (hb && (lane_miss(t, lb, idx, (uint32_t)lk, i) || lane_naps(t, (uint32_t)lk, i)))
                hb = ab = 0;                            /* (the roll first, so a NAP keeps LOOP's rolls in step) */
            if (hb && lb >> 6) {                        /* RATCH x2..x4: its repeats are this lane's own */
                lane_rat[lk].m[(lb >> 6) - 1u] |= (uint8_t)(1u << i);
                lane_rat[lk].a[(lb >> 6) - 1u] |= (uint8_t)(ab << i);
            }
            lanes.hit |= (uint8_t)(hb << i);
            lanes.acc |= (uint8_t)(ab << i);
            if (hb)
                step_set_loct(&lanes, i, step_loct(ls, i));
            if (!lk && shell_map_on && (i == 0u || i == 1u || i == 3u)) {   /* RUDIMENT's MAP (MAP_LANE): a new */
                uint32_t h;                                                  /* KICK cycle, the next CHAOS */
                if (!i && !at)
                    map_cyc++;
                h = map_hit(i == 3u ? 2u : i, at, map_cyc);
                lanes.hit |= (uint8_t)((h >> 1) << i);
                lanes.acc |= (uint8_t)((h & 1u) << i);
            }
        }
        lane_logic(lk, &lanes.hit, &lanes.acc, lane_ff[lk]);
        memcpy(lane_rat[lk].oct, lanes.just, sizeof lanes.just);
        lanes.hit &= (uint8_t)~dm;                      /* (a DIV lane plays on its own clock) */
        lanes.acc &= (uint8_t)~dm;
        if (s->time == ST_TIE)                          /* a tied column holds what sounds, whatever its lanes */
            lanes.time = ST_TIE;
        else if (lanes.hit)
            lanes.time = ST_NOTE;
        else if (!lanes.n)
            lanes.time = ST_REST;
        s = &lanes;
    }
#endif
    if (seq_miss(t, s, idx))
        skip = SEQ_MISS;
    if (m)
        motion_step(t, idx, m, skip == SEQ_ROLLED);
#if FELUCCA_SHELL
    if (shell_gen_part == trk_index(t) + 1u && rec_on(t))
        skip = SEQ_MISS;                            /* armed over a generator's take: the generator is heard and */
#endif                                              /* written, the take silent (a take never plays twice) */
    if (t->rskip_n && t->rskip_idx == idx) {
        for (k = 0; k < t->rskip_n; k++) {
            for (i = 0; i < s->n; i++)
                if (s->note[i] == t->rskip[k])
                    skip |= 1u << i;
            for (i = 0; i < NLANE; i++)
                if (((s->hit >> i) & 1u) && lane_note_at(t, s, i) == t->rskip[k])
                    skip |= 1u << (8u + i);
        }
        t->rskip_n = 0;
    }
    seq_step(t, s, idx, period, skip);
}

/* QUANTIZE ON: step seq_idx plays on its start, or (1.5.6, #213) late by the matrix's TIME (not inlined, as seq_play) */
static __attribute__((noinline)) void seq_play_on(track_t *t, uint32_t period)
{
    uint32_t h = seq_time(t, t->seq_idx, period);
    if (h)
        t->nd_late = h + 1u;
    else
        seq_play(t, t->seq_idx, period);
}

/* NUDGE (1.2, #162; QUANTIZE OFF): a step plays at its start + nudge x DIV / 16. When a step starts its own late play
 * (nd_late) and the next step's early one (nd_early: that much before the end of this one) are latched; an early
 * play marks the step (nd_fired), whose start then plays nothing. A late play falls before the next start (at most
 * 7/16 of the step, the shortest swung step is 0.6 of it); an early one never before the late one of the step before.
 * Not across the end of a song row's pattern (the next row's pattern is not there yet): such a step plays on its
 * start. QUANTIZE ON (the default), or every nudge 0 (every older pattern): each step plays on its start, as before.
 * seq_nd_due: before step seq_idx's end is checked, the latched plays due by now (seq_pos): this step's late one, then
 * the next step's early one (not inlined: off the audio ISR's loop, and only called with one latched) */
static __attribute__((noinline)) void seq_nd_due(track_t *t, uint32_t period, uint32_t len, uint32_t cur_len, uint32_t n)
{
    if (t->nd_late != ND_NONE && (t->seq_pos >= t->nd_late - 1u || t->seq_pos >= cur_len)) {
        t->nd_late = ND_NONE;                       /* (also when a shorter DIV / LEN left it past the end) */
        seq_play(t, t->seq_idx, period);
    }
    if (t->nd_early != ND_NONE && t->seq_pos >= t->nd_early - 1u && t->seq_pos != 0x7FFFFFFFu + n) {
        uint32_t nx = (t->seq_idx + 1u) % len;
        t->nd_early = ND_NONE;
        t->nd_fired = (uint8_t)(nx + 1u);
        seq_play(t, nx, period);
    }
}

/* NUDGE: step seq_idx has started: it plays now (or played early: nd_fired), or its late play is latched; and the
 * next step's early play (not inlined, as seq_nd_due) */
static __attribute__((noinline)) void seq_nd_start(track_t *t, uint32_t period, uint32_t len)
{
    uint32_t nx = (t->seq_idx + 1u) % len;
    int32_t d;
    t->nd_early = ND_NONE;
    if (t->nd_fired == t->seq_idx + 1u) {
        ;                                           /* (it played early) */
    } else {                                        /* late by its NUDGE and (1.5.6) the matrix's TIME */
        uint32_t h = seq_time(t, t->seq_idx, period);
        d = seq_nudge(t, &seq_steps(t)[t->seq_idx]);
        if (d > 0 || h)
            t->nd_late = (uint32_t)(d > 0 ? d : 0) * period / NUDGE_DIV + h + 1u;
        else
            seq_play(t, t->seq_idx, period);
    }
    t->nd_fired = 0;
    if ((!chain.running || nx) && (d = seq_nudge(t, &seq_steps(t)[nx])) < 0) {
        uint32_t at = step_samples(t, period, t->seq_idx), back = (uint32_t)-d * period / NUDGE_DIV;
        at = (at > back ? at - back : 0u) + 1u;
        t->nd_early = at < t->nd_late ? t->nd_late : at;   /* (never before this step's own late play) */
    }
}

/* step seq_idx starts (seq_tick; top: PLAY's or a SONG section's first): a loop's first counts it (seq_cond_miss), a
 * recorded note held ties into it (not inlined: once a step, off the audio ISR's loop) */
static __attribute__((noinline)) void seq_enter(track_t *t, uint32_t top, uint32_t len)
{
    if (!t->seq_idx) {
        t->cond_loop = (uint8_t)(top || t->cond_loop + 1u >= COND_LOOPS ? 0u : t->cond_loop + 1u);
        rec_pass_next(trk_index(t));
        shell_loop_top(t);
    }
    rec_hold(t, t->seq_idx, len);
}

#if FELUCCA_SHELL
static void seq_lanes_div(track_t *t, uint32_t n, uint32_t cur_len);
#endif
static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period, len, top;
#if FELUCCA_SHELL
    uint64_t pq = div_q16((uint32_t)t->p[P_SDIV]);
    uint32_t *ka = &seq_abs[trk_index(t)];
#endif
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= n)
            seq_release(t);
        else
            t->seq_off -= n;
    }
    if (!song.playing)
        return;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    len = len ? len : 1u;
    t->seq_pos += n;
    for (;;) {
#if FELUCCA_SHELL                                   /* (the step's length on the Q16 grid, GROOVE's offsets on it) */
        uint32_t cur_len = step_samples(t, period, t->seq_idx) - period + (grid_at(pq, *ka + 1u) - grid_at(pq, *ka));
#else
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
#endif
        if (t->nd_late | t->nd_early)
            seq_nd_due(t, period, len, cur_len, n);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        top = t->seq_pos >= 0x7FFFFFFFu;            /* (PLAY, a SONG section: its first step) */
        t->seq_pos = top ? (chain.running ? chain.carry : 0u) : t->seq_pos - cur_len;
#if FELUCCA_SHELL
        *ka = top ? 0u : *ka + 1u;
#endif
        t->seq_idx = (uint16_t)(t->seq_idx + 1u < len ? t->seq_idx + 1u : 0u);   /* (a LEN shortened past it: */
        seq_enter(t, top, len);                                                     /* the next step is 1) */
        if (SEQ_QNT_PLAY(t) && !t->nd_fired)
            seq_play_on(t, period);                 /* QUANTIZE ON: every step on its start, as before 1.2 */
        else
            seq_nd_start(t, period, len);
    }
    if (t->rat_left)
        seq_ratchet(t, period);
#if FELUCCA_SHELL
    if (lane_rat[0].m[0] | lane_rat[0].m[1] | lane_rat[0].m[2] | lane_rat[1].m[0] | lane_rat[1].m[1] | lane_rat[1].m[2])
        seq_lane_ratchet(t, period);
    seq_lanes_div(t, n, step_samples(t, period, t->seq_idx) - period + (uint32_t)(grid_at(pq, *ka + 1u) - grid_at(pq, *ka)));
#endif
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts (5..16 never get
 * here: midi_event drops them), SEL (1) every channel the selected track; CH5-8 .. CH13-16 (2..4) as CH1-4 on
 * their four channels (midi_base); TRACK (5, 1.5.5) the first track set to the channel, else the first set to OMNI, else
 * none (dropped as a channel outside a block) */
static uint8_t midi_route;                 /* the G_ROUTE events_block last saw (a change to a block: midi_route_block;
                                            * MIDI_ROUTE_AGAIN: TRACK's channels changed, the same again) */
#define MIDI_ROUTE_AGAIN 0xFFu
static int32_t midi_slot(uint32_t ch)      /* the track channel ch plays, -1 none (and with SEL) */
{
    int32_t b = midi_base(), omni = -1;
    uint32_t i;
    if (b != -2)
        return b >= 0 && ch - (uint32_t)b < NPART ? (int32_t)(ch - (uint32_t)b) : -1;
    for (i = 0; i < NPART; i++) {
        uint32_t c = mch_get(i);
        if (c == ch)
            return (int32_t)i;
        if (c == MCH_OMNI && omni < 0)
            omni = (int32_t)i;
    }
    return omni;
}
static int midi_in_block(uint32_t ch) { return midi_slot(ch) >= 0; }   /* ch is heard by a track (never with SEL) */
static track_t *midi_track(uint32_t ch)
{
    int32_t k = midi_slot(ch);
    return k >= 0 ? &trk[k] : TSEL;
}

#include "midi_control.c"
#include "midi_clock.c"

/* the count-in: its first beat now (accented) */
static __attribute__((noinline)) void cin_begin(void)
{
    cin_total = cin_left = (uint8_t)(4u * (rec_cin ? 1u : cin_bars));
    cin_pos = 0;
    memset(cin_n, 0, sizeof cin_n);
    click_req = 2;
}
/* n samples on: a beat ends; the last one starts the sequencer (in this block, before seq_tick: step 0 with it) */
static __attribute__((noinline)) void cin_block(uint32_t n)
{
    uint32_t b = click_beat_len();
    cin_pos += n;
    if (cin_pos < b)
        return;
    cin_pos -= b;
    cin_pos = cin_pos < b ? cin_pos : 0u;
    if (--cin_left) {
        click_req = (uint8_t)((cin_total - cin_left) % 4u ? 1u : 2u);
        return;
    }
    seq_start();
    cin_flush = 1;
}
/* the count-in's kept notes onto step 1, which has just started (events_block, after seq_tick): as if played now */
static __attribute__((noinline)) void cin_record(void)
{
    uint32_t i, k;
    cin_flush = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < cin_n[i]; k++)
            if (rec_on(t)) {
#if FELUCCA_SHELL
                voice_just = cin_just[i][k];
                rec_note(t, cin_note[i][k], cin_vel[i][k]);
                voice_just = 0;
#else
                rec_note(t, cin_note[i][k], cin_vel[i][k]);
#endif
                if ((cin_up[i] >> k) & 1u)
                    rec_release(t, cin_note[i][k]);
            }
        cin_n[i] = 0;
    }
}
/* the metronome's beat: adv samples on while the transport runs (click.c sounds it in this block) */
#if FELUCCA_SHELL
static uint32_t clk_abs;                            /* the click's 1/16s since the transport started */
#endif
static void click_tick(uint32_t adv)
{
    uint32_t p = div_samples(2), beat = 0;
#if FELUCCA_SHELL
    uint64_t pq = div_q16(2);
#endif
    if (clk_pos == CLK_START) {
        clk_pos = 0;
        clk_step = 0;
#if FELUCCA_SHELL
        clk_abs = 0;
#endif
        beat = 2;
    } else {
        clk_pos += adv;
        for (;;) {                                  /* (as seq_tick: the remainder kept) */
#if FELUCCA_SHELL
            p = grid_at(pq, clk_abs + 1u) - grid_at(pq, clk_abs);
#endif
            if (clk_pos < p)
                break;
#if FELUCCA_SHELL
            clk_abs++;
#endif
            clk_pos -= p;
            clk_step = (clk_step + 1u) & 15u;
            if (!(clk_step & 3u))
                beat = clk_step ? 1u : 2u;
        }
    }
    if (beat && click_wanted())
        click_req = (uint8_t)beat;
}
/* the beat of the bar the click is on (0..3: 0 the accented first), -1 stopped: the count-in's, else the playing
 * transport's (clk_step, internal or external clock). It moves in the block a beat clicks in (the header's metronome,
 * ui_draw.c, swings on it; CLICK OFF it moves all the same) */
static int32_t click_beat_now(void)
{
    if (cin_left)
        return (int32_t)((uint32_t)(cin_total - cin_left) % 4u);
    if (!song.playing)
        return -1;
    return clk_pos == CLK_START ? 0 : (int32_t)(clk_step >> 2);
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    qn_chord_block();
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (cin_left && !clock_mode)
        cin_block(n);
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else if (cin_left)
            ;                                       /* counting in already (the UI's PLAY stops it: seq_counting) */
        else if ((cin_bars || rec_cin) && song.rec && !song.playing && !chain.armed)
            cin_begin();                            /* recording from stop: the count-in first */
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    if (midi_route != (uint8_t)song.g[G_ROUTE]) {     /* ROUT changed: a block lets go of the other channels */
        midi_route = (uint8_t)song.g[G_ROUTE];
        if (midi_route != ROUTE_SEL)
            midi_route_block();
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        for (i = 0; i < NTRK; i++) {
            trk[i].rh_n = trk[i].rskip_n = 0;
            trk[i].seq_n = trk[i].seq_hold = trk[i].slide_glide = trk[i].rat_left = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            arp_forget(t);
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
#if FELUCCA_SHELL
        /* the keys' latch ends: HOLD off, or an arp mode on (it plays the notes from here) */
        if (!t->armp && t->aholdp && (!t->p[P_AHOLD] || t->p[P_AMODE]) && t->nheld) {
            uint32_t keep = t->p[P_AMODE] != 0;
            uint8_t hn[16], k, m = t->nheld;
            for (k = 0; k < m; k++)
                hn[k] = t->held[k];
            latch_end(t);
            if (keep) {                             /* (the arp takes the chord as held notes) */
                for (k = 0; k < m; k++)
                    t->held[k] = hn[k];
                t->nheld = m;
            }
            if (!t->p[P_AHOLD])
                t->arp_phys = 0;
        }
#endif
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note)
                arp_silence(t);
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
#if FELUCCA_SHELL
    rstrum_block(n);
    prog_block();
    shell_block(n);
#endif
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    if (aud_req)
        seq_audition();                               /* (STEP PREVIEW: a step sounds, stopped) */
    chain_tick(seq_n);
#if FELUCCA_SHELL
    sh_q_isr(seq_n);                                /* (a queued pattern switch lands on the same bar line) */
    sh_move_isr(seq_n);                             /* (the shell's MOVE: a bar line in this block moves the scale
                                                     * before any part steps on it, by the clock's own advance) */
#endif
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    if (cin_flush)
        cin_record();
    if (song.playing)
        click_tick(seq_n);
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], clock_mode ? seq_n : n, clock_mode && song.playing ? seq_n : n);
    beat_pos += clock_mode ? seq_n : n;               /* the beat the ARP LED flashes on (ui_leds) */
    if (beat_pos >= beat_samples()) {
        beat_pos -= beat_samples();
        beat_pos = beat_pos < beat_samples() ? beat_pos : 0u;
        beat_n = (beat_n + 1u) & 3u;
    }
    if (song.playing)
        song.tick++;
}
