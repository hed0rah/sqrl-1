#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hed0rah
# the SQRL-1 shell's host suites without the JieLi toolchain: on Windows with clang and mingw's headers (fork from
# tests/shim/win, a SKIP there), and the same on any host. Each suite says ok, SKIP (exit 77: it needed fork) or
# FAIL, its log in build/host-win/; any FAIL fails the run, and a SKIP is never counted as ok.
#   tests/run_host_win.sh            CC (default clang), PYTHON (default python3)
set -u
cd "$(dirname "$0")/.."
CC="${CC:-clang}"
PY="${PYTHON:-python3}"
OUT=build/host-win
X=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) X=.exe ;; esac
# build/gen first; where the generators cannot run (Windows' Pillow has no raqm for the font's features), the headers
# already there do: the host suites do not depend on them changing. GEN=0 skips the step
H="ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h shell_brand.h felucca_samples.h"
have=1
for h in $H; do [ -f "build/gen/$h" ] || have=0; done
if [ "${GEN:-1}" != 0 ] && ! "$PY" tools/build.py --gen > "${TMPDIR:-/tmp}/run_host_win_gen.log" 2>&1; then
    [ "$have" = 1 ] || { echo "run_host_win: tools/build.py --gen failed and build/gen is incomplete"; exit 1; }
    echo "run_host_win: tools/build.py --gen failed here; using the headers already in build/gen"
fi
[ "$have" = 1 ] || for h in $H; do [ -f "build/gen/$h" ] || { echo "run_host_win: build/gen/$h missing"; exit 1; }; done
mkdir -p "$OUT"
I="-Itests/shim/win -Ibuild/gen -Ifirmware/src -Itests"
ok=0 skip=0 fail=0
suite()                                                 # name, source, its flags; then its arguments after --
{
    n=$1 src=$2
    shift 2
    fl=""
    while [ $# -gt 0 ] && [ "$1" != -- ]; do fl="$fl $1"; shift; done
    [ $# -gt 0 ] && shift
    # shellcheck disable=SC2086
    if ! $CC -O1 -w $fl $I -o "$OUT/$n$X" "tests/$src" -lm > "$OUT/$n.build.log" 2>&1; then
        echo "FAIL  $n (build: $OUT/$n.build.log)"; fail=$((fail + 1)); return
    fi
    "$OUT/$n$X" "$@" > "$OUT/$n.log" 2>&1
    case $? in
    0) echo "ok    $n"; ok=$((ok + 1)) ;;
    77) echo "SKIP  $n (it forks)"; skip=$((skip + 1)) ;;
    *) echo "FAIL  $n ($OUT/$n.log)"; fail=$((fail + 1)) ;;
    esac
}
suite mk_fun10 mk_fun10.c -- "$OUT/fun10_stock.bin"
suite storage_shell storage_test.c -DFELUCCA_SHELL=1
suite shell_test shell_test.c -DFELUCCA_SHELL=1
suite dataloss_test dataloss_test.c -DFELUCCA_SHELL=1
suite drift_test drift_test.c -DFELUCCA_SHELL=1
suite fun11_test fun11_test.c -DFELUCCA_SHELL=1 -- "$OUT/fun10_stock.bin"
suite autosave_sqrl_test autosave_sqrl_test.c -DFELUCCA_SHELL=1 -- "$OUT/fun10_stock.bin"
suite drumlane_test drumlane_test.c -DFELUCCA_SHELL=1
suite backup_sqrl_test backup_sqrl_test.c -DFELUCCA_SHELL=1
suite editor_sqrl_test editor_sqrl_test.c -DFELUCCA_SHELL=1
echo "run_host_win: $ok ok, $skip SKIP, $fail FAIL"
[ "$fail" -eq 0 ]
