// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- SD: the analogue snare (circuit-style model): two drum heads plus the "snappy" noise path.
//
// Signal flow (per sample):
//   head 1: sine at TUNE, head 2: sine at TUNE * interval(TONE), both with a short pitch drop on the hit and
//   their own exponential decays (head 2 a little shorter, like the 808's upper bridged-T), a gentle soft clip
//     + snappy: noise source/colour per SNAPPY TYPE, its own envelope (SNAP.DEC), level SNAP.LV
//   -> DC blocker -> part filter (CUTOFF/RESON) -> choke fade -> declick -> out
//
// Mappings:
//   TUNE     0..1023 -> head 1 at 120 .. 400 Hz, exponential (default 500 -> ~217 Hz)
//   TONE     0..100% -> head 2 interval 1.1x .. 1.9x of head 1 (linear in log-frequency); the balance tips
//                       slightly toward head 2 as the interval widens, as tuning the second head brightens the drum
//   DECAY    0..1023 -> body time to -60 dB 50 ms .. 2 s, exponential (head 2 decays at 0.7x of that time)
//   pitch drop       -> both heads start +0.6 octave high and fall with an 8 ms time constant (fixed, like the
//                       hardware's built-in "skin tension" bend; the snare has no SWEEP param)
//   SNAP.DEC 0..1023 -> snappy time to -60 dB 30 ms .. 1.5 s, exponential
//   SNAP.LV  0..100% -> snappy level 0 .. 0.42 (re. body 0.5); soft output ceiling as on BD
//   SNAPPY TYPE:
//     808     high-passed noise (1.8 kHz) with a band peak around 5 kHz (the 808's snappy filter)
//     909     brighter, low-passed at 9 kHz, a short initial burst on top (909 noise "crack")
//     White   plain white noise
//     Pink    pink noise (Kellet filter), darker and fuller
//     Bright  noise high-passed at 4.5 kHz: sizzle only
//     Rattle  808-coloured noise amplitude-modulated by fast decaying bursts at irregular ~3-9 ms intervals,
//             like loose snare wires buzzing against the bottom head
//     Lo-Fi 1 909 colour, 6-bit, held at 1/2 rate (24 kHz)
//     Lo-Fi 2 909 colour, 4-bit, 1/4 rate (12 kHz)
//     Lo-Fi 3 909 colour, 3-bit, 1/8 rate (6 kHz) -- the crushing also applies to the body mix, like a lo-fi PCM
//   Velocity scales the level and opens the snappy slightly brighter at high velocity (accent).
#include "analog.h"

namespace dlx {
namespace {

using namespace analog;

class SnareDrum : public Part {
 public:
  void Init(const float* p, int which, uint32_t seed) override {
    (void)which;
    p_ = p;
    noise_.Seed(seed ^ 0x5D5Du);
    dc_.Set(20.0f);
    active_ = false;
    a1_ = a2_ = sn_ = 0.0f;
  }

  void Trigger(float velocity, int note) override {
    (void)note;
    vel_ = Clamp(velocity, 0.0f, 1.0f);
    type_ = Opt(p_[SD_SNAPPY], 9);
    SetupSnappy();
    a1_ = 1.0f; a2_ = 1.0f; sn_ = 1.0f;
    pe_ = 1.0f;
    ph1_ = 0.0f; ph2_ = 0.0f;
    sub_ = 0;
    float f1 = f1s_.primed ? f1s_.v : TuneHz();
    float r2 = r2s_.primed ? r2s_.v : Interval();
    float m = exp2f(kDropOct * pe_);
    inc1_ = f1 * m * kInvSampleRate;
    inc2_ = f1 * r2 * m * kInvSampleRate;
    burst_ = 1.0f;
    rattle_ = 0.0f;
    rcount_ = 0;
    crush_cnt_ = 0;
    fade_.Reset();
    declick_.Arm();
    active_ = true;
  }

  void Choke() override { if (active_) fade_.Start(); }
  bool Active() const override { return active_; }

