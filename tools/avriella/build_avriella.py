"""Builds Avriella, a baby girl, in Blender: a low poly, N64 (Ocarina of Time) style model with small hand-painted textures, a skeleton and clips.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0, Python 3.11):
    blender -b --python tools/avriella/build_avriella.py
    python3 tools/avriella/build_avriella.py
It writes assets/avriella/avriella.blend, avriella.glb, avriella.fbx and the textures as PNG. Then run export_avriella.py to turn the .blend into
the game's shared/avriella_model.h. Everything is made from code so it can be rebuilt and tweaked; you can also edit avriella.blend by hand in
Blender and only run the export.

The look follows a photo of a happy baby: a big round head with chubby cheeks, big dark eyes and a wide open smile, dark brown hair with a small
curly tuft on top, and a long-sleeved ruffle-shoulder romper printed with pumpkins and fall leaves. It is a cute, stylised character in the manner
of Ocarina of Time's Kokiri and not a realistic likeness. Sizes are in metres here (she stands about 0.62 m tall); the export scales them to game
units.

Coordinates: Blender's Z is up and she faces -Y (towards the camera in Front view). Her left is +X.

The skeleton also has three little rocks (rock.1 to rock.3) that she stacks in the "stack" clip; in every other clip they are tucked under the
ground, where the terrain hides them.
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Euler, Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "lilo"))
import build_lilo as BL  # noqa: E402  (loft/ring helpers and the paint helpers are shared with Lilo)

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "assets", "avriella")
FPS = 20   # the game's own update rate; every frame of every clip is a key

mix, jitter, quant5, blend, ease, env = BL.mix, BL.jitter, BL.quant5, BL.blend, BL.ease, BL.env

# ---------------------------------------------------------------------------------------------------------------------------------
# Textures. Everything is RGBA16 in the game (5 bits a channel) and the graphics chip holds 4 KB of texture at a time, so each image is at most
# 64x32: "cloth" (the pumpkin romper, torso at left, two sleeves at right), "skin" (legs, hair, ruffles, a stone, plain skin) and five faces.
# ---------------------------------------------------------------------------------------------------------------------------------
CLOTH_W, CLOTH_H = 64, 32
SKIN_W, SKIN_H = 64, 32
FACE_W, FACE_H = 32, 32
FACES = ("smile", "half", "shut", "giggle", "oh")

CREAM = (0.97, 0.93, 0.82)
PIPING = (0.92, 0.82, 0.55)
SKIN = (0.99, 0.80, 0.69)
SKIN_LIGHT = (1.0, 0.87, 0.77)
SKIN_SHADE = (0.90, 0.66, 0.57)
BLUSH = (0.97, 0.52, 0.50)
HAIR = (0.22, 0.13, 0.09)
HAIR_LIGHT = (0.33, 0.20, 0.13)
HAIR_DARK = (0.12, 0.07, 0.05)
RUFFLE = (0.95, 0.92, 0.94)
RUFFLE_SHADE = (0.84, 0.80, 0.90)
STONE = (0.56, 0.55, 0.52)
EYE = (0.10, 0.06, 0.05)
EYE_RIM = (0.30, 0.17, 0.11)
MOUTH = (0.46, 0.13, 0.15)
TONGUE = (0.88, 0.42, 0.44)
STEM = (0.40, 0.28, 0.15)

PUMPKINS = {   # base, dark, light
    "orange": ((0.93, 0.52, 0.16), (0.78, 0.36, 0.12), (0.99, 0.70, 0.32)),
    "teal": ((0.34, 0.62, 0.52), (0.22, 0.46, 0.40), (0.55, 0.80, 0.66)),
    "green": ((0.46, 0.66, 0.34), (0.32, 0.50, 0.24), (0.66, 0.82, 0.48)),
    "red": ((0.80, 0.32, 0.24), (0.62, 0.22, 0.18), (0.92, 0.50, 0.38)),
    "tan": ((0.86, 0.66, 0.42), (0.70, 0.50, 0.30), (0.95, 0.80, 0.58)),
}
LEAVES = ((0.80, 0.22, 0.18), (0.92, 0.50, 0.14), (0.70, 0.40, 0.16))


def put(img, x, y, c, x0=0, w=None, h=32):
    """Sets a pixel, wrapping across `w` pixels from x0 (so the print runs round a tube without a seam)."""
    if w is not None:
        x = x0 + (x % w)
    if 0 <= x < len(img[0]) and 0 <= y < h:
        img[y][x] = c


def pumpkin(img, cx, cy, rx, ry, kind, x0, w, h=32):
    base, dark, light = PUMPKINS[kind]
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            if dx * dx + dy * dy > 1.0:
                continue
            c = base
            if dx * 0.6 + dy * 0.7 < -0.5:
                c = light
            elif dx * 0.6 + dy * 0.7 > 0.55:
                c = dark
            if abs(dx) < 0.13 or abs(abs(dx) - 0.66) < 0.09:   # the ribs
                c = mix(c, dark, 0.55)
            put(img, x, y, c, x0, w, h)
    put(img, int(cx), int(cy - ry) - 1, STEM, x0, w, h)
    put(img, int(cx), int(cy - ry) - 2, STEM, x0, w, h)


def leaf(img, x, y, i, x0, w, h=32):
    c = LEAVES[i % len(LEAVES)]
    put(img, x, y, c, x0, w, h)
    put(img, x + 1, y, c, x0, w, h)
    put(img, x, y + 1, mix(c, (0.3, 0.1, 0.05), 0.4), x0, w, h)


def paint_cloth():
    img = [[CREAM] * CLOTH_W for _ in range(CLOTH_H)]
    # torso: s 0..31 wraps round her; the top of the image is her neck, the bottom the leg openings
    for x in range(32):
        img[12][x] = PIPING   # the seam under the chest
    torso = [(3, 5, 3.2, 3.0, "teal"), (11, 4, 2.8, 2.6, "orange"), (19, 6, 3.2, 3.0, "green"), (27, 4, 3.0, 2.8, "orange"),
             (7, 18, 3.6, 3.3, "orange"), (16, 20, 3.2, 3.4, "red"), (25, 18, 3.4, 3.2, "green"), (2, 26, 3.0, 2.6, "tan"),
             (12, 28, 3.4, 3.0, "teal"), (21, 27, 3.2, 2.8, "orange"), (30, 27, 3.0, 2.8, "green")]
    for cx, cy, rx, ry, kind in torso:
        pumpkin(img, cx, cy, rx, ry, kind, 0, 32)
    for i, (x, y) in enumerate([(8, 9), (15, 9), (23, 10), (30, 11), (5, 23), (14, 24), (21, 22), (29, 22), (0, 15), (11, 14), (19, 14)]):
        leaf(img, x, y, i, 0, 32)
    # sleeves: s 32..47 her left, 48..63 her right (16 wide, round the arm); a red plaid cuff at the wrist
    sleeve = [(3, 5, 2.5, 2.4, "orange"), (11, 8, 2.4, 2.2, "teal"), (6, 14, 2.6, 2.4, "green"), (13, 19, 2.3, 2.2, "red")]
    for x0, shift in ((32, 0), (48, 5)):
        for cx, cy, rx, ry, kind in sleeve:
            pumpkin(img, (cx + shift) % 16, cy, rx, ry, kind, x0, 16)
        for i, (x, y) in enumerate([(9, 3), (0, 11), (15, 16), (4, 21), (9, 23)]):
            leaf(img, (x + shift) % 16, y, i, x0, 16)
        for y in range(24, 32):
            for x in range(16):
                red = (0.72, 0.20, 0.18)
                c = red
                if x % 4 == 0 or y % 4 == 0:
                    c = (0.45, 0.10, 0.10)
                if x % 8 == 2 or y % 8 == 2:
                    c = (0.93, 0.85, 0.74)
                img[y][x0 + x] = c
    return [[quant5(jitter(c, x, y, 3, 0.02)) for x, c in enumerate(row)] for y, row in enumerate(img)]


def paint_skin():
    img = [[SKIN] * SKIN_W for _ in range(SKIN_H)]
    for y in range(SKIN_H):
        for x in range(SKIN_W):
            if x < 32:        # legs: her left at s 0..15, right at 16..31; soft light along the front, chubby creases at thigh and ankle
                u = (x % 16 + 0.5) / 16
                c = mix(SKIN_SHADE, SKIN_LIGHT, max(0.0, 1.0 - abs(u - 0.35) * 2.2) * 0.8 + 0.1)
                if y in (9, 10, 22) or (y == 23 and 0.2 < u < 0.8):
                    c = mix(c, SKIN_SHADE, 0.7)
                if y >= 27:
                    c = mix(c, SKIN_LIGHT, 0.4)
            elif x < 48:      # hair: dark brown with lighter strands
                c = HAIR
                if (x * 7 + y) % 5 == 0:
                    c = HAIR_LIGHT
                if (x * 3 + y * 2) % 11 == 0:
                    c = HAIR_DARK
                if y < 6:
                    c = mix(c, HAIR_LIGHT, 0.3)
            elif y < 12:      # the ruffles: pale lavender white with folds
                c = RUFFLE_SHADE if (x - 48) % 4 == 0 else RUFFLE
                if y > 8:
                    c = mix(c, RUFFLE_SHADE, 0.45)
            elif y < 22:      # a small stone
                c = STONE
                n = BL.hash01(x, y, 5)
                if n < 0.2:
                    c = (0.42, 0.41, 0.40)
                elif n > 0.86:
                    c = (0.70, 0.69, 0.66)
            else:             # plain skin (hands, neck, ears)
                c = mix(SKIN, SKIN_LIGHT, 0.3) if (x + y) % 6 else SKIN
            img[y][x] = quant5(jitter(c, x, y, 9, 0.018))
    return img


def ellipse(x, y, cx, cy, rx, ry):
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2


def paint_face(kind):
    """kind: smile, half (sleepy), shut (asleep), giggle (eyes squeezed, wide open mouth) or oh (a round mouth)."""
    img = [[SKIN] * FACE_W for _ in range(FACE_H)]
    eyes = [(9.6, 13.6), (22.4, 13.6)]
    for t in range(FACE_H):
        for s in range(FACE_W):
            x, y = s + 0.5, t + 0.5
            c = mix(SKIN_LIGHT, SKIN, min(1.0, max(0.0, (y - 4) / 26)))
            # chubby pink cheeks
            for cx in (6.5, 25.5):
                k = ellipse(x, y, cx, 20.5, 4.8, 3.4)
                if k < 1.0:
                    c = mix(c, BLUSH, 0.55 * (1.0 - k))
            # brows: thin soft arcs
            for cx in (9.6, 22.4):
                if abs(x - cx) < 3.6 and abs(y - (8.3 + ((x - cx) / 3.6) ** 2 * 1.3)) < 0.55:
                    c = mix(c, HAIR_LIGHT, 0.8)
            # nose: a small shadowed button with a highlight
            if ellipse(x, y, 16, 19.2, 1.9, 1.3) < 1.0:
                c = mix(c, SKIN_SHADE, 0.55)
            if ellipse(x, y, 15.6, 18.5, 0.8, 0.5) < 1.0:
                c = SKIN_LIGHT
            for ex, ey in eyes:
                k = ellipse(x, y, ex, ey, 3.7, 4.4)
                if kind in ("smile", "oh") or (kind == "half"):
                    if k < 1.0:
                        c = EYE_RIM if k > 0.72 and y > ey else EYE
                        if abs(x - (ex - 1.2)) < 0.9 and abs(y - (ey - 1.7)) < 0.95:
                            c = (1.0, 1.0, 1.0)        # the big highlight
                        if abs(x - (ex + 1.4)) < 0.6 and abs(y - (ey + 1.6)) < 0.5:
                            c = (0.80, 0.72, 0.66)     # and a small one
                    if kind == "half" and y < ey + 0.6 and k < 1.35:
                        c = SKIN_SHADE if y < ey - 0.6 else EYE   # lids half down over the eye
                    # lashes at the outer upper corner
                    side = 1 if ex > 16 else -1
                    if kind != "half" and abs((y - (ey - 3.0)) - (x - ex) * side * -0.0) < 0.5 and 2.6 < (x - ex) * side < 4.7:
                        c = EYE
                elif kind == "shut":      # asleep: a soft downward curve with lashes
                    if abs(x - ex) < 3.8 and abs(y - (ey + 1.0 - ((x - ex) / 3.8) ** 2 * 1.8)) < 0.7:
                        c = EYE
                else:                     # giggle: happy squeezed arcs, up like a little hill
                    if abs(x - ex) < 3.9 and abs(y - (ey + 0.8 + ((x - ex) / 3.9) ** 2 * 2.2 - 2.0)) < 0.95:
                        c = EYE
            # mouth
            if kind == "smile":     # a wide open smile: the inside is a half oval under the upper lip
                k = ellipse(x, y, 16, 23.2, 6.3, 4.2)
                if k < 1.0 and y > 23.0:
                    c = MOUTH
                    if y > 25.6 and abs(x - 16) < 3.4:
                        c = TONGUE
                if abs(y - 23.0) < 0.5 and abs(x - 16) < 6.6:
                    c = mix(c, SKIN_SHADE, 0.8)
            elif kind == "half":    # a small content smile
                if abs(x - 16) < 4.2 and abs(y - (24.0 + ((x - 16) / 4.2) ** 2 * 1.3 - 1.0)) < 0.6:
                    c = MOUTH
            elif kind == "shut":    # a tiny soft mouth
                if abs(x - 16) < 2.2 and abs(y - 24.2) < 0.7:
                    c = mix(MOUTH, SKIN, 0.4)
            elif kind == "giggle":  # a big open laugh
                k = ellipse(x, y, 16, 23.0, 7.2, 5.6)
                if k < 1.0 and y > 22.3:
                    c = MOUTH
                    if y > 25.8 and abs(x - 16) < 4.6:
                        c = TONGUE
                if abs(y - 22.3) < 0.5 and abs(x - 16) < 7.4:
                    c = mix(c, SKIN_SHADE, 0.8)
            elif kind == "oh":      # a round "ooh" or "ba"
                k = ellipse(x, y, 16, 24.0, 2.7, 3.0)
                if k < 1.0:
                    c = MOUTH if k > 0.35 else TONGUE if y > 25 else MOUTH
            img[t][s] = quant5(jitter(c, s, t, 11 + len(kind), 0.015))
    return img


def make_image(name, pix, w, h):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    im = bpy.data.images.new(name, w, h, alpha=False)
    flat = []
    for row in range(h - 1, -1, -1):   # Blender's rows start at the bottom
        for col in range(w):
            r, g, b = pix[row][col]
            flat += [r, g, b, 1.0]
    im.pixels = flat
    im.filepath_raw = os.path.join(OUT, name + ".png")
    im.file_format = "PNG"
    im.save()
    return im


# ---------------------------------------------------------------------------------------------------------------------------------
# The skeleton. Heads are the joints the parts turn about. Her arms and legs hang straight in the rest pose.
# ---------------------------------------------------------------------------------------------------------------------------------
def tup(v):
    return (v.x, v.y, v.z)


ROCKS = [   # name, rest centre (between her legs, in front of her), radius
    ("rock.1", (0.0, -0.15, 0.034), 0.046),
    ("rock.2", (-0.055, -0.225, 0.029), 0.037),
    ("rock.3", (0.06, -0.235, 0.026), 0.032),
]
ROCKS_REST = {name: Vector(c) for name, c, _ in ROCKS}

BONES = [
    ("root", (0, 0, 0), (0, 0, 0.08), None),
    ("pelvis", (0, 0, 0.18), (0, 0, 0.24), "root"),
    ("chest", (0, 0, 0.24), (0, 0, 0.355), "pelvis"),
    ("neck", (0, 0, 0.355), (0, 0, 0.40), "chest"),
    ("head", (0, 0, 0.40), (0, 0, 0.62), "neck"),
    ("tuft", (0.012, -0.035, 0.607), (0.04, -0.03, 0.64), "head"),
]
for side, sx in (("L", 1), ("R", -1)):
    BONES += [
        ("ruffle." + side, (0.082 * sx, 0, 0.36), (0.082 * sx, 0, 0.31), "chest"),
        ("upper_arm." + side, (0.082 * sx, 0, 0.345), (0.09 * sx, 0, 0.275), "chest"),
        ("forearm." + side, (0.09 * sx, 0, 0.275), (0.095 * sx, 0, 0.205), "upper_arm." + side),
        ("hand." + side, (0.095 * sx, 0, 0.205), (0.095 * sx, 0, 0.165), "forearm." + side),
        ("thigh." + side, (0.042 * sx, 0, 0.185), (0.042 * sx, -0.003, 0.105), "pelvis"),
        ("shin." + side, (0.042 * sx, -0.003, 0.105), (0.042 * sx, 0, 0.042), "thigh." + side),
        ("foot." + side, (0.042 * sx, 0, 0.042), (0.042 * sx, -0.07, 0.02), "shin." + side),
    ]
for name, c, r in ROCKS:
    BONES.append((name, c, (c[0], c[1], c[2] + 0.03), None))   # no parent: she sinks to sit, the rocks must not
BONE_NAMES = [b[0] for b in BONES]

# ---------------------------------------------------------------------------------------------------------------------------------
# The mesh. Rings are lofted for the head, body, neck, limbs, ruffles and the rest. About 950 triangles.
# ---------------------------------------------------------------------------------------------------------------------------------
MAT_CLOTH, MAT_SKIN, MAT_FACE = 0, 1, 2
HEAD_C = Vector((0.0, 0.0, 0.50))
HEAD_R = (0.113, 0.106, 0.112)    # x (side), y (front to back), z
FACE_TOP, FACE_BOTTOM, FACE_W_M = 0.062, 0.118, 0.22    # the face picture's frame, relative to the head centre


def up_ring(z, rx, ry, n, yc=0.0, xc=0.0):
    return BL.ring(Vector((xc, yc, z)), Vector((0, 0, 1)), Vector((0, -1, 0)), ry, rx, n)


def build_mesh():
    b = BL.Builder()
    Z = Vector((0, 0, 1))

    # head: rings stacked upwards, ring point 0 straight to the front; round and chubby at the cheeks
    prof = [-0.97, -0.80, -0.50, -0.15, 0.25, 0.60, 0.85, 0.97]
    n = 10
    rings = []
    for t in prof:
        s = math.sqrt(1 - t * t)
        s *= 1.0 + 0.075 * math.exp(-((t + 0.38) / 0.30) ** 2)   # cheeks
        s *= 1.0 - 0.07 * math.exp(-((t + 0.97) / 0.2) ** 2)    # a little chin
        rings.append(up_ring(HEAD_C.z + HEAD_R[2] * t, HEAD_R[0] * s, HEAD_R[1] * s, n))
    first = len(b.faces)
    BL.loft(b, rings, [{"head": 1.0}] * len(rings), MAT_SKIN, lambda i, k: (0, 0),
            cap0=(HEAD_C + Vector((0, 0, -HEAD_R[2] * 1.0)), {"head": 1.0}, (0, 0)),
            cap1=(HEAD_C + Vector((0, 0, HEAD_R[2] * 1.0)), {"head": 1.0}, (0, 0)))
    head_faces = list(range(first, len(b.faces)))

    # ears, small and round
    for sx in (1, -1):
        c = Vector((0.108 * sx, 0.008, 0.492))
        rr = [BL.ring(c + Vector((sx * o, 0, 0)), Vector((sx, 0, 0)), Vector((0, 0, 1)), r1, r2, 6)
              for o, r1, r2 in ((0.0, 0.026, 0.02), (0.012, 0.030, 0.024), (0.022, 0.020, 0.014))]
        BL.loft(b, rr, [{"head": 1.0}] * 3, MAT_SKIN, lambda i, k: (50 + k / 6 * 10, 24 + i * 3),
                cap0=(c - Vector((sx * 0.004, 0, 0)), {"head": 1.0}, (55, 24)), cap1=(c + Vector((sx * 0.03, 0, 0)), {"head": 1.0}, (55, 30)))

    # the curly tuft: a small hair horn that bends to her left
    base = Vector((0.012, -0.035, 0.604))
    pts = [base, base + Vector((0.006, -0.008, 0.016)), base + Vector((0.020, -0.010, 0.026)), base + Vector((0.034, -0.002, 0.024))]
    rad = [0.036, 0.032, 0.026, 0.016]
    tr = []
    for i, (p, r) in enumerate(zip(pts, rad)):
        d = pts[min(i + 1, 3)] - pts[max(i - 1, 0)]
        tr.append(BL.ring(p, d, Vector((0, -1, 0)), r, r, 6))
    BL.loft(b, tr, [{"head": 1.0}, {"tuft": 1.0}, {"tuft": 1.0}, {"tuft": 1.0}], MAT_SKIN, lambda i, k: (32 + k / 6 * 15, 3 + i * 8),
            cap1=(pts[3] + Vector((0.014, 0.006, -0.004)), {"tuft": 1.0}, (39, 28)))

    # neck
    nk = [up_ring(z, 0.031, 0.031, 6) for z in (0.345, 0.372, 0.402)]
    BL.loft(b, nk, [{"chest": 1.0}, {"neck": 1.0}, {"head": 1.0}], MAT_SKIN, lambda i, k: (50 + k / 6 * 10, 24 + i * 3))

    # the romper's body: from the leg openings up to the collar. Pelvis below, chest above
    body = [(0.163, 0.066, 0.058, {"pelvis": 1.0}), (0.188, 0.082, 0.073, {"pelvis": 1.0}), (0.228, 0.087, 0.079, blend("pelvis", "chest", 0.5)),
            (0.278, 0.081, 0.073, {"chest": 1.0}), (0.322, 0.068, 0.060, {"chest": 1.0}), (0.355, 0.048, 0.044, {"chest": 1.0})]
    br = [up_ring(z, rx, ry, 8) for z, rx, ry, _ in body]
    BL.loft(b, br, [w for _, _, _, w in body], MAT_CLOTH, lambda i, k: (k / 8 * 32, (1 - i / (len(body) - 1)) * 31.0 + 0.4),
            cap0=(Vector((0, 0, 0.152)), {"pelvis": 1.0}, (16, 31.6)), cap1=(Vector((0, 0, 0.36)), {"chest": 1.0}, (16, 0.5)))

    # the ruffle round her bottom (like the romper's frilly bloomers)
    rr = [up_ring(z, rx, ry, 8) for z, rx, ry in ((0.178, 0.078, 0.070), (0.158, 0.100, 0.090), (0.140, 0.103, 0.093))]
    BL.loft(b, rr, [{"pelvis": 1.0}] * 3, MAT_SKIN, lambda i, k: (48 + k / 8 * 15, 0.5 + i * 5))

    for side, sx in (("L", 1), ("R", -1)):
        ua, fa, hd, ru = "upper_arm." + side, "forearm." + side, "hand." + side, "ruffle." + side
        x0 = 32 if sx > 0 else 48
        # sleeve: shoulder down to the wrist, the plaid cuff last
        spec = [(0.348, 0.086, 0.038, {ua: 1.0}), (0.312, 0.0885, 0.0355, {ua: 1.0}), (0.275, 0.092, 0.033, blend(ua, fa, 0.5)),
                (0.24, 0.0945, 0.031, {fa: 1.0}), (0.207, 0.097, 0.030, {fa: 1.0})]
        sr = [BL.ring(Vector((x * sx, 0, z)), Vector((0, 0, -1)), Vector((0, -1, 0)), r, r, 6) for z, x, r, _ in spec]
        BL.loft(b, sr, [w for _, _, _, w in spec], MAT_CLOTH, lambda i, k, x0=x0: (x0 + k / 6 * 15.5, i / (len(spec) - 1) * 31.0),
                cap0=(Vector((0.08 * sx, 0, 0.356)), {ua: 1.0}, (x0 + 8, 0.5)))
        # the hand: a round mitt
        hs = [(0.205, 0.024, {fa: 0.5, hd: 0.5}), (0.19, 0.032, {hd: 1.0}), (0.166, 0.034, {hd: 1.0}), (0.143, 0.023, {hd: 1.0})]
        hr = [BL.ring(Vector((0.097 * sx, 0, z)), Vector((0, 0, -1)), Vector((0, -1, 0)), r, r * 0.85, 6) for z, r, _ in hs]
        BL.loft(b, hr, [w for _, _, w in hs], MAT_SKIN, lambda i, k: (50 + k / 6 * 10, 24 + i * 2),
                cap1=(Vector((0.097 * sx, 0, 0.132)), {hd: 1.0}, (55, 30)))
        # the frill on the shoulder
        fr = [BL.ring(Vector((0.082 * sx, 0, z)), Vector((0, 0, -1)), Vector((0, -1, 0)), r1, r2, 6)
              for z, r1, r2 in ((0.376, 0.032, 0.040), (0.360, 0.046, 0.056), (0.343, 0.048, 0.058))]
        BL.loft(b, fr, [{ru: 1.0}] * 3, MAT_SKIN, lambda i, k: (48 + k / 6 * 15, 0.5 + i * 4.5),
                cap0=(Vector((0.082 * sx, 0, 0.382)), {ru: 1.0}, (55, 0.5)))
        # leg: thigh to the ankle, chubby; the foot is its own tube
        th, sh, ft = "thigh." + side, "shin." + side, "foot." + side
        lx = 0.042 * sx
        lx0 = 0 if sx > 0 else 16
        leg = [(0.19, 0.0, 0.046, {th: 1.0}), (0.16, -0.002, 0.047, {th: 1.0}), (0.125, -0.004, 0.041, {th: 1.0}),
               (0.105, -0.003, 0.036, blend(th, sh, 0.5)), (0.08, -0.001, 0.031, {sh: 1.0}), (0.057, 0.0, 0.0265, {sh: 1.0}),
               (0.043, 0.0, 0.0245, blend(sh, ft, 0.5))]
        lr = [BL.ring(Vector((lx, y, z)), Vector((0, 0, -1)), Vector((0, -1, 0)), r, r * 0.95, 6) for z, y, r, _ in leg]
        BL.loft(b, lr, [w for _, _, _, w in leg], MAT_SKIN, lambda i, k, lx0=lx0: (lx0 + k / 6 * 15.5, i / (len(leg) - 1) * 28.0 + 0.5),
                cap0=(Vector((lx, 0, 0.2)), {th: 1.0}, (lx0 + 8, 0.5)), cap1=(Vector((lx, 0, 0.034)), {ft: 1.0}, (lx0 + 8, 30)))
        fo = [(0.025, 0.036, 0.020, 0.022), (0.0, 0.030, 0.026, 0.027), (-0.035, 0.024, 0.021, 0.029), (-0.062, 0.018, 0.016, 0.024)]
        fr2 = [BL.ring(Vector((lx, y, z)), Vector((0, -1, 0)), Vector((0, 0, 1)), r1, r2, 6) for y, z, r1, r2 in fo]
        BL.loft(b, fr2, [{ft: 1.0}] * 4, MAT_SKIN, lambda i, k, lx0=lx0: (lx0 + k / 6 * 15.5, 27 + i),
                cap0=(Vector((lx, 0.042, 0.035)), {ft: 1.0}, (lx0 + 8, 27)), cap1=(Vector((lx, -0.076, 0.017)), {ft: 1.0}, (lx0 + 8, 31)))

    # three tiny rocks to stack
    for name, c, r in ROCKS:
        rk = [BL.ring(Vector(c) + Vector((0, 0, dz)), Z, Vector((0, -1, 0)), r1 * r, r2 * r, 6)
              for dz, r1, r2 in ((-0.7 * r, 0.75, 0.85), (0.0, 1.0, 1.0), (0.65 * r, 0.7, 0.8))]
        BL.loft(b, rk, [{name: 1.0}] * 3, MAT_SKIN, lambda i, k: (48 + k / 6 * 15, 12.5 + i * 4.5),
                cap0=(Vector(c) + Vector((0, 0, -0.85 * r)), {name: 1.0}, (55, 13)), cap1=(Vector(c) + Vector((0, 0, 0.9 * r)), {name: 1.0}, (55, 21)))
    return b, set(head_faces)


def face_uv(p):
    """Planar projection of the face picture onto the front of the head (s to the right, t down)."""
    return ((p.x / FACE_W_M + 0.5) * 32.0, (HEAD_C.z + FACE_TOP - p.z) / (FACE_TOP + FACE_BOTTOM) * 32.0)


def make_mesh_object(b, head_faces, mats):
    verts = [v[0] for v in b.verts]

    def normal(idx):
        n = Vector((0, 0, 0))
        for i in range(len(idx)):
            a, c = verts[idx[i]], verts[idx[(i + 1) % len(idx)]]
            n += Vector(((a.y - c.y) * (a.z + c.z), (a.z - c.z) * (a.x + c.x), (a.x - c.x) * (a.y + c.y)))
        return n.normalized()

    faces = []
    for fi, (idx, mat, uvs) in enumerate(b.faces):
        if fi in head_faces:
            centre = sum((verts[i] for i in idx), Vector()) / len(idx)
            n = normal(idx)
            if n.dot(centre - HEAD_C) < 0:
                n = -n
            rel = centre - HEAD_C
            if n.dot(Vector((0, -1, 0))) > 0.35 and rel.z < FACE_TOP - 0.004:
                mat, uvs = MAT_FACE, [face_uv(verts[i]) for i in idx]
            elif rel.z > 0.025 or (rel.y > -0.01 and rel.z > -0.08) or (rel.z > -0.03 and abs(rel.x) > 0.075):
                mat = MAT_SKIN   # hair: wrapped round the head by angle and height (the hair strip is nearly uniform, so the seam never shows)
                uvs = []
                for i in idx:
                    p = verts[i] - HEAD_C
                    ang = math.atan2(p.x, -p.y) / (2 * math.pi) + 0.5
                    uvs.append((32 + ang * 15.0, max(0.5, min(30.5, 30.0 - (p.z + 0.06) / 0.17 * 28.0))))
            else:
                mat = MAT_SKIN
                uvs = [(50 + 5 * ((j * 7) % 3) * 0.5, 25 + (j % 2)) for j in range(len(idx))]
        faces.append((idx, mat, uvs))

    me = bpy.data.meshes.new("Avriella")
    me.from_pydata([tuple(v) for v in verts], [], [f[0] for f in faces])
    me.update()
    for m in mats:
        me.materials.append(m)
    uvl = me.uv_layers.new(name="UVMap")
    sizes = {MAT_CLOTH: (CLOTH_W, CLOTH_H), MAT_SKIN: (SKIN_W, SKIN_H), MAT_FACE: (FACE_W, FACE_H)}
    for poly, (idx, mat, uvs) in zip(me.polygons, faces):
        poly.material_index = mat
        poly.use_smooth = True
        w, h = sizes[mat]
        for li, (s, t) in zip(poly.loop_indices, uvs):
            uvl.data[li].uv = (s / w, 1.0 - t / h)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("Avriella", me)
    bpy.context.scene.collection.objects.link(obj)
    for name in BONE_NAMES:
        obj.vertex_groups.new(name=name)
    for vi, (_, wts) in enumerate(b.verts):
        for bone, wt in wts.items():
            obj.vertex_groups[bone].add([vi], wt, "REPLACE")
    return obj


def make_armature():
    arm = bpy.data.armatures.new("AvriellaRig")
    obj = bpy.data.objects.new("AvriellaRig", arm)
    bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    for name, head, tail, parent in BONES:
        eb = arm.edit_bones.new(name)
        eb.head, eb.tail = Vector(head), Vector(tail)
        eb.roll = 0.0
        if parent:
            eb.parent = arm.edit_bones[parent]
            eb.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.display_type = "STICK"
    return obj


# ---------------------------------------------------------------------------------------------------------------------------------
# Animation. Each clip is a function of time giving, per bone, a rotation about the model's own axes at that joint (X: pitch, + tips her
# nose down and swings a hanging leg back; Y: roll, + tips the top of a bone towards her left; Z: turn, + turns the front towards her left)
# and a shift of the bone. The keys are converted to each bone's own space.
# ---------------------------------------------------------------------------------------------------------------------------------
TAU = 2 * math.pi
SIDES = (("L", 1), ("R", -1))
RIG = None   # set while building: the stack clip asks the rig where her hand is to carry a rock there


class Pose(dict):
    def __init__(self):
        super().__init__()
        self.loc = {}

    def add(self, bone, x=0.0, y=0.0, z=0.0, k=1.0):
        ox, oy, oz = self.get(bone, (0.0, 0.0, 0.0))
        self[bone] = (ox + x * k, oy + y * k, oz + z * k)
        return self

    def shift(self, bone, x=0.0, y=0.0, z=0.0, k=1.0):
        ox, oy, oz = self.loc.get(bone, (0.0, 0.0, 0.0))
        self.loc[bone] = (ox + x * k, oy + y * k, oz + z * k)
        return self

    def lift(self, dz, k=1.0, dy=0.0):
        return self.shift("root", 0.0, dy, dz, k)


def lerp_pose(a, b, k):
    out = Pose()
    for bone in set(a) | set(b):
        pa, pb = a.get(bone, (0, 0, 0)), b.get(bone, (0, 0, 0))
        out[bone] = tuple(pa[i] + (pb[i] - pa[i]) * k for i in range(3))
    for bone in set(a.loc) | set(b.loc):
        pa, pb = a.loc.get(bone, (0, 0, 0)), b.loc.get(bone, (0, 0, 0))
        out.loc[bone] = tuple(pa[i] + (pb[i] - pa[i]) * k for i in range(3))
    return out


def hide_rocks(p):
    for name, _, _ in ROCKS:
        p.shift(name, 0, 0, -0.8)


def jiggle(p, ph, amt=1.0):
    """The tuft and the shoulder frills bob a little behind the body."""
    p.add("tuft", x=0.22 * amt * math.sin(ph * 2 + 1.0), z=0.2 * amt * math.sin(ph + 0.4))
    for s, sx in SIDES:
        p.add("ruffle." + s, y=0.12 * amt * sx * math.sin(ph * 2 + 0.5), x=0.06 * amt * math.sin(ph * 2))


def sit_base(p, k=1.0, lean=0.12):
    """Sitting up on her bottom with her legs out in a V, leaning a little forward, hands in her lap."""
    p.lift(-0.101, k)
    p.add("pelvis", x=0.04, k=k)
    p.add("chest", x=lean, k=k)
    p.add("neck", x=-0.10, k=k)
    p.add("head", x=-0.06, k=k)
    for s, sx in SIDES:
        p.add("thigh." + s, x=-1.42, z=0.32 * sx, k=k)
        p.add("shin." + s, x=0.55, k=k)
        p.add("foot." + s, x=0.35, k=k)
        p.add("upper_arm." + s, x=-0.30, y=-0.16 * sx, k=k)
        p.add("forearm." + s, x=-0.65, k=k)
        p.add("hand." + s, x=-0.1, k=k)
    return p


def lay_pose(p, k=1.0, froggy=1.0):
    """On her back: arms up beside her head, knees bent up and out."""
    p.add("root", x=-1.5708, k=k)
    p.lift(0.10, k, dy=-0.31)
    p.add("neck", x=0.0, k=k)
    for s, sx in SIDES:
        p.add("upper_arm." + s, x=-2.55, y=-0.55 * sx, k=k)
        p.add("forearm." + s, x=-0.5, k=k)
        p.add("thigh." + s, x=-1.0 * froggy, z=0.55 * sx * froggy, k=k)
        p.add("shin." + s, x=1.55 * froggy, k=k)
        p.add("foot." + s, x=0.2, k=k)
    return p


def clip_idle(t, T):
    """Sitting, looking about, patting her knees, rocking a little."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=0.1 + 0.03 * math.sin(ph))
    hide_rocks(p)
    look = env(t, 0.5, 1.1, 2.2, 2.9)
    p.add("head", z=0.45 * math.sin(ph) * look, x=0.05 * math.sin(ph * 2))
    p.add("neck", z=0.15 * math.sin(ph) * look)
    p.add("chest", x=0.012 * math.sin(ph * 3), z=0.05 * math.sin(ph))
    pat = max(0.0, math.sin(ph * 4)) * env(t, 3.2, 3.5, 4.3, 4.6)
    for s, sx in SIDES:
        p.add("upper_arm." + s, x=-0.12 * pat * (1 if s == "L" else -1))
    jiggle(p, ph)
    return p


