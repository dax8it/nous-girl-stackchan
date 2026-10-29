#!/usr/bin/env python3
"""Starter faces for Nous-Girl public firmware: robot, boy, girl.

Original artwork, drawn entirely from code (no source images).
License of the generated art and this script: CC0 1.0 (public domain dedication).

Outputs, per character, in OUT/<name>/:
  gifs/*.gif            12 face animations, 320x240, for the firmware assets (fairy-assets/)
  _emote_aliases.json   alias table the assets builder expects
  hg_mouth_frames.h     4 mouth sets x 8 visemes, RGB565, for the lip-sync overlay
  hg_face_layout.h      eye / anger-mark positions for the code-drawn scowl
  sheet.png             every frame on one sheet (the template to copy)
  preview.png           labelled contact sheet
Usage: python3 make_starter_faces.py [OUT_DIR]   (needs Pillow)
"""
import json, math, os, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
S = 3                                   # supersampling factor
LEVELS = [0, 70, 140, 200, 235, 255]    # grayscale palette (black ink on white)
INK, MID, LIGHT, WHITE = 0, 140, 200, 255
EYE_L, EYE_R, EYE_Y = 118, 202, 104     # eye centres (screen pixels)
MOUTH_C = (160, 158)                    # mouth centre
MOUTH_BOX = (128, 140, 64, 36)          # x, y, w, h of the lip-sync overlay
ANGER_XY = (228, 34)
SCOWL = [((136, 96), (100, 84)), ((184, 96), (220, 84))]   # (inner low, outer high) per eye
LW = 5                                  # line width

def P(v):                                # scale a coordinate
    return v * S

class Pen:
    def __init__(self, img):
        self.d = ImageDraw.Draw(img)
    def ell(self, cx, cy, rx, ry, fill=None, outline=INK, w=LW):
        self.d.ellipse([P(cx - rx), P(cy - ry), P(cx + rx), P(cy + ry)], fill=fill,
                       outline=outline, width=P(w) if outline is not None else 0)
    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=INK, w=LW):
        self.d.rounded_rectangle([P(x0), P(y0), P(x1), P(y1)], radius=P(r), fill=fill,
                                 outline=outline, width=P(w) if outline is not None else 0)
    def line(self, pts, w=LW, fill=INK):
        self.d.line([(P(x), P(y)) for x, y in pts], fill=fill, width=P(w), joint="curve")
        for x, y in (pts[0], pts[-1]):
            self.d.ellipse([P(x) - P(w) / 2, P(y) - P(w) / 2, P(x) + P(w) / 2, P(y) + P(w) / 2], fill=fill)
    def arc(self, cx, cy, rx, ry, a0, a1, w=LW, fill=INK, n=24):
        pts = [(cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))
               for a in [a0 + (a1 - a0) * i / n for i in range(n + 1)]]
        self.line(pts, w, fill)
    def poly(self, pts, fill=INK, outline=None):
        self.d.polygon([(P(x), P(y)) for x, y in pts], fill=fill, outline=outline)

# ---------- small decorations ----------
def heart(p, cx, cy, r):
    pts = [(cx + r * 16 * math.sin(t) ** 3 / 16,
            cy - r * (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) / 16)
           for t in [i * 2 * math.pi / 60 for i in range(60)]]
    p.poly(pts, fill=INK)

def star(p, cx, cy, r, spikes=4):
    pts = []
    for i in range(spikes * 2):
        a = math.pi * i / spikes - math.pi / 2
        rr = r if i % 2 == 0 else r * 0.38
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    p.poly(pts, fill=INK)

def zed(p, x, y, s):
    p.line([(x, y), (x + s, y), (x, y + s), (x + s, y + s)], w=3)

def envelope(p, x, y):
    p.rrect(x, y, x + 46, y + 32, 4, fill=WHITE)
    p.line([(x + 2, y + 3), (x + 23, y + 18), (x + 44, y + 3)], w=4)

