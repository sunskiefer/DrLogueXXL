// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the send REVERB.
//
// Room / Hall / Space / Riser / Submarine / Labirinto / Luce run on one engine written for DrLogueXXL:
//   pre-delay -> input diffusion (4 all-passes per side) -> an 8-line feedback delay network (orthonormal
//   Hadamard mix, per-line RT60 gain, one-pole damping in every line, slow chorus modulation of every read head).
//   Per type: Room/Hall/Space differ in size, modulation and decay range (Space reaches ~5 min at TIME 100: a
//   near-freeze). Riser puts an upward pitch shifter in two of the lines' feedback (each pass climbs: a shimmer
//   that rises), Submarine a downward one plus extra in-loop darkening (each pass sinks and dulls).
//   Labirinto (own design in the spirit of fedemone's NeonLabirinto; see VENDORED_FX.md): two 4-line banks that
//   feed each other (true ping-pong: energy crosses the stereo field once per bounce), a resonant "material"
//   colour in the loop, bounce time on DIVE.
//   Luce (own design in the spirit of fedemone's LuceAlNeon): a clean FDN tail feeding parallel paths -- an
//   octave-down "dark" halo (DIVE), granular up-pitched sparkle pops (RISE) and a harmonic exciter (BRIGHT > 50).
// Clouds: Mutable Instruments Clouds' reverb (Griesinger/Dattorro loop), diffuser and pitch shifter as adapted to
//   48 kHz by cldmnky (clouds-revfx), vendored under third_party/; the parameter mapping is at ProcessClouds().
//
// INPUT (all own types): input density and level. 0 = no pre-diffusion (discrete early echoes, grainy onset) and
//   -6 dB in, 100 = four all-passes at 0.75 (smooth, dense onset) and 0 dB in. 50 = all-passes at 0.5, -2.5 dB.
// BRIGHT: in-loop damping, 0 = ~800 Hz, 100 = ~18 kHz (one-pole low-pass in every line).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fx.h"
#include "fx_util.h"

#include "clouds/dsp/frame.h"   // third_party/mutable (the vendored headers expect it included first)
#include "reverb.h"             // third_party/community/cldmnky/clouds-revfx
#include "diffuser.h"
#include "pitch_shifter.h"