def clip_sit(t, T):
    """Plain sitting: a slow breath and a happy wiggle."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=0.14)
    hide_rocks(p)
    p.add("chest", x=0.02 * math.sin(ph * 2), z=0.04 * math.sin(ph))
    p.add("head", z=0.12 * math.sin(ph), y=0.08 * math.sin(ph + 0.8))
    for s, sx in SIDES:
        p.add("foot." + s, x=0.12 * math.sin(ph * 2 + sx))
    jiggle(p, ph, 0.6)
    return p


def clip_crawl(t, T):
    """Crawling on hands and knees: left hand with right knee, then the other pair; head up and looking ahead."""
    p = Pose()
    ph = t / T * TAU
    hide_rocks(p)
    p.lift(-0.036 + 0.006 * math.sin(ph * 2))
    p.add("pelvis", x=1.22, z=0.1 * math.sin(ph), y=0.04 * math.sin(ph))
    p.add("chest", x=-0.14, z=-0.12 * math.sin(ph))
    p.add("neck", x=-0.55)
    p.add("head", x=-0.45, z=0.06 * math.sin(ph))
    for s, sx in SIDES:
        a = ph + (0.0 if s == "L" else math.pi)         # arm phase
        b = a + math.pi                                 # the opposite knee goes with it
        swing = math.sin(a)
        lift = max(0.0, math.cos(a))                    # hand is off the ground on the way forward
        p.add("upper_arm." + s, x=-1.22 + 0.5 * swing, y=-0.08 * sx)
        p.add("forearm." + s, x=-0.35 - 0.95 * lift)
        p.add("hand." + s, x=0.25 * lift)
        sb = math.sin(b)
        lb = max(0.0, math.cos(b))
        p.add("thigh." + s, x=-1.22 + 0.42 * sb, z=0.12 * sx)
        p.add("shin." + s, x=1.57 - 0.2 * sb + 0.5 * lb)
        p.add("foot." + s, x=0.0)
    jiggle(p, ph, 1.3)
    return p


def clip_wave(t, T):
    """Sitting and waving hello with her right hand, head tilted, bouncing a little."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=0.05)
    hide_rocks(p)
    up = env(t, 0.0, 0.3, T - 0.3, T)
    p.add("upper_arm.R", x=0.30 * up, y=2.25 * up)          # (cancels the sit pose's forward lean of the arm, so it goes up and out)
    p.add("forearm.R", x=0.65 * up, y=0.15 * up + 0.5 * math.sin(ph * 3) * up)
    p.add("hand.R", y=0.25 * math.sin(ph * 3 + 0.6) * up)
    p.add("upper_arm.L", x=-0.2, y=-0.1)
    p.add("head", z=0.0, y=-0.14 * up + 0.04 * math.sin(ph * 3))
    p.add("chest", x=0.02 * math.sin(ph * 6), y=-0.05 * up)
    jiggle(p, ph, 1.0)
    return p


