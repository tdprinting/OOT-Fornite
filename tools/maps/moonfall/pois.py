"""Every place on Moonfall Isle, built from the kit (kit.py, sized from adult Link). Positions come from layout.py. Nothing on this map is
scattered at random when a match starts: every building, bridge, tree and rock is laid here, once, and baked into the map."""
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

def stall(w, x, z, yaw, k):
    y = H(x, z)
    f = Frame(x, z, yaw)
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = f.p(sx * 60, sz * 40)
            w.box(px, y, pz, 7, 120, 7, 'timber', yaw, col='none')
    w.hip(x, z, 136, 96, y + 120, 32, 'roof_red' if k % 2 else 'roof_blue', yaw, overhang=10, col=None)
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
        # a landing post at each corner
        w.box(b[0], b[1], b[2], 30, 70, 30, parapet, col='none')

# ---- Hyrule Castle, its road and the falls ----------------------------------------------------------------------------------------
PY = 620   # the plateau

def hyrule_castle(w):
    cx, cz = region(w, 'Hyrule Castle')
    py = PY
    x0, x1, z0, z1 = cx - 680, cx + 680, cz - 780, cz + 420      # curtain walls; the gate is in the south wall (z1)
    t = 70; top = py + 230                                         # the wall walk is 70 wide: a bot's lattice always lands on it
    g = 170
    south = [(cx - g / 2, z1), (x0, z1), (x0, z0), (cx + 200, z0), (cx + 200, z0 + t), (x0 + t, z0 + t), (x0 + t, z1 - t), (cx - g / 2, z1 - t)]
    north = [(cx + g / 2, z1 - t), (x1 - t, z1 - t), (x1 - t, z0 + t), (cx + 310, z0 + t), (cx + 310, z0), (x1, z0), (x1, z1), (cx + g / 2, z1)]
    w.prism(south, py - 60, top, 'castle_stone', surf='stone', climb=(1,))         # the west wall's outside is ivy: a way in without the gate
    w.prism(north, py - 60, top, 'castle_stone', surf='stone')
    # battlements along the wall walk (drawn)
    for (ax, az, bx, bz) in [(x0, z1, x0, z0), (x0, z0, x1, z0), (x1, z0, x1, z1), (x0, z1, cx - g / 2, z1), (cx + g / 2, z1, x1, z1)]:
        L = math.hypot(bx - ax, bz - az); n = int(L / 70)
        for k in range(0, n, 2):
            u = (k + .5) / n; px, pz = ax + (bx - ax) * u, az + (bz - az) * u
            w.box(px, top, pz, 34 if az == bz else t, 40, t if az == bz else 34, 'castle_stone', col='none')
    # Ramps up to the wall walk: along the inside of the west and east walls, from the courtyard's south end up to the wall top
    for rx in (x0 + t + 45, x1 - t - 45):
        w.ramp(rx, z1 - t - 55, rx, z1 - t - 55 - 440, 90, py, top, 'castle_stone', base=py - 10)
    # and from the wall walk up onto the two chest towers (north east along the east wall, south west along the west wall; the north wall walk
    # is cut by the spire)
    w.ramp(x1 - t / 2, z0 + 560, x1 - t / 2, z0 + 60, t - 10, top, top + 170, 'castle_stone', base=top)
    w.ramp(x0 + t / 2, z1 - 480, x0 + t / 2, z1 - 120, t - 10, top, top + 170, 'castle_stone', base=top)
    chest(w, x0 + t / 2, top, cz - 300, 'west wall walk')
    chest(w, x1 - t / 2, top, cz + 100, 'east wall walk')
    # Corner towers (round, teal spires), one with ivy to climb
    for k, (tx, tz) in enumerate([(x0, z0), (x1, z0), (x1, z1), (x0, z1)]):
        w.cylinder(tx, tz, 110, py - 60, top + 170, 'castle_stone', n=10, surf='stone', climb=(k == 3))
        w.far = True
        w.cylinder(tx, tz, 122, top + 170, top + 192, 'castle_stone', n=10, col='none')
        w.cone(tx, tz, 126, top + 192, top + 520, 'roof_blue', n=10)
        w.cylinder(tx, tz, 4, top + 520, top + 570, 'gold', n=4, col='none')
        w.far = False
        if k in (1, 3): chest(w, tx, top + 170, tz, 'castle tower top')
    # Gatehouse: two square towers flanking the gate, a lintel walk between them
    for side in (-1, 1):
        w.box(cx + side * (g / 2 + 65), py - 60, z1 - 15, 130, top + 110 - py + 60, 150, 'castle_stone', surf='stone')
        w.far = True
        w.hip(cx + side * (g / 2 + 65), z1 - 15, 130, 150, top + 110, 190, 'roof_blue', overhang=12, col=None)
        w.far = False
        w.decal(cx + side * (g / 2 + 65), py + 120, z1 + 61, 60, 110, 0, 1, 'banner_red', off=3)
    w.box(cx, top - 20, z1 - 15, g + 10, 60, 150, 'castle_stone', surf='stone')
    for sx in (-1, 1): lamp(w, cx + sx * (g / 2 + 20), z1 + 90, py)
    # The keep: a great hall (two floors) with a flat roof, and the tall central spire behind it
    house(w, cx, cz - 110, 520, 400, 0.0, storeys=2, style=style(wall='castle_stone', base='castle_stone', trim='castle_stone', roof='roof_blue',
          roof_kind='flat', floor='cobble', window='window_lit', tint=(1, 1, 1)), name='Castle keep', floor_y=py + 10, door_width=150)
    w.far = True
    spiral_tower(w, cx, cz - 470, 300, 760, yaw=0.0, wall='castle_stone', floor='cobble', roof='roof_blue', name='Castle spire', cap='spire', y0=py + 10)
    w.far = False
    # The spire's own entrance across the rear terrace
    w.ramp(cx, cz - 310, cx, cz - 330, 150, py + 10, py + 10, 'castle_stone', surf='stone', base=py - 20)
    w.walkways.append((cx, cz - 300, cx, cz - 330, 75, py + 10, py + 10))
    # Side spires on the keep's corners (drawn, seen from everywhere)
    for sx in (-1, 1):
        for sz in (-1, 1):
            x = cx + sx * 235; z = cz - 110 + sz * 175
            yb = py + 10 + 2 * STOREY + 60
            w.cylinder(x, z, 50, yb, yb + 180, 'castle_stone', n=8, col='none')
            w.cone(x, z, 64, yb + 180, yb + 400, 'roof_blue', n=8)
    w.far = False
    # Courtyard: a fountain, hedges, banners on the walls
    fy = py
    fx_, fz_ = cx - 400, cz + 180      # off the gate's line into the keep
    ring_wall(w, fx_, fz_, 90, 26, fy, fy + 40, 'castle_stone', n=12)
    w.cylinder(fx_, fz_, 76, fy + 26, fy + 30, 'water', n=12, col='none')
    w.cylinder(fx_, fz_, 20, fy, fy + 130, 'castle_stone', n=8, surf='stone')
    w.cylinder(fx_, fz_, 48, fy + 100, fy + 112, 'castle_stone', n=8, col='none')
    chest(w, cx + 400, fy, cz + 180, 'courtyard')
    for x in (cx - 320, cx - 230, cx + 230, cx + 320):
        bush(w, x, cz + 260, 0.7, int(x) & 255, 'leaves_dark', y=fy)
    for x in np.linspace(x0 + 120, x1 - 120, 6):
        w.decal(x, fy + 80, z0 + t + 1, 50, 150, 0, 1, 'banner_blue', off=2)
    # Pines round the plateau's edge
    rng = rng_for('castle pines')
    for k in range(30):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(800, 950)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if abs(x - cx) < 260 and z > cz + 300: continue    # keep the gate's approach clear
        if x > cx + 250 and z > cz + 650: continue           # and the garden lake
        if H(x, z) < py - 30: continue
        pine(w, x, z, rng.uniform(0.8, 1.1), k)
    castle_road(w)
    garden_lake(w)

def castle_road(w):
    """The switchback of stone ramps from the gate down the plateau's south face to the meadow, and the west road's start."""
    cx, cz = POI_AT['Hyrule Castle']
    gate = (cx, PY, cz + 420 + 100)
    foot_x, foot_z = cx - 150, -2500
    foot_y = H(foot_x, foot_z) + 4
    # three legs: east along the cliff top, back west and down, then down to the road's foot
    pts = [gate, (cx, PY, cz + 650), (cx + 520, PY - 230, cz + 930), (cx - 200, PY - 440, cz + 1180), (foot_x, foot_y, foot_z)]
    pts = [(cx, PY, cz + 640)] + pts[2:]
    # legs rise at most 0.5
    fixed = [(cx, PY, cz + 640)]
    for p in pts[1:]:
        a = fixed[-1]; L = math.hypot(p[0] - a[0], p[2] - a[2]); drop = min(a[1] - p[1], L * 0.5)
        fixed.append((p[0], a[1] - drop, p[2]))
    stone_ramp_road(w, fixed, width=210)
    # the gate's square between the gatehouse and the road's head
    w.prism(rect(cx, cz + 600, 420, 300), PY - 40, PY + 2, 'cobble', surf='stone')
    chest(w, fixed[2][0] + 60, fixed[2][1], fixed[2][2] + 40, 'castle road landing')

