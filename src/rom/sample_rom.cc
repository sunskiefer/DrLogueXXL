// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the sample memory (see sample_rom.h).
//
// Publication: each bank is one heap block (BankData: the Sample table + one int16 buffer) published with an
// atomic pointer (release store; readers acquire-load). A published bank is never modified.
//
// USER bank lifetime: a rescan builds a complete new BankData, swaps the pointer, and puts the old one on a retired
// list owned by the worker. A retired bank is freed when (a) its pin count is 0 (every voice that plays a sample
// pins its bank with Acquire() and unpins with Release() when the voice ends or is stolen) and (b) it has been
// retired for at least 2 s. (b) closes the race in which a part loaded the old pointer just before the swap and
// pins it a few microseconds later. So nothing leaks and nothing is freed under a playing voice. Banks still
// retired at shutdown are freed by the destructor after the worker has been joined (if still pinned they are
// leaked on purpose: a host that unloads the library with voices still playing is already in undefined land).
#include "sample_rom.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "synth_banks.h"
#include "wav.h"

namespace dlx {

namespace {

const int kCap[NUM_BANKS] = {16, 16, 16, 16, 64, 128, 64};

double Now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

struct BankData {
  std::atomic<int> pins;
  int count;
  int16_t* mem;
  uint64_t bytes;
  double retired_at;
  Sample s[128];
  BankData() : pins(0), count(0), mem(0), bytes(0), retired_at(0.0) { memset(s, 0, sizeof(s)); }
  ~BankData() { free(mem); }
};

// one finished sound waiting to be packed into a bank
struct Pending {
  std::vector<int16_t> pcm;
  uint32_t frames;
  int channels;
  float gain;
  std::string name;
};

BankData* Pack(std::vector<Pending>& v) {
  BankData* b = new BankData();
  size_t total = 0;
  for (size_t i = 0; i < v.size(); i++) total += v[i].pcm.size();
  b->mem = (int16_t*)malloc(total ? total * sizeof(int16_t) : sizeof(int16_t));
  if (!b->mem) { delete b; return 0; }
  size_t off = 0;
  int n = (int)v.size();
  if (n > 128) n = 128;
  for (int i = 0; i < n; i++) {
    Pending& p = v[i];
    if (!p.pcm.empty()) memcpy(b->mem + off, &p.pcm[0], p.pcm.size() * sizeof(int16_t));
    Sample& s = b->s[i];
    s.data = b->mem + off;
    s.frames = p.frames;
    s.channels = (uint8_t)p.channels;
    s.gain = p.gain;
    snprintf(s.name, sizeof(s.name), "%s", p.name.c_str());
    s.owner = b;
    off += p.pcm.size();
  }
  b->count = n;
  b->bytes = total * sizeof(int16_t);
  return b;
}

// Trim trailing near-silence, fade the very end, normalise the peak to full scale for int16 resolution and compute
// the playback gain that brings the sound to a common loudness (loudest 10 ms RMS window -> about -10 dBFS, never
// above a peak of 1).
void Finish(syn::Buf& b, const char* name, Pending* out) {
  const float kMax = 4.0f * 48000.0f;
  if (b.n > (int)kMax) b.n = (int)kMax;
  float pk = 0.0f;
  for (int i = 0; i < b.n; i++) {
    float a = fabsf(b.l[i]);
    if (b.stereo) { float c = fabsf(b.r[i]); if (c > a) a = c; }
    if (!(a == a)) a = 0.0f;
    if (a > pk) pk = a;
  }
  if (pk <= 1e-9f) pk = 1.0f;
  float thr = pk * 0.001f;    // -60 dB
  int last = 0;
  for (int i = b.n - 1; i >= 0; i--) {
    float a = fabsf(b.l[i]);
    if (b.stereo) { float c = fabsf(b.r[i]); if (c > a) a = c; }
    if (a > thr) { last = i; break; }
  }
  int n = last + 1 + 96;   // + 2 ms of room for the fade
  if (n > b.n) n = b.n;
  b.n = n;
  syn::FadeOut(b, 0.003f);
  int ch = b.Channels();
  float inv = 1.0f / pk;
  // loudest 10 ms window (RMS) of the normalised signal
  const int W = 480;
  double acc = 0.0, best = 0.0;
  for (int i = 0; i < n; i++) {
    float x = b.l[i] * inv;
    float y = b.stereo ? b.r[i] * inv : x;
    acc += 0.5 * (x * x + y * y);
    if (i >= W) {
      float xo = b.l[i - W] * inv;
      float yo = b.stereo ? b.r[i - W] * inv : xo;
      acc -= 0.5 * (xo * xo + yo * yo);
    }
    double r = acc / (double)(i + 1 < W ? i + 1 : W);
    if (i >= W / 4 && r > best) best = r;
    if (i < W / 4 && i == n - 1) best = r;
  }
  float rms = (float)sqrt(best > 1e-12 ? best : 1e-12);
  float g = 0.33f / rms;
  if (g > 1.0f) g = 1.0f;
  out->pcm.resize((size_t)n * ch);
  for (int i = 0; i < n; i++) {
    for (int c = 0; c < ch; c++) {
      float v = (c ? b.r[i] : b.l[i]) * inv * 32767.0f;
      if (!(v == v)) v = 0.0f;
      v = v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v);
      out->pcm[(size_t)i * ch + c] = (int16_t)lrintf(v);
    }
  }
  out->frames = (uint32_t)n;
  out->channels = ch;
  out->gain = g;
  out->name = name;
}

bool HasWavExt(const char* n) {
  const char* d = strrchr(n, '.');
  return d && (strcasecmp(d, ".wav") == 0 || strcasecmp(d, ".wave") == 0);
}

struct FileEntry { std::string folder, name, path; };

bool EntryLess(const FileEntry& a, const FileEntry& b) {
  int c = strcasecmp(a.folder.c_str(), b.folder.c_str());
  if (c) return c < 0;
  c = strcasecmp(a.name.c_str(), b.name.c_str());
  if (c) return c < 0;
  return a.path < b.path;
}

void ScanDir(const std::string& root, const std::string& rel, int depth, std::vector<FileEntry>* out,
             const std::atomic<bool>& quit) {
  if (out->size() >= 4096 || quit.load()) return;
  std::string dir = rel.empty() ? root : root + "/" + rel;
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  std::vector<std::string> subdirs;
  struct dirent* e;
  while ((e = readdir(d)) != 0) {
    const char* n = e->d_name;
    if (n[0] == '.') continue;   // ".", "..", hidden files and macOS "._name.wav" resource forks
    std::string path = dir + "/" + n;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) continue;
    if (S_ISLNK(st.st_mode)) continue;   // never follow symlinks
    if (S_ISDIR(st.st_mode)) {
      if (depth < 3) subdirs.push_back(n);
    } else if (S_ISREG(st.st_mode) && HasWavExt(n)) {
      FileEntry fe;
      fe.folder = rel;
      fe.name = n;
      fe.path = path;
      out->push_back(fe);
      if (out->size() >= 4096) break;
    }
  }
  closedir(d);
  for (size_t i = 0; i < subdirs.size(); i++)
    ScanDir(root, rel.empty() ? subdirs[i] : rel + "/" + subdirs[i], depth + 1, out, quit);
}

}  // namespace

