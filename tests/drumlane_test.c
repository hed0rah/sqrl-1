/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* SQRL-1's sound per drum (eng_drum.c drum_lane_d): a lane's DECY lengthens its tail and TUNE moves its pitch, the
 * other lanes as designed; all at 0, the kit is bit for bit as before.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/drumlane_test tests/drumlane_test.c -lm */
#define main hostsim_main
#include "hostsim.c"
#undef main
void shell_block(uint32_t n) { (void)n; }
void sh_move_isr(uint32_t adv) { (void)adv; }   /* (the shell's MOVE: none here) */
void sh_q_isr(uint32_t adv) { (void)adv; }      /* (nor its queued pattern switch) */
void shell_ne_key(uint32_t key, int down) { (void)key; (void)down; }
void sh_take_note(track_t *t) { (void)t; }
void shell_loop_top(track_t *t) { (void)t; }

static double tail_e, early_e;
static uint32_t crossings;
static uint64_t hash;
static void hit(uint32_t note)                           /* one hit, 0.6 s: energy after 150 ms, zero crossings, a hash */
{
    static int32_t out[2 * CTL];
    uint32_t b, i, n = 0;
    int32_t last = 0;
    track_t *t = &trk[0];
    host_tracks_init();
    host_preset(t, ENGI_DRUM, 0);
    input_on(t, note, 127);
    tail_e = early_e = 0.0;
    crossings = 0;
    hash = 1469598103934665603ull;
    for (b = 0; b < (FS * 3u / 5u) / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++, n++) {
            int32_t x = mix_l[i];
            if (n > FS * 3u / 20u)
                tail_e += (double)x * x;
            else
                early_e += (double)x * x;
            if ((x > 0) != (last > 0) && n < FS / 10u)
                crossings++;
            last = x;
            hash = (hash ^ (uint32_t)x) * 1099511628211ull;
        }
    }
}
static int check(const char *what, int ok)
{
    printf("drumlane: %-76s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
int main(void)
{
    double tail0;
    uint32_t cr0;
    uint64_t h0;
    int bad = 0;
    memset(drum_lane_d, 0, sizeof drum_lane_d);
    hit(38);                                             /* the snare, as designed */
    tail0 = tail_e; cr0 = crossings; h0 = hash;
    drum_lane_d[0][DRL_DECY][DV_SNARE] = 40;             /* its DECY up */
    hit(38);
    bad += check("a lane's DECY up lengthens its tail", tail_e > tail0 * 1.5);
    drum_lane_d[0][DRL_DECY][DV_SNARE] = 0;
    drum_lane_d[0][DRL_TUNE][DV_KICK] = 50;              /* another lane's TUNE: the snare untouched */
    hit(38);
    bad += check("another lane's setting leaves the snare bit for bit", hash == h0);
    hit(36);
    {
        uint32_t up = crossings;
        drum_lane_d[0][DRL_TUNE][DV_KICK] = 0;
        hit(36);
        bad += check("a lane's TUNE up raises its pitch (more zero crossings)", up > crossings);
    }
    drum_lane_d[0][DRL_TUNE][DV_SNARE] = 0;
    hit(38);
    bad += check("all at 0: the kit as designed, bit for bit", hash == h0 && crossings == cr0);
    printf(bad ? "drumlane: %d FAILED\n" : "drumlane: all passed\n", bad);
    return bad != 0;
}
