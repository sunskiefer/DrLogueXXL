// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "Maxisynth": boochow's maxisynth (MIT), a one-oscillator subtractive synth built on the Maximilian
// library (MIT, boochow's patched fork), vendored in third_party/community/boochow/.
#include "units/unit_wrap.h"
// Maximilian is compiled as its own sources at global scope (maximilian.cpp, PolyBLEP.cpp); include its headers
// here, globally, so the nested includes inside the unit's namespace below are no-ops.
#include "../../third_party/community/boochow/Maximilian/src/maximilian.h"
#include "../../third_party/community/boochow/Maximilian/src/libs/maxiPolyBLEP.h"

namespace u_maxisynth {
#include "../../third_party/community/boochow/maxisynth/synth.h"

// the unit's header.c, as a C++ aggregate (header.c uses C99 designated initialisers)
static const unit_header_t kHeader = {
    sizeof(unit_header_t), (uint16_t)(k_unit_target_drumlogue | k_unit_module_synth), k_unit_api_2_0_0,
    0x0U, 0x0U, 0x00010000U, "maxisynth", 0, 12,
    {
        {0, 127, 60, 60, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        {0, 2, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Wave"}},
        {0, 1270, 0, 1270, k_unit_param_type_none, 1, 1, 0, {"Cutoff"}},
        {-128, 128, 0, -16, k_unit_param_type_none, 5, 0, 0, {"Reso"}},
        {0, 1000, 0, 0, k_unit_param_type_msec, 0, 0, 0, {"Attack"}},
        {0, 1000, 0, 0, k_unit_param_type_msec, 0, 0, 0, {"Decay"}},
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Sustain"}},
        {0, 1000, 0, 0, k_unit_param_type_msec, 0, 0, 0, {"Release"}},
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"EG >AMP"}},
        {-100, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {">Pitch"}},
        {-100, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {">Cutoff"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {">PWM"}},
    }};
static const char* const kWaveNames[3] = {"Saw", "Square", "Tri"};

class Unit : public dlx::SynthUnit {
 public:
  int8_t Init(const unit_runtime_desc_t* desc) override {
    int8_t e = dlx::CheckDesc(desc, kHeader);
    if (e) return e;
    e = s_.Init(desc);
    if (e) return e;
    s_.Reset();
    for (uint8_t i = 0; i < kHeader.num_params; ++i) s_.setParameter(i, kHeader.params[i].init);
    return k_unit_err_none;
  }
  const unit_header_t& Header() const override { return kHeader; }
  void SetParam(uint8_t id, int32_t v) override {
    if (id < kHeader.num_params) s_.setParameter(id, dlx::ClampParam(kHeader, id, v));
  }
  int32_t GetParam(uint8_t id) override { return id < kHeader.num_params ? s_.getParameterValue(id) : 0; }
  // "Wave" is a typeless 0..2 on the unit; DrLogueXXL names the three shapes (display only)
  const char* ParamString(uint8_t id, int32_t v) override { return id == 1 && v >= 0 && v <= 2 ? kWaveNames[v] : nullptr; }
  void NoteOn(uint8_t note, uint8_t velocity) override { held_++; s_.NoteOn(note, velocity); }
  void NoteOff(uint8_t note) override { if (held_ > 0) held_--; s_.NoteOff(note); }
  void GateOn(uint8_t velocity) override { held_++; s_.GateOn(velocity); }
  void GateOff() override { if (held_ > 0) held_--; s_.GateOff(); }
  // the unit's AllNoteOff() is empty and its gate is a counter: release every gate it still holds
  void AllNoteOff() override { for (int i = 0; i < 64; ++i) s_.GateOff(); held_ = 0; }
  void Render(float* out, uint32_t frames) override { s_.Render(out, frames > 64 ? 64 : frames); }
  void Reset() override { AllNoteOff(); }

 private:
  Synth s_;
  int held_ = 0;
};
}  // namespace u_maxisynth

namespace dlx {
SynthUnit* NewMaxisynthUnit() { return new u_maxisynth::Unit(); }
}