def clip_giggle(t, T):
    """Shaking with laughter: shoulders bouncing, head back, kicking her feet, hands to her cheeks."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=-0.1)
    hide_rocks(p)
    shake = math.sin(ph * 8)
    p.lift(0.006 * abs(shake))
    p.add("chest", x=0.07 * shake - 0.05)
    p.add("neck", x=-0.12)
    p.add("head", x=-0.2 + 0.05 * shake, z=0.1 * math.sin(ph * 2))
    for s, sx in SIDES:
        p.add("upper_arm." + s, x=-0.9, y=-0.35 * sx, k=1.0)
        p.add("forearm." + s, x=-1.0 + 0.1 * shake)
        p.add("hand." + s, x=-0.2)
        p.add("thigh." + s, x=0.28 * math.sin(ph * 4 + (0 if s == "L" else math.pi)))
        p.add("shin." + s, x=0.25 * math.sin(ph * 4 + (0 if s == "L" else math.pi) + 1.2))
    jiggle(p, ph * 2, 1.6)
    return p


CLAP_KEYS = {}


def clap_base(p):
    sit_base(p, lean=-0.02)
    hide_rocks(p)
    p.add("head", x=-0.06)
    return p


def clap_keys():
    if not CLAP_KEYS:
        rest = (-0.30, -0.16, 0.0, -0.65)
        for s, sx in SIDES:
            apart = solve_arm(s, Vector((0.16 * sx, -0.17, 0.22)), rest, clap_base)
            together = solve_arm(s, Vector((0.012 * sx, -0.17, 0.21)), apart, clap_base)
            CLAP_KEYS[s] = (apart, together)
    return CLAP_KEYS


def clip_clap(t, T):
    """Sitting and clapping: hands out wide and then together in front of her, again and again."""
    p = clap_base(Pose())
    ph = t / T * TAU
    c = 0.5 + 0.5 * math.sin(ph * 4 - 1.2)          # 0 hands apart, 1 hands together
    c = ease(c)
    ck = clap_keys()
    for s, sx in SIDES:
        apart, together = ck[s]
        arm_pose(p, s, tuple(apart[i] + (together[i] - apart[i]) * c for i in range(4)))
    p.add("chest", x=0.03 * c)
    p.add("head", x=0.05 * c)
    jiggle(p, ph, 1.4)
    return p


def clip_tumble(t, T):
    """Rolling along the ground on her back and front, round and round: how she gets about (she cannot crawl yet). The body lies across her way."""
    p = Pose()
    ph = t / T
    lay_pose(p, 1.0, froggy=0.5)
    hide_rocks(p)
    p.loc["root"] = (0.31, 0.0, 0.10)       # she lies along x, not y (the turn below), and rolls about her own middle
    p.add("root", y=-ph * TAU, z=1.5708)
    p.add("thigh.L", x=0.25 * math.sin(ph * TAU * 2))
    p.add("thigh.R", x=-0.25 * math.sin(ph * TAU * 2))
    p.add("head", x=0.0, z=0.0)
    jiggle(p, ph * TAU, 1.0)
    return p


def clip_kick(t, T):
    """On her back kicking her legs over and over, arms waving, laughing."""
    p = Pose()
    ph = t / T * TAU
    lay_pose(p, 1.0, froggy=0.0)
    hide_rocks(p)
    for s, sx in SIDES:
        a = ph * 4 + (0.0 if s == "L" else math.pi)
        p.add("thigh." + s, x=-1.35 + 0.65 * math.sin(a), z=0.28 * sx)
        p.add("shin." + s, x=0.6 + 0.65 * max(0.0, -math.sin(a)) + 0.3)
        p.add("foot." + s, x=0.2 + 0.2 * math.sin(a))
        p.add("upper_arm." + s, x=0.3 * math.sin(ph * 2 + sx), z=0.0)
    p.add("chest", x=0.03 * math.sin(ph * 8))
    p.add("head", z=0.2 * math.sin(ph * 2), x=0.1)
    p.shift("root", 0, 0, 0.004 * abs(math.sin(ph * 4)))
    jiggle(p, ph * 2, 1.5)
    return p


def chew_base(p):
    sit_base(p, lean=0.1)
    hide_rocks(p)
    p.add("head", x=0.04)
    return p


CHEW_KEY = {}


def mouth_world(pose):
    apply_pose(RIG, pose)
    bpy.context.view_layer.update()
    pb = RIG.pose.bones["head"]
    return (pb.matrix @ RIG.data.bones["head"].matrix_local.inverted()) @ Vector((-0.012, -0.108, 0.425))


def chew_arm():
    if not CHEW_KEY:
        base = chew_base(Pose())
        rest = (-0.30, 0.16, 0.0, -0.65)
        CHEW_KEY["rest"] = (-0.30, -0.16, 0.0, -0.65)
        CHEW_KEY["mouth"] = solve_arm("R", mouth_world(base), CHEW_KEY["rest"], chew_base)
    return CHEW_KEY


def clip_chew(t, T):
    """Sitting and gnawing on a tiny rock held up to her mouth: the head bobs, the hand wiggles, a contented smile in between."""
    ck = chew_arm()
    ph = t / T * TAU
    p = chew_base(Pose())
    k = env(t, 0.0, 0.4, T - 0.45, T)
    arm = tuple(ck["rest"][i] + (ck["mouth"][i] - ck["rest"][i]) * k for i in range(4))
    arm_pose(p, "R", arm)
    chomp = math.sin(ph * 6) * k
    p.add("head", x=0.07 * chomp)
    p.add("upper_arm.R", x=0.03 * chomp)
    p.add("forearm.R", x=0.05 * chomp)
    p.add("upper_arm.L", x=-0.4 * k, y=0.05)
    p.add("forearm.L", x=-0.3 * k)
    jiggle(p, ph, 0.8)
    hand = hand_at(p, "R")
    d = hand - ROCKS_REST["rock.1"] + Vector((0, -0.005, 0.0))
    p.loc["rock.1"] = (d.x, d.y, d.z)
    return p


def clip_roll(t, T):
    """Tips over from sitting onto her back, rolls right over (a full turn) and sits up again."""
    sit = clip_sit(0.0, 3.0)
    lay = lay_pose(Pose(), 1.0)
    hide_rocks(lay)
    lay.add("chest", x=0.0)
    down = ease((t - 0.0) / 0.3)
    upk = ease((t - (T - 0.35)) / 0.3)
    roll = ease((t - 0.3) / (T - 0.3 - 0.4)) * TAU
    k = down * (1.0 - upk)
    p = lerp_pose(sit, lay, k)
    p.add("root", y=roll)
    # while she rolls the arms stay up and the legs pedal a little
    p.add("thigh.L", x=0.2 * math.sin(roll * 2))
    p.add("thigh.R", x=-0.2 * math.sin(roll * 2))
    return p


def clip_nap(t, T):
    """Asleep on her back, arms by her head, a slow breath and a little twitch of the foot."""
    p = Pose()
    ph = t / T * TAU
    lay_pose(p, 1.0)
    hide_rocks(p)
    p.add("chest", x=0.035 * math.sin(ph))
    p.add("head", z=0.12, x=0.05)
    p.add("foot.L", x=0.18 * env(t, 1.8, 1.9, 2.0, 2.2))
    p.add("hand.R", x=0.2 * math.sin(ph))
    p.shift("root", 0, 0, 0.003 * math.sin(ph))
    jiggle(p, ph, 0.25)
    return p


def clip_stand(t, T):
    """Standing on her own two feet and wobbling: arms out for balance, knees giving, never quite falling."""
    p = Pose()
    ph = t / T * TAU
    hide_rocks(p)
    w1, w2 = math.sin(ph), math.sin(ph * 2 + 0.7)
    p.lift(-0.012 + 0.006 * w2)
    p.add("root", y=0.10 * w1, x=0.05 * w2)
    p.add("pelvis", y=-0.08 * w1, z=0.06 * w2)
    p.add("chest", y=0.06 * w1, x=0.06)
    p.add("neck", x=-0.10)
    p.add("head", y=-0.10 * w1, x=-0.04)
    for s, sx in SIDES:
        lagging = w1 * sx
        p.add("thigh." + s, x=-0.22 - 0.1 * lagging, y=0.0, z=0.06 * sx)
        p.add("shin." + s, x=0.42 + 0.12 * lagging)
        p.add("foot." + s, x=-0.2 - 0.02 * lagging)
        p.add("upper_arm." + s, x=-0.1, y=(-0.95 + 0.3 * w1 * sx) * sx)
        p.add("forearm." + s, x=-0.5, y=0.0)
        p.add("hand." + s, y=-0.2 * w2 * sx)
    jiggle(p, ph, 1.5)
    return p


def clip_reach(t, T):
    """Sitting and reaching one hand out for something with her whole body: fingers wiggling, mouth open."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=0.5)
    hide_rocks(p)
    ext = env(t, 0.0, 0.25, T - 0.25, T)
    wig = math.sin(ph * 4)
    p.add("chest", x=0.2 * ext, z=0.12 * ext)
    p.add("neck", x=-0.3 * ext)
    p.add("head", x=-0.2 * ext)
    p.add("upper_arm.R", x=-1.55 * ext, y=0.05 * ext, z=0.15 * ext)
    p.add("forearm.R", x=0.5 * ext)
    p.add("hand.R", x=-0.2 * ext + 0.35 * wig * ext)
    p.add("upper_arm.L", x=-0.6 * ext, y=-0.3 * ext)
    jiggle(p, ph, 1.2)
    return p


