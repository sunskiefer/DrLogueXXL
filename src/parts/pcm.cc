// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sample part (CH, OH, RS, CP, SP1, SP2): a sample player over src/rom/sample_rom.h.
//
// Signal path per part:   voice A + voice B  ->  + click absorber  ->  REDUX  ->  DRIVE  ->  PartFilter  ->  out
// Each voice: 4-point Hermite read of the int16 sample (pitch = TUNE (+ note - 60) via the playback rate),
// START..END window (END < START plays backwards), attack-decay envelope (no hold/sustain, like the hardware),
// velocity, the sample's loudness gain, and a short fade before the END point so a cut sample never clicks.
//
// Retrigger: the playing voice is handed to the second slot and faded out over 2.5 ms while the new one starts.
// If both slots are busy, the quieter one is stolen; its last output value goes into a "click absorber" (a DC
// residual that decays with a ~1 ms time constant), so even a trigger storm faster than the fade stays click-free.
// Choke(): every voice fades over 3 ms.
//
// Parameter mapping (values in the units of params.json):
//   BANK 0..6 (CH OH RS CP MISC USER EXP), SAMPLE 1..128 (clamped to the bank's count; silence if empty/not ready)
//   TUNE -24..+24 semitones (continuous, live)
//   ATTACK 0..100 %: 0 = instant, else 1 ms * 2000^(a/100) (100 % = 2 s), linear rise (read at trigger)
//   DECAY 0..1023: exponential decay stage reaching -60 dB after 10 ms * 200^((d/1022)^0.75) (10 ms .. 2 s);
//     1023 = no decay (play to the end). Live.
//   START/END 0..100 % of the sample (read at trigger)
//   REDUX 0..100 %: bit depth 16 -> 2 bits (continuous), above 70 % also a sample-rate hold 48 kHz -> 6 kHz
//   DRIVE 0..100 %: SoftClip(x * g) / sqrt(g), g = 1 .. 16 (keeps the loudness roughly constant)
//   CUTOFF/RESON: PartFilter (bipolar LP/THRU/HP)
//
// Real-time: no allocation, no locks, no I/O in Trigger/Render. Samples are pinned (SampleRom::Acquire) while a
// voice plays them, so a USER rescan cannot free them underneath.
#include "../part.h"
#include "../rom/sample_rom.h"

#include <stdio.h>

namespace dlx {

namespace {

const float kRetrigFadeSec = 0.0025f;
const float kChokeFadeSec = 0.003f;
const float kEndFadeFrames = 48.0f;     // 1 ms before the END point
const float kStartFadeFrames = 24.0f;   // 0.5 ms when starting inside the sample (START > 0 or reverse)
const float kMaxStep = 32.0f;

inline float Hermite(float xm1, float x0, float x1, float x2, float t) {
  float c = 0.5f * (x1 - xm1);
  float v = x0 - x1;
  float w = c + v;
  float a = w + v + 0.5f * (x2 - x0);
  float bn = w + a;
  return ((a * t - bn) * t + c) * t + x0;
}

inline float AttackSec(float a) { return a <= 0.05f ? 0.0f : 0.001f * powf(2000.0f, a * 0.01f); }

inline float DecaySec(float d) {
  if (d >= 1022.5f) return -1.0f;   // no decay
  float x = Clamp(d, 0.0f, 1022.0f) / 1022.0f;
  return 0.01f * powf(200.0f, powf(x, 0.75f));
}

struct Voice {
  const Sample* s = 0;
  double pos = 0.0;
  double end = 0.0;         // stop position (exclusive in the playing direction)
  float dir = 1.0f;
  float semis = 0.0f;       // note transposition captured at trigger
  float vel = 1.0f;
  // envelope
  int stage = 0;            // 0 idle, 1 attack, 2 decay/play
  float env = 0.0f;
  float att_inc = 1.0f;
  float fade = 1.0f;        // retrigger / choke fade gain
  float fade_dec = 0.0f;    // per-sample decrement (0 = not fading)
  float start_ramp = 1.0f, start_inc = 0.0f;
  float last_l = 0.0f, last_r = 0.0f;
  uint32_t age = 0;

