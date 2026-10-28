#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""raw2png.py DIR [SCALE]: the shell test's screen dumps (240 x 240 RGB565, byte-swapped as the LCD gets them,
tests/shell_test.c SHELL_SHOTS=DIR) into PNGs beside them, enlarged SCALE times (default 2)."""
import glob
import sys

from PIL import Image

d, scale = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 2
for f in sorted(glob.glob(f"{d}/*.raw")):
    b = open(f, "rb").read()
    px = []
    for i in range(0, len(b), 2):
        v = b[i] << 8 | b[i + 1]
        px.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    im = Image.new("RGB", (240, 240))
    im.putdata(px)
    im.resize((240 * scale, 240 * scale), Image.NEAREST).save(f[:-4] + ".png")
    print(f[:-4] + ".png")
