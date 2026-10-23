// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sound generators the factory recipes (synth_banks.cc) are made of. Worker thread only.
//
// Every generator takes ten parameters p[0..9] (meaning per generator below), a fixed seed and a stereo flag, and
// renders raw float audio at 48 kHz into a Buf (length chosen from its own decay times, capped at 4 s).
#pragma once
#include "synth_util.h"

namespace dlx {
namespace syn {

enum Gen {
  // p: pitch scale, decay60 s, bp1 Hz, bp2 Hz, bp2 mix, hpf Hz, osc set (0 808, 1 606, 2 909/metal, 3 CR),
  //    noise mix, open curve (fast-decay share), click
  G_HAT = 0,
  // p: carrier Hz, mod ratio, index (rad), index decay60 s, amp decay60 s, feedback, pitch-env start ratio,
  //    pitch-env time s, hpf Hz, noise mix
  G_FM,
  // p: filter (0 LP 1 BP 2 HP), Hz, Q, attack s, decay60 s, extra hpf Hz, colour (0 white 1 pink),
  //    sweep end ratio (0/1 none), sweep time s, click
  G_NOISE,
  // p: base Hz, ratio2, ratio3, ratio4, decay60 s, amp2, amp3/4, click, hpf Hz, drive
  G_MODAL,
  // p: bursts, spacing s, bp Hz, Q, burst time s, tail decay60 s, tail level, reverb mix, reverb time s, spread
  G_CLAP,
  // p: f1 Hz, f2 Hz, bp Hz, Q, fast share, decay60 s, duty, hpf Hz
  G_COW,
  // p: Hz, pitch start ratio, pitch time s, decay60 s, click, noise, noise decay60 s, noise bp Hz (0 = hp),
  //    drive, second tone ratio
  G_DRUM,
  // p: partials, lo Hz, hi Hz, decay60 s, noise, hpf Hz, bell, attack s, ring, high-partial decay ratio
  G_CYM,
  // p: Hz, decay60 s, brightness 0..1, pick brightness 0..1, stereo detune cents, body Hz (0 none)
  G_PLUCK,
  // p: root MIDI, chord type, voices per note, detune cents, cutoff start Hz, cutoff end Hz, filter time s, Q,
  //    amp decay60 s, wave (0 saw 1 square 2 saw + sub)
  G_CHORD,
  // p: Hz, vowel (0 a 1 o 2 e 3 i 4 u), duration s, glide start ratio, glide time s, vibrato cents, breath,
  //    second vowel (-1 none), formant shift, release decay60 s
  G_VOX,
  // p: start Hz, end Hz, sweep time s, decay60 s, wave (0 sine 1 square 2 tri), wobble depth, repeats,
  //    drive, repeat period s
  G_ZAP,
  // p: start Hz, end Hz, duration s, Q, shape (0 rise 1 fall 2 swell), tone, hpf Hz, colour
  G_SWEEP,
  // p: mode (0 data 1 stutter 2 bleeps 3 crackle), rate Hz, duration s, decay60 s, base Hz, bits, density
  G_GLITCH,
  // p: attack s, decay60 s, hpf Hz, bead Hz, bead Q, collisions/s, jingle, jingle Hz, 2nd stroke gap s, jingle decay s
  G_SHAKER,
  NUM_GENS
};

void Generate(int gen, const float* p, uint32_t seed, bool stereo, Buf& out);

}  // namespace syn
}  // namespace dlx
