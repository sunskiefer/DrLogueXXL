// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the screen pages (see ui_pages.h). Each part: its sound pages first (the parameters that are not on
// the kit knobs come first, as on the machine's screen), then two mixer pages (level, pan, sends; master, sidechain,
// choke, priority).
#include "ui_pages.h"

#include <stdio.h>
#include <string.h>

namespace dlx {

namespace {

const char* const kLong[NUM_SEL] = {"BASS DRUM", "SNARE DRUM", "LOW TOM", "HIGH TOM", "CLOSED HAT", "OPEN HAT",
                                    "RIM SHOT", "CLAP", "SAMPLE 1", "SAMPLE 2", "VPM", "NOISE", "USER", "DELAY",
                                    "REVERB", "MASTER", "GLOBAL"};
const char* const kShort[NUM_SEL] = {"BD", "SD", "LT", "HT", "CH", "OH", "RS", "CP", "SP1", "SP2", "MULTI", "MULTI",
                                     "MULTI", "FX", "FX", "FX", ""};

struct Builder {
  int (*pages)[4];
  int n = 0, slot = 0;
  void Add(int id) {
    if (n >= kMaxPages) return;
    pages[n][slot] = id;
    if (++slot == 4) { slot = 0; ++n; }
  }
  void Page(int a, int b, int c, int d) { Add(a); Add(b); Add(c); Add(d); }
  int Done() {
    if (slot) { while (slot) Add(-1); }
    return n;
  }
};

// the two mixer pages of a part, from its LEVEL id (the mix keys follow it in params.json order)
void MixPages(Builder& b, int level, bool has_spread) {
  int i = level;
  int pan = i + 1, spread = has_spread ? i + 2 : -1, dly = i + (has_spread ? 3 : 2);
  int rev = dly + 1, master = dly + 2, sidech = dly + 3, choke = dly + 4, prio = dly + 5;
  b.Page(level, pan, dly, rev);
  b.Page(master, sidech, choke, prio);
  if (has_spread) b.Page(spread, -1, -1, -1);
}

void PcmPages(Builder& b, int bank, int level) {
  // BANK, SAMPLE, START, END | REDUX, DRIVE, CUTOFF, RESON | TUNE, ATTACK, DECAY (also on the kit knobs)
  b.Page(bank, bank + 1, bank + 5, bank + 6);
  b.Page(bank + 9, bank + 10, bank + 7, bank + 8);
  b.Page(bank + 2, bank + 3, bank + 4, -1);
  MixPages(b, level, true);
}

}  // namespace

int UiPart(int sel) {
  if (sel <= SEL_SP2) return sel;          // BD..SP2 are parts 0..9 in the same order
  if (sel <= SEL_USER) return PART_MT;
  return -1;
}

int UiPages(int sel, const float* v, int pages[kMaxPages][4], int unit_params) {
  for (int k = 0; k < kMaxPages; ++k) for (int j = 0; j < 4; ++j) pages[k][j] = -1;
  Builder b;
  b.pages = pages;
  switch (sel) {
    case SEL_BD:
      b.Page(P_BD_SWEEP, P_BD_SWTIME, P_BD_HOLD, P_BD_ATTACK);
      b.Page(P_BD_TRANSIENT, P_BD_CUTOFF, P_BD_RESON, -1);
      b.Page(P_BD_DECAY, P_BD_TUNE, P_BD_DRIVE, -1);
      MixPages(b, P_BD_LEVEL, false);
      break;
    case SEL_SD:
      b.Page(P_SD_SNAPPY, P_SD_TONE, P_SD_SNAPLV, P_SD_SNAPDEC);
      b.Page(P_SD_CUTOFF, P_SD_RESON, P_SD_DECAY, P_SD_TUNE);
      MixPages(b, P_SD_LEVEL, true);
      break;
    case SEL_LT:
    case SEL_HT: {
      int d = sel == SEL_LT ? P_LT_DECAY : P_HT_DECAY;
      b.Page(d + 3, d + 4, d + 5, d + 8);     // LAYER, SWEEP, ATTACK, DRIVE
      b.Page(d + 6, d + 7, d, d + 1);         // CUTOFF, RESON, DECAY, TUNE
      b.Page(d + 2, -1, -1, -1);              // DETUNE
      MixPages(b, sel == SEL_LT ? P_LT_LEVEL : P_HT_LEVEL, false);
      break;
    }
    case SEL_CH: PcmPages(b, P_CH_BANK, P_CH_LEVEL); break;
    case SEL_OH: PcmPages(b, P_OH_BANK, P_OH_LEVEL); break;
    case SEL_RS: PcmPages(b, P_RS_BANK, P_RS_LEVEL); break;
    case SEL_CP: PcmPages(b, P_CP_BANK, P_CP_LEVEL); break;
    case SEL_SP1: PcmPages(b, P_SP1_BANK, P_SP1_LEVEL); break;
    case SEL_SP2: PcmPages(b, P_SP2_BANK, P_SP2_LEVEL); break;
    case SEL_VPM:
      b.Page(P_VPM_PRESET, P_VPM_NOTE, P_VPM_INDEX, P_VPM_RATIO);
      b.Page(P_VPM_NOISE, P_VPM_EG, P_VPM_ATTACK, P_VPM_RELEASE);
      b.Page(P_VPM_IDXMOD, -1, -1, -1);
      MixPages(b, P_MT_LEVEL, true);
      break;
    case SEL_NOISE:
      b.Page(P_NOISE_ALGO, P_NOISE_COLOR, P_NOISE_PEAK, -1);
      b.Page(P_NOISE_ATTACK, P_NOISE_RELEASE, -1, -1);
      MixPages(b, P_MT_LEVEL, true);
      break;
    case SEL_USER: {
      b.Page(P_MT_ENGINE, P_MT_UNOTE, unit_params > 0 ? P_U_1 : -1, unit_params > 1 ? P_U_1 + 1 : -1);
      for (int i = 2; i < unit_params && i < kNumUnitSlots; ++i) b.Add(P_U_1 + i);
      b.Done();
      MixPages(b, P_MT_LEVEL, true);
      break;
    }
    case SEL_DELAY: {
      int t = (int)v[P_DLY_TYPE];
      bool bpm = t == 1 || t == 3 || t == 5 || t == 6;   // Stereo BPM, Mono BPM, Tape BPM, Tribe (on the grid)
      b.Page(P_DLY_TYPE, bpm ? P_DLY_DIV : P_DLY_TIME, P_DLY_FB, P_DLY_LEVEL);
      b.Page(P_DLY_FILTER, P_DLY_CUTOFF, P_DLY_SAT, P_DLY_SPREAD);
      b.Page(P_DLY_ROUTE, P_DLY_MOD, P_DLY_TRACKING, P_DLY_TONE);
      b.Page(P_DLY_RETURN, P_DLY_REVERB, -1, -1);
      break;
    }
    case SEL_REVERB:
      b.Page(P_REV_TYPE, P_REV_TIME, P_REV_INPUT, P_REV_BRIGHT);
      b.Page(P_REV_RISE, P_REV_DIVE, P_REV_LEVEL, P_REV_RETURN);
      break;
    case SEL_MASTER:
      switch ((int)v[P_MFX_TYPE]) {
        case 1:
          b.Page(P_MFX_TYPE, P_COMP_THRESH, P_COMP_RATIO, P_COMP_SIDECH);
          b.Page(P_COMP_ATTACK, P_COMP_RELEASE, P_COMP_MAKEUP, P_COMP_KNEE);
          b.Page(P_COMP_WET, -1, -1, -1);
          break;
        case 2:
          b.Page(P_MFX_TYPE, P_MF_MODE, P_MF_CUTOFF, P_MF_RESON);
          b.Page(P_MF_SATURATE, -1, -1, -1);
          break;
        case 3:
          b.Page(P_MFX_TYPE, P_BOOST_CENTER, P_BOOST_WIDTH, P_BOOST_GAIN);
          b.Page(P_BOOST_SATURATE, -1, -1, -1);
          break;
        case 4:
          b.Page(P_MFX_TYPE, P_EQ_LOW, P_EQ_MID, P_EQ_HIGH);
          b.Page(P_EQ_GAIN, P_EQ_LOWMID, P_EQ_MIDHIGH, P_EQ_SLOPE);
          break;
        case 5:
          b.Page(P_MFX_TYPE, P_MFX_U1, P_MFX_U2, P_MFX_U3);
          b.Page(P_MFX_U4, P_MFX_U5, P_MFX_U6, P_COMP_SIDECH);
          break;
        default:
          b.Page(P_MFX_TYPE, -1, -1, -1);
      }
      break;
    default:   // GLOBAL
      b.Page(P_VOLUME, P_VEL_SENS, P_NOTE_MAP, P_BASE_NOTE);
      b.Page(P_MULTI_KEYS, P_KEY_SPLIT, P_LIM_DRIVE, P_LIM_CEILING);
      b.Page(P_RESCAN, P_PANIC, -1, -1);
  }
  return b.Done();
}

void UiTitle(int sel, const float* v, const char* unit_name, char* buf, int len) {
  if (sel < 0 || sel >= NUM_SEL) sel = 0;
  if (sel == SEL_USER) { snprintf(buf, len, "USER  %s", unit_name && *unit_name ? unit_name : "---"); return; }
  if (sel == SEL_MASTER) {
    static const char* const t[] = {"THRU", "COMPRESSOR", "FILTER", "BOOST", "EQ THREE", "OMNI"};
    int k = (int)v[P_MFX_TYPE];
    snprintf(buf, len, "MASTER  %s", t[k < 0 || k > 5 ? 0 : k]);
    return;
  }
  if (sel <= SEL_SP2) snprintf(buf, len, "%s  %s", kShort[sel], kLong[sel]);
  else snprintf(buf, len, "%s", kLong[sel]);
}

}  // namespace dlx
