// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- tests of the sample memory (factory synthesis, WAV loading, USER bank) and the sample part.
//
//   g++ -std=gnu++11 -O2 -fsanitize=address,undefined -pthread -Isrc test/samples_test.cc src/parts/pcm.cc +
//       src/rom/sample_rom.cc src/rom/synth_util.cc src/rom/synth_gen.cc src/rom/synth_banks.cc src/rom/wav.cc +
//       -o build/samples_test -lm && build/samples_test [--quick]     (+ = line continues)
//
// Writes every factory sample, rendered through the part, to test/out/samples/<bank>/NN_name.wav.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <complex>
#include <string>
#include <vector>

#include "../src/part.h"
#include "../src/rom/sample_rom.h"
#include "../src/rom/wav.h"

using namespace dlx;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char* kBankName[NUM_BANKS] = {"CH", "OH", "RS", "CP", "MISC", "USER", "EXP"};

static double Now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static void MkDirs(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) {
    cur += p[i];
    if (p[i] == '/') mkdir(cur.c_str(), 0755);
  }
  mkdir(p.c_str(), 0755);
}

// ---------------------------------------------------------------- params
struct Params {
  float v[PCM_DRIVE + 1];
  // defaults of a part's block (part = P_CH_BANK, P_OH_BANK, ...)
  void Defaults(int first) { for (int k = 0; k <= PCM_DRIVE; k++) v[k] = kParams[first + k].def; }
};

static int DefaultPartFor(int bank) {
  switch (bank) {
    case BANK_CH: return P_CH_BANK;
    case BANK_OH: return P_OH_BANK;
    case BANK_RS: return P_RS_BANK;
    case BANK_CP: return P_CP_BANK;
    default: return P_SP1_BANK;
  }
}

// render until inactive (or max frames); returns the frames and channel count
struct Out { std::vector<float> l, r; int ch = 1; bool went_inactive = false; };

static void RenderAll(Part* part, int max_frames, Out* o) {
  o->l.clear(); o->r.clear(); o->ch = 1;
  float l[kBlock], r[kBlock];
  int done = 0;
  while (done < max_frames) {
    if (!part->Active()) { o->went_inactive = true; break; }
    int c = part->Render(l, r, kBlock);
    if (c == 2) o->ch = 2;
    for (int i = 0; i < kBlock; i++) { o->l.push_back(l[i]); o->r.push_back(c == 2 ? r[i] : l[i]); }
    done += kBlock;
  }
  if (!part->Active()) o->went_inactive = true;
}

struct Stats { float peak = 0, rms = 0; bool bad = false; float maxdiff = 0; };
static Stats Analyse(const std::vector<float>& l, const std::vector<float>& r) {
  Stats s;
  double acc = 0;
  for (size_t i = 0; i < l.size(); i++) {
    float a = l[i], b = r[i];
    if (!std::isfinite(a) || !std::isfinite(b)) { s.bad = true; continue; }
    s.peak = fmaxf(s.peak, fmaxf(fabsf(a), fabsf(b)));
    acc += 0.5 * (a * a + b * b);
    if (i) s.maxdiff = fmaxf(s.maxdiff, fmaxf(fabsf(a - l[i - 1]), fabsf(b - r[i - 1])));
  }
  s.rms = l.empty() ? 0 : (float)sqrt(acc / l.size());
  return s;
}

// spectral centroid (Hz) of the first 16384 frames, Hann window, power-weighted
static void Fft(std::vector<std::complex<double> >& a) {
  size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len;
    std::complex<double> wl(cos(ang), sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (size_t j = 0; j < len / 2; j++) {
        std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v; a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}
static float Centroid(const std::vector<float>& x) {
  const size_t N = 16384;
  size_t m = x.size() < N ? x.size() : N;
  if (m < 64) return 0;
  std::vector<std::complex<double> > a(N);
  for (size_t i = 0; i < m; i++) a[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / (m - 1)));
  Fft(a);
  double num = 0, den = 0;
  for (size_t k = 1; k < N / 2; k++) {
    double p = std::norm(a[k]);
    num += p * (48000.0 * k / N);
    den += p;
  }
  return den > 0 ? (float)(num / den) : 0;
}

static std::string SafeName(const char* n) {
  std::string s;
  for (const char* p = n; *p; p++) {
    char c = *p;
    s += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') ? c : '_';
  }
  return s;
}

// ---------------------------------------------------------------- WAV writers for the USER tests
static void Put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
static void Put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255); }
static void PutTag(std::vector<uint8_t>& b, const char* t) { for (int i = 0; i < 4; i++) b.push_back(t[i]); }

