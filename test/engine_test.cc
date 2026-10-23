// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- whole-engine test: plays the engine the way MPC does (44.1 kHz, 128-frame int16 blocks, MIDI at block
// starts) and checks what a user would hear.
//
//   test/run_tests.sh            (x86, ASan/UBSan)  -- or build by hand:
//   g++ -O2 -std=gnu++11 -Isrc -Ithird_party/logue-sdk/platform/drumlogue/common <sources> test/engine_test.cc -lpthread
//
// Writes test/out/engine/*.wav: a groove on every part, each part alone, every delay/reverb/master type, every MULTI
// engine. Checks: no NaN, no int16 clipping run, every part audible, choke groups, mute, state round trip (values and
// user-sample names), two instances, engine switch resets the unit slots, a full random parameter sweep while playing
// (stable, no NaN), and the render cost per block.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "dlx_engine.h"
#include "param_ids.h"
#include "rom/sample_rom.h"

using namespace dlx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void WriteWav(const std::string& path, const std::vector<int16_t>& lr) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t data = (uint32_t)lr.size() * 2, sr = 44100, br = sr * 4, riff = 36 + data;
  uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
  uint32_t fl = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
  fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
  fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(lr.data(), 2, lr.size(), f);
  fclose(f);
}

struct Stats { double peak = 0, rms = 0; int clipped = 0; bool nan = false; };

// render `blocks` blocks; `at(block)` may send MIDI before each block
template <class F>
static Stats Play(Engine& e, int blocks, F at, std::vector<int16_t>* rec = nullptr) {
  Stats s;
  int16_t buf[256];
  float fl[128], fr[128];
  double sum = 0;
  long n = 0;
  int run = 0;
  for (int b = 0; b < blocks; ++b) {
    at(b);
    if (rec) {
      e.Render(buf, 128);
      for (int i = 0; i < 256; ++i) {
        double x = buf[i] / 32768.0;
        sum += x * x; ++n;
        if (fabs(x) > s.peak) s.peak = fabs(x);
        if (abs(buf[i]) >= 32767) { if (++run > 4) ++s.clipped; } else run = 0;
      }
      rec->insert(rec->end(), buf, buf + 256);
    } else {
      e.RenderFloat(fl, fr, 128);
      for (int i = 0; i < 128; ++i) {
        if (fl[i] != fl[i] || fr[i] != fr[i] || isinf(fl[i]) || isinf(fr[i])) s.nan = true;
        double m = fmax(fabs(fl[i]), fabs(fr[i]));
        if (m > s.peak) s.peak = m;
        sum += fl[i] * fl[i]; ++n;
      }
    }
  }
  s.rms = sqrt(sum / (n ? n : 1));
  return s;
}

static void Note(Engine& e, int note, int vel) {
  uint8_t m[3] = {(uint8_t)(vel ? 0x90 : 0x80), (uint8_t)note, (uint8_t)vel};
  e.Midi(m, 3);
}
static void Set(Engine& e, const char* k, double v) {
  char b[32];
  snprintf(b, sizeof b, "%g", v);
  e.SetParam(k, b);
}
static double Get(Engine& e, const char* k) {
  char b[64];
  return e.GetParam(k, b, sizeof b) > 0 ? atof(b) : -999;
}
static std::string Text(Engine& e, const char* k) {
  char b[64] = {0};
  e.GetParam(k, b, sizeof b);
  return b;
}

static void WaitRom() {
  for (int i = 0; i < 600 && !(SampleRom::Get().Ready(BANK_CH) && SampleRom::Get().Ready(BANK_MISC) &&
                               SampleRom::Get().Ready(BANK_EXP) && SampleRom::Get().Ready(BANK_USER)); ++i)
    usleep(20000);
}

