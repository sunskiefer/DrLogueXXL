// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- adapter that turns a Nu:Tekt NTS-1 mkII oscillator unit (logue SDK 2.x osc API: a Processor
// subclass named Osc, pitch from a runtime context, mono out) into a drumlogue-style synth unit (SynthUnit):
//   note on/off  -> the Osc's own noteOn/noteOff + its pitch (setPitch(w0) every block, as the osc runtime does)
//   amp envelope -> added here (ATTACK / RELEASE, the two slots after the unit's own parameters)
//   mono out     -> duplicated to stereo interleaved
// The unit's own parameters keep their ranges, names and types (the NTS-1 and drumlogue param enums agree).
//
// Include this INSIDE the unit's namespace, after the Osc header, with these defined in that namespace:
//   kOscParams[] / kOscNumParams   the unit's parameter descriptors (drumlogue unit_param_t)
//   kUnitName                      display name
//   kAttackInit / kReleaseInit     envelope defaults in ms
// osc_white() (nts_osc_runtime.cc) draws from dlx::t_nts_rng, which every call into the Osc points at this
// instance's own generator state, so instances share no mutable state.
// Not a standalone header.

class NtsOscUnit : public dlx::SynthUnit {
 public:
  NtsOscUnit() {
    rng_ = 0x2545F491u ^ (uint32_t)(uintptr_t)this;
    dlx::NtsOscTablesInit();
    RngScope rs(this);
    osc_ = new (storage_) Osc();          // Osc's constructors may draw noise (Waves' tuning imperfection)
    memset(&header_, 0, sizeof header_);
    header_.header_size = sizeof(unit_header_t);
    header_.target = (uint16_t)k_unit_target_drumlogue_synth;
    header_.api = k_unit_api_2_0_0;
    header_.version = 0x00010000u;
    snprintf(header_.name, sizeof header_.name, "%s", kUnitName);
    header_.num_presets = 0;
    header_.num_params = kOscNumParams + 2;
    for (int i = 0; i < kOscNumParams; ++i) header_.params[i] = kOscParams[i];
    unit_param_t& a = header_.params[kOscNumParams];
    a.min = 0; a.max = 2000; a.init = kAttackInit; a.type = k_unit_param_type_msec;
    snprintf(a.name, sizeof a.name, "ATTACK");
    unit_param_t& r = header_.params[kOscNumParams + 1];
    r.min = 0; r.max = 8000; r.init = kReleaseInit; r.type = k_unit_param_type_msec;
    snprintf(r.name, sizeof r.name, "RELEASE");
    for (int i = 0; i < UNIT_MAX_PARAM_COUNT; ++i) values_[i] = header_.params[i].init;
    memset(zeros_, 0, sizeof zeros_);
  }
  ~NtsOscUnit() override { osc_->~Osc(); }

  int8_t Init(const unit_runtime_desc_t* desc) override {
    if (!desc || desc->samplerate != 48000 || desc->output_channels != 2) return k_unit_err_samplerate;
    RngScope rs(this);
    osc_->init(nullptr);
    for (int i = 0; i < kOscNumParams; ++i) osc_->setParameter((uint8_t)i, values_[i]);
    SetEnvTimes();
    return k_unit_err_none;
  }
  const unit_header_t& Header() const override { return header_; }
  void SetParam(uint8_t id, int32_t v) override {
    if (id >= header_.num_params) return;
    const unit_param_t& p = header_.params[id];
    v = v < p.min ? p.min : (v > p.max ? p.max : v);
    values_[id] = v;
    if (id < kOscNumParams) { RngScope rs(this); osc_->setParameter(id, v); }
    else SetEnvTimes();
  }
  int32_t GetParam(uint8_t id) override { return id < header_.num_params ? values_[id] : 0; }
  const char* ParamString(uint8_t id, int32_t v) override {
    return id < kOscNumParams ? osc_->getParameterStrValue(id, v) : nullptr;
  }
  void NoteOn(uint8_t note, uint8_t velocity) override {
    note_ = note;
    vel_ = velocity / 127.0f;
    gate_ = true;
    stage_ = 1;
    if (att_inc_ >= 1.0f) env_ = 1.0f;
    RngScope rs(this);
    osc_->noteOn(note, velocity);
  }
  void NoteOff(uint8_t note) override {
    if (note != note_ && note != 0xFF) return;   // mono, last-note priority
    gate_ = false;
    RngScope rs(this);
    osc_->noteOff(note_);
  }
  void GateOn(uint8_t velocity) override { NoteOn(note_, velocity); }
  void GateOff() override { NoteOff(note_); }
  void AllNoteOff() override {
    gate_ = false;
    RngScope rs(this);
    osc_->allNoteOff();
  }
  void SetTempo(uint32_t t) override { osc_->setTempo((t >> 16) + (t & 0xFFFF) / 65536.0f); }
  void Reset() override {
    gate_ = false; stage_ = 0; env_ = 0.0f;
    RngScope rs(this);
    osc_->reset();
  }
  void Render(float* out, uint32_t frames) override {
    if (frames > 64) frames = 64;
    RngScope rs(this);
    // pitch, as the osc runtime gives it: w0 = f / samplerate
    float hz = 440.0f * exp2f(((float)note_ - 69.0f) * (1.0f / 12.0f));
    osc_->setPitch(hz * (1.0f / 48000.0f));
    osc_->setShapeLfo(0.0f);
    osc_->process(zeros_, mono_, frames);
    for (uint32_t i = 0; i < frames; ++i) {
      // envelope: linear attack, hold while gated, exponential release (60 dB over RELEASE)
      if (stage_ == 1) {
        env_ += att_inc_;
        if (env_ >= 1.0f) { env_ = 1.0f; stage_ = 2; }
      }
      if (!gate_ && stage_ != 0) {
        env_ *= rel_mul_;
        if (env_ < 1e-5f) { env_ = 0.0f; stage_ = 0; }
      }
      float y = mono_[i] * env_ * vel_;
      out[2 * i] = y;
      out[2 * i + 1] = y;
    }
  }

 private:
  struct RngScope {   // points the unit's osc_white() at this instance's generator for the duration of a call
    uint32_t* prev;
    explicit RngScope(NtsOscUnit* u) : prev(dlx::t_nts_rng) { dlx::t_nts_rng = &u->rng_; }
    ~RngScope() { dlx::t_nts_rng = prev; }
  };
  void SetEnvTimes() {
    float a_ms = (float)values_[kOscNumParams] + 1.0f;          // +1 ms: never a click at ATTACK 0
    float r_ms = (float)values_[kOscNumParams + 1] + 5.0f;
    att_inc_ = 1.0f / (a_ms * 48.0f);
    rel_mul_ = expf(-6.9077553f / (r_ms * 48.0f));
  }

  alignas(16) unsigned char storage_[sizeof(Osc)];
  Osc* osc_;
  unit_header_t header_;
  int32_t values_[UNIT_MAX_PARAM_COUNT];
  uint32_t rng_;
  float zeros_[2 * 64];
  float mono_[64];
  uint8_t note_ = 60;
  bool gate_ = false;
  int stage_ = 0;   // 0 idle, 1 attack, 2 sustain/release
  float env_ = 0.0f, vel_ = 1.0f, att_inc_ = 1.0f, rel_mul_ = 0.999f;
};

