/* SPDX-License-Identifier: MIT AND BSD-3-Clause
 * Copyright (c) 2026 hed0rah
 * lpc5220: the TMS5220 generator, written from a reference model of the chip (not published), which follows MAME's
 * tms5220.cpp (the non-"perfect interpolation" path). The tables (ENERGY, PITCH, K,
 * CHIRP, INTERP's chip row) are MAME's tms5110r.hxx, checked by their authors against decapped TMS5220NL
 * and TMS5220CNL dies. Both MAME files are under the BSD-3-Clause licence, whose notice follows:
 *
 * Copyright (c) Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud, Michael Zapf
 * (MAME, src/devices/sound/tms5220.cpp and tms5110r.hxx)
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
 *    following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 *    the following disclaimer in the documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
 *    promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Signed right shifts are arithmetic (floor), as in Python and on every compiler we build with. */
#include "lpc5220.h"

static const uint8_t LPC_KBITS[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
static const uint8_t LPC_ENERGY[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};
static const uint8_t LPC_PITCH[64] = {
    0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 46, 48,
    50, 52, 53, 56, 58, 60, 62, 65, 68, 70, 72, 76, 78, 80, 84, 86,
    91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};
static const int16_t LPC_K0[32] = {
    -501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
    -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436};
static const int16_t LPC_K1[32] = {
    -328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
    248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506};
static const int16_t LPC_K2_6[5][16] = {
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409}};
static const int16_t LPC_K7_9[3][8] = {
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307}};
static const int8_t LPC_CHIRP[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32,
    0x3b, 0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d};
/* right shift per interpolation period: the chip's, a hard step, a slow slur */
static const uint8_t LPC_INTERP[LPC_INTERP_N][8] = {
    {0, 3, 3, 3, 2, 2, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 4, 4, 4, 4, 4, 4, 4}};

static int32_t lpc_ktab(uint32_t i, uint32_t idx)
{
    return i == 0 ? LPC_K0[idx] : i == 1 ? LPC_K1[idx] : i < 7 ? LPC_K2_6[i - 2][idx] : LPC_K7_9[i - 7][idx];
}

/* matrix_multiply: a wrapped to 10 bits, b to 15 bits, both signed, product >> 9 */
static inline int32_t lpc_mm(int32_t a, int32_t b)
{
    a = ((a + 512) & 1023) - 512;
    b = ((b + 16384) & 32767) - 16384;
    return (a * b) >> 9;
}

static uint32_t lpc_read_bits(lpc5220_t *s, uint32_t w)
{
    uint32_t v = 0;
    while (w--) {
        uint32_t bit = s->pos < s->nbits ? (s->data[s->pos >> 3] >> (s->pos & 7)) & 1u : 0u;
        if (s->glitch_q16) {
            s->grng ^= s->grng << 13;
            s->grng ^= s->grng >> 17;
            s->grng ^= s->grng << 5;
            bit ^= (s->grng & 0xFFFFu) < s->glitch_q16;
        }
        v = (v << 1) | bit;
        s->pos++;
    }
    return v;
}

void lpc5220_init(lpc5220_t *s)
{
    uint8_t *p = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        p[i] = 0;
    s->rate_q8 = 256;
    s->grng = 0x9E3779B9u;
    s->ended = 1;
}

void lpc5220_start(lpc5220_t *s, const uint8_t *data, uint32_t nbytes)
{
    uint32_t i;
    s->data = data;
    s->nbits = nbytes * 8u;
    s->pos = 0;
    s->new_e = s->new_p = 0;
    for (i = 0; i < 10; i++) {
        s->new_k[i] = 0;
        s->k_cur[i] = 0;
        s->x[i] = 0;
    }
    for (i = 0; i < 11; i++)
        s->u[i] = 0;
    s->e_cur = s->p_cur = 0;
    s->zpar = s->uv_zpar = 1;
    s->olde = s->oldp = 1;
    s->inhibit = s->pitch_zero = s->done = s->stopping = s->ended = 0;
    s->ip = 0;
    s->pc = 0;
    s->sub = 1;
    s->rng = 0x1FFF;
    s->pitch_count = 0;
    s->pcount_q8 = 0;
    s->prev_e = 0;
    s->rate_acc = 0;
}

/* the frame load (at IP 0, PC 12, sub 1); 0 = the stream has ended */
static int lpc_load(lpc5220_t *s)
{
    uint32_t e, i;
    int new_sil, new_uv;
    if (s->done && ++s->stopping > 1) {
        s->ended = 1;
        return 0;
    }
    s->zpar = s->uv_zpar = 0;
    e = s->done || s->pos >= s->nbits ? 15u : lpc_read_bits(s, 4);
    if (e == 0 || e == 15) {
        s->new_e = (uint8_t)e;
        if (e == 15)
            s->done = 1;
    } else {
        uint32_t r = lpc_read_bits(s, 1), p = lpc_read_bits(s, 6);
        if (!r)
            for (i = 0; i < (p == 0 ? 4u : 10u); i++)
                s->new_k[i] = (uint8_t)lpc_read_bits(s, LPC_KBITS[i]);
        s->uv_zpar = p == 0;
        s->new_e = (uint8_t)e;
        s->new_p = (uint8_t)p;
    }
    new_sil = s->new_e == 0 || s->new_e == 15;
    new_uv = s->new_p == 0;
    s->inhibit = (!s->oldp && new_uv) || (s->oldp && !new_uv) || (s->olde && !new_sil) || (s->oldp && new_sil);
    return 1;
}

