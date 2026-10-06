"""Builds Avriella's favourite toys in Blender and writes them for the game: a stuffed Bluey, a little television and a laptop that pop out of the ground.

Run with the `bpy` Python module (pip install bpy==4.2.0 Pillow, Python 3.11) or with Blender:
    python3 tools/avriella/build_toys.py
    blender -b --python tools/avriella/build_toys.py
It writes assets/avriella/toys.blend, toys.glb, the textures as PNG in assets/avriella/toys/ and the game's data, shared/avriella_toys.h. Then
tools/avriella/render_toys.py makes preview pictures. Everything is made from code so it can be rebuilt and tweaked.

Both toys are stiff parts joined at pivots (no skin): the plush is a body with a head, two ears, two arms, two legs and a tail, and the TV is a box
with a curved screen, two aerials and little knobs, and the laptop is a deck and a lid on a hinge. The game moves the parts (see shared/avriella_toys.h and DrawToy in RoyaleMod.cpp), so every
move the plush makes (a hug, a shake, a dance) is a few angles, not a baked clip. The look is the N64 (Ocarina of Time) way: a few hundred
triangles, flat colours from tiny 8x8 swatches with a little fabric noise, and nothing smoother than it has to be. The plush is a fan toy of
Bluey, the blue heeler pup of the television show.

Blender's Z is up and the toys face -Y; the export turns that into the game's axes (Y up, facing +Z). Sizes are metres here, game units x100.
"""
import math
import os
import random

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "assets", "avriella")
TEX_DIR = os.path.join(OUT, "toys")
HEADER = os.path.join(ROOT, "shared", "avriella_toys.h")
SCALE = 100.0
MAX_BATCH = 32

# ---------------------------------------------------------------------------------------------------------------------------------------
# Textures (RGBA16 in the game; 64x32 or smaller so each fits the graphics chip's 4 KB at once)
# ---------------------------------------------------------------------------------------------------------------------------------------
def quant(c):
    return tuple(round(max(0.0, min(1.0, v)) * 31) / 31 for v in c[:3])


def mixc(a, b, k):
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(3))


PLUSH_PAL = [   # 8 swatches of 8x8: (name, colour)
    ("blue", (0.36, 0.62, 0.86)), ("belly", (0.72, 0.85, 0.96)), ("navy", (0.19, 0.27, 0.55)), ("tan", (0.95, 0.76, 0.42)),
    ("black", (0.07, 0.07, 0.10)), ("white", (0.98, 0.98, 0.98)), ("pink", (0.93, 0.52, 0.55)), ("shade", (0.27, 0.49, 0.78))]
TV_PAL = [
    ("cream", (0.92, 0.86, 0.70)), ("teal", (0.28, 0.60, 0.60)), ("glass", (0.07, 0.10, 0.12)), ("chrome", (0.72, 0.74, 0.78)),
    ("wood", (0.62, 0.39, 0.20)), ("red", (0.88, 0.26, 0.22)), ("yellow", (0.96, 0.80, 0.26)), ("black", (0.09, 0.09, 0.11))]
LAPTOP_PAL = [
    ("silver", (0.80, 0.82, 0.86)), ("grey", (0.36, 0.38, 0.43)), ("black", (0.06, 0.06, 0.08)), ("key", (0.90, 0.91, 0.94)),
    ("keydark", (0.20, 0.21, 0.25)), ("red", (0.86, 0.26, 0.24)), ("blue", (0.36, 0.62, 0.86)), ("yellow", (0.96, 0.80, 0.26))]
PAL = {"plush": {n: i for i, (n, _) in enumerate(PLUSH_PAL)}, "tv": {n: i for i, (n, _) in enumerate(TV_PAL)},
       "laptop": {n: i for i, (n, _) in enumerate(LAPTOP_PAL)}}