def garden_lake(w):
    """The castle's lake on the plateau's south east edge and the waterfall it pours down the cliff into the Castle Falls pool."""
    x0, x1, z0, z1, py = POOLS[0]
    # stone rim round the lake (the water box is the game's), a willow and a bench
    for (ax, az, bx, bz) in [(x0, z0, x1, z0), (x1, z0, x1, z1), (x0, z1, x0, z0)]:
        L = math.hypot(bx - ax, bz - az)
        beam(w, (ax, py + 20, az), (bx, py + 20, bz), 30, 50, 'castle_stone', block=True)
    oak(w, x0 - 120, z1 + 60, 0.9, 11, leaves='leaves', y=PY - 10)
    # the fall: sheets of water from the lake's south edge down the cliff to the pool (drawn), foam at the foot
    fx, fz = POI_AT['Castle Falls']
    top = py - 2
    w.far = True
    for k in range(5):
        xa = x0 + 60 + k * 80; xb = xa + 80
        w.face([(xa - 120, WATER_Y, fz - 300), (xb - 120, WATER_Y, fz - 300), (xb, top, z1 + 20), (xa, top, z1 + 20)], 'water', double=True, uvs=[(0, 5), (1, 5), (1, 0), (0, 0)])
    w.far = False
    w.cylinder(fx - 60, fz - 220, 220, WATER_Y - 4, WATER_Y + 5, 'ice', n=12, col='none')

def castle_falls(w):
    """The pool at the waterfall's foot: stepping stones, a Zora shrine with a chest, and the overlook path from the castle road."""
    px, pz = region(w, 'Castle Falls')
    rng = rng_for('falls')
    for k, (x, z) in enumerate([(px - 300, pz + 120), (px - 120, pz + 220), (px + 100, pz + 180), (px + 300, pz + 60)]):
        w.cylinder(x, z, 55, H(x, z) - 20, WATER_Y + 30, 'moss_stone', n=8, surf='stone')
    sx, sz = px + 540, pz + 200
    sy = H(sx, sz)
    w.cylinder(sx, sz, 150, sy - 40, sy + 14, 'moss_stone', n=10, surf='stone')
    for k in range(5):
        a = k * 2 * math.pi / 5
        w.cylinder(sx + 120 * math.cos(a), sz + 120 * math.sin(a), 15, sy + 14, sy + 190, 'moss_stone', n=6, col='prop')
    w.hip(sx, sz, 270, 270, sy + 190, 90, 'roof_blue', overhang=15, col=None)
    chest(w, sx, sy + 14, sz, 'Zora shrine')
    for k in range(5):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(500, 650)
        x, z = px + r * math.cos(a), pz + r * math.sin(a)
        if H(x, z) < WATER_Y + 30: continue
        rock(w, x, z, rng.uniform(0.8, 1.3), 40 + k, solid=False)

# ---- Clock Town and the Lantern Bridge ---------------------------------------------------------------------------------------------
def clock_town(w):
    cx, cz = region(w, 'Clock Town')
    y = H(cx, cz)
    # The town wall with four gates (west: Lantern Bridge, north: Kakariko, south: Lost Woods, east: the shore)
    # (the north gate sits where the Kakariko road leaves, north east; the wall stands well clear of the houses' footings)
    gates = [(math.pi, 220), (math.atan2(-500, 400), 200), (math.pi / 2, 200), (0.0, 180)]
    ring_wall(w, cx, cz, 980, 40, y - 40, y + 190, 'town_stone', gaps=gates, n=20, climb=True)
    for a, wd in gates:    # gate posts with lamps
        for s in (-1, 1):
            gx, gz = cx + 990 * math.cos(a) + s * (wd / 2 + 30) * -math.sin(a), cz + 990 * math.sin(a) + s * (wd / 2 + 30) * math.cos(a)
            gy = H(gx, gz)
            w.box(gx, gy - 20, gz, 44, 260, 44, 'town_stone', a, surf='stone')
            lamp(w, gx, gz, gy + 240)
    # The clock tower in the middle of the plaza: ramps spiral up inside to a look-out under the bell roof; the west face is ivy to climb
    spiral_tower(w, cx, cz, 340, 720, yaw=0.0, wall='town_stone', floor='cobble', roof='roof_red', clock=True, name='Clock Tower', cap='spire')
    houses = [
        (470, 420, 2, 'flat', 'roof_red', (1.0, 0.95, 0.88), 'Milk Bar'),
        (620, -120, 2, 'hip', 'roof_red', (1.0, 1.0, 1.0), 'Clock Town inn'),
        (380, -520, 2, 'gable', 'roof_blue', (0.92, 0.96, 1.0), 'Clock Town house 2'),
        (-420, -500, 2, 'flat', 'roof_blue', (1.0, 0.92, 0.86), 'Clock Town house 3'),
        (-620, 60, 2, 'gable', 'roof_red', (0.96, 0.92, 0.84), 'Clock Town house 4'),
        (-400, 480, 1, 'hip', 'roof_green', (1.0, 0.97, 0.9), 'Clock Town house 5'),
    ]
    for i, (dx, dz, st, kind, roof, tint, name) in enumerate(houses):
        x, z = cx + dx, cz + dz
        yaw = face_yaw(x, z, cx, cz)
        house(w, x, z, 380, 360 if st > 1 else 320, yaw, storeys=st, style=style(roof=roof, roof_kind=kind, tint=tint), name=name, roof_rise=150)
    # Market stalls round the plaza, lamps, a well
    for k, a in enumerate(np.radians([25, 110, 205, 290])):
        r = 330
        stall(w, cx + r * math.cos(a), cz + r * math.sin(a), a + math.pi / 2, k)
    for a in np.radians([70, 160, 250, 340]):
        lamp(w, cx + 300 * math.cos(a), cz + 300 * math.sin(a))
    chest(w, cx + 240, y, cz - 240, 'plaza corner')
    lantern_bridge(w)

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

def lantern_bridge(w):
    x, z = region(w, 'Lantern Bridge')
    a = (x - 420, H(x - 420, z) + 4, z); b = (x + 420, H(x + 420, z) + 4, z)
    p0, p1, y = stone_bridge(w, a, b, width=190, piers=2)
    for u in (0.25, 0.75):
        for side in (-1, 1):
            lamp(w, p0[0] + (p1[0] - p0[0]) * u, z + side * 70, y)
    chest(w, (p0[0] + p1[0]) / 2, y, z + 30, 'Lantern Bridge middle')

# ---- Temple of Time: the hot drop in the middle ------------------------------------------------------------------------------------
def temple_of_time(w):
    cx, cz = region(w, 'Temple of Time')
    yaw = face_yaw(cx, cz, cx, cz + 2000)     # the facade looks south, to the lagoon
    f = Frame(cx, cz, yaw)
    lo, hi = ground_range(cx, cz, 1000, 760, yaw)
    base = hi + 24
    w.prism(f.outline(rect(0, 0, 1000, 760)), lo - 60, base - 24, 'moss_stone', surf='stone')
    w.prism(f.outline(rect(0, -20, 900, 680)), base - 24, base, 'moss_stone', surf='stone', top_mat='cobble')
    # steps up on every side (ramps)
    for (lx, lz, dx, dz) in [(0, 380, 0, 1), (0, -380, 0, -1), (500, 0, 1, 0), (-500, 0, -1, 0)]:
        px, pz = f.p(lx, lz); ex, ez = f.p(lx + dx * 130, lz + dz * 130)
        w.ramp(ex, ez, px, pz, 200, H(ex, ez) - 4, base, 'moss_stone', base=lo - 40)
    w.buildings.append((cx, cz, 500, 380, base, yaw))
    # broken walls: the hall's sides stand to different heights, the back is mostly gone
    segs = [((-430, -320), (-430, 300), 300), ((430, -320), (430, 40), 240), ((430, 40), (430, 300), 120), ((-430, -320), (-60, -320), 200), ((120, -320), (430, -320), 110)]
    for (ax, az), (bx, bz), hgt in segs:
        a = f.p(ax, az); b = f.p(bx, bz)
        L = math.hypot(b[0] - a[0], b[1] - a[1]); mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        w.box(mx, base, mz, L + 40, hgt, 40, 'moss_stone', ang, surf='stone', climb=(0, 2))
    chest(w, f.p(-390, -100)[0], base, f.p(-390, -100)[1], 'temple west aisle')
    # the facade: two tall pillars and a lintel with the Door of Time between
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
    # columns inside, some fallen
    for k, lz in enumerate(np.linspace(-200, 200, 4)):
        for sx in (-1, 1):
            px, pz = f.p(sx * 230, lz)
            hgt = 360 if (k + (sx > 0)) % 3 else 130
            w.cylinder(px, pz, 28, base, base + hgt, 'moss_stone', n=6, col='prop')
    a = f.p(-330, 60); b = f.p(-190, 230)
    beam(w, (a[0], base + 50, a[1]), (b[0], base + 50, b[1]), 56, 50, 'moss_stone', col='prop')
    # the altar with the three pedestals, and the bell tower stump at the back corner, open at the top
    ax, az = f.p(0, -120)
    w.box(ax, base, az, 220, 40, 110, 'castle_stone', yaw, surf='stone')
    for sx in (-1, 0, 1):
        qx, qz = f.p(sx * 70, -120)
        w.cylinder(qx, qz, 14, base + 40, base + 64, 'gold', n=6, col='none')
    chest(w, ax, base + 40, az, 'temple altar')
    # the bell tower stands on the ground behind the platform's west corner, the vault behind its east corner (both clear of the ruin's walls)
    tx, tz = f.p(-720, -300)
    spiral_tower(w, tx, tz, 300, 520, yaw=yaw, wall='moss_stone', floor='cobble', roof='moss_stone', name='Temple bell tower', cap='hip')
    vx, vz = f.p(720, -320)
    house(w, vx, vz, 360, 360, face_yaw(vx, vz, cx, cz), 1, HouseStyle(wall='moss_stone', base='moss_stone', trim='moss_stone', roof='moss_stone', roof_kind='flat',
          floor='cobble', climb=True), back_door=False, furnish=False, name='Temple vault')
    for k in range(4):
        bush(w, *f.p(-480 + k * 320, -470), 0.8, 30 + k, 'leaves_dark')
    for a in np.radians([30, 150, 250, 290]):
        oak(w, cx + 760 * math.cos(a), cz + 640 * math.sin(a), 1.0, 60 + int(a * 10))

