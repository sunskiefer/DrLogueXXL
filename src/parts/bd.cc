// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- BD: the analogue bass drum (circuit-style model) plus a synthesised transient layer.
//
// Signal flow (per sample):
//   sine core (pitch = TUNE * 2^(SWEEP oct * pitch env))
//     -> asymmetric diode-style shaper (always on: the hardware body is saturated before DRIVE)
//     -> amp envelope (HOLD, then exponential DECAY)
//     -> DRIVE (soft clip with peak makeup)  + noise floor (level from DRIVE, own fixed envelope)
//     -> DC blocker -> + transient (TRANSIENT shape, ATTACK level) -> part filter (CUTOFF/RESON)
//     -> choke fade -> declick -> out
//
// Mappings (params.json units in, see src/param_ids.h):
//   TUNE   0..1023  -> 30 .. 120 Hz, exponential (two octaves; default 400 -> ~51.5 Hz)
//   SWEEP  0..100 % -> pitch-envelope depth 0 .. +3.5 octaves above TUNE at the hit (default 50 % = +1.75 oct)
//   SW.TIME 0..100% -> pitch-envelope time constant 80 ms .. 2 ms, exponential (higher = faster sweep)
//   HOLD   0..3     -> level held for 0 / 10 / 25 / 50 ms before the decay starts
//   DECAY  0..1023  -> time to -60 dB 40 ms .. 4 s, exponential
//   DRIVE  0..1023  -> soft-clip gain 1 .. 13 (quadratic taper) with makeup that keeps the peak at the clean level;
//                      also sets the noise floor (0 .. -24 dB re. body peak, x^1.5 taper) whose envelope is a
//                      fixed 300 ms (to -60 dB) independent of DECAY: short kicks keep a short hiss tail.
//   ATTACK 0..100 % -> transient level 0 .. 0.32 (re. the body's 0.78 peak); the output then passes a soft
//                      ceiling (linear to 0.75, never above ~0.98)
//   Velocity scales the level; it also pushes the drive stage (x0.85 .. x1.15) and the transient a little.
#include "analog.h"

