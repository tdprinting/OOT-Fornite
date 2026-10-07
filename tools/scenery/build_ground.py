#!/usr/bin/env python3
"""Builds the ground patches (puddles, snow piles and drifts, frost, fallen leaves, blossom petals) in Blender and exports them for the mod.

  pip install bpy numpy pillow        (Blender as a Python module)
  python3 tools/scenery/build_ground.py [preview.png]

Every patch is modelled about 100 units from the middle to its edge, lying on y = 0, in the Ocarina of Time field art's palette: clear teal-blue water
with a sky-coloured middle and a four-pointed glint, wet earth and pebbles round it, ice with cracks, snow lit warm on top with cool blue shadows,
pale rime, red and gold leaves, pink and white petals. The game scales a patch to the size it wants (shared/ground_patches.h), so one shape
serves a small patch and, stretched, a patch that grew out of several that ran together.

The puddles, banks and snow are smooth polar-grid surfaces (about 30 corners round, six rings), lit per vertex from a smooth normal so nothing is faceted.
Puddles fade out at their rim with a vertex alpha (the game multiplies it by its own fade), so the edge is soft and the wet ground round the water
blends into the grass. Each puddle has a mud bank (a low lip round the water, opaque) so the water sits in a slight dip; snow piles and drifts fall
away to nothing at their edge with no step. The game also needs about 6-unit triangles to press footprints into the snow, which is why the snow is
a grid of that size.

It saves assets/scenery/ground.blend (open it in Blender to look at or change the pieces, the colours are in a colour attribute) and exports
shared/ground_model.h: every triangle corner with its position and baked colour. The game draws with vertex colours, so the light is baked in
here, the way the game's own models are lit (warm key light, cool fill). Blender is Z up, the game is Y up: (x, y, z) becomes (x, z, -y).
"""
import math, os, random, sys
import bpy, bmesh
from mathutils import Vector

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ASSETS = os.path.join(ROOT, "assets", "scenery")
OUT_H = os.path.join(ROOT, "shared", "ground_model.h")
TAU = 2 * math.pi

# ------------------------------------------------------------------------------------------------------------------------- colours
def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] * (1 - t) + b[i] * t for i in range(3))

def mul(c, k):
    return tuple(min(255.0, v * k) for v in c)

SUN = Vector((-0.45, 0.4, 0.8)).normalized()   # in Blender axes (the game's sun, (-0.45, 0.8, 0.4), with y and z swapped)

def lit(col, n, amount=1.0):
    """The game's warm key light and cool fill, as a multiplier on a colour (n = unit normal, Z up)."""
    k = max(0.0, n.dot(SUN))
    shade = 1.0 - amount * 0.46 * (1.0 - k)
    tint = (1.0 + amount * 0.10 * k - amount * 0.04, 1.0 + amount * 0.05 * k - amount * 0.02, 1.0 - amount * 0.10 * k + amount * 0.05)
    return tuple(min(255.0, col[i] * shade * tint[i]) for i in range(3))

# ------------------------------------------------------------------------------------------------------------------------- pieces
class Piece:
    """A bag of triangles with per-corner colours. `shade` (0 flat colours, 1 fully lit) says how much of the sun to bake in."""
    def __init__(self, name, seed, xlu=False, shade=1.0):
        self.name, self.rng, self.xlu, self.shade = name, random.Random(seed), xlu, shade
        self.tris = []
    def tri(self, a, b, c, ca, cb=None, cc=None, up=True, al=(255, 255, 255)):
        a, b, c = Vector(a), Vector(b), Vector(c)
        n = (b - a).cross(c - a)
        if n.length < 1e-6:
            return
        n.normalize()
        if up and n.z < 0:   # every patch faces up
            b, c = c, b
            cb, cc = (cc, cb) if cb is not None else (None, None)
            al = (al[0], al[2], al[1])
            n = -n
        cb = cb or ca; cc = cc or ca
        f = lambda col: lit(col, n, self.shade) if self.shade > 0 else col
        self.tris.append((a, b, c, f(ca), f(cb), f(cc), al))
    def quad(self, a, b, c, d, ca, cb=None, cc=None, cd=None):
        cb = cb or ca; cc = cc or ca; cd = cd or ca
        self.tri(a, b, c, ca, cb, cc); self.tri(a, c, d, ca, cc, cd)

