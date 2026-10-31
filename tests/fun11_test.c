/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* FUN11, SQRL-1's project format (firmware/src/project.c proj_pack11, proj_unpack11), on the shell's host build.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/fun11_test tests/fun11_test.c -lm
 *   build/host/fun11_test [build/host/fun10_stock.bin]        (the stock FUN10 from tests/mk_fun10.c)
 * 1. the sizes; 2. random projects (six notes and JUST a step, ties, hits, chances, ratchets, nudges, motion, FM6
 * patches, a name, MIDI IN TRACK, the pairing) round-trip field for field, and pack again byte for byte; 3. a project
 * too full is refused, nothing written; 4. a stock FUN10 loads (its four notes, no JUST) and saves as FUN11; 5. a
 * malformed FUN11 (n 7, a version, a byte after the stream, a hash, any byte changed) is refused or loads valid,
 * never read past (ASan build in run_tests.sh) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static volatile uint32_t audio_max_us;
static uint32_t shed_count;
#include "../firmware/src/shell.c"

static uint32_t rnd = 12345u;
static uint32_t rn(uint32_t n) { rnd = rnd * 1664525u + 1013904223u; return (rnd >> 8) % n; }

static void rand_step(step_t *s, uint32_t full)
{
    uint32_t j;
    memset(s, 0, sizeof *s);
    s->time = (uint8_t)(full ? ST_NOTE : rn(3));
    if (!full && rn(3) == 0u) {                         /* (a third of them empty) */
        s->time = ST_REST;
        return;
    }
    s->n = (uint8_t)(full ? STEP_NOTES : rn(STEP_NOTES + 1u));
    for (j = 0; j < s->n; j++) {
        s->note[j] = (uint8_t)rn(128);
        step_set_just(s, j, (int32_t)rn(16) - 8);
    }
    s->flags = (uint8_t)rn(4);
    s->vel = (uint8_t)rn(128);
    if (full || rn(2)) {
        s->hit = (uint8_t)rn(256);
        s->acc = (uint8_t)(rn(256) & s->hit);
    }
    if (full || rn(2))
        s->probability = (uint8_t)rn(COND_FIRST + NCOND);
    step_set_ratchet(s, full ? 4u : rn(4) + 1u);
    step_set_nudge(s, full ? 7 : (int32_t)rn(16) - 8);
}

