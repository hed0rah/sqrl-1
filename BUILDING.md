# Building SQRL-1

SQRL-1 is Felucca's build with one switch: `FELUCCA_SHELL=1 ./build.sh` builds SQRL-1, plain `./build.sh` builds
stock Felucca (its own tests use that). Everything below is Felucca's build and applies to both.

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites

The JieLi toolchain is Linux x86-64 only. On Linux x86-64 it runs natively; on macOS the build runs each tool in a
`linux/amd64` `debian:bookworm-slim` container, so it needs Docker Desktop (Rosetta on Apple silicon), with the source
tree in a folder Docker can share, e.g. under `/Users`.

- Python 3 with Pillow and fontTools: `pip3 install Pillow==10.2.0 fonttools` (the UI font and icons are
  rasterised at build time)
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- For the tests: git, and a host C compiler (`cc`, gcc or clang) with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).
- numpy (optional), only for the offline table generators `tools/gen_uac_fir.py` and
  `tools/make_shock_table.py`; the build uses the tables they wrote.

## Build

```
FELUCCA_SHELL=1 ./build.sh      # SQRL-1
./build.sh                      # stock Felucca, for its own tests
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`). A build that is not a release reports its version with `-dev` after it.

`FELUCCA_SHELL=1 ./build.sh --release 1.0.0` makes a release build. The number must be the one in
`firmware/src/sqrl_version.h`, which is the only place the version is set. The package
identity becomes `FM-1_950` (SQRL-1 1.X is `FM-1_95X`, clear of Felucca's `FM-1_9XY`), the package is
`build/sqrl-1-1.0.0.fwsc`, and `build/release-sqrl-1-1.0.0/` holds what a release ships: the package, the app
(`sqrl-1-1.0.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_SHELL` | 0 | `1`: SQRL-1, the shell and its nuts (`firmware/src/shell.c`) in place of Felucca's UI |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/src/usb.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 1 | the SLICE engine |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build/emu 8790     # open http://localhost:8790/
```

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in
`fonts/`). The page keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated:
USB (audio, MIDI out, the editor, updates), TRS MIDI and the user sample slots. Felucca's own build of it, which runs
stock Felucca, is at <https://hugelton.github.io/Felucca/webapp/try/>.

## Tests

```
tests/run_tests.sh
```

Runs the host tests; with Node.js installed, the web page tests as well, and with Emscripten the browser emulator's. Run
it after `./build.sh`: it uses `build/` and needs `AC79_SDK` set as for the build. The suites cover flash storage, user
presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock, control), USB audio, the update
entry and loader, the command-line installer, the UI (the real drawing code against stubs: every screen in every palette
is rendered and checked for clipped or overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS,
FM6, SLICE, the DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER and
swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with their floating-point
originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the SQRL-1 web installer, [databurn.org/sqrl/install](https://databurn.org/sqrl/install/), in Chrome or Edge.
Felucca's own installer (<https://hugelton.github.io/Felucca/webapp/installer/>) installs stock Felucca, not SQRL-1.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py --dev build/felucca.fwsc dev /tmp/sqrl-site
cd /tmp/sqrl-site && python3 -m http.server 8000
# open http://localhost:8000/install/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

### What an installed FM-1 accepts from a later package

The update entry in every installed SQRL-1 checks the package's loader before it erases anything
(`firmware/src/ota.c`, step 2). A later release installs on an FM-1 running this one only if its loader keeps to
these limits, so they do not change:

- the packed loader, after its 32-byte header, at most 19968 bytes (`OTA_AREA_LEN` 0x5000 less 0x200), unpacked at
  most 80 KiB (0x14000);
- its load address 0x01C0A800;
- the marker `FELUCCA-LOADER-1` in its first LZ4 literals (`firmware/loader/crt0_ldr.S`).

Today's loader is 6859 bytes packed (`loader/ota.bin`, 6891 with its 32-byte header) and 8188 unpacked. There is
no version check, so an older package installs over a newer one as well.
