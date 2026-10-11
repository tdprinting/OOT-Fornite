"""Build the four hireable allies (Kokiri, Zora, Goron, Gerudo): original stylised models in a blend of Ocarina of Time, Majora's Mask and
Fortnite, one shared 18-bone rig and eleven animation clips each.
Run: python tools/allies/build_allies.py [kind ...]   (needs `pip install bpy==4.2.0`; writes assets/allies/<kind>.blend and textures)

No ROM, downloaded model or texture is used. The looks follow the art notes (art-style/art-and-artifacts-notes.md) and the reference pictures in
art-style/npc-refs: the Kokiri from the OoT artwork (big eyes, pointed hat, ragged tunic), the golden remake Goron with his white mane, rocky
shoulder and iron bands, the pale Ruto-like Zora with a domed head-tail, and a Nabooru-style Gerudo (long red ponytail, gold jewels, pink gloves).
Bodies are smooth one-piece lofts with blended joints, not stacked balls; a 256 x 256 painted atlas plus a 128 x 64 face (tools/allies/paint_allies.py).
Blender: Z up, the character faces -Y (the exporter turns that into the game's +Z). Its own left is -X.
"""
from pathlib import Path
import math
import random
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import paint_allies as PA  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/allies'
KINDS = ['kokiri', 'zora', 'goron', 'gerudo']
ATLAS = PA.ATLAS
FACES = ['normal', 'blink', 'fierce']
# name, seconds, loops, fps
CLIPS = [('idle', 3.2, True, 20), ('walk', .9, True, 30), ('invite', 1.6, True, 24), ('attack', .9, False, 30),
         ('attack2', .9, False, 30), ('heal', 1.2, False, 24), ('hurt', .45, False, 30), ('cheer', 1.2, False, 24),
         ('rest', 3.0, True, 12), ('rise', .9, False, 30), ('dance', 2.4, True, 25)]
BONES = ['root', 'hips', 'chest', 'head', 'arm_l', 'fore_l', 'hand_l', 'arm_r', 'fore_r', 'hand_r', 'leg_l', 'shin_l', 'foot_l',
         'leg_r', 'shin_r', 'foot_r', 'tail1', 'tail2']
TAU = math.tau
GORON_BALL_SHIFT = (0, .45, -.05)


def frames_of(seconds, loops, fps):
    return round(seconds * fps) + (0 if loops else 1)


# ---- dimensions (metres, standing on z = 0): where the joints are; the rig and the clips are built from these ------------------------------
DIM = {
    'kokiri': dict(H=.62, hipZ=.25, kneeZ=.14, ankleZ=.045, hipX=.045, shZ=.43, shX=.085, elZ=.34, wrZ=.265, handEnd=.225, neckZ=.44,
                   chestZ=.36, headZ=.52, headTop=.64, foot=.07,
                   t1=((0, .02, .66), (0, .07, .72)), t2=((0, .07, .72), (0, .15, .70))),
    'zora': dict(H=1.05, hipZ=.50, kneeZ=.27, ankleZ=.05, hipX=.06, shZ=.86, shX=.135, elZ=.68, wrZ=.51, handEnd=.43, neckZ=.9,
                 chestZ=.72, headZ=1.0, headTop=1.1, foot=.12,
                 t1=((0, .08, 1.03), (0, .18, .95)), t2=((0, .18, .95), (0, .23, .7))),
    'goron': dict(H=.95, hipZ=.32, kneeZ=.18, ankleZ=.05, hipX=.10, shZ=.73, shX=.27, elZ=.52, wrZ=.30, handEnd=.16, neckZ=.76,
                  chestZ=.58, headZ=.84, headTop=.99, foot=.12,
                  t1=((0, .08, .9), (0, .14, .9)), t2=((0, .14, .9), (0, .2, .9))),
    'gerudo': dict(H=1.1, hipZ=.53, kneeZ=.29, ankleZ=.06, hipX=.07, shZ=.89, shX=.13, elZ=.68, wrZ=.49, handEnd=.40, neckZ=.93,
                   chestZ=.75, headZ=1.0, headTop=1.1, foot=.1,
                   t1=((0, .08, 1.07), (0, .2, .97)), t2=((0, .2, .97), (0, .27, .7))),
}
# Where the walk is quick and how much each kind sways, bounces, swings its arms and legs, and how fast a full cycle (two steps) covers ground.
GAIT = {
    'kokiri': dict(leg=.62, arm=.55, knee=.9, lean=.10, bob=.018, twist=.12, speed=120.0),
    'zora': dict(leg=.5, arm=.42, knee=.8, lean=.06, bob=.020, twist=.14, speed=118.0),
    'goron': dict(leg=.38, arm=.3, knee=.55, lean=.04, bob=.016, twist=.10, speed=100.0),
    'gerudo': dict(leg=.52, arm=.42, knee=.8, lean=.05, bob=.016, twist=.16, speed=128.0),
}

# ---- materials: name -> (paint style, colour, options); the face colours are read from the same table ---------------------------------
MATS = {
    'kokiri': dict(
        skin=('skin', (.97, .79, .61), {}), nose=('skin', (.93, .70, .55), {}),
        tunic=('cloth', (.24, .58, .18), {}), tunic_hem=('cloth', (.24, .58, .18), dict(hem=True)), collar=('cloth', (.50, .76, .38), {}),
        shorts=('cloth', (.15, .43, .12), dict(hem=True)), hat=('cloth', (.26, .60, .19), {}), hat_band=('cloth', (.16, .44, .12), {}),
        belt=('leather', (.40, .25, .12), {}), gold=('gold', (.95, .78, .26), {}), hair=('fur', (.80, .46, .18), {}),
        boot=('leather', (.48, .31, .16), {}), boot_cuff=('leather', (.62, .43, .25), {}), wood=('wood', (.56, .37, .19), {}),
        band=('flat', (.82, .22, .16), {}), lid=('flat', (.30, .16, .08), {})),
    'goron': dict(
        skin=('skin', (.95, .74, .30), {}), torso=('torso', (.95, .74, .30), dict(belly=(1.0, .85, .48), navel=True)),
        sole=('skin', (.82, .58, .22), {}), fur=('fur', (.96, .94, .89), {}), brow=('fur', (.34, .20, .10), {}),
        iron=('metal', (.42, .40, .40), {}), rock=('rock', (.50, .40, .32), {}), cloth=('cloth', (.95, .93, .86), dict(hem=True)),
        mark=('flat', (.72, .26, .16), {}), lid=('flat', (.34, .20, .10), {})),
    'zora': dict(
        skin=('skin', (.72, .89, .98), {}), torso=('torso', (.72, .89, .98), dict(belly=(.93, .98, 1.0))), dark=('scales', (.48, .70, .90), {}),
        gold=('gold', (.95, .80, .30), {}), sapphire=('gold', (.22, .50, 1.0), {}),
        fin=('fin', (.58, .82, .96), {}), fin2=('fin', (.42, .66, .90), {}), lid=('flat', (.20, .20, .42), {})),
    'gerudo': dict(
        skin=('skin', (.80, .50, .31), {}), nose=('skin', (.74, .45, .28), {}), pants=('cloth', (.98, .84, .88), {}),
        top=('gold', (.88, .90, .94), {}), glove=('cloth', (.96, .62, .72), {}), gold=('gold', (.96, .78, .24), {}),
        gem=('flat', (.25, .55, .98), {}), ruby=('flat', (.86, .14, .22), {}), hair=('fur', (.84, .14, .08), {}), red2=('fur', (.60, .08, .05), {}),
        shoe=('leather', (.90, .56, .66), {}), wood=('wood', (.50, .30, .14), {}), feather=('flat', (.96, .93, .86), {}),
        lid=('flat', (.16, .07, .04), {})),
}


def face_palette(kind):
    m = MATS[kind]
    return {k: v[1] for k, v in m.items()}


# ---- geometry: everything goes into one list of vertices and faces, with per-face UVs, texture and smoothing ------------------------------
class Geo:
    def __init__(self, kind):
        self.kind = kind
        self.atlas = PA.Atlas(MATS[kind])
        self.V, self.W, self.F, self.FUV, self.FT, self.FS = [], [], [], [], [], []
        self.bones = {n: (Vector(h), Vector(t)) for n, (h, t, _) in bone_table(kind).items()}

    def weights(self, p, spec):
        if isinstance(spec, str):
            return {spec: 1.0}
        if callable(spec):
            return spec(p)
        ds = []
        for b in spec:
            h, t = self.bones[b]
            d = t - h
            k = max(0.0, min(1.0, (p - h).dot(d) / max(d.length_squared, 1e-9)))
            ds.append(((p - (h + d * k)).length, b))
        ds.sort()
        if len(ds) == 1:
            return {ds[0][1]: 1.0}
        eps = .006
        w0, w1 = 1 / (ds[0][0] + eps) ** 4, 1 / (ds[1][0] + eps) ** 4
        return {ds[0][1]: w0 / (w0 + w1), ds[1][1]: w1 / (w0 + w1)}

    def add(self, verts, faces, uvs, mats, w, smooth=True, tex=0):
        """verts: points; faces: index tuples; uvs: per face a tuple of 0..1 UVs (inside the material's cell); mats: per face a material."""
        base = len(self.V)
        for p in verts:
            p = FRAME @ Vector(p)
            self.V.append(p)
            self.W.append(self.weights(p, w))
        for f, uv, m in zip(faces, uvs, mats):
            self.F.append(tuple(base + i for i in f))
            if tex == 0:
                self.FUV.append(tuple(self.atlas.uv(m, u, v) for u, v in uv))
            else:
                self.FUV.append(tuple(uv))
            self.FT.append(tex)
            self.FS.append(smooth)


G = None
FRAME = Matrix.Identity(4)       # applied to every part: used to build props in the pose they are held in


