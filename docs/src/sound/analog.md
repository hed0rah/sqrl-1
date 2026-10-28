# ANALOG

ANALOG is a virtual analog synth: two oscillators, noise and drive into a resonant filter, for anything that
starts with a saw and a low-pass.

<!-- auto:card -->

## Playing it

MAIN holds WAVE, CUT and RES, the filter's cutoff and resonance, and REL, the release (ATK is on ENV).
Sweep CUT while holding a chord, or turn RES up and play low for squelch.

ANALOG is on A4 of the default wheel. With
[LINES](../note/lines.md) it plays an acid line: its FILTER page is the 303's.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### OSC

WAVE picks the oscillator pair: SAW, SQR, TRI, SIN and PWM play two of the same wave, detuned by DTN; MIX blends
them, and NOIS adds noise. Two settings change what the knobs mean. SYNC hard-syncs the second saw to the first
(it restarts every time the first does), and DTN, shown as SYNC, sets how far above the first it sits: sweep it
from the envelope or an LFO on SHP for a sync sweep. SUB makes the second oscillator a square an octave down,
and MIX, shown as SUB, sets its level.

### FLT

DRV drives the filter's input. KTR is key tracking, how far the cutoff follows the note you play.

### FILTER

TYPE chooses low-pass, band-pass or high-pass. All three peak alike at the cutoff, so switching keeps the level.

## Where it comes from

`firmware/src/eng_analog.c`, Felucca's engine (GPL-3.0), by Leo Kuroshita, Hügelton Instruments. The filter is
Simper's trapezoidal state-variable filter.
