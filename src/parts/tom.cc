// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- LT / HT: the analogue toms (circuit-style model) plus a synthesised layer. One class for both;
// Init's `which` (PART_LT / PART_HT) picks the pitch range.
//
// Signal flow (per sample):
//   tone A at TUNE, tone B at TUNE * 2^(DETUNE semis / 12), both with an exponential pitch drop (SWEEP) and an
//   exponential amp decay (DECAY; tone B at 0.8x the time, as a slightly stiffer second mode), a touch of
//   always-on soft saturation
//     -> DRIVE (Off / Low / High: soft clip at gain 1 / 2.5 / 6 with peak makeup)
//     -> DC blocker -> + layer (LAYER shape, ATTACK level) -> part filter -> choke fade -> declick -> out
//
// Mappings:
//   TUNE   0..1023 -> LT 55 .. 220 Hz, HT 100 .. 400 Hz, exponential (two octaves each; defaults: LT 400 -> ~94 Hz,
//                     HT 600 -> ~225 Hz)
//   DETUNE 0..1023 -> interval of tone B above tone A 0 .. +7 semitones, linear in semitones (default 300 -> ~2 st)
//   SWEEP  0..100% -> pitch drop: the hit starts 0 .. +1.5 octaves above the tuned pitch and falls with a time
//                     constant that grows with DECAY (12 .. 60 ms) so long toms keep a longer "doom" glide
//   DECAY  0..1023 -> time to -60 dB 50 ms .. 3 s, exponential
//   ATTACK 0..100% -> layer level 0 .. 0.38 (re. body 0.7); soft output ceiling as on BD
//   Velocity scales the level and adds up to +20 % drive gain (accent).
#include "analog.h"

