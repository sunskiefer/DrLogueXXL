// SPDX-License-Identifier: GPL-3.0-or-later
// Flush denormals to zero for the duration of a block: a decaying reverb tail (and a frozen tank fading to
// silence) is exactly where they appear, and on the device they cost far more than normal numbers.
#pragma once
#include <stdint.h>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

class ScopedDenormalDisable {
 public:
  ScopedDenormalDisable() {
#if defined(__arm__)
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(old_));
    uint32_t fz = old_ | (1u << 24);   // FPSCR.FZ (VFP, single and double)
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fz));
#elif defined(__aarch64__)
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(old64_));
    uint64_t fz = old64_ | (1ull << 24);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fz));
#elif defined(__SSE__) || defined(__x86_64__)
    old_ = _mm_getcsr();
    _mm_setcsr(old_ | 0x8040);   // FTZ | DAZ
#endif
  }
  ~ScopedDenormalDisable() {
#if defined(__arm__)
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(old_));
#elif defined(__aarch64__)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(old64_));
#elif defined(__SSE__) || defined(__x86_64__)
    _mm_setcsr(old_);
#endif
  }

 private:
  uint32_t old_ = 0;
  uint64_t old64_ = 0;
  ScopedDenormalDisable(const ScopedDenormalDisable&);
  ScopedDenormalDisable& operator=(const ScopedDenormalDisable&);
};