# ---- Snowhead, the Frozen Pond, Moonfall Span and the Fairy Fountain --------------------------------------------------------------
def snowhead(w):
    cx, cz = region(w, 'Snowhead Lodge')
    ss = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='snow', roof_kind='gable', floor='planks', tint=(1.0, 0.96, 0.92), window='window_lit')
    house(w, cx, cz, 520, 400, 0.15, 2, ss, name='Snowhead Lodge', roof_rise=230, door_width=130)
    for i, (dx, dz) in enumerate([(-520, 300), (500, 260), (440, -330)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 300, 300, face_yaw(x, z, cx, cz + 200), 1, ss, back_door=False, name=f'Snowhead cabin {i}', roof_rise=150)
    # the Goron village of Majora's Mask: a rock hut with a round door by the road's end (the Goron ally waits here)
    gx, gz = cx + 420, cz + 260 + 40
    gy = H(cx + 400, cz + 200)
    # a snowy lookout platform on posts at the shelf's edge
    lx, lz = cx - 300, cz - 520
    ly = H(lx, lz) + 170
    w.box(lx, ly - 20, lz, 220, 20, 220, 'planks', surf='wood')
    w.walkways.append((lx - 110, lz, lx + 110, lz, 110, ly, ly))
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.cylinder(lx + sx * 95, lz + sz * 95, 9, H(lx, lz) - 20, ly - 20, 'timber', n=6, col='none')
    w.ramp(lx + 110 + 320, lz, lx + 110, lz, 90, H(lx + 430, lz) - 4, ly, 'planks', surf='wood', base=H(lx + 430, lz) - 60)
    w.walkways.append((lx + 430, lz, lx + 110, lz, 45, H(lx + 430, lz) - 4, ly))
    chest(w, lx, ly, lz, 'snow lookout')
    rng = rng_for('snowhead pines')
    placed = 0
    for k in range(500):
        x = cx + rng.uniform(-2200, 2200); z = cz + rng.uniform(-1800, 1600)
        if placed > 60: break
        h = H(x, z)
        if h < 300 or terrain.slope_up(np.array(x), np.array(z)) < 0.75: continue
        if any(math.hypot(x - bx, z - bz) < 520 for bx, bz in [(cx, cz), (cx - 520, cz + 300), (cx + 500, cz + 260), (cx + 440, cz - 330), (lx, lz), FROZEN_POND[:2]]): continue
        if terrain.road_dist(np.array(x), np.array(z)) < 180: continue
        if math.hypot(x + 1700, z + 4400) < 700: continue
        pine(w, x, z, rng.uniform(0.9, 1.3), 100 + k, snow=True); placed += 1
    frozen_pond(w)
    moonfall_span(w)

def frozen_pond(w):
    px, pz = region(w, 'Frozen Pond')
    pr = FROZEN_POND[2]
    y = H(px, pz)
    w.cylinder(px, pz, pr, y - 20, y + 3, 'ice', n=16, surf='ice')
    ss = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='snow', roof_kind='gable', floor='planks', tint=(1.0, 0.96, 0.92), window='window_lit')
    house(w, px + pr + 170, pz + 40, 260, 260, face_yaw(px + pr + 170, pz + 40, px, pz), 1, ss, back_door=False, name='ice hut', roof_rise=120)
    # a hole in the ice with a fishing rod, and a snowman
    w.cylinder(px - 40, pz + 30, 30, y - 30, y + 4, 'water', n=8, col='none')
    w.cylinder(px + 90, pz - 120, 30, y, y + 36, 'snow', n=6, col='prop'); w.sphere(px + 90, y + 56, pz - 120, 22, 22, 22, 'snow', rings=3, segs=6)
    chest(w, px - pr - 80, H(px - pr - 80, pz), pz, 'frozen shore')

def moonfall_span(w):
    """The high stone bridge over the ravine between the castle's west road and the Snowhead shelf, with lantern chests on a ledge under it."""
    x, z = region(w, 'Moonfall Span')
    a = (x + 440, H(x + 440, z) + 4, z); b = (x - 440, H(x - 440, z) + 4, z)
    p0, p1, y = stone_bridge(w, a, b, width=220, piers=2, towers=True, name='Moonfall Span')
    chest(w, x, y, z + 40, 'span middle')
    for u in (0.2, 0.8):
        lamp(w, p0[0] + (p1[0] - p0[0]) * u, z - 95, y)
    # the pier ledge: a stone shelf halfway down the east pier, reached by the ivy on the pier's south face, with two chests
    px = p0[0] + (p1[0] - p0[0]) / 3
    bed = H(px, z)
    ly = bed + (y - bed) * 0.5
    w.box(px, ly - 20, z + 120, 160, 20, 90, 'castle_stone', surf='stone')
    w.box(px, bed - 40, z + 60, 90, ly - 20 - bed + 40, 60, 'castle_stone', surf='stone', climb=(0, 2))
    chest(w, px - 40, ly, z + 120, 'span ledge lantern'); chest(w, px + 40, ly, z + 120, 'span ledge lantern')
    for k in range(6):
        rock(w, x + (k - 2.5) * 170, z + 400 * (1 if k % 2 else -1), 0.9 + 0.2 * (k % 3), 700 + k, solid=False)

def fairy_fountain(w):
    x, z = region(w, 'Fairy Fountain')
    y = H(x, z)
    w.cylinder(x, z, 200, y - 30, y + 10, 'moss_stone', n=14, surf='stone')
    ring_wall(w, x, z, 150, 28, y + 10, y + 50, 'moss_stone', n=14)
    w.cylinder(x, z, 136, y + 24, y + 28, 'water', n=14, col='none')
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(x + 250 * math.cos(a), z + 250 * math.sin(a), 20, y, y + 190, 'moss_stone', n=6, col='prop')
    chest(w, x, y + 50, z, 'fairy fountain')
    chest(w, x + 300, H(x + 300, z + 200), z + 200, 'fountain hollow')
    rng = rng_for('fountain pines')
    for k in range(14):
        a = k * 0.45 + 0.2; r = rng.uniform(380, 560)
        px, pz = x + r * math.cos(a), z + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 170: continue
        pine(w, px, pz, rng.uniform(1.0, 1.4), 200 + k)

# ---- The desert: Spirit Colossus, Spirit Oasis -----------------------------------------------------------------------------------
def spirit_colossus(w):
    cx, cz = region(w, 'Spirit Colossus')
    py = H(cx, cz)
    gs = HouseStyle(wall='sandstone', base='sandstone', trim='sandstone', roof='sandstone', roof_kind='flat', floor='sand', tint=(1.0, 0.97, 0.92), climb=True)
    ring_wall(w, cx, cz, 900, 50, py - 40, py + 200, 'sandstone', gaps=[(math.pi * 1.05, 220), (math.pi * 0.25, 200), (math.pi * 1.6, 200)], n=16, climb=True)
    # The stepped temple in the middle: three terraces with ramps, a shrine with a chest on top
    tiers = [(520, py, py + 100), (380, py + 100, py + 200), (240, py + 200, py + 300)]
    for k, (s, y0, y1) in enumerate(tiers):
        w.box(cx, y0 - (60 if k == 0 else 0), cz - 40, s, y1 - y0 + (60 if k == 0 else 0), s, 'sandstone', surf='sand')
        run = (y1 - y0) / 0.5
        zf = cz - 40 + s / 2
        w.ramp(cx + (90 if k % 2 else -90), zf + run, cx + (90 if k % 2 else -90), zf, 90, y0, y1, 'sandstone', surf='sand', base=y0 - 5)
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.cylinder(cx + sx * 90, cz - 40 + sz * 90, 16, py + 300, py + 440, 'sandstone', n=6, col='prop')
    w.hip(cx, cz - 40, 230, 230, py + 440, 90, 'sandstone', overhang=8, col=None)
    chest(w, cx, py + 300, cz - 40, 'temple top')
    colossus(w, cx + 40, cz - 520)
    # Sandstone houses with roof decks
    for i, (x, z) in enumerate([(cx - 470, cz + 360), (cx + 480, cz + 330), (cx - 480, cz - 360)]):
        house(w, x, z, 360, 360, face_yaw(x, z, cx, cz), 1, gs, name=f'Gerudo house {i}')
    # Colonnades
    for k in range(4):
        z = cz - 250 + k * 90
        w.cylinder(cx + 330, z, 22, py - 10, py + 260, 'sandstone', n=6, col='prop')
        w.box(cx + 330, py + 260, z, 60, 24, 60, 'sandstone', col='none')
    for k in range(4):
        w.cylinder(cx - 240 + k * 130, cz + 470, 22, py - 10, py + 170 + 40 * (k % 2), 'sandstone', n=6, col='prop')
    for k, (x, z) in enumerate([(cx + 200, cz + 420), (cx - 380, cz + 40)]):
        tent(w, x, z, k)
    chest(w, cx + 200, H(cx + 200, cz + 420), cz + 420, 'Gerudo tent')
    rng = rng_for('desert rocks')
    for k in range(16):
        x = cx + rng.uniform(-1900, 1900); z = cz + rng.uniform(-1500, 1700)
        if terrain.region(np.array(x), np.array(z), DESERT, 900) < 0.6 or near_any(x, z, 450) or H(x, z) < WATER_Y + 60: continue
        rock(w, x, z, rng.uniform(1.0, 1.7), 3000 + k, 'sandstone', solid=rng.random() < 0.5)

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

