#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hed0rah
# SQRL-1's manual from the firmware: tests/doc_dump.c walks the shell (data and screens), tools/gen_docs.py writes
# docs/*.md and docs/img/ from it and docs/src/, and with --site DIR the HTML site. Run after one ./build.sh.
#   tools/docs.sh [--site DIR]
set -e
cd "$(dirname "$0")/.."
[ -f build/gen/felucca_tables.h ] || { echo "docs: run ./build.sh once first (build/gen)"; exit 1; }
mkdir -p build/host build/docs/shots
FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # (ABOUT: the real base version)
${CC:-cc} -O1 -w -DFELUCCA_SHELL=1 "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o build/host/doc_dump tests/doc_dump.c -lm
SHELL_SHOTS=build/docs/shots build/host/doc_dump > build/docs/shell.json
# the sidelines: the SOUNDs SQRL-1 leaves out, built back for their own pages (gen_docs.py SIDE), with the sample
# sets a SQRL-1 build leaves out of build/gen
mkdir -p build/docs/shots_side build/docs/gen
FELUCCA_SAMPLE=1 python3 tools/gen_samples.py build/docs/gen/felucca_samples.h > /dev/null
${CC:-cc} -O1 -w -DFELUCCA_SHELL=1 -DFELUCCA_LOFI=1 -DFELUCCA_VOICE=1 -DFELUCCA_TRIO=1 \
    -DFELUCCA_SAMPLE=1 -DFELUCCA_GRAIN=1 -DFELUCCA_NOISE=1 -DFELUCCA_SYLLABLE=1 -DFELUCCA_VODER=1 -DFELUCCA_ORGAN=1 -DFELUCCA_LOOP=1 \
    -DFELUCCA_WHEEL=1 -DFELUCCA_LIBRARY=1 "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/docs/gen -Ibuild/gen -Ifirmware/src -Itests \
    -o build/host/doc_dump_side tests/doc_dump.c -lm
SHELL_SHOTS=build/docs/shots_side build/host/doc_dump_side > build/docs/sidelines.json
python3 tools/gen_docs.py "$@"
