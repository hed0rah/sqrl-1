/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* The HOME pictures of the engines the flower stood in for (ANALOG PHASE LOFI SAMPLE VOICE TRIO WHEEL GRAIN PHYS
 * NOISE SHOCK SLICE DRUMS), and the helpers they share. Part of the shell: shell.c includes it where they sit in
 * its drawing code, after the other pictures, so they use its primitives (sh_line, sh_rect, sh_text, the SC_
 * colours) and the engines' own tables. Main loop only, never the audio ISR: tools/size_fns.py builds it at
 * LLVM's minsize (SIZE_FILES) */

/* ------------------------------------------------------- the pictures of the engines the flower stood in for --- */
/* Each reads the engine's eight (p[P_E0 .. P_E7], which the signature hashes, so a turn redraws it) through the
 * engine's own tables and formulas where it has them, at C4 and full velocity; no per-pixel loops, a few hundred
 * short lines at most */

/* PHYS: the body the model is (a bar, a string, a drum head, a string with three sympathetic ones), struck at POS;
 * under it its partials as they ring: time across (SH_PK_T seconds), the ratio to the lowest up (log, 1 .. 32), each
 * a line fading over 40 dB with its decay, from the model's own Q (px_modal_q) or the string's loop filter */
#define SH_PK_T 2.0f                   /* the ringing chart's seconds */
static void sh_pk_fix(char *b, int32_t v, int d)        /* fmt_fix with the 0 before the point: 0.25 */
{
    if (v >= 0 && v < (d == 1 ? 10 : 100)) {
        *b++ = '0';
    }
    fmt_fix(b, v, d);
}
#define SH_PK_X0 24
#define SH_PK_X1 232
#define SH_PK_Y1 168                   /* the chart's ratio 1 row; 19 px an octave up from it */
static float sh_pk_row(float r) { return (float)SH_PK_Y1 - sh_log2(r) * 19.0f; }
/* one partial: level a0 (linear, 1 the loudest) decaying by tau seconds, from t0 seconds (the sympathetic strings
 * take a moment); 16 runs, the colour falling with the level in dB */
static void sh_pk_ring(float r, float a0, float tau, float t0, uint16_t c)
{
    uint32_t i;
    int32_t y = (int32_t)(sh_pk_row(r) + 0.5f);
    float db0 = 6.0206f * sh_log2(a0), w = (float)(SH_PK_X1 - SH_PK_X0) / 16.0f;
    if (r < 0.99f || y < 74 || a0 < 0.01f)
        return;
    for (i = 0; i < 16u; i++) {
        float t = ((float)i + 0.5f) * (SH_PK_T / 16.0f), pct;
        int32_t xa = SH_PK_X0 + (int32_t)((float)i * w), xb = SH_PK_X0 + (int32_t)((float)(i + 1u) * w);
        if (t < t0)
            continue;
        pct = 100.0f + 2.5f * (db0 - 8.6859f * (t - t0) / (tau > 0.001f ? tau : 0.001f));
        if (pct < 6.0f)
            break;
        pct = pct > 100.0f ? 100.0f : pct;              /* a blade: as thick as it is loud, 1 .. 5 px */
        sh_rect(xa, y - (int32_t)(pct * 0.02f), xb - xa - (i == 15u ? 0 : 1), 1 + 2 * (int32_t)(pct * 0.02f),
                sh_mix(SC_BG, c, 30 + (int32_t)(pct * 0.7f)));
    }
}
/* a string's loop: its partial n's decay (s) at C4, from the loop low-pass DAMP and BRIT set (px_string_block: a
 * one-pole dcut semitones above the note, open past DAMP 95 %) */
