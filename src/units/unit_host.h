// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- hosting drumlogue "user synth" units (logue SDK 2.0, drumlogue platform) in the MULTI part.
//
// A drumlogue unit is written as ONE global instance behind C entry points (unit_init, unit_render...), which
// doesn't fit a plugin that may be loaded several times in one process. So each unit is wrapped as a class that
// owns the unit's own synth object (the class the unit's unit.cc forwards to) and its unit_header_t parameter
// table, and forwards the same calls. The unit sources are vendored under third_party/community/ and
// third_party/logue-sdk/ with the changes listed in src/VENDORED.md.
//
// Rate and block: units run exactly as on the drumlogue, 48 kHz, stereo interleaved float out, called with at most
// kBlock (64) frames. Threading: audio thread only, under the engine lock; NoteOn/SetParam/Render never allocate.
#pragma once
#include <stdint.h>
#include "runtime.h"       // third_party/logue-sdk/platform/drumlogue/common: unit_param_t, unit_runtime_desc_t

namespace dlx {

class SynthUnit {
 public:
  virtual ~SynthUnit() {}
  // desc: samplerate 48000, frames_per_buffer 64, 0 in / 2 out, target/api of the drumlogue synth platform; the
  // sample callbacks report no banks. Returns k_unit_err_none (0) or the unit's error.
  virtual int8_t Init(const unit_runtime_desc_t* desc) = 0;
  virtual const unit_header_t& Header() const = 0;        // name, num_params, params[] (ranges, types, names)
  virtual void SetParam(uint8_t id, int32_t value) = 0;   // value in the unit's own range (params[id].min..max)
  virtual int32_t GetParam(uint8_t id) = 0;
  virtual const char* ParamString(uint8_t id, int32_t value) = 0;   // text for strings-type params, else NULL
  virtual void NoteOn(uint8_t note, uint8_t velocity) = 0;
  virtual void NoteOff(uint8_t note) = 0;
  virtual void GateOn(uint8_t velocity) = 0;
  virtual void GateOff() = 0;
  virtual void AllNoteOff() = 0;
  virtual void SetTempo(uint32_t tempo_16_16) {}          // BPM in 16.16 fixed point, as unit_set_tempo
  virtual void Render(float* out_lr, uint32_t frames) = 0;   // interleaved stereo, frames <= 64
  virtual int NumPresets() const { return 0; }
  virtual const char* PresetName(uint8_t) { return nullptr; }
  virtual void LoadPreset(uint8_t) {}
  virtual void Reset() {}
};

// The registry (src/units/registry.cc). Order and names match src/units/registry.txt, which gen_params.py turns into
// the MULTI part's engine list after VPM and NOISE.
int NumSynthUnits();
const char* SynthUnitName(int index);
SynthUnit* NewSynthUnit(int index);       // allocates (call off the audio thread, or at Init)

// Fill a runtime descriptor the way the drumlogue does for a synth unit.
void FillRuntimeDesc(unit_runtime_desc_t* desc);

// Text for a unit parameter value, formatted from its type/frac like the drumlogue screen ("42%", "-3.0dB",
// "C4", a strings value...). buf gets at most len bytes.
void FormatUnitParam(SynthUnit* u, uint8_t id, int32_t value, char* buf, int len);

}  // namespace dlx
