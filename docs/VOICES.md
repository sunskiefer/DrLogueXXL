# DrLogueXXL voices: what the drumlogue does and how DrLogueXXL models it

DrLogueXXL is a drumlogue-style hybrid drum machine for the Akai Force / MPC OS (VST2). It is not a port of Korg
firmware: the analogue voices are circuit models written here, the factory samples are synthesised, and the MULTI
part hosts open-source logue SDK units (Korg's own BSD units and community units, see src/VENDORED.md).

Sources for the hardware facts: the drumlogue Owner's Manual (Korg, parameter names and ranges), and the reviews in
Sound On Sound and Attack Magazine (sound character). Everything below is paraphrased.

## The hardware in short

- 11 parts: 4 analogue (BD, SD, LT, HT), 6 sample parts (CH, OH, RS, CP, SP1, SP2) and 1 MULTI (digital synth).
- The analogue parts are analogue synthesis with a digital layer: BD and the toms add a PCM transient/layer, the
  snare adds a "snappy" noise layer (types including 808- and 909-style, several noise colours and three lo-fi ones).
- Every part has a filter (one bipolar CUTOFF: low-pass below the centre, THRU in the centre, high-pass above) with
  RESON, a pan, delay/reverb sends, a master-FX on/bypass and a sidechain send to the master compressor.
- Sample parts: attack/decay envelope (no hold, no sustain, each stage up to about 2 s), START/END, TUNE +-24 semis,
  REDUX (bit reduction) and DRIVE. Banks CH, OH, RS, CP, MISC (64), USER (128), EXP.
- MULTI: VPM (Korg's variable phase modulation, an FM-like engine), NOISE (filtered noise, 7 algorithms) or a user
  synth unit (logue SDK 2.0; the factory one is a commercial Sinevibes unit, which DrLogueXXL does not include).
- Effects: a send delay (Stereo, Mono, Tape, each also BPM-synced), a send reverb (Room, Hall, Space, Riser,
  Submarine) and a master effect (Compressor with sidechain, Filter, Boost, EQ Three), all extendable with units.
- 48 kHz internal sample rate; 11 voices, one per part (a user synth may be polyphonic).

## Part by part

### BD (analogue, + PCM transient)
Hardware: DECAY, TUNE, DRIVE on the panel; screen: TRANSIENT (16 attack samples), SWEEP (pitch sweep depth),
SW.TIME (sweep speed), HOLD 0-3 (time the level is held before the decay), ATTACK (transient level), CUTOFF, RESON.
Character: a fat, already-saturated sine body with a noise floor that Drive raises; the noise does not follow
Decay, so a short kick still leaves a short hiss tail (the filter can roll it off).
Model (src/parts/bd.cc): a sine core shaped by a diode-style waveshaper (always a little asymmetric), exponential
pitch envelope (depth SWEEP, rate SW.TIME) on top of TUNE, hold then exponential amplitude decay, a pre-drive
saturation stage that is always on, DRIVE into a tanh stage, a noise floor scaled by DRIVE with its own longer
envelope, a synthesised transient chosen from 16, then the part filter.

### SD (analogue, + snappy noise)
Hardware: DECAY, TUNE, SNAP.DEC; screen: SNAPPY TYPE (9), TONE (detune between the two heads), SNAP.LV, CUTOFF,
RESON. Entirely analogue on the hardware: a tuned two-tone body plus a noise layer.
Model (src/parts/sd.cc): two damped resonant tones (the two heads) a TONE-controlled interval apart with a short
pitch drop, plus a noise path whose colour/envelope follows SNAPPY TYPE (808, 909, white, pink, bright, rattle and
three lo-fi variants with bit and rate reduction), level SNAP.LV, length SNAP.DEC, then the part filter.

### LT / HT (analogue, + PCM layer)
Hardware: DECAY, TUNE, DETUNE; screen: LAYER (10), SWEEP, ATTACK (layer level), CUTOFF, RESON, DRIVE Off/Low/High.
Two detuned tones per voice; the layers add realism or blocks and zaps ("Slap" works as an alternative snare).
Model (src/parts/tom.cc): two resonant tones DETUNE apart with a pitch sweep, a synthesised layer from 10, a
three-step drive, then the part filter. LT and HT differ in pitch range.

### CH, OH, RS, CP, SP1, SP2 (samples)
Model (src/parts/pcm.cc + src/rom/): a sample player over a sample memory. The factory banks are synthesised when
the plugin opens (808/909/606/CR-78/metal-FM hats, rims, claps, and a MISC bank of percussion, cymbals, stabs,
plucks and vocal-ish blips); USER plays the user's own WAV files from the plugin's `Samples` folder.

### MULTI
VPM: a carrier sine phase-modulated by a modulator at RATIO (1/4 to 16x), depth INDEX, noise into the phase (NOISE),
EG AR/ASR/GATE with ATTACK/RELEASE, IDX MOD (envelope to index), 8 presets that change the modulator shape and
feedback (Default, Oct., Half, Fourth, Air, Creep, Throat, Harsh). NOISE: white noise through LPF/BPF/HPF 2- or
4-pole (COLOR = cutoff, PEAK = resonance) or DECIM (sample-rate reduction), with ATTACK/RELEASE.
Units: see src/units/registry.txt and src/VENDORED.md.

## Effects
Delay: Stereo / Mono / Tape (+ BPM versions) with FB, SPREAD, FILTER LP/HP + CUTOFF, SAT., ROUTE
(normal/crossed/ping-pong), Tape MOD/TRACKING/TONE, LEVEL, RETURN pre/post master, send to reverb.
Reverb: Room, Hall, Space, Riser (rising pitch), Submarine (diving), TIME, INPUT, BRIGHT, RISE, DIVE, LEVEL, RETURN.
Master: Compressor (THRESH, ATTACK, RELEASE, RATIO, MAKEUP, KNEE, SIDECH, WET), Filter (THRU, LPF/BPF/HPF 2/4,
SATURATE, RESON, CUTOFF), Boost (CENTER, WIDTH, SATURATE, BOOST), EQ Three (LOW, MID, HIGH, GAIN, two crossovers,
SLOPE). Community additions are listed in src/VENDORED.md. The output ends in a brickwall limiter.
