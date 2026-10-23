# DrLogueXXL user guide

DrLogueXXL 0.2.0, by L'Cronx. A drumlogue-style hybrid drum machine for MPC OS (Akai Force and other first-generation
MPC OS units). This guide covers every page, part, effect and control.

- [Install](#install)
- [Playing it](#playing-it)
- [PERFORM page](#perform-page)
- [MIXER page](#mixer-page)
- [SCREEN page](#screen-page)
- [The parts](#the-parts)
- [The effects](#the-effects)
- [GLOBAL](#global)
- [Q-Links](#q-links)
- [Your own samples](#your-own-samples)
- [Saving](#saving)
- [Troubleshooting](#troubleshooting)

## Install

Needs root SSH access to the device (for example MockbaMod) and MPC OS 3.x. Download
`DrLogueXXL-<version>-mpc-armv7.zip` from the [releases](../../../releases), unzip it, then:

```
scp -r DrLogueXXL-<version> root@<device-ip>:/tmp/
ssh -t root@<device-ip> sh /tmp/DrLogueXXL-<version>/install.sh
```

The installer stops MPC (save first), copies the plugin to `/sdcard/Synths/L'Cronx - VST - DrLogueXXL/`, backs up
and edits `MPC.settings`, and starts MPC again. Running it again upgrades in place. `uninstall.sh` removes it.
`INSTALL.md` in the zip has the manual steps.

Load **DrLogueXXL** (manufacturer L'Cronx) as a plugin instrument on a track. The factory sample banks are made when
the plugin first opens: the hats, rims and claps are ready in well under a second, the rest within a few seconds.

## Playing it

DrLogueXXL plays from MIDI notes: the Force's pads, its sequencer, or a keyboard. It has no step sequencer of its own.

**Note Map** (GLOBAL page 1) decides which note plays which part:

| Part | Pads (default, Base Note C1 = 36) | GM | Custom |
| --- | --- | --- | --- |
| BD | 36 | 36 | each part's MIDI Note |
| SD | 37 | 38 | |
| LT | 38 | 45 | |
| HT | 39 | 50 | |
| CH | 40 | 42 | |
| OH | 41 | 46 | |
| RS | 42 | 37 | |
| CP | 43 | 39 | |
| SP 1 | 44 | 49 | |
| SP 2 | 45 | 56 | |
| MULTI | 46 | 60 | |

*Pads* puts the 11 parts on 11 neighbouring notes from **Base Note**, so a pad bank plays the whole kit. *GM* follows
General MIDI drum notes, for MIDI files and other drum programs. *Custom* uses each part's own **MIDI Note** (on its
screen pages).

**Multi Keys** (on by default): every note from **Key Split** (C3 = 60) up plays the MULTI part chromatically, at that
pitch, held as long as the note. Below the split the note map applies.

**Velocity** sets the level of each hit; the **Velocity** amount (GLOBAL) sets how much: 0 % plays every hit at full
level, 100 % follows velocity fully. A hard hit also drives the analogue parts a little harder, like an accent.

**Choke groups**: CH and OH are in group 1, so a closed hat cuts the open one. Any part can join group 1-4; a part
cuts the other parts in its group whose **Priority** is the same or lower.

## PERFORM page

![PERFORM](img/perform.png)

Laid out like the machine.

- **Kit knobs** (left), three per section, as on the hardware:
  - BASS DRUM: DECAY, TUNE, DRIVE
  - SNARE DRUM: DECAY, TUNE, SNAPPY (the snappy decay)
  - TOMS: LT DECAY, LT TUNE, LT DETUNE; with **SHIFT** on: HT DECAY, HT TUNE, HT DETUNE (the grey labels)
  - HIGH HATS: CH DECAY, OH DECAY; CLAP: DECAY
  - SAMPLE 1/2: SP 1 ATTACK, SP 1 DECAY, SP 1 TUNE; with **SHIFT** on: the same for SP 2

  Turning a kit knob shows its part, name and value on the screen's title for a moment (the machine's parameter
  pop-up).
- **The screen** shows the selected part or effect: its name on top, the page (for example `2/5`) on the right, and
  four boxes, one per knob below it.
- **PARAM 1-4**: the four knobs under the screen edit the four boxes. On option lists (Transient, Snappy, Bank, effect
  types...) a Q-Link click or a wheel click moves one step; a long turn jumps.
- **PAGE ▲ ▼** go through the selection's pages. Each part has its sound pages first, then its mix pages (Level, Pan,
  Delay, Reverb; Master, Sidechain, Choke, Priority; and Spread where the part has one).
- **VOLUME**: the output level.
- **SHIFT**: switches the TOMS and SAMPLE 1/2 knobs to HT and SP 2.
- **GLOBAL**: selects the global settings on the screen ([GLOBAL](#global)).
- **The 16 keys** select what the screen shows: BD, SD, LT, HT, CH, OH, RS, CP, SP 1, SP 2 (the parts), VPM, NOISE,
  USER (the MULTI part's sources), DELAY, REVERB, MASTER (the effects). The lit LED shows the selection. As with
  PART + VPM / NOISE / USER on the machine, picking **VPM** or **NOISE** makes the MULTI part play that source, and
  **USER** goes back to the last unit you used.

## MIXER page

![MIXER](img/mixer.png)

Eleven strips (BD to MULTI), each with **PAN**, **DELAY** and **REVERB** (the send amounts), the **level fader** and
**MUTE** (its LED lit while muted; a muted part ignores its notes and stops at once). **MASTER**: the limiter's
**DRIVE** (push into it, 0 to 12 dB) and **CEILING** (its maximum, -12 to 0 dB), the **VOLUME** fader, and **PANIC**
(stops every sound and clears the effects' tails).

## SCREEN page

![SCREEN](img/screen.png)

The screen, big: a wave picture of the selection (the MULTI sources and VPM presets each have their own) over its four
parameter boxes, which the Q-Links of this page also turn. Below it, four **macros** that act on the whole kit:

| Macro | Range | What it does |
| --- | --- | --- |
| **DECAY** | -100 to +100 | every part shorter (left) or longer (right): analogue decays, the snappy decay, the sample parts' decay |
| **TUNE** | -100 to +100 | every part lower or higher: the analogue tunes, the sample parts by up to an octave |
| **FILTER** | -100 to +100 | every part's cutoff toward low-pass (left) or high-pass (right) |
| **SPACE** | 0 to 100 | more delay and reverb send on every part |

Macros are offsets over what the parts play: the saved values stay as they are, so turning a macro back to 0 brings
the kit back exactly.

## The parts

### BD (analogue model + transient)
| Control | Range | |
| --- | --- | --- |
| DECAY | 0-1023 | about 40 ms to 4 s |
| TUNE | 0-1023 | 30 to 120 Hz |
| DRIVE | 0-1023 | saturation; it also raises a noise floor with its own short envelope (as on the machine, a short kick keeps a little hiss tail) |
| TRANSIENT | 16 | Click, Beater, Felt, Tick, Snap, Wood, Pop, Noise, Air, Zap, Metal, Thump, Vinyl, Bit, Chirp, Laser |
| SWEEP | 0-100 % | depth of the pitch drop (up to 3.5 octaves) |
| SW.TIME | 0-100 % | speed of the drop |
| HOLD | 0-3 | 0, 10, 25 or 50 ms at full level before the decay |
| ATTACK | 0-100 % | transient level |
| CUTOFF / RESON | | the part filter (below) |

The body is a sine through a slightly asymmetric diode-style shaper, so it is already saturated before DRIVE.

### SD (analogue model + snappy)
| Control | Range | |
| --- | --- | --- |
| DECAY | 0-1023 | the body |
| TUNE | 0-1023 | 120 to 400 Hz |
| SNAP.DEC | 0-1023 | snappy length, 30 ms to 1.5 s |
| SNAPPY | 9 | 808, 909, White, Pink, Bright, Rattle (snare-wire bursts), Lo-Fi 1-3 (the whole snare crushed, more each step) |
| TONE | 0-100 % | the interval between the two heads |
| SNAP.LV | 0-100 % | snappy level |

### LT / HT (analogue model + layer)
| Control | Range | |
| --- | --- | --- |
| DECAY | 0-1023 | 50 ms to 3 s |
| TUNE | 0-1023 | LT 55-220 Hz, HT 100-400 Hz |
| DETUNE | 0-1023 | the second tone up to 7 semitones above |
| LAYER | 10 | Stick, Skin, Slap (works as a second snare), Block, Zap, Rim, Metal, Bloop, Shaker, Brush |
| SWEEP | 0-100 % | the pitch drop |
| ATTACK | 0-100 % | layer level |
| DRIVE | Off / Low / High | |

### CH, OH, RS, CP, SP 1, SP 2 (samples)
| Control | Range | |
| --- | --- | --- |
| BANK | CH, OH, RS, CP, MISC, USER, EXP | |
| SAMPLE | 1-128 | the screen shows its name |
| TUNE | -24 to +24 semitones | |
| ATTACK | 0-100 % | up to about 2 s |
| DECAY | 0-1023 | 10 ms to 2 s; 1023 plays to the end |
| START / END | 0-100 % | END before START plays backwards |
| REDUX | 0-100 % | bit reduction (down to about 2 bits) |
| DRIVE | 0-100 % | |

The banks (made by DrLogueXXL, not Korg's samples): **CH** 16 and **OH** 16 hats (808, 909, 606, CR-78, metal, FM,
noise...), **RS** 16 rims, woods and claves, **CP** 16 claps, **MISC** 64 (cowbells, congas, bongos, shakers,
tambourines, rides, crashes, kicks, snares, toms, plucks, chord stabs, bass hits, vocal blips, zaps, risers, glitches),
**EXP** 64 vintage-machine sets (CR-78, Linn-style 8-bit, SP-style 12-bit, 8-bit, circuit-bent, FM, tape), and
**USER**, your own files.

### The part filter
CUTOFF works like the machine's: left of the middle is a low-pass (`L0.0%` to `L99.5%`), the middle is `THRU`, right
of it a high-pass (`H0.5%` to `H100%`). RESON adds resonance.

### MULTI
The 11th part plays one source:

| Source | Controls |
| --- | --- |
| **VPM** (variable phase modulation, an FM-style voice) | PRESET (Default, Oct., Half, Fourth, Air, Creep, Throat, Harsh), NOTE, INDEX, RATIO (1/4 to 16x), NOISE, EG (AR / ASR / GATE), ATTACK, RELEASE, IDX MOD |
| **NOISE** | ALGO (LPF 2/4, BPF 2/4, HPF 2/4, DECIM), COLOR, PEAK, ATTACK, RELEASE |
| **EffeESP32** | a 6-operator FM drum synth: 59 instruments (kicks, snares, toms, hats, cymbals, percussion), its 24 controls (algorithm, envelopes, filter, operator levels...) |
| **Pluck** | Korg's Karplus-Strong string |
| **Waves** | Korg's dual-wavetable oscillator (with tables made for DrLogueXXL), sub, ring and bit crush |
| **Elementish** | a modal voice (bow, blow, strike), after Mutable Instruments Elements |
| **Drupiter** | a Jupiter-8 style poly / mono synth: two DCOs, filter, envelopes, LFO |
| **Maxisynth** | a two-oscillator synth with a filter, on the Maximilian library |

On the screen, USER's first page has the unit (ENGINE) and the NOTE a pad hit plays; then the unit's own parameters,
named as the unit names them. Changing the unit resets its parameters to the unit's own defaults.

## The effects

Every part has a **Delay** and a **Reverb** send, **Master** (On: through the master effect; Bypass: around it) and a
**Sidechain** send (to the compressor's detector).

**DELAY** (select DELAY): TYPE Stereo, Stereo BPM, Mono, Mono BPM, Tape, Tape BPM (the BPM types follow the MPC
tempo, 1/64 to 1 bar), Tribe (a rhythmic multi-tap spreading echoes across the stereo field), Cassette (a worn
portastudio echo). TIME, FEEDBACK, LEVEL; FILTER (---, LP, HP) and CUTOFF in the loop, SAT., SPREAD; ROUTE (Normal,
Crossed, Ping-Pong), MOD, TRACKING and TONE (tape types); RETURN (Pre: into the master effect, Post: after it) and TO
REVERB.

**REVERB**: TYPE Room, Hall, Space, Riser (the tail rises in pitch), Submarine (it dives), Clouds (Mutable
Instruments' Clouds reverb, with shimmer on RISE and an octave down on DIVE), Labyrinth (two banks bouncing across the
stereo field, a resonant colour), Halo (a clean tail with an octave-down halo and sparkling grains). TIME, INPUT,
BRIGHT, RISE, DIVE, LEVEL, RETURN.

**MASTER**: TYPE Thru; Compressor (THRESH, RATIO, ATTACK, RELEASE, MAKEUP, KNEE, SIDECHAIN: when on, the parts'
sidechain sends key it, the classic kick ducking; WET for parallel compression); Filter (MODE LPF/BPF/HPF 2 or 4 pole,
CUTOFF, RESON, SATURATE); Boost (CENTER, WIDTH, BOOST, SATURATE); EQ Three (LOW, MID, HIGH down to a kill, GAIN, the
two crossovers, SLOPE 12 or 24 dB); Omni (a function compressor: THRESH, FUNCTION from expansion through compression
and limiting to reverse, ATTACK, RELEASE, DRIVE, MIX).

The output ends in a brickwall limiter (MIXER page, MASTER).

## GLOBAL

Press **GLOBAL** (PERFORM page, right column). Page 1: VOLUME, VELOCITY, NOTE MAP, BASE NOTE. Page 2: MULTI KEYS,
KEY SPLIT, LIM DRIVE, LIM CEILING. Page 3: RESCAN (reads the Samples folder again) and PANIC.

## Q-Links

| Page | Sub-page | Q-Links |
| --- | --- | --- |
| PERFORM | KIT | the 15 kit knobs in reading order (top row first), then VOLUME |
| PERFORM | SCREEN | PARAM 1-4, the selection, SHIFT, then the HT and SP 2 knobs |
| MIXER | LEVEL | the 11 faders, VOLUME, limiter DRIVE and CEILING |
| MIXER | PAN / DELAY / REVERB / MUTE | the 11 strips' control of that row |
| SCREEN | MACROS | DECAY, TUNE, FILTER, SPACE, then PARAM 1-4 |

CC 20-35 on the track's MIDI input move the first page's Q-Links.

## Your own samples

Put WAV files in the `Samples` folder inside the plugin's folder (`/sdcard/Synths/L'Cronx - VST - DrLogueXXL/Samples/`;
sub-folders are read too, three levels deep). 16, 24 or 32-bit, or float, any sample rate, mono or stereo; up to 128
files, 4 s each (longer ones are cut), 32 MB in all. They make up the **USER** bank, sorted by folder and name. After
copying files, press **RESCAN** (GLOBAL page 3) or reload the plugin.

## Saving

Everything is saved with the MPC project. Samples from the USER bank are saved by name, so adding files to the folder
later doesn't change which sample a part plays; a file that is gone leaves that part on its current number.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| A sample part is silent right after loading | The banks are still being made (a few seconds the first time). |
| USER plays nothing | No WAVs in `Samples`, or RESCAN not pressed yet after copying them. |
| A note plays nothing | Check the Note Map (GLOBAL page 1) and the part's MUTE (MIXER). |
| Everything sounds squashed | The limiter: lower LIM DRIVE, or turn the part levels down. |
| A hanging MULTI note | PANIC (MIXER page or GLOBAL page 3). |
| The CPU meter is high | Heavy MULTI units (Elementish, Drupiter, Maxisynth) and the bigger reverbs cost the most; try VPM, a lighter reverb type, or fewer instances. |