# ---------- characters ----------
def body_robot(p):
    p.line([(160, 34), (160, 16)], w=5)
    p.ell(160, 12, 7, 7, fill=INK)
    p.rrect(46, 96, 64, 134, 5, fill=LIGHT)
    p.rrect(256, 96, 274, 134, 5, fill=LIGHT)
    p.rrect(62, 34, 258, 194, 30, fill=WHITE, w=6)
    p.ell(84, 54, 4, 4, fill=INK, outline=None)
    p.ell(236, 54, 4, 4, fill=INK, outline=None)

def body_boy(p):
    p.ell(70, 116, 14, 20, fill=WHITE)
    p.ell(250, 116, 14, 20, fill=WHITE)
    p.ell(160, 114, 92, 80, fill=WHITE, w=6)
    # hair cap with a front tuft
    pts = [(74, 92)]
    pts += [(160 + 90 * math.cos(math.radians(a)), 112 + 82 * math.sin(math.radians(a))) for a in range(190, 351, 8)]
    pts += [(246, 92), (214, 70), (188, 80), (172, 62), (150, 78), (120, 66), (98, 84)]
    p.poly(pts, fill=MID)
    p.line([(160, 32), (166, 18), (176, 30)], w=5)

def body_girl(p):
    p.ell(70, 56, 30, 30, fill=MID)
    p.ell(250, 56, 30, 30, fill=MID)
    p.ell(160, 114, 92, 80, fill=WHITE, w=6)
    pts = [(70, 112)]
    pts += [(160 + 92 * math.cos(math.radians(a)), 114 + 82 * math.sin(math.radians(a))) for a in range(180, 361, 8)]
    pts += [(250, 112), (244, 84)]
    for i in range(8, -1, -1):                    # scalloped bangs
        x = 84 + i * 19
        pts += [(x + 9, 76 + (6 if i % 2 else 0)), (x, 70)]
    pts += [(76, 84)]
    p.poly(pts, fill=MID)
    p.ell(236, 50, 9, 9, fill=WHITE)              # hair clip
    p.ell(236, 50, 3, 3, fill=INK, outline=None)

CHARS = {"robot": body_robot, "boy": body_boy, "girl": body_girl}

# ---------- eyes ----------
def eyes(p, kind, style, gaze=(0, 0)):
    gx, gy = gaze
    for cx in (EYE_L, EYE_R):
        if kind == "open" or kind == "wide":
            big = 2 if kind == "wide" else 0
            if style == "robot":
                p.rrect(cx - 24, EYE_Y - 22, cx + 24, EYE_Y + 22, 9, fill=WHITE)
                p.rrect(cx - 12 - big + gx, EYE_Y - 12 - big + gy, cx + 12 + big + gx, EYE_Y + 12 + big + gy, 4, fill=INK, outline=None)
                p.rrect(cx - 7 + gx, EYE_Y - 8 + gy, cx - 2 + gx, EYE_Y - 3 + gy, 1, fill=WHITE, outline=None)
            else:
                p.ell(cx, EYE_Y, 17, 21, fill=WHITE)
                p.ell(cx + gx, EYE_Y + gy + 2, 9 + big, 10 + big, fill=INK, outline=None)
                p.ell(cx + gx - 3, EYE_Y + gy - 2, 3, 3, fill=WHITE, outline=None)
        elif kind == "blink":
            p.line([(cx - 17, EYE_Y + 2), (cx + 17, EYE_Y + 2)], w=6)
        elif kind == "happy":
            p.arc(cx, EYE_Y + 8, 16, 14, 200, 340, w=6)
        elif kind == "closed":
            p.arc(cx, EYE_Y - 2, 16, 12, 20, 160, w=6)
        elif kind == "dizzy":
            pts = [(cx + (2 + t * 0.8) * math.cos(t), EYE_Y + (2 + t * 0.8) * math.sin(t)) for t in [i * 0.3 for i in range(60)]]
            p.line(pts, w=4)
    return

