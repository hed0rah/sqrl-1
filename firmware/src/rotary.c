/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * rotary: see rotary.h */
#include "rotary.h"

#define ROT_FS 44100
/* the rotors' speeds, phase increments a sample (2^32 a turn) */
static const int32_t ROT_H_INC[3] = {0, 80836, 662252};      /* horn: stop, 0.83 Hz, 6.8 Hz */
static const int32_t ROT_D_INC[3] = {0, 65253, 564862};      /* drum: stop, 0.67 Hz, 5.8 Hz */
/* inertia: 2^24 / (tau * ROT_FS), per sample, speeding up and slowing down */
#define ROT_H_UP 1268                        /* horn: tau 0.3 s */
#define ROT_H_DN 761                         /* 0.5 s */
#define ROT_D_UP 254                         /* drum: tau 1.5 s */
#define ROT_D_DN 190                         /* 2 s */
/* the Doppler sweeps, Q8 samples: centre and swing (the horn's mouth ~15 cm from its axis: 19 samples) */
#define ROT_H_C (24 << 8)
#define ROT_H_A (19 << 8)
#define ROT_D_C (24 << 8)
#define ROT_D_A (6 << 8)
#define ROT_H_AM 19661                       /* the horn's loudness swing, Q15: 0.6 */
#define ROT_D_AM 9830                        /* the drum's: 0.3 */
#define ROT_XO 3480                          /* the crossover's one pole, Q15: ~800 Hz */

static inline int32_t rot_sin(uint32_t ph)  /* Q15, a parabola: a rotation's sweep */
{
    int32_t y = (int32_t)ph >> 16, a = y < 0 ? -y : y;
    return (y * (32768 - a)) >> 13;
}
static inline int32_t rot_tap(const rot_t *r, int32_t d)
{
    uint32_t i = (uint32_t)d >> 8;
    int32_t f = d & 255, s0 = r->buf[(r->w - i) & r->mask], s1 = r->buf[(r->w - i - 1u) & r->mask];
    return s0 + (((s1 - s0) * f) >> 8);
}
static int32_t rot_speed(int32_t f, int32_t tgt, int32_t up, int32_t dn, uint32_t n)
{
    int32_t k = tgt > f ? up : dn, d = (int32_t)(((int64_t)(tgt - f) * k * (int32_t)n) >> 24);
    if (!d)
        d = tgt > f ? 1 : tgt < f ? -1 : 0;
    return f + d;
}

void rot_init(rot_t *r, int16_t *buf, uint32_t len)
{
    uint32_t i;
    r->buf = buf;
    r->mask = len - 1u;
    for (i = 0; i < len; i++)
        buf[i] = 0;
    r->w = 0;
    r->hph = 0;
    r->dph = 0x40000000u;
    r->hf = r->df = 0;
    r->lp[0] = r->lp[1] = r->lp[2] = r->lp[3] = 0;
}

/* one listener at angle a (2^32 a turn) on both rotors: horn above the crossover, drum below */
static inline int32_t rot_hear(const rot_t *r, uint32_t a, int32_t bal, int32_t *lph, int32_t *lpd)
{
    uint32_t ha = r->hph + a, da = r->dph + a;
    int32_t hs = rot_sin(ha), hc = rot_sin(ha + 0x40000000u), ds = rot_sin(da), dc = rot_sin(da + 0x40000000u);
    int32_t xh = rot_tap(r, ROT_H_C + ((hs * ROT_H_A) >> 15)), xd = rot_tap(r, ROT_D_C + ((ds * ROT_D_A) >> 15)), h, d;
    *lph += ((xh - *lph) * ROT_XO) >> 15;    /* (each path its own crossover, after its own delay) */
    *lpd += ((xd - *lpd) * ROT_XO) >> 15;
    h = xh - *lph;
    d = *lpd;
    h = (h * (32768 - ((ROT_H_AM * (32768 - hc)) >> 16))) >> 15;   /* the mouth facing the listener at cos = 1 */
    d = (d * (32768 - ((ROT_D_AM * (32768 - dc)) >> 16))) >> 15;
    return (h * bal + d * (128 - bal)) >> 6;
}

void rot_block(rot_t *r, uint8_t speed, uint8_t drive, uint8_t bal, const int32_t *in, int32_t *out, int32_t *side,
               uint32_t n)
{
    uint32_t i, s = speed > ROT_FAST ? ROT_FAST : speed;
    int32_t g = 256 + 10 * (int32_t)(drive > 127 ? 127 : drive), ig = (32768 * 512) / (256 + g);
    int32_t b = bal > 127 ? 128 : bal + (bal >> 6);    /* 0 .. 128 */
    int32_t h0 = r->hf, d0 = r->df, h1, d1, hs, ds;
    h1 = rot_speed(h0, ROT_H_INC[s] << 8, ROT_H_UP, ROT_H_DN, n);
    d1 = rot_speed(d0, ROT_D_INC[s] << 8, ROT_D_UP, ROT_D_DN, n);
    r->hf = h1;
    r->df = d1;
    hs = (h1 - h0) / (int32_t)n;
    ds = (d1 - d0) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t x = in[i], m;
        if (drive) {                     /* the preamp: u - 4/27 u^3 up to 1.5, its level kept */
            int32_t a = x < 0 ? -x : x, u = (a * g) >> 11, u3;
            u = u > 6144 ? 6144 : u;
            u3 = (((u * u) >> 12) * u) >> 12;
            u -= (u3 * 4855) >> 15;
            u = ((u << 3) * ig) >> 15;
            x = x < 0 ? -u : u;
        }
        r->buf[r->w & r->mask] = (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
        m = rot_hear(r, 0, b, &r->lp[0], &r->lp[1]);
        if (side) {
            int32_t q = rot_hear(r, 0x40000000u, b, &r->lp[2], &r->lp[3]);
            out[i] = (m + q) >> 1;
            side[i] = (m - q) >> 1;
        } else
            out[i] = m;
        r->hph += (uint32_t)((h0 + hs * (int32_t)i) >> 8);
        r->dph += (uint32_t)((d0 + ds * (int32_t)i) >> 8);
        r->w++;
    }
}