namespace dlx {
namespace {

using namespace analog;

static const int kNumLayers = 10;
static const int kLayerLen = 7200;   // 150 ms (Slap and Shaker/Brush need the length; the rest are shorter)
typedef ShotBank<kNumLayers, kLayerLen> LayerBank;

// Init only: synthesise the 10 layers (names: kOpts_LT_LAYER).
static LayerBank* BuildLayers() {
  LayerBank* b = new LayerBank;
  memset(b, 0, sizeof(*b));
  Noise rng;
  rng.Seed(0x70A1u);
  for (int t = 0; t < kNumLayers; ++t) {
    float* d = b->data[t];
    int n = kLayerLen, fin = 2, fout = Ms(3.0f);
    float peak = 1.0f;
    switch (t) {
      case 0:   // Stick: a stick hitting the skin: hard click + wooden mid ring
        n = Ms(25.0f); fin = 1;
        AddNoise(d, n, rng, 0.8f, 2, 3000.0f, 0.7f, 0.0f, 0.0008f);
        AddResonator(d, n, 0.7f, 1900.0f, 14.0f);
        AddResonator(d, n, 0.35f, 3700.0f, 10.0f);
        break;
      case 1:   // Skin: the skin's noisy, low-mid "thwack" with a falling blip
        n = Ms(40.0f);
        AddNoise(d, n, rng, 1.0f, 1, 750.0f, 1.0f, 0.0003f, 0.007f);
        AddGlideSine(d, n, 0.7f, 600.0f, 260.0f, 0.004f, 0.008f);
        break;
      case 2:   // Slap: a snare-like hit (two tuned heads + wire noise), usable as an alternative snare
        n = Ms(150.0f); fout = Ms(20.0f);
        AddGlideSine(d, n, 0.7f, 290.0f, 190.0f, 0.006f, 0.035f);
        AddGlideSine(d, n, 0.4f, 500.0f, 330.0f, 0.006f, 0.025f);
        AddNoise(d, n, rng, 0.9f, 2, 1600.0f, 0.8f, 0.0005f, 0.045f);
        AddNoise(d, n, rng, 0.5f, 1, 4800.0f, 1.4f, 0.0f, 0.03f);
        break;
      case 3:   // Block: a wood block (two strongly resonant modes)
        n = Ms(60.0f); fin = 1;
        AddResonator(d, n, 1.0f, 870.0f, 40.0f);
        AddResonator(d, n, 0.55f, 2350.0f, 30.0f);
        break;
      case 4:   // Zap: fast downward chirp
        n = Ms(45.0f);
        AddGlideSine(d, n, 1.0f, 3800.0f, 140.0f, 0.005f, 0.014f);
        break;
      case 5:   // Rim: rimshot-like crack: two high modes + a click (808 rim style)
        n = Ms(35.0f); fin = 1;
        AddResonator(d, n, 1.0f, 1650.0f, 20.0f);
        AddResonator(d, n, 0.7f, 470.0f, 9.0f);
        AddNoise(d, n, rng, 0.4f, 2, 4000.0f, 0.7f, 0.0f, 0.001f);
        break;
      case 6:   // Metal: inharmonic metallic partials (cowbell/cymbal-ish), short ring
        n = Ms(90.0f); fout = Ms(10.0f);
        AddMetal(d, n, 1.0f, 540.0f, 0.03f);
        break;
      case 7:   // Bloop: a round, upward "bloop"
        n = Ms(60.0f);
        AddGlideSine(d, n, 1.0f, 280.0f, 950.0f, 0.012f, 0.018f);
        break;
      case 8: {  // Shaker: shaped high noise with a soft rise and a fall
        n = Ms(120.0f); fin = Ms(1.0f); fout = Ms(20.0f);
        Svf f; f.Set(7000.0f, 1.2f);
        for (int i = 0; i < n; ++i) {
          float tt = (float)i * kInvSampleRate;
          float e = (tt < 0.025f ? tt / 0.025f : expf(-(tt - 0.025f) / 0.025f));
          d[i] = f.Bp(rng.Next()) * e;
        }
        peak = 0.8f;
        break;
      }
      default: {  // 9 Brush: brush sweep: low-passed noise with a slow swell and a grainy texture
        n = Ms(150.0f); fin = Ms(2.0f); fout = Ms(30.0f);
        Svf f; f.Set(3500.0f, 0.7f);
        float grain = 0.0f;
        for (int i = 0; i < n; ++i) {
          float tt = (float)i * kInvSampleRate;
          float e = (tt < 0.015f ? tt / 0.015f : expf(-(tt - 0.015f) / 0.05f));
          if (i % 96 == 0) grain = 0.5f + 0.5f * rng.Uni();
          d[i] = f.Lp(rng.Next()) * e * grain;
        }
        peak = 0.8f;
        break;
      }
    }
    Finish(d, n, fin, fout < n / 3 ? fout : n / 3, peak);
    b->len[t] = n;
  }
  return b;
}

static const LayerBank* Layers() {
  static LayerBank* bank = BuildLayers();
  return bank;
}

class Tom : public Part {
 public:
  void Init(const float* p, int which, uint32_t seed) override {
    (void)seed;   // the layers are fixed tables; nothing random per voice
    p_ = p;
    lo_ = which == PART_HT ? 100.0f : 55.0f;
    hi_ = which == PART_HT ? 400.0f : 220.0f;
    bank_ = Layers();
    dc_.Set(15.0f);
    active_ = false;
    aa_ = ab_ = 0.0f;
    lpos_ = kLayerLen;
  }

  void Trigger(float velocity, int note) override {
    (void)note;
    vel_ = Clamp(velocity, 0.0f, 1.0f);
    lsel_ = Opt(p_[TOM_LAYER], kNumLayers);
    lpos_ = 0;
    aa_ = 1.0f; ab_ = 1.0f;
    pe_ = 1.0f;
    pha_ = 0.0f; phb_ = 0.0f;
    sub_ = 0;
    float fa = fa_.primed ? fa_.v : TuneHz();
    float rb = rb_.primed ? rb_.v : DetuneRatio();
    float m = exp2f(SweepOct() * pe_);
    inca_ = fa * m * kInvSampleRate;
    incb_ = fa * rb * m * kInvSampleRate;
    fade_.Reset();
    declick_.Arm();
    active_ = true;
  }

  void Choke() override { if (active_) fade_.Start(); }
  bool Active() const override { return active_; }

