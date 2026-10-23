// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- WAV loader (see wav.h).
#include "wav.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace dlx {

namespace {

// a tiny random-access reader over memory or a FILE*, so a large file is never read whole
struct Reader {
  const uint8_t* mem = 0;
  size_t mem_size = 0;
  FILE* f = 0;
  size_t file_size = 0;
  size_t Size() const { return f ? file_size : mem_size; }
  // read up to n bytes at offset; returns bytes read
  size_t Read(size_t off, void* dst, size_t n) {
    size_t sz = Size();
    if (off >= sz) return 0;
    if (n > sz - off) n = sz - off;
    if (f) {
      if (fseek(f, (long)off, SEEK_SET) != 0) return 0;
      return fread(dst, 1, n, f);
    }
    memcpy(dst, mem + off, n);
    return n;
  }
};

inline uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

enum { FMT_PCM = 1, FMT_FLOAT = 3, FMT_EXT = 0xFFFE };

inline float Sinc(double x) {
  if (fabs(x) < 1e-9) return 1.0f;
  double px = 3.14159265358979323846 * x;
  return (float)(sin(px) / px);
}

bool Parse(Reader& rd, uint32_t max_frames, WavData* out) {
  uint8_t h[12];
  if (rd.Read(0, h, 12) != 12) return false;
  if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
  size_t size = rd.Size();
  size_t pos = 12;
  bool have_fmt = false;
  int fmt = 0, chans = 0, bits = 0, block = 0;
  uint32_t rate = 0;
  size_t data_off = 0, data_len = 0;
  bool have_data = false;
  while (pos + 8 <= size) {
    uint8_t ch[8];
    if (rd.Read(pos, ch, 8) != 8) break;
    uint32_t len = U32(ch + 4);
    size_t body = pos + 8;
    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fb[40];
      memset(fb, 0, sizeof(fb));
      size_t want = len < 40 ? len : 40;
      if (len < 16 || rd.Read(body, fb, want) != want) return false;
      fmt = U16(fb);
      chans = U16(fb + 2);
      rate = U32(fb + 4);
      block = U16(fb + 12);
      bits = U16(fb + 14);
      if (fmt == FMT_EXT) {
        if (len < 40) return false;
        // SubFormat GUID: first two bytes are the format code (KSDATAFORMAT_SUBTYPE_PCM = 1, IEEE_FLOAT = 3)
        fmt = U16(fb + 24);
        int valid = U16(fb + 18);
        (void)valid;   // container size (bits) decides the decoding; valid bits only zero-pad the low end
      }
      have_fmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      data_off = body;
      data_len = len;
      if (data_off + data_len > size || len == 0xFFFFFFFFu) {   // truncated (or a streaming writer's placeholder)
        data_len = size > data_off ? size - data_off : 0;
        out->truncated = true;
      }
      have_data = true;
      if (have_fmt) break;
    }
    if ((size_t)len > size) break;      // garbage length: stop scanning
    pos = body + len + (len & 1);       // chunks are padded to even sizes
  }
  if (!have_fmt || !have_data) return false;
  if (chans < 1 || rate < 1000 || rate > 768000) return false;
  if (fmt != FMT_PCM && fmt != FMT_FLOAT) return false;
  if (fmt == FMT_PCM && bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
  if (fmt == FMT_FLOAT && bits != 32 && bits != 64) return false;
  int bps = bits / 8;
  if (block < bps * chans) block = bps * chans;
  size_t in_frames = data_len / (size_t)block;
  if (in_frames == 0) return false;
  // how many source frames are needed for max_frames at 48 kHz (+ resampler margin)
  double ratio = 48000.0 / (double)rate;
  size_t need = (size_t)((double)max_frames / ratio) + 64;
  if (in_frames > need) in_frames = need;
  int oc = chans >= 2 ? 2 : 1;
  std::vector<float> fl(in_frames * oc);
  // decode in pieces
  std::vector<uint8_t> raw;
  const size_t kPiece = 4096;
  size_t done = 0;
  while (done < in_frames) {
    size_t n = in_frames - done;
    if (n > kPiece) n = kPiece;
    raw.resize(n * block);
    size_t got = rd.Read(data_off + done * block, &raw[0], n * block);
    size_t gf = got / block;
    if (gf == 0) break;
    for (size_t i = 0; i < gf; i++) {
      const uint8_t* fr = &raw[i * block];
      for (int c = 0; c < oc; c++) {
        const uint8_t* s = fr + c * bps;
        float v;
        if (fmt == FMT_FLOAT) {
          if (bits == 32) { uint32_t u = U32(s); float x; memcpy(&x, &u, 4); v = x; }
          else { uint64_t u = (uint64_t)U32(s) | ((uint64_t)U32(s + 4) << 32); double x; memcpy(&x, &u, 8); v = (float)x; }
          if (!(v == v) || v > 1e6f || v < -1e6f) v = 0.0f;   // NaN / absurd values
        } else if (bits == 8) v = ((float)s[0] - 128.0f) * (1.0f / 128.0f);
        else if (bits == 16) v = (float)(int16_t)U16(s) * (1.0f / 32768.0f);
        else if (bits == 24) { int32_t x = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24); v = (float)x * (1.0f / 2147483648.0f); }
        else v = (float)(int32_t)U32(s) * (1.0f / 2147483648.0f);
        fl[(done + i) * oc + c] = v;
      }
    }
    done += gf;
    if (gf < n) { out->truncated = true; break; }
  }
  if (done == 0) return false;
  std::vector<float> rs;
  uint32_t of = 0;
  const float* src = &fl[0];
  if (rate != 48000) {
    ResampleSinc(&fl[0], (uint32_t)done, oc, (double)rate, 48000.0, max_frames, &rs, &of);
    src = rs.empty() ? 0 : &rs[0];
  } else {
    of = (uint32_t)(done < max_frames ? done : max_frames);
  }
  if (of == 0 || !src) return false;
  // float -> int16 (clip; float files may exceed 0 dBFS)
  out->pcm.resize((size_t)of * oc);
  for (size_t i = 0; i < (size_t)of * oc; i++) {
    float v = src[i] * 32767.0f;
    v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
    out->pcm[i] = (int16_t)lrintf(v);
  }
  out->frames = of;
  out->channels = oc;
  out->src_rate = rate;
  out->src_bits = bits;
  return true;
}

