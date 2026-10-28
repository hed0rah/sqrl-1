/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * resonator: see resonator.h */
#include "resonator.h"

#define RZ_FS 44100
#define RZ_X0 625376                        /* pi f / fs at MIDI 0 (8.1758 Hz), Q30 */
#define RZ_XMAX 1517966742                  /* 0.45 pi: no resonator above 0.45 fs */
#define RZ_DM0 3363785                      /* ln 1000 / (0.05 s RZ_FS), Q30: the shortest decay's damping a sample */
#define RZ_DSTEP 2447                       /* log2 80 / 127 in 1/192 octave Q8: DECAY's range, 50 ms x 80 = 4 s */
#define RZ_RETUNE 6                         /* resonators given a new frequency a block */
#define RZ_PMAX 2032                        /* MIDI 127 */
#define RZ_WMAX (8 * 32768 - 1)             /* the wet's clamp */

static const uint32_t RZ_SEMI[12] = {        /* 2^(s / 12), Q30 */
    1073741824u, 1137589835u, 1205234447u, 1276901417u, 1352829926u, 1433273380u,
    1518500250u, 1608794974u, 1704458901u, 1805811301u, 1913190429u, 2026954652u};
static const uint32_t RZ_FINE[16] = {        /* 2^(f / 192), Q30 */
    1073741824u, 1077625190u, 1081522600u, 1085434106u, 1089359758u, 1093299609u, 1097253708u, 1101222108u,
    1105204861u, 1109202018u, 1113213631u, 1117239753u, 1121280436u, 1125335733u, 1129405696u, 1133490379u};

/* m >> sh, rounded toward zero: a bank that holds less than the output's last bit gives exactly 0 */
#define RZ_RZ(m, sh) (((m) + (((m) >> 31) & ((1 << (sh)) - 1))) >> (sh))
/* a b >> 30 rounded to nearest, a Q30 rotation on a state (unbiased: no drift) */
#define RZ_MR(a, b) ((int32_t)(((int64_t)(a) * (b) + (1 << 29)) >> 30))
/* r s >> 30 rounded toward zero, the damping: it takes at least 1 from a state that is not 0, so a ring never
 * freezes where r s rounds back to s (nearest would, under |s| ~ 1 / (1 - r)) */
#define RZ_MD(r, s) ((int32_t)(((int64_t)(r) * (s) + (((int64_t)(r) * (s)) >> 63 & ((1 << 30) - 1))) >> 30))

/* 2^(u / 49152), Q30 in an int64, u in 1/192 octave Q8, -16 .. +12 octaves: exact on the 1/192 grid (the 1/16
 * semitones), within 2e-6 between */
static int64_t rz_pow2(int32_t u)
{
    int32_t k = u >> 8, f = u & 255, oct = (k + 192 * 16) / 192 - 16, idx = k - oct * 192;
    int64_t y0 = ((int64_t)RZ_SEMI[idx / 16] * RZ_FINE[idx % 16]) >> 30, y1;
    idx++;
    y1 = idx == 192 ? (int64_t)1 << 31 : ((int64_t)RZ_SEMI[idx / 16] * RZ_FINE[idx % 16]) >> 30;
    y0 += ((y1 - y0) * f) >> 8;
    return oct >= 0 ? y0 << oct : y0 >> -oct;
}

static uint32_t rz_isqrt(uint64_t n)
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > n)
        b >>= 2;
    while (b) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return (uint32_t)r;
}

/* 2 sin x for x in 0 .. 0.45 pi, Q30 in and out: the series to x^13, within 2e-6. The series' divisors as
 * Q30 reciprocals, so no 64-bit divide (a library call on a 32-bit core) */
#define RZ_INV(k) ((int64_t)((1 << 30) / (k)))
static int32_t rz_2sin(int32_t x)
{
    int64_t h = ((int64_t)x * x) >> 31, t;   /* x^2 / 2 */
    t = (1 << 30) - ((h * RZ_INV(78)) >> 30);
    t = (1 << 30) - ((((h * t) >> 30) * RZ_INV(55)) >> 30);
    t = (1 << 30) - ((((h * t) >> 30) * RZ_INV(36)) >> 30);
    t = (1 << 30) - ((((h * t) >> 30) * RZ_INV(21)) >> 30);
    t = (1 << 30) - ((((h * t) >> 30) * RZ_INV(10)) >> 30);
    t = (1 << 30) - ((((h * t) >> 30) * RZ_INV(3)) >> 30);
    return (int32_t)(((int64_t)x * t) >> 29);
}