int main() {
  system("mkdir -p test/out/engine");
  const int base = 36;   // "Pads" note map: part n = base + n
  Engine e("test/out/engine_samples");
  WaitRom();
  CHECK(SampleRom::Get().Ready(BANK_CH), "sample ROM not ready after 12 s");

  // ---- every part alone, at defaults -------------------------------------------------------------------------
  for (int p = 0; p < NUM_PARTS; ++p) {
    std::vector<int16_t> rec;
    Stats s = Play(e, 400, [&](int b) {
      if (b == 0) Note(e, base + p, 127);
      if (b == 2) Note(e, base + p, 0);
      if (b == 200) Note(e, base + p, 70);
      if (b == 202) Note(e, base + p, 0);
    }, &rec);
    WriteWav(std::string("test/out/engine/part_") + kPartKeys[p] + ".wav", rec);
    printf("part %-4s peak %.3f rms %.4f\n", kPartKeys[p], s.peak, s.rms);
    CHECK(s.peak > 0.02, "part %s silent", kPartKeys[p]);
    CHECK(s.clipped == 0, "part %s clips", kPartKeys[p]);
  }

  // ---- a groove with sends on, through each delay / reverb / master type ---------------------------------------
  Set(e, "sd_rev", 40); Set(e, "cp_rev", 50); Set(e, "ch_dly", 30); Set(e, "mt_dly", 40);
  auto groove = [&](int b) {
    int step = b / 21, sub = b % 21;   // ~16th notes at ~126 bpm
    if (sub) return;
    if (step % 4 == 0) Note(e, base + PART_BD, 120);
    if (step % 8 == 4) Note(e, base + PART_SD, 110);
    if (step % 2 == 1) Note(e, base + PART_CH, 90);
    if (step % 16 == 14) Note(e, base + PART_OH, 100);
    if (step % 16 == 12) Note(e, base + PART_CP, 100);
    if (step % 16 == 7) Note(e, base + PART_LT, 100);
    if (step % 16 == 15) Note(e, base + PART_HT, 100);
    if (step % 16 == 3) Note(e, base + PART_RS, 100);
    if (step % 8 == 6) { Note(e, 60 + (step % 5) * 3, 100); }
    if (step % 8 == 7) { for (int n = 60; n < 80; ++n) Note(e, n, 0); }
  };
  const char* dly_types[] = {"Stereo", "Stereo BPM", "Mono", "Mono BPM", "Tape", "Tape BPM", "Tribal", "Cassette"};
  for (int t = 0; t < 8; ++t) {
    Set(e, "dly_type", t);
    std::vector<int16_t> rec;
    Stats s = Play(e, 700, groove, &rec);
    WriteWav(std::string("test/out/engine/dly_") + std::to_string(t) + ".wav", rec);
    printf("delay %-10s peak %.3f rms %.4f\n", dly_types[t], s.peak, s.rms);
    CHECK(s.clipped == 0, "delay type %d clips", t);
  }
  for (int t = 0; t < 8; ++t) {
    Set(e, "rev_type", t);
    std::vector<int16_t> rec;
    Stats s = Play(e, 700, groove, &rec);
    WriteWav(std::string("test/out/engine/rev_") + std::to_string(t) + ".wav", rec);
    printf("reverb %d peak %.3f rms %.4f\n", t, s.peak, s.rms);
    CHECK(s.clipped == 0, "reverb type %d clips", t);
  }
  Set(e, "rev_type", 1);
  for (int t = 0; t < 6; ++t) {
    Set(e, "mfx_type", t);
    Set(e, "comp_sidech", 1);
    std::vector<int16_t> rec;
    Stats s = Play(e, 700, groove, &rec);
    WriteWav(std::string("test/out/engine/mfx_") + std::to_string(t) + ".wav", rec);
    printf("master %d peak %.3f rms %.4f\n", t, s.peak, s.rms);
    CHECK(s.clipped == 0, "master type %d clips", t);
  }
  Set(e, "mfx_type", 0);

  // ---- every MULTI engine --------------------------------------------------------------------------------------
  int engines = kNumBuiltinEngines + kNumUnits;
  for (int en = 0; en < engines; ++en) {
    Set(e, "mt_engine", en);
    char nk[16];
    std::string slot0 = Text(e, "u_1_name");
    std::vector<int16_t> rec;
    Stats s = Play(e, 500, [&](int b) {
      if (b == 0) Note(e, base + PART_MT, 110);
      if (b == 40) Note(e, base + PART_MT, 0);
      if (b == 100) { Note(e, 60, 100); Note(e, 64, 100); Note(e, 67, 100); }
      if (b == 260) { Note(e, 60, 0); Note(e, 64, 0); Note(e, 67, 0); }
    }, &rec);
    snprintf(nk, sizeof nk, "%02d", en);
    WriteWav(std::string("test/out/engine/multi_") + nk + ".wav", rec);
    printf("multi engine %2d (%s, slot1 '%s') peak %.3f rms %.4f\n", en,
           kParams[P_MT_ENGINE].opts[en], slot0.c_str(), s.peak, s.rms);
    CHECK(s.peak > 0.005, "multi engine %d silent", en);
    CHECK(s.clipped == 0, "multi engine %d clips", en);
  }
  Set(e, "mt_engine", 0);

  // ---- choke: CH cuts OH (group 1 by default) ------------------------------------------------------------------
  {
    Engine c("test/out/engine_samples");
    Set(c, "oh_decay", 1023);
    Play(c, 1, [&](int) { Note(c, base + PART_OH, 127); });
    Stats before = Play(c, 20, [](int) {});
    Note(c, base + PART_CH, 1);    // quiet closed hat chokes the open one
    Play(c, 30, [](int) {});
    Stats after = Play(c, 20, [](int) {});
    printf("choke: OH rms %.4f before, %.5f after CH\n", before.rms, after.rms);
    CHECK(after.rms < before.rms * 0.2, "CH did not choke OH");
    // mute
    Set(c, "bd_mute", 1);
    Play(c, 200, [](int) {});
    Stats m = Play(c, 20, [&](int b) { if (!b) Note(c, base + PART_BD, 127); });
    CHECK(m.peak < 1e-3, "muted BD sounds (%.4f)", m.peak);
  }

  // ---- state round trip ----------------------------------------------------------------------------------------
  {
    Engine a("test/out/engine_samples"), b("test/out/engine_samples");
    srand(7);
    for (int i = 0; i < P_COUNT; ++i) {
      if (i == P_PANIC || i == P_RESCAN) continue;
      float u = rand() / (float)RAND_MAX;
      a.SetValue(i, kParams[i].min + u * (kParams[i].max - kParams[i].min));
    }
    char st[8192];
    int n = a.GetParam("state", st, sizeof st);
    CHECK(n > 0 && n < 8192, "state size %d", n);
    printf("state: %d bytes\n", n);
    b.SetParam("state", st);
    int bad = 0;
    for (int i = 0; i < P_COUNT; ++i) {
      if (i == P_PANIC || i == P_RESCAN) continue;
      float tol = (kParams[i].max - kParams[i].min) / 30000.0f + 1e-6f;
      if (fabsf(a.Value(i) - b.Value(i)) > tol) {
        if (bad++ < 5) printf("  state mismatch %s: %g vs %g\n", kParams[i].key, a.Value(i), b.Value(i));
      }
    }
    CHECK(bad == 0, "%d params differ after state restore", bad);
    // truncated / garbage state must not crash
    b.SetParam("state", "DLX1;AAAA");
    b.SetParam("state", "garbage");
    b.SetParam("state", "DLX1;!!!!;ch=;=x;;sp1=nonexistent");
    Play(b, 50, [](int) {});
  }

  // ---- engine switch resets the unit slots to that engine's defaults and reports them -------------------------
  if (kNumUnits > 0) {
    Engine u("test/out/engine_samples");
    char k[48];
    while (u.NextChanged(k, sizeof k)) {}
    Set(u, "mt_engine", kNumBuiltinEngines);   // the first unit
    int reported = 0;
    while (u.NextChanged(k, sizeof k)) ++reported;
    printf("engine switch reported %d slot changes; slot 1 = %s (%s)\n", reported, Text(u, "u_1_name").c_str(),
           Text(u, "u_1_display").c_str());
    CHECK(Text(u, "u_1_name") != "---", "unit slot 1 has no name");
  }

  // ---- display texts ---------------------------------------------------------------------------------------------
  printf("display: bd_cutoff=%s ch_sample=%s ch_tune=%s bd_pan=%s comp_ratio=%s dly_div=%s\n",
         Text(e, "bd_cutoff_display").c_str(), Text(e, "ch_sample_display").c_str(), Text(e, "ch_tune_display").c_str(),
         Text(e, "bd_pan_display").c_str(), Text(e, "comp_ratio_display").c_str(), Text(e, "dly_div_display").c_str());
  Set(e, "bd_cutoff", -30);
  CHECK(Text(e, "bd_cutoff_display") == "L70.0%", "cutoff text %s", Text(e, "bd_cutoff_display").c_str());
  Set(e, "bd_cutoff", 0);

  // ---- random sweep of every parameter while playing -------------------------------------------------------------
  {
    Engine r("test/out/engine_samples");
    srand(11);
    double worst = 0;
    bool nan = false;
    for (int round = 0; round < 60; ++round) {
      for (int k = 0; k < 40; ++k) {
        int i = rand() % P_COUNT;
        if (i == P_RESCAN) continue;
        float u = rand() / (float)RAND_MAX;
        r.SetValue(i, kParams[i].min + u * (kParams[i].max - kParams[i].min));
      }
      r.SetValue(P_VOLUME, 80);
      Stats s = Play(r, 60, [&](int b) { if (b % 7 == 0) Note(r, base + rand() % 11, 1 + rand() % 127);
                                          if (b % 9 == 0) Note(r, 60 + rand() % 24, rand() % 2 ? 100 : 0); });
      if (s.nan) nan = true;
      if (s.peak > worst) worst = s.peak;
    }
    printf("sweep: worst peak %.3f%s\n", worst, nan ? " NaN!" : "");
    CHECK(!nan, "NaN during the parameter sweep");
  }

  // ---- cost: a busy groove, per 128-frame block -------------------------------------------------------------------
  {
    Engine c("test/out/engine_samples");
    Set(c, "sd_rev", 40); Set(c, "ch_dly", 30); Set(c, "mfx_type", 1);
    int16_t buf[256];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const int N = 3000;
    for (int b = 0; b < N; ++b) {
      if (b % 21 == 0) for (int p = 0; p < NUM_PARTS; ++p) if ((b / 21 + p) % 3 == 0) Note(c, base + p, 100);
      c.Render(buf, 128);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / N;
    printf("cost: %.1f us per 128-frame block (%.1f%% of the 2902 us budget on this machine)\n", us, us / 29.02);
  }

  printf(g_fail ? "FAILED (%d)\n" : "PASSED\n", g_fail);
  return g_fail ? 1 : 0;
}
