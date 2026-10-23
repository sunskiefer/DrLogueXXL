// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- effects test: every DELAY, REVERB and MASTER type on impulses and a synthesised drum loop
// (test/out/fx/*.wav, 48 kHz float), parameter extremes, rapid type switching, time sweeps while running.
// Checks: no NaN/Inf, bounded output (peak <= 2 even at full feedback), tails decay to silence, EQ Three at 0 dB
// is transparent within 0.1 dB, compressor SIDECH ducks only while the key has energy, filter extremes stable.
// Prints the cost of each type (ns per 48 kHz stereo frame on this machine).
//
//   g++ -O2 -std=gnu++11 -fsanitize=address,undefined -Isrc -Ithird_party/mutable
//       -Ithird_party/community/cldmnky/clouds-revfx src/fx/delay.cc src/fx/reverb.cc src/fx/master.cc
//       test/fx_test.cc -o build/fx_test && build/fx_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <string>
#include <vector>

#include "fx/fx.h"
#include "param_ids.h"

using namespace dlx;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                                         \
  do {                                                        \
    ++g_checks;                                               \
    if (!(c)) {                                               \
      ++g_fail;                                               \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                    \
      printf("\n");                                           \
    }                                                         \
  } while (0)

static const char* kDlyNames[] = {"stereo", "stereo_bpm", "mono", "mono_bpm", "tape", "tape_bpm", "tribal", "cassette"};
static const char* kRevNames[] = {"room", "hall", "space", "riser", "submarine", "clouds", "labirinto", "luce"};
static const char* kMfxNames[] = {"thru", "compressor", "filter", "boost", "eq_three", "omnipress"};

static double Now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t frames = (uint32_t)l.size(), data = frames * 8, sr = 48000, br = sr * 8, riff = 36 + data, fl = 16;
  uint16_t fmt = 3, ch = 2, ba = 8, bits = 32;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
  fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (uint32_t i = 0; i < frames; ++i) { fwrite(&l[i], 4, 1, f); fwrite(&r[i], 4, 1, f); }
  fclose(f);
}

static void Defaults(float* v) {
  for (int i = 0; i < P_COUNT; ++i) v[i] = kParams[i].def;
}

// ---------------------------------------------------------------- test signals (48 kHz)
static uint32_t g_rng = 1;
static float Rnd() { g_rng = g_rng * 1664525u + 1013904223u; return (float)(int32_t)g_rng * (1.0f / 2147483648.0f); }

// a drum loop at 120 bpm: kick on quarters, hats on eighths, a noise-burst snare on 2 and 4, then silence
static void Drums(std::vector<float>& l, std::vector<float>& r, float seconds, float play_seconds, std::vector<float>* key = nullptr) {
  int n = (int)(seconds * 48000), play = (int)(play_seconds * 48000);
  l.assign(n, 0.0f); r.assign(n, 0.0f);
  if (key) key->assign(n, 0.0f);
  const int q = 24000;   // quarter
  float hp = 0.0f;
  for (int s0 = 0; s0 < play; s0 += q / 2) {
    int beat = s0 / q;
    bool on_q = (s0 % q) == 0;
    if (on_q) {   // kick: sine with a pitch drop
      float ph = 0.0f;
      for (int i = 0; i < 12000 && s0 + i < n; ++i) {
        float t = i / 48000.0f;
        float f = 48.0f + 140.0f * expf(-t * 30.0f);
        ph += f / 48000.0f;
        float y = sinf(6.2831853f * ph) * expf(-t * 9.0f) * 0.8f;
        l[s0 + i] += y; r[s0 + i] += y;
        if (key) (*key)[s0 + i] += y;
      }
    }
    // hat: high-passed noise, 40 ms, panned right
    for (int i = 0; i < 2400 && s0 + i < n; ++i) {
      float x = Rnd();
      hp += 0.6f * (x - hp);
      float y = (x - hp) * expf(-i / 300.0f) * 0.25f;
      l[s0 + i] += y * 0.6f; r[s0 + i] += y;
    }
    if (on_q && (beat & 1)) {   // snare: noise + 200 Hz tone, panned left
      for (int i = 0; i < 9000 && s0 + i < n; ++i) {
        float t = i / 48000.0f;
        float y = (Rnd() * 0.5f + 0.4f * sinf(6.2831853f * 200.0f * t)) * expf(-t * 18.0f) * 0.6f;
        l[s0 + i] += y; r[s0 + i] += y * 0.7f;
      }
    }
  }
}

struct Stats {
  float peak = 0.0f;
  bool finite = true;
};
static void Scan(const float* x, int n, Stats* s) {
  for (int i = 0; i < n; ++i) {
    if (!(x[i] == x[i]) || isinf(x[i])) s->finite = false;
    float a = fabsf(x[i]);
    if (a > s->peak) s->peak = a;
  }
}
static float RmsDb(const std::vector<float>& l, const std::vector<float>& r, int from, int to) {
  double e = 0.0;
  for (int i = from; i < to; ++i) e += (double)l[i] * l[i] + (double)r[i] * r[i];
  e /= 2.0 * (to - from > 0 ? to - from : 1);
  return 10.0f * log10f((float)e + 1e-30f);
}

