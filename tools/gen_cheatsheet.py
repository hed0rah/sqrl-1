#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hed0rah
"""gen_cheatsheet.py [--check]: the quick sheet, web/sheet/index.html, two landscape pages (front, back) to print.

Its fonts are web/sheet/fonts/ (Inter Tight and DejaVu Sans Mono, cut to the characters the sheet uses); make_site.py
serves the folder at sheet/. --check writes nothing and fails if the committed page is stale."""
import html, math, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "web" / "sheet" / "index.html"

# the shell's own colours (firmware/src/shell.c SH_MODES, SH_NOTES, SC_KNOB)
NUT = {
    "FM6": (216, 172, 79, 6), "LIZARD": (95, 184, 168, 4), "SHOCK": (222, 112, 96, 5), "DRUMS": (224, 137, 74, 8),
    "MOUTH": (240, 132, 186, 7), "ANALOG": (214, 196, 120, 4), "PHASE": (150, 200, 120, 5), "PHYS": (140, 200, 200, 6),
    "KEYS": (196, 196, 186, 0), "CHORD": (215, 122, 147, 0), "LINES": (120, 206, 176, 0), "TONNETZ": (120, 190, 225, 0),
    "RUDIMENT": (224, 137, 74, 0),
}

WHITES = ["F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"]
BLACK_AFTER = {"F", "G", "A", "C", "D"}


def blacks():
    """(name, left white index) for each black key from F3 to G5"""
    out = []
    for i, w in enumerate(WHITES[:-1]):
        if w[0] in BLACK_AFTER:
            out.append((w[0] + "#" + w[1], i))
    return out

# digits as outlines, placed by their ink: build time only (fontTools), so the sheet needs no font for them and
# screen and print draw the same shapes. The repo's Inter Tight, instanced at the weight asked
FONT = ROOT / "assets" / "fonts" / "InterTight[wght].ttf"
_INST = {}


def _font(weight):
    if weight not in _INST:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
        _INST[weight] = instancer.instantiateVariableFont(TTFont(FONT), {"wght": weight})
    return _INST[weight]


def ink_text(text, size, cx, cy, weight=600, track=0.0):
    """text as one path of size `size` (user units), its ink bounding box centred on (cx, cy)"""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.transformPen import TransformPen
    f = _font(weight)
    gs, cmap, hmtx = f.getGlyphSet(), f.getBestCmap(), f["hmtx"]
    k = size / f["head"].unitsPerEm
    # lay the glyphs out in font units (y up), then bound them
    x, items = 0.0, []
    for ch in text:
        g = cmap[ord(ch)]
        items.append((g, x))
        x += hmtx[g][0] + track / k
    bp = BoundsPen(gs)
    for g, gx in items:
        gs[g].draw(TransformPen(bp, (1, 0, 0, 1, gx, 0)))
    x0, y0, x1, y1 = bp.bounds
    # font units to user units: scale k, flip y, ink centre onto (cx, cy)
    ox, oy = cx - (x0 + x1) / 2 * k, cy + (y0 + y1) / 2 * k
    sp = SVGPathPen(gs, ntos=lambda v: f"{v:.3f}".rstrip("0").rstrip("."))
    for g, gx in items:
        gs[g].draw(TransformPen(sp, (k, 0, 0, -k, ox + gx * k, oy)))
    return sp.getCommands()


def poly(cx, cy, r, sides, rot=-90.0):
    import math
    if sides == 0:
        return f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}"/>'
    pts = []
    for k in range(sides):
        a = math.radians(rot + 360.0 * k / sides)
        pts.append(f"{cx + r * math.cos(a):.2f},{cy + r * math.sin(a):.2f}")
    return f'<polygon points="{" ".join(pts)}"/>'


def rgb(name):
    r, g, b, _ = NUT[name]
    return f"rgb({r},{g},{b})"


def keyboard(x, y, w, h, white_lbl=None, black_lbl=None, mark=None, wl_rot=False, nut_white=None, nut_black=None,
             black_h=0.6, cls="kb"):
    """an F3..G5 keyboard. white_lbl / black_lbl: note -> label; mark: set of notes drawn lit"""
    white_lbl = white_lbl or {}
    black_lbl = black_lbl or {}
    nut_white = nut_white or {}
    nut_black = nut_black or {}
    mark = mark or set()
    kw = w / len(WHITES)
    s = [f'<g class="{cls}">']
    for i, n in enumerate(WHITES):
        kx = x + i * kw
        lit = " lit" if n in mark else ""
        s.append(f'<rect class="wk{lit}" x="{kx:.2f}" y="{y:.2f}" width="{kw:.2f}" height="{h:.2f}"/>')
        if n in nut_white:
            nm = nut_white[n]
            if nm:
                s.append(f'<g fill="{rgb(nm)}" stroke="none">{poly(kx + kw / 2, y + h - 9.2, 2.1, NUT[nm][3])}</g>')
        if n in white_lbl:
            t = html.escape(white_lbl[n])
            if wl_rot:
                s.append(f'<text class="wl rot" transform="translate({kx + kw / 2 + 0.727 * 1.7 / 2:.2f},{y + h - 2.4:.2f}) rotate(-90)">{t}</text>')
            else:
                s.append(f'<text class="wl" x="{kx + kw / 2:.2f}" y="{y + h - 2.2:.2f}">{t}</text>')
    bw = kw * 0.62
    for n, i in blacks():
        bx = x + (i + 1) * kw - bw / 2
        lit = " lit" if n in mark else ""
        s.append(f'<rect class="bk{lit}" x="{bx:.2f}" y="{y:.2f}" width="{bw:.2f}" height="{h * black_h:.2f}"/>')
        if n in nut_black:
            nm = nut_black[n]
            s.append(f'<rect x="{bx + 0.7:.2f}" y="{y + h * black_h - 2.2:.2f}" width="{bw - 1.4:.2f}" height="1.2" fill="{rgb(nm)}"/>')
        if n in black_lbl:
            t = html.escape(black_lbl[n])
            s.append(f'<text class="bl" transform="translate({bx + bw / 2 + 0.727 * 1.75 / 2:.2f},{y + h * black_h - 3.6:.2f}) rotate(-90)">{t}</text>')
    s.append("</g>")
    return "".join(s)