  bool On() const { return s != 0; }
  void Stop() {
    if (s) SampleRom::Get().Release(s);
    s = 0;
    stage = 0;
    last_l = last_r = 0.0f;
  }
  float Level() const { return env * fade * vel; }
};

class SamplePart : public Part {
 public:
  ~SamplePart() { for (int i = 0; i < 2; i++) v_[i].Stop(); }

  void Init(const float* p, int which, uint32_t seed) override {
    p_ = p;
    which_ = which;
    (void)seed;
    for (int i = 0; i < 2; i++) v_[i].Stop();
    res_l_ = res_r_ = 0.0f;
    res_k_ = expf(-1.0f / (0.001f * kSampleRate));
    flt_[0].Reset();
    flt_[1].Reset();
    hold_ph_ = 1.0f;
    hold_l_ = hold_r_ = 0.0f;
    was_stereo_ = false;
  }

  void Trigger(float velocity, int note) override {
    if (!Active()) { flt_[0].Reset(); flt_[1].Reset(); hold_ph_ = 1.0f; }
    // make room: the playing voice fades out; if both slots are busy, steal the quieter one
    Voice* nv = 0;
    for (int i = 0; i < 2; i++) if (!v_[i].On()) { nv = &v_[i]; break; }
    if (!nv) {
      nv = v_[0].Level() <= v_[1].Level() ? &v_[0] : &v_[1];
      Kill(*nv);
    }
    for (int i = 0; i < 2; i++) {
      Voice& o = v_[i];
      if (&o != nv && o.On()) {
        float d = 1.0f / (kRetrigFadeSec * kSampleRate);
        if (o.fade_dec < d) o.fade_dec = d;
      }
    }

    SampleRom& rom = SampleRom::Get();
    int bank = (int)(p_[PCM_BANK] + 0.5f);
    if (bank < 0 || bank >= NUM_BANKS) return;
    int cnt = rom.Count(bank);
    if (cnt <= 0) return;
    int idx = (int)(p_[PCM_SAMPLE] + 0.5f) - 1;
    if (idx < 0) idx = 0;
    if (idx >= cnt) idx = cnt - 1;
    const Sample* s = rom.Acquire(bank, idx);
    if (!s || s->frames < 2) { rom.Release(s); return; }

    Voice& v = *nv;
    v.s = s;
    double last = (double)(s->frames - 1);
    double st = Clamp(p_[PCM_START], 0.0f, 100.0f) * 0.01 * last;
    double en = Clamp(p_[PCM_END], 0.0f, 100.0f) * 0.01 * last;
    if (en >= st) { v.dir = 1.0f; v.pos = st; v.end = en; }
    else { v.dir = -1.0f; v.pos = st; v.end = en; }
    if (fabs(v.end - v.pos) < 1.0) {   // empty window: play a minimal blip instead of nothing? -> silence
      v.Stop();
      return;
    }
    bool inside = st > 0.5 || v.dir < 0.0f;
    v.start_ramp = inside ? 0.0f : 1.0f;
    v.start_inc = inside ? 1.0f / kStartFadeFrames : 0.0f;
    v.semis = note >= 0 ? (float)(note - 60) : 0.0f;
    v.vel = Clamp(velocity, 0.0f, 1.0f) * s->gain;
    float at = AttackSec(p_[PCM_ATTACK]);
    if (at > 0.0f) { v.stage = 1; v.env = 0.0f; v.att_inc = 1.0f / (at * kSampleRate); }
    else { v.stage = 2; v.env = 1.0f; }
    v.fade = 1.0f;
    v.fade_dec = 0.0f;
    v.last_l = v.last_r = 0.0f;
    v.age = ++age_;
  }

