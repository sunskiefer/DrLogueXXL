// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the engine: 11 parts, mixer (level, pan, spread, sends, master on/bypass, sidechain), choke groups,
// MIDI note map, the three effect slots, the 48 -> 44.1 kHz resampler, the output limiter and the saved state.
// Host-free: src/vst_glue.cc adapts it to mpc-vst-plugins' mpc_engine() contract, test/ drives it directly.
#pragma once
#include <stdint.h>
#include "param_ids.h"

namespace dlx {

class Engine {
 public:
  explicit Engine(const char* data_dir);
  ~Engine();
  void Midi(const uint8_t* msg, int len);
  void SetParam(const char* key, const char* val);      // the wrapper's string interface
  int GetParam(const char* key, char* buf, int len);    // > 0 on success
  void SetValue(int id, float value);                   // native units, clamped/rounded
  float Value(int id) const;
  void Render(int16_t* out_lr, int frames);             // 44.1 kHz interleaved, any frames <= 128
  void RenderFloat(float* out_l, float* out_r, int frames);   // the same before int16 conversion (tests)
  // keys this engine changed by itself since the last call (mt_engine resets the unit slots): mpc_engine_changed()
  int NextChanged(char* key, int len);
  void NoteOn(int note, int velocity);
  void NoteOff(int note);
  void Panic();
  struct Impl;   // engine.cc only
 private:
  Impl* d_;
  Engine(const Engine&);
  Engine& operator=(const Engine&);
};

}  // namespace dlx