namespace dlx {
namespace {

using namespace analog;

static const int kNumTransients = 16;
static const int kTransLen = 1440;   // 30 ms at 48 kHz
typedef ShotBank<kNumTransients, kTransLen> TransientBank;

// Init only: synthesise the 16 attack sounds (names: kOpts_BD_TRANSIENT).
static TransientBank* BuildTransients() {
  TransientBank* b = new TransientBank;
  memset(b, 0, sizeof(*b));
  Noise rng;
  rng.Seed(0xBD0001u);
  for (int t = 0; t < kNumTransients; ++t) {
    float* d = b->data[t];
    int n = kTransLen, fin = 4;
    switch (t) {
      case 0:   // Click: a hard, very short broadband tick plus a high blip
        n = Ms(5.0f); fin = 1;
        AddNoise(d, n, rng, 1.0f, 2, 3500.0f, 0.7f, 0.0f, 0.0005f);
        AddGlideSine(d, n, 0.8f, 3200.0f, 1500.0f, 0.001f, 0.0006f);
        break;
      case 1:   // Beater: hard beater on the skin (909-ish): falling mid blip + band noise
        n = Ms(14.0f);
        AddGlideSine(d, n, 1.0f, 1700.0f, 600.0f, 0.002f, 0.0025f);
        AddNoise(d, n, rng, 0.6f, 1, 2600.0f, 1.2f, 0.0f, 0.002f);
        break;
      case 2:   // Felt: soft felt beater, rounded low-mid thud with a slow-ish onset
        n = Ms(22.0f); fin = Ms(1.0f);
        AddNoise(d, n, rng, 1.0f, 0, 900.0f, 0.7f, 0.001f, 0.004f);
        AddGlideSine(d, n, 0.8f, 420.0f, 260.0f, 0.003f, 0.005f);
        break;
      case 3:   // Tick: tiny high resonant tick
        n = Ms(4.0f); fin = 1;
        AddResonator(d, n, 1.0f, 5200.0f, 7.0f);
        break;
      case 4:   // Snap: short band-passed noise crack
        n = Ms(20.0f); fin = 2;
        AddNoise(d, n, rng, 1.0f, 1, 2100.0f, 2.0f, 0.0002f, 0.0045f);
        AddNoise(d, n, rng, 0.4f, 2, 5000.0f, 0.7f, 0.0f, 0.002f);
        break;
      case 5:   // Wood: a struck wooden block (two resonant modes)
        n = Ms(28.0f); fin = 1;
        AddResonator(d, n, 1.0f, 1150.0f, 28.0f);
        AddResonator(d, n, 0.6f, 2380.0f, 22.0f);
        break;
      case 6:   // Pop: a round falling pop
        n = Ms(16.0f);
        AddGlideSine(d, n, 1.0f, 950.0f, 300.0f, 0.0012f, 0.004f);
        break;
      case 7:   // Noise: plain white noise burst
        n = Ms(30.0f); fin = 2;
        AddNoise(d, n, rng, 1.0f, 2, 40.0f, 0.6f, 0.0f, 0.008f);
        break;
      case 8:   // Air: breathy high noise with a soft onset
        n = Ms(30.0f); fin = Ms(1.0f);
        AddNoise(d, n, rng, 1.0f, 2, 6500.0f, 0.7f, 0.002f, 0.008f);
        break;
      case 9:   // Zap: fast laser-style downward chirp into the low end
        n = Ms(30.0f);
        AddGlideSine(d, n, 1.0f, 4200.0f, 100.0f, 0.003f, 0.009f);
        break;
      case 10:  // Metal: inharmonic 808-cymbal partials, short
        n = Ms(30.0f); fin = 2;
        AddMetal(d, n, 1.0f, 430.0f, 0.008f);
        break;
      case 11:  // Thump: low thud that reinforces the body's first cycle
        n = Ms(30.0f); fin = Ms(0.5f);
        AddGlideSine(d, n, 1.0f, 230.0f, 90.0f, 0.004f, 0.008f);
        AddNoise(d, n, rng, 0.5f, 0, 320.0f, 0.7f, 0.0f, 0.003f);
        break;
      case 12: {  // Vinyl: a few crackles over a little hiss
        n = Ms(30.0f); fin = 2;
        float imp[kTransLen];
        memset(imp, 0, sizeof(imp));
        for (int k = 0; k < 7; ++k) {
          int pos = (int)(rng.Uni() * (float)(n - 8)) + (k == 0 ? 0 : 4);
          if (k == 0) pos = 0;
          imp[pos] += (rng.Uni() * 0.7f + 0.3f) * (rng.Next() < 0.0f ? -1.0f : 1.0f);
        }
        Svf f; f.Set(3200.0f, 2.5f);
        for (int i = 0; i < n; ++i) d[i] += f.Bp(imp[i]) * 2.0f;
        AddNoise(d, n, rng, 0.08f, 0, 5000.0f, 0.7f, 0.0f, 0.02f);
        break;
      }
      case 13: {  // Bit: an 8-bit-ish blip: square, sample-and-held, 3-bit quantised
        n = Ms(16.0f); fin = 1;
        float ph = 0.0f, held = 0.0f;
        for (int i = 0; i < n; ++i) {
          ph += 1250.0f * kInvSampleRate; ph -= (float)(int)ph;
          float s = (ph < 0.5f ? 1.0f : -1.0f) * 0.6f + 0.4f * sinf(kTwoPi * ph * 1.6f);
          if (i % 6 == 0) held = s;
          float e = expf(-(float)i * kInvSampleRate / 0.004f);
          d[i] = floorf(held * e * 4.0f + 0.5f) * 0.25f;
        }
        break;
      }
      case 14:  // Chirp: short upward chirp
        n = Ms(25.0f);
        AddGlideSine(d, n, 1.0f, 700.0f, 3600.0f, 0.004f, 0.006f);
        break;
      default:  // 15 Laser: long bright downward sweep with a little FM bite
        n = Ms(30.0f); {
          float ph = 0.0f, pm = 0.0f;
          for (int i = 0; i < n; ++i) {
            float t = (float)i * kInvSampleRate;
            float f = 380.0f + (6500.0f - 380.0f) * expf(-t / 0.006f);
            pm += f * 1.5f * kInvSampleRate; pm -= (float)(int)pm;
            ph += f * kInvSampleRate; ph -= (float)(int)ph;
            d[i] = expf(-t / 0.011f) * sinf(kTwoPi * ph + 1.2f * sinf(kTwoPi * pm));
          }
        }
        break;
    }
    Finish(d, n, fin, Ms(2.0f) < n / 3 ? Ms(2.0f) : n / 3, 1.0f);
    b->len[t] = n;
  }
  return b;
}

static const TransientBank* Transients() {
  static TransientBank* bank = BuildTransients();   // C++11 thread-safe static, built once on first Init
  return bank;
}

static const int kHoldSamples[4] = {0, 480, 1200, 2400};   // 0 / 10 / 25 / 50 ms

class BassDrum : public Part {
 public:
  void Init(const float* p, int which, uint32_t seed) override {
    (void)which;
    p_ = p;
    bank_ = Transients();
    noise_.Seed(seed ^ 0xB0D0u);
    dc_.Set(8.0f);
    active_ = false;
    amp_ = 0.0f; nenv_ = 0.0f; pe_ = 0.0f; ph_ = 0.0f; tpos_ = kTransLen; tsel_ = 0;
    hold_ = 0; sub_ = 0; inc_ = 0.0f; dinc_ = 0.0f; vel_ = 1.0f;
  }

  void Trigger(float velocity, int note) override {
    (void)note;
    vel_ = Clamp(velocity, 0.0f, 1.0f);
    tsel_ = Opt(p_[BD_TRANSIENT], kNumTransients);
    tpos_ = 0;
    amp_ = 1.0f;
    hold_ = kHoldSamples[Opt(p_[BD_HOLD], 4)];
    nenv_ = 1.0f;
    pe_ = 1.0f;
    ph_ = 0.0f;   // restart from the zero crossing; the declicker bridges any step from the previous hit
    sub_ = 0;     // force a new frequency ramp segment at the first sample
    // start frequency of the sweep, so the first sub-block starts at the right pitch
    float fb = fbase_.primed ? fbase_.v : TuneHz();
    inc_ = fb * exp2f(SweepOct() * pe_) * kInvSampleRate;
    fade_.Reset();
    declick_.Arm();
    active_ = true;
  }