  void Choke() override {
    float d = 1.0f / (kChokeFadeSec * kSampleRate);
    for (int i = 0; i < 2; i++) if (v_[i].On() && v_[i].fade_dec < d) v_[i].fade_dec = d;
  }

  bool Active() const override {
    return v_[0].On() || v_[1].On() || res_l_ != 0.0f || res_r_ != 0.0f;
  }

  int Render(float* l, float* r, int n) override {
    if (n > kBlock) n = kBlock;
    bool stereo = (v_[0].On() && v_[0].s->channels == 2) || (v_[1].On() && v_[1].s->channels == 2);
    if (stereo && !was_stereo_) { flt_[1] = flt_[0]; res_r_ = res_l_; hold_r_ = hold_l_; }
    was_stereo_ = stereo;
    float* R = stereo ? r : tmp_;
    for (int i = 0; i < n; i++) { l[i] = 0.0f; R[i] = 0.0f; }

    // per-block parameters
    float tune = Clamp(p_[PCM_TUNE], -24.0f, 24.0f);
    float dsec = DecaySec(p_[PCM_DECAY]);
    float dcoef = dsec > 0.0f ? DecayCoef60(dsec) : 1.0f;
    for (int k = 0; k < 2; k++) if (v_[k].On()) RenderVoice(v_[k], tune, dcoef, l, R, n, stereo);

    // click absorber (both residuals decay in step; in mono only the left one is heard)
    if (res_l_ != 0.0f || res_r_ != 0.0f) {
      for (int i = 0; i < n; i++) {
        l[i] += res_l_;
        R[i] += res_r_;
        res_l_ *= res_k_;
        res_r_ *= res_k_;
      }
      if (fabsf(res_l_) < 3e-4f && fabsf(res_r_) < 3e-4f) res_l_ = res_r_ = 0.0f;   // -70 dB: done
    }

    // REDUX
    float rx = Clamp(p_[PCM_REDUX], 0.0f, 100.0f);
    if (rx > 0.25f) {
      float bits = 16.0f - 14.0f * rx * 0.01f;
      float q = exp2f(bits - 1.0f), iq = 1.0f / q;
      float hold = rx > 70.0f ? exp2f(-(rx - 70.0f) * (1.0f / 30.0f) * 3.0f) : 1.0f;   // 1 .. 1/8 (48k -> 6k)
      int nc = stereo ? 2 : 1;
      for (int i = 0; i < n; i++) {
        hold_ph_ += hold;
        if (hold_ph_ >= 1.0f) {
          hold_ph_ -= 1.0f;
          hold_l_ = floorf(l[i] * q + 0.5f) * iq;
          if (nc == 2) hold_r_ = floorf(R[i] * q + 0.5f) * iq;
        }
        l[i] = hold_l_;
        if (nc == 2) R[i] = hold_r_;
      }
    }
    // DRIVE
    float dr = Clamp(p_[PCM_DRIVE], 0.0f, 100.0f);
    if (dr > 0.25f) {
      float g = 1.0f + 15.0f * dr * 0.01f;
      float mk = 1.0f / sqrtf(g);
      for (int i = 0; i < n; i++) l[i] = SoftClip(l[i] * g) * mk;
      if (stereo) for (int i = 0; i < n; i++) R[i] = SoftClip(R[i] * g) * mk;
    }
    // filter
    float cut = Clamp(p_[PCM_CUTOFF], -100.0f, 100.0f), res = Clamp(p_[PCM_RESON], 0.0f, 100.0f);
    flt_[0].Set(cut, res);
    if (flt_[0].mode != 0) {
      for (int i = 0; i < n; i++) l[i] = flt_[0].Process(l[i]);
      if (stereo) {
        flt_[1].Set(cut, res);
        for (int i = 0; i < n; i++) R[i] = flt_[1].Process(R[i]);
      }
    }
    return stereo ? 2 : 1;
  }

