# S+H

S+H is sample and hold into the scale. Each step it samples a source and holds the value as a note of the shared
scale. The sources range from plain noise to a random walk, the logistic map, bytebeats and integer sequences, so
the line can be anything from a drunk wander to a structured glitch, and it always stays in key.

<!-- auto:card -->

## Playing it

<!-- auto:keys F3-G5=TRANSPOSE -->

Press PLAY. Each step of the part's sequencer (DIV on its SEQ page sets the step) takes a new sample and plays it as
a degree of the scale, spread over RANGE octaves and counted up from the key you played last. A tap moves the line
along the scale to start from that note; the next step plays from there. Stopped, hold a key to hear the line: it
plays as it would with the transport running, from that note, until you let go.

While it plays, hold a key and the sample holds too: the same note again each step until you let go. Tap for a new start, hold for
a held note, and the source goes on from where it was.

DENS sets how many steps sound. The rests are random, so the line breathes.

S+H runs free by default. Turn RANGE past 4 OCT to 1 LOCK .. 4 LOCK and it locks, as a Turing machine does: at the top
of each cycle of LEN (the SEQ page's) every source starts again from its seed, so the line repeats after LEN steps,
rests included. Turn LEN to change the phrase's length; locked NOISE at LEN 7 is a seven-step riff.

FLUX, on the LINE page, changes a locked loop as a Turing machine's knob does. Each step, with chance FLUX, takes a
new note (half the time a degree or two from the one it had, half anywhere in RANGE) or turns from a rest to a note or
back, and keeps it pass after pass, so the riff drifts. Turn FLUX to 0 and the loop freezes as it is now. Turning
SRC, P or RANGE, or pressing PLAY from stop, goes back to the source's own loop. Unlocked, FLUX does nothing.

CLK, on the LINE page, chooses what steps S+H: STEP, every step of its own (the default); BEAT, every hit BEAT
plays; or L1 to L8, one lane of BEAT's (under DRUMS L1 is the kick, L2 the snare, L4 the closed hat). Clocked by a
lane, S+H takes its sample and plays it on that lane's hits and holds the note until the next, so the line locks to
the groove: L1 is the 303-over-a-909 trick. DENS still thins the hits out.

LEAP sets the largest move from one note to the next, 1 to 7 degrees (ANY: no limit). A sample further away lands
that far toward it, so NOISE and BYTE climb and fall in steps instead of jumping, which makes them sing.

To play the chord instead of the scale, set QNT to CHORD on SCALE ([SCALE](../shell.md#scale)): S+H then picks
only the notes of the chord PLAY sounds, over RANGE's octaves, so NOISE is a random arpeggio, WALK climbs and falls
through the inversions, and a new chord on CHORD changes the line at its next step.

## The sources

SRC picks the source, and P is the source's own knob:

| SRC | What it samples | P |
|---|---|---|
| NOISE | a random value | how often it changes: 0 rarely (1 step in 16), 15 every step |
| WALK | a random walk, turned back at RANGE's ends: brown noise, a line that stays near itself | the largest move: 1 degree at P 0..3, up to 4 at 12..15 |
| LOGIS | the logistic map x' = r x (1 - x) | r: 3.5 at P 0 (a few notes round, a riff), up to 4.0 at 15 (chaos) |
| BYTE | a bytebeat, stepped once a step instead of played at audio rate | the equation (P and P + 8 are the same one, read twice as far apart) |
| INT | an integer sequence | P 0, 4, 8, 12: the digits of pi; 1, 5..: van Eck's sequence; 2, 6..: Dress's (the 1 bits of 1, 2, 3...); 3, 7..: Norgard's infinity sequence. P / 4 skips terms: 1, 3, 5 or 7 a step |

LOGIS is the one to sweep: past about 3.57 its riff splits into more notes and then into noise, with a few calm
windows on the way. INT's sequences are the most structured. Norgard's infinity sequence repeats itself at every
scale, and Dress's climbs and falls back with each power of two.

S+H is not on the default wheel: pick it on the PAIR page (hold HOME, tap GLO, KNOB 2) or give it a black key with
the slot picker ([The wheel](../wheel.md)). It plays on part 3, LINE, which it shares with LINES, ALIVE, TONNETZ,
ENIGMA and CANON, and goes on playing when you leave it. To keep a line you like as steps, FREEZE it (hold REC,
press OCT+: [Sequencing](../sequencing.md)).

<!-- auto:home -->

<!-- auto:pages -->

## Where it comes from

Ornament & Crime's Quantermain quantizes internal sources the same way: its random, logistic and integer-sequence
sources pick a scale degree in a range. Viznutcracker, sweet! plays bytebeats stepped. Both are by Patrick Dowling,
Tim Churches and Max Stadler, under MIT. The bytebeat equations: BYTE 0 to 2 are viznut's, 3 is stephth's, and 4
to 7 are new. `firmware/src/snh.c` is written fresh for SQRL-1's note engines (databurn, MIT).
