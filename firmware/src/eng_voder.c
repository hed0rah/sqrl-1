/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 hed0rah
 * VODER: Homer Dudley's Voder (1939) on the FM-1's keys. The first ten white keys open the ten
 * bands, the first three black keys are the stops (p t k), the fourth is the wrist bar's hiss.
 * Pitch comes from PEDAL, a knob, as it came from a foot pedal; the keys never change it. It had
 * to be learned like an instrument: a year of practice for the operators of 1939. So does this one.
 *
 * Unity build, as the other eng_*.c: engines.c includes this file after dsp.c. Needs voder.c/.h.
 * The engine maps the keys itself (engine_t.keys): band b plays note 36 + b, stop j note 48 + j,
 * the HISS key note 52, so the sequencer and MIDI reach them too. One shared source per part,
 * made in block(); each held key is a voice that filters it.
 *
 *   PEDAL   the pitch, a note (the buzz's fundamental)
 *   HISS    buzz .. hiss (the wrist bar between them; the HISS key is all hiss while held)
 *   TILT    the buzz's brightness
 *   Q       the bands: 0 as wide as Dudley's, 127 three times narrower
 *   VIB     vibrato depth, at 5.5 Hz
 *   GLIDE   how slowly the pitch follows PEDAL
 *   STOPS   the stop keys' loudness
 *   DRIFT   a slow random wander of the pitch: the operator's own unsteadiness */
#include "voder.c"

#define VODER_NOTE_BAND 36
#define VODER_NOTE_STOP 48
#define VODER_NOTE_HISS 52
#define VODER_NOTE_NONE 120

typedef struct {
    voder_src_t src;
    voder_key_t key[NVOICE];
    int32_t buf[CTL];
    int32_t pitch16;                 /* the gliding pitch, 1/16 semitone << 8 */
    uint32_t vib_ph, drift_ph;
    int32_t drift, drift_to;
    int32_t rnd;
    uint8_t hiss_held, hiss_voice;
    int16_t q_set;
} voder_part_t;
ENG_MEM_FITS("VODER", sizeof(voder_part_t));

static voder_part_t *voder_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (voder_part_t *)ENG_MEM(t - trk) : 0;
}

/* key k (0 = F3, the lowest) -> its note: white keys 0..9 the bands, black keys 0..3 p t k HISS */
static int32_t voder_keys(const track_t *t, uint32_t k)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};   /* from F */
    uint32_t i, w = 0, b = 0;
    (void)t;
    for (i = 0; i < k; i++)
        BLACK[i % 12] ? b++ : w++;
    if (BLACK[k % 12])
        return b < 3u ? VODER_NOTE_STOP + (int32_t)b : b == 3u ? VODER_NOTE_HISS : VODER_NOTE_NONE;
    return w < VODER_NBAND ? VODER_NOTE_BAND + (int32_t)w : VODER_NOTE_NONE;
}

static void voder_note_on(track_t *t, voice_t *v)
{
    voder_part_t *vp = voder_of(t);
    uint32_t i = (uint32_t)(v - t->v);
    int32_t n = v->note, band = -1, lvl = 16384 + (v->vel << 7);
    if (!vp || i >= NVOICE)
        return;
    if (n >= VODER_NOTE_BAND && n < VODER_NOTE_BAND + VODER_NBAND)
        band = n - VODER_NOTE_BAND;
    else if (n >= VODER_NOTE_STOP && n < VODER_NOTE_STOP + VODER_NSTOP) {
        band = VODER_NBAND + n - VODER_NOTE_STOP;
        lvl = (lvl * (t->p[P_E6] + 1)) >> 6;
    } else if (n == VODER_NOTE_HISS)
        vp->hiss_held = 1;
    voder_key_start(&vp->key[i], band < 0 ? 99 : band, lvl > 32767 ? 32767 : lvl);
}