/* resonator j's rotation for its slot's note at the damping in force, and its gain factor */
static int32_t rz_tune(rz_t *z, uint32_t j)
{
    uint32_t slot = j % RZ_NOTES, k = j / RZ_NOTES;
    int32_t sh = (int32_t)z->shift - 64, semis = (sh * 24 + (sh < 0 ? -63 : 63)) / 127;
    int32_t r = (1 << 30) - z->dm, r2, a, c1, c2;
    int64_t x = (RZ_X0 * rz_pow2(((int32_t)z->pitch[slot] + semis * 16) << 8)) >> 30, v, n, d;
    x *= k + 1;                          /* the octave and the twelfth: exact harmonics */
    x = x > RZ_XMAX ? RZ_XMAX : x;
    a = rz_2sin((int32_t)x);             /* 2 sin(W / 2), the rotation of an undamped pair at W */
    /* with the damping on one state the pole lands on W for e^2 = ((1 - r)^2 + r a^2) / r^2 */
    v = (int64_t)r * (((int64_t)a * a) >> 30) + (int64_t)z->dm * z->dm;
    v = rz_isqrt((uint64_t)v);
    v += (v * z->dm) >> 30;
    /* the peak gain there, from the transfer function to the fed state, (1 - r^2 z^-1) / (1 - 2 r cos W z^-1 +
     * r^2 z^-2) at z = e^jW: its inverse over (1 - r) is |1 - r e^-2jW| / |1 - r^2 e^-jW|, at most 2 */
    r2 = (int32_t)(((int64_t)r * r) >> 30);
    c1 = (1 << 30) - (int32_t)(((int64_t)a * a) >> 31);           /* cos W = 1 - a^2 / 2 */
    c2 = (int32_t)((((int64_t)c1 * c1) >> 29) - (1 << 30));       /* cos 2W */
    n = ((int64_t)1 << 60) - 2 * ((int64_t)r2 * c1) + (int64_t)r2 * r2;
    d = ((int64_t)1 << 60) - 2 * ((int64_t)r * c2) + (int64_t)r * r;
    {                                    /* their ratio, Q14, at most 2: a 32-bit divide on roots scaled to ~15 bits */
        uint32_t rd = rz_isqrt((uint64_t)d) >> 15, rn = rz_isqrt((uint64_t)n) >> 15;
        d = rn ? (int64_t)((rd << 14) / rn) : 32767;
    }
    z->kg[j] = (int16_t)(d > 32767 ? 32767 : d);
    return (int32_t)v;
}

/* a pair stored after its block; one that can no longer reach the output's last bit is silenced: the pair's
 * conserved quadratic s1^2 + s2^2 + e s1 s2 bounds |s1| by sqrt(q / (1 - e^2 / 4)) over the turns to come */
static void rz_put(rz_t *z, uint32_t j, int32_t s1, int32_t s2, int32_t e)
{
    int32_t a1 = s1 < 0 ? -s1 : s1, a2 = s2 < 0 ? -s2 : s2;
    if (a1 + a2 < (1 << 14)) {
        int64_t q = (int64_t)s1 * s1 + (int64_t)s2 * s2 + (int64_t)RZ_MR(e, s1) * s2;
        if (q < ((int64_t)1 << 26) - (((int64_t)e * e) >> 36))
            s1 = s2 = 0;
    }
    z->s[j][0] = s1;
    z->s[j][1] = s2;
    z->e[j] = e;
}

void rz_init(rz_t *z)
{
    uint32_t j;
    for (j = 0; j < RZ_N; j++) {
        z->s[j][0] = z->s[j][1] = 0;
        z->e[j] = 0;
        z->kg[j] = 0;
    }
    for (j = 0; j < RZ_NOTES; j++) {
        z->pitch[j] = -1;
        z->held[j] = 0;
    }
    z->dirty = 0;
    z->r2 = z->dm = z->g = 0;
    z->decay = z->shift = 255;
    z->nheld = 0;
    z->seq = 0;
}