def clip_babble(t, T):
    """Babbling away: arms flapping, head bobbing, bouncing on her bottom."""
    p = Pose()
    ph = t / T * TAU
    sit_base(p, lean=0.0)
    hide_rocks(p)
    bob = abs(math.sin(ph * 2))
    p.lift(0.008 * bob)
    p.add("chest", x=-0.04 * bob)
    p.add("head", x=-0.12 * bob - 0.05, y=0.1 * math.sin(ph * 2), z=0.12 * math.sin(ph))
    for s, sx in SIDES:
        p.add("upper_arm." + s, x=-0.75, y=(-0.55 + 0.25 * math.sin(ph * 2 + (0 if s == "L" else math.pi))) * sx)
        p.add("forearm." + s, x=-0.9 + 0.35 * math.sin(ph * 2 + 1.0))
        p.add("hand." + s, x=-0.2)
    jiggle(p, ph, 1.2)
    return p


STACK_LEG_SPREAD = 0.38   # extra splay of her legs while she stacks, so there is room between her knees for the rocks
TOWER = [Vector((0.0, -0.15, 0.034)), Vector((0.0, -0.15, 0.09)), Vector((0.0, -0.15, 0.134))]   # rock centres: on the floor, on that, on top
ARM_KEYS = {}   # solved once: arm angles that put a hand at a place (see solve_arm)