namespace dlx {

namespace {

enum { R_ROOM = 0, R_HALL, R_SPACE, R_RISER, R_SUB, R_CLOUDS, R_LAB, R_LUCE, R_COUNT };
static const int kN = 8;
static const int kLineSz = 1 << 15;      // 0.68 s per FDN line (Labirinto's longest bounce is 0.5 s * 1.15)
// base FDN line lengths (samples at 48 kHz, mutually prime), scaled per type
static const float kBaseLen[kN] = {1559.f, 1801.f, 2053.f, 2311.f, 2593.f, 2851.f, 3137.f, 3433.f};
static const float kDiffLen[2][4] = {{142.f, 107.f, 379.f, 277.f}, {151.f, 113.f, 397.f, 263.f}};
static const float kLfoHz[kN] = {0.11f, 0.17f, 0.23f, 0.29f, 0.13f, 0.19f, 0.31f, 0.37f};

struct TypeCfg {
  float scale;      // line length scale
  float rt_min, rt_max;
  float mod;        // read-head modulation depth (samples)
  float predelay;   // ms
  float diff_scale; // diffuser length scale
  float out_gain;
};
static const TypeCfg kCfg[R_COUNT] = {
    {0.42f, 0.2f, 3.0f, 3.0f, 4.0f, 0.6f, 0.8f},      // Room
    {1.00f, 0.6f, 12.0f, 7.0f, 18.0f, 1.0f, 0.75f},   // Hall
    {1.85f, 1.0f, 20.0f, 22.0f, 35.0f, 1.6f, 0.8f},   // Space (TIME 100: ~5 min)
    {1.10f, 0.6f, 14.0f, 6.0f, 15.0f, 1.0f, 0.8f},    // Riser
    {1.10f, 0.6f, 14.0f, 6.0f, 15.0f, 1.0f, 0.8f},    // Submarine
    {1.00f, 0.6f, 12.0f, 0.0f, 0.0f, 1.0f, 1.0f},     // Clouds (own engine unused)
    {1.00f, 0.3f, 12.0f, 2.0f, 6.0f, 0.8f, 1.5f},     // Labirinto (lengths set by the bounce)
    {1.00f, 0.4f, 12.0f, 0.0f, 10.0f, 1.0f, 0.65f},   // Luce (pure tail, no modulation in the loop)
};

inline float RtSec(int type, float t) {
  t = Clamp(t, 0.0f, 100.0f) * 0.01f;
  const TypeCfg& c = kCfg[type];
  if (type == R_SPACE && t > 0.95f) {   // last 5 %: 20 s -> ~300 s (near-freeze)
    float u = (t - 0.95f) * 20.0f;
    return c.rt_max * powf(15.0f, u);
  }
  if (type == R_SPACE) t /= 0.95f;
  return c.rt_min * powf(c.rt_max / c.rt_min, t);
}
inline float DampHz(float b) { return 800.0f * powf(22.5f, Clamp(b, 0.0f, 100.0f) * 0.01f); }
inline float CloudsKrt(float t) { return 0.3f + 0.69f * Clamp(t, 0.0f, 100.0f) * 0.01f; }
inline float BounceMs(float dive) { return 60.0f * powf(500.0f / 60.0f, Clamp(dive, 0.0f, 100.0f) * 0.01f); }

struct Allpass {
  fxu::Line l;
  int len = 100;
  inline float Process(float x, float g) {
    float d = l.AtU(len - 1);   // the line was zeroed at Alloc and is zeroed again by Clear (1 kB)
    float w = x + g * d;
    l.WriteU(w);
    return d - g * w;
  }
};

// Luce's sparkle: random 5-15 ms grains from an 85 ms buffer, played 5..24 semitones up, random pan
struct Sparkle {
  fxu::Line buf;
  Noise rnd;
  float pos = 0.0f, inc = 1.0f, env_ph = 1.0f, env_inc = 0.0f, gl = 0.0f, gr = 0.0f;
  int wait = 0;
  void Clear() { buf.Clear(); env_ph = 1.0f; wait = 0; }
  inline void Process(float x, float density, float* ol, float* orr) {
    buf.Write(x);
    if (env_ph >= 1.0f) {
      if (--wait > 0) { *ol = *orr = 0.0f; return; }
      static const float kSemis[5] = {5.0f, 7.0f, 12.0f, 19.0f, 24.0f};
      float st = kSemis[rnd.NextU() % 5];
      inc = exp2f(st * (1.0f / 12.0f));
      float len = (5.0f + 10.0f * rnd.Uni()) * 48.0f;
      env_inc = 1.0f / len;
      env_ph = 0.0f;
      pos = len * inc + 16.0f;               // start far enough back that it never catches the write head
      if (pos > 4000.0f) pos = 4000.0f;
      float p = rnd.Uni() * 0.5f * kPi;
      gl = cosf(p); gr = sinf(p);
      wait = (int)((0.02f + 0.25f * (1.0f - density)) * 48000.0f * (0.5f + rnd.Uni()));
    }
    float e = SinPhase(env_ph * 0.5f);       // half-sine window
    float y = buf.Lin(pos) * e;
    pos += 1.0f - inc;                       // read head moves faster than the write head
    if (pos < 1.0f) pos = 1.0f;
    env_ph += env_inc;
    *ol = y * gl;
    *orr = y * gr;
  }
};

}  // namespace

struct ReverbFx::Impl {
  // own engine
  // the 8 FDN lines, interleaved frame by frame in one ring (one write index for all, 8 floats per frame)
  float* fdn = nullptr;
  int fdn_w = 0, fdn_filled = 0;
  fxu::Line pre[2];
  Allpass ap[2][4];
  float len[kN], len_t[kN];
  float g[kN], damp_s[kN], damp_a = 0.5f;
  float lfo_c[kN], lfo_s[kN], lfo_bc[kN], lfo_bs[kN], lfo_v[kN], lfo_dv[kN];
  fxu::Shifter shift[2];
  Svf colour[kN];
  float col_mix = 0.0f;
  fxu::Dither dith;
  OnePole dc_in[2];
  // Luce
  fxu::Shifter dark[2];
  fxu::Tpt1 dark_lp[2];
  Sparkle spark;
  fxu::Tpt1 exc_hp[2];
  // Clouds (vendored)
  uint16_t* cl_rev_buf = nullptr;
  uint16_t* cl_dif_buf = nullptr;
  uint16_t* cl_ps_buf[2] = {nullptr, nullptr};
  clouds_revfx::Reverb* cl_rev = nullptr;
  clouds_revfx::Diffuser48k* cl_dif = nullptr;
  clouds_revfx::PitchShifter* cl_ps[2] = {nullptr, nullptr};
  clouds::FloatFrame cl_frames[kBlock], cl_tmp[kBlock], cl_mix[kBlock];
  float cl_tex = 0.0f, cl_up = 0.0f, cl_dn = 0.0f;
  bool cl_used = true;   // Clouds' memory needs a (90 kB) clear only after Clouds has run
  // common
  int type = -1;
  float level = 0.0f, in_gain = 0.5f, ap_g = 0.5f;
  bool inited = false;

