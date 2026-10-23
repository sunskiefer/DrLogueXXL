// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the pieces of the NTS-1 mkII oscillator runtime that the vendored Korg osc units (Pluck, Waves)
// link against: osc_white(), the wave banks wavesA..wavesF and the tanpi/bitres lookup tables (on the hardware
// these live in the firmware ROM; here they are generated, see nts_osc_runtime.cc and src/units/VENDORED_UNITS.md).
#pragma once
#include <stdint.h>

namespace dlx {
// The generator osc_white() draws from. Each hosted unit points it at its own state for the duration of each call
// into the unit (thread_local: plugin instances on different threads never share it).
extern thread_local uint32_t* t_nts_rng;
// Fills the generated tables once (thread-safe); called before any osc unit is constructed.
void NtsOscTablesInit();
}  // namespace dlx