  void Choke() override { if (active_) fade_.Start(); }
  bool Active() const override { return active_; }

  int Render(float* l, float* r, int n) override {
    (void)r;
    if (!active_) { for (int i = 0; i < n; ++i) l[i] = 0.0f; return 1; }
    // ---- per-block parameter read ----
    float fb = fbase_.Step(TuneHz());
    float oct = SweepOct();
    float sw = Pct(p_[BD_SWTIME]);
    float tau_p = 0.08f * powf(0.025f, sw);                        // 80 ms .. 2 ms
    float cp_sub = expf(-(float)kSub / (tau_p * kSampleRate));     // pitch env per sub-block
    float cd = DecayCoef60(ExpMap(Knob(p_[BD_DECAY]), 0.04f, 4.0f));
    float cn = DecayCoef60(0.3f);
    float d = Knob(p_[BD_DRIVE]);
    float dg = (1.0f + 12.0f * d * d) * (0.85f + 0.3f * vel_);
    float mk = kBody / SoftClip(dg * kBody);
    float nlev = 0.063f * d * sqrtf(d) * kBody;
    float tg = 0.32f * Pct(p_[BD_ATTACK]) * (0.85f + 0.15f * vel_);
    filter_.Set(fcut_.Step(p_[BD_CUTOFF]), fres_.Step(p_[BD_RESON]));
    const float* tr = bank_->data[tsel_];
    int tlen = bank_->len[tsel_];
    float vel = vel_;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i) {
      // pitch: exponential sweep evaluated every kSub samples, linear increment ramp in between
      if (sub_ == 0) {
        pe_ *= cp_sub;
        float inc_end = fb * exp2f(oct * pe_) * kInvSampleRate;
        dinc_ = (inc_end - inc_) * (1.0f / (float)kSub);
        sub_ = kSub;
      }
      --sub_;
      inc_ += dinc_;
      ph_ += inc_;
      if (ph_ >= 1.0f) ph_ -= 1.0f;
      float c = SinPhase(ph_);
      // diode-style asymmetric shaper: the positive half saturates harder than the negative one
      float body = c >= 0.0f ? SoftClip(1.8f * c) : SoftClip(1.2f * c);
      // amplitude envelope: hold, then exponential decay
      if (hold_ > 0) --hold_; else amp_ *= cd;
      body *= amp_ * kBody;
      float y = SoftClip(dg * body) * mk;
      // noise floor: own envelope, does not follow DECAY
      nenv_ *= cn;
      y += nlp_.Lp(noise_.Next(), 0.6f) * nlev * nenv_;
      y = dc_.Process(y) * vel;
      if (tpos_ < tlen) y += tr[tpos_++] * tg * vel;
      y = filter_.Process(y);
      // fade, then the declicker (keeps retriggers continuous), then the soft ceiling (monotonic, so it keeps
      // that continuity and also bounds the declick offset on a noisy onset)
      y = Ceiling(declick_.Process(y * fade_.Next()));
      float a = fabsf(y);
      peak = a > peak ? a : peak;
      l[i] = y;
    }
    Flush(pe_); Flush(nenv_); Flush(amp_);
    if (fade_.Done() || (amp_ < kSilent && nenv_ * nlev < kSilent && tpos_ >= tlen && peak < kSilent &&
                         declick_.Settled())) {
      active_ = false;
      amp_ = nenv_ = 0.0f;
    }
    return 1;
  }

  bool Format(int off, float value, char* buf, int len) override {
    if (off != BD_HOLD) return false;
    static const char* const kTxt[4] = {"0 ms", "10 ms", "25 ms", "50 ms"};
    int i = Opt(value, 4);
    strncpy(buf, kTxt[i], (size_t)len);
    if (len > 0) buf[len - 1] = 0;
    return true;
  }

 private:
  static constexpr float kBody = 0.78f;   // body peak at full velocity
  float TuneHz() const { return ExpMap(Knob(p_[BD_TUNE]), 30.0f, 120.0f); }
  float SweepOct() const { return 3.5f * Pct(p_[BD_SWEEP]); }

  const float* p_ = nullptr;
  const TransientBank* bank_ = nullptr;
  Noise noise_;
  OnePole nlp_;
  DcBlock dc_;
  PartFilter filter_;
  BlockSmooth fbase_, fcut_, fres_;
  Fade fade_;
  Declick declick_;
  bool active_ = false;
  float vel_ = 1.0f, amp_ = 0.0f, nenv_ = 0.0f, pe_ = 0.0f, ph_ = 0.0f, inc_ = 0.0f, dinc_ = 0.0f;
  int hold_ = 0, sub_ = 0, tpos_ = 0, tsel_ = 0;
};

constexpr float BassDrum::kBody;

}  // namespace

Part* NewBassDrum() { return new BassDrum; }

}  // namespace dlx