  void ClearAll() {
    fdn_filled = 0;   // reads of older frames return 0 until history refills (no 1 MB memset)
    for (int i = 0; i < kN; ++i) { damp_s[i] = 0.0f; colour[i].Reset(); }
    for (int c = 0; c < 2; ++c) {
      memset(pre[c].buf, 0, sizeof(float) * pre[c].size);
      for (int k = 0; k < 4; ++k) memset(ap[c][k].l.buf, 0, sizeof(float) * ap[c][k].l.size);
      shift[c].Clear();
      dark[c].Clear();
      dark_lp[c].Reset();
      exc_hp[c].Reset();
      dc_in[c].z = 0.0f;
    }
    spark.Clear();
    if (cl_rev && cl_used) { cl_rev->Clear(); cl_dif->Clear(); cl_ps[0]->Clear(); cl_ps[1]->Clear(); cl_used = false; }
    for (int i = 0; i < kN; ++i) {   // LFO phasors restart in quadrature-spread positions
      float p = (float)i * 0.125f;
      lfo_c[i] = SinPhase(p + 0.25f);
      lfo_s[i] = SinPhase(p);
      float w = kTwoPi * kLfoHz[i] * kInvSampleRate * kBlock;   // rotation per 64-frame block
      lfo_bc[i] = cosf(w);
      lfo_bs[i] = sinf(w);
      lfo_v[i] = -1.0f;
      lfo_dv[i] = 0.0f;
    }
    type = -1;
  }

  void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v);
  void ProcessClouds(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v);
};

ReverbFx::ReverbFx() : d_(new Impl) { Init(); }
ReverbFx::~ReverbFx() {
  Impl& s = *d_;
  delete[] s.fdn;
  for (int c = 0; c < 2; ++c) {
    s.pre[c].Free();
    for (int k = 0; k < 4; ++k) s.ap[c][k].l.Free();
    s.shift[c].Free();
    s.dark[c].Free();
    delete s.cl_ps[c];
    delete[] s.cl_ps_buf[c];
  }
  s.spark.buf.Free();
  delete s.cl_rev;
  delete s.cl_dif;
  delete[] s.cl_rev_buf;
  delete[] s.cl_dif_buf;
  delete d_;
}