def note_names(x, y, w):
    kw = w / len(WHITES)
    return "".join(f'<text class="nn" x="{x + (i + 0.5) * kw:.2f}" y="{y:.2f}">{n}</text>' for i, n in enumerate(WHITES))


# ---------------------------------------------------------------- the front's panel map (mm, viewBox = the page box)
def panel():
    W, H = 255, 88
    s = [f'<svg class="panel" viewBox="0 0 {W} {H}" width="{W}mm" height="{H}mm" role="img" '
         'aria-label="The FM-1 panel: encoders, screen, knobs, buttons and keys, with the default wheel">']
    # outline, drawn as a part drawing: a body, a centre line, the datum
    s.append('<rect class="body" x="0.4" y="0.4" width="254.2" height="87.2" rx="3"/>')
    # left block: encoders
    def enc(cx, cy, label, sub=None, r=4.6):
        out = [f'<circle class="enc" cx="{cx}" cy="{cy}" r="{r}"/>',
               f'<line class="tick" x1="{cx}" y1="{cy - r + 1}" x2="{cx}" y2="{cy - r + 2.6}"/>',
               f'<text class="cap" x="{cx}" y="{cy + 4.6 + 4}">{label}</text>']
        if sub:
            out.append(f'<text class="sub" x="{cx}" y="{cy + 4.6 + 7.4}">{sub}</text>')
        return "".join(out)
    s.append(enc(12.8, 12, "MASTER", "volume", r=3.8))
    s.append(enc(34, 12, "SELECT", "pages"))
    s.append(enc(12.8, 33, "PRESETS", "sounds"))
    s.append(enc(34, 33, "ALGORITHM", "character"))
    def btn(x, y, label, cls="btn", w=13.6, h=5.4):
        return (f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="1.2"/>'
                f'<text class="bt" x="{x + w / 2}" y="{y + h / 2 + 1.25}">{label}</text>')
    s.append(btn(6, 46.4, "OCT-", "btn press"))
    s.append(btn(27.2, 46.4, "OCT+", "btn press"))
    # screen
    s.append('<rect class="screen" x="52" y="5" width="44" height="44" rx="1"/>')
    s.append('<text class="scr" x="54" y="10.2">SOUND FM6 MAIN</text><text class="scr r" x="94" y="10.2">120</text>')
    s.append('<line class="scrl" x1="54" y1="12" x2="94" y2="12"/>')
    s.append(f'<g fill="none" stroke="{rgb("FM6")}" stroke-width="0.35">{poly(74, 25.5, 9, 6)}{poly(74, 25.5, 5, 6, -60)}</g>')
    for k, c in enumerate(["k1", "k2", "k3", "k4"]):
        x = 54 + k * 10.2
        s.append(f'<rect class="card" x="{x}" y="39.6" width="9.4" height="7.4" rx="0.6"/>'
                 f'<circle class="dot {c}" cx="{x + 1.8}" cy="41.6" r="0.8"/>')
    # knobs
    for k, c in enumerate(["k1", "k2", "k3", "k4"]):
        cx = 117.8 + k * 20
        s.append(f'<circle class="enc" cx="{cx}" cy="11" r="4.6"/><line class="tick" x1="{cx}" y1="7.4" x2="{cx}" y2="9"/>'
                 f'<circle class="dot {c}" cx="{cx + 5.6}" cy="6.6" r="1.1"/>'
                 f'<text class="cap" x="{cx}" y="20">KNOB {k + 1}</text>')
    # buttons, as start.md draws them
    row1 = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"]
    row2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
    press = {"PLAY", "REC"}
    for i, b in enumerate(row1):
        s.append(btn(104 + i * 14.8, 26, b))
    for i, b in enumerate(row2):
        s.append(btn(104 + i * 14.8, 35.4, b, "btn press" if b in press else "btn"))
    s.append('<text class="sub l" x="104" y="46.6">PLAY, REC, OCT- and OCT+ act the moment they go down; the rest act on release</text>')
    # pages a button walks, as dimension callouts
    bx, by = 194.4, 5
    s.append(f'<rect class="ckbox" x="{bx}" y="{by}" width="54.6" height="44" rx="1.2"/>'
             f'<text class="ckt" x="{bx + 3}" y="{by + 5.0}">CHANGE A KEY</text>'
             f'<text class="cks" x="{bx + 3}" y="{by + 8.5}">put any NOTE engine on a black key</text>')
    steps = [("1", "hold HOME"), ("2", "hold GLO + a black key"), ("3", "turn SELECT: the catalogue"),
             ("4", "let go of GLO, then HOME")]
    for i, (n, t) in enumerate(steps):
        y = by + 13.6 + i * 4.75
        s.append(f'<circle class="ckn" cx="{bx + 4.6}" cy="{y - 0.8}" r="1.9"/><path class="cknt" d="{ink_text(n, 2.1, bx + 4.6, y - 0.8, 700)}"/>'
                 f'<text class="ckl" x="{bx + 8.4}" y="{y}">{t}</text>')
    s.append(f'<line class="ckr" x1="{bx + 3}" y1="{by + 31.4}" x2="{bx + 51.6}" y2="{by + 31.4}"/>'
             f'<text class="cks" x="{bx + 3}" y="{by + 35.0}">Let go of HOME first: no change.</text>'
             f'<text class="cks" x="{bx + 3}" y="{by + 38.2}">The black keys, or their default: the installer\'s</text>'
             f'<text class="cks b" x="{bx + 3}" y="{by + 41.4}">Set up your wheel</text>')
    # keys
    kx, ky, kw, kh = 6, 58, 243, 24
    wl = {"C4": "FM6", "D4": "LIZARD", "E4": "SHOCK", "F4": "DRUMS", "G4": "MOUTH", "A4": "ANALOG", "B4": "PHASE",
          "C5": "PHYS"}
    bl = {"C#4": "KEYS", "D#4": "CHORD", "F#4": "LINES", "G#4": "TONNETZ", "A#4": "RUDIMENT"}
    nw = {k: (v if v != "spare" else None) for k, v in wl.items()}
    s.append(keyboard(kx, ky, kw, kh, white_lbl=wl, black_lbl=bl, wl_rot=False,
                      nut_white=nw, nut_black=bl, black_h=0.62, mark=set(wl) | set(bl)))
    s.append(note_names(kx, ky + kh + 3.6, kw))
    # the wheel's span, a dimension line
    w1 = kx + 4 * kw / 16
    w2 = kx + 12 * kw / 16
    s.append(f'<line class="dim" x1="{w1}" y1="55.8" x2="{w2}" y2="55.8"/>'
             f'<line class="dim" x1="{w1}" y1="54.2" x2="{w1}" y2="57.4"/><line class="dim" x1="{w2}" y1="54.2" x2="{w2}" y2="57.4"/>'
             f'<text class="dimt" x="{(w1 + w2) / 2}" y="54.6">HOME held: the wheel, C4 to C5. white = the eight SOUNDs, black = NOTE. to change a black key: above right</text>')
    s.append("</svg>")
    return "".join(s)


