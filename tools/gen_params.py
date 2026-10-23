#!/usr/bin/env python3
"""The single source of DrLogueXXL's parameter list: writes params.json (for mpc-vst-plugins' gen_vst.py) and
src/param_ids.h (P_<KEY> index constants, value lists and per-part blocks the engine reads).

    python3 tools/gen_params.py

MPC stores automation and saved values by parameter INDEX, so once a version has shipped this list is append-only:
never reorder, rename a key or remove a parameter. params.lock (written by gen_vst.py) enforces it.

Units: every value is in the units the drumlogue shows on its screen (0-1023 for the kit knobs, 0-100 % for the
screen parameters, semitones, dB, ms), so a sound from the hardware's manual reads the same here.
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

PARTS = [  # key, label, kind
    ("bd", "BD", "bd"), ("sd", "SD", "sd"), ("lt", "LT", "tom"), ("ht", "HT", "tom"),
    ("ch", "CH", "pcm"), ("oh", "OH", "pcm"), ("rs", "RS", "pcm"), ("cp", "CP", "pcm"),
    ("sp1", "SP1", "pcm"), ("sp2", "SP2", "pcm"), ("mt", "MULTI", "multi"),
]
OFF_ON = ["Off", "On"]
TRANSIENTS = ["Click", "Beater", "Felt", "Tick", "Snap", "Wood", "Pop", "Noise", "Air", "Zap", "Metal", "Thump",
              "Vinyl", "Bit", "Chirp", "Laser"]                       # BD SOUND SELECT, 16
SNAPPY = ["808", "909", "White", "Pink", "Bright", "Rattle", "Lo-Fi 1", "Lo-Fi 2", "Lo-Fi 3"]   # SD, 9
LAYERS = ["Stick", "Skin", "Slap", "Block", "Zap", "Rim", "Metal", "Bloop", "Shaker", "Brush"]   # LT/HT, 10
TOM_DRIVE = ["Off", "Low", "High"]
BANKS = ["CH", "OH", "RS", "CP", "MISC", "USER", "EXP"]
VPM_PRESETS = ["Default", "Oct.", "Half", "Fourth", "Air", "Creep", "Throat", "Harsh"]
VPM_RATIOS = ["1/4", "1/2", "3/4", "1x"] + ["%dx" % i for i in range(2, 17)]
VPM_EG = ["AR", "ASR", "GATE"]
NOISE_ALGOS = ["LPF 2", "LPF 4", "BPF 2", "BPF 4", "HPF 2", "HPF 4", "DECIM"]
# Synth engines of the MULTI part: the two built in, then the units of src/units/registry.txt (same order as
# src/units/registry.cc; test/units_test.cc checks they agree).
UNIT_NAMES = [l.strip() for l in open(os.path.join(ROOT, "src", "units", "registry.txt")) if l.strip()]
MT_ENGINES = ["VPM", "NOISE"] + UNIT_NAMES
NUM_UNIT_SLOTS = 24                                                   # a drumlogue unit has up to 24 parameters
DLY_TYPES = ["Stereo", "Stereo BPM", "Mono", "Mono BPM", "Tape", "Tape BPM", "Tribe", "Cassette"]
DLY_DIVS = ["1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "3/32", "1/8", "1/6", "3/16", "1/4", "1/3", "3/8",
            "1/2", "3/4", "1"]                                        # of a bar, as on the drumlogue
DLY_FILTERS = ["---", "LP", "HP"]
DLY_ROUTES = ["Normal", "Crossed", "P-Pong"]
PRE_POST = ["Pre", "Post"]
REV_TYPES = ["Room", "Hall", "Space", "Riser", "Submarine", "Clouds", "Labyrinth", "Halo"]
MFX_TYPES = ["Thru", "Compressor", "Filter", "Boost", "EQ Three", "Omni"]
MF_MODES = ["Thru", "LPF 2", "LPF 4", "BPF 2", "BPF 4", "HPF 2", "HPF 4"]
EQ_SLOPES = ["12dB", "24dB"]
CHOKE = ["Off", "1", "2", "3", "4"]
PRIO = ["Low", "Mid", "High"]
NOTE_MAPS = ["Pads", "GM", "Custom"]
MASTER = ["On", "Bypass"]

# default MIDI notes: "Pads" = base note + part number; "GM" = General MIDI drum notes; "Custom" = <part>_note
GM_NOTES = {"bd": 36, "sd": 38, "lt": 45, "ht": 50, "ch": 42, "oh": 46, "rs": 37, "cp": 39, "sp1": 49, "sp2": 56,
            "mt": 60}


def knob(key, name, lo, hi, default, **kw):
    p = {"key": key, "name": name, "min": float(lo), "max": float(hi), "default": float(default),
         "dynamic_display": True}
    p.update(kw)
    return p


def whole(key, name, lo, hi, default, **kw):
    # Whole numbers. A short range takes the wrapper's "int" stepping (one step per Q-Link event / wheel click). A long
    # one (notes, sample numbers, the 0-1023 kit knobs) stays continuous for the host: MPC adds 1/128 (Q-Link) or 1/100
    # (wheel) of the range to the value it reads back, and the engine rounds, so a 0-127 range moves one step per event.
    p = knob(key, name, lo, hi, default, **kw)
    if hi - lo < 20:
        p["display"] = "int"
    p["whole"] = True
    return p


def opts(key, name, options, default=0, **kw):
    p = {"key": key, "name": name, "options": list(options), "default": default}
    p.update(kw)
    return p


P = []
# ---- global ---------------------------------------------------------------------------------------------------
P += [
    knob("volume", "Volume", 0, 100, 70),
    opts("note_map", "Note Map", NOTE_MAPS, 0),
    whole("base_note", "Base Note", 0, 116, 36),
    knob("vel_sens", "Velocity", 0, 100, 70),
    opts("multi_keys", "Multi Keys", OFF_ON, 1),
    whole("key_split", "Key Split", 0, 127, 60),
    knob("lim_drive", "Lim Drive", 0, 12, 0),
    knob("lim_ceiling", "Lim Ceiling", -12, 0, -0.3),
]

PART_DEFAULTS = {  # sound defaults per part (only the keys that differ from the kind's defaults)
    "lt": {"tune": 400}, "ht": {"tune": 600},
    "ch": {"bank": 0, "decay": 300}, "oh": {"bank": 1, "decay": 600}, "rs": {"bank": 2, "decay": 400},
    "cp": {"bank": 3, "decay": 500}, "sp1": {"bank": 4, "sample": 1}, "sp2": {"bank": 4, "sample": 9},
}


def part_sound(k, kind):
    d = PART_DEFAULTS.get(k, {})
    g = lambda name, dflt: d.get(name, dflt)
    if kind == "bd":
        return [whole(k + "_decay", "Decay", 0, 1023, 512), whole(k + "_tune", "Tune", 0, 1023, 400),
                whole(k + "_drive", "Drive", 0, 1023, 200), opts(k + "_transient", "Transient", TRANSIENTS, 0),
                knob(k + "_sweep", "Sweep", 0, 100, 50), knob(k + "_swtime", "Sw.Time", 0, 100, 50),
                whole(k + "_hold", "Hold", 0, 3, 0), knob(k + "_attack", "Attack", 0, 100, 50),
                knob(k + "_cutoff", "Cutoff", -100, 100, 0), knob(k + "_reson", "Reson", 0, 100, 0)]
    if kind == "sd":
        return [whole(k + "_decay", "Decay", 0, 1023, 400), whole(k + "_tune", "Tune", 0, 1023, 500),
                whole(k + "_snapdec", "Snap.Dec", 0, 1023, 450), opts(k + "_snappy", "Snappy", SNAPPY, 0),
                knob(k + "_tone", "Tone", 0, 100, 50), knob(k + "_snaplv", "Snap.Lv", 0, 100, 70),
                knob(k + "_cutoff", "Cutoff", -100, 100, 0), knob(k + "_reson", "Reson", 0, 100, 0)]
    if kind == "tom":
        return [whole(k + "_decay", "Decay", 0, 1023, 500), whole(k + "_tune", "Tune", 0, 1023, g("tune", 500)),
                whole(k + "_detune", "Detune", 0, 1023, 300), opts(k + "_layer", "Layer", LAYERS, 0),
                knob(k + "_sweep", "Sweep", 0, 100, 40), knob(k + "_attack", "Attack", 0, 100, 40),
                knob(k + "_cutoff", "Cutoff", -100, 100, 0), knob(k + "_reson", "Reson", 0, 100, 0),
                opts(k + "_drive", "Drive", TOM_DRIVE, 0)]
    if kind == "pcm":
        return [opts(k + "_bank", "Bank", BANKS, g("bank", 4)),
                whole(k + "_sample", "Sample", 1, 128, g("sample", 1)),
                knob(k + "_tune", "Tune", -24, 24, 0), knob(k + "_attack", "Attack", 0, 100, 0),
                whole(k + "_decay", "Decay", 0, 1023, g("decay", 1023)),
                knob(k + "_start", "Start", 0, 100, 0), knob(k + "_end", "End", 0, 100, 100),
                knob(k + "_cutoff", "Cutoff", -100, 100, 0), knob(k + "_reson", "Reson", 0, 100, 0),
                knob(k + "_redux", "Redux", 0, 100, 0), knob(k + "_drive", "Drive", 0, 100, 0)]
    if kind == "multi":
        s = [opts(k + "_engine", "Engine", MT_ENGINES, 0, refresh_display=True),
             whole(k + "_unote", "Note", 0, 127, 60),
             opts("vpm_preset", "Preset", VPM_PRESETS, 0), whole("vpm_note", "Note", 0, 127, 48),
             knob("vpm_index", "Index", 0, 100, 40), opts("vpm_ratio", "Ratio", VPM_RATIOS, 4),
             knob("vpm_noise", "Noise", 0, 100, 0), opts("vpm_eg", "EG", VPM_EG, 0),
             knob("vpm_attack", "Attack", 0, 100, 0), knob("vpm_release", "Release", 0, 100, 40),
             knob("vpm_idxmod", "Idx Mod", -100, 100, 50),
             opts("noise_algo", "Algo", NOISE_ALGOS, 0), knob("noise_color", "Color", 0, 100, 60),
             knob("noise_peak", "Peak", 0, 100, 20), knob("noise_attack", "Attack", 0, 100, 0),
             knob("noise_release", "Release", 0, 100, 30)]
        # the selected unit's own parameters: names and value text come from the unit (dynamic)
        s += [knob("u_%d" % (i + 1), "Unit %d" % (i + 1), 0, 1, 0, dynamic_name=True) for i in range(NUM_UNIT_SLOTS)]
        return s
    raise ValueError(kind)


def part_mix(k, kind):
    m = [knob(k + "_level", "Level", 0, 100, 80), knob(k + "_pan", "Pan", -100, 100, 0)]
    if kind not in ("bd", "tom"):
        m.append(knob(k + "_spread", "Spread", -100, 100, 0))
    m += [knob(k + "_dly", "Delay", 0, 100, 0), knob(k + "_rev", "Reverb", 0, 100, 0),
          opts(k + "_master", "Master", MASTER, 0), knob(k + "_sidech", "Sidechain", 0, 100, 100 if k == "bd" else 0),
          opts(k + "_choke", "Choke", CHOKE, 1 if k in ("ch", "oh") else 0), opts(k + "_prio", "Priority", PRIO, 1),
          opts(k + "_mute", "Mute", OFF_ON, 0), whole(k + "_note", "MIDI Note", 0, 127, GM_NOTES[k])]
    return m


for k, label, kind in PARTS:
    P += part_sound(k, kind)
    P += part_mix(k, kind)

# ---- delay ----------------------------------------------------------------------------------------------------
P += [
    opts("dly_type", "Delay", DLY_TYPES, 1), knob("dly_time", "Time", 0, 100, 40),
    opts("dly_div", "Time", DLY_DIVS, 10), knob("dly_fb", "Feedback", 0, 100, 35),
    knob("dly_spread", "Spread", -100, 100, 0), opts("dly_filter", "Filter", DLY_FILTERS, 0),
    knob("dly_cutoff", "Cutoff", 0, 100, 50), knob("dly_sat", "Sat.", 0, 100, 0),
    opts("dly_route", "Route", DLY_ROUTES, 0), knob("dly_mod", "Mod", 0, 100, 20),
    knob("dly_tracking", "Tracking", 0, 100, 50), knob("dly_tone", "Tone", 0, 100, 50),
    knob("dly_level", "Level", 0, 100, 70), opts("dly_return", "Return", PRE_POST, 0),
    knob("dly_reverb", "To Reverb", 0, 100, 0),
]
# ---- reverb ---------------------------------------------------------------------------------------------------
P += [
    opts("rev_type", "Reverb", REV_TYPES, 1), knob("rev_time", "Time", 0, 100, 50),
    knob("rev_input", "Input", 0, 100, 50), knob("rev_bright", "Bright", 0, 100, 50),
    knob("rev_rise", "Rise", 0, 100, 50), knob("rev_dive", "Dive", 0, 100, 50),
    knob("rev_level", "Level", 0, 100, 70), opts("rev_return", "Return", PRE_POST, 0),
]
# ---- master effect --------------------------------------------------------------------------------------------
P += [
    opts("mfx_type", "Master FX", MFX_TYPES, 0, refresh_display=True),
    knob("comp_thresh", "Thresh", -64, 0, -12), knob("comp_attack", "Attack", 0.5, 100, 10),
    knob("comp_release", "Release", 50, 1000, 200), knob("comp_ratio", "Ratio", 1, 10, 4),
    knob("comp_makeup", "Makeup", 0, 8, 0), knob("comp_knee", "Knee", 0, 8, 2),
    opts("comp_sidech", "Sidechain", OFF_ON, 0), knob("comp_wet", "Wet", 0, 100, 100),
    opts("mf_mode", "Mode", MF_MODES, 1), knob("mf_saturate", "Saturate", 0, 100, 0),
    knob("mf_reson", "Reson", 0, 100, 20), knob("mf_cutoff", "Cutoff", 0, 100, 100),
    knob("boost_center", "Center", 0, 100, 30), knob("boost_width", "Width", 0, 100, 50),
    knob("boost_saturate", "Saturate", 0, 100, 0), knob("boost_gain", "Boost", 0, 12, 0),
    knob("eq_low", "Low", -64, 6, 0), knob("eq_mid", "Mid", -64, 6, 0), knob("eq_high", "High", -64, 6, 0),
    knob("eq_gain", "Gain", -6, 6, 0), knob("eq_lowmid", "Low/Mid", 0, 100, 30),
    knob("eq_midhigh", "Mid/High", 0, 100, 70), opts("eq_slope", "Slope", EQ_SLOPES, 0),
]
# the Omni master effect uses six generic slots, named by the effect
P += [knob("mfx_u%d" % (i + 1), "FX %d" % (i + 1), 0, 100, 50, dynamic_name=True) for i in range(6)]
# engine-side controls
P += [{"key": "panic", "name": "Panic", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True, "type": "trigger"},
      {"key": "rescan", "name": "Rescan", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True,
       "type": "trigger"}]

keys = [p["key"] for p in P]
assert len(keys) == len(set(keys)), "duplicate key"

SECTIONS = [("Global", keys[:8])]
for k, label, kind in PARTS:
    SECTIONS.append((label, [x for x in keys if x.startswith(k + "_") or
                             (kind == "multi" and x.split("_")[0] in ("vpm", "noise", "u"))]))
SECTIONS.append(("Delay", [x for x in keys if x.startswith("dly_")]))
SECTIONS.append(("Reverb", [x for x in keys if x.startswith("rev_")]))
SECTIONS.append(("Master FX", [x for x in keys if x.split("_")[0] in ("mfx", "comp", "mf", "boost", "eq")]))


def c_ident(s):
    return "".join(ch if ch.isalnum() else "_" for ch in s).upper()


def write():
    with open(os.path.join(ROOT, "params.json"), "w") as f:
        json.dump({"name": "DrLogueXXL", "params": P,
                   "sections": [{"label": l, "keys": ks} for l, ks in SECTIONS]}, f, indent=1)
        f.write("\n")
    h = ["// Generated by tools/gen_params.py -- do not edit.", "#pragma once", "",
         "namespace dlx {", "", "enum ParamId {"]
    for i, p in enumerate(P):
        h.append("  P_%s = %d," % (c_ident(p["key"]), i))
    h += ["  P_COUNT = %d" % len(P), "};", ""]
    h.append("// key, min, max, default, option count (0 = continuous), whole number")
    h.append("struct ParamInfo { const char* key; float min, max, def; int num_opts; bool whole; "
             "const char* const* opts; };")
    lists = {}
    for p in P:
        if "options" in p:
            name = "kOpts_" + c_ident(p["key"])
            lists[p["key"]] = name
            h.append("static const char* const %s[] = {%s};" % (name, ", ".join(json.dumps(o) for o in p["options"])))
    h.append("static const ParamInfo kParams[P_COUNT] = {")
    for p in P:
        if "options" in p:
            n = len(p["options"])
            h.append('  {"%s", 0, %d, %d, %d, true, %s},' % (p["key"], n - 1, p.get("default", 0), n, lists[p["key"]]))
        else:
            h.append('  {"%s", %r, %r, %r, 0, %s, nullptr},' % (
                p["key"], p["min"], p["max"], p.get("default", p["min"]), "true" if p.get("whole") else "false"))
    h.append("};")
    h.append("")
    # per-part blocks: the offset of each mix/sound key relative to the part's first sound key is the same for
    # parts of one kind, so the engine can address part n's "decay" as kPartBase[n] + offset.
    h.append("enum PartIndex { %s, NUM_PARTS };" % ", ".join("PART_%s" % c_ident(k) for k, _, _ in PARTS))
    h.append('static const char* const kPartNames[NUM_PARTS] = {%s};' % ", ".join('"%s"' % l for _, l, _ in PARTS))
    h.append('static const char* const kPartKeys[NUM_PARTS] = {%s};' % ", ".join('"%s"' % k for k, _, _ in PARTS))
    h.append("static const int kGmNotes[NUM_PARTS] = {%s};" % ", ".join(str(GM_NOTES[k]) for k, _, _ in PARTS))
    h.append("static const int kNumUnitSlots = %d;" % NUM_UNIT_SLOTS)
    h.append("static const int kNumBuiltinEngines = 2;   // VPM, NOISE; units follow")
    h.append("static const int kNumUnits = %d;" % len(UNIT_NAMES))
    h += ["", "}  // namespace dlx", ""]
    with open(os.path.join(ROOT, "src", "param_ids.h"), "w") as f:
        f.write("\n".join(h))


if __name__ == "__main__":
    write()
    print("params.json: %d parameters" % len(P))
