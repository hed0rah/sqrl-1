# LIBRARY

LIBRARY puts most of the engines on one key. PRESETS walks all their factory sounds as a single list, with no trip to
the wheel.

<!-- auto:card -->

LIBRARY is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out, and SHOCK has its key, E4. A custom
build with `FELUCCA_LIBRARY=1` brings it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1).

## Playing it

Each step of PRESETS loads the next factory sound, and the engine changes when the list crosses into the next
engine's sounds. The list wraps round at the end. Hold GLO and turn PRESETS to jump a whole engine at a
time, landing on its first sound. Your saved sounds are not in this list: PRESETS walks them under an engine's own
SOUND.

The pages follow the engine that is playing. MAIN shows that engine's own four knobs, EDIT walks its pages, and
ALGORITHM turns its first parameter. The tables below show LIBRARY as it starts, on ANALOG; the page for each
engine has the rest.

The keys play notes. The screen shows the engine's picture where it has one, and the seed-of-life pattern where it
does not.

## Engines in the list

<!-- auto:library -->

<!-- auto:home -->

<!-- auto:pages -->

## Where it comes from

LIBRARY is part of the shell, `firmware/src/shell.c` (`CH_ENGINES` lists the engines in the order PRESETS walks
them). The engines are Felucca's and databurn's; their pages credit each one.