// ---------------------------------------------------------------- generic runners
enum Slot { SL_DLY, SL_REV, SL_MFX };
struct Fx {
  DelayFx dly;
  ReverbFx rev;
  MasterFx mfx;
  void Run(Slot s, const float* il, const float* ir, const float* key, float* ol, float* orr, int n, const float* v, float bpm) {
    if (s == SL_DLY) dly.Process(il, ir, ol, orr, n, v, bpm);
    else if (s == SL_REV) rev.Process(il, ir, ol, orr, n, v);
    else {
      memcpy(ol, il, n * 4); memcpy(orr, ir, n * 4);
      mfx.Process(ol, orr, key, n, v);
    }
  }
  void Clear(Slot s) { if (s == SL_DLY) dly.Clear(); else if (s == SL_REV) rev.Clear(); else mfx.Clear(); }
};

// render a whole buffer through one slot in blocks of `blk`; returns ns per frame
static double Render(Fx& fx, Slot s, const std::vector<float>& il, const std::vector<float>& ir,
                     const std::vector<float>* key, std::vector<float>& ol, std::vector<float>& orr, float* v,
                     float bpm, int blk = 64) {
  int n = (int)il.size();
  ol.assign(n, 0.0f); orr.assign(n, 0.0f);
  double t0 = Now();
  for (int i = 0; i < n; i += blk) {
    int m = n - i < blk ? n - i : blk;
    fx.Run(s, &il[i], &ir[i], key ? &(*key)[i] : nullptr, &ol[i], &orr[i], m, v, bpm);
  }
  return (Now() - t0) * 1e9 / n;
}

