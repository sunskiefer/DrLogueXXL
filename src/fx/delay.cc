// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the send DELAY.
//
// Types: Stereo, Stereo BPM, Mono, Mono BPM, Tape, Tape BPM, Tribal, Cassette.
//   Stereo / Mono (+BPM): a clean digital line pair (mono: one line, output centred, SPREAD pans it). Time changes
//     crossfade between two read heads (20 ms), so knob moves never click.
//   Tape (+BPM): a modulated line (MOD = wow + flutter + drift), TRACKING = how the playback head follows a time
//     change (0: crossfade jump, >0: slewed head = varispeed pitch glide, 100: ~1 s glide), TONE = darkening in the
//     loop + a head bump on the output (more bump when darker), tape saturation.
//   Tribal (own design, see VENDORED_FX.md): a multi-tap percussion spatialiser. 2..10 "clone" taps on an
//     irregular grid derived from the BPM division; each clone later, quieter, darker, panned outwards and wobbling
//     in time per hit, like extra drummers behind the leader.
//   Cassette (own design): a portastudio cassette echo -- narrow bandwidth, heavy wow/flutter, asymmetric tape
//     saturation that is always a little on, a strong head bump and some L/R crosstalk.
// In-loop chain on the line input: (input path saturation) + FB * routed return -> loop saturation (gain <= 1)
// -> FILTER (LP/HP SVF, Q 0.707, never > 0 dB) -> tape tone -> DC block -> line. FB 100 % = 0.995 loop gain.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fx.h"
#include "fx_util.h"

namespace dlx {

namespace {

enum { T_STEREO = 0, T_STEREO_BPM, T_MONO, T_MONO_BPM, T_TAPE, T_TAPE_BPM, T_TRIBAL, T_CASSETTE, T_COUNT };
static const float kDivWhole[16] = {1.0f / 64, 1.0f / 48, 1.0f / 32, 1.0f / 24, 1.0f / 16, 1.0f / 12, 3.0f / 32,
                                    1.0f / 8,  1.0f / 6,  3.0f / 16, 1.0f / 4,  1.0f / 3,  3.0f / 8,  1.0f / 2,
                                    3.0f / 4,  1.0f};
static const int kLineSize = 1 << 17;                    // 2.73 s at 48 kHz
static const float kMaxDelay = 2.6f * 48000.0f;          // longest time any type asks for
static const float kMinDelay = 48.0f;                    // 1 ms
static const int kFadeLen = 960;                         // 20 ms head crossfade
static const int kTaps = 10;
// Tribal grid, in quarters of the division (so 1/4 -> sixteenths), in the order clones are added, and their levels
static const float kTribalPos[kTaps] = {1.0f, 1.5f, 3.0f, 4.0f, 2.5f, 5.5f, 6.0f, 3.5f, 7.0f, 4.5f};
static const float kTribalGain[kTaps] = {0.85f, 0.6f, 0.72f, 0.66f, 0.5f, 0.42f, 0.5f, 0.38f, 0.36f, 0.32f};

inline float TimeMs(float t) { return powf(2000.0f, Clamp(t, 0.0f, 100.0f) * 0.01f); }   // 1 ms .. 2 s
inline float DivMs(int div, float bpm) {
  if (bpm <= 0.0f) bpm = 120.0f;
  bpm = Clamp(bpm, 20.0f, 999.0f);
  return kDivWhole[div < 0 ? 0 : (div > 15 ? 15 : div)] * 4.0f * 60000.0f / bpm;
}
inline float CutoffHz(float c) { return 20.0f * powf(1000.0f, Clamp(c, 0.0f, 100.0f) * 0.01f); }

// Two read heads: a time change jumps the idle head to the new time and crossfades to it (no click); in glide
// mode only head 0 is used and it is slewed (varispeed).
struct Heads {
  float d[2] = {4800.0f, 4800.0f};
  float w = 0.0f;       // weight of head 1
  int active = 0;
  void Reset(float t) { d[0] = d[1] = t; w = 0.0f; active = 0; }
  inline bool Fading() const { return active ? w < 1.0f : w > 0.0f; }
  inline void Jump(float target) {
    if (Fading() || fabsf(target - d[active]) < 0.5f) return;
    active ^= 1;
    d[active] = target;
  }
  inline void StepFade() {
    const float s = 1.0f / kFadeLen;
    if (active) { w += s; if (w > 1.0f) w = 1.0f; }
    else { w -= s; if (w < 0.0f) w = 0.0f; }
  }
  inline float Read(const fxu::Line& l, float mod) const {
    if (w <= 0.0f) return l.Herm(d[0] + mod);
    if (w >= 1.0f) return l.Herm(d[1] + mod);
    return l.Herm(d[0] + mod) * (1.0f - w) + l.Herm(d[1] + mod) * w;
  }
};

struct Chan {
  fxu::Line line;
  Heads heads;
  Svf filt;
  fxu::Tpt1 tone;      // tape darkening in the loop
  OnePole dc;          // DC / rumble block in the loop
  Svf bump;            // head bump on the output
  void Clear(float t) {
    line.Clear();
    heads.Reset(t);
    filt.Reset();
    tone.Reset();
    dc.z = 0.0f;
    bump.Reset();
  }
};

}  // namespace

struct DelayFx::Impl {
  Chan ch[2];
  fxu::Dither dith;
  Noise rnd;
  int type = -1;
  bool inited = false;
  // smoothed controls
  float fb = 0.0f, level = 0.0f, cut = 50.0f, panl = 0.7071f, panr = 0.7071f;
  int filt_mode = 0;
  // tape modulation
  float wow_ph = 0.0f, flut_ph = 0.0f, drift = 0.0f, drift_t = 0.0f;
  int drift_cnt = 0;
  // tribal
  float unit = 4800.0f;                // grid unit in samples (slewed)
  float tap_off[kTaps], tap_off_t[kTaps];
  float tap_lp[kTaps][2];
  float tap_gl[kTaps], tap_gr[kTaps];
  int wobble_cnt = 0;
  float last_tribal_in = 0.0f;
  // cassette crosstalk
  float xtalk = 0.0f;