def brows(p, kind):
    if kind == "sad":
        p.line([(100, 74), (134, 66)], w=5); p.line([(186, 66), (220, 74)], w=5)
    elif kind == "think":
        p.line([(100, 74), (134, 74)], w=5); p.line([(186, 64), (220, 58)], w=5)

def blush(p):
    for cx in (94, 226):
        p.ell(cx, 136, 14, 8, fill=LIGHT, outline=None)

# ---------- mouths (drawn inside MOUTH_BOX) ----------
VISEMES = ["closed", "half", "open", "wide_e", "round_o", "small_u", "wide_i", "smile"]

def mouth(p, shape, style, mood="neutral"):
    cx, cy = MOUTH_C
    if mood == "thinking":
        cx += 6
    k = 0.85 if mood == "thinking" else 1.0
    robot = style == "robot"
    if shape == "closed":
        if mood == "sad":
            p.arc(cx, cy + 10, 16, 9, 200, 340, w=5)
        elif mood == "thinking":
            p.line([(cx - 10, cy + 2), (cx + 12, cy - 1)], w=5)
        elif robot:
            p.line([(cx - 18, cy), (cx + 18, cy)], w=5)
            for x in (-9, 0, 9):
                p.line([(cx + x, cy - 4), (cx + x, cy + 4)], w=3)
        else:
            p.arc(cx, cy - 6, 16, 10, 25, 155, w=5)
        return
    if shape == "smile":
        p.arc(cx, cy - 8, 22, 14, 15, 165, w=6)
        return
    dims = {"half": (13, 6), "open": (15, 13), "wide_e": (22, 7), "round_o": (10, 12),
            "small_u": (6, 6), "wide_i": (24, 4)}[shape]
    rx, ry = dims[0] * k, dims[1] * k
    if robot:
        p.rrect(cx - rx - 2, cy - ry - 2, cx + rx + 2, cy + ry + 2, min(rx, ry) * 0.6 + 2, fill=INK)
        if ry > 5:
            p.line([(cx - rx + 2, cy), (cx + rx - 2, cy)], w=2, fill=MID)
    else:
        p.ell(cx, cy, rx + 1, ry + 1, fill=INK, outline=None)
        if ry >= 10:
            p.ell(cx, cy + ry * 0.55, rx * 0.6, ry * 0.32, fill=MID, outline=None)

# ---------- frame composer ----------
def quantize(img):
    lut = []
    for v in range(256):
        lut.append(min(LEVELS, key=lambda L: abs(L - v)))
    return img.point(lut)

def frame(char, eye="open", gaze=(0, 0), mouth_shape="closed", mood="neutral", brow=None,
          extras=None, dx=0, dy=0, rot=0.0, draw_mouth=True, blush_on=False):
    big = Image.new("L", (P(W), P(H)), WHITE)
    p = Pen(big)
    CHARS[char](p)
    eyes(p, eye, char, gaze)
    if brow:
        brows(p, brow)
    if blush_on:
        blush(p)
    if draw_mouth:
        mouth(p, mouth_shape, char, mood)
    for fn in extras or []:
        fn(p)
    if rot or dx or dy:
        big = big.rotate(rot, resample=Image.BICUBIC, center=(P(160), P(200)),
                         translate=(P(dx), P(dy)), fillcolor=WHITE)
    small = big.resize((W, H), Image.LANCZOS)
    return quantize(small)