# ---------------------------------------------------------------- glyphs: the sheet's notation
GLYPHS = """
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
 <defs>
  <symbol id="g-tap" viewBox="0 0 12 12"><circle cx="6" cy="6" r="2.4" class="gf"/></symbol>
  <symbol id="g-press" viewBox="0 0 12 12"><path d="M2.6 3.03 H9.4 L6 9.03 Z" class="gf"/></symbol>
  <symbol id="g-hold" viewBox="0 0 12 12"><circle cx="3.48" cy="6" r="2.4" class="gf"/><path d="M3.48 6 H10.48 M10.48 3.8 V8.2" class="gs"/></symbol>
  <symbol id="g-r1" viewBox="0 0 12 12"><circle cx="6" cy="6" r="1.6" class="gf"/><circle cx="6" cy="6" r="3.6" class="gs"/></symbol>
  <symbol id="g-r2" viewBox="0 0 12 12"><circle cx="6" cy="6" r="1.4" class="gf"/><circle cx="6" cy="6" r="3.2" class="gs"/><circle cx="6" cy="6" r="4.9" class="gs"/></symbol>
  <symbol id="g-r3" viewBox="0 0 12 12"><circle cx="6" cy="6" r="1.2" class="gf"/><circle cx="6" cy="6" r="2.7" class="gs"/><circle cx="6" cy="6" r="4.1" class="gs"/><circle cx="6" cy="6" r="5.5" class="gs"/></symbol>
  <symbol id="g-turn" viewBox="0 0 16 15"><g transform="translate(0 -0.4)"><circle cx="8" cy="9.1" r="3.6" class="gs"/><path d="M8 9.1 V5.50" class="gs"/><path d="M2.36 7.05 A6 6 0 0 1 13.64 7.05" class="gs"/><path d="M14.29 8.83 L14.80 5.99 L12.07 6.98 Z" class="gf"/><path d="M1.71 8.83 L1.20 5.99 L3.93 6.98 Z" class="gf"/></g></symbol>
  <symbol id="g-turnr" viewBox="0 0 16 15"><g transform="translate(0 -0.4)"><circle cx="6.5" cy="9.1" r="3.6" class="gs"/><path d="M6.5 9.1 V5.50" class="gs"/><path d="M5.98 3.12 A6 6 0 0 1 12.30 7.55" class="gs"/><path d="M12.79 9.38 L13.54 6.59 L10.74 7.34 Z" class="gf"/></g></symbol>
  <symbol id="g-turnl" viewBox="0 0 16 15"><g transform="translate(0 -0.4)"><g transform="translate(16 0) scale(-1 1)"><circle cx="6.5" cy="9.1" r="3.6" class="gs"/><path d="M6.5 9.1 V5.50" class="gs"/><path d="M5.98 3.12 A6 6 0 0 1 12.30 7.55" class="gs"/><path d="M12.79 9.38 L13.54 6.59 L10.74 7.34 Z" class="gf"/></g></g></symbol>
  <symbol id="g-twice" viewBox="0 0 12 12"><circle cx="3.4" cy="6" r="2.1" class="gf"/><circle cx="8.6" cy="6" r="2.1" class="gf"/></symbol>
  <symbol id="g-key" viewBox="0 0 12 12"><rect x="3" y="1.4" width="6" height="9.2" rx="0.6" class="gs"/><rect x="3" y="1.4" width="6" height="3" class="gf"/></symbol>
 </defs>
</svg>
"""


def g(name, cls=""):
    if name.startswith("turn"):
        return f'<svg class="gl t {cls}" viewBox="0 0 16 15" aria-hidden="true"><use href="#g-{name}"/></svg>'
    return f'<svg class="gl {cls}" viewBox="0 0 12 12" aria-hidden="true"><use href="#g-{name}"/></svg>'


def b(name, held=False):
    """a button chip; held: the bar under it says it stays down"""
    return f'<span class="b{" h" if held else ""}">{name}</span>'


def e(name, d=""):
    return f'<span class="e">{g({"l": "turnl", "r": "turnr"}.get(d, "turn"))}{name}</span>'


def k(name):
    return f'<span class="k">{name}</span>'


P = '<span class="p">+</span>'
TO = '<span class="to">→</span>'


def row(how, what):
    return f'<div class="row"><div class="how">{how}</div><div class="what">{what}</div></div>'


def sec(num, title, body, cls=""):
    # the badge: ring and digits in one box, the digits' ink centred on the ring's centre
    badge = (f'<svg class="nb" viewBox="0 0 10 10" aria-hidden="true"><circle cx="5" cy="5" r="4.55"/>'
             f'<path class="nbd" d="{ink_text(num, 4.6, 5, 5)}"/></svg>')
    return (f'<section class="sec {cls}"><h2>{badge}<span class="ht">{title}</span></h2>{body}</section>')


