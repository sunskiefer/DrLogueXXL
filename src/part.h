// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the interface every sound part implements (BD, SD, LT/HT, the six sample parts, MULTI).
//
// Threading: everything here runs on the audio thread under the wrapper's engine lock. No allocation, no locks,
// no I/O in Trigger/Render. Init() may allocate (it runs from Engine construction).
//
// Parameters: the engine keeps one float per parameter, in the units of params.json (src/param_ids.h). A part gets
// a pointer to the first SOUND parameter of its own block (for LT that is &v[P_LT_DECAY]) and reads its values
// through the offsets below, so LT and HT (and the six sample parts) share one class. The mix parameters
// (level, pan, sends, choke...) are the engine's business, not the part's.
#pragma once
#include "dsp.h"
#include "param_ids.h"

namespace dlx {

// offsets inside a part's sound block (relative to its first sound key)
enum BdOff { BD_DECAY = 0, BD_TUNE = P_BD_TUNE - P_BD_DECAY, BD_DRIVE = P_BD_DRIVE - P_BD_DECAY,
             BD_TRANSIENT = P_BD_TRANSIENT - P_BD_DECAY, BD_SWEEP = P_BD_SWEEP - P_BD_DECAY,
             BD_SWTIME = P_BD_SWTIME - P_BD_DECAY, BD_HOLD = P_BD_HOLD - P_BD_DECAY,
             BD_ATTACK = P_BD_ATTACK - P_BD_DECAY, BD_CUTOFF = P_BD_CUTOFF - P_BD_DECAY,
             BD_RESON = P_BD_RESON - P_BD_DECAY };
enum SdOff { SD_DECAY = 0, SD_TUNE = P_SD_TUNE - P_SD_DECAY, SD_SNAPDEC = P_SD_SNAPDEC - P_SD_DECAY,
             SD_SNAPPY = P_SD_SNAPPY - P_SD_DECAY, SD_TONE = P_SD_TONE - P_SD_DECAY,
             SD_SNAPLV = P_SD_SNAPLV - P_SD_DECAY, SD_CUTOFF = P_SD_CUTOFF - P_SD_DECAY,
             SD_RESON = P_SD_RESON - P_SD_DECAY };
enum TomOff { TOM_DECAY = 0, TOM_TUNE = P_LT_TUNE - P_LT_DECAY, TOM_DETUNE = P_LT_DETUNE - P_LT_DECAY,
              TOM_LAYER = P_LT_LAYER - P_LT_DECAY, TOM_SWEEP = P_LT_SWEEP - P_LT_DECAY,
              TOM_ATTACK = P_LT_ATTACK - P_LT_DECAY, TOM_CUTOFF = P_LT_CUTOFF - P_LT_DECAY,
              TOM_RESON = P_LT_RESON - P_LT_DECAY, TOM_DRIVE = P_LT_DRIVE - P_LT_DECAY };
enum PcmOff { PCM_BANK = 0, PCM_SAMPLE = P_CH_SAMPLE - P_CH_BANK, PCM_TUNE = P_CH_TUNE - P_CH_BANK,
              PCM_ATTACK = P_CH_ATTACK - P_CH_BANK, PCM_DECAY = P_CH_DECAY - P_CH_BANK,
              PCM_START = P_CH_START - P_CH_BANK, PCM_END = P_CH_END - P_CH_BANK,
              PCM_CUTOFF = P_CH_CUTOFF - P_CH_BANK, PCM_RESON = P_CH_RESON - P_CH_BANK,
              PCM_REDUX = P_CH_REDUX - P_CH_BANK, PCM_DRIVE = P_CH_DRIVE - P_CH_BANK };
static_assert(P_HT_TUNE - P_HT_DECAY == TOM_TUNE && P_HT_DRIVE - P_HT_DECAY == TOM_DRIVE, "LT/HT blocks differ");
static_assert(P_SP2_DRIVE - P_SP2_BANK == PCM_DRIVE && P_RS_DRIVE - P_RS_BANK == PCM_DRIVE, "sample blocks differ");

class Part {
 public:
  virtual ~Part() {}
  // p: this part's sound block (see above). `which` tells a shared class which part it is (PART_LT or PART_HT...).
  virtual void Init(const float* p, int which, uint32_t seed) = 0;
  // A hit. velocity 0..1 (already through the velocity-sensitivity curve, 1 = full). note: the MIDI note when the
  // part is played chromatically (MULTI and, with tune offset from 60, the sample parts), else -1.
  virtual void Trigger(float velocity, int note) = 0;
  virtual void Release(int note) { (void)note; }      // note-off; drums ignore it
  virtual void Choke() = 0;                           // fast (~3 ms) fade to silence: choke groups, retrigger, panic
  virtual bool Active() const = 0;                    // false once silent: the engine then skips Render()
  // Write (not add) n <= kBlock frames of the dry part signal into l (and r for stereo parts). Returns the number
  // of channels written: 1 = mono in l (r untouched), 2 = stereo.
  virtual int Render(float* l, float* r, int n) = 0;
  // Optional value text for one of this part's sound params (offset as above); return false for the default text.
  virtual bool Format(int off, float value, char* buf, int len) { (void)off; (void)value; (void)buf; (void)len; return false; }
};

// Factories (each in its own .cc so the pieces can be built and tested on their own).
Part* NewBassDrum();            // src/parts/bd.cc
Part* NewSnareDrum();           // src/parts/sd.cc
Part* NewTom();                 // src/parts/tom.cc   (LT and HT)
Part* NewSamplePart();          // src/parts/pcm.cc   (CH, OH, RS, CP, SP1, SP2)
// MULTI (VPM, NOISE and the synth units): src/parts/multi.h, NewMulti()

}  // namespace dlx