def _frames(path, up):
    """Tangents and a twist-free side axis along a path (parallel transport)."""
    n = len(path)
    T = []
    for i in range(n):
        a, b = path[max(0, i - 1)], path[min(n - 1, i + 1)]
        T.append((b - a).normalized())
    up = Vector(up)
    side = up - T[0] * up.dot(T[0])
    if side.length < 1e-4:
        side = Vector((0, 1, 0)) - T[0] * T[0].y
    side.normalize()
    S = [side]
    for i in range(1, n):
        q = T[i - 1].rotation_difference(T[i])
        s = q @ S[-1]
        s = (s - T[i] * s.dot(T[i])).normalized()
        S.append(s)
    return T, S


def tube(path, radii, mat, w, seg=12, caps=(False, False), cap_round=1.0, up=(1, 0, 0), hem=None, closed=False, smooth=True, flat_below=None,
         bands=None, profile=None):
    """A loft through `path` with elliptical rings (radii: r or (side, other) per point). caps: rounded ends. hem: (depth, teeth) zigzag on
    the last ring. closed: the path is a loop (a ring band). bands: [(last ring index, material)] to change material along the tube.
    profile(j, angle) -> radius multiplier per ring vertex."""
    path = [Vector(p) for p in path]
    radii = [(r, r) if not isinstance(r, (tuple, list)) else tuple(r) for r in radii]
    if closed:
        path = path + [path[0]]
        radii = radii + [radii[0]]
    T, S = _frames(path, up)
    if closed:
        T[0] = T[-1] = (path[1] - path[-2]).normalized()
    rings, ring_v = [], []
    lengths = [0.0]
    for i in range(1, len(path)):
        lengths.append(lengths[-1] + (path[i] - path[i - 1]).length)
    total = max(lengths[-1], 1e-6)
    for i, (p, t, s) in enumerate(zip(path, T, S)):
        o = t.cross(s)
        ring = []
        for j in range(seg):
            a = TAU * j / seg
            rx, ry = radii[i]
            k = profile(i, a) if profile else 1.0
            q = p + (s * math.cos(a) * rx + o * math.sin(a) * ry) * k
            if hem and i == len(path) - 1 and j % 2 == 0:
                q = q + t * hem[0]
            if flat_below is not None and q.z < flat_below:
                q.z = flat_below
            ring.append(q)
        rings.append(ring)
        ring_v.append(1.0 - lengths[i] / total)
    verts, idx = [], []
    for i, ring in enumerate(rings):
        if closed and i == len(rings) - 1:
            idx.append(idx[0])
            continue
        idx.append(list(range(len(verts), len(verts) + seg)))
        verts.extend(ring)
    faces, uvs, mats = [], [], []

    def mat_at(i):
        if not bands:
            return mat, 0, len(path) - 1
        lo = 0
        for end, m in bands:
            if i < end:
                return m, lo, end
            lo = end
        return bands[-1][1], lo, len(path) - 1

    def vloc(i, lo, hi):
        if not bands:
            return ring_v[i]
        a, b = lengths[lo], lengths[hi]
        return 1.0 - (lengths[i] - a) / max(b - a, 1e-6)

    for i in range(len(path) - 1):
        m, lo, hi = mat_at(i)
        v0, v1 = vloc(i, lo, hi), vloc(i + 1, lo, hi)
        for j in range(seg):
            j1 = (j + 1) % seg
            faces.append((idx[i][j], idx[i][j1], idx[i + 1][j1], idx[i + 1][j]))
            uvs.append(((j / seg, v0), ((j + 1) / seg, v0), ((j + 1) / seg, v1), (j / seg, v1)))
            mats.append(m)
    if not closed:
        for end in (0, 1):
            if not caps[end]:
                continue
            i = 0 if end == 0 else len(path) - 1
            p, t, s = path[i], T[i] * (-1 if end == 0 else 1), S[i]
            m = mat_at(i if end == 0 else len(path) - 2)[0]
            vv = 1.0 if end == 0 else 0.0
            rx, ry = radii[i]
            r = (rx + ry) * .5 * cap_round
            prev = idx[i]
            for th in (math.radians(40), math.radians(72)):
                ring = [p + (q - p) * math.cos(th) + t * r * math.sin(th) for q in rings[i]]
                cur = list(range(len(verts), len(verts) + seg))
                verts.extend(ring)
                for j in range(seg):
                    j1 = (j + 1) % seg
                    f = (prev[j], prev[j1], cur[j1], cur[j]) if end == 1 else (prev[j1], prev[j], cur[j], cur[j1])
                    faces.append(f)
                    uvs.append(((j / seg, vv),) * 4)
                    mats.append(m)
                prev = cur
            pole = len(verts)
            verts.append(p + t * r)
            for j in range(seg):
                j1 = (j + 1) % seg
                faces.append((prev[j], prev[j1], pole) if end == 1 else (prev[j1], prev[j], pole))
                uvs.append(((j / seg, vv), ((j + 1) / seg, vv), ((j + .5) / seg, vv)))
                mats.append(m)
    G.add(verts, faces, uvs, mats, w, smooth)


def blob(c, r, mat, w, seg=16, rings=10, deform=None, rot=None, smooth=True, flat_below=None, noise=0.0, seed=0):
    """An ellipsoid (radii r) with poles on its local Z. deform(unit point) -> radius multiplier."""
    c = Vector(c)
    R = rot if rot is not None else Matrix.Identity(3)
    rnd = random.Random(seed)
    verts, faces, uvs, mats = [], [], [], []

    def point(u):
        k = deform(u) if deform else 1.0
        if noise:
            k *= 1 + noise * (rnd.random() - .5) * 2
        q = c + R @ Vector((u.x * r[0], u.y * r[1], u.z * r[2])) * k
        if flat_below is not None and q.z < flat_below:
            q.z = flat_below
        return q

    verts.append(point(Vector((0, 0, 1))))
    for i in range(1, rings):
        ph = math.pi * i / rings
        for j in range(seg):
            th = TAU * j / seg
            verts.append(point(Vector((math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph)))))
    verts.append(point(Vector((0, 0, -1))))
    bot = len(verts) - 1

    def at(i, j):
        return 1 + (i - 1) * seg + (j % seg)
    for j in range(seg):
        faces.append((0, at(1, j), at(1, j + 1)))
        uvs.append((((j + .5) / seg, 1.0), (j / seg, 1 - 1 / rings), ((j + 1) / seg, 1 - 1 / rings)))
        mats.append(mat)
    for i in range(1, rings - 1):
        for j in range(seg):
            faces.append((at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)))
            v0, v1 = 1 - i / rings, 1 - (i + 1) / rings
            uvs.append(((j / seg, v0), (j / seg, v1), ((j + 1) / seg, v1), ((j + 1) / seg, v0)))
            mats.append(mat)
    for j in range(seg):
        faces.append((at(rings - 1, j), bot, at(rings - 1, j + 1)))
        v0 = 1 / rings
        uvs.append(((j / seg, v0), ((j + .5) / seg, 0.0), ((j + 1) / seg, v0)))
        mats.append(mat)
    G.add(verts, faces, uvs, mats, w, smooth)


def slab(pts, normal, thick, mat, w, smooth=False):
    """A flat piece (a fin, a cloth flap): polygon `pts` pushed out `thick` along `normal`, UVs from its own box."""
    n = Vector(normal).normalized() * thick * .5
    P = [Vector(p) for p in pts]
    k = len(P)
    verts = [p + n for p in P] + [p - n for p in P]
    # planar UVs in the polygon's plane
    a = (P[1] - P[0]).normalized()
    b = Vector(normal).normalized().cross(a)
    us = [p.dot(a) for p in P]
    vs = [p.dot(b) for p in P]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    uv = [((x - u0) / max(u1 - u0, 1e-6), (y - v0) / max(v1 - v0, 1e-6)) for x, y in zip(us, vs)]
    faces = [tuple(range(k)), tuple(range(2 * k - 1, k - 1, -1))]
    uvs = [tuple(uv), tuple(uv[i - k] for i in range(2 * k - 1, k - 1, -1))]
    for i in range(k):
        j = (i + 1) % k
        faces.append((i, j, k + j, k + i))
        uvs.append((uv[i], uv[j], uv[j], uv[i]))
    G.add(verts, faces, uvs, [mat] * len(faces), w, smooth)


def tuft(base, tip, r, mat, w, bend=(0, 0, 0), seg=6):
    """A curved, pointed lock of hair or fur."""
    base, tip, bend = Vector(base), Vector(tip), Vector(bend)
    path = [base + (tip - base) * t + bend * math.sin(math.pi * t) for t in (0, .35, .7, 1.0)]
    tube(path, [r, r * .8, r * .45, r * .04], mat, w, seg=seg, caps=(True, False), cap_round=.6)


def band(c, axis, r, thick, mat, w, seg=8, n=18, rxy=None):
    """A ring around `axis` through `c` (a belt, an arm band, an earring)."""
    c, axis = Vector(c), Vector(axis).normalized()
    a = axis.orthogonal().normalized()
    b = axis.cross(a)
    rx, ry = rxy if rxy else (r, r)
    path = [c + a * math.cos(TAU * i / n) * rx + b * math.sin(TAU * i / n) * ry for i in range(n)]
    tube(path, [thick] * n, mat, w, seg=seg, closed=True, up=tuple(axis))


def on_ellipsoid(c, r, p, lift):
    """Push `p` onto the ellipsoid (centre c, radii r) along its direction from the centre, `lift` above the surface."""
    c, p = Vector(c), Vector(p)
    d = p - c
    k = 1.0 / math.sqrt((d.x / r[0]) ** 2 + (d.y / r[1]) ** 2 + (d.z / r[2]) ** 2)
    q = c + d * k
    return q + (q - c).normalized() * lift


