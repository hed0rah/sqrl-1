/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* SQRL-1's full backup out and back (firmware/src/editor_backup.c in the FELUCCA_SHELL build), on tests/backup_test.c's
 * simulated NOR flash:
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/backup_sqrl_test tests/backup_sqrl_test.c -lm
 * A device with four FUN11 projects (chords of six notes with JUST, the shell's pairs), MOUTH's phrases U1..U8 and its
 * settings is backed up (LIST, GET), the flash and RAM wiped, the archive put back (PUT, in the list's order); then
 * every object reads back byte for byte: the projects in flash and in the retained slots, the settings, the phrases
 * in RAM and in their record beside the settings (and after a power-on load of it) */
#define BACKUP_NO_MAIN 1
#include "backup_test.c"

static uint32_t rnd = 777u;
static uint32_t rn(uint32_t n) { rnd = rnd * 1664525u + 1013904223u; return (rnd >> 8) % n; }

static void chord_project(project_t *q, uint32_t k)     /* project k: every track a six-note JUST chord a 4th step */
{
    uint32_t t, i, j;
    project_capture(q);
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < NSTEP; i += 4u) {
            step_t *s = &q->t[t].step[i];
            memset(s, 0, sizeof *s);
            s->time = ST_NOTE;
            s->vel = (uint8_t)(64 + rn(64));
            s->n = STEP_NOTES;
            for (j = 0; j < STEP_NOTES; j++) {
                s->note[j] = (uint8_t)(36 + 7 * j + rn(5));
                step_set_just(s, j, (int32_t)rn(16) - 8);
            }
        }
    q->pair[0] = (uint8_t)k;                            /* (each its own pairs) */
    q->pair[2] = (uint8_t)(k & 1u);
    q->sum = proj_sum(q);
}

static uint8_t arch[32][4096];                          /* the archive: each object's bytes */
static uint32_t arch_len[32], arch_id[32], arch_crc[32], arch_n;

int main(void)
{
    static project_t q;
    static project_store_t img[4], back;
    static mouth_user_t phr[MOUTH_NUSER];
    static persist_t ps0, ps1;
    static phr_rec_t r;
    static up_rec_t drm;
    uint32_t k, i, ok, off;
    int bad = 0;

    reset();                                            /* the device: four projects, phrases, settings */
    for (k = 0; k < 4u; k++) {
        chord_project(&q, k);
        ok = proj_pack(&img[k], &q);
        memcpy(&proj_slot[k], &img[k], sizeof img[k]);
        st_save(OBJ_PROJECT0 + k, &img[k], sizeof img[k]);
        if (!ok)
            bad += check("a six-note JUST project packs", 0);
    }
#if FELUCCA_CMP
    static uint8_t cmp0[ST_PAYLOAD_MAX];                /* slot 1's companion, one object: a header and a stream */
    int cmp0_n = 1200;
    {
        cmp_hdr_t h = {CMP_MAGIC, cmp_key_of(img[0].raw), 0x1234u, 1200u - (uint32_t)sizeof h, 0, 1, {0, 0}};
        memcpy(cmp0, &h, sizeof h);
        for (i = sizeof h; i < 1200u; i++)
            cmp0[i] = (uint8_t)rn(256);
        st_save(OBJ_CMP0, cmp0, 1200u);
    }
#endif
    for (k = 0; k < MOUTH_NUSER; k++) {
        phr[k].n = (uint8_t)(3u + k);
        for (i = 0; i < phr[k].n; i++) {
            phr[k].ph[i] = (uint8_t)(1u + rn(39u));
            phr[k].len[i] = (uint8_t)rn(3u);
        }
    }
    memcpy(mouth_user, phr, sizeof phr);
    phr_pack();
    st_tail_set(&phr_rec, sizeof phr_rec, 1);
    memset(&ps0, 0, sizeof ps0);
    settings_export(&ps0);
    persist_saved = ps0;
    st_save(OBJ_SETTINGS, &ps0, sizeof ps0);

    memset(&drm, 0, sizeof drm);                        /* a DRUMS sound with its sound per drum (record version 6) */
    drm.used = UP_USED; drm.ver = UP_VER_DRL; drm.engine = ENGI_DRUM; drm.np = P_COUNT;
    memcpy(drm.name, "DRM", 3);
    for (i = 0; i < P_COUNT; i++)
        drm.packed[i] = 64;
    for (i = 0; i < 16u; i++) {
        drm.note[i] = (uint8_t)(20u + i * 5u);
        drm.flags[i] = (uint8_t)(100u - i * 3u);
    }
    {
        int rc = up_put(5, &drm);
        ok = rc == 0 || rc == 3;
    }
    if (!ok || up_rec(5)->ver != UP_VER_DRL)
        bad += check("a version 6 DRUMS sound stores", 0);

    {   /* back up: LIST, then every object with something in it, 256 bytes a GET */
        uint32_t n;
        call(ED_BACKUP_LIST, 0, 0);
        ok = rep[1] == 0u && rep[2] == ED_BK_N;
        n = rep[2];
        for (i = 0; i < n && ok; i++) {
            const uint8_t *o = rep + 3 + i * 11u;
            arch_id[arch_n] = o[0];
            arch_len[arch_n] = ed_bk_r32(o + 1);
            arch_crc[arch_n] = ed_bk_r32(o + 6);
            arch_n++;
        }
        for (i = 0; i < arch_n && ok; i++)
            for (off = 0; off < arch_len[i] && ok; off += 256u) {
                uint32_t c = arch_len[i] - off > 256u ? 256u : arch_len[i] - off;
                ok = get(arch_id[i], off, c) == 0u && ed_unpack7(rep + 9, rep_n - 9u, arch[i] + off, 256u) == c;
            }
        for (i = 0; i < arch_n && ok; i++)
            ok = st_crc32(arch[i], arch_len[i]) == arch_crc[i];
        bad += check("SQRL-1 backup: LIST names the phrases (id 10); every object read back with its CRC", ok);
    }

    reset();                                            /* the wiped device */
    memset(mouth_user, 0, sizeof mouth_user);
    memset(&phr_rec, 0, sizeof phr_rec);
    st_tail_set(&phr_rec, 0, 0);

    for (ok = 1, i = 0; i < arch_n; i++) {              /* restore, in the list's order */
        if (!arch_len[i] && arch_id[i] != 1u && arch_id[i] != 10u)
            continue;
        ok = ok && put_all(arch_id[i], arch[i], arch_len[i], arch_crc[i]) == 0u;
    }
    bad += check("SQRL-1 restore: every object put back onto the wiped flash", ok);

    for (ok = 1, k = 0; k < 4u; k++) {
        ok = ok && !memcmp(&proj_slot[k], &img[k], sizeof img[k]);
        ok = ok && st_load(OBJ_PROJECT0 + k, &back, sizeof back) == (int)sizeof back && !memcmp(&back, &img[k], sizeof back);
    }
    bad += check("SQRL-1 restore: the four FUN11 projects byte for byte, in flash and in their slots", ok);
#if FELUCCA_CMP
    {
        static uint8_t got[ST_PAYLOAD_MAX];
        ok = st_load(OBJ_CMP0, got, sizeof got) == cmp0_n && !memcmp(got, cmp0, (uint32_t)cmp0_n);
        for (i = 0; i < arch_n; i++)
            ok = ok && arch_id[i] < 32u;                /* (no sample slots listed: their flash is the companions') */
        bad += check("SQRL-1 restore: slot 1's companion (ids 11..26) byte for byte; no sample ids listed", ok);
    }
#endif
    ok = up_rec(5)->ver == UP_VER_DRL && !memcmp(up_rec(5)->note, drm.note, 16) && !memcmp(up_rec(5)->flags, drm.flags, 16);
    bad += check("SQRL-1 restore: a DRUMS sound's sound per drum (record version 6) byte for byte", ok);
    ok = st_load(OBJ_SETTINGS, &ps1, sizeof ps1) == (int)sizeof ps1 && !memcmp(&ps0, &ps1, sizeof ps0);
    bad += check("SQRL-1 restore: the settings object byte for byte, and the shell told to reload its own", ok && sh_restored);
    ok = !memcmp(mouth_user, phr, sizeof phr) && !st_tail_load(&r, sizeof r) && phr_valid(&r) && !memcmp(r.u, phr, sizeof phr);
    memset(mouth_user, 0, sizeof mouth_user);           /* (a power-on: the phrases from their record) */
    phr_load();
    ok = ok && !memcmp(mouth_user, phr, sizeof phr);
    bad += check("SQRL-1 restore: MOUTH's phrases in RAM, in their record, and after a power-on load", ok);

    {   /* stock Felucca and earlier SQRL-1 know no id 10: refused cleanly (here: a PUT of a bad record) */
        static phr_rec_t badr;
        memcpy(&badr, &phr_rec, sizeof badr);
        badr.crc ^= 1u;
        ok = put_all(10, &badr, sizeof badr, st_crc32(&badr, sizeof badr)) == 2u && !memcmp(mouth_user, phr, sizeof phr);
        bad += check("SQRL-1 restore: a phrase record with a wrong CRC refused, the phrases untouched", ok);
    }
    printf("%s\n", bad ? "SQRL-1 BACKUP TEST FAILED" : "SQRL-1 backup test passed");
    return bad != 0;
}
