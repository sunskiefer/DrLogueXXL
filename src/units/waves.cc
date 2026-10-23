// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- "Waves": Korg's NTS-1 mkII waves oscillator (morphing wavetable, BSD-3-Clause, vendored in
// third_party/logue-sdk/platform/nts-1_mkii/waves) hosted as a drumlogue synth through nts_osc_adapter.h. Its wave
// banks are generated (nts_osc_runtime.cc): the originals are in the NTS-1 firmware ROM, not in the SDK.
#include "units/unit_wrap.h"
#include "units/nts_osc_runtime.h"

namespace u_waves {
#include "../../third_party/logue-sdk/platform/nts-1_mkii/waves/osc.h"

static const unit_param_t kOscParams[] = {
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SHPE"}},
    {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SUB"}},
    {0, WAVE_A_CNT - 1, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"WAVE A"}},
    {0, WAVE_B_CNT - 1, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"WAVE B"}},
    {0, SUB_WAVE_CNT - 1, 0, 0, k_unit_param_type_enum, 0, 0, 0, {"SUB WAVE"}},
    {0, 1000, 0, 0, k_unit_param_type_percent, 1, 1, 0, {"RING MIX"}},
    {0, 1000, 0, 0, k_unit_param_type_percent, 1, 1, 0, {"BIT CRUSH"}},
    {0, 1000, 0, 250, k_unit_param_type_percent, 1, 1, 0, {"DRIFT"}},
};
static const int kOscNumParams = 8;
static const char kUnitName[] = "Waves";
static const int kAttackInit = 2, kReleaseInit = 250;
#include "units/nts_osc_adapter.h"
}  // namespace u_waves

namespace dlx {
SynthUnit* NewWavesUnit() { return new u_waves::NtsOscUnit(); }
}
