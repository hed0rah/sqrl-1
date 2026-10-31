# Changelog

## v1.0.1

- Credits and copyright name databurn: ABOUT, the source headers, LICENSING.md and the manual's credits page.
  Nothing else changes.

## v1.0.0

The first release. SQRL-1 is built on Felucca 1.5.6 and keeps its engines, voices, sequencer, effects, storage and
installer; this lists what it adds or changes.

### The shell

- Stopped, a key held on TONNETZ, ENIGMA, S+H, CANON or ALIVE plays it as the transport would, at its DIV, until
  the last key is up, so every NOTE engine makes a sound from the keys without PLAY.
- HARMONY (SCL twice): each note a NOTE engine plays brings a third, fifth, triad, seventh, sus, sixth, add9 or
  octave in the scale, CLOSE OPEN DROP2 or WIDE, above or below it; each NOTE engine keeps its own, and a project keeps
  them. MASK: on SCALE the keys draw the scale (pressed together: those notes from the lowest; a tap: one in or out)
  as SCALE USER, kept with the project.
- A project saved while it plays waits for the stop (SAVES TO C AT STOP) instead of refusing.
- Felucca's song chain is left out (the queue switches slots on the bar), which frees 13 KB of RAM for the above.
- QNT CHORD: the scale is the chord PLAY sounds, so S+H, ENIGMA, ALIVE, CANON, the lanes and ACID follow chord changes, and
  KEYS on one PLAY part rounds to the other's chord. FLUX on S+H's and ACID's LINE page: a locked S+H loop or an ACID
  line changes step by step (ACID's notes, accents and slides) and keeps the changes; FLUX 0 freezes it.
- S+H's LINE page also has CLK (its own steps, every BEAT hit, or one BEAT lane: the line locks to the kick) and LEAP
  (the largest move, in degrees). HARP: PAD is on by default, so a root key plays its chord; STRUM on its LINE page strums
  the plate by itself while a root is held, UP DOWN UP+DN or RAND over SPAN strings, running or stopped.
- Felucca's screens are replaced by a shell built around pairs: a SOUND makes the tone, a NOTE engine decides the
  notes, and you play one of each.
- The wheel (hold HOME) picks the pair from the octave C4 to C5: the white keys are the eight SOUNDs, the black keys
  NOTE engines. GLO plus a black key in the open wheel puts another NOTE engine there, kept over power-off.
- The PAIR page (hold HOME, tap GLO) picks the SOUND, the NOTE engine, the preset and a track's mute with the four
  knobs.
- Three tracks. BEAT (RUDIMENT, part 4) and LINE (LINES, ALIVE, TONNETZ, ENIGMA, S+H or CANON, part 3) keep playing
  when you leave them; PLAY (KEYS, CHORD, HARP, parts 1 and 2) is what you play, with takes, and a take loops on when you
  leave it too, two at once. A third PLAY pair takes an empty, then a muted, then the longest-left part; the take
  there stops and comes back with its pair. HOME + PLAY on a pair in the wheel mutes its part, and HOME glows while
  one is; HOME + REC on one brings it to the front under REC's rings, to undo or clear it there. PLAY on the
  wheel's centre stops everything. In the wheel, the keys of every pair that plays flip on its
  beat and a muted pair's breathe; opened while playing, it names what plays behind. ENIGMA and TONNETZ run under HARP, each on its own channel.
- SYSTEM, SYSTEM 2, PROJECTS, ABOUT and LEGAL are the shell's own pages; SELECT walks them both ways. Hold GLO and
  tap any button for a card of what it does.
- Two views per pair, SOUND and NOTE: a HOME tap flips between them (TAB), each where it was left; twice, the pair
  playing behind (and again, the next, round them all) or the pair played before, in the same view. A wheel pick of a new NOTE engine opens its NOTE view, of a SOUND alone its SOUND
  view. SELECT walks the view's pages, ALGORITHM turns the view's own, and HOME is lit for SOUND, breathing for NOTE.
  SOUND MAIN is the engine's four with its picture, NOTE MAIN the NOTE engine's.
- The SOUND pictures show what sounds as well as what is set: FM6's operators light, SHOCK's wave steepens with the
  envelope, DRUMS' pads flash, ANALOG's cutoff and PHASE's bend ride the envelope, PHYS marks the time since the
  strike. On SOUND MAIN, what a knob sets lights in its colour as it turns.
- LINES' octave pages: OCT- and OCT+ page the lanes by octaves, -2 to +2, and each hit keeps its own octave, so a
  line runs over several. Playing, the lane keys glow as their lanes hit, the busiest brightest; a hit on
  another octave page pulses OCT+ or OCT-. LIZARD's DRAW pages open under any NOTE engine; INSERT shows no A B C while its type is
  OFF.
- The lights answer: a detent clicks the page button, a key let go fades, TAB dips HOME and the double tap blinks
  it, SEQ blinks as a pattern lands. The peek draws sqrl on the keys and flashes the panel on a corner.
- TONNETZ: SPRD is VOIC, with CHORD's INV1 INV2 DROP2 and VAR after CLOSE OPEN WIDE (which sound as before).
- Glide: each GLD value now has its own speed (the top third did nothing before), TIME reaches the 10.0 s it shows,
  and LEG glides on every SOUND. VOICE's SPRD reads WIDTH; GLD is one value on VOICE and GLIDE.
- LINES: a pattern switched in live keeps each hit's octave.
- The header marks a part that holds notes or hits: a ring and the loop's bars, in both views.
- RUDIMENT's rings under any SOUND: NOTE MAIN draws them with the lane's note in the middle; its NOTES page keeps
  the rows for setting pitches.
- REC + a key while it plays (KEYS, CHORD): that note leaves each step the playhead passes, for wrong notes in a loop.
- Double taps: ARP twice toggles HOLD, REC twice flips + KNOBS (knob moves recorded or not) and keeps the arm.
- ARP goes straight to the ARP page, MODE first, from any page; again ARP 2. SOUND MAIN starts with the engine's
  switch: ANALOG's and PHASE's WAVE, PHYS' MODEL, DRUMS' KIT, LIZARD's MODE, MOUTH's PHRASE.
- The peek is sqrl's alone: it bounces, and a touch sends it off the screen. Nibs left GLO's walk and hides on ABOUT.

### Nuts

- SOUND: eight engines, one on each white key: FM6, LIZARD (draw the wave), SHOCK, DRUMS (the kits), MOUTH (SAM-style
  speech), ANALOG, PHASE and PHYS. The other engines, Felucca's and databurn's (SAMPLE, LOFI, VOICE, TRIO, GRAIN,
  NOISE, SLICE, WHEEL, SYLLABLE, VODER, ORGAN, LOOP), stay in the source, each built back with a flag; a sound saved
  on one plays on its nearest of the eight (MOUTH, PHYS or ANALOG). SAMPLE's instruments leave the image with it. The manual keeps their pages, "On the
  sidelines".
- TALK, a TMS5220 voice, is in the source but not built: it has no word banks cleared for distribution, and its
  bank format and encoder are not published yet.
- NOTE: CHORD (Orchid-style voicings, strum, just tuning and RPT, a strum that repeats on the beat), RUDIMENT (eight
  lanes of steps, each its own length, over any SOUND), LINES (RUDIMENT's lanes again, as rows of notes, for a line
  over the beat), ALIVE, TONNETZ, ENIGMA, HARP, S+H and CANON.
- ACID: ALGORITHM on LINES writes a seeded line on its lanes as long as LOOP (up to 32 steps), after TB-3PO (one note
  a step in SCALE, accents, slides, ties); DENS adds or takes notes from the same line, SPAN stretches its range, and
  each number is the same line again (1 to 255). TRN moves LINES by semitones as it plays; STYLE draws it as WALK,
  OCTAVE, PEDAL, ARP or CALL.
- LANE 2 arranges the lane in front (RUDIMENT and LINES): PAD adds empty steps after FILL's, LOOP repeats its
  chances (OCT+ rolls new ones), and WAKE and NAP play some bars and rest others. A project keeps all of it.
- RUDIMENT's lanes: LEN, FILL and ROT per lane, after Euclidean Circles; FILL turned left past 0 on lanes 3 to 8 makes
  a logic lane, AND, OR, XOR, NOR, A-B, FLIP or LATCH of the two lanes above it. LEN keeps a lane's hits, the steps
  past a shorter LEN too; FILL and ROT write the lane.
- RUDIMENT and LINES mark a step SLIDE, TIE or, on a hit, ACCENT with SEQ held and its key (under DRUMS, ACCENT only).
- RUDIMENT's ALGORITHM gains MAP: Mutable Instruments' Grids (Emilie Gillet) plays KICK, SNARE and
  HATCL live from its 5 x 5 drum map, X and Y picking the place, CHAOS and ACC as Grids', and a density per lane (MAP
  and MAP 2 on ARP). Your own hits play on top, the map's are shown faint; EVOLVE drifts X and Y; projects keep it.
