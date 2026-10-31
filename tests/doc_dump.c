/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* The manual's data, from the shell itself (docs/, tools/gen_docs.py): every nut entered as a player enters it,
 * every page it has walked, and each knob's label, range, power-on value and value names written as JSON; with
 * SHELL_SHOTS=dir, the screen of each page beside it (raw RGB565, tools/raw2png.py).
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/doc_dump tests/doc_dump.c -lm
 *   build/host/doc_dump > build/docs/shell.json */
#define SHELL_TEST_NO_MAIN 1
#include "shell_test.c"
#undef main

static void js_str(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}
static void js_val(const param_desc_t *d, int32_t v)   /* a value as the screen shows it, its unit apart */
{
    char val[24];
    const char *unit;
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (d->fmt == F_NOTE && d->max > 11) {              /* (a MIDI note: its octave too, which the card leaves out) */
        printf("{\"v\":\"%s%d\",\"u\":\"\"}", NN[v % 12], v / 12 - 1);
        return;
    }
    if (d == &SP_DESC[SP_SEED - SP_FIRST] && v < 0)     /* (SEED's -1 is a hidden seed: the range starts at 0) */
        v = 0;
    if (d->fmt == F_ENUM && d->names) {
        js_str(d->names[v - d->min]);
        return;
    }
    param_format(d, v, val, &unit);
    printf("{\"v\":");
    js_str(val);
    printf(",\"u\":");
    js_str(unit);
    putchar('}');
}
static const char *const FMT[] = {"INT", "PCT", "BIPCT", "TIME", "LFOHZ", "CUTOFF", "DB", "SEMI", "ENUM", "BPM", "NOTE"};

/* 0: a page's data and its screen; 1: the data only (a nut as it starts); 2: the screen only (the nut posed) */
static int dd_mode;

static void js_page(const char *shot_as)
{
    uint8_t ids[4];
    int glob, k;
    char nm[64];
    const char *pn = sh_page_name();
    if (dd_mode == 2) {
        if (shot_as)
            shot(shot_as);
        return;
    }
    sh_page_ids(ids, &glob);
    printf("{\"name\":");
    js_str(pn);
    printf(",\"shared\":%d,\"knobs\":[", glob);
    for (k = 0; k < 4; k++) {
        int16_t *vp = 0;
        const param_desc_t *d;
        int32_t v;
        const uint8_t *o;
        if (k)
            putchar(',');
        if (ids[k] == SH_NONE || sh_fmop_page() || (!ids[k] && sh.page[sh.mode] != SHP_HOME && !glob)) {
            printf("null");
            continue;
        }
        d = sh_desc(ids[k], glob, &vp);
        if (!d || !d->label || str_eq(d->label, "-")) {
            printf("null");
            continue;
        }
        v = vp ? *vp : d->def;
        printf("{\"label\":");
        js_str(d->label);
        printf(",\"fmt\":");
        js_str(d->fmt < NELEM(FMT) ? FMT[d->fmt] : "INT");
        o = enum_order(d);                              /* (a menu the knob walks in its own order: shown so) */
        printf(",\"min\":");
        js_val(d, o ? d->min + o[0] : d->min);
        printf(",\"max\":");
        js_val(d, o ? d->min + o[d->max - d->min] : d->max);
        printf(",\"now\":");
        js_val(d, v);
        printf(",\"n\":%d", d->max - d->min + 1);
        if (d->fmt == F_ENUM && d->names) {
            int32_t i, j, first = 1;
            printf(",\"names\":[");
            for (i = 0; i <= d->max - d->min; i++) {
                j = o ? o[i] : i;
                if (enum_orig(d, d->min + j) != d->min + j)   /* (a retired alias: the knob steps over it) */
                    continue;
                if (!first)
                    putchar(',');
                first = 0;
                js_str(d->names[j]);
            }
            putchar(']');
        }
        putchar('}');
    }
    printf("]");
    if (shot_as) {
        snprintf(nm, sizeof nm, "%s", shot_as);
        if (dd_mode == 0)
            shot(nm);
        printf(",\"shot\":");
        js_str(nm);
    }
    putchar('}');
}

