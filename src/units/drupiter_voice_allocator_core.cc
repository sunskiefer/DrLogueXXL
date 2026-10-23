// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- compiles the vendored Drupiter source ../common/voice_allocator_core.cc inside namespace u_drupiter (see drupiter.cc).
#ifndef DRUPITER_MAX_VOICES
#define DRUPITER_MAX_VOICES 4
#endif
#define UNISON_MAX_DETUNE 50
#define ENABLE_POLYBLEP 1
#include "units/unit_wrap.h"

namespace u_drupiter {
#include "../../third_party/community/cldmnky/drupiter-synth/../common/voice_allocator_core.cc"
}  // namespace u_drupiter
