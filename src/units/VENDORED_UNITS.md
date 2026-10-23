# MULTI synth units: sources, licences, changes

DrLogueXXL (GPL-3.0-or-later) hosts these drumlogue / logue SDK units in its MULTI part. Each is wrapped as a
`dlx::SynthUnit` (src/units/unit_host.h) that **owns** the unit's synth object and parameter table; the vendored
sources are `#include`d inside a per-unit namespace (`u_pluck`, `u_waves`, `u_effeesp32`, `u_elementish`,
`u_drupiter`, `u_maxisynth`) so the units' global `unit_header`s and class names can't collide. The units run as on the
drumlogue: 48 kHz, at most 64 frames per call, stereo interleaved out (src/units/unit_host.cc `FillRuntimeDesc`:
target `k_unit_target_drumlogue_synth`, API 2.0.0, 0 in / 2 out, no sample banks).

Rules applied: only code with a clear licence that allows redistribution in a GPL-3.0 project; no commercial ROM data;
no NEON-only code (the armv7 build has no NEON: every unit builds and runs scalar); no mutable state shared between
plugin instances (every such static was fixed, listed below; `test/multi_test.cc` proves it by rendering each unit
next to other instances and comparing with it alone, bit for bit).

Order users see (src/units/registry.txt): EffeESP32, Pluck, Waves, Elementish, Drupiter, Maxisynth.

Cost: ns per 48 kHz stereo frame on x86 (Xeon 2.8 GHz, -O2), default patch, one note every 0.5 s held 0.25 s
(`multi_test` prints it). The Force budget per unit is about 600-800 cycles per frame (Cortex-A17 at 1.8 GHz), and as a
rough guide that's about 100 ns on this x86. All six fit. Elementish, Drupiter and Maxisynth are the heaviest,
VPM/NOISE the lightest:
EffeESP32 15, Pluck 27, Waves 29, Elementish 84, Drupiter 74-96, Maxisynth 97-129, VPM 20, NOISE 8.

---