void ReverbFx::Init() {
  Impl& s = *d_;
  if (s.inited) return;
  s.fdn = new float[kLineSz * kN]();
  for (int i = 0; i < kN; ++i) s.len[i] = s.len_t[i] = kBaseLen[i];
  for (int c = 0; c < 2; ++c) {
    s.pre[c].Alloc(4096);
    for (int k = 0; k < 4; ++k) s.ap[c][k].l.Alloc(1024);
    s.shift[c].Alloc();
    s.dark[c].Alloc();
    s.dark[c].ratio = 0.5f;
    s.dark[c].size = c ? 2203.0f : 2257.0f;
    s.cl_ps_buf[c] = new uint16_t[8192]();
    s.cl_ps[c] = new clouds_revfx::PitchShifter();
    s.cl_ps[c]->Init(s.cl_ps_buf[c]);
  }
  s.spark.buf.Alloc(4096);
  s.spark.rnd.Seed(0x51a7c0deu);
  s.cl_rev_buf = new uint16_t[32768]();
  s.cl_dif_buf = new uint16_t[4096]();
  s.cl_rev = new clouds_revfx::Reverb();
  s.cl_dif = new clouds_revfx::Diffuser48k();
  s.cl_rev->Init(s.cl_rev_buf);
  s.cl_dif->Init(s.cl_dif_buf);
  s.ClearAll();
  s.inited = true;
}

void ReverbFx::Clear() { d_->ClearAll(); }

void ReverbFx::Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v) {
  d_->Process(in_l, in_r, out_l, out_r, n, v);
}

