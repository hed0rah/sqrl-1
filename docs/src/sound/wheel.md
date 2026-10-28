# WHEEL

WHEEL is Felucca's tonewheel organ: pick a drawbar registration from a list, shade it with three knobs, and add
percussion, key click, drive and a rotary speaker.

<!-- auto:card -->

## Playing it

REG on MAIN picks a registration, a preset setting of all nine drawbars (FLUTE, GOSPL, ROCK, FULL and others).
PERC adds the struck 2nd or 3rd harmonic, normal, soft or slow; it is single trigger, so only a note started with
no other key held gets it. DRV is a soft overdrive on each voice. ROTR spins a rotary speaker SLOW or FAST, and the
rotors ramp between the speeds rather than jump.

WHEEL is [on the sidelines](../sidelines.md): SQRL-1's build leaves it out. A custom build with `FELUCCA_WHEEL=1`
brings it back, picked on the PAIR page (hold HOME, tap GLO, KNOB 1). Without it, a WHEEL sound in a project or a
saved sound plays on ANALOG.

<!-- auto:home -->

<!-- auto:pages -->

## Pages

### BARS

SUB, BODY and TOP raise or lower groups of the registration's bars: SUB the 16' and
5-1/3', BODY the 8' and 4', TOP everything from 2-2/3' up. Louder registrations give each bar a little less, so a
full organ is louder than one bar without clipping.

### TONE

CLICK is the burst of noise at the key contact. With PERC on, the 1' bar falls silent and, at
normal level, the other bars drop about 3 dB, as on the original circuit.

## WHEEL and ORGAN

Both are tonewheel organs. WHEEL works from a list of registrations with three shading knobs and has its own drive
and rotor; it plays nine sines per voice. ORGAN sets each drawbar directly (the top three as one) and reads one
wavetable a voice; it has no rotor, so it pairs with the ROTOR insert.

## Where it comes from

`firmware/src/eng_wheel.c`, Felucca's own design (GPL-3.0), by Leo Kuroshita, Hügelton Instruments.
