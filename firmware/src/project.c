/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots. The slots live in .noinit RAM: they
 * survive resets and UBOOT entry. With FELUCCA_FLASH (default) every save
 * also goes to flash through storage.c, and an
 * empty RAM slot is filled from flash on load.
 *
 * Format 7 ("FUN7", written) stores P_COUNT (byte 66) and maps an older count as user presets do: FUN7 of
 * 89 parameters (before the chord keys P_CHRD / P_VOIC) loads its engine values at today's P_E0..P_E7,
 * the chord keys OFF / CLOSE, and its motion events' ids from its P_E0 on move up with them (proj_motion_ids).
 * 68 + 4 x (91 + 2 + 64 x 9) + chain + motion = 3040 of the 3372 bytes before the name: 332 spare, room for
 * 83 more track parameters (4 bytes each).
 * Format 6 ("FUN6") adds a 36-byte song chain before the checksum.
 * Format 5 ("FUN5", read only) = format 4 with the drum grid: a step is 10 bytes (step_t: its lane hits and
 * their accents after the 8 bytes it was), and each track has 40 reserved bytes (lane[8][5], written 0: room
 * for per-lane sounds of the DRUM engine). 4 ("FUN4": PROJ_NP_V4 parameters, the modulation matrix), 3
 * ("FUN3": PROJ_NP_V3, the SLICER), 2 ("FUN2") and 1 ("FUN1": PROJ_NP_V2) are read and converted, mapped by
 * count as user presets are (the first np - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the
 * parameters added since take their defaults: the SLICER OFF, every matrix slot OFF). Their steps get no
 * hits; on a DRUM track their notes that are a lane's note become its hits (proj_grid: the same notes and
 * velocities play, the grid shows them as its own).
 * Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines
 * added since (SLICE 8, ..) were appended, no index moved.
 *
 * Track 4 was the GM drum part until 1.0 (no engine: its byte 0; its level and reverb send in the
 * globals G_DRLVL / G_DRREV, which are inert now). A project says which it has in `parts`: NPART
 * when written since, 0 before (a reserved byte, always written 0: the format and its size did not
 * change). proj_drums_to_part turns such a track 4 into a DRUM part with its default kit (the GM map, so
 * its drum steps still play drums; until 1.0.2 it was the SAMPLE engine's PERC set), keeping its steps, its
 * pattern and mix parameters (LEN DIV SWING GATE, PAN MUTE), its SLICER, and the drum level and reverb send
 * as LEVEL and REV.
 *
 * The byte `phys` (reserved, always 0, before 1.0) says what a PHYS track's MODEL means: 0 MODEL 2 was
 * DUST (dropped: it loads as MODAL bowed, eng_phys.c phys_legacy); 1 MODEL 4 was DRUM (the kit is the DRUM
 * engine since: such a track loads as DRUM, core.h drum_from_phys); 2 (PROJ_PHYS) as today. proj_phys.
 *
 * Format 8 ("FUN8", written since 1.0) = FUN7 with each track's FM6 patch (eng_fm6.c, the 128-byte packed
 * record, 4 x 128 bytes just before the name): a project is self-contained. On load an FM6 track's SLOT shows F n
 * when its patch is that factory one, else OWN (eng_fm6.c fm6_adopt; a stored 9..40, the B slots of the patch bank
 * before 1.0.3 or of 1.4.1's voice bank, is OWN too: the project has the patch).
 * It is 3584 bytes (FUN7: 3388); FUN7 is read (its tracks get the init patch). The retained cache (proj_slot,
 * .noinit) grew with it: after an update its slot 1 still starts with a FUN7 record, which is read; the other
 * slots fail their hash and come back from flash (persist_boot).
 *
 * Format 9 ("FUN9", written since 1.1) = FUN8 with 64 more bytes: the DRUM lane levels (P_LN0..P_LN7, #97) made
 * P_COUNT 99 (P_E0 91), and 68 + 4 x (99 + 2 + 576) + chain + motion = 3072 no longer fit before FUN8's patches
 * (3056). 3648 bytes, the patches at PROJ_FM6_OFF (3120): 48 spare, room for 12 more track parameters. FUN8 (3584,
 * its 91 parameters mapped by count: the lane levels 100 %) and FUN7 are read; the retained cache grew again (its
 * slot 1 still starts with the older record, read as above).
 *
 * Format 10 ("FUNA", written since 1.2; FUN10 in the docs) = FUN9 laid out the same way, 3840 bytes (a whole storage
 * object, storage.c ST_PAYLOAD_MAX), for four changes made together:
 *   - P_COUNT 104 (P_E0 96): LFO 2's SYNC TRIG POL, QUANTIZE and SPREAD (P_LSYNC .. P_SPRD) before the engine
 *     values. SPREAD (#148) came before FUN10's release, into the spare bytes below: a FUN10 of a 1.2 build before
 *     it holds 103 (byte 66) and loads by count (SPREAD 0, its engine values and their motion moved up by one);
 *   - 128 motion records (core.h MOTION_MAX, 64 before), 3 bytes each (place, id, the value as an int8_t: the values
 *     were always -64..127): the block is 4 + 384 = 388 bytes (FUN9's 260 held 64 of 4 bytes);
 *   - each step's NUDGE (core.h step_nudge, -8..+7) in bit 7 of its four note bytes (bit j in note j), bits every
 *     earlier format wrote 0: the step stays 9 bytes;
 *   - the song's sections (core.h chain_row_t): a slot per track (A..D or "-") and the repeats, 5 bytes a row: the
 *     chain is 4 + 16 x 5 = 84 bytes (FUN6..FUN9: 36, rows {slot, repeat}, chain_v9_t; they load as sections with the
 *     four tracks on the row's slot, song_chain.c chain_from_v9: the same song).
 * 68 + 4 x (104 + 2 + 576) + chain + motion = 3268; the patches at PROJ_FM6_OFF (3312): 44 spare, room for 11 more
 * track parameters. 1.5 took seven of them (no format change, FUN10 as it was): the INSERT (P_ITYPE .. P_IMIX, 96..100),
 * ANALOG's filter TYPE (P_FTYPE, 101) and ENV SYNC (P_ESYNC, 102), before the engine values (P_COUNT 111, P_E0 103; 16
 * spare, room for 4). A FUN10 of 1.2 .. 1.4 (104) loads by count: the INSERT OFF, TYPE LP, ENV SYNC OFF (as they
 * played), its engine values and their motion moved up by seven.
 * Firmware 1.2 .. 1.4 refuses a FUN10 of more parameters than its own (np > P_COUNT): a 1.5 project shows EMPTY there. FUN9 / FUN8 / FUN7 load as before (their 64 records of 4 bytes read by proj_motion_v9, every
 * step's nudge 0: the timing they always had; parameters mapped by count, the four new ones at their defaults:
 * SYNC OFF, TRIG NOTE, POL BI, QUANTIZE ON, SPREAD 0, which play as before 1.2), and so do FUN6..FUN1. Firmware before 1.2
 * refuses a FUN10 everywhere (its size and magic are no format it knows): a slot holding one shows EMPTY there (the
 * flash copy stays until that slot is saved over), a backup PUT of one is refused (rc 1), the retained RAM copy of
 * one fails its checks.
 *
 * Parameter locks (1.1, core.h MOTION_LOCK) are motion records with bit 7 of their id byte set (P_COUNT 99 < 128:
 * the bit is free): no byte moved for them, and every project before 1.1 has none. FUN9 holds them. A FUN8 / FUN7
 * record of a 1.1 development build may hold some: on load their ids move to today's positions as the others do,
 * the lock bit kept (proj_motion_ids). Firmware before 1.1 refuses a project that holds a lock (an id out of its
 * range), as with RATCH below; FUN9 it refuses anyway (a newer format).
 *
 * A step's RATCH (core.h SF_RATCH, the hits - 1, 0..3) is in bit 7 of its velocity byte (bit 0) and of its chance
 * byte (bit 1) of FUN7 / FUN8: bits every firmware before wrote 0, so every older project loads with its steps x1,
 * and no format or size changed. Firmware before RATCH refuses a project that holds a ratchet (those bytes out
 * of their range), as it would a newer format. FUN6..FUN1 have none (x1).
 *
 * DIGITAL (engine 1) was retired in 1.0 (fm4_convert.c): a track of it, in any format, loads as FM6 with the
 * patch converted from its values as the track's own (proj_fm4, on every import; the stored record keeps what it
 * holds until saved again). Its motion events on the EDIT or OP ENV values are dropped.
 *
 * SAMPLE's SET 4, PERC (the GM drum kit), was retired after 1.0.2: a SAMPLE track that selects it, in any
 * format, loads as the DRUM engine with its default kit (core.h drum_from_perc: the same GM key map, its steps
 * as they are, the rest of its sound kept; proj_perc, on every import as proj_fm4). Its motion events on the
 * EDIT values are dropped (SAMPLE's meanings, not DRUM's).
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#define PROJ_MAGIC_11 0x46554E42u              /* "FUNB" (FUN11): SQRL-1's (FELUCCA_SHELL builds), see proj_pack11 */
#define PROJ_MAGIC 0x46554E41u                 /* "FUNA" (FUN10): 104 (1.5: 111) parameters, 128 motion records, nudges */
#define PROJ_MAGIC_V9 0x46554E39u              /* "FUN9": FUN8, 64 bytes longer (the DRUM lane levels, 99 parameters) */
#define PROJ_MAGIC_V8 0x46554E38u              /* "FUN8": FUN7 + the tracks' FM6 patches */
#define PROJ_MAGIC_V7 0x46554E37u              /* "FUN7": serialized (byte params, packed steps), chain, motion */
#define PROJ_MAGIC_V6 0x46554E36u              /* FUN6: 69 parameters, drum grid, chain */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": the grid, without the chain; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, PROJ_NP_V4 parameters, 8-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": four tracks, PROJ_NP_V3 parameters; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NP_V4 69u                         /* P_COUNT of format 4 (P_E0 61) */
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* NPART; 0: track 4 is the old GM drum part (see the top) */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;
    proj_trk_t t[NTRK];
    chain_config_t chain;
    motion_store_t motion;
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t mch[NTRK];                         /* 1.5.5: MIDI IN TRACK's channels, as song.mch (PROJ_MIDI_OFF) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
#if FELUCCA_SHELL
    uint8_t pair[8];                           /* SQRL-1 (FUN11): the shell's pairing (shell.c sh_pair_get): [0] the SOUND
                                                * in front, [1] its NOTE engine, [2] its part; [3..5] the pair behind
                                                * as those (127: none); [6] GROOVE; [7] bit 0: [3] says none with 127
                                                * (clear: with 21, the catalogue's length when FUN11 began) */
    uint8_t pair_ok;                           /* .. it was read from the project (not stored; FUN10 and older: 0) */
    uint8_t lanes[SH_LANES_N];                         /* RUDIMENT's lanes (shell.c sh_lanes_get): LEN[8] FILL[8] ROT[8] NOTE[8]
                                                * OCT (+ 2) DIV[8] (signed, 0 x1; 33 bytes before DIV); stored in FUN11's
                                                * tail as tag 6 when not the default (=LEN 0, 0, 0, AUTO each, OCT 0, x1) */
    uint8_t lines[SH_LANES_N];                       /* LINES' lanes, the same, as tag 8 */
    uint8_t drl[NTRK][32];                     /* each part's DRUM sound per drum (shell.c sh_drl_get): TUNE[8] DECY[8]
                                                * TONE[8] SNAP[8], 64 as designed; tag 7 (part, 32 bytes) per part
                                                * that is not all 64 */
    uint8_t iext[NTRK][3];                     /* each part's FILTER / COMB extras (shell.c sh_iext_get): DRV KTRK ENV,
                                                * ENV + 64; tag 5 (part, 3 bytes) per part not (0, 0, 64) */
    uint8_t tracks[9];                         /* the parts left playing (shell.c sh_tracks_get): LINE's SOUND and NOTE,
                                                * BEAT's, 127 none; the mutes, a bit per part; parts 1 and 2's pairs;
                                                * tag 9 when one holds a pair (5 bytes before the PLAY loops) */
    uint8_t map[8];                            /* RUDIMENT's MAP (shell.c sh_map_get): [0] on, X Y CHAOS ACC KICK SNARE
                                                * HATS; tag 10 when not the default (off, P11_MAP_DEF) */
    uint8_t prog[4];                           /* CHORD's PROG (shell.c sh_prog_get): PROG EVERY DIR LEN; tag 11 when
                                                * not the default (OFF, P11_PROG_DEF) */
    uint8_t acid[10];                          /* ACID's line (shell.c sh_acid_get): LINE ROT TRN+64 OCT+64; then the LINE
                                                * pages' own: ACID's FLUX, S+H's FLUX CLK LEAP, HARP's STRUM SPAN (a
                                                * reader takes what it knows: older firmware the first 4, an older
                                                * project leaves the rest at P11_ACID_DEF's); tag 12 when not the default */
    uint8_t harm[P11_HARM_N];                  /* HARMONY, each NOTE engine's (shell.c sh_harm_get): TYPE VOIC LEAD VEL in
                                                * SH_NOTES' order; tag 14 when one is not the default (OFF CLOSE ABOVE 100) */
    uint8_t umask[2];                          /* SCALE USER's notes from ROOT (seq.c scale_user), low byte first; tag 15
                                                * when not SCALE_USER_DEF */
    uint8_t nudge_ok;                          /* its steps' NUDGE plays (tag 13, empty, when a step holds one); without
                                                * it, saved before SQRL-1 played NUDGE: a QUANTIZE ON part's are cleared
                                                * on load, as they were never heard (seq.c SEQ_QNT_PLAY) */
#endif
    uint32_t sum;
} project_t;
#if FELUCCA_SHELL
static void proj_iext_def(project_t *q)                 /* the INSERT extras' defaults: DRV 0, KTRK 0, ENV 64 */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        q->iext[k][0] = q->iext[k][1] = 0;
        q->iext[k][2] = 64;
    }
}
static const uint8_t P11_MAP_DEF[8] = {0, 64, 64, 0, 63, 64, 48, 64};
static const uint8_t P11_PROG_DEF[4] = {0, 1, 0, 0};    /* OFF, 1 BAR, FWD, ALL */
static void proj_harm_def(uint8_t *h)
{
    uint32_t i;
    for (i = 0; i < P11_HARM_N; i++)
        h[i] = i % 4u == 3u ? 100u : 0u;
}
static const uint8_t P11_ACID_DEF[10] = {0, 0, 64, 64, 0, 0, 0, 0, 0, 8};   /* no seed, ROT 0, TRN 0, OCT 0; FLUX
                                                         * 0, S+H's FLUX 0 CLK STEP LEAP ANY, HARP's STRUM OFF SPAN 8 */
