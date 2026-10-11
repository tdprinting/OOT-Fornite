"""Every place on Skyloft Isles, built from the kit (kit.py, sized from adult Link). Positions come from layout.py. Nothing on this map is
scattered at random when a match starts: every building, bridge, tree and rock is laid here, once, and baked into the map.

Layout inspired by Skyloft (Skyward Sword); every shape here is the project's own low-poly OoT-style geometry."""
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
            segs = max(1, int(L / 120))      # short pieces: a blocker is a bounding box, and one long diagonal box would close the deck
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

# ---- the Knight Academy lobe --------------------------------------------------------------------------------------------------------
def knight_academy(w):
    cx, cz = region(w, 'Knight Academy')
    acad = HouseStyle(wall='whitewash', base='town_stone', trim='timber', roof='roof_red', roof_kind='hip', floor='planks', window='window_lit')
    # the main hall: two storeys, the headmaster's tower at its east end, dormitory wings north and south round the courtyard
    house(w, cx, cz - 60, 620, 440, face_yaw(cx, cz - 60, cx, cz + 1200), 2, acad, name='Knight Academy hall', roof_rise=200, door_width=140, stair_landing=True)
    spiral_tower(w, cx + 470, cz - 160, 300, 620, yaw=face_yaw(cx + 470, cz - 160, cx, cz + 400), wall='whitewash', floor='planks', roof='roof_red',
                 name='Headmaster tower', cap='spire')
    for i, (dx, dz) in enumerate([(-560, 380), (520, 420)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 420, 340, face_yaw(x, z, cx, cz + 400), 2, acad, name=f'Academy dorm {i}', roof_rise=150)
    # the courtyard: paving, a well, benches, lamps, the knights' banner poles
    y = H(cx, cz + 420)
    ring_wall(w, cx, cz + 420, 70, 20, y - 10, y + 50, 'town_stone', n=10)
    for a in np.radians([30, 150, 270]):
        lamp(w, cx + 300 * math.cos(a), cz + 420 + 220 * math.sin(a))
        bench(w, cx + 230 * math.cos(a + 0.6), cz + 420 + 170 * math.sin(a + 0.6), a + 0.6 + math.pi / 2)
    for sx in (-1, 1):
        w.box(cx + sx * 120, y, cz + 760, 12, 300, 12, 'timber', col='none')
        w.decal(cx + sx * 120 + 2, y + 160, cz + 760, 60, 120, 1, 0, 'banner_blue', off=8)
    chest(w, cx + 180, y, cz + 520, 'academy courtyard')
    # the pond with a jetty, and the cliff-edge fence
    x0, x1, z0, z1, wy = POOLS[1]
    beam(w, (x1 + 60, H(x1 + 60, (z0 + z1) / 2) + 4, (z0 + z1) / 2), (x1 - 160, wy + 24, (z0 + z1) / 2), 80, 16, 'planks', surf='wood')
    chest(w, (x0 + x1) / 2, H((x0 + x1) / 2, z0 - 90), z0 - 90, 'academy pond')
    fence(w, [(cx - 900, cz + 700), (cx - 300, cz + 880), (cx + 400, cz + 880), (cx + 900, cz + 700)], 60)
    signpost(w, cx + 60, cz + 900, 0.2)
    rng = rng_for('academy oaks')
    for k in range(11):
        a = k * 0.57 + 0.4; r = 860 + 80 * (k % 2)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if H(x, z) < ACADEMY - 60 or terrain.road_dist(np.array(x), np.array(z)) < 180 or near_any(x, z, 60): continue
        oak(w, x, z, rng.uniform(0.9, 1.2), 100 + k)
    sparring_hall(w)
    academy_bridge(w)

def sparring_hall(w):
    cx, cz = region(w, 'Sparring Hall')
    hs = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='roof_red', roof_kind='gable', floor='planks', tint=(1.0, 0.96, 0.9))
    house(w, cx - 120, cz - 200, 520, 400, face_yaw(cx - 120, cz - 200, cx, cz + 400), 2, hs, name='Sparring Hall', roof_rise=230, door_width=140)
    # the training yard: a fence ring, straw dummies, a log balance course and the quartermaster's shed
    yx, yz = cx - 420, cz + 320
    y = H(yx, yz)
    fence(w, [(yx + 300 * math.cos(a), yz + 240 * math.sin(a)) for a in np.linspace(0.6, 2 * math.pi + 0.2, 12)], 60)
    for k in range(4):
        px, pz = yx - 140 + k * 90, yz - 100 + (k % 2) * 60
        w.cylinder(px, pz, 8, H(px, pz), H(px, pz) + 110, 'timber', n=5, col='none')
        w.sphere(px, H(px, pz) + 140, pz, 26, 30, 26, 'hay', rings=3, segs=6)
        w.box(px, H(px, pz) + 70, pz, 70, 14, 14, 'timber', col='none')
    beam(w, (yx - 160, H(yx - 160, yz + 170) + 40, yz + 170), (yx + 180, H(yx + 180, yz + 190) + 40, yz + 190), 44, 40, 'bark', col='prop', surf='wood')
    chest(w, yx + 120, H(yx + 120, yz + 40), yz + 40, 'training yard')
    house(w, cx + 380, cz - 160, 280, 260, face_yaw(cx + 380, cz - 160, cx, cz), 1, style(roof='roof_red', roof_kind='gable'), back_door=False, name='quartermaster shed', roof_rise=120)
    chest(w, cx + 200, H(cx + 200, cz + 380), cz + 380, 'terrace edge')
    signpost(w, cx + 120, cz + 420, 0.9)