/* the parameter update (at sub 2 of PC 0..11) */
static void lpc_update(lpc5220_t *s)
{
    int32_t inh = s->inhibit && s->ip != 0, sh = LPC_INTERP[s->interp][s->ip], t;
    if (s->pc == 0) {
        if (s->ip == 0)
            s->pitch_zero = 0;
        t = LPC_ENERGY[s->new_e];
        s->e_cur = (s->e_cur + (((t - s->e_cur) * (1 - inh)) >> sh)) * (1 - s->zpar);
    } else if (s->pc == 1) {
        t = LPC_PITCH[s->new_p];
        s->p_cur = (s->p_cur + (((t - s->p_cur) * (1 - inh)) >> sh)) * (1 - s->zpar);
    } else {
        uint32_t i = s->pc - 2u;
        int32_t z = i < 4 ? s->zpar : (s->zpar || s->uv_zpar);
        t = lpc_ktab(i, s->new_k[i]);
        s->k_cur[i] = (s->k_cur[i] + (((t - s->k_cur[i]) * (1 - inh)) >> sh)) * (1 - z);
    }
}

static void lpc_advance(lpc5220_t *s)
{
    s->sub++;
    if (s->sub == 2 && s->pc == 12) {
        if (s->ip == 7) {
            if (s->inhibit)
                s->pitch_zero = 1;
            s->olde = s->new_e == 0 || s->new_e == 15;
            s->oldp = s->new_p == 0;
        }
        s->sub = 1;
        s->pc = 0;
        s->ip = (uint8_t)((s->ip + 1) & 7);
    } else if (s->sub == 3) {
        s->sub = 1;
        s->pc++;
    }
}

/* one timing step without the sample: load, update, advance */
static int lpc_timing(lpc5220_t *s)
{
    if (s->ip == 0 && s->pc == 12 && s->sub == 1 && !lpc_load(s))
        return 0;
    if (s->sub == 2 && s->pc < 12)
        lpc_update(s);
    return 1;
}

int32_t lpc5220_tick(lpc5220_t *s)
{
    int32_t exc, out, i;
    uint32_t steps;
    if (s->ended)
        return 0;
    /* rate: whole timing steps this sample; at rate 1 exactly one, in the chip's order */
    s->rate_acc += s->rate_q8;
    steps = s->rate_acc >> 8;
    s->rate_acc &= 255u;
    if (steps && !lpc_timing(s))
        return 0;

    if (s->oldp)
        exc = s->rng & 1u ? -64 : 64;
    else {
        uint32_t c = s->period_q8 ? s->pcount_q8 >> 8 : (uint32_t)s->pitch_count;
        exc = LPC_CHIRP[c < 51u ? c : 51u];
    }
    for (i = 0; i < 20; i++) {
        uint32_t r = s->rng, bit = ((r >> 12) ^ (r >> 3) ^ (r >> 2) ^ r) & 1u;
        s->rng = (uint16_t)(((r << 1) | bit) & 0x1FFFu);
    }

    s->u[10] = lpc_mm(s->prev_e, exc * 64);
    for (i = 9; i >= 0; i--)
        s->u[i] = s->u[i + 1] - lpc_mm(s->k_cur[i], s->x[i]);
    for (i = 9; i > 0; i--)
        s->x[i] = s->x[i - 1] + lpc_mm(s->k_cur[i - 1], s->u[i - 1]);
    s->x[0] = s->u[0];
    s->prev_e = s->e_cur;
    out = ((s->u[0] + 16384) & 32767) - 16384;
    out = out < -2048 ? -2048 : out > 2047 ? 2047 : out;
    out &= ~0xF;

    if (steps) {
        lpc_advance(s);
        while (--steps)
            if (!lpc_timing(s))
                break;
            else
                lpc_advance(s);
    }

    if (s->period_q8) {
        s->pcount_q8 += 256u;
        if (s->pcount_q8 >= s->period_q8)
            s->pcount_q8 -= s->period_q8;
        if (s->pitch_zero || s->pcount_q8 >= s->period_q8)
            s->pcount_q8 = 0;
    } else {
        s->pitch_count++;
        if (s->pitch_count >= s->p_cur || s->pitch_zero)
            s->pitch_count = 0;
        s->pitch_count &= 0x1FF;
    }
    return out;
}

void lpc5220_out_init(lpc5220_out_t *o, uint32_t host_sr)
{
    lpc5220_init(&o->chip);
    o->acc = 0;
    o->hold = o->prev = 0;
    o->linear = 0;
    lpc5220_out_clock(o, host_sr, 65536u);
}

void lpc5220_out_clock(lpc5220_out_t *o, uint32_t host_sr, uint32_t clock_q16)
{
    /* 8 kHz per host sample, Q32; the device's 44.1 kHz as a constant (no 64-bit divide there) */
    uint64_t k = host_sr == 44100u ? 779132389u : ((uint64_t)LPC5220_SR << 32) / host_sr;
    uint64_t inc = ((uint64_t)clock_q16 * k) >> 16;
    o->inc = inc > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)inc;
}

void lpc5220_out_render(lpc5220_out_t *o, int32_t *out, uint32_t n)
{
    while (n--) {
        uint32_t before = o->acc;
        int32_t y;
        o->acc += o->inc;
        if (o->acc < before) {
            o->prev = o->hold;
            o->hold = lpc5220_tick(&o->chip);
        }
        y = o->linear ? o->prev + (int32_t)(((int64_t)(o->hold - o->prev) * (o->acc >> 16)) >> 16) : o->hold;
        *out++ += y * 16;
    }
}
