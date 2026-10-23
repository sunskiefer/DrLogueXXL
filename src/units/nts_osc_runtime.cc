// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- generated stand-ins for the NTS-1 mkII firmware tables used by the vendored Korg osc units.
//
// wavesA..wavesF: the osc API declares six wave banks (16, 16, 14, 13, 15, 16 single-cycle waves of 128 samples,
// "organized in categories from A to F in order of increasing harmonic components"); the data itself is in the
// hardware ROM and not part of the logue SDK. These are our own additive/harmonic families with the same counts and
// sizes, band-limited (<= 40 harmonics at 128 samples), each normalised to a peak of 1:
//   A (16) pure and near-pure: sine, then sine + one low harmonic (2..6) at growing levels, odd/even blends
//   B (16) soft few-harmonic tones: 1/n^2 series (triangle-like) over 2..9 harmonics, odd-only and all-harmonic
//   C (14) organ-ish drawbar mixes of harmonics 1, 2, 3, 4, 6, 8 in 14 registrations
//   D (13) formant-ish: harmonic series with a resonant bump centred on harmonic 2..14
//   E (15) bright: 1/n (saw-like) and odd 1/n (square-like) series over 4..40 harmonics, plus pulse-width spectra
//   F (16) harsh/inharmonic-sounding: sparse high harmonic clusters, ring-mod style sidebands, comb spectra
// tanpi_lut_f: tan(pi x) for x in [0, 0.49] (257 points, as the osc API indexes it).
// bitres_lut_f: quantisation scale for osc_bitresf(x): 2^(bits-1), bits exponentially mapped 24 (x=0) .. 1 (x=1).
#include "units/nts_osc_runtime.h"

#include <math.h>
#include <string.h>
#include <mutex>

namespace dlx {
thread_local uint32_t* t_nts_rng = nullptr;
}

namespace {
const int kN = 128, kL = 129;   // k_waves_size, k_waves_lut_size
float gA[16][kL], gB[16][kL], gC[14][kL], gD[13][kL], gE[15][kL], gF[16][kL];
thread_local uint32_t t_fallback_rng = 0x6C8E9CF5u;

void Additive(float* w, const float* amp, int nh, const float* phase = nullptr) {
  double buf[kN];
  double peak = 0.0;
  for (int i = 0; i < kN; ++i) {
    double s = 0.0, t = (double)i / kN;
    for (int h = 1; h <= nh; ++h)
      if (amp[h - 1] != 0.0f) s += amp[h - 1] * sin(2.0 * M_PI * h * t + (phase ? phase[h - 1] : 0.0));
    buf[i] = s;
    if (fabs(s) > peak) peak = fabs(s);
  }
  double g = peak > 1e-9 ? 1.0 / peak : 0.0;
  for (int i = 0; i < kN; ++i) w[i] = (float)(buf[i] * g);
  w[kN] = w[0];
}

void Generate() {
  float a[40];
  // A: sine, sine + harmonic k (2..6) at 3 levels, then 3 odd/even blends
  { int idx = 0;
    memset(a, 0, sizeof a); a[0] = 1; Additive(gA[idx++], a, 1);
    for (int k = 2; k <= 6; ++k)
      for (int l = 0; l < 3 && idx < 16; ++l) { memset(a, 0, sizeof a); a[0] = 1; a[k - 1] = 0.15f + 0.25f * l; Additive(gA[idx++], a, k); }
  }
  // B: 1/n^2 series, odd-only (triangle family) then all harmonics, 2..9 harmonics
  for (int i = 0; i < 16; ++i) {
    memset(a, 0, sizeof a);
    int nh = 2 + (i % 8) * (i < 8 ? 2 : 1) + 1;
    if (nh > 17) nh = 17;
    for (int h = 1; h <= nh; ++h) {
      if (i < 8 && (h % 2) == 0) continue;
      a[h - 1] = ((h / 2) % 2 ? -1.0f : 1.0f) / (float)(h * h);
    }
    Additive(gB[i], a, nh);
  }
  // C: drawbar registrations over harmonics 1, 2, 3, 4, 6, 8
  { static const float reg[14][6] = {
      {1, 1, 0, 0, 0, 0}, {1, 0, 1, 0, 0, 0}, {1, 1, 1, 0, 0, 0}, {1, .5f, .5f, .5f, 0, 0}, {1, 1, 1, 1, 0, 0},
      {.5f, 1, .5f, 1, 0, 0}, {1, 0, 1, 0, 1, 0}, {1, .7f, .5f, .4f, .3f, .2f}, {1, 1, 1, 1, 1, 1}, {.3f, 1, 0, 1, 0, 1},
      {1, 0, 0, 1, 0, 1}, {.6f, .3f, 1, .3f, .6f, .3f}, {1, .2f, .8f, .2f, .6f, .9f}, {.4f, .4f, .4f, 1, 1, 1}};
    static const int hn[6] = {1, 2, 3, 4, 6, 8};
    for (int i = 0; i < 14; ++i) { memset(a, 0, sizeof a); for (int j = 0; j < 6; ++j) a[hn[j] - 1] = reg[i][j]; Additive(gC[i], a, 8); }
  }
  // D: 1/n series with a resonant bump at harmonic 2..14
  for (int i = 0; i < 13; ++i) {
    memset(a, 0, sizeof a);
    float c = 2.0f + i;
    for (int h = 1; h <= 24; ++h) { float d = (h - c) / 1.5f; a[h - 1] = 0.35f / h + expf(-d * d); }
    Additive(gD[i], a, 24);
  }
  // E: saw (1/n) and square (odd 1/n) over 4..40 harmonics, then pulse spectra
  for (int i = 0; i < 15; ++i) {
    memset(a, 0, sizeof a);
    if (i < 5) { int nh = 4 + i * 9; for (int h = 1; h <= nh; ++h) a[h - 1] = 1.0f / h; Additive(gE[i], a, nh); }
    else if (i < 10) { int nh = 5 + (i - 5) * 8; for (int h = 1; h <= nh; h += 2) a[h - 1] = 1.0f / h; Additive(gE[i], a, nh); }
    else { float pw = 0.08f + 0.08f * (i - 10); for (int h = 1; h <= 40; ++h) a[h - 1] = sinf((float)M_PI * h * pw) / h; Additive(gE[i], a, 40); }
  }
  // F: sparse high clusters, sidebands around a high centre, comb spectra
  for (int i = 0; i < 16; ++i) {
    memset(a, 0, sizeof a);
    if (i < 6) { a[0] = 0.5f; int c = 7 + i * 5; for (int k = -1; k <= 1; ++k) a[c - 1 + k] = 1.0f - 0.3f * (k != 0); }
    else if (i < 11) { int c = 6 + (i - 6) * 6, m = 1 + (i - 6); a[c - 1] = 1; a[c - 1 - m] = .8f; if (c - 1 + m < 40) a[c - 1 + m] = .8f; a[0] = .3f; }
    else { int step = 2 + (i - 11); for (int h = 1; h <= 40; h += step) a[h - 1] = 1.0f / sqrtf((float)h); }
    Additive(gF[i], a, 40);
  }
}
}  // namespace

