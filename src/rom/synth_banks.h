// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the factory sample banks (CH, OH, RS, CP, MISC, EXP), synthesised from recipes.
//
// Each sound is a recipe: a generator (808-style square-cluster hats, FM, modal resonators, tonal drums, claps,
// cymbal partial banks, Karplus-Strong, chord stabs, formant voices, sweeps, glitch textures...) with a parameter
// list, plus an optional "vintage machine" post stage (EXP bank: CR-78, Linn-ish mu-law, 12-bit SP style, 8-bit,
// circuit-bent, tape). Fully deterministic: every recipe has a fixed seed.
#pragma once
#include "synth_util.h"

namespace dlx {
namespace syn {

int FactoryCount(int bank);                    // number of recipes in a factory bank (0 for USER)
const char* FactoryName(int bank, int index);  // display name (<= 16 chars)
// Render one factory sound (raw: not trimmed or normalised; sample_rom.cc does that). Returns false if no such sound.
bool RenderFactory(int bank, int index, Buf& out);

}  // namespace syn
}  // namespace dlx
