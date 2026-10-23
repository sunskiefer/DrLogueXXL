// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "Drupiter": cldmnky's Jupiter-8-style poly synth (Apache-2.0 per cldmnky/drumlogue-units; DSP
// based on Nick Copeland's Bristol, GPL-3.0-or-later), vendored in third_party/community/cldmnky/drupiter-synth.
// Each of its .cc files is compiled by its own src/units/drupiter_*.cc wrapper inside namespace u_drupiter (so its
// `dsp`, `common` and `unit_header`
// can't collide with anything else); 4 voices, scalar (no NEON).
#ifndef DRUPITER_MAX_VOICES
#define DRUPITER_MAX_VOICES 4
#endif
#define UNISON_MAX_DETUNE 50
#define ENABLE_POLYBLEP 1
#include "units/unit_wrap.h"

namespace u_drupiter {
#include "../../third_party/community/cldmnky/drupiter-synth/header.c"
#include "../../third_party/community/cldmnky/drupiter-synth/drupiter_synth.h"

class Unit : public dlx::SynthUnit {
 public:
  int8_t Init(const unit_runtime_desc_t* desc) override {
    int8_t e = dlx::CheckDesc(desc, unit_header);
    if (e) return e;
    return s_.Init(desc);
  }
  const unit_header_t& Header() const override { return unit_header; }
  void SetParam(uint8_t id, int32_t v) override {
    if (id < unit_header.num_params) s_.SetParameter(id, dlx::ClampParam(unit_header, id, v));
  }
  int32_t GetParam(uint8_t id) override { return s_.GetParameter(id); }
  const char* ParamString(uint8_t id, int32_t v) override { return s_.GetParameterStr(id, v); }
  void NoteOn(uint8_t note, uint8_t velocity) override { s_.NoteOn(note, velocity); }
  void NoteOff(uint8_t note) override { s_.NoteOff(note); }
  // the unit's own gate handlers (unit.cc): gate on plays C4, gate off releases everything
  void GateOn(uint8_t velocity) override { s_.NoteOn(60, velocity); }
  void GateOff() override { s_.AllNoteOff(); }
  void AllNoteOff() override { s_.AllNoteOff(); }
  void Render(float* out, uint32_t frames) override { s_.Render(out, frames > 64 ? 64 : frames); }
  int NumPresets() const override { return (int)unit_header.num_presets; }
  const char* PresetName(uint8_t i) override { return s_.GetPresetName(i); }
  void LoadPreset(uint8_t i) override { s_.LoadPreset(i); }
  void Reset() override { s_.Reset(); }

 private:
  DrupiterSynth s_;
};
}  // namespace u_drupiter

namespace dlx {
SynthUnit* NewDrupiterUnit() { return new u_drupiter::Unit(); }
}
