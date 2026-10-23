// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the synth units the MULTI part hosts, in the order of src/units/registry.txt (the order users see;
// tools/gen_params.py builds the mt_engine list from that file and test/multi_test.cc checks the two agree).
// Sources, licences and changes: src/units/VENDORED_UNITS.md.
#include "units/unit_host.h"

namespace dlx {

SynthUnit* NewEffeESP32Unit();    // src/units/effeesp32.cc
SynthUnit* NewPluckUnit();        // src/units/pluck.cc
SynthUnit* NewWavesUnit();        // src/units/waves.cc
SynthUnit* NewElementishUnit();   // src/units/elementish.cc
SynthUnit* NewDrupiterUnit();     // src/units/drupiter.cc (+ drupiter_*.cc)
SynthUnit* NewMaxisynthUnit();    // src/units/maxisynth.cc

namespace {
struct Entry { const char* name; SynthUnit* (*make)(); };
const Entry kUnits[] = {
    {"EffeESP32", NewEffeESP32Unit},
    {"Pluck", NewPluckUnit},
    {"Waves", NewWavesUnit},
    {"Elementish", NewElementishUnit},
    {"Drupiter", NewDrupiterUnit},
    {"Maxisynth", NewMaxisynthUnit},
};
}  // namespace

int NumSynthUnits() { return (int)(sizeof kUnits / sizeof kUnits[0]); }
const char* SynthUnitName(int i) { return i >= 0 && i < NumSynthUnits() ? kUnits[i].name : ""; }
SynthUnit* NewSynthUnit(int i) { return i >= 0 && i < NumSynthUnits() ? kUnits[i].make() : nullptr; }

}  // namespace dlx
