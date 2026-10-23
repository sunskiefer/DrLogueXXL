// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the constants the vendored copych/fedemone FM engine headers (fm_operator.h, fm_voice6.h, adsr.h,
// svf_filter.h) use. Written for DrLogueXXL; replaces the unit's own constants.h (not vendored).
#pragma once
#include <cstdint>
#include <cmath>

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline))
#endif

constexpr float SAMPLE_RATE = 48000.0f;
constexpr float INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;
#ifndef PI
constexpr float PI = 3.14159265358979f;
#endif
constexpr float TWO_PI = 2.0f * PI;
constexpr float PI_F = PI;
constexpr float ONE_DIV_SQRT2 = 0.707106781f;
constexpr float ONE_DIV_SQRT3 = 0.577350269f;
constexpr float ONE_DIV_SQRT5 = 0.447213595f;