static void proj_tracks_def(project_t *q)               /* no pair on any part left, none muted; MAP off, PROG OFF */
{
    memset(q->tracks, 127, sizeof q->tracks);
    q->tracks[4] = 0;
    memcpy(q->map, P11_MAP_DEF, sizeof q->map);
    memcpy(q->prog, P11_PROG_DEF, sizeof q->prog);
    memcpy(q->acid, P11_ACID_DEF, sizeof q->acid);
    proj_harm_def(q->harm);
    q->umask[0] = (uint8_t)SCALE_USER_DEF;
    q->umask[1] = (uint8_t)(SCALE_USER_DEF >> 8);
    q->nudge_ok = 0;
}
static const uint8_t P11_LANES_DEF[SH_LANES_N] = {[32] = 2, [41] = 12};   /* project_t.lanes: LEN 0 (=LEN, the part's), FILL 0, ROT 0, NOTE
                                                       * AUTO, OCT 0 */
#endif
/* Historical FUN5/6 types are frozen, independent of today's P_COUNT/step_t. */
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc; } step10_t;
typedef struct { int16_t p[69]; uint8_t engine, preset; step10_t step[NSTEP]; uint8_t lane[NLANE][5]; } proj_trk_v5_t;
typedef struct {                               /* format 5, before the song chain */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK];
    uint32_t sum;
} project_v5_t;
typedef struct { uint32_t magic, size; int16_t g[G_COUNT]; uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK]; chain_v9_t chain; uint32_t sum; } project_v6_t;
_Static_assert(sizeof(project_v5_t) == 3352u && sizeof(project_v6_t) == 3388u, "frozen formats 5 / 6 sizes");
/* Serialized FUN7 keeps the retained cache's exact extent. Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 16 bytes of the
 * reserved tail were left for parameters added later. FUN9: the same, 3648 bytes (see the top). */
#define PROJ_STORE_SIZE 3840u                  /* FUN10: a whole storage object (storage.c ST_PAYLOAD_MAX) */
#define PROJ_STORE_V9 3648u                    /* FUN9 */
#define PROJ_MOTION_V9 260u                    /* the motion block of FUN7..FUN9: 64 x (place, id, i16 value) */
#define PROJ_STORE_V8 3584u                    /* FUN8 */
#define PROJ_STORE_V7 3388u                    /* FUN7 */
#define PROJ_NAME_OFF (PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN)
#define PROJ_FM6_OFF (PROJ_NAME_OFF - NTRK * FM6_PACKED)
/* 1.5.5 (#202): MIDI IN TRACK, the last 3 of FUN10's 16 spare bytes (just before the patches: the parameters a later
 * version adds grow from the other end, 13 bytes, 3 more track parameters). 24 bits LE: bits 5k..5k+4 track k's channel
 * as song.mch holds it (0..15 its distance up from channel k + 1, 16 OMNI, 17 OFF; 18..31 read as 0), bit 23 set: MIDI IN
 * is TRACK. Then the globals' G_ROUTE holds the block TRACK falls back to (proj_route_fallback: the block its four
 * channels make, else CH1-4), which is all a firmware before 1.5.5 reads (these bytes it never did): a TRACK project plays
 * there as that block. Every project before has them 0: channels 1..4, MIDI IN as its G_ROUTE says */
#define PROJ_MIDI_OFF (PROJ_FM6_OFF - 3u)
typedef union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE]; } project_store_t;
_Static_assert(G_COUNT == 27u, "FUN7 globals retain original IDs");
_Static_assert(sizeof(project_store_t) == 3840u && PROJ_STORE_V7 == sizeof(project_v6_t), "FUN10 / FUN7 sizes");
typedef struct {                               /* a track of format 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v4_t;
typedef struct {                               /* format 4 (1.0 development builds), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v4_t t[NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (0.9 .. 1.0), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, rsv[2];
    proj_trk_v3_t t[NTRK];
    uint32_t sum;
} project_v3_t;
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (format 1) */
#define PROJ_DEF_KEEP 0xFEu                    /* .. DRUM's kit (once SAMPLE PERC), steps and the rest kept (old drums) */
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v4_t) == 2680u, "formats 1 / 2 / 3 / 4 as they were stored");
project_store_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static uint8_t as_hold;                        /* SQRL-1: an autosave left unrestored at power-on (a boot not counted
                                                * clean): none is written over it until the player saves or loads a
                                                * project (stock Felucca: always 0) */
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q &&
    q->sum == proj_sum(q) && chain_valid(&q->chain) && motion_valid(&q->motion); }

/* G_RTYPE (id 24) was G_DRCH, the GM drum part's MIDI channel (0..16, 10 by default) until 1.0: a project of a
 * format before FUN7 may hold any channel there. FUN7 came after it was inert (always 0), so only those imports
 * set it: ROOM, the only reverb they knew */
static void proj_rtype_room(int16_t *g) { g[G_RTYPE] = 0; }

/* the globals of formats 1 and 2 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_v2(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V2 ? g2[i] : GP[i].def;
    proj_rtype_room(g);
}

/* np stored parameters, engine, preset and steps of an older track -> today's, mapped by count (see the top);
 * the steps without hits */
static void proj_trk_from(proj_trk_t *d, const int16_t *p, uint32_t np, uint8_t engine, uint8_t preset, const step8_t *step)
{
    int16_t def[P_E0];
    uint32_t k;
    for (k = 0; k < P_E0; k++)
        def[k] = TP[k].def;
    params_by_count(d->p, p, np, def);
    d->engine = engine;                         /* (indices 0..7 of formats 1 and 2 as they were) */
    d->preset = preset;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &d->step[k];
        memcpy(s->note, step[k].note, 4);
        s->n = step[k].n;
        s->time = step[k].time;
        s->flags = step[k].flags & 3u;          /* accent, slide: x1 (no RATCH then) */
        s->vel = step[k].vel;
        s->hit = s->acc = 0;
    }
}

/* the DRUM tracks of a project of before the grid: their lanes' notes as hits (see the top) */
static void proj_grid(project_t *q)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        if (q->t[k].engine == ENGI_DRUM)
            for (i = 0; i < NSTEP; i++)
                step_to_grid(&q->t[k].step[i]);
    q->sum = proj_sum(q);
}
static void proj_trk_from_v2(proj_trk_t *d, const proj_trk_v2_t *s)
{
    proj_trk_from(d, s->p, PROJ_NP_V2, s->engine, s->preset, s->step);
}

/* a project written with the GM drum part as track 4 (parts 0) -> track 4 a part (see the top);
 * project_load gives it DRUM's kit (PROJ_DEF_KEEP; the SAMPLE PERC sound until 1.0.2). Idempotent */
static void proj_drums_to_part(project_t *q)
{
    proj_trk_t *d = &q->t[NTRK - 1u];
    if (q->parts == NPART)
        return;
    d->engine = ENGI_DRUM;                      /* DRUM's first kit: independent of today's power-on drum sound */
    d->preset = PROJ_DEF_KEEP;
    d->p[P_LEVEL] = (int16_t)clamp(q->g[G_DRLVL], 0, 127);   /* the drum part's level and reverb send */
    d->p[P_REV] = (int16_t)clamp(q->g[G_DRREV], 0, 127);
#if FELUCCA_SHELL
    q->g[G_DRLVL] = 0;                                  /* (now the tape echo's REV and SAT: from here, the song's) */
    q->g[G_DRREV] = 0;
#endif
    q->parts = NPART;
    q->sum = proj_sum(q);
}

/* PHYS tracks of a project written before PROJ_PHYS (see the top) -> today's: DUST as MODAL bowed, MODEL
 * DRUM as the DRUM engine (its E values moved, the preset its first). Idempotent */
static void proj_phys(project_t *q)
{
    uint32_t k;
    if (q->phys >= PROJ_PHYS)
        return;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        if (d->engine != ENGI_PHYS)
            continue;
        if (!q->phys)
            phys_legacy(&d->p[P_E0]);
        if (drum_from_phys(d->engine, &d->p[P_E0])) {
            d->engine = ENGI_DRUM;
            d->preset = 0;
        }
    }
    q->phys = PROJ_PHYS;
    q->sum = proj_sum(q);
}

/* DIGITAL tracks (engine 1; without FELUCCA_FM4) -> FM6 with the converted patch as the track's own (fm4_convert.c,
 * whatever format the project is: the conversion runs on every load, the stored record keeps what it holds until it
 * is saved again). Their motion events on the EDIT values or the OP ENV values go: DIGITAL's meanings do not carry
 * over to FM6's macros. Idempotent */
static void proj_fm4(project_t *q)
{
#if !FELUCCA_FM4
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        uint8_t v[FP_SIZE + 1u];
        uint32_t pr;
        if (d->engine != ENGI_DIGITAL)
            continue;
        for (i = 0; i < P_COUNT; i++)
            d->p[i] = (int16_t)clamp(d->p[i], param_desc_of(ENGI_DIGITAL, i)->min, param_desc_of(ENGI_DIGITAL, i)->max);
        pr = fm4_convert(d->p, v);
        fm6_pack(v, q->fm6[k]);
        d->engine = ENGI_FM6;
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = (uint8_t)pr;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && (MOTION_ID(e) >= P_E0 || (MOTION_ID(e) >= P_FM1_ATK && MOTION_ID(e) <= P_FM4_LEVEL)))
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
#else
    (void)q;
#endif
}

/* SAMPLE tracks of the retired PERC set (SET 4) -> DRUM with its default kit (see the top; core.h drum_from_perc,
 * whatever format the project is: on every load, the stored record keeps what it holds until it is saved again).
 * Their motion events on the EDIT values go. Idempotent */
static void proj_perc(project_t *q)
{
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        if (!drum_from_perc(d->engine, &d->p[P_E0]))
            continue;
        d->engine = ENGI_DRUM;
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = 0;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && MOTION_ID(e) >= P_E0)
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
}

/* a format 4 project (n bytes in *v4) -> slot q as format 6 */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v4->sel;
    q->parts = v4->parts;
    q->phys = v4->phys;
    for (i = 0; i < NTRK; i++)
        proj_trk_from(&q->t[i], v4->t[i].p, PROJ_NP_V4, v4->t[i].engine, v4->t[i].preset, v4->t[i].step);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);
    return 1;
}

