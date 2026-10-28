# Building it yourself

SQRL-1 builds from this repository on Linux x86-64, where the FM-1's toolchain runs natively; macOS builds through
Docker, as `BUILDING.md` in the repository describes. The firmware is Felucca's build with one switch,
`FELUCCA_SHELL=1`, which puts the SQRL-1 shell and its nuts in place of Felucca's screens.

## What you need

- Python 3 with Pillow and fontTools (`pip3 install Pillow==10.2.0 fonttools`): the screen's font and icons are drawn
  at build time. Another Pillow works, but its FreeType draws the screen's font slightly differently, and the tests
  then fail on the manual's screens.
- The JieLi toolchain for the FM-1's chip: `tools/get_toolchain.sh` installs it to `~/.jieli/toolchain`.
- Three files from JieLi's AC79 SDK (Apache-2.0), which are not in this tree: `cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin` and `cfg/eq_cfg_hw.bin`, read from a clone of it:

```
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

`JIELI_TOOLCHAIN` and `AC79_SDK` tell the build where to look if you keep them elsewhere.

## Build

```
FELUCCA_SHELL=1 ./build.sh
```

SQRL-1 builds eight SOUNDs. Each of the others has a flag that builds it back, set in the environment:
`FELUCCA_PHASE`, `FELUCCA_LOFI`, `FELUCCA_VOICE`, `FELUCCA_TRIO`, `FELUCCA_GRAIN`, `FELUCCA_NOISE`, `FELUCCA_SLICE`,
`FELUCCA_WHEEL`, `FELUCCA_SYLLABLE`, `FELUCCA_VODER`, `FELUCCA_ORGAN`, `FELUCCA_LOOP` and `FELUCCA_LIBRARY`, for example
`FELUCCA_ORGAN=1 FELUCCA_SHELL=1 ./build.sh`. Each costs 3 to 6 KB of flash (SLICE about 29 KB, with its break). An
engine built back is offered by the setup page's SOUND knob, not on a white key. [On the sidelines](sidelines.md)
lists them, each with its page.

The package is `build/felucca.fwsc`. To install it from the web installer, make a local copy of the installer
around it and open that from `localhost` (Web MIDI only runs in a secure context):

```
python3 web/make_site.py --dev build/felucca.fwsc mybuild /tmp/sqrl-site
cd /tmp/sqrl-site && python3 -m http.server 8000
# open http://localhost:8000/install/ in Chrome or Edge
```

`tools/fm1_install.py build/felucca.fwsc` installs from a terminal instead (it needs `pip3 install mido
python-rtmidi`). Installing your own build is at your own risk; **Return to official V15** in the installer is the
way back.

## Test

The shell's host test drives it frame by frame on the computer, running the same code as the FM-1:

```
cc -O1 -w -DFELUCCA_SHELL=1 -Ibuild/gen -Ifirmware/src -Itests -o build/host/shell_test tests/shell_test.c -lm
build/host/shell_test
```

`tests/run_tests.sh` runs Felucca's whole suite. Both need one `./build.sh` first, for the generated tables in
`build/gen`.

## This manual

The manual is generated from the firmware, so it is rebuilt with it:

```
tools/docs.sh                       # docs/*.md and docs/img/
tools/docs.sh --site /tmp/sqrl-docs # also the HTML site
```

`tests/doc_dump.c` enters every nut as a player would, walks its pages and writes each knob's label, range and
starting value, with a screen of every page. `tools/gen_docs.py` puts that together with the prose in `docs/src/`.
Edit the prose there, never the generated files in `docs/`.

To add a nut of your own, see [Making a nut](making-a-nut.md).
