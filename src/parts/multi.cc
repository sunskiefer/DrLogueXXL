// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the MULTI part (the drumlogue's 11th part): VPM, NOISE, or one of the hosted synth units.
//
// VPM (variable phase modulation): a sine carrier phase-modulated by a modulator at RATIO times the carrier, depth
//   INDEX (0..100 % -> 0..5 rad), white noise into the carrier phase (NOISE), an AR / ASR / GATE envelope with
//   ATTACK / RELEASE (exponential times 1 ms .. 5 s), IDX MOD (envelope -> index, -100..100 %), and 8 PRESETs that
//   change the modulator's frequency multiplier, shape, feedback and add fixed partials or noise:
//     Default  sine modulator at RATIO
//     Oct.     modulator an octave above RATIO, light feedback
//     Half     modulator an octave below RATIO, half-wave rectified (even partials, hollow)
//     Fourth   modulator a fourth above RATIO (4/3), a touch of feedback: inharmonic, bell-like
//     Air      breathy: extra noise into the phase and a little feedback
//     Creep    slow and detuned: the modulator drifts 0.6 % sharp with a slow 0.4 Hz wobble, the carrier vibrates
//     Throat   formant-ish: a second, fixed ~700 Hz modulator joins the first (vowel-like resonance)
//     Harsh    high modulator feedback (a saw-like modulator) and 1.5x the index
//   NOTE (vpm_note) is the pitch of a pad hit; chromatic notes play their own pitch.
// NOISE: white noise through an LPF / BPF / HPF, 2- or 4-pole (COLOR = cutoff 20 Hz .. 20 kHz exponential,
//   PEAK = resonance), or DECIM (sample-and-hold sample-rate reduction, COLOR = 20 Hz .. 48 kHz rate, PEAK = bit
//   reduction 16 -> 2 bits), with an AR envelope (ATTACK / RELEASE as VPM).
// Units: src/units (registry.cc). Every unit is allocated and initialised in Init(); slot values u_1..u_24 are the
//   unit's parameters normalised 0..1 and are applied (rounded to whole values) only when they change.
#include "parts/multi.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "units/unit_host.h"

