# Vendored code in DrLogueXXL

Everything not written for DrLogueXXL lives under `third_party/` as committed copies (nothing is fetched at build
time). Per-piece details (upstream repo and commit, licence evidence, every local change):

- **Synth units of the MULTI part** (Korg NTS-1 mkII Pluck and Waves, EffeESP32, Elementish, Drupiter, Maxisynth +
  Maximilian): [src/units/VENDORED_UNITS.md](units/VENDORED_UNITS.md)
- **Effects** (Mutable Instruments Clouds reverb via cldmnky's clouds-revfx, stmlib): [src/fx/VENDORED_FX.md](fx/VENDORED_FX.md)
- **Korg logue SDK drumlogue headers** (`third_party/logue-sdk/platform/drumlogue/common`, BSD-3-Clause, korginc/logue-sdk
  commit e78b16c): unchanged; they define the unit API the hosted units are written against.
- **mpc-vst-plugins** (`third_party/mpc-vst-plugins`, git submodule, sunskiefer fork at 3845bea): the VST2 wrapper with the
  Q-Link / automation fix (#486-#489) and the skin tools.
- **Brickwall limiter** (`src/limiter.h`) and `src/denormals.h`: from the same author's PlateauXXL / RMXXXL (GPL-3.0).

Left out on licence grounds (no licence from their authors, or ROM data): fedemone's ScrutaAstri, Brachetti, EffeMD,
delay/reverb/master units; dukesrg's FM64, Tesseract, loguePADS; cldmnky's Pepege (PPG ROM wavetables) and Druteus
(Proteus ROM); the microKORG 2 units (Sinevibes licence). The effect types Tribe, Labyrinth, Halo and Omni are written
here after the descriptions of fedemone's units; none of their code is used.