def outline(rng, n, aspect=1.0, lobes=3, amp=0.22, base=1.0):
    """A soft, irregular blob: a few low waves round a circle, so no two patches look the same. Returns n points for a unit radius."""
    ph = [rng.random() * TAU for _ in range(lobes)]
    out = []
    for i in range(n):
        a = TAU * i / n
        r = base * (1 + sum(amp / (k + 1) * math.sin((k + 2) * a + ph[k]) for k in range(lobes)) + rng.uniform(-0.035, 0.035))
        out.append((math.cos(a) * r, math.sin(a) * r * aspect))
    return out

def ring_of(pts, scale, z, off=(0, 0)):
    return [(p[0] * scale * 100.0 + off[0], p[1] * scale * 100.0 + off[1], z) for p in pts]

def fan(piece, rings, cols, centre):
    """Joins rings (lists of corners, outer first) and a centre point with triangles, colouring each ring with cols[i]."""
    n = len(rings[0])
    for r in range(len(rings) - 1):
        for i in range(n):
            k = (i + 1) % n
            piece.quad(rings[r][i], rings[r][k], rings[r + 1][k], rings[r + 1][i], cols[r], cols[r], cols[r + 1], cols[r + 1])
    last = rings[-1]
    for i in range(n):
        piece.tri(last[i], last[(i + 1) % n], centre[0], cols[-1], cols[-1], centre[1])

def wave_outline(rng, lobes=3, amp=0.22):
    """A smooth irregular outline: radius as a function of angle (a few low waves, no per-point noise), so no two patches look alike."""
    ph = [rng.random() * TAU for _ in range(lobes)]
    return lambda a: 1.0 + sum(amp / (k + 1) * math.sin((k + 2) * a + ph[k]) for k in range(lobes))

def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def polar_surface(p, nseg, rads, pos, colour, alpha=None, light=True):
    """A smooth surface on a polar grid. `rads` are the ring radii (fractions, outermost first, the last 0 = the middle point), `pos(q, a)` gives the
    corner (x, y, z) at fraction q and angle a, `colour(q, a, n)` its colour (n: the smooth unit normal, Z up) and `alpha(q, a)` its opacity.
    Normals are averaged over the faces that meet at each grid point, so the baked light is smooth across the whole surface."""
    R = len(rads)
    P = {}
    def key(r, i): return (r, 0) if r == R - 1 else (r, i % nseg)
    for r in range(R):
        for i in range(nseg):
            P[key(r, i)] = Vector(pos(rads[r], TAU * i / nseg))
    faces = []
    for r in range(R - 2):
        for i in range(nseg):
            a, b, c, d = key(r, i), key(r, i + 1), key(r + 1, i + 1), key(r + 1, i)
            faces += [(a, b, c), (a, c, d)]
    for i in range(nseg):
        faces.append((key(R - 2, i), key(R - 2, i + 1), key(R - 1, i)))
    N = {k: Vector((0, 0, 0)) for k in P}
    for (a, b, c) in faces:
        n = (P[b] - P[a]).cross(P[c] - P[a])
        if n.z < 0: n = -n
        for k in (a, b, c): N[k] += n
    for k in N:
        N[k] = N[k].normalized() if N[k].length > 1e-9 else Vector((0, 0, 1))
    def corner(k):
        r, i = k
        a = TAU * i / nseg
        col = colour(rads[r], a, N[k])
        if light and p.shade > 0: col = lit(col, N[k], p.shade)
        return P[k], col, (alpha(rads[r], a) if alpha else 255)
    for (a, b, c) in faces:
        (pa, ca, aa), (pb, cb, ab), (pc, cc, ac) = corner(a), corner(b), corner(c)
        n = (pb - pa).cross(pc - pa)
        if n.length < 1e-6: continue
        if n.z < 0: pb, pc, cb, cc, ab, ac = pc, pb, cc, cb, ac, ab
        p.tris.append((pa, pb, pc, ca, cb, cc, (aa, ab, ac)))

# ---- puddles ----
WATER = dict(wet=(98, 84, 66), edge=(58, 104, 142), inner=(88, 150, 204), centre=(150, 202, 236))
ICE = dict(wet=(206, 214, 226), edge=(174, 202, 224), inner=(214, 232, 246), centre=(244, 250, 255))

