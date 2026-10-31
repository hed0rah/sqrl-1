/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* Timing drift on SQRL-1's shell build: every timed subsystem over 64 bars at 97, 120 and 133 BPM, each event's sample
 * time against its grid computed in double from the tempo (a quarter 60 * FS / BPM samples, no rounding).
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/drift_test tests/drift_test.c -lm
 *   build/host/drift_test
 * 1. the sequencer's 1/16 steps (DIV 1/16), SWING 0 and 50 (the swung grid as core.h swing_step_len, in double):
 *    a step starts where seq_idx moves, at the block's start less seq_pos (samples into the step: exact);
 * 2. RATCH x2, x3, x4 on every step: the note-ons (a voice's age moves) against the step's even parts;
 * 3. the arp, AM_UP on a held note, at every ARP RATE (fx.c rate_samples, 0..14): note-ons against the grid from
 *    the first (playing: the grid from the transport's start, fx.c grid_at);
 * 4. CHORD PLAY's RPT 1..6 (seq.c rstrum_block) on a held root, transport playing: the strikes (a block where 3 or
 *    more voices start) against the grid from the transport's start;
 * 5. the click (seq.c click_tick): click_req set, at the block's start less clk_pos, against the quarters;
 * 6. REC + PLAY's count-in (rec_cin): its beats against the quarters, the sequencer's start against 4 of them, and
 *    exactly the first block at or past 4 x click_beat_len;
 * 7. the FX layer's 1/16 (perform.c pf.ph, an absolute phase; perf_begin run after the events as fx.c mix_block
 *    runs it): its boundaries against the 1/16 grid.
 * The bound: one events block (HALF_FRAMES) for a time read inside the block, two for one read at the block (the
 * firmware's block and the measurement's). The trend: the last event's error within the same bound of the first's.
 * The blocks run events_block alone (no track_render): every time here is decided there, and 64 bars x 3 tempos
 * x every run would take minutes rendered. The device runs its events every CTL (32) samples, the host test every
 * HALF_FRAMES (128): its quantisation is the coarser. Every grid is SQRL-1's Q16 tempo (fx.c beat_q16): before it, the
 * truncated beat ran every one of them 0.01 % fast, 12 to 16 ms early by bar 64, and the arp dropped a block a step */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static volatile uint32_t audio_max_us;
static uint32_t shed_count;
#include "../firmware/src/shell.c"

#define HF HALF_FRAMES
#define BARS 64u

static const int BPMS[3] = {97, 120, 133};
static uint32_t blk;                                /* blocks run since the origin */
static double now;                                  /* the start of the block just run, samples from the origin */

static void sframe(void)                            /* a UI frame as main.c runs it (tests/shell_test.c's) */
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    shell_input();
    shell_leds();
    shell_draw();
}
static void sframes(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) sframe(); }
static void sdown(uint32_t b) { fm1_in.buttons |= 1u << panel.btn[b]; host_pressed |= 1u << panel.btn[b]; }
static void sup(uint32_t b) { fm1_in.buttons &= ~(1u << panel.btn[b]); }
static void skey(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); sframe();
                               fm1_in.notes &= ~(1u << k); keyboard_block(); sframe(); }
static void srender(uint32_t blocks)                /* rendered blocks (an engine switch fades out in track_render) */
{
    static int32_t buf[2 * HALF_FRAMES];
    uint32_t b, p;
    for (b = 0; b < blocks; b++) {
        events_block(HALF_FRAMES);
        for (p = 0; p < NTRK; p++) {
            memset(buf, 0, sizeof buf);
            track_render(&trk[p], buf, HALF_FRAMES);
        }
    }
}
static uint32_t slot_of(const char *name)
{
    uint32_t m;
    for (m = 0; m < SH_NMODES && !str_eq(SH_MODES[m].name, name); m++)
        ;
    return m;
}
static uint32_t note_of(const char *name)
{
    uint32_t n;
    for (n = 0; n < SH_NNOTES && !str_eq(SH_NOTES[n].name, name); n++)
        ;
    return n;
}
static void sgo2(uint32_t slot, uint32_t note)      /* the wheel: a SOUND slot and a NOTE engine to the front */
{
    sdown(B_HOME); sframes(SH_TAP_MS + 100u);
    skey((uint32_t)sh_slot_key(slot));
    skey((uint32_t)sh_note_key(note));
    sup(B_HOME); sframe();
}

static void dblock(void)
{
    now = (double)blk++ * HF;
    events_block(HF);
    perf_begin(HF);                                 /* (fx.c mix_block: the FX layer's clock after the events) */
}
static void dorigin(void) { blk = 0; now = 0.0; }   /* (the next block is the run's first) */
static double quarter(int bpm) { return 60.0 * FS / bpm; }

