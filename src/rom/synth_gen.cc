// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sound generators of the factory sample banks (see synth_gen.h for the parameter lists).
//
// Circuit notes (paraphrased from public descriptions of the machines):
//  - TR-808 hats/cymbal: six square-wave oscillators (~205, 304, 370, 523, 540, 800 Hz) summed, two band-pass
//    filters (~3.4 and ~7.1 kHz), a VCA with an exponential decay, then a high-pass.
//  - TR-808 cowbell: two of those squares (540 / 800 Hz) through a band-pass, VCA with a fast-then-slow decay.
//  - TR-808 rim shot: bridged-T resonators (~455 Hz and ~1.67 kHz) pinged by a pulse, a high-pass and a hard VCA.
//  - TR-808 clap: band-passed noise through a VCA re-triggered 3-4 times ~10 ms apart (sawtooth envelope), plus a
//    longer "reverb" noise tail.
//  - TR-909 hats were 6-bit samples of a metallic source; modelled as ring-modulated square pairs through filters
//    and a 6-bit quantiser.
#include "synth_gen.h"

namespace dlx {
namespace syn {

static const float kMaxSec = 4.0f;
static inline float Cap(float s) { return s > kMaxSec ? kMaxSec : (s < 0.01f ? 0.01f : s); }

// per-sample multiplier for exp(-t / tau)
static inline float TauCoef(float tau) { return tau <= 0.0f ? 0.0f : expf(-1.0f / (tau * kSr)); }

// ------------------------------------------------------------------------------------------------- hats
static void GenHat(const float* p, uint32_t seed, Buf& b) {
  static const float kSets[4][6] = {
    {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f},     // 808
    {245.0f, 306.0f, 365.0f, 415.0f, 437.0f, 619.0f},     // 606-ish
    {263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f},     // metallic (ring-modulated pairs)
    {317.0f, 421.0f, 538.0f, 643.0f, 811.0f, 1040.0f},    // CR-78-ish
  };
  float pitch = p[0], dec = p[1], bp1 = p[2], bp2 = p[3], mix2 = p[4], hpf = p[5];
  int set = (int)p[6] & 3;
  float nmix = p[7], open = p[8], click = p[9];
  b.Alloc(Cap(dec * 1.05f + 0.01f), false);
  SqOsc o[6];
  Noise nz; nz.Seed(seed);
  for (int k = 0; k < 6; k++) { o[k].Set(kSets[set][k] * pitch); o[k].ph = nz.Uni(); }
  Svf f1, f2, h1, h2;
  f1.Set(bp1, 2.5f); f2.Set(bp2, 2.5f); h1.Set(hpf, 0.707f); h2.Set(hpf, 0.6f);
  float ks = DecayCoef60(dec), kf = DecayCoef60(dec * 0.22f), kc = TauCoef(0.0006f);
  float es = 1.0f - open, ef = open, ec = click;
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    float s;
    if (set == 2) s = (o[0].Next() * o[1].Next() + o[2].Next() * o[3].Next() + o[4].Next() * o[5].Next()) * 0.33f;
    else s = (o[0].Next() + o[1].Next() + o[2].Next() + o[3].Next() + o[4].Next() + o[5].Next()) * (1.0f / 6.0f);
    float n = nz.Next();
    s = s * (1.0f - nmix) + n * nmix;
    float y = f1.Bp(s) + mix2 * f2.Bp(s);
    y = y * (es + ef) + ec * n * 2.0f;
    es *= ks; ef *= kf; ec *= kc;
    x[i] = h2.Hp(h1.Hp(y));
  }
}

// ------------------------------------------------------------------------------------------------- 2-op FM
static void GenFm(const float* p, uint32_t seed, Buf& b) {
  float fc = p[0], ratio = p[1], idx = p[2], idec = p[3], adec = p[4], fb = p[5], pst = p[6], pt = p[7];
  float hpf = p[8], nmix = p[9];
  b.Alloc(Cap(adec * 1.05f), false);
  Noise nz; nz.Seed(seed);
  float kI = DecayCoef60(idec), kA = DecayCoef60(adec), kP = TauCoef(pt);
  float pe = pst > 0.0f ? pst - 1.0f : 0.0f;
  float cph = 0.0f, mph = 0.0f, prev = 0.0f, amp = 1.0f, I = idx * (1.0f / kTwoPi);
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    float f = fc * (1.0f + pe);
    pe *= kP;
    mph += f * ratio / kSr; if (mph >= 1.0f) mph -= (float)(int)mph;
    float m = SinPhase(mph + fb * prev * 0.16f);
    prev = m;
    cph += f / kSr; if (cph >= 1.0f) cph -= (float)(int)cph;
    float ph = cph + I * m;
    if (ph < 0.0f) ph += (float)(int)(-ph) + 1.0f;
    float y = SinPhase(ph);
    x[i] = (y + nmix * nz.Next()) * amp;
    amp *= kA; I *= kI;
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
}

// ------------------------------------------------------------------------------------------------- filtered noise
static void GenNoise(const float* p, uint32_t seed, bool stereo, Buf& b) {
  int type = (int)p[0];
  float hz = p[1], q = p[2], att = p[3], dec = p[4], hpf = p[5];
  bool pink = p[6] > 0.5f;
  float swEnd = p[7], swT = p[8], click = p[9];
  b.Alloc(Cap(att + dec * 1.05f), stereo);
  bool sweep = swEnd > 0.0f && swEnd != 1.0f && swT > 0.0f;
  for (int c = 0; c < b.Channels(); c++) {
    Noise nz; nz.Seed(seed + 977u * c);
    Pink pk;
    Svf f; f.Set(hz, q);
    float k = DecayCoef60(dec), kc = TauCoef(0.0008f), env = 1.0f, ec = click;
    int na = (int)(att * kSr);
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) {
      if (sweep && (i & 15) == 0) {
        float t = (float)i / kSr / swT;
        f.Set(hz * powf(swEnd, t > 1.0f ? 1.0f : t), q);
      }
      float n = nz.Next();
      float s = pink ? pk.Next(n) * 2.5f : n;
      float l, bp, h;
      f.Process(s, &l, &bp, &h);
      float y = type == 0 ? l : (type == 1 ? bp : h);
      float g;
      if (i < na) g = (float)i / (float)na;
      else { g = env; env *= k; }
      x[i] = y * g + ec * n;
      ec *= kc;
    }
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
}

// ------------------------------------------------------------------------------------------------- modal (rims, woods, bells)
static void GenModal(const float* p, uint32_t seed, Buf& b) {
  float base = p[0], dec = p[4], click = p[7], hpf = p[8], drive = p[9];
  float ratio[4] = {1.0f, p[1], p[2], p[3]};
  float amp[4] = {1.0f, p[5], p[6], p[6] * 0.6f};
  float dmul[4] = {1.0f, 0.8f, 0.6f, 0.45f};
  b.Alloc(Cap(dec * 1.05f + 0.01f), false);
  Reso r[4];
  int nr = 0;
  float ra[4];
  for (int k = 0; k < 4; k++) {
    if (ratio[k] <= 0.0f || amp[k] <= 0.0f || base * ratio[k] > 20000.0f) continue;
    r[nr].Set(base * ratio[k], dec * dmul[k]);
    ra[nr] = amp[k];
    nr++;
  }
  Noise nz; nz.Seed(seed);
  float kc = TauCoef(0.0007f), ec = 1.0f;
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    float n = nz.Next();
    float e = (i == 0 ? 1.0f : 0.0f) + n * ec * 0.25f;
    float y = 0.0f;
    for (int k = 0; k < nr; k++) y += ra[k] * r[k].Tick(e);
    x[i] = y + click * n * ec;
    ec *= kc;
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
  if (drive > 0.0f) Saturate(b, drive);
}

// ------------------------------------------------------------------------------------------------- claps
static void GenClap(const float* p, uint32_t seed, bool stereo, Buf& b) {
  int bursts = (int)p[0];
  if (bursts < 1) bursts = 1;
  if (bursts > 12) bursts = 12;
  float sp = p[1], bp = p[2], q = p[3], bt = p[4], tail = p[5], tlev = p[6], vmix = p[7], vtime = p[8];
  float spread = p[9];
  Noise jr; jr.Seed(seed ^ 0xabcdefu);
  int starts[2][12];
  float lev[2][12];
  for (int c = 0; c < 2; c++) {
    float t = 0.0f;
    for (int k = 0; k < bursts; k++) {
      starts[c][k] = (int)(t * kSr);
      lev[c][k] = Rnd(jr, 0.75f, 1.0f);
      float j = Rnd(jr, 0.75f, 1.25f);
      if (c == 1) { starts[1][k] = (int)(starts[0][k] + spread * Rnd(jr, -0.003f, 0.003f) * kSr); if (starts[1][k] < 0) starts[1][k] = 0; }
      t += sp * j;
    }
  }
  float len = (float)starts[0][bursts - 1] / kSr + tail * 1.05f + 0.01f;
  b.Alloc(Cap(len), stereo);
  float kb = TauCoef(bt), kt = DecayCoef60(tail);
  for (int c = 0; c < b.Channels(); c++) {
    Noise n1; n1.Seed(seed);
    Noise n2; n2.Seed(seed + 31337u);
    Svf f; f.Set(bp, q);
    Svf h; h.Set(350.0f, 0.707f);
    float* x = b.Ch(c);
    int k = 0;
    float env = 0.0f, tenv = 0.0f;
    bool intail = false;
    float wc = c ? spread : 0.0f;
    for (int i = 0; i < b.n; i++) {
      if (k < bursts && i == starts[c][k]) {
        env = lev[c][k];
        if (k == bursts - 1) { intail = true; tenv = tlev; }
        k++;
      }
      float a = n1.Next(), bb = n2.Next();
      float n = a * (1.0f - wc) + bb * wc;
      float y = f.Bp(n) * 2.0f;
      float g = env + tenv;
      env *= kb;
      if (intail) tenv *= kt;
      x[i] = h.Hp(y * g);
    }
  }
  if (vmix > 0.0f) AddVerb(b, vmix, vtime, 0.35f, 0.75f, 0.008f);
}

// ------------------------------------------------------------------------------------------------- cowbell
static void GenCow(const float* p, uint32_t seed, Buf& b) {
  float f1 = p[0], f2 = p[1], bp = p[2], q = p[3], fast = p[4], dec = p[5], duty = p[6], hpf = p[7];
  b.Alloc(Cap(dec * 1.05f), false);
  SqOsc a, c;
  a.Set(f1); c.Set(f2);
  a.duty = c.duty = duty > 0.05f ? duty : 0.5f;
  Noise nz; nz.Seed(seed);
  a.ph = nz.Uni(); c.ph = nz.Uni();
  Svf f; f.Set(bp, q);
  float kf = DecayCoef60(dec * 0.12f < 0.08f ? dec * 0.12f : 0.08f), ks = DecayCoef60(dec);
  float ef = fast, es = 1.0f - fast;
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    float s = (a.Next() + 0.8f * c.Next()) * 0.5f;
    x[i] = f.Bp(s) * (ef + es);
    ef *= kf; es *= ks;
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
}

// ------------------------------------------------------------------------------------------------- tonal drums
static void GenDrum(const float* p, uint32_t seed, Buf& b) {
  float hz = p[0], pst = p[1], pt = p[2], dec = p[3], click = p[4], nzl = p[5], ndec = p[6], nbp = p[7];
  float drive = p[8], r2 = p[9];
  float len = (dec > ndec ? dec : ndec) * 1.05f + 0.005f;
  b.Alloc(Cap(len), false);
  Noise nz; nz.Seed(seed);
  Svf nf;
  if (nbp > 0.0f) nf.Set(nbp, 0.9f); else nf.Set(1800.0f, 0.707f);
  float kA = DecayCoef60(dec), k2 = DecayCoef60(dec * 0.45f), kN = DecayCoef60(ndec > 0.0f ? ndec : 0.01f);
  float kP = TauCoef(pt), kC = TauCoef(0.0006f);
  float pe = pst > 0.0f ? pst - 1.0f : 0.0f;
  float ph = 0.0f, ph2 = 0.0f, a = 1.0f, a2 = 0.4f, an = nzl, ac = click;
  float* x = b.L();
  // 0.5 ms fade-in on the body so a sine start never clicks unless the recipe asks for one (click)
  int fin = (int)(0.0005f * kSr);
  for (int i = 0; i < b.n; i++) {
    float f = hz * (1.0f + pe);
    pe *= kP;
    ph += f / kSr; if (ph >= 1.0f) ph -= 1.0f;
    float y = SinPhase(ph) * a;
    if (r2 > 0.0f) {
      ph2 += f * r2 / kSr; if (ph2 >= 1.0f) ph2 -= (float)(int)ph2;
      y += SinPhase(ph2) * a2;
      a2 *= k2;
    }
    if (i < fin) y *= (float)i / (float)fin;
    float n = nz.Next();
    float l, bp, h;
    nf.Process(n, &l, &bp, &h);
    y += (nbp > 0.0f ? bp * 1.5f : h) * an + ac * n;
    a *= kA; an *= kN; ac *= kC;
    x[i] = y;
  }
  if (drive > 0.0f) Saturate(b, drive);
}

// ------------------------------------------------------------------------------------------------- cymbals
static void GenCym(const float* p, uint32_t seed, bool stereo, Buf& b) {
  int np = (int)p[0];
  float lo = p[1], hi = p[2], dec = p[3], nzl = p[4], hpf = p[5], bell = p[6], att = p[7], ring = p[8];
  float hidec = p[9] > 0.0f ? p[9] : 1.0f;
  b.Alloc(Cap(dec * 1.1f + att), stereo);
  Noise nz; nz.Seed(seed);
  float* L = b.L();
  float* R = b.R();
  float llo = logf(lo), lhi = logf(hi);
  int total = np + (bell > 0.0f ? 4 : 0);
  for (int k = 0; k < total; k++) {
    float f, a, d;
    if (k < np) {
      float u = nz.Uni();
      f = expf(llo + (lhi - llo) * u);
      a = Rnd(nz, 0.3f, 1.0f) * powf(lo / f, 0.25f);
      d = dec * Rnd(nz, 0.55f, 1.1f) * powf(hidec, u);
    } else {
      static const float kBell[4] = {1.0f, 2.32f, 4.25f, 6.63f};
      f = lo * 3.1f * kBell[k - np];
      a = bell * 2.5f / (1.0f + (k - np));
      d = dec * 1.2f;
    }
    if (f > 21000.0f) continue;
    Partial pa;
    pa.Set(f, d, a, nz.Uni() * kTwoPi);
    int m = (int)(d * 1.05f * kSr);
    if (m > b.n) m = b.n;
    if (stereo) {
      float pan = Rnd(nz, 0.15f, 0.85f);
      float gl = cosf(pan * kPi * 0.5f) * 1.41f, gr = sinf(pan * kPi * 0.5f) * 1.41f;
      for (int i = 0; i < m; i++) { float v = pa.Next(); L[i] += v * gl; R[i] += v * gr; }
    } else {
      for (int i = 0; i < m; i++) L[i] += pa.Next();
    }
  }
  // scale partial sum, add the noise wash, ring modulation and attack swell
  float sc = 1.0f / sqrtf((float)(np > 0 ? np : 1));
  for (int c = 0; c < b.Channels(); c++) {
    Noise n2; n2.Seed(seed + 101u * (c + 1));
    float* x = b.Ch(c);
    float kn = DecayCoef60(dec * 0.7f), en = nzl;
    float rph = 0.0f;
    int na = (int)(att * kSr);
    for (int i = 0; i < b.n; i++) {
      float y = x[i] * sc + n2.Next() * en;
      en *= kn;
      if (ring > 0.0f) {
        rph += 1373.0f / kSr; if (rph >= 1.0f) rph -= 1.0f;
        y = y * (1.0f - ring) + ring * y * SinPhase(rph) * 1.6f;
      }
      if (i < na) y *= (float)i / (float)na;
      x[i] = y;
    }
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
}

// ------------------------------------------------------------------------------------------------- Karplus-Strong
static void KsString(float hz, float dec, float bright, float pick, uint32_t seed, float* out, int n) {
  float period = kSr / hz;
  float b = 0.5f + 0.5f * bright;                     // FIR: b*x[n] + (1-b)*x[n-1]; delay (1-b)
  float dl = period - (1.0f - b);
  int len = (int)dl + 4;
  std::vector<float> line(len, 0.0f);
  float frac = dl - (float)(int)dl;
  int di = (int)dl;
  float g = powf(10.0f, -3.0f * period / (dec * kSr));
  Noise nz; nz.Seed(seed);
  OnePole lp;
  float pa = OnePoleHz(500.0f + 12000.0f * pick);
  int burst = (int)period;
  int w = 0;
  float prev = 0.0f;
  for (int i = 0; i < n; i++) {
    // read the delayed sample (linear interpolation between di and di+1 samples ago)
    int r0 = w - di; if (r0 < 0) r0 += len;
    int r1 = r0 - 1; if (r1 < 0) r1 += len;
    float d = line[r0] + frac * (line[r1] - line[r0]);
    float fl = g * (b * d + (1.0f - b) * prev);
    prev = d;
    float ex = i < burst ? lp.Lp(nz.Next(), pa) : 0.0f;
    float y = fl + ex;
    line[w] = y;
    if (++w >= len) w = 0;
    out[i] += y;
  }
}

static void GenPluck(const float* p, uint32_t seed, bool stereo, Buf& b) {
  float hz = p[0], dec = p[1], bright = p[2], pick = p[3], det = p[4], body = p[5];
  b.Alloc(Cap(dec * 1.05f), stereo);
  KsString(hz, dec, bright, pick, seed, b.L(), b.n);
  if (stereo) KsString(hz * SemiToRatio(det * 0.01f), dec, bright, pick, seed + 7u, b.R(), b.n);
  if (body > 0.0f) {
    for (int c = 0; c < b.Channels(); c++) {
      Reso r; r.Set(body, 0.12f);
      float* x = b.Ch(c);
      for (int i = 0; i < b.n; i++) x[i] += 0.25f * r.Tick(x[i] * 0.2f);
    }
  }
  HighPass(b, 30.0f, 0.707f);
}

// ------------------------------------------------------------------------------------------------- chord stabs
static void GenChord(const float* p, uint32_t seed, bool stereo, Buf& b) {
  static const int kChords[10][5] = {
    {0, 4, 7, 12, -1},    // major
    {0, 3, 7, 12, -1},    // minor
    {0, 3, 7, 10, -1},    // min7
    {0, 4, 7, 11, -1},    // maj7
    {0, 5, 7, 12, -1},    // sus4
    {0, 7, 12, -1, -1},   // power
    {0, 4, 7, 10, -1},    // dom7
    {0, 3, 10, 14, -1},   // min9 voicing
    {0, 12, -1, -1, -1},  // octave (bass)
    {0, -1, -1, -1, -1},  // single
  };
  float root = p[0];
  int type = (int)p[1];
  if (type < 0 || type > 9) type = 0;
  int vpn = (int)p[2];
  if (vpn < 1) vpn = 1;
  if (vpn > 4) vpn = 4;
  float det = p[3], c0 = p[4], c1 = p[5], ft = p[6], q = p[7], adec = p[8];
  int wave = (int)p[9];
  b.Alloc(Cap(adec * 1.05f), stereo);
  Noise nz; nz.Seed(seed);
  const int kMaxV = 20;
  SawOsc so[kMaxV]; SqOsc qo[kMaxV]; SqOsc sub[5];
  float panl[kMaxV], panr[kMaxV];
  int nv = 0, nn = 0;
  for (int k = 0; k < 5 && kChords[type][k] >= 0; k++) {
    float f = MidiToHz(root + (float)kChords[type][k]);
    if (wave == 2) { sub[nn].Set(f * 0.5f); sub[nn].ph = nz.Uni(); }
    nn++;
    for (int v = 0; v < vpn; v++) {
      float c = vpn > 1 ? det * (-1.0f + 2.0f * (float)v / (float)(vpn - 1)) : 0.0f;
      float fv = f * SemiToRatio(c * 0.01f);
      so[nv].Set(fv); so[nv].ph = nz.Uni();
      qo[nv].Set(fv); qo[nv].ph = nz.Uni();
      float pan = stereo ? (vpn > 1 ? (float)v / (float)(vpn - 1) : 0.5f) : 0.5f;
      pan = 0.2f + 0.6f * pan;
      panl[nv] = cosf(pan * kPi * 0.5f); panr[nv] = sinf(pan * kPi * 0.5f);
      nv++;
    }
  }
  Svf fl, fr;
  float kA = DecayCoef60(adec), amp = 1.0f, kF = TauCoef(ft);
  float fe = 1.0f;
  int natt = (int)(0.003f * kSr);
  float norm = 1.0f / sqrtf((float)nv) * 0.7f;
  float* L = b.L();
  float* R = b.R();
  for (int i = 0; i < b.n; i++) {
    if ((i & 31) == 0) {
      float fc = c1 + (c0 - c1) * fe;
      fl.Set(fc, q); fr.Set(fc, q);
    }
    fe *= kF;
    float sl = 0.0f, sr = 0.0f;
    for (int v = 0; v < nv; v++) {
      float s = wave == 1 ? qo[v].Next() : so[v].Next();
      sl += s * panl[v]; sr += s * panr[v];
    }
    if (wave == 2) {
      float s = 0.0f;
      for (int k = 0; k < nn; k++) s += sub[k].Next();
      s *= 0.5f;
      sl += s; sr += s;
    }
    float g = amp * (i < natt ? (float)i / (float)natt : 1.0f) * norm;
    amp *= kA;
    if (stereo) { L[i] = fl.Lp(sl) * g; R[i] = fr.Lp(sr) * g; }
    else L[i] = fl.Lp((sl + sr) * 0.7f) * g;
  }
}

// ------------------------------------------------------------------------------------------------- formant voice
static void GenVox(const float* p, uint32_t seed, Buf& b) {
  static const float kVow[5][3] = {
    {800.0f, 1150.0f, 2900.0f},   // a
    {450.0f, 800.0f, 2830.0f},    // o
    {400.0f, 1700.0f, 2600.0f},   // e
    {270.0f, 2290.0f, 3010.0f},   // i (ee)
    {325.0f, 700.0f, 2530.0f},    // u
  };
  static const float kAmp[3] = {1.0f, 0.55f, 0.3f};
  static const float kBw[3] = {80.0f, 110.0f, 150.0f};
  float hz = p[0];
  int v1 = (int)p[1] % 5;
  float dur = p[2], gst = p[3], gt = p[4], vib = p[5], breath = p[6];
  int v2 = (int)p[7];
  float fsh = p[8] > 0.0f ? p[8] : 1.0f, rel = p[9];
  if (v2 < 0 || v2 > 4) v2 = v1;
  b.Alloc(Cap(dur + rel * 1.05f), false);
  Noise nz; nz.Seed(seed);
  SawOsc src;
  Svf f[3];
  float kG = TauCoef(gt), ge = gst > 0.0f ? gst - 1.0f : 0.0f;
  float vph = 0.0f;
  int nd = (int)(dur * kSr), na = (int)(0.008f * kSr);
  float kR = DecayCoef60(rel), rg = 1.0f;
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    if ((i & 15) == 0) {
      float m = nd > 0 ? (float)i / (float)nd : 1.0f;
      if (m > 1.0f) m = 1.0f;
      for (int k = 0; k < 3; k++) {
        float fq = Lerp(kVow[v1][k], kVow[v2][k], m) * fsh;
        f[k].Set(fq, fq / kBw[k]);
      }
    }
    vph += 5.5f / kSr; if (vph >= 1.0f) vph -= 1.0f;
    float pf = hz * (1.0f + ge) * SemiToRatio(vib * 0.01f * SinPhase(vph) * (i > na * 10 ? 1.0f : 0.0f));
    ge *= kG;
    src.Set(pf);
    float s = src.Next() + breath * nz.Next();
    float y = 0.0f;
    for (int k = 0; k < 3; k++) y += f[k].Bp(s) * kAmp[k];
    float g = i < na ? (float)i / (float)na : 1.0f;
    if (i >= nd) { g *= rg; rg *= kR; }
    x[i] = y * g;
  }
}

// ------------------------------------------------------------------------------------------------- zaps / lasers
static void GenZap(const float* p, uint32_t seed, Buf& b) {
  float f0 = p[0], f1 = p[1], st = p[2], dec = p[3];
  int wave = (int)p[4];
  float wob = p[5];
  int reps = (int)p[6];
  if (reps < 1) reps = 1;
  if (reps > 8) reps = 8;
  float drive = p[7], per = p[8];
  b.Alloc(Cap((reps - 1) * per + dec * 1.05f), false);
  (void)seed;
  float* x = b.L();
  float kS = TauCoef(st), kA = DecayCoef60(dec);
  for (int r = 0; r < reps; r++) {
    int s0 = (int)(r * per * kSr);
    float lev = powf(0.55f, (float)r);
    float fe = 1.0f, a = lev, ph = 0.0f, wph = 0.0f;
    SqOsc sq;
    int fin = (int)(0.0004f * kSr);
    for (int i = s0; i < b.n; i++) {
      float f = f1 + (f0 - f1) * fe;
      fe *= kS;
      wph += 23.0f / kSr; if (wph >= 1.0f) wph -= 1.0f;
      f *= 1.0f + wob * SinPhase(wph);
      float y;
      if (wave == 1) { sq.Set(f); y = sq.Next() * 0.6f; }
      else {
        ph += f / kSr; if (ph >= 1.0f) ph -= (float)(int)ph;
        y = wave == 2 ? (4.0f * fabsf(ph - 0.5f) - 1.0f) : SinPhase(ph);
      }
      int k = i - s0;
      if (k < fin) y *= (float)k / (float)fin;
      x[i] += y * a;
      a *= kA;
      if (a < 1e-5f) break;
    }
  }
  if (drive > 0.0f) Saturate(b, drive);
}

// ------------------------------------------------------------------------------------------------- sweeps / risers
static void GenSweep(const float* p, uint32_t seed, bool stereo, Buf& b) {
  float f0 = p[0], f1 = p[1], dur = p[2], q = p[3];
  int shape = (int)p[4];
  float tone = p[5], hpf = p[6];
  bool pink = p[7] > 0.5f;
  b.Alloc(Cap(dur), stereo);
  float kD = DecayCoef60(dur);
  for (int c = 0; c < b.Channels(); c++) {
    Noise nz; nz.Seed(seed + 55u * c);
    Pink pk;
    Svf f;
    float ph = 0.0f, fc = f0, env = 1.0f;
    float* x = b.Ch(c);
    for (int i = 0; i < b.n; i++) {
      float t = (float)i / (float)b.n;
      if ((i & 15) == 0) { fc = f0 * powf(f1 / f0, t); f.Set(fc, q); }
      float n = nz.Next();
      float s = pink ? pk.Next(n) * 2.5f : n;
      float l, bp, h;
      f.Process(s, &l, &bp, &h);
      float y = bp + 0.3f * l;
      if (tone > 0.0f) { ph += fc / kSr; if (ph >= 1.0f) ph -= (float)(int)ph; y += tone * SinPhase(ph) * 0.3f; }
      float g;
      if (shape == 0) g = t * t;
      else if (shape == 1) { g = env; env *= kD; }
      else { float s2 = sinf(kPi * t); g = s2 * s2; }
      x[i] = y * g;
    }
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
  FadeOut(b, 0.012f);
}

// ------------------------------------------------------------------------------------------------- glitch / bit textures
static void GenGlitch(const float* p, uint32_t seed, Buf& b) {
  int mode = (int)p[0];
  float rate = p[1], dur = p[2], dec = p[3], base = p[4], bits = p[5], dens = p[6];
  b.Alloc(Cap(dur), false);
  Noise nz; nz.Seed(seed);
  float* x = b.L();
  float kA = DecayCoef60(dec), amp = 1.0f;
  if (mode == 0) {   // data: random sample-and-hold segments of tones / noise / silence
    int seg = (int)(kSr / rate);
    if (seg < 8) seg = 8;
    SqOsc o;
    int kind = 0;
    float held = 0.0f;
    for (int i = 0; i < b.n; i++) {
      if (i % seg == 0) {
        kind = nz.Uni() < dens ? 1 + (int)(nz.Uni() * 3.0f) : 0;
        o.Set(base * (float)(1 + (int)(nz.Uni() * 8.0f)));
        held = nz.Next();
      }
      float y = kind == 1 ? o.Next() * 0.7f : (kind == 2 ? nz.Next() : (kind == 3 ? held : 0.0f));
      x[i] = y * amp;
      amp *= kA;
    }
  } else if (mode == 1) {   // stutter: a grain repeated faster and faster (bouncing)
    float t = 0.0f, gap = 1.0f / rate;
    Svf f; f.Set(base, 4.0f);
    float lev = 1.0f;
    while (t < dur - 0.01f && gap > 0.004f) {
      int s0 = (int)(t * kSr);
      int gl = (int)(0.012f * kSr);
      for (int k = 0; k < gl && s0 + k < b.n; k++) {
        float e = expf(-(float)k / (0.003f * kSr));
        x[s0 + k] += f.Bp(nz.Next()) * e * lev * 3.0f;
      }
      t += gap;
      gap *= dens;
      lev *= 0.93f;
    }
  } else if (mode == 2) {   // bleeps: a random melody of short square notes
    int seg = (int)(kSr / rate);
    if (seg < 32) seg = 32;
    SqOsc o;
    float ne = 0.0f, kN = DecayCoef60((float)seg / kSr * 0.9f);
    for (int i = 0; i < b.n; i++) {
      if (i % seg == 0) {
        o.Set(base * SemiToRatio((float)(int)(nz.Uni() * 25.0f)));
        ne = nz.Uni() < dens ? 1.0f : 0.0f;
      }
      x[i] = o.Next() * 0.6f * ne * amp;
      int k = i % seg;
      if (k < 24) x[i] *= (float)k / 24.0f;
      ne *= kN;
      amp *= kA;
    }
  } else {   // crackle: sparse filtered impulses over a little hiss
    Svf f; f.Set(base, 1.5f);
    float pr = rate / kSr;
    for (int i = 0; i < b.n; i++) {
      float imp = nz.Uni() < pr ? nz.Next() * (nz.Uni() < dens ? 4.0f : 1.0f) : 0.0f;
      x[i] = (f.Bp(imp) * 2.0f + imp * 0.5f + nz.Next() * 0.01f) * amp;
      amp *= kA;
    }
  }
  if (bits > 0.0f) Crush(b, bits);
  FadeOut(b, 0.006f);
}

// ------------------------------------------------------------------------------------------------- shakers / tambourine
static void GenShaker(const float* p, uint32_t seed, Buf& b) {
  float att = p[0], dec = p[1], hpf = p[2], bead = p[3], bq = p[4], coll = p[5], jing = p[6], jhz = p[7];
  float gap = p[8], jdec = p[9];
  float len = gap + att + dec * 1.05f + (jing > 0.0f ? jdec : 0.0f);
  b.Alloc(Cap(len), false);
  Noise nz; nz.Seed(seed);
  Svf f; f.Set(bead, bq);
  Reso jr[3];
  static const float kJ[3] = {1.0f, 1.34f, 1.79f};
  for (int k = 0; k < 3; k++) jr[k].Set(jhz * kJ[k], jdec);
  int na = (int)(att * kSr), g0 = (int)(gap * kSr);
  float kD = DecayCoef60(dec);
  float pc = coll / kSr;
  float e1 = 0.0f, e2 = 0.0f;
  float* x = b.L();
  for (int i = 0; i < b.n; i++) {
    // energy of stroke 1 and stroke 2 (attack ramp then exponential decay)
    float s1 = i < na ? (float)i / (float)na : (e1 == 0.0f ? (e1 = 1.0f) : (e1 *= kD));
    float s2 = 0.0f;
    if (gap > 0.0f && i >= g0) {
      int k = i - g0;
      s2 = 0.7f * (k < na ? (float)k / (float)na : (e2 == 0.0f ? (e2 = 1.0f) : (e2 *= kD)));
    }
    float E = s1 + s2;
    float n = nz.Next();
    float imp = nz.Uni() < pc * (0.3f + E) ? nz.Next() * E * 3.0f : 0.0f;
    float y = f.Bp(n * E * 0.6f + imp);
    if (jing > 0.0f) {
      float j = 0.0f;
      for (int k = 0; k < 3; k++) j += jr[k].Tick(imp * 0.2f);
      y += j * jing;
    }
    x[i] = y;
  }
  if (hpf > 0.0f) HighPass(b, hpf, 0.707f);
}

void Generate(int gen, const float* p, uint32_t seed, bool stereo, Buf& out) {
  switch (gen) {
    case G_HAT: GenHat(p, seed, out); break;
    case G_FM: GenFm(p, seed, out); break;
    case G_NOISE: GenNoise(p, seed, stereo, out); break;
    case G_MODAL: GenModal(p, seed, out); break;
    case G_CLAP: GenClap(p, seed, stereo, out); break;
    case G_COW: GenCow(p, seed, out); break;
    case G_DRUM: GenDrum(p, seed, out); break;
    case G_CYM: GenCym(p, seed, stereo, out); break;
    case G_PLUCK: GenPluck(p, seed, stereo, out); break;
    case G_CHORD: GenChord(p, seed, stereo, out); break;
    case G_VOX: GenVox(p, seed, out); break;
    case G_ZAP: GenZap(p, seed, out); break;
    case G_SWEEP: GenSweep(p, seed, stereo, out); break;
    case G_GLITCH: GenGlitch(p, seed, out); break;
    case G_SHAKER: GenShaker(p, seed, out); break;
    default: out.Alloc(0.01f, false); break;
  }
}

}  // namespace syn
}  // namespace dlx
