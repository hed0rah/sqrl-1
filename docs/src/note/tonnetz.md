# TONNETZ

TONNETZ walks a triad around Euler's Tonnetz, the grid of pitch classes in which neighbouring triads share two
notes. Each move keeps two notes and shifts the third by a step or two, so the walk voice-leads smoothly however far
it wanders.

<!-- auto:card -->

## Playing it

<!-- auto:keys F3-G5=START -->

A key sets the starting triad, a major chord on that note an octave below the hand, and restarts the walk from
there. Stopped, hold the key to hear the walk: it plays as it would with the transport running, from the first step, until you let go. Press PLAY and the triad moves every EVERY steps of the part's sequencer (DIV on
its SEQ page sets the step), applying the moves MOVE names in turn:

| Move | Changes | Example |
|---|---|---|
| P, parallel | the third by a semitone | C major to C minor |
| R, relative | the fifth up a tone, or the root down a tone | C major to A minor |
| L, leading-tone exchange | the root down a semitone, or the fifth up one | C major to E minor |

MOVE cycles a pattern: P L R, L R, P L, P R, a single move repeated, or RAND for a random move each time. A single
move flips back and forth between two chords; the longer patterns travel. The walk ignores the shared scale on
purpose: it is chromatic, and that is its sound. It walks on for as long as it plays; LEN on the SEQ page is only how
much of it FREEZE takes.

PLAY sets what each step sounds. CHORD strikes the new triad on every move; UP, DOWN and RAND play one of its notes
per step, an arpeggio whose harmony shifts underneath it. VOIC voices the triad, with the names CHORD uses: CLOSE,
OPEN (the root an octave down), WIDE (the root down and the top up), INV1 (the lowest note up an octave), INV2 (the
lowest two up), DROP2 (the middle note down an octave), or VAR, a voicing of its own for each chord. The walk itself
is voice-led, so each move still changes one note by a step or two.

<!-- auto:home -->

<!-- auto:pages -->

## Where it comes from

The Tonnetz is Leonhard Euler's (1739); the P, L and R moves are neo-Riemannian theory's. The idea of a walking
triad comes from Ornament & Crime's Automatonnetz and Harrington 1200 (Patrick Dowling, Tim Churches, MIT).
`firmware/src/tonnetz.c` is written fresh for SQRL-1's note engines (databurn, MIT).
