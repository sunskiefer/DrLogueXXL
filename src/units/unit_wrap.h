// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- shared prelude for the unit wrappers (src/units/<unit>.cc).
//
// Each wrapper #includes a vendored unit's headers inside its own namespace (u_<name>), so the many units that
// define a global `unit_header`, a class called `Synth` and the like don't collide. For that to work, every system
// header those sources use must already be included at global scope (their include guards then make the nested
// #includes no-ops): that is what this file does. It also neutralises the unit-binary attributes (section
// placement of the header, "used" callbacks) that only matter for a standalone .drmlgunit.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <functional>
#include <iostream>
#include <iterator>
#include <new>
#include <numeric>
#include <utility>
#include <vector>

#include "attributes.h"   // drumlogue (third_party/logue-sdk/platform/drumlogue/common)
#include "runtime.h"
#include "unit.h"
#undef __unit_header
#define __unit_header
#undef __unit_callback
#define __unit_callback

#include "units/unit_host.h"

namespace dlx {

// Common forwarding for a drumlogue synth unit whose synth class has the usual Init/Render/NoteOn... members.
// T = the unit's synth class (owned by value), H = a reference to the unit's header.
// Method names vary between authors, so each wrapper maps them with small lambdas/overrides where needed.
inline int8_t CheckDesc(const unit_runtime_desc_t* desc, const unit_header_t& h) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != h.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->samplerate != 48000) return k_unit_err_samplerate;
  if (desc->output_channels != 2) return k_unit_err_geometry;
  return k_unit_err_none;
}

inline int32_t ClampParam(const unit_header_t& h, uint8_t id, int32_t v) {
  if (id >= h.num_params) return 0;
  const unit_param_t& p = h.params[id];
  return v < p.min ? p.min : (v > p.max ? p.max : v);
}

}  // namespace dlx