  void ClearAll() {
    float t = 0.5f * 48000.0f;
    ch[0].Clear(t);
    ch[1].Clear(t);
    wow_ph = 0.0f; flut_ph = 0.25f; drift = drift_t = 0.0f; drift_cnt = 0;
    for (int k = 0; k < kTaps; ++k) { tap_off[k] = tap_off_t[k] = 0.0f; tap_lp[k][0] = tap_lp[k][1] = 0.0f; }
    wobble_cnt = 0;
    last_tribal_in = 0.0f;
    type = -1;   // re-snap times on the next block
  }
};

DelayFx::DelayFx() : d_(new Impl) { Init(); }
DelayFx::~DelayFx() {
  d_->ch[0].line.Free();
  d_->ch[1].line.Free();
  delete d_;
}

void DelayFx::Init() {
  if (d_->inited) return;
  d_->ch[0].line.Alloc(kLineSize);
  d_->ch[1].line.Alloc(kLineSize);
  d_->rnd.Seed(0x5eed1234u);
  d_->ClearAll();
  d_->inited = true;
}

void DelayFx::Clear() { d_->ClearAll(); }

void DelayFx::Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n, const float* v,
                      float bpm) {
  Impl& s = *d_;
  const int type = fxu::TypeOf(v[P_DLY_TYPE], T_COUNT);
  const bool bpm_type = type == T_STEREO_BPM || type == T_MONO_BPM || type == T_TAPE_BPM || type == T_TRIBAL;
  const bool mono = type == T_MONO || type == T_MONO_BPM;
  const bool tape = type == T_TAPE || type == T_TAPE_BPM || type == T_CASSETTE;
  const bool cass = type == T_CASSETTE;
  const float inv_n = 1.0f / (float)n;

  // ---- time
  float ms = bpm_type ? DivMs((int)(v[P_DLY_DIV] + 0.5f), bpm) : TimeMs(v[P_DLY_TIME]);
  float base = Clamp(ms * 48.0f, kMinDelay, kMaxDelay);
  const float spread = Clamp(v[P_DLY_SPREAD], -100.0f, 100.0f) * 0.01f;
  float tl = base, tr = base;
  if (!mono && type != T_TRIBAL) {   // stereo types: SPREAD offsets the L/R times (+: left earlier)
    tl = Clamp(base * (1.0f - 0.25f * spread), kMinDelay, kMaxDelay);
    tr = Clamp(base * (1.0f + 0.25f * spread), kMinDelay, kMaxDelay);
  }
  if (s.type != type) {   // first block of a type: snap, no glide from the old type's time
    s.ch[0].heads.Reset(tl);
    s.ch[1].heads.Reset(tr);
    s.unit = Clamp(base * 0.25f, 12.0f, kMaxDelay / 7.5f);
    s.fb = Clamp(v[P_DLY_FB], 0.0f, 100.0f) * 0.00995f;
    s.level = Clamp(v[P_DLY_LEVEL], 0.0f, 100.0f) * 0.01f;
    s.cut = v[P_DLY_CUTOFF];
    s.type = type;
  }
  // ---- controls (targets, ramped across the block)
  const float fb_t = Clamp(v[P_DLY_FB], 0.0f, 100.0f) * 0.00995f;
  const float lvl_t = Clamp(v[P_DLY_LEVEL], 0.0f, 100.0f) * 0.01f;
  const float dfb = (fb_t - s.fb) * inv_n, dlv = (lvl_t - s.level) * inv_n;
  const float sat = Clamp(v[P_DLY_SAT], 0.0f, 100.0f) * 0.01f + (cass ? 0.25f : (tape ? 0.08f : 0.0f));
  const float in_drive = 1.0f + 6.0f * sat;
  const float in_norm = sat > 0.0f ? 0.5f / SoftClip(0.5f * in_drive) : 1.0f;
  const float loop_g = 1.0f + 3.0f * sat;
  const float loop_inv = 1.0f / loop_g;
  const float asym = cass ? 0.15f : (tape ? 0.06f : 0.0f);   // tape curve is a little asymmetric
  // filter
  int fm = (int)(v[P_DLY_FILTER] + 0.5f);
  if (fm != s.filt_mode) { s.ch[0].filt.Reset(); s.ch[1].filt.Reset(); s.filt_mode = fm; }
  s.cut += (v[P_DLY_CUTOFF] - s.cut) * 0.35f;
  if (fm) { float hz = CutoffHz(s.cut); s.ch[0].filt.Set(hz, 0.7071f); s.ch[1].filt.Set(hz, 0.7071f); }
  // tape tone + bump
  const float tone = Clamp(v[P_DLY_TONE], 0.0f, 100.0f) * 0.01f;
  float bump_g = 0.0f;
  if (tape || type == T_TRIBAL) {
    float lp = cass ? 1800.0f * powf(5.0f, tone) : 2500.0f * powf(6.4f, tone);
    if (type == T_TRIBAL) lp = 3000.0f * powf(6.0f, tone);
    s.ch[0].tone.Set(lp);
    s.ch[1].tone.Set(lp);
    if (tape) {
      float db = (cass ? 4.0f : 2.5f) * (1.0f - 0.7f * tone);
      bump_g = DbToGain(db) - 1.0f;
      s.ch[0].bump.Set(cass ? 110.0f : 85.0f, 1.0f);
      s.ch[1].bump.Set(cass ? 110.0f : 85.0f, 1.0f);
    }
  }
  const float dc_a = OnePoleHz(cass ? 70.0f : (tape ? 35.0f : 12.0f));
  const int route = type == T_TRIBAL || mono ? 0 : (int)(v[P_DLY_ROUTE] + 0.5f);
  // mono output pan (SPREAD), equal power
  if (mono || type == T_TRIBAL) {
    float p = (spread + 1.0f) * 0.25f * kPi;
    s.panl = cosf(p);
    s.panr = sinf(p);
  }
  // tape: tracking
  const float trk = Clamp(v[P_DLY_TRACKING], 0.0f, 100.0f) * 0.01f;
  const bool glide = tape && trk > 0.02f;
  const float glide_a = glide ? 1.0f / ((0.004f + 1.0f * trk * trk) * 48000.0f) : 0.0f;
  // tape: modulation depths (samples) and rates
  const float mod = Clamp(v[P_DLY_MOD], 0.0f, 100.0f) * 0.01f;
  float wow_d = 0.0f, flut_d = 0.0f, drift_d = 0.0f, wow_inc = 0.0f, flut_inc = 0.0f;
  if (tape) {
    wow_d = mod * (cass ? 110.0f : 55.0f);
    flut_d = mod * (cass ? 3.0f : 1.4f);
    drift_d = mod * (cass ? 40.0f : 20.0f);
    wow_inc = (cass ? 0.9f : 0.55f) * kInvSampleRate;
    flut_inc = (cass ? 11.0f : 6.5f) * kInvSampleRate;
  }
  s.xtalk = cass ? 0.06f : 0.0f;

  if (type == T_TRIBAL) {
    // ---------------------------------------------------------------- Tribal
    int clones = 2 + 2 * (int)(trk * 4.0f + 0.5f);   // TRACKING -> 2/4/6/8/10 clones
    if (clones > kTaps) clones = kTaps;
    float unit_t = Clamp(base * 0.25f, 12.0f, kMaxDelay / 7.5f);
    float unit_a = 1.0f / (0.03f * 48000.0f);
    // per-hit timing wobble (MOD): every ~80 ms each clone picks a new offset, smoothed
    if (--s.wobble_cnt <= 0) {
      s.wobble_cnt = 60;
      for (int k = 0; k < kTaps; ++k) s.tap_off_t[k] = s.rnd.Next() * mod * 288.0f;   // up to 6 ms
    }
    float tone_k = 0.55f + 0.4f * tone;
    float tl_acc = 1.0f;
    float norm = 0.0f;
    for (int k = 0; k < clones; ++k) norm += kTribalGain[k];
    norm = 1.2f / (norm > 1.0f ? sqrtf(norm) * 1.3f : 1.0f);
    for (int k = 0; k < kTaps; ++k) {
      tl_acc *= tone_k;
      float a = OnePoleHz(18000.0f * tl_acc + 400.0f);
      s.tap_lp[k][1] = a;   // [1] holds the coefficient, [0] the state
      float g = k < clones ? kTribalGain[k] * norm : 0.0f;
      float pan = spread * ((k & 1) ? 1.0f : -1.0f) * (0.35f + 0.65f * (float)(k + 1) / (float)clones);
      pan = Clamp(pan, -1.0f, 1.0f);
      float p = (pan + 1.0f) * 0.25f * kPi;
      s.tap_gl[k] = g * cosf(p) * 1.4142f;
      s.tap_gr[k] = g * sinf(p) * 1.4142f;
    }
    // the clone that closes the loop: the latest enabled one
    int last = 0;
    for (int k = 1; k < clones; ++k) if (kTribalPos[k] > kTribalPos[last]) last = k;
    fxu::Line& line = s.ch[0].line;
    Chan& c = s.ch[0];
    for (int i = 0; i < n; ++i) {
      s.fb += dfb;
      s.level += dlv;
      s.unit += (unit_t - s.unit) * unit_a;
      float x = 0.5f * (in_l[i] + in_r[i]);
      if (sat > 0.0f) x = SoftClip(x * in_drive) * in_norm;
      float ol = 0.0f, orr = 0.0f, fbk = 0.0f;
      for (int k = 0; k < clones; ++k) {
        s.tap_off[k] += (s.tap_off_t[k] - s.tap_off[k]) * 0.002f;
        float d = s.unit * kTribalPos[k] + s.tap_off[k];
        if (d < 2.0f) d = 2.0f;
        float y = line.Lin(d);
        s.tap_lp[k][0] += s.tap_lp[k][1] * (y - s.tap_lp[k][0]);
        y = s.tap_lp[k][0];
        ol += y * s.tap_gl[k];
        orr += y * s.tap_gr[k];
        if (k == last) fbk = y;
      }
      float w = x + s.fb * 0.9f * fbk;
      w = SoftClip(w * loop_g) * loop_inv;
      if (fm == 1) w = c.filt.Lp(w); else if (fm == 2) w = c.filt.Hp(w);
      w = c.dc.Hp(w, dc_a) + s.dith.Next();
      line.Write(w);
      out_l[i] = ol * s.level;
      out_r[i] = orr * s.level;
    }
    for (int k = 0; k < kTaps; ++k) if (fabsf(s.tap_lp[k][0]) < 1e-20f) s.tap_lp[k][0] = 0.0f;
    return;
  }

  // ------------------------------------------------------------------ line types
  const int nch = mono ? 1 : 2;
  float tgt[2] = {tl, tr};
  for (int c = 0; c < nch; ++c) if (!glide) s.ch[c].heads.Jump(tgt[c]);
  for (int i = 0; i < n; ++i) {
    s.fb += dfb;
    s.level += dlv;
    float modv = 0.0f, modr = 0.0f;
    if (tape) {
      s.wow_ph += wow_inc; if (s.wow_ph >= 1.0f) s.wow_ph -= 1.0f;
      s.flut_ph += flut_inc; if (s.flut_ph >= 1.0f) s.flut_ph -= 1.0f;
      if (--s.drift_cnt <= 0) { s.drift_cnt = 2400; s.drift_t = s.rnd.Uni(); }
      s.drift += (s.drift_t - s.drift) * 0.0002f;
      float w1 = 0.5f + 0.5f * SinPhase(s.wow_ph), w2 = 0.5f + 0.5f * SinPhase(s.wow_ph + 0.17f);
      float f1 = 0.5f + 0.5f * SinPhase(s.flut_ph);
      modv = wow_d * w1 + flut_d * f1 + drift_d * s.drift;
      modr = wow_d * w2 + flut_d * f1 + drift_d * s.drift;
    }
    float r[2];
    for (int c = 0; c < nch; ++c) {
      Heads& h = s.ch[c].heads;
      if (glide) {
        float e = (tgt[c] - h.d[0]) * glide_a;
        h.d[0] += Clamp(e, -0.5f, 0.5f);
        h.w = 0.0f; h.active = 0;
      } else {
        h.StepFade();
      }
      r[c] = h.Read(s.ch[c].line, c ? modr : modv);
    }
    if (mono) r[1] = r[0];
    float xin[2];
    if (mono) {
      xin[0] = 0.5f * (in_l[i] + in_r[i]);
    } else if (route == 2) {           // P-Pong: mono-summed input into the left line, feedback crossed
      xin[0] = 0.5f * (in_l[i] + in_r[i]);
      xin[1] = 0.0f;
    } else {
      xin[0] = in_l[i];
      xin[1] = in_r[i];
    }
    float ret[2];
    if (route == 0) { ret[0] = r[0]; ret[1] = r[1]; }
    else { ret[0] = r[1]; ret[1] = r[0]; }
    if (s.xtalk > 0.0f) { float a = ret[0], b = ret[1]; ret[0] += s.xtalk * (b - a); ret[1] += s.xtalk * (a - b); }
    for (int c = 0; c < nch; ++c) {
      Chan& ch = s.ch[c];
      float x = xin[c];
      if (sat > 0.0f) {
        x = SoftClip(x * in_drive + asym * x * x) * in_norm;
      }
      float w = x + s.fb * ret[c];
      if (sat > 0.0f) w = SoftClip(w * loop_g) * loop_inv;
      w = fxu::SafeLim(w);
      if (fm == 1) w = ch.filt.Lp(w); else if (fm == 2) w = ch.filt.Hp(w);
      if (tape) w = ch.tone.Lp(w);
      w = ch.dc.Hp(w, dc_a) + s.dith.Next();
      ch.line.Write(w);
    }
    float ol = r[0], orr = r[1];
    if (tape) {
      float l_, b_, h_;
      s.ch[0].bump.Process(ol, &l_, &b_, &h_);
      ol += bump_g * b_;
      if (!mono) { s.ch[1].bump.Process(orr, &l_, &b_, &h_); orr += bump_g * b_; }
      else orr = ol;
    }
    if (mono) { float m = ol; ol = m * s.panl * 1.4142f; orr = m * s.panr * 1.4142f; }
    out_l[i] = ol * s.level;
    out_r[i] = orr * s.level;
  }
}

bool DelayFx::Format(int param, float value, char* buf, int len) {
  switch (param) {
    case P_DLY_TIME: fxu::FormatMs(TimeMs(value), buf, len); return true;
    case P_DLY_CUTOFF: fxu::FormatHz(CutoffHz(value), buf, len); return true;
    default: return false;
  }
}

}  // namespace dlx