def paint_palette(pal, seed, noise):
    rnd = random.Random(seed)
    img = [[(0, 0, 0)] * 64 for _ in range(32)]
    for i, (_, col) in enumerate(pal):
        x0, y0 = (i % 8) * 8, (i // 8) * 8
        for y in range(8):
            for x in range(8):
                n = (rnd.random() - 0.5) * noise
                img[y0 + y][x0 + x] = quant(tuple(v + n for v in col))
    return img


def paint_mouth():
    """A 32x16 smile with a little tongue, on the muzzle."""
    w, h = 32, 16
    tan = PLUSH_PAL[3][1]
    img = [[quant(tan)] * w for _ in range(h)]
    for x in range(w):   # the smile: a curve, thicker in the middle
        t = (x - 15.5) / 14.0
        if abs(t) > 1.0:
            continue
        y = int(round(4 + 6.0 * (1 - t * t)))
        for k in range(2):
            if 0 <= y + k < h:
                img[y + k][x] = quant(PLUSH_PAL[4][1])
    for x in range(10, 22):   # a pink tongue under the middle of the smile
        t = (x - 15.5) / 6.0
        if abs(t) <= 1.0:
            y = int(round(4 + 6.0 * (1 - ((x - 15.5) / 14.0) ** 2))) + 2
            for k in range(int(3 * (1 - t * t)) + 1):
                if 0 <= y + k < h:
                    img[y + k][x] = quant(PLUSH_PAL[6][1])
    return img


def paint_keys():
    """32x16 top of the laptop's deck: rows of keys, a long space bar and a track pad."""
    w, h = 32, 16
    deck, key, dark = quant(LAPTOP_PAL[1][1]), quant(LAPTOP_PAL[3][1]), quant(LAPTOP_PAL[4][1])
    img = [[deck] * w for _ in range(h)]
    for row in range(4):
        y = 1 + row * 2
        if row < 3:
            for kx in range(1, 31, 3):
                for dx in range(2):
                    img[y][kx + dx] = key if (kx // 3 + row) % 5 else dark
        else:
            for x in range(8, 24):
                img[y][x] = key
            for x in (2, 3, 5, 6, 26, 27, 29, 30):
                img[y][x] = dark
    for y in range(10, 15):   # the track pad
        for x in range(10, 22):
            img[y][x] = quant(mixc(LAPTOP_PAL[1][1], (0.6, 0.62, 0.66), 0.6))
    return img


def fill_ellipse(img, cx, cy, rx, ry, col):
    for y in range(len(img)):
        for x in range(len(img[0])):
            if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0:
                img[y][x] = quant(col)


def fill_poly(img, pts, col):
    n = len(pts)
    for y in range(len(img)):
        for x in range(len(img[0])):
            px, py = x + 0.5, y + 0.5
            inside = False
            j = n - 1
            for i in range(n):
                if (pts[i][1] > py) != (pts[j][1] > py) and px < (pts[j][0] - pts[i][0]) * (py - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]:
                    inside = not inside
                j = i
            if inside:
                img[y][x] = quant(col)


def paint_screen(frame):
    """The picture on the television, 32x32. frame 0 and 1: Bluey cheering (arms up, then arms a little lower); 2 and 3: static; 4: switched off."""
    w = h = 32
    rnd = random.Random(40 + frame)
    if frame == 4:
        return [[quant((0.05, 0.08, 0.09))] * w for _ in range(h)]
    if frame >= 2:
        img = [[(0, 0, 0)] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                v = rnd.random()
                v = v * 0.8 + (0.25 if (y // 2 + frame) % 4 == 0 else 0.0)
                img[y][x] = quant((v, v, v * 1.05))
        return img
    blue, belly, navy, tan, black, white = (PLUSH_PAL[i][1] for i in (0, 1, 2, 3, 4, 5))
    img = [[quant(mixc((0.62, 0.84, 0.97), (0.86, 0.95, 1.0), y / h))] * w for y in range(h)]
    for y in range(24, h):   # a bit of lawn
        for x in range(w):
            img[y][x] = quant(mixc((0.45, 0.72, 0.30), (0.34, 0.60, 0.24), (y - 24) / 8))
    fill_ellipse(img, 26, 6, 3.2, 3.2, (1.0, 0.92, 0.45))   # the sun
    arm_y = 1 if frame == 1 else 0   # the arms go up and down; the whole pup bobs a pixel
    bob = 1 if frame == 1 else 0
    fill_ellipse(img, 16, 24 + bob, 6.5, 6.5, blue)           # body
    fill_ellipse(img, 16, 25 + bob, 3.6, 4.6, belly)
    fill_ellipse(img, 11, 29, 3.2, 1.8, belly)                # feet
    fill_ellipse(img, 21, 29, 3.2, 1.8, belly)
    fill_poly(img, [(11, 21 + bob), (6.5, 14 + arm_y * 2), (4.5, 14 + arm_y * 2), (9, 22 + bob)], blue)     # arms up in a cheer
    fill_poly(img, [(21, 21 + bob), (25.5, 14 + arm_y * 2), (27.5, 14 + arm_y * 2), (23, 22 + bob)], blue)
    fill_ellipse(img, 5.5, 13 + arm_y * 2, 2.2, 2.2, blue)
    fill_ellipse(img, 26.5, 13 + arm_y * 2, 2.2, 2.2, blue)
    fill_ellipse(img, 16, 14 + bob, 8.5, 7.0, blue)           # head
    fill_poly(img, [(8.5, 10 + bob), (6.0, 2 + bob), (13.5, 7.5 + bob)], navy)    # ears
    fill_poly(img, [(23.5, 10 + bob), (26.0, 2 + bob), (18.5, 7.5 + bob)], navy)
    fill_poly(img, [(9, 9 + bob), (7.4, 4.5 + bob), (11.8, 8 + bob)], tan)
    fill_poly(img, [(23, 9 + bob), (24.6, 4.5 + bob), (20.2, 8 + bob)], tan)
    fill_ellipse(img, 16, 17 + bob, 5.5, 4.2, tan)            # muzzle
    fill_ellipse(img, 12.3, 11.5 + bob, 2.6, 3.0, white)      # eyes
    fill_ellipse(img, 19.7, 11.5 + bob, 2.6, 3.0, white)
    fill_ellipse(img, 12.6, 12 + bob, 1.2, 1.6, black)
    fill_ellipse(img, 19.4, 12 + bob, 1.2, 1.6, black)
    fill_ellipse(img, 16, 15 + bob, 2.0, 1.4, black)          # nose
    fill_ellipse(img, 16, 19.2 + bob, 3.0, 1.5, black)        # a big open smile
    fill_ellipse(img, 16, 19.8 + bob, 2.2, 0.8, PLUSH_PAL[6][1])
    for y in range(0, h, 2):   # scanlines
        for x in range(w):
            img[y][x] = quant(tuple(v * 0.93 for v in img[y][x]))
    return img


TEXTURES = []   # (name, w, h, pixels)  top row first


def build_textures():
    TEXTURES.append(("plush_pal", 64, 32, paint_palette(PLUSH_PAL, 7, 0.07)))   # 0 (the fabric noise is the "stuffed" look)
    TEXTURES.append(("plush_mouth", 32, 16, paint_mouth()))                       # 1
    TEXTURES.append(("tv_pal", 64, 32, paint_palette(TV_PAL, 8, 0.03)))          # 2
    for f in range(5):                                                            # 3..7: Bluey A, Bluey B, static A, static B, off
        TEXTURES.append(("screen_%d" % f, 32, 32, paint_screen(f)))
    TEXTURES.append(("laptop_pal", 64, 32, paint_palette(LAPTOP_PAL, 9, 0.03)))   # 8
    TEXTURES.append(("laptop_keys", 32, 16, paint_keys()))                         # 9


T_PLUSH, T_MOUTH, T_TVPAL, T_SCREEN, T_LAPPAL, T_KEYS = 0, 1, 2, 3, 8, 9


def swatch(kind, name):
    i = PAL[kind][name]
    return ((i % 8) * 8 + 4, (i // 8) * 8 + 4)   # pixel at the middle of the swatch


# ---------------------------------------------------------------------------------------------------------------------------------------
# Geometry. A piece is a bit of mesh with a texture; pieces are grouped into parts. Positions are Blender metres (Z up, facing -Y).
# ---------------------------------------------------------------------------------------------------------------------------------------
class Piece:
    def __init__(self, tex):
        self.tex = tex
        self.v = []      # position
        self.uv = []     # pixel coordinates in the texture
        self.ref = []    # a point inside the shape, to tell which way is out
        self.tris = []

    def add(self, p, uv, ref):
        self.v.append(Vector(p)); self.uv.append(uv); self.ref.append(Vector(ref))
        return len(self.v) - 1

    def tri(self, a, b, c):
        n = (self.v[b] - self.v[a]).cross(self.v[c] - self.v[a])
        out = (self.v[a] + self.v[b] + self.v[c]) / 3 - (self.ref[a] + self.ref[b] + self.ref[c]) / 3
        if n.length < 1e-12:
            return
        self.tris.append((a, c, b) if n.dot(out) < 0 else (a, b, c))   # always wound outwards


def ellipsoid(tex, uv, c, r, seg=10, rings=6, top=None, inflate=1.0):
    """An ellipsoid with `rings` bands from pole to pole; `top` (radians) keeps only the part above that angle from the top pole... (0 = whole)."""
    p = Piece(tex)
    c = Vector(c)
    r = Vector(r) * inflate
    lat0 = 0.0
    lat1 = math.pi if top is None else top
    idx = []
    for i in range(rings + 1):
        la = lat0 + (lat1 - lat0) * i / rings
        row = []
        if i == 0 or (top is None and i == rings):
            row.append(p.add(c + Vector((0, 0, r.z * math.cos(la))), uv, c))
        else:
            for j in range(seg):
                lo = 2 * math.pi * j / seg
                row.append(p.add(c + Vector((r.x * math.sin(la) * math.cos(lo), r.y * math.sin(la) * math.sin(lo), r.z * math.cos(la))), uv, c))
        idx.append(row)
    for i in range(rings):
        a, b = idx[i], idx[i + 1]
        if len(a) == 1:
            for j in range(seg):
                p.tri(a[0], b[j], b[(j + 1) % seg])
        elif len(b) == 1:
            for j in range(seg):
                p.tri(a[j], b[0], a[(j + 1) % seg])
        else:
            for j in range(seg):
                k = (j + 1) % seg
                p.tri(a[j], b[j], b[k]); p.tri(a[j], b[k], a[k])
    return p


def loft(tex, uv, rings, sides=8, power=2.0, caps=(True, True)):
    """A tube through ring centres; each ring is (centre, rx, ry) (rx across, ry the other way), power > 2 makes the rings squarer."""
    p = Piece(tex)
    a = (Vector(rings[-1][0]) - Vector(rings[0][0])).normalized()
    ref = Vector((0, 1, 0)) if abs(a.y) < 0.9 else Vector((0, 0, 1))
    u = ref.cross(a).normalized()
    v = a.cross(u).normalized()
    ids = []
    for c, rx, ry in rings:
        row = []
        for j in range(sides):
            t = 2 * math.pi * j / sides
            cs, sn = math.cos(t), math.sin(t)
            e = 2.0 / power
            x = math.copysign(abs(cs) ** e, cs) * rx
            y = math.copysign(abs(sn) ** e, sn) * ry
            row.append(p.add(Vector(c) + u * x + v * y, uv, c))
        ids.append(row)
    for i in range(len(rings) - 1):
        for j in range(sides):
            k = (j + 1) % sides
            p.tri(ids[i][j], ids[i + 1][j], ids[i + 1][k]); p.tri(ids[i][j], ids[i + 1][k], ids[i][k])
    for end, (row, c) in enumerate(((ids[0], rings[0][0]), (ids[-1], rings[-1][0]))):
        if caps[end]:
            m = p.add(c, uv, c)
            for j in range(sides):
                p.tri(m, row[j], row[(j + 1) % sides])
    return p


def patch(tex, c, r, az, el, nu, nv, uvbox, inflate=1.02):
    """A bent sheet lying on an ellipsoid's front (azimuth az0..az1 round the Z axis with -Y at 0, elevation el0..el1), with a picture on it."""
    p = Piece(tex)
    c = Vector(c)
    ids = []
    for i in range(nv + 1):
        row = []
        for j in range(nu + 1):
            a = az[0] + (az[1] - az[0]) * j / nu
            e = el[0] + (el[1] - el[0]) * i / nv
            pos = c + Vector((r[0] * inflate * math.cos(e) * math.sin(a), -r[1] * inflate * math.cos(e) * math.cos(a), r[2] * inflate * math.sin(e)))
            uv = (uvbox[0] + (uvbox[2] - uvbox[0]) * j / nu, uvbox[3] + (uvbox[1] - uvbox[3]) * i / nv)   # top of the picture at the top
            row.append(p.add(pos, uv, c))
        ids.append(row)
    for i in range(nv):
        for j in range(nu):
            p.tri(ids[i][j], ids[i][j + 1], ids[i + 1][j + 1]); p.tri(ids[i][j], ids[i + 1][j + 1], ids[i + 1][j])
    return p


def screen_sheet(tex, cx, y, cz, hw, hh, bulge, n=4):
    """The curved glass of the television: a grid facing -Y, pushed out in the middle, showing the whole picture."""
    p = Piece(tex)
    ids = []
    for i in range(n + 1):
        row = []
        for j in range(n + 1):
            u, v = j / n * 2 - 1, i / n * 2 - 1
            d = bulge * (1 - 0.5 * (u * u + v * v))
            row.append(p.add((cx + u * hw, y - d, cz + v * hh), (j / n * 31.99, (1 - i / n) * 31.99), (cx, y + 0.1, cz)))
        ids.append(row)
    for i in range(n):
        for j in range(n):
            p.tri(ids[i][j], ids[i][j + 1], ids[i + 1][j + 1]); p.tri(ids[i][j], ids[i + 1][j + 1], ids[i + 1][j])
    return p


def sheet(tex, origin, right, up, hw, hh, uvbox, n=1, bulge=0.0):
    """A flat (or slightly bulging) grid facing right x up, with a picture on it."""
    p = Piece(tex)
    right, up = Vector(right).normalized(), Vector(up).normalized()
    nrm = right.cross(up)
    ids = []
    for i in range(n + 1):
        row = []
        for j in range(n + 1):
            u, v = j / n * 2 - 1, i / n * 2 - 1
            pos = Vector(origin) + right * (u * hw) + up * (v * hh) + nrm * (bulge * (1 - 0.5 * (u * u + v * v)))
            uv = (uvbox[0] + (uvbox[2] - uvbox[0]) * j / n, uvbox[3] + (uvbox[1] - uvbox[3]) * i / n)
            row.append(p.add(pos, uv, Vector(origin) - nrm * 0.1))
        ids.append(row)
    for i in range(n):
        for j in range(n):
            p.tri(ids[i][j], ids[i][j + 1], ids[i + 1][j + 1]); p.tri(ids[i][j], ids[i + 1][j + 1], ids[i + 1][j])
    return p


class Part:
    def __init__(self, name, parent, pivot):
        self.name, self.parent, self.pivot = name, parent, Vector(pivot)
        self.pieces = []

    def add(self, piece):
        self.pieces.append(piece)
        return piece


def build_plush():
    S = lambda n: swatch("plush", n)
    parts = []
    body = Part("body", None, (0, 0, 0.0))
    parts.append(body)
    body.add(loft(T_PLUSH, S("blue"), [((0, 0, 0.045), 0.07, 0.06), ((0, 0, 0.09), 0.093, 0.08), ((0, 0, 0.15), 0.092, 0.078), ((0, 0, 0.2), 0.062, 0.056)], sides=10))
    body.add(ellipsoid(T_PLUSH, S("belly"), (0, -0.05, 0.115), (0.055, 0.04, 0.07), seg=8, rings=5))   # the pale tummy
    body.add(ellipsoid(T_PLUSH, S("shade"), (0, 0.06, 0.10), (0.07, 0.04, 0.06), seg=8, rings=4))   # a rounder, darker back
    head = Part("head", 0, (0, 0, 0.205))
    parts.append(head)
    hc, hr = (0, 0, 0.285), (0.098, 0.088, 0.082)
    head.add(ellipsoid(T_PLUSH, S("blue"), hc, hr, seg=12, rings=7))
    head.add(ellipsoid(T_PLUSH, S("shade"), hc, hr, seg=12, rings=4, top=math.radians(48), inflate=1.025))   # the darker crown of the head
    head.add(ellipsoid(T_PLUSH, S("tan"), (0, -0.076, 0.258), (0.052, 0.05, 0.042), seg=10, rings=6))   # muzzle
    head.add(patch(T_MOUTH, (0, -0.076, 0.258), (0.052, 0.05, 0.042), (-0.75, 0.75), (-0.62, 0.05), 6, 3, (1, 1, 30, 14), 1.02))   # the smile
    head.add(ellipsoid(T_PLUSH, S("black"), (0, -0.123, 0.278), (0.019, 0.013, 0.013), seg=8, rings=4))   # nose
    for sx in (-1, 1):
        head.add(ellipsoid(T_PLUSH, S("white"), (sx * 0.037, -0.072, 0.31), (0.027, 0.017, 0.032), seg=8, rings=5))   # eyes
        head.add(ellipsoid(T_PLUSH, S("black"), (sx * 0.035, -0.086, 0.31), (0.012, 0.008, 0.016), seg=6, rings=3))
        head.add(ellipsoid(T_PLUSH, S("white"), (sx * 0.0325, -0.0925, 0.317), (0.0045, 0.003, 0.0055), seg=5, rings=2))   # a glint
    for name, sx in (("ear.L", 1), ("ear.R", -1)):
        ear = Part(name, 1, (sx * 0.055, 0.0, 0.345))
        parts.append(ear)
        ear.add(loft(T_PLUSH, S("shade"), [((sx * 0.055, 0.0, 0.34), 0.038, 0.026), ((sx * 0.065, 0.004, 0.395), 0.028, 0.02), ((sx * 0.082, 0.008, 0.46), 0.003, 0.003)], sides=6))
        ear.add(loft(T_PLUSH, S("tan"), [((sx * 0.055, -0.018, 0.345), 0.026, 0.01), ((sx * 0.064, -0.017, 0.392), 0.019, 0.008), ((sx * 0.079, -0.014, 0.44), 0.002, 0.002)], sides=6))
    for name, sx in (("arm.L", 1), ("arm.R", -1)):
        arm = Part(name, 0, (sx * 0.084, 0.0, 0.185))
        parts.append(arm)
        arm.add(loft(T_PLUSH, S("blue"), [((sx * 0.084, 0, 0.185), 0.027, 0.027), ((sx * 0.112, -0.012, 0.15), 0.024, 0.024), ((sx * 0.128, -0.022, 0.118), 0.02, 0.02)], sides=7))
        arm.add(ellipsoid(T_PLUSH, S("blue"), (sx * 0.13, -0.024, 0.108), (0.024, 0.022, 0.024), seg=7, rings=4))
    for name, sx in (("leg.L", 1), ("leg.R", -1)):
        leg = Part(name, 0, (sx * 0.046, 0.0, 0.075))
        parts.append(leg)
        leg.add(loft(T_PLUSH, S("blue"), [((sx * 0.046, 0, 0.075), 0.034, 0.036), ((sx * 0.048, 0, 0.035), 0.033, 0.035)], sides=8, caps=(False, False)))
        leg.add(ellipsoid(T_PLUSH, S("belly"), (sx * 0.048, -0.024, 0.022), (0.037, 0.055, 0.024), seg=8, rings=4))   # big pale paws
    tail = Part("tail", 0, (0, 0.082, 0.085))
    parts.append(tail)
    tail.add(loft(T_PLUSH, S("shade"), [((0, 0.082, 0.085), 0.034, 0.034), ((0, 0.125, 0.12), 0.03, 0.03), ((0, 0.165, 0.175), 0.02, 0.02)], sides=7))
    tail.add(ellipsoid(T_PLUSH, S("navy"), (0, 0.17, 0.182), (0.022, 0.022, 0.024), seg=7, rings=4))
    return parts


def build_tv():
    S = lambda n: swatch("tv", n)
    parts = []
    body = Part("body", None, (0, 0, 0))
    parts.append(body)
    cz = 0.175
    body.add(loft(T_TVPAL, S("teal"), [((0, -0.12, cz), 0.168, 0.135), ((0, -0.04, cz), 0.172, 0.138), ((0, 0.06, cz), 0.158, 0.128), ((0, 0.15, cz), 0.115, 0.095)], sides=16, power=4.0))
    body.add(loft(T_TVPAL, S("cream"), [((0, -0.128, cz), 0.172, 0.139), ((0, -0.1, cz), 0.172, 0.139)], sides=16, power=4.0))   # the front frame
    body.add(screen_sheet(T_SCREEN, -0.03, -0.1315, cz + 0.002, 0.118, 0.092, 0.014))
    for i, z in enumerate((0.215, 0.165)):   # two knobs
        body.add(loft(T_TVPAL, S("red" if i == 0 else "yellow"), [((0.142, -0.128, z), 0.016, 0.016), ((0.142, -0.147, z), 0.014, 0.014)], sides=8))
    for i in range(4):   # a speaker grille
        body.add(loft(T_TVPAL, S("black"), [((0.142, -0.13, 0.12 - i * 0.013), 0.022, 0.0025), ((0.142, -0.134, 0.12 - i * 0.013), 0.022, 0.0025)], sides=4, power=4.0))
    for sx in (-1, 1):   # four little feet
        for sy in (-1, 1):
            body.add(loft(T_TVPAL, S("wood"), [((sx * 0.115, sy * 0.075, 0.032), 0.02, 0.02), ((sx * 0.12, sy * 0.08, 0.0), 0.022, 0.022)], sides=6))
    body.add(ellipsoid(T_TVPAL, S("chrome"), (0, 0.02, 0.312), (0.04, 0.035, 0.016), seg=8, rings=3))   # the aerial base
    for name, sx in (("aerial.L", 1), ("aerial.R", -1)):
        ae = Part(name, 0, (sx * 0.012, 0.02, 0.318))
        parts.append(ae)
        ae.add(loft(T_TVPAL, S("chrome"), [((sx * 0.012, 0.02, 0.318), 0.005, 0.005), ((sx * 0.07, 0.02, 0.38), 0.004, 0.004), ((sx * 0.125, 0.02, 0.44), 0.004, 0.004)], sides=5))
        ae.add(ellipsoid(T_TVPAL, S("red"), (sx * 0.128, 0.02, 0.444), (0.011, 0.011, 0.011), seg=6, rings=3))
    return parts


def build_laptop():
    """A little laptop: a deck with a keyboard, and a lid on a hinge (modelled open, a little past upright; the game swings it shut)."""
    S = lambda n: swatch("laptop", n)
    parts = []
    base = Part("base", None, (0, 0, 0))
    parts.append(base)
    base.add(loft(T_LAPPAL, S("silver"), [((0, 0, 0.0), 0.15, 0.106), ((0, 0, 0.008), 0.152, 0.108), ((0, 0, 0.016), 0.15, 0.106)], sides=16, power=4.0))
    base.add(sheet(T_KEYS, (0, -0.005, 0.0168), (1, 0, 0), (0, 1, 0), 0.128, 0.083, (0.5, 0.5, 31.5, 15.5), n=1))
    base.add(loft(T_LAPPAL, S("grey"), [((0, 0.103, 0.014), 0.14, 0.006), ((0, 0.103, 0.02), 0.14, 0.006)], sides=8))   # the hinge
    for sx in (-1, 1):
        base.add(loft(T_LAPPAL, S("black"), [((sx * 0.12, -0.08, 0.002), 0.012, 0.012), ((sx * 0.12, -0.08, -0.002), 0.012, 0.012)], sides=6))
    hinge = Vector((0, 0.103, 0.018))
    t = Vector((0, 0.26, 0.966)).normalized()         # up the lid
    n = Vector((1, 0, 0)).cross(t).normalized()        # out of the screen, towards whoever sits in front
    lid = Part("lid", 0, hinge)
    parts.append(lid)
    lid.add(loft(T_LAPPAL, S("silver"), [(hinge + t * 0.004, 0.152, 0.006), (hinge + t * 0.1, 0.153, 0.007), (hinge + t * 0.205, 0.151, 0.006)], sides=16, power=4.0))
    lid.add(loft(T_LAPPAL, S("black"), [(hinge + t * 0.01 + n * 0.0065, 0.146, 0.0015), (hinge + t * 0.2 + n * 0.0065, 0.146, 0.0015)], sides=12, power=4.0))   # the bezel
    lid.add(sheet(T_SCREEN, hinge + t * 0.105 + n * 0.0085, (1, 0, 0), t, 0.134, 0.092, (0.5, 0.5, 31.5, 31.5), n=4, bulge=0.0))
    lid.add(ellipsoid(T_LAPPAL, S("blue"), hinge + t * 0.11 - n * 0.0075, (0.034, 0.003, 0.034), seg=10, rings=3))   # a blue sticker on the back
    lid.add(ellipsoid(T_LAPPAL, S("grey"), hinge + t * 0.155 - n * 0.0075 + Vector((-0.016, 0, 0)), (0.012, 0.0035, 0.016), seg=6, rings=2))
    lid.add(ellipsoid(T_LAPPAL, S("grey"), hinge + t * 0.155 - n * 0.0075 + Vector((0.016, 0, 0)), (0.012, 0.0035, 0.016), seg=6, rings=2))
    return parts


# ---------------------------------------------------------------------------------------------------------------------------------------
# Blender objects (so the .blend, the .glb and the previews exist) and the game's header
# ---------------------------------------------------------------------------------------------------------------------------------------
def write_png(name, w, h, px):
    from PIL import Image
    im = Image.new("RGBA", (w, h))
    im.putdata([tuple(int(round(v * 255)) for v in c) + (255,) for row in px for c in row])
    path = os.path.join(TEX_DIR, name + ".png")
    im.save(path)
    return path


def make_blender(models):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    imgs, mats = [], []
    for name, w, h, px in TEXTURES:
        img = bpy.data.images.load(write_png(name, w, h, px))
        img.pack()
        imgs.append(img)
        m = bpy.data.materials.new("mat_" + name)
        m.use_nodes = True
        nt = m.node_tree
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.interpolation = "Closest"
        bsdf = nt.nodes["Principled BSDF"]
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.9
        if name.startswith("screen"):
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = 0.8
        mats.append(m)
    objs = []
    for mname, (parts, origin) in models.items():
        empty = bpy.data.objects.new(mname, None)
        bpy.context.scene.collection.objects.link(empty)
        empty.location = origin
        for part in parts:
            verts, faces, uvs, fmat = [], [], [], []
            for pc in part.pieces:
                base = len(verts)
                verts += [tuple(v) for v in pc.v]
                for t in pc.tris:
                    faces.append(tuple(base + i for i in t))
                    uvs.append([(pc.uv[i][0] / TEXTURES[pc.tex][1], 1 - pc.uv[i][1] / TEXTURES[pc.tex][2]) for i in t])
                    fmat.append(pc.tex)
            me = bpy.data.meshes.new(mname + "." + part.name)
            me.from_pydata(verts, [], faces)
            me.update()
            uvl = me.uv_layers.new(name="UVMap")
            li = 0
            for fi, f in enumerate(faces):
                for k in range(3):
                    uvl.data[li].uv = uvs[fi][k]
                    li += 1
            used = sorted(set(fmat))
            for t in used:
                me.materials.append(mats[t])
            for fi, poly in enumerate(me.polygons):
                poly.material_index = used.index(fmat[fi])
                poly.use_smooth = True
            ob = bpy.data.objects.new(mname + "." + part.name, me)
            bpy.context.scene.collection.objects.link(ob)
            ob.parent = empty
            objs.append(ob)
    return objs


def to_game(v):
    return (v.x * SCALE, v.z * SCALE, -v.y * SCALE)   # Blender (Z up, facing -Y) to game (Y up, facing +Z)


def rgba5551(px):
    out = bytearray()
    for row in px:
        for c in row:
            r, g, b = (min(31, max(0, int(round(v * 31)))) for v in c)
            v = (r << 11) | (g << 6) | (b << 1) | 1
            out += bytes(((v >> 8) & 0xFF, v & 0xFF))
    return out


def export_model(prefix, parts, o):
    """Writes one model's arrays: parts (with vertices in the part's own space, the pivot at its origin), batches and triangles."""
    names = [p.name for p in parts]
    verts, batches, tris, part_rows = [], [], [], []
    for pi, part in enumerate(parts):
        first_batch = len(batches)
        v0 = len(verts)
        for tex in sorted(set(pc.tex for pc in part.pieces)):
            # one batch at a time: up to 32 vertices and the triangles that use only them
            cur_map, cur_tris, cur_verts = {}, [], []

            def flush():
                nonlocal cur_map, cur_tris, cur_verts
                if not cur_tris:
                    return
                batches.append((tex, len(verts), len(cur_verts), len(tris), len(cur_tris)))
                verts.extend(cur_verts)
                tris.extend(cur_tris)
                cur_map, cur_tris, cur_verts = {}, [], []
            for pc in [q for q in part.pieces if q.tex == tex]:
                normals = [Vector((0, 0, 0)) for _ in pc.v]
                for a, b, c in pc.tris:
                    n = (pc.v[b] - pc.v[a]).cross(pc.v[c] - pc.v[a])
                    for i in (a, b, c):
                        normals[i] += n
                for tri in pc.tris:
                    new = [i for i in tri if (id(pc), i) not in cur_map]
                    if len(cur_verts) + len(set(new)) > MAX_BATCH:
                        flush()
                    local = []
                    for i in tri:
                        key = (id(pc), i)
                        if key not in cur_map:
                            cur_map[key] = len(cur_verts)
                            n = normals[i].normalized() if normals[i].length > 0 else Vector((0, 0, 1))
                            g = to_game(pc.v[i] - part.pivot)
                            ng = (n.x, n.z, -n.y)
                            cur_verts.append((g, tuple(int(round(k * 127)) for k in ng), (int(round(pc.uv[i][0] * 32)), int(round(pc.uv[i][1] * 32)))))
                        local.append(cur_map[key])
                    cur_tris.append(tuple(local))
            flush()
        par = part.parent
        off = to_game(part.pivot - (parts[par].pivot if par is not None else Vector((0, 0, 0))))
        part_rows.append((-1 if par is None else par, off, v0, len(verts) - v0, first_batch, len(batches) - first_batch))
    o.append("// ---- %s: %d parts, %d vertices in %d batches, %d triangles ----" % (prefix, len(parts), len(verts), len(batches), len(tris)))
    o.append("constexpr int k%sPartCount = %d;" % (prefix, len(parts)))
    o.append("inline constexpr const char* k%sPartNames[k%sPartCount] = { %s };" % (prefix, prefix, ", ".join('"%s"' % n for n in names)))
    o.append("inline constexpr Part k%sParts[k%sPartCount] = {" % (prefix, prefix))
    for pr in part_rows:
        o.append("    { %d, %.2ff, %.2ff, %.2ff, %d, %d, %d, %d }," % (pr[0], pr[1][0], pr[1][1], pr[1][2], pr[2], pr[3], pr[4], pr[5]))
    o.append("};")
    o.append("constexpr int k%sVertCount = %d;" % (prefix, len(verts)))
    o.append("inline constexpr Vert k%sVerts[k%sVertCount] = {" % (prefix, prefix))
    for g, n, uv in verts:
        o.append("    {%.2ff, %.2ff, %.2ff, %d, %d, %d, %d, %d}," % (g[0], g[1], g[2], n[0], n[1], n[2], uv[0], uv[1]))
    o.append("};")
    o.append("constexpr int k%sBatchCount = %d;" % (prefix, len(batches)))
    o.append("inline constexpr Batch k%sBatches[k%sBatchCount] = {" % (prefix, prefix))
    for b in batches:
        o.append("    { %d, %d, %d, %d, %d }," % b)
    o.append("};")
    o.append("constexpr int k%sTriCount = %d;" % (prefix, len(tris)))
    o.append("inline constexpr uint8_t k%sTris[k%sTriCount][3] = {" % (prefix, prefix))
    o.append(", ".join("{%d,%d,%d}" % t for t in tris))
    o.append("};")
    return len(verts), len(tris)


def write_header(plush, tv, laptop):
    o = []
    o.append("#pragma once")
    o.append("// GENERATED by tools/avriella/build_toys.py (also writes assets/avriella/toys.blend). Do not edit by hand: change the build script and run it again.")
    o.append("//")
    o.append("// Avriella's favourite toys in the N64 style: a stuffed Bluey and a little television. Each is a few stiff parts joined at pivots, so the game")
    o.append("// poses them with a few angles (DrawToy in RoyaleMod.cpp). Game units, Y up, facing +Z, standing on y = 0. A part's vertices are in its own space")
    o.append("// (its pivot at the origin); `ox, oy, oz` is where that pivot sits relative to its parent's pivot (the parent always comes earlier in the list).")
    o.append("#include <cstdint>")
    o.append("")
    o.append("namespace royale {")
    o.append("namespace avriella_toys {")
    o.append("")
    o.append("// A vertex: position, normal (x127), texel coordinate (1/32 texels). A batch: up to 32 vertices loaded together, the texture they use and the triangles drawn")
    o.append("// from them (indices within the batch; `firstVert` counts from the start of the model's vertex list). A part owns a run of vertices and of batches.")
    o.append("struct Vert { float x, y, z; int8_t nx, ny, nz; int16_t s, t; };")
    o.append("struct Batch { uint8_t texture; uint16_t firstVert; uint8_t vertCount; uint16_t firstTri; uint16_t triCount; };")
    o.append("struct Part { int8_t parent; float ox, oy, oz; uint16_t firstVert, vertCount; uint16_t firstBatch, batchCount; };")
    o.append("")
    o.append("// Textures (RGBA16, big-endian 5-5-5-1). 0 the plush's colour swatches, 1 the smile, 2 the TV's colour swatches, then the pictures on the TV:")
    o.append("// Bluey cheering (two frames), static (two frames) and the switched-off glass. The TV's screen batch uses kTexScreen; the game swaps in the picture it wants.")
    o.append("enum Tex : uint8_t { kTexPlush = 0, kTexMouth = 1, kTexTv = 2, kTexScreen = 3, kTexBlueyA = 3, kTexBlueyB = 4, kTexStaticA = 5, kTexStaticB = 6, kTexOff = 7, kTexLaptop = 8, kTexKeys = 9, kTexCount = 10 };")
    o.append("struct TexInfo { const uint8_t* data; int w, h; };")
    for i, (name, w, h, px) in enumerate(TEXTURES):
        data = rgba5551(px)
        o.append("alignas(8) inline constexpr uint8_t kTex%d[%d] = {   // %s %dx%d" % (i, len(data), name, w, h))
        for j in range(0, len(data), 32):
            o.append("    " + ", ".join("0x%02X" % b for b in data[j:j + 32]) + ",")
        o.append("};")
    o.append("inline constexpr TexInfo kTextures[kTexCount] = { " + ", ".join("{ kTex%d, %d, %d }" % (i, t[1], t[2]) for i, t in enumerate(TEXTURES)) + " };")
    o.append("")
    pv, pt = export_model("Plush", plush, o)
    o.append("")
    tv_v, tv_t = export_model("Tv", tv, o)
    o.append("")
    lp_v, lp_t = export_model("Laptop", laptop, o)
    o.append("")
    o.append("} // namespace avriella_toys")
    o.append("} // namespace royale")
    with open(HEADER, "w") as f:
        f.write("\n".join(o) + "\n")
    return pv, pt, tv_v, tv_t, lp_v, lp_t


def main():
    os.makedirs(TEX_DIR, exist_ok=True)
    build_textures()
    plush, tv, laptop = build_plush(), build_tv(), build_laptop()
    pv, pt, tv_v, tv_t, lp_v, lp_t = write_header(plush, tv, laptop)
    make_blender({"Plush": (plush, (0, 0, 0)), "Tv": (tv, (0.6, 0, 0)), "Laptop": (laptop, (1.2, 0, 0))})
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "toys.blend"))
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "toys.glb"), export_format="GLB")
    print("plush: %d vertices, %d triangles; tv: %d, %d; laptop: %d, %d" % (pv, pt, tv_v, tv_t, lp_v, lp_t))


main()