- FREEZE: REC held + OCT+ writes what a generator plays into lanes to edit, with one undo: MAP's beat into RUDIMENT's
  lanes, an ACID line left to the hand, and the last loop of ALIVE, TONNETZ, ENIGMA, S+H or CANON into LINES' lanes
  (a pitch a lane), LINE then playing LINES.
- S+H, after Ornament & Crime's Quantermain: each step samples a source into SCALE over RANGE, from the last key; a
  held key holds the sample. NOISE, WALK (brown), LOGIS (the logistic map), BYTE (stepped bytebeats) and INT (pi,
  van Eck, Dress, Norgard), each with its own P.
- CHORD's PROG, after Acid Curds: a held or latched root plays a progression of the scale's chords (POP, 50S, JAZZ,
  BLUES, ANDAL, PACH, 5THS, SAD, ROYAL), moving every half, 1, 2 or 4 bars while playing, a chord per press stopped;
  DIR (FWD, REV, PEND, RAND, BROWN) and LEN. Projects keep it.
- CANON, after CopierMaschine: what you play goes into a 64-step ring, and one to three voices answer DELAY steps
  apart and DEG degrees of SCALE away; HOLD loops the last 8 to 64 steps behind the keys.
- RUDIMENT: REC held through its second ring clears every lane (REC and a lane key clears one). The lane keys with
  hits are lit mid, the empty ones faint; with the lane in front empty, the other lanes' hits glow on the white keys.