// ---------------------------------------------------------------- DELAY
static void TestDelay(Fx& fx, const std::string& dir) {
  printf("-- delay\n");
  float v[P_COUNT];
  for (int t = 0; t < 8; ++t) {
    Defaults(v);
    v[P_DLY_TYPE] = t;
    v[P_DLY_LEVEL] = 100.0f;
    fx.dly.Clear();
    // impulse response, 4 s
    std::vector<float> il(48000 * 4, 0.0f), ir(48000 * 4, 0.0f), ol, orr;
    il[0] = ir[0] = 1.0f;
    Render(fx, SL_DLY, il, ir, nullptr, ol, orr, v, 120.0f);
    WriteWav(dir + "/delay_" + kDlyNames[t] + "_ir.wav", ol, orr);
    Stats st; Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
    CHECK(st.finite && st.peak <= 2.0f, "delay %s IR: finite=%d peak=%.3f", kDlyNames[t], st.finite, st.peak);
    CHECK(st.peak > 0.05f, "delay %s IR silent (peak %.4f)", kDlyNames[t], st.peak);
    // drum loop: 4 s playing + 16 s tail
    fx.dly.Clear();
    Drums(il, ir, 20.0f, 4.0f);
    double ns = Render(fx, SL_DLY, il, ir, nullptr, ol, orr, v, 120.0f);
    std::vector<float> wl(ol.begin(), ol.begin() + 48000 * 8), wr(orr.begin(), orr.begin() + 48000 * 8);
    for (size_t i = 0; i < wl.size(); ++i) { wl[i] = 0.5f * (wl[i] + il[i]); wr[i] = 0.5f * (wr[i] + ir[i]); }
    WriteWav(dir + "/delay_" + kDlyNames[t] + "_drums.wav", wl, wr);
    st = Stats(); Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
    float tail = RmsDb(ol, orr, 48000 * 19, 48000 * 20);
    CHECK(st.finite && st.peak <= 2.0f, "delay %s drums: finite=%d peak=%.3f", kDlyNames[t], st.finite, st.peak);
    CHECK(tail < -100.0f, "delay %s tail does not decay: %.1f dB at 19..20 s", kDlyNames[t], tail);
    printf("  %-11s peak %.3f  tail %.0f dB  cost %.1f ns/frame\n", kDlyNames[t], st.peak, tail, ns);
  }
  // full feedback + saturation + every route/filter: bounded for 30 s of hits
  for (int t = 0; t < 8; ++t)
    for (int route = 0; route < 3; ++route)
      for (int fm = 0; fm < 3; ++fm) {
        Defaults(v);
        v[P_DLY_TYPE] = t; v[P_DLY_FB] = 100.0f; v[P_DLY_LEVEL] = 100.0f; v[P_DLY_ROUTE] = route;
        v[P_DLY_FILTER] = fm; v[P_DLY_CUTOFF] = fm == 1 ? 100.0f : 0.0f; v[P_DLY_SAT] = (route == 1) ? 100.0f : 0.0f;
        v[P_DLY_TIME] = 20.0f; v[P_DLY_DIV] = 0; v[P_DLY_SPREAD] = 100.0f; v[P_DLY_MOD] = 100.0f;
        fx.dly.Clear();
        std::vector<float> il, ir, ol, orr;
        Drums(il, ir, 30.0f, 30.0f);
        for (size_t i = 0; i < il.size(); ++i) { il[i] *= 2.0f; ir[i] *= 2.0f; }   // hot input (+6 dB)
        Render(fx, SL_DLY, il, ir, nullptr, ol, orr, v, 300.0f);
        Stats st; Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
        CHECK(st.finite && st.peak <= 2.0f, "delay %s fb100 route %d filt %d: finite=%d peak=%.3f", kDlyNames[t], route, fm,
              st.finite, st.peak);
        if (route == 0 && fm == 0) {
          // fb 100 %: after the input stops the repeats stay bounded (and may ring for a long time)
          std::vector<float> zl(48000 * 10, 0.0f), zr(48000 * 10, 0.0f);
          Render(fx, SL_DLY, zl, zr, nullptr, ol, orr, v, 300.0f);
          Stats s2; Scan(&ol[0], ol.size(), &s2); Scan(&orr[0], orr.size(), &s2);
          CHECK(s2.finite && s2.peak <= 2.0f, "delay %s fb100 ringing unbounded %.3f", kDlyNames[t], s2.peak);
        }
      }
  // time sweeps while running (each type), random extremes, rapid type switching (with and without Clear)
  {
    std::vector<float> il, ir;
    Drums(il, ir, 20.0f, 20.0f);
    Stats st;
    float ol[64], orr[64];
    float maxstep = 0.0f;
    for (int t = 0; t < 8; ++t) {
      Defaults(v);
      v[P_DLY_TYPE] = t; v[P_DLY_FB] = 70.0f; v[P_DLY_TRACKING] = (t & 1) ? 0.0f : 100.0f;
      fx.dly.Clear();
      float prevl = 0.0f;
      for (int b = 0; b < 48000 * 6 / 64; ++b) {
        float ph = b / 750.0f;
        v[P_DLY_TIME] = 50.0f + 50.0f * sinf(ph * 6.2831853f * 0.7f);
        v[P_DLY_DIV] = (float)((b / 40) % 16);
        v[P_DLY_SPREAD] = 100.0f * sinf(ph * 3.1f);
        v[P_DLY_CUTOFF] = 50.0f + 50.0f * sinf(ph * 5.0f);
        v[P_DLY_FILTER] = (b / 300) % 3;
        fx.dly.Process(&il[b * 64], &ir[b * 64], ol, orr, 64, v, 90.0f + 60.0f * sinf(ph));
        Scan(ol, 64, &st); Scan(orr, 64, &st);
        // continuity across sweeps: no sample-to-sample jump bigger than the dry signal could make
        for (int i = 0; i < 64; ++i) { float d = fabsf(ol[i] - prevl); if (d > maxstep) maxstep = d; prevl = ol[i]; }
      }
    }
    CHECK(st.finite && st.peak <= 2.0f, "delay sweeps: finite=%d peak=%.3f", st.finite, st.peak);
    printf("  time/div/spread/cutoff sweeps on every type: peak %.3f, largest sample step %.3f\n", st.peak, maxstep);
    // click test: a 200 Hz sine, time jumped by large amounts every 100 ms on Stereo (crossfade) and Tape (glide)
    for (int t : {0, 2, 4}) {
      Defaults(v);
      v[P_DLY_TYPE] = t; v[P_DLY_FB] = 0.0f; v[P_DLY_LEVEL] = 100.0f; v[P_DLY_TRACKING] = 0.0f; v[P_DLY_MOD] = 0.0f;
      fx.dly.Clear();
      float in[64], prev = 0.0f, worst = 0.0f;
      double ph = 0.0;
      for (int b = 0; b < 48000 * 4 / 64; ++b) {
        for (int i = 0; i < 64; ++i) { in[i] = 0.5f * (float)sin(ph); ph += 6.2831853 * 200.0 / 48000.0; }
        if (b % 75 == 0) v[P_DLY_TIME] = (b / 75) & 1 ? 85.0f : 55.0f;
        fx.dly.Process(in, in, ol, orr, 64, v, 120.0f);
        for (int i = 0; i < 64; ++i) {
          if (b > 1000) { float d = fabsf(ol[i] - prev); if (d > worst) worst = d; }
          prev = ol[i];
        }
      }
      // a 200 Hz sine at 0.5 changes by at most 0.013 per sample; allow some slack for the crossfade
      CHECK(worst < 0.03f, "delay %s time jump clicks: step %.4f", kDlyNames[t], worst);
      printf("  %-8s time jumps on a 200 Hz sine: largest step %.4f (sine alone 0.013)\n", kDlyNames[t], worst);
    }
    // random extremes + rapid type switching
    Stats s2;
    g_rng = 77;
    for (int b = 0; b < 48000 * 20 / 64; ++b) {
      if (b % 3 == 0) {
        int nt = (int)((Rnd() * 0.5f + 0.5f) * 8.0f) & 7;
        if ((b & 1) && nt != (int)v[P_DLY_TYPE]) fx.dly.Clear();   // the engine clears on a type change
        v[P_DLY_TYPE] = nt;
      }
      for (int p = P_DLY_TIME; p <= P_DLY_LEVEL; ++p) {
        if (Rnd() > 0.7f) {
          const ParamInfo& pi = kParams[p];
          float x = Rnd() > 0.0f ? pi.max : pi.min;
          if (Rnd() > 0.5f) x = pi.min + (pi.max - pi.min) * (0.5f + 0.5f * Rnd());
          if (pi.whole) x = floorf(x + 0.5f);
          v[p] = x;
        }
      }
      fx.dly.Process(&il[(b * 64) % (il.size() - 64)], &ir[(b * 64) % (ir.size() - 64)], ol, orr, 64, v, Rnd() > 0.9f ? 0.0f : 200.0f);
      Scan(ol, 64, &s2); Scan(orr, 64, &s2);
    }
    CHECK(s2.finite && s2.peak <= 2.0f, "delay random extremes/type switching: finite=%d peak=%.3f", s2.finite, s2.peak);
    printf("  random extremes + type switching every 3 blocks: peak %.3f\n", s2.peak);
  }
  char buf[32];
  CHECK(fx.dly.Format(P_DLY_TIME, 0.0f, buf, 32) && !strcmp(buf, "1.0 ms"), "time format 0 -> %s", buf);
  CHECK(fx.dly.Format(P_DLY_TIME, 100.0f, buf, 32) && !strcmp(buf, "2.00 s"), "time format 100 -> %s", buf);
}

