/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * filter: see filter.h */
#include "filter.h"

#define FL_FS 44100.0f
#define FL_FMAX (0.42f * FL_FS)             /* the highest cutoff: 18.5 kHz, where tan's warp is still gentle */
#define FL_FMIN 16.0f
#define FL_H 2.0f                           /* the input saturator's headroom: DRIVE 0 is nearly clean at full scale */
#define FL_WMAX (8.0f * 32768.0f - 1.0f)    /* the wet's clamp */
#define FL_REL (1.0f / (0.060f * FL_FS))    /* the follower's release a sample, 60 ms */
#define FL_QUIET (1.0f / 131072.0f)         /* under the int16 output's half bit: a state this small is set to 0 */
#define FL_LQ 8192.0f                       /* COMB: the line's scale (int16 holds +-4) */

static inline float fl_abs(float x) { return x < 0.0f ? -x : x; }
static inline float fl_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* tanh's [3/2] Pade, clamped at +-3 where it reaches +-1: smooth, odd, monotonic */
static inline float fl_sat(float x)
{
    float x2;
    x = fl_clamp(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* the band state's bound: linear to +-4 (+12 dB over the host's half scale), then a soft knee into +-8, so RES is the
 * linear filter's Q at any usual level and the top of RES rings near self-oscillation without running away */
static inline float fl_bound(float x)
{
    float a = fl_abs(x);
    if (a <= 4.0f)
        return x;
    a = 4.0f + 4.0f * fl_sat((a - 4.0f) * 0.25f);
    return x < 0.0f ? -a : a;
}

/* 2^x, x in -64 .. 64: the exponent from the integer part, a quartic for the fraction (within 4e-6) */
static float fl_exp2(float x)
{
    union {
        float f;
        int32_t i;
    } u;
    int32_t i = (int32_t)x;
    float f;
    if ((float)i > x)
        i--;
    f = x - (float)i;
    u.f = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    u.i += i * (1 << 23);
    return u.f;
}

/* tan x for x in 0 .. 1.4 (pi 0.45): a [5/4] Pade, within 0.1 % to 1.32 (the highest cutoff) */
static float fl_tan(float x)
{
    float x2 = x * x;
    return x * (945.0f - 105.0f * x2 + x2 * x2) / (945.0f - 420.0f * x2 + 15.0f * x2 * x2);
}

/* 1 / sqrt(x), x > 0: the bit guess and two Newton steps (within 5e-6) */
static float fl_rsqrt(float x)
{
    union {
        float f;
        int32_t i;
    } u;
    float h = 0.5f * x;
    u.f = x;
    u.i = 0x5F3759DF - (u.i >> 1);
    u.f *= 1.5f - h * u.f * u.f;
    u.f *= 1.5f - h * u.f * u.f;
    return u.f;
}

void fl_init(fl_t *f)
{
    f->ic1 = f->ic2 = f->env = f->lp = f->dly = 0.0f;
    f->note = 60.0f;
    f->wp = 0;
}

void fl_note(fl_t *f, int32_t p16)
{
    if (p16 >= 0)
        f->note = (float)p16 * (1.0f / 16.0f);
}

/* the frequency CUT / TUNE, KTRK and ENV put the next block at, Hz */
static float fl_hz(const fl_t *f, const fl_param_t *p)
{
    float n = 16.0f + (float)p->cut * (118.0f / 127.0f);
    float e = f->env < 1.0f ? f->env : 1.0f;
    n += (float)(p->ktrk > 127 ? 127 : p->ktrk) * (1.0f / 127.0f) * (f->note - 60.0f);
    n += ((float)(p->env > 127 ? 127 : p->env) - 64.0f) * (36.0f / 63.0f) * e;
    return fl_clamp(440.0f * fl_exp2((n - 69.0f) * (1.0f / 12.0f)), FL_FMIN, FL_FMAX);
}

float fl_freq(const fl_t *f, const fl_param_t *p) { return fl_hz(f, p); }

static float fl_k(uint8_t res) { return 2.0f * fl_exp2((float)(res > 127 ? 127 : res) * (-8.0f / 127.0f)); }
static float fl_dg(uint8_t drive)
{
    float d = (float)(drive > 127 ? 127 : drive) * (1.0f / 127.0f);
    return 1.0f + 15.0f * d * d;
}
/* DRIVE's make-up: the level of a quarter-scale input (-12 dB at the host's half scale, a voice's usual level) held
 * through the saturator; quieter inputs come out louder with DRIVE, louder ones softer, as a driven stage does */
static float fl_comp(float dg) { return 0.25f / (FL_H * fl_sat(0.25f * dg * (1.0f / FL_H))); }
/* MORPH's weights on LP, BP, HP: LP -> BP -> HP -> NOTCH (LP + HP) */
static void fl_morph(uint8_t m, float *wl, float *wb, float *wh)
{
    float t = (float)(m > 127 ? 127 : m) * (3.0f / 127.0f);
    if (t < 1.0f) {
        *wl = 1.0f - t, *wb = t, *wh = 0.0f;
    } else if (t < 2.0f) {
        *wl = 0.0f, *wb = 2.0f - t, *wh = t - 1.0f;
    } else {
        *wl = t - 2.0f, *wb = 0.0f, *wh = 1.0f;
    }
}

void fl_filter(fl_t *f, const fl_param_t *p, const int32_t *in, int32_t *out, uint32_t n)
{
    float g = fl_tan(3.14159265f * fl_hz(f, p) * (1.0f / FL_FS)), k = fl_k(p->res), dg = fl_dg(p->drive);
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2, comp = fl_comp(dg);
    float ic1 = f->ic1, ic2 = f->ic2, env = f->env, wl, wb, wh;
    uint32_t i;
    fl_morph(p->morph, &wl, &wb, &wh);
    wl *= comp * 32768.0f, wb *= comp * 32768.0f, wh *= comp * 32768.0f;
    for (i = 0; i < n; i++) {
        float x = (float)in[i] * (1.0f / 32768.0f), a = fl_abs(x), v1, v2, v3, y;
        env = a > env ? a : env - (env - a) * FL_REL;
        x = FL_H * fl_sat(x * dg * (1.0f / FL_H));
        v3 = x - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = fl_bound(2.0f * v1 - ic1);
        ic2 = 2.0f * v2 - ic2;
        y = wl * v2 + wb * v1 + wh * (x - k * v1 - v2);
        out[i] = (int32_t)fl_clamp(y, -FL_WMAX, FL_WMAX);
    }
    if (fl_abs(ic1) < FL_QUIET && fl_abs(ic2) < FL_QUIET)
        ic1 = ic2 = 0.0f;
    if (env < FL_QUIET)
        env = 0.0f;
    f->ic1 = ic1, f->ic2 = ic2, f->env = env;
}

void fl_comb(fl_t *f, const fl_param_t *p, int16_t *line, uint32_t len, const int32_t *in, int32_t *out, uint32_t n)
{
    float fb = ((float)(p->res > 127 ? 127 : p->res) - 64.0f) * (0.97f / 63.0f), dg = fl_dg(p->drive);
    float d1 = FL_FS / fl_hz(f, p) * (fb < 0.0f ? 0.5f : 1.0f), d0 = f->dly, dd, comp = fl_comp(dg);
    float lp = f->lp, env = f->env, a = 1.0f - (float)(p->morph > 127 ? 127 : p->morph) * (0.9f / 127.0f);
    uint32_t i, wp = f->wp, mask = len - 1u;
    fb = fl_clamp(fb, -0.97f, 0.97f);
    d1 = fl_clamp(d1, 2.0f, (float)len - 3.0f);
    if (d0 <= 0.0f)
        d0 = d1;
    dd = (d1 - d0) / (float)(n ? n : 1u);               /* (the delay glides across the block: no click on a retune) */
    for (i = 0; i < n; i++) {
        float x = (float)in[i] * (1.0f / 32768.0f), ax = fl_abs(x), d = d0 + dd * (float)(i + 1u), fr, z, y;
        int32_t di = (int32_t)d, w;
        fr = d - (float)di;
        z = ((float)line[(wp - (uint32_t)di) & mask] * (1.0f - fr) + (float)line[(wp - (uint32_t)di - 1u) & mask] * fr) *
            (1.0f / FL_LQ);
        lp += (z - lp) * a;
        env = ax > env ? ax : env - (env - ax) * FL_REL;
        y = FL_H * fl_sat(x * dg * (1.0f / FL_H)) + fb * lp;
        y = 4.0f * fl_sat(y * 0.25f);                   /* (the loop's bound: the line holds +-4) */
        w = (int32_t)(y * FL_LQ);
        line[wp & mask] = (int16_t)(w > 32767 ? 32767 : w < -32767 ? -32767 : w);
        wp++;
        out[i] = (int32_t)fl_clamp(y * comp * 32768.0f, -FL_WMAX, FL_WMAX);
    }
    if (fl_abs(lp) < FL_QUIET)
        lp = 0.0f;
    if (env < FL_QUIET)
        env = 0.0f;
    f->lp = lp, f->env = env, f->dly = d1, f->wp = wp & mask;
}

float fl_mag(const fl_t *f, const fl_param_t *p, float hz)
{
    float fc = fl_hz(f, p), w = hz / fc, k = fl_k(p->res), wl, wb, wh, nr, ni, dr, di;
    fl_morph(p->morph, &wl, &wb, &wh);
    nr = wl - wh * w * w;                               /* (wl + wb s + wh s^2) / (s^2 + k s + 1), s = j w */
    ni = wb * w;
    dr = 1.0f - w * w;
    di = k * w;
    nr = nr * nr + ni * ni;
    dr = dr * dr + di * di;
    return nr > 0.0f ? nr * fl_rsqrt(nr) * fl_rsqrt(dr) : 0.0f;
}
