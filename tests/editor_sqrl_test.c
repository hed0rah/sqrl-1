/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* SQRL-1's step in the editor protocol (firmware/src/editor.c, FELUCCA_SHELL): INFO 4A 01 06; STEP_SET and STEP_GET
 * with notes 5 and 6 and the six JUST values after the condition (23 step bytes); an editor of four notes (15 bytes)
 * writing back what it read keeps the fifth and sixth notes and the JUST, and one that changed a note drops that
 * note's JUST and, the chord changed, the notes past four.
 *   cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/editor_sqrl_test tests/editor_sqrl_test.c -lm */
#define EDITOR_NO_MAIN 1
#include "editor_test.c"

static const uint8_t *reply(void)                      /* the reply's arguments (after F0 7D 46 4C cmd) */
{
    return host_wire + 5;
}

int main(void)
{
    static const uint8_t NT[6] = {48, 55, 60, 64, 67, 70};
    static const int8_t JT[6] = {0, 0, 0, -2, 0, -5};
    uint8_t a[32];
    uint32_t n, k, ok;
    int bad = 0;
    step_t *s;
    reset();
    n = request(ED_INFO, 0, 0);
    for (ok = 0, k = 5; k + 2u < n; k++)
        ok |= host_wire[k] == 0x4A && host_wire[k + 1u] == 1 && host_wire[k + 2u] == STEP_NOTES;
    bad += check("SQRL-1: INFO says 4A 01 06 (six notes and JUST a step)", ok);

    memset(a, 0, sizeof a);                             /* STEP_SET, 24 bytes: index 0, then the 23 */
    a[1] = 6;
    for (k = 0; k < 4u; k++)
        a[2 + k] = NT[k];
    a[6] = ST_NOTE; a[8] = 100; a[12] = 100; a[13] = 1; a[14] = 8; a[15] = 0;
    a[16] = NT[4]; a[17] = NT[5];
    for (k = 0; k < 6u; k++)
        a[18 + k] = (uint8_t)(JT[k] + 8);
    n = request(ED_STEP_SET, a, 24);
    s = &TSEL->step[0];
    ok = n == 30u && s->n == 6u;
    for (k = 0; k < 6u; k++)
        ok = ok && s->note[k] == NT[k] && step_just(s, k) == JT[k];
    bad += check("SQRL-1: STEP_SET of 24 bytes writes six notes and their JUST; the reply 8 bytes longer", ok);

    n = request(ED_STEP_GET, a, 1);
    ok = n == 30u && reply()[1] == 6u && reply()[16] == NT[4] && reply()[17] == NT[5] && reply()[18 + 3] == (uint8_t)(JT[3] + 8) &&
         reply()[18 + 5] == (uint8_t)(JT[5] + 8);
    bad += check("SQRL-1: STEP_GET gives notes 5, 6 and the six JUST after the condition", ok);

    memcpy(a + 1, reply() + 1, 15);                     /* an editor of four notes writes back what it read */
    a[0] = 0;
    request(ED_STEP_SET, a, 16);
    ok = s->n == 6u && s->note[4] == NT[4] && s->note[5] == NT[5] && step_just(s, 3) == JT[3] && step_just(s, 5) == JT[5];
    bad += check("SQRL-1: a four-note editor writing back what it read keeps notes 5, 6 and the JUST", ok);

    a[2 + 3] = 66;                                      /* .. and one that changed note 4 */
    request(ED_STEP_SET, a, 16);
    ok = s->n == 4u && s->note[3] == 66u && step_just(s, 3) == 0 && step_just(s, 4) == 0 && step_just(s, 5) == 0 &&
         step_just(s, 0) == JT[0];
    bad += check("SQRL-1: a four-note editor changing a note: four notes, that note's JUST and the rest's gone", ok);

    a[0] = 0; a[18] = 16;                               /* a JUST out of range: refused, nothing written */
    a[1] = 6;
    {
        step_t before = *s;
        request(ED_STEP_SET, a, 24);
        ok = !memcmp(&before, s, sizeof before);
    }
    bad += check("SQRL-1: STEP_SET with a JUST past 15 is refused", ok);
    {   /* WHEEL (112): INFO 51 01 112; a read: rc, 13 keys, the counts, each SOUND's flags and name, each NOTE's
         * name; a set (op 1), a refused one (rc 1, as it was), the default (op 2), a bad op (rc 2) */
        static const char *const SN[9] = {"FM6", "LIZARD", "LIBRARY", "RUDIMENT", "TALK", "SYLLABLE", "VODER", "ORGAN",
                                          "SLICE"};
        static const char *const NN[6] = {"KEYS", "CHORD", "ALIVE", "TONNETZ", "ENIGMA", "HARP"};
        const uint8_t *r;
        uint32_t i, p;
        n = request(ED_INFO, 0, 0);
        for (ok = 0, k = 5; k + 2u < n; k++)
            ok |= host_wire[k] == 0x51 && host_wire[k + 1u] == 1 && host_wire[k + 2u] == ED_WHEEL;
        sh_wheel_factory();
        n = request(ED_WHEEL, 0, 0);
        r = reply();
        ok = ok && host_wire[4] == ED_WHEEL && r[0] == 0u && !memcmp(r + 1, SH_WHEEL_DEF, SH_WHEEL_N) && r[14] == 9u &&
             r[15] == 6u;
        for (p = 16, i = 0; i < 9u && ok; i++) {        /* flags, then the name */
            ok = r[p] == (uint32_t)((i == 8u ? 0u : 1u) | (i == 3u ? 2u : 0u)) && !strcmp((const char *)r + p + 1, SN[i]);
            p += 2u + (uint32_t)strlen(SN[i]);
        }
        for (i = 0; i < 6u && ok; i++) {
            ok = !strcmp((const char *)r + p, NN[i]);
            p += 1u + (uint32_t)strlen(NN[i]);
        }
        ok = ok && 5u + p + 1u == n && host_wire[n - 1u] == 0xF7;
        bad += check("WHEEL: INFO 51 01 112; a read: rc, the 13 keys, the counts, SOUNDs with flags, NOTE engines", ok);
        a[0] = 1;
        memcpy(a + 1, SH_WHEEL_DEF, SH_WHEEL_N);
        a[1 + 7] = 7;                                   /* ORGAN on C5 */
        request(ED_WHEEL, a, 1u + SH_WHEEL_N);
        ok = reply()[0] == 0u && reply()[1 + 7] == 7u;
        a[1 + 3] = 0;                                   /* RUDIMENT's key taken: refused, as it was */
        request(ED_WHEEL, a, 1u + SH_WHEEL_N);
        ok = ok && reply()[0] == 1u && reply()[1 + 3] == 3u && reply()[1 + 7] == 7u;
        a[0] = 2;
        request(ED_WHEEL, a, 1);
        ok = ok && reply()[0] == 0u && reply()[1 + 7] == 127u;
        a[0] = 9;
        request(ED_WHEEL, a, 1);
        ok = ok && reply()[0] == 2u;
        bad += check("WHEEL: set (op 1), refused whole (rc 1), the default (op 2), an unknown op (rc 2)", ok);
    }
    printf("%s\n", bad ? "SQRL-1 EDITOR TEST FAILED" : "SQRL-1 editor test passed");
    {   /* the engine is the SOUND the wheel put on the part: the editor loads that engine's sounds, not another engine */
        uint8_t a[4];
        uint32_t e0 = ed_eng(TSEL), other = e0 == 5u ? 0u : 5u;
        a[0] = 1; a[1] = G_ENGSEL; a[2] = (uint8_t)((8192 + other) & 127); a[3] = (uint8_t)((8192 + other) >> 7);
        request(ED_SET, a, 4);
        ok = ed_eng(TSEL) == e0;
        a[0] = (uint8_t)other; a[1] = 0;
        request(ED_PRESET, a, 2);
        ok = ok && ed_eng(TSEL) == e0;
        a[0] = (uint8_t)e0; a[1] = 1;
        request(ED_PRESET, a, 2);
        ok = ok && ed_eng(TSEL) == e0 && TSEL->preset == 1u;
        bad += check("SQRL-1: the editor's engine change is refused (the wheel's SOUND), its own engine's PRESET loads", ok);
    }
    return bad != 0;
}
