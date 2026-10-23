// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "Elementish": cldmnky's modal synth inspired by Mutable Instruments Elements (Apache-2.0, cldmnky/
// drumlogue-units; exciter samples from Elements' resources, MIT), vendored in third_party/community/cldmnky/.
// Built in its lightweight configuration (ELEMENTS_LIGHTWEIGHT, as the unit ships), monophonic, scalar (no NEON).
#define ELEMENTS_LIGHTWEIGHT 1
#include "units/unit_wrap.h"

namespace u_elementish {
#include "../../third_party/community/cldmnky/elementish-synth/elements_synth_v2.h"
#include "../../third_party/community/cldmnky/elementish-synth/header.c"

class Unit : public dlx::SynthUnit {
 public:
  int8_t Init(const unit_runtime_desc_t* desc) override {
    int8_t e = dlx::CheckDesc(desc, unit_header);
    if (e) return e;
    return s_.Init(desc);
  }
  const unit_header_t& Header() const override { return unit_header; }
  void SetParam(uint8_t id, int32_t v) override {
    if (id < unit_header.num_params) s_.setParameter(id, dlx::ClampParam(unit_header, id, v));
  }
  int32_t GetParam(uint8_t id) override { return s_.getParameterValue(id); }
  const char* ParamString(uint8_t id, int32_t v) override { return s_.getParameterStrValue(id, v); }
  void NoteOn(uint8_t note, uint8_t velocity) override { s_.NoteOn(note, velocity); }
  void NoteOff(uint8_t note) override { s_.NoteOff(note); }
  void GateOn(uint8_t velocity) override { s_.GateOn(velocity); }
  void GateOff() override { s_.GateOff(); }
  void AllNoteOff() override { s_.AllNoteOff(); }
  void SetTempo(uint32_t t) override { s_.SetTempo(t); }
  void Render(float* out, uint32_t frames) override { s_.Render(out, frames > 64 ? 64 : frames); }
  int NumPresets() const override { return (int)unit_header.num_presets; }
  const char* PresetName(uint8_t i) override { return ElementsSynth::getPresetName(i); }
  void LoadPreset(uint8_t i) override { s_.LoadPreset(i); }
  void Reset() override { s_.Reset(); }

 private:
  ElementsSynth s_;
};
}  // namespace u_elementish

namespace dlx {
SynthUnit* NewElementishUnit() { return new u_elementish::Unit(); }
}
