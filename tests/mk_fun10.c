/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* A stock-built FUN10 project, for tests/fun11_test.c (SQRL-1 loads stock projects): track 1's step 1 a 4-note chord,
 * step 2 a single note with NUDGE and RATCH, the rest the defaults.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/mk_fun10 tests/mk_fun10.c -lm
 *   build/host/mk_fun10 build/host/fun10_stock.bin */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"

int main(int argc, char **argv)
{
    static project_t q;
    static project_store_t out;
    static const uint8_t CH[4] = {60, 64, 67, 70};
    uint32_t t, i;
    FILE *f;
    memset(&q, 0, sizeof q);
    q.magic = PROJ_MAGIC;
    q.size = sizeof q;
    q.parts = NPART;
    q.phys = PROJ_PHYS;
    chain_defaults(&q.chain);
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++)
            q.t[t].p[i] = TP[i].def;
        for (i = 0; i < NSTEP; i++)
            q.t[t].step[i].time = ST_REST;
        memcpy(q.fm6[t], FM6_INIT, FM6_PACKED);
    }
    for (i = 0; i < 4u; i++)
        q.t[0].step[0].note[i] = CH[i];
    q.t[0].step[0].n = 4;
    q.t[0].step[0].time = ST_NOTE;
    q.t[0].step[0].vel = 100;
    q.t[0].step[1].note[0] = 72;
    q.t[0].step[1].n = 1;
    q.t[0].step[1].time = ST_NOTE;
    q.t[0].step[1].vel = 90;
    step_set_ratchet(&q.t[0].step[1], 3);
    step_set_nudge(&q.t[0].step[1], -3);
    if (argc < 2 || !proj_pack(&out, &q))
        return 1;
    f = fopen(argv[1], "wb");
    if (!f || fwrite(&out, 1, sizeof out, f) != sizeof out)
        return 1;
    fclose(f);
    return 0;
}