/* a format 3 project (n bytes in *v3) -> slot q as format 6 */
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v3->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v3->sel;
    q->parts = v3->parts;
    for (i = 0; i < NTRK; i++)
        proj_trk_from(&q->t[i], v3->t[i].p, PROJ_NP_V3, v3->t[i].engine, v3->t[i].preset, v3->t[i].step);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);                     /* (a format 3 of firmware before 1.0: parts 0) */
    return 1;
}

/* a format 2 project (n bytes in *v2) -> slot q as format 6 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v2->g);
    q->sel = v2->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v2(&q->t[i], &v2->t[i]);
    proj_drums_to_part(q);                     /* (format 2 had the drum track: parts 0) */
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 6: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v1->g);
    q->parts = NPART;                          /* (format 1 had no track 4) */
    proj_trk_from_v2(&q->t[0], &v1->t);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            q->t[i].p[k] = param_desc_of(trk_def_engine(i), k)->def;
        q->t[i].engine = (uint8_t)trk_def_engine(i);
        q->t[i].preset = PROJ_DEF_SOUND;
        for (k = 0; k < NSTEP; k++)
            q->t[i].step[k].time = ST_REST;
    }
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (any format) -> slot q as format 6; 0 = not a project */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t size);
#if FELUCCA_SHELL
static int proj_unpack11(project_t *q, const uint8_t *b);
#endif
/* the init patch on every track (a project of a format before FUN8) */
static void proj_fm6_init(project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
    q->sum = proj_sum(q);
}
static int proj_import_old(project_t *q, const void *b, int n);
static int proj_import_any(project_t *q, const void *b, int n);
/* every track's engine one this firmware has (DIGITAL, 1, is one: it converts): a project of another number
 * is not one (it never was: no engine number past the last was ever stored) */
static int proj_engines_ok(const project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (q->t[t].engine >= NENGINES)
            return 0;
    return 1;
}
/* n bytes of a stored project (any format) -> q as today's, DIGITAL and SAMPLE PERC tracks converted; 0 = not a
 * project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (!proj_import_any(q, b, n) || !proj_engines_ok(q))
        return 0;
#if FELUCCA_SHELL
    if (n != PROJ_STORE_SIZE || ((const uint32_t *)b)[0] != PROJ_MAGIC_11) {   /* (every other format zeroes or copies */
        memcpy(q->lanes, P11_LANES_DEF, sizeof q->lanes);                      /* q whole: the default lanes, after it) */
        memcpy(q->lines, P11_LANES_DEF, sizeof q->lines);
        memset(q->drl, 64, sizeof q->drl);
        proj_iext_def(q);
        proj_tracks_def(q);
        q->sum = proj_sum(q);
    }
#endif
    proj_fm4(q);
    proj_perc(q);
    return 1;
}
static int proj_import_any(project_t *q, const void *b, int n)
{
#if FELUCCA_SHELL
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[0] == PROJ_MAGIC_11)
        return proj_unpack11(q, b);
#endif
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[0] == PROJ_MAGIC)
        return proj_unpack(q, b, PROJ_STORE_SIZE);
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[1] >= 8u && ((const uint32_t *)b)[1] < PROJ_STORE_SIZE)
        n = (int)((const uint32_t *)b)[1];      /* a retained slot holding an older, shorter record: its own size
                                                 * (every format checks its magic and hash) */
    if (n == (int)PROJ_STORE_V9 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V9)
        return proj_unpack(q, b, PROJ_STORE_V9);
    if (n == (int)PROJ_STORE_V8 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V8)
        return proj_unpack(q, b, PROJ_STORE_V8);
    if (n == (int)PROJ_STORE_V7 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V7)
        return proj_unpack(q, b, PROJ_STORE_V7);
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        proj_drums_to_part(q);
        proj_phys(q);
        return 1;
    }
    if (!proj_import_old(q, b, n))
        return 0;
    proj_fm6_init(q);
    return 1;
}
static int proj_import_old(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof(project_v5_t) || n == (int)sizeof(project_v6_t)) {
        const project_v5_t *v = b;
        const project_v6_t *v6 = b;
        uint32_t bytes = n, i, k;
        chain_config_t c6;
        if (((bytes == sizeof *v && v->magic == PROJ_MAGIC_V5) ||
             (bytes == sizeof *v6 && v->magic == PROJ_MAGIC_V6 && chain_from_v9(&c6, &v6->chain))) &&
            v->size == bytes && ((const uint32_t *)b)[bytes / 4u - 1u] == proj_hash(b, bytes - 4u)) {
            memset(q, 0, sizeof *q);
            q->magic = PROJ_MAGIC; q->size = sizeof *q;
            memcpy(q->g, v->g, sizeof q->g);
            proj_rtype_room(q->g);
            q->sel = v->sel; q->parts = v->parts; q->phys = v->phys;
            for (i = 0; i < NTRK; i++) {
                int16_t def[P_COUNT];
                if (v->t[i].engine >= NENGINES) return 0;
                for (k = 0; k < P_COUNT; k++) def[k] = param_desc_of(v->t[i].engine, k)->def;
                params_by_count(q->t[i].p, v->t[i].p, 69u, def);
                q->t[i].engine = v->t[i].engine; q->t[i].preset = v->t[i].preset;
                for (k = 0; k < NSTEP; k++) {           /* (field by field: step_t is not step10_t's layout in
                                                         * every build) */
                    const step10_t *o = &v->t[i].step[k];
                    step_t *s = &q->t[i].step[k];
                    uint32_t j;
                    for (j = 0; j < 4u; j++)
                        s->note[j] = o->note[j];
                    s->n = o->n; s->time = o->time; s->vel = o->vel; s->hit = o->hit; s->acc = o->acc;
                    s->flags = o->flags & 3u;           /* (x1: no RATCH then) */
                }
            }
            if (bytes == sizeof *v6) q->chain = c6; else chain_defaults(&q->chain);
            q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
            return 1;
        }
    }
    if (proj_from_v4(q, (const project_v4_t *)b, n) || proj_from_v3(q, (const project_v3_t *)b, n) ||
        proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n)) {
        proj_phys(q);                          /* (formats 1..3 had no PHYS track: only the byte) */
        proj_grid(q);                          /* (after it: a PHYS DRUM track is DRUM now) */
        return 1;
    }
    return 0;
}

/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* MIDI IN TRACK (PROJ_MIDI_OFF): G_ROUTE TRACK is stored as the block its channels make (0 1 2 3 + 4n: CH1-4 .. CH13-16),
 * else CH1-4, and the TRACK bit */
static uint32_t proj_route_fallback(const uint8_t *mch)
{
    uint32_t k;
    for (k = 1; k < NTRK; k++)
        if (mch[k] != mch[0])
            return 0;
    return mch[0] % 4u || mch[0] >= 16u ? 0u : mch[0] ? mch[0] / 4u + 1u : 0u;
}
static void proj_midi_pack(uint8_t *b, const project_t *q, uint32_t off)   /* the 3 bytes at off */
{
    uint32_t k, v = 0;
    for (k = 0; k < NTRK; k++)
        v |= (uint32_t)(q->mch[k] < MCH_N ? q->mch[k] : 0u) << (5u * k);
    if (q->g[G_ROUTE] == ROUTE_TRACK) {
        int16_t r = (int16_t)proj_route_fallback(q->mch);
        memcpy(b + 8 + 2u * G_ROUTE, &r, 2);
        v |= 1u << 23;
    }
    b[off] = (uint8_t)v; b[off + 1u] = (uint8_t)(v >> 8); b[off + 2u] = (uint8_t)(v >> 16);
}
static void proj_midi_unpack(project_t *q, const uint8_t *b, uint32_t off)
{
    uint32_t k, v = b[off] | (uint32_t)b[off + 1u] << 8 | (uint32_t)b[off + 2u] << 16, f;
    for (k = 0; k < NTRK; k++) {
        f = (v >> (5u * k)) & 31u;
        q->mch[k] = (uint8_t)(f < MCH_N ? f : 0u);
    }
    if (v >> 23)
        q->g[G_ROUTE] = ROUTE_TRACK;
}

/* A stable serialized schema: first header retains FUN6's fields; at byte66
 * np, format flags, then four byte-param tracks and nine-byte steps; FUN8: the patches at PROJ_FM6_OFF. */
#if FELUCCA_SHELL
static int proj_pack11(project_store_t *out, const project_t *q);
#endif
static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i; uint32_t magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
#if FELUCCA_SHELL
    return proj_pack11(out, q);                         /* (SQRL-1 writes FUN11 only: FUN10 cannot hold it) */
#endif
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || P_COUNT > 127u) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = P_COUNT;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++) {
            if (q->t[t].p[i] < -64 || q->t[t].p[i] > 127) return 0;
            b[pos++] = (uint8_t)(q->t[t].p[i] + 64);
        }
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            uint32_t r = step_ratchet(s) - 1u;          /* RATCH: bit 7 of the velocity and chance bytes (see the top) */
            uint32_t nd = (uint32_t)step_nudge(s) & 15u;   /* NUDGE (FUN10): bit j in bit 7 of note j */
            if (s->n > 4u || s->time > ST_REST || (s->flags & ~(3u | SF_RATCH | SF_NUDGE)) || s->probability >= COND_FIRST + NCOND)
                return 0;
            for (uint32_t j = 0; j < 4u; j++)           /* what proj_unpack checks, so a saved project always loads: */
                b[pos++] = (uint8_t)((s->note[j] > 127u ? 127u : s->note[j]) | (nd >> j & 1u) << 7);   /* notes and
                                                         * velocity 0..127, accents */
            b[pos++] = (uint8_t)(s->n | s->time << 3 | (s->flags & 3u) << 5);   /* only on hits */
            b[pos++] = (uint8_t)((s->vel > 127u ? 127u : s->vel) | (r & 1u) << 7); b[pos++] = s->hit;
            b[pos++] = s->acc & s->hit;
            b[pos++] = (uint8_t)(s->probability | (r >> 1) << 7);
        }
    }
    if (pos + sizeof q->chain + sizeof q->motion > PROJ_MIDI_OFF) return 0;
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, &q->motion, sizeof q->motion);
    proj_midi_pack(b, q, PROJ_MIDI_OFF);
    memcpy(b + PROJ_FM6_OFF, q->fm6, sizeof q->fm6);
    {   /* the name (0-padded; stops at the first 0) */
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}
/* the motion of a FUN7 / FUN8 written with np parameters (np < P_COUNT: a FUN7 of 89 before the chord keys, every
 * FUN8 of 91 before the DRUM lane levels) -> today's ids: an event names a parameter id, and the ids from that
 * store's P_E0 (np - 8) on moved up with P_E0, as its values did (params_by_count); the ones below kept theirs. A
 * lock's bit 7 (MOTION_LOCK) stays: the id moves under it (a FUN8 / FUN7 of a 1.1 development build may hold
 * locks). 0: an id that store could not have named (np or more) */
static int proj_motion_ids(motion_store_t *m, uint32_t np)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        uint32_t id = MOTION_ID(&m->event[i]);
        if (id >= np) return 0;
        if (np < P_COUNT && id >= np - 8u)
            m->event[i].param = (uint8_t)((m->event[i].param & MOTION_LOCK) | (id + P_COUNT - np));
    }
    return 1;
}
/* the motion block of a FUN7 / FUN8 / FUN9 (PROJ_MOTION_V9 bytes at b: count, on, 2 reserved, 64 x (place, id, i16
 * value LE)) -> m: its records as they are (values -64..127, as motion_valid wanted then), the rest 0. 0: not one */
