#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 databurn
"""Make the site, served at https://databurn.org/sqrl/:

  index.html                  the landing page (index_site.html): what SQRL-1 is, and the links
  og-sqrl-1.png               the 1200x630 social card every page's og:image names (assets/brand/)
  install/index.html          index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, fm1backup.js, fm1wheel.js
                              and the metadata inlined)
  editor/index.html           editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt, fm1backup.js, fm1wheel.js)
  sheet/                      the quick sheet to print, web/sheet/ as committed (tools/gen_cheatsheet.py), with its fonts
  manual/                     created empty if missing, never touched: tools/docs.sh --site writes the manual there
  firmware/sqrl-1-VER.fwsc    the package, its SHA256SUMS, and the licences: LICENSE, LICENSING.md, their .txt copies
                              (the pages link those: the server sends .md and extensionless files as downloads) and
                              LICENSES/ (the package holds JieLi SDK files under Apache-2.0, see LICENSING.md)

  web/make_site.py [--dev] build/release-sqrl-1-X.Y.Z/sqrl-1-X.Y.Z.fwsc X.Y.Z OUT_DIR

The version may be given as X.Y.Z or vX.Y.Z; a release links its source at the tag vX.Y.Z and its release notes.
--dev: a development build with any version name, for a local test site only; it links the repository's root.

The package must be one made by tools/fm1pkg_make.py (SQRL-1's own loader, no vendor files).
Its identity (FM-1_9xx) is read from the package; the device must report it after the install.
"""
import base64
import hashlib
import io
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SITE = "https://databurn.org/sqrl/"
REPO = "https://github.com/hed0rah/sqrl-1"


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def lockup_uri():
    """the SQRL-1 lockup (the splash's mask) as a white-on-alpha PNG data URI, for a CSS mask"""
    from PIL import Image
    m = Image.open(HERE.parent / "assets" / "brand" / "mask-sqrl-lockup.png").convert("L")
    im = Image.new("RGBA", m.size, (255, 255, 255, 0))
    im.putalpha(m)
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def fill(html, marks, once=()):
    """replace each /*MARK*/ with its value; every mark must be there, the ones in once exactly once"""
    for mark, value in marks.items():
        n = html.count(f"/*{mark}*/")
        if not n or (mark in once and n != 1):
            raise SystemExit(f"a page must contain /*{mark}*/{' once' if mark in once else ''}; update make_site.py")
        html = html.replace(f"/*{mark}*/", value)
    return html


def main(pkg, version, out, dev=False):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a SQRL-1 package (FM-1_9xx)")
    if product == "FM-1_900" and not dev:           # a development build: never on a public site
        raise SystemExit(f"{pkg}: a development build (FM-1_900); publish a release (tools/build.py --release), "
                         "or pass --dev for a local test site")
    if b"FELUCCA-LOADER-1" not in raw:              # the loader's marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no SQRL-1 loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    version = re.sub(r"^v(?=\d)", "", re.sub(r"^sqrl-1-", "", version))
    if dev:
        version = re.sub(r"[^A-Za-z0-9.-]", "-", version)
        source, notes = REPO, None
    elif re.fullmatch(r"\d+(\.\d+){1,3}(-[A-Za-z0-9]+)?", version):
        source, notes = f"{REPO}/tree/v{version}", f"{REPO}/releases/tag/v{version}"
    else:
        raise SystemExit(f"{version!r}: a release version is X.Y.Z (its tag is vX.Y.Z); --dev for any other name")
    card = HERE.parent / "assets" / "brand" / "og-sqrl-1.png"
    if not card.is_file():                          # every page's og:image: a site without it unfurls bare
        raise SystemExit(f"{card}: the social card is missing")
    name = f"sqrl-1-{version}.fwsc"
    digest = hashlib.sha256(raw).hexdigest()
    lockup = lockup_uri()

    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8"))
                    for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js", "fm1wheel.js"))
    bf = pkg.parent / "BUILD"                       # (tools/build.py: the commit, beside a release's package)
    build = bf.read_text().strip() if bf.is_file() else None
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name, "sha256": digest,
                       "notes": notes, "build": build})
    style = re.search(r"<style>.*?</style>", html, re.S)[0]     # the landing page wears the installer's style
    marks = {"SITE": SITE, "VERSION": version, "NOTES": notes or "", "SOURCE": source,
             "SOURCE_LABEL": re.sub(r"^https://", "", source), "LOCKUP": lockup}
    html = fill(html, {"LIB": lib, "META": meta, **marks}, once=("LIB", "META"))
    landing = fill((HERE / "index_site.html").read_text(encoding="utf-8"),
                   {"STYLE": style, **{k: marks[k] for k in ("SITE", "VERSION", "SOURCE")}}, once=("STYLE",))
    landing = fill(landing, {"LOCKUP": lockup})

    editor = (HERE / "editor.html").read_text(encoding="utf-8")
    link = f'href="{REPO}" id="source">{REPO[8:]}<'
    if editor.count(link) != 1:
        raise SystemExit("editor.html's footer must link the source once (id=\"source\"); update make_site.py")
    editor = editor.replace(link, f'href="{source}" id="source">{source[8:]}<')

    inst, ed, fw = out / "install", out / "editor", out / "firmware"
    sheet = HERE / "sheet"
    if not (sheet / "index.html").is_file():         # the landing page links it
        raise SystemExit(f"{sheet}: the quick sheet is missing; run tools/gen_cheatsheet.py")
    for d in (inst, ed, fw, out / "manual"):
        d.mkdir(parents=True, exist_ok=True)
    for old in [*fw.glob("sqrl-1-*.fwsc"), *fw.glob("felucca-*.fwsc")]:   # one package: the current one
        old.unlink()                                                     # (felucca-*: sites made before the rename)
    (out / "index.html").write_text(landing, encoding="utf-8", newline="\n")
    shutil.copy(card, out / card.name)
    (inst / "index.html").write_text(html, encoding="utf-8", newline="\n")
    (ed / "index.html").write_text(editor, encoding="utf-8", newline="\n")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js", "fm1wheel.js"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    shutil.copytree(sheet, out / "sheet", dirs_exist_ok=True)
    shutil.copy(pkg, fw / name)
    (fw / "SHA256SUMS").write_text(f"{digest}  {name}\n", encoding="utf-8", newline="\n")
    for doc, txt in (("LICENSE", "LICENSE.txt"), ("LICENSING.md", "LICENSING.txt")):
        shutil.copy(HERE.parent / doc, fw / doc)
        shutil.copy(HERE.parent / doc, fw / txt)
    lic = HERE.parent / "LICENSES"                  # the package holds JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with SQRL-1's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the installer links this folder: the server lists no folders
        '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
        '<title>SQRL-1 licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.txt">LICENSING</a> · <a href="../LICENSE.txt">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8", newline="\n")
    print(f"site: {out}: index.html, install/ ({len(html)} B), editor/, sheet/, "
          f"firmware/{name} ({len(raw)} B, {product}, sha256 {digest[:16]}...)")


if __name__ == "__main__":
    dev = "--dev" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--dev"]
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args, dev=dev)
