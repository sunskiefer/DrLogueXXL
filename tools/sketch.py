#!/usr/bin/env python3
"""DrLogueXXL UI sketch: the three pages (PERFORM, MIXER, SCREEN) as 1280x628 PNGs, for review before the skin.

    python3 tools/sketch.py [outdir]      (default build/sketch)

Look, from the user's photos of the machine (art/source/): the black brushed panel, the dark ash wood cheeks (cut from
the photo), grey knob caps with a light pointer, grey square part buttons with red LEDs, the white-on-black screen
(enlarged). Knobs and buttons are re-rendered at full resolution in the photo's measured colours (the photo's own
caps are ~50 px, too small to scale up cleanly). The mixer faders are LXRXXL ZYNTH's (art/zynth/). Lettering: Barlow
Semi Condensed (OFL), a DIN-like condensed sans close to the machine's panel print. No maker names or logos.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ART = os.path.join(ROOT, "art")
W, H = 1280, 628
S = 2                                   # supersampling
WOOD = 26                               # wood cheek width (page px)

INK = (222, 219, 220)                   # panel print
INK_DIM = (138, 133, 136)               # the shifted (second) label
PANEL = (27, 25, 26)
CAP_HI, CAP_LO = (112, 109, 115), (58, 56, 61)
BTN = (84, 76, 80)
LED_ON, LED_OFF = (236, 70, 88), (70, 30, 36)
OLED = (238, 240, 246)


def font(px, weight="SemiBold"):
    return ImageFont.truetype(os.path.join(ART, "fonts", "BarlowSemiCondensed-%s.ttf" % weight), int(px * S))


def P(v):
    return int(round(v * S))


# ---------------------------------------------------------------------------------------------- panel and wood
def panel():
    rnd = random.Random(7)
    img = Image.new("RGB", (W * S, H * S), PANEL)
    # brushed finish: faint horizontal streaks
    streaks = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(streaks)
    for _ in range(2600):
        y = rnd.randrange(H * S)
        x = rnd.randrange(-200, W * S)
        d.line([(x, y), (x + rnd.randrange(80, 900), y)], fill=rnd.randrange(4, 14))
    streaks = streaks.filter(ImageFilter.GaussianBlur(0.6))
    img = Image.composite(Image.new("RGB", img.size, (60, 57, 59)), img, streaks)
    # soft light falloff like the photo (lighter top-left)
    shade = Image.radial_gradient("L").resize((W * S, H * S)).point(lambda v: int(v * 0.35))
    img = Image.composite(Image.new("RGB", img.size, (14, 13, 14)), img, shade)
    return img.convert("RGBA")


def wood_cheeks(img):
    src = Image.open(os.path.join(ART, "source", "panel_full.jpg")).convert("RGB")
    cheek = src.crop((7, 12, 33, 664))                       # the left wood end, without the panel edge
    cheek = cheek.resize((P(WOOD), H * S), Image.LANCZOS)
    cheek = cheek.filter(ImageFilter.UnsharpMask(2, 60, 2))
    cheek = cheek.point(lambda v: min(255, int(v * 1.9 + 6)))   # lift the dark ash so its grain reads on screen
    img.paste(cheek, (0, 0))
    img.paste(cheek.transpose(Image.FLIP_LEFT_RIGHT), (P(W - WOOD), 0))
    d = ImageDraw.Draw(img)
    for x in (P(WOOD), P(W - WOOD) - 1):                      # the joint
        d.line([(x, 0), (x, H * S)], fill=(8, 8, 8), width=S * 2)


# ---------------------------------------------------------------------------------------------- controls
def shadow(img, cx, cy, r, blur=6, off=4, alpha=170):
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([P(cx - r), P(cy - r + off), P(cx + r), P(cy + r + off)], fill=(0, 0, 0, alpha))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(P(blur))))


def knob(img, cx, cy, r, angle=-30, pointer=True):
    """the machine's grey cap: a slightly domed disc, light from the top left, a light pointer line"""
    shadow(img, cx, cy, r)
    cap = Image.new("RGBA", (P(2 * r), P(2 * r)), (0, 0, 0, 0))
    grad = Image.linear_gradient("L").rotate(45, expand=False).resize(cap.size)
    body = Image.composite(Image.new("RGBA", cap.size, CAP_LO + (255,)), Image.new("RGBA", cap.size, CAP_HI + (255,)), grad)
    mask = Image.new("L", cap.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, cap.size[0] - 1, cap.size[1] - 1], fill=255)
    cap.paste(body, (0, 0), mask)
    d = ImageDraw.Draw(cap)
    d.ellipse([1, 1, cap.size[0] - 2, cap.size[1] - 2], outline=(132, 129, 136, 255), width=max(1, S))   # rim light
    d.ellipse([P(r * 0.12), P(r * 0.12), P(2 * r - r * 0.12), P(2 * r - r * 0.12)], outline=(0, 0, 0, 40), width=S)
    if pointer:
        a = math.radians(angle - 90)
        x0, y0 = P(r + math.cos(a) * r * 0.42), P(r + math.sin(a) * r * 0.42)
        x1, y1 = P(r + math.cos(a) * r * 0.88), P(r + math.sin(a) * r * 0.88)
        d.line([(x0, y0), (x1, y1)], fill=(214, 212, 214, 255), width=max(2, P(r * 0.11)))
    img.alpha_composite(cap, (P(cx - r), P(cy - r)))