static float sh_pk_str_tau(float b, float d, float n)
{
    float dcut = 12.0f + 60.0f * d * d + 24.0f * b, R, g2;
    if (d >= 0.95f)
        dcut += (d - 0.95f) * 20.0f * (128.0f - dcut);
    R = sh_exp2(dcut / 12.0f);
    g2 = 1.0f / (1.0f + (n / R) * (n / R));             /* the loop's gain squared, a period */
    return g2 > 0.999999f ? 99.0f : -2.0f / (sh_log2(g2) * 0.6931f * 261.6f);
}
/* a string from x0 to x1 at y, plucked at u (0..1) by h px (a triangle), its bridges; curved: the bridge an arc */
static void sh_pk_string(float x0, float x1, float y, float u, float h, uint16_t c, int bridges, int curved)
{
    float xp = x0 + (x1 - x0) * u;
    sh_line(x0, y, xp, y - h, c);
    sh_line(xp, y - h, x1, y, c);
    if (!bridges)
        return;
    if (curved) {
        sh_line(x0 - 4.0f, y + 5.0f, x0 - 1.0f, y + 1.0f, SC_DIM);
        sh_line(x0 - 1.0f, y + 1.0f, x0 + 5.0f, y, SC_DIM);
    } else {
        sh_line(x0 - 3.0f, y + 5.0f, x0, y, SC_DIM);
        sh_line(x0, y, x0 + 3.0f, y + 5.0f, SC_DIM);
    }
    sh_line(x1 - 3.0f, y + 5.0f, x1, y, SC_DIM);
    sh_line(x1, y, x1 + 3.0f, y + 5.0f, SC_DIM);
}
static void sh_pic_phys(const sh_mode_t *m, const track_t *t)
{
    sh_live_t L;
    uint16_t hc = 0;
    uint32_t hot;
    const int16_t *p = t->p;
    uint32_t md = (uint32_t)p[P_E0] % PM_COUNT, i, n = 0;
    float acc = (float)clamp(p[P_E5] * 127 * 4, 0, 65536) * (1.0f / 65536.0f), br = (float)p[P_E2] * (1.0f / 127.0f);
    float b = br + acc * 0.25f * (1.0f - br), d0 = (float)p[P_E3] * (1.0f / 127.0f), d = d0 + acc * 0.25f * (1.0f - d0);
    float r[PX_NMODE], a[PX_NMODE], tau[PX_NMODE], amax = 0.0f;
    char cap[40], v[12];
    static const float RT[5] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
    for (i = 0; i < 5u; i++) {                          /* the ratio guides, the time axis */
        int32_t y = (int32_t)(sh_pk_row(RT[i]) + 0.5f);
        sh_rect(SH_PK_X0, y, SH_PK_X1 - SH_PK_X0, 1, i ? SC_PANEL : SC_FAINT);
        fmt_int(v, (int32_t)RT[i]);
        sh_text_r(SH_PK_X0 - 4, y - 7, &AF_S, v, SC_MID);
    }
    sh_text(SH_PK_X0, SH_PK_Y1 + 4, &AF_S, "STRIKE", SC_MID);
    sh_text_r(SH_PK_X1, SH_PK_Y1 + 4, &AF_S, "2 S", SC_MID);
    if (md == PM_MODAL || md == PM_MEMB) {              /* the resonator bank: px_modal_block / px_memb_block */
        int32_t ql;
        float s = (float)p[P_E1] * (516.0f / 65536.0f), pos = (float)p[P_E4] * (1.0f / 127.0f), q0, qk;
        uint64_t q8 = px_modal_q(md == PM_MEMB ? 0 : (int32_t)(s * 65536.0f), (int32_t)(b * 65536.0f),
                                 (int32_t)(d * 65536.0f), &ql);
        q0 = (float)(uint32_t)(q8 > 0xFFFFFFFFu ? 0xFFFFFFFFu : q8) * (1.0f / 256.0f);   /* (through 32 bits: no
                                                         * 64-bit to float helper on the device) */
        qk = (float)ql * (1.0f / 65536.0f);
        if (md == PM_MODAL) {                           /* stretched by STRC, the strike's cos(2 pi pos i) */
            float st = (float)px_stiff((int32_t)(s * 65536.0f)) * (1.0f / 65536.0f), st2 = st, str = 1.0f;
            for (n = 0; n < PX_NMODE; n++) {
                r[n] = (float)(n + 1u) * str;
                a[n] = sh_cos(SH_TAU * pos * 0.25f * (float)n);
                a[n] = a[n] < 0.0f ? -a[n] : a[n];
                str += st2;
                st2 *= st2 < 0.0f ? 0.93f : 0.98f;
            }
        } else {                                        /* the head's modes blended by HARM, POS centre to rim */
            float w1 = 1.5f * pos > 1.0f ? 1.0f : 1.5f * pos, w[6];
            w[0] = 1.0f - 0.6f * pos;
            w[1] = w1;
            for (i = 2; i < 6u; i++)
                w[i] = w[i - 1u] * w1;
            for (n = 0; n < PX_NMEMB; n++) {
                r[n] = ((float)PX_MEMB_IDEAL[n] + (float)((int32_t)PX_MEMB_LOADED[n] - (int32_t)PX_MEMB_IDEAL[n]) * s) *
                       (1.0f / 65536.0f);
                a[n] = w[PX_MEMB_M[n]] / (r[n] * r[n]);
            }
        }
        {   /* the strike's low-pass (px_modal_exciter): its cutoff in harmonics of the note, by BRIT; bowed, wider */
            int bow = md == PM_MODAL && p[P_E6];
            float cx = (bow ? 4.0f : 2.0f) * sh_exp2((b * (2.0f - b) - 0.5f) * (bow ? 3.0f : 5.0f)), iq = bow ? 1.4f : 0.67f;
            for (i = 0; i < n; i++) {
                float x = r[i] / cx, g = (1.0f - x * x) * (1.0f - x * x) + x * x * iq * iq;
                a[i] *= sh_exp2(-0.5f * sh_log2(g));        /* 1 / sqrt(g) */
            }
        }
        for (i = 0; i < n; i++) {                       /* Q = 1 + f q0 ql^i; the decay Q / (pi f) */
            float f = 261.6f * r[i] / r[0];
            tau[i] = md == PM_MODAL && p[P_E6] ? 99.0f : (1.0f + f * (1.0f / 44100.0f) * q0) / (3.1415927f * f);
            q0 *= qk;
            amax = a[i] > amax ? a[i] : amax;
        }
    } else {                                            /* STRNG, SYMP: harmonics, the pluck's comb and the burst's tone */
        float u = md == PM_STRING ? (float)p[P_E4] * (258.0f / 65536.0f) : 0.0f;
        float cx = 4.0f * sh_exp2(6.0f * (b * (2.0f - b) - 0.5f));   /* the burst's low-pass, in harmonics */
        n = 12u;
        for (i = 0; i < n; i++) {
            float k = (float)(i + 1u), c = SH_TAU * 0.5f * k * u;
            r[i] = k;
            a[i] = (u > 0.0f ? (sh_sin(c) < 0.0f ? -sh_sin(c) : sh_sin(c)) : 1.0f) / (1.0f + (k / cx) * (k / cx));
            tau[i] = sh_pk_str_tau(b, d, k);
            amax = a[i] > amax ? a[i] : amax;
        }
    }
    hot = sh_hot_main(&hc);                             /* (STRC BRIT DAMP: the partials they set; POS: the strike) */
    for (i = n; i-- > 0;)                               /* the partials, the lowest drawn last (on top) */
        sh_pk_ring(r[i] / r[0], a[i] / (amax > 0.0f ? amax : 1.0f), tau[i], 0.0f,
                   hot == P_E1 || hot == P_E2 || hot == P_E3 ? hc : i ? m->color : SC_BONE);
    sh_live_of(t, &L);
    if (L.n && n) {                                     /* sounding: now, on the time axis, where the lowest partial's
                                                         * decay has the loudest note's level */
        float lv = L.top / (L.vel > 0.05f ? L.vel : 1.0f), tn = lv > 0.001f ? -tau[0] * 0.6931f * sh_log2(lv > 1.0f ? 1.0f : lv) : SH_PK_T;
        int32_t x = SH_PK_X0 + (int32_t)((float)(SH_PK_X1 - SH_PK_X0) * (tn < 0.0f ? 0.0f : tn > SH_PK_T ? SH_PK_T : tn) / SH_PK_T);
        sh_rect(x, 74, 2, SH_PK_Y1 - 72, SC_BONE);
    }
    str_cpy(cap, N_PHYS_MODEL[md], sizeof cap);
    if (md == PM_SYMP) {                                /* the three it rings along with: CHRD's chord */
        uint32_t c = (uint32_t)p[P_E1] >> 4, k;
        float sy = (float)p[P_E4] * (1.0f / 127.0f), lo = 1.0f;
        for (k = 0; k < PX_NSYMP; k++) {
            float rs = c < 7u ? sh_exp2((float)PHYS_CHORDS[c][k] / 1200.0f)
                              : sh_exp2((float)(48 + t->p[P_ROOT] % 12 + (k == 1u ? 7 : k == 2u ? 12 : 0) - 60) / 12.0f);
            lo = rs < lo ? rs : lo;
            for (i = 1; i <= 3u; i++)
                sh_pk_ring(rs * (float)i, sy * 0.6f / (float)i, sh_pk_str_tau(b, d, (float)i), 0.12f, SC_DIM);
        }
        str_cpy(cap + str_len(cap), " + ", sizeof cap - str_len(cap));
        str_cpy(cap + str_len(cap), N_PHYS_CHORD[c > 7u ? 7u : c], sizeof cap - str_len(cap));
        if (lo < 1.0f)                                  /* (OCT rings one below the note: off the chart) */
            str_cpy(cap + str_len(cap), " BELOW", sizeof cap - str_len(cap));
    }
    sh_text(8, 22, &AF_S, cap, m->color);
    if (md == PM_MODAL || md == PM_MEMB) {              /* the first ratios, as the presets name them */
        char rr[32];
        rr[0] = 0;
        for (i = 1; i < 4u && i < n; i++) {
            fmt_fix(v, (int32_t)(r[i] / r[0] * 100.0f + 0.5f), 2);
            str_cpy(rr + str_len(rr), i > 1u ? "  " : "1  ", sizeof rr - str_len(rr));
            str_cpy(rr + str_len(rr), v, sizeof rr - str_len(rr));
        }
        sh_text_r(232, 22, &AF_S, rr, SC_DIM);
    }
    switch (md) {                                       /* the body, struck where POS strikes */
    case PM_MODAL: {
        float xs = 40.0f + 160.0f * (float)p[P_E4] * (0.5f / 127.0f);   /* pos 0 an end, 1/4 the middle */
        sh_rect(40, 50, 160, 8, SC_PANEL);              /* the bar on its two cords */
        sh_rect(40, 50, 160, 1, SC_DIM);
        sh_rect(40, 57, 160, 1, SC_MID);
        sh_rect(40, 50, 1, 8, SC_DIM);
        sh_rect(199, 50, 1, 8, SC_DIM);
        sh_rect(76, 58, 1, 4, SC_MID);
        sh_rect(164, 58, 1, 4, SC_MID);
        sh_disc(xs, 45.0f, L.n && L.top > 0.5f ? 5.0f : 3.5f, hot == P_E4 ? hc : L.n && L.top > 0.5f ? SC_BONE : m->color);   /* the mallet */
        sh_line(xs + 2.5f, 42.5f, xs + 16.0f, 33.0f, m->color);
        break;
    }
    case PM_MEMB: {
        float xs = 120.0f + 62.0f * (float)p[P_E4] * (1.0f / 127.0f), px = 0.0f, py = 0.0f;
        for (i = 0; i <= 32u; i++) {                    /* the head, seen from above and in front */
            float an = SH_TAU * (float)i / 32.0f, x = 120.0f + 64.0f * sh_cos(an), y = 50.0f + 9.0f * sh_sin(an);
            if (i)
                sh_line(px, py, x, y, SC_DIM);
            px = x;
            py = y;
        }
        for (i = 0; i < 16u; i++) {                     /* the shell's front, under it */
            float a0 = SH_TAU * (float)i / 32.0f, a1 = SH_TAU * (float)(i + 1u) / 32.0f;
            sh_line(120.0f + 64.0f * sh_cos(a0), 61.0f + 9.0f * sh_sin(a0), 120.0f + 64.0f * sh_cos(a1),
                    61.0f + 9.0f * sh_sin(a1), SC_MID);
        }
        sh_line(56.0f, 50.0f, 56.0f, 61.0f, SC_MID);
        sh_line(184.0f, 50.0f, 184.0f, 61.0f, SC_MID);
        sh_disc(xs, 50.0f, hot == P_E4 ? 4.0f : 3.0f, hot == P_E4 ? hc : m->color);
        if (p[P_E6]) {                                  /* BEND: the pitch's fall after the strike */
            sh_pk_fix(v, p[P_E6] * 120 / 127, 1);
            str_cpy(v + str_len(v), " ST", sizeof v - str_len(v));
            sh_text(196, 36, &AF_S, "BEND", SC_DIM);
            sh_text(196, 50, &AF_S, v, m->color);
        }
        break;
    }
    default: {                                          /* the string, plucked, its next swings fainter */
        float u = md == PM_STRING ? 0.04f + (float)p[P_E4] * (0.46f / 127.0f) : 0.12f;
        int curved = (md == PM_STRING ? p[P_E1] * 516 : 16384 - p[P_E6] * 129) < 15729;   /* nl < 0: the bridge */
        float tk = sh_pk_str_tau(b, d, 1.0f);
        for (i = 3; i > 0; i--) {
            float g = sh_exp2(-1.4427f * (float)i * 0.08f / tk);
            if (g > 0.05f)
                sh_pk_string(40.0f, 200.0f, 50.0f, u, 11.0f * g * (i & 1u ? 1.0f : -1.0f), SC_MID, 0, 0);
        }
        sh_pk_string(40.0f, 200.0f, 50.0f, u, 11.0f, m->color, 1, curved);
        if (md == PM_SYMP) {
            uint32_t c = (uint32_t)p[P_E1] >> 4, k;
            for (k = 0; k < PX_NSYMP; k++) {            /* the sympathetic strings, each as long as its pitch */
                float rs = c < 7u ? sh_exp2((float)PHYS_CHORDS[c][k] / 1200.0f) : 1.0f + 0.5f * (float)k;
                float l = 160.0f / rs;
                sh_rect(40, 57 + (int32_t)k * 4, (int32_t)(l > 160.0f ? 160.0f : l), 1, SC_DIM);
            }
        }
        if (curved || (md == PM_STRING && p[P_E1] * 516 > 17039))
            sh_text_r(232, 22, &AF_S, curved ? "BUZZ BRIDGE" : "STIFF", SC_DIM);
        break;
    }
    }
}

/* a trace through points given one at a time: sh_pk_to(&pn, &pn, x, y) after sh_pk_pen(&pn, &pn, c); the pen on the stack */
typedef struct {
    float x, y;
    int on;
    uint16_t c;
} sh_pk_t;
static void sh_pk_pen(sh_pk_t *pn, uint16_t c) { pn->on = 0; pn->c = c; }
static void sh_pk_to(sh_pk_t *pn, float x, float y)
{
    if (pn->on)
        sh_line(pn->x, pn->y, x, y, pn->c);
    pn->x = x;
    pn->y = y;
    pn->on = 1;
}
/* a frequency response, 20 Hz .. 20 kHz across x0..x1, 0 dB at row y0, 1.4 px a dB (+16 .. -34 shown): the curve
 * mag(f) in c over a faint fill, the cutoff fc a bone tick */
static float sh_pk_fx(float hz, int32_t x0, int32_t x1) { return (float)x0 + (float)(x1 - x0) * sh_log2(hz / 20.0f) * (1.0f / 9.9658f); }
static void sh_pk_resp(int32_t x0, int32_t x1, int32_t y0, float fc, float (*mag)(float w, const float *k), const float *k,
                       uint16_t c)
{
    sh_pk_t pn;
    static const uint16_t GRID[3] = {100, 1000, 10000};
    uint32_t i;
    float yb = (float)y0 + 48.0f;
    for (i = 0; i < 3u; i++)
        sh_rect((int32_t)sh_pk_fx((float)GRID[i], x0, x1), y0 - 22, 1, 70, SC_PANEL);
    sh_rect(x0, y0, x1 - x0, 1, SC_FAINT);              /* 0 dB */
    sh_pk_pen(&pn, c);
    for (i = 0; i <= 52u; i++) {
        float hz = 20.0f * sh_exp2((float)i * (9.9658f / 52.0f)), db = 6.0206f * sh_log2(mag(hz / fc, k)), x, y;
        db = db > 16.0f ? 16.0f : db < -34.0f ? -34.0f : db;
        x = sh_pk_fx(hz, x0, x1);
        y = (float)y0 - db * 1.4f;
        if (i % 2u == 0u && y < yb)                     /* the fill: every other step, faint */
            sh_rect((int32_t)x, (int32_t)y + 2, 1, (int32_t)(yb - y) - 2, sh_mix(SC_BG, c, 16));
        sh_pk_to(&pn, x, y);
    }
    if (fc > 20.0f && fc < 20000.0f)
        sh_rect((int32_t)sh_pk_fx(fc, x0, x1), y0 - 24, 1, 72, SC_BONE);
}
/* the SVF's magnitude at w = f / fc: k[0] the damping, k[1] the output (0 LP, 1 BP, 2 HP) */
static float sh_pk_svf(float w, const float *k)
{
    float a = 1.0f - w * w, d = a * a + k[0] * k[0] * w * w, n = k[1] < 0.5f ? 1.0f : k[1] < 1.5f ? w : w * w;
    return n * sh_exp2(-0.5f * sh_log2(d > 1e-9f ? d : 1e-9f));
}

/* ANALOG: its two oscillators, each two cycles at its level in the MIX (SYNC: OSC 2 restarted by OSC 1, SUB: the
 * square an octave down), and the filter they go through: TYPE's response at CUT with RES's peak */