def academy_bridge(w):
    """The stone bridge up from the plateau to the academy terrace, over the channel (its deck climbs, at most 0.3 per unit)."""
    x, z = region(w, 'Academy Bridge')
    a = (-2200, PLATEAU + 4, -1600)
    b = (-2950, TERRACE + 4, -2350)
    sloped_bridge(w, a, b, width=220, piers=2, name='Academy Bridge')
    chest(w, (a[0] + b[0]) / 2 + 40, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 - 40, 'Academy Bridge middle')
    for end, s in ((a, 1), (b, -1)):
        for side in (-1, 1):
            tx, tz = end[0] + side * 150, end[2] - side * 150
            w.box(tx, min(end[1], H(tx, tz)) - 40, tz, 80, max(220, end[1] - H(tx, tz) + 260), 80, 'castle_stone', 0.78, surf='stone')
            lamp(w, tx, tz, min(end[1], H(tx, tz)) - 40 + max(220, end[1] - H(tx, tz) + 260))

# ---- the plateau: Bazaar, Windmill Hill, the plaza and the diving decks -----------------------------------------------------------
def bazaar(w):
    cx, cz = region(w, 'Bazaar')
    y = H(cx, cz)
    # the great tent: a ring of posts, a low counter wall with four openings, the striped roof on a drum (drawn), a finial pole
    R = 560
    ring_wall(w, cx, cz, R, 30, y - 10, y + 60, 'town_stone', gaps=[(0.0, 260), (math.pi / 2, 260), (math.pi, 260), (-math.pi / 2, 260)], n=16)
    for k in range(16):
        a = k * 2 * math.pi / 16 + math.pi / 16
        px, pz = cx + R * math.cos(a), cz + R * math.sin(a)
        w.cylinder(px, pz, 14, y + 60, y + 330, 'timber', n=6, col='prop')
    w.far = True
    w.cone(cx, cz, R + 90, y + 330, y + 720, 'canvas_stripe', n=16, bottom=True)
    w.cylinder(cx, cz, 60, y + 700, y + 760, 'canvas_blue', n=8, col='none')
    w.cylinder(cx, cz, 6, y + 760, y + 900, 'timber', n=5, col='none')
    w.decal(cx + 3, y + 820, cz, 70, 50, 1, 0, 'banner_red', off=0)
    w.far = False
    w.interior.append((cx - R, cx + R, cz - R, cz + R, y - 10, y + 330))
    # the shops inside: potion counter, scrap shop, item check, fortune teller's booth, the cafe
    for k, (dx, dz, ang) in enumerate([(-300, -200, 0.4), (300, -220, -0.5), (-320, 220, 2.4), (320, 240, -2.3)]):
        stall(w, cx + dx, cz + dz, ang, k)
    w.cylinder(cx, cz, 110, y, y + 36, 'planks', n=8, col='prop', surf='wood', top_mat='timber')     # the fortune teller's round table
    w.sphere(cx, y + 60, cz, 22, 22, 22, 'ice', rings=3, segs=8)
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(cx + 150 * math.cos(a), cz + 150 * math.sin(a), 11, y, y + 18, 'planks', n=6, col='none')
    for k, (dx, dz) in enumerate([(0, -360), (-420, 0), (0, 380), (430, -40)]):
        chest(w, cx + dx, y, cz + dz, 'bazaar stall %d' % k)
    chest(w, cx + 40, y + 36, cz, 'fortune table')
    # outside: lamps at the four doors, crates, the bell post, the bazaar's own well
    for a in (0.0, math.pi / 2, math.pi, -math.pi / 2):
        for s in (-1, 1):
            lamp(w, cx + (R + 70) * math.cos(a) + s * 170 * -math.sin(a), cz + (R + 70) * math.sin(a) + s * 170 * math.cos(a))
    for k in range(5):
        crate(w, cx + 700 + (k % 3) * 44, cz - 300 + (k // 3) * 44, H(cx + 700, cz - 300), 38 + (k % 2) * 6, k * 0.3)
    barrel(w, cx - 720, cz + 260, H(cx - 720, cz + 260)); barrel(w, cx - 690, cz + 300, H(cx - 690, cz + 300))
    bx, bz = cx + 640, cz + 420
    for sx in (-1, 1): w.box(bx + sx * 50, H(bx, bz), bz, 12, 260, 12, 'timber', col='none')
    w.box(bx, H(bx, bz) + 250, bz, 130, 14, 14, 'timber', col='none')
    w.cylinder(bx, bz, 22, H(bx, bz) + 190, H(bx, bz) + 240, 'gold', n=8, col='none', r1=16)
    signpost(w, cx + 60, cz + 700, 0.3); signpost(w, cx - 760, cz - 560, 2.2)
    chest(w, cx + 720, H(cx + 720, cz - 380), cz - 380, 'bazaar crates'); chest(w, cx - 760, H(cx - 760, cz + 320), cz + 320, 'bazaar barrels')
    windmill_hill(w)

def windmill(w, x, z, yaw_to, name):
    y = H(x, z)
    w.far = True
    house(w, x, z, 360, 360, face_yaw(x, z, *yaw_to), 2, HouseStyle(wall='whitewash', base='town_stone', trim='timber', roof='roof_orange', roof_kind='hip',
          floor='planks', tint=(1, 0.98, 0.94)), name=name, roof_rise=260)
    f = Frame(x, z, face_yaw(x, z, *yaw_to))
    hx, hz = f.p(-190, 0)
    hub = (hx, y + 2 * STOREY + 140, hz)
    ax, az = f.dir(0, 1)       # the sails turn in the plane of the house's front
    for k in range(4):
        a = k * math.pi / 2 + 0.3
        ey, er = math.sin(a) * 440, math.cos(a) * 440
        py_, pr = math.cos(a) * 40, -math.sin(a) * 40
        w.face([(hub[0] + ax * pr * 0.2, hub[1] + py_ * 0.2, hub[2] + az * pr * 0.2), (hub[0] + ax * (er + pr), hub[1] + ey + py_, hub[2] + az * (er + pr)),
                (hub[0] + ax * (er - pr), hub[1] + ey - py_, hub[2] + az * (er - pr)), (hub[0] - ax * pr * 0.2, hub[1] - py_ * 0.2, hub[2] - az * pr * 0.2)], 'cloth', double=True)
    nx, nz = f.dir(-1, 0)
    w.cylinder(hx - nx * 20, hz - nz * 20, 28, hub[1] - 28, hub[1] + 28, 'timber', n=6, col='none')
    w.far = False

def windmill_hill(w):
    x, z = region(w, 'Windmill Hill')
    windmill(w, x, z, (x - 1000, z + 1600), 'North windmill')
    chest(w, x + 300, H(x + 300, z + 120), z + 120, 'windmill hill'); chest(w, x - 320, H(x - 320, z - 200), z - 200, 'knoll north')
    for k in range(5):
        a = k * 1.3 + 0.5
        rock(w, x + 520 * math.cos(a), z + 480 * math.sin(a), 0.8 + 0.2 * (k % 3), 200 + k, solid=k % 2 == 0)
    oak(w, x - 420, z + 380, 1.1, 210); bush(w, x + 380, z - 300, 1.0, 211)

def statue_plaza(w):
    cx, cz = region(w, 'Statue Plaza')
    y = H(cx, cz)
    # the bird statue in the middle: a stone loftwing on a plinth, wings spread (drawn), the plinth climbable
    w.cylinder(cx, cz, 160, y - 30, y + 60, 'castle_stone', n=12, surf='stone')
    w.cylinder(cx, cz, 110, y + 60, y + 120, 'castle_stone', n=12, surf='stone', climb=True)
    chest(w, cx + 60, y + 120, cz, 'bird statue plinth')
    w.far = True
    w.box(cx, y + 120, cz, 70, 110, 150, 'castle_stone', 0.0, col='none')                    # body
    w.box(cx, y + 230, cz - 90, 50, 60, 70, 'castle_stone', col='none')                      # head
    w.cone(cx, cz - 140, 18, y + 250, y + 300, 'gold', n=5)                                   # beak (drawn)
    for s in (-1, 1):
        w.solid([(cx + s * 35, y + 200, cz), (cx + s * 35, y + 170, cz + 60), (cx + s * 320, y + 330, cz - 40), (cx + s * 300, y + 300, cz + 80)],
                [(0, 1, 3), (0, 3, 2), (0, 2, 1), (1, 2, 3)], 'castle_stone', col='none')
    w.far = False
    # the ring: lamps, benches, a fountain pool, the notice board, the plaza's chests
    for k in range(8):
        a = k * math.pi / 4
        lamp(w, cx + 420 * math.cos(a), cz + 420 * math.sin(a))
        if k % 2: bench(w, cx + 330 * math.cos(a), cz + 330 * math.sin(a), a + math.pi / 2)
    fx, fz = cx - 300, cz + 260
    w.cylinder(fx, fz, 90, H(fx, fz) - 20, H(fx, fz) + 36, 'castle_stone', n=10, surf='stone')
    w.cylinder(fx, fz, 76, H(fx, fz) + 24, H(fx, fz) + 30, 'water', n=10, col='none')
    w.cylinder(fx, fz, 12, H(fx, fz) + 30, H(fx, fz) + 110, 'castle_stone', n=6, col='none')
    bx, bz = cx + 360, cz - 300
    w.box(bx, H(bx, bz), bz, 140, 160, 14, 'planks', 0.4, col='prop', surf='wood')
    chest(w, cx + 250, y, cz + 250, 'plaza east'); chest(w, cx - 250, y, cz - 250, 'plaza west')
    signpost(w, cx + 80, cz + 560, 0.0); signpost(w, cx - 600, cz + 100, 1.4)
    # the two diving decks
    x, z = region(w, 'West Diving Deck'); deck_over_sea(w, x, z, face_yaw(x, z, x - 600, z + 800), name='West Diving Deck')
    x, z = region(w, 'South Diving Deck'); deck_over_sea(w, x, z, face_yaw(x, z, x, z + 1000), name='South Diving Deck')
    rng = rng_for('plaza oaks')
    for k in range(4):
        a = k * 1.6 + 0.9
        px, pz = cx + 760 * math.cos(a), cz + 700 * math.sin(a)
        if terrain.road_dist(np.array(px), np.array(pz)) < 170: continue
        oak(w, px, pz, rng.uniform(0.9, 1.2), 300 + k)

# ---- the Goddess Statue and its hill ------------------------------------------------------------------------------------------------
def goddess_statue(w):
    cx, cz = region(w, 'Goddess Statue')
    py = H(cx, cz)
    # the shrine under the statue: a stone hall with a flat roof you walk onto from the hill's back; the statue stands on the roof's plinth
    house(w, cx, cz + 160, 460, 400, face_yaw(cx, cz + 160, cx - 1000, cz + 800), 1, HouseStyle(wall='castle_stone', base='castle_stone', trim='castle_stone',
          roof='castle_stone', roof_kind='flat', floor='cobble', climb=True), back_door=False, furnish=False, name='Goddess shrine', door_width=130)
    top = py + 14 + STOREY
    w.box(cx, py - 40, cz - 220, 420, 60, 380, 'castle_stone', surf='stone')
    px, pz = cx, cz - 220
    w.box(px, py + 20, pz, 300, 60, 260, 'castle_stone', surf='stone', climb=(0, 1, 2, 3))
    chest(w, px + 180, py + 20, pz - 150, 'statue plinth'); chest(w, cx - 150, py + 14, cz + 160, 'shrine'); chest(w, cx + 150, py + 14 + STOREY, cz + 160, 'shrine roof')
    w.far = True
    base = py + 80
    w.cylinder(px, pz, 120, base, base + 420, 'castle_stone', n=8, surf='stone', r1=90)           # the robe
    w.box(px, base + 420, pz, 170, 120, 100, 'castle_stone', col='none')                           # the shoulders
    w.cylinder(px, pz - 10, 50, base + 540, base + 640, 'castle_stone', n=8, col='none')          # the head
    w.box(px, base + 640, pz, 150, 30, 60, 'castle_stone', col='none')                             # the crown
    for s in (-1, 1):                                                                              # the arms, held out over the isle
        w.solid([(px + s * 80, base + 460, pz), (px + s * 80, base + 400, pz + 40), (px + s * 300, base + 330, pz + 60), (px + s * 280, base + 380, pz + 10)],
                [(0, 1, 3), (0, 3, 2), (0, 2, 1), (1, 2, 3)], 'castle_stone', col='none')
        w.solid([(px + s * 60, base + 460, pz - 20), (px + s * 160, base + 120, pz - 80), (px + s * 200, base + 420, pz - 60)], [(0, 1, 2), (0, 2, 1)], 'castle_stone', col='none')   # the wings' folds
    w.far = False
    # the hill top: standing stones, a ring of lamps and the goddess's own chests
    for k in range(6):
        a = k * math.pi / 3 + 0.2
        sx, sz = cx + 440 * math.cos(a), cz + 400 * math.sin(a)
        w.box(sx, H(sx, sz) - 20, sz, 50, 180 + 30 * (k % 3), 36, 'moss_stone', a, col='prop', surf='stone')
    for a in np.radians([60, 180, 300]):
        lamp(w, cx + 300 * math.cos(a), cz + 300 * math.sin(a))
    for k in range(8):      # pines on the hill's lower slopes
        a = k * 0.78 + 0.2; r = 1150 + 120 * (k % 2)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if H(px, pz) < PLATEAU + 60 or terrain.road_dist(np.array(px), np.array(pz)) < 170 or near_any(px, pz, 60) or terrain.coast_distance(np.array(px), np.array(pz)) > -350: continue
        pine(w, px, pz, 1.0 + 0.2 * (k % 3), 350 + k)
    chest(w, cx + 380, H(cx + 380, cz + 300), cz + 300, 'hill top east'); chest(w, cx - 400, H(cx - 400, cz - 200), cz - 200, 'hill top west')
    signpost(w, cx - 420, cz + 420, 2.4)
    goddess_stair(w)
    loftwing_roost(w)

def goddess_stair(w):
    """The switchback stair road from the plateau up to the statue's hill, with lamps at the bends."""
    x, z = region(w, 'Goddess Stair')
    pts = [(1650, H(1650, -1250) + 4, -1250), (2050, 250, -1100), (1950, 390, -1500), (2350, 520, -1700), (2600, STATUE + 4, -1800)]
    pts[1] = (2050, max(pts[1][1], H(2050, -1100) + 4), -1100)
    stone_ramp_road(w, pts, width=180)
    for px, py, pz in pts[1:4]:
        lamp(w, px + 110, pz, py)
    chest(w, pts[2][0] - 40, pts[2][1], pts[2][2] + 40, 'stair landing')

def loftwing_roost(w):
    cx, cz = region(w, 'Loftwing Roost')
    y = H(cx, cz)
    # the rope bridge from the statue hill's shoulder out to the crag
    sx, sz = 3450, -2550
    sy = H(sx, sz)
    ex, ez = cx - 330, cz + 250
    a = (sx, sy + 4, sz); b = (ex, y + 4, ez)
    L = math.hypot(ex - sx, ez - sz)
    if abs(b[1] - a[1]) / L > MAX_SLOPE:
        # a stone step at the hill end lifts the bridge's start: the crag is high
        lift = abs(b[1] - a[1]) - MAX_SLOPE * 0.9 * L
        w.box(sx, sy - 20, sz, 200, lift + 20, 200, 'moss_stone', surf='stone', climb=(0, 1, 2, 3))
        w.ramp(sx - 100 - lift / 0.5, sz, sx - 100, sz, 120, H(sx - 100 - lift / 0.5, sz) - 4, sy + lift, 'moss_stone', base=sy - 60)
        w.walkways.append((sx - 100 - lift / 0.5, sz, sx - 100, sz, 60, H(sx - 100 - lift / 0.5, sz) - 4, sy + lift))
        a = (sx, sy + lift + 4, sz)
    sloped_bridge(w, a, b, width=130, deck_mat='planks', piers=1, name='Roost bridge', rope=True)
    # the nests: rings of hay and twigs, eggs (drawn), a windbreak of rocks, the roost chests
    rng = rng_for('roost')
    for k, (dx, dz) in enumerate([(0, -40), (-170, 160), (190, 120)]):
        nx, nz = cx + dx, cz + dz
        w.cylinder(nx, nz, 120, H(nx, nz) - 10, H(nx, nz) + 40, 'hay', n=9, col='prop', surf='wood', top_mat='hay')
        for e in range(3):
            w.sphere(nx + 30 * math.cos(e * 2.1), H(nx, nz) + 52, nz + 30 * math.sin(e * 2.1), 16, 20, 16, 'plaster', rings=3, segs=6)
        chest(w, nx + 150, H(nx + 150, nz), nz, 'roost nest %d' % k)
    for k in range(6):
        a = k * 1.05 + 0.3; r = 330 + 40 * (k % 2)
        rock(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(1.0, 1.5), 400 + k, solid=k % 2 == 0)
    w.box(cx + 60, y, cz + 260, 10, 180, 10, 'timber', col='none'); w.decal(cx + 62, y + 110, cz + 260, 60, 100, 1, 0, 'banner_red', off=6)

# ---- the east cliffs: the Waterfall Cave and the pool --------------------------------------------------------------------------------
def waterfall_cave(w):
    cx, cz = region(w, 'Waterfall Cave')
    # the falls: a water sheet from the reservoir's lip down the gorge's end wall, drawn
    top = CLIFFS - 30
    fx, fz = 4250, -1620
    gy = H(fx, fz + 220)          # the gorge floor at the cave's mouth
    w.far = True
    for k in range(3):
        w.face([(fx - 90 + k * 60, gy, fz), (fx - 30 + k * 60, gy, fz), (fx - 30 + k * 60, top, fz - 20), (fx - 90 + k * 60, top, fz - 20)], 'water', double=True)
    w.far = False
    # the cave: the gorge's end is walled with rock and roofed by a stone arch; chests on two ledges inside and one on the floor
    rng = rng_for('cave')
    for k in range(7):
        a = -math.pi / 2 + (k - 3) * 0.36
        rx, rz = fx + 480 * math.cos(a), fz + 180 + 420 * math.sin(a)
        rock(w, rx, rz, rng.uniform(1.3, 1.9), 500 + k, solid=True)
    w.box(fx, gy + 250, fz + 80, 420, 60, 120, 'rock', col='none')
    w.box(fx - 170, gy - 10, fz + 120, 120, 70, 100, 'rock', surf='stone')
    chest(w, fx - 170, gy + 60, fz + 120, 'cave ledge')
    w.box(fx + 170, gy - 10, fz + 80, 100, 120, 100, 'rock', surf='stone', climb=(0,))
    chest(w, fx + 170, gy + 110, fz + 80, 'cave high ledge')
    chest(w, fx, H(fx, fz + 300), fz + 300, 'cave floor')
    # the reservoir: the pump house and a sluice beam over the lip
    house(w, 3620, -2080, 280, 260, face_yaw(3620, -2080, 4200, -1900), 1, style(wall='town_stone', roof='roof_teal', roof_kind='gable'), back_door=False, name='pump house', roof_rise=120)
    x0, x1, z0, z1, wy = POOLS[0]
    beam(w, (x0 - 40, H(x0 - 40, (z0 + z1) / 2) + 4, (z0 + z1) / 2), (x0 + 200, wy + 24, (z0 + z1) / 2), 80, 16, 'planks', surf='wood')
    chest(w, x1 + 60, H(x1 + 60, z0 - 60), z0 - 60, 'reservoir lip')
    for k in range(5):
        a = k * 1.3; r = 560 + 60 * (k % 2)
        px, pz = 4000 + r * math.cos(a), -1900 + r * math.sin(a)
        if H(px, pz) < CLIFFS - 80: continue
        pine(w, px, pz, 1.0 + 0.15 * (k % 3), 600 + k)
    waterfall_pool(w)

def waterfall_pool(w):
    x, z = region(w, 'Waterfall Pool')
    # a jetty from the west shore, a moored boat, the Zora's rock, rocks round the rim
    ay = H(x - 420, z) + 4
    beam(w, (x - 420, ay, z), (x - 120, WATER_Y + 40, z), 90, 18, 'planks', surf='wood')
    chest(w, x - 180, ay + (WATER_Y + 40 - ay) * (240 / 300), z, 'pool jetty')
    boat(w, x + 60, z + 160, 0.8)
    rng = rng_for('pool rocks')
    for k in range(7):
        a = k * 0.9 + 0.2; r = 430 + 40 * (k % 3)
        px, pz = x + r * math.cos(a), z + r * math.sin(a)
        if H(px, pz) < WATER_Y - 30 or terrain.road_dist(np.array(px), np.array(pz)) < 150: continue
        rock(w, px, pz, rng.uniform(0.8, 1.3), 700 + k, solid=False)
    signpost(w, x - 520, z + 160, 1.2)

def boat(w, x, z, yaw, L=240, W=90):
    y = WATER_Y - 8
    f = Frame(x, z, yaw)
    hull = [f.p(0, L / 2), f.p(W / 2, L / 4), f.p(W / 2, -L / 2), f.p(-W / 2, -L / 2), f.p(-W / 2, L / 4)]
    w.prism(hull, y - 20, y + 30, 'planks', col='prop', surf='wood', top_mat='timber')
    px, pz = f.p(0, 0)
    w.cylinder(px, pz, 6, y + 30, y + 240, 'timber', n=5, col='none')
    sx, sz = f.p(0, -L * 0.35)
    w.face([(px, y + 80, pz), (px, y + 230, pz), (sx, y + 90, sz)], 'cloth', double=True)

# ---- the fields, the homes, the graveyard ---------------------------------------------------------------------------------------------
def pumpkin_fields(w):
    cx, cz = region(w, 'Pumpkin Fields')
    fs = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='thatch', roof_kind='gable', floor='planks')
    house(w, cx + 260, cz + 380, 480, 400, face_yaw(cx + 260, cz + 380, cx, cz), 2, fs, name='Pumpkin barn', roof_rise=220, door_width=140)
    windmill(w, cx + 520, cz - 420, (cx, cz), 'East windmill')
    # three fenced patches of pumpkins, a scarecrow, a hay cart and the farmer's tool shed
    rng = rng_for('pumpkins')
    for k, (px, pz) in enumerate([(cx - 420, cz - 300), (cx - 420, cz + 200), (cx + 40, cz - 220)]):
        fence(w, [(px + sx * 230, pz + sz * 170) for sx, sz in [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]], 50)
        for i in range(4):
            for j in range(3):
                qx, qz = px - 160 + i * 105 + rng.uniform(-12, 12), pz - 100 + j * 100 + rng.uniform(-10, 10)
                pumpkin_plant(w, qx, qz, rng.uniform(0.8, 1.3), 800 + k * 20 + i * 3 + j)
        chest(w, px + 190, H(px + 190, pz - 130), pz - 130, 'pumpkin patch %d' % k)
    sx, sz = cx - 150, cz
    w.cylinder(sx, sz, 8, H(sx, sz), H(sx, sz) + 140, 'timber', n=5, col='none')
    w.box(sx, H(sx, sz) + 105, sz, 100, 10, 10, 'timber', col='none')
    w.sphere(sx, H(sx, sz) + 160, sz, 22, 26, 22, 'hay', rings=3, segs=6)
    w.box(cx - 100, H(cx - 100, cz + 480), cz + 480, 160, 50, 90, 'planks', 0.3, col='prop', surf='wood')
    w.cylinder(cx - 100, cz + 480, 44, H(cx - 100, cz + 480) + 50, H(cx - 100, cz + 480) + 100, 'hay', n=8, col='none', top_mat='hay')
    house(w, cx - 560, cz + 540, 260, 260, face_yaw(cx - 560, cz + 540, cx, cz), 1, style(wall='timber', roof='thatch', roof_kind='gable'), back_door=False, name='tool shed', roof_rise=110)
    signpost(w, cx - 200, cz - 620, 0.4)
    for k in range(5):
        px, pz = cx + 900 * math.cos(k * 1.25 + 0.6), cz + 800 * math.sin(k * 1.25 + 0.6)
        if terrain.road_dist(np.array(px), np.array(pz)) < 160 or near_any(px, pz, 60): continue
        oak(w, px, pz, 1.0, 900 + k)
    east_span(w)

def east_span(w):
    x, z = region(w, 'East Span')
    a = (2260, PLATEAU + 4, 1950); b = (3000, PLATEAU + 44, 2150)
    sloped_bridge(w, a, b, width=130, deck_mat='planks', piers=1, name='East Span', rope=True)
    chest(w, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 + 30, 'East Span middle')
    for end in (a, b):
        for side in (-1, 1):
            w.box(end[0] + side * 95 * 0.26, end[1] - 10, end[2] - side * 95 * 0.96, 30, 200, 30, 'timber', 0.26, surf='wood')

def westwind_homes(w):
    cx, cz = region(w, 'Westwind Homes')
    homes = [(-380, -320, 2, 'roof_blue', (0.95, 0.95, 1.0)), (320, -360, 1, 'roof_green', (0.92, 1.0, 0.9)), (560, 120, 2, 'roof_teal', (1.0, 0.95, 0.88)),
             (-520, 300, 1, 'roof_blue', (1.0, 0.92, 0.9)), (60, 540, 2, 'roof_orange', (1.0, 0.97, 0.88)), (-120, -40, 1, 'roof_red', (1.0, 1.0, 1.0))]
    for i, (dx, dz, st, roof, tint) in enumerate(homes):
        x, z = cx + dx, cz + dz
        house(w, x, z, 360 if st > 1 else 300, 360 if st > 1 else 300, face_yaw(x, z, cx, cz), st, style(roof=roof, roof_kind='gable' if i % 2 else 'hip', tint=tint),
              name=f'Westwind cottage {i}', roof_rise=150)
    # gardens: fences, bushes, a lookout post on the cliff, washing lines, the homes' chest in the garden
    fence(w, [(cx - 700, cz - 500), (cx - 700, cz + 100)], 60); fence(w, [(cx + 300, cz + 560), (cx + 700, cz + 420)], 60)
    for k in range(14):
        a = k * 0.45; r = 700 + 40 * (k % 2)
        if terrain.road_dist(np.array(cx + r * math.cos(a)), np.array(cz + r * math.sin(a))) < 120: continue
        bush(w, cx + r * math.cos(a), cz + r * math.sin(a), 0.9 + 0.2 * (k % 3), 1000 + k)
    lx, lz = cx - 820, cz + 600
    spiral_tower(w, lx, lz, 240, 360, yaw=face_yaw(lx, lz, cx, cz), wall='timber', floor='planks', roof='roof_blue', name='west lookout', cap='hip')
    chest(w, cx + 260, H(cx + 260, cz + 300), cz + 300, 'cottage garden'); chest(w, cx - 600, H(cx - 600, cz - 120), cz - 120, 'west garden')
    signpost(w, cx + 700, cz - 100, 1.5)
    oak(w, cx + 820, cz - 500, 1.1, 1100); oak(w, cx - 300, cz - 760, 1.0, 1101)
    fledges_cove(w)
    beedles_perch(w)

def fledges_cove(w):
    x, z = region(w, "Fledge's Cove")
    y = H(x, z)
    # the dock out from the beach, two boats, nets and barrels, the boathouse
    beam(w, (x, y + 4, z + 100), (x, WATER_Y + 40, z + 460), 100, 18, 'planks', surf='wood')
    chest(w, x, WATER_Y + 40 + (y + 4 - WATER_Y - 40) * (80 / 360), z + 380, 'dock end')
    boat(w, x + 160, z + 420, 0.2); boat(w, x - 170, z + 520, 2.8)
    for k in range(3): barrel(w, x + 120 + k * 42, z + 20, H(x + 120 + k * 42, z + 20))
    house(w, x - 260, z - 60, 280, 260, face_yaw(x - 260, z - 60, x, z + 400), 1, style(wall='planks', roof='thatch', roof_kind='gable', surf='wood'), back_door=False, name='boathouse', roof_rise=120)
    rng = rng_for('cove rocks')
    for k in range(5):
        a = k * 1.2 + 0.4
        rock(w, x + 420 * math.cos(a), z + 380 * math.sin(a), rng.uniform(0.9, 1.4), 1200 + k, solid=k % 2 == 0)

def beedles_perch(w):
    x, z = region(w, "Beedle's Perch")
    # the merchant's tower on its islet, and the bridge from the west lobe
    spiral_tower(w, x, z, 260, 460, yaw=face_yaw(x, z, x + 1000, z + 300), wall='timber', floor='planks', roof='roof_orange', name="Beedle's shop", cap='hip')
    a = (-4760, PLATEAU + 34, 1630); b = (-5280, 44, 1470)
    sloped_bridge(w, a, b, width=140, deck_mat='planks', piers=1, name="Beedle's bridge", rope=True)
    chest(w, x + 220, H(x + 220, z - 160), z - 160, "Beedle's crates")
    for k in range(3): crate(w, x + 200 + k * 40, z - 200, H(x + 200 + k * 40, z - 200), 36, k * 0.4)
    w.box(x + 300, H(x + 300, z + 160), z + 160, 10, 220, 10, 'timber', col='none'); w.decal(x + 302, H(x + 300, z + 160) + 130, z + 160, 70, 110, 1, 0, 'banner_red', off=6)

def hollow_graveyard(w):
    x, z = region(w, 'Hollow Graveyard')
    rng = rng_for('graves')
    for i in range(5):
        for j in range(3):
            gx, gz = x - 260 + i * 130 + rng.uniform(-15, 15), z + 100 + j * 120
            y = H(gx, gz)
            w.box(gx, y - 10, gz, 50, 76 + rng.uniform(-14, 14), 16, 'moss_stone', rng.uniform(-.15, .15), col='prop', surf='stone')
    house(w, x, z - 260, 360, 360, face_yaw(x, z - 260, x, z + 300), 1, HouseStyle(wall='brick_dark', base='brick_dark', trim='moss_stone', roof='brick_dark',
          roof_kind='flat', floor='cobble', climb=True), back_door=False, name='old crypt')
    fence(w, [(x - 380, z - 20), (x - 380, z + 460), (x + 380, z + 460), (x + 380, z - 20)], 70, 'iron')
    for k in range(5):
        dead_tree(w, x - 520 + k * 260, z + 600 + (k % 2) * 90, 0.9 + 0.15 * k, 70 + k)
    w.box(x + 250, H(x + 250, z + 340), z + 340, 90, 30, 150, 'moss_stone', col='prop', surf='stone')
    chest(w, x - 250, H(x - 250, z + 370), z + 370, 'graveyard corner'); chest(w, x + 250, H(x + 250, z + 60), z + 60, 'graveyard tomb')
    house(w, x + 560, z - 80, 300, 300, face_yaw(x + 560, z - 80, x, z), 1, style(roof='roof_teal', roof_kind='gable', tint=(0.9, 0.9, 0.95)), name='Graveyard house 0', roof_rise=130)
    signpost(w, x - 520, z - 200, 2.0)
    pumpkin_bridge(w)

def pumpkin_bridge(w):
    x, z = region(w, 'Pumpkin Bridge')
    a = (4720, PLATEAU + 44, 3290); b = (5330, 64, 3800)
    sloped_bridge(w, a, b, width=160, deck_mat='planks', wall_mat='timber', piers=2, name='Pumpkin Bridge', rope=True)
    chest(w, (a[0] + b[0]) / 2 - 40, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 + 50, 'Pumpkin Bridge middle')
    lamp(w, a[0], a[2] - 100, a[1]); lamp(w, b[0] + 100, b[2], b[1])

def lumpy_pumpkin(w):
    cx, cz = region(w, 'Lumpy Pumpkin')
    ps = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='pumpkin', roof_kind='hip', floor='planks', tint=(1.0, 0.96, 0.9), window='window_lit')
    y0, top = house(w, cx, cz, 520, 460, face_yaw(cx, cz, cx - 1000, cz - 400), 2, ps, name='Lumpy Pumpkin', roof_rise=260, door_width=140, stair_landing=True)
    w.far = True
    w.cylinder(cx, cz, 30, top + 250, top + 330, 'bark', n=6, col='none', r1=18)      # the stalk
    w.far = False
    # the pumpkin patch behind, the inn's yard with tables, the islet's lamp ring
    rng = rng_for('inn pumpkins')
    for k in range(8):
        a = k * 0.75 + 0.3; r = 400 + 40 * (k % 2)
        pumpkin_plant(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(0.9, 1.4), 1300 + k)
    f = Frame(cx, cz, face_yaw(cx, cz, cx - 1000, cz - 400))
    for k in range(2):
        table(w, f, -200 + k * 400, 360, H(*f.p(-200 + k * 400, 360)), 0.0)
        stool(w, f, -200 + k * 400, 310, H(*f.p(-200 + k * 400, 310))); stool(w, f, -200 + k * 400, 410, H(*f.p(-200 + k * 400, 410)))
    for a in np.radians([20, 140, 260]):
        lamp(w, cx + 430 * math.cos(a), cz + 400 * math.sin(a))
    chest(w, cx + 380, H(cx + 380, cz - 300), cz - 300, 'inn yard'); chest(w, cx - 380, H(cx - 380, cz + 300), cz + 300, 'inn patch')
    barrel(w, cx - 340, cz + 60, H(cx - 340, cz + 60)); barrel(w, cx - 340, cz + 110, H(cx - 340, cz + 110))