void ReverbFx::Impl::Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n,
                             const float* v) {
  const int t = fxu::TypeOf(v[P_REV_TYPE], R_COUNT);
  const float inv_n = 1.0f / (float)n;
  const float lvl_t = Clamp(v[P_REV_LEVEL], 0.0f, 100.0f) * 0.01f;
  if (t != type) level = lvl_t;
  if (t == R_CLOUDS) {
    ProcessClouds(in_l, in_r, out_l, out_r, n, v);
    return;
  }
  const TypeCfg& cfg = kCfg[t];
  const float time = v[P_REV_TIME], input = Clamp(v[P_REV_INPUT], 0.0f, 100.0f) * 0.01f;
  const float bright = v[P_REV_BRIGHT];
  const float rise = Clamp(v[P_REV_RISE], 0.0f, 100.0f) * 0.01f;
  const float dive = Clamp(v[P_REV_DIVE], 0.0f, 100.0f) * 0.01f;

  // ---- line lengths
  if (t == R_LAB) {
    float b = BounceMs(v[P_REV_DIVE]) * 48.0f;
    static const float kSp[4] = {1.0f, 0.93f, 1.07f, 0.87f};
    for (int i = 0; i < kN; ++i) len_t[i] = b * kSp[i & 3] * (i < 4 ? 1.0f : 1.03f);
  } else {
    for (int i = 0; i < kN; ++i) len_t[i] = kBaseLen[i] * cfg.scale;
  }
  if (t != type) {   // new type: snap lengths, set the diffusers, reset gains
    for (int i = 0; i < kN; ++i) len[i] = len_t[i];
    for (int c = 0; c < 2; ++c)
      for (int k = 0; k < 4; ++k) ap[c][k].len = (int)(kDiffLen[c][k] * cfg.diff_scale);
    in_gain = 0.5f + 0.5f * input;
    ap_g = 0.75f * input;
    type = t;
  }
  // ---- decay gains (from the current, possibly gliding, lengths) and damping
  const float rt = RtSec(t, time);
  const float k60 = -6.9077553f / (rt * kSampleRate);
  for (int i = 0; i < kN; ++i) g[i] = expf(k60 * len[i]);
  float damp_hz = DampHz(bright);
  if (t == R_SUB) damp_hz *= 1.0f - 0.75f * dive;
  if (t == R_LUCE) damp_hz = DampHz(bright < 50.0f ? bright : 50.0f + 0.5f * (bright - 50.0f));
  damp_a = OnePoleHz(damp_hz);
  // in-loop pitch shift (Riser up, Submarine down)
  float shift_mix = 0.0f;
  if (t == R_RISER) {
    float semis = 1.0f + 11.0f * rise;     // RISE: interval per pass and amount
    shift_mix = 0.15f + 0.55f * rise;
    shift[0].ratio = shift[1].ratio = exp2f(semis * (1.0f / 12.0f));
    shift[0].size = 2400.0f; shift[1].size = 2160.0f;
  } else if (t == R_SUB) {
    float semis = 0.5f + 11.5f * dive;
    shift_mix = 0.15f + 0.55f * dive;
    shift[0].ratio = shift[1].ratio = exp2f(-semis * (1.0f / 12.0f));
    shift[0].size = 2880.0f; shift[1].size = 2640.0f;
  }
  // Labirinto colour: a resonant band (RISE = resonance amount and pitch), normalised to <= 0 dB
  if (t == R_LAB) {
    col_mix = 0.15f + 0.6f * rise;
    float hz = 300.0f * powf(12.0f, rise);
    for (int i = 0; i < kN; ++i) colour[i].Set(hz * (1.0f + 0.13f * (float)i), 1.6f);
  }
  const float ag_t = 0.75f * input, ig_t = 0.5f + 0.5f * input;
  const float dag = (ag_t - ap_g) * inv_n, dig = (ig_t - in_gain) * inv_n;
  const float dlv = (lvl_t - level) * inv_n;
  const float len_a = 1.0f / (0.08f * kSampleRate);
  const int pre_d = (int)(cfg.predelay * 48.0f);
  const float mod_d = t == R_RISER || t == R_SUB ? cfg.mod * (0.5f + 0.5f * (t == R_RISER ? rise : dive)) : cfg.mod;
  const float dc_a = OnePoleHz(30.0f);
  const float og = cfg.out_gain;
  // Luce paths
  const float dark_amt = t == R_LUCE ? dive * 0.9f : 0.0f;
  const float spark_amt = t == R_LUCE ? rise * 1.0f : 0.0f;
  const float exc_amt = t == R_LUCE && bright > 50.0f ? (bright - 50.0f) * 0.02f * 0.6f : 0.0f;
  if (t == R_LUCE) {
    dark_lp[0].Set(1200.0f); dark_lp[1].Set(1300.0f);
    exc_hp[0].Set(5000.0f); exc_hp[1].Set(5000.0f);
  }

  // the chorus LFOs (< 0.4 Hz) advance once per block; the read offset is interpolated linearly across it
  for (int k = 0; k < kN; ++k) {
    float c = lfo_c[k], sn = lfo_s[k];
    lfo_c[k] = c * lfo_bc[k] - sn * lfo_bs[k];
    lfo_s[k] = sn * lfo_bc[k] + c * lfo_bs[k];
    float target = mod_d * (1.0f + lfo_s[k]);
    if (lfo_v[k] < 0.0f) lfo_v[k] = target;   // first block after Clear
    lfo_dv[k] = (target - lfo_v[k]) * inv_n;
  }
  bool gliding = false;   // only Labirinto's bounce time moves the line lengths
  for (int k = 0; k < kN; ++k) {
    if (fabsf(len_t[k] - len[k]) < 0.01f) len[k] = len_t[k];
    else gliding = true;
  }
  bool fast = true;
  for (int k = 0; k < kN; ++k) fast = fast && fdn_filled > (int)(fmaxf(len[k], len_t[k]) + 2.0f * mod_d) + 2 * kBlock;
  const int fmask = kLineSz - 1;
  // block-local copies: the line writes go through a float*, which would otherwise force every member float to
  // be reloaded each sample (aliasing)
  float L[kN], LT[kN], LV[kN], LDV[kN], DS[kN], G[kN];
  for (int k = 0; k < kN; ++k) { L[k] = len[k]; LT[k] = len_t[k]; LV[k] = lfo_v[k]; LDV[k] = lfo_dv[k]; DS[k] = damp_s[k]; G[k] = g[k]; }
  float* const fdnp = fdn;
  int fw = fdn_w;
  const int ffill = fdn_filled;
  const float da = damp_a;
  float lvl = level, apg = ap_g, ing = in_gain;
  fxu::Dither dth = dith;
  for (int i = 0; i < n; ++i) {
    lvl += dlv;
    apg += dag;
    ing += dig;
    // input: DC block, pre-delay, diffusion
    float xl = dc_in[0].Hp(in_l[i], dc_a) * ing + dth.Next(), xr = dc_in[1].Hp(in_r[i], dc_a) * ing + dth.Next();
    pre[0].WriteU(xl);   // pre-delay and all-pass buffers are small and really zeroed by Clear: unchecked
    pre[1].WriteU(xr);
    if (pre_d > 0) { xl = pre[0].AtU(pre_d); xr = pre[1].AtU(pre_d); }
    for (int k = 0; k < 4; ++k) { xl = ap[0][k].Process(xl, apg); xr = ap[1][k].Process(xr, apg); }
    // read the lines (unchecked once every line holds more history than its longest read)
    float y[kN];
    for (int k = 0; k < kN; ++k) {
      if (gliding) L[k] += (LT[k] - L[k]) * len_a;
      LV[k] += LDV[k];
      float d = L[k] + LV[k];
      int di = (int)d;
      float fr = d - (float)di;
      float a, b;
      if (fast || di + 1 < ffill + i) {
        a = fdnp[((fw - di) & fmask) * kN + k];
        b = fdnp[((fw - di - 1) & fmask) * kN + k];
      } else {
        a = di < ffill + i ? fdnp[((fw - di) & fmask) * kN + k] : 0.0f;
        b = 0.0f;
      }
      float r = a + (b - a) * fr;
      DS[k] += da * (r - DS[k]);
      y[k] = DS[k];
    }
    float ol = (y[0] - y[2] + y[4] - y[6]) * 0.5f + (y[1] + y[5]) * 0.15f;
    float orr = (y[1] - y[3] + y[5] - y[7]) * 0.5f + (y[2] + y[6]) * 0.15f;
    float f[kN];
    for (int k = 0; k < kN; ++k) f[k] = y[k] * G[k];
    if (shift_mix > 0.0f) {
      f[0] += shift_mix * (shift[0].Process(f[0]) - f[0]);
      f[1] += shift_mix * (shift[1].Process(f[1]) - f[1]);
    }
    if (t == R_LAB) {
      for (int k = 0; k < kN; ++k) {
        float lp, bp, hp;
        colour[k].Process(f[k], &lp, &bp, &hp);
        f[k] += col_mix * (bp * colour[k].k - f[k]);
      }
      // two banks, each mixed on its own, written into the other one (ping-pong) with a little bleed
      float a[4] = {f[0], f[1], f[2], f[3]}, b[4] = {f[4], f[5], f[6], f[7]};
      fxu::Fwht4(a);
      fxu::Fwht4(b);
      for (int k = 0; k < 4; ++k) {
        // [[c, s], [s, -c]] (x) H4: orthogonal (0.2431^2 + 0.97^2 = 1), so the loop gain is set by g[] alone
        f[k] = 0.97f * b[k] + 0.2431f * a[k];
        f[k + 4] = 0.97f * a[k] - 0.2431f * b[k];
      }
      f[0] += xl * 0.7f; f[2] -= xl * 0.7f;
      f[4] += xr * 0.7f; f[6] -= xr * 0.7f;
      ol = (y[0] + y[1] - y[2] - y[3]) * 0.5f;
      orr = (y[4] + y[5] - y[6] - y[7]) * 0.5f;
    } else {
      fxu::Fwht8(f);
      f[0] += xl; f[2] -= xl * 0.8f; f[4] += xl * 0.6f; f[6] -= xl * 0.4f;
      f[1] += xr; f[3] -= xr * 0.8f; f[5] += xr * 0.6f; f[7] -= xr * 0.4f;
    }
    // (the input dither above keeps every line out of the denormal range: the mix reaches all of them)
    fw = (fw + 1) & fmask;
    float* wp = fdnp + fw * kN;
    for (int k = 0; k < kN; ++k) wp[k] = fxu::SafeLim(f[k]);
    if (t == R_LUCE) {
      float el = 0.0f, er = 0.0f;
      if (dark_amt > 0.0f) {
        el = dark_lp[0].Lp(SoftClip(dark[0].Process(ol) * 1.3f)) * dark_amt;
        er = dark_lp[1].Lp(SoftClip(dark[1].Process(orr) * 1.3f)) * dark_amt;
      }
      if (spark_amt > 0.0f) {
        float sl, sr;
        spark.Process(0.5f * (ol + orr), rise, &sl, &sr);
        el += sl * spark_amt;
        er += sr * spark_amt;
      }
      if (exc_amt > 0.0f) {
        float hl = exc_hp[0].Hp(ol) * 3.0f, hr = exc_hp[1].Hp(orr) * 3.0f;
        hl = Clamp(hl, -1.0f, 1.0f); hr = Clamp(hr, -1.0f, 1.0f);
        el += (hl - hl * hl * hl * 0.3333f) * exc_amt;
        er += (hr - hr * hr * hr * 0.3333f) * exc_amt;
      }
      ol += el;
      orr += er;
    }
    out_l[i] = fxu::SafeLim(ol * og) * lvl;
    out_r[i] = fxu::SafeLim(orr * og) * lvl;
  }
  for (int k = 0; k < kN; ++k) { len[k] = L[k]; lfo_v[k] = LV[k]; damp_s[k] = DS[k]; }
  fdn_w = fw;
  level = lvl; ap_g = apg; in_gain = ing;
  dith = dth;
  fdn_filled = fdn_filled + n < kLineSz ? fdn_filled + n : kLineSz;
  fxu::Flush(dc_in[0].z);
  fxu::Flush(dc_in[1].z);
  // renormalise the LFO phasors (rounding drift)
  for (int k = 0; k < kN; ++k) {
    float m = lfo_c[k] * lfo_c[k] + lfo_s[k] * lfo_s[k];
    float r = 1.5f - 0.5f * m;
    lfo_c[k] *= r;
    lfo_s[k] *= r;
  }
}

