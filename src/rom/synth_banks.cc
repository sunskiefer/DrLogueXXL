// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the factory sample recipes (see synth_banks.h; generator parameters in synth_gen.h).
#include "synth_banks.h"
#include "synth_gen.h"
#include "sample_rom.h"

namespace dlx {
namespace syn {

namespace {

// vintage-machine post stages (EXP bank mostly)
enum { V_NONE = 0, V_CR78, V_LINN, V_SP12, V_8BIT, V_BENT, V_TAPE, V_909, NUM_V };

void VintPreset(int v, Vint& o) {
  o = Vint();
  switch (v) {
    case V_CR78: o.drive = 0.4f; o.aa = 9000.0f; o.lp = 7500.0f; o.hiss = 0.004f; o.hp = 35.0f; break;
    case V_LINN: o.aa = 12500.0f; o.mulaw = true; o.rate = 28000.0f; o.lp = 13000.0f; break;
    case V_SP12: o.rate = 26040.0f; o.bits = 12.0f; o.drive = 0.3f; o.lp = 15000.0f; break;
    case V_8BIT: o.rate = 11025.0f; o.bits = 8.0f; break;
    case V_BENT: o.drive = 2.5f; o.rate = 7000.0f; o.bits = 5.0f; o.hp = 60.0f; break;
    case V_TAPE: o.drive = 1.0f; o.lp = 8500.0f; o.hiss = 0.008f; o.hp = 40.0f; break;
    case V_909: o.bits = 6.0f; o.rate = 32000.0f; o.lp = 16000.0f; break;
    default: break;
  }
}

struct Recipe {
  const char* name;
  uint8_t gen;
  uint8_t stereo;
  uint8_t vint;
  float p[10];
};

#define M 0
#define S 1

// ----------------------------------------------------------------------------------------------------- CH (16)
// G_HAT: pitch, dec, bp1, bp2, mix2, hpf, set, noise, open(fast share), click
const Recipe kCh[] = {
  {"808 CH",       G_HAT, M, V_NONE, {1.0f, 0.075f, 3440, 7100, 0.6f, 6000, 0, 0.0f, 0.35f, 0.0f}},
  {"808 CH 2",     G_HAT, M, V_NONE, {1.0f, 0.045f, 3440, 7100, 0.8f, 7000, 0, 0.05f, 0.5f, 0.1f}},
  {"909 CH",       G_HAT, M, V_909,  {1.0f, 0.07f, 8000, 10500, 0.7f, 7000, 2, 0.35f, 0.5f, 0.3f}},
  {"909 CH 2",     G_HAT, M, V_909,  {1.15f, 0.045f, 9000, 12000, 0.6f, 8000, 2, 0.3f, 0.6f, 0.4f}},
  {"606 CH",       G_HAT, M, V_NONE, {1.0f, 0.05f, 7500, 10000, 0.5f, 6500, 1, 0.25f, 0.4f, 0.0f}},
  {"CR78 CH",      G_HAT, M, V_CR78, {1.0f, 0.04f, 6000, 9000, 0.4f, 5000, 3, 0.15f, 0.3f, 0.0f}},
  {"Metal Hat",    G_FM,  M, V_NONE, {3100, 1.43f, 6.0f, 0.08f, 0.1f, 0.4f, 0, 0, 5000, 0.05f}},
  {"FM Hat",       G_FM,  M, V_NONE, {5200, 2.71f, 4.0f, 0.05f, 0.06f, 0.2f, 0, 0, 6500, 0.1f}},
  // G_NOISE: type, hz, q, att, dec, hpf, pink, sweep end, sweep t, click
  {"Noise CH",     G_NOISE, M, V_NONE, {2, 8000, 0.8f, 0, 0.06f, 6000, 0, 0, 0, 0.2f}},
  {"Pink CH",      G_NOISE, M, V_NONE, {1, 9000, 1.2f, 0, 0.07f, 4000, 1, 0, 0, 0.1f}},
  {"Dark CH",      G_HAT, M, V_NONE, {0.8f, 0.1f, 2500, 5000, 0.6f, 2800, 0, 0.1f, 0.4f, 0.0f}},
  {"Tick Hat",     G_NOISE, M, V_NONE, {2, 10000, 0.7f, 0, 0.018f, 7000, 0, 0, 0, 0.6f}},
  {"Pedal Hat",    G_HAT, M, V_NONE, {0.95f, 0.14f, 3000, 6500, 0.7f, 3500, 0, 0.15f, 0.75f, 0.0f}},
  // G_SHAKER: att, dec, hpf, bead hz, bead q, coll/s, jingle, jingle hz, gap, jingle dec
  {"Shaker Hat",   G_SHAKER, M, V_NONE, {0.008f, 0.05f, 5000, 9000, 0.9f, 3000, 0, 0, 0, 0}},
  {"Lo-Fi CH",     G_HAT, M, V_8BIT, {1.0f, 0.06f, 3440, 7100, 0.6f, 4000, 0, 0.1f, 0.4f, 0.0f}},
  {"Sizzle CH",    G_HAT, M, V_NONE, {1.2f, 0.09f, 9500, 13000, 0.8f, 8000, 2, 0.6f, 0.4f, 0.0f}},
};

// ----------------------------------------------------------------------------------------------------- OH (16)
const Recipe kOh[] = {
  {"808 OH",       G_HAT, M, V_NONE, {1.0f, 0.55f, 3440, 7100, 0.6f, 6000, 0, 0.0f, 0.25f, 0.0f}},
  {"808 OH 2",     G_HAT, M, V_NONE, {1.0f, 0.9f, 3440, 7100, 0.7f, 5500, 0, 0.05f, 0.2f, 0.0f}},
  {"909 OH",       G_HAT, M, V_909,  {1.0f, 0.6f, 8000, 10500, 0.7f, 7000, 2, 0.35f, 0.3f, 0.2f}},
  {"909 OH 2",     G_HAT, M, V_909,  {1.1f, 1.0f, 9000, 12000, 0.7f, 7500, 2, 0.3f, 0.3f, 0.2f}},
  {"606 OH",       G_HAT, M, V_NONE, {1.0f, 0.45f, 7500, 10000, 0.5f, 6500, 1, 0.25f, 0.3f, 0.0f}},
  {"CR78 OH",      G_HAT, M, V_CR78, {1.0f, 0.35f, 6000, 9000, 0.4f, 5000, 3, 0.15f, 0.2f, 0.0f}},
  {"Metal OH",     G_FM,  M, V_NONE, {3100, 1.43f, 6.0f, 0.5f, 0.7f, 0.5f, 0, 0, 5000, 0.05f}},
  {"FM OH",        G_FM,  M, V_NONE, {5200, 2.71f, 5.0f, 0.4f, 0.55f, 0.3f, 0, 0, 6000, 0.1f}},
  {"Noise OH",     G_NOISE, M, V_NONE, {2, 7500, 0.8f, 0, 0.5f, 6000, 0, 0, 0, 0.15f}},
  // G_CYM: partials, lo, hi, dec, noise, hpf, bell, attack, ring, high-partial decay ratio
  {"Sizzle OH",    G_CYM, S, V_NONE, {28, 3000, 16000, 0.7f, 0.25f, 5000, 0, 0, 0, 0.6f}},
  {"Dark OH",      G_HAT, M, V_NONE, {0.8f, 0.6f, 2500, 5000, 0.6f, 2800, 0, 0.1f, 0.25f, 0.0f}},
  {"Trashy OH",    G_HAT, M, V_SP12, {1.05f, 0.5f, 7000, 11000, 0.8f, 5000, 2, 0.5f, 0.3f, 0.2f}},
  {"Half Open",    G_HAT, M, V_NONE, {1.0f, 0.26f, 3440, 7100, 0.7f, 5500, 0, 0.05f, 0.5f, 0.0f}},
  {"Long OH",      G_HAT, M, V_NONE, {1.0f, 1.6f, 3440, 7100, 0.6f, 6000, 0, 0.02f, 0.2f, 0.0f}},
  {"Swish OH",     G_NOISE, S, V_NONE, {2, 7000, 0.9f, 0.06f, 0.45f, 5000, 0, 0, 0, 0}},
  {"Ring OH",      G_CYM, S, V_NONE, {24, 2500, 14000, 0.8f, 0.15f, 3500, 0, 0, 0.5f, 0.7f}},
};

// ----------------------------------------------------------------------------------------------------- RS (16)
// G_MODAL: base, r2, r3, r4, dec, amp2, amp3/4, click, hpf, drive
const Recipe kRs[] = {
  {"808 Rim",      G_MODAL, M, V_NONE, {455, 3.67f, 0, 0, 0.045f, 0.9f, 0, 0.6f, 300, 1.5f}},
  {"909 Rim",      G_MODAL, M, V_NONE, {500, 2.2f, 3.7f, 0, 0.035f, 0.6f, 0.4f, 0.8f, 400, 0.8f}},
  {"Rim Click",    G_MODAL, M, V_NONE, {1700, 2.6f, 0, 0, 0.015f, 0.5f, 0, 1.0f, 600, 0.5f}},
  {"Side Stick",   G_MODAL, M, V_NONE, {380, 2.4f, 5.2f, 0, 0.06f, 0.7f, 0.4f, 0.5f, 200, 0.6f}},
  {"Wood Block",   G_MODAL, M, V_NONE, {1150, 2.57f, 0, 0, 0.08f, 0.3f, 0, 0.25f, 300, 0.0f}},
  {"Wood Low",     G_MODAL, M, V_NONE, {640, 2.57f, 0, 0, 0.1f, 0.35f, 0, 0.25f, 150, 0.0f}},
  {"Clave",        G_MODAL, M, V_NONE, {2200, 2.9f, 0, 0, 0.18f, 0.1f, 0, 0.2f, 500, 0.0f}},
  {"808 Clave",    G_MODAL, M, V_NONE, {2500, 0, 0, 0, 0.12f, 0, 0, 0.1f, 600, 0.3f}},
  {"Metal Rim",    G_MODAL, M, V_NONE, {900, 2.76f, 5.4f, 8.93f, 0.25f, 0.6f, 0.4f, 0.4f, 300, 0.2f}},
  {"Tin Rim",      G_FM,    M, V_NONE, {1200, 3.5f, 3.0f, 0.04f, 0.07f, 0.2f, 0, 0, 400, 0.15f}},
  {"Snap Rim",     G_MODAL, M, V_NONE, {600, 4.1f, 0, 0, 0.03f, 0.5f, 0, 1.6f, 900, 1.0f}},
  {"Rim Deep",     G_MODAL, M, V_NONE, {300, 2.1f, 0, 0, 0.06f, 0.6f, 0, 0.4f, 100, 2.0f}},
  {"Rim Ring",     G_MODAL, M, V_NONE, {700, 2.1f, 0, 0, 0.3f, 0.5f, 0, 0.3f, 200, 0.0f}},
  {"Click",        G_MODAL, M, V_NONE, {3500, 1.6f, 0, 0, 0.008f, 0.5f, 0, 1.0f, 1000, 0.0f}},
  {"Block Hi",     G_MODAL, M, V_NONE, {1800, 2.6f, 0, 0, 0.05f, 0.3f, 0, 0.3f, 400, 0.0f}},
  {"Lo-Fi Rim",    G_MODAL, M, V_8BIT, {455, 3.67f, 0, 0, 0.05f, 0.9f, 0, 0.6f, 300, 1.5f}},
};

// ----------------------------------------------------------------------------------------------------- CP (16)
// G_CLAP: bursts, spacing s, bp Hz, Q, burst tau s, tail dec60, tail level, verb mix, verb time, stereo spread
const Recipe kCp[] = {
  {"808 Clap",     G_CLAP, M, V_NONE, {4, 0.011f, 1000, 1.3f, 0.0035f, 0.28f, 0.45f, 0, 0, 0}},
  {"808 Clap 2",   G_CLAP, M, V_NONE, {4, 0.012f, 1050, 1.3f, 0.004f, 0.5f, 0.5f, 0, 0, 0}},
  {"909 Clap",     G_CLAP, S, V_NONE, {4, 0.009f, 1200, 1.0f, 0.004f, 0.22f, 0.6f, 0.15f, 0.6f, 0.3f}},
  {"909 Clap 2",   G_CLAP, S, V_NONE, {4, 0.008f, 1500, 1.1f, 0.0035f, 0.16f, 0.55f, 0.1f, 0.5f, 0.3f}},
  {"Clap Room",    G_CLAP, S, V_NONE, {4, 0.010f, 1100, 1.2f, 0.004f, 0.2f, 0.5f, 0.4f, 0.9f, 0.4f}},
  {"Clap Tight",   G_CLAP, M, V_NONE, {3, 0.006f, 1600, 1.4f, 0.003f, 0.08f, 0.5f, 0, 0, 0}},
  {"Clap Big",     G_CLAP, S, V_NONE, {6, 0.013f, 900, 1.0f, 0.005f, 0.6f, 0.5f, 0.35f, 1.6f, 0.6f}},
  {"Clap Lo-Fi",   G_CLAP, M, V_8BIT, {4, 0.011f, 1000, 1.3f, 0.0035f, 0.3f, 0.45f, 0, 0, 0}},
  {"Snap",         G_CLAP, M, V_NONE, {1, 0.0f, 2600, 2.0f, 0.006f, 0.05f, 0.3f, 0, 0, 0}},
  {"Clap Hall",    G_CLAP, S, V_NONE, {4, 0.011f, 1150, 1.2f, 0.004f, 0.25f, 0.5f, 0.55f, 2.4f, 0.4f}},
  {"Clap Dark",    G_CLAP, M, V_NONE, {4, 0.012f, 700, 1.1f, 0.0045f, 0.3f, 0.5f, 0, 0, 0}},
  {"Clap Bright",  G_CLAP, M, V_NONE, {4, 0.009f, 2200, 1.3f, 0.003f, 0.2f, 0.5f, 0, 0, 0}},
  {"Clap Wide",    G_CLAP, S, V_NONE, {5, 0.010f, 1200, 1.2f, 0.004f, 0.3f, 0.5f, 0.2f, 0.7f, 1.0f}},
  {"Clap Group",   G_CLAP, S, V_NONE, {9, 0.008f, 1250, 0.9f, 0.004f, 0.4f, 0.55f, 0.25f, 1.0f, 0.8f}},
  {"Clap Plate",   G_CLAP, S, V_NONE, {4, 0.010f, 1300, 1.2f, 0.004f, 0.15f, 0.4f, 0.6f, 0.5f, 0.3f}},
  {"Clap Snap",    G_CLAP, M, V_NONE, {2, 0.004f, 2000, 1.6f, 0.004f, 0.12f, 0.5f, 0, 0, 0}},
};

// ----------------------------------------------------------------------------------------------------- MISC (64)
// G_COW: f1, f2, bp, Q, fast share, dec, duty, hpf
// G_DRUM: Hz, pitch start ratio, pitch time, dec, click, noise, noise dec, noise bp (0 = hp), drive, 2nd tone ratio
// G_PLUCK: Hz, dec, brightness, pick, stereo detune cents, body Hz
// G_CHORD: root, type, voices/note, detune c, cut start, cut end, filter time, Q, amp dec, wave
// G_VOX: Hz, vowel, dur, glide start, glide t, vibrato c, breath, 2nd vowel, formant shift, release
// G_ZAP: f0, f1, sweep t, dec, wave, wobble, repeats, drive, period
// G_SWEEP: f0, f1, dur, Q, shape, tone, hpf, pink
// G_GLITCH: mode, rate, dur, dec, base Hz, bits, density
const Recipe kMisc[] = {
  {"808 Cowbell",  G_COW, M, V_NONE, {540, 800, 1000, 1.2f, 0.75f, 0.35f, 0.5f, 300}},
  {"Cowbell Long", G_COW, M, V_NONE, {587, 845, 1100, 1.5f, 0.5f, 0.7f, 0.5f, 350}},
  {"Cowbell Hi",   G_COW, M, V_NONE, {800, 1180, 1600, 1.4f, 0.7f, 0.3f, 0.4f, 500}},
  {"Claves",       G_MODAL, M, V_NONE, {2500, 2.75f, 0, 0, 0.15f, 0.08f, 0, 0.15f, 600, 0}},
  {"Conga Hi",     G_DRUM, M, V_NONE, {340, 1.25f, 0.008f, 0.28f, 0.25f, 0.05f, 0.02f, 2000, 0, 1.5f}},
  {"Conga Mid",    G_DRUM, M, V_NONE, {255, 1.25f, 0.008f, 0.32f, 0.25f, 0.05f, 0.02f, 1800, 0, 1.5f}},
  {"Conga Lo",     G_DRUM, M, V_NONE, {180, 1.3f, 0.01f, 0.4f, 0.2f, 0.05f, 0.02f, 1500, 0, 1.5f}},
  {"Bongo Hi",     G_DRUM, M, V_NONE, {480, 1.2f, 0.006f, 0.15f, 0.3f, 0.05f, 0.015f, 3000, 0, 1.6f}},
  {"Bongo Lo",     G_DRUM, M, V_NONE, {360, 1.2f, 0.006f, 0.18f, 0.3f, 0.05f, 0.015f, 2500, 0, 1.6f}},
  {"Timbale",      G_MODAL, M, V_NONE, {520, 1.58f, 2.14f, 2.65f, 0.45f, 0.6f, 0.4f, 0.4f, 200, 0.3f}},
  {"Shaker",       G_SHAKER, M, V_NONE, {0.02f, 0.1f, 3000, 7000, 0.8f, 4000, 0, 0, 0, 0}},
  {"Shaker 2",     G_SHAKER, M, V_NONE, {0.015f, 0.08f, 3500, 6500, 0.8f, 4000, 0, 0, 0.09f, 0}},
  {"Cabasa",       G_NOISE, M, V_NONE, {2, 6000, 0.9f, 0.01f, 0.12f, 5000, 0, 0, 0, 0}},
  {"Tambourine",   G_SHAKER, M, V_NONE, {0.004f, 0.15f, 2500, 8000, 0.8f, 5000, 1.0f, 5200, 0, 0.25f}},
  {"Tamb Shake",   G_SHAKER, M, V_NONE, {0.03f, 0.2f, 2500, 8000, 0.8f, 6000, 0.8f, 5600, 0.12f, 0.2f}},
  {"Maracas",      G_SHAKER, M, V_NONE, {0.005f, 0.06f, 3500, 5000, 1.0f, 5000, 0, 0, 0, 0}},
  {"Ride",         G_CYM, S, V_NONE, {48, 300, 12000, 2.2f, 0.05f, 300, 0.3f, 0, 0, 0.4f}},
  {"Ride Bell",    G_CYM, S, V_NONE, {20, 450, 9000, 1.8f, 0.03f, 400, 1.0f, 0, 0, 0.5f}},
  {"Ride Dark",    G_CYM, S, V_NONE, {40, 200, 8000, 1.7f, 0.05f, 200, 0.2f, 0, 0, 0.5f}},
  {"Crash",        G_CYM, S, V_NONE, {56, 400, 15000, 2.5f, 0.4f, 400, 0, 0.002f, 0, 0.6f}},
  {"Crash 2",      G_CYM, S, V_NONE, {56, 600, 17000, 3.0f, 0.5f, 600, 0, 0.003f, 0, 0.7f}},
  {"Crash Short",  G_CYM, S, V_NONE, {48, 500, 15000, 1.2f, 0.4f, 500, 0, 0.001f, 0, 0.6f}},
  {"China",        G_CYM, S, V_NONE, {48, 500, 13000, 1.8f, 0.5f, 500, 0, 0.001f, 0.6f, 0.6f}},
  {"Splash",       G_CYM, S, V_NONE, {40, 800, 16000, 0.8f, 0.4f, 800, 0, 0.001f, 0, 0.7f}},
  {"808 Cymbal",   G_HAT, M, V_NONE, {1.0f, 1.8f, 3440, 7100, 0.8f, 2000, 0, 0.0f, 0.3f, 0.0f}},
  {"Kick 808",     G_DRUM, M, V_NONE, {50, 3.0f, 0.012f, 1.2f, 0.1f, 0, 0, 0, 0.3f, 0}},
  {"Kick 909",     G_DRUM, M, V_NONE, {55, 4.5f, 0.008f, 0.5f, 0.6f, 0.1f, 0.02f, 0, 1.2f, 0}},
  {"Kick Punch",   G_DRUM, M, V_NONE, {60, 3.5f, 0.01f, 0.35f, 0.4f, 0, 0, 0, 2.0f, 0}},
  {"Kick Sub",     G_DRUM, M, V_NONE, {42, 2.0f, 0.03f, 1.8f, 0.05f, 0, 0, 0, 0, 0}},
  {"Kick Hard",    G_DRUM, M, V_NONE, {65, 6.0f, 0.006f, 0.4f, 0.8f, 0.05f, 0.01f, 0, 3.0f, 0}},
  {"Snare 808",    G_DRUM, M, V_NONE, {238, 1.15f, 0.01f, 0.18f, 0.1f, 0.7f, 0.2f, 0, 0, 2.0f}},
  {"Snare 909",    G_DRUM, M, V_NONE, {190, 1.6f, 0.01f, 0.15f, 0.2f, 0.7f, 0.25f, 4500, 0.5f, 1.47f}},
  {"Snare Tight",  G_DRUM, M, V_NONE, {260, 1.3f, 0.006f, 0.1f, 0.3f, 0.8f, 0.12f, 6000, 0.3f, 1.6f}},
  {"Snare Fat",    G_DRUM, M, V_NONE, {170, 1.5f, 0.012f, 0.22f, 0.1f, 0.6f, 0.35f, 2800, 1.5f, 1.5f}},
  {"Tom Lo",       G_DRUM, M, V_NONE, {90, 1.6f, 0.04f, 0.6f, 0.15f, 0.02f, 0.03f, 0, 0.2f, 0}},
  {"Tom Mid",      G_DRUM, M, V_NONE, {130, 1.6f, 0.035f, 0.5f, 0.15f, 0.02f, 0.03f, 0, 0.2f, 0}},
  {"Tom Hi",       G_DRUM, M, V_NONE, {190, 1.6f, 0.03f, 0.42f, 0.15f, 0.02f, 0.03f, 0, 0.2f, 0}},
  {"Syn Tom",      G_DRUM, M, V_NONE, {120, 3.0f, 0.15f, 0.7f, 0.05f, 0.05f, 0.3f, 3000, 0.3f, 0}},
  {"Pluck A",      G_PLUCK, M, V_NONE, {110.0f, 1.2f, 0.6f, 0.7f, 0, 0}},
  {"Pluck C",      G_PLUCK, S, V_NONE, {261.63f, 1.0f, 0.5f, 0.6f, 6, 0}},
  {"Pluck E Hi",   G_PLUCK, M, V_NONE, {659.26f, 0.7f, 0.7f, 0.9f, 0, 0}},
  {"Muted Pluck",  G_PLUCK, M, V_NONE, {146.83f, 0.25f, 0.2f, 0.4f, 0, 0}},
  {"Stab Maj",     G_CHORD, S, V_NONE, {60, 0, 3, 12, 6000, 900, 0.15f, 1.2f, 0.8f, 0}},
  {"Stab Min",     G_CHORD, S, V_NONE, {57, 1, 3, 12, 6000, 900, 0.15f, 1.2f, 0.8f, 0}},
  {"Stab Min7",    G_CHORD, S, V_NONE, {60, 2, 3, 14, 5000, 700, 0.12f, 1.6f, 0.7f, 0}},
  {"Stab Maj7",    G_CHORD, S, V_NONE, {62, 3, 3, 10, 7000, 1200, 0.2f, 1.0f, 0.9f, 0}},
  {"Stab Sus4",    G_CHORD, S, V_NONE, {58, 4, 3, 12, 5500, 800, 0.15f, 1.4f, 0.7f, 0}},
  {"Organ Stab",   G_CHORD, M, V_NONE, {60, 0, 2, 2, 3500, 3500, 0.1f, 0.7f, 0.5f, 1}},
  {"House Chord",  G_CHORD, S, V_NONE, {53, 7, 2, 8, 2500, 1200, 0.3f, 0.9f, 1.0f, 0}},
  {"Power Stab",   G_CHORD, S, V_NONE, {45, 5, 3, 16, 4000, 600, 0.1f, 2.0f, 0.6f, 2}},
  {"Bass Hit",     G_CHORD, M, V_NONE, {33, 8, 2, 8, 2000, 200, 0.08f, 2.0f, 0.6f, 2}},
  {"Bass Saw",     G_CHORD, M, V_NONE, {36, 9, 3, 15, 3500, 300, 0.1f, 3.0f, 0.5f, 0}},
  {"Sub Bass",     G_DRUM,  M, V_NONE, {55, 1.0f, 0.01f, 1.2f, 0, 0, 0, 0, 0.2f, 0}},
  {"Acid Blip",    G_CHORD, M, V_NONE, {48, 9, 1, 0, 4000, 300, 0.06f, 8.0f, 0.35f, 0}},
  {"Vox Ah",       G_VOX, M, V_NONE, {160, 0, 0.35f, 1.06f, 0.03f, 20, 0.05f, -1, 1.0f, 0.12f}},
  {"Vox Oh",       G_VOX, M, V_NONE, {140, 1, 0.3f, 1.05f, 0.03f, 15, 0.05f, -1, 1.0f, 0.12f}},
  {"Vox Ee",       G_VOX, M, V_NONE, {220, 3, 0.25f, 1.08f, 0.02f, 25, 0.04f, -1, 1.05f, 0.1f}},
  {"Vox Oh-Ee",    G_VOX, M, V_NONE, {180, 1, 0.4f, 0.94f, 0.05f, 20, 0.05f, 3, 1.0f, 0.12f}},
  {"Zap",          G_ZAP, M, V_NONE, {3000, 80, 0.03f, 0.2f, 0, 0, 1, 0, 0}},
  {"Laser",        G_ZAP, M, V_NONE, {6000, 200, 0.06f, 0.4f, 1, 0.02f, 1, 0, 0}},
  {"Riser",        G_SWEEP, S, V_NONE, {200, 8000, 2.0f, 4.0f, 0, 0.5f, 100, 0}},
  {"Noise Sweep",  G_SWEEP, S, V_NONE, {9000, 300, 1.5f, 2.0f, 1, 0, 150, 1}},
  {"Glitch Data",  G_GLITCH, M, V_NONE, {0, 60, 0.5f, 0.6f, 220, 4, 0.7f}},
  {"Stutter",      G_GLITCH, M, V_NONE, {1, 12, 0.7f, 0, 2500, 0, 0.82f}},
};

// ----------------------------------------------------------------------------------------------------- EXP (64)
const Recipe kExp[] = {
  // CR-78 flavour: soft, slightly saturated, band-limited, a touch of hiss
  {"CR78 Kick",    G_DRUM, M, V_CR78, {70, 1.15f, 0.01f, 0.3f, 0.05f, 0, 0, 0, 0.6f, 0}},
  {"CR78 Snare",   G_DRUM, M, V_CR78, {300, 1.1f, 0.005f, 0.08f, 0.1f, 0.6f, 0.12f, 4000, 0.3f, 1.8f}},
  {"CR78 Hat",     G_HAT,  M, V_CR78, {1.0f, 0.04f, 6000, 9000, 0.4f, 5000, 3, 0.15f, 0.3f, 0.0f}},
  {"CR78 Cymbal",  G_HAT,  M, V_CR78, {0.9f, 0.7f, 5500, 8000, 0.5f, 4000, 3, 0.2f, 0.2f, 0.0f}},
  {"CR78 Rim",     G_MODAL, M, V_CR78, {1000, 2.3f, 0, 0, 0.03f, 0.4f, 0, 0.4f, 300, 0.5f}},
  {"CR78 Guiro",   G_GLITCH, M, V_CR78, {1, 28, 0.32f, 0, 2400, 0, 1.0f}},
  {"CR78 Tamb",    G_SHAKER, M, V_CR78, {0.004f, 0.12f, 2500, 8000, 0.8f, 5000, 0.8f, 4800, 0, 0.2f}},
  {"CR78 Cowbell", G_COW,  M, V_CR78, {800, 1180, 1500, 1.5f, 0.85f, 0.18f, 0.5f, 500}},
  {"CR78 Bongo",   G_DRUM, M, V_CR78, {420, 1.05f, 0.005f, 0.12f, 0.05f, 0, 0, 0, 0.2f, 0}},
  {"CR78 Conga",   G_DRUM, M, V_CR78, {260, 1.05f, 0.005f, 0.2f, 0.05f, 0, 0, 0, 0.2f, 0}},
  {"CR78 Maracas", G_SHAKER, M, V_CR78, {0.003f, 0.05f, 4000, 6000, 1.0f, 5000, 0, 0, 0, 0}},
  // Linn-ish: 8-bit mu-law, ~28 kHz
  {"Linn Kick",    G_DRUM, M, V_LINN, {58, 2.0f, 0.015f, 0.35f, 0.4f, 0.1f, 0.02f, 0, 0.8f, 0}},
  {"Linn Snare",   G_DRUM, M, V_LINN, {200, 1.4f, 0.008f, 0.16f, 0.3f, 1.0f, 0.3f, 3500, 0.8f, 1.6f}},
  {"Linn Snare 2", G_DRUM, M, V_LINN, {230, 1.3f, 0.006f, 0.12f, 0.4f, 1.1f, 0.2f, 5000, 1.2f, 1.5f}},
  {"Linn Hat",     G_HAT,  M, V_LINN, {1.0f, 0.06f, 8500, 11000, 0.6f, 6500, 2, 0.5f, 0.5f, 0.2f}},
  {"Linn OH",      G_HAT,  M, V_LINN, {1.0f, 0.5f, 8500, 11000, 0.6f, 6500, 2, 0.5f, 0.3f, 0.2f}},
  {"Linn Clap",    G_CLAP, M, V_LINN, {4, 0.010f, 1300, 1.2f, 0.004f, 0.25f, 0.5f, 0, 0, 0}},
  {"Linn Rim",     G_MODAL, M, V_LINN, {600, 2.9f, 0, 0, 0.04f, 0.7f, 0, 0.7f, 300, 1.0f}},
  {"Linn Tom",     G_DRUM, M, V_LINN, {110, 1.5f, 0.03f, 0.5f, 0.2f, 0.03f, 0.03f, 0, 0.4f, 0}},
  {"Linn Cabasa",  G_NOISE, M, V_LINN, {2, 7000, 0.9f, 0.008f, 0.1f, 5000, 0, 0, 0, 0}},
  {"Linn Tamb",    G_SHAKER, M, V_LINN, {0.003f, 0.18f, 2500, 8500, 0.8f, 5000, 1.0f, 5400, 0, 0.3f}},
  {"Linn Cowbell", G_COW,  M, V_LINN, {562, 845, 1200, 1.3f, 0.7f, 0.3f, 0.45f, 350}},
  {"Linn Conga",   G_DRUM, M, V_LINN, {300, 1.2f, 0.006f, 0.3f, 0.3f, 0.04f, 0.02f, 2200, 0.2f, 1.5f}},
  // SP-style 12-bit, 26 kHz, no anti-alias filter
  {"SP Kick",      G_DRUM, M, V_SP12, {52, 4.0f, 0.02f, 0.7f, 0.3f, 0.05f, 0.03f, 0, 1.5f, 0}},
  {"SP Snare",     G_DRUM, M, V_SP12, {180, 1.5f, 0.01f, 0.2f, 0.3f, 1.0f, 0.3f, 0, 1.0f, 1.5f}},
  {"SP Hat",       G_HAT,  M, V_SP12, {1.0f, 0.08f, 7000, 10000, 0.6f, 5000, 2, 0.4f, 0.4f, 0.2f}},
  {"SP Clap",      G_CLAP, M, V_SP12, {4, 0.011f, 1100, 1.2f, 0.004f, 0.3f, 0.5f, 0, 0, 0}},
  {"SP Rim",       G_MODAL, M, V_SP12, {500, 2.2f, 3.7f, 0, 0.04f, 0.6f, 0.4f, 0.8f, 400, 1.0f}},
  {"SP Stab",      G_CHORD, M, V_SP12, {55, 2, 2, 10, 3500, 900, 0.2f, 1.2f, 0.9f, 0}},
  {"SP Bass",      G_CHORD, M, V_SP12, {31, 9, 2, 6, 1500, 250, 0.1f, 1.5f, 0.7f, 2}},
  {"SP Vox",       G_VOX,  M, V_SP12, {130, 0, 0.4f, 1.1f, 0.04f, 10, 0.08f, 1, 1.0f, 0.15f}},
  // 8-bit, 11 kHz
  {"8bit Kick",    G_ZAP,  M, V_8BIT, {400, 40, 0.025f, 0.2f, 1, 0, 1, 0, 0}},
  {"8bit Snare",   G_NOISE, M, V_8BIT, {2, 1500, 0.7f, 0, 0.15f, 300, 0, 0, 0, 0.3f}},
  {"8bit Hat",     G_NOISE, M, V_8BIT, {2, 5000, 0.7f, 0, 0.05f, 3000, 0, 0, 0, 0}},
  {"8bit Tom",     G_ZAP,  M, V_8BIT, {300, 100, 0.05f, 0.3f, 2, 0, 1, 0, 0}},
  {"8bit Blip",    G_ZAP,  M, V_8BIT, {1200, 1200, 0.01f, 0.08f, 1, 0, 2, 0, 0.07f}},
  {"8bit Jump",    G_ZAP,  M, V_8BIT, {300, 1300, 0.08f, 0.25f, 1, 0, 1, 0, 0}},
  {"8bit Noise",   G_GLITCH, M, V_8BIT, {0, 40, 0.35f, 0.4f, 330, 0, 0.9f}},
  // circuit-bent: crushed, aliased, overdriven
  {"Bent Kick",    G_DRUM, M, V_BENT, {60, 5.0f, 0.02f, 0.5f, 0.5f, 0.1f, 0.05f, 0, 2.0f, 0}},
  {"Bent Snare",   G_DRUM, M, V_BENT, {210, 2.0f, 0.02f, 0.2f, 0.4f, 1.0f, 0.25f, 0, 1.5f, 1.7f}},
  {"Bent Hat",     G_FM,   M, V_BENT, {4400, 1.97f, 9.0f, 0.04f, 0.08f, 0.8f, 0, 0, 3000, 0.2f}},
  {"Bent Vox",     G_VOX,  M, V_BENT, {110, 2, 0.5f, 1.5f, 0.15f, 60, 0.1f, 0, 1.3f, 0.15f}},
  {"Bent Bleeps",  G_GLITCH, M, V_BENT, {2, 18, 0.9f, 0.8f, 440, 0, 0.8f}},
  {"Bent Crackle", G_GLITCH, M, V_BENT, {3, 120, 0.8f, 0.9f, 1800, 0, 0.3f}},
  {"Bent Stutter", G_GLITCH, M, V_BENT, {1, 10, 0.8f, 0, 1200, 0, 0.85f}},
  {"Bent Zap",     G_ZAP,  M, V_BENT, {2500, 60, 0.04f, 0.3f, 1, 0.1f, 3, 1.0f, 0.09f}},
  // FM drums
  {"FM Kick",      G_FM,   M, V_NONE, {52, 1.0f, 3.0f, 0.05f, 0.5f, 0, 4.0f, 0.01f, 0, 0}},
  {"FM Snare",     G_FM,   M, V_NONE, {180, 1.41f, 8.0f, 0.06f, 0.2f, 0.3f, 1.5f, 0.01f, 80, 0.4f}},
  {"FM Tom",       G_FM,   M, V_NONE, {110, 1.5f, 2.0f, 0.1f, 0.5f, 0, 1.6f, 0.03f, 40, 0}},
  {"FM Hat",       G_FM,   M, V_NONE, {4000, 1.97f, 10.0f, 0.03f, 0.06f, 0.5f, 0, 0, 6000, 0.05f}},
  {"FM Cowbell",   G_FM,   M, V_NONE, {560, 1.4f, 2.5f, 0.2f, 0.4f, 0, 0, 0, 200, 0}},
  {"FM Bell",      G_FM,   M, V_NONE, {440, 3.5f, 4.0f, 0.6f, 2.0f, 0, 0, 0, 80, 0}},
  {"FM Clang",     G_FM,   M, V_NONE, {230, 2.76f, 8.0f, 0.4f, 1.0f, 0.6f, 0, 0, 100, 0}},
  {"FM Wood",      G_FM,   M, V_NONE, {900, 2.0f, 1.5f, 0.03f, 0.06f, 0, 1.1f, 0.005f, 200, 0}},
  {"FM Bass",      G_FM,   M, V_NONE, {55, 1.0f, 4.0f, 0.2f, 0.8f, 0.2f, 1.3f, 0.02f, 30, 0}},
  {"FM Rim",       G_FM,   M, V_NONE, {1500, 2.3f, 5.0f, 0.015f, 0.03f, 0.3f, 0, 0, 500, 0.3f}},
  // tape: saturated, darker, hissy
  {"Tape Kick",    G_DRUM, M, V_TAPE, {48, 3.0f, 0.015f, 0.8f, 0.2f, 0, 0, 0, 0.8f, 0}},
  {"Tape Snare",   G_DRUM, M, V_TAPE, {195, 1.5f, 0.01f, 0.2f, 0.2f, 0.9f, 0.35f, 0, 0.6f, 1.5f}},
  {"Tape Hat",     G_HAT,  M, V_TAPE, {1.0f, 0.12f, 3440, 7100, 0.6f, 4000, 0, 0.1f, 0.4f, 0.0f}},
  {"Tape Clap",    G_CLAP, S, V_TAPE, {4, 0.011f, 1050, 1.2f, 0.004f, 0.3f, 0.5f, 0.3f, 1.0f, 0.4f}},
  {"Tape Shaker",  G_SHAKER, M, V_TAPE, {0.02f, 0.12f, 2500, 6000, 0.8f, 4000, 0, 0, 0.1f, 0}},
  {"Tape Chord",   G_CHORD, S, V_TAPE, {57, 2, 3, 18, 3000, 800, 0.3f, 1.0f, 1.2f, 0}},
  {"Tape Pluck",   G_PLUCK, M, V_TAPE, {196.0f, 1.0f, 0.5f, 0.6f, 0, 220}},
  {"Tape Ride",    G_CYM,  S, V_TAPE, {40, 300, 11000, 1.6f, 0.05f, 300, 0.4f, 0, 0, 0.4f}},
};

#undef M
#undef S

struct BankDef { const Recipe* r; int n; };
BankDef Def(int bank) {
  BankDef d = {0, 0};
  switch (bank) {
    case BANK_CH: d.r = kCh; d.n = (int)(sizeof(kCh) / sizeof(kCh[0])); break;
    case BANK_OH: d.r = kOh; d.n = (int)(sizeof(kOh) / sizeof(kOh[0])); break;
    case BANK_RS: d.r = kRs; d.n = (int)(sizeof(kRs) / sizeof(kRs[0])); break;
    case BANK_CP: d.r = kCp; d.n = (int)(sizeof(kCp) / sizeof(kCp[0])); break;
    case BANK_MISC: d.r = kMisc; d.n = (int)(sizeof(kMisc) / sizeof(kMisc[0])); break;
    case BANK_EXP: d.r = kExp; d.n = (int)(sizeof(kExp) / sizeof(kExp[0])); break;
    default: break;
  }
  return d;
}

}  // namespace

int FactoryCount(int bank) { return Def(bank).n; }

const char* FactoryName(int bank, int index) {
  BankDef d = Def(bank);
  return (index >= 0 && index < d.n) ? d.r[index].name : "";
}

bool RenderFactory(int bank, int index, Buf& out) {
  BankDef d = Def(bank);
  if (index < 0 || index >= d.n) return false;
  const Recipe& r = d.r[index];
  uint32_t seed = 0x9e3779b9u * (uint32_t)(bank * 131 + index + 1) ^ 0x5bd1e995u;
  Generate(r.gen, r.p, seed, r.stereo != 0, out);
  if (r.vint != V_NONE) {
    Vint v;
    VintPreset(r.vint, v);
    ApplyVint(out, v, seed);
  }
  return true;
}

}  // namespace syn
}  // namespace dlx