  int Render(float* l, float* r, int n) override {
    (void)r;
    if (!active_) { for (int i = 0; i < n; ++i) l[i] = 0.0f; return 1; }
    float fa = fa_.Step(TuneHz());
    float rb = rb_.Step(DetuneRatio());
    float oct = SweepOct();
    float dk = Knob(p_[TOM_DECAY]);
    float tdec = ExpMap(dk, 0.05f, 3.0f);
    float ca = DecayCoef60(tdec), cb = DecayCoef60(tdec * 0.8f);
    float tau_p = 0.012f + 0.048f * dk;
    float cpe = expf(-(float)kSub / (tau_p * kSampleRate));
    static const float kDriveGain[3] = {1.0f, 2.5f, 6.0f};
    int dsel = Opt(p_[TOM_DRIVE], 3);
    float dg = kDriveGain[dsel] * (1.0f + 0.2f * vel_);
    float mk = kBody / SoftClip(dg * kBody);
    float lg = 0.38f * Pct(p_[TOM_ATTACK]);
    filter_.Set(fcut_.Step(p_[TOM_CUTOFF]), fres_.Step(p_[TOM_RESON]));
    const float* ly = bank_->data[lsel_];
    int llen = bank_->len[lsel_];
    float vel = vel_;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i) {
      if (sub_ == 0) {
        pe_ *= cpe;
        float m = exp2f(oct * pe_) * kInvSampleRate;
        float ea = fa * m, eb = fa * rb * m;
        da_ = (ea - inca_) * (1.0f / (float)kSub);
        db_ = (eb - incb_) * (1.0f / (float)kSub);
        sub_ = kSub;
      }
      --sub_;
      inca_ += da_; incb_ += db_;
      pha_ += inca_; if (pha_ >= 1.0f) pha_ -= 1.0f;
      phb_ += incb_; if (phb_ >= 1.0f) phb_ -= 1.0f;
      aa_ *= ca; ab_ *= cb;
      float body = (0.62f * aa_ * SinPhase(pha_) + 0.38f * ab_ * SinPhase(phb_));
      body = SoftClip(1.25f * body) * (kBody / SoftClip(1.25f));
      float y = dsel ? SoftClip(dg * body) * mk : body;
      y = dc_.Process(y) * vel;
      if (lpos_ < llen) y += ly[lpos_++] * lg * vel;
      y = filter_.Process(y);
      // fade, then the declicker (keeps retriggers continuous), then the soft ceiling (monotonic, so it keeps
      // that continuity and also bounds the declick offset on a noisy onset)
      y = Ceiling(declick_.Process(y * fade_.Next()));
      float a = fabsf(y);
      peak = a > peak ? a : peak;
      l[i] = y;
    }
    Flush(pe_); Flush(aa_); Flush(ab_);
    if (fade_.Done() || (aa_ < kSilent && lpos_ >= llen && peak < kSilent && declick_.Settled())) active_ = false;
    return 1;
  }

 private:
  static constexpr float kBody = 0.7f;
  float TuneHz() const { return ExpMap(Knob(p_[TOM_TUNE]), lo_, hi_); }
  float DetuneRatio() const { return SemiToRatio(7.0f * Knob(p_[TOM_DETUNE])); }
  float SweepOct() const { return 1.5f * Pct(p_[TOM_SWEEP]); }

  const float* p_ = nullptr;
  const LayerBank* bank_ = nullptr;
  DcBlock dc_;
  PartFilter filter_;
  BlockSmooth fa_, rb_, fcut_, fres_;
  Fade fade_;
  Declick declick_;
  bool active_ = false;
  float lo_ = 55.0f, hi_ = 220.0f, vel_ = 1.0f, aa_ = 0.0f, ab_ = 0.0f, pe_ = 0.0f, pha_ = 0.0f, phb_ = 0.0f;
  float inca_ = 0.0f, incb_ = 0.0f, da_ = 0.0f, db_ = 0.0f;
  int sub_ = 0, lpos_ = 0, lsel_ = 0;
};

constexpr float Tom::kBody;

}  // namespace

Part* NewTom() { return new Tom; }

}  // namespace dlx
