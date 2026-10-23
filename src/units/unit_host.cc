// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- drumlogue unit hosting: the runtime descriptor the units are initialised with, and the value text
// of a unit parameter formatted from its type/frac the way the drumlogue screen shows it.
#include "units/unit_host.h"

#include <stdio.h>
#include <string.h>

namespace dlx {

namespace {
// The sample-bank callbacks: DrLogueXXL exposes no sample banks to units (the drumlogue's are its own sample memory).
uint8_t NoBanks() { return 0; }
uint8_t NoSamples(uint8_t) { return 0; }
const sample_wrapper_t* NoSample(uint8_t, uint8_t) { return nullptr; }
const char* const kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}  // namespace

void FillRuntimeDesc(unit_runtime_desc_t* d) {
  memset(d, 0, sizeof(*d));
  d->target = (uint16_t)k_unit_target_drumlogue_synth;
  d->api = k_unit_api_2_0_0;
  d->samplerate = 48000;
  d->frames_per_buffer = 64;
  d->input_channels = 0;
  d->output_channels = 2;
  d->get_num_sample_banks = NoBanks;
  d->get_num_samples_for_bank = NoSamples;
  d->get_sample = NoSample;
}

void FormatUnitParam(SynthUnit* u, uint8_t id, int32_t value, char* buf, int len) {
  if (!buf || len <= 0) return;
  buf[0] = 0;
  if (!u) return;
  const unit_header_t& h = u->Header();
  if (id >= h.num_params) return;
  const unit_param_t& p = h.params[id];
  // scaled value and its number of decimals
  double x = value;
  int dec = 0;
  if (p.frac) {
    if (p.frac_mode == k_unit_param_frac_mode_decimal) {
      for (int i = 0; i < p.frac; ++i) x /= 10.0;
      dec = p.frac;
    } else {
      x = value / (double)(1 << p.frac);
      dec = p.frac <= 3 ? 1 : 2;
    }
  }
  char num[24];
  snprintf(num, sizeof num, "%.*f", dec, x);
  char snum[24];   // signed: "+" for positive offsets
  snprintf(snum, sizeof snum, "%s%.*f", value > 0 ? "+" : "", dec, x);
  switch (p.type) {
    case k_unit_param_type_percent: snprintf(buf, len, "%s%%", num); break;
    case k_unit_param_type_db: snprintf(buf, len, "%sdB", num); break;
    case k_unit_param_type_cents: snprintf(buf, len, "%sC", snum); break;
    case k_unit_param_type_semi:
    case k_unit_param_type_oct: snprintf(buf, len, "%s", snum); break;
    case k_unit_param_type_hertz:
      if (x >= 1000.0) snprintf(buf, len, "%.2fkHz", x / 1000.0);
      else snprintf(buf, len, "%sHz", num);
      break;
    case k_unit_param_type_khertz: snprintf(buf, len, "%skHz", num); break;
    case k_unit_param_type_bpm: snprintf(buf, len, "%s", num); break;
    case k_unit_param_type_msec:
      if (x >= 1000.0) snprintf(buf, len, "%.2fs", x / 1000.0);
      else snprintf(buf, len, "%sms", num);
      break;
    case k_unit_param_type_sec: snprintf(buf, len, "%ss", num); break;
    case k_unit_param_type_enum:
      snprintf(buf, len, "%d", (int)(p.min == 0 ? value + 1 : value));
      break;
    case k_unit_param_type_strings: {
      const char* s = u->ParamString(id, value);
      if (s && s[0]) snprintf(buf, len, "%s", s);
      else snprintf(buf, len, "%s", num);
      break;
    }
    case k_unit_param_type_drywet:
      if (value == 0) snprintf(buf, len, "BAL");
      else snprintf(buf, len, "%c%.*f", value < 0 ? 'D' : 'W', dec, x < 0 ? -x : x);
      break;
    case k_unit_param_type_pan:
      if (value == 0) snprintf(buf, len, "C");
      else snprintf(buf, len, "%c%.*f%%", value < 0 ? 'L' : 'R', dec, x < 0 ? -x : x);
      break;
    case k_unit_param_type_spread:
      if (value == 0) snprintf(buf, len, "0");
      else snprintf(buf, len, "%c%.*f", value < 0 ? '<' : '>', dec, x < 0 ? -x : x);
      break;
    case k_unit_param_type_onoff: snprintf(buf, len, "%s", value ? "On" : "Off"); break;
    case k_unit_param_type_midi_note: {
      int n = value < 0 ? 0 : (value > 127 ? 127 : (int)value);
      snprintf(buf, len, "%s%d", kNotes[n % 12], n / 12 - 1);
      break;
    }
    default: snprintf(buf, len, "%s", num); break;   // none, bitmaps
  }
}

}  // namespace dlx