def small_knob(img, cx, cy, r, angle=0):
    """the knurled black mini knob of the mixer row"""
    shadow(img, cx, cy, r, blur=3, off=2)
    d = ImageDraw.Draw(img)
    d.ellipse([P(cx - r), P(cy - r), P(cx + r), P(cy + r)], fill=(20, 19, 21, 255), outline=(70, 68, 72, 255), width=S)
    for k in range(18):                              # knurl
        a = 2 * math.pi * k / 18
        d.line([(P(cx + math.cos(a) * r * 0.78), P(cy + math.sin(a) * r * 0.78)),
                (P(cx + math.cos(a) * r * 0.97), P(cy + math.sin(a) * r * 0.97))], fill=(52, 50, 54, 255), width=S)
    a = math.radians(angle - 90)
    d.line([(P(cx), P(cy)), (P(cx + math.cos(a) * r * 0.8), P(cy + math.sin(a) * r * 0.8))],
           fill=(205, 203, 205, 255), width=max(2, P(r * 0.13)))


def encoder(img, cx, cy, r):
    """the four flat grey parameter knobs under the screen (no pointer)"""
    knob(img, cx, cy, r, pointer=False)


def button(img, cx, cy, w, h, label="", lit=None, fsize=17):
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([P(cx - w / 2), P(cy - h / 2 + 3), P(cx + w / 2), P(cy + h / 2 + 3)],
                                         P(5), fill=(0, 0, 0, 180))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(P(3))))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([P(cx - w / 2), P(cy - h / 2), P(cx + w / 2), P(cy + h / 2)], P(5), fill=(16, 15, 16, 255))
    d.rounded_rectangle([P(cx - w / 2 + 3), P(cy - h / 2 + 3), P(cx + w / 2 - 3), P(cy + h / 2 - 3)], P(4),
                        fill=BTN + (255,), outline=(104, 96, 100, 255), width=S)
    if label:
        text(img, cx, cy, label, fsize, (196, 188, 192))


def tri(img, cx, cy, up, r=7):
    d = ImageDraw.Draw(img)
    pts = [(cx - r, cy + r * 0.5), (cx + r, cy + r * 0.5), (cx, cy - r * 0.6)] if up else \
          [(cx - r, cy - r * 0.5), (cx + r, cy - r * 0.5), (cx, cy + r * 0.6)]
    d.polygon([(P(x), P(y)) for x, y in pts], fill=(200, 192, 196, 255))


