# Connections

The FM-1 connects through its USB-C port and its TRS MIDI input. One USB cable carries MIDI, audio to the
computer and a serial console at once; the TRS jack takes MIDI from other gear.

## USB

The FM-1 shows up on the computer as **SQRL-1**, made by databurn, with three functions:

| Function | What it does |
| --- | --- |
| USB MIDI | notes and controllers in, the keys out, clock in; the installer and the editor talk to the FM-1 through it |
| USB audio | the FM-1's output as a 16-bit stereo input, at 44.1 or 48 kHz |
| Serial console | a text console for diagnostics |

All three are class compliant, so macOS, Windows, Linux, iOS and iPadOS use their built-in drivers.

While the installer writes a new firmware, the FM-1 shows up as **SQRL-1 Update** instead, with MIDI only. It
comes back as SQRL-1 when the install is done.

## TRS MIDI IN

The TRS jack is a MIDI input at the standard 31250 baud. It hears the same things as USB MIDI (notes, controllers,
pitch bend, aftertouch, Program Change) and shares USB's channels: a note on channel 1 is the same note whichever
way it came. It also carries clock, Start, Continue and Stop, which SQRL-1 follows when CLK on SYSTEM is set to TRS.
SysEx on the jack is ignored, so the installer and the editor need USB. SQRL-1 sends nothing on the jack.

## Which part a channel plays

**ROUT** on SYSTEM 2 (tap GLO twice) decides where incoming notes go. It is kept with a project.