# ---- the south: the Light Tower, Ember Bridge and Ember Rock -------------------------------------------------------------------------
def light_tower(w):
    cx, cz = region(w, 'Light Tower')
    spiral_tower(w, cx, cz, 320, 780, yaw=face_yaw(cx, cz, cx - 2500, cz - 2300), wall='whitewash', floor='planks', roof='roof_red', name='Light Tower', cap='hip')
    w.far = True
    w.box(cx, H(cx, cz) + 12 + 780 + 170 + 50, cz, 70, 70, 70, 'window_lit', col='none')
    w.far = False
    house(w, cx - 380, cz - 120, 300, 280, face_yaw(cx - 380, cz - 120, cx, cz), 1, style(wall='whitewash', roof='roof_red', roof_kind='gable'), back_door=False, name='keeper cottage', roof_rise=130)
    for k in range(4): barrel(w, cx + 260 + (k % 2) * 44, cz + 200 + (k // 2) * 44, H(cx + 260, cz + 200))
    chest(w, cx + 300, H(cx + 300, cz - 260), cz - 260, 'headland rocks'); chest(w, cx - 200, H(cx - 200, cz + 300), cz + 300, 'headland south')
    rng = rng_for('headland rocks')
    for k in range(6):
        a = k * 1.1 + 0.2; r = 430 + 50 * (k % 2)
        px, pz = cx + r * math.cos(a), cz + r * math.sin(a)
        if H(px, pz) < WATER_Y - 30 or terrain.road_dist(np.array(px), np.array(pz)) < 150: continue
        if pz > 4300 and 1400 < px < 1900: continue        # the Ember Bridge's landing stays clear
        if near_any(px, pz, 60): continue
        rock(w, px, pz, rng.uniform(0.9, 1.4), 1400 + k, solid=k % 2 == 0)
    signpost(w, cx - 200, cz - 520, 0.3)
    ember_bridge(w)
    ember_rock(w)

def ember_bridge(w):
    x, z = region(w, 'Ember Bridge')
    a = (1650, HEADLAND + 4, 4480); b = (1690, 84, 5240)
    sloped_bridge(w, a, b, width=180, deck_mat='cobble', wall_mat='brick_dark', piers=2, name='Ember Bridge')
    chest(w, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 + 40, 'Ember Bridge middle')

def ember_rock(w):
    cx, cz = region(w, 'Ember Rock')
    py = H(cx, cz)
    fs = HouseStyle(wall='brick_dark', base='lava_rock', trim='iron', roof='brick_dark', roof_kind='flat', floor='cobble', tint=(1.0, 0.9, 0.84), window='window_lit')
    house(w, cx + 60, cz - 80, 400, 360, face_yaw(cx + 60, cz - 80, cx, cz + 600), 2, fs, name='Scrap smithy', door_width=130)
    hx, hz = cx + 60, cz - 440
    w.cylinder(hx, hz, 60, py - 20, py + 520, 'brick_dark', n=8, surf='stone')
    w.far = True
    w.cylinder(hx, hz, 44, py + 520, py + 560, 'lava_rock', n=8, col='none')
    w.far = False
    for k, (dx, dz) in enumerate([(-200, 60), (-120, 140), (-260, 160)]):
        w.box(cx + dx, py, cz + dz, 50, 30, 24, 'iron', k * 0.4, col='prop')
    w.box(cx - 300, py, cz - 40, 120, 36, 50, 'planks', col='prop', surf='wood')
    w.cylinder(cx - 300, cz - 40, 50, py + 36, py + 40, 'water', n=8, col='none')
    rng = rng_for('ember lava')
    for k in range(4):
        a = k * 1.4 + 0.5; r = 240 + 40 * (k % 2)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        w.cylinder(x, z, rng.uniform(50, 90), H(x, z) - 30, H(x, z) + 3, 'lava_rock', n=9, col='none')
    w.cylinder(cx - 120, cz + 240, 100, py - 40, py + 48, 'brick_dark', n=8, surf='stone')
    chest(w, cx - 120, py + 48, cz + 240, 'smithy dais'); chest(w, cx + 300, H(cx + 300, cz + 200), cz + 200, 'scrap pile')
    for k in range(5):
        crate(w, cx + 280 + (k % 3) * 44, cz + 240 + (k // 3) * 44, H(cx + 280 + (k % 3) * 44, cz + 240 + (k // 3) * 44), 36, k * 0.5)
    for k in range(6):
        a = k * 1.05 + 0.2; r = 420 + 40 * (k % 2)
        if cz + r * math.sin(a) < cz - 250 and abs(cx + r * math.cos(a) - 1690) < 260: continue        # not on the bridge's landing
        rock(w, cx + r * math.cos(a), cz + r * math.sin(a), rng.uniform(0.9, 1.5), 1500 + k, 'lava_rock', solid=k % 2 == 0)

# ---- the meadow between everything -------------------------------------------------------------------------------------------------
def field(w):
    """Lone oaks, bushes and rocks over the plateau, the cliff-edge fences, signposts at the junctions."""
    rng = rng_for('field oaks')
    pts = poisson(rng, (-HALF_X + 600, -HALF_Z + 600), (HALF_X - 600, HALF_Z - 600), 440, 3600)
    n = 0
    for x, z in pts:
        h = H(x, z)
        if h < WATER_Y + 80 or h > 560: continue
        if terrain.slope_up(np.array(x), np.array(z)) < 0.82: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 220: continue
        if near_any(x, z, 450): continue
        if terrain.coast_distance(np.array(x), np.array(z)) > -400: continue
        pick = rng.random()
        if pick < 0.45:
            oak(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
        elif pick < 0.6:
            pine(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
        elif pick < 0.85:
            bush(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
            bush(w, x + 70, z + 30, rng.uniform(0.7, 1.0), 2100 + n)
            if rng.random() < 0.5: bush(w, x - 60, z + 50, rng.uniform(0.6, 0.9), 2200 + n)
        else:
            s = rng.uniform(1.0, 1.5)
            rock(w, x, z, s, 2000 + n, solid=s > 1.3)
        n += 1
    for (x, z, yaw) in [(600, -100, 0.2), (2200, 1000, 0.9), (-1800, -1300, 2.3), (-1600, 2200, 0.5), (1000, 3200, 1.1), (2700, 100, 0.0)]:
        signpost(w, x + 90, z + 70, yaw)
    # a shepherd's shelter and a hay cart in the north meadow, a watch post over the academy channel
    sx, sz = 600, -1800
    for s in (-1, 1): w.box(sx + s * 110, H(sx, sz), sz, 9, 150, 9, 'timber', col='none')
    w.gable(sx, sz, 220, 240, H(sx, sz), 150, 'cloth', 0.0, overhang=0, col=None)
    chest(w, sx, H(sx, sz), sz, 'shepherd tent')
    tx, tz = -1900, -900
    spiral_tower(w, tx, tz, 240, 360, yaw=face_yaw(tx, tz, 0, 0), wall='timber', floor='planks', roof='thatch', name='channel watch post', cap='hip')

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
    ('Knight Academy', knight_academy), ('Bazaar', bazaar), ('Statue Plaza', statue_plaza), ('Goddess Statue', goddess_statue),
    ('Waterfall Cave', waterfall_cave), ('Pumpkin Fields', pumpkin_fields), ('Westwind Homes', westwind_homes), ('Hollow Graveyard', hollow_graveyard),
    ('Lumpy Pumpkin', lumpy_pumpkin), ('Light Tower', light_tower), ('Skyloft Field', field),
]

def build_all(w, only=None):
    _WORLD[0] = w
    for name, fn in BUILDERS:
        if isinstance(only, str): only = only.split(',')
        if only and 'all' not in only and name not in only: continue
        fn(w)
