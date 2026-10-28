/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering.
 * Runs in the audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only NVOICE of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place, as before. Extra UNISON
 * voices only start when there is room. */
static uint32_t vage;                                   /* voice ages: one clock for every part */
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* the S&H value of a new LFO cycle. Parts 1..3 draw it from the shared generator (rng); part 4 has its
 * own xorshift, so the shared sequence, which also seeds the engines' noise (PHYS, VOICE, ..), does not
 * depend on part 4's LFO (the golden renders rely on it) */
#define LFO_SHARED_RNG 3u
static uint32_t lfo_rand(track_t *t)
{
    uint32_t s = t->lfo_rnd ? t->lfo_rnd : 0x9E3779B9u;
    if ((uint32_t)(t - trk) < LFO_SHARED_RNG)
        return rng();
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* LFO 2 (1.2, #154; params.c N_LSYNC): with SYNC on, one cycle lasts the note value of the tempo (fx.c div_samples:
 * BPM, or the external clock's measured tempo while it runs), whatever RATE says (the matrix's RATE too): the phase
 * advances 2^32 x CTL / cycle a block (two 32-bit divides; the 1-bit truncation of 0xFFFFFFFF drifts by less than a
 * millionth of a cycle a cycle). OFF: RATE's table, as before */
static uint32_t div_samples(uint32_t div);         /* (fx.c) */
static __attribute__((noinline)) uint32_t lfo_sync_inc(const track_t *t)   /* (not inlined: its divides stay out of
                                                                           * the audio ISR's loop over the tracks) */
{
    uint32_t cyc, q, r;
    cyc = div_samples(LSYNC_DIV[(uint32_t)t->p[P_LSYNC] % NELEM(LSYNC_DIV)]);
    cyc = cyc ? cyc : 1u;
    q = 0xFFFFFFFFu / cyc;
    r = 0xFFFFFFFFu % cyc;
    return q * CTL + (r * CTL + CTL) / cyc;
}

static void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += t->p[P_LSYNC] ? lfo_sync_inc(t) : LFO_INC[t->p[P_LRATE] & 127];
    if (t->lfo_ph < old) {
        t->lfo_prev = (int16_t)mod_sh(t);               /* mod.c SLEW: it glides on from there */
        t->lfo_rnd = lfo_rand(t);
    }
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->p[P_LPOL])                                   /* UNI: 0..+1 (the depths reach as far, one way only) */
        t->lfo_val = (t->lfo_val + 32768) >> 1;
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else all of them */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine]->poly;
    return c && c < NVOICE ? c : NVOICE;
}

#if FELUCCA_SHELL
static volatile uint8_t shell_chord_play;               /* (chord.c's: the parts CHORD plays, in front or behind) */
static volatile uint8_t shell_ne_poly;                  /* (shell.c's: the parts a chord-playing NOTE engine plays) */
#endif
/* the part's voice mode: P_VOICE, POLY for an engine of hits (engine_t.oneshot: DRUM). SQRL-1: POLY on a part CHORD
 * plays, whatever VCE says (VCE stays as set, for KEYS) */
static uint32_t trk_vmode(const track_t *t)
{
#if FELUCCA_SHELL
    uint32_t i = (uint32_t)(t - trk);
    if (i < NTRK && ((shell_chord_play | shell_ne_poly) >> i) & 1u)
        return V_POLY;
#endif
    return ENGINES[t->engine]->oneshot ? V_POLY : (uint32_t)t->p[P_VOICE];
}

/* ------------------------------------------------- the shared voice budget --- */
static uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra voices of other non-POLY parts (also ones left after a mode change).
 * Returns its index, *pp its part; NVOICE = none */
#if FELUCCA_SHELL
/* the shell: the parts' roles in a steal. SR_REL, a part a mode has left: its voices go first;
 * SR_BG, a mode running behind: its voices are spared while it sounds no more than its reserve; SR_FG and none:
 * the stock rules. All roles none: stock behaviour */
