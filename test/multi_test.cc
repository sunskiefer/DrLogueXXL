// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- MULTI part test: VPM (all presets, ratio extremes, EG types), NOISE (all algorithms) and every
// registered synth unit (defaults, notes, pad hits, every slot swept over its range, engine switches while notes
// ring, two instances side by side), with per-unit CPU cost. WAVs go to test/out/multi/.
//
//   g++ -O2 -std=gnu++11 -fsanitize=address,undefined -Isrc -Ithird_party/logue-sdk/platform/drumlogue/common
//       test/multi_test.cc src/parts/multi.cc src/units/*.cc
//       third_party/community/boochow/Maximilian/src/maximilian.cpp
//       third_party/community/boochow/Maximilian/src/libs/PolyBLEP/PolyBLEP.cpp -o multi_test && ./multi_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <chrono>
#include <string>
#include <vector>

#include "param_ids.h"
#include "parts/multi.h"
#include "units/unit_host.h"

using namespace dlx;

static int g_fail = 0;
#if defined(__arm__)
static const char kArch[] = "armv7, emulated: relative only";
#else
static const char kArch[] = "host";
#endif
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

struct Inst {
  float v[P_COUNT];
  MultiPart* m;
  explicit Inst(uint32_t seed) {
    for (int i = 0; i < P_COUNT; ++i) v[i] = kParams[i].def;
    m = NewMulti();
    m->Init(&v[P_MT_ENGINE], PART_MT, seed);
    float d[kNumUnitSlots];
    m->EngineChanged((int)v[P_MT_ENGINE], d);
  }
  ~Inst() { delete m; }
  int Engine(int e) {
    v[P_MT_ENGINE] = (float)e;
    float d[kNumUnitSlots];
    int n = m->EngineChanged(e, d);
    for (int i = 0; i < kNumUnitSlots; ++i) v[P_U_1 + i] = d[i];
    return n;
  }
};

struct Rec {
  std::vector<float> l, r;
  float peak = 0.0f;
  bool bad = false;
};

// render `blocks` 64-frame blocks (only while Active(), like the engine), append to rec
static void Run(Inst& in, Rec& rec, int blocks) {
  float l[kBlock], r[kBlock];
  for (int b = 0; b < blocks; ++b) {
    if (!in.m->Active()) {
      for (int i = 0; i < kBlock; ++i) { rec.l.push_back(0); rec.r.push_back(0); }
      continue;
    }
    int ch = in.m->Render(l, r, kBlock);
    if (ch == 1) memcpy(r, l, sizeof l);
    for (int i = 0; i < kBlock; ++i) {
      if (!std::isfinite(l[i]) || !std::isfinite(r[i])) rec.bad = true;
      float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
      if (a > rec.peak) rec.peak = a;
      rec.l.push_back(l[i]); rec.r.push_back(r[i]);
    }
  }
}

static float Peak(const Rec& rec, size_t from, size_t to) {
  float p = 0;
  for (size_t i = from; i < to && i < rec.l.size(); ++i) {
    p = fmaxf(p, fabsf(rec.l[i])); p = fmaxf(p, fabsf(rec.r[i]));
  }
  return p;
}

