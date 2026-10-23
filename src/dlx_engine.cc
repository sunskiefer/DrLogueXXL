// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- engine core: see engine.h. Signal flow per 48 kHz block (as the drumlogue's):
//
//   part -> level/pan/spread -+-> MAIN bus (MASTER On) ----> [master FX] --+--> + BYPASS bus --> volume
//                             |   or BYPASS bus (MASTER Bypass)            |     + returns set to Post
//                             +-> DELAY send ---> delay --+-> return Pre ---+ (into the master FX)
//                             |                           +-> To Reverb send
//                             +-> REVERB send --> reverb ---> return Pre/Post
//                             +-> SIDECHAIN key (mono) -> the master compressor's detector
//   -> 48 -> 44.1 kHz resampler -> brickwall limiter (RMXXXL's) -> int16
#include "dlx_engine.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "denormals.h"
#include "dsp.h"
#include "fx/fx.h"
#include "limiter.h"
#include "part.h"
#include "parts/multi.h"
#include "resampler.h"
#include "rom/sample_rom.h"
#include "ui_pages.h"

namespace dlx {

// ---------------------------------------------------------------------------------------------------------------
// key lookup: a sorted index built once (C++11 function-local statics initialise thread-safely)
namespace {

struct KeyIndex {
  int order[P_COUNT];
  KeyIndex() {
    for (int i = 0; i < P_COUNT; ++i) order[i] = i;
    std::sort(order, order + P_COUNT, [](int a, int b) { return strcmp(kParams[a].key, kParams[b].key) < 0; });
  }
  int Find(const char* key) const {
    int lo = 0, hi = P_COUNT - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >> 1;
      int c = strcmp(kParams[order[mid]].key, key);
      if (c == 0) return order[mid];
      if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
  }
};

const KeyIndex& Keys() {
  static KeyIndex k;
  return k;
}

// first sound key and first mix key (LEVEL) of each part
const int kSoundStart[NUM_PARTS] = {P_BD_DECAY, P_SD_DECAY, P_LT_DECAY, P_HT_DECAY, P_CH_BANK, P_OH_BANK,
                                    P_RS_BANK, P_CP_BANK, P_SP1_BANK, P_SP2_BANK, P_MT_ENGINE};
const int kMixStart[NUM_PARTS] = {P_BD_LEVEL, P_SD_LEVEL, P_LT_LEVEL, P_HT_LEVEL, P_CH_LEVEL, P_OH_LEVEL,
                                  P_RS_LEVEL, P_CP_LEVEL, P_SP1_LEVEL, P_SP2_LEVEL, P_MT_LEVEL};
// mix keys (BD and the toms have no SPREAD, so these are looked up per part at construction)
struct MixIds { int level, pan, spread, dly, rev, master, sidech, choke, prio, mute, note; };

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
void NoteName(int n, char* buf, int len) { snprintf(buf, len, "%s%d", kNoteNames[n % 12], n / 12 - 1); }

bool EndsWith(const char* s, const char* suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  return a >= b && !strcmp(s + a - b, suffix);
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int B64Val(char c) {
  const char* p = strchr(kB64, c);
  return (c && p) ? (int)(p - kB64) : -1;
}

}  // namespace

void PartFilter::Format(float cutoff, char* buf, int len) {
  if (cutoff < -0.25f) snprintf(buf, len, "L%.1f%%", 100.0f + cutoff);
  else if (cutoff > 0.25f) snprintf(buf, len, "H%.1f%%", cutoff);
  else snprintf(buf, len, "THRU");
}

// ---------------------------------------------------------------------------------------------------------------
struct Engine::Impl {
  float v[P_COUNT];
  float e[P_COUNT];          // what the parts and effects hear: v with the macros applied (ApplyMacros)
  // the screen (ui_pages.h): the page shown per selection, the pages of the current one, the knobs' targets
  int ui_page[NUM_SEL];
  int pages[kMaxPages][4];
  int npages = 0;
  int target[4] = {-1, -1, -1, -1};
  int unit_slots = 0;
  int last_unit = kNumBuiltinEngines;
  // the machine's "parameter pop-up": a control turned on the panel shows on the screen's title for a moment
  int touched = -1;
  long touch_until = 0;
  long blocks = 0;
  bool restoring = false;   // a state restore: the selection must not switch the MULTI source
  Part* parts[NUM_PARTS];
  MultiPart* multi;
  MixIds mix[NUM_PARTS];
  int last_engine = 0;

  // mixer state, ramped per block
  struct Ramp { float gl = 0, gr = 0, dly = 0, rev = 0, sc = 0; };
  Ramp ramp[NUM_PARTS];
  float spread_buf[NUM_PARTS][1024];
  int spread_pos[NUM_PARTS];

  float tl[kBlock], tr[kBlock];
  float main_l[kBlock], main_r[kBlock], byp_l[kBlock], byp_r[kBlock];
  float dly_in_l[kBlock], dly_in_r[kBlock], rev_in_l[kBlock], rev_in_r[kBlock], key[kBlock];
  float dly_out_l[kBlock], dly_out_r[kBlock], rev_out_l[kBlock], rev_out_r[kBlock];
  float vol_cur = 1.0f;