static void voder_block(track_t *t)
{
    voder_part_t *vp = voder_of(t);
    const int16_t *p = t->p;
    int32_t target, vib, st16;
    uint32_t i, hiss_now = 0;
    if (!vp)
        return;
    if (!vp->src.inc && !vp->src.rng) {                  /* first use (the region comes zeroed) */
        voder_init(&vp->src);
        vp->pitch16 = (p[P_E0] * 16) << 8;
        vp->q_set = -1;
        vp->rnd = 0x2545F491;
    }
    for (i = 0; i < NVOICE; i++)                         /* the HISS key: held while its voice's gate is */
        if (t->v[i].active && t->v[i].gate && t->v[i].note == VODER_NOTE_HISS)
            hiss_now = 1;
    vp->hiss_held = (uint8_t)hiss_now;
    if (vp->q_set != p[P_E3]) {
        vp->q_set = p[P_E3];
        voder_bands(&vp->src, 256u + (uint32_t)p[P_E3] * 4u);
    }
    /* PEDAL with GLIDE, VIB at 5.5 Hz, DRIFT toward a new random target every ~0.7 s */
    target = (clamp(p[P_E0], 24, 96) * 16) << 8;
    vp->pitch16 += (int32_t)(((int64_t)(target - vp->pitch16) * (4096 >> (p[P_E5] >> 4))) >> 12);
    vp->vib_ph += 17138700u;                             /* 5.5 Hz in CTL blocks: 2^32 x 5.5 x 32 / 44100 */
    vib = (sine_i(vp->vib_ph) * p[P_E4]) >> 19;          /* up to +-8 (1/16 semitone): half a semitone */
    if ((vp->drift_ph += 1u) >= 960u) {
        vp->drift_ph = 0;
        vp->drift_to = (int32_t)(noise32(&vp->rnd) >> 27) - 16;   /* +-16 (1/16 semitone): one semitone */
    }
    vp->drift += (vp->drift_to * p[P_E7] / 127 - vp->drift) >> 6;
    st16 = (vp->pitch16 >> 8) + vib + vp->drift;
    vp->src.inc = pitch_inc((uint32_t)clamp(st16, 0, 2047));
    vp->src.hiss = p[P_E1] * 258;
    vp->src.tilt = 4000 + (32767 - 4000) * p[P_E2] / 127;
    voder_source(&vp->src, vp->buf, CTL, vp->hiss_held);
}

static __attribute__((noinline)) void voder_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    voder_part_t *vp = voder_of(t);
    uint32_t i = (uint32_t)(v - t->v), j;
    int32_t tmp[CTL];
    if (!vp || i >= NVOICE || n > CTL || vp->key[i].band >= VODER_NBAND + VODER_NSTOP)
        return;
    for (j = 0; j < n; j++)
        tmp[j] = 0;
    voder_key_render(&vp->src, &vp->key[i], vp->buf, tmp, n);
    for (j = 0; j < n; j++)
        out[j] += voice_amp(tmp[j] * 3, m, j);
}

static const preset_t VODER_PRESETS[] = {
    /* PEDAL HISS TILT Q | VIB GLIDE STOPS DRIFT */
    {"DUDLEY 1939", {57, 0, 60, 0, 10, 48, 90, 30}, {2, 40, 127, 20}, 0, 0, FX(0, 0, 0, 25), PAT(0)},
    {"LOW MAN", {45, 0, 50, 20, 6, 64, 90, 40}, {2, 40, 127, 25}, 0, 0, FX(0, 10, 0, 30), PAT(0)},
    {"WHISPERER", {57, 127, 80, 30, 0, 32, 70, 0}, {4, 40, 127, 30}, 0, 0, FX(0, 0, 10, 40), PAT(0)},
    {"ORGAN BANDS", {48, 0, 100, 100, 20, 16, 40, 0}, {10, 40, 127, 50}, 0, 0, FX(0, 60, 20, 70), PAT(0)},
};

static const engine_t ENG_VODER = {
    .name = "VODER",
    .page_title = {"VODER", "VOICE"},
    .edit = {
        {"PEDAL", F_NOTE, 24, 96, 57, 0, 0},
        {"HISS", F_PCT, 0, 127, 0, 0, 0},
        {"TILT", F_PCT, 0, 127, 60, 0, 0},
        {"Q", F_PCT, 0, 127, 0, 0, 0},
        {"VIB", F_PCT, 0, 127, 10, 0, 0},
        {"GLIDE", F_PCT, 0, 127, 48, 0, 0},
        {"STOPS", F_PCT, 0, 127, 90, 0, 0},
        {"DRIFT", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = VODER_PRESETS,
    .npresets = NELEM(VODER_PRESETS),
    .note_on = voder_note_on,
    .render = voder_render,
    .block = voder_block,
    .keys = voder_keys,
    .knob = {P_E0, P_E1, P_E3, P_E4},
    .mem = sizeof(voder_part_t),
};
