# LOFI

LOFI is a chip voice: pulse, triangle, saw, noise and a 4-bit wave RAM, with the coarse volume steps and pitch
tricks of old game consoles.

<!-- auto:card -->

## Playing it

WAVE on MAIN picks the waveform. DUTY sets the pulse width; with WAVE at WRAM it picks one of the built-in 4-bit
wave tables instead (shown as WAV# and the table's name). CRSH adds bit and rate reduction, and REL is the
release.

LOFI is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_LOFI=1` brings
it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a LOFI sound in a project or a saved sound
plays on ANALOG.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### CHIP

CHIP picks the character: 4BIT, 4B/2, 8BIT and 1BIT set how coarse the output is. STEP behaves like the old sound
chips themselves: a stepped 4-bit volume envelope replaces the smooth one, the triangle becomes a 32-step
staircase, the pulse widths are 12.5, 25, 50 and 75 %, and CRSH (shown as DCY) sets the envelope's decay.

### MOTN

MOTN holds the motion inside the note. SWP bends the pitch down after the note starts (the falling "pew"), VIB
adds vibrato, and ARP cycles the note through an octave or a major or minor chord, the way chip music fakes
chords on one voice. TONE is a low-pass: turn it down to soften the edges.

## Where it comes from

`firmware/src/eng_lofi.c`, Felucca's engine (GPL-3.0), by Leo Kuroshita, Hügelton Instruments. The wave RAM tables
are the author's own shapes, built from simple formulas.