static int proj_motion_v9(motion_store_t *m, const uint8_t *b)
{
    uint32_t i;
    memset(m, 0, sizeof *m);
    if (b[0] > 64u)
        return 0;
    m->count = b[0];
    m->on = b[1];
    for (i = 0; i < m->count; i++) {
        const uint8_t *e = b + 4u + 4u * i;
        int32_t v = (int16_t)(e[2] | e[3] << 8);
        if (v < -64 || v > 127)
            return 0;
        m->event[i].place = e[0];
        m->event[i].param = e[1];
        m->event[i].value = (int8_t)v;
    }
    return 1;
}
/* a stored FUN10 (st = PROJ_STORE_SIZE), FUN9 (PROJ_STORE_V9: 64 motion records of 4 bytes, no nudges), FUN8
 * (PROJ_STORE_V8: FUN9 with its tail 64 bytes shorter) or FUN7 (PROJ_STORE_V7: no patches, the init one) */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t st)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66], v7 = st == PROJ_STORE_V7, v10 = st == PROJ_STORE_SIZE;
    uint32_t name_off = st - 4u - PROJ_NAME_LEN, end = v7 ? name_off : name_off - NTRK * FM6_PACKED;
    uint32_t mot = v10 ? sizeof q->motion : PROJ_MOTION_V9, chn = v10 ? sizeof q->chain : sizeof(chain_v9_t);
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + st - 4u, 4);
    if (magic != (v7 ? PROJ_MAGIC_V7 : st == PROJ_STORE_V8 ? PROJ_MAGIC_V8 : st == PROJ_STORE_V9 ? PROJ_MAGIC_V9 : PROJ_MAGIC) ||
        size != st || sum != proj_hash(b, st - 4u) ||
        np < 8u || np > P_COUNT || 68u + NTRK * (np + 2u + NSTEP * 9u) + chn + mot > end)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = b[63]; q->phys = b[64];
    if (v7 && (q->g[G_RTYPE] < 0 || q->g[G_RTYPE] > 1))   /* a FUN7 may still hold the old drum channel there */
        proj_rtype_room(q->g);
    for (t = 0; t < NTRK; t++) {
        int16_t values[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++) { if (b[pos] > 191u) return 0; values[i] = (int16_t)b[pos++] - 64; }
        q->t[t].engine = b[pos++]; q->t[t].preset = b[pos++];
        if (q->t[t].engine >= NENGINES) return 0;
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine, i)->def;
        params_by_count(q->t[t].p, values, np, def);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i]; uint32_t meta, nd = 0;
            for (uint32_t j = 0; j < 4u; j++) {         /* (FUN10: bit 7 of note j is bit j of the nudge) */
                if (b[pos] > 127u && !v10) return 0;
                nd |= (uint32_t)(b[pos] >> 7) << j;
                s->note[j] = b[pos++] & 127u;
            }
            meta = b[pos++];
            s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
            s->vel = b[pos] & 127u; meta |= (b[pos++] & 128u) << 1;      /* (RATCH bit 0 at bit 8 of meta) */
            s->hit = b[pos++]; s->acc = b[pos++];
            s->probability = b[pos] & 127u; meta |= (b[pos++] & 128u) << 2;   /* (bit 1 at bit 9) */
            if ((meta & 255u) > 127u || s->n > 4u || s->time > ST_REST || s->probability >= COND_FIRST + NCOND) return 0;
            if (s->acc & ~s->hit) return 0;
            step_set_ratchet(s, (meta >> 8) + 1u);
            step_set_nudge(s, (int32_t)(nd ^ 8u) - 8);
        }
    }
    if (v10)                                            /* (FUN10: sections, a slot per track; before: rows) */
        memcpy(&q->chain, b + pos, sizeof q->chain);
    else {
        chain_v9_t c9;
        memcpy(&c9, b + pos, sizeof c9);
        if (!chain_from_v9(&q->chain, &c9)) return 0;
    }
    pos += chn;
    if (v10)
        memcpy(&q->motion, b + pos, sizeof q->motion);
    else if (!proj_motion_v9(&q->motion, b + pos))
        return 0;
    if (!proj_motion_ids(&q->motion, np) || !chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    if (v10)
        proj_midi_unpack(q, b, PROJ_MIDI_OFF);          /* (1.5.5: 0 in every FUN10 before, FUN9..FUN7 have none) */
    for (t = 0; t < NTRK; t++) {
        if (v7)
            memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
        else
            for (i = 0; i < FM6_PACKED; i++)
                q->fm6[t][i] = b[end + t * FM6_PACKED + i] & 0x7Fu;
    }
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + name_off));
    }
    q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
    return 1;
}

#if FELUCCA_SHELL
/* FUN11 ("FUNB", SQRL-1 only): FUN10's 3840 bytes, its header, chain, motion, MIDI IN TRACK, name and hash, but the
 * rest a stream, since SQRL-1's 127 parameters and steps of six notes with their JUST do not fit fixed records (FUN10
 * would need 3360 bytes before its tail at 3309). Header: FUN10's to byte 64, then 65 the FM6 mask (bit k: track
 * k's patch is in the stream; else FM6_INIT), 66 np, 67 the stream's version (1). The stream from 68, up to
 * P11_PAIR_OFF, zero after its end:
 *   4 x (np parameters + 64, engine, preset), the chain (84 bytes), the motion (count, on, count x (place, param,
 *   value)), 4 x (8 bytes: bit i of byte i / 8 a step that is not empty (n 0, REST, the rest 0), then each such
 *   step's record), the FM6 patches of the mask's tracks (128 bytes each).
 * A step's record: n (3 bits) | time << 3 | accent, slide << 5 | E << 7; vel | J << 7; with E: ratchet - 1 | nudge
 * (4 bits) << 2 | P << 6 | H << 7, then with P the chance byte, with H hit and acc; the n notes; with J the JUST
 * nibbles of the n notes ((n + 1) / 2 bytes, as step_t keeps them). 2 bytes a rest or tie, 3 a note, 11 a chord of
 * six with JUST. Then the shell's pairing (8 bytes, project_t.pair), MIDI IN TRACK (3), the name (12), the hash.
 * Stock Felucca and SQRL-1 before it refuse a FUN11 (its magic): the slot reads empty there */
#define P11_MIDI_OFF (PROJ_NAME_OFF - 3u)
#define P11_PAIR_OFF (P11_MIDI_OFF - 8u)       /* (the stream's end at the latest) */
/* byte 67, the stream's version: 1 as above; P11_VER_TAIL (2) the same stream, then a tagged tail (a later version's
 * units): (tag 1..255, len, len bytes).., a tag 0 ends it, zero after. A reader skips the tags it does not know and
 * takes the project without them; a save writes what it knows (this version: 1, or 2 with tags 6 and 8 when
 * RUDIMENT's or LINES' lanes are not the default). Any other framing
 * takes a new version byte, which every version before it refuses */
#define P11_VER_TAIL 2u
/* the tags (append only, each a unit; a unit's fields append within its tag, a reader takes those it knows and
 * defaults the rest): 1 REVERB, 2 DELAY, 3 CHORUS, 4 SHAPE, 5 INSERT (reserved for v1.1), 6 RUDIMENT's lanes,
 * 7 a DRUM part's sound per drum (the part, then TUNE[8] DECY[8] TONE[8] SNAP[8] + 64; once per such part), 8 LINES'
 * lanes (tag 6's layout). Tag 5 holds INSERT extras: FILTER's and COMB's DRV KTRK ENV + 64, the part first, once per
 * part not (0, 0, 64) */
#define P11_TAG_INS 5u
#define P11_TAG_LANES 6u
#define P11_TAG_DRL 7u
#define P11_TAG_LINES 8u
#define P11_TAG_TRACKS 9u                      /* the tracks: LINE's SOUND and NOTE, BEAT's (127 none), the mutes */
#define P11_TAG_MAP 10u                        /* RUDIMENT's MAP: on, X Y CHAOS ACC KICK SNARE HATS */
#define P11_TAG_PROG 11u                       /* CHORD's PROG: PROG EVERY DIR LEN */
#define P11_TAG_ACID 12u                       /* ACID's line: LINE ROT TRN OCT (+ 64) */
#define P11_TAG_NUDGE 13u                      /* empty: the steps' NUDGE plays (project_t.nudge_ok) */
#define P11_TAG_HARM 14u                       /* HARMONY: each NOTE engine's TYPE VOIC LEAD VEL */
#define P11_TAG_UMASK 15u                      /* SCALE USER's notes, 12 bits */
static void sh_map_get(uint8_t *m);            /* (shell.c) */
static void sh_map_set(const uint8_t *m);
static void sh_prog_get(uint8_t *m);
static void sh_prog_set(const uint8_t *m);
static void sh_acid_get(uint8_t *m);
static void sh_acid_set(const uint8_t *m);
static void sh_harm_get(uint8_t *m);
static void sh_harm_set(const uint8_t *m);
static void sh_tracks_get(uint8_t *t);         /* (shell.c) */
static uint8_t proj_full;                      /* the last proj_pack ran out of room: PROJECT TOO FULL */
static void sh_pair_get(uint8_t *p);           /* (shell.c, included after this file in the tests) */
static void sh_project_before(void);
static void sh_project_loaded(const uint8_t *pair, int has, const uint8_t *tracks);
static void sh_lanes_get(uint32_t k, uint8_t *l);    /* lanes engine k: 0 RUDIMENT, 1 LINES */
static void sh_lanes_set(uint32_t k, const uint8_t *l);
static void sh_drl_get(uint32_t part, uint8_t *d);       /* a part's sound per drum, 32 bytes + 64 */
static void sh_drl_set(uint32_t part, const uint8_t *d);
static void sh_iext_get(uint32_t part, uint8_t *d);      /* a part's INSERT extras, 3 bytes (ENV + 64) */
static void sh_iext_set(uint32_t part, const uint8_t *d);
static int sh_quiet(uint32_t who);                       /* nothing sounding: the settings' write gate (the autosave) */
static int proj_too_full(void) { return proj_full; }

static int proj_step_empty(const step_t *s)
{
    return !s->n && s->time == ST_REST && !s->flags && !s->vel && !s->hit && !s->acc && !s->probability;
}

