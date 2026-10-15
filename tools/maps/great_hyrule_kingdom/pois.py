"""Every place in Great Hyrule Kingdom, built from the kit (kit.py, sized from adult Link). Positions come from layout.py. Nothing on this map
is scattered at random when a match starts: every building, bridge, tree and rock is laid here, once, and baked into the map.

The ground is Tee's fantasy landscape model (cleaned, see prepare_landscape.py); the places are Hyrule's (Ocarina of Time, Twilight Princess,
Breath of the Wild), every shape here the project's own low-poly OoT-style geometry. Zero build: cover comes from walls, ruins, rocks, hedges,
trees, upper floors and rooftops, never from building pieces."""
import math
import numpy as np
from kit import *
from geom import rect
from layout import *
import terrain

def face_yaw(x, z, tx, tz):
    """The yaw that turns a building's front (local +z) towards (tx, tz)."""
    return math.atan2(-(tx - x), (tz - z))

def rng_for(name):
    return np.random.default_rng(sum(ord(c) * (i + 1) for i, c in enumerate(name)))

TOWN = dict(wall='plaster', base='town_stone', trim='timber', floor='planks')
def style(**kw):
    d = dict(TOWN); d.update(kw); return HouseStyle(**d)

def region(w, name):
    x, z = POI_AT[name]
    w.markers.append({'name': name, 'kind': 'region', 'x': x, 'z': z})
    return x, z

def chest(w, x, y, z, name):
    w.loot.append((x, y, z, name))

def lamp(w, x, z, y=None):
    y = H(x, z) if y is None else y
    w.cylinder(x, z, 5, y, y + 150, 'iron', n=6, col='none')
    w.box(x, y + 150, z, 22, 26, 22, 'window_lit', col='none')
    w.hip(x, z, 22, 22, y + 176, 12, 'iron', overhang=4, col=None)

def signpost(w, x, z, yaw, y=None):
    """A wooden signpost at a junction: a post and two boards (drawn; you walk round it)."""
    y = H(x, z) if y is None else y
    w.box(x, y - 6, z, 10, 150, 10, 'timber', yaw, col='none')
    w.box(x, y + 96, z, 90, 26, 6, 'planks', yaw, col='none')
    w.box(x, y + 126, z, 70, 22, 6, 'planks', yaw + 0.6, col='none')

def bench(w, x, z, yaw, y=None):
    y = H(x, z) if y is None else y
    w.box(x, y + 20, z, 90, 6, 30, 'planks', yaw, col='prop', surf='wood')
    f = Frame(x, z, yaw)
    for sx in (-1, 1):
        px, pz = f.p(sx * 36, 0)
        w.box(px, y, pz, 8, 20, 28, 'timber', yaw, col='none')

def stall(w, x, z, yaw, k, roof='canvas_stripe'):
    y = H(x, z)
    f = Frame(x, z, yaw)
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = f.p(sx * 60, sz * 40)
            w.box(px, y, pz, 7, 120, 7, 'timber', yaw, col='none')
    w.hip(x, z, 136, 96, y + 120, 32, roof if k % 2 else 'canvas_blue', yaw, overhang=10, col=None)
    w.box(x, y, z, 120, 44, 44, 'planks', yaw, col='prop', surf='wood')
    px, pz = f.p(28, 0); pot(w, px, pz, y + 44, 9)
    px, pz = f.p(-44, 52); crate(w, px, pz, y, 34, yaw)

def stone_ramp_road(w, pts, width=200, mat='cobble', parapet='castle_stone', rail=True):
    """A built road of stone ramps along (x, y, z) points: each leg is a beam (solid, a walkway for the bots) with low parapets.
    Legs must rise at most MAX_SLOPE per unit run."""
    for a, b in zip(pts[:-1], pts[1:]):
        L = math.hypot(b[0] - a[0], b[2] - a[2])
        assert abs(b[1] - a[1]) / max(1, L) <= MAX_SLOPE + 1e-6, ('road leg too steep', a, b, abs(b[1] - a[1]) / L)
        beam(w, a, b, width, 40, mat, surf='stone')
        if rail:
            dx, dz = (b[0] - a[0]) / L, (b[2] - a[2]) / L; nx, nz = -dz, dx
            for side in (-1, 1):
                ox, oz = nx * side * (width / 2 - 10), nz * side * (width / 2 - 10)
                beam(w, (a[0] + ox, a[1] + 50, a[2] + oz), (b[0] + ox, b[1] + 50, b[2] + oz), 20, 50, parapet, surf='stone', block=True)
        w.box(b[0], b[1], b[2], 30, 70, 30, parapet, col='none')

def sloped_bridge(w, a, b, width=200, deck_mat='cobble', wall_mat='castle_stone', piers=2, name='bridge', rope=False):
    """A bridge whose deck runs straight from a = (x, y, z) to b (sloping if the ends differ, at most MAX_SLOPE): parapets or rope rails, piers
    down to the sea bed. The deck is a walkway for the bots."""
    ax, ay, az = a; bx, by, bz = b
    L = math.hypot(bx - ax, bz - az); ux, uz = (bx - ax) / L, (bz - az) / L
    assert abs(by - ay) / L <= MAX_SLOPE, ('bridge too steep', name, abs(by - ay) / L)
    thick = 24 if rope else 46
    beam(w, a, b, width, thick, deck_mat, surf='wood' if rope else 'stone')
    nx, nz = -uz, ux
    for side in (-1, 1):
        ox, oz = nx * side * (width / 2 - 12), nz * side * (width / 2 - 12)
        if rope:
            for u in np.linspace(0, 1, int(L / 160) + 2):
                px, pz, py = ax + (bx - ax) * u + ox, az + (bz - az) * u + oz, ay + (by - ay) * u
                w.box(px, py, pz, 10, 90, 10, 'timber', col='none')
            beam(w, (ax + ox, ay + 90, az + oz), (bx + ox, by + 90, bz + oz), 6, 6, 'timber', col='none', block=False)
        else:
            straight = abs(bx - ax) < 1 or abs(bz - az) < 1
            segs = 1 if straight else max(1, int(L / 120))      # short pieces: a blocker is a bounding box, and one long diagonal box would close the deck
            for k in range(segs):
                u0, u1 = k / segs, (k + 1) / segs
                beam(w, (ax + (bx - ax) * u0 + ox, ay + (by - ay) * u0 + 56, az + (bz - az) * u0 + oz), (ax + (bx - ax) * u1 + ox, ay + (by - ay) * u1 + 56, az + (bz - az) * u1 + oz), 22, 56, wall_mat, block=True)
            for u in np.linspace(0.08, 0.92, 7):
                px, pz, py = ax + (bx - ax) * u + ox, az + (bz - az) * u + oz, ay + (by - ay) * u
                w.box(px, py + 50, pz, 30, 26, 30, wall_mat, col='none')
    for k in range(piers):
        u = (k + 1) / (piers + 1)
        px, pz, py = ax + (bx - ax) * u, az + (bz - az) * u, ay + (by - ay) * u
        bed = H(px, pz)
        if rope:
            for side in (-1, 1):
                w.cylinder(px + nx * side * (width / 2 - 20), pz + nz * side * (width / 2 - 20), 12, bed - 20, py - thick, 'timber', n=6, col='none', top=False)
        else:
            w.box(px, bed - 40, pz, 90, py - thick - bed + 40, width - 30, wall_mat, math.atan2(uz, ux), surf='stone')
    return L

def deck_over_sea(w, x, z, yaw, L=760, W=260, name='deck'):
    """A diving deck: planks cantilevered out over the cliff from the ground at (x, z), on posts, with rails and a chest."""
    y = H(x, z) + 10
    f = Frame(x, z, yaw)
    cx, cz = f.p(0, L / 2 - 40)
    w.box(cx, y - 16, cz, W, 16, L, 'planks', yaw, surf='wood', hookshot=True)
    a = f.p(0, -40); b = f.p(0, L - 40)
    w.walkways.append((a[0], a[1], b[0], b[1], W / 2, y, y))
    for lx in (-W / 2 + 14, W / 2 - 14):
        for lz in np.linspace(L / 4, L - 60, 3):
            px, pz = f.p(lx, lz)
            w.cylinder(px, pz, 10, min(H(px, pz), y - 200) - 10, y - 16, 'timber', n=6, col='none', top=False)
            w.box(px, y, pz, 8, 70, 8, 'timber', yaw, col='none')
    for lz in (L / 4, L - 60):
        px, pz = f.p(0, lz)
        w.box(px, y + 60, pz, W - 20, 8, 8, 'timber', yaw, col='none')
    ex, ez = f.p(0, L - 50)
    w.box(ex, y, ez, W - 28, 8, 8, 'timber', yaw, col='none')
    w.ramp(*f.p(0, -40 - 60), *f.p(0, -40), W - 40, H(*f.p(0, -100)) - 2, y, 'planks', surf='wood', base=H(*f.p(0, -100)) - 40)
    w.walkways.append((*f.p(0, -100), *f.p(0, -40), (W - 40) / 2, H(*f.p(0, -100)) - 2, y))
    px, pz = f.p(0, L - 120)
    chest(w, px, y, pz, name)
    return y

def dead_tree(w, x, z, s=1.0, seed=0):
    y = H(x, z) - 10
    rng = np.random.default_rng(seed)
    w.cylinder(x, z, 20 * s, y, y + 200 * s, 'bark', n=6, col='none', top=False, r1=12 * s)
    w.obstacles.append((x - 20, x + 20, z - 20, z + 20))
    for k in range(3):
        a = rng.uniform(0, 2 * math.pi) + k * 2.1
        bx, bz = x + math.cos(a) * 80 * s, z + math.sin(a) * 80 * s
        w.solid([(x - 8 * s, y + 150 * s, z), (x + 8 * s, y + 150 * s, z), (x, y + 170 * s, z + 8 * s), (bx, y + 260 * s, bz)], [(0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)], 'bark', col='none')

def pumpkin_plant(w, x, z, s=1.0, seed=0):
    y = H(x, z)
    w.sphere(x, y + 22 * s, z, 30 * s, 24 * s, 30 * s, 'pumpkin', rings=3, segs=7, seed=seed, jitter=0.08)
    w.cylinder(x, z, 4, y + 42 * s, y + 56 * s, 'bark', n=4, col='none')

def stone_bridge(w, a, b, width=200, deck_mat='cobble', wall_mat='castle_stone', piers=2, towers=False, arches=True, name='bridge'):
    """A stone bridge (level deck between two approach ramps if the ends differ), parapets, piers in the water and drawn arches."""
    ax, ay, az = a; bx, by, bz = b
    L = math.hypot(bx - ax, bz - az); ux, uz = (bx - ax) / L, (bz - az) / L
    y = max(ay, by)
    p0 = (ax + ux * 150, y, az + uz * 150); p1 = (bx - ux * 150, y, bz - uz * 150)
    beam(w, a, p0, width, 46, deck_mat); beam(w, p0, p1, width, 46, deck_mat); beam(w, p1, b, width, 46, deck_mat)
    nx, nz = -uz, ux
    for side in (-1, 1):
        ox, oz = nx * side * (width / 2 - 12), nz * side * (width / 2 - 12)
        beam(w, (p0[0] + ox, y + 56, p0[2] + oz), (p1[0] + ox, y + 56, p1[2] + oz), 22, 56, wall_mat, block=True)
        for u in np.linspace(0.08, 0.92, 7):
            px, pz = p0[0] + (p1[0] - p0[0]) * u + ox, p0[2] + (p1[2] - p0[2]) * u + oz
            w.box(px, y + 50, pz, 30, 26, 30, wall_mat, col='none')
    for k in range(piers):
        u = (k + 1) / (piers + 1)
        px, pz = p0[0] + (p1[0] - p0[0]) * u, p0[2] + (p1[2] - p0[2]) * u
        bed = H(px, pz)
        w.box(px, bed - 40, pz, 90, y - 46 - bed + 40, width - 30, wall_mat, math.atan2(uz, ux), surf='stone')
    if arches:
        LL = math.hypot(p1[0] - p0[0], p1[2] - p0[2])
        spans = [(k / (piers + 1), (k + 1) / (piers + 1)) for k in range(piers + 1)]
        for side in (-1, 1):
            ox, oz = nx * side * (width / 2 - 16), nz * side * (width / 2 - 16)
            for t0, t1 in spans:
                for k in range(6):
                    ua, ub = t0 + (t1 - t0) * k / 6, t0 + (t1 - t0) * (k + 1) / 6
                    xa, za = p0[0] + (p1[0] - p0[0]) * ua + ox, p0[2] + (p1[2] - p0[2]) * ua + oz
                    xb, zb = p0[0] + (p1[0] - p0[0]) * ub + ox, p0[2] + (p1[2] - p0[2]) * ub + oz
                    da = 40 + (1 - math.sin(math.pi * (ua - t0) / (t1 - t0))) * 150; db = 40 + (1 - math.sin(math.pi * (ub - t0) / (t1 - t0))) * 150
                    w.face([(xa, y - 46 - da, za), (xb, y - 46 - db, zb), (xb, y - 46, zb), (xa, y - 46, za)], wall_mat, double=True)
    if towers:
        for end, s in ((p0, -1), (p1, 1)):
            for side in (-1, 1):
                tx, tz = end[0] + nx * side * (width / 2 + 60), end[2] + nz * side * (width / 2 + 60)
                hgt = 300 if (s < 0) == (side < 0) else 200
                foot = min(y - 40, H(tx, tz) - 40)
                w.box(tx, foot, tz, 100, hgt + (y - 40 - foot), 100, 'moss_stone', math.atan2(uz, ux), surf='stone', climb=(0,))
                w.box(tx, y - 40 + hgt, tz, 76, 36, 60, 'moss_stone', col='none')
    return p0, p1, y


