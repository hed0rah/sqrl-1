# Credits and licences

SQRL-1 is a fork of [Felucca](https://github.com/hugelton/Felucca), the open firmware for the FM-1 by Leo Kuroshita
(@kurogedelic), Hügelton Instruments. Everything under the shell is Felucca's: the engines it shipped with, the
voices, the sequencer, the effects, the storage and the installer. SQRL-1 is free software under Felucca's
licence, GPL-3.0-only, and anyone who distributes it, changed or not, must offer its complete source under the
same terms. SQRL-1's source is at [github.com/hed0rah/sqrl-1](https://github.com/hed0rah/sqrl-1).

## SQRL-1's own parts

The shell, the wheel and LIZARD are hed0rah's, under GPL-3.0-only like the rest. The parts that came from databurn,
an audio research group, are MIT: SYLLABLE, VODER and MOUTH (with its letter-to-sound rules), ORGAN, SHOCK,
TONNETZ, ENIGMA, S+H, CANON and HARP, CHORD's playing and PROG, the SHAPE stage, the tape echo, LOOP's tape, the chorus modes, and
the ROTOR, RESON and VOWEL inserts. CHORD extends Felucca's chord keys and
ALIVE is part of the shell; both are GPL-3.0-only. Art and marks, the squirrel and Nibs included, are hed0rah's, under
CC-BY-SA-4.0; the names are not licensed as trademarks.

## From elsewhere

- FM6's operator core is msfa, the synthesis core of Dexed (copyright Google Inc. and Pascal Gauthier), ported to
  integer C by Leo Kuroshita (Apache-2.0).
- PHYS ports DaisySP (Electrosmith and Emilie Gillet) and the sympathetic strings of Mutable Instruments' Rings
  (Emilie Gillet), both MIT.
- SQRL-1 builds eight SOUNDs. SAMPLE, LOFI, VOICE, TRIO, GRAIN, NOISE, SLICE, WHEEL and databurn's SYLLABLE, VODER,
  ORGAN and LOOP are in the source and built with a flag each (core.h); their credits stand with their code.
- TALK, a TMS5220 voice from databurn (MIT), is in the source but not built: it has no word banks cleared for
  distribution, and its bank format and encoder are not published yet. Its tables and generator follow MAME (Frank
  Palazzolo, Aaron Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud, Michael Zapf; BSD-3-Clause), whose notice
  goes here once TALK is in a build.
- SAMPLE's instruments, in a build with it, are from Versilian Studios' VSCO-2 Community Edition and VCSL by Sam
  Gossner, released as CC0.
- The screen's typeface is Inter Tight by The Inter Project Authors (SIL Open Font License), and its icons are
  Hügelton's Fukiai font (MIT).
- SCALE's scales after Felucca's own sixteen are Braids' and Yarns' by Emilie Gillet (MIT).
- RUDIMENT's MAP is Mutable Instruments' Grids by Emilie Gillet: its drum map, the 25 nodes as they are, read as
  Grids reads them (GPL-3.0-or-later, used under GPL-3.0).
- Every firmware package also holds three unmodified files from JieLi's AC79 SDK: `uboot.boot`, the boot loader;
  `cfg_tool.bin`, the chip configuration; and `eq_cfg_hw.bin`, the default EQ table. They are Apache-2.0, copyright
  JieLi Technology, and none of them is in the source tree.

## By file

These are the files in `firmware/src` whose licence or author differs from Felucca's, as their headers state them.
Every other file there is Felucca's, GPL-3.0-only, copyright Leo Kuroshita.

<!-- auto:licences -->

<!-- auto:notice BSD-3-Clause-MAME TALK -->

The full licence texts travel with the firmware package and are in the repository's `LICENSES/`.
