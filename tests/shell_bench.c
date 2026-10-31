/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* The shell build's heaviest sounds, measured on the host (instructions and ns per 44.1 kHz sample, one part):
 * CHORD's six notes with BASS on FM6, LIZARD's drawn unison with SHAPE, against the references the device already
 * plays. Instructions come from the kernel (Linux perf_event_open, user space only; macOS proc_pid_rusage), the
 * same on every run within ~1 %; ns are for information. Host x86 / arm counts are not the device's cycles: the
 * ratios to the references are what to read (the device's own: SYSTEM's DSP meter).
 * The gate (tests/shell_bench_baseline.txt): every row against its baseline, within 3 %. A host on the toolchain the
 * baseline was made with compares instructions; any other compares each row's ratio to its reference (a different
 * compiler or CPU moves every count, far less the ratios). BENCH_UPDATE=1 writes the baseline; no counter: skipped.
 *   cc -O2 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -o build/host/shell_bench tests/shell_bench.c -lm
 *   build/host/shell_bench [tests/shell_bench_baseline.txt] */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
static int pfd = -1;
static uint64_t instr_now(void)
{
    uint64_t v = 0;
    if (pfd < 0) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof a;
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        pfd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        if (pfd < 0)
            return 0;
    }
    if (read(pfd, &v, sizeof v) != (ssize_t)sizeof v)
        return 0;
    return v;
}
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
#else
static uint64_t instr_now(void) { return 0; }
#endif

static int32_t out[2 * CTL];
static uint32_t bench_voices;                           /* voices sounding at the end of the count */
void shell_block(uint32_t n) { (void)n; }               /* (seq.c's hook into shell.c: ALIVE, not measured here) */
void sh_move_isr(uint32_t adv) { (void)adv; }   /* (the shell's MOVE: none here) */
void sh_q_isr(uint32_t adv) { (void)adv; }      /* (nor its queued pattern switch) */
void shell_ne_key(uint32_t key, int down) { (void)key; (void)down; }   /* (the NOTE engines' keys: none here) */
void sh_take_note(track_t *t) { (void)t; }
void shell_loop_top(track_t *t) { (void)t; }

typedef struct {
    const char *name;
    uint32_t engine;
    const char *preset;
    uint8_t n, note[12];
    int16_t uni, warp, fold, sym, crush, down, mode;   /* mode < 0: the preset's */
    int16_t ins;                                        /* the INSERT's TYPE at MIX 127 (0: none) */
    uint32_t pf;                                        /* the FX layer's effects held (PF_BIT, perform.c; 0: none) */
    int8_t pk[4];                                       /* its macros: FILTER CRUSH THROW DEPTH / SHIMR */
    uint8_t sweep;                                      /* FILTER swept LP to HP and back once a second */
} bench_t;

