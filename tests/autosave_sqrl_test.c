/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* SQRL-1's autosave (project.c autosave_*, shell.c sh_quiet / sh_as_boot, main.c): the shell driven as tests/shell_test.c
 * drives it, on simulated NOR flash (FELUCCA_FLASH 1, which shell_test.c itself is built without), with main.c's loop
 * (shell_input, settings_poll, autosave_poll) and its power-on (RAM lost, the settings from flash, sh_as_boot):
 * the music written once stopped and quiet (the settings' gate: no voice, no echo tail), never while playing; a FUN11
 * with its pairs; restored on a clean boot only, its pair in front over the settings' one; a stock FUN10 left alone.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/autosave_sqrl_test \
 *      tests/autosave_sqrl_test.c -lm
 *   build/host/autosave_sqrl_test [build/host/fun10_stock.bin]        (the stock FUN10 from tests/mk_fun10.c) */
#include <stdint.h>
#include <string.h>
#define FELUCCA_FLASH 1
static uint8_t nor[0x100000], flash_ok;
static uint32_t as_erases;                              /* (the autosave's two sectors, 0xE5000 / 0xE6000) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    memset(nor + off, 0xFF, 4096);
    as_erases += off >= 0xE5000u && off < 0xE7000u;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#define SHELL_TEST_NO_MAIN 1
#include "shell_test.c"
#undef main

static uint32_t fails;
static void probe(const char *what, int ok)
{
    printf("autosave: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint32_t r_acc, tail;                            /* tail: the echo's |wet| the buses report each frame */
/* main.c's loop for ms: the audio as the ISR runs it, the shell, the queued settings write, the autosave */
static void run(uint32_t ms)
{
    uint32_t f;
    for (f = 0; f < ms / 16u; f++) {
        r_acc += 16u * FS / 1000u;
        while (r_acc >= HALF_FRAMES) {
            srender(1);
            r_acc -= HALF_FRAMES;
        }
        fx_wet_peak = (int32_t)(tail > (uint32_t)fx_wet_peak ? tail : (uint32_t)fx_wet_peak);
        host_ticks += 16000u;
        fm1_ms += 16u;
        shell_input();
        settings_poll();
        autosave_poll();
    }
}

/* what RAM holds at power-on (the shell's lanes, the parts' sounds per drum and INSERT extras) */
static int16_t eu0[SH_NLI][3][8], ln0[SH_NLI][8], lsp0[SH_NLI][3], drl0[NTRK][DRL_N][DV_NLANE], ix0[NTRK][IX_N];
static int16_t lx0[SH_NLI][SH_LX_N][8];
/* a power-on: RAM lost, the settings as flash holds them (st: the favourites block the shell keeps them in, 0 none),
 * then main.c: sh_as_boot from boot_clean, the loop until the splash is gone */
static void power_on(int clean, const void *st)
{
    shell_reset();                                      /* (ui_power_on, the shell's own state; runs a frame) */
    memset(sh_nrec, 0, sizeof sh_nrec);
    memset(sh_nstep_bank, 0, sizeof sh_nstep_bank);     /* (the parked takes, the snapshots, the parked knob moves: */
    memset(sh_pk_pair, 0xFF, sizeof sh_pk_pair);        /* .pool, zeroed at power-on by main.c) */
    memset(sh_pk_age, 0, sizeof sh_pk_age);
    sh_pk_n = 0;
    memset(sh_snap, 0, sizeof sh_snap);
    memset(sh_snapx, 0, sizeof sh_snapx);
    sh_park_n = 0;
    memset(shell_lstep, 0, sizeof shell_lstep);
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    memset(&as, 0, sizeof as);
    as_booted = 0;
    memcpy(sh_eu_i, eu0, sizeof eu0);
    memcpy(sh_lx_i, lx0, sizeof lx0);
    memset(shell_lane_seed_i, 0, sizeof shell_lane_seed_i);
    memcpy(sh_lnote_i, ln0, sizeof ln0);
    memcpy(sh_lsp, lsp0, sizeof lsp0);
    memcpy(drum_lane_d, drl0, sizeof drl0);
    memcpy(ins_ext, ix0, sizeof ix0);
    if (st)
        memcpy(&favorites, st, sizeof favorites);
    memset(&sh, 0, sizeof sh);                          /* (the shell comes up again, on these settings) */
    sh_as_boot = (uint8_t)(clean ? 2u : 1u);
    tail = 0;
    as_erases = 0;
    run(16u);
}
static uint32_t as_magic(void)                          /* the newest autosave's first word, 0 none */
{
    st_hdr_t h;
    return st_current(OBJ_AUTOSAVE, &h) < 0 ? 0u : ((const uint32_t *)st_buf)[0];
}

int main(int argc, char **argv)
{
    static uint8_t settings[sizeof favorites];
    step_t keepf[NSTEP], keepb[NSTEP];
    uint32_t fm, fn, fp, bm, bn, bp, k;
    int ok;
    track_t *t;

    memcpy(eu0, sh_eu_i, sizeof eu0);
    memcpy(lx0, sh_lx_i, sizeof lx0);
    memcpy(ln0, sh_lnote_i, sizeof ln0);
    memcpy(lsp0, sh_lsp, sizeof lsp0);
    memcpy(drl0, drum_lane_d, sizeof drl0);
    memcpy(ix0, ins_ext, sizeof ix0);
    memset(nor, 0xFF, sizeof nor);
    flash_ok = 1;
    power_on(1, 0);
    run(SH_SPLASH_MS + 64u);
    run(120000u);
    probe("a first power-on: nothing restored; 2 min unchanged: nothing written",
          !as.writes && !as_erases && !as_magic() && sh.ready);

    /* the music: DRUMS + RUDIMENT on BEAT (hits, lane 3 LEN 12), LIZARD + CHORD in front (notes, LEN 32, a knob move) */
    sgoc(slot_of("DRUMS"), note_of("RUDIMENT"));
    bm = sh.mode; bn = sh.note; bp = sh.part;
    t = sh_trk();
    for (k = 0; k < NSTEP; k++)
        step_clear(&t->step[k]);
    for (k = 0; k < 16u; k += 4u) {
        t->step[k].time = ST_NOTE;
        t->step[k].hit = 1;
    }
    sh_eu_i[0][0][2] = 12;
    sgoc(slot_of("LIZARD"), note_of("CHORD"));          /* (RUDIMENT's BEAT left: it plays on) */
    fm = sh.mode; fn = sh.note; fp = sh.part;
    t = sh_trk();
    for (k = 0; k < NSTEP; k++)
        step_clear(&t->step[k]);
    motion_clear(t);
    t->p[P_SLEN] = 32;
    t->step[0].n = 1; t->step[0].note[0] = 60; t->step[0].time = ST_NOTE; t->step[0].vel = 100;
    t->step[4].n = 1; t->step[4].note[0] = 63; t->step[4].time = ST_NOTE; t->step[4].vel = 90;
    (void)motion_set_event(t, 3, P_DIST, 50);
    memcpy(keepf, t->step, sizeof keepf);
    memcpy(keepb, trk[bp].step, sizeof keepb);
    ok = bp != fp && sh_partpair[bp] == (uint16_t)(bm | bn << 8);

    transport_req = 1;                                  /* played for 2 min: nothing written */
    run(120000u);
    ok = ok && song.playing && !as.writes && !as_erases;
    probe("the music changed while playing: nothing written in 2 min of it", ok);

    transport_req = 2;                                  /* stopped, the echo still ringing: nothing */
    tail = 4000u;
    run(30000u);
    probe("stopped, the echo's tail still sounding (sh_quiet): nothing written in 30 s", !song.playing && !as.writes);
    tail = 0;
    run(9000u);
    ok = !as.writes;
    run(3000u);
    probe("quiet: written once after 10 s (one erase), a FUN11", ok && as.writes == 1u && as_erases == 1u &&
          as_magic() == PROJ_MAGIC_11);
    run(300000u);
    probe("5 min unchanged after it: no more writes", as.writes == 1u && as_erases == 1u);

    /* changed after the write, then the power goes: another pair in front (the settings follow it), the front's steps
     * and the lane changed */
    for (k = 0; k < NSTEP; k++)
        step_clear(&trk[fp].step[k]);
    sh_eu_i[0][0][2] = 16;
    sgoc(slot_of("FM6"), note_of("KEYS"));
    run(2000u);
    ok = sh.mode != fm && SH_ST[SH_ST_PAIR] == sh.mode + 1u;
    memcpy(settings, &favorites, sizeof settings);      /* (the settings in flash: their pair FM6 + KEYS) */

    power_on(0, settings);                              /* a crash, hang or failed boot before: nothing restored */
    run(SH_SPLASH_MS + 64u);
    ok = ok && memcmp(keepf, trk[fp].step, sizeof keepf) && sh.mode != fm;
    run(120000u);
    probe("after a crash / hang / failed boot: not restored, nothing written (the autosave kept)",
          ok && !as.writes && !as_erases && as_magic() == PROJ_MAGIC_11);

    power_on(1, settings);                              /* a clean boot: the music back, its pair in front and BEAT's */
    ok = 1;
    run(SH_SPLASH_MS + 64u);
    ok = sh.mode == fm && sh.note == fn && sh.part == fp && sh_partpair[bp] == (uint16_t)(bm | bn << 8);
    ok = ok && !memcmp(keepf, trk[fp].step, sizeof keepf) && !memcmp(keepb, trk[bp].step, sizeof keepb) &&
         trk[fp].p[P_SLEN] == 32 && motion_count(&trk[fp]) == 1u && sh_eu_i[0][0][2] == 12 &&
         !trk[fp].p[P_MUTE] && !trk[bp].p[P_MUTE];
    probe("a clean boot: the music back (steps, LEN, the knob move, the lane), its pair in front and BEAT's", ok);
    probe("the splash's toast says LAST SESSION RESTORED", str_eq(sh.toast, "LAST SESSION RESTORED"));
    run(20000u);
    probe("the settings follow the restored pair (SH_ST_PAIR: the pair in front)",
          SH_ST[SH_ST_PAIR] == fm + 1u && SH_ST[SH_ST_PAIR + 1u] == fn + 1u);
    run(280000u);
    probe("restored, 5 min unchanged: nothing written", !as.writes && !as_erases);

    /* a returning player whose saved pair this build no longer offers (LIBRARY + CHORD, the old CHORD default), on a
     * boot not counted clean (as after a crash, or an install's restart): the music waits, and nothing the shell does
     * by itself overwrites it; the next clean boot brings it back (the first minute, gone, used to write over it) */
    sgoc(slot_of("FM6"), note_of("KEYS"));
    t = sh_trk();
    for (k = 0; k < NSTEP; k++)
        step_clear(&t->step[k]);
    t->step[0].n = 1; t->step[0].note[0] = 61; t->step[0].time = ST_NOTE; t->step[0].vel = 100;
    fp = sh.part;
    run(15000u);
    memcpy(settings, &favorites, sizeof settings);
    {
        uint8_t *st = settings + ((uint8_t *)SH_ST - (uint8_t *)&favorites);
        st[SH_ST_PAIR] = (uint8_t)(slot_of("LIBRARY") + 1u);
        st[SH_ST_PAIR + 1u] = (uint8_t)(note_of("CHORD") + 1u);
    }
    power_on(0, settings);
    run(SH_SPLASH_MS + 64u);
    ok = sh_partpair[2] == SH_NOPAIR && sh_partpair[3] == SH_NOPAIR &&   /* (BEAT and LINE as left: empty) */
         str_eq(sh.toast, "LAST SESSION KEPT: AUTOSAVE OFF");
    sgoc(slot_of("FM6"), note_of("KEYS"));              /* the player changes things: a new step */
    sh_trk()->step[2].n = 1; sh_trk()->step[2].note[0] = 70; sh_trk()->step[2].time = ST_NOTE; sh_trk()->step[2].vel = 90;
    run(60000u);
    ok = ok && !as.writes;
    power_on(1, settings);
    run(SH_SPLASH_MS + 64u);
    ok = ok && str_eq(sh.toast, "LAST SESSION RESTORED") && trk[fp].step[0].n == 1u && trk[fp].step[0].note[0] == 61u &&
         !trk[fp].step[2].n;
    probe("a cut SOUND's pair saved, a boot not clean: nothing on BEAT or LINE, AUTOSAVE OFF said, edits write nothing; "
          "the old music back at the next clean boot", ok);
    power_on(0, settings);                              /* .. and a PROJECTS save turns the autosave back on */
    run(SH_SPLASH_MS + 64u);
    sgoc(slot_of("FM6"), note_of("KEYS"));
    sh_trk()->step[2].n = 1; sh_trk()->step[2].note[0] = 70; sh_trk()->step[2].time = ST_NOTE; sh_trk()->step[2].vel = 90;
    ok = project_save(3) == 0;
    sh_trk()->step[3].n = 1; sh_trk()->step[3].note[0] = 72; sh_trk()->step[3].time = ST_NOTE; sh_trk()->step[3].vel = 90;
    run(60000u);
    probe("held after a boot not clean: a PROJECTS save, and the autosave writes again", ok && as.writes >= 1u);

    {   /* PROJECTS > NEW: the empty music is the autosave at once (fundamentals.md: a power cycle after NEW is blank),
         * packed through st_buf so UNDO NEW still stands until power-off; once the player changes it, written as ever */
        uint32_t w = as.writes, k, any = 0;
        sh.part = (uint8_t)fp;
        project_new();
        run(3000u);
        ok = as.writes == w + 1u && project_new_kept();
        project_new_undo();                             /* (UNDO NEW, the same session: the music back) */
        ok = ok && trk[fp].step[3].n == 1u && trk[fp].step[3].note[0] == 72u;
        project_new();
        run(3000u);
        power_on(1, settings);
        run(SH_SPLASH_MS + 64u);
        for (k = 0; k < NSTEP; k++)
            any |= trk[fp].step[k].n | trk[SH_BEAT].step[k].hit;
        ok = ok && !any;
        project_new();
        run(2000u);
        sgoc(slot_of("FM6"), note_of("KEYS"));
        sh_trk()->step[5].n = 1; sh_trk()->step[5].note[0] = 66; sh_trk()->step[5].time = ST_NOTE; sh_trk()->step[5].vel = 90;
        run(60000u);
        ok = ok && as.writes >= 1u && !project_new_kept();
        power_on(1, settings);
        run(SH_SPLASH_MS + 64u);
        ok = ok && sh_trk()->step[5].n == 1u && !sh_trk()->step[3].n;
        probe("PROJECTS > NEW: the empty music written at once, UNDO NEW kept till power-off, a power cycle blank; "
              "changed, written as ever", ok);
    }
    {   /* the companion (project.c PROJ_CMP): FM6 + KEYS's melody parked off its part (ANALOG + KEYS, then LIZARD +
         * KEYS took the parts) comes back after a power cycle and plays; a save to B and a load round trip it; a
         * companion that did not commit (the power cut between its write and the project's) is ignored: the project
         * loads, nothing parked */
        static const uint8_t MEL[4] = {60, 64, 67, 72};
        uint32_t fm6 = slot_of("FM6"), kn = note_of("KEYS"), k, q, okp, okb, okt, heard = 0;
        st_hdr_t h;
        int c;
#define MEL_SET() do { t = sh_trk(); for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]); for (k = 0; k < 4u; k++) { \
        t->step[4u * k].n = 1; t->step[4u * k].note[0] = MEL[k]; t->step[4u * k].time = ST_NOTE; \
        t->step[4u * k].vel = 100; } t->p[P_SLEN] = 16; } while (0)
#define MEL_IS() (sh_trk()->step[0].n == 1u && sh_trk()->step[0].note[0] == 60u && sh_trk()->step[12].note[0] == 72u && \
        sh_trk()->step[8].note[0] == 67u)
