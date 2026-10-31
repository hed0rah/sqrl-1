/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* the player's work kept where a knob, a preset, a Program Change, a clear or an erase would replace it: one level of
 * undo on REC held through its first ring (UNDO BEAT, UNDO SOUND, UNDO TAKE), driven as tests/shell_test.c drives it.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/dataloss_test tests/dataloss_test.c -lm */
#define SHELL_TEST_NO_MAIN 1
#include "shell_test.c"
#undef main

static uint32_t lane_bits(const track_t *t, uint32_t l)
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}
static void rec_ring1(void)                             /* REC held through its first ring, let go */
{
    sdown(B_REC);
    sframes(SH_LONG_MS + 64u);
    sup(B_REC);
    sframe();
}
static uint32_t fails;
static int probe(const char *what, int ok)
{
    printf("dataloss: %-80s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    return ok;
}
static void fresh(void)                                 /* shell_reset plus the shell's file statics the tests leave */
{
    shell_reset();
    memset((void *)&sh_take, 0, sizeof sh_take);
    sh_rhy_sum = 0;
}

static void nav(int ok) { fails += !ok; }             /* sgoto / sturn_id could not get there: a failure of its own */
static void lizard_spell(void);
int main(void)
{
    track_t *t;
    uint32_t bits;
    int16_t v0, v1;

    fresh();

    /* A. RUDIMENT: a lane built by hand, then a knob of its cycle on HOME (LEN / FILL / ROT, KNOB 2..4) */
    sh_switch_pair(3, SH_N_RUDIMENT);                   /* DRUMS + RUDIMENT */
    sframe();
    t = sh_trk();
    sturn(EN_ALGO, -40);                                /* EMPTY: a blank grid */
    memset((void *)&sh_take, 0, sizeof sh_take);        /* (as a player who never turned ALGORITHM: nothing to undo) */
    skey(SH_BLACK_KEYS[0]);                             /* KICK in front */
    skey(sh_white_key(0)); skey(sh_white_key(3)); skey(sh_white_key(6)); skey(sh_white_key(10));
    bits = lane_bits(t, 0);
    probe("A0 hand-built KICK lane holds steps 1 4 7 11", bits == 0x449u);
    {
        uint8_t ids[4];
        int glob;
        sh_page_ids(ids, &glob);
        nav(sgoto(SHP_NOTE));
        sh_page_ids(ids, &glob);
        probe("A1 RUDIMENT's NOTE MAIN: the lane and its LEN FILL ROT", sh.page[sh.mode] == SHP_NOTE &&
              sh_kind() == SHK_RUDIMENT && ids[0] == SP_LANE && ids[1] == SP_ELEN && ids[2] == SP_EFILL &&
              ids[3] == SP_EROT);
    }
    nav(sturn_id(SP_EROT, 1));                          /* ROT one step, from NOTE MAIN */
    probe("A2 ROT turned once on a hand-built lane: its hits a step later, none gone", lane_bits(t, 0) == 0x892u);
    rec_ring1();
    probe("A3 REC held to the first ring: BEAT UNDONE, the hand-built lane as it was",
          lane_bits(t, 0) == 0x449u && str_eq(sh.toast, "BEAT UNDONE") && sh_eu[2][0] == 0);
    /* the same with a rhythm's lane edited by hand: ALGORITHM IO, add a kick on step 2, turn LEN */
    sh_sp[SP_RHY - SP_FIRST] = 0;
    sturn(EN_ALGO, 1);                                  /* IO: kick 1 5 9 13 (NOTE MAIN stays in front) */
    skey(sh_white_key(1));                              /* a kick on step 2 by hand */
    bits = lane_bits(t, 0);
    probe("A4 IO's kick plus a hand kick on step 2", bits == 0x1113u);
    nav(sturn_id(SP_ELEN, -1));                         /* LEN 15 */
    probe("A5 LEN turned once: the hits stay where they are, the hand kick too, the lane 15 long",
          lane_bits(t, 0) == 0x1113u && sh_eu[0][0] == 15 && shell_lane_len[0] == 15u);
    rec_ring1();
    probe("A6 REC held: BEAT UNDONE puts back IO plus the hand kick, =LEN again",
          str_eq(sh.toast, "BEAT UNDONE") && lane_bits(t, 0) == 0x1113u && sh_eu[0][0] == 0 && shell_lane_len[0] == 0u);

    /* B. PRESETS over an edited sound: no undo gesture in the shell, though ui.c took the copy */
    fresh();
    t = sh_trk();                                       /* FM6 on part 1, HOME */
    {
        uint8_t ids[4]; int glob;
        sh_page_ids(ids, &glob);
        v0 = t->p[ids[0]];
        sturn(EN_K1, 7);
        v1 = t->p[ids[0]];
        probe("B0 KNOB 1 edited the sound", v1 != v0);
        sturn(EN_PRESET, 1);
        probe("B1 PRESETS turned once: the edit is gone", t->p[ids[0]] != v1 && t->preset == 1u);
        rec_ring1();
        probe("B2 REC held to the first ring: SOUND UNDONE, the edit is back",
              t->p[ids[0]] == v1 && str_eq(sh.toast, "SOUND UNDONE"));
        rec_ring1();
        probe("B2b held again: the preset again (undo_swap swaps)", t->p[ids[0]] != v1);
        undo_swap();                                    /* (back to the edit for B3 / B4 below, which then swap once more) */
        /* a pair switch ends the sound undo: the other pair's part must never swap with this copy */
        sturn(EN_PRESET, 1);
        sh_switch(1);                                   /* LIZARD, on the other part */
        sframe();
        rec_ring1();
        probe("B5 after a pair switch REC held says NO TAKE (no stray swap onto the new pair)", str_eq(sh.toast, "NO TAKE TO UNDO"));
    }

    /* B6. the last thing done is undone: a take, then a sound load; REC held gives the sound back, the take stays */
    fresh();
    t = sh_trk();
    {
        uint8_t ids[4]; int glob;
        step_t keep[NSTEP];
        sh_page_ids(ids, &glob);
        sh_take_note(t);                                /* a take: its first note keeps the steps, then goes in */
        t->step[2].note[0] = 64u; t->step[2].time = ST_NOTE; t->step[2].n = 1;
        memcpy(keep, t->step, sizeof keep);
        sturn(EN_K1, 7);
        v1 = t->p[ids[0]];
        sturn(EN_PRESET, 1);
        rec_ring1();
        probe("B6 a take, then PRESETS: REC held gives SOUND UNDONE, the take's notes kept",
              t->p[ids[0]] == v1 && str_eq(sh.toast, "SOUND UNDONE") && !memcmp(keep, t->step, sizeof keep));
        rec_ring1();
        probe("B6b held again: the preset again, the take's notes still kept",
              t->p[ids[0]] != v1 && str_eq(sh.toast, "SOUND UNDONE") && !memcmp(keep, t->step, sizeof keep));
        sh_take.live = 0;                               /* a new take after the load (sh_take_poll, as REC arms) */
        sh_take_note(t);                                /* .. now REC held undoes the take */
        t->step[5].note[0] = 67u; t->step[5].time = ST_NOTE; t->step[5].n = 1;
        rec_ring1();
        probe("B6c a take after the load: REC held undoes the take (TAKE UNDONE)",
              str_eq(sh.toast, "TAKE UNDONE") && !memcmp(keep, t->step, sizeof keep));
    }

    /* C. MIDI Program Change on the part in front */
    fresh();
    t = sh_trk();
    {
        uint8_t ids[4]; int glob;
        sh_page_ids(ids, &glob);
        sturn(EN_K1, 7);
        v1 = t->p[ids[0]];
        ui.mpc[sh.part] = 0x80u | 2u;
        sframe();
        probe("C0 a Program Change replaces the edited sound with preset 3, nothing asked", t->p[ids[0]] != v1 && t->preset == 2u);
        rec_ring1();
        probe("C1 REC held: SOUND UNDONE after a Program Change", t->p[ids[0]] == v1 && str_eq(sh.toast, "SOUND UNDONE"));
    }

    /* D. RUDIMENT: REC held + a white key erases the step on every lane, no undo */
    fresh();
    sh_switch_pair(3, SH_N_RUDIMENT);
    sframe();
    t = sh_trk();
    sh_sp[SP_RHY - SP_FIRST] = 0;
    sturn(EN_ALGO, 1);                                  /* IO */
    memset((void *)&sh_take, 0, sizeof sh_take);        /* (nothing to undo before the erase) */
    bits = lane_bits(t, 0);
    sdown(B_REC);
    sframe();
    skey(sh_white_key(0));
    sup(B_REC);
    sframe();
    probe("D0 REC held + step key 1: the kick on step 1 erased, REC not left armed",
          (bits & 1u) && !(lane_bits(t, 0) & 1u) && !((song.rec >> sh.part) & 1u));
    rec_ring1();
    probe("D1 REC held afterwards: BEAT UNDONE, the erased step is back",
          lane_bits(t, 0) == bits && str_eq(sh.toast, "BEAT UNDONE"));

    /* E. REC's second ring: the clear drops the undo it had */
    fresh();
    t = sh_trk();                                       /* FM6 > KEYS */
    sdown(B_SEQ);
    sframe();
    skey(sh_white_key(0)); skey(sh_white_key(4));
    sup(B_SEQ);
    sframe();
    probe("E0 SEQ held + two keys: two steps", t->step[0].n == 1u && t->step[4].n == 1u);
    sdown(B_REC);
    sframes(SH_LONG_MS + 64u);
    sframes(SH_LONG_MS - SH_TAP_MS + 64u);
    sup(B_REC);
    sframe();
    probe("E1 REC held through both rings: PATTERN CLEARED", t->step[0].n == 0u && str_eq(sh.toast, "PATTERN CLEARED"));
    rec_ring1();
    probe("E2 REC held to the first ring: TAKE UNDONE, both steps back",
          t->step[0].n == 1u && t->step[4].n == 1u && str_eq(sh.toast, "TAKE UNDONE"));

    /* F. SPELL's SAVE while the transport runs: said SAVED, written only once quiet */
    fresh();
    {
        uint32_t m;
        for (m = 0; m < SH_NMODES && SH_MODES[m].engine != ENGI_MOUTH; m++)
            ;
        if (m < SH_NMODES && eng_ok(ENGI_MOUTH)) {
            sh_switch(m);
            sframe();
            nav(sgoto(SHP_SPELL));
            sframe();
            transport_req = 1u;
            srender(4);
            sframe();
            stap(B_SAVE);
            stap(B_OCTUP);
            probe("F0 SPELL SAVE with the transport running: STOP TO SAVE, as SAVE says", song.playing && str_eq(sh.toast, "STOP TO SAVE"));
            sframes(SH_ST_IDLE_MS + 500u);
        } else
            printf("probe: F skipped (no MOUTH in this build)\n");
    }
    lizard_spell();
    printf(fails ? "dataloss: %u FAILED\n" : "dataloss: all passed\n", fails);
    return fails != 0;
}

static void lizard_spell(void)                          /* LIZARD's own drawing, SPELL's phrase left and come back */
{
    track_t *t; int16_t d[8]; uint32_t i, m, n0, n1;
    shell_reset();
    sh_switch(1); sframe(); t = sh_trk();
    t->p[P_E0] = LZM_DRAW;
    nav(sgoto(SHP_DRAW1));
    nav(sturn_id(P_LZD0, 9)); nav(sturn_id(P_LZD1, -7)); nav(sturn_id(P_LZD2, 5));
    for (i = 0; i < 8u; i++) d[i] = t->p[P_LZD0 + i];
    nav(sgoto(SHP_HOME));
    nav(sturn_id(P_E2, 1));                             /* WAVE */
    nav(sgoto(SHP_DRAW1));
    sframe();
    probe("G3 DRAW opened after a WAVE turn: the own levels still there (WAVE plays the factory one)",
          !memcmp(d, &t->p[P_LZD0], sizeof d) && !lizard_own(t->p));
    nav(sturn_id(P_LZD0, -1));                      /* (down: the level may sit at its top) */
    probe("G5 a DRAW knob turned: the own drawing is back in force, edited by that one step",
          lizard_own(t->p) && t->p[P_LZD0] != d[0] && !memcmp(d + 1, &t->p[P_LZD1], sizeof d - sizeof d[0]));
    shell_reset();
    for (m = 0; m < SH_NMODES && SH_MODES[m].engine != ENGI_MOUTH; m++) ;
    sh_switch(m); sframe();
    nav(sgoto(SHP_SPELL));
    sframe(); n0 = spl.n;
    stap(B_OCTUP); stap(B_OCTUP); stap(B_OCTUP); n1 = spl.n;
    nav(sgoto(SHP_ARP));                                /* the NOTE view and back */
    nav(sgoto(SHP_SPELL));
    sframe();
    probe("H1 away and back: the edited phrase is still in the editor", spl.n == n1 && n1 == n0 + 3u);
    sframes(400);
    nav(sgoto(SHP_ARP)); sturn(EN_ALGO, 1); sframes(400);   /* PHRASE turned: the next visit starts from that phrase */
    nav(sgoto(SHP_SPELL));
    sframe();
    probe("H2 PHRASE turned meanwhile: SPELL starts from the new phrase", spl.n != n1 || sh_trk()->p[P_E4] != 0);
}