void rz_notes(rz_t *z, const int32_t *pitch16, uint32_t n)
{
    int16_t p[RZ_NOTES];
    uint8_t to[RZ_NOTES], taken[RZ_NOTES], fresh[RZ_NOTES], old[RZ_NOTES];
    uint32_t i, j;
    n = n > RZ_NOTES ? RZ_NOTES : n;
    for (i = 0; i < n; i++) {
        p[i] = (int16_t)(pitch16[i] < 0 ? 0 : pitch16[i] > RZ_PMAX ? RZ_PMAX : pitch16[i]);
        to[i] = RZ_NOTES;
        fresh[i] = 0;
    }
    for (j = 0; j < RZ_NOTES; j++)
        taken[j] = 0;
    /* a note still sounding keeps its slot */
    for (i = 0; i < n; i++)
        for (j = 0; j < RZ_NOTES; j++)
            if (!taken[j] && z->held[j] && z->pitch[j] == p[i]) {
                taken[j] = 1;
                to[i] = (uint8_t)j;
                break;
            }
    /* a note within a semitone of a slot's is that note bent: it retunes the slot */
    for (i = 0; i < n; i++)
        if (to[i] == RZ_NOTES) {
            int32_t bd = 17, best = -1;
            for (j = 0; j < RZ_NOTES; j++)
                if (!taken[j] && z->pitch[j] >= 0) {
                    int32_t d = z->pitch[j] - p[i];
                    d = d < 0 ? -d : d;
                    if (d < bd) {
                        bd = d;
                        best = (int32_t)j;
                    }
                }
            if (best >= 0) {
                taken[best] = 1;
                to[i] = (uint8_t)best;
            }
        }
    /* the rest take slots that have ended, the quietest first; a held slot only when none has ended */
    for (i = 0; i < n; i++)
        if (to[i] == RZ_NOTES) {
            int64_t bk = (int64_t)1 << 62;
            int32_t best = 0;
            for (j = 0; j < RZ_NOTES; j++)
                if (!taken[j]) {
                    int32_t s1 = z->s[j][0], s2 = z->s[j][1];
                    int64_t key = (int64_t)(s1 < 0 ? -s1 : s1) + (s2 < 0 ? -s2 : s2);
                    key += z->held[j] ? (int64_t)1 << 40 : 0;
                    if (key < bk) {
                        bk = key;
                        best = (int32_t)j;
                    }
                }
            taken[best] = 1;
            to[i] = (uint8_t)best;
            fresh[i] = 1;
        }
    /* a note that went on or bent keeps its stamp; a new one is the newest */
    for (j = 0; j < RZ_NOTES; j++) {
        old[j] = z->held[j];
        z->held[j] = 0;
    }
    for (i = 0; i < n; i++) {
        j = to[i];
        if (z->pitch[j] != p[i]) {
            z->pitch[j] = p[i];
            z->dirty |= (1u | 1u << RZ_NOTES | 1u << 2 * RZ_NOTES) << j;
        }
        if (old[j] && !fresh[i])
            z->held[j] = old[j];
        else {
            z->seq = z->seq == 255 ? 1 : (uint8_t)(z->seq + 1);
            z->held[j] = z->seq;
        }
    }
    z->nheld = (uint8_t)n;
}