/* page p in front, drawn; its name for the shot file: nut and page, lowercase, no spaces */
static int dd_page(uint32_t p)
{
    if (!sh_page_ok(p))
        return 0;
    sh.global = SHG_NONE;
    sh.page[sh.mode] = (uint8_t)p;
    sframes(400);
    return 1;
}
static void shot_name(char *out, size_t n, const char *nut, const char *page)
{
    size_t i = 0;
    const char *s;
    for (s = nut; *s && i + 2 < n; s++)                /* (as the manual's page names: S+H is s-h) */
        out[i++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ? *s : '-');
    out[i++] = '_';
    for (s = page; *s && i + 1 < n; s++)
        out[i++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s == ' ' || *s == '-' ? '_' : *s);
    out[i] = 0;
}
static void js_pages(const char *nut, const uint8_t *ps, uint32_t np)
{
    uint32_t i, first = 1;
    char nm[64];
    if (dd_mode != 2)
        printf("\"pages\":[");
    for (i = 0; i < np; i++) {
        if (!dd_page(ps[i]))
            continue;
        shot_name(nm, sizeof nm, nut, sh_page_name());
        if (dd_mode != 2)
            printf("%s\n   ", first ? "" : ",");
        js_page(nm);
        first = 0;
    }
    if (dd_mode != 2)
        printf("]");
}
static void pose(const char *n);
static void unpose(void);
/* a nut's pages twice: their data as the nut starts, then their screens with the nut posed */
static void nut_pages(const char *nut, const uint8_t *ps, uint32_t np)
{
    dd_mode = 1;
    js_pages(nut, ps, np);
    pose(nut);
    dd_mode = 2;
    js_pages(nut, ps, np);
    dd_mode = 0;
    unpose();
}
static void js_hero(uint32_t id)                       /* what ALGORITHM turns */
{
    int16_t *vp;
    const param_desc_t *d = id ? sh_desc(id, 0, &vp) : 0;
    printf("\"hero\":");
    if (d && d->label && !str_eq(d->label, "-"))
        js_str(d->label);
    else
        printf("null");
    putchar(',');
}
static void js_color(uint16_t c)
{
    printf("\"#%02X%02X%02X\"", (c >> 11 & 31) * 255 / 31, (c >> 5 & 63) * 255 / 63, (c & 31) * 255 / 31);
}
static void js_presets(uint32_t e)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k, first;
    printf("\"engine\":");
    js_str(en->name);
    printf(",\"presets\":[");
    for (k = 0, first = 1; k < en->npresets; k++) {
        if (preset_orig(en, k) != k)                    /* (SAMPLE's retired aliases: browsing skips them) */
            continue;
        if (!first)
            putchar(',');
        js_str(en->presets[k].name);
        first = 0;
    }
    putchar(']');
}
/* a nut doing what it does, for its screens (as shell_test.c's pictures): a chord held, a pattern in, the engines
 * that run on the clock running */
static void pose(const char *n)
{
    uint32_t k;
    if (str_eq(n, "CHORD")) {                           /* G7: m7 held, the root G4 */
        fm1_in.notes |= 1u << CB_M7; keyboard_block(); srender(2);
        skey_sung(14);
        fm1_in.notes &= ~(1u << CB_M7); keyboard_block(); srender(2);
    } else if (str_eq(n, "HARP")) {                     /* F3, then a run up the strings */
        fm1_in.notes |= 1u; keyboard_block(); srender(4); sframe();
        for (k = 12; k < 20u; k++)
            skey_sung(k);
        fm1_in.notes &= ~1u; keyboard_block(); srender(4);
    } else if (str_eq(n, "ALIVE") || str_eq(n, "TONNETZ") || str_eq(n, "ENIGMA")) {
        transport_req = 1;
        srender(1);
        if (!str_eq(n, "ALIVE"))
            skey_sung(12);
        for (k = 0; k < 120u; k++) { srender(4); if (k % 8u == 0u) sframe(); }
    } else if (str_eq(n, "SYLLABLE")) {
        apply_preset_to(sh_trk(), 8);
        srender(8);
        for (k = 0; k < 3u; k++) skey_sung(0);
    } else if (str_eq(n, "MOUTH")) {                    /* a note held on an open vowel: the lips show it */
        srender(60);                                    /* (the engine's fade-in first, or the key waits) */
        fm1_in.notes |= 1u << 12; keyboard_block();
        srender(55);
    } else if (str_eq(n, "TALK")) {
        skey_sung(0);
    } else if (str_eq(n, "RUDIMENT")) {                 /* kick on 1 5 9 13, snare 5 13, hats on 8ths */
        static const uint8_t K[] = {0, 4, 8, 12}, S[] = {4, 12}, H[] = {0, 2, 4, 6, 8, 10, 12, 14};
        for (k = 0; k < 4u; k++) skey((uint32_t)sh_white_key(K[k]));
        skey(3);
        for (k = 0; k < 2u; k++) skey((uint32_t)sh_white_key(S[k]));
        skey(5);
        skey(8);
        for (k = 0; k < 8u; k++) skey((uint32_t)sh_white_key(H[k]));
        skey(3);
    }
    sframes(SH_TOAST_MS + 200u);
}
static void unpose(void)
{
    if (song.playing || transport_req == 1u) {
        transport_req = 2;
        srender(4);
    }
    fm1_in.notes = 0;
    keyboard_block();
    sframes(400);
}