def spirit_oasis(w):
    x, z = region(w, 'Spirit Oasis')
    r = OASIS[2]
    rng = rng_for('oasis')
    for k in range(10):
        a = k * 2 * math.pi / 10 + 0.3
        rr = r + 150 + 80 * (k % 2)
        palm(w, x + rr * math.cos(a), z + rr * math.sin(a), 1.0 + 0.2 * (k % 3), 1300 + k)
    # a pavilion on the east rim
    pxp, pzp = x + r + 360, z - 60
    py = H(pxp, pzp)
    w.cylinder(pxp, pzp, 170, py - 30, py + 14, 'sandstone', n=8, surf='sand')
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(pxp + 140 * math.cos(a), pzp + 140 * math.sin(a), 14, py + 14, py + 190, 'sandstone', n=6, col='prop')
    w.cone(pxp, pzp, 200, py + 190, py + 300, 'roof_red', n=8)
    chest(w, pxp, py + 14, pzp, 'oasis pavilion')
    # the Gerudo camp: two tents and a well
    for k, (tx, tz) in enumerate([(x - 120, z + 480), (x + 160, z + 520)]):
        tent(w, tx, tz, k + 2)
    chest(w, x + 160, H(x + 160, z + 520), z + 520, 'Gerudo camp')
    # a jetty into the pond
    a = (x + r + 40, z + 60); b = (x + r - 160, z + 60)
    beam(w, (a[0], H(*a) + 4, a[1]), (b[0], POOLS[2][4] + 30, b[1]), 90, 18, 'planks', surf='wood')

# ---- The volcano: Goron Forge, Kakariko, Ikana ------------------------------------------------------------------------------------
def goron_forge(w):
    cx, cz = region(w, 'Goron Forge')
    py = H(cx, cz)
    fs = HouseStyle(wall='brick_dark', base='lava_rock', trim='iron', roof='brick_dark', roof_kind='flat', floor='cobble', tint=(1.0, 0.9, 0.84), window='window_lit')
    # the fort: a red-stone ring wall with two gates (the trail's, south west, and the crater's, north), a wall walk with ramps
    gates = [(math.atan2(-3400 - cz, 3800 - cx), 220), (-math.pi / 2, 200)]
    ring_wall(w, cx, cz, 600, 46, py - 40, py + 210, 'lava_rock', gaps=gates, n=16, climb=True)
    for side in (0, 1):
        a = math.pi * (0.1 + 0.9 * side)
        ax, az = cx + 540 * math.cos(a), cz + 540 * math.sin(a); bx, bz = cx + 540 * math.cos(a + 0.65), cz + 540 * math.sin(a + 0.65)
        w.ramp(ax, az, bx, bz, 80, py, py + 210, 'brick_dark', base=py - 10)
        w.box(bx, py + 190, bz, 120, 20, 120, 'brick_dark', surf='stone')
        chest(w, bx, py + 210, bz, 'forge wall walk')
    # the forge hall: two floors, flat roof; the furnace chimney behind it
    house(w, cx + 150, cz - 120, 440, 380, face_yaw(cx + 150, cz - 120, cx, cz + 400), 2, fs, name='Forge hall', door_width=130)
    hx, hz = cx + 150, cz - 400
    w.cylinder(hx, hz, 70, py - 20, py + 560, 'brick_dark', n=8, surf='stone')
    w.far = True
    w.cylinder(hx, hz, 50, py + 560, py + 600, 'lava_rock', n=8, col='none')
    w.far = False
    # the lookout tower on the fort's rim, over the trail
    spiral_tower(w, cx - 330, cz + 260, 280, 540, yaw=face_yaw(cx - 330, cz + 260, cx, cz), wall='brick_dark', floor='cobble', roof='roof_red', name='Forge lookout', cap='hip')
    # the forge yard: anvils, a quench trough, lava channels (drawn glowing rock) round an obsidian dais with the Goron's chest
    for k, (dx, dz) in enumerate([(-120, 60), (-30, 120), (60, 40)]):
        w.box(cx + dx, py, cz + dz, 50, 30, 24, 'iron', k * 0.4, col='prop')
    w.box(cx - 220, py, cz + 120, 120, 36, 50, 'planks', col='prop', surf='wood')
    w.cylinder(cx - 220, cz + 120, 50, py + 36, py + 40, 'water', n=8, col='none')
    rng = rng_for('lava')
    for k in range(5):
        a = k * 1.2 + rng.uniform(0, .4); r = rng.uniform(300, 480)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        w.cylinder(x, z, rng.uniform(60, 110), H(x, z) - 30, H(x, z) + 3, 'lava_rock', n=9, col='none')
    w.cylinder(cx - 60, cz - 40, 110, py - 40, py + 48, 'brick_dark', n=8, surf='stone')
    chest(w, cx - 60, py + 48, cz - 40, 'forge dais')
    # The crater behind the fort: basalt pillars on the rim, glowing pools, the summit chest on a spur
    mx, mz, peak, rad, crater = MOUNTAINS[0]
    for k in range(10):
        a = k * 2 * math.pi / 10 + 0.2
        r = crater * rad + 120
        x, z = mx + r * math.cos(a), mz + r * math.sin(a)
        if math.hypot(x - cx, z - cz) < 800: continue
        rock(w, x, z, rng.uniform(0.9, 1.4), 300 + k, 'lava_rock', solid=False)
    floor = H(mx, mz)
    for k in range(5):
        a = k * 1.3 + rng.uniform(0, .5); r = rng.uniform(150, 320)
        x, z = mx + r * math.cos(a), mz + r * math.sin(a)
        w.cylinder(x, z, rng.uniform(80, 130), H(x, z) - 30, H(x, z) + 4, 'lava_rock', n=9, col='none')
    w.cylinder(mx, mz, 120, floor - 40, floor + 50, 'brick_dark', n=8, surf='stone')
    chest(w, mx, floor + 50, mz, 'crater dais')
    # the trail from Kakariko: stone steps where the mountain is steep (a built ramp road), and a shrine at its bend
    tx, tz = 3963, -3262        # the dais sits beside the trail's bend, outside the fort's wall
    ty = H(tx + 160, tz + 60)
    w.cylinder(tx + 160, tz + 60, 90, ty - 30, ty + 12, 'brick_dark', n=8, surf='stone')
    for k in range(4):
        a = k * math.pi / 2 + 0.4
        w.cylinder(tx + 160 + 70 * math.cos(a), tz + 60 + 70 * math.sin(a), 11, ty + 12, ty + 150, 'brick_dark', n=6, col='prop')
    w.hip(tx + 160, tz + 60, 170, 170, ty + 150, 60, 'roof_red', overhang=10, col=None)
    chest(w, tx + 160, ty + 12, tz + 60, 'trail shrine')
    rng = rng_for('mountain pines')
    for k, (x, z) in enumerate(poisson(rng, (2200, -6800), (6800, -2400), 380, 300)):
        h = H(x, z)
        if h < 250 or h > 1100 or terrain.slope_up(np.array(x), np.array(z)) < 0.7 or near_any(x, z, 450): continue
        if terrain.road_dist(np.array(x), np.array(z)) < 200: continue
        pine(w, x, z, rng.uniform(0.9, 1.3), 4000 + k)