// Clouds parameter mapping (vendored clouds-revfx; original panel name -> DrLogueXXL control):
//   TIME (reverb_time 0..0.99)       <- TIME 0..100 % -> 0.30..0.99
//   IN GAIN (0..0.5) + DIFFUSION      <- INPUT: gain 0.10..0.45, diffusion 0.45..0.75
//   LP DAMP (0.3..0.95)               <- BRIGHT 0..100 % -> 0.30..0.95
//   TEXTURE (diffuser amount)         <- RISE below 50 % (50 -> 0, 0 -> 1)
//   SHIFT AMT at +12 st (octave up)   <- RISE above 50 % (50 -> 0, 100 -> 1)
//   SHIFT SIZE (grain window)         <- DIVE below 50 % (50 -> full 64 ms, 0 -> smallest)
//   SHIFT AMT at -12 st (octave down) <- DIVE above 50 % (a second shifter instance)
//   DRY/WET = 100 % wet; LEVEL scales the return.  Not used: the micro-granular stage and the two LFOs.
void ReverbFx::Impl::ProcessClouds(const float* in_l, const float* in_r, float* out_l, float* out_r, int n,
                                   const float* v) {
  if (type != R_CLOUDS) {
    cl_tex = cl_up = cl_dn = 0.0f;
    type = R_CLOUDS;
  }
  cl_used = true;
  const float time = v[P_REV_TIME], input = Clamp(v[P_REV_INPUT], 0.0f, 100.0f) * 0.01f;
  const float bright = Clamp(v[P_REV_BRIGHT], 0.0f, 100.0f) * 0.01f;
  const float rise = Clamp(v[P_REV_RISE], 0.0f, 100.0f) * 0.01f;
  const float dive = Clamp(v[P_REV_DIVE], 0.0f, 100.0f) * 0.01f;
  cl_rev->set_amount(1.0f);
  cl_rev->set_time(CloudsKrt(time));
  cl_rev->set_input_gain(0.10f + 0.35f * input);
  cl_rev->set_diffusion(0.45f + 0.30f * input);
  cl_rev->set_lp(0.30f + 0.65f * bright);
  float tex_t = rise < 0.5f ? (0.5f - rise) * 2.0f : 0.0f;
  float up_t = rise > 0.5f ? (rise - 0.5f) * 2.0f : 0.0f;
  float dn_t = dive > 0.5f ? (dive - 0.5f) * 2.0f : 0.0f;
  float size = dive < 0.5f ? dive * 2.0f : 1.0f;
  cl_tex += (tex_t - cl_tex) * 0.3f;
  cl_up += (up_t - cl_up) * 0.3f;
  cl_dn += (dn_t - cl_dn) * 0.3f;
  cl_dif->set_amount(cl_tex);
  cl_ps[0]->set_pitch(12.0f);
  cl_ps[1]->set_pitch(-12.0f);
  cl_ps[0]->set_amount(1.0f);
  cl_ps[1]->set_amount(1.0f);
  cl_ps[0]->set_size(size);
  cl_ps[1]->set_size(size);
  const float lvl_t = Clamp(v[P_REV_LEVEL], 0.0f, 100.0f) * 0.01f;
  const float dlv = (lvl_t - level) / (float)n;
  for (int i = 0; i < n; ++i) { cl_frames[i].l = in_l[i]; cl_frames[i].r = in_r[i]; }
  cl_rev->Process(cl_frames, (size_t)n);
  if (cl_tex > 0.001f) cl_dif->Process(cl_frames, (size_t)n);
  const bool up = cl_up > 0.001f, dn = cl_dn > 0.001f;
  if (up || dn) {
    const float ku = up ? cl_up : 0.0f, kd = dn ? cl_dn : 0.0f;
    const float keep = 1.0f - 0.5f * (ku + kd);
    for (int i = 0; i < n; ++i) { cl_mix[i].l = cl_frames[i].l * keep; cl_mix[i].r = cl_frames[i].r * keep; }
    if (up) {
      memcpy(cl_tmp, cl_frames, sizeof(clouds::FloatFrame) * n);
      cl_ps[0]->Process(cl_tmp, (size_t)n);
      for (int i = 0; i < n; ++i) { cl_mix[i].l += cl_tmp[i].l * ku; cl_mix[i].r += cl_tmp[i].r * ku; }
    }
    if (dn) {
      memcpy(cl_tmp, cl_frames, sizeof(clouds::FloatFrame) * n);
      cl_ps[1]->Process(cl_tmp, (size_t)n);
      for (int i = 0; i < n; ++i) { cl_mix[i].l += cl_tmp[i].l * kd; cl_mix[i].r += cl_tmp[i].r * kd; }
    }
    memcpy(cl_frames, cl_mix, sizeof(clouds::FloatFrame) * n);
  }
  for (int i = 0; i < n; ++i) {
    level += dlv;
    out_l[i] = fxu::SafeLim(cl_frames[i].l) * level;
    out_r[i] = fxu::SafeLim(cl_frames[i].r) * level;
  }
}

bool ReverbFx::Format(int param, float value, char* buf, int len) {
  if (param == P_REV_TIME) {
    int t = d_->type < 0 ? R_HALL : d_->type;
    float sec;
    if (t == R_CLOUDS) {   // ~0.24 s per half loop at gain krt
      float k = CloudsKrt(value);
      sec = 0.24f * -3.0f / log10f(k);
    } else {
      sec = RtSec(t, value);
    }
    if (sec < 10.0f) snprintf(buf, len, "%.2f s", sec);
    else snprintf(buf, len, "%.0f s", sec);
    return true;
  }
  if (param == P_REV_BRIGHT && d_->type != R_CLOUDS) { fxu::FormatHz(DampHz(value), buf, len); return true; }
  if (param == P_REV_DIVE && d_->type == R_LAB) { fxu::FormatMs(BounceMs(value), buf, len); return true; }
  return false;
}

}  // namespace dlx