def face_patch(centre, radii, deform=None, az=56.0, el=30.0, nx=16, ny=9, lift=1.012):
    """The painted face: a grid laid on the front of the head and mapped onto the whole face texture."""
    cx, cy, cz = centre
    rx, ry, rz = radii
    verts, faces, uvs = [], [], []
    for j in range(ny + 1):
        for i in range(nx + 1):
            a = math.radians(az * (2.0 * i / nx - 1.0))
            e = math.radians(el * (1.0 - 2.0 * j / ny))
            u = Vector((math.cos(e) * math.sin(a), -math.cos(e) * math.cos(a), math.sin(e)))
            k = (deform(u) if deform else 1.0) * lift
            verts.append((cx + rx * u.x * k, cy + ry * u.y * k, cz + rz * u.z * k))
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            faces.append((a, a + nx + 1, a + nx + 2, a + 1))
            uvs.append(((i / nx, 1 - j / ny), (i / nx, 1 - (j + 1) / ny), ((i + 1) / nx, 1 - (j + 1) / ny), ((i + 1) / nx, 1 - j / ny)))
    G.add(verts, faces, uvs, [None] * len(faces), 'head', True, tex=1)


def held(bone_pos):
    """Build a prop as it looks with the arm raised straight forward: this puts it back on the hanging hand."""
    return Matrix.Translation(bone_pos) @ Matrix.Rotation(math.radians(90), 4, 'X')


def mitten(c, r, thumb_side, mat, w):
    """A chunky hand: a rounded mitten and a thumb toward the body's front."""
    c = Vector(c)
    blob(c, r, mat, w, seg=12, rings=8)
    t0 = c + Vector((-thumb_side * r[0] * .4, -r[1] * .7, r[2] * .3))
    tube([t0, t0 + Vector((-thumb_side * r[0] * .25, -r[1] * .45, -r[2] * .5))], [r[0] * .42, r[0] * .32], mat, w, seg=8, caps=(False, True))


# ---- the four models -----------------------------------------------------------------------------------------------------------------
def build_kokiri():
    d = DIM['kokiri']
    # body: one loft from the neck to the frayed hem of the tunic
    zs = [.452, .44, .425, .40, .37, .335, .305, .29, .275, .245, .215, .19]
    rs = [(.034, .031), (.040, .036), (.062, .050), (.078, .060), (.081, .063), (.074, .058), (.072, .057), (.074, .059), (.080, .064),
          (.095, .074), (.108, .083), (.114, .088)]
    tube([(0, 0, z) for z in zs], rs, 'tunic', ['head', 'chest', 'hips'], seg=18, hem=(.016, 18),
         bands=[(1, 'skin'), (6, 'tunic'), (7, 'belt'), (11, 'tunic_hem')])
    slab([(-.032, -.058, .432), (.032, -.058, .432), (0, -.066, .382)], (0, -1, 0), .004, 'collar', 'chest')     # the light V of the collar
    blob((0, -.06, .297), (.018, .007, .013), 'gold', 'hips', seg=8, rings=6)                                       # buckle
    for s, sd in ((-1, 'l'), (1, 'r')):
        x = s * d['hipX']
        tube([(x, 0, .255), (x, 0, .20), (x, 0, .145), (x, 0, .10), (x, 0, .06)], [.026, .024, .021, .019, .018], 'skin',
             ['hips', 'leg_' + sd, 'shin_' + sd], seg=10)
        tube([(x * 1.05, 0, .262), (x * 1.1, 0, .215), (x * 1.14, 0, .178)], [.036, .037, .036], 'shorts', ['hips', 'leg_' + sd], seg=12, hem=(.012, 12))
        tube([(x, 0, .108), (x, 0, .100), (x, 0, .086), (x, 0, .06), (x, .004, .036)], [.030, .034, .031, .027, .028], 'boot',
             ['shin_' + sd, 'foot_' + sd], seg=12, caps=(True, False), cap_round=.4, bands=[(2, 'boot_cuff'), (4, 'boot')])
        tube([(x, .026, .03), (x, -.02, .027), (x, -.062, .026), (x, -.08, .029)], [(.028, .026), (.032, .025), (.030, .023), (.025, .019)],
             'boot', ['shin_' + sd, 'foot_' + sd], seg=12, caps=(True, True), cap_round=.8, flat_below=.003)
        sx = s * d['shX']
        tube([(sx, 0, .438), (s * .088, 0, .39), (s * .088, 0, .345), (s * .088, -.002, .30), (s * .088, 0, .262)],
             [.027, .024, .021, .021, .018], 'skin', ['chest', 'arm_' + sd, 'fore_' + sd], seg=10)
        tube([(s * .07, 0, .448), (s * .09, 0, .422), (s * .096, 0, .39)], [.032, .035, .034], 'tunic_hem', ['chest', 'arm_' + sd], seg=12,
             hem=(.012, 12))
        mitten((s * .088, -.004, .244), (.021, .018, .026), s, 'skin', 'hand_' + sd)
    # head: round, full cheeks, a small nose, long ears, the hair sticking out under the hat
    hc, hr = (0, 0, d['headZ']), (.125, .112, .118)

    def cheeks(u):
        return 1 + .07 * max(0.0, -u.y) * max(0.0, -u.z) * 2 - .03 * max(0.0, u.y) * max(0.0, u.z)
    blob(hc, hr, 'skin', 'head', seg=22, rings=14, deform=cheeks)
    face_patch(hc, hr, cheeks, az=58, el=32)
    blob((0, -.12, .503), (.019, .017, .017), 'nose', 'head', seg=10, rings=6)
    for s in (-1, 1):
        tube([(s * .11, .006, .515), (s * .16, .026, .536), (s * .218, .05, .568)], [(.03, .011), (.019, .008), (.002, .002)], 'skin', 'head',
             seg=8, up=(0, 0, 1), caps=(False, True), cap_round=.3)
        for tip in ((s * .195, .09, .505), (s * .185, .115, .47), (s * .16, .085, .45)):      # tufts behind the ears
            tuft((s * .1, .04, .53), tip, .026, 'hair', 'head', bend=(0, 0, .015))
    blob((0, .025, .545), (.128, .1, .095), 'hair', 'head', seg=16, rings=10)                                       # hair under the hat
    for i in range(7):                                                                                             # the fringe
        a = math.radians(-54 + 18 * i)
        b = Vector((math.sin(a) * .116, -math.cos(a) * .1, .588))
        tuft(b, b + Vector((math.sin(a) * .02, -.028, -.06 - .018 * (i % 2))), .022, 'hair', 'head', bend=(0, -.012, 0))
    for x in (-.06, 0, .06):
        tuft((x, .09, .52), (x * 1.4, .145, .44), .028, 'hair', 'head')
    # the pointed hat, floppy: it rides the two tail bones
    tube([(0, 0, .575), (0, .004, .625), (0, .018, .675), (0, .045, .715), (0, .085, .735), (0, .125, .725), (0, .158, .70), (0, .178, .672)],
         [.132, .118, .096, .074, .054, .036, .02, .003], 'hat', ['head', 'tail1', 'tail2'], seg=18, caps=(False, True), cap_round=.3)
    tube([(0, 0, .566), (0, 0, .586)], [.136, .134], 'hat_band', 'head', seg=18)
    # slingshot in the left hand
    global FRAME
    hp = Vector((-d['shX'], 0, d['wrZ'] - .02))
    FRAME = held(hp)
    tube([(0, 0, -.03), (0, 0, .07)], [.009, .008], 'wood', 'hand_l', seg=6, caps=(True, False))
    for s in (-1, 1):
        tube([(0, 0, .065), (s * .02, 0, .095), (s * .032, 0, .122)], [.008, .007, .006], 'wood', 'hand_l', seg=6, caps=(False, True))
    tube([(-.032, .004, .118), (.032, .004, .118)], [.004, .004], 'band', 'hand_l', seg=5)
    FRAME = Matrix.Identity(4)