struct SampleRom::Impl {
  std::atomic<BankData*> bank[NUM_BANKS];
  std::atomic<uint32_t> user_gen;
  std::atomic<uint32_t> rescan_req;   // incremented by Rescan()/Start(new dir)
  std::atomic<uint32_t> rescan_done; // the request number the last finished scan served
  std::atomic<bool> busy, quit, started, initial_done;
  std::atomic<uint32_t> bytes;        // < 4 GB by construction (12 MB factory + 32 MB user + retired)
  std::atomic<float> build_sec;
  std::mutex mu;                      // guards user_dir and thread start (never taken on the audio thread)
  std::string user_dir;
  bool have_dir = false;
  pthread_t thread;
  sem_t sem;
  std::vector<BankData*> retired;     // worker-owned

  Impl() : user_gen(0), rescan_req(0), rescan_done(0), busy(false), quit(false), started(false), initial_done(false),
           bytes(0), build_sec(0.0f) {
    for (int i = 0; i < NUM_BANKS; i++) bank[i].store(0);
    sem_init(&sem, 0, 0);
  }

  ~Impl() {
    if (started.load()) {
      quit.store(true);
      sem_post(&sem);
      pthread_join(thread, 0);
    }
    sem_destroy(&sem);
    for (size_t i = 0; i < retired.size(); i++)
      if (retired[i]->pins.load() == 0) delete retired[i];
    for (int i = 0; i < NUM_BANKS; i++) {
      BankData* b = bank[i].load();
      if (b && b->pins.load() == 0) delete b;
    }
  }