# ---------------------------------------------------------------- front
def legend():
    items = [
        (g("tap"), "tap", "under 300 ms, on release"),
        (g("press"), "press", "acts as it goes down"),
        (g("hold"), "hold", "300 ms, or with another control: a modifier"),
        (g("r1") + g("r2"), "long", "1.2 s a ring, for what destroys"),
        (g("turn"), "turn", "an encoder or knob, either way"),
        (g("turnr"), "right", "clockwise"),
        (g("turnl"), "left", "anticlockwise"),
        (b("REC", True), "held", "the bar: keep it down"),
    ]
    return '<div class="legend">' + "".join(
        f'<div class="lg"><span class="lgg">{a}</span><span class="lgn">{n}</span><span class="lgd">{d}</span></div>'
        for a, n, d in items) + "</div>"


def front():
    grammar = "".join([
        row(g("tap") + b("HOME"), "TAB: SOUND or NOTE view, where you left it"),
        row(g("twice") + b("HOME"), "the pair playing behind, same view"),
        row(g("tap") + b("EDIT") + b("ENV") + b("LFO") + b("FX"), "that family's pages; again: the next"),
        row(g("tap") + b("ARP") + b("SEQ"), "ARP: the arp, MODE first; again ARP 2; twice quickly HOLD. SEQ: the pattern"),
        row(g("tap") + b("SCL"), "SCALE for every nut; again: back"),
        row(g("hold") + b("SCL"), "playing: FILL while held"),
        row(b("GLO", True) + P + g("tap") + "&nbsp;any", "a card of what it does here"),
        row(b("ARP", True) + "&nbsp;1.2 s", "HOLD on/off; no arp: keys latch"),
        row(b("SEQ", True) + P + "&nbsp;white keys", "enter steps; or hold a step: C#5 ACC, D#5 SLD, F#5 TIE"),
        row(b("SEQ", True) + P + k("C#5") + k("D#5"), "step pages back, forward (lanes, SEQ page: no hold); F#5 FOLLOW"),
        row(b("SEQ", True) + P + "PRESETS", "NEXT: a slot's pattern on the next bar, SOUND stays"),
        row("a step held" + P + "knobs", "CHNC RATCH NUDGE VEL; on EDIT ENV LFO FX: a lock on that step"),
        row(b("SEQ", True) + P + g("press") + b("OCT+") + "&nbsp;/" + g("tap") + b("OCT-"), "DOUBLE / HALF the loop, 1 to 64 steps"),
        row(b("REC", True) + P + g("press") + b("OCT+"), "FREEZE a generator into lanes"),
        row(g("tap") + b("SEQ") + "&nbsp;on SEQ", "EVOLVE: EVERY, AMT, WHAT / MOVE, BACK"),
        row(g("press") + b("OCT+") + "&nbsp;" + b("OCT-"), "octave (LINES: the lanes' octave page); a question: yes / no"),
    ])
    pairs = "".join([
        row(b("HOME", True) + P + k("white") + P + k("black"), "pick SOUND and NOTE, let go: it plays"),
        row(b("HOME", True) + P + g("tap") + b("GLO"), "the PAIR page: KNOB 1 SOUND, 2 NOTE, 3 PRESET, 4 MUTE"),
        row(b("HOME", True) + P + "&nbsp;pair keys&nbsp;" + P + g("press") + b("PLAY"), "mute that part (a take, BEAT, LINE); again: back"),
        row(b("HOME", True) + P + "&nbsp;centre&nbsp;" + P + g("press") + b("PLAY"), "stop everything"),
        row(b("GLO", True) + P + k("C4-C5"), "white: only the SOUND; black: only the NOTE"),
        row(b("HOME", True) + P + g("press") + b("OCT-"), "close the wheel, stay where you were"),
    ])
    glo = "".join([
        row(g("tap") + b("GLO"), "SYSTEM " + TO + " SYSTEM 2 " + TO + " PROJECTS " + TO + " ABOUT " + TO + " LEGAL"),
        row(b("GLO", True), "SHIFT: a card lists what it adds"),
        row(b("GLO", True) + P + e("SELECT"), "the tempo"),
        row(e("SELECT"), "the pair's pages, both ways"),
        row(e("SELECT", "l") + "&nbsp;from HOME", "one click: the last page (DRUM, on DRUMS)"),
        row(e("PRESETS"), "the SOUND's sounds, yours after the factory ones"),
        row(e("ALGORITHM"), "the nut's character: FM6's algorithm, CHORD's voicing"),
    ])
    return f"""
<div class="page front">
  <header class="top">
    <div class="ttl"><span class="mark">SQRL-1</span><span class="for">for the M-VAVE FM-1</span><span class="ver">quick sheet / {VERSION} / front</span></div>
    {legend()}
  </header>
  <div class="map">{panel()}</div>
  <div class="cols3">
    {sec("01", "The grammar", grammar)}
    {sec("02", "Pairs and the wheel", pairs)}
    {sec("03", "GLO and the encoders", glo)}
  </div>
  <footer class="foot"><span>a pair: a SOUND makes the tone, a NOTE engine decides the notes</span><span>the knob dots, left to right: <i class="dot k1"></i>blue <i class="dot k2"></i>green <i class="dot k3"></i>bone <i class="dot k4"></i>orange, KNOB 1 to 4</span></footer>
</div>"""