def build_puddle(shape, frozen):
    """Water in a shallow dip: a wet-earth halo that fades into the grass, a darker bank line, then water that brightens to a sky-coloured middle.
    Every ring is a smooth blend (vertex colour and alpha) over a 28-corner outline, so there are no straight edges."""
    pal = ICE if frozen else WATER
    p = Piece("puddle%d%s" % (shape, "Ice" if frozen else ""), 1200 + shape, xlu=True, shade=0.0)
    rng = p.rng
    aspect = [0.9, 0.74, 0.62, 0.82][shape]
    wob = wave_outline(rng, 3, [0.15, 0.2, 0.13, 0.22][shape])
    def xy(q, a, z): r = wob(a) * q * 100.0; return (math.cos(a) * r, math.sin(a) * r * aspect, z)
    rads = [1.3, 1.16, 1.0, 0.86, 0.62, 0.32, 0.0]
    zs = {1.3: 0.15, 1.16: 0.3, 1.0: 0.5, 0.86: 0.35, 0.62: 0.3, 0.32: 0.3, 0.0: 0.3}
    def skyk(x, y): return 0.88 + 0.2 * max(-1.0, min(1.0, (-x + y) / 130.0))
    def col(q, a, n):
        x, y, _ = xy(q, a, 0)
        k = skyk(x, y)
        if q >= 1.16: return pal["wet"]
        if q >= 1.0: return mix(pal["wet"], mul(pal["edge"], 0.9), (1.16 - q) / 0.16 * 0.85)
        if q >= 0.86: return mul(mix(mul(pal["edge"], 0.9), pal["edge"], (1.0 - q) / 0.14), k)
        if q >= 0.62: return mul(mix(pal["edge"], pal["inner"], (0.86 - q) / 0.24), k)
        return mul(mix(pal["inner"], pal["centre"], smooth((0.62 - q) / 0.62)), k)
    def alp(q, a):
        if q >= 1.3: return 0
        if q >= 1.16: return int(150 * (1.3 - q) / 0.14)
        if q >= 1.0: return int(150 + 85 * (1.16 - q) / 0.16)
        return 255 if q < 0.9 else int(235 + 20 * (1.0 - q) / 0.1)
    polar_surface(p, 28, rads, lambda q, a: xy(q, a, zs[min(zs, key=lambda k: abs(k - q))]), col, alp, light=False)
    if frozen:   # cracks across the ice, and a pale sheen
        for c in range(3):
            a = rng.random() * TAU
            p0 = (math.cos(a) * 9, math.sin(a) * 9, 1.0); p1 = (math.cos(a + 0.4) * 62, math.sin(a + 0.4) * 62, 1.0)
            p.tri(p0, (p1[0] + 1.8, p1[1], 1.0), (p1[0] - 1.8, p1[1] + 1.8, 1.0), (120, 150, 180))
    else:        # a four-pointed glint of sun on the water, and a thin streak of sky
        gx, gy = -22.0 + rng.uniform(-8, 8), 14.0 + rng.uniform(-6, 6)
        for a0 in (0.0, TAU / 4):
            c, s = math.cos(a0), math.sin(a0)
            p.tri((gx + c * 15, gy + s * 15, 1.0), (gx - s * 2.2, gy + c * 2.2, 1.0), (gx + s * 2.2, gy - c * 2.2, 1.0), (255, 252, 226))
            p.tri((gx - c * 15, gy - s * 15, 1.0), (gx + s * 2.2, gy - c * 2.2, 1.0), (gx - s * 2.2, gy + c * 2.2, 1.0), (255, 252, 226))
        p.quad((20, -24, 1.0), (46, -14, 1.0), (44, -10, 1.0), (18, -20, 1.0), (206, 232, 250))
    # pebbles on the wet earth, and on two of the shapes a few reed blades on the bank
    for i in range(4):
        a = rng.random() * TAU; r = 1.1 + 0.12 * rng.random()
        x, y, _ = xy(r, a, 0); s = 4 + 3 * rng.random(); colr = (150, 138, 128) if not frozen else (176, 182, 196)
        p.tri((x - s, y - s * 0.7, 0.4), (x + s, y - s * 0.5, 0.4), (x, y + s, s * 0.9), mul(colr, 0.8))
        p.tri((x + s, y - s * 0.5, 0.4), (x + s * 0.4, y + s, 0.4), (x, y + s, s * 0.9), colr)
    if shape in (1, 3):
        for i in range(5):
            a = rng.random() * TAU; x, y, _ = xy(1.04, a, 0)
            h = 16 + 10 * rng.random(); lean = (math.cos(a) * 5, math.sin(a) * 5)
            g = (90, 150, 70) if not frozen else (190, 200, 180)
            p.tri((x - 1.6, y, 0.6), (x + 1.6, y, 0.6), (x + lean[0], y + lean[1], h), g, g, mul(g, 1.3))
    return p