// fmt: 1 PCM, 3 float; ext: write WAVE_FORMAT_EXTENSIBLE; junk: odd-sized unknown chunk before data;
// declared_extra: data size claimed beyond what is written (truncated file)
static std::vector<uint8_t> MakeWav(int fmt, int bits, int ch, uint32_t rate, uint32_t frames, float hz, bool ext,
                                    bool junk, uint32_t declared_extra) {
  std::vector<uint8_t> d;
  for (uint32_t i = 0; i < frames; i++) {
    for (int c = 0; c < ch; c++) {
      float v = 0.5f * sinf(2 * (float)M_PI * hz * (c + 1) * i / rate);
      if (fmt == 3) { uint32_t u; memcpy(&u, &v, 4); Put32(d, u); }
      else if (bits == 16) Put16(d, (uint16_t)(int16_t)lrintf(v * 32767));
      else if (bits == 24) { int32_t x = lrintf(v * 8388607); d.push_back(x & 255); d.push_back((x >> 8) & 255); d.push_back((x >> 16) & 255); }
      else if (bits == 32) Put32(d, (uint32_t)(int32_t)lrintf(v * 2147483000.0f));
      else d.push_back((uint8_t)lrintf(v * 127 + 128));
    }
  }
  std::vector<uint8_t> b;
  PutTag(b, "RIFF"); Put32(b, 0); PutTag(b, "WAVE");
  int bps = bits / 8;
  if (ext) {
    PutTag(b, "fmt "); Put32(b, 40);
    Put16(b, 0xFFFE); Put16(b, ch); Put32(b, rate); Put32(b, rate * ch * bps); Put16(b, ch * bps); Put16(b, bits);
    Put16(b, 22); Put16(b, bits); Put32(b, ch == 2 ? 3 : 4);
    Put16(b, (uint16_t)fmt);   // SubFormat GUID ...
    static const uint8_t g[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    b.insert(b.end(), g, g + 14);
  } else {
    PutTag(b, "fmt "); Put32(b, 16);
    Put16(b, fmt); Put16(b, ch); Put32(b, rate); Put32(b, rate * ch * bps); Put16(b, ch * bps); Put16(b, bits);
  }
  if (junk) { PutTag(b, "LIST"); Put32(b, 5); for (int i = 0; i < 5; i++) b.push_back('x'); b.push_back(0); }
  PutTag(b, "data"); Put32(b, (uint32_t)d.size() + declared_extra);
  b.insert(b.end(), d.begin(), d.end());
  if (d.size() & 1) b.push_back(0);
  uint32_t riff = (uint32_t)b.size() - 8 + declared_extra;
  b[4] = riff & 255; b[5] = (riff >> 8) & 255; b[6] = (riff >> 16) & 255; b[7] = riff >> 24;
  return b;
}
static void WriteFile(const std::string& path, const std::vector<uint8_t>& b) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("cannot write %s\n", path.c_str()); exit(1); }
  fwrite(&b[0], 1, b.size(), f);
  fclose(f);
}

// estimated frequency by zero crossings of channel c
static float ZcHz(const Sample* s, int c) {
  int z = 0;
  for (uint32_t i = 1; i < s->frames; i++) {
    int16_t a = s->data[(i - 1) * s->channels + c], b = s->data[i * s->channels + c];
    if ((a < 0) != (b < 0)) z++;
  }
  return z * 0.5f * 48000.0f / s->frames;
}

