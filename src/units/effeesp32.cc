// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "EffeESP32": the copych ESP32-S3 FM drum engine (6-operator FM voice, 18 algorithms, ADSR, morph SVF,
// 59-instrument kit; MIT) as ported to the drumlogue by fedemone (engine headers vendored in
// third_party/community/fedemone/EffeESP32, MIT). The unit's own control layer (synth.h/unit.cc/header.c in
// fedemone/logue-sdk) carries no licence of its own, so this controller is written for DrLogueXXL: same 24
// parameters and meanings as the unit's panel, own voice pool (4 voices), no NEON, no limiter (DrLogueXXL's output
// stage has one).
#include "units/unit_wrap.h"

namespace u_effeesp32 {
#include "../../third_party/community/fedemone/EffeESP32/drum_patches.h"

enum {
  P_INSTR = 0, P_PITCH, P_LEVEL, P_PAN, P_ALGO, P_ATTACK, P_HOLD, P_DECAY, P_SUSTAIN, P_RELEASE, P_VELOMOD,
  P_FILTER, P_FLT_FREQ, P_FLT_RESO, P_FLT_MORPH, P_DETUNE, P_OP1, P_OP2, P_OP3, P_OP4, P_OP5, P_OP6, P_NOTE,
  P_FEEDBK, P_COUNT
};
static const int kVoices = 4;
static const float kMasterGain = 0.5f;
static const fmo_waveform_t kCarrierWf[4] = {WF_SINE, WF_TRIANGLE, WF_SQUARE, WF_SAW};
static const char* const kAlgoNames[FMV_NUM_ALGOS] = {
    "2C", "3C", "1M>1C", "2M>2C", "3M>1C", "4M>1C", "3M>1C+", "3M1M>2C", "2M1M>1C", "2M2M>2C", "2M>3C", "3M>3C",
    "2M>4C", "1M>5C", "AM 1C", "AM 2C", "AM2 1C", "AM 4M"};
static const char* const kFilterNames[10] = {"Sqr Off", "Tri Off", "Sin Off", "Saw Off", "Off", "On",
                                             "Sin On", "Tri On", "Sqr On", "Saw On"};

static const unit_header_t kHeader = {
    sizeof(unit_header_t), (uint16_t)(k_unit_target_drumlogue | k_unit_module_synth), k_unit_api_2_0_0,
    0x46654465U, 0x34U, 0x00010000U, "EffeESP32", DRUM_INST_COUNT, P_COUNT,
    {
        {0, DRUM_INST_COUNT - 1, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"Instr"}},
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Pitch"}},
        {0, 200, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Level"}},
        {-100, 100, 0, 0, k_unit_param_type_pan, 0, 0, 0, {"Pan"}},
        {0, FMV_NUM_ALGOS - 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Algo"}},
        {0, 2000, 0, 1, k_unit_param_type_msec, 0, 0, 0, {"Attack"}},
        {0, 2000, 0, 0, k_unit_param_type_msec, 0, 0, 0, {"Hold"}},
        {0, 8000, 0, 200, k_unit_param_type_msec, 0, 0, 0, {"Decay"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Sustain"}},
        {0, 8000, 0, 100, k_unit_param_type_msec, 0, 0, 0, {"Release"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"VeloMod"}},
        {-4, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Filter"}},
        {20, 20000, 0, 16000, k_unit_param_type_hertz, 0, 0, 0, {"FltFrq"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"FltRes"}},
        {0, 100, 0, 33, k_unit_param_type_percent, 0, 0, 0, {"FltMrp"}},
        {-50, 50, 0, 0, k_unit_param_type_hertz, 0, 0, 0, {"Detune"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op1"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op2"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op3"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op4"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op5"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"Op6"}},
        {0, 127, 0, 36, k_unit_param_type_midi_note, 0, 0, 0, {"Note"}},
        {0, 200, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Feedbk"}},
    }};

class Unit : public dlx::SynthUnit {
 public:
  Unit() {
    for (int i = 0; i < P_COUNT; ++i) params_[i] = kHeader.params[i].init;
    LoadInstrument(1);
  }
  int8_t Init(const unit_runtime_desc_t* desc) override {
    int8_t e = dlx::CheckDesc(desc, kHeader);
    if (e) return e;
    for (int i = 0; i < kVoices; ++i) voices_[i].setSampleRate(SAMPLE_RATE);
    LoadInstrument(1);
    return k_unit_err_none;
  }
  const unit_header_t& Header() const override { return kHeader; }
  void SetParam(uint8_t id, int32_t v) override {
    if (id >= P_COUNT) return;
    v = dlx::ClampParam(kHeader, id, v);
    if (id == P_INSTR) { if (v != params_[P_INSTR]) LoadInstrument(v); return; }
    params_[id] = v;
    Apply(id, v);
  }
  int32_t GetParam(uint8_t id) override { return id < P_COUNT ? params_[id] : 0; }
  const char* ParamString(uint8_t id, int32_t v) override {
    if (id == P_INSTR && v >= 0 && v < DRUM_INST_COUNT) return g_drum_inst_names[v];
    if (id == P_ALGO && v >= 0 && v < FMV_NUM_ALGOS) return kAlgoNames[v];
    if (id == P_FILTER && v >= -4 && v <= 5) return kFilterNames[v + 4];
    return nullptr;
  }
  void NoteOn(uint8_t note, uint8_t velocity) override {
    if (velocity == 0) { NoteOff(note); return; }
    int idx = Allocate(note);
    FmVoice6& v = voices_[idx];
    v.applyPatch(working_);
    v.setVolume(working_.volume);
    v.setVeloMod(working_.veloMod);
    v.setPan(working_.pan);
    v.setFilterActive(working_.useFilter != 0);
    // pitch: the instrument's base frequency, moved by Pitch and by the note's distance from C4 (60, the MULTI pad's
    // default unit note), so a pad hit plays the patch as designed and chromatic notes transpose it. (Upstream the
    // note only picks the voice; the unit's Note parameter is kept for GateOn, as on the drumlogue.)
    float semis = (float)params_[P_PITCH] + ((int)note - 60);
    v.setFrequency(working_.baseFreq * exp2f(semis * (1.0f / 12.0f)));
    v.addDetune((float)params_[P_DETUNE]);
    v.addFeedback((params_[P_FEEDBK] * 0.01f - 1.0f) * 3.5f);
    v.noteOn(note, velocity * (1.0f / 127.0f));
  }
  void NoteOff(uint8_t note) override {
    for (int i = 0; i < kVoices; ++i)
      if (voices_[i].isActive() && voices_[i].getNote() == note) voices_[i].noteOff();
  }
  void GateOn(uint8_t velocity) override { NoteOn((uint8_t)params_[P_NOTE], velocity); }
  void GateOff() override { NoteOff((uint8_t)params_[P_NOTE]); }
  void AllNoteOff() override { for (int i = 0; i < kVoices; ++i) voices_[i].noteOff(); }
  void Reset() override { for (int i = 0; i < kVoices; ++i) voices_[i].silence(); }
  void Render(float* out, uint32_t frames) override {
    if (frames > 64) frames = 64;
    memset(out, 0, sizeof(float) * 2 * frames);
    for (int a = 0; a < kVoices; ++a) {
      FmVoice6& v = voices_[a];
      if (!v.isActive()) continue;
      v.processBlock(scratch_, (int)frames);
      const float gl = v.getPanL() * kMasterGain, gr = v.getPanR() * kMasterGain;
      for (uint32_t i = 0; i < frames; ++i) { out[2 * i] += scratch_[i] * gl; out[2 * i + 1] += scratch_[i] * gr; }
    }
  }
  int NumPresets() const override { return DRUM_INST_COUNT; }
  const char* PresetName(uint8_t i) override { return i < DRUM_INST_COUNT ? g_drum_inst_names[i] : nullptr; }
  void LoadPreset(uint8_t i) override { if (i < DRUM_INST_COUNT) LoadInstrument(i); }

 private:
  static int Ci(float x, int lo, int hi) { int v = (int)(x + 0.5f); return v < lo ? lo : (v > hi ? hi : v); }
  void LoadInstrument(int idx) {
    working_ = g_drum_patches[idx];
    base_wf_ = working_.ops[0].waveform;
    params_[P_INSTR] = idx;
    params_[P_LEVEL] = Ci(working_.volume * 100.0f, 0, 200);
    params_[P_PAN] = Ci(working_.pan * 100.0f + 100.0f, 0, 200) - 100;
    params_[P_ALGO] = working_.algo;
    params_[P_ATTACK] = Ci(working_.attack * 1000.0f, 0, 2000);
    params_[P_HOLD] = Ci(working_.hold * 1000.0f, 0, 2000);
    params_[P_DECAY] = Ci(working_.decay * 1000.0f, 0, 8000);
    params_[P_SUSTAIN] = Ci(working_.sustain * 100.0f, 0, 100);
    params_[P_RELEASE] = Ci(working_.release * 1000.0f, 0, 8000);
    params_[P_VELOMOD] = Ci(working_.veloMod * 100.0f, 0, 100);
    params_[P_FILTER] = working_.useFilter ? 1 : 0;
    params_[P_FLT_FREQ] = Ci(working_.filterFreqHz, 20, 20000);
    params_[P_FLT_RESO] = Ci(working_.filterReso * 100.0f, 0, 100);
    params_[P_FLT_MORPH] = Ci(working_.filterMorph * 100.0f, 0, 100);
    for (int i = 0; i < FMV_NUM_OPS; ++i) params_[P_OP1 + i] = Ci(working_.ops[i].volume * 100.0f, 0, 100);
    params_[P_NOTE] = g_drum_inst_notes[idx];
  }
  void Apply(int id, int32_t v) {
    switch (id) {
      case P_LEVEL: working_.volume = v * 0.01f; break;
      case P_PAN: working_.pan = v * 0.01f; break;
      case P_ALGO: working_.algo = (uint8_t)v; break;
      case P_ATTACK: working_.attack = v * 0.001f; break;
      case P_HOLD: working_.hold = v * 0.001f; break;
      case P_DECAY: working_.decay = v * 0.001f; break;
      case P_SUSTAIN: working_.sustain = v * 0.01f; break;
      case P_RELEASE: working_.release = v * 0.001f; break;
      case P_VELOMOD: working_.veloMod = v * 0.01f; break;
      case P_FILTER: {
        working_.useFilter = v >= 1 ? 1 : 0;
        int wf = v <= 0 ? (-v - 1) : (v - 2);
        working_.ops[0].waveform = (wf >= 0 && wf < 4) ? kCarrierWf[wf] : base_wf_;
        break;
      }
      case P_FLT_FREQ: working_.filterFreqHz = (float)v; break;
      case P_FLT_RESO: working_.filterReso = v * 0.01f; break;
      case P_FLT_MORPH: working_.filterMorph = v * 0.01f; break;
      case P_OP1: case P_OP2: case P_OP3: case P_OP4: case P_OP5: case P_OP6:
        working_.ops[id - P_OP1].volume = v * 0.01f;
        break;
      default: break;   // Pitch, Detune, Note, Feedbk are read at note-on
    }
  }
  int Allocate(uint8_t note) {   // a retriggered note fades out (semi-fast) on its voice, the new hit takes another
    for (int i = 0; i < kVoices; ++i)
      if (voices_[i].isActive() && voices_[i].getNote() == note) voices_[i].noteChoke();
    for (int i = 0; i < kVoices; ++i)
      if (!voices_[i].isActive()) return i;
    int best = 0;
    float bs = voices_[0].getStealScore();
    for (int i = 1; i < kVoices; ++i) {
      float s = voices_[i].getStealScore();
      if (s > bs) { bs = s; best = i; }
    }
    return best;
  }

  FmVoice6 voices_[kVoices];
  fm_drum_patch_t working_;
  fmo_waveform_t base_wf_ = WF_SINE;
  int32_t params_[P_COUNT];
  float scratch_[64];
};
}  // namespace u_effeesp32

namespace dlx {
SynthUnit* NewEffeESP32Unit() { return new u_effeesp32::Unit(); }
}
