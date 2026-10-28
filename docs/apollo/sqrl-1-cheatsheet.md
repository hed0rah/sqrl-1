---
title: SQRL-1 quick reference
section: "Q"
tab: QRC
handbook: Operations Handbook
system: "SQRL-1 -- firmware for the M-VAVE FM-1"
doc: SQRL-1 QR
mission: "v1.0.0"
date: 10.28.2026
by: "Cheatsheet, from the SQRL-1 manual. Tap under 300 ms, hold from 300 ms, long hold 1.2 s."
---


## Buttons

:::table Tap and Hold
| Button | Tap | Hold |
|---|---|---|
| HOME | flip SOUND / NOTE view; twice: the pair playing behind | the wheel; + GLO tap: PAIR page by knobs |
| GLO | SYSTEM, SYSTEM 2, PROJECTS, ABOUT, LEGAL | SHIFT; + any button: what it does here |
| SCL | SCALE; again HARMONY; again back | FILL while playing |
| EDIT ENV LFO FX | that family of SOUND pages, in turn | FX: the FX layer on the keys |
| ARP | ARP page; again ARP 2; twice: HOLD | HOLD on or off on release |
| SEQ | the pattern; again EVOLVE | white keys are steps; OCT+ DOUBLE, OCT- HALF; PRESETS queue NEXT |
| SAVE | store the sound (OCT+ yes) | try: knobs snap back; OCT+ keeps |
| REC | arm; playing: overdub; twice: + KNOBS | ring 1 UNDO; ring 2 CLEAR; + SELECT: REC MODE; + OCT+: FREEZE |
| PLAY | start / stop, on press | + REC held: count in and record |
| OCT- OCT+ | octave, on press | OCT- with ARP SEQ EDIT SCL: reset that family |
:::

::::row
:::table Encoders
| Control | Does |
|---|---|
| SELECT | walk the view's pages |
| GLO + SELECT | tempo |
| PRESETS | walk sounds, factory then yours |
| ALGORITHM | the nut's main character |
| KNOB 1-4 | the four cards; fast turn x2, not with GLO |
:::
|||
:::table Resets (hold, then OCT- to the ring)
| Held | Resets |
|---|---|
| ARP | the NOTE engine's arp and own knobs |
| SEQ | SWG, GATE, QUANTIZE |
| EDIT | the SOUND to its stored preset |
| SCL | ROOT, SCL, QNT, TRN on every part |
:::
::::

## The wheel and tracks

::::row
:::table Default Wheel (hold HOME)
| White | SOUND | Black | NOTE |
|---|---|---|---|
| C4 | FM6 | C#4 | KEYS |
| D4 | LIZARD | D#4 | CHORD |
| E4 | SHOCK | F#4 | LINES |
| F4 | DRUMS | G#4 | TONNETZ |
| G4 | MOUTH | A#4 | RUDIMENT |
| A4 | ANALOG | | |
| B4 | PHASE | | |
| C5 | PHYS | | |
:::
|||
:::table Wheel Gestures
| Want | Do |
|---|---|
| a pair | HOME, white + black, let go |
| only one half | GLO + its key |
| by knobs | HOME, tap GLO |
| a black key's engine | HOME + GLO + black key, SELECT |
| mute a part | HOME + its pair + PLAY |
| clear a pair | HOME + its pair + hold REC |
| stop all | PLAY on the wheel's centre |
| leave | OCT- |
:::
::::

:::table Tracks
| Track | Part | NOTE engines |
|---|---|---|
| PLAY | 1 and 2, in turn | KEYS, CHORD, HARP: your takes |
| LINE | 3 | LINES, ALIVE, TONNETZ, ENIGMA, S+H, CANON, ACID |
| BEAT | 4 | RUDIMENT |
:::

## Steps and recording

::::row
:::table Hold a Step (white key)
| Control | Sets |
|---|---|
| C#5 | ACCENT |
| D#5 | SLIDE |
| F#5 | TIE |
| black F#3-A#4 | lane hit / note degree |
| OCT+- SELECT | octave / semitone |
| KNOB 1 | CHNC, 1:2..4:4, FILL |
| KNOB 2 | RATCH x1-x4 |
| KNOB 3 | NUDGE -8..+7 |
| KNOB 4 | VEL |
| SOUND knob | lock on this step |
:::
|||
:::table Recording
| Do | Gets |
|---|---|
| REC + PLAY | count in, record |
| REC tap | close take / overdub |
| REC twice | knobs record or not |
| REC + SELECT | ADD, REPLACE, + KNOBS |
| REC + white | erase step |
| REC + key, playing | erase that note |
| REC ring 1 | UNDO |
| REC ring 2 | CLEAR |
| REC + OCT+ | FREEZE a generator |
| SEQ + PRESETS | NEXT pattern at bar |
:::
::::