// ---------------------------------------------------------------- tests
static void TestFactory(bool write) {
  SampleRom& rom = SampleRom::Get();
  printf("\n%-5s %-3s %-16s %6s %3s %6s %6s %7s %7s\n", "bank", "#", "name", "ms", "ch", "gain", "peak", "rms", "cent.Hz");
  for (int b = 0; b < NUM_BANKS; b++) {
    if (b == BANK_USER) continue;
    CHECK(rom.Ready(b), "bank %s not ready", kBankName[b]);
    CHECK(rom.Count(b) == SampleRom::Capacity(b), "bank %s count %d", kBankName[b], rom.Count(b));
    std::string dir = std::string("test/out/samples/") + kBankName[b];
    if (write) MkDirs(dir);
    double sumc = 0;
    for (int i = 0; i < rom.Count(b); i++) {
      const Sample* s = rom.Find(b, i);
      CHECK(s && s->frames > 10, "%s %d missing", kBankName[b], i);
      if (!s) continue;
      CHECK(strlen(s->name) > 0 && strlen(s->name) <= 16, "name length '%s'", s->name);
      CHECK(strcmp(s->name, SampleRom::FactoryName(b, i)) == 0, "FactoryName mismatch %s", s->name);
      CHECK(rom.IndexOf(b, s->name) == i, "IndexOf(%s) = %d (duplicate name?)", s->name, rom.IndexOf(b, s->name));
      CHECK(s->frames <= 4 * 48000, "too long %s", s->name);
      CHECK(s->gain > 0.0f && s->gain <= 1.0f, "gain %s %f", s->name, s->gain);
      // through the part, with the defaults of the part that plays this bank
      Params p;
      p.Defaults(DefaultPartFor(b));
      p.v[PCM_BANK] = (float)b;
      p.v[PCM_SAMPLE] = (float)(i + 1);
      p.v[PCM_DECAY] = 1023;   // whole sample
      Part* part = NewSamplePart();
      part->Init(p.v, 0, 1);
      part->Trigger(1.0f, -1);
      Out o;
      RenderAll(part, 5 * 48000, &o);
      Stats st = Analyse(o.l, o.r);
      CHECK(!st.bad, "NaN/Inf in %s", s->name);
      CHECK(st.peak <= 1.5f, "peak %s %f", s->name, st.peak);
      CHECK(st.peak > 0.02f, "silent %s %f", s->name, st.peak);
      CHECK(o.went_inactive, "%s never went inactive", s->name);
      CHECK(o.ch == s->channels, "%s channel count %d vs %d", s->name, o.ch, s->channels);
      std::vector<float> mono(o.l.size());
      for (size_t k = 0; k < mono.size(); k++) mono[k] = 0.5f * (o.l[k] + o.r[k]);
      float c = Centroid(mono);
      sumc += c;
      printf("%-5s %-3d %-16s %6.0f %3d %6.3f %6.3f %7.4f %7.0f\n", kBankName[b], i + 1, s->name,
             s->frames / 48.0, s->channels, s->gain, st.peak, st.rms, c);
      if (write) {
        char fn[512];
        snprintf(fn, sizeof(fn), "%s/%02d_%s.wav", dir.c_str(), i + 1, SafeName(s->name).c_str());
        WriteWavFloat(fn, &o.l[0], o.ch == 2 ? &o.r[0] : 0, (uint32_t)o.l.size(), 48000);
      }
      delete part;
    }
    printf("%-5s mean centroid %.0f Hz\n", kBankName[b], sumc / rom.Count(b));
  }
}

// a few sounds that must have a clear spectral character
static void TestCharacter() {
  SampleRom& rom = SampleRom::Get();
  struct { int bank; const char* name; float lo, hi; } t[] = {
    {BANK_CH, "808 CH", 4000, 20000}, {BANK_OH, "909 OH", 4000, 20000}, {BANK_MISC, "Kick 808", 20, 250},
    {BANK_MISC, "Sub Bass", 20, 200}, {BANK_MISC, "808 Cowbell", 500, 3000}, {BANK_CP, "808 Clap", 600, 4000},
  };
  for (size_t k = 0; k < sizeof(t) / sizeof(t[0]); k++) {
    int i = rom.IndexOf(t[k].bank, t[k].name);
    CHECK(i >= 0, "missing %s", t[k].name);
    if (i < 0) continue;
    const Sample* s = rom.Find(t[k].bank, i);
    std::vector<float> x(s->frames);
    for (uint32_t f = 0; f < s->frames; f++) x[f] = s->data[f * s->channels] / 32768.0f;
    float c = Centroid(x);
    CHECK(c >= t[k].lo && c <= t[k].hi, "%s centroid %.0f outside %.0f..%.0f", t[k].name, c, t[k].lo, t[k].hi);
  }
}