BANK_H = 3.4   # how high the mud lip stands (the game scales it by its deformation option)

def build_bank(shape):
    """The lip of wet mud round a puddle: a low smooth ridge (opaque) that falls away to the grass outside and into the water inside, so the water
    sits in a slight dip. Same outline as the puddle of the same shape (the outline's random waves use the same seed)."""
    p = Piece("bank%d" % shape, 1200 + shape, shade=0.9)
    rng = p.rng
    aspect = [0.9, 0.74, 0.62, 0.82][shape]
    wob = wave_outline(rng, 3, [0.15, 0.2, 0.13, 0.22][shape])
    def hgt(q): return BANK_H * math.exp(-(((q - 1.0) / 0.11) ** 2))
    def xy(q, a): r = wob(a) * q * 100.0; return (math.cos(a) * r, math.sin(a) * r * aspect, hgt(q))
    rads = [1.34, 1.2, 1.1, 1.0, 0.9, 0.78]
    def col(q, a, n):
        up = max(0.0, min(1.0, hgt(q) / BANK_H))
        base = mix((88, 74, 56), (132, 112, 86), up)
        return mix(base, (62, 82, 64), smooth((q - 1.12) / 0.2) * 0.6)   # the outer foot takes a little of the grass's green
    # the inner edge is a ring with no middle, so close it with a fan to a point just under the water
    polar_surface(p, 24, rads + [0.0], lambda q, a: xy(q, a) if q > 0 else (0.0, 0.0, -2.0), col)
    return p

# ---- snow ----
SNOW_EDGE, SNOW_MID, SNOW_TOP = (206, 220, 242), (238, 244, 252), (253, 253, 255)

def snow_colour(hf, n, rim):
    """Snow colour from how high on the heap it is (0 at the foot, 1 at the top): cool at the foot, white at the top. `rim` blends the foot toward slush."""
    c = mix(SNOW_EDGE, SNOW_MID, smooth(hf * 2.5))
    c = mix(c, SNOW_TOP, smooth((hf - 0.35) / 0.65))
    return mix(c, (186, 200, 214), rim * 0.55)

def build_snow_pile(v):
    """A heap that falls away to nothing at its edge with no step (height ~ (1 - q^2)^1.5), a few soft lumps on it, 30 corners round and six rings
    so it can be pressed into footprints."""
    p = Piece("snowPile%d" % v, 800 + v, shade=0.8)
    rng = p.rng
    H = [34, 30, 36, 32][v]
    wob = wave_outline(rng, 3, 0.16)
    asp = [1.0, 0.92, 1.04, 0.88][v]
    lumps = [(rng.uniform(-0.45, 0.45), rng.uniform(-0.45, 0.45), rng.uniform(0.25, 0.4), rng.uniform(0.12, 0.28)) for _ in range(2 + v % 2)]
    def pos(q, a):
        r = wob(a) * q
        x, y = math.cos(a) * r, math.sin(a) * r * asp
        prof = (max(0.0, 1.0 - q * q)) ** 1.5
        z = H * prof * (1.0 + sum(g * math.exp(-((x - lx) ** 2 + (y - ly) ** 2) / (s * s)) for (lx, ly, s, g) in lumps))
        return (x * 100.0, y * 100.0, z)
    rads = [1.0, 0.88, 0.74, 0.58, 0.4, 0.2, 0.0]
    polar_surface(p, 30, rads, pos, lambda q, a, n: snow_colour(max(0.0, 1.0 - q), n, smooth((q - 0.8) / 0.2)))
    return p