# ---------------------------------------------------------------- back
def rings():
    W, H = 74, 35
    s = [f'<svg class="rings" viewBox="0 0 {W} {H}" width="{W}mm" height="{H}mm" role="img" aria-label="REC held: two rings">']
    cx, cy = 17.4, 17.2
    labels = [("1", "let go: undo; notes, LEN, knob moves (09)"),
              ("2", "clear notes, knob moves; RUDIMENT: the lane in front")]
    for i, r in enumerate([8.0, 14.0]):
        s.append(f'<circle class="ring r{i + 1}" cx="{cx}" cy="{cy}" r="{r}"/>')
    s.append(f'<circle class="ringc" cx="{cx}" cy="{cy}" r="3"/><text class="ringt" x="{cx}" y="{cy + 1.1}">REC</text>')
    for i, (n, t) in enumerate(labels):
        r = [8.0, 14.0][i]
        a = math.radians([-25, 25][i])
        px, py = cx + r * math.cos(a), cy + r * math.sin(a)
        ex, ey = cx + 17.6 * math.cos(a), cy + 17.6 * math.sin(a)
        s.append(f'<polyline class="lead" points="{px:.2f},{py:.2f} {ex:.2f},{ey:.2f} 37,{ey:.2f}"/>'
                 f'<circle class="leadd" cx="{px:.2f}" cy="{py:.2f}" r="0.55"/>'
                 f'<path class="rn" d="{ink_text(n, 2.6, 39.4, ey, 700)}"/>'
                 f'<text class="rt" x="42" y="{ey + 0.8:.2f}">{t.split(";")[0]}</text>')
        if ";" in t:
            s.append(f'<text class="rt dimx" x="42" y="{ey + 4.2:.2f}">{t.split(";")[1].strip()}</text>')
    s.append("</svg>")
    return "".join(s)


def lanes_kb():
    W, H = 168, 35
    s = [f'<svg class="lanes" viewBox="0 0 {W} {H}" width="{W}mm" height="{H}mm" role="img" aria-label="RUDIMENT and LINES on the keys">']
    kx, ky, kw, kh = 1, 5.4, 166, 22.4
    bl = {"F#3": "1 KICK", "G#3": "2 SNARE", "A#3": "3 CLAP", "C#4": "4 HATCL", "D#4": "5 HATOP", "F#4": "6 TOM",
          "G#4": "7 RIM", "A#4": "8 BELL"}
    wl = {n: str(i + 1) for i, n in enumerate(WHITES)}
    s.append(keyboard(kx, ky, kw, kh, white_lbl=wl, black_lbl=bl, mark=set(bl), black_h=0.66))
    s.append(note_names(kx, ky + kh + 3.4, kw))
    s.append(f'<text class="dimt l" x="{kx}" y="3.6">black: the 8 lanes (the drum each strikes under DRUMS; under any other SOUND, a scale degree up from ROOT)</text>')
    s.append("</svg>")
    return "".join(s)


def fx_kb():
    W, H = 80, 30
    s = [f'<svg class="fxkb" viewBox="0 0 {W} {H}" width="{W}mm" height="{H}mm" role="img" aria-label="The FX layer on the keys">']
    kx, ky, kw, kh = 0.5, 1, 79, 22
    wl = {"F3": "RPT 1/8", "G3": "RPT 1/16", "A3": "RPT 1/32", "B3": "REVERSE", "C4": "LPF", "D4": "HPF",
          "E4": "TAPE STOP", "F4": "FREEZE", "G4": "OCT UP", "A4": "OCT DN", "B4": "FLANGER", "C5": "PHASER",
          "D5": "REV THROW", "E5": "ECHO THROW"}
    bl = {"F#3": "MUTE 1", "G#3": "MUTE 2", "A#3": "MUTE 3", "C#4": "MUTE 4"}
    s.append(keyboard(kx, ky, kw, kh, white_lbl=wl, black_lbl=bl, wl_rot=True, mark=set(bl), black_h=0.5, cls="kb small"))
    s.append(note_names(kx, ky + kh + 3.2, kw).replace('class="nn"', 'class="nn s"'))
    s.append("</svg>")
    return "".join(s)