static Part* MakePart(Params& p, int bank, const char* name) {
  p.Defaults(P_SP1_BANK);
  p.v[PCM_BANK] = (float)bank;
  p.v[PCM_SAMPLE] = (float)(SampleRom::Get().IndexOf(bank, name) + 1);
  Part* part = NewSamplePart();
  part->Init(p.v, 0, 7);
  return part;
}

static void CheckOut(const char* what, const Out& o, bool expect_sound, bool expect_inactive) {
  Stats s = Analyse(o.l, o.r);
  CHECK(!s.bad, "%s: NaN/Inf", what);
  CHECK(s.peak <= 1.5f, "%s: peak %f", what, s.peak);
  if (expect_sound) CHECK(s.peak > 0.01f, "%s: silent (peak %f)", what, s.peak);
  if (expect_inactive) CHECK(o.went_inactive, "%s: still active", what);
  printf("  %-28s frames %7zu ch %d peak %.3f rms %.4f maxstep %.4f %s\n", what, o.l.size(), o.ch, s.peak, s.rms,
         s.maxdiff, o.went_inactive ? "" : "(active)");
}

static void TestExtremes() {
  printf("\nparameter extremes:\n");
  Params p;
  Out o;
  struct Case { const char* what; int bank; const char* name; int off; float val; } cases[] = {
    {"tune +24 (808 OH)", BANK_OH, "808 OH", PCM_TUNE, 24},
    {"tune -24 (808 OH)", BANK_OH, "808 OH", PCM_TUNE, -24},
    {"tune -24 (Crash 2, stereo)", BANK_MISC, "Crash 2", PCM_TUNE, -24},
    {"redux 100 (Stab Maj)", BANK_MISC, "Stab Maj", PCM_REDUX, 100},
    {"redux 50 (Kick 909)", BANK_MISC, "Kick 909", PCM_REDUX, 50},
    {"drive 100 (Snare 909)", BANK_MISC, "Snare 909", PCM_DRIVE, 100},
    {"attack 100 (Ride)", BANK_MISC, "Ride", PCM_ATTACK, 100},
    {"decay 0 (Crash)", BANK_MISC, "Crash", PCM_DECAY, 0},
    {"decay 200 (Crash)", BANK_MISC, "Crash", PCM_DECAY, 200},
    {"cutoff -40 reso 100 (Clap)", BANK_CP, "909 Clap", PCM_CUTOFF, -40},
    {"cutoff +70 (Kick 808)", BANK_MISC, "Kick 808", PCM_CUTOFF, 70},
  };
  for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
    Part* part = MakePart(p, cases[k].bank, cases[k].name);
    p.v[cases[k].off] = cases[k].val;
    if (cases[k].off == PCM_CUTOFF && cases[k].val < 0) p.v[PCM_RESON] = 100;
    part->Trigger(1.0f, -1);
    RenderAll(part, 20 * 48000, &o);
    CheckOut(cases[k].what, o, true, true);
    delete part;
  }
  // decay 0 must be short
  {
    Part* part = MakePart(p, BANK_MISC, "Crash");
    p.v[PCM_DECAY] = 0;
    part->Trigger(1.0f, -1);
    RenderAll(part, 48000, &o);
    CHECK(o.l.size() < 48 * 30, "decay 0 too long: %zu frames", o.l.size());
    delete part;
  }
  // reverse: END < START
  {
    Part* part = MakePart(p, BANK_MISC, "Kick 808");
    p.v[PCM_START] = 100; p.v[PCM_END] = 0;
    part->Trigger(1.0f, -1);
    RenderAll(part, 5 * 48000, &o);
    CheckOut("reverse (Kick 808)", o, true, true);
    // reversed kick: loudness grows towards the end
    size_t n = o.l.size();
    double a = 0, b = 0;
    for (size_t i = 0; i < n / 4; i++) a += fabs(o.l[i]);
    for (size_t i = n - n / 4; i < n; i++) b += fabs(o.l[i]);
    CHECK(b > a * 3, "reverse does not look reversed (%f vs %f)", a, b);
    delete part;
  }
  // start = end: silence, inactive at once
  {
    Part* part = MakePart(p, BANK_MISC, "Kick 808");
    p.v[PCM_START] = 40; p.v[PCM_END] = 40;
    part->Trigger(1.0f, -1);
    CHECK(!part->Active(), "empty window should be inactive");
    delete part;
  }
  // note transposition: note 72 = +12 -> half the length
  {
    Part* part = MakePart(p, BANK_MISC, "Sub Bass");
    part->Trigger(1.0f, -1);
    RenderAll(part, 10 * 48000, &o);
    size_t n0 = o.l.size();
    part->Trigger(1.0f, 72);
    RenderAll(part, 10 * 48000, &o);
    size_t n1 = o.l.size();
    CHECK(n1 < n0 * 0.6 && n1 > n0 * 0.4, "note 72 length %zu vs %zu", n1, n0);
    delete part;
  }
  // empty USER bank (before a dir is set): silence, inactive
  {
    Params q;
    q.Defaults(P_SP1_BANK);
    q.v[PCM_BANK] = BANK_USER;
    Part* part = NewSamplePart();
    part->Init(q.v, 0, 1);
    part->Trigger(1.0f, -1);
    CHECK(!part->Active(), "empty bank should give no voice");
    char buf[64];
    CHECK(part->Format(PCM_SAMPLE, 3, buf, sizeof(buf)), "Format");
    printf("  format of empty USER: '%s'\n", buf);
    delete part;
  }
  // sample index clamp + Format
  {
    Part* part = MakePart(p, BANK_CH, "808 CH");
    p.v[PCM_SAMPLE] = 128;
    char buf[64];
    part->Format(PCM_SAMPLE, 128, buf, sizeof(buf));
    printf("  format CH #128 -> '%s'\n", buf);
    CHECK(strstr(buf, SampleRom::Get().Find(BANK_CH, 15)->name) != 0, "clamped format");
    part->Format(PCM_SAMPLE, 7, buf, sizeof(buf));
    printf("  format CH #7 -> '%s'\n", buf);
    part->Trigger(1.0f, -1);
    RenderAll(part, 48000, &o);
    CheckOut("sample 128 clamps to 16", o, true, true);
    delete part;
  }
}

