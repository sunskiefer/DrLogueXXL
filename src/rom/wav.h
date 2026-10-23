// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- WAV file loading for the USER sample bank (worker thread only; allocates).
//
// Reads RIFF/WAVE (also RF64 is rejected cleanly): PCM 8/16/24/32-bit int, 32/64-bit float, WAVE_FORMAT_EXTENSIBLE,
// any channel count (the first two are kept), any sample rate (converted to 48 kHz with a windowed-sinc
// resampler). Unknown chunks are skipped, odd chunk sizes are padded, a truncated data chunk keeps what is there.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace dlx {

struct WavData {
  std::vector<int16_t> pcm;   // interleaved, 48 kHz
  uint32_t frames = 0;
  int channels = 0;           // 1 or 2
  bool truncated = false;     // data chunk was shorter than declared (still usable)
  uint32_t src_rate = 0;
  int src_bits = 0;
};

// Parse a WAV image in memory. max_frames caps the result (at 48 kHz). Returns false if it is not a usable WAV.
bool ParseWav(const uint8_t* data, size_t size, uint32_t max_frames, WavData* out);
// Load a file (reads at most `max_bytes` of it). Returns false on I/O error or not a usable WAV.
bool LoadWav(const char* path, uint32_t max_frames, size_t max_bytes, WavData* out);

// Write a 16-bit PCM WAV (tests and the offline renderer). Returns false on error.
bool WriteWav16(const char* path, const int16_t* interleaved, uint32_t frames, int channels, uint32_t rate);
bool WriteWavFloat(const char* path, const float* l, const float* r, uint32_t frames, uint32_t rate);  // r may be NULL

// Resample interleaved float audio from src_rate to dst_rate (windowed sinc, 16 taps each side). Used by the loader.
void ResampleSinc(const float* in, uint32_t in_frames, int channels, double src_rate, double dst_rate,
                  uint32_t max_out_frames, std::vector<float>* out, uint32_t* out_frames);

}  // namespace dlx
