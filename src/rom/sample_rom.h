// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sample memory the six sample parts (CH, OH, RS, CP, SP1, SP2) play from.
//
// The drumlogue ships factory PCM; we can't, so the factory banks (CH, OH, RS, CP, MISC, EXP) are SYNTHESISED once,
// when the first instance opens, on a worker thread (shared by every instance in the process, read-only afterwards).
// USER holds the user's own WAV files from "<plugin folder>/Samples" (scanned on the same worker and on Rescan).
// Sample parts render silence for a bank until Ready(bank) is true.
//
// All sample data is 48 kHz (files at other rates are converted on load), int16, mono or interleaved stereo.
//
// Threading and memory (see sample_rom.cc for the details):
//  - One worker thread per process (nice 10). It builds CH, OH, RS, CP, MISC, then scans USER, then builds EXP, and
//    publishes each bank with an atomic pointer. It then sleeps until Rescan()/Start(new dir); it is stopped and
//    joined by a static guard when the plugin library is unloaded (dlclose) or the process exits.
//  - Ready/Count/Find/IndexOf/Acquire/Release are lock-free reads (safe on the audio thread). Rescan() only posts a
//    semaphore (also audio-thread safe).
//  - Factory banks live for the whole process. A user bank replaced by a rescan is "retired": it is freed by the
//    worker only once no voice holds a pin on it (Acquire/Release) AND it has been retired for >= 2 s (which
//    closes the window between a part reading the bank pointer and pinning it). Parts must pin what they play.
//    A pointer from Find() (unpinned) stays valid for at least 2 s after a rescan: fine for reading a name for the
//    display, not for playing.
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
  void* owner;             // internal: the bank this sample belongs to (for Acquire/Release)
};

class SampleRom {
 public:
  static SampleRom& Get();                       // the process-wide instance
  // Start building the factory banks and scanning user_dir (may be NULL) if not done yet. Never blocks.
  // Later calls only matter when user_dir differs from the current one (then the USER bank is rescanned).
  void Start(const char* user_dir);
  void Rescan();                                 // rescan the user folder (worker thread); real-time safe
  bool Ready(int bank) const;                    // lock-free
  int Count(int bank) const;                     // 0 until ready
  const Sample* Find(int bank, int index) const; // index 0-based; NULL if not there (or not ready)
  // A user sample's position by name (state restore keeps samples by name); -1 if not found. Exact match first,
  // then case-insensitive. Works for every bank.
  int IndexOf(int bank, const char* name) const;
  static int Capacity(int bank);                 // CH 16, OH 16, RS 16, CP 16, MISC 64, USER 128, EXP 64

  // ---- additions ----
  // Find() + pin the owning bank so a rescan cannot free it while a voice plays it. Every non-NULL result must be
  // given back with Release(). Lock-free (two atomic ops).
  const Sample* Acquire(int bank, int index);
  void Release(const Sample* s);
  // Number of completed user scans (changes after every Rescan; the engine can use it to re-resolve names).
  uint32_t UserGeneration() const;
  bool Busy() const;                             // the worker is building or scanning
  // Block until the worker is idle (tests, offline render). Not for the audio thread.
  void WaitIdle();
  // Factory names without building anything (for listings); "" if out of range.
  static const char* FactoryName(int bank, int index);
  // Statistics of what is published: total bytes of sample data and the time the factory build took.
  uint64_t MemoryBytes() const;
  double FactoryBuildSeconds() const;
  // Bounds of the USER bank (the drumlogue's): max frames per sample (4 s at 48 kHz) and total bytes.
  static const uint32_t kUserMaxFrames = 4 * 48000;
  static const uint64_t kUserMaxBytes = 32ull * 1024 * 1024;

  struct Impl;
 private:
  SampleRom();
  Impl* d_;
};

}  // namespace dlx