def led(img, cx, cy, on, r=5):
    if on:
        glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse([P(cx - r * 2.6), P(cy - r * 2.6), P(cx + r * 2.6), P(cy + r * 2.6)],
                                     fill=LED_ON + (110,))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(P(4))))
    d = ImageDraw.Draw(img)
    d.ellipse([P(cx - r), P(cy - r), P(cx + r), P(cy + r)], fill=(LED_ON if on else LED_OFF) + (255,),
              outline=(20, 10, 12, 255), width=S)
    if on:
        d.ellipse([P(cx - r * 0.4), P(cy - r * 0.55), P(cx + r * 0.1), P(cy - r * 0.05)], fill=(255, 210, 215, 255))


def text(img, cx, cy, s, px, color=INK, weight="SemiBold", anchor="mm"):
    ImageDraw.Draw(img).text((P(cx), P(cy)), s, font=font(px, weight), fill=color + (255,), anchor=anchor)


def vline(img, x, y0, y1, color=(150, 146, 148)):
    ImageDraw.Draw(img).line([(P(x), P(y0)), (P(x), P(y1))], fill=color + (255,), width=S)


def hline_label(img, x0, x1, y, label, px=15):
    """a bracket line with its title in the middle, as over the part buttons"""
    f = font(px)
    tw = ImageDraw.Draw(img).textlength(label, font=f) / S
    mid = (x0 + x1) / 2
    d = ImageDraw.Draw(img)
    d.line([(P(x0), P(y)), (P(mid - tw / 2 - 8), P(y))], fill=(150, 146, 148, 255), width=S)
    d.line([(P(mid + tw / 2 + 8), P(y)), (P(x1), P(y))], fill=(150, 146, 148, 255), width=S)
    text(img, mid, y, label, px)


def fader(img, cx, y0, h, pos):
    """LXRXXL ZYNTH's fader: its box (ends kept, middle stretched) and its cap"""
    box = Image.open(os.path.join(ART, "zynth", "fader_box.png")).convert("RGBA")
    bw, bh = box.size
    w = 34
    k = w / bw
    end = 18
    top = box.crop((0, 0, bw, end)).resize((P(w), P(end * k)), Image.LANCZOS)
    bot = box.crop((0, bh - end, bw, bh)).resize((P(w), P(end * k)), Image.LANCZOS)
    mid = box.crop((0, end, bw, bh - end)).resize((P(w), P(h) - top.height - bot.height), Image.LANCZOS)
    x = P(cx - w / 2)
    img.alpha_composite(top, (x, P(y0)))
    img.alpha_composite(mid, (x, P(y0) + top.height))
    img.alpha_composite(bot, (x, P(y0) + top.height + mid.height))
    cap = Image.open(os.path.join(ART, "zynth", "fader_cap.png")).convert("RGBA")
    cw, ch = 30, 54
    cap = cap.resize((P(cw), P(ch)), Image.LANCZOS)
    t0, t1 = y0 + 16 * k + ch / 2 - 6, y0 + h - 16 * k - ch / 2 + 6
    cy = t1 - (t1 - t0) * pos
    img.alpha_composite(cap, (P(cx - cw / 2), P(cy - ch / 2)))