def back():
    rec = "".join([
        row(g("press") + b("REC"), "arm the part in front; playing: overdub on or off; twice quickly: knobs record or not"),
        row(b("REC", True) + P + g("press") + b("PLAY"), "arm, count in a bar, record; playing: arm"),
        row(b("REC", True) + P + k("white"), "erase that step"),
        row(b("REC", True) + P + e("SELECT"), "REC MODE: ADD or REPLACE, each + KNOBS"),
        row("ADD / REPLACE", "notes join, a tie stops at a note / this pass's win"),
        row(g("tap") + b("REC"), "close the take; the first sets the loop, whole bars"),
        row("arp armed", "rewrites its steps each pass; LINE is never recorded"),
        row("a knob won't stay put", "it has recorded moves: UNDO TAKE, or clear with ring 2"),
    ])
    lanes = "".join([
        row(g("tap") + k("black"), "the lane in front"),
        row(g("tap") + k("white"), "its steps on and off; held: C#5 ACC, D#5 SLD, F#5 TIE, lane keys stack hits"),
        row(e("KNOB 2-4"), "the lane's LEN (=LEN: the SEQ page's; below: its own), FILL, ROT, on HOME; FILL left of 0: AND OR XOR NOR A-B FLIP LATCH of the two lanes above"),
        row(e("ALGORITHM"), "RUDIMENT: a RHYTHM; LINES: a LINE (DENS, SPAN on LINE); left: EMPTY"),
        row(b("lane", True) + P + e("KNOB 1-4"), "that lane's DIV LEN FILL ROT, any page, live"),
        row(b("lane", True) + P + e("ALGORITHM"), "nudge its hits a step (its ROT)"),
        row(b("lane", True) + P + e("PRESETS"), "mutate: one hit moves to a neighbour"),
        row(b("SEQ", True) + P + k("white"), "ACCENT (on a hit) " + TO + " SLIDE " + TO + " ACCENT SLIDE " + TO + " TIE " + TO + " plain; under DRUMS: ACCENT only"),
        row(b("REC", True) + P + k("white"), "erase that step, every lane"),
        row(b("REC", True) + P + k("black"), "stopped: clear the lane; playing: as it passes"),
        row(g("tap") + b("ARP") + "&nbsp;page, NOTE", "the lane's own note; OCT moves them all"),
    ])
    drum = "".join([
        row(e("SELECT", "l") + "&nbsp;from HOME", "one click: DRUM, the last page; or tap EDIT on"),
        row("DRUM", "TUNE DECY TONE SNAP: one drum, on top of the kit"),
        row("which drum", "RUDIMENT: the lane in front; KEYS: the one struck last"),
        row("KIT", "DIGI: SQRL-1's own, simple enough to design from"),
    ])
    fx = "".join([
        row(g("tap") + b("FX"), "FX, INSERT, INSERT 2, then the shared ALL pages"),
        row(b("FX", True) + P + k("keys"), "effects on the whole mix while held; the keys, by default:"),
        row(k("F3-B3"), "REPEAT 1/8, 1/16, 1/32, REVERSE"),
        row(k("C4-F4"), "LPF, HPF, TAPE STOP, FREEZE"),
        row(k("G4-C5"), "OCT UP, OCT DN, FLANGER, PHASER"),
        row(k("D5 E5"), "REV THROW, ECHO THROW"),
        row(k("F#3-C#4") + "&nbsp;black", "mute parts 1 to 4"),
    ])
    save = "".join([
        row(g("tap") + b("SAVE") + TO + g("press") + b("OCT+"), "store the sound; stop the transport first"),
        row(b("SAVE", True) + P + e("knobs"), "try: let go and it snaps back"),
        row(b("SAVE", True) + P + g("press") + b("OCT+"), "keep what you tried"),
        row(g("tap") + b("GLO") + "&nbsp;&times;3", "PROJECTS, slots A to D, then NEW: " + e("KNOB 1")),
        row(g("press") + b("OCT+") + "&nbsp;" + b("OCT-"), "save / load, asked: OCT+ again is yes"),
    ])
    undo = "".join([
        row("UNDO TAKE", "the last take, or a clear"),
        row("UNDO BEAT", "a RHYTHM, FILL, ROT, an erase"),
        row("UNDO SOUND", "PRESETS replaced your sound"),
        row("after ring 2", "notes and knob moves come back"),
    ])
    lights = "".join([
        row("REC blinks", "armed"), row("PLAY green", "running"),
        row("HOME glows", "a part muted"), row("OCT+ breathes", "a question: yes"),
    ])
    return f"""
<div class="page back">
  <header class="top slim">
    <div class="ttl"><span class="mark">SQRL-1</span><span class="for">recording, lanes, drums, effects</span><span class="ver">quick sheet / {VERSION} / back</span></div>
  </header>
  <div class="btop">
    <div class="c-rec">{sec("04", "Recording", rec + '<div class="fig">' + rings() + "</div>")}</div>
    <div class="c-lanes">{sec("05", "Lanes: RUDIMENT " + k("A#4") + " and LINES " + k("F#4"), '<div class="fig">' + lanes_kb() + '</div><div class="two">' + lanes + "</div>")}</div>
  </div>
  <div class="bbot">
    <div>{sec("06", "The DRUM page, DRUMS " + k("F4"), drum + '<p class="tip">a 303 line: LINES on a sound with VCE on MONO or LEG and GLIDE up; a SLIDE glides into the next step</p>')}</div>
    <div>{sec("07", "Effects", fx, "fx")}</div>
    <div>{sec("08", "Save, try, projects", save)}</div>
    <div>{sec("09", "REC's first ring undoes", undo)}{sec("10", "Lights", lights, "lights")}</div>
  </div>
  <footer class="foot"><span>hold GLO and tap any button for its card</span><span>the manual: databurn.org/sqrl/manual</span></footer>
</div>"""


