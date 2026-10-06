#!/usr/bin/env python3
"""Builds the Fortnite Map's Hyrule Field scenery in Blender, with painted textures, and exports it for the mod.

  pip install bpy numpy pillow        (Blender as a Python module)
  python3 tools/scenery/build_scenery.py

What it does, in order:
  1. paints the textures (leaves, bark, warm rock, strata, turf, snow, petals, reeds ...) for each of the four seasons into assets/scenery/tex/
  2. builds the eight pieces as Blender meshes (oak, hedge, boulder, cliff slab, crag, two flower drifts, cattails), with real materials that use
     those textures and UV maps, and saves assets/scenery/scenery.blend (open it in Blender to look at or change them)
  3. exports shared/scenery_model.h: every triangle corner with its position and, for each season, the texture's colour at its UV, lit the way the
     game's own models are (warm key light, cool fill). The game draws with vertex colours, so the texture is baked into the corners here.
Blender is Z up; the game is Y up, so the export turns (x, y, z) into (x, z, -y). Units are game units (a Link is about 60 tall).
"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageFilter
import bpy, bmesh
from mathutils import Vector

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ASSETS = os.path.join(ROOT, "assets", "scenery")
TEX = os.path.join(ASSETS, "tex")
OUT_H = os.path.join(ROOT, "shared", "scenery_model.h")
SEASONS = ["spring", "summer", "autumn", "winter"]
N = 128   # texture size

# ---------------------------------------------------------------------------------------------------------------- textures
def fbm(seed, scale, octaves=4, stretch=(1, 1)):
    """Smooth value noise in 0..1, tiling roughly: sum of upscaled random grids."""
    rng = np.random.RandomState(seed)
    out = np.zeros((N, N)); amp = 1.0; tot = 0.0
    for o in range(octaves):
        gx = max(2, int(scale * stretch[0] * (2 ** o))); gy = max(2, int(scale * stretch[1] * (2 ** o)))
        g = rng.rand(gy, gx)
        out += amp * np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC)).astype(float) / 255.0
        tot += amp; amp *= 0.5
    return out / tot

def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return np.array(a, float) * (1 - t) + np.array(b, float) * t

PAL = {  # season: dark leaf, light leaf, turf, turf tip, petal A (white), petal B (yellow), petal C (pink)
    "spring": ((70, 156, 56), (176, 230, 96), (120, 200, 66), (184, 234, 100), (250, 248, 240), (252, 226, 70), (238, 110, 150)),
    "summer": ((52, 134, 48), (150, 216, 84), (104, 180, 58), (170, 224, 92), (248, 246, 236), (250, 214, 60), (236, 92, 128)),
    "autumn": ((176, 90, 36), (248, 190, 70), (170, 156, 64), (224, 196, 96), (230, 130, 50), (200, 70, 40), (244, 200, 80)),
    "winter": ((92, 124, 96), (164, 192, 152), (232, 238, 246), (244, 246, 250), (236, 240, 248), (220, 226, 240), (240, 240, 250)),
}

def paint(name, season):
    dark, light, turf, tip, pa, pb, pc = PAL[season]
    s = SEASONS.index(season)
    if name == "leaf":      # mottled, with sunlit clumps
        n = fbm(1 + s, 6); d = fbm(11 + s, 18)
        img = mix(dark, light, 0.25 + 0.7 * n * (0.6 + 0.4 * d))
        img = mix(img, tip, np.clip((d - 0.62) * 3, 0, 1) * 0.5)
        if season == "winter": img = mix(img, (244, 246, 252), np.clip((n - 0.55) * 3, 0, 1) * 0.6)
    elif name == "bark":    # vertical streaks, darker in the cracks
        n = fbm(21, 3, 4, (4, 1)); c = fbm(22, 10, 3, (3, 1))
        img = mix((70, 48, 38), (128, 90, 62), n * 1.2 - 0.1); img = mix(img, (40, 28, 24), np.clip((c - 0.7) * 4, 0, 1))
    elif name == "rock":    # warm red-brown with leaning bands, pink speckle, dark cracks
        yy = np.linspace(0, 1, N)[:, None]
        bands = 0.5 + 0.5 * np.sin(yy * 6.28 * 3 + fbm(31, 3) * 3)
        n = fbm(32, 8); c = fbm(33, 14)
        img = mix((150, 102, 86), (220, 166, 130), 0.3 * bands + 0.7 * n)
        img = mix(img, (232, 160, 160), np.clip((c - 0.6) * 3, 0, 1) * 0.5)
        img = mix(img, (84, 60, 56), np.clip((fbm(34, 12, 3) - 0.74) * 5, 0, 1))
    elif name == "strata":  # four bands (bottom to top) with ragged edges: dark, pink, tan, gold
        yy = np.linspace(0, 1, N)[:, None] + (fbm(41, 5) - 0.5) * 0.07
        cols = [(138, 98, 88), (222, 166, 166), (190, 138, 112), (236, 186, 146)]
        img = np.zeros((N, N, 3))
        for k, col in enumerate(cols):
            img = np.where(((yy >= k / 4) & (yy < (k + 1) / 4))[..., None] * np.ones((1, 1, 3)) > 0, np.array(col, float), img)
        img = mix(img, (255, 255, 255), (fbm(42, 14) - 0.5) * 0.25)
        img = img * (0.92 + 0.16 * fbm(43, 20)[..., None])
    elif name == "turf":
        n = fbm(51, 10, 4, (6, 1))
        img = mix(turf, tip, n); img = mix(img, (0, 0, 0), np.clip((fbm(52, 16) - 0.7) * 2, 0, 0.35))
    elif name == "crag":    # grey-lilac rock, pink in the light
        n = fbm(61, 6); img = mix((150, 124, 150), (214, 176, 196), n)
        img = mix(img, (110, 92, 118), np.clip((fbm(62, 14) - 0.6) * 3, 0, 1) * 0.6)
    elif name == "snow":
        n = fbm(71, 5); img = mix((246, 238, 250), (255, 214, 232), n * 0.5)
    elif name in ("petalA", "petalB", "petalC"):
        col = {"petalA": pa, "petalB": pb, "petalC": pc}[name]; n = fbm(81, 6)
        img = mix(col, (255, 255, 255), n * 0.2)
    elif name == "stem":
        n = fbm(91, 4, 3, (1, 4)); img = mix((44, 106, 40), (110, 176, 70) if season != "winter" else (170, 180, 150), n)
    elif name == "reed":
        n = fbm(95, 4, 3, (1, 4))
        a, b = ((54, 104, 50), tip) if season != "winter" else ((150, 140, 110), (226, 214, 170))
        img = mix(a, b, n)
    elif name == "cattail":
        n = fbm(97, 8); img = mix((92, 58, 36), (140, 90, 56), n)
    else:
        raise KeyError(name)
    return np.clip(img, 0, 255).astype(np.uint8)

MATERIALS = ["leaf", "bark", "rock", "strata", "turf", "crag", "snow", "petalA", "petalB", "petalC", "stem", "reed", "cattail"]
TILE = {"leaf": 110, "bark": 70, "rock": 170, "strata": 168, "turf": 100, "crag": 200, "snow": 160, "petalA": 20, "petalB": 20, "petalC": 20,
        "stem": 60, "reed": 120, "cattail": 24}
SMOOTH = {"leaf", "rock", "bark", "cattail"}     # shaded round; the rest keep their facets
TEXS = {}

def make_textures():
    os.makedirs(TEX, exist_ok=True)
    for season in SEASONS:
        for m in MATERIALS:
            a = paint(m, season)
            TEXS[(m, season)] = a
            Image.fromarray(a).save(os.path.join(TEX, "%s_%s.png" % (m, season)))

# ---------------------------------------------------------------------------------------------------------------- geometry
MIDX = {m: i for i, m in enumerate(MATERIALS)}

class Piece:
    def __init__(self, name, seed):
        self.name = name; self.bm = bmesh.new(); self.rng = random.Random(seed)
    def face(self, pts, mat):
        vs = [self.bm.verts.new(p) for p in pts]
        try:
            f = self.bm.faces.new(vs)
        except ValueError:
            return
        f.material_index = MIDX[mat]
        return f
    def blob(self, c, r, mat, subdiv=2, noise=0.1, squash_bottom=0.0, seed=0):
        """An icosphere with radii r=(rx, ry, rz), pushed about by smooth noise; bottom flattened to the ground if squash_bottom > 0."""
        rng = random.Random(seed or self.rng.random())
        m = bmesh.new(); bmesh.ops.create_icosphere(m, subdivisions=subdiv, radius=1.0)
        ph = [rng.random() * 6.28 for _ in range(4)]
        for v in m.verts:
            x, y, z = v.co
            k = 1.0 + noise * (math.sin(x * 2.3 + ph[0]) * math.cos(y * 2.1 + ph[1]) + 0.6 * math.sin((x + y - z) * 3.1 + ph[2]))
            v.co = Vector((c[0] + x * r[0] * k, c[1] + y * r[1] * k, c[2] + z * r[2] * k))
        if squash_bottom:   # sits on the ground: the part below z = 0 is cut off flat (c[2] is the height of the centre above the cut)
            lift = r[2] * (1 - squash_bottom) - c[2]
            for v in m.verts:
                v.co.z += lift
                if v.co.z < 0: v.co.z = 0.0
        for f in m.faces:
            self.face([v.co.copy() for v in f.verts], mat)
        m.free()
    def lathe(self, profile, sides, mat, c=(0, 0), jitter=0.0, tilt=(0, 0)):
        """Revolves [(radius, height), ...] about the vertical axis at c; tilt leans the top by (dx, dy) per unit height."""
        rings = []
        for (r, h) in profile:
            ring = []
            for k in range(sides):
                a = 2 * math.pi * k / sides
                rr = r * (1 + (self.rng.random() - 0.5) * jitter)
                ring.append((c[0] + math.cos(a) * rr + tilt[0] * h, c[1] + math.sin(a) * rr + tilt[1] * h, h))
            rings.append(ring)
        for i in range(len(rings) - 1):
            for k in range(sides):
                k2 = (k + 1) % sides
                if profile[i + 1][0] <= 1e-6:
                    self.face([rings[i][k], rings[i][k2], rings[i + 1][k]], mat)
                else:
                    self.face([rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]], mat)
    def blade(self, base, height, ang, lean, w, mat):
        out = (math.cos(ang), math.sin(ang)); side = (-out[1], out[0])
        def at(f, hw):
            bend = lean * f * f
            return (base[0] + out[0] * bend + side[0] * hw, base[1] + out[1] * bend + side[1] * hw, base[2] + height * f)
        self.face([at(0, -w), at(0, w), at(0.5, w * 0.7), at(0.5, -w * 0.7)], mat)
        self.face([at(0.5, -w * 0.7), at(0.5, w * 0.7), at(1, 0)], mat)
    def tuft(self, blades, tall, spread, mat="turf"):
        for i in range(blades):
            a = (i + self.rng.random() * 0.7) / blades * 2 * math.pi; r = spread * self.rng.random()
            self.blade((math.cos(a) * r, math.sin(a) * r, 0), tall * (0.7 + 0.5 * self.rng.random()), a + (self.rng.random() - 0.5), tall * (0.15 + 0.3 * self.rng.random()), 3.0, mat)
    def bloom(self, at, r, mat, petals=5):
        """A flat flower head: a fan of petals round a centre, raised a little at the middle."""
        for k in range(petals):
            a0 = 2 * math.pi * k / petals; a1 = 2 * math.pi * (k + 1) / petals
            self.face([(at[0], at[1], at[2] + r * 0.45), (at[0] + math.cos(a0) * r, at[1] + math.sin(a0) * r, at[2]),
                       (at[0] + math.cos(a1) * r, at[1] + math.sin(a1) * r, at[2])], mat)
    def flower(self, at, r, mat):
        self.blade((at[0], at[1], 0), at[2], 0.0, 0.0, 1.5, "stem"); self.bloom(at, r, mat)

def build_oak():
    p = Piece("oak", 1)
    p.lathe([(26, 0), (20, 22), (17, 40), (12, 100)], 9, "bark", jitter=0.12)
    p.lathe([(7, 70), (4.5, 120)], 6, "bark", c=(26, 0), tilt=(0.0, 0.0))
    p.blob((0, 0, 128), (100, 96, 76), "leaf", 3, 0.12, seed=3)
    p.blob((84, 34, 100), (56, 54, 44), "leaf", 2, 0.12, seed=4)
    p.blob((-76, -30, 104), (60, 56, 46), "leaf", 2, 0.12, seed=5)
    return p

def build_hedge():
    p = Piece("hedge", 2)
    p.blob((0, 0, 0), (36, 32, 30), "leaf", 2, 0.1, 0.35, seed=7)
    p.blob((34, 8, 0), (26, 24, 22), "leaf", 2, 0.1, 0.35, seed=8)
    p.blob((-30, -10, 0), (28, 26, 24), "leaf", 2, 0.1, 0.35, seed=9)
    mats = ["petalA", "petalB", "petalC"]
    for i in range(10):
        a = p.rng.random() * 6.28; r = 6 + 28 * p.rng.random()
        p.bloom((math.cos(a) * r, math.sin(a) * r * 0.9, 22 - r * 0.45 + 6 * p.rng.random()), 4.8, mats[i % 3], 4)
    return p

def build_boulder():
    p = Piece("boulder", 3)
    p.blob((0, 0, 0), (70, 64, 54), "rock", 3, 0.14, 0.3, seed=11)
    p.blob((72, 24, 0), (40, 36, 30), "rock", 2, 0.14, 0.3, seed=12)
    p.blob((-58, 40, 0), (28, 26, 20), "rock", 1, 0.1, 0.3, seed=13)
    p.tuft(6, 20, 80)
    return p

def build_cliff():
    p = Piece("cliff", 4)
    N_, L = 12, 4
    H = [0, 44, 90, 132, 166]; W0, D0 = 150.0, 56.0
    jit = [[0.9 + 0.2 * p.rng.random() for _ in range(N_)] for _ in range(L + 1)]
    def ring(i, k, z, grow=1.0):
        a = 2 * math.pi * k / N_; c, s = math.cos(a), math.sin(a)
        x = W0 * (1 - 0.05 * i) * jit[i][k] * math.copysign(abs(c) ** 0.55, c) * grow
        y = D0 * (1 - 0.1 * i) * jit[i][k] * math.copysign(abs(s) ** 0.55, s) * grow
        return (x, y, z)
    for i in range(L):
        for k in range(N_):
            p.face([ring(i, k, H[i]), ring(i, (k + 1) % N_, H[i]), ring(i + 1, (k + 1) % N_, H[i + 1]), ring(i + 1, k, H[i + 1])], "strata")
    for k in range(N_):   # the grassy lip and the flat top
        k2 = (k + 1) % N_
        p.face([ring(L, k, H[L] - 6, 1.06), ring(L, k2, H[L] - 6, 1.06), ring(L, k2, H[L] + 8), ring(L, k, H[L] + 8)], "turf")
        p.face([ring(L, k, H[L] + 8), ring(L, k2, H[L] + 8), (0, 0, H[L] + 10)], "turf")
    p.tuft(5, 22, 60)
    return p

def build_crag():
    p = Piece("crag", 5)
    for (cx, cy, r0, h) in [(0, 0, 62, 250), (64, 26, 44, 180), (-52, -30, 40, 150)]:
        yy = [0, h * 0.4, h * 0.75, h]; rr = [r0, r0 * 0.66, r0 * 0.36]
        ang = [2 * math.pi * (k + 0.3 * p.rng.random()) / 6 for k in range(6)]
        def at(level, k):
            f = rr[level] * (0.85 + 0.3 * ((k * 7 + level * 3) % 5) / 4.0) if level < 3 else 0.0
            return (cx + math.cos(ang[k % 6]) * f, cy + math.sin(ang[k % 6]) * f, yy[level])
        for k in range(6):
            p.face([at(0, k), at(0, k + 1), at(1, k + 1), at(1, k)], "crag")
            p.face([at(1, k), at(1, k + 1), at(2, k + 1), at(2, k)], "snow")
            p.face([at(2, k), at(2, k + 1), (cx, cy, h)], "snow")
    return p

def build_flowers(kind):
    p = Piece("flowers" + kind, 6 if kind == "A" else 7)
    p.tuft(8, 22, 30)
    if kind == "A":
        mats = ["petalA", "petalB", "petalA"]
        for i in range(11):
            a = p.rng.random() * 6.28; r = 32 * p.rng.random()
            p.flower((math.cos(a) * r, math.sin(a) * r, 14 + 12 * p.rng.random()), 5.5, mats[i % 3])
    else:
        for i in range(7):
            a = p.rng.random() * 6.28; r = 30 * p.rng.random()
            p.flower((math.cos(a) * r, math.sin(a) * r, 14 + 10 * p.rng.random()), 5.5, "petalC" if i % 2 else "petalB")
        for i in range(3):
            a = p.rng.random() * 6.28; r = 8 + 20 * p.rng.random(); x, y = math.cos(a) * r, math.sin(a) * r; h = 58 + 20 * p.rng.random()
            p.blade((x, y, 0), h, a, 4, 1.6, "stem")
            for k in range(3): p.bloom((x + math.cos(a) * 1.5, y + math.sin(a) * 1.5, h * (0.55 + 0.2 * k)), 4.5 - k, "petalC", 4)
    return p

def build_reeds():
    p = Piece("reeds", 8)
    for i in range(10):
        a = p.rng.random() * 6.28; r = 18 * p.rng.random(); h = 60 + 50 * p.rng.random(); x, y = math.cos(a) * r, math.sin(a) * r
        p.blade((x, y, 0), h, a, 8, 2.8, "reed")
        if i % 3 == 0:
            p.lathe([(3.5, h * 0.62), (3.5, h * 0.8), (0.1, h * 0.84)], 5, "cattail", c=(x + math.cos(a) * 3, y + math.sin(a) * 3))
    return p

BUILDERS = [build_oak, build_hedge, build_boulder, build_cliff, build_crag, lambda: build_flowers("A"), lambda: build_flowers("B"), build_reeds]
NAMES = ["oak", "hedge", "boulder", "cliff", "crag", "flowersWhite", "flowersPink", "reeds"]

# ---------------------------------------------------------------------------------------------------------------- UVs, Blender objects
def assign_uvs(bm, mat_names):
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        m = mat_names[f.material_index]; t = TILE[m]; n = f.normal
        for l in f.loops:
            x, y, z = l.vert.co
            if m in ("strata",):                       # bands run once up the cliff, round its sides
                u, v = math.atan2(y, x) / (2 * math.pi) * 3.0, z / t
            elif abs(n.z) > 0.7:
                u, v = x / t, y / t
            else:
                u, v = (x if abs(n.y) > abs(n.x) else y) / t, z / t
            l[uv].uv = (u, v)

def make_blender(pieces):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = []
    for m in MATERIALS:
        mt = bpy.data.materials.new(m); mt.use_nodes = True
        img = bpy.data.images.load(os.path.abspath(os.path.join(TEX, "%s_summer.png" % m)))
        img.pack()
        tex = mt.node_tree.nodes.new("ShaderNodeTexImage"); tex.image = img; tex.interpolation = "Closest"
        bsdf = mt.node_tree.nodes["Principled BSDF"]
        mt.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.9
        mats.append(mt)
    for i, p in enumerate(pieces):
        bmesh.ops.recalc_face_normals(p.bm, faces=p.bm.faces)
        assign_uvs(p.bm, MATERIALS)
        me = bpy.data.meshes.new(NAMES[i]); p.bm.to_mesh(me)
        for mt in mats: me.materials.append(mt)
        ob = bpy.data.objects.new(NAMES[i], me); ob.location = (i * 400.0, 0, 0)
        bpy.context.scene.collection.objects.link(ob)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ASSETS, "scenery.blend"))

# ---------------------------------------------------------------------------------------------------------------- export
SUN = np.array([-0.45, 0.8, 0.4]); SUN /= np.linalg.norm(SUN)

def sample(m, season, uv):
    a = TEXS[(m, season)]; u = (uv[0] % 1.0) * N - 0.5; v = (uv[1] % 1.0) * N - 0.5
    x0 = int(math.floor(u)); y0 = int(math.floor(v)); fx = u - x0; fy = v - y0
    g = lambda x, y: a[y % N, x % N].astype(float)
    return (g(x0, y0) * (1 - fx) + g(x0 + 1, y0) * fx) * (1 - fy) + (g(x0, y0 + 1) * (1 - fx) + g(x0 + 1, y0 + 1) * fx) * fy

def corners(piece):
    """Triangulated corners: (position in game axes, normal, material, uv)."""
    bm = piece.bm
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    assign_uvs(bm, MATERIALS)
    uvl = bm.loops.layers.uv.active
    acc = {}
    for f in bm.faces:
        mname = MATERIALS[f.material_index]
        if mname in SMOOTH:
            area = f.calc_area()
            for v in f.verts:
                key = (mname, tuple(round(c, 2) for c in v.co))
                acc[key] = acc.get(key, Vector((0, 0, 0))) + f.normal * area
    out = []
    for f in bm.faces:
        mname = MATERIALS[f.material_index]
        for l in f.loops:
            v = l.vert
            n = acc[(mname, tuple(round(c, 2) for c in v.co))].normalized() if mname in SMOOTH else f.normal.copy()
            out.append(((v.co.x, v.co.z, -v.co.y), (n.x, n.z, -n.y), mname, tuple(l[uvl].uv)))
    return out

def export(pieces):
    items = []
    for p in pieces:
        c = corners(p)
        z0 = min(q[0][1] for q in c)
        items.append((c, z0))
    lines = ["// GENERATED by tools/scenery/build_scenery.py from assets/scenery/scenery.blend. Do not edit by hand: change the Blender script and run it again.",
             "// The Fortnite Map's Hyrule Field scenery, built in Blender with painted textures (assets/scenery/tex). Every triangle corner keeps its position and, for each",
             "// season, the colour its texture has there, lit with the game's own warm key and cool fill. Item order: oak, hedge, boulder, cliff, crag, white flowers,",
             "// pink flowers, reeds (royale::MeshKind::Scenery, variant = item + 8 * season).",
             "#pragma once", "#include <cstdint>", "", "namespace royale {", "namespace scenery_model {", ""]
    counts = []; pos = []; cols = [[] for _ in SEASONS]
    for c, z0 in items:
        counts.append(len(c))
        for (pp, nn, mname, uv) in c:
            pos.append((round(pp[0] * 4), round(max(0.0, pp[1]) * 4), round(pp[2] * 4)))   # quarter units
        for si, season in enumerate(SEASONS):
            for (pp, nn, mname, uv) in c:
                n = np.array(nn); lit = max(0.0, float(n @ SUN)); shade = 0.54 + 0.46 * lit
                tint = np.array([0.84 + 0.22 * lit, 0.88 + 0.13 * lit, 1.04 - 0.12 * lit])
                ao = 0.8 + 0.2 * min(1.0, max(0.0, pp[1]) / 60.0)                         # darker down in the grass
                col = sample(mname, season, uv) * shade * tint * ao * 1.28
                cols[si].append(tuple(int(min(255, max(0, round(x)))) for x in col))
    lines.append("inline constexpr int kItems = %d;" % len(items))
    lines.append("// Corners (three per triangle) in each item, in order.")
    lines.append("inline constexpr int kCorners[kItems] = {%s};" % ", ".join(str(c) for c in counts))
    lines.append("inline constexpr int kTotalCorners = %d;" % sum(counts))
    lines.append("// Position of every corner in quarter game units (x, y up, z), item after item.")
    lines.append("inline constexpr int16_t kPos[kTotalCorners * 3] = {")
    flat = [v for p in pos for v in p]
    for i in range(0, len(flat), 24): lines.append("    " + ",".join(str(v) for v in flat[i:i + 24]) + ",")
    lines.append("};")
    for si, season in enumerate(SEASONS):
        lines.append("// Colour (r, g, b) of every corner in %s." % season)
        lines.append("inline constexpr uint8_t kCol%s[kTotalCorners * 3] = {" % season.capitalize())
        flat = [v for c in cols[si] for v in c]
        for i in range(0, len(flat), 36): lines.append("    " + ",".join(str(v) for v in flat[i:i + 36]) + ",")
        lines.append("};")
    lines += ["", "} // namespace scenery_model", "} // namespace royale", ""]
    open(OUT_H, "w").write("\n".join(lines))
    print("wrote", OUT_H, os.path.getsize(OUT_H) // 1024, "KB;", ", ".join("%s %d tris" % (n, c // 3) for n, c in zip(NAMES, counts)))

def main():
    make_textures()
    pieces = [b() for b in BUILDERS]
    make_blender(pieces)
    export(pieces)

if __name__ == "__main__":
    main()
