#!/usr/bin/env python3
"""DrLogueXXL's skin: three pages built from the panel look of tools/sketch.py (the user's photos of the machine:
black brushed panel, ash wood cheeks, grey caps, square part keys with red LEDs, the screen enlarged; the mixer faders
from LXRXXL ZYNTH).

    python3 tools/ui.py --write                    writes layout.conf (tabs + Q-Links for mpc-vst-plugins' gen_vst.py)
    python3 tools/ui.py "<skin>/Plugin Skins"      builds every control of the built skin (after gen_vst.py)

Pages:
  PERFORM  the kit knobs (five sections as on the machine; SHIFT switches TOMS to HT and SAMPLE 1/2 to SP 2), the
           screen with the selected part's page (title, page, four parameter boxes) and its four knobs, VOLUME, PAGE
           up/down, SHIFT, GLOBAL, and the 16 part / effect keys with their LEDs (the selection).
  MIXER    11 strips (PAN, DELAY, REVERB, the ZYNTH fader, MUTE with its LED) and MASTER (VOLUME fader, limiter
           DRIVE and CEILING, PANIC).
  SCREEN   the screen, big: the wave picture of the selection (the "scope" parameter) over its four parameter boxes,
           and the four macros (DECAY, TUNE, FILTER, SPACE).
MPC draws the live text (screen title, page, parameter names and values) in Titillium Web, the only face it renders;
everything printed on the panel is Barlow Semi Condensed, baked into the pictures.
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sketch as K  # noqa: E402  (the drawing primitives, at K.S supersampling)

ROOT = K.ROOT
W, H = K.W, K.H
Y0 = 86                       # Force Shadow y offset of layout.conf coordinates
TABS = ["PERFORM", "MIXER", "SCREEN"]
KNOB_FRAMES = 128
KNOB_SPAN = 150

# ---------------------------------------------------------------------------------------------- geometry (page px)
KIT_X0, KIT_CW = 40, 117
KIT_ROWS = [108, 224, 340]
KIT = [  # column: (title, [(key, label), ...], [(shifted key, label), ...] or None)
    ("BASS DRUM", [("bd_decay", "DECAY"), ("bd_tune", "TUNE"), ("bd_drive", "DRIVE")], None),
    ("SNARE DRUM", [("sd_decay", "DECAY"), ("sd_tune", "TUNE"), ("sd_snapdec", "SNAPPY")], None),
    ("TOMS", [("lt_decay", "LT DECAY"), ("lt_tune", "LT TUNE"), ("lt_detune", "LT DETUNE")],
     [("ht_decay", "HT DECAY"), ("ht_tune", "HT TUNE"), ("ht_detune", "HT DETUNE")]),
    ("HIGH HATS", [("ch_decay", "CH DECAY"), ("oh_decay", "OH DECAY"), ("cp_decay", "DECAY")], None),
    ("SAMPLE 1/2", [("sp1_attack", "SP 1 ATTACK"), ("sp1_decay", "SP 1 DECAY"), ("sp1_tune", "SP 1 TUNE")],
     [("sp2_attack", "SP 2 ATTACK"), ("sp2_decay", "SP 2 DECAY"), ("sp2_tune", "SP 2 TUNE")]),
]
KIT_R = 30
SCREEN1 = (642, 108, 440, 190)            # x, y, w, h of the window
ENC_Y, ENC_R, ENC_X0, ENC_DX = 360, 36, 668, 128
VOL = (1200, 82, 30)
PAGE_UP, PAGE_DN = (1200, 186), (1200, 244)
SHIFT_KEY, GLOBAL_KEY = (1200, 304), (1200, 360)
KEYS_Y, KEYS_X0, KEYS_DX = 560, 64, 74.4
SEL16 = ["BD", "SD", "LT", "HT", "CH", "OH", "RS", "CP", "SP 1", "SP 2", "VPM", "NOISE", "USER", "DELAY", "REVERB",
         "MASTER"]
NUM_SEL = 17                               # + GLOBAL

MIX_NAMES = ["BD", "SD", "LT", "HT", "CH", "OH", "RS", "CP", "SP 1", "SP 2", "MULTI"]
MIX_KEYS = ["bd", "sd", "lt", "ht", "ch", "oh", "rs", "cp", "sp1", "sp2", "mt"]
MIX_X0, MIX_DX = 58, 94
MIX_KNOBS = [("pan", "PAN", 92), ("dly", "DELAY", 172), ("rev", "REVERB", 252)]
MIX_KR = 18
FADER_Y, FADER_H, FADER_W = 330, 210, 34
MUTE_Y = 572
MASTER_X = MIX_X0 + MIX_DX * 11 + 6

SCREEN3 = (92, 100, 1096, 330)
MACRO_Y, MACRO_R = 520, 42
MACROS = [("macro1", "DECAY"), ("macro2", "TUNE"), ("macro3", "FILTER"), ("macro4", "SPACE")]


def kit_pos(c, r):
    return KIT_X0 + KIT_CW * c + KIT_CW / 2, KIT_ROWS[r]


def keys_x(i):
    return KEYS_X0 + KEYS_DX * i


# ---------------------------------------------------------------------------------------------- Q-Links
def qlink_pages(tab):
    """(sub-page name, keys) per Q-Link sub-page of a tab, in reading order"""
    if tab == 0:
        kit = [KIT[c][1][r][0] for r in range(3) for c in range(5)] + ["volume"]
        shifted = [k for c in (2, 4) for k, _ in KIT[c][2]]
        return [("KIT", kit), ("SCREEN", ["p1", "p2", "p3", "p4", "sel", "shift"] + shifted)]
    if tab == 1:
        return [("LEVEL", [k + "_level" for k in MIX_KEYS] + ["volume", "lim_drive", "lim_ceiling"]),
                ("PAN", [k + "_pan" for k in MIX_KEYS]), ("DELAY", [k + "_dly" for k in MIX_KEYS]),
                ("REVERB", [k + "_rev" for k in MIX_KEYS]), ("MUTE", [k + "_mute" for k in MIX_KEYS])]
    return [("MACROS", [k for k, _ in MACROS] + ["p1", "p2", "p3", "p4"])]


def page_keys(tab):
    """every parameter a page shows (for layout.conf: gen_vst.py makes the tabs, Q-Links and parameter table)"""
    keys = []
    for _, ks in qlink_pages(tab):
        for k in ks:
            if k not in keys:
                keys.append(k)
    extra = {0: ["page_up", "page_down", "lcd_title", "lcd_page"], 1: ["panic"], 2: ["lcd_title", "lcd_page", "scope"]}
    return keys + [k for k in extra[tab] if k not in keys]


def write_layout(path):
    out = ["# DrLogueXXL -- page layout (mpc-vst-plugins layout.conf syntax). GENERATED by tools/ui.py --write: edit that.",
           "# Only the tabs and their Q-Links matter here; tools/ui.py then builds every control of the three pages.",
           "theme_bg=1b191a", "theme_panel=1b191a", "theme_ink=dedbdc", "theme_ink_dim=8a8588", "theme_accent=ec4658",
           "theme_knob_ring=ec4658", "theme_seg_active=ec4658", ""]
    for tab, name in enumerate(TABS):
        out.append("[tab %s]" % name)
        for i, k in enumerate(page_keys(tab)):
            out.append('knob cx=%d cy=%d r=12 label="" key=%s ns=0 bw=30' % (24 + (i % 36) * 34, Y0 + 24 + (i // 36) * 34, k))
        for sname, ks in qlink_pages(tab):
            out.append('qlinks "%s %s" = %s' % (name, sname, ",".join(ks)))
        out.append("")
    open(path, "w").write("\n".join(out))


# ---------------------------------------------------------------------------------------------- TUI.json pieces
def R(v):
    return int(round(v))


def bounds_s(x, y, w, h):
    return "%d %d %d %d" % (R(x), R(y), R(w), R(h))


def sub(name, typ, data, b, visible="Always"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": visible,
                       "boundsType": "Absolute", "bounds": b, "additionalInvalidatingHandles": []}}


def focus(w, h):
    return sub("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": "00000000",
                                  "backgroundInset": 2.0, "outlineThickness": 0.0}, bounds_s(0, 0, w, h), "WhenFocussed")


def action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": extra, "handle remapping": {"version": 1, "map": []}}


KNOB_ACTIONS = [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
                action("Enter Pressed", "Show Overlay", "knob overlay")]
KEY_ACTIONS = [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
RADIO_ACTIONS = [action("Mouse Down", "Q-Link")]
TRIG_ACTIONS = [action("Mouse Down", "Toggle Switch"), action("Enter Pressed", "Toggle Switch")]
TRIGGERS = ("page_up", "page_down", "panic")


def definition(key, actions, parts, ignore=False):
    return {"key": key, "value": {
        "version": 4, "actions": actions,
        "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
                           "unfocussed": {"version": 1, "colour": "0", "image": ""}},
        "ignoreMousePresses": ignore, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True,
        "componentsData": parts}}


def filmstrip(name, img, frames, w, h, drag="Vertical", x=0, y=0):
    return sub(name, "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": frames,
                              "invert": False, "dragOrientation": drag, "handleName": "Data"}, bounds_s(x, y, w, h))


def button(on_img, off_img, bid, n, w, h):
    return sub("Button", "Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": bid,
                                    "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"},
               bounds_s(0, 0, w, h))


def text_label(kind, w, h, px, colour="ffffff", just="horizontallyCentred verticallyCentred"):
    """MPC's own text, kind "Value" (the parameter's value) or "Name" (its name)"""
    return sub(kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
                                                                                "style": "SemiBold", "height": px},
                                                          "colour": "ff" + colour, "justification": just,
                                                          "case": "Original"},
                               "type": kind, "handleName": "Data"}, bounds_s(0, 0, w, h))


def placed(dkey, name, index, rect, focus_on=True, when=None):
    x, y, w, h = rect
    c = {"version": 2,
         "componentData": {"version": 1, "name": name, "type": dkey, "data": {"version": 1, "handleName": "Data"}},
         "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % index}]},
         "bounds": {"version": 2, "acceptsHWFocus": "Yes" if focus_on else "No", "showWhenDataModelInvalid": "Hide",
                    "whenVisible": "Always", "boundsType": "Absolute", "bounds": bounds_s(x, y, w, h),
                    "additionalInvalidatingHandles": []}}
    if when:   # (param index, option, count): shown only while that param is at that option
        c["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/%d/%d/Parameter %d" % (when[1], when[2], when[0])]
    return c


def picture(img, rect, index, option, count):
    c = sub("Picture", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": img}, bounds_s(*rect))
    c["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/%d/%d/Parameter %d" % (option, count, index)]
    return c


class Skin:
    def __init__(self, skin, params):
        self.skin = skin
        self.index = {p["key"]: i for i, p in enumerate(params)}
        self.params = params
        self.defs = {}
        self.used = set()

    def save(self, img, name):
        img.save(os.path.join(self.skin, name))
        self.used.add(name)
        return name

    def define(self, key, actions, parts, ignore=False):
        if key not in self.defs:
            self.defs[key] = definition(key, actions, parts, ignore)
        return key

    def nopts(self, key):
        return len(self.params[self.index[key]].get("options") or [])


# ---------------------------------------------------------------------------------------------- pictures
def down(img):
    return img.resize((img.width // K.S, img.height // K.S), Image.LANCZOS)


def knob_strip(sk, kind, r, pad=8):
    """a transparent filmstrip of a knob turning -150..+150 degrees (128 frames, numFrames 127)"""
    name = "dl_knob_%s_%d.png" % (kind, r)
    side = 2 * (r + pad)
    if name in sk.used:
        return name, side
    frames = 2 if kind == "enc" else KNOB_FRAMES
    strip = Image.new("RGBA", (side, side * frames), (0, 0, 0, 0))
    for k in range(frames):
        ang = -KNOB_SPAN + 2 * KNOB_SPAN * k / (KNOB_FRAMES - 1)
        c = Image.new("RGBA", (side * K.S, side * K.S), (0, 0, 0, 0))
        if kind == "mini":
            K.small_knob(c, r + pad, r + pad, r, ang)
        elif kind == "enc":
            K.encoder(c, r + pad, r + pad, r)
        else:
            K.knob(c, r + pad, r + pad, r, ang)
        strip.paste(down(c), (0, k * side))
    assert strip.height <= 16128, name
    sk.save(strip, name)
    return name, side


def crop_layer(bg, rect):
    x, y, w, h = rect
    return bg.crop((R(x), R(y), R(x + w), R(y + h))).convert("RGBA")


def overlay(bg, rect, draw):
    """bg's crop under rect with draw(canvas, ox, oy) painted on it (canvas at K.S, page coords offset by ox, oy)"""
    x, y, w, h = rect
    base = crop_layer(bg, rect)
    c = Image.new("RGBA", (R(w) * K.S, R(h) * K.S), (0, 0, 0, 0))
    draw(c, -x, -y)
    base.alpha_composite(down(c))
    return base.convert("RGB")


