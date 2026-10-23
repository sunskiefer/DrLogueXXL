// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- shared building blocks of the four analogue parts (BD, SD, LT/HT): parameter mapping helpers,
// the onset/retrigger declicker, the choke fade, per-block smoothing, a DC blocker, pink noise and the helpers
// that synthesise the one-shot transient/layer tables at Init time.
//
// Real-time rules: everything marked "Init only" may use libm freely; the rest is cheap per-sample code.
#pragma once
#include "../part.h"

namespace dlx {
namespace analog {

// ---- parameter helpers (params.json units -> 0..1 / option index) ----
inline float Knob(float v) { return Clamp(v * (1.0f / 1023.0f), 0.0f, 1.0f); }   // kit knob 0..1023
inline float Pct(float v) { return Clamp(v * 0.01f, 0.0f, 1.0f); }               // screen param 0..100 %
inline int Opt(float v, int n) {
  int i = (int)(v + 0.5f);
  return i < 0 ? 0 : (i >= n ? n - 1 : i);
}
// exponential (log-even) map of x in 0..1 onto lo..hi
inline float ExpMap(float x, float lo, float hi) { return lo * powf(hi / lo, x); }

static const float kSilent = 3.2e-5f;   // -90 dB: below this a voice is inaudible

// ---- Declick ----
// Every Trigger() arms it. On the first rendered sample of the new hit it measures the step between the last
// output and the new sample and adds that difference back as an offset that dies out with a ~0.5 ms time
// constant. The output therefore stays continuous through retriggers (and through a transient that starts on a
// non-zero sample), at the cost of nothing more than a multiply-add per sample.
struct Declick {
  float off = 0.0f, last = 0.0f, coef = 0.959f;   // exp(-1 / (0.5 ms * 48 kHz))
  bool pending = false;
  void Arm() { pending = true; }
  inline float Process(float y) {
    if (pending) { off += last - (y + off); pending = false; }
    y += off;
    off *= coef;
    if (fabsf(off) < 1e-9f) off = 0.0f;   // flush: a decaying offset would otherwise run into denormals (slow)
    last = y;
    return y;
  }
  bool Settled() const { return fabsf(off) < kSilent; }
};

// flush a decaying envelope/state to 0 once it is far below audibility (called per block): exponential
// decays otherwise walk into the denormal range, which costs ~3x CPU on x86 and on ARM VFP with FZ off
inline void Flush(float& x) { if (fabsf(x) < 1e-12f) x = 0.0f; }

// ---- output ceiling: linear up to 0.75, then a soft knee that never exceeds ~0.98 ----
// Keeps hot settings (full ATTACK/transient on a driven body, loud snappy, resonant filter) in a sane range for
// the engine's mix without touching normal hits. Costs one compare on the common path.
inline float Ceiling(float x) {
  const float k = 0.75f, r = 0.23f;
  float a = fabsf(x);
  if (a <= k) return x;
  float y = k + r * SoftClip((a - k) * (1.0f / r));
  return x < 0.0f ? -y : y;
}

// ---- Choke: linear 3 ms fade to silence ----
struct Fade {
  float g = 1.0f, step = 0.0f;
  void Reset() { g = 1.0f; step = 0.0f; }
  void Start() { if (step == 0.0f) step = 1.0f / (0.003f * kSampleRate); }
  bool Fading() const { return step != 0.0f; }
  bool Done() const { return step != 0.0f && g <= 0.0f; }
  inline float Next() {
    if (step != 0.0f) { g -= step; if (g < 0.0f) g = 0.0f; }
    return g;
  }
};

// ---- per-block one-pole smoother (a = 0.3 per 64-sample block ~ 4 ms) ----
struct BlockSmooth {
  float v = 0.0f;
  bool primed = false;
  inline float Step(float target, float a = 0.3f) {
    if (!primed) { v = target; primed = true; return v; }
    v += a * (target - v);
    if (fabsf(target - v) < 1e-4f * (fabsf(target) + 1e-3f)) v = target;
    return v;
  }
};

// ---- DC blocker (first-order high-pass) ----
struct DcBlock {
  float x1 = 0.0f, y1 = 0.0f, r = 0.999f;
  void Set(float hz) { r = 1.0f - kTwoPi * hz * kInvSampleRate; }
  inline float Process(float x) {
    float y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// ---- pink noise (Paul Kellet's economy filter on white noise), roughly unity peak for white in [-1, 1) ----
struct Pink {
  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
  inline float Process(float w) {
    b0 = 0.99765f * b0 + w * 0.0990460f;
    b1 = 0.96300f * b1 + w * 0.2965164f;
    b2 = 0.57000f * b2 + w * 1.0526913f;
    return (b0 + b1 + b2 + w * 0.1848f) * 0.3f;
  }
};

// ---- one-shot tables (transients, tom layers): synthesised once, shared, read-only afterwards ----
template <int N, int MAXLEN>
struct ShotBank {
  float data[N][MAXLEN];
  int len[N];
};

// Init only: helpers for the table generators
inline int Ms(float ms) { return (int)(ms * 0.001f * kSampleRate + 0.5f); }
inline float Rand01(Noise& n) { return n.Uni(); }

// damped sine with an exponential pitch glide f0 -> f1 (glide time constant tau_f) and amplitude time constant
// tau_a, added into d[0..n)
inline void AddGlideSine(float* d, int n, float amp, float f0, float f1, float tau_f, float tau_a) {
  float ph = 0.0f;
  for (int i = 0; i < n; ++i) {
    float t = (float)i * kInvSampleRate;
    float f = f1 + (f0 - f1) * expf(-t / tau_f);
    ph += f * kInvSampleRate;
    ph -= (float)(int)ph;
    d[i] += amp * expf(-t / tau_a) * sinf(kTwoPi * ph);
  }
}
// filtered noise burst through an SVF (mode 0 lp, 1 bp, 2 hp) with attack (linear, sec) and exp decay tau
inline void AddNoise(float* d, int n, Noise& rng, float amp, int mode, float hz, float q, float atk, float tau) {
  Svf f;
  f.Set(hz, q);
  for (int i = 0; i < n; ++i) {
    float t = (float)i * kInvSampleRate;
    float l, b, h;
    f.Process(rng.Next(), &l, &b, &h);
    float y = mode == 0 ? l : (mode == 1 ? b : h);
    float e = (atk > 0.0f && t < atk) ? t / atk : 1.0f;
    d[i] += amp * e * expf(-t / tau) * y;
  }
}
// a struck resonator: an impulse into a bandpass SVF of high Q (wood, rims, sticks)
inline void AddResonator(float* d, int n, float amp, float hz, float q) {
  Svf f;
  f.Set(hz, q);
  for (int i = 0; i < n; ++i) {
    float l, b, h;
    f.Process(i == 0 ? 1.0f : 0.0f, &l, &b, &h);
    d[i] += amp * b;
  }
}
// sum of square-ish inharmonic partials (808-style metal), exp decay tau
inline void AddMetal(float* d, int n, float amp, float base, float tau) {
  static const float kRatios[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};   // TR-808 cymbal
  float ph[6] = {0.0f, 0.13f, 0.37f, 0.61f, 0.29f, 0.83f};
  Svf hp;
  hp.Set(base * 1.5f, 0.8f);
  for (int i = 0; i < n; ++i) {
    float s = 0.0f;
    for (int k = 0; k < 6; ++k) {
      ph[k] += base * kRatios[k] * kInvSampleRate;
      ph[k] -= (float)(int)ph[k];
      s += ph[k] < 0.5f ? 1.0f : -1.0f;
    }
    float l, b, h;
    hp.Process(s * (1.0f / 6.0f), &l, &b, &h);
    d[i] += amp * expf(-(float)i * kInvSampleRate / tau) * h;
  }
}
// Finish a table: remove DC (20 Hz high-pass), raised-cosine fade in/out, normalise to `peak`.
inline void Finish(float* d, int n, int fade_in, int fade_out, float peak) {
  DcBlock dc;
  dc.Set(20.0f);
  for (int i = 0; i < n; ++i) d[i] = dc.Process(d[i]);
  if (fade_in > n) fade_in = n;
  if (fade_out > n) fade_out = n;
  for (int i = 0; i < fade_in; ++i) d[i] *= 0.5f - 0.5f * cosf(kPi * (float)i / (float)fade_in);
  for (int i = 0; i < fade_out; ++i) d[n - 1 - i] *= 0.5f - 0.5f * cosf(kPi * (float)i / (float)fade_out);
  float m = 0.0f;
  for (int i = 0; i < n; ++i) m = fabsf(d[i]) > m ? fabsf(d[i]) : m;
  if (m > 1e-9f) {
    float g = peak / m;
    for (int i = 0; i < n; ++i) d[i] *= g;
  }
}

// ---- pitched oscillator with a per-sub-block exponential frequency glide ----
// Frequencies are computed (exp2f) at 16-sample sub-block boundaries and the phase increment is ramped
// linearly in between, so exponential pitch envelopes cost one exp2f per 16 samples.
static const int kSub = 16;

}  // namespace analog
}  // namespace dlx