static int proj_pack11(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw, mask = 0;
    uint32_t pos = 68u, t, i, j, magic = PROJ_MAGIC_11, size = PROJ_STORE_SIZE, sum;
#define PUT(v) do { if (pos >= P11_PAIR_OFF) { proj_full = 1; return 0; } b[pos++] = (uint8_t)(v); } while (0)
    proj_full = 0;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || P_COUNT > 127u) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = P_COUNT; b[67] = 1;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++) {
            if (q->t[t].p[i] < -64 || q->t[t].p[i] > 127) return 0;
            PUT(q->t[t].p[i] + 64);
        }
        PUT(q->t[t].engine); PUT(q->t[t].preset);
    }
    for (i = 0; i < sizeof q->chain; i++)
        PUT(((const uint8_t *)&q->chain)[i]);
    PUT(q->motion.count); PUT(q->motion.on);
    for (i = 0; i < q->motion.count; i++) {
        PUT(q->motion.event[i].place); PUT(q->motion.event[i].param); PUT(q->motion.event[i].value);
    }
    for (t = 0; t < NTRK; t++) {
        const step_t *st = q->t[t].step;
        for (i = 0; i < NSTEP; i += 8u) {
            uint32_t m = 0;
            for (j = 0; j < 8u; j++)
                m |= (uint32_t)!proj_step_empty(&st[i + j]) << j;
            PUT(m);
        }
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &st[i];
            uint32_t r = step_ratchet(s) - 1u, nd = (uint32_t)step_nudge(s) & 15u, e, jt = 0;
            if (proj_step_empty(s))
                continue;
            if (s->n > STEP_NOTES || s->time > ST_REST || (s->flags & ~(3u | SF_RATCH | SF_NUDGE)) ||
                s->probability >= COND_FIRST + NCOND)
                return 0;
            for (j = 0; j < s->n; j++)
                jt |= step_just(s, j) != 0;
            e = r || nd || s->probability || s->hit;
            PUT(s->n | s->time << 3 | (s->flags & 3u) << 5 | e << 7);
            PUT((s->vel > 127u ? 127u : s->vel) | jt << 7);
            if (e) {
                PUT(r | nd << 2 | (uint32_t)(s->probability != 0) << 6 | (uint32_t)(s->hit != 0) << 7);
                if (s->probability)
                    PUT(s->probability);
                if (s->hit) {
                    PUT(s->hit); PUT(s->acc & s->hit);
                }
            }
            for (j = 0; j < s->n; j++)
                PUT(s->note[j] > 127u ? 127u : s->note[j]);
            for (j = 0; jt && j < s->n; j += 2u)        /* (a nibble past n: 0) */
                PUT((step_just(s, j) & 15) | (j + 1u < s->n ? (step_just(s, j + 1u) & 15) << 4 : 0));
        }
    }
    for (t = 0; t < NTRK; t++)
        if (memcmp(q->fm6[t], FM6_INIT, FM6_PACKED)) {
            mask |= (uint8_t)(1u << t);
            for (i = 0; i < FM6_PACKED; i++)
                PUT(q->fm6[t][i]);
        }
    for (t = 0; t < 2u; t++) {                          /* the tail, only when it holds something: version 2 */
        const uint8_t *ln = t ? q->lines : q->lanes;
        if (!memcmp(ln, P11_LANES_DEF, sizeof q->lanes))
            continue;
        b[67] = P11_VER_TAIL;
        PUT(t ? P11_TAG_LINES : P11_TAG_LANES);
        PUT(sizeof q->lanes);
        for (i = 0; i < sizeof q->lanes; i++)
            PUT(ln[i]);
    }
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < sizeof q->drl[t] && q->drl[t][i] == 64u; i++)
            ;
        if (i == sizeof q->drl[t])
            continue;
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_DRL);
        PUT(1u + sizeof q->drl[t]);
        PUT(t);
        for (i = 0; i < sizeof q->drl[t]; i++)
            PUT(q->drl[t][i]);
    }
    if (q->tracks[0] != 127u || q->tracks[2] != 127u || q->tracks[4] || q->tracks[5] != 127u || q->tracks[7] != 127u) {
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_TRACKS);
        PUT(sizeof q->tracks);
        for (i = 0; i < sizeof q->tracks; i++)
            PUT(q->tracks[i]);
    }
    if (memcmp(q->map, P11_MAP_DEF, sizeof q->map)) {
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_MAP);
        PUT(sizeof q->map);
        for (i = 0; i < sizeof q->map; i++)
            PUT(q->map[i]);
    }
    if (memcmp(q->prog, P11_PROG_DEF, sizeof q->prog)) {
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_PROG);
        PUT(sizeof q->prog);
        for (i = 0; i < sizeof q->prog; i++)
            PUT(q->prog[i]);
    }
    if (memcmp(q->acid, P11_ACID_DEF, sizeof q->acid)) {
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_ACID);
        PUT(sizeof q->acid);
        for (i = 0; i < sizeof q->acid; i++)
            PUT(q->acid[i]);
    }
    {
        uint8_t hd[P11_HARM_N];
        proj_harm_def(hd);
        if (memcmp(q->harm, hd, sizeof hd)) {
            b[67] = P11_VER_TAIL;
            PUT(P11_TAG_HARM);
            PUT(sizeof q->harm);
            for (i = 0; i < sizeof q->harm; i++)
                PUT(q->harm[i]);
        }
    }
    if ((q->umask[0] | q->umask[1] << 8) != SCALE_USER_DEF) {
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_UMASK);
        PUT(2);
        PUT(q->umask[0]);
        PUT(q->umask[1]);
    }
    for (t = 0; q->nudge_ok && t < NTRK * NSTEP && !step_nudge(&q->t[t / NSTEP].step[t % NSTEP]); t++)
        ;
    if (q->nudge_ok && t < NTRK * NSTEP) {             /* (an older project's, never heard: no tag, as it came) */
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_NUDGE);
        PUT(0);
    }
    for (t = 0; t < NTRK; t++) {
        if (!q->iext[t][0] && !q->iext[t][1] && q->iext[t][2] == 64u)
            continue;
        b[67] = P11_VER_TAIL;
        PUT(P11_TAG_INS);
        PUT(1u + sizeof q->iext[t]);
        PUT(t);
        for (i = 0; i < sizeof q->iext[t]; i++)
            PUT(q->iext[t][i]);
    }
    if (b[67] == P11_VER_TAIL)
        PUT(0);                                         /* (PUT refuses past the room: PROJECT TOO FULL, never cut) */
#undef PUT
    b[65] = mask;
    memcpy(b + P11_PAIR_OFF, q->pair, sizeof q->pair);
    proj_midi_pack(b, q, P11_MIDI_OFF);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}

static int proj_unpack11(project_t *q, const uint8_t *b)
{
    uint32_t magic, size, sum, pos = 68u, np = b[66], t, i, j, v;
    uint8_t mask = b[65];
#define GET(x) do { if (pos >= P11_PAIR_OFF) return 0; (x) = b[pos++]; } while (0)
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + PROJ_STORE_SIZE - 4u, 4);
    if (magic != PROJ_MAGIC_11 || size != PROJ_STORE_SIZE || sum != proj_hash(b, PROJ_STORE_SIZE - 4u) ||
        (b[67] != 1u && b[67] != P11_VER_TAIL) || np < 8u || np > P_COUNT || mask > 15u)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = b[63]; q->phys = b[64];
    for (t = 0; t < NTRK; t++) {
        int16_t values[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++) {
            GET(v);
            if (v > 191u) return 0;
            values[i] = (int16_t)((int32_t)v - 64);
        }
        GET(q->t[t].engine); GET(q->t[t].preset);
        if (q->t[t].engine >= NENGINES) return 0;
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine, i)->def;
        params_by_count(q->t[t].p, values, np, def);
    }
    for (i = 0; i < sizeof q->chain; i++)
        GET(((uint8_t *)&q->chain)[i]);
    GET(q->motion.count); GET(q->motion.on);
    if (q->motion.count > MOTION_MAX) return 0;
    for (i = 0; i < q->motion.count; i++) {
        GET(q->motion.event[i].place); GET(q->motion.event[i].param); GET(v);
        q->motion.event[i].value = (int8_t)v;
    }
    for (t = 0; t < NTRK; t++) {
        uint8_t occ[NSTEP / 8u];
        for (i = 0; i < NSTEP / 8u; i++)
            GET(occ[i]);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            uint32_t b0, b1, e = 0, jt;
            s->time = ST_REST;
            if (!((occ[i / 8u] >> (i % 8u)) & 1u))
                continue;
            GET(b0); GET(b1);
            s->n = (uint8_t)(b0 & 7u); s->time = (uint8_t)((b0 >> 3) & 3u); s->flags = (uint8_t)((b0 >> 5) & 3u);
            s->vel = (uint8_t)(b1 & 127u); jt = b1 >> 7;
            if (s->n > STEP_NOTES || s->time > ST_REST) return 0;
            if (b0 >> 7) {
                GET(e);
                if ((e >> 6) & 1u) {
                    GET(s->probability);
                    if (s->probability >= COND_FIRST + NCOND) return 0;
                }
                if (e >> 7) {
                    GET(s->hit); GET(s->acc);
                    if (s->acc & ~s->hit) return 0;
                }
            }
            step_set_ratchet(s, (e & 3u) + 1u);
            step_set_nudge(s, (int32_t)(((e >> 2) & 15u) ^ 8u) - 8);
            for (j = 0; j < s->n; j++) {
                GET(s->note[j]);
                if (s->note[j] > 127u) return 0;
            }
            for (j = 0; jt && j < s->n; j += 2u) {
                GET(v);
                if (j + 1u >= s->n && (v >> 4)) return 0;   /* (a nibble past n must be 0) */
                step_set_just(s, j, (int32_t)((v & 15u) ^ 8u) - 8);
                if (j + 1u < s->n)
                    step_set_just(s, j + 1u, (int32_t)((v >> 4) ^ 8u) - 8);
            }
        }
    }
    for (t = 0; t < NTRK; t++) {
        if (!((mask >> t) & 1u)) {
            memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
            continue;
        }
        for (i = 0; i < FM6_PACKED; i++) {
            GET(q->fm6[t][i]);
            if (q->fm6[t][i] > 127u) return 0;
        }
    }
    memcpy(q->lanes, P11_LANES_DEF, sizeof q->lanes);
    memcpy(q->lines, P11_LANES_DEF, sizeof q->lines);
    memset(q->drl, 64, sizeof q->drl);
    proj_iext_def(q);
    proj_tracks_def(q);
    if (b[67] == P11_VER_TAIL)                          /* the tagged tail: the tags known taken, the rest skipped */
        for (;;) {
            uint32_t tag;
            GET(tag);
            if (!tag) break;
            GET(v);
            if (P11_PAIR_OFF - pos < v) return 0;       /* (a tag past the end: corrupt) */
            if (tag == P11_TAG_LANES || tag == P11_TAG_LINES)   /* (shorter: the rest default; longer: a later
                                                                 * version's fields) */
                for (i = 0; i < v && i < sizeof q->lanes; i++)
                    (tag == P11_TAG_LANES ? q->lanes : q->lines)[i] = b[pos + i];
            if (tag == P11_TAG_INS && v && b[pos] < NTRK)
                for (i = 0; i + 1u < v && i < sizeof q->iext[0]; i++)
                    q->iext[b[pos]][i] = b[pos + 1u + i];
            if (tag == P11_TAG_TRACKS)
                for (i = 0; i < v && i < sizeof q->tracks; i++)
                    q->tracks[i] = b[pos + i];
            if (tag == P11_TAG_MAP)
                for (i = 0; i < v && i < sizeof q->map; i++)
                    q->map[i] = b[pos + i];
            if (tag == P11_TAG_PROG)
                for (i = 0; i < v && i < sizeof q->prog; i++)
                    q->prog[i] = b[pos + i];
            if (tag == P11_TAG_ACID)
                for (i = 0; i < v && i < sizeof q->acid; i++)
                    q->acid[i] = b[pos + i];
            if (tag == P11_TAG_NUDGE)
                q->nudge_ok = 1;
            if (tag == P11_TAG_UMASK)
                for (i = 0; i < v && i < sizeof q->umask; i++)
                    q->umask[i] = b[pos + i];
            if (tag == P11_TAG_HARM)
                for (i = 0; i < v && i < sizeof q->harm; i++)
                    q->harm[i] = b[pos + i];
            if (tag == P11_TAG_DRL && v && b[pos] < NTRK)   /* (a part past NTRK: a later firmware's; skipped) */
                for (i = 0; i + 1u < v && i < sizeof q->drl[0]; i++)
                    q->drl[b[pos]][i] = b[pos + 1u + i];
            pos += v;
        }
#undef GET
    for (; pos < P11_PAIR_OFF; pos++)                   /* (zero after the stream's, or the tail's, end) */
        if (b[pos]) return 0;
    if (!proj_motion_ids(&q->motion, np) || !chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    memcpy(q->pair, b + P11_PAIR_OFF, sizeof q->pair);
    q->pair_ok = 1;
    proj_midi_unpack(q, b, P11_MIDI_OFF);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + PROJ_NAME_OFF));
    }
    q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
    return 1;
}
#endif

#if !FELUCCA_SHELL
static int proj_too_full(void) { return 0; }
#endif

#ifndef PROJ_HOST
/* the GM drum part of a project of before 1.0 (proj_drums_to_part): DRUM's first kit, as a preset load sets it
 * (until 1.0.2 the SAMPLE engine's PERC set, retired since) */
static void proj_legacy_drums(track_t *t)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const preset_t *pr = &ENGINES[ENGI_DRUM]->presets[0];
    uint32_t i;
    t->eng_req = ENGI_DRUM;
    t->preset = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
}

static project_t proj_scratch               /* decoded main-loop work, never audio ISR */
    __attribute__((section(".pool")));
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work; no retained expansion */
    project_store_t s;
    uint8_t raw[3840];                         /* (the staging of a backup object, up to a storage object: editor_backup.c) */
} proj_wire_u __attribute__((section(".pool")));
#define proj_wire (proj_wire_u.s)
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */

static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > STEP_NOTES) s[i].n = STEP_NOTES;
        if (s[i].time > ST_REST) s[i].time = ST_REST;
        for (j = 0; j < STEP_NOTES; j++) {
            s[i].note[j] &= 127u;
            if (j >= s[i].n) step_set_just(&s[i], j, 0);   /* (only a sounding note has a JUST) */
        }
        s[i].acc &= s[i].hit;
    }
}

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 3u | SF_RATCH | SF_NUDGE;
            s->vel &= 127u;
            if (s->probability >= COND_FIRST + NCOND) s->probability = 0;
        }
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)param_fit(d, q->t[t].p[i]);   /* (a retired KIT: the kit it plays) */
        }
    }
    q->sum = proj_sum(q);
}

#if FELUCCA_FLASH
/* slot from flash into RAM (format 7, or format 6 / 5 / 4 / 3 / 2 / 1 converted) */
static void proj_fetch(uint32_t slot)
{
    project_store_t *q = &proj_slot[slot & 3u];
    int n;
    proj_wire_gen++;
    n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire);
    if (!proj_import(&proj_scratch, &proj_wire, n))
        memset(q->raw, 0, 4);
    else {
        proj_bound(&proj_scratch);
        if (!proj_pack(q, &proj_scratch))
            memset(q->raw, 0, 4);
    }
}
#endif