def anims(char):
    A = {}
    A["neutral"] = ([frame(char), frame(char, eye="blink"), frame(char)], [2400, 120, 600])
    fr = []
    for i, dy in enumerate([0, -4, -6, -3]):
        def sp(p, i=i):
            for j, (x, y) in enumerate([(30, 40), (292, 52), (28, 170), (290, 180)]):
                star(p, x, y, [5, 9, 12, 8][(i + j) % 4])
        fr.append(frame(char, eye="happy", mouth_shape="open", dy=dy, extras=[sp]))
    A["happy"] = (fr, [160] * 4)
    fr = []
    for i, ty in enumerate([126, 140, 154, 168, 182]):
        def tear(p, ty=ty):
            p.poly([(104, ty - 9), (99, ty + 2), (109, ty + 2)], fill=INK)
            p.ell(104, ty + 3, 5, 5, fill=INK, outline=None)
        fr.append(frame(char, gaze=(0, 5), mood="sad", brow="sad", extras=[tear]))
    A["sad"] = (fr, [300, 220, 220, 220, 400])
    fr = []
    for i in range(4):
        def dots(p, i=i):
            for k in range(min(i + 1, 3)):
                p.ell(250 + k * 18, 40 - k * 6, 4 + k, 4 + k, fill=INK, outline=None)
        fr.append(frame(char, gaze=(6, -7), mood="thinking", brow="think", extras=[dots]))
    A["thinking"] = (fr, [350, 350, 350, 1000])
    fr = []
    for i in range(6):
        def zz(p, i=i):
            for k in range(3):
                t = (i + k * 2) % 6
                zed(p, 240 + k * 14 + t * 2, 70 - t * 9 - k * 10, 8 + k * 3)
        fr.append(frame(char, eye="closed", mouth_shape="small_u", dy=[0, 1, 2, 2, 1, 0][i], extras=[zz]))
    A["sleeping"] = (fr, [420] * 6)
    fr = []
    for i in range(6):
        def hearts(p, i=i):
            for k, x in enumerate((44, 278)):
                y = 170 - ((i * 18 + k * 50) % 120)
                heart(p, x, y, 9 + (i + k) % 3 * 2)
        fr.append(frame(char, eye="happy", mood="pet", blush_on=True, extras=[hearts]))
    A["pet"] = (fr, [160] * 6)
    fr = []
    for i in range(8):
        def stars(p, i=i):
            for k in range(3):
                a = math.radians(i * 45 + k * 120)
                star(p, 160 + 70 * math.cos(a), 26 + 10 * math.sin(a), 7)
        fr.append(frame(char, eye="dizzy", mouth_shape="wide_i", rot=[-5, 0, 5, 0] [i % 4],
                        dx=[-5, 0, 5, 0][i % 4], extras=[stars]))
    A["shake"] = (fr, [90] * 8)
    seq = ["closed", "open", "half", "round_o", "wide_e"]
    A["speaking"] = ([frame(char, mouth_shape=s) for s in seq], [140, 160, 120, 300, 160])
    fr = []
    for i in range(3):
        def waves(p, i=i):
            for k in range(i + 1):
                p.arc(280, 116, 10 + k * 10, 16 + k * 10, -50, 50, w=4)
        fr.append(frame(char, eye="wide", extras=[waves]))
    A["listening"] = (fr, [220, 220, 620])
    A["look_left"] = ([frame(char, gaze=(g, 0)) for g in [0, -4, -8, -8, -4, 0]], [200, 90, 90, 1000, 90, 90])
    A["look_right"] = ([frame(char, gaze=(g, 0)) for g in [0, 5, 8, 5, 0]], [200, 90, 790, 300, 200])
    fr = []
    for i in range(12):
        t = i / 11
        a = math.radians(165 + t * 190)
        x, y = 137 + 140 * math.cos(a), 104 + 100 * math.sin(a)
        def env(p, x=x, y=y):
            envelope(p, x, y)
        g = (int(-6 + 12 * t), int(4 - 10 * t))
        fr.append(frame(char, gaze=g, mouth_shape="smile", extras=[env]))
    A["email"] = (fr, [260, 260, 260, 360, 300, 110, 110, 110, 110, 160, 160, 400])
    return A