def stack_base(p):
    sit_base(p, lean=0.5)
    hide_rocks(p)
    for name, _, _ in ROCKS:
        p.loc.pop(name, None)
    for s, sx in SIDES:
        p.add("thigh." + s, z=STACK_LEG_SPREAD * sx)
    p.add("neck", x=-0.2)
    p.add("head", x=0.1)
    return p


def arm_pose(p, side, ang):
    sx = 1 if side == "L" else -1
    ux, uy, uz, fx = ang
    p["upper_arm." + side] = (ux, uy * sx, uz * sx)
    p["forearm." + side] = (fx, 0.0, 0.0)
    return p


def hand_at(pose, side):
    """Where the middle of her mitt is once `pose` is applied (the stack clip puts a rock there)."""
    apply_pose(RIG, pose)
    bpy.context.view_layer.update()
    return RIG.pose.bones["hand." + side].tail.copy()


def solve_arm(side, target, start, base=None):
    """A little coordinate search for the arm angles (upper arm pitch, roll, turn; elbow bend) that put her hand on `target`."""
    best = list(start)

    def cost(ang):
        pose = (base or stack_base)(Pose())
        arm_pose(pose, side, ang)
        d = (hand_at(pose, side) - target).length
        return d + 0.01 * sum((a - b) ** 2 for a, b in zip(ang, start))

    c = cost(best)
    for step in (0.5, 0.25, 0.12, 0.06, 0.03, 0.015, 0.007):
        for _ in range(8):
            improved = False
            for i in range(4):
                for sgn in (1, -1):
                    trial = list(best)
                    trial[i] += sgn * step
                    tc = cost(trial)
                    if tc < c - 1e-7:
                        best, c, improved = trial, tc, True
            if not improved:
                break
    return tuple(best)