enum { SR_NONE, SR_FG, SR_BG, SR_REL };
static volatile uint8_t shell_role[NPART], shell_reserve[NPART];
#define SR_RESERVE_MAX 3u                /* (shell.c's largest reserve) */
/* voice_room's hard path needs a victim: the voices a steal can't take (a reserve, one per other part: its bass or
 * a mono voice) must stay under the budget */
_Static_assert(SR_RESERVE_MAX + NPART - 1u < NVOICE, "the shell's voice reserve leaves no victim");
#endif
#if FELUCCA_SHELL
static uint32_t voice_victim_g(const track_t *self, int soft, track_t **pp, int guard);
/* the shell: a part that is not in front (a take looping on, BEAT, LINE) never takes a note held in front, unless
 * nothing else can go (the front holding the whole budget: the budget comes first) */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    int guard = self && (uint32_t)(self - trk) < NPART && shell_role[self - trk] != SR_FG;
    uint32_t k = voice_victim_g(self, soft, pp, guard);
    return k == NVOICE && guard ? voice_victim_g(self, soft, pp, 0) : k;
}
static uint32_t voice_victim_g(const track_t *self, int soft, track_t **pp, int guard)
#else
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
#endif
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t);
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
#if FELUCCA_SHELL
        if (shell_role[p] == SR_BG) {                   /* behind: spared within its reserve */
            uint32_t n = 0;
            for (i = 0; i < NVOICE; i++)
                n += t->v[i].active && t->v[i].stage != 4u;
            if (n <= shell_reserve[p])
                continue;
        }
#endif
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
#if FELUCCA_SHELL
            if (guard && shell_role[p] == SR_FG && v->gate)
                continue;                               /* (a note held in front) */
            if (shell_role[p] == SR_REL)                /* a part left: any of its voices first */
                c = 0;
            else
#endif
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode != V_POLY && i > 0)
                c = 2;                                  /* extra UNISON, or a voice left by a mode / cap change */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
static void voice_kill(voice_t *v)                      /* fade out over the next block (env_tick stage 4) */
{
    v->gate = 0;
    v->stage = 4;
    voice_kills++;
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < NVOICE)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen: each of the
                                                         * NPART < NVOICE parts protects one voice at most) */
    voice_kill(&vp->v[k]);
    return 1;
}

static voice_t *voice_reuse(track_t *t, voice_t *v)
{
    if (v->stage == 4u)
        voice_room(t, 0);                               /* its budget was taken before this block */
    return v;
}

/* POLY allocation (within the engine's voice cap). DRUM: reuse its lane; other engines: the same note.
 * Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    if (t->engine == ENGI_DRUM) {
        voice_t *v = drum_reuse(t, note);
        if (v)
            return voice_reuse(t, v);
    }
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note)
            return voice_reuse(t, &t->v[i]);
        nfree += !t->v[i].active;
    }
    if (nfree && voices_busy() >= NVOICE) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t)
            return &t->v[k];                            /* our own: restart it in place (no click) */
        if (k < NVOICE)
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return voice_reuse(t, &t->v[best == np ? 0 : best]);
}

#if FELUCCA_SHELL && !FELUCCA_FM4
/* GLIDE SPREAD (P_GSPR): each note glides in its own time, up to 4x the GLIDE. Its rank is its place by pitch among
 * the part's held notes as it starts (0: the lowest), so a chord from CHORD, the keys or MIDI spreads the same way.
 * ORDER (P_GORD): RAND, LOW (the low notes land first), HIGH (the high notes first). -> the time factor, Q8 (256: x1) */
static int32_t glide_spread(const track_t *t, const voice_t *v)
{
    static uint32_t seed = 0x2545F491u;
    uint32_t i, r = 0, n = 1, k;
    if (!t->p[P_GSPR])
        return 256;
    for (i = 0; i < NVOICE; i++)
        if (&t->v[i] != v && t->v[i].active && t->v[i].gate) {
            n++;
            r += t->v[i].pitch16 < v->pitch16;
        }
    if (t->p[P_GORD] == 1)                              /* LOW: the lowest at 1x, the highest at the full spread */
        k = n > 1u ? r * 255u / (n - 1u) : 0u;
    else if (t->p[P_GORD] == 2)                         /* HIGH: the mirror */
        k = n > 1u ? (n - 1u - r) * 255u / (n - 1u) : 0u;
    else {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        k = seed >> 24;
    }
    return 256 + (int32_t)(k * (uint32_t)t->p[P_GSPR] * 3u / 127u);
}
#endif

