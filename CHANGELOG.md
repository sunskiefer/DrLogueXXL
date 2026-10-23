# Changelog

## 0.2.1
- Note names as MPC shows them (36 = C1, 60 = C3) on the screen.
- The full README and a user guide (docs/USER_GUIDE.md); listed in the mpc-vst-plugins catalog.

## 0.2.0
- The panel: three pages. PERFORM (the kit knobs as on the machine with SHIFT for HT and SP 2, the screen and its
  four knobs with pages for every part, delay, reverb, master and global, the 16 part / effect keys with LEDs, the
  parameter pop-up), MIXER (11 strips with the ZYNTH faders, MASTER) and SCREEN (big screen with the wave picture,
  four macros: DECAY, TUNE, FILTER, SPACE).
- Engine: the screen's pages and knobs (src/ui_pages.cc), macros applied to what the parts hear (saved values stay),
  picking VPM / NOISE / USER plays that MULTI source, long whole-number ranges step one per Q-Link event.
- Still on the mpc-vst-plugins wrapper with the Q-Link and automation fix (#486-#489).

## 0.1.0
- First build: the sound engine. 11 parts like the drumlogue: analogue-style BD, SD, LT, HT circuit models; six
  sample parts (CH, OH, RS, CP, SP1, SP2) playing synthesised factory banks or your own WAVs; the MULTI part with VPM,
  NOISE and open-source logue SDK synth units (Korg's and the community's). Send delay and reverb, master effect with
  sidechain, choke groups, velocity, note maps (Pads / GM / Custom), chromatic MULTI above a key split, brickwall
  limiter. Runs at 48 kHz inside (as the drumlogue and its units do) and resamples to MPC's 44.1 kHz.
- Built on the mpc-vst-plugins wrapper with the Q-Link and automation fix (#486-#489).
- Played on an Akai Force (sound engine), 2026-10-23.