def build_snow_drift(v):
    """A broad, low drift swept into a ridge (steeper on the side the wind came from), falling away to nothing at the edge. Stretched, it makes the
    big merged drifts."""
    p = Piece("snowDrift%d" % v, 900 + v, shade=0.8)
    rng = p.rng
    H = 12.5
    wob = wave_outline(rng, 3, 0.18)
    asp = [0.55, 0.62, 0.5, 0.7][v]
    def pos(q, a):
        r = wob(a) * q
        x, y = math.cos(a) * r, math.sin(a) * r * asp
        prof = (max(0.0, 1.0 - q * q)) ** 1.5
        crest = 1.0 + 0.16 * math.sin(x * 5.0 + v) + (0.2 * y if y > 0 else 0.0) / asp * 0.5
        return (x * 100.0 + (6.0 * prof if y > 0 else 0.0), y * 100.0, H * prof * crest)
    rads = [1.0, 0.88, 0.74, 0.58, 0.4, 0.2, 0.0]
    polar_surface(p, 32, rads, pos, lambda q, a, n: snow_colour(max(0.0, 1.0 - q), n, smooth((q - 0.8) / 0.2)))
    return p

# ---- frost ----
def build_frost(v):
    """A patch of rime on the ground: a ragged crystalline star with fern-like tines, pale blue at the tips and white in the middle."""
    p = Piece("frost%d" % v, 1600 + v, xlu=True, shade=0.0)
    rng = p.rng
    spikes = 11 + 2 * v
    outer = []
    for i in range(spikes * 2):
        a = TAU * i / (spikes * 2)
        r = (1.0 if i % 2 == 0 else 0.52) * (0.8 + 0.4 * rng.random())
        outer.append((math.cos(a) * r * 100, math.sin(a) * r * 100 * (0.8 + 0.1 * v), 0.5))
    m = len(outer)
    tip, body, core = (176, 214, 238), (214, 234, 248), (246, 252, 255)
    for i in range(m):
        k = (i + 1) % m
        p.tri(outer[i], outer[k], (0, 0, 0.6), tip if i % 2 == 0 else body, tip if k % 2 == 0 else body, core)
        o = outer[i]
        if i % 2 == 0:   # a tine pointing outward past the tip, with a pair of barbs
            ln = math.hypot(o[0], o[1]); ux, uy = o[0] / ln, o[1] / ln
            p.tri((o[0] - uy * 5, o[1] + ux * 5, 0.5), (o[0] + uy * 5, o[1] - ux * 5, 0.5), (o[0] + ux * 22, o[1] + uy * 22, 0.5), tip, tip, (232, 244, 252))
            bx, by = o[0] + ux * 11, o[1] + uy * 11
            for sgn in (-1, 1):
                p.tri((bx, by, 0.5), (bx + ux * 7 + sgn * uy * 11, by + uy * 7 - sgn * ux * 11, 0.5), (bx + ux * 2 - sgn * uy * 2, by + uy * 2 + sgn * ux * 2, 0.5), tip, (232, 244, 252), tip)
    return p

# ---- the seasons' litter ----
LEAF_PALS = [[(206, 66, 40), (226, 120, 44), (170, 50, 36), (236, 150, 60)],        # reds and oranges
             [(236, 196, 70), (214, 160, 50), (248, 220, 110), (196, 140, 44)],     # gold
             [(206, 66, 40), (236, 196, 70), (140, 96, 52), (120, 150, 60)]]        # a mix with brown and green

def build_leaves(v):
    """A heap of fallen leaves: forty-odd small leaves, thick and a little raised in the middle and scattered thin at the edge."""
    p = Piece("leaves%d" % v, 500 + v, shade=0.7)
    rng = p.rng
    pal = LEAF_PALS[v]
    for i in range(46):
        a = rng.random() * TAU; r = 92 * (rng.random() ** 0.7)
        x, y = math.cos(a) * r, math.sin(a) * r * 0.9
        z = 1.0 + 11.0 * max(0.0, 1.0 - r / 95.0) ** 1.4 + rng.random() * 3.0
        yaw = rng.random() * TAU; ln = 9 + 5 * rng.random(); w = 4.5 + 2.5 * rng.random()
        c, s = math.cos(yaw), math.sin(yaw)
        tilt = (rng.random() - 0.5) * 5.0
        col = pal[rng.randrange(len(pal))]
        a0 = (x + c * ln, y + s * ln, z + tilt); a1 = (x - s * w, y + c * w, z); a2 = (x - c * ln * 0.6, y - s * ln * 0.6, z - tilt * 0.5); a3 = (x + s * w, y - c * w, z)
        p.tri(a0, a1, a3, mul(col, 1.05)); p.tri(a1, a2, a3, mul(col, 0.88))
    # a few dark, damp leaves underneath hold the heap together and keep the grass from showing through
    base = outline(rng, 10, aspect=0.9, amp=0.15)
    for i in range(10):
        k = (i + 1) % 10
        p.tri((base[i][0] * 56, base[i][1] * 56, 0.4), (base[k][0] * 56, base[k][1] * 56, 0.4), (0, 0, 2.0), (92, 76, 44), (92, 76, 44), (116, 94, 52))
    return p

