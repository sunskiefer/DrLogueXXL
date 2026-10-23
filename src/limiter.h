// SPDX-License-Identifier: GPL-3.0-or-later
// Brickwall limiter, the same as RMXXXL's (https://github.com/sunskiefer/RMXXXL, src/dsp.h, by the same author).
// Stereo-linked look-ahead peak limiter. Drive pushes the signal in, Ceiling is the absolute output maximum.
// The gain needed for each incoming sample is known kLookahead samples before that sample is played; a sliding
// minimum over that window, smoothed to reach its value within the window, pulls the gain down before the peak
// arrives (no distortion on transients), and a final clamp at the ceiling makes it a true brickwall even if the
// smoothing lags. Adds kLookahead samples (1.45 ms) of latency.
// Same output as RMXXXL's, sample for sample: the window minimum comes from a monotonic queue (O(1) per sample)
// instead of a scan of all 64 entries (PlateauXXL test/plateau_test.cc checks it against the scan).
#pragma once
#include <math.h>
#include <string.h>

namespace dlx {

struct Limiter {
  static const int kLookahead = 64;
  float delay[kLookahead][2];
  // sliding minimum of the gain the last kLookahead samples need: increasing values, oldest first
  float q_val[kLookahead];
  long q_idx[kLookahead];
  int q_head, q_count;
  long counter;
  int pos;
  float gain;
  float gr_db;                   // last block's deepest gain reduction

  void Init() {
    memset(this, 0, sizeof *this);
    gain = 1.0f;
  }

  static float DbToGain(float db) { return powf(10.0f, db / 20.0f); }
  static float Clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

  void Process(float* l, float* r, int n, float drive_db, float ceiling_db, float release_ms, float sample_rate) {
    float drive = DbToGain(drive_db), ceiling = DbToGain(ceiling_db);
    float attack = 1.0f - expf(-1.0f / (kLookahead / 5.0f));            // ~99 % within the look-ahead
    float release = 1.0f - expf(-1.0f / (Clampf(release_ms, 1.0f, 2000.0f) * 0.001f * sample_rate));
    float deepest = 1.0f;
    for (int i = 0; i < n; ++i) {
      float a = l[i] * drive, b = r[i] * drive;
      float peak = fmaxf(fabsf(a), fabsf(b));
      float need = peak > ceiling ? ceiling / peak : 1.0f;
      float out_l = delay[pos][0], out_r = delay[pos][1];
      delay[pos][0] = a; delay[pos][1] = b;
      pos = (pos + 1) % kLookahead;
      // the window: this sample's need and the previous kLookahead - 1 (all <= 1, as the unfilled start's are)
      while (q_count > 0 && q_val[(q_head + q_count - 1) % kLookahead] >= need) --q_count;
      int back = (q_head + q_count) % kLookahead;
      q_val[back] = need;
      q_idx[back] = counter;
      ++q_count;
      while (q_idx[q_head] <= counter - kLookahead) { q_head = (q_head + 1) % kLookahead; --q_count; }
      ++counter;
      float target = q_val[q_head];
      gain += (target - gain) * (target < gain ? attack : release);
      l[i] = Clampf(out_l * gain, -ceiling, ceiling);    // the brickwall
      r[i] = Clampf(out_r * gain, -ceiling, ceiling);
      if (gain < deepest) deepest = gain;
    }
    gr_db = 20.0f * log10f(fmaxf(deepest, 1e-6f));
  }
};

}  // namespace dlx