def build_goron():
    d = DIM['goron']
    # the big body, one piece: a round belly in front (painted lighter, with a navel)
    rings = [(.27, .17, .14, 0), (.32, .23, .19, -.01), (.38, .28, .24, -.04), (.46, .30, .265, -.065), (.54, .30, .25, -.06),
             (.61, .30, .22, -.035), (.67, .31, .20, -.01), (.72, .29, .18, 0), (.77, .21, .15, 0), (.80, .14, .12, 0)]
    tube([(0, y, z) for z, _, _, y in rings], [(rx, ry) for _, rx, ry, _ in rings], 'torso', ['hips', 'chest', 'head'], seg=22,
         caps=(True, False), cap_round=.5)
    # loincloth: a white wrap, an iron chain belt with rings, flaps front and back
    tube([(0, -.012, .335), (0, -.008, .295), (0, 0, .255)], [(.25, .21), (.24, .205), (.205, .175)], 'cloth', 'hips', seg=22,
         caps=(False, True), cap_round=.4)
    band((0, -.012, .345), (0, 0, 1), .25, .02, 'iron', 'hips', seg=6, n=24, rxy=(.255, .215))
    for x in (-.09, 0, .09):
        band((x, -.235, .315), (0, 1, 0), .028, .008, 'iron', 'hips', seg=5, n=10)
    slab([(-.12, -.228, .335), (.12, -.228, .335), (.10, -.22, .175), (.0, -.212, .15), (-.10, -.22, .175)], (0, -1, 0), .012, 'cloth', 'hips')
    slab([(-.11, .19, .335), (.11, .19, .335), (.09, .185, .2), (-.09, .185, .2)], (0, 1, 0), .012, 'cloth', 'hips')
    for s, sd in ((-1, 'l'), (1, 'r')):
        tube([(s * .12, 0, .30), (s * .13, 0, .22), (s * .13, 0, .15), (s * .13, 0, .085)], [.115, .105, .098, .094], 'skin',
             ['hips', 'leg_' + sd, 'shin_' + sd], seg=14)
        blob((s * .13, -.04, .052), (.11, .15, .065), 'skin', ['shin_' + sd, 'foot_' + sd], seg=14, rings=8, flat_below=.002)
        for dx in (-.05, 0, .05):
            blob((s * .13 + dx, -.172, .036), (.034, .03, .03), 'sole', 'foot_' + sd, seg=8, rings=5, flat_below=.002)
        # arms: massive, bulging forearms, big fists with knuckles
        tube([(s * .27, 0, .72), (s * .31, 0, .66), (s * .32, 0, .58), (s * .32, 0, .52), (s * .32, -.01, .44), (s * .315, -.01, .37), (s * .31, 0, .32)],
             [.13, .125, .105, .095, .112, .106, .09], 'skin', ['chest', 'arm_' + sd, 'fore_' + sd], seg=14)
        blob((s * .30, 0, .70), (.14, .13, .13), 'skin', ['chest', 'arm_' + sd], seg=14, rings=9)
        fx = s * .315
        blob((fx, -.01, .245), (.105, .1, .1), 'skin', 'hand_' + sd, seg=14, rings=9)
        for dx in (-.05, -.017, .017, .05):
            blob((fx + dx, -.09, .225), (.03, .028, .03), 'skin', 'hand_' + sd, seg=8, rings=5)
        blob((fx - s * .075, -.06, .275), (.035, .035, .04), 'skin', 'hand_' + sd, seg=8, rings=5)
        # iron bands: one round the upper arm, a spiked cuff on the wrist
        band((s * .32, 0, .60), (0, 0, 1), .106, .02, 'iron', 'arm_' + sd, seg=6, n=16)
        tube([(fx, -.01, .37), (fx, -.005, .34), (fx, 0, .31)], [.122, .124, .118], 'iron', 'fore_' + sd, seg=14)
        for k in range(4):
            a = TAU * (k + .5) / 4
            o = Vector((math.cos(a), math.sin(a), 0))
            b = Vector((fx, -.005, .34)) + o * .12
            tube([b, b + o * .05], [.022, .002], 'iron', 'fore_' + sd, seg=6, caps=(False, True), cap_round=.2)
    # a rocky pauldron on the left shoulder
    for i, (p, r) in enumerate([((-.34, -.02, .83), .09), ((-.43, .03, .78), .08), ((-.31, .08, .81), .07), ((-.41, -.09, .77), .07),
                                ((-.46, -.02, .70), .06), ((-.26, -.07, .80), .06), ((-.37, .1, .74), .06)]):
        blob(p, (r * 1.1, r, r * .8), 'rock', 'arm_l', seg=7, rings=5, smooth=False, noise=.18, seed=i)
    for i, p in enumerate([(-.12, .2, .66), (.1, .21, .6), (0, .23, .5), (.15, .19, .7)]):         # rock studs on the back
        blob(p, (.05, .035, .045), 'rock', 'chest', seg=7, rings=5, smooth=False, noise=.15, seed=10 + i)
    # the Goron mark on the right shoulder: three dark triangles
    sc, sr = (.30, 0, .70), (.14, .13, .13)
    for cy_, cz_ in ((0, .03), (-.03, -.02), (.03, -.02)):
        tri = [(0, cy_, cz_ + .022), (0, cy_ - .02, cz_ - .013), (0, cy_ + .02, cz_ - .013)]
        pts = [on_ellipsoid(sc, sr, Vector((.44, y, .70 + z)), .003) for _, y, z in tri]
        slab(pts, (1, 0, 0), .002, 'mark', 'arm_r')
    # head: low between the shoulders, a broad jaw, heavy brows, a white mane and beard all round
    hc, hr = (0, -.03, .87), (.175, .16, .155)

    def jaw(u):
        return 1 + .08 * max(0.0, -u.z) * max(0.0, -u.y)
    blob(hc, hr, 'skin', 'head', seg=22, rings=14, deform=jaw)
    face_patch(hc, hr, jaw, az=58, el=30)
    blob((0, -.192, .858), (.05, .038, .04), 'skin', 'head', seg=10, rings=6)
    for s in (-1, 1):
        tube([(s * .02, -.178, .925), (s * .075, -.172, .94), (s * .132, -.14, .928)], [.026, .029, .02], 'brow', 'head', seg=8, caps=(True, True))
        blob((s * .175, -.01, .86), (.03, .048, .058), 'skin', 'head', seg=8, rings=5)
    rnd = random.Random(3)
    n = 90
    for i in range(n):                         # the mane: soft tufts swept out and back over the head, none on the face
        z = 1 - 2 * (i + .5) / n
        a = math.pi * (1 + 5 ** .5) * i
        u = Vector((math.sqrt(1 - z * z) * math.cos(a), math.sqrt(1 - z * z) * math.sin(a), z))
        if (u.y < -.3 and u.z < .62) or u.z < -.5:
            continue
        b = Vector(hc) + Vector((u.x * hr[0], u.y * hr[1], u.z * hr[2])) * .9
        out = (u + Vector((0, .55, .12))).normalized()
        ln = .1 + .05 * rnd.random()
        tuft(b, b + out * ln, .062, 'fur', 'head', bend=(0, .02, .02))
    for row, (dy, ln0) in enumerate(((0, .25), (.04, .2), (.08, .15))):    # the big beard over the chest, three rows
        for k in range(9):
            a = math.radians(-80 + 20 * k)
            b = Vector((math.sin(a) * .15, hc[1] - .135 * math.cos(a) + dy, hc[2] - .1 + .04 * abs(math.sin(a))))
            ln = ln0 - .08 * abs(math.sin(a))
            tuft(b, b + Vector((math.sin(a) * .06, -.07, -ln)), .062, 'fur', 'head', bend=(0, -.03, 0))
    for s in (-1, 1):                          # wide whiskers at the cheeks
        for dz, dy in ((.0, .0), (-.05, .03), (.05, .03), (-.1, .0)):
            b = Vector((s * .16, -.06 + dy, .86 + dz))
            tuft(b, b + Vector((s * .16, .04, -.03 + dz * .5)), .055, 'fur', 'head', bend=(0, 0, .02))


def build_zora():
    d = DIM['zora']
    zs = [(.46, .09, .074), (.52, .115, .086), (.58, .105, .078), (.64, .088, .066), (.72, .104, .074), (.80, .116, .078), (.845, .108, .072),
          (.882, .075, .06), (.915, .05, .05), (.96, .044, .044)]
    tube([(0, 0, z) for z, _, _ in zs], [(a, b) for _, a, b in zs], 'torso', ['hips', 'chest', 'head'], seg=18, caps=(True, False), cap_round=.5)
    band((0, 0, .9), (0, 0, 1), .05, .008, 'gold', 'chest', seg=6, n=16)                                        # Zora's Sapphire on a gold chain
    blob((0, -.06, .865), (.02, .012, .03), 'gold', 'chest', seg=10, rings=7)
    blob((0, -.068, .862), (.016, .01, .026), 'sapphire', 'chest', seg=10, rings=7, deform=lambda u: 1 + .25 * max(0.0, -u.z))
    for s, sd in ((-1, 'l'), (1, 'r')):
        x = s * d['hipX']
        tube([(s * .065, 0, .53), (s * .07, 0, .42), (s * .07, 0, .33), (s * .07, 0, .27), (s * .066, 0, .19), (s * .06, 0, .07)],
             [.064, .056, .044, .04, .042, .028],
             'skin', ['hips', 'leg_' + sd, 'shin_' + sd], seg=12)
        tube([(s * .06, .025, .045), (s * .06, -.04, .03), (s * .06, -.1, .02), (s * .062, -.14, .014)],
             [(.03, .022), (.036, .015), (.044, .009), (.04, .006)], 'dark', ['shin_' + sd, 'foot_' + sd], seg=10, up=(1, 0, 0),
             caps=(True, True), cap_round=.5, flat_below=.004)
        slab([(s * .1, .0, .58), (s * .2, .05, .52), (s * .25, .12, .36), (s * .16, .09, .42), (s * .1, .04, .5)], (s, -.2, .3), .01, 'fin',
             ['hips', 'leg_' + sd])                                                                               # hip fins
        slab([(s * .09, .03, .32), (s * .11, .12, .25), (s * .095, .15, .15), (s * .08, .05, .22)], (1, 0, 0), .008, 'fin', 'shin_' + sd)
        tube([(s * .105, 0, .872), (s * .138, 0, .845), (s * .145, 0, .78), (s * .145, 0, .68), (s * .145, -.003, .6), (s * .142, 0, .52)],
             [.05, .048, .04, .034, .036, .027],
             'skin', ['chest', 'arm_' + sd, 'fore_' + sd], seg=12, caps=(True, False), cap_round=.8)
        slab([(s * .15, .025, .71), (s * .16, .13, .65), (s * .16, .19, .52), (s * .15, .06, .58)], (1, 0, 0), .009, 'fin', 'fore_' + sd)
        mitten((s * .142, -.003, .48), (.03, .023, .044), s, 'skin', 'hand_' + sd)
    # head: a dome that runs back into the long head-tail, a fin crest over the top, small fins at the sides
    hc, hr = (0, .01, d['headZ'] + .005), (.1, .115, .108)

    def dome(u):
        return 1 + .14 * max(0.0, u.y) * max(0.0, u.z + .3) + .05 * max(0.0, u.z)
    blob(hc, hr, 'skin', 'head', seg=22, rings=14, deform=dome)
    face_patch(hc, hr, dome, az=56, el=30)
    tube([(0, .02, 1.075), (0, .09, 1.07), (0, .16, 1.02), (0, .21, .94), (0, .235, .84), (0, .24, .74), (0, .235, .68)],
         [(.085, .08), (.08, .074), (.068, .062), (.052, .05), (.04, .038), (.028, .024), (.004, .004)], 'skin', ['head', 'tail1', 'tail2'],
         seg=16, caps=(False, True), cap_round=.3, bands=[(2, 'skin'), (6, 'dark')])
    slab([(0, -.03, 1.1), (0, .06, 1.135), (0, .17, 1.075), (0, .255, .95), (0, .275, .8), (0, .25, .7), (0, .225, .76), (0, .185, .95),
          (0, .1, 1.06)], (1, 0, 0), .01, 'fin2', ['head', 'tail1', 'tail2'])
    for s in (-1, 1):
        slab([(s * .088, -.0, 1.02), (s * .135, .06, 1.045), (s * .125, .085, .975), (s * .088, .045, .975)], (1, 0, 0), .006, 'fin2', 'head')