ALIASES = {"email.gif": [], "happy.gif": ["happy"], "listening.gif": ["listening"], "look_left.gif": [],
           "look_right.gif": [], "neutral.gif": ["neutral"], "pet.gif": [], "sad.gif": ["sad"],
           "shake.gif": [], "sleeping.gif": [], "speaking.gif": ["speaking"], "thinking.gif": ["thinking"]}

def save_gif(frames, durs, path):
    rgb = [f.convert("RGB") for f in frames]
    pal = Image.new("P", (1, 1))
    flat = []
    for v in LEVELS:
        flat += [v, v, v]
    pal.putpalette(flat + [0, 0, 0] * (256 - len(LEVELS)))
    q = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in rgb]
    q[0].save(path, save_all=True, append_images=q[1:], duration=durs, loop=0, optimize=False, disposal=1)

def rgb565_box(img, box):
    x, y, w, h = box
    out = bytearray()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            v = img.getpixel((xx, yy))
            c = ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3)
            out += bytes((c & 0xFF, c >> 8))     # little endian, as the LVGL RGB565 image expects
    return out

def mouth_header(char):
    sets = [("neutral", "neutral", dict()), ("sad", "sad", dict(gaze=(0, 5), brow="sad")),
            ("thinking", "thinking", dict(gaze=(6, -7), brow="think")),
            ("pet", "pet", dict(eye="happy", blush_on=True))]
    x, y, w, h = MOUTH_BOX
    lines = ["// generated by make_starter_faces.py (starter face: %s). CC0. Do not edit." % char,
             "#pragma once", "#include <stdint.h>",
             "// frame index: 0 closed, 1 half, 2 open(A), 3 wide_e(E), 4 round_o(O), 5 small_u(U), 6 wide_i(I), 7 smile",
             "#define HG_MOUTH_COUNT 8"]
    total = 0
    for name, mood, kw in sets:
        names = []
        for v in VISEMES:
            if v == "smile" and name != "neutral":
                names.append("hg_mouth_%s_closed" % name)
                continue
            img = frame(char, mouth_shape=v, mood=mood, **kw)
            data = rgb565_box(img, MOUTH_BOX)
            total += len(data)
            arr = "hg_mouth_%s_%s" % (name, v)
            lines.append("static const uint8_t %s[%d] = {%s};" % (arr, len(data), ",".join(map(str, data))))
            names.append(arr)
        lines.append("static const uint8_t* const hg_mouth_%s[HG_MOUTH_COUNT] = {%s};" % (name, ",".join(names)))
    lines.append("struct HgMouthSet { const char* gif; int x, y, w, h; const uint8_t* const* frames; };")
    lines.append("static const HgMouthSet hg_mouth_sets[] = {")
    for name, _, _ in sets:
        lines.append('    {"%s", %d, %d, %d, %d, hg_mouth_%s},' % (name, x, y, w, h, name))
    lines += ["};", "#define HG_MOUTH_SET_COUNT 4", "// total pixel bytes: %d" % total]
    return "\n".join(lines) + "\n"

def layout_header(char):
    (a0, a1), (b0, b1) = SCOWL
    return ("// generated by make_starter_faces.py (starter face: %s). CC0.\n#pragma once\n"
            "#define HG_FACE_NAME \"%s\"\n"
            "#define HG_SCOWL_A0 {%d, %d}\n#define HG_SCOWL_A1 {%d, %d}\n"
            "#define HG_SCOWL_B0 {%d, %d}\n#define HG_SCOWL_B1 {%d, %d}\n"
            "#define HG_ANGER_X %d\n#define HG_ANGER_Y %d\n") % (char, char, *a0, *a1, *b0, *b1, *ANGER_XY)

def font(sz):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        if os.path.exists(f):
            return ImageFont.truetype(f, sz)
    return ImageFont.load_default()