#if FELUCCA_SHELL
/* a project's companion (shell.c sh_cmp_pack: the takes parked off their parts, the NOTE records, the SOUND
 * snapshots, the parked knob moves), saved with it in ST_CMP_NOBJ objects of its own (storage.c OBJ_CMP0: slot k's
 * from OBJ_CMP0 + k * ST_CMP_NOBJ, the autosave's as slot 4). Each object: a header (the project's key, the save's
 * generation, its index, the count, the stream's length), then its share of the stream. The key is the CRC of the
 * project as stored, its name left out (a rename keeps the companion). A load takes the companion only when every
 * object is there, valid, of the same save and of this project: a power cut between the writes, a project restored
 * from a backup without its companion, or none at all, and the project loads as it would without one */
static void sh_cmp_pack(void (*put)(uint32_t b));      /* (shell.c) */
static int sh_cmp_unpack(int (*get)(void));
static void sh_cmp_lo_stage(int (*get)(void), uint32_t k, step_t *st);
static uint8_t proj_cmp_slot = 0xFFu;                   /* a load's companion to put back (4: the autosave's), 0xFF none */
static uint32_t proj_cmp_key;
#if FELUCCA_FLASH && FELUCCA_CMP
#define CMP_MAGIC 0x31504D43u                           /* "CMP1" */
typedef struct { uint32_t magic, key, gen, total; uint8_t idx, n, rsv[2]; } cmp_hdr_t;
#define CMP_CAP (ST_PAYLOAD_MAX - (uint32_t)sizeof(cmp_hdr_t))
static uint32_t cmp_pos, cmp_lo, cmp_hi, cmp_gen;
static void cmp_put(uint32_t b)                         /* the stream's bytes of this object into st_buf */
{
    if (cmp_pos >= cmp_lo && cmp_pos < cmp_hi)
        st_buf[sizeof(cmp_hdr_t) + cmp_pos - cmp_lo] = (uint8_t)b;
    cmp_pos++;
}
static uint32_t cmp_key_of(const uint8_t *raw) { return st_crc32(raw, PROJ_NAME_OFF); }
/* the companion of slot (0..3, 4 the autosave), for the project of key: 0 written, -1 too big, -2 a write failed */
static int proj_cmp_save(uint32_t slot, uint32_t key)
{
    uint32_t total, n, i, seq;
    cmp_hdr_t h;
    cmp_pos = cmp_lo = cmp_hi = 0;
    sh_cmp_pack(cmp_put);                               /* (counted: the main loop, stopped, nothing moves meanwhile) */
    total = cmp_pos;
    n = (total + CMP_CAP - 1u) / CMP_CAP;
    if (!n || n > ST_CMP_NOBJ)
        return -1;
    memset(&h, 0, sizeof h);
    h.magic = CMP_MAGIC;
    h.key = key;
    h.gen = ++cmp_gen * 2654435761u ^ fm1_ms ^ key;
    h.total = total;
    h.n = (uint8_t)n;
    for (i = 0; i < n; i++) {
        uint32_t obj = OBJ_CMP0 + slot * ST_CMP_NOBJ + i;
        int to = -1;
        st_save_prep(obj, &to, &seq);                   /* (its check reads into st_buf: before the stream) */
        h.idx = (uint8_t)i;
        memcpy(st_buf, &h, sizeof h);
        cmp_pos = 0;
        cmp_lo = i * CMP_CAP;
        cmp_hi = cmp_lo + CMP_CAP;
        sh_cmp_pack(cmp_put);
        if (st_write(obj, (uint32_t)sizeof h + (total - cmp_lo < CMP_CAP ? total - cmp_lo : CMP_CAP), to, seq))
            return -2;
    }
    return 0;
}
static uint32_t cr_slot, cr_key, cr_gen, cr_n, cr_total, cr_i, cr_pos, cr_len, cr_done;
static int cmp_obj(uint32_t i)                          /* object i into st_buf, checked: its length, -1 */
{
    st_hdr_t sh;
    cmp_hdr_t h;
    if (st_current(OBJ_CMP0 + cr_slot * ST_CMP_NOBJ + i, &sh) < 0 || sh.len <= sizeof h)
        return -1;
    memcpy(&h, st_buf, sizeof h);
    if (h.magic != CMP_MAGIC || h.key != cr_key || h.idx != i)
        return -1;
    if (!i) {
        cr_gen = h.gen;
        cr_n = h.n;
        cr_total = h.total;
        if (!cr_n || cr_n > ST_CMP_NOBJ || cr_total > cr_n * CMP_CAP)
            return -1;
    } else if (h.gen != cr_gen || h.n != cr_n || h.total != cr_total)
        return -1;
    return (int)sh.len;
}
static int cmp_get(void)                                /* the stream's next byte, object after object; -1 its end */
{
    int l;
    if (cr_done >= cr_total)
        return -1;
    if (cr_pos >= cr_len) {
        if (++cr_i >= cr_n || (l = cmp_obj(cr_i)) < 0)
            return -1;
        cr_len = (uint32_t)l;
        cr_pos = sizeof(cmp_hdr_t);
    }
    cr_done++;
    return st_buf[cr_pos++];
}
/* slot's companion (0..3, 4 the autosave's) for the project of key, whole, ready for cmp_get: 1, else 0 */
static int proj_cmp_open(uint32_t slot, uint32_t key)
{
    uint32_t i, sum = 0;
    int l;
    cr_slot = slot;
    cr_key = key;
    if (!flash_ok || cmp_obj(0) < 0)
        return 0;
    for (i = 0; i < cr_n; i++) {                        /* every object of the same save first, then the stream */
        if ((l = cmp_obj(i)) < 0)
            return 0;
        sum += (uint32_t)l - (uint32_t)sizeof(cmp_hdr_t);
    }
    if (sum != cr_total || (l = cmp_obj(0)) < 0)
        return 0;
    cr_i = 0;
    cr_len = (uint32_t)l;
    cr_pos = sizeof(cmp_hdr_t);
    cr_done = 0;
    return 1;
}
/* shell.c sh_project_loaded, before the project's pairs come to the front: the companion the load named, if whole */
static void proj_cmp_pending(void)
{
    uint32_t slot = proj_cmp_slot;
    if (slot > 4u)
        return;
    proj_cmp_slot = 0xFFu;
    if (proj_cmp_open(slot, proj_cmp_key))
        (void)sh_cmp_unpack(cmp_get);
}
/* a queued switch's staged steps of part k: their lanes' octaves from the slot's companion (the project keeps none) */
static void proj_cmp_lo(uint32_t slot, uint32_t k, step_t *st)
{
    if (proj_cmp_open(slot & 3u, cmp_key_of(proj_slot[slot & 3u].raw)))
        sh_cmp_lo_stage(cmp_get, k, st);
}
#else
static uint32_t cmp_key_of(const uint8_t *raw) { (void)raw; return 0; }
static int proj_cmp_save(uint32_t slot, uint32_t key) { (void)slot; (void)key; return 0; }
static void proj_cmp_pending(void) { proj_cmp_slot = 0xFFu; }
static void proj_cmp_lo(uint32_t slot, uint32_t k, step_t *st) { (void)slot; (void)k; (void)st; }
#endif
#endif

static void project_capture(project_t *p)
{
    uint32_t i;
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    p->chain = chain_config;
    for (i = 0; i < NTRK; i++) {
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        fm6_pack(fm6_patch[i], p->fm6[i]);
    }
    p->motion = motion;
    memcpy(p->mch, song.mch, sizeof p->mch);
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
#if FELUCCA_SHELL
    sh_pair_get(p->pair);
    p->pair_ok = 1;
    p->nudge_ok = 1;
    sh_lanes_get(0u, p->lanes);
    sh_lanes_get(1u, p->lines);
    for (i = 0; i < NTRK; i++) {
        sh_drl_get(i, p->drl[i]);
        sh_iext_get(i, p->iext[i]);
    }
    sh_tracks_get(p->tracks);
    sh_map_get(p->map);
    sh_prog_get(p->prog);
    sh_acid_get(p->acid);
    sh_harm_get(p->harm);
    p->umask[0] = (uint8_t)scale_user;
    p->umask[1] = (uint8_t)(scale_user >> 8);
#endif
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, p)) { ui_message(proj_too_full() ? "PROJECT TOO FULL" : "SAVE FORMAT ERROR"); return 2; }

#if FELUCCA_FLASH
    if (flash_ok) {
        if (st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire)) {
            ui_message("SAVE ERROR");
            return 2;
        }
        memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
        proj_name_get(proj_name, (const uint8_t *)p->name);
        proj_cur = (uint8_t)(slot & 3u);
        pat_from_set((slot & 3u) + 1u);
        as_hold = 0;                                    /* (the player chose what to keep: the autosave may write) */
#if FELUCCA_SHELL
        if (proj_cmp_save(slot & 3u, cmp_key_of(proj_wire.raw))) {   /* (the parked takes beside it) */
            ui_message("SAVED: PARKED TAKES NOT KEPT");
            return 0;
        }
#endif
        ui_message("SAVED");
        return 0;
    }
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot & 3u);
    pat_from_set((slot & 3u) + 1u);
    as_hold = 0;
    ui_message("SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""). Uses proj_scratch */
static int project_name(uint32_t slot, char *b) { return project_info(slot, b, 0, 0, 0); }
/* .. and, len48 not 0, the length of each track's pattern as SONG plays it (song_chain.c chain_tick: the clock track's
 * LEN steps of its DIV, clamped as chain_prepare does) in 1/48 quarter notes (a 4/4 bar: 192; 0 = an empty slot):
 * len48[0..NTRK-1] */
static uint32_t len48_of(int32_t len, int32_t div)
{
    uint32_t n = (uint32_t)clamp(len, TP[P_SLEN].min, TP[P_SLEN].max), d = (uint32_t)clamp(div, TP[P_SDIV].min, TP[P_SDIV].max);
    return n * (d < 6u ? 48u / DIV_DEN[d] : d < 10u ? 48u << (d - 5u) : 48u / DIV_DEN[d % 6u]);   /* (fx.c div_samples) */
}
/* SONG's picture of a pattern (ui_graph.c graph_song): its LEN steps in n = min(LEN, 16) cells (a cell: steps
 * c * LEN / n .. (c + 1) * LEN / n - 1), 2 bits a cell, cell c at bits 2c: a step weighs 0 (rest, empty, CHANCE 0),
 * 1 (a tie), 2 (a note or hit), 3 (accented); a cell's level is its steps' mean, rounded up (0..3) */
static uint32_t proj_density(const proj_trk_t *t, uint8_t *n)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), nc = len < 16u ? len : 16u, c, i, d = 0;
    for (c = 0; c < nc; c++) {
        uint32_t i0 = c * len / nc, i1 = (c + 1u) * len / nc, w = 0;
        for (i = i0; i < i1; i++) {
            const step_t *s = &t->step[i];
            if (s->time == ST_TIE)
                w += 1u;
            else if (s->time == ST_NOTE && (s->n || s->hit) && step_chance(s))
                w += (s->flags & SF_ACCENT) || s->acc ? 3u : 2u;
        }
        w = (w + (i1 - i0) - 1u) / (i1 - i0);
        d |= (w < 3u ? w : 3u) << (2u * c);
    }
    *n = (uint8_t)nc;
    return d;
}
/* .. and, den not 0, each track's picture (proj_density: den[k], its cells n[k]; 0 for an empty slot) */
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n)
{
    uint32_t k;
    b[0] = 0;
    if (len48)
        memset(len48, 0, NTRK * sizeof *len48);
    if (den) {
        memset(den, 0, NTRK * sizeof *den);
        memset(n, 0, NTRK);
    }
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t)))
        return 0;
    proj_name_get(b, (const uint8_t *)proj_scratch.name);
    for (k = 0; len48 && k < NTRK; k++)
        len48[k] = (uint16_t)len48_of(proj_scratch.t[k].p[P_SLEN], proj_scratch.t[k].p[P_SDIV]);
    for (k = 0; den && k < NTRK; k++)
        den[k] = proj_density(&proj_scratch.t[k], &n[k]);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    if (!proj_import(p, &proj_slot[slot & 3u], sizeof(project_store_t))) {
        ui_message("EMPTY SLOT");
        return 1;
    }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, p)) { ui_message(proj_too_full() ? "PROJECT TOO FULL" : "SAVE FORMAT ERROR"); return 2; }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire)) {
        ui_message("SAVE ERROR");
        return 2;
    }
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    if (proj_cur == (slot & 3u))
        proj_name_get(proj_name, (const uint8_t *)p->name);
#if FELUCCA_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED (RAM)");
    return 0;
}