// ---------------------------------------------------------------- REVERB
static void TestReverb(Fx& fx, const std::string& dir) {
  printf("-- reverb\n");
  float v[P_COUNT];
  for (int t = 0; t < 8; ++t) {
    Defaults(v);
    v[P_REV_TYPE] = t;
    v[P_REV_LEVEL] = 100.0f;
    fx.rev.Clear();
    std::vector<float> il(48000 * 6, 0.0f), ir(48000 * 6, 0.0f), ol, orr;
    il[0] = ir[0] = 1.0f;
    Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
    WriteWav(dir + "/reverb_" + kRevNames[t] + "_ir.wav", ol, orr);
    Stats st; Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
    CHECK(st.finite && st.peak <= 2.0f && st.peak > 0.005f, "reverb %s IR: finite=%d peak=%.4f", kRevNames[t], st.finite, st.peak);
    // drum loop 4 s + 26 s tail
    fx.rev.Clear();
    Drums(il, ir, 30.0f, 4.0f);
    double ns = Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
    std::vector<float> wl(ol.begin(), ol.begin() + 48000 * 10), wr(orr.begin(), orr.begin() + 48000 * 10);
    for (size_t i = 0; i < wl.size(); ++i) { wl[i] = 0.6f * il[i] + 0.4f * wl[i]; wr[i] = 0.6f * ir[i] + 0.4f * wr[i]; }
    WriteWav(dir + "/reverb_" + kRevNames[t] + "_drums.wav", wl, wr);
    st = Stats(); Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
    float tail = RmsDb(ol, orr, 48000 * 29, 48000 * 30);
    float during = RmsDb(ol, orr, 48000 * 1, 48000 * 4);
    CHECK(st.finite && st.peak <= 2.0f, "reverb %s drums: finite=%d peak=%.3f", kRevNames[t], st.finite, st.peak);
    CHECK(tail < -90.0f, "reverb %s tail does not decay: %.1f dB at 29..30 s", kRevNames[t], tail);
    // rough RT60 from the impulse response envelope (energy in 50 ms windows)
    printf("  %-10s peak %.3f  level %.1f dB  tail %.0f dB  cost %.1f ns/frame\n", kRevNames[t], st.peak, during, tail, ns);
  }
  // extremes: every type at every corner of TIME/INPUT/BRIGHT/RISE/DIVE with hot drums, bounded
  Stats st;
  for (int t = 0; t < 8; ++t)
    for (int corner = 0; corner < 32; ++corner) {
      Defaults(v);
      v[P_REV_TYPE] = t; v[P_REV_LEVEL] = 100.0f;
      v[P_REV_TIME] = corner & 1 ? 100.0f : 0.0f;
      v[P_REV_INPUT] = corner & 2 ? 100.0f : 0.0f;
      v[P_REV_BRIGHT] = corner & 4 ? 100.0f : 0.0f;
      v[P_REV_RISE] = corner & 8 ? 100.0f : 0.0f;
      v[P_REV_DIVE] = corner & 16 ? 100.0f : 0.0f;
      fx.rev.Clear();
      std::vector<float> il, ir, ol, orr;
      Drums(il, ir, 3.0f, 3.0f);
      for (size_t i = 0; i < il.size(); ++i) { il[i] *= 2.0f; ir[i] *= 2.0f; }
      Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
      Stats s1; Scan(&ol[0], ol.size(), &s1); Scan(&orr[0], orr.size(), &s1);
      CHECK(s1.finite && s1.peak <= 2.0f, "reverb %s corner %d: finite=%d peak=%.3f", kRevNames[t], corner, s1.finite, s1.peak);
      if (s1.peak > st.peak) st.peak = s1.peak;
      if (!s1.finite) st.finite = false;
    }
  printf("  32 parameter corners x 8 types, +6 dB drums: worst peak %.3f\n", st.peak);
  // Space at TIME 100 is a near-freeze: it must hold, stay bounded, and fade once TIME comes down
  {
    Defaults(v);
    v[P_REV_TYPE] = 2; v[P_REV_LEVEL] = 100.0f; v[P_REV_TIME] = 100.0f;
    fx.rev.Clear();
    std::vector<float> il, ir, ol, orr;
    Drums(il, ir, 30.0f, 2.0f);
    Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
    float hold = RmsDb(ol, orr, 48000 * 28, 48000 * 30), early = RmsDb(ol, orr, 48000 * 2, 48000 * 4);
    Stats s1; Scan(&ol[0], ol.size(), &s1); Scan(&orr[0], orr.size(), &s1);
    CHECK(s1.finite && s1.peak <= 2.0f && hold > early - 12.0f, "space freeze: peak %.3f, early %.1f, after 28 s %.1f",
          s1.peak, early, hold);
    printf("  space TIME 100 (freeze-ish): %.1f dB at 2-4 s, %.1f dB at 28-30 s\n", early, hold);
    v[P_REV_TIME] = 30.0f;
    std::vector<float> zl(48000 * 30, 0.0f), zr(48000 * 30, 0.0f);
    Render(fx, SL_REV, zl, zr, nullptr, ol, orr, v, 0.0f);
    float tail = RmsDb(ol, orr, 48000 * 29, 48000 * 30);
    CHECK(tail < -90.0f, "space after unfreeze still %.1f dB", tail);
  }
  // rapid type switching + random parameters while playing
  {
    Stats s2;
    std::vector<float> il, ir;
    Drums(il, ir, 20.0f, 20.0f);
    float ol[64], orr[64];
    g_rng = 99;
    Defaults(v);
    for (int b = 0; b < 48000 * 20 / 64; ++b) {
      if (b % 4 == 0) {
        int nt = (int)((Rnd() * 0.5f + 0.5f) * 8.0f) & 7;
        if ((b & 4) && nt != (int)v[P_REV_TYPE]) fx.rev.Clear();
        v[P_REV_TYPE] = nt;
      }
      for (int p = P_REV_TIME; p <= P_REV_LEVEL; ++p)
        if (Rnd() > 0.8f) v[p] = Rnd() > 0.0f ? 100.0f : (Rnd() > 0.0f ? 0.0f : 50.0f + 50.0f * Rnd());
      fx.rev.Process(&il[b * 64], &ir[b * 64], ol, orr, 64, v);
      Scan(ol, 64, &s2); Scan(orr, 64, &s2);
    }
    CHECK(s2.finite && s2.peak <= 2.0f, "reverb random/type switching: finite=%d peak=%.3f", s2.finite, s2.peak);
    printf("  random extremes + type switching every 4 blocks: peak %.3f\n", s2.peak);
  }
  // Riser rises, Submarine dives: spectral centroid of the late tail vs the early tail (a 1 kHz tone burst in)
  for (int t : {3, 4}) {
    Defaults(v);
    v[P_REV_TYPE] = t; v[P_REV_LEVEL] = 100.0f; v[P_REV_TIME] = 70.0f; v[P_REV_RISE] = 80.0f; v[P_REV_DIVE] = 80.0f;
    v[P_REV_BRIGHT] = 100.0f;
    fx.rev.Clear();
    std::vector<float> il(48000 * 4, 0.0f), ir, ol, orr;
    for (int i = 0; i < 4800; ++i) il[i] = 0.5f * sinf(6.2831853f * 1000.0f * i / 48000.0f) * sinf(3.14159f * i / 4800.0f);
    ir = il;
    Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
    // zero-crossing rate as a cheap pitch/brightness estimate
    auto zcr = [&](int a, int b) { int z = 0; for (int i = a + 1; i < b; ++i) z += (ol[i] > 0) != (ol[i - 1] > 0); return z * 48000.0f / (2.0f * (b - a)); };
    float e = zcr(48000 / 4, 48000 / 2), l8 = zcr(48000 * 3 / 2, 48000 * 2);
    printf("  %-10s zero-crossing frequency 0.25-0.5 s: %.0f Hz, 1.5-2 s: %.0f Hz\n", kRevNames[t], e, l8);
    if (t == 3) CHECK(l8 > e * 1.2f, "riser does not rise (%.0f -> %.0f Hz)", e, l8);
    else CHECK(l8 < e * 0.8f, "submarine does not dive (%.0f -> %.0f Hz)", e, l8);
  }
  char buf[32];
  for (int t = 0; t < 8; ++t) {
    Defaults(v); v[P_REV_TYPE] = t;
    float a = 0, b = 0; fx.rev.Clear(); fx.rev.Process(&a, &a, &a, &b, 1, v);
    fx.rev.Format(P_REV_TIME, 0.0f, buf, 32);
    std::string lo = buf;
    fx.rev.Format(P_REV_TIME, 100.0f, buf, 32);
    printf("  %-10s TIME range %s .. %s\n", kRevNames[t], lo.c_str(), buf);
  }
}

