// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- small DSP helpers shared by every part and effect. The whole engine runs at 48 kHz (the drumlogue's
// own rate, which the logue-sdk units also assume) and is resampled to MPC's 44.1 kHz once, at the output.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace dlx {

static const float kSampleRate = 48000.0f;
static const float kInvSampleRate = 1.0f / 48000.0f;
static const int kBlock = 64;            // internal block at 48 kHz (the drumlogue's own buffer size)
static const float kPi = 3.14159265358979f;
static const float kTwoPi = 6.28318530717959f;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float DbToGain(float db) { return powf(10.0f, db * 0.05f); }
inline float GainToDb(float g) { return 20.0f * log10f(g > 1e-9f ? g : 1e-9f); }
inline float SemiToRatio(float s) { return exp2f(s * (1.0f / 12.0f)); }
inline float MidiToHz(float n) { return 440.0f * exp2f((n - 69.0f) * (1.0f / 12.0f)); }

// time constant -> one-pole coefficient: the state reaches ~63 % of a step after `sec`
inline float OnePoleCoef(float sec) {
  return sec <= 0.0f ? 1.0f : 1.0f - expf(-1.0f / (sec * kSampleRate));
}
// per-sample multiplier of an exponential decay that falls by 60 dB in `sec`
inline float DecayCoef60(float sec) {
  return sec <= 0.0f ? 0.0f : expf(-6.9077553f / (sec * kSampleRate));
}

// soft clip, cheap: exact tanh shape near zero, bounded at +-1
inline float SoftClip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// fast sine for a phase in [0, 1)
// sin(2 pi ph) for any ph >= 0 (wrapped), max error ~0.1 %
inline float SinPhase(float ph) {
  ph -= (float)(int)ph;
  float q = 2.0f * ph - 1.0f;                 // sin(2 pi ph) = -sin(pi q), q in [-1, 1)
  float y = 4.0f * q * (1.0f - fabsf(q));     // parabola through sin(pi q)
  y = y * (0.775f + 0.225f * fabsf(y));       // refinement
  return -y;
}

// xorshift32 white noise in [-1, 1)
struct Noise {
  uint32_t s = 0x12345678u;
  void Seed(uint32_t v) { s = v ? v : 0x9e3779b9u; }
  inline uint32_t NextU() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  inline float Next() { return (float)(int32_t)NextU() * (1.0f / 2147483648.0f); }
  inline float Uni() { return (float)(NextU() >> 8) * (1.0f / 16777216.0f); }
};

struct OnePole {
  float z = 0.0f;
  inline float Lp(float x, float a) { z += a * (x - z); return z; }
  inline float Hp(float x, float a) { z += a * (x - z); return x - z; }
};

// coefficient of a one-pole (Lp/Hp above) for cutoff `hz`
inline float OnePoleHz(float hz) {
  hz = Clamp(hz, 1.0f, 20000.0f);
  return 1.0f - expf(-kTwoPi * hz * kInvSampleRate);
}

// Zavalishin TPT state-variable filter (stable under fast modulation). Outputs low, band and high at once.
struct Svf {
  float ic1 = 0.0f, ic2 = 0.0f;
  float g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
  void Reset() { ic1 = ic2 = 0.0f; }
  // q: 0.5 .. ~40; returns false if unchanged
  void Set(float hz, float q) {
    hz = Clamp(hz, 10.0f, 22000.0f);
    g = tanf(kPi * hz * kInvSampleRate);
    k = 1.0f / (q < 0.5f ? 0.5f : q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  inline void Process(float x, float* lp, float* bp, float* hp) {
    float v3 = x - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    *lp = v2;
    *bp = v1;
    *hp = x - k * v1 - v2;
  }
  inline float Lp(float x) { float l, b, h; Process(x, &l, &b, &h); return l; }
  inline float Bp(float x) { float l, b, h; Process(x, &l, &b, &h); return b; }
  inline float Hp(float x) { float l, b, h; Process(x, &l, &b, &h); return h; }
};

// The drumlogue's bipolar CUTOFF: -100..<0 = low-pass L0.0%..L99.5%, 0 = THRU, >0..100 = high-pass H0.5%..H100%,
// plus RESON 0..100 %. Shared by every part that has one (12 dB/oct, slightly driven, like an analogue SVF).
struct PartFilter {
  Svf svf;
  float cur = 0.0f, res = -1.0f;
  int mode = 0;   // 0 thru, 1 lp, 2 hp
  void Reset() { svf.Reset(); }
  void Set(float cutoff, float reson) {
    int m = cutoff < -0.25f ? 1 : (cutoff > 0.25f ? 2 : 0);
    if (m != mode) { svf.Reset(); mode = m; }
    if (cutoff == cur && reson == res) return;
    cur = cutoff; res = reson;
    float q = 0.6f + reson * reson * 0.0012f;   // 0.6 .. 12.6
    if (mode == 1) svf.Set(20.0f * powf(1000.0f, (100.0f + cutoff) * 0.01f), q);        // 20 Hz .. 20 kHz
    else if (mode == 2) svf.Set(20.0f * powf(400.0f, cutoff * 0.01f), q);               // 20 Hz .. 8 kHz
  }
  inline float Process(float x) {
    if (mode == 0) return x;
    float l, b, h;
    svf.Process(x, &l, &b, &h);
    float y = mode == 1 ? l : h;
    return res > 50.0f ? SoftClip(y) : y;   // high resonance stays in bounds like the hardware's
  }
  static void Format(float cutoff, char* buf, int len);
};

}  // namespace dlx
