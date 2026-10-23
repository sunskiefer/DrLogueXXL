// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- building blocks shared by the three effect slots (delay.cc, reverb.cc, master.cc).
// Everything here is allocation-free once Alloc() has run (constructor time), and lock/IO free.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "../dsp.h"

namespace dlx {
namespace fxu {

// A power-of-two float ring buffer. Clear() is O(1): reads older than what was written since the last Clear()
// return 0, so a type change never needs a megabyte memset on the audio thread.
struct Line {
  float* buf = nullptr;
  int size = 0, mask = 0, w = 0, filled = 0;
  void Alloc(int pow2) {
    if (buf) return;
    buf = new float[pow2]();
    size = pow2;
    mask = pow2 - 1;
    w = 0;
    filled = 0;
  }
  void Free() { delete[] buf; buf = nullptr; size = mask = 0; }
  void Clear() { filled = 0; }
  inline void Write(float x) {
    w = (w + 1) & mask;
    buf[w] = x;
    filled += filled < size;
  }
  // the sample written k writes ago (k = 0: the last one written)
  inline float At(int k) const { return (unsigned)k < (unsigned)filled ? buf[(w - k) & mask] : 0.0f; }
  inline float Lin(float d) const {
    int i = (int)d;
    float f = d - (float)i;
    float a = At(i), b = At(i + 1);
    return a + (b - a) * f;
  }
  // 4-point Hermite, d >= 1
  inline float Herm(float d) const {
    int i = (int)d;
    float f = d - (float)i;
    float xm1 = At(i - 1), x0 = At(i), x1 = At(i + 1), x2 = At(i + 2);
    float c = (x1 - xm1) * 0.5f;
    float v = x0 - x1;
    float ww = c + v;
    float a = ww + v + (x2 - x0) * 0.5f;
    float bn = ww + a;
    return (((a * f) - bn) * f + c) * f + x0;
  }
};

// Topology-preserving (bilinear) one-pole: LP + HP == input exactly, LP - HP is a first-order all-pass.
struct Tpt1 {
  float s = 0.0f, g = 0.5f;
  void Reset() { s = 0.0f; }
  void Set(float hz) {
    float t = tanf(kPi * Clamp(hz, 5.0f, 23000.0f) * kInvSampleRate);
    g = t / (1.0f + t);
  }
  inline float Lp(float x) {
    float v = (x - s) * g;
    float y = v + s;
    s = y + v;
    return y;
  }
  inline float Hp(float x) { return x - Lp(x); }
};

// Tiny broadband noise (~ -400 dBFS) added where a recursive state could otherwise decay into denormals on
// x86 (the device runs flush-to-zero anyway). Broadband so it survives both low- and high-pass filters.
struct Dither {
  uint32_t s = 0x2545f491u;
  inline float Next() {
    s = s * 1664525u + 1013904223u;
    return (float)(int32_t)s * 4.66e-30f;   // +-1e-20
  }
};

// Transparent below 1, then a soft knee that never exceeds 1.25 -- the safety net inside feedback loops.
inline float SafeLim(float x) {
  float a = fabsf(x);
  if (a <= 1.0f) return x;
  float u = (a - 1.0f) * 4.0f;
  float y = 1.0f + 0.25f * u / (1.0f + u);
  return x < 0.0f ? -y : y;
}

inline float FastLog2(float x) {   // x > 0, error < 0.005
  union { float f; uint32_t i; } u;
  u.f = x;
  float e = (float)(int)((u.i >> 23) & 255) - 127.0f;
  u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
  float m = u.f;
  return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
inline float FastExp2(float x) {   // relative error < 1e-4
  x = Clamp(x, -126.0f, 126.0f);
  int i = (int)x;
  if (x < 0.0f && (float)i != x) --i;
  float f = x - (float)i;
  float p = 1.0f + f * (0.6960656421f + f * (0.224494337f + f * 0.07944023841f));
  union { uint32_t i; float f; } u;
  u.i = (uint32_t)(i + 127) << 23;
  return u.f * p;
}
inline float FastDb(float x) { return 6.0205999f * FastLog2(x > 1e-12f ? x : 1e-12f); }
inline float FastDbToGain(float db) { return FastExp2(db * 0.16609640f); }

// Hadamard mixes (orthonormal)
inline void Fwht4(float* x) {
  float a = x[0] + x[1], b = x[0] - x[1], c = x[2] + x[3], d = x[2] - x[3];
  x[0] = (a + c) * 0.5f; x[2] = (a - c) * 0.5f; x[1] = (b + d) * 0.5f; x[3] = (b - d) * 0.5f;
}
inline void Fwht8(float* x) {
  for (int h = 1; h < 8; h <<= 1)
    for (int i = 0; i < 8; i += h << 1)
      for (int j = i; j < i + h; ++j) {
        float a = x[j], b = x[j + h];
        x[j] = a + b;
        x[j + h] = a - b;
      }
  for (int i = 0; i < 8; ++i) x[i] *= 0.35355339f;
}

// Two-head granular pitch shifter (the classic Clouds/Eventide layout): two read heads half a window apart,
// triangle crossfade, so each head is silent when it jumps. ratio 2 = octave up, 0.5 = octave down.
struct Shifter {
  Line buf;
  float phase = 0.0f, size = 2048.0f, ratio = 1.0f;
  void Alloc() { buf.Alloc(4096); }
  void Free() { buf.Free(); }
  void Clear() { buf.Clear(); phase = 0.0f; }
  inline float Process(float x) {
    buf.Write(x);
    phase += (1.0f - ratio) / size;
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    float tri = 2.0f * (phase >= 0.5f ? 1.0f - phase : phase);
    float d1 = phase * size;
    float d2 = d1 + 0.5f * size;
    if (d2 >= size) d2 -= size;
    return buf.Lin(d1) * tri + buf.Lin(d2) * (1.0f - tri);
  }
};

inline void FormatHz(float hz, char* buf, int len) {
  if (hz < 1000.0f) snprintf(buf, len, "%.0f Hz", hz);
  else if (hz < 10000.0f) snprintf(buf, len, "%.2f kHz", hz * 0.001f);
  else snprintf(buf, len, "%.1f kHz", hz * 0.001f);
}
inline void FormatMs(float ms, char* buf, int len) {
  if (ms < 10.0f) snprintf(buf, len, "%.1f ms", ms);
  else if (ms < 1000.0f) snprintf(buf, len, "%.0f ms", ms);
  else snprintf(buf, len, "%.2f s", ms * 0.001f);
}
inline int TypeOf(float v, int count) {
  int t = (int)(v + 0.5f);
  return t < 0 ? 0 : (t >= count ? count - 1 : t);
}

}  // namespace fxu
}  // namespace dlx