# ---------------------------------------------------------------------------------------------- the screen
def screen(img, x, y, w, h, kind="params"):
    """the machine's OLED, enlarged: a dark window in a bevelled bezel, white pixels"""
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([P(x - 14), P(y - 14), P(x + w + 14), P(y + h + 14)], P(8), fill=(10, 10, 11, 255),
                        outline=(58, 56, 58, 255), width=S)
    d.rectangle([P(x), P(y), P(x + w), P(y + h)], fill=(4, 4, 7, 255))
    glass = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(glass).polygon([(P(x), P(y)), (P(x + w * 0.55), P(y)), (P(x + w * 0.25), P(y + h)), (P(x), P(y + h))],
                                  fill=(255, 255, 255, 7))
    img.alpha_composite(glass)
    if kind == "params":
        # header: part and page, then four boxes (name over value), one per knob below
        text(img, x + 16, y + 26, "BD  BASS DRUM", 26, OLED, "SemiBold", "lm")
        text(img, x + w - 16, y + 26, "1/3", 24, OLED, "Medium", "rm")
        d.line([(P(x + 10), P(y + 48)), (P(x + w - 10), P(y + 48))], fill=OLED + (255,), width=S)
        names, vals = ["SWEEP", "SW.TIME", "HOLD", "ATTACK"], ["50%", "50%", "0 ms", "50%"]
        bw = (w - 20) / 4
        for i in range(4):
            bx = x + 10 + bw * i
            d.rectangle([P(bx + 4), P(y + 60), P(bx + bw - 4), P(y + h - 12)], outline=OLED + (255,), width=S)
            text(img, bx + bw / 2, y + 84, names[i], 22, OLED, "Medium")
            text(img, bx + bw / 2, y + h - 44, vals[i], 34, OLED, "SemiBold")
    else:
        # the wave screen: the MULTI engine's waveform, big, with its name and two values
        text(img, x + 16, y + 26, "MULTI  WAVES", 26, OLED, "SemiBold", "lm")
        text(img, x + w - 16, y + 26, "WAVE A  12", 24, OLED, "Medium", "rm")
        d.line([(P(x + 10), P(y + 48)), (P(x + w - 10), P(y + 48))], fill=OLED + (255,), width=S)
        mid = y + 48 + (h - 48 - 40) / 2
        pts = []
        for i in range(int(w - 40)):
            t = i / (w - 40) * 2
            v = 0.55 * math.sin(2 * math.pi * t) + 0.25 * math.sin(6 * math.pi * t + 0.4) + 0.12 * math.sin(10 * math.pi * t)
            pts.append((P(x + 20 + i), P(mid - v * (h - 110) / 2)))
        d.line([(P(x + 20), P(mid)), (P(x + w - 20), P(mid))], fill=(90, 92, 100, 255), width=S)
        d.line(pts, fill=OLED + (255,), width=P(2.4))
        text(img, x + 16, y + h - 20, "SHAPE 34%", 22, OLED, "Medium", "lm")
        text(img, x + w - 16, y + h - 20, "SUB 10%", 22, OLED, "Medium", "rm")


def logo(img, x, y):
    text(img, x, y, "DrLogueXXL", 40, INK, "SemiBold", "lm")
    text(img, x + 2, y + 30, "HYBRID DRUM MACHINE  ·  L'Cronx", 16, INK_DIM, "Medium", "lm")


# ---------------------------------------------------------------------------------------------- pages
PARTS16 = ["BD", "SD", "LT", "HT", "CH", "OH", "RS", "CP", "SP 1", "SP 2", "VPM", "NOISE", "USER", "DELAY", "REVERB",
           "MASTER"]


def part_buttons(img, y, selected=0):
    x0, step = 64, 74.4
    for i, name in enumerate(PARTS16):
        cx = x0 + step * i
        led(img, cx, y - 46, i == selected)
        text(img, cx, y - 66, str(i + 1), 15, INK_DIM, "Medium")
        button(img, cx, y, 60, 46, name, fsize=17 if len(name) < 5 else 14)
    for a, b, label in ((0, 3, "ANALOG"), (4, 9, "DIGITAL"), (10, 12, "MULTI"), (13, 15, "EFFECTS")):
        hline_label(img, x0 + step * a - 28, x0 + step * b + 28, y - 26, label, 15)


