# Effects: vendored code and own designs

DrLogueXXL's DELAY, REVERB and MASTER slots (`src/fx/delay.cc`, `reverb.cc`, `master.cc`, helpers in `fx_util.h`).
Everything not listed under "Vendored" was written for DrLogueXXL (GPL-3.0-or-later).

Build: add `src/fx/delay.cc src/fx/reverb.cc src/fx/master.cc` to the sources and
`-Ithird_party/mutable -Ithird_party/community/cldmnky/clouds-revfx` to the include dirs (only `reverb.cc` needs
them).

## Vendored

### Clouds reverb, diffuser and pitch shifter (REVERB type "Clouds")

| | |
|---|---|
| Upstream unit | cldmnky/drumlogue-units, `drumlogue/clouds-revfx` (https://github.com/cldmnky/drumlogue-units) |
| Commit | 3a6a050411595bb06a0d22fae849ea7caacf66b4 (2026-10-08) |
| Files | `reverb.h`, `diffuser.h`, `pitch_shifter.h` -> `third_party/community/cldmnky/clouds-revfx/` |
| Licence | MIT for the Mutable-derived DSP (MIT notice in `pitch_shifter.h`; "Original Copyright 2014 Emilie Gillet (MIT License)" in `reverb.h`/`diffuser.h`; README.clouds-revfx.md "License: ... MIT"). The repository's own `LICENSE` file is Apache-2.0 (copied next to the files); README.md says MIT. Both are GPL-3.0 compatible. |
| Changes | none to the files. Not vendored: `clouds_fx.cc/.h` (the drumlogue unit wrapper), the micro-granular stage, the LFOs and the NEON helpers (`dsp/neon_dsp.h`): DrLogueXXL drives the three DSP classes itself, scalar only. |

| | |
|---|---|
| Upstream | pichenettes/eurorack (Mutable Instruments), commit f4cfa7697095a28d292bff758beb51deb49b5a86 |
| Files | `clouds/dsp/fx/fx_engine.h`, `clouds/dsp/frame.h` -> `third_party/mutable/clouds/dsp/` |
| Licence | MIT (notice in each file) |
| Changes | none |

| | |
|---|---|
| Upstream | pichenettes/stmlib (eurorack submodule e3bd7c9cc00e4364166f9905c0509b6ffd0535ec), via the LXRXXL ZYNTH copy |
| Files | `stmlib.h`, `dsp/dsp.h`, `dsp/cosine_oscillator.h`, `LICENSE` -> `third_party/mutable/stmlib/` |
| Licence | MIT (`third_party/mutable/stmlib/LICENSE`) |
| Changes | `dsp/dsp.h`, marked "DrLogueXXL": the portable `Clip16/ClipU16/Sqrt` are used on every non-ARM build (they were only for `TEST`), so the x86 tests build; ARM keeps the `ssat/usat/vsqrt` asm. |

Clouds parameter mapping (DrLogueXXL control -> clouds-revfx parameter; also in `reverb.cc`, `ProcessClouds`):

| DrLogueXXL | clouds-revfx |
|---|---|
| TIME 0..100 % | reverb_time 0.30..0.99 (RT60 about 1.4 s .. 165 s) |
| INPUT 0..100 % | IN GAIN 0.10..0.45 and DIFFUSION 0.45..0.75 together |
| BRIGHT 0..100 % | LP DAMP 0.30..0.95 |
| RISE 0..50 % | TEXTURE (the diffuser after the reverb) 1..0 |
| RISE 50..100 % | SHIFT AMT 0..1 with SHFT PTCH +12 st (octave-up shimmer on the output) |
| DIVE 0..50 % | SHFT SIZE 0..1 (pitch-shifter grain window, 4 ms .. 64 ms) |
| DIVE 50..100 % | SHIFT AMT 0..1 with SHFT PTCH -12 st (a second shifter instance, octave down) |
| (fixed) | DRY/WET 100 % wet; LEVEL scales the return. GRAIN*, FREEZE and the two LFOs are not used. |

Memory: 64 kB (reverb, 12-bit) + 8 kB (diffuser) + 2 x 16 kB (shifters).

## Own designs in place of fedemone's units (licence not clear enough to vendor)

The fedemone units in fedemone/logue-sdk (`platform/drumlogue/delay_tribal`, `PortaCassette`, `reverb_labirinto`,
`reverb_light`, `omnipress`, checked at commit 018a853c0378e98a4afa6b0860ccc214756aca40) carry **no licence of
their own**: no SPDX tag, copyright line or licence text in any source or README of those units, only an "as is"
disclaimer. The repository's root `LICENSE` is KORG's BSD-3-Clause for KORG's SDK, which does not by itself
license a third party's unit, and the README describes the units as "independently developed, third-party
projects". So none of their code is in DrLogueXXL. The types below are written from scratch, from the units'
published descriptions only (their READMEs), and keep the names for now; if fedemone adds a licence these can be
swapped for the originals (the names may need changing otherwise).

| Type | DrLogueXXL design |
|---|---|
| DELAY Tribe (written here; inspired by the README of fedemone's Tribal unit) | A multi-tap percussion spatialiser: 2/4/6/8/10 "clone" taps (TRACKING picks the count) on an irregular grid of quarters of the BPM division (DIV), each clone later, quieter, darker (TONE sets how fast the clones darken), panned outward alternately (SPREAD = width), with a per-hit timing wobble of up to 6 ms (MOD). FB feeds the latest clone back; FILTER/CUTOFF and SAT in the loop as usual. |
| DELAY Cassette | A portastudio cassette echo on the free TIME: heavy wow (0.9 Hz) + flutter (11 Hz) + random drift (MOD), narrow bandwidth in the loop (TONE: 1.8..9 kHz), a 110 Hz head bump on the output (stronger when darker), asymmetric tape saturation that is always slightly on (SAT adds more), 6 % L/R crosstalk, 70 Hz low cut in the loop. TRACKING as for Tape. No hiss is added. |
| REVERB Labyrinth (written here; inspired by fedemone's Labirinto README) | Two 4-line FDN banks, each Hadamard-mixed and written into the other (true ping-pong: energy crosses the stereo field once per bounce, with a small orthogonal bleed), a resonant "material" band in the loop (RISE = resonance amount and pitch, 300 Hz..3.6 kHz), bounce time on DIVE (60..500 ms, shown in ms). |
| REVERB Halo (written here; inspired by fedemone's Luce README) | A clean 8-line FDN tail (no modulation in the loop) feeding parallel paths: an octave-down "dark" two-head grain halo through a 1.2 kHz low-pass (DIVE = level), random 5-15 ms grains pitched +5/+7/+12/+19/+24 semitones with random pan (RISE = level and density), and a harmonic exciter above 5 kHz (BRIGHT above 50 %). |
| MASTER Omni (written here; inspired by fedemone's Omnipress README) | An Omnipressor-style function compressor (`master.cc`, `Omni`). Slots: 1 THRESH (-60..0 dB pivot), 2 FUNCTION (0 = 1:2 expansion, 33 = 1:1, 50 = 2:1, 67 = limit, 100 = reverse), 3 ATTACK (0.1..100 ms), 4 RELEASE (10..2000 ms), 5 DRIVE (biased tube-style saturation after the dynamics, level-matched, DC-blocked), 6 MIX (dry/wet). The function acts on both sides of the pivot (quiet material is lifted toward the threshold, at most +12 dB, never below -70 dBFS); cut is limited to -30 dB. 5 ms RMS detector; the compressor's SIDECH switch keys it from the key bus. |

## Own designs (not modelled on a community unit)

Delay Stereo/Mono/Tape (+BPM), reverb Room/Hall/Space/Riser/Submarine, master Compressor/Filter/Boost/EQ Three.
See the header comments of the three files.
