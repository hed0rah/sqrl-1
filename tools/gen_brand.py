#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hed0rah
"""gen_brand.py OUT.h: the shell's brand pictures (assets/brand/*.png) as run-length images, and Nibs's face as
ALIVE's hidden seed.

  nibs.png                the acidcat mascot, Nibs: rasterised from acidcat's logo (the face only, the
                          lettering left out), 200 x 200 RGBA. The hand-drawn whiskers are the brand: never redraw
  mask-wordmark-200.png   databurn's wordmark at its own 4.69:1, 16-level alpha: the splash and
  mask-wordmark-150.png   ABOUT
  mask-nibs-line-120.png  Nibs's line art (acidcat's mark.svg, its stroke thickened), on dark grounds: peek84's source
  alive-nibs.txt          his silhouette as ALIVE's 47 x 38 cells: the hidden NIBS seed
  mask-sqrl-mark-32.png   sqrl's mark, SINE ACORN: the wheel's centre
  mask-sqrl-lockup.png    the splash's lockup (databurn): "SQRL-1" on a rule with
                          the squirrel seated on it, 217 x 214

Made from those at build time: peek84 (Nibs's line art at 84 px: the idle peek, whole on the screen) and sqrl52
(the lockup's squirrel, above its rule, 52 px tall: the peek's bouncing logo).

Copyright (C) 2026 hed0rah. The art is CC-BY-SA-4.0 (assets/brand/LICENSE.txt); the names and marks: Trademarks in
LICENSING.md. A colour picture becomes a palette of up to 32 (RGB565, alpha 0..256) and a byte stream:
bits 0..4 the palette index, bits 5..7 the run length - 1 (rows wrap). A mask (greyscale) is one ink, drawn in the
colour given: its bytes are bits 0..3 the coverage (0..15), bits 4..7 the run - 1; 0xF0 (16 clear) is followed by a
byte of that many more clear pixels (BR_*_MASK 1: about 40 % smaller on the large, mostly empty marks)."""
import sys
from pathlib import Path

from PIL import Image

ASSETS = Path(__file__).resolve().parent.parent / "assets" / "brand"
PICS = [("nibs", "nibs.png"), ("databurn150", "mask-wordmark-150.png"),
        ("sqrllockup", "mask-sqrl-lockup.png"),
        ("sqrl32", "mask-sqrl-mark-32.png")]
AL_W, AL_ROWS = 47, 38                                  # shell.c ALIVE's ring and its history


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def derived(name):
    """the build's own sizes of the marks (a smaller Nibs, a larger squirrel), LANCZOS from the drawn ones"""
    if name == "peek84":
        return Image.open(ASSETS / "mask-nibs-line-120.png").resize((84, 84), Image.Resampling.LANCZOS)
    im = Image.open(ASSETS / "mask-sqrl-lockup.png")
    sq = im.crop((0, 0, im.width, 158))                 # (the squirrel above the rule)
    sq = sq.crop(sq.point(lambda v: 255 if v > 8 else 0).getbbox())
    return sq.resize((round(sq.width * 52 / sq.height), 52), Image.Resampling.LANCZOS)


def encode(name, path):
    src = derived(name) if path is None else Image.open(path)
    N = name.upper()
    if src.mode == "L":                                 # a mask: one ink, 16 coverage steps, runs of up to 16 + 255 clear
        idx = [(v * 15 + 127) // 255 for v in src.getdata()]
        w, h = src.size
        out, i = [], 0
        while i < len(idx):
            run, lim = 1, 16 + 255 if idx[i] == 0 else 16
            while run < lim and i + run < len(idx) and idx[i + run] == idx[i]:
                run += 1
            if idx[i] == 0 and run >= 16:
                out += [0xF0, run - 16]
            else:
                out.append(idx[i] | (run - 1) << 4)
            i += run
        lines = [f"static const uint8_t BR_{N}_RLE[{len(out)}] = {{"]
        for k in range(0, len(out), 24):
            lines.append("    " + ", ".join(str(v) for v in out[k: k + 24]) + ",")
        lines += ["};", f"#define BR_{N}_W {w}", f"#define BR_{N}_H {h}", f"#define BR_{N}_MASK 1"]
        print(f"brand: {name} {w}x{h}, mask, {len(out)} B")
        return "\n".join(lines)
    else:
        im = src.convert("RGBA")
        q = im.quantize(32, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        pal = q.getpalette("RGBA")[: 32 * 4]
        idx = list(q.getdata())
        cols = [(rgb565(*pal[i * 4: i * 4 + 3]), (pal[i * 4 + 3] * 256 + 127) // 255) for i in range(max(idx) + 1)]
    w, h = src.size
    n = len(cols)
    out, i = [], 0
    while i < len(idx):
        run = 1
        while run < 8 and i + run < len(idx) and idx[i + run] == idx[i]:
            run += 1
        out.append(idx[i] | (run - 1) << 5)
        i += run
    lines = [f"static const uint16_t BR_{N}_COL[{n}] = {{" + ", ".join(f"0x{c:04X}" for c, _ in cols) + "};",
             f"static const uint16_t BR_{N}_A[{n}] = {{" + ", ".join(str(a) for _, a in cols) + "};",
             f"static const uint8_t BR_{N}_RLE[{len(out)}] = {{"]
    for k in range(0, len(out), 24):
        lines.append("    " + ", ".join(str(v) for v in out[k: k + 24]) + ",")
    lines += ["};", f"#define BR_{N}_W {w}", f"#define BR_{N}_H {h}", f"#define BR_{N}_MASK 0"]
    print(f"brand: {name} {w}x{h}, {n} colours, {len(out)} B")
    return "\n".join(lines)


def cells():
    """Nibs's head as ALIVE's 47 x 38 history: alive-nibs.txt, oldest
    row first, # alive, his chin on the newest row so the rule has something to grow from. Row r is bit-packed, cell
    i in byte i / 8, bit i % 8."""
    grid = (ASSETS / "alive-nibs.txt").read_text().split()
    assert len(grid) == AL_ROWS and all(len(r) == AL_W for r in grid), "alive-nibs.txt: 38 rows of 47"
    rows = [[sum((r[b * 8 + k] == "#") << k for k in range(8) if b * 8 + k < AL_W) for b in range((AL_W + 7) // 8)]
            for r in grid]
    print(f"brand: nibs cells {AL_W}x{AL_ROWS}, {sum(r.count('#') for r in grid)} alive")
    return (f"static const uint8_t BR_NIBS_CELLS[{AL_ROWS}][{(AL_W + 7) // 8}] = {{\n" +
            "\n".join("    {" + ", ".join(str(v) for v in r) + "}," for r in rows) + "\n};")


def main(out):
    body = "\n\n".join([encode(n, ASSETS / f) for n, f in PICS] + [encode(n, None) for n in ("peek84", "sqrl52")] +
                       [cells()])
    Path(out).write_text("/* generated by tools/gen_brand.py from assets/brand: do not edit */\n"
                         "#pragma once\n#include <stdint.h>\n\n" + body + "\n", encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1])