static float sh_pk_wave(uint32_t w, float u)            /* ANALOG's waves, -1 .. 1, u a cycle's phase */
{
    u -= (float)(int32_t)u;
    switch (w) {
    case 1: case 4: return u < 0.5f ? 1.0f : -1.0f;
    case 2: return u < 0.5f ? 4.0f * u - 1.0f : 3.0f - 4.0f * u;
    case 3: return sh_sin(SH_TAU * u);
    default: return 2.0f * u - 1.0f;
    }
}
static void sh_pic_analog(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    sh_live_t L;
    uint16_t hc = 0;
    uint32_t hot;
    const int16_t *p = t->p;
    uint32_t w = (uint32_t)clamp(p[P_E0], 0, 6), k, i;
    float mix = (float)p[P_E2] * (1.0f / 127.0f), kk[2];
    char b[24], v[12];
    for (k = 0; k < 2u; k++) {                          /* the two panels */
        int32_t x0 = k ? 128 : 8, x1 = k ? 232 : 112;
        float lv = k ? mix : 1.0f - mix, ratio = 1.0f, a = 4.0f + 12.0f * lv;
        uint16_t c = lv < 0.02f ? SC_MID : k ? sh_mix(m->color, SC_BONE, 35) : m->color;
        str_cpy(b, k ? "OSC 2 " : "OSC 1 ", sizeof b);
        str_cpy(b + str_len(b), w >= 5u ? "SAW" : N_ANALOG_WAVE[w], sizeof b - str_len(b));
        if (k && w == 5u) {                             /* SYNC: 3/16 semitone a DTN step above OSC 1 */
            ratio = sh_exp2((float)p[P_E1] * (3.0f / 192.0f));
            str_cpy(b + str_len(b), " SYNC", sizeof b - str_len(b));
        } else if (k && w == 6u) {
            ratio = 0.5f;
            str_cpy(b, "SUB SQR", sizeof b);
        } else if (k && p[P_E1]) {
            str_cpy(b + str_len(b), " +", sizeof b - str_len(b));
            fmt_int(b + str_len(b), p[P_E1]);
            str_cpy(b + str_len(b), "CT", sizeof b - str_len(b));
        }
        sh_text(x0, 24, &AF_S, b, k ? SC_DIM : m->color);
        sh_rect(x0, 60, x1 - x0, 1, SC_FAINT);
        sh_pk_pen(&pn, c);
        for (i = 0; i <= 96u; i++) {                    /* two cycles of OSC 1 */
            float u = (float)i * (2.0f / 96.0f), s;
            if (k && w == 5u)
                s = sh_pk_wave(0, (u - (float)(int32_t)u) * ratio);
            else
                s = sh_pk_wave(k && w == 6u ? 1u : w >= 5u ? 0u : w, u * ratio);
            sh_pk_to(&pn, (float)x0 + (float)(x1 - x0) * (float)i / 96.0f, 60.0f - a * s);
        }
        fmt_int(v, (int32_t)(lv * 100.0f + 0.5f));
        str_cpy(v + str_len(v), "%", 2);
        sh_text_r(x1, 80, &AF_S, v, SC_MID);
    }
    if (p[P_E3] || p[P_E6]) {                           /* NOISE and DRIVE, when they are in it */
        b[0] = 0;
        if (p[P_E3]) {
            str_cpy(b, "NOISE ", sizeof b);
            fmt_int(b + str_len(b), p[P_E3] * 100 / 127);
            str_cpy(b + str_len(b), "%  ", sizeof b - str_len(b));
        }
        if (p[P_E6]) {
            str_cpy(b + str_len(b), "DRIVE ", sizeof b - str_len(b));
            fmt_int(b + str_len(b), p[P_E6] * 100 / 127);
            str_cpy(b + str_len(b), "%", sizeof b - str_len(b));
        }
        sh_text(8, 80, &AF_S, b, SC_DIM);
    }
    kk[0] = (float)ANALOG_K(p[P_E5]) * (1.0f / 4096.0f);
    kk[1] = (float)clamp(t->p[P_FTYPE], 0, 2);
    sh_live_of(t, &L);
    hot = sh_hot_main(&hc);
    sh_pk_resp(16, 224, 124, (float)CUTOFF_HZ[p[P_E4] & 127], sh_pk_svf, kk,
               hot == P_E4 || hot == P_E5 ? hc : L.n ? sh_mix(SC_BG, m->color, 55) : m->color);   /* (CUT, RES: lit) */
    if (L.n) {                                          /* sounding: the cutoff where ENV takes it now, the notes on it */
        int32_t cn = clamp(p[P_E4] + (int32_t)((float)p[P_ED_FLT] * L.top), 0, 127);
        float x = sh_pk_fx((float)CUTOFF_HZ[cn], 16, 224);
        sh_pk_resp(16, 224, 124, (float)CUTOFF_HZ[cn], sh_pk_svf, kk, SC_BONE);
        for (i = 0; i < L.n; i++) {                     /* each note's fundamental, a tick its height its level */
            float hz = 440.0f * sh_exp2(((float)L.note[i] - 69.0f) * (1.0f / 12.0f));
            int32_t xn = (int32_t)sh_pk_fx(hz, 16, 224), hh = 2 + (int32_t)(18.0f * L.lv[i]);
            sh_rect(xn, 172 - hh, 2, hh, m->color);
        }
        (void)x;
    }
    if (hot == P_ATK || hot == P_REL)                   /* ATK, REL: the envelope they shape */
        sh_pk_adsr(t, 132, 132, 92, 40, hot, hc);
    str_cpy(b, kk[1] < 0.5f ? "LOW-PASS" : kk[1] < 1.5f ? "BAND-PASS" : "HIGH-PASS", sizeof b);
    sh_text(16, 178, &AF_S, b, SC_DIM);
}

/* SHOCK: the wave as the bore steepens it: the sine it starts from, the wave at the envelope's floor and at its top
 * (shock.c's own table, at C4, through LOSS's low-pass, SUB under it), and sigma on a ruler 0 .. 3 with its ceiling */
static void sh_pk_shock(float sg, float a, float sub, uint16_t c)
{
    sh_pk_t pn;
    int r0;
    float fr, lp = 0.0f;
    uint32_t i, ph = 0, inc = 25414007u, sp = 0;       /* (a period in 169 samples: C4) */
    shock_rows(sg, &r0, &fr);
    sh_pk_pen(&pn, c);
    for (i = 0; i < 3u * 169u; i++) {                   /* a period to settle the low-pass, then two drawn */
        float y = shock_read(r0, fr, ph) + (sub > 0.0f ? sub * shock_read(0, 0.0f, sp) : 0.0f);
        lp += a * (y - lp);
        if (i >= 169u && (i & 1u) == 0u)
            sh_pk_to(&pn, 16.0f + (float)(i - 169u) * (208.0f / 338.0f), 86.0f - 38.0f * lp / (1.0f + sub));
        ph += inc;
        sp += inc >> 1;
    }
}
static void sh_pic_shock(const sh_mode_t *m, const track_t *t)
{
    const int16_t *p = t->p;
    float maxs = 0.5f + 2.5f * (float)p[P_E7] * (1.0f / 127.0f), hi = 3.0f * (float)p[P_E0] * (1.0f / 127.0f), lo, a;
    float sub = (float)p[P_E4] * (1.0f / 127.0f), mult = 1.5f * sh_exp2(5.415f * (1.0f - (float)p[P_E3] * (1.0f / 127.0f)));
    float now;
    sh_live_t L;
    uint16_t hc = 0;
    uint32_t hot;
    char b[24];
    lo = hi * (1.0f - (float)p[P_E1] * (1.0f / 127.0f));
    hi = hi > maxs ? maxs : hi;
    lo = lo > maxs ? maxs : lo;
    a = SH_TAU * mult / 169.0f;                         /* LOSS: 1.5 .. 64 harmonics open, a sample's coefficient */
    a = a > 1.0f ? 1.0f : a;
    sh_live_of(t, &L);
    hot = sh_hot_main(&hc);                             /* (DRIVE: the top; ENV: the floor; LOSS, SUB: the wave) */
    now = lo + (hi - lo) * L.top;                       /* sounding: sigma where the envelope has it now */
    sh_rect(16, 86, 208, 1, SC_FAINT);
    sh_pk_shock(0.0f, 1.0f, 0.0f, SC_MID);              /* the sine it starts from */
    if (hi - lo > 0.05f || hot == P_E1)
        sh_pk_shock(lo, a, sub, hot == P_E1 ? hc : sh_mix(SC_BG, m->color, 45));
    sh_pk_shock(hi, a, sub, hot == P_E0 || hot == P_E3 || hot == P_E4 ? hc : L.n ? sh_mix(SC_BG, m->color, 45) : m->color);
    if (L.n)
        sh_pk_shock(now, a, sub, SC_BONE);              /* the wave as it sounds */
    str_cpy(b, "SIGMA ", sizeof b);
    if (hi - lo > 0.05f) {
        sh_pk_fix(b + str_len(b), (int32_t)(lo * 100.0f + 0.5f), 2);
        str_cpy(b + str_len(b), " .. ", sizeof b - str_len(b));
    }
    sh_pk_fix(b + str_len(b), (int32_t)(hi * 100.0f + 0.5f), 2);
    sh_text(16, 22, &AF_S, b, m->color);
    sh_text_r(224, 22, &AF_S, (L.n ? now : hi) >= 1.0f ? "SHOCK FRONT" : "STEEPENING", L.n ? SC_BONE : SC_DIM);
    {   /* the ruler: 0 .. 3, the shock at 1, sigma's span lit, MAXS a bone mark */
        int32_t x0 = 24, x1 = 216, y = 158, k;
        float sx = (float)(x1 - x0) / 3.0f;
        static const char *const RL[4] = {"0", "1", "2", "3"};
        sh_rect(x0, y, x1 - x0, 1, SC_MID);
        for (k = 0; k < 4; k++) {
            sh_rect(x0 + (int32_t)((float)k * sx), y - 3, 1, 7, k == 1 ? SC_DIM : SC_MID);
            sh_text_c(x0 + (int32_t)((float)k * sx), y + 6, &AF_S, RL[k], k == 1 ? SC_DIM : SC_MID);
        }
        if (maxs < 0.6f || maxs > 1.4f)                 /* (the shock's name over its mark, where MAX is not) */
            sh_text_c(x0 + (int32_t)sx, y - 24, &AF_S, "SHOCK", SC_MID);
        sh_rect(x0 + (int32_t)(lo * sx), y - 2, (int32_t)((hi - lo) * sx) + 2, 5, hot == P_E0 || hot == P_E1 ? hc : m->color);
        sh_rect(x0 + (int32_t)(maxs * sx), y - 8, 1, 17, SC_BONE);
        sh_text_c(x0 + (int32_t)(maxs * sx), y - 24, &AF_S, "MAX", SC_DIM);
        if (L.n)                                        /* sigma now, riding the span */
            sh_disc((float)x0 + now * sx, (float)y, 4.0f, SC_BONE);
    }
}