static void rand_project(project_t *q, uint32_t full)
{
    uint32_t t, i, id = 0, n;
    project_capture(q);
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < NSTEP; i++)
            rand_step(&q->t[t].step[i], full);
        if (full || rn(2))
            for (i = 0; i < FM6_PACKED; i++)
                q->fm6[t][i] = (uint8_t)rn(128);
        else
            memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
        q->mch[t] = (uint8_t)rn(MCH_N);
    }
    while (id < P_COUNT && !motion_param(id))           /* (a parameter motion may move) */
        id++;
    n = full ? MOTION_MAX : rn(MOTION_MAX + 1u);
    q->motion.count = (uint8_t)n;
    q->motion.on = (uint8_t)rn(16);
    for (i = 0; i < n; i++) {                           /* (each place once: no two alike) */
        q->motion.event[i].place = (uint8_t)i;
        q->motion.event[i].param = (uint8_t)id;
        q->motion.event[i].value = (int8_t)((int32_t)rn(192) - 64);
    }
    memset(q->name, 0, sizeof q->name);
    for (i = 0; i < rn(PROJ_NAME_LEN + 1u); i++)
        q->name[i] = (char)('A' + rn(26));
    for (i = 0; i < sizeof q->pair; i++)
        q->pair[i] = (uint8_t)rn(256);
    memcpy(q->lanes, P11_LANES_DEF, sizeof q->lanes);   /* RUDIMENT's lanes: the default, or a quarter of the time not */
    if (!rn(4))
        for (i = 0; i < 8u; i++) {
            q->lanes[i] = (uint8_t)(1u + rn(32));
            q->lanes[8u + i] = (uint8_t)rn(q->lanes[i] + 1u);
            q->lanes[16u + i] = (uint8_t)rn(32);
            q->lanes[24u + i] = (uint8_t)(rn(2) ? 0u : rn(128));
            q->lanes[32] = (uint8_t)rn(5);
        }
    proj_iext_def(q);                                   /* the INSERT extras (tag 5): the defaults, or a part's not */
    if (!rn(4)) {
        uint32_t pp = rn(NTRK);
        q->iext[pp][0] = (uint8_t)rn(128); q->iext[pp][1] = (uint8_t)rn(128); q->iext[pp][2] = (uint8_t)rn(128);
    }
    memset(q->drl, 64, sizeof q->drl);                  /* the DRUM parts' sound per drum (tag 7): as designed, or */
    if (!rn(4)) {                                       /* a quarter of the time one part's not */
        uint32_t pp = rn(NTRK);
        for (i = 0; i < sizeof q->drl[0]; i++)
            q->drl[pp][i] = (uint8_t)(rn(3) ? 64u : rn(128));
    }
    memcpy(q->lines, P11_LANES_DEF, sizeof q->lines);   /* LINES' (tag 8): the same */
    if (!rn(4))
        for (i = 0; i < 8u; i++) {
            q->lines[i] = (uint8_t)(1u + rn(32));
            q->lines[8u + i] = (uint8_t)rn(q->lines[i] + 1u);
            q->lines[16u + i] = (uint8_t)rn(32);
            q->lines[24u + i] = (uint8_t)(rn(2) ? 0u : rn(128));
            q->lines[32] = (uint8_t)rn(5);
        }
    proj_tracks_def(q);                                 /* the tracks (tag 9): none, or a quarter of the time some */
    if (!rn(4)) {
        q->tracks[0] = (uint8_t)rn(128); q->tracks[1] = (uint8_t)rn(128);
        q->tracks[2] = (uint8_t)rn(128); q->tracks[3] = (uint8_t)rn(128); q->tracks[4] = (uint8_t)rn(16);
    }
    if (!rn(4)) {                                       /* RUDIMENT's MAP (tag 10), a quarter of the time */
        q->map[0] = (uint8_t)rn(2);
        for (i = 1; i < sizeof q->map; i++)
            q->map[i] = (uint8_t)rn(128);
    }
    if (!rn(4)) {                                       /* CHORD's PROG (tag 11), a quarter of the time */
        q->prog[0] = (uint8_t)rn(10); q->prog[1] = (uint8_t)rn(4);
        q->prog[2] = (uint8_t)rn(5); q->prog[3] = (uint8_t)rn(13);
        q->acid[0] = (uint8_t)rn(100); q->acid[1] = (uint8_t)rn(64);
        q->acid[2] = (uint8_t)(57 + rn(15)); q->acid[3] = (uint8_t)(62 + rn(5));
    }
    q->sum = proj_sum(q);
}

static int same_step(const step_t *a, const step_t *b)
{
    uint32_t j;
    if (a->n != b->n || a->time != b->time || a->flags != b->flags || a->vel != b->vel || a->hit != b->hit ||
        a->acc != b->acc || a->probability != b->probability)
        return 0;
    for (j = 0; j < a->n; j++)
        if (a->note[j] != b->note[j] || step_just(a, j) != step_just(b, j))
            return 0;
    return 1;
}

static int same_project(const project_t *a, const project_t *b)
{
    uint32_t t, i;
    if (memcmp(a->g, b->g, sizeof a->g) || a->sel != b->sel || a->parts != b->parts || a->phys != b->phys ||
        memcmp(&a->chain, &b->chain, sizeof a->chain) || a->motion.count != b->motion.count ||
        a->motion.on != b->motion.on || memcmp(a->fm6, b->fm6, sizeof a->fm6) || memcmp(a->mch, b->mch, sizeof a->mch) ||
        memcmp(a->name, b->name, sizeof a->name) || memcmp(a->pair, b->pair, sizeof a->pair) ||
        memcmp(a->lanes, b->lanes, sizeof a->lanes) || memcmp(a->lines, b->lines, sizeof a->lines) ||
        memcmp(a->drl, b->drl, sizeof a->drl) || memcmp(a->iext, b->iext, sizeof a->iext) ||
        memcmp(a->tracks, b->tracks, sizeof a->tracks) || memcmp(a->map, b->map, sizeof a->map))
        return 0;
    for (i = 0; i < a->motion.count; i++)
        if (memcmp(&a->motion.event[i], &b->motion.event[i], sizeof a->motion.event[i]))
            return 0;
    for (t = 0; t < NTRK; t++) {
        if (memcmp(a->t[t].p, b->t[t].p, sizeof a->t[t].p) || a->t[t].engine != b->t[t].engine ||
            a->t[t].preset != b->t[t].preset)
            return 0;
        for (i = 0; i < NSTEP; i++)
            if (!same_step(&a->t[t].step[i], &b->t[t].step[i]))
                return 0;
    }
    return 1;
}