def arm_keys():
    if ARM_KEYS:
        return ARM_KEYS
    rest = (-0.30, -0.16, 0.0, -0.65)
    r = {"rest": rest}
    r["clap"] = solve_arm("R", Vector((-0.012, -0.15, 0.2)), rest)
    r["grab2"] = solve_arm("R", ROCKS_REST["rock.2"] + Vector((0, 0, 0.01)), rest)
    r["put2"] = solve_arm("R", TOWER[1] + Vector((0, 0, 0.01)), r["grab2"])
    r["knock0"] = solve_arm("R", Vector((-0.13, -0.15, 0.13)), r["put2"])
    r["knock1"] = solve_arm("R", Vector((0.12, -0.15, 0.12)), r["knock0"])
    l = {"rest": rest}
    l["clap"] = solve_arm("L", Vector((0.012, -0.15, 0.2)), rest)
    l["grab3"] = solve_arm("L", ROCKS_REST["rock.3"] + Vector((0, 0, 0.01)), rest)
    l["put3"] = solve_arm("L", TOWER[2] + Vector((0, 0, 0.01)), l["grab3"])
    ARM_KEYS["R"], ARM_KEYS["L"] = r, l
    return ARM_KEYS


def key_blend(keys, t):
    """keys: [(time, angles)] sorted; the angles at time t, eased between the neighbours, held outside."""
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, a0), (t1, a1) in zip(keys, keys[1:]):
        if t <= t1:
            k = ease((t - t0) / (t1 - t0)) if t1 > t0 else 1.0
            return tuple(a0[i] + (a1[i] - a0[i]) * k for i in range(4))
    return keys[-1][1]