// ---------------------------------------------------------------- MASTER
// magnitude response (dB) of a linear impulse response at frequency hz
static float MagDb(const std::vector<float>& h, float hz) {
  double re = 0, im = 0, w = 2.0 * M_PI * hz / 48000.0;
  for (size_t i = 0; i < h.size(); ++i) { re += h[i] * cos(w * i); im -= h[i] * sin(w * i); }
  return (float)(10.0 * log10(re * re + im * im + 1e-30));
}

static void TestMaster(Fx& fx, const std::string& dir) {
  printf("-- master\n");
  float v[P_COUNT];
  for (int t = 0; t < 6; ++t) {
    Defaults(v);
    v[P_MFX_TYPE] = t;
    // make each type audible on the drum loop
    if (t == 1) { v[P_COMP_THRESH] = -24.0f; v[P_COMP_RATIO] = 6.0f; v[P_COMP_MAKEUP] = 6.0f; }
    if (t == 2) { v[P_MF_MODE] = 2; v[P_MF_CUTOFF] = 55.0f; v[P_MF_RESON] = 60.0f; v[P_MF_SATURATE] = 30.0f; }
    if (t == 3) { v[P_BOOST_GAIN] = 9.0f; v[P_BOOST_CENTER] = 25.0f; v[P_BOOST_SATURATE] = 60.0f; }
    if (t == 4) { v[P_EQ_LOW] = 4.0f; v[P_EQ_MID] = -12.0f; v[P_EQ_HIGH] = 3.0f; }
    if (t == 5) { v[P_MFX_U1] = 30.0f; v[P_MFX_U2] = 60.0f; v[P_MFX_U5] = 40.0f; v[P_MFX_U6] = 100.0f; }
    fx.mfx.Clear();
    std::vector<float> il(48000, 0.0f), ir(48000, 0.0f), ol, orr, key;
    il[100] = ir[100] = 0.5f;
    Render(fx, SL_MFX, il, ir, nullptr, ol, orr, v, 0.0f);
    WriteWav(dir + "/master_" + kMfxNames[t] + "_ir.wav", ol, orr);
    fx.mfx.Clear();
    Drums(il, ir, 8.0f, 6.0f, &key);
    double ns = Render(fx, SL_MFX, il, ir, &key, ol, orr, v, 0.0f);
    WriteWav(dir + "/master_" + kMfxNames[t] + "_drums.wav", ol, orr);
    Stats st; Scan(&ol[0], ol.size(), &st); Scan(&orr[0], orr.size(), &st);
    float tail = RmsDb(ol, orr, 48000 * 7, 48000 * 8);
    CHECK(st.finite && st.peak <= 2.0f, "master %s: finite=%d peak=%.3f", kMfxNames[t], st.finite, st.peak);
    CHECK(tail < -100.0f, "master %s: silence in, %.1f dB out", kMfxNames[t], tail);
    printf("  %-11s peak %.3f  cost %.1f ns/frame\n", kMfxNames[t], st.peak, ns);
  }
  // EQ Three: transparent within 0.1 dB at 0 dB everywhere, both slopes, several crossover settings
  {
    float worst = 0.0f;
    for (int slope = 0; slope < 2; ++slope)
      for (int xs = 0; xs < 4; ++xs) {
        static const float kA[4] = {30.0f, 0.0f, 50.0f, 60.0f}, kB[4] = {70.0f, 100.0f, 50.0f, 40.0f};
        Defaults(v);
        v[P_MFX_TYPE] = 4; v[P_EQ_SLOPE] = slope; v[P_EQ_LOWMID] = kA[xs]; v[P_EQ_MIDHIGH] = kB[xs];
        fx.mfx.Clear();
        std::vector<float> il(1 << 15, 0.0f), ir(1 << 15, 0.0f), ol, orr;
        float warm[64] = {0}, wr2[64] = {0};
        for (int k = 0; k < 40; ++k) fx.mfx.Process(warm, wr2, nullptr, 64, v);   // let the crossovers settle
        il[0] = ir[0] = 1.0f;
        Render(fx, SL_MFX, il, ir, nullptr, ol, orr, v, 0.0f);
        for (float hz = 20.0f; hz < 22000.0f; hz *= 1.06f) {
          float m = fabsf(MagDb(ol, hz));
          if (m > worst) worst = m;
        }
      }
    CHECK(worst < 0.1f, "EQ Three at 0 dB not transparent: %.3f dB", worst);
    printf("  EQ Three all 0 dB: worst deviation %.4f dB (20 Hz..22 kHz, both slopes, 4 crossover pairs)\n", worst);
    // and it does something: LOW kill at 24 dB slope removes 50 Hz, keeps 5 kHz
    Defaults(v);
    v[P_MFX_TYPE] = 4; v[P_EQ_LOW] = -64.0f; v[P_EQ_SLOPE] = 1;
    fx.mfx.Clear();
    std::vector<float> il(1 << 15, 0.0f), ir(1 << 15, 0.0f), ol, orr;
    float warm[64] = {0}, wr2[64] = {0};
    for (int k = 0; k < 40; ++k) fx.mfx.Process(warm, wr2, nullptr, 64, v);
    il[0] = ir[0] = 1.0f;
    Render(fx, SL_MFX, il, ir, nullptr, ol, orr, v, 0.0f);
    float m50 = MagDb(ol, 40.0f), m5k = MagDb(ol, 5000.0f);
    CHECK(m50 < -30.0f && fabsf(m5k) < 0.5f, "EQ low kill: 40 Hz %.1f dB, 5 kHz %.2f dB", m50, m5k);
    printf("  EQ LOW kill (24 dB): 40 Hz %.1f dB, 5 kHz %.2f dB\n", m50, m5k);
  }
  // Compressor sidechain: a steady 200 Hz tone on the main bus; the key is silent for 1 s, then kicks for 1 s,
  // then silent again. SIDECH On ducks only while the key has energy; SIDECH Off with the same key does nothing
  // (the tone sits under the threshold).
  for (int sc = 0; sc < 2; ++sc) {
    Defaults(v);
    v[P_MFX_TYPE] = 1; v[P_COMP_THRESH] = -30.0f; v[P_COMP_RATIO] = 10.0f; v[P_COMP_ATTACK] = 1.0f;
    v[P_COMP_RELEASE] = 100.0f; v[P_COMP_KNEE] = 0.0f; v[P_COMP_SIDECH] = sc; v[P_COMP_WET] = 100.0f;
    v[P_COMP_MAKEUP] = 0.0f;
    fx.mfx.Clear();
    int n = 48000 * 3;
    std::vector<float> il(n), ir(n), key(n, 0.0f), ol, orr;
    for (int i = 0; i < n; ++i) il[i] = ir[i] = 0.02f * sinf(6.2831853f * 200.0f * i / 48000.0f);   // -34 dBFS
    for (int s0 = 48000; s0 < 96000; s0 += 12000)
      for (int i = 0; i < 6000; ++i) key[s0 + i] = 0.9f * sinf(6.2831853f * 60.0f * i / 48000.0f) * expf(-i / 2000.0f);
    Render(fx, SL_MFX, il, ir, &key, ol, orr, v, 0.0f);
    float in_db = RmsDb(il, ir, 0, 48000);
    float quiet1 = RmsDb(ol, orr, 24000, 48000) - in_db;
    float duck = RmsDb(ol, orr, 48000, 96000) - in_db;
    float quiet2 = RmsDb(ol, orr, 120000, 144000) - in_db;
    printf("  compressor SIDECH %s: before key %.2f dB, during kicks %.2f dB, after %.2f dB\n", sc ? "On " : "Off",
           quiet1, duck, quiet2);
    CHECK(fabsf(quiet1) < 0.1f && fabsf(quiet2) < 0.1f, "sidechain %d: gain change without key (%.2f / %.2f dB)", sc,
          quiet1, quiet2);
    if (sc) CHECK(duck < -6.0f, "sidechain On does not duck (%.2f dB)", duck);
    else CHECK(fabsf(duck) < 0.1f, "sidechain Off reacts to the key (%.2f dB)", duck);
  }
  // compressor on the main bus: a loud tone gets reduced by about (level - thr) * (1 - 1/ratio)
  {
    Defaults(v);
    v[P_MFX_TYPE] = 1; v[P_COMP_THRESH] = -20.0f; v[P_COMP_RATIO] = 4.0f; v[P_COMP_KNEE] = 0.0f; v[P_COMP_SIDECH] = 0;
    fx.mfx.Clear();
    std::vector<float> il(48000), ir(48000), ol, orr;
    for (int i = 0; i < 48000; ++i) il[i] = ir[i] = 0.5f * (i & 64 ? 1.0f : -1.0f);   // square at -6 dBFS peak
    Render(fx, SL_MFX, il, ir, nullptr, ol, orr, v, 0.0f);
    float g = RmsDb(ol, orr, 24000, 48000) - RmsDb(il, ir, 24000, 48000);
    CHECK(fabsf(g - (-10.5f)) < 0.5f, "compressor static curve: %.2f dB (expected -10.5)", g);
    printf("  compressor -6 dB square, thr -20, 4:1: %.2f dB (expected -10.5), meter %.2f dB\n", g, fx.mfx.GainReductionDb());
  }
  // Filter extremes: every mode, cutoff 0/100 and fast sweeps, reson 0/100, saturate 0/100 -> stable, decays
  {
    Stats st;
    float worst_tail = -300.0f;
    for (int mode = 0; mode < 7; ++mode)
      for (int c = 0; c < 8; ++c) {
        Defaults(v);
        v[P_MFX_TYPE] = 2; v[P_MF_MODE] = mode;
        v[P_MF_CUTOFF] = c & 1 ? 100.0f : 0.0f; v[P_MF_RESON] = c & 2 ? 100.0f : 0.0f; v[P_MF_SATURATE] = c & 4 ? 100.0f : 0.0f;
        fx.mfx.Clear();
        std::vector<float> il, ir, key;
        Drums(il, ir, 5.0f, 3.0f, &key);
        float ol[64], orr[64];
        std::vector<float> outl, outr;
        for (size_t b = 0; b + 64 <= il.size(); b += 64) {
          if (b > 48000 && b < 48000 * 3) v[P_MF_CUTOFF] = 50.0f + 50.0f * sinf((float)b * 0.0003f);   // fast sweep
          memcpy(ol, &il[b], 256); memcpy(orr, &ir[b], 256);
          fx.mfx.Process(ol, orr, &key[b], 64, v);
          Scan(ol, 64, &st); Scan(orr, 64, &st);
          outl.insert(outl.end(), ol, ol + 64); outr.insert(outr.end(), orr, orr + 64);
        }
        if (mode == 2 && c == 3) WriteWav(dir + "/master_filter_lpf4_reso_sweep.wav", outl, outr);
        float tail = RmsDb(outl, outr, 48000 * 4, (int)outl.size());
        if (tail > worst_tail) worst_tail = tail;
      }
    CHECK(st.finite && st.peak <= 2.0f, "filter extremes: finite=%d peak=%.3f", st.finite, st.peak);
    CHECK(worst_tail < -100.0f, "filter extremes: tail %.1f dB", worst_tail);
    printf("  filter 7 modes x cutoff/reson/saturate extremes + sweeps: peak %.3f, worst tail %.0f dB\n", st.peak, worst_tail);
  }
  // Boost and Omnipress extremes, rapid type switching
  {
    Stats st;
    std::vector<float> il, ir, key;
    Drums(il, ir, 20.0f, 20.0f, &key);
    float ol[64], orr[64];
    g_rng = 5;
    Defaults(v);
    for (size_t b = 0; b + 64 <= il.size(); b += 64) {
      if ((b / 64) % 3 == 0) {
        int nt = (int)((Rnd() * 0.5f + 0.5f) * 6.0f) % 6;
        if (((b / 64) & 1) && nt != (int)v[P_MFX_TYPE]) fx.mfx.Clear();
        v[P_MFX_TYPE] = nt;
      }
      for (int p = P_COMP_THRESH; p <= P_MFX_U6; ++p)
        if (Rnd() > 0.85f) {
          const ParamInfo& pi = kParams[p];
          float x = Rnd() > 0.0f ? pi.max : pi.min;
          if (pi.whole) x = floorf(x + 0.5f);
          v[p] = x;
        }
      memcpy(ol, &il[b], 256); memcpy(orr, &ir[b], 256);
      fx.mfx.Process(ol, orr, &key[b], 64, v);
      Scan(ol, 64, &st); Scan(orr, 64, &st);
    }
    // master gains can legitimately add up to +12 dB (EQ band + output gain, compressor makeup, boost): the bound
    // is the input peak raised by 12 dB, plus the boost/resonance overshoot margin
    Stats si; Scan(&il[0], il.size(), &si); Scan(&ir[0], ir.size(), &si);
    CHECK(st.finite && st.peak <= si.peak * 4.0f + 0.5f, "master random extremes: finite=%d peak=%.3f (input %.3f)", st.finite, st.peak, si.peak);
    printf("  random extremes + type switching every 3 blocks: peak %.3f\n", st.peak);
  }
  // slot names
  Defaults(v);
  v[P_MFX_TYPE] = 5;
  CHECK(fx.mfx.SlotName(0, v) && !strcmp(fx.mfx.SlotName(0, v), "THRESH"), "omnipress slot 1 name");
  CHECK(fx.mfx.SlotName(5, v) && !strcmp(fx.mfx.SlotName(5, v), "MIX"), "omnipress slot 6 name");
  v[P_MFX_TYPE] = 1;
  CHECK(fx.mfx.SlotName(0, v) == NULL, "compressor has no generic slots");
}

