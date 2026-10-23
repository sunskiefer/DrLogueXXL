# DrLogueXXL v0.1.0 (sound engine; UI in progress)

**A drumlogue-style hybrid drum machine running natively inside MPC OS on the Akai Force.**

DrLogueXXL is a VST2 instrument for MPC OS's built-in plugin host, made by L'Cronx (shown on the device as
**DrLogueXXL** by **L'Cronx**). It follows the layout of Korg's drumlogue: 11 parts, a send delay, a send reverb and
a master effect with sidechain. It is not a port of Korg firmware: the analogue voices are circuit-style models written
here, the factory samples are synthesised, and the MULTI part hosts open-source logue SDK synth units.

> [!NOTE]
> **Status: 0.1.0, offline only.** The engine passes its tests on x86 (ASan + UBSan, mpc-vst-plugins host test) and
> on the armv7 build under QEMU. It has not run on a Force yet. The skin is a temporary auto-generated test layout;
> the real UI comes next.

## The parts

| Part | What it is | Controls |
| --- | --- | --- |
| **BD** | sine body through a diode-style shaper, pitch sweep, hold, drive with its own noise floor, 16 synthesised transients | Decay, Tune, Drive, Transient, Sweep, Sw.Time, Hold, Attack, Cutoff, Reson |
| **SD** | two tuned heads plus a snappy noise layer: 808, 909, White, Pink, Bright, Rattle, Lo-Fi 1-3 | Decay, Tune, Snap.Dec, Snappy, Tone, Snap.Lv, Cutoff, Reson |
| **LT / HT** | two detuned tones with a sweep, 10 layers (Stick, Skin, Slap, Block, Zap, Rim, Metal, Bloop, Shaker, Brush), drive Off/Low/High | Decay, Tune, Detune, Layer, Sweep, Attack, Cutoff, Reson, Drive |
| **CH OH RS CP SP1 SP2** | sample players: 192 synthesised factory sounds in CH, OH, RS, CP, MISC and EXP banks, plus a USER bank of your own WAVs | Bank, Sample, Tune +-24, Attack, Decay, Start, End (End before Start plays backwards), Cutoff, Reson, Redux, Drive |
| **MULTI** | VPM (Korg-style variable phase modulation, 8 presets), NOISE (7 filter algorithms) or a hosted unit: EffeESP32, Pluck, Waves, Elementish, Drupiter, Maxisynth | the engine's own controls, or up to 24 unit parameters named by the unit |

Every part has Level, Pan, (Spread), Delay and Reverb sends, Master On/Bypass, a Sidechain send, a Choke group (with
priority), Mute and a MIDI note. CUTOFF works like the drumlogue's: low-pass to the left, THRU in the middle,
high-pass to the right.

**Effects.** Delay: Stereo, Stereo BPM, Mono, Mono BPM, Tape, Tape BPM, Tribe, Cassette (BPM types follow the MPC
tempo). Reverb: Room, Hall, Space, Riser, Submarine, Clouds, Labyrinth, Halo. Master: Compressor (sidechain keyed by the
parts' sidechain sends), Filter, Boost, EQ Three, Omni. A brickwall limiter ends the chain.

**Playing it.** Note Map *Pads* (default): the 11 parts are notes Base Note (C1 = 36) and up, in the order above. *GM*:
General MIDI drum notes. *Custom*: each part's MIDI Note. With Multi Keys on, notes from the Key Split (C3) up play
the MULTI part chromatically. Velocity sets the level (Velocity sensitivity in the global page).

**Your samples.** Put WAV files (16/24/32-bit or float, any rate, mono or stereo, up to 4 s each, up to 128) in the
`Samples` folder inside the plugin folder; they appear in the USER bank. Projects remember them by name.

## Building

```
git clone --recurse-submodules https://github.com/sunskiefer/DrLogueXXL
cd DrLogueXXL && ./build.sh        # python3 + Pillow + cairosvg, gcc, Zig (pip install ziglang)
```

Output: `build/package/L'Cronx - VST - DrLogueXXL/`. Tests: `third_party/mpc-vst-plugins/tools/test_port.sh vst.json`
and the engine tests in `test/` (see each file's header).

## Credits and licences

DrLogueXXL is GPL-3.0-or-later. It includes code from Korg's logue SDK (BSD-3-Clause), cldmnky's drumlogue-units
(Elementish, Drupiter, Clouds reverb FX; Apache-2.0 / MIT, with Mutable Instruments code under MIT), copych's ESP32
FM drum engine (MIT), boochow's maxisynth and the Maximilian library (MIT). Details and every local change:
[src/VENDORED.md](src/VENDORED.md). "drumlogue" is a trademark of Korg Inc.; DrLogueXXL is not affiliated with Korg.