void rz_block(rz_t *z, const rz_param_t *p, const int32_t *in, int32_t *out, uint32_t n)
{
    int32_t de[RZ_N], et[RZ_N], gain[RZ_N], w[RZ_PART], r2, b = p->bright > 127 ? 127 : p->bright;
    int32_t m = p->mix > 127 ? 128 : p->mix + (p->mix >> 6);
    uint8_t act[RZ_N], fade[RZ_N], order[RZ_NOTES];
    uint32_t i, j, k, na = 0, nf = 0, cnt = 0, glid = 0;

    if (!n)
        return;
    if (p->decay != z->decay) {          /* the damping: r = 1 - x with x = ln 1000 / (T fs), T 50 ms .. 4 s */
        int32_t d = p->decay > 127 ? 127 : p->decay, dm = (int32_t)((RZ_DM0 * rz_pow2(-d * RZ_DSTEP)) >> 30);
        dm -= (int32_t)(((int64_t)dm * dm) >> 31);   /* 1 - e^-x = x - x^2 / 2 */
        z->dm = dm;
        z->r2 = (int32_t)(((int64_t)((1 << 30) - dm) * ((1 << 30) - dm)) >> 30);
        /* (1 - r) over the peak gain, 2 at DECAY 0 rising to 4 at 127: the gain's inverse before the note's own
         * factor */
        z->g = (int32_t)(((int64_t)dm * rz_pow2(d * 49152 / 127)) >> 29);
        z->decay = (uint8_t)d;
        z->dirty = (1u << RZ_N) - 1u;
    }
    if (p->shift != z->shift) {
        z->shift = p->shift > 127 ? 127 : p->shift;
        z->dirty = (1u << RZ_N) - 1u;
    }
    r2 = z->r2;

    /* new frequencies, a few a block, the fundamentals first (bits 0 .. 7); a silent resonator takes its new
     * rotation at once, a ringing one glides there across the block */
    for (j = 0; j < RZ_N && cnt < RZ_RETUNE; j++)
        if (z->dirty >> j & 1u) {
            z->dirty &= ~(1u << j);
            if (z->pitch[j % RZ_NOTES] < 0)
                continue;
            et[j] = rz_tune(z, j);
            if (!z->e[j] || (!z->s[j][0] && !z->s[j][1]))
                z->e[j] = et[j];
            else
                glid |= 1u << j;
            cnt++;
        }

    /* who rings and who is fed: the partials by BRIGHT; slots that have ended only while no note is held. The
     * budget goes by partial, held slots before ended ones, the newest first; what rings past it fades out */
    w[0] = 32767;
    w[1] = (b * 23170) / 127;
    w[2] = (b * b * 16384) / (127 * 127);
    for (j = 0; j < RZ_NOTES; j++) {     /* the slots in that order, an insertion sort on their rank */
        uint32_t rank = z->held[j] ? (uint8_t)(z->seq - z->held[j]) : 256 + j;
        for (k = j; k > 0; k--) {
            uint32_t o = order[k - 1], r = z->held[o] ? (uint8_t)(z->seq - z->held[o]) : 256 + o;
            if (r <= rank)
                break;
            order[k] = (uint8_t)o;
        }
        order[k] = (uint8_t)j;
    }
    for (k = 0; k < RZ_PART; k++)
        for (i = 0; i < RZ_NOTES; i++) {
            uint32_t slot = order[i];
            int32_t fed, ringing;
            j = k * RZ_NOTES + slot;
            fed = z->e[j] && w[k] && (z->held[slot] || !z->nheld);
            ringing = z->s[j][0] || z->s[j][1];
            if (!fed && !ringing)
                continue;
            if (na >= RZ_MAX_ACTIVE) {
                if (ringing)
                    fade[nf++] = (uint8_t)j;
                continue;
            }
            gain[j] = fed ? (int32_t)(((((int64_t)z->g * z->kg[j]) >> 24) * w[k]) >> 15) : 0;
            de[j] = glid >> j & 1u ? (et[j] - z->e[j]) / (int32_t)n : 0;
            act[na++] = (uint8_t)j;
        }

    for (i = 0; i < n; i++)
        out[i] = 0;
    /* the pairs run two at a time, interleaved, so a core that overlaps instructions has two chains to fill (the
     * odd one out runs beside a silent dummy) */
    for (j = 0; j < na; j += 2) {
        uint32_t a = act[j], c = j + 1 < na ? act[j + 1] : RZ_N;
        int32_t s1 = z->s[a][0], s2 = z->s[a][1], e = z->e[a], d = de[a], g = gain[a];
        int32_t t1 = 0, t2 = 0, f = 0, dd = 0, h = 0;
        if (c < RZ_N) {
            t1 = z->s[c][0];
            t2 = z->s[c][1];
            f = z->e[c];
            dd = de[c];
            h = gain[c];
        }
        /* each pair: s1 turns by e s2 and takes the input, s2 by -e s1 under the damping; the states are Q13 on
         * int16 scale, within 2^29 at the bank's peak gain */
        for (i = 0; i < n; i++) {
            int32_t x = in[i];
            s1 += RZ_MR(e, s2) + ((x * g) >> 7);
            t1 += RZ_MR(f, t2) + ((x * h) >> 7);
            s2 = RZ_MD(r2, s2) - RZ_MR(e, s1);
            t2 = RZ_MD(r2, t2) - RZ_MR(f, t1);
            out[i] += (s1 >> 5) + (t1 >> 5);
            e += d;
            f += dd;
        }
        rz_put(z, a, s1, s2, glid >> a & 1u ? et[a] : e);
        if (c < RZ_N)
            rz_put(z, c, t1, t2, glid >> c & 1u ? et[c] : f);
    }
    for (j = 0; j < nf; j++) {           /* over the budget: a block's fade on the ring, unfed, then silence */
        uint32_t a = fade[j];
        int32_t s1 = z->s[a][0], s2 = z->s[a][1], e = z->e[a], gf = 32767, gstep = 32767 / (int32_t)n;
        for (i = 0; i < n; i++) {
            s1 += RZ_MR(e, s2);
            s2 = RZ_MD(r2, s2) - RZ_MR(e, s1);
            out[i] += (int32_t)(((int64_t)(s1 >> 5) * gf) >> 15);
            gf -= gstep;
        }
        z->s[a][0] = z->s[a][1] = 0;
        if (glid >> a & 1u)
            z->e[a] = et[a];
    }
    for (i = 0; i < n; i++) {
        int32_t v = RZ_RZ(out[i], 8);
        v = v < -RZ_WMAX ? -RZ_WMAX : v > RZ_WMAX ? RZ_WMAX : v;
        out[i] = (in[i] * (128 - m) + v * m) >> 7;
    }
}