#if FELUCCA_TRIO
/* TRIO: its three oscillators in lanes, each wave as the set has it (TRIO_SETS: trio_naive's own shapes) at its level
 * and its interval over two cycles of the first; RING flips the first by the third, SYNC restarts the second with it */
static void sh_pic_trio(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    const int16_t *p = t->p;
    const trio_set_t *ws = &TRIO_SETS[(uint32_t)p[P_E0] & 15u];
    int32_t duty = clamp(p[P_E7] << 8, 768, 32000), gmax = 1, iv[3];
    trio_pw_t q;
    uint32_t k, i;
    float r[3];
    char b[24];
    q.pw = (uint32_t)duty << 17;
    q.pdc = 2 * duty - 32768;
    q.psm = 32768 - ((duty * duty) >> 15);
    iv[0] = 0;
    iv[1] = p[P_E1];
    iv[2] = p[P_E2];
    for (k = 0; k < 3u; k++) {
        r[k] = sh_exp2(((float)iv[k] + (k == 1u ? 0.01f : k == 2u ? -0.01f : 0.0f) * (float)p[P_E3]) / 12.0f);
        gmax = ws->g[k] > gmax ? ws->g[k] : gmax;
    }
    for (k = 0; k < 3u; k++) {
        int32_t yc = 62 + (int32_t)k * 44;
        float a = 15.0f * (float)ws->g[k] / (float)gmax;
        int ringonly = !ws->g[k] && k == 2u && (ws->flags & TRIO_RING);
        uint16_t c = k == 0u ? m->color : k == 1u ? sh_mix(m->color, SC_BONE, 40) : sh_mix(m->color, SC_DIM, 55);
        b[0] = (char)('1' + k);
        b[1] = 0;
        sh_text(8, yc - 16, &AF_M, b, ws->g[k] || ringonly ? c : SC_MID);
        if (k) {                                        /* the interval, and the detune that beats */
            fmt_int(b, iv[k]);
            if (iv[k] > 0) {
                b[0] = '+';
                fmt_int(b + 1, iv[k]);
            }
            sh_text(8, yc + 4, &AF_S, b, SC_DIM);
        }
        if (!ws->g[k] && !ringonly) {
            sh_text(36, yc - 7, &AF_S, "OFF", SC_MID);
            continue;
        }
        sh_rect(36, yc, 192, 1, SC_FAINT);
        if (ringonly)                                   /* silent, only flipping the first */
            a = 10.0f, c = SC_MID;
        sh_pk_pen(&pn, c);
        for (i = 0; i <= 192u; i++) {
            float u = (float)i * (2.0f / 192.0f), f = u * r[k], s;
            uint32_t w = ws->w[ringonly ? 2 : k] , ph;
            if (k == 1u && (ws->flags & TRIO_SYNC))     /* restarted by the first's every cycle */
                f = (u - (float)(int32_t)u) * r[1];
            ph = (uint32_t)((f - (float)(int32_t)f) * 4294967295.0f);
            if (w == TW_NOI)                            /* a new value 16 times a cycle */
                s = (float)((int32_t)(((uint32_t)(f * 16.0f) * 2654435761u) >> 16) - 32768) / 32768.0f;
            else
                s = (float)trio_naive(w, ph, &q) / 32768.0f;
            if (k == 0u && (ws->flags & TRIO_RING)) {   /* RING: the first's sign by the third's top bit */
                float f3 = u * r[2];
                if (f3 - (float)(int32_t)f3 >= 0.5f)
                    s = -s;
            }
            sh_pk_to(&pn, 36.0f + (float)i, (float)yc - a * s);
        }
    }
    sh_text(36, 22, &AF_S, N_TRIO_WAVE[(uint32_t)p[P_E0] & 15u], m->color);
    if (p[P_E3]) {                                      /* the detune: 2 up, 3 down */
        str_cpy(b, "DTN ", sizeof b);
        fmt_int(b + str_len(b), p[P_E3]);
        str_cpy(b + str_len(b), " CT", sizeof b - str_len(b));
        sh_text_r(228, 22, &AF_S, b, SC_DIM);
    }
}
#endif

#if FELUCCA_PHASE
/* PHASE: the CZ bend itself: the phase map (in, bent out) of WAVE at DCW, the cosine read through it over two cycles,
 * WAVE then WAVE2 as they alternate (pd_setup / pd_wave, FOLD's sine fold at A4, SUB under it); faint, the plain
 * cosine (DCW 0), and the wave where ENV takes DCW at the note's start */
static float sh_pk_pd(const track_t *t, uint32_t depth, float u)   /* PHASE's line at DCW depth (0..127 << 8) */
{
    const int16_t *p = t->p;
    uint32_t dcw = (uint32_t)clamp((int32_t)depth, 0, 127 << 8) * 65535u / (127u << 8), w1 = (uint32_t)p[P_E0] & 7u;
    uint32_t w2 = (uint32_t)clamp(p[P_E1], 0, 8), cyc = (uint32_t)u, ph;
    pd_t b;
    float s;
    dcw = (dcw * 56000u) >> 16;
    pd_setup(&b, cyc & 1u && w2 ? w2 - 1u : w1, dcw);
    ph = (uint32_t)((u - (float)cyc) * 65535.0f);
    s = (float)pd_wave(&b, ph) * (1.0f / 32768.0f);
    if (p[P_E5])                                        /* RING with its detuned twin, at the note's start: s s */
        s = s * s;
    if (p[P_E7]) {                                      /* FOLD: sin(pi/2 s g), g at A4 */
        float g = (float)pd_fold_gain((uint32_t)p[P_E7], PD_FOLD_REF) * (1.0f / 256.0f);
        s = sh_sin(1.5707963f * s * g);
    }
    return s + (float)p[P_E6] * (0.775f / 127.0f) * sh_sin(3.1415927f * u);   /* SUB: a sine an octave down */
}
#endif
#if FELUCCA_PHASE
static void sh_pic_phase(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    sh_live_t L;
    uint16_t hc = 0;
    uint32_t hot;
    const int16_t *p = t->p;
    uint32_t i, w1 = (uint32_t)p[P_E0] & 7u, dep = (uint32_t)p[P_E2] << 8, top = (uint32_t)clamp(p[P_E2] + p[P_E3], 0, 127) << 8;
    float dcw = (float)((uint32_t)clamp((int32_t)dep, 0, 127 << 8) * 65535u / (127u << 8)) * 56000.0f / 65536.0f, sc;
    pd_t b;
    char s[24];
    sc = 1.0f + (float)p[P_E6] * (0.775f / 127.0f);
    sh_rect(14, 30, 52, 52, SC_PANEL);                  /* the phase map: in across, out up */
    sh_line(14.0f, 81.0f, 65.0f, 30.0f, SC_FAINT);
    pd_setup(&b, w1, (uint32_t)dcw);
    sh_pk_pen(&pn, m->color);
    for (i = 0; i <= 48u; i++) {
        uint32_t ph = i * 65535u / 48u, pd;
        if (w1 >= 5u) {                                 /* RESO: the window over the bent core */
            float u = (float)i / 48.0f, win = w1 == 5u ? 1.0f - u : w1 == 6u ? (u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u)
                                                      : (u < 0.25f ? 4.0f * u : u > 0.75f ? 4.0f - 4.0f * u : 1.0f);
            sh_pk_to(&pn, 14.0f + 51.0f * u, 81.0f - 51.0f * win);
            continue;
        }
        switch (w1) {                                   /* pd_wave's bent phase, as it reads the cosine */
        case 0: pd = ph < b.x ? (ph * b.k0) >> 16 : 32768u + (((ph - b.x) * b.k1) >> 16); break;
        case 1: pd = ph < b.x ? (ph * b.k0) >> 16 : ph < 32768u ? 32768u : ph < 32768u + b.x ? 32768u + (((ph - 32768u) * b.k1) >> 16) : 65535u; break;
        case 2: pd = ph < b.x ? (ph * b.k0) >> 16 : 65535u; break;
        case 3: pd = ph < b.x ? (ph * b.k0) >> 16 : ((ph - b.x) * b.k1) >> 16; break;
        default: pd = ph >= b.x ? 65535u : ph < b.peak ? (ph * b.k0) >> 16 : 32768u + (((ph - b.peak) * b.k1) >> 16); break;
        }
        sh_pk_to(&pn, 14.0f + 51.0f * (float)i / 48.0f, 81.0f - 51.0f * (float)(pd > 65535u ? 65535u : pd) / 65535.0f);
    }
    sh_text(76, 30, &AF_S, "WAVE", SC_MID);
    sh_text(124, 30, &AF_S, N_PD_WAVE[w1], m->color);
    sh_text(76, 46, &AF_S, "THEN", SC_MID);
    sh_text(124, 46, &AF_S, p[P_E1] ? N_PD_WAVE2[(uint32_t)clamp(p[P_E1], 0, 8)] : N_PD_WAVE[w1], p[P_E1] ? m->color : SC_DIM);
    str_cpy(s, "DCW ", sizeof s);
    fmt_int(s + 4, p[P_E2] * 100 / 127);
    str_cpy(s + str_len(s), "%", sizeof s - str_len(s));
    if (top != dep) {
        str_cpy(s + str_len(s), " > ", sizeof s - str_len(s));
        fmt_int(s + str_len(s), (int32_t)(top >> 8) * 100 / 127);
        str_cpy(s + str_len(s), "%", sizeof s - str_len(s));
    }
    sh_text(76, 62, &AF_S, s, SC_DIM);
    if (p[P_E5] || p[P_E7] || p[P_E6]) {
        s[0] = 0;
        if (p[P_E5])
            str_cpy(s, "RING ", sizeof s);
        if (p[P_E7])
            str_cpy(s + str_len(s), "FOLD ", sizeof s - str_len(s));
        if (p[P_E6])
            str_cpy(s + str_len(s), "SUB", sizeof s - str_len(s));
        sh_text_r(232, 30, &AF_S, s, SC_DIM);
    }
    sh_rect(12, 136, 216, 1, SC_FAINT);
    sh_rect(120, 98, 1, 76, SC_PANEL);                  /* the two cycles: WAVE, WAVE2 */
    sh_pk_pen(&pn, SC_MID);                                  /* the plain cosine, DCW 0 */
    for (i = 0; i <= 108u; i++)
        sh_pk_to(&pn, 12.0f + 2.0f * (float)i, 136.0f + 34.0f * sh_cos(SH_TAU * (float)i / 54.0f) / sc);
    if (top != dep) {                                   /* where ENV takes it */
        sh_pk_pen(&pn, sh_mix(SC_BG, m->color, 40));
        for (i = 0; i <= 108u; i++)
            sh_pk_to(&pn, 12.0f + 2.0f * (float)i, 136.0f - 34.0f * sh_pk_pd(t, top, (float)i / 54.0f) / sc);
    }
    sh_live_of(t, &L);
    hot = sh_hot_main(&hc);                             /* (DCW: the wave; ENV: where it takes it; REL: the envelope) */
    if (hot == P_E3 && top != dep) {
        sh_pk_pen(&pn, hc);
        for (i = 0; i <= 108u; i++)
            sh_pk_to(&pn, 12.0f + 2.0f * (float)i, 136.0f - 34.0f * sh_pk_pd(t, top, (float)i / 54.0f) / sc);
    }
    sh_pk_pen(&pn, hot == P_E2 ? hc : L.n ? sh_mix(SC_BG, m->color, 60) : m->color);
    for (i = 0; i <= 108u; i++)
        sh_pk_to(&pn, 12.0f + 2.0f * (float)i, 136.0f - 34.0f * sh_pk_pd(t, dep, (float)i / 54.0f) / sc);
    if (L.n) {                                          /* sounding: the wave where ENV has DCW now */
        uint32_t now = dep + (uint32_t)((float)(top > dep ? top - dep : 0u) * L.top);
        sh_pk_pen(&pn, SC_BONE);
        for (i = 0; i <= 108u; i++)
            sh_pk_to(&pn, 12.0f + 2.0f * (float)i, 136.0f - 34.0f * L.top * sh_pk_pd(t, now, (float)i / 54.0f) / sc);
    }
    if (hot == P_REL)
        sh_pk_adsr(t, 128, 100, 100, 36, hot, hc);        /* (over the second cycle, a moment) */
}
#endif

