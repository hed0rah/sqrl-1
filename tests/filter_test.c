/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * (databurn's test_filter.c, built against the firmware's copy)
 *   cc -std=c99 -O2 -Wall -Wextra -Wdouble-promotion -Ifirmware/src firmware/src/filter.c tests/filter_test.c -lm */
/* filter's checks: the FILTER's LP, BP, HP and NOTCH responses against the analog prototype fl_mag draws (passband,
 * the peak at the cutoff as the Q, 12 dB an octave), MORPH's halfway points, fl_mag against the measured response,
 * KTRK and ENV moving the cutoff by what they say, DRIVE holding the level while it adds harmonics, the top of RES
 * and DRIVE on full-scale noise under a cutoff jumping every block staying finite and under the clamp, exact
 * silence after the input stops (FILTER and COMB, at the top of RES / FDBK), the COMB ringing every harmonic with
 * positive feedback and only the odd ones with negative, at the pitch TUNE says, and its retune gliding without a
 * step; then the cost and the size.
 *   cc -std=c99 -O2 -Wall -Wextra -Wdouble-promotion -I.. ../filter.c test_filter.c -lm -o bin/test_filter  (from tests/) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FS 44100.0
#define B 32u
#define N (44100u * 4u)
static fl_t fl;
static int16_t line[256];
static int fails;
static char what[300];
static int32_t X[N];                                    /* (int16 values, the hosts' half scale) */
static int32_t Y[N];
static uint32_t lcg = 7u;
static uint32_t rnd(void)                               /* (our own: rand()'s range differs by platform) */
{
    lcg = lcg * 1664525u + 1013904223u;
    return lcg >> 8;
}
static int16_t rnd16(void) { return (int16_t)(rnd() >> 8); }   /* full-range, symmetric */

