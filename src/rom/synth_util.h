// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- building blocks for synthesising the factory sample banks (worker thread only, may allocate).
//
// Everything renders at 48 kHz into float buffers; src/rom/sample_rom.cc trims, normalises and stores them as int16.
// All randomness comes from dlx::Noise with fixed seeds, so the banks are identical on every run.
#pragma once
#include <math.h>
#include <stdint.h>
#include <vector>
#include "../dsp.h"

namespace dlx {
namespace syn {

static const float kSr = 48000.0f;

// one rendered sound: mono (l) or stereo (l + r)
struct Buf {
  std::vector<float> l, r;
  bool stereo = false;
  int n = 0;
  void Alloc(float sec, bool st) {
    n = (int)(sec * kSr);
    if (n < 1) n = 1;
    l.assign(n, 0.0f);
    stereo = st;
    if (st) r.assign(n, 0.0f); else r.clear();
  }
  float* L() { return &l[0]; }
  float* R() { return stereo ? &r[0] : &l[0]; }
  float* Ch(int c) { return c ? R() : L(); }
  int Channels() const { return stereo ? 2 : 1; }
  // mono -> stereo copy (so a stereo effect can follow a mono source)
  void MakeStereo() { if (!stereo) { r = l; stereo = true; } }
};

inline float Rnd(Noise& n, float lo, float hi) { return lo + (hi - lo) * n.Uni(); }

// PolyBLEP correction for a discontinuity at phase 0; t in [0,1), dt = phase increment
inline float PolyBlep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

// band-limited square (duty 0.5 default) / pulse
struct SqOsc {
  float ph = 0.0f, inc = 0.0f, duty = 0.5f;
  void Set(float hz) { inc = hz / kSr; if (inc > 0.45f) inc = 0.45f; }
  inline float Next() {
    float y = ph < duty ? 1.0f : -1.0f;
    y += PolyBlep(ph, inc);
    float t2 = ph - duty; if (t2 < 0.0f) t2 += 1.0f;
    y -= PolyBlep(t2, inc);
    ph += inc; if (ph >= 1.0f) ph -= 1.0f;
    return y;
  }
};

// band-limited sawtooth
struct SawOsc {
  float ph = 0.0f, inc = 0.0f;
  void Set(float hz) { inc = hz / kSr; if (inc > 0.45f) inc = 0.45f; }
  inline float Next() {
    float y = 2.0f * ph - 1.0f - PolyBlep(ph, inc);
    ph += inc; if (ph >= 1.0f) ph -= 1.0f;
    return y;
  }
};

// exponentially decaying sinusoid by recursion (a damped complex rotation): 4 multiplies, no sin() per sample
struct Partial {
  float c = 0.0f, s = 0.0f, re = 0.0f, im = 0.0f;
  void Set(float hz, float decay60, float amp, float phase) {
    float r = decay60 > 0.0f ? DecayCoef60(decay60) : 1.0f;
    float w = kTwoPi * hz / kSr;
    c = r * cosf(w); s = r * sinf(w);
    re = amp * cosf(phase); im = amp * sinf(phase);
  }
  inline float Next() {
    float nr = re * c - im * s;
    im = re * s + im * c;
    re = nr;
    return im;
  }
};

// two-pole resonator ("bridged-T" ping): an impulse of 1 gives a sine of amplitude ~1
struct Reso {
  float a1 = 0.0f, a2 = 0.0f, b0 = 0.0f, y1 = 0.0f, y2 = 0.0f;
  void Set(float hz, float decay60) {
    float w = kTwoPi * hz / kSr;
    float r = DecayCoef60(decay60);
    a1 = 2.0f * r * cosf(w); a2 = r * r; b0 = sinf(w);
  }
  inline float Tick(float x) {
    float y = b0 * x + a1 * y1 - a2 * y2;
    y2 = y1; y1 = y;
    return y;
  }
};

// Paul Kellet's economy pink noise
struct Pink {
  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
  inline float Next(float w) {
    b0 = 0.99765f * b0 + w * 0.0990460f;
    b1 = 0.96300f * b1 + w * 0.2965164f;
    b2 = 0.57000f * b2 + w * 1.0526913f;
    return (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
  }
};

// small Schroeder/Freeverb-style stereo reverb (mono in), for clap rooms, crashes and the like
struct Verb {
  struct Comb { std::vector<float> d; int i = 0; float fb = 0.8f, damp = 0.3f, z = 0.0f;
    inline float Tick(float x) { float y = d[i]; z = y + damp * (z - y); d[i] = x + z * fb; if (++i >= (int)d.size()) i = 0; return y; } };
  struct Ap { std::vector<float> d; int i = 0;
    inline float Tick(float x) { float b = d[i]; float y = b - x; d[i] = x + b * 0.5f; if (++i >= (int)d.size()) i = 0; return y; } };
  Comb c[2][4];
  Ap a[2][2];
  // time: decay to -60 dB in seconds (roughly), damp 0..1 (high-frequency loss), size scales the delay lengths
  void Init(float time, float damp, float size) {
    static const int kc[4] = {1557, 1617, 1491, 1422};
    static const int ka[2] = {556, 441};
    for (int ch = 0; ch < 2; ch++) {
      for (int k = 0; k < 4; k++) {
        int len = (int)((kc[k] + ch * 23) * 1.088f * size);
        if (len < 8) len = 8;
        c[ch][k].d.assign(len, 0.0f);
        c[ch][k].i = 0; c[ch][k].z = 0.0f;
        float t = (float)len / kSr;
        c[ch][k].fb = powf(10.0f, -3.0f * t / (time > 0.05f ? time : 0.05f));
        c[ch][k].damp = damp;
      }
      for (int k = 0; k < 2; k++) {
        int len = (int)((ka[k] + ch * 23) * 1.088f * size);
        a[ch][k].d.assign(len, 0.0f);
        a[ch][k].i = 0;
      }
    }
  }
  inline void Tick(float x, float* l, float* r) {
    float in = x * 0.08f;
    for (int ch = 0; ch < 2; ch++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += c[ch][k].Tick(in);
      s = a[ch][0].Tick(s);
      s = a[ch][1].Tick(s);
      if (ch) *r = s; else *l = s;
    }
  }
};

// ---------- whole-buffer processing (post effects) ----------
void HighPass(Buf& b, float hz, float q = 0.707f);
void LowPass(Buf& b, float hz, float q = 0.707f);
void BandPass(Buf& b, float hz, float q);
void Saturate(Buf& b, float drive);            // tanh-ish, drive 1 = gentle
void Crush(Buf& b, float bits);                // quantise to `bits` (relative to the buffer's peak)
void HoldRate(Buf& b, float hz);               // zero-order-hold sample-rate reduction (aliasing on purpose)
void MuLaw8(Buf& b);                           // 8-bit mu-law round trip (LinnDrum-era companding)
void AddVerb(Buf& b, float mix, float time, float damp, float size, float predelay);   // makes the buffer stereo
void FadeOut(Buf& b, float sec);               // raised-cosine fade over the last `sec`
void Gain(Buf& b, float g);
float Peak(const Buf& b);

// vintage-machine colouring used by the EXP bank
struct Vint {
  float rate = 0.0f;    // > 0: zero-order hold at this rate (no anti-alias filter: SP-1200 style)
  float aa = 0.0f;      // > 0: low-pass at this frequency before the hold (a machine with a filter)
  float bits = 0.0f;    // > 0: quantise
  bool mulaw = false;   // 8-bit mu-law
  float lp = 0.0f;      // output low-pass (reconstruction filter)
  float hp = 0.0f;
  float drive = 0.0f;
  float hiss = 0.0f;    // noise floor level (relative to peak), follows the sound's envelope loosely
};
void ApplyVint(Buf& b, const Vint& v, uint32_t seed);

}  // namespace syn
}  // namespace dlx