/* RATE's step for GLD g, 1/16 st per block in Q16, as it always ran (ENV_LIN, the envelopes' table, taken once a
 * block): an octave in about three quarters of the time shown, now every GLD its own speed */
static int32_t glide_rate_q16(uint32_t g) { return (int32_t)(ENV_LIN[(g ? g : 40u) & 127u] << 2); }
/* a gliding note's step, Q16: TIME, its interval over the GLIDE's time (the time shown, any interval); RATE, a fixed
 * speed (0: worked out each tick). f: the time factor (GLIDE SPREAD), Q8 */
static int32_t glide_step(const track_t *t, const voice_t *v, int32_t f)
{
    if (t->p[P_GLMODE]) {
        int64_t d = v->pitch16 - v->pitch_cur,          /* (the table's ticks are 32 samples; the glide moves once a */
                ticks = ((int64_t)(1u << 24) / ENV_LIN[t->p[P_GLIDE] & 127]) * 32 / HALF_FRAMES * f / 256;   /* block) */
        int64_t st;
        d = d < 0 ? -d : d;
        st = (d << 16) / (ticks > 0 ? ticks : 1);
        return st > 0 ? (int32_t)st : 1;
    }
    if (f != 256) {                                     /* RATE, spread: the step this note keeps */
        int64_t st = (int64_t)glide_rate_q16((uint32_t)t->p[P_GLIDE]) * 256 / f;
        return st > 0 ? (int32_t)st : 1;
    }
    return 0;
}

/* glide: RATE = a fixed speed, TIME = the same time for any interval. GLIDE SPREAD stretches each note's own; with an
 * ORDER, a new note re-ranks the part's gliding notes, so the chord spreads the same whichever note came first */
static void glide_set(track_t *t, voice_t *v, int glide)
{
    v->gfrac = 0;
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    v->gstart = v->pitch_cur;
#if FELUCCA_SHELL && !FELUCCA_FM4
    if (t->p[P_GSPR]) {
        uint32_t i;
        v->gstep = glide_step(t, v, glide_spread(t, v));
        if (t->p[P_GORD])                               /* (RAND: each note keeps its own draw) */
            for (i = 0; i < NVOICE; i++)
                if (&t->v[i] != v && t->v[i].active && t->v[i].gate && t->v[i].gstep &&
                    t->v[i].pitch_cur != t->v[i].pitch16 && t->v[i].pitch_cur == t->v[i].gstart)   /* (not moved yet) */
                    t->v[i].gstep = glide_step(t, &t->v[i], glide_spread(t, &t->v[i]));
        return;
    }
#endif
    v->gstep = glide_step(t, v, 256);
}