def build_gerudo():
    d = DIM['gerudo']
    zs = [(.52, .11, .085, 0), (.58, .115, .088, 0), (.64, .088, .068, 0), (.70, .085, .065, 0), (.76, .098, .072, -.006), (.80, .112, .08, -.012),
          (.85, .105, .072, -.006), (.88, .10, .07, 0), (.91, .05, .045, 0), (.96, .04, .04, 0)]
    tube([(0, y, z) for z, _, _, y in zs], [(a, b) for _, a, b, _ in zs], 'skin', ['hips', 'chest', 'head'], seg=18, caps=(True, False),
         cap_round=.5, bands=[(2, 'pants'), (4, 'skin'), (6, 'top'), (9, 'skin')])
    band((0, 0, .925), (0, 0, 1), .05, .012, 'gold', 'chest', seg=6, n=16)                                          # choker
    blob((0, -.052, .905), (.014, .008, .02), 'ruby', 'chest', seg=8, rings=6)
    band((0, 0, .632), (0, 0, 1), .09, .01, 'gold', 'hips', seg=6, n=18, rxy=(.092, .072))                           # the belt
    slab([(-.04, -.074, .64), (.04, -.074, .64), (0, -.08, .58)], (0, -1, 0), .008, 'gold', 'hips')
    blob((0, -.084, .615), (.014, .007, .02), 'gem', 'hips', seg=8, rings=6)
    for s, sd in ((-1, 'l'), (1, 'r')):
        tube([(s * .07, 0, .57), (s * .08, 0, .45), (s * .086, 0, .32), (s * .08, 0, .19), (s * .075, 0, .115), (s * .075, 0, .095)],
             [.10, .112, .108, .092, .062, .048], 'pants', ['hips', 'leg_' + sd, 'shin_' + sd], seg=14)
        band((s * .075, 0, .1), (0, 0, 1), .05, .01, 'gold', 'shin_' + sd, seg=6, n=12)
        tube([(s * .075, .03, .06), (s * .075, -.04, .055), (s * .075, -.11, .055), (s * .075, -.15, .075), (s * .075, -.16, .105)],
             [(.034, .03), (.038, .03), (.03, .024), (.016, .014), (.004, .004)], 'shoe', ['shin_' + sd, 'foot_' + sd], seg=10,
             caps=(True, False), flat_below=.004)
        tube([(s * .13, 0, .893), (s * .135, 0, .80), (s * .135, 0, .735), (s * .135, 0, .68), (s * .135, -.003, .58), (s * .135, 0, .49)],
             [.034, .030, .029, .028, .025, .02], 'skin', ['chest', 'arm_' + sd, 'fore_' + sd], seg=12, bands=[(2, 'skin'), (5, 'glove')])
        tube([(s * .135, 0, .75), (s * .135, 0, .728)], [.038, .034], 'glove', 'arm_' + sd, seg=12)                    # the glove's cuff
        blob((s * .125, 0, .886), (.04, .04, .04), 'skin', ['chest', 'arm_' + sd], seg=12, rings=8)
        for z in (.83, .815):
            band((s * .135, 0, z), (0, 0, 1), .034, .006, 'gold', 'arm_' + sd, seg=5, n=12)
        mitten((s * .135, -.003, .457), (.024, .018, .036), s, 'glove', 'hand_' + sd)
        band((s * .088, -.005, .955), (0, 1, 0), .014, .003, 'gold', 'head', seg=5, n=10)                          # hoop earrings
    # head: a slim face, a hooked nose, the red hair swept back into a ponytail that falls to her waist
    hc, hr = (0, 0, d['headZ']), (.085, .09, .098)

    def chin(u):
        return 1 - .1 * max(0.0, -u.z) * (1 - abs(u.y)) * .5 + .03 * max(0.0, -u.y) * max(0.0, u.z)
    blob(hc, hr, 'skin', 'head', seg=22, rings=14, deform=chin)
    face_patch(hc, hr, chin, az=56, el=30)
    tube([(0, -.082, 1.01), (0, -.103, .99), (0, -.1, .972)], [.012, .011, .007], 'nose', 'head', seg=8, caps=(False, True))
    blob((0, .02, 1.025), (.093, .094, .09), 'hair', 'head', seg=18, rings=12, deform=lambda u: 1 - .25 * max(0.0, -u.y) * max(0.0, -u.z))
    blob((0, -.085, 1.075), (.016, .007, .024), 'gold', 'head', seg=8, rings=6)                                      # the jewel on her brow
    blob((0, -.09, 1.073), (.009, .005, .013), 'gem', 'head', seg=8, rings=6)
    band((0, .035, 1.115), (0, .3, 1), .03, .009, 'gold', 'head', seg=6, n=12)
    tube([(0, .03, 1.105), (0, .06, 1.155), (0, .12, 1.14), (0, .18, 1.05), (0, .22, .93), (0, .25, .79), (0, .27, .65), (0, .272, .52)],
         [(.04, .032), (.05, .03), (.06, .03), (.065, .028), (.062, .026), (.052, .022), (.035, .016), (.006, .004)], 'hair',
         ['head', 'tail1', 'tail2'], seg=12, caps=(False, True), cap_round=.4, up=(1, 0, 0))
    # bow in the left hand, quiver and arrows on the back
    global FRAME
    hp = Vector((-d['shX'], 0, d['wrZ'] - .02))
    FRAME = held(hp)
    path = [Vector((0, -.09 * math.sin(math.pi * t) + .045, (t - .5) * .55)) for t in np.linspace(0, 1, 11)]
    tube(path, [.008, .010, .011, .012, .012, .013, .012, .012, .011, .010, .008], 'wood', 'hand_l', seg=6, caps=(True, True))
    tube([(0, .045, -.275), (0, .045, .275)], [.0025, .0025], 'feather', 'hand_l', seg=4)
    FRAME = Matrix.Identity(4)
    tube([(.07, .1, .96), (.1, .12, .6)], [.05, .045], 'wood', 'chest', seg=10, caps=(True, True), cap_round=.3)
    for i, o in enumerate((-.012, .012, 0)):
        tube([(.07 + o, .1, .95), (.065 + o * 2, .105, 1.03 + .02 * i)], [.006, .006], 'feather', 'chest', seg=5, caps=(False, True))
    tube([(-.1, .0, .98), (.1, .12, .62)], [.004, .004], 'wood', 'chest', seg=4)


BUILDERS = {'kokiri': build_kokiri, 'zora': build_zora, 'goron': build_goron, 'gerudo': build_gerudo}


def bone_table(kind):
    d = DIM[kind]
    hz, hx, sx = d['hipZ'], d['hipX'], d['shX']
    b = {
        'root': ((0, 0, 0), (0, 0, .06), None),
        'hips': ((0, 0, hz), (0, 0, hz + .06), 'root'),
        'chest': ((0, 0, hz + .06), (0, 0, d['neckZ']), 'hips'),
        'head': ((0, 0, d['neckZ']), (0, 0, d['headTop']), 'chest'),
        'tail1': (d['t1'][0], d['t1'][1], 'head'),
        'tail2': (d['t2'][0], d['t2'][1], 'tail1'),
    }
    for s, sf in ((-1, 'l'), (1, 'r')):
        b['arm_' + sf] = ((s * sx, 0, d['shZ']), (s * sx, 0, d['elZ']), 'chest')
        b['fore_' + sf] = ((s * sx, 0, d['elZ']), (s * sx, 0, d['wrZ']), 'arm_' + sf)
        b['hand_' + sf] = ((s * sx, 0, d['wrZ']), (s * sx, 0, d['handEnd']), 'fore_' + sf)
        b['leg_' + sf] = ((s * hx, 0, hz), (s * hx, 0, d['kneeZ']), 'hips')
        b['shin_' + sf] = ((s * hx, 0, d['kneeZ']), (s * hx, 0, d['ankleZ']), 'leg_' + sf)
        b['foot_' + sf] = ((s * hx, 0, d['ankleZ']), (s * hx, -d['foot'], d['ankleZ']), 'shin_' + sf)
    return b


