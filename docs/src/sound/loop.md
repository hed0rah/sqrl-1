# LOOP

LOOP is a loop of tape you record onto and play from the keys. It uses the tape echo's own tape, so the echo is
off while LOOP is the SOUND in front. Leaving LOOP gives the tape back to the echo, which clears it: what you
recorded is gone when you come back, and the screen says LOOP: TAPE CLEARED as you leave. There is no room for a
second tape, and no undo.

<!-- auto:card -->

## Playing it

LOOP records what the other parts send to the delay: a pair running behind (RUDIMENT, ALIVE), the patterns of the
other parts, or MIDI coming in. Turn a part's DLY send up on its FX page, switch REC on, and the tape fills; switch
REC off and the loop holds as it is. FDBK decides how much of the old loop survives each pass while REC is on, so a
low FDBK replaces the loop and a high one layers on top of it. Turn LOOP's own DLY send down, or the keys record
themselves back onto the tape.

The tape plays all the time, through the delay's MIX. The keys play it as well, through heads of their own: C4 plays
it as recorded, and every other key plays it higher or lower, faster or slower. On the KEYS page, MODE TAPE runs the
loop at the key's speed and GRAIN scatters short grains from it at the key's pitch, without changing its time.

SPEED is the whole tape's speed in semitones, so everything on it changes pitch and time together. MOTOR STOP winds
the tape down to silence and REV runs it backwards; INERT sets how long either takes, as the weight of the reels
would. QUAL trades the tape's length for its fidelity, and a change of QUAL wipes the tape. SPLCE cuts the loop
shorter; a new length takes effect when the loop next comes round, so it never clicks. WOW adds the tape's wobble.

The screen shows the tape as a reel: the loop is the coloured arc, the heads are the dot moving round it, and the
number is the loop's length in seconds.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### DECK

QUAL HI records at the full rate in 16 bits, MID at half the rate, LO in 8 bits at half the rate and CASS in 8 bits
at a quarter: each step down doubles the tape's length. MID and LO are filtered to about 8 kHz, CASS to about 4 kHz.

### KEYS

POS is where on the loop a key starts. GRAIN is a grain's length and SPRAY how far the grains' starts scatter.

### KEYS 2

SCAN moves GRAIN's starts through the loop: at 64 they move with the tape; at 0 they hold at POS.

LOOP is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_LOOP=1` brings
it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a LOOP sound in a project or a saved sound
plays on PHYS.

## Where it comes from

LOOP follows the tape loops of the 1960s and Expert Sleepers' Augustus Loop; the code is databurn's own. The tape is
`firmware/src/tapeloop.c`, from databurn (MIT), and the engine `firmware/src/eng_loop.c`.