def clip_stack(t, T):
    """Sitting with her legs out wide, stacking three tiny rocks: lifts the second onto the first, then the third, claps, and knocks them down."""
    ak = arm_keys()
    R, L = ak["R"], ak["L"]
    rk = [(0.0, R["rest"]), (0.45, R["grab2"]), (0.5, R["grab2"]), (0.95, R["put2"]), (1.05, R["put2"]), (1.3, R["rest"]), (2.1, R["rest"]),
          (2.25, R["clap"]), (2.5, R["clap"]), (2.6, R["knock0"]), (2.95, R["knock1"]), (3.15, R["knock1"]), (3.4, R["rest"])]
    lk = [(0.0, L["rest"]), (1.3, L["rest"]), (1.75, L["grab3"]), (1.8, L["grab3"]), (2.25, L["put3"]), (2.3, L["put3"]), (2.45, L["clap"]),
          (2.5, L["clap"]), (2.65, L["rest"]), (3.4, L["rest"])]
    # the clap is the middle of the tower's wait; the first hand is back before the second goes
    p = stack_base(Pose())
    arm_pose(p, "R", key_blend(rk, t))
    arm_pose(p, "L", key_blend(lk, t))
    cl = max(0.0, math.sin(t * TAU * 3.5)) * env(t, 2.28, 2.35, 2.45, 2.5)
    p.add("upper_arm.R", z=-0.25 * cl)
    p.add("upper_arm.L", z=0.25 * cl)
    p.add("chest", x=0.05 * math.sin(t / T * TAU * 2))
    jiggle(p, t / T * TAU, 0.8)
    handR, handL = hand_at(p, "R"), hand_at(p, "L")

    def centre(name):
        return ROCKS_REST[name]

    def fall(a, b, k, h):
        return a * (1 - k) + b * k + Vector((0, 0, h * math.sin(k * math.pi)))

    # rock 2: waits, rides the right hand up onto the first rock, sits there, tumbles down when she knocks it
    if t < 0.5:
        pos2 = centre("rock.2")
    elif t < 0.95:
        pos2 = handR
    elif t < 2.7:
        pos2 = TOWER[1]
    elif t < 3.15:
        pos2 = fall(TOWER[1], centre("rock.2"), (t - 2.7) / 0.45, 0.04)
    else:
        pos2 = centre("rock.2")
    if t < 1.8:
        pos3 = centre("rock.3")
    elif t < 2.25:
        pos3 = handL
    elif t < 2.85:
        pos3 = TOWER[2]
    elif t < 3.3:
        pos3 = fall(TOWER[2], centre("rock.3"), (t - 2.85) / 0.45, 0.06)
    else:
        pos3 = centre("rock.3")
    for name, pos in (("rock.1", centre("rock.1")), ("rock.2", pos2), ("rock.3", pos3)):
        d = pos - centre(name)
        p.loc[name] = (d.x, d.y, d.z)
    return p