  int Render(float* l, float* r, int n) override {
    (void)r;
    if (!active_) { for (int i = 0; i < n; ++i) l[i] = 0.0f; return 1; }
    float f1 = f1s_.Step(TuneHz());
    float r2 = r2s_.Step(Interval());
    float tone = Pct(p_[SD_TONE]);
    float g1 = 0.5f * (1.0f - 0.25f * tone), g2 = 0.5f * (0.55f + 0.3f * tone);
    float tdec = ExpMap(Knob(p_[SD_DECAY]), 0.05f, 2.0f);
    float c1 = DecayCoef60(tdec), c2 = DecayCoef60(tdec * 0.7f);
    float cs = DecayCoef60(ExpMap(Knob(p_[SD_SNAPDEC]), 0.03f, 1.5f));
    float snl = 0.42f * Pct(p_[SD_SNAPLV]);
    float cpe = kDropCoefSub;
    filter_.Set(fcut_.Step(p_[SD_CUTOFF]), fres_.Step(p_[SD_RESON]));
    float vel = vel_;
    float peak = 0.0f;
    const int type = type_;

    for (int i = 0; i < n; ++i) {
      if (sub_ == 0) {
        pe_ *= cpe;
        float m = exp2f(kDropOct * pe_) * kInvSampleRate;
        float e1 = f1 * m, e2 = f1 * r2 * m;
        d1_ = (e1 - inc1_) * (1.0f / (float)kSub);
        d2_ = (e2 - inc2_) * (1.0f / (float)kSub);
        sub_ = kSub;
      }
      --sub_;
      inc1_ += d1_; inc2_ += d2_;
      ph1_ += inc1_; if (ph1_ >= 1.0f) ph1_ -= 1.0f;
      ph2_ += inc2_; if (ph2_ >= 1.0f) ph2_ -= 1.0f;
      a1_ *= c1; a2_ *= c2;
      float body = g1 * a1_ * SinPhase(ph1_) + g2 * a2_ * SinPhase(ph2_);
      body = SoftClip(body * 1.3f) * (1.0f / 1.3f);

      // ---- snappy ----
      float w = noise_.Next();
      float s;
      switch (type) {
        case 0: s = svf_.Bp(w) * 1.1f + hp_.Hp(w, hpa_) * 0.45f; break;                 // 808
        case 2: s = w * 0.8f; break;                                                     // White
        case 3: s = pink_.Process(w) * 1.3f; break;                                      // Pink
        case 4: s = svf_.Hp(w) * 0.9f; break;                                            // Bright
        case 5: {                                                                        // Rattle
          if (--rcount_ <= 0) {
            rcount_ = 144 + (int)(noise_.Uni() * 288.0f);   // 3 .. 9 ms
            rattle_ = 0.55f + 0.45f * noise_.Uni();
          }
          rattle_ *= 0.9965f;                                // each buzz decays in ~6 ms
          s = (svf_.Bp(w) * 1.0f + hp_.Hp(w, hpa_) * 0.35f) * (0.25f + 1.3f * rattle_);
          break;
        }
        default: {                                                                       // 909 and Lo-Fi
          burst_ *= 0.996f;
          s = svf_.Lp(w) * (0.7f + 0.6f * burst_);
          break;
        }
      }
      sn_ *= cs;
      float y = body + s * sn_ * snl * bright_;
      if (crush_div_ > 1) {   // Lo-Fi: sample-and-hold + bit reduction of the whole voice
        if (crush_cnt_ <= 0) { held_ = floorf(y * crush_q_ + 0.5f) * crush_iq_; crush_cnt_ = crush_div_; }
        --crush_cnt_;
        y = held_;
      }
      y = dc_.Process(y) * vel;
      y = filter_.Process(y);
      // fade, then the declicker (keeps retriggers continuous), then the soft ceiling (monotonic, so it keeps
      // that continuity and also bounds the declick offset on a noisy onset)
      y = Ceiling(declick_.Process(y * fade_.Next()));
      float a = fabsf(y);
      peak = a > peak ? a : peak;
      l[i] = y;
    }
    Flush(pe_); Flush(a1_); Flush(a2_); Flush(sn_); Flush(burst_); Flush(rattle_);
    if (fade_.Done() || (a1_ < kSilent && sn_ * snl < kSilent && peak < kSilent && declick_.Settled())) {
      active_ = false;
    }
    return 1;
  }

 private:
  static constexpr float kDropOct = 0.6f;
  static constexpr float kDropCoefSub = 0.959189f;   // exp(-16 / (8 ms * 48 kHz))
  float TuneHz() const { return ExpMap(Knob(p_[SD_TUNE]), 120.0f, 400.0f); }
  float Interval() const { return ExpMap(Pct(p_[SD_TONE]), 1.1f, 1.9f); }

  // Init-time-ish per-hit setup (a couple of tanf, only on Trigger)
  void SetupSnappy() {
    bright_ = 0.85f + 0.3f * vel_;
    float vb = 1.0f + 0.25f * vel_;   // accent opens the noise
    crush_div_ = 1;
    switch (type_) {
      case 0: svf_.Set(5000.0f * vb, 1.6f); hpa_ = OnePoleHz(1800.0f); break;
      case 4: svf_.Set(4500.0f, 0.7f); break;
      case 5: svf_.Set(4200.0f * vb, 1.4f); hpa_ = OnePoleHz(1500.0f); break;
      case 1: case 6: case 7: case 8: svf_.Set(Clamp(7200.0f * vb, 100.0f, 16000.0f), 0.8f); break;
      default: break;
    }
    if (type_ == 6) { crush_div_ = 2; crush_q_ = 32.0f; }
    if (type_ == 7) { crush_div_ = 4; crush_q_ = 8.0f; }
    if (type_ == 8) { crush_div_ = 8; crush_q_ = 4.0f; }
    crush_iq_ = 1.0f / crush_q_;
  }

  const float* p_ = nullptr;
  Noise noise_;
  Svf svf_;
  OnePole hp_;
  Pink pink_;
  DcBlock dc_;
  PartFilter filter_;
  BlockSmooth f1s_, r2s_, fcut_, fres_;
  Fade fade_;
  Declick declick_;
  bool active_ = false;
  int type_ = 0, sub_ = 0, rcount_ = 0, crush_div_ = 1, crush_cnt_ = 0;
  float vel_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, sn_ = 0.0f, pe_ = 0.0f, ph1_ = 0.0f, ph2_ = 0.0f;
  float inc1_ = 0.0f, inc2_ = 0.0f, d1_ = 0.0f, d2_ = 0.0f, burst_ = 0.0f, rattle_ = 0.0f, hpa_ = 0.2f;
  float bright_ = 1.0f, crush_q_ = 1.0f, crush_iq_ = 1.0f, held_ = 0.0f;
};

constexpr float SnareDrum::kDropOct;
constexpr float SnareDrum::kDropCoefSub;

}  // namespace

Part* NewSnareDrum() { return new SnareDrum; }

}  // namespace dlx