/* 1.5.5 (#199): a stored project erased (SAVE > PROJECT, KNOB 2 ERASE, the dialog): the slot empty. Written as a save
 * is (st_save: the other copy of the A/B pair erased and an empty record committed last, as a full backup's restore
 * writes an empty slot), so a power cut leaves the project there or the slot empty, never half of either. The music
 * playing is not touched: it keeps its name; were it loaded from or last saved to this slot it now has none (the
 * header's slot letter goes). 0 erased, 1 refused (playing, an empty slot), 2 failed (the slot as it was) */
static int project_erase(uint32_t slot)
{
    uint32_t k;
    slot &= 3u;
    if (transport_busy()) {                            /* (a flash erase silences the audio: project_save_as) */
        ui_message("STOP TO ERASE");
        return 1;
    }
    if (!project_used(slot)) {
        ui_message("EMPTY SLOT");
        return 1;
    }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + slot, &proj_wire, 0)) {
        ui_message("ERASE ERROR");
        return 2;
    }
#endif
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    if (proj_cur == slot)
        proj_cur = PROJ_NO_SLOT;
    for (k = 0; k < NTRK; k++)
        if (pat_from[k] == slot + 1u)
            pat_from[k] = 0;
#if FELUCCA_FLASH
    if (flash_ok) {
        char b[20] = "PROJECT A ERASED";
        b[8] = (char)('A' + slot);
        ui_message(b);
        return 0;
    }
#endif
    ui_message("ERASED (RAM)");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input) || !proj_engines_ok(input)) return 1;
#if FELUCCA_SHELL
    sh_project_before();                                /* (the pairs playing keep their state in their slots) */
#endif
    mom_back(0);                                  /* (ui_input.c MOMENTARY: the values as they were, then the load) */
    memset(drum_mute, 0, sizeof drum_mute);       /* (1.4.1: the DRUM lanes' mutes, performance state) */
    if (p != input) memcpy(p, input, sizeof *p);
    proj_drums_to_part(p);                              /* a RAM slot of firmware before 1.0 */
    proj_phys(p);                                       /* .. before PHYS lost DUST and DRUM */
    proj_fm4(p);                                        /* .. that had DIGITAL tracks */
    proj_perc(p);                                       /* .. or SAMPLE PERC tracks */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    transport_req = 0;
    chain_config = p->chain;
    motion = p->motion;
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    ui.song_row = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++)                          /* MIDI IN TRACK's channels (1.5.5; 0, channels 1..4, before) */
        song.mch[k] = (uint8_t)(p->mch[k] < MCH_N ? p->mch[k] : 0u);
    midi_route = MIDI_ROUTE_AGAIN;                      /* (events_block: what the channels no longer hear let go) */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine;                         /* (< NENGINES: proj_engines_ok) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range (param_fit) */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)param_fit(d, s->p[i]);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
#if FELUCCA_SHELL
        if (!p->nudge_ok && t->p[P_SQNT])               /* (a live take's timing, kept but never heard) */
            for (i = 0; i < NSTEP; i++)
                step_set_nudge(&t->step[i], 0);
#endif
        {   /* the project's own FM6 patch, never reloaded from SLOT: F n if it is that factory patch, else OWN */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            fm6_set_patch(k, v);
            fm6_adopt(k);
        }
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    pat_from_set(0);                                    /* (project_load: its slot; the header's letter) */
    undo.trk = 0;                                       /* (ui.c) the undo copy belongs to the old project */
    undo_depth++;                                       /* and these loads take none */
    for (k = 0; k < NTRK; k++) {                        /* the power-on sounds: format 1 (tracks 2..4), old drums */
        track_t *t = &trk[k];
        int16_t keep[P_COUNT];
        if (p->t[k].preset == PROJ_DEF_SOUND) {
            apply_preset_to(t, TRK_DEF[k][1]);
            track_defaults_steps(t);
        } else if (p->t[k].preset == PROJ_DEF_KEEP) {   /* the steps, LEVEL PAN MUTE, LEN DIV SWING GATE kept */
            memcpy(keep, t->p, sizeof keep);
            fm1_irq_off();                           /* publish the legacy sound as one bounded parameter batch */
            proj_legacy_drums(t);                     /* (keeps the SLICER: param_kept) */
            t->p[P_REV] = keep[P_REV];                  /* and the drums' reverb send */
            for (i = P_AMODE; i <= P_TRANS; i++)        /* the drum part had no arp or scale */
                t->p[i] = TP[i].def;
            fm1_irq_on();
        }
        pat_sig[k] = ~steps_sig(t);                     /* a project's steps are the user's */
    }
    undo_depth--;
#if FELUCCA_SHELL
    sh_lanes_set(0u, p->lanes);                         /* (the lanes, before their pairs come to the front) */
    sh_lanes_set(1u, p->lines);
    sh_map_set(p->map);
    sh_prog_set(p->prog);
    sh_acid_set(p->acid);
    sh_harm_set(p->harm);
    scale_user = (uint16_t)((p->umask[0] | p->umask[1] << 8) & 0xFFFu);
    for (k = 0; k < NTRK; k++) {
        sh_drl_set(k, p->drl[k]);
        sh_iext_set(k, p->iext[k]);
    }
    sh_project_loaded(p->pair, p->pair_ok, p->tracks);  /* (the project's pairs to the front and the tracks) */
#endif
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    memset(snd_said, 0, sizeof snd_said);               /* a missing sample is said again, after LOADED */
    return 0;
}
static void project_load(uint32_t slot)
{
#if FELUCCA_FLASH
    if (flash_ok && !proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) proj_fetch(slot);
#endif
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) { ui_message("EMPTY SLOT"); return; }
    proj_bound(&proj_scratch);                          /* a retained RAM slot of an older format: as from flash */
#if FELUCCA_SHELL
    proj_cmp_slot = (uint8_t)(slot & 3u);               /* (its companion, put back as its pairs come in) */
    proj_cmp_key = cmp_key_of(proj_slot[slot & 3u].raw);
#endif
    if (!project_restore_runtime(&proj_scratch)) {
        proj_cur = (uint8_t)(slot & 3u);
        pat_from_set((slot & 3u) + 1u);
        as_hold = 0;
    }
#if FELUCCA_SHELL
    proj_cmp_slot = 0xFFu;
#endif
}

#if FELUCCA_SHELL
/* a queued pattern switch (shell.c sh_q): slot's part k into the song chain's source 0, its steps, LEN DIV SWG GATE
 * QNT and its knob moves and locks (the other parts' staged ones kept), and the slot's lanes into ln. Main loop, while
 * it plays: proj_scratch is free then (saves wait for the stop). 0: an empty slot */
static int proj_q_stage(uint32_t slot, uint32_t k, uint8_t ln[2][SH_LANES_N])
{
    chain_pattern_t *c = &chain.source[0];
    project_t *p = &proj_scratch;
    uint32_t i, n;
    k %= NTRK;
#if FELUCCA_FLASH
    if (flash_ok && !proj_import(p, &proj_slot[slot & 3u], sizeof(project_store_t))) proj_fetch(slot);
#endif
    if (!proj_import(p, &proj_slot[slot & 3u], sizeof(project_store_t)))
        return 0;
    proj_bound(p);
    proj_steps(p->t[k].step);                           /* (the slot read and made ready in proj_scratch, the ISR */
    proj_cmp_lo(slot, k, p->t[k].step);                 /* (LINES' octaves: the companion's, as a load puts them) */
    if (!p->nudge_ok && p->t[k].p[P_SQNT])              /* running; as a load: a live take's timing, never heard) */
        for (i = 0; i < NSTEP; i++)
            step_set_nudge(&p->t[k].step[i], 0);
    fm1_irq_off();                                      /* the staging another queued part reads at the bar: its */
    memcpy(c->step[k], p->t[k].step, sizeof c->step[k]);   /* events move down as k's go, in one go (sh_q_isr) */
    memcpy(c->timing[k], &p->t[k].p[P_SLEN], sizeof c->timing[k]);
    c->qnt[k] = (uint8_t)p->t[k].p[P_SQNT];
    for (i = 0, n = 0; i < c->motion.count; i++)
        if ((c->motion.event[i].place >> 6) != k)
            c->motion.event[n++] = c->motion.event[i];
    for (i = 0; i < p->motion.count && n < MOTION_MAX; i++)
        if ((p->motion.event[i].place >> 6) == k)
            c->motion.event[n++] = p->motion.event[i];
    c->motion.count = (uint8_t)n;
    fm1_irq_on();
    memcpy(ln[0], p->lanes, sizeof p->lanes);
    memcpy(ln[1], p->lines, sizeof p->lines);
    return 1;
}
#endif

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"
#if FELUCCA_FLASH && FELUCCA_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if FELUCCA_FLASH
static persist_t persist_saved __attribute__((section(".pool")));
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if FELUCCA_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n))
            persist_saved = p;
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_import(&proj_scratch, &proj_slot[i], sizeof(project_store_t)))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t)); }

/* Main loop only: no flash access or copies when the ISR changes rows. */
static uint32_t chain_prepare(void)
{
    uint32_t i, k, j, used = 0;
    if (FELUCCA_SHELL)                                  /* (no songs: CHAIN_NSRC) */
        return 1;
    if (transport_busy())
        return 2;
    if (!chain_valid(&chain_config) || !chain_config.count)
        return 1;
    for (i = 0; i < chain_config.count; i++)            /* the slots the lanes play (not the silent ones) */
        for (k = 0; k < NTRK; k++) {
            uint32_t s = chain_config.row[i].slot[k];
            if (s >= 4u || ((used >> s) & 1u))
                continue;
            if (!project_used(s))
                return 3u + s;
            used |= 1u << s;
        }
    chain.config = chain_config;
    for (i = 0; i < CHAIN_NSRC; i++)
        if ((used >> i) & 1u) {
            project_t *p = &proj_scratch;
            if (!proj_import(p, &proj_slot[i], sizeof(project_store_t))) return 3u + i;
            chain.source[i].motion = p->motion;
            /* A song keeps its current instruments. Engine-specific motion from
             * another instrument would change a kit/wave/algorithm unexpectedly. */
            {
                motion_store_t *m = &chain.source[i].motion; uint32_t n = 0;
                for (uint32_t e = 0; e < m->count; e++) {
                    const motion_event_t *v = &m->event[e]; uint32_t owner = v->place >> 6;
                    if (MOTION_ID(v) >= P_FM1_ATK && p->t[owner].engine != trk[owner].eng_req) continue;
                    m->event[n++] = *v;
                }
                m->count = (uint8_t)n;
            }
            for (k = 0; k < NTRK; k++) {
                memcpy(chain.source[i].step[k], p->t[k].step, sizeof p->t[k].step);
                proj_steps(chain.source[i].step[k]);
                chain.source[i].qnt[k] = p->t[k].p[P_SQNT] != 0;
                for (j = 0; j < 4u; j++)
                    chain.source[i].timing[k][j] = (int16_t)clamp(p->t[k].p[P_SLEN + j],
                        TP[P_SLEN + j].min, TP[P_SLEN + j].max);
            }
        }
    RING_PUBLISH();
    chain.armed = 1;
    transport_req = 1;
    return 0;
}