#if FELUCCA_SHELL
static int8_t voice_just;               /* CHORD's TUNE JUST (seq.c key_on): the next voice's offset, 1/16 st, for that start */
#else
#define voice_just 0
#endif
static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    uint32_t keep = e->keep;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->hpar = 0;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16 + voice_just;
    v->mvel = t->m_vel;                                 /* mod.c: the note's VEL and RAND */
    v->mrnd = t->m_rnd;
    t->m_vi = (uint8_t)(v - t->v);
    glide_set(t, v, glide);
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
        v->side = t->sp_alt;                            /* SPREAD: left, right, left .. (UNISON: mono_play); a */
        t->sp_alt ^= 1u;                                /* sounding voice keeps its side (no jump) */
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !e->sampled) {                     /* retrigger / steal: keep phases and filter */
        v->ph[0] = ph0;                                 /* states (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (keep & 0x01u)                               /* engine_t.keep: the slots the engines use */
            v->s[0] = s0;
        if (keep & 0x02u)
            v->s[1] = s1;
        if (keep & 0x10u)
            v->s[4] = s4;
        if (keep & 0x20u)
            v->s[5] = s5;
        if (keep & 0x40u)
            v->s[6] = s6;
        if (keep & 0x80u)
            v->s[7] = s7;
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_nvoice(t) : 1u, i;
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(nv - 1u);      /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NVOICE ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            v->side = (uint8_t)(i & 1u);                    /* SPREAD: the detuned voices alternate */
            if (nv > 1u && i && !ENGINES[t->engine]->sampled) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            v->mvel = t->m_vel;
            v->mrnd = t->m_rnd;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

/* HARMONY: a POLY part's played note brings voices a third, fifth, .. above it in its scale (the shell sets harm_cfg
 * per part from the NOTE engine on it: TYPE VOIC LEAD VEL; TYPE 0 off). Each harmony voice keeps its played note
 * (hpar), so the key's release ends them; a part's QNT CHORD scale gives chord tones, so a third is the next one */
enum { HM_OFF, HM_3RD, HM_5TH, HM_TRIAD, HM_7TH, HM_SUS2, HM_SUS4, HM_6TH, HM_ADD9, HM_OCT, HM_N };
enum { HV_CLOSE, HV_OPEN, HV_DROP2, HV_WIDE };
static volatile uint8_t harm_cfg[NTRK][4];
static uint32_t scale_mask(const track_t *t);          /* (seq.c) */
static int32_t scale_snap(const track_t *t, int32_t n);
/* n moved k notes up the part's scale (a note off the scale keeps its distance from the one below it) */
static int32_t harm_up(const track_t *t, int32_t n, uint32_t k)
{
    static const uint8_t MAJ[9] = {0, 2, 4, 5, 7, 9, 11, 12, 14};   /* (a twelve-note scale: the major's degrees) */
    uint32_t mask = scale_mask(t), r = (uint32_t)t->p[P_ROOT] % 12u;
    int32_t b, guard = 48;
    if (__builtin_popcount(mask) >= 12)
        return n + MAJ[k % 9u];
    b = scale_snap(t, n);
    n -= b;
    while (k && guard--) {
        b++;
        k -= (mask >> (uint32_t)((b - (int32_t)r + 120) % 12) & 1u);
    }
    return b + n;
}
/* the notes TYPE VOIC LEAD (c) put with a played note, into h; how many (the shell's HARMONY page shows them too) */
static uint32_t harm_notes(const track_t *t, const volatile uint8_t *c, uint32_t note, int32_t h[3])
{
    static const uint8_t DEG[HM_N][3] = {{0}, {2}, {4}, {2, 4}, {2, 4, 6}, {1, 4}, {3, 4}, {2, 4, 5}, {2, 4, 8}, {7}};
    static const uint8_t NUM[HM_N] = {0, 1, 1, 2, 3, 2, 2, 3, 3, 1};
    uint32_t type = c[0] < HM_N ? c[0] : 0u, m = NUM[type], i;
    for (i = 0; i < m; i++)
        h[i] = type == HM_OCT ? (int32_t)note + 12 : harm_up(t, (int32_t)note, DEG[type][i]);
    if (c[1] == HV_OPEN || (c[1] == HV_DROP2 && m >= 2u))   /* OPEN: the lowest up an octave; DROP2: the second */
        h[c[1] == HV_OPEN ? 0u : m - 2u] += c[1] == HV_OPEN ? 12 : -12;   /* highest of the chord down one */
    for (i = 0; i < m; i++)
        h[i] += (c[1] == HV_WIDE ? 12 : 0) - (c[2] ? 12 : 0);   /* WIDE: all up; LEAD BELOW: all down */
    return m;
}
static void harm_on(track_t *t, uint32_t note, uint32_t vel)
{
    const volatile uint8_t *c = harm_cfg[(uint32_t)(t - trk) % NTRK];
    uint32_t m, i, hv = (vel * c[3] + 63u) / 127u;
    int32_t h[3];
    m = harm_notes(t, c, note, h);
    for (i = 0; i < m; i++) {
        voice_t *v;
        int32_t x = h[i];
        if (x < 1 || x > 127 || x == (int32_t)note || !hv)
            continue;
        v = voice_alloc(t, (uint32_t)x);
        v->fine = 0;
        voice_start(t, v, (uint32_t)x, hv, 0);
        v->hpar = (uint8_t)(note + 1u);
    }
}

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = trk_vmode(t);
    if (t->p[P_MUTE])
        return;
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    vel = mod_note(t, note, vel);                       /* the matrix's VEL / KEY / RAND (1.5.6: VEL as a destination) */
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger (TRIG NOTE) and fade */
        if (!t->p[P_LTRIG])
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0 && !ENGINES[t->engine]->oneshot);
        if (harm_cfg[(uint32_t)(t - trk) % NTRK][0])
            harm_on(t, note, vel);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = trk_vmode(t);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                              /* a mode change must not retain a released key */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* a fallback above already has its new pitch; HARMONY's */
        if ((t->v[i].note == note || t->v[i].hpar == note + 1u) && t->v[i].gate) {   /* voices go with their note */
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        t->engine = eng_idx(t->eng_req);
        eng_mem_claim(t);                               /* its state region, before the waiting notes */
        t->xf_on = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    eng_mem_claim(t);                                   /* (an engine set directly: power-on, a test) */
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* ENV SYNC (1.5, #175; P_ESYNC, params.c ESYNC_NAMES): ATK DEC REL as note values of the tempo (BPM, or the external
 * clock's measured tempo: fx.c beat_samples, as LFO 2 SYNC). The part's three steps, made once a block before its
 * voices (track_render; the parts render one at a time), as gen_tables.py makes ENV_LIN / ENV_EXP for a time in ms:
 * the attack's linear step 2^24 CTL / T (rounded), the decay's and release's 1 - e^-x (x = 4.6 CTL / T: ~99 % of the way
 * after T), here as x - x^2/2 + x^3/6 - x^4/24 (x <= 0.33 at the shortest, 1/64T at 240 BPM: under 0.04 % off). The
 * coefficient is Q16 as ENV_EXP's: past ~2 s it is a few units, the time within half of one (4BAR at 90 BPM: 2.5 %).
 * Value 0 (and a time below a block) takes the tables' 0, as with SYNC OFF. OFF: the tables' values, as before (looked
 * up once a block here instead of once a voice in env_tick: the same numbers) */
static uint32_t beat_samples(void);                /* (fx.c) */
_Static_assert(CTL == 32, "esync_one: 4.6 CTL in Q16 is 9646899");
static uint32_t esync_k[3];      /* ATK's step, DEC's and REL's coefficient (Q16): the part's, this block (env_tick) */
static uint32_t esync_one(uint32_t quarter, int32_t v, uint32_t i)
{
    uint32_t u = ESYNC_UNITS[esync_idx(v)], T = quarter * u / 96u, x, x2, x3, x4;   /* T: samples */
    if (T < CTL)
        return i ? ENV_EXP[0] : ENV_LIN[0];
    if (!i)
        return ((1u << 24) * CTL + T / 2u) / T;
    x = (9646899u + T / 2u) / T;                        /* 4.6 CTL (CTL 32), Q16, rounded */
    if (x > 49152u)                                     /* (below ~200 samples: 0.75, the series 0.3 % low) */
        x = 49152u;
    x2 = x * x >> 16;
    x3 = x2 * x >> 16;
    x4 = x3 * x >> 16;
    x = x - x2 / 2u + x3 / 6u - x4 / 24u;
    return x < 1u ? 1u : x > 65535u ? 65535u : x;
}
static __attribute__((noinline)) void esync_coef(const track_t *t)   /* (not inlined: its divides stay out of the
                                                                    * audio ISR's loop over the voices) */
{
    const int16_t *p = t->p;
    if (p[P_ESYNC]) {
        uint32_t q = beat_samples();
        esync_k[0] = esync_one(q, p[P_ATK], 0);
        esync_k[1] = esync_one(q, p[P_DEC], 1);
        esync_k[2] = esync_one(q, p[P_REL], 2);
    } else {                                            /* OFF: the tables, as always */
        esync_k[0] = ENV_LIN[p[P_ATK] & 127];
        esync_k[1] = ENV_EXP[p[P_DEC] & 127];
        esync_k[2] = ENV_EXP[p[P_REL] & 127];
    }
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)esync_k[0];                  /* ATK's step (esync_coef: the table's, or ENV SYNC's) */
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, esync_k[1]);
        break;
    case 3:
        v->env -= mulq16(v->env, esync_k[2]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: this block fades it */
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* SPREAD (#148, fx.c mix_spread): while spread_on (mix_part sets it for the part it renders), track_render renders the
 * voices of side 0 into spread_buf and the others into out, then adds spread_buf to out: out is every voice, as always
 * (the engines add into either the same way), spread_buf the left ones. A cost per block, none per voice */
static uint8_t spread_on;
static int32_t spread_buf[CTL];
static __attribute__((noinline)) void spread_clear(void)
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        spread_buf[i] = 0;
}
static __attribute__((noinline)) void spread_sum(int32_t *out)
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        out[i] += spread_buf[i];
}