// retrigger: the jump at each new hit must not exceed what the signal itself does
static void TestRetrigger() {
  printf("\nretrigger / choke:\n");
  Params p;
  const char* names[] = {"Sub Bass", "Stab Maj", "Ride"};
  int periods[] = {6000, 700, 97, 31, 5};
  for (int ni = 0; ni < 3; ni++) {
    for (int pi = 0; pi < 5; pi++) {
      Part* part = MakePart(p, BANK_MISC, names[ni]);
      // reference: the steepest step of one undisturbed hit
      part->Trigger(1.0f, -1);
      Out ref;
      RenderAll(part, 4 * 48000, &ref);
      float refstep = Analyse(ref.l, ref.r).maxdiff;
      std::vector<float> L, R;
      float lb[kBlock], rb[kBlock];
      int period = periods[pi], next = 0;
      int total = 48000;
      Noise vel;
      vel.Seed(99);
      for (int t = 0; t < total; t += kBlock) {
        // trigger inside a block is not possible (block-based engine); emulate sub-block timing with n < kBlock
        int n = kBlock;
        if (next >= t && next < t + kBlock) {
          int pre = next - t;
          if (pre > 0) {
            int c = part->Render(lb, rb, pre);
            for (int i = 0; i < pre; i++) { L.push_back(lb[i]); R.push_back(c == 2 ? rb[i] : lb[i]); }
          }
          part->Trigger(0.5f + 0.5f * vel.Uni(), -1);
          next += period;
          n = kBlock - pre;
        }
        if (part->Active()) {
          int c = part->Render(lb, rb, n);
          for (int i = 0; i < n; i++) { L.push_back(lb[i]); R.push_back(c == 2 ? rb[i] : lb[i]); }
        } else {
          for (int i = 0; i < n; i++) { L.push_back(0); R.push_back(0); }
        }
      }
      Stats s = Analyse(L, R);
      char what[96];
      snprintf(what, sizeof(what), "%s every %d", names[ni], period);
      CHECK(!s.bad && s.peak <= 1.5f, "%s: bad/peak %f", what, s.peak);
      // allowed: the sound's own steepest step plus a small margin
      float lim = refstep * 1.6f + 0.01f;
      CHECK(s.maxdiff <= lim, "%s: click, step %.4f > %.4f", what, s.maxdiff, lim);
      printf("  %-24s peak %.3f maxstep %.4f (single hit %.4f)\n", what, s.peak, s.maxdiff, refstep);
      // then choke: silent within ~4 ms, no click
      part->Choke();
      Out o;
      RenderAll(part, 48000, &o);
      Stats cs = Analyse(o.l, o.r);
      // 3 ms fade (+ up to ~8 ms of the click absorber's sub -50 dB tail after a voice steal)
      CHECK(o.went_inactive && o.l.size() <= 8 * kBlock, "%s: choke took %zu frames", what, o.l.size());
      CHECK(cs.maxdiff <= lim, "%s: choke click %.4f", what, cs.maxdiff);
      delete part;
    }
  }
}