  void Publish(int bi, BankData* b) {
    BankData* old = bank[bi].exchange(b, std::memory_order_acq_rel);
    if (b) bytes.fetch_add((uint32_t)b->bytes);
    if (old) {
      old->retired_at = Now();
      retired.push_back(old);
    }
  }

  void FreeRetired(bool force_time) {
    double t = Now();
    for (size_t i = 0; i < retired.size();) {
      BankData* b = retired[i];
      if (b->pins.load(std::memory_order_acquire) == 0 && (force_time || t - b->retired_at >= 2.0)) {
        bytes.fetch_sub((uint32_t)b->bytes);
        delete b;
        retired.erase(retired.begin() + i);
      } else {
        i++;
      }
    }
  }

  bool BuildFactory(int bi) {
    std::vector<Pending> v;
    int n = syn::FactoryCount(bi);
    v.resize(n);
    for (int i = 0; i < n; i++) {
      if (quit.load()) return false;
      syn::Buf b;
      syn::RenderFactory(bi, i, b);
      Finish(b, syn::FactoryName(bi, i), &v[i]);
    }
    BankData* d = Pack(v);
    if (!d) return false;
    Publish(bi, d);
    return true;
  }

  void ScanUser() {
    std::string dir;
    bool have;
    {
      std::lock_guard<std::mutex> lk(mu);
      dir = user_dir;
      have = have_dir;
    }
    std::vector<FileEntry> files;
    if (have && !dir.empty()) ScanDir(dir, "", 0, &files, quit);
    std::sort(files.begin(), files.end(), EntryLess);
    std::vector<Pending> v;
    uint64_t total = 0;
    for (size_t i = 0; i < files.size() && v.size() < 128; i++) {
      if (quit.load()) return;
      WavData w;
      if (!LoadWav(files[i].path.c_str(), kUserMaxFrames, 0, &w)) continue;
      uint64_t sz = (uint64_t)w.pcm.size() * sizeof(int16_t);
      if (total + sz > kUserMaxBytes) continue;   // does not fit: skip it (a smaller one later may)
      total += sz;
      Pending p;
      p.pcm.swap(w.pcm);
      p.frames = w.frames;
      p.channels = w.channels;
      p.gain = 1.0f;   // user samples play as recorded (like the hardware)
      std::string nm = files[i].name;
      size_t dot = nm.rfind('.');
      if (dot != std::string::npos) nm = nm.substr(0, dot);
      if (nm.size() > 31) nm = nm.substr(0, 31);
      p.name = nm;
      v.push_back(Pending());
      v.back().pcm.swap(p.pcm);
      v.back().frames = p.frames;
      v.back().channels = p.channels;
      v.back().gain = p.gain;
      v.back().name = p.name;
    }
    BankData* d = Pack(v);
    if (!d) return;
    Publish(BANK_USER, d);
    user_gen.fetch_add(1);
  }

  void Run() {
    if (nice(10) == -1 && errno != 0) { /* not fatal */ }
    busy.store(true);
    uint32_t seen = rescan_req.load();
    double t0 = Now();
    static const int kOrder[] = {BANK_CH, BANK_OH, BANK_RS, BANK_CP, BANK_MISC};
    for (int k = 0; k < 5 && !quit.load(); k++) BuildFactory(kOrder[k]);
    double tf = Now() - t0;
    if (!quit.load()) ScanUser();
    double t1 = Now();
    if (!quit.load()) BuildFactory(BANK_EXP);
    build_sec.store((float)(tf + (Now() - t1)));
    rescan_done.store(seen);
    initial_done.store(true);
    busy.store(false);
    while (!quit.load()) {
      if (retired.empty()) {
        while (sem_wait(&sem) != 0 && errno == EINTR) {}
      } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 250000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        sem_timedwait(&sem, &ts);
      }
      if (quit.load()) break;
      uint32_t r = rescan_req.load();
      if (r != seen) {
        busy.store(true);
        seen = r;
        ScanUser();
        rescan_done.store(seen);
        busy.store(false);
      }
      FreeRetired(false);
    }
  }

  static void* Entry(void* self) {
    static_cast<Impl*>(self)->Run();
    return 0;
  }
};

