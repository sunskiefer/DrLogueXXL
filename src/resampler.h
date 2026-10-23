// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- 48 kHz -> 44.1 kHz stereo resampler (exact 147/160 ratio, polyphase windowed sinc).
//
// The engine runs at 48 kHz like the drumlogue (and the logue SDK units, which assume it) and MPC wants 44.1 kHz.
// Output sample k sits at upsampled time t = k * 160; with i0 = t / 147 and phase p = t % 147,
//   y[k] = sum_{j<T} c[p][j] * x[i0 - j],   c[p][j] = h(p + j * 147)
// where h is a Kaiser-windowed sinc low-pass at the upsampled rate (cutoff 20.6 kHz, flat to 20 kHz, ~-90 dB above
// 22.05 kHz). Each phase is normalised to unity DC gain. Latency: T / 2 input samples (0.33 ms).
#pragma once
#include <math.h>
#include <string.h>

namespace dlx {

class Resampler48To44 {
 public:
  static const int kL = 147, kM = 160, kTaps = 32;
  static const int kRing = 1024;           // input history (power of two)

  void Init() {
    const int N = kL * kTaps;
    const double fu = 48000.0 * kL;       // upsampled rate
    const double fc = 20600.0 / fu;       // normalised cutoff
    const double beta = 9.0;
    double i0b = Bessel0(beta);
    for (int p = 0; p < kL; ++p) {
      double sum = 0.0;
      for (int j = 0; j < kTaps; ++j) {
        int n = p + j * kL;
        double m = n - (N - 1) * 0.5;
        double x = 2.0 * fc * m;
        double sinc = fabs(x) < 1e-12 ? 1.0 : sin(M_PI * x) / (M_PI * x);
        double r = 2.0 * n / (N - 1) - 1.0;
        double w = Bessel0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0b;
        coef_[p][j] = sinc * w;
        sum += coef_[p][j];
      }
      for (int j = 0; j < kTaps; ++j) coef_[p][j] = (float)(coef_[p][j] / sum);
    }
    Reset();
  }

  void Reset() {
    memset(ring_l_, 0, sizeof ring_l_);
    memset(ring_r_, 0, sizeof ring_r_);
    written_ = kTaps;                     // pretend kTaps zeros were written (history for the first outputs)
    t_ = (long long)(kTaps - 1) * kL;     // first output uses inputs 0..kTaps-1 (all zero)
  }

  // input samples needed beyond what is buffered to produce `frames` more outputs
  int InputNeeded(int frames) const {
    long long last_t = t_ + (long long)(frames - 1) * kM;
    long long need = last_t / kL + 1;     // index of the newest input the last output reads, + 1
    long long missing = need - written_;
    return missing > 0 ? (int)missing : 0;
  }

  void Push(const float* l, const float* r, int n) {
    for (int i = 0; i < n; ++i) {
      int w = (int)(written_ & (kRing - 1));
      ring_l_[w] = l[i];
      ring_r_[w] = r[i];
      ++written_;
    }
  }

  // produce `frames` outputs; the caller has pushed InputNeeded(frames) inputs first
  void Pull(float* out_l, float* out_r, int frames) {
    for (int k = 0; k < frames; ++k) {
      long long i0 = t_ / kL;
      int p = (int)(t_ - i0 * kL);
      const float* c = coef_[p];
      float al = 0.0f, ar = 0.0f;
      for (int j = 0; j < kTaps; ++j) {
        int idx = (int)((i0 - j) & (kRing - 1));
        al += c[j] * ring_l_[idx];
        ar += c[j] * ring_r_[idx];
      }
      out_l[k] = al;
      out_r[k] = ar;
      t_ += kM;
    }
    // keep the counters small: shift both by a multiple of the ring and of kL
    const long long kShift = (long long)kRing * kL;
    if (written_ > 2 * kShift && t_ / kL > kShift + kRing) {
      written_ -= kShift;
      t_ -= kShift * kL;
    }
  }

 private:
  static double Bessel0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k) {
      t *= (x / (2.0 * k)) * (x / (2.0 * k));
      s += t;
    }
    return s;
  }
  float coef_[kL][kTaps];
  float ring_l_[kRing], ring_r_[kRing];
  long long written_ = 0;   // inputs written so far (absolute index of the next one)
  long long t_ = 0;         // upsampled time of the next output
};

}  // namespace dlx
