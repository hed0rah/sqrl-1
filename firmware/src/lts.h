/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * lts: English text to MOUTH phonemes by letter-to-sound rules, after the US Naval Research
 * Laboratory's (Elovitz, Johnson, McHugh and Shore 1976, NRL Report 7948, a US government work in
 * the public domain). Each rule is left context [match] right context = phonemes; the first rule of
 * a letter that fits wins. Not for the audio path: it walks text, no allocation. */
#ifndef LTS_H
#define LTS_H
#include <stdint.h>
#include "mouth.h"

/* the words of text (letters; anything else separates them, case ignored) as phoneme bytes, each
 * word's last with MTH_EOW. Returns the count written, at most max */
uint32_t lts_text(const char *text, uint8_t *out, uint32_t max);

#endif
