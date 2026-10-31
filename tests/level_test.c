/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* The engines' levels against the limiter, SQRL-1 build (fx.c ENG_TRIM, PRE_TRIM; eng_mouth.c MOUTH_GAIN).
 *   build/host/level_test                           (run_tests.sh builds and runs it)
 *
 * Each factory preset plays a C4 at velocity 127 for 2 s (VODER: bands 1 4 7 held, its keys being bands), POLY, with
 * its own sends; its peak before the limiter is measured in dB against LIM_T. Each preset runs in its own fork()ed
 * child, as regress.c's renders do: a voice still releasing or an echo still ringing from the one before would be
 * measured with it. DRUM plays a kick, snare and closed hat at once (36 38 42), the loudest step a grid plays, and is
 * held to the per-preset rule only. Checks, each engine's median over its presets (LOOP not measured):
 *   an engine trimmed (or MOUTH, made up)   within 1.5 dB of LIM_T
 *   an engine left at unity                 not over LIM_T by more than 1.5 dB
 *   any preset                              not over LIM_T by more than 5.5 dB (the presets' trims: 4 dB, + 1.5)
 * A failure means an engine's or a preset's level moved: measure again and set the trims again. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <unistd.h>
#include <sys/wait.h>
void shell_block(uint32_t n) { (void)n; }
void sh_move_isr(uint32_t adv) { (void)adv; }   /* (the shell's MOVE: none here) */
void sh_q_isr(uint32_t adv) { (void)adv; }      /* (nor its queued pattern switch) */
void shell_ne_key(uint32_t key, int down) { (void)key; (void)down; }
void sh_take_note(track_t *t) { (void)t; }
void shell_loop_top(track_t *t) { (void)t; }

static double peak_db(uint32_t e, uint32_t pi)          /* in a child of its own */
{
    static int32_t out[2 * CTL];
    static const uint8_t VB[3] = {37, 40, 43}, DR[3] = {36, 38, 42};
    uint32_t b, i, k, nk = e == ENGI_VODER || e == ENGI_DRUM ? 3u : 1u;
    int32_t mg;
    double pk = 0.0;
    track_t *t = &trk[0];
    host_tracks_init();
    mg = (int32_t)song.master_q12;
    host_preset(t, e, pi);
    t->p[P_AMODE] = 0;
    t->p[P_VOICE] = V_POLY;
    for (k = 0; k < nk; k++)
        input_on(t, e == ENGI_DRUM ? DR[k] : nk > 1u ? VB[k] : 60u, 127);
    for (b = 0; b < 2u * FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            double l = fabs((double)((((mix_l[i] + wet[i]) >> 2) * mg) >> 10));
            double r = fabs((double)((((mix_r[i] + wet[i]) >> 2) * mg) >> 10));
            pk = l > pk ? l : pk;
            pk = r > pk ? r : pk;
        }
    }
    return 20.0 * log10(pk / LIM_T + 1e-9);
}

static double measure(uint32_t e, uint32_t pi)
{
    int fd[2];
    double v = -999.0;
    pid_t p;
    if (pipe(fd))
        return v;
    p = fork();
    if (!p) {
        v = peak_db(e, pi);
        if (write(fd[1], &v, sizeof v) != (ssize_t)sizeof v)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &v, sizeof v) != (ssize_t)sizeof v)
        v = -999.0;
    close(fd[0]);
    waitpid(p, 0, 0);
    return v;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(void)
{
    uint32_t e, pi, bad = 0;
    for (e = 0; e < NENGINES; e++) {
        double v[64], med, top = -999.0;
        uint32_t n = ENGINES[e]->npresets, made = ENG_TRIM[e] != 16384u || e == ENGI_MOUTH, ok;
        if (!eng_ok(e) || e == ENGI_LOOP || !n)
            continue;
        for (pi = 0; pi < n && pi < 64u; pi++) {
            v[pi] = measure(e, pi);
            if (v[pi] > 5.5) {
                printf("  %s %s: %+.1f dB over LIM_T\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name, v[pi]);
                bad++;
            }
            top = v[pi] > top ? v[pi] : top;
        }
        qsort(v, pi, sizeof v[0], cmp);
        med = pi & 1u ? v[pi / 2u] : (v[pi / 2u - 1u] + v[pi / 2u]) / 2.0;
        ok = e == ENGI_DRUM ? top <= 5.5 : made ? fabs(med) <= 1.5 : med <= 1.5;
        printf("level: %-9s median %+5.1f dB, top %+5.1f (%s)%*s %s\n", ENGINES[e]->name, med, top,
               e == ENGI_DRUM ? "three hits, under +5.5" : made ? "trimmed to LIM_T" : "unity, under it", 0, "",
               ok ? "ok" : "FAIL");
        bad += !ok;
    }
    printf(bad ? "level: %u FAILED\n" : "level: all passed\n", bad);
    return bad != 0;
}