### Playing and sequencing

- A step holds up to six notes, each with its just tuning.
- One LEN per part (1-64), knob 1 of every SEQ page, so parts run different lengths against each other. RUDIMENT's
  and LINES' lanes follow it (=LEN) or take their own cycle below it; LEN is their loop. On the generators LEN is the
  cycle: ENIGMA's register, CANON's HOLD loop, S+H's LOCK, and TONNETZ and ALIVE start again from their seed each
  cycle. MAP sets LEN to 32, the moons to their bar.
- Step pages: the left and right black keys past C5 (C#5, D#5) turn the white keys to steps 17-32, 33-48 and 49-64,
  with nothing held on RUDIMENT, LINES and the SEQ page, with SEQ held elsewhere. The page stays where you put it
  while the pattern plays (F#5 turns FOLLOW on, if you want it); the header shows P1/2, and the page key toward the
  playhead pulses on the beat.
- SEQ held and a white key under CHORD enters the last chord whole.
- A take under CHORD plays back through STRM, TOP and RPT, so turning them later changes it; RPT's strikes are not
  recorded.
- An action REC can undo says HOLD REC: UNDO under its toast.
- LIZARD starts POLY.
- The arpeggiator gains dotted rates (1/4., 1/8., 1/16.) and 1/4T and 1/32T, and locks to the beat grid while the
  transport runs. Felucca's arp ran slow by up to 14 % at fast rates.
- HOLD latches the keys on any pair when no arp mode is on.
- GROOVE (SYSTEM 2) shapes the swing: SWING, SHUFFLE, PUSH, LAID and HUMAN. It is saved with a project.
- The tempo holds the set BPM exactly; at 120 BPM Felucca ran about 0.01 % fast, 16 ms ahead of a DAW over 64 bars.
- CLICK and COUNT-IN are on SYSTEM 2.
- MIDI IN follows the pair in front by default (ROUT SEL on SYSTEM 2), and the FM-1's keys go out on channel 1.
- Recording takes what you play on PLAY; LINE's generators are its content and are never recorded. REC held through
  its second ring clears the pattern and its knob moves, and UNDO TAKE takes back a take's notes, length and knob
  moves.
- REC MODE (REC held, SELECT turned): ADD, ADD + KNOBS, REPLACE (this pass's notes take the place of an earlier
  pass's in each step they go in) and REPLACE + KNOBS; the header says RPL in REPLACE. It replaces Felucca's NOTES /
  NOTES AND KNOBS choice.
- One press records as one step: a strummed chord goes whole into the step it was pressed in, and a legato chord keeps
  the notes it shares with the one before. A held note ties only into empty steps, so an overdub never erases, HOLD's
  latched chords included. The arp rewrites its steps each pass instead of piling up. Under QNT SEQ+T a note plays back
  as it was heard.
- A first take sets the loop's length in bars, from stop, after a count-in or over BEAT and LINE, and never
  cuts a note off; the header shows it open as `REC 3/--`.
- Hold a step to edit it, wherever the white keys are steps (the lanes, the SEQ page, ACID): a tap is the step on or
  off as you let go; held, C#5 ACCENT, D#5 SLIDE, F#5 TIE, the lane keys stack hits on it, and on KEYS and ACID a
  black key sets its degree, OCT its octave, SELECT its semitone, each note heard. The knobs set the held step's
  CHNC (and the conditions 1:2 to 4:4), RATCH, NUDGE and VEL, shown on the cards; held into a SOUND page, a knob
  locks its value on that step. SEQ is lit solid whenever the white keys are steps.
- A step's NUDGE plays (Felucca played it only with QUANTIZE OFF, which SQRL-1 has no key for); a live take stays on
  the grid.
- Switching patterns live: SEQ held + PRESETS queues a PROJECTS slot's pattern for the part in front, on the next bar
  line, its SOUND kept.
- RUDIMENT and LINES: each lane has its own CHNC and RATCH per step (x3 on the hats, chance on a snare ghost, the
  rest untouched), kept with the project; an older project's step CHNC goes to every lane.
- FILL: hold SCL while it plays. A held step's CHNC reaches FILL and !FILL past the loop conditions.
- REC ADD with the arp recording keeps the chords a step holds; each pass replaces only the arp's own notes.
- A first take armed while the transport runs loops from the bar of its first note, not from the bars before it.
- A lane's DIV on RUDIMENT and LINES (a lane key held, KNOB 1): its step against the pattern's, x2 to 1/4, so a
  lane plays triplets or double time over the others, on a clock of its own; a project keeps it.
- A PLAY take (KEYS, CHORD, HARP) is the pair's: FM6 and ANALOG on KEYS keep a take each, and up to three parked
  takes wait for their pairs.
- HARP on SCL CHR stacks the major scale's thirds, as CHORD does; TONNETZ's and HARP's chords play whole on a MONO or
  LEGATO SOUND; RHY on RUDIMENT's and LINES' NOTE page loads the rhythm as ALGORITHM does; ACID's REC ring puts back
  the hand line's LINE, so DENS and SPAN never write over it.
- CHORD's B3 adds the note its toast names to every chord, the same note over any root (it added an interval from
  the last chord's root, so a note added before any chord came out wrong). RUDIMENT's and LINES' SEQ page is LEN DIV
  SWG GATE, as every other.
- A step's CHNC, RATCH, NUDGE and locks show without holding it: marks under the step (a dot, a tick, a dash, a
  diamond), a dot outside RUDIMENT's ring, and its key mid in place of lit.
- RUDIMENT and LINES live: LEN spreads a FILL lane's hits over the new length and only trims a hand lane, ROT turns
  hand hits and accents too, FILL asks before writing over a hand lane, and a lane key held turns that lane's LEN
  FILL ROT on any page; every change lands at once. LINES has RUDIMENT's per-lane NOTE page; its line writer is LINE
  (DENS SPAN on a LINE page), and the first hand edit keeps the line.
- ACID, a 303 on LINE: one note a step with ACCENT, SLIDE and TIE, a 303's pitch mode (stopped, REC armed), the keys
  transposing it as it plays, MAIN LEN ROT TRN OCT, LINE's seeded lines on ALGORITHM; ANALOG's ACID sound on LEGATO the
  first time. Not on the default wheel.
- Blank means blank: a generator only visited leaves LINE as it was, the PAIR page puts its pick in front on leaving,
  NEW clears the lanes, rhythms, lines, arps, generators, EVOLVE and MOVE and is the autosave at once, power-on lands
  on a generator only with its music, and ARP HOLD needs a long hold.
- SEQ held and OCT+ doubles the pattern in front (to 64 steps), while it plays and while you record, RUDIMENT's and
  LINES' lanes too; SEQ held and a tap of OCT- halves it, and a DOUBLE straight after gives back what was there.
- QNT gains NEAR (the nearest scale note) and DROP (a note off the scale is silent, so a scale change changes which
  notes play).
- MOVE (SELECT on from SCALE) moves the scale every 1 to 8 bars while it plays: to the next mode, a fifth up, to a
  second scale and back, or anywhere, with each part's own scale back at the stop.
- EVOLVE (SEQ tapped again from SEQ) changes a pattern as it plays, every 1 to 8 bars, never in its stored steps: a
  take moves by +3RD, +5TH, OCT or a progression; LINES' hits move a degree or a step; RUDIMENT's drift. BACK goes
  home after so many changes, and each part evolves the same way each time.

### Projects and backups

- Four project slots (PROJECTS, GLO's third page) keep the music with the pairs that played it, BEAT's and LINE's
  and their mutes too. NEW, past D, empties the music and keeps the sounds, with UNDO NEW until you change something.
- The music comes back after power-off on its own, with its pairs: it is written once stopped and quiet, never
  while anything sounds, and not restored after a crash. Left unrestored, it is never written over until you save to
  or load from PROJECTS, and the first start after an install counts as clean.
- A project keeps what you left off its parts beside it: the takes parked with pairs no part holds, LINE's and
  BEAT's patterns under the NOTE engines not playing, the NOTE engines' settings, the SOUNDs' sounds and the parked
  knob moves. A load and a power cycle bring them back; a save cut short loads the project alone.
- A complete backup carries SQRL-1's projects with what they keep beside them, its sounds, settings and MOUTH's
  phrases, and restores Felucca's backups too. SQRL-1 has no user sample slots (their flash holds the projects'
  companions): an older backup's samples are left out, and the editor says so.

### Effects

- A three-head tape echo with wow, saturation and reverse, a reverb pre-delay, ensemble and dimension chorus modes,
  and a rotating-speaker insert.
- The FX layer: hold FX and the keys play Felucca's effects on the whole mix (repeats, reverse, filters, tape stop,
  freeze, octaves, flanger, phaser, throws) and mute parts, KNOB 1 to 4 are its macros, and the screen shows the
  map, with FX lit while it is held. A tap of FX still walks the pages.

### Installer and editor

- The installer sets up the wheel's black keys from the computer, and the editor has the same panel.
- Return to official V15 (for the unmodified `FM-1.fwsc`) backs up first and is built to resume if interrupted.

### Under the hood

- A voice is shed at once when the sound engine runs out of time, so an overload costs one glitch at most.
- The USB device names itself SQRL-1 (SQRL-1 Update while installing).
- The manual is generated from the firmware, and the tests fail if it falls behind.
