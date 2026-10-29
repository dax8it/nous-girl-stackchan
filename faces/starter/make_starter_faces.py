#!/usr/bin/env python3
"""Anime / manga style starter faces for Nous-Girl public firmware: robot, boy, girl.

Original artwork, drawn entirely from code (no source images, no reference art).
License of the generated art and this script: CC0 1.0 (public domain dedication).

Style: manga ink on white, 6-grey cel shading (screentone look), big eyes with
highlights, hair shine, hatched blush, sweat drops, sparkles, hearts.

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
S = 4                                    # supersampling factor
LEVELS = [0, 70, 140, 200, 235, 255]     # grayscale palette (black ink on white)
INK, DARK, MID, LIGHT, PALE, WHITE = LEVELS
EYE_L, EYE_R, EYE_Y = 118, 202, 104      # eye centres (screen pixels)
MOUTH_C = (160, 158)                     # mouth centre
MOUTH_BOX = (128, 140, 64, 36)           # x, y, w, h of the lip-sync overlay
ANGER_XY = (228, 34)
SCOWL = [((136, 96), (100, 84)), ((184, 96), (220, 84))]   # (inner low, outer high) per eye
LW = 4                                   # default ink line width

def P(v):
    return v * S

def bez(p0, p1, p2, p3, n=16):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u**3*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t**3*p3[0],
                    u**3*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t**3*p3[1]))
    return out

def arcpts(cx, cy, rx, ry, a0, a1, n=32):
    return [(cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))
            for a in [a0 + (a1 - a0) * i / n for i in range(n + 1)]]

class Pen:
    def __init__(self, img):
        self.img = img
        self.d = ImageDraw.Draw(img)
    def ell(self, cx, cy, rx, ry, fill=None, outline=INK, w=LW):
        self.d.ellipse([P(cx - rx), P(cy - ry), P(cx + rx), P(cy + ry)], fill=fill,
                       outline=outline, width=int(P(w)) if outline is not None else 0)
    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=INK, w=LW):
        self.d.rounded_rectangle([P(x0), P(y0), P(x1), P(y1)], radius=P(r), fill=fill,
                                 outline=outline, width=int(P(w)) if outline is not None else 0)
    def line(self, pts, w=LW, fill=INK, caps=True):
        self.d.line([(P(x), P(y)) for x, y in pts], fill=fill, width=int(P(w)), joint="curve")
        if caps:
            for x, y in (pts[0], pts[-1]):
                self.d.ellipse([P(x) - P(w) / 2, P(y) - P(w) / 2, P(x) + P(w) / 2, P(y) + P(w) / 2], fill=fill)
    def arc(self, cx, cy, rx, ry, a0, a1, w=LW, fill=INK):
        self.line(arcpts(cx, cy, rx, ry, a0, a1), w, fill)
    def poly(self, pts, fill=INK, outline=None, w=LW):
        self.d.polygon([(P(x), P(y)) for x, y in pts], fill=fill)
        if outline is not None:
            self.line(list(pts) + [pts[0]], w, outline)
    def taper(self, pts, w0, w1, fill=INK):
        """Brush stroke: thick in the middle, thin at the ends (manga ink)."""
        n = len(pts)
        left, right = [], []
        for i, (x, y) in enumerate(pts):
            a = pts[max(i - 1, 0)]; b = pts[min(i + 1, n - 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy) or 1
            t = i / (n - 1)
            w = w0 + (w1 - w0) * math.sin(math.pi * t)
            nx, ny = -dy / L * w / 2, dx / L * w / 2
            left.append((x + nx, y + ny)); right.append((x - nx, y - ny))
        self.poly(left + right[::-1], fill=fill)

def clipped(img, mask_fn, draw_fn):
    """Draw draw_fn only inside the shape that mask_fn paints (white on black)."""
    layer = img.copy()
    draw_fn(Pen(layer))
    m = Image.new("L", img.size, 0)
    mask_fn(Pen(m))
    img.paste(layer, (0, 0), m)

# ---------- manga symbols ----------
def sparkle(p, cx, cy, r, fill=INK):
    pts = []
    for i in range(8):
        a = math.pi * i / 4 - math.pi / 2
        rr = r if i % 2 == 0 else r * 0.2
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    p.poly(pts, fill=fill)

def heart(p, cx, cy, r, filled=True):
    pts = [(cx + r * math.sin(t) ** 3,
            cy - r * (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) / 16)
           for t in [i * 2 * math.pi / 60 for i in range(60)]]
    if filled:
        p.poly(pts, fill=INK)
        p.ell(cx - r * 0.42, cy - r * 0.42, r * 0.18, r * 0.14, fill=WHITE, outline=None)
    else:
        p.poly(pts, fill=PALE, outline=INK, w=2.5)

def drop(p, cx, cy, s, fill=PALE):
    """Sweat drop / tear: point up, round bottom."""
    pts = [(cx, cy - 1.9 * s)] + arcpts(cx, cy, s, s, -30, 210, 20)
    p.poly(pts, fill=fill, outline=INK, w=2.2)
    p.ell(cx - s * 0.35, cy - s * 0.1, s * 0.22, s * 0.3, fill=WHITE, outline=None)

def zed(p, x, y, s, w=3):
    p.line([(x, y), (x + s, y), (x, y + s), (x + s, y + s)], w=w)

def qmark(p, x, y, s):
    p.arc(x, y, s, s, 180, 400, w=3.5)
    p.line([(x + s * 0.77, y + s * 0.64), (x, y + s * 1.2), (x, y + s * 1.6)], w=3.5)
    p.ell(x, y + s * 2.3, 2.4, 2.4, fill=INK, outline=None)

def anger(p, cx, cy, r):
    """Manga cross-popping vein (four bent strokes)."""
    for k in range(4):
        a = math.radians(45 + 90 * k)
        ox, oy = cx + r * 0.75 * math.cos(a), cy + r * 0.75 * math.sin(a)
        p.arc(ox, oy, r * 0.55, r * 0.55, math.degrees(a) + 135, math.degrees(a) + 225, w=3.2)

def envelope(p, x, y):
    p.rrect(x, y, x + 46, y + 32, 4, fill=WHITE, w=3)
    p.poly([(x + 3, y + 3), (x + 23, y + 19), (x + 43, y + 3)], fill=PALE, outline=INK, w=2.5)
    heart(p, x + 23, y + 17, 5)

def blush(p, style, strong=False):
    for cx in (100, 220):
        cy = 142
        p.ell(cx, cy, 15, 8, fill=LIGHT if not strong else MID, outline=None)
        for k in range(-2, 3) if strong else range(-1, 2):
            x = cx + k * 6
            p.line([(x + 3, cy - 5), (x - 3, cy + 5)], w=1.8)

# ---------- characters (drawn back to front) ----------
FACE = ([(78, 116)] + arcpts(160, 116, 82, 84, 180, 360)[1:] +
        bez((242, 116), (242, 160), (204, 196), (160, 207))[1:] +
        bez((160, 207), (116, 196), (78, 160), (78, 116))[1:])

def neck_human(p, collar):
    p.rrect(140, 186, 180, 250, 6, fill=WHITE, w=4)
    p.poly([(141, 196)] + bez((141, 196), (150, 214), (170, 214), (179, 196)) , fill=LIGHT)
    if collar == "boy":       # hoodie collar
        p.poly([(96, 250), (112, 222), (140, 216), (160, 236), (180, 216), (208, 222), (224, 250)], fill=MID, outline=INK, w=4)
        p.line([(140, 216), (160, 236), (180, 216)], w=4)
        p.line([(150, 228), (148, 248)], w=2.5); p.line([(170, 228), (172, 248)], w=2.5)
    else:                     # sailor-ish collar with a star pin
        p.poly([(92, 250), (110, 222), (142, 214), (160, 240), (178, 214), (210, 222), (228, 250)], fill=WHITE, outline=INK, w=4)
        p.line([(112, 232), (146, 222)], w=2.5); p.line([(208, 232), (174, 222)], w=2.5)
        sparkle(p, 160, 232, 7)

def face_base(p):
    p.poly(FACE, fill=WHITE, outline=INK, w=4)
    # cel shadow down the right cheek (light from top-left)
    clipped(p.img, lambda m: m.poly(FACE, fill=255),
            lambda q: q.poly(bez((244, 100), (236, 160), (206, 200), (156, 212)) + [(260, 212), (260, 100)], fill=PALE))
    p.line(FACE + [FACE[0]], 4)

def hair_shine(p, mask_poly, cx, cy, rx, ry, a0, a1, segs, fill):
    def draw(q):
        span = (a1 - a0) / segs
        for i in range(segs):
            s0 = a0 + i * span + span * 0.18
            s1 = a0 + (i + 1) * span - span * 0.18
            pts = arcpts(cx, cy, rx, ry, s0, s1, 8)
            q.taper(pts, 1, 7, fill=fill)
    clipped(p.img, lambda m: m.poly(mask_poly, fill=255), draw)

def bangs(tips, notches, start, end):
    """Hairline: right side start -> alternating tips/notches -> left side end."""
    pts = [start]
    seq = []
    for i, t in enumerate(tips):
        seq.append(t)
        if i < len(notches):
            seq.append(notches[i])
    prev = start
    for q in seq:
        mx, my = (prev[0] + q[0]) / 2, (prev[1] + q[1]) / 2
        pts.append((mx + 2.5, my))           # slight bend so strands curve
        pts.append(q)
        prev = q
    pts.append(end)
    return pts

def body_boy(p):
    # back hair: big swept spikes at the crown and sides
    back = [(70, 132), (52, 104), (40, 112), (50, 76), (34, 58), (66, 52), (70, 26), (100, 34),
            (120, 6), (148, 22), (178, 2), (196, 24), (230, 14), (234, 42), (270, 44), (262, 70),
            (286, 92), (268, 104), (278, 124), (250, 132)]
    p.poly(back, fill=DARK, outline=INK, w=4)
    p.ell(74, 126, 12, 17, fill=WHITE, w=4); p.ell(246, 126, 12, 17, fill=WHITE, w=4)   # ears
    p.arc(74, 126, 5, 9, 120, 240, w=2.5); p.arc(246, 126, 5, 9, -60, 60, w=2.5)
    neck_human(p, "boy")
    face_base(p)
    top = arcpts(160, 112, 90, 94, 180, 360)
    fringe = bangs(tips=[(242, 150), (218, 78), (190, 76), (162, 92), (134, 76), (104, 78), (80, 150)],
                   notches=[(228, 66), (204, 56), (176, 60), (148, 58), (120, 62), (92, 74)],
                   start=(250, 112), end=(70, 112))
    poly = top + fringe
    p.poly(poly, fill=MID)
    clipped(p.img, lambda m: m.poly(poly, fill=255),
            lambda q: q.poly([(60, 10), (260, 10), (260, 40)] + arcpts(160, 140, 110, 100, 320, 220, 20) + [(60, 40)], fill=DARK))
    hair_shine(p, poly, 160, 112, 66, 70, 205, 335, 5, WHITE)
    p.line(poly + [poly[0]], 4)
    for x0, x1 in ((190, 176), (134, 146), (216, 206)):          # strand lines
        p.line(bez((x0, 70), (x0 - 2, 60), (x1, 50), (x1 + 4, 40), 8), w=2)
    p.line(bez((156, 22), (150, 4), (176, -2), (184, 12), 12), w=5)      # cowlick
    p.line(bez((156, 22), (160, 10), (174, 6), (184, 12), 12), w=2, fill=MID)

def body_girl(p):
    back = ([(56, 250), (54, 140)] + arcpts(160, 112, 104, 100, 180, 360)[1:] + [(266, 140), (264, 250)])
    p.poly(back, fill=DARK, outline=INK, w=4)
    neck_human(p, "girl")
    face_base(p)
    top = arcpts(160, 112, 92, 94, 180, 360)
    fringe = bangs(tips=[(226, 80), (200, 78), (176, 86), (156, 80), (132, 86), (110, 78), (94, 80)],
                   notches=[(214, 64), (188, 62), (166, 66), (144, 64), (122, 62), (100, 68)],
                   start=(252, 112), end=(68, 112))
    poly = top + fringe
    p.poly(poly, fill=DARK)
    # long side locks framing the face
    for sgn in (-1, 1):
        def X(x): return 160 + sgn * (x - 160)
        lock = [(X(70), 100), (X(98), 112), (X(100), 160), (X(92), 214), (X(84), 238), (X(74), 200), (X(64), 140)]
        p.poly(lock, fill=DARK, outline=INK, w=4)
        p.line(bez((X(84), 118), (X(90), 160), (X(86), 200), (X(82), 226), 10), w=2, fill=MID)
    hair_shine(p, poly, 160, 112, 68, 70, 205, 335, 5, LIGHT)
    p.line(poly + [poly[0]], 4)
    for x0, x1 in ((176, 168), (132, 140)):
        p.line(bez((x0, 84), (x0, 70), (x1, 56), (x1 - 2, 42), 8), w=2, fill=MID)
    # ribbon bow, top left
    bx, by = 92, 42
    p.poly([(bx, by), (bx - 26, by - 18), (bx - 30, by + 8)], fill=WHITE, outline=INK, w=3.5)
    p.poly([(bx, by), (bx + 22, by - 24), (bx + 30, by + 2)], fill=WHITE, outline=INK, w=3.5)
    p.poly([(bx - 4, by + 4), (bx - 14, by + 30), (bx - 4, by + 26), (bx + 2, by + 32), (bx + 4, by + 6)], fill=LIGHT, outline=INK, w=3)
    p.line([(bx - 6, by - 2), (bx - 20, by - 8)], w=2); p.line([(bx + 6, by - 4), (bx + 18, by - 14)], w=2)
    p.ell(bx, by, 7, 6, fill=LIGHT, w=3.5)

def body_robot(p):
    # swept antenna fins
    for sgn in (-1, 1):
        def X(x): return 160 + sgn * (x - 160)
        p.poly([(X(80), 90), (X(34), 22), (X(46), 70), (X(64), 112)], fill=WHITE, outline=INK, w=4)
        p.poly([(X(70), 88), (X(44), 46), (X(52), 76)], fill=MID)
    # neck joint
    p.rrect(130, 196, 190, 250, 6, fill=MID, w=4)
    for y in (210, 222, 234):
        p.line([(134, y), (186, y)], w=2.5)
    p.poly([(80, 250), (104, 226), (216, 226), (240, 250)], fill=WHITE, outline=INK, w=4)
    p.line([(118, 238), (202, 238)], w=2, fill=MID)
    # helmet shell
    p.rrect(56, 28, 264, 208, 50, fill=LIGHT, w=5)
    clipped(p.img, lambda m: m.rrect(56, 28, 264, 208, 50, fill=255, outline=None),
            lambda q: q.ell(110, 40, 46, 16, fill=WHITE, outline=None))       # specular
    for y in (120, 132, 144):                                               # side vents
        p.line([(62, y), (72, y)], w=2.5); p.line([(248, y), (258, y)], w=2.5)
    # crest + gem
    p.poly([(144, 30), (160, 4), (176, 30)], fill=WHITE, outline=INK, w=4)
    p.poly([(152, 28), (160, 14), (168, 28)], fill=MID)
    p.ell(160, 42, 9, 8, fill=PALE, w=3.5)
    p.ell(157, 39, 3, 2.5, fill=WHITE, outline=None)
    # faceplate
    p.rrect(78, 56, 242, 196, 40, fill=WHITE, w=4)
    clipped(p.img, lambda m: m.rrect(78, 56, 242, 196, 40, fill=255, outline=None),
            lambda q: q.poly(bez((244, 90), (236, 160), (210, 196), (150, 202)) + [(250, 204), (250, 90)], fill=PALE))
    p.rrect(78, 56, 242, 196, 40, outline=INK, w=4)
    p.line([(104, 184), (216, 184)], w=2, fill=MID)                         # chin seam
    for sgn in (-1, 1):                                                     # cheek vents
        for k in range(3):
            x = 160 + sgn * (62 + k * 6)
            p.line([(x, 160), (x - sgn * 4, 172)], w=2, fill=MID)

CHARS = {"robot": body_robot, "boy": body_boy, "girl": body_girl}

# ---------- eyes ----------
def eye_shape(cx, style):
    if style == "robot":
        return None
    return arcpts(cx, EYE_Y + 6, 21, 25, 0, 360, 40)

def draw_eye_open(p, cx, side, style, gaze, wide=False, teary=False):
    gx, gy = gaze
    ix, iy = cx + gx, EYE_Y + 8 + gy
    if style == "robot":
        def socket(m, fill=255):
            m.rrect(cx - 22, EYE_Y - 22, cx + 22, EYE_Y + 28, 16, fill=fill, outline=None)
        p.rrect(cx - 22, EYE_Y - 22, cx + 22, EYE_Y + 28, 16, fill=WHITE, outline=None)
        def lens(q):
            r = 16 if not wide else 15
            q.ell(ix, iy, r, r + 2, fill=DARK, outline=INK, w=2.5)
            q.ell(ix, iy, r - 5, r - 4, fill=MID, outline=None)
            q.ell(ix, iy, 6, 7, fill=INK, outline=None)
            for k in range(6):                       # aperture blades
                a = math.radians(30 + 60 * k)
                q.line([(ix + 7 * math.cos(a), iy + 8 * math.sin(a)), (ix + 11 * math.cos(a + 0.5), iy + 12 * math.sin(a + 0.5))], w=1.6, fill=INK, caps=False)
            q.poly([(cx - 30, EYE_Y - 30), (cx + 30, EYE_Y - 30), (cx + 30, EYE_Y - 12), (cx - 30, EYE_Y - 4)], fill=INK)   # visor shade
            q.ell(ix - 6, iy - 7, 5, 6, fill=WHITE, outline=None)
            q.ell(ix + 6, iy + 8, 2.5, 2.5, fill=WHITE, outline=None)
            if wide:
                sparkle(q, ix + 6, iy - 4, 6, fill=WHITE)
        clipped(p.img, socket, lens)
        p.rrect(cx - 22, EYE_Y - 22, cx + 22, EYE_Y + 28, 16, outline=INK, w=5)
        return
    shape = eye_shape(cx, style)
    p.poly(shape, fill=WHITE)
    def iris(q):
        rx, ry = (16, 22) if not wide else (15, 21)
        q.ell(ix, iy, rx, ry, fill=DARK, outline=INK, w=2.2)
        clipped(q.img, lambda m: m.ell(ix, iy, rx, ry, fill=255, outline=None),
                lambda r: (r.ell(ix, iy + 9, rx * 0.9, ry * 0.6, fill=MID, outline=None),
                           r.ell(ix, iy + 14, rx * 0.6, ry * 0.3, fill=LIGHT, outline=None)))
        q.ell(ix, iy - 2, 5.5, 8, fill=INK, outline=None)
        clipped(q.img, lambda m: m.ell(ix, iy, rx, ry, fill=255, outline=None),
                lambda r: r.ell(ix, iy - ry - 4, rx + 4, 13, fill=INK, outline=None))   # lid shadow on the iris
        q.ell(ix - 5, iy - 8, 5.5, 6.5, fill=WHITE, outline=None)
        q.ell(ix + 6, iy + 7, 2.6, 2.6, fill=WHITE, outline=None)
        if wide:
            sparkle(q, ix + 5, iy - 6, 6, fill=WHITE)
            q.ell(ix - 6, iy + 9, 1.8, 1.8, fill=WHITE, outline=None)
        if teary:
            q.line([(cx - 18, EYE_Y + 22), (cx - 8, EYE_Y + 19), (cx + 2, EYE_Y + 23), (cx + 12, EYE_Y + 19), (cx + 20, EYE_Y + 22)], w=2.6, fill=WHITE)
    clipped(p.img, lambda m: m.poly(shape, fill=255), iris)
    # upper lid: tapered brush stroke, heavy at the outer corner
    lid = arcpts(cx, EYE_Y + 6, 23, 27, 185 if side < 0 else 200, 340 if side < 0 else 355, 24)
    if side > 0:
        pass
    p.taper(lid, 2, 7.5)
    ox = cx + side * 23
    if style == "girl":
        p.taper([(ox - side * 3, EYE_Y - 3), (ox + side * 4, EYE_Y - 7), (ox + side * 9, EYE_Y - 8)], 1, 3.5)
    # lower lash, outer half only
    low = arcpts(cx, EYE_Y + 6, 19, 25, 90 + side * -10, 90 + side * -60, 10) if side > 0 else arcpts(cx, EYE_Y + 6, 19, 25, 100, 150, 10)
    p.line(low, w=2)

def eyes(p, kind, style, gaze=(0, 0), teary=False):
    for cx, side in ((EYE_L, -1), (EYE_R, 1)):
        if kind in ("open", "wide"):
            draw_eye_open(p, cx, side, style, gaze, wide=(kind == "wide"), teary=teary)
        elif kind == "blink":
            p.taper(arcpts(cx, EYE_Y + 2, 20, 8, 10, 170, 16), 2, 6)
            p.line([(cx + side * 19, EYE_Y + 5), (cx + side * 25, EYE_Y + 2)], w=2.5)
        elif kind == "happy":                       # ^ ^
            p.taper(arcpts(cx, EYE_Y + 14, 18, 18, 195, 345, 18), 3, 7)
        elif kind == "closed":                      # sleeping: soft u with lashes
            p.taper(arcpts(cx, EYE_Y - 2, 18, 12, 15, 165, 18), 2, 6)
            for k in (-8, 0, 8):
                x = cx + k
                p.line([(x, EYE_Y + 10), (x + k * 0.15, EYE_Y + 15)], w=2)
        elif kind == "dizzy":
            pts = [(cx + (1 + t * 1.0) * math.cos(t * side), EYE_Y + 4 + (1 + t * 1.0) * math.sin(t * side))
                   for t in [i * 0.25 for i in range(76)]]
            p.line(pts, w=3.2)

BROWS = {
    None:    [((102, 70), (134, 68)), ((186, 68), (218, 70))],
    "sad":   [((102, 74), (136, 62)), ((184, 62), (218, 74))],
    "think": [((102, 74), (136, 74)), ((186, 62), (220, 54))],
}
def brows(p, kind, style):
    w = {"robot": 5, "boy": 4.5, "girl": 3.2}[style]
    for a, b in BROWS[kind]:
        if style == "robot":
            p.line([a, b], w=w)
        else:
            mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 - 2.5)
            p.taper(bez(a, mid, mid, b, 10), 1.5, w)

# ---------- mouths (drawn inside MOUTH_BOX) ----------
VISEMES = ["closed", "half", "open", "wide_e", "round_o", "small_u", "wide_i", "smile"]
DIMS = {"half": (10, 6), "open": (12, 11), "wide_e": (16, 7), "round_o": (8, 10),
        "small_u": (5, 5), "wide_i": (17, 4)}

def dshape(cx, cy, rx, ry, flat):
    pts = []
    for i in range(48):
        t = 2 * math.pi * i / 48
        s = math.sin(t)
        pts.append((cx + rx * math.cos(t), cy + ry * s * (1 if s > 0 else flat)))
    return pts

def nose(p, style):
    if style != "robot":
        p.line([(163, 128), (160, 135), (165, 136)], w=2.2)

def mouth(p, shape, style, mood="neutral"):
    cx, cy = MOUTH_C
    if mood == "thinking":
        cx += 6
    k = 0.85 if mood == "thinking" else 1.0
    robot = style == "robot"
    if shape == "closed":
        if mood == "sad":
            p.line([(cx - 11, cy + 4), (cx - 5, cy - 1), (cx, cy + 1), (cx + 5, cy - 1), (cx + 11, cy + 4)], w=3)
        elif mood == "thinking":
            p.line([(cx - 10 + i * 2.5, cy + 2 * math.sin(i * 1.2)) for i in range(9)], w=3)
        elif mood == "pet":                         # cat mouth
            p.arc(cx - 5, cy - 2, 5, 5, 0, 180, w=3); p.arc(cx + 5, cy - 2, 5, 5, 0, 180, w=3)
        elif robot:
            p.arc(cx, cy - 10, 16, 10, 30, 150, w=4)
            p.line([(cx - 16, cy - 6), (cx - 18, cy - 9)], w=3); p.line([(cx + 16, cy - 6), (cx + 18, cy - 9)], w=3)
        else:
            p.arc(cx, cy - 7, 10, 7, 30, 150, w=3)
        return
    if shape == "smile":
        if robot:
            p.arc(cx, cy - 12, 22, 14, 20, 160, w=4.5)
        else:
            p.poly(dshape(cx, cy - 2, 14, 9, 0.15), fill=DARK, outline=INK, w=3)
            clipped(p.img, lambda m: m.poly(dshape(cx, cy - 2, 14, 9, 0.15), fill=255),
                    lambda q: q.ell(cx, cy + 7, 8, 4, fill=LIGHT, outline=None))
        return
    rx, ry = DIMS[shape][0] * k, DIMS[shape][1] * k
    if robot:
        p.rrect(cx - rx - 3, cy - ry - 3, cx + rx + 3, cy + ry + 3, min(rx, ry) * 0.6 + 2, fill=DARK, w=3)
        n = max(1, int(ry / 3))
        for j in range(n):
            y = cy - ry + (j + 0.5) * (2 * ry / n)
            p.line([(cx - rx + 2, y), (cx + rx - 2, y)], w=1.6, fill=LIGHT)
        return
    round_ = shape in ("round_o", "small_u")
    shp = dshape(cx, cy, rx, ry, 1.0 if round_ else 0.45) if True else None
    p.poly(shp, fill=DARK, outline=INK, w=3 if ry > 4 else 2.5)
    def inner(q):
        if ry >= 6:
            q.ell(cx + 1, cy + ry * 0.75, rx * 0.65, ry * 0.45, fill=LIGHT, outline=None)    # tongue
        if shape in ("wide_e", "wide_i", "open"):
            q.poly([(cx - rx - 2, cy - ry - 2), (cx + rx + 2, cy - ry - 2), (cx + rx + 2, cy - ry * 0.25), (cx - rx - 2, cy - ry * 0.25)], fill=WHITE)  # teeth
    clipped(p.img, lambda m: m.poly(shp, fill=255), inner)
    p.line(shp + [shp[0]], 3 if ry > 4 else 2.5)

# ---------- frame composer ----------
def quantize(img):
    lut = [min(LEVELS, key=lambda L: abs(L - v)) for v in range(256)]
    return img.point(lut)

def frame(char, eye="open", gaze=(0, 0), mouth_shape="closed", mood="neutral", brow=None,
          extras=None, under=None, dx=0, dy=0, rot=0.0, draw_mouth=True, blush_on=False, strong_blush=False, teary=False):
    big = Image.new("L", (P(W), P(H)), WHITE)
    p = Pen(big)
    for fn in under or []:
        fn(p)
    CHARS[char](p)
    if blush_on:
        blush(p, char, strong_blush)
    eyes(p, eye, char, gaze, teary)
    brows(p, brow, char)
    nose(p, char)
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
            for j, (x, y) in enumerate([(30, 44), (290, 52), (26, 166), (294, 176), (52, 110), (270, 112)]):
                sparkle(p, x, y, [6, 10, 14, 9][(i + j) % 4])
        fr.append(frame(char, eye="happy", mouth_shape="open", dy=dy, blush_on=True, extras=[sp]))
    A["happy"] = (fr, [160] * 4)
    fr = []
    for i, ty in enumerate([136, 148, 160, 172, 186]):
        def tear(p, ty=ty):
            drop(p, 106, ty, 8)
            drop(p, 214, ty + 8 if ty < 180 else 140, 7.5)
        fr.append(frame(char, gaze=(0, 4), mood="sad", brow="sad", teary=True, extras=[tear]))
    A["sad"] = (fr, [300, 220, 220, 220, 400])
    fr = []
    for i in range(4):
        def dots(p, i=i):
            for k in range(min(i + 1, 3)):
                p.ell(254 + k * 16, 44 - k * 8, 3.5 + k, 3.5 + k, fill=INK, outline=None)
            if i == 3:
                qmark(p, 284, 24, 7)
            drop(p, 250, 82, 6)
        fr.append(frame(char, gaze=(6, -7), mood="thinking", brow="think", extras=[dots]))
    A["thinking"] = (fr, [350, 350, 350, 1000])
    fr = []
    for i in range(6):
        def zz(p, i=i):
            for k in range(3):
                t = (i + k * 2) % 6
                zed(p, 246 + k * 14 + t * 2, 70 - t * 9 - k * 10, 8 + k * 3)
            r = [4, 7, 10, 13, 15, 0][i]                     # nose bubble, pops on the last frame
            if r:
                p.ell(186, 128 - r * 0.3, r, r, fill=PALE, w=2.5)
                p.arc(186, 128 - r * 0.3, r * 0.6, r * 0.6, 200, 260, w=2, fill=WHITE)
            else:
                for a in range(0, 360, 60):
                    x, y = 186 + 12 * math.cos(math.radians(a)), 126 + 12 * math.sin(math.radians(a))
                    p.line([(x, y), (x + 4 * math.cos(math.radians(a)), y + 4 * math.sin(math.radians(a)))], w=2)
        fr.append(frame(char, eye="closed", mouth_shape="small_u", dy=[0, 1, 2, 2, 1, 0][i], extras=[zz]))
    A["sleeping"] = (fr, [420] * 6)
    fr = []
    for i in range(6):
        def hearts(p, i=i):
            for k, x in enumerate((40, 282)):
                y = 176 - ((i * 18 + k * 50) % 120)
                heart(p, x + 4 * math.sin(i + k), y, 10 + (i + k) % 3 * 2, filled=(k + i // 3) % 2 == 0)
            sparkle(p, 160 + 110 * math.cos(i), 24, 6)
        fr.append(frame(char, eye="happy", mood="pet", blush_on=True, strong_blush=True, extras=[hearts]))
    A["pet"] = (fr, [160] * 6)
    fr = []
    for i in range(8):
        def stars(p, i=i):
            for k in range(3):
                a = math.radians(i * 45 + k * 120)
                sparkle(p, 160 + 72 * math.cos(a), 22 + 10 * math.sin(a), 8)
            drop(p, 40 - (i % 4) * 3, 90 + (i % 4) * 4, 6); drop(p, 282 + (i % 4) * 3, 96 + (i % 4) * 4, 5)
            for sgn in (-1, 1):                               # motion lines
                for j in range(3):
                    x = 160 + sgn * (112 + j * 7)
                    p.line([(x, 150 + j * 10), (x + sgn * 8, 150 + j * 10)], w=2.5)
        fr.append(frame(char, eye="dizzy", mouth_shape="wide_i", mood="neutral", rot=[-5, 0, 5, 0][i % 4],
                        dx=[-5, 0, 5, 0][i % 4], extras=[stars]))
    A["shake"] = (fr, [90] * 8)
    seq = ["closed", "open", "half", "round_o", "wide_e"]
    A["speaking"] = ([frame(char, mouth_shape=s) for s in seq], [140, 160, 120, 300, 160])
    fr = []
    for i in range(3):
        def waves(p, i=i):
            for k in range(i + 1):
                p.arc(282, 118, 8 + k * 9, 14 + k * 10, -50, 50, w=3.5)
            if i == 0:
                p.line([(296, 40), (293, 62)], w=5); p.ell(292, 72, 3, 3, fill=INK, outline=None)
            sparkle(p, 36, 60 + i * 8, 7 + i * 2)
        fr.append(frame(char, eye="wide", extras=[waves]))
    A["listening"] = (fr, [220, 220, 620])
    A["look_left"] = ([frame(char, gaze=(g, 0)) for g in [0, -4, -7, -7, -4, 0]], [200, 90, 90, 1000, 90, 90])
    A["look_right"] = ([frame(char, gaze=(g, 0)) for g in [0, 4, 7, 4, 0]], [200, 90, 790, 300, 200])
    fr = []
    for i in range(12):
        t = i / 11
        a = math.radians(165 + t * 190)
        x, y = 137 + 140 * math.cos(a), 104 + 100 * math.sin(a)
        def env(p, x=x, y=y, i=i):
            envelope(p, x, y)
            sparkle(p, x + 52, y - 4, 5 + (i % 3) * 2)
            sparkle(p, x - 8, y + 34, 4 + ((i + 1) % 3) * 2)
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

MOUTH_SETS = [("neutral", "neutral", dict()), ("sad", "sad", dict(gaze=(0, 4), brow="sad", teary=True)),
              ("thinking", "thinking", dict(gaze=(6, -7), brow="think")),
              ("pet", "pet", dict(eye="happy", blush_on=True, strong_blush=True))]

def mouth_header(char):
    x, y, w, h = MOUTH_BOX
    lines = ["// generated by make_starter_faces.py (starter face: %s). CC0. Do not edit." % char,
             "#pragma once", "#include <stdint.h>",
             "// frame index: 0 closed, 1 half, 2 open(A), 3 wide_e(E), 4 round_o(O), 5 small_u(U), 6 wide_i(I), 7 smile",
             "#define HG_MOUTH_COUNT 8"]
    total = 0
    for name, mood, kw in MOUTH_SETS:
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
    for name, _, _ in MOUTH_SETS:
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

ORDER = ["neutral", "happy", "sad", "thinking", "sleeping", "pet", "shake", "speaking",
         "listening", "look_left", "look_right", "email"]

def sheets(char, A, mouths, out):
    cols = 12
    sheet = Image.new("L", (W * cols, H * (len(ORDER) + 1)), WHITE)
    for r, name in enumerate(ORDER):
        for c, f in enumerate(A[name][0]):
            sheet.paste(f, (c * W, r * H))
    for c, f in enumerate(mouths):
        sheet.paste(f, (c * W, len(ORDER) * H))
    sheet.save(os.path.join(out, "sheet.png"), optimize=True)
    tw, th, lab = 160, 120, 26
    prev = Image.new("RGB", (tw * cols + 150, (th + lab) * (len(ORDER) + 1) + 50), (245, 245, 245))
    d = ImageDraw.Draw(prev)
    d.text((10, 10), "Starter face: %s  (anime style, CC0, generated by make_starter_faces.py)" % char, fill=(0, 0, 0), font=font(22))
    rows = [(n, A[n][0], A[n][1]) for n in ORDER] + [("lip sync", mouths, VISEMES)]
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
    only = sys.argv[2:] or list(CHARS)
    allA = {}
    for char in only:
        out = os.path.join(out_root, char)
        os.makedirs(os.path.join(out, "gifs"), exist_ok=True)
        A = anims(char); allA[char] = A
        for name, (frs, durs) in A.items():
            save_gif(frs, durs, os.path.join(out, "gifs", name + ".gif"))
        with open(os.path.join(out, "gifs", "_emote_aliases.json"), "w") as f:
            json.dump(ALIASES, f, indent=2)
        with open(os.path.join(out, "hg_mouth_frames.h"), "w") as f:
            f.write(mouth_header(char))
        with open(os.path.join(out, "hg_face_layout.h"), "w") as f:
            f.write(layout_header(char))
        mouths = [frame(char, mouth_shape=v) for v in VISEMES]
        sheets(char, A, mouths, out)
        print(char, "done")
    picks = [("neutral", 0), ("happy", 2), ("sad", 2), ("thinking", 3), ("sleeping", 2), ("pet", 2),
             ("shake", 0), ("speaking", 1), ("listening", 2), ("look_left", 3), ("email", 5)]
    tw, th = 200, 150
    comb = Image.new("RGB", (120 + tw * len(picks), 60 + (th + 30) * len(only)), (245, 245, 245))
    d = ImageDraw.Draw(comb)
    d.text((10, 10), "Nous-Girl public starter faces, anime style (CC0): robot (default), boy, girl", fill=(0, 0, 0), font=font(24))
    for r, char in enumerate(only):
        A = allA[char]
        y = 50 + r * (th + 30)
        d.text((10, y + th // 2 - 10), char, fill=(0, 0, 0), font=font(20))
        for c, (n, i) in enumerate(picks):
            x = 120 + c * tw
            comb.paste(A[n][0][i].convert("RGB").resize((tw, th), Image.LANCZOS), (x, y))
            d.text((x + 4, y + th + 4), n, fill=(60, 60, 60), font=font(14))
    comb.save(os.path.join(out_root, "starter-faces-combined.png"), optimize=True)

if __name__ == "__main__":
    main()
