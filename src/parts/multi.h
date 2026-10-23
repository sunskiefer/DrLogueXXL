// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the MULTI part (the drumlogue's 11th part): VPM, NOISE, or one of the hosted synth units.
//
// Its sound block starts at P_MT_ENGINE (Init gets &v[P_MT_ENGINE]; read every key as p[P_<KEY> - P_MT_ENGINE]).
// u_1..u_24 hold the selected unit's parameters normalised 0..1 (u_n = (value - min) / (max - min), rounded to the
// unit's whole steps when applied); their names and value text come from the unit. Every unit is allocated in
// Init(), so switching engines on the audio thread allocates nothing.
//
// Engine switching and slot values: when mt_engine changes, the engine (src/engine.cc) first calls
// EngineChanged() which returns the new unit's default slot values so the u_ params can be reset to them (the unit's
// own init values), then the normal per-block parameter read applies them.
#pragma once
#include "../part.h"

namespace dlx {

class MultiPart : public Part {
 public:
  // Name of slot i (0..23) for the current engine ("" when the engine has fewer params); false = slot unused.
  virtual bool SlotName(int slot, char* buf, int len) = 0;
  // Value text of slot i at normalised value `norm`.
  virtual bool SlotDisplay(int slot, float norm, char* buf, int len) = 0;
  // Called when mt_engine changes (audio thread, under the lock). Fills defaults[24] with the new engine's
  // normalised init values (unused slots 0) and returns the number of used slots.
  virtual int EngineChanged(int engine, float* defaults) = 0;
  virtual void SetTempo(float bpm) = 0;
  // Preset list of the current unit (for a future preset browser); 0 when it has none.
  virtual int NumPresets() = 0;
  virtual const char* PresetName(int i) = 0;
  virtual void LoadPreset(int i, float* slot_values) = 0;   // loads it and reports the unit's new slot values
};

MultiPart* NewMulti();   // src/parts/multi.cc

}  // namespace dlx