def draw_key(c, ox, oy, cx, cy, w, h, label, fsize, lit_led=None, led_dy=-46, num=None):
    if lit_led is not None:
        if num is not None:
            K.text(c, cx + ox, cy + oy - 66, num, 15, K.INK_DIM, "Medium")
        K.led(c, cx + ox, cy + oy + led_dy, lit_led)
    K.button(c, cx + ox, cy + oy, w, h, label, fsize=fsize)


def scope_picture(i, w, h):
    """the wave screen's picture for SCOPES[i] (tools/gen_params.py): white pixels on the screen's black"""
    img = Image.new("RGB", (w * K.S, h * K.S), (4, 4, 7))
    d = ImageDraw.Draw(img)
    mid = h / 2
    d.line([(K.P(10), K.P(mid)), (K.P(w - 10), K.P(mid))], fill=(70, 72, 80), width=K.S)
    n = w - 40
    pts = []
    import random
    rnd = random.Random(i)
    for k in range(n):
        t = k / n
        if i in (0, 2, 3):                       # BD, toms: a decaying sine, falling pitch
            f = {0: 3.0, 2: 6.0, 3: 9.0}[i]
            ph = 2 * math.pi * f * (t + 0.6 * (1 - math.exp(-t * 6)) / 6 * 6)
            v = math.exp(-t * 3.2) * math.sin(ph)
        elif i == 1:                             # SD: body + noise
            v = math.exp(-t * 4) * (0.6 * math.sin(2 * math.pi * 12 * t) + 0.5 * rnd.uniform(-1, 1))
        elif i in (4, 5):                        # hats: noise bursts
            v = math.exp(-t * (14 if i == 4 else 3)) * rnd.uniform(-1, 1)
        elif i == 6:                             # RS: a short ring
            v = math.exp(-t * 18) * math.sin(2 * math.pi * 40 * t)
        elif i == 7:                             # CP: three bursts and a tail
            env = sum(math.exp(-((t - c) * 60) ** 2) for c in (0.04, 0.1, 0.16)) + 0.6 * math.exp(-max(0, t - 0.2) * 5) * (t > 0.2)
            v = env * rnd.uniform(-1, 1)
        elif i in (8, 9):                        # SP: a sample's waveform
            v = math.exp(-t * 2.2) * (0.6 * math.sin(2 * math.pi * 7 * t) + 0.35 * math.sin(2 * math.pi * 19 * t + 1)) + 0.15 * rnd.uniform(-1, 1) * math.exp(-t * 5)
        elif 10 <= i < 18:                       # VPM presets: two cycles of a phase-modulated sine
            p = i - 10
            ratio = [1, 2, 0.5, 1.333, 3, 1.01, 5, 7][p]
            idx = [1.2, 1.8, 1.4, 1.6, 2.4, 1.0, 2.8, 4.0][p]
            v = math.sin(2 * math.pi * 2 * t + idx * math.sin(2 * math.pi * 2 * ratio * t))
            if p == 4:
                v += 0.25 * rnd.uniform(-1, 1)
        elif 18 <= i < 25:                       # NOISE algorithms: filtered noise of different colours
            a = i - 18
            v = rnd.uniform(-1, 1)
            if not pts:
                acc = 0.0
            smooth = [0.18, 0.08, 0.3, 0.2, 0.9, 0.95, 0.0][a]
            acc = acc * smooth + v * (1 - smooth) if a < 4 else (v - (pts[-1][2] if pts else 0)) * 0.5
            if a == 6:                           # DECIM: held steps
                v = pts[-1][2] if pts and k % 12 else rnd.uniform(-0.9, 0.9)
            else:
                v = acc * (2.5 if a < 4 else 1.6)
        elif i == 25:                            # USER: a unit's tone
            v = 0.7 * math.sin(2 * math.pi * 3 * t) + 0.2 * math.sin(2 * math.pi * 9 * t)
        elif i == 26:                            # DELAY: echoes
            v = sum(math.exp(-((t - c) * 80) ** 2) * (0.85 ** j) for j, c in enumerate((0.05, 0.3, 0.55, 0.8))) * math.sin(2 * math.pi * 60 * t)
        elif i == 27:                            # REVERB: a dense tail
            v = math.exp(-t * 2.5) * rnd.uniform(-1, 1) * (1 - math.exp(-t * 40))
        elif i == 28:                            # MASTER: a compressed loop
            v = max(-0.7, min(0.7, 1.4 * math.sin(2 * math.pi * 4 * t) * (0.6 + 0.4 * math.sin(2 * math.pi * t))))
        else:                                    # GLOBAL: a flat line with a level marker
            v = 0.0
        v = max(-1.0, min(1.0, v))
        pts.append((K.P(20 + k), K.P(mid - v * (h / 2 - 16)), v))
    d.line([(x, y) for x, y, _ in pts], fill=K.OLED, width=K.P(2.2))
    return down(img)