# ---- animation ---------------------------------------------------------------------------------------------------------------------------
class Ctl:
    """Poses the rig with rotations about the world axes (so '+forward' means the same for every bone)."""
    def __init__(self, rig):
        self.rig = rig
        self.R = {b.name: b.matrix_local.to_3x3() for b in rig.data.bones}
        self.reset()

    def reset(self):
        self.q = {n: Quaternion() for n in BONES}
        self.loc = {n: Vector() for n in BONES}

    def rotw(self, bone, axis, a):
        self.q[bone] = Quaternion(axis, a) @ self.q[bone]

    # limbs that hang down: swing the lower end forward (+) / back (-)
    def sw(self, bone, a): self.rotw(bone, (1, 0, 0), -a)
    # things that point up (chest, head): lean the top forward (+)
    def lean(self, bone, a): self.rotw(bone, (1, 0, 0), a)
    def tw(self, bone, a): self.rotw(bone, (0, 0, 1), a)       # + turns to the character's right... see calibration in preview
    def roll(self, bone, a): self.rotw(bone, (0, 1, 0), a)     # + tips the top toward +X (the right)
    def ab(self, bone, a):                                       # a hanging limb out to the side (+)
        self.rotw(bone, (0, 1, 0), a if bone.endswith('_l') else -a)
    def move(self, bone, v): self.loc[bone] += Vector(v)

    def snapshot(self, fn):
        self.reset()
        fn()
        return ({k: v.copy() for k, v in self.q.items()}, {k: v.copy() for k, v in self.loc.items()})

    def blend(self, a, b, k):
        for n in BONES:
            self.q[n] = a[0][n].slerp(b[0][n], k)
            self.loc[n] = a[1][n].lerp(b[1][n], k)

    def apply(self, rig):
        for n in BONES:
            pb = rig.pose.bones[n]
            R = self.R[n]
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (R.inverted() @ self.q[n].to_matrix() @ R).to_quaternion()
            pb.location = R.inverted() @ self.loc[n]
        bpy.context.view_layer.update()