static void TestUser(const std::string& dir) {
  printf("\nUSER bank:\n");
  SampleRom& rom = SampleRom::Get();
  MkDirs(dir + "/b_sub/deeper");
  MkDirs(dir + "/A_first");
  // folder order: "" (root) < "A_first" < "b_sub" < "b_sub/deeper" (case-insensitive)
  WriteFile(dir + "/zeta mono 44k.wav", MakeWav(1, 16, 1, 44100, 44100, 1000, false, false, 0));
  WriteFile(dir + "/Alpha stereo 96k.WAV", MakeWav(1, 24, 2, 96000, 48000, 500, false, true, 0));
  WriteFile(dir + "/A_first/float 48k.wav", MakeWav(3, 32, 1, 48000, 24000, 440, false, false, 0));
  WriteFile(dir + "/b_sub/extensible.wav", MakeWav(1, 16, 2, 48000, 12000, 300, true, true, 0));
  WriteFile(dir + "/b_sub/deeper/truncated.wav", MakeWav(1, 16, 1, 22050, 11025, 200, false, false, 50000));
  WriteFile(dir + "/b_sub/deeper/long 8bit.wav", MakeWav(1, 8, 1, 48000, 6 * 48000, 100, false, false, 0));
  std::vector<uint8_t> junk(1000, 0x5a);
  WriteFile(dir + "/not a wav.wav", junk);
  WriteFile(dir + "/readme.txt", junk);
  WriteFile(dir + "/.hidden.wav", MakeWav(1, 16, 1, 48000, 100, 100, false, false, 0));
  std::vector<uint8_t> tiny(10, 1);
  WriteFile(dir + "/tiny.wav", tiny);
  // symlink must be ignored
  if (symlink((dir + "/zeta mono 44k.wav").c_str(), (dir + "/link.wav").c_str()) != 0) printf("  (symlink failed)\n");

  uint32_t g0 = rom.UserGeneration();
  double t0 = Now();
  rom.Start(dir.c_str());   // new dir -> rescan
  rom.WaitIdle();
  printf("  scan took %.1f ms\n", (Now() - t0) * 1000);
  CHECK(rom.UserGeneration() > g0, "no rescan after Start(new dir)");
  int n = rom.Count(BANK_USER);
  const char* expect[] = {"Alpha stereo 96k", "zeta mono 44k", "float 48k", "extensible", "long 8bit", "truncated"};
  CHECK(n == 6, "user count %d", n);
  for (int i = 0; i < n; i++) {
    const Sample* s = rom.Find(BANK_USER, i);
    printf("  %d %-20s frames %6u ch %d gain %.2f zc %.0f Hz\n", i + 1, s->name, s->frames, s->channels, s->gain, ZcHz(s, 0));
    if (i < 6) CHECK(strcmp(s->name, expect[i]) == 0, "order: %d is %s, expected %s", i, s->name, expect[i]);
  }
  CHECK(rom.IndexOf(BANK_USER, "extensible") == 3, "IndexOf extensible");
  CHECK(rom.IndexOf(BANK_USER, "EXTENSIBLE") == 3, "IndexOf case-insensitive");
  CHECK(rom.IndexOf(BANK_USER, "nope") == -1, "IndexOf missing");
  if (n == 6) {
    const Sample* a = rom.Find(BANK_USER, 0);   // 24-bit stereo 96k, 0.5 s, L 500 Hz, R 1000 Hz
    CHECK(a->channels == 2 && abs((int)a->frames - 24000) < 4, "96k frames %u", a->frames);
    CHECK(fabsf(ZcHz(a, 0) - 500) < 5 && fabsf(ZcHz(a, 1) - 1000) < 10, "96k pitch %f %f", ZcHz(a, 0), ZcHz(a, 1));
    const Sample* z = rom.Find(BANK_USER, 1);   // 44.1k 1 s 1000 Hz
    CHECK(z->channels == 1 && abs((int)z->frames - 48000) < 4, "44.1k frames %u", z->frames);
    CHECK(fabsf(ZcHz(z, 0) - 1000) < 5, "44.1k pitch %f", ZcHz(z, 0));
    // amplitude preserved through the resampler (0.5 full scale)
    int16_t mx = 0;
    for (uint32_t i = 1000; i < z->frames - 1000; i++) if (z->data[i] > mx) mx = z->data[i];
    CHECK(fabsf(mx / 32767.0f - 0.5f) < 0.01f, "44.1k amplitude %f", mx / 32767.0f);
    const Sample* f = rom.Find(BANK_USER, 2);
    CHECK(f->frames == 24000 && fabsf(ZcHz(f, 0) - 440) < 3, "float");
    const Sample* e = rom.Find(BANK_USER, 3);
    CHECK(e->channels == 2 && e->frames == 12000, "extensible %u %d", e->frames, e->channels);
    const Sample* l = rom.Find(BANK_USER, 4);
    CHECK(l->frames == 4 * 48000, "4 s cap: %u", l->frames);
    const Sample* t = rom.Find(BANK_USER, 5);
    CHECK(abs((int)t->frames - 24000) < 4, "truncated 22.05k: %u", t->frames);
  }

  // play a user sample through the part, rescan while it plays (the old bank must stay valid: ASan would catch a
  // use-after-free), then check the new bank
  Params p;
  p.Defaults(P_SP1_BANK);
  p.v[PCM_BANK] = BANK_USER;
  p.v[PCM_SAMPLE] = 5;   // long 8bit (4 s)
  Part* part = NewSamplePart();
  part->Init(p.v, 0, 3);
  part->Trigger(1.0f, -1);
  float lb[kBlock], rb[kBlock];
  for (int i = 0; i < 100; i++) part->Render(lb, rb, kBlock);
  WriteFile(dir + "/new one.wav", MakeWav(1, 16, 1, 48000, 4800, 700, false, false, 0));
  unlink((dir + "/b_sub/extensible.wav").c_str());
  uint32_t g1 = rom.UserGeneration();
  rom.Rescan();
  rom.WaitIdle();
  CHECK(rom.UserGeneration() == g1 + 1, "rescan generation");
  CHECK(rom.Count(BANK_USER) == 6, "after rescan count %d", rom.Count(BANK_USER));
  CHECK(rom.IndexOf(BANK_USER, "new one") >= 0 && rom.IndexOf(BANK_USER, "extensible") < 0, "rescan content");
  usleep(2600 * 1000);   // past the retire grace time: the old bank is pinned by the voice, must survive
  Out o;
  RenderAll(part, 6 * 48000, &o);
  CheckOut("user voice across rescan", o, true, true);
  delete part;
  // now nothing pins it: it is freed on the worker's next sweep (ASan/LSan at exit reports a leak otherwise)
  rom.Rescan();
  rom.WaitIdle();
  usleep(2600 * 1000);
  rom.Rescan();
  rom.WaitIdle();
  printf("  memory after rescans: %.2f MB\n", rom.MemoryBytes() / 1048576.0);
}