  bool Format(int off, float value, char* buf, int len) override {
    if (off != PCM_SAMPLE || !buf || len <= 0) return false;
    int bank = (int)(p_ ? p_[PCM_BANK] + 0.5f : 0.0f);
    int idx = (int)(value + 0.5f);
    SampleRom& rom = SampleRom::Get();
    int cnt = rom.Count(bank);
    if (cnt <= 0) {
      if (bank == BANK_USER && rom.Ready(BANK_USER)) snprintf(buf, len, "%d (empty)", idx);
      else snprintf(buf, len, "%d ...", idx);
      return true;
    }
    if (idx < 1) idx = 1;
    if (idx > cnt) idx = cnt;
    const Sample* s = rom.Find(bank, idx - 1);
    snprintf(buf, len, "%d %s", idx, s ? s->name : "");
    return true;
  }

 private:
  // stop a voice now, handing its current output to the click absorber
  void Kill(Voice& v) {
    res_l_ += v.last_l;
    res_r_ += v.s && v.s->channels == 2 ? v.last_r : v.last_l;
    v.Stop();
  }

  void RenderVoice(Voice& v, float tune, float dcoef, float* l, float* r, int n, bool stereo) {
    const Sample* s = v.s;
    const int16_t* d = s->data;
    const int last = (int)s->frames - 1;
    const int ch = s->channels;
    float step = SemiToRatio(tune + v.semis);
    if (step > kMaxStep) step = kMaxStep;
    double inc = (double)(step * v.dir);
    const float sc = v.vel * (1.0f / 32768.0f);
    float istep = 1.0f / (step * kEndFadeFrames);
    for (int i = 0; i < n; i++) {
      double rem = (v.end - v.pos) * v.dir;   // frames left in the playing direction
      if (rem <= 0.0) { v.last_l = v.last_r = 0.0f; v.Stop(); return; }
      // envelope
      float e;
      if (v.stage == 1) {
        v.env += v.att_inc;
        if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; }
        e = v.env;
      } else {
        e = v.env;
        v.env *= dcoef;
      }
      float g = e * v.fade * sc;
      float ef = (float)rem * istep;
      if (ef < 1.0f) g *= ef;
      if (v.start_ramp < 1.0f) { g *= v.start_ramp; v.start_ramp += v.start_inc; }
      if (v.fade_dec > 0.0f) {
        v.fade -= v.fade_dec;
        if (v.fade <= 0.0f) { v.last_l = v.last_r = 0.0f; v.Stop(); return; }
      }
      // read
      int i0 = (int)v.pos;
      float t = (float)(v.pos - (double)i0);
      int im1 = i0 > 0 ? i0 - 1 : 0;
      int i1 = i0 < last ? i0 + 1 : last;
      int i2 = i0 + 2 <= last ? i0 + 2 : last;
      if (ch == 1) {
        float y = Hermite(d[im1], d[i0], d[i1], d[i2], t) * g;
        l[i] += y;
        if (stereo) r[i] += y;
        v.last_l = v.last_r = y;
      } else {
        float yl = Hermite(d[im1 * 2], d[i0 * 2], d[i1 * 2], d[i2 * 2], t) * g;
        float yr = Hermite(d[im1 * 2 + 1], d[i0 * 2 + 1], d[i1 * 2 + 1], d[i2 * 2 + 1], t) * g;
        l[i] += yl;
        r[i] += yr;
        v.last_l = yl; v.last_r = yr;
      }
      v.pos += inc;
      if (v.stage == 2 && v.env < 1e-4f) { v.Stop(); return; }   // -80 dB: done (no step: the tail is inaudible)
    }
  }

  const float* p_ = 0;
  int which_ = 0;
  Voice v_[2];
  uint32_t age_ = 0;
  PartFilter flt_[2];
  float res_l_ = 0.0f, res_r_ = 0.0f, res_k_ = 0.95f;
  float hold_ph_ = 1.0f, hold_l_ = 0.0f, hold_r_ = 0.0f;
  bool was_stereo_ = false;
  float tmp_[kBlock];
};

}  // namespace

Part* NewSamplePart() { return new SamplePart(); }

}  // namespace dlx