/* a run's errors (measured - ideal), the k-th event against the k-th grid time */
typedef struct { double max, first, last; uint32_t n, want; } dr_t;
static void dr_add(dr_t *d, double err)
{
    if (!d->n)
        d->first = err;
    d->last = err;
    if (fabs(err) > d->max)
        d->max = fabs(err);
    d->n++;
}
static int dr_ok(const dr_t *d, double bound)
{
    return d->n == d->want && d->max <= bound && fabs(d->last - d->first) <= bound;
}
static void dr_show(const char *what, int bpm, const dr_t *d, double bound)
{
    printf("  (%3d BPM %-14s %5u/%5u events, max |err| %8.1f, first %7.1f, last %8.1f, bound %3.0f%s)\n", bpm, what,
           d->n, d->want, d->max, d->first, d->last, bound, dr_ok(d, bound) ? "" : ": drifts");
}

static uint32_t vlast[NTRK][NVOICE];
static void ages_keep(void)
{
    uint32_t p, q;
    for (p = 0; p < NTRK; p++)
        for (q = 0; q < NVOICE; q++)
            vlast[p][q] = trk[p].v[q].age;
}
static uint32_t ages_new(const track_t *t)          /* voices started since the last look */
{
    uint32_t q, n = 0, p = trk_index(t);
    for (q = 0; q < NVOICE; q++)
        if (t->v[q].age != vlast[p][q]) {
            n++;
            vlast[p][q] = t->v[q].age;
        }
    return n;
}

/* stopped, every track silent and empty, the tempo set, nothing armed or clicking */
static void drift_reset(int bpm)
{
    uint32_t i, k;
    transport_req = 2;
    dblock();
    rec_cin = 0;
    song.rec = 0;
    click_mode = CLICK_OFF;
    cin_bars = 0;
    click_req = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = 0;
    song.g[G_CLOCK] = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_AMODE] = 0;
        t->p[P_AHOLD] = 0;
        t->p[P_SSWING] = 0;
        t->p[P_SDIV] = 2;                           /* 1/16 */
        t->p[P_SLEN] = 16;
        t->p[P_SQNT] = 1;
        t->nheld = 0;
        t->arp_phys = 0;
    }
    dblock();
    dblock();
}

/* 1, 5, 7: steps, the click and the FX layer's 1/16, one run at swing sw (the click and FX at sw 0 only) */
static int run_steps(int bpm, int sw, dr_t *st, dr_t *ck, dr_t *fx)
{
    track_t *t = &trk[0];
    double q = quarter(bpm), b = q / 4.0, ev = b * (1.0 + sw / 250.0), od = b * (1.0 - sw / 250.0);
    double lim = 4.0 * BARS * q, x;
    uint32_t prev, ph;
    drift_reset(bpm);
    t->p[P_SSWING] = (int16_t)sw;
    click_mode = ck ? CLICK_ON : CLICK_OFF;
    memset(st, 0, sizeof *st);
    st->want = 16u * BARS;
    if (ck) {
        memset(ck, 0, sizeof *ck);
        memset(fx, 0, sizeof *fx);
        ck->want = 4u * BARS;
        fx->want = 16u * BARS;
    }
    dorigin();
    transport_req = 1;
    prev = 0xFFFFu;                                 /* (step 0 in the first block, whatever seq_idx was) */
    ph = 0;                                        /* (perf_start zeroes it in the first block) */
    while (now < lim + 2.0 * q) {
        uint32_t k;
        dblock();
        if (t->seq_idx != prev && st->n < st->want) {
            k = st->n;
            x = now - (double)t->seq_pos;
            dr_add(st, x - ((double)(k / 2u) * (ev + od) + ((k & 1u) ? ev : 0.0)));
        }
        prev = t->seq_idx;
        if (!ck)
            continue;
        if (click_req) {
            if (ck->n < ck->want)
                dr_add(ck, now - (double)clk_pos - (double)ck->n * q);
            click_req = 0;                          /* (click.c click_render takes it) */
        }
        if (fx->n < fx->want && pf_bnd(ph, HF) < HF)   /* (perform.c pf_bnd, perf_begin's: the 1/16 in this block) */
            dr_add(fx, now + (double)pf_bnd(ph, HF) - (double)fx->n * b);
        ph = pf.ph;
    }
    return 0;
}