#if FELUCCA_VOICE
/* VOICE: the vowel as its spectrum: the four resonators' cascade (vowel_at's F1 F2 F3 and the fixed F4, SHIFT's ratio,
 * Q's bandwidths) as an envelope over 0 .. 4 kHz, a sung C3's harmonics under it; TALK: the second vowel's envelope
 * faint, where the mouth goes. Above, A E I O U with VOWL's place (and VOWL2's, a ring, with TALK) */
static void sh_pk_vowel(const int16_t *p, int32_t pos, float *f, float *bw)
{
    int32_t fa[3], ba[3], j;
    float ratio = (float)fmt_ratio(p[P_E3] * 16) * (1.0f / 65536.0f);
    float bws = (float)fmt_ratio((64 - p[P_E6]) * 4) * (1.0f / 65536.0f);
    vowel_at(pos, fa, ba);
    for (j = 0; j < 3; j++) {
        f[j] = (float)fa[j] * (1.0f / 16.0f) * ratio;
        bw[j] = (float)ba[j] * (1.0f / 16.0f) * ratio * bws;
    }
    f[3] = 3300.0f * ratio;
    bw[3] = 250.0f * ratio * bws;
}
#endif
static float sh_pk_formants(const float *f, const float *bw, float hz)   /* the cascade's gain at hz, dB */
{
    uint32_t j;
    float db = 0.0f;
    for (j = 0; j < 4u; j++) {
        float r = hz / f[j], a = 1.0f - r * r, q = r * bw[j] / f[j];
        db -= 3.0103f * sh_log2(a * a + q * q + 1e-9f);
    }
    return db;
}
#if FELUCCA_VOICE
static void sh_pic_voice(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    const int16_t *p = t->p;
    float f[4], bw[4], g[4], gw[4], top = -99.0f, y, x;
    uint32_t i, k;
    int talk = p[P_E2] != 0;
    static const char *const V[5] = {"A", "E", "I", "O", "U"};
    sh_pk_vowel(p, p[P_E0] << 8, f, bw);
    sh_pk_vowel(p, p[P_E1] << 8, g, gw);
    for (i = 0; i <= 64u; i++) {                        /* the loudest point: the envelope's top row */
        float d = sh_pk_formants(f, bw, 30.0f + (float)i * 62.0f);
        top = d > top ? d : top;
    }
#define SH_VX(hz) (16.0f + (hz) * (208.0f / 4000.0f))
#define SH_VY(db) (72.0f + (top - (db)) * 2.5f)
    for (i = 0; i < 5u; i++)
        sh_text_c(40 + (int32_t)i * 40, 22, &AF_S, V[i], SC_MID);
    sh_rect(40, 40, 160, 1, SC_FAINT);
    if (talk) {
        sh_line(40.0f + (float)p[P_E0] * (160.0f / 127.0f), 40.0f, 40.0f + (float)p[P_E1] * (160.0f / 127.0f), 40.0f, m->color);
        sh_ring(40.0f + (float)p[P_E1] * (160.0f / 127.0f), 40.5f, 4.0f, m->color);
    }
    sh_disc(40.0f + (float)p[P_E0] * (160.0f / 127.0f), 40.5f, 3.5f, m->color);
    for (k = 1; 130.8f * (float)k < 4000.0f; k++) {     /* C3's harmonics, as loud as the envelope lets them */
        float hz = 130.8f * (float)k, d = sh_pk_formants(f, bw, hz);
        y = SH_VY(d);
        if (y < 172.0f)
            sh_rect((int32_t)SH_VX(hz), (int32_t)y, 1, 172 - (int32_t)y, sh_mix(SC_BG, m->color, 45));
    }
    if (talk) {
        sh_pk_pen(&pn, sh_mix(SC_BG, SC_BONE, 35));
        for (i = 0; i <= 80u; i++) {
            x = 25.0f * (float)i + 10.0f;
            y = SH_VY(sh_pk_formants(g, gw, x));
            sh_pk_to(&pn, SH_VX(x), y > 172.0f ? 172.0f : y < 58.0f ? 58.0f : y);
        }
    }
    sh_pk_pen(&pn, m->color);
    for (i = 0; i <= 80u; i++) {
        x = 25.0f * (float)i + 10.0f;
        y = SH_VY(sh_pk_formants(f, bw, x));
        sh_pk_to(&pn, SH_VX(x), y > 172.0f ? 172.0f : y);
    }
    sh_rect(16, 172, 208, 1, SC_MID);
    for (i = 0; i < 3u; i++) {                          /* the formants named over their peaks */
        char b[4] = {'F', (char)('1' + i), 0, 0};
        float d = sh_pk_formants(f, bw, f[i]);
        if (f[i] < 3900.0f)
            sh_text_c((int32_t)SH_VX(f[i]), (int32_t)SH_VY(d) - 16, &AF_S, b, SC_BONE);
    }
    sh_text(16, 176, &AF_S, "0", SC_MID);
    sh_text_r(224, 176, &AF_S, "4 KHZ", SC_MID);
#undef SH_VX
#undef SH_VY
}
#endif

#if FELUCCA_WHEEL
/* WHEEL: its nine tonewheels, each with as many teeth as its partial is high (four a harmonic of the note), lit by
 * its bar (REG + SUB / BODY / TOP, drw_level), the level inside, the footage under; PERC's wheel ringed, the 1' off
 * under it; below, two cycles of the wave the bars sum to (DRW_LV's 3 dB steps) */
static void sh_pic_wheel(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    static const float RATIO[DRW_NP] = {0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f};
    static const char *const FT[DRW_NP] = {"16", "5.3", "8", "4", "2.7", "2", "1.6", "1.3", "1"};
    const int16_t *p = t->p;
    uint32_t perc = (uint32_t)p[P_E4] % 7u, k, i, pk = perc ? 3u + ((perc - 1u) & 1u) : DRW_NP;
    float lv[DRW_NP], sum = 0.0f;
    char b[4];
    for (k = 0; k < DRW_NP; k++) {
        int32_t l = drw_level(p, k);
        float cx = 24.0f + 24.0f * (float)k, cy = 58.0f, px = 0.0f, py = 0.0f;
        uint32_t teeth = (uint32_t)(RATIO[k] * 4.0f + 0.5f);
        int off = perc && k == DRW_NP - 1u;
        uint16_t c = off || !l ? SC_FAINT : sh_mix(SC_MID, m->color, 20 + l * 10);
        lv[k] = off ? 0.0f : (float)DRW_LV[l] * (1.0f / 32767.0f);
        sum += lv[k];
        for (i = 0; i <= 48u; i++) {                    /* the rim, toothed */
            float a = SH_TAU * (float)i / 48.0f, r = 9.0f + 1.4f * sh_cos(a * (float)teeth);
            float x = cx + r * sh_cos(a), y = cy + r * sh_sin(a);
            if (i)
                sh_line(px, py, x, y, c);
            px = x;
            py = y;
        }
        if (k == pk)
            sh_ring(cx, cy, 13.0f, RGB(224, 137, 74));
        b[0] = (char)('0' + (off ? 0 : l));
        b[1] = 0;
        sh_text_c((int32_t)cx, 51, &AF_S, b, l && !off ? SC_BONE : SC_MID);
        sh_text_c((int32_t)cx, 74, &AF_S, FT[k], SC_DIM);
    }
    sh_text(8, 22, &AF_S, N_DRW_REG[(uint32_t)p[P_E0] & 15u], m->color);
    {
        char s[32];
        s[0] = 0;
        if (perc) {
            str_cpy(s, "PERC ", sizeof s);
            str_cpy(s + str_len(s), N_DRW_PERC[perc], sizeof s - str_len(s));
        }
        if (p[P_E7] % 3) {
            str_cpy(s + str_len(s), s[0] ? "  ROTOR " : "ROTOR ", sizeof s - str_len(s));
            str_cpy(s + str_len(s), N_DRW_ROTR[p[P_E7] % 3], sizeof s - str_len(s));
        }
        sh_text_r(232, 22, &AF_S, s, SC_DIM);
    }
    sh_rect(12, 136, 216, 1, SC_FAINT);
    {   /* two cycles of the 8', scaled to its own peak */
        float w[145], pk = 0.001f;
        for (i = 0; i <= 144u; i++) {
            float u = (float)i * (2.0f / 144.0f), s = 0.0f;
            for (k = 0; k < DRW_NP; k++)
                if (lv[k] > 0.0f)
                    s += lv[k] * sh_sin(SH_TAU * RATIO[k] * u);
            w[i] = s;
            pk = s > pk ? s : -s > pk ? -s : pk;
        }
        sh_pk_pen(&pn, m->color);
        for (i = 0; i <= 144u; i++)
            sh_pk_to(&pn, 12.0f + 1.5f * (float)i, 136.0f - 40.0f * w[i] / pk);
    }
    (void)sum;
}
#endif

/* NOISE: the noise through its filter as an analyser would show it: 20 Hz .. 20 kHz, the source's colour (COLR:
 * white, pink, brown) through the SVF (FREQ, RES's morph from low-pass to band-pass, as noise_render mixes them),
 * columns jittered as noise is; DUST sparse dots, LFSR a clocked register's sinc, META a tone's harmonics; the
 * filter's own curve over it and FREQ a bone tick */
