/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah
 * The lizard oscillator core, included into the firmware by eng_lizard.c. */
#include "lizard.h"

#define TWO_PI 6.28318530718f
#define PH_ONE 4294967296.0f            /* one cycle in phase units */

/* cycle fraction for a frequency ratio, clamped below Nyquist */
static uint32_t phase_inc(float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 0.5f) ratio = 0.5f;
    return (uint32_t)(ratio * PH_ONE + 0.5f);
}

/* phase to 0..1 using the top 24 bits, so the result never rounds up to 1.0 */
static float ph_float(uint32_t p) { return (float)(p >> 8) * (1.0f / 16777216.0f); }

/* 0..1 to phase */
static uint32_t ph_from(float f)
{
    if (f <= 0.0f) return 0;
    if (f >= 1.0f) return 0xFFFFFFFFu;
    return (uint32_t)(f * PH_ONE);
}

static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* polyBLEP residuals for a unit step: t in [0,1) just after the edge, t in (-1,0] just before */
static float blep_after(float t) { return t + t - t * t - 1.0f; }
static float blep_before(float t) { return t * t + t + t + 1.0f; }

static void make_order(lz_osc *o, int *ord)
{
    int i;
    switch (o->sweep) {
    case LZ_UP:
        for (i = 0; i < 8; i++) ord[i] = i;
        o->n = 8;
        break;
    case LZ_DOWN:
        for (i = 0; i < 8; i++) ord[i] = 7 - i;
        o->n = 8;
        break;
    case LZ_UPDOWN:
        /* 0..7..1, ends not repeated: 14 steps a cycle */
        for (i = 0; i < 8; i++) ord[i] = i;
        for (i = 0; i < 6; i++) ord[8 + i] = 6 - i;
        o->n = 14;
        break;
    case LZ_RANDOM:
        for (i = 0; i < 8; i++) ord[i] = i;
        for (i = 7; i > 0; i--) {
            int j = xorshift(&o->rng) % (i + 1), t = ord[i];
            ord[i] = ord[j];
            ord[j] = t;
        }
        o->n = 8;
        break;
    }
}

void lz_init(lz_osc *o, float sr, uint32_t seed)
{
    int i;
    for (i = 0; i < (int)sizeof *o; i++) ((char *)o)[i] = 0;
    o->sr = sr;
    o->freq = 110.0f;
    o->blep = 1;
    o->rng = seed ? seed : 1;
    for (i = 0; i < 8; i++) {
        o->steps[i].src = LZ_DC;
        o->steps[i].level = 0.0f;
    }
    o->voices = 1;
    o->mix[0] = 1.0f;
    lz_set_sweep(o, LZ_UP);
}

void lz_set_sweep(lz_osc *o, enum lz_sweep s)
{
    o->sweep = s;
    make_order(o, o->order);
    make_order(o, o->next_order);
    o->prev_last = o->order[o->n - 1];
}

void lz_note(lz_osc *o, float hz)
{
    int v;
    o->freq = hz;
    o->phase = 0;
    make_order(o, o->order);
    make_order(o, o->next_order);
    o->prev_last = o->order[o->n - 1];
    for (v = 0; v < LZ_MAX_VOICES; v++) o->vphase[v] = o->start[v];
    for (v = 0; v < 2 * LZ_HB_N; v++) o->hb[v] = 0.0f;
}

void lz_phasedsaw(lz_osc *o, int voices, float amount)
{
    int v;
    if (voices < 2) voices = 2;
    if (voices > LZ_MAX_VOICES) voices = LZ_MAX_VOICES;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    o->voices = voices;
    for (v = 0; v < voices; v++) {
        /* slaves alternate sharp and flat, each a little further out */
        o->detune[v] = v ? 0.01f * amount * (float)v * (v & 1 ? 1.0f : -1.0f) : 0.0f;
        o->mix[v] = 1.0f / (float)voices;
        o->start[v] = ph_from(amount * (float)v / (float)voices);
    }
}

void lz_superwave(lz_osc *o, float detune, const float mix[4])
{
    /* two down, two up, the outer pair wider, uneven so the beating does not line up */
    static const float spread[4] = { -1.0f, -0.42f, 0.47f, 1.06f };
    int v;
    o->voices = 5;
    o->detune[0] = 0.0f;
    o->mix[0] = 0.4f;
    for (v = 0; v < 4; v++) {
        o->detune[v + 1] = 0.025f * detune * spread[v];
        o->mix[v + 1] = 0.4f * mix[v];
    }
    for (v = 0; v < 5; v++) o->start[v] = ph_from(0.17f * (float)v);
}