/* a SOUND entry of the catalogue in front, on the wheel's C5 (the picker's empty slot) with KEYS */
static void enter_sound(uint32_t m)
{
    sh_wslot[7] = (uint8_t)m;
    sgo2(7, 0);
    sframes(400);
}
static void enter_note(uint32_t n)                      /* (FM6; RUDIMENT on DRUMS, its classic pair) */
{
    sh_wnoteslot[4] = (uint8_t)n;
    sgo2(n == SH_N_RUDIMENT ? sh_mode_pos(3) : 0u, n);
    sframes(400);
}

/* the shell's own screens for the guide: the wheel, the picker, SHIFT, GLO + each button's card, SAVE's question,
 * the splash, ABOUT */
static void gesture_shots(void)
{
    static const struct { uint8_t b; const char *nm; } HB[] = {{B_HOME, "help_home"}, {B_GLO, "help_glo"},
        {B_SCL, "help_scale"}, {B_SAVE, "help_save"}, {B_EDIT, "help_edit"}, {B_ENV, "help_env"}, {B_LFO, "help_lfo"},
        {B_FX, "help_fx"}, {B_ARP, "help_arp"}, {B_SEQ, "help_seq"}, {B_REC, "help_rec"}, {B_PLAY, "help_play"}};
    uint32_t k;
    enter_sound(0);
    sh_wslot[7] = SH_EMPTY;
    stap(B_HOME);
    sdown(B_HOME); sframes(SH_TAP_MS + 400u);
    shot("shell_wheel");
    sdown(B_GLO); sframe();
    skey((uint32_t)sh_slot_key(7));
    for (k = 0; k < 8u; k++)
        sturn(EN_SELECT, 1);
    sframes(200);
    shot("shell_picker");
    sturn(EN_SELECT, -8);
    stap(B_OCTDN);
    sup(B_GLO); sframe();
    sup(B_HOME); sframes(SH_TOAST_MS + 200u);
    stap(B_OCTDN);
    sframes(SH_TOAST_MS + 200u);
    sdown(B_GLO); sframes(SH_TAP_MS + 200u);
    shot("shell_shift");
    sup(B_GLO); sframes(200);
    for (k = 0; k < NELEM(HB); k++) {
        if (HB[k].b == B_GLO)
            continue;
        sdown(B_GLO); sframes(SH_TAP_MS + 100u);
        stap(HB[k].b);
        sframes(200);
        shot(HB[k].nm);
        sup(B_GLO); sframes(400);
    }
    stap(B_HOME);
    stap(B_SAVE);
    sframes(200);
    shot("shell_save");
    stap(B_OCTDN);
    sframes(SH_TOAST_MS + 200u);
    stap(B_GLO); sframes(1200);
    shot("shell_system");
    stap(B_GLO); sframes(200);
    shot("shell_system2");
    stap(B_GLO); sframes(200);
    shot("shell_projects");
    stap(B_GLO); sframes(200);
    shot("shell_about");
    stap(B_GLO); sframes(200);
    shot("shell_legal");
    stap(B_HOME); sframes(SH_TOAST_MS + 200u);
    stap(B_SCL); sframes(200);
    shot("shell_scale");
    sturn(EN_SELECT, 1); sframes(200);                 /* MOVE, SELECT on from SCALE */
    shot("shell_move");
    stap(B_HOME); sframes(200);
    stap(B_SCL); stap(B_SCL); sframes(200);             /* HARMONY, SCL again: TRIAD for the NOTE engine in front */
    sh_hm[sh.note % SH_NNOTES][0] = HM_TRIAD; sframes(200);
    shot("shell_harmony");
    sh_hm[sh.note % SH_NNOTES][0] = 0;
    stap(B_HOME); sframes(200);
    sdown(B_HOME); sframes(SH_TAP_MS + 100u);           /* the PAIR page: HOME held, GLO tapped, let go */
    stap(B_GLO);
    sup(B_HOME); sframes(SH_TOAST_MS + 200u);
    shot("shell_pair");
    stap(B_HOME); sframes(200);
    sdown(B_FX); sframes(SH_TAP_MS + 100u);             /* the FX layer: FX held, C4 (LPF) held, FILTER turned */
    fm1_in.notes |= 1u << sh_white_key(4); keyboard_block(); sframe();
    sturn(EN_K1, -30);
    sframes(200);
    shot("shell_fx_layer");
    fm1_in.notes &= ~(1u << sh_white_key(4)); keyboard_block(); sframe();
    sup(B_FX); sframes(200);
    {   /* the peek: sqrl bouncing; then a corner hit (time moved on, never back) */
        uint32_t t, b, since;
        int32_t x, y;
        sh.page[sh.mode] = SHP_HOME; sframes(SH_TOAST_MS + 200u);
        fm1_ms = sh.idle_t + SH_PEEK_MS + 37000u;
        ui.force = 1; shell_draw();
        shot("shell_peek");
        for (t = 38000u; t < 3600000u; t += 40u) {
            sh_sq_at(t, &x, &y, &b, &since);
            if (since == 200u)
                break;
        }
        fm1_ms = sh.idle_t + SH_PEEK_MS + t;
        ui.force = 1; shell_draw();
        shot("shell_peek_corner");
        sturn(EN_K1, 1);                                /* a knob's first detent: it wakes, sqrl leaves */
        sframes(100);
        ui.force = 1; shell_draw();
        shot("shell_peek_leave");
        sframes(SH_LEAVE_MS + 200u);
        sh.page[sh.mode] = SHP_HOME; sframes(200);
    }
    {   /* RUDIMENT on FM6: the lines view, an acid line posed (accents, a slide, a tie, lanes of their own lengths) */
        static const uint8_t H[8][16] = {
            {1,0,0,1,0,0,1,0,0,0,1,0,1,0,0,0}, {0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, {0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0},
            {0}, {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0}, {1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0}};
        static const uint8_t LN[8] = {16, 16, 12, 16, 7, 16, 16, 5};
        track_t *r4 = &trk[SH_NOTES[SH_N_RUDIMENT].part % NTRK];
        uint32_t l, s;
        sgo2(0u, SH_N_RUDIMENT);
        r4->p[P_ROOT] = 0; r4->p[P_SCALE] = 1;
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = LN[l]; shell_lane_len[l] = LN[l];
            for (s = 0; s < 16u; s++)
                grid_hit(r4, s, l, H[l][s]);
        }
        grid_acc(r4, 0, 0, 1u); grid_acc(r4, 12, 0, 1u); grid_acc(r4, 9, 2, 1u); grid_acc(r4, 0, 7, 1u);
        r4->step[5].flags |= SF_SLIDE; r4->step[13].flags |= SF_SLIDE; r4->step[6].time = ST_TIE;
        sh_sp[SP_LANE - SP_FIRST] = 0;
        stap(B_HOME); sframes(SH_TOAST_MS + 200u);
        sh.page[sh.mode] = SHP_NOTE; sframes(100);
        shot("rudiment_rings_fm6");
        sh.page[sh.mode] = SHP_NOTE2; sframes(100);
        shot("rudiment_lines");
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = 16; shell_lane_len[l] = 16;
        }
        for (s = 0; s < NSTEP; s++)
            step_clear(&r4->step[s]);
        sframe();
    }
}

