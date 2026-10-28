# VOICE

VOICE sings vowels from the keyboard: a glottal pulse (the buzz of the vocal folds) through four formant
filters, the resonances that make an "ah" different from an "oo".

<!-- auto:card -->

## Playing it

VOWL on MAIN moves through the vowels A, E, I, O and U, blending between neighbours. BUZZ sets how bright and
pressed the voice is, from a soft, open tone to a buzzy one. BRTH mixes in breath noise; at the top the voicing
fades out and the voice whispers. TALK is a glide time: each note moves from VOWL to a second vowel, VOWL2, over
that time, so a held chord can say "ah-oo".

VOICE is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_VOICE=1`
brings it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a VOICE sound in a project or a
saved sound plays on MOUTH.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### VOWL

SHIFT moves the formants up or down in semitones: down for a bigger, darker throat, up for a smaller one.

### TONE

Q sets how narrow the formants are: high values ring, low ones blur. RAND pulls both vowels towards a random vowel
chosen for each note, so a choir's voices do not all say the same thing.

## Where it comes from

`firmware/src/eng_formant.c`, Felucca's engine (GPL-3.0), by Leo Kuroshita, Hügelton Instruments. It follows
D. H. Klatt's cascade formant synthesizer (JASA, 1980), with a Rosenberg glottal pulse and vowel formants from
Hillenbrand et al. (JASA, 1995).
