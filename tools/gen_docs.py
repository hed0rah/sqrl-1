#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 databurn
"""gen_docs.py: SQRL-1's manual, as markdown in docs/ and as a static site, from one source.

  tools/gen_docs.py [--site OUT_DIR]

Reads the hand-written prose in docs/src/ and the shell's own data: build/docs/shell.json and the screens in
build/docs/shots/ (tests/doc_dump.c, run by tools/docs.sh). Every table, range, power-on value, preset list,
screen and colour comes from the firmware at build time; the prose only says what things are for.

The prose marks where the data goes with HTML comments:
  <!-- auto:card -->           the nut's header: its glyph, what it is, where it sits, its HOME screen
  <!-- auto:home -->           HOME's four knobs
  <!-- auto:pages -->          every other page: screen and knobs; a "## Pages" section's "### NAME" prose is
                               moved under the page of that name
  <!-- auto:keys F3-E4=ROOT -->  the 27 keys, zones named
  <!-- auto:page NAME -->      a shared page (SHARED, SCALE, SYSTEM, SYSTEM 2) by name
  <!-- auto:insert TYPE -->    an INSERT type's page
  <!-- auto:shot NAME caption -->  a screen of build/docs/shots
  <!-- auto:wheel -->  <!-- auto:nuts -->  <!-- auto:flow -->
  <!-- auto:const NAME [word] -->  a number the firmware sets (doc_dump's "const"), inline
  <!-- auto:rhythms -->        RUDIMENT's rhythms: meter, each lane's hits/steps and turn

Writes docs/*.md, docs/img/ (PNG and SVG) and, with --site, OUT_DIR/*.html: self-contained, relative links, no
external requests. Fails if a marker names something the firmware does not have, or a colour misses its contrast."""
import base64
import html
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "src")
OUT = os.path.join(ROOT, "docs")
IMG = os.path.join(OUT, "img")
DATA = os.path.join(ROOT, "build", "docs", "shell.json")
SHOTS = os.path.join(ROOT, "build", "docs", "shots")
DATA_SIDE = os.path.join(ROOT, "build", "docs", "sidelines.json")
SHOTS_DIR = [SHOTS]                                     # the screens the page being written uses (a sideline's: its own)

# the pages, in the order the sidebar walks them: (source path without .md, group)
GROUPS = ["Start", "The shell", "SOUND nuts", "On the sidelines", "NOTE nuts", "Shared pages", "Building"]
STATIC = [("index", "Start"), ("start", "Start"), ("shell", "The shell"), ("wheel", "The shell"),
          ("pages", "Shared pages"), ("effects", "Shared pages"), ("sequencing", "Shared pages"), ("connections", "Shared pages"),
          ("build", "Building"), ("making-a-nut", "Building"), ("credits", "Building"),
          ("sidelines", "On the sidelines")]
# the SOUNDs SQRL-1's build leaves out (core.h FELUCCA_x, shell.c FELUCCA_LIBRARY): their pages come from a second dump
# with every one of them built (tools/docs.sh, build/docs/sidelines.json and shots_side/); the flag that brings each
# back, and the SOUND a saved sound on it plays on (shell.c sh_eng_sibling)
SIDE = {"SAMPLE": ("FELUCCA_SAMPLE", "PHYS"), "LOFI": ("FELUCCA_LOFI", "ANALOG"), "VOICE": ("FELUCCA_VOICE", "MOUTH"),
        "TRIO": ("FELUCCA_TRIO", "ANALOG"), "GRAIN": ("FELUCCA_GRAIN", "PHYS"), "NOISE": ("FELUCCA_NOISE", "ANALOG"),
        "SYLLABLE": ("FELUCCA_SYLLABLE", "MOUTH"), "VODER": ("FELUCCA_VODER", "MOUTH"), "ORGAN": ("FELUCCA_ORGAN", "ANALOG"),
        "LOOP": ("FELUCCA_LOOP", "PHYS"), "WHEEL": ("FELUCCA_WHEEL", "ANALOG"), "LIBRARY": ("FELUCCA_LIBRARY", "SHOCK")}


def die(msg):
    sys.exit(f"gen_docs: {msg}")


# ------------------------------------------------------------------------------------------- the palette ---
def palette():
    """the shell's colours, from shell.c"""
    src = open(os.path.join(ROOT, "firmware", "src", "shell.c"), encoding="utf-8").read()
    pal = {m[0]: tuple(int(x) for x in m[1:]) for m in
           re.findall(r"#define (SC_\w+) RGB\((\d+), (\d+), (\d+)\)", src)}
    k = re.search(r"SC_KNOB\[4\] = \{(.*?)\};", src).group(1)
    pal["KNOB"] = [tuple(int(x) for x in m) for m in re.findall(r"RGB\((\d+), (\d+), (\d+)\)", k)]
    return pal


