#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sync web/editor.html's mock engine tables with the firmware's (build/host/desc.json) for the engines
SQRL-1 adds: the ENG entries, the ENG name list of G_ENGSEL, and ENGINE_ORDER. Usage: tools/editor_eng.py REPO NAME... (after the host tests wrote build/host/desc.json). Each entry
replaced in its own slot (the array index is the engine number); nothing written unless the slots match desc.json."""
import json, re, sys  # noqa: E401
repo, names = sys.argv[1], sys.argv[2:]
d = json.load(open(f"{repo}/build/host/desc.json"))
FN = ["INT", "PCT", "BIPCT", "TIME", "LFOHZ", "CUTOFF", "DB", "SEMI", "ENUM", "BPM", "NOTE", "ONOFF", "OCT", "STEPS"]
js = lambda v: json.dumps(v)
def param(p):
    if p["fmt"] == 8 and p["names"] and p["min"] == 0:
        return f'E({js(p["label"])}, {js(p["names"])}, {p["def"]})'
    extra = f', {js(p["names"])}' if p["names"] else ""
    return f'D({js(p["label"])}, F.{FN[p["fmt"]]}, {p["min"]}, {p["max"]}, {p["def"]}{extra})'
def entry(e):
    ps = ", ".join(f'P({js(p["name"])}, {js(p["e"])}, {js(p["env"])}, {p["mono"]}, {p["pat"]})' for p in e["presets"])
    ed = ", ".join(param(p) for p in e["edit"])
    return f'    {{ name: {js(e["name"])}, titles: {js(e["titles"])}, edit: [{ed}],\n      presets: [{ps}] }},   /* SQRL-1 (generated from desc.json) */\n'
h = open(f"{repo}/web/editor.html").read()
eng = [x["name"] for x in d["ENG"]]
END = "  ];\n  const FM6_E = ENG.findIndex"
def slots(h):
    """the engine names of the page's ENG array, in order: the array index is the engine number"""
    start = h.rindex("const ENG = [", 0, h.index(END))
    return re.findall(r'^    \{ name: "([^"]+)"', h[start:h.index(END)], flags=re.M)
for n in names:
    e = next((x for x in d["ENG"] if x["name"] == n), None)
    if e is None:
        sys.exit(f"editor_eng: {n} is not an engine in desc.json")
    pat = r'    \{ name: "' + re.escape(n) + r'".*?/\* SQRL-1 \(generated from desc\.json\) \*/\n'
    if re.search(pat, h, flags=re.S):                   # in place: its slot is its engine number
        h = re.sub(pat, lambda m: entry(e), h, count=1, flags=re.S)
    elif slots(h) == eng[:len(slots(h))] and eng.index(n) == len(slots(h)):
        i = h.index(END)                                # new, and next in engine order: appended
        h = h[:i] + entry(e) + h[i:]
    else:
        sys.exit(f"editor_eng: {n} (engine {eng.index(n)}) has no SQRL-1 entry and is not next in order")
if slots(h) != eng:
    sys.exit(f"editor_eng: ENG slots {slots(h)} != desc.json {eng}; nothing written")
h = re.sub(r'E\("ENG", \[[^\]]*\], 0\)', 'E("ENG", ' + js(eng).replace('","', '", "') + ', 0)', h, count=1)
order = [n if isinstance(n, str) else eng[n] for n in d["ORDER"]]
full = ["ANALOG", "FM6", "DIGITAL"] + [n for n in order if n not in ("ANALOG", "FM6")]
h = re.sub(r'const ENGINE_ORDER = \[[^\]]*\];', "const ENGINE_ORDER = " + js(full).replace('","', '", "') + ";", h, count=1)
open(f"{repo}/web/editor.html", "w").write(h)
print("editor tables:", ", ".join(names), "| ENG", len(eng), "| ORDER", full)