static uint32_t stream_end(const uint8_t *b)            /* where a packed FUN11's stream ends (zeros after it) */
{
    uint32_t e = P11_PAIR_OFF;
    while (e > 68u && !b[e - 1u])
        e--;
    return e;
}

static void rehash(uint8_t *b)
{
    uint32_t h = proj_hash(b, PROJ_STORE_SIZE - 4u);
    memcpy(b + PROJ_STORE_SIZE - 4u, &h, 4);
}

int main(int argc, char **argv)
{
    static project_t a, b2, c;
    static project_store_t s1, s2, keep;
    int bad = 0;
    uint32_t k, ok, packed = 0, full = 0, maxlen = 0;
    ui_power_on();
    memset(&sh, 0, sizeof sh);
    shell_input();
    shell_draw();

    bad += check("FUN11: step_t 16 bytes, track_t a multiple of 16, a project one storage object",
                 sizeof(step_t) == 16u && sizeof(track_t) % 16u == 0 && sizeof(project_store_t) == 3840u);

    for (ok = 1, k = 0; k < 400u; k++) {                /* 2: random projects */
        rand_project(&a, 0);
        if (!proj_pack(&s1, &a)) {
            full += proj_too_full();
            ok = ok && proj_too_full();                 /* (refused only for room) */
            continue;
        }
        packed++;
        maxlen = stream_end(s1.raw) > maxlen ? stream_end(s1.raw) : maxlen;
        ok = ok && ((uint32_t *)s1.raw)[0] == PROJ_MAGIC_11;
        ok = ok && proj_import(&b2, &s1, sizeof s1) && same_project(&a, &b2);
        ok = ok && proj_pack(&s2, &b2) && !memcmp(&s1, &s2, sizeof s1);
    }
    printf("  (%u random projects packed, %u too full; the longest stream ends at %u of %u)\n", packed, full, maxlen,
           (unsigned)P11_PAIR_OFF);
    bad += check("FUN11: random projects round-trip field for field and pack again byte for byte", ok && packed > 300u);

    {   /* 2b: the tail at its worst: tags 6 and 8 (41 bytes each, the lanes' DIV too) and a tag 7 for every part cost
         * 226 bytes; how many projects
         * that pack with the default tail no longer fit with it */
        uint32_t e0, d, dmax = 0, n0 = 0, lost = 0, i;
        for (ok = 1, k = 0; k < 400u; k++) {
            rand_project(&a, 0);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes);
            memcpy(a.lines, P11_LANES_DEF, sizeof a.lines);
            memset(a.drl, 64, sizeof a.drl);
            a.sum = proj_sum(&a);
            if (!proj_pack(&s1, &a))
                continue;
            n0++;
            e0 = stream_end(s1.raw);
            for (i = 0; i < sizeof a.lanes; i++)
                a.lanes[i] = a.lines[i] = (uint8_t)(i < 8u ? 32u : i < 16u ? 31u : i < 24u ? 7u : i < 32u ? 100u : 4u);
            memset(a.drl, 127, sizeof a.drl);
            a.sum = proj_sum(&a);
            if (!proj_pack(&s2, &a)) {
                lost++;
                ok = ok && proj_too_full();
                continue;
            }
            d = stream_end(s2.raw) - e0;
            dmax = d > dmax ? d : dmax;
            ok = ok && proj_import(&b2, &s2, sizeof s2) && same_project(&a, &b2);
        }
        printf("  (the full tail: +%u bytes at most; %u of %u projects that fit without it no longer do)\n", dmax, lost,
               n0);
        bad += check("FUN11: the tail at its worst (tags 6, 8 and four 7s) costs at most 358 bytes and round-trips",
                     ok && n0 > 300u && dmax <= 359u);   /* (358, + 1: stream_end drops a stream's last zero; tags 6 and
                                                          * 8 of SH_LANES_N each) */
    }

    {   /* 3: too full: refused, PROJECT TOO FULL, the slot as it was */
        rand_project(&a, 0);
        ok = proj_pack(&s1, &a);
        memcpy(&proj_slot[0], &s1, sizeof s1);
        memcpy(&keep, &proj_slot[0], sizeof keep);
        rand_project(&a, 1);
        ok = ok && !proj_pack(&s2, &a) && proj_too_full();
        for (k = 0; k < NTRK; k++)
            memcpy(trk[k].step, a.t[k].step, sizeof trk[k].step);
        motion = a.motion;
        for (k = 0; k < NTRK; k++)
            fm6_unpack(a.fm6[k], fm6_patch[k]);
        ok = ok && project_save(0) == 2 && !memcmp(&keep, &proj_slot[0], sizeof keep);
        bad += check("FUN11: a project too full is refused (PROJECT TOO FULL), the slot left as it was", ok);
        motion.count = 0;
        for (k = 0; k < NTRK; k++)
            for (ok = 0; ok < NSTEP; ok++)
                step_clear(&trk[k].step[ok]);
    }

    if (argc > 1) {                                     /* 4: a stock FUN10 */
        FILE *f = fopen(argv[1], "rb");
        ok = f && fread(&s1, 1, sizeof s1, f) == sizeof s1;
        if (f)
            fclose(f);
        ok = ok && ((uint32_t *)s1.raw)[0] == PROJ_MAGIC && proj_import(&a, &s1, sizeof s1);
        ok = ok && a.t[0].step[0].n == 4u && a.t[0].step[0].note[3] == 70u && step_just(&a.t[0].step[0], 3) == 0 &&
             a.t[0].step[1].note[0] == 72u && step_ratchet(&a.t[0].step[1]) == 3u && step_nudge(&a.t[0].step[1]) == -3;
        ok = ok && proj_pack(&s2, &a) && ((uint32_t *)s2.raw)[0] == PROJ_MAGIC_11 && proj_import(&b2, &s2, sizeof s2) &&
             same_project(&a, &b2);
        memcpy(&proj_slot[1], &s1, sizeof s1);          /* (the slot shows it, as SQRL-1 showed EMPTY before) */
        ok = ok && project_used(1);
        bad += check("FUN11: a stock FUN10 loads (four notes, no JUST, RATCH, NUDGE) and saves as FUN11", ok);
        {   /* the formats before FUN11 hold no lanes: they load the default ones (LEN 16, not a cleared 0 = 1) */
            project_v6_t v6;                            /* a FUN6 record (memset to 0 whole, as the old paths start) */
            uint32_t i, l;
            ok = !memcmp(a.lanes, P11_LANES_DEF, sizeof a.lanes) && s2.raw[67] == 1u;
            memset(&v6, 0, sizeof v6);
            v6.magic = PROJ_MAGIC_V6; v6.size = sizeof v6; v6.parts = NPART; v6.phys = PROJ_PHYS;
            memcpy(v6.g, a.g, sizeof v6.g);
            for (l = 0; l < NTRK; l++)
                for (i = 0; i < 69u; i++)
                    v6.t[l].p[i] = TP[i < 61u ? i : i - 61u + P_E0].def;
            v6.sum = proj_hash(&v6, sizeof v6 - 4u);
            memset(&c, 0x55, sizeof c);
            ok = ok && proj_import(&c, &v6, sizeof v6) && !memcmp(c.lanes, P11_LANES_DEF, sizeof c.lanes) &&
                 c.sum == proj_sum(&c);
            for (l = 0; l < 8u; l++) {                  /* a beat's lanes live, then the FUN10 slot loaded */
                sh_eu[0][l] = 5; sh_eu[1][l] = 2; shell_lane_len[l] = 5;
            }
            project_load(1);
            for (l = 0; l < 8u; l++)
                ok = ok && sh_eu[0][l] == 0 && sh_eu[1][l] == 0 && sh_eu[2][l] == 0 && shell_lane_len[l] == 0u;
            bad += check("FUN11: FUN10 and FUN6 load RUDIMENT's lanes at their default (=LEN, 0, 0), not cleared", ok);
        }
    } else
        printf("  (no stock FUN10 given: the migration case skipped)\n");

    {   /* 5: malformed (from a version 1: the lanes at their default, no tail) */
        rand_project(&a, 0);
        memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        while (!proj_pack(&s1, &a)) {
            rand_project(&a, 0);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        }
        memcpy(&s2, &s1, sizeof s2);
        s2.raw[67] = 3;
        rehash(s2.raw);
        ok = !proj_import(&c, &s2, sizeof s2);                       /* a version it does not know */
        memcpy(&s2, &s1, sizeof s2);
        s2.raw[67] = P11_VER_TAIL;                                   /* version 2, an empty tail (tag 0) */
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && same_project(&a, &c);
        {   /* version 2 with tags this version does not know: skipped, the project loads as it is */
            uint32_t e = stream_end(s1.raw), room = P11_PAIR_OFF - e;
            memcpy(&s2, &s1, sizeof s2);
            s2.raw[67] = P11_VER_TAIL;
            s2.raw[e] = 200; s2.raw[e + 1u] = 3; s2.raw[e + 2u] = 0x55; s2.raw[e + 3u] = 0xFF; s2.raw[e + 4u] = 0;
            s2.raw[e + 5u] = 1; s2.raw[e + 6u] = 0;                  /* (a tag of length 0) */
            s2.raw[e + 7u] = 0;
            rehash(s2.raw);
            ok = ok && proj_import(&c, &s2, sizeof s2) && same_project(&a, &c);
            ok = ok && proj_pack(&s2, &c) && !memcmp(&s2, &s1, sizeof s2);   /* saved here: version 1, no tail */
            memcpy(&s2, &s1, sizeof s2);                             /* a byte after the tail's end */
            s2.raw[67] = P11_VER_TAIL;
            s2.raw[e] = 9; s2.raw[e + 1u] = 1; s2.raw[e + 2u] = 0; s2.raw[e + 3u] = 0; s2.raw[e + 4u] = 1;
            rehash(s2.raw);
            ok = ok && !proj_import(&c, &s2, sizeof s2);
            memcpy(&s2, &s1, sizeof s2);                             /* no tag 0: tags to the very end */
            s2.raw[67] = P11_VER_TAIL;
            memset(s2.raw + e, 1, room);
            rehash(s2.raw);
            ok = ok && !proj_import(&c, &s2, sizeof s2);
            memcpy(&s2, &s1, sizeof s2);                             /* a tag's length runs past the end */
            s2.raw[67] = P11_VER_TAIL;
            s2.raw[e] = 1; s2.raw[e + 1u] = (uint8_t)(room - 2u);
            rehash(s2.raw);
            ok = ok && (room - 2u > 255u || !proj_import(&c, &s2, sizeof s2));
            memcpy(&s2, &s1, sizeof s2);                             /* .. and one that ends just before it */
            s2.raw[67] = P11_VER_TAIL;
            s2.raw[e] = 1; s2.raw[e + 1u] = (uint8_t)(room - 3u);
            rehash(s2.raw);
            ok = ok && (room - 3u > 255u || (proj_import(&c, &s2, sizeof s2) && same_project(&a, &c)));
            for (k = 0; k < 4000u; k++) {                            /* random tails: refused, or the project as it is */
                uint32_t at = e, end = e + rn(room + 1u);
                memcpy(&s2, &s1, sizeof s2);
                s2.raw[67] = P11_VER_TAIL;
                while (at < end && at < P11_PAIR_OFF) {
                    uint32_t len = rn(4u) ? rn(8u) : rn(256u);
                    s2.raw[at++] = (uint8_t)(rn(8u) ? 1u + rn(255u) : 0u);
                    if (at < P11_PAIR_OFF) s2.raw[at++] = (uint8_t)len;
                    while (len-- && at < P11_PAIR_OFF) s2.raw[at++] = (uint8_t)rn(256u);
                }
                if (rn(2u) && at < P11_PAIR_OFF) s2.raw[at] = 0;
                if (rn(8u) == 0u) s2.raw[e + rn(room)] = (uint8_t)rn(256u);
                rehash(s2.raw);
                if (proj_import(&c, &s2, sizeof s2)) {   /* (a known tag among them, 5 to 10, sets what it holds:
                                                          * all else as it was) */
                    memcpy(c.lanes, a.lanes, sizeof c.lanes);
                    memcpy(c.lines, a.lines, sizeof c.lines);
                    memcpy(c.tracks, a.tracks, sizeof c.tracks);
                    memcpy(c.map, a.map, sizeof c.map);
                    memcpy(c.prog, a.prog, sizeof c.prog);
                    memcpy(c.acid, a.acid, sizeof c.acid);
                    memcpy(c.drl, a.drl, sizeof c.drl);
                    memcpy(c.iext, a.iext, sizeof c.iext);
                    ok = ok && same_project(&a, &c);
                }
            }
        }
        memcpy(&s2, &s1, sizeof s2);
        s2.raw[100] ^= 1u;
        ok = ok && !proj_import(&c, &s2, sizeof s2);                 /* the hash */
        memcpy(&s2, &s1, sizeof s2);
        s2.raw[stream_end(s2.raw) + 1u] = 1u;
        rehash(s2.raw);
        ok = ok && !proj_import(&c, &s2, sizeof s2);                 /* a byte after the stream */
        {   /* n 7: the first step record of track 1 (after its parameters, the chain, the motion, the bitmap) */
            project_t one;
            memcpy(&one, &a, sizeof one);
            one.motion.count = 0;
            for (k = 0; k < NSTEP; k++)
                memset(&one.t[0].step[k], 0, sizeof one.t[0].step[k]), one.t[0].step[k].time = ST_REST;
            one.t[0].step[0].n = 1; one.t[0].step[0].time = ST_NOTE; one.t[0].step[0].note[0] = 60;
            ok = ok && proj_pack(&s2, &one);
            k = 68u + NTRK * (P_COUNT + 2u) + sizeof one.chain + 2u + NSTEP / 8u;
            ok = ok && s2.raw[k] == (1u | ST_NOTE << 3);
            s2.raw[k] = (uint8_t)(7u | ST_NOTE << 3);
            rehash(s2.raw);
            ok = ok && !proj_import(&c, &s2, sizeof s2);
        }
        for (k = 0; k < 3000u && ok; k++) {             /* any byte of the stream: refused, or a valid project */
            uint32_t at = 68u + rn(stream_end(s1.raw) - 68u);
            memcpy(&s2, &s1, sizeof s2);
            s2.raw[at] = (uint8_t)rn(256);
            rehash(s2.raw);
            if (proj_import(&c, &s2, sizeof s2))
                ok = proj_pack(&s2, &c) || proj_too_full();
        }
        bad += check("FUN11: a malformed project (version, hash, past the stream, n 7, any byte) is refused or loads valid; a version 2 tail is skipped",
                     ok);
    }

    {   /* 6: RUDIMENT's lanes, FUN11's tag 6: LEN[8] FILL[8] ROT[8] NOTE[8] OCT DIV[8] (41 bytes; 33 before DIV), TRN
         * STYLE, LANE 2's PAD LOOP WAKE-1 NAP [8] and LOOP's seeds [8 x 4] (SH_LANES_N) */
        static uint8_t L[SH_LANES_N] = {12, 16, 5, 16, 16, 16, 16, 9,  4, 3, 2, 0, 0, 0, 0, 1,  0, 2, 1, 0, 0, 0, 0, 8,
                                        0, 50, 0, 0, 62, 0, 0, 0,  3,  0, 0, 0, 2, 0, 0, 0, 0xFF,  12, 2,
                                        3, 0, 0, 0, 0, 0, 0, 1,  0, 2, 0, 0, 0, 0, 0, 4,  0, 0, 2, 0, 0, 0, 0, 7,
                                        0, 0, 1, 0, 0, 0, 0, 8,  [75] = 0x11, 0x22, 0x33, 0x44, [103] = 0xAA, 0xBB, 0xCC, 0xDD};
        uint32_t e, n = sizeof L;
        rand_project(&a, 0);
        memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        while (!proj_pack(&s1, &a)) {
            rand_project(&a, 0);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        }
        ok = s1.raw[67] == 1u && sizeof a.lanes == n;                /* the default lanes: version 1, no tail */
        memcpy(a.lanes, L, n);
        ok = ok && proj_pack(&s2, &a) && s2.raw[67] == P11_VER_TAIL && proj_import(&c, &s2, sizeof s2) &&
             same_project(&a, &c) && proj_pack(&keep, &c) && !memcmp(&keep, &s2, sizeof keep);
        e = stream_end(s1.raw);                                      /* the same stream, then the tail */
        ok = ok && !memcmp(s1.raw + 68, s2.raw + 68, e - 68u) && s2.raw[e] == P11_TAG_LANES && s2.raw[e + 1u] == n &&
             !memcmp(s2.raw + e + 2u, L, n) && s2.raw[e + 2u + n] == 0u;
        memcpy(&s2, &s1, sizeof s2);                                 /* a later version's tag 6, 8 longer: its first
                                                                      * SH_LANES_N taken */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_LANES; s2.raw[e + 1u] = (uint8_t)(n + 8u);
        memcpy(s2.raw + e + 2u, L, n);
        memset(s2.raw + e + 2u + n, 0x33, 8);
        s2.raw[e + 10u + n] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && !memcmp(c.lanes, L, n);
        memcpy(&s2, &s1, sizeof s2);                                 /* a round 4 firmware's 41 (no TRN, no LANE 2): */
        s2.raw[67] = P11_VER_TAIL;                                   /* the rest default */
        s2.raw[e] = P11_TAG_LANES; s2.raw[e + 1u] = 41u;
        memcpy(s2.raw + e + 2u, L, 41);
        s2.raw[e + 43u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && !memcmp(c.lanes, L, 41) &&
             !memcmp(c.lanes + 41, P11_LANES_DEF + 41, n - 41u);
        memcpy(&s2, &s1, sizeof s2);                                 /* an older firmware's 33 (no DIV): every lane x1 */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_LANES; s2.raw[e + 1u] = 33u;
        memcpy(s2.raw + e + 2u, L, 33);
        s2.raw[e + 35u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && !memcmp(c.lanes, L, 33) &&
             !memcmp(c.lanes + 33, P11_LANES_DEF + 33, n - 33u);
        memcpy(&s2, &s1, sizeof s2);                                 /* the 24 of LEN FILL ROT alone: NOTE and OCT
                                                                      * default */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_LANES; s2.raw[e + 1u] = 24u;
        memcpy(s2.raw + e + 2u, L, 24);
        s2.raw[e + 26u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && !memcmp(c.lanes, L, 24) &&
             !memcmp(c.lanes + 24, P11_LANES_DEF + 24, n - 24u);
        memcpy(&s2, &s1, sizeof s2);                                 /* a short one, 10 bytes: the rest default */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_LANES; s2.raw[e + 1u] = 10u;
        memcpy(s2.raw + e + 2u, L, 10);
        s2.raw[e + 12u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && !memcmp(c.lanes, L, 10) &&
             !memcmp(c.lanes + 10, P11_LANES_DEF + 10, n - 10u);
        {   /* the shell's own: a lane's LEN and NOTE and the OCT set, the music saved to a slot, reset, loaded */
            sh_eu[0][3] = 12; sh_eu[1][3] = 5; sh_eu[2][3] = 2; shell_lane_len[3] = 12;
            sh_lnote[2] = 64; sh_sp[SP_LOCT - SP_FIRST] = -1; sh_ediv[3] = 2;   /* (DIV 2/3) */
            ok = ok && project_save(2) == 0;
            sh_eu[0][3] = 16; sh_eu[1][3] = 0; sh_eu[2][3] = 0; shell_lane_len[3] = 16;
            sh_lnote[2] = 0; sh_sp[SP_LOCT - SP_FIRST] = 0; sh_ediv[3] = 0;
            project_load(2);
            ok = ok && sh_eu[0][3] == 12 && sh_eu[1][3] == 5 && sh_eu[2][3] == 2 && shell_lane_len[3] == 12u &&
                 sh_lnote[2] == 64 && sh_sp[SP_LOCT - SP_FIRST] == -1 && sh_ediv[3] == 2;
            sh_eu[0][3] = 16; sh_eu[1][3] = 0; sh_eu[2][3] = 0; shell_lane_len[3] = 16;
            sh_lnote[2] = 0; sh_sp[SP_LOCT - SP_FIRST] = 0; sh_ediv[3] = 0;
        }
        {   /* LINES' lanes: tag 8, tag 6's layout; alone, or after RUDIMENT's tag 6 */
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
            memcpy(a.lines, L, n);
            ok = ok && proj_pack(&s2, &a) && s2.raw[67] == P11_VER_TAIL && s2.raw[e] == P11_TAG_LINES &&
                 s2.raw[e + 1u] == n && !memcmp(s2.raw + e + 2u, L, n) && s2.raw[e + 2u + n] == 0u &&
                 proj_import(&c, &s2, sizeof s2) && same_project(&a, &c) && !memcmp(c.lanes, P11_LANES_DEF, n);
            memcpy(a.lanes, L, n);
            a.lines[0] = 7;
            ok = ok && proj_pack(&s2, &a) && s2.raw[e] == P11_TAG_LANES && s2.raw[e + 2u + n] == P11_TAG_LINES &&
                 proj_import(&c, &s2, sizeof s2) && same_project(&a, &c) && c.lines[0] == 7u &&
                 proj_pack(&keep, &c) && !memcmp(&keep, &s2, sizeof keep);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines); memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        }
        bad += check("FUN11: RUDIMENT's lanes in tag 6 (default: version 1, as before; longer: SH_LANES_N taken; 41, 33, 24 "
                     "or 10: the rest default; saved and loaded with a project, NOTE, OCT and DIV too)", ok);
    }

    {   /* 7: a DRUM part's sound per drum, FUN11's tag 7 (the part, then 32 bytes + 64), once per part not all 64 */
        uint32_t e, k;
        rand_project(&a, 0);
        memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines);
        memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        while (!proj_pack(&s1, &a)) {
            rand_project(&a, 0);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines);
            memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        }
        ok = s1.raw[67] == 1u;                                       /* all as designed: no tag 7 */
        a.drl[2][9] = 100; a.drl[2][24] = 10;                       /* part 3: the snare's DECY up, the kick's SNAP down */
        e = stream_end(s1.raw);
        ok = ok && proj_pack(&s2, &a) && s2.raw[67] == P11_VER_TAIL && s2.raw[e] == P11_TAG_DRL && s2.raw[e + 1u] == 33u &&
             s2.raw[e + 2u] == 2u && s2.raw[e + 35u] == 0u && proj_import(&c, &s2, sizeof s2) && same_project(&a, &c);
        memcpy(&s2, &s1, sizeof s2);                                 /* a part past NTRK (a later firmware's): skipped */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_DRL; s2.raw[e + 1u] = 3u; s2.raw[e + 2u] = NTRK; s2.raw[e + 3u] = 1; s2.raw[e + 4u] = 1;
        s2.raw[e + 5u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2);
        for (k = 0; k < sizeof c.drl; k++)
            ok = ok && ((const uint8_t *)c.drl)[k] == 64u;
        {   /* the shell's own: a part's drum set, saved to a slot, cleared, loaded back */
            drum_lane_d[3][DRL_TUNE][DV_SNARE] = 20; drum_lane_d[3][DRL_SNAP][DV_HATC] = -30;
            ok = ok && project_save(3) == 0;
            memset(drum_lane_d, 0, sizeof drum_lane_d);
            project_load(3);
            ok = ok && drum_lane_d[3][DRL_TUNE][DV_SNARE] == 20 && drum_lane_d[3][DRL_SNAP][DV_HATC] == -30 &&
                 drum_lane_d[0][DRL_TUNE][DV_SNARE] == 0;
            memset(drum_lane_d, 0, sizeof drum_lane_d);
        }
        bad += check("FUN11: a DRUM part's sound per drum in tag 7 (all designed: none; per part; a part past NTRK skipped; "
                     "saved and loaded with a project)", ok);
    }

    {   /* 8: FILTER's and COMB's extras, FUN11's tag 5 (the part, then DRV KTRK ENV + 64), once per part not (0, 0, 64) */
        uint32_t e;
        rand_project(&a, 0);
        memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines);
        memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        while (!proj_pack(&s1, &a)) {
            rand_project(&a, 0);
            memcpy(a.lanes, P11_LANES_DEF, sizeof a.lanes); memcpy(a.lines, P11_LANES_DEF, sizeof a.lines);
            memset(a.drl, 64, sizeof a.drl); proj_iext_def(&a); proj_tracks_def(&a);
        }
        ok = s1.raw[67] == 1u;                                       /* the defaults: no tag 5 */
        a.iext[1][0] = 90; a.iext[1][2] = 20;                       /* part 2: DRV up, ENV down */
        e = stream_end(s1.raw);
        ok = ok && proj_pack(&s2, &a) && s2.raw[67] == P11_VER_TAIL && s2.raw[e] == P11_TAG_INS && s2.raw[e + 1u] == 4u &&
             s2.raw[e + 2u] == 1u && s2.raw[e + 3u] == 90u && s2.raw[e + 5u] == 20u && s2.raw[e + 6u] == 0u &&
             proj_import(&c, &s2, sizeof s2) && same_project(&a, &c);
        memcpy(&s2, &s1, sizeof s2);                                 /* a part past NTRK: skipped */
        s2.raw[67] = P11_VER_TAIL;
        s2.raw[e] = P11_TAG_INS; s2.raw[e + 1u] = 4u; s2.raw[e + 2u] = NTRK; s2.raw[e + 3u] = 1; s2.raw[e + 6u] = 0;
        rehash(s2.raw);
        ok = ok && proj_import(&c, &s2, sizeof s2) && c.iext[0][0] == 0u && c.iext[3][2] == 64u;
        {   /* the shell's own: a part's extras set, saved to a slot, cleared, loaded back */
            ins_ext[2][IX_DRV] = 70; ins_ext[2][IX_ENV] = -30;
            ok = ok && project_save(1) == 0;
            memset(ins_ext, 0, sizeof ins_ext);
            project_load(1);
            ok = ok && ins_ext[2][IX_DRV] == 70 && ins_ext[2][IX_ENV] == -30 && ins_ext[0][IX_DRV] == 0;
            memset(ins_ext, 0, sizeof ins_ext);
        }
        bad += check("FUN11: FILTER's and COMB's extras in tag 5 (defaults: none; per part; a part past NTRK skipped; saved "
                     "and loaded with a project)", ok);
    }
    printf(bad ? "fun11: %d FAILED\n" : "fun11: all passed\n", bad);
    return bad != 0;
}