def hexc(c):
    return "#%02X%02X%02X" % c


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def lum(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def ink_on(c, ground, need=4.5):
    """c, darkened (or lightened) until it reads on ground at need:1"""
    dark = lum(ground) > 0.5
    for i in range(0, 101):
        t = i / 100
        d = tuple(round(v * (1 - t)) if dark else round(v + (255 - v) * t) for v in c)
        if contrast(d, ground) >= need:
            return d
    die(f"no ink for {c} on {ground}")


# ------------------------------------------------------------------------------------------ the screens ---
def png(w, h, rows):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def shot_png(name):
    """build/docs/shots/NAME.raw (240 x 240 RGB565, byte-swapped as the LCD gets it) -> docs/img/NAME.png"""
    src = os.path.join(SHOTS_DIR[0], name + ".raw")
    if not os.path.exists(src):
        die(f"no screen {name} (run tools/docs.sh)")
    b = open(src, "rb").read()
    rows = []
    for y in range(240):
        r = bytearray()
        for x in range(240):
            v = b[2 * (y * 240 + x)] << 8 | b[2 * (y * 240 + x) + 1]
            r += bytes(((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows.append(r)
    out = os.path.join(IMG, name + ".png")
    data = png(240, 240, rows)
    if not os.path.exists(out) or open(out, "rb").read() != data:
        open(out, "wb").write(data)
    return "img/" + name + ".png"


# ---------------------------------------------------------------------------------------------- the data ---
class Data:
    def __init__(self, d, pal, side=False):
        self.d, self.pal, self.side = d, pal, side
        self.side_rows, self.side_data = [], None
        self.nuts = {}
        for kind in ("sound", "note"):
            for n in d[kind]:
                n["half"] = kind
                self.nuts[n["name"]] = n
        self.shared = {p["name"]: p for p in d["shared"]["pages"]}
        self.shared["SCALE"] = d["shared"]["scale"]
        self.shared["SYSTEM"] = d["shared"]["system"]
        if "system2" in d["shared"]:                # (SQRL-1's SYSTEM 2: GROOVE, CLICK, COUNT-IN)
            self.shared["SYSTEM 2"] = d["shared"]["system2"]
        self.inserts = {i["type"]: i["page"] for i in d["shared"]["insert"]}
        self.white = {w["sound"]: w["key"] for w in d["wheel"]["white"] if w["sound"]}
        self.black = {b["note"]: b["key"] for b in d["wheel"]["black"]}

    def where(self, n):
        if self.side and n["name"] in SIDE:
            return f"a custom build, {SIDE[n['name']][0]}=1 (else on {SIDE[n['name']][1]})"
        if n["half"] == "sound":
            k = self.white.get(n["name"])
            return f"white key {key_name(k)} in the wheel" if k is not None else "the slot picker"
        k = self.black.get(n["name"])
        return f"black key {key_name(k)} in the wheel" if k is not None else "the slot picker"


# auto:notice's heading and lead, written only when the nut that needs the notice is in the build
NOTICE_TITLE = {"BSD-3-Clause-MAME": "MAME's notice"}
NOTICE_LEAD = {"BSD-3-Clause-MAME": "TALK carries MAME's TMS5220 tables, and their licence asks for this notice in the "
                                    "documentation of every binary that includes them:"}
WORDS = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"]
NOTES = ["F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"]


def key_name(k):
    """key 0..26 = F3..G5"""
    n = 53 + k
    return f"{NOTES[(n - 53) % 12]}{n // 12 - 1}"


def key_index(name):
    m = re.fullmatch(r"([A-G]#?)(\d)", name)
    if not m:
        die(f"bad key {name}")
    pc = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"].index(m.group(1))
    k = (int(m.group(2)) + 1) * 12 + pc - 53
    if not 0 <= k <= 26:
        die(f"key {name} is not on the FM-1 (F3..G5)")
    return k


def fmt_val(v):
    if v is None:
        return ""
    if isinstance(v, str):
        return v
    return v["v"] + v["u"]


def knob_rows(page):
    """(knob, label, range, starts at) for a page's knobs"""
    rows = []
    for i, k in enumerate(page["knobs"]):
        if not k:
            continue
        if "names" in k:
            names = k["names"]
            rng = " ".join(names) if len(names) <= 10 else f"{len(names)} values: {' '.join(names[:4])} .. {names[-1]}"
        else:
            rng = f"{fmt_val(k['min'])} .. {fmt_val(k['max'])}"
        rows.append((i + 1, k["label"], rng, fmt_val(k["now"]), k))
    return rows


# --------------------------------------------------------------------------------------------- the SVGs ---
def svg_glyph(sides, color, size=28):
    import math
    c = size / 2
    r = size / 2 - 3
    if not sides:
        body = f'<circle cx="{c}" cy="{c}" r="{r}"/>'
    else:
        pts = []
        for i in range(sides):
            a = -math.pi / 2 + 2 * math.pi * i / sides
            pts.append(f"{c + r * math.cos(a):.1f},{c + r * math.sin(a):.1f}")
        body = f'<polygon points="{" ".join(pts)}"/>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {size} {size}" '
            f'fill="none" stroke="{color}" stroke-width="2.2" role="img" aria-hidden="true">{body}</svg>')


WHITE_PCS = {0, 2, 4, 5, 7, 9, 11}
ZONE = re.compile(r"([A-G]#?\d)(?:-([A-G]#?\d))?(:white)?=(.*?)(?=\s+[A-G]#?\d(?:-[A-G]#?\d)?(?::white)?=|$)")


def parse_zones(arg, where):
    """"F3-E4=ROOT F#3=KICK F3-G5:white=STEPS" -> [(lo, hi, white_only, label)]; later zones win a key"""
    zones = [(key_index(m.group(1)), key_index(m.group(2) or m.group(1)), bool(m.group(3)), m.group(4).strip())
             for m in ZONE.finditer(arg.strip())]
    if not zones:
        die(f"{where}: no zones in auto:keys")
    return zones


def is_white(k):
    return (53 + k) % 12 in WHITE_PCS


def svg_keys(zones, pal, title):
    """the 27 keys F3..G5, each zone's keys in its colour, its name under it (labels that would collide stack)"""
    kw, kh, bh = 26, 96, 58
    whites = [k for k in range(27) if is_white(k)]
    W = len(whites) * kw + 2
    bg, bone, dim, mid = hexc(pal["SC_BG"]), hexc(pal["SC_BONE"]), hexc(pal["SC_DIM"]), hexc(pal["SC_MID"])
    zc = [hexc(pal["KNOB"][0]), hexc(pal["KNOB"][3]), hexc(pal["KNOB"][1]), hexc(pal["KNOB"][2])]
    xof = {k: 1 + i * kw for i, k in enumerate(whites)}

    def zone(k):
        z = None
        for i, (lo, hi, wo, lab) in enumerate(zones):
            if lo <= k <= hi and (not wo or is_white(k)):
                z = i
        return z

    def span(i):
        lo, hi, wo, lab = zones[i]
        ks = [k for k in range(lo, hi + 1) if zone(k) == i]
        ks = ks or [lo]
        x0 = min(xof[k] if is_white(k) else xof[k - 1] + kw - 8 for k in ks)
        x1 = max(xof[k] + kw - 2 if is_white(k) else xof[k - 1] + kw + 6 for k in ks)
        return x0, x1
    labs = []
    for z in zones:
        if z[3] not in labs:
            labs.append(z[3])
    zc = [zc[labs.index(z[3]) % 4] for z in zones]   # (one colour per name: QUALITY twice is one colour)
    rows, place = [], []
    for i, z in enumerate(zones):
        x0, x1 = span(i)
        tw = max(len(z[3]), len(key_name(z[0]) + ".." + key_name(z[1])) if z[0] != z[1] else 3) * 7 + 8
        cx = (x0 + x1) / 2
        a, b = cx - tw / 2, cx + tw / 2
        r = 0
        while r < len(rows) and any(not (b < c or a > d) for c, d in rows[r]):
            r += 1
        if r == len(rows):
            rows.append([])
        rows[r].append((a, b))
        place.append((x0, x1, cx, r))
    H = kh + 14 + len(rows) * 34 + 6
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
           f'aria-label="{html.escape(title)}" font-family="ui-monospace, Menlo, Consolas, monospace" font-size="11">',
           f'<rect width="{W}" height="{H}" fill="{bg}" rx="6"/>']
    for k in whites:
        z = zone(k)
        fill, op = (zc[z], "0.85") if z is not None else (bone, "0.16")
        out.append(f'<rect x="{xof[k]}" y="1" width="{kw - 2}" height="{kh}" rx="2" fill="{fill}" fill-opacity="{op}"/>')
    for k in range(27):
        if is_white(k):
            continue
        x = xof[k - 1] + kw - 8
        z = zone(k)
        out.append(f'<rect x="{x}" y="1" width="14" height="{bh}" rx="2" fill="{bg}" stroke="{mid}"/>')
        if z is not None:
            out.append(f'<rect x="{x + 2}" y="3" width="10" height="{bh - 4}" rx="1" fill="{zc[z]}" fill-opacity="0.9"/>')
    for k in (0, 7, 19):
        out.append(f'<text x="{xof[k] + kw / 2 - 1}" y="{kh - 6}" text-anchor="middle" fill="{bg}" '
                   f'font-weight="bold">{key_name(k)}</text>')
    for i, (lo, hi, wo, lab) in enumerate(zones):
        x0, x1, cx, r = place[i]
        y = kh + 12 + r * 34
        c = zc[i]
        out.append(f'<path d="M{x0} {kh + 6}V{y}H{x1}V{kh + 6}" fill="none" stroke="{c}" stroke-width="1.4" '
                   f'stroke-opacity="{1 if r == 0 else 0.6}"/>' if x1 - x0 > 16 else
                   f'<path d="M{cx} {kh + 6}V{y}" stroke="{c}" stroke-width="1.4"/>')
        out.append(f'<text x="{cx}" y="{y + 13}" text-anchor="middle" fill="{bone}">{html.escape(lab)}</text>')
        if lo != hi:
            out.append(f'<text x="{cx}" y="{y + 26}" text-anchor="middle" fill="{dim}">{key_name(lo)}..{key_name(hi)}</text>')
    out.append("</svg>")
    return "\n".join(out)


def svg_wheel(data, pal):
    """the wheel's octave C4..C5: a SOUND on each white key, a NOTE engine on each black key"""
    kw, kh, bh = 92, 150, 92
    d = data.d["wheel"]
    W, H = 8 * kw + 2, kh + 12
    bg, bone, dim, mid = hexc(pal["SC_BG"]), hexc(pal["SC_BONE"]), hexc(pal["SC_DIM"]), hexc(pal["SC_MID"])
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
           f'aria-label="The wheel: hold HOME, then a white key picks the SOUND, a black key the NOTE engine" '
           f'font-family="ui-monospace, Menlo, Consolas, monospace" font-size="12">',
           f'<rect width="{W}" height="{H}" fill="{bg}" rx="6"/>']
    for i, w in enumerate(d["white"]):
        x = 1 + i * kw
        n = data.nuts.get(w["sound"]) if w["sound"] else None
        col = n["color"] if n else mid
        out.append(f'<rect x="{x + 1}" y="6" width="{kw - 4}" height="{kh}" rx="3" fill="{panel(pal)}" stroke="{col}"/>')
        out.append(f'<text x="{x + kw / 2 - 1}" y="{kh - 24}" text-anchor="middle" fill="{col if n else dim}" '
                   f'font-weight="bold">{w["sound"] or "empty"}</text>')
        out.append(f'<text x="{x + kw / 2 - 1}" y="{kh - 8}" text-anchor="middle" fill="{dim}">{key_name(w["key"])}</text>')
    whites = [w["key"] for w in d["white"]]
    for b in d["black"]:
        i = max(j for j, k in enumerate(whites) if k < b["key"])
        x = 1 + (i + 1) * kw - 34
        n = data.nuts[b["note"]]
        out.append(f'<rect x="{x}" y="6" width="66" height="{bh}" rx="3" fill="{bg}" stroke="{n["color"]}" stroke-width="1.5"/>')
        out.append(f'<text x="{x + 33}" y="{bh - 30}" text-anchor="middle" fill="{n["color"]}" font-weight="bold" '
                   f'font-size="10.5">{b["note"]}</text>')
        out.append(f'<text x="{x + 33}" y="{bh - 14}" text-anchor="middle" fill="{dim}" font-size="10.5">'
                   f'{key_name(b["key"])}</text>')
    out.append("</svg>")
    return "\n".join(out)


def panel(pal):
    return hexc(pal["SC_PANEL"])


def svg_flow(pal):
    """where a note goes: NOTE engine, SOUND, the part's FX and INSERT, the shared effects, out"""
    bg, bone, dim, mid = hexc(pal["SC_BG"]), hexc(pal["SC_BONE"]), hexc(pal["SC_DIM"]), hexc(pal["SC_MID"])
    k = [hexc(c) for c in pal["KNOB"]]
    boxes = [("keys", "27 keys", dim), ("NOTE", "makes the notes", k[0]), ("SOUND", "plays them", k[1]),
             ("SHAPE, FX, INSERT", "the part's own", k[2]), ("DELAY REVERB CHORUS", "shared by every part", k[3]),
             ("out", "MASTER", dim)]
    arrows = ["key, down / up", "note on / off", "audio", "sends", "mix"]
    bw, gap, h = 150, 46, 64
    W = len(boxes) * bw + (len(boxes) - 1) * gap + 4
    H = h + 40
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" '
           f'aria-label="Signal flow: keys to the NOTE engine, to the SOUND, through the part\'s effects to the '
           f'shared effects and out" font-family="ui-monospace, Menlo, Consolas, monospace" font-size="11.5">',
           f'<rect width="{W}" height="{H}" fill="{bg}" rx="6"/>',
           f'<defs><marker id="a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" '
           f'orient="auto"><path d="M0 0L8 4L0 8z" fill="{dim}"/></marker></defs>']
    for i, (t, s, c) in enumerate(boxes):
        x = 2 + i * (bw + gap)
        out.append(f'<rect x="{x}" y="20" width="{bw}" height="{h}" rx="4" fill="{hexc(pal["SC_PANEL"])}" stroke="{c}"/>')
        out.append(f'<text x="{x + bw / 2}" y="{20 + h / 2 - 3}" text-anchor="middle" fill="{bone}" '
                   f'font-weight="bold">{html.escape(t)}</text>')
        out.append(f'<text x="{x + bw / 2}" y="{20 + h / 2 + 14}" text-anchor="middle" fill="{dim}">{html.escape(s)}</text>')
        if i < len(arrows):
            x0, x1, y = x + bw, x + bw + gap, 20 + h / 2
            out.append(f'<path d="M{x0 + 2} {y}H{x1 - 3}" stroke="{dim}" stroke-width="1.4" marker-end="url(#a)"/>')
            out.append(f'<text x="{(x0 + x1) / 2}" y="{14}" text-anchor="middle" fill="{dim}" font-size="10">'
                       f'{html.escape(arrows[i])}</text>')
    out.append("</svg>")
    return "\n".join(out)


