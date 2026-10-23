# Changelog

## 0.1.0 (in progress)
- First build: the sound engine. 11 parts like the drumlogue: analogue-style BD, SD, LT, HT circuit models; six
  sample parts (CH, OH, RS, CP, SP1, SP2) playing synthesised factory banks or your own WAVs; the MULTI part with VPM,
  NOISE and open-source logue SDK synth units (Korg's and the community's). Send delay and reverb, master effect with
  sidechain, choke groups, velocity, note maps (Pads / GM / Custom), chromatic MULTI above a key split, brickwall
  limiter. Runs at 48 kHz inside (as the drumlogue and its units do) and resamples to MPC's 44.1 kHz.
- Built on the mpc-vst-plugins wrapper with the Q-Link and automation fix (#486-#489).
- The skin is a temporary auto-generated test layout; the real UI comes next.