def page_perform():
    img = panel()
    # kit control: five sections of three knobs, as on the machine (second label = the part SHIFT switches to)
    cols = [("BASS DRUM", ["DECAY", "TUNE", "DRIVE"], None),
            ("SNARE DRUM", ["DECAY", "TUNE", "SNAPPY"], None),
            ("TOMS", ["LT DECAY", "LT TUNE", "LT DETUNE"], ["HT DECAY", "HT TUNE", "HT DETUNE"]),
            ("HIGH HATS", ["CH DECAY", "OH DECAY", "DECAY"], None),
            ("SAMPLE 1/2", ["SP 1 ATTACK", "SP 1 DECAY", "SP 1 TUNE"], ["SP 2 ATTACK", "SP 2 DECAY", "SP 2 TUNE"])]
    x0, cw = 40, 117
    angles = [[-40, 10, 60], [-20, -60, 30], [0, 40, -50], [-30, 20, 15], [10, -20, 45]]
    for c, (title, labels, shifted) in enumerate(cols):
        cx = x0 + cw * c + cw / 2
        text(img, cx, 46, title, 21)
        if c:
            vline(img, x0 + cw * c, 34, 420)
        for r in range(3):
            ky = 108 + r * 116
            knob(img, cx, ky, 30, angles[c][r])
            ly = ky + 46
            if title == "HIGH HATS" and r == 2:
                text(img, cx, ky - 50, "CLAP", 21)
            text(img, cx, ly, labels[r], 17 if len(labels[r]) < 10 else 15)
            if shifted:
                text(img, cx, ly + 18, shifted[r], 15, INK_DIM)
    # logo and screen
    logo(img, 640, 36)
    screen(img, 642, 108, 440, 190, "params")
    for i in range(4):
        encoder(img, 668 + i * 128, 360, 36)
        text(img, 668 + i * 128, 406, "PARAM %d" % (i + 1), 15, INK_DIM, "Medium")
    # right column: volume, page, part/mute/shift
    knob(img, 1200, 82, 30, 40)
    text(img, 1200, 128, "VOLUME", 17)
    button(img, 1200, 186, 52, 30)
    tri(img, 1200, 186, True)
    text(img, 1200, 214, "PAGE", 16)
    button(img, 1200, 244, 52, 30)
    tri(img, 1200, 244, False)
    for k, (label, y) in enumerate((("MUTE", 300), ("PART", 352), ("SHIFT", 404))):
        button(img, 1200, y, 64, 36, label, fsize=15)
    part_buttons(img, 560, selected=0)
    wood_cheeks(img)
    return img


def page_mixer():
    img = panel()
    names = ["BD", "SD", "LT", "HT", "CH", "OH", "RS", "CP", "SP 1", "SP 2", "MULTI"]
    x0, step = 58, 94
    pos = [0.8, 0.72, 0.6, 0.6, 0.55, 0.62, 0.5, 0.66, 0.58, 0.58, 0.7]
    for i, n in enumerate(names):
        cx = x0 + step * i
        if i:
            vline(img, cx - step / 2, 28, 600, (70, 68, 70))
        text(img, cx, 38, n, 24)
        for k, (label, ang) in enumerate((("PAN", 0), ("DELAY", -100), ("REVERB", -60))):
            ky = 92 + k * 80
            small_knob(img, cx, ky, 18, ang + i * 7 if k else (i - 5) * 9)
            text(img, cx, ky + 31, label, 15)
        fader(img, cx, 330, 210, pos[i])
        led(img, cx - 20, 572, i == 6, 4)
        button(img, cx + 8, 572, 50, 30, "MUTE", fsize=13)
    # master
    cx = x0 + step * 11 + 6
    vline(img, cx - step / 2 - 3, 28, 600, (110, 106, 108))
    text(img, cx, 38, "MASTER", 24)
    knob(img, cx, 112, 30, 40)
    text(img, cx, 158, "VOLUME", 16)
    small_knob(img, cx, 220, 18, -20)
    text(img, cx, 251, "LIMIT", 15)
    fader(img, cx, 330, 210, 0.78)
    button(img, cx, 572, 64, 30, "PANIC", fsize=13)
    wood_cheeks(img)
    return img


def page_screen(kind="wave"):
    img = panel()
    logo(img, 52, 48)
    screen(img, 92, 100, 1096, 330, kind)
    for i, label in enumerate(("MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4")):
        cx = 220 + i * 280
        knob(img, cx, 520, 42, -60 + 40 * i)
        text(img, cx, 584, label, 20)
    wood_cheeks(img)
    return img


def save(img, path):
    img.resize((W, H), Image.LANCZOS).convert("RGB").save(path)
    print(path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "sketch")
    os.makedirs(out, exist_ok=True)
    save(page_perform(), os.path.join(out, "1_perform.png"))
    save(page_mixer(), os.path.join(out, "2_mixer.png"))
    save(page_screen("wave"), os.path.join(out, "3_screen_wave.png"))
    save(page_screen("params"), os.path.join(out, "3_screen_params.png"))