CSS = r"""
@font-face { font-family: "Inter Tight"; src: url("fonts/InterTight-subset.woff") format("woff"); font-weight: 100 900; }
@font-face { font-family: "DejaVu Sans Mono"; src: url("fonts/DejaVuSansMono-subset.woff") format("woff"); font-weight: 400; }
@font-face { font-family: "DejaVu Sans Mono"; src: url("fonts/DejaVuSansMono-Bold-subset.woff") format("woff"); font-weight: 600 700; }
:root {
  --paper: #f3f1ec; --ink: #16181b; --ink2: #4a5058; --ink3: #8a9099; --rule: #c9c5bb; --rule2: #e2ded5;
  --wk: #ffffff; --bk: #1d2024; --bkt: #e6e1d3; --lit: #fbe6cf; --chip: #ffffff; --held: #16181b;
  --k1: rgb(106,169,220); --k2: rgb(143,191,106); --k3: rgb(230,225,211); --k4: rgb(224,137,74);
  --accent: rgb(224,137,74); --screen: #07080a; --scr: rgb(230,225,211); --desk: #dcd8cf;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --paper: #121418; --ink: #e6e1d3; --ink2: #aab0b8; --ink3: #7d858d; --rule: #3a4048; --rule2: #24282e;
  --wk: #1c1f24; --bk: #050607; --bkt: #c9c5bb; --lit: #3a2a1c; --chip: #1c1f24; --held: #e6e1d3; --desk: #07080a; } }
:root[data-theme="dark"] {
  --paper: #121418; --ink: #e6e1d3; --ink2: #aab0b8; --ink3: #7d858d; --rule: #3a4048; --rule2: #24282e;
  --wk: #1c1f24; --bk: #050607; --bkt: #c9c5bb; --lit: #3a2a1c; --chip: #1c1f24; --held: #e6e1d3; --desk: #07080a; }
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--desk); color: var(--ink); }
body { font: 400 2.55mm/1.32 "Inter Tight", system-ui, sans-serif; padding: 10mm 0; }
.bar { width: min(255mm, calc(100vw - 32px)); margin: 0 auto 6mm; display: flex; justify-content: space-between; align-items: center; font-size: 13px; color: var(--ink2); gap: 12px; flex-wrap: wrap; }
.bar button { font: inherit; border: 1px solid var(--rule); background: var(--paper); color: var(--ink); border-radius: 4px; padding: 4px 10px; cursor: pointer; }
.sheetwrap { overflow-x: auto; padding: 0 16px; }
.page { width: 255mm; height: 185mm; margin: 0 auto 10mm; background: var(--paper); position: relative; padding: 5mm 5.5mm 4mm;
  box-shadow: 0 1px 2px rgba(0,0,0,.12), 0 8px 28px rgba(0,0,0,.10); display: flex; flex-direction: column; overflow: hidden; }
/* registration marks: an engineering sheet's corners */
.page::before, .page::after { content: ""; position: absolute; width: 4mm; height: 4mm; border: 0 solid var(--ink3); }
.page::before { top: 1.6mm; left: 1.6mm; border-top-width: .2mm; border-left-width: .2mm; }
.page::after { bottom: 1.6mm; right: 1.6mm; border-bottom-width: .2mm; border-right-width: .2mm; }
.top { display: flex; justify-content: space-between; align-items: flex-start; gap: 4mm; border-bottom: .25mm solid var(--ink); padding-bottom: 1.6mm; margin-bottom: 2.2mm; }
/* one header height on both sides: the rules line up front to back, held to the light */
.top { height: 15.9mm; }
.top.slim { margin-bottom: 2.2mm; }
.ttl { display: flex; flex-direction: column; gap: .3mm; min-width: 44mm; }
.mark { font: 700 6.6mm/1 "Inter Tight", sans-serif; letter-spacing: .05em; }
.for { font-size: 2.6mm; color: var(--ink2); }
.ver { font: 400 2.1mm/1.2 "DejaVu Sans Mono", monospace; color: var(--ink3); letter-spacing: .04em; }
.legend { flex: 1; display: grid; grid-template-columns: repeat(4, 1fr); gap: .8mm 5mm; align-items: center; }
.lg { display: grid; grid-template-columns: 12mm 12.5mm 1fr; align-items: center; gap: 1mm; font-size: 2.25mm; }
.lgg { display: flex; align-items: center; gap: .4mm; }
.lgn { font-weight: 600; text-transform: uppercase; letter-spacing: .06em; font-size: 2.05mm; }
.lgd { color: var(--ink2); }
.gl { width: 3.2mm; height: 3.2mm; flex: none; vertical-align: -0.7mm; }
.gf { fill: var(--ink); } .gs { fill: none; stroke: var(--ink); stroke-width: .9; stroke-linecap: round; }
.b, .e, .k { display: inline-flex; align-items: center; gap: .5mm; font: 600 2.05mm/1 "DejaVu Sans Mono", monospace; letter-spacing: .02em; white-space: nowrap; }
.b { border: .2mm solid var(--ink); border-radius: .7mm; padding: .55mm .9mm .45mm; background: var(--chip); margin: 0 .25mm; }
.b.h { box-shadow: 0 .55mm 0 0 var(--held); }
.e { padding: .1mm .4mm; }
.gl.t, .e .gl.t { width: 4.5mm; height: 4.2mm; vertical-align: -1.2mm; }
.gl.t .gs { stroke-width: 1.05; }
.k { margin: 0 .2mm; border: .2mm solid var(--ink2); border-top-width: .9mm; border-radius: 0 0 .5mm .5mm; padding: .35mm .7mm 1.05mm; background: var(--wk); }
.p { font: 600 2.4mm/1 "DejaVu Sans Mono", monospace; color: var(--accent); margin: 0 .5mm; }
.to { color: var(--ink3); margin: 0 .4mm; }
.map { border-bottom: .2mm solid var(--rule); padding-bottom: 1.4mm; margin-bottom: 2.4mm; }
svg.panel { display: block; width: 100%; height: auto; }
.body { fill: none; stroke: var(--ink); stroke-width: .25; }
.enc { fill: var(--wk); stroke: var(--ink); stroke-width: .3; }
.tick { stroke: var(--ink); stroke-width: .45; stroke-linecap: round; }
.cap { font: 600 2.2px "DejaVu Sans Mono", monospace; fill: var(--ink); text-anchor: middle; letter-spacing: .05px; }
.sub { font: 400 1.95px "Inter Tight", sans-serif; fill: var(--ink2); text-anchor: middle; }
.sub.l { text-anchor: start; }
.btn { fill: var(--chip); stroke: var(--ink); stroke-width: .28; }
.btn.press { stroke-width: .28; fill: var(--lit); }
.bt { font: 600 2.15px "DejaVu Sans Mono", monospace; fill: var(--ink); text-anchor: middle; }
.screen { fill: var(--screen); stroke: var(--ink); stroke-width: .3; }
.scr { font: 600 1.7px "DejaVu Sans Mono", monospace; fill: var(--scr); } .scr.r { text-anchor: end; }
.scrl { stroke: #4a525b; stroke-width: .15; }
.card { fill: #121418; stroke: #2a2f36; stroke-width: .15; }
.dot.k1 { fill: var(--k1); } .dot.k2 { fill: var(--k2); } .dot.k3 { fill: var(--k3); stroke: #8a8578; stroke-width: .15; } .dot.k4 { fill: var(--k4); }
i.dot { display: inline-block; width: 1.8mm; height: 1.8mm; border-radius: 50%; margin: 0 .6mm 0 1.6mm; vertical-align: -0.2mm; }
i.dot.k1 { background: var(--k1); } i.dot.k2 { background: var(--k2); } i.dot.k3 { background: var(--k3); box-shadow: inset 0 0 0 .15mm #8a8578; } i.dot.k4 { background: var(--k4); }
.wk { fill: var(--wk); stroke: var(--ink); stroke-width: .25; }
.wk.lit { fill: var(--lit); }
.bk { fill: var(--bk); stroke: var(--ink); stroke-width: .25; }
.wl { font: 600 2.05px "DejaVu Sans Mono", monospace; fill: var(--ink); text-anchor: middle; }
.wl.rot { text-anchor: start; font-size: 1.7px; }
.bl { font: 600 1.75px "DejaVu Sans Mono", monospace; fill: var(--bkt); text-anchor: start; }
.kb.small .bl { font-size: 1.5px; }
.nn { font: 400 1.75px "DejaVu Sans Mono", monospace; fill: var(--ink3); text-anchor: middle; }
.nn.s { font-size: 1.45px; }
.dim { stroke: var(--accent); stroke-width: .22; }
.dimt { font: 600 2px "Inter Tight", sans-serif; fill: var(--accent); text-anchor: middle; letter-spacing: .03px; }
.dimt.l { text-anchor: start; }
.cols3 { display: grid; grid-template-columns: 1fr 1.08fr 1fr; gap: 5mm; flex: 1; min-height: 0; }
.sec h2 { height: 4.8mm;  font: 700 2.5mm/1 "Inter Tight", sans-serif; text-transform: uppercase; letter-spacing: .08em; margin: 0 0 1.4mm; display: flex; align-items: center; gap: 1.6mm; flex-wrap: wrap; }
.nb { width: 4.4mm; height: 4.4mm; flex: none; display: block; }
.nb circle { fill: none; stroke: var(--accent); stroke-width: .48; }
.nbd { fill: var(--accent); }
.sec h2 .ht { display: inline-flex; align-items: center; gap: 1.6mm; }
.row { display: grid; grid-template-columns: 50% 50%; gap: 1.4mm; padding: .5mm 0; border-top: .15mm solid var(--rule2); align-items: center; }
.row:first-of-type { border-top: 0; }
.how { display: flex; align-items: center; flex-wrap: wrap; row-gap: .6mm; }
.what { color: var(--ink2); }
.foot { display: flex; justify-content: space-between; font-size: 2.1mm; color: var(--ink3); border-top: .2mm solid var(--rule); padding-top: 1.2mm; margin-top: 1.6mm; }
.btop { display: grid; grid-template-columns: 74mm 1fr; gap: 5mm; border-bottom: .2mm solid var(--rule); padding-bottom: 2mm; margin-bottom: 2.4mm; }
.bbot { display: grid; grid-template-columns: 1.1fr .95fr .95fr 1.1fr; gap: 5mm; flex: 1; min-height: 0; }
.lights { margin-top: 1.6mm; }
.fx .row { grid-template-columns: 34% 66%; }
.c-rec .fig svg { width: 84%; margin: 0 auto; }
.two { display: grid; grid-template-columns: 1fr 1fr; column-gap: 5mm; }
.two .row:nth-child(2) { border-top: 0; }
.fig { margin: .6mm 0 0; }
.fig svg { display: block; width: 100%; height: auto; }
.ring { fill: none; stroke: var(--ink); stroke-width: .35; }
.ring.r1 { stroke-width: .6; } .ring.r2 { stroke-width: .45; stroke-dasharray: 1.2 .6; } .ring.r3 { stroke-width: .35; stroke-dasharray: .4 .6; }
.ringc { fill: var(--ink); } .ringt { font: 700 1.6px "DejaVu Sans Mono", monospace; fill: var(--paper); text-anchor: middle; }
.lead { fill: none; stroke: var(--accent); stroke-width: .2; stroke-linejoin: round; } .leadd { fill: var(--accent); }
.rn { fill: var(--accent); }
.rt { font: 500 2.25px "Inter Tight", sans-serif; fill: var(--ink); } .rt.dimx { fill: var(--ink2); font-weight: 400; }
.ckbox { fill: var(--lit); stroke: var(--accent); stroke-width: .45; }
.ckt { font: 700 3.1px "Inter Tight", sans-serif; fill: var(--ink); letter-spacing: .25px; }
.cks { font: 400 1.95px "Inter Tight", sans-serif; fill: var(--ink2); } .cks.b { font-weight: 700; fill: var(--ink); }
.ckn { fill: var(--accent); } .cknt { fill: #fff; }
.ckl { font: 600 2.5px "Inter Tight", sans-serif; fill: var(--ink); }
.ckr { stroke: var(--accent); stroke-width: .2; }
.tip { margin: 1.2mm 0 0; padding: .9mm 1.4mm; border-left: .5mm solid var(--accent); color: var(--ink2); font-size: 2.3mm; }
.lights .row { padding: .5mm 0; }
@media print {
  @page { size: landscape; margin: 0; }
  :root, :root[data-theme="dark"] { --paper: #fff; --ink: #111; --ink2: #3f444a; --ink3: #7a8088; --rule: #bdb9b0; --rule2: #e4e1da;
    --wk: #fff; --bk: #1b1d20; --bkt: #ece7d9; --lit: #fbe6cf; --chip: #fff; --held: #111; --desk: #fff; }
  html, body { background: #fff; padding: 0; }
  .bar { display: none; }
  .sheetwrap { padding: 0; overflow: visible; }
  /* the paper is whatever the printer has: centre the 255 x 185 mm sheet on it, so Letter and A4 both get even margins */
  .page { margin: calc((100vh - 185mm) / 2 - 0.01mm) auto 0; box-shadow: none; break-after: page; page-break-after: always; }
  .page:last-child { break-after: auto; page-break-after: auto; }
  * { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
}
@media screen and (max-width: 1000px) { body { padding-top: 16px; } }
"""