extern "C" {
extern const float* const wavesA[16] = {gA[0], gA[1], gA[2], gA[3], gA[4], gA[5], gA[6], gA[7], gA[8], gA[9], gA[10], gA[11], gA[12], gA[13], gA[14], gA[15]};
extern const float* const wavesB[16] = {gB[0], gB[1], gB[2], gB[3], gB[4], gB[5], gB[6], gB[7], gB[8], gB[9], gB[10], gB[11], gB[12], gB[13], gB[14], gB[15]};
extern const float* const wavesC[14] = {gC[0], gC[1], gC[2], gC[3], gC[4], gC[5], gC[6], gC[7], gC[8], gC[9], gC[10], gC[11], gC[12], gC[13]};
extern const float* const wavesD[13] = {gD[0], gD[1], gD[2], gD[3], gD[4], gD[5], gD[6], gD[7], gD[8], gD[9], gD[10], gD[11], gD[12]};
extern const float* const wavesE[15] = {gE[0], gE[1], gE[2], gE[3], gE[4], gE[5], gE[6], gE[7], gE[8], gE[9], gE[10], gE[11], gE[12], gE[13], gE[14]};
extern const float* const wavesF[16] = {gF[0], gF[1], gF[2], gF[3], gF[4], gF[5], gF[6], gF[7], gF[8], gF[9], gF[10], gF[11], gF[12], gF[13], gF[14], gF[15]};
extern float tanpi_lut_f[257];  // declared const in osc_api.h; written once before any unit exists
float tanpi_lut_f[257];
extern float bitres_lut_f[129];
float bitres_lut_f[129];

float osc_white(void) {
  uint32_t* p = dlx::t_nts_rng ? dlx::t_nts_rng : &t_fallback_rng;
  uint32_t s = *p;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  *p = s;
  return (float)(int32_t)s * (1.0f / 2147483648.0f);
}
}

namespace dlx {
void NtsOscTablesInit() {
  static std::once_flag once;
  std::call_once(once, [] {
    Generate();
    for (int i = 0; i < 257; ++i) tanpi_lut_f[i] = (float)tan(M_PI * (0.49 * i / 256.0));
    for (int i = 0; i < 129; ++i) {
      double bits = pow(24.0, 1.0 - i / 128.0);
      bitres_lut_f[i] = (float)pow(2.0, bits - 1.0);
    }
  });
}
}  // namespace dlx
