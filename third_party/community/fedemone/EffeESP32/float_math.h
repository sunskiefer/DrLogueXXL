// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- float helpers for the vendored FM engine headers: Korg's logue SDK float_math.h (BSD-3-Clause,
// third_party/logue-sdk/platform/nts-1_mkii/common/utils) plus e_expff(), which the engine's (unused) ExpDecay()
// helper names. Replaces the unit's own float_math.h (not vendored).
#pragma once
#include "../../../logue-sdk/platform/nts-1_mkii/common/utils/float_math.h"
static inline float e_expff(float x) { return expf(x); }