def tent(w, x, z, k):
    y = H(x, z)
    f = Frame(x, z, k * 0.7)
    for sx in (-1, 1):
        px, pz = f.p(sx * 110, 0)
        w.box(px, y, pz, 9, 150, 9, 'timber', k * 0.7, col='none')
    w.gable(x, z, 220, 240, y, 150, 'roof_red' if k % 2 else 'cloth', k * 0.7, overhang=0, col=None)


def palm(w, x, z, s=1.0, seed=0):
    y = H(x, z) - 10
    old = w.group; w.group = 'Foliage'
    rng = np.random.default_rng(seed)
    lean = rng.uniform(-0.25, 0.25)
    hgt = 300 * s
    w.cylinder(x, z, 14 * s, y, y + hgt * 0.6, 'bark', n=6, col='none', top=False, r1=11 * s)
    w.cylinder(x + lean * hgt * 0.3, z, 11 * s, y + hgt * 0.6, y + hgt, 'bark', n=6, col='none', top=False, r1=9 * s)
    w.props.append((x - 14, y, z - 14, x + 14, y + hgt, z + 14, 'palm'))
    w.obstacles.append((x - 14, x + 14, z - 14, z + 14))
    tx, tz = x + lean * hgt * 0.3, z
    for k in range(7):
        a = k * 2 * math.pi / 7 + rng.uniform(-.2, .2)
        ex, ez = tx + math.cos(a) * 150 * s, tz + math.sin(a) * 150 * s
        w.face([(tx, y + hgt, tz), (tx + math.cos(a + .35) * 50 * s, y + hgt + 30 * s, tz + math.sin(a + .35) * 50 * s), (ex, y + hgt - 70 * s, ez),
                (tx + math.cos(a - .35) * 50 * s, y + hgt + 30 * s, tz + math.sin(a - .35) * 50 * s)], 'leaves', double=True)
    w.group = old


def colossus(w, x, z):
    y = H(x, z)
    w.far = True
    w.box(x, y - 40, z, 420, 150, 300, 'sandstone', surf='sand')                       # plinth
    w.box(x, y + 110, z + 20, 340, 100, 240, 'sandstone', surf='sand')                  # lap: a platform 210 up
    w.box(x, y + 110, z - 120, 290, 420, 130, 'sandstone', surf='sand', climb=(2,))    # torso, its front climbable
    for sx in (-1, 1):
        w.box(x + sx * 185, y + 300, z - 80, 90, 210, 110, 'sandstone', col='none')     # shoulders
        w.box(x + sx * 170, y + 220, z + 50, 70, 70, 190, 'sandstone', col='none')      # forearms on the knees
        w.box(x + sx * 100, y - 40, z + 140, 100, 150, 100, 'sandstone', col='none')    # shins
    w.box(x, y + 530, z - 120, 170, 180, 160, 'sandstone', col='none')                   # head
    w.box(x, y + 710, z - 120, 210, 30, 180, 'sandstone', col='none')                    # headdress
    w.box(x - 55, y + 600, z - 39, 32, 24, 4, 'iron', col='none'); w.box(x + 55, y + 600, z - 39, 32, 24, 4, 'iron', col='none')
    w.far = False
    chest(w, x, y + 210, z + 60, 'colossus lap')

def poisson(rng, lo, hi, r, n_try):
    pts = []
    for _ in range(n_try * 4):
        if len(pts) >= n_try: break
        x = rng.uniform(lo[0], hi[0]); z = rng.uniform(lo[1], hi[1])
        if all((x - px) ** 2 + (z - pz) ** 2 > r * r for px, pz in pts): pts.append((x, z))
    return pts

# ---- shared pieces ------------------------------------------------------------------------------------------------------------------
def deck(w, x, z, W, D, y, yaw=0.0, mat='planks', post='timber', ramp=True, ramp_w=90, name=None, rails=True):
    """(ramp: True for the local +z side, or 'x' for the local +x side.)"""
    """A raised wooden platform on posts with a ramp down to the ground off its local +z side (a lookout, a treehouse floor, a scaffold)."""
    f = Frame(x, z, yaw)
    w.box(x, y - 20, z, W, 20, D, mat, yaw, surf='wood', hookshot=True)
    a = f.p(-W / 2, 0); b = f.p(W / 2, 0)
    w.walkways.append((a[0], a[1], b[0], b[1], D / 2, y, y))
    w.buildings.append((x, z, W / 2, D / 2, y, yaw))
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = f.p(sx * (W / 2 - 12), sz * (D / 2 - 12))
            w.cylinder(px, pz, 10, H(px, pz) - 20, y - 20, post, n=6, col='none', top=False)
    if rails:
        for lx, lz, sx, sz in [(0, -D / 2 + 4, W - 10, 8), (-W / 2 + 4, 0, 8, D - 10), (W / 2 - 4, 0, 8, D - 10)]:
            px, pz = f.p(lx, lz)
            w.box(px, y + 56, pz, sx, 8, sz, post, yaw, col='none')
    if ramp:
        side = (lambda d: f.p(d, 0)) if ramp == 'x' else (lambda d: f.p(0, d))
        half = W / 2 if ramp == 'x' else D / 2
        run = 200.0
        for _ in range(4):
            gx, gz = side(half + run); g = H(gx, gz)
            run = max(80.0, (y - g) / 0.5)
        gx, gz = side(half + run); g = H(gx, gz)
        ex, ez = side(half)
        w.ramp(gx, gz, ex, ez, ramp_w, g - 4, y, mat, surf='wood', base=g - 60)
        w.walkways.append((gx, gz, ex, ez, ramp_w / 2, g - 4, y))
    if name: chest(w, x, y, z, name)
    return y

def waterfall(w, x, z, ux, uz, width, top, bottom, lean=70):
    """Sheets of falling water across a step (flowing along (ux, uz)), with foam where they land (drawn only)."""
    nx, nz = -uz, ux
    old = w.far; w.far = True
    n = max(2, int(width / 90))
    for k in range(n):
        a = -width / 2 + width * k / n; b = -width / 2 + width * (k + 1) / n
        w.face([(x + nx * a + ux * lean, bottom, z + nz * a + uz * lean), (x + nx * b + ux * lean, bottom, z + nz * b + uz * lean),
                (x + nx * b, top, z + nz * b), (x + nx * a, top, z + nz * a)], 'water', double=True, uvs=[(0, 4), (1, 4), (1, 0), (0, 0)])
    w.far = old
    for k in range(3):
        u = -width / 3 + k * width / 3
        w.cylinder(x + nx * u + ux * (lean + 40), z + nz * u + uz * (lean + 40), width / 3, bottom - 3, bottom + 5, 'ice', n=10, col='none')

def broken_wall(w, a, b, hgt, mat='moss_stone', t=44, rng=None, gaps=1):
    """A ruined wall from a to b in pieces of different heights, with gaps to run through; every piece climbable ivy."""
    rng = rng or np.random.default_rng(int(a[0] * 7 + b[1] * 3) & 0xffff)
    L = math.hypot(b[0] - a[0], b[1] - a[1]); ang = math.atan2(b[1] - a[1], b[0] - a[0])
    n = max(2, int(L / 180))
    skip = set(rng.choice(n, size=min(gaps, n - 1), replace=False)) if gaps else set()
    for k in range(n):
        if k in skip: continue
        u0, u1 = k / n, (k + 1) / n
        mx, mz = a[0] + (b[0] - a[0]) * (u0 + u1) / 2, a[1] + (b[1] - a[1]) * (u0 + u1) / 2
        hh = hgt * rng.uniform(0.45, 1.0)
        y = H(mx, mz)
        w.box(mx, y - 30, mz, L / n + 2, hh + 30, t, mat, ang, surf='stone', climb=(0, 2))

def arch(w, x, z, yaw, span=220, hgt=300, mat='moss_stone'):
    """Two square pillars and a lintel: a gateway to run through (the lintel is a ledge to stand on)."""
    f = Frame(x, z, yaw)
    y = H(x, z)
    for sx in (-1, 1):
        px, pz = f.p(sx * (span / 2 + 30), 0)
        w.box(px, H(px, pz) - 30, pz, 60, y + hgt - H(px, pz) + 30, 60, mat, yaw, surf='stone', climb=(0,))
    w.box(x, y + hgt, z, span + 140, 50, 70, mat, yaw, surf='stone')

def gravestones(w, x, z, cols, rows, yaw=0.0, seed=0, dx=130, dz=120):
    rng = np.random.default_rng(seed)
    f = Frame(x, z, yaw)
    for i in range(cols):
        for j in range(rows):
            gx, gz = f.p((i - (cols - 1) / 2) * dx + rng.uniform(-15, 15), (j - (rows - 1) / 2) * dz)
            w.box(gx, H(gx, gz) - 10, gz, 50, 76 + rng.uniform(-14, 14), 16, 'moss_stone', yaw + rng.uniform(-.15, .15), col='prop', surf='stone')

def hay_bale(w, x, z, k=0, y=None):
    y = H(x, z) if y is None else y
    w.cylinder(x, z, 46, y - 4, y + 70, 'hay', n=8, surf='wood', col='prop')