// cost of each type at its heaviest common setting, on the drum loop (no sanitizer build gives real numbers)
static void Cost(Fx& fx) {
  printf("-- cost (ns per 48 kHz stereo frame, heaviest settings)\n");
  float v[P_COUNT];
  std::vector<float> il, ir, key, ol, orr;
  Drums(il, ir, 10.0f, 10.0f, &key);
  for (int t = 0; t < 8; ++t) {
    Defaults(v); v[P_DLY_TYPE] = t; v[P_DLY_FILTER] = 1; v[P_DLY_SAT] = 50.0f; v[P_DLY_ROUTE] = 1; v[P_DLY_TRACKING] = 100.0f;
    fx.dly.Clear();
    double ns = Render(fx, SL_DLY, il, ir, nullptr, ol, orr, v, 120.0f);
    printf("  delay  %-11s %6.1f\n", kDlyNames[t], ns);
  }
  for (int t = 0; t < 8; ++t) {
    Defaults(v); v[P_REV_TYPE] = t; v[P_REV_RISE] = 80.0f; v[P_REV_DIVE] = 80.0f; v[P_REV_BRIGHT] = 80.0f;
    fx.rev.Clear();
    double ns = Render(fx, SL_REV, il, ir, nullptr, ol, orr, v, 0.0f);
    printf("  reverb %-11s %6.1f\n", kRevNames[t], ns);
  }
  for (int t = 0; t < 6; ++t) {
    Defaults(v); v[P_MFX_TYPE] = t; v[P_MF_MODE] = 4; v[P_MF_SATURATE] = 50.0f; v[P_BOOST_SATURATE] = 50.0f;
    v[P_EQ_SLOPE] = 1; v[P_MFX_U5] = 50.0f;
    fx.mfx.Clear();
    double ns = Render(fx, SL_MFX, il, ir, &key, ol, orr, v, 0.0f);
    printf("  master %-11s %6.1f\n", kMfxNames[t], ns);
  }
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "test/out/fx";
  mkdir("test/out", 0755);
  mkdir(dir.c_str(), 0755);
  Fx* fx = new Fx;
  // "one <slot 0..2> <type>": render 4 s of drums through one type at its heaviest settings and exit (for
  // instruction counting with valgrind --tool=callgrind --toggle-collect='dlx::*Process*')
  if (argc > 4 && !strcmp(argv[2], "one")) {
    int sl = atoi(argv[3]), t = atoi(argv[4]);
    float v[P_COUNT];
    Defaults(v);
    v[P_DLY_TYPE] = t; v[P_DLY_FILTER] = 1; v[P_DLY_SAT] = 50.0f; v[P_DLY_ROUTE] = 1; v[P_DLY_TRACKING] = 100.0f;
    v[P_REV_TYPE] = t; v[P_REV_RISE] = 80.0f; v[P_REV_DIVE] = 80.0f; v[P_REV_BRIGHT] = 80.0f;
    v[P_MFX_TYPE] = t; v[P_MF_MODE] = 4; v[P_MF_SATURATE] = 50.0f; v[P_BOOST_SATURATE] = 50.0f; v[P_EQ_SLOPE] = 1;
    v[P_MFX_U5] = 50.0f;
    std::vector<float> il, ir, key, ol, orr;
    float secs = argc > 5 ? (float)atof(argv[5]) : 4.0f;
    Drums(il, ir, secs, secs, &key);
    if (sl < 3) Render(*fx, (Slot)sl, il, ir, &key, ol, orr, v, 120.0f);   // slot 3: baseline (signal only)
    delete fx;
    return 0;
  }
  bool cost_only = argc > 2 && !strcmp(argv[2], "cost");
  if (!cost_only) {
    TestDelay(*fx, dir);
    TestReverb(*fx, dir);
    TestMaster(*fx, dir);
  }
  Cost(*fx);
  delete fx;
  if (!cost_only) printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
