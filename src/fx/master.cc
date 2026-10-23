// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the MASTER effect (in place on the main bus, with a mono sidechain key).
//
// Thru; Compressor (feed-forward, log-domain soft knee, peak detector on the main bus or, SIDECH On, on the key bus;
// parallel WET mix; GainReductionDb() meter); Filter (TPT state-variable, 2- or 4-pole LP/BP/HP, pre-filter drive);
// Boost (a bell boost whose band is pushed through a saturator -- the exciter of a hardware "boost" pedal);
// EQ Three (Linkwitz-Riley 3-band split, phase-compensated so equal gains are an all-pass: transparent at 0 dB);
// Omnipress (own design after the Eventide Omnipressor, as fedemone's OmniPress unit is: see VENDORED_FX.md).
// Gains ramp per sample, cutoffs glide in log frequency with coefficients refreshed every 8 samples (no zipper).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fx.h"
#include "fx_util.h"

namespace dlx {

namespace {

enum { M_THRU = 0, M_COMP, M_FILTER, M_BOOST, M_EQ, M_OMNI, M_COUNT };
static const int kCoefEvery = 8;

inline float FilterHz(float c) { return 20.0f * powf(1000.0f, Clamp(c, 0.0f, 100.0f) * 0.01f); }      // 20 Hz..20 kHz
inline float BoostHz(float c) { return 30.0f * powf(16000.0f / 30.0f, Clamp(c, 0.0f, 100.0f) * 0.01f); }
inline float XoverHz(float c) { return 50.0f * powf(320.0f, Clamp(c, 0.0f, 100.0f) * 0.01f); }        // 50 Hz..16 kHz
inline float BoostOct(float w) { return 0.3f + 3.7f * Clamp(w, 0.0f, 100.0f) * 0.01f; }               // octaves
inline void CopyCoefs(Svf& d, const Svf& s) { d.g = s.g; d.k = s.k; d.a1 = s.a1; d.a2 = s.a2; d.a3 = s.a3; }
inline float BandDb(float db) { return db <= -63.5f ? -200.0f : db; }   // -64 = kill
// Omnipress slots
inline float OmniThresh(float u) { return -60.0f + 0.6f * Clamp(u, 0.0f, 100.0f); }
inline float OmniFunc(float u) { return -1.0f + 0.03f * Clamp(u, 0.0f, 100.0f); }   // -1 expand .. 0 1:1 .. 1 limit .. 2 reverse
inline float OmniAtk(float u) { return 0.1f * powf(1000.0f, Clamp(u, 0.0f, 100.0f) * 0.01f); }   // 0.1..100 ms
inline float OmniRel(float u) { return 10.0f * powf(200.0f, Clamp(u, 0.0f, 100.0f) * 0.01f); }   // 10..2000 ms

// a one-pole that glides log-frequency, and a counter that says when to refresh coefficients
struct Glide {
  float lg = 10.0f;   // log2(Hz)
  bool init = false;
  float last = -1.0f;   // log2 frequency the coefficients were last computed for
  inline float Step(float target_hz, float a) {
    float t = log2f(target_hz);
    if (!init) { lg = t; init = true; }
    lg += (t - lg) * a;
    return exp2f(lg);
  }
  // true when the coefficients need recomputing (moved by more than ~0.01 cent)
  inline bool Moved() {
    if (fabsf(lg - last) < 1e-5f) return false;
    last = lg;
    return true;
  }
};

// Linkwitz-Riley crossover stage on one channel: low/high split at one frequency, either LR2 or LR4.
struct LrStage {
  Svf lp1, lp2;
  void Reset() { lp1.Reset(); lp2.Reset(); }
  void Set(float hz, bool lr4) {
    lp1.Set(hz, lr4 ? 0.70710678f : 0.5f);
    CopyCoefs(lp2, lp1);
  }
  void CopyFrom(const LrStage& o) { CopyCoefs(lp1, o.lp1); CopyCoefs(lp2, o.lp1); }
  // LR4: lo = LP_bw^2 and, since LP_bw^2 + HP_bw^2 = AP_bw = x - 2k*BP_bw, hi = AP - lo (exact, one SVF saved);
  // LR2: one Q 0.5 SVF gives both, the high band inverted so that lo + hi is the first-order all-pass.
  inline void Split(float x, bool lr4, float* lo, float* hi) {
    float l, b, h;
    lp1.Process(x, &l, &b, &h);
    if (lr4) {
      *lo = lp2.Lp(l);
      *hi = x - 2.0f * lp1.k * b - *lo;
    } else {
      *lo = l;
      *hi = -h;
    }
  }
};
// the all-pass that LP+HP of that crossover make (phase compensation of the band that skips it)
struct LrAllpass {
  Svf f;
  float k2 = 2.0f;
  bool lr4 = false;
  void Reset() { f.Reset(); }
  void Set(float hz, bool lr4_) {
    lr4 = lr4_;
    f.Set(hz, lr4 ? 0.70710678f : 0.5f);
  }
  inline float Process(float x) {
    float l, b, h;
    f.Process(x, &l, &b, &h);
    return lr4 ? x - 2.0f * f.k * b : l - h;   // LR4: 2nd-order AP; LR2: (1-s)/(1+s) via LP - HP at Q 0.5
  }
};

}  // namespace

struct MasterFx::Impl {
  int type = -1;
  bool inited = false;
  // compressor / omnipress
  float env_db = -120.0f, gr_db = 0.0f, gr_meter = 0.0f;
  float wet = 1.0f, makeup = 1.0f;
  float rms = 0.0f;
  // filter
  Svf f1[2], f2[2];
  Glide fglide;
  float fdrive = 1.0f, fq_a = -1.0f, fq_b = -1.0f;
  int fmode = -1;
  // boost
  Svf bell[2];
  Glide bglide;
  float bq = 1.0f, bgain = 0.0f, bsat = 0.0f, bq_set = -1.0f;
  // EQ
  LrStage x1[2], x2[2];
  LrAllpass ap2[2];
  Glide e1, e2;
  float gl = 1.0f, gm = 1.0f, gh = 1.0f, gout = 1.0f;
  int slope = -1;
  // omnipress drive state
  OnePole tube_dc[2];
  float odrive = 0.0f, omix = 1.0f;
  int cnt = 0;

