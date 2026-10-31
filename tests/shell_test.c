/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* SQRL-1's shell (firmware/src/shell.c) on the host: the stock UI test's stubs and power-on (tests/ui_test.c, its main
 * renamed), then the shell driven frame by frame as main.c drives it with FELUCCA_SHELL=1.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/shell_test tests/shell_test.c -lm */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static volatile uint32_t audio_max_us;                  /* (audio.c's, not in the host build: the DSP meter) */
static uint32_t shed_count;
#include "../firmware/src/shell.c"

static uint32_t sh_frames;
static void sframe(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    shell_input();
    shell_leds();
    shell_draw();
    sh_frames++;
}
static void sframes(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) sframe(); }
static void sdown(uint32_t b) { fm1_in.buttons |= 1u << panel.btn[b]; host_pressed |= 1u << panel.btn[b]; }
static void sup(uint32_t b) { fm1_in.buttons &= ~(1u << panel.btn[b]); }
/* a tap, apart from the last (ARP's and REC's double taps are stap2) */
static void stap(uint32_t b) { sh_arp_t = sh_rec_t = 0; sdown(b); sframe(); sup(b); sframe(); }
static void stap2(uint32_t b) { sh_arp_t = sh_rec_t = 0; sdown(b); sframe(); sup(b); sframe(); sdown(b); sframe(); sup(b); sframe(); }
/* home, as the tests before TAB went there (a HOME tap is TAB now: tests of it tap it): SOUND MAIN; RUDIMENT's and
 * LINES' lane knobs (LEN FILL ROT on KNOB 2-4), which were on HOME, are NOTE MAIN's now */
static void shome(void)
{
    sh.global = SHG_NONE;
    sh.page[sh.mode] = sh_kind() == SHK_RUDIMENT ? SHP_NOTE : SHP_HOME;
    sframe(); sframe();
}
/* the NOTE view's first page (MAIN; KEYS: ARP) */
static void snmain(void) { sh.global = SHG_NONE; sh.page[sh.mode] = sh_page_ok(SHP_NOTE) ? SHP_NOTE : SHP_ARP; sframe(); sframe(); }
/* the NOTE view, where it was left (ALGORITHM turns the NOTE engine's there) */
static void snote(void) { sh.global = SHG_NONE; sh.page[sh.mode] = (uint8_t)sh_view_page(1); sframe(); sframe(); }
static void sturn(uint32_t role, int32_t s) { host_enc[panel.enc[role]] += s * panel.dir[role]; sframe(); }
/* a page or a knob by what it is, not by the taps that lead there today: tests about the player's data move on these,
 * so a new layout breaks the gesture tests (below), not them. Each says NAV FAIL and returns 0 when it cannot */
static int sgoto(uint32_t p)
{
    uint32_t i;
    if (p >= SHP_N || !sh_page_ok(p)) {
        printf("NAV FAIL: page %u is not there for %s\n", p, sh_cur()->name);
        return 0;
    }
    sh.global = SHG_NONE;
    if (sh_page_note(p) != sh_view_note()) {            /* the other view, as HOME's TAB opens it */
        sh_view_keep();
        sh.page[sh.mode] = (uint8_t)sh_view_page(sh_page_note(p));
        sframe();
    }
    for (i = 0; i < 2u * SHP_N && sh.page[sh.mode] != p; i++)
        sturn(EN_SELECT, 1);
    if (sh.page[sh.mode] != p) {
        printf("NAV FAIL: SELECT never reached page %u (stopped on %u)\n", p, sh.page[sh.mode]);
        return 0;
    }
    return 1;
}
static int sturn_id(uint32_t id, int32_t s)             /* the knob on the page in front that turns id */
{
    static const uint8_t KR[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
    uint8_t ids[4];
    int g;
    uint32_t k;
    sh_page_ids(ids, &g);
    for (k = 0; k < 4u; k++)
        if (ids[k] == id) {
            sturn(KR[k], s);
            return 1;
        }
    printf("NAV FAIL: no knob turns id %u on page %u\n", id, sh.page[sh.mode]);
    return 0;
}
static void skey(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); sframe();
                               fm1_in.notes &= ~(1u << k); keyboard_block(); sframe(); }
/* audio blocks as the ISR runs them (the engines' note-ons, the switches): events, then every part's voices */
static void srender(uint32_t blocks)
{
    static int32_t buf[2 * HALF_FRAMES];
    uint32_t b, p;
    for (b = 0; b < blocks; b++) {
        events_block(HALF_FRAMES);
        for (p = 0; p < NTRK; p++) {
            memset(buf, 0, sizeof buf);
            track_render(&trk[p], buf, HALF_FRAMES);
        }
    }
}
/* LEGATO from note a to b with a still held, GLD g in RATE (0) or TIME (1): the glide's length in seconds, -1 none
 * started, the limit if it never lands */
static float sglide(track_t *t, uint32_t g, uint32_t timed, uint32_t a, uint32_t b, float limit)
{
    int16_t keep[3] = {t->p[P_VOICE], t->p[P_GLIDE], t->p[P_GLMODE]};
    uint32_t k, n = 0, at = NVOICE, lim = (uint32_t)(limit * 44100.0f / (float)HALF_FRAMES);
    t->p[P_VOICE] = V_LEGATO; t->p[P_GLIDE] = (int16_t)g; t->p[P_GLMODE] = (int16_t)timed;
    srender(64);                                        /* (a SOUND just picked: its engine's fade over first) */
    trk_note_on(t, a, 100u); srender(8);
    trk_note_on(t, b, 100u);
    for (k = 0; k < NVOICE; k++)
    for (k = 0; k < NVOICE; k++)
        if (t->v[k].active && t->v[k].gate && t->v[k].pitch_cur != t->v[k].pitch16)
            at = k;
    if (at < NVOICE)
        while (t->v[at].pitch_cur != t->v[at].pitch16 && n < lim) {
            srender(1);
            n++;
        }
    trk_note_off(t, b); trk_note_off(t, a); srender(64);
    t->p[P_VOICE] = keep[0]; t->p[P_GLIDE] = keep[1]; t->p[P_GLMODE] = keep[2];
    return at < NVOICE ? (float)n * (float)HALF_FRAMES / 44100.0f : -1.0f;
}
static int sh_sounds(const track_t *t, uint32_t note)   /* a gated voice plays note */
{
    uint32_t q;
    for (q = 0; q < NVOICE; q++)
        if (t->v[q].active && t->v[q].gate && t->v[q].note == note)
            return 1;
    return 0;
}
/* power of x[0..n) at f Hz (Goertzel) */
static double st_goertzel(const int32_t *x, uint32_t n, double f)
{
    double w = 2.0 * cos(2.0 * 3.14159265358979 * f / FS), s1 = 0.0, s2 = 0.0, s0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = (double)x[i] + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - w * s1 * s2;
}
static double st_band(const int32_t *x, double f0, double f1)   /* a band's power, 4096 samples, 10 Hz apart */
{
    double e = 0.0, f;
    for (f = f0; f <= f1; f += 10.0)
        e += st_goertzel(x, 4096, f);
    return e;
}
static void skey_sung(uint32_t k)                       /* a key with the audio running under it */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); srender(8); sframe();
    fm1_in.notes &= ~(1u << k); keyboard_block(); srender(40); sframe();
}
/* a mode by its name: a SOUND slot (with KEYS), or, since the NOTE / SOUND split, CHORD and ALIVE as the pairs the
 * modes of those names were (CHORD over LIBRARY, its first engine ANALOG; ALIVE over LIZARD) */
#define TM_CHORD 100u
#define TM_ALIVE 101u
#define TM_RUD 102u                     /* (RUDIMENT, the lanes, on DRUMS: the classic beat) */
/* the SOUND on G4 by default: TALK, or MOUTH in a build without it (shell.c SH_G4) */
#define G4 (FELUCCA_TALK ? "TALK" : "MOUTH")
#define G4_ENG (FELUCCA_TALK ? ENGI_TALK : ENGI_MOUTH)
static uint32_t slot_of(const char *name)
{
    uint32_t m;
    for (m = 0; m < SH_NMODES && !str_eq(SH_MODES[m].name, name); m++)
        ;
    return m;
}
static uint32_t mode_of(const char *name)
{
    return str_eq(name, "CHORD") ? TM_CHORD : str_eq(name, "ALIVE") ? TM_ALIVE : str_eq(name, "RUDIMENT") ? TM_RUD :
           slot_of(name);
}
static uint32_t note_of(const char *name)
{
    uint32_t n;
    for (n = 0; n < SH_NNOTES && !str_eq(SH_NOTES[n].name, name); n++)
        ;
    return n;
}
static uint32_t tm_slot(uint32_t m)
{
    return m == TM_CHORD ? slot_of("ANALOG") : m == TM_ALIVE ? slot_of("LIZARD") : m == TM_RUD ? slot_of("DRUMS") : m;
}
static uint32_t tm_note(uint32_t m)
{
    return m == TM_CHORD ? note_of("CHORD") : m == TM_ALIVE ? note_of("ALIVE") : m == TM_RUD ? note_of("RUDIMENT") : 0u;
}
static int sh_is(uint32_t m) { return sh.mode == tm_slot(m) && sh.note == tm_note(m); }
static void skey(uint32_t k);
/* the wheel: HOME held, the slot's white key and its NOTE engine's black key, let go: the pair to the front at once */
static void sgo2(uint32_t slot, uint32_t note)         /* any pair: a SOUND slot and a NOTE engine (by its black
                                                         * key, or, off the wheel, put under it as the PAIR page does) */
{
    uint32_t pos = sh_note_pos(note);
    sdown(B_HOME); sframes(SH_TAP_MS + 100u);
    skey((uint32_t)sh_slot_key(slot));
    if (pos < SH_NOTE_SLOTS)
        skey((uint32_t)sh_note_key(pos));
    sup(B_HOME); sframe();
    if (pos >= SH_NOTE_SLOTS && note < SH_NNOTES) {
        sh_switch_pair(sh.mode, note);
        sframe();
    }
}
/* a SOUND by its catalogue index (slot_of), on the wheel or off it (as the PAIR page puts it), with a NOTE engine */
static void sgoc(uint32_t mode, uint32_t note)
{
    if (sh_mode_pos(mode) < SH_SLOTS) {
        sgo2(sh_mode_pos(mode), note);
        return;
    }
    sh_switch_pair(mode, note < SH_NNOTES ? note : sh.note);
    sframe();
}
/* BEAT and LINE play whenever the transport runs: a test that needs every voice for the pair in front mutes them
 * (on 1) and brings them back (0) */
static void tracks_hush(int on)
{
    uint32_t p;
    for (p = SH_LINE; p <= SH_BEAT; p++)
        if (sh_track_note(p) < SH_NNOTES && (int)((sh_tmute >> p) & 1u) != on)
            sh_track_mute(sh_partpair[p] & 0xFFu, sh_track_note(p));
    sframe();                                           /* (the parts' roles follow) */
}
static void sgo(uint32_t m)
{
    if (sh_is(m))
        return;
    sgoc(tm_slot(m), tm_note(m));
}
static void shell_reset(void)
{
    ui_power_on();
    memset(&sh, 0, sizeof sh);
    memset(sh_b, 0, sizeof sh_b);
    memset(sh_snap, 0, sizeof sh_snap);
    for (int pp_ = 0; pp_ < NPART; pp_++) sh_partpair[pp_] = SH_NOPAIR;
    memset(sh_snapx, 0, sizeof sh_snapx);
    kb_mask = 0;
    sframe();
    while (sh.splash_t)                                 /* (the power-on wordmark: the brand block tests it) */
        sframe();
}
static const uint8_t KR4[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
static int sh_led_on(uint32_t id)                       /* a button's LED fully lit */
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((fm1_led[q >> 3] >> (q & 7u)) & 1u);
}
static uint32_t sh_key_lv(uint32_t k)                   /* key k's LED: 3 lit, 2 mid, 1 glow, 0 dark (4 breathing) */
{
    uint8_t q = led_pos[14u + k], m = (uint8_t)(1u << (q & 7u));
    if (q == 0xFF)
        return 0;
    return fm1_led[q >> 3] & m ? 3u : fm1_led_breath[q >> 3] & m ? 4u : fm1_led_mid[q >> 3] & m ? 2u :
           fm1_led_dim[q >> 3] & m ? 1u : 0u;
}
static uint32_t screen_count(uint16_t c)                /* pixels exactly that colour */
{
    uint32_t k, n = 0;
    for (k = 0; k < 240u * 240u; k++)
        n += host_screen[k] == swap16(c);
    return n;
}
/* SHELL_SHOTS=dir: the screen as raw RGB565 (byte-swapped as the LCD gets it) for tools to turn into PNGs */
static void shot(const char *name)
{
    const char *dir = getenv("SHELL_SHOTS");
    char path[256];
    FILE *f;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.raw", dir, name);
    if ((f = fopen(path, "wb"))) {
        fwrite(host_screen, 2, 240 * 240, f);
        fclose(f);
    }
}
static int count_zero(void) { uint32_t k, n = 0; for (k = 0; k < 240u * 240u; k++) n += host_screen[k] == 0u; return n > 0u; }
static uint32_t screen_ink(uint32_t y0, uint32_t y1)    /* pixels not the background in rows y0..y1-1 */
{
    uint32_t k, n = 0;
    uint16_t bg = swap16(SC_BG);
    for (k = y0 * 240u; k < y1 * 240u; k++)
        n += host_screen[k] != bg && host_screen[k] != 0u;   /* (0: never drawn; bg: the background) */
    return n;
}

/* hold-to-see: the map in front has the item key (and act, unless 0) */
static int hs_has(const char *key, const char *act)
{
    sh_hs_t h;
    uint32_t b = sh_hs_which(), i;
    if (b == NB)
        return 0;
    sh_hs_map(&h, b);
    for (i = 0; i < h.n; i++)
        if (str_eq(h.key[i], key) && (!act || str_eq(h.act[i], act)))
            return 1;
    return 0;
}
static uint32_t hs_cut(void)                            /* items the map in front leaves out for want of room */
{
    sh_hs_t h;
    int32_t x[SH_HS_N], y[SH_HS_N];
    uint32_t b = sh_hs_which(), n, i;
    if (b == NB)
        return 0;
    sh_hs_map(&h, b);
    n = sh_hs_layout(&h, x, y);
    for (i = 0; i < h.n; i++)
        if (x[i] < 0)
            printf("  (left out: %s %s)\n", h.key[i], h.act[i]);
    return n;
}
static uint32_t cv_hash(void)                           /* the canvas as drawn */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NELEM(cv_px); i++)
        h = (h ^ cv_px[i]) * 16777619u;
    return h;
}
static uint32_t band_hash(void)                         /* the knob cards' band as drawn */
{
    uint32_t k, h = 2166136261u;
    for (k = SH_Y_LEG * 240u; k < 240u * 240u; k++)
        h = (h ^ host_screen[k]) * 16777619u;
    return h;
}
/* the picture alone, into the two bands as shell_draw draws them (flower: the flower that stands in for one): its hash,
 * *out the pixels drawn outside its band (SH_Y_PIC .. SH_Y_LEG), *ink those inside */
static uint32_t pic_draw(int flower, uint32_t *out, uint32_t *ink)
{
    uint32_t band, y, x, h = 2166136261u;
    uint16_t bg = swap16(SC_BG);
    *out = *ink = 0;
    for (band = 0; band < 2u; band++) {
        sh_y0 = (int32_t)band * 120;
        cv_begin(240, 120, SC_BG);
        if (flower)
            sh_flower(120.0f, 104.0f, 34.0f);
        else
            sh_draw_picture();
        for (y = 0; y < 120u; y++)
            for (x = 0; x < 240u; x++) {
                uint32_t sy = y + band * 120u, c = cv_px[y * 240u + x];
                if (sy < SH_Y_PIC || sy >= SH_Y_LEG) {
                    *out += c != bg;
                } else {
                    *ink += c != bg;
                    h = (h ^ c) * 16777619u;
                }
            }
    }
    sh_y0 = 0;
    return h;
}
/* the engine in front over its presets and each of its eight at its ends and middle: the worst picture's pixels out
 * of the band, and whether one was the flower or empty (*bare) */
static uint32_t pic_sweep(uint32_t flower, int *bare)
{
    const engine_t *en = ENGINES[sh_eng()];
    track_t *t = sh_trk();
    uint32_t k, j, out, ink, worst = 0, np = en->npresets < 16u ? en->npresets : 16u;
    for (k = 0; k <= np; k++) {
        if (k < np)
            apply_preset_to(t, k);
        else
            apply_preset_to(t, 0);
        srender(2);
        for (j = 0; j < (k < np ? 1u : 24u); j++) {     /* (after the presets: each of the eight at min, mid, max) */
            int16_t keep = 0;
            if (k == np) {
                const param_desc_t *d = &en->edit[j / 3u];
                keep = t->p[P_E0 + j / 3u];
                t->p[P_E0 + j / 3u] = (int16_t)(j % 3u == 0u ? d->min : j % 3u == 1u ? (d->min + d->max) / 2 : d->max);
            }
            if (pic_draw(0, &out, &ink) == flower || ink < 200u)
                *bare = 1;
            worst = out > worst ? out : worst;
            if (k == np)
                t->p[P_E0 + j / 3u] = keep;
        }
    }
    return worst;
}

/* the arp under test: ms of audio and frames together (16 ms a frame) */
static uint32_t arp_acc;
static void arp_run(uint32_t ms)
{
    uint32_t f;
    for (f = 0; f < ms / 16u; f++) {
        arp_acc += 16u * FS / 1000u;
        while (arp_acc >= HALF_FRAMES) {
            srender(1);
            arp_acc -= HALF_FRAMES;
        }
        sframe();
    }
}
static uint32_t arp_gated(const track_t *t)             /* the part's voices sounding */
{
    uint32_t q, n = 0;
    for (q = 0; q < NVOICE; q++)
        n += t->v[q].active && t->v[q].gate;
    return n;
}
static void arp_key(uint32_t k, int down)               /* a key of the keyboard, down or up, as the ISR sees it */
{
    if (down) {
        fm1_in.notes |= 1u << k;
        host_notes |= 1u << k;
    } else
        fm1_in.notes &= ~(1u << k);
    keyboard_block();
}


/* a lanes part played for a while: for each bar from the transport's start (seq_abs / 16, at most 8), the steps where
 * a new note of `note` started, as a mask */
static void lane_bars(track_t *t, uint32_t note, uint32_t until_bar, uint16_t m[8])
{
    uint32_t q, top = 0, k;
    for (q = 0; q < NVOICE; q++)
        if (t->v[q].active && t->v[q].note == note && t->v[q].age > top)
            top = t->v[q].age;
    for (k = 0; k < 200000u && (!k || seq_abs[trk_index(t)] / 16u < until_bar); k++) {   /* (a block first: a PLAY
                                                         * just asked starts in it) */
        uint32_t a;
        srender(1);
        a = seq_abs[trk_index(t)];                      /* (the step a note in this block started on) */
        for (q = 0; q < NVOICE; q++)
            if (t->v[q].active && t->v[q].note == note && t->v[q].age > top) {
                top = t->v[q].age;
                if (a / 16u < 8u)
                    m[a / 16u] |= (uint16_t)(1u << (a % 16u));
            }
    }
}
#ifdef SHELL_TEST_NO_MAIN
#define main shell_test_main                            /* (tests/doc_dump.c: its own) */
#endif
int main(void)
{
    int bad = 0, ok;
    int16_t v, keep;
    uint32_t k2;
    uint32_t rows;

    {   /* the SOUND snapshots: one record for each SOUND the wheel can hold, no more */
        uint32_t m, n = 0;
        for (m = 0; m < SH_NMODES; m++)
            n += (uint32_t)sh_wheel_avail(m);
        bad += check("snapshots: SH_NSNAP is the wheel's SOUNDs, each its own record",
                     n == SH_NSNAP && sh_snap_of(SH_NMODES - 1u) != sh_snap_of(0) && sh_snap_of(0) == &sh_snap[0]);
    }
    {   /* the brand: the wordmark at power-on (skippable), ABOUT inert, Nibs hidden on it (OCT- and OCT+ together) */
        int16_t bpm;
        uint32_t k;
        ui_power_on();
        memset(&sh, 0, sizeof sh);
        sframe();
        shot("splash");
        ok = sh.splash_t && screen_count(SC_BONE) > 1000u && screen_count(SC_BG) > 30000u;
        sframes(800);
        ok = ok && sh.splash_t;
        skey(5);
        bad += check("brand: sqrl's mark, SQRL-1 and the maker at power-on; any control skips it", ok && !sh.splash_t);
        memset(&sh, 0, sizeof sh);
        sframe();
        sframes(SH_SPLASH_MS + 64u);
        bad += check("brand: the wordmark goes by itself after SH_SPLASH_MS", !sh.splash_t && sh.ready);
        stap(B_GLO);
        ok = sh.global == SHG_SYSTEM;
        song.cpu_q8 = 100; audio_max_us = 2000;         /* (39 %, a 2 ms block: 68 %) */
        sframes(1100);
        ok = ok && sh.dsp == 39u && sh.dsp_pk == 68u && audio_max_us == 0u;
        shot("system_dsp");
        song.cpu_q8 = 0;
        bad += check("SYSTEM: the DSP meter, the ISR's average and last second's peak (audio_max_us reset)", ok);
        sturn(EN_PRESET, 1);                            /* a sound's card up: the meter waits, then comes back */
        ok = !sh_dsp_shown();
        sframes(SH_POP_MS + 100u);
        ok = ok && sh_dsp_shown() && sh.global == SHG_SYSTEM;
        sturn(EN_PRESET, -1);
        sframes(SH_POP_MS + 100u);
        bad += check("SYSTEM: the DSP meter comes back once a PRESETS card is gone (not hidden for good)", ok);
        stap(B_GLO);
        shot("system2");
        ok = ok && sh.global == SHG_SYSTEM2;
        stap(B_GLO);
        shot("projects");
        ok = ok && sh.global == SHG_PROJECT;
        stap(B_GLO);
        shot("about");
        ok = ok && sh.global == SHG_ABOUT && screen_count(SC_BONE) > 1000u;
        ok = ok && text_w(&AF_S, "v1.0.0-dev+0000000-dirty") <= 144;   /* (ABOUT's SQRL-1 row from x 92: a build's longest id) */
        bpm = song.g[G_BPM];
        sturn(EN_K1, 5);
        ok = ok && song.g[G_BPM] == bpm;
        k = (uint32_t)song.octave;
        stap(B_OCTUP);                                  /* one OCT alone: nothing, ABOUT stays */
        ok = ok && sh.global == SHG_ABOUT && (uint32_t)song.octave == k;
        sdown(B_OCTDN); sframe();
        sdown(B_OCTUP); sframe();
        shot("nibs");
        ok = ok && sh.global == SHG_NIBS && screen_count(SC_PAPER) > 20000u &&
             240u * 240u - screen_count(SC_PAPER) > 15000u;   /* (his face: about pi 84^2 of yellow) */
        sup(B_OCTUP); sup(B_OCTDN); sframe();
        ok = ok && sh.global == SHG_NIBS && (uint32_t)song.octave == k;
        stap(B_GLO);
        ok = ok && sh.global == SHG_SYSTEM;
        for (k = 0; k < 4u; k++)
            stap(B_GLO);
        ok = ok && sh.global == SHG_LEGAL;
        stap(B_GLO);
        bad += check("brand: GLO walks SYSTEM, SYSTEM 2, PROJECTS, ABOUT, LEGAL, SYSTEM; the knobs do nothing on ABOUT; "
                     "OCT- and OCT+ together there find Nibs", ok && sh.global == SHG_SYSTEM);
        shome();
        sframes(SH_TOAST_MS + 100u);
        {   /* the peek (5c): it starts at the idle time, never with a key held; sqrl bounces inside the screen and
             * finds a corner now and then; a touch: sqrl leaves, and the touch acts (PLAY plays at once), but a
             * knob's first detent only wakes; a second touch ends the leaving */
            uint32_t okp, okb = 1, okc, ok2, seed, tt, corners = 0, last, gap, gmax = 0, gmin = ~0u, b, c, sum = 0, nseed = 0;
            int32_t x, y;
            int16_t v;
            uint32_t idle_keep, okl, kq;
            uint8_t pos_keep[sizeof led_pos];
            memcpy(pos_keep, led_pos, sizeof led_pos);      /* (the keys on bits of their own: the stub has no map) */
            for (kq = 0; kq < 27u; kq++)
                led_pos[14u + kq] = (uint8_t)((1u + kq / 7u) << 3 | kq % 7u);
            ok = !sh_peek_ms();
            fm1_in.notes |= 1u << 4; keyboard_block();      /* a key held through the idle time: no peek */
            sframes(SH_PEEK_MS + 500u);
            okp = !sh_peek_ms();
            fm1_in.notes &= ~(1u << 4); keyboard_block(); sframe();
            sframes(SH_PEEK_MS - 200u);
            okp = okp && !sh_peek_ms();
            sframes(400);
            okp = okp && sh_peek_ms();
            sframes(3000u);
            shot("peek_bounce");
            okp = okp && screen_count(SC_PAPER) < 100u;     /* (no Nibs: none of his paper) */
            {                                               /* the keys: sqrl's light under it, mid on its middle */
                uint32_t t = sh_peek_ms() - 1u, kk, mids = 0, lits = 0, near = 0, dn = ~0u;
                int32_t hx;
                sh_sq_at(t - t % 40u, &x, &y, &b, &c);
                hx = (x + BR_SQRL52_W / 2) * 2;
                shell_leds();
                for (kk = 0; kk < 27u; kk++) {
                    int32_t d = hx - 16 * (int32_t)SH_KEY_X[kk];
                    uint32_t e = (uint32_t)(d < 0 ? -d : d);
                    mids += sh_key_lv(kk) == 2u;
                    lits += sh_key_lv(kk) == 3u;
                    if (e < dn)
                        dn = e, near = kk;
                }
                okl = (c < 400u || (mids >= 1u && !lits)) && sh_key_lv(near) >= 2u;
            }
            idle_keep = sh.idle_t;
            for (seed = 0; seed < 12u; seed++) {            /* twelve peeks, 20 minutes each: inside, and its corners */
                sh.idle_t = 1000u + seed * 7919u;
                last = 0;
                for (c = 0, tt = 0; tt < 20u * 60000u; tt += 40u) {
                    sh_sq_at(tt, &x, &y, &b, &c);
                    okb = okb && x >= 0 && x + BR_SQRL52_W <= 240 && y >= SH_Y_PIC && y + BR_SQRL52_H <= 240;
                    if (!c && tt + 1u > last + 1000u) {    /* (a corner now; a second sample of it, once) */
                        if (last) {                         /* (the gaps between corners) */
                            gap = tt + 1u - last;
                            gmax = gap > gmax ? gap : gmax;
                            gmin = gap < gmin ? gap : gmin;
                            sum += gap;
                            nseed++;
                        }
                        corners++;
                        last = tt + 1u;
                    }
                }
                if (!last)
                    okb = 0;                                /* (none in 20 minutes: too rare) */
            }
            sh.idle_t = idle_keep;
            printf("  (sqrl: %u corners in 12 x 20 min, a gap of %u s on average, %u s at most, %u s at least)\n", corners,
                   nseed ? sum / nseed / 1000u : 0u, gmax / 1000u, gmin / 1000u);
            okb = okb && corners >= 12u && sum / (nseed ? nseed : 1u) > 90000u;   /* (now and then: not every minute) */
            sh.page[sh.mode] = SHP_HOME; sframe();
            v = sh_trk()->p[sh_main_id(sh_eng(), 0u)];
            sturn(EN_K1, 1);                                /* the first detent: it wakes, it changes nothing */
            sframe();
            okc = sh_trk()->p[sh_main_id(sh_eng(), 0u)] == v && !sh_peek_ms() && sh_pk.leave;
            shot("peek_leave");
            {                                               /* sqrl leaves fast: off the screen by SH_LEAVE_MS */
                int32_t lx, ly;
                uint32_t lb;
                sh_leave_at(SH_LEAVE_MS, &lx, &ly, &lb);
                okc = okc && (lx <= -BR_SQRL52_W || lx >= 240 || ly <= SH_Y_PIC - BR_SQRL52_H || ly >= 240);
            }
            sframes(100);
            sturn(EN_K1, 1);                                /* the next: it turns, and ends the leaving */
            sframe();
            okc = okc && sh_trk()->p[sh_main_id(sh_eng(), 0u)] != v && !sh_pk.leave;
            sh_trk()->p[sh_main_id(sh_eng(), 0u)] = v;
            sframes(SH_PEEK_MS + 1200u);                    /* PLAY during the peek: it plays on that press */
            ok2 = sh_peek_ms() != 0;
            sdown(B_PLAY); sframe();
            srender(2); sframe();
            ok2 = ok2 && song.playing && sh_pk.leave;
            sup(B_PLAY); sframe();
            sframes(SH_LEAVE_MS + 100u);
            ok2 = ok2 && !sh_pk.leave;
            transport_req = 2; srender(4); sframe();
            printf("  (peek: idle and held key %u, bounce inside and its corners %u, the knob's wake %u, PLAY %u)\n", okp,
                   okb, okc, ok2);
            bad += check("brand: the peek at SH_PEEK_MS, never with a key held; sqrl bounces inside the screen, a corner "
                         "now and then; no Nibs; sqrl leaves fast; a knob's first detent only wakes, PLAY plays on its press",
                         ok && okp && okb && okc && ok2);
            bad += check("lights: the peek's sqrl glows on the keys under it", okl);
            shome();
            sframes(SH_TOAST_MS + 100u);
            {                                               /* a key let go fades: mid, then the glow, then dark */
                uint32_t k0, k1, k2, i, dip = 0;
                fm1_in.notes |= 1u << 4; keyboard_block(); sframes(100);
                shell_leds();
                k0 = sh_key_lv(4);
                fm1_in.notes &= ~(1u << 4); keyboard_block(); sframe();
                shell_leds();
                k1 = sh_key_lv(4);
                sframes(250);
                shell_leds();
                k2 = sh_key_lv(4);
                sframes(400);
                shell_leds();
                stap(B_HOME);                               /* TAB: HOME dips; again soon after: HOME blinks */
                for (i = 0; i < 4u; i++)
                    dip |= sh_lfx[i].kind == LFX_DIP && sh_lfx[i].id == panel.btn[B_HOME];
                sframes(400);
                stap(B_HOME);
                printf("  (key trail: held %u, let go %u, 250 ms %u, then %u; TAB's dip %u)\n", k0, k1, k2, sh_key_lv(4), dip);
                bad += check("lights: a key let go fades from mid through the glow to dark; TAB dips HOME",
                             k0 == 3u && k1 == 2u && k2 == 1u && sh_key_lv(4) == 0u && dip);
            }
            memcpy(led_pos, pos_keep, sizeof led_pos);
        }
    }

    shell_reset();
    bad += check("shell: boots into FM6 on part 1, its HOME page, the keys the ISR's (HOME, GLO, SEQ, FX, REC aside)",
                 sh.ready && sh.mode == 0u && (*sh_trk()).eng_req == ENGI_FM6 && sh.page[0] == SHP_HOME &&
                 kb_mask == ((1u << panel.btn[B_HOME]) | (1u << panel.btn[B_GLO]) | (1u << panel.btn[B_SEQ]) |
                             (1u << panel.btn[B_FX]) | (1u << panel.btn[B_REC])) && perf_mask == 1u << panel.btn[B_FX] &&
                 song.sel == 0u);
    shot("home");
    bad += check("shell: the whole screen drawn (no pixel left as it was at power-on)", !count_zero());
    bad += check("shell: the screen drawn (header, picture, legend)",
                 screen_ink(0, 20) > 50u && screen_ink(20, 196) > 500u && screen_ink(196, 240) > 200u);

    stap(B_EDIT);
    ok = sh.page[0] == SHP_EDIT1;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_EDIT2;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_FMOP;                       /* (FM6: its operators, ui_fm6op.c) */
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_FMOP2;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_FMENV;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_SHAPE;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_VOICE;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_GLIDE;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_HOME;
    stap(B_EDIT);
    ok &= sh.page[0] == SHP_EDIT1;
    stap(B_ENV);
    ok &= sh.page[0] == SHP_ENV;
    stap(B_ENV);
    ok &= sh.page[0] == SHP_ENVD;
    stap(B_ENV);
    ok &= sh.page[0] == SHP_HOME;
    stap(B_ENV);
    ok &= sh.page[0] == SHP_ENV;
    bad += check("pages: FM6's EDIT walks EDIT 1, 2, OPERATOR, 2, OP ENV, SHAPE, VOICE, GLIDE, MAIN, round; ENV: ENV, ENV DEST, MAIN", ok);
    {   /* the FX hold layer: FX held, a white key plays its effect (no note), KNOB 1 is FILTER; letting go the
         * macros snap back and the page stays; past the hold the map shows; with GLO, FX is its card, no effect */
        uint32_t kc = (uint32_t)sh_white_key(4), pg;   /* (C4: LPF by default) */
        int16_t d0;
        shome();
        pg = sh.page[sh.mode];
        d0 = sh_trk()->p[P_DIST];
        sdown(B_FX);
        sframe();
        fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
        ok = (perf_held >> PF_LPF) & 1u && kb_note[kc] == KB_SILENT && (kb_layer >> kc) & 1u && sh_fx_shown();
        shot("fx_layer");
        ok = ok && screen_count(SC_PANEL) > 54u * 24u * 10u;
        fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
        ok = ok && !perf_held;
        sturn(EN_K1, -10);
        ok = ok && perf_k[0] < 0 && sh_trk()->p[P_DIST] == d0;
        sup(B_FX);
        sframe();
        ok = ok && !perf_k[0] && sh.page[sh.mode] == pg && !sh_fx_shown();
        sdown(B_FX);
        sframes(400);
        ok = ok && sh_fx_shown();
        sup(B_FX);
        sframe();
        ok = ok && sh.page[sh.mode] == pg;
        sdown(B_GLO); sframe();
        stap(B_FX);
        ok = ok && sh.help == B_FX + 1u;
        sdown(B_FX); sframe();
        fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
        ok = ok && !perf_held && !sh_fx_shown();
        fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
        sup(B_FX); sup(B_GLO); sframe();
        {   /* (every label within its 54 px cell, the card's row within the card) */
            static const char *const LB[] = {"FILTER", "CRUSH", "THROW", "DEPTH", "SHIMR", "MUTE 4"};
            uint32_t e;
            for (e = 0; e < NELEM(LB); e++)
                ok = ok && text_w(&AF_S, LB[e]) <= 52;
            for (e = 0; e < PF_NFX; e++)
                ok = ok && text_w(&AF_S, PF_CELL[e]) + 12 <= 52;   /* (the key's letter beside it) */
            ok = ok && text_w(&AF_M, "100 HP") <= 54 && text_w(&AF_S, "HOLD + KEYS: FX ON THE MIX") <= 196;
        }
        bad += check("FX layer: FX held, a key plays its effect and no note, KNOB 1 FILTER; let go, back; GLO + FX its card", ok);
        {   /* HOME's labels sit 13 px into a 60 px cell (sh_draw_legend): every engine's four, the shell's by kind */
            uint32_t e, k, fit = 1;
            for (e = 0; e < NENGINES; e++)
                for (k = 0; k < 4u; k++)
                    fit &= text_w(&AF_S, param_desc_of(e, sh_main_id(e, k))->label) <= 46;
            for (e = 1; e < NELEM(SHK_HOME); e++)
                for (k = 0; k < 4u; k++)
                    if (SHK_HOME[e][k] >= SP_FIRST && SHK_HOME[e][k] < SP_NE0)
                        fit &= text_w(&AF_S, SP_DESC[SHK_HOME[e][k] - SP_FIRST].label) <= 46;
            for (e = 0; e < SH_NMODES; e++)            /* (RUDIMENT's HOME: the SOUND's main knob first) */
                if (SH_MODES[e].hero < SP_FIRST && !SH_MODES[e].lib)
                    fit &= text_w(&AF_S, param_desc_of(SH_MODES[e].engine, SH_MODES[e].hero)->label) <= 46;
            bad += check("HOME: every engine's and kind's knob labels fit their cell (MOUTH's GRIT)", fit);
        }
        {   /* the edges: a key in FX's own frame is not lost; the black keys mute and unmute; HOME and SEQ win; FX let
             * go mid-effect leaves nothing stuck, stopped or playing; FX lit while held */
            uint32_t kb = 1u, run, lit;               /* (F#3: part 1's mute) */
            uint8_t pos0[sizeof led_pos];
            memcpy(pos0, led_pos, sizeof pos0);         /* (the host's key map is a zero stub: FX given a place) */
            led_pos[panel.btn[B_FX]] = 0u << 3 | 1u;
            shome();
            sdown(B_FX);                                /* no frame between: the ISR sees FX before the shell does */
            fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block();
            ok = (perf_held >> PF_LPF) & 1u;
            sframe();
            fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
            fm1_in.notes |= 1u << kb; host_notes |= 1u << kb; keyboard_block(); sframe();
            ok = ok && (perf_held >> PF_M1) & 1u && kb_note[kb] == KB_SILENT;
            lit = sh_led_on(panel.btn[B_FX]);
            fm1_in.notes &= ~(1u << kb); keyboard_block(); sframe();
            ok = ok && !perf_held;
            sup(B_FX); sframe();
            ok = ok && lit && !sh_led_on(panel.btn[B_FX]) && sh.page[sh.mode] == pg;
            memcpy(led_pos, pos0, sizeof pos0);
            bad += check("FX layer: a key in FX's first frame plays; F#3 mutes part 1 and unmutes; FX lit while held", ok);
            ok = 1;
            sdown(B_HOME); sframe();                    /* HOME (the wheel past its hold) and SEQ win over FX */
            sdown(B_FX); sframe();
            fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
            ok = !perf_held && !sh_fx_shown();
            sframes(SH_TAP_MS + 100u);
            ok = ok && sh.wheel && !perf_held && !sh_fx_shown();
            fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
            sup(B_FX); sframe(); sup(B_HOME); sframes(200);
            shome(); sframes(SH_TOAST_MS + 200u);
            sdown(B_SEQ); sframe();
            sdown(B_FX); sframe();
            fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
            ok = ok && !perf_held && !sh_fx_shown();
            fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
            sup(B_FX); sframe(); sup(B_SEQ); sframe();
            step_clear(&sh_trk()->step[4]);             /* (SEQ + C4 entered a step: gone again) */
            bad += check("FX layer: HOME (and the wheel) and SEQ held keep the keys theirs, no effect", ok);
            ok = 1;
            for (run = 0; run < 2u; run++) {            /* FX let go before the key: the effect ends with the key */
                if (run)
                    stap(B_PLAY);
                sdown(B_FX); sframe();
                fm1_in.notes |= 1u << kc | 1u << kb; host_notes |= 1u << kc | 1u << kb; keyboard_block(); sframe();
                ok = ok && (perf_held >> PF_LPF) & 1u && (perf_held >> PF_M1) & 1u;
                sup(B_FX); sframe();
                srender(8);
                fm1_in.notes &= ~(1u << kc | 1u << kb); keyboard_block(); sframe();
                srender(8);
                ok = ok && !perf_held && !perf_latched && !kb_layer && !perf_k[0] && song.playing == run;
                if (run) {
                    stap(B_PLAY);
                    srender(2);                         /* (the stop taken by the ISR) */
                    ok = ok && !song.playing;
                }
            }
            for (run = 0; run < NTRK; run++)            /* (the run's voices gone: the tests after start silent) */
                memset(trk[run].v, 0, sizeof trk[run].v);
            bad += check("FX layer: FX let go mid-effect, stopped and playing: nothing stuck once the keys are up", ok);
            {   /* a note held into FX ends with its key; a key held out of FX starts no note when FX lets go */
                uint32_t t, i, g;
#define SH_GATES(g) do { g = 0; for (t = 0; t < NTRK; t++) for (i = 0; i < NELEM(trk[t].v); i++) g += trk[t].v[i].gate; } while (0)
                fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
                srender(2);
                SH_GATES(g);
                ok = g > 0u && kb_note[kc] != KB_SILENT;
                sdown(B_FX); sframe();
                fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
                srender(2);
                SH_GATES(g);
                ok = ok && !g;                          /* (kb_note keeps the note, as after any key-up) */
                sup(B_FX); sframe();
                srender(2);
                SH_GATES(g);
                ok = ok && !g && !perf_held;
                sdown(B_FX); sframe();
                fm1_in.notes |= 1u << kc; host_notes |= 1u << kc; keyboard_block(); sframe();
                ok = ok && (perf_held >> PF_LPF) & 1u;
                sup(B_FX); sframe();
                srender(2);
                SH_GATES(g);
                ok = ok && !g && kb_note[kc] == KB_SILENT && (perf_held >> PF_LPF) & 1u;   /* (ends with its key) */
                fm1_in.notes &= ~(1u << kc); keyboard_block(); sframe();
                srender(2);
                SH_GATES(g);
                ok = ok && !g && !perf_held && !perf_k[0];
#undef SH_GATES
                for (t = 0; t < NTRK; t++)
                    memset(trk[t].v, 0, sizeof trk[t].v);
                bad += check("FX layer: a note held into FX ends with its key; a key held out of FX starts no note", ok);
            }
        }
    }
    {   /* FX walks FX, INSERT, INSERT 2 and the shared DELAY, REVERB, CHORUS: those turn song.g, every part's */
        uint8_t ids[4];
        int glob, g0;
        uint64_t walk = 0;
        uint32_t k;
        shome();
        for (k = 0; k < 9u; k++) { stap(B_FX); walk = walk * 100u + sh.page[0]; }
        ok = walk == (((((((SHP_FX * 100ull + SHP_INS) * 100u + SHP_INS2) * 100u + SHP_DELAY) * 100u + SHP_DELAY2) * 100u +
                      SHP_REVERB) * 100u + SHP_CHORUS) * 100u + SHP_HOME) * 100u + SHP_FX;
        stap(B_FX); stap(B_FX); stap(B_FX);             /* DELAY */
        sh_page_ids(ids, &glob);
        g0 = song.g[G_DMIX];
        sturn(EN_K4, -3);
        ok = ok && glob && ids[0] == G_DTIME && ids[3] == G_DMIX && song.g[G_DMIX] < g0;
        song.g[G_DMIX] = (int16_t)g0;
        stap(B_FX);                                     /* DELAY 2: the tape echo */
        sh_page_ids(ids, &glob);
        ok = ok && glob && ids[0] == G_TDHEADS && ids[1] == SP_WOW && ids[2] == G_TDSAT && ids[3] == G_TDREV;
        sturn(EN_K1, 6);                                /* HEADS up: song.g, the song's */
        ok = ok && song.g[G_TDHEADS] > 0;
        sturn(EN_K2, -3);                               /* WOW: the shell's, copied to the bus */
        ok = ok && sh_sp[SP_WOW - SP_FIRST] < 10 && fx_wow == (uint8_t)sh_sp[SP_WOW - SP_FIRST];
        song.g[G_TDHEADS] = 0; sh_sp[SP_WOW - SP_FIRST] = 10;
        stap(B_FX);                                     /* REVERB, PRE */
        sh_page_ids(ids, &glob);
        ok = ok && glob && ids[0] == G_RTYPE && ids[1] == G_RSIZE && ids[2] == G_RDAMP && ids[3] == SP_PRE;
        stap(B_FX);                                     /* CHORUS, MODE */
        sh_page_ids(ids, &glob);
        ok = ok && glob && ids[0] == G_CRATE && ids[1] == G_CDEPTH && ids[2] == SP_CMODE;
        stap(B_FX); stap(B_FX);
        sh_page_ids(ids, &glob);
        ok = ok && !glob && ids[0] == P_DIST;
        bad += check("pages: FX walks FX, INSERT, INSERT 2, DELAY, DELAY 2, REVERB, CHORUS (shared: song.g, the shell's), MAIN, round", ok);
        {   /* the buses: ENS puts the chorus on the stereo side path; the tape echo repeats; back to CHOR, mono again */
            static int32_t ci[CTL], di[CTL], ri[CTL], w[CTL];
            uint32_t b, i2;
            int32_t pk = 0;
            sh_sp[SP_CMODE - SP_FIRST] = 1; sframe();
            for (b = 0; b < 400u; b++) {
                for (i2 = 0; i2 < CTL; i2++) { ci[i2] = b < 40u ? (int32_t)((i2 * 997u) % 4000u) - 2000 : 0; di[i2] = ci[i2]; ri[i2] = 0; }
                song.g[G_RTYPE] = 0;
                fx_buses(ci, di, ri, w, CTL);
                if (b > 60u)
                    for (i2 = 0; i2 < CTL; i2++) pk = w[i2] > pk ? w[i2] : -w[i2] > pk ? -w[i2] : pk;
            }
            ok = hl.side && pk > 0;                     /* (the echo still sounding after the input stopped) */
            sh_sp[SP_CMODE - SP_FIRST] = 0; sframe();
            for (b = 0; b < 4u; b++) fx_buses(ci, di, ri, w, CTL);
            ok = ok && !hl.side;
            bad += check("FX: ENS on the stereo side path, the tape echo repeats, CHOR mono again", ok);
            {   /* the shell's settings kept: a change waits for silence and its page left, then goes to the settings
                 * object (favorites row 13, bytes 16..: SLICE's presets 128.. that no engine has) */
                uint32_t e, most = 0, q;
                for (e = 0; e < NENGINES; e++)
                    if (ENGINES[e] && ENGINES[e]->npresets > most)
                        most = ENGINES[e]->npresets;
                ok = most < 128u;
                sh_sp[SP_WOW - SP_FIRST] = 33; sframe();
                ok = ok && sh_st.pending && SH_ST[0] == SH_ST_MARK && SH_ST[1] == 34u;
                trk[0].v[0].active = 1;                 /* a voice sounding: it waits, past its page and the idle */
                shome(); sframes(SH_ST_IDLE_MS + 500u);
                ok = ok && sh_st.pending;
                trk[0].v[0].active = 0;
                for (q = 0; q < NTRK; q++) memset(trk[q].v, 0, sizeof trk[q].v);
                fx_wet_peak = 5000; sframe();           /* an echo still ringing: it waits for that too */
                ok = ok && sh_st.pending;
                sframe();
                ok = ok && !sh_st.pending;
                sh_sp[SP_WOW - SP_FIRST] = 10;          /* power-on: back as saved */
                sh_store_load();
                ok = ok && sh_sp[SP_WOW - SP_FIRST] == 33;
                sh_sp[SP_WOW - SP_FIRST] = 10; sframes(SH_ST_IDLE_MS + 500u);
                bad += check("shell settings: a change waits for silence, then is kept (WOW back after power-on)", ok);
            }
            {   /* the loudest the echo gets: three heads in phase (a full-scale DC send), full feedback, MIX 127: its
                 * level into the wet bus never wraps (the wet keeps the echo's sign) */
                int16_t keep[5] = {song.g[G_TDHEADS], song.g[G_DFDBK], song.g[G_TDSAT], song.g[G_DMIX], song.g[G_DCOLOR]};
                uint32_t flips = 0, big = 0;
                song.g[G_TDHEADS] = 6; song.g[G_DFDBK] = 120; song.g[G_TDSAT] = 0; song.g[G_DMIX] = 127;
                song.g[G_DCOLOR] = 127;
                for (b = 0; b < 600u; b++) {
                    for (i2 = 0; i2 < CTL; i2++) { ci[i2] = 0; ri[i2] = 0; di[i2] = 65535; }
                    fx_buses(ci, di, ri, w, CTL);
                    for (i2 = 0; i2 < CTL; i2++) {
                        flips += (w[i2] > 0 && fx_dw[i2] < 0) || (w[i2] < 0 && fx_dw[i2] > 0);
                        big += fx_dw[i2] > 40000 || fx_dw[i2] < -40000;
                    }
                }
                song.g[G_TDHEADS] = keep[0]; song.g[G_DFDBK] = keep[1]; song.g[G_TDSAT] = keep[2];
                song.g[G_DMIX] = keep[3]; song.g[G_DCOLOR] = keep[4];
                bad += check("FX: three heads at full feedback, MIX 127: the echo's level never wraps (no sign flip)",
                             !flips && big > 0);
            }
        }
    }
    {   /* GLIDE SPREAD: each note of a chord in its own time, ranked by pitch among the held notes, so a chord played
         * low to high (as chord.c starts it) and one played high to low (by hand) spread the same */
        track_t *t = sh_trk();
        voice_t *v = t->v;
        int16_t keep[4] = {t->p[P_GLIDE], t->p[P_GLMODE], t->p[P_GSPR], t->p[P_GORD]};
        static const uint8_t UP[3] = {0, 1, 2}, DOWN[3] = {2, 1, 0};
        int32_t st[2][3], tm[3], t0 = 0;
        uint32_t k, o, run;
        t->p[P_GLIDE] = 50; t->p[P_GLMODE] = 1;         /* (TIME, ~38 ms: 13 blocks) */
        for (run = 0; run < 4u; run++) {                /* GSPR 0; LOW up and down; HIGH up */
            t->p[P_GSPR] = run ? 127 : 0;
            t->p[P_GORD] = run == 3u ? 2 : 1;
            for (k = 0; k < NVOICE; k++) { v[k].active = 0; v[k].gate = 0; v[k].gstep = 0; }
            for (o = 0; o < 3u; o++) {
                k = run == 2u ? DOWN[o] : UP[o];        /* voice k plays the k-th note up: C E G two octaves up */
                v[k].pitch_cur = 36 * 16; v[k].pitch16 = (60 + (int32_t)(k * 4u)) * 16;
                v[k].active = 1; v[k].gate = 1;
                glide_set(t, &v[k], 1);
            }
            for (k = 0; k < 3u; k++)                    /* each note's glide time, in ticks */
                tm[k] = (int32_t)(((int64_t)(v[k].pitch16 - v[k].pitch_cur) << 16) / (v[k].gstep ? v[k].gstep : 1));   /* (Q16) */
            if (run == 0u)
                ok = tm[2] - tm[0] <= 2 && tm[0] - tm[2] <= 2;
            for (k = 0; k < 3u; k++)
                if (run == 1u || run == 2u)
                    st[run - 1u][k] = tm[k];
            if (run == 0u)
                t0 = tm[0];
            if (run == 3u)                              /* HIGH: the top note lands first, LOW's mirror */
                ok = ok && tm[2] < tm[1] && tm[1] < tm[0] && tm[2] - st[0][0] <= 4 && st[0][0] - tm[2] <= 4 &&
                     tm[0] - st[0][2] <= 4 && st[0][2] - tm[0] <= 4;   /* (within the steps' rounding) */
        }
        ok = ok && st[0][0] < st[0][1] && st[0][1] < st[0][2];   /* LOW: the low note lands first */
        ok = ok && st[0][0] - t0 <= 2 && t0 - st[0][0] <= 2;   /* (the first at 1x the GLIDE) */
        {   /* a note already moving keeps its time when a later key re-ranks the chord */
            int32_t g;
            for (k = 0; k < NVOICE; k++) { v[k].active = 0; v[k].gate = 0; v[k].gstep = 0; }
            t->p[P_GORD] = 1;
            v[1].pitch_cur = 36 * 16; v[1].pitch16 = 64 * 16; v[1].active = 1; v[1].gate = 1;
            glide_set(t, &v[1], 1);
            v[1].pitch_cur += 16;                       /* (it has moved) */
            g = v[1].gstep;
            v[0].pitch_cur = 36 * 16; v[0].pitch16 = 60 * 16; v[0].active = 1; v[0].gate = 1;
            glide_set(t, &v[0], 1);
            ok = ok && v[1].gstep == g;
        }
        for (k = 0; k < 3u; k++)
            ok = ok && st[0][k] == st[1][k];            /* played high to low: the same */
        for (k = 0; k < NVOICE; k++) { v[k].active = 0; v[k].gate = 0; v[k].pitch_cur = v[k].pitch16; v[k].gstep = 0; }
        t->p[P_GLIDE] = keep[0]; t->p[P_GLMODE] = keep[1]; t->p[P_GSPR] = keep[2]; t->p[P_GORD] = keep[3];
        bad += check("GLIDE SPREAD: ranked by pitch: LOW each note up slower, the same played up or down; HIGH", ok);
    }
    {   /* TONNETZ's VOIC on C major: CLOSE OPEN WIDE as they always were, then CHORD's INV1 INV2 DROP2 by pitch, and VAR
         * one of those six per chord */
        static const int8_t W[6][3] = {{48, 52, 55}, {36, 52, 55}, {36, 52, 67}, {52, 55, 60}, {55, 60, 64}, {40, 48, 55}};
        tz_t z;
        int n[3], v, i, j, x, okz = 1, any;
        for (i = 0; i <= TZ_VAR; i++) {
            tz_init(&z);
            tz_param(&z, 3, (int16_t)i);
            tz_set(&z, 0, 0);
            z.base = 48;
            for (v = 0; v < 3; v++)
                n[v] = tz_note(&z, v);
            for (v = 1; v < 3; v++)                     /* ascending */
                for (j = v; j > 0 && n[j - 1] > n[j]; j--) { x = n[j]; n[j] = n[j - 1]; n[j - 1] = x; }
            if (i < TZ_VAR)
                okz = okz && n[0] == W[i][0] && n[1] == W[i][1] && n[2] == W[i][2];
            else {
                for (any = 0, j = 0; j < 6; j++)
                    any |= n[0] == W[j][0] && n[1] == W[j][1] && n[2] == W[j][2];
                okz = okz && any && z.var < 6u;
            }
        }
        bad += check("TONNETZ VOIC: CLOSE OPEN WIDE unchanged, INV1 INV2 DROP2 by pitch, VAR one of them a chord", okz);
    }
    {   /* glide, LEGATO, an octave up with the first key still held: every GLD its own time (90 < 110 < 127, in RATE
         * and TIME), TIME's 127 the 10.0 s it shows, and every SOUND glides under LEG at a long GLD (none stalls) */
        static const char *const SND[] = {"ANALOG", "PHASE", "PHYS", "FM6", "LIZARD", "SHOCK", "MOUTH", "ORGAN"};
        static const uint8_t G[3] = {90, 110, 127};
        uint32_t mode, i, okg = 1, oke = 1, m0 = sh.mode, n0 = sh.note;
        float sec[2][3];
        for (mode = 0; mode < 2u; mode++)
            for (i = 0; i < 3u; i++) {
                sgoc(slot_of("ANALOG"), note_of("KEYS"));
                sec[mode][i] = sglide(sh_trk(), G[i], mode, 60u, 72u, 14.0f);
            }
        for (mode = 0; mode < 2u; mode++)
            okg = okg && sec[mode][0] > 0.0f && sec[mode][0] < sec[mode][1] && sec[mode][1] < sec[mode][2];
        okg = okg && sec[1][2] > 9.5f && sec[1][2] < 10.5f;   /* (TIME: what the knob shows) */
        okg = okg && sec[0][2] > 6.5f && sec[0][2] < 8.5f;   /* (RATE at 127: an octave in ~7.5 s) */
        printf("  (glide an octave: RATE %.2f %.2f %.2f s, TIME %.2f %.2f %.2f s)\n", (double)sec[0][0], (double)sec[0][1],
               (double)sec[0][2], (double)sec[1][0], (double)sec[1][1], (double)sec[1][2]);
        for (i = 0; i < NELEM(SND); i++) {
            float t1;
            sgoc(slot_of(SND[i]), note_of("KEYS"));
            t1 = sglide(sh_trk(), 110u, 0u, 60u, 72u, 4.0f);
            if (!(t1 > 0.3f && t1 < 4.0f)) {
                printf("  (LEG glide on %s: %.2f s)\n", SND[i], (double)t1);
                oke = 0;
            }
        }
        sgoc(m0, n0);
        bad += check("glide: GLD 90 < 110 < 127 each longer in RATE and TIME, TIME's 127 is 10 s; LEG glides on every SOUND",
                     okg && oke);
    }
    {   /* an operator page: KNOB 1 picks the operator, KNOB 4 writes its LEVEL into the part's patch (SLOT OWN) */
        uint32_t b;
        uint8_t lv;
        sh.page[0] = SHP_FMOP;
        for (b = 0; b < 6u; b++) sturn(EN_K1, -1);
        sturn(EN_K1, 1); sturn(EN_K1, 1);               /* OP 3 (a detent at a time: no acceleration) */
        b = (5u - fop_op) * FP_OP + FP_OL;
        lv = fm6_patch[sh.part][b];
        sturn(EN_K4, lv > 50 ? -1 : 1);
        shot("fm6_operator");
        ok = fop_op == 2u && fm6_patch[sh.part][b] != lv && fm6_slot[sh.part] == FM6_OWN;
        sh.page[0] = SHP_FMENV;
        sframe();
        shot("fm6_openv");
        bad += check("FM6 OPERATOR: KNOB 1 picks OP 3, KNOB 4 writes its LEVEL into the part's patch (SLOT OWN)", ok);
        sh.page[0] = SHP_ENV;                           /* (where the test before left it) */
        sframe();
    }

    v = (*sh_trk()).p[P_ATK];
    sturn(EN_K1, 3);
    shot("env");
    bad += check("knobs: KNOB 1 on ENV turns ATK; its card lit", (*sh_trk()).p[P_ATK] != v && sh.hot == 0u);

    stap(B_HOME);                                       /* TAB: the NOTE view (KEYS: ARP), then back to ENV */
    ok = sh_view_note() && sh.page[0] == SHP_ARP;
    sframes(400);
    stap(B_HOME);
    ok = ok && sh.page[0] == SHP_ENV && !sh.global;
    sframes(400);
    sdown(B_GLO); sframe(); sup(B_GLO); sframe();       /* a shell page: HOME back to the view it came from */
    stap(B_HOME);
    ok = ok && sh.page[0] == SHP_ENV && !sh.global;
    sframes(400);
    stap(B_ENV);                                        /* ENV walked past its last: SOUND MAIN */
    ok = ok && sh.page[0] == SHP_ENVD;
    stap(B_ENV);
    bad += check("TAB: HOME flips SOUND and NOTE, each where it was left; from a shell page, back; ENV walks into MAIN",
                 ok && sh.page[0] == SHP_HOME && !sh.global);

    sdown(B_HOME);
    sframes(400);
    ok = sh.wheel == 1u;
    shot("wheel_pre");
    skey((uint32_t)sh_slot_key(sh_mode_pos(mode_of(G4))));   /* G4's slot key: TALK, or MOUTH without it */
    ok &= sh.wsel == sh_mode_pos(mode_of(G4)) && sh.wsel == 4u && sh.wheel;
    shot("wheel_talk");
    sup(B_HOME);
    sframe();
    bad += check("TALK: offered only in a build with it (FELUCCA_TALK); without it MOUTH holds G4 by default",
                 sh_wheel_avail(mode_of("TALK")) == (FELUCCA_TALK != 0) && !strcmp(SH_MODES[SH_G4].name, G4) &&
                 eng_ok(ENGI_TALK) == (FELUCCA_TALK != 0));
    bad += check("wheel: HOME held opens it, a key picks a slot (G4's), letting go enters it",
                 ok && !sh.wheel && sh.mode == mode_of(G4) && (*sh_trk()).eng_req == G4_ENG);
    bad += check("  no note sounded while HOME held the keys (kb_mask)", !gates());

    k2 = sh_main_id((*sh_trk()).eng_req, 1u);           /* G4's HOME: KNOB 2's parameter (TALK's RATE, MOUTH's SPEED) */
    v = (*sh_trk()).p[k2];
    sturn(EN_K2, 5);
    keep = (*sh_trk()).p[k2];
    sdown(B_GLO);
    sframe();
    skey(7);                                            /* GLO + C4: FM6 */
    sup(B_GLO);
    sframe();
    ok = sh.mode == 0u && (*sh_trk()).eng_req == ENGI_FM6;
    stap(B_HOME);
    sframes(100);
    stap(B_HOME);
    bad += check("SHIFT + C4 jumps to FM6; HOME twice: back to G4's nut, its turned KNOB 2 kept",
                 ok && keep != v && sh.mode == mode_of(G4) && (*sh_trk()).eng_req == G4_ENG && (*sh_trk()).p[k2] == keep);

    v = (int16_t)(*sh_trk()).preset;
    sturn(EN_PRESET, 1);
    shot("presets");
    bad += check("PRESETS: the next sound of the mode, on any page; its card", (*sh_trk()).preset != (uint8_t)v && sh.pop == 1u);

    v = (*sh_trk()).p[sh_cur()->hero];
    sturn(EN_ALGO, 1);
    bad += check("ALGORITHM: the mode's main choice (its hero: TALK's BANK, MOUTH's PHRASE)",
                 (*sh_trk()).p[sh_cur()->hero] != v && sh.pop == 2u);

    sdown(B_PLAY);
    sframe();
    ok = transport_req == 1u;
    sup(B_PLAY);
    sframe();
    sdown(B_OCTUP);
    sframe();
    ok &= song.octave == 1;
    sup(B_OCTUP);
    sframe();
    bad += check("PLAY and OCT+ act on the press (before the release)", ok && song.octave == 1);
    transport_req = 0;
    song.octave = 0;

    stap(B_ENV);
    v = (*sh_trk()).p[P_ATK];
    sdown(B_SAVE);
    sframes(400);
    sturn(EN_K1, 6);
    ok = sh.mom && (*sh_trk()).p[P_ATK] != v;
    sup(B_SAVE);
    sframe();
    ok &= (*sh_trk()).p[P_ATK] == v && !sh.mom;
    sdown(B_SAVE);
    sframes(400);
    sturn(EN_K1, 6);
    keep = (*sh_trk()).p[P_ATK];
    sdown(B_OCTUP);
    sframe();
    sup(B_OCTUP);
    sframe();
    sup(B_SAVE);
    sframe();
    bad += check("MOMENTARY: SAVE held, knobs turned, let go: back; OCT+ while held: kept (no octave)",
                 ok && (*sh_trk()).p[P_ATK] == keep && keep != v && song.octave == 0);

    song.rec = 0;
    sdown(B_REC);
    sframes(1300);
    sup(B_REC);
    sframe();
    ok = song.rec == 0u;
    sdown(B_REC);
    sframes(600);
    sup(B_REC);
    sframe();
    bad += check("REC held through the ring: no arm left; let go early: the arm stays, nothing cleared",
                 ok && song.rec == 1u << sh.part && !sh.ring);
    song.rec = 0;

    stap(B_GLO);
    v = song.g[G_BPM];
    sturn(EN_K1, 4);
    ok = sh.global == SHG_SYSTEM && song.g[G_BPM] != v;
    stap(B_SCL);
    sturn(EN_K1, 2);
    ok &= sh.global == SHG_SCALE && trk[1].p[P_ROOT] == trk[0].p[P_ROOT] && trk[3].p[P_ROOT] == trk[0].p[P_ROOT];
    bad += check("GLO: SYSTEM (KNOB 1 the tempo); SEL: SCALE, its ROOT set on every part", ok);

    shome();
    v = song.g[G_BPM];
    sdown(B_GLO);
    sframe();
    sturn(EN_SELECT, 3);
    sup(B_GLO);
    sframe();
    bad += check("GLO + SELECT: the tempo from any page", song.g[G_BPM] != v && !sh.global);

    sdown(B_GLO);
    sframe();
    stap(B_REC);
    shot("help");
    ok = sh.help == B_REC + 1u;
    sup(B_GLO);
    sframe();
    bad += check("GLO + a button: its card; let go of GLO: gone", ok && !sh.help);

    {   /* parts: a new PLAY pair on the other part; the one left loops on (unmuted); no undo entry for a switch */
        uint32_t from = sh.part, to, u = undo.trk;
        uint8_t fm6a[FP_SIZE + 1u];
        sdown(B_GLO); sframe(); skey(7); sup(B_GLO); sframe();          /* FM6 */
        memcpy(fm6a, fm6_patch[sh.part], sizeof fm6a);
        from = sh.part;
        sdown(B_GLO); sframe(); skey(9); sup(B_GLO); sframe();          /* LIZARD */
        to = sh.part;
        ok = to == (from ^ 1u) && song.sel == to && trk[to].eng_req == ENGI_LIZARD && !trk[to].p[P_MUTE];
        ok &= trk[from].p[P_MUTE] == 0 && sh_partpair[from] == (uint16_t)(slot_of("FM6") | 0u << 8);   /* (it loops on) */
        ok &= trk[2].p[P_MUTE] == 1 && trk[3].p[P_MUTE] == 1;
        bad += check("parts: the next PLAY pair on the other part, the one left looping on (unmuted), 3 and 4 silent", ok);
        sdown(B_GLO); sframe(); skey(7); sup(B_GLO); sframe();          /* FM6 again: back on its own part */
        ok = sh.part == from;
        bad += check("  FM6 comes back to the part it loops on, with its own patch",
                     ok && trk[sh.part].eng_req == ENGI_FM6 && !memcmp(fm6_patch[sh.part], fm6a, FP_SIZE));
        bad += check("  a mode switch leaves the undo alone", undo.trk == u);
    }

    sframes(2000);                                      /* (the toasts and cards gone) */
    rows = host_blit_rows;
    sframes(200);
    bad += check("the screen: nothing redrawn while nothing changes", host_blit_rows == rows);

    sdown(B_HOME);
    sframes(400);
    sturn(EN_SELECT, (int32_t)SH_SLOTS - (int32_t)sh.wsel);    /* to the centre */
    shot("wheel_centre");
    ok = sh.wsel == SH_SLOTS && screen_ink(0, 240) > 1000u;
    sup(B_HOME);
    sframe();
    bad += check("wheel: SELECT to its centre, let go: SYSTEM", ok && sh.global == SHG_SYSTEM);

    {   /* the pictures, mode by mode (SHELL_SHOTS: one screen each) */
        uint32_t m, k, w;
        char nm[32];
        ok = 1;
        for (w = 0; w < SH_SLOTS; w++) {                /* (the wheel's slots: what a player reaches) */
            const char *n;
            if ((m = sh_wslot[w]) == SH_EMPTY)
                continue;
            n = SH_MODES[m].name;
            if (sh.mode != m) {                         /* (by its slot: a catalogue index need not be one; DRUMS
                                                         * with RUDIMENT, its classic pair) */
                sdown(B_HOME); sframes(SH_TAP_MS + 100u);
                skey((uint32_t)sh_slot_key(w));
                skey((uint32_t)sh_note_key(str_eq(n, "DRUMS") ? sh_note_pos(note_of("RUDIMENT")) : 0u));
                sup(B_HOME); sframe();
            }
            shome();
            if (str_eq(n, "SYLLABLE")) {                /* its HELLO phrase, three notes */
                apply_preset_to(sh_trk(), 8);
                srender(8);
                for (k = 0; k < 3u; k++) skey_sung(0);
            }
            if (str_eq(n, "TALK"))
                skey_sung(0);
            if (str_eq(n, "CHORD")) {                   /* m7 held (G3), the root G4 */
                fm1_in.notes |= 1u << CB_M7; keyboard_block(); srender(2);
                skey_sung(14);
                fm1_in.notes &= ~(1u << CB_M7); keyboard_block(); srender(2);
            }
            if (str_eq(n, "DRUMS")) {                   /* a pattern: kick on 1 5 9 13, snare 5 13, hats on 8ths */
                static const uint8_t K[] = {0, 4, 8, 12}, S[] = {4, 12}, H[] = {0, 2, 4, 6, 8, 10, 12, 14};
                for (k = 0; k < 4u; k++) skey((uint32_t)sh_white_key(K[k]));
                skey(3);                                /* (the second black key: SNARE) */
                for (k = 0; k < 2u; k++) skey((uint32_t)sh_white_key(S[k]));
                skey(5);
                skey(8);                                /* (the fourth: HATCL) */
                for (k = 0; k < 8u; k++) skey((uint32_t)sh_white_key(H[k]));
                skey(3);
            }
            if (str_eq(n, "ALIVE")) {
                transport_req = 1;
                srender(1);
                for (k = 0; k < 120u; k++) { srender(4); if (k % 8u == 0u) sframe(); }
            }
            if (str_eq(n, "VODER"))                     /* bands and the T stop held */
                fm1_in.notes |= (1u << 2) | (1u << 3) | (1u << 4);
            sframes(2000);
            ok &= sh.mode == m && screen_ink(20, 196) > 300u;
            snprintf(nm, sizeof nm, "pic_%s", n);
            shot(nm);
            fm1_in.notes = 0;
            keyboard_block();
            sframe();
        }
        bad += check("pictures: every SOUND on the wheel draws its own", ok);
    }

    {   /* the instruments: what they play, not only what they draw */
        uint32_t c = mode_of("CHORD"), r = mode_of("RUDIMENT"), a = mode_of("ALIVE"), g0, p;
        int32_t sounding;
        transport_req = 2; srender(2);
        sgo(c);
        {   /* the Orchid / ChoralRoot layout: F3..C#4 the block (silent), D4..G5 the roots */
            track_t *t = sh_trk();
            uint32_t na, q;
            t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;        /* (C major: the key is the part's, shared since the split) */
            srender(2);
            fm1_in.notes |= 1u << CB_M7; keyboard_block(); srender(2);      /* m7 held, the root G4 */
            skey_sung(14);
            ok = (chord_last[sh.part].mask >> 10) & 1u && (chord_last[sh.part].mask >> 4) & 1u;
            fm1_in.notes &= ~(1u << CB_M7); keyboard_block(); srender(2);
            fm1_in.notes |= 1u << CB_MIN; keyboard_block(); srender(2);     /* MIN held, the root D4 */
            skey_sung(CB_ROOT0);
            ok = ok && (chord_last[sh.part].mask >> 3) & 1u && !((chord_last[sh.part].mask >> 4) & 1u);
            fm1_in.notes &= ~(1u << CB_MIN); keyboard_block(); srender(40);
            for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
            ok = ok && na == 0u && shell_kb_skip == CB_KEYS && shell_chord_part == sh.part + 1u;
            bad += check("CHORD: the block shapes the root's chord (m7: G7 on G4; MIN: Dm), and never sounds", ok);
            sh_sp[SP_BASS - SP_FIRST] = 1; sframe();
            skey_sung(CB_ROOT0 + 5u);                   /* G4 with BASS ROOT */
            ok = chord_last[sh.part].note[0] + 12u == chord_last[sh.part].root;
            sh_sp[SP_BASS - SP_FIRST] = 0; sframe();
            t->p[P_QUANT] = 0; sframe();
            skey_sung(CB_ROOT0 + 1u);                   /* D#4, KEY OFF: major */
            ok = ok && chord_last[sh.part].mask == ((1u << 0) | (1u << 4) | (1u << 7));
            t->p[P_QUANT] = 1; sframe();
            bad += check("CHORD: BASS ROOT adds the root an octave down; KEY OFF plays any root major", ok);
            skey(CB_LATCH);
            skey_sung(CB_ROOT0);                        /* LATCH on: D4's chord keeps sounding after the key */
            for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
            ok = shell_chord_latch && na >= 3u;
            skey(CB_LATCH); srender(40);                /* LATCH off: it ends */
            for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
            bad += check("CHORD: B3 LATCH holds the chord past the key; LATCH off ends it", ok && !shell_chord_latch && na == 0u);
            {   /* B3 held: a root key adds its note (silent), SELECT walks the chord list; a hold used is no LATCH */
                uint32_t e0 = t->eng_req, add, hit, lo = 127, hi = 0;
                fm1_in.notes |= 1u << CB_LATCH; keyboard_block(); sframe(); srender(2);
                skey(19);                               /* C (over D: the minor seventh) */
                for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
                add = shell_chord_add;
                sturn(EN_SELECT, 1);                    /* MAJ */
                fm1_in.notes &= ~(1u << CB_LATCH); keyboard_block(); sframe();
                ok = add == 1u << 0 && na == 0u && shell_chord_qual == 1u && !shell_chord_latch && shell_kb_skip == CB_KEYS;
                skey_sung(CB_ROOT0);                    /* D: MAJ + C, D7 */
                ok = ok && chord_last[sh.part].mask == ((1u << 0) | (1u << 4) | (1u << 7) | (1u << 10));
                bad += check("CHORD: B3 held + a key adds that note, + SELECT picks MAJ: D4 plays D7; no LATCH", ok);
                fm1_in.notes |= 1u << CB_LATCH; keyboard_block(); sframes(SH_LONG_MS + 200u);
                hit = sh.ring == SH_RING_CHORD;
                fm1_in.notes &= ~(1u << CB_LATCH); keyboard_block(); sframe();
                ok = hit && !shell_chord_add && !shell_chord_qual && !shell_chord_latch && !sh.ring;
                skey_sung(CB_ROOT0);
                ok = ok && chord_last[sh.part].mask == ((1u << 0) | (1u << 3) | (1u << 7));   /* Dm again */
                bad += check("CHORD: B3 held long and let go untouched: the ring, then the standard chords", ok);
                {   /* no chord played yet: B3 + B adds a B, and D4's chord plays it (the toast's note), not a C# */
                    chord_last[sh.part].gen = 0;
                    fm1_in.notes |= 1u << CB_LATCH; keyboard_block(); sframe(); srender(2);
                    skey(18);                           /* B4 */
                    ok = str_eq(sh.toast, "ADDED B");
                    fm1_in.notes &= ~(1u << CB_LATCH); keyboard_block(); sframe();
                    skey_sung(CB_ROOT0);                /* Dm + B over D: D F A B */
                    ok = ok && chord_last[sh.part].mask == ((1u << 0) | (1u << 3) | (1u << 7) | (1u << 9));
                    shell_chord_add = 0;
                    sframe();
                    bad += check("CHORD: B3 + a key before any chord adds the note the toast names (B over D)", ok);
                }
                t->p[P_VOIC] = VC_DROP2;
                skey_sung(CB_ROOT0);
                for (q = 0; q < chord_last[sh.part].n; q++) {
                    lo = chord_last[sh.part].note[q] < lo ? chord_last[sh.part].note[q] : lo;
                    hi = chord_last[sh.part].note[q] > hi ? chord_last[sh.part].note[q] : hi;
                }
                ok = chord_last[sh.part].n == 3u && hi - lo > 12u;
                t->p[P_VOIC] = VC_CLOSE;
                bad += check("CHORD: DROP2 spreads the triad past an octave", ok);
                {   /* CHORD PLAY (ARP's first page in CHORD): STRM staggers the starts and a release drops the rest;
                     * TOP lifts the top note; TUNE JUST tunes the third 5:4 (-2/16) */
                    uint8_t ids[4];
                    int glob;
                    uint32_t na1, na2, na3, vtop = 0, vin = 127, vbass = 0;
                    int32_t p3 = 0, p5 = 0;
#define SOUNDING(t_, n_) do { uint32_t q_; (n_) = 0; for (q_ = 0; q_ < NVOICE; q_++) (n_) += (t_)->v[q_].active && (t_)->v[q_].gate; } while (0)
                    shome();                            /* (from the SOUND view: ARP opens the NOTE half's first) */
                    snmain();                           /* (the NOTE half: CHORD's MAIN, then CHORD PLAY) */
                    sh_page_ids(ids, &glob);
                    ok = sh.page[sh.mode] == SHP_NOTE && ids[0] == SP_BASS && ids[1] == P_VOIC;
                    sturn(EN_SELECT, 1);
                    sh_page_ids(ids, &glob);
                    ok = ok && sh.page[sh.mode] == SHP_CPLAY && ids[0] == SP_STRM && ids[1] == SP_TOP && ids[2] == SP_TUNE;
                    t->p[P_QUANT] = 0;                  /* (any root major: D F# A) */
                    t->p[P_VOICE] = V_POLY;             /* (the jump's preset may be a MONO one) */
                    t->p[P_GLIDE] = 0;
                    sh_sp[SP_STRM - SP_FIRST] = 127; sframe();
                    memset(t->v, 0, sizeof t->v);
                    fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
                    SOUNDING(t, na1);
                    fm1_ms += 300u; keyboard_block();
                    SOUNDING(t, na2);
                    fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(40);
                    fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
                    fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block();   /* let go mid-strum */
                    fm1_ms += 300u; keyboard_block();
                    SOUNDING(t, na3);
                    ok = ok && na1 == 1u && na2 == 3u && na3 == 0u;
                    bad += check("CHORD PLAY: STRM starts the notes one by one; let go mid-strum, the rest never start", ok);
                    sh_sp[SP_STRM - SP_FIRST] = 0; sh_sp[SP_TOP - SP_FIRST] = 127; sframe(); srender(40);
                    memset(t->v, 0, sizeof t->v);
                    fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
                    for (q = 0; q < NVOICE; q++)
                        if (t->v[q].active && t->v[q].gate) {
                            if (t->v[q].note == 62u + 7u) vtop = t->v[q].vel;
                            else if (t->v[q].note == 62u) vbass = t->v[q].vel;
                            else vin = t->v[q].vel;
                        }
                    fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(40);
                    ok = vtop > vbass && vin < vbass;
                    sh_sp[SP_TOP - SP_FIRST] = 0; sh_sp[SP_TUNE - SP_FIRST] = 1; sframe();
                    memset(t->v, 0, sizeof t->v);
                    fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
                    for (q = 0; q < NVOICE; q++)
                        if (t->v[q].active && t->v[q].gate) {
                            if (t->v[q].note == 66u) p3 = t->v[q].pitch16;
                            if (t->v[q].note == 69u) p5 = t->v[q].pitch16;
                        }
                    fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(40);
                    ok = ok && p3 == 66 * 16 - 2 && p5 == 69 * 16;
                    sh_sp[SP_TUNE - SP_FIRST] = 0; t->p[P_QUANT] = 1; sframe();
                    bad += check("CHORD PLAY: TOP lifts the top note over the bass, the inner below; JUST: the third 5:4", ok);
#undef SOUNDING
                }
            }
        }
        sgo(r);
        bad += check("RUDIMENT: on its own part (4); white key 1 holds KICK at step 1, the snare lane its own hits",
                     sh.part == 3u && ((step_lanes(&trk[3].step[0]) >> 0) & 1u) &&
                     ((step_lanes(&trk[3].step[4]) >> 1) & 1u) && !((step_lanes(&trk[3].step[1]) >> 1) & 1u) &&
                     shell_kb_skip == SH_ALL_KEYS);
        sgo(a);
        stap(B_PLAY); srender(1);                       /* (PLAY in front: used, so ALIVE stays on LINE) */
        g0 = al.gen;
        for (p = 0; p < 200u; p++) srender(2);
        ok = al.gen > g0 && sh.part == 2u;
        sgo(0);   /* away: LINE is a track, it plays on */
        g0 = al.gen;
        for (p = 0; p < 200u; p++) srender(2);
        bad += check("ALIVE: grows on the clock (its part 3, LINE); left for another pair, it keeps growing",
                     ok && al.gen > g0 && !trk[2].p[P_MUTE] && sh.part != 2u);
        sh_wnoteslot[2] = (uint8_t)note_of("ALIVE");    /* (ALIVE on F#4 again, LINES' key, for this) */
        sdown(B_HOME); sframes(400);                    /* the wheel: LIZARD (white), ALIVE (black), PLAY: mute */
        skey((uint32_t)sh_slot_key(sh_mode_pos(tm_slot(a)))); skey((uint32_t)sh_note_key(sh_note_pos(tm_note(a))));
        sdown(B_PLAY); sframe(); sup(B_PLAY); sframe();
        sup(B_HOME); sframe();
        srender(40);
        sounding = 0;
        for (p = 0; p < NVOICE; p++) sounding += trk[2].v[p].gate != 0;
        g0 = al.gen;
        for (p = 0; p < 100u; p++) srender(2);
        ok = (sh_tmute >> 2) & 1u && trk[2].p[P_MUTE] && !al.note && !sounding && al.gen == g0;
        sgo(c);   /* CHORD in front */
        sdown(B_HOME); sframes(400);                    /* PLAY on it again: back */
        skey((uint32_t)sh_slot_key(sh_mode_pos(tm_slot(a)))); skey((uint32_t)sh_note_key(sh_note_pos(tm_note(a))));
        sdown(B_PLAY); sframe(); sup(B_PLAY); sframe();
        sup(B_HOME); sframe();                          /* (letting go of HOME brings that pair to the front) */
        sgo(c);
        ok = ok && !sh_tmute && !trk[2].p[P_MUTE] && sh_is(c);
        g0 = al.gen;
        for (p = 0; p < 200u; p++) srender(2);
        bad += check("tracks: HOME + PLAY on ALIVE's slot mutes LINE (silent, it stops growing), again brings it back "
                     "under CHORD", ok && al.gen > g0 && !trk[2].p[P_MUTE]);
        sh_wnoteslot[2] = SH_N_LINES;
        transport_req = 2; srender(2);
        bad += check("  stop: ALIVE's note ended", !al.note);
    }

    {   /* ALIVE's clock is its part's sequencer: a row a step, whatever the swing */
        uint32_t a = mode_of("ALIVE"), k, steps = 0, g0, last;
        sgo(a);
        song.g[G_SWING] = 60;
        transport_req = 1; srender(1);
        g0 = al.gen; last = trk[2].seq_idx;
        for (k = 0; k < 400u; k++) {
            srender(1);
            if (trk[2].seq_idx != last) { steps++; last = trk[2].seq_idx; }
        }
        transport_req = 2; srender(2);
        song.g[G_SWING] = GP[G_SWING].def;
        bad += check("ALIVE: a row on each of its part's sequencer steps (swing on: in step with the sequencer), from "
                     "its seed again at the top of each LEN",   /* (al_seed counts a generation too) */
                     steps > 8u && al.gen - g0 >= steps && al.gen - g0 <= steps + 2u + steps / 16u);
    }

    {   /* the voice budget (voice.c voice_victim with the shell's roles) on voices set by hand */
        track_t *pp = 0;
        uint32_t p, i, v;
        for (p = 0; p < NPART; p++) { memset(trk[p].v, 0, sizeof trk[p].v); shell_role[p] = SR_NONE; }
        for (i = 0; i < 2u; i++) {                      /* part 4 behind: two drum hits sounding (its reserve) */
            trk[3].v[i].active = 1; trk[3].v[i].gate = 0; trk[3].v[i].stage = 3; trk[3].v[i].age = 1u + i;
        }
        for (i = 0; i < 6u; i++) {                      /* part 1 in front: six held notes */
            trk[0].v[i].active = 1; trk[0].v[i].gate = 1; trk[0].v[i].stage = 2; trk[0].v[i].age = 10u + i;
            trk[0].v[i].note = (uint8_t)(60 + i);
        }
        shell_role[0] = SR_FG; shell_role[3] = SR_BG; shell_reserve[3] = 2;
        v = voice_victim(&trk[0], 0, &pp);
        ok = pp != &trk[3];                              /* (the stock rules would take a released drum first) */
        trk[1].v[0].active = 1; trk[1].v[0].gate = 1; trk[1].v[0].stage = 2; trk[1].v[0].age = 99;
        shell_role[1] = SR_REL;
        pp = 0;
        v = voice_victim(&trk[0], 0, &pp);
        ok &= pp == &trk[1] && v == 0u;                  /* a part left: taken first, held or not */
        shell_role[3] = SR_NONE; shell_role[1] = SR_NONE;
        pp = 0;
        v = voice_victim(&trk[0], 0, &pp);
        bad += check("voice budget: the mode behind keeps its reserve, a part left is stolen first, else stock",
                     ok && pp == &trk[3]);
        for (p = 0; p < NPART; p++) { memset(trk[p].v, 0, sizeof trk[p].v); shell_role[p] = SR_NONE; }
    }

#if FELUCCA_SYLLABLE
    {   /* ALIVE into any SOUND (the split): over SYLLABLE it babbles, on its own part */
        uint32_t k, c, vw, e, ph, pos, sy[8], ns = 0, i, diff = 0;
        sgoc(slot_of("SYLLABLE"), note_of("ALIVE"));
        ok = trk[2].eng_req == ENGI_SYLLABLE && sh.part == 2u;
        sh_sp[SP_SEED - SP_FIRST] = 40;                  /* (a scatter: the centre lives often) */
        transport_req = 1; srender(1);
        for (k = 0; k < 600u && ns < 8u; k++) {
            srender(2);
            if (syl_now(&trk[2], &c, &vw, &e, &ph, &pos)) {
                uint32_t code = c * 32u + vw;
                if (!ns || sy[ns - 1u] != code) sy[ns++] = code;
            }
        }
        for (i = 1; i < ns; i++) diff += sy[i] != sy[0];
        transport_req = 2; srender(2);
        bad += check("ALIVE over SYLLABLE (any SOUND): the cells pick the syllables",
                     ok && ns >= 3u && diff >= 2u);
        sh_sp[SP_SEED - SP_FIRST] = 0;
    }
#endif

    {   /* SAVE: a tap asks (OCT+ yes, OCT- no), the sound goes to a user slot, PRESETS walks to it after the factory */
        uint32_t m = mode_of("FM6"), k, nf, n0;
        int16_t keep;
        int8_t oct;
        track_t *t;
        sgo(m);
        shome();
        sframes(SH_TOAST_MS + 100u);
        t = sh_trk();
        for (k = 0; k < UP_SLOTS; k++) up_put(k, 0);
        n0 = up_count();
        oct = song.octave;
        stap(B_SAVE);
        ok = sh.save && sh.save_slot == 0u;
        stap(B_OCTDN);
        ok = ok && !sh.save && up_count() == n0 && song.octave == oct;
        bad += check("SAVE: a tap asks for U01; OCT- says no (nothing written, the octave kept)", ok);
        t->p[P_E0] = keep = (int16_t)(t->p[P_E0] == 3 ? 4 : 3);
        stap(B_SAVE);
        shot("save_ask");
        stap(B_OCTUP);
        ok = !sh.save && up_used(0) && t->user == 1u && song.octave == oct && up_engine(0) == ENGI_FM6;
        bad += check("SAVE: OCT+ writes the part's sound to U01 (the part now plays U01)", ok);
        nf = ENGINES[ENGI_FM6]->npresets;
        sturn(EN_PRESET, 1);
        ok = t->user == 0u && t->preset == 0u;
        t->p[P_E0] = (int16_t)(keep + 1);
        sturn(EN_PRESET, -1);
        ok = ok && t->user == 1u && t->p[P_E0] == keep;
        sframe();
        shot("save_user_preset");
        bad += check("PRESETS: after the factory sounds, round, the user's own (U01 loads as saved)", ok && nf > 0u);
        stap(B_SAVE);
        ok = sh.save && sh.save_slot == 0u;
        sh_save_answer(0);
        transport_req = 1; srender(2); sframe();
        stap(B_SAVE);
        ok = ok && !sh.save;
        transport_req = 2; srender(2); sframe();
        bad += check("SAVE: a U sound saves over its own slot; not while playing (STOP TO SAVE)", ok);
        for (k = 0; k < UP_SLOTS; k++) up_put(k, 0);
        apply_preset_to(t, 0);
    }

    {   /* LIZARD's DRAW pages: EDIT walks OSC TONE DRAW 1-4 DRAW 5-8 in MODE DRAW; the knobs edit the drawing it plays */
        uint32_t m = mode_of("LIZARD"), k, same = 1;
        uint64_t walk = 0;
        track_t *t;
        lz_osc o;
        float before;
        sgo(m);
        t = sh_trk();
        t->p[P_E0] = LZM_DRAW;
        t->p[P_E2] = 3 * 8;                             /* STAIR */
        t->p[P_LZUSER] = 0;
        shome();
        for (k = 0; k < 9u; k++) { stap(B_EDIT); walk = walk * 100u + sh.page[sh.mode]; }
        ok = walk == (((((((SHP_EDIT1 * 100ull + SHP_EDIT2) * 100u + SHP_SHAPE) * 100u + SHP_DRAW1) * 100u + SHP_DRAW2) * 100u +
                      SHP_VOICE) * 100u + SHP_GLIDE) * 100u + SHP_HOME) * 100u + SHP_EDIT1;
        bad += check("LIZARD DRAW: EDIT walks OSC, TONE, SHAPE, DRAW 1-4, DRAW 5-8, VOICE, GLIDE, MAIN, round", ok);
        stap(B_EDIT); stap(B_EDIT); stap(B_EDIT);       /* DRAW 1-4 */
        for (k = 0; k < 8u; k++)
            same &= t->p[P_LZD0 + k] == (LZ_DRAW[3].lv[k] < 0 ? LZ_DRAW[3].lv[k] * 64 / 100 : LZ_DRAW[3].lv[k] * 63 / 100);
        ok = sh.page[sh.mode] == SHP_DRAW1 && same && !lizard_own(t->p);
        memset(&o, 0, sizeof o);
        lz_setup(&o, t->p);
        before = o.steps[1].level;
        sturn(EN_K2, 6);
        shot("lizard_draw");
        lz_setup(&o, t->p);
        ok = ok && lizard_own(t->p) && t->p[P_LZD1] > (int16_t)(LZ_DRAW[3].lv[1] * 63 / 100) &&
             o.steps[1].level > before && lizard_desc(t, 2u) == &LZ_WAVE_OWN;
        bad += check("LIZARD DRAW: the knobs start from the drawing WAVE plays; a turn makes it the part's own", ok);
        for (k = 0; k < UP_SLOTS; k++) up_put(k, 0);
        stap(B_SAVE); stap(B_OCTUP);
        v = t->p[P_LZD1];
        t->p[P_LZD1] = (int16_t)(v - 20);
        sturn(EN_PRESET, 1); sturn(EN_PRESET, -1);
        ok = t->user == 1u && lizard_own(t->p) && t->p[P_LZD1] == v;
        bad += check("LIZARD DRAW: a saved sound keeps its drawing (U01 loads it back)", ok);
        shome();
        t->p[P_E2] = 5 * 8;                             /* WAVE turned: the factory drawings again */
        ok = !lizard_own(t->p);
        t->p[P_E0] = LZM_PSAW;
        sh.page[sh.mode] = SHP_DRAW2;
        sframe();
        ok = ok && sh.page[sh.mode] == SHP_EDIT1;
        for (k = 0; k < 6u; k++) stap(B_EDIT);
        ok = ok && sh.page[sh.mode] == SHP_EDIT1;       /* (OSC TONE SHAPE VOICE GLIDE MAIN, round: no DRAW pages) */
        bad += check("LIZARD DRAW: WAVE turned plays the factory ones; out of DRAW the DRAW pages are gone", ok);
        for (k = 0; k < UP_SLOTS; k++) up_put(k, 0);
        apply_preset_to(t, 0);
        sframes(400);                                   /* (no HOME double tap with the next block's) */
    }

    {   /* SHAPE: every engine's third EDIT page (FOLD SYM CRUSH DOWN; LIZARD's UNI WARP FOLD SYM), lzshape on the voice */
        uint32_t m = mode_of("FM6"), l = mode_of("LIZARD"), k, vi;
        uint64_t walk = 0;
        uint8_t ids[4];
        int glob;
        track_t *t;
        static int32_t buf[2 * HALF_FRAMES];
        int32_t pk = 0;
        sgo(m);
        shome();
        for (k = 0; k < 10u; k++) { stap(B_EDIT); walk = walk * 100u + sh.page[sh.mode]; }
        sh_page_ids(ids, &glob);
        ok = walk == ((((((((SHP_EDIT1 * 100ull + SHP_EDIT2) * 100u + SHP_FMOP) * 100u + SHP_FMOP2) * 100u + SHP_FMENV) * 100u +
                       SHP_SHAPE) * 100u + SHP_VOICE) * 100u + SHP_GLIDE) * 100u + SHP_HOME) * 100u + SHP_EDIT1;
        for (k = 0; k < 5u; k++) stap(B_EDIT);          /* (to SHAPE) */
        sh_page_ids(ids, &glob);
        ok = ok && sh.page[sh.mode] == SHP_SHAPE && ids[0] == P_FOLD && ids[3] == P_DOWN;
        sframes(400);                                   /* (ANALOG: it sounds at once, no arp, no patch to load) */
        sgoc(slot_of("ANALOG"), sh.note);
        shome();
        sh.page[sh.mode] = SHP_SHAPE;
        t = sh_trk();
        ok = ok && t->eng_req == 0u;
        apply_preset_to(t, 0);
        srender(2);                                     /* (the switch's panic, first: it would end the note) */
        sturn(EN_K1, 40);
        input_on(t, 60, 100); srender(4);
        for (vi = 0; vi < NVOICE && !t->v[vi].active; vi++)
            ;
        memset(buf, 0, sizeof buf);
        track_render(t, buf, HALF_FRAMES);
        for (k = 0; k < HALF_FRAMES; k++) pk = buf[k] > pk ? buf[k] : -buf[k] > pk ? -buf[k] : pk;
        ok = ok && t->p[P_FOLD] > 0 && vi < NVOICE && shape_age[(uint32_t)(t - trk) % NPART][vi] == t->v[vi].age && pk > 0;
        input_off(t, 60); srender(40);
        bad += check("SHAPE: FM6 walks OSC TONE SHAPE; FOLD up, ANALOG's voices go through lzshape, sounding", ok);
        t->p[P_FOLD] = 0;
        sframes(400);
        sgo(l);
        shome();
        srender(2);                                     /* (the switch's panic first: LIZARD may take FM6's own part) */
        t = sh_trk();
        t->p[P_E0] = LZM_DRAW;
        sh.page[sh.mode] = SHP_SHAPE;
        sh_page_ids(ids, &glob);
        ok = ids[0] == P_LZUNI && ids[1] == P_LZWARP && ids[2] == P_FOLD && ids[3] == P_SYM;
        t->p[P_LZUNI] = 5; t->p[P_LZWARP] = 60; t->p[P_FOLD] = 80;
        shot("lizard_shape");
        pk = 0;
        input_on(t, 60, 100); srender(2);
        memset(buf, 0, sizeof buf);
        track_render(t, buf, HALF_FRAMES);
        for (k = 0; k < HALF_FRAMES; k++) pk = buf[k] > pk ? buf[k] : -buf[k] > pk ? -buf[k] : pk;
        input_off(t, 60); srender(40);
        bad += check("SHAPE: LIZARD's page is UNI WARP FOLD SYM; UNI 5 + WARP + FOLD in DRAW sounds, in range",
                     ok && pk > 0 && pk < 4 * 32768);
        apply_preset_to(t, 0);
        sframes(400);
    }

    {   /* ARP: its page (MODE RATE OCT GATE) in every mode; ARP held toggles HOLD */
        uint32_t m = mode_of("FM6");
        uint8_t ids[4];
        int glob, hold;
        track_t *t;
        sgo(m);
        t = sh_trk();
        stap(B_ARP);
        sh_page_ids(ids, &glob);
        ok = sh.page[sh.mode] == SHP_ARP && ids[0] == P_AMODE && ids[1] == P_ARATE && ids[2] == P_AOCT && ids[3] == P_AGATE;
        sturn(EN_K1, 1);
        ok = ok && t->p[P_AMODE] == 1;
        hold = t->p[P_AHOLD];
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();
        ok = ok && t->p[P_AHOLD] == !hold && sh.page[sh.mode] == SHP_ARP;
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();
        bad += check("ARP: its page MODE RATE OCT GATE (MODE turned: UP); ARP held toggles HOLD", ok && t->p[P_AHOLD] == hold);
        t->p[P_AMODE] = 0;
        shome();
        sframes(400);
    }

    {   /* SEQ: its page (LEN DIV SWING GATE); held, the white keys are the 16 steps, a step takes the last note */
        uint32_t m = mode_of("FM6"), q, na;
        uint8_t ids[4];
        int glob;
        track_t *t;
        sgo(m);
        t = sh_trk();
        srender(2);
        for (q = 0; q < 16u; q++) step_clear(&t->step[q]);
        skey_sung(12);                                  /* a note played: the steps take it (on the SEQ page the keys */
        stap(B_SEQ);                                    /* are steps) */
        sh_page_ids(ids, &glob);
        ok = sh.page[sh.mode] == SHP_SEQ && ids[0] == P_SLEN && ids[1] == P_SDIV && ids[2] == P_SSWING && ids[3] == P_SGATE;
        sdown(B_SEQ); sframes(400);
        skey((uint32_t)sh_white_key(0)); skey((uint32_t)sh_white_key(4)); skey((uint32_t)sh_white_key(9));
        skey((uint32_t)sh_white_key(9));                /* (on, then off again) */
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        ok = ok && t->step[0].n && t->step[4].n && !t->step[9].n && t->step[0].note[0] == kb_note[12] && na == 0u &&
             sh_step_led((uint32_t)sh_white_key(4)) == SHL_LIT && sh_step_led((uint32_t)sh_white_key(1)) == SHL_OFF;
        sup(B_SEQ); sframe();
        shot("seq");
        bad += check("SEQ: its page LEN DIV SWING GATE; held, white keys set steps to the last note (silent), lit", ok &&
                     sh.page[sh.mode] == SHP_SEQ);
        for (q = 0; q < 16u; q++) step_clear(&t->step[q]);
        shome();
        sframes(400);
    }

    {   /* a beat programmed, then ALGORITHM knocked through two rhythms: REC held through its first ring gets the beat
         * back (its lanes and RHYTHM too); an end of the list turned past loads nothing */
        uint32_t r = mode_of("RUDIMENT"), q, lanes = 0;
        int16_t eu[3][8];
        step_t keep[NSTEP];
        int rec, label;
        track_t *t;
        sgo(r);
        shome();
        t = sh_trk();
        sh_sp[SP_RHY - SP_FIRST] = 1;
        snote();
        sturn(EN_ALGO, -1);                             /* EMPTY */
        grid_hit(t, 0, 0, 1u); grid_hit(t, 6, 0, 1u); grid_hit(t, 4, 1, 1u); grid_hit(t, 2, 2, 1u);
        sh_eu[0][2] = 12; shell_lane_len[2] = 12;       /* the hats' lane 12 long */
        memcpy(keep, t->step, sizeof keep);
        memcpy(eu, sh_eu, sizeof eu);
        sturn(EN_ALGO, -1);                             /* EMPTY again, all the way left: nothing loads */
        ok = !memcmp(keep, t->step, sizeof keep) && sh_sp[SP_RHY - SP_FIRST] == 0;
        sturn(EN_ALGO, 1); sturn(EN_ALGO, 1);           /* IO, EUROPA: the beat replaced twice */
        ok = ok && memcmp(keep, t->step, sizeof keep) && sh_sp[SP_RHY - SP_FIRST] == 2;
        rec = song.rec & 1u;
        sdown(B_REC); sframes(SH_LONG_MS + 100u);
        label = sh.ring == B_REC + 1u && sh.ring2 && sh_take.have && sh_take.rhy;
        sup(B_REC); sframe();
        for (q = 0; q < 16u; q++) lanes |= step_lanes(&t->step[q]);
        ok = ok && label && !memcmp(keep, t->step, sizeof keep) && !memcmp(eu, sh_eu, sizeof eu) &&
             shell_lane_len[2] == 12u && sh_sp[SP_RHY - SP_FIRST] == 0 && t->p[P_SLEN] == 16 && lanes == 7u &&
             !memcmp(sh.toast, "BEAT UNDONE", 11) && (song.rec & 1u) == (uint32_t)rec && !sh_take.have;
        bad += check("RUDIMENT: a beat replaced by ALGORITHM's rhythms comes back with REC held (BEAT UNDONE)", ok);
        sh_sp[SP_RHY - SP_FIRST] = 1;
        sturn(EN_ALGO, -1);                             /* (EMPTY, the start, as the tests below expect) */
        shome();
        sframes(400);
    }

    {   /* REC held through both rings (the first offers UNDO TAKE): RUDIMENT clears the whole beat, every lane, the
         * lane in front or not; UNDO BEAT brings it back */
        uint32_t r = mode_of("RUDIMENT"), q, lanes = 0;
        int rec;
        track_t *t;
        sgo(r);
        shome();
        t = sh_trk();
        for (q = 0; q < 16u; q++) step_clear(&t->step[q]);
        grid_hit(t, 0, 0, 1u); grid_hit(t, 4, 0, 1u); grid_hit(t, 4, 1, 1u);   /* kick 1 5, snare 5 */
        sh_sp[SP_LANE - SP_FIRST] = 2;                  /* (an empty lane in front) */
        rec = song.rec & 1u;
        sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();
        for (q = 0; q < 16u; q++) lanes |= step_lanes(&t->step[q]);
        ok = !lanes && (song.rec & 1u) == (uint32_t)rec && str_eq(sh.toast, "CLEARED ALL LANES");
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* the first ring: UNDO BEAT */
        for (lanes = 0, q = 0; q < 16u; q++) lanes |= step_lanes(&t->step[q]);
        ok = ok && lanes == 3u;
        sh_sp[SP_LANE - SP_FIRST] = 0;
        sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();
        for (lanes = 0, q = 0; q < 16u; q++) lanes |= step_lanes(&t->step[q]);
        bad += check("RUDIMENT: REC held through both rings clears every lane (UNDO BEAT brings them back)", ok && !lanes);
        {   /* the lane in front empty while others play: their hits glow on the white keys, the lane keys with hits
             * mid, the empty ones low */
            uint32_t k, w0 = 99, w4 = 99, w1 = 99, b0 = 99, b1 = 99, b2 = 99;
            transport_req = 2; srender(4); sframe();
            grid_hit(t, 0, 0, 1u); grid_hit(t, 4, 1, 1u);
            sh_sp[SP_LANE - SP_FIRST] = 2;
            for (k = 0; k < 27u; k++) {
                if (sh_key_white(k) == 0) w0 = k;
                if (sh_key_white(k) == 1) w1 = k;
                if (sh_key_white(k) == 4) w4 = k;
                if (sh_key_black(k) == 0) b0 = k;
                if (sh_key_black(k) == 1) b1 = k;
                if (sh_key_black(k) == 2) b2 = k;
            }
            ok = sh_inst_led(w0, 0) == SHL_LOW && sh_inst_led(w4, 0) == SHL_LOW && sh_inst_led(w1, 0) == SHL_OFF &&
                 sh_inst_led(b0, 0) == SHL_MID && sh_inst_led(b1, 0) == SHL_MID && sh_inst_led(b2, 0) == SHL_LIT;
            sh_sp[SP_LANE - SP_FIRST] = 0;              /* (a lane with hits in front: its own steps, as before) */
            ok = ok && sh_inst_led(w0, 0) == SHL_LIT && sh_inst_led(w4, 0) == SHL_LOW && sh_inst_led(b2, 0) == SHL_LOW;
            for (k = 0; k < 16u; k++) step_clear(&t->step[k]);
            bad += check("RUDIMENT: the lane in front empty, the others' hits glow; the lane keys with hits mid", ok);
        }
        {   /* REC as ERASE (the MPC's): + a step key, its step; + a lane key stopped, the lane; playing, as it passes */
            uint32_t len, n0, n1, kick = 0, snare = 0;
            for (q = 0; q < 16u; q++) { grid_hit(t, q, 0, 1u); if (!(q % 4u)) grid_hit(t, q, 1, 1u); }
            rec = song.rec & 1u;
            sdown(B_REC); sframe();
            skey((uint32_t)sh_white_key(4));
            ok = !step_lanes(&t->step[4]) && step_lanes(&t->step[0]) == 3u;
            skey(SH_BLACK_KEYS[1]);                     /* stopped: the snare lane */
            for (q = 0; q < 16u; q++) snare |= (step_lanes(&t->step[q]) >> 1) & 1u;
            sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
            for (q = 0; q < 16u; q++) kick += step_lanes(&t->step[q]) & 1u;
            ok = ok && !snare && kick == 15u && (song.rec & 1u) == (uint32_t)rec && !sh.ring;
            bad += check("RUDIMENT: REC + a step key erases the step, + a lane key (stopped) the lane; no arm, no clear", ok);
            len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u;
            transport_req = 1; srender(2);
            sdown(B_REC); sframe();
            fm1_in.notes |= 1u << SH_BLACK_KEYS[0]; host_notes |= 1u << SH_BLACK_KEYS[0]; sframe();
            for (n0 = 0, q = 0; q < 16u; q++) n0 += step_lanes(&t->step[q]) & 1u;
            for (q = 0; q < 4000u; q++) { srender(1); if (!(q % 8u)) sframe(); }   /* (a few bars, frames between) */
            fm1_in.notes &= ~(1u << SH_BLACK_KEYS[0]); sframe();
            sup(B_REC); sframe();
            transport_req = 2; srender(2);
            for (n1 = 0, q = 0; q < 16u; q++) n1 += step_lanes(&t->step[q]) & 1u;
            bad += check("RUDIMENT: playing, REC + a held lane key erases its hits as the playhead passes", n0 > 0u && n1 == 0u &&
                         (song.rec & 1u) == (uint32_t)rec && len == 16u);
        }
        shome();
        sframes(400);
    }

    {   /* REC MODE: REC + SELECT walks ADD, ADD + KNOBS, REPLACE, REPLACE + KNOBS (no wrap); an armed REC records
         * knob moves only with + KNOBS; the second ring clears the notes and the part's knob moves together (its
         * locks stay), and UNDO TAKE brings both back */
        uint32_t m = mode_of("FM6"), n, q;
        int ok2, sel;
        track_t *t;
        sgo(m);
        shome();
        t = sh_trk();
        motion_clear(t);
        sdown(B_REC); sturn(EN_SELECT, -1); sturn(EN_SELECT, -1); sturn(EN_SELECT, -1); sup(B_REC); sframe();
        ok = !motion_knobs && !rec_replace && !sh.ring && str_eq(sh.toast, "REC: ADD");
        song.rec |= 1u << trk_index(t);
        transport_req = 1; srender(2);
        sel = t == TSEL && rec_on(t);
        (void)motion_capture(t, P_DIST, 40);
        ok = ok && sel && motion_count(t) == 0u;        /* ADD: a turn records nothing */
        sdown(B_REC); sturn(EN_SELECT, 1); sup(B_REC); sframe();
        ok = ok && motion_knobs && !rec_replace && str_eq(sh.toast, "REC: ADD + KNOBS") && rec_on(t);   /* (the arm kept) */
        (void)motion_capture(t, P_DIST, 41);
        ok = ok && motion_count(t) == 1u;                /* + KNOBS: it records */
        sdown(B_REC); sturn(EN_SELECT, 1); sup(B_REC); sframe();
        ok = ok && !motion_knobs && rec_replace && str_eq(sh.toast, "REC: REPLACE");
        sdown(B_REC); sturn(EN_SELECT, 1); sturn(EN_SELECT, 1); sup(B_REC); sframe();
        ok = ok && motion_knobs && rec_replace && str_eq(sh.toast, "REC: REPLACE + KNOBS");   /* (the end: no wrap) */
        sdown(B_REC); sturn(EN_SELECT, -1); sturn(EN_SELECT, -1); sup(B_REC); sframe();
        ok = ok && motion_knobs && !rec_replace;
        transport_req = 2; srender(2);
        song.rec &= ~(1u << trk_index(t));
        stap(B_REC); sframe();
        ok = ok && str_eq(sh.toast, "REC ARMED: ADD + KNOBS");
        stap(B_REC); sframe();
        bad += check("REC MODE: REC + SELECT walks ADD, ADD + KNOBS, REPLACE, REPLACE + KNOBS; knob moves only with "
                     "+ KNOBS; the arm says the mode", ok);
        sframes(SH_ST_IDLE_MS / 16u + 50u);
        bad += check("REC MODE kept in the settings (SH_ST[12] 2 for ADD + KNOBS, as NOTES AND KNOBS was; 0 reads as "
                     "ADD)", SH_ST[SH_ST_KNOBS] == 2u);
        motion_clear(t);
        for (q = 0; q < 16u; q++) step_clear(&t->step[q]);
        t->step[2].note[0] = 60u; t->step[2].time = ST_NOTE; t->step[2].n = 1;
        (void)motion_set_event(t, 3, P_DIST, 50);
        (void)motion_set_event(t, 7, P_REV, 20);
        (void)motion_set_lock(t, 5, P_DIST, 90);
        sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u);
        n = motion_count(t);
        ok2 = !sh.ring && n == 1u && motion_lock_count(t) == 1u && !t->step[2].n && str_eq(sh.toast, "PATTERN CLEARED");
        sup(B_REC); sframe();
        bad += check("REC held through the second ring clears the notes and the knob moves together, the locks kept; "
                     "no third ring", ok2 && motion_count(t) == 1u);
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        bad += check("UNDO TAKE after the clear brings the notes and the knob moves back", motion_count(t) == 3u &&
                     t->step[2].n == 1u && str_eq(sh.toast, "TAKE UNDONE"));
        motion_clear(t);
        step_clear(&t->step[2]);
        {   /* REC held past the first ring, SELECT turned, held on past the second: the turn froze the rings */
            uint32_t notes0 = 0, notes1 = 0, rec0 = song.rec;
            step_t *st = &t->step[0];
            for (q = 0; q < 16u; q++) step_clear(&t->step[q]);
            st->note[0] = 60u; st->time = ST_NOTE; st->n = 1;
            for (q = 0; q < 16u; q++) notes0 += t->step[q].n;
            sdown(B_REC); sframes(SH_LONG_MS + 100u);
            sturn(EN_SELECT, 1);
            sframes(2u * SH_LONG_MS + 100u);
            sup(B_REC); sframe();
            for (q = 0; q < 16u; q++) notes1 += t->step[q].n;
            bad += check("REC + SELECT mid-ring: the turn freezes the rings (no clear), the mode moves, the arm stays",
                         notes0 == 1u && notes1 == 1u && sh_rec_mode() == 2u && !sh.ring && song.rec == rec0);
            step_clear(st);
        }
        sdown(B_REC); sturn(EN_SELECT, -1); sturn(EN_SELECT, -1); sturn(EN_SELECT, -1); sup(B_REC); sframe();
        bad += check("the default wheel: G4 one voice, A4 ANALOG, B4 PHASE, C5 a spare",
                     str_eq(SH_MODES[SH_A4].name, "ANALOG") && str_eq(SH_MODES[SH_B4].name, "PHASE"));
        shome();
        sframes(400);
    }

    {   /* RUDIMENT's rhythms on ALGORITHM: by meter, each lane its own Euclidean cycle, over their common length */
        uint32_t r = mode_of("RUDIMENT"), q, l, i, kick = 0, fit = 1;
        track_t *t;
        sgo(r);
        shome();
        t = sh_trk();
        ok = sh_sp[SP_RHY - SP_FIRST] == 0 && SP_DESC[SP_RHY - SP_FIRST].def == 0;   /* (EMPTY to start, as the grid) */
        sh_sp[SP_RHY - SP_FIRST] = 2;
        snote();
        sturn(EN_ALGO, -1);                             /* IO: 4/4, the kick on 1 5 9 13 */
        for (q = 0; q < 16u; q++) kick |= (step_lanes(&t->step[q]) & 1u) << q;
        ok = ok && sh_sp[SP_RHY - SP_FIRST] == 1 && t->p[P_SLEN] == 16 && kick == 0x1111u && sh.pop == 2u;
        shot("rudiment_rhythm");
        sturn(EN_ALGO, 1);                              /* EUROPA: the hats' lane 12 long against the kick's 16 */
        ok = ok && sh_lane_len(3) == 12u && shell_lane_len[3] == 12u && sh_lane_len(0) == 16u;
        for (i = 0; i < SH_NRHY; i++) {                 /* every rhythm: each lane fits a lane's 32 */
            for (l = 0; l < 8u; l++)
                fit &= SH_RHY[i].lane[l][0] <= 32u && SH_RHY[i].lane[l][1] <= SH_RHY[i].lane[l][0];
            fit &= str_eq(N_SP_RHY[i], SH_RHY[i].name);
        }
        bad += check("RUDIMENT: ALGORITHM picks a rhythm (IO: kick 1 5 9 13; EUROPA's hats 12 against 16), lanes fit", ok && fit);
        {   /* EMPTY, all the way left: a blank grid, every lane LEN 16 with no hits */
            uint32_t any = 0;
            snote();
            sturn(EN_ALGO, -40);
            for (q = 0; q < NSTEP; q++) any |= step_lanes(&t->step[q]);
            for (l = 0, i = 1; l < 8u; l++) i &= sh_lane_len(l) == 16u && sh_eu[1][l] == 0;
            bad += check("RUDIMENT: ALGORITHM all the way left is EMPTY: no hit on any lane, each LEN 16 FILL 0",
                         sh_sp[SP_RHY - SP_FIRST] == 0 && !any && i && t->p[P_SLEN] == 16);
            sturn(EN_ALGO, 2);                          /* (EUROPA again, as the tests below expect) */
        }
        {   /* HOME: the SOUND's first knob, then the lane in front's LEN FILL ROT; played, each lane wraps at its own
             * LEN (they phase). SEQ holds the loop (LOOP), the NOTE page RHY LANE NOTE OCT */
            uint8_t ids[4];
            int16_t *vp_l;
            int glob;
            uint32_t mk = 0, mh = 0, b;
            snote();
            sturn(EN_ALGO, -1);                         /* IO */
            stap(B_SEQ);
            sh_page_ids(ids, &glob);
            ok = ids[0] == SP_LOOP && ids[1] == P_SDIV && ids[2] == P_SSWING && ids[3] == P_SGATE &&
                 str_eq(sh_desc(SP_LOOP, 0, &vp_l)->label, "LEN") && vp_l == &t->p[P_SLEN];
            snmain(); sturn(EN_SELECT, 1);              /* NOTE MAIN, then NOTES: RHY LANE NOTE OCT */
            sh_page_ids(ids, &glob);
            ok = ok && sh.page[sh.mode] == SHP_NOTE2 && ids[0] == SP_RHY && ids[1] == SP_LANE && ids[2] == SP_LNOTE &&
                 ids[3] == SP_LOCT;
            sframes(600);                               /* (not HOME twice quickly: the pair before) */
            sh.page[sh.mode] = SHP_NOTE; sframe();      /* NOTE MAIN */
            skey(SH_BLACK_KEYS[3]);                     /* the hats' lane in front */
            sh_page_ids(ids, &glob);
            ok = ok && ids[0] == SP_LANE && ids[1] == SP_ELEN && ids[2] == SP_EFILL &&
                 ids[3] == SP_EROT && sh_sp[SP_LANE - SP_FIRST] == 3;
            ok = ok && sh_eu[0][3] == 0 && str_eq(sh_desc(SP_ELEN, 0, &vp_l)->names[0], "=LEN");
            sturn(EN_K2, -1); sturn(EN_K2, -1);         /* LEN =LEN (16) -> 14 (a detent at a time: no acceleration) */
            sturn(EN_K3, -1);                           /* FILL 8 -> 7 */
            ok = ok && sh_lane_len(3) == 14u && shell_lane_len[3] == 14u && sh_eu[1][3] == 7;
            for (q = 0, b = 0; q < 14u; q++) b += (step_lanes(&t->step[q]) >> 3) & 1u;
            ok = ok && b == 7u && !((step_lanes(&t->step[14]) >> 3) & 1u);
            shot("rudiment_euclid");
            transport_req = 1; srender(2);
            for (q = 0; q < 3000u; q++) {
                srender(1);
                if (shell_lane_pos[0] > mk) mk = shell_lane_pos[0];
                if (shell_lane_pos[3] > mh) mh = shell_lane_pos[3];
            }
            transport_req = 2; srender(2);
            bad += check("RUDIMENT: NOTE MAIN is the lane and its LEN FILL ROT (hats =LEN, then 14, 7 hits); SEQ the LEN, NOTES RHY LANE NOTE OCT; "
                         "played, kick wraps at 16, hats at 14",
                         ok && mk == 15u && mh == 13u);
            {   /* the lanes' phase: MIDI Start (0xFA) resets it, Continue (0xFB) keeps it */
                uint8_t keep;
                transport_req = 1; srender(2);
                for (q = 0; q < 300u; q++) srender(1);
                transport_req = 2; srender(2);
                keep = shell_lane_pos[3];
                midi_clock_transport(0xFBu, fm1_ms);
                ok = shell_lane_pos[3] == keep;
                midi_clock_transport(0xFCu, fm1_ms);
                midi_clock_transport(0xFAu, fm1_ms);
                ok = ok && shell_lane_pos[3] == 0u && shell_lane_pos[0] == 0u;
                midi_clock_transport(0xFCu, fm1_ms);
                srender(2);
                bad += check("RUDIMENT: the lanes' phase: MIDI Continue keeps it, Start resets it", ok && keep != 0u);
            }
            sframes(400);                               /* (no HOME double tap: a mode switch) */
            shome();
            sframes(400);
        }
        {   /* a lane key held: ALGORITHM nudges it round the pattern, PRESETS mutates it (its density kept) */
            uint32_t before = 0, after = 0, snare0 = 0, snare1 = 0, n0 = 0, n1 = 0, len;
            sh_sp[SP_RHY - SP_FIRST] = 2;
            snote();
            sturn(EN_ALGO, -1);                         /* IO again: 16 steps */
            len = sh_lane_len(0);
            for (q = 0; q < len; q++) { before |= (step_lanes(&t->step[q]) & 1u) << q; snare0 |= ((step_lanes(&t->step[q]) >> 1) & 1u) << q; }
            fm1_in.notes |= 1u << SH_BLACK_KEYS[0]; host_notes |= 1u << SH_BLACK_KEYS[0]; sframe();
            sturn(EN_ALGO, 1);
            for (q = 0; q < len; q++) { after |= (step_lanes(&t->step[q]) & 1u) << q; snare1 |= ((step_lanes(&t->step[q]) >> 1) & 1u) << q; }
            ok = after == (((before << 1) | (before >> (len - 1u))) & 0xFFFFu) && snare1 == snare0 && sh_sp[SP_RHY - SP_FIRST] == 1;
            for (q = 0; q < len; q++) n0 += step_lanes(&t->step[q]) & 1u;
            sturn(EN_PRESET, 6);
            for (q = 0; q < len; q++) n1 += step_lanes(&t->step[q]) & 1u;
            fm1_in.notes &= ~(1u << SH_BLACK_KEYS[0]); sframe();
            bad += check("RUDIMENT: a lane held + ALGORITHM nudges it (+1: a step later, round); + PRESETS mutates it, the hits kept",
                         ok && n1 == n0 && n0 == 4u);
        }
        shome();
        sframes(400);
    }

    {   /* the stock pages the shell had hidden: VOICE (every engine), FILTER (ANALOG: CHORD), LANES (DRUM: RUDIMENT),
         * ENV DEST, LFO DEST / 2, INSERT / 2 (empty knobs inert), ARP 2 */
        uint32_t m = mode_of("FM6"), c = mode_of("CHORD"), r = mode_of("RUDIMENT"), k, seen;
        track_t *t;
        int16_t ia;
        sframes(400);
        sgo(m);
        t = sh_trk();
        sh.page[sh.mode] = SHP_VOICE; sframe();
        t->p[P_VOICE] = V_POLY;
        sturn(EN_K1, 1);
        shot("voice_page");
        ok = t->p[P_VOICE] == V_MONO;
        t->p[P_VOICE] = V_POLY;
        sh.page[sh.mode] = SHP_INS2; sframe();
        ia = t->p[P_IA];
        sturn(EN_K2, 3); sturn(EN_K3, 3);
        ok = ok && t->p[P_IA] == ia;
        stap(B_ARP); stap(B_ARP);
        ok = ok && sh.page[sh.mode] == SHP_ARP2;
        stap(B_LFO); stap(B_LFO);
        ok = ok && sh.page[sh.mode] == SHP_LFOD;
        bad += check("pages: VOICE (KNOB 1: POLY -> MONO), INSERT 2's empty knobs inert, ARP 2, LFO DEST", ok);
        sdown(B_GLO); sframe(); stap(B_EDIT); sframe();  /* GLO + EDIT: FM6's EDIT pages listed */
        shot("help_edit");
        ok = sh.help == B_EDIT + 1u;
        sup(B_GLO); sframe();
        bad += check("help: GLO + EDIT lists the part's EDIT pages (FM6: seven)", ok);
        sframes(400);
        sgo(c);
        for (seen = 0, k = 0; k < 8u; k++) { stap(B_EDIT); seen |= sh.page[sh.mode] == SHP_FILTER; }
        ok = seen;
        sframes(400);
        sgo(r);
        for (seen = 0, k = 0; k < 8u; k++) { stap(B_EDIT); seen |= (sh.page[sh.mode] == SHP_LANES1) << 0 | (sh.page[sh.mode] == SHP_FILTER) << 1; }
        bad += check("pages: CHORD's EDIT has ANALOG's FILTER, RUDIMENT's DRUM LANES (and no FILTER)", ok && seen == 1u);
        shome();
        sframes(400);
    }

    {   /* CHORD on SHOCK (E4): its first sound plays the chord; the pages follow the part's engine; the engine stays */
        uint32_t sk = slot_of("SHOCK"), cn = note_of("CHORD"), m = mode_of("FM6"), na, q;
        track_t *t;
        sframes(400);
        sgoc(sk, cn);
        t = sh_trk();
        ok = t->eng_req == ENGI_SHOCK && t->p[P_CHRD] != 0;
        for (q = 0; q < SHP_N; q++) ok = ok && (q != SHP_FILTER || !sh_page_ok(q));
        shot("chord_shock");
        srender(4);
        fm1_in.notes |= 1u << (CB_ROOT0 + 5u); host_notes |= 1u << (CB_ROOT0 + 5u); keyboard_block(); srender(8); sframe();
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        fm1_in.notes &= ~(1u << (CB_ROOT0 + 5u)); keyboard_block(); srender(40); sframe();
        ok = ok && na >= 3u;
        sframes(400);
        sgo(m);
        sframes(400);
        sgoc(sk, cn);
        ok = ok && sh_trk()->eng_req == ENGI_SHOCK;
        bad += check("CHORD on SHOCK: a chord sounds, no FILTER page; kept past a switch", ok);
        shome();
        sframes(400);
    }

    {   /* SCALE: Felucca's 16, then Braids / Yarns' 21 (shell build), JOGES, then USER */
        track_t *t = sh_trk();
        const param_desc_t *d = &TP[P_SCALE];
        int16_t keep = t->p[P_SCALE];
        t->p[P_SCALE] = 36;
        ok = d->max == 37 && str_eq(d->names[16], "BLMAJ") && str_eq(d->names[36], "JOGES") && scale_mask(t) == 0x639u &&
             str_eq(d->names[37], "USER") && SCALE_USER == 37u;
        t->p[P_SCALE] = keep;
        bad += check("SCALE: 38 scales, Braids / Yarns' 21 after Felucca's 16 (JOGES, its mask), then USER", ok);
    }

    {   /* MASK: on SCALE the keys are silent and draw the scale: D F A pressed together make D USER of those three
         * (every part, the root the lowest); a lone tap of E puts E in; a tap of the root's key takes it out; the
         * keys show the scale */
        track_t *t = sh_trk();
        int16_t kr[NTRK], ks[NTRK];
        uint16_t ku = scale_user;
        uint32_t i, sounding = 0;
        for (i = 0; i < NTRK; i++)
            kr[i] = trk[i].p[P_ROOT], ks[i] = trk[i].p[P_SCALE];
        stap(B_SCL);
        sframe();
        host_notes |= 1u << 9 | 1u << 12 | 1u << 16;     /* D4 F4 A4 (key 0 is F3) */
        fm1_in.notes |= 1u << 9 | 1u << 12 | 1u << 16; keyboard_block(); sframe(); srender(2);
        for (i = 0; i < NVOICE; i++)
            sounding += t->v[i].gate;
        fm1_in.notes &= ~(1u << 9 | 1u << 12 | 1u << 16); host_notes = 0; keyboard_block(); sframe(); sframe();
        ok = !sounding && t->p[P_ROOT] == 2 && (uint32_t)t->p[P_SCALE] == SCALE_USER && trk[3].p[P_ROOT] == 2 &&
             (uint32_t)trk[3].p[P_SCALE] == SCALE_USER && scale_mask(t) == (1u | 1u << 3 | 1u << 7);
        skey(11);                                       /* E4: in */
        sframe();
        ok = ok && scale_mask(t) == (1u | 1u << 2 | 1u << 3 | 1u << 7);
        skey(9);                                        /* D4, the root: out */
        sframe();
        ok = ok && scale_mask(t) == (1u << 2 | 1u << 3 | 1u << 7) && !memcmp(sh.toast, "D USER: 3 NOTES", 15);
        shell_leds();
        ok = ok && shell_kb_scl;
        stap(B_SCL);
        stap(B_SCL);
        sframe();
        ok = ok && !shell_kb_scl;
        bad += check("MASK: SCALE's keys draw the scale (together: those notes, the lowest the ROOT; a tap: one in or out)",
                     ok);
        scale_user = ku;
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_ROOT] = kr[i], trk[i].p[P_SCALE] = ks[i];
        shome();
    }

    {   /* ALIVE's hidden seed: SEED turned below 0% is NIBS, his face the history, the rule growing from his chin */
        uint32_t a = mode_of("ALIVE"), k, same = 0, r, i, g0;
        const char *label = 0;
        char val[12] = "";
        sgo(a);
        shome(); snmain();                      /* (SEED: on ALIVE's MAIN page, the NOTE half's first) */
        sh_sp[SP_SEED - SP_FIRST] = 0;
        for (k = 0; k < 4u; k++) {
            uint8_t ids[4];
            int glob;
            sh_page_ids(ids, &glob);
            if (ids[k] == SP_SEED) { sturn(KR4[k], -3); sh_param_text(k, &label, val); }
        }
        al_seed();
        for (r = 0; r < AL_ROWS; r++)
            for (i = 0; i < AL_W; i++)
                same += al_cell(r, i) == ((BR_NIBS_CELLS[r][i >> 3] >> (i & 7u)) & 1u);
        ok = sh_sp[SP_SEED - SP_FIRST] == -1 && str_eq(val, "NIBS") && same == AL_ROWS * AL_W;
        sframe();
        shot("alive_nibs");
        g0 = al.gen;
        transport_req = 1; srender(1);
        for (k = 0; k < 400u && al.gen < g0 + 3u; k++) srender(2);
        transport_req = 2; srender(2);
        bad += check("ALIVE: SEED below 0% reads NIBS, his face is the history; on PLAY the rule grows from it",
                     ok && al.gen >= g0 + 3u);
        sh_sp[SP_SEED - SP_FIRST] = 0;
    }

    {   /* NOTE and SOUND (shell.md): the partition, each half kept on its own, presets masked, pairs that don't fit */
        uint32_t i, j, dup = 0, both = 0, kn = note_of("KEYS"), cn = note_of("CHORD"), an = note_of("ALIVE");
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), vo = slot_of("DRUMS");
        static const uint8_t NOTE_MUST[] = {P_AMODE, P_ARATE, P_AOCT, P_AGATE, P_ASWING, P_APROB, P_AHOLD, P_AORDER,
                                            P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_SQNT, P_CHRD, P_VOIC, P_QUANT};
        static const uint8_t SOUND_MUST[] = {P_E0, P_E7, P_ATK, P_REL, P_LRATE, P_VOICE, P_GLIDE, P_GSPR, P_DIST, P_REV,
                                             P_ITYPE, P_FOLD, P_SPRD, P_M1SRC};
        static const uint8_t SHARED_MUST[] = {P_ROOT, P_SCALE, P_TRANS, P_LEVEL, P_PAN, P_MUTE};
        track_t *t;
        int16_t v0, v1;
        uint8_t ids[4];
        int glob;
        for (i = 0; i < NELEM(SH_NOTE_IDS); i++)
            for (j = 0; j < NELEM(SH_NOTE_IDS); j++)
                dup += i != j && SH_NOTE_IDS[i] == SH_NOTE_IDS[j];
        for (i = 0; i < NELEM(SH_NOTE_IDS); i++)
            for (j = 0; j < NELEM(SH_SHARED_IDS); j++)
                both += SH_NOTE_IDS[i] == SH_SHARED_IDS[j];
        ok = !dup && !both;
        for (i = 0; i < P_COUNT; i++)                   /* every id in exactly one half */
            ok = ok && sh_half(i) <= SH_SHARED;
        for (i = 0; i < NELEM(NOTE_MUST); i++) ok = ok && sh_half(NOTE_MUST[i]) == SH_NOTE;
        for (i = 0; i < NELEM(SOUND_MUST); i++) ok = ok && sh_half(SOUND_MUST[i]) == SH_SOUND;
        for (i = 0; i < NELEM(SHARED_MUST); i++) ok = ok && sh_half(SHARED_MUST[i]) == SH_SHARED;
        for (i = 0; i < NPAGES; i++)                    /* every stock ARP / SEQ page's ids: the NOTE half */
            if (PAGES[i].fam == FAM_ARP)
                for (j = 0; j < 4u; j++)
                    ok = ok && (PAGES[i].id[j] == 0xFFu || sh_half(PAGES[i].id[j]) == SH_NOTE);
        bad += check("NOTE / SOUND: every id in one half (the arp, the pattern, the chord keys NOTE; the key shared)", ok);
        for (ok = 1, i = 0; i < P_COUNT; i++)           /* what makes a preset load the SOUND only: Felucca keeps these */
            ok = ok && (sh_half(i) == SH_SOUND || param_kept(i));
        bad += check("NOTE / SOUND: every NOTE and SHARED id is one a preset load keeps (ui.c param_kept)", ok);

        sgo2(lz, kn);                                   /* a LIZARD tweak, then CHORD over LIZARD: the tweak is there */
        t = sh_trk();
        v0 = t->p[P_E5] = (int16_t)(t->p[P_E5] == 40 ? 41 : 40);
        sgo2(lz, cn);
        t = sh_trk();
        ok = t->p[P_E5] == v0 && sh_kind() == SHK_CHORD;
        t->p[P_VOIC] = VC_DROP2;                        /* CHORD's voicing, then CHORD over FM6: it follows CHORD */
        sgo2(fm, cn);
        t = sh_trk();
        ok = ok && t->p[P_VOIC] == VC_DROP2 && t->eng_req == ENGI_FM6;
        sgo2(lz, kn);
        ok = ok && sh_trk()->p[P_E5] == v0;
        bad += check("NOTE / SOUND: each half kept on its own (LIZARD's tweak under CHORD, CHORD's DROP2 over FM6)", ok);

        sgo2(fm, cn);
        t = sh_trk();
        t->p[P_AMODE] = 2; v1 = t->p[P_E0];
        shome(); snmain();                      /* the NOTE page: PRESETS still picks sounds, the page stays */
        for (i = 0; i < 3u; i++) sturn(EN_PRESET, 1);
        ok = sh.page[sh.mode] == SHP_NOTE && t->p[P_AMODE] == 2 && t->p[P_VOIC] == VC_DROP2;
        t->p[P_AMODE] = 0; t->p[P_VOIC] = VC_CLOSE;
        (void)v1;
        bad += check("NOTE / SOUND: PRESETS is the SOUND on a NOTE page; a preset never changes the arp or the voicing", ok);
        {   /* an arp running (latched), PRESETS turned through presets that would set another: its state never moves */
            uint32_t q, steady = 1;
            int16_t am, ar, ao;
            t->p[P_AMODE] = 1; t->p[P_ARATE] = 3; t->p[P_AHOLD] = 1; t->p[P_AORDER] = 1;
            am = t->p[P_AMODE]; ar = t->p[P_ARATE]; ao = t->p[P_AORDER];
            transport_req = 1; srender(1);
            fm1_in.notes |= 1u << CB_ROOT0; keyboard_block(); srender(4);
            fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block();
            for (q = 0; q < 6u; q++) {
                sturn(EN_PRESET, 1);
                srender(2);
                steady &= t->p[P_AMODE] == am && t->p[P_ARATE] == ar && t->p[P_AORDER] == ao && t->p[P_AHOLD] == 1;
            }
            transport_req = 2; srender(2);
            t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0; t->p[P_AORDER] = 0;
            srender(40);
            bad += check("NOTE / SOUND: a latched arp runs on through six preset loads, its settings never moving", steady);
        }

        t->p[P_VOICE] = V_MONO;                         /* CHORD into a MONO sound: CHORD plays POLY, the whole chord */
        sframes(400);                                   /* (HOME twice inside 320 ms: the pair before) */
        shome(); sframes(SH_POP_MS + 200u);
        ok = sh.mode == fm && sh.note == cn;
        skey_sung(CB_ROOT0);                            /* (a chord struck: the picture shows it) */
        sframes(100);
        {
            uint8_t out[CHORD_MAX];
            ok = ok && chord_build(t, 62u, out) > 1u && t->p[P_VOICE] == V_MONO;
            shot("pair_root_only");
            ok = ok && screen_ink(20, 196) > 0u;
        }
        t->p[P_VOICE] = V_POLY;
        sgoc(vo, cn);                                   /* .. into DRUMS (a kit): the root */
        {
            uint8_t out[CHORD_MAX];
            ok = ok && chord_build(sh_trk(), 62u, out) == 1u;
        }
        bad += check("NOTE / SOUND: CHORD into MONO plays the chord (VCE kept), into DRUMS the root", ok);
        sgoc(vo, an);                                   /* ALIVE into the kit: its notes go through, it grows */
        transport_req = 1; srender(1);
        v0 = (int16_t)al.gen;
        for (i = 0; i < 200u; i++) srender(2);
        ok = al.gen > (uint32_t)v0 && sh.part == 2u && trk[2].eng_req == ENGI_DRUM;
        transport_req = 2; srender(2);
        bad += check("NOTE / SOUND: ALIVE into DRUMS (a kit): its notes go through unchanged, it grows", ok);

        sgo2(fm, kn);                                   /* KEYS: ARP opens ARP, again ARP 2; EDIT keeps its depth */
        sh.lastedit[fm] = 0;
        shome(); stap(B_ARP);
        ok = sh.page[sh.mode] == SHP_ARP;
        stap(B_ARP);
        ok = ok && sh.page[sh.mode] == SHP_ARP2;
        stap(B_ARP);
        ok = ok && sh.page[sh.mode] == SHP_ARP;
        stap(B_EDIT); stap(B_EDIT); stap(B_EDIT);        /* (EDIT 3: FM6's OPERATOR) */
        i = sh.page[sh.mode];
        stap(B_ARP);
        ok = ok && sh.page[sh.mode] == SHP_ARP;
        stap(B_EDIT);
        ok = ok && sh.page[sh.mode] == i;
        sh_page_ids(ids, &glob);
        (void)glob;
        bad += check("NOTE / SOUND: ARP opens ARP (MODE first) from anywhere, again ARP 2, again ARP; EDIT goes back to its page as left", ok);

        sgo2(lz, cn);
        sframe();
        ok = SH_ST[SH_ST_PAIR] == lz + 1u && SH_ST[SH_ST_PAIR + 1u] == cn + 1u;
        bad += check("NOTE / SOUND: the pair in front kept with the shell's settings", ok);
#if !FELUCCA_TALK
        {   /* TALK left out of this build: its label, a project or a saved pair naming it never puts it in front */
            uint32_t tk = slot_of("TALK"), e0 = sh_trk()->eng_req;
            uint8_t pair[8] = {0};
            const char *lb;
            sh_trk()->eng_req = ENGI_TALK;              /* (as a SysEx sender of old could) */
            lb = sh_preset_label();
            sh_trk()->eng_req = (uint8_t)e0;
            ok = str_eq(lb, "-");
            pair[0] = (uint8_t)tk; pair[1] = (uint8_t)cn; pair[2] = 0; pair[3] = SH_PAIR_NONE; pair[7] = 1;
            sh_project_loaded(pair, 1, (const uint8_t[9]){127, 127, 127, 127, 0, 127, 127, 127, 127});
            sframe();
            ok = ok && sh.mode == lz && sh_trk()->eng_req != ENGI_TALK;
            SH_ST[SH_ST_PAIR] = (uint8_t)(tk + 1u);     /* the last pair TALK, as a TALK build saved it: a boot */
            sh.ready = 0;
            sframe();
            ok = ok && sh.ready && sh.mode != tk && sh_trk()->eng_req != ENGI_TALK;
            bad += check("TALK left out: its label reads -, a project or a saved pair naming it leaves the pair alone",
                         ok && !eng_ok(ENGI_TALK));
            sframes(SH_TOAST_MS + 200u);
        }
#endif
        {   /* a backup's settings put back (editor_backup.c): the shell takes WOW, the wheel and the pair from them */
            uint8_t st[16], st3[16];
            uint32_t en = note_of("ENIGMA");
            const param_desc_t *dw = &SP_DESC[SP_WOW - SP_FIRST];
            int16_t wow;
            sgo2(lz, cn);
            sframe();
            wow = sh_sp[SP_WOW - SP_FIRST];
            memcpy(st, SH_ST, sizeof st);
            memcpy(st3, SH_ST3, sizeof st3);
            st3[0] = SH_ST3_MARK;                       /* (a wheel the player set, in the backup) */
            st3[9u + 1u] = (uint8_t)(en + 1u);           /* (its second black key: ENIGMA, where the live wheel has CHORD) */
            sh_sp[SP_WOW - SP_FIRST] = (int16_t)(wow == dw->max ? dw->min : dw->max);
            sgo2(fm, kn);
            sframe();                                   /* (the live values stored over the bytes) */
            ok = SH_ST[SH_ST_PAIR] == fm + 1u && sh_wnoteslot[1] == cn;
            memcpy(SH_ST, st, sizeof st);               /* the restore: settings_import puts the backup's bytes back */
            memcpy(SH_ST3, st3, sizeof st3);
            sh_restored = 1;
            sframes(64);
            ok = ok && sh_sp[SP_WOW - SP_FIRST] == wow && sh_wnoteslot[1] == en && sh.mode == lz &&
                 SH_ST[SH_ST_PAIR] == lz + 1u && !sh_restored;
            bad += check("backup restored: the shell takes WOW, the wheel and the pair from the backup, not the live ones", ok);
            sh_wnoteslot[1] = (uint8_t)cn;
            sframes(SH_TOAST_MS + 200u);
        }
        {   /* the pair's sound over power-off: its preset, or its user slot while that still holds its engine's sound */
            track_t *t;
            uint32_t k = UP_SLOTS - 1u, p0;
            uint8_t st[16];
#define BOOT() (memcpy(st, SH_ST, sizeof st), sgo2(lz, kn), sframe(), memcpy(SH_ST, st, sizeof st), \
                sh_snap_of(fm)->valid = 0, memset(sh_partpair, 0xFF, sizeof sh_partpair), \
                sh_enter_saved())   /* (a power-on: no snapshot kept for FM6, no part holding a pair) */
            up_rec_t keep;
            sgo2(fm, kn);
            sframe();
            t = sh_trk();
            sturn(EN_PRESET, 3);
            sframes(SH_ST_IDLE_MS + 500u);
            p0 = t->preset;
            ok = p0 && SH_ST[SH_ST_SND] == p0 + 1u && !SH_ST[SH_ST_SND + 1u];
            ok = ok && BOOT();                          /* power-on, as from another pair */
            t = sh_trk();                               /* (the pair may land on the other part) */
            ok = ok && t->preset == p0 && !t->user;
            memcpy(&keep, up_rec(k), sizeof keep);
            {   int rc = up_store(k, "KEPT");           /* a user sound of FM6, in front (3: RAM only, the host) */
                ok = ok && (rc == 0 || rc == 3);
            }
            t->user = (uint8_t)(k + 1u);
            sframes(SH_ST_IDLE_MS + 500u);
            ok = ok && SH_ST[SH_ST_SND + 1u] == k + 1u && !SH_ST[SH_ST_SND];
            ok = ok && BOOT();
            t = sh_trk();
            ok = ok && t->user == k + 1u;
            memset(up_rec(k), 0, sizeof *up_rec(k));    /* the slot deleted, then power-on: the first preset, silently */
            ok = ok && BOOT();
            t = sh_trk();
            ok = ok && t->preset == 0u && !t->user;
            memcpy(up_rec(k), &keep, sizeof keep);
            sframes(SH_ST_IDLE_MS + 500u);
            bad += check("power-on: the pair comes back on the sound it played (its preset or user slot; a deleted slot: the first)", ok);
#undef BOOT
        }
        shome();
        sgo2(fm, kn);
    }

    {   /* the note_engine.h engines on the spare black keys: TONNETZ walks voice-led triads, ENIGMA loops a melody;
         * both on the part's clock, quiet on stop and on a switch, their four on MAIN and kept */
        uint32_t tn = note_of("TONNETZ"), en = note_of("ENIGMA"), fm = slot_of("FM6"), q, k, steps, moved = 0, smooth = 1;
        uint32_t na, last[3] = {0, 0, 0}, have = 0, seq[16], ns, same;
        uint8_t ids[4];
        int glob;
        track_t *t;
        sgo2(fm, tn);
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        shome(); snmain();
        sh_page_ids(ids, &glob);
        ok = sh_kind() == SHK_NE && sh.page[sh.mode] == SHP_NOTE && ids[0] == SP_NE0 && shell_kb_skip == SH_ALL_KEYS;
        sh_ne_val[tn - SH_NE_FIRST][1] = 2;             /* EVERY 2 */
        sh_ne_val[tn - SH_NE_FIRST][2] = 0;             /* PLAY CHORD */
        transport_req = 1; srender(1);
        fm1_in.notes |= 1u << 14; keyboard_block(); srender(2);   /* G4: the triad's root */
        fm1_in.notes &= ~(1u << 14); keyboard_block();
        for (steps = 0; steps < 2000u && moved < 4u; steps++) {
            uint32_t now[3], n = 0;
            srender(1);
            for (q = 0; q < NVOICE && n < 3u; q++)
                if (t->v[q].active && t->v[q].gate) now[n++] = t->v[q].note;
            if (n == 3u) {
                for (k = 1; k < 3u; k++) {             /* (sorted) */
                    uint32_t x = now[k], j = k;
                    while (j && now[j - 1u] > x) { now[j] = now[j - 1u]; j--; }
                    now[j] = x;
                }
                if (have && (now[0] != last[0] || now[1] != last[1] || now[2] != last[2])) {
                    uint32_t d = 0;
                    for (k = 0; k < 3u; k++) d += now[k] > last[k] ? now[k] - last[k] : last[k] - now[k];
                    smooth &= d <= 4u;                  /* (a move: one voice by a step or two, or none) */
                    moved++;
                }
                memcpy(last, now, sizeof last);
                have = 1;
            }
        }
        ok = ok && moved >= 4u && smooth;
        transport_req = 2; srender(4);
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        bad += check("TONNETZ: triads walk on the clock, each move voice-led (a step or two); stop: none left on", ok && !na);
        {   /* stopped, a key held: it walks as if the transport ran, at the part's DIV; the last key up silences it */
            uint32_t ch = 0, prev = 0, hn = 0, cur;
            fm1_in.notes |= 1u << 14; keyboard_block();
            for (steps = 0; steps < 400u; steps++) {
                srender(1);
                for (cur = 0, q = 0; q < NVOICE; q++)
                    if (t->v[q].active && t->v[q].gate) cur += t->v[q].note * (q + 1u);
                hn += cur != 0u;
                ch += prev && cur && cur != prev;
                prev = cur ? cur : prev;
            }
            fm1_in.notes &= ~(1u << 14); keyboard_block(); srender(4);
            for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
            printf("  (TONNETZ held while stopped: %u blocks sounding, %u changes, %u on after)\n", hn, ch, na);
            bad += check("TONNETZ stopped, a key held: it walks as if playing; let go: silent",
                         !song.playing && hn > 300u && ch >= 2u && !na);
        }
        {   /* S+H stopped, a key held: the line moves (a tap's transpose, not a held sample); let go: silent */
            uint32_t notes[128] = {0}, distinct = 0;
            sgo2(fm, note_of("S+H"));
            t = sh_trk();
            t->p[P_VOICE] = V_POLY;
            fm1_in.notes |= 1u << 7; keyboard_block();          /* C4 */
            for (steps = 0; steps < 600u; steps++) {
                srender(1);
                for (q = 0; q < NVOICE; q++)
                    if (t->v[q].active && t->v[q].gate && !notes[t->v[q].note & 127u]++)
                        distinct++;
            }
            fm1_in.notes &= ~(1u << 7); keyboard_block(); srender(8);
            for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
            printf("  (S+H held while stopped: %u distinct notes, %u on after)\n", distinct, na);
            bad += check("S+H stopped, a key held: the line plays and moves; let go: silent",
                         !song.playing && distinct >= 3u && !na && !sh_snh_tapped);
            sgo2(fm, note_of("ALIVE"));                 /* ALIVE stopped, a key held: rows grow and notes play */
            t = sh_trk();
            t->p[P_VOICE] = V_POLY;
            {
                uint32_t g0 = al.gen, on = 0;
                fm1_in.notes |= 1u << 7; keyboard_block();
                for (steps = 0; steps < 600u; steps++) {
                    srender(1);
                    for (q = 0; q < NVOICE; q++) on += t->v[q].active && t->v[q].gate;
                }
                fm1_in.notes &= ~(1u << 7); keyboard_block(); srender(8);
                for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
                printf("  (ALIVE held while stopped: %u rows, %u voice-blocks, %u on after)\n", al.gen - g0, on, na);
                bad += check("ALIVE stopped, a key held: it grows and plays; let go: silent",
                             !song.playing && al.gen - g0 >= 8u && on > 0u && !na);
            }
            sgo2(fm, tn);
            t = sh_trk();
        }

        sgo2(fm, en);                                   /* ENIGMA: LEN 4 (the part's), CHNC 0, DENS 100: a 4-step loop */
        t = sh_trk();
        shome(); snmain();
        {
            int16_t *vp;
            ok = sh_desc(SP_NE0, 0, &vp) == track_desc(t, P_SLEN) && vp == &t->p[P_SLEN];   /* (its LEN knob: the part's) */
        }
        t->p[P_SLEN] = 4; sh_ne_val[en - SH_NE_FIRST][1] = 0; sh_ne_val[en - SH_NE_FIRST][3] = 100;
        transport_req = 1; srender(1);
        for (ns = 0, steps = 0; steps < 4000u && ns < 16u; steps++) {
            uint32_t idx = t->seq_idx;
            srender(1);
            if (t->seq_idx != idx)
                for (q = 0; q < NVOICE; q++)
                    if (t->v[q].active && t->v[q].gate && t->v[q].age >= t->v[0].age) { seq[ns++] = t->v[q].note; break; }
        }
        for (same = 0, k = 4; k < ns; k++) same += seq[k] == seq[k - 4u];
        transport_req = 2; srender(4);
        ok = ok && ns >= 12u && same + 1u >= ns - 4u && sh_ne_sent[en - SH_NE_FIRST][NE_P_LEN] == 4 + 1;
        t->p[P_SLEN] = 16;
        sgo2(fm, note_of("KEYS"));                      /* switched away: quiet, the keys the player's again */
        srender(40);
        for (na = 0, q = 0; q < NVOICE; q++) na += trk[0].v[q].active && trk[0].v[q].gate && trk[1].v[q].gate;
        ok = ok && !shell_ne_idx && shell_kb_skip == 0u;
        sframes(SH_ST_IDLE_MS + 500u);
        ok = ok && SH_ST2[4u * (en - SH_NE_FIRST) + 3u] == 100u + 1u;   /* (DENS 100: kept as 101) */
        bad += check("ENIGMA: LEN 4 (the part's, its knob the same) at no change loops every 4 steps; a switch ends it; "
                     "its values kept", ok);
    }

    {   /* LINE's generators are its content: ENIGMA on LINE with REC armed records nothing into the part, and plays on
         * (never quiet) */
        uint32_t en = note_of("ENIGMA"), fm = slot_of("FM6"), k, b, notes = 0, on = 0, q;
        track_t *t;
        sgo2(fm, en);
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        sh_ne_val[en - SH_NE_FIRST][0] = 4; sh_ne_val[en - SH_NE_FIRST][1] = 0; sh_ne_val[en - SH_NE_FIRST][3] = 100;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = (uint16_t)(1u << sh.part);
        transport_req = 1; srender(1);
        for (b = 0; b < 4000u && t->seq_idx < 12u; b++) {
            srender(1);
            for (q = 0; q < NVOICE; q++)
                on += t->v[q].active && t->v[q].gate;
        }
        transport_req = 2; srender(4);
        song.rec = 0;
        for (k = 0; k < NSTEP; k++)
            notes += t->step[k].n;
        ok = sh.part == 2u && !notes && on > 0u && !sh_gen_give_way(t);
        bad += check("REC: ENIGMA on LINE (part 3) records nothing into its part and plays on, armed or not", ok);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sgo2(fm, note_of("KEYS"));
    }

    {   /* S+H and CANON: appended to the catalogue (after LINES), engines 3 and 4 by the index map */
        static uint8_t mem[NE_MEM_MAX] __attribute__((aligned(4)));
        const note_engine_t *e = &NE_SNH;
        ne_out_t o;
        uint32_t k, n, inscale = 1, same = 1, cyc = 1, wide = 0, walk = 1, quiet = 1;
        int last = -1, notes[16];
        uint8_t seen[256];
        static const int PI_NOTES[7] = {62, 67, 62, 69, 62, 64, 71};   /* PI's 1 4 1 5 9 2 6, mod 8, from C4 in C major */
        ok = SH_NOTES[note_of("S+H")].ne == &NE_SNH && SH_NOTES[note_of("CANON")].ne == &NE_CANON &&
             note_of("S+H") == 8u && sh_ne_i(note_of("S+H")) == 3u && sh_ne_i(note_of("CANON")) == 4u &&
             sh_ne_i(SH_N_LINES) == SH_NNE && SH_NE_AT[0] == note_of("TONNETZ") && SH_NE_AT[2] == note_of("HARP");
        for (k = 0; k < SH_NNE; k++)                    /* (each engine's state slot is its own size or more) */
            ok &= SH_NOTES[SH_NE_AT[k]].ne->mem <= SH_NE_MEMAT[k + 1u] - SH_NE_MEMAT[k];
        for (k = 0; k < SH_NNOTES; k++)                 /* (the generators, and only they, share LINE's pattern) */
            ok &= (SH_NREC_BANK[k] == SH_NREC_GEN) == (SH_NOTES[k].kind == SHK_ALIVE ||
                                                       (SH_NOTES[k].kind == SHK_NE && SH_NOTES[k].part == SH_LINE));
        bad += check("S+H and CANON: after LINES in the catalogue, NOTE engines 3 and 4 by the index map", ok);

        memset(mem, 0, sizeof mem); e->init(mem);
        e->param(mem, 0, SNH_INT); e->param(mem, 1, 1); e->param(mem, 2, 0); e->param(mem, 3, 100);
        e->scale(mem, 0, 0xAB5);
        o.n = 0; e->key(mem, 60 - NE_KEY_F3, 1, &o); e->key(mem, 60 - NE_KEY_F3, 0, &o);
        for (ok = 1, k = 0; k < 7u; k++) {
            o.n = 0; e->step(mem, k, &o);
            for (n = 0; n < (uint32_t)o.n && !o.ev[n].on; n++)
                ;
            ok &= n < (uint32_t)o.n && o.ev[n].note == PI_NOTES[k];
        }
        e->key(mem, 64 - NE_KEY_F3, 1, &o);            /* E4 held: the sample holds, from E4 */
        for (k = 0; k < 6u; k++) {
            o.n = 0; e->step(mem, k, &o);
            for (n = 0; n < (uint32_t)o.n && !o.ev[n].on; n++)
                ;
            notes[k] = n < (uint32_t)o.n ? o.ev[n].note : -1;
            same &= notes[k] == notes[0];
        }
        ok = ok && same && notes[0] == 74;              /* (the last sample, B's degree 6, from E: D5) */
        e->key(mem, 64 - NE_KEY_F3, 0, &o);
        bad += check("S+H INT PI: digits mod RANGE into C major from the key; a held key holds the sample", ok);

        for (k = 0; k < 5u; k++) {                      /* every source, P high: notes in the scale, two octaves */
            memset(mem, 0, sizeof mem); e->init(mem);
            e->param(mem, 0, (int16_t)k); e->param(mem, 1, 2); e->param(mem, 2, 15); e->param(mem, 3, 100);
            e->scale(mem, 2, 0x6AD);                    /* D dorian (the mask from D) */
            for (n = 0; n < 200u; n++) {
                int i;
                o.n = 0; e->step(mem, n, &o);
                for (i = 0; i < o.n; i++)
                    if (o.ev[i].on)
                        inscale &= ((0x6ADu >> ((o.ev[i].note + 10u) % 12u)) & 1u) && o.ev[i].note >= 60 - 12 &&
                                   o.ev[i].note <= 60 + 26;
            }
        }
        memset(mem, 0, sizeof mem); e->init(mem);      /* LOGIS: r 3.5 settles on four values, r 4.0 is chaos */
        e->param(mem, 0, SNH_LOGIS); e->param(mem, 2, 0); e->param(mem, 3, 100);
        for (n = 0; n < 100u; n++) { o.n = 0; e->step(mem, n, &o); }
        memset(seen, 0, sizeof seen);
        for (k = 0, n = 0; n < 64u; n++) { o.n = 0; e->step(mem, 100u + n, &o); k += !seen[((snh_t *)mem)->val]; seen[((snh_t *)mem)->val] = 1; }
        cyc = k <= 4u;
        e->param(mem, 2, 15);
        memset(seen, 0, sizeof seen);
        for (k = 0, n = 0; n < 64u; n++) { o.n = 0; e->step(mem, 100u + n, &o); k += !seen[((snh_t *)mem)->val]; seen[((snh_t *)mem)->val] = 1; }
        wide = k >= 24u;
        memset(mem, 0, sizeof mem); e->init(mem);      /* WALK, P 0: a degree a step, within RANGE */
        e->param(mem, 0, SNH_WALK); e->param(mem, 1, 1); e->param(mem, 2, 0); e->param(mem, 3, 100);
        for (n = 0; n < 200u; n++) {
            int d0 = ((snh_t *)mem)->deg, d;
            o.n = 0; e->step(mem, n, &o);
            d = ((snh_t *)mem)->deg;
            walk &= (d - d0 == 1 || d0 - d == 1) && d >= 0 && d <= 7;
            last = d;
        }
        e->param(mem, 3, 0);                            /* DENS 0: silent */
        for (n = 0; n < 50u; n++) { int i; o.n = 0; e->step(mem, n, &o); for (i = 0; i < o.n; i++) quiet &= !o.ev[i].on; }
        {   /* LEN is the loop: idx wrapping at 8, NOISE at P 15 plays the same 8 notes each time round */
            int a[24];
            uint32_t i;
            memset(mem, 0, sizeof mem); e->init(mem);
            e->param(mem, 0, SNH_NOISE); e->param(mem, 1, 6); e->param(mem, 2, 15); e->param(mem, 3, 100);   /* (2 LOCK) */
            e->scale(mem, 0, 0xAB5);
            for (n = 0; n < 24u; n++) {
                o.n = 0; e->step(mem, n % 8u, &o);
                for (a[n] = -1, i = 0; i < (uint32_t)o.n; i++)
                    if (o.ev[i].on) a[n] = o.ev[i].note;
            }
            for (n = 8; n < 24u; n++) cyc &= a[n] == a[n - 8u];
        }
        ok = inscale && cyc && wide && walk && last >= 0 && quiet;
        bad += check("S+H: every source lands in the scale and RANGE; LOGIS 3.5 cycles, 4.0 does not; WALK a degree a "
                     "step; DENS 0 silent; LOCK: from its seed again each LEN", ok);
        printf("  (S+H: in scale %u, logistic cycle %u wide %u, walk %u, quiet %u)\n", inscale, cyc, wide, walk, quiet);
    }

    {   /* CANON: the keys sound at once and into the ring; the answer DELAY steps later, DEG degrees up; HOLD loops */
        static uint8_t mem[NE_MEM_MAX] __attribute__((aligned(4)));
        const note_engine_t *e = &NE_CANON;
        ne_out_t o;
        uint32_t k, n, loop = 1;
        int on[64], off[64], hv[24];
        memset(mem, 0, sizeof mem); e->init(mem);
        e->param(mem, 0, 2); e->param(mem, 1, 1); e->param(mem, 2, 2); e->param(mem, 3, 0);
        e->scale(mem, 0, 0xAB5);
        o.n = 0; e->key(mem, 60 - NE_KEY_F3, 1, &o);
        ok = o.n == 1 && o.ev[0].on && o.ev[0].note == 60;
        for (k = 0; k < 6u; k++) {
            int i;
            if (k == 1u) { o.n = 0; e->key(mem, 60 - NE_KEY_F3, 0, &o); ok &= o.n == 1 && !o.ev[0].on; }
            o.n = 0; e->step(mem, k, &o);
            on[k] = off[k] = -1;
            for (i = 0; i < o.n; i++)
                if (o.ev[i].on) on[k] = o.ev[i].note; else off[k] = o.ev[i].note;
        }
        ok = ok && on[0] < 0 && on[1] == 64 && on[2] < 0 && off[2] < 0 && off[3] == 64 && on[3] < 0;   /* (E4, tied a step) */
        bad += check("CANON: a key sounds at once; DELAY 2 DEG 2 answers a third up two steps on, held as it was", ok);

        memset(mem, 0, sizeof mem); e->init(mem);      /* four notes over 8 steps, then HOLD 8: they loop, answered */
        e->param(mem, 0, 2); e->param(mem, 1, 2); e->param(mem, 2, 0); e->param(mem, 3, 0);
        e->scale(mem, 0, 0xAB5);
        for (k = 0; k < 8u; k++) {
            o.n = 0;
            if (!(k & 1u)) e->key(mem, (uint8_t)(60 - NE_KEY_F3 + 2u * k), 1, &o);
            e->step(mem, k, &o);
            if (!(k & 1u)) e->key(mem, (uint8_t)(60 - NE_KEY_F3 + 2u * k), 0, &o);
        }
        e->param(mem, NE_P_LEN, 8);                     /* (the part's LEN 8: the loop) */
        e->param(mem, 3, 1);
        for (k = 0; k < 24u; k++) {
            int i;
            o.n = 0; e->step(mem, k, &o);
            hv[k] = -1;
            for (i = 0; i < o.n; i++)
                if (o.ev[i].on && o.ev[i].vel == 104) hv[k] = o.ev[i].note;   /* (the loop's own voice) */
        }
        for (n = 0, k = 8; k < 24u; k++) { loop &= hv[k] == hv[k - 8u]; n += hv[k] >= 0; }
        ok = loop && n >= 2u && ((canon_t *)mem)->vox == 2u;
        bad += check("CANON HOLD at LEN 8: the last 8 steps loop on their own, the same every 8", ok);
        printf("  (CANON hold: loop %u, notes in 16 %u)\n", loop, n);
    }

    {   /* CANON on LINE through the shell: a key, and its answer on the part DELAY steps later; its values kept */
        uint32_t cn = note_of("CANON"), fm = slot_of("FM6"), b, q, heard = 0, i = sh_ne_i(note_of("CANON"));
        track_t *t;
        sgo2(fm, cn);
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;            /* C major (CHR, the default: DEG counts semitones) */
        sh_ne_val[i][0] = 2; sh_ne_val[i][1] = 0; sh_ne_val[i][2] = 2;   /* (VOX's shell value counts from 0: 1) */
        transport_req = 1; srender(1);
        for (b = 0; b < 4000u && t->seq_idx < 2u; b++) srender(1);
        fm1_in.notes |= 1u << 7; host_notes |= 1u << 7; keyboard_block(); srender(1);   /* C4 */
        fm1_in.notes &= ~(1u << 7); keyboard_block();
        for (b = 0; b < 4000u && t->seq_idx < 8u; b++) {
            srender(1);
            for (q = 0; q < NVOICE; q++)
                heard |= t->v[q].active && t->v[q].gate && t->v[q].note == 64u;
        }
        transport_req = 2; srender(4);
        ok = sh.part == SH_LINE && heard && shell_ne_on[1] == i + 1u;
        sframes(SH_ST_IDLE_MS + 500u);
        ok = ok && sh_st2(i)[0] == 2u && SH_ST4[0] == 2u;   /* (DELAY 2, its min 1: kept as 2, in DRUM's row) */
        bad += check("CANON on LINE: a key's answer a third up plays on the part; its four kept past the first four "
                     "engines'", ok);
        sgo2(fm, note_of("KEYS"));
    }

    {   /* PROG (chordprog.h): roots along the scale; the directions */
        cpg_t c = {0};
        uint32_t k, seen = 0, ok2 = 1, prev;
        static const uint8_t PEND[8] = {1, 2, 3, 2, 1, 0, 1, 2};
        ok = cpg_root(0xAB5, 0, 60, 4) == 67 && cpg_root(0xAB5, 0, 60, 5) == 69 && cpg_root(0xAB5, 0, 60, -1) == 59 &&
             cpg_root(0xAB5, 0, 61, 1) == 63 && cpg_root(0x5AD, 9, 57, -1) == 55 && cpg_root(0xAB5, 0, 60, 7) == 72 &&
             cpg_len(4, 0) == 12u && cpg_len(1, 6) == 6u && cpg_degree(1, 5) == 4;   /* (POP's 6th: its 2nd again) */
        cpg_start(&c);
        for (k = 0; k < 8u; k++) { cpg_next(&c, CPG_PEND, 4); ok2 &= c.i == PEND[k]; }
        cpg_start(&c);
        for (k = 0; k < 4u; k++) cpg_next(&c, CPG_REV, 4);
        ok2 &= c.i == 0u;
        for (k = 0; k < 200u; k++) { prev = c.i; cpg_next(&c, CPG_RAND, 4); ok2 &= c.i != prev && c.i < 4u; seen |= 1u << c.i; }
        ok2 &= seen == 15u;
        for (k = 0; k < 200u; k++) { prev = c.i; cpg_next(&c, CPG_BROWN, 4); ok2 &= (c.i + 4u - prev) % 4u <= 1u || (prev + 4u - c.i) % 4u == 1u; }
        bad += check("PROG: a degree moves the root by the scale's own steps; FWD REV PEND RAND BROWN", ok && ok2);
    }

    {   /* PROG on CHORD: stopped, each press the next chord; playing, a held chord moves on the bar; a project keeps it */
        uint32_t cn = note_of("CHORD"), fm = slot_of("FM6"), k, b, roots[6], nr = 0, bar = 99;
        uint8_t pg[4], back[4];
        static const uint8_t POP[4] = {72, 79, 81, 77};   /* from C5: C G A F (I V vi IV) */
        track_t *t;
        sgo2(fm, cn);
        t = sh_trk();
        t->p[P_ROOT] = 0;
        sh_sp[SP_PROG - SP_FIRST] = 1; sh_sp[SP_PEVERY - SP_FIRST] = 1; sh_sp[SP_PDIR - SP_FIRST] = 0;
        sh_sp[SP_PLEN - SP_FIRST] = 0;
        sframe();
        ok = sh_page_ok(SHP_PROG) && shell_chord_prog == 1u;
        for (k = 0; k < 5u; k++) {                      /* C5 five times: I V vi IV I */
            skey(19);
            ok &= chord_last[sh.part].root == POP[k % 4u];
        }
        bad += check("PROG POP, stopped: C5 pressed again and again plays C G Am F C", ok);

        chord_cpg.started = 0;
        fm1_in.notes |= 1u << 19; host_notes |= 1u << 19; keyboard_block(); sframe();   /* C5 held */
        transport_req = 1; srender(1);
        for (b = 0; b < 20000u && clk_abs < 16u * 4u + 2u; b++) {
            srender(1);
            if (clk_abs >> 4 != bar && clk_abs % 16u >= 1u && nr < 6u) {
                bar = clk_abs >> 4;
                roots[nr++] = chord_last[sh.part].root;
            }
        }
        transport_req = 2; srender(4);
        fm1_in.notes &= ~(1u << 19); keyboard_block(); sframe();
        ok = nr >= 4u;
        for (k = 0; k < nr && k < 4u; k++)
            ok &= roots[k] == POP[k];
        printf("  (PROG playing: %u bars, roots %u %u %u %u)\n", nr, roots[0], roots[1], roots[2], roots[3]);
        bad += check("PROG POP playing, 1 BAR: a held C5 is C, G, Am, F bar by bar from the start", ok);

        {   /* with an arp mode on, the arp's held notes follow the progression */
            uint32_t q, has79 = 0, has72 = 0, b2;
            t->p[P_AMODE] = 1;
            chord_cpg.started = 0;
            fm1_in.notes |= 1u << 19; host_notes |= 1u << 19; keyboard_block(); sframe();
            transport_req = 1; srender(1);
            for (b2 = 0; b2 < 20000u && clk_abs < 16u + 4u; b2++)
                srender(1);
            for (q = 0; q < t->nheld; q++) { has79 |= t->held[q] == 79u; has72 |= t->held[q] == 72u; }
            transport_req = 2; srender(4);
            fm1_in.notes &= ~(1u << 19); keyboard_block(); sframe();
            t->p[P_AMODE] = 0; sframe();
            bad += check("PROG under the arp: in bar 2 the arp holds G's notes, not C's", has79 && !has72);
        }

        sh_prog_get(pg);
        sh_sp[SP_PROG - SP_FIRST] = 0; sh_sp[SP_PEVERY - SP_FIRST] = 3;
        sh_prog_set(pg);
        sh_prog_get(back);
        back[0] = 50; sh_prog_set(back);                /* (a later firmware's progression: the last one) */
        ok = pg[0] == 1u && pg[1] == 1u && sh_sp[SP_PROG - SP_FIRST] == (int16_t)(CPG_N - 1u);
        sh_prog_set(pg);
        ok = ok && sh_sp[SP_PEVERY - SP_FIRST] == 1;
        sh_sp[SP_PROG - SP_FIRST] = 0; sframe();
        bad += check("PROG: a project's four come back, an unknown progression clamped", ok);
        sgo2(fm, note_of("KEYS"));
    }

#if FELUCCA_ORGAN
    {   /* ORGAN (LIBRARY, databurn's): under CHORD with TUNE JUST and PERC 3RD, single trigger: the chord's first note
         * (the lowest, struck alone) gets the percussion, the rest do not; the third just */
        uint32_t lib = slot_of("LIBRARY"), cn = note_of("CHORD"), q, k, withp = 0, lowp = 0, low = 128;
        int32_t p3 = 0;
        track_t *t;
        sgo2(lib, cn);
        t = sh_trk();
        for (k = 0; k < 40u && t->eng_req != ENGI_ORGAN; k++) {   /* GLO + PRESETS: engine to engine */
            sdown(B_GLO); sframe(); sturn(EN_PRESET, 1); sup(B_GLO); sframe();
        }
        ok = t->eng_req == ENGI_ORGAN;
        srender(40);                                    /* (the switch's fade, then the engine is the part's) */
        t->p[P_VOICE] = V_POLY; t->p[P_E7] = ORG_P3; t->p[P_QUANT] = 0;
        sh_sp[SP_TUNE - SP_FIRST] = 1; sframe();
        memset(t->v, 0, sizeof t->v);
        fm1_in.notes |= 1u << CB_ROOT0; keyboard_block(); srender(1);   /* D4: D F# A */
        for (q = 0; q < NVOICE; q++)
            if (t->v[q].active && t->v[q].gate) {
                uint32_t has = org_of(t)->v[q].perc > 0;
                withp += has;
                if (t->v[q].note < low) { low = t->v[q].note; lowp = has; }
                if (t->v[q].note == 66u) p3 = t->v[q].pitch16;
            }
        fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(40);
        sh_sp[SP_TUNE - SP_FIRST] = 0; t->p[P_QUANT] = 1; sframe();
        bad += check("ORGAN under CHORD, JUST, PERC 3RD: the first note alone gets the percussion; the third 5:4",
                     ok && withp == 1u && lowp && p3 == 66 * 16 - 2);
        shot("organ_chord");
        shome(); sframes(400); stap(B_EDIT); sframes(SH_POP_MS + 200u);
        shot("organ_bars");
    }
#endif

    {   /* the wheel's white keys are the build's eight SOUNDs, and no others are offered: GLO + a white key in the wheel
         * opens no picker; a layout from the editor or an old installer (LIBRARY on E4) sets the black keys and leaves
         * the white ones; a black key's rules still hold; the default stash */
        static const char *const EIGHT[SH_SLOTS] = {"FM6", "LIZARD", "SHOCK", "DRUMS", "MOUTH", "ANALOG", "PHASE",
                                                    "PHYS"};
        uint8_t w0[SH_WHEEL_N], w[SH_WHEEL_N], g[SH_WHEEL_N];
        uint32_t fm = slot_of("FM6"), en = note_of("ENIGMA"), k, avail = 0;
        sgo(fm);
        sframes(400);
        sh_wheel_get(w0);
        ok = 1;
        for (k = 0; k < SH_SLOTS; k++)
            ok = ok && w0[k] < SH_NMODES && str_eq(SH_MODES[w0[k]].name, EIGHT[k]);
        for (k = 0; k < SH_NMODES; k++)
            avail += (uint32_t)sh_wheel_avail(k);
        ok = ok && avail == SH_SLOTS;
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        sdown(B_GLO); sframe();
        skey((uint32_t)sh_slot_key(7));
        ok = ok && !sh.pick;
        sup(B_GLO); sup(B_HOME); sframe();
        sgo(fm);
        memcpy(w, w0, sizeof w);
        w[2] = (uint8_t)slot_of("LIBRARY");
        w[SH_SLOTS + 1u] = (uint8_t)en;
        ok = ok && sh_wheel_set(w) == 0u;
        sh_wheel_get(g);
        ok = ok && !memcmp(g, w0, SH_SLOTS) && g[SH_SLOTS + 1u] == en;
        w[SH_SLOTS + 2u] = (uint8_t)en;                 /* (a NOTE engine twice: refused) */
        ok = ok && sh_wheel_set(w) == 1u;
        sh_wheel_factory();
        sh_wheel_get(g);
        ok = ok && !memcmp(g, w0, sizeof g);
        bad += check("wheel: the white keys are the build's eight (no picker, a layout leaves them); the black keys as set",
                     ok);
        sgo(fm);
    }
    {   /* a lanes engine new to the wheel starts fresh: its lanes as a new project's, RHY EMPTY; the other set kept */
        uint8_t w0[SH_WHEEL_N], w[SH_WHEEL_N];
        uint32_t ln = note_of("LINES"), al = note_of("ALIVE"), k;
        sgo2(slot_of("FM6"), 0u);
        sh_wheel_get(w0);
        for (k = 0; k < 5u && w0[SH_SLOTS + k] != ln; k++)
            ;
        sh_eu_i[1][0][2] = 7; sh_eu_i[1][1][2] = 3; sh_lnote_i[1][2] = 60;
        sh_eu_i[0][0][2] = 9;
        if (sh_li == 1u) sh_sp[SP_RHY - SP_FIRST] = 4; else sh_lsp[1][0] = 4;
        memcpy(w, w0, sizeof w);
        w[SH_SLOTS + k] = (uint8_t)al;                  /* LINES off the wheel, then back */
        ok = k < 5u && sh_wheel_set(w) == 0u && sh_eu_i[1][0][2] == 7;
        ok = ok && sh_wheel_set(w0) == 0u && sh_eu_i[1][0][2] == 0 && sh_eu_i[1][1][2] == 0 && sh_lnote_i[1][2] == 0 &&
             sh_lanes_sp(1u, 0u) == 0 && sh_eu_i[0][0][2] == 9;
        sh_eu_i[0][0][2] = 0;
        bad += check("wheel: LINES new to the wheel starts with fresh lanes and RHY EMPTY; RUDIMENT's set kept", ok);
    }

    {   /* the setup page: HOME held, GLO tapped, let go: the pair by knobs (1 SOUND over the whole catalogue, 2 NOTE,
         * 3 preset, 4 behind); SOUND and NOTE are picked and put in front on leaving (nothing parks as they scroll),
         * the preset and MUTE put the pick there first; OCT- or HOME leaves on the pair picked */
        uint32_t fm = slot_of("FM6"), org = slot_of("SHOCK"), k, p0, alive = note_of("ALIVE");
        sgo2(fm, 0u);
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        ok = !sh_wheel_hint();
        sframes(2100u);                                 /* (the last line's turn: the way to this page) */
        ok = ok && sh_wheel_hint();
        shot("wheel_hint");
        stap(B_GLO);
        sup(B_HOME); sframe();
        ok = ok && sh.global == SHG_PAIR && !sh.wheel && sh.mode == fm;
        shot("pair");
        ok = ok && 110 + text_w(&AF_S, "BEAT OR LINE") <= 236;   /* (MUTE's "not a track" fits the screen) */
        for (k = 0; k < SH_NMODES && (sh_ppend.on ? sh_ppend.m : sh.mode) != org; k++)
            sturn(EN_K1, 1);
        ok = ok && org < SH_NMODES && sh_ppend.m == org && sh.mode == fm;   /* (picked, not yet in front) */
        for (k = 0; k < SH_NNOTES && sh_ppend.n != alive; k++)
            sturn(EN_K2, 1);
        ok = ok && sh_ppend.n == alive && sh.note == 0u && sh_track_note(2) != alive;   /* (nothing parked on LINE) */
        sturn(EN_K3, 1);                                /* the preset: the pick in front first */
        p0 = sh_trk()->preset;
        ok = ok && sh.mode == org && sh.note == alive;
        sturn(EN_K3, 1);
        ok = ok && sh_trk()->preset != p0;
        sturn(EN_K4, 1);                                /* (SHOCK + ALIVE is LINE's: MUTE) */
        ok = ok && sh.part == 2u && ((sh_tmute >> 2) & 1u) && trk[2].p[P_MUTE];
        sturn(EN_K4, -1);
        ok = ok && !sh_tmute && !trk[2].p[P_MUTE];
        stap(B_OCTDN);
        ok = ok && sh.global == SHG_NONE && sh.mode == org && sh.note == alive;
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        stap(B_GLO);
        sup(B_HOME); sframe();
        sturn(EN_K1, -1);
        ok = ok && sh.global == SHG_PAIR && sh.mode == org && sh_ppend.on;
        {   /* every notch redraws at once, not when the row's highlight fades */
            uint32_t g0 = sh_signature();
            sturn(EN_K1, -1);
            ok = ok && sh_signature() != g0;
            g0 = sh_signature();
            sturn(EN_K2, 1);
            ok = ok && sh_signature() != g0;
            g0 = sh_signature();
            sturn(EN_K2, 1);
            ok = ok && sh_signature() != g0;
            sturn(EN_K2, -1);                           /* (back to the pick the tests below expect) */
            sturn(EN_K2, -1);
            sturn(EN_K1, 1);
        }
        shome();
        sframe();
        ok = ok && sh.global == SHG_NONE && sh.mode != org && !sh_ppend.on;   /* (leaving: the pick in front) */
        bad += check("setup page: HOME + GLO opens it; knobs pick SOUND, NOTE (in front on leaving), preset, a track's "
                     "MUTE, each notch shown at once; OCT- and HOME leave", ok);
        {   /* a fast spin: knob 1 through 20 detents in one frame is one switch (the pair before is the one the spin
             * started on), under a held key; the key let go, no part holds a note */
            uint32_t m0, i, held = 0, nav;
            sdown(B_HOME); sframes(SH_TAP_MS + 100u);
            stap(B_GLO);
            sup(B_HOME); sframe();
            m0 = sh.mode;
            for (i = 0, nav = 0; i < SH_NMODES; i++)        /* (a spin that cannot come round to where it began: */
                nav += sh_wheel_avail(i) != 0;              /* fast turns count double) */
            fm1_in.notes |= 1u << 12; host_notes |= 1u << 12; keyboard_block(); sframe();
            sturn(EN_K1, (40 % nav) ? 20 : 19);
            ok = sh.global == SHG_PAIR && sh.mode == m0 && sh_ppend.m != m0;
            fm1_in.notes &= ~(1u << 12); host_notes &= ~(1u << 12); keyboard_block(); sframe();
            shome(); sframe();                     /* (leaving: one switch, to the pick) */
            ok = ok && sh.mode != m0 && sh.prev == m0;
            srender(400);
            for (i = 0; i < NTRK; i++)
                held += gated_notes(&trk[i]);
            ok = ok && !held;
            bad += check("setup page: knob 1 spun 20 detents in a frame switches once; the key let go, no note left on", ok);
        }
        sgo2(fm, 0u);
    }

    {   /* GLO + OCT+ / OCT-: their own cards (answers and leaving), the octave as it was */
        int8_t oct = song.octave;
        sdown(B_GLO); sframes(SH_TAP_MS + 100u);
        stap(B_OCTUP);
        ok = sh.help == B_OCTUP + 1u;
        sup(B_GLO); sframes(400);
        bad += check("GLO + OCT+: OCT+'s card, the octave as it was", ok && song.octave == oct);
        song.octave = oct;
    }

    {   /* REC through its second ring clears the pattern under ALIVE (and the note engines): what they recorded went
         * there, and a take silences them until it is cleared */
        uint32_t fm = slot_of("FM6"), k, left = 0;
        track_t *t;
        sgo2(fm, note_of("ALIVE"));
        t = sh_trk();
        step_clear(&t->step[3]);
        t->step[3].note[0] = 60; t->step[3].n = 1; t->step[3].time = ST_NOTE; t->step[3].vel = 90;
        sdown(B_REC); sframes(2u * SH_LONG_MS + 300u); sup(B_REC); sframe();
        for (k = 0; k < NSTEP; k++)
            left += t->step[k].n;
        bad += check("ALIVE: REC through its second ring clears what it recorded (not NOTHING TO CLEAR)",
                     !left && !memcmp(sh.toast, "PATTERN CLEARED", 15));
        sgo2(fm, 0u);
    }

    {   /* HARMONY: SCL twice; each NOTE engine's own TYPE VOIC LEAD VEL; a held key brings its third and fifth in the
         * scale, its release takes them; LEAD BELOW puts them under; CHORD plays its own chords, none added */
        uint32_t fm = slot_of("FM6"), kn = note_of("KEYS"), i, on = 0, below = 0, after = 0, ch = 0;
        int16_t keep[5], hk[4];
        track_t *t;
        sgo2(fm, kn);
        t = sh_trk();
        keep[0] = t->p[P_VOICE]; keep[1] = t->p[P_ROOT]; keep[2] = t->p[P_SCALE]; keep[3] = t->p[P_QUANT];
        keep[4] = t->p[P_TRANS];
        memcpy(hk, sh_hm[kn], sizeof hk);
        t->p[P_VOICE] = V_POLY; t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_QUANT] = 0; t->p[P_TRANS] = 0;
        stap(B_SCL);
        stap(B_SCL);
        ok = sh.global == SHG_HARM;
        sturn(EN_K1, 1);
        ok = ok && sh_hm[kn][0] > 0;
        shot("harmony");
        sh_hm[kn][0] = HM_TRIAD; sh_hm[kn][1] = HV_CLOSE; sh_hm[kn][2] = 0; sh_hm[kn][3] = 100;
        sframe();
        fm1_in.notes |= 1u << 7; host_notes |= 1u << 7; keyboard_block(); sframe(); srender(2);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].gate)
                on |= (t->v[i].note == 60u) << 0 | (t->v[i].note == 64u) << 1 | (t->v[i].note == 67u) << 2 |
                      (t->v[i].note != 60u && t->v[i].note != 64u && t->v[i].note != 67u) << 3;
        fm1_in.notes &= ~(1u << 7); host_notes &= ~(1u << 7); keyboard_block(); sframe(); srender(2);
        for (i = 0; i < NVOICE; i++)
            after += t->v[i].gate;
        sh_hm[kn][2] = 1;                               /* LEAD BELOW: E and G under C */
        sframe();
        fm1_in.notes |= 1u << 7; host_notes |= 1u << 7; keyboard_block(); sframe(); srender(2);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].gate)
                below |= (t->v[i].note == 52u) << 0 | (t->v[i].note == 55u) << 1;
        fm1_in.notes &= ~(1u << 7); host_notes &= ~(1u << 7); keyboard_block(); sframe(); srender(8);
        ok = ok && on == 7u && !after && below == 3u;
        sh_hm[note_of("CHORD")][0] = HM_TRIAD;
        sgo2(fm, note_of("CHORD"));
        sframe();
        ch = harm_cfg[sh.part % NTRK][0];
        ok = ok && !ch && sh_harm_why(sh.part) != 0;
        sh_hm[note_of("CHORD")][0] = 0;
        bad += check("HARMONY: SCL twice; a held C brings E and G in C major (BELOW: under it), its release ends them; "
                     "CHORD adds none", ok);
        sgo2(fm, kn);
        memcpy(sh_hm[kn], hk, sizeof hk);
        t->p[P_VOICE] = keep[0]; t->p[P_ROOT] = keep[1]; t->p[P_SCALE] = keep[2]; t->p[P_QUANT] = keep[3];
        t->p[P_TRANS] = keep[4];
        shome();
    }

    {   /* moving around (navigation review N1..N5): SELECT walks the shell's pages both ways in GLO's order, on PAIR
         * it turns the SOUND; PRESETS on PAIR walks sounds; PAIR lights HOME and OCT-; PROJECTS' question lights OCT+
         * breathing and OCT- mid; SCL again goes back to the page it left */
        static const uint8_t G[8] = {SHG_SCALE, SHG_MOVE, SHG_HARM, SHG_SYSTEM, SHG_SYSTEM2, SHG_PROJECT, SHG_ABOUT, SHG_LEGAL};
        static const uint8_t BT[4] = {B_HOME, B_OCTDN, B_OCTUP, B_GLO};
        uint8_t pos0[sizeof led_pos];
        uint32_t fm = slot_of("FM6"), k, m0, walk = 1, back = 1, lv[4];
        sgo2(fm, 0u);
        stap(B_SCL);
        for (k = 1; k <= 8u; k++) {                     /* SCALE, then on round to SCALE */
            sturn(EN_SELECT, 1);
            walk &= sh.global == G[k % 8u];
            if (G[k % 8u] == SHG_LEGAL)
                shot("legal");
        }
        for (k = 8; k > 0u; k--) {
            sturn(EN_SELECT, -1);
            back &= sh.global == G[(k - 1u) % 8u];
        }
        ok = walk && back;
        bad += check("moving: SELECT walks SCALE MOVE HARMONY SYSTEM SYSTEM 2 PROJECTS ABOUT LEGAL and round, and back", ok);
        shome();                                   /* GLO's taps: ... PROJECTS ABOUT LEGAL */
        for (k = 0; k < 5u; k++)
            stap(B_GLO);
        ok = sh.global == SHG_LEGAL;
        stap(B_GLO);
        bad += check("LEGAL: GLO's fifth tap, after ABOUT, then round to SYSTEM (GPL 5(d) notices)", ok && sh.global == SHG_SYSTEM);
        shome();
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        stap(B_GLO);
        sup(B_HOME); sframe();
        m0 = sh.mode;
        sturn(EN_SELECT, 1);                            /* (a SOUND picked, not yet in front) */
        ok = sh.global == SHG_PAIR && sh.mode == m0 && sh_ppend.on && sh_ppend.m != m0;
        sturn(EN_PRESET, 1);                            /* PRESETS: the pick in front, then its preset */
        ok = ok && sh.global == SHG_PAIR && sh.mode != m0 && sh.prev == m0 && !sh_ppend.on;
        memcpy(pos0, led_pos, sizeof pos0);             /* four buttons on bits of their own */
        memset(led_pos, 0xFF, sizeof led_pos);
        for (k = 0; k < 4u; k++)
            led_pos[panel.btn[BT[k]]] = (uint8_t)k;
#define SH_LV(k) ((fm1_led[0] >> (k)) & 1u ? 3u : (fm1_led_breath[0] >> (k)) & 1u ? 2u : (fm1_led_mid[0] >> (k)) & 1u ? 1u : 0u)
        song.octave = 0;
        shell_leds();
        uint32_t dip = SH_LV(0) == 0u && SH_LV(1) == 3u;         /* the detent's click: HOME, PAIR's button, dips */
        sframes(80u);
        shell_leds();
        for (k = 0; k < 4u; k++)
            lv[k] = SH_LV(k);
        ok = ok && lv[0] == 3u && lv[1] == 3u && lv[3] == 1u;
        bad += check("moving: on PAIR, SELECT turns the SOUND once and PRESETS the preset; HOME and OCT- lit, GLO mid", ok);
        bad += check("lights: a detent dips the page's button for a frame or two (a click), then it is back", dip);
        shome();
        sh.global = SHG_PROJECT;
        sh_proj_read();
        sh.pslot = 3;
        stap(B_OCTUP);                                  /* SAVE TO D? */
        shell_leds();
        for (k = 0; k < 4u; k++)
            lv[k] = SH_LV(k);
        ok = sh.pq != 0u && lv[2] == 2u && lv[1] == 1u;
        stap(B_PLAY);                                   /* (anything but OCT+: no) */
        if (song.playing)
            stap(B_PLAY);
#undef SH_LV
        memcpy(led_pos, pos0, sizeof pos0);
        ok = ok && !sh.pq;
        sh.pslot = 0;
        bad += check("moving: PROJECTS' question breathes OCT+ and holds OCT- mid, as SAVE's", ok);
        shome();
        stap(B_EDIT);
        k = sh.page[sh.mode];
        stap(B_SCL);
        ok = sh.global == SHG_SCALE;
        stap(B_SCL);
        ok = ok && sh.global == SHG_HARM;
        stap(B_SCL);
        ok = ok && sh.global == SHG_NONE && sh.page[sh.mode] == k;
        sh.global = SHG_SYSTEM2;
        stap(B_SCL);
        stap(B_SCL);
        stap(B_SCL);
        ok = ok && sh.global == SHG_SYSTEM2;
        stap(B_SCL);
        shome();
        ok = ok && sh.global == SHG_NONE && sh.page[sh.mode] == SHP_HOME;
        bad += check("moving: SCL walks SCALE, HARMONY, then back to the page it left (EDIT, SYSTEM 2); HOME from SCALE goes home", ok);
        sgo2(fm, 0u);
    }

    {   /* MIDI in the shell: ROUT is SEL from power-on, so a keyboard on channel 1 plays the pair in front whichever
         * part it is on; MIDI out stays on channel 1 over the same switches. SYSTEM 2's fourth knob is ROUT */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), k, i, parts = 0, played = 0, mo, pk = 0, ch1 = 1;
        ok = song.g[G_ROUTE] == ROUTE_SEL && SHG_SYS2_IDS[3] == G_ROUTE;
        usb.config = 1;
        for (k = 0; k < 5u; k++) {
            sgo2(k & 1u ? lz : fm, 0u);
            sframes(50);
            parts |= 1u << sh.part;
            midi_event(0x90, 0, 60, 100);
            played += midi_note_get(0, 60) == sh.part + 1u;   /* (the part channel 1's note went to) */
            midi_event(0x80, 0, 60, 0);
            mo = mo_w;
            skey(12);                                   /* a key of the FM-1's own */
            sframes(20);
            for (i = mo; i < mo_w; i++, pk++)
                ch1 &= ((midi_out_q[i % MQ] >> 8) & 0x0Fu) == 0u;
        }
        usb.config = 0;
        ok = ok && played == 5u && (parts & (parts - 1u)) && pk >= 10u && ch1;
        bad += check("MIDI: ROUT SEL by default; channel 1 plays the pair in front over 5 switches across parts; "
                     "the keys go out on channel 1 throughout", ok);
        sgo(fm);
    }

    {   /* HARP (picked onto RUDIMENT's black key, then put back): F3 held picks F, a run up the plate's strings strums
         * F A C climbing; every string let go, nothing left on */
        uint32_t hn = note_of("HARP"), k, q, na, ok2 = 1, prev = 0, pcs = 0;
        track_t *t;
        sframes(400);
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        sdown(B_GLO); sframe();
        skey((uint32_t)sh_note_key(4));
        for (k = 0; k < 2u * SH_NNOTES && sh.pickcur != hn; k++) sturn(EN_SELECT, 1);
        sup(B_GLO); sframe();
        sup(B_HOME); sframe();
        t = sh_trk();
        ok = sh.note == hn && sh_wnoteslot[4] == hn && sh_kind() == SHK_NE && shell_kb_skip == SH_ALL_KEYS;
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;            /* (C MAJOR, whichever PLAY part it is on) */
        t->p[P_VOICE] = V_POLY;
        srender(4);
        memset(t->v, 0, sizeof t->v);
        fm1_in.notes |= 1u << 0; keyboard_block(); srender(2);   /* F3: the root */
        for (k = 12; k < 15u; k++) {                    /* three strings up the plate */
            uint32_t newest = 0, age = 0;
            fm1_in.notes |= 1u << k; keyboard_block(); srender(2);
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].gate && t->v[q].age >= age) { age = t->v[q].age; newest = t->v[q].note; }
            ok2 &= newest > prev;
            prev = newest;
            pcs |= 1u << (newest % 12u);
        }
        ok = ok && ok2 && pcs == ((1u << 5) | (1u << 9) | (1u << 0));   /* F A C */
        fm1_in.notes = 0; keyboard_block(); srender(40);
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        bad += check("HARP (via the picker): F3 picks F, a run up three strings strums F A C climbing; let go, none left on",
                     ok && !na);
        sgo(slot_of("FM6"));
        sh_wnoteslot[4] = (uint8_t)note_of("RUDIMENT");   /* (the default stash again) */
    }

    {   /* ROTOR (an INSERT, databurn's rotary.c): a steady sine through it swirls at FAST (the level swings as the horn
         * turns), stands still at STOP (once the rotors have stopped), and MIX 0 leaves the part bit for bit */
        track_t *t = &trk[3];
        static int32_t b[CTL];
        int16_t keep[5] = {t->p[P_ITYPE], t->p[P_IA], t->p[P_IB], t->p[P_IC], t->p[P_IMIX]};
        uint32_t blk, i, ph = 0, run;
        int32_t lo[2], hi[2];
        for (run = 0; run < 2u; run++) {               /* FAST, then STOP */
            t->p[P_ITYPE] = IT_ROTARY; t->p[P_IA] = run ? ROT_STOP : ROT_FAST; t->p[P_IB] = 0; t->p[P_IC] = 127;
            t->p[P_IMIX] = 127;
            lo[run] = 0x7FFFFFFF; hi[run] = 0;
            {
                int32_t pk = 0;
                for (blk = 0; blk < (FS * 8u) / CTL; blk++) {   /* 8 s: the rotors up to speed, or stopped */
                    for (i = 0; i < CTL; i++, ph += 0x02800000u) b[i] = osc_sine(ph) / 2;
                    track_insert(t, b, CTL);
                    for (i = 0; i < CTL; i++) pk = b[i] > pk ? b[i] : -b[i] > pk ? -b[i] : pk;
                    if ((blk & 15u) == 15u) {           /* (a peak each 512 samples: several cycles of the sine) */
                        if (blk > (FS * 6u) / CTL) {    /* (the last 2 s) */
                            lo[run] = pk < lo[run] ? pk : lo[run];
                            hi[run] = pk > hi[run] ? pk : hi[run];
                        }
                        pk = 0;
                    }
                }
            }
        }
        ok = hi[0] - lo[0] > (hi[0] >> 3) && hi[1] - lo[1] < (hi[1] >> 4);   /* swirling / still */
        t->p[P_IMIX] = 0;
        for (blk = 0; blk < 200u; blk++) {              /* (faded out) */
            for (i = 0; i < CTL; i++) b[i] = 1000;
            track_insert(t, b, CTL);
        }
        for (i = 0; i < CTL; i++) b[i] = (int32_t)i * 37 - 500;
        track_insert(t, b, CTL);
        for (i = 0; i < CTL; i++) ok = ok && b[i] == (int32_t)i * 37 - 500;
        t->p[P_ITYPE] = keep[0]; t->p[P_IA] = keep[1]; t->p[P_IB] = keep[2]; t->p[P_IC] = keep[3]; t->p[P_IMIX] = keep[4];
        bad += check("ROTOR insert: a sine swirls at FAST, stands still at STOP; MIX 0: the part bit for bit", ok);
    }

    {   /* RESON and VOWEL (INSERTs, databurn's resonator.c and vowel.c). RESON rings at the part's held note: noise in
         * comes out 20 dB stronger at A4 than a tritone away; a full-scale sine on the note comes back past a voice's
         * full scale (the insert's path is int32). VOWEL on noise: IY puts its second formant high (2290 Hz), UW low
         * (870): the 2.1 .. 2.5 kHz band against 750 .. 1000 Hz moves by more than 10 dB. MIX 0: the part bit for bit */
        track_t *t = &trk[3];
        static int32_t b[CTL], y[4096];
        int16_t keep[5] = {t->p[P_ITYPE], t->p[P_IA], t->p[P_IB], t->p[P_IC], t->p[P_IMIX]};
        voice_t vk = t->v[0];
        uint32_t blk, i, rnd = 1, ph = 0, ok_r, ok_v, ok_m = 1;
        int32_t pk = 0;
        double r_on, r_off, iy, uw;
#define NOISE() ((int32_t)((rnd = rnd * 1664525u + 1013904223u) >> 16) - 32768) / 4
#define RUN(src, blocks) do { for (blk = 0; blk < (blocks); blk++) { for (i = 0; i < CTL; i++) b[i] = (src);        \
                     track_insert(t, b, CTL); if (blk >= (blocks) - 4096u / CTL)                                  \
                     memcpy(y + (blk - ((blocks) - 4096u / CTL)) * CTL, b, sizeof b); } } while (0)
        memset(&t->v[0], 0, sizeof t->v[0]);
        t->v[0].active = t->v[0].gate = 1;
        t->v[0].pitch16 = t->v[0].pitch_cur = 69 * 16;
        t->v[0].age = 1;
        t->p[P_ITYPE] = IT_RESON; t->p[P_IA] = 100; t->p[P_IB] = 0; t->p[P_IC] = 64; t->p[P_IMIX] = 127;
        RUN(NOISE(), FS / CTL);
        r_on = st_goertzel(y, 4096, 440.0);
        r_off = st_goertzel(y, 4096, 311.1);
        RUN(osc_sine(ph += 0x028DD8F6u) * 2, FS / CTL);   /* (a 440 Hz sine at a voice's full scale) */
        for (i = 0; i < 4096u; i++)
            pk = y[i] > pk ? y[i] : -y[i] > pk ? -y[i] : pk;
        ok_r = 10.0 * log10(r_on / r_off) > 20.0 && pk > 70000;
        t->p[P_IMIX] = 0;
        RUN(1000, 200u);
        t->p[P_ITYPE] = IT_VOWEL; t->p[P_IA] = 0; t->p[P_IB] = 0; t->p[P_IC] = 64; t->p[P_IMIX] = 127;
        RUN(NOISE(), FS / CTL);
        iy = st_band(y, 2100.0, 2500.0) / st_band(y, 750.0, 1000.0);
        t->p[P_IA] = 127;
        RUN(NOISE(), FS / CTL);
        uw = st_band(y, 2100.0, 2500.0) / st_band(y, 750.0, 1000.0);
        ok_v = 10.0 * log10(iy / uw) > 10.0;
        t->p[P_IMIX] = 0;
        RUN(1000, 200u);
        for (i = 0; i < CTL; i++) b[i] = (int32_t)i * 37 - 500;
        track_insert(t, b, CTL);
        for (i = 0; i < CTL; i++) ok_m = ok_m && b[i] == (int32_t)i * 37 - 500;
        printf("  (RESON: A4 against a tritone off %+.1f dB, a sine on it peaks %d; VOWEL IY against UW %+.1f dB)\n",
               10.0 * log10(r_on / r_off), (int)pk, 10.0 * log10(iy / uw));
        bad += check("RESON insert: noise rings at the held note (20 dB over a tritone off), its wet past int16", ok_r);
        bad += check("VOWEL insert: IY and UW move noise's formant balance by over 10 dB; MIX 0: bit for bit", ok_v && ok_m);
        {   /* FILTER: LP at a low CUT darkens noise (the 4..8 kHz band against 200..400 Hz drops by over 20 dB from CUT
             * 127); COMB with positive FDBK at A4 rings 440 Hz and its octave over a tritone between */
            double hi, lo;
            t->p[P_ITYPE] = IT_FILTER; t->p[P_IA] = 127; t->p[P_IB] = 0; t->p[P_IC] = 0; t->p[P_IMIX] = 127;
            memset(ins_ext[3], 0, sizeof ins_ext[3]);
            RUN(NOISE(), FS / CTL);
            hi = st_band(y, 4000.0, 8000.0) / st_band(y, 200.0, 400.0);
            t->p[P_IA] = 50;                                /* (about 400 Hz) */
            RUN(NOISE(), FS / CTL);
            lo = st_band(y, 4000.0, 8000.0) / st_band(y, 200.0, 400.0);
            ok_r = 10.0 * log10(hi / lo) > 20.0;
            t->p[P_IMIX] = 0;
            RUN(1000, 200u);
            t->p[P_ITYPE] = IT_COMB; t->p[P_IA] = 57; t->p[P_IB] = 110; t->p[P_IC] = 0; t->p[P_IMIX] = 127;
            RUN(NOISE(), FS / CTL);
            r_on = st_goertzel(y, 4096, 440.0) + st_goertzel(y, 4096, 880.0);
            r_off = st_goertzel(y, 4096, 622.3) * 2.0;
            ok_v = 10.0 * log10(r_on / r_off) > 10.0;
            printf("  (FILTER LP: CUT 127 against 50 %+.1f dB in 4..8 kHz; COMB A4: its harmonics %+.1f dB over between)\n",
                   10.0 * log10(hi / lo), 10.0 * log10(r_on / r_off));
            bad += check("FILTER insert: LP at a low CUT darkens noise by over 20 dB; COMB rings its pitch's harmonics", ok_r && ok_v);
            t->p[P_IMIX] = 0;
            RUN(1000, 200u);
        }
        t->v[0] = vk;
        t->p[P_ITYPE] = keep[0]; t->p[P_IA] = keep[1]; t->p[P_IB] = keep[2]; t->p[P_IC] = keep[3]; t->p[P_IMIX] = keep[4];
#undef NOISE
#undef RUN
    }

    {   /* FILTER and COMB on the INSERT page: TYPE turned onto them starts at their defaults (a CUT, a pitch); INSERT 2
         * holds MIX and their DRV KTRK ENV, the part's own; under any other type INSERT 2 is MIX alone */
        uint8_t ids[4];
        int glob;
        track_t *t;
        sgo(slot_of("FM6"));
        t = sh_trk();
        t->p[P_ITYPE] = IT_VOWEL; t->p[P_IA] = 10; t->p[P_IB] = 10; t->p[P_IC] = 10;
        sh.page[sh.mode] = SHP_INS; sframe();
        sturn(EN_K1, 1);
        ok = t->p[P_ITYPE] == IT_FILTER && t->p[P_IA] == 80 && t->p[P_IB] == 32 && t->p[P_IC] == 0;
        sturn(EN_K1, 1);
        ok = ok && t->p[P_ITYPE] == IT_COMB && t->p[P_IA] == 57 && t->p[P_IB] == 100 && t->p[P_IC] == 32;
        sh.page[sh.mode] = SHP_INS2; sframe();
        sh_page_ids(ids, &glob);
        ok = ok && ids[0] == P_IMIX && ids[1] == SP_FDRV && ids[2] == SP_FKTRK && ids[3] == SP_FENV;
        sturn(EN_K4, -10);                              /* ENV down: the part's own */
        ok = ok && ins_ext[sh.part][IX_ENV] < 0;
        {   /* kept in a saved sound (four spare bytes past the values), and a record without them loads the defaults */
            uint32_t k = UP_SLOTS - 3u;
            int rc;
            int16_t env = ins_ext[sh.part][IX_ENV];
            up_rec_t keep;
            memcpy(&keep, up_rec(k), sizeof keep);
            ins_ext[sh.part][IX_DRV] = 33;
            rc = up_store(k, "FLT");
            memset(ins_ext[sh.part], 0, sizeof ins_ext[0]);
            ok = ok && (rc == 0 || rc == 3) && !up_load(k) && ins_ext[sh.part][IX_DRV] == 33 &&
                 ins_ext[sh.part][IX_ENV] == env && ins_ext[sh.part][IX_KTRK] == 0;
            up_rec(k)->packed[UP_IX_AT] = 0;
            ok = ok && !up_load(k) && ins_ext[sh.part][IX_DRV] == 0 && ins_ext[sh.part][IX_ENV] == 0;
            memcpy(up_rec(k), &keep, sizeof keep);
            ins_ext[sh.part][IX_ENV] = env;
            t = sh_trk();
        }
        sframes(SH_TOAST_MS + 200u);
        sh.page[sh.mode] = SHP_INS; sframe();
        sturn(EN_K1, -1);                               /* FILTER again */
        t->p[P_ITYPE] = IT_RESON; sh.page[sh.mode] = SHP_INS2; sframe();
        sh_page_ids(ids, &glob);
        ok = ok && ids[0] == P_IMIX && ids[1] == SH_NONE;
        memset(ins_ext[sh.part], 0, sizeof ins_ext[0]);
        t->p[P_ITYPE] = 0;
        shome(); sframe();
        bad += check("INSERT: FILTER and COMB start at their defaults; INSERT 2 holds their DRV KTRK ENV, MIX alone otherwise", ok);
    }

    {   /* REC arms the part the pair in front plays on (ping-pong puts every other pair on part 2): a take played
         * there lands in that part's pattern */
        uint32_t k, any = 0;
        track_t *t;
        if (sh.part == 0u)
            sgo(slot_of("LIZARD"));
        if (sh.part == 0u)
            sgo(slot_of("FM6"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = 0;
        stap(B_REC);
        ok = sh.part != 0u && song.rec == 1u << sh.part;
        transport_req = 1;
        srender(2);
        skey_sung(12);
        srender(64);
        for (k = 0; k < NSTEP; k++)
            any |= t->step[k].n;
        transport_req = 2;
        srender(2);
        stap(B_REC);
        bad += check("REC: arms the pair's own part (here part 2), a key played lands in its pattern; again: off",
                     ok && any && song.rec == 0u);
    }

    {   /* MOVE: every bar the scale moves, every part with it: 5TH C G D, BACK 2 home to C, then G again, the
         * downbeat already in the new scale; MODE walks MAJ MIX DOR; ALT between the scale set by hand and ALT; a
         * stop puts each part's own scale back (a part in another key keeps it); a run from the top goes home; ROOT
         * turned by hand while moved keeps home's SCALE */
        uint32_t k, ok = 1, b, down = 1, own = 1, i;
        uint8_t got[5];
        track_t *t;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        for (k = 0; k < NTRK; k++) { trk[k].p[P_ROOT] = 0; trk[k].p[P_SCALE] = 1; }
        sh_sp[SP_MEVERY - SP_FIRST] = 1; sh_sp[SP_MHOW - SP_FIRST] = 1; sh_sp[SP_MBACK - SP_FIRST] = 2;
        transport_req = 1; srender(2); sframe();
        for (b = 1; b <= 4u; b++) {
            for (k = 0; k < 40000u && sh_move_bar() < b; k++)
                srender(1);
            down &= t->p[P_ROOT] == (b == 1u ? 7 : b == 2u ? 2 : b == 3u ? 0 : 7);   /* (at the bar line, no frame yet) */
            sframe();
            got[b] = (uint8_t)t->p[P_ROOT];
            ok = ok && trk[0].p[P_ROOT] == trk[3].p[P_ROOT];
        }
        ok = ok && got[1] == 7u && got[2] == 2u && got[3] == 0u && got[4] == 7u && down;
        printf("  (MOVE 5TH roots %u %u %u %u)\n", got[1], got[2], got[3], got[4]);
        transport_req = 3; srender(2); sframe();        /* a run from the top (GLO + PLAY): home */
        ok = ok && t->p[P_ROOT] == 0 && sh_mv.n == 0;
        transport_req = 2; srender(2); sframe();
        ok = ok && t->p[P_ROOT] == 0 && trk[2].p[P_ROOT] == 0;
        trk[2].p[P_ROOT] = 9; trk[2].p[P_SCALE] = 2;    /* a part in another key: a stop with no move leaves it */
        transport_req = 1; srender(2); sframe();
        transport_req = 2; srender(2); sframe();
        own = trk[2].p[P_ROOT] == 9 && trk[2].p[P_SCALE] == 2;
        transport_req = 1; srender(2); sframe();        /* .. and after a move, its own comes back */
        for (k = 0; k < 40000u && sh_move_bar() < 1u; k++)
            srender(1);
        sframe();
        transport_req = 2; srender(2); sframe();
        own = own && trk[2].p[P_ROOT] == 9 && trk[2].p[P_SCALE] == 2 && t->p[P_ROOT] == 0;
        for (i = 0; i < NTRK; i++) { trk[i].p[P_ROOT] = 0; trk[i].p[P_SCALE] = 1; }
        ok = ok && own;
        sh_sp[SP_MHOW - SP_FIRST] = 0; sh_sp[SP_MBACK - SP_FIRST] = 0;   /* MODE: MAJ, MIX, DOR */
        transport_req = 1; srender(2); sframe();
        for (b = 1; b <= 2u; b++) {
            for (k = 0; k < 40000u && sh_move_bar() < b; k++)
                srender(1);
            sframe();
            got[b] = (uint8_t)t->p[P_SCALE];
        }
        ok = ok && got[1] == 4u && got[2] == 3u;
        sh.global = SHG_SCALE;                          /* ROOT turned by hand while moved: home C# MAJ, not C# DOR */
        sh_page_ids(got, (int *)&i);
        sturn(EN_K1, 1);
        transport_req = 2; srender(2); sframe();
        ok = ok && t->p[P_ROOT] == 1 && t->p[P_SCALE] == 1;
        sh.global = SHG_NONE;
        for (i = 0; i < NTRK; i++) { trk[i].p[P_ROOT] = 0; trk[i].p[P_SCALE] = 1; }
        sh_sp[SP_MHOW - SP_FIRST] = 2;                  /* ALT: MAJ, MIN (ALT's default), MAJ */
        transport_req = 1; srender(2); sframe();
        for (b = 1; b <= 2u; b++) {
            for (k = 0; k < 40000u && sh_move_bar() < b; k++)
                srender(1);
            sframe();
            got[b] = (uint8_t)t->p[P_SCALE];
        }
        ok = ok && got[1] == 2u && got[2] == 1u;
        transport_req = 2; srender(2); sframe();
        sh_sp[SP_MEVERY - SP_FIRST] = 0; sh_sp[SP_MHOW - SP_FIRST] = 0;
        bad += check("MOVE: every bar the scale moves for every part, the downbeat in it (5TH, BACK, MODE, ALT); a stop "
                     "or a run from the top puts each part's own back; ROOT by hand keeps home's SCALE", ok);
    }
    {   /* QNT NEAR and DROP (SQRL-1): the keys to the nearest note of the scale, a tie toward the last note; DROP
         * silences a key off the scale and rests a pattern note off it, the steps unchanged */
        track_t *t;
        uint32_t k, v, ok, n61 = 0, n60 = 0;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_TRANS] = 0;   /* C MAJOR */
        t->p[P_QUANT] = QN_NEAR;
        memset(qn_last, 0, sizeof qn_last);
        memset(qn_klast, 0, sizeof qn_klast);
        ok = kb_pitch(t, 8, 0) == 60u;                  /* C#4, nothing pressed before: down to C */
        ok = ok && kb_pitch(t, 11, 0) == 64u;            /* E: on the scale */
        qn_klast[trk_index(t)] = 64;                    /* (as a press of E leaves it) */
        ok = ok && kb_pitch(t, 8, 0) == 62u;            /* C# after E (above): up, toward it, to D */
        ok = ok && kb_pitch(t, 8, 0) == 62u;            /* again: a re-map (the arp's) moves nothing */
        qn_klast[trk_index(t)] = 55;
        ok = ok && kb_pitch(t, 8, 0) == 60u;            /* after a G below: down to C */
        qn_klast[trk_index(t)] = 0;
        fm1_in.notes |= 1u << 11; host_notes |= 1u << 11; keyboard_block();   /* a real press of E */
        fm1_in.notes &= ~(1u << 11); keyboard_block();
        ok = ok && qn_klast[trk_index(t)] == 64u;
        t->p[P_QUANT] = QN_DROP;
        ok = ok && kb_pitch(t, 8, 0) == KB_SILENT && kb_pitch(t, 9, 0) == 62u;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        for (k = 0; k < 16u; k++) {                     /* every step C#4: under DROP a rest, under NEAR C */
            t->step[k].note[0] = 61; t->step[k].time = ST_NOTE; t->step[k].n = 1;
        }
        memset(t->v, 0, sizeof t->v);
        transport_req = 1; srender(2);
        for (k = 0; k < 200u; k++) {
            srender(1);
            for (v = 0; v < NVOICE; v++)
                n61 |= t->v[v].active && t->v[v].note == 61u;
        }
        t->p[P_QUANT] = QN_NEAR;
        memset(qn_last, 0, sizeof qn_last);
        for (k = 0; k < 200u; k++) {
            srender(1);
            for (v = 0; v < NVOICE; v++) {
                n61 |= t->v[v].active && t->v[v].note == 61u;
                n60 |= t->v[v].active && t->v[v].note == 60u;
            }
        }
        transport_req = 2; srender(2);
        ok = ok && !n61 && n60 && t->step[3].note[0] == 61u;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        memset(t->v, 0, sizeof t->v);
        t->p[P_QUANT] = 0;
        sframe();
        bad += check("QNT NEAR and DROP: the nearest note (a tie toward the last), an off-scale key silent and a "
                     "pattern note off the scale a rest, the steps kept", ok);
    }
    {   /* DOUBLE: SEQ held + OCT+ copies the pattern in front after itself (LEN 16 > 32 > 64, then no room), its knob
         * moves too, playing and armed; the octave stays; REC held to its first ring undoes the last; not in a first
         * take, not on a lanes engine */
        track_t *t;
        uint32_t k, ok = 1;
        int8_t oct = song.octave;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        t->step[0].note[0] = 60; t->step[0].time = ST_NOTE; t->step[0].n = 1;
        t->step[4].note[0] = 67; t->step[4].time = ST_NOTE; t->step[4].n = 1;
        (void)motion_set_event(t, 3, P_DIST, 50);
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(4); sframe();
        sh_take.first = 0;
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        ok = t->p[P_SLEN] == 32 && t->step[16].n == 1u && t->step[16].note[0] == 60u && t->step[20].note[0] == 67u &&
             motion_count(t) == 2u && str_eq(sh.toast, "DOUBLE: 32 STEPS") && song.octave == oct && song.playing;
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        ok = ok && t->p[P_SLEN] == 64 && t->step[48].note[0] == 60u && t->step[52].note[0] == 67u && motion_count(t) == 4u;
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        ok = ok && t->p[P_SLEN] == 64 && str_eq(sh.toast, "64 STEPS: NO ROOM");
        sdown(B_OCTDN); sframe(); sup(B_OCTDN); sframe();   /* a tap of OCT-: HALF, the steps past it kept */
        ok = ok && song.octave == oct && t->p[P_SLEN] == 32 && str_eq(sh.toast, "HALF: 32 STEPS") &&
             t->step[48].note[0] == 60u;
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();   /* DOUBLE straight after: what was there, not a copy */
        ok = ok && t->p[P_SLEN] == 64 && str_eq(sh.toast, "DOUBLE: 64 STEPS") && t->step[48].note[0] == 60u &&
             motion_count(t) == 4u;
        sup(B_SEQ); sframe();
        ok = ok && sh.page[sh.mode] != SHP_SEQ;          /* (SEQ held as a modifier: its tap's page not taken) */
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* undo: the last DOUBLE */
        ok = ok && t->p[P_SLEN] == 32 && t->step[16].note[0] == 60u && str_eq(sh.toast, "DOUBLE UNDONE");
        sh_take.first = 1;                              /* a first take: LEN still open */
        sh_take.fpart = sh.part;
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        ok = ok && t->p[P_SLEN] == 32 && str_eq(sh.toast, "DOUBLE: TAP REC TO CLOSE THE TAKE");
        sh_take.first = 0;
        {   /* the take count: a DOUBLE between takes is no take, its undo leaves the count; one inside a take is
             * undone with it, counted once */
            uint8_t n0;
            sh_take.n = 2; sh_take.live = 0;
            n0 = sh_take.n;
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
            sup(B_SEQ); sframe();
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
            ok = ok && sh_take.n == n0 && t->p[P_SLEN] == 32 && str_eq(sh.toast, "DOUBLE UNDONE");
            fm1_irq_off(); sh_take_note(t); fm1_irq_on();   /* a take under way (its first note kept the steps) */
            n0 = sh_take.n;
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
            sup(B_SEQ); sframe();
            fm1_irq_off(); sh_take_note(t); fm1_irq_on();   /* (the same take: no second count) */
            ok = ok && sh_take.n == n0 && t->p[P_SLEN] == 64;
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
            ok = ok && t->p[P_SLEN] == 32 && str_eq(sh.toast, "TAKE UNDONE");
        }
        song.rec = 0;
        transport_req = 2; srender(2);
        sgo2(slot_of("FM6"), note_of("ENIGMA"));       /* a note engine: its take doubles, nothing without one */
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        ok = ok && t->p[P_SLEN] == 16 && str_eq(sh.toast, "DOUBLE: RECORD A TAKE FIRST");
        t->step[1].note[0] = 62; t->step[1].time = ST_NOTE; t->step[1].n = 1;
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        ok = ok && t->p[P_SLEN] == 32 && t->step[17].note[0] == 62u;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sgo2(slot_of("DRUMS"), note_of("RUDIMENT"));    /* the lanes double too */
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        grid_hit(t, 0, 0, 1u);
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        ok = ok && str_eq(sh.toast, "DOUBLE: 32 STEPS") && t->p[P_SLEN] == 32 && (step_lanes(&t->step[16]) & 1u);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        sframe();
        bad += check("DOUBLE: SEQ held + OCT+ copies the pattern and its knob moves (16, 32, 64, no room) while it plays "
                     "and records, the octave kept, REC held undoes it (the take count kept); a tap of OCT- HALF, DOUBLE after it what "
                     "was there; a note engine's take; not in a first take; the lanes too", ok);
    }
    {   /* after a take has ended, DOUBLE keeps its own undo; a project loaded over an open first take keeps its LEN;
         * a pair left while a knob move plays keeps its own value, not the moved one */
        track_t *t;
        uint32_t k, ok, n2 = 0;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        motion_clear(t);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        t->step[0].note[0] = 60; t->step[0].time = ST_NOTE; t->step[0].n = 1;
        t->step[8].note[0] = 62; t->step[8].time = ST_NOTE; t->step[8].n = 1;
        song.rec = 0;
        transport_req = 1; srender(2); sframe();
        stap(B_REC); sframe();                          /* a take: its note kept the steps */
        fm1_irq_off(); sh_take_note(t); fm1_irq_on();
        stap(B_REC); sframe(); sframe();                /* .. and its end */
        transport_req = 2; srender(2); sframe();
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        for (k = 0; k < 16u; k++)
            n2 += t->step[k].n != 0u;
        ok = str_eq(sh.toast, "DOUBLE UNDONE") && t->p[P_SLEN] == 16 && n2 == 2u;
        ok = ok && project_save(2) == 0;                /* a project of LEN 16, then a first take open over another */
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        transport_req = 1; srender(2); sframe();
        stap(B_REC); sframe(); sframes(40u);
        ok = ok && sh_take.first;
        project_load(2);
        sframe(); sframe();
        t = sh_trk();
        ok = ok && t->p[P_SLEN] == 16 && t->step[8].n == 1u && !sh_take.first;
        song.rec = 0;
        transport_req = 2; srender(2); sframe();
        t->p[P_DIST] = 0;                               /* a knob move plays, the pair is left and come back to */
        (void)motion_set_event(t, 1, P_DIST, 100);
        transport_req = 1; srender(2);
        for (k = 0; k < 400u && t->seq_idx < 3u; k++)
            srender(1);
        ok = ok && t->p[P_DIST] == 100;
        sdown(B_SAVE); sframes(SH_LONG_MS + 100u); sup(B_SAVE); sframe();   /* MOMENTARY let go: the move's value stays */
        ok = ok && t->p[P_DIST] == 100;
        {   /* a pair loaded onto the part while the move plays: its own value, and its snapshot keeps it */
            uint32_t lz = slot_of("LIZARD"), kn = note_of("KEYS"), pt = sh.part;
            sh_snap_of(lz)->p[P_DIST] = 55;
            if (sh_snap_of(lz)->valid && sh_nrec[sh_rec_of(lz, kn)].valid) {
                (void)sh_load_pair(lz, kn, pt, NPART);
                ok = ok && t->p[P_DIST] == 55 && sh_snap_of(lz)->p[P_DIST] == 55;
                (void)sh_load_pair(slot_of("FM6"), kn, pt, NPART);
            }
        }
        sgo2(slot_of("FM6"), note_of("CHORD"));
        srender(4); sframe();
        transport_req = 2; srender(2); sframe();
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        motion_clear(t);
        ok = ok && t->p[P_DIST] == 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sframe();
        bad += check("takes: DOUBLE after a take keeps the take; a project loaded over an open first take keeps its "
                     "LEN; a pair left mid knob move keeps its own value, MOMENTARY and a pair loaded onto it too", ok);
    }
    {   /* recording: REC + PLAY arms the part, counts in one bar with the click, then records; the first take on an
         * empty pattern sets LEN to the bars it ran (2.4 bars: 2, 2.6: 3); REC held through the first ring and let go
         * undoes the last take (steps and LEN), once; through the second it clears */
        static step_t keep[NSTEP];
        uint32_t k, w, ok1, ok2, ok3, ok4, ok5, spb, acc = 0;
        int32_t len1, len2;
        track_t *t;
#define SAV(ms) do { uint32_t f_; for (f_ = 0; f_ < (ms) / 16u; f_++) { acc += 16u * FS / 1000u;            \
                     while (acc >= HALF_FRAMES) { srender(1); acc -= HALF_FRAMES; } sframe(); } } while (0)
#define KEY_ON(k) do { fm1_in.notes |= 1u << (k); host_notes |= 1u << (k); keyboard_block(); } while (0)
#define KEY_OFF(k) do { fm1_in.notes &= ~(1u << (k)); keyboard_block(); } while (0)
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        t->p[P_SLEN] = 16;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = 0;
        transport_req = 2; srender(2); sframe();
        spb = sh_bar_steps(t);
        sdown(B_REC); sframe(); sdown(B_PLAY); sframe(); sup(B_PLAY); sframe(); sup(B_REC); sframe();
        srender(1);
        ok1 = song.rec == 1u << sh.part && cin_left == 4u && !song.playing && click_wanted() && !sh.ring;
        for (w = 0; w < 300u && !song.playing; w++)     /* (a bar at the tempo) */
            SAV(16u);
        ok1 = ok1 && song.playing && rec_on(t) && t->p[P_SLEN] == NSTEP;
        KEY_ON(12u); SAV(64u); KEY_OFF(12u);            /* a note on step 1 */
        for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 12u / 5u); w++)   /* to 2.4 bars */
            SAV(16u);
        stap(B_PLAY); srender(2); sframe(); sframe();
        len1 = t->p[P_SLEN];
        ok2 = len1 == (int32_t)(2u * spb) && sh_take.n == 1u;
        for (k = (uint32_t)len1; k < NSTEP; k++)
            ok2 = ok2 && !t->step[k].n;
        memcpy(keep, t->step, sizeof keep);
        transport_req = 1; srender(2); SAV(32u);        /* take 2, still armed: over it */
        KEY_ON(16u); SAV(64u); KEY_OFF(16u);
        SAV(500u);
        stap(B_PLAY); srender(2); sframe(); sframe();
        ok3 = sh_take.n == 2u && memcmp(keep, t->step, sizeof keep) && t->p[P_SLEN] == len1;
        sdown(B_REC); sframes(SH_LONG_MS + 100u);
        ok3 = ok3 && sh.ring == B_REC + 1u && sh.ring2;
        sup(B_REC); sframe();
        ok3 = ok3 && !memcmp(keep, t->step, sizeof keep) && t->p[P_SLEN] == len1 && song.rec == 1u << sh.part;
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* again: nothing kept, nothing changes */
        ok3 = ok3 && !memcmp(keep, t->step, sizeof keep);
        for (k = 0; k < NSTEP; k++)                     /* 2.6 bars: 3 */
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        transport_req = 1; srender(2); SAV(32u);
        KEY_ON(12u); SAV(64u); KEY_OFF(12u);
        for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 13u / 5u); w++)
            SAV(16u);
        stap(B_PLAY); srender(2); sframe(); sframe();
        len2 = t->p[P_SLEN];
        ok4 = len2 == (int32_t)(3u * spb);
        {   /* a note played past the bar a take rounds to: a bar more, nothing lost (2.3 bars, a note at 2.2: 3) */
            sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();
            t->p[P_SLEN] = 16;
            transport_req = 1; srender(2); SAV(32u);
            for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 11u / 5u); w++)
                SAV(16u);
            KEY_ON(14u); SAV(64u); KEY_OFF(14u);
            for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 23u / 10u); w++)
                SAV(16u);
            stap(B_PLAY); srender(2); sframe(); sframe();
            printf("  (a first take of 2.3 bars with a note at 2.2: LEN %d)\n", (int)t->p[P_SLEN]);
            ok4 = ok4 && t->p[P_SLEN] == (int32_t)(3u * spb) && sh_pat_any(t);
        }
        sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();   /* both rings: cleared */
        for (ok5 = 1, k = 0; k < NSTEP; k++)
            ok5 = ok5 && !t->step[k].n;
        ok5 = ok5 && song.rec == 1u << sh.part;
        {   /* a first take armed while the transport runs (a pair behind keeps it running): LEN in bars too */
            int32_t len3;
            sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();   /* cleared */
            t->p[P_SLEN] = 16;
            song.rec = 0;
            transport_req = 1; srender(2); SAV(400u);    /* playing, not armed */
            while (t->seq_idx != 2u) SAV(16u);
            stap(B_REC); sframe();                      /* armed on step 3 of the loop */
            KEY_ON(12u); SAV(64u); KEY_OFF(12u);
            for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 13u / 5u); w++)
                SAV(16u);
            stap(B_REC); srender(2); sframe(); sframe();
            len3 = t->p[P_SLEN];
            printf("  (a first take armed while playing: LEN %d)\n", (int)len3);
            ok4 = ok4 && len3 == (int32_t)(3u * spb) && song.playing;
            {   /* armed in bar 1, the first note in bar 3: the loop starts at bar 3, two bars long, its notes on
                     * steps 3 and a bar later, the playhead with them */
                int32_t len4;
                uint32_t ph;
                transport_req = 2; srender(2); sframe();
                sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();   /* cleared */
                for (k = 0; k < NSTEP; k++)
                    step_clear(&t->step[k]);
                t->p[P_SLEN] = 16;
                song.rec = 0;
                transport_req = 1; srender(2); SAV(400u);
                while (t->seq_idx != 2u) SAV(16u);
                stap(B_REC); sframe();                  /* armed on step 3, nothing played for two bars */
                for (w = 0; w < 4000u && t->seq_idx != 2u * spb + 2u; w++)
                    SAV(16u);
                KEY_ON(12u); SAV(64u); KEY_OFF(12u);
                for (w = 0; w < 4000u && t->seq_idx != 3u * spb + 2u; w++)
                    SAV(16u);
                KEY_ON(14u); SAV(64u); KEY_OFF(14u);
                for (w = 0; w < 4000u && t->seq_idx < 4u * spb - 3u; w++)
                    SAV(16u);
                ph = t->seq_idx;
                stap(B_REC); srender(2); sframe(); sframe();
                len4 = t->p[P_SLEN];
                printf("  (armed in bar 1, played from bar 3: LEN %d, notes on %u %u, playhead %u from %u)\n",
                       (int)len4, t->step[2].n, t->step[spb + 2u].n, t->seq_idx, ph);
                ok4 = ok4 && len4 == (int32_t)(2u * spb) && t->step[2].n && t->step[spb + 2u].n &&
                      t->seq_idx >= ph - 2u * spb && t->seq_idx <= ph - 2u * spb + 1u;
            }
            {   /* armed late in bar 1, played from bar 2's downbeat, REC off a few steps on (fewer than it was armed
                 * into the bar): the loop still starts at bar 2, one bar, its note on step 1 */
                int32_t len5;
                transport_req = 2; srender(2); sframe();
                sdown(B_REC); sframes(2u * SH_LONG_MS - SH_TAP_MS + 100u); sup(B_REC); sframe();   /* cleared */
                for (k = 0; k < NSTEP; k++)
                    step_clear(&t->step[k]);
                t->p[P_SLEN] = 16;
                song.rec = 0;
                transport_req = 1; srender(2); SAV(400u);
                while (t->seq_idx != spb - 2u) SAV(16u);
                stap(B_REC); sframe();                  /* armed on step spb - 1 */
                for (w = 0; w < 4000u && t->seq_idx != spb; w++)
                    SAV(16u);
                KEY_ON(12u); SAV(64u); KEY_OFF(12u);
                for (w = 0; w < 4000u && t->seq_idx != spb + 3u; w++)
                    SAV(16u);
                stap(B_REC); srender(2); sframe(); sframe();
                len5 = t->p[P_SLEN];
                printf("  (armed late in bar 1, played from bar 2: LEN %d, the note on step 1 %u)\n", (int)len5,
                       t->step[0].n);
                ok4 = ok4 && len5 == (int32_t)spb && t->step[0].n;
            }
            transport_req = 2; srender(2); sframe();
            for (k = 0; k < NSTEP; k++)                 /* notes only past LEN: not empty, no first take, nothing lost */
                step_clear(&t->step[k]);
            t->p[P_SLEN] = 16;
            t->step[20].note[0] = 64; t->step[20].time = ST_NOTE; t->step[20].n = 1;
            t->step[40].note[0] = 65; t->step[40].time = ST_NOTE; t->step[40].n = 1;
            song.rec = 0;
            transport_req = 1; srender(2); SAV(200u);
            stap(B_REC); sframe(); SAV(1500u); stap(B_REC); sframe(); sframe();
            ok4 = ok4 && t->p[P_SLEN] == 16 && t->step[40].n == 1u && t->step[20].n == 1u;
            transport_req = 2; srender(2); sframe();
            {   /* a first take whose pair leaves the front: it closes on its own part, the new part's LEN kept */
                static step_t keep2[NSTEP];
                track_t *t2;
                int16_t len2k;
                motion_clear(t);                        /* (an earlier test's knob moves would play and stay put) */
                for (k = 0; k < NSTEP; k++)
                    step_clear(&t->step[k]);
                t->p[P_SLEN] = 16;
                transport_req = 1; srender(2); SAV(200u);
                stap(B_REC); sframe();
                KEY_ON(12u); SAV(64u); KEY_OFF(12u);
                for (w = 0; w < 4000u && t->seq_idx < (uint32_t)(spb * 2u); w++)
                    SAV(16u);
                sgo2(slot_of("FM6"), note_of("CHORD"));        /* (the other part: ping-pong) */
                t2 = sh_trk();
                memcpy(keep2, t2->step, sizeof keep2);  /* (the other part as earlier tests left it, put back after) */
                len2k = t2->p[P_SLEN];
                ok4 = ok4 && t2 != t && t->p[P_SLEN] > 0 && t->p[P_SLEN] < NSTEP && t->p[P_SLEN] % (int32_t)spb == 0;
                t2->p[P_SLEN] = 16;
                SAV(500u);
                stap(B_REC); sframe(); sframe();
                ok4 = ok4 && t2->p[P_SLEN] == 16 && !sh_take.first;
                printf("  (a first take left for another pair: its LEN %d, the new part's %d)\n", (int)t->p[P_SLEN],
                       (int)t2->p[P_SLEN]);
                transport_req = 2; srender(2); sframe();
                for (k = 0; k < NSTEP; k++)
                    step_clear(&t->step[k]);
                memcpy(t2->step, keep2, sizeof keep2);
                t2->p[P_SLEN] = len2k;
                sgo2(slot_of("FM6"), note_of("KEYS"));
                t = sh_trk();
            }
            song.rec = (uint8_t)(1u << sh.part);
        }
        printf("  (bar %u steps; first takes: LEN %d and %d)\n", spb, (int)len1, (int)len2);
        bad += check("REC + PLAY: armed, one bar counted in with the click, then recording on all 64 steps", ok1);
        bad += check("REC: a first take of 2.4 bars sets LEN to 2 bars (what ran past it gone)", ok2);
        bad += check("REC held through the first ring: the last take undone (steps, LEN), the arm kept; again: nothing", ok3);
        bad += check("REC: a first take of 2.6 bars sets LEN to 3 bars, from stop and armed while playing; notes past LEN "
                     "make no first take; a pair switch closes it on its part; a note past the rounding keeps its bar", ok4);
        bad += check("REC held through both rings: the pattern cleared, the arm kept", ok5);
        song.rec = 0;
        t->p[P_SLEN] = 16;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
#undef SAV
#undef KEY_ON
#undef KEY_OFF
    }

    {   /* FUN11's steps: six notes, each with its own JUST (a signed nibble, -8 .. 7). Every writer keeps them
         * together: step_clear and SEQ's step key leave none behind, the DRUM grid's compaction moves a note's JUST with
         * it, a loaded sequence keeps only the sounding notes' */
        step_t st, sq[NSTEP];
        uint32_t j, ok1 = 1, ok2, ok3, ok4 = 1;
        memset(&st, 0, sizeof st);
        for (j = 0; j < STEP_NOTES; j++) {
            step_set_just(&st, j, (int32_t)j * 3 - 8);   /* -8 -5 -2 1 4 7 */
            ok1 = ok1 && step_just(&st, j) == (int32_t)j * 3 - 8;
        }
        step_set_just(&st, 0, 20);                      /* (clamped) */
        ok1 = ok1 && step_just(&st, 0) == 7 && step_just(&st, 1) == -5;
        st.n = 6; st.time = ST_NOTE;
        step_clear(&st);
        ok2 = !st.just[0] && !st.just[1] && !st.just[2];
        memset(&st, 0, sizeof st);                      /* a DRUM step: a kick (a lane), then a low tom with JUST 3 */
        st.time = ST_NOTE; st.n = 2; st.note[0] = DRUM_LANE_NOTE[0]; st.note[1] = 41;
        step_set_just(&st, 0, -4); step_set_just(&st, 1, 3);
        step_to_grid(&st);
        ok3 = st.n == 1u && st.note[0] == 41u && step_just(&st, 0) == 3 && step_just(&st, 1) == 0 && (st.hit & 1u);
        memset(sq, 0, sizeof sq);
        sq[0].n = 2; sq[0].time = ST_NOTE;
        for (j = 0; j < STEP_NOTES; j++)
            step_set_just(&sq[0], j, 5);
        proj_steps(sq);
        for (j = 0; j < STEP_NOTES; j++)
            ok4 = ok4 && step_just(&sq[0], j) == (j < 2u ? 5 : 0);
        {   /* SEQ held, a white key on an empty step: the note with no JUST, whatever the step held before */
            track_t *t;
            sgo2(slot_of("FM6"), note_of("KEYS"));
            t = sh_trk();
            step_clear(&t->step[0]);
            t->step[0].just[0] = 0x55u;
            sdown(B_SEQ); sframes(SH_TAP_MS + 100u);
            skey((uint32_t)sh_white_key(0));
            sup(B_SEQ); sframe();
            ok4 = ok4 && t->step[0].n == 1u && step_just(&t->step[0], 0) == 0;
            step_clear(&t->step[0]);
        }
        bad += check("FUN11 steps: six JUST nibbles round-trip (clamped); step_clear leaves none", ok1 && ok2);
        bad += check("FUN11 steps: the DRUM grid moves a note's JUST with it; a load and SEQ's key leave no stale JUST",
                     ok3 && ok4);
    }

    {   /* FUN11 playback: a step of six notes, each with its own JUST, starts six voices at note x 16 + JUST */
        static const uint8_t NT[6] = {48, 55, 60, 64, 67, 70};
        static const int8_t JT[6] = {0, 0, 0, -2, 0, -5};
        track_t *t;
        uint32_t k, q, w, found = 0;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        tracks_hush(1);                                 /* (six voices: the tracks left quiet) */
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        t->p[P_QUANT] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (k = 0; k < 6u; k++) {
            t->step[0].note[k] = NT[k];
            step_set_just(&t->step[0], k, JT[k]);
        }
        t->step[0].time = ST_NOTE;
        t->step[0].vel = 100;
        t->step[0].n = 6;
        song.rec = 0;
        transport_req = 1;
        for (w = 0; w < 6u; w++)
            srender(1);
        for (k = 0; k < 6u; k++)
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].gate && t->v[q].note == NT[k] &&
                    t->v[q].pitch16 == (int32_t)NT[k] * 16 + JT[k]) {
                    found++;
                    break;
                }
        transport_req = 2; srender(4);
        step_clear(&t->step[0]);
        tracks_hush(0);
        bad += check("FUN11 playback: a six-note step with JUST starts each voice at its note x 16 + its JUST", found == 6u);
    }

    {   /* FUN11 recording: chord_drop, one rule for a played chord and a recorded one. C9 over a low C with an 11 (seven
         * notes): the plain fifth goes. With a b5 instead: the doubled root goes, then the highest extension. A take
         * of seven notes into one step keeps six by that rule, each with the JUST it sounded with */
        static const uint8_t C9[7] = {36, 48, 52, 55, 58, 62, 65};
        static const uint8_t B5[7] = {36, 48, 52, 54, 58, 62, 65};
        uint8_t b5[6];
        uint32_t k, q, ok1, ok2, ok3 = 1, d;
        track_t *t;
        step_t *st;
        ok1 = C9[chord_drop(C9, 7, 48)] == 55u;
        d = chord_drop(B5, 7, 48);
        ok2 = B5[d] == 48u;
        for (k = 0, q = 0; k < 7u; k++)
            if (k != d)
                b5[q++] = B5[k];
        ok2 = ok2 && b5[chord_drop(b5, 6, 48)] == 65u;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = (uint16_t)(1u << sh.part);
        transport_req = 1; srender(2);
        for (k = 0; k < 7u; k++) {                      /* (the seven at once, the seventh with JUST -3) */
            voice_just = (int8_t)(k == 4u ? -3 : 0);
            input_on(t, C9[k], 100);
            voice_just = 0;
        }
        srender(1);
        for (k = 0; k < 7u; k++)
            input_off(t, C9[k]);
        transport_req = 2; srender(4);
        song.rec = 0;
        for (q = 0; q < NSTEP && !t->step[q].n; q++)
            ;
        st = &t->step[q < NSTEP ? q : 0];
        ok3 = st->n == 6u;
        for (k = 0; k < st->n; k++) {
            ok3 = ok3 && st->note[k] != 55u;            /* (the fifth given up) */
            ok3 = ok3 && step_just(st, k) == (st->note[k] == 58u ? -3 : 0);
        }
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        bad += check("chord_drop: a seven-note C9 gives up the plain fifth; with b5 the doubled root, then the top extension",
                     ok1 && ok2);
        bad += check("FUN11 recording: seven notes into a step keep six by chord_drop, each with its JUST", ok3);
    }

    {   /* ARP RATE's dotted values and the 1/4T and 1/32T: their lengths against a quarter, the knob walking them
         * longest first, and an arp at 1/4T firing every two thirds of a beat */
        uint32_t q = beat_samples(), ok = 1, k, b, last = 0, gaps = 0, sum = 0;
        int32_t prev = -1;
        const param_desc_t *d = &TP[P_ARATE];
        track_t *t;
        ok = rate_samples(12) == q * 3u / 2u && rate_samples(10) == q * 3u / 4u && rate_samples(11) == q * 3u / 8u &&
             rate_samples(13) == q * 2u / 3u && rate_samples(14) == q / 12u && d->max == 14;
        for (k = 0; k + 1u < 15u; k++)                  /* (the shown order: each value shorter than the one before) */
            ok = ok && rate_samples(ARATE_ORDER[k]) > rate_samples(ARATE_ORDER[k + 1u]);
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_ARATE] = 13; t->p[P_ASWING] = 0; t->p[P_APROB] = 127;
        input_on(t, 60, 100);
        for (b = 0; b < 800u; b++) {
            uint32_t on = t->arp_note != 0u;
            srender(1);
            if (t->arp_note && (!on || (int32_t)t->arp_idx != prev)) {
                if (prev >= 0 && (int32_t)t->arp_idx != prev) {
                    sum += (b - last) * HALF_FRAMES;
                    gaps++;
                }
                last = b;
                prev = (int32_t)t->arp_idx;
            }
        }
        input_off(t, 60);
        t->p[P_AMODE] = 0; t->p[P_ARATE] = 2;
        srender(8);
        printf("  (1/4T: %u gaps, %u samples on average against %u)\n", gaps, gaps ? sum / gaps : 0u, q * 2u / 3u);
        ok = ok && gaps >= 4u && sum / gaps + HALF_FRAMES >= q * 2u / 3u && sum / gaps <= q * 2u / 3u + HALF_FRAMES;
        bad += check("ARP RATE: 1/4. 1/8. 1/16. 1/4T 1/32T long as they say, the knob longest first, the arp at 1/4T", ok);
    }

    {   /* HOLD with no arp mode: the keys latch. A note let go sounds on; a note pressed with no key down ends the
         * latch and starts another; two keys down together latch together; HOLD off, an arp mode on and a switch of
         * pairs end it; under REC a latched note records as held (ties) until it ends */
        track_t *t;
        uint32_t ok, k, ties = 0;
#define SOUNDS(n) sh_sounds(t, n)
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        srender(2);
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();   /* ARP held: HOLD on, no arp mode needed */
        ok = t->p[P_AHOLD] == 1;
        input_on(t, 60, 100); srender(2); input_off(t, 60); srender(4);
        ok = ok && SOUNDS(60);
        input_on(t, 64, 100); input_on(t, 67, 100); srender(2);   /* a fresh press: 60 ends, 64 and 67 latch */
        input_off(t, 64); srender(2);
        ok = ok && !SOUNDS(60) && SOUNDS(64) && SOUNDS(67);
        input_off(t, 67); srender(4);
        ok = ok && SOUNDS(64) && SOUNDS(67);
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();   /* HOLD off: all of it ends */
        srender(4);
        ok = ok && !t->p[P_AHOLD] && !SOUNDS(64) && !SOUNDS(67);
        bad += check("HOLD with no arp: the keys latch, a fresh press starts a new latch, HOLD off ends it", ok);
        t->p[P_AHOLD] = 1; srender(2);
        input_on(t, 62, 100); srender(2); input_off(t, 62); srender(2);
        sgo2(slot_of("LIZARD"), note_of("KEYS"));        /* a switch of pairs: nothing latched survives */
        srender(4);
        ok = !SOUNDS(62);
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_AHOLD] = 1; t->p[P_AMODE] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = (uint16_t)(1u << sh.part);
        transport_req = 1; srender(2);
        input_on(t, 60, 100); srender(2); input_off(t, 60);
        for (k = 0; k < 300u; k++)                      /* (about three steps) */
            srender(1);
        t->p[P_AHOLD] = 0; srender(4);
        transport_req = 2; srender(4);
        song.rec = 0;
        for (k = 0; k < NSTEP; k++)
            ties += t->step[k].time == ST_TIE;
        ok = ok && ties >= 2u && !SOUNDS(60);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (k = 0; k < NTRK; k++)                      /* (the switch put the pair on the other part: both off) */
            trk[k].p[P_AHOLD] = 0;
        srender(4);
#undef SOUNDS
        bad += check("HOLD with no arp: a switch of pairs ends the latch; under REC a latched note records held (ties)", ok);
    }

    {   /* CHORD PLAY's RPT: a held chord strums again every RPT. Stopped, from the press at the song's tempo: at 1/8
         * (STRM 0: the chord struck whole) a second held strikes it 4 times. With STRM 60 the strums alternate: the
         * first note of one the lowest, of the next the highest. Under REC (STRM 0) the chord is kept once, the
         * strikes not (one step, held); played back, the step strikes again every 1/8 as the held chord did */
        uint32_t cn = note_of("CHORD"), q, k, strikes = 0, dir[8], nd = 0, alt = 1, even = 0, odd = 0, last;
        uint32_t lastage[NVOICE];
        track_t *t;
        sgoc(slot_of("ANALOG"), cn);
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        srender(40);
        transport_req = 2; srender(2);
        for (int pass = 0; pass < 2; pass++) {          /* 0: STRM 0, count; 1: STRM 60, directions */
            sh_sp[SP_STRM - SP_FIRST] = pass ? 60 : 0; sh_sp[SP_RPT - SP_FIRST] = 2; sframe();
            memset(t->v, 0, sizeof t->v);
            for (q = 0; q < NVOICE; q++)
                lastage[q] = 0;
            fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
            for (k = 0, last = 0; k < FS / HALF_FRAMES; k++) {   /* (a second: 1/8 at 120 is 250 ms) */
                uint32_t started = 0, first = 0;
                srender(1);
                if (!(k & 3u))
                    sframe();
                for (q = 0; q < NVOICE; q++)
                    if (t->v[q].active && t->v[q].age != lastage[q]) {
                        started++;
                        first = t->v[q].note;
                        lastage[q] = t->v[q].age;
                    }
                if (!pass && started >= 3u)
                    strikes++;
                if (pass && started && (!nd || k - last > 30u) && nd < 8u)   /* (a strum's first note: 30 blocks
                                                                               * after the last one's notes) */
                    dir[nd++] = first;
                if (started)
                    last = k;
            }
            fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(60);
        }
        for (k = 1; k < nd; k++)
            alt = alt && dir[k] != dir[k - 1u];
        printf("  (RPT 1/8 stopped, a second: %u strikes; STRM 60: %u strum starts seen, alternating %u)\n", strikes, nd, alt);
        ok = (strikes == 4u || strikes == 5u) && nd >= 3u && alt;   /* (the press, then each 1/8: 5 if the last
                                                                     * lands on the second's end) */
        sh_sp[SP_STRM - SP_FIRST] = 0; sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        song.rec = (uint16_t)(1u << sh.part);
        transport_req = 1; srender(2);
        fm1_in.notes |= 1u << CB_ROOT0; keyboard_block();
        for (k = 0; k < 2u * FS / HALF_FRAMES; k++) {   /* (two seconds: a bar at 120) */
            srender(1);
            if (!(k & 3u))
                sframe();
        }
        fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block();
        transport_req = 2; srender(4);
        song.rec = 0;
        for (k = 0; k < 16u; k++)
            even += t->step[k].time == ST_NOTE && t->step[k].n > 0u;   /* (even: the steps that start notes) */
        memset(t->v, 0, sizeof t->v);
        for (q = 0; q < NVOICE; q++)
            lastage[q] = 0;
        transport_req = 1; srender(2);                  /* played back: the step strikes on every 1/8 */
        for (k = 0; k < 2u * FS / HALF_FRAMES; k++) {
            uint32_t started = 0;
            srender(1);
            if (!(k & 3u))
                sframe();
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].age != lastage[q]) {
                    started++;
                    lastage[q] = t->v[q].age;
                }
            odd += started >= 3u;                       /* (odd: the strikes heard) */
        }
        transport_req = 2; srender(4);
        printf("  (RPT 1/8 under REC: %u steps hold the chord; played back, %u strikes in a bar)\n", even, odd);
        ok = ok && even == 1u && odd >= 8u && odd <= 9u;   /* (8 a bar, the loop's first again) */
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sh_sp[SP_RPT - SP_FIRST] = 0; sframe();
        bad += check("CHORD RPT: a held chord strikes again every 1/8, strums alternating; under REC kept once, played "
                     "back it strikes every 1/8",
                     ok);
    }

    {   /* ARP MODE turned on from CHORD's page (its fourth knob): a toast names the arp's RATE and where it is */
        track_t *t;
        uint8_t ids[4];
        int glob;
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        shome(); snmain();
        sh_page_ids(ids, &glob);
        t->p[P_AMODE] = 0; t->p[P_ARATE] = 2;
        ok = sh.page[sh.mode] == SHP_NOTE && ids[3] == P_AMODE;
        sturn(EN_K4, 1);
        ok = ok && t->p[P_AMODE] == 1 && str_len(sh.toast) > 9u && !memcmp(sh.toast, "ARP RATE ", 9) &&
             strstr(sh.toast, "1/16") != 0;
        t->p[P_AMODE] = 0;
        shome();
        bad += check("CHORD's ARP MODE turned on: the toast names the arp's RATE (1/16) and its page", ok);
    }

    {   /* a project brings its pairs back: saved with FM6 > KEYS in front (a tweak, a step), loaded over LIZARD >
         * CHORD (tweaked too): FM6 > KEYS is in front again on its part with the tweak and the step, FM6's slot and
         * FM6 > KEYS's parked take hold them, LIZARD (a SOUND the project does not hold) keeps its tweak in its slot */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), kn = note_of("KEYS"), cn = note_of("CHORD"), part;
        int16_t fmv, lzv;
        track_t *t;
        sgo2(fm, kn);
        t = sh_trk();
        part = sh.part;
        {   /* (a tweak inside E1's range for this engine) */
            const param_desc_t *d = param_desc_of(t->eng_req, P_E1);
            fmv = t->p[P_E1] = (int16_t)(t->p[P_E1] == d->min ? d->min + 1 : d->min);
        }
        step_clear(&t->step[3]);
        t->step[3].note[0] = 65; t->step[3].n = 1; t->step[3].time = ST_NOTE; t->step[3].vel = 90;
        {   /* (the SOUND loaded over must be one the project does not hold: LIZARD, else SHOCK, else PHYS) */
            static const char *const C[3] = {"LIZARD", "SHOCK", "PHYS"};
            uint32_t c, q, in;
            for (c = 0; c < 3u; c++) {
                lz = slot_of(C[c]);
                for (in = 0, q = 0; q < NPART; q++)
                    in |= sh_partpair[q] != SH_NOPAIR && (sh_partpair[q] & 0xFFu) == lz;
                if (!in)
                    break;
            }
        }
        ok = project_save(2) == 0;
        sgoc(lz, cn);
        {
            const param_desc_t *d = param_desc_of(sh_trk()->eng_req, P_E2);
            lzv = sh_trk()->p[P_E2] = (int16_t)(sh_trk()->p[P_E2] == d->min ? d->min + 1 : d->min);
        }
        project_load(2);
        sframes(100);
        t = sh_trk();
        ok = ok && sh.mode == fm && sh.note == kn && sh.part == part && t->eng_req == ENGI_FM6 && t->p[P_E1] == fmv &&
             t->step[3].n == 1u && t->step[3].note[0] == 65u && !t->p[P_MUTE];
        ok = ok && sh_snap_of(fm)->valid && sh_snap_of(fm)->p[P_E1] == fmv && sh_pk_find(fm, kn) >= 0 &&
             sh_nstep_bank[sh_pk_find(fm, kn)][3].note[0] == 65u;   /* (the take parked as FM6 > KEYS's own) */
        ok = ok && sh_snap_of(lz)->valid && sh_snap_of(lz)->p[P_E2] == lzv;
        step_clear(&t->step[3]);
        bad += check("project load: its pair comes back in front with its sound and steps; LIZARD keeps its tweak", ok);
        {   /* a project without pairs (FUN10 and older): it lands on the pair in front */
            static project_t q;
            project_capture(&q);
            ok = q.pair[3] == SH_PAIR_NONE && (q.pair[7] & 1u);   /* (none behind: 127, flagged) */
            bad += check("project save: nothing behind is SH_PAIR_NONE with byte 7's flag, never the catalogue's length", ok);
            q.pair_ok = 0;
            q.sum = proj_sum(&q);
            sgoc(lz, cn);
            ok = !project_restore_runtime(&q) && sh.mode == lz && sh.note == cn && !sh_trk()->p[P_MUTE];
            bad += check("project load without pairs (FUN10): it lands on the pair in front", ok);
            sgo2(fm, kn);
        }
        {   /* RUDIMENT saved as a SOUND (index 3, before the split; pair[7] bit 1 clear): it loads as DRUMS + RUDIMENT;
             * a new save sets the bit */
            static project_t q;
            project_capture(&q);
            ok = (q.pair[7] & 2u) != 0u;
            q.pair[0] = 3u; q.pair[1] = 0u; q.pair[2] = 3u; q.pair[7] = 1u;   /* (its own part 4, as it always was, */
            q.t[3].engine = ENGI_DRUM;                                         /* playing its kit) */
            q.sum = proj_sum(&q);
            ok = ok && !project_restore_runtime(&q) && sh.mode == 3u && sh.note == SH_N_RUDIMENT && sh.part == 3u;
            q.pair[2] = 0u; q.pair[7] = 3u;             /* (the bit set: SOUND 3 with KEYS is DRUMS with KEYS) */
            q.t[0].engine = ENGI_DRUM;
            q.sum = proj_sum(&q);
            ok = ok && !project_restore_runtime(&q) && sh.mode == 3u && sh.note == 0u;
            bad += check("project: one saved before the split (RUDIMENT a SOUND) loads as DRUMS + RUDIMENT; saves flag it", ok);
            sgo2(fm, kn);
        }
        {   /* the shell's settings saved before the split (wheel mark 0x52): DRUMS keeps the white key, the black keys
             * go back to the default (RUDIMENT on A#4), a pair saved on SOUND 3 comes back as DRUMS + RUDIMENT */
            uint8_t st[16], st3[16];
            uint32_t k;
            memcpy(st, SH_ST, sizeof st);
            memcpy(st3, SH_ST3, sizeof st3);
            SH_ST3[0] = SH_ST3_MARK_V1;
            for (k = 0; k < 5u; k++) SH_ST3[9u + k] = (uint8_t)(k + 1u);   /* (KEYS CHORD ALIVE TONNETZ ENIGMA) */
            {   /* (the default wheel, so only the mark asks for a write) */
                static const uint8_t wd[SH_SLOTS] = {1u, 2u, 3u, 4u, SH_G4 + 1u, 6u, 7u, 0xFEu};
                memcpy(&SH_ST3[1], wd, sizeof wd);
            }
            SH_ST[0] = SH_ST_MARK; SH_ST[SH_ST_PAIR] = 4u; SH_ST[SH_ST_PAIR + 1u] = 1u;   /* (RUDIMENT, KEYS) */
            sh_store_load();
            ok = !memcmp(sh_wnoteslot, SH_WNOTE_DEF, sizeof sh_wnoteslot) && sh_enter_saved() && sh.mode == 3u &&
                 sh.note == SH_N_RUDIMENT;
            sgo2(3u, kn);                               /* DRUMS + KEYS played, settled: the wheel is written anew */
            sframes(SH_ST_IDLE_MS + 500u);
            ok = ok && SH_ST3[0] == SH_ST3_MARK && SH_ST[SH_ST_PAIR] == 4u && SH_ST[SH_ST_PAIR + 1u] == kn + 1u;
            sh_store_load();                            /* power-on: DRUMS + KEYS, as saved, not RUDIMENT */
            ok = ok && sh_enter_saved() && sh.mode == 3u && sh.note == kn;
            memcpy(SH_ST, st, sizeof st);
            memcpy(SH_ST3, st3, sizeof st3);
            sh_store_load();
            bad += check("settings from before the split (0x52): the black keys' default, a pair on SOUND 3 as RUDIMENT; the "
                         "next write takes the new mark, so DRUMS + KEYS comes back as DRUMS + KEYS", ok);
            sgo2(fm, kn);
        }
        {   /* settings from before LINES (0x53): the black keys the default, so LINES is on F#4; the white keys stay the
             * build's eight (an old wheel's ORGAN on C5 is not read) */
            uint8_t st[16], st3[16];
            uint32_t k, og = slot_of("ORGAN");
            memcpy(st, SH_ST, sizeof st);
            memcpy(st3, SH_ST3, sizeof st3);
            SH_ST3[0] = SH_ST3_MARK_V2;
            for (k = 0; k < SH_SLOTS; k++) SH_ST3[1u + k] = (uint8_t)(sh_wslot[k] == SH_EMPTY ? 0xFEu : sh_wslot[k] + 1u);
            SH_ST3[1u + 7u] = (uint8_t)(og + 1u);       /* (C5: ORGAN, an old player's) */
            SH_ST3[9u] = 1; SH_ST3[10] = 2; SH_ST3[11] = 3; SH_ST3[12] = 4; SH_ST3[13] = SH_N_RUDIMENT + 1u;
            sh_store_load();
            ok = sh_wslot[7] == SH_C5 && !memcmp(sh_wnoteslot, SH_WNOTE_DEF, sizeof sh_wnoteslot) &&
                 sh_wnoteslot[2] == SH_N_LINES;
            memcpy(SH_ST, st, sizeof st);
            memcpy(SH_ST3, st3, sizeof st3);
            sh_wslot[7] = SH_C5;
            sh_store_load();
            bad += check("settings from before LINES (0x53): the black keys' default (LINES on F#4), the white keys the build's",
                         ok);
            sgo2(fm, kn);
        }
        {   /* LINES, the second lanes engine: lanes of its own (RUDIMENT's untouched), on part 3; its line in front
             * with DRUMS + RUDIMENT behind, both sounding; its lanes kept with a project; its undo its own */
            uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), ln = note_of("LINES"), v, k, lp, rp, sl = 0, sr = 0;
            track_t *tl, *tr;
            lp = SH_NOTES[ln].part; rp = SH_NOTES[rn].part;
            tl = &trk[lp]; tr = &trk[rp];
            sgo2(dr, rn);                               /* RUDIMENT: a kick on step 1, its lane 0 LEN 16 */
            for (k = 0; k < NSTEP; k++) step_clear(&tr->step[k]);
            grid_hit(tr, 0, 0, 1u);
            sgo2(fm, ln);                               /* LINES on FM6: its own lanes */
            ok = sh.part == lp && sh_kind() == SHK_RUDIMENT && sh_li == 1u;
            for (k = 0; k < NSTEP; k++) step_clear(&tl->step[k]);
            sh_eu[0][0] = 9; shell_lane_len[0] = 9;
            grid_hit(tl, 0, 0, 1u);
            ok = ok && sh_eu_i[0][0][0] == 0 && sh_eu_i[1][0][0] == 9 && shell_lane_len_i[0][0] == 0u;
            sgo2(dr, rn);
            ok = ok && sh_li == 0u && sh_eu[0][0] == 0 && ((step_lanes(&tr->step[0])) & 1u);
            sgo2(fm, ln);                               /* LINES on FM6 in front, BEAT (DRUMS + RUDIMENT) left */
            ok = ok && sh_track_note(rp) == rn && sh_track_live(rp) && sh.part == lp && shell_lanes_part[1] == lp + 1u;
            memset(tl->v, 0, sizeof tl->v);
            memset(tr->v, 0, sizeof tr->v);
            transport_req = 1; srender(6);
            for (v = 0; v < NVOICE; v++) {
                sl |= tl->v[v].active && tl->v[v].note == shell_lane_note_i[1][0];
                sr |= tr->v[v].active && tr->v[v].note == shell_lane_note_i[0][0];
            }
            transport_req = 2; srender(2);
            ok = ok && sl && sr;
            bad += check("LINES: its own lanes (RUDIMENT's untouched); in front on FM6 with DRUMS + RUDIMENT behind, both sound", ok);
            ok = project_save(1) == 0;                  /* its lanes with a project */
            sh_eu[0][0] = 0; shell_lane_len[0] = 0;
            project_load(1);
            ok = ok && sh_eu_i[1][0][0] == 9 && shell_lane_len_i[1][0] == 9u && sh_eu_i[0][0][0] == 0;
            bad += check("LINES: its lanes kept with a project (tag 8), RUDIMENT's beside them", ok);
            sgo2(fm, ln);
            sh_sp[SP_RHY - SP_FIRST] = 0;
            snote();
            sturn(EN_ALGO, 1);                          /* ACID 1 onto LINES' lanes */
            ok = sh_eu_i[1][0][0] == 0 && sh_eu_i[0][0][0] == 0 && (step_lanes(&tl->step[0]) & 1u) &&
                 sh_sp[SP_RHY - SP_FIRST] == 1 && str_eq(sh.toast, "NEW LINE: LINE 1");
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
            ok = ok && str_eq(sh.toast, "BEAT UNDONE") && sh_eu_i[1][0][0] == 9 && sh_sp[SP_RHY - SP_FIRST] == 0 &&
                 ((step_lanes(&tr->step[0])) & 1u);
            bad += check("LINES: ALGORITHM writes an ACID line on its lanes, REC held: BEAT UNDONE gives LINES' lanes back, "
                         "RUDIMENT's kept", ok);
            {   /* ACID: the same seed, DENS and SPAN, the same line; DENS steps play, one hit each, inside SPAN lanes;
                 * accents, slides and ties among them; DENS turned writes the line again */
                static step_t a[NSTEP];
                uint32_t seed, plays, one, inside, acc = 0, sld = 0, tie = 0, same = 1;
                ok = 1;
                tl->p[P_SLEN] = 16;
                for (seed = 1; seed <= 20u; seed++) {
                    sh_sp[SP_DENS - SP_FIRST] = (int16_t)(4u + seed % 12u);
                    sh_sp[SP_SPAN - SP_FIRST] = (int16_t)(1u + seed % 8u);
                    sh_lwr_load(tl, seed);
                    memcpy(a, tl->step, sizeof a);
                    sh_lwr_load(tl, seed);
                    same &= !memcmp(a, tl->step, sizeof a);
                    for (plays = 0, one = 1, inside = 1, k = 0; k < 16u; k++) {
                        uint32_t h = step_lanes(&tl->step[k]);
                        plays += h != 0u;
                        one &= !(h & (h - 1u));
                        inside &= h < (1u << sh_sp[SP_SPAN - SP_FIRST]);
                        acc += step_accents(&tl->step[k]) != 0u;
                        sld += (tl->step[k].flags & SF_SLIDE) != 0u;
                        tie += tl->step[k].time == ST_TIE;
                    }
                    ok = ok && plays == (uint32_t)sh_sp[SP_DENS - SP_FIRST] && one && inside && (step_lanes(&tl->step[0]) & 1u) &&
                         tl->p[P_SLEN] == 16;
                }
                {   /* DENS adds to the line: d's hits are among d+1's, on the same lanes; SPAN keeps the steps */
                    static step_t lo[NSTEP];
                    uint32_t d, nest = 1, sp, steps0 = 0, steps1;
                    for (seed = 1; seed <= 8u; seed++)
                        for (d = 1; d < 16u; d++) {
                            sh_sp[SP_SPAN - SP_FIRST] = 5;
                            sh_sp[SP_DENS - SP_FIRST] = (int16_t)d;
                            sh_lwr_load(tl, seed);
                            memcpy(lo, tl->step, sizeof lo);
                            sh_sp[SP_DENS - SP_FIRST] = (int16_t)(d + 1u);
                            sh_lwr_load(tl, seed);
                            for (k = 0; k < 16u; k++)
                                nest &= !step_lanes(&lo[k]) || step_lanes(&lo[k]) == step_lanes(&tl->step[k]);
                        }
                    for (sp = 1; sp <= 8u; sp++) {
                        sh_sp[SP_SPAN - SP_FIRST] = (int16_t)sp;
                        sh_sp[SP_DENS - SP_FIRST] = 8;
                        sh_lwr_load(tl, 5u);
                        for (steps1 = 0, k = 0; k < 16u; k++)
                            steps1 |= (step_lanes(&tl->step[k]) != 0u) << k;
                        nest &= sp == 1u ? (steps0 = steps1, 1) : steps1 == steps0;
                    }
                    {   /* LOOP: the line as long as the part's LEN; a longer one plays, in the shorter one's steps,
                         * only notes the shorter one played, each on its lane (DENS fixed: they spread, so some go) */
                        static const uint8_t LS[4] = {8, 16, 24, 32};
                        uint32_t a2, b2, keeps = 1, lens = 1;
                        for (seed = 1; seed <= 8u; seed++)
                            for (a2 = 0; a2 < 3u; a2++)
                                for (b2 = a2 + 1u; b2 < 4u; b2++) {
                                    sh_sp[SP_SPAN - SP_FIRST] = 5;
                                    sh_sp[SP_DENS - SP_FIRST] = 6;
                                    tl->p[P_SLEN] = LS[a2];
                                    sh_lwr_load(tl, seed);
                                    memcpy(lo, tl->step, sizeof lo);
                                    lens &= sh_eu[0][0] == 0 && shell_lane_len[7] == 0u;
                                    tl->p[P_SLEN] = LS[b2];
                                    sh_lwr_load(tl, seed);
                                    for (k = 0; k < LS[a2]; k++)
                                        keeps &= !step_lanes(&tl->step[k]) || step_lanes(&tl->step[k]) == step_lanes(&lo[k]);
                                    for (steps1 = 0, k = 0; k < LS[b2]; k++)
                                        steps1 += step_lanes(&tl->step[k]) != 0u;
                                    lens &= steps1 == 6u && sh_eu[0][0] == 0;
                                }
                        tl->p[P_SLEN] = 16;
                        ok = ok && keeps && lens;
                        printf("  (ACID: DENS nests, SPAN keeps the steps %u; LOOP: lengths %u, a longer line keeps "
                               "the shorter one's notes it plays %u)\n", nest, lens, keeps);
                    }
                    ok = ok && nest;
                }
                ok = ok && same && acc && sld && tie;
                printf("  (ACID over 20 seeds: accents %u, slides %u, ties %u)\n", acc, sld, tie);
                sh_sp[SP_DENS - SP_FIRST] = 6; sh_sp[SP_SPAN - SP_FIRST] = 5;
                sh_sp[SP_RHY - SP_FIRST] = 7;
                sh_lwr_load(tl, 7u);
                sh.page[sh.mode] = SHP_LINE; sframe();
                sturn(EN_K1, 4);                        /* DENS up from 6: the line again */
                for (plays = 0, k = 0; k < 16u; k++) plays += step_lanes(&tl->step[k]) != 0u;
                ok = ok && sh_sp[SP_DENS - SP_FIRST] > 6 && plays == (uint32_t)sh_sp[SP_DENS - SP_FIRST];
                {   /* LEN turned on the SEQ page never writes the line: longer, the lanes follow and the new steps
                     * are empty; shorter and back, the steps past it kept */
                    static step_t was[NSTEP];
                    uint32_t pg, lp0 = (uint32_t)tl->p[P_SLEN], same = 1, empty = 1;
                    memcpy(was, tl->step, sizeof was);
                    for (pg = 0; pg < 8u && sh.page[sh.mode] != SHP_SEQ; pg++)
                        stap(B_SEQ);
                    sturn(EN_K1, 2);
                    for (k = 0; k < lp0; k++) same &= !memcmp(&was[k], &tl->step[k], sizeof was[k]);
                    for (k = lp0; k < (uint32_t)tl->p[P_SLEN]; k++) empty &= !step_lanes(&tl->step[k]);
                    ok = ok && sh.page[sh.mode] == SHP_SEQ && (uint32_t)tl->p[P_SLEN] > lp0 && sh_eu[0][0] == 0 &&
                         sh_lane_len(0) == (uint32_t)tl->p[P_SLEN] && same && empty;
                    sturn(EN_K1, -4);
                    sturn(EN_K1, 2);
                    ok = ok && (uint32_t)tl->p[P_SLEN] == lp0 && !memcmp(was, tl->step, sizeof was);
                    printf("  (ACID: LEN %u longer keeps the line %u, the new steps empty %u)\n", lp0, same, empty);
                }
                sh_sp[SP_DENS - SP_FIRST] = 10; sh_sp[SP_RHY - SP_FIRST] = 0;
                shome();
                bad += check("ACID: a seed is a line (the same each time); DENS steps play, one hit each, inside SPAN; "
                             "accents, slides, ties; DENS adds to it, the LEN drawn at its length; DENS turned writes it "
                             "again, LEN never", ok);
            }
            sh_eu[0][0] = 0; shell_lane_len[0] = 0;
            for (k = 0; k < NSTEP; k++) { step_clear(&tl->step[k]); step_clear(&tr->step[k]); }
            sgo2(fm, kn);
        }
        {   /* the tracks round trip (tag 9): BEAT and LINE's pairs and LINE's mute saved, other pairs and no mute after,
             * the load puts them back; a project naming no tracks leaves them with none */
            uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), an = slot_of("ANALOG"), ln = note_of("LINES");
            sgo2(dr, rn);
            sgoc(an, ln);
            sh_track_mute(an, ln);                      /* (LINE muted) */
            sgo2(fm, kn);
            ok = project_save(3) == 0 && ((sh_tmute >> 2) & 1u);
            sgo2(slot_of("LIZARD"), rn);                /* BEAT on another pair, LINE back */
            sh_track_mute(an, ln);
            sgo2(fm, kn);
            ok = ok && sh_partpair[3] == (uint16_t)(slot_of("LIZARD") | rn << 8) && !sh_tmute;
            project_load(3);
            sframes(100);
            ok = ok && sh.mode == fm && sh.note == kn && sh_partpair[3] == (uint16_t)(dr | rn << 8) &&
                 sh_partpair[2] == (uint16_t)(an | ln << 8) && ((sh_tmute >> 2) & 1u) && trk[2].p[P_MUTE] &&
                 !trk[3].p[P_MUTE];
            bad += check("project: the tracks' pairs and LINE's mute saved (tag 9) and put back over others", ok);
            sh_track_mute(an, ln);
            sgo2(fm, kn);
        }
    }

    {   /* PROJECTS (GLO's third page): PRESETS walks A..D (or the first knob); OCT+ asks, OCT+ again saves the music to the slot (its name
         * shown); OCT- on an empty slot says so; OCT- then OCT+ loads it back: its pair in front, the page closed */
        uint32_t fm = slot_of("FM6"), kn = note_of("KEYS"), k;
        sgo2(fm, kn);
        for (k = 0; k < 4u; k++)
            memset(&proj_slot[k], 0, sizeof proj_slot[k]);
        transport_req = 2; srender(2);
        shome();
        stap(B_GLO); stap(B_GLO); stap(B_GLO);
        ok = sh.global == SHG_PROJECT && !sh_pused;
        sturn(EN_PRESET, 1);
        ok = ok && sh.pslot == 1u;
        stap(B_OCTUP);
        ok = ok && sh.pq == 1u;
        shot("projects_ask");
        stap(B_OCTUP);
        ok = ok && !sh.pq && ((sh_pused >> 1) & 1u) && project_used(1);
        sturn(EN_PRESET, 1);                            /* C: empty */
        stap(B_OCTDN);
        ok = ok && !sh.pq && !memcmp(sh.toast, "THAT SLOT IS EMPTY", 18);
        sturn(EN_PRESET, -1);
        sgo2(slot_of("LIZARD"), note_of("CHORD"));
        stap(B_GLO); stap(B_GLO); stap(B_GLO);
        ok = ok && sh.global == SHG_PROJECT && sh.pslot == 1u;
        stap(B_OCTDN);
        ok = ok && sh.pq == 2u;
        stap(B_OCTUP);
        sframes(100);
        ok = ok && sh.mode == fm && sh.note == kn && sh.global == SHG_NONE;
        for (k = 0; k < 4u; k++)
            memset(&proj_slot[k], 0, sizeof proj_slot[k]);
        bad += check("PROJECTS: PRESETS walks the slots, OCT+ OCT+ saves, an empty one refuses, OCT- OCT+ loads its pair back",
                     ok);
    }

    {   /* the arp on the grid: at every RATE, 16 notes each within a block of k x the rate (from the transport's start,
         * playing; from the first note, stopped), no step lost to the block it fell in */
        track_t *t;
        uint32_t r, pass, k, b, worst = 0, ok = 1;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_ASWING] = 0; t->p[P_APROB] = 127; t->p[P_AHOLD] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (pass = 0; pass < 2u; pass++)               /* 0 stopped, 1 playing */
            for (r = 0; r <= 14u; r++) {
                uint32_t per = rate_samples(r), n = 0, lastidx = 0, start;
                if (per * 16u > 400u * FS)              /* (the 2 and 4 bar rates: 16 of them is minutes; 4 do) */
                    continue;
                t->p[P_ARATE] = (int16_t)r;
                transport_req = 2; srender(2);
                if (pass) {
                    transport_req = 1;
                    srender(1);                         /* (the transport starts at this block's start) */
                }
                input_on(t, 60, 100);
                lastidx = t->arp_idx;
                start = 0;
                for (b = 1; n < 16u && b < 200000u; b++) {
                    srender(1);
                    if (t->arp_idx != lastidx) {        /* a note fired in this block (its end: at) */
                        uint32_t at = (b + pass) * HALF_FRAMES, ideal, e;
                        if (!n)                         /* (the first: at the key; stopped, the grid starts there) */
                            start = pass ? 0u : at;
                        else {                          /* (playing: the grid from the transport's start) */
                            ideal = start + grid_at(rate_q16(r), n);   /* (the Q16 grid: fx.c grid_at) */
                            e = at > ideal ? at - ideal : ideal - at;
                            worst = e > worst ? e : worst;
                            ok = ok && e <= HALF_FRAMES;
                        }
                        lastidx = t->arp_idx;
                        n++;
                    }
                }
                ok = ok && n == 16u;
                input_off(t, 60);
                srender(4);
            }
        transport_req = 2; srender(4);
        t->p[P_AMODE] = 0; t->p[P_ARATE] = 2;
        printf("  (the arp, every rate, 16 notes: worst %u samples off the grid)\n", worst);
        bad += check("the arp on the grid: every rate, 16 notes within a block of the grid, playing and stopped", ok);
    }

    {   /* GROOVE (SYSTEM 2), SWG its depth: two bars of 1/16 at 120 BPM, each step's start against the shape's formula;
         * SHUFFLE at 100 on the triplet (2/3 of the 8th); PUSH at 100 at the fastest step never before the step it
         * follows; HUMAN the same on the second pass as the first, velocities too; GROOVE saved with a project */
        track_t *t = &trk[2];
        uint32_t g, i, base, ok = 1, okh = 1, okp = 1;
        int16_t keep_sw = song.g[G_SWING], keep_bpm = song.g[G_BPM], keep_own = t->p[P_SSWING];
        song.g[G_BPM] = 120;
        t->p[P_SSWING] = 0;
        base = 5512u;                                   /* (1/16 at 120) */
        for (g = 0; g < GR_N; g++) {
            int64_t at = 0;
            song_groove = (uint8_t)g;
            song.g[G_SWING] = 100;
            for (i = 0; i < 32u; i++) {
                int32_t want = (int32_t)(i % 2u ? (g == GR_SHUFFLE ? base / 3u : g == GR_PUSH ? 0u : base * 100u / 250u) : 0u);
                int64_t formula;
                if (g == GR_PUSH)
                    want = i % 2u ? -(int32_t)(base * 100u / 250u) : 0;
                if (g == GR_LAID && (i % 16u == 4u || i % 16u == 12u))
                    want += (int32_t)(base * 100u / 250u) * 2 / 3;
                formula = (int64_t)i * base + groove_off(100, base, i, 2u);
                if (g != GR_HUMAN)
                    ok = ok && formula == (int64_t)i * base + want;
                ok = ok && at == formula;              /* (the sequencer's lengths add up to it) */
                at += swing_step_len(t, base, i);
            }
        }
        song_groove = GR_SHUFFLE;                       /* the triplet: the off-16th at 2/3 of the 8th */
        {   /* (within a sample: 5512 is no multiple of 3) */
            int64_t e = 3 * ((int64_t)base + groove_off(100, base, 1, 2u)) - 2 * 2 * (int64_t)base;
            printf("  (SHUFFLE at 100: the off-16th %d samples from the triplet x3; formulas %u, PUSH %u)\n", (int)e, ok, okp);
            ok = ok && e >= -3 && e <= 3;
        }
        song_groove = GR_PUSH;                          /* PUSH, the fastest step (1/32): never before the one before */
        for (i = 0; i < 64u; i++)
            okp = okp && (int32_t)swing_step_len(t, base / 2u, i) > 0;
        song_groove = GR_HUMAN;                         /* HUMAN: bar 2 as bar 1, a part's own */
        for (i = 0; i < 16u; i++) {
            okh = okh && groove_off(100, base, i, 2u) == groove_off(100, base, i + 16u, 2u) &&
                  groove_vel(100, 100, base, i, 2u) == groove_vel(100, 100, base, i + 16u, 2u);
            okh = okh && (i == 0u ? groove_off(100, base, i, 2u) == 0 : 1);
        }
        for (i = 1, g = 0; i < 16u; i++)
            g += groove_off(100, base, i, 2u) != groove_off(100, base, i, 3u);
        okh = okh && g > 8u;                            /* (two parts move apart) */
        song.g[G_SWING] = keep_sw; song.g[G_BPM] = keep_bpm; t->p[P_SSWING] = keep_own;
        bad += check("GROOVE: each shape's steps on its formula (SHUFFLE on the triplet), PUSH never past a step's start", ok && okp);
        bad += check("GROOVE: HUMAN the same each bar, velocities too, each part its own", okh);
        {   /* SYSTEM 2: GROOVE, CLICK, COUNT-IN; GROOVE saved and loaded with the project, CLICK into the settings */
            shome();
            stap(B_GLO); stap(B_GLO);
            ok = sh.global == SHG_SYSTEM2;
            sh_sp[SP_GROOVE - SP_FIRST] = 0;
            sturn(EN_K1, 4);
            sturn(EN_K2, 2);
            ok = ok && sh_sp[SP_GROOVE - SP_FIRST] == GR_HUMAN && song_groove == GR_HUMAN && rp_get(RP_CLICK) == 2u;
            shome();
            for (i = 0; i < 4u; i++)
                memset(&proj_slot[i], 0, sizeof proj_slot[i]);
            ok = ok && project_save(3) == 0;
            sh_sp[SP_GROOVE - SP_FIRST] = GR_PUSH;
            project_load(3);
            sframes(100);
            ok = ok && sh_sp[SP_GROOVE - SP_FIRST] == GR_HUMAN && song_groove == GR_HUMAN;
            sh_sp[SP_GROOVE - SP_FIRST] = 0; rp_put(RP_CLICK, 0); sframe();
            memset(&proj_slot[3], 0, sizeof proj_slot[3]);
            bad += check("SYSTEM 2: GROOVE CLICK COUNT-IN on its knobs; GROOVE back with its project", ok);
        }
    }

    {   /* the reverb's pre-delay line passes a full send unclipped: four parts can send 262144, far over int16 */
        static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
        uint32_t k, b;
        uint8_t keep = fx_pre;
        int32_t worst = 0;
        fx_pre = 0;
        for (b = 0; b < 8u; b++) {
            for (k = 0; k < CTL; k++) {
                c[k] = d[k] = 0;
                r[k] = 200000;
            }
            fx_buses(c, d, r, w, CTL);
        }
        for (k = 0; k < CTL; k++) {
            int32_t e = fx_ri[k] - 200000;
            e = e < 0 ? -e : e;
            worst = e > worst ? e : worst;
        }
        fx_pre = keep;
        bad += check("reverb PRE: a send of 200000 (three parts at full) leaves the pre-delay within 16 of itself",
                     worst <= 16);
    }

#if FELUCCA_ORGAN
    {   /* the mod matrix leaves ORGAN's drawbars alone (a step rebuilds its tables), PERC stays a destination */
        track_t *t = &trk[0];
        int16_t keep[P_COUNT];
        uint32_t k;
        int ok2;
        uint8_t e0 = t->engine, e1 = t->eng_req;
        memcpy(keep, t->p, sizeof keep);
        t->engine = t->eng_req = ENGI_ORGAN;
        t->lfo_val = 32767;
        t->lfo_fade = 32767;
        t->p[P_M1SRC] = MS_LFO; t->p[P_M1DST] = MD_E1; t->p[P_M1AMT] = 64;
        t->p[P_M2SRC] = MS_LFO; t->p[P_M2DST] = MD_E1 + 7; t->p[P_M2AMT] = 64;
        t->p[P_E0] = 4; t->p[P_E7] = 0;
        mod_begin(t);
        ok2 = t->p[P_E0] == 4 && t->p[P_E7] != 0;
        mod_end(t);
        for (k = 0; k < P_COUNT; k++)
            t->p[k] = keep[k];
        t->engine = e0;
        t->eng_req = e1;
        bad += check("mod matrix: ORGAN's drawbar E1 is not a destination; PERC (E8) still is", ok2);
    }
#endif

    {   /* INSERT: a change of type fits the knobs to it; ROTOR's SPEED from DRIVE's 50 % is its default, SLOW */
        track_t *t = sh_trk();
        int16_t keep[4] = {t->p[P_ITYPE], t->p[P_IA], t->p[P_IB], t->p[P_IC]};
        shome();
        t->p[P_ITYPE] = IT_CHORUS;                      /* (the type before ROTOR) */
        t->p[P_IA] = 64;
        stap(B_FX); stap(B_FX);                         /* FX, then INSERT */
        sturn(EN_K1, 1);
        bad += check("INSERT: turned to ROTOR, SPEED out of its range goes to its default (SLOW)",
                     t->p[P_ITYPE] == IT_ROTARY && t->p[P_IA] == track_desc(t, P_IA)->def && t->p[P_IA] == 1);
        t->p[P_ITYPE] = keep[0]; t->p[P_IA] = keep[1]; t->p[P_IB] = keep[2]; t->p[P_IC] = keep[3];
        shome();
    }

    {   /* a track left plays: DRUMS + RUDIMENT's kicks go on under FM6 in front, its part never muted */
        uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), k, hits = 0;
        track_t *r;
        transport_req = 2;
        srender(2);
        sgo(TM_RUD);
        r = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&r->step[k]);
        for (k = 0; k < 4u; k++)
            skey((uint32_t)sh_white_key(4u * k));       /* kicks on 1 5 9 13 */
        sgo(slot_of("FM6"));
        transport_req = 1;
        for (k = 0; k < 200u; k++) {
            uint32_t v;
            srender(2);
            for (v = 0; v < NVOICE; v++)
                hits += r->v[v].active;
        }
        transport_req = 2;
        srender(2);
        bad += check("tracks: BEAT (DRUMS + RUDIMENT) left for FM6 in front keeps playing its kicks, unmuted",
                     sh_partpair[3] == (uint16_t)(dr | rn << 8) && sh.mode == slot_of("FM6") && sh.note == 0u &&
                     !r->p[P_MUTE] && hits > 0);
    }

    {   /* RUDIMENT is a NOTE engine: under every SOUND it builds its lanes on part 4; a RUDIMENT pair coming to the front
         * takes part 4 from the one behind */
        uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), m, all = 1, k;
        for (m = 0; m < SH_NMODES; m++) {
            if (!sh_wheel_avail(m))
                continue;
            sh_switch_pair(m, rn);
            sframes(64);
            grid_hit(sh_trk(), 0, 1, 1u);
            all &= sh.part == 3u && sh_kind() == SHK_RUDIMENT && sh.mode == m && ((step_lanes(&trk[3].step[0]) >> 1) & 1u);
            transport_req = 1; srender(8); transport_req = 2; srender(2);
        }
        bad += check("RUDIMENT under every SOUND: its lanes on part 4, a hit set, the pair plays", all);
        sh_switch_pair(dr, rn);
        sframe();
        sh_switch_pair(slot_of("FM6"), 0u);              /* DRUMS + RUDIMENT, left: BEAT plays on */
        ok = sh_partpair[3] == (uint16_t)(dr | rn << 8) && sh_track_live(3u) && sh.part != 3u;
        sh_switch_pair(slot_of("LIZARD"), rn);          /* another RUDIMENT pair: BEAT is its now */
        ok = ok && sh.part == 3u && sh_partpair[3] == (uint16_t)(slot_of("LIZARD") | rn << 8);
        bad += check("BEAT under FM6 plays on; another RUDIMENT pair to the front replaces BEAT's", ok);
        {   /* what the lanes play: AUTO on DRUMS the kit's pieces; on FM6 the scale's degrees up from ROOT in octave 3,
             * OCT moving them, a lane's NOTE its own; the lane names follow; a hit plays the lane's note */
            static const uint8_t CMAJ[8] = {48, 50, 52, 53, 55, 57, 59, 60};
            track_t *r4 = &trk[3];
            uint32_t v, hitn = 0;
            sh_switch_pair(dr, rn);
            sframe();
            ok = !memcmp((const void *)shell_lane_note, DRUM_LANE_NOTE, 8) && str_eq(N_SP_LANE[0], "KICK");
            sh_switch_pair(slot_of("FM6"), rn);
            r4->p[P_ROOT] = 0; r4->p[P_SCALE] = 1;
            sframe();
            ok = ok && !memcmp((const void *)shell_lane_note, CMAJ, 8) && str_eq(N_SP_LANE[0], "C3") &&
                 str_eq(N_SP_LANE[7], "C4");
            sh_sp[SP_LOCT - SP_FIRST] = 1; sframe();
            ok = ok && shell_lane_note[0] == 60u && shell_lane_note[7] == 72u;
            sh_sp[SP_LOCT - SP_FIRST] = -1; sframe();
            ok = ok && shell_lane_note[0] == 36u;
            sh_sp[SP_LOCT - SP_FIRST] = 0;
            r4->p[P_SCALE] = 0; sframe();                       /* (CHROMATIC: MAJOR's degrees) */
            ok = ok && !memcmp((const void *)shell_lane_note, CMAJ, 8);
            sh_lnote[2] = 70; sframe();
            ok = ok && shell_lane_note[2] == 70u && str_eq(N_SP_LANE[2], "A#4");
            r4->p[P_SCALE] = 1;
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            grid_hit(r4, 0, 2, 1u);                             /* lane 2 on step 1: its note sounds */
            memset(r4->v, 0, sizeof r4->v);
            transport_req = 1; srender(6);
            for (v = 0; v < NVOICE; v++)
                hitn |= r4->v[v].active && r4->v[v].note == 70u;
            transport_req = 2; srender(2);
            ok = ok && hitn;
            sh_lnote[2] = 0;
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sframe();
            bad += check("RUDIMENT's lanes: DRUMS plays the pieces, FM6 the scale up from ROOT (OCT, CHROMATIC, a NOTE), "
                         "names follow, a hit sounds its note", ok);
        }
        {   /* the lines view: RUDIMENT on FM6 draws rows of notes (rings on DRUMS); a posed acid line for the manual */
            static const uint8_t H[8][16] = {
                {1,0,0,1,0,0,1,0,0,0,1,0,1,0,0,0}, {0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, {0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0},
                {0}, {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
                {0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0}, {1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0}};
            static const uint8_t LN[8] = {16, 16, 12, 16, 7, 16, 16, 5};
            track_t *r4 = &trk[3];
            uint32_t l, s;
            sh_switch_pair(slot_of("FM6"), rn);
            r4->p[P_ROOT] = 0; r4->p[P_SCALE] = 1;
            for (l = 0; l < 8u; l++) {
                sh_eu[0][l] = LN[l]; shell_lane_len[l] = LN[l];
                for (s = 0; s < 16u; s++)
                    grid_hit(r4, s, l, H[l][s]);
            }
            grid_acc(r4, 0, 0, 1u); grid_acc(r4, 12, 0, 1u); grid_acc(r4, 9, 2, 1u); grid_acc(r4, 0, 7, 1u);
            r4->step[5].flags |= SF_SLIDE; r4->step[13].flags |= SF_SLIDE; r4->step[6].time = ST_TIE;
            sh_sp[SP_LANE - SP_FIRST] = 0;
            shome(); sframes(SH_TOAST_MS + 200u);
            ok = sh_kind() == SHK_RUDIMENT && r4->eng_req != ENGI_DRUM;
            for (l = 0; l < 8u; l++) {
                sh_eu[0][l] = 0; shell_lane_len[l] = 0;
            }
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sframe();
            bad += check("RUDIMENT's lines view: drawn on a SOUND that is not DRUMS (posed for the manual)", ok);
        }
        {   /* the lines view past 16 steps: every row shows the page in view, wherever the playhead is: page 2 of
             * LEN 32 has the hit on 21 in column 5 and its mark bright; page 1, column 5 is step 5, empty */
            track_t *r4 = &trk[3];
            uint32_t l;
            uint16_t bright;
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            r4->p[P_SLEN] = 32;                         /* (lane 8 =LEN, the others 16 of their own) */
            for (l = 0; l < 7u; l++) {
                sh_eu[0][l] = 16; shell_lane_len[l] = 16;
            }
            grid_hit(r4, 20, 7, 1u);
            shell_lane_pos[7] = 5;                      /* (the playhead on page 1) */
            sh_pg_hand = 1;
            sh_y0 = 0;
            cv_begin(240, 120, SC_BG);
            sh_pic_lines(sh_cur(), r4);
            ok = sh_kind() == SHK_RUDIMENT && cv_px[38u * 240u + 83u] != cv_px[0] &&   /* (row 1, column 5: the hit) */
                 cv_px[40u * 240u + 219u] != cv_px[40u * 240u + 215u];               /* (page 2's mark the bright one) */
            bright = cv_px[40u * 240u + 219u];
            sh_pg_hand = 0;                             /* page 1: column 5 is step 5, empty */
            cv_begin(240, 120, SC_BG);
            sh_pic_lines(sh_cur(), r4);
            ok = ok && cv_px[38u * 240u + 83u] == cv_px[0] && cv_px[40u * 240u + 215u] == bright;
            for (l = 0; l < 8u; l++) {
                sh_eu[0][l] = 0; shell_lane_len[l] = 0;
            }
            r4->p[P_SLEN] = 16;
            step_clear(&r4->step[20]);
            sframe();
            bad += check("RUDIMENT's lines view: past 16 every row shows the page in view, not the playhead's, and "
                         "marks which", ok);
        }
        {   /* a column's slide and tie: SEQ held + a white key cycles plain, SLIDE, TIE; one undo; a tied column
             * starts nothing, so what sounds holds through it whatever its lanes say */
            track_t *r4 = &trk[3];
            uint32_t v, held0 = 0, new1 = 0, w2 = (uint32_t)sh_white_key(2);
            sh_switch_pair(slot_of("FM6"), rn);
            sframe();
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            skey(w2);
            ok = (r4->step[2].flags & SF_SLIDE) && str_eq(sh.toast, "SLIDE");
            skey(w2);
            ok = ok && !(r4->step[2].flags & SF_SLIDE) && r4->step[2].time == ST_TIE && str_eq(sh.toast, "TIE");
            skey(w2);
            ok = ok && !(r4->step[2].flags & SF_SLIDE) && r4->step[2].time != ST_TIE;
            sup(B_SEQ); sframe();
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* UNDO BEAT: the TIE back */
            ok = ok && r4->step[2].time == ST_TIE && str_eq(sh.toast, "BEAT UNDONE");
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            grid_hit(r4, 0, 0, 1u);                     /* lane 0 on step 1, lane 1 on step 2, step 2 tied */
            grid_hit(r4, 1, 1, 1u);
            r4->step[1].time = ST_TIE;
            memset(r4->v, 0, sizeof r4->v);
            transport_req = 1; srender(6);
            for (v = 0; v < NVOICE; v++)
                held0 |= r4->v[v].active && r4->v[v].note == shell_lane_note[0];
            for (k = 0; k < 400u && r4->seq_idx < 2u; k++)
                srender(1);
            for (v = 0; v < NVOICE; v++)
                new1 |= r4->v[v].active && r4->v[v].note == shell_lane_note[1];
            transport_req = 2; srender(2);
            ok = ok && held0 && !new1;
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sframe();
            bad += check("RUDIMENT: SEQ held + a key marks its column SLIDE, TIE, plain (UNDO BEAT); a tied column starts nothing", ok);
        }
        {   /* logic lanes: FILL left of 0 on lanes 3 to 8 picks an op over the two lanes above (A l-1, B l-2); lanes 1
             * and 2 stop at 0; the op shows as FILL's value and is kept with a project */
            static const uint8_t WANT[7][4] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {0, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 1, 0}};
            uint8_t hit, acc, l8[sizeof P11_LANES_DEF];
            uint32_t op, ab, ok2 = 1;
            int16_t eu_keep[3][8];
            static step_t st_keep[NSTEP];
            memcpy(eu_keep, sh_eu, sizeof eu_keep);     /* (the lanes as the tests after expect them, put back) */
            memcpy(st_keep, trk[3].step, sizeof st_keep);
            const char *lab;
            char val[12];
            for (op = 1; op <= 5u; op++) {              /* AND OR XOR NOR A-B: the truth table, (a, b) = 00 01 10 11 */
                memset((void *)shell_lane_op_i, 0, sizeof shell_lane_op_i);
                shell_lane_op_i[0][2] = (uint8_t)op;
                for (ab = 0; ab < 4u; ab++) {
                    hit = (uint8_t)ab;                  /* A (lane 2, bit 1) = ab's high bit, B (lane 1, bit 0) its low */
                    acc = hit;
                    lane_logic(0, &hit, &acc, lane_ff[0]);
                    ok2 = ok2 && ((hit >> 2) & 1u) == WANT[op - 1u][ab];
                }
            }
            memset((void *)shell_lane_op_i, 0, sizeof shell_lane_op_i);   /* FLIP: every other A, B resets */
            shell_lane_op_i[0][2] = LOP_FLIP;
            lane_ff[0][2] = 0;
            {
                static const uint8_t A[6] = {1, 1, 1, 1, 0, 1}, B[6] = {0, 0, 0, 1, 0, 0}, F[6] = {1, 0, 1, 1, 0, 0};
                uint32_t q;
                for (q = 0; q < 6u; q++) {
                    hit = (uint8_t)(A[q] << 1 | B[q]);
                    acc = 0;
                    lane_logic(0, &hit, &acc, lane_ff[0]);
                    ok2 = ok2 && ((hit >> 2) & 1u) == F[q];
                }
            }
            shell_lane_op_i[0][2] = LOP_LATCH;          /* LATCH: from an A until a B */
            lane_ff[0][2] = 0;
            {
                static const uint8_t A[6] = {0, 1, 0, 0, 0, 0}, B[6] = {0, 0, 0, 1, 0, 0}, L[6] = {0, 1, 1, 0, 0, 0};
                uint32_t q;
                for (q = 0; q < 6u; q++) {
                    hit = (uint8_t)(A[q] << 1 | B[q]);
                    acc = 0;
                    lane_logic(0, &hit, &acc, lane_ff[0]);
                    ok2 = ok2 && ((hit >> 2) & 1u) == L[q];
                }
            }
            shell_lane_op_i[0][2] = LOP_OR;             /* a chain: lane 4 = lane 3 AND lane 2, lane 3 = lane 2 OR lane 1 */
            shell_lane_op_i[0][3] = LOP_AND;
            hit = 0x01; acc = 0x01;                     /* lane 1 (bit 0) only: OR gives lane 3, AND of 3 and 2 nothing */
            lane_logic(0, &hit, &acc, lane_ff[0]);
            ok2 = ok2 && (hit & 0x0Cu) == 0x04u && (acc & 0x04u);
            memset((void *)shell_lane_op_i, 0, sizeof shell_lane_op_i);
            sh_switch_pair(slot_of("FM6"), rn);         /* the knobs: lane 1 stops at 0, lane 4 turns to NOR */
            sframe();
            shome(); sframe();
            sh_sp[SP_LANE - SP_FIRST] = 0;
            sh_eu[1][0] = 0;
            sturn(EN_K3, -1);
            ok2 = ok2 && sh_eu[1][0] == 0;
            sh_sp[SP_LANE - SP_FIRST] = 3;
            sh_eu[1][3] = 2;                            /* emptying a lane stops at 0, however far it turns */
            sturn(EN_K3, -1); sturn(EN_K3, -1); sturn(EN_K3, -1); sturn(EN_K3, -1);
            ok2 = ok2 && sh_eu[1][3] == 0;
            sframes(SH_FILL0_MS + 100u);                /* a pause, then a fresh turn left: AND, OR, XOR, NOR */
            sturn(EN_K3, -1); sturn(EN_K3, -1); sturn(EN_K3, -1); sturn(EN_K3, -1);
            sframe();
            sh_param_text(2, &lab, val);
            ok2 = ok2 && sh_eu[1][3] == -4 && shell_lane_op_i[0][3] == LOP_NOR && str_eq(val, "NOR") &&
                  !(step_lanes(&trk[3].step[0]) & 8u);
            sh_lanes_get(0, l8);                        /* kept: 64 + op, back as -op */
            sh_eu[1][3] = 5;
            sh_lanes_set(0, l8);
            ok2 = ok2 && l8[8 + 3] == 68u && sh_eu[1][3] == -4;
            sh_eu[1][3] = 0;
            sh_euclid_lane(&trk[3], 3);
            sframe();
            ok2 = ok2 && !shell_lane_op_i[0][3];
            {   /* played: lane 3 NOR of kick (1 5 9 13) and snare (5 13) sounds on the 12 other steps of a loop;
                 * a start clears FLIP's and LATCH's state; a tap on a logic lane writes nothing */
                track_t *r4 = &trk[3];
                uint32_t q, v, ons = 0, seen[NVOICE] = {0}, last = 0xFFFFu, steps = 0, ln;
                for (q = 0; q < NSTEP; q++)
                    step_clear(&r4->step[q]);
                for (q = 0; q < 8u; q++) {
                    sh_eu[0][q] = 0; sh_eu[1][q] = 0; sh_eu[2][q] = 0; shell_lane_len[q] = 0;
                }
                for (q = 0; q < 16u; q += 4u) grid_hit(r4, q, 0, 1u);
                grid_hit(r4, 4, 1, 1u); grid_hit(r4, 12, 1, 1u);
                sh_eu[1][2] = -LOP_NOR;
                sh_sp[SP_LANE - SP_FIRST] = 2;
                skey((uint32_t)sh_white_key(3));        /* a tap on the logic lane: nothing written */
                ok2 = ok2 && !((step_lanes(&r4->step[3]) >> 2) & 1u) && str_eq(sh.toast, "LOGIC LANE: ITS HITS COME FROM ABOVE");
                sframe();
                ok2 = ok2 && shell_lane_op_i[0][2] == LOP_NOR && ((sh_lc_hit[1] >> 2) & 1u) && !((sh_lc_hit[4] >> 2) & 1u);
                ln = shell_lane_note[2];
                lane_ff[0][5] = 1;                      /* (stale state a start must clear) */
                memset(r4->v, 0, sizeof r4->v);
                transport_req = 1; srender(2);
                ok2 = ok2 && !lane_ff[0][5];
                for (q = 0; q < 20000u && steps < 16u; q++) {
                    srender(1);
                    if (r4->seq_idx != last) { last = r4->seq_idx; steps++; }
                    for (v = 0; v < NVOICE; v++)
                        if (r4->v[v].active && r4->v[v].note == ln && r4->v[v].age != seen[v]) {
                            seen[v] = r4->v[v].age;
                            ons++;
                        }
                }
                transport_req = 2; srender(2);
                printf("  (a NOR lane over a loop: %u hits)\n", ons);
                ok2 = ok2 && ons == 12u;
                for (q = 0; q < NSTEP; q++)
                    step_clear(&r4->step[q]);
                memset(r4->v, 0, sizeof r4->v);
            }
            memcpy(sh_eu, eu_keep, sizeof eu_keep);
            memcpy(trk[3].step, st_keep, sizeof st_keep);
            for (op = 0; op < 8u; op++)
                shell_lane_len[op] = (uint8_t)sh_lane_len(op);
            sframe();
            bad += check("RUDIMENT: logic lanes (FILL left of 0): AND OR XOR NOR A-B FLIP LATCH over the two lanes above, "
                         "chained; FILL stops at 0 until a pause; lanes 1-2 stop at 0; the op on the knob, kept with a project", ok2);
        }
        {   /* a hit's accent: on a hit of the lane in front SEQ held + its key cycles plain, ACCENT, SLIDE, ACCENT
             * SLIDE, TIE, plain; the accent is that lane's (velocity 127), one undo, kept with a project */
            track_t *r4 = &trk[3];
            uint32_t v, vel = 0, w2 = (uint32_t)sh_white_key(2);
            sh_switch_pair(slot_of("FM6"), rn);
            sframe();
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            grid_hit(r4, 2, 0, 1u); grid_hit(r4, 2, 1, 1u);   /* lanes 1 and 2 on step 3, lane 1 in front */
            sh_sp[SP_LANE - SP_FIRST] = 0;
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            skey(w2);
            ok = step_accents(&r4->step[2]) == 1u && !(r4->step[2].flags & SF_SLIDE) && str_eq(sh.toast, "ACCENT");
            skey(w2);
            ok = ok && !step_accents(&r4->step[2]) && (r4->step[2].flags & SF_SLIDE) && str_eq(sh.toast, "SLIDE");
            skey(w2);
            ok = ok && step_accents(&r4->step[2]) == 1u && (r4->step[2].flags & SF_SLIDE) &&
                 str_eq(sh.toast, "ACCENT SLIDE");
            sup(B_SEQ); sframe();
            ok = ok && project_save(1) == 0;
            step_clear(&r4->step[2]);
            project_load(1);
            ok = ok && step_accents(&r4->step[2]) == 1u && (r4->step[2].flags & SF_SLIDE) &&
                 (step_lanes(&r4->step[2]) & 3u) == 3u;
            sh_switch_pair(slot_of("FM6"), rn);
            sframe();
            sh_sp[SP_LANE - SP_FIRST] = 0;
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            skey(w2);
            ok = ok && !step_accents(&r4->step[2]) && !(r4->step[2].flags & SF_SLIDE) && r4->step[2].time == ST_TIE &&
                 str_eq(sh.toast, "TIE");
            skey(w2);
            ok = ok && !step_accents(&r4->step[2]) && r4->step[2].time != ST_TIE && str_eq(sh.toast, "PLAIN STEP");
            skey(w2);
            sup(B_SEQ); sframe();
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* UNDO BEAT: plain again */
            ok = ok && !step_accents(&r4->step[2]) && str_eq(sh.toast, "BEAT UNDONE");
            grid_acc(r4, 2, 0, 1u);                     /* the accented hit plays at 127, its neighbour at the velocity */
            memset(r4->v, 0, sizeof r4->v);
            transport_req = 1;
            for (k = 0; k < 4000u && !vel; k++) {      /* (until step 3's hits sound, wherever the loop starts) */
                srender(1);
                for (v = 0; v < NVOICE; v++)
                    if (r4->v[v].active && r4->v[v].note == shell_lane_note[0])
                        vel = r4->v[v].vel;
            }
            transport_req = 2; srender(2);
            ok = ok && vel == 127u && (step_lanes(&r4->step[2]) & 3u) == 3u && !(step_accents(&r4->step[2]) & 2u);
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sframe();
            bad += check("RUDIMENT: SEQ held + a key on a hit of the lane in front: ACCENT, SLIDE, ACCENT SLIDE, TIE, plain "
                         "(that lane's, 127, UNDO BEAT, kept with a project)", ok);
        }
        {   /* the marks stay through the grid's edits: a hit tapped into a SLIDE column, a lane knob's rewrite, an
             * erase; a TIE keeps its column's hits (shown, held through); a clear takes the marks; under DRUMS no mark */
            track_t *r4 = &trk[3];
            uint32_t w3 = (uint32_t)sh_white_key(3), w5 = (uint32_t)sh_white_key(5);
            sh_switch_pair(slot_of("FM6"), rn);
            sframe();
            for (k = 0; k < NSTEP; k++)
                step_clear(&r4->step[k]);
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            skey(w3);                                   /* SLIDE on the empty column 4 */
            skey(w5); skey(w5);                         /* TIE on column 6 */
            sup(B_SEQ); sframe();
            ok = (r4->step[3].flags & SF_SLIDE) && r4->step[5].time == ST_TIE;
            grid_hit(r4, 3, 0, 1u);                     /* a hit tapped into the SLIDE column */
            ok = ok && (r4->step[3].flags & SF_SLIDE) && (step_lanes(&r4->step[3]) & 1u);
            grid_hit(r4, 3, 0, 0u);                     /* .. and out again: the column empty, the mark kept */
            ok = ok && (r4->step[3].flags & SF_SLIDE);
            grid_hit(r4, 5, 1, 1u); grid_hit(r4, 5, 2, 1u);   /* hits in the TIE column: kept, shown */
            ok = ok && r4->step[5].time == ST_TIE && (step_lanes(&r4->step[5]) & 6u) == 6u;
            sh_sp[SP_LANE - SP_FIRST] = 0;
            shome(); sframe();
            sturn(EN_K4, 1);                            /* ROT on the lane in front: its column rewrite keeps marks */
            ok = ok && (r4->step[3].flags & SF_SLIDE) && r4->step[5].time == ST_TIE && (step_lanes(&r4->step[5]) & 6u) == 6u;
            sdown(B_REC); sframes(SH_TAP_MS + 50u);      /* REC + the step key: its hits gone, its TIE kept */
            skey(w5);
            sup(B_REC); sframe();
            ok = ok && r4->step[5].time == ST_TIE && !r4->step[5].hit;
            sh_clear();                                 /* REC's clear, every lane: the marks go with the pattern */
            ok = ok && !(r4->step[3].flags & SF_SLIDE) && r4->step[5].time != ST_TIE;
            sh_switch_pair(slot_of("DRUMS"), rn);       /* DRUMS: SEQ held + a key marks nothing but an accent */
            sframe();
            sh_sp[SP_LANE - SP_FIRST] = 0;
            grid_hit(&trk[3], 5, 0, 1u);
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
            skey(w3);
            ok = ok && !(trk[3].step[3].flags & SF_SLIDE) && trk[3].step[3].time != ST_TIE &&
                 str_eq(sh.toast, "ACCENT: ON A HIT OF THE LANE");
            skey(w5);
            ok = ok && (step_accents(&trk[3].step[5]) & 1u) && !(trk[3].step[5].flags & SF_SLIDE) && str_eq(sh.toast, "ACCENT");
            skey(w5);
            ok = ok && !step_accents(&trk[3].step[5]) && trk[3].step[5].time != ST_TIE && str_eq(sh.toast, "PLAIN STEP");
            sup(B_SEQ); sframe();
            for (k = 0; k < NSTEP; k++)
                step_clear(&trk[3].step[k]);
            sframe();
            bad += check("RUDIMENT: SLIDE and TIE stay through a tap, a lane knob and an erase; a TIE keeps its hits; "
                         "a clear takes them; under DRUMS only an accent", ok);
        }
        {   /* DRUMS' DRUM page: the drum in front's own TUNE DECY TONE SNAP, on the part; under KEYS the drum struck
             * last is in front; the SOUND keeps them over a pair switch, and a first visit starts as designed */
            uint32_t dr = slot_of("DRUMS"), kn = note_of("KEYS"), part, g;
            int16_t d;
            sh_switch_pair(dr, kn);
            sframe();
            part = sh.part;
            memset(drum_lane_d[part], 0, sizeof drum_lane_d[0]);
            sh_sp[SP_LANE - SP_FIRST] = DV_BELL;        /* (a lanes engine's lane in front: left alone) */
            last_note = 36; sframe();
            last_note = 38; sframe();                   /* a snare from the keys: the snare in front */
            ok = sh_kdrum == DV_SNARE && sh_sp[SP_LANE - SP_FIRST] == DV_BELL;
            for (g = 0; g < 24u && sh.page[sh.mode] != SHP_DRUM; g++)
                stap(B_EDIT);
            ok = ok && sh.page[sh.mode] == SHP_DRUM && str_eq(sh_page_name(), "DRUM");
            sturn(EN_K2, 10);                           /* DECY */
            d = drum_lane_d[part][DRL_DECY][DV_SNARE];
            ok = ok && d > 0 && drum_lane_d[part][DRL_DECY][DV_KICK] == 0;
            sh_switch_pair(slot_of("FM6"), kn);
            sframe();
            sh_switch_pair(dr, kn);
            sframe();
            ok = ok && drum_lane_d[sh.part][DRL_DECY][DV_SNARE] == d;
            {   /* saved as a sound (record version 6, no pattern), the drums come back with it; a kit-only sound clears them */
                uint32_t k = UP_SLOTS - 2u;
                int rc;
                up_rec_t keep;
                memcpy(&keep, up_rec(k), sizeof keep);
                drum_lane_d[sh.part][DRL_TUNE][DV_HATC] = -20;
                rc = up_store(k, "DRM");
                ok = ok && (rc == 0 || rc == 3) && up_rec(k)->ver == UP_VER_DRL && up_pat_empty(up_rec(k));
                memset(drum_lane_d[sh.part], 0, sizeof drum_lane_d[0]);
                ok = ok && !up_load(k) && drum_lane_d[sh.part][DRL_DECY][DV_SNARE] == d &&
                     drum_lane_d[sh.part][DRL_TUNE][DV_HATC] == -20 && drum_lane_d[sh.part][DRL_TONE][DV_KICK] == 0;
                up_rec(k)->ver = UP_VER;                /* the same sound kept by stock Felucca: the kit only */
                ok = ok && !up_load(k) && drum_lane_d[sh.part][DRL_DECY][DV_SNARE] == 0;
                memcpy(up_rec(k), &keep, sizeof keep);
            }
            drum_lane_d[sh.part][DRL_DECY][DV_SNARE] = 0;
            shome();
            sframe();
            bad += check("DRUMS' DRUM page: the drum struck last in front, its DECY its own, kept over a pair switch and in a "
                         "saved sound", ok);
            {   /* under RUDIMENT the page sets the piece the lane in front strikes: a lane whose NOTE is a kick sets
                 * the kick, and says KICK */
                track_t *r4 = &trk[3];
                sh_switch_pair(dr, note_of("RUDIMENT"));
                sframe();
                sh_lnote[2] = 36;                       /* lane 3 (CLAP) plays a kick */
                sh_sp[SP_LANE - SP_FIRST] = 2;
                sframe();
                for (g = 0; g < 24u && sh.page[sh.mode] != SHP_DRUM; g++)
                    stap(B_EDIT);
                memset(drum_lane_d[r4 - trk], 0, sizeof drum_lane_d[0]);
                sturn(EN_K1, 4);                        /* TUNE */
                ok = sh.page[sh.mode] == SHP_DRUM && drum_lane_d[r4 - trk][DRL_TUNE][DV_KICK] > 0 &&
                     drum_lane_d[r4 - trk][DRL_TUNE][DV_CLAP] == 0 && sh_drum_piece() == DV_KICK;
                memset(drum_lane_d[r4 - trk], 0, sizeof drum_lane_d[0]);
                sh_lnote[2] = 0;
                shome();
                sframe();
                bad += check("DRUMS' DRUM page under RUDIMENT: the piece the lane strikes (a lane set to a kick sets the kick)", ok);
            }
        }
        {   /* a track's pair replaced by another and brought back is as it was (its SOUND); no snapshot crosses */
            uint32_t dr = slot_of("DRUMS"), fm = slot_of("FM6"), lz = slot_of("LIZARD"), kn = note_of("KEYS");
            uint32_t an = note_of("ALIVE");
            track_t *r4 = &trk[3];
            sh_switch_pair(dr, rn);
            sframe();
            sh_switch_pair(fm, rn);                     /* FM6 + RUDIMENT: BEAT plays FM6 now */
            sframe();
            ok = r4->eng_req == ENGI_FM6;
            sh_switch_pair(fm, kn);
            sframe();
            sh_switch_pair(dr, rn);                     /* DRUMS + RUDIMENT back: DRUMS again */
            sframe();
            ok = ok && sh.mode == dr && r4->eng_req == ENGI_DRUM && !r4->p[P_MUTE];
            sh_switch_pair(fm, kn);                     /* .. left: it plays on, DRUMS' snapshot stays DRUMS */
            sframe();
            ok = ok && sh_snap_of(dr)->eng == ENGI_DRUM && r4->eng_req == ENGI_DRUM && !r4->p[P_MUTE];
            sh_wnoteslot[2] = (uint8_t)an;
            sh_snap_of(lz)->valid = 0;                      /* (LIZARD fresh: its own engine, whatever ran before) */
            sh_switch_pair(lz, an);
            sframe();
            sh_switch_pair(fm, SH_N_LINES);             /* LINE: FM6 + LINES replaces LIZARD + ALIVE */
            sframe();
            ok = ok && trk[2].eng_req == ENGI_FM6;
            sh_switch_pair(lz, an);
            sframe();
            ok = ok && trk[2].eng_req == ENGI_LIZARD;
            sh_switch_pair(fm, kn);
            sh_wnoteslot[2] = SH_N_LINES;
            sframe();
            bad += check("tracks: a pair replaced on its track and brought back is as it was (DRUMS + RUDIMENT, LIZARD + "
                         "ALIVE)", ok);
        }
        {   /* the undo is the pair's that kept it: a beat kept under LINES is not undone onto ALIVE on the same part */
            uint32_t fm = slot_of("FM6"), an = note_of("ALIVE");
            step_t keep[NSTEP];
            int16_t eu[8];
            sh_switch_pair(fm, SH_N_LINES);
            sframe();
            sh_sp[SP_RHY - SP_FIRST] = 0;
            snote();
            sturn(EN_ALGO, 1);                          /* a rhythm: LINES' beat kept (UNDO BEAT) */
            ok = sh_take.have && sh_take.rhy && sh_take.part == 2u;
            sh_switch_pair(fm, an);                     /* ALIVE on the same part */
            sframe();
            memcpy(keep, trk[2].step, sizeof keep);
            memcpy(eu, sh_eu_i[0][0], sizeof eu);
            sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
            ok = ok && str_eq(sh.toast, "NO TAKE TO UNDO") && !memcmp(keep, trk[2].step, sizeof keep) &&
                 !memcmp(eu, sh_eu_i[0][0], sizeof eu);
            sh_switch_pair(fm, SH_N_LINES);             /* back on LINES: its undo is there */
            sframe();
            ok = ok && sh_take_here();
            sh_switch_pair(fm, note_of("KEYS"));
            sframe();
            bad += check("undo: a beat kept under LINES is not undone onto ALIVE on the same part, nor into RUDIMENT", ok);
        }
        {   /* LINES away for a while, then back: its lanes come back in time with the loop and RUDIMENT behind */
            uint32_t fm = slot_of("FM6"), dr = slot_of("DRUMS"), kn = note_of("KEYS"), l, same = 1;
            sh_switch_pair(dr, rn);
            sframe();
            sh_switch_pair(fm, SH_N_LINES);
            sframe();
            trk[2].p[P_SLEN] = 16; trk[3].p[P_SLEN] = 16;
            for (l = 0; l < 8u; l++) {
                sh_eu_i[0][0][l] = sh_eu_i[1][0][l] = 16;
                shell_lane_len_i[0][l] = shell_lane_len_i[1][l] = 16;
            }
            transport_req = 1; srender(40);
            sh_switch_pair(fm, kn);                     /* away for a few steps */
            sframe();
            srender(300);
            sh_switch_pair(fm, SH_N_LINES);             /* back */
            sframe();
            for (l = 0; l < 8u; l++)
                same &= shell_lane_pos_i[1][l] == shell_lane_pos_i[0][l];
            transport_req = 2; srender(2);
            sh_switch_pair(fm, kn);
            sframe();
            bad += check("LINES back after a while away: its lanes in time with RUDIMENT's, not where they stopped", same);
        }
        for (k = 0; k < NSTEP; k++)
            step_clear(&trk[3].step[k]);
        sgo(slot_of("FM6"));
    }

    {   /* HOME and a key pressed together (well under SH_TAP_MS): the key picks its slot */
        uint32_t liz = slot_of("LIZARD");
        sgo(slot_of("FM6"));
        sdown(B_HOME); sframe();
        skey((uint32_t)sh_slot_key(liz));
        sup(B_HOME); sframe();
        bad += check("HOME + a key within a frame: the wheel opens and the key picks LIZARD", sh.mode == liz);
        sgo(slot_of("FM6"));
    }

    {   /* GLO held, REC held on to read its card: no ring, no clear, no arm */
        track_t *t = sh_trk();
        uint32_t k, n = 0;
        song.rec = 0;
        sdown(B_SEQ); sframes(SH_TAP_MS + 100u);
        skey((uint32_t)sh_white_key(2));                /* (a step to lose) */
        sup(B_SEQ); sframe();
        sdown(B_GLO); sframes(SH_TAP_MS + 100u);
        sdown(B_REC); sframes(SH_LONG_MS + 300u);
        ok = sh.help == B_REC + 1u;
        sup(B_REC); sframe();
        sup(B_GLO); sframe();
        for (k = 0; k < NSTEP; k++)
            n += t->step[k].n != 0;
        bad += check("GLO + REC held to read its card: the pattern stays, REC is not armed", ok && n > 0 && !song.rec);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
    }

    {   /* LEDs: ARP HOLD on breathes (it waits on you); breathing beats the button's resting glow */
        uint8_t pos0[sizeof led_pos];
        uint32_t q;
        int16_t h;
        sgo2(slot_of("FM6"), note_of("KEYS"));          /* (KEYS: the keys' latch) */
        sh_trk()->p[P_VOICE] = V_POLY; sh_trk()->p[P_AMODE] = 0; srender(4);
        h = sh_trk()->p[P_AHOLD];
        memcpy(pos0, led_pos, sizeof pos0);
        memset(led_pos, 0xFF, sizeof led_pos);
        led_pos[panel.btn[B_ARP]] = 0u << 3 | 3u;
        q = led_pos[panel.btn[B_ARP]];
        shome();
        sh_trk()->p[P_AHOLD] = 1;
        shell_leds();                                   /* HOLD on, nothing latched yet: armed, MID */
        ok = (fm1_led_mid[q >> 3] >> (q & 7u)) & 1u;
        ok = ok && !((fm1_led_dim[q >> 3] >> (q & 7u)) & 1u) && !((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u);
        input_on(sh_trk(), 60, 100); srender(2); input_off(sh_trk(), 60); srender(2);
        shell_leds();                                   /* a note latched, no key down: it plays by itself */
        ok = ok && (fm1_led_breath[q >> 3] >> (q & 7u)) & 1u;
        ok = ok && !((fm1_led_dim[q >> 3] >> (q & 7u)) & 1u) && !((fm1_led_mid[q >> 3] >> (q & 7u)) & 1u);
        sh_trk()->p[P_AHOLD] = 0; srender(4);
        shell_leds();                                   /* MODE OFF, HOLD off: LOW */
        ok = ok && sh_trk()->p[P_AMODE] == 0 && (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u;
        ok = ok && !((fm1_led_mid[q >> 3] >> (q & 7u)) & 1u) && !((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u);
        sh_trk()->p[P_AHOLD] = h;
        memcpy(led_pos, pos0, sizeof pos0);
        shell_leds();
        bad += check("LEDs: ARP LOW off, MID armed (HOLD waiting), BREATH when a latch plays by itself", ok);
    }

    {   /* ARP held: with MODE OFF it toggles HOLD too (the keys latch); a knob turned with a finger on ARP leaves HOLD */
        track_t *t;
        int16_t m, h;
        uint32_t k;
        shome();
        t = sh_trk();
        m = t->p[P_AMODE]; h = t->p[P_AHOLD];
        t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();
        ok = t->p[P_AHOLD] == 1;
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();
        ok = ok && !t->p[P_AHOLD];
        t->p[P_AMODE] = 1;
        sdown(B_ARP); sframe();
        sturn(EN_K1, 1);
        sframes(SH_TAP_MS + 100u); sup(B_ARP); sframe();
        ok = ok && !t->p[P_AHOLD];
        t->p[P_AMODE] = m; t->p[P_AHOLD] = h;
        for (k = 0; k < NTRK; k++)
            if (&trk[k] != t)
                trk[k].p[P_AHOLD] = 0;
        shome();
        bad += check("ARP held: MODE OFF toggles HOLD (the keys latch); a knob turned while resting on ARP leaves HOLD alone",
                     ok);
    }

    {   /* a toast raised in the wheel is drawn there: PLAY on a pair that cannot run behind says so */
        static uint16_t before[240 * 20];
        uint32_t k, diff = 0;
        sgo(slot_of("FM6"));
        sdown(B_HOME); sframes(SH_TAP_MS + 400u);
        memcpy(before, host_screen + 130 * 240, sizeof before);
        stap(B_PLAY);
        sframes(100);
        for (k = 0; k < 240u * 20u; k++)
            diff += host_screen[130 * 240 + k] != before[k];
        stap(B_PLAY);                                   /* (and back: unmuted) */
        sup(B_HOME); sframe();
        sframes(SH_TOAST_MS + 100u);
        bad += check("wheel: a toast raised in it (KEYS MUTED) is drawn under its centre; PLAY again unmutes",
                     diff > 200u && !sh_tmute);
    }

    {   /* TAB: the views. The SOUND's picture on every SOUND page, also under LINES and RUDIMENT; HOME twice: the pair
         * before in the view in front; SELECT walks the view's own pages */
        uint32_t sk = slot_of("SHOCK"), dr = slot_of("DRUMS"), ln = note_of("LINES"), rn = note_of("RUDIMENT"), k;
        uint32_t okp = 1, okd, oks = 1, q;
        static step_t keep[2][NSTEP];
        sh_snap_of(sk)->valid = 0;                          /* (SHOCK fresh: a test before left its slot on another engine) */
        sgoc(sk, ln);
        shome();
        sh.page[sh.mode] = SHP_HOME; sframe();
        sh_draw_picture();
        okp = sh_pic_eng == ENGI_SHOCK;
        for (k = 0; k < 2u; k++) {                      /* EDIT, ENV: SHOCK still */
            stap(k ? B_ENV : B_EDIT);
            sh_draw_picture();
            okp = okp && sh_pic_eng == ENGI_SHOCK;
        }
        sframes(400);
        stap(B_HOME);                                   /* NOTE: LINES' rows */
        sh_draw_picture();
        okp = okp && sh_view_note() && sh.page[sh.mode] == SHP_NOTE && sh_pic_eng == SH_NONE;
        sframes(400);
        stap(B_HOME);                                   /* back: ENV, where it was left */
        okp = okp && sh.page[sh.mode] == SHP_ENV;
        sframes(400);
        sgoc(dr, rn);
        sh.page[sh.mode] = SHP_HOME; sframe();
        sh_draw_picture();
        okp = okp && sh_pic_eng == ENGI_DRUM;
        bad += check("TAB: the SOUND's picture on SOUND MAIN, EDIT and ENV under LINES (SHOCK) and RUDIMENT (DRUMS); NOTE: the lanes'",
                     okp);
        memcpy(keep, trk[0].step, sizeof keep[0]);      /* (no take left looping on PLAY: only the two tracks walk; */
        memcpy(keep[1], trk[1].step, sizeof keep[1]);   /* the takes back after) */
        for (k = 0; k < 2u; k++)
            for (q = 0; q < NSTEP; q++)
                step_clear(&trk[k].step[q]);
        sframes(400);
        stap(B_HOME);                                   /* DRUMS > RUDIMENT, NOTE view */
        okd = sh_view_note();
        sframes(400);
        stap(B_HOME); stap(B_HOME);                     /* twice: SHOCK > LINES, still NOTE (the flip undone) */
        sframes(100);
        okd = okd && sh.mode == sk && sh.note == ln && sh_view_note() && sh.page[sh.mode] == SHP_NOTE;
        sframes(400);
        stap(B_HOME); stap(B_HOME);                     /* and back, NOTE still */
        sframes(100);
        okd = okd && sh.mode == dr && sh.note == rn && sh_view_note();
        sframes(400);
        stap(B_HOME);                                   /* SOUND, then twice: SHOCK > LINES in SOUND, at ENV */
        sframes(400);
        stap(B_HOME); stap(B_HOME);
        sframes(100);
        okd = okd && sh.mode == sk && !sh_view_note() && sh.page[sh.mode] == SHP_ENV;
        memcpy(trk[0].step, keep[0], sizeof keep[0]);
        memcpy(trk[1].step, keep[1], sizeof keep[1]);
        bad += check("TAB: HOME twice is the pair before in the same view, each where it was left", okd);
        for (k = 0; k < 2u * SHP_N; k++) {              /* SELECT round the SOUND view, then the NOTE view */
            sturn(EN_SELECT, 1);
            oks = oks && !sh_page_note(sh.page[sh.mode]) && sh_page_ok(sh.page[sh.mode]);
        }
        sframes(400);
        stap(B_HOME);
        for (k = 0; k < 2u * SHP_N; k++) {
            sturn(EN_SELECT, -1);
            oks = oks && sh_page_note(sh.page[sh.mode]) && sh_page_ok(sh.page[sh.mode]);
        }
        bad += check("SELECT walks the view's pages alone (SOUND: MAIN EDIT ENV LFO FX; NOTE: MAIN ARP SEQ)", oks);
        {   /* the wheel picks the view: another NOTE engine its NOTE view (RUDIMENT: ALGORITHM the rhythm), a SOUND
             * alone (or KEYS) the SOUND view; HOME twice from a shell page opens the pair before on its view */
            uint32_t okw;
            sframes(400);
            sgoc(slot_of("FM6"), 0);
            shome();
            sframes(400);
            sgo2(sh_mode_pos(dr), rn);                  /* the wheel: DRUMS > RUDIMENT, from SOUND MAIN */
            okw = sh_view_note() && sh.page[sh.mode] == SHP_NOTE && sh_hero_id() == SP_RHY;
            sframes(400);
            sgoc(sk, rn);                               /* another SOUND alone: SHOCK under RUDIMENT, its SOUND view */
            okw = okw && !sh_view_note() && sh_hero_id() == SH_MODES[sk].hero;
            sframes(400);
            sgoc(sk, 0);                                /* KEYS: the SOUND view */
            okw = okw && !sh_view_note();
            sframes(400);
            sdown(B_GLO); sframe(); sup(B_GLO); sframe();   /* SYSTEM, then HOME twice: SHOCK > RUDIMENT, no shell page */
            stap(B_HOME); stap(B_HOME);
            sframes(100);
            okw = okw && sh.global == SHG_NONE && sh.mode == sk && sh.note == rn && !sh_view_note();
            bad += check("wheel: a new NOTE engine opens its NOTE view (RUDIMENT: ALGORITHM picks rhythms), a SOUND alone "
                         "or KEYS the SOUND view; HOME twice from SYSTEM opens the pair before, not SYSTEM", okw);
        }
        sframes(400);
        stap(B_HOME);
        sgoc(slot_of("FM6"), 0);
        shome();
    }
#define NOTCH(r_, s_) do { sturn((r_), (s_)); sframes(200); } while (0)
    {   /* LINES' LINE page: TRN moves the line as it plays (not its steps), kept with a project, no note hung by a turn
         * mid-note; STYLE: five ways to draw over the same seed, each the same line again for a seed */
        uint32_t fm = slot_of("FM6"), ln = note_of("LINES"), k, st, sd, v, okt, oks = 1, oku, sig0, sig1, n0, hung = 0, pre;
        uint8_t w[SH_LWR_MAX], w2[SH_LWR_MAX], ws[5][SH_LWR_MAX];
        track_t *t;
        uint8_t ids[4];
        int glob;
        sgoc(fm, ln);
        t = sh_trk();
        t->p[P_SLEN] = 16;
        sh_sp[SP_LSTY - SP_FIRST] = 0; sh_sp[SP_LTRN - SP_FIRST] = 0;
        sh_sp[SP_RHY - SP_FIRST] = 7;
        sh_lwr_load(t, 7u); sframe();
        sh.page[sh.mode] = SHP_LINE; sframe();
        sh_page_ids(ids, &glob);
        okt = ids[2] == SP_LTRN && ids[3] == SP_LSTY;
        for (sig0 = 0, k = 0; k < NSTEP; k++) sig0 = sig0 * 31u + step_lanes(&t->step[k]);
        n0 = shell_lane_note_i[1][0];
        for (k = 0; k < 5u; k++) NOTCH(EN_K3, 1);       /* TRN +5 */
        sframe();
        for (sig1 = 0, k = 0; k < NSTEP; k++) sig1 = sig1 * 31u + step_lanes(&t->step[k]);
        okt = okt && sh_sp[SP_LTRN - SP_FIRST] == 5 && sig1 == sig0 && shell_lane_note_i[1][0] == n0 + 5u;
        okt = okt && project_save(1) == 0;
        sh_sp[SP_LTRN - SP_FIRST] = 0;
        project_load(1); sframes(50);
        okt = okt && sh_sp[SP_LTRN - SP_FIRST] == 5;
        sgoc(fm, ln);
        t = sh_trk();
        memset(t->v, 0, sizeof t->v);
        transport_req = 1;                              /* playing: TRN turned while notes sound, back and forth */
        for (k = 0; k < 400u; k++) {
            srender(1);
            if (k % 7u == 3u) { sh.page[sh.mode] = SHP_LINE; NOTCH(EN_K3, k % 14u < 7u ? 1 : -1); }
        }
        transport_req = 2; srender(200);
        for (v = 0; v < NVOICE; v++)
            hung += t->v[v].active && t->v[v].gate;
        okt = okt && !hung;
        printf("  (TRN: the knob 1, steps kept, lane 1 %u -> %u, a project 1, hung %u)\n", n0, shell_lane_note_i[1][0], hung);
        {   /* a lane note held long, TRN turned under it: the note that was turned on is the one let go */
            uint32_t was = 0, still = 0, n1, lk;
            for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
            grid_hit(t, 0, 0, 1u);                      /* lane 1 on step 1 alone, a long gate */
            t->p[P_SGATE] = 120;
            sh_sp[SP_LTRN - SP_FIRST] = 0; sframe();
            memset(t->v, 0, sizeof t->v);
            transport_req = 1; srender(3);
            n1 = shell_lane_note_i[1][0];
            for (v = 0; v < NVOICE; v++)
                was += t->v[v].active && t->v[v].gate && t->v[v].note == n1;
            sh.page[sh.mode] = SHP_LINE; sframe();
            for (k = 0; k < 7u; k++) { sturn(EN_K3, 1); sframe(); }   /* TRN +7 mid-note */
            for (lk = 0; lk < 120u; lk++) srender(1);  /* (past the step's gate, before the loop comes round) */
            for (v = 0; v < NVOICE; v++)
                still += t->v[v].active && t->v[v].gate && t->v[v].note == n1;
            transport_req = 2; srender(4);
            okt = okt && was == 1u && !still && shell_lane_note_i[1][0] != n1;
            printf("  (TRN mid-note: the old pitch sounding %u, still gated after its gate %u)\n", was, still);
            sh_sp[SP_LTRN - SP_FIRST] = 0; t->p[P_SGATE] = 64;
        }
        bad += check("LINES: TRN on LINE moves the line by semitones as it plays, the steps kept; a project keeps it; turned "
                     "mid-note, no note hangs and the note let go is the one turned on", okt);
        sh_sp[SP_LTRN - SP_FIRST] = 0;
        for (sd = 1; sd < 256u; sd += 37u)              /* each STYLE: the same line for a seed, and its own shape */
            for (st = 0; st < 5u; st++) {
                uint32_t c = 0, last = 0, okst = 1;
                sh_lwr_draw(sd, 16u, 10u, st, w);
                sh_lwr_draw(sd, 16u, 10u, st, w2);
                okst = !memcmp(w, w2, sizeof w);
                memcpy(ws[st], w, sizeof w);
                for (k = 0; k < 16u; k++) {
                    uint32_t d = (uint32_t)w[k] >> 4;
                    if (!(w[k] & SH_LW_ON))
                        continue;
                    okst = okst && (st != SH_LS_OCTAVE || d == 0u || d == 4u || d == 7u) &&
                           (st != SH_LS_ARP || d == 0u || d == 2u || d == 4u || d == 7u) &&
                           (st != SH_LS_PEDAL || c % 2u || d == 0u);
                    c++;
                    last = d;
                }
                okst = okst && c == 10u && (st != SH_LS_CALL || last == 0u);
                oks = oks && okst;
                if (st && !memcmp(ws[st], ws[0], sizeof w))
                    oks = 0;                            /* (each style its own line) */
            }
        bad += check("LINES: STYLE WALK OCTAVE PEDAL ARP CALL, each the same line again for a seed, DENS's steps kept; "
                     "OCTAVE root fifth octave, ARP the chord, PEDAL the root between, CALL ends home", oks);
        sframes(SH_TOAST_MS + 200u);
        snote();
        for (pre = 0, k = 0; k < NSTEP; k++) pre = pre * 31u + step_lanes(&t->step[k]);
        sturn(EN_ALGO, 1); sframes(300);                /* a LINE by ALGORITHM (the project's load left none) */
        for (sig0 = 0, k = 0; k < NSTEP; k++) sig0 = sig0 * 31u + step_lanes(&t->step[k]);
        oku = sh_sp[SP_RHY - SP_FIRST] != 0 && sig0 != 0u;
        sh.page[sh.mode] = SHP_LINE; sframes(SH_TOAST_MS + 200u);
        NOTCH(EN_K4, 1);                                /* STYLE OCTAVE: the line drawn again */
        for (sig1 = 0, k = 0; k < NSTEP; k++) sig1 = sig1 * 31u + step_lanes(&t->step[k]);
        oku = oku && sh_sp[SP_LSTY - SP_FIRST] == SH_LS_OCTAVE && sig1 != sig0 && sh.toast_undo;
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* REC's first ring: the beat before the run */
        for (sig1 = 0, k = 0; k < NSTEP; k++) sig1 = sig1 * 31u + step_lanes(&t->step[k]);
        oku = oku && sig1 == pre;
        bad += check("LINES: STYLE turned draws the line again; REC's first ring gives back the beat before the run of turns", oku);
        sh_sp[SP_LSTY - SP_FIRST] = 0; sh_sp[SP_RHY - SP_FIRST] = 0;
        sh_lwr_load(t, 0u);
        sgoc(fm, 0);
        shome();
    }
    {   /* LANE 2 (Pam's): PAD's cycle and hits; LOOP's chances bar for bar, OCT+ new rolls; WAKE NAP from PLAY on,
         * across a queued switch; all of it with a project */
        uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), k, l, okp, okl, okw, oks, note, b;
        uint16_t m1[8], m2[8], m3[8], mw[8];
        static project_store_t keep1;
        track_t *t;
        uint8_t ids[4];
        int glob;
        sgoc(dr, rn);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        memcpy(&keep1, &proj_slot[1], sizeof keep1);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep[0], 0, sizeof shell_lstep[0]);
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = sh_eu[1][l] = sh_eu[2][l] = 0; sh_ediv[l] = 0; shell_lane_len[l] = 0;
            sh_lx[SH_LX_PAD][l] = sh_lx[SH_LX_LOOP][l] = sh_lx[SH_LX_NAP][l] = 0; sh_lx[SH_LX_WAKE][l] = 1;
        }
        t->p[P_SLEN] = 16;
        sh_sp[SP_LANE - SP_FIRST] = 0;
        sh_eu[0][0] = 5; sh_eu[1][0] = 3; sh_euclid_lane(t, 0);   /* KICK: 3 over 5 */
        sh.page[sh.mode] = SHP_LANE2; sframe();
        sh_page_ids(ids, &glob);
        okp = ids[0] == SP_LPAD && ids[1] == SP_LLOOP && ids[2] == SP_LWAKE && ids[3] == SP_LNAP && sh_li == 0u;
        for (k = 0; k < 3u; k++) NOTCH(EN_K1, 1);       /* PAD 3: the cycle 8, the hits on 1 3 5 */
        for (b = 0, k = 0; k < 16u; k++) b |= (step_lanes(&t->step[k]) & 1u) << k;
        okp = okp && sh_lx[SH_LX_PAD][0] == 3 && shell_lane_len[0] == 8u && sh_lane_len(0) == 8u && b == 0x15u;
        sh_eu[1][0] = 2; sh_euclid_lane(t, 0);          /* (FILL 2 over the span of 5: 1 and 4) */
        for (b = 0, k = 0; k < 16u; k++) b |= (step_lanes(&t->step[k]) & 1u) << k;
        okp = okp && b == 0x09u;
        sh_eu[1][0] = 3; sh_euclid_lane(t, 0);
        sframes(SH_TOAST_MS + 200u);
        NOTCH(EN_K1, 1);                                /* PAD 4, then REC's first ring: the lane before the run of turns */
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        for (b = 0, k = 0; k < 16u; k++) b |= (step_lanes(&t->step[k]) & 1u) << k;
        okp = okp && sh_lx[SH_LX_PAD][0] == 0 && shell_lane_len[0] == 5u && str_eq(sh.toast, "BEAT UNDONE") && b == 0x15u;
        bad += check("LANE 2: PAD 3 on KICK's 3 over 5 makes its cycle 8, the hits grouped on 1 3 5; REC's ring undoes it",
                     okp);
        sh_eu[0][0] = 0; sh_eu[1][0] = 16; sh_lx[SH_LX_PAD][0] = 0; shell_lane_len[0] = 0;
        sh_euclid_lane(t, 0);                           /* KICK on every step, CHNC 50 % each */
        for (k = 0; k < NSTEP; k++) shell_lstep[0][0][k] = 10;
        sframe();
        note = shell_lane_note_i[0][0];
        memset(m1, 0, sizeof m1); memset(m2, 0, sizeof m2); memset(m3, 0, sizeof m3); memset(mw, 0, sizeof mw);
        transport_req = 1;
        lane_bars(t, note, 3u, m1);                     /* LOOP OFF: each bar its own */
        NOTCH(EN_K2, 1);                                /* LOOP 1 BAR, in bar 4: its rolls from now on */
        lane_bars(t, note, 7u, m1);
        okl = sh_lx[SH_LX_LOOP][0] == 1 && (m1[0] != m1[1] || m1[1] != m1[2]) && m1[4] == m1[5] && m1[5] == m1[6] &&
              m1[4] == m1[3] && m1[4] && m1[4] != 0xFFFFu;
        stap(B_OCTUP);                                  /* OCT+: new rolls from the next loop */
        lane_bars(t, note, 8u, m2);
        transport_req = 2; srender(4); sframe();
        memset(m2, 0, sizeof m2);
        transport_req = 1;                              /* (PLAY again: the loop from its seed) */
        lane_bars(t, note, 3u, m2);
        okl = okl && m2[0] == m2[1] && m2[1] == m2[2] && m2[0] != m1[4];
        transport_req = 2; srender(4); sframe();
        printf("  (LOOP: free %04x %04x %04x, looped %04x %04x %04x, new rolls %04x %04x)\n", m1[0], m1[1], m1[2], m1[4],
               m1[5], m1[6], m2[0], m2[1]);
        bad += check("LANE 2: LOOP repeats the lane's chances bar for bar from the bar it was turned on; OCT+ new rolls, "
                     "repeating too", okl);
        for (k = 0; k < NSTEP; k++) shell_lstep[0][0][k] = 0;   /* WAKE 2 NAP 1, LOOP OFF: two bars on, one off */
        sh_lx[SH_LX_LOOP][0] = 0;
        NOTCH(EN_K3, 1);
        NOTCH(EN_K4, 1);
        okw = sh_lx[SH_LX_WAKE][0] == 2 && sh_lx[SH_LX_NAP][0] == 1 && str_eq(sh.toast, "KICK PLAYS 2, RESTS 1");
        okw = okw && project_save(1) == 0;              /* (slot B the same: a queued switch keeps the count) */
        transport_req = 1;
        lane_bars(t, note, 2u, mw);
        sh_q.picking = 1; sh_q.pick = 1; sh_q_commit(); sframe();
        lane_bars(t, note, 6u, mw);
        okw = okw && !sh_q.mask && mw[0] == 0xFFFFu && mw[1] == 0xFFFFu && !mw[2] && mw[3] == 0xFFFFu && mw[4] == 0xFFFFu &&
              !mw[5];
        transport_req = 2; srender(4); sframe();
        memset(mw, 0, sizeof mw);
        transport_req = 1;                              /* PLAY again: the count from its start */
        lane_bars(t, note, 3u, mw);
        okw = okw && mw[0] == 0xFFFFu && mw[1] == 0xFFFFu && !mw[2];
        transport_req = 2; srender(4); sframe();
        printf("  (WAKE 2 NAP 1: bars %04x %04x %04x, after PLAY %04x %04x %04x)\n", mw[0], mw[1], mw[2], mw[0], mw[1], mw[2]);
        bad += check("LANE 2: WAKE 2 NAP 1 plays two bars and rests one, counted from PLAY, across a queued switch", okw);
        sh_lx[SH_LX_PAD][1] = 2; sh_lx[SH_LX_LOOP][1] = 3; sh_lx[SH_LX_WAKE][1] = 4; sh_lx[SH_LX_NAP][1] = 5;
        sh_eu[0][1] = 6; shell_lane_seed_i[0][1] = 0xC0FFEE11u;
        oks = project_save(1) == 0;
        sh_lx[SH_LX_PAD][1] = sh_lx[SH_LX_LOOP][1] = sh_lx[SH_LX_NAP][1] = 0; sh_lx[SH_LX_WAKE][1] = 1;
        shell_lane_seed_i[0][1] = 0; sh_eu[0][1] = 0;
        project_load(1); sframes(50);
        oks = oks && sh_lx_i[0][SH_LX_PAD][1] == 2 && sh_lx_i[0][SH_LX_LOOP][1] == 3 && sh_lx_i[0][SH_LX_WAKE][1] == 4 &&
              sh_lx_i[0][SH_LX_NAP][1] == 5 && shell_lane_seed_i[0][1] == 0xC0FFEE11u && shell_lane_len_i[0][1] == 8u &&
              sh_lx_i[0][SH_LX_WAKE][0] == 2 && sh_lx_i[0][SH_LX_NAP][0] == 1;
        bad += check("LANE 2: PAD LOOP WAKE NAP and LOOP's seed kept with a project (the lanes' tag)", oks);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        for (l = 0; l < 8u; l++) {
            sh_eu_i[0][0][l] = sh_eu_i[0][1][l] = sh_eu_i[0][2][l] = 0; shell_lane_len_i[0][l] = 0;
            sh_lx_i[0][SH_LX_PAD][l] = sh_lx_i[0][SH_LX_LOOP][l] = sh_lx_i[0][SH_LX_NAP][l] = 0;
            sh_lx_i[0][SH_LX_WAKE][l] = 1; shell_lane_seed_i[0][l] = 0;
        }
        memcpy(&proj_slot[1], &keep1, sizeof keep1);
        sh.page[sh.mode] = SHP_NOTE; sframe();          /* (RUDIMENT's NOTE view left on MAIN, as the tests below expect) */
        sgoc(slot_of("FM6"), 0);
        shome();
    }
#undef NOTCH
    {   /* HOME twice goes to what plays behind: RUDIMENT playing, SHOCK + KEYS then FM6 + KEYS (no take): RUDIMENT */
        uint32_t ok5, k, sk = slot_of("SHOCK"), dr = slot_of("DRUMS"), rn = note_of("RUDIMENT");
        track_t *t;
        sgoc(dr, rn);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        grid_hit(t, 0, 0, 1u);
        transport_req = 1; srender(4); sframe();
        sframes(400);
        sgoc(sk, 0);
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sframes(400);
        sgoc(slot_of("FM6"), 0);
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sframes(400);
        stap(B_HOME); stap(B_HOME);
        sframes(100);
        ok5 = sh.mode == dr && sh.note == rn;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&trk[SH_BEAT].step[k]);
        sframes(400);
        sgoc(slot_of("FM6"), 0);
        shome();
        bad += check("HOME twice: to the pair playing behind (RUDIMENT), not the pair before (SHOCK, nothing playing)", ok5);
    }
    {   /* HOME twice returns to the pair played just before, as a pair */
        uint32_t liz = slot_of("LIZARD"), fm = slot_of("FM6");
        sgo2(fm, 0);
        sgo2(liz, 0);                                   /* LIZARD > KEYS */
        sgo2(liz, 1);                                   /* LIZARD > CHORD */
        sframes(400);
        stap(B_HOME); stap(B_HOME);
        sframes(100);
        bad += check("HOME twice: from LIZARD > CHORD back to LIZARD > KEYS, the pair before", sh.mode == liz && sh.note == 0u);
        sgo2(fm, 0);
    }

    {   /* MOMENTARY: a preset change while SAVE is held goes back too */
        track_t *t;
        uint32_t e0, p0;
        int16_t c0;
        sgo2(slot_of("FM6"), 0);
        t = sh_trk();
        e0 = t->eng_req; p0 = t->preset; c0 = t->p[P_E0];
        sdown(B_SAVE); sframes(SH_TAP_MS + 100u);
        sturn(EN_PRESET, -1);                          /* (round: FM6's last sound) */
        sframes(100);
        ok = t->preset != p0;
        sup(B_SAVE); sframes(100);
        bad += check("MOMENTARY: PRESETS while SAVE is held, let go: engine, preset and knobs back",
                     ok && t->eng_req == e0 && t->preset == p0 && t->p[P_E0] == c0);
        sgo2(slot_of("FM6"), 0);
    }

    {   /* small QoL: the NOTE page kept across a NOTE switch; a black key alone on the wheel's empty C5; PRESETS idle on
         * ABOUT */
        uint32_t fm = slot_of("FM6");
        uint8_t c5;
        sgo2(fm, 1);                                    /* FM6 > CHORD */
        snmain();                                       /* (its MAIN) */
        sdown(B_GLO); sframes(SH_TAP_MS + 100u);
        skey((uint32_t)sh_note_key(2));                 /* GLO + ALIVE's black key: the NOTE engine switches */
        sup(B_GLO); sframes(100);
        ok = SHP_FAM[sh.page[sh.mode]] == B_ARP;
        c5 = sh_wslot[7];
        sh_wslot[7] = SH_EMPTY;                         /* (an earlier pick may have filled C5) */
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        sturn(EN_SELECT, (int32_t)(7u - sh.wsel));      /* (the cursor on the empty C5) */
        skey((uint32_t)sh_note_key(1));                 /* CHORD's black key */
        sup(B_HOME); sframes(100);
        ok = ok && sh.mode == fm && sh.note == sh_note_at(1);   /* (whatever an earlier pick put on that key) */
        stap(B_GLO); stap(B_GLO); stap(B_GLO); stap(B_GLO);   /* ABOUT (SYSTEM, SYSTEM 2, PROJECTS, ABOUT) */
        ok = ok && sh.global == SHG_ABOUT;
        {
            uint32_t pr = sh_trk()->preset;
            sturn(EN_PRESET, 1);
            ok = ok && sh_trk()->preset == pr;
        }
        shome();
        sh_wslot[7] = c5;
        bad += check("QoL: NOTE page kept on a NOTE switch; black key alone on empty C5 switches NOTE; PRESETS idle on ABOUT",
                     ok);
        sgo2(fm, 0);
    }

#if FELUCCA_LOOP
    {   /* LOOP holds the echo's tape while it is in front: the delay send of another part goes on the tape with REC
         * on; leaving LOOP gives the echo a clean tape back */
        static int32_t ob[2 * CTL];
        uint32_t lp = slot_of("LOOP"), k, nz = 0, c5 = sh_wslot[7];
        track_t *src = &trk[2], *t;
#define MIX(nb) do { uint32_t b_; for (b_ = 0; b_ < (nb); b_++) mix_block(ob, CTL); } while (0)   /* (the buses too) */
        sh_wslot[7] = (uint8_t)lp;
        sgo2(7, 0);
        t = sh_trk();
        MIX(4);
        ok = loop_hold && fx_tape_loop;
        t->p[P_E1] = 1;                                 /* REC on */
        host_preset(src, 0, 0);
        src->p[P_MUTE] = 0; src->p[P_DLY] = 127;
        input_on(src, 60, 120);
        MIX(60);
        input_off(src, 60);
        t->p[P_E1] = 0;
        MIX(10);
        for (k = 0; k < DLY_LEN; k++)
            nz += dly_buf[k] != 0;
        ok = ok && nz > 1000u;
        {   /* a key on LOOP plays the tape back at its pitch */
            static int32_t vb[2 * HALF_FRAMES];
            int32_t pk = 0;
            uint32_t b;
            input_on(t, 60, 120);
            for (b = 0; b < 80u; b++) {
                memset(vb, 0, sizeof vb);
                events_block(HALF_FRAMES);
                track_render(t, vb, HALF_FRAMES);
                for (k = 0; k < 2u * HALF_FRAMES; k++)
                    pk = vb[k] > pk ? vb[k] : -vb[k] > pk ? -vb[k] : pk;
            }
            input_off(t, 60);
            ok = ok && pk > 200;
        }
        src->p[P_DLY] = 0;                              /* (else the echo rightly records its tail once it is back) */
        sgo2(slot_of("FM6"), 0);
        MIX(4);
#undef MIX
        for (k = 0, nz = 0; k < DLY_LEN; k++)
            nz += dly_buf[k] != 0;
        bad += check("LOOP: holds the tape in front, records another part's delay send with REC on; the echo gets it back clean",
                     ok && !loop_hold && !fx_tape_loop && nz < 64u);
        sh_wslot[7] = c5;
        src->p[P_DLY] = 0;
    }
#endif

    {   /* SPELL (MOUTH's third EDIT page): the phrase loads as syllables and back unchanged; any knob edit, insert or
         * delete leaves a valid phrase (a vowel a syllable, at most 24 phonemes, never empty); a key held sings the
         * cursor's syllable and a VOWEL turn sings the new one at once, with no note left on after */
        uint32_t mo = slot_of("MOUTH"), k, n, i, c5 = sh_wslot[7], lcg = 12345u, bad_ph = 0;
        const uint8_t *hp, *hl;
        uint8_t ph[SPL_MAX + 8], len[SPL_MAX + 8];
        track_t *t;
        mth_part_t *mp;
        sh_wslot[7] = (uint8_t)mo;
        sgo2(7, 0);
        t = sh_trk();
        t->p[P_E4] = 0;                                 /* HELLO */
        sh.page[sh.mode] = SHP_SPELL;
        sframes(100);
        hp = mouth_phrase(0, &hl, &n);
        k = spl_bytes(ph, len);
        ok = mouth_spell_n && spl.n == 2u && k == n;
        for (i = 0; ok && i < n; i++)
            ok = (ph[i] & ~MTH_EOS) == hp[i];
        for (k = 0; k < 600u; k++) {                    /* random edits */
            uint32_t r;
            lcg = lcg * 1664525u + 1013904223u;
            r = lcg >> 16;
            if (r % 8u < 4u)
                sturn((uint8_t[]){EN_K1, EN_K2, EN_K3, EN_K4}[r % 4u], (int32_t)(r >> 4) % 7 - 3);
            else if (r % 8u < 6u)
                stap(r % 2u ? B_OCTUP : B_OCTDN);
            else
                sturn(EN_SELECT, (int32_t)(r >> 4) % 3 - 1);
            n = spl_bytes(ph, len);
            bad_ph += !spl.n || n != spl_count() || n > SPL_MAX || !(ph[n - 1u] & MTH_EOW) || spl.cur >= spl.n;
            for (i = 0; i < spl.n; i++)
                bad_ph += !spl_vowel(spl.s[i].v) || spl.s[i].non > 3u || spl.s[i].nco > 3u || spl.s[i].len > 2u;
        }
        ok = ok && !bad_ph;
        for (k = 0; k < 40u; k++)
            stap(B_OCTUP);
        ok = ok && spl_count() <= SPL_MAX;
        for (k = 0; k < 40u; k++)
            stap(B_OCTDN);
        ok = ok && spl.n == 1u && song.octave == 0;
        mp = mouth_of(t);                               /* a key held, VOWEL turned under it */
        srender(60);
        fm1_in.notes |= 1u << 12; keyboard_block();
        srender(20);
        sturn(EN_K2, 2);
        for (i = 0, n = 0; i < MOUTH_POLY; i++)
            n += t->v[i].gate && mp->v[i].ph == mouth_spell_ph && mth_active(&mp->v[i]);
        srender(20);
        fm1_in.notes &= ~(1u << 12); keyboard_block();
        srender(400);
        for (i = 0; i < NVOICE; i++)
            n += t->v[i].gate ? 100u : 0u;
        shome();
        sframes(100);
        bad += check("SPELL: HELLO loads as HH EH | L OW and back; 600 random edits keep a valid phrase; OCT+ / OCT- bounds; "
                     "VOWEL under a held key re-sings, no note left on", ok && n == 1u && !mouth_spell_n);
        sh_wslot[7] = c5;
        sgo2(slot_of("FM6"), 0);
    }

    {   /* SPELL's letters: GLO held, H E L L X, OCT- (back), O, let go: the phrase is lts_text("hello")'s phonemes */
        static const uint8_t KEYS[] = {7, 4, 11, 11, 23};
        uint32_t mo = slot_of("MOUTH"), k, n, m, c5 = sh_wslot[7];
        uint8_t want[SPL_MAX], ph[SPL_MAX + 8], len[SPL_MAX + 8];
        sh_wslot[7] = (uint8_t)mo;
        sgo2(7, 0);
        sh.page[sh.mode] = SHP_SPELL;
        sframes(100);
        sdown(B_GLO); sframes(SH_TAP_MS + 100u);
        for (k = 0; k < NELEM(KEYS); k++)
            skey(KEYS[k]);
        stap(B_OCTDN);                                  /* (X goes) */
        skey(14);                                       /* O */
        ok = spl_tn == 5u && spl_txt[4] == 'O';
        sup(B_GLO); sframes(100);
        n = lts_text("hello", want, SPL_MAX);
        m = spl_bytes(ph, len);
        ok = ok && n == m && !spl_tn && sh.global == SHG_NONE;
        for (k = 0; ok && k < n; k++)
            ok = (uint8_t)(ph[k] & ~MTH_EOS) == want[k];
        shome();
        sframes(100);
        bad += check("SPELL letters: GLO + H E L L X, OCT- back, O, let go: the phrase is lts_text(\"hello\")'s, SYSTEM not opened",
                     ok);
        sh_wslot[7] = c5;
        sgo2(slot_of("FM6"), 0);
    }

    {   /* SPELL's SAVE: SELECT picks U3, OCT+ writes the phrase there and PHRASE names it; OCT- leaves a slot as it was */
        uint32_t mo = slot_of("MOUTH"), k, n, m, c5 = sh_wslot[7];
        const uint8_t *up, *ul;
        uint8_t ph[SPL_MAX + 8], len[SPL_MAX + 8];
        sh_wslot[7] = (uint8_t)mo;
        sgo2(7, 0);
        sh_trk()->p[P_E4] = 1;                          /* COUNT */
        sh.page[sh.mode] = SHP_SPELL;
        sframes(100);
        sturn(EN_K2, 1);                                /* (an edit, so it differs from COUNT) */
        stap(B_SAVE);
        ok = sh_splsv.open && sh_splsv.slot == 0u;
        sturn(EN_SELECT, 2);
        stap(B_OCTUP);
        n = spl_bytes(ph, len);
        up = mouth_phrase(MTH_NPHRASE + 2u, &ul, &m);
        ok = ok && !sh_splsv.open && sh_trk()->p[P_E4] == (int16_t)(MTH_NPHRASE + 2u) && m == n;
        for (k = 0; ok && k < n; k++)
            ok = up[k] == ph[k] && ul[k] == len[k];
        stap(B_SAVE);
        stap(B_OCTDN);
        ok = ok && !sh_splsv.open && mouth_phrase(MTH_NPHRASE + 2u, &ul, &m) == up && m == n;
        shome();
        sframes(100);
        bad += check("SPELL SAVE: SELECT picks U3, OCT+ writes the phrase and PHRASE names it; OCT- leaves it", ok);
        sh_wslot[7] = c5;
        sgo2(slot_of("FM6"), 0);
    }

    {   /* SPELL under other NOTE engines (CHORD and a note_engine.h engine on black keys 2 and 4): the page is there and
         * GLO's letters still type, the NOTE engine's own keys silent meanwhile */
        static const uint8_t KEYS[] = {7, 4, 11, 11, 14};
        uint32_t mo = slot_of("MOUTH"), k, w, n, m, c5 = sh_wslot[7], pos[2] = {1, 3};
        uint8_t want[SPL_MAX], ph[SPL_MAX + 8], len[SPL_MAX + 8];
        ok = 1;
        sh_wslot[7] = (uint8_t)mo;
        for (w = 0; w < 2u; w++) {
            sgo2(7, pos[w]);
            ok = ok && sh_page_ok(SHP_SPELL) && sh.note == sh_note_at(pos[w]);
            sh.page[sh.mode] = SHP_SPELL;
            sframes(100);
            sdown(B_GLO); sframes(SH_TAP_MS + 100u);
            for (k = 0; k < NELEM(KEYS); k++)
                skey(KEYS[k]);
            sup(B_GLO); sframes(100);
            n = lts_text("hello", want, SPL_MAX);
            m = spl_bytes(ph, len);
            ok = ok && n == m;
            for (k = 0; ok && k < n; k++)
                ok = (uint8_t)(ph[k] & ~MTH_EOS) == want[k];
            shome();
            sframes(100);
        }
        bad += check("SPELL under CHORD and a note engine: the page is there and GLO's letters type HELLO", ok);
        sh_wslot[7] = c5;
        sgo2(slot_of("FM6"), 0);
    }

    {   /* a part's bus has headroom for its chord: six notes come out within 2 dB of one (of the six alone, by mean
         * power: sines a fifth and more apart sum as power, sqrt(6) louder unscaled), the limiter never touched; the
         * headroom glides in, no step when the chord lands */
        static int32_t ob[2 * CTL];
        static const uint8_t CH[6] = {48, 55, 62, 69, 76, 83};
        track_t *t = &trk[2];
        double e1 = 0.0, e6 = 0.0, db;
        uint32_t c, k, b, i;
        int32_t a1 = 0, lmax = LIM_T;
        for (c = 0; c < 7u; c++) {                      /* (each note alone, then all six) */
            double e = 0.0;
            host_preset(t, 0, 5);                       /* ANALOG SINE KEY */
            t->p[P_MUTE] = 0; t->p[P_VOICE] = V_POLY;
            t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
            for (b = 0; b < 40u; b++)
                mix_block(ob, CTL);
            lim_env = LIM_T;
            for (k = 0; k < 6u; k++)
                if (c == 6u || c == k)
                    input_on(t, CH[k], 50);
            if (c == 6u) {
                mix_block(ob, CTL);
                a1 = fx_bus_att[t - trk];
            }
            for (b = 0; b < 100u; b++)                  /* (the glide settles: 512 samples a time constant) */
                mix_block(ob, CTL);
            for (b = 0; b < 40u; b++) {
                mix_block(ob, CTL);
                for (i = 0; i < CTL; i++)                /* (the dry mix, before MASTER) */
                    e += (double)mix_l[i] * mix_l[i] + (double)mix_r[i] * mix_r[i];
                lmax = lim_env > lmax ? lim_env : lmax;
            }
            for (k = 0; k < 6u; k++)
                input_off(t, CH[k]);
            for (b = 0; b < 20000u; b++) {              /* (the release out and the headroom back to none) */
                uint32_t nv = 0;
                for (k = 0; k < NVOICE; k++)
                    nv += t->v[k].active;
                if (!nv && !fx_bus_att[t - trk])
                    break;
                mix_block(ob, CTL);
            }
            if (c < 6u)
                e1 += e / 6.0;
            else
                e6 = e;
        }
        db = 10.0 * log10(e6 / e1);
        printf("  (six notes against one: %+.1f dB; the headroom a block in %d of %d)\n", db, (int)a1, 18114);
        bad += check("part bus: a six-note chord within 2 dB of one note, the limiter untouched, the headroom gliding in",
                     fabs(db) < 2.0 && lmax == LIM_T && a1 > 0 && a1 < 18114 / 2);
    }

    {   /* the master limiter rides the gain, it does not shape the wave: a 200 Hz sine at +12 dB over LIM_T comes out,
         * once settled, with THD under 1 % (the harmonics' energy against the fundamental's, by DFT over whole cycles) */
        static int32_t yl[44100];
        uint32_t i, h, N = 220u * 50u;                  /* (50 cycles of 220 samples: 200.45 Hz, whole cycles) */
        double fund = 0.0, harm = 0.0, thd;
        int32_t keep = lim_env;
        lim_env = LIM_T;
        for (i = 0; i < 44100u; i++) {
            int32_t l = (int32_t)(4.0 * LIM_T * sin(2.0 * 3.14159265358979 * (double)i / 220.0)), r = l;
            master_out(&l, &r);
            yl[i] = l;
        }
        for (h = 1; h <= 15u; h++) {
            double re = 0.0, im = 0.0;
            for (i = 0; i < N; i++) {
                double a = 2.0 * 3.14159265358979 * (double)(h * i) / 220.0, y = (double)yl[44100u - N + i];
                re += y * cos(a);
                im += y * sin(a);
            }
            if (h == 1)
                fund = re * re + im * im;
            else
                harm += re * re + im * im;
        }
        thd = 100.0 * sqrt(harm / fund);
        lim_env = keep;
        printf("  (limiter THD at +12 dB over LIM_T: %.2f %%)\n", thd);
        bad += check("master limiter: a 200 Hz sine at +12 dB over LIM_T comes out with THD under 1 %", thd < 1.0);
    }


    {   /* an overdub adds and never erases: chords played back to back under CHORD, the keys free and with HOLD
         * latching them, stay as the loop comes round (a latched hold tied the last chord over the rest), after a
         * DOUBLE too; a key held across a step that plays ends its hold there */
        track_t *t;
        uint32_t k, cfg, acc = 0, ok = 1;
        static const uint8_t at[4] = {2, 6, 8, 10};
#define OD_KON(k) do { fm1_in.notes |= 1u << (k); host_notes |= 1u << (k); keyboard_block(); } while (0)
#define OD_KOFF(k) do { fm1_in.notes &= ~(1u << (k)); keyboard_block(); } while (0)
#define OD_RUN(ms) do { uint32_t f_; for (f_ = 0; f_ < (ms) / 16u; f_++) { acc += 16u * FS / 1000u;         \
                        while (acc >= HALF_FRAMES) { srender(1); acc -= HALF_FRAMES; } sframe(); } } while (0)
#define OD_TO(i) do { uint32_t w_; for (w_ = 0; w_ < 20000u && t->seq_idx != (i); w_++) OD_RUN(16u); } while (0)
#define OD_KEPT(off) (t->step[(off) + at[0]].n && t->step[(off) + at[1]].n && t->step[(off) + at[2]].n &&         \
                      t->step[(off) + at[3]].n)
        for (cfg = 0; cfg < 2u; cfg++) {
            sgoc(slot_of("ANALOG"), note_of("CHORD"));
            t = sh_trk();
            for (k = 0; k < NSTEP; k++)
                step_clear(&t->step[k]);
            motion_clear(t);
            t->p[P_SLEN] = 16; t->p[P_AHOLD] = (int16_t)cfg; t->p[P_AMODE] = 0;
            song.rec = 0;
            sframe();
            transport_req = 1; srender(2); OD_RUN(200u);
            OD_TO(2u); stap(B_REC); sframe();           /* a first take: one chord */
            OD_KON(CB_ROOT0); OD_RUN(400u); OD_KOFF(CB_ROOT0);
            OD_TO(13u); stap(B_REC); sframe(); sframe();
            ok = ok && t->p[P_SLEN] == 16;
            stap(B_REC); sframe();                      /* over it: three chords back to back */
            OD_TO(6u);
            OD_KON(CB_ROOT0 + 1u); OD_RUN(250u); OD_KON(CB_ROOT0 + 2u); OD_RUN(30u); OD_KOFF(CB_ROOT0 + 1u); OD_RUN(220u);
            OD_KON(CB_ROOT0 + 3u); OD_RUN(30u); OD_KOFF(CB_ROOT0 + 2u); OD_RUN(220u); OD_KOFF(CB_ROOT0 + 3u);
            OD_TO(15u); OD_TO(1u); OD_TO(15u);         /* round the loop */
            ok = ok && OD_KEPT(0u);
            sdown(B_SEQ); sframes(SH_TAP_MS + 50u);    /* DOUBLE, then twice round */
            sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
            sup(B_SEQ); sframe();
            OD_TO(31u); OD_TO(1u); OD_TO(31u); OD_TO(1u);
            ok = ok && t->p[P_SLEN] == 32 && OD_KEPT(0u) && OD_KEPT(16u);
            song.rec = 0; transport_req = 2; srender(2); sframe();
            t->p[P_AHOLD] = 0;
            fm1_in.notes = 0; keyboard_block(); srender(4); sframe();
        }
        sgo2(slot_of("FM6"), note_of("KEYS"));          /* KEYS: a key held over a step that plays */
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        t->step[8].note[0] = 67; t->step[8].time = ST_NOTE; t->step[8].n = 1;
        song.rec = (uint8_t)(1u << sh.part);
        sh_take.first = 0;
        transport_req = 1; srender(2);
        OD_TO(4u);
        OD_KON(12u); OD_TO(11u); OD_KOFF(12u);
        ok = ok && t->step[4].n && t->step[5].time == ST_TIE && t->step[8].n == 1u && t->step[8].note[0] == 67u &&
             t->step[9].time != ST_TIE;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sframe();
        bad += check("overdub: chords back to back under CHORD stay as the loop comes round, the keys free and with "
                     "HOLD latching them, after a DOUBLE too; a key held over a step that plays ends its hold there", ok);
    }
    {   /* one press, one step: a strummed chord goes whole into the step it was pressed in (STRM only delays its
         * sound); a legato chord records the tones it shares with the chord still held (D F A, then F A C: F A C);
         * a chord in the count-in's last eighth leaves the first take open (LEN 64 until REC closes it) */
        track_t *t;
        uint32_t k, w, acc = 0, ok = 1, ok2 = 1, ok3 = 1, first = 0;
        int32_t len = 0;
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AHOLD] = 0; t->p[P_AMODE] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        t->step[15].note[0] = 50; t->step[15].time = ST_NOTE; t->step[15].n = 1;   /* (a take before: no first take) */
        sh_sp[SP_STRM - SP_FIRST] = 60; sframe();
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(2u);
        OD_KON(CB_ROOT0); OD_RUN(400u); OD_KOFF(CB_ROOT0);
        ok = t->step[2].n == 3u && !t->step[3].n;
        sh_sp[SP_STRM - SP_FIRST] = 0; sframe();
        OD_TO(6u);                                      /* legato: the second chord before the first is let go */
        OD_KON(CB_ROOT0); OD_RUN(250u); OD_KON(CB_ROOT0 + 3u); OD_RUN(30u); OD_KOFF(CB_ROOT0); OD_RUN(220u);
        OD_KOFF(CB_ROOT0 + 3u);                         /* (D4's chord, then F4's: D F A, F A C in C) */
        for (k = 6u; k < 12u && !(t->step[k].n && k > 6u && t->step[k].time == ST_NOTE); k++)
            ;
        ok2 = k < 12u && t->step[k].n == 3u;
        printf("  (legato: step %u, %u notes: %u %u %u)\n", k, k < 12u ? t->step[k].n : 0u, k < 12u ? t->step[k].note[0] : 0u,
               k < 12u ? t->step[k].note[1] : 0u, k < 12u ? t->step[k].note[2] : 0u);
        song.rec = 0; transport_req = 2; srender(2); sframe(); sframe();
        for (k = 0; k < NSTEP; k++)                     /* REC + PLAY over an empty pattern: a chord in the count-in */
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sframe();
        sdown(B_REC); sframe(); sdown(B_PLAY); sframe(); sup(B_PLAY); sframe(); sup(B_REC); sframe();
        for (w = 0; w < 20000u && !(cin_left == 1u && cin_pos > click_beat_len() * 3u / 4u); w++) {
            srender(1);
            if (!(w & 3u))
                sframe();
        }
        OD_KON(CB_ROOT0 + 1u); OD_RUN(48u); OD_KOFF(CB_ROOT0 + 1u);
        OD_RUN(400u);
        first = sh_take.first;
        len = t->p[P_SLEN];
        ok3 = song.playing && t->step[0].n == 3u && first && len == NSTEP;
        song.rec = 0; transport_req = 2; srender(2); sframe(); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sframe();
        printf("  (STRM chord in one step %u; legato's shared tones %u; count-in chord: first %u LEN %d)\n", ok, ok2,
               first, (int)len);
        bad += check("rec: a strummed chord goes whole into its step, a legato chord keeps the tones it shares, a "
                     "chord in the count-in's last eighth leaves the first take open", ok && ok2 && ok3);
    }
    {   /* the recording model: REPLACE puts this pass's chord in place of an earlier pass's and leaves the steps it
         * does not play; a held note in REPLACE ties over an earlier pass's steps; the arp left armed with HOLD rewrites
         * its steps each pass (one note a step, no pile-up); UNDO TAKE takes back the take's knob moves; under QNT
         * SEQ+T the note recorded plays back as it was heard (TRN once); armed over ALIVE's take, the take is silent */
        track_t *t;
        uint32_t k, acc = 0, ok1, ok2, ok3, ok4, ok5, ok6, most = 0, q, heard = 0;
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AHOLD] = 0; t->p[P_AMODE] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        for (k = 0; k < 3u; k++) {                      /* an earlier pass: D F A on step 3, and a note on step 9 */
            t->step[3].note[k] = (uint8_t)(62u + 3u * k); t->step[3].n++;
        }
        t->step[3].time = ST_NOTE;
        t->step[9].note[0] = 50; t->step[9].time = ST_NOTE; t->step[9].n = 1;
        sh_rec_mode_set(2u);                            /* REPLACE */
        song.rec = (uint8_t)(1u << sh.part);
        sh_take.first = 0;
        sframe();
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(3u);
        OD_KON(CB_ROOT0 + 5u); OD_RUN(48u); OD_KOFF(CB_ROOT0 + 5u);   /* G B D over it */
        OD_TO(5u); OD_TO(12u);
        ok1 = t->step[3].n == 3u && t->step[3].note[0] != 62u && t->step[9].n == 1u && t->step[9].note[0] == 50u;
        OD_TO(8u);                                      /* held from step 8 over step 9's note, let go on step 11 */
        OD_KON(CB_ROOT0); OD_TO(11u); OD_KOFF(CB_ROOT0);
        ok2 = t->step[8].n == 3u && t->step[9].time == ST_TIE && t->step[3].n == 3u;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        sh_rec_mode_set(0u);
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        for (k = 0; k < NSTEP; k++)                     /* SEQ+T, TRN +2: C4's key heard as D4, plays back D4 */
            step_clear(&t->step[k]);
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_TRANS] = 2; t->p[P_QUANT] = QN_SEQT; t->p[P_CHRD] = 0;
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(2u);
        OD_KON(7u); OD_RUN(48u); OD_KOFF(7u);
        song.rec = 0;
        OD_TO(1u);
        for (k = 0; k < 400u && t->seq_idx != 2u; k++)
            OD_RUN(16u);
        OD_RUN(32u);
        for (q = 0; q < NVOICE; q++)
            heard |= t->v[q].active && t->v[q].note == 62u;
        ok5 = t->step[2].n == 1u && t->step[2].note[0] == 60u && heard;
        transport_req = 2; srender(2); sframe();
        t->p[P_TRANS] = 0; t->p[P_QUANT] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (k = 0; k < NSTEP; k++)                     /* a take with a note and a knob move, then UNDO TAKE */
            step_clear(&t->step[k]);
        motion_clear(t);
        sh_rec_mode_set(1u);                            /* ADD + KNOBS */
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(2u);
        OD_KON(12u); OD_RUN(48u); OD_KOFF(12u);
        OD_TO(5u);
        (void)motion_capture(t, P_DIST, 44);
        ok4 = motion_count(t) == 1u;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        ok4 = ok4 && str_eq(sh.toast, "TAKE UNDONE") && motion_count(t) == 0u && !t->step[2].n;
        sh_rec_mode_set(0u);
        sgo2(slot_of("FM6"), note_of("KEYS"));          /* the arp: UP 1/16 with HOLD, C E G, three passes armed */
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16; t->p[P_AMODE] = 1; t->p[P_ARATE] = 2; t->p[P_AHOLD] = 1;
        song.rec = (uint8_t)(1u << sh.part);
        sframe();
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_KON(7u); OD_KON(11u); OD_KON(14u); OD_RUN(100u); OD_KOFF(7u); OD_KOFF(11u); OD_KOFF(14u);
        for (q = 0; q < 3u; q++) {
            OD_TO(15u); OD_TO(1u);
        }
        for (k = 0; k < 16u; k++)
            most = t->step[k].n > most ? t->step[k].n : most;
        ok3 = most == 1u;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        fm1_in.notes = 0; keyboard_block(); srender(8); sframe();
        sgo2(slot_of("LIZARD"), note_of("ALIVE"));      /* ALIVE on LINE armed: nothing written, ALIVE never quiet */
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        song.rec = (uint8_t)(1u << sh.part);
        sframe();
        transport_req = 1; srender(2);
        for (k = 0, heard = 0; k < 150u; k++) {
            OD_RUN(16u);
            for (q = 0; q < NVOICE; q++)
                heard |= t->v[q].active && t->v[q].gate;
        }
        for (k = 0, q = 0; k < NSTEP; k++)
            q += t->step[k].n;
        ok6 = heard && !q && !shell_gen_part;
        song.rec = 0;
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sgo2(slot_of("FM6"), note_of("KEYS"));
        sframe();
        printf("  (REPLACE chord %u, tie %u; arp pile-up %u (most %u); UNDO knob moves %u; SEQ+T %u; ALIVE on LINE %u)\n",
               ok1, ok2, ok3, most, ok4, ok5, ok6);
        bad += check("rec model: REPLACE swaps a pass's chord and ties over an earlier pass; the arp rewrites each pass; "
                     "UNDO TAKE takes the knob moves back; SEQ+T plays back as heard; ALIVE armed on LINE writes nothing",
                     ok1 && ok2 && ok3 && ok4 && ok5 && ok6);
    }
    {   /* the review's: HARP, played from the keys, is no generator (its take heard while armed); TRN turned while a
         * note is held under SEQ+T and REPLACE leaves no hold behind; a take's knob move before its first note, and a
         * take of knob moves alone, are undone with it; a first take closing behind the playhead starts a new pass */
        track_t *t;
        uint32_t k, q, acc = 0, ok1, ok2, ok3, ok4, heard = 0;
        uint16_t pass0;
        sgo2(slot_of("FM6"), note_of("HARP"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        for (k = 0; k < 16u; k++) {
            t->step[k].note[0] = 21; t->step[k].time = ST_NOTE; t->step[k].n = 1;
        }
        song.rec = (uint8_t)(1u << sh.part);
        sframe();
        transport_req = 1; srender(2);
        for (k = 0; k < 100u && !heard; k++) {
            OD_RUN(16u);
            for (q = 0; q < NVOICE; q++)
                heard |= t->v[q].active && t->v[q].gate && t->v[q].note == 21u;
        }
        ok1 = !shell_gen_part && heard;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        sgo2(slot_of("FM6"), note_of("KEYS"));          /* SEQ+T, REPLACE: TRN turned while a note is held */
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_CHRD] = 0; t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16; t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_TRANS] = 0; t->p[P_QUANT] = QN_SEQT;
        t->step[6].note[0] = 50; t->step[6].time = ST_NOTE; t->step[6].n = 1;
        t->step[10].note[0] = 52; t->step[10].time = ST_NOTE; t->step[10].n = 1;
        sh_rec_mode_set(2u);
        song.rec = (uint8_t)(1u << sh.part);
        sh_take.first = 0;
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(2u);
        OD_KON(7u); OD_TO(4u); t->p[P_TRANS] = 2; OD_TO(5u); OD_KOFF(7u);
        OD_TO(14u);
        ok2 = !t->rh_n && t->step[6].n && t->step[6].time == ST_NOTE && t->step[10].n;
        song.rec = 0; transport_req = 2; srender(2); sframe();
        t->p[P_TRANS] = 0; t->p[P_QUANT] = 0;
        sh_rec_mode_set(1u);                            /* ADD + KNOBS: a knob move, then the take's first note */
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->step[0].note[0] = 55; t->step[0].time = ST_NOTE; t->step[0].n = 1;   /* (a take before) */
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(1u);
        (void)motion_capture(t, P_DIST, 44);
        OD_TO(3u);
        OD_KON(12u); OD_RUN(48u); OD_KOFF(12u);
        song.rec = 0; OD_RUN(32u);
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        ok3 = str_eq(sh.toast, "TAKE UNDONE") && motion_count(t) == 0u && !t->step[3].n && t->step[0].n == 1u;
        song.rec = (uint8_t)(1u << sh.part);            /* a take of a knob move alone */
        OD_RUN(48u);
        OD_TO(6u);
        (void)motion_capture(t, P_DIST, 30);
        song.rec = 0; OD_RUN(32u);
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();
        ok3 = ok3 && motion_count(t) == 0u && t->step[0].n == 1u;
        transport_req = 2; srender(2); sframe();
        sh_rec_mode_set(0u);
        for (k = 0; k < NSTEP; k++)                     /* a first take of 2.4 bars: LEN 32, the playhead past it */
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        song.rec = 0;
        transport_req = 1; srender(2); OD_RUN(100u);
        stap(B_REC); sframe();
        OD_KON(12u); OD_RUN(48u); OD_KOFF(12u);
        for (k = 0; k < 20000u && t->seq_idx < (uint32_t)(sh_bar_steps(t) * 12u / 5u); k++)
            OD_RUN(16u);
        pass0 = rec_pass[trk_index(t)];
        stap(B_REC); sframe(); sframe();
        ok4 = t->p[P_SLEN] == (int16_t)(2u * sh_bar_steps(t)) && rec_pass[trk_index(t)] == (uint16_t)(pass0 + 1u);
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sframe();
        printf("  (HARP heard armed %u; TRN mid-hold %u; knob-move undo %u; first-take pass %u)\n", ok1, ok2, ok3, ok4);
        bad += check("rec review: HARP's take heard while armed; TRN turned mid-hold leaves no hold; knob moves before "
                     "a take's first note, or alone, undo with it; a first take closing behind the playhead is a new "
                     "pass", ok1 && ok2 && ok3 && ok4);
    }
    {   /* the step pages: SEQ + C#5 / D#5 turn the white keys to steps 17-32 .. 49-64, the keys glowing toward another
         * page; a part's LEN never clears a step; the view stays where it is put while it plays, the key toward the
         * playhead's page pulsing on the beat; on the SEQ page with nothing held the page keys page, no note; PLAY
         * from stop: page 1 */
        track_t *t;
        uint32_t k, acc = 0, na, ok1 = 1, ok2 = 1, ok3 = 1, q;
#define PG_ON(k) ((fm1_led_dim[led_pos[14u + (k)] >> 3] >> (led_pos[14u + (k)] & 7u)) & 1u)
        uint8_t pos0[sizeof led_pos];
        memcpy(pos0, led_pos, sizeof pos0);             /* (the host's key map is a zero stub: the page keys given places) */
        led_pos[14u + SH_PG_LEFT] = 9u << 3 | 1u;
        led_pos[14u + SH_PG_RIGHT] = 9u << 3 | 2u;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        skey_sung(12);
        sdown(B_SEQ); sframes(400);
        ok1 = !PG_ON(SH_PG_LEFT) && !PG_ON(SH_PG_RIGHT);            /* one page: no hint */
        t->p[P_SLEN] = 64; sframe();
        ok1 = ok1 && !PG_ON(SH_PG_LEFT) && PG_ON(SH_PG_RIGHT);      /* more to the right */
        skey(SH_PG_RIGHT);
        ok1 = ok1 && str_eq(sh.toast, "STEPS 17-32") && sh_pg() == 16u && PG_ON(SH_PG_LEFT) && PG_ON(SH_PG_RIGHT);
        skey((uint32_t)sh_white_key(0));
        skey(SH_PG_RIGHT); skey(SH_PG_RIGHT); skey(SH_PG_RIGHT);    /* (the last page stays) */
        ok1 = ok1 && str_eq(sh.toast, "STEPS 49-64") && sh_pg() == 48u && !PG_ON(SH_PG_RIGHT);
        skey((uint32_t)sh_white_key(15));
        ok1 = ok1 && sh_step_led((uint32_t)sh_white_key(15)) == SHL_LIT && sh_step_led((uint32_t)sh_white_key(0)) != SHL_LIT;
        skey(SH_PG_LEFT);
        ok1 = ok1 && str_eq(sh.toast, "STEPS 33-48");
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        sup(B_SEQ); sframe();
        ok1 = ok1 && t->step[16].n && t->step[63].n && !t->step[0].n && !t->step[15].n && !t->step[48].n && na == 0u;
        t->p[P_SLEN] = 16; sframe(); t->p[P_SLEN] = 64; sframe();
        ok2 = t->step[16].n && t->step[63].n;                       /* LEN shorter and back: the steps stay */
        sh_pg_hand = 3; sframe();                                   /* (page 4 in view) */
        t->p[P_SLEN] = 20; sframe();                                /* LEN 20: two pages, the second 17-20 */
        ok2 = ok2 && sh_pg() == 16u && sh_pg_hand == 1u;            /* (the page in view gone: the last) */
        sdown(B_SEQ); sframes(400);
        sh_pg_hand = 0; skey(SH_PG_RIGHT);
        ok2 = ok2 && str_eq(sh.toast, "STEPS 17-20") && sh_step_led((uint32_t)sh_white_key(5)) == SHL_OFF;
        sup(B_SEQ); sframe();
        transport_req = 1; srender(2); OD_RUN(100u);                /* PLAY from stop: page 1, and it stays */
        ok3 = sh_pg_hand == 0u && sh_pg() == 0u;
        OD_TO(16u);
        ok3 = ok3 && sh_pg() == 0u && sh_pg_led(SH_PG_RIGHT) == SHL_LIT;   /* the playhead's page: on the beat */
        OD_TO(17u);
        ok3 = ok3 && sh_pg() == 0u && sh_pg_led(SH_PG_RIGHT) == SHL_LOW;
        sh.page[sh.mode] = SHP_SEQ; sframe();                       /* the SEQ page, nothing held: the keys page */
        for (na = 0, q = 0; q < NVOICE; q++) t->v[q].active = 0;
        skey(SH_PG_RIGHT);
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        ok3 = ok3 && sh_pg() == 16u && str_eq(sh.toast, "STEPS 17-20") && !t->step[17].n;
        transport_req = 2; srender(2); sframe();
        ok3 = ok3 && sh_pg() == 16u;                                /* (stopped: where it was) */
        transport_req = 1; srender(2); sframe();
        ok3 = ok3 && sh_pg() == 0u;
        transport_req = 2; srender(2); sframe();
        sh.page[sh.mode] = SHP_HOME; sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16; sh_pg_hand = 0;
        sframe();
        memcpy(led_pos, pos0, sizeof pos0);
        printf("  (pages and hint %u; LEN keeps steps %u; the view stays, the pulse, SEQ page keys %u, sounded %u)\n",
               ok1, ok2, ok3, na);
        bad += check("step pages: SEQ + C#5 / D#5 page the white keys to steps 17-64, the keys glow toward another page, "
                     "silent; the part's LEN keeps its steps; playing, the view stays and the key toward the playhead "
                     "pulses; the SEQ page pages with nothing held", ok1 && ok2 && ok3);
    }
    {   /* patterns.md, the owner's four checks. 1: RUDIMENT, a kick on 1, SEQ + OCT+: DOUBLE: 32 STEPS and P1/2, the
         * kick on 1 and 17; a cowbell on 1 of page 1 plays once per 32; D#5 (nothing held): page 2 has the kick on 17
         * and no cowbell. 2: playing, the view stays on page 1, the page key toward the playhead pulses. 3: SEQ + a
         * tap of OCT-: HALF: 16 STEPS; SEQ + OCT+ again, page 2 as it was. 4: LINES at LEN 24 against RUDIMENT at 16:
         * they drift and line up every 48 steps */
        track_t *t, *tl;
        uint32_t k, acc = 0, l, ok1, ok2, ok3, ok4, h16, h32, wraps = 0, lw[3] = {99, 99, 99}, last;
        sgo2(slot_of("DRUMS"), note_of("RUDIMENT"));
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = sh_eu[1][l] = sh_eu[2][l] = 0; shell_lane_len[l] = 0;
        }
        t->p[P_SLEN] = 16; sh_pg_hand = 0;
        sh.page[sh.mode] = SHP_HOME; sframe();
        skey(SH_BLACK_KEYS[0]); skey((uint32_t)sh_white_key(0));   /* KICK on 1 */
        sh_y0 = 0; cv_begin(240, 16, SC_BG); sh_draw_head(); h16 = cv_hash();
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        sh_y0 = 0; cv_begin(240, 16, SC_BG); sh_draw_head(); h32 = cv_hash();
        ok1 = str_eq(sh.toast, "DOUBLE: 32 STEPS") && t->p[P_SLEN] == 32 && sh_pg_n() == 2u && sh_pg() == 0u &&
              h32 != h16 && (step_lanes(&t->step[0]) & 1u) && (step_lanes(&t->step[16]) & 1u);
        skey(SH_BLACK_KEYS[7]); skey((uint32_t)sh_white_key(0));   /* COWBELL (lane 8) on 1 of page 1 */
        ok1 = ok1 && sh_lane_len(7) == 32u && ((step_lanes(&t->step[0]) >> 7) & 1u) && !((step_lanes(&t->step[16]) >> 7) & 1u);
        skey(SH_PG_RIGHT); sframe();                               /* D#5, nothing held: page 2 */
        ok1 = ok1 && sh_pg() == 16u && str_eq(sh.toast, "STEPS 17-32");
        skey(SH_PG_LEFT); sframe();
        transport_req = 1; srender(2); OD_RUN(100u);              /* 2: playing, the view stays on page 1 */
        OD_TO(20u);
        ok2 = sh_pg() == 0u && sh_pg_led(SH_PG_RIGHT) == SHL_LIT && sh_pg_led(SH_PG_LEFT) == SHL_OFF;
        OD_TO(21u);
        ok2 = ok2 && sh_pg() == 0u && sh_pg_led(SH_PG_RIGHT) == SHL_LOW;
        skey(SH_PG_FOLLOW); OD_TO(22u);                            /* F#5: FOLLOW, the view on the playhead's page */
        ok2 = ok2 && str_eq(sh.toast, "FOLLOW ON") && (ui_prefs2 & SH_PREF_FOLLOW) && sh_pg() == 16u;
        skey(SH_PG_FOLLOW); sframe();
        ok2 = ok2 && str_eq(sh.toast, "FOLLOW OFF") && !(ui_prefs2 & SH_PREF_FOLLOW);
        transport_req = 2; srender(2); sframe();
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);                    /* 3: SEQ + a tap of OCT-, then OCT+ */
        sdown(B_OCTDN); sframe(); sup(B_OCTDN); sframe();
        ok3 = str_eq(sh.toast, "HALF: 16 STEPS") && t->p[P_SLEN] == 16 && sh_pg_n() == 1u;
        sdown(B_OCTUP); sframe(); sup(B_OCTUP); sframe();
        sup(B_SEQ); sframe();
        ok3 = ok3 && t->p[P_SLEN] == 32 && (step_lanes(&t->step[16]) & 1u) && !((step_lanes(&t->step[16]) >> 7) & 1u) &&
              ((step_lanes(&t->step[0]) >> 7) & 1u);
        sdown(B_SEQ); sframes(SH_TAP_MS + 50u);                    /* (held to the ring it is still SEQ's reset) */
        sdown(B_OCTDN); sframes(SH_TAP_MS + 100u); sup(B_OCTDN); sframe(); sup(B_SEQ); sframe();
        ok3 = ok3 && t->p[P_SLEN] == 32 && str_eq(sh.toast, "LET GO EARLY: NOTHING RESET");
        t->p[P_SLEN] = 16;                                         /* 4: LINES at 24 against RUDIMENT at 16 */
        tl = &trk[SH_NOTES[SH_N_LINES].part];
        tl->p[P_SLEN] = 24;
        transport_req = 1; srender(2);
        last = t->seq_idx;
        for (k = 0; k < 200000u && wraps < 3u; k++) {
            srender(1);
            if (t->seq_idx != last && t->seq_idx == 0u)
                lw[wraps++] = tl->seq_idx;
            last = t->seq_idx;
        }
        transport_req = 2; srender(2); sframe();
        ok4 = lw[0] == 16u && lw[1] == 8u && lw[2] == 0u;          /* (after 16, 32, 48 steps) */
        tl->p[P_SLEN] = 16;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16; sh_pg_hand = 0;
        sframe();
        printf("  (owner's checks: DOUBLE and the cowbell %u, the view stays %u, HALF and back %u, 24 against 16 %u: "
               "%u %u %u)\n", ok1, ok2, ok3, ok4, lw[0], lw[1], lw[2]);
        bad += check("patterns.md: the owner's four checks (DOUBLE on RUDIMENT, the view stays while it plays, F#5 FOLLOW "
                     "if wanted, HALF and DOUBLE back, LINES 24 against RUDIMENT 16 lines up every 48)", ok1 && ok2 && ok3 && ok4);
    }
    {   /* fundamentals.md, the owner's five checks, each by the panel's own gestures. 1: NEW, then PLAY: silence (a power
         * cycle after it: autosave_sqrl_test). 2: TONNETZ visited on the wheel and left unplayed: nothing behind.
         * 3: ACID, REC armed and stopped: a 16-step line on the keys, OCT for a high note, SEQ for rests; step 5 held +
         * C#5 an accent, step 6 held + D#5 a slide; on ANALOG ACID (LEGATO) it plays, and goes on under RUDIMENT.
         * 4: RUDIMENT: a 4-hit kick from FILL, hats by hand; HATCL held + K4 turns the hats at once, KICK held + K2 to 12
         * spreads four kicks over 12, heard at once. 5: LINES, three lanes of different LEN; a lane held + K4 while it
         * plays turns it at once, the LENs kept */
#define KDN(k_) do { fm1_in.notes |= 1u << (k_); host_notes |= 1u << (k_); keyboard_block(); sframe(); } while (0)
#define KUP(k_) do { fm1_in.notes &= ~(1u << (k_)); keyboard_block(); sframe(); } while (0)
        uint32_t fm = slot_of("FM6"), an = slot_of("ANALOG"), dr = slot_of("DRUMS"), kn = note_of("KEYS"),
                 rn = note_of("RUDIMENT"), ln = note_of("LINES"), tz = note_of("TONNETZ"), ac = note_of("ACID");
        uint32_t k, q, on, ok1, ok2, ok3, ok4, ok5, b0, b1, c;
        track_t *t;
        static const uint8_t LINE16[16] = {0, 2, 4, 0, 7, 4, 2, 9, 5, 4, 2, 0, 7, 4, 2, 0};   /* (white keys; 3, 11: rests) */
        transport_req = 2; srender(2); sframe();
        sgo2(dr, rn);                                   /* (some music first: a kick, and ENIGMA used on LINE) */
        skey(SH_BLACK_KEYS[0]); skey((uint32_t)sh_white_key(0));
        sgo2(fm, kn);
        /* 1 */
        for (k = 0; k < 8u && sh.global != SHG_PROJECT; k++)
            stap(B_GLO);
        for (k = 0; k < 6u && sh.pslot != 4u; k++)
            sturn(EN_K1, 1);
        stap(B_OCTDN);
        ok1 = sh.global == SHG_PROJECT && sh.pq == 3u;
        stap(B_OCTUP); sframes(50);
        shome();
        ok1 = ok1 && sh.global == SHG_NONE && project_new_kept();
        stap(B_PLAY); srender(400);
        for (on = 0, k = 0; k < NTRK; k++)
            for (q = 0; q < NVOICE; q++)
                on += trk[k].v[q].active && trk[k].v[q].gate;
        stap(B_PLAY); srender(4); sframe();
        for (k = 0; k < 8u; k++)
            ok1 = ok1 && sh_eu_i[0][0][k] == 0 && sh_eu_i[1][0][k] == 0;
        ok1 = ok1 && !on && sh_partpair[SH_BEAT] == SH_NOPAIR && sh_partpair[SH_LINE] == SH_NOPAIR;
        /* 2 */
        sgo2(fm, tz);                                   /* (the wheel: HOME held, TONNETZ's key) */
        ok2 = sh_track_note(SH_LINE) == tz;
        sgo2(fm, kn);                                   /* left, not played */
        stap(B_PLAY); srender(400);
        for (on = 0, q = 0; q < NVOICE; q++)
            on += trk[SH_LINE].v[q].active && trk[SH_LINE].v[q].gate;
        stap(B_PLAY); srender(4); sframe();
        ok2 = ok2 && sh_track_note(SH_LINE) != tz && !on;
        /* 3 */
        sdown(B_HOME); sframes(SH_TAP_MS + 100u); stap(B_GLO); sup(B_HOME); sframe();   /* PAIR: ANALOG + ACID */
        for (k = 0; k < SH_NMODES && (sh_ppend.on ? sh_ppend.m : sh.mode) != an; k++)
            sturn(EN_K1, 1);
        for (k = 0; k < SH_NNOTES && (sh_ppend.on ? sh_ppend.n : sh.note) != ac; k++)
            sturn(EN_K2, 1);
        shome(); sframe();
        t = sh_trk();
        ok3 = sh.note == ac && sh.mode == an && sh.part == SH_LINE && t->p[P_VOICE] == V_LEGATO &&
              str_eq(ENGINES[0]->presets[t->preset % ENGINES[0]->npresets].name, "ACID");
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; song.octave = 0;
        stap(B_REC);                                    /* armed, stopped: pitch mode, from step 1 */
        ok3 = ok3 && sh_acid_entry() && sh_acid_cur == 0u;
        for (k = 0; k < 16u; k++) {
            if (k == 3u || k == 11u)
                stap(B_SEQ);                            /* a rest */
            else if (k == 7u) {
                stap(B_OCTUP); skey((uint32_t)sh_white_key(LINE16[k])); stap(B_OCTDN);   /* a high note */
            } else
                skey((uint32_t)sh_white_key(LINE16[k]));
        }
        stap(B_REC);                                    /* REC again: the end */
        ok3 = ok3 && !sh_acid_entry() && t->step[0].n && t->step[0].note[0] == NE_KEY_F3 + sh_white_key(0) &&
              t->step[3].time == ST_REST && t->step[11].time == ST_REST &&
              t->step[7].note[0] == NE_KEY_F3 + 12 + sh_white_key(9) && t->step[15].n;
        KDN((uint32_t)sh_white_key(4)); skey(SH_PG_LEFT); KUP((uint32_t)sh_white_key(4));   /* step 5 + C#5: ACCENT */
        KDN((uint32_t)sh_white_key(5)); skey(SH_PG_RIGHT); KUP((uint32_t)sh_white_key(5));  /* step 6 + D#5: SLIDE */
        ok3 = ok3 && (t->step[4].flags & SF_ACCENT) && (t->step[5].flags & SF_SLIDE) && t->step[4].n && t->step[5].n;
        stap(B_PLAY); srender(200);
        for (on = 0, q = 0; q < NVOICE; q++)
            on += t->v[q].active;
        sgo2(dr, rn);                                   /* left playing, RUDIMENT in front */
        for (c = 0, k = 0; k < 200u; k++) {
            srender(1);
            for (q = 0; q < NVOICE; q++)
                c += trk[SH_LINE].v[q].active && trk[SH_LINE].v[q].gate;
        }
        ok3 = ok3 && on && c && sh_track_note(SH_LINE) == ac;
        /* 4: still playing */
        t = sh_trk();
        if (!sh_view_note()) {                          /* TAB: RUDIMENT's NOTE MAIN, the lane's LEN FILL ROT */
            sframes(400);
            stap(B_HOME);
        }
        ok4 = sh.page[sh.mode] == SHP_NOTE;
        skey(SH_BLACK_KEYS[0]);                         /* KICK: FILL 4 (an empty lane: written at once) */
        for (k = 0; k < 4u; k++) sturn(EN_K3, 1);
        skey(SH_BLACK_KEYS[3]);                         /* HATCL by hand: 3, 7, 11, 16 */
        skey((uint32_t)sh_white_key(2)); skey((uint32_t)sh_white_key(6)); skey((uint32_t)sh_white_key(10));
        skey((uint32_t)sh_white_key(15));
        for (b0 = 0, k = 0; k < 16u; k++) b0 |= ((step_lanes(&t->step[k]) >> 3) & 1u) << k;
        KDN(SH_BLACK_KEYS[3]); sturn(EN_K4, 1); KUP(SH_BLACK_KEYS[3]);   /* HATCL held + K4: turned a step, at once */
        for (b1 = 0, k = 0; k < 16u; k++) b1 |= ((step_lanes(&t->step[k]) >> 3) & 1u) << k;
        ok4 = ok4 && b0 == 0x8444u && b1 == ((b0 << 1) | (b0 >> 15)) % 0x10000u;
        KDN(SH_BLACK_KEYS[0]);                          /* KICK held + K2 down to 12: four over 12 */
        for (k = 0; k < 4u; k++) sturn(EN_K2, -1);
        KUP(SH_BLACK_KEYS[0]);
        for (c = 0, k = 0; k < 12u; k++) c += step_lanes(&t->step[k]) & 1u;
        ok4 = ok4 && sh_lane_len(0) == 12u && shell_lane_len[0] == 12u && c == 4u && (step_lanes(&t->step[0]) & 1u) &&
              (step_lanes(&t->step[3]) & 1u);
        /* 5 */
        sgo2(fm, ln);                                   /* LINES on FM6 (ACID's LINE given over) */
        t = sh_trk();
        for (q = 0; q < 3u; q++) {                      /* three lanes: hits by hand, LENs 16, 12, 10 */
            skey(SH_BLACK_KEYS[q]);
            skey((uint32_t)sh_white_key(q)); skey((uint32_t)sh_white_key(q + 4u));
            KDN(SH_BLACK_KEYS[q]);
            for (k = 0; k < (q ? 2u * q + 2u : 0u); k++) sturn(EN_K2, -1);   /* (=LEN 16, then 12, 10) */
            KUP(SH_BLACK_KEYS[q]);
        }
        for (b0 = 0, k = 0; k < 16u; k++) b0 |= ((step_lanes(&t->step[k]) >> 1) & 1u) << k;
        KDN(SH_BLACK_KEYS[1]); sturn(EN_K4, 1); KUP(SH_BLACK_KEYS[1]);   /* lane 2 held + K4 while it plays */
        for (b1 = 0, k = 0; k < 16u; k++) b1 |= ((step_lanes(&t->step[k]) >> 1) & 1u) << k;
        ok5 = song.playing && sh_lane_len(0) == 16u && sh_lane_len(1) == 12u && sh_lane_len(2) == 10u &&
              b0 == 0x22u && b1 == 0x44u && sh_lane_len(1) == 12u;
        stap(B_PLAY); srender(4); sframe();
        for (k = 0; k < NPART; k++) for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        for (q = 0; q < SH_NLI; q++) for (k = 0; k < 8u; k++) sh_eu_i[q][0][k] = sh_eu_i[q][1][k] = sh_eu_i[q][2][k] = 0;
        sgo2(fm, kn);
        printf("  (fundamentals: NEW silent %u, a visit leaves nothing %u, ACID %u, RUDIMENT live %u, LINES live %u)\n",
               ok1, ok2, ok3, ok4, ok5);
        bad += check("fundamentals.md: the owner's five checks by the panel's gestures (NEW then PLAY silent, TONNETZ "
                     "visited leaves nothing, an ACID line by pitch mode with an accent and a slide playing under "
                     "RUDIMENT, RUDIMENT's ROT and LEN live, LINES' lane turned live)", ok1 && ok2 && ok3 && ok4 && ok5);
#undef KDN
#undef KUP
    }

    {   /* a step held, its knobs (sequencing.md): on KEYS' SEQ page KNOB 1-4 the step's CHNC RATCH NUDGE VEL, on the
         * cards as they turn; held into EDIT, a knob locks its value on the step (starred), turned back the lock goes;
         * a tap that empties the step takes its locks. SEQ lit where the white keys are steps, mid on EVOLVE. NUDGE
         * plays (QUANTIZE ON has no key on SQRL-1) and a project keeps it; one saved before (no tag 13) has a QUANTIZE
         * ON part's cleared, as it never played */
#define KDN(k_) do { fm1_in.notes |= 1u << (k_); host_notes |= 1u << (k_); keyboard_block(); sframe(); } while (0)
#define KUP(k_) do { fm1_in.notes &= ~(1u << (k_)); keyboard_block(); sframe(); } while (0)
        uint32_t an = slot_of("ANALOG"), fm = slot_of("FM6"), kn = note_of("KEYS"), k, w2 = (uint32_t)sh_white_key(2),
                 ok1, ok2, ok3, ok4, ok5, id = SH_NONE;
        uint8_t ids[4], pos0[sizeof led_pos];
        int glob;
        int16_t v;
        int32_t f;
        const char *l;
        char val[12];
        track_t *t;
        project_t *p = &proj_scratch;
        sgoc(an, kn);                                   /* (ANALOG: FM6's EDIT is its operators, no locks there) */
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear_locks(t, NSTEP);
        sh.page[sh.mode] = SHP_SEQ; sframe();
        skey(w2);                                       /* a tap: step 3 on */
        KDN(w2);
#define NOTCH(r_, s_) do { sturn((r_), (s_)); sframes(200); } while (0)   /* (one detent, no acceleration) */
        for (k = 0; k < 120u && step_chance_v(&t->step[2]) != 50u; k++)
            NOTCH(EN_K1, -1);                           /* CHNC 50% */
        sh_param_text(0, &l, val);
        ok1 = t->step[2].n && step_chance(&t->step[2]) == 50u && str_eq(l, "CHNC") && str_eq(val, "50%");
        for (k = 0; k < 120u && !step_cond(&t->step[2]); k++)
            NOTCH(EN_K1, 1);                            /* past 100%: the conditions */
        sh_param_text(0, &l, val);
        ok1 = ok1 && step_cond(&t->step[2]) != 0u && str_eq(val, SH_COND[step_cond(&t->step[2]) - 1u]);
        NOTCH(EN_K2, 1); NOTCH(EN_K2, 1);               /* RATCH x3 */
        NOTCH(EN_K3, 1);                                /* NUDGE +1 (RATCH > 1: it waits, said so) */
        ok1 = ok1 && step_ratchet(&t->step[2]) == 3u && step_nudge(&t->step[2]) == 1 && str_eq(sh.toast, "NUDGE WAITS: A RATCHET PLAYS ON TIME");
        NOTCH(EN_K2, -1); NOTCH(EN_K2, -1);
        for (k = 0; k < 40u && t->step[2].vel != 116u; k++)
            NOTCH(EN_K4, 1);                            /* VEL 116 */
        sh_param_text(3, &l, val);
        f = sh_param_frac(3);
        ok1 = ok1 && step_ratchet(&t->step[2]) == 1u && t->step[2].vel == 116u && str_eq(val, "116") && f > 200;
        ok1 = ok1 && t->p[P_SLEN] == 16 && t->step[2].n;   /* (LEN untouched; the hold was not a tap) */
        /* the lock: held into EDIT */
        sh.lastedit[sh.mode] = 0;                       /* (EDIT 1, not the EDIT page a test before left) */
        stap(B_EDIT);
        ok2 = sh_stp.on && sh.page[sh.mode] != SHP_SEQ;
        sh_page_ids(ids, &glob);
        for (k = 0; k < 4u && id == SH_NONE; k++)
            if (ids[k] < SP_FIRST && motion_param(ids[k]))
                id = ids[k];
        k = 0;
        while (k < 4u && ids[k] != id)
            k++;
        {
            int16_t base = t->p[id];
            NOTCH(KR4[k], 1);
            ok2 = ok2 && motion_lock_get(t, 2, id, &v) && v != base && t->p[id] == base;   /* (the sound's own stays) */
            sh_param_text(k, &l, val);
            ok2 = ok2 && l[0] == '*';
            NOTCH(KR4[k], -1);                          /* back to the sound's own: no lock */
            ok2 = ok2 && !motion_lock_get(t, 2, id, &v) && str_eq(sh.toast + str_len(sh.toast) - 9, " LOCK OFF");
            NOTCH(KR4[k], 1);
            ok2 = ok2 && motion_lock_get(t, 2, id, &v);
        }
        KUP(w2);
        ok2 = ok2 && !sh_stp.on && t->step[2].n;        /* (let go on EDIT: never a tap) */
        /* SEQ lit where the keys are steps */
        memcpy(pos0, led_pos, sizeof pos0);
        memset(led_pos, 0xFF, sizeof led_pos);
        led_pos[panel.btn[B_SEQ]] = 0u;
        shell_leds();
        ok3 = !(fm1_led[0] & 1u);                       /* EDIT: the keys play */
        sh.page[sh.mode] = SHP_SEQ; sframe(); shell_leds();
        ok3 = ok3 && (fm1_led[0] & 1u);
        stap(B_SEQ); shell_leds();                      /* EVOLVE: SEQ's page, no steps on the keys: mid */
        ok3 = ok3 && sh.page[sh.mode] != SHP_SEQ && !(fm1_led[0] & 1u) && (fm1_led_mid[0] & 1u);
        sdown(B_SEQ); sframes(400); shell_leds();       /* SEQ held: steps */
        ok3 = ok3 && (fm1_led[0] & 1u);
        sup(B_SEQ); sframe();
        memcpy(led_pos, pos0, sizeof pos0);
        /* a tap empties step 3: its lock goes */
        sh.page[sh.mode] = SHP_SEQ; sframe();
        skey(w2);
        ok4 = !t->step[2].n && !motion_lock_get(t, 2, id, &v);
        /* NUDGE plays with QUANTIZE ON; a project keeps it; an older one's is cleared */
        skey(w2);
        step_set_nudge(&t->step[2], 3);
        ok5 = t->p[P_SQNT] && seq_nudge(t, &t->step[2]) == 3;
        ok5 = ok5 && project_save(2) == 0;
        step_set_nudge(&t->step[2], 0);
        project_load(2);
        t = &trk[sh.part];
        ok5 = ok5 && step_nudge(&t->step[2]) == 3;
        project_capture(p);
        p->nudge_ok = 0;
        p->sum = proj_sum(p);
        ok5 = ok5 && !project_restore_runtime(p) && step_nudge(&trk[sh.part].step[2]) == 0;
        t = sh_trk();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear_locks(t, NSTEP);
        sh.page[sh.mode] = SHP_HOME; sframe();
        {   /* RUDIMENT's NOTE MAIN: the held step's four; the mix (DELAY, a page of the whole mix in FX's family): the
             * knobs as ever */
            uint32_t pg, w4 = (uint32_t)sh_white_key(4);
            int16_t *dv;
            sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
            t = sh_trk();
            for (k = 0; k < NSTEP; k++)
                step_clear(&t->step[k]);
            sh.page[sh.mode] = SHP_NOTE; sframe();
            skey(w4);                                   /* a hit on step 5 */
            KDN(w4);
            NOTCH(EN_K2, 1);                            /* (the lane in front's own RATCH, not the column's) */
            ok1 = ok1 && step_lanes(&t->step[4]) && step_ratchet(&t->step[4]) == 1u &&
                  shell_lstep[sh_li][sh_sp[SP_LANE - SP_FIRST] % 8u][4] >> 6 == 1u;
            for (pg = 0; pg < SHP_N; pg++) {           /* a page of the mix, held into it */
                sh.page[sh.mode] = (uint8_t)pg;
                sh_page_ids(ids, &glob);
                if (SHP_FAM[pg] == B_FX && glob && ids[0] != SH_NONE)
                    break;
            }
            sframe();
            ok2 = ok2 && pg < SHP_N;
            if (pg < SHP_N) {
                (void)sh_desc(ids[0], glob, &dv);
                v = *dv;
                NOTCH(EN_K1, -1);
                ok2 = ok2 && *dv != v && step_chance(&t->step[4]) == 100u;
                *dv = v;
            }
            KUP(w4);
            for (k = 0; k < NSTEP; k++)
                step_clear(&t->step[k]);
            memset(shell_lstep, 0, sizeof shell_lstep);
            sh.page[sh.mode] = SHP_HOME; sframe();
        }
        sgo2(fm, kn);
        printf("  (step knobs %u, a lock %u, SEQ lit %u, locks go with the step %u, NUDGE kept %u)\n", ok1, ok2, ok3,
               ok4, ok5);
        bad += check("a step held: KNOB 1-4 its CHNC (then the conditions) RATCH NUDGE VEL, shown on the cards; held into "
                     "EDIT a knob locks on the step, back to the sound's own it goes; an emptied step loses its locks; "
                     "SEQ lit where the white keys are steps; NUDGE plays and a project keeps it, an older one's "
                     "cleared", ok1 && ok2 && ok3 && ok4 && ok5);
#undef NOTCH
#undef KDN
#undef KUP
    }

    {   /* a lane's DIV (sequencing.md): a lane key held, KNOB 1 turns it; HATCL at 2/3 under a 16-step kick plays 24
         * hats in the 16 part steps, each on its 2/3 grid (within a block), the kick 4; kept over save and load, DOUBLE
         * and HALF; a logic lane's says it plays at x1 */
#define KDN(k_) do { fm1_in.notes |= 1u << (k_); host_notes |= 1u << (k_); keyboard_block(); sframe(); } while (0)
#define KUP(k_) do { fm1_in.notes &= ~(1u << (k_)); keyboard_block(); sframe(); } while (0)
#define NOTCH(r_, s_) do { sturn((r_), (s_)); sframes(200); } while (0)
        uint32_t k, q, b, P, nh = 0, nk = 0, okt = 1, b0 = ~0u, ok1, ok2, ok3, ok4, age[NVOICE];
        track_t *t;
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        for (k = 0; k < 8u; k++) {
            sh_eu[0][k] = sh_eu[1][k] = sh_eu[2][k] = 0;
            shell_lane_len[k] = 0;
            sh_ediv[k] = 0;
        }
        t->p[P_SLEN] = 16; t->p[P_SSWING] = 0;
        memset(shell_lstep, 0, sizeof shell_lstep);
        for (k = 0; k < 16u; k += 4u) grid_hit(t, k, 0, 1u);   /* KICK on the beats, HATCL on every step */
        for (k = 0; k < 16u; k++) grid_hit(t, k, 3, 1u);
        KDN(SH_BLACK_KEYS[3]);
        NOTCH(EN_K1, 1); NOTCH(EN_K1, 1);                /* x1 > 3/4 > 2/3 */
        ok1 = sh_ediv[3] == 2 && strstr(sh.toast, "DIV 2/3") != 0;
        KUP(SH_BLACK_KEYS[3]);
        sframe();
        P = div_samples((uint32_t)t->p[P_SDIV]);
        for (q = 0; q < NVOICE; q++) age[q] = t->v[q].age;
        transport_req = 1;
        for (b = 0; (b + 2u) * HALF_FRAMES <= 16u * P; b++) {
            srender(1);
            if (!(b & 7u)) sframe();
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].age != age[q]) {
                    age[q] = t->v[q].age;
                    if (t->v[q].note == lane_note(t, 0)) {
                        if (b0 == ~0u) b0 = b;
                        nk++;
                    } else if (t->v[q].note == lane_note(t, 3)) {
                        uint32_t want = (b0 == ~0u ? b : b0) + nh * 2u * P / 3u / HALF_FRAMES;
                        okt = okt && b + 1u >= want && b <= want + 1u;
                        nh++;
                    }
                }
        }
        transport_req = 2; srender(4); sframe();
        printf("  (DIV 2/3: %u hats, %u kicks in 16 steps, on the grid %u)\n", nh, nk, okt);
        ok2 = nh == 24u && nk == 4u && okt;
        ok3 = project_save(2) == 0;
        sh_ediv[3] = 0;
        project_load(2);
        t = sh_trk();
        ok3 = ok3 && sh_ediv[3] == 2;
        sh_double();
        ok4 = t->p[P_SLEN] == 32 && sh_ediv[3] == 2;
        sh_halve();
        ok4 = ok4 && t->p[P_SLEN] == 16 && sh_ediv[3] == 2;
        sh_halfk.on = 0;                                /* (the HALF's DOUBLE-back not left for the tests after) */
        sh_eu[1][4] = -1;                               /* a logic lane: its DIV says x1 */
        KDN(SH_BLACK_KEYS[4]);
        NOTCH(EN_K1, 1);
        ok4 = ok4 && strstr(sh.toast, "AT x1 HERE") != 0;
        KUP(SH_BLACK_KEYS[4]);
        sh_eu[1][4] = 0; sh_ediv[4] = 0; sh_ediv[3] = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh_pg_hand = 0;
        sframe();
        sgo2(0, note_of("KEYS"));
        sh_pg_hand = 0;
        printf("  (DIV: knob %u, triplets %u, save and load %u, DOUBLE HALF and logic %u)\n", ok1, ok2, ok3, ok4);
        bad += check("a lane's DIV: KNOB 1 with a lane held; HATCL at 2/3 under a 16-step kick plays 24 hats on the 2/3 "
                     "grid; kept over save and load, DOUBLE and HALF; a logic lane plays at x1", ok1 && ok2 && ok3 && ok4);
#undef NOTCH
#undef KDN
#undef KUP
    }
    {   /* a lane's own CHNC and RATCH (one byte a lane a step): a 12-step KICK over a 16-step part, x3 on HATCL's step 5,
         * 50 % on SNARE's step 3; over 12 bars only those hits change: the kicks all play, the hats gain two a bar, the
         * snare misses some step 3s and never another; and an older project's column CHNC goes to every lane */
        uint32_t k, q, b, P, nk = 0, nh = 0, ns = 0, ns3 = 0, age[NVOICE], ok1, ok2, ok3;
        track_t *t;
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        for (k = 0; k < 8u; k++) {
            sh_eu[0][k] = sh_eu[1][k] = sh_eu[2][k] = 0;
            shell_lane_len[k] = 0;
            sh_ediv[k] = 0;
        }
        t->p[P_SLEN] = 16; t->p[P_SSWING] = 0;
        sh_eu[0][0] = 12; shell_lane_len[0] = 12;      /* KICK, its own 12 */
        for (k = 0; k < 12u; k += 3u) grid_hit(t, k, 0, 1u);
        for (k = 2; k < 16u; k += 4u) grid_hit(t, k, 1, 1u);   /* SNARE on 3 7 11 15 */
        for (k = 0; k < 16u; k++) grid_hit(t, k, 3, 1u);       /* HATCL on all */
        shell_lstep[0][3][4] = sh_ls_b(100u, 3u);
        shell_lstep[0][1][2] = sh_ls_b(50u, 1u);
        sframe();
        P = div_samples((uint32_t)t->p[P_SDIV]);
        for (q = 0; q < NVOICE; q++) age[q] = t->v[q].age;
        transport_req = 1;
        for (b = 0; (b + 2u) * HALF_FRAMES <= 192u * P; b++) {
            srender(1);
            if (!(b & 7u)) sframe();
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].age != age[q]) {
                    age[q] = t->v[q].age;
                    if (t->v[q].note == lane_note(t, 0)) nk++;
                    else if (t->v[q].note == lane_note(t, 3)) nh++;
                    else if (t->v[q].note == lane_note(t, 1)) {
                        ns++;
                        ns3 += t->seq_idx % 16u == 2u;
                    }
                }
        }
        transport_req = 2; srender(4); sframe();
        ok1 = nk == 64u && nh == 192u + 24u;            /* (12 x 16 / 12 x 4 kicks; 2 more hats a bar) */
        ok2 = ns - ns3 == 36u && ns3 > 0u && ns3 < 12u;  /* (step 3 at 50 %, 7 11 15 always) */
        {   /* an older project: the column's 50 % on step 9 to every lane, the column cleared */
            for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
            memset(shell_lstep, 0, sizeof shell_lstep);
            grid_hit(t, 8, 0, 1u);
            step_set_chance(&t->step[8], 50u);
            sh_ls_migrate();
            ok3 = sh_ls_v(shell_lstep[0][0][8]) == 50u && sh_ls_v(shell_lstep[0][5][8]) == 50u &&
                  !t->step[8].probability && sh_ls_v(shell_lstep[0][0][7]) == 100u;
        }
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        sh_eu[0][0] = 0; shell_lane_len[0] = 0;
        sgo2(0, note_of("KEYS"));
        printf("  (own bytes: kicks %u hats %u %u; snares %u, %u on step 3 %u; an old project migrates %u)\n", nk, nh,
               ok1, ns, ns3, ok2, ok3);
        bad += check("a lane's own CHNC and RATCH: a 12-step KICK under 16, x3 on HATCL's step 5, 50 % on SNARE's "
                     "step 3: only those hits change; an older project's column CHNC goes to every lane", ok1 && ok2 && ok3);
    }
    {   /* the lanes' bytes through the pattern tools: DOUBLE copies them with the steps (a following lane's over LEN, an
         * own lane's over its LEN), and a lane whose own LEN cannot double in 64 stays as it is, writing nothing past
         * its row; a clear clears them and REC's ring brings them back with a lane's DIV; NEW empties them and NEW's
         * undo brings them back */
        uint32_t k, ok1, ok2, ok3, ok4;
        track_t *t;
        uint8_t guard[NSTEP];
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        for (k = 0; k < 8u; k++) {
            sh_eu[0][k] = sh_eu[1][k] = sh_eu[2][k] = 0;
            shell_lane_len[k] = 0;
            sh_ediv[k] = 0;
        }
        t->p[P_SLEN] = 16;
        grid_hit(t, 1, 3, 1u); shell_lstep[0][3][1] = sh_ls_b(50u, 1u);   /* HATCL step 2 at 50 %, following LEN */
        sh_eu[0][0] = 6; shell_lane_len[0] = 6;         /* KICK its own 6, x2 on its step 4 */
        grid_hit(t, 3, 0, 1u); shell_lstep[0][0][3] = sh_ls_b(100u, 2u);
        sh_halfk.on = 0;
        sh_double(); sframe();
        ok1 = t->p[P_SLEN] == 32 && sh_ls_v(shell_lstep[0][3][17]) == 50u && (shell_lstep[0][0][9] >> 6) == 1u &&
              shell_lane_len[0] == 12u;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);   /* LEN 48, KICK its own 40: no room for 80 */
        memset(shell_lstep, 0, sizeof shell_lstep);
        t->p[P_SLEN] = 48;
        sh_eu[0][0] = 40; shell_lane_len[0] = 40;
        for (k = 0; k < NSTEP; k++) shell_lstep[0][1][k] = (uint8_t)(k | 1u);   /* (the next lane's row: a guard) */
        memcpy(guard, shell_lstep[0][1], sizeof guard);
        shell_lstep[0][0][39] = sh_ls_b(75u, 1u);
        grid_hit(t, 39, 0, 1u);
        sh_halfk.on = 0;
        sh_double(); sframe();
        ok2 = t->p[P_SLEN] == 64 && shell_lane_len[0] == 40u && sh_eu[0][0] == 40 &&
              !memcmp(guard, shell_lstep[0][1], 48u);   /* (KICK's 40..79 would have run into these) */
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        sh_eu[0][0] = 0; shell_lane_len[0] = 0;
        t->p[P_SLEN] = 16;
        grid_hit(t, 2, 1, 1u); shell_lstep[0][1][2] = sh_ls_b(25u, 3u);
        sh_ediv[1] = 2;                                 /* (SNARE at 2/3: REC's ring brings that back too) */
        sh_rhy_sum = 0;
        sh_clear(); sframe();
        ok3 = !shell_lstep[0][1][2];
        sh_ediv[1] = 0;
        sh_take_undo(); sframe();
        ok3 = ok3 && shell_lstep[0][1][2] == sh_ls_b(25u, 3u) && sh_ediv[1] == 2 && step_lanes(&t->step[2]);
        sh_ediv[1] = 0;
        project_new(); sframes(100);
        ok4 = !shell_lstep[0][1][2] && project_new_kept();
        project_new_undo(); sframes(100);
        ok4 = ok4 && shell_lstep[0][1][2] == sh_ls_b(25u, 3u);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        sgo2(0, note_of("KEYS"));
        printf("  (bytes: DOUBLE copies %u, an own 40 stays %u, clear and undo with DIV %u, NEW and its undo %u)\n",
               ok1, ok2, ok3, ok4);
        bad += check("the lanes' bytes: DOUBLE copies them, an own LEN with no room stays and writes nothing past its "
                     "row; a clear and REC's ring (DIV too); NEW and NEW's undo", ok1 && ok2 && ok3 && ok4);
    }

    {   /* a lane's LEN never rewrites it: hits placed by hand stay put, the steps past a shorter LEN come back with a
         * longer one; FILL still writes the lane; the lane's steps 17-32 by the page; ERASE on the page */
        track_t *t;
        uint32_t k, ok1, ok2, ok3, l, q;
        sgo(TM_RUD);
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 32;
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = 0; sh_eu[1][l] = 0; sh_eu[2][l] = 0; shell_lane_len[l] = 0;
        }
        skey(SH_BLACK_KEYS[0]);                                     /* KICK in front, =LEN (32) */
        sh.page[sh.mode] = SHP_NOTE; sframe();
        {
            uint8_t ids[4];
            int glob;
            sh_page_ids(ids, &glob);
            ok1 = ids[1] == SP_ELEN && ids[2] == SP_EFILL;
        }
        skey((uint32_t)sh_white_key(3));                            /* a hit on 4 */
        skey(SH_PG_RIGHT); sframe();                                /* nothing held: D#5 pages */
        ok1 = ok1 && str_eq(sh.toast, "STEPS 17-32") && sh_pg() == 16u;
        skey((uint32_t)sh_white_key(4)); skey((uint32_t)sh_white_key(9));   /* hits on 21 and 26 */
        ok1 = ok1 && (step_lanes(&t->step[3]) & 1u) && (step_lanes(&t->step[20]) & 1u) && (step_lanes(&t->step[25]) & 1u) &&
              !(step_lanes(&t->step[4]) & 1u);
        sdown(B_REC); sframe(); skey((uint32_t)sh_white_key(9)); sup(B_REC); sframe();   /* ERASE 26, on the page */
        ok1 = ok1 && !(step_lanes(&t->step[25]) & 1u) && (step_lanes(&t->step[20]) & 1u);
        for (k = 0; k < 16u; k++)
            sturn(EN_K2, -1);                                       /* LEN =LEN (32) -> 16, its own */
        ok2 = sh_lane_len(0) == 16u && shell_lane_len[0] == 16u && (step_lanes(&t->step[3]) & 1u) &&
              (step_lanes(&t->step[20]) & 1u) && sh_pg() == 16u;    /* (the part's pages: the lane ends on page 1) */
        for (k = 0; k < 16u; k++)
            sturn(EN_K2, 1);                                        /* back to =LEN: 21 still there */
        ok2 = ok2 && sh_lane_len(0) == 32u && sh_eu[0][0] == 0 && (step_lanes(&t->step[3]) & 1u) &&
              (step_lanes(&t->step[20]) & 1u);
        sturn(EN_K3, 1);                                            /* a hand lane: FILL's first detent asks */
        ok3 = str_eq(sh.toast, "HAND 2: TURN AGAIN") && (step_lanes(&t->step[20]) & 1u);
        sturn(EN_K3, 1);                                            /* FILL 1: the lane written from FILL */
        for (k = 0, q = 0; k < 32u; k++) q += step_lanes(&t->step[k]) & 1u;
        ok3 = ok3 && q == 1u && (step_lanes(&t->step[0]) & 1u);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        for (l = 0; l < 8u; l++) {
            sh_eu[0][l] = 0; sh_eu[1][l] = 0; sh_eu[2][l] = 0; shell_lane_len[l] = 0;
        }
        t->p[P_SLEN] = 16;
        sh_pg_hand = 0;
        sframe();
        printf("  (lane pages and erase %u; LEN keeps hits %u; FILL writes %u)\n", ok1, ok2, ok3);
        bad += check("a lane's LEN keeps its hits and the steps past it; FILL still writes it; the lane's steps 17-32 "
                     "by the page, ERASE too", ok1 && ok2 && ok3);
    }
    {   /* a take on CHORD's part plays through CHORD PLAY: recorded at STRM 0, played back at STRM 100 its notes start
         * low to high across STRM's spread, at -100 high to low, at 0 together; TOP lifts the top note as it plays */
        track_t *t;
        uint32_t k, q, acc = 0, ok1, ok2, ok3, ok4, notes = 0, cs = 0;
        uint32_t lastage[NVOICE], at[3][128];
        uint8_t topv = 0, lowv = 0;
        int32_t strm[3] = {100, -100, 0}, c;
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        sh_sp[SP_STRM - SP_FIRST] = 0; sh_sp[SP_RPT - SP_FIRST] = 0; sh_sp[SP_TOP - SP_FIRST] = 0; sframe();
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(2u);
        OD_KON(CB_ROOT0); OD_RUN(500u); OD_KOFF(CB_ROOT0);   /* (held: a strum's spread fits; a tap's gate would
                                                             * end it, as a key let go does) */
        song.rec = 0; OD_RUN(32u);
        transport_req = 2; srender(4); sframe();
        for (k = 0, cs = 0; k < 16u; k++)
            if (t->step[k].time == ST_NOTE && t->step[k].n >= 3u) {
                notes++;
                cs = k;
            }
        ok1 = notes == 1u;
        for (c = 0; c < 3; c++) {                       /* played back at each STRM, the first strike */
            uint32_t got = 0;
            sh_sp[SP_STRM - SP_FIRST] = (int16_t)strm[c]; sframe();
            memset(t->v, 0, sizeof t->v);
            memset(at[c], 0xFF, sizeof at[c]);
            for (q = 0; q < NVOICE; q++)
                lastage[q] = 0;
            transport_req = 1; srender(2);
            for (k = 0; k < 2u * FS / HALF_FRAMES / 4u && got < 3u; k++) {   /* (half a second) */
                srender(1);
                if (!(k & 3u))
                    sframe();
                for (q = 0; q < NVOICE; q++)
                    if (t->v[q].active && t->v[q].age != lastage[q]) {
                        lastage[q] = t->v[q].age;
                        if (t->v[q].note < 128u && at[c][t->v[q].note] == 0xFFFFFFFFu) {
                            at[c][t->v[q].note] = k;
                            got++;
                        }
                    }
            }
            transport_req = 2; srender(4); sframe();
        }
        {
            uint32_t lo = 127, hi = 0;
            for (q = 0; q < STEP_NOTES; q++)
                if (q < t->step[cs].n) {
                    lo = t->step[cs].note[q] < lo ? t->step[cs].note[q] : lo;
                    hi = t->step[cs].note[q] > hi ? t->step[cs].note[q] : hi;
                }
            ok2 = at[0][lo] != 0xFFFFFFFFu && at[0][hi] != 0xFFFFFFFFu && at[0][hi] > at[0][lo] + 30u;   /* (~200 ms
                                                                                           * apart: 63 blocks) */
            ok3 = at[1][lo] != 0xFFFFFFFFu && at[1][hi] != 0xFFFFFFFFu && at[1][lo] > at[1][hi] + 30u;
            ok4 = at[2][lo] != 0xFFFFFFFFu && at[2][lo] == at[2][hi];
            sh_sp[SP_STRM - SP_FIRST] = 0; sh_sp[SP_TOP - SP_FIRST] = 127; sframe();   /* TOP: the top note over */
            memset(t->v, 0, sizeof t->v);
            transport_req = 1; srender(2);
            for (k = 0; k < 2u * FS / HALF_FRAMES / 4u && !t->seq_n; k++)   /* (to the chord's step) */
                srender(1);
            srender(2);
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].note == hi)
                    topv = t->v[q].vel;
                else if (t->v[q].active && t->v[q].note == lo)
                    lowv = t->v[q].vel;
            transport_req = 2; srender(4); sframe();
            ok4 = ok4 && topv > lowv;
        }
        sh_sp[SP_STRM - SP_FIRST] = 0; sh_sp[SP_TOP - SP_FIRST] = 0; sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        printf("  (one step %u; STRM 100 low first %u; -100 high first %u; 0 together, TOP %u/%u %u)\n", ok1, ok2, ok3,
               topv, lowv, ok4);
        bad += check("CHORD's take plays through CHORD PLAY: STRM turned after the take strums it up or down, at 0 "
                     "together; TOP lifts its top note", ok1 && ok2 && ok3 && ok4);
    }
    {   /* the defaults: LIZARD's first visit plays POLY (its first preset is a MONO lead); PHYS on the wheel's C5 */
        uint32_t lz = slot_of("LIZARD"), fm = slot_of("FM6");
        uint8_t w0[SH_WHEEL_N], c5;
        sgo2(fm, 0);
        sh_wheel_get(w0);
        sh_wheel_factory();
        c5 = sh_wslot[7];
        (void)sh_wheel_set(w0);
        sh_snap_of(lz)->valid = 0;                          /* (a first visit) */
        sgo2(lz, 0);
        bad += check("defaults: LIZARD starts POLY; PHYS sits on C5", sh.mode == lz && sh_trk()->p[P_VOICE] == V_POLY &&
                     sh_trk()->eng_req == ENGI_LIZARD && c5 == slot_of("PHYS"));
        sgo2(fm, 0);
    }
    {   /* SEQ + a white key under CHORD: the last chord played, whole, into the step (KEYS: the last note alone) */
        track_t *t;
        uint32_t k, ok = 1, same = 1;
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        fm1_in.notes |= 1u << CB_ROOT0; host_notes |= 1u << CB_ROOT0; keyboard_block(); srender(8); sframe();
        fm1_in.notes &= ~(1u << CB_ROOT0); keyboard_block(); srender(40); sframe();
        sdown(B_SEQ); sframes(400);
        skey((uint32_t)sh_white_key(5));
        sup(B_SEQ); sframe();
        ok = chord_last[sh.part].n >= 3u && t->step[5].n == chord_last[sh.part].n && t->step[5].time == ST_NOTE;
        for (k = 0; k < t->step[5].n && k < chord_last[sh.part].n; k++)
            same = same && t->step[5].note[k] == chord_last[sh.part].note[k];
        sdown(B_SEQ); sframes(400);
        skey((uint32_t)sh_white_key(5));                /* (again: off) */
        sup(B_SEQ); sframe();
        ok = ok && same && !t->step[5].n;
        bad += check("SEQ + a white key under CHORD enters the last chord whole, again clears it", ok);
    }
    {   /* a destructive action says it can be undone: HOLD REC: UNDO under the toast (a clear, ERASE, FILL, a beat) */
        track_t *t;
        uint32_t ok;
        sgo(mode_of("FM6"));
        t = sh_trk();
        t->step[2].note[0] = 60; t->step[2].time = ST_NOTE; t->step[2].n = 1;
        sh_clear(); sframe();
        ok = str_eq(sh.toast, "PATTERN CLEARED") && sh.toast_undo;
        shot("toast_undo");
        sh_clear(); sframe();
        ok = ok && str_eq(sh.toast, "THE PATTERN IS EMPTY") && !sh.toast_undo;
        sgo(TM_RUD);
        sh.page[sh.mode] = SHP_NOTE; sframe();          /* (NOTE MAIN: the lane's FILL on KNOB 3) */
        sturn(EN_K3, 1);
        ok = ok && str_eq(sh.toast, "FILL WRITES THE LANE") && sh.toast_undo;
        sturn(EN_K3, -1);
        sdown(B_REC); sframe(); skey((uint32_t)sh_white_key(0)); sup(B_REC); sframe();
        ok = ok && str_eq(sh.toast, "ERASED A STEP") && sh.toast_undo;
        sframes(SH_TOAST_MS + 200u);
        sgo(mode_of("FM6"));
        bad += check("a clear, FILL and ERASE toast HOLD REC: UNDO; an empty pattern's clear does not", ok);
    }
    {   /* REC held past UNDO: a second clear of what is already empty keeps the undo (KEYS, and the lanes) */
        track_t *t;
        uint32_t ok1, ok2, k, l;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[2].note[0] = 60; t->step[2].time = ST_NOTE; t->step[2].n = 1;
        sh_clear(); sframe();
        sh_clear(); sframe();                           /* (the overshoot) */
        ok1 = str_eq(sh.toast, "THE PATTERN IS EMPTY");
        sh_take_undo(); sframe();
        ok1 = ok1 && t->step[2].n == 1u && t->step[2].note[0] == 60u;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sgo(TM_RUD);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        for (l = 0; l < 8u; l++) { sh_eu[0][l] = 0; sh_eu[1][l] = 0; sh_eu[2][l] = 0; shell_lane_len[l] = 0; }
        grid_hit(t, 0, 0, 1u); grid_hit(t, 4, 1, 1u);
        sh_sp[SP_LANE - SP_FIRST] = 0; sframe();
        sh_clear(); sframe();                           /* every lane */
        ok2 = !(step_lanes(&t->step[0]) & 1u) && !(step_lanes(&t->step[4]) & 2u);
        sh_clear(); sframe();                           /* (the overshoot: nothing) */
        ok2 = ok2 && str_eq(sh.toast, "THE BEAT IS EMPTY");
        sh_take_undo(); sframe();
        ok2 = ok2 && (step_lanes(&t->step[0]) & 1u) && (step_lanes(&t->step[4]) & 2u);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sframe();
        sgo(mode_of("FM6"));
        printf("  (KEYS overshoot keeps the undo %u; lanes %u)\n", ok1, ok2);
        bad += check("REC held past UNDO: an empty clear keeps the undo, on the keys and the lanes", ok1 && ok2);
    }
    {   /* the arp's chords on CHORD's part play back as the arp struck them, whole, not through STRM */
        track_t *t;
        uint32_t k, q, lastage[NVOICE], first[3] = {~0u, ~0u, ~0u};
        static const uint8_t NT[3] = {60, 64, 67};
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16; t->p[P_AMODE] = AM_CHORD;
        for (k = 0; k < 3u; k++) t->step[0].note[k] = NT[k];
        t->step[0].time = ST_NOTE; t->step[0].n = 3;
        sh_sp[SP_STRM - SP_FIRST] = 100; sframe();
        memset(t->v, 0, sizeof t->v);
        for (q = 0; q < NVOICE; q++) lastage[q] = 0;
        transport_req = 1; srender(2);
        for (k = 0; k < 60u; k++) {
            srender(1);
            if (!(k & 3u)) sframe();
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].age != lastage[q]) {
                    uint32_t j;
                    lastage[q] = t->v[q].age;
                    for (j = 0; j < 3u; j++)
                        if (t->v[q].note == NT[j] && first[j] == ~0u) first[j] = k;
                }
        }
        transport_req = 2; srender(4); sframe();
        sh_sp[SP_STRM - SP_FIRST] = 0; t->p[P_AMODE] = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sframe();
        bad += check("CHORD with an arp mode: a recorded chord plays back whole, not strummed",
                     first[0] != ~0u && first[0] == first[1] && first[1] == first[2]);
    }
    {   /* REC MODE ADD with the arp recording over a CHORD take: each pass's arp note goes in with the chord the step
         * holds, the next pass takes back only the arp's own; REPLACE: the arp's pass rewrites the step, as before */
        track_t *t;
        uint32_t k, j, p, ok1 = 1, ok2 = 1, ok3 = 1;
        static const uint8_t CH[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
        sgoc(slot_of("ANALOG"), note_of("CHORD"));
        t = sh_trk();
        p = sh.part;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16; t->p[P_VOICE] = V_POLY;
        for (k = 0; k < 4u; k++) {
            for (j = 0; j < 3u; j++) t->step[k * 4u].note[j] = CH[k][j];
            t->step[k * 4u].time = ST_NOTE; t->step[k * 4u].n = 3; t->step[k * 4u].vel = 96;
        }
        sh_take.first = 0;
        sh_rec_mode_set(0u);                            /* ADD */
        song.playing = 1; song.rec = (uint8_t)(1u << p);
#define ARP_PASS(base_) do { rec_pass_next(p); for (k = 0; k < 4u; k++) { t->seq_idx = (uint16_t)(k * 4u); \
            t->seq_pos = 0; rec_note_machine(t, (base_) + k, 100u); } } while (0)
        ARP_PASS(72u);
        for (k = 0; k < 4u; k++) {
            const step_t *st = &t->step[k * 4u];
            ok1 = ok1 && st->n == 4u && st->note[0] == CH[k][0] && st->note[1] == CH[k][1] && st->note[2] == CH[k][2] &&
                  st->note[3] == 72u + k;
        }
        ARP_PASS(84u);                                  /* the next pass: its own note in place of the last one's */
        for (k = 0; k < 4u; k++) {
            const step_t *st = &t->step[k * 4u];
            ok2 = ok2 && st->n == 4u && st->note[0] == CH[k][0] && st->note[2] == CH[k][2] && st->note[3] == 84u + k;
        }
        sh_rec_mode_set(2u);                            /* REPLACE */
        ARP_PASS(48u);
        for (k = 0; k < 4u; k++)
            ok3 = ok3 && t->step[k * 4u].n == 1u && t->step[k * 4u].note[0] == 48u + k;
#undef ARP_PASS
        sh_rec_mode_set(0u);
        song.rec = 0; song.playing = 0;
        t->seq_idx = 0; t->seq_pos = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sframe();
        sgo2(0, note_of("KEYS"));
        printf("  (ADD keeps the chords %u, a pass takes back its own %u, REPLACE rewrites %u)\n", ok1, ok2, ok3);
        bad += check("REC ADD with the arp recording: the chords played stay and the arp's note goes in with them, "
                     "the next pass replacing only the arp's; REPLACE rewrites", ok1 && ok2 && ok3);
    }
    {   /* knob moves go with the pair that recorded them: A (LIZARD+CHORD, a take) -> B (a take) -> C takes the part
         * left longest ago, A's, and says A stops; C plays none of A's moves; A back has its notes and knob moves;
         * another SOUND under CHORD gets neither: a PLAY take is the pair's */
        uint32_t lz = slot_of("LIZARD"), fm = slot_of("FM6"), an = slot_of("ANALOG"), cn = note_of("CHORD"), k, acc = 0;
        uint32_t p0, mc0, ok1, ok2, ok3;
        track_t *t;
        sgo2(lz, cn);
        sh_rec_mode_set(1u);
        t = sh_trk(); p0 = sh.part;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        t->step[3].note[0] = 60; t->step[3].time = ST_NOTE; t->step[3].n = 1;
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(5u);
        (void)motion_capture(t, P_DIST, 44);
        OD_RUN(200u);
        song.rec = 0; transport_req = 2; srender(4); sframe();
        sh_rec_mode_set(0u);
        mc0 = motion_count(t);
        sgo2(fm, 0);                                    /* B: FM6 + KEYS, the other part, a take of its own */
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sh_trk()->step[0].n = 1; sh_trk()->step[0].note[0] = 50; sh_trk()->step[0].time = ST_NOTE; sh_trk()->step[0].vel = 90;
        sframe();
        sh_switch_pair(an, note_of("HARP")); sframe();   /* C: both loop, so A's part, left longest ago */
        {
            char want[48];
            snprintf(want, sizeof want, "LIZARD CHORD STOPS: PART %u", p0 + 1u);
            ok1 = sh.part == p0 && motion_count(sh_trk()) == 0u && str_eq(sh.toast, want);
        }
        sgo2(lz, cn);                                   /* A again: its take and moves back */
        t = sh_trk();
        ok2 = mc0 == 1u && t->step[3].n == 1u && motion_count(t) == 1u;
        sgo2(fm, cn);                                   /* CHORD under FM6: a take of its own (none yet), not
                                                         * LIZARD's notes or knob moves */
        ok3 = sh_trk()->step[3].n == 0u && motion_count(sh_trk()) == 0u;
        sgo2(lz, cn);
        ok3 = ok3 && motion_count(sh_trk()) == 1u;
        motion_clear(sh_trk());
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sframe();
        sgo2(fm, 0);
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sframe();
        printf("  (C on A's part, A stops, plays none %u; A back with its moves %u; FM6+CHORD none, LIZARD+CHORD again %u)\n",
               ok1, ok2, ok3);
        bad += check("knob moves go with the pair that recorded them, over any number of switches", ok1 && ok2 && ok3);
    }
    {   /* a PLAY take is the pair's: FM6 + KEYS a melody, ANALOG + KEYS arrives empty and takes a bass, LIZARD + KEYS
         * arrives empty too (FM6's part, FM6's take parked), FM6 back with its melody, ANALOG still with its bass */
        uint32_t fm = slot_of("FM6"), an = slot_of("ANALOG"), lz = slot_of("LIZARD"), kn = note_of("KEYS"), k;
        uint32_t ok1, ok2, ok3;
        for (k = 0; k < SH_NPK; k++)
            sh_pk_pair[k] = SH_PK_NONE;
        sgoc(fm, kn);
        for (k = 0; k < NSTEP; k++) step_clear(&sh_trk()->step[k]);
        sh_trk()->step[0].note[0] = 72; sh_trk()->step[0].n = 1; sh_trk()->step[0].time = ST_NOTE;
        sframe();
        sgoc(an, kn);
        ok1 = !sh_trk()->step[0].n;                     /* (a pair new to KEYS: no take of FM6's) */
        sh_trk()->step[2].note[0] = 36; sh_trk()->step[2].n = 1; sh_trk()->step[2].time = ST_NOTE;
        sframe();
        sgoc(lz, kn);
        ok2 = !sh_trk()->step[0].n && !sh_trk()->step[2].n;
        sgoc(fm, kn);
        ok3 = sh_trk()->step[0].n == 1u && sh_trk()->step[0].note[0] == 72u && !sh_trk()->step[2].n;
        sgoc(an, kn);
        ok3 = ok3 && sh_trk()->step[2].n == 1u && sh_trk()->step[2].note[0] == 36u && !sh_trk()->step[0].n;
        for (k = 0; k < NPART; k++) {
            uint32_t q;
            for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        }
        for (k = 0; k < SH_NPK; k++)
            sh_pk_pair[k] = SH_PK_NONE;
        sgoc(fm, kn);
        printf("  (ANALOG new %u, LIZARD new %u, FM6 and ANALOG back with their own %u)\n", ok1, ok2, ok3);
        bad += check("a PLAY take is the pair's: two SOUNDs on KEYS keep two takes, a third pair arrives empty and "
                     "each comes back with its own", ok1 && ok2 && ok3);
    }
    {   /* the NOTE engines as their pages say: HARP's chords on SCL CHR are the major scale's (C E G, not C D E); RHY on
         * RUDIMENT's NOTE page loads the rhythm, as ALGORITHM does; ACID's REC ring puts back the LINE its kept line
         * came from (a hand line: none), so DENS never writes over it; TONNETZ's CHORD on a LEGATO SOUND plays three */
        uint32_t k, q, on, ok1, ok2, ok3, ok4, sig0, sig1;
        hp_t h;
        track_t *t;
        memset(&h, 0, sizeof h);
        hp_scale(&h, 0, 0xFFFu);
        ok1 = h.smask == 0xAB5u;
        hp_scale(&h, 9, SCALE_MASK[2]);
        ok1 = ok1 && h.smask == (SCALE_MASK[2] & 0xFFFu);
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh_sp[SP_RHY - SP_FIRST] = 0;
        sh.page[sh.mode] = SHP_NOTE2; sframe();         /* NOTES: KNOB 1 RHY */
        for (sig0 = 0, k = 0; k < NSTEP; k++) sig0 = sig0 * 31u + step_lanes(&t->step[k]);
        sturn(EN_K1, 1); sframes(200);
        for (sig1 = 0, k = 0; k < NSTEP; k++) sig1 = sig1 * 31u + step_lanes(&t->step[k]);
        ok2 = sh_sp[SP_RHY - SP_FIRST] != 0 && sig1 != sig0 && str_eq(sh.toast, "NEW BEAT: IO");
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh_sp[SP_RHY - SP_FIRST] = 0;
        sh.page[sh.mode] = SHP_HOME; sframe();
        sgoc(slot_of("ANALOG"), note_of("ACID"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh_sp[SP_ASEED - SP_FIRST] = 0;
        t->step[0].note[0] = 48; t->step[0].n = 1; t->step[0].time = ST_NOTE;   /* a hand line */
        sframe();
        snote();
        sturn(EN_ALGO, 1); sframes(300);                /* LINE 1 written over it */
        ok3 = sh_sp[SP_ASEED - SP_FIRST] != 0;
        sh_take_undo(); sframe();
        ok3 = ok3 && sh_sp[SP_ASEED - SP_FIRST] == 0 && t->step[0].n == 1u && t->step[0].note[0] == 48u;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sgoc(slot_of("ANALOG"), note_of("TONNETZ"));
        t = sh_trk();
        t->p[P_VOICE] = V_LEGATO;
        transport_req = 1; srender(2);
        for (on = 0, k = 0; k < 400u; k++) {
            uint32_t n = 0;
            srender(1);
            if (!(k & 3u)) sframe();
            for (q = 0; q < NVOICE; q++)
                n += t->v[q].active && t->v[q].gate;
            on = n > on ? n : on;
        }
        transport_req = 2; srender(4); sframe();
        ok4 = on >= 3u;
        sgo2(0, note_of("KEYS"));
        printf("  (HARP major on CHR %u, RHY on the NOTE page %u, ACID's undo %u, TONNETZ three on LEGATO %u)\n",
               ok1, ok2, ok3, ok4);
        bad += check("NOTE engines: HARP stacks the major scale's thirds on CHR; RHY on the NOTE page loads a rhythm; "
                     "ACID's undo puts back the hand line's no-LINE; TONNETZ's triads on a LEGATO "
                     "SOUND", ok1 && ok2 && ok3 && ok4);
    }
    {   /* FILL: SCL held past its tap while it plays: FILL on (a FILL step plays, a !FILL step does not), let go off
         * with no SCALE page; SCL + OCT- (the reset ring) is no FILL; stopped, none; the CHNC list reaches them */
        uint32_t ok1, ok2, ok3, k;
        step_t st;
        track_t *t;
        sgoc(slot_of("ANALOG"), note_of("KEYS"));
        t = sh_trk();
        transport_req = 1; srender(2); sframe();
        sdown(B_SCL); sframes(SH_TAP_MS + 100u);
        memset(&st, 0, sizeof st);
        st.time = ST_NOTE; st.n = 1;
        step_set_chance_v(&st, 100u + COND_FILL);
        ok1 = seq_fill && !seq_cond_miss(t, &st, 0);
        step_set_chance_v(&st, 100u + COND_NFILL);
        ok1 = ok1 && seq_cond_miss(t, &st, 0);
        sturn(EN_K1, 1);                                /* (a knob turned during the fill: still FILL) */
        ok1 = ok1 && seq_fill;
        sup(B_SCL); sframe();
        ok1 = ok1 && !seq_fill && sh.global == SHG_NONE && !seq_cond_miss(t, &st, 0);
        sdown(B_SCL); sframes(SH_TAP_MS + 100u);
        sdown(B_OCTDN); sframe();                       /* the reset ring: a combination, no FILL */
        ok2 = !seq_fill;
        sup(B_OCTDN); sframe(); sup(B_SCL); sframe();
        transport_req = 2; srender(4); sframe();
        sdown(B_SCL); sframes(SH_TAP_MS + 100u);
        ok2 = ok2 && !seq_fill;                         /* (stopped) */
        sup(B_SCL); sframe();
        sh.global = SHG_NONE; sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        ok3 = SH_NCOND == NCOND && str_eq(SH_COND[COND_FILL - 1u], "FILL") && str_eq(SH_COND[COND_NFILL - 1u], "!FILL");
        printf("  (SCL held FILL and let go %u, no FILL in the ring or stopped %u, the list %u)\n", ok1, ok2, ok3);
        bad += check("FILL: SCL held while it plays plays FILL steps and skips !FILL; let go, no SCALE page; not with "
                     "OCT- or stopped; CHNC reaches FILL and !FILL", ok1 && ok2 && ok3);
    }
    {   /* a step's extras shown without holding it: on KEYS' SEQ page a CHNC is a dot under the step, at once (the
         * signature has it), a lock a bone diamond; the step's key MID, a plain one LIT; a RUDIMENT hit the same */
        uint32_t k, w0 = (uint32_t)sh_white_key(0), w1 = (uint32_t)sh_white_key(1), sig, ok1, ok2, ok3;
        track_t *t;
        sgo2(0, note_of("KEYS"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        motion_clear_locks(t, NSTEP);
        sh.page[sh.mode] = SHP_SEQ;
        for (k = 0; k < 2u; k++) {
            t->step[k].note[0] = 60; t->step[k].time = ST_NOTE; t->step[k].n = 1;
        }
        ui.force = 1; sframe();
        sig = sh_signature();
        ok1 = host_screen[188 * 240 + 13] != swap16(SC_DIM) && sh_step_led(w0) == SHL_LIT;
        step_set_chance(&t->step[0], 50);
        ok1 = ok1 && sh_signature() != sig;
        sframe();
        ok1 = ok1 && host_screen[188 * 240 + 13] == swap16(SC_DIM) && sh_step_led(w0) == SHL_MID &&
              sh_step_led(w1) == SHL_LIT;
        ok2 = !motion_set_lock(t, 1, P_ED_FLT, 20);    /* step 2: a lock, its diamond (bone) */
        sframe();
        ok2 = ok2 && host_screen[187 * 240 + 12 + 14 + 1 + 8] == swap16(SC_BONE) && sh_step_led(w1) == SHL_MID;
        motion_clear_locks(t, NSTEP);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh.page[sh.mode] = SHP_HOME;
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sh_sp[SP_LANE - SP_FIRST] = 0;
        grid_hit(t, 0, 0, 1u); grid_hit(t, 4, 0, 1u);
        shell_lstep[0][0][4] = sh_ls_b(100u, 3u);       /* (KICK's own RATCH x3 on step 5) */
        sframe();
        ok3 = sh_inst_led(w0, SHL_OFF) == SHL_LIT && sh_inst_led((uint32_t)sh_white_key(4), SHL_OFF) == SHL_MID;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(shell_lstep, 0, sizeof shell_lstep);
        sgo2(0, note_of("KEYS"));
        printf("  (KEYS: CHNC's dot and MID %u, a lock's diamond %u; RUDIMENT: a RATCH hit MID %u)\n", ok1, ok2, ok3);
        bad += check("a step's CHNC, RATCH, NUDGE or lock shows without holding it: a mark under the step, at once, "
                     "and its key MID", ok1 && ok2 && ok3);
    }
    {   /* switching patterns live: slot B holds KEYS's part with a C5 on step 1 (LEN 16) and RUDIMENT's lanes (KICK
         * LEN 12); playing, SEQ held + PRESETS to B, let go: the old steps play until the bar line (16 steps of the
         * clock), then B's from step 1, the SOUND as it was, the other parts untouched. A part at LEN 12 switches at
         * the clock's bar, not its own loop's end; RUDIMENT's part takes its lanes too; a second slot while B waits is
         * refused, an empty slot is refused */
        uint32_t an = slot_of("ANALOG"), kn = note_of("KEYS"), k, ok1, ok2, ok3, ok4, ok5, kp, rp, e0;
        int16_t e1;
        track_t *t, *r;
        uint8_t keep4[sizeof proj_slot[3]];
#define QN(s_) do { sturn(EN_PRESET, (s_)); sframes(150); } while (0)
        memcpy(keep4, &proj_slot[3], sizeof keep4);
        memset(proj_slot[3].raw, 0, 8);                 /* (slot D empty) */
        transport_req = 2; srender(4); sframe();
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        r = sh_trk(); rp = sh.part;
        for (k = 0; k < NSTEP; k++) step_clear(&r->step[k]);
        sh_eu[0][0] = 12;                               /* B's lanes: KICK LEN 12, a kick on 1 */
        shell_lane_len[0] = 12;
        grid_hit(r, 0, 0, 1u);
        sgoc(an, kn);
        t = sh_trk(); kp = sh.part;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        t->step[0].note[0] = 72; t->step[0].n = 1; t->step[0].time = ST_NOTE;
        sframe();
        ok1 = project_save(1) == 0;                     /* slot B */
        t->step[0].note[0] = 60;                        /* what plays now: a C4, LEN 12 later */
        sh_eu[0][0] = 0; shell_lane_len_i[0][0] = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&r->step[k]);
        e0 = t->eng_req; e1 = t->p[P_E1];
        transport_req = 1; srender(2); sframe();
        while (clk_abs < 3u) { srender(1); sframe(); }
        sdown(B_SEQ); sframes(400);
        QN(1); QN(1);                                   /* OFF > A > B */
        ok1 = ok1 && str_eq(sh.toast, "NEXT B AT THE BAR: SOUND STAYS");
        sup(B_SEQ); sframe();
        ok1 = ok1 && sh_q.mask == (1u << kp) && sh_q.slot == 1u && t->step[0].note[0] == 60u;
        while (clk_abs < 15u) { srender(1); }
        ok1 = ok1 && t->step[0].note[0] == 60u;         /* (before the bar: the old) */
        while (clk_abs < 16u) { srender(1); }
        sframe();
        ok1 = ok1 && t->seq_pos == clk_pos;             /* (on the clock's grid to the sample: both 1/16 from the bar) */
        ok1 = ok1 && t->step[0].note[0] == 72u && t->seq_idx == 0u && t->eng_req == e0 && t->p[P_E1] == e1 &&
              !sh_q.mask && str_eq(sh.toast, "PATTERN B: SOUND STAYS") && !step_lanes(&r->step[0]);
        /* a LEN 12 part: at the clock's bar (step 32), not its own end (step 24) */
        t->step[0].note[0] = 60; t->p[P_SLEN] = 12;
        while (clk_abs < 18u) { srender(1); sframe(); }
        sdown(B_SEQ); sframes(400);
        QN(1); QN(1);
        sup(B_SEQ); sframe();
        while (clk_abs < 30u) { srender(1); }
        ok2 = t->step[0].note[0] == 60u && t->p[P_SLEN] == 12;   /* (its own loop came round at 24: still the old) */
        while (clk_abs < 32u) { srender(1); }
        sframe();
        ok2 = ok2 && t->step[0].note[0] == 72u && t->p[P_SLEN] == 16 && t->seq_idx == 0u;
        /* RUDIMENT's part: its lanes too */
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        sdown(B_SEQ); sframes(400);
        QN(1); QN(1);
        sup(B_SEQ); sframe();
        while (clk_abs < 48u) { srender(1); }
        sframe();
        ok3 = sh.part == rp && shell_lane_len_i[0][0] == 12u && (step_lanes(&r->step[0]) & 1u);
        /* a second slot while B waits; an empty slot */
        sgoc(an, kn);
        sdown(B_SEQ); sframes(400);
        QN(1); QN(1);
        sup(B_SEQ); sframe();                           /* (KEYS: B queued) */
        sgo2(sh_mode_pos(slot_of("DRUMS")), note_of("RUDIMENT"));
        sdown(B_SEQ); sframes(400);
        QN(1); QN(1); QN(1);                            /* C on RUDIMENT's part */
        sup(B_SEQ); sframe();
        ok4 = str_eq(sh.toast, "ONE SLOT AT A TIME: B IS QUEUED") && sh_q.mask == (1u << kp);
        sgoc(an, kn);
        sdown(B_SEQ); sframes(400);
        QN(-1); QN(-1); QN(-1);                         /* B > A > OFF > D */
        sup(B_SEQ); sframe();
        ok5 = str_eq(sh.toast, "SLOT D IS EMPTY") && !sh_q.mask;
        transport_req = 2; srender(4); sframe();
        sh_q.mask = 0; sh_q.slot = SH_Q_OFF;
        {   /* the staging a queued part reads at the bar (chain.source[0]) is written only with interrupts off, so a
             * second part queued from the same slot never moves the first's events under it */
            static uint8_t ln_[2][SH_LANES_N];
            _Static_assert(sizeof chain.source[0] <= sizeof host_watch_copy, "the watch's copy");
            host_masked = 0;
            host_watch = &chain.source[0];
            host_watch_n = sizeof chain.source[0];
            memcpy(host_watch_copy, host_watch, host_watch_n);
            host_watch_bad = 0;
            ok4 = ok4 && proj_q_stage(1, 2, ln_) && !host_watch_bad &&
                  !memcmp(host_watch_copy, &chain.source[0], sizeof chain.source[0]);
            host_watch = 0;
        }
        memcpy(&proj_slot[3], keep4, sizeof keep4);
        for (k = 0; k < NSTEP; k++) { step_clear(&t->step[k]); step_clear(&r->step[k]); }
        sh_eu[0][0] = 0; shell_lane_len_i[0][0] = 0;
        t->p[P_SLEN] = 16;
        sgo2(0, kn);
#undef QN
        printf("  (KEYS at the bar %u, LEN 12 at the clock's bar %u, lanes %u, one slot %u, empty %u)\n", ok1, ok2, ok3,
               ok4, ok5);
        {   /* (a first take open on the part: no queue) */
            sh_take.first = 1; sh_take.fpart = (uint8_t)sh.part; sh_q.picking = 0;
            sh_q_turn(1);
            ok5 = ok5 && !sh_q.picking && str_eq(sh.toast, "CLOSE THE TAKE FIRST: TAP REC");
            sh_take.first = 0;
        }
        bad += check("switching patterns live: SEQ + PRESETS queues a slot's part for the bar line (16 of the clock), "
                     "from step 1, SOUND kept; LEN 12 still at the clock's bar; lanes switch; one slot at a time; "
                     "an empty slot refused", ok1 && ok2 && ok3 && ok4 && ok5);
    }
    {   /* a queued switch: REC refuses to arm over it; a stop, a project loaded and NEW each drop it; landed, REC's first
         * ring puts the part back as it was when queued */
        uint32_t k, ok1, ok2, ok3, w;
        track_t *t;
        static project_store_t keep1;
        sgoc(slot_of("ANALOG"), note_of("KEYS"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        memcpy(&keep1, &proj_slot[1], sizeof keep1);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        t->step[0].note[0] = 72; t->step[0].n = 1; t->step[0].time = ST_NOTE;   /* slot B: a C5 on step 1 */
        ok1 = project_save(1) == 0;
        step_clear(&t->step[0]);
        t->step[4].note[0] = 48; t->step[4].n = 1; t->step[4].time = ST_NOTE;   /* playing: a C3 on step 5 */
        song.rec = 0;
#define QUEUE_B() do { sh_q.picking = 1; sh_q.pick = 1; sh_q_commit(); sframe(); } while (0)
        transport_req = 1; srender(2); sframe();
        QUEUE_B();
        ok1 = ok1 && sh_q_waits(sh.part);
        stap(B_REC); sframe();
        ok1 = ok1 && !((song.rec >> sh.part) & 1u) && str_eq(sh.toast, "A PATTERN WAITS: SEQ + PRESETS TO OFF");
        transport_req = 2; srender(4); sframe(); sframe();
        ok2 = !sh_q.mask;                               /* (stopped before the bar: gone) */
        transport_req = 1; srender(2); sframe();
        QUEUE_B();
        project_new(); sframes(100);
        ok2 = ok2 && !sh_q.mask;
        project_new_undo(); sframes(100);
        t = sh_trk();
        QUEUE_B();
        project_load(1); sframes(100);
        ok2 = ok2 && !sh_q.mask;
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[4].note[0] = 48; t->step[4].n = 1; t->step[4].time = ST_NOTE;
        transport_req = 1; srender(2); sframe();
        QUEUE_B();
        for (w = 0; w < 4000u && sh_q.mask; w++) { srender(4); sframe(); }
        ok3 = !sh_q.mask && t->step[0].n == 1u && !t->step[4].n;   /* (landed: slot B's) */
        sh_take_undo(); sframe();
        ok3 = ok3 && t->step[4].n == 1u && t->step[4].note[0] == 48u && !t->step[0].n;
#undef QUEUE_B
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memcpy(&proj_slot[1], &keep1, sizeof keep1);
        sgo2(0, note_of("KEYS"));
        printf("  (queued: REC refused %u; dropped by a stop, NEW and a load %u; REC's ring undoes it %u)\n", ok1, ok2,
               ok3);
        bad += check("a queued switch: REC does not arm over it; a stop, NEW and a project load drop it; landed, REC's "
                     "first ring puts the part back", ok1 && ok2 && ok3);
    }
    {   /* the page stays put while SEQ is held, the playhead moving on; a page picked by hand goes with a NOTE switch */
        track_t *t;
        uint32_t k, acc = 0, ok1, ok2;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 32; sh_pg_hand = 0;
        skey_sung(12);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(10u);
        sdown(B_SEQ); OD_RUN(48u);
        OD_TO(20u);                                     /* (the playhead on the second page, SEQ still held) */
        skey((uint32_t)sh_white_key(0));
        sup(B_SEQ); OD_RUN(32u);
        ok1 = t->step[0].n && !t->step[16].n;
        transport_req = 2; srender(4); sframe();
        t->p[P_SLEN] = 64; sframe();
        sdown(B_SEQ); sframes(400); skey(SH_PG_RIGHT); skey(SH_PG_RIGHT); skey(SH_PG_RIGHT); sup(B_SEQ); sframe();
        ok2 = sh_pg() == 48u;
        sgo2(sh.mode, SH_N_LINES);                      /* another NOTE engine, the same SOUND */
        ok2 = ok2 && sh_pg_hand == 0u;
        sgo2(sh.mode, 0);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sframe();
        printf("  (SEQ held: page kept %u; a NOTE switch drops the hand page %u)\n", ok1, ok2);
        bad += check("pages: held SEQ keeps its page as the playhead moves on; a NOTE switch lets the page follow again",
                     ok1 && ok2);
    }
    {   /* REC + PLAY while the beat runs: it keeps running, armed (stopped: the count-in, as before) */
        uint32_t ok;
        sgo(mode_of("FM6"));
        transport_req = 2; srender(4); sframe();
        song.rec = 0;
        transport_req = 1; srender(2); sframes(200);
        sdown(B_REC); sframes(SH_TAP_MS + 50u);
        stap(B_PLAY);
        sup(B_REC); sframes(100);
        srender(2); sframe();
        ok = song.playing && ((song.rec >> sh.part) & 1u) && str_eq(sh.toast, "RECORDING");
        song.rec = 0; transport_req = 2; srender(4); sframe();
        bad += check("REC + PLAY while the beat runs: it keeps running, armed", ok);
    }

    {   /* hold-to-see: SEQ or REC held past the tap puts the map of what it does for the pair in front in the knob
         * cards' band, only what applies now (PAGE past 16 steps, DOUBLE when OCT+ would double); a tap, a knob
         * turned or the button let go shows the cards; the keys and REC do what they did */
        track_t *t;
        uint32_t k, l, ok1, ok2, ok3, ok4, ok5, ok6, ok7, ok8 = 1, ok9, cut = 0, h0, h1, m;
        uint8_t rec0;
#define HS_HAS(key_, act_) hs_has(key_, act_)
#define HS_BAND() band_hash()
        sgo(mode_of("FM6"));                            /* KEYS on FM6 */
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        sh_pg_hand = 0;
        sh.page[sh.mode] = SHP_HOME;
        sframes(1400);                                  /* (no knob card lit) */
        h0 = HS_BAND();
        sdown(B_SEQ); sframes(100);
        ok1 = sh_hs_which() == NB && HS_BAND() == h0;   /* not yet a hold: the cards */
        sframes(300);
        h1 = HS_BAND();
        ok1 = ok1 && sh_hs_which() == B_SEQ && h1 != h0 && HS_HAS("WHITE", "STEP ON/OFF") &&
              HS_HAS("OCT+", "NOTHING TO DOUBLE") && !HS_HAS("C#5/D#5", 0) && !HS_HAS("OCT+", "DOUBLE");
        cut += hs_cut();
        skey((uint32_t)sh_white_key(2));                /* SEQ + a white key: its step, as before */
        ok1 = ok1 && t->step[2].n == 1u && sh_hs_which() == B_SEQ && HS_HAS("OCT+/-", "DOUBLE/HALF");
        t->p[P_SLEN] = 32; sframe();                    /* past 16: the pages */
        ok2 = HS_HAS("C#5/D#5", "PAGE 1/2");
        cut += hs_cut();
        skey(SH_PG_RIGHT);
        ok2 = ok2 && HS_HAS("C#5/D#5", "PAGE 2/2") && str_eq(sh.toast, "STEPS 17-32");
        t->p[P_SLEN] = 64; sframe();
        ok2 = ok2 && HS_HAS("OCT+/-", "FULL/HALF") && HS_HAS("C#5/D#5", "PAGE 2/4");
        cut += hs_cut();
        t->p[P_SLEN] = 16; sframe();
        ok2 = ok2 && !HS_HAS("C#5/D#5", 0);
        sturn(EN_K1, 1);                                /* a knob turned while held: its card shows */
        ok3 = sh_hs_which() == NB;
        sturn(EN_K1, -1);
        sframes(1400);
        ok3 = ok3 && sh_hs_which() == B_SEQ;
        sup(B_SEQ); sframe();
        ok3 = ok3 && sh_hs_which() == NB && HS_BAND() == h0 && t->step[2].n == 1u;
        sh_pg_hand = 0;
        sgo(TM_CHORD);                                  /* CHORD: steps as KEYS (one note, the key's) */
        sframes(1400);
        sdown(B_SEQ); sframes(400);
        chord_last[sh.part].n = 0; sframe();
        ok4 = sh_hs_which() == B_SEQ && HS_HAS("WHITE", "STEP ON/OFF");
        chord_last[sh.part].n = 3; sframe();             /* a chord played: the step takes it whole */
        ok4 = ok4 && HS_HAS("WHITE", "LAST CHORD ON/OFF");
        cut += hs_cut();
        chord_last[sh.part].n = 0;
        cut += hs_cut();
        sup(B_SEQ); sframe();
        sgo(TM_ALIVE);                                  /* ALIVE: no steps by key, no pages */
        for (k = 0; k < NSTEP; k++)
            step_clear(&sh_trk()->step[k]);
        sframes(1400);
        sdown(B_SEQ); sframes(400);
        ok4 = ok4 && !HS_HAS("WHITE", 0) && !HS_HAS("C#5/D#5", 0) && HS_HAS("OCT+", "NOTHING TO DOUBLE");
        cut += hs_cut();
        sup(B_SEQ); sframe();
        sgo(TM_RUD);                                    /* RUDIMENT on DRUMS: ACCENT, a lane past 16 pages */
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        l = (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u;
        sh_eu[0][l] = 0; sh_eu[1][l] = 0;
        sframes(1400);
        sdown(B_SEQ); sframes(400);
        ok5 = HS_HAS("WHITE", "ACCENT") && HS_HAS("OCT+/-", "DOUBLE/HALF") && !HS_HAS("C#5/D#5", 0);
        t->p[P_SLEN] = 32; sframe();
        ok5 = ok5 && HS_HAS("C#5/D#5", "PAGE 1/2");
        cut += hs_cut();
        t->p[P_SLEN] = 16;
        sup(B_SEQ); sframe();
        sgoc(slot_of("FM6"), note_of("LINES"));         /* LINES (the lanes on another SOUND): the marks */
        sframes(1400);
        sdown(B_SEQ); sframes(400);
        ok5 = ok5 && sh_kind() == SHK_RUDIMENT && HS_HAS("WHITE", "ACC SLD TIE") && HS_HAS("OCT+/-", "DOUBLE/HALF");
        cut += hs_cut();
        sup(B_SEQ); sframe();
        sgo(mode_of("FM6"));                            /* REC held on KEYS: ERASE, the rings, REC MODE, the count-in */
        sframes(1400);
        rec0 = song.rec;
        h0 = HS_BAND();
        sdown(B_REC); sframes(100);
        ok6 = sh_hs_which() == NB && ((song.rec >> sh.part) & 1u) != ((rec0 >> sh.part) & 1u);   /* the press arms */
        sframes(300);
        ok6 = ok6 && sh_hs_which() == B_REC && HS_BAND() != h0 && HS_HAS("RING 1", 0) && HS_HAS("RING 2", "CLEAR") &&
              HS_HAS("SELECT", N_SH_RECM[sh_rec_mode()]) && HS_HAS("PLAY", "COUNT IN") && HS_HAS("WHITE", "ERASE STEP") &&
              !HS_HAS("LANE", 0);
        cut += hs_cut();
        sup(B_REC); sframe();                           /* in the first ring: nothing undone, as before */
        ok6 = ok6 && sh_hs_which() == NB && str_eq(sh.toast, "REC ARMED, NOTHING UNDONE") && HS_BAND() == h0;
        song.rec = rec0; sframe();
        sgo(TM_RUD);                                    /* REC held on the lanes: ERASE, then the rings */
        t = sh_trk();
        sframes(1400);
        rec0 = song.rec;
        sdown(B_REC); sframes(400);
        ok7 = HS_HAS("WHITE", "ERASE STEP") && HS_HAS("LANE", "ERASE LANE") && HS_HAS("RING 1", 0) &&
              HS_HAS("SELECT", 0);
        cut += hs_cut();
        sup(B_REC); sframe();
        song.rec = rec0; sframe();
        sh.page[sh.mode] = SHP_HOME; sframe();
        for (m = 0; m < 3u; m++) {                      /* the REC map's ERASE is what REC + a white key does */
            int says, did;
            sgo(m == 0u ? mode_of("FM6") : m == 1u ? TM_CHORD : TM_RUD);
            t = sh_trk();
            transport_req = 2; srender(2); sframe();
            for (k = 0; k < NSTEP; k++)
                step_clear(&t->step[k]);
            if (m == 2u)
                grid_hit(t, 0, (uint32_t)sh_sp[SP_LANE - SP_FIRST] % 8u, 1u);
            else
                t->step[0].note[0] = 60, t->step[0].time = ST_NOTE, t->step[0].n = 1;
            sframes(1400);
            rec0 = song.rec;
            sdown(B_REC); sframes(400);
            says = HS_HAS("WHITE", "ERASE STEP");
            skey((uint32_t)sh_white_key(0));
            did = t->step[0].n == 0u;
            sup(B_REC); sframe();
            song.rec = rec0;
            ok8 = ok8 && says == did && says;           /* (KEYS, CHORD and the lanes: REC + a white key erases) */
            for (k = 0; k < NSTEP; k++)
                step_clear(&t->step[k]);
            sframe();
        }
        sgo(TM_RUD);                                    /* the lanes empty: how a beat starts, nothing held */
        t = sh_trk();
        transport_req = 2; srender(2); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sframes(SH_TOAST_MS + 200u);                    /* (the toasts gone: they reach into the band) */
        sh.page[sh.mode] = SHP_HOME; sframe();
        ok9 = !sh_lanes_hint();                         /* (the SOUND view: its ALGORITHM is the SOUND's, the cards) */
        snote();
        h0 = HS_BAND();
        ok9 = ok9 && sh_lanes_hint() && sh_hs_which() == NB;
        sdown(B_SEQ); sframes(400);
        ok9 = ok9 && sh_hs_which() == B_SEQ && HS_BAND() != h0;     /* SEQ held: the map over it */
        sup(B_SEQ); sframe();
        ok9 = ok9 && HS_BAND() == h0;
        skey((uint32_t)sh_white_key(0));                /* a hit: the knob cards */
        ok9 = ok9 && !sh_lanes_hint() && HS_BAND() != h0;
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        sframe();
        sgo(mode_of("FM6"));
        ok9 = ok9 && !sh_lanes_hint();                  /* KEYS: never */
        shome();                                        /* (the SOUND view again, as the tests below expect) */
        printf("  (KEYS %u, pages %u, knob and let go %u, CHORD ALIVE %u, lanes %u, REC %u, REC lanes %u, ERASE as REC "
               "does %u, empty lanes %u, cut %u)\n", ok1, ok2, ok3, ok4, ok5, ok6, ok7, ok8, ok9, cut);
        bad += check("hold-to-see: SEQ or REC held shows its map in the knob cards' band per NOTE engine (PAGE past 16, "
                     "DOUBLE when it would); a tap, a knob or letting go shows the cards; keys and REC as before; "
                     "empty lanes say ALGORITHM: A BEAT  WHITE: STEPS",
                     ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && cut == 0u);
#undef HS_HAS
#undef HS_BAND
    }

    {   /* the lanes, stopped: a lane key sounds its lane once, a hit turned on sounds once, both let go after a step's
         * GATE; a hit turned off, the transport playing or REC held: nothing more sounds */
        track_t *t;
        uint32_t k, l, q, on0, off0, on1, off1, sil, play = 0, rec = 0, ln = note_of("LINES"), lsnd;
        sgo(TM_RUD);
        t = &trk[SH_NOTES[note_of("RUDIMENT")].part];
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        for (l = 0; l < 8u; l++) { sh_eu[0][l] = 0; sh_eu[1][l] = 0; sh_eu[2][l] = 0; shell_lane_len[l] = 0; }
        memset(t->v, 0, sizeof t->v);
        sframe();
        skey(SH_BLACK_KEYS[1]);                         /* the snare's lane to the front, heard */
        on0 = sh_sp[SP_LANE - SP_FIRST] == 1 && sh_sounds(t, shell_lane_note_i[0][1]);
        srender(400);
        off0 = !sh_sounds(t, shell_lane_note_i[0][1]);
        skey((uint32_t)sh_white_key(4));                /* a hit on: heard */
        on1 = (step_lanes(&t->step[4]) & 2u) && sh_sounds(t, shell_lane_note_i[0][1]);
        srender(400);
        off1 = !sh_sounds(t, shell_lane_note_i[0][1]);
        skey((uint32_t)sh_white_key(4));                /* the hit off: silent */
        sil = !(step_lanes(&t->step[4]) & 2u) && !sh_sounds(t, shell_lane_note_i[0][1]);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        transport_req = 1; srender(2); sframe();        /* playing: the keys edit, the pattern is the audition */
        memset(t->v, 0, sizeof t->v);
        skey(SH_BLACK_KEYS[2]);
        skey((uint32_t)sh_white_key(9));
        for (q = 0; q < NVOICE; q++)
            play |= t->v[q].active;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        memset(t->v, 0, sizeof t->v);
        sdown(B_REC); sframe();                         /* REC held (ERASE): silent */
        skey(SH_BLACK_KEYS[3]);
        skey((uint32_t)sh_white_key(2));
        for (q = 0; q < NVOICE; q++)
            rec |= t->v[q].active;
        sup(B_REC); sframe();
        song.rec = 0;
        sframes(SH_TOAST_MS + 200u);
        sgo2(slot_of("FM6"), ln);                       /* LINES on FM6: its note, on its own part */
        srender(40); sframe();                          /* (the switch to FM6 faded through) */
        memset(trk[SH_NOTES[ln].part].v, 0, sizeof trk[SH_NOTES[ln].part].v);
        skey(SH_BLACK_KEYS[0]);
        lsnd = sh_sounds(&trk[SH_NOTES[ln].part], shell_lane_note_i[1][0]);
        srender(400);
        lsnd = lsnd && !sh_sounds(&trk[SH_NOTES[ln].part], shell_lane_note_i[1][0]);
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sgo(mode_of("FM6"));
        printf("  (lane key %u/%u, hit on %u/%u, hit off silent %u, playing %u, REC held %u, LINES %u)\n", on0, off0, on1,
               off1, sil, play, rec, lsnd);
        bad += check("lanes stopped: a lane key and a hit turned on sound once and let go; off, playing, REC held: silent",
                     on0 && off0 && on1 && off1 && sil && !play && !rec && lsnd);
    }

    {   /* a recorded note's velocity: 111..126 plays as played (only 127 is an ACCENT); an overdub on a step entered by
         * hand keeps the louder of the two (the hand step plays 96) */
        track_t *t;
        uint32_t k, acc = 0, i, ok;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); OD_RUN(100u);
        OD_TO(4u);
        i = t->seq_idx;
        t->step[(i + 2u) % 16u].note[0] = 55; t->step[(i + 2u) % 16u].time = ST_NOTE; t->step[(i + 2u) % 16u].n = 1;
        fm1_irq_off(); rec_note(t, 60, 120); fm1_irq_on();
        ok = t->step[i].vel == 120u && !(t->step[i].flags & SF_ACCENT);
        OD_TO((i + 2u) % 16u);
        fm1_irq_off(); rec_note(t, 62, 60); fm1_irq_on();
        ok = ok && t->step[(i + 2u) % 16u].n == 2u && t->step[(i + 2u) % 16u].vel == 96u;
        song.rec = 0; transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sframe();
        bad += check("recorded velocity: 120 plays 120, not an ACCENT; a quiet overdub keeps a hand step's 96", ok);
    }
    {   /* VCE LEG: a note played over the one held records legato (the step before it slides into it); MONO does not */
        track_t *t;
        uint32_t k, acc = 0, i, mode, slid[2] = {0, 0};
        sgo(mode_of("FM6"));
        t = sh_trk();
        for (i = 0; i < 2u; i++) {
            mode = i ? V_MONO : V_LEGATO;
            transport_req = 2; srender(4); sframe();
            for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
            t->p[P_SLEN] = 16; t->p[P_VOICE] = (int16_t)mode;
            song.rec = (uint8_t)(1u << sh.part);
            transport_req = 1; srender(2); OD_RUN(100u);
            OD_TO(2u);
            OD_KON(12u); OD_RUN(500u); OD_KON(19u); OD_RUN(100u); OD_KOFF(12u); OD_RUN(300u); OD_KOFF(19u);
            song.rec = 0; OD_RUN(32u);
            transport_req = 2; srender(4); sframe();
            for (k = 0; k < 16u; k++)
                if (t->step[k].time == ST_NOTE && t->step[k].n && k > 2u)
                    slid[i] = (t->step[(k + 15u) % 16u].flags & SF_SLIDE) != 0u;
        }
        t->p[P_VOICE] = V_POLY;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sframe();
        bad += check("VCE LEG: a note over the held one records sliding into it; MONO does not", slid[0] && !slid[1]);
    }
    {   /* KEYS: REC held + a white key erases that step (silent, not recorded), and UNDO TAKE brings it back; CHORD's
         * B3 with SEQ held enters step 4 and leaves LATCH alone; SEQ + a white key under ALIVE says why it does nothing */
        track_t *t;
        uint32_t k, ok1, ok2, ok3, na, q;
        uint8_t latch;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[2].note[0] = 60; t->step[2].time = ST_NOTE; t->step[2].n = 1;
        song.rec = 0;
        sdown(B_REC); sframes(300);
        fm1_in.notes |= 1u << sh_white_key(2); host_notes |= 1u << sh_white_key(2); keyboard_block(); srender(4); sframe();
        for (na = 0, q = 0; q < NVOICE; q++) na += t->v[q].active && t->v[q].gate;
        fm1_in.notes &= ~(1u << sh_white_key(2)); keyboard_block(); sframe();
        sup(B_REC); sframe();
        ok1 = !t->step[2].n && na == 0u && !song.rec && str_eq(sh.toast, "ERASED A STEP");
        sh_take_undo(); sframe();
        ok1 = ok1 && t->step[2].n == 1u;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sgo(TM_CHORD);
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        latch = shell_chord_latch;
        sdown(B_SEQ); sframes(400);
        skey(CB_LATCH);
        sup(B_SEQ); sframe(); sframe();
        ok2 = shell_chord_latch == latch && t->step[sh_key_white(CB_LATCH)].n;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        sgo(TM_ALIVE);
        sdown(B_SEQ); sframes(400);
        skey((uint32_t)sh_white_key(0));
        sup(B_SEQ); sframe();
        ok3 = str_eq(sh.toast, "NO STEPS HERE: RECORD A TAKE");
        sgo(mode_of("FM6"));
        printf("  (KEYS REC + white erases %u; SEQ + B3 not LATCH %u; ALIVE SEQ + white says why %u)\n", ok1, ok2, ok3);
        bad += check("REC + a white key erases under KEYS; SEQ + B3 leaves LATCH; SEQ + white under ALIVE says why",
                     ok1 && ok2 && ok3);
    }
    {   /* a first take with LEN turned while it records counts the steps that played, not wraps at 64 */
        track_t *t;
        uint32_t k, acc = 0, ok;
        sgo(mode_of("FM6"));
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16; song.rec = 0; sframe();
        transport_req = 1; srender(2); OD_RUN(100u);
        stap(B_REC); sframe();
        OD_TO(3u); OD_KON(12u); OD_RUN(48u); OD_KOFF(12u);
        t->p[P_SLEN] = 16;                              /* (LEN turned during the take) */
        for (k = 0; k < 2u; k++) { OD_TO(15u); OD_TO(0u); }
        OD_TO(4u);
        stap(B_REC); sframe(); sframe();
        ok = t->p[P_SLEN] == 32;
        printf("  (LEN after a 2.25-loop first take with LEN turned to 16: %d)\n", t->p[P_SLEN]);
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16; sframe();
        bad += check("a first take with LEN turned while it records sets LEN from the steps that played", ok);
    }
    if (slot_of("MOUTH") < SH_NMODES) {   /* a take sings its phrase from the top each pass: MOUTH's cursor (the next
                                         * piece of the phrase, which no step records) is where it was at step 1 of the
                                         * recording, so passes 1 and 2 sing the pieces heard live (one sung before
                                         * leaves the cursor mid-phrase; the take still starts at the phrase's top) */
        track_t *t;
        uint32_t k, q, acc = 0, c1, ps, ok, n[3] = {0, 0, 0}, got[3][8] = {{0}}, lastage[NVOICE];
#define PH_RUN(ms) do { uint32_t f_; for (f_ = 0; f_ < (ms) / 16u; f_++) { acc += 16u * FS / 1000u;                  \
                        while (acc >= HALF_FRAMES) { srender(1); acc -= HALF_FRAMES;                                   \
                            for (q = 0; q < NVOICE; q++)                                                               \
                                if (t->v[q].active && t->v[q].age != lastage[q]) {                                     \
                                    lastage[q] = t->v[q].age;                                                          \
                                    if (n[ps] < 8u) got[ps][n[ps]++] = mouth_of(t)->cursor;                            \
                                } } sframe(); } } while (0)
#define PH_TO(i) do { uint32_t w_; for (w_ = 0; w_ < 20000u && t->seq_idx != (i); w_++) PH_RUN(16u); } while (0)
        sgoc(slot_of("MOUTH"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = 0; t->p[P_AHOLD] = 0;
        t->p[P_E4] = 0; t->p[P_E5] = 0;                 /* PHRASE HELLO, PLAY SYLL */
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        motion_clear(t);
        t->p[P_SLEN] = 16;
        mouth_of(t)->cursor = 0; mouth_of(t)->phrase = 0;
        OD_KON(CB_ROOT0); OD_RUN(120u); OD_KOFF(CB_ROOT0); OD_RUN(200u);   /* stopped: the first piece */
        c1 = mouth_of(t)->cursor;
        for (q = 0; q < NVOICE; q++)
            lastage[q] = t->v[q].age;
        ps = 0;
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); PH_RUN(100u);
        for (k = 0; k < 3u; k++) {                      /* the take: three notes, steps 3 7 11 */
            PH_TO(2u + 4u * k);
            OD_KON(CB_ROOT0 + 2u * k); PH_RUN(120u); OD_KOFF(CB_ROOT0 + 2u * k);
        }
        PH_TO(15u);
        song.rec = 0;
        for (ps = 1; ps < 3u; ps++) {
            PH_TO(0u);
            PH_TO(15u);
        }
        transport_req = 2; srender(4); sframe();
        ok = n[0] == 3u && n[1] == 3u && n[2] == 3u && got[0][0] == c1;
        for (k = 0; ok && k < 3u; k++)
            ok = got[1][k] == got[0][k] && got[2][k] == got[0][k];
        printf("  (cursor after each note: live %u %u %u, pass 1 %u %u %u, pass 2 %u %u %u; the first piece %u)\n",
               got[0][0], got[0][1], got[0][2], got[1][0], got[1][1], got[1][2], got[2][0], got[2][1], got[2][2], c1);
        for (k = 0; k < NSTEP; k++)
            step_clear(&t->step[k]);
        bad += check("MOUTH: a take sings its phrase from the top each pass; passes 1 and 2 sing what was sung live", ok);
#undef PH_TO
#undef PH_RUN
    }

    {   /* the arp under CHORD, one clock: RPT and an arp mode exclusive on CHORD's part (each turned on turns the
         * other off, said); a project holding both plays the arp (the ISR leaves an arp part to its arp) */
        track_t *t;
        uint8_t got[64];
        uint32_t n = 0, k, ok1, ok2, ok3, three = 0;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_AHOLD] = 0; t->p[P_ARATE] = 2;
        sh_sp[SP_RPT - SP_FIRST] = 0; sframe();
        sh.page[sh.mode] = SHP_CPLAY; sframe();
        sturn(EN_K4, 2);                                /* RPT on: the arp goes */
        ok1 = sh_sp[SP_RPT - SP_FIRST] != 0 && t->p[P_AMODE] == 0 && str_eq(sh.toast, "ARP OFF: RPT STRUMS");
        sh.page[sh.mode] = SHP_ARP; sframe();
        sturn(EN_K1, 1);                                /* an arp mode on: RPT goes */
        ok2 = t->p[P_AMODE] != 0 && sh_sp[SP_RPT - SP_FIRST] == 0 && str_eq(sh.toast, "RPT OFF: THE ARP PLAYS");
        t->p[P_AMODE] = AM_UP;
        shell_chord_rpt = 2;                            /* (as a project left them: the ISR alone) */
        transport_req = 1; srender(2);
        arp_key(CB_ROOT0, 1);
        for (k = 0; k < 240u; k++) {                    /* ~1 s of audio, RPT 1/8 kept on in the ISR */
            srender(1);
            shell_chord_rpt = 2;
            if (t->arp_note && (!n || got[n - 1u] != t->arp_note) && n < 64u)
                got[n++] = t->arp_note;
        }
        arp_key(CB_ROOT0, 0); srender(4);
        transport_req = 2; srender(4);
        for (k = 2; k < n; k++)
            three += got[k] != got[k - 2u];             /* (a 2-note loop repeats every other note) */
        ok3 = n >= 6u && three > 0u;
        t->p[P_AMODE] = 0; sh_sp[SP_RPT - SP_FIRST] = 0; shell_chord_rpt = 0; sframe();
        printf("  (RPT on: arp off %u; arp on: RPT off %u; RPT left on in the ISR: %u notes, walks every tone %u)\n",
               ok1, ok2, n, ok3);
        bad += check("ARP x RPT: turning one on turns the other off; RPT never restarts an arp (every chord tone plays)",
                     ok1 && ok2 && ok3);
    }
    {   /* the first arp note while the transport plays: in the second half of a RATE cell it waits for the next
         * grid point, so a take of an arp at the pattern's DIV holds one note a step */
        track_t *t;
        uint32_t k, ok = 1, any = 0;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_AHOLD] = 0; t->p[P_ARATE] = 2; t->p[P_SDIV] = 2;
        t->p[P_SLEN] = 16;
        sh_sp[SP_RPT - SP_FIRST] = 0; sh_sp[SP_STRM - SP_FIRST] = 0; sframe();
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2);
        for (k = 0; k < 4000u && t->seq_idx != 2u; k++)
            srender(1);
        arp_run(80u);                                   /* ~60 % into step 3 */
        arp_key(CB_ROOT0, 1);
        arp_run(700u);
        arp_key(CB_ROOT0, 0);
        arp_run(48u);
        song.rec = 0; sframe();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) {
            ok &= t->step[k].n <= 1u;
            any |= t->step[k].n;
            if (t->step[k].n > 1u)
                printf("  (step %u: %u notes)\n", k + 1u, t->step[k].n);
        }
        ok = ok && any;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_AMODE] = 0;
        bad += check("ARP: pressed late in a step while recording, the arp's first note waits for the grid (a note a step)",
                     ok);
    }
    {   /* MODE OFF turns every arp-like thing off: HOLD, LATCH and the notes they latched */
        track_t *t;
        uint32_t ok1, ok2;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_AHOLD] = 1; t->p[P_ARATE] = 2;
        sh_sp[SP_RPT - SP_FIRST] = 0; sframe();
        skey(CB_LATCH);                                 /* LATCH on */
        arp_key(CB_ROOT0, 1); arp_run(200u);
        arp_key(CB_ROOT0, 0); arp_run(300u);
        sh.page[sh.mode] = SHP_ARP; sframe();
        sturn(EN_K1, -20);                              /* MODE to OFF */
        arp_run(300u);
        ok1 = t->p[P_AMODE] == 0 && t->p[P_AHOLD] == 0 && !shell_chord_latch && !arp_gated(t) &&
              str_eq(sh.toast, "ARP OFF");
        arp_key(CB_ROOT0 + 2u, 1); arp_run(200u);
        arp_key(CB_ROOT0 + 2u, 0); arp_run(1000u);
        ok2 = !arp_gated(t);
        printf("  (MODE OFF: HOLD, LATCH and the notes off %u; the next chord let go is silent %u)\n", ok1, ok2);
        bad += check("ARP: MODE OFF also ends HOLD and LATCH and the notes they held; nothing latches after", ok1 && ok2);
        shell_chord_latch = 0; t->p[P_AHOLD] = 0; sframe();
    }
    {   /* CHORD plays POLY whatever VCE says (voice.c trk_vmode): the arp walks the chord on a MONO SOUND, VCE is
         * kept as it was for KEYS, and turning it under CHORD says so */
        track_t *t;
        uint8_t seen[128] = {0};
        uint32_t k, n = 0, ok1, ok2, ok3;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        t->p[P_VOICE] = V_MONO; t->p[P_AMODE] = AM_UP; t->p[P_AHOLD] = 0; t->p[P_ARATE] = 2;
        sh_sp[SP_RPT - SP_FIRST] = 0; sframe();
        arp_key(CB_ROOT0, 1);
        for (k = 0; k < 160u; k++) {
            srender(1);
            if (t->arp_note && !seen[t->arp_note & 127u]) {
                seen[t->arp_note & 127u] = 1;
                n++;
            }
        }
        arp_key(CB_ROOT0, 0); srender(8);
        ok1 = n >= 3u && t->p[P_VOICE] == V_MONO && trk_vmode(t) == V_POLY;
        sh.page[sh.mode] = SHP_VOICE; sframe();
        sturn(EN_K1, 1);
        ok2 = str_eq(sh.toast, "VCE IS FOR KEYS: CHORD PLAYS POLY");
        t->p[P_VOICE] = V_MONO;
        t->p[P_AMODE] = 0;
        sgo(mode_of("FM6"));
        t = sh_trk();
        k = (uint32_t)t->p[P_VOICE];
        t->p[P_VOICE] = V_MONO;
        ok3 = trk_vmode(t) == V_MONO;                   /* (off CHORD's parts: VCE as set) */
        t->p[P_VOICE] = (int16_t)k;
        printf("  (CHORD on a MONO SOUND: %u tones, VCE kept %u; the VOICE page says why %u; KEYS keeps MONO %u)\n",
               n, ok1, ok2, ok3);
        bad += check("CHORD plays POLY on a MONO SOUND: the arp walks the chord, VCE untouched and greyed under CHORD",
                     ok1 && ok2 && ok3);
    }
    {   /* a latched arp over its own take, the part not armed: quiet, as the generators are (sh_gen_quiet); a key
         * pressed plays it again */
        track_t *t;
        uint32_t k, fires = 0, sounded = 0, idx, ok;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_VOICE] = V_POLY; t->p[P_AMODE] = AM_UP; t->p[P_AHOLD] = 1; t->p[P_ARATE] = 2; t->p[P_SDIV] = 2;
        t->p[P_SLEN] = 16;
        sh_sp[SP_RPT - SP_FIRST] = 0; sframe();
        song.rec = (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2);
        arp_key(CB_ROOT0, 1); arp_run(200u);
        arp_key(CB_ROOT0, 0); arp_run(2200u);
        song.rec = 0; arp_run(300u);
        for (k = 0; k < 400u && t->seq_idx != 0u; k++) /* (from the take's top: its notes, not the rests after) */
            arp_run(16u);
        idx = t->arp_idx;
        for (k = 0; k < 60u; k++) {
            arp_run(16u);
            fires += t->arp_note != 0u;
            sounded += arp_gated(t) != 0u;
        }
        ok = fires == 0u && sounded > 0u && t->nheld;
        arp_key(CB_ROOT0 + 2u, 1);
        for (k = 0, idx = 0; k < 20u; k++) {            /* a key down: the arp plays over the take */
            arp_run(16u);
            idx += t->arp_note != 0u;
        }
        ok = ok && idx > 0u;
        arp_key(CB_ROOT0 + 2u, 0);
        transport_req = 2; srender(4); sframe();
        printf("  (latched arp over its take: %u blocks it played, the take sounded in %u; a key plays it %u)\n",
               fires, sounded, ok);
        t->p[P_AHOLD] = 0; t->p[P_AMODE] = 0; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        bad += check("ARP: a latched arp is quiet over its own take until the part is armed or a key plays it", ok);
    }
    {   /* MODE under CHORD skips REPEAT and ORD (REPEAT plays the top note alone, ORD plays as UP there) */
        track_t *t;
        uint32_t ok;
        sgo(TM_CHORD);
        t = sh_trk();
        sh.page[sh.mode] = SHP_ARP; sframe();
        t->p[P_AMODE] = AM_RND; sframe();
        sturn(EN_K1, 1);
        ok = t->p[P_AMODE] == AM_DNUP;
        sturn(EN_K1, -1);
        ok = ok && t->p[P_AMODE] == AM_RND;
        t->p[P_AMODE] = 0; sframe();
        sgo(mode_of("FM6"));
        t = sh_trk();
        sh.page[sh.mode] = SHP_ARP; sframe();
        t->p[P_AMODE] = AM_RND; sframe();
        sturn(EN_K1, 1);
        ok = ok && t->p[P_AMODE] == AM_ORD;             /* (KEYS: every mode) */
        t->p[P_AMODE] = 0; sframe();
        bad += check("ARP MODE under CHORD skips REPEAT and ORD; KEYS keeps them", ok);
    }
    {   /* the resets: a family's button held, then OCT- held through a 1.2 s ring: that family's settings to their
         * defaults, never the steps, the knob moves or the other half; let go early, nothing; REC's first ring undoes
         * it. ARP's lone hold toggles HOLD as it is let go, so ARP + OCT- never toggles it */
        track_t *t;
        uint32_t k, ok1, ok2, ok3, ok4, ok5, ok6, ok7;
        int16_t snd[P_COUNT];
        int8_t oct;
        sgo(TM_CHORD);
        t = sh_trk();
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[3].note[0] = 62; t->step[3].time = ST_NOTE; t->step[3].n = 1;
        t->p[P_AMODE] = AM_UPDN; t->p[P_ARATE] = 4; t->p[P_AOCT] = 2; t->p[P_AGATE] = 30; t->p[P_ASWING] = 20;
        t->p[P_APROB] = 90; t->p[P_AHOLD] = 1; t->p[P_AORDER] = 1; t->p[P_VOIC] = VC_DROP2; t->p[P_QUANT] = 0;
        sh_sp[SP_BASS - SP_FIRST] = 1; sh_sp[SP_STRM - SP_FIRST] = 50; sh_sp[SP_TOP - SP_FIRST] = 40;
        sh_sp[SP_TUNE - SP_FIRST] = 1; sh_sp[SP_RPT - SP_FIRST] = 0;
        shell_chord_add = 1u << 9; shell_chord_qual = 3; shell_chord_latch = 1;
        sframe();
        memcpy(snd, t->p, sizeof snd);
        oct = song.octave;
        sh.page[sh.mode] = SHP_HOME; sframe();
        sdown(B_ARP); sframes(SH_TAP_MS + 100u);        /* (past ARP's hold: HOLD is not toggled by holding) */
        sdown(B_OCTDN); sframes(600u);
        sup(B_OCTDN); sframe(); sup(B_ARP); sframe();   /* let go early: nothing */
        ok1 = t->p[P_AMODE] == AM_UPDN && t->p[P_AHOLD] == 1 && song.octave == oct &&
              str_eq(sh.toast, "LET GO EARLY: NOTHING RESET");
        sdown(B_ARP); sframes(SH_TAP_MS + 100u);
        sdown(B_OCTDN); sframes(SH_LONG_MS + 200u);
        sup(B_OCTDN); sframe(); sup(B_ARP); sframe();
        ok2 = t->p[P_AMODE] == 0 && t->p[P_ARATE] == track_desc(t, P_ARATE)->def && t->p[P_AOCT] == 1 &&
              t->p[P_AGATE] == track_desc(t, P_AGATE)->def && t->p[P_ASWING] == 0 && t->p[P_APROB] == 127 &&
              t->p[P_AHOLD] == 0 && t->p[P_AORDER] == 0 && t->p[P_VOIC] == VC_CLOSE && t->p[P_QUANT] == 1 &&
              sh_sp[SP_BASS - SP_FIRST] == 0 && sh_sp[SP_STRM - SP_FIRST] == 0 && sh_sp[SP_TOP - SP_FIRST] == 0 &&
              sh_sp[SP_TUNE - SP_FIRST] == 0 && !shell_chord_add && !shell_chord_qual && !shell_chord_latch;
        for (k = 0; k < P_COUNT; k++)                   /* (the SOUND and the key untouched) */
            if (sh_half(k) != SH_NOTE && t->p[k] != snd[k])
                ok2 = 0;
        ok2 = ok2 && t->step[3].n == 1u && t->step[3].note[0] == 62 && song.octave == oct;
        ok3 = str_eq(sh_ring_word(), "UNDO RESET");
        sh_take_undo(); sframe();                       /* REC's first ring: back */
        ok3 = ok3 && t->p[P_AMODE] == AM_UPDN && t->p[P_ARATE] == 4 && t->p[P_AHOLD] == 1 &&
              t->p[P_VOIC] == VC_DROP2 && t->p[P_QUANT] == 0 && sh_sp[SP_STRM - SP_FIRST] == 50 &&
              shell_chord_add == 1u << 9 && shell_chord_qual == 3 && shell_chord_latch == 1 &&
              str_eq(sh.toast, "RESET UNDONE");
        shell_chord_add = 0; shell_chord_qual = 0; shell_chord_latch = 0;
        t->p[P_AHOLD] = 0; t->p[P_AMODE] = 0; sframe();
        sdown(B_ARP); sframes(SH_TAP_MS + 100u);        /* ARP alone, short: no HOLD (a deliberate long hold only) */
        ok4 = t->p[P_AHOLD] == 0 && str_eq(sh.toast, "KEEP HOLDING: HOLD ON");
        sup(B_ARP); sframe();
        ok4 = ok4 && t->p[P_AHOLD] == 0 && str_eq(sh.toast, "HOLD ARP LONGER FOR HOLD");
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();   /* long: HOLD as it is let go */
        ok4 = ok4 && t->p[P_AHOLD] == 1;
        sdown(B_ARP); sframes(SH_LONG_MS + 100u); sup(B_ARP); sframe();
        ok4 = ok4 && t->p[P_AHOLD] == 0;
        /* SEQ: SWG GATE QUANTIZE; LEN and DIV stay while the pattern plays */
        t->p[P_SSWING] = 30; t->p[P_SGATE] = 20; t->p[P_SLEN] = 32; t->p[P_SDIV] = 3; t->p[P_SQNT] = 0; sframe();
        sdown(B_SEQ); sframes(SH_TAP_MS + 100u);
        sdown(B_OCTDN); sframes(SH_LONG_MS + 200u);
        sup(B_OCTDN); sframe(); sup(B_SEQ); sframe();
        ok5 = t->p[P_SSWING] == 0 && t->p[P_SGATE] == track_desc(t, P_SGATE)->def &&
              t->p[P_SQNT] == track_desc(t, P_SQNT)->def && t->p[P_SLEN] == 32 && t->p[P_SDIV] == 3 &&
              t->step[3].n == 1u;
        t->p[P_SLEN] = 16; t->p[P_SDIV] = 2;
        /* SCL: the scale on every part */
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_ROOT] = 5; trk[k].p[P_SCALE] = 2; trk[k].p[P_TRANS] = 3;
        }
        sframe();
        sdown(B_SCL); sframes(SH_TAP_MS + 100u);
        sdown(B_OCTDN); sframes(SH_LONG_MS + 200u);
        sup(B_OCTDN); sframe(); sup(B_SCL); sframe();
        ok6 = 1;
        for (k = 0; k < NTRK; k++)
            ok6 = ok6 && trk[k].p[P_ROOT] == 0 && trk[k].p[P_SCALE] == 0 && trk[k].p[P_TRANS] == 0;
        sh_take_undo(); sframe();
        for (k = 0; k < NTRK; k++)
            ok6 = ok6 && trk[k].p[P_ROOT] == 5 && trk[k].p[P_SCALE] == 2 && trk[k].p[P_TRANS] == 3;
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_ROOT] = 0; trk[k].p[P_SCALE] = 0; trk[k].p[P_TRANS] = 0;
        }
        sframe();
        /* EDIT: the SOUND back to its preset, UNDO SOUND brings the edit back */
        {
            int16_t was = t->p[P_DIST];
            t->p[P_DIST] = (int16_t)(was < 100 ? was + 20 : was - 20); sframe();
            sdown(B_EDIT); sframes(SH_TAP_MS + 100u);
            sdown(B_OCTDN); sframes(SH_LONG_MS + 200u);
            sup(B_OCTDN); sframe(); sup(B_EDIT); sframe();
            ok7 = t->p[P_DIST] == was && t->step[3].n == 1u && str_eq(sh_ring_word(), "UNDO SOUND");
            sh_take_undo(); sframe();
            ok7 = ok7 && t->p[P_DIST] == (int16_t)(was < 100 ? was + 20 : was - 20);
            t->p[P_DIST] = was; sframe();
        }
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        printf("  (resets: early %u; ARP + OCT- %u; undo %u; ARP alone on release %u; SEQ %u; SCL %u; EDIT %u)\n",
               ok1, ok2, ok3, ok4, ok5, ok6, ok7);
        bad += check("resets: a family + OCT- held through the ring resets ARP, SEQ, SCL, EDIT, REC undoes; ARP's HOLD on "
                     "release", ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7);
    }

#if FELUCCA_LOOP
    {   /* leaving LOOP gives its tape to the echo, which clears it (one 128 KiB line, no room for a second): a tape
         * recorded on says so as LOOP goes; one never recorded on leaves quietly */
        static int32_t ob[2 * CTL];
        uint32_t lp = slot_of("LOOP"), fm = slot_of("FM6"), k, nz, ok, c5 = sh_wslot[7];
        track_t *src = &trk[2], *t;
#define MIX(nb) do { uint32_t b_; for (b_ = 0; b_ < (nb); b_++) mix_block(ob, CTL); } while (0)
        sframes(SH_TOAST_MS + 200u);
        sh_wslot[7] = (uint8_t)lp;
        sgo2(7, 0);
        t = sh_trk();
        MIX(4);
        t->p[P_E1] = 1; sframe();                       /* REC on: another part's delay send onto the tape */
        host_preset(src, 0, 0);
        src->p[P_MUTE] = 0; src->p[P_DLY] = 127;
        input_on(src, 60, 120);
        MIX(60);
        input_off(src, 60);
        t->p[P_E1] = 0; sframe();
        MIX(10);
        src->p[P_DLY] = 0;
        for (k = 0, nz = 0; k < DLY_LEN; k++)
            nz += dly_buf[k] != 0;
        ok = loop_hold && fx_tape_loop && nz > 1000u && !str_eq(sh.toast, "LOOP: TAPE CLEARED");
        sgo2(fm, 0);
        MIX(4);
        ok = ok && !loop_hold && !fx_tape_loop && str_eq(sh.toast, "LOOP: TAPE CLEARED");
        sgo2(7, 0);                                     /* back: a clean tape, as the toast said */
        MIX(4);
        for (k = 0, nz = 0; k < DLY_LEN; k++)
            nz += dly_buf[k] != 0;
        ok = ok && loop_hold && fx_tape_loop && nz < 64u;
        sframes(SH_TOAST_MS + 200u);
        sgo2(fm, 0);                                    /* never recorded on this time: no toast */
        MIX(4);
        ok = ok && !str_eq(sh.toast, "LOOP: TAPE CLEARED");
#undef MIX
        sh_wslot[7] = c5;
        bad += check("LOOP: leaving it with a recorded tape toasts LOOP: TAPE CLEARED (the echo takes the tape); an unused one leaves quietly",
                     ok);
    }
#endif
    {   /* every SOUND's HOME picture is its own (not the flower, not empty), inside the band, over its presets and
         * each of its eight at the ends and middle; LIBRARY the same on each engine it can play */
        uint32_t m, e, out, ink, flower = pic_draw(1, &out, &ink), worst = 0;
        int bare;
        char who[96];
        who[0] = 0;
        ok = 1;
        shell_reset();                                  /* (last: it leaves every SOUND on other sounds) */
        for (m = 0; m < SH_NMODES; m++) {
            if (!SH_MODES[m].lib && !eng_ok(SH_MODES[m].engine))
                continue;                               /* (TALK, left out of this build) */
            sgoc(m, note_of("KEYS"));
            sh.global = SHG_NONE;
            sh.page[sh.mode] = SHP_HOME;
            sframe();
            for (e = 0; e < NENGINES; e++) {
                uint32_t w;
                if (SH_MODES[m].lib ? !eng_ok(e) : e != SH_MODES[m].engine)
                    continue;
                if (SH_MODES[m].lib)
                    set_engine_of(sh_trk(), e);
                bare = 0;
                w = pic_sweep(flower, &bare);
                worst = w > worst ? w : worst;
                if (w)
                    printf("  %s (engine %u): %u pixels outside the band\n", ENGINES[e]->name, e, w);
                if (bare) {
                    ok = 0;
                    if (str_len(who) < 80u) {
                        str_cpy(who + str_len(who), " ", sizeof who - str_len(who));
                        str_cpy(who + str_len(who), ENGINES[e]->name, sizeof who - str_len(who));
                        fmt_int(who + str_len(who), (int32_t)e);
                    }
                }
            }
            sframes(100);
        }
        if (!ok)
            printf("  the flower or nothing on:%s\n", who);
        bad += check("pictures: each engine's HOME its own, inside its band, over its presets and its eight's ranges",
                     ok && !worst);
    }

    {   /* SQRL-1's sounds are the wheel's eight (core.h FELUCCA_x): every other engine is never offered (the wheel,
         * PRESETS' engine list, LIBRARY), and a sound on one that arrives (an old project, a preset, the editor) plays on
         * its sibling: the voices on MOUTH, the sampled ones on PHYS, the rest on ANALOG. The pair goes to the
         * sibling's SOUND, so the SOUND it named (FM6 here) keeps its own sound */
        static const uint8_t CUT[][2] = {{3, 0}, {ENGI_SAMPLE, ENGI_PHYS}, {5, ENGI_MOUTH}, {6, 0}, {ENGI_WHEEL, 0},
                                         {8, ENGI_PHYS}, {11, 0}, {ENGI_SLICE, ENGI_PHYS}, {ENGI_TALK, ENGI_MOUTH},
                                         {ENGI_SYLLABLE, ENGI_MOUTH}, {ENGI_VODER, ENGI_MOUTH}, {ENGI_ORGAN, 0},
                                         {ENGI_LOOP, ENGI_PHYS}};
        uint32_t ok = NENG_SHOWN == 8u && !sh_wheel_avail(slot_of("LIBRARY")), k, i, shown = 0, lands = 0, fmv;
        memset(sh_partpair, 0xFF, sizeof sh_partpair);
        for (k = 0; k < NELEM(CUT); k++) {
            for (i = 0; i < NENG_SHOWN; i++)
                shown += eng_vis(i) == CUT[k][0];
            ok = ok && !eng_ok(CUT[k][0]);
            sgo(mode_of("FM6"));
            sh_partpair[sh.part ^ 1u] = SH_NOPAIR;      /* (the sibling's pair on no other part: it moves there) */
            fmv = sh_snap_of(mode_of("FM6"))->p[P_E0 + 2];
            trk[sh.part].eng_req = CUT[k][0];
            sframe(); sframe();
            lands += trk[sh.part].eng_req == CUT[k][1] && SH_MODES[sh.mode].engine == CUT[k][1] &&
                     sh_partpair[sh.part] == (uint16_t)(sh.mode | sh.note << 8) &&
                     sh_snap_of(mode_of("FM6"))->eng == ENGI_FM6 && sh_snap_of(mode_of("FM6"))->p[P_E0 + 2] == fmv;
        }
        ok = ok && !shown && lands == NELEM(CUT);
        sgo2(slot_of("LIZARD"), 0);
        memset(sh_partpair, 0xFF, 2u * sizeof sh_partpair[0]);
        sgo(mode_of("FM6"));
        printf("  (cut engines shown %u, on their sibling %u of %u)\n", shown, lands, (unsigned)NELEM(CUT));
        bad += check("the eight SOUNDs: the other engines never offered, their sounds play on a sibling", ok);
    }
    {   /* tracks: ENIGMA on LINE runs on its own host channel under HARP in front (the keys HARP's), both sounding;
         * PLAY on the wheel's centre stops everything */
        uint32_t en = note_of("ENIGMA"), hp = note_of("HARP"), fm = slot_of("FM6"), k, q, lon = 0, hon = 0, w0;
        track_t *tl = &trk[2], *th;
        sgo2(fm, en);
        skey((uint32_t)sh_white_key(7));                /* (a key played: ENIGMA used, it stays on LINE) */
        sh_ne_val[en - SH_NE_FIRST][0] = 4; sh_ne_val[en - SH_NE_FIRST][3] = 100;
        w0 = sh_wnoteslot[4];
        sh_wnoteslot[4] = (uint8_t)hp;
        sgoc(fm, hp);
        th = sh_trk();
        ok = sh.part < 2u && shell_ne_on[1] == en - SH_NE_FIRST + 1u && shell_ne_on[0] == hp - SH_NE_FIRST + 1u &&
             shell_ne_idx == hp - SH_NE_FIRST + 1u && sh_track_note(2) == en;
        th->p[P_ROOT] = 0; th->p[P_SCALE] = 1;
        transport_req = 1; srender(2);
        fm1_in.notes |= 1u << 0; keyboard_block();      /* F3: HARP's root */
        for (k = 0; k < 400u; k++) {
            if (k == 10u) { fm1_in.notes |= 1u << 12; keyboard_block(); }   /* a string */
            srender(1);
            for (q = 0; q < NVOICE; q++) {
                lon += tl->v[q].active && tl->v[q].gate;
                hon += th->v[q].active && th->v[q].gate;
            }
        }
        fm1_in.notes = 0; keyboard_block(); srender(4);
        ok = ok && lon > 0u && hon > 0u;
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);       /* the wheel's centre, PLAY: all stopped */
        sturn(EN_SELECT, (int32_t)SH_SLOTS - (int32_t)sh.wsel);
        stap(B_PLAY); srender(2);
        ok = ok && sh.wsel == SH_SLOTS && !song.playing;
        stap(B_OCTDN);
        sup(B_HOME); sframe();
        sh_wnoteslot[4] = (uint8_t)w0;
        sgo2(fm, note_of("KEYS"));
        printf("  (tracks: ENIGMA under HARP %u voice-blocks, HARP %u)\n", lon, hon);
        bad += check("tracks: ENIGMA on LINE plays under HARP in front, each on its own host channel; PLAY on the wheel's "
                     "centre stops everything", ok);
    }

    {   /* EVOLVE: SEQ tapped again from SEQ; while playing, every EVERY bars the part plays its steps changed, the
         * stored ones never touched: a take moved whole (+5TH: C up a fifth, then a second, folding), LINES' lanes AMT
         * hits at a time, the same way each run; BACK home; a stop or an edit plays the steps as they are */
        uint32_t k, b, okp, okt, okl, same, moved, hits0, hits1;
        uint32_t got[4];
        static step_t keep[NSTEP], run1[NSTEP];
        track_t *t;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        t = sh_trk();
        stap(B_SEQ);
        okp = sh.page[sh.mode] == SHP_SEQ;
        stap(B_SEQ);
        {
            uint8_t ids[4];
            int glob;
            sh_page_ids(ids, &glob);
            okp = okp && sh.page[sh.mode] == SHP_EVOLVE && ids[0] == SP_EEVERY && ids[3] == SP_EBACK &&
                  str_eq(sh_desc(SP_EWHAT, 0, &(int16_t *){0})->label, "MOVE");
        }
        shot("evolve");
        shome();
        for (k = 0; k < NTRK; k++) { trk[k].p[P_ROOT] = 0; trk[k].p[P_SCALE] = 1; }
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[0].n = 1; t->step[0].note[0] = 60; t->step[0].vel = 100;   /* C4 on step 1 */
        t->step[8].n = 1; t->step[8].note[0] = 64; t->step[8].vel = 100;   /* E4 on step 9 */
        t->p[P_SLEN] = 16;
        memcpy(keep, t->step, sizeof keep);
        sh_ev[sh.part][0] = 1; sh_ev[sh.part][2] = 1; sh_ev[sh.part][3] = 3;   /* every bar, +5TH, back after 3 */
        transport_req = 1; srender(2); sframe();
        for (b = 1; b <= 4u; b++) {
            for (k = 0; k < 40000u && sh_move_bar() < b; k++)
                srender(1);
            sframe();
            got[b - 1u] = seq_steps(t)[0].note[0];
        }
        okt = got[0] == 67u && got[1] == 62u && got[2] == 69u && got[3] == 60u &&   /* G, D, A (folded), home */
              seq_steps(t)[8].n == 1u && !memcmp(t->step, keep, sizeof keep);
        printf("  (EVOLVE +5TH: %u %u %u %u)\n", got[0], got[1], got[2], got[3]);
        transport_req = 2; srender(2); sframe();
        okt = okt && seq_steps(t) == t->step;
        sh_ev[sh.part][0] = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        {   /* LINES' lanes: an ACID line, NOTES AMT 1 a bar, twice the same */
            track_t *tl;
            uint32_t run;
            sgo2(slot_of("ANALOG"), note_of("LINES"));
            tl = sh_trk();
            sh_sp[SP_DENS - SP_FIRST] = 8; sh_sp[SP_SPAN - SP_FIRST] = 5;
            sh_lwr_load(tl, 3u);
            memcpy(keep, tl->step, sizeof keep);
            sh_ev[sh.part][0] = 1; sh_ev[sh.part][1] = 1; sh_ev[sh.part][2] = 0; sh_ev[sh.part][3] = 0;   /* (AMT 1:
                                                                                                  * one hit moves) */
            okl = 1; same = 1;
            for (run = 0; run < 2u; run++) {
                transport_req = 1; srender(2); sframe();
                for (k = 0; k < 40000u && sh_move_bar() < 1u; k++)
                    srender(1);
                sframe();
                okl = okl && seq_steps(tl) != tl->step && !memcmp(tl->step, keep, sizeof keep);
                for (moved = 0, hits0 = hits1 = 0, k = 0; k < 16u; k++) {
                    hits0 += (uint32_t)__builtin_popcount(keep[k].hit);
                    hits1 += (uint32_t)__builtin_popcount(seq_steps(tl)[k].hit);
                    moved += seq_steps(tl)[k].hit != keep[k].hit;
                }
                okl = okl && hits0 == hits1 && moved == 1u;
                if (!run)
                    memcpy(run1, seq_steps(tl), sizeof run1);
                else
                    same = !memcmp(run1, seq_steps(tl), sizeof run1);
                transport_req = 2; srender(2); sframe();
                okl = okl && seq_steps(tl) == tl->step;
            }
            transport_req = 1; srender(2); sframe();    /* an edit while evolved: the steps as they are */
            for (k = 0; k < 40000u && sh_move_bar() < 1u; k++)
                srender(1);
            sframe();
            tl->step[15].hit ^= 1u;
            sframe();
            okl = okl && same && seq_steps(tl) == tl->step;
            transport_req = 2; srender(2); sframe();
            sh_ev[sh.part][0] = 0;
            for (k = 0; k < NSTEP; k++) step_clear(&tl->step[k]);
            sh_sp[SP_DENS - SP_FIRST] = 10;
        }
        sgo2(slot_of("FM6"), note_of("KEYS"));
        printf("  (EVOLVE: page %u, take %u, lanes %u)\n", okp, okt, okl);
        bad += check("EVOLVE: SEQ again opens it; a take moves whole by +5TH and comes BACK, LINES' hits change AMT at a "
                     "time and the same each run; the stored steps never change; a stop or an edit plays them as they are",
                     okp && okt && okl);
    }
    {   /* PLAY loops: a KEYS take keeps looping when its pair is left (in time, back as it plays); HOME + PLAY on it mutes
         * it and brings it back, an unheld PLAY pair has NOTHING TO MUTE; REC arms only the pair in front; a CHORD take
         * left plays through CHORD PLAY; the front's held notes are never taken by a take looping on; a third pair
         * takes the part left longest ago and the take returns with its pair; a project keeps both PLAY parts */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), an = slot_of("ANALOG"), kn = note_of("KEYS"), cn = note_of("CHORD");
        uint32_t k, v, pa, pb, heard = 0, idx0, okl, okm, okr, okc, okv, okt, okp;
        track_t *ta, *tb;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sh_tmute = 0;
        sgo2(fm, kn);                                   /* A: FM6 + KEYS, a take on every step */
        ta = sh_trk(); pa = sh.part;
        ta->p[P_SLEN] = 16; ta->p[P_VOICE] = V_POLY;
        for (k = 0; k < 16u; k++) { ta->step[k].n = 1; ta->step[k].note[0] = 48; ta->step[k].time = ST_NOTE; ta->step[k].vel = 100; }
        sframe();
        sgo2(lz, cn);                                   /* B: LIZARD + CHORD, in front, on the other part */
        tb = sh_trk(); pb = sh.part;
        transport_req = 1; srender(2);
        for (k = 0; k < 200u; k++) {
            srender(1);
            for (v = 0; v < NVOICE; v++) heard += ta->v[v].active && ta->v[v].gate && ta->v[v].note == 48u;
        }
        idx0 = ta->seq_idx;
        okl = pa != pb && heard > 0u && !ta->p[P_MUTE];
        sgo2(fm, kn);                                   /* back to A: as it plays, in time */
        okl = okl && sh.part == pa && ta->seq_idx == idx0 && ta->step[0].n == 1u;
        okr = song.rec == 0u || song.rec == (1u << sh.part);
        song.rec = (uint8_t)(1u << sh.part);
        sgo2(lz, cn);                                   /* REC follows the front: A's part not armed */
        okr = okr && song.rec == (uint8_t)(1u << pb) && !((song.rec >> pa) & 1u);
        song.rec = 0;
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);       /* mute A from the wheel: FM6 (C4) + KEYS (C#4) */
        skey((uint32_t)sh_slot_key(sh_mode_pos(fm))); skey((uint32_t)sh_note_key(sh_note_pos(kn)));
        stap(B_PLAY);
        okm = ((sh_tmute >> pa) & 1u) && ta->p[P_MUTE] && str_eq(sh.toast, "KEYS MUTED");
        stap(B_PLAY);
        okm = okm && !sh_tmute && !ta->p[P_MUTE];
        skey((uint32_t)sh_slot_key(sh_mode_pos(an)));  /* ANALOG + KEYS: no part holds it */
        stap(B_PLAY);
        okm = okm && str_eq(sh.toast, "NOTHING TO MUTE") && !sh_tmute;
        stap(B_OCTDN);
        sup(B_HOME); sframe();
        sgo2(lz, cn);
        sgo2(fm, cn);                                   /* FM6 + CHORD: a CHORD pair on A's part ... */
        sgo2(fm, kn);
        okc = 1;
        {   /* (LIZARD + CHORD left, KEYS in front: CHORD's part plays through CHORD PLAY still) */
            uint32_t cp;
            for (cp = 0; cp < 2u && sh_track_note(cp) != cn; cp++)
                ;
            okc = cp < 2u && cp != sh.part && ((shell_chord_play >> cp) & 1u);
        }
        {   /* the front's held notes: a take looping on never takes them (the steal itself, voice.c voice_victim): four
             * held in front, four on the take, the budget full; the take's victim is its own; the front's (FG) only
             * when nothing else can go */
            track_t *tf, *tl, *vp = 0;
            uint32_t kk;
            sgo2(fm, kn);
            sframe();                                   /* (the parts' roles follow the front) */
            tf = sh_trk(); tl = &trk[sh.part ^ 1u];
            transport_req = 2; srender(4);
            for (k = 0; k < NPART; k++) memset(trk[k].v, 0, sizeof trk[k].v);
            for (k = 0; k < 4u; k++) {
                tf->v[k].active = tf->v[k].gate = 1; tf->v[k].note = (uint8_t)(72u + k); tf->v[k].age = 10u + k;
                tl->v[k].active = tl->v[k].gate = 1; tl->v[k].note = (uint8_t)(60u + k); tl->v[k].age = 1000u + k;
            }
            kk = voice_victim(tl, 0, &vp);
            okv = shell_role[tf - trk] == SR_FG && kk < NVOICE && vp == tl;
            for (k = 0; k < 4u; k++) tl->v[k].active = tl->v[k].gate = 0;
            for (k = 4u; k < 8u; k++) {                 /* the front alone holds the budget: then it gives one */
                tf->v[k].active = tf->v[k].gate = 1; tf->v[k].note = (uint8_t)(80u + k); tf->v[k].age = 10u + k;
            }
            vp = 0;
            kk = voice_victim(tl, 0, &vp);
            okv = okv && kk < NVOICE && vp == tf;
            for (k = 0; k < NPART; k++) memset(trk[k].v, 0, sizeof trk[k].v);
        }
        transport_req = 2; srender(4); sframe();
        {   /* the take-over: A (FM6+KEYS, a take) and B (LIZARD+CHORD, a take) loop; C (ANALOG+KEYS) takes the part
             * left longest ago; A comes back with its take */
            sgo2(fm, kn);
            ta = sh_trk(); pa = sh.part;
            for (k = 0; k < NSTEP; k++) step_clear(&ta->step[k]);
            ta->step[5].n = 1; ta->step[5].note[0] = 55; ta->step[5].time = ST_NOTE; ta->step[5].vel = 100;
            sframe();
            sgo2(lz, cn);
            tb = sh_trk(); pb = sh.part;
            for (k = 0; k < NSTEP; k++) step_clear(&tb->step[k]);
            tb->step[2].n = 1; tb->step[2].note[0] = 62; tb->step[2].time = ST_NOTE; tb->step[2].vel = 100;
            sframe();
            sh_switch_pair(an, kn); sframe();
            {
                char want[48];
                snprintf(want, sizeof want, "FM6 KEYS STOPS: PART %u", pa + 1u);
                okt = sh.part == pa && str_eq(sh.toast, want) && sh_partpair[pb] == (uint16_t)(lz | cn << 8);
            }
            sgo2(fm, kn);                               /* A back: on the part left longest ago, B's, with its take */
            okt = okt && sh_trk()->step[5].n == 1u && sh_trk()->step[5].note[0] == 55u;
        }
        {   /* a project keeps both PLAY parts' pairs and their mutes (tag 9, 9 bytes) */
            uint32_t p1 = sh.part, p2 = sh.part ^ 1u;
            uint16_t a1 = sh_partpair[p1], a2 = sh_partpair[p2];
            sh_tmute = (uint8_t)(1u << p2); trk[p2].p[P_MUTE] = 1;
            okp = project_save(2) == 0;
            sh_tmute = 0; trk[p2].p[P_MUTE] = 0;
            sgo2(slot_of("PHASE"), kn);
            project_load(2);
            sframes(100);
            okp = okp && sh_partpair[p1] == a1 && sh_partpair[p2] == a2 && ((sh_tmute >> p2) & 1u) && trk[p2].p[P_MUTE] &&
                  sh.part == p1;
            sh_tmute = 0; trk[p2].p[P_MUTE] = 0;
        }
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sgo2(fm, kn);
        printf("  (PLAY loops: left loops %u, mute %u, REC %u, CHORD PLAY %u, front held %u, take-over %u, project %u)\n",
               okl, okm, okr, okc, okv, okt, okp);
        bad += check("PLAY loops: a take left loops on and comes back in time; HOME + PLAY mutes it; REC arms the front only; "
                     "CHORD left plays through CHORD PLAY; the front's held notes kept; a third pair takes the part left "
                     "longest ago, the take back with its pair; a project keeps both", okl && okm && okr && okc && okv &&
                     okt && okp);
    }
    {   /* what plays behind, in the wheel: HOME held, every pair playing on a part pulses its keys on its beat (the
         * picked one goes out, the others light), a muted pair breathes, the screen marks both; the wheel opening
         * with the transport running names what plays behind the front */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), dr = slot_of("DRUMS"), kn = note_of("KEYS"),
                 cn = note_of("CHORD"), rn = note_of("RUDIMENT");
        uint32_t k, v, on, beat, mute, wb = 1u << sh_mode_pos(dr), nb = 1u << (8u + sh_note_pos(rn)), seen = 0, okk, okm;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sh_tmute = 0;
        sgo2(dr, rn);                                   /* DRUMS + RUDIMENT on BEAT */
        sgo2(fm, kn);                                   /* FM6 + KEYS, a take, left */
        sh_trk()->step[0].n = 1; sh_trk()->step[0].note[0] = 48; sh_trk()->step[0].time = ST_NOTE; sh_trk()->step[0].vel = 100;
        sframe();
        sgo2(lz, cn);                                   /* LIZARD + CHORD in front */
        transport_req = 1; srender(2);
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);
        okk = sh.wheel && strstr(sh.toast, "KEYS") && strstr(sh.toast, "RUDIMENT") && !strstr(sh.toast, "CHORD");
        for (k = 0; k < 400u; k++) {
            srender(1);
            sh_part_keys(&on, &beat, &mute);
            seen |= (beat & (wb | nb)) == (wb | nb) ? 1u : !(beat & nb) ? 2u : 0u;
            if (beat & nb)
                okk = okk && sh_wheel_led(0, 8u + sh_note_pos(rn), beat, mute) == SHL_LIT &&
                      sh_wheel_led(1, 8u + sh_note_pos(rn), beat, mute) == SHL_OFF;
        }
        okk = okk && seen == 3u && (on & wb) && (on & nb) && !mute;
        skey((uint32_t)sh_slot_key(sh_mode_pos(dr))); skey((uint32_t)sh_note_key(sh_note_pos(rn)));
        stap(B_PLAY);                                   /* BEAT muted: RUDIMENT's key breathes */
        sh_part_keys(&on, &beat, &mute);
        okm = ((sh_tmute >> SH_BEAT) & 1u) && (mute & nb) && !(on & nb) && !(beat & nb) &&
              sh_wheel_led(0, 8u + sh_note_pos(rn), beat, mute) == SHL_BREATH;
        stap(B_PLAY);
        okm = okm && !sh_tmute;
        stap(B_OCTDN);
        sup(B_HOME); sframe();
        transport_req = 2; srender(4); sframe();
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);       /* stopped: nothing plays, no news */
        okm = okm && sh.wheel && !strstr(sh.toast, "RUDIMENT");
        stap(B_OCTDN);
        sup(B_HOME); sframe();
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sgo2(fm, kn);
        printf("  (wheel marks: playing %u, muted %u)\n", okk, okm);
        bad += check("the wheel shows what plays: the pairs on a part pulse their keys on its beat, a muted one breathes; "
                     "opened while playing, it names what plays behind", okk && okm);
    }
    {   /* PROJECTS > NEW: asked, then every part's steps, the knob moves, the tracks' pairs and the takes parked gone,
         * the sounds kept; REC's first ring is UNDO NEW and brings it all back; once the music changes, it is not */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), dr = slot_of("DRUMS"), kn = note_of("KEYS"),
                 rn = note_of("RUDIMENT");
        uint32_t k, v, any = 0, okn, oku, okc;
        int16_t lev;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sh_tmute = 0;
        sgo2(dr, rn);
        grid_hit(sh_trk(), 0, 0, 1u); grid_hit(sh_trk(), 4, 1, 1u);
        sgo2(lz, kn);                                   /* LIZARD + KEYS: a take, left looping */
        sh_trk()->step[3].n = 1; sh_trk()->step[3].note[0] = 50; sh_trk()->step[3].time = ST_NOTE; sh_trk()->step[3].vel = 90;
        sframe();
        sgo2(fm, kn);                                   /* FM6 + KEYS in front, a take of its own */
        sh_trk()->step[1].n = 1; sh_trk()->step[1].note[0] = 60; sh_trk()->step[1].time = ST_NOTE; sh_trk()->step[1].vel = 90;
        okn = motion_put(sh_trk(), 2, P_E0, 1, 0) == 0;
        lev = sh_trk()->p[P_E0 + 1];
        sframe();
        sh.global = SHG_PROJECT; sh.pslot = 3; sframe();
        sturn(EN_K1, 1);                                /* the cursor past D: NEW */
        okn = okn && sh.pslot == 4u;
        stap(B_OCTDN);
        okn = okn && sh.pq == 3u;
        stap(B_OCTUP);
        sframes(50);
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) any |= trk[k].step[v].n | trk[k].step[v].hit;
        for (k = 0; k < SH_NNOTES; k++) for (v = 0; v < NSTEP; v++) any |= sh_nsteps(k)[v].n | sh_nsteps(k)[v].hit;
        okn = okn && !any && motion.count == 0u && str_eq(sh.toast, "NEW: EMPTY PROJECT") && sh.toast_undo &&
              sh_partpair[SH_BEAT] == SH_NOPAIR && sh_partpair[sh.part ^ 1u] == SH_NOPAIR && sh.mode == fm &&
              sh_trk()->p[P_E0 + 1] == lev && str_eq(sh_ring_word(), "UNDO NEW");
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* the first ring: UNDO NEW */
        oku = str_eq(sh.toast, "NEW UNDONE") && sh_trk()->step[1].n == 1u && motion.count > 0u &&
              (step_lanes(&trk[SH_BEAT].step[4]) & 2u) && sh_partpair[SH_BEAT] == (uint16_t)(dr | rn << 8) &&
              trk[sh.part ^ 1u].step[3].n == 1u && !project_new_kept();
        sh.global = SHG_PROJECT; sh.pslot = 4; sframe();
        stap(B_OCTUP); stap(B_OCTUP); sframes(50);      /* NEW again, then a step: the undo is the take's */
        okc = project_new_kept();
        sh_trk()->step[0].n = 1; sh_trk()->step[0].note[0] = 62; sh_trk()->step[0].time = ST_NOTE; sh_trk()->step[0].vel = 90;
        sframe();
        okc = okc && !project_new_kept() && !str_eq(sh_ring_word(), "UNDO NEW");
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sh.global = SHG_NONE; sframe();
        sgo2(fm, kn);
        printf("  (NEW: empties %u, undone %u, changed: no undo %u)\n", okn, oku, okc);
        bad += check("PROJECTS > NEW: asked, it empties every part, the knob moves, the tracks and the takes parked, the "
                     "sounds kept; UNDO NEW brings it back until the music changes", okn && oku && okc);
    }
    {   /* RUDIMENT's MAP: ALGORITHM past the moons; Grids' map plays KICK SNARE HATCL live, ORed with the lanes' own
         * hits (the map's faint on the keys, yours lit), X Y and the densities as Grids reads them, the strikes heard
         * as the map has them, EVOLVE drifting X and Y with the steps left alone, a project keeping it all */
        uint32_t dr = slot_of("DRUMS"), rn = note_of("RUDIMENT"), k, q, s, okt, okm = 1, okl, okh, oke, okp, want = 0,
                 kicks = 0, lastage[NVOICE] = {0}, mk[32], w2 = 99;
        track_t *t;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        sh_tmute = 0;
        sgo2(dr, rn);
        shome(); sframe();
        t = sh_trk();
        grid_hit(t, 2, 0, 1u); grid_hit(t, 6, 2, 1u);  /* a kick (a map lane) and a clap (its own) */
        snote();
        for (k = 0; k < 30u && sh_sp[SP_RHY - SP_FIRST] != (int16_t)SH_MAP; k++)
            sturn(EN_ALGO, 1);
        sframe();
        okt = sh_sp[SP_RHY - SP_FIRST] == (int16_t)SH_MAP && shell_map_on && sh_eu[0][0] == 0 && sh_eu[0][3] == 0 &&
              !(step_lanes(&t->step[2]) & 1u) && (step_lanes(&t->step[6]) & 4u) && sh_page_ok(SHP_MAP) &&
              sh_page_ok(SHP_MAP2) && str_eq(sh.toast, "MAP: KICK SNARE HATS ON THE MAP");
        for (s = 0; s < 32u; s++) {                     /* X Y 64 (Grids' 128: node 4 alone), KICK 64, SNARE 48 */
            uint32_t kl = GRIDS_NODE[4][s] * 255u / 256u * 255u / 256u,   /* (Grids' U8Mix at 0 twice: x 255/256) */
                     sl = GRIDS_NODE[4][32u + s] * 255u / 256u * 255u / 256u,
                     hl = GRIDS_NODE[4][64u + s] * 255u / 256u * 255u / 256u;
            okm = okm && sh_map_at(0, s) == (kl > 127u ? 2u | (kl > 192u) : 0u) &&
                  sh_map_at(1, s) == (sl > 159u ? 2u | (sl > 192u) : 0u) &&
                  sh_map_at(3, s) == (hl > 127u ? 2u | (hl > 192u) : 0u) && !sh_map_at(2, s);
            mk[s] = kl > 127u;
            if (!mk[s] && s < 16u && w2 == 99u)
                w2 = s;
        }
        sh_sp[SP_LANE - SP_FIRST] = 0; sframe();       /* the KICK lane in front: the map's hits faint, yours lit */
        grid_hit(t, w2, 0, 1u);
        okl = sh_inst_led((uint32_t)sh_white_key(w2), 0) == SHL_LIT;
        for (s = 0; s < 16u; s++)
            if (mk[s] && s != w2)
                okl = okl && sh_inst_led((uint32_t)sh_white_key(s), 0) == SHL_LOW;
        for (s = 0; s < 16u; s++)
            want += mk[s] || s == w2;
        transport_req = 1; srender(2);                  /* a bar at 120: the kicks as the map and the hand have them */
        for (k = 0; k < 2u * FS / HALF_FRAMES - 8u; k++) {
            srender(1);
            if (!(k & 3u))
                sframe();
            for (q = 0; q < NVOICE; q++)
                if (t->v[q].active && t->v[q].age != lastage[q]) {
                    kicks += t->v[q].note == lane_note(t, 0);
                    lastage[q] = t->v[q].age;
                }
        }
        okh = kicks == want && !shell_ev_steps[SH_BEAT];
        sturn(EN_K1, 0);
        sh_ev[SH_BEAT][0] = 1;                          /* EVOLVE every bar: X and Y drift, the steps stay */
        {
            uint8_t keep[sizeof t->step];
            memcpy(keep, t->step, sizeof keep);
            for (k = 0; k < 6u * FS / HALF_FRAMES; k++) {
                srender(1);
                if (!(k & 3u))
                    sframe();
            }
            oke = (sh_map_dx || sh_map_dy) && !memcmp(keep, t->step, sizeof keep) && !shell_ev_steps[SH_BEAT] &&
                  shell_map_x == (uint8_t)(clamp(64 + sh_map_dx, 0, 127) * 2);
        }
        transport_req = 2; srender(4); sframe();
        oke = oke && !sh_map_dx && !sh_map_dy;          /* (stopped: back) */
        sh_ev[SH_BEAT][0] = 0;
        sh.page[sh.mode] = SHP_MAP2; sframe();          /* MAP 2: the densities */
        sturn(EN_K1, 10); sframe();
        okm = okm && sh_sp[SP_MKICK - SP_FIRST] > 64 && shell_map_dens[0] == (uint8_t)(sh_sp[SP_MKICK - SP_FIRST] * 2);
        sh_sp[SP_MX - SP_FIRST] = 10;
        okp = project_save(2) == 0;
        sh_sp[SP_MX - SP_FIRST] = 64; sh_sp[SP_RHY - SP_FIRST] = 0; sframe();
        project_load(2); sframes(50);
        okp = okp && sh_map_rhy() == (int16_t)SH_MAP && sh_sp[SP_MX - SP_FIRST] == 10 && shell_map_on &&
              shell_map_x == 20u;
        sgo2(dr, rn); shome(); sframe();
        snote();
        sturn(EN_ALGO, -1); sframe();                   /* back to a moon: the map off */
        okp = okp && !shell_map_on && !sh_page_ok(SHP_MAP);
        sh_sp[SP_MX - SP_FIRST] = 64; sh_sp[SP_MKICK - SP_FIRST] = 64; sh_sp[SP_RHY - SP_FIRST] = 0;
        for (k = 0; k < NPART; k++) for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        for (k = 0; k < 8u; k++) { sh_eu_i[0][0][k] = 16; sh_eu_i[0][1][k] = sh_eu_i[0][2][k] = 0; shell_lane_len_i[0][k] = 16; }
        sframe();
        sgo2(slot_of("FM6"), note_of("KEYS"));
        printf("  (MAP: turned %u, Grids' levels %u, keys %u, heard %u of %u kicks %u, EVOLVE %u, project %u)\n", okt,
               okm, okl, kicks, want, okh, oke, okp);
        bad += check("RUDIMENT's MAP: Grids' map plays KICK SNARE HATCL live with the lanes' own hits, X Y and the "
                     "densities as Grids reads them, EVOLVE drifts X and Y, a project keeps it", okt && okm && okl &&
                     okh && oke && okp);
    }
    {   /* FREEZE (REC + OCT+): MAP's hits into KICK SNARE HATCL, ALGORITHM off MAP; ENIGMA on LINE: the last loop heard
         * into LINES' lanes, a pitch a lane, LINE on LINES playing what was heard; ACID: the line left alone by DENS;
         * each undone by REC's first ring */
        uint32_t dr = slot_of("DRUMS"), an = slot_of("ANALOG"), rn = note_of("RUDIMENT"), en = note_of("ENIGMA"),
                 ln = note_of("LINES"), k, q, s, okg, oke, oku, oka, okn, cap = 0, hits = 0, same = 1, rec0;
        uint8_t keepm[32];
        uint32_t elen;
        track_t *t;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        sh_tmute = 0;
        sgo2(dr, rn); shome(); sframe();
        t = sh_trk();
        snote();
        for (k = 0; k < 30u && sh_sp[SP_RHY - SP_FIRST] != (int16_t)SH_MAP; k++)
            sturn(EN_ALGO, 1);
        sframe();
        for (s = 0; s < 32u; s++)
            keepm[s] = (uint8_t)((sh_map_at(0, s) ? 1u : 0u) | (sh_map_at(1, s) ? 2u : 0u) | (sh_map_at(3, s) ? 8u : 0u));
        rec0 = song.rec;
        sdown(B_REC); sframes(SH_TAP_MS + 50u); stap(B_OCTUP); sup(B_REC); sframe();
        okg = !shell_map_on && sh_sp[SP_RHY - SP_FIRST] == 0 && song.rec == rec0 && str_eq(sh.toast, "FROZEN: THE MAP'S BEAT IS YOURS");
        for (s = 0; s < 32u; s++)
            okg = okg && (step_lanes(&t->step[s]) & 0x0Bu) == keepm[s];
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe(); sframe();   /* UNDO BEAT: the map again */
        oku = shell_map_on && !(step_lanes(&t->step[0]) & 0x0Bu);
        sturn(EN_ALGO, -1); sframe();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        for (k = 0; k < 8u; k++) { sh_eu_i[0][0][k] = 16; sh_eu_i[0][1][k] = sh_eu_i[0][2][k] = 0; shell_lane_len_i[0][k] = 16; }
        sh_sp[SP_RHY - SP_FIRST] = 0; sframe();
        sgo2(an, en);                                   /* ENIGMA on LINE: a loop played, then frozen */
        t = sh_trk();
        sdown(B_REC); sframes(SH_TAP_MS + 50u); stap(B_OCTUP); sup(B_REC); sframe();
        okn = str_eq(sh.toast, "PLAY A WHOLE LOOP, THEN FREEZE") && sh.note == en;
        transport_req = 1; srender(2);
        for (k = 0; k < 9u * FS / HALF_FRAMES; k++) {   /* (nine seconds: over a loop of 32 at 120) */
            srender(1);
            if (!(k & 3u))
                sframe();
        }
        elen = (uint32_t)clamp(t->p[P_SLEN], 1, 32);   /* (the loop as it is when it freezes) */
        for (s = 0; s < elen; s++)
            cap += sh_frz.n[s];
        sdown(B_REC); sframes(SH_TAP_MS + 50u); stap(B_OCTUP); sup(B_REC); sframe();
        oke = sh.note == ln && sh.part == SH_LINE && str_eq(sh.toast, "FROZEN INTO LINES: ENIGMA") && sh_li == 1u;
        for (s = 0; s < elen; s++) {
            hits += (uint32_t)__builtin_popcount(step_lanes(&t->step[s]));
            for (k = 0; k < sh_frz.n[s]; k++) {         /* each note heard: a hit on the lane that plays it */
                uint32_t l;
                for (l = 0; l < 8u && sh_lnote_i[1][l] != sh_frz.note[s][k]; l++)
                    ;
                same = same && l < 8u && ((step_lanes(&t->step[s]) >> l) & 1u) && shell_lane_note_i[1][l] == sh_frz.note[s][k];
            }
        }
        oke = oke && cap > 0u && hits > 0u && hits <= cap && same && sh_eu_i[1][0][0] == 0 &&
              trk[SH_LINE].p[P_SLEN] == (int16_t)elen;
        transport_req = 2; srender(4); sframe();
        sdown(B_REC); sframes(SH_LONG_MS + 100u); sup(B_REC); sframe();   /* undone: ENIGMA back */
        oku = oku && sh.note == en && str_eq(sh.toast, "FREEZE UNDONE") && sh_lnote_i[1][0] == 0;
        sgo2(an, ln);                                   /* ACID: a line, frozen: DENS leaves it */
        t = sh_trk();
        snote();
        sturn(EN_ALGO, 3); sframe();
        {
            uint8_t line[sizeof t->step];
            oka = sh_sp[SP_RHY - SP_FIRST] != 0 && sh_pat_any(t);
            sdown(B_REC); sframes(SH_TAP_MS + 50u); stap(B_OCTUP); sup(B_REC); sframe();
            oka = oka && sh_sp[SP_RHY - SP_FIRST] == 0 && str_eq(sh.toast, "FROZEN: THE LINE IS YOURS");
            memcpy(line, t->step, sizeof line);
            sh.page[sh.mode] = SHP_LINE; sframe();
            sturn(EN_K1, 4); sframe();                  /* DENS: nothing rewritten */
            oka = oka && !memcmp(line, t->step, sizeof line);
        }
        for (k = 0; k < NPART; k++) for (q = 0; q < NSTEP; q++) step_clear(&trk[k].step[q]);
        sh.page[sh.mode] = SHP_HOME;
        sgo2(slot_of("FM6"), note_of("KEYS"));
        sdown(B_REC); sframes(SH_TAP_MS + 50u); stap(B_OCTUP); sup(B_REC); sframe();
        okn = okn && str_eq(sh.toast, "FREEZE: PUT A GENERATOR IN FRONT");
        printf("  (FREEZE: MAP %u, ENIGMA %u (%u notes, %u hits), undo %u, ACID %u, refusals %u)\n", okg, oke, cap, hits,
               oku, oka, okn);
        bad += check("FREEZE: REC + OCT+ writes MAP's hits, ENIGMA's last loop (into LINES) and ACID's line into lanes to "
                     "edit; REC's first ring undoes it", okg && oke && oku && oka && okn);
    }
    {   /* the SOUND view's pages are the SOUND's: the same under every NOTE engine as under KEYS (LIZARD's DRAW pages
         * once needed KEYS), and EDIT walks to DRAW 1-4 under LINES; INSERT OFF: its A B C blank */
        uint32_t m, n, nn, p, diff = 0, keys = note_of("KEYS"), lz = slot_of("LIZARD"), seen = 0, i;
        uint64_t base, o;
        uint8_t ids[4];
        int g;
        for (m = 0; m < SH_NMODES; m++) {
            if (sh_mode_pos(m) >= SH_SLOTS)
                continue;
            for (n = 0, base = 0; n < SH_NNOTES; n++) {
                nn = n == 0 ? keys : n == keys ? 0 : n;
                sgoc(m, nn);
                if (sh_trk()->eng_req == ENGI_LIZARD)
                    sh_trk()->p[P_E0] = LZM_DRAW;
                for (p = 0, o = 0; p < SHP_N; p++)
                    if (sh_page_ok(p) && !sh_page_note(p))
                        o |= 1ull << p;
                if (n == 0)
                    base = o;
                else
                    diff += o != base;
            }
        }
        sgoc(lz, note_of("LINES"));
        sh_trk()->p[P_E0] = LZM_DRAW;
        if (sh_view_note()) {
            stap(B_HOME); sframes(400);
        }
        sh.page[sh.mode] = SHP_HOME;
        for (i = 0; i < 8u; i++) {
            stap(B_EDIT);
            seen |= (sh.page[sh.mode] == SHP_DRAW1) | (sh.page[sh.mode] == SHP_DRAW2) << 1;
        }
        sh.page[sh.mode] = SHP_INS;
        sh_trk()->p[P_ITYPE] = 0;
        sh_page_ids(ids, &g);
        ok = ids[0] == P_ITYPE && ids[1] == SH_NONE && ids[3] == SH_NONE;
        sh_trk()->p[P_ITYPE] = 1;
        sh_page_ids(ids, &g);
        ok = ok && ids[1] == P_IA;
        sh_trk()->p[P_ITYPE] = 0;
        sh.page[sh.mode] = SHP_HOME;
        printf("  (SOUND pages differing from KEYS' %u; DRAW 1-4 and 5-8 under LINES %u; INSERT %u)\n", diff, seen, ok);
        bad += check("pages: every SOUND's pages the same under any NOTE engine; LIZARD's DRAW under LINES; INSERT OFF "
                     "shows no A B C", !diff && seen == 3u && ok);
    }
    {   /* ARP from anywhere, under every NOTE engine: the ARP page, MODE on KNOB 1; SOUND MAIN starts with the switch
         * that makes the engine another instrument (the rest of MAIN as before) */
        uint32_t n, from, arp = 1, mains = 0, m;
        uint8_t ids[4];
        int g;
        static const struct { const char *nm; uint8_t id; } SW[] = {
            {"ANALOG", P_E0}, {"PHASE", P_E0}, {"PHYS", P_E0}, {"DRUMS", P_E0}, {"LIZARD", P_E0}, {"MOUTH", P_E4},
        };
        for (n = 0; n < SH_NNOTES; n++)
            for (from = 0; from < 3u; from++) {
                sgoc(slot_of("ANALOG"), n);
                if (from == 0u)
                    shome();
                else if (from == 1u && sh_page_ok(SHP_NOTE))   /* (KEYS' NOTE view opens on ARP itself) */
                    snmain();
                else
                    sh.global = SHG_SYSTEM;
                stap(B_ARP);
                sh_page_ids(ids, &g);
                arp &= sh.page[sh.mode] == SHP_ARP && ids[0] == P_AMODE && !sh.global;
            }
        stap(B_ARP);
        arp &= sh.page[sh.mode] == SHP_ARP2;
        stap(B_ARP);
        arp &= sh.page[sh.mode] == SHP_ARP;
        for (m = 0; m < NELEM(SW); m++) {
            sgoc(slot_of(SW[m].nm), note_of("KEYS"));
            sh.page[sh.mode] = SHP_HOME; sframe();
            sh_page_ids(ids, &g);
            mains += ids[0] == SW[m].id;
        }
        printf("  (ARP lands on MODE %u; MAINs starting with their switch %u of %u)\n", arp, mains, (uint32_t)NELEM(SW));
        bad += check("pages: ARP opens the ARP page (MODE) from any page under any NOTE engine, again ARP 2; SOUND MAIN "
                     "starts with the engine's switch (WAVE, MODEL, KIT, MODE, PHRASE)", arp && mains == NELEM(SW));
        shome();
    }
    {   /* a wrong note in a loop, armed and playing: REC + its key held over a pass takes it out wherever it plays,
         * the rest of each step stays, the arm stays, and UNDO TAKE brings it back */
        track_t *t;
        uint32_t k = sh_white_key(4), x, i, left = 0, other = 0, ok;
        sgoc(slot_of("ANALOG"), note_of("KEYS"));
        t = sh_trk();
        x = kb_map(t, k);
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        t->p[P_SLEN] = 4;
        for (i = 0; i < 4u; i++) {                      /* each step: a C, and the wrong note on steps 2 and 4 */
            t->step[i].note[0] = 48u; t->step[i].n = 1; t->step[i].time = ST_NOTE;
            if (i & 1u) { t->step[i].note[1] = (uint8_t)x; t->step[i].n = 2; }
        }
        song.rec |= (uint8_t)(1u << sh.part);
        transport_req = 1; srender(2); sframe();
        sh_arp_t = sh_rec_t = 0;
        sdown(B_REC); sframe();
        fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); sframe();
        for (i = 0; i < 60u; i++) { srender(8); sframe(); }   /* (more than a pass of 4 steps at 120) */
        fm1_in.notes &= ~(1u << k); host_notes &= ~(1u << k); keyboard_block(); sframe();
        sup(B_REC); sframe();
        for (i = 0; i < 4u; i++) {
            uint32_t j;
            for (j = 0; j < t->step[i].n; j++) {
                left += t->step[i].note[j] == x;
                other += t->step[i].note[j] == 48u;
            }
        }
        ok = !left && other == 4u && ((song.rec >> sh.part) & 1u);
        transport_req = 2; srender(2); sframe();
        song.rec &= (uint8_t)~(1u << sh.part);
        sh_take_undo();
        ok = ok && t->step[1].n == 2u && t->step[1].note[1] == x;
        printf("  (erased live: %u of the note left, %u C kept, armed %u)\n", left, other, (song.rec >> sh.part) & 1u);
        bad += check("REC + a key while it plays: that note leaves every step it passes, the rest stays; UNDO TAKE "
                     "brings it back", ok);
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        shome();
    }
    {   /* double taps: ARP twice, HOLD on or off with the page left as the first tap put it; REC twice, armed as it
         * was and REC MODE's + KNOBS flipped (and back) */
        track_t *t;
        int16_t h;
        uint32_t m0, ok1, ok2;
        sgoc(slot_of("ANALOG"), note_of("KEYS"));
        t = sh_trk();
        t->p[P_AMODE] = 1;
        h = t->p[P_AHOLD];
        shome(); sframes(400);
        stap2(B_ARP);
        ok1 = t->p[P_AHOLD] == !h && sh.page[sh.mode] == SHP_ARP;
        sframes(400);
        stap(B_ARP);
        ok1 = ok1 && sh.page[sh.mode] == SHP_ARP2 && t->p[P_AHOLD] == !h;
        t->p[P_AHOLD] = h; t->p[P_AMODE] = 0;
        m0 = sh_rec_mode();
        song.rec |= (uint8_t)(1u << sh.part);
        sframes(400);
        stap2(B_REC);
        ok2 = ((song.rec >> sh.part) & 1u) && sh_rec_mode() == (m0 ^ 1u) && str_eq(sh.toast, m0 & 1u ? "NOTES ONLY" : "KNOBS RECORD");
        sframes(400);
        stap2(B_REC);
        ok2 = ok2 && ((song.rec >> sh.part) & 1u) && sh_rec_mode() == m0;
        sframes(400);
        stap(B_REC);
        ok2 = ok2 && !((song.rec >> sh.part) & 1u);
        sframes(400);
        shome();
        bad += check("double taps: ARP twice toggles HOLD (the page stays ARP); REC twice keeps the arm and flips + KNOBS",
                     ok1 && ok2);
    }
    {   /* HOME twice walks every pair playing behind in turn (the one left longest ago first); REC on a pair in the
         * wheel: that pair in front, a tap arms nothing, held its rings clear it (not the pair that was in front) */
        uint32_t fm = slot_of("FM6"), lz = slot_of("LIZARD"), kn = note_of("KEYS"), cn = note_of("CHORD");
        uint32_t k, v, p, want = 0, seen = 0, okc, okw;
        track_t *ta, *tb;
        transport_req = 2; srender(4); sframe();
        for (k = 0; k < NPART; k++) for (v = 0; v < NSTEP; v++) step_clear(&trk[k].step[v]);
        sh_tmute = 0; song.rec = 0;
        sgo2(fm, kn);                                   /* A: FM6 + KEYS, a take */
        ta = sh_trk();
        ta->p[P_SLEN] = 16;
        for (k = 0; k < 16u; k++) { ta->step[k].n = 1; ta->step[k].note[0] = 48; ta->step[k].time = ST_NOTE; ta->step[k].vel = 100; }
        sframe();
        sgo2(lz, cn);                                   /* B: LIZARD + CHORD, a take on the other part */
        tb = sh_trk();
        tb->p[P_SLEN] = 16;
        tb->step[0].n = 1; tb->step[0].note[0] = 60; tb->step[0].time = ST_NOTE; tb->step[0].vel = 100;
        sframe();
        sgo(TM_RUD);                                    /* C: DRUMS + RUDIMENT in front */
        transport_req = 1; srender(2);
        for (p = 0; p < NPART; p++)
            if (sh_part_state(p) == 1u)
                want |= 1u << p;
        for (k = 0; k < 6u; k++) {
            sframes(400);
            stap(B_HOME); stap(B_HOME);
            seen |= 1u << sh.part;
        }
        okc = want == seen && ((want >> 0) & 1u) && ((want >> 1) & 1u) && ((want >> 3) & 1u);
        sframes(400);
        sgo2(lz, cn);
        sframes(400);
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);       /* the wheel: FM6 + KEYS, REC tapped */
        skey((uint32_t)sh_slot_key(sh_mode_pos(fm))); skey((uint32_t)sh_note_key(sh_note_pos(kn)));
        stap(B_REC);
        okw = sh.mode == fm && sh.note == kn && sh_trk() == ta && !song.rec && ta->step[3].n == 1u && !sh.wheel;
        sup(B_HOME); sframe();
        sgo2(lz, cn);
        sframes(400);
        sdown(B_HOME); sframes(SH_TAP_MS + 100u);       /* again, REC held through both rings: A cleared, B kept */
        skey((uint32_t)sh_slot_key(sh_mode_pos(fm))); skey((uint32_t)sh_note_key(sh_note_pos(kn)));
        sdown(B_REC);
        sframes(2u * SH_LONG_MS + 200u);
        okw = okw && sh.mode == fm && !sh_pat_any(ta) && tb->step[0].n == 1u && !song.rec;
        sup(B_REC); sup(B_HOME); sframe(); sframe();
        okw = okw && !song.rec && !sh_wrec;
        printf("  (home walk %u: want %x seen %x; wheel rec %u)\n", okc, want, seen, okw);
        transport_req = 2; srender(4);
        shome();
        bad += check("HOME twice walks each pair playing behind in turn; REC on a pair in the wheel brings it in front, "
                     "a tap arms nothing, held through both rings it clears that pair's take only", okc && okw);
    }
    {   /* QNT CHORD: S+H on LINE plays only the tones of the chord PLAY sounds, and follows it when it changes (held
         * while nothing sounds); a PLAY part's keys round to the other PLAY part's nearest chord tone */
        uint32_t snh = note_of("S+H"), k, v, b, okm, oka = 1, okb = 1, hits_a = 0, hits_b = 0, okk;
        track_t *tl, *tp = &trk[0];
        int16_t q0[NTRK];
        transport_req = 2; srender(4); sframe();
        sgoc(slot_of("ANALOG"), snh);
        tl = sh_trk();
        for (k = 0; k < NTRK; k++) { q0[k] = trk[k].p[P_QUANT]; trk[k].p[P_QUANT] = QN_CHORD; }
        memset(qn_chord_pc, 0, sizeof qn_chord_pc);
        okm = scale_mask(tl) == SCALE_MASK[clamp(tl->p[P_SCALE], 0, 15)];   /* (no chord yet: the scale) */
        for (k = 0; k < 3u; k++) {                      /* C E G held on part 1 */
            tp->v[k].active = tp->v[k].gate = 1; tp->v[k].stage = 2; tp->v[k].note = (uint8_t)(60u + (k ? 3u * k + 1u : 0u));
        }
        qn_chord_block();
        memset(tp->v, 0, sizeof tp->v);                 /* (let go: the chord holds) */
        qn_chord_block();
        okm = okm && qn_chord_pc[SH_LINE] == (1u << 0 | 1u << 4 | 1u << 7) && !qn_chord_pc[0];
        transport_req = 1;
        for (b = 0; b < 1500u; b++) {
            srender(1);
            for (v = 0; v < NVOICE; v++)
                if (tl->v[v].active && tl->v[v].gate && tl->v[v].stage == 1u) {
                    hits_a++;
                    oka &= ((1u << 0 | 1u << 4 | 1u << 7) >> (tl->v[v].note % 12u)) & 1u;
                }
        }
        for (k = 0; k < 3u; k++) {                      /* then D F A */
            tp->v[k].active = tp->v[k].gate = 1; tp->v[k].stage = 2; tp->v[k].note = (uint8_t)(62u + (k ? 3u * k + (k == 2u) : 0u));
        }
        qn_chord_block();
        memset(tp->v, 0, sizeof tp->v);
        srender(64);
        for (b = 0; b < 1500u; b++) {
            srender(1);
            for (v = 0; v < NVOICE; v++)
                if (tl->v[v].active && tl->v[v].gate && tl->v[v].stage == 1u) {
                    hits_b++;
                    okb &= ((1u << 2 | 1u << 5 | 1u << 9) >> (tl->v[v].note % 12u)) & 1u;
                }
        }
        transport_req = 2; srender(4);
        qn_chord_pc[1] = (uint16_t)(1u << 0 | 1u << 4 | 1u << 7);   /* part 2's keys: D4 to C4 or E4, F#4 to G4 */
        okk = (kb_pitch(&trk[1], 62u - 53u, 0) == 60u || kb_pitch(&trk[1], 62u - 53u, 0) == 64u) &&
              kb_pitch(&trk[1], 66u - 53u, 0) == 67u;
        printf("  (qnt chord: mask %u, C E G %u/%u, D F A %u/%u, keys %u)\n", okm, oka, hits_a, okb, hits_b, okk);
        for (k = 0; k < NTRK; k++) trk[k].p[P_QUANT] = q0[k];
        memset(qn_chord_pc, 0, sizeof qn_chord_pc);
        shome();
        bad += check("QNT CHORD: S+H on LINE plays only the chord PLAY sounds (C E G, then D F A), held when let go; "
                     "a PLAY part's keys round to the other part's nearest chord tone",
                     okm && oka && okb && hits_a > 4u && hits_b > 4u && okk);
    }
    {   /* FLUX: S+H's locked loop changes step by step with chance FLUX and keeps the changes; FLUX 0 freezes it, P
         * turned brings the source's own loop back. ACID's FLUX moves notes along the scale as they play, never the
         * steps; FLUX 0 freezes it, PLAY from stop starts from the line as written */
        static uint8_t mem[NE_MEM_MAX] __attribute__((aligned(4)));
        const note_engine_t *e = &NE_SNH;
        ne_out_t o;
        int lp[4][16], c, k, i, okf, changed = 0, frozen = 1, back = 1, oka, moved = 0, inkey = 1, afroz = 1, kept = 1, flips;
        uint32_t b = 0;
        track_t *t;
        memset(mem, 0, sizeof mem); e->init(mem);
        e->param(mem, 0, SNH_NOISE); e->param(mem, 1, 6); e->param(mem, 2, 15); e->param(mem, 3, 100);   /* 2 LOCK */
        e->scale(mem, 0, 0xAB5);
        for (c = 0; c < 4; c++) {
            if (c == 1) e->param(mem, NE_P_LINE, 60);
            if (c == 2) e->param(mem, NE_P_LINE, 0);
            for (k = 0; k < 16; k++) {
                o.n = 0; e->step(mem, (uint32_t)k, &o);
                for (lp[c][k] = -1, i = 0; i < o.n; i++) if (o.ev[i].on) lp[c][k] = o.ev[i].note;
            }
        }
        for (k = 0; k < 16; k++) { changed += lp[1][k] != lp[0][k]; frozen &= lp[3][k] == lp[2][k]; }
        e->param(mem, 2, 14); e->param(mem, 2, 15);    /* P turned and back: the source's own loop */
        for (k = 0; k < 16; k++) {
            int n = -1;
            o.n = 0; e->step(mem, (uint32_t)k, &o);
            for (i = 0; i < o.n; i++) if (o.ev[i].on) n = o.ev[i].note;
            back &= n == lp[0][k];
        }
        okf = changed >= 3 && frozen && back;
        sgoc(slot_of("ANALOG"), note_of("ACID"));
        t = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->p[P_SLEN] = 16;
        for (k = 0; k < 16; k++) { t->step[k].n = 1; t->step[k].note[0] = 60; t->step[k].time = ST_NOTE; t->step[k].vel = 100; }
        sh_sp[SP_AFLUX - SP_FIRST] = 100;
        sframe();
        transport_req = 1;
        for (c = 0; c < 4; c++) {
            uint32_t last = 999;
            if (c == 2) { sh_sp[SP_AFLUX - SP_FIRST] = 0; sframe(); }
            for (k = 0; k < 16; ) {
                srender(1);
                if (!(b++ & 3u)) sframe();
                if (t->seq_idx != last) {
                    last = t->seq_idx;
                    srender(1);
                    lp[c][t->seq_idx % 16u] = t->mono_note ? t->mono_note : -1;
                    k++;
                }
            }
        }
        for (k = 0; k < 16; k++) {
            moved += lp[1][k] != 60;
            inkey &= lp[1][k] < 0 || ((0xAB5u >> (lp[1][k] % 12)) & 1u);
            afroz &= lp[3][k] == lp[2][k];
            kept &= t->step[k].note[0] == 60u && !t->step[k].flags;
        }
        flips = __builtin_popcount(acid_fl[0][0]) + __builtin_popcount(acid_fl[1][0]);
        transport_req = 2; srender(4); sframe();
        oka = moved >= 3 && inkey && afroz && kept && flips >= 1;
        sh_sp[SP_AFLUX - SP_FIRST] = 0;
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        printf("  (flux: S+H changed %d frozen %d back %d; ACID moved %d in key %d frozen %d steps kept %d, %d accents "
               "or slides flipped)\n", changed, frozen, back, moved, inkey, afroz, kept, flips);
        shome();
        bad += check("FLUX: S+H's locked loop changes and keeps it, FLUX 0 freezes it, P brings the source's loop back; "
                     "ACID's notes move along the scale and its ACCENTs and SLIDEs flip as they play, FLUX 0 freezes them, "
                     "the steps never change", okf && oka);
    }
    {   /* the LINE pages: S+H's LEAP keeps each move to that many degrees; CLK KICK steps S+H on BEAT's kicks only;
         * HARP's STRUM climbs the plate one string a step over SPAN while a root key is held, and stops with it;
         * the knobs come back with a project (the ACID section's tail) */
        static uint8_t mem[NE_MEM_MAX] __attribute__((aligned(4)));
        const note_engine_t *e = &NE_SNH, *h = &NE_HARP;
        ne_out_t o;
        int k, i, prev = -1, okl = 1, n, last = -1, okh = 1, ons = 0, offs = 0;
        uint32_t snh = note_of("S+H"), b, kicks = 0, sons = 0, steps = 0, idx0, okc, okp;
        uint8_t m[10];
        track_t *tl, *tb;
        memset(mem, 0, sizeof mem); e->init(mem);
        e->param(mem, 0, SNH_NOISE); e->param(mem, 1, 4); e->param(mem, 2, 15); e->param(mem, 3, 100);
        e->param(mem, NE_P_LINE + 2, 1); e->scale(mem, 0, 0xAB5);
        for (k = 0; k < 200; k++) {
            o.n = 0; e->step(mem, (uint32_t)k, &o);
            for (i = 0; i < o.n; i++)
                if (o.ev[i].on) {
                    okl &= prev < 0 || (o.ev[i].note - prev <= 2 && prev - o.ev[i].note <= 2);
                    prev = o.ev[i].note;
                }
        }
        memset(mem, 0, sizeof mem); h->init(mem);
        h->param(mem, NE_P_LINE, 1); h->param(mem, NE_P_LINE + 1, 4); h->scale(mem, 0, 0xAB5);
        o.n = 0; h->key(mem, 60 - NE_KEY_F3, 1, &o);  /* C held: the pad (PAD 90 by default) */
        okh = o.n >= 3;
        for (k = 0; k < 8; k++) {
            o.n = 0; h->step(mem, (uint32_t)k, &o);
            for (n = -1, i = 0; i < o.n; i++) if (o.ev[i].on) n = o.ev[i].note;
            okh &= n >= 0 && (k % 4 == 0 ? n < last || last < 0 : n > last);   /* (up, round SPAN 4) */
            last = n;
        }
        o.n = 0; h->key(mem, 60 - NE_KEY_F3, 0, &o);
        for (i = 0; i < o.n; i++) offs += !o.ev[i].on;
        o.n = 0; h->step(mem, 9, &o);
        for (i = 0; i < o.n; i++) ons += o.ev[i].on;
        okh = okh && offs >= 4 && !ons;                 /* (the pad's three and the strummed one off; no more) */
        transport_req = 2; srender(4); sframe();
        sgo(TM_RUD);                                    /* kicks on 1 5 9 13 on BEAT */
        tb = sh_trk();
        for (k = 0; k < NSTEP; k++) step_clear(&tb->step[k]);
        for (k = 0; k < 4; k++) skey((uint32_t)sh_white_key(4u * (uint32_t)k));
        sgoc(slot_of("ANALOG"), snh);
        tl = sh_trk();
        sh_ne_val[sh_ne_i(snh)][3] = (int16_t)(100 - sh_ne_off(sh_ne_i(snh), 3));   /* DENS 100 */
        sh_sp[SP_SCLK - SP_FIRST] = 2;                  /* CLK L1, the kicks' lane */
        transport_req = 1; srender(2);
        idx0 = tl->seq_idx;
        {
            uint32_t sl = vage, sb = vage;
            for (b = 0; b < 3000u; b++) {
                uint32_t v, was = tl->seq_idx, tl_top = sl, tb_top = sb;
                srender(1);
                steps += tl->seq_idx != was;
                for (v = 0; v < NVOICE; v++) {          /* (a voice newer than any seen: a note-on) */
                    if (tl->v[v].active && tl->v[v].age > sl) { sons++; tl_top = tl->v[v].age > tl_top ? tl->v[v].age : tl_top; }
                    if (tb->v[v].active && tb->v[v].age > sb) { kicks++; tb_top = tb->v[v].age > tb_top ? tb->v[v].age : tb_top; }
                }
                sl = tl_top; sb = tb_top;
            }
        }
        transport_req = 2; srender(4); sframe();
        (void)idx0;
        okc = steps >= 16u && kicks >= 4u && sons > 0u && sons <= kicks + 1u && sons * 2u < steps;
        sh_sp[SP_SCLK - SP_FIRST] = 0;
        sh_sp[SP_FLUX - SP_FIRST] = 37; sh_sp[SP_SLEAP - SP_FIRST] = 3; sh_sp[SP_HSPAN - SP_FIRST] = 11;
        sh_acid_get(m);
        sh_sp[SP_FLUX - SP_FIRST] = 0; sh_sp[SP_SLEAP - SP_FIRST] = 0; sh_sp[SP_HSPAN - SP_FIRST] = 8;
        sh_acid_set(m);
        okp = sh_sp[SP_FLUX - SP_FIRST] == 37 && sh_sp[SP_SLEAP - SP_FIRST] == 3 && sh_sp[SP_HSPAN - SP_FIRST] == 11;
        sh_sp[SP_FLUX - SP_FIRST] = 0; sh_sp[SP_SLEAP - SP_FIRST] = 0; sh_sp[SP_HSPAN - SP_FIRST] = 8;
        for (k = 0; k < NSTEP; k++) step_clear(&tb->step[k]);
        printf("  (line pages: LEAP %d, HARP strum %d, CLK KICK %u of %u steps (%u kick blocks), kept %u)\n",
               okl, okh, sons, steps, kicks, okp);
        shome();
        bad += check("LINE pages: S+H's LEAP 1 moves a degree at most; HARP's STRUM climbs SPAN with a root held and stops "
                     "with it; S+H's CLK KICK steps on BEAT's kicks only; the knobs ride the project's ACID section",
                     okl && okh && okc && okp);
    }
    {   /* LINES' octave pages: OCT+ and OCT- move the lanes in view, a tap puts the hit in that octave (another
         * octave's hit comes to this one, this one's goes off), the keys show the others low, and it plays there */
        track_t *t;
        uint32_t q, okx, heard = 0, n0, lv1, lv2;
        sgoc(slot_of("ANALOG"), note_of("LINES"));
        sframes(100);
        if (!sh_view_note()) {
            stap(B_HOME); sframes(400);
        }
        t = sh_trk();
        for (q = 0; q < NSTEP; q++)
            step_clear(&t->step[q]);
        t->p[P_SLEN] = 16;
        sh_sp[SP_LANE - SP_FIRST] = 0;
        sh_lopg[1] = 0;
        sframe();
        stap(B_OCTUP);
        okx = sh_lopg[1] == 1 && str_eq(sh.toast, "OCTAVE +1") && song.octave == 0;
        skey(0);
        okx = okx && (t->step[0].hit & 1u) && step_loct(&t->step[0], 0) == 1;
        lv1 = sh_inst_led(0, SHL_OFF);
        stap(B_OCTDN); stap(B_OCTDN);
        lv2 = sh_inst_led(0, SHL_OFF);
        okx = okx && sh_lopg[1] == -1 && lv1 == SHL_LIT && lv2 == SHL_LOW;
        skey(0);                                        /* (another octave's hit: here now) */
        okx = okx && (t->step[0].hit & 1u) && step_loct(&t->step[0], 0) == -1;
        skey(0);                                        /* (this octave's: off) */
        okx = okx && !(t->step[0].hit & 1u) && step_loct(&t->step[0], 0) == 0;
        stap(B_OCTUP); stap(B_OCTUP); stap(B_OCTUP);
        skey(0);                                        /* two up, and played */
        okx = okx && sh_lopg[1] == 2 && step_loct(&t->step[0], 0) == 2;
        n0 = shell_lane_note_i[1][0];
        transport_req = 1;
        for (q = 0; q < 40u && !heard; q++) {
            uint32_t v;
            srender(1);
            for (v = 0; v < NVOICE; v++)
                heard |= t->v[v].active && t->v[v].note == n0 + 24u;
        }
        transport_req = 2; srender(4); sframe();
        sh_grid_o = 0;                                  /* another lane's tap and accent leave it be; NUDGE carries it */
        sh_grid_hit(t, 0u, 3u, 1u);
        grid_acc(t, 0u, 5u, 1u);
        okx = okx && step_loct(&t->step[0], 0) == 2 && step_loct(&t->step[0], 3) == 0;
        sh_nudge(t, 0u, 1);
        okx = okx && !(t->step[0].hit & 1u) && (t->step[1].hit & 1u) && step_loct(&t->step[1], 0) == 2;
        sh_lopg[1] = 0;
        step_clear(&t->step[0]);
        step_clear(&t->step[1]);
        printf("  (octave pages: taps, keys, other lanes and NUDGE %u, heard two up %u)\n", okx, heard);
        bad += check("LINES: OCT+ and OCT- page the lanes by octaves; a tap puts the hit in the octave in view, the keys "
                     "show the others low, and it plays in its octave", okx && heard);
    }
    {   /* the lanes' keys glow as they hit, playing: a lane that hits every step stays mid, one that hits once a bar
         * blinks; the lane in front stays lit; a hit in another octave pulses OCT+, its key stays low */
        track_t *t;
        uint32_t q, k, kb[8] = {0}, busy = 0, rare = 0, front = 0, other = 0, n = 0, up = 0;
        sgoc(slot_of("ANALOG"), note_of("LINES"));
        sframes(100);
        t = sh_trk();
        for (q = 0; q < NSTEP; q++)
            step_clear(&t->step[q]);
        t->p[P_SLEN] = 16;
        for (q = 0; q < 16u; q++)
            grid_hit(t, q, 1u, 1u);                     /* lane 2: every step */
        grid_hit(t, 4u, 4u, 1u);                        /* lane 5: once a bar */
        grid_hit(t, 8u, 2u, 1u);                        /* lane 3: an octave up */
        step_set_loct(&t->step[8], 2u, 1);
        for (k = 0; k < 27u; k++)
            if (sh_key_black(k) >= 0 && sh_key_black(k) < 8)
                kb[sh_key_black(k)] = k;
        sh_sp[SP_LANE - SP_FIRST] = 0;
        sh_lopg[1] = 0;
        transport_req = 1;
        srender(2);
        for (q = 0; q < 240u; q++) {                    /* ~4 s of frames with the audio under them (a frame: 16 ms,
                                                         * five or six blocks of 2.9 ms) */
            srender(5u + (q & 1u)); sframe();
            if (q < 20u)
                continue;                               /* (the first bar's rise) */
            n++;
            busy += sh_inst_led(kb[1], SHL_OFF) == SHL_MID;
            rare += sh_inst_led(kb[4], SHL_OFF) == SHL_MID;
            other += sh_inst_led(kb[2], SHL_OFF) == SHL_MID;
            front += sh_inst_led(kb[0], SHL_OFF) == SHL_LIT;
            up += sh_lg_up && fm1_ms - sh_lg_up < 150u;
        }
        transport_req = 2; srender(4); sframe();
        for (q = 0; q < NSTEP; q++)
            step_clear(&t->step[q]);
        printf("  (lane glow over %u frames: every step %u, once a bar %u, an octave up %u (OCT+ %u), the front lit %u)\n",
               n, busy, rare, other, up, front);
        bad += check("lights: playing, the lanes' keys glow as they hit, a busy lane more than a rare one; the lane in "
                     "front stays lit; a hit an octave up pulses OCT+, not its key",
                     busy > n * 8u / 10u && rare > 0u && rare < n / 3u && !other && up > 0u && front == n);
    }
    {   /* SAVES AT STOP: PROJECTS' save asked while it plays waits for the stop, then writes; a load takes it back */
        int okq, oks, okl;
        shome();
        if (!song.playing)
            stap(B_PLAY);
        srender(4);
        sh.global = SHG_PROJECT;
        sh_proj_read();
        sh.pslot = 2;
        stap(B_OCTUP);
        stap(B_OCTUP);
        okq = song.playing && sh_psave == 3u && !memcmp(sh.toast, "SAVES TO C AT STOP", 18);
        stap(B_PLAY);
        srender(4); sframe(); sframe();
        oks = !song.playing && !sh_psave && !memcmp(sh.toast, "SAVED", 5) && ((sh_pused >> 2) & 1u);   /* (RAM) here */
        stap(B_PLAY);
        srender(4);
        sh.pslot = 2;
        stap(B_OCTUP);
        stap(B_OCTUP);                                  /* asked again, */
        sh.pslot = 2;
        stap(B_OCTDN);
        stap(B_OCTUP);                                  /* then a load: no save at the stop */
        okl = !sh_psave;
        if (song.playing)
            stap(B_PLAY);
        srender(4); sframe();
        bad += check("PROJECTS: a save asked while playing says SAVES TO C AT STOP and writes at the stop; a load drops it",
                     okq && oks && okl);
        shome();
    }

    {   /* HARMONY goes with the project: each NOTE engine's own, through a save and a load (tag 14); the default
         * (all OFF) writes no tag */
        uint32_t sn = note_of("S+H"), kn = note_of("KEYS");
        uint8_t m[P11_HARM_N], d[P11_HARM_N];
        if (song.playing)
            stap(B_PLAY);
        memset(sh_hm, 0, sizeof sh_hm);
        for (k2 = 0; k2 < SH_NNOTES; k2++)
            sh_hm[k2][3] = 100;
        sh_harm_get(m);
        proj_harm_def(d);
        ok = !memcmp(m, d, sizeof d);
        sh_hm[sn][0] = HM_7TH; sh_hm[sn][1] = HV_DROP2; sh_hm[sn][2] = 1; sh_hm[sn][3] = 77;
        sh_hm[kn][0] = HM_OCT;
        scale_user = 0x891;                             /* (and SCALE USER's notes, tag 15) */
        ok = ok && project_save(2) == 0;
        memset(sh_hm, 0, sizeof sh_hm);
        scale_user = SCALE_USER_DEF;
        project_load(2);
        ok = ok && scale_user == 0x891;
        scale_user = SCALE_USER_DEF;
        ok = ok && sh_hm[sn][0] == HM_7TH && sh_hm[sn][1] == HV_DROP2 && sh_hm[sn][2] == 1 && sh_hm[sn][3] == 77 &&
             sh_hm[kn][0] == HM_OCT && sh_hm[kn][3] == 100 && !sh_hm[note_of("ACID")][0];
        bad += check("HARMONY: each NOTE engine's own (and SCALE USER's notes) go through a project save and load", ok);
        memset(sh_hm, 0, sizeof sh_hm);
        for (k2 = 0; k2 < SH_NNOTES; k2++)
            sh_hm[k2][3] = 100;
    }
    printf(bad ? "shell: %d FAILED\n" : "shell: all passed\n", bad);
    return bad != 0;
}