void PutU32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
void PutU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

}  // namespace

// Windowed-sinc kernel table: k(u) = sinc(u) * blackman(u / kZero) for u in [0, kZero], kRes points per zero
// crossing, linearly interpolated. Built once (worker thread; a function-local static is thread-safe in C++11).
namespace {
const int kZero = 16;
const int kRes = 256;
struct SincTable {
  float t[kZero * kRes + 2];
  SincTable() {
    for (int i = 0; i <= kZero * kRes + 1; i++) {
      double u = (double)i / kRes;
      double w = u >= kZero ? 0.0 : 0.42 + 0.5 * cos(3.14159265358979 * u / kZero) + 0.08 * cos(2.0 * 3.14159265358979 * u / kZero);
      t[i] = (float)(Sinc(u) * w);
    }
  }
  inline float At(float u) const {   // u >= 0
    float x = u * kRes;
    int i = (int)x;
    if (i >= kZero * kRes) return 0.0f;
    float f = x - (float)i;
    return t[i] + f * (t[i + 1] - t[i]);
  }
};
const SincTable& Table() { static SincTable tab; return tab; }
}  // namespace

void ResampleSinc(const float* in, uint32_t in_frames, int ch, double src_rate, double dst_rate,
                  uint32_t max_out, std::vector<float>* out, uint32_t* out_frames) {
  const SincTable& tab = Table();
  double step = src_rate / dst_rate;            // input samples per output sample
  double cut = step > 1.0 ? 1.0 / step : 1.0;   // normalised cutoff (anti-alias when downsampling)
  cut *= 0.94;                                  // a little transition band below Nyquist
  double half = kZero / cut;                    // half-width in input samples
  uint32_t n = (uint32_t)floor((double)in_frames / step);
  if (n > max_out) n = max_out;
  out->assign((size_t)n * ch, 0.0f);
  float fcut = (float)cut;
  for (uint32_t i = 0; i < n; i++) {
    double t = i * step;
    int lo = (int)ceil(t - half), hi = (int)floor(t + half);
    if (lo < 0) lo = 0;
    if (hi > (int)in_frames - 1) hi = (int)in_frames - 1;
    float acc[2] = {0.0f, 0.0f};
    float wsum = 0.0f;
    for (int j = lo; j <= hi; j++) {
      float x = (float)((double)j - t);
      float k = tab.At(fabsf(x) * fcut);
      wsum += k;
      for (int c = 0; c < ch; c++) acc[c] += k * in[(size_t)j * ch + c];
    }
    // normalise by the kernel sum (exact DC gain, also near the edges)
    float g = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
    for (int c = 0; c < ch; c++) (*out)[(size_t)i * ch + c] = acc[c] * g;
  }
  *out_frames = n;
}

bool ParseWav(const uint8_t* data, size_t size, uint32_t max_frames, WavData* out) {
  Reader rd;
  rd.mem = data;
  rd.mem_size = size;
  *out = WavData();
  return Parse(rd, max_frames, out);
}

bool LoadWav(const char* path, uint32_t max_frames, size_t max_bytes, WavData* out) {
  *out = WavData();
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  Reader rd;
  rd.f = f;
  bool ok = false;
  if (fseek(f, 0, SEEK_END) == 0) {
    long sz = ftell(f);
    if (sz > 0) {
      rd.file_size = (size_t)sz;
      if (max_bytes && rd.file_size > max_bytes) rd.file_size = max_bytes;
      ok = Parse(rd, max_frames, out);
    }
  }
  fclose(f);
  return ok;
}

bool WriteWav16(const char* path, const int16_t* pcm, uint32_t frames, int ch, uint32_t rate) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  uint8_t h[44];
  uint32_t bytes = frames * ch * 2;
  memcpy(h, "RIFF", 4); PutU32(h + 4, 36 + bytes); memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4); PutU32(h + 16, 16); PutU16(h + 20, 1); PutU16(h + 22, (uint16_t)ch);
  PutU32(h + 24, rate); PutU32(h + 28, rate * ch * 2); PutU16(h + 32, (uint16_t)(ch * 2)); PutU16(h + 34, 16);
  memcpy(h + 36, "data", 4); PutU32(h + 40, bytes);
  bool ok = fwrite(h, 1, 44, f) == 44;
  // little-endian host assumed (x86, ARM)
  ok = ok && fwrite(pcm, 2, (size_t)frames * ch, f) == (size_t)frames * ch;
  fclose(f);
  return ok;
}

bool WriteWavFloat(const char* path, const float* l, const float* r, uint32_t frames, uint32_t rate) {
  int ch = r ? 2 : 1;
  std::vector<int16_t> pcm((size_t)frames * ch);
  for (uint32_t i = 0; i < frames; i++) {
    for (int c = 0; c < ch; c++) {
      float v = (c ? r[i] : l[i]) * 32767.0f;
      v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
      pcm[(size_t)i * ch + c] = (int16_t)lrintf(v);
    }
  }
  return WriteWav16(path, pcm.empty() ? 0 : &pcm[0], frames, ch, rate);
}

}  // namespace dlx