def version():
    """the release the sheet describes: firmware/src/sqrl_version.h's SQRL_VERSION_NUM, without the v"""
    h = (ROOT / "firmware" / "src" / "sqrl_version.h").read_text()
    return re.search(r'#define SQRL_VERSION_NUM "v([^"]+)"', h)[1]


VERSION = version()


def build():
    page = f"""<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 hed0rah
     Made by tools/gen_cheatsheet.py: edit that, not this. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SQRL-1 Quick Sheet</title>
<meta name="description" content="SQRL-1's buttons, combos and pages on one front-and-back sheet, to print.">
<style>{CSS}</style>
</head>
<body>
{GLYPHS}
<div class="bar"><span>SQRL-1 quick sheet: print both sides, landscape, US Letter or A4, at 100 %</span><span><button id="theme" type="button">light / dark</button> <button type="button" onclick="window.print()">print</button></span></div>
<div class="sheetwrap">
{front()}
{back()}
</div>
<script>
// screen only: flip the theme; print is always light
(function () {{
  var r = document.documentElement, b = document.getElementById("theme");
  b.addEventListener("click", function () {{
    var dark = r.dataset.theme ? r.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
    r.dataset.theme = dark ? "light" : "dark";
  }});
}})();
</script>
</body>
</html>
"""
    return page


if __name__ == "__main__":
    page = build()
    if "--check" in sys.argv:
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != page:
            sys.exit(f"{OUT.relative_to(ROOT)} is stale: run tools/gen_cheatsheet.py and commit it")
    else:
        OUT.write_text(page, encoding="utf-8", newline="\n")