def kakariko(w):
    cx, cz = region(w, 'Kakariko Windmill')
    ks = lambda roof, tint=(1, 1, 1), kind='gable': HouseStyle(wall='plaster', base='town_stone', trim='timber', roof=roof, roof_kind=kind, floor='planks', tint=tint)
    houses = [(-420, -300, 2, 'roof_red', (1.0, 0.95, 0.86)), (400, -340, 1, 'thatch', (0.96, 0.92, 0.84)), (760, -40, 2, 'thatch', (1, 1, 1)),
              (-440, 320, 1, 'roof_red', (0.95, 0.9, 0.82)), (20, -600, 1, 'thatch', (1.0, 0.97, 0.9)), (-160, 640, 2, 'roof_red', (1.0, 0.94, 0.9))]
    windmill(w, cx + 720, cz + 560)       # first: the houses and the lookout keep clear of it
    for i, (dx, dz, st, roof, tint) in enumerate(houses):
        x, z = cx + dx, cz + dz
        house(w, x, z, 360 if st > 1 else 300, 360 if st > 1 else 300, face_yaw(x, z, cx, cz), st, ks(roof, tint), name=f'Kakariko house {i}', roof_rise=150)
    # The well in the square
    y = H(cx, cz)
    ring_wall(w, cx, cz, 70, 20, y - 10, y + 50, 'town_stone', n=10)
    for sx in (-1, 1):
        w.box(cx + sx * 64, y, cz, 10, 150, 10, 'timber', col='none')
    w.gable(cx, cz, 160, 80, y + 150, 46, 'thatch', math.pi / 2, overhang=8, col=None)
    # the lookout tower (wooden), the windmill on its rise, the cuccos' pen
    spiral_tower(w, cx + 300, cz + 380, 280, 500, yaw=face_yaw(cx + 300, cz + 380, cx, cz), wall='timber', floor='planks', roof='thatch', name='Kakariko lookout', cap='hip')
    fence(w, [(cx - 700, cz - 100), (cx - 700, cz + 220), (cx - 420, cz + 220)], 60, 'timber')
    for k in range(4):
        w.cylinder(cx - 640 + (k % 2) * 90, cz - 40 + (k // 2) * 100, 14, H(cx - 640, cz), H(cx - 640, cz) + 24, 'cloth', n=6, col='none')
    for k, (x, z) in enumerate([(cx - 760, cz - 600), (cx + 820, cz - 520), (cx - 820, cz + 560)]):
        oak(w, x, z, 1.1, 40 + k)
    chest(w, cx - 180, y, cz + 120, 'village square')
    ikana_graveyard(w)

def windmill(w, x, z):
    y = H(x, z)
    w.far = True
    house(w, x, z, 380, 380, face_yaw(x, z, x - 600, z - 400), 2, HouseStyle(wall='plaster', base='town_stone', trim='timber', roof='thatch', roof_kind='hip',
          floor='planks', tint=(1, 0.97, 0.9)), name='Windmill', roof_rise=300)
    # sails: four big lattice blades on the west face (drawn)
    hub = (x - 190, y + 2 * STOREY + 150, z)
    for k in range(4):
        a = k * math.pi / 2 + 0.3
        ex, ey = math.cos(a) * 460, math.sin(a) * 460
        px, py_ = -math.sin(a) * 42, math.cos(a) * 42
        w.face([(hub[0], hub[1] + py_ * 0.2, hub[2] + px * 0.2), (hub[0], hub[1] + ey + py_, hub[2] + ex + px),
                (hub[0], hub[1] + ey - py_, hub[2] + ex - px), (hub[0], hub[1] - py_ * 0.2, hub[2] - px * 0.2)], 'cloth', double=True)
    w.cylinder(hub[0] + 22, hub[2], 28, hub[1] - 28, hub[1] + 28, 'timber', n=6, col='none')
    w.far = False

def ikana_graveyard(w):
    x, z = region(w, 'Ikana Graveyard')
    rng = rng_for('graves')
    for i in range(5):
        for j in range(3):
            gx, gz = x - 260 + i * 130 + rng.uniform(-15, 15), z + 60 + j * 120
            y = H(gx, gz)
            w.box(gx, y - 10, gz, 50, 76 + rng.uniform(-14, 14), 16, 'moss_stone', rng.uniform(-.15, .15), col='prop', surf='stone')
    # the crypt: a small stone house with a flat roof and a chest inside; the Shade boss haunts the yard
    house(w, x, z - 260, 360, 360, face_yaw(x, z - 260, x, z + 300), 1, HouseStyle(wall='brick_dark', base='brick_dark', trim='moss_stone', roof='brick_dark',
          roof_kind='flat', floor='cobble', climb=True), back_door=False, name='Ikana crypt')
    fence(w, [(x - 380, z - 60), (x - 380, z + 420), (x + 380, z + 420), (x + 380, z - 60)], 70, 'iron')
    for k in range(3):
        oak(w, x - 420 + k * 420, z + 580, 0.9, 70 + k, leaves='leaves_dark')
    # the royal tomb: a stone slab over a chest at the yard's end
    w.box(x + 250, H(x + 250, z + 300), z + 300, 90, 30, 150, 'moss_stone', col='prop', surf='stone')
    chest(w, x - 250, H(x - 250, z + 330), z + 330, 'graveyard corner')

# ---- The west: the Astral Observatory, Romani Ranch, Milk Road, Lake Romani --------------------------------------------------------
def astral_observatory(w):
    x, z = region(w, 'Astral Observatory')
    y = H(x, z)
    spiral_tower(w, x, z, 320, 440, yaw=face_yaw(x, z, x + 1000, z + 600), wall='town_stone', floor='planks', roof='roof_blue', name='Astral Observatory',
                 cap='hip', clock=False)
    # the dome and the telescope over the look-out (drawn: the roof is the hip cap)
    top = y + 12 + 440
    w.far = True
    w.sphere(x, top + 240, z, 150, 110, 150, 'roof_blue', rings=4, segs=10, seed=5)
    w.cylinder(x + 40, z, 16, top + 300, top + 420, 'iron', n=6, col='none', r1=22)
    w.far = False
    # the scarecrow's garden and the stargazer's shed
    house(w, x + 360, z + 200, 280, 260, face_yaw(x + 360, z + 200, x, z), 1, style(roof='roof_green', roof_kind='gable', tint=(0.95, 1, 0.9)), back_door=False,
          name='stargazer shed', roof_rise=120)
    w.cylinder(x - 300, z + 160, 8, H(x - 300, z + 160), H(x - 300, z + 160) + 130, 'timber', n=5, col='none')
    w.box(x - 300, H(x - 300, z + 160) + 100, z + 160, 90, 10, 10, 'timber', col='none')
    w.sphere(x - 300, H(x - 300, z + 160) + 150, z + 160, 22, 26, 22, 'hay', rings=3, segs=6)
    chest(w, x - 260, H(x - 260, z - 220), z - 220, 'observatory garden')
    rng = rng_for('blossom')
    for k in range(9):
        a = k * 0.7 + 0.3; r = rng.uniform(360, 520)
        px, pz = x + r * math.cos(a), z + r * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 160: continue
        blossom_tree(w, px, pz, 1.0 + 0.2 * (k % 3), 1400 + k)

def romani_ranch(w):
    cx, cz = region(w, 'Romani Ranch')
    py = H(cx, cz)
    ring_wall(w, cx, cz, 790, 40, py - 30, py + 120, 'town_stone', gaps=[(math.pi * 0.08, 240), (math.pi * 0.95, 240), (math.pi / 2 + 0.2, 220)], n=18)
    rs = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='roof_red', roof_kind='gable', floor='planks')
    house(w, cx - 300, cz - 160, 480, 400, face_yaw(cx - 300, cz - 160, cx, cz), 2, rs, name='Romani barn', roof_rise=220, door_width=140)
    house(w, cx + 300, cz - 300, 360, 360, face_yaw(cx + 300, cz - 300, cx, cz), 2,
          HouseStyle(wall='plaster', base='town_stone', trim='timber', roof='roof_red', roof_kind='gable', tint=(1, .96, .9)), name='Ranch house', roof_rise=160)
    # the silo: a round tower, climbable, with a platform on top
    sx, sz = cx - 400, cz + 300
    sy = H(sx, sz)
    w.cylinder(sx, sz, 100, sy - 30, sy + 420, 'planks', n=10, surf='wood', climb=True, top_mat='planks')
    w.far = True
    w.cone(sx, sz, 118, sy + 420, sy + 560, 'roof_red', n=10)
    w.far = False
    chest(w, sx + 60, sy + 420, sz, 'silo top')
    # paddock: a fence ring with hay bales and cows (the game's own actors are not here; hay stands in)
    fence(w, [(cx + 280 + 260 * math.cos(a), cz + 240 + 190 * math.sin(a)) for a in np.linspace(0.4, 2 * math.pi + 0.0, 14)], 60, 'timber')
    for k, (x, z) in enumerate([(cx + 80, cz + 420), (cx + 160, cz + 480), (cx - 120, cz + 120)]):
        w.cylinder(x, z, 38, H(x, z) - 5, H(x, z) + 60, 'hay', n=8, col='prop', top_mat='hay')
    chest(w, cx + 300, py, cz + 240, 'paddock')
    # the pond with a little jetty
    x0, x1, z0, z1, wy = POOLS[1]
    beam(w, (x0 - 60, H(x0 - 60, (z0 + z1) / 2) + 4, (z0 + z1) / 2), (x0 + 140, wy + 24, (z0 + z1) / 2), 80, 16, 'planks', surf='wood')
    for k in range(5):
        a = k * 1.25 + 0.2
        oak(w, cx + 900 * math.cos(a), cz + 900 * math.sin(a), 1.0, 1200 + k)
    milk_road(w)
    lake_romani(w)

def milk_road(w):
    x, z = region(w, 'Milk Road')
    a = (x + 400, H(x + 400, z) + 4, z); b = (x - 400, H(x - 400, z) + 4, z)
    p0, p1, y = stone_bridge(w, a, b, width=180, piers=2, wall_mat='moss_stone')
    chest(w, x, y, z - 30, 'Milk Road middle')
    # the ranch gate: two posts and a sign beam, and the milk cart stop (a shed)
    gx, gz = x - 560, z
    for s in (-1, 1):
        w.box(gx, H(gx, gz + s * 120) - 10, gz + s * 120, 26, 220, 26, 'timber', col='prop')
    w.box(gx, H(gx, gz) + 200, gz, 30, 24, 260, 'planks', col='none')
    house(w, x - 700, z - 330, 280, 260, face_yaw(x - 700, z - 330, x, z), 1, style(wall='timber', roof='thatch', roof_kind='gable'), back_door=False,
          name='milk stop', roof_rise=120)

def lake_romani(w):
    """The lake above the ranch: a fishing jetty and a boat on the east shore, rocks."""
    lx, lz, rx, rz = LAKE_ROMANI
    ex, ez = lx + rx + 60, lz + 80
    beam(w, (ex + 60, H(ex + 60, ez) + 4, ez), (ex - 260, WATER_Y + 40, ez), 90, 18, 'planks', surf='wood')
    boat(w, ex - 400, ez + 160, 1.2)
    chest(w, ex + 60, H(ex + 60, ez + 110), ez + 110, 'lake jetty')
    rng = rng_for('lake rocks')
    for k in range(6):
        a = rng.uniform(0, 2 * math.pi)
        x, z = lx + (rx + 260) * math.cos(a), lz + (rz + 260) * math.sin(a)
        if H(x, z) < WATER_Y + 30 or terrain.road_dist(np.array(x), np.array(z)) < 150: continue
        rock(w, x, z, rng.uniform(0.9, 1.4), 820 + k, solid=False)

def boat(w, x, z, yaw, L=240, W=90):
    y = WATER_Y - 8
    f = Frame(x, z, yaw)
    hull = [f.p(0, L / 2), f.p(W / 2, L / 4), f.p(W / 2, -L / 2), f.p(-W / 2, -L / 2), f.p(-W / 2, L / 4)]
    w.prism(hull, y - 20, y + 30, 'planks', col='prop', surf='wood', top_mat='timber')
    px, pz = f.p(0, 0)
    w.cylinder(px, pz, 6, y + 30, y + 240, 'timber', n=5, col='none')
    sx, sz = f.p(0, -L * 0.35)
    w.face([(px, y + 80, pz), (px, y + 230, pz), (sx, y + 90, sz)], 'cloth', double=True)

# ---- The lagoon: Great Bay Stilts, the Fisherman's Hut, Zora Cape, Termina Bridge --------------------------------------------------
DECK_Y = -165

def stilts_deck(w, x, z, sx, sz, yaw=0.0, y=DECK_Y):
    w.box(x, y - 16, z, sx, 16, sz, 'planks', yaw, surf='wood', hookshot=True)
    f0 = Frame(x, z, yaw); a = f0.p(-sx / 2, 0); b = f0.p(sx / 2, 0)
    w.walkways.append((a[0], a[1], b[0], b[1], sz / 2, y, y))
    f = Frame(x, z, yaw)
    for lx in np.linspace(-sx / 2 + 12, sx / 2 - 12, max(2, int(sx / 180) + 1)):
        for lz in np.linspace(-sz / 2 + 12, sz / 2 - 12, max(2, int(sz / 180) + 1)):
            px, pz = f.p(lx, lz)
            w.cylinder(px, pz, 10, H(px, pz) - 10, y - 16, 'timber', n=6, col='none', top=False)

def boardwalk(w, a, b, width=120, y=DECK_Y, y1=None):
    (ax, az), (bx, bz) = a, b
    L = math.hypot(bx - ax, bz - az)
    beam(w, (ax, y, az), (bx, y if y1 is None else y1, bz), width, 16, 'planks', surf='wood')
    for k in range(int(L / 220) + 1):
        u = k / max(1, int(L / 220))
        px, pz = ax + (bx - ax) * u, az + (bz - az) * u
        yy = y + ((y1 if y1 is not None else y) - y) * u
        for side in (-1, 1):
            nx, nz = -(bz - az) / L * side * (width / 2 - 8), (bx - ax) / L * side * (width / 2 - 8)
            w.cylinder(px + nx, pz + nz, 9, H(px + nx, pz + nz) - 10, yy + 50, 'timber', n=6, col='none', top=True)

def great_bay_stilts(w):
    cx, cz = region(w, 'Great Bay Stilts')
    ls = HouseStyle(wall='planks', base='timber', trim='timber', roof='thatch', roof_kind='gable', floor='planks', climb=True, surf='wood')
    decks = [(cx, cz - 400, 420, 380, 0.2), (cx - 520, cz - 120, 400, 360, -0.3), (cx + 460, cz - 60, 400, 360, 0.1), (cx - 200, cz + 380, 460, 400, 0.4),
             (cx + 380, cz + 480, 320, 320, 0.0)]
    for i, (x, z, sx, sz, yaw) in enumerate(decks):
        stilts_deck(w, x, z, sx, sz, yaw)
        if i < 4:
            house(w, x, z, 260, 240, yaw + (math.pi if i % 2 else 0), 1, ls, back_door=True, name=f'stilt hut {i}', floor_y=DECK_Y, roof_rise=120, footing=False)
    chest(w, cx + 380, DECK_Y, cz + 480, 'drying deck')
    # Boardwalks between the decks and up to the north shore landing
    walks = [((cx, cz - 400), (cx - 520, cz - 120)), ((cx, cz - 400), (cx + 460, cz - 60)), ((cx - 520, cz - 120), (cx - 200, cz + 380)),
             ((cx + 460, cz - 60), (cx + 380, cz + 480)), ((cx - 200, cz + 380), (cx + 380, cz + 480))]
    for a, b in walks: boardwalk(w, a, b)
    # the landing: a boardwalk north to the shore, climbing to the beach
    sx_, sz_ = cx, cz - 400 - 190
    shore = (cx, cz - 1180)
    shore_y = H(*shore) + 4
    boardwalk(w, (sx_, sz_), shore, y=DECK_Y, y1=max(DECK_Y, shore_y))
    if shore_y > DECK_Y + 10:
        pass
    # steps out of the water at the ends of the decks
    for x, z in [(cx - 760, cz - 120), (cx + 700, cz - 60), (cx - 200, cz + 620)]:
        w.box(x, WATER_Y - 20, z, 90, 50, 60, 'planks', surf='wood')
    for x, z in [(cx + 100, cz - 260), (cx - 380, cz + 300)]:
        barrel(w, x, z, DECK_Y); crate(w, x + 50, z - 30, DECK_Y, 36)
    # Fishing boats moored by the decks, nets
    for x, z, yaw in [(cx - 300, cz - 520, 0.4), (cx + 720, cz + 300, -0.6)]:
        boat(w, x, z, yaw)
    fishermans_hut(w)

def fishermans_hut(w):
    x, z = region(w, "Fisherman's Hut")
    ls = HouseStyle(wall='planks', base='town_stone', trim='timber', roof='thatch', roof_kind='gable', floor='planks', climb=True, surf='wood')
    house(w, x, z, 300, 280, face_yaw(x, z, POI_AT['Great Bay Stilts'][0], POI_AT['Great Bay Stilts'][1]), 1, ls, back_door=False, name="Fisherman's Hut", roof_rise=130)
    y = H(x, z)
    # a pier west towards the stilts, a boat and a lookout post
    ay = H(x - 150, z + 40) + 4
    beam(w, (x - 150, ay, z + 40), (x - 460, WATER_Y + 40, z + 40), 90, 18, 'planks', surf='wood')
    boat(w, x - 560, z + 160, 1.5)
    w.cylinder(x + 180, z - 120, 9, y, y + 160, 'timber', n=5, col='none')
    chest(w, x - 400, ay + (WATER_Y + 40 - ay) * (400 - 150) / (460 - 150), z + 40, 'pier end')
    for k in range(4):
        a = k * 1.6 + 0.4
        rock(w, x + 340 * math.cos(a), z + 300 * math.sin(a), 0.8 + 0.2 * k, 900 + k, solid=True)
    palm(w, x + 150, z + 140, 1.0, 50); palm(w, x - 120, z - 160, 1.2, 51)

def zora_cape(w):
    cx, cz = region(w, 'Zora Cape')
    os_ = HouseStyle(wall='planks', base='town_stone', trim='timber', roof='thatch', roof_kind='gable', floor='planks', tint=(1, .95, .88), surf='wood')
    for i, (dx, dz) in enumerate([(-200, -220), (-260, 180), (200, 400)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 320, 300, face_yaw(x, z, cx + 500, cz), 1, os_, name=f'Zora Cape house {i}', roof_rise=140)
    # Piers out over the lagoon (east), a boat shed on the longest
    for k, (z, L) in enumerate([(cz - 200, 560), (cz + 80, 760)]):
        x0 = cx + 300
        ay = H(x0 - 60, z) + 4
        beam(w, (x0 - 60, ay, z), (x0 + L, DECK_Y + 30, z), 120, 18, 'planks', surf='wood')
        for xx in np.linspace(x0, x0 + L, int(L / 200) + 1):
            for side in (-1, 1):
                w.cylinder(xx, z + side * 50, 9, H(xx, z) - 10, DECK_Y + 70, 'timber', n=6, col='none')
        boat(w, x0 + L * 0.6, z + 150, 1.57)
    shed_x, shed_z = cx + 300 + 620, cz + 80
    shed_y = ay + (DECK_Y + 30 - ay) * (620 + 60) / (760 + 60)       # the long pier's deck at the shed
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.box(shed_x + sx * 100, shed_y, shed_z + sz * 56, 12, 170, 12, 'timber', col='none')
    w.gable(shed_x, shed_z, 220, 130, shed_y + 170, 90, 'thatch', math.pi / 2, overhang=24, col=None)
    chest(w, shed_x, shed_y, shed_z, 'boat shed')
    for k in range(4):
        barrel(w, cx + 40 + k * 46, cz - 420, H(cx + 40 + k * 46, cz - 420))
    # The lighthouse on the point (south west)
    lx, lz = cx - 240, cz + 540
    spiral_tower(w, lx, lz, 300, 700, yaw=face_yaw(lx, lz, cx, cz), wall='plaster', floor='planks', roof='roof_red', name='Zora Cape lighthouse', cap='hip')
    w.far = True
    w.box(lx, H(lx, lz) + 12 + 700 + 170 + 50, lz, 60, 60, 60, 'window_lit', col='none')
    w.far = False
    rng = rng_for('cape rocks')
    for k in range(8):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(600, 800)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if H(x, z) < WATER_Y - 40: continue
        if x > cx + 150 and cz - 420 < z < cz + 320: continue        # not across the piers
        rock(w, x, z, rng.uniform(1.0, 1.6), 1000 + k, solid=k % 2 == 0)

def termina_bridge(w):
    x, z = region(w, 'Termina Bridge')
    a = (x - 520, H(x - 520, z) + 4, z); b = (x + 520, H(x + 520, z) + 4, z)
    p0, p1, y = stone_bridge(w, a, b, width=220, piers=3, towers=True, wall_mat='moss_stone')
    chest(w, x, y, z + 40, 'Termina Bridge middle')
    chest(w, p0[0], y - 40 + 300, z - 170, 'bridge tower top')

# ---- The south east: Deku Tree Village, the Lost Woods ----------------------------------------------------------------------------
def giant_tree(w, x, z, r, height, seed, platform=None, hut=False, far=True):
    """A great forest tree: a climbable trunk (scene collision), roots, a high canopy; optionally a ring deck round it with a hut."""
    y = H(x, z) - 20
    old = w.group; w.group = 'Foliage'; w.far = far
    w.cylinder(x, z, r * 1.5, y - 30, y + 60, 'bark', n=9, surf='wood', r1=r, climb=True)
    w.cylinder(x, z, r, y + 60, y + height, 'bark', n=9, surf='wood', r1=r * 0.75, climb=True)
    rng = np.random.default_rng(seed)
    for k in range(5):
        a = k * 2 * math.pi / 5 + rng.uniform(-.3, .3)
        bx, bz = x + math.cos(a) * r * 2.4, z + math.sin(a) * r * 2.4
        by = y + height * rng.uniform(0.75, 0.95)
        w.solid([(x, y + height * 0.65, z), (x + math.cos(a + 1.6) * 24, y + height * 0.7, z + math.sin(a + 1.6) * 24), (x, y + height * 0.75, z), (bx, by, bz)],
                [(0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)], 'bark', col='none')
        w.sphere(bx, by + 50, bz, r * 2.9, r * 1.5, r * 2.9, 'leaves_dark', rings=4, segs=8, seed=seed + k, jitter=0.15, squash_bottom=0.5)
    w.sphere(x, y + height + 60, z, r * 4.0, r * 2.0, r * 4.0, 'leaves', rings=5, segs=10, seed=seed + 9, jitter=0.12, squash_bottom=0.5)
    # a low skirt so the branch crowns and the top crown overlap into one closed canopy (no sky between them)
    w.sphere(x, y + height * 0.82, z, r * 3.6, r * 1.3, r * 3.6, 'leaves_dark', rings=4, segs=10, seed=seed + 10, jitter=0.1, squash_bottom=0.5)
    w.group = old; w.far = False
    if platform:
        py = y + platform
        R = r * 2.6
        for half in (0, 1):
            angs = np.linspace(half * math.pi, (half + 1) * math.pi, 7)
            outer = [(x + R * math.cos(a), z + R * math.sin(a)) for a in angs]
            inner = [(x + (r * 0.85) * math.cos(a), z + (r * 0.85) * math.sin(a)) for a in angs[::-1]]
            w.prism(outer + inner, py - 18, py, 'planks', surf='wood', hookshot=True)
        for a in np.linspace(0, 2 * math.pi, 9)[:-1]:
            px, pz = x + (R - 14) * math.cos(a), z + (R - 14) * math.sin(a)
            w.cylinder(px, pz, 6, py, py + 60, 'timber', n=5, col='none')
        chest(w, x + (R - 60), py, z, 'tree deck')
        if hut:
            hx, hz = x + (r + 120) * math.cos(2.3), z + (r + 120) * math.sin(2.3)
            w.prism(rect(hx, hz, 180, 180, 2.3), py, py + 150, 'planks', col='none')
            w.hip(hx, hz, 180, 180, py + 150, 90, 'thatch', 2.3, overhang=20, col=None)
        # a log ramp from the ground up to the deck, for the bots (players can just climb the bark)
        a = rng.uniform(0, 2 * math.pi)
        run = platform / 0.5
        sx, sz = x + (R + run) * math.cos(a), z + (R + run) * math.sin(a)
        ex, ez = x + (R - 10) * math.cos(a), z + (R - 10) * math.sin(a)
        beam(w, (sx, H(sx, sz) - 5, sz), (ex, py, ez), 90, 24, 'planks', surf='wood')

def stump_house(w, x, z, r, seed, door_angle):
    y = H(x, z)
    w.cylinder(x, z, r + 24, y - 40, y + 12, 'bark', n=10, surf='wood', top_mat='planks')
    ring_wall(w, x, z, r, 30, y + 12, y + 200, 'bark', gaps=[(door_angle, 110)], n=12, climb=True)
    w.cone(x, z, r + 50, y + 200, y + 330, 'thatch', n=10)
    w.buildings.append((x, z, r * 0.7, r * 0.7, y + 12, 0.0))
    w.interior.append((x - r, x + r, z - r, z + r, y, y + 200))
    chest(w, x - math.cos(door_angle) * r * 0.5, y + 12, z - math.sin(door_angle) * r * 0.5, 'stump house')
    bed(w, Frame(x, z, door_angle + math.pi / 2), r * 0.45, 0, y + 12)

def deku_tree_village(w):
    cx, cz = region(w, 'Deku Tree Village')
    deku_tree(w, cx, cz)
    trees = [(cx - 560, cz - 420, 100, 760, 240, True), (cx + 520, cz - 380, 95, 700, 220, False), (cx + 560, cz + 360, 105, 800, 260, True),
             (cx - 520, cz + 440, 90, 720, 0, False)]
    for k, (x, z, r, hgt, plat, hut) in enumerate(trees):
        giant_tree(w, x, z, r, hgt, 500 + k, platform=plat or None, hut=hut)
    for k, (x, z) in enumerate([(cx - 150, cz + 560), (cx + 160, cz - 600), (cx - 620, cz)]):
        stump_house(w, x, z, 150, 600 + k, math.atan2(cz - z, cx - x) + 0.5)
    # a rope bridge between the two decks with huts (a plank beam high up)
    (ax, az, ar, ah, ap, _), (bx, bz, br, bh, bp, _) = trees[0], trees[2]
    ya = H(ax, az) - 20 + ap; yb = H(bx, bz) - 20 + bp
    beam(w, (ax + ar * 2.5, ya, az), (bx - br * 2.5, yb, bz), 100, 14, 'planks', surf='wood')
    chest(w, (ax + bx) / 2, (ya + yb) / 2, (az + bz) / 2, 'rope bridge')
    # posts under the span every ~200 units so it never hangs in the air
    sx, sz, ex, ez = ax + ar * 2.5, az, bx - br * 2.5, bz
    nposts = int(math.hypot(ex - sx, ez - sz) // 200)
    for k in range(1, nposts + 1):
        t = k / (nposts + 1); px, pz = sx + (ex - sx) * t, sz + (ez - sz) * t; py = ya + (yb - ya) * t - 14
        w.cylinder(px, pz, 14, H(px, pz) - 30, py + 2, 'timber', n=6)

def deku_tree(w, x, z):
    y = H(x, z)
    r = 260
    a = math.atan2(POI_AT['Lost Woods'][1] - z, POI_AT['Lost Woods'][0] - x)
    w.cylinder(x, z, r + 100, y - 40, y + 12, 'bark', n=12, surf='wood', top_mat='planks')
    ring_wall(w, x, z, r, 70, y + 12, y + 700, 'bark', gaps=[(a, 160)], n=14, climb=True)
    w.buildings.append((x, z, r * 0.7, r * 0.7, y + 12, 0.0))
    w.interior.append((x - r, x + r, z - r, z + r, y, y + 700))
    # inside: a ramp up the back wall to a ledge, and a hole in the bark to look out from
    ba = a + math.pi
    p0 = (x + math.cos(ba + 1.0) * (r - 90), z + math.sin(ba + 1.0) * (r - 90))
    p1 = (x + math.cos(ba - 0.4) * (r - 90), z + math.sin(ba - 0.4) * (r - 90))
    L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    w.ramp(p0[0], p0[1], p1[0], p1[1], 90, y + 12, y + 12 + L * 0.5, 'planks', surf='wood', base=y + 8)
    lx, lz = x + math.cos(ba - 0.85) * (r - 100), z + math.sin(ba - 0.85) * (r - 100)
    w.cylinder(lx, lz, 100, y + 12 + L * 0.5 - 18, y + 12 + L * 0.5, 'planks', n=8, surf='wood')
    chest(w, lx, y + 12 + L * 0.5, lz, 'Deku Tree ledge')
    chest(w, x, y + 12, z, 'Deku Tree hollow')
    # the face: eyes and the moustache of bark (drawn), the crown and great roots
    fx, fz = x + math.cos(a) * (r + 36), z + math.sin(a) * (r + 36)
    for s in (-1, 1):
        ex, ez = fx + math.cos(a + math.pi / 2) * s * 90, fz + math.sin(a + math.pi / 2) * s * 90
        w.box(ex, y + 330, ez, 44, 30, 30, 'iron', a, col='none')
    w.far = True
    old = w.group; w.group = 'Foliage'
    w.sphere(x, y + 980, z, 820, 340, 820, 'leaves', rings=5, segs=12, seed=77, jitter=0.12, squash_bottom=0.5)
    for k in range(6):
        aa = a + 0.5 + k * 1.0
        rx, rz = x + math.cos(aa) * (r + 280), z + math.sin(aa) * (r + 280)
        w.solid([(x + math.cos(aa) * r, y + 160, z + math.sin(aa) * r), (x + math.cos(aa + .25) * r, y - 20, z + math.sin(aa + .25) * r),
                 (x + math.cos(aa - .25) * r, y - 20, z + math.sin(aa - .25) * r), (rx, H(rx, rz) - 20, rz)], [(0, 1, 3), (0, 3, 2), (0, 2, 1), (1, 2, 3)], 'bark', col='none')
    w.group = old; w.far = False

def lost_woods(w):
    cx, cz = region(w, 'Lost Woods')
    rng = rng_for('lost woods')
    # a thick ring of big dark oaks with winding ways through, a hollow log with a chest, a stump with the Kokiri's chest
    for k in range(26):
        a = k * 2 * math.pi / 26 + rng.uniform(-.1, .1); r = 330 + 160 * (k % 3) + rng.uniform(-40, 40)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if terrain.road_dist(np.array(x), np.array(z)) < 150: continue
        oak(w, x, z, rng.uniform(1.2, 1.6), 5000 + k, leaves='leaves_dark')
    y = H(cx, cz)
    log = ((cx - 90, y + 40, cz + 30), (cx + 120, y + 40, cz - 60))
    w.cylinder(cx + 15, cz - 15, 44, y - 10, y + 70, 'bark', n=6, col='prop')
    chest(w, cx + 200, H(cx + 200, cz - 110), cz - 110, 'hollow log')
    sx, sz = cx - 260, cz + 180
    w.cylinder(sx, sz, 60, H(sx, sz) - 20, H(sx, sz) + 40, 'bark', n=9, surf='wood', top_mat='planks')
    chest(w, sx, H(sx, sz) + 40, sz, 'Kokiri stump')
    for k in range(6):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(120, 300)
        bush(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(0.8, 1.2), 5100 + k)
    forest_fill(w)

def forest_fill(w):
    """Big shady trees through the painted woods (the south east), thick enough to hide in, thin enough to run through."""
    rng = rng_for('forest')
    pts = poisson(rng, (1800, 600), (7000, 6200), 300, 900)
    n = 0
    for x, z in pts:
        if terrain.region(np.array(x), np.array(z), FOREST, 700) < 0.6: continue
        if H(x, z) < WATER_Y + 40 or terrain.slope_up(np.array(x), np.array(z)) < 0.72: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 170: continue
        if near_any(x, z, 380): continue
        s = rng.uniform(0.9, 1.4)
        if rng.random() < 0.2: pine(w, x, z, s, 900 + n)
        else: oak(w, x, z, s, 900 + n, leaves='leaves_dark' if rng.random() < 0.5 else 'leaves')
        n += 1

# ---- Moon Crater and the meadow ----------------------------------------------------------------------------------------------------
def moon_crater(w):
    x, z = region(w, 'Moon Crater')
    y = H(x, z)
    rng = rng_for('crater')
    # the rim: a ring of scorched rocks, broken on the road's side; the floor is cracked stone round the fallen moon
    for k in range(16):
        a = k * 2 * math.pi / 16
        if abs(((a - 0.3 + math.pi) % (2 * math.pi)) - math.pi) < 0.35: continue
        r = 300 + rng.uniform(-30, 30)
        rock(w, x + r * math.cos(a), z + r * math.sin(a), rng.uniform(1.0, 1.5), 6000 + k, 'brick_dark', solid=k % 3 == 0)
    w.cylinder(x, z, 250, y - 30, y + 6, 'brick_dark', n=16, surf='stone')
    # the moon: a cracked stone face half sunk in the crater, its brow a platform you climb to by the crack
    w.sphere(x, y + 40, z, 150, 170, 150, 'moss_stone', rings=5, segs=10, seed=3, jitter=0.1)
    w.cylinder(x, z, 120, y + 6, y + 150, 'moss_stone', n=10, surf='stone', climb=True, r1=90)
    w.cylinder(x, z, 90, y + 150, y + 160, 'moss_stone', n=10, surf='stone')
    for s in (-1, 1):
        w.box(x + s * 40, y + 100, z - 95, 24, 16, 10, 'iron', col='none')     # the moon's eyes, glaring north
    chest(w, x, y + 160, z, 'moon brow'); chest(w, x + 180, y + 6, z + 60, 'crater floor'); chest(w, x - 170, y + 6, z - 80, 'crater floor')
    for k in range(4):
        a = k * 1.5 + 0.7
        w.cylinder(x + 200 * math.cos(a), z + 200 * math.sin(a), rng.uniform(30, 50), y - 20, y + 4, 'lava_rock', n=8, col='none')

def field(w):
    """The meadow between everything: lone oaks, bushes and rocks, the old gate, fences, the lagoon's beach rocks."""
    rng = rng_for('field oaks')
    pts = poisson(rng, (-HALF_X + 600, -HALF_Z + 600), (HALF_X - 600, HALF_Z - 600), 650, 2600)
    n = 0
    for x, z in pts:
        h = H(x, z)
        if h < WATER_Y + 60 or h > 560: continue
        if terrain.region(np.array(x), np.array(z), FOREST, 700) > 0.3: continue
        if terrain.region(np.array(x), np.array(z), DESERT, 900) > 0.35: continue
        if terrain.slope_up(np.array(x), np.array(z)) < 0.82: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 220: continue
        if near_any(x, z, 500): continue
        if terrain.coast_distance(np.array(x), np.array(z)) > -450: continue
        if rng.random() < 0.55:
            oak(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
        elif rng.random() < 0.5:
            bush(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
            bush(w, x + 70, z + 30, rng.uniform(0.7, 1.0), 2100 + n)
        else:
            s = rng.uniform(1.0, 1.6)
            rock(w, x, z, s, 2000 + n, solid=s > 1.35)
        n += 1
    # The old gate on the meadow north of the temple (ref: TP's ruined arch by the fences): two broken towers and a broken span
    gx, gz = 600, -1500
    yaw = 0.5
    f = Frame(gx, gz, yaw)
    for side in (-1, 1):
        px, pz = f.p(side * 170, 0)
        w.box(px, H(px, pz) - 30, pz, 110, 440 if side < 0 else 300, 120, 'moss_stone', yaw, surf='stone', climb=(1,))
    a = f.p(-120, 0); b = f.p(40, 0)
    beam(w, (a[0], H(gx, gz) + 410, a[1]), (b[0], H(gx, gz) + 360, b[1]), 100, 60, 'moss_stone', block=True)   # a broken span, not a walkway
    _lx, _lz = f.p(-170, 0); chest(w, _lx, H(_lx, _lz) + 410, _lz, 'old gate top')
    w.markers.append({'name': 'Old Gate', 'kind': 'landmark', 'x': gx, 'z': gz})
    fence(w, [f.p(240, 50), f.p(900, 200)], 60)
    fence(w, [f.p(-240, 50), f.p(-800, 240)], 60)
    # a shepherd's shelter and a hay cart in the west meadow, a lone watchtower by the lagoon's north shore
    sx, sz = -1300, -300
    tent(w, sx, sz, 5)
    chest(w, sx, H(sx, sz), sz, 'shepherd tent')
    tx, tz = 400, 2650
    spiral_tower(w, tx, tz, 240, 360, yaw=face_yaw(tx, tz, 0, 0), wall='timber', floor='planks', roof='thatch', name='shore watchtower', cap='hip')
    rng = rng_for('beach rocks')
    for k in range(14):
        a = rng.uniform(0, 2 * math.pi)
        lx, lz, rx, rz = LAGOON
        x, z = lx + (rx + 120) * math.cos(a), lz + (rz + 120) * math.sin(a)
        if H(x, z) < WATER_Y - 40 or H(x, z) > 40 or near_any(x, z, 300): continue
        rock(w, x, z, rng.uniform(0.7, 1.2), 7000 + k, solid=False)

def poisson(rng, lo, hi, r, n_try):
    pts = []
    for _ in range(n_try * 4):
        if len(pts) >= n_try: break
        x = rng.uniform(lo[0], hi[0]); z = rng.uniform(lo[1], hi[1])
        if all((x - px) ** 2 + (z - pz) ** 2 > r * r for px, pz in pts): pts.append((x, z))
    return pts

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
    ('Hyrule Castle', hyrule_castle), ('Castle Falls', castle_falls), ('Clock Town', clock_town), ('Temple of Time', temple_of_time),
    ('Snowhead Lodge', snowhead), ('Fairy Fountain', fairy_fountain), ('Spirit Colossus', spirit_colossus), ('Spirit Oasis', spirit_oasis),
    ('Goron Forge', goron_forge), ('Kakariko Windmill', kakariko), ('Astral Observatory', astral_observatory), ('Romani Ranch', romani_ranch),
    ('Great Bay Stilts', great_bay_stilts), ('Zora Cape', zora_cape), ('Termina Bridge', termina_bridge), ('Deku Tree Village', deku_tree_village),
    ('Lost Woods', lost_woods), ('Moon Crater', moon_crater), ('Hyrule Field', field),
]

def build_all(w, only=None):
    _WORLD[0] = w
    for name, fn in BUILDERS:
        if isinstance(only, str): only = only.split(',')
        if only and 'all' not in only and name not in only: continue
        fn(w)