PETAL_PALS = [[(250, 196, 214), (244, 160, 190), (255, 240, 244), (236, 128, 168)],   # blossom
              [(250, 248, 240), (252, 226, 70), (255, 255, 255), (238, 232, 210)]]      # white and yellow

def build_petals(v):
    """Blossom blown onto the ground in spring: fifty tiny petals thrown in a loose drift, mostly flat, a few leaning on the grass."""
    p = Piece("petals%d" % v, 2100 + v, shade=0.5)
    rng = p.rng
    pal = PETAL_PALS[v]
    for i in range(54):
        a = rng.random() * TAU; r = 98 * (rng.random() ** 0.6)
        x, y = math.cos(a) * r, math.sin(a) * r * 0.85
        z = 0.8 + rng.random() * 2.0
        yaw = rng.random() * TAU; ln = 5 + 3 * rng.random(); w = 3 + 1.5 * rng.random()
        c, s = math.cos(yaw), math.sin(yaw)
        col = pal[rng.randrange(len(pal))]
        p.tri((x + c * ln, y + s * ln, z), (x - s * w, y + c * w, z + 0.4), (x + s * w, y - c * w, z + 0.4), col)
        p.tri((x - c * ln * 0.7, y - s * ln * 0.7, z), (x + s * w, y - c * w, z + 0.4), (x - s * w, y + c * w, z + 0.4), mul(col, 0.9))
    return p

# The order of these is the variant number the game asks for (shared/meshes.h, MeshKind::Ground, and shared/ground_patches.h).
BUILDERS = ([lambda s=s: build_puddle(s, False) for s in range(4)] + [lambda s=s: build_puddle(s, True) for s in range(4)] +
            [lambda v=v: build_snow_pile(v) for v in range(4)] + [lambda v=v: build_snow_drift(v) for v in range(4)] +
            [lambda v=v: build_frost(v) for v in range(2)] + [lambda v=v: build_leaves(v) for v in range(3)] + [lambda v=v: build_petals(v) for v in range(2)] +
            [lambda s=s: build_bank(s) for s in range(4)])

