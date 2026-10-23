// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- whole-buffer helpers for the factory sample synthesis (see synth_util.h).
#include "synth_util.h"

namespace dlx {
namespace syn {

static void FilterBuf(Buf& b, float hz, float q, int type) {
  for (int c = 0; c < b.Channels(); c++) {
    Svf f;
    f.Set(hz, q);
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) {
      float l, bp, h;
      f.Process(x[i], &l, &bp, &h);
      x[i] = type == 0 ? l : (type == 1 ? bp : h);
    }
  }
}
void HighPass(Buf& b, float hz, float q) { FilterBuf(b, hz, q, 2); }
void LowPass(Buf& b, float hz, float q) { FilterBuf(b, hz, q, 0); }
void BandPass(Buf& b, float hz, float q) { FilterBuf(b, hz, q, 1); }

float Peak(const Buf& b) {
  float p = 0.0f;
  for (int i = 0; i < b.n; i++) {
    float a = fabsf(b.l[i]);
    if (a > p) p = a;
    if (b.stereo) { a = fabsf(b.r[i]); if (a > p) p = a; }
  }
  return p;
}

void Gain(Buf& b, float g) {
  for (int i = 0; i < b.n; i++) { b.l[i] *= g; if (b.stereo) b.r[i] *= g; }
}

void Saturate(Buf& b, float drive) {
  float pk = Peak(b);
  if (pk <= 0.0f) return;
  float g = (1.0f + drive) / pk;
  float mk = pk / SoftClip(1.0f + drive);
  for (int c = 0; c < b.Channels(); c++) {
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) x[i] = SoftClip(x[i] * g) * mk;
  }
}

void Crush(Buf& b, float bits) {
  float pk = Peak(b);
  if (pk <= 0.0f) return;
  float steps = powf(2.0f, bits - 1.0f);
  for (int c = 0; c < b.Channels(); c++) {
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) x[i] = floorf(x[i] / pk * steps + 0.5f) / steps * pk;
  }
}

void HoldRate(Buf& b, float hz) {
  if (hz <= 0.0f || hz >= kSr) return;
  float step = hz / kSr;
  for (int c = 0; c < b.Channels(); c++) {
    float* x = b.Ch(c);
    float ph = 1.0f, held = 0.0f;
    for (int i = 0; i < b.n; i++) {
      ph += step;
      if (ph >= 1.0f) { ph -= 1.0f; held = x[i]; }
      x[i] = held;
    }
  }
}

void MuLaw8(Buf& b) {
  float pk = Peak(b);
  if (pk <= 0.0f) return;
  const float mu = 255.0f, lmu = logf(1.0f + mu);
  for (int c = 0; c < b.Channels(); c++) {
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) {
      float v = x[i] / pk;
      float s = v < 0.0f ? -1.0f : 1.0f;
      float e = logf(1.0f + mu * fabsf(v)) / lmu;      // 0..1
      e = floorf(e * 127.0f + 0.5f) / 127.0f;           // 8 bits incl. sign
      x[i] = s * (powf(1.0f + mu, e) - 1.0f) / mu * pk;
    }
  }
}

void AddVerb(Buf& b, float mix, float time, float damp, float size, float predelay) {
  if (mix <= 0.0f) return;
  int extra = (int)((time * 0.6f + predelay) * kSr);
  int n0 = b.n;
  b.l.resize(n0 + extra, 0.0f);
  if (b.stereo) b.r.resize(n0 + extra, 0.0f);
  b.n = n0 + extra;
  b.MakeStereo();
  Verb v;
  v.Init(time, damp, size);
  int pd = (int)(predelay * kSr);
  std::vector<float> dl(pd + 1, 0.0f);
  int di = 0;
  float wet = mix, dry = 1.0f - 0.5f * mix;
  for (int i = 0; i < b.n; i++) {
    float in = 0.5f * (b.l[i] + b.r[i]);
    dl[di] = in;
    di = di + 1 > pd ? 0 : di + 1;
    float d = dl[di];
    float wl, wr;
    v.Tick(d, &wl, &wr);
    b.l[i] = b.l[i] * dry + wl * wet;
    b.r[i] = b.r[i] * dry + wr * wet;
  }
}

void FadeOut(Buf& b, float sec) {
  int m = (int)(sec * kSr);
  if (m > b.n) m = b.n;
  for (int k = 0; k < m; k++) {
    int i = b.n - m + k;
    float g = 0.5f + 0.5f * cosf(kPi * (float)(k + 1) / (float)m);
    b.l[i] *= g;
    if (b.stereo) b.r[i] *= g;
  }
}

void ApplyVint(Buf& b, const Vint& v, uint32_t seed) {
  if (v.drive > 0.0f) Saturate(b, v.drive);
  if (v.aa > 0.0f) { LowPass(b, v.aa, 0.6f); LowPass(b, v.aa, 0.6f); }
  if (v.mulaw) MuLaw8(b);
  if (v.bits > 0.0f) Crush(b, v.bits);
  if (v.rate > 0.0f) HoldRate(b, v.rate);
  if (v.hiss > 0.0f) {
    Noise nz;
    nz.Seed(seed * 2654435761u + 77u);
    float pk = Peak(b);
    // hiss follows a slow envelope of the signal (a noisy machine's floor, gated by its VCA)
    for (int c = 0; c < b.Channels(); c++) {
      float* x = b.Ch(c);
      float env = 0.0f;
      float ka = OnePoleCoef(0.002f), kr = OnePoleCoef(0.15f);
      for (int i = 0; i < b.n; i++) {
        float a = fabsf(x[i]);
        env += (a > env ? ka : kr) * (a - env);
        x[i] += nz.Next() * v.hiss * (0.3f * pk + env);
      }
    }
  }
  if (v.hp > 0.0f) HighPass(b, v.hp, 0.707f);
  if (v.lp > 0.0f) LowPass(b, v.lp, 0.707f);
}

}  // namespace syn
}  // namespace dlx