static const bench_t B[] = {
    {"FM6 PAD, 8 notes (reference)", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    {"FM6 PAD, CHORD + BASS: 6 notes", ENGI_FM6, "PAD", 6, {36, 43, 48, 52, 55, 59}, 1, 0, 0, 0, 0, 0, -1},
    {"FM6 PAD, CHORD + BASS, FOLD 80", ENGI_FM6, "PAD", 6, {36, 43, 48, 52, 55, 59}, 1, 0, 80, 20, 0, 0, -1},
    {"FM6 PAD, CHORD + BASS, FOLD CRUSH DOWN", ENGI_FM6, "PAD", 6, {36, 43, 48, 52, 55, 59}, 1, 0, 80, 20, 60, 40, -1},
    {"LIZARD HQ DRAW, 8 notes (reference)", ENGI_LIZARD, "HQ DRAW", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    {"LIZARD SUPER SAW, 8 notes (reference)", ENGI_LIZARD, "SUPER SAW", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    {"LIZARD DRAW, 4 notes, UNI 5 WARP FOLD", ENGI_LIZARD, "HQ DRAW", 4, {48, 55, 60, 64}, 5, 60, 80, 0, 0, 0, 0},
    {"LIZARD DRAW, 8 notes, UNI 5 WARP FOLD", ENGI_LIZARD, "HQ DRAW", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 5, 60, 80, 0, 0, 0, 0},
    /* the speech engines and SHOCK, against FM6 PAD (a speech preset's MONO made POLY here: the chord is the load) */
#if FELUCCA_TALK
    {"TALK BENT, 8 notes", ENGI_TALK, "BENT", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
#endif
    {"SYLLABLE NIBS, 4-note chord", ENGI_SYLLABLE, "NIBS", 4, {72, 76, 79, 83}, 1, 0, 0, 0, 0, 0, -1},
    {"VODER DUDLEY 1939, all 10 bands", ENGI_VODER, "DUDLEY 1939", 10, {36, 37, 38, 39, 40, 41, 42, 43, 44, 45}, 1, 0, 0,
     0, 0, 0, -1},
    {"SHOCK BRASS, 8 notes", ENGI_SHOCK, "BRASS", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    {"ORGAN FULL ORGAN, 8 notes", ENGI_ORGAN, "FULL ORGAN", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    {"MOUTH HELLO, 4-note chord", ENGI_MOUTH, "HELLO", 4, {60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1},
    /* the INSERTs, each on FM6 PAD's 8 notes (fx.c IT_*: SOFT, CRUSH, PHASR, CHOR, ROTOR, RESON, VOWEL, FILTR, COMB) */
    {"FM6 PAD, 8 notes, INSERT SOFT", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 1},
    {"FM6 PAD, 8 notes, INSERT CRUSH", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 5},
    {"FM6 PAD, 8 notes, INSERT PHASR", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 6},
    {"FM6 PAD, 8 notes, INSERT CHOR", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 8},
    {"FM6 PAD, 8 notes, INSERT ROTOR", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 9},
    {"FM6 PAD, 8 notes, INSERT RESON", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 10},
    {"FM6 PAD, 8 notes, INSERT VOWEL", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 11},
    {"FM6 PAD, 8 notes, INSERT FILTR", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 12},
    {"FM6 PAD, 8 notes, INSERT COMB", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1, 13},
    /* the FX layer on the master (perform.c) over FM6 PAD's 8 notes, the macros up and FILTER sweeping */
    {"FM6 PAD, 8 notes, FX REPEAT 1/16, sweep", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0,
     -1, 0, PF_BIT(PF_R16), {-60, 60, 60, 0}, 1},
    {"FM6 PAD, 8 notes, FX OCT UP SHIMR, sweep", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0,
     -1, 0, PF_BIT(PF_OUP), {-60, 60, 60, 100}, 1},
    {"FM6 PAD, 8 notes, FX all held, sweep", ENGI_FM6, "PAD", 8, {48, 52, 55, 59, 60, 64, 67, 71}, 1, 0, 0, 0, 0, 0, -1,
     0, PF_BIT(PF_R16) | PF_BIT(PF_OUP) | PF_BIT(PF_FLG) | PF_BIT(PF_PHS) | PF_BIT(PF_RTHR) | PF_BIT(PF_ETHR),
     {-60, 60, 60, 100}, 1},
};

static uint32_t preset_of(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, name))
            return i;
    return 0;
}

static void bench(const bench_t *b, double *ipc, double *ns)
{
    uint32_t i, k, nb = FS / CTL;
    uint64_t i0 = 0, t0 = 0;
    track_t *t = &trk[0];
    host_tracks_init();
    for (i = 0; i < NPART; i++)                         /* (the last case's voices: gone, as at boot) */
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_preset(t, b->engine, preset_of(b->engine, b->preset));
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->p[P_AMODE] = 0;
    if (b->mode >= 0)
        t->p[P_E0] = b->mode;
    t->p[P_LZUNI] = b->uni;
    t->p[P_LZWARP] = b->warp;
    t->p[P_FOLD] = b->fold;
    t->p[P_SYM] = b->sym;
    t->p[P_CRUSH] = b->crush;
    t->p[P_DOWN] = b->down;
    if (b->ins) {
        t->p[P_ITYPE] = b->ins;
        t->p[P_IA] = t->p[P_IB] = t->p[P_IC] = 64;
        t->p[P_IMIX] = 127;
    }
    for (i = 0; i < b->n; i++)
        trk_note_on(t, b->note[i], 100);
    perf_held = perf_latched = 0;
    for (i = 0; i < PF_N; i++)                          /* (pressed in order: the last buffer effect plays) */
        if ((b->pf >> i) & 1u)
            perf_press(i, 1);
    for (i = 0; i < 4u; i++)
        perf_k[i] = b->pk[i];
    for (k = 0; k < nb / 2u + nb; k++) {                /* 0.5 s to settle, then 1 s counted */
        if (k == nb / 2u) {
            i0 = instr_now();
            t0 = now_ns();
        }
        if (b->sweep) {                                 /* (a triangle, -100 .. 100 .. -100 a second) */
            int32_t ph = (int32_t)(k % nb) * 400 / (int32_t)nb;
            perf_k[0] = (int8_t)(ph < 200 ? ph - 100 : 300 - ph);
        }
        mix_block(out, CTL);
    }
    perf_held = 0;
    for (i = 0; i < 4u; i++)
        perf_k[i] = 0;
    for (i = 0, k = 0; i < NVOICE; i++)
        k += t->v[i].active;
    bench_voices = k;
    *ns = (double)(now_ns() - t0) / (nb * CTL);
    *ipc = i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}

/* the shared buses alone (fx.c fx_buses: SQRL-1's tape echo, ensemble and pre-delay), a busy send on all three:
 * instructions a sample, for the ratios to a sound's */
static double bench_fx(int heads, int wow, int sat, int rev, int cmode, int pre)
{
    static int32_t ci[CTL], di[CTL], ri[CTL], wet[CTL];
    uint32_t i, k, nb = FS / CTL, ph = 0;
    uint64_t i0 = 0;
    song.g[G_TDHEADS] = (int16_t)heads; song.g[G_TDSAT] = (int16_t)sat; song.g[G_TDREV] = (int16_t)rev;
    song.g[G_DFDBK] = 90; song.g[G_RTYPE] = 2;
    fx_wow = (uint8_t)wow; fx_cmode = (uint8_t)cmode; fx_pre = (uint8_t)pre;
    for (k = 0; k < nb / 2u + nb; k++) {
        if (k == nb / 2u)
            i0 = instr_now();
        for (i = 0; i < CTL; i++) {
            ph += 0x0400000u;
            ci[i] = di[i] = ri[i] = osc_sine(ph) * 2;
        }
        fx_buses(ci, di, ri, wet, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}

#define NROW (sizeof B / sizeof B[0] + 3u)
static const char *row_name[NROW];
static double row_ins[NROW];
static uint32_t row_ref[NROW];                          /* the row whose count the ratio is to */
#if defined(__x86_64__)
#define BENCH_ARCH "x86_64"
#elif defined(__aarch64__)
#define BENCH_ARCH "aarch64"
#else
#define BENCH_ARCH "other"
#endif
#define BENCH_TOOLCHAIN BENCH_ARCH " " __VERSION__

/* the gate: 0 every row within 3 % of the baseline (or no counter, or written), 1 not */
static int gate(const char *path)
{
    char line[256], tc[160] = "";
    double base[NROW], bratio[NROW];
    uint32_t i, seen[NROW], bad = 0, same;
    FILE *f;
    if (!row_ins[0]) {
        printf("shell_bench: no instruction counter: the gate skipped\n");
        return 0;
    }
    if (getenv("BENCH_UPDATE")) {
        if (!(f = fopen(path, "w")))
            return 1;
        fprintf(f, "# shell_bench's baseline (tests/shell_bench.c): instructions a sample, ratio to the reference, row\n");
        fprintf(f, "# toolchain: %s\n", BENCH_TOOLCHAIN);
        for (i = 0; i < NROW; i++)
            fprintf(f, "%.0f\t%.4f\t%s\n", row_ins[i], row_ins[i] / row_ins[row_ref[i]], row_name[i]);
        fclose(f);
        printf("shell_bench: baseline %s written\n", path);
        return 0;
    }
    if (!(f = fopen(path, "r"))) {
        printf("shell_bench: no baseline %s (BENCH_UPDATE=1 writes it)\n", path);
        return 1;
    }
    memset(seen, 0, sizeof seen);
    while (fgets(line, sizeof line, f)) {
        double v, r;
        int at;
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "# toolchain: ", 13)) {
            snprintf(tc, sizeof tc, "%s", line + 13);
            continue;
        }
        if (line[0] == '#' || sscanf(line, "%lf\t%lf\t%n", &v, &r, &at) < 2)
            continue;
        for (i = 0; i < NROW; i++)
            if (!strcmp(line + at, row_name[i])) {
                base[i] = v;
                bratio[i] = r;
                seen[i] = 1;
            }
    }
    fclose(f);
    same = !strcmp(tc, BENCH_TOOLCHAIN);
    printf("shell_bench: the gate, %s against the baseline (made with %s), 3 %% a row\n",
           same ? "instructions" : "ratios to the reference", tc[0] ? tc : "?");
    for (i = 0; i < NROW; i++) {
        double now = same ? row_ins[i] : row_ins[i] / row_ins[row_ref[i]], was = same ? base[i] : bratio[i], d;
        if (!seen[i]) {
            printf("  %-42s not in the baseline (BENCH_UPDATE=1)  FAIL\n", row_name[i]);
            bad++;
            continue;
        }
        d = was ? (now / was - 1.0) * 100.0 : 0.0;
        if (d > 3.0 || d < -3.0) {
            printf("  %-42s %+.1f %%  FAIL\n", row_name[i], d);
            bad++;
        }
    }
    printf(bad ? "shell_bench: %u rows off their baseline\n" : "shell_bench: every row within 3 %% of its baseline\n", bad);
    return bad != 0;
}

int main(int argc, char **argv)
{
    uint32_t i;
    double ipc[sizeof B / sizeof B[0]], ns[sizeof B / sizeof B[0]];
    uint32_t nv[sizeof B / sizeof B[0]];
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        bench(&B[i], &ipc[i], &ns[i]);
        nv[i] = bench_voices;
    }
    if (!ipc[0])
        printf("shell_bench: no instruction counter here (perf_event_paranoid?): ns only\n");
    printf("shell_bench: one part, instructions / ns per sample, and x the reference (LIZARD: HQ DRAW; the rest: FM6 PAD)\n");
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        uint32_t ref = B[i].engine == ENGI_LIZARD ? 4u : 0u;   /* (LIZARD: its HQ DRAW; the rest: FM6 PAD) */
        printf("  %-42s %u voices %7.0f instr %7.1f ns  x%.2f\n", B[i].name, nv[i], ipc[i], ns[i],
               ipc[ref] ? ipc[i] / ipc[ref] : ns[i] / ns[ref]);
        row_name[i] = B[i].name;
        row_ins[i] = ipc[i];
        row_ref[i] = ref;
    }
    {
        double plain = bench_fx(0, 0, 0, 0, 0, 0), dflt = bench_fx(0, 10, 0, 0, 0, 0);
        double worst = bench_fx(6, 127, 127, 2, 1, 120);
        printf("shell_bench: the shared buses, instructions / sample (x FM6 PAD's 8 notes):\n");
        printf("  %-42s %7.0f instr  x%.2f\n", "1 head, CHOR, no WOW SAT REV PRE", plain, ipc[0] ? plain / ipc[0] : 0);
        printf("  %-42s %7.0f instr  x%.2f\n", "the defaults (1 head, WOW 10)", dflt, ipc[0] ? dflt / ipc[0] : 0);
        printf("  %-42s %7.0f instr  x%.2f\n", "ALL heads, WOW SAT 127, ALT, ENS, PRE 120", worst, ipc[0] ? worst / ipc[0] : 0);
        i = sizeof B / sizeof B[0];
        row_name[i] = "buses: 1 head, CHOR, no WOW SAT REV PRE"; row_ins[i] = plain; row_ref[i++] = 0;
        row_name[i] = "buses: the defaults"; row_ins[i] = dflt; row_ref[i++] = 0;
        row_name[i] = "buses: everything"; row_ins[i] = worst; row_ref[i] = 0;
    }
    return argc > 1 ? gate(argv[1]) : 0;
}
