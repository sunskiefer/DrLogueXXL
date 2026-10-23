// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- offline test of the analogue parts (BD, SD, LT, HT).
// Build (x86, from the repo root):
//   g++ -O2 -std=gnu++11 -fsanitize=address,undefined -Isrc test/analog_test.cc src/parts/bd.cc src/parts/sd.cc
//       src/parts/tom.cc -o /tmp/analog_test && /tmp/analog_test
// Renders hits into test/out/analog/*.wav (48 kHz 16-bit mono), prints peak/RMS/f0/decay per WAV, asserts sanity,
// and times a 10 s render of each part.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include "part.h"

using namespace dlx;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

static void WriteWav(const std::string& path, const std::vector<float>& x) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("cannot write %s\n", path.c_str()); ++g_fail; return; }
  uint32_t n = (uint32_t)x.size(), sr = 48000, br = sr * 2, data = n * 2, riff = 36 + data;
  uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
  uint32_t fmt = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
  fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (uint32_t i = 0; i < n; ++i) {
    float v = x[i] < -1.0f ? -1.0f : (x[i] > 1.0f ? 1.0f : x[i]);
    int16_t s = (int16_t)lrintf(v * 32767.0f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

struct Rig {
  float v[P_COUNT];
  Part* part;
  int base;
  int which;
  Rig(int which_) : which(which_) {
    for (int i = 0; i < P_COUNT; ++i) v[i] = kParams[i].def;
    if (which == PART_BD) { part = NewBassDrum(); base = P_BD_DECAY; }
    else if (which == PART_SD) { part = NewSnareDrum(); base = P_SD_DECAY; }
    else { part = NewTom(); base = which == PART_LT ? P_LT_DECAY : P_HT_DECAY; }
    part->Init(&v[base], which, 1234u + (uint32_t)which);
  }
  ~Rig() { delete part; }
  float& P(int off) { return v[base + off]; }
  void Defaults() { for (int i = 0; i < P_COUNT; ++i) v[i] = kParams[i].def; }
  // render `sec` seconds; events: (sample, velocity) triggers, choke at choke_at (-1 none)
  std::vector<float> Run(float sec, const std::vector<std::pair<int, float> >& trig, int choke_at = -1,
                         int* inactive_at = nullptr) {
    int total = (int)(sec * 48000.0f);
    std::vector<float> out(total, 0.0f);
    float l[kBlock], r[kBlock];
    size_t ti = 0;
    if (inactive_at) *inactive_at = -1;
    for (int pos = 0; pos < total; pos += kBlock) {
      int n = total - pos < kBlock ? total - pos : kBlock;
      // events are quantised to block starts (as the engine does)
      while (ti < trig.size() && trig[ti].first < pos + n) { part->Trigger(trig[ti].second, -1); ++ti; if (inactive_at) *inactive_at = -1; }
      if (choke_at >= pos && choke_at < pos + n) part->Choke();
      if (part->Active()) {
        int ch = part->Render(l, r, n);
        if (ch != 1) { printf("  FAIL: Render returned %d\n", ch); ++g_fail; }
        for (int i = 0; i < n; ++i) out[pos + i] = l[i];
      } else if (inactive_at && *inactive_at < 0) {
        *inactive_at = pos;
      }
    }
    return out;
  }
};

struct Stats { float peak, rms, dc, f0, t40, maxjump; };

// fundamental by autocorrelation over [a, b) seconds, searched in [fmin, fmax]
static float EstimateF0(const std::vector<float>& x, float a, float b, float fmin, float fmax) {
  int i0 = (int)(a * 48000), i1 = (int)(b * 48000);
  if (i1 > (int)x.size()) i1 = (int)x.size();
  int lmin = (int)(48000 / fmax), lmax = (int)(48000 / fmin);
  if (i1 - i0 < lmax * 2) return 0.0f;
  float best = -1e30f; int bl = 0;
  std::vector<float> c(lmax + 2, 0.0f);
  for (int L = lmin; L <= lmax + 1; ++L) {
    double s = 0, e1 = 0, e2 = 0;
    for (int i = i0; i + L < i1; ++i) { s += x[i] * x[i + L]; e1 += x[i] * x[i]; e2 += x[i + L] * x[i + L]; }
    c[L] = (float)(s / (sqrt(e1 * e2) + 1e-20));
  }
  // first strong peak (avoid octave errors): the earliest local max within 90 % of the global max
  float gmax = -1.0f;
  for (int L = lmin + 1; L <= lmax; ++L) gmax = c[L] > gmax ? c[L] : gmax;
  for (int L = lmin + 1; L <= lmax; ++L)
    if (c[L] >= c[L - 1] && c[L] >= c[L + 1] && c[L] > 0.9f * gmax) { bl = L; best = c[L]; break; }
  if (bl == 0 || best < 0.3f) return 0.0f;
  // parabolic interpolation
  float y0 = c[bl - 1], y1 = c[bl], y2 = c[bl + 1];
  float d = 0.5f * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-20f);
  return 48000.0f / ((float)bl + d);
}

static Stats Analyse(const std::vector<float>& x, int from, int to, float f0a, float f0b, float fmin, float fmax) {
  Stats s; memset(&s, 0, sizeof(s));
  int n = to - from;
  double sum = 0, sq = 0;
  int last40 = from;
  float pk = 0;
  for (int i = from; i < to; ++i) { float a = fabsf(x[i]); pk = a > pk ? a : pk; }
  for (int i = from; i < to; ++i) {
    float a = fabsf(x[i]);
    sum += x[i]; sq += (double)x[i] * x[i];
    if (a > pk * 0.01f) last40 = i;
    if (i > from) { float j = fabsf(x[i] - x[i - 1]); s.maxjump = j > s.maxjump ? j : s.maxjump; }
  }
  s.peak = pk;
  // DC over the hit: from the onset to the -40 dB point, but at least 100 ms (a hit that the filter reduces to a
  // few-ms burst would otherwise report that burst's half-cycle as "DC")
  int win = last40 - from + 1; if (win < 4800) win = 4800 < n ? 4800 : n;
  sum = 0; for (int i = from; i < from + win; ++i) sum += x[i];
  s.dc = (float)(sum / win);
  s.rms = (float)sqrt(sq / (last40 - from + 1));
  s.t40 = (float)(last40 - from) / 48000.0f;
  std::vector<float> seg(x.begin() + from, x.begin() + from + n);
  s.f0 = f0b > f0a ? EstimateF0(seg, f0a, f0b, fmin, fmax) : 0.0f;
  return s;
}

static bool Finite(const std::vector<float>& x) {
  for (size_t i = 0; i < x.size(); ++i) if (!(x[i] == x[i]) || fabsf(x[i]) > 1e30f) return false;
  return true;
}

static const char* kOut = "test/out/analog/";

// Render one single hit with the current params, write WAV, analyse, run the standard asserts.
static Stats Hit(Rig& rig, const char* name, float sec, float vel = 1.0f, float f0a = 0, float f0b = 0,
                 float fmin = 20, float fmax = 1000, bool expect_inactive = true) {
  std::vector<std::pair<int, float> > t(1, std::make_pair(0, vel));
  int inact = -1;
  std::vector<float> x = rig.Run(sec, t, -1, &inact);
  WriteWav(std::string(kOut) + name + ".wav", x);
  Stats s = Analyse(x, 0, (int)x.size(), f0a, f0b, fmin, fmax);
  printf("  %-26s peak %.3f  rms %.3f  dc %+.4f  f0 %7.1f Hz  t-40dB %6.3f s  off@ %6.3f s\n", name, s.peak, s.rms,
         s.dc, s.f0, s.t40, inact < 0 ? -1.0f : inact / 48000.0f);
  CHECK(Finite(x), "%s: NaN/Inf", name);
  CHECK(s.peak <= 1.5f, "%s: peak %.3f > 1.5", name, s.peak);
  CHECK(s.peak > 0.01f, "%s: silent (peak %.4f)", name, s.peak);
  CHECK(fabsf(s.dc) < 0.02f, "%s: DC %.4f", name, s.dc);
  if (expect_inactive) CHECK(inact >= 0, "%s: still Active() after %.1f s", name, sec);
  return s;
}

static void RetrigTest(Rig& rig, const char* name, float ref_jump) {
  // rapid retriggers at 10..80 ms spacing with varying velocity, mid-note
  std::vector<std::pair<int, float> > t;
  int pos = 0;
  float vel[] = {1.0f, 0.6f, 1.0f, 0.3f, 0.9f, 1.0f, 0.5f, 1.0f};
  int gaps[] = {4800, 3840, 480, 960, 1440, 2400, 720, 9600};
  for (int k = 0; k < 8; ++k) { t.push_back(std::make_pair(pos, vel[k])); pos += gaps[k]; }
  std::vector<float> x = rig.Run(2.0f, t);
  WriteWav(std::string(kOut) + name + ".wav", x);
  float worst = 0;
  for (size_t k = 1; k < t.size(); ++k) {
    int at = (t[k].first / kBlock) * kBlock;
    for (int i = at; i < at + 3 && i < (int)x.size(); ++i) {
      float j = fabsf(x[i] - x[i - 1]);
      worst = j > worst ? j : worst;
    }
  }
  printf("  %-26s retrigger jump %.4f (fresh-hit max jump %.4f)\n", name, worst, ref_jump);
  CHECK(Finite(x), "%s: NaN/Inf", name);
  CHECK(worst <= ref_jump * 1.25f + 0.02f, "%s: click on retrigger (%.4f vs %.4f)", name, worst, ref_jump);
  float pk = 0; for (size_t i = 0; i < x.size(); ++i) pk = fabsf(x[i]) > pk ? fabsf(x[i]) : pk;
  CHECK(pk <= 1.5f, "%s: peak %.3f", name, pk);
}

static void ChokeTest(Rig& rig, const char* name) {
  std::vector<std::pair<int, float> > t(1, std::make_pair(0, 1.0f));
  int inact = -1;
  int at = 4800;   // 100 ms in
  std::vector<float> x = rig.Run(0.5f, t, at, &inact);
  WriteWav(std::string(kOut) + name + ".wav", x);
  float maxj = 0, after = 0;
  for (int i = at; i < at + 400; ++i) { float j = fabsf(x[i] - x[i - 1]); maxj = j > maxj ? j : maxj; }
  for (int i = at + 64 + 200; i < (int)x.size(); ++i) after = fabsf(x[i]) > after ? fabsf(x[i]) : after;
  printf("  %-26s inactive @ %.4f s, max jump in fade %.4f, residual %.5f\n", name, inact / 48000.0f, maxj, after);
  CHECK(inact >= 0 && inact <= at + 64 * 4, "%s: choke too slow (inactive @ %d)", name, inact);
  CHECK(after < 1e-3f, "%s: residual after choke %.5f", name, after);
}

static double TimeRender(Rig& rig) {
  // 10 s, a hit every 250 ms with a long decay so the voice stays active the whole time
  const int total = 480000;
  float l[kBlock], r[kBlock];
  volatile float sink = 0;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  int active_samples = 0;
  for (int pos = 0; pos < total; pos += kBlock) {
    if (pos % 12000 < kBlock) rig.part->Trigger(1.0f, -1);
    if (rig.part->Active()) { rig.part->Render(l, r, kBlock); sink += l[7]; active_samples += kBlock; }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  (void)sink;
  double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
  return ns / (active_samples > 0 ? active_samples : 1);
}

int main() {
  mkdir("test/out", 0755);
  mkdir("test/out/analog", 0755);
  char nm[64];

  // ================= BD =================
  printf("BD\n");
  {
    Rig rig(PART_BD);
    Stats s = Hit(rig, "bd_default", 3.0f, 1.0f, 0.25f, 0.45f, 25, 200);
    CHECK(s.f0 > 46 && s.f0 < 57, "bd default f0 %.1f (expect ~51.5)", s.f0);
    float ref_jump = s.maxjump;
    rig.P(BD_SWEEP) = 0; rig.P(BD_ATTACK) = 0; rig.P(BD_DRIVE) = 0;
    rig.P(BD_TUNE) = 0;    s = Hit(rig, "bd_tune_min", 3.0f, 1.0f, 0.05f, 0.4f, 20, 200);
    CHECK(fabsf(s.f0 - 30) < 3, "bd tune min f0 %.1f (expect 30)", s.f0);
    rig.P(BD_TUNE) = 1023; s = Hit(rig, "bd_tune_max", 3.0f, 1.0f, 0.05f, 0.4f, 20, 300);
    CHECK(fabsf(s.f0 - 120) < 8, "bd tune max f0 %.1f (expect 120)", s.f0);
    rig.Defaults();
    rig.P(BD_DECAY) = 0;    s = Hit(rig, "bd_decay_min", 1.0f);
    CHECK(s.t40 < 0.2f, "bd decay min t40 %.3f", s.t40);
    rig.P(BD_DECAY) = 1023; s = Hit(rig, "bd_decay_max", 6.0f);
    CHECK(s.t40 > 2.0f, "bd decay max t40 %.3f", s.t40);
    rig.Defaults();
    rig.P(BD_DRIVE) = 0;    Hit(rig, "bd_drive_min", 3.0f);
    rig.P(BD_DRIVE) = 1023; Hit(rig, "bd_drive_max", 3.0f);
    rig.P(BD_DECAY) = 0;    s = Hit(rig, "bd_drive_max_short", 1.5f);
    rig.Defaults();
    rig.P(BD_SWEEP) = 0;   Hit(rig, "bd_sweep_0", 3.0f);
    rig.P(BD_SWEEP) = 100; rig.P(BD_SWTIME) = 0;   Hit(rig, "bd_sweep_100_slow", 3.0f);
    rig.P(BD_SWEEP) = 100; rig.P(BD_SWTIME) = 100; Hit(rig, "bd_sweep_100_fast", 3.0f);
    rig.Defaults();
    for (int h = 0; h < 4; ++h) { rig.P(BD_HOLD) = (float)h; snprintf(nm, sizeof nm, "bd_hold_%d", h); Hit(rig, nm, 3.0f); }
    rig.Defaults();
    rig.P(BD_ATTACK) = 100;
    for (int t = 0; t < 16; ++t) {
      rig.P(BD_TRANSIENT) = (float)t;
      snprintf(nm, sizeof nm, "bd_tr_%02d_%s", t, kOpts_BD_TRANSIENT[t]);
      Hit(rig, nm, 3.0f);
    }
    rig.P(BD_ATTACK) = 0; Hit(rig, "bd_attack_0", 3.0f);
    rig.Defaults();
    rig.P(BD_CUTOFF) = -100; rig.P(BD_RESON) = 100; Hit(rig, "bd_lp_min_res", 3.0f, 1.0f, 0, 0, 20, 1000, true);
    rig.P(BD_CUTOFF) = -50;  rig.P(BD_RESON) = 100; Hit(rig, "bd_lp_mid_res", 3.0f);
    rig.P(BD_CUTOFF) = 100;  rig.P(BD_RESON) = 100; Hit(rig, "bd_hp_max_res", 3.0f);
    rig.P(BD_CUTOFF) = 30;   rig.P(BD_RESON) = 0;   Hit(rig, "bd_hp_30", 3.0f);
    rig.Defaults();
    Hit(rig, "bd_vel_030", 3.0f, 0.3f);
    // extremes all at once
    rig.P(BD_DRIVE) = 1023; rig.P(BD_SWEEP) = 100; rig.P(BD_SWTIME) = 100; rig.P(BD_ATTACK) = 100; rig.P(BD_HOLD) = 3;
    rig.P(BD_TUNE) = 1023; rig.P(BD_DECAY) = 1023; Hit(rig, "bd_all_max", 8.0f);
    rig.Defaults();
    rig.P(BD_ATTACK) = 0; RetrigTest(rig, "bd_retrig", ref_jump);
    rig.Defaults(); rig.P(BD_DECAY) = 1023; ChokeTest(rig, "bd_choke");
    rig.Defaults(); rig.P(BD_DECAY) = 1023;
    printf("  cost: %.1f ns/sample (x86)\n", TimeRender(rig));
  }

  // ================= SD =================
  printf("SD\n");
  {
    Rig rig(PART_SD);
    rig.P(SD_SNAPLV) = 0;
    Stats s = Hit(rig, "sd_body_default", 3.0f, 1.0f, 0.03f, 0.15f, 150, 450);
    CHECK(s.f0 > 195 && s.f0 < 240, "sd body f0 %.1f (expect ~217)", s.f0);
    rig.P(SD_TONE) = 0;
    rig.P(SD_TUNE) = 0;    s = Hit(rig, "sd_tune_min", 3.0f, 1.0f, 0.03f, 0.2f, 80, 500);
    rig.P(SD_TUNE) = 1023; s = Hit(rig, "sd_tune_max", 3.0f, 1.0f, 0.03f, 0.2f, 80, 900);
    rig.P(SD_TUNE) = 500;
    rig.P(SD_TONE) = 100;  Hit(rig, "sd_tone_100", 3.0f);
    rig.Defaults();
    s = Hit(rig, "sd_default", 3.0f);
    float ref_jump = s.maxjump;
    rig.P(SD_DECAY) = 0; rig.P(SD_SNAPDEC) = 0; s = Hit(rig, "sd_decays_min", 1.0f);
    CHECK(s.t40 < 0.1f, "sd decays min t40 %.3f", s.t40);
    rig.P(SD_DECAY) = 1023; rig.P(SD_SNAPDEC) = 1023; s = Hit(rig, "sd_decays_max", 4.0f);
    CHECK(s.t40 > 0.8f, "sd decays max t40 %.3f", s.t40);
    rig.Defaults();
    rig.P(SD_SNAPLV) = 100;
    for (int t = 0; t < 9; ++t) {
      rig.P(SD_SNAPPY) = (float)t;
      snprintf(nm, sizeof nm, "sd_snap_%d_%s", t, kOpts_SD_SNAPPY[t]);
      for (char* c = nm; *c; ++c) if (*c == ' ' || *c == '-') *c = '_';
      Hit(rig, nm, 3.0f);
    }
    rig.P(SD_SNAPPY) = 0; rig.P(SD_SNAPLV) = 0; Hit(rig, "sd_snaplv_0", 3.0f);
    rig.Defaults();
    rig.P(SD_CUTOFF) = -100; rig.P(SD_RESON) = 100; Hit(rig, "sd_lp_min_res", 3.0f);
    rig.P(SD_CUTOFF) = 100; rig.P(SD_RESON) = 100; Hit(rig, "sd_hp_max_res", 3.0f);
    rig.Defaults();
    Hit(rig, "sd_vel_030", 3.0f, 0.3f);
    RetrigTest(rig, "sd_retrig", ref_jump);
    rig.P(SD_DECAY) = 1023; rig.P(SD_SNAPDEC) = 1023; ChokeTest(rig, "sd_choke");
    rig.P(SD_SNAPPY) = 5;
    printf("  cost (Rattle): %.1f ns/sample (x86)\n", TimeRender(rig));
    rig.P(SD_SNAPPY) = 8;
    printf("  cost (Lo-Fi 3): %.1f ns/sample (x86)\n", TimeRender(rig));
  }

  // ================= LT / HT =================
  for (int w = PART_LT; w <= PART_HT; ++w) {
    const char* k = w == PART_LT ? "lt" : "ht";
    float lo = w == PART_LT ? 55.0f : 100.0f, hi = w == PART_LT ? 220.0f : 400.0f;
    printf("%s\n", w == PART_LT ? "LT" : "HT");
    Rig rig(w);
    rig.P(TOM_ATTACK) = 0; rig.P(TOM_DETUNE) = 0; rig.P(TOM_SWEEP) = 0;
    rig.P(TOM_TUNE) = 0;    snprintf(nm, sizeof nm, "%s_tune_min", k);
    Stats s = Hit(rig, nm, 3.0f, 1.0f, 0.03f, 0.3f, lo * 0.5f, hi * 2);
    CHECK(fabsf(s.f0 / lo - 1) < 0.06f, "%s tune min f0 %.1f (expect %.0f)", k, s.f0, lo);
    rig.P(TOM_TUNE) = 1023; snprintf(nm, sizeof nm, "%s_tune_max", k);
    s = Hit(rig, nm, 3.0f, 1.0f, 0.03f, 0.3f, lo * 0.5f, hi * 2);
    CHECK(fabsf(s.f0 / hi - 1) < 0.06f, "%s tune max f0 %.1f (expect %.0f)", k, s.f0, hi);
    rig.Defaults();
    snprintf(nm, sizeof nm, "%s_default", k); s = Hit(rig, nm, 3.0f, 1.0f, 0.1f, 0.3f, lo * 0.5f, hi * 2);
    float ref_jump = s.maxjump;
    rig.P(TOM_DETUNE) = 1023; snprintf(nm, sizeof nm, "%s_detune_max", k); Hit(rig, nm, 3.0f);
    rig.Defaults();
    rig.P(TOM_DECAY) = 0;    snprintf(nm, sizeof nm, "%s_decay_min", k); s = Hit(rig, nm, 1.0f);
    CHECK(s.t40 < 0.1f, "%s decay min t40 %.3f", k, s.t40);
    rig.P(TOM_DECAY) = 1023; snprintf(nm, sizeof nm, "%s_decay_max", k); s = Hit(rig, nm, 5.0f);
    CHECK(s.t40 > 1.5f, "%s decay max t40 %.3f", k, s.t40);
    rig.Defaults();
    rig.P(TOM_SWEEP) = 0;   snprintf(nm, sizeof nm, "%s_sweep_0", k); Hit(rig, nm, 3.0f);
    rig.P(TOM_SWEEP) = 100; snprintf(nm, sizeof nm, "%s_sweep_100", k); Hit(rig, nm, 3.0f);
    rig.Defaults();
    rig.P(TOM_ATTACK) = 100;
    for (int t = 0; t < 10; ++t) {
      rig.P(TOM_LAYER) = (float)t;
      snprintf(nm, sizeof nm, "%s_layer_%d_%s", k, t, kOpts_LT_LAYER[t]);
      Hit(rig, nm, 3.0f);
    }
    rig.Defaults();
    for (int d = 0; d < 3; ++d) {
      rig.P(TOM_DRIVE) = (float)d;
      snprintf(nm, sizeof nm, "%s_drive_%s", k, kOpts_LT_DRIVE[d]);
      Hit(rig, nm, 3.0f);
    }
    rig.Defaults();
    rig.P(TOM_CUTOFF) = -100; rig.P(TOM_RESON) = 100; snprintf(nm, sizeof nm, "%s_lp_min_res", k); Hit(rig, nm, 3.0f);
    rig.P(TOM_CUTOFF) = 100;  rig.P(TOM_RESON) = 100; snprintf(nm, sizeof nm, "%s_hp_max_res", k); Hit(rig, nm, 3.0f);
    rig.Defaults();
    snprintf(nm, sizeof nm, "%s_vel_030", k); Hit(rig, nm, 3.0f, 0.3f);
    rig.P(TOM_DRIVE) = 2; rig.P(TOM_SWEEP) = 100; rig.P(TOM_ATTACK) = 100; rig.P(TOM_DETUNE) = 1023;
    rig.P(TOM_DECAY) = 1023; rig.P(TOM_TUNE) = 1023; snprintf(nm, sizeof nm, "%s_all_max", k); Hit(rig, nm, 5.0f);
    rig.Defaults();
    rig.P(TOM_ATTACK) = 0; snprintf(nm, sizeof nm, "%s_retrig", k); RetrigTest(rig, nm, ref_jump);
    rig.Defaults(); rig.P(TOM_DECAY) = 1023; snprintf(nm, sizeof nm, "%s_choke", k); ChokeTest(rig, nm);
    rig.P(TOM_DRIVE) = 2;
    printf("  cost (drive High): %.1f ns/sample (x86)\n", TimeRender(rig));
  }

  // parameter-change-while-sounding (zipper / stability): sweep tune and cutoff on a long BD each block
  printf("modulation\n");
  {
    Rig rig(PART_BD);
    rig.P(BD_DECAY) = 1023; rig.P(BD_RESON) = 100;
    float l[kBlock], r[kBlock];
    std::vector<float> x;
    rig.part->Trigger(1.0f, -1);
    for (int b = 0; b < 3000; ++b) {
      rig.P(BD_TUNE) = (float)((b * 7) % 1024);
      rig.P(BD_CUTOFF) = -100.0f + (float)((b * 3) % 201);
      if (!rig.part->Active()) break;
      rig.part->Render(l, r, kBlock);
      x.insert(x.end(), l, l + kBlock);
    }
    WriteWav(std::string(kOut) + "bd_modulated.wav", x);
    CHECK(Finite(x), "modulated: NaN/Inf");
    float pk = 0; for (size_t i = 0; i < x.size(); ++i) pk = fabsf(x[i]) > pk ? fabsf(x[i]) : pk;
    printf("  bd_modulated peak %.3f\n", pk);
    CHECK(pk <= 1.5f, "modulated: peak %.3f", pk);
  }

  printf(g_fail ? "\n%d FAILURES\n" : "\nALL OK\n", g_fail);
  return g_fail ? 1 : 0;
}