# name, seconds, loops, function. The game refers to clips by position in this list (see shared/avriella_model.h).
CLIPS = [
    ("idle", 5.0, True, clip_idle),
    ("sit", 3.0, True, clip_sit),
    ("crawl", 1.0, True, clip_crawl),
    ("wave", 1.6, True, clip_wave),
    ("giggle", 1.2, True, clip_giggle),
    ("clap", 1.2, True, clip_clap),
    ("roll", 2.0, False, clip_roll),
    ("nap", 3.0, True, clip_nap),
    ("stand", 2.4, True, clip_stand),
    ("reach", 1.2, True, clip_reach),
    ("babble", 1.0, True, clip_babble),
    ("rocks", 3.4, True, clip_stack),
    ("tumble", 1.4, True, clip_tumble),
    ("kick", 1.2, True, clip_kick),
    ("chew", 2.4, True, clip_chew),
]


def frames_of(seconds, loops):
    n = int(round(seconds * FPS))
    return n if loops else n + 1   # a loop's last frame is its first again; a one-shot keeps its last


def apply_pose(rig, pose):
    for pb in rig.pose.bones:
        rest = rig.data.bones[pb.name].matrix_local.to_quaternion()
        x, y, z = pose.get(pb.name, (0.0, 0.0, 0.0))
        q_model = Euler((x, y, z), "XYZ").to_quaternion()
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = rest.inverted() @ q_model @ rest
        pb.location = rest.inverted() @ Vector(pose.loc.get(pb.name, (0.0, 0.0, 0.0)))


def make_actions(rig):
    rig.animation_data_create()
    actions = []
    for name, seconds, loops, fn in CLIPS:
        act = bpy.data.actions.new(name)
        act.use_fake_user = True
        rig.animation_data.action = act
        n = frames_of(seconds, loops)
        for f in range(n):
            apply_pose(rig, fn(f / FPS, seconds))
            for pb in rig.pose.bones:
                pb.keyframe_insert("rotation_quaternion", frame=f)
                pb.keyframe_insert("location", frame=f)
        act["avriella_loop"] = loops
        act["avriella_fps"] = FPS
        actions.append(act)
        # keep each clip on its own NLA track so the glTF and FBX exporters write all of them
        track = rig.animation_data.nla_tracks.new()
        track.name = name
        track.strips.new(name, 0, act)
        track.mute = True
    rig.animation_data.action = actions[0]
    return actions


def build():
    global RIG
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.name = "Avriella"

    cloth = make_image("avriella_cloth", paint_cloth(), CLOTH_W, CLOTH_H)
    skin = make_image("avriella_skin", paint_skin(), SKIN_W, SKIN_H)
    faces = [make_image("avriella_face_%s" % n, paint_face(n), FACE_W, FACE_H) for n in FACES]
    mats = [BL.make_material("AvriellaCloth", cloth), BL.make_material("AvriellaSkin", skin), BL.make_material("AvriellaFace", faces[0])]

    b, head_faces = build_mesh()
    mesh = make_mesh_object(b, head_faces, mats)
    rig = make_armature()
    RIG = rig
    mesh.parent = rig
    mod = mesh.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    make_actions(rig)
    scene.frame_start, scene.frame_end = 0, frames_of(CLIPS[0][1], True) - 1
    print("Avriella: %d vertices, %d triangles, %d bones, %d clips" % (len(mesh.data.vertices), sum(len(p.vertices) - 2 for p in mesh.data.polygons),
                                                                       len(rig.data.bones), len(CLIPS)))
    for im in [cloth, skin] + faces:
        im.pack()
        im.use_fake_user = True   # the other faces are only used by the game, so keep them in the file
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "avriella.blend"), compress=True)
    return mesh, rig


def export_interchange():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "avriella.glb"), export_format="GLB", export_animations=True,
                              export_animation_mode="ACTIONS", export_force_sampling=True, export_frame_step=1)
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "avriella.fbx"), use_selection=False, add_leaf_bones=False, bake_anim=True,
                             bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, path_mode="COPY", embed_textures=True,
                             axis_forward="-Y", axis_up="Z")


if __name__ == "__main__":
    build()
    if "--no-export" not in sys.argv:
        export_interchange()