/*
 * sin(2 pi p) for p in 0..1, no libm (Felucca and other bare-metal targets link none):
 * fold into -pi/2..pi/2, then the odd Taylor series to x^9; worst error about 4e-6
 */
static float lz_sin(float p)
{
    float x, x2;
    if (p < 0.25f) x = TWO_PI * p;
    else if (p < 0.75f) x = TWO_PI * (0.5f - p);
    else x = TWO_PI * (p - 1.0f);
    x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

/* value of step s at source phase p, 0..1 */
static float step_value(const lz_step *s, float p)
{
    switch (s->src) {
    case LZ_DC: return s->level;
    case LZ_SINE: return s->level * lz_sin(p);
    case LZ_TRI: return s->level * (p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p);
    case LZ_SAW: return s->level * (2.0f * p - 1.0f);
    }
    return 0.0f;
}

/*
 * wavedrawer and the switcher are one engine: a pattern is n steps, step k plays
 * steps[order[k]]. all-DC steps give the Lizard's WaveDrawer; mixed sources give the
 * analog switcher's OUT2 in VCO form.
 *
 * slice mode: the pattern is one note cycle and every source runs in sync with the note,
 * so a step takes a slice of its source at the note's own phase. whole_cycles: each step
 * lasts one full note cycle and plays a full cycle of its source, so 2 alternating steps
 * give the Casio CZ trick and halve the pitch.
 */
static float drawer_ratio(const lz_osc *o)
{
    return o->freq / o->sr / (o->whole_cycles ? (float)o->n : 1.0f);
}

uint32_t lz_phase_inc(const lz_osc *o)
{
    return phase_inc(drawer_ratio(o));
}

__attribute__((noinline)) void lz_wavedrawer(lz_osc *o, float *out, int len)
{
    lz_wavedrawer_steps(o, out, 0, len);
}

/* one drawer sample at the current phase, then advance by inc. ratio is the note's
 * pattern cycles per sample at this call's rate. returns the step played via *step */
static float drawer_sample(lz_osc *o, float ratio, uint32_t inc, int blep, int *step)
{
    int n = o->n;
    uint64_t pos = (uint64_t)o->phase * (uint32_t)n;
    int k = (int)(pos >> 32);
    float frac = ph_float((uint32_t)pos);
    const lz_step *cur = &o->steps[o->order[k]];
    float v = step_value(cur, o->whole_cycles ? frac : ph_float(o->phase));
    uint32_t next;

    if (blep) {
        float dts = ratio * (float)n;           /* step phase per sample */
        if (dts > 1.0f) dts = 1.0f;
        if (frac < dts) {
            /* edge into step k: jump from the previous step's end value */
            const lz_step *prev = k > 0 ? &o->steps[o->order[k - 1]]
                                        : &o->steps[o->prev_last];
            float b = o->whole_cycles ? 0.0f : (float)k / (float)n;
            float h = step_value(cur, b) - step_value(prev, o->whole_cycles || !k ? 1.0f : b);
            v += 0.5f * h * blep_after(frac / dts);
        }
        if (frac > 1.0f - dts) {
            /* edge out of step k into the next one */
            const lz_step *nx = k + 1 < n ? &o->steps[o->order[k + 1]]
                                          : &o->steps[o->next_order[0]];
            float b = o->whole_cycles ? 1.0f : (float)(k + 1) / (float)n;
            float h = step_value(nx, b < 1.0f ? b : 0.0f) - step_value(cur, b);
            v += 0.5f * h * blep_before((frac - 1.0f) / dts);
        }
    }
    *step = o->order[k];

    next = o->phase + inc;
    if (next < o->phase) {
        /* wrapped: the next pattern begins */
        int j;
        o->prev_last = o->order[n - 1];
        for (j = 0; j < LZ_MAX_STEPS; j++) o->order[j] = o->next_order[j];
        make_order(o, o->next_order);
    }
    o->phase = next;
    return v;
}

/*
 * 2x decimator for LZ_BLEP_HQ: polyphase IIR halfband, H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2.
 * x1, the later of the two 2x samples, runs through A0 (even coefficients), x0 through A1.
 * stopband from 24.1 kHz at 44.1 kHz out: -51.7 dB (a minimax design).
 */
static const float hb_coef[LZ_HB_N] = { 0.67954255f, 0.89790637f, 0.12749131f, 0.40666679f };

static float halfband(lz_osc *o, float x0, float x1)
{
    int i;
    for (i = 0; i < LZ_HB_N; i++) {
        float *s = o->hb + 2 * i, *x = (i & 1) ? &x0 : &x1;
        float y = hb_coef[i] * (*x - s[1]) + s[0];     /* (a + z^-1) / (1 + a z^-1) */
        s[0] = *x;
        s[1] = y;
        *x = y;
    }
    return 0.5f * (x0 + x1);
}

/* once a block: allpass states that decayed into denormals go to 0 (silence would otherwise keep them
 * there forever, and denormals may be slow or trap on the device) */
static void hb_flush(lz_osc *o)
{
    int i;
    for (i = 0; i < 2 * LZ_HB_N; i++)
        if (o->hb[i] > -1e-15f && o->hb[i] < 1e-15f)
            o->hb[i] = 0.0f;
}

__attribute__((noinline)) void lz_wavedrawer_steps(lz_osc *o, float *out, int *step_out, int len)
{
    /* freq is fixed for the block */
    float ratio = drawer_ratio(o);
    uint32_t inc = lz_phase_inc(o);
    int i, k;

    if (o->blep == LZ_BLEP_HQ) {
        /* two half steps per output sample, so the phase still advances by inc a sample */
        uint32_t ia = inc >> 1, ib = inc - ia;
        for (i = 0; i < len; i++) {
            int k2;
            float a = drawer_sample(o, 0.5f * ratio, ia, 1, &k);
            float b = drawer_sample(o, 0.5f * ratio, ib, 1, &k2);
            out[i] = halfband(o, a, b);
            if (step_out) step_out[i] = k;
        }
        hb_flush(o);
        return;
    }
    for (i = 0; i < len; i++) {
        out[i] = drawer_sample(o, ratio, inc, o->blep, &k);
        if (step_out) step_out[i] = k;
    }
}

/* one multisaw sample, then advance each voice by its inc */
static float saw_sample(lz_osc *o, const float *ratio, const uint32_t *inc, int blep)
{
    float acc = 0.0f;
    int v;
    for (v = 0; v < o->voices; v++) {
        float t = ph_float(o->vphase[v]);
        float s = 2.0f * t - 1.0f;
        if (blep) {
            float d = ratio[v];
            if (t < d) s -= blep_after(t / d);
            else if (t > 1.0f - d) s -= blep_before((t - 1.0f) / d);
        }
        acc += o->mix[v] * s;
        o->vphase[v] += inc[v];
    }
    return acc;
}

/*
 * phasedsaw and superwave: a center saw plus detuned slaves, each band-limited.
 * phasedsaw: 2..5 voices, the slaves' detune and start phase set the timbre.
 * superwave: 5 voices, two down and two up, a mix per slave.
 */
__attribute__((noinline)) void lz_multisaw(lz_osc *o, float *out, int len)
{
    float ratio[LZ_MAX_VOICES];
    uint32_t inc[LZ_MAX_VOICES];
    int i, v;

    for (v = 0; v < o->voices; v++) {
        ratio[v] = o->freq * (1.0f + o->detune[v]) / o->sr;
        inc[v] = phase_inc(ratio[v]);
    }
    if (o->blep == LZ_BLEP_HQ) {
        float half[LZ_MAX_VOICES];
        uint32_t ia[LZ_MAX_VOICES], ib[LZ_MAX_VOICES];
        for (v = 0; v < o->voices; v++) {
            half[v] = 0.5f * ratio[v];
            ia[v] = inc[v] >> 1;
            ib[v] = inc[v] - ia[v];
        }
        for (i = 0; i < len; i++) {
            float a = saw_sample(o, half, ia, 1);
            float b = saw_sample(o, half, ib, 1);
            out[i] = halfband(o, a, b);
        }
        hb_flush(o);
        return;
    }
    for (i = 0; i < len; i++) out[i] = saw_sample(o, ratio, inc, o->blep);
}