# ---------------------------------------------------------------------------------------------- the pages
def page_background(tab):
    img = K.panel()
    if tab == 0:
        for c, (title, labels, shifted) in enumerate(KIT):
            cx = kit_pos(c, 0)[0]
            K.text(img, cx, 46, title, 21)
            if c:
                K.vline(img, KIT_X0 + KIT_CW * c, 34, 420)
            for r in range(3):
                ky = KIT_ROWS[r]
                lab = labels[r][1]
                if title == "HIGH HATS" and r == 2:
                    K.text(img, cx, ky - 50, "CLAP", 21)
                K.text(img, cx, ky + 46, lab, 17 if len(lab) < 10 else 15)
                if shifted:
                    K.text(img, cx, ky + 64, shifted[r][1], 15, K.INK_DIM)
        K.logo(img, 640, 36)
        x, y, w, h = SCREEN1
        screen_window(img, x, y, w, h, big=False)
        for i in range(4):
            K.text(img, ENC_X0 + i * ENC_DX, ENC_Y + 46, "PARAM %d" % (i + 1), 15, K.INK_DIM, "Medium")
        K.text(img, VOL[0], VOL[1] + 46, "VOLUME", 17)
        K.text(img, PAGE_UP[0], (PAGE_UP[1] + PAGE_DN[1]) / 2, "PAGE", 16)
        for a, b, label in ((0, 3, "ANALOG"), (4, 9, "DIGITAL"), (10, 12, "MULTI"), (13, 15, "EFFECTS")):
            K.hline_label(img, keys_x(a) - 28, keys_x(b) + 28, KEYS_Y - 32, label, 15)
        for i in range(16):
            K.text(img, keys_x(i), KEYS_Y - 66, str(i + 1), 15, K.INK_DIM, "Medium")
    elif tab == 1:
        for i, n in enumerate(MIX_NAMES):
            cx = MIX_X0 + MIX_DX * i
            if i:
                K.vline(img, cx - MIX_DX / 2, 28, 600, (70, 68, 70))
            K.text(img, cx, 38, n, 24)
            for _, label, ky in MIX_KNOBS:
                K.text(img, cx, ky + 31, label, 15)
            fader_slot(img, cx)
        cx = MASTER_X
        K.vline(img, cx - MIX_DX / 2 - 3, 28, 600, (110, 106, 108))
        K.text(img, cx, 38, "MASTER", 24)
        K.text(img, cx, MIX_KNOBS[0][2] + 31, "DRIVE", 15)
        K.text(img, cx, MIX_KNOBS[1][2] + 31, "CEILING", 15)
        fader_slot(img, cx)
    else:
        K.logo(img, 52, 48)
        x, y, w, h = SCREEN3
        screen_window(img, x, y, w, h, big=True)
        for i, (_, label) in enumerate(MACROS):
            K.text(img, 220 + i * 280, MACRO_Y + 64, label, 20)
    K.wood_cheeks(img)
    return down(img).convert("RGB")


