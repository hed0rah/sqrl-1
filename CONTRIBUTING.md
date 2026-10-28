# Contributing to SQRL-1

Bug reports, fixes and new nuts are welcome. This file explains how the source fits together, because a few of its
rules are not obvious from any single file.

## Reporting a bug

Include the version (tap GLO four times for ABOUT, or read the installer page), the nut pair in front, the steps to
reproduce it, what you expected and what happened. A photo of the screen or a short video helps. For an install
problem, attach the installer's log (its **Copy log** button).

## Pull requests

The bar is the same for every change, including AI-assisted ones:

- keep it small and about one thing;
- understand the change well enough to explain it in review;
- run the tests below, and try it on a real FM-1 if it touches sound or timing;
- regenerate the manual if it changes a page, a knob or a screen;
- give new files an SPDX licence header.

By contributing you agree that your contribution is licensed under GPL-3.0-only, or under the licence already in
the header of the file you change.

## How the firmware is built

**One translation unit.** `firmware/src/felucca.c` includes every other `.c` file in a fixed order, and the result
is compiled as one file. A file can use anything included before it; anything included after it needs a forward
declaration. That is why `seq.c` declares `shell_block` and `shell_ne_key` (they live in `shell.c`, included much
later) and why `shell.c` declares `up_store` (in `upreset.c`, included after it).

**`FELUCCA_SHELL=1` makes it SQRL-1.** The switch is read in `core.h` and selects `shell.c` in place of Felucca's UI
loop (`main.c`). Felucca's UI code is still compiled: the shell borrows its FM6 operator pages (`ui_fm6op.c`).
Without the switch the tree builds stock Felucca, which is how Felucca's own tests run.

**Two contexts.** The audio interrupt renders sound and plays the keys: `seq.c`'s `keyboard_block` reads the keys
every block, `events_block` runs the sequencer and calls `shell_block` once a block, and `voice.c` and `fx.c`
render. The main loop, about 60 times a second, runs `shell_input`, `shell_leds` and `shell_draw`. The shell talks
to the interrupt through a handful of `volatile` variables, listed at the top of `shell.c`. A change that spans
more than one of them happens between `fm1_irq_off()` and `fm1_irq_on()`.

**Memory.** Large buffers go in the `.pool` section, and `tools/build.py` fails a build that leaves less than 8 KiB
of it free. Anything new that needs pool should say how much in its pull request.

## Adding a nut

A **SOUND** is an `engine_t` (see `eng_organ.c` for a small, complete one):

1. Write `eng_<name>.c`, include it from `engines.c` and append it to `ENGINES[]`. Engine numbers are stored in
   projects and user sounds, so append only and never reorder. Give it an `ENGI_` number in `core.h` and raise
   `NENGINES`.
2. Append a row to `SH_MODES[]` in `shell.c`. The wheel saves catalogue indexes, so this list is append only too.
3. Optionally add it to `CH_ENGINES[]` (LIBRARY walks those) and give it a picture: a `sh_pic_*` function called
   from `sh_draw_picture`.

A **NOTE engine** implements `note_engine_t` from `note_engine.h`: four parameters, its state in at most
`NE_MEM_MAX` bytes, and it owns its note-offs (every note it starts, it ends, including on `stop`). Include its `.c`
from `shell.c` next to `tonnetz.c` and append it to `SH_NOTES[]` after the existing note engines. The shell saves the
four values of the first four note engines only.

Then write its manual page, `docs/src/sound/<name>.md` or `docs/src/note/<name>.md`, by copying an existing one.

## Tests

Run `./build.sh` once first: the tests use the tables it generates in `build/gen`.

```
tests/run_tests.sh
```

runs everything: Felucca's suites on the stock build, then the SQRL-1 ones on the shell build. The ones that matter
most for the shell:

- `tests/shell_test.c` drives the shell frame by frame, the same code the FM-1 runs, against stubbed hardware.
  Its helpers press buttons (`stap(B_HOME)`), turn encoders (`sturn(EN_K1, 3)`), play keys (`skey(k)`, key 0 is F3)
  and render audio (`srender(blocks)`); `sgo` and `sgo2` enter a pair. Its blocks share state and run in order.
  `SHELL_SHOTS=dir` saves the screen at each `shot()`.
- `tests/regress.c` renders every factory sound, the shared effects, the inserts and the NOTE engines' output, and
  compares a hash of each with `tests/golden_shell.txt` (the shell build) and `tests/golden.txt` (stock). A change
  to a sound fails here until its golden is rewritten on purpose: `GOLDEN_UPDATE=1 tests/run_tests.sh`, then
  review the diff of the golden file.
- `tests/target_budget.py` checks the audio interrupt's cost in the disassembly against
  `tests/target_budget_shell.txt`.

## The manual

`docs/*.md` and `docs/img/` are generated: edit the prose in `docs/src/`, then run `tools/docs.sh` (add
`--site DIR` for the HTML site) and commit the result. `tests/doc_dump.c` walks every nut and page the way a player
would and records each knob's label, range and first value with a screen of every page; `tools/gen_docs.py` fills
the prose's `<!-- auto:... -->` markers from that. A `### PAGE` heading that names no page fails the build.

## Comments

Comments say why, or carry a fact the code cannot: a unit, a range, the reason a constant has its value, an
interrupt rule. Leave history to the commit log: no names, dates, commit hashes, release numbers or paths outside
this repository.
