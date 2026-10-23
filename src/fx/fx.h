// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the three effect slots of the drumlogue: DELAY and REVERB (send effects) and the MASTER effect
// (insert on the main mix, with a sidechain key fed by each part's SIDECH send).
//
// Each slot holds every type of its list (params.json "dly_type", "rev_type", "mfx_type") and runs the selected one;
// a type change clears the old type's memory so nothing old rings out of the new one. All run at 48 kHz on blocks
// of at most kBlock (64) frames. `v` is the engine's parameter array (src/param_ids.h, values in params.json units);
// each slot reads its own keys (dly_*, rev_*, comp_*/mf_*/boost_*/eq_*/mfx_u*) every block.
// Init() allocates (constructor time); Process/Clear never allocate, lock or do I/O.
#pragma once
#include "../dsp.h"
#include "../param_ids.h"

namespace dlx {

class DelayFx {
 public:
  DelayFx();
  ~DelayFx();
  void Init();
  void Clear();
  // in: the delay send bus (stereo). out: the wet return, already scaled by dly_level. bpm > 0 when the host reports
  // a tempo (the BPM types follow it; without one they assume 120).
  void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v, float bpm);
  bool Format(int param, float value, char* buf, int len);   // optional value text (e.g. time in ms); false = default
 private:
  struct Impl;
  Impl* d_;
};

class ReverbFx {
 public:
  ReverbFx();
  ~ReverbFx();
  void Init();
  void Clear();
  // in: the reverb send bus; out: the wet return, already scaled by rev_level.
  void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v);
  bool Format(int param, float value, char* buf, int len);
 private:
  struct Impl;
  Impl* d_;
};

class MasterFx {
 public:
  MasterFx();
  ~MasterFx();
  void Init();
  void Clear();
  // In place on the main bus. key: the mono sidechain key bus (sum of the parts' SIDECH sends).
  void Process(float* l, float* r, const float* key, int n, const float* v);
  bool Format(int param, float value, char* buf, int len);
  // Name of generic slot i (0..5, the mfx_u1..mfx_u6 params) for the selected type, or NULL when unused.
  const char* SlotName(int i, const float* v) const;
  float GainReductionDb() const;   // compressor meter, 0 when no compressor
 private:
  struct Impl;
  Impl* d_;
};

}  // namespace dlx