#define MEL_PARK() do { sgoc(slot_of("ANALOG"), kn); sh_trk()->step[2].n = 1; sh_trk()->step[2].note[0] = 40;       \
        sh_trk()->step[2].time = ST_NOTE; sh_trk()->step[2].vel = 90; sgoc(slot_of("LIZARD"), kn); } while (0)
        /* (ANALOG a take of its own, so LIZARD takes the part left longest ago, FM6's, not an empty one) */
        project_new();
        run(3000u);
        sgoc(fm6, kn);
        MEL_SET();
        MEL_PARK();
        okp = sh_pk_find(fm6, kn) >= 0 && sh_partpair[0] != (uint16_t)(fm6 | kn << 8) &&
              sh_partpair[1] != (uint16_t)(fm6 | kn << 8);
        k = as.writes;
        run(60000u);
        okp = okp && as.writes > k;
        power_on(1, settings);
        run(SH_SPLASH_MS + 64u);
        okp = okp && sh_pk_find(fm6, kn) >= 0;
        sgoc(fm6, kn);
        okp = okp && MEL_IS();
        transport_req = 1;
        for (k = 0; k < 40u; k++) {
            run(16u);
            for (q = 0; q < NVOICE; q++)
                heard |= sh_trk()->v[q].active && sh_trk()->v[q].note == 60u;
        }
        transport_req = 2;
        run(2000u);
        probe("a melody parked off its part (FM6 + KEYS) back after a power cycle, and it plays (the companion)",
              okp && heard);
        MEL_PARK();                                     /* .. saved to B, NEW, B loaded: parked again, back again */
        okb = sh_pk_find(fm6, kn) >= 0 && project_save(1) == 0;
        project_new();
        run(3000u);
        okb = okb && sh_pk_find(fm6, kn) < 0;
        project_load(1);
        run(500u);
        okb = okb && sh_pk_find(fm6, kn) >= 0;
        sgoc(fm6, kn);
        okb = okb && MEL_IS();
        probe("a save to B keeps the parked melody, NEW clears it, loading B brings it back", okb);
        {   /* the lanes' own CHNC and RATCH (the companion's section): set, autosaved, back after a power cycle */
            int okl;
            shell_lstep[0][3][4] = 0x80u | 0u;          /* (HATCL step 5: RATCH x3) */
            shell_lstep[1][2][7] = 10u;                 /* (LINES lane 3 step 8: 50 %) */
            k = as.writes;
            run(60000u);
            okl = as.writes > k;
            power_on(1, settings);
            run(SH_SPLASH_MS + 64u);
            okl = okl && shell_lstep[0][3][4] == 0x80u && shell_lstep[1][2][7] == 10u && !shell_lstep[0][0][0];
            probe("the lanes' own CHNC and RATCH are autosaved and back after a power cycle", okl);
            memset(shell_lstep, 0, sizeof shell_lstep);
        }
        {   /* LANE 2: PAD LOOP WAKE NAP and LOOP's seed, autosaved, back after a power cycle */
            int okx;
            sh_lx_i[0][SH_LX_PAD][2] = 3; sh_lx_i[0][SH_LX_LOOP][2] = 2;
            sh_lx_i[1][SH_LX_WAKE][5] = 3; sh_lx_i[1][SH_LX_NAP][5] = 1;
            shell_lane_seed_i[1][5] = 0x5EEDF00Du;
            k = as.writes;
            run(60000u);
            okx = as.writes > k;
            power_on(1, settings);
            run(SH_SPLASH_MS + 64u);
            okx = okx && sh_lx_i[0][SH_LX_PAD][2] == 3 && sh_lx_i[0][SH_LX_LOOP][2] == 2 && sh_lx_i[1][SH_LX_WAKE][5] == 3 &&
                  sh_lx_i[1][SH_LX_NAP][5] == 1 && shell_lane_seed_i[1][5] == 0x5EEDF00Du && sh_lx_i[0][SH_LX_WAKE][0] == 1;
            probe("LANE 2 (PAD LOOP WAKE NAP, LOOP's seed) is autosaved and back after a power cycle", okx);
            memcpy(sh_lx_i, lx0, sizeof lx0);
            memset(shell_lane_seed_i, 0, sizeof shell_lane_seed_i);
        }
        {   /* LINES' octaves: a hit two octaves up, autosaved (the companion's), back after a power cycle */
            int oko;
            step_t *ls;
            sgoc(slot_of("ANALOG"), note_of("LINES"));
            ls = &sh_trk()->step[6];
            grid_hit(sh_trk(), 6u, 2u, 1u);
            step_set_loct(ls, 2u, 2);
            k = as.writes;
            run(60000u);
            oko = as.writes > k;
            power_on(1, settings);
            run(SH_SPLASH_MS + 64u);
            sgoc(slot_of("ANALOG"), note_of("LINES"));
            ls = &sh_trk()->step[6];
            oko = oko && (ls->hit >> 2) & 1u && step_loct(ls, 2u) == 2 && step_loct(ls, 1u) == 0;
            probe("LINES' octaves (a hit two up) are autosaved and back after a power cycle", oko);
            step_clear(ls);
        }
        {   /* a queued switch to a LINES slot: the staged steps carry each hit's octave from the slot's companion */
            int okq;
            uint32_t i, lp;
            track_t *t;
            static uint8_t ln_[2][SH_LANES_N];
            sgoc(slot_of("ANALOG"), note_of("LINES"));
            t = sh_trk(); lp = sh.part % NTRK;
            for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
            grid_hit(t, 0u, 2u, 1u); step_set_loct(&t->step[0], 2u, 2);     /* lane 3 on step 1, two octaves up */
            grid_hit(t, 4u, 0u, 1u); step_set_loct(&t->step[4], 0u, -1);    /* lane 1 on step 5, an octave down */
            okq = project_save(2) == 0;
            for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
            okq = okq && proj_q_stage(2, lp, ln_) && step_loct(&chain.source[0].step[lp][0], 2u) == 2 &&
                  step_loct(&chain.source[0].step[lp][4], 0u) == -1 && step_loct(&chain.source[0].step[lp][4], 2u) == 0;
            probe("a queued switch to a LINES slot keeps each hit's octave (the slot's companion), not flat", okq);
            memset(chain.source[0].step[lp], 0, sizeof chain.source[0].step[lp]);
        }
        MEL_PARK();                                     /* the torn write: the autosave and its companion, then the */
        run(60000u);                                    /* project changed and written again, its companion cut */
        sgoc(slot_of("ANALOG"), kn);
        sh_trk()->step[1].n = 1; sh_trk()->step[1].note[0] = 50; sh_trk()->step[1].time = ST_NOTE; sh_trk()->step[1].vel = 90;
        k = as.writes;
        run(60000u);
        okt = as.writes > k && (c = st_current(OBJ_CMP0 + 4u * ST_CMP_NOBJ, &h)) >= 0;
        memset(nor + st_sector(OBJ_CMP0 + 4u * ST_CMP_NOBJ, (uint32_t)c), 0xFF, sizeof h);   /* (never committed) */
        power_on(1, settings);
        run(SH_SPLASH_MS + 64u);
        okt = okt && str_eq(sh.toast, "LAST SESSION RESTORED") && sh_pk_find(fm6, kn) < 0;
        sgoc(slot_of("ANALOG"), kn);
        okt = okt && sh_trk()->step[1].n == 1u && sh_trk()->step[1].note[0] == 50u;
        sgoc(fm6, kn);
        okt = okt && !sh_trk()->step[0].n;
        probe("a companion that did not commit (the cut between the writes): the project loads, nothing parked", okt);
        probe("the companions live where the user samples were: 0xA0000.., below the user presets",
              st_sector(OBJ_CMP0, 0) == 0xA0000u && st_sector(OBJ_COUNT - 1u, 1) + 4096u <= 0xDC000u);
#undef MEL_SET
#undef MEL_IS
#undef MEL_PARK
    }
    if (argc > 1) {                                     /* a stock FUN10 in the slot (Felucca's autosave) */
        static project_store_t s;
        FILE *f = fopen(argv[1], "rb");
        ok = f && fread(&s, 1, sizeof s, f) == sizeof s && s.align == PROJ_MAGIC;
        if (f)
            fclose(f);
        ok = ok && !st_save(OBJ_AUTOSAVE, &s, sizeof s) && as_magic() == PROJ_MAGIC;
        power_on(1, settings);
        run(SH_SPLASH_MS + 64u);
        ok = ok && !(trk[0].step[0].n == 4u && trk[0].step[0].note[3] == 70u) && sh.mode != fm;
        run(120000u);
        probe("a stock FUN10 in the autosave: not restored as SQRL-1's, not written over unchanged",
              ok && !as.writes && !as_erases && as_magic() == PROJ_MAGIC);
    }
    printf(fails ? "autosave: %u FAILED\n" : "autosave: all passed\n", fails);
    return fails != 0;
}