def screen_window(img, x, y, w, h, big):
    """the screen's bezel and window, the header rule and the four boxes (MPC's text goes on top)"""
    d = ImageDraw.Draw(img)
    P = K.P
    d.rounded_rectangle([P(x - 14), P(y - 14), P(x + w + 14), P(y + h + 14)], P(8), fill=(10, 10, 11, 255),
                        outline=(58, 56, 58, 255), width=K.S)
    d.rectangle([P(x), P(y), P(x + w), P(y + h)], fill=(4, 4, 7, 255))
    hy = y + (52 if big else 48)
    d.line([(P(x + 10), P(hy)), (P(x + w - 10), P(hy))], fill=K.OLED + (255,), width=K.S)
    for (bx, by, bw, bh) in boxes(big):
        d.rectangle([P(bx), P(by), P(bx + bw), P(by + bh)], outline=K.OLED + (255,), width=K.S)


def boxes(big):
    if big:
        x, y, w, h = SCREEN3
        top = y + 52 + 150
        bw = (w - 20) / 4
        return [(x + 10 + bw * i + 6, top + 8, bw - 12, y + h - top - 18) for i in range(4)]
    x, y, w, h = SCREEN1
    bw = (w - 20) / 4
    return [(x + 10 + bw * i + 4, y + 60, bw - 8, h - 72) for i in range(4)]