## EffeESP32 (`src/units/effeesp32.cc`)
- Upstream: copych/ESP32-S3_FM_Drum_Synth (https://github.com/copych/ESP32-S3_FM_Drum_Synth, commit 6e47275), MIT,
  © 2025 Evgeny Aslovskiy "Copych", via its drumlogue port in fedemone/logue-sdk `platform/drumlogue/EffeESP32`
  (commit 018a853).
- Vendored: `third_party/community/fedemone/EffeESP32/` `fm_operator.h`, `fm_voice6.h`, `adsr.h`, `svf_filter.h`,
  `drum_patches.h` (all carry copych's MIT attribution; patch data is generated from copych's MIT drumkit JSON) and
  `LICENSE.copych-MIT`.
- **Not** vendored: fedemone's own control layer (`synth.h`, `unit.cc`, `header.c`, `constants.h`, `float_math.h`,
  `output_stage.h`): fedemone/logue-sdk has no licence statement for its own units (the repo root LICENSE is Korg's BSD
  for the SDK). So `effeesp32.cc` is a controller written for DrLogueXXL, with the unit's 24 panel parameters (same
  names, ranges, meanings), a 4-voice pool with choke-and-reallocate, and the 59 instruments also exposed as presets.
  It has no limiter (DrLogueXXL's output has one). `constants.h` and `float_math.h` in that folder are DrLogueXXL files:
  the constants the engine headers need, and Korg's BSD `float_math.h` plus `e_expff`.
- Pitch: the instrument's base frequency, +Pitch, + (note − 60). A pad hit at the default unit note C4 plays the patch
  as designed; chromatic notes transpose it.
- Statics: none (the engine is per-voice state).

## Pluck (`src/units/pluck.cc`)
- Upstream: korginc/logue-sdk `platform/nts-1_mkii/pluck` (commit e78b16c), BSD-3-Clause, © KORG.
- Vendored: `third_party/logue-sdk/platform/nts-1_mkii/pluck/{osc.h,header.c,dsp/*.hpp}` plus the needed
  `nts-1_mkii/common` headers (`attributes.h`, `macros.h`, `osc_api.h`, `processor.h`, `unit_osc.h`,
  `utils/float_math.h`, `utils/int_math.h`, `dsp/biquad.hpp`).
- Changes: the C++17 structured binding in `process()` was rewritten as C++11 (the plugin builds with gnu++11).
  Include paths were made relative. `unit_osc.h` includes only `attributes.h`/`macros.h`/`osc_api.h` (not the NTS-1
  `unit.h`/`runtime.h`). `osc_api.h` no longer includes `utils/fixed_math.h` (it needs CMSIS and the units don't use it).
- Adapter (`nts_osc_adapter.h`): an NTS-1 mkII *oscillator* gets its pitch from a runtime context and outputs mono. The
  adapter turns note on/off into the Osc's `noteOn`/`noteOff` plus `setPitch(f/48000)` each block. It adds an amp
  envelope in two extra slots, ATTACK (0-2000 ms) and RELEASE (0-8000 ms, Pluck default 4 s so the string's own DECAY
  rules), and duplicates mono to stereo. The unit's own 8 parameters are unchanged.
- `osc_white()` (firmware) is provided by `nts_osc_runtime.cc`: xorshift32. Each adapter instance owns its state and
  points the thread-local `dlx::t_nts_rng` at it around every call into the Osc, so instances share nothing.

## Waves (`src/units/waves.cc`)
- Upstream: korginc/logue-sdk `platform/nts-1_mkii/waves` (commit e78b16c), BSD-3-Clause, © KORG.
- Vendored: `third_party/logue-sdk/platform/nts-1_mkii/waves/{osc.h,waves_common.h,header.c}` (CRLF -> LF).
- **Wave banks are ours**: the unit reads `wavesA..wavesF` from the NTS-1 firmware ROM (not in the SDK).
  `nts_osc_runtime.cc` generates stand-ins with the same counts and size (16/16/14/13/15/16 single cycles of 128
  samples + guard point), band-limited additive families in order of increasing harmonic content (A near-pure sines, B
  soft triangle-like series, C organ drawbar mixes, D formant bumps, E saw/square/pulse spectra, F sparse
  high/inharmonic-sounding clusters). It also generates `tanpi_lut_f` (tan(πx) over [0, 0.49]) and `bitres_lut_f`
  (2^(bits−1), bits mapped exponentially 24 -> 1). The algorithm runs unchanged.
- Changes: include paths; `osc_bitresf()` argument clamped below 1.0 (at BIT CRUSH 100 % Korg's lookup reads one past
  the 129-entry table: found by UBSan).
- Adapter as Pluck (ATTACK default 2 ms, RELEASE 250 ms).

## Elementish (`src/units/elementish.cc`)
- Upstream: cldmnky/drumlogue-units `drumlogue/elementish-synth` (commit 3a6a050), Apache-2.0 (repo LICENSE; README
  says "Project code: MIT"; both allow GPL-3.0 inclusion, and Apache-2.0 is GPL-3.0-compatible). Inspired by Mutable
  Instruments Elements (MIT); `samples.h` is resampled Elements exciter data (MIT).
- Vendored: `third_party/community/cldmnky/elementish-synth/{elements_synth_v2.h,modal_synth.h,samples.h,header.c,
  dsp/*.h}`, `common/{neon_dsp.h,fixed_mathq.h,arm_intrinsics.h,perf_mon.h}`, `LICENSE`.
- Built as shipped: `ELEMENTS_LIGHTWEIGHT`, monophonic, scalar (`USE_NEON` and the NEON resonator are not defined; the
  unit has full scalar paths). No quality reduction was needed (84 ns/frame).
- Instance-safety fixes: `ElementsSynth::Render` `static float out_l/out_r[]` -> members; `ModalSynth` `static float
  wet_buffer[]` and the RND-LFO `static float last_phase` -> members; `Reverb` `static` comb/allpass delay lines (36 KB,
  shared by every instance) -> members; `MarblesSequencer::GetCurrentScale` scale tables made `static const`.
  `perf_mon.h` include path made relative. `arm_intrinsics.h`: the inline-asm (ARMv6 DSP) path only on 32-bit ARM, the
  portable C path elsewhere.

## Drupiter (`src/units/drupiter.cc` + `drupiter_*.cc`)
- Upstream: cldmnky/drumlogue-units `drumlogue/drupiter-synth` (commit 3a6a050), Apache-2.0 (as above). The DSP is
  "based on" Nick Copeland's Bristol (GPL-3.0-or-later, checked in Bristol's source headers), which is compatible with
  DrLogueXXL's GPL-3.0-or-later.
- Vendored: `third_party/community/cldmnky/drupiter-synth/` (all sources except `unit.cc`) and
  `common/{catchable_value.h,dsp_utils.h,hub_control.h,midi_helper.h,param_format.h,preset_manager.h,
  smoothed_value.h,stereo_widener.h,voice_allocator.h,voice_allocator_core.cc}`.
- Each upstream `.cc` is compiled by its own `src/units/drupiter_<file>.cc` wrapper inside `namespace u_drupiter` (the
  files share file-local names, so a single unity TU doesn't work). Build: 4 voices (`DRUPITER_MAX_VOICES=4`, the
  shipped value), scalar (`USE_NEON` is only auto-defined under `__ARM_NEON`, which the build doesn't set).
- Instance-safety fixes: `JupiterDCO` and `JupiterVCF` static lookup tables are now filled exactly once,
  thread-safe (C++11 magic static; they are read-only afterwards, same values). `JupiterDCO` noise used `rand()`
  (process-global) -> a hash of the phase. `voice_allocator.cc` file statics `cached_poly_scale/voice_count` -> computed
  per block. `polyphonic_renderer.cc` file-static `s_poly_cache` -> per-`DrupiterSynth` storage. `GetParameterStr`'s
  `static char tune_buf[]` -> member. `hub_control.h` process-wide lazily filled string cache disabled (always formats
  into the caller's buffer). DEBUG-only statics left (DEBUG isn't defined).
- C++11 fixes: `midi_helper.h` multi-statement `constexpr` functions made single-return. `dsp_utils.h` `fast_pow2`
  (always_inline and recursive, which GCC refuses at -O1) unrolled, with the same values. Out-of-class definitions for
  the ODR-used `static constexpr` members of `JupiterEnvelope`/`JupiterLFO` (in `drupiter.cc`). One include path fixed
  in `dsp/unison_oscillator.cc`.
- Gate handlers as in the unit's own `unit.cc`: GateOn plays C4, GateOff releases all.

## Maxisynth (`src/units/maxisynth.cc`)
- Upstream: boochow/maxisynth (commit b69a9a0), MIT, © 2023 boochow; Maximilian library, boochow's patched fork
  boochow/Maximilian branch `main` (commit 60e6ec6, the one maxisynth targets), MIT, © 2009 Mick Grierson. PolyBLEP
  (Martin Finke, from Tale's Jesusonic code) is under the zlib-style WDL licence (its header).
- Vendored: `third_party/community/boochow/maxisynth/{synth.h,header.c,LICENSE}`,
  `third_party/community/boochow/Maximilian/{LICENSE.txt,src/maximilian.{h,cpp},src/libs/maxiPolyBLEP.h,
  src/libs/PolyBLEP/PolyBLEP.{h,cpp}}`.
- Changes: `synth.h` NEON store (`vst1_f32`) -> two scalar stores, `<arm_neon.h>` removed, Maximilian include paths made
  relative. `Init` no longer writes `maxiSettings::sampleRate` (a process-wide static); instead its initial value in
  `maximilian.cpp` is 48000 (nothing writes it). `maxiPolyBLEP.h` got an include guard; `maximilian.h` includes
  `<cstdint>`.
- Wrapper: the header is the unit's `header.c` as a C++ aggregate. "Wave" (typeless 0..2) shows Saw/Square/Tri.
  AllNoteOff releases every held gate (the unit's own AllNoteOff is empty and its gate is a counter).
- Note: at extreme resonance the unit's biquad output goes far past full scale (19x seen in the slot sweep). The MULTI
  part's safety stage (unity up to ±1, soft knee to ±2) holds it, as the drumlogue's mixer would clip.

---

## Considered and left out
- **EffeMD** (fedemone, Machinedrum-style FM percussion): its drum models are ports of ctag-fh-kiel/md-drum-synth,
  whose repo has no licence. fedemone's own code has no licence statement either. It cannot be redistributed.
- **ScrutaAstri** (fedemone drone synth), **Brachetti** (fedemone modal/physical drum synth; its sample folder also
  records no provenance): no licence statement by the author; the repo LICENSE is Korg's BSD for the SDK only.
- **Pepege** (cldmnky, PPG-style wavetable): its wavetables (`ppg_waves.cc`, `wavetables.h`) are the PPG Wave 2.2 8-bit
  waveform ROM data taken from usynth's `ppg_data.c`. Commercial ROM data, so it's out. Rebuilding it on generated
  tables would be a different instrument, so it was left out rather than renamed.
- **Druteus** (cldmnky): needs Proteus SoundFont/ROM data.
- **FM64 / Tesseract / loguePAD** (dukesrg, drumlogue branch): no licence statement by the author (Korg's BSD at the
  root covers the SDK); FM64 and Tesseract are also NEON-intrinsic code.
- microKORG2 units: Sinevibes-to-Korg licence, not allowed.
- The NTS-1 mkII `dummy-osc` and the hvcc generator (boochow): not instruments.

## The MULTI part itself
`src/parts/multi.cc`: VPM and NOISE are written for DrLogueXXL (spec: docs/VOICES.md). Every unit is allocated and
initialised in `Init()` (engine switches never allocate). Slot values are applied only when the rounded value changes.
Unit output is checked for NaN/Inf (a unit that goes unstable is reset and silenced) and passes through the safety
stage. `Active()` for units is "a note held, or output above -90 dBFS within the last 200 ms".