static void TestWavParser() {
  printf("\nWAV parser:\n");
  WavData w;
  std::vector<uint8_t> b = MakeWav(1, 32, 3, 32000, 3200, 250, true, true, 0);   // 32-bit int, 3 channels
  CHECK(ParseWav(&b[0], b.size(), 192000, &w), "32-bit 3ch");
  CHECK(w.channels == 2 && abs((int)w.frames - 4800) < 3, "3ch -> 2: %u %d", w.frames, w.channels);
  b = MakeWav(3, 32, 1, 48000, 100, 250, true, false, 0);
  CHECK(ParseWav(&b[0], b.size(), 192000, &w) && w.frames == 100, "float extensible");
  // every truncation point of a small file must be handled (no crash; result valid or rejected)
  b = MakeWav(1, 24, 2, 44100, 300, 250, false, true, 0);
  int ok = 0;
  for (size_t len = 0; len <= b.size(); len++) {
    std::vector<uint8_t> c(b.begin(), b.begin() + len);
    if (ParseWav(c.empty() ? 0 : &c[0], c.size(), 192000, &w)) ok++;
  }
  printf("  truncations accepted: %d of %zu\n", ok, b.size() + 1);
  // random corruption fuzz
  Noise nz;
  nz.Seed(5);
  for (int k = 0; k < 3000; k++) {
    std::vector<uint8_t> c = b;
    int m = 1 + (int)(nz.Uni() * 8);
    for (int j = 0; j < m; j++) c[(size_t)(nz.Uni() * 60)] = (uint8_t)nz.NextU();
    ParseWav(&c[0], c.size(), 192000, &w);
  }
  CHECK(!ParseWav(0, 0, 100, &w), "empty");
}