/* render one block of a part into out (cleared here); returns the voices rendered */
/* Channel bend is live performance state, outside projects/presets. Q8 semitones. */
static int32_t midi_bend_q8[NTRK], midi_bend_target[NTRK];
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    int32_t bend, bend16, bend_fine;
    uint32_t ti = (uint32_t)(t - trk);
    if (ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM) {
        midi_bend_q8[ti] = midi_bend_target[ti] = 0;
    } else {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
    bend_fine = (bend - bend16 * 16) * 2367 / 2560;
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (mod.on && mod.ldep < 32767)                     /* the matrix's DEPTH (mod.c) */
        lfo = mulq15(lfo, mod.ldep);
    if (spread_on)
        spread_clear();
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
#if FELUCCA_SHELL
    int shaped = t->eng_req != ENGI_LIZARD && shape_on(p);   /* (LIZARD shapes inside its render) */
#endif
    track_lfo_tick(t);
    esync_coef(t);                                      /* this block's envelope steps for env_tick (ENV SYNC) */
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                v->env = 1 << 24;                       /* (the ADSR's release never ends it) */
                env = 32767;
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(p[P_LPOL] ? lfo : (lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16) {               /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            uint32_t a = (uint32_t)v->gfrac + (uint32_t)(v->gstep ? v->gstep : glide_rate_q16((uint32_t)p[P_GLIDE]));   /* (slide: GLD 0, ~45 ms) */
            int32_t st = (int32_t)(a >> 16);            /* whole 1/16 st this tick, the rest carried */
            v->gfrac = (uint16_t)a;
            if (d > 0 ? d <= st : -d <= st) {
                v->pitch_cur = v->pitch16;
                v->gfrac = 0;
            } else
                v->pitch_cur += d > 0 ? st : -st;
        }
        if (!env && !m.amp0 && v->stage == 2 && !e->sampled)
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        pitch = v->pitch_cur + tune + bend16 + ((lfo * p[P_LD_PIT] * 3) >> 15) + ((m.envq15 * p[P_ED_PIT] * 3) >> 15);
        m.pitch16 = clamp(pitch, 0, 2047);
        m.inc = pitch_inc(m.pitch16);
        m.fine = v->fine + tune_fine + bend_fine;
        if (v->fine + tune_fine + bend_fine)             /* residual below 1/16 semitone */
            m.inc += (uint32_t)((int32_t)(m.inc >> 12) * (v->fine + tune_fine + bend_fine));
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
        if (mod.on)                                     /* the modulation matrix (mod.c) */
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
#if FELUCCA_SHELL
        if (shaped)                                     /* the SHAPE stage (eng_lizard.c shape_voice) */
            shape_voice(e, t, v, spread_on > v->side ? spread_buf : out, n, &m);
        else
#endif
        e->render(t, v, spread_on > v->side ? spread_buf : out, n, &m);   /* (SPREAD: the left ones) */
        nr++;
    }
    if (spread_on)
        spread_sum(out);                                /* out: every voice; spread_buf: the left ones */
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