/* 2: RATCH r on every step, its parts' note-ons */
static void run_ratchet(int bpm, uint32_t r, dr_t *d)
{
    track_t *t = &trk[0];
    double q = quarter(bpm), b = q / 4.0, lim = 4.0 * BARS * q;
    uint32_t k;
    drift_reset(bpm);
    t->p[P_VOICE] = V_POLY;
    for (k = 0; k < 16u; k++) {
        step_t *s = &t->step[k];
        s->time = ST_NOTE;
        s->n = 1;
        s->note[0] = 60;
        s->vel = 100;
        step_set_ratchet(s, r);
    }
    memset(d, 0, sizeof *d);
    d->want = 16u * BARS * r;
    ages_keep();
    dorigin();
    transport_req = 1;
    while (now < lim + q) {
        dblock();
        if (ages_new(t) && d->n < d->want) {
            k = d->n;
            dr_add(d, now - ((double)(k / r) * b + (double)(k % r) * b / r));
        }
    }
    for (k = 0; k < 16u; k++)
        step_clear(&t->step[k]);
}

/* 3: the arp at rate d (in quarters: f), AM_UP on one held note, against the grid from its first note */
static const double ARATE_Q[15] = {1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 2.0, 4.0, 8.0, 16.0,
                                   0.75, 0.375, 1.5, 2.0 / 3.0, 1.0 / 12.0};   /* fx.c rate_samples, params.c N_ARATE */
static void run_arp(int bpm, uint32_t rate, dr_t *d)
{
    track_t *t = &trk[0];
    double P = quarter(bpm) * ARATE_Q[rate], lim = 4.0 * BARS * quarter(bpm), x0 = 0.0;
    drift_reset(bpm);
    t->p[P_VOICE] = V_POLY;
    t->p[P_AMODE] = AM_UP;
    t->p[P_ARATE] = (int16_t)rate;
    t->p[P_APROB] = 127;
    t->p[P_ASWING] = 0;
    t->p[P_AOCT] = 1;
    t->p[P_AGATE] = 64;
    memset(d, 0, sizeof *d);
    for (d->want = 0; (double)d->want * P < lim; d->want++)
        ;
    dblock();                                       /* (armp follows AMODE: the arp is on before the note) */
    ages_keep();
    dorigin();
    input_on(t, 60, 100);
    while (d->n < d->want && now < 1.25 * lim + 2.0 * P) {   /* (a slow arp: until its notes are in, or a quarter more) */
        dblock();
        if (ages_new(t)) {
            if (!d->n)
                x0 = now;
            dr_add(d, now - x0 - (double)d->n * P);
        }
    }
    input_off(t, 60);
    t->p[P_AMODE] = 0;
    dblock();
}

/* 4: CHORD PLAY's RPT v on a held root, the transport playing (shell_test's CHORD RPT: its set-up) */
static const double RPT_Q[7] = {0.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.5, 0.75};   /* seq.c RPT_RATE, shell.c N_SP_RPT */
static void run_rpt(int bpm, uint32_t v, dr_t *d)
{
    track_t *t;
    double R = quarter(bpm) * RPT_Q[v], lim = 4.0 * BARS * quarter(bpm);
    uint32_t k;
    drift_reset(bpm);
    t = sh_trk();
    t->p[P_VOICE] = V_POLY;
    sh_sp[SP_STRM - SP_FIRST] = 0;
    sh_sp[SP_RPT - SP_FIRST] = (int16_t)v;
    sframe();
    memset(d, 0, sizeof *d);
    for (k = 1; (double)k * R < lim; k++)
        ;
    d->want = k - 1u;
    fm1_in.notes |= 1u << CB_ROOT0;
    keyboard_block();                               /* (struck at the press, stopped) */
    ages_keep();
    dorigin();
    transport_req = 1;
    while (now < lim + R) {
        dblock();
        if (ages_new(t) >= 3u && d->n < d->want)
            dr_add(d, now - (double)(d->n + 1u) * R);
    }
    fm1_in.notes &= ~(1u << CB_ROOT0);
    keyboard_block();
    sh_sp[SP_RPT - SP_FIRST] = 0;
    sframe();
    dblock();
}

/* 6: REC + PLAY from stop (rec_cin): a bar's count-in, then step 1. *at: the start's block */
static void run_cin(int bpm, dr_t *d, double *start, uint32_t *at)
{
    double q = quarter(bpm);
    drift_reset(bpm);
    song.rec = 1u;
    rec_cin = 1;
    memset(d, 0, sizeof *d);
    d->want = 4u;
    *at = 0;
    *start = -1.0;
    dorigin();
    transport_req = 1;
    while (now < 6.0 * q) {
        dblock();
        if (!song.playing && click_req && d->n < d->want)
            dr_add(d, now - (double)d->n * q);
        click_req = 0;
        if (song.playing && *start < 0.0) {
            *start = now;
            *at = blk - 1u;
        }
    }
    transport_req = 2;
    dblock();
    song.rec = 0;
    rec_cin = 0;
}