static void TestPerf() {
  printf("\nplayback cost:\n");
  Params p;
  struct { const char* name; int redux, drive, cut; float tune; } cs[] = {
    {"Crash 2", 0, 0, 0, 0.0f}, {"Crash 2", 0, 0, 0, 3.3f}, {"Crash 2", 40, 50, -30, 3.3f}, {"Kick 808", 0, 0, 0, 0.0f},
    {"Kick 808", 40, 50, -30, -5.0f},
  };
  for (size_t k = 0; k < sizeof(cs) / sizeof(cs[0]); k++) {
    Part* part = MakePart(p, BANK_MISC, cs[k].name);
    p.v[PCM_REDUX] = (float)cs[k].redux; p.v[PCM_DRIVE] = (float)cs[k].drive; p.v[PCM_CUTOFF] = (float)cs[k].cut;
    p.v[PCM_TUNE] = cs[k].tune; p.v[PCM_DECAY] = 1023;
    float lb[kBlock], rb[kBlock];
    long frames = 0;
    double t0 = Now();
    for (int rep = 0; rep < 40; rep++) {
      part->Trigger(1.0f, -1);
      for (int i = 0; i < 1500; i++) { part->Render(lb, rb, kBlock); frames += kBlock; }   // 2 s, two voices crossfading at the start
    }
    double t = Now() - t0;
    printf("  %-9s redux %2d drive %2d cut %3d tune %+4.1f: %6.1f ns/frame\n", cs[k].name, cs[k].redux, cs[k].drive,
           cs[k].cut, cs[k].tune, t * 1e9 / frames);
    delete part;
  }
}

int main(int argc, char** argv) {
  bool quick = argc > 1 && strcmp(argv[1], "--quick") == 0;
  SampleRom& rom = SampleRom::Get();
  CHECK(!rom.Ready(BANK_CH), "ready before start");
  double t0 = Now();
  rom.Start(NULL);
  double tch = 0;
  while (!rom.Ready(BANK_CP)) usleep(500);
  tch = Now() - t0;
  rom.Start(NULL);   // no-op
  rom.WaitIdle();
  double t = Now() - t0;
  uint64_t mem = rom.MemoryBytes();
  printf("factory build: %.3f s wall (worker reports %.3f s), CH..CP ready after %.3f s, memory %.2f MB\n", t,
         rom.FactoryBuildSeconds(), tch, mem / 1048576.0);
  for (int b = 0; b < NUM_BANKS; b++) printf("  %-4s %d\n", kBankName[b], rom.Count(b));
  CHECK(rom.Ready(BANK_USER) && rom.Count(BANK_USER) == 0, "USER without dir");
  CHECK(mem <= 12u * 1024 * 1024, "factory memory %.2f MB", mem / 1048576.0);

  TestFactory(!quick);
  TestCharacter();
  TestExtremes();
  TestRetrigger();
  TestWavParser();
  char tmpl[] = "/tmp/dlx_samples_XXXXXX";
  const char* tmp = mkdtemp(tmpl);
  CHECK(tmp != 0, "mkdtemp");
  if (tmp) TestUser(tmp);
  TestPerf();
  if (tmp) { std::string cmd = std::string("rm -rf '") + tmp + "'"; if (system(cmd.c_str()) != 0) {} }
  printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL OK", g_fail);
  return g_fail ? 1 : 0;
}
