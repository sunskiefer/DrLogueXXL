// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "Pluck": Korg's NTS-1 mkII pluck oscillator (Karplus-Strong waveguide, BSD-3-Clause, vendored in
// third_party/logue-sdk/platform/nts-1_mkii/pluck) hosted as a drumlogue synth through nts_osc_adapter.h.
#include "units/unit_wrap.h"
#include "units/nts_osc_runtime.h"

namespace u_pluck {
#include "../../third_party/logue-sdk/platform/nts-1_mkii/pluck/osc.h"

// the unit's parameters, exactly as its header.c declares them
static const unit_param_t kOscParams[] = {
    {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"DAMP"}},
    {0, 1023, 0, 1023, k_unit_param_type_none, 0, 0, 0, {"DECAY"}},
    {0, 1023, 0, 1023, k_unit_param_type_none, 0, 0, 0, {"NOISE CUTOFF"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"PICKUP POS"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"DRIVE"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"STIFFNESS"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"NOISE FM"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"DISPERSION"}},
};
static const int kOscNumParams = 8;
static const char kUnitName[] = "Pluck";
static const int kAttackInit = 0, kReleaseInit = 4000;   // a pluck rings on through its own DECAY
#include "units/nts_osc_adapter.h"
}  // namespace u_pluck

namespace dlx {
SynthUnit* NewPluckUnit() { return new u_pluck::NtsOscUnit(); }
}