  DelayFx dly;
  ReverbFx rev;
  MasterFx mfx;
  int last_dly_type = -1, last_rev_type = -1, last_mfx_type = -1;
  Resampler48To44 rs;
  Limiter lim;
  float bpm = 120.0f;

  // user samples restored by name, resolved once the USER bank is ready
  char pend_name[NUM_PARTS][32];
  bool pend[NUM_PARTS];

  uint8_t changed[P_COUNT];
  int changed_scan = 0;

  float out_l[128], out_r[128];
  bool chroma_held[128];
};

Engine::Engine(const char* data_dir) : d_(new Impl) {
  Impl& s = *d_;
  memset(s.spread_buf, 0, sizeof s.spread_buf);
  memset(s.spread_pos, 0, sizeof s.spread_pos);
  memset(s.changed, 0, sizeof s.changed);
  memset(s.pend, 0, sizeof s.pend);
  memset(s.chroma_held, 0, sizeof s.chroma_held);
  Keys();
  for (int i = 0; i < P_COUNT; ++i) s.v[i] = kParams[i].def;
  memcpy(s.e, s.v, sizeof s.e);
  memset(s.ui_page, 0, sizeof s.ui_page);
  char k[48];
  for (int p = 0; p < NUM_PARTS; ++p) {
    MixIds& m = s.mix[p];
    auto id = [&](const char* suffix) { snprintf(k, sizeof k, "%s_%s", kPartKeys[p], suffix); return Keys().Find(k); };
    m.level = id("level"); m.pan = id("pan"); m.spread = id("spread"); m.dly = id("dly"); m.rev = id("rev");
    m.master = id("master"); m.sidech = id("sidech"); m.choke = id("choke"); m.prio = id("prio");
    m.mute = id("mute"); m.note = id("note");
  }
  SampleRom::Get().Start(data_dir);
  uint32_t seed = 0x51ED270Bu ^ (uint32_t)(uintptr_t)this;
  s.parts[PART_BD] = NewBassDrum();
  s.parts[PART_SD] = NewSnareDrum();
  s.parts[PART_LT] = NewTom();
  s.parts[PART_HT] = NewTom();
  for (int p = PART_CH; p <= PART_SP2; ++p) s.parts[p] = NewSamplePart();
  s.multi = NewMulti();
  s.parts[PART_MT] = s.multi;
  for (int p = 0; p < NUM_PARTS; ++p) s.parts[p]->Init(&s.e[kSoundStart[p]], p, seed + 7919u * p);
  float defaults[kNumUnitSlots];
  s.unit_slots = s.multi->EngineChanged(0, defaults);
  RefreshUi();
  s.dly.Init();
  s.rev.Init();
  s.mfx.Init();
  s.rs.Init();
  s.lim.Init();
}

Engine::~Engine() {
  for (int p = 0; p < NUM_PARTS; ++p) delete d_->parts[p];
  delete d_;
}

float Engine::Value(int id) const { return id >= 0 && id < P_COUNT ? d_->v[id] : 0.0f; }

void Engine::SetValue(int id, float x) {
  Impl& s = *d_;
  if (id < 0 || id >= P_COUNT) return;
  if (id >= P_P1 && id <= P_P4) { SetKnob(id - P_P1, x); return; }
  if (id == P_PAGE_UP || id == P_PAGE_DOWN) {
    if (x > 0.5f) {
      int sel = (int)s.v[P_SEL];
      int pg = s.ui_page[sel] + (id == P_PAGE_UP ? -1 : 1);
      if (pg >= 0 && pg < s.npages) { s.ui_page[sel] = pg; RefreshUi(); }
    }
    s.v[id] = 0.0f;
    return;
  }
  const ParamInfo& pi = kParams[id];
  if (x != x) return;   // NaN
  x = Clamp(x, pi.min, pi.max);
  if (pi.whole) x = floorf(x + 0.5f);
  float old = s.v[id];
  s.v[id] = x;
  if (id == P_MT_ENGINE && (int)x != s.last_engine) {
    // a new synth engine: its parameter slots take that engine's own defaults (reported back to the host)
    s.last_engine = (int)x;
    float defaults[kNumUnitSlots];
    s.parts[PART_MT]->Choke();
    s.unit_slots = s.multi->EngineChanged(s.last_engine, defaults);
    if (s.last_engine >= kNumBuiltinEngines) s.last_unit = s.last_engine;
    s.changed[P_MT_ENGINE] = 1;
    for (int i = 0; i < kNumUnitSlots; ++i) {
      if (s.v[P_U_1 + i] != defaults[i]) s.changed[P_U_1 + i] = 1;
      s.v[P_U_1 + i] = defaults[i];
    }
  }
  if (id == P_PANIC && x > 0.5f) { Panic(); s.v[id] = 0.0f; }
  if (id == P_RESCAN && x > 0.5f) { SampleRom::Get().Rescan(); s.v[id] = 0.0f; }
  for (int p = 0; p < NUM_PARTS; ++p)
    if (id == s.mix[p].mute && x > 0.5f && old <= 0.5f) s.parts[p]->Choke();
  for (int p = PART_CH; p <= PART_SP2; ++p)
    if (id == kSoundStart[p] + PCM_SAMPLE || id == kSoundStart[p] + PCM_BANK) s.pend[p] = false;
  if (id == P_SEL && (int)x != (int)old) {
    // as PART + VPM / NOISE / USER on the machine: picking a MULTI source plays it
    int sel = (int)x;
    if (s.restoring) sel = -1;
    if (sel == SEL_VPM) SetValue(P_MT_ENGINE, 0);
    else if (sel == SEL_NOISE) SetValue(P_MT_ENGINE, 1);
    else if (sel == SEL_USER && s.v[P_MT_ENGINE] < kNumBuiltinEngines) SetValue(P_MT_ENGINE, (float)s.last_unit);
    RefreshUi();
  } else if (id == P_MT_ENGINE || id == P_MFX_TYPE || id == P_DLY_TYPE) {
    if (x != old) RefreshUi();
  }
  for (int k = 0; k < 4; ++k)   // a control the screen's knobs point at moved: their values follow
    if (s.target[k] == id && x != old) { s.v[P_P1 + k] = Norm(id); s.changed[P_P1 + k] = 1; }
}

void Engine::UpperName(int id, char* buf, int len) const {
  int i = 0;
  for (const char* c = kParams[id].name; *c && i < len - 1; ++c) buf[i++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
  buf[i] = 0;
}

float Engine::Norm(int id) const {
  const ParamInfo& pi = kParams[id];
  return pi.max > pi.min ? (d_->v[id] - pi.min) / (pi.max - pi.min) : 0.0f;
}

// the selection's pages, the page shown, the four knobs' targets; tells the host what changed with them
void Engine::RefreshUi() {
  Impl& s = *d_;
  int sel = (int)s.v[P_SEL];
  if (sel < 0 || sel >= NUM_SEL) sel = 0;
  s.npages = UiPages(sel, s.v, s.pages, s.unit_slots);
  if (s.ui_page[sel] >= s.npages) s.ui_page[sel] = s.npages > 0 ? s.npages - 1 : 0;
  for (int k = 0; k < 4; ++k) {
    s.target[k] = s.npages > 0 ? s.pages[s.ui_page[sel]][k] : -1;
    s.v[P_P1 + k] = s.target[k] >= 0 ? Norm(s.target[k]) : 0.0f;
    s.changed[P_P1 + k] = 1;
  }
  // the wave screen's picture (tools/gen_params.py SCOPES)
  int scope;
  if (sel <= SEL_SP2) scope = sel;
  else if (sel == SEL_VPM) scope = 10 + (int)s.v[P_VPM_PRESET];
  else if (sel == SEL_NOISE) scope = 18 + (int)s.v[P_NOISE_ALGO];
  else scope = 25 + (sel - SEL_USER);
  if (s.v[P_SCOPE] != (float)scope) { s.v[P_SCOPE] = (float)scope; s.changed[P_SCOPE] = 1; }
  s.changed[P_LCD_TITLE] = 1;
  s.changed[P_LCD_PAGE] = 1;
}

// one of the four knobs under the screen: a small move steps an option list or a short whole-number range by one
// (a Q-Link event or a wheel click), a bigger one sets the value outright
void Engine::SetKnob(int k, float x) {
  Impl& s = *d_;
  int t = s.target[k];
  if (t < 0) { s.v[P_P1 + k] = 0.0f; return; }
  const ParamInfo& pi = kParams[t];
  x = Clamp(x, 0.0f, 1.0f);
  float span = pi.max - pi.min;
  int steps = pi.num_opts ? pi.num_opts - 1 : (pi.whole ? (int)span : 0);
  float cur = Norm(t);
  float value = pi.min + x * span;
  if (steps > 0 && steps <= 40) {
    float d = x - cur;
    if (fabsf(d) > 1e-5f && fabsf(d) <= 0.03f) value = s.v[t] + (d > 0 ? 1.0f : -1.0f) * (span / steps);
  }
  if (t == P_PANIC || t == P_RESCAN) { SetValue(t, x > 0.5f ? 1.0f : 0.0f); s.v[P_P1 + k] = 0.0f; return; }
  SetValue(t, value);
  s.v[P_P1 + k] = Norm(t);
  s.changed[t] = 1;   // the kit knob or mixer control of the same parameter follows
}

void Engine::SetParam(const char* key, const char* val) {
  Impl& s = *d_;
  if (!key || !val) return;
  if (!strcmp(key, "lfo_bpm")) {   // HAS_LFO_BPM: the host tempo
    float b = (float)atof(val);
    if (b > 20.0f && b < 999.0f) { s.bpm = b; s.multi->SetTempo(b); }
    return;
  }
  if (!strcmp(key, "state")) {
    // "DLX1;<base64 of one uint16 per param, in index order>;<part key>=<user sample name>;..."
    if (strncmp(val, "DLX1;", 5)) return;
    const char* p = val + 5;
    const char* end = strchr(p, ';');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    uint8_t bytes[P_COUNT * 2 + 8];
    size_t nb = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n && nb < sizeof bytes; ++i) {
      int b = B64Val(p[i]);
      if (b < 0) continue;
      acc = (acc << 6) | (uint32_t)b;
      bits += 6;
      if (bits >= 8) { bits -= 8; bytes[nb++] = (uint8_t)(acc >> bits); }
    }
    int count = (int)(nb / 2);
    s.restoring = true;
    for (int i = 0; i < P_COUNT; ++i) {
      // triggers and the screen's own values (derived from the selection) are not restored
      if (i == P_PANIC || i == P_RESCAN || i == P_PAGE_UP || i == P_PAGE_DOWN || (i >= P_P1 && i <= P_P4) ||
          i == P_LCD_TITLE || i == P_LCD_PAGE || i == P_SCOPE) continue;
      float x = kParams[i].def;   // params newer than the saved state keep their defaults
      if (i < count) {
        float u = (float)((bytes[2 * i] << 8) | bytes[2 * i + 1]) / 65535.0f;
        x = kParams[i].min + u * (kParams[i].max - kParams[i].min);
      }
      SetValue(i, x);
    }
    s.restoring = false;
    RefreshUi();
    // user sample names
    while (end && *end == ';') {
      p = end + 1;
      end = strchr(p, ';');
      const char* eq = strchr(p, '=');
      if (!eq || (end && eq > end)) continue;
      char part[8];
      size_t kl = (size_t)(eq - p);
      if (kl >= sizeof part) continue;
      memcpy(part, p, kl);
      part[kl] = 0;
      size_t vl = end ? (size_t)(end - eq - 1) : strlen(eq + 1);
      for (int q = PART_CH; q <= PART_SP2; ++q)
        if (!strcmp(part, kPartKeys[q])) {
          size_t m = vl < 31 ? vl : 31;
          memcpy(s.pend_name[q], eq + 1, m);
          s.pend_name[q][m] = 0;
          s.pend[q] = true;
        }
    }
    return;
  }
  int id = Keys().Find(key);
  if (id < 0) return;
  float old = s.v[id];
  SetValue(id, (float)atof(val));
  bool ui = id >= P_SEL || id == P_PANIC || id == P_RESCAN;
  bool on_screen = false;
  for (int k = 0; k < 4; ++k) on_screen |= s.target[k] == id;
  if (!ui && !on_screen && s.v[id] != old) {
    s.touched = id;
    s.touch_until = s.blocks + 260;   // ~1.5 s of 48 kHz blocks
    s.changed[P_LCD_TITLE] = 1;
  }
}

int Engine::GetParam(const char* key, char* buf, int len) {
  Impl& s = *d_;
  if (!key || !buf || len < 2) return 0;
  if (!strcmp(key, "state")) {
    // see SetParam("state")
    int o = snprintf(buf, len, "DLX1;");
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < P_COUNT && o < len - 8; ++i) {
      const ParamInfo& pi = kParams[i];
      float u = pi.max > pi.min ? (s.v[i] - pi.min) / (pi.max - pi.min) : 0.0f;
      uint32_t q = (uint32_t)(Clamp(u, 0.0f, 1.0f) * 65535.0f + 0.5f);
      for (int b = 0; b < 2; ++b) {
        acc = (acc << 8) | ((q >> (8 - 8 * b)) & 0xFF);
        bits += 8;
        while (bits >= 6) { bits -= 6; buf[o++] = kB64[(acc >> bits) & 63]; }
      }
    }
    if (bits > 0 && o < len - 1) buf[o++] = kB64[(acc << (6 - bits)) & 63];
    buf[o] = 0;
    for (int p = PART_CH; p <= PART_SP2; ++p) {
      int bank = (int)s.v[kSoundStart[p] + PCM_BANK];
      if (bank != BANK_USER) continue;
      const char* name = nullptr;
      if (s.pend[p]) name = s.pend_name[p];
      else {
        const Sample* sm = SampleRom::Get().Find(BANK_USER, (int)s.v[kSoundStart[p] + PCM_SAMPLE] - 1);
        if (sm) name = sm->name;
      }
      if (!name || !*name) continue;
      char clean[32];
      int c = 0;
      for (const char* q = name; *q && c < 31; ++q) clean[c++] = (*q == ';' || *q == '=') ? '_' : *q;
      clean[c] = 0;
      o += snprintf(buf + o, len - o > 0 ? len - o : 0, ";%s=%s", kPartKeys[p], clean);
      if (o >= len) { buf[len - 1] = 0; break; }
    }
    return (int)strlen(buf) + 1;
  }
  size_t kl = strlen(key);
  // "<key>_name": dynamic names of the unit and master-FX slots
  if (EndsWith(key, "_name") && kl < 40) {
    char base[48];
    memcpy(base, key, kl - 5);
    base[kl - 5] = 0;
    int id = Keys().Find(base);
    if (id >= P_P1 && id <= P_P4) {
      int t = s.target[id - P_P1];
      if (t < 0) { snprintf(buf, len, "---"); return 1; }
      if (t >= P_U_1 && t < P_U_1 + kNumUnitSlots) {
        if (!s.multi->SlotName(t - P_U_1, buf, len) || !buf[0]) snprintf(buf, len, "---");
      } else if (t >= P_MFX_U1 && t <= P_MFX_U6) {
        const char* n = s.mfx.SlotName(t - P_MFX_U1, s.v);
        snprintf(buf, len, "%s", n ? n : "---");
      } else {
        UpperName(t, buf, len);
      }
      return 1;
    }
    if (id >= P_U_1 && id < P_U_1 + kNumUnitSlots) {
      if (!s.multi->SlotName(id - P_U_1, buf, len) || !buf[0]) snprintf(buf, len, "---");
      return 1;
    }
    if (id >= P_MFX_U1 && id <= P_MFX_U6) {
      const char* n = s.mfx.SlotName(id - P_MFX_U1, s.v);
      snprintf(buf, len, "%s", n ? n : "---");
      return 1;
    }
    return 0;
  }
  // "<key>_display": value text
  if (EndsWith(key, "_display") && kl < 48) {
    char base[48];
    memcpy(base, key, kl - 8);
    base[kl - 8] = 0;
    int id = Keys().Find(base);
    if (id < 0) return 0;
    if (id >= P_P1 && id <= P_P4) {
      int t = s.target[id - P_P1];
      if (t < 0) { snprintf(buf, len, " "); return 1; }
      if (kParams[t].num_opts) {
        snprintf(buf, len, "%s", kParams[t].opts[(int)s.v[t]]);
        return 1;
      }
      char k2[56];
      snprintf(k2, sizeof k2, "%s_display", kParams[t].key);
      return GetParam(k2, buf, len);
    }
    float x = s.v[id];
    const ParamInfo& pi = kParams[id];
    if (id >= P_U_1 && id < P_U_1 + kNumUnitSlots) {
      if (!s.multi->SlotDisplay(id - P_U_1, x, buf, len)) snprintf(buf, len, "---");
      return 1;
    }
    for (int p = 0; p < NUM_PARTS; ++p)
      if (id >= kSoundStart[p] && id < kMixStart[p] && s.parts[p]->Format(id - kSoundStart[p], x, buf, len)) return 1;
    if (!strncmp(base, "dly_", 4) && s.dly.Format(id, x, buf, len)) return 1;
    if (!strncmp(base, "rev_", 4) && s.rev.Format(id, x, buf, len)) return 1;
    if (s.mfx.Format(id, x, buf, len)) return 1;
    if (pi.min == -100.0f && EndsWith(base, "_cutoff")) { PartFilter::Format(x, buf, len); return 1; }
    if (EndsWith(base, "_pan")) {
      if (fabsf(x) < 0.5f) snprintf(buf, len, "C");
      else snprintf(buf, len, "%c%.0f%%", x < 0 ? 'L' : 'R', fabsf(x));
      return 1;
    }
    if (EndsWith(base, "_spread")) { snprintf(buf, len, "%.0f%%", 100.0f + x); return 1; }
    if (EndsWith(base, "_note") || id == P_BASE_NOTE || id == P_KEY_SPLIT || id == P_MT_UNOTE || id == P_VPM_NOTE) {
      NoteName((int)x, buf, len);
      return 1;
    }
    if (id >= P_CH_BANK && id <= P_SP2_DRIVE && EndsWith(base, "_tune")) { snprintf(buf, len, "%+.2f", x); return 1; }
    if (id == P_LIM_DRIVE || id == P_LIM_CEILING || id == P_COMP_THRESH || id == P_COMP_MAKEUP ||
        id == P_COMP_KNEE || id == P_BOOST_GAIN || id == P_EQ_LOW || id == P_EQ_MID || id == P_EQ_HIGH ||
        id == P_EQ_GAIN) {
      snprintf(buf, len, "%.1fdB", x);
      return 1;
    }
    if (id == P_COMP_ATTACK || id == P_COMP_RELEASE) { snprintf(buf, len, "%.*fms", x < 10 ? 1 : 0, x); return 1; }
    if (id == P_COMP_RATIO) { snprintf(buf, len, "%.1f:1", x); return 1; }
    if (id == P_PANIC || id == P_RESCAN) { snprintf(buf, len, "PUSH"); return 1; }
    if (pi.whole) { snprintf(buf, len, "%d", (int)x); return 1; }
    if (pi.min >= -100.0f && pi.max <= 100.0f && (pi.max == 100.0f)) { snprintf(buf, len, "%.0f%%", x); return 1; }
    snprintf(buf, len, "%.2f", x);
    return 1;
  }
  if (!strcmp(key, "lcd_title")) {
    if (s.touched >= 0 && s.blocks < s.touch_until) {
      char name[32], val[40], k2[56];
      UpperName(s.touched, name, sizeof name);
      const char* part = "";
      for (int p = 0; p < NUM_PARTS; ++p)
        if (s.touched >= kSoundStart[p] && s.touched <= s.mix[p].note) part = kPartNames[p];
      if (kParams[s.touched].num_opts) snprintf(val, sizeof val, "%s", kParams[s.touched].opts[(int)s.v[s.touched]]);
      else { snprintf(k2, sizeof k2, "%s_display", kParams[s.touched].key); GetParam(k2, val, sizeof val); }
      snprintf(buf, len, "%s%s%s  %s", part, *part ? " " : "", name, val);
      return 1;
    }
    int sel = (int)s.v[P_SEL];
    int en = (int)s.v[P_MT_ENGINE];
    UiTitle(sel, s.v, kParams[P_MT_ENGINE].opts[en < 0 || en >= kParams[P_MT_ENGINE].num_opts ? 0 : en], buf, len);
    return 1;
  }
  if (!strcmp(key, "lcd_page")) {
    snprintf(buf, len, "%d/%d", s.ui_page[(int)s.v[P_SEL]] + 1, s.npages > 0 ? s.npages : 1);
    return 1;
  }
  int id = Keys().Find(key);
  if (id < 0) return 0;
  if (kParams[id].whole) snprintf(buf, len, "%d", (int)s.v[id]);
  else snprintf(buf, len, "%.6g", s.v[id]);
  return 1;
}

int Engine::NextChanged(char* key, int len) {
  Impl& s = *d_;
  for (int n = 0; n < P_COUNT; ++n) {
    int i = (s.changed_scan + n) % P_COUNT;
    if (s.changed[i]) {
      s.changed[i] = 0;
      s.changed_scan = i + 1;
      snprintf(key, len, "%s", kParams[i].key);
      return 1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------------------------------------------
void Engine::Panic() {
  Impl& s = *d_;
  for (int p = 0; p < NUM_PARTS; ++p) s.parts[p]->Choke();
  memset(s.chroma_held, 0, sizeof s.chroma_held);
  s.dly.Clear();
  s.rev.Clear();
  s.mfx.Clear();
}

static void TriggerPart(Engine::Impl& s, int p, float vel, int note);

void Engine::NoteOn(int note, int velocity) {
  Impl& s = *d_;
  if (velocity <= 0) { NoteOff(note); return; }
  float v01 = velocity / 127.0f;
  float sens = s.v[P_VEL_SENS] * 0.01f;
  float vel = (1.0f - sens) + sens * powf(v01, 1.6f);
  if (s.v[P_MULTI_KEYS] > 0.5f && note >= (int)s.v[P_KEY_SPLIT]) {
    if (s.v[s.mix[PART_MT].mute] > 0.5f) return;
    s.chroma_held[note & 127] = true;
    TriggerPart(s, PART_MT, vel, note);
    return;
  }
  int map = (int)s.v[P_NOTE_MAP];
  for (int p = 0; p < NUM_PARTS; ++p) {
    int n = map == 0 ? (int)s.v[P_BASE_NOTE] + p : (map == 1 ? kGmNotes[p] : (int)s.v[s.mix[p].note]);
    if (n == note) TriggerPart(s, p, vel, -1);
  }
}

void Engine::NoteOff(int note) {
  Impl& s = *d_;
  if (s.chroma_held[note & 127]) {
    s.chroma_held[note & 127] = false;
    s.parts[PART_MT]->Release(note);
    return;
  }
  int map = (int)s.v[P_NOTE_MAP];
  int n = map == 0 ? (int)s.v[P_BASE_NOTE] + PART_MT : (map == 1 ? kGmNotes[PART_MT] : (int)s.v[s.mix[PART_MT].note]);
  if (n == note) s.parts[PART_MT]->Release(-1);
}

static void TriggerPart(Engine::Impl& s, int p, float vel, int note) {
  const MixIds& m = s.mix[p];
  if (s.v[m.mute] > 0.5f) return;
  int group = (int)s.v[m.choke];
  if (group > 0) {
    int prio = (int)s.v[m.prio];
    for (int q = 0; q < NUM_PARTS; ++q)
      if (q != p && (int)s.v[s.mix[q].choke] == group && (int)s.v[s.mix[q].prio] <= prio) s.parts[q]->Choke();
  }
  s.parts[p]->Trigger(vel, note);
}

void Engine::Midi(const uint8_t* msg, int len) {
  if (!msg || len < 1) return;
  uint8_t st = msg[0] & 0xF0;
  if (st == 0x90 && len >= 3) NoteOn(msg[1] & 127, msg[2] & 127);
  else if (st == 0x80 && len >= 3) NoteOff(msg[1] & 127);
  else if (st == 0xB0 && len >= 3 && (msg[1] == 120 || msg[1] == 123)) {
    Impl& s = *d_;
    for (int n = 0; n < 128; ++n)
      if (s.chroma_held[n]) { s.chroma_held[n] = false; s.parts[PART_MT]->Release(n); }
    if (msg[1] == 120) Panic();
  }
}

// ---------------------------------------------------------------------------------------------------------------
// The four macros of the SCREEN page: offsets over the whole kit, applied to what the parts hear (s.e), never to the
// saved values. DECAY lengthens/shortens every part, TUNE moves every part's pitch, FILTER pushes every part's
// cutoff toward low-pass (left) or high-pass (right), SPACE adds delay and reverb send to every part.
static void ApplyMacros(Engine::Impl& s) {
  memcpy(s.e, s.v, sizeof s.e);
  float dec = s.v[P_MACRO1] * 0.01f, tune = s.v[P_MACRO2] * 0.01f, filt = s.v[P_MACRO3], space = s.v[P_MACRO4] * 0.01f;
  auto add = [&](int id, float d) { s.e[id] = Clamp(s.e[id] + d, kParams[id].min, kParams[id].max); };
  if (dec != 0.0f) {
    const int ids[] = {P_BD_DECAY, P_SD_DECAY, P_SD_SNAPDEC, P_LT_DECAY, P_HT_DECAY};
    for (int id : ids) add(id, dec * 400.0f);
    for (int p = PART_CH; p <= PART_SP2; ++p) add(kSoundStart[p] + PCM_DECAY, dec * 400.0f);
  }
  if (tune != 0.0f) {
    const int ids[] = {P_BD_TUNE, P_SD_TUNE, P_LT_TUNE, P_HT_TUNE};
    for (int id : ids) add(id, tune * 250.0f);
    for (int p = PART_CH; p <= PART_SP2; ++p) add(kSoundStart[p] + PCM_TUNE, tune * 12.0f);
  }
  if (filt != 0.0f) {
    const int ids[] = {P_BD_CUTOFF, P_SD_CUTOFF, P_LT_CUTOFF, P_HT_CUTOFF};
    for (int id : ids) add(id, filt);
    for (int p = PART_CH; p <= PART_SP2; ++p) add(kSoundStart[p] + PCM_CUTOFF, filt);
  }
  if (space > 0.0f)
    for (int p = 0; p < NUM_PARTS; ++p) { add(s.mix[p].dly, space * 40.0f); add(s.mix[p].rev, space * 60.0f); }
}

static void Render48(Engine::Impl& s, float* out_l, float* out_r) {
  const int n = kBlock;
  ++s.blocks;
  if (s.touched >= 0 && s.blocks == s.touch_until) s.changed[P_LCD_TITLE] = 1;
  ApplyMacros(s);
  memset(s.main_l, 0, sizeof s.main_l); memset(s.main_r, 0, sizeof s.main_r);
  memset(s.byp_l, 0, sizeof s.byp_l);   memset(s.byp_r, 0, sizeof s.byp_r);
  memset(s.dly_in_l, 0, sizeof s.dly_in_l); memset(s.dly_in_r, 0, sizeof s.dly_in_r);
  memset(s.rev_in_l, 0, sizeof s.rev_in_l); memset(s.rev_in_r, 0, sizeof s.rev_in_r);
  memset(s.key, 0, sizeof s.key);

  // user samples restored by name
  for (int p = PART_CH; p <= PART_SP2; ++p)
    if (s.pend[p] && SampleRom::Get().Ready(BANK_USER)) {
      int idx = SampleRom::Get().IndexOf(BANK_USER, s.pend_name[p]);
      if (idx >= 0) {
        s.v[kSoundStart[p] + PCM_SAMPLE] = (float)(idx + 1);
        s.changed[kSoundStart[p] + PCM_SAMPLE] = 1;
      }
      s.pend[p] = false;
    }

  const float inv_n = 1.0f / n;
  for (int p = 0; p < NUM_PARTS; ++p) {
    const MixIds& m = s.mix[p];
    Engine::Impl::Ramp& r = s.ramp[p];
    float lev = s.v[m.level] * 0.01f;
    float g = lev * lev;
    float th = (s.v[m.pan] + 100.0f) * (0.005f * kPi * 0.5f);
    float gl_t = fminf(1.0f, cosf(th) * 1.41421356f) * g;
    float gr_t = fminf(1.0f, sinf(th) * 1.41421356f) * g;
    float dly_t = s.e[m.dly] * 0.01f, rev_t = s.e[m.rev] * 0.01f, sc_t = s.e[m.sidech] * 0.01f;
    if (!s.parts[p]->Active()) {   // silent: jump the ramps (nothing to click)
      r.gl = gl_t; r.gr = gr_t; r.dly = dly_t; r.rev = rev_t; r.sc = sc_t;
      continue;
    }
    int ch = s.parts[p]->Render(s.tl, s.tr, n);
    float spread = m.spread >= 0 ? s.v[m.spread] : 0.0f;
    if (ch == 1) {
      if (fabsf(spread) > 0.5f) {
        // mono source: a fixed 9 ms Haas copy faded in on one side by |spread|
        float a = fabsf(spread) * 0.01f;
        float* buf = s.spread_buf[p];
        int pos = s.spread_pos[p];
        const int d = 432;
        for (int i = 0; i < n; ++i) {
          buf[pos] = s.tl[i];
          float del = buf[(pos - d) & 1023];
          pos = (pos + 1) & 1023;
          float x = s.tl[i];
          if (spread > 0) { s.tr[i] = x + a * (del - x); s.tl[i] = x; }
          else { s.tr[i] = x; s.tl[i] = x + a * (del - x); }
        }
        s.spread_pos[p] = pos;
      } else {
        memcpy(s.tr, s.tl, sizeof(float) * n);
        float* buf = s.spread_buf[p];   // keep the history current so a later spread starts clean
        int pos = s.spread_pos[p];
        for (int i = 0; i < n; ++i) { buf[pos] = s.tl[i]; pos = (pos + 1) & 1023; }
        s.spread_pos[p] = pos;
      }
    } else if (fabsf(spread) > 0.5f) {
      float w = 1.0f + spread * 0.01f;   // stereo source: side gain 0..2
      for (int i = 0; i < n; ++i) {
        float mid = 0.5f * (s.tl[i] + s.tr[i]), side = 0.5f * (s.tl[i] - s.tr[i]) * w;
        s.tl[i] = mid + side;
        s.tr[i] = mid - side;
      }
    }
    bool to_main = s.v[m.master] < 0.5f;
    float* dl = to_main ? s.main_l : s.byp_l;
    float* dr = to_main ? s.main_r : s.byp_r;
    float d_gl = (gl_t - r.gl) * inv_n, d_gr = (gr_t - r.gr) * inv_n;
    float d_dly = (dly_t - r.dly) * inv_n, d_rev = (rev_t - r.rev) * inv_n, d_sc = (sc_t - r.sc) * inv_n;
    for (int i = 0; i < n; ++i) {
      r.gl += d_gl; r.gr += d_gr; r.dly += d_dly; r.rev += d_rev; r.sc += d_sc;
      float l = s.tl[i] * r.gl, rr = s.tr[i] * r.gr;
      dl[i] += l; dr[i] += rr;
      s.dly_in_l[i] += l * r.dly; s.dly_in_r[i] += rr * r.dly;
      s.rev_in_l[i] += l * r.rev; s.rev_in_r[i] += rr * r.rev;
      s.key[i] += 0.5f * (l + rr) * r.sc;
    }
    r.gl = gl_t; r.gr = gr_t; r.dly = dly_t; r.rev = rev_t; r.sc = sc_t;
  }

  // effects; a type change clears the slot so the old type's tail doesn't ring through the new one
  int dt = (int)s.v[P_DLY_TYPE], rt = (int)s.v[P_REV_TYPE], mt = (int)s.v[P_MFX_TYPE];
  if (dt != s.last_dly_type) { s.dly.Clear(); s.last_dly_type = dt; }
  if (rt != s.last_rev_type) { s.rev.Clear(); s.last_rev_type = rt; }
  if (mt != s.last_mfx_type) { s.mfx.Clear(); s.last_mfx_type = mt; }
  s.dly.Process(s.dly_in_l, s.dly_in_r, s.dly_out_l, s.dly_out_r, n, s.e, s.bpm);
  float to_rev = s.v[P_DLY_REVERB] * 0.01f;
  if (to_rev > 0.0f)
    for (int i = 0; i < n; ++i) { s.rev_in_l[i] += s.dly_out_l[i] * to_rev; s.rev_in_r[i] += s.dly_out_r[i] * to_rev; }
  s.rev.Process(s.rev_in_l, s.rev_in_r, s.rev_out_l, s.rev_out_r, n, s.e);
  bool dly_pre = s.v[P_DLY_RETURN] < 0.5f, rev_pre = s.v[P_REV_RETURN] < 0.5f;
  if (dly_pre) for (int i = 0; i < n; ++i) { s.main_l[i] += s.dly_out_l[i]; s.main_r[i] += s.dly_out_r[i]; }
  if (rev_pre) for (int i = 0; i < n; ++i) { s.main_l[i] += s.rev_out_l[i]; s.main_r[i] += s.rev_out_r[i]; }
  s.mfx.Process(s.main_l, s.main_r, s.key, n, s.e);

  float vol = s.v[P_VOLUME] * (1.0f / 80.0f);
  vol *= vol;
  float dv = (vol - s.vol_cur) * inv_n;
  for (int i = 0; i < n; ++i) {
    s.vol_cur += dv;
    float l = s.main_l[i] + s.byp_l[i] + (dly_pre ? 0.0f : s.dly_out_l[i]) + (rev_pre ? 0.0f : s.rev_out_l[i]);
    float r = s.main_r[i] + s.byp_r[i] + (dly_pre ? 0.0f : s.dly_out_r[i]) + (rev_pre ? 0.0f : s.rev_out_r[i]);
    out_l[i] = l * s.vol_cur;
    out_r[i] = r * s.vol_cur;
  }
  s.vol_cur = vol;
}

void Engine::RenderFloat(float* out_l, float* out_r, int frames) {
  Impl& s = *d_;
  ScopedDenormalDisable no_denormals;
  float bl[kBlock], br[kBlock];
  int need = s.rs.InputNeeded(frames);
  while (need > 0) {
    Render48(s, bl, br);
    s.rs.Push(bl, br, kBlock);
    need -= kBlock;
  }
  s.rs.Pull(out_l, out_r, frames);
  s.lim.Process(out_l, out_r, frames, s.v[P_LIM_DRIVE], s.v[P_LIM_CEILING], 80.0f, 44100.0f);
}

void Engine::Render(int16_t* out, int frames) {
  Impl& s = *d_;
  if (frames > 128) frames = 128;
  if (frames <= 0) return;
  RenderFloat(s.out_l, s.out_r, frames);
  for (int i = 0; i < frames; ++i) {
    float l = Clamp(s.out_l[i], -1.0f, 1.0f) * 32767.0f;
    float r = Clamp(s.out_r[i], -1.0f, 1.0f) * 32767.0f;
    out[2 * i] = (int16_t)lrintf(l);
    out[2 * i + 1] = (int16_t)lrintf(r);
  }
}

}  // namespace dlx
