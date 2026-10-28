# Getting started

SQRL-1 installs from a web page over USB, with no extra hardware or drivers. The same page puts the FM-1's
official firmware back. Installing is at your own risk.

## Install

The installer, [databurn.org/sqrl/install](https://databurn.org/sqrl/install/), talks to the FM-1 over USB MIDI, so
it needs Chrome or Edge: Firefox and Safari have no Web MIDI.

1. Connect the FM-1 straight to the computer with a USB data cable (no hub). Close other programs that use MIDI.
2. Open the installer and allow MIDI with SysEx when the browser asks.
3. Check that the version on the page is the build you mean to install, then press **Install**. Leave the cable
   alone until the page says it is done.

If the page can't find the FM-1 or gets no reply, close Bitwig or any other DAW or app using the FM-1's MIDI port, or
restart the browser: a DAW, or a stale tab, can hold the port. If something goes wrong, **Copy log** puts the installer's log on the clipboard for a bug
report. An interrupted install is built to resume when you press Install again, and an FM-1 that no longer starts
at all can be recovered with [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter). Neither has been
tried on a device yet.

To go back, download official V15, `FM-1.fwsc`, from M-VAVE's download page (FM-1, "PC Firmware"), open the same
page and use **Return to official V15**. It checks that the file is V15, saves a complete backup of your music and
sounds to the computer, then installs it. M-VAVE's own updater, M-UPGRADE, does not see an FM-1 running SQRL-1, so use
**Return to official V15** first; after that M-UPGRADE works as on stock. On Windows, close Chrome or Edge before
you run M-UPGRADE: only one program can hold the FM-1's port. Projects and sounds saved under SQRL-1 stay in the FM-1's
flash, but the official firmware does not read them.

If the FM-1 runs M-VAVE's FM1 Open, the installer stops: FM1 Open keeps its projects and samples where SQRL-1's code
goes. Back up your projects and samples and return to official V15 with M-VAVE's workbench first, then install
SQRL-1.

## The panel

```
┌────────────────────┬────────┬────────────────────────────────┐
│ MASTER   SELECT    │        │ KNOB 1  KNOB 2  KNOB 3  KNOB 4 │
│ PRESETS  ALGORITHM │ screen │                                │
│                    │        │ FX   SCL  ENV  LFO  EDIT  GLO  │
│ OCT-  OCT+         │        │ HOME SAVE ARP  SEQ  PLAY  REC  │
├────────────────────┴────────┴────────────────────────────────┤
│ keys F3 to G5; C4 is the fifth white key                     │
└──────────────────────────────────────────────────────────────┘
```

MASTER is the volume. SELECT walks pages, PRESETS walks sounds, ALGORITHM turns the setting that changes a nut's
character most, and KNOB 1 to 4 set the four values at the bottom of the screen.

## The first five minutes

The FM-1 starts on the squirrel, SQRL-1's mark. The first time, it then opens FM6 with KEYS: six-operator FM,
played straight from the keys, with BEAT and LINE empty for you to fill. After that it opens the pair you played
last, with the music you left.

<!-- auto:shot shell_splash The splash at power-on. Any control skips it. -->

1. Play a few keys. Turn **PRESETS** to walk FM6's factory sounds; its name shows for a moment and the knobs at the
   bottom show the new sound's values.
2. Turn **ALGORITHM**. In FM6 it picks the algorithm, the way the six operators are wired.
3. Hold **HOME**. The wheel appears: the white keys from C4 to C5 hold SOUND nuts, the black keys between them hold
   NOTE engines. They light up while you hold HOME, the pair in front brightest. Press the black key D#4 for CHORD,
   then let go of HOME.
4. Play one key at a time from D4 up: each key is now a chord in the scale. The nine keys below D4 set the chord's
   quality and make no sound on their own. Tap **SCL** (scale) to change the key and the scale every nut shares,
   and tap SCL again to come back.
5. Hold **REC** and press **PLAY**: the click counts in one bar, then play a few bars. Tap **REC** to close the
   take. The loop is the bars you played, rounded to whole bars (64 steps at most), and it plays on. Tap REC again
   to record over it: what you play adds to the loop and never erases it. To swap chords instead, hold REC and turn
   SELECT to REPLACE.
6. Press **PLAY** to stop if the loop is still playing: the FM-1 saves only while stopped. Tap **SAVE**, then **OCT+**,
   to keep the sound you are on. Tap **GLO** three times for PROJECTS, choose a slot with KNOB 1, and press OCT+ twice
   to save the music with its pairs for good. The music you leave also comes back by itself after a power cycle, on
   the pair you were playing: it is kept once the FM-1 has been stopped and quiet for
   <!-- auto:const as_idle_s word --> seconds. Your saved sounds follow the factory ones in PRESETS.
7. Hold **GLO** and tap any button to see what it does.

To choose with knobs instead of keys, hold HOME and tap **GLO**: the PAIR page picks the SOUND, the NOTE engine and
the preset.

To get back to plain keys, hold HOME, press the black key C#4 (KEYS on the default wheel) and let go: the SOUND
stays and the keys play as they are. Holding GLO and pressing C#4 does the same without the wheel, and tapping HOME
twice quickly goes back to the pair you played before.

[The shell](shell.md) covers the buttons, pages and knobs, and [The wheel](wheel.md) covers pairs and the slot
picker for the black keys.

## When something goes wrong

Most silent or stuck states come from a setting, and each has a short way out.

- **FM-1 not found.** The installer cannot reach the FM-1. Use a USB data cable straight into the computer (no
  hub), close Bitwig or any other DAW or app using the FM-1's MIDI port, the editor in another tab included, and press
  Install again. A DAW can keep the port after its window closes: quit it fully.
- **The FM-1 in update mode was not found.** The FM-1 restarted into its update loader, but the computer did not
  give the browser its MIDI port (Windows can miss it). Unplug and replug the FM-1, or restart the computer, and press
  Install again: the FM-1 stays in update mode until the install finishes.
- **MIDI (SysEx) access was not allowed.** The browser's MIDI prompt was refused. Allow MIDI in the site settings
  (the icon in the address bar) and press Install again.
- **No sound.** Turn up MASTER. TONNETZ, ENIGMA, S+H, CANON, ALIVE, RUDIMENT, LINES and ACID play with the transport, so press PLAY; the generators also play while you hold a key. If
  HOME glows, a part is muted: HOME + PLAY on its pair in the wheel brings it back ([The wheel](wheel.md)).
- **PLAY stops again after half a second.** CLK on SYSTEM is set to USB or TRS and no clock is coming in. Set it to
  INT ([Connections](connections.md)).
- **CHORD's screen says ROOT ONLY.** The SOUND plays one note at a time (VCE on its VOICE page is not POLY) or is a
  drum or one-shot sound, so only the root sounds. Set VCE to POLY or choose another SOUND.
- **The wheel says PLAY MUTES BEAT OR LINE.** PLAY in the wheel mutes a track; a PLAY pair (KEYS, CHORD, HARP) has
  none to mute ([The wheel](wheel.md)).

## Version, changes and bugs

The version shows in the square at power-on, before the squirrel, and on ABOUT (the fourth tap of GLO); the
console's `status` prints it too. What changed in each release is in
[CHANGELOG.md](https://github.com/hed0rah/sqrl-1/blob/main/CHANGELOG.md) in the repository.

Report bugs at [github.com/hed0rah/sqrl-1/issues](https://github.com/hed0rah/sqrl-1/issues) and name the version.
For an install problem attach the installer's log (**Copy log**); for a crash, the output of `status`, `stack` and
`crash` from the serial console ([Connections](connections.md)).
