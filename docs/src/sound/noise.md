# NOISE

NOISE runs noise through a resonant filter: white noise for wind and rain, digital shift registers for clicks,
buzzes and metallic tones.

<!-- auto:card -->

## Playing it

FREQ on MAIN is the filter's frequency and RES its resonance: at 0 the filter is a low-pass, and as RES rises it
narrows into a band-pass that whistles at FREQ. COLR tilts the noise from white through pink to brown (darker,
like distant surf). REL is the release.

NOISE is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_NOISE=1`
brings it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a NOISE sound in a project or a
saved sound plays on ANALOG.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### SRC

MODE picks the source:

- ANLG: white noise, with random impulses mixed in.
- DUST: the impulses alone, for crackle and drops through the filter.
- LFSR: a long shift register (a digital noise generator) clocked above the key, from hiss to the coarse noise of
  old game consoles.
- META: one short register sequence repeated once per period of the note, which gives a pitched metallic tone.

### MOVE

TRK makes FREQ follow the keyboard, so a melody plays a tune through the filter. DENS sets how many impulses fire.
DRFT lets the filter and the clock wander slowly at random. CRSH adds sample-and-hold and fewer bits (in ANLG and
DUST).

In LFSR and META two knobs change job. DENS becomes LEN, which picks the register's length in LFSR and the sequence
in META. CRSH becomes CLK, the register's clock in semitones above the key.

## Where it comes from

`firmware/src/eng_noise.c`, Felucca's engine (GPL-3.0), by Leo Kuroshita, Hügelton Instruments.