static float sh_pk_nz(const int16_t *p, float hz, float fc)   /* dB at hz, the source's tilt and the filter */
{
    float colr = (float)p[P_E1] * (1.0f / 127.0f), slope = colr <= 0.5f ? -6.0f * colr : -3.0f - 6.0f * (colr - 0.5f);
    float k = (float)(8192 - p[P_E3] * 7800 / 127) * (1.0f / 4096.0f), mix = (float)p[P_E3] * (1.0f / 127.0f);
    float w = hz / fc, a = 1.0f - w * w, d = a * a + k * k * w * w, bg = 1.5f * sh_exp2(0.5f * sh_log2(k));
    /* LP = 1 / D, BP = j w / D: (1 - mix) LP + mix bg BP, its magnitude squared over |D|^2 */
    float re = 1.0f - mix, im = mix * bg * w, g2 = (re * re + im * im) / (d > 1e-9f ? d : 1e-9f);
    return 3.0103f * sh_log2(g2 + 1e-9f) + slope * sh_log2(hz / 1000.0f);
}
#if FELUCCA_NOISE
static void sh_pic_noise(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)p[P_E0] & 3u, i, r = 0x9E3779B9u;
    float fc = (float)CUTOFF_HZ[p[P_E2] & 127], top = -99.0f, fclk = 261.6f * sh_exp2((float)p[P_E7] / 12.0f);
    static const char *const COL[3] = {"WHITE", "PINK", "BROWN"};
    char b[24];
    for (i = 0; i <= 40u; i++) {                        /* the loudest point: the top row */
        float d = sh_pk_nz(p, 20.0f * sh_exp2((float)i * (9.9658f / 40.0f)), fc);
        top = d > top ? d : top;
    }
#define SH_NY(db) (44.0f + (top - (db)) * 2.2f)
    for (i = 0; i < 3u; i++) {
        static const uint16_t GR[3] = {100, 1000, 10000};
        sh_rect((int32_t)sh_pk_fx((float)GR[i], 16, 224), 40, 1, 132, SC_PANEL);
    }
    if (mode == NZ_META) {                              /* a tone: the key's harmonics, a sequence's sinc */
        uint32_t len = NOISE_SEQ[((uint32_t)p[P_E5] >> 4) & 7u].len, n;
        float f0 = p[P_E7] ? fclk : 261.6f;            /* (a sequence a period of the key, or of CLK above it) */
        for (n = 1; f0 * (float)n < 20000.0f && n < 160u; n++) {
            float hz = f0 * (float)n, x = (float)n / (float)len, s = sh_sin(3.1415927f * x), d, y;
            s = x > 0.001f ? s / (3.1415927f * x) : 1.0f;
            d = sh_pk_nz(p, hz, fc) + 3.0103f * sh_log2(s * s + 1e-6f);
            y = SH_NY(d);
            if (y < 172.0f && hz > 20.0f)
                sh_rect((int32_t)sh_pk_fx(hz, 16, 224), (int32_t)y, 1, 172 - (int32_t)y,
                        sh_mix(SC_BG, m->color, n % 8u == 1u ? 90 : 45));
        }
    } else {
        for (i = 0; i < 70u; i++) {                     /* columns, jittered */
            float hz = 20.0f * sh_exp2(((float)i + 0.5f) * (9.9658f / 70.0f)), d = sh_pk_nz(p, hz, fc), y;
            r = r * 1664525u + 1013904223u;
            d += (float)(r >> 28) * 0.8f - 6.0f;
            if (mode == NZ_LFSR) {                      /* a register clocked at fclk: sinc^2, nulls at its rate */
                float x = hz / fclk, s = sh_sin(3.1415927f * x);
                s = x > 0.001f ? s / (3.1415927f * x) : 1.0f;
                d += 3.0103f * sh_log2(s * s + 1e-4f);
            }
            y = SH_NY(d);
            if (y >= 172.0f)
                continue;
            if (mode == NZ_DUST) {                      /* impulses: dots, as sparse as DENS keeps them */
                if ((r >> 20 & 127u) <= (uint32_t)p[P_E5] + 8u)
                    sh_disc(sh_pk_fx(hz, 16, 224), y, 1.2f, m->color);
            } else {
                sh_rect((int32_t)sh_pk_fx(hz, 16, 224), (int32_t)y, 2, 172 - (int32_t)y, sh_mix(SC_BG, m->color, 70));
            }
        }
    }
    sh_pk_pen(&pn, SC_BONE);                                 /* the filter's own curve, the colour without jitter */
    for (i = 0; i <= 52u; i++) {
        float hz = 20.0f * sh_exp2((float)i * (9.9658f / 52.0f)), y = SH_NY(sh_pk_nz(p, hz, fc));
        sh_pk_to(&pn, sh_pk_fx(hz, 16, 224), y > 172.0f ? 172.0f : y < 40.0f ? 40.0f : y);
    }
#undef SH_NY
    sh_rect(16, 172, 208, 1, SC_MID);
    if (fc > 20.0f && fc < 20000.0f)
        sh_rect((int32_t)sh_pk_fx(fc, 16, 224), 168, 1, 9, SC_BONE);
    str_cpy(b, N_NOISE_MODE[mode], sizeof b);
    sh_text(16, 22, &AF_S, b, m->color);
    sh_text_r(224, 22, &AF_S, COL[p[P_E1] < 43 ? 0 : p[P_E1] < 86 ? 1 : 2], SC_DIM);
    sh_text(16, 176, &AF_S, "20", SC_MID);
    sh_text_r(224, 176, &AF_S, "20K", SC_MID);
}
#endif

/* the zone set (or user slot) si plays for note: as sample_note_on picks it (a user slot: the narrowest), 0 none */
static const smp_zone_t *sh_pk_zone(uint32_t si, uint32_t note)
{
    const smp_zone_t *best = 0;
    uint32_t i, width = 128u;
    si %= SMP_NALL;
    if (smp_set_missing(si))
        return 0;
    if (si < SMP_NSETS) {
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (note >= SMP_ZONES[set->z0 + i].lo && note <= SMP_ZONES[set->z0 + i].hi)
                return &SMP_ZONES[set->z0 + i];
        return &SMP_ZONES[set->z0];
    }
    for (i = 0; i < usr_nz[si - SMP_NSETS]; i++) {
        const smp_zone_t *z = &usr_zone[si - SMP_NSETS][i];
        if (note >= z->lo && note <= z->hi && z->hi - z->lo <= width) {
            best = z;
            width = z->hi - z->lo;
        }
    }
    return best;
}
/* an outline of IMA ADPCM data: the coder's step size at its largest in each of 64 stretches (the step follows the
 * signal's level; only the step index is decoded, a table add and a clamp a nibble, no state kept between frames:
 * the pictures add no RAM). The index can only be followed from a known decoder state, so long data is read in
 * windows from the states the firmware already keeps, SLICE's grid (slc_src_t: 128 points over every user slot and
 * BREAK and PIANO), SH_PK_WIN nibbles at each: 4096 nibbles a redraw at most. Data up to SH_PK_FULL nibbles is read
 * whole, which is also the bound for the zones no grid covers (the built-in FLUTE and SAX, 20947 each) */
#define SH_PK_WIN 32u
#define SH_PK_FULL 21000u
/* nibbles from .. to of the data at off, the index from idx: the peak into *pk; the index at the end */
static int32_t sh_pk_scan(uint32_t off, uint32_t from, uint32_t to, int32_t idx, uint8_t *pk)
{
    uint32_t i;
    for (i = from; i < to; i++) {
        uint32_t byte = SMP_DATA[off + (i >> 1)], code = (i & 1u) ? byte >> 4 : byte & 15u;
        idx += IMA_IDX[code & 7u];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        if ((uint32_t)idx > *pk)
            *pk = (uint8_t)idx;
    }
    return idx;
}
static void sh_pk_outline(uint8_t *env, uint32_t off, uint32_t n)   /* all of it, from index 0 */
{
    uint32_t b;
    int32_t idx = 0;
    for (b = 0; b < 64u; b++) {
        env[b] = 0;
        idx = sh_pk_scan(off, n / 64u * b, b == 63u ? n : n / 64u * (b + 1u), idx, &env[b]);
    }
}
#if FELUCCA_SLICE
/* from SLICE's grid: the material's samples a .. a + n (a zone of it, or all of it) as 64 stretches, a window at each
 * grid point inside; a stretch no point falls in takes the one before's */
static void sh_pk_grid(uint8_t *env, const slc_src_t *s, uint32_t a, uint32_t n)
{
    uint32_t g, b;
    for (b = 0; b < 64u; b++)
        env[b] = 0;
    for (g = 0; g < SLC_GRID; g++) {
        uint32_t p = slc_gpos(s, g), st = s->grid[g], sg = st >> 24, rel, end;
        if (p < a || p >= a + n || sg >= s->nseg)
            continue;
        rel = p - s->seg[sg].at;
        end = rel + SH_PK_WIN < s->seg[sg].n ? rel + SH_PK_WIN : s->seg[sg].n;
        sh_pk_scan(s->seg[sg].off, rel, end, (int32_t)((st >> 16) & 127u), &env[(p - a) * 64u / n]);   /* (n <= 160k) */
    }
    for (b = 1; b < 64u; b++)
        if (!env[b])
            env[b] = env[b - 1u];
}
/* the grid that covers zone z (a user slot's zone, or the PIANO zone SLICE plays) and where z starts in it, 0 none */
static const slc_src_t *sh_pk_gridof(const smp_zone_t *z, uint32_t *at)
{
    uint32_t k, g;
    for (k = 0; k <= SMP_USER_SLOTS; k++) {
        const slc_src_t *c = k < SMP_USER_SLOTS ? (usr_nz[k] && slc_usr[k].len ? &slc_usr[k] : 0) :
                             SLC_PIANO.len ? &SLC_PIANO : 0;
        for (g = 0; c && g < c->nseg; g++)
            if (c->seg[g].off == z->off && c->seg[g].n == z->n) {
                *at = c->seg[g].at;
                return c;
            }
    }
    return 0;
}
#endif
static void sh_pk_zone_outline(uint8_t *env, const smp_zone_t *z)
{
#if FELUCCA_SLICE
    uint32_t at = 0;
    const slc_src_t *s = z->n > SH_PK_FULL ? sh_pk_gridof(z, &at) : 0;
    if (s) {
        sh_pk_grid(env, s, at, z->n);
        return;
    }
#endif
    sh_pk_outline(env, z->off, z->n > 163840u ? 163840u : z->n);
}
static void sh_pk_bars(const uint8_t *env, int32_t x0, int32_t x1, int32_t yc, int32_t h, int32_t sh, uint16_t c)
{
    uint32_t i;
    int32_t mx = 1;
    for (i = 0; i < 64u; i++)
        mx = IMA_STEP[env[i]] > mx ? IMA_STEP[env[i]] : mx;
    for (i = 0; i < 64u; i++) {
        int32_t xa = x0 + (int32_t)i * (x1 - x0) / 64, xb = x0 + (int32_t)(i + 1u) * (x1 - x0) / 64;
        int32_t v = (int32_t)IMA_STEP[env[i]] * 32767 / mx, a;
        v = (v >> sh) << sh;                            /* BITS: the bits it removes */
        a = v * h / 32767;
        if (a < 1)
            a = env[i] ? 1 : 0;
        if (a)
            sh_rect(xa, yc - a, xb - xa - 1 > 1 ? xb - xa - 1 : 1, 2 * a, c);
    }
}
static void sh_pk_strip(const smp_zone_t *z, int32_t x0, int32_t x1, int32_t yc, int32_t h, int32_t sh, uint16_t c)
{
    uint8_t env[64];
    sh_pk_zone_outline(env, z);
    sh_pk_bars(env, x0, x1, yc, h, sh, c);
}

