// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sample memory the six sample parts (CH, OH, RS, CP, SP1, SP2) play from.
//
// The drumlogue ships factory PCM; we can't, so the factory banks (CH, OH, RS, CP, MISC, EXP) are SYNTHESISED once,
// when the first instance opens, on a worker thread (shared by every instance in the process, read-only afterwards).
// USER holds the user's own WAV files from "<plugin folder>/Samples" (scanned on the same worker and on Rescan).
// Sample parts render silence for a bank until Ready(bank) is true.
//
// All sample data is 48 kHz (files at other rates are converted on load), int16, mono or interleaved stereo.
#pragma once
#include <stdint.h>

namespace dlx {

enum Bank { BANK_CH = 0, BANK_OH, BANK_RS, BANK_CP, BANK_MISC, BANK_USER, BANK_EXP, NUM_BANKS };

struct Sample {
  const int16_t* data;     // frames * channels, interleaved
  uint32_t frames;
  uint8_t channels;        // 1 or 2
  float gain;              // playback gain that brings the sample to a common loudness
  char name[32];
};

class SampleRom {
 public:
  static SampleRom& Get();                       // the process-wide instance
  // Start building the factory banks and scanning user_dir (may be NULL) if not done yet. Never blocks.
  void Start(const char* user_dir);
  void Rescan();                                 // rescan the user folder (worker thread)
  bool Ready(int bank) const;                    // lock-free
  int Count(int bank) const;                     // 0 until ready
  const Sample* Find(int bank, int index) const; // index 0-based; NULL if not there (or not ready)
  // A user sample's position by name (state restore keeps samples by name); -1 if not found.
  int IndexOf(int bank, const char* name) const;
  // Synthesise one factory sample directly (tests, and the offline renderer of docs/VOICES.md).
  static int Capacity(int bank);                 // CH 16, OH 16, RS 16, CP 16, MISC 64, USER 128, EXP 64
};

}  // namespace dlx
