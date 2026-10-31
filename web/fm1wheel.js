// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 databurn
// SQRL-1's wheel over the editor protocol (EDITOR_PROTOCOL.md "The wheel", command 112): which SOUND sits on each
// white key C4..C5 and which NOTE engine on each black key. The device checks every rule again; this side only keeps
// the choices valid so a set is never refused. Used by the installer and the editor, with any request(cmd, args)
// that answers with the reply's payload (fm1backup.js BackupConnection).

export const WHEEL_CMD = 112;
export const WHEEL_WHITE = 8, WHEEL_BLACK = 5, WHEEL_NONE = 127;
export const WHEEL_WHITE_KEYS = ["C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"];
export const WHEEL_BLACK_KEYS = ["C#4", "D#4", "F#4", "G#4", "A#4"];

export function parseWheel(a) {
  let i = 0;
  const b = () => { if (i >= a.length) throw new Error("short WHEEL reply"); return a[i++]; };
  const s = () => { let t = ""; for (let c; (c = b());) t += String.fromCharCode(c); return t; };
  const rc = b(), keys = Array.from({ length: WHEEL_WHITE + WHEEL_BLACK }, b), ns = b(), nn = b();
  const sounds = Array.from({ length: ns }, () => { const f = b(); return { avail: !!(f & 1), welded: !!(f & 2), name: s() }; });
  const notes = Array.from({ length: nn }, () => ({ name: s() }));
  return { rc, white: keys.slice(0, WHEEL_WHITE), black: keys.slice(WHEEL_WHITE), sounds, notes };
}

// the device's rules: each entry once per ring, only what the build has (a welded SOUND, flag bit 1, is never set
// since RUDIMENT became a NOTE engine; the check stays harmless)
export function wheelProblem(w, white, black) {
  const welded = w.white.findIndex((m) => m !== WHEEL_NONE && w.sounds[m]?.welded);
  const seen = new Set();
  for (let k = 0; k < WHEEL_WHITE; k++) {
    const m = white[k];
    if (m === WHEEL_NONE) { if (k === welded) return "welded"; continue; }
    const e = w.sounds[m];
    if (!e || !e.avail) return "missing";
    if (e.welded !== (k === welded)) return "welded";
    if (seen.has(m)) return "twice";
    seen.add(m);
  }
  seen.clear();
  for (const n of black) {
    if (!(n < w.notes.length)) return "missing";
    if (seen.has(n)) return "twice";
    seen.add(n);
  }
  return null;
}

export const wheelGet = async (request) => parseWheel(await request([WHEEL_CMD, []]));
export const wheelSet = async (request, white, black) => parseWheel(await request([WHEEL_CMD, [1, ...white, ...black]]));
export const wheelFactory = async (request) => parseWheel(await request([WHEEL_CMD, [2]]));

// the editing form: a select per black key, what another key holds greyed out; the white keys shown fixed.
// t(key) gives the labels "empty"; onChange(white, black) after every choice
export function wheelForm(root, w, t, onChange = () => {}) {
  const white = [...w.white], black = [...w.black];
  root.replaceChildren();
  const row = (label, keys, list, pick, opts) => {
    const box = document.createElement("div");
    box.className = "wheel-row";
    const h = document.createElement("p");
    h.className = "small";
    h.textContent = label;
    box.append(h);
    root.append(box);
    const sels = keys.map((key, k) => {
      const l = document.createElement("label"), sel = document.createElement("select");
      l.className = "wheel-key";
      l.append(key, " ", sel);
      box.append(l);
      sel.addEventListener("change", () => { list[k] = Number(sel.value); refresh(); onChange([...white], [...black]); });
      return sel;
    });
    const refresh = () => sels.forEach((sel, k) => {
      const fixed = opts(k) === null;
      sel.replaceChildren(...(fixed ? [[list[k], pick[list[k]]?.name ?? t("wheelEmpty")]] : opts(k)).map(([v, name, off]) => {
        const o = document.createElement("option");
        o.value = v; o.textContent = name; o.disabled = !!off; o.selected = v === list[k];
        return o;
      }));
      sel.disabled = fixed;
    });
    return refresh;
  };
  const refreshes = [];
  // the device keeps its eight SOUNDs on the white keys and ignores what is sent for them: shown, never chosen
  refreshes.push(row(t("wheelSounds"), WHEEL_WHITE_KEYS, white, w.sounds, () => null));
  refreshes.push(row(t("wheelNotes"), WHEEL_BLACK_KEYS, black, w.notes, (k) =>
    w.notes.map((e, n) => [n, e.name, black.some((x, j) => j !== k && x === n)])));
  const refresh = () => refreshes.forEach((r) => r());
  refresh();
  return { get: () => ({ white: [...white], black: [...black] }) };
}