#if FELUCCA_SAMPLE
/* SAMPLE: the instrument's name, the zone C4 plays as its own outline (decoded once), its loop marked when LOOP is
 * on and the zone loops (else ONE SHOT), BITS stepping it; under it the set's zones across the keyboard, C4's lit */
static void sh_pic_sample(const sh_mode_t *m, const track_t *t)
{
    const int16_t *p = t->p;
    uint32_t si = (uint32_t)p[P_E0] % SMP_NALL, i, nz;
    const smp_zone_t *z = sh_pk_zone(si, 60u), *zs;
    char b[24];
    sh_text_c(120, 26, &AF_L, SMP_ALL_NAMES[si], m->color);
    if (!z) {
        sh_text_c(120, 92, &AF_S, si < SMP_NSETS ? "NOT IN THIS BUILD" : "EMPTY SLOT", SC_DIM);
        sh_text_c(120, 108, &AF_S, "THE KEYS PLAY A SINE", SC_MID);
        return;
    }
    if (p[P_E3] && z->looped && z->n) {                 /* the loop: its stretch behind, its ends bone */
        int32_t la = 16 + (int32_t)(z->ls * 208u / z->n), lb = 16 + (int32_t)(z->le * 208u / z->n);   /* (n <= 160k) */
        sh_rect(la, 74, lb - la + 1, 76, SC_PANEL);
        sh_rect(la, 74, 1, 76, SC_BONE);
        sh_rect(lb, 74, 1, 76, SC_BONE);
    }
    sh_rect(16, 112, 208, 1, SC_FAINT);
    sh_pk_strip(z, 16, 224, 112, 34, p[P_E2] / 10, m->color);
    {   /* its length at its root, and the root */
        uint32_t ms = z->rate ? (uint32_t)((float)z->n * (65536000.0f / 44100.0f) / (float)z->rate) : 0u;
        int32_t rn = (z->root16 + 8) / 16;
        sh_pk_fix(b, (int32_t)(ms / 10u), 2);
        str_cpy(b + str_len(b), p[P_E3] && z->looped ? " S  LOOP" : " S  ONE SHOT", sizeof b - str_len(b));
        sh_text(16, 150, &AF_S, b, SC_MID);
        str_cpy(b, "ROOT ", sizeof b);
        str_cpy(b + str_len(b), N_NOTE[(uint32_t)(rn + 120) % 12u], sizeof b - str_len(b));
        fmt_int(b + str_len(b), rn / 12 - 1);
    }
    nz = si < SMP_NSETS ? SMP_SETS[si].nz : usr_nz[si - SMP_NSETS];
    for (i = 0; i < nz; i++) {                          /* the zones, C1 .. C8 across */
        int32_t lo, hi, x0, x1;
        zs = si < SMP_NSETS ? &SMP_ZONES[SMP_SETS[si].z0 + i] : &usr_zone[si - SMP_NSETS][i];
        lo = zs->lo < 24 ? 24 : zs->lo;
        hi = zs->hi > 108 ? 108 : zs->hi;
        if (hi < lo)
            continue;
        x0 = 16 + (lo - 24) * 208 / 85;
        x1 = 16 + (hi + 1 - 24) * 208 / 85;
        sh_rect(x0, 172, x1 - x0 - 1, 4, zs == z ? m->color : (i & 1u) ? SC_MID : SC_DIM);
    }
    str_cpy(b + str_len(b), "  ", sizeof b - str_len(b));
    fmt_int(b + str_len(b), (int32_t)nz);
    str_cpy(b + str_len(b), nz == 1u ? " ZONE" : " ZONES", sizeof b - str_len(b));
    sh_text_r(224, 150, &AF_S, b, SC_MID);
    sh_rect(16 + (60 - 24) * 208 / 85, 168, 1, 12, SC_BONE);   /* C4 */
}
#endif

#if FELUCCA_GRAIN
/* GRAIN: the source's zone as its outline (the strip), POS on it in bone and SPRD's reach around it; above, the
 * grains of the last 0.4 s (DENS a second) as windows as wide as SIZE reads at C4, each where it reads, raised by its
 * pitch (PTCH and RAND's detune, +-1 octave at most), the reversed ones (up to half, by RAND) in bone. A fixed draw of
 * random numbers: the picture moves with the knobs, not by itself */
static void sh_pic_grain(const sh_mode_t *m, const track_t *t)
{
    sh_pk_t pn;
    const int16_t *p = t->p;
    uint32_t si = (uint32_t)p[P_E0] % SMP_NALL, i, n, r = 0x2545F491u;
    const smp_zone_t *z = sh_pk_zone(si, 60u);
    float rate = sh_exp2((float)p[P_E3] * (10.0f / 192.0f)), len = 0.01f * sh_exp2((float)p[P_E2] / 24.0f);
    float pos = (float)p[P_E1] * (1.0f / 127.0f), sp = (float)p[P_E5] * (516.0f / 65536.0f), w, ov, amt, gn;
    char b[24];
    sh_text(16, 22, &AF_S, SMP_ALL_NAMES[si], m->color);
    fmt_int(b, (int32_t)(rate + 0.5f));
    str_cpy(b + str_len(b), "/S  ", sizeof b - str_len(b));
    fmt_int(b + str_len(b), (int32_t)(len * 1000.0f + 0.5f));
    str_cpy(b + str_len(b), " MS", sizeof b - str_len(b));
    sh_text_r(224, 22, &AF_S, b, SC_DIM);
    if (!z) {
        sh_text_c(120, 92, &AF_S, si < SMP_NSETS ? "NOT IN THIS BUILD" : "EMPTY SLOT", SC_DIM);
        sh_text_c(120, 108, &AF_S, "THE KEYS PLAY A SINE", SC_MID);
        return;
    }
    {   /* SPRD's reach, the strip, POS */
        float a = pos - sp * 0.5f, c = pos + sp * 0.5f;
        a = a < 0.0f ? 0.0f : a;
        c = c > 1.0f ? 1.0f : c;
        sh_rect(16 + (int32_t)(a * 208.0f), 150, (int32_t)((c - a) * 208.0f) + 1, 30, SC_PANEL);
        sh_pk_strip(z, 16, 224, 165, 13, 0, SC_MID);
        sh_rect(16 + (int32_t)(pos * 208.0f), 146, 1, 38, SC_BONE);
    }
    /* a grain's share of the zone: SIZE's seconds at C4's rate against the zone's length */
    w = z->n ? len * 44100.0f * sh_exp2(((float)(60 * 16 - z->root16) + (float)p[P_E4] * 16.0f) / 192.0f) *
                   ((float)z->rate / 65536.0f) / (float)z->n : 1.0f;
    w = w > 1.0f ? 1.0f : w;
    ov = rate * len;                                    /* sounding at once, on average: their level 1 / sqrt */
    gn = (float)GR_NORM[ov >= 12.0f ? 12 : (uint32_t)ov] * (1.0f / 32767.0f);
    amt = (float)(p[P_E6] * p[P_E6]) * (12.0f / (127.0f * 127.0f));   /* RAND's detune, semitones */
    n = (uint32_t)(rate * 0.4f) + 1u;
    n = n > GR_NG ? GR_NG : n;
    sh_rect(16, 92, 208, 1, SC_FAINT);                  /* the note's pitch */
    for (i = 0; i < n; i++) {
        float u, d, x0, gw, y0, h;
        uint32_t k, rev;
        r = r * 1664525u + 1013904223u;
        u = pos + sp * ((float)(r >> 16) * (1.0f / 65536.0f) - 0.5f);
        r = r * 1664525u + 1013904223u;
        d = (float)p[P_E4] + amt * ((float)(r >> 16) * (1.0f / 32768.0f) - 1.0f);
        rev = ((r >> 8) & 255u) < (uint32_t)p[P_E6];
        u = u < 0.0f ? 0.0f : u > 1.0f - w ? 1.0f - w : u;
        gw = w * 208.0f < 6.0f ? 6.0f : w * 208.0f;
        x0 = 16.0f + u * 208.0f;
        y0 = 92.0f - d * 2.5f;
        y0 = y0 < 66.0f ? 66.0f : y0 > 140.0f ? 140.0f : y0;
        h = 4.0f + 24.0f * gn;
        sh_line(x0 + gw * 0.5f, y0 + 2.0f, x0 + gw * 0.5f, 146.0f, sh_mix(SC_BG, m->color, 20));
        sh_pk_pen(&pn, rev ? SC_BONE : m->color);
        for (k = 0; k <= 12u; k++) {                    /* its Hann window */
            float s = sh_sin(3.1415927f * (float)k / 12.0f);
            sh_pk_to(&pn, x0 + gw * (float)k / 12.0f, y0 - h * s * s);
        }
        sh_rect((int32_t)x0, (int32_t)y0, (int32_t)gw, 1, rev ? SC_BONE : m->color);
    }
}
#endif

#if FELUCCA_SLICE
/* SLICE: its material as one outline (decoded once, its zones one after another as slc_dec_next reads them), cut
 * where DIV cuts it (equal, AUTO's onsets or MAN's by hand: slc_bounds), the slice C4 plays lit (START, ROOT),
 * REV and MODE said */