static void WriteWav(const std::string& name, const Rec& rec) {
  std::string path = "test/out/multi/" + name + ".wav";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t n = (uint32_t)rec.l.size(), bytes = n * 4;
  auto u32 = [&](uint32_t x) { fwrite(&x, 4, 1, f); };
  auto u16 = [&](uint16_t x) { fwrite(&x, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(bytes);
  for (uint32_t i = 0; i < n; ++i) {
    int16_t a = (int16_t)lrintf(fmaxf(-1.f, fminf(1.f, rec.l[i])) * 32767.f);
    int16_t b = (int16_t)lrintf(fmaxf(-1.f, fminf(1.f, rec.r[i])) * 32767.f);
    fwrite(&a, 2, 1, f); fwrite(&b, 2, 1, f);
  }
  fclose(f);
}

// note on, hold, off, wait for the tail; returns false if it never went inactive
static bool WaitIdle(Inst& in, Rec& rec, int max_blocks) {
  for (int b = 0; b < max_blocks; ++b) {
    if (!in.m->Active()) return true;
    Run(in, rec, 1);
  }
  return !in.m->Active();
}

static const int kBlocksPerSec = 48000 / kBlock;   // 750

// ---------------------------------------------------------------------------------------------------------------
static void TestVpm() {
  for (int preset = 0; preset < 8; ++preset) {
    for (int eg = 0; eg < 3; ++eg) {
      Inst in(1234 + preset);
      in.Engine(0);
      in.v[P_VPM_PRESET] = (float)preset;
      in.v[P_VPM_EG] = (float)eg;
      in.v[P_VPM_INDEX] = 60;
      in.v[P_VPM_RELEASE] = 30;
      Rec rec;
      in.m->Trigger(1.0f, -1);          // pad hit, vpm_note
      Run(in, rec, 300);
      size_t mark = rec.l.size();
      float on_peak = Peak(rec, 0, mark);
      in.m->Release(-1);
      in.m->Trigger(0.8f, 72);          // chromatic
      Run(in, rec, 200);
      in.m->Release(72);
      bool idle = WaitIdle(in, rec, 6 * kBlocksPerSec);
      char name[64];
      snprintf(name, sizeof name, "vpm_%s_%s", kOpts_VPM_PRESET[preset], kOpts_VPM_EG[eg]);
      for (char* c = name; *c; ++c) if (*c == '.' || *c == ' ') *c = '_';
      if (eg == 0) WriteWav(name, rec);
      CHECK(!rec.bad, "%s: NaN/Inf", name);
      CHECK(on_peak > 0.05f, "%s: no sound after note-on (peak %.4f)", name, on_peak);
      CHECK(rec.peak <= 2.0f, "%s: peak %.2f", name, rec.peak);
      CHECK(idle, "%s: still active after release", name);
    }
  }
  // ratio extremes, index and idx-mod extremes, noise
  const int ratios[] = {0, 3, 18};
  for (int ri : ratios)
    for (float idx : {0.0f, 100.0f})
      for (float im : {-100.0f, 100.0f}) {
        Inst in(77);
        in.Engine(0);
        in.v[P_VPM_RATIO] = (float)ri; in.v[P_VPM_INDEX] = idx; in.v[P_VPM_IDXMOD] = im; in.v[P_VPM_NOISE] = 100;
        in.v[P_VPM_ATTACK] = 100; in.v[P_VPM_RELEASE] = 0;
        Rec rec;
        in.m->Trigger(1.0f, 108);
        Run(in, rec, 200);
        in.v[P_VPM_ATTACK] = 0;
        in.m->Trigger(1.0f, 12);
        Run(in, rec, 200);
        CHECK(!rec.bad && rec.peak <= 2.0f, "vpm ratio %d idx %.0f im %.0f: bad/peak %.2f", ri, idx, im, rec.peak);
        CHECK(rec.peak > 0.01f, "vpm ratio %d idx %.0f im %.0f: silent", ri, idx, im);
      }
  // display text
  Inst in(5);
  char buf[32];
  CHECK(in.m->Format(P_VPM_ATTACK - P_MT_ENGINE, 0, buf, sizeof buf) && !strcmp(buf, "1ms"), "attack 0 -> %s", buf);
  CHECK(in.m->Format(P_VPM_RELEASE - P_MT_ENGINE, 100, buf, sizeof buf) && !strcmp(buf, "5.00s"), "release 100 -> %s", buf);
  printf("VPM: 8 presets x 3 EG types, ratio/index extremes ok\n");
}

static void TestNoise() {
  for (int a = 0; a < 7; ++a) {
    Rec rec;
    for (float color : {0.0f, 50.0f, 100.0f})
      for (float peak : {0.0f, 100.0f}) {
        Inst in(99 + a);
        in.Engine(1);
        in.v[P_NOISE_ALGO] = (float)a; in.v[P_NOISE_COLOR] = color; in.v[P_NOISE_PEAK] = peak;
        in.v[P_NOISE_RELEASE] = 20;
        size_t s0 = rec.l.size();
        in.m->Trigger(1.0f, -1);
        Run(in, rec, 60);
        float p = Peak(rec, s0, rec.l.size());
        bool idle = WaitIdle(in, rec, 4 * kBlocksPerSec);
        CHECK(p > 0.002f, "noise %s color %.0f peak %.0f: silent (%.5f)", kOpts_NOISE_ALGO[a], color, peak, p);
        CHECK(idle, "noise %s: stays active", kOpts_NOISE_ALGO[a]);
      }
    char name[64];
    snprintf(name, sizeof name, "noise_%s", kOpts_NOISE_ALGO[a]);
    for (char* c = name; *c; ++c) if (*c == ' ') *c = '_';
    WriteWav(name, rec);
    CHECK(!rec.bad, "%s: NaN/Inf", name);
    CHECK(rec.peak <= 2.0f, "%s: peak %.2f", name, rec.peak);
  }
  printf("NOISE: 7 algorithms x colour/peak extremes ok\n");
}

// ---------------------------------------------------------------------------------------------------------------
static double CostNsPerFrame(int unit) {
  Inst in(4242);
  in.Engine(kNumBuiltinEngines + unit);
  float l[kBlock], r[kBlock];
  double ns = 0;
  int frames = 0;
  for (int rep = 0; rep < 8; ++rep) {   // a typical playing: a note every 0.5 s, held 0.25 s
    in.m->Trigger(0.9f, 48 + (rep * 5) % 24);
    for (int b = 0; b < kBlocksPerSec / 2; ++b) {
      if (b == kBlocksPerSec / 4) in.m->Release(48 + (rep * 5) % 24);
      auto t0 = std::chrono::steady_clock::now();
      in.m->Render(l, r, kBlock);
      auto t1 = std::chrono::steady_clock::now();
      ns += std::chrono::duration<double, std::nano>(t1 - t0).count();
      frames += kBlock;
    }
  }
  return ns / frames;
}

static void TestUnits(bool bench) {
  // registry order agrees with the mt_engine option list (registry.txt -> gen_params.py)
  CHECK(NumSynthUnits() == kNumUnits, "registry has %d units, params %d", NumSynthUnits(), kNumUnits);
  for (int i = 0; i < NumSynthUnits() && i < kNumUnits; ++i)
    CHECK(!strcmp(SynthUnitName(i), kOpts_MT_ENGINE[kNumBuiltinEngines + i]), "unit %d: %s vs %s", i,
          SynthUnitName(i), kOpts_MT_ENGINE[kNumBuiltinEngines + i]);

  for (int u = 0; u < NumSynthUnits(); ++u) {
    const char* name = SynthUnitName(u);
    Inst in(1000 + u);
    int used = in.Engine(kNumBuiltinEngines + u);
    CHECK(used > 0, "%s: no slots", name);
    // names and value text of every slot
    char nb[32], vb[32];
    int named = 0;
    for (int s = 0; s < kNumUnitSlots; ++s) {
      if (in.m->SlotName(s, nb, sizeof nb)) {
        ++named;
        CHECK(in.m->SlotDisplay(s, in.v[P_U_1 + s], vb, sizeof vb) && vb[0], "%s slot %d (%s): no value text", name, s, nb);
      }
    }
    CHECK(named > 0, "%s: no named slots", name);

    Rec rec;
    // pad hit at the unit note, then a few chromatic notes
    in.m->Trigger(1.0f, -1);
    Run(in, rec, 120);
    float pad_peak = rec.peak;
    in.m->Release(-1);
    Run(in, rec, 60);
    for (int k = 0; k < 4; ++k) {
      int n = 48 + k * 7;
      in.m->Trigger(0.8f, n);
      Run(in, rec, 90);
      in.m->Release(n);
      Run(in, rec, 30);
    }
    CHECK(pad_peak > 0.01f || rec.peak > 0.01f, "%s: no sound after note-on (pad %.4f, all %.4f)", name, pad_peak, rec.peak);
    CHECK(pad_peak > 0.003f, "%s: pad hit silent (%.5f)", name, pad_peak);
    bool idle = WaitIdle(in, rec, 15 * kBlocksPerSec);
    CHECK(idle, "%s: Active() still true 15 s after note-off", name);
    WriteWav(std::string("unit_") + name, rec);

    // sweep every slot across its range while a note rings (and back to the default)
    Rec sw;
    for (int s = 0; s < kNumUnitSlots; ++s) {
      if (!in.m->SlotName(s, nb, sizeof nb)) continue;
      float def = in.v[P_U_1 + s];
      in.m->Trigger(1.0f, 55);
      for (int step = 0; step <= 8; ++step) {
        in.v[P_U_1 + s] = step / 8.0f;
        in.m->SlotDisplay(s, in.v[P_U_1 + s], vb, sizeof vb);
        Run(in, sw, 6);
      }
      in.m->Release(55);
      Run(in, sw, 10);
      in.v[P_U_1 + s] = def;
    }
    CHECK(!sw.bad, "%s: NaN/Inf in slot sweep", name);
    CHECK(sw.peak <= 2.0f, "%s: slot sweep peak %.2f", name, sw.peak);
    // presets
    for (int pi = 0; pi < in.m->NumPresets(); ++pi) {
      float sv[kNumUnitSlots];
      CHECK(in.m->PresetName(pi) != nullptr, "%s: preset %d has no name", name, pi);
      in.m->LoadPreset(pi, sv);
      for (int s = 0; s < kNumUnitSlots; ++s) in.v[P_U_1 + s] = sv[s];
      in.m->Trigger(1.0f, -1);
      Run(in, sw, 40);
      in.m->Release(-1);
      Run(in, sw, 20);
    }
    CHECK(!sw.bad, "%s: NaN/Inf with presets", name);
    in.m->SetTempo(133.5f);
    in.m->Choke();
    Run(in, sw, 4);
    CHECK(!in.m->Active(), "%s: still active after Choke + 4 blocks", name);
    CHECK(!rec.bad, "%s: NaN/Inf", name);
    CHECK(rec.peak <= 2.0f, "%s: peak %.2f > 2", name, rec.peak);
    printf("unit %-11s slots %2d presets %2d  peak %.3f  pad %.3f", name, named, in.m->NumPresets(), rec.peak, pad_peak);
    if (bench) printf("  %7.1f ns/frame (%s)", CostNsPerFrame(u), kArch);
    printf("\n");
  }
}

// switching engines while notes ring, through every engine, both directions
static void TestSwitching() {
  Inst in(31337);
  Rec rec;
  int total = kNumBuiltinEngines + NumSynthUnits();
  for (int pass = 0; pass < 2; ++pass)
    for (int k = 0; k < total; ++k) {
      int e = pass == 0 ? k : total - 1 - k;
      in.m->Trigger(1.0f, -1);
      in.m->Trigger(0.7f, 60 + k);
      Run(in, rec, 20);
      in.m->Choke();          // what the engine does on an engine change
      in.Engine(e);
      in.m->Trigger(1.0f, 64);
      Run(in, rec, 20);
      in.Engine((e + 3) % total);   // switch without choke, note held
      Run(in, rec, 10);
      in.m->Release(64);
    }
  CHECK(!rec.bad, "switching: NaN/Inf");
  CHECK(rec.peak <= 2.0f, "switching: peak %.2f", rec.peak);
  WriteWav("switching", rec);
  printf("engine switching while notes ring: ok (peak %.3f)\n", rec.peak);
}

// two instances side by side on different units, then on the SAME unit with different settings: each renders the
// same as when alone (proves no shared mutable globals between instances)
static void TestTwoInstances() {
  int nu = NumSynthUnits();
  for (int u = 0; u < nu; ++u) {
    int other = (u + 1) % nu;
    // reference: unit u alone
    Rec ref;
    {
      Inst a(7);
      a.Engine(kNumBuiltinEngines + u);
      a.m->Trigger(1.0f, 57);
      Run(a, ref, 100);
    }
    // the same, with a second instance (same unit, different note + other unit) interleaved block by block
    Rec got, b_rec, c_rec;
    {
      Inst a(7), b(8), c(9);
      a.Engine(kNumBuiltinEngines + u);
      b.Engine(kNumBuiltinEngines + u);
      c.Engine(kNumBuiltinEngines + other);
      for (int s = 0; s < kNumUnitSlots; ++s) b.v[P_U_1 + s] = 1.0f - b.v[P_U_1 + s] * 0.5f;
      b.m->Trigger(0.5f, 40);
      c.m->Trigger(1.0f, 70);
      a.m->Trigger(1.0f, 57);
      for (int k = 0; k < 100; ++k) {
        Run(b, b_rec, 1);
        Run(a, got, 1);
        Run(c, c_rec, 1);
      }
    }
    float maxdiff = 0;
    for (size_t i = 0; i < ref.l.size() && i < got.l.size(); ++i)
      maxdiff = fmaxf(maxdiff, fmaxf(fabsf(ref.l[i] - got.l[i]), fabsf(ref.r[i] - got.r[i])));
    CHECK(maxdiff < 1e-5f, "%s: output differs with other instances running (max diff %.6f)", SynthUnitName(u), maxdiff);
    CHECK(!b_rec.bad && !c_rec.bad, "%s: NaN in parallel instances", SynthUnitName(u));
  }
  printf("instances side by side: every unit renders identically with other instances running\n");
}

int main(int argc, char** argv) {
  bool bench = !(argc > 1 && !strcmp(argv[1], "--no-bench"));
  mkdir("test/out", 0755);
  mkdir("test/out/multi", 0755);
  TestVpm();
  TestNoise();
  TestUnits(bench);
  TestSwitching();
  TestTwoInstances();
  if (bench) {
    // VPM / NOISE cost for reference
    for (int e = 0; e < 2; ++e) {
      Inst in(1);
      in.Engine(e);
      float l[kBlock], r[kBlock];
      in.v[P_VPM_EG] = 1;
      in.m->Trigger(1.0f, -1);
      auto t0 = std::chrono::steady_clock::now();
      for (int b = 0; b < kBlocksPerSec; ++b) in.m->Render(l, r, kBlock);
      auto t1 = std::chrono::steady_clock::now();
      printf("%-5s %7.1f ns/frame (%s)\n", e ? "NOISE" : "VPM",
             std::chrono::duration<double, std::nano>(t1 - t0).count() / (kBlocksPerSec * kBlock), kArch);
    }
  }
  printf(g_fail ? "multi_test: %d FAILURES\n" : "multi_test: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