def write_svg(name, svg):
    p = os.path.join(IMG, name + ".svg")
    if not os.path.exists(p) or open(p, encoding="utf-8").read() != svg:
        open(p, "w", encoding="utf-8").write(svg)
    return "img/" + name + ".svg"


# ------------------------------------------------------------------------- markdown: a small subset ---
INLINE = [
    (re.compile(r"`([^`]+)`"), lambda m: "<code>" + m.group(1) + "</code>"),
    (re.compile(r"\*\*([^*]+)\*\*"), lambda m: "<strong>" + m.group(1) + "</strong>"),
    (re.compile(r"(?<![\w*])\*([^*\s][^*]*)\*(?!\w)"), lambda m: "<em>" + m.group(1) + "</em>"),
]


def inline(s, link):
    """inline markdown -> html (escaped first; code spans keep their text)"""
    parts = re.split(r"(`[^`]+`)", s)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
            continue
        p = html.escape(p, quote=False)
        p = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", lambda m: f'<img src="{link(m.group(2))}" alt="{m.group(1)}">', p)
        p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: f'<a href="{link(m.group(2))}">{m.group(1)}</a>', p)
        p = re.sub(r"&lt;(https?://[^&\s]+)&gt;", lambda m: f'<a href="{m.group(1)}">{m.group(1)}</a>', p)
        for rx, f in INLINE[1:]:
            p = rx.sub(f, p)
        out.append(p)
    return "".join(out)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def md_to_html(text, link):
    """paragraphs, ATX headings, lists, pipe tables, blockquotes, fenced code, raw HTML blocks (lines starting with <)"""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        ln = lines[i]
        if not ln.strip():
            i += 1
            continue
        if ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j])) + "</code></pre>")
            i = j + 1
            continue
        m = re.match(r"(#{1,4}) (.*)", ln)
        if m:
            n, t = len(m.group(1)), m.group(2)
            out.append(f'<h{n} id="{slug(t)}">{inline(t, link)}</h{n}>')
            i += 1
            continue
        if ln.startswith("<"):
            j = i
            while j < len(lines) and lines[j].strip():
                j += 1
            out.append("\n".join(lines[i:j]))
            i = j
            continue
        if ln.startswith(">"):
            j = i
            while j < len(lines) and lines[j].startswith(">"):
                j += 1
            body = " ".join(l[1:].strip() for l in lines[i:j])
            out.append(f"<blockquote><p>{inline(body, link)}</p></blockquote>")
            i = j
            continue
        if ln.startswith("|"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            rows = [[c.strip() for c in r.strip().strip("|").split("|")] for r in lines[i:j]]
            head, body = rows[0], [r for r in rows[2:]]
            t = ["<table><thead><tr>" + "".join(f"<th>{inline(c, link)}</th>" for c in head) + "</tr></thead><tbody>"]
            for r in body:
                t.append("<tr>" + "".join(f"<td>{inline(c, link)}</td>" for c in r) + "</tr>")
            out.append("".join(t) + "</tbody></table>")
            i = j
            continue
        m = re.match(r"(\s*)([-*]|\d+\.) ", ln)
        if m:
            tag = "ol" if m.group(2)[0].isdigit() else "ul"
            base, items = len(m.group(1)), []           # [text, sub-list tag, sub-items]: one level of nesting
            while i < len(lines) and lines[i].strip():
                mm = re.match(r"(\s*)([-*]|\d+\.) (.*)", lines[i])
                if mm and (len(mm.group(1)) <= base or not items):
                    items.append([mm.group(3), None, []])
                elif mm:
                    items[-1][1] = items[-1][1] or ("ol" if mm.group(2)[0].isdigit() else "ul")
                    items[-1][2].append(mm.group(3))
                elif items and items[-1][2]:
                    items[-1][2][-1] += " " + lines[i].strip()
                elif items:
                    items[-1][0] += " " + lines[i].strip()
                i += 1
            lis = []
            for x, st, subs in items:
                inner = f"<{st}>" + "".join(f"<li>{inline(y, link)}</li>" for y in subs) + f"</{st}>" if subs else ""
                lis.append(f"<li>{inline(x, link)}{inner}</li>")
            out.append(f"<{tag}>" + "".join(lis) + f"</{tag}>")
            continue
        j = i
        while j < len(lines) and lines[j].strip() and not re.match(r"(#{1,4} |```|\||<|>|\s*([-*]|\d+\.) )", lines[j]):
            j += 1
        out.append("<p>" + inline(" ".join(l.strip() for l in lines[i:j]), link) + "</p>")
        i = j
    return "\n".join(out)


# ------------------------------------------------------------------------------- the auto blocks ---
class Page:
    def __init__(self, rel, group):
        self.rel, self.group = rel, group               # rel: "sound/fm6" ...
        self.text = open(os.path.join(SRC, rel + ".md"), encoding="utf-8").read()
        self.title = re.match(r"# (.*)", self.text).group(1).strip()
        self.nut = None

    def up(self):
        return "../" * self.rel.count("/")


def split_pages_section(text):
    """the "## Pages" section's "### NAME" parts: {NAME: prose}, and the text without the section"""
    m = re.search(r"^## Pages\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    if not m:
        return {}, text
    parts = {}
    for mm in re.finditer(r"^### (.*?)\n(.*?)(?=^### |\Z)", m.group(1), re.S | re.M):
        names = [x.strip() for x in re.split(r",\s*|\s+and\s+", mm.group(1).strip()) if x.strip()]
        parts[names[0]] = mm.group(2).strip()          # ("### LANES and LANES 2": the prose under the first)
        for x in names[1:]:
            parts.setdefault(x, "")
    return parts, text[:m.start()] + text[m.end():]


class Render:
    """each auto block twice: markdown (GitHub) and HTML (the site)"""

    def __init__(self, data, pal):
        self.data, self.pal = data, pal

    def legend_html(self, page):
        cells = []
        for i in range(4):
            k = page["knobs"][i]
            c = hexc(self.pal["KNOB"][i])
            if not k:
                cells.append('<div class="kc empty"></div>')
                continue
            cells.append(f'<div class="kc" style="--k:{c}"><span class="kl">{html.escape(k["label"])}</span>'
                         f'<span class="kv">{html.escape(fmt_val(k["now"]))}</span></div>')
        return '<div class="legend">' + "".join(cells) + "</div>"

    def table_md(self, page):
        rows = knob_rows(page)
        if not rows:
            return ""
        t = ["| Knob | Label | Range | Starts at |", "| --- | --- | --- | --- |"]
        for n, lab, rng, now, _ in rows:
            t.append(f"| {n} | `{lab}` | {rng} | {now} |")
        return "\n".join(t)

    def table_html(self, page):
        rows = knob_rows(page)
        if not rows:
            return ""
        t = ['<table class="knobs"><thead><tr><th>Knob</th><th>Label</th><th>Range</th><th>Starts at</th></tr>'
             '</thead><tbody>']
        for n, lab, rng, now, k in rows:
            c = hexc(self.pal["KNOB"][n - 1])
            if "names" in k and len(k["names"]) > 10:
                rng_h = (f'<details><summary>{len(k["names"])} values</summary>'
                         f'{html.escape(" ".join(k["names"]))}</details>')
            else:
                rng_h = html.escape(rng)
            t.append(f'<tr><td><span class="dot" style="--k:{c}"></span>{n}</td><td><code>{html.escape(lab)}</code>'
                     f'</td><td>{rng_h}</td><td>{html.escape(now)}</td></tr>')
        return "".join(t) + "</tbody></table>"

    def screen(self, page, alt, md, up):
        src = shot_png(page["shot"])
        if md:
            return f'<img src="{up}{src}" width="240" height="240" alt="{html.escape(alt)}">'
        return (f'<figure class="lcd"><img src="{up}{src}" width="240" height="240" alt="{html.escape(alt)}" '
                f'loading="lazy"></figure>')

    def page_block(self, page, prose, md, up, level=3, title=None, link=None):
        title = title or page["name"]
        if not level:                                   # (inside the prose's own section: no heading of its own)
            if md:
                tb = self.table_md(page)
                return self.screen(page, f"The {title} page", True, up) + ("\n\n" + tb if tb else "")
            return (f'<div class="pgrow bare">{self.screen(page, "The " + title + " page", False, up)}'
                    f'<div class="pgtext">{self.table_html(page)}</div></div>')
        if md:
            parts = [f"{'#' * level} {title}", "", self.screen(page, f"The {title} page", True, up)]
            if prose:
                parts += ["", prose]
            tb = self.table_md(page)
            if tb:
                parts += ["", tb]
            return "\n".join(parts)
        body = md_to_html(prose, link) if prose else ""
        return (f'<section class="pg"><h{level} id="{slug(title)}">{html.escape(title)}</h{level}>'
                f'<div class="pgrow">{self.screen(page, "The " + title + " page", False, up)}'
                f'<div class="pgtext">{body}{self.table_html(page)}</div></div></section>')

    def card(self, p, md, up):
        n = p.nut
        home = n["pages"][0]
        half = "SOUND" if n["half"] == "sound" else "NOTE"
        where = self.data.where(n)
        glyph = write_svg("glyph_" + slug(n["name"]), svg_glyph(n.get("sides", 0) if n["half"] == "sound" else
                                                                note_sides(n), n["color"]))
        presets = n.get("presets") or []
        if md:
            alg = ("the engine's first EDIT knob" if n["name"] == "LIBRARY" else f'`{n["hero"]}`') if n.get("hero") else ""
            lines = [f'<img src="{up}{glyph}" width="28" height="28" alt=""> **{half} nut:** {n["about"]}. '
                     f'**Reach it:** {where}.' + (f" **ALGORITHM turns:** {alg}." if alg else ""), ""]
            if n["half"] == "sound":
                lines.append(f'**Engine:** `{n["engine"]}`' + (f', {len(presets)} factory sounds' if presets else ""))
                lines.append("")
            lines.append(self.screen(home, f'{n["name"]} on its MAIN page', True, up))
            return "\n".join(lines)
        facts = [("Half", half), ("Reach it", where)]
        if n.get("hero"):
            facts.append(("ALGORITHM", "the engine's first EDIT knob" if n["name"] == "LIBRARY" else n["hero"]))
        if n["half"] == "sound":
            facts.append(("Engine", n["engine"]))
            if presets:
                facts.append(("Factory sounds", str(len(presets))))
        dl = "".join(f"<dt>{html.escape(a)}</dt><dd>{html.escape(b)}</dd>" for a, b in facts)
        return (f'<div class="card"><div class="cardtext"><p class="about">{html.escape(n["about"])}</p><dl>{dl}</dl>'
                f'</div>{self.screen(home, n["name"] + " on its MAIN page", False, up)}</div>')

    def presets(self, p, md):
        pr = p.nut.get("presets") or []
        if not pr or p.nut["half"] != "sound":
            return ""
        if md:
            return "### Factory sounds\n\n" + ", ".join(f"`{x}`" for x in pr) + "\n"
        return ('<section class="pg"><h3 id="factory-sounds">Factory sounds</h3><ul class="presets">' +
                "".join(f"<li>{html.escape(x)}</li>" for x in pr) + "</ul></section>")


def licences():
    """firmware/src files whose licence or author is not Felucca's (GPL-3.0-only, Leo Kuroshita), from their headers"""
    d = os.path.join(ROOT, "firmware", "src")
    rows = []
    for f in sorted(os.listdir(d)):
        if not f.endswith((".c", ".h")):
            continue
        head = "".join(open(os.path.join(d, f), encoding="utf-8", errors="replace").readlines()[:5])
        m = re.search(r"SPDX-License-Identifier:\s*([A-Za-z0-9.+\- ]+?)\s*(?:\*/|-->|\n|$)", head)
        c = re.search(r"Copyright (?:\(C\) |\(c\) )?(.*?)\s*(?:\*/|\n)", head)
        lic, who = (m.group(1) if m else "not stated"), (c.group(1).strip() if c else "not stated")
        if lic == "GPL-3.0-only" and "Kuroshita" in who:
            continue
        rows.append((f, lic, who))
    return rows


def note_sides(n):
    return {"KEYS": 0, "CHORD": 3, "ALIVE": 0, "TONNETZ": 6, "ENIGMA": 6, "HARP": 3, "RUDIMENT": 8, "S+H": 4, "CANON": 5, "ACID": 7}.get(n["name"], 0)


def const_value(data, rel, arg):
    """auto:const NAME [word]: a number the firmware sets (doc_dump's "const"), inline; "word": spelled out"""
    name, _, how = arg.partition(" ")
    if name not in data.d.get("const", {}):
        die(f"{rel}: no constant {name}")
    v = data.d["const"][name]
    return WORDS[v] if how == "word" and isinstance(v, int) and v < len(WORDS) else str(v)


def expand(p, data, pal, md, link):
    """the page's text with every auto block filled, as markdown or as html"""
    R = Render(data, pal)
    up = p.up()
    text = re.sub(r"<!-- auto:const ([^>]*?) -->", lambda m: const_value(data, p.rel, m.group(1).strip()),
                  p.text)                                # (first: a number may sit in a page's own prose too)
    parts, text = split_pages_section(text)
    used = set()
    blocks = {}

    def put(s):
        key = f"\x00{len(blocks)}\x00"
        blocks[key] = s
        return "\n\n" + key + "\n\n"

    def sub(m):
        kind, arg = m.group(1), (m.group(2) or "").strip()
        n = p.nut
        if kind == "rhythms":                           # RUDIMENT's rhythms: meter and each lane's hits / steps
            lanes = data.d["lanes"]
            t = ["| RHYTHM | METER | " + " | ".join(lanes) + " |", "| --- | --- |" + " --- |" * len(lanes)]
            for r in data.d["rhythms"]:
                cells = [f"{k}/{n}" + (f" +{rot}" if rot else "") if n else "" for n, k, rot in r["lanes"]]
                t.append(f'| {r["name"]} | {r["meter"]} | ' + " | ".join(cells) + " |")
            return "\n\n" + "\n".join(t) + "\n\n"
        if kind == "card":
            return put(R.card(p, md, up))
        if kind == "home":                              # the nut's MAIN, with its "### MAIN" prose if it has one
            pg = n["pages"][0]
            used.add(pg["name"])
            prose = parts.get(pg["name"], "")
            if md:
                return put("## MAIN\n\n" + (prose + "\n\n" if prose else "") +
                           (R.table_md(pg) or "MAIN shows the four knobs it is about."))
            return put(f'<h2 id="main">MAIN</h2>' + (md_to_html(prose, link) if prose else "") + R.table_html(pg))
        if kind == "pages":
            out = []
            for pg in n["pages"][1:]:
                used.add(pg["name"])
                out.append(R.page_block(pg, parts.get(pg["name"], ""), md, up, 3, link=link))
            pr = R.presets(p, md)
            head = "## Pages" if md else '<h2 id="pages">Pages</h2>'
            return put("\n\n".join([head] + out + ([pr] if pr else [])) if (out or pr) else "")
        if kind == "keys":
            zones = parse_zones(arg, p.rel)
            src = write_svg("keys_" + slug(p.title), svg_keys(zones, pal, f"{p.title} on the keys"))
            alt = f"{p.title} on the keys: " + "; ".join(
                (f"{key_name(z[0])} {z[3]}" if z[0] == z[1] else
                 f"{key_name(z[0])} to {key_name(z[1])}{' (white keys)' if z[2] else ''} {z[3]}") for z in zones)
            return put(f'<img src="{up}{src}" alt="{html.escape(alt)}">' if md else
                       f'<figure class="diag"><img src="{up}{src}" alt="{html.escape(alt)}"></figure>')
        if kind == "page":
            name = arg
            if name not in data.shared:
                die(f"{p.rel}: no shared page {name}")
            used.add(name)
            return put(R.page_block(data.shared[name], "", md, up, 0, link=link))
        if kind == "insert":
            if arg not in data.inserts:
                die(f"{p.rel}: no INSERT type {arg}")
            used.add(arg)
            return put(R.page_block(data.inserts[arg], "", md, up, 0, title=arg, link=link))
        if kind == "shot":
            name, _, cap = arg.partition(" ")
            src = shot_png(name)
            if md:
                return put(f'<img src="{up}{src}" width="240" height="240" alt="{html.escape(cap)}">\n\n*{cap}*')
            return put(f'<figure class="lcd solo"><img src="{up}{src}" width="240" height="240" '
                       f'alt="{html.escape(cap)}"><figcaption>{html.escape(cap)}</figcaption></figure>')
        if kind == "wheel":
            src = write_svg("wheel", svg_wheel(data, pal))
            alt = "The wheel's octave: " + ", ".join(
                f'{key_name(w["key"])} {w["sound"] or "empty"}' for w in data.d["wheel"]["white"]) + "; " + ", ".join(
                f'{key_name(b["key"])} {b["note"]}' for b in data.d["wheel"]["black"])
            return put(f'<img src="{up}{src}" alt="{html.escape(alt)}">' if md else
                       f'<figure class="diag"><img src="{up}{src}" alt="{html.escape(alt)}"></figure>')
        if kind == "library":
            names = data.d["library"]
            if md:
                return put(", ".join(f"[{e}]({up}sound/{slug(e)}.md)" for e in names))
            return put('<ul class="presets">' + "".join(
                f'<li><a href="{link(up + "sound/" + slug(e) + ".md")}">{e}</a></li>' for e in names) + "</ul>")
        if kind == "notice":                            # a licence's full text, when its nut is in this build
            fname, nut = arg.split()
            if nut not in data.nuts:
                return put("")
            path = os.path.join(ROOT, "LICENSES", fname + ".txt")
            if not os.path.exists(path):
                die(f"{p.rel}: no LICENSES/{fname}.txt")
            txt = open(path, encoding="utf-8").read().strip()
            head = (f"## {NOTICE_TITLE[fname]}\n\n{NOTICE_LEAD[fname]}\n\n" if fname in NOTICE_TITLE else "")
            if md:
                return put(head + "```\n" + txt + "\n```")
            return put(md_to_html(head, link) + "<pre class=\"notice\">" + html.escape(txt) + "</pre>")
        if kind == "licences":
            rows = licences()
            if md:
                t = ["| File | Licence | Copyright |", "| --- | --- | --- |"]
                t += [f"| `{f}` | {l} | {c} |" for f, l, c in rows]
                return put("\n".join(t))
            return put("<table><thead><tr><th>File</th><th>Licence</th><th>Copyright</th></tr></thead><tbody>" +
                       "".join(f"<tr><td><code>{html.escape(f)}</code></td><td>{l}</td><td>{html.escape(c)}</td></tr>"
                               for f, l, c in rows) + "</tbody></table>")
        if kind == "flow":
            src = write_svg("flow", svg_flow(pal))
            alt = "Keys to the NOTE engine to the SOUND, through the part's own effects, the shared effects, and out"
            return put(f'<img src="{up}{src}" alt="{alt}">' if md else
                       f'<figure class="diag"><img src="{up}{src}" alt="{alt}"></figure>')
        if kind == "nuts":
            half = arg or "sound"
            rows, dw = ([n2 for n2 in data.d[half]], data) if half != "sideline" else (data.side_rows, data.side_data)
            if half == "sideline":                       # (the pages are sound/ pages; Reach it: the flag)
                half = "sound"
            if md:
                t = ["| | Nut | What it does | Reach it |", "| --- | --- | --- | --- |"]
                for n2 in rows:
                    g = write_svg("glyph_" + slug(n2["name"]), svg_glyph(
                        n2.get("sides", 0) if half == "sound" else note_sides(n2), n2["color"]))
                    t.append(f'| <img src="{up}{g}" width="20" height="20" alt=""> | [{n2["name"]}]({up}{half}/'
                             f'{slug(n2["name"])}.md) | {n2["about"]} | {dw.where(n2)} |')
                return put("\n".join(t))
            items = []
            for n2 in rows:
                g = write_svg("glyph_" + slug(n2["name"]), svg_glyph(
                    n2.get("sides", 0) if half == "sound" else note_sides(n2), n2["color"]))
                items.append(f'<a class="nutcell" href="{link(up + half + "/" + slug(n2["name"]) + ".md")}" '
                             f'style="--nut:{n2["color"]}"><img src="{up}{g}" width="24" height="24" alt="">'
                             f'<span class="nn">{n2["name"]}</span><span class="na">{html.escape(n2["about"])}</span>'
                             f'<span class="nw">{html.escape(dw.where(n2))}</span></a>')
            return put('<div class="nutgrid">' + "".join(items) + "</div>")
        die(f"{p.rel}: unknown marker {kind}")

    text = re.sub(r"<!-- auto:(\w+)( [^>]*?)? -->", sub, text)
    left = set(parts) - used
    if left:
        die(f"{p.rel}: '### {sorted(left)[0]}' under '## Pages' names no page this nut has")
    if md:
        for k, v in blocks.items():
            text = text.replace(k, v)
        return re.sub(r"\n{3,}", "\n\n", text).strip() + "\n"
    body = md_to_html(text, link)
    for k, v in blocks.items():
        body = body.replace(f"<p>{k}</p>", v).replace(k, v)
    return body


# -------------------------------------------------------------------------------------------- the site ---
CSS = """
:root{--bg:%(bg)s;--panel:%(panel)s;--faint:%(faint)s;--mid:%(mid)s;--dim:%(dimtext)s;--ink:%(bone)s;
--nut:%(bone)s;--k1:%(k1)s;--k2:%(k2)s;--k3:%(k3)s;--k4:%(k4)s;color-scheme:dark;
--mono:ui-monospace,SFMono-Regular,"Cascadia Mono",Menlo,Consolas,monospace;
--sans:"Helvetica Neue",Helvetica,Arial,system-ui,sans-serif}
@media (prefers-color-scheme:light){:root:not([data-theme="dark"]){%(light)s}}
:root[data-theme="light"]{%(light)s}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%%}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.6 var(--sans)}
a{color:var(--nuttext,var(--ink));text-decoration-thickness:1px;text-underline-offset:3px}
a:focus-visible,button:focus-visible,summary:focus-visible{outline:2px solid var(--k1);outline-offset:2px}
code{font:0.88em var(--mono);background:var(--panel);padding:0.08em 0.35em;border-radius:3px}
pre{background:var(--panel);padding:12px 14px;overflow-x:auto;border-radius:4px;border:1px solid var(--faint)}
pre code{background:none;padding:0}\npre.notice{white-space:pre-wrap;font:12px/1.5 var(--mono);color:var(--dim)}
.wrap{display:grid;grid-template-columns:250px minmax(0,1fr);min-height:100vh}
nav.side{border-right:1px solid var(--faint);padding:18px 16px 40px;font:13px/1.5 var(--mono);position:sticky;
top:0;height:100vh;overflow-y:auto}
nav.side .brand{display:flex;align-items:center;gap:10px;margin:0 0 18px;color:var(--ink);text-decoration:none}
nav.side .brand .mark{width:44px;height:44px;color:var(--ink);flex:none}
nav.side .brand b{font-size:18px;letter-spacing:0.04em}
nav.side .brand span{display:block;color:var(--dim);font-size:11px}
nav.side h2{font:600 11px var(--mono);color:var(--dim);letter-spacing:0.12em;text-transform:uppercase;margin:18px 0 6px}
nav.side ul{list-style:none;margin:0;padding:0}
nav.side li a{display:flex;align-items:center;gap:8px;padding:3px 6px;border-radius:3px;color:var(--ink);
text-decoration:none}
nav.side li a:hover{background:var(--panel)}
nav.side li a[aria-current]{background:var(--panel);box-shadow:inset 3px 0 0 var(--navc,var(--ink))}
nav.side li img{width:14px;height:14px;flex:none}
main{padding:0 40px 80px;max-width:1040px;min-width:0}
.status{display:flex;align-items:center;gap:10px;font:13px var(--mono);border-bottom:1px solid var(--nut);
padding:14px 0 10px;margin-bottom:8px;color:var(--dim);flex-wrap:wrap}
.status .nm{color:var(--nuttext,var(--ink));font-weight:700;letter-spacing:0.04em}
.status img{width:20px;height:20px}
.status .grow{flex:1}
.status button{font:12px var(--mono);background:none;color:var(--dim);border:1px solid var(--faint);
border-radius:3px;padding:3px 8px;cursor:pointer}
h1{font:700 34px/1.15 var(--mono);letter-spacing:0.02em;margin:22px 0 6px;color:var(--nuttext,var(--ink))}
h2{font:700 20px var(--mono);margin:44px 0 10px;padding-top:8px;border-top:1px solid var(--faint)}
h3{font:700 16px var(--mono);margin:30px 0 8px;color:var(--nuttext,var(--ink))}
h4{font:700 14px var(--mono);margin:22px 0 6px}
p,li{max-width:70ch}
blockquote{margin:16px 0;padding:8px 14px;border-left:3px solid var(--faint);background:var(--panel);max-width:70ch}
main>p:first-of-type{font-size:18px;color:var(--ink)}
table{border-collapse:collapse;font-size:14px;margin:12px 0;display:block;overflow-x:auto;max-width:100%%}
th,td{text-align:left;padding:5px 12px 5px 0;border-bottom:1px solid var(--faint);vertical-align:top}
th{font:600 11px var(--mono);color:var(--dim);letter-spacing:0.08em;text-transform:uppercase}
td code{white-space:nowrap}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%%;border:2px solid var(--k);margin-right:7px}
details summary{cursor:pointer;color:var(--dim)}
details{font:13px var(--mono)}
figure{margin:0}
.lcd{background:#000;padding:10px;border-radius:12px;border:1px solid var(--faint);width:fit-content;flex:none}
.lcd img{display:block;width:240px;height:240px;image-rendering:auto;border-radius:2px}
.lcd.solo{margin:14px 0}
.lcd figcaption{font:12px var(--mono);color:#7D858D;margin-top:8px;max-width:240px}
.legend{display:grid;grid-template-columns:repeat(4,1fr);gap:2px;margin-top:6px;width:240px}
.kc{font:10px/1.2 var(--mono);color:#E6E1D3;border-top:2px solid var(--k);padding:3px 2px 0;min-height:30px}
.kc .kl{display:block;color:#7D858D}
.kc .kv{display:block;font-size:11px}
.kc.empty{border-top-color:#1D2228}
.card{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap;margin:18px 0 8px}
.cardtext{flex:1;min-width:240px}
.card .about{font:15px var(--mono);color:var(--nuttext,var(--ink));margin:0 0 12px}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 16px;font-size:14px;margin:0}
dt{font:600 11px var(--mono);color:var(--dim);letter-spacing:0.08em;text-transform:uppercase;padding-top:3px}
dd{margin:0}
.pgrow{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.pgrow.bare{margin:14px 0}
.pgtext{flex:1;min-width:260px}
.pgtext p:first-child{margin-top:0}
.presets{display:flex;flex-wrap:wrap;gap:6px;list-style:none;padding:0;font:12px var(--mono)}
.presets li{border:1px solid var(--faint);padding:2px 8px;border-radius:3px}
.diag{margin:16px 0;overflow-x:auto;max-width:100%%}
.diag img{display:block;max-width:100%%;height:auto}
.nutgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:10px;margin:14px 0}
.nutcell{display:grid;grid-template-columns:24px 1fr;gap:2px 10px;padding:10px 12px;border:1px solid var(--faint);
border-left:3px solid var(--nut);border-radius:4px;text-decoration:none;color:var(--ink);background:var(--panel)}
.nutcell:hover{border-color:var(--nut)}
.nutcell img{grid-row:span 3}
.nutcell .nn{font:700 14px var(--mono)}
.nutcell .na{font-size:13px}
.nutcell .nw{font:11px var(--mono);color:var(--dim)}
footer{margin-top:60px;padding-top:14px;border-top:1px solid var(--faint);font-size:13px;color:var(--dim)}
footer p{max-width:none}
.pager{display:flex;justify-content:space-between;gap:12px;margin-top:40px;font:13px var(--mono)}
.menu{display:none}
@media (max-width:820px){
.wrap{display:block}
nav.side{position:static;height:auto;border-right:0;border-bottom:1px solid var(--faint);padding:12px 16px}
nav.side .groups{display:none}
nav.side.open .groups{display:block}
.menu{display:inline-block;font:12px var(--mono);background:none;color:var(--ink);border:1px solid var(--faint);
border-radius:3px;padding:4px 10px;margin-left:auto;cursor:pointer}
nav.side .top{display:flex;align-items:center}
nav.side .brand{margin:0}
main{padding:0 16px 60px}
h1{font-size:26px}
.lcd img,.legend{width:min(240px,calc(100vw - 54px));height:auto}
}
"""


def site(pages, data, pal, out_dir, version, rev):
    os.makedirs(os.path.join(out_dir, "img"), exist_ok=True)
    k = [hexc(c) for c in pal["KNOB"]]
    paper = pal["SC_PAPER"]
    lt_ink = (24, 26, 30)
    light = (f"--bg:{hexc(paper)};--panel:#E9E6DE;--faint:#D6D2C8;--mid:#A8A49A;--dim:{hexc(ink_on(pal['SC_DIM'], paper))};"
             f"--ink:{hexc(lt_ink)};--nut:{hexc(lt_ink)};color-scheme:light;"
             + "".join(f"--k{i + 1}:{hexc(ink_on(pal['KNOB'][i], paper, 3.0))};" for i in range(4)))
    dimtext = ink_on(pal["SC_DIM"], pal["SC_BG"])
    for name, c in (("dim", dimtext), ("bone", pal["SC_BONE"])):
        if contrast(c, pal["SC_BG"]) < 4.5:
            die(f"{name} misses 4.5:1 on the screen's ground")
    css = CSS % dict(bg=hexc(pal["SC_BG"]), panel=hexc(pal["SC_PANEL"]), faint=hexc(pal["SC_FAINT"]),
                     mid=hexc(pal["SC_MID"]), dimtext=hexc(dimtext), bone=hexc(pal["SC_BONE"]),
                     k1=k[0], k2=k[1], k3=k[2], k4=k[3], light=light)
    open(os.path.join(out_dir, "sqrl.css"), "w", encoding="utf-8").write(css)
    for f in os.listdir(IMG):
        shutil.copyfile(os.path.join(IMG, f), os.path.join(out_dir, "img", f))
    mark_svg = open(os.path.join(SRC, "sqrl-mark.svg"), encoding="utf-8").read().strip()
    mark_svg = mark_svg.replace("<svg ", '<svg class="mark" aria-hidden="true" ', 1)
    order = [p for g in GROUPS for p in pages if p.group == g]
    for idx, p in enumerate(order):
        up = p.up()

        def link(href, up=up):
            if re.match(r"[a-z]+:", href) or href.startswith("#"):
                return href
            return re.sub(r"\.md(#|$)", r".html\1", href)
        SHOTS_DIR[0] = getattr(p, "shots", SHOTS)
        body = expand(p, getattr(p, "data", data), pal, False, link)
        # the way up to the site the manual is published in, at /sqrl/manual/
        nav = [f'<h2>Site</h2><ul><li><a href="{up}../">SQRL-1</a></li><li><a href="{up}../install/">Install</a></li></ul>']
        for g in GROUPS:
            items = []
            for q in [q for q in order if q.group == g]:
                ic = ""
                style = ""
                if q.nut:
                    ic = f'<img src="{up}img/glyph_{slug(q.nut["name"])}.svg" alt="">'
                    style = f' style="--navc:{q.nut["color"]}"'
                cur = ' aria-current="page"' if q is p else ""
                items.append(f'<li><a href="{up}{q.rel}.html"{cur}{style}>{ic}{html.escape(q.title)}</a></li>')
            nav.append(f"<h2>{g}</h2><ul>{''.join(items)}</ul>")
        if p.nut:
            c = rgb(p.nut["color"])
            if contrast(c, pal["SC_BG"]) < 4.5:
                die(f"{p.nut['name']}'s colour misses 4.5:1 on the screen's ground")
            lt = ink_on(c, paper)
            nutvars = (f'<style>:root{{--nut:{p.nut["color"]};--nuttext:{p.nut["color"]}}}'
                       f'@media (prefers-color-scheme:light){{:root:not([data-theme="dark"]){{--nut:{hexc(lt)};'
                       f'--nuttext:{hexc(lt)}}}}}:root[data-theme="light"]{{--nut:{hexc(lt)};--nuttext:{hexc(lt)}}}'
                       f'</style>')
            half = "SOUND" if p.nut["half"] == "sound" else "NOTE"
            status = (f'<img src="{up}img/glyph_{slug(p.nut["name"])}.svg" alt=""><span class="nm">{p.nut["name"]}'
                      f'</span><span>{half}</span><span>{html.escape(p.nut["about"])}</span>')
        else:
            nutvars = ""
            status = f'<span class="nm">SQRL-1</span><span>{html.escape(p.group)}</span>'
        prev = order[idx - 1] if idx else None
        nxt = order[idx + 1] if idx + 1 < len(order) else None
        pager = '<div class="pager">' + (f'<a href="{up}{prev.rel}.html">&larr; {html.escape(prev.title)}</a>' if prev
                                         else "<span></span>") + \
                (f'<a href="{up}{nxt.rel}.html">{html.escape(nxt.title)} &rarr;</a>' if nxt else "<span></span>") + "</div>"
        title = "SQRL-1 manual" if p.rel == "index" else f"{p.title}: SQRL-1 manual"
        desc = re.split(r"\n\n", p.text.split("\n", 1)[1].strip())[0]
        desc = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", desc).replace("**", "").replace("`", "")   # (markdown as text)
        desc = re.sub(r"\s+", " ", desc)[:200]
        doc = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title><meta name="description" content="{html.escape(desc)}">
<link rel="stylesheet" href="{up}sqrl.css">{nutvars}
<script>try{{var t=localStorage.getItem("sqrl-theme");if(t)document.documentElement.dataset.theme=t}}catch(e){{}}</script>
</head><body><div class="wrap">
<nav class="side" aria-label="Manual"><div class="top"><a class="brand" href="{up}index.html">
{mark_svg}<span><b>SQRL-1</b><span>manual, {html.escape(version)}</span></span></a>
<button type="button" class="menu" id="menu" aria-expanded="false">pages</button></div>
<div class="groups">{''.join(nav)}</div></nav>
<main><div class="status">{status}<span class="grow"></span><button type="button" id="theme">light / dark</button></div>
{body}
{pager}
<footer><p>SQRL-1 is a fork of <a href="https://github.com/hugelton/Felucca">Felucca</a> by Leo Kuroshita
(Hügelton Instruments), free software under GPL-3.0-only, with its source at
<a href="https://github.com/hed0rah/sqrl-1">github.com/hed0rah/sqrl-1</a>. Engines from databurn are MIT and the art
is CC-BY-SA-4.0. Every table and screen on these pages is generated from the firmware of
SQRL-1 {html.escape(version)}.</p></footer>
</main></div>
<script>
document.getElementById("menu").addEventListener("click",function(){{var n=this.closest("nav");n.classList.toggle("open");this.setAttribute("aria-expanded",n.classList.contains("open"))}});
document.getElementById("theme").addEventListener("click",function(){{var r=document.documentElement,d=r.dataset.theme||(matchMedia("(prefers-color-scheme: light)").matches?"light":"dark"),n=d==="light"?"dark":"light";r.dataset.theme=n;try{{localStorage.setItem("sqrl-theme",n)}}catch(e){{}}}});
</script></body></html>
"""
        path = os.path.join(out_dir, p.rel + ".html")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w", encoding="utf-8").write(doc)


def main():
    out_site = None
    if "--site" in sys.argv:
        out_site = sys.argv[sys.argv.index("--site") + 1]
    if not os.path.exists(DATA):
        die(f"no {DATA}: run tools/docs.sh")
    pal = palette()
    data = Data(json.load(open(DATA, encoding="utf-8")), pal)
    os.makedirs(IMG, exist_ok=True)
    try:
        rev = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        rev = "a local tree"
    version = data.d["version"]                     # sqrl_version.h: a release is just its number
    if version.endswith("-dev"):                    # (between releases the commit says which tree)
        version += f" ({rev})"
    pages = []
    for rel, g in STATIC:
        if os.path.exists(os.path.join(SRC, rel + ".md")):
            pages.append(Page(rel, g))
    for half, g in (("sound", "SOUND nuts"), ("note", "NOTE nuts")):
        for n in data.d[half]:
            rel = f"{half}/{slug(n['name'])}"
            if not os.path.exists(os.path.join(SRC, rel + ".md")):
                print(f"gen_docs: {rel}.md missing: skipped", file=sys.stderr)
                continue
            p = Page(rel, g)
            if p.title != n["name"]:
                die(f"{rel}.md: its title is {p.title}, the nut is {n['name']}")
            p.nut = data.nuts[n["name"]]
            pages.append(p)
    if os.path.exists(DATA_SIDE):                       # the sidelines: the SOUNDs this build leaves out
        side = Data(json.load(open(DATA_SIDE, encoding="utf-8")), pal, side=True)
        data.side_data = side
        for n in side.d["sound"]:
            if n["name"] in data.nuts or n["name"] not in SIDE:
                continue
            rel = f"sound/{slug(n['name'])}"
            if not os.path.exists(os.path.join(SRC, rel + ".md")):
                print(f"gen_docs: {rel}.md missing: skipped", file=sys.stderr)
                continue
            p = Page(rel, "On the sidelines")
            p.nut, p.data, p.shots = side.nuts[n["name"]], side, os.path.join(ROOT, "build", "docs", "shots_side")
            data.side_rows.append(n)
            pages.append(p)
    for p in pages:
        def link(href):
            return href
        SHOTS_DIR[0] = getattr(p, "shots", SHOTS)
        text = expand(p, getattr(p, "data", data), pal, True, link)
        banner = ("<!-- generated by tools/gen_docs.py from docs/src/" + p.rel + ".md and the firmware: "
                  "edit the source, not this file -->\n")
        path = os.path.join(OUT, p.rel + ".md" if p.rel != "index" else "README.md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if p.rel == "index":                            # (docs/README.md: GitHub shows it for docs/)
            text = text.replace("](", "](")
        if "<!-- auto:" in text:                        # (a marker the generator never reached: fail, not blank)
            die(f"{p.rel}: an auto marker was left unfilled: {text[text.index('<!-- auto:'):][:60]}")
        open(path, "w", encoding="utf-8").write(banner + text)
    if out_site:
        site(pages, data, pal, out_site, version, rev)
    print(f"gen_docs: {len(pages)} pages, {len(os.listdir(IMG))} images" + (f", site in {out_site}" if out_site else ""))


if __name__ == "__main__":
    main()