def sheets(char, A, mouths, out):
    order = ["neutral", "happy", "sad", "thinking", "sleeping", "pet", "shake", "speaking",
             "listening", "look_left", "look_right", "email"]
    cols = 12
    sheet = Image.new("L", (W * cols, H * (len(order) + 1)), WHITE)
    for r, name in enumerate(order):
        for c, f in enumerate(A[name][0]):
            sheet.paste(f, (c * W, r * H))
    for c, f in enumerate(mouths):
        sheet.paste(f, (c * W, len(order) * H))
    sheet.save(os.path.join(out, "sheet.png"), optimize=True)
    tw, th, lab = 160, 120, 26
    prev = Image.new("RGB", (tw * cols + 150, (th + lab) * (len(order) + 1) + 50), (245, 245, 245))
    d = ImageDraw.Draw(prev)
    d.text((10, 10), "Starter face: %s  (CC0, generated by make_starter_faces.py)" % char, fill=(0, 0, 0), font=font(22))
    rows = [(n, A[n][0], A[n][1]) for n in order] + [("lip sync", mouths, VISEMES)]
    for r, (name, frs, info) in enumerate(rows):
        y = 50 + r * (th + lab)
        d.text((8, y + th // 2 - 8), name, fill=(0, 0, 0), font=font(16))
        for c, f in enumerate(frs):
            x = 150 + c * tw
            prev.paste(f.convert("RGB").resize((tw, th), Image.LANCZOS), (x, y))
            d.rectangle([x, y, x + tw - 1, y + th - 1], outline=(180, 180, 180))
            d.text((x + 4, y + th + 3), str(info[c]) + ("" if name == "lip sync" else " ms"), fill=(60, 60, 60), font=font(13))
    prev.save(os.path.join(out, "preview.png"), optimize=True)
    return prev

def main():
    out_root = sys.argv[1] if len(sys.argv) > 1 else "starter-faces-out"
    previews = []
    for char in CHARS:
        out = os.path.join(out_root, char)
        os.makedirs(os.path.join(out, "gifs"), exist_ok=True)
        A = anims(char)
        for name, (frs, durs) in A.items():
            save_gif(frs, durs, os.path.join(out, "gifs", name + ".gif"))
        with open(os.path.join(out, "gifs", "_emote_aliases.json"), "w") as f:
            json.dump(ALIASES, f, indent=2)
        with open(os.path.join(out, "hg_mouth_frames.h"), "w") as f:
            f.write(mouth_header(char))
        with open(os.path.join(out, "hg_face_layout.h"), "w") as f:
            f.write(layout_header(char))
        mouths = [frame(char, mouth_shape=v) for v in VISEMES]
        previews.append(sheets(char, A, mouths, out))
        print(char, "done")
    # combined: neutral, happy, sad, thinking, pet, speaking(open) per character
    picks = [("neutral", 0), ("happy", 2), ("sad", 2), ("thinking", 3), ("sleeping", 1), ("pet", 2),
             ("shake", 0), ("speaking", 1), ("listening", 2), ("look_left", 3), ("email", 5)]
    tw, th = 200, 150
    comb = Image.new("RGB", (120 + tw * len(picks), 60 + (th + 30) * len(CHARS)), (245, 245, 245))
    d = ImageDraw.Draw(comb)
    d.text((10, 10), "Nous-Girl public starter faces (CC0): robot (default), boy, girl", fill=(0, 0, 0), font=font(24))
    for r, char in enumerate(CHARS):
        A = anims(char)
        y = 50 + r * (th + 30)
        d.text((10, y + th // 2 - 10), char, fill=(0, 0, 0), font=font(20))
        for c, (n, i) in enumerate(picks):
            x = 120 + c * tw
            comb.paste(A[n][0][i].convert("RGB").resize((tw, th), Image.LANCZOS), (x, y))
            d.text((x + 4, y + th + 4), n, fill=(60, 60, 60), font=font(14))
    comb.save(os.path.join(out_root, "starter-faces-combined.png"), optimize=True)

if __name__ == "__main__":
    main()