# ------------------------------------------------------------------------------------------------------------------------- Blender
def make_blender(pieces):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mt = bpy.data.materials.new("ground"); mt.use_nodes = True
    attr = mt.node_tree.nodes.new("ShaderNodeVertexColor"); attr.layer_name = "Col"
    bsdf = mt.node_tree.nodes["Principled BSDF"]
    mt.node_tree.links.new(attr.outputs["Color"], bsdf.inputs["Base Color"]); bsdf.inputs["Roughness"].default_value = 1.0
    cols = 8
    for i, pc in enumerate(pieces):
        bm = bmesh.new()
        layer = bm.loops.layers.color.new("Col")
        for (a, b, c, ca, cb, cc, al) in pc.tris:
            try:
                f = bm.faces.new([bm.verts.new(a), bm.verts.new(b), bm.verts.new(c)])
            except ValueError:
                continue
            for l, col, alp in zip(f.loops, (ca, cb, cc), al):
                l[layer] = tuple((v / 255.0) ** 2.2 for v in col) + (alp / 255.0,)
        me = bpy.data.meshes.new(pc.name); bm.to_mesh(me); bm.free()
        me.materials.append(mt)
        ob = bpy.data.objects.new(pc.name, me); ob.location = ((i % cols) * 300.0, -(i // cols) * 300.0, 0)
        bpy.context.scene.collection.objects.link(ob)
    os.makedirs(ASSETS, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ASSETS, "ground.blend"))


def render_preview(pieces, path):
    """A quick picture of every patch on a patch of grass (painter's algorithm, seen from above at an angle), so the shapes can be judged
    without opening Blender. Water and ice are drawn about 85% opaque, as in the game."""
    from PIL import Image, ImageDraw
    W, H, cols = 2000, 900, 8
    img = Image.new("RGB", (W, H), (96, 150, 70)); d = ImageDraw.Draw(img, "RGBA")
    S, tilt = 0.85, math.radians(52)
    def proj(v, ox, oy):
        x, y, z = v.x, v.y, v.z
        return (ox + x * S, oy - (y * math.sin(tilt) + z * math.cos(tilt) * 1.0) * S)
    for i, pc in enumerate(pieces):
        ox, oy = 140 + (i % cols) * 245, 190 + (i // cols) * 280
        order = sorted(pc.tris, key=lambda t: (t[0].z + t[1].z + t[2].z) / 3 * 4 + (t[0].y + t[1].y + t[2].y) / 3 * 0.8)
        for (a, b, c, ca, cb, cc, al) in order:
            col = tuple(int(max(0, min(255, (ca[k] + cb[k] + cc[k]) / 3))) for k in range(3))
            op = sum(al) / 3.0 / 255.0
            d.polygon([proj(a, ox, oy), proj(b, ox, oy), proj(c, ox, oy)], fill=col + (int((215 if pc.xlu else 255) * op),))
        d.text((ox - 60, oy + 105), pc.name, fill=(255, 255, 255, 255))
    img.save(path)

# ------------------------------------------------------------------------------------------------------------------------- export
def export(pieces):
    counts, pos, col, alph = [], [], [], []
    for pc in pieces:
        n = 0
        for (a, b, c, ca, cb, cc, al) in pc.tris:
            for v, k, aa in ((a, ca, al[0]), (b, cb, al[1]), (c, cc, al[2])):
                alph.append(int(max(0, min(255, aa))))
                pos.append((round(v.x * 4), round(max(0.0, v.z) * 4), round(-v.y * 4)))   # quarter units, game axes
                col.append(tuple(int(max(0, min(255, round(x)))) for x in k))
            n += 3
        counts.append(n)
    xlu = [1 if pc.xlu else 0 for pc in pieces]
    L = ["// GENERATED by tools/scenery/build_ground.py from assets/scenery/ground.blend. Do not edit by hand: change the Blender script and run it again.",
         "// The ground patches (royale::MeshKind::Ground), each about 100 units from its middle to its edge, lying on y = 0, with the light baked into the corner colours.",
         "// Variant: 0-3 puddle shapes, 4-7 the same frozen, 8-11 snow piles, 12-15 snow drifts, 16-17 frost, 18-20 fallen leaves (red, gold, mixed), 21-22 blossom, 23-26 the mud bank round each puddle shape.",
         "#pragma once", "#include <cstdint>", "", "namespace royale {", "namespace ground_model {", "",
         "inline constexpr int kItems = %d;" % len(pieces),
         "// Corners (three per triangle) in each item, in order.",
         "inline constexpr int kCorners[kItems] = {%s};" % ", ".join(map(str, counts)),
         "// 1 when the item is drawn see-through (water, ice, frost), 0 when it is solid.",
         "inline constexpr uint8_t kSeeThrough[kItems] = {%s};" % ", ".join(map(str, xlu)),
         "inline constexpr int kTotalCorners = %d;" % sum(counts),
         "// Position of every corner in quarter game units (x, y up, z), item after item.",
         "inline constexpr int16_t kPos[kTotalCorners * 3] = {"]
    flat = [v for p in pos for v in p]
    for i in range(0, len(flat), 24): L.append("    " + ",".join(map(str, flat[i:i + 24])) + ",")
    L += ["};", "// Colour (r, g, b) of every corner.", "inline constexpr uint8_t kCol[kTotalCorners * 3] = {"]
    flat = [v for c in col for v in c]
    for i in range(0, len(flat), 36): L.append("    " + ",".join(map(str, flat[i:i + 36])) + ",")
    L += ["};", "// Opacity (0-255) of every corner: puddles fade out at their rim; everything else is 255.", "inline constexpr uint8_t kAlpha[kTotalCorners] = {"]
    for i in range(0, len(alph), 48): L.append("    " + ",".join(map(str, alph[i:i + 48])) + ",")
    L += ["};", "", "} // namespace ground_model", "} // namespace royale", ""]
    open(OUT_H, "w").write("\n".join(L))
    print("wrote", OUT_H, os.path.getsize(OUT_H) // 1024, "KB;", ", ".join("%s %d" % (p.name, c // 3) for p, c in zip(pieces, counts)))

def main():
    pieces = [b() for b in BUILDERS]
    make_blender(pieces)
    if len(sys.argv) > 1: render_preview(pieces, sys.argv[1])
    export(pieces)

if __name__ == "__main__":
    main()