def sm(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def mix(a, b, k):
    return a + (b - a) * k


# One pose function per clip. `ph` is a phase in radians for loops, `u` runs 0..1 for one-shots. Every clip ends close to the ready stance so the
# game can fade into idle.
def ready(P, k, arms=True):
    """The ready stance each kind stands in. Clips that pose the arms themselves (attacks, cheering) take only the legs."""
    P.sw('leg_l', .04); P.sw('leg_r', -.02)
    P.sw('shin_l', -.08); P.sw('shin_r', -.06)
    P.ab('leg_l', .05); P.ab('leg_r', .05)
    if k == 'goron':
        P.ab('leg_l', .15); P.ab('leg_r', .15); P.lean('chest', .08)
    if not arms:
        return
    if k == 'kokiri':      # hands forward holding the slingshot
        P.sw('arm_l', .55); P.sw('fore_l', .8); P.sw('arm_r', .25); P.sw('fore_r', .9); P.ab('arm_r', .15)
    elif k == 'zora':
        P.ab('arm_l', .22); P.ab('arm_r', .22); P.sw('fore_l', .25); P.sw('fore_r', .25); P.sw('arm_l', .08); P.sw('arm_r', .08)
        P.lean('head', .05)
    elif k == 'goron':     # fists up, elbows out, ready to fight
        P.ab('arm_l', .38); P.ab('arm_r', .38); P.sw('arm_l', .2); P.sw('arm_r', .2); P.sw('fore_l', 1.1); P.sw('fore_r', 1.1)
        P.ab('fore_l', -.25); P.ab('fore_r', -.25)
    else:                  # bow held low in the left hand, right hand loose
        P.sw('arm_l', .4); P.sw('fore_l', .7); P.sw('arm_r', .1); P.ab('arm_r', .12); P.sw('fore_r', .3)
        P.tw('chest', .05)


def pose_idle(P, k, ph):
    ready(P, k)
    s, c = math.sin(ph), math.cos(ph)
    P.lean('chest', .025 * s)
    P.tw('head', .22 * math.sin(ph) * (1 if k != 'goron' else .6))
    P.lean('head', -.03 * s + .03 * c)
    P.move('root', (0, 0, .004 * c))
    P.roll('hips', .025 * math.sin(ph)); P.roll('chest', -.02 * math.sin(ph))
    amp = {'kokiri': .05, 'zora': .04, 'goron': .03, 'gerudo': .03}[k]
    P.sw('arm_l', amp * s); P.sw('arm_r', -amp * s)
    P.rotw('tail1', (1, 0, 0), .06 * s); P.rotw('tail1', (0, 0, 1), .08 * math.sin(ph * 2))
    P.rotw('tail2', (0, 0, 1), .14 * math.sin(ph * 2 - .8)); P.rotw('tail2', (1, 0, 0), .06 * c)
    if k == 'zora':     # a gentle sway and fin flutter
        P.roll('chest', .03 * math.sin(ph)); P.rotw('tail2', (1, 0, 0), .12 * math.sin(ph * 3))
    if k == 'goron':
        P.move('root', (0, 0, .008 * math.sin(ph * 2)))    # heavy breathing
        P.lean('chest', .02 * math.sin(ph * 2))
    if k == 'gerudo':   # weight on one hip, a hand on it
        P.roll('hips', .03); P.ab('arm_r', .5); P.ab('fore_r', -1.15); P.sw('fore_r', -.1)
        P.move('hips', (.012 * math.sin(ph), 0, 0))


def pose_walk(P, k, ph):
    g = GAIT[k]
    s, c = math.sin(ph), math.cos(ph)
    for sd, sg in (('l', 1.0), ('r', -1.0)):
        a = s * sg
        sw_ph = c * sg                                  # > 0 while the leg swings forward
        P.sw('leg_' + sd, g['leg'] * a)
        P.sw('shin_' + sd, -g['knee'] * max(0.0, sw_ph) ** .8 - .1)
        P.sw('foot_' + sd, .25 * max(0.0, -a) - .3 * max(0.0, sw_ph) * .3)
        P.ab('leg_' + sd, .04 + (.1 if k == 'goron' else 0))
        arm = -a * g['arm']
        P.sw('arm_' + sd, arm)
        P.sw('fore_' + sd, .35 + .5 * max(0.0, arm / max(g['arm'], .01)))
        P.ab('arm_' + sd, .06 + (.28 if k == 'goron' else 0))
    P.lean('chest', g['lean'] + .02 * math.cos(2 * ph))
    P.tw('chest', g['twist'] * s * 0.9); P.tw('hips', -g['twist'] * s * .8); P.tw('head', -g['twist'] * s * .5)
    P.roll('hips', .04 * s * (3 if k == 'goron' else 1))
    P.roll('chest', -.03 * s * (3 if k == 'goron' else 1))
    P.move('root', (0, 0, g['bob'] * (.5 + .5 * math.cos(2 * ph))))
    if k == 'goron':
        P.move('hips', (.02 * s, 0, 0))
    if k == 'kokiri':   # the ready stance slingshot arm stays forward
        P.sw('arm_l', .5); P.sw('fore_l', .6)
    if k == 'gerudo':
        P.sw('arm_l', .45 - .1 * s); P.sw('fore_l', .6)
    P.rotw('tail1', (1, 0, 0), .16 * math.cos(2 * ph - .6)); P.rotw('tail1', (0, 0, 1), .3 * math.sin(ph - .5))
    P.rotw('tail2', (1, 0, 0), .2 * math.cos(2 * ph - 1.3)); P.rotw('tail2', (0, 0, 1), .42 * math.sin(ph - 1.1))


def pose_invite(P, k, ph):
    s, c = math.sin(ph), math.cos(ph)
    ready(P, k, arms=(k == 'zora'))
    if k == 'kokiri':   # both arms up, hopping and waving
        hop = abs(math.sin(ph * 3))
        P.move('root', (0, 0, .03 * hop))
        for sd, o in (('l', 0.0), ('r', math.pi)):
            w = math.sin(ph * 3 + o)
            P.ab('arm_' + sd, 1.35 + .3 * w); P.sw('arm_' + sd, .35); P.ab('fore_' + sd, .5 + .5 * w)
        P.sw('leg_l', -.2 * hop); P.sw('leg_r', .1 * hop); P.sw('shin_l', -.4 * hop); P.sw('shin_r', -.4 * hop)
        P.tw('head', .2 * s)
    elif k == 'zora':   # arms open, a slow graceful welcome
        P.ab('arm_l', .9 + .12 * s); P.ab('arm_r', .9 - .12 * s); P.sw('fore_l', .35); P.sw('fore_r', .35)
        P.sw('arm_l', .25); P.sw('arm_r', .25); P.roll('chest', .08 * s); P.lean('head', -.06); P.roll('head', -.1 * s)
        P.move('root', (0, 0, .008 * math.sin(ph * 2)))
        P.rotw('tail1', (1, 0, 0), .1 * math.sin(ph * 2)); P.rotw('tail2', (1, 0, 0), .2 * math.sin(ph * 2 - 1))
    elif k == 'goron':  # pounds his chest, laughing
        hit_r, hit_l = max(0, math.sin(ph * 2)), max(0, -math.sin(ph * 2))
        P.lean('chest', -.18 + .05 * hit_r + .05 * hit_l)
        for sd, h in (('l', hit_l), ('r', hit_r)):
            P.ab('arm_' + sd, .5 - .4 * h); P.sw('arm_' + sd, .6 + .3 * h); P.sw('fore_' + sd, 1.7 + .5 * h); P.ab('fore_' + sd, -.7 * h)
        P.move('root', (0, 0, -.01 * (hit_l + hit_r)))
        P.lean('head', -.2)
        P.sw('leg_l', .1); P.sw('leg_r', -.1)
    else:               # one hand on the hip, beckoning with the other
        P.roll('hips', .06 * math.sin(ph)); P.ab('arm_r', .55); P.ab('fore_r', -1.15)
        P.sw('arm_l', 1.0); P.sw('fore_l', 1.0 + .6 * math.sin(ph * 2)); P.ab('arm_l', .1)
        P.roll('head', -.1 * s); P.lean('head', -.05)
        P.tw('chest', .08 * s)
        P.rotw('tail1', (0, 0, 1), .25 * math.sin(ph)); P.rotw('tail2', (0, 0, 1), .35 * math.sin(ph - .8))
    P.rotw('tail2', (0, 0, 1), .15 * math.sin(ph * 2 - 1.0))


def pose_attack(P, k, u, var):
    """Frame 0 is the wind-up held a moment (the game starts this clip as the hit lands), the strike peaks at u = .12."""
    ready(P, k, arms=False)
    strike = sm(u / .12)
    rec = sm((u - .42) / .5)
    e = strike * (1 - rec)
    flip = -1.0 if var else 1.0        # which side
    sdA, sdB = ('r', 'l') if not var else ('l', 'r')
    snap = math.sin(min(1.0, u / .12) * math.pi) if u < .12 else 0.0
    if k == 'goron':
        # a hooked punch: fist drawn back, then driven through; the guard hand stays up. torso follows the fist.
        P.tw('chest', flip * mix(.55, -.6, strike) * (1 - rec))
        P.tw('hips', flip * mix(.25, -.2, strike) * (1 - rec))
        P.tw('head', flip * .35 * e)
        P.lean('chest', .1 + .32 * e)
        P.ab('arm_' + sdA, mix(.5, .2, strike)); P.sw('arm_' + sdA, mix(-.5, 1.58, strike) * (1 - rec) + .2 * rec)
        P.sw('fore_' + sdA, mix(2.0, .04, strike) * (1 - rec) + 1.1 * rec); P.ab('fore_' + sdA, 0)
        P.sw('arm_' + sdB, .55 * (1 - e) + .8 * e); P.sw('fore_' + sdB, 1.9)
        P.sw('leg_' + sdA, -.12 * e); P.sw('leg_' + sdB, .38 * e); P.sw('shin_' + sdB, -.2 * e)   # a step into the blow
        P.move('root', (0, -.07 * e, -.05 * e + .02 * snap))
        P.tw('hips', 0)
    elif k == 'kokiri':
        # slingshot: left arm out, right hand pulls the band to the cheek, then lets it go.
        rel = sm((u - .08) / .06)
        P.tw('chest', .25 * (1 - rec * .8) * (1 if not var else .9)); P.tw('head', -.22 * (1 - rec))
        P.sw('arm_l', mix(.55, 1.56, 1 - rec) + .06 * snap); P.sw('fore_l', mix(.8, .05, 1 - rec))
        P.ab('arm_l', .08)
        P.sw('arm_r', mix(.25, 1.2, 1 - rec) + .1 * (1 - rel)); P.ab('arm_r', mix(.15, .85, 1 - rec) * (1 - .5 * rel))
        P.sw('fore_r', mix(.9, 2.25, 1 - rec) * (1 - .55 * rel)); P.lean('chest', -.05 * (1 - rec) + .1 * snap)
        P.sw('leg_l', .22 * (1 - rec)); P.sw('leg_r', -.2 * (1 - rec))
        P.move('root', (0, 0, -.008 * e))
    elif k == 'gerudo':
        rel = sm((u - .08) / .05)
        P.tw('chest', .42 * (1 - rec * .85)); P.tw('hips', .15 * (1 - rec)); P.tw('head', -.35 * (1 - rec))
        P.sw('arm_l', mix(.4, 1.57, 1 - rec) + .05 * snap); P.sw('fore_l', mix(.7, .0, 1 - rec)); P.ab('arm_l', .05)
        P.sw('arm_r', mix(.1, .9, 1 - rec)); P.ab('arm_r', mix(.12, 1.25, 1 - rec) * (1 - .35 * rel)); P.sw('fore_r', mix(.3, 2.4, 1 - rec) * (1 - .45 * rel))
        P.sw('leg_l', .3 * (1 - rec)); P.sw('leg_r', -.28 * (1 - rec)); P.ab('leg_l', .1); P.ab('leg_r', .1)
        P.rotw('tail1', (0, 0, 1), .4 * snap); P.rotw('tail2', (0, 0, 1), -.5 * snap)
    else:  # zora: both palms thrown forward, the body whipping through behind them
        pull = 1 - strike
        P.lean('chest', -.1 * (1 - rec) * pull + .32 * e * strike); P.lean('head', .1 * e)
        for sd in ('l', 'r'):
            P.sw('arm_' + sd, mix(-.3, 1.5, strike) * (1 - rec) + .1 * rec); P.sw('fore_' + sd, mix(2.0, .05, strike) * (1 - rec) + .25 * rec)
            P.ab('arm_' + sd, mix(.15, .12, strike) + .1 * e)
        P.tw('chest', .3 * pull * (1 - rec) - .15 * e); P.sw('leg_l', .28 * e); P.sw('leg_r', -.25 * e)
        P.rotw('tail1', (1, 0, 0), .5 * snap - .1 * e); P.rotw('tail2', (1, 0, 0), .8 * snap - .2 * e)
        P.move('root', (0, -.03 * e, 0))


def pose_heal(P, k, u):
    if k != 'zora':
        pose_cheer(P, k, u)
        return
    ready(P, k, arms=False)
    up = sm(u / .4) * (1 - sm((u - .55) / .35))
    out = sm((u - .45) / .2) * (1 - sm((u - .8) / .2))
    for sd in ('l', 'r'):
        P.sw('arm_' + sd, 2.9 * up + .9 * out); P.ab('arm_' + sd, .5 * up); P.sw('fore_' + sd, .25 * up + .2)
    P.lean('chest', -.22 * up + .2 * out); P.lean('head', -.2 * up)
    P.move('root', (0, 0, .03 * up))
    P.rotw('tail1', (1, 0, 0), .5 * up); P.rotw('tail2', (1, 0, 0), .7 * up - .3 * out)


def pose_hurt(P, k, u):
    ready(P, k)
    env = math.sin(min(1.0, u) * math.pi) ** .7
    P.lean('chest', -.42 * env); P.lean('head', -.35 * env); P.tw('head', .3 * env)
    for sd in ('l', 'r'):
        P.ab('arm_' + sd, .5 * env); P.sw('arm_' + sd, -.3 * env)
    P.move('root', (0, .05 * env, -.025 * env)); P.sw('leg_l', -.15 * env); P.sw('leg_r', .15 * env); P.sw('shin_l', -.2 * env)
    P.rotw('tail1', (1, 0, 0), .6 * env); P.rotw('tail2', (1, 0, 0), .8 * env)


def pose_cheer(P, k, u):
    ready(P, k, arms=False)
    hop = abs(math.sin(u * math.pi * 3)) * (1 - sm((u - .8) / .2)) * (1 if u < 1 else 0)
    raise_ = sm(u / .12) * (1 - sm((u - .82) / .18))
    P.move('root', (0, 0, .07 * hop * (1.4 if k == 'kokiri' else 1.0)))
    for sd in ('l', 'r'):
        P.sw('arm_' + sd, 2.5 * raise_ + .6 * (1 - raise_)); P.ab('arm_' + sd, .75 * raise_); P.sw('fore_' + sd, .15 * raise_ + .8 * (1 - raise_))
    P.lean('head', -.25 * raise_); P.lean('chest', -.1 * raise_)
    if k == 'goron':
        P.sw('fore_l', 1.0 * raise_ + .1); P.sw('fore_r', 1.0 * raise_ + .1)
    P.sw('shin_l', -.5 * hop); P.sw('shin_r', -.5 * hop); P.sw('leg_l', -.25 * hop); P.sw('leg_r', -.25 * hop)
    P.rotw('tail1', (1, 0, 0), .4 * hop); P.rotw('tail2', (1, 0, 0), .6 * hop)


def pose_rest(P, k, ph):
    """Waiting for someone to come by: a Goron curls into his rock ball, the others sit on the ground."""
    s = math.sin(ph)
    d = DIM[k]
    if k == 'goron':       # tucked into a ball, the studded back on top, breathing slowly
        P.lean('root', .55); P.lean('chest', .75 + .03 * s); P.lean('head', .6)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 2.15); P.sw('shin_' + sd, -2.55); P.sw('foot_' + sd, .6); P.ab('leg_' + sd, .1)
            P.sw('arm_' + sd, 1.0); P.ab('arm_' + sd, -.1); P.sw('fore_' + sd, 2.0); P.ab('fore_' + sd, -.8)
        P.move('root', GORON_BALL_SHIFT)
        return
    drop = d['hipZ'] - .055 * d['H']
    P.move('root', (0, 0, -drop + .003 * s))
    P.lean('head', .06 * s - .05); P.tw('head', .3 * math.sin(ph))
    if k == 'gerudo':      # legs out, leaning back on her hands
        P.lean('chest', -.35 + .02 * s)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 1.45); P.sw('shin_' + sd, -.25 if sd == 'l' else -.9); P.sw('arm_' + sd, -.55); P.ab('arm_' + sd, .25)
        P.lean('head', .3)
    else:                  # knees up, arms around them
        P.lean('chest', .15 + .02 * s)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 2.05); P.sw('shin_' + sd, -2.1); P.ab('leg_' + sd, .15)
            P.sw('arm_' + sd, .95); P.sw('fore_' + sd, .75); P.ab('fore_' + sd, -.4)
        P.rotw('tail1', (1, 0, 0), .05 * s)


def pose_rise(P, k, u):
    """From resting to the ready stance; a Goron uncurls with a big stretch."""
    a = P.snapshot(lambda: pose_rest(P, k, 0.0))
    b = P.snapshot(lambda: pose_idle(P, k, 0.0))
    e = sm(u / .85)
    P.blend(a, b, e)
    bump = math.sin(min(1.0, u / .85) * math.pi)
    if k == 'goron':
        P.ab('arm_l', .9 * bump); P.ab('arm_r', .9 * bump); P.lean('chest', -.25 * bump); P.lean('head', -.3 * bump)
        P.move('root', (0, 0, .04 * bump))
    else:
        P.lean('chest', .2 * bump)