static void check(int ok, const char *w)
{
    printf("%s  %s\n", ok ? "pass" : "FAIL", w);
    fails += !ok;
}
static fl_param_t P(int cut, int res, int morph, int drive, int ktrk, int env)
{
    fl_param_t p;
    p.cut = (uint8_t)cut, p.res = (uint8_t)res, p.morph = (uint8_t)morph;
    p.drive = (uint8_t)drive, p.ktrk = (uint8_t)ktrk, p.env = (uint8_t)env;
    return p;
}
static double cut_hz(int cut) { return 440.0 * pow(2.0, (16.0 + cut * 118.0 / 127.0 - 69.0) / 12.0); }
static void run(const fl_param_t *p, uint32_t n, int comb)
{
    uint32_t o;
    for (o = 0; o < n; o += B) {
        if (comb)
            fl_comb(&fl, p, line, 256, X + o, Y + o, B);
        else
            fl_filter(&fl, p, X + o, Y + o, B);
    }
}
static void sine(double hz, double amp, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        X[i] = (int16_t)lrint(amp * 32767.0 * sin(2.0 * M_PI * hz * i / FS));
}
/* the gain of a sine through p, dB, measured on the last half (the transient gone) */
static double gain_db(const fl_param_t *p, double hz, int comb)
{
    uint32_t i, n = N / 2;
    double a = 0, b = 0;
    fl_init(&fl);
    memset(line, 0, sizeof line);
    sine(hz, 0.25, n);
    run(p, n, comb);
    for (i = n / 2; i < n; i++) {
        a += (double)X[i] * X[i];
        b += (double)Y[i] * Y[i];
    }
    return 10.0 * log10(b / a);
}
/* the energy at hz in Y[from .. from + n), Goertzel */
static double goertzel(uint32_t from, uint32_t n, double hz)
{
    double c = 2.0 * cos(2.0 * M_PI * hz / FS), s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double s = Y[from + i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main(void)
{
    uint32_t i, j;
    double g, fc;

    /* LP: unity below, the Q at the cutoff, 12 dB an octave above */
    fc = cut_hz(80);
    g = gain_db(&(fl_param_t){80, 0, 0, 0, 0, 64}, fc / 8.0, 0);
    snprintf(what, sizeof what, "LP passband (fc %.0f Hz, an octave x3 below): %.2f dB, within 0.3", fc, g);
    check(fabs(g) < 0.3, what);
    g = gain_db(&(fl_param_t){80, 0, 0, 0, 0, 64}, fc, 0);
    snprintf(what, sizeof what, "LP at the cutoff, RES 0 (Q 0.5): %.2f dB, -6.0 +-0.5", g);
    check(fabs(g + 6.02) < 0.5, what);
    g = gain_db(&(fl_param_t){80, 64, 0, 0, 0, 64}, fc, 0);
    snprintf(what, sizeof what, "LP at the cutoff, RES 64 (Q %.2f): %.2f dB, 20 log Q +-0.7", 0.5 * pow(2.0, 8.0 * 64 / 127), g);
    check(fabs(g - 20.0 * log10(0.5 * pow(2.0, 8.0 * 64 / 127))) < 0.7, what);
    g = gain_db(&(fl_param_t){60, 0, 0, 0, 0, 64}, cut_hz(60) * 4.0, 0) - gain_db(&(fl_param_t){60, 0, 0, 0, 0, 64}, cut_hz(60) * 8.0, 0);
    snprintf(what, sizeof what, "LP 12 dB an octave above: %.2f dB, 12 +-1", g);
    check(fabs(g - 12.0) < 1.0, what);
    /* HP, BP, NOTCH */
    g = gain_db(&(fl_param_t){60, 0, 85, 0, 0, 64}, cut_hz(60) * 8.0, 0);
    snprintf(what, sizeof what, "HP passband: %.2f dB, within 0.3", g);
    check(fabs(g) < 0.3, what);
    g = gain_db(&(fl_param_t){60, 64, 42, 0, 0, 64}, cut_hz(60), 0);
    snprintf(what, sizeof what, "BP peak at the cutoff, RES 64: %.2f dB, 20 log Q +-0.7", g);
    check(fabs(g - 20.0 * log10(0.5 * pow(2.0, 8.0 * 64 / 127))) < 0.7, what);
    g = gain_db(&(fl_param_t){60, 0, 127, 0, 0, 64}, cut_hz(60), 0);
    snprintf(what, sizeof what, "NOTCH at the cutoff: %.1f dB, under -30", g);
    check(g < -30.0, what);
    /* fl_mag against the measure, a sweep of each mode */
    {
        double worst = 0;
        int ms[4] = {0, 42, 85, 21}, m, r;
        for (m = 0; m < 4; m++)
            for (r = 0; r <= 96; r += 48)
                for (j = 0; j < 7; j++) {
                    fl_param_t p = P(70, r, ms[m], 0, 0, 64);
                    double hz = 100.0 * pow(2.0, j * 0.85), meas = gain_db(&p, hz, 0), pred;
                    fl_init(&fl);
                    pred = 20.0 * log10((double)fl_mag(&fl, &p, (float)hz) + 1e-9);
                    if (pred > -40.0 && fabs(meas - pred) > worst)
                        worst = fabs(meas - pred);
                }
        snprintf(what, sizeof what, "fl_mag against the measured response, 100 Hz .. 4.5 kHz, four modes, three RES: worst %.2f dB, "
                 "under 1", worst);
        check(worst < 1.0, what);
    }
    /* KTRK and ENV */
    fl_init(&fl);
    {
        fl_param_t p = P(60, 0, 0, 0, 127, 64);
        double a = fl_freq(&fl, &p), b2;
        fl_note(&fl, 72 * 16);
        b2 = fl_freq(&fl, &p);
        snprintf(what, sizeof what, "KTRK 127: a note an octave up doubles the cutoff (x %.4f)", b2 / a);
        check(fabs(b2 / a - 2.0) < 0.002, what);
        p.ktrk = 0;
        b2 = fl_freq(&fl, &p);
        snprintf(what, sizeof what, "KTRK 0: the note moves nothing (x %.4f)", b2 / a);
        check(fabs(b2 / a - 1.0) < 1e-6, what);
    }
    {
        fl_param_t p = P(40, 0, 0, 0, 0, 127);
        double a, b2;
        fl_init(&fl);
        a = fl_freq(&fl, &p);
        sine(200.0, 1.0, 4410);
        run(&p, 4410, 0);
        b2 = fl_freq(&fl, &p);
        snprintf(what, sizeof what, "ENV 127 on a full-scale input: the cutoff up %.1f semitones, 36 +-1", 12.0 * log2(b2 / a));
        check(fabs(12.0 * log2(b2 / a) - 36.0) < 1.0, what);
        p.env = 0;
        b2 = fl_freq(&fl, &p);
        snprintf(what, sizeof what, "ENV 0: down %.1f semitones, 36 +-1", -12.0 * log2(b2 / a));
        check(fabs(12.0 * log2(b2 / a) + 36.0) < 1.0, what);
        p.env = 64;
        b2 = fl_freq(&fl, &p);
        check(fabs(b2 / a - 1.0) < 1e-6, "ENV 64: no move");
    }
    /* DRIVE: the level held, harmonics added */
    {
        fl_param_t p0 = P(127, 0, 0, 0, 0, 64), p1 = P(127, 0, 0, 127, 0, 64);
        double l0 = gain_db(&p0, 220.0, 0), l1 = gain_db(&p1, 220.0, 0), h0, h1;
        snprintf(what, sizeof what, "DRIVE 127 holds the level of a quarter-scale sine: %.1f dB against %.1f, within 3", l1, l0);
        check(fabs(l1 - l0) < 3.0, what);
        gain_db(&p0, 220.0, 0);
        h0 = goertzel(N / 4, 8820, 660.0) / goertzel(N / 4, 8820, 220.0);
        gain_db(&p1, 220.0, 0);
        h1 = goertzel(N / 4, 8820, 660.0) / goertzel(N / 4, 8820, 220.0);
        snprintf(what, sizeof what, "DRIVE 127 adds the 3rd harmonic: %.1f dB against %.1f, up at least 20 dB",
                 10.0 * log10(h1 + 1e-30), 10.0 * log10(h0 + 1e-30));
        check(10.0 * log10((h1 + 1e-30) / (h0 + 1e-30)) > 20.0, what);
    }
    /* stability: full-scale noise, the top of RES and DRIVE, the cutoff jumping every block */
    {
        int32_t mx = 0;
        int finite = 1;
        fl_init(&fl);
        for (i = 0; i < N; i++)
            X[i] = rnd16();
        for (j = 0; j < N; j += B) {
            fl_param_t p = P((int)(rnd() % 128u), 127, (int)(rnd() % 128u), 127, 0, (int)(rnd() % 128u));
            fl_filter(&fl, &p, X + j, Y + j, B);
        }
        for (i = 0; i < N; i++) {
            int32_t a = Y[i] < 0 ? -Y[i] : Y[i];
            mx = a > mx ? a : mx;
        }
        finite = fl.ic1 == fl.ic1 && fl.ic2 == fl.ic2;
        snprintf(what, sizeof what, "full-scale noise, RES and DRIVE 127, CUT and MORPH jumping each block: finite, peak %.1fx, under 8x",
                 mx / 32768.0);
        check(finite && mx < 8 * 32768, what);
    }
    /* silence after the input stops */
    {
        int c;
        for (c = 0; c < 2; c++) {
            fl_param_t p = c ? P(57, 127, 0, 64, 0, 64) : P(80, 127, 0, 64, 0, 64);
            uint32_t last = 0;
            fl_init(&fl);
            memset(line, 0, sizeof line);
            sine(330.0, 0.5, 4410);
            memset(X + 4410, 0, (N - 4410) * sizeof X[0]);
            run(&p, N, c);
            for (i = 0; i < N; i++)
                if (Y[i])
                    last = i;
            snprintf(what, sizeof what, "%s at the top of %s: exact silence %.2f s after the input stops, within 4 s",
                     c ? "COMB" : "FILTER", c ? "FDBK" : "RES", (last - 4410) / FS);
            check(last < N - 1u, what);
        }
    }
    /* COMB: harmonics, from its impulse response (deterministic: a noise drive's single-bin energies scatter by dB) */
    {
        double e1, e2, e3;
        fl_param_t p = P(0, 120, 0, 0, 0, 64);
        double hz;
        int tune = (int)lrint((69.0 - 16.0) * 127.0 / 118.0);   /* A4 */
        p.cut = (uint8_t)tune;
        fl_init(&fl);
        hz = fl_freq(&fl, &p);
        memset(line, 0, sizeof line);
        memset(X, 0, sizeof X);
        X[0] = 16384;
        run(&p, 16384, 1);
        e1 = goertzel(0, 16384, hz);
        e2 = goertzel(0, 16384, 2.0 * hz);
        e3 = goertzel(0, 16384, 1.5 * hz);
        snprintf(what, sizeof what, "COMB +FDBK at %.1f Hz: the 2nd harmonic within 3 dB of the 1st (%.1f), the half-way under both by 15 (%.1f)",
                 hz, 10 * log10(e2 / e1), 10 * log10(e3 / e1));
        check(fabs(10 * log10(e2 / e1)) < 3.0 && 10 * log10(e3 / e1) < -15.0, what);
        p.res = 8;
        fl_init(&fl);
        memset(line, 0, sizeof line);
        run(&p, 16384, 1);
        e1 = goertzel(0, 16384, hz);
        e2 = goertzel(0, 16384, 2.0 * hz);
        e3 = goertzel(0, 16384, 3.0 * hz);
        snprintf(what, sizeof what, "COMB -FDBK at the same pitch: the 3rd harmonic within 3 dB of the 1st (%.1f), the 2nd under by 15 (%.1f)",
                 10 * log10(e3 / e1), 10 * log10(e2 / e1));
        check(fabs(10 * log10(e3 / e1)) < 3.0 && 10 * log10(e2 / e1) < -15.0, what);
    }
    /* COMB: a retune glides */
    {
        fl_param_t p = P(70, 110, 0, 0, 127, 64);
        int32_t worst = 0, typ = 0;
        fl_init(&fl);
        memset(line, 0, sizeof line);
        sine(500.0, 0.25, N);
        for (j = 0; j < N / 2; j += B) {
            if (j == N / 4)
                fl_note(&fl, 67 * 16);                  /* a fifth up mid-ring */
            fl_comb(&fl, &p, line, 256, X + j, Y + j, B);
        }
        for (i = N / 8; i < N / 2 - 1; i++) {
            int32_t d = Y[i + 1] - Y[i];
            d = d < 0 ? -d : d;
            if (i < N / 4 - 64)
                typ = d > typ ? d : typ;
            else if (i < N / 4 + 64)
                worst = d > worst ? d : worst;
        }
        snprintf(what, sizeof what, "COMB retuned a fifth mid-ring: the largest step %d against %d before, within 2x", worst, typ);
        check(worst < 2 * typ + 64, what);
    }
    /* cost and size */
    {
        fl_param_t p = P(70, 100, 30, 64, 64, 90);
        clock_t t0;
        double ns;
        sine(220.0, 0.5, N);
        fl_init(&fl);
        t0 = clock();
        for (j = 0; j < 4; j++)
            run(&p, N, 0);
        ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (4.0 * N);
        printf("cost   FILTER %.1f ns a sample on this PC", ns);
        memset(line, 0, sizeof line);
        t0 = clock();
        for (j = 0; j < 4; j++)
            run(&p, N, 1);
        ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (4.0 * N);
        printf(", COMB %.1f ns; state %u bytes (+ the host's line for COMB)\n", ns, (unsigned)sizeof(fl_t));
    }
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
