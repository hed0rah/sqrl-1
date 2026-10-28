# VODER

VODER is Homer Dudley's Voder, the speaking machine Bell Labs showed at the 1939 New York World's Fair, laid out on
the FM-1's keys. You speak with it by opening filter bands by hand, as its operators did.

<!-- auto:card -->

## Playing it

<!-- auto:keys F3-A4:white=BANDS F#3=P G#3=T A#3=K C#4=HISS -->

The first ten white keys, F3 to A4, each open one band of a ten-band filter bank, lowest band on the left. Hold
several together to shape a vowel: the bands you hold are the formants you hear. The first three black keys are
the stops p, t and k, short bursts for the plosives. The fourth, C#4, is the wrist bar: while it is held the source
turns to hiss, for s, sh and f.

The keys never change the pitch. PEDAL does, as the foot pedal did on the original, and ALGORITHM turns it, so one
hand can carry an intonation while the other spells. MAIN's four knobs set the source and the bands:

- PEDAL sets the pitch of the buzz.
- HISS mixes the source between buzz and hiss.
- Q narrows the bands: at 0 they are as wide as Dudley's, at the top about three times narrower.
- VIB adds vibrato.

The screen shows the ten bands as bars and the P T K HISS keys above them, lit while held.

Start with a vowel. B3 and C4 together sit near the first two formants of "ah"; G3 and E4 sit near "ee". Move one
finger and listen to the vowel change. The 1939 operators trained for about a year; a few minutes gets you to a
robot.

VODER is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_VODER=1`
brings it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a VODER sound in a project or a
saved sound plays on MOUTH.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### VODER

TILT sets the buzz's brightness before it reaches the bands.

### VOICE

VIB is the vibrato depth. GLIDE sets how slowly the pitch follows PEDAL, so a fast turn becomes a slide. STOPS is the
loudness of the p, t and k keys. DRIFT lets the pitch wander slowly at random, the unsteadiness of a human
operator.

## The bands

| Key | Band (Hz) |
|---|---|
| F3 | 0 to 225 |
| G3 | 225 to 450 |
| A3 | 450 to 700 |
| B3 | 700 to 1000 |
| C4 | 1000 to 1400 |
| D4 | 1400 to 2000 |
| E4 | 2000 to 2700 |
| F4 | 2700 to 3800 |
| G4 | 3800 to 5400 |
| A4 | 5400 to 7500 |

The edges are Dudley's, from Dudley, Riesz and Watkins, "A synthetic speaker", Journal of the Franklin Institute
227 (1939). Each key is two band-pass filters in series.

## Where it comes from

`firmware/src/voder.c` and `firmware/src/eng_voder.c` (MIT). The engine maps the keys itself, so the sequencer and
MIDI reach the bands and stops too ([Connections](../connections.md)): band b is MIDI note 36 + b, the stops 48 to
50, the hiss 52.
