# TALK

TALK is a talking chip: an emulation of the TMS5220, the LPC speech chip of the Speak & Spell family. Every key
speaks a word at that key's pitch, and the knobs bend, stretch and corrupt the chip's frames.

<!-- auto:card -->

## Playing it

Each key speaks the word WORD names, on the key's pitch, up to <!-- auto:const talk_poly word --> at once. ALGORITHM picks the BANK the words
come from: DIGITS, WORDS or SYLLS. The screen spells the word that is speaking.

MAIN's four knobs shape the speech:

- WORD picks the word a note speaks.
- RATE sets the speed of the speech without changing its pitch: slow it down to a drawl or speed it up to a
  chirp.
- BEND moves the chip's clock in semitones. Bending the clock moves the formants (the resonances that make a vowel
  sound like itself), so the voice gets smaller or larger.
- GLTCH flips bits as the chip reads its data. A little sounds like a damaged cartridge; a lot falls apart into
  noise and pitch jumps.

LPC (linear predictive coding) stores speech as a buzz or hiss source and a filter that changes every few
milliseconds. The chip plays those frames one after another, so slowing RATE stretches the vowels and leaves the
pitch alone.

Load COUNTER and play a melody: it counts through the digits, one per note, because its PLAY is set to NEXT.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### WORD

This page holds WORD, BANK, RATE and BEND, so you can pick a bank and a word side by side.

### CHIP

SMTH sets how the chip moves between frames: CHIP uses its own interpolation, STEP jumps from frame to frame, and
SLUR glides. INTON chooses the pitch: OFF speaks every word on the key's pitch as a monotone, ON uses the word's own
recorded intonation, and the key only starts it. PLAY sets what a note does: ONCE speaks the word, LOOP repeats it
while the key is held, NEXT moves on to the next word of the bank with each note, and RAND picks one.

## Where it comes from

The generator, `firmware/src/lpc5220.c`, follows MAME's tms5220.cpp; its tables are MAME's (BSD-3-Clause), checked
by their authors against decapped chips. The engine is `firmware/src/eng_talk.c` (MIT). No word banks ship: TALK
stays out of the build until there are banks cleared for distribution.