namespace dlx {

namespace {

enum MtOff {
  MT_ENGINE = 0, MT_UNOTE = P_MT_UNOTE - P_MT_ENGINE,
  VPM_PRESET = P_VPM_PRESET - P_MT_ENGINE, VPM_NOTE = P_VPM_NOTE - P_MT_ENGINE, VPM_INDEX = P_VPM_INDEX - P_MT_ENGINE,
  VPM_RATIO = P_VPM_RATIO - P_MT_ENGINE, VPM_NOISE = P_VPM_NOISE - P_MT_ENGINE, VPM_EG = P_VPM_EG - P_MT_ENGINE,
  VPM_ATTACK = P_VPM_ATTACK - P_MT_ENGINE, VPM_RELEASE = P_VPM_RELEASE - P_MT_ENGINE,
  VPM_IDXMOD = P_VPM_IDXMOD - P_MT_ENGINE,
  NOISE_ALGO = P_NOISE_ALGO - P_MT_ENGINE, NOISE_COLOR = P_NOISE_COLOR - P_MT_ENGINE,
  NOISE_PEAK = P_NOISE_PEAK - P_MT_ENGINE, NOISE_ATTACK = P_NOISE_ATTACK - P_MT_ENGINE,
  NOISE_RELEASE = P_NOISE_RELEASE - P_MT_ENGINE,
  U_1 = P_U_1 - P_MT_ENGINE
};

const int kMaxUnits = 32;
const float kSilence = 3.16e-5f;            // -90 dBFS
const int kSilentFrames = 9600;             // 200 ms at 48 kHz
const float kChokeStep = 1.0f / (0.003f * kSampleRate);

// ATTACK / RELEASE 0..100 % -> 1 ms .. 5 s, exponential
inline float EgTime(float pct) { return 0.001f * powf(5000.0f, Clamp(pct, 0.0f, 100.0f) * 0.01f); }

void FormatTime(float sec, char* buf, int len) {
  if (sec < 1.0f) snprintf(buf, len, "%.0fms", sec * 1000.0f);
  else snprintf(buf, len, "%.2fs", sec);
}

// Safety stage on unit output: exactly unity up to full scale, then a soft knee towards +-2 (some units can go far
// past full scale at extreme settings, e.g. maxisynth at maximum resonance; the hardware's mixer would clip there).
inline float SafetyLimit(float x) {
  float a = fabsf(x);
  if (a <= 1.0f) return x;
  float over = a - 1.0f;
  float y = 1.0f + over / (1.0f + over);
  return x < 0.0f ? -y : y;
}

// AR / ASR / GATE envelope with a linear attack and an exponential release (-60 dB over RELEASE)
struct Eg {
  enum { AR = 0, ASR = 1, GATE = 2 };
  int mode = AR;
  int stage = 0;   // 0 idle, 1 attack, 2 sustain, 3 release
  bool gate = false;
  float level = 0.0f, att_inc = 1.0f, rel_mul = 0.99f;
  void Set(int m, float attack_pct, float release_pct) {
    mode = m;
    if (mode == GATE) {   // a gate: on and off with just enough ramp not to click
      att_inc = 1.0f / (0.002f * kSampleRate);
      rel_mul = DecayCoef60(0.005f);
    } else {
      att_inc = 1.0f / (EgTime(attack_pct) * kSampleRate);
      rel_mul = DecayCoef60(EgTime(release_pct));
    }
  }
  void Trigger() { stage = 1; gate = true; }       // from the current level: retriggers don't click
  void Release() {
    gate = false;
    if (mode != AR && stage != 0) stage = 3;
  }
  void Kill() { stage = 0; level = 0.0f; gate = false; }
  inline float Process() {
    switch (stage) {
      case 1:
        level += att_inc;
        if (level >= 1.0f) { level = 1.0f; stage = (mode == AR || !gate) ? 3 : 2; }
        break;
      case 2:
        if (!gate) stage = 3;
        break;
      case 3:
        level *= rel_mul;
        if (level < 1e-5f) { level = 0.0f; stage = 0; }
        break;
      default: break;
    }
    return level;
  }
  bool Idle() const { return stage == 0; }
};

// ---------------------------------------------------------------------------------------------------------------
struct VpmPreset {
  float mult;        // modulator frequency multiplier on top of RATIO
  float fb;          // modulator self-feedback (cycles of phase per unit output)
  int shape;         // 0 sine, 1 half-wave rectified sine
  float partial_hz;  // fixed-frequency second modulator (0 = none)
  float partial_amt; // its depth relative to the index
  float noise;       // extra noise into the carrier phase (cycles)
  float drift;       // modulator detune (fraction) + slow wobble
  float idx_scale;   // index multiplier
};
const VpmPreset kVpmPresets[8] = {
    {1.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},           // Default
    {2.0f, 0.12f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},          // Oct.
    {0.5f, 0.0f, 1, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f},           // Half
    {4.0f / 3.0f, 0.05f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},   // Fourth
    {1.0f, 0.08f, 0, 0.0f, 0.0f, 0.06f, 0.0f, 0.9f},         // Air
    {1.0f, 0.06f, 0, 0.0f, 0.0f, 0.0f, 0.006f, 1.0f},        // Creep
    {1.0f, 0.0f, 0, 700.0f, 0.8f, 0.0f, 0.0f, 0.8f},         // Throat
    {1.0f, 0.9f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f},           // Harsh
};
const float kVpmRatios[19] = {0.25f, 0.5f, 0.75f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

struct Vpm {
  Eg eg;
  Noise rng;
  float pc = 0.0f, pm = 0.0f, pp = 0.0f, lfo = 0.0f;   // carrier, modulator, partial, wobble phases (cycles)
  float m1 = 0.0f, m2 = 0.0f;                           // last modulator outputs (feedback, averaged)
  float note = 48.0f, vel = 1.0f;
  void Trigger(int n, float v, const float* p) {
    note = (float)n;
    vel = v;
    if (eg.level < 0.01f) { pc = pm = pp = 0.0f; m1 = m2 = 0.0f; }   // fresh hit: same attack every time
    Setup(p);
    eg.Trigger();
  }
  void Setup(const float* p) {
    eg.Set((int)p[VPM_EG], p[VPM_ATTACK], p[VPM_RELEASE]);
  }
  void Render(const float* p, float* out, int n) {
    Setup(p);
    const VpmPreset& pr = kVpmPresets[(int)Clamp(p[VPM_PRESET], 0.0f, 7.0f)];
    const float ratio = kVpmRatios[(int)Clamp(p[VPM_RATIO], 0.0f, 18.0f)];
    const float f = MidiToHz(note);
    const float base_idx = p[VPM_INDEX] * 0.01f;
    const float idx_mod = p[VPM_IDXMOD] * 0.01f;
    const float noise_amt = p[VPM_NOISE] * 0.01f * 0.25f + pr.noise;
    // modulation index in cycles of carrier phase: 0..5 rad at INDEX 100 %
    const float kIdx = 5.0f / kTwoPi * pr.idx_scale;
    const float lfo_inc = 0.4f * kInvSampleRate;
    float fm_base = f * ratio * pr.mult * (1.0f + pr.drift);
    if (fm_base > 20000.0f) fm_base = 20000.0f;
    const float pp_inc = pr.partial_hz * kInvSampleRate;
    for (int i = 0; i < n; ++i) {
      float e = eg.Process();
      float wob = 0.0f;
      if (pr.drift > 0.0f) {
        lfo += lfo_inc; if (lfo >= 1.0f) lfo -= 1.0f;
        wob = SinPhase(lfo);
      }
      float fc = f * (1.0f + 0.002f * wob);
      float fm = fm_base * (1.0f + pr.drift * wob);
      pc += fc * kInvSampleRate; pc -= (float)(int)pc;
      pm += fm * kInvSampleRate; pm -= (float)(int)pm;
      float m = SinPhase(pm + pr.fb * 0.5f * (m1 + m2) + 1.0f);
      if (pr.shape == 1) m = m > 0.0f ? 2.0f * m - 0.64f : -0.64f;   // half-wave rectified, roughly DC-free
      m2 = m1; m1 = m;
      float idx = Clamp(base_idx + idx_mod * e, 0.0f, 1.5f) * kIdx;
      float mod = m * idx;
      if (pp_inc > 0.0f) {
        pp += pp_inc; pp -= (float)(int)pp;
        mod += SinPhase(pp) * idx * pr.partial_amt;
      }
      if (noise_amt > 0.0f) mod += rng.Next() * noise_amt;
      out[i] = SinPhase(pc + mod + 2.0f) * e * vel * 0.7f;
    }
  }
};

// ---------------------------------------------------------------------------------------------------------------
struct NoiseEngine {
  Eg eg;
  Noise rng;
  Svf f1, f2;
  int algo = -1;
  float cut = 1000.0f;            // smoothed cutoff (Hz)
  float hold = 0.0f, hold_ph = 1.0f;
  float vel = 1.0f;
  void Trigger(float v, const float* p) {
    vel = v;
    eg.Set(Eg::AR, p[NOISE_ATTACK], p[NOISE_RELEASE]);
    eg.Trigger();
  }
  void Render(const float* p, float* out, int n) {
    eg.Set(Eg::AR, p[NOISE_ATTACK], p[NOISE_RELEASE]);
    int a = (int)Clamp(p[NOISE_ALGO], 0.0f, 6.0f);
    if (a != algo) { algo = a; f1.Reset(); f2.Reset(); }
    const float color = Clamp(p[NOISE_COLOR], 0.0f, 100.0f) * 0.01f;
    const float peak = Clamp(p[NOISE_PEAK], 0.0f, 100.0f) * 0.01f;
    if (algo == 6) {   // DECIM: sample and hold + bit reduction
      float rate = 20.0f * powf(2400.0f, color);
      float inc = rate * kInvSampleRate;
      float levels = exp2f(15.0f - peak * 14.0f);
      for (int i = 0; i < n; ++i) {
        float e = eg.Process();
        hold_ph += inc;
        if (hold_ph >= 1.0f) {
          hold_ph -= (float)(int)hold_ph;
          hold = floorf(rng.Next() * levels + 0.5f) / levels;
        }
        out[i] = hold * 0.5f * e * vel;
      }
      return;
    }
    float target = 20.0f * powf(1000.0f, color);
    cut += (target - cut) * 0.5f;   // per block: glides over ~2 blocks, no zipper
    const bool four = (algo & 1) != 0;
    float q2 = 0.707f + peak * peak * 19.0f;
    if (four) {
      f1.Set(cut, 0.54f);
      f2.Set(cut, 1.31f + peak * peak * 15.0f);
    } else {
      f1.Set(cut, q2);
    }
    const int type = algo >> 1;   // 0 LP, 1 BP, 2 HP
    // band-pass is normalised to unity at its peak (x k) then lifted by the bandwidth it loses
    const float bp_gain = (four ? f2.k : f1.k) * (1.5f + 2.0f * sqrtf(1.0f - color));
    for (int i = 0; i < n; ++i) {
      float e = eg.Process();
      float x = rng.Next() * 0.5f;
      float l, b, h, y;
      f1.Process(x, &l, &b, &h);
      y = type == 0 ? l : (type == 1 ? b : h);
      if (four) {
        f2.Process(y, &l, &b, &h);
        y = type == 0 ? l : (type == 1 ? b : h);
      }
      if (type == 1) y *= bp_gain;
      out[i] = SoftClip(y) * e * vel;
    }
  }
};

// ---------------------------------------------------------------------------------------------------------------
class Multi : public MultiPart {
 public:
  ~Multi() override {
    for (int i = 0; i < num_units_; ++i) delete units_[i];
  }

  void Init(const float* p, int which, uint32_t seed) override {
    (void)which;
    p_ = p;
    vpm_.rng.Seed(seed ^ 0xA5A5F00Du);
    noise_.rng.Seed(seed * 2654435761u + 17u);
    FillRuntimeDesc(&desc_);
    num_units_ = NumSynthUnits();
    if (num_units_ > kMaxUnits) num_units_ = kMaxUnits;
    for (int i = 0; i < num_units_; ++i) {
      units_[i] = NewSynthUnit(i);
      ok_[i] = units_[i] && units_[i]->Init(&desc_) == k_unit_err_none;
      if (ok_[i]) {   // the unit's own init values, as the drumlogue sets them after loading a unit
        const unit_header_t& h = units_[i]->Header();
        for (uint32_t k = 0; k < h.num_params && k < (uint32_t)kNumUnitSlots; ++k)
          units_[i]->SetParam((uint8_t)k, h.params[k].init);
      }
    }
    engine_ = (int)p_[MT_ENGINE];
    Invalidate();
  }

  void Trigger(float velocity, int note) override {
    CheckEngine();
    fade_ = 1.0f;
    choking_ = false;
    silent_ = 0;
    active_ = true;
    if (engine_ == 0) {
      vpm_.Trigger(note >= 0 ? note : (int)p_[VPM_NOTE], velocity, p_);
    } else if (engine_ == 1) {
      noise_.Trigger(velocity, p_);
    } else if (SynthUnit* u = Unit()) {
      ApplySlots();
      uint8_t vel = (uint8_t)Clamp(velocity * 127.0f + 0.5f, 1.0f, 127.0f);
      if (note < 0) {
        if (pad_held_) u->NoteOff(pad_note_);
        pad_note_ = (uint8_t)Clamp(p_[MT_UNOTE], 0.0f, 127.0f);
        pad_held_ = true;
        u->NoteOn(pad_note_, vel);
      } else {
        held_[note & 127] = true;
        u->NoteOn((uint8_t)(note & 127), vel);
      }
    }
  }

  void Release(int note) override {
    if (engine_ == 0) vpm_.eg.Release();
    else if (engine_ == 1) noise_.eg.Release();
    else if (SynthUnit* u = Unit()) {
      if (note < 0) {
        if (pad_held_) { u->NoteOff(pad_note_); pad_held_ = false; }
      } else if (held_[note & 127]) {
        held_[note & 127] = false;
        u->NoteOff((uint8_t)(note & 127));
      }
    }
  }

  void Choke() override {
    if (SynthUnit* u = Unit()) u->AllNoteOff();
    ClearHeld();
    vpm_.eg.Release();
    noise_.eg.Release();
    choking_ = true;
  }

  bool Active() const override {
    if (!active_) return false;
    if (engine_ == 0) return !vpm_.eg.Idle();
    if (engine_ == 1) return !noise_.eg.Idle();
    return true;
  }

  int Render(float* l, float* r, int n) override {
    CheckEngine();
    if (n > kBlock) n = kBlock;
    if (engine_ == 0) {
      vpm_.Render(p_, l, n);
      memcpy(r, l, sizeof(float) * n);
    } else if (engine_ == 1) {
      noise_.Render(p_, l, n);
      memcpy(r, l, sizeof(float) * n);
    } else if (SynthUnit* u = Unit()) {
      ApplySlots();
      u->Render(buf_, (uint32_t)n);
      bool bad = false;
      float pk = 0.0f;
      for (int i = 0; i < n; ++i) {
        float a = buf_[2 * i], b = buf_[2 * i + 1];
        if (!(a == a) || !(b == b) || fabsf(a) > 1e6f || fabsf(b) > 1e6f) { bad = true; break; }
        l[i] = SafetyLimit(a); r[i] = SafetyLimit(b);
        float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
        if (m > pk) pk = m;
      }
      if (bad) {   // a unit went unstable: silence it rather than pass NaN/Inf on
        memset(l, 0, sizeof(float) * n); memset(r, 0, sizeof(float) * n);
        u->AllNoteOff(); u->Reset();
        pk = 0.0f;
      }
      // units don't report when they are silent: count silent frames once nothing is held any more
      if (pk > kSilence || Held()) silent_ = 0;
      else if ((silent_ += n) >= kSilentFrames) active_ = false;
    } else {   // a unit that failed to initialise: silent
      memset(l, 0, sizeof(float) * n); memset(r, 0, sizeof(float) * n);
      active_ = false;
    }
    if (choking_) {
      for (int i = 0; i < n; ++i) {
        fade_ -= kChokeStep;
        if (fade_ < 0.0f) fade_ = 0.0f;
        l[i] *= fade_; r[i] *= fade_;
      }
      if (fade_ <= 0.0f) {   // faded out: stop everything for good
        vpm_.eg.Kill();
        noise_.eg.Kill();
        if (SynthUnit* u = Unit()) { u->AllNoteOff(); u->Reset(); }
        choking_ = false;
        active_ = false;
      }
    }
    return 2;
  }

  bool Format(int off, float value, char* buf, int len) override {
    switch (off) {
      case VPM_ATTACK: case VPM_RELEASE: case NOISE_ATTACK: case NOISE_RELEASE:
        FormatTime(EgTime(value), buf, len);
        return true;
      case NOISE_COLOR: {
        if ((int)p_[NOISE_ALGO] == 6) {
          float hz = 20.0f * powf(2400.0f, value * 0.01f);
          if (hz >= 1000.0f) snprintf(buf, len, "%.1fkHz", hz * 0.001f); else snprintf(buf, len, "%.0fHz", hz);
        } else {
          float hz = 20.0f * powf(1000.0f, value * 0.01f);
          if (hz >= 1000.0f) snprintf(buf, len, "%.1fkHz", hz * 0.001f); else snprintf(buf, len, "%.0fHz", hz);
        }
        return true;
      }
      case VPM_IDXMOD:
        snprintf(buf, len, "%+.0f%%", value);
        return true;
      default:
        return false;
    }
  }

  // ---- MultiPart --------------------------------------------------------------------------------------------
  bool SlotName(int slot, char* buf, int len) override {
    if (len > 0) buf[0] = 0;
    SynthUnit* u = Unit();
    if (!u || slot < 0 || slot >= kNumUnitSlots) return false;
    const unit_header_t& h = u->Header();
    if ((uint32_t)slot >= h.num_params || !h.params[slot].name[0]) return false;
    snprintf(buf, len, "%s", h.params[slot].name);
    return true;
  }

  bool SlotDisplay(int slot, float norm, char* buf, int len) override {
    if (len > 0) buf[0] = 0;
    SynthUnit* u = Unit();
    if (!u || slot < 0 || slot >= kNumUnitSlots) return false;
    const unit_header_t& h = u->Header();
    if ((uint32_t)slot >= h.num_params || !h.params[slot].name[0]) return false;
    FormatUnitParam(u, (uint8_t)slot, ToValue(h.params[slot], norm), buf, len);
    return true;
  }

  int EngineChanged(int engine, float* defaults) override {
    for (int i = 0; i < kNumUnitSlots; ++i) defaults[i] = 0.0f;
    SwitchTo(engine);
    SynthUnit* u = Unit();
    if (!u) return 0;
    const unit_header_t& h = u->Header();
    int used = 0;
    for (uint32_t i = 0; i < h.num_params && i < (uint32_t)kNumUnitSlots; ++i) {
      defaults[i] = ToNorm(h.params[i], h.params[i].init);
      if (h.params[i].name[0]) used = (int)i + 1;
    }
    return used;
  }

  void SetTempo(float bpm) override {
    uint32_t t = (uint32_t)(Clamp(bpm, 1.0f, 999.0f) * 65536.0f);
    for (int i = 0; i < num_units_; ++i)
      if (ok_[i]) units_[i]->SetTempo(t);
  }

  int NumPresets() override { SynthUnit* u = Unit(); return u ? u->NumPresets() : 0; }
  const char* PresetName(int i) override {
    SynthUnit* u = Unit();
    return u && i >= 0 && i < u->NumPresets() ? u->PresetName((uint8_t)i) : nullptr;
  }
  void LoadPreset(int i, float* slot_values) override {
    SynthUnit* u = Unit();
    if (!u || i < 0 || i >= u->NumPresets()) return;
    u->LoadPreset((uint8_t)i);
    const unit_header_t& h = u->Header();
    for (int k = 0; k < kNumUnitSlots; ++k) {
      if ((uint32_t)k < h.num_params) {
        int32_t v = u->GetParam((uint8_t)k);
        slot_values[k] = ToNorm(h.params[k], v);
        applied_[k] = ToValue(h.params[k], slot_values[k]);   // already in the unit: don't re-apply
      } else {
        slot_values[k] = 0.0f;
      }
    }
  }

 private:
  static int32_t ToValue(const unit_param_t& prm, float norm) {
    norm = Clamp(norm, 0.0f, 1.0f);
    return prm.min + (int32_t)floorf(norm * (float)(prm.max - prm.min) + 0.5f);
  }
  static float ToNorm(const unit_param_t& prm, int32_t v) {
    if (prm.max <= prm.min) return 0.0f;
    return Clamp((float)(v - prm.min) / (float)(prm.max - prm.min), 0.0f, 1.0f);
  }
  SynthUnit* Unit() const {
    int k = engine_ - kNumBuiltinEngines;
    return k >= 0 && k < num_units_ && ok_[k] ? units_[k] : nullptr;
  }
  void Invalidate() { for (int i = 0; i < kNumUnitSlots; ++i) applied_[i] = INT_MIN; }
  void ClearHeld() { memset(held_, 0, sizeof held_); pad_held_ = false; }
  bool Held() const {
    if (pad_held_) return true;
    for (int i = 0; i < 128; ++i) if (held_[i]) return true;
    return false;
  }
  void SwitchTo(int engine) {
    if (engine == engine_) return;
    if (SynthUnit* u = Unit()) { u->AllNoteOff(); u->Reset(); }
    vpm_.eg.Kill();
    noise_.eg.Kill();
    ClearHeld();
    engine_ = engine;
    active_ = false;
    choking_ = false;
    fade_ = 1.0f;
    Invalidate();
  }
  void CheckEngine() { SwitchTo((int)Clamp(p_[MT_ENGINE], 0.0f, (float)(kNumBuiltinEngines + num_units_ - 1))); }
  void ApplySlots() {
    SynthUnit* u = Unit();
    if (!u) return;
    const unit_header_t& h = u->Header();
    for (uint32_t i = 0; i < h.num_params && i < (uint32_t)kNumUnitSlots; ++i) {
      const unit_param_t& prm = h.params[i];
      if (!prm.name[0] && prm.max == prm.min) continue;   // blank slot (a page spacer)
      int32_t v = ToValue(prm, p_[U_1 + i]);
      if (v != applied_[i]) { u->SetParam((uint8_t)i, v); applied_[i] = v; }
    }
  }

  const float* p_ = nullptr;
  int engine_ = 0;
  Vpm vpm_;
  NoiseEngine noise_;
  unit_runtime_desc_t desc_;
  SynthUnit* units_[kMaxUnits] = {};
  bool ok_[kMaxUnits] = {};
  int num_units_ = 0;
  int32_t applied_[kNumUnitSlots];
  bool held_[128] = {};
  bool pad_held_ = false;
  uint8_t pad_note_ = 60;
  bool active_ = false, choking_ = false;
  float fade_ = 1.0f;
  int silent_ = 0;
  float buf_[2 * kBlock];
};

}  // namespace

MultiPart* NewMulti() { return new Multi(); }

}  // namespace dlx