int main(void)
{
    /* a SOUND's own pages; the rest every SOUND shares (written once, under "shared") */
    static const uint8_t OWN[] = {SHP_HOME, SHP_EDIT1, SHP_EDIT2, SHP_FMOP, SHP_FMOP2, SHP_FMENV, SHP_FILTER, SHP_DRAW1,
                                  SHP_DRAW2, SHP_LANES1, SHP_LANES2, SHP_LKEYS, SHP_LKEYS2,
                                  SHP_SPELL, SHP_DRUM};
    static const uint8_t SHARED[] = {SHP_VOICE, SHP_GLIDE, SHP_SHAPE, SHP_ENV, SHP_ENVD, SHP_LFO, SHP_LFOD, SHP_LFO2,
                                     SHP_FX, SHP_INS, SHP_INS2, SHP_DELAY, SHP_DELAY2, SHP_REVERB, SHP_CHORUS, SHP_ARP,
                                     SHP_ARP2, SHP_SEQ, SHP_EVOLVE};
    static const uint8_t NOTEP[] = {SHP_NOTE, SHP_LINE, SHP_CPLAY, SHP_PROG};   /* (NOTE MAIN first) */
    static const uint8_t KEYSP[] = {SHP_ARP};           /* (KEYS has no engine: its NOTE view opens on ARP) */
    uint32_t m, n, k;
    uint8_t w0[SH_SLOTS];
    char nm[64];

    shell_reset();
    sframes(SH_SPLASH_MS + 64u);
    printf("{\"version\":");
    js_str(SQRL_VERSION);
    /* the numbers the prose quotes (auto:const NAME): a gesture's times, the meter, the polyphony caps */
    printf(",\n\"const\":{\"tap_ms\":%u,\"long_s\":\"%u.%u\",\"peek_min\":%u,\"dsp_red\":%u,\"strum_ms\":%u,"
           "\"fm6_poly\":%u,\"lz_uni\":%u,\"as_idle_s\":%u,\"as_gap_s\":%u",
           SH_TAP_MS, SH_LONG_MS / 1000u, SH_LONG_MS % 1000u / 100u, SH_PEEK_MS / 60000u, SH_DSP_RED, CP_STRUM_MS,
           FM6_POLY, LZS_MAX_UNI, AS_IDLE_MS / 1000u, AS_GAP_MS / 1000u);   /* (as_*: the autosave's) */
#if FELUCCA_SYLLABLE
    printf(",\"syl_poly\":%u", SYL_POLY);
#endif
#if FELUCCA_TALK
    printf(",\"talk_poly\":%u", TALK_POLY);
#endif
    putchar('}');
    printf(",\n\"rhythms\":[");                        /* RUDIMENT's: name, meter, each lane's n k rot */
    for (k = 0; k < SH_NRHY; k++) {
        uint32_t l;
        printf("%s{\"name\":", k ? "," : "");
        js_str(SH_RHY[k].name);
        printf(",\"meter\":");
        js_str(SH_RHY[k].meter);
        printf(",\"lanes\":[");
        for (l = 0; l < 8u; l++)
            printf("%s[%d,%d,%d]", l ? "," : "", SH_RHY[k].lane[l][0], SH_RHY[k].lane[l][1], SH_RHY[k].lane[l][2]);
        printf("]}");
    }
    printf("],\"lanes\":[");
    for (k = 0; k < 8u; k++)
        printf("%s\"%s\"", k ? "," : "", N_SP_LANE[k]);
    putchar(']');
    printf(",\n\"wheel\":{\"white\":[");
    for (k = 0; k < SH_SLOTS; k++)
        printf("%s{\"key\":%d,\"sound\":%s%s%s}", k ? "," : "", sh_slot_key(k),
               sh_wslot[k] == SH_EMPTY ? "" : "\"", sh_wslot[k] == SH_EMPTY ? "null" : SH_MODES[sh_wslot[k]].name,
               sh_wslot[k] == SH_EMPTY ? "" : "\"");
    printf("],\"black\":[");
    for (k = 0; k < SH_NOTE_SLOTS; k++)
        printf("%s{\"key\":%d,\"note\":\"%s\"}", k ? "," : "", sh_note_key(k), SH_NOTES[sh_wnoteslot[k]].name);
    printf("]},\n\"library\":[");
    for (k = 0; k < NELEM(CH_ENGINES); k++) {
        if (k)
            putchar(',');
        js_str(ENGINES[CH_ENGINES[k]]->name);
    }
    printf("],\n\"sound\":[");
    memcpy(w0, sh_wslot, sizeof w0);                    /* (the default wheel, before entering moves anything) */
    for (m = 0; m < SH_NMODES; m++) {
        if (!sh_wheel_avail(m))                         /* (a SOUND this build leaves out: TALK without FELUCCA_TALK) */
            continue;
        const sh_mode_t *s = &SH_MODES[m];
        enter_sound(m);
        printf("%s\n {\"name\":", m ? "," : "");
        js_str(s->name);
        printf(",\"about\":");
        js_str(s->about);
        printf(",\"color\":");
        js_color(s->color);
        for (k = 0; k < SH_SLOTS && w0[k] != m; k++)
            ;
        printf(",\"sides\":%d,\"default_slot\":%d,\"kind\":%d,", s->sides, k < SH_SLOTS ? (int)k : -1, s->kind);
        js_hero(s->hero);
        js_presets(trk[sh.part].eng_req);
        putchar(',');
        if (trk[sh.part].eng_req == ENGI_LIZARD) {
            static const uint8_t LP[] = {SHP_HOME, SHP_EDIT1, SHP_EDIT2, SHP_SHAPE, SHP_DRAW1, SHP_DRAW2};
            nut_pages(s->name, LP, NELEM(LP));
        } else {
            nut_pages(s->name, OWN, NELEM(OWN));
        }
        printf("}");
    }
    printf("],\n\"note\":[");
    for (n = 0; n < SH_NNOTES; n++) {
        const sh_note_t *s = &SH_NOTES[n];
        enter_note(n);
        printf("%s\n {\"name\":", n ? "," : "");
        js_str(s->name);
        printf(",\"about\":");
        js_str(s->about);
        printf(",\"color\":");
        js_color(s->color);
        printf(",\"kind\":%d,\"default_slot\":%d,", s->kind, n < SH_NOTE_SLOTS ? (int)n : -1);
        js_hero(n ? s->hero : 0u);
        if (s->kind == SHK_RUDIMENT) {                  /* (MAIN: the lane and its cycle; NOTES: the rhythm, its notes) */
            static const uint8_t RP[] = {SHP_NOTE, SHP_NOTE2, SHP_LANE2, SHP_LINE, SHP_SEQ};
            nut_pages(s->name, RP, NELEM(RP));
        } else if (s->kind == SHK_ENGINE) {
            nut_pages(s->name, KEYSP, NELEM(KEYSP));
        } else {
            uint32_t i;
            if (s->kind == SHK_ACID) {                  /* (ACID drawn with a line on it: LINE 7) */
                sh_sp[SP_DENS - SP_FIRST] = 10; sh_sp[SP_SPAN - SP_FIRST] = 6;
                sh_acid_load(sh_trk(), 7u);
            }
            nut_pages(s->name, NOTEP, NELEM(NOTEP));
            if (s->kind == SHK_ACID)
                for (i = 0; i < NSTEP; i++)
                    step_clear(&sh_trk()->step[i]);
        }
        printf("}");
    }
    sh_wnoteslot[4] = SH_N_RUDIMENT;
    enter_sound(0);
    printf("],\n\"shared\":{");
    js_pages("SHARED", SHARED, NELEM(SHARED));
    printf(",\n\"insert\":[");                          /* every INSERT type, its page as that type shows it */
    {
        const param_desc_t *d = track_desc(sh_trk(), P_ITYPE);
        int32_t v;
        uint32_t k;
        for (v = d->min; v <= d->max; v++) {
            sh_trk()->p[P_ITYPE] = (int16_t)v;
            for (k = 0; k < 3u; k++)                    /* (each type at its own defaults, not the last one's values) */
                sh_trk()->p[P_IA + k] = track_desc(sh_trk(), P_IA + k)->def;
            dd_page(SHP_INS);
            shot_name(nm, sizeof nm, "INSERT", d->names ? d->names[v - d->min] : "?");
            printf("%s\n   {\"type\":", v > d->min ? "," : "");
            js_str(d->names ? d->names[v - d->min] : "?");
            printf(",\"page\":");
            js_page(nm);
            putchar('}');
        }
        sh_trk()->p[P_ITYPE] = (int16_t)d->def;
    }
    printf("],\n\"scale\":");
    sh.global = SHG_SCALE;
    sframes(400);
    js_page("global_scale");
    printf(",\n\"system\":");
    sh.global = SHG_SYSTEM;
    sframes(400);
    js_page("global_system");
    printf(",\n\"system2\":");
    sh.global = SHG_SYSTEM2;
    sframes(400);
    js_page("global_system2");
    sh.global = SHG_NONE;
    printf("}}\n");
    gesture_shots();
    ui_power_on();
    memset(&sh, 0, sizeof sh);
    sframe();
    shot("shell_splash");
    return 0;
}