  void ClearAll() {
    env_db = -120.0f; gr_db = 0.0f; gr_meter = 0.0f; rms = 0.0f;
    for (int c = 0; c < 2; ++c) {
      f1[c].Reset(); f2[c].Reset(); bell[c].Reset();
      x1[c].Reset(); x2[c].Reset(); ap2[c].Reset();
      tube_dc[c].z = 0.0f;
    }
    fglide.init = bglide.init = e1.init = e2.init = false;
    fglide.last = bglide.last = e1.last = e2.last = -1.0f;
    slope = -1;
    fq_a = fq_b = -1.0f;
    fmode = -1;
    bq_set = -1.0f;
    cnt = 0;
    type = -1;
  }
  void Comp(float* l, float* r, const float* key, int n, const float* v);
  void Filter(float* l, float* r, int n, const float* v);
  void Boost(float* l, float* r, int n, const float* v);
  void Eq(float* l, float* r, int n, const float* v);
  void Omni(float* l, float* r, const float* key, int n, const float* v);
};

MasterFx::MasterFx() : d_(new Impl) { Init(); }
MasterFx::~MasterFx() { delete d_; }
void MasterFx::Init() {
  if (d_->inited) return;
  d_->ClearAll();
  d_->inited = true;
}
void MasterFx::Clear() { d_->ClearAll(); }

void MasterFx::Process(float* l, float* r, const float* key, int n, const float* v) {
  Impl& s = *d_;
  int t = fxu::TypeOf(v[P_MFX_TYPE], M_COUNT);
  if (t != s.type) {
    s.ClearAll();
    s.type = t;
    // start smoothed values at their targets so a type switch doesn't swoop
    s.wet = Clamp(v[P_COMP_WET], 0.0f, 100.0f) * 0.01f;
    s.makeup = DbToGain(Clamp(v[P_COMP_MAKEUP], 0.0f, 8.0f));
    s.bgain = DbToGain(Clamp(v[P_BOOST_GAIN], 0.0f, 12.0f)) - 1.0f;
    s.bsat = Clamp(v[P_BOOST_SATURATE], 0.0f, 100.0f) * 0.01f;
    s.gl = DbToGain(BandDb(v[P_EQ_LOW]));
    s.gm = DbToGain(BandDb(v[P_EQ_MID]));
    s.gh = DbToGain(BandDb(v[P_EQ_HIGH]));
    s.gout = DbToGain(Clamp(v[P_EQ_GAIN], -6.0f, 6.0f));
    s.fdrive = Clamp(v[P_MF_SATURATE], 0.0f, 100.0f) * 0.01f;
    s.odrive = Clamp(v[P_MFX_U5], 0.0f, 100.0f) * 0.01f;
    s.omix = Clamp(v[P_MFX_U6], 0.0f, 100.0f) * 0.01f;
  }
  switch (t) {
    case M_COMP: s.Comp(l, r, key, n, v); break;
    case M_FILTER: s.Filter(l, r, n, v); break;
    case M_BOOST: s.Boost(l, r, n, v); break;
    case M_EQ: s.Eq(l, r, n, v); break;
    case M_OMNI: s.Omni(l, r, key, n, v); break;
    default: s.gr_meter = 0.0f; break;
  }
  // keep decaying filter states out of the denormal range (x86; the device flushes to zero anyway)
  for (int c = 0; c < 2; ++c) {
    fxu::FlushSvf(s.f1[c]); fxu::FlushSvf(s.f2[c]); fxu::FlushSvf(s.bell[c]); fxu::FlushSvf(s.ap2[c].f);
    fxu::FlushSvf(s.x1[c].lp1); fxu::FlushSvf(s.x1[c].lp2); fxu::FlushSvf(s.x2[c].lp1); fxu::FlushSvf(s.x2[c].lp2);
    fxu::Flush(s.tube_dc[c].z);
  }
  fxu::Flush(s.rms);
}

// ------------------------------------------------------------------------------------------------ Compressor
void MasterFx::Impl::Comp(float* l, float* r, const float* key, int n, const float* v) {
  const float thr = Clamp(v[P_COMP_THRESH], -64.0f, 0.0f);
  const float ratio = Clamp(v[P_COMP_RATIO], 1.0f, 10.0f);
  const float knee = Clamp(v[P_COMP_KNEE], 0.0f, 8.0f);
  const float slope = 1.0f / ratio - 1.0f;   // dB of gain per dB over the threshold (<= 0)
  const float atk = 1.0f - expf(-1.0f / (Clamp(v[P_COMP_ATTACK], 0.5f, 100.0f) * 0.001f * kSampleRate));
  const float rel = 1.0f - expf(-1.0f / (Clamp(v[P_COMP_RELEASE], 50.0f, 1000.0f) * 0.001f * kSampleRate));
  const bool sc = v[P_COMP_SIDECH] >= 0.5f && key != nullptr;
  const float inv_n = 1.0f / (float)n;
  const float wet_t = Clamp(v[P_COMP_WET], 0.0f, 100.0f) * 0.01f;
  const float mk_t = DbToGain(Clamp(v[P_COMP_MAKEUP], 0.0f, 8.0f));
  const float dw = (wet_t - wet) * inv_n, dm = (mk_t - makeup) * inv_n;
  const float hk = 0.5f * knee;
  float maxgr = 0.0f;
  for (int i = 0; i < n; ++i) {
    wet += dw;
    makeup += dm;
    float det = sc ? fabsf(key[i]) : fmaxf(fabsf(l[i]), fabsf(r[i]));
    float x = fxu::FastDb(det);
    float o = x - thr, tgt;
    if (o <= -hk) tgt = 0.0f;
    else if (o < hk) { float u = o + hk; tgt = slope * u * u / (2.0f * knee); }
    else tgt = slope * o;
    // attack while the reduction grows, release while it shrinks (smoothing in dB)
    gr_db += (tgt - gr_db) * (tgt < gr_db ? atk : rel);
    if (gr_db < maxgr) maxgr = gr_db;
    float g = fxu::FastDbToGain(gr_db) * makeup;
    float m = 1.0f - wet + wet * g;
    l[i] *= m;
    r[i] *= m;
  }
  if (gr_db > -1e-6f) gr_db = 0.0f;
  gr_meter = maxgr;
}

// ------------------------------------------------------------------------------------------------ Filter
void MasterFx::Impl::Filter(float* l, float* r, int n, const float* v) {
  const int mode = (int)(v[P_MF_MODE] + 0.5f);
  gr_meter = 0.0f;
  if (mode <= 0 || mode > 6) return;
  const bool four = mode == 2 || mode == 4 || mode == 6;
  const int kind = (mode - 1) / 2;   // 0 lp, 1 bp, 2 hp
  const float res = Clamp(v[P_MF_RESON], 0.0f, 100.0f) * 0.01f;
  // Q: 2-pole 0.5..18; 4-pole: first stage fixed Butterworth-ish, the resonance in the second
  const float q2 = 0.5f + 17.5f * res * res;
  const float q4a = kind == 1 ? 0.7f : 0.54f, q4b = 0.7f + 14.0f * res * res;
  const float target = FilterHz(v[P_MF_CUTOFF]);
  const float ga = 1.0f - expf(-1.0f / (0.012f * kSampleRate / kCoefEvery));
  const float sat_t = Clamp(v[P_MF_SATURATE], 0.0f, 100.0f) * 0.01f;
  const float dsat = (sat_t - fdrive) / (float)n;
  for (int i = 0; i < n; ++i) {
    if (cnt-- <= 0) {
      cnt = kCoefEvery - 1;
      float hz = fglide.Step(target, ga);
      if (kind == 2) hz = fminf(hz, 18000.0f);
      float qa = four ? q4a : q2;
      if (fglide.Moved() || qa != fq_a || q4b != fq_b || mode != fmode) {
        fq_a = qa; fq_b = q4b; fmode = mode;
        f1[0].Set(hz, qa); CopyCoefs(f1[1], f1[0]);
        if (four) { f2[0].Set(hz, q4b); CopyCoefs(f2[1], f2[0]); }
      }
    }
    fdrive += dsat;
    float x[2] = {l[i], r[i]};
    for (int c = 0; c < 2; ++c) {
      float y = x[c];
      if (fdrive > 0.001f) {
        float d = 1.0f + 11.0f * fdrive * fdrive;
        y = SoftClip(y * d) / (1.0f + 0.45f * (d - 1.0f));
      }
      float lo, bp, hi;
      if (four) {
        f1[c].Process(y, &lo, &bp, &hi);
        y = kind == 0 ? lo : (kind == 1 ? bp * f1[c].k : hi);
        f2[c].Process(y, &lo, &bp, &hi);
        y = kind == 0 ? lo : (kind == 1 ? bp * f2[c].k : hi);   // band-pass at unity peak
      } else {
        f1[c].Process(y, &lo, &bp, &hi);
        y = kind == 0 ? lo : (kind == 1 ? bp * f1[c].k : hi);
      }
      // the resonant peak (up to ~25 dB) is folded back softly above 0 dBFS, like the hardware filter
      x[c] = res > 0.5f ? 2.0f * SoftClip(0.5f * y) : y;
    }
    l[i] = x[0];
    r[i] = x[1];
  }
}

// ------------------------------------------------------------------------------------------------ Boost
void MasterFx::Impl::Boost(float* l, float* r, int n, const float* v) {
  gr_meter = 0.0f;
  const float target = BoostHz(v[P_BOOST_CENTER]);
  const float bw = BoostOct(v[P_BOOST_WIDTH]);
  const float q = 1.0f / (2.0f * sinhf(0.34657359f * bw));
  const float ga = 1.0f - expf(-1.0f / (0.012f * kSampleRate / kCoefEvery));
  const float g_t = DbToGain(Clamp(v[P_BOOST_GAIN], 0.0f, 12.0f)) - 1.0f;
  const float s_t = Clamp(v[P_BOOST_SATURATE], 0.0f, 100.0f) * 0.01f;
  const float inv_n = 1.0f / (float)n;
  const float dg = (g_t - bgain) * inv_n, ds = (s_t - bsat) * inv_n;
  bq += (q - bq) * 0.3f;
  for (int i = 0; i < n; ++i) {
    if (cnt-- <= 0) {
      cnt = kCoefEvery - 1;
      float hz = fminf(bglide.Step(target, ga), 20000.0f);
      if (bglide.Moved() || fabsf(bq - bq_set) > 1e-4f) {
        bq_set = bq;
        bell[0].Set(hz, bq);
        CopyCoefs(bell[1], bell[0]);
      }
    }
    bgain += dg;
    bsat += ds;
    float x[2] = {l[i], r[i]};
    for (int c = 0; c < 2; ++c) {
      float lo, bp, hi;
      bell[c].Process(x[c], &lo, &bp, &hi);
      float band = bp * bell[c].k;                     // unity-peak band-pass
      if (bsat > 0.001f) {                             // exciter: the band itself is driven into a saturator
        float d = 1.0f + 9.0f * bsat;
        float sb = SoftClip(band * d) / d;
        band += bsat * (sb * (1.0f + 0.6f * bsat * (d - 1.0f)) - band);
      }
      // the boosted part itself saturates near full scale (the pedal's op-amp): bounded, never a hard clip
      float y = x[c] + 1.5f * SoftClip(bgain * band * (1.0f / 1.5f));
      if (bsat > 0.001f) {                             // and a touch of the whole signal, level-matched
        float d = 1.0f + 2.0f * bsat;
        y += bsat * 0.5f * (SoftClip(y * d) / d * (1.0f + 0.3f * (d - 1.0f)) - y);
      }
      x[c] = y;
    }
    l[i] = x[0];
    r[i] = x[1];
  }
}

// ------------------------------------------------------------------------------------------------ EQ Three
void MasterFx::Impl::Eq(float* l, float* r, int n, const float* v) {
  gr_meter = 0.0f;
  const bool lr4 = v[P_EQ_SLOPE] >= 0.5f;
  const int sl = lr4 ? 1 : 0;
  if (sl != slope) {
    for (int c = 0; c < 2; ++c) { x1[c].Reset(); x2[c].Reset(); ap2[c].Reset(); }
    slope = sl;
    e1.last = e2.last = -1.0f;   // recompute for the new slope
  }
  const float h1 = XoverHz(v[P_EQ_LOWMID]), h2 = XoverHz(v[P_EQ_MIDHIGH]);
  const float ga = 1.0f - expf(-1.0f / (0.015f * kSampleRate / kCoefEvery));
  const float inv_n = 1.0f / (float)n;
  const float gl_t = DbToGain(BandDb(v[P_EQ_LOW])), gm_t = DbToGain(BandDb(v[P_EQ_MID]));
  const float gh_t = DbToGain(BandDb(v[P_EQ_HIGH])), go_t = DbToGain(Clamp(v[P_EQ_GAIN], -6.0f, 6.0f));
  const float dl = (gl_t - gl) * inv_n, dm = (gm_t - gm) * inv_n, dh = (gh_t - gh) * inv_n, dgo = (go_t - gout) * inv_n;
  for (int i = 0; i < n; ++i) {
    if (cnt-- <= 0) {
      cnt = kCoefEvery - 1;
      float a = e1.Step(h1, ga), b = e2.Step(h2, ga);
      if (e1.Moved()) { x1[0].Set(a, lr4); x1[1].CopyFrom(x1[0]); }
      if (e2.Moved()) {
        x2[0].Set(b, lr4); x2[1].CopyFrom(x2[0]);
        ap2[0].Set(b, lr4); ap2[1].lr4 = lr4; CopyCoefs(ap2[1].f, ap2[0].f);
      }
    }
    gl += dl; gm += dm; gh += dh; gout += dgo;
    float x[2] = {l[i], r[i]};
    for (int c = 0; c < 2; ++c) {
      float lo, rest, mid, hi;
      x1[c].Split(x[c], lr4, &lo, &rest);
      x2[c].Split(rest, lr4, &mid, &hi);
      lo = ap2[c].Process(lo);
      x[c] = (lo * gl + mid * gm + hi * gh) * gout;
    }
    l[i] = x[0];
    r[i] = x[1];
  }
}

// ------------------------------------------------------------------------------------------------ Omnipress
// Slots: 1 THRESH (-60..0 dB pivot), 2 FUNCTION (0 = 2:1 expansion, 33 = 1:1, 50 = 2:1, 67 = limit, 100 =
// reverse), 3 ATTACK (0.1..100 ms), 4 RELEASE (10..2000 ms), 5 DRIVE (tube-style asymmetric saturation after the
// dynamics, level-matched), 6 MIX (dry/wet). The function applies on both sides of the pivot, so compression also
// lifts quiet material toward the threshold (bounded at +12 dB of lift and -30 dB of cut, and no lift below
// -70 dBFS). Detector: RMS-ish (5 ms) on the main bus; SIDECH On (the compressor's switch) keys it from the key bus.
void MasterFx::Impl::Omni(float* l, float* r, const float* key, int n, const float* v) {
  const float thr = OmniThresh(v[P_MFX_U1]);
  const float f = OmniFunc(v[P_MFX_U2]);
  const float atk = 1.0f - expf(-1.0f / (OmniAtk(v[P_MFX_U3]) * 0.001f * kSampleRate));
  const float rel = 1.0f - expf(-1.0f / (OmniRel(v[P_MFX_U4]) * 0.001f * kSampleRate));
  const bool sc = v[P_COMP_SIDECH] >= 0.5f && key != nullptr;
  const float inv_n = 1.0f / (float)n;
  const float dr_t = Clamp(v[P_MFX_U5], 0.0f, 100.0f) * 0.01f, mx_t = Clamp(v[P_MFX_U6], 0.0f, 100.0f) * 0.01f;
  const float ddr = (dr_t - odrive) * inv_n, dmx = (mx_t - omix) * inv_n;
  const float rms_a = 1.0f - expf(-1.0f / (0.005f * kSampleRate));
  const float dc_a = OnePoleHz(8.0f);
  float maxgr = 0.0f;
  for (int i = 0; i < n; ++i) {
    odrive += ddr;
    omix += dmx;
    float det = sc ? key[i] * key[i] : 0.5f * (l[i] * l[i] + r[i] * r[i]);
    rms += (det - rms) * rms_a;
    float x = 0.5f * fxu::FastDb(rms + 1e-14f);   // power -> dB
    float o = x - thr;
    float tgt = -f * o;
    if (tgt > 12.0f) tgt = 12.0f;
    if (tgt < -30.0f) tgt = -30.0f;
    if (tgt > 0.0f && x < -70.0f) tgt *= fmaxf(0.0f, 1.0f + (x + 70.0f) * 0.1f);   // no lift on silence
    gr_db += (tgt - gr_db) * (tgt < gr_db ? atk : rel);
    if (gr_db < maxgr) maxgr = gr_db;
    float g = fxu::FastDbToGain(gr_db);
    float y[2] = {l[i] * g, r[i] * g};
    if (odrive > 0.001f) {
      float d = 1.0f + 7.0f * odrive;
      float comp = 1.0f / (1.0f + 0.55f * (d - 1.0f));
      for (int c = 0; c < 2; ++c) {
        float u = y[c] * d + 0.12f * odrive;              // bias = even harmonics, removed by the DC blocker
        float s = SoftClip(u) - SoftClip(0.12f * odrive);
        y[c] = tube_dc[c].Hp(s * comp * (1.0f + 0.25f * odrive), dc_a);
      }
    }
    l[i] += omix * (y[0] - l[i]);
    r[i] += omix * (y[1] - r[i]);
  }
  gr_meter = maxgr;
}

// ------------------------------------------------------------------------------------------------ UI helpers
const char* MasterFx::SlotName(int i, const float* v) const {
  static const char* const kOmni[6] = {"THRESH", "FUNCTION", "ATTACK", "RELEASE", "DRIVE", "MIX"};
  if (i < 0 || i > 5 || !v) return NULL;
  if (fxu::TypeOf(v[P_MFX_TYPE], M_COUNT) == M_OMNI) return kOmni[i];
  return NULL;
}

float MasterFx::GainReductionDb() const { return d_->gr_meter; }

bool MasterFx::Format(int param, float value, char* buf, int len) {
  switch (param) {
    case P_COMP_ATTACK: case P_COMP_RELEASE: fxu::FormatMs(value, buf, len); return true;
    case P_COMP_RATIO:
      if (value >= 9.95f) snprintf(buf, len, "10:1");
      else snprintf(buf, len, "%.1f:1", value);
      return true;
    case P_MF_CUTOFF: fxu::FormatHz(FilterHz(value), buf, len); return true;
    case P_BOOST_CENTER: fxu::FormatHz(BoostHz(value), buf, len); return true;
    case P_BOOST_WIDTH: snprintf(buf, len, "%.1f oct", BoostOct(value)); return true;
    case P_EQ_LOWMID: case P_EQ_MIDHIGH: fxu::FormatHz(XoverHz(value), buf, len); return true;
    case P_EQ_LOW: case P_EQ_MID: case P_EQ_HIGH:
      if (value <= -63.5f) snprintf(buf, len, "KILL");
      else snprintf(buf, len, "%+.1f dB", value);
      return true;
    default: break;
  }
  if (param >= P_MFX_U1 && param <= P_MFX_U6 && d_->type == M_OMNI) {
    switch (param - P_MFX_U1) {
      case 0: snprintf(buf, len, "%.1f dB", OmniThresh(value)); return true;
      case 1: {
        float f = OmniFunc(value);
        if (fabsf(f) < 0.02f) snprintf(buf, len, "1:1");
        else if (f < 0.0f) snprintf(buf, len, "1:%.1f EXP", 1.0f - f);
        else if (f < 0.98f) snprintf(buf, len, "%.1f:1", 1.0f / (1.0f - f));
        else if (f < 1.02f) snprintf(buf, len, "LIMIT");
        else snprintf(buf, len, "REV %.0f%%", (f - 1.0f) * 100.0f);
        return true;
      }
      case 2: fxu::FormatMs(OmniAtk(value), buf, len); return true;
      case 3: fxu::FormatMs(OmniRel(value), buf, len); return true;
      default: return false;
    }
  }
  return false;
}

}  // namespace dlx