SampleRom::SampleRom() : d_(new Impl()) {}

SampleRom& SampleRom::Get() {
  // function-local static: constructed on first use (thread-safe in C++11), destroyed at exit/dlclose, which
  // stops and joins the worker.
  struct Holder {
    SampleRom rom;
    ~Holder() { delete rom.d_; rom.d_ = 0; }
  };
  static Holder h;
  return h.rom;
}

void SampleRom::Start(const char* user_dir) {
  std::lock_guard<std::mutex> lk(d_->mu);
  std::string dir = user_dir ? user_dir : "";
  if (!d_->started.load()) {
    d_->user_dir = dir;
    d_->have_dir = user_dir != 0;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024);
    if (pthread_create(&d_->thread, &at, &Impl::Entry, d_) == 0) d_->started.store(true);
    pthread_attr_destroy(&at);
    return;
  }
  if (user_dir && dir != d_->user_dir) {
    d_->user_dir = dir;
    d_->have_dir = true;
    d_->rescan_req.fetch_add(1);
    sem_post(&d_->sem);
  }
}

void SampleRom::Rescan() {
  if (!d_) return;
  d_->rescan_req.fetch_add(1);
  if (d_->started.load()) sem_post(&d_->sem);
}

bool SampleRom::Ready(int bank) const {
  if (!d_ || bank < 0 || bank >= NUM_BANKS) return false;
  return d_->bank[bank].load(std::memory_order_acquire) != 0;
}

int SampleRom::Count(int bank) const {
  if (!d_ || bank < 0 || bank >= NUM_BANKS) return 0;
  BankData* b = d_->bank[bank].load(std::memory_order_acquire);
  return b ? b->count : 0;
}

const Sample* SampleRom::Find(int bank, int index) const {
  if (!d_ || bank < 0 || bank >= NUM_BANKS) return 0;
  BankData* b = d_->bank[bank].load(std::memory_order_acquire);
  if (!b || index < 0 || index >= b->count) return 0;
  return &b->s[index];
}

const Sample* SampleRom::Acquire(int bank, int index) {
  if (!d_ || bank < 0 || bank >= NUM_BANKS) return 0;
  BankData* b = d_->bank[bank].load(std::memory_order_acquire);
  if (!b || index < 0 || index >= b->count) return 0;
  b->pins.fetch_add(1, std::memory_order_acq_rel);
  return &b->s[index];
}

void SampleRom::Release(const Sample* s) {
  if (!s || !s->owner) return;
  static_cast<BankData*>(s->owner)->pins.fetch_sub(1, std::memory_order_acq_rel);
}

int SampleRom::IndexOf(int bank, const char* name) const {
  if (!name) return -1;
  BankData* b = (d_ && bank >= 0 && bank < NUM_BANKS) ? d_->bank[bank].load(std::memory_order_acquire) : 0;
  if (!b) return -1;
  for (int i = 0; i < b->count; i++) if (strcmp(b->s[i].name, name) == 0) return i;
  for (int i = 0; i < b->count; i++) if (strcasecmp(b->s[i].name, name) == 0) return i;
  return -1;
}

int SampleRom::Capacity(int bank) { return (bank >= 0 && bank < NUM_BANKS) ? kCap[bank] : 0; }

uint32_t SampleRom::UserGeneration() const { return d_ ? d_->user_gen.load() : 0; }

bool SampleRom::Busy() const { return d_ && d_->busy.load(); }

void SampleRom::WaitIdle() {
  if (!d_ || !d_->started.load()) return;
  // initial build finished and every rescan requested so far served
  while (!d_->quit.load()) {
    if (d_->initial_done.load() && d_->rescan_done.load() == d_->rescan_req.load() && !d_->busy.load()) return;
    usleep(2000);
  }
}

const char* SampleRom::FactoryName(int bank, int index) { return syn::FactoryName(bank, index); }

uint64_t SampleRom::MemoryBytes() const { return d_ ? d_->bytes.load() : 0; }

double SampleRom::FactoryBuildSeconds() const { return d_ ? d_->build_sec.load() : 0.0; }

}  // namespace dlx