def scope_rect():
    x, y, w, h = SCREEN3
    return (x + 10, y + 58, w - 20, 140)


def fader_slot(img, cx):
    """the ZYNTH fader box, printed on the page (the cap is the filmstrip)"""
    box = Image.open(os.path.join(K.ART, "zynth", "fader_box.png")).convert("RGBA")
    bw, bh = box.size
    w = FADER_W
    k = w / bw
    end = 18
    P = K.P
    top = box.crop((0, 0, bw, end)).resize((P(w), P(end * k)), Image.LANCZOS)
    bot = box.crop((0, bh - end, bw, bh)).resize((P(w), P(end * k)), Image.LANCZOS)
    mid = box.crop((0, end, bw, bh - end)).resize((P(w), P(FADER_H) - top.height - bot.height), Image.LANCZOS)
    x = P(cx - w / 2)
    img.alpha_composite(top, (x, P(FADER_Y)))
    img.alpha_composite(mid, (x, P(FADER_Y) + top.height))
    img.alpha_composite(bot, (x, P(FADER_Y) + top.height + mid.height))


def fader_strip(sk, bg, cx, name):
    """the fader's filmstrip over its slot: the ZYNTH cap from the bottom (frame 0) to the top"""
    cap = Image.open(os.path.join(K.ART, "zynth", "fader_cap.png")).convert("RGBA").resize((30, 54), Image.LANCZOS)
    w = 56
    rect = (R(cx - w / 2), FADER_Y, w, FADER_H)
    under = crop_layer(bg, rect)
    k = FADER_W / 40.0
    t0, t1 = 16 * k + 27 - 6, FADER_H - 16 * k - 27 + 6
    frames = min(101, 12288 // FADER_H)
    strip = Image.new("RGB", (w, FADER_H * frames))
    for f in range(frames):
        v = f / (frames - 1)
        fr = under.copy()
        cy = t1 - (t1 - t0) * v
        fr.alpha_composite(cap, ((w - 30) // 2, R(cy - 27)))
        strip.paste(fr.convert("RGB"), (0, f * FADER_H))
    sk.save(strip, name)
    return rect, frames


def build_page(sk, tab, bg):
    kids = []
    focused = set()
    idx = sk.index

    def place(dkey, key, rect, when=None, focus_on=True):
        f = focus_on and key not in focused
        focused.add(key)
        kids.append(placed(dkey, key, idx[key], rect, f, when))

    def knob(key, cx, cy, r, kind="cap", when=None):
        strip, side = knob_strip(sk, kind, r)
        tb = max(side, 64)
        frames = 2 if kind == "enc" else KNOB_FRAMES
        dkey = sk.define("dl_%s_%d" % (kind, r), KNOB_ACTIONS,
                         [focus(tb, tb), filmstrip("Knob", strip, frames - 1 if frames > 2 else 1, side, side,
                                                   "Vertical", (tb - side) // 2, (tb - side) // 2)])
        place(dkey, key, (R(cx - tb / 2), R(cy - tb / 2), tb, tb), when)

    def readout(key, rect, px, just="horizontallyCentred verticallyCentred", colour="eef0f6"):
        dkey = sk.define("dl_text_%s_%d_%d" % (key, rect[2], px), [],
                         [text_label("Value", rect[2], rect[3], px, colour, just)], ignore=True)
        place(dkey, key, rect, focus_on=False)

    def name_text(key, rect, px, colour="eef0f6"):
        dkey = sk.define("dl_name_%d_%d_%d" % (rect[2], rect[3], px), [], [text_label("Name", rect[2], rect[3], px, colour)],
                         ignore=True)
        kids.append(placed(dkey, key, idx[key], rect, False))

    def key_button(key, cx, cy, w, h, label, fsize, led=False, bid=None, n=None, num=None, led_dy=-46):
        top = cy - h / 2 - (60 if led else 4)
        rect = (R(cx - w / 2 - 6), R(top), R(w + 12), R(cy + h / 2 + 8 - top))
        ox, oy = 0, 0

        def drawer(on):
            def f(c, dx, dy):
                draw_key(c, dx, dy, cx, cy, w, h, label, fsize, (on if led else None), led_dy, None)
                if not led and on:   # a lit key without LED: a red glow inside
                    K.led(c, cx + dx + w / 2 - 9, cy + dy - h / 2 + 9, True, 3)
            return f
        on = sk.save(overlay(bg, rect, drawer(True)), "dl_key_%s_%s_on.png" % (key, bid if bid is not None else "t"))
        off = sk.save(overlay(bg, rect, drawer(False)), "dl_key_%s_%s_off.png" % (key, bid if bid is not None else "t"))
        if bid is None:
            dkey = sk.define("dl_key_%s" % key, TRIG_ACTIONS if key in TRIGGERS else KEY_ACTIONS,
                             [focus(rect[2], rect[3]), button(on, off, 1, 1, rect[2], rect[3])])
        else:
            dkey = sk.define("dl_key_%s_%d" % (key, bid), RADIO_ACTIONS, [focus(rect[2], rect[3]),
                                                                          button(on, off, bid, n, rect[2], rect[3])])
        place(dkey, key, rect)

    shift_i = idx["shift"]
    if tab == 0:
        for c, (title, labels, shifted) in enumerate(KIT):
            for r in range(3):
                cx, cy = kit_pos(c, r)
                if shifted:
                    knob(labels[r][0], cx, cy, KIT_R, when=(shift_i, 0, 2))
                    knob(shifted[r][0], cx, cy, KIT_R, when=(shift_i, 1, 2))
                else:
                    knob(labels[r][0], cx, cy, KIT_R)
        knob("volume", VOL[0], VOL[1], VOL[2])
        for i in range(4):
            knob("p%d" % (i + 1), ENC_X0 + i * ENC_DX, ENC_Y, ENC_R, "enc")
        x, y, w, h = SCREEN1
        readout("lcd_title", (x + 14, y + 6, w - 110, 40), 30, "left verticallyCentred")
        readout("lcd_page", (x + w - 96, y + 6, 84, 40), 28, "right verticallyCentred")
        for i, (bx, by, bw, bh) in enumerate(boxes(False)):
            name_text("p%d" % (i + 1), (bx + 2, by + 6, bw - 4, 34), 23)
            readout("p%d" % (i + 1), (bx + 2, by + bh - 62, bw - 4, 52), 36)
        # PAGE up/down, SHIFT, GLOBAL
        for key, (cx, cy), arrow in (("page_up", PAGE_UP, True), ("page_down", PAGE_DN, False)):
            rect = (R(cx - 32), R(cy - 21), 64, 42)

            def dr(on, cx=cx, cy=cy, arrow=arrow):
                def f(c, dx, dy):
                    K.button(c, cx + dx, cy + dy, 52, 30)
                    K.tri(c, cx + dx, cy + dy, arrow)
                    if on:
                        K.led(c, cx + dx + 18, cy + dy - 7, True, 2.5)
                return f
            on = sk.save(overlay(bg, rect, dr(True)), "dl_%s_on.png" % key)
            off = sk.save(overlay(bg, rect, dr(False)), "dl_%s_off.png" % key)
            dkey = sk.define("dl_key_%s" % key, TRIG_ACTIONS, [focus(64, 42), button(on, off, 1, 1, 64, 42)])
            place(dkey, key, rect)
        key_button("shift", SHIFT_KEY[0], SHIFT_KEY[1], 64, 36, "SHIFT", 15)
        key_button("sel", GLOBAL_KEY[0], GLOBAL_KEY[1], 64, 36, "GLOBAL", 14, bid=16, n=NUM_SEL)
        for i, name in enumerate(SEL16):
            key_button("sel", keys_x(i), KEYS_Y, 60, 46, name, 17 if len(name) < 5 else 14, led=True, bid=i, n=NUM_SEL)
    elif tab == 1:
        for i, k in enumerate(MIX_KEYS):
            cx = MIX_X0 + MIX_DX * i
            for suffix, _, ky in MIX_KNOBS:
                knob("%s_%s" % (k, suffix), cx, ky, MIX_KR, "mini")
            rect, frames = fader_strip(sk, bg, cx, "dl_fader_%s.png" % k)
            dkey = sk.define("dl_fader_%s" % k, KNOB_ACTIONS,
                             [focus(rect[2], rect[3]), filmstrip("Slider", "dl_fader_%s.png" % k, frames, rect[2], rect[3])])
            place(dkey, k + "_level", rect)
            key_button(k + "_mute", cx + 8, MUTE_Y, 50, 30, "MUTE", 13)
        cx = MASTER_X
        knob("lim_drive", cx, MIX_KNOBS[0][2], MIX_KR, "mini")
        knob("lim_ceiling", cx, MIX_KNOBS[1][2], MIX_KR, "mini")
        rect, frames = fader_strip(sk, bg, cx, "dl_fader_master.png")
        dkey = sk.define("dl_fader_master", KNOB_ACTIONS,
                         [focus(rect[2], rect[3]), filmstrip("Slider", "dl_fader_master.png", frames, rect[2], rect[3])])
        place(dkey, "volume", rect)
        key_button("panic", cx, MUTE_Y, 64, 30, "PANIC", 13)
    else:
        x, y, w, h = SCREEN3
        sx, sy, sw, sh = scope_rect()
        n = sk.nopts("scope")
        for i in range(n):
            pic = sk.save(scope_picture(i, sw, sh), "dl_scope_%02d.png" % i)
            kids.append(picture(pic, (sx, sy, sw, sh), idx["scope"], i, n))
        readout("lcd_title", (x + 16, y + 6, w - 200, 44), 34, "left verticallyCentred")
        readout("lcd_page", (x + w - 180, y + 6, 164, 44), 32, "right verticallyCentred")
        for i, (bx, by, bw, bh) in enumerate(boxes(True)):
            name_text("p%d" % (i + 1), (bx + 4, by + 6, bw - 8, 36), 27)
            readout("p%d" % (i + 1), (bx + 4, by + bh - 62, bw - 8, 54), 42)
        for i, (key, _) in enumerate(MACROS):
            knob(key, 220 + i * 280, MACRO_Y, MACRO_R)
    return kids


def main(skin):
    tui_path = os.path.join(skin, "TUI.json")
    tui = json.load(open(tui_path))
    cdefs = tui["pageData"]["componentDefinitions"]
    defs = cdefs["localComponentDefinitions"]
    params = json.load(open(os.path.join(ROOT, "params.json")))["params"]
    sk = Skin(skin, params)
    pages = {}
    for tab, name in enumerate(TABS):
        bg = page_background(tab)
        bgname = sk.save(bg, "dl_bg_%d.png" % tab)
        kids = build_page(sk, tab, bg.convert("RGBA"))
        pages[name] = [sub("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname},
                           bounds_s(0, 0, W, H))] + kids
        # every Q-Link key must have a control on its page
        shown = {c["handle remapping"]["map"][0]["value"] for c in kids if c["handle remapping"]["map"]}
        for sname, ks in qlink_pages(tab):
            for k in ks:
                if "Parameter %d" % sk.index[k] not in shown:
                    raise SystemExit("ui.py: %s (Q-Links %s %s) has no control on the page" % (k, name, sname))
    keep = []
    for e in defs:
        tabname = e["key"].split("|")[0]
        if "|" in e["key"] and tabname in pages:
            e["value"]["componentsData"] = pages[tabname]
            e["value"]["backgroundData"] = {"version": 1, "focussed": {"version": 1, "colour": "ff1b191a", "image": ""},
                                            "unfocussed": {"version": 1, "colour": "ff1b191a", "image": ""}}
            keep.append(e)
    cdefs["localComponentDefinitions"] = keep + list(sk.defs.values())
    json.dump(tui, open(tui_path, "w"), indent=1)
    for f in os.listdir(skin):
        if f.endswith(".png") and f not in sk.used:
            os.remove(os.path.join(skin, f))
    print("ui.py: %d definitions, %d pictures" % (len(sk.defs), len(sk.used)))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--write":
        write_layout(os.path.join(ROOT, "layout.conf"))
        print("layout.conf written")
    elif len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        print(__doc__)