int main(void)
{
    static const char *const RN[15] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/2", "1/1", "2BAR", "4BAR",
                                       "1/8.", "1/16.", "1/4.", "1/4T", "1/32T"};
    static const char *const VN[7] = {"OFF", "1/4", "1/8", "1/8T", "1/16", "1/4.", "1/8."};
    int bad = 0, ok;
    uint32_t i, r;
    char what[96], name[32];
    ui_power_on();
    memset(&sh, 0, sizeof sh);
    memset(sh_b, 0, sizeof sh_b);
    memset(sh_snap, 0, sizeof sh_snap);
    for (int pp_ = 0; pp_ < NPART; pp_++) sh_partpair[pp_] = SH_NOPAIR;
    memset(sh_snapx, 0, sizeof sh_snapx);
    kb_mask = 0;
    sframe();
    while (sh.splash_t)
        sframe();
    for (i = 0; i < 3u; i++) {
        int bpm = BPMS[i];
        dr_t st, ck, fx, d;
        double start;
        uint32_t at, bl;

        run_steps(bpm, 0, &st, &ck, &fx);
        dr_show("steps SWING 0", bpm, &st, HF);
        dr_show("click", bpm, &ck, HF);
        dr_show("FX 1/16", bpm, &fx, HF);
        snprintf(what, sizeof what, "drift %d BPM: 1/16 steps, SWING 0, on the grid over 64 bars", bpm);
        bad += check(what, dr_ok(&st, HF));
        snprintf(what, sizeof what, "drift %d BPM: the click on the quarters over 64 bars", bpm);
        bad += check(what, dr_ok(&ck, HF));
        snprintf(what, sizeof what, "drift %d BPM: the FX layer's 1/16 (pf.ph) on the grid over 64 bars", bpm);
        bad += check(what, dr_ok(&fx, HF));

        run_steps(bpm, 50, &st, 0, 0);
        dr_show("steps SWING 50", bpm, &st, HF);
        snprintf(what, sizeof what, "drift %d BPM: 1/16 steps, SWING 50, on the swung grid over 64 bars", bpm);
        bad += check(what, dr_ok(&st, HF));

        for (r = 2; r <= 4u; r++) {
            run_ratchet(bpm, r, &d);
            snprintf(name, sizeof name, "RATCH x%u", r);
            dr_show(name, bpm, &d, 2 * HF);
            snprintf(what, sizeof what, "drift %d BPM: RATCH x%u's hits on the step's parts over 64 bars", bpm, r);
            bad += check(what, dr_ok(&d, 2 * HF));
        }

        ok = 1;
        for (r = 0; r < 15u; r++) {
            run_arp(bpm, r, &d);
            snprintf(name, sizeof name, "ARP %s", RN[r]);
            dr_show(name, bpm, &d, 2 * HF);
            ok = ok && dr_ok(&d, 2 * HF);
        }
        snprintf(what, sizeof what, "drift %d BPM: the arp's notes, every RATE, on the grid over 64 bars", bpm);
        bad += check(what, ok);

        run_cin(bpm, &d, &start, &at);
        bl = (4u * click_beat_len() + HF - 1u) / HF;   /* (the first block at or past four of its beats) */
        dr_show("count-in", bpm, &d, 2 * HF);
        printf("  (%3d BPM count-in: step 1 in block %u at %.0f, 4 beats %.1f, its own 4 x %u in block %u)\n", bpm,
               at, start, 4.0 * quarter(bpm), click_beat_len(), bl);
        snprintf(what, sizeof what, "drift %d BPM: REC + PLAY's count-in, then step 1 exactly 4 beats on", bpm);
        bad += check(what, dr_ok(&d, 2 * HF) && at == bl && fabs(start - 4.0 * quarter(bpm)) <= 2 * HF);
    }
    sgo2(slot_of("LIBRARY"), note_of("CHORD"));     /* CHORD PLAY on the part (last: the arp's part is the stock one) */
    srender(40);
    for (i = 0; i < 3u; i++) {
        int bpm = BPMS[i];
        dr_t d;

        ok = 1;
        for (r = 1; r <= 6u; r++) {
            run_rpt(bpm, r, &d);
            snprintf(name, sizeof name, "RPT %s", VN[r]);
            dr_show(name, bpm, &d, 2 * HF);
            ok = ok && dr_ok(&d, 2 * HF);
        }
        snprintf(what, sizeof what, "drift %d BPM: CHORD PLAY's RPT, every value, on the grid over 64 bars", bpm);
        bad += check(what, ok);
    }
    if (bad)
        printf("drift: %d FAILED\n", bad);
    else
        printf("drift: all passed\n");
    return bad ? 1 : 0;
}
