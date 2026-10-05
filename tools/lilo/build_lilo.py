"""Builds Lilo the cat in Blender: a low poly, N64 style model with small hand-painted textures, a skeleton and animation clips.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0):
    blender -b --python tools/lilo/build_lilo.py
    python3 tools/lilo/build_lilo.py
It writes assets/lilo/lilo.blend, lilo.glb, lilo.fbx and the textures as PNG. Then run export_lilo.py to turn the .blend into the game's
shared/lilo_model.h. Everything is made from code so it can be rebuilt and tweaked; you can also edit lilo.blend by hand in Blender and only
run the export.

The look follows the photo of Lilo: a grey-brown mackerel tabby with a white bib, belly, muzzle and blaze, white front legs and feet, a dark
ringed tail with a near-black tip, big ears, green-gold eyes and a pink nose. Sizes are in metres here (she is about half a metre long);
the export scales them to game units.

Coordinates: Blender's Z is up and Lilo faces -Y (towards the camera in Front view). Her left is +X.
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Euler, Matrix, Quaternion, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "lilo")
FPS = 20   # the game's own update rate; every frame of every clip is a key

# ---------------------------------------------------------------------------------------------------------------------------------
# Textures. Two small images, like an N64 character: a 64x32 fur atlas and a 32x32 face (in three versions for blinking). Painted per
# pixel in sRGB, then cut to 5 bits per channel, which is what the game's RGBA16 format holds.
# ---------------------------------------------------------------------------------------------------------------------------------
FUR_W, FUR_H = 64, 32
FACE_W, FACE_H = 32, 32

WHITE = (0.93, 0.92, 0.89)
TABBY = (0.50, 0.48, 0.44)
TABBY_LIGHT = (0.60, 0.58, 0.54)
TABBY_WARM = (0.56, 0.47, 0.38)
STRIPE = (0.20, 0.19, 0.17)
TAIL = (0.27, 0.26, 0.24)
TAIL_DARK = (0.12, 0.11, 0.10)
EAR_PINK = (0.78, 0.6, 0.58)
MOUTH = (0.42, 0.16, 0.19)
NOSE = (0.82, 0.50, 0.48)
IRIS = (0.62, 0.62, 0.26)
IRIS_RIM = (0.36, 0.44, 0.20)
PUPIL = (0.05, 0.05, 0.05)


def mix(a, b, k):
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(3))


def hash01(x, y, seed=0):
    n = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def jitter(c, x, y, seed, amount=0.035):
    j = (hash01(x, y, seed) - 0.5) * 2 * amount
    return tuple(min(1.0, max(0.0, v + j)) for v in c)


def quant5(c):
    return tuple(round(min(1.0, max(0.0, v)) * 31) / 31 for v in c)


def paint_fur():
    img = [[(0, 0, 0)] * FUR_W for _ in range(FUR_H)]
    for t in range(FUR_H):
        v = (t + 0.5) / FUR_H
        for s in range(FUR_W):
            if s < 32:     # body: around (0 = belly, 0.5 = spine) by length (front to back)
                u = (s + 0.5) / 32
                a = min(u, 1 - u) * 2                                   # 0 underneath, 1 along the spine
                white_to = 0.46 if v < 0.22 else (0.30 if v < 0.72 else 0.20)
                if a < white_to:
                    c = WHITE
                elif a < white_to + 0.07:
                    c = mix(WHITE, TABBY_LIGHT, 0.6)
                else:
                    base = mix(TABBY_WARM, TABBY, min(1.0, (a - 0.3) * 2.2)) if v > 0.45 else TABBY
                    stripe = ((v * 7.0 + 0.18 * math.sin(a * 7.0)) % 1.0) < 0.27 and a > white_to + 0.1
                    c = STRIPE if (stripe or a > 0.9) else base
            elif s < 40:   # front legs: white with tabby on the outside of the upper leg
                u = (s - 32 + 0.5) / 8
                back = 0.25 < u < 0.75
                if t < 13 and back:
                    c = STRIPE if (t % 5 == 1) else TABBY
                elif t < 5:
                    c = mix(TABBY_LIGHT, WHITE, 0.4)
                else:
                    c = WHITE
            elif s < 48:   # hind legs: tabby thigh and shin, white feet
                u = (s - 40 + 0.5) / 8
                if t < 20:
                    inner = u < 0.12 or u > 0.88
                    c = TABBY_LIGHT if inner else (STRIPE if (t % 5 == 2) else (TABBY_WARM if t < 9 else TABBY))
                    if 16 <= t and 0.4 < u < 0.6:
                        c = STRIPE
                elif t < 22:
                    c = mix(TABBY_LIGHT, WHITE, 0.5)
                else:
                    c = WHITE
            elif s < 56:   # tail: dark rings and a near-black tip
                u = (s - 48 + 0.5) / 8
                if t >= 24:
                    c = TAIL_DARK
                else:
                    c = TAIL_DARK if ((t / 4.5) % 1.0) < 0.42 else TAIL
                    if u < 0.15 or u > 0.85:
                        c = mix(c, TABBY, 0.35)
            else:          # head fur (back of the head and the backs of the ears), inside of the ears, inside of the mouth
                if t < 22:
                    c = STRIPE if (t % 5 == 3 and 57 <= s <= 62) else TABBY
                elif t < 27:
                    c = EAR_PINK if 57 <= s <= 62 else mix(EAR_PINK, TABBY, 0.5)
                else:
                    c = MOUTH
            img[t][s] = quant5(jitter(c, s, t, 1))
    return img


def paint_face(eyes):
    """eyes: 0 open, 1 half shut, 2 shut. s runs to the right as you look at her (her left), t runs down."""
    img = [[(0, 0, 0)] * FACE_W for _ in range(FACE_H)]
    eye_c = [(9.4, 11.6), (22.6, 11.6)]
    for t in range(FACE_H):
        for s in range(FACE_W):
            x, y = s + 0.5, t + 0.5
            dx = x - 16.0
            c = TABBY
            # the "M" on the forehead and the stripes over the crown
            for col in (-5.5, -2.4, 2.4, 5.5):
                if abs(dx - col) < 0.75 and y < 8.5 - abs(col) * 0.35:
                    c = STRIPE
            # stripes from the outer corner of each eye back over the cheek
            for ex, ey in eye_c:
                side = 1 if ex > 16 else -1
                ox = ex + side * 3.6
                if 0 <= (x - ox) * side < 4.5 and abs((y - ey - 0.8) - (x - ox) * side * 0.55) < 0.7:
                    c = STRIPE
            # white blaze between the eyes widening into the muzzle, white chin and lower cheeks
            blaze = y > 4.5 and abs(dx) < 0.7 + (y - 4.5) * 0.22
            muzzle = y > 15.5 and abs(dx) < 3.6 + (y - 15.5) * 1.0
            lower = y > 23.0 and abs(dx) < 13
            if blaze or muzzle or lower:
                c = WHITE
            elif (y > 14.5 and abs(dx) < 4.6 + (y - 14.5) * 1.0) or (y > 3.5 and abs(dx) < 1.4 + (y - 3.5) * 0.22):
                c = mix(WHITE, TABBY_LIGHT, 0.55)
            # eyes
            for ex, ey in eye_c:
                ex_, ey_ = (x - ex) / 3.3, (y - ey) / 2.9
                r = math.hypot(ex_, ey_)
                if r < 1.22:
                    if eyes == 2:     # shut: fur with a dark curve
                        c = STRIPE if abs(ey_ - 0.25 * (1 - ex_ * ex_)) < 0.28 and abs(ex_) < 1.1 else TABBY
                    else:
                        if r > 1.0:
                            c = STRIPE                       # dark rim
                        else:
                            c = mix(IRIS, IRIS_RIM, r * 0.8)
                            if math.hypot((x - ex) / 1.55, (y - ey) / 2.1) < 1.0:
                                c = PUPIL
                            if abs(x - (ex - 1.2)) < 0.6 and abs(y - (ey - 1.3)) < 0.6:
                                c = (1.0, 1.0, 1.0)
                        if eyes == 1 and ey_ < 0.05:        # half shut: the upper lid comes down
                            c = TABBY if ey_ < -0.15 else STRIPE
            # nose: a pink inverted triangle; mouth: a small dark "w" under it
            if 18.4 < y < 21.3 and abs(dx) < (21.3 - y) * 0.75:
                c = NOSE
            if (abs(dx) < 0.5 and 21.0 < y < 22.6) or (0.6 < abs(dx) < 2.2 and abs(y - (22.4 + (abs(dx) - 1.4) ** 2 * 0.4)) < 0.45):
                c = STRIPE
            img[t][s] = quant5(jitter(c, s, t, 7 + eyes, 0.025))
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
# The skeleton. Heads are the joints the parts turn about.
# ---------------------------------------------------------------------------------------------------------------------------------
TAIL_BASE = Vector((0.0, 0.185, 0.245))
TAIL_DIR = Vector((0.0, 0.85, 0.53)).normalized()
TAIL_SEG = 0.074

# The head is modelled at a plain size and then made bigger and set a little lower and further back (a cat's big kitten head, N64 style).
HEAD_CENTRE = Vector((0, -0.215, 0.355))
HEAD_SCALE = 1.32
HEAD_SHIFT = Vector((0, 0.012, -0.014))


def hp(p):
    return HEAD_CENTRE + (Vector(p) - HEAD_CENTRE) * HEAD_SCALE + HEAD_SHIFT


def hp_inv(p):
    return (Vector(p) - HEAD_SHIFT - HEAD_CENTRE) / HEAD_SCALE + HEAD_CENTRE


def tup(v):
    return (v.x, v.y, v.z)

BONES = [   # name, head, tail, parent
    ("root", (0, 0, 0), (0, 0, 0.08), None),
    ("pelvis", (0, 0.13, 0.23), (0, 0.0, 0.23), "root"),
    ("chest", (0, 0.0, 0.225), (0, -0.13, 0.23), "pelvis"),
    ("neck", (0, -0.165, 0.255), (0, -0.205, 0.315), "chest"),
    ("head", tup(hp((0, -0.21, 0.325))), tup(hp((0, -0.21, 0.41))), "neck"),
    ("jaw", tup(hp((0, -0.245, 0.322))), tup(hp((0, -0.29, 0.312))), "head"),
]
for side, sx in (("L", 1), ("R", -1)):
    BONES += [
        ("ear." + side, tup(hp((0.044 * sx, -0.217, 0.39))), tup(hp((0.058 * sx, -0.217, 0.46))), "head"),
        ("upper_arm." + side, (0.038 * sx, -0.125, 0.195), (0.038 * sx, -0.13, 0.11), "chest"),
        ("forearm." + side, (0.038 * sx, -0.13, 0.11), (0.038 * sx, -0.135, 0.035), "upper_arm." + side),
        ("paw." + side, (0.038 * sx, -0.135, 0.035), (0.038 * sx, -0.16, 0.008), "forearm." + side),
        ("thigh." + side, (0.05 * sx, 0.12, 0.205), (0.05 * sx, 0.085, 0.135), "pelvis"),
        ("shin." + side, (0.05 * sx, 0.085, 0.135), (0.05 * sx, 0.15, 0.065), "thigh." + side),
        ("foot." + side, (0.05 * sx, 0.15, 0.065), (0.05 * sx, 0.125, 0.006), "shin." + side),
    ]
for i in range(5):
    h = TAIL_BASE + TAIL_DIR * (TAIL_SEG * i)
    BONES.append(("tail.%d" % (i + 1), tuple(h), tuple(h + TAIL_DIR * TAIL_SEG), "pelvis" if i == 0 else "tail.%d" % i))
BONE_NAMES = [b[0] for b in BONES]

# ---------------------------------------------------------------------------------------------------------------------------------
# The mesh: lofted rings (8 or 6 sided) for the body, neck, head, legs and tail, plus a jaw and two ears. About 600 triangles.
# Each vertex gets one or two bone weights; each face corner gets a texel coordinate in its material's image.
# ---------------------------------------------------------------------------------------------------------------------------------
MAT_FUR, MAT_FACE = 0, 1


class Builder:
    def __init__(self):
        self.verts = []      # (Vector, {bone: weight})
        self.faces = []      # (indices, material, [(s, t) texels])

    def vert(self, p, weights):
        self.verts.append((Vector(p), dict(weights)))
        return len(self.verts) - 1

    def face(self, idx, mat, uvs):
        self.faces.append((list(idx), mat, list(uvs)))


def frame_for(d, ref):
    d = d.normalized()
    w1 = (ref - d * ref.dot(d)).normalized()
    w2 = d.cross(w1)
    return w1, w2


def ring(center, d, ref, r1, r2, n):
    w1, w2 = frame_for(d, ref)
    return [center + w1 * (r1 * math.cos(2 * math.pi * k / n)) + w2 * (r2 * math.sin(2 * math.pi * k / n)) for k in range(n)]


def loft(b, rings, weights, mat, uv, cap0=None, cap1=None):
    """rings: list of point lists (same n). weights[i]: bone weights for ring i. uv(i, k) -> (s, t) with k from 0 to n (n is the seam).
    cap0/cap1: (point, weights, (s, t)) to close the first/last ring with a fan."""
    n = len(rings[0])
    ids = [[b.vert(p, weights[i]) for p in r] for i, r in enumerate(rings)]
    for i in range(len(rings) - 1):
        for k in range(n):
            k1 = (k + 1) % n
            b.face([ids[i][k], ids[i][k1], ids[i + 1][k1], ids[i + 1][k]], mat,
                   [uv(i, k), uv(i, k + 1), uv(i + 1, k + 1), uv(i + 1, k)])
    for cap, i, flip in ((cap0, 0, True), (cap1, len(rings) - 1, False)):
        if cap is None:
            continue
        c = b.vert(cap[0], cap[1])
        for k in range(n):
            k1 = (k + 1) % n
            tri = [ids[i][k], ids[i][k1], c]
            uvs = [uv(i, k), uv(i, k + 1), cap[2]]
            if flip:
                tri.reverse()
                uvs.reverse()
            b.face(tri, mat, uvs)
    return ids


def blend(a, b, k):
    if k <= 0:
        return {a: 1.0}
    if k >= 1:
        return {b: 1.0}
    return {a: 1.0 - k, b: k}


def build_mesh():
    b = Builder()
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))

    # body: along +Y, rings start at the belly (k = 0) and go up the left side to the spine (k = 4)
    body = [(-0.19, 0.228, 0.036, 0.042), (-0.155, 0.22, 0.068, 0.076), (-0.085, 0.215, 0.079, 0.08), (0.0, 0.218, 0.075, 0.076),
            (0.075, 0.222, 0.078, 0.075), (0.135, 0.228, 0.074, 0.072), (0.18, 0.234, 0.046, 0.048)]
    rings_ = []
    for y, zc, rx, rz in body:
        rings_.append([Vector((rx * math.sin(2 * math.pi * k / 8), y, zc - rz * math.cos(2 * math.pi * k / 8))) for k in range(8)])
    w = [blend("chest", "pelvis", (y + 0.05) / 0.1) for y, _, _, _ in body]
    loft(b, rings_, w, MAT_FUR, lambda i, k: (k / 8 * 32, i / (len(body) - 1) * 31.5),
         cap0=(Vector((0, -0.2, 0.232)), {"chest": 1.0}, (16, 0.5)), cap1=(Vector((0, 0.19, 0.236)), {"pelvis": 1.0}, (16, 31.5)))

    # neck: from inside the chest up into the head; the throat (k = 0) faces forward and down
    pts = [Vector((0, -0.15, 0.25)), Vector((0, -0.18, 0.282)), hp((0, -0.205, 0.322))]
    ref = Vector((0, -0.7, -0.7))
    nr = [ring(p, pts[min(i + 1, 2)] - pts[max(i - 1, 0)], ref, r, r * 0.95, 8) for i, (p, r) in enumerate(zip(pts, (0.058, 0.052, 0.048)))]
    loft(b, nr, [{"chest": 1.0}, {"neck": 1.0}, {"neck": 0.5, "head": 0.5}], MAT_FUR, lambda i, k: (k / 8 * 32, 0.5 + i * 2.0))

    # head: along -Y from the back of the skull to the nose; the front faces get the face picture, the rest the head fur
    head = [(-0.18, 0.36, 0.03, 0.03), (-0.195, 0.362, 0.055, 0.05), (-0.222, 0.36, 0.066, 0.056), (-0.25, 0.352, 0.06, 0.05),
            (-0.272, 0.338, 0.042, 0.034), (-0.29, 0.33, 0.03, 0.024)]
    hr = [[hp(Vector((rx * math.sin(2 * math.pi * k / 8), y, zc - rz * math.cos(2 * math.pi * k / 8)))) for k in range(8)] for y, zc, rx, rz in head]
    first_head_face = len(b.faces)
    loft(b, hr, [{"head": 1.0}] * len(hr), MAT_FUR, lambda i, k: (0, 0),
         cap0=(hp(Vector((0, -0.172, 0.362))), {"head": 1.0}, (0, 0)), cap1=(hp(Vector((0, -0.298, 0.333))), {"head": 1.0}, (0, 0)))
    head_faces = range(first_head_face, len(b.faces))

    # jaw: a small wedge under the muzzle; its top is the inside of the mouth
    jv = [hp(Vector(p)) for p in [(-0.022, -0.245, 0.318), (0.022, -0.245, 0.318), (0.016, -0.288, 0.316), (-0.016, -0.288, 0.316),
                                  (-0.02, -0.245, 0.3), (0.02, -0.245, 0.3), (0.014, -0.286, 0.305), (-0.014, -0.286, 0.305)]]
    ji = [b.vert(p, {"jaw": 1.0}) for p in jv]
    mouth_uv = [(57, 28), (62, 28), (62, 31), (57, 31)]
    b.face([ji[0], ji[3], ji[2], ji[1]], MAT_FUR, mouth_uv)                     # top: inside of the mouth
    for q in ([4, 5, 6, 7], [3, 7, 6, 2], [0, 4, 7, 3], [1, 2, 6, 5], [0, 1, 5, 4]):
        b.face([ji[i] for i in q], MAT_FACE, [None] * 4)                         # projected below

    # ears: a pyramid each, pink inside
    for side, sx in (("L", 1), ("R", -1)):
        base = [hp(Vector((0.017 * sx, -0.236, 0.396))), hp(Vector((0.07 * sx, -0.226, 0.376))), hp(Vector((0.046 * sx, -0.192, 0.386)))]
        apex = hp(Vector((0.06 * sx, -0.217, 0.458)))
        wv = {"ear." + side: 1.0}
        bi = [b.vert(p, wv) for p in base]
        ai = b.vert(apex, wv)
        front = [bi[0], bi[1], ai] if sx > 0 else [bi[1], bi[0], ai]
        b.face(front, MAT_FUR, [(57.5, 26.5), (62.5, 26.5), (60, 22.5)])
        b.face([bi[1], bi[2], ai] if sx > 0 else [bi[2], bi[1], ai], MAT_FUR, [(57, 14), (62, 14), (59.5, 3)])
        b.face([bi[2], bi[0], ai] if sx > 0 else [bi[0], bi[2], ai], MAT_FUR, [(57, 14), (62, 14), (59.5, 3)])
        b.face([bi[0], bi[2], bi[1]] if sx > 0 else [bi[1], bi[2], bi[0]], MAT_FUR, [(58, 10), (60, 10), (59, 8)])

    # legs: six sided tubes, k = 0 at the front of the leg
    fwd = Vector((0, -1, 0))
    for side, sx in (("L", 1), ("R", -1)):
        ua, fa, pw = "upper_arm." + side, "forearm." + side, "paw." + side
        front = [((0.038, -0.122, 0.205), 0.03, 0.028, {ua: 1.0}), ((0.038, -0.127, 0.15), 0.026, 0.025, {ua: 1.0}),
                 ((0.038, -0.13, 0.11), 0.021, 0.02, {ua: 0.5, fa: 0.5}), ((0.038, -0.132, 0.07), 0.018, 0.017, {fa: 1.0}),
                 ((0.038, -0.135, 0.036), 0.016, 0.016, {fa: 0.5, pw: 0.5}), ((0.038, -0.143, 0.018), 0.021, 0.019, {pw: 1.0})]
        th, sh, ft = "thigh." + side, "shin." + side, "foot." + side
        hind = [((0.048, 0.12, 0.215), 0.05, 0.04, {th: 1.0}), ((0.052, 0.105, 0.17), 0.044, 0.036, {th: 1.0}),
                ((0.05, 0.088, 0.135), 0.026, 0.024, {th: 0.5, sh: 0.5}), ((0.05, 0.118, 0.1), 0.02, 0.019, {sh: 1.0}),
                ((0.05, 0.15, 0.065), 0.017, 0.016, {sh: 0.5, ft: 0.5}), ((0.05, 0.139, 0.032), 0.016, 0.015, {ft: 1.0}),
                ((0.05, 0.128, 0.014), 0.02, 0.019, {ft: 1.0})]
        for spec, s0, toe in ((front, 32, Vector((0.038 * sx, -0.148, 0.0))), (hind, 40, Vector((0.05 * sx, 0.123, 0.0)))):
            cs = [Vector((p[0] * sx, p[1], p[2])) for p, _, _, _ in spec]
            rr = []
            for i, (p, r1, r2, _) in enumerate(spec):
                d = cs[min(i + 1, len(cs) - 1)] - cs[max(i - 1, 0)]
                rr.append(ring(cs[i], d, fwd, r1 * 1.2, r2 * 1.2, 6))
            ws = [wt for _, _, _, wt in spec]
            last = ws[-1]
            loft(b, rr, ws, MAT_FUR, lambda i, k, s0=s0, n=len(spec): (s0 + k / 6 * 8, i / (n - 1) * 29 + 0.5),
                 cap0=(cs[0] + (cs[0] - cs[1]).normalized() * 0.012, ws[0], (s0 + 4, 0.5)), cap1=(toe, last, (s0 + 4, 31.5)))

    # tail: six sided, tapering, k = 0 underneath
    n_r = 7
    tpts = [TAIL_BASE + TAIL_DIR * (TAIL_SEG * 5 * i / (n_r - 1)) for i in range(n_r)]
    tr = [ring(p, TAIL_DIR, Vector((0, 0, -1)), 0.026 - 0.012 * i / (n_r - 1), 0.026 - 0.012 * i / (n_r - 1), 6) for i, p in enumerate(tpts)]
    tw = []
    for i in range(n_r):
        f = 5 * i / (n_r - 1)
        j = min(int(f), 4)
        tw.append(blend("tail.%d" % (j + 1), "tail.%d" % min(j + 2, 5), (f - j) if j < 4 else 0))
    loft(b, tr, tw, MAT_FUR, lambda i, k: (48 + k / 6 * 8, i / (n_r - 1) * 29 + 0.5),
         cap0=(TAIL_BASE - TAIL_DIR * 0.01, {"pelvis": 1.0}, (52, 0.5)), cap1=(tpts[-1] + TAIL_DIR * 0.018, {"tail.5": 1.0}, (52, 31.5)))
    return b, head_faces


def face_uv(p):
    """Planar projection of the face picture onto the front of the head (s to the right, t down)."""
    p = hp_inv(p)
    return ((p.x + 0.075) / 0.15 * 32, (0.425 - p.z) / 0.15 * 32)


def make_mesh_object(b, head_faces, mats):
    # per face normals (to decide which head faces show the face picture)
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
            n = normal(idx)
            centre = hp_inv(sum((verts[i] for i in idx), Vector()) / len(idx))
            if n.dot(Vector((0, -1, 0))) > 0.28 and centre.y < -0.24:
                mat, uvs = MAT_FACE, [face_uv(verts[i]) for i in idx]
            else:   # head fur, wrapped around: across by the angle about the head's axis, down by depth
                mat = MAT_FUR
                uvs = []
                for i in idx:
                    p = hp_inv(verts[i])
                    ang = math.atan2(p.x, -(p.z - 0.36)) / (2 * math.pi) + 0.5
                    uvs.append((56.5 + ang * 7, 1 + (p.y + 0.3) / 0.13 * 20))
        elif mat == MAT_FACE and uvs[0] is None:
            uvs = [face_uv(verts[i]) for i in idx]
        faces.append((idx, mat, uvs))

    me = bpy.data.meshes.new("Lilo")
    me.from_pydata([tuple(v) for v in verts], [], [f[0] for f in faces])
    me.update()
    for m in mats:
        me.materials.append(m)
    uvl = me.uv_layers.new(name="UVMap")
    sizes = {MAT_FUR: (FUR_W, FUR_H), MAT_FACE: (FACE_W, FACE_H)}
    for poly, (idx, mat, uvs) in zip(me.polygons, faces):
        poly.material_index = mat
        poly.use_smooth = True
        w, h = sizes[mat]
        for li, (s, t) in zip(poly.loop_indices, uvs):
            uvl.data[li].uv = (s / w, 1.0 - t / h)
    # make every part's faces point outwards
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("Lilo", me)
    bpy.context.scene.collection.objects.link(obj)
    for name in BONE_NAMES:
        obj.vertex_groups.new(name=name)
    for vi, (_, wts) in enumerate(b.verts):
        for bone, wt in wts.items():
            obj.vertex_groups[bone].add([vi], wt, "REPLACE")
    return obj


def make_material(name, image):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 1.0
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.0
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Linear"   # the N64 filtered its textures
    tex.extension = "EXTEND"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def make_armature():
    arm = bpy.data.armatures.new("LiloRig")
    obj = bpy.data.objects.new("LiloRig", arm)
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
# Animation. Each clip is a function of time giving, per bone, a rotation about the model's own axes at that joint (X: pitch, + swings
# a leg back and tips the nose down; Y: roll; Z: turn) and a lift/drop for the root. The keys are converted to each bone's own space.
# ---------------------------------------------------------------------------------------------------------------------------------
TAU = 2 * math.pi
SIDES = ("L", "R")


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def env(t, a, b, c, d):
    """0 before a, eases up to 1 by b, holds, eases down to 0 from c to d."""
    if t < a or t > d:
        return 0.0
    if t < b:
        return ease((t - a) / (b - a))
    if t <= c:
        return 1.0
    return 1.0 - ease((t - c) / (d - c))


class Pose(dict):
    def add(self, bone, x=0.0, y=0.0, z=0.0, k=1.0):
        ox, oy, oz = self.get(bone, (0.0, 0.0, 0.0))
        self[bone] = (ox + x * k, oy + y * k, oz + z * k)
        return self

    def turn(self, bone, axis, angle):
        """An extra rotation about any axis (in the model's axes as carried by the parent), applied after the X/Y/Z ones."""
        if not hasattr(self, "extra"):
            self.extra = {}
        q = Quaternion(Vector(axis).normalized(), angle)
        self.extra[bone] = q @ self.extra.get(bone, Quaternion())
        return self

    def lift(self, dz, k=1.0, dy=0.0):
        self.root = (0.0, getattr(self, "root", (0, 0, 0))[1] + dy * k, getattr(self, "root", (0, 0, 0))[2] + dz * k)
        return self


def tail_curve(p, base_up, sway_amp, sway_ph, curl=0.0, curl_z=0.0, k=1.0):
    p.add("tail.1", x=base_up, k=k)
    for i in range(1, 6):
        p.add("tail.%d" % i, x=curl, z=curl_z + sway_amp * math.sin(sway_ph - i * 0.7), k=k)


def tail_wrap(p, pitch, curl, first=2, k=1.0):
    """Curls the tail sideways along the ground: each segment from `first` turns by `curl` about the world's up axis, which in the tail's
    frame is tipped by the pitch built up so far (the X turns of the pelvis and the first segments; -0.56 of them lays the tail level)."""
    axis = (0.0, math.sin(pitch), math.cos(pitch))
    for i in range(first, 6):
        p.turn("tail.%d" % i, axis, curl * k)


def sit_pose(p, t, k=1.0):
    """Sitting up on her haunches, front legs straight, tail wrapped round to the front (as in the photo)."""
    p.lift(-0.16, k)
    p.add("pelvis", x=-0.72, k=k)
    p.add("neck", x=0.25, k=k)
    p.add("head", x=0.47, k=k)
    for s in SIDES:
        sgn = 1 if s == "L" else -1
        p.add("upper_arm." + s, x=0.62, k=k)
        p.add("forearm." + s, x=0.05, k=k)
        p.add("paw." + s, x=0.05, k=k)
        p.add("thigh." + s, x=-0.25, z=0.25 * sgn, k=k)
        p.add("shin." + s, x=1.75, k=k)
        p.add("foot." + s, x=-1.85, k=k)
    p.add("tail.1", x=-0.6, k=k)
    p.add("tail.2", x=0.76, k=k)
    tail_wrap(p, -0.56, 1.05, 3, k)
    return p


def stand_breath(p, t, k=1.0):
    p.add("chest", x=0.015 * math.sin(t * TAU / 2.4), k=k)
    return p


def clip_idle(t, T):
    p = Pose()
    ph = t / T * TAU
    stand_breath(p, t)
    p.add("neck", x=-0.12)
    p.add("head", z=0.35 * math.sin(ph) * env(t, 0.3, 0.9, 1.9, 2.6), x=0.05 * math.sin(ph * 2))
    p.add("ear.L", x=-0.4 * env(t, 1.2, 1.3, 1.35, 1.5))
    tail_curve(p, 0.55, 0.18, ph, curl=-0.12)
    return p


def legs_gait(p, ph, amp, lift_amt, offsets):
    for name, off in offsets.items():
        side = name[-1]
        a = ph + off * TAU
        swing = math.sin(a)
        lift = max(0.0, -math.cos(a))   # the foot is up while the leg swings forward
        if name.startswith("f"):
            p.add("upper_arm." + side, x=amp * swing - 0.15 * lift)
            p.add("forearm." + side, x=1.1 * lift * lift_amt)
            p.add("paw." + side, x=0.7 * lift * lift_amt)
        else:
            p.add("thigh." + side, x=amp * swing - 0.35 * lift * lift_amt)
            p.add("shin." + side, x=0.5 * lift * lift_amt)
            p.add("foot." + side, x=-0.45 * lift * lift_amt)


def clip_walk(t, T):
    p = Pose()
    ph = t / T * TAU
    legs_gait(p, ph, 0.42, 1.0, {"hL": 0.0, "fL": 0.25, "hR": 0.5, "fR": 0.75})
    p.lift(0.004 * math.sin(ph * 2))
    p.add("pelvis", z=0.05 * math.sin(ph), y=0.03 * math.sin(ph))
    p.add("chest", z=-0.06 * math.sin(ph + 1.5))
    p.add("neck", x=-0.08)
    p.add("head", x=0.04 * math.sin(ph * 2), z=0.04 * math.sin(ph))
    tail_curve(p, 0.75, 0.12, ph, curl=-0.1)
    return p


def clip_run(t, T):
    p = Pose()
    ph = t / T * TAU
    legs_gait(p, ph, 0.8, 1.3, {"fL": 0.0, "fR": 0.1, "hL": 0.5, "hR": 0.6})
    p.lift(0.018 * math.sin(ph + 0.6) + 0.006)
    p.add("pelvis", x=0.12 * math.sin(ph + 1.4))
    p.add("chest", x=-0.16 * math.sin(ph + 1.4))
    p.add("neck", x=0.05)
    p.add("head", x=-0.1 * math.sin(ph + 1.4))
    for s in SIDES:
        p.add("ear." + s, x=0.35)
    tail_curve(p, -0.05, 0.08, ph * 0.5, curl=0.04)
    return p


def clip_jump(t, T):
    p = Pose()
    crouch = env(t, 0.0, 0.16, 0.2, 0.3) + 0.7 * env(t, 0.62, 0.7, 0.72, 0.9)
    air = env(t, 0.22, 0.3, 0.55, 0.66)
    reach = env(t, 0.5, 0.6, 0.64, 0.74)
    h = max(0.0, math.sin(min(1.0, max(0.0, (t - 0.22) / 0.44)) * math.pi)) if 0.22 <= t <= 0.66 else 0.0
    p.lift(0.24 * h - 0.04 * crouch)
    p.add("pelvis", x=-0.25 * air + 0.12 * crouch)
    p.add("chest", x=0.1 * crouch)
    p.add("neck", x=-0.25 * air)
    p.add("head", x=0.15 * air)
    for s in SIDES:
        p.add("upper_arm." + s, x=-0.5 * crouch - 1.0 * air + 0.7 * reach)
        p.add("forearm." + s, x=0.9 * crouch + 0.2 * air)
        p.add("thigh." + s, x=-0.6 * crouch + 0.9 * air - 0.5 * reach)
        p.add("shin." + s, x=0.9 * crouch + 0.3 * air)
        p.add("foot." + s, x=-0.5 * crouch + 0.6 * air)
        p.add("ear." + s, x=0.3 * air)
    tail_curve(p, 0.3 - 0.6 * air, 0.0, 0.0, curl=-0.1 + 0.15 * air)
    return p


def clip_sit(t, T):
    p = Pose()
    ph = t / T * TAU
    sit_pose(p, t)
    p.add("chest", x=0.012 * math.sin(ph))
    p.add("head", z=0.3 * math.sin(ph) * env(t, 0.5, 1.2, 2.4, 3.2), x=-0.05 * math.sin(ph * 2))
    p.add("ear.R", x=-0.35 * env(t, 2.0, 2.08, 2.12, 2.25))
    p.add("tail.5", z=0.35 * math.sin(ph * 2))
    p.add("tail.4", z=0.15 * math.sin(ph * 2 + 0.6))
    return p


def clip_talk(t, T):
    p = Pose()
    ph = t / T * TAU
    sit_pose(p, t)
    mew = max(0.0, math.sin(ph * 2)) ** 0.7
    p.add("jaw", x=0.4 * mew)
    p.add("head", x=-0.22 * mew, y=0.12 * math.sin(ph))
    p.add("neck", x=-0.08 * mew)
    for s in SIDES:
        p.add("ear." + s, x=-0.15 * mew, y=(0.1 if s == "L" else -0.1) * mew)
    p.add("tail.5", z=0.4 * math.sin(ph * 2))
    return p


def clip_groom(t, T):
    p = Pose()
    ph = t / T * TAU
    sit_pose(p, t)
    lick = math.sin(ph * 4)
    p.add("upper_arm.L", x=-1.45, z=-0.25)
    p.add("forearm.L", x=1.7)
    p.add("paw.L", x=0.6 + 0.12 * lick)
    p.add("neck", x=0.25)
    p.add("head", x=0.2 + 0.08 * lick, z=0.35, y=-0.25)
    p.add("jaw", x=0.15 + 0.1 * max(0.0, lick))
    p.add("tail.5", z=0.25 * math.sin(ph))
    return p


def clip_sleep(t, T):
    """Curled up like a loaf: paws tucked under, head low, eyes shut, tail round the side."""
    p = Pose()
    ph = t / T * TAU
    p.lift(-0.135)
    p.add("pelvis", x=0.02 * math.sin(ph))
    p.add("chest", x=0.04 + 0.015 * math.sin(ph))
    p.add("neck", x=-0.05)
    p.add("head", x=0.12, z=0.2)
    for s in SIDES:
        sgn = 1 if s == "L" else -1
        p.add("upper_arm." + s, x=-0.9)
        p.add("forearm." + s, x=2.4)
        p.add("paw." + s, x=0.4)
        p.add("thigh." + s, x=-1.1, z=0.12 * sgn)
        p.add("shin." + s, x=1.92)
        p.add("foot." + s, x=-2.0)
        p.add("ear." + s, x=-0.15)
    p.add("tail.1", x=-1.3)
    p.add("tail.2", x=0.74)
    tail_wrap(p, -0.56, -0.9, 3)
    return p


def clip_stretch(t, T):
    k = env(t, 0.0, 0.5, 1.4, 2.0)
    p = Pose()
    p.lift(-0.03, k)
    p.add("pelvis", x=0.05, k=k)
    p.add("chest", x=0.38, k=k)
    p.add("neck", x=-0.5, k=k)
    p.add("head", x=-0.25, k=k)
    p.add("jaw", x=0.45 * env(t, 0.6, 0.8, 1.1, 1.3))
    for s in SIDES:
        p.add("upper_arm." + s, x=-1.25, k=k)
        p.add("forearm." + s, x=0.15, k=k)
        p.add("paw." + s, x=-0.2, k=k)
        p.add("thigh." + s, x=0.25, k=k)
        p.add("ear." + s, x=0.25, k=k)
    tail_curve(p, 1.0 * k, 0.0, 0.0, curl=-0.05)
    return p


def clip_pounce(t, T):
    p = Pose()
    crouch = env(t, 0.0, 0.25, 0.85, 0.95)
    leap = env(t, 0.88, 0.95, 1.2, 1.32)
    land = env(t, 1.25, 1.32, 1.36, 1.5)
    h = math.sin((t - 0.9) / 0.45 * math.pi) if 0.9 <= t <= 1.35 else 0.0
    p.lift(0.14 * h - 0.06 * crouch - 0.03 * land)
    wig = math.sin(t * 28.0) * crouch
    p.add("pelvis", x=0.1 * crouch - 0.3 * leap, z=0.12 * wig)
    p.add("chest", x=0.12 * crouch)
    p.add("neck", x=-0.25 * crouch)
    p.add("head", x=-0.1 * crouch + 0.2 * leap)
    for s in SIDES:
        p.add("upper_arm." + s, x=-0.35 * crouch - 1.1 * leap)
        p.add("forearm." + s, x=0.75 * crouch + 0.2 * leap)
        p.add("thigh." + s, x=-0.75 * crouch + 0.8 * leap)
        p.add("shin." + s, x=1.1 * crouch + 0.2 * leap)
        p.add("foot." + s, x=-0.5 * crouch + 0.5 * leap)
        p.add("ear." + s, x=0.25 * crouch)
    tail_curve(p, -0.2 * crouch + 0.3, 0.5 * crouch, t * 22.0, curl=-0.05)
    return p


def clip_happy(t, T):
    p = Pose()
    ph = t / T * TAU
    hop = abs(math.sin(ph))
    p.lift(0.03 * hop)
    p.add("pelvis", x=-0.08 * hop)
    p.add("head", x=-0.15, z=0.1 * math.sin(ph))
    for s in SIDES:
        p.add("upper_arm." + s, x=-0.25 * hop)
        p.add("thigh." + s, x=0.2 * hop)
        p.add("ear." + s, x=-0.15)
    p.add("tail.1", x=1.15)
    for i in range(2, 6):
        p.add("tail.%d" % i, x=-0.08, z=0.12 * math.sin(ph * 2 - i))
    return p


# name, seconds, loops, function. The game refers to clips by position in this list (see shared/lilo_model.h).
CLIPS = [
    ("idle", 2.6, True, clip_idle),
    ("walk", 1.0, True, clip_walk),
    ("run", 0.5, True, clip_run),
    ("jump", 0.9, False, clip_jump),
    ("sit", 3.2, True, clip_sit),
    ("talk", 1.0, True, clip_talk),
    ("groom", 2.0, True, clip_groom),
    ("sleep", 3.0, True, clip_sleep),
    ("stretch", 2.0, False, clip_stretch),
    ("pounce", 1.5, False, clip_pounce),
    ("happy", 0.6, True, clip_happy),
]


def frames_of(seconds, loops):
    n = int(round(seconds * FPS))
    return n if loops else n + 1   # a loop's last frame is its first again; a one-shot keeps its last


def apply_pose(rig, pose):
    for pb in rig.pose.bones:
        rest = rig.data.bones[pb.name].matrix_local.to_quaternion()
        x, y, z = pose.get(pb.name, (0.0, 0.0, 0.0))
        q_model = getattr(pose, "extra", {}).get(pb.name, Quaternion()) @ Euler((x, y, z), "XYZ").to_quaternion()
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = rest.inverted() @ q_model @ rest
        if pb.name == "root":
            root = getattr(pose, "root", (0.0, 0.0, 0.0))
            pb.location = rest.inverted() @ Vector(root)
        else:
            pb.location = (0, 0, 0)


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
                if pb.name == "root":
                    pb.keyframe_insert("location", frame=f)
        act["lilo_loop"] = loops
        act["lilo_fps"] = FPS
        actions.append(act)
        # keep each clip on its own NLA track so the glTF and FBX exporters write all of them
        track = rig.animation_data.nla_tracks.new()
        track.name = name
        track.strips.new(name, 0, act)
        track.mute = True
    rig.animation_data.action = actions[0]
    return actions


def build():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.name = "Lilo"

    fur = make_image("lilo_fur", paint_fur(), FUR_W, FUR_H)
    faces = [make_image("lilo_face_%s" % n, paint_face(i), FACE_W, FACE_H) for i, n in enumerate(("open", "half", "shut"))]
    mats = [make_material("LiloFur", fur), make_material("LiloFace", faces[0])]

    b, head_faces = build_mesh()
    mesh = make_mesh_object(b, set(head_faces), mats)
    rig = make_armature()
    mesh.parent = rig
    mod = mesh.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    make_actions(rig)
    scene.frame_start, scene.frame_end = 0, frames_of(CLIPS[0][1], True) - 1
    print("Lilo: %d vertices, %d triangles, %d bones, %d clips" % (len(mesh.data.vertices), sum(len(p.vertices) - 2 for p in mesh.data.polygons),
                                                                   len(rig.data.bones), len(CLIPS)))
    for im in [fur] + faces:
        im.pack()
        im.use_fake_user = True   # the half-shut and shut faces are only used by the game, so keep them in the file
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "lilo.blend"), compress=True)
    return mesh, rig


def export_interchange():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "lilo.glb"), export_format="GLB", export_animations=True,
                              export_animation_mode="ACTIONS", export_force_sampling=True, export_frame_step=1)
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "lilo.fbx"), use_selection=False, add_leaf_bones=False, bake_anim=True,
                             bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, path_mode="COPY", embed_textures=True,
                             axis_forward="-Y", axis_up="Z")


if __name__ == "__main__":
    build()
    if "--no-export" not in sys.argv:
        export_interchange()