static void sh_pic_slice(const sh_mode_t *m, const track_t *t)
{
    const int16_t *p = t->p;
    uint32_t src = slc_src_of(p), div = (uint32_t)clamp(p[P_E1], 0, 5), n, j, a, b, st, c4;
    const slc_src_t *sr = slc_get(src);
    char s[32];
    uint8_t env[64];
    sh_text(16, 22, &AF_S, N_SLC_SRC[src], m->color);
    if (!sr) {
        sh_text_c(120, 92, &AF_S, "NO MATERIAL IN IT", SC_DIM);
        sh_text_c(120, 108, &AF_S, "THE KEYS PLAY A SINE", SC_MID);
        return;
    }
    sh_pk_grid(env, sr, 0, sr->len);                    /* (its zones one after another, from its grid) */
    n = slc_count(sr, div);
    n = n ? n : 1u;
    c4 = slc_note_slice(p, (uint32_t)(SLC_BASE + p[P_ROOT]), n);
    slc_bounds(sr, div, c4, &a, &b, &st);
    sh_rect(16 + (int32_t)(a * 208u / sr->len), 50, (int32_t)((b - a) * 208u / sr->len) + 1, 96, SC_PANEL);   /* (len
                                                         * <= 160k: 32 bits) */
    sh_rect(16, 98, 208, 1, SC_FAINT);
    sh_pk_bars(env, 16, 224, 98, 42, 0, SC_MID);
    for (j = 0; j < n && j < 32u; j++) {                /* the cuts, C4's slice in the mode's colour */
        slc_bounds(sr, div, j, &a, &b, &st);
        sh_rect(16 + (int32_t)(a * 208u / sr->len), 50, 1, 96, j == c4 ? m->color : SC_DIM);
        if (j == c4)
            sh_rect(16 + (int32_t)(b * 208u / sr->len) - 1, 50, 1, 96, m->color);
    }
    fmt_int(s, (int32_t)n);
    str_cpy(s + str_len(s), n == 1u ? " SLICE" : " SLICES", sizeof s - str_len(s));
    if (div >= SLC_DIV_AUTO)
        str_cpy(s + str_len(s), div == SLC_DIV_AUTO ? " AUTO" : " BY HAND", sizeof s - str_len(s));
    sh_text_r(224, 22, &AF_S, s, SC_DIM);
    str_cpy(s, "C4 PLAYS ", sizeof s);
    fmt_int(s + str_len(s), (int32_t)c4 + 1);
    sh_text(16, 152, &AF_S, s, m->color);
    str_cpy(s, N_SLC_MODE[(uint32_t)clamp(p[P_E4], 0, 2)], sizeof s);
    if (p[P_E5])
        str_cpy(s + str_len(s), "  REVERSE", sizeof s - str_len(s));
    sh_text_r(224, 152, &AF_S, s, SC_MID);
}
#endif

/* DRUMS (on the keys): the kit's eight pieces as pads, each named as this KIT plays it (drum_lane_name) with the note
 * that strikes it (DRUM_LANE_NOTE); KICK's PUNCH or ROUND said. A pad lights as its drum rings (its key, or its lane
 * under RUDIMENT), fading as it does */
static void sh_pic_drums(const sh_mode_t *m, const track_t *t)
{
    uint32_t l, i, hot;
    uint16_t hc = 0;
    sh_live_t L;
    char b[24];
    str_cpy(b, "KIT ", sizeof b);
    str_cpy(b + 4, N_DRUM_KIT[(uint32_t)clamp(t->p[P_E0], 0, DK_COUNT - 1)], sizeof b - 4);
    sh_text(12, 24, &AF_M, b, m->color);
    sh_text_r(228, 26, &AF_S, t->p[P_E6] ? "ROUND KICK" : "PUNCH KICK", SC_DIM);
    sh_live_of(t, &L);
    hot = sh_hot_main(&hc);
    for (l = 0; l < DV_NLANE; l++) {
        int32_t x = 12 + (int32_t)(l % 4u) * 55, y = 54 + (int32_t)(l / 4u) * 60, n = DRUM_LANE_NOTE[l];
        float lv = 0.0f;
        for (i = 0; i < L.n; i++)                       /* the pad lit as its drum rings, fading with it */
            if (L.note[i] == (uint8_t)n || L.note[i] == shell_lane_note_i[0][l])
                lv = L.lv[i] > lv ? L.lv[i] : lv;
        sh_rect(x, y, 51, 52, SC_PANEL);                /* (the names' edges are drawn against the panel) */
        if (lv > 0.02f) {                               /* ringing: its top edge thick and bright, its level under */
            sh_rect(x, y, 51, 2 + (int32_t)(4.0f * lv), sh_mix(m->color, SC_BONE, (int32_t)(60.0f * lv)));
            sh_rect(x + 4, y + 47, (int32_t)(43.0f * lv) + 1, 2, m->color);
        } else
            sh_rect(x, y, 51, 2, hot != SH_NONE ? hc : l ? sh_mix(SC_BG, m->color, 55) : m->color);   /* (MAIN's four:
                                                         * every drum's, lit as one) */
        sh_text_c(x + 25, y + 12, &AF_S, drum_lane_name(t, l), SC_BONE);
        str_cpy(b, N_NOTE[n % 12], sizeof b);
        fmt_int(b + str_len(b), n / 12 - 1);
        sh_text_c(x + 25, y + 30, &AF_S, b, SC_MID);
    }
}

#if FELUCCA_LOFI
/* LOFI: two cycles of the chip's wave at C4, sample by sample as lofi_render makes it (the wave, its bits, CRSH's
 * held samples, TONE's low-pass), the bit levels behind; STEP: its volume staircase under it (DCY, 240 Hz frames) */
static void sh_pic_lofi(const sh_mode_t *m, const track_t *t)
{
    static const uint8_t STEP_DUTY[4] = {1, 2, 4, 6};
    const int16_t *p = t->p;
    uint32_t chip = (uint32_t)clamp(p[P_E0], 0, 4), wave = (uint32_t)clamp(p[P_E1], 0, 4), i, stepc = chip == CHIP_STEP;
    uint32_t duty = stepc ? 0x20000000u * STEP_DUTY[(p[P_E2] / 32) & 3] : 0x20000000u * (1u + (uint32_t)p[P_E2] / 32u);
    int32_t crush = stepc ? 0 : p[P_E3], bits = chip == CHIP_8BIT ? 8 : chip == CHIP_1BIT ? 1 : 4;
    int32_t hold = 1 + crush / 8 + (chip == 1u ? 1 : 0), lpk = 3000 + (p[P_E7] << 8), held = 0, cnt = 0, lp = 0, wdc = 0;
    uint32_t ph0 = 0, inc = 25414007u, lfsr = 0x7FFF;   /* (a cycle in 169 samples: C4) */
    const uint8_t *wr = WRAM[((uint32_t)p[P_E2] >> 3) & 15u];
    float py = 0.0f, px = 16.0f;
    char b[24];
    lpk = lpk > 32767 ? 32767 : lpk;
    if (wave == RW_WRAM) {
        int32_t sum = 0;
        for (i = 0; i < 16u; i++)
            sum += bits == 1 ? ((wr[i] >> 7) + ((wr[i] >> 3) & 1)) * 2 - 2 : (wr[i] >> 4) + (wr[i] & 15) - 15;
        wdc = bits == 1 ? sum * 32767 / 32 : sum * 2 * Q4 / 32;
    }
    if (bits <= 4)                                      /* the levels the bits allow */
        for (i = 0; i < (1u << bits); i++)
            sh_rect(16, 58 + (int32_t)(i * 64u / ((1u << bits) - 1u)), 208, 1, SC_PANEL);
    sh_rect(16, 90, 208, 1, SC_FAINT);
    for (i = 0; i < 2u * 169u + 169u; i++) {            /* a cycle to settle the low-pass, then two drawn */
        int32_t s;
        if (--cnt <= 0) {
            uint32_t q = bits < 16;
            cnt = hold;
            switch (wave) {
            case RW_TRI:
                if (stepc) {
                    uint32_t k = ph0 >> 27;
                    s = (int32_t)(k < 16u ? 15u - k : k - 16u) * (2 * Q4) - 15 * Q4;
                    q = 0;
                } else {
                    s = osc_tri(ph0);
                }
                break;
            case RW_SAW:
                s = (int32_t)(ph0 >> 16) - 32768;
                break;
            case RW_NOIS:
                if ((ph0 + inc * (uint32_t)hold) < ph0 || chip == 3u)
                    lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 1)) & 1u) << 14);
                s = (lfsr & 1u) ? 32767 : -32768;
                break;
            case RW_WRAM: {
                uint32_t k = ph0 >> 27, bb = wr[k >> 1];
                bb = (k & 1u) ? bb & 15u : bb >> 4;
                s = (bits == 1 ? (bb >= 8u ? 32767 : -32767) : ((int32_t)bb * 2 - 15) * Q4) - wdc;
                q = 0;
                break;
            }
            default:
                s = (ph0 < duty ? 32767 : -32768) - ((int32_t)(duty >> 16) - 32768);
                break;
            }
            if (q)
                s = ((s + (1 << (15 - bits))) >> (16 - bits)) << (16 - bits);
            if (crush > 64)
                s = (s >> 12) << 12;
            held = s;
        }
        ph0 += inc;
        lp += mulq15(held - lp, lpk);
        if (i >= 169u) {                                /* a step: across, then up or down */
            float x = 16.0f + (float)(i - 169u) * (208.0f / 338.0f), y = 90.0f - (float)lp * (32.0f / 32768.0f);
            if (i > 169u) {
                sh_line(px, py, x, py, m->color);
                if (y - py > 0.6f || py - y > 0.6f)
                    sh_line(x, py, x, y, m->color);
            }
            px = x;
            py = y;
        }
    }
    str_cpy(b, N_CHIP[chip], sizeof b);
    sh_text(16, 22, &AF_S, b, m->color);
    str_cpy(b, wave == RW_WRAM ? N_WRAM[((uint32_t)p[P_E2] >> 3) & 15u] : N_RWAVE[wave], sizeof b);
    sh_text_r(224, 22, &AF_S, b, SC_DIM);
    b[0] = 0;
    if (p[P_E6]) {
        static const char *const A[4] = {"", "ARP 0 12", "ARP 0 4 7", "ARP 0 3 7"};
        str_cpy(b, A[p[P_E6] & 3], sizeof b);
    }
    if (p[P_E4])
        str_cpy(b + str_len(b), b[0] ? "  SWEEP" : "SWEEP", sizeof b - str_len(b));
    if (p[P_E5])
        str_cpy(b + str_len(b), b[0] ? "  VIB" : "VIB", sizeof b - str_len(b));
    sh_text(16, 146, &AF_S, b, SC_MID);
    if (stepc) {                                        /* the stepped volume: 15 .. 0, (period + 1) frames a step */
        int32_t bq = p[P_E3] >> 2, per = bq <= 16 ? bq - 1 : bq - 16, x = 16, v = 15;
        float fw = 208.0f / 240.0f;                     /* a second across: px a frame */
        sh_text_r(224, 146, &AF_S, bq ? (bq > 16 ? "LOOPING DECAY" : "DECAY") : "CONSTANT", SC_MID);
        while (x < 224) {
            int32_t w = bq ? (int32_t)((float)(per + 1) * fw + 0.5f) : 208, y = 186 - v;
            w = w < 1 ? 1 : w;
            sh_rect(x, y, x + w > 224 ? 224 - x : w, 1, m->color);
            x += w;
            if (!bq)
                break;
            if (v)
                v--;
            else if (bq > 16)
                v = 15;
            else
                break;
            if (x < 224)
                sh_rect(x, 186 - v - 1, 1, 2, m->color);
        }
    }
}
#endif