| ROUT | Channels heard |
| --- | --- |
| SEL | every channel plays the pair in front, whichever part it is on (SQRL-1's default) |
| CH1-4 | channels 1 to 4 play parts 1 to 4; the others are ignored |
| CH5-8, CH9-12, CH13-16 | that block of four plays parts 1 to 4 |
| TRACK | each part on a channel of its own; SQRL-1 has no page to set them, so with the defaults it behaves as CH1-4 |

The pair in front plays on part 1 or part 2, alternating each time you change pairs; ALIVE plays on part 3 and RUDIMENT
on part 4, and a part no pair holds is muted. SEL suits playing from a keyboard, because the notes follow you from pair
to pair. CH1-4 suits a sequencer that plays BEAT and LINE on their own channels. Changing ROUT releases every
note held on a channel no longer heard. SEL is the default only for a new project: a project saved under Felucca keeps
its ROUT.

## Notes

An incoming note plays the SOUND on that part, at its velocity. If the part plays CHORD, each note is played as CHORD
would build it from the key (STRM, TOP and TUNE act on the FM-1's own keys only); with the arpeggiator on, the notes go
to the arp. TONNETZ, ENIGMA, S+H, CANON and HARP take the FM-1's keys only: MIDI notes go straight to the SOUND.

Pitch bend reaches the part, ±2 semitones by default. RPN 0 (CC101 and CC100 at 0, then CC6 and CC38) sets the
range, up to 24 semitones and 99 cents. CC64 is the sustain pedal.

## Controllers

A fixed set of CCs is always on. Each sets its parameter on the part the channel plays, as turning the knob would,
from 0 to 127 over the parameter's whole range (64 is the middle of a bipolar one).

| CC | Sets |
| --- | --- |
| 1 | the mod wheel |
| 5 | GLD, the glide time |
| 6, 38 | data entry for RPN 0 (the bend range) |
| 7 | LVL, the part's level |
| 10 | PAN |
| 11 | expression |
| 64 | sustain |
| 71 | the engine's resonance, where it has one |
| 72 | REL |
| 73 | ATK |
| 74 | the engine's cutoff or brightness, where it has one |
| 75 | DEC |
| 91 | REV send |
| 93 | CHO send |
| 94 | DLY send |
| 98, 99 | NRPN select (cancels RPN entry) |
| 100, 101 | RPN select |
| 120 | All Sound Off: the part goes silent at once, past its release |
| 121 | Reset All Controllers: bend, mod wheel, aftertouch, expression and sustain back to rest; the CCs above keep their values |
| 123 | All Notes Off: every held note releases normally, the sustain pedal respected |

CC74 and CC71 reach an engine's own filter, such as ANALOG's CUT and RES. The mod wheel, expression (CC11) and channel
aftertouch are sources in the part's modulation matrix; SQRL-1 has no page for the matrix, so they act only where a
sound already routes them.

Program Change loads a factory sound on the part the channel plays: program 0 is the SOUND's first factory sound,
1 the second, and so on. A program past the last is ignored.

MIDI LEARN, Felucca's way of setting any CC to any knob, has no control in SQRL-1. A map learned under stock
Felucca is kept in the FM-1's settings and still applies: those CCs set their learned parameters instead of the
table above. Felucca's SCREEN OFF timer is kept the same way: if the screen goes dark after some idle minutes,
that is the setting, and any button, key or knob wakes it.

## MIDI out

The FM-1's keys go out over USB MIDI as notes, at the velocity they were played. Under ROUT SEL they go out on
channel 1 whichever part the pair is on, so outboard gear stays on one channel; under CH1-4 (or another block) on
the channel of the part the pair is on. Under CHORD the whole chord goes out, strummed as it sounds. The
arpeggiator, the pattern and the NOTE engines that make their own notes (ALIVE, TONNETZ, ENIGMA, S+H, CANON, HARP) are not sent.
SQRL-1 sends no clock.

## Clock

**CLK** on SYSTEM (tap GLO) picks where the tempo comes from: INT, the FM-1 itself; USB, MIDI clock over USB; or TRS,
MIDI clock on the jack. Only the source picked is heard; clock on the other port is ignored.

On USB or TRS, SQRL-1 follows the incoming clock's tempo, which shows as BPM (40 to 240), and its Start, Continue and
Stop: Start plays from the top, Continue from where it stopped. The patterns and the arpeggiator follow it. If
the clock stops for half a second while playing, the transport stops, so a pulled cable never leaves notes hanging.
There is no count-in under external clock.

Pressing PLAY on the FM-1 under external clock starts the transport, but without clock coming in it stops again
half a second later: start and stop from the clock's source instead.

## USB audio

The computer sees an audio input with two channels, the FM-1's output: what the speaker and the headphone jack get,
after the effects, but without the metronome. The level follows the MASTER knob. The computer picks 44.1 kHz or
48 kHz; SQRL-1 runs at 44.1 kHz and resamples for 48.

On macOS 13 to 15 the audio input may not appear while the serial console is presented: macOS then gives the whole
device to its serial driver. macOS 26 and later, Windows, Linux, iOS and iPadOS are not affected. Felucca's MENU has a
USB SERIAL switch for this; SQRL-1 has no control for it. A build made with `FELUCCA_CDC_DEFAULT=0` (see
[Building it yourself](build.md)) leaves the console out from power-on.

## The serial console

The console is a developer's tool. Open the FM-1's serial port in any terminal program (the baud rate does not
matter) and it answers with its version and a prompt. Type `help` for the commands:

| Command | Shows |
| --- | --- |
| `status` | version, uptime, CPU load, late audio blocks, voices shed, the part in front and its sound, tempo, USB, USB audio and TRS MIDI counters |
| `stack` | each stack's size, the deepest it has been since power-on, and what is left |
| `crash` | the record of the last crash, if any |
| `inp` | the panel scan's timing since the last `inp` |
| `params` | the front part's parameter values, by number |
| `dbg` | the audio loop's debug counters (and USB audio's) |
| `memr ADDR [LEN]` | memory, in hex |
| `flr OFF [LEN]` | flash, in hex |
| `uboot yes` | restarts into the FM-1's update mode |

Everything but `uboot yes` only reads. The output of `status`, `stack` and `crash` is what to attach to a bug report.

## The installer and the editor

Both are web pages that talk to the FM-1 over USB MIDI with SysEx. They need Chrome or Edge (Firefox and Safari have
no Web MIDI), and the browser asks once for permission to use MIDI devices with SysEx: allow it. Close other programs
that hold the FM-1's MIDI port first, and if a page still can't find the FM-1 or gets no reply, restart the browser:
a stale tab can hold the port.

The **installer** puts SQRL-1 on the FM-1 ([Getting started](start.md)). It also has:

- **Set up your wheel**: read the wheel from the FM-1, choose a nut for each key and save, or **Default layout**
  ([The wheel](wheel.md)).
- **Return to official V15**: takes the FM-1 back to M-VAVE's firmware. Download official FM-1 V15 from M-VAVE
  (FM-1, "PC Firmware") and choose the unmodified `FM-1.fwsc`; the page checks it in the browser and sends it
  nowhere. Before it writes, it
  saves a complete backup to the computer. If the return is interrupted, tick **Resume an interrupted update** and
  press the button again.

The **editor**, linked from the installer, edits the FM-1 live from the computer: the sound in front, FM6's patches, the
pattern, the mix, the factory and user sounds, the sample slots (for a build with SAMPLE), the projects and backups, and the wheel. Press
**Connect** first. Its engine menu shows the engine of the SOUND in front and loads that engine's sounds; to play
another engine, change the SOUND on the FM-1 (hold HOME and tap GLO for the PAIR page).

## Backups

A complete backup is a single file on the computer holding the music in RAM, the four projects A to D, the user
sounds, FM6's user patches, MOUTH's phrases U1 to U8 and the settings, the wheel's layout among them, and what each
project keeps beside it (its parked takes). SQRL-1 has no user sample slots (their flash holds that): an older
backup's samples are left out when it is restored, and the editor says so. In the editor, open **Projects**: **Save backup** writes the file, **Restore backup** puts one back.
Stop the transport first and keep the cable in until it says it is done.

Back up with SQRL-1's editor. SQRL-1 stores projects in a format of its own: stock Felucca's editor saves them as
empty, so restoring its backup erases them ([The shell](shell.md)).
