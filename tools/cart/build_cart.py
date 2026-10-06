"""Builds the Lon Lon Buggy in Blender: a small wooden cart for two in the Ocarina of Time style, low poly with one small hand-painted texture.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0):
    blender -b --python tools/cart/build_cart.py
    python3 tools/cart/build_cart.py
It writes assets/cart/cart.blend, cart.glb and the texture as PNG. Then run export_cart.py to turn the .blend into the game's shared/cart_model.h
and shared/cart_geometry.h. Everything is made from code so it can be rebuilt and tweaked; you can also edit cart.blend by hand in Blender and only
run the export (keep the object names, the empties and the one material).

The design: Lon Lon Ranch built it from what they had. A plank deck on two dark beams, four big spoked wheels with iron tyres, two of Epona's
saddles one behind the other (the driver sits astride like on a horse, so Link's own riding poses fit), a red saddle blanket with a gold stripe,
handlebars with a carved horse head on the front, and in the back a Goron firebox (a stone stove with a bomb flower glowing inside) that
pushes it along and puffs smoke from a little iron chimney. A red pennant flies from a pole at the back.

Colours come from the art style guide: leather brown #86672D, saddle brown #671E09, crest red #AD3725, gold #F7D622 -> #BE9834, warm
planks and steel greys. Bold flat blocks, chunky shapes, about 700 triangles.

Coordinates: metres, Blender's Z is up, the cart faces -Y (towards the camera in Front view), its left is +X. The export turns this into
game units (x100) with Y up, facing +Z.
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "cart")

# ---------------------------------------------------------------------------------------------------------------------------------
# The texture: one 64x32 atlas (4 KB as RGBA16, exactly what the N64's texture memory holds). Each region is painted per pixel in sRGB and
# cut to 5 bits per channel. Regions are (s, t, w, h) in texels, t from the top.
# ---------------------------------------------------------------------------------------------------------------------------------
TEX_W, TEX_H = 64, 32
R_PLANK = (0, 0, 32, 16)      # deck and side boards: four planks with dark grout lines and painted grain
R_BEAM = (32, 0, 16, 16)      # dark stained wood: beams, wheels, the horse head
R_SADDLE = (48, 0, 16, 16)    # saddle leather with stitching
R_IRON = (0, 16, 16, 16)      # iron with rivets: tyres, axles, handlebars, chimney
R_CLOTH = (16, 16, 16, 16)    # red cloth with a gold stripe: blanket, pennant, mane
R_GOLD = (32, 16, 8, 16)      # gold trim
R_STONE = (40, 16, 12, 16)    # the firebox's bricks
R_EMBER = (52, 16, 12, 16)    # the bomb flower glowing in the firebox's mouth


def hash01(x, y, seed=0):
    n = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def hexc(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


def mix(a, b, k):
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(3))


def shade(c, k):
    return tuple(min(1.0, max(0.0, v * k)) for v in c)


def jitter(c, x, y, seed, amount=0.03):
    j = (hash01(x, y, seed) - 0.5) * 2 * amount
    return tuple(min(1.0, max(0.0, v + j)) for v in c)


def quant5(c):
    return tuple(round(min(1.0, max(0.0, v)) * 31) / 31 for v in c)


PLANK = hexc("#B07D45")
PLANK_DARK = hexc("#7A5128")
GROUT = hexc("#3B2410")
BEAM = hexc("#5C3B1F")
BEAM_DARK = hexc("#3A2412")
SADDLE = hexc("#7A2C10")
SADDLE_HI = hexc("#A4502A")
STITCH = hexc("#D8B784")
IRON = hexc("#6F7782")
IRON_DARK = hexc("#3D434C")
IRON_HI = hexc("#AEB8C2")
CLOTH = hexc("#AD3725")
CLOTH_DARK = hexc("#7C2218")
GOLD = hexc("#F7D622")
GOLD_DARK = hexc("#BE9834")
STONE = hexc("#8C8B78")
MORTAR = hexc("#4A473C")
EMBER_HOT = hexc("#FFE27A")
EMBER = hexc("#FF8A1E")
SOOT = hexc("#2A1A12")


def paint():
    img = [[(1.0, 0.0, 1.0)] * TEX_W for _ in range(TEX_H)]

    def put(s, t, c):
        img[t][s] = quant5(c)

    # planks: four boards across the region, each 4 texels, the last row of each a dark groove; long streaks of grain and a butt joint
    x0, y0, w, h = R_PLANK
    for t in range(h):
        board, row = t // 4, t % 4
        tint = 0.92 + 0.16 * hash01(board, 7, 1)
        joint = 5 + int(hash01(board, 3, 2) * 22)
        for s in range(w):
            if row == 3:
                c = GROUT
            else:
                streak = hash01(s // 3 + board * 11, row, 3)
                c = mix(PLANK, PLANK_DARK, 0.55 if streak > 0.78 else 0.12 * hash01(s, t, 4))
                c = shade(c, tint * (1.06 if row == 0 else 1.0))
                if s == joint or s == (joint + 16) % w:
                    c = mix(GROUT, PLANK_DARK, 0.4)
                if hash01(s, t, 5) > 0.97:
                    c = shade(PLANK_DARK, 0.8)   # a knot
            put(x0 + s, y0 + t, jitter(c, s, t, 6, 0.02))

    # dark beams: lengthwise grain
    x0, y0, w, h = R_BEAM
    for t in range(h):
        for s in range(w):
            g = hash01(s // 4, t, 7)
            c = mix(BEAM, BEAM_DARK, 0.5 if g > 0.7 else 0.1)
            if t in (0, h - 1):
                c = shade(BEAM_DARK, 0.9)
            put(x0 + s, y0 + t, jitter(c, s, t, 8, 0.025))

    # saddle leather: a lighter middle, a stitched border
    x0, y0, w, h = R_SADDLE
    for t in range(h):
        for s in range(w):
            edge = min(s, t, w - 1 - s, h - 1 - t)
            c = mix(SADDLE, SADDLE_HI, max(0.0, 0.5 - abs(t - h * 0.4) / h))
            if edge == 0:
                c = shade(SADDLE, 0.7)
            elif edge == 1 and (s + t) % 3 != 0:
                c = STITCH
            put(x0 + s, y0 + t, jitter(c, s, t, 9, 0.02))

    # iron: brushed bands, darker edges, a rivet in each corner
    x0, y0, w, h = R_IRON
    for t in range(h):
        for s in range(w):
            edge = min(s, t, w - 1 - s, h - 1 - t)
            c = mix(IRON, IRON_HI, 0.35 if t in (3, 4) else 0.05 * hash01(s, t, 10))
            if edge == 0:
                c = IRON_DARK
            for rs, rt in ((2, 2), (w - 3, 2), (2, h - 3), (w - 3, h - 3)):
                if s == rs and t == rt:
                    c = IRON_HI
                elif s == rs + 1 and t == rt + 1:
                    c = IRON_DARK
            put(x0 + s, y0 + t, jitter(c, s, t, 11, 0.02))

    # cloth: crest red, a faint weave, a gold stripe near the bottom edge
    x0, y0, w, h = R_CLOTH
    for t in range(h):
        for s in range(w):
            c = CLOTH if (s + t) % 2 == 0 else mix(CLOTH, CLOTH_DARK, 0.25)
            if t in (11, 12):
                c = GOLD if s % 4 != 3 else GOLD_DARK
            elif t == 13 or t == 10:
                c = CLOTH_DARK
            put(x0 + s, y0 + t, jitter(c, s, t, 12, 0.02))

    # gold: bright to deep, top to bottom
    x0, y0, w, h = R_GOLD
    for t in range(h):
        for s in range(w):
            put(x0 + s, y0 + t, jitter(mix(GOLD, GOLD_DARK, t / (h - 1)), s, t, 13, 0.02))

    # firebox bricks: rows of four-texel bricks, offset each row, dark mortar, soot towards the top
    x0, y0, w, h = R_STONE
    for t in range(h):
        for s in range(w):
            row = t // 4
            if t % 4 == 3 or (s + (2 if row % 2 else 0)) % 6 == 5:
                c = MORTAR
            else:
                c = shade(STONE, 0.9 + 0.2 * hash01(row, (s + (2 if row % 2 else 0)) // 6, 14))
            c = mix(c, SOOT, max(0.0, 0.45 - t / h))
            put(x0 + s, y0 + t, jitter(c, s, t, 15, 0.025))

    # the firebox mouth: an iron frame round a glowing bomb flower
    x0, y0, w, h = R_EMBER
    cx, cy = (w - 1) / 2, (h - 1) / 2 + 1
    for t in range(h):
        for s in range(w):
            d = math.hypot((s - cx) / (w / 2), (t - cy) / (h / 2))
            c = mix(EMBER_HOT, EMBER, min(1.0, d * 1.4))
            c = mix(c, SOOT, max(0.0, min(1.0, (d - 0.62) * 3.0)))
            if min(s, t, w - 1 - s, h - 1 - t) == 0:
                c = IRON_DARK
            put(x0 + s, y0 + t, jitter(c, s, t, 16, 0.02))
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
# Shapes. A Part collects faces (corner positions, UVs in texels) for one rigid piece; each piece becomes one object whose origin is the point it
# turns about (a wheel's centre, the handlebar's column).
# ---------------------------------------------------------------------------------------------------------------------------------
class Part:
    def __init__(self, name, pivot):
        self.name = name
        self.pivot = Vector(pivot)
        self.faces = []   # (list of Vector, list of (s, t))

    def quad(self, corners, region, flip_uv=False, tile=None):
        """One face (3 or 4 corners, outward by the right-hand rule), its texture region stretched over it. Long faces are cut into
        pieces with the texture repeated on each (`tile` metres per repeat), since the game clamps textures rather than wrapping them."""
        x0, y0, w, h = region
        u0, u1, v0, v1 = x0 + 0.5, x0 + w - 0.5, y0 + 0.5, y0 + h - 0.5
        if len(corners) == 3:
            uv = [(u0, v1), (u1, v1), ((u0 + u1) / 2, v0)]
            self.faces.append(([Vector(c) for c in corners], uv))
            return
        c = [Vector(p) for p in corners]
        # the texture's long direction follows the face's long side
        if (c[1] - c[0]).length < (c[3] - c[0]).length:
            c = [c[1], c[2], c[3], c[0]]
        if flip_uv:
            c = [c[2], c[3], c[0], c[1]]
        n = 1 if tile is None else max(1, int(math.ceil((c[1] - c[0]).length / tile - 0.15)))
        for i in range(n):
            a, b = i / n, (i + 1) / n
            p0, p1 = c[0].lerp(c[1], a), c[0].lerp(c[1], b)
            p3, p2 = c[3].lerp(c[2], a), c[3].lerp(c[2], b)
            self.faces.append(([p0, p1, p2, p3], [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]))

    def box(self, centre, size, region, rot=None, tile=None, skip=(), regions=None):
        """A box: centre and full size in metres, turned by `rot` (a Matrix) about its centre. `regions` can give a face its own region by
        name (+x, -x, +y, -y, +z, -z); `skip` leaves faces out (ones that sit against something)."""
        cx, cy, cz = centre
        sx, sy, sz = (v / 2 for v in size)
        rot = rot or Matrix.Identity(3)

        def P(x, y, z):
            return Vector((cx, cy, cz)) + rot @ Vector((x, y, z))

        faces = {
            "+x": [P(sx, -sy, -sz), P(sx, sy, -sz), P(sx, sy, sz), P(sx, -sy, sz)],
            "-x": [P(-sx, sy, -sz), P(-sx, -sy, -sz), P(-sx, -sy, sz), P(-sx, sy, sz)],
            "+y": [P(sx, sy, -sz), P(-sx, sy, -sz), P(-sx, sy, sz), P(sx, sy, sz)],
            "-y": [P(-sx, -sy, -sz), P(sx, -sy, -sz), P(sx, -sy, sz), P(-sx, -sy, sz)],
            "+z": [P(-sx, -sy, sz), P(sx, -sy, sz), P(sx, sy, sz), P(-sx, sy, sz)],
            "-z": [P(-sx, sy, -sz), P(sx, sy, -sz), P(sx, -sy, -sz), P(-sx, -sy, -sz)],
        }
        for k, f in faces.items():
            if k in skip:
                continue
            self.quad(f, (regions or {}).get(k, region), tile=tile)

    def wheel(self, radius, width, sides=10):
        """A spoked cart wheel about the X axis, centred on the part's pivot: an iron tyre, dark wooden faces, a hub, and spokes standing out
        from the outer face so the turning reads from a distance."""
        hw = width / 2
        ring = [(math.cos(2 * math.pi * (i + 0.5) / sides), math.sin(2 * math.pi * (i + 0.5) / sides)) for i in range(sides)]
        for i in range(sides):
            (c0, s0), (c1, s1) = ring[i], ring[(i + 1) % sides]
            a = Vector((hw, c0 * radius, s0 * radius))
            b = Vector((hw, c1 * radius, s1 * radius))
            self.quad([Vector((-hw, a.y, a.z)), Vector((-hw, b.y, b.z)), b, a], R_IRON)   # tread
        # the two faces as fans of wooden wedges (a lighter disc of dark wood)
        x0, y0, w, h = R_BEAM
        for side in (1, -1):
            x = hw * side
            for i in range(sides):
                (c0, s0), (c1, s1) = ring[i], ring[(i + 1) % sides]
                tri = [Vector((x, 0, 0)), Vector((x, c0 * radius * 0.98, s0 * radius * 0.98)), Vector((x, c1 * radius * 0.98, s1 * radius * 0.98))]
                if side < 0:
                    tri = [tri[0], tri[2], tri[1]]
                uv = [(x0 + w / 2, y0 + h / 2)] + [(x0 + 0.5 + (w - 1) * (0.5 + 0.5 * c), y0 + 0.5 + (h - 1) * (0.5 + 0.5 * s))
                                                   for c, s in ((c0, s0), (c1, s1))]
                if side < 0:
                    uv = [uv[0], uv[2], uv[1]]
                self.faces.append((tri, uv))
        # spokes: two crossed bars proud of each face, and an iron hub
        for side in (1, -1):
            x = (hw + 0.012) * side
            for ang in (0.0, math.pi / 2):
                rot = Matrix.Rotation(ang + math.pi / 4, 3, "X")
                self.box((x, 0, 0), (0.024, radius * 1.8, 0.04), R_PLANK, rot=rot, skip=("+x",) if side < 0 else ("-x",))
            self.box((x + 0.012 * side, 0, 0), (0.03, 0.075, 0.075), R_GOLD, skip=("+x",) if side < 0 else ("-x",))


def rot_x(deg):
    return Matrix.Rotation(math.radians(deg), 3, "X")


def rot_z(deg):
    return Matrix.Rotation(math.radians(deg), 3, "Z")


# ---------------------------------------------------------------------------------------------------------------------------------
# The cart. Every measure is here, so the game's geometry (shared/cart_geometry.h) is written from the same numbers.
# ---------------------------------------------------------------------------------------------------------------------------------
WHEEL_R = 0.22
WHEEL_W = 0.10
WHEEL_X = 0.43
FRONT_Y = -0.46
BACK_Y = 0.42
DECK_Z = 0.37
SADDLE_TOP = 0.535
DRIVER = Vector((0.0, -0.06, SADDLE_TOP))
PASSENGER = Vector((0.0, 0.33, SADDLE_TOP))
BAR_PIVOT = Vector((0.0, -0.40, 0.56))     # the handlebar turns about the steering column here
EXHAUST = Vector((-0.12, 0.60, 0.98))      # the chimney's top, for the smoke
FLAG_TOP = Vector((0.27, 0.62, 1.10))


def build_body():
    b = Part("CartBody", (0, 0, 0))
    # two long beams under the deck, a cross beam at each end
    for x in (0.27, -0.27):
        b.box((x, -0.03, 0.30), (0.08, 1.38, 0.09), R_BEAM, tile=0.5)
    b.box((0, -0.70, 0.31), (0.66, 0.08, 0.10), R_BEAM)                           # the front bumper, with a gold band along it
    b.box((0, -0.745, 0.31), (0.60, 0.012, 0.025), R_GOLD)
    b.box((0, 0.64, 0.31), (0.62, 0.06, 0.09), R_BEAM)
    # the deck and low side boards
    b.box((0, -0.03, DECK_Z), (0.66, 1.18, 0.05), R_PLANK, tile=0.45)
    for x in (0.315, -0.315):
        b.box((x, 0.06, DECK_Z + 0.07), (0.035, 0.98, 0.10), R_PLANK, tile=0.45)
    # axles (iron) and the springs they hang from
    for y in (FRONT_Y, BACK_Y):
        b.box((0, y, WHEEL_R), (2 * WHEEL_X - WHEEL_W, 0.05, 0.05), R_IRON, tile=0.4)
        for x in (0.27, -0.27):
            b.box((x, y, (WHEEL_R + 0.26) / 2), (0.05, 0.07, 0.26 - WHEEL_R + 0.03), R_IRON)
    # mudguards over the wheels: boards bent round the top of each wheel in four pieces
    for x in (WHEEL_X, -WHEEL_X):
        for y in (FRONT_Y, BACK_Y):
            for ang in (-60, -20, 20, 60):
                r = WHEEL_R + 0.05
                a = math.radians(ang)
                b.box((x, y + math.sin(a) * r, WHEEL_R + math.cos(a) * r), (WHEEL_W + 0.04, 0.205, 0.02), R_BEAM, rot=rot_x(-ang))
    # two saddles one behind the other, each on a red blanket with a gold stripe
    for seat in (DRIVER, PASSENGER):
        b.box((0, seat.y, DECK_Z + 0.06), (0.10, 0.20, 0.07), R_BEAM)                       # the post it sits on
        b.box((0, seat.y, DECK_Z + 0.105), (0.40, 0.34, 0.02), R_CLOTH)                     # blanket
        b.box((0, seat.y, SADDLE_TOP - 0.045), (0.24, 0.30, 0.09), R_SADDLE)               # seat
        b.box((0, seat.y - 0.15, SADDLE_TOP + 0.005), (0.14, 0.05, 0.08), R_SADDLE)        # pommel
        b.box((0, seat.y + 0.155, SADDLE_TOP + 0.02), (0.22, 0.04, 0.10), R_SADDLE)        # cantle
        b.box((0, seat.y - 0.165, SADDLE_TOP + 0.05), (0.05, 0.03, 0.03), R_GOLD)          # horn
        for x in (0.15, -0.15):                                                             # stirrups
            b.box((x, seat.y, DECK_Z + 0.11), (0.012, 0.012, 0.15), R_SADDLE)
            b.box((x, seat.y, DECK_Z + 0.03), (0.05, 0.07, 0.015), R_IRON)
    # the steering column, leaning back a little
    b.box((0, -0.46, DECK_Z + 0.09), (0.12, 0.12, 0.14), R_BEAM)
    # the horse head figurehead on the front: neck, head, muzzle, ears, a red mane and gold eyes
    b.box((0, -0.62, 0.52), (0.10, 0.12, 0.30), R_BEAM, rot=rot_x(-25))
    b.box((0, -0.69, 0.68), (0.11, 0.20, 0.12), R_BEAM, rot=rot_x(-10))
    b.box((0, -0.80, 0.65), (0.09, 0.07, 0.08), R_BEAM)
    b.box((0, -0.835, 0.655), (0.07, 0.01, 0.05), R_IRON)                                   # nose
    for x in (0.035, -0.035):
        b.quad([(x, -0.64, 0.73), (x, -0.60, 0.73), (x, -0.62, 0.81)] if x > 0 else
               [(x, -0.60, 0.73), (x, -0.64, 0.73), (x, -0.62, 0.81)], R_BEAM)
        b.box((x * 1.65, -0.73, 0.70), (0.012, 0.03, 0.03), R_GOLD)                         # eyes
    b.box((0, -0.58, 0.64), (0.035, 0.18, 0.12), R_CLOTH, rot=rot_x(-30))                  # mane
    # the Goron firebox in the back: a brick stove, its mouth glowing, an iron chimney with a cap
    b.box((0, 0.60, DECK_Z + 0.15), (0.40, 0.24, 0.25), R_STONE, regions={"+y": R_EMBER, "+z": R_IRON})
    b.box((EXHAUST.x, EXHAUST.y, (DECK_Z + 0.27 + EXHAUST.z) / 2), (0.07, 0.07, EXHAUST.z - DECK_Z - 0.27), R_IRON)
    b.box((EXHAUST.x, EXHAUST.y, EXHAUST.z - 0.01), (0.12, 0.12, 0.03), R_IRON)
    # the pennant: a pole and a red flag with the gold stripe
    b.box((FLAG_TOP.x, FLAG_TOP.y, (DECK_Z + FLAG_TOP.z) / 2), (0.025, 0.025, FLAG_TOP.z - DECK_Z), R_BEAM)
    b.box((FLAG_TOP.x, FLAG_TOP.y, FLAG_TOP.z + 0.015), (0.045, 0.045, 0.03), R_GOLD)
    b.quad([(FLAG_TOP.x, FLAG_TOP.y, FLAG_TOP.z - 0.02), (FLAG_TOP.x, FLAG_TOP.y + 0.30, FLAG_TOP.z - 0.09),
            (FLAG_TOP.x, FLAG_TOP.y, FLAG_TOP.z - 0.20)], R_CLOTH)
    return b


def build_handlebar():
    h = Part("Handlebar", BAR_PIVOT)
    p = BAR_PIVOT
    lean = rot_x(-12)
    h.box((p.x, p.y, p.z - 0.04), (0.05, 0.05, 0.22), R_IRON, rot=lean)                     # column
    h.box((p.x, p.y + 0.03, p.z + 0.09), (0.46, 0.04, 0.04), R_IRON, tile=0.25)             # cross bar
    for x in (0.25, -0.25):
        h.box((x, p.y + 0.03, p.z + 0.09), (0.09, 0.055, 0.055), R_SADDLE)                  # grips
    h.box((p.x, p.y + 0.01, p.z + 0.12), (0.06, 0.06, 0.04), R_GOLD)                        # a gold cap
    return h


def build_wheel(name, x, y):
    w = Part(name, (x, y, WHEEL_R))
    w.wheel(WHEEL_R, WHEEL_W)
    # the part's own faces are made around the origin: move them to the wheel's place
    for corners, _ in w.faces:
        for c in corners:
            c += w.pivot
    return w


WHEELS = (("WheelFL", WHEEL_X, FRONT_Y), ("WheelFR", -WHEEL_X, FRONT_Y), ("WheelBL", WHEEL_X, BACK_Y), ("WheelBR", -WHEEL_X, BACK_Y))


def make_object(part, mat):
    """Turns a Part into a mesh object whose origin is its pivot."""
    verts, faces, uvs = [], [], []
    index = {}
    for corners, uv in part.faces:
        idx = []
        for c in corners:
            local = c - part.pivot
            key = (round(local.x, 5), round(local.y, 5), round(local.z, 5))
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            idx.append(index[key])
        faces.append(idx)
        uvs.append(uv)
    me = bpy.data.meshes.new(part.name)
    me.from_pydata(verts, [], faces)
    me.update()
    me.materials.append(mat)
    uvl = me.uv_layers.new(name="UVMap")
    for poly, uv in zip(me.polygons, uvs):
        poly.use_smooth = False   # flat faces: the faceted N64 look
        for li, (s, t) in zip(poly.loop_indices, uv):
            uvl.data[li].uv = (s / TEX_W, 1.0 - t / TEX_H)
    obj = bpy.data.objects.new(part.name, me)
    obj.location = part.pivot
    bpy.context.scene.collection.objects.link(obj)
    return obj


def make_material(image):
    m = bpy.data.materials.new("CartAtlas")
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


def empty(name, at):
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = "PLAIN_AXES"
    e.empty_display_size = 0.05
    e.location = at
    bpy.context.scene.collection.objects.link(e)
    return e


def build():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = "Cart"
    atlas = make_image("cart_atlas", paint(), TEX_W, TEX_H)
    mat = make_material(atlas)
    root = empty("Cart", (0, 0, 0))
    objs = [make_object(build_body(), mat), make_object(build_handlebar(), mat)]
    objs += [make_object(build_wheel(n, x, y), mat) for n, x, y in WHEELS]
    for o in objs:
        o.parent = root
    # the points the game needs: where the riders sit, where they get on and off, the chimney top, the flag
    for name, at in (("SeatDriver", DRIVER), ("SeatPassenger", PASSENGER), ("ExitDriver", (0.62, DRIVER.y, 0)),
                     ("ExitPassenger", (-0.62, PASSENGER.y, 0)), ("Exhaust", EXHAUST), ("FlagTop", FLAG_TOP)):
        empty(name, at).parent = root
    tris = sum(len(p.vertices) - 2 for o in objs for p in o.data.polygons)
    print("Cart: %d objects, %d vertices, %d triangles" % (len(objs), sum(len(o.data.vertices) for o in objs), tris))
    atlas.pack()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "cart.blend"), compress=True)
    return objs


def export_interchange():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "cart.glb"), export_format="GLB")


if __name__ == "__main__":
    build()
    if "--no-export" not in sys.argv:
        export_interchange()