def pose_dance(P, k, ph):
    """The dance the ally does when its owner emotes or wins: four beats a loop, each kind in its own way."""
    beat = abs(math.sin(ph * 2))
    side = math.sin(ph * 2)
    ready(P, k, arms=False)
    if k == 'kokiri':      # a jig: hopping from foot to foot, kicking forward, arms swinging high out to the sides, the hat flopping
        P.move('root', (0, 0, .035 * beat))
        kl, kr = max(0.0, side), max(0.0, -side)
        P.sw('leg_l', .7 * kl - .1 * kr); P.sw('shin_l', -.3 * kl - .5 * kr)
        P.sw('leg_r', .7 * kr - .1 * kl); P.sw('shin_r', -.3 * kr - .5 * kl)
        for sd, o in (('l', 0.0), ('r', math.pi)):
            w = math.sin(ph * 2 + o)
            P.ab('arm_' + sd, 1.2 + .35 * w); P.sw('arm_' + sd, .3); P.ab('fore_' + sd, .4 + .5 * w)
        P.roll('chest', .12 * side); P.tw('head', .25 * math.sin(ph)); P.roll('head', -.15 * side)
        P.rotw('tail1', (0, 0, 1), .3 * side); P.rotw('tail2', (0, 0, 1), .5 * math.sin(ph * 2 - .8)); P.rotw('tail2', (1, 0, 0), .3 * beat)
    elif k == 'goron':     # Darunia's dance: a wide squat, stamping feet, fists swinging from side to side, the belly bouncing
        P.move('root', (0, 0, -.03 + .025 * beat))
        P.ab('leg_l', .2); P.ab('leg_r', .2)
        sl, sr = max(0.0, side), max(0.0, -side)
        P.sw('leg_l', .35 * sl); P.sw('shin_l', -.5 * sl); P.sw('leg_r', .35 * sr); P.sw('shin_r', -.5 * sr)
        P.roll('chest', .15 * side); P.roll('hips', -.06 * side); P.tw('chest', .2 * side)
        for sd, sg in (('l', -1.0), ('r', 1.0)):
            P.ab('arm_' + sd, .5 + .35 * side * sg); P.sw('arm_' + sd, .7); P.sw('fore_' + sd, 1.5)
        P.lean('head', -.1); P.roll('head', .2 * side)
    elif k == 'zora':      # flowing: hips swaying, arms waving overhead like water, the head-tail swishing
        sway = math.sin(ph)
        P.roll('hips', .12 * sway); P.move('hips', (.03 * sway, 0, 0)); P.roll('chest', -.15 * sway); P.tw('chest', .25 * math.sin(ph + .5))
        for sd, o in (('l', 0.0), ('r', math.pi / 2)):
            P.sw('arm_' + sd, 2.0 + .4 * math.sin(ph * 2 + o)); P.ab('arm_' + sd, 1.05 + .3 * math.cos(ph * 2 + o))
            P.sw('fore_' + sd, .6 + .4 * math.sin(ph * 2 + o + 1))
        P.move('root', (0, 0, .012 * math.sin(ph * 2)))
        P.sw('leg_l', .15 * sway); P.sw('leg_r', -.15 * sway); P.sw('shin_l', -.2 * max(0.0, sway)); P.sw('shin_r', -.2 * max(0.0, -sway))
        P.rotw('tail1', (0, 0, 1), .3 * sway); P.rotw('tail2', (0, 0, 1), .5 * math.sin(ph - .9)); P.rotw('tail2', (1, 0, 0), .2 * math.sin(ph * 2))
    else:                  # gerudo: hips swaying with a step, one hand on the hip and the other twirling overhead, then the other way
        P.roll('hips', .14 * side); P.move('hips', (.035 * side, 0, 0)); P.roll('chest', -.1 * side)
        P.sw('leg_l', .12 * max(0.0, side)); P.sw('shin_l', -.25 * max(0.0, side)); P.sw('leg_r', .12 * max(0.0, -side)); P.sw('shin_r', -.25 * max(0.0, -side))
        a = .5 + .5 * math.sin(ph)
        twirl = math.sin(ph * 4)
        for sd, up in (('r', a), ('l', 1 - a)):
            P.sw('arm_' + sd, 2.6 * up); P.ab('arm_' + sd, .4 * up + .55 * (1 - up))
            P.sw('fore_' + sd, (.5 + .3 * twirl) * up); P.ab('fore_' + sd, -1.15 * (1 - up))
        P.roll('head', -.12 * side); P.tw('head', .2 * math.sin(ph))
        P.rotw('tail1', (0, 0, 1), .35 * side); P.rotw('tail2', (0, 0, 1), .55 * math.sin(ph * 2 - .9))


def pose_for(P, kind, name, f, n, loops):
    P.reset()
    t = f / n if loops else f / max(1, n - 1)
    ph = t * TAU
    if name == 'idle': pose_idle(P, kind, ph)
    elif name == 'walk': pose_walk(P, kind, ph)
    elif name == 'invite': pose_invite(P, kind, ph)
    elif name == 'attack': pose_attack(P, kind, t, 0)
    elif name == 'attack2': pose_attack(P, kind, t, 1)
    elif name == 'heal': pose_heal(P, kind, t)
    elif name == 'hurt': pose_hurt(P, kind, t)
    elif name == 'cheer': pose_cheer(P, kind, t)
    elif name == 'rest': pose_rest(P, kind, ph)
    elif name == 'rise': pose_rise(P, kind, t)
    elif name == 'dance': pose_dance(P, kind, ph)


def build_rig(kind, mesh):
    arm = bpy.data.armatures.new('%sSkeleton' % kind.capitalize())
    rig = bpy.data.objects.new('%sRig' % kind.capitalize(), arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    mesh.select_set(False)
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    table = bone_table(kind)
    for n in BONES:
        head, tail, parent = table[n]
        b = arm.edit_bones.new(n)
        b.head, b.tail = head, tail
        if parent:
            b.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    mod = mesh.modifiers.new('skin', 'ARMATURE')
    mod.object = rig
    mesh.parent = rig
    return rig


def animate(rig, kind):
    ctl = Ctl(rig)
    rig.animation_data_create()
    for name, seconds, loops, fps in CLIPS:
        act = bpy.data.actions.new(name)
        act['ally_fps'] = fps
        rig.animation_data.action = act
        n = frames_of(seconds, loops, fps)
        for f in range(n):
            pose_for(ctl, kind, name, f, n, loops)
            ctl.apply(rig)
            for pb in rig.pose.bones:
                pb.keyframe_insert('rotation_quaternion', frame=f)
                pb.keyframe_insert('location', frame=f)
        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'
        tr = rig.animation_data.nla_tracks.new()
        tr.name = name
        tr.strips.new(name, 0, act)
        tr.mute = True
    rig.animation_data.action = bpy.data.actions['idle']


def texture_paths(kind):
    return OUT / ('%s_body.png' % kind), [OUT / ('%s_face_%s.png' % (kind, e)) for e in FACES]


def save_png(arr, path):
    from PIL import Image
    Image.fromarray((np.clip(arr, 0, 1) * 255 + .5).astype(np.uint8)).save(path)


def build_mesh(kind):
    me = bpy.data.meshes.new(kind.capitalize())
    me.from_pydata([tuple(v) for v in G.V], [], G.F)
    me.update()
    uvl = me.uv_layers.new()
    for poly, uvs, t, s in zip(me.polygons, G.FUV, G.FT, G.FS):
        poly.material_index = t
        poly.use_smooth = s
        for li, uv in zip(poly.loop_indices, uvs):
            uvl.data[li].uv = uv
    obj = bpy.data.objects.new(kind.capitalize(), me)
    bpy.context.collection.objects.link(obj)
    groups = {n: obj.vertex_groups.new(name=n) for n in BONES}
    for i, w in enumerate(G.W):
        for b, x in w.items():
            if x > .001:
                groups[b].add([i], x, 'REPLACE')
    body, faces = texture_paths(kind)
    OUT.mkdir(parents=True, exist_ok=True)
    save_png(G.atlas.paint(), body)
    for e, path in zip(FACES, faces):
        save_png(PA.paint_face(kind, e, face_palette(kind)), path)
    for path in (body, faces[0]):
        mat = bpy.data.materials.new(path.stem)
        mat.use_nodes = True
        node = mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = bpy.data.images.load(str(path))
        bs = mat.node_tree.nodes.get('Principled BSDF')
        mat.node_tree.links.new(node.outputs['Color'], bs.inputs['Base Color'])
        bs.inputs['Roughness'].default_value = 1.0
        me.materials.append(mat)
    return obj


def build_kind(kind):
    global G, FRAME
    bpy.ops.wm.read_factory_settings(use_empty=True)
    FRAME = Matrix.Identity(4)
    G = Geo(kind)
    BUILDERS[kind]()
    mesh = build_mesh(kind)
    rig = build_rig(kind, mesh)
    animate(rig, kind)
    bpy.context.scene.render.fps = 24
    bpy.context.scene['art_notes'] = ('Original stylised %s ally (OoT x Majora x Fortnite); 256x256 painted atlas + 128x64 face in 3 expressions; '
                                      '18 bones; %d clips.' % (kind, len(CLIPS)))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / ('%s.blend' % kind)))
    try:
        for tr in rig.animation_data.nla_tracks:
            tr.mute = False
        rig.animation_data.action = None
        bpy.ops.export_scene.gltf(filepath=str(OUT / ('%s.glb' % kind)), export_format='GLB', export_animations=True,
                                  export_animation_mode='NLA_TRACKS', export_yup=True)
    except Exception as e:   # the glTF add-on is optional; the .blend is the source of truth
        print('glb skipped:', e)
    print('built', kind, len(mesh.data.vertices), 'vertices', len(mesh.data.polygons), 'faces', len(G.atlas.index), 'materials')


if __name__ == '__main__':
    only = [a for a in sys.argv[1:] if a in KINDS]
    for k in (only or KINDS):
        build_kind(k)