def crates(w, x, z, k=0, n=3, yaw=0.0):
    f = Frame(x, z, yaw)
    for q in range(n):
        px, pz = f.p((q % 2) * 44, (q // 2) * 44)
        crate(w, px, pz, H(px, pz), 38, yaw + 0.2 * q)

def snowman(w, x, z):
    y = H(x, z)
    w.sphere(x, y + 30, z, 36, 32, 36, 'snow', rings=3, segs=7)
    w.sphere(x, y + 82, z, 24, 22, 24, 'snow', rings=3, segs=7)
    w.props.append((x - 36, y, z - 36, x + 36, y + 100, z + 36, 'snowman'))

def watch_tower(w, x, z, size, height, mat='castle_stone', roof='roof_red', yaw=0.0, name='watchtower', cap=True):
    """A solid stone tower with ivy up every face and an open look-out on top (a chest, a roof on four posts): cheap collision, a climb."""
    f = Frame(x, z, yaw)
    lo, hi = ground_range(x, z, size, size, yaw)
    top = hi + height
    w.prism(f.outline(rect(0, 0, size, size)), lo - 40, top, mat, surf='stone', climb=(0, 1, 2, 3))
    w.buildings.append((x, z, size / 2, size / 2, top, yaw))
    old = w.far; w.far = True
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = f.p(sx * (size / 2 - 14), sz * (size / 2 - 14))
            w.box(px, top, pz, 22, 40, 22, mat, yaw, col='none')
            if cap: w.box(px, top + 40, pz, 12, 150, 12, 'timber', yaw, col='none')
    if cap: w.hip(x, z, size + 20, size + 20, top + 190, size * 0.5, roof, yaw, overhang=20, col=None)
    w.far = old
    chest(w, x, top, z, name + ' top')
    return top

def gate_posts(w, cx, cz, r, a, wd, mat='town_stone', hgt=260, lamps=True):
    for s in (-1, 1):
        gx, gz = cx + r * math.cos(a) + s * (wd / 2 + 30) * -math.sin(a), cz + r * math.sin(a) + s * (wd / 2 + 30) * math.cos(a)
        gy = H(gx, gz)
        w.box(gx, gy - 20, gz, 50, hgt, 50, mat, a, col='none')
        if lamps: lamp(w, gx, gz, gy + hgt - 20)

def sector_spots(cx, cz, gates, r, per=None):
    """Spots on a ring between its gates (angles): one per sector, two in a wide one, none in a narrow one."""
    gs = sorted(a % (2 * math.pi) for a, wd in gates)
    out = []
    for i, a in enumerate(gs):
        b = gs[(i + 1) % len(gs)] + (2 * math.pi if i == len(gs) - 1 else 0)
        span = b - a
        n = 0 if span < 0.6 else max(1, int(span * r / 460) - 1)
        for k in range(n):
            t = a + span * (k + 1) / (n + 1)
            out.append((cx + r * math.cos(t), cz + r * math.sin(t), t))
    return out

# ---- Castle Town, the hot drop ------------------------------------------------------------------------------------------------------
def castle_town(w):
    cx, cz = region(w, 'Castle Town')
    y = H(cx, cz)
    ang = lambda tx, tz: math.atan2(tz - cz, tx - cx)
    # five gates: the bridge road (north), the docks (north west), the river (east), the castle (south) and the west gate (Lon Lon, the ruins)
    gates = [(ang(-100, -1700), 220), (ang(-900, -1500), 220), (ang(1200, -900), 220), (ang(-200, 100), 240), (2.74, 300)]
    ring_wall(w, cx, cz, 760, 44, y - 40, y + 200, 'town_stone', gaps=gates, n=14, climb=True)
    for a, wd in gates:
        gate_posts(w, cx, cz, 760, a, wd)
    # the market square: the fountain with the goddess column, stalls, lamps and benches
    ring_wall(w, cx, cz, 110, 26, y - 10, y + 44, 'castle_stone', n=12)
    w.cylinder(cx, cz, 96, y + 26, y + 30, 'water', n=12, col='none')
    w.cylinder(cx, cz, 22, y, y + 180, 'castle_stone', n=8, surf='stone')
    w.cylinder(cx, cz, 50, y + 150, y + 162, 'castle_stone', n=8, col='none')
    for k, a in enumerate(np.radians([35, 125, 215, 305])):
        stall(w, cx + 185 * math.cos(a), cz + 185 * math.sin(a), a + math.pi / 2, k)
    for k, a in enumerate(np.radians([80, 170, 260, 350])):
        chest(w, cx + 165 * math.cos(a), y, cz + 165 * math.sin(a), 'market square')
    for a, wd in gates:
        for s_ in (-1, 1):
            lamp(w, cx + 300 * math.cos(a) - s_ * 140 * math.sin(a), cz + 300 * math.sin(a) + s_ * 140 * math.cos(a))
    # the houses round the square, between the gates: shops below, rooms above, roof terraces to fight on
    spots = [(-0.83, 'roof_red', 'flat'), (0.80, 'roof_blue', 'hip'), (2.21, 'roof_orange', 'flat'), (-2.80, 'roof_green', 'gable')]
    for i, (a, roof, kind) in enumerate(spots):
        x, z = cx + 480 * math.cos(a), cz + 480 * math.sin(a)
        yaw = face_yaw(x, z, cx, cz)
        house(w, x, z, 380, 360, yaw, 2, style(roof=roof, roof_kind=kind, tint=(1.0, 0.96 - 0.02 * (i % 3), 0.9)), name=f'Castle Town house {i}',
              roof_rise=140, stair_landing=True, back_door=False)
        # crates and barrels in the lanes beside each house: low cover
        for side in (-1, 1):
            f = Frame(x, z, yaw)
            bx_, bz_ = f.p(side * 250, 40)
            crates(w, bx_, bz_, i, 3, yaw)
    # the guard tower in the north west lane
    a = -1.99
    tx, tz = cx + 520 * math.cos(a), cz + 520 * math.sin(a)
    spiral_tower(w, tx, tz, 260, 520, yaw=face_yaw(tx, tz, cx, cz), wall='town_stone', floor='cobble', roof='roof_blue', name='Castle Town guard tower', cap='spire')
    for a in np.radians([20, 200]):
        bench(w, cx + 330 * math.cos(a), cz + 330 * math.sin(a), a)
    # hedges in planters on the square (cover in the open)
    for a in np.radians([0, 90, 180, 270]):
        px, pz = cx + 380 * math.cos(a + 0.4), cz + 380 * math.sin(a + 0.4)
        w.box(px, y - 10, pz, 160, 50, 50, 'town_stone', a + 0.4 + math.pi / 2, surf='stone')
        bush(w, px, pz, 0.7, int(a * 10), 'leaves', y=y + 30)
    for a, wd in gates:
        sx, sz = cx + 900 * math.cos(a) + 140 * math.sin(a), cz + 900 * math.sin(a) - 140 * math.cos(a)
        signpost(w, sx, sz, a + math.pi / 2)

# ---- Hyrule Castle -----------------------------------------------------------------------------------------------------------------
def hyrule_castle(w):
    cx, cz = region(w, 'Hyrule Castle')
    yaw = face_yaw(cx, cz, -100, -800)          # the gate (local +z) looks down the road to Castle Town
    f = Frame(cx, cz, yaw)
    py = H(cx, cz)
    W, D, t = 1300, 1150, 70
    top = py + 230
    g = 200
    # curtain walls: back, the two sides (the west one ivy), and the front either side of the gate
    for lx, lz, sx, sz, climb in [(0, -D / 2 + t / 2, W, t, ()), (-W / 2 + t / 2, 0, t, D, (0, 1, 2, 3)), (W / 2 - t / 2, 0, t, D, ()),
                                  (-(W / 2 + g / 2) / 2, D / 2 - t / 2, W / 2 - g / 2, t, ()), ((W / 2 + g / 2) / 2, D / 2 - t / 2, W / 2 - g / 2, t, ())]:
        w.prism(f.outline(rect(lx, lz, sx, sz)), py - 60, top, 'castle_stone', surf='stone', climb=climb)
    # battlements (drawn)
    for (ax, az, bx, bz) in [(-W / 2, -D / 2, W / 2, -D / 2), (-W / 2, -D / 2, -W / 2, D / 2), (W / 2, -D / 2, W / 2, D / 2)]:
        L = math.hypot(bx - ax, bz - az); n = int(L / 70)
        for k in range(0, n, 2):
            u = (k + .5) / n
            px, pz = f.p(ax + (bx - ax) * u, az + (bz - az) * u)
            w.box(px, top, pz, 34 if az == bz else t, 40, t if az == bz else 34, 'castle_stone', yaw, col='none')
    # ramps up to the wall walk along the inside of the side walls
    for sx in (-1, 1):
        lx = sx * (W / 2 - t - 50)
        a = f.p(lx, D / 2 - t - 60); b = f.p(lx, D / 2 - t - 60 - 470)
        w.ramp(a[0], a[1], b[0], b[1], 90, py, top, 'castle_stone', base=py - 10)
        w.walkways.append((a[0], a[1], b[0], b[1], 45, py, top))
    for lx, lz in [(-W / 2 + t / 2, -200), (W / 2 - t / 2, 100), (0, -D / 2 + t / 2)]:
        px, pz = f.p(lx, lz); chest(w, px, top, pz, 'castle wall walk')
    # corner towers with teal spires; two have ivy and a chest on top
    for k, (lx, lz) in enumerate([(-W / 2, -D / 2), (W / 2, -D / 2), (W / 2, D / 2), (-W / 2, D / 2)]):
        tx, tz = f.p(lx, lz)
        w.cylinder(tx, tz, 110, py - 60, top + 170, 'castle_stone', n=10, surf='stone', climb=(k in (1, 3)))
        w.far = True
        w.cylinder(tx, tz, 122, top + 170, top + 192, 'castle_stone', n=10, col='none')
        w.cone(tx, tz, 126, top + 192, top + 520, 'roof_teal', n=10)
        w.cylinder(tx, tz, 4, top + 520, top + 570, 'gold', n=4, col='none')
        w.far = False
        if k in (1, 3): chest(w, tx, top + 170, tz, 'castle tower top')
    # the gatehouse
    for sx in (-1, 1):
        px, pz = f.p(sx * (g / 2 + 65), D / 2 - 15)
        w.box(px, py - 60, pz, 130, top + 110 - py + 60, 150, 'castle_stone', yaw, surf='stone')
        w.far = True
        w.hip(px, pz, 130, 150, top + 110, 190, 'roof_teal', yaw, overhang=12, col=None)
        w.far = False
        bx_, bz_ = f.p(sx * (g / 2 + 65), D / 2 + 61)
        nx_, nz_ = f.dir(0, 1)
        w.decal(bx_, py + 120, bz_, 60, 110, nx_, nz_, 'banner_red', off=3)
        lx_, lz_ = f.p(sx * (g / 2 + 20), D / 2 + 90); lamp(w, lx_, lz_, H(lx_, lz_))
    px, pz = f.p(0, D / 2 - 15)
    w.box(px, top - 20, pz, g + 10, 60, 150, 'castle_stone', yaw, surf='stone')
    # the keep (two floors, flat roof) at the back west, the great spire at the back east; the courtyard in the middle stays open
    kx, kz = f.p(-260, -330)
    house(w, kx, kz, 460, 330, yaw, 2, style(wall='castle_stone', base='castle_stone', trim='castle_stone', roof='roof_teal', roof_kind='flat', floor='cobble',
          window='window_lit', tint=(1, 1, 1)), name='Castle keep', floor_y=py + 10, door_width=150, back_door=False)
    sx_, sz_ = f.p(330, -340)
    spiral_tower(w, sx_, sz_, 290, 760, yaw=yaw, wall='castle_stone', floor='cobble', roof='roof_teal', name='Castle spire', cap='spire', y0=py + 10)
    w.far = True
    for lx in (-470, -50):
        for lz in (-480, -180):
            x, z = f.p(lx, lz)
            yb = py + 10 + 2 * STOREY + 60
            w.cylinder(x, z, 46, yb, yb + 170, 'castle_stone', n=8, col='none')
            w.cone(x, z, 58, yb + 170, yb + 380, 'roof_teal', n=8)
    w.far = False
    # courtyard: Zelda's garden hedges, a fountain off the gate's line, banners on the back wall
    fx_, fz_ = f.p(-350, 260)
    ring_wall(w, fx_, fz_, 90, 26, py, py + 40, 'castle_stone', n=12)
    w.cylinder(fx_, fz_, 76, py + 26, py + 30, 'water', n=12, col='none')
    w.cylinder(fx_, fz_, 20, py, py + 130, 'castle_stone', n=8, surf='stone')
    px, pz = f.p(350, 260); chest(w, px, py, pz, 'castle courtyard')
    for lx in (-420, -300, 300, 420):
        bx_, bz_ = f.p(lx, 80)
        bush(w, bx_, bz_, 0.8, int(lx) & 255, 'leaves_dark', y=py)
    for lx in np.linspace(-W / 2 + 140, W / 2 - 140, 6):
        px, pz = f.p(lx, -D / 2 + t + 1)
        nx_, nz_ = f.dir(0, 1)
        w.decal(px, py + 80, pz, 50, 150, nx_, nz_, 'banner_blue', off=2)
    # pines round the rise, the approach kept clear
    rng = rng_for('castle pines')
    for k in range(34):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(860, 1040)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(x), np.array(z)) < 200 or near_any(x, z, -500): continue
        pine(w, x, z, rng.uniform(0.85, 1.15), 300 + k)

# ---- Temple of Time (a ruin on its mesa) -------------------------------------------------------------------------------------------
def temple_of_time(w):
    cx, cz = region(w, 'Temple of Time')
    yaw = face_yaw(cx, cz, -300, 1250)      # the facade looks back to the castle
    f = Frame(cx, cz, yaw)
    lo, hi = ground_range(cx, cz, 1000, 760, yaw)
    base = hi + 24
    w.prism(f.outline(rect(0, 0, 1000, 760)), lo - 60, base - 24, 'moss_stone', surf='stone')
    w.prism(f.outline(rect(0, -20, 900, 680)), base - 24, base, 'moss_stone', surf='stone', top_mat='cobble')
    for (lx, lz, dx, dz) in [(0, 380, 0, 1), (0, -380, 0, -1), (500, 0, 1, 0), (-500, 0, -1, 0)]:
        px, pz = f.p(lx, lz); ex, ez = f.p(lx + dx * 130, lz + dz * 130)
        w.ramp(ex, ez, px, pz, 200, H(ex, ez) - 4, base, 'moss_stone', base=lo - 40)
        w.walkways.append((ex, ez, px, pz, 100, H(ex, ez) - 4, base))
    w.buildings.append((cx, cz, 500, 380, base, yaw))
    segs = [((-430, -320), (-430, 300), 300), ((430, -320), (430, 40), 240), ((430, 40), (430, 300), 120), ((-430, -320), (-60, -320), 200), ((120, -320), (430, -320), 110)]
    for (ax, az), (bx, bz), hgt in segs:
        a = f.p(ax, az); b = f.p(bx, bz)
        L = math.hypot(b[0] - a[0], b[1] - a[1]); mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        w.box(mx, base, mz, L + 40, hgt, 40, 'moss_stone', ang, surf='stone', climb=(0, 2))
    chest(w, f.p(-330, -100)[0], base, f.p(-330, -100)[1], 'temple west aisle')
    for sx in (-1, 1):
        px, pz = f.p(sx * 260, 320)
        w.box(px, base, pz, 90, 520, 90, 'moss_stone', yaw, surf='stone', climb=(1,))
    px, pz = f.p(0, 320)
    w.box(px, base + 460, pz, 610, 80, 100, 'moss_stone', yaw, surf='stone')
    chest(w, px, base + 540, pz, 'temple lintel')
    w.far = True
    w.gable(px, pz, 610, 100, base + 540, 150, 'moss_stone', yaw + math.pi / 2, overhang=8, col=None)
    w.far = False
    nx, nz = f.dir(0, 1)
    w.decal(px, base, pz, 220, 400, -nx, -nz, 'door', off=-51)
    for k, lz in enumerate(np.linspace(-200, 200, 4)):
        for sx in (-1, 1):
            px, pz = f.p(sx * 230, lz)
            hgt = 360 if (k + (sx > 0)) % 3 else 130
            w.cylinder(px, pz, 28, base, base + hgt, 'moss_stone', n=6, col='prop')
    a = f.p(-330, 60); b = f.p(-190, 230)
    beam(w, (a[0], base + 50, a[1]), (b[0], base + 50, b[1]), 56, 50, 'moss_stone', col='prop')
    ax, az = f.p(0, -120)
    w.box(ax, base, az, 220, 40, 110, 'castle_stone', yaw, surf='stone')
    for sx in (-1, 0, 1):
        qx, qz = f.p(sx * 70, -120)
        w.cylinder(qx, qz, 14, base + 40, base + 64, 'gold', n=6, col='none')
    chest(w, ax, base + 40, az, 'Master Sword pedestal')
    vx, vz = f.p(720, -320)
    house(w, vx, vz, 360, 360, face_yaw(vx, vz, cx, cz), 1, HouseStyle(wall='moss_stone', base='moss_stone', trim='moss_stone', roof='moss_stone', roof_kind='flat',
          floor='cobble', climb=True), back_door=False, furnish=False, name='Temple vault')
    for k in range(4):
        bush(w, *f.p(-480 + k * 320, -470), 0.8, 30 + k, 'leaves_dark')
    for a in np.radians([30, 150, 250, 290]):
        oak(w, cx + 780 * math.cos(a), cz + 660 * math.sin(a), 1.0, 60 + int(a * 10))

# ---- Hyrule Field Ruins -------------------------------------------------------------------------------------------------------------
def field_ruins(w):
    cx, cz = region(w, 'Hyrule Field Ruins')
    rng = rng_for('field ruins')
    y = H(cx, cz)
    # an old square fort, its walls broken down to stumps with gaps, a corner keep still standing
    S = 760
    corners = [(cx - S / 2, cz - S / 2), (cx + S / 2, cz - S / 2), (cx + S / 2, cz + S / 2), (cx - S / 2, cz + S / 2)]
    for k in range(4):
        broken_wall(w, corners[k], corners[(k + 1) % 4], 280, rng=rng, gaps=2)
    watch_tower(w, cx + S / 2 - 250, cz - S / 2 + 250, 240, 380, 'moss_stone', yaw=face_yaw(cx + S / 2, cz - S / 2, cx, cz), name='Ruined keep', cap=False)
    arch(w, cx, cz + 120, 0.0, span=200, hgt=280)
    chest(w, cx + 60, H(cx + 60, cz + 40), cz + 40, 'ruin arch')
    # fallen columns and broken pillars inside
    for k in range(4):
        a = k * 1.25 + 0.4; r = 200 + 40 * (k % 2)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        w.cylinder(px, pz, 34, H(px, pz) - 10, H(px, pz) + rng.uniform(90, 260), 'moss_stone', n=7, surf='stone')
    a = (cx - 300, y + 40, cz + 260); b = (cx - 80, y + 40, cz + 320)
    beam(w, a, b, 70, 70, 'moss_stone', col='prop')
    chest(w, cx - 240, H(cx - 240, cz - 200), cz - 200, 'ruin courtyard')
    chest(w, cx + 220, H(cx + 220, cz + 250), cz + 250, 'ruin cellar')
    for k in range(8):
        a = k * 0.8 + 0.2; r = rng.uniform(520, 700)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 160: continue
        rock(w, px, pz, rng.uniform(0.9, 1.4), 400 + k, 'moss_stone', solid=k % 2 == 0)

# ---- Lake Hylia and the docks -------------------------------------------------------------------------------------------------------
def lake_hylia(w):
    cx, cz = region(w, 'Lake Hylia')
    y = H(cx, cz)
    # the lakeside laboratory (two floors, a roof terrace), the scarecrow's dock and the Water Temple's pillar ring; the middle stays open
    lx, lz = cx - 180, cz - 200
    house(w, lx, lz, 380, 360, face_yaw(lx, lz, cx, cz + 400), 2, style(wall='whitewash', roof='roof_blue', roof_kind='flat', tint=(0.92, 0.96, 1.0)),
          name='Lakeside laboratory', stair_landing=True)
    for k in range(6):
        a = k * math.pi / 3 + 0.3
        px, pz = cx + 330 * math.cos(a), cz + 330 * math.sin(a)
        if math.hypot(px - lx, pz - lz) < 330: continue
        w.cylinder(px, pz, 30, H(px, pz) - 20, H(px, pz) + 260, 'moss_stone', n=8, surf='stone', climb=(k % 2 == 0))
    # the Water Temple's mark: a stone dais by the shore, with the chest
    tx, tz = cx + 200, cz + 120
    w.cylinder(tx, tz, 120, H(tx, tz) - 20, H(tx, tz) + 20, 'castle_stone', n=10, surf='stone')
    chest(w, tx, H(tx, tz) + 20, tz, 'Water Temple dais')
    for k in range(5):
        a = rng_for('lake').uniform(0, 2 * math.pi)
        px, pz = cx + 380 * math.cos(a), cz + 380 * math.sin(a)
        if H(px, pz) > WATER_Y + 20 and math.hypot(px - lx, pz - lz) > 300: bush(w, px, pz, 0.9, 900 + k, 'leaves')
    oak(w, cx + 60, cz - 330, 1.3, 77, leaves='leaves')
    # the long bridge to the mainland, towards the docks
    ux, uz = -1500 / math.hypot(1500, 1900), 1900 / math.hypot(1500, 1900)
    a = (cx + ux * 380, H(cx + ux * 380, cz + uz * 380) + 6, cz + uz * 380)
    b = (cx + ux * 1980, H(cx + ux * 1980, cz + uz * 1980) + 6, cz + uz * 1980)
    yb = max(a[1], b[1], WATER_Y + 110)
    a = (a[0], yb, a[2]); b = (b[0], yb, b[2])
    sloped_bridge(w, a, b, width=180, deck_mat='planks', wall_mat='timber', piers=6, name='Lake Hylia bridge', rope=True)
    for end in (a, b):
        g = H(end[0] - ux * 0 , end[2])
    # ramps from the bridge's ends down to the ground
    for (ex, ey, ez), s_ in ((a, 1), (b, -1)):
        g = H(ex + ux * s_ * -160, ez + uz * s_ * -160)
        if ey - g > 20:
            gx, gz = ex - ux * s_ * max(80, (ey - g) / 0.5), ez - uz * s_ * max(80, (ey - g) / 0.5)
            g2 = H(gx, gz)
            w.ramp(gx, gz, ex, ez, 160, g2 - 4, ey, 'planks', surf='wood', base=g2 - 60)
            w.walkways.append((gx, gz, ex, ez, 80, g2 - 4, ey))
    mx, mz = (a[0] + b[0]) / 2, (a[2] + b[2]) / 2
    chest(w, mx + 40, yb, mz, 'Lake Hylia bridge')
    for u in (0.25, 0.75):
        lamp(w, a[0] + (b[0] - a[0]) * u + 70 * uz, a[2] + (b[2] - a[2]) * u - 70 * ux, yb)

def hylia_docks(w):
    cx, cz = region(w, 'Hylia Docks')
    y = H(cx, cz)
    sea = math.radians(285)
    # two piers out over the sea, a boathouse, the harbour master's warehouse and the lighthouse on the point
    for k, da in enumerate((-0.32, 0.25)):
        a = sea + da
        px, pz = cx + 430 * math.cos(a), cz + 430 * math.sin(a)
        deck_over_sea(w, px, pz, face_yaw(px, pz, px + math.cos(a), pz + math.sin(a)), L=620, W=220, name=f'Hylia pier {k}')
    bx, bz = cx + 260 * math.cos(sea + math.pi / 2), cz + 260 * math.sin(sea + math.pi / 2)
    house(w, bx, bz, 380, 360, face_yaw(bx, bz, cx, cz), 2, style(wall='timber', roof='roof_blue', roof_kind='gable', tint=(0.95, 0.95, 1.0)),
          name='Harbour warehouse', roof_rise=170, stair_landing=True)
    hx, hz = cx + 300 * math.cos(sea - math.pi / 2 - 0.3), cz + 300 * math.sin(sea - math.pi / 2 - 0.3)
    house(w, hx, hz, 300, 280, face_yaw(hx, hz, cx, cz), 1, style(wall='plaster', roof='roof_teal', roof_kind='hip'), name='Boathouse', back_door=False)
    lx, lz = cx + 640 * math.cos(sea - 1.1), cz + 640 * math.sin(sea - 1.1)
    spiral_tower(w, lx, lz, 300, 560, yaw=face_yaw(lx, lz, cx, cz), wall='whitewash', floor='planks', roof='roof_red', name='Hylia lighthouse', cap='hip')
    w.far = True
    w.box(lx, H(lx, lz) + 12 + 560 + 140, lz, 60, 60, 60, 'window_lit', col='none')
    w.far = False
    for k in range(4):
        a = sea + math.pi + k * 0.5 - 0.75
        crates(w, cx + 200 * math.cos(a), cz + 200 * math.sin(a), k, 3, a)
    for k in range(5):
        barrel(w, cx - 120 + 44 * k, cz + 120, H(cx - 120 + 44 * k, cz + 120))
    chest(w, cx, y, cz, 'docks yard')
    signpost(w, cx + 300, cz + 300, 0.8)

# ---- Ordon Village ----------------------------------------------------------------------------------------------------------------
def ordon_village(w):
    cx, cz = region(w, 'Ordon Village')
    y = H(cx, cz)
    os_ = lambda roof, kind='gable', tint=(1, 0.97, 0.9): HouseStyle(wall='timber', base='town_stone', trim='planks', roof=roof, roof_kind=kind, floor='planks', tint=tint)
    houses = [(-420, -380, 2, 'thatch', 'Mayor Bo\'s house'), (380, -420, 1, 'thatch', 'Rusl\'s house'), (520, 220, 1, 'roof_red', 'Sera\'s shop'),
              (-560, 280, 1, 'thatch', 'Jaggle\'s house'), (60, 560, 2, 'roof_green', 'Ordon smithy')]
    for i, (dx, dz, st, roof, name) in enumerate(houses):
        x, z = cx + dx, cz + dz
        house(w, x, z, 380 if st > 1 else 300, 360 if st > 1 else 280, face_yaw(x, z, cx, cz), st, os_(roof), name=name, roof_rise=170, stair_landing=st > 1)
    # Link's treehouse: a great oak with the house on a deck in its arms, a ramp up
    tx, tz = cx - 120, cz - 650
    oak(w, tx - 120, tz - 60, 2.0, 123, leaves='leaves')
    deck(w, tx + 110, tz, 260, 220, y + 230, face_yaw(tx + 110, tz, cx, cz), name='Link\'s treehouse')
    # the goat pen and the pumpkin patch
    fence(w, [(cx + 300, cz + 650), (cx + 760, cz + 650), (cx + 760, cz + 1000), (cx + 300, cz + 1000)], 60, 'timber')
    for k in range(10):
        px, pz = cx - 700 + (k % 5) * 80, cz + 640 + (k // 5) * 110
        pumpkin_plant(w, px, pz, 1.0 + 0.1 * (k % 3), 800 + k)
    for k in range(4): hay_bale(w, cx + 380 + 90 * k, cz + 800)
    # the spring's pool (a shallow dish of water drawn on the ground), the well, lamps
    ring_wall(w, cx, cz, 70, 20, y - 10, y + 50, 'town_stone', n=10)
    for sx in (-1, 1): w.box(cx + sx * 64, y, cz, 10, 150, 10, 'timber', col='none')
    w.gable(cx, cz, 160, 80, y + 150, 46, 'thatch', math.pi / 2, overhang=8, col=None)
    chest(w, cx + 160, y, cz - 80, 'Ordon well')
    for a in np.radians([45, 135, 225, 315]):
        lamp(w, cx + 260 * math.cos(a), cz + 260 * math.sin(a))
    rng = rng_for('ordon trees')
    for k in range(14):
        a = k * 2 * math.pi / 14 + rng.uniform(-.1, .1); r = rng.uniform(880, 1050)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200 or H(px, pz) < WATER_Y + 40: continue
        oak(w, px, pz, rng.uniform(1.0, 1.3), 810 + k, leaves='leaves' if k % 2 else 'leaves_dark')

# ---- Lon Lon Ranch -----------------------------------------------------------------------------------------------------------------
def lon_lon_ranch(w):
    cx, cz = region(w, 'Lon Lon Ranch')
    y = H(cx, cz)
    ang = lambda tx, tz: math.atan2(tz - cz, tx - cx)
    gates = [(ang(-2400, -1100), 220), (ang(-3900, -2300), 220), (ang(-4600, -700), 220), (ang(-4400, 200), 220)]
    ring_wall(w, cx, cz, 740, 40, y - 30, y + 130, 'town_stone', gaps=gates, n=14, climb=True)
    for a, wd in gates: gate_posts(w, cx, cz, 740, a, wd, mat='timber', hgt=200)
    # the paddock: a round rail fence with four openings, hay bales and a trough inside (cover in the open)
    for k in range(4):
        a0 = k * math.pi / 2 + 0.25; a1 = a0 + math.pi / 2 - 0.5
        fence(w, [(cx + 230 * math.cos(a), cz + 230 * math.sin(a)) for a in np.linspace(a0, a1, 5)], 70, 'timber')
    rng = rng_for('lon lon hay')
    for k in range(7):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(70, 170)
        hay_bale(w, cx + r * math.cos(a), cz + r * math.sin(a), k)
    w.box(cx + 120, y - 10, cz - 60, 160, 50, 50, 'planks', 0.5, surf='wood', col='prop')
    # the barn (two floors), Talon's house, the silo, the stable shed
    bx, bz = cx - 385, cz - 268
    house(w, bx, bz, 460, 380, face_yaw(bx, bz, cx, cz), 2, HouseStyle(wall='timber', base='town_stone', trim='planks', roof='roof_red', roof_kind='gable',
          floor='planks', tint=(1.0, 0.92, 0.86)), name='Lon Lon barn', roof_rise=240, stair_landing=True, back_door=False)
    hx, hz = cx + 376, cz - 282
    house(w, hx, hz, 360, 320, face_yaw(hx, hz, cx, cz), 2, style(roof='roof_red', roof_kind='hip', tint=(1.0, 0.97, 0.9)), name='Talon\'s house', stair_landing=True, back_door=False)
    sx, sz = cx + 350, cz + 300
    sy = H(sx, sz)
    w.cylinder(sx, sz, 120, sy - 30, sy + 420, 'plaster', n=10, surf='stone', climb=True)
    w.far = True
    w.cone(sx, sz, 140, sy + 420, sy + 560, 'roof_red', n=10)
    w.far = False
    st_x, st_z = cx - 350, cz + 300
    f = Frame(st_x, st_z, face_yaw(st_x, st_z, cx, cz))
    for lx in (-180, 0, 180):
        for lz in (-90, 90):
            px, pz = f.p(lx, lz)
            w.box(px, H(px, pz), pz, 16, 190, 16, 'timber', f.yaw, col='none')
    w.gable(st_x, st_z, 400, 220, y + 190, 80, 'thatch', f.yaw, overhang=20, col=None)
    for lx in (-120, 60):
        px, pz = f.p(lx, 0); hay_bale(w, px, pz)
    chest(w, f.p(150, 0)[0], H(*f.p(150, 0)), f.p(150, 0)[1], 'Lon Lon stable')
    chest(w, cx - 60, y, cz + 140, 'Lon Lon paddock')
    for k in range(10):
        a = k * 2 * math.pi / 10 + 0.15; r = rng.uniform(780, 900)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200: continue
        oak(w, px, pz, rng.uniform(0.9, 1.2), 840 + k)

# ---- Kokiri Forest and the Lost Woods ----------------------------------------------------------------------------------------------
def kokiri_forest(w):
    cx, cz = region(w, 'Kokiri Forest')
    y = H(cx, cz)
    rng = rng_for('kokiri')
    entry = math.atan2(-3500 - cz, 3300 - cx)       # the way in from the Lost Woods
    ks = HouseStyle(wall='bark', base='moss_stone', trim='bark', roof='thatch', roof_kind='hip', floor='planks', tint=(1.0, 0.96, 0.9))
    # the Kokiri's stump houses round the clearing; Mido's has two floors
    for i, da in enumerate([0.9, 1.7, 2.6, 3.5, 4.4, 5.3]):
        a = entry + da
        x, z = cx + 470 * math.cos(a), cz + 470 * math.sin(a)
        st = 2 if i == 2 else 1
        house(w, x, z, 380 if st > 1 else 300, 360 if st > 1 else 300, face_yaw(x, z, cx, cz), st, ks, name='Mido\'s house' if st > 1 else f'Kokiri house {i}',
              roof_rise=170, stair_landing=st > 1, back_door=False)
    # the Great Deku Tree at the back of the clearing, opposite the way in, and treehouse decks in the big trees
    gx, gz = cx + 760 * math.cos(entry + math.pi), cz + 760 * math.sin(entry + math.pi)
    w.far = True
    oak(w, gx, gz, 3.2, 1234, leaves='leaves')
    w.far = False
    for k, da in enumerate([1.3, 2.3, 4.85]):
        a = entry + da
        x, z = cx + 790 * math.cos(a), cz + 790 * math.sin(a)
        oak(w, x + 140 * math.cos(a), z + 140 * math.sin(a), 1.7, 1240 + k, leaves='leaves_dark')
        deck(w, x, z, 200, 200, y + 190, face_yaw(x, z, cx, cz), name=f'Kokiri treehouse {k}', ramp='x')
    # the training ground's boulder ring and the shop's counter in the clearing; the middle is open (the Moss boss)
    for k, da in enumerate([1.3, 2.15, 3.05, 3.95, 4.85]):
        a = entry + da
        rock(w, cx + 240 * math.cos(a), cz + 240 * math.sin(a), 0.7, 1250 + k, 'moss_stone', solid=k % 2 == 0)
    chest(w, cx + 150 * math.cos(entry + 2), y, cz + 150 * math.sin(entry + 2), 'Kokiri clearing')
    # the forest wall: big trees round the clearing, the way in left open
    for k in range(34):
        a = k * 2 * math.pi / 34 + rng.uniform(-.06, .06)
        if abs(((a - entry + math.pi) % (2 * math.pi)) - math.pi) < 0.25: continue
        r = 930 + rng.uniform(-60, 120)
        oak(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(1.3, 1.7), 1300 + k, leaves='leaves_dark' if k % 3 else 'leaves')

def lost_woods(w):
    cx, cz = region(w, 'Lost Woods')
    rng = rng_for('lost woods')
    for k in range(26):
        a = k * 2 * math.pi / 26 + rng.uniform(-.1, .1); r = 330 + 160 * (k % 3) + rng.uniform(-40, 40)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(x), np.array(z)) < 150: continue
        oak(w, x, z, rng.uniform(1.2, 1.6), 5000 + k, leaves='leaves_dark')
    y = H(cx, cz)
    w.cylinder(cx + 15, cz - 15, 44, y - 10, y + 70, 'bark', n=6, col='prop')
    a = (cx - 200, y + 34, cz + 60); b = (cx + 40, y + 34, cz - 120)
    beam(w, a, b, 68, 68, 'bark', col='prop')
    chest(w, cx + 130, H(cx + 130, cz + 60), cz + 60, 'hollow log')
    sx, sz = cx - 260, cz + 180
    w.cylinder(sx, sz, 60, H(sx, sz) - 20, H(sx, sz) + 40, 'bark', n=9, surf='wood', top_mat='planks')
    chest(w, sx, H(sx, sz) + 40, sz, 'Saria\'s stump')
    # hedge walls: the maze's low green walls with ways through
    for k in range(5):
        a = k * 1.26 + 0.3
        p0 = (cx + 400 * math.cos(a), cz + 400 * math.sin(a)); p1 = (cx + 420 * math.cos(a + 0.5), cz + 420 * math.sin(a + 0.5))
        L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        mx, mz = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
        w.box(mx, H(mx, mz) - 10, mz, L, 120, 50, 'leaves_dark', math.atan2(p1[1] - p0[1], p1[0] - p0[0]), surf='wood')
    for k in range(6):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(120, 300)
        bush(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(0.8, 1.2), 5100 + k)
    chest(w, cx - 60, H(cx - 60, cz - 260), cz - 260, 'Skull Kid stump')

# ---- the river: Great Hylia Bridge, Zora's River, Zora's Domain ---------------------------------------------------------------------
def great_hylia_bridge(w):
    x, z = region(w, 'Great Hylia Bridge')
    a = (1300, H(1300, z) + 4, z); b = (2420, H(2420, z) + 4, z)
    yd = max(a[1], b[1])
    p0, p1, y = stone_bridge(w, (a[0], yd, a[2]), (b[0], yd, b[2]), width=230, piers=3, towers=True, name='Great Hylia Bridge')
    for (ex, ey, ez), s_ in ((a, -1), (b, 1)):
        g = H(ex, ez)
        if yd - g > 20:
            run = max(80, (yd - g) / 0.5)
            w.ramp(ex + s_ * run, ez, ex, ez, 230, H(ex + s_ * run, ez) - 4, yd, 'cobble', base=H(ex + s_ * run, ez) - 60)
            w.walkways.append((ex + s_ * run, ez, ex, ez, 115, H(ex + s_ * run, ez) - 4, yd))
    chest(w, (p0[0] + p1[0]) / 2, y, z + 40, 'Great Hylia Bridge middle')
    for u in (0.2, 0.8):
        lamp(w, p0[0] + (p1[0] - p0[0]) * u, z - 100, y)
    # the pier ledge under the bridge's west end: a stone shelf above the water, reached by the ivy on the pier
    px = p0[0] + (p1[0] - p0[0]) / 4
    bed = H(px, z)
    ly = max(bed, -140) + 60
    w.box(px, ly - 20, z + 150, 160, 20, 90, 'castle_stone', surf='stone')
    w.box(px, bed - 40, z + 95, 90, ly - 20 - bed + 40, 30, 'castle_stone', surf='stone', climb=(0, 2))
    chest(w, px, ly, z + 150, 'bridge pier ledge')

def zoras_river(w):
    x, z = region(w, 'Zora\'s River')
    a = (2050, H(2050, -900) + 4, -900); b = (2820, H(2820, -900) + 4, -900)
    sloped_bridge(w, a, b, width=160, deck_mat='planks', wall_mat='timber', piers=3, name='Zora\'s River rope bridge', rope=True)
    chest(w, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, -870, 'rope bridge middle')
    # the river's falls: one at every step of the river, and the Domain's outflow
    falls = [(RIVER[0][0], RIVER[0][1], RIVER[0][2], 380, RIVER[0][3], RIVER[1])]
    for k in range(1, len(RIVER) - 1):
        px, pz, hw, lv = RIVER[k]; up = RIVER[k - 1][3]
        if up - lv < 20: continue
        falls.append((px, pz, hw, up, lv, RIVER[k + 1]))
    for px, pz, hw, top, bot, nxt in falls:
        L = math.hypot(nxt[0] - px, nxt[1] - pz); ux, uz = (nxt[0] - px) / L, (nxt[1] - pz) / L
        waterfall(w, px, pz, ux, uz, hw * 2 * 0.9, top + 2, bot, lean=50)
        for s_ in (-1, 1):
            rx, rz = px - uz * s_ * (hw + 60), pz + ux * s_ * (hw + 60)
            rock(w, rx, rz, 1.1, int(px) & 1023 + (s_ > 0), 'moss_stone', solid=True)
    # a fisherman's hut on the east bank, stepping stones
    hx, hz = 3000, -1250
    house(w, hx, hz, 300, 280, face_yaw(hx, hz, 2500, -1100), 1, style(roof='thatch', roof_kind='gable'), name='River hut', back_door=False)
    for k, (sx, sz) in enumerate([(2380, -1250), (2470, -1310), (2560, -1260)]):
        w.cylinder(sx, sz, 50, H(sx, sz) - 20, 20 + 24, 'moss_stone', n=8, surf='stone')
    chest(w, 2900, H(2900, -700), -700, 'river bank')

def zoras_domain(w):
    cx, cz = region(w, 'Zora\'s Domain')
    lv = POOLS[0][4]
    zs = HouseStyle(wall='whitewash', base='castle_stone', trim='castle_stone', roof='roof_blue', roof_kind='hip', floor='cobble', tint=(0.82, 0.92, 1.05))
    # the walkway across the lake through the round island, on stone piers
    a = (cx - 620, lv + 50, cz); b = (cx + 620, lv + 50, cz)
    for end in (a, b):
        g = H(end[0], end[2])
    sloped_bridge(w, (cx - 560, lv + 50, cz), (cx + 560, lv + 50, cz), width=150, deck_mat='cobble', wall_mat='castle_stone', piers=4, name='Domain walkway')
    for s_ in (-1, 1):
        ex = cx + s_ * 560; g = H(ex + s_ * 120, cz)
        if lv + 50 - g > 10:
            run = max(60, (lv + 50 - g) / 0.5)
            w.ramp(ex + s_ * run, cz, ex, cz, 150, H(ex + s_ * run, cz) - 4, lv + 50, 'cobble', base=H(ex + s_ * run, cz) - 60)
            w.walkways.append((ex + s_ * run, cz, ex, cz, 75, H(ex + s_ * run, cz) - 4, lv + 50))
    w.cylinder(cx, cz, 180, H(cx, cz) - 20, lv + 50, 'castle_stone', n=12, surf='stone', top_mat='cobble')
    w.buildings.append((cx, cz, 120, 120, lv + 50, 0.0))
    chest(w, cx, lv + 50, cz + 90, 'Domain island')
    for k in range(6):
        ang_ = k * math.pi / 3 + 0.5
        w.cylinder(cx + 140 * math.cos(ang_), cz + 140 * math.sin(ang_), 14, lv + 50, lv + 240, 'castle_stone', n=6, col='prop')
    w.hip(cx, cz, 300, 300, lv + 240, 80, 'roof_blue', overhang=12, col=None)
    # King Zora's terrace on the high shore with the falls behind it pouring into the lake
    tx, tz = cx, cz + 720
    ty = H(tx, tz)
    w.cylinder(tx, tz, 230, ty - 40, ty + 60, 'castle_stone', n=12, surf='stone', top_mat='cobble')
    w.buildings.append((tx, tz, 160, 160, ty + 60, 0.0))
    w.ramp(tx, tz - 230 - 120, tx, tz - 230, 160, H(tx, tz - 350) - 4, ty + 60, 'castle_stone', base=H(tx, tz - 350) - 40)
    w.walkways.append((tx, tz - 350, tx, tz - 230, 80, H(tx, tz - 350) - 4, ty + 60))
    w.box(tx, ty + 60, tz + 120, 160, 120, 60, 'castle_stone', surf='stone')
    chest(w, tx + 80, ty + 60, tz - 60, 'King Zora\'s terrace')
    w.far = True
    w.prism([(tx - 420, tz + 300), (tx + 420, tz + 300), (tx + 420, tz + 420), (tx - 420, tz + 420)], ty - 40, ty + 520, 'moss_stone', surf='stone')
    w.far = False
    waterfall(w, tx + 260, tz + 290, 0, -1, 200, ty + 500, ty + 60, lean=60)
    waterfall(w, tx - 260, tz + 290, 0, -1, 200, ty + 500, ty + 60, lean=60)
    # Zora houses on the shore
    for i, (dx, dz) in enumerate([(-800, 420), (780, 260), (-760, -320)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 320, 300, face_yaw(x, z, cx, cz), 1, zs, name=f'Zora house {i}', back_door=False, roof_rise=140)
    for k in range(8):
        a_ = k * math.pi / 4 + 0.2
        px, pz = cx + 600 * math.cos(a_), cz + 520 * math.sin(a_)
        if H(px, pz) > lv + 10 and not near_any(px, pz, -600): rock(w, px, pz, 0.8, 1400 + k, 'moss_stone', solid=False)

# ---- Kakariko Village and its graveyard --------------------------------------------------------------------------------------------
def windmill(w, x, z, yaw):
    y0, top = house(w, x, z, 380, 380, yaw, 2, HouseStyle(wall='plaster', base='town_stone', trim='timber', roof='thatch', roof_kind='hip',
                    floor='planks', tint=(1, 0.97, 0.9)), name='Kakariko windmill', roof_rise=300, stair_landing=True)
    f = Frame(x, z, yaw)
    hx, hz = f.p(0, -200)
    hub = (hx, y0 + 2 * STOREY + 150, hz)
    nx, nz = f.dir(1, 0)
    w.far = True
    for k in range(4):
        a = k * math.pi / 2 + 0.3
        ex, ey = math.cos(a) * 460, math.sin(a) * 460
        px, py_ = -math.sin(a) * 42, math.cos(a) * 42
        w.face([(hub[0] + nx * px * 0.2, hub[1] + py_ * 0.2, hub[2] + nz * px * 0.2), (hub[0] + nx * (ex + px), hub[1] + ey + py_, hub[2] + nz * (ex + px)),
                (hub[0] + nx * (ex - px), hub[1] + ey - py_, hub[2] + nz * (ex - px)), (hub[0] - nx * px * 0.2, hub[1] - py_ * 0.2, hub[2] - nz * px * 0.2)], 'cloth', double=True)
    w.far = False

def kakariko_village(w):
    cx, cz = region(w, 'Kakariko Village')
    y = H(cx, cz)
    ks = lambda roof, tint=(1, 1, 1), kind='gable': HouseStyle(wall='plaster', base='town_stone', trim='timber', roof=roof, roof_kind=kind, floor='planks', tint=tint)
    for i, (a, roof, kind, name) in enumerate([(1.5, 'roof_red', 'gable', 'Impa\'s house'), (2.85, 'thatch', 'hip', 'Kakariko inn'), (-1.75, 'roof_blue', 'hip', 'Kakariko shop')]):
        x, z = cx + 480 * math.cos(a), cz + 480 * math.sin(a)
        house(w, x, z, 380, 360, face_yaw(x, z, cx, cz), 2, ks(roof, (1.0, 0.95, 0.88), kind), name=name, roof_rise=160, stair_landing=True, back_door=False)
    wx, wz = cx + 560 * math.cos(-0.2), cz + 560 * math.sin(-0.2)
    windmill(w, wx, wz, face_yaw(wx, wz, cx, cz))
    tx, tz = cx + 600 * math.cos(3.6), cz + 600 * math.sin(3.6)
    watch_tower(w, tx, tz, 220, 420, 'town_stone', 'thatch', yaw=face_yaw(tx, tz, cx, cz), name='Kakariko lookout')
    # the well in the square
    ring_wall(w, cx, cz, 70, 20, y - 10, y + 50, 'town_stone', n=10)
    for sx in (-1, 1): w.box(cx + sx * 64, y, cz, 10, 150, 10, 'timber', col='none')
    w.gable(cx, cz, 160, 80, y + 150, 46, 'thatch', math.pi / 2, overhang=8, col=None)
    chest(w, cx - 150, y, cz + 100, 'Kakariko well')
    chest(w, cx + 200, y, cz - 160, 'Kakariko square')
    # the cuccos' pen, crates of the builders' yard, lamps
    fence(w, [(cx - 300, cz - 330), (cx - 300, cz - 160), (cx - 120, cz - 160)], 60, 'timber')
    for k in range(4):
        w.cylinder(cx - 260 + (k % 2) * 70, cz - 290 + (k // 2) * 60, 14, H(cx - 260, cz - 290), H(cx - 260, cz - 290) + 24, 'cloth', n=6, col='none')
    crates(w, cx + 260, cz + 120, 1, 3, 0.4); crates(w, cx + 120, cz + 300, 2, 2, 1.1)
    for a in np.radians([60, 150, 240, 330]):
        lamp(w, cx + 300 * math.cos(a), cz + 300 * math.sin(a))
    rng = rng_for('kakariko trees')
    for k in range(10):
        a = k * 0.63 + 0.2; r = rng.uniform(820, 980)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200 or near_any(px, pz, -600): continue
        oak(w, px, pz, rng.uniform(1.0, 1.3), 1600 + k)

def kakariko_graveyard(w):
    x, z = region(w, 'Kakariko Graveyard')
    y = H(x, z)
    gravestones(w, x - 230, z + 40, 3, 3, seed=11)
    gravestones(w, x + 230, z + 40, 3, 3, seed=12)
    house(w, x, z - 330, 380, 360, face_yaw(x, z - 330, x, z), 1, HouseStyle(wall='brick_dark', base='brick_dark', trim='moss_stone', roof='brick_dark',
          roof_kind='flat', floor='cobble', climb=True), back_door=False, name='Royal crypt')
    fence(w, [(x - 420, z - 120), (x - 420, z + 380), (x - 90, z + 380)], 70, 'iron')
    fence(w, [(x + 90, z + 380), (x + 420, z + 380), (x + 420, z - 120)], 70, 'iron')
    hx, hz = x + 520, z + 120
    house(w, hx, hz, 300, 280, face_yaw(hx, hz, x, z), 1, style(roof='thatch', roof_kind='gable', tint=(0.9, 0.88, 0.85)), back_door=False, name='Dampe\'s hut', roof_rise=130)
    w.box(x, y, z + 260, 90, 30, 150, 'moss_stone', col='prop', surf='stone')
    chest(w, x - 120, y, z + 260, 'royal tomb')
    chest(w, x + 230, H(x + 230, z - 120), z - 120, 'graveyard corner')
    for k in range(5):
        dead_tree(w, x - 520 + k * 260, z + 560 + (k % 2) * 90, 0.9 + 0.15 * k, 70 + k)

# ---- the Gerudo desert: Gerudo Fortress, the Haunted Wasteland, the Desert Colossus ------------------------------------------------
GS = dict(wall='sandstone', base='sandstone', trim='sandstone', roof='sandstone', roof_kind='flat', floor='sand', tint=(1.0, 0.97, 0.92), climb=True)

def gerudo_fortress(w):
    cx, cz = region(w, 'Gerudo Fortress')
    y = H(cx, cz)
    ang = lambda tx, tz: math.atan2(tz - cz, tx - cx)
    gates = [(ang(-4400, 200), 240), (ang(-4200, 2100), 240)]
    ring_wall(w, cx, cz, 790, 50, y - 40, y + 230, 'sandstone', gaps=gates, n=14, climb=True)
    for a, wd in gates: gate_posts(w, cx, cz, 790, a, wd, mat='sandstone', hgt=300)
    # the fortress: four two-floor blocks with roof terraces round the training yard, two watchtowers
    for i, a in enumerate([1.7, 2.6, 3.5, 4.4]):
        x, z = cx + 500 * math.cos(a), cz + 500 * math.sin(a)
        house(w, x, z, 380, 360, face_yaw(x, z, cx, cz), 2, HouseStyle(**GS), name=f'Gerudo fortress block {i}', stair_landing=True, back_door=False)
    for k, a in enumerate([-0.1]):
        x, z = cx + 560 * math.cos(a), cz + 560 * math.sin(a)
        watch_tower(w, x, z, 240, 480, 'sandstone', 'roof_red', yaw=face_yaw(x, z, cx, cz), name=f'Gerudo watchtower {k}')
    # the training yard: target posts, tents and sandstone blocks to hide behind; the middle is open (the Dune boss)
    for k, a in enumerate([0.2, 0.9, 2.15, 3.05, 3.95, 5.4]):
        px, pz = cx + 250 * math.cos(a), cz + 250 * math.sin(a)
        if k % 2: w.box(px, y - 10, pz, 120, 90, 50, 'sandstone', a, surf='sand')
        else: tent(w, px, pz, k)
    for k in range(3):
        a = 2.2 + k * 0.9
        w.cylinder(cx + 150 * math.cos(a), cz + 150 * math.sin(a), 10, y, y + 160, 'timber', n=6, col='prop')
    chest(w, cx + 90, y, cz - 60, 'training yard')
    chest(w, cx - 120, y, cz + 140, 'Gerudo tent')
    rng = rng_for('gerudo palms')
    for k in range(12):
        a = k * 0.52 + rng.uniform(-.1, .1); r = rng.uniform(880, 1000)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200 or H(px, pz) < WATER_Y + 60: continue
        palm(w, px, pz, rng.uniform(0.9, 1.2), 1700 + k)

def haunted_wasteland(w):
    cx, cz = region(w, 'Haunted Wasteland')
    y = H(cx, cz)
    rng = rng_for('wasteland')
    # the flag poles marking the way across the sand
    for k in range(7):
        px, pz = cx - 600 + k * 200, cz - 300 + 110 * math.sin(k)
        py = H(px, pz)
        w.cylinder(px, pz, 8, py - 10, py + 260, 'timber', n=6, col='prop')
        w.face([(px, py + 250, pz), (px + 90, py + 230, pz + 10), (px + 90, py + 180, pz + 10), (px, py + 200, pz)], 'banner_red', double=True)
    house(w, cx + 120, cz + 180, 380, 360, face_yaw(cx + 120, cz + 180, cx, cz - 300), 1, HouseStyle(**GS), name='Wasteland shelter', back_door=False)
    arch(w, cx - 250, cz - 80, 0.4, span=200, hgt=260, mat='sandstone')
    for k in range(6):
        a = k * 1.05 + 0.3; r = 300 + 40 * (k % 2)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if math.hypot(px - cx - 120, pz - cz - 180) < 280: continue
        w.cylinder(px, pz, 30, H(px, pz) - 20, H(px, pz) + rng.uniform(60, 200), 'sandstone', n=7, surf='sand')
    chest(w, cx - 300, H(cx - 300, cz + 150), cz + 150, 'wasteland ruin')
    for k in range(4):
        dead_tree(w, cx + 500 * math.cos(k * 1.6 + 2), cz + 500 * math.sin(k * 1.6 + 2), 1.0, 1800 + k)
    for k in range(8):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(450, 700)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 160 or H(px, pz) < WATER_Y + 60: continue
        rock(w, px, pz, rng.uniform(1.0, 1.6), 1810 + k, 'sandstone', solid=k % 2 == 0)

def desert_colossus(w):
    cx, cz = region(w, 'Desert Colossus')
    py = H(cx, cz)
    gs = HouseStyle(**GS)
    # the Spirit Temple's stepped terraces with ramps, a shrine with a chest on top
    tiers = [(520, py, py + 100), (380, py + 100, py + 200), (240, py + 200, py + 300)]
    for k, (s_, y0, y1) in enumerate(tiers):
        w.box(cx, y0 - (60 if k == 0 else 0), cz - 40, s_, y1 - y0 + (60 if k == 0 else 0), s_, 'sandstone', surf='sand')
        run = (y1 - y0) / 0.5
        zf = cz - 40 + s_ / 2
        rx = cx + (90 if k % 2 else -90)
        w.ramp(rx, zf + run, rx, zf, 90, y0, y1, 'sandstone', surf='sand', base=y0 - 5)
        w.walkways.append((rx, zf + run, rx, zf, 45, y0, y1))
        w.buildings.append((cx, cz - 40, s_ / 2, s_ / 2, y1, 0.0))
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.cylinder(cx + sx * 90, cz - 40 + sz * 90, 16, py + 300, py + 440, 'sandstone', n=6, col='prop')
    w.hip(cx, cz - 40, 230, 230, py + 440, 90, 'sandstone', overhang=8, col=None)
    chest(w, cx, py + 300, cz - 40, 'Spirit Temple top')
    colossus(w, cx + 40, cz - 540)
    for i, (x, z) in enumerate([(cx - 470, cz + 360), (cx + 480, cz + 330)]):
        house(w, x, z, 380, 360, face_yaw(x, z, cx, cz), 1, gs, name=f'Colossus house {i}', back_door=False)
    for k in range(4):
        z = cz - 250 + k * 90
        w.cylinder(cx + 360, z, 22, py - 10, py + 260, 'sandstone', n=6, col='prop')
        w.box(cx + 360, py + 260, z, 60, 24, 60, 'sandstone', col='none')
    # the Great Fairy's fountain in the cliff's lee
    fx, fz = cx - 600, cz - 300
    fy = H(fx, fz)
    ring_wall(w, fx, fz, 120, 26, fy - 10, fy + 40, 'sandstone', n=12)
    w.cylinder(fx, fz, 106, fy + 24, fy + 28, 'water', n=12, col='none')
    chest(w, fx, H(fx, fz + 200), fz + 200, 'fairy fountain')
    rng = rng_for('colossus palms')
    for k in range(10):
        a = k * 0.63 + 0.1; r = rng.uniform(760, 900)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200: continue
        if rng.random() < 0.5: palm(w, px, pz, rng.uniform(0.9, 1.2), 1900 + k)
        else: rock(w, px, pz, rng.uniform(1.0, 1.5), 1900 + k, 'sandstone', solid=True)

# ---- Snowpeak ----------------------------------------------------------------------------------------------------------------------
SNOWS = dict(wall='timber', base='town_stone', trim='planks', roof='snow', roof_kind='gable', floor='planks', tint=(1.0, 0.96, 0.92), window='window_lit')

def snowpeak_lodge(w):
    cx, cz = region(w, 'Snowpeak Lodge')
    ss = HouseStyle(**SNOWS)
    # the mansion at the back of the shelf (two floors), cabins round the yard; the yard in the middle is open (the Frost boss)
    mx, mz = cx + 480 * math.cos(-2.2), cz + 480 * math.sin(-2.2)
    house(w, mx, mz, 520, 400, face_yaw(mx, mz, cx, cz), 2, ss, name='Snowpeak mansion', roof_rise=230, door_width=130, stair_landing=True, back_door=False)
    for i, a in enumerate([-0.6, 0.8, 2.3, 3.3]):
        x, z = cx + 500 * math.cos(a), cz + 500 * math.sin(a)
        house(w, x, z, 300, 300, face_yaw(x, z, cx, cz), 1, ss, back_door=False, name=f'Snowpeak cabin {i}', roof_rise=150)
    # the frozen pond, snowmen, the lookout platform
    px, pz = cx + 160, cz + 230
    py = H(px, pz)
    w.cylinder(px, pz, 150, py - 20, py + 3, 'ice', n=16, surf='ice')
    for k in range(3): snowman(w, cx - 200 + k * 120, cz - 160 + 40 * k)
    lx, lz = cx + 640 * math.cos(1.55), cz + 640 * math.sin(1.55)
    deck(w, lx, lz, 220, 220, H(lx, lz) + 170, face_yaw(lx, lz, cx, cz), name='Snowpeak lookout', ramp='x')
    chest(w, cx - 60, H(cx - 60, cz + 60), cz + 60, 'Snowpeak yard')
    rng = rng_for('snowpeak pines')
    placed = 0
    for k in range(400):
        x = cx + rng.uniform(-1500, 1500); z = cz + rng.uniform(-1300, 1500)
        if placed > 40: break
        if H(x, z) < 450 or terrain.slope_up(np.array(x), np.array(z)) < 0.75: continue
        if math.hypot(x - cx, z - cz) < 860 or terrain.road_dist(np.array(x), np.array(z)) < 180 or near_any(x, z, -500): continue
        pine(w, x, z, rng.uniform(0.9, 1.3), 2000 + k, snow=True); placed += 1

def snowpeak_summit(w):
    cx, cz = region(w, 'Snowpeak Summit')
    y = H(cx, cz)
    rng = rng_for('summit')
    pts = [(cx + 330 * math.cos(a), cz + 330 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 9)]
    for k in range(8):
        if k in (2, 6): continue
        broken_wall(w, pts[k], pts[k + 1], 220, 'castle_stone', rng=rng, gaps=0)
    watch_tower(w, cx + 120, cz - 100, 240, 420, 'castle_stone', 'snow', yaw=face_yaw(cx + 120, cz - 100, cx - 400, cz + 200), name='Summit watchtower')
    for k in range(5):
        a = k * 1.25 + 0.6; r = rng.uniform(500, 650)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 160: continue
        rock(w, px, pz, rng.uniform(1.0, 1.4), 2100 + k, 'ice', solid=True)
    chest(w, cx - 150, y, cz + 80, 'summit ruin')
    snowman(w, cx - 60, cz - 200)

# ---- Lanayru Spring --------------------------------------------------------------------------------------------------------------
def lanayru_spring(w):
    cx, cz = region(w, 'Lanayru Spring')
    x0, x1, z0, z1, lv = POOLS[1]
    px, pz = (x0 + x1) / 2, (z0 + z1) / 2
    # the goddess plinth in the spring (a step out of the water), the pillar ring, the shrine pavilion on the north rim
    w.cylinder(px, pz, 80, H(px, pz) - 20, lv + 18, 'castle_stone', n=10, surf='stone')
    w.far = True
    w.cylinder(px, pz, 26, lv + 18, lv + 200, 'castle_stone', n=8, col='none')
    w.sphere(px, lv + 230, pz, 34, 40, 34, 'castle_stone', rings=3, segs=8)
    w.far = False
    chest(w, px + 40, lv + 18, pz, 'spring plinth')
    for k in range(10):
        a = k * 2 * math.pi / 10
        qx, qz = px + 330 * math.cos(a), pz + 300 * math.sin(a)
        w.cylinder(qx, qz, 26, H(qx, qz) - 20, H(qx, qz) + (260 if k % 3 else 120), 'castle_stone', n=8, surf='stone', climb=(k % 4 == 0))
    sx, sz = px, pz + 520
    sy = H(sx, sz)
    w.cylinder(sx, sz, 170, sy - 30, sy + 14, 'castle_stone', n=8, surf='stone')
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(sx + 140 * math.cos(a), sz + 140 * math.sin(a), 14, sy + 14, sy + 190, 'castle_stone', n=6, col='prop')
    w.cone(sx, sz, 200, sy + 190, sy + 300, 'roof_blue', n=8)
    chest(w, sx, sy + 14, sz, 'spring shrine')
    # the cliff with the spring's falls behind the pool
    w.far = True
    w.prism([(px - 360, pz - 360), (px + 360, pz - 360), (px + 360, pz - 470), (px - 360, pz - 470)], H(px, pz - 420) - 30, lv + 420, 'moss_stone', surf='stone')
    w.far = False
    waterfall(w, px, pz - 350, 0, 1, 240, lv + 400, lv, lean=50)
    for k in range(6):
        a = k * 1.05 + 0.4; r = 620
        rock(w, px + r * math.cos(a), pz + r * math.sin(a), 1.0, 2200 + k, 'moss_stone', solid=k % 2 == 0)

# ---- Goron City, Death Mountain and its trail ---------------------------------------------------------------------------------------
GORON = dict(wall='lava_rock', base='lava_rock', trim='brick_dark', roof='brick_dark', roof_kind='flat', floor='cobble', tint=(1.0, 0.9, 0.84), window='window_lit', climb=True)

def goron_city(w):
    cx, cz = region(w, 'Goron City')
    y = H(cx, cz)
    # the great hall terraces (off the middle, the plaza stays open for the Lava boss), the rock houses round the plaza, the big vase
    tx, tz = cx + 470 * math.cos(0.9), cz + 470 * math.sin(0.9)
    w.cylinder(tx, tz, 300, y - 40, y + 100, 'lava_rock', n=14, surf='stone')
    w.cylinder(tx, tz, 180, y + 100, y + 200, 'lava_rock', n=12, surf='stone')
    w.buildings.append((tx, tz, 210, 210, y + 100, 0.0)); w.buildings.append((tx, tz, 125, 125, y + 200, 0.0))
    ux, uz = math.cos(0.9 + math.pi), math.sin(0.9 + math.pi)
    nx, nz = -uz, ux
    a0 = (tx + ux * 300 + nx * 220, tz + uz * 300 + nz * 220)
    w.ramp(a0[0] - nx * 200, a0[1] - nz * 200, a0[0] + nx * 0, a0[1] + nz * 0, 90, y, y + 100, 'lava_rock', base=y - 10)
    w.walkways.append((a0[0] - nx * 200, a0[1] - nz * 200, a0[0], a0[1], 45, y, y + 100))
    b0 = (tx + ux * 180 - nx * 120, tz + uz * 180 - nz * 120)
    w.ramp(b0[0] + nx * 220, b0[1] + nz * 220, b0[0], b0[1], 80, y + 100, y + 200, 'lava_rock', base=y + 90)
    w.walkways.append((b0[0] + nx * 220, b0[1] + nz * 220, b0[0], b0[1], 40, y + 100, y + 200))
    w.far = True
    w.cylinder(tx, tz, 60, y + 200, y + 300, 'gold', n=10, col='none', r1=40)
    w.far = False
    chest(w, tx + 90, y + 200, tz, 'Goron hall top'); chest(w, tx - 220, y + 100, tz + 60, 'Goron hall terrace')
    for i, a in enumerate([2.3, 3.2, 4.5, 5.4]):
        x, z = cx + 540 * math.cos(a), cz + 540 * math.sin(a)
        house(w, x, z, 380, 360, face_yaw(x, z, cx, cz), 1 if i % 2 else 2, HouseStyle(**GORON), name=f'Goron house {i}', back_door=False, stair_landing=i % 2 == 0)
    # bomb flowers, lava pools, the rolling Goron's boulders
    for k in range(6):
        a = k * 1.05 + 0.5; r = 230
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        w.sphere(px, H(px, pz) + 18, pz, 20, 18, 20, 'leaves_dark', rings=3, segs=6)
    for k, (dx, dz) in enumerate([(-160, 120), (140, -170)]):
        w.cylinder(cx + dx, cz + dz, 70, y - 30, y + 3, 'lava_rock', n=9, col='none')
    chest(w, cx + 40, y, cz + 40, 'Goron plaza')
    rng = rng_for('goron rocks')
    for k in range(10):
        a = k * 0.63 + 0.3; r = rng.uniform(820, 1000)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 200 or near_any(px, pz, -600): continue
        rock(w, px, pz, rng.uniform(1.1, 1.7), 2300 + k, 'lava_rock', solid=True)

def death_mountain(w):
    cx, cz = region(w, 'Death Mountain')
    y = H(cx, cz)
    # the Fire Temple's gate at the back of the crater, lava pools, the warp pad, scorched rocks
    gx, gz = cx + 220, cz + 160
    house(w, gx, gz, 380, 360, face_yaw(gx, gz, cx - 300, cz - 300), 1, HouseStyle(**GORON), name='Fire Temple gate', back_door=False)
    for k, (dx, dz, r) in enumerate([(-180, 120, 80), (120, -200, 70)]):
        w.cylinder(cx + dx, cz + dz, r, y - 30, y + 3, 'lava_rock', n=9, col='none')
    w.cylinder(cx - 140, cz - 60, 90, y - 20, y + 16, 'brick_dark', n=10, surf='stone')
    chest(w, cx - 140, y + 16, cz - 60, 'Bolero warp pad')
    rng = rng_for('crater rocks')
    for k in range(8):
        a = k * 0.78 + 0.2; r = rng.uniform(280, 360)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if math.hypot(px - gx, pz - gz) < 260 or terrain.road_dist(np.array(px), np.array(pz)) < 120: continue
        rock(w, px, pz, rng.uniform(0.9, 1.3), 2400 + k, 'lava_rock', solid=k % 2 == 0)
    # the pass up from Goron City: boulders and a stone arch where it cuts through the rim
    (ax, az), (bx, bz), hw = CUTS[0]
    mx, mz = ax + (bx - ax) * 0.62, az + (bz - az) * 0.62
    arch(w, mx, mz, math.atan2(bz - az, bx - ax) + math.pi / 2, span=260, hgt=280, mat='lava_rock')
    chest(w, mx + 40, H(mx + 40, mz), mz, 'crater pass')

def dm_trail(w):
    cx, cz = region(w, 'Death Mountain Trail')
    y = H(cx, cz)
    rng = rng_for('trail')
    # a Goron camp (tents round a fire ring), the owl's stone, a lookout deck over the drop, boulders and bomb flowers
    for k, a in enumerate([0.4, 1.6, 2.8]):
        tent(w, cx + 220 * math.cos(a), cz + 220 * math.sin(a), k)
    ring_wall(w, cx, cz, 60, 18, y - 10, y + 24, 'lava_rock', n=8)
    chest(w, cx + 220 * math.cos(1.0), y, cz + 220 * math.sin(1.0), 'Goron camp')
    w.cylinder(cx - 260, cz - 180, 40, y - 20, y + 160, 'moss_stone', n=8, surf='stone')
    w.sphere(cx - 260, y + 200, cz - 180, 50, 45, 50, 'moss_stone', rings=3, segs=7)
    lx, lz = cx + 380 * math.cos(-0.3), cz + 380 * math.sin(-0.3)
    deck(w, lx, lz, 220, 220, H(lx, lz) + 160, face_yaw(lx, lz, cx, cz), name='trail lookout', ramp='x')
    for k in range(9):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(420, 620)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 170 or math.hypot(px - lx, pz - lz) < 400: continue
        rock(w, px, pz, rng.uniform(1.0, 1.6), 2500 + k, 'lava_rock' if k % 2 else 'rock', solid=True)
    chest(w, cx - 120, y, cz + 220, 'trail boulders')

# ---- the land between the places ----------------------------------------------------------------------------------------------------
def field(w):
    """Trees, bushes, rocks and old wall stumps over the land between the places: the woods thick, the open field dotted with cover
    (zero build: every open stretch has something to duck behind within a sprint)."""
    rng = rng_for('field')
    pts = poisson(rng, (-HALF_X + 500, -HALF_Z + 500), (HALF_X - 500, HALF_Z - 500), 330, 4200)
    n = 0
    for x, z in pts:
        h = H(x, z)
        if h < WATER_Y + 50: continue
        if terrain.slope_up(np.array(x), np.array(z)) < 0.8: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 190: continue
        if near_any(x, z, 380): continue
        d_r, _, _ = terrain.seg_dist(np.array(x), np.array(z), RIVER)
        if d_r < 520: continue
        if any(x0 - 150 <= x <= x1 + 150 and z0 - 150 <= z <= z1 + 150 for x0, x1, z0, z1, py in terrain.ALL_POOLS): continue
        X, Z = np.array(x), np.array(z)
        woods = max(float(terrain.region(X, Z, FOREST, 700)), float(terrain.tree_density(X, Z)))
        snow = float(terrain.region(X, Z, SNOW, 700)) > 0.5 or h > PEAK_SNOW
        desert = float(terrain.region(X, Z, DESERT, 700)) > 0.5
        red = float(terrain.region(X, Z, REDROCK, 700)) > 0.5
        pick = rng.random()
        if snow:
            if pick < 0.7: pine(w, x, z, rng.uniform(0.9, 1.3), 3000 + n, snow=True)
            elif pick < 0.85: rock(w, x, z, rng.uniform(1.0, 1.5), 3000 + n, 'ice', solid=False)
            else: continue
        elif desert:
            if pick < 0.35:
                sc = rng.uniform(1.0, 1.7); rock(w, x, z, sc, 3000 + n, 'sandstone', solid=sc > 1.45)
            elif pick < 0.5: palm(w, x, z, rng.uniform(0.9, 1.2), 3000 + n)
            elif pick < 0.6: dead_tree(w, x, z, rng.uniform(0.9, 1.2), 3000 + n)
            elif pick < 0.65: broken_wall(w, (x - 120, z), (x + 120, z + 60), 180, 'sandstone', rng=rng, gaps=0)
            else: continue
        elif red:
            if pick < 0.45:
                sc = rng.uniform(1.0, 1.8); rock(w, x, z, sc, 3000 + n, 'lava_rock', solid=sc > 1.5)
            elif pick < 0.6: dead_tree(w, x, z, rng.uniform(0.9, 1.2), 3000 + n)
            else: continue
        elif woods > 0.5:
            if pick < 0.6: oak(w, x, z, rng.uniform(1.0, 1.5), 3000 + n, leaves='leaves_dark' if rng.random() < 0.5 else 'leaves')
            elif pick < 0.8: pine(w, x, z, rng.uniform(1.0, 1.4), 3000 + n)
            elif pick < 0.92: bush(w, x, z, rng.uniform(0.9, 1.3), 3000 + n)
            else: rock(w, x, z, rng.uniform(1.0, 1.4), 3000 + n, 'moss_stone', solid=False)
        else:
            if pick < 0.3: oak(w, x, z, rng.uniform(0.9, 1.3), 3000 + n)
            elif pick < 0.42: pine(w, x, z, rng.uniform(0.9, 1.2), 3000 + n)
            elif pick < 0.62:
                bush(w, x, z, rng.uniform(0.9, 1.3), 3000 + n); bush(w, x + 70, z + 30, rng.uniform(0.7, 1.0), 3100 + n)
            elif pick < 0.82:
                sc = rng.uniform(1.0, 1.6); rock(w, x, z, sc, 3000 + n, solid=sc > 1.45)
            elif pick < 0.87:
                a = rng.uniform(0, math.pi)
                broken_wall(w, (x - 150 * math.cos(a), z - 150 * math.sin(a)), (x + 150 * math.cos(a), z + 150 * math.sin(a)), 200, rng=rng, gaps=0)
            else: continue
        n += 1
    for (x, z, yaw) in [(-700, -1700, 0.2), (-2400, -1300, 0.9), (-1900, -2700, 2.3), (500, 1900, 0.5), (2600, 3300, 1.1), (4600, 2200, 0.0), (-3900, -2200, 1.4)]:
        signpost(w, x + 90, z + 70, yaw)

_WORLD = [None]

_WORLD = [None]

def near_any(x, z, margin):
    w = _WORLD[0]
    if w is not None:
        for bx, bz, hw, hd, *_ in w.buildings:
            if math.hypot(x - bx, z - bz) < max(hw, hd) + 200: return True
        for lx, ly, lz, *_ in w.loot:
            if math.hypot(x - lx, z - lz) < 180: return True
    for name, px, pz, rad, *_ in POIS:
        if math.hypot(x - px, z - pz) < rad * 0.75 + margin: return True
    for px, pz in CART_SPOTS + [(sx, sz) for sx, sz, k in ALLY_SPOTS] + [SPAWN]:
        if math.hypot(x - px, z - pz) < 220: return True
    return False

BUILDERS = [
    ('Castle Town', castle_town), ('Hyrule Castle', hyrule_castle), ('Temple of Time', temple_of_time), ('Hyrule Field Ruins', field_ruins),
    ('Lake Hylia', lake_hylia), ('Hylia Docks', hylia_docks), ('Ordon Village', ordon_village), ('Lon Lon Ranch', lon_lon_ranch),
    ('Kokiri Forest', kokiri_forest), ('Lost Woods', lost_woods), ('Great Hylia Bridge', great_hylia_bridge), ("Zora's River", zoras_river),
    ("Zora's Domain", zoras_domain), ('Kakariko Village', kakariko_village), ('Kakariko Graveyard', kakariko_graveyard),
    ('Gerudo Fortress', gerudo_fortress), ('Haunted Wasteland', haunted_wasteland), ('Desert Colossus', desert_colossus),
    ('Snowpeak Lodge', snowpeak_lodge), ('Snowpeak Summit', snowpeak_summit), ('Lanayru Spring', lanayru_spring), ('Goron City', goron_city),
    ('Death Mountain', death_mountain), ('Death Mountain Trail', dm_trail), ('Hyrule Field', field),
]

def build_all(w, only=None):
    _WORLD[0] = w
    for name, fn in BUILDERS:
        if isinstance(only, str): only = only.split(',')
        if only and 'all' not in only and name not in only: continue
        fn(w)