## SCALE, HARMONY, MOVE

::::row
:::table QNT (on SCALE)
| QNT | Keys do |
|---|---|
| OFF | as played |
| SNAP | round down to the scale |
| WHITE | scale on white keys from C4, black silent |
| SEQ / SEQ+T | snap; pattern mapped too (+T follows TRN) |
| NEAR | nearest scale note |
| DROP | off-scale notes go silent |
| CHORD | nearest tone of the chord PLAY sounds |
:::
|||
:::table HARMONY (SCL twice)
| Knob | Values |
|---|---|
| TYPE | 3RD 5TH TRIAD 7TH SUS2 SUS4 6TH ADD9 OCT |
| VOIC | CLOSE OPEN DROP2 WIDE |
| LEAD | ABOVE / BELOW |
| VEL | harmony level |
:::
::::

:::note
Draw a scale on SCALE: press keys together, the lowest is ROOT; tap one to toggle it. HARMONY needs VOICE mode POLY. CHORD, TONNETZ, HARP and DRUMS take none. MOVE (SELECT on from SCALE) moves the
scale on the bar: EVERY 1-8 bars, by MODE, 5TH, ALT or RAND, home after BACK.
:::

## NOTE engines

:::table What Each Plays
| Engine | Plays | ALGORITHM |
|---|---|---|
| KEYS | the keyboard as it is | |
| CHORD | a chord a key: quality keys left, roots from D4 | voicing |
| HARP | Omnichord: chord keys F3-E4, strum plate above | |
| LINES | eight lanes of steps, a note a lane | |
| ALIVE | a 1-D cellular automaton's rows as melody | RULE |
| TONNETZ | a triad walking Euler's Tonnetz | |
| ENIGMA | Turing Machine: a looping, drifting register | |
| S+H | sample and hold into the scale: noise, walks, logistic map, bytebeat | |
| CANON | answers you in 1-3 voices, DELAY steps and DEG degrees apart | |
| ACID | a 303 line: note, octave, ACCENT, SLIDE, TIE; FLUX mutates it | |
| RUDIMENT | eight lanes, sixteen steps, a length per lane | the rhythm |
:::

## SOUNDs

:::table The Eight on the Wheel
| SOUND | Is | MAIN starts with |
|---|---|---|
| FM6 | six-operator FM, DX7 family | ALGORITHM picks the algorithm |
| LIZARD | Electrik Lizard oscillator modes, drawn waves | MODE |
| SHOCK | a sine steepened into a brassy shock wave | DRIVE |
| DRUMS | kits; GM map on KEYS, lanes on RUDIMENT | KIT |
| MOUTH | sings a phrase, a syllable or word a key | PHRASE |
| ANALOG | two oscillators and noise into a resonant filter | WAVE |
| PHASE | Casio CZ phase distortion | WAVE, DCW |
| PHYS | struck, plucked and bowed bodies | MODEL |
:::

## FX layer and no sound

::::row
:::table Keys While FX Is Held
| Keys | Effects |
|---|---|
| F3 G3 A3 B3 | REPEAT 1/8, 1/16, 1/32, REVERSE |
| C4 D4 E4 F4 | LPF, HPF, TAPE STOP, FREEZE |
| G4 A4 B4 C5 | OCT UP, OCT DN, FLANGER, PHASER |
| D5 E5 | REV THROW, ECHO THROW |
| F#3 G#3 A#3 C#4 | mute parts 1 to 4 |
| KNOB 1-4 | FILTER, CRUSH, THROW, DEPTH |
:::
|||
:::checklist No Sound | Player
MASTER ............... UP
GENERATOR ............ PLAY OR HOLD A KEY
HOME GLOWS ........... PART MUTED
UNMUTE ............... HOME + PAIR + PLAY
PLAY DIES IN 0.5 S ... CLK TO INT
ROOT ONLY ............ VCE TO POLY
:::
::::
