# Making a nut

A nut is either a SOUND (an engine that turns notes into samples) or a NOTE engine (which turns keys and the
clock into notes). This page covers building one into the firmware. It assumes the tree builds and its tests pass, as
[Building it yourself](build.md) describes, and that you read C.

Everything here is under `#if FELUCCA_SHELL`. Built without it, the tree is stock Felucca and must stay bit for bit
what it was.

## The two kinds

### SOUND

A SOUND is a row of `SH_MODES[]` in `firmware/src/shell.c` that points at an engine in `ENGINES[]`
(`firmware/src/engines.c`). The row is the shell's view of it: its name, its glyph and colour, and how it sits on
the wheel. The engine is the sound.

`sh_mode_t`, one row of `SH_MODES[]`:

| Field | What it is |
| --- | --- |
| `name` | the name on the wheel, the card and the manual (the manual page's title must match it) |
| `about` | one line, lower case: "pulls tonewheel drawbars" |
| `color` | its identity colour, `RGB(r, g, b)` (RGB565) |
| `sides` | its glyph: a regular polygon of this many sides, 0 a circle |
| `engine` | its `ENGINES[]` index, an `ENGI_*` |
| `hero` | the parameter ALGORITHM turns: a `P_*` id, or an `SP_*` one |
| `kind` | `SHK_ENGINE`: every SOUND is an engine; the other kinds (`SHK_CHORD`, `SHK_RUDIMENT`, ..) are NOTE engines' |
| `part` | its own part, 0..3, or `SH_PINGPONG` (part 1 or 2, alternating at each switch) |
| `bg` | unused now (the tracks: a NOTE engine's `part` of 2 or 3 is LINE's or BEAT's) |
| `lib` | 1: LIBRARY, which walks every engine in `CH_ENGINES[]` (built with `FELUCCA_LIBRARY=1` only); 0 for a nut of your own |

`engine_t`, in `firmware/src/core.h`:

| Field | What it is |
| --- | --- |
| `name` | the engine's name in PRESETS and the editor |
| `page_title[2]` | the titles of EDIT 1 and EDIT 2 |
| `edit[8]` | a `param_desc_t` for each of `P_E0`..`P_E7`: label, format (`F_*`), min, max, default, value names, unit |
| `presets`, `npresets` | its factory sounds, a `preset_t[]` (fewer than 128: see below) |
| `knob[4]` | HOME's four knobs, as parameter ids |
| `poly` | its voice cap for POLY and UNISON, 0 = all `NVOICE` (8) |
| `sampled` | 1: it plays recorded material by position; `voice.c` keeps no phases over a retrigger and spreads none for UNISON |
| `keep` | bit k: `voice_t.s[k]` survives a retrigger of a sounding voice (filter state) |
| `oneshot` | 1: hits, as DRUM: always POLY, no glide |
| `note_on(t, v)` | a voice starts (or retriggers: `v->env` is then non-zero) |
| `render(t, v, out, n, m)` | adds `n` mono samples of voice `v` into `out` |
| `amp(t, v, adsr)` | optional: the voice's amplitude in place of the ADSR curve, once a control tick, Q15 in, Q15 out |
| `desc(t, k)` | optional: another descriptor for EDIT k (same range and default, other label or names), 0 = `edit[k]` |
| `block(t)` | optional: once a block per part before its voices, also with none sounding |
| `keys(t, k)` | optional: the note of key k when the engine maps the keys itself, -1 for the scale keyboard |
| `ownenv` | 1: its own envelopes are the amplitude (FM6); the voice renders at 1.0 and ends when `done()` says so |
| `done(t, v)` | with `ownenv`: 1 when the voice has finished |
| `mem` | bytes of the part's shared engine memory it keeps its state in, 0 = none |

A sound is a `preset_t`: its name, `e[8]` (`P_E0`..`P_E7`, `int8_t`), `env[4]` (ATK DEC SUS REL), `fenv`, `mono`,
then `FX(dist, chorus, delay, reverb)` and `PAT(n)`, a suggested pattern (0 none).

Per-voice state fits `voice_t.s[8]` and `ph[3]`. State past that goes in the part's engine memory: a struct sized by
`ENG_MEM_FITS("NAME", sizeof(x))`, reached with `ENG_MEM(part)` and declared in `.mem`. Each part has one region of
`ENG_MEM_PART` (12888) bytes that its engines share, sized by PHYS; a switch to an engine zeroes its `mem` bytes
when another engine held the region (`eng_mem_claim`). An engine that needs more grows the region for all four
parts.

### NOTE

A NOTE nut is a row of `SH_NOTES[]` in `shell.c`. A new one is `SHK_NE`: an implementation of `note_engine_t`
(`firmware/src/note_engine.h`), the contract TONNETZ, ENIGMA, HARP, S+H and CANON follow.

`sh_note_t`:

| Field | What it is |
| --- | --- |
| `name`, `about`, `color` | as a SOUND's |
| `kind` | `SHK_NE` (the others, `SHK_ENGINE` KEYS, `SHK_CHORD`, `SHK_ALIVE`, are the shell's own) |
| `hero` | `SP_NE0`: ALGORITHM turns the engine's first value |
| `part` | `SH_PINGPONG` for one played from the keys (PLAY, host channel 0); 2 for one that plays by itself (LINE, host channel 1, running when left) |
| `bg` | 1 with part 2, else 0 (unused now) |
| `ne` | `&NE_<NAME>`, the engine |
| `clocked` | 1 plays on the clock (PLAY starts it), 0 from the keys; it changes what the screen says, not when `step` runs |

`note_engine_t`:

| Field | What it is |
| --- | --- |
| `name` | its name |
| `mem` | `sizeof` its state, at most `NE_MEM_MAX` (512) |
| `params` | four `ne_param_t`: label, format (`NE_INT`, `NE_ENUM`, `NE_PCT`, or `NE_LEN`: the knob is the part's LEN, which the host turns), min, max, default, names |
| `init(st)` | first visit only: the state is zeroed, then `init` runs; it lives on over switches |
| `param(st, id, v)` | value `id` (0..3) changed, or `NE_P_LEN` (4): the part's LEN, 1..64, the cycle to repeat after; the engine clamps it |
| `scale(st, root, mask)` | SCALE changed: root 0..11, a 12-bit mask, bit 0 the root |
| `key(st, key, down, o)` | key 0..26 (F3..G5, `NE_KEY_F3` 53 is key 0's MIDI note) down or up |
| `step(st, idx, o)` | a step of the part's sequencer, while the transport runs; `idx` wraps at LEN, so 0 is a cycle's top |
| `stop(st, o)` | transport stop or a switch away: send an off for every note it has on |

Events go out through `ne_emit(o, on, note, vel, token)` into an `ne_out_t` of at most `NE_MAX_EV` (16) a call;
`ne_emit` drops the rest silently. Velocity 0 on a note-on is played as 1. `ne_degree(base, mask, d)` gives the
scale's degree `d` above `base`, octaves included.

The host is in `shell.c`. `sh_ne_prepare` turns the four `ne_param_t` into the shell's knobs: an `NE_ENUM` counts
from 0 on the screen and the engine gets `v + min`; an `NE_PCT` with max 127 shows as a percentage of 127, any other
`NE_PCT` as a plain number with a `%` unit. The engine owns its note-offs: the host never guesses them, and every
note it starts it must end, in `key`, `step` or `stop`.

HOME of a NOTE nut is its first two values and the SOUND's first two knobs; its MAIN page (ARP) is all four
(`SHK_HERO2`, `SHK_HOME`).

## The files a nut touches

Work through them in this order.

### A SOUND

1. `firmware/src/eng_<name>.c`: the engine, `static const engine_t ENG_<NAME>`, its presets and value names. A DSP
   core of its own goes in a separate file the engine includes, as `eng_organ.c` includes `organ.c`.
2. `firmware/src/core.h`: an `ENGI_<NAME>` after `ENGI_LOOP` (21), and `NENGINES` raised by one in its
   `FELUCCA_SHELL` term.
3. `firmware/src/engines.c`: `#include "eng_<name>.c"` in the `#if FELUCCA_SHELL` block after `eng_loop.c`; append
   `&ENG_<NAME>` at the end of `ENGINES[]`, inside the same `#if`; add its index to `ENGINE_ORDER[]`, the order
   PRESETS and the editor show. `ENGINE_ORDER[]` is sized `NENG_SHOWN`, so a missing entry is a zero, a second
   ANALOG, not a compile error.
4. The other tables indexed by engine number, each sized `NENGINES` and zero-filled past its last initialiser:
   - `firmware/src/fx.c` `ENG_TRIM[]`: the level trim, Q14. **Zero is silence**: start at 16384 (unity) and let
     `tests/level_test.c` say whether it needs trimming.
   - `firmware/src/midi_control.c` `MIDI_CC_ENG[]`: which E knob CC74 and CC71 turn, 0x00 none.
   - `firmware/src/category.c` `PRESET_CAT[]`: a letter per preset for PRESETS' list filter; none reads as OTHER.
   - `firmware/src/favorites.c` `FAV_EXTRA[]`: where its presets' favourite bits live. Row 15, which engines 14 and
     up use, has no byte left: see the stored formats below.
5. `firmware/src/shell.c`: append the row to `SH_MODES[]`, and give `sh_eng_sibling` a case if a build may leave
   it out. Optionally add the engine to `CH_ENGINES[]` (LIBRARY walks those) and a picture: a `sh_pic_<name>` called from `sh_draw_picture`'s `switch`, which otherwise draws the
   default flower.
6. Pages. A SOUND has HOME (`knob[4]`), EDIT 1 and EDIT 2 (`edit[8]`), and the shared pages every engine has (SHAPE,
   VOICE, GLIDE, ENV, LFO, FX, ARP, SEQ). A page beyond those is the shell's: an `SHP_*` before `SHP_N`, its family
   in `SHP_FAM[]`, its four ids in `SHP_IDS[]`, its name in both page-name arrays in `shell.c`, a case in
   `sh_page_ok` that shows it for your engine only, and its id in `tests/doc_dump.c`'s `OWN[]`. LOOP's KEYS pages
   (`SHP_LKEYS`, `SHP_LKEYS2`) are the model.
7. Parameters past the eight E knobs are shell parameters, `SP_*`: see below.

### A NOTE engine

1. `firmware/src/<name>.c`: the engine, ending in `const note_engine_t NE_<NAME> = {...};` and a size check
   against `NE_MEM_MAX`.
2. `firmware/src/shell.c`: `#include "<name>.c"` beside `tonnetz.c`, `enigma.c` and `harp.c`; append the row to
   `SH_NOTES[]`, and its catalogue index to `SH_NE_AT[]`, the map from engine index to row (TONNETZ, ENIGMA and
   HARP are rows 3..5, S+H and CANON 8 and 9, after LINES; ACID, row 10, is the shell's own and no note engine), and
   to `SH_NREC_BANK[]` the pattern it keeps when left: its own, or `SH_NREC_GEN`, the one LINE's generators share
   (they record nothing).

The keys, the clock, SCALE and the knobs reach it with no more wiring: `shell_ne_block` and `shell_ne_key` call it.

### On the wheel

A new SOUND is in the catalogue but on no key: the white keys are fixed (`sh_wslot[]`), and the setup page's SOUND
knob reaches it. Putting it on a white key means changing `sh_wslot[]` and `sh_wheel_factory`. A new NOTE engine is on
no black key until a player puts it there with the slot picker ([The wheel](wheel.md)) or the installer's "Set up your
wheel" (the editor protocol's WHEEL command); both list it without any change on your part.

### Its manual page

1. `docs/src/sound/<name>.md` or `docs/src/note/<name>.md`, its title the nut's name, copied from a page of the same
   kind. The markers, HTML comments of the form `auto:<what>`, are filled from the firmware:
   `auto:card`, `auto:home`, `auto:pages`, and `auto:keys F3-G5=ZONE` for a NOTE engine's keys. Under `## Pages`, a
   `### NAME` heading is moved under the page of that name; one that names no page fails the build.
2. `tests/doc_dump.c` enters every row of `SH_MODES[]` and `SH_NOTES[]` by itself. Its `pose()` sets up a nut
   before its screens are taken (a chord held, the clock running); add a case if yours looks empty at rest.
3. `tools/gen_docs.py` `note_sides()` gives each NOTE nut's glyph in the manual by name; add yours, or it is a
   circle.
4. `tools/docs.sh` writes `docs/`. Commit the source and the generated pages together.

## Stored formats: append only

Each of these lists is stored by index, so an entry already there never moves. New entries go at the end.

| List | Where its index or its order is stored |
| --- | --- |
| `ENGINES[]` (`ENGI_*`) | a track's engine byte in projects (`project.c`) and user sounds (`upreset.c`), the editor protocol, the favourite bits (`favorites.c`), and the tables above indexed by engine |
| `SH_MODES[]` | the wheel's white keys as older builds saved them (`SH_ST3`: catalogue index + 1, in NOISE's favourites row; not read now), the pair played last (`SH_ST[SH_ST_PAIR]`), a project's pairing (`project_t.pair[0]`, `[3]`) |
| `SH_NOTES[]` | the wheel's black keys (`SH_ST3[9..13]`), the pair played last (`SH_ST[SH_ST_PAIR + 1]`), a project's pairing (`pair[1]`, `[4]`) |
| `SH_NE_AT[]` | the note engines' four values, by place: engine `i` owns bytes `4i..4i+3` of `SH_ST2` (0..3) or `4(i - 4)..` of `SH_ST4` (4..7) |
| `SH_ST_IDS[]` | the shell's kept settings, bytes 1..7 of `SH_ST`, in this order |

`SH_ST`, `SH_ST2`, `SH_ST3` and `SH_ST4` are bytes 16..31 of favourites rows 13, 12, 11 and 10: presets 128 and up, which no
engine has. That is why an engine must have fewer than 128 presets; `tests/shell_test.c` checks it.

### Room left

- **Parameters.** `P_COUNT` is 127 in the SQRL-1 build and cannot grow: motion stores a parameter id in 7 bits
  (`MOTION_ID`, bit 7 is the lock) and `upreset.c` asserts `P_COUNT < 128`. A SOUND has its eight E knobs; anything
  more is a shell parameter.
- **Shell parameters.** `SP_*` ids run from `SP_FIRST` (160) to `SP_END` (229). Page tables hold ids in a byte and
  0xFF is `SH_NONE`, so 26 remain above; `SP_FIRST` can come down to `P_COUNT` (127) for 33 more. Append to the `SP_*` enum, to `SP_DESC[]` and to the `sh_sp[]` initialiser in the
  same order: the two arrays are parallel, and the default lives in both. Several ranges are tested as runs
  (`SP_ELEN..SP_EROT`, `SP_NE0..SP_NE3`, `SP_SCONS..SP_SLEN`), so never insert into one.
- **What a shell parameter keeps.** `sh_sp[]` is RAM. A value is kept over power-off only if its id is in
  `SH_ST_IDS[]`, and that list is full: seven values end where the pair begins at byte 8 (`sh_st_fits`). Shell
  parameters are in no project, no user sound and no motion (`motion_param` and MIDI LEARN take ids under
  `P_COUNT` only). LOOP's KEYS page is like this: its values reset at power-on.
- **Note engines' values.** `SH_ST2` and `SH_ST4` hold eight engines' worth (32 bytes); five are used
  (`sh_st4_fits`). Each value is stored as `v - min + 1` in a byte,
  so a range spans at most 254 steps.
- **Favourites.** `favorites.factory[15]` is full: `FAV_EXTRA` uses bytes 0..9 and 12..13, the settings the rest.
  An engine past `FAV_EXTRA` gets no favourite bits (`fav_byte` returns 0) until it has a place.
- **Automation.** Motion values are `int8_t`, -64..127; an E knob with a range outside that cannot be recorded.

## In the audio interrupt

`render`, `note_on`, `block`, `amp`, `done` and every `note_engine_t` call but `init` run in the audio interrupt.

- **Time.** The interrupt fills a half buffer of `HALF_FRAMES` (128) frames, 2.9 ms at 44.1 kHz (`FS`), in blocks
  of `CTL` (32) samples: events (keys, sequencer, `shell_block`), then each part's voices, the buses and the master.
  A note engine runs once a block. A half over 85 % twice running, or over 95 % once, sheds a voice (`audio.c`).
  The device's own load is SYSTEM's DSP meter.
- **Memory.** No allocation; there is no heap. Large buffers go in the `.pool` section; `tools/build.py` fails a
  build that leaves less than 8 KiB of it free. The shell's own cost per nut is pool too: each `SH_MODES[]` row
  keeps a 416-byte `sh_snap_t`, each `SH_NOTES[]` row a 1280-byte `sh_nrec_t`, each `SHK_NE` engine
  `NE_MEM_MAX` (512) bytes of state.
- **Numbers.** The note engines are integer. Most SOUNDs are fixed point: Q15 samples, `voice_amp(s, m, i)` to scale
  one by the voice's amplitude ramp and level, `mulq15`, `clamp`, the oscillators and filters of `dsp.c`. The
  AC791N has an FPU (`-mfprev1`), and LIZARD and SHOCK render in `float`. Float is allowed but costs cycles, so
  measure it.
- **Aliasing.** `out` may alias `voice_t.s[]`: read state into locals, render, write it back (as `eng_noise.c`).
- **Sharing.** The shell and the interrupt share a handful of `volatile` variables, listed at the top of `shell.c`.
  A change that must land as one, across several of them, goes between `fm1_irq_off()` and `fm1_irq_on()`.

These tests check the CPU cost:

- `tests/shell_bench.c`: the shell build's heaviest sounds against FM6 PAD and LIZARD on the host, instructions a
  sample, within 3 % of `tests/shell_bench_baseline.txt`. Its rows are a list (`B[]`); add yours, then
  `BENCH_UPDATE=1` writes the baseline. On Linux it counts with `perf_event_open`; without a counter it is skipped.
- `tests/regress.c`, built as `regress_shell`: instructions per sample for each engine and preset against
  `tests/cpu_baseline.txt` at +25 %. It counts only on macOS (`proc_pid_rusage`); elsewhere the budget is not
  checked. `BUDGET_UPDATE=1` rewrites the file.
- `tests/target_budget.py`: the loops of named render functions in the target's disassembly, against
  `tests/target_budget_shell.txt` for the shell build (`build/felucca_shell.dis`). Its function list (`FUNCS`) is by
  name: add your render function.

## Testing

`tests/run_tests.sh` builds and runs everything; `./build.sh` once first for `build/gen`.

- `tests/shell_test.c`: add a block that enters your nut (`sgo2(slot, note)`, `note_of("NAME")`, `slot_of("NAME")`)
  and checks what it does: notes on the keys or the clock, nothing left on after a stop or a switch, its values
  kept. A nut not on the default wheel is put there first, through the picker as HARP's block does (a NOTE engine)
  or by setting `sh_wslot[]` / `sh_wnoteslot[]`. The ENIGMA and HARP blocks are the models.
- `tests/regress.c`: every factory sound of every engine renders and hashes against `tests/golden_shell.txt` with no
  code of yours; a new render is reported and does not fail. A note engine needs a case: `job_ne` picks among
  `NE_TONNETZ`, `NE_ENIGMA`, `NE_HARP`, `NE_SNH` and `NE_CANON` by number, and the job list names the settings each is rendered at.
  `GOLDEN_UPDATE=1 tests/run_tests.sh` rewrites the goldens; review the diff before you commit it.
- `tests/level_test.c`: each engine's median level against the limiter, which sets `ENG_TRIM`.
- Stock Felucca: the same script builds without `FELUCCA_SHELL` and checks `tests/golden.txt`,
  `tests/union_golden.txt` and `tests/target_budget.txt`. They must not change. If one does, something of yours is
  outside `#if FELUCCA_SHELL`.

## Licensing

Every source file starts with an SPDX line in its first lines, then its copyright:

```
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) <year> <name> */
```

GPL-3.0-only unless the code is someone else's under another licence, which its header then states. LICENSING.md's
table comes from those headers, and only from tracked files, so `git add` the new file first:

```
git add firmware/src/eng_<name>.c
tools/gen_licensing.py           # rewrites LICENSING.md's table
tools/gen_licensing.py --check   # fails on a file without SPDX or a stale table
```

The manual's credits list every file whose licence or author is not Felucca's, from the same headers.

## A NOTE engine, worked

ENIGMA, trimmed: a shift register turns once a step and its low byte picks a degree of the scale. The real one is
`firmware/src/enigma.c`; this drops its transposition and its gate.

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) <year> <name>
 * WALK: a looping register, as ENIGMA (enigma.c) */
#include "note_engine.h"

typedef struct {
    uint16_t reg, mask;
    uint8_t len, chance, range, dens, root;
    int16_t sounding;                    /* the note on now, -1 = none */
    uint32_t rng;
} wk_t;

static uint32_t wk_rand(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r;
}

static void wk_init(void *st)            /* the state is zeroed before this */
{
    wk_t *w = (wk_t *)st;
    w->rng = 0x6D2B79F5u;
    w->reg = (uint16_t)(wk_rand(&w->rng) | 1u);
    w->len = 8;
    w->range = 2;
    w->dens = 100;
    w->mask = 0xAB5;                     /* major, until scale() says otherwise */
    w->sounding = -1;
}

static void wk_param(void *st, int id, int16_t v)   /* the engine clamps */
{
    wk_t *w = (wk_t *)st;
    switch (id) {
    case 0: w->len = (uint8_t)(v < 2 ? 2 : v > 16 ? 16 : v); break;
    case 1: w->chance = (uint8_t)(v < 0 ? 0 : v > 50 ? 50 : v); break;
    case 2: w->range = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v); break;
    case 3: w->dens = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    }
}

static void wk_scale(void *st, uint8_t root, uint16_t mask)
{
    wk_t *w = (wk_t *)st;
    w->root = (uint8_t)(root % 12u);
    w->mask = mask & 0xFFFu ? mask & 0xFFFu : 0xFFFu;
}

static void wk_key(void *st, uint8_t key, int down, ne_out_t *o)
{
    (void)st; (void)key; (void)down; (void)o;   /* the clock plays it; the keys are silent */
}

static void wk_step(void *st, uint32_t idx, ne_out_t *o)
{
    wk_t *w = (wk_t *)st;
    unsigned bit = (w->reg >> (w->len - 1u)) & 1u;
    int n, i, note;
    if (wk_rand(&w->rng) % 100u < w->chance)
        bit ^= 1u;
    w->reg = (uint16_t)((w->reg << 1) | bit);
    if (w->sounding >= 0)                /* every note on, it ends */
        ne_emit(o, 0, w->sounding, 0, NE_NO_TOKEN);
    w->sounding = -1;
    if (wk_rand(&w->rng) % 100u >= w->dens)
        return;
    for (n = 0, i = 0; i < 12; i++)
        n += (w->mask >> i) & 1;
    note = ne_degree(48 + w->root, w->mask, (w->reg & 0xFF) * n * w->range / 256);
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    ne_emit(o, 1, note, idx % 4u == 0u ? 112 : 96, NE_NO_TOKEN);
    w->sounding = (int16_t)note;
}

static void wk_stop(void *st, ne_out_t *o)
{
    wk_t *w = (wk_t *)st;
    if (w->sounding >= 0)
        ne_emit(o, 0, w->sounding, 0, NE_NO_TOKEN);
    w->sounding = -1;
}

static const char *const WK_RANGE_NAMES[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
static const ne_param_t WK_PARAMS[4] = {
    {"LEN", NE_INT, 2, 16, 8, 0},
    {"CHNC", NE_PCT, 0, 50, 0, 0},
    {"RANGE", NE_ENUM, 1, 4, 2, WK_RANGE_NAMES},   /* the shell shows 0..3, the engine gets 1..4 */
    {"DENS", NE_PCT, 0, 100, 100, 0},
};

typedef char wk_fits[sizeof(wk_t) <= NE_MEM_MAX ? 1 : -1];

const note_engine_t NE_WALK = {
    "WALK", sizeof(wk_t), WK_PARAMS, wk_init, wk_param, wk_scale, wk_key, wk_step, wk_stop,
};
```

In `shell.c`, beside the other note engines and at the end of `SH_NOTES[]`:

```c
#include "walk.c"
    /* .. */
    {"WALK", "walks a looping register", RGB(150, 190, 140), SHK_NE, SP_NE0, SH_PINGPONG, 0, &NE_WALK, 1},
```

`tools/docs.sh` fails a colour that misses its contrast against the screen; lighten it until it passes.

## A SOUND, worked

The smallest engine that plays is a sine with its octave mixed in, its state in `voice_t`. Like most engines it is
fixed point.

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) <year> <name>
 * TWO: a sine and its octave
 *   OCT  the octave's level, 0..127 */

static void two_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env)                         /* a retrigger keeps the phase */
        v->ph[0] = 0;
}

static __attribute__((noinline)) void two_render(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                 const vmod_t *m)
{
    int32_t oct = t->p[P_E0] << 8;       /* 0..127 -> Q15 */
    uint32_t ph = v->ph[0], i;           /* in a local: out may alias v's state */
    for (i = 0; i < n; i++) {
        int32_t s = (osc_sine(ph) + mulq15(osc_sine(ph << 1), oct)) >> 1;
        out[i] += voice_amp(s, m, i);
        ph += m->inc;
    }
    v->ph[0] = ph;
}

#define TWO_RSV {"-", F_INT, 0, 0, 0, 0, 0}
static const preset_t TWO_PRESETS[] = {
    /* OCT; ATK DEC SUS REL; fenv, mono; sends; no pattern */
    {"PURE", {0}, {0, 40, 127, 30}, 0, 0, FX(0, 0, 0, 20), PAT(0)},
    {"HOLLOW", {90}, {2, 60, 100, 50}, 0, 0, FX(0, 30, 0, 40), PAT(0)},
};

static const engine_t ENG_TWO = {
    .name = "TWO",
    .page_title = {"TONE", "-"},
    .edit = {
        {"OCT", F_PCT, 0, 127, 0, 0, 0},
        TWO_RSV, TWO_RSV, TWO_RSV, TWO_RSV, TWO_RSV, TWO_RSV, TWO_RSV,
    },
    .presets = TWO_PRESETS,
    .npresets = NELEM(TWO_PRESETS),
    .note_on = two_note_on,
    .render = two_render,
    .knob = {P_E0, P_ATK, P_DEC, P_REL},
};
```

Then, in order: `ENGI_TWO 22u` and `NENGINES` in `core.h`; the `#include`, the `ENGINES[]` entry and its
`ENGINE_ORDER[]` index in `engines.c`; `16384` at the end of `ENG_TRIM[]` in `fx.c`; the `SH_MODES[]` row:

```c
    {"TWO", "a sine and its octave", RGB(170, 200, 230), 5, ENGI_TWO, P_E0, SHK_ENGINE, SH_PINGPONG, 0, 0},
```

`SINE[]` is full-scale Q15 (`tools/gen_tables.py`), so the two sines summed and halved stay in range: with OCT at
127 a voice peaks near 21000, under `VOICE_FS` (24000).

Every engine in the tree fills both EDIT pages. TWO leaves EDIT 2 reserved; run `tools/docs.sh` and look at its
screens before you ship a page like that, or give it a second value.