static void settings_poll(void)
{
#if FELUCCA_FLASH
    persist_t p;
#if FELUCCA_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
#if FELUCCA_SHELL
    _Static_assert(ST_PAYLOAD_OFF + sizeof(persist_t) + 256u <= ST_TAIL_OFF,
                   "the settings object and SQRL-1's record share the settings sector, with room to spare");
    if (!memcmp(&p, &persist_saved, sizeof p) && !st_tail_dirty) {
#else
    if (!memcmp(&p, &persist_saved, sizeof p)) {
#endif
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
#if FELUCCA_SHELL
        st_tail_dirty = 0;
#endif
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

/* The autosave (1.2, Discussion #130): the music as it is (what a project holds: every track's sound, steps, LEN DIV
 * SWING GATE, mix, the song, the automation and locks, the FM6 patches, the name) kept in its own storage object
 * (OBJ_AUTOSAVE, A/B at 0xE5000 / 0xE6000, never one of the four project slots), and put back at power-on
 * (autosave_boot) with MENU > SYSTEM > RESTORE LAST ON (the default). Flash wear: a 4 KiB erase per write, the two
 * sectors taking turns, so a write only when
 *   - something a project holds changed since the last one (autosave_sig: a hash of those values, no copy; not the
 *     selected track, nor the PROJECT page's own controls),
 *   - and it has stayed the same for AS_IDLE_MS (10 s) with the transport stopped, no key or button down, no voice
 *     sounding and no editor transfer (a backup or a sample: autosave_hold); an erase stops the audio for its
 *     ~40 ms, as every save does, so it never happens while anything plays,
 *   - and at least AS_GAP_MS (60 s) after the one before (AS_RETRY_MS after a failed write).
 * So at most one write a minute while editing, none while playing or idle: about 100,000 erase cycles per sector
 * give over 200,000 writes, a write a minute for 8 hours a day for over a year, far more with real pauses.
 * RESTORE LAST OFF writes none (and restores none). Never after a crash, a hang or a failed boot (main.c): the music
 * that was playing then could be what crashed. Checked every AS_POLL_MS; the main loop only (never the audio ISR).
 * SQRL-1 (FELUCCA_SHELL) uses the same object and sectors: proj_pack writes FUN11 there (its pairs, lanes, sounds per
 * drum and INSERT extras, in the signature too), quiet is the shell's settings gate (shell.c sh_quiet: the echo's tail
 * gone as well), and the restore waits for the shell to come up (shell.c, main.c) and takes a FUN11 only. Each side
 * leaves the other's alone: Felucca refuses a FUN11 (its magic), SQRL-1 a FUN10 or older; nothing is restored and
 * nothing written until the music changes */
#define AS_POLL_MS 250u
#define AS_IDLE_MS 10000u
#define AS_GAP_MS 60000u
#define AS_RETRY_MS 30000u
static struct {
    uint32_t saved, seen;                        /* the signature written last (or loaded at power-on), seen last */
    uint32_t t, last, poll;                      /* fm1_ms: seen changed / busy, the last write, the last check */
    uint32_t writes;                             /* (written since power-on: the host tests count them) */
    uint8_t wrote, err;                          /* a write since power-on (last is valid); the last one failed */
} as __attribute__((section(".pool")));
#if FELUCCA_SHELL
static int autosave_held(void) { return as_hold; }   /* (shell.c: the boot's toast says so) */
static uint8_t as_booted;                        /* autosave_boot ran (once the shell came up: shell.c); none written
                                                  * before (zeroed at power-on, as .pool is not) */
#endif

static uint32_t as_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

/* what an autosave would hold, hashed (no copy, no RAM): project_capture's values */
static uint32_t autosave_sig(void)
{
    uint32_t h = 2166136261u, i, j;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        for (j = 0; j < P_COUNT; j++) {
            int16_t v = motion_base_value(t, j);
            h = as_mix(h, &v, sizeof v);
        }
        h = as_mix(h, &t->eng_req, 1);
        h = as_mix(h, &t->preset, 1);
        h = as_mix(h, t->step, sizeof t->step);
        h = as_mix(h, fm6_patch[i], sizeof fm6_patch[i]);
    }
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_NAME && i != G_LOAD && i != G_SAVE)
            h = as_mix(h, &song.g[i], sizeof song.g[i]);
    h = as_mix(h, song.mch, sizeof song.mch);           /* (1.5.5: MIDI IN TRACK's channels, the project's) */
    h = as_mix(h, &motion, sizeof motion);
    h = as_mix(h, &chain_config, sizeof chain_config);
#if FELUCCA_SHELL
    {   /* what FUN11 holds besides (project_capture): the pairs, the lanes, each part's sound per drum and extras */
        uint8_t b[sizeof proj_scratch.lanes > sizeof proj_scratch.drl[0] ? sizeof proj_scratch.lanes :
                  sizeof proj_scratch.drl[0]];
        sh_pair_get(b);
        h = as_mix(h, b, sizeof proj_scratch.pair);
        for (i = 0; i < 2u; i++) {
            sh_lanes_get(i, b);
            h = as_mix(h, b, sizeof proj_scratch.lanes);
        }
        for (i = 0; i < NTRK; i++) {
            sh_drl_get(i, b);
            h = as_mix(h, b, sizeof proj_scratch.drl[i]);
            sh_iext_get(i, b);
            h = as_mix(h, b, sizeof proj_scratch.iext[i]);
        }
        sh_tracks_get(b);
        h = as_mix(h, b, sizeof proj_scratch.tracks);
        sh_map_get(b);
        h = as_mix(h, b, sizeof proj_scratch.map);
        sh_prog_get(b);
        h = as_mix(h, b, sizeof proj_scratch.prog);
        sh_acid_get(b);
        h = as_mix(h, b, sizeof proj_scratch.acid);
        sh_harm_get(b);
        h = as_mix(h, b, sizeof proj_scratch.harm);
        b[0] = (uint8_t)scale_user;
        b[1] = (uint8_t)(scale_user >> 8);
        h = as_mix(h, b, 2u);
        h = as_mix(h, shell_lstep, sizeof shell_lstep);   /* (the lanes' own CHNC and RATCH: the companion's) */
    }
#endif
    return as_mix(h, proj_name, sizeof proj_name);
}

#if FELUCCA_SHELL
/* PROJECTS > NEW: a real blank, its sounds kept (every part's steps and knob moves, no pair on a part but the front's,
 * none muted, the lanes =LEN with FILL and ROT 0, no MAP, PROG or ACID line, the arp off and nothing latched;
 * sh_new_takes: the takes parked with pairs, the rhythms EMPTY, the generators' memory, EVOLVE and MOVE). The music
 * before stays packed in proj_wire as its one undo until the music changes from the empty one (pn_sig) or anything
 * else packs proj_wire; the empty project is the autosave at once (pn_write: packed through st_buf, so proj_wire
 * keeps the undo), so a power cycle comes back blank */
static uint8_t pn_ok, pn_gen, pn_write;
static uint32_t pn_sig;
static void sh_new_takes(void);
static void project_new(void)
{
    project_t *p = &proj_scratch;
    uint32_t i, j;
    project_capture(p);
    memcpy(shell_lstep_new, shell_lstep, sizeof shell_lstep_new);   /* (the lanes' own bytes are no project field) */
    proj_wire_gen++;
    pn_ok = (uint8_t)proj_pack(&proj_wire, p);
    pn_gen = proj_wire_gen;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < NSTEP; j++)
            step_clear(&p->t[i].step[j]);
    p->motion.count = 0;
    memset(p->motion.event, 0, sizeof p->motion.event);
    memset(p->name, 0, sizeof p->name);
    proj_tracks_def(p);
    memcpy(p->lanes, P11_LANES_DEF, sizeof p->lanes);
    memcpy(p->lines, P11_LANES_DEF, sizeof p->lines);
    for (i = 0; i < NTRK; i++)
        p->t[i].p[P_AMODE] = p->t[i].p[P_AHOLD] = 0;
    p->sum = proj_sum(p);
    project_restore_runtime(p);
    sh_new_takes();
    pn_sig = autosave_sig();
    pn_write = 1;
}
static int project_new_kept(void)                       /* NEW's undo is there: nothing changed since */
{
    return pn_ok && proj_wire_gen == pn_gen && autosave_sig() == pn_sig;
}
static void project_new_undo(void)
{
    if (!project_new_kept())
        return;
    pn_ok = 0;
    if (proj_import(&proj_scratch, &proj_wire, sizeof proj_wire)) {
        proj_bound(&proj_scratch);
        shell_lstep_back = 1;                           /* (sh_project_loaded: the bytes from before NEW) */
        project_restore_runtime(&proj_scratch);
        shell_lstep_back = 0;
    }
}
#endif

/* nothing going on that an erase (the audio stopped ~40 ms) would be heard in, or would race */
static int autosave_quiet(void)
{
#if FELUCCA_SHELL
    return !fm1_in.notes && !fm1_in.buttons && sh_quiet(1u);   /* (the settings' gate: the transport, the voices, the
                                                              * echo's tail) */
#else
    uint32_t k, i;
    if (transport_busy() || fm1_in.notes || fm1_in.buttons)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 0;
    return 1;
#endif
}

static void autosave_hold(void) { as.t = fm1_ms; }   /* the editor's transfers: their staging RAM is proj_wire */

/* main loop, every pass (after settings_poll) */
static void autosave_poll(void)
{
#if FELUCCA_FLASH
    uint32_t sig;
#if FELUCCA_SHELL
    if (!as_booted)
        return;
#endif
    if (!flash_ok || fm1_ms - as.poll < AS_POLL_MS)
        return;
    as.poll = fm1_ms;
    sig = autosave_sig();
    if (sig != as.seen || !autosave_quiet()) {
        as.seen = sig;
        as.t = fm1_ms;
        return;
    }
#if FELUCCA_SHELL
    if (pn_ok) {                                        /* NEW's undo: the music before kept in proj_wire */
        if (proj_wire_gen != pn_gen || sig != pn_sig)
            pn_ok = 0;
        else {
            if (pn_write && !(ui_prefs & PREF_RESTORE_OFF)) {   /* the empty project the autosave at once, packed */
                pn_write = 0;                                    /* through st_buf (proj_wire keeps the undo) */
                as_hold = 0;                                     /* (NEW, as a save or load: the player's say) */
                as.last = fm1_ms;
                as.wrote = 1;
                int to = -1;
                uint32_t seq;
                uint32_t key = 0;
                project_capture(&proj_scratch);
                st_save_prep(OBJ_AUTOSAVE, &to, &seq);   /* (its check reads into st_buf: before the pack) */
                as.err = !proj_pack((project_store_t *)(void *)st_buf, &proj_scratch) ||
                         (key = cmp_key_of(st_buf), st_write(OBJ_AUTOSAVE, sizeof proj_wire, to, seq) != 0) ||
                         proj_cmp_save(4u, key) != 0;   /* (NEW's companion: nothing parked) */
                if (!as.err) {
                    as.saved = sig;
                    as.writes++;
                }
            }
            return;
        }
    }
#endif
    if (sig == as.saved || as_hold || (ui_prefs & PREF_RESTORE_OFF) || fm1_ms - as.t < AS_IDLE_MS ||
        (as.wrote && fm1_ms - as.last < (as.err ? AS_RETRY_MS : AS_GAP_MS)))
        return;
    as.last = fm1_ms;
    as.wrote = 1;
    project_capture(&proj_scratch);
    proj_wire_gen++;
    as.err = !proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0;
#if FELUCCA_SHELL
    as.err = as.err || proj_cmp_save(4u, cmp_key_of(proj_wire.raw)) != 0;   /* (the parked takes beside it) */
#endif
    if (!as.err) {
        as.saved = sig;
        as.writes++;
    }
#endif
}

/* power-on, after the defaults (main.c): the autosave becomes the music when `allowed` (no crash, hang or failed boot
 * before) and RESTORE LAST is ON; an autosave that does not load (none, torn, damaged, another format) is ignored.
 * The music now (restored or not) is what is saved: nothing is written until it changes. SQRL-1: an autosave of its
 * own left unrestored (not allowed) is held: nothing is written over it this session until the player saves or loads
 * a project (as_hold), so the next clean boot brings it back. 1 = restored */
static int autosave_boot(int allowed)
{
    int rc = 0;
#if FELUCCA_FLASH
#if FELUCCA_SHELL
    as_hold = 0;
    if (flash_ok && !allowed && !(ui_prefs & PREF_RESTORE_OFF)) {
        proj_wire_gen++;
        as_hold = st_load(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) == (int)PROJ_STORE_SIZE &&
                  proj_wire.align == PROJ_MAGIC_11;   /* (SQRL-1's own: Felucca's FUN10 is not held) */
    }
#endif
    if (flash_ok && allowed && !(ui_prefs & PREF_RESTORE_OFF)) {
        int n;
        proj_wire_gen++;
        n = st_load(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire);
#if FELUCCA_SHELL
        if (n != PROJ_STORE_SIZE || proj_wire.align != PROJ_MAGIC_11)
            n = 0;                                      /* (Felucca's FUN10, or older: Felucca's music, not SQRL-1's) */
#endif
        if (n > 0 && proj_import(&proj_scratch, &proj_wire, n)) {
            proj_bound(&proj_scratch);
#if FELUCCA_SHELL
            proj_cmp_slot = 4u;                         /* (the autosave's companion: the takes parked when it wrote) */
            proj_cmp_key = cmp_key_of(proj_wire.raw);
#endif
            if (!project_restore_runtime(&proj_scratch)) {
                ui_message("RESTORED");
                rc = 1;
            }
#if FELUCCA_SHELL
            proj_cmp_slot = 0xFFu;
#endif
        }
    }
#else
    (void)allowed;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
#if FELUCCA_SHELL
    as_booted = 1;
#endif
    return rc;
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_store_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
