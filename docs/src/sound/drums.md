# DRUMS

DRUMS plays drum kits. Each key of a kit is a drum piece: with KEYS you play the kit from the keys, and with
[RUDIMENT](../note/rudiment.md), its NOTE engine, eight lanes of steps play eight of its pieces.

<!-- auto:card -->

## Playing it

Under KEYS the keys play the General MIDI drum map, as a keyboard does: the kick, the snare and the hats sit on
their usual notes. Hold HOME, press F4 and then A#4 for DRUMS with RUDIMENT, the beat machine.

PRESETS walks the kits. MAIN holds the kit and its most used controls.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### KIT and HIT

KIT picks the drum kit: STD is Felucca's own, DIGI is SQRL-1's, and the others are model kits. DIGI's voices are
short and simple on purpose: a sine kick with a fast drop and a click, a white-noise snare, three-burst claps, hats of
pure high-passed noise, and on the last three lanes a POP (a sine falling two octaves), a TICK (a 3 kHz sine and its
octave) and a BLIP (two raw squares an octave apart, aliasing as a chip does). Each is simple enough that TUNE, DECY,
TONE and SNAP on the DRUM page describe it whole, so they are the ones to design your own clicks and kicks from. TUNE, TONE and DECY move every piece
from its designed sound together. On HIT, SNAP is each piece's extra character (the kick's drive, the snare's
snappiness, the clap's spread, the hats' noise), ACC the accent, KICK the kick's type, and DRV a soft clip on every
hit.

### LANES and LANES 2

These pages set the level of each of the kit's eight pieces, kick to bell, under KEYS and RUDIMENT alike. 100 % plays
the kit as designed. A level follows the piece: a RUDIMENT lane whose NOTE strikes another piece plays at that piece's
level.

### DRUM

One drum's own sound: TUNE, DECY, TONE and SNAP move only that drum, on top of the kit-wide knobs; 0 is the drum as
designed. The screen names the drum it sets. Under RUDIMENT it is the drum the lane in front strikes (tap its black key;
a lane whose NOTE is set to another drum sets that drum); under KEYS it is the drum you played last, so strike a snare
and turn. A project keeps each part's drums, and DRUMS keeps them while another SOUND is in front or PRESETS walks to
another kit. SAVE keeps them with the sound on the FM-1, and a backup keeps them too; the editor copies a saved DRUMS
sound's kit only.

## Where it comes from

The drum voices are Felucca's DRUM engine (`firmware/src/eng_drum.c`, `firmware/src/drum_voice.c`, GPL-3.0).
