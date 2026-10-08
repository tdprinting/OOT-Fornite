"""Every place on Hyrule Kingdom, built from the kit. Positions come from layout.py. Nothing on this map is scattered at random when a
match starts: every building, tree and rock is laid here, once, and baked into the map."""
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

CLOCK = dict(wall='plaster', base='town_stone', trim='timber', floor='planks')
def style(**kw):
    d = dict(CLOCK); d.update(kw); return HouseStyle(**d)

# ---- Clock Town -----------------------------------------------------------------------------------------------------------------------
def clock_town(w):
    cx, cz = 0, 1150
    w.markers.append({'name': 'Clock Town', 'kind': 'region', 'x': cx, 'z': cz})
    # The clock tower in the middle of the plaza: ramps spiral up inside to a look-out under the bell roof; the west face is ivy you can climb
    spiral_tower(w, cx, cz, 520, 1250, yaw=0.0, wall='town_stone', floor='cobble', roof='roof_red', clock=True, name='Clock Tower', cap='spire')
    houses = [
        (620, 640, 3, 'flat', 'roof_red', (1.0, 0.95, 0.88)),
        (860, 1180, 2, 'hip', 'roof_red', (1.0, 1.0, 1.0)),
        (600, 1720, 2, 'flat', 'roof_red', (0.92, 0.96, 1.0)),
        (-560, 1760, 3, 'flat', 'roof_blue', (1.0, 0.92, 0.86)),
        (-880, 1120, 2, 'gable', 'roof_red', (0.96, 0.92, 0.84)),
        (-620, 600, 2, 'hip', 'roof_green', (1.0, 0.97, 0.9)),
    ]
    for i, (x, z, st, kind, roof, tint) in enumerate(houses):
        yaw = face_yaw(x, z, cx, cz)
        house(w, x, z, 540, 600, yaw, storeys=st, style=style(roof=roof, roof_kind=kind, tint=tint), name=f'Clock Town house {i}', roof_rise=230)
    # Market stalls round the plaza: striped awnings on poles, crates and pots
    for k, a in enumerate(np.radians([20, 70, 115, 200, 245, 295, 340])):
        r = 420
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        stall(w, x, z, a + math.pi / 2, k)
    # Lamps along the north street to the bridge
    for z in (760, 640):
        for x in (-150, 150): lamp(w, x, z)

def stall(w, x, z, yaw, k):
    y = H(x, z)
    f = Frame(x, z, yaw)
    for sx in (-1, 1):
        for sz in (-1, 1):
            px, pz = f.p(sx * 85, sz * 55)
            w.box(px, y, pz, 10, 170, 10, 'timber', yaw, col='none')
    w.hip(x, z, 190, 130, y + 170, 45, 'roof_red' if k % 2 else 'roof_blue', yaw, overhang=12, col=None)
    w.box(x, y, z, 170, 75, 60, 'planks', yaw, col='prop', surf='wood')
    px, pz = f.p(40, 0); pot(w, px, pz, y + 75, 14)
    px, pz = f.p(-60, 70); crate(w, px, pz, y, 55, yaw)

def lamp(w, x, z):
    y = H(x, z)
    w.cylinder(x, z, 8, y, y + 210, 'iron', n=6, col='none')
    w.box(x, y + 210, z, 30, 34, 30, 'window_lit', col='none')
    w.hip(x, z, 30, 30, y + 244, 16, 'iron', overhang=5, col=None)

# ---- Castle Bridge and Hyrule Castle -----------------------------------------------------------------------------------------------
def castle_bridge(w):
    """A long arched stone viaduct from Clock Town's north gate, high over Zora's River, up to the castle gate (the Twilight Princess view)."""
    a = (0.0, H(0, 560) + 6, 560.0); b = (0.0, 526.0, -1020.0)
    width = 330
    beam(w, a, b, width, 60, 'cobble', surf='stone')
    # parapets you can hide behind
    for side in (-1, 1):
        ox = side * (width / 2 - 14)
        beam(w, (ox, a[1] + 85, a[2]), (ox, b[1] + 85, b[2]), 28, 85, 'castle_stone', surf='stone', block=True)
        # stone posts on the parapet
        for t in np.linspace(0.05, 0.95, 9):
            y = a[1] + (b[1] - a[1]) * t; z = a[2] + (b[2] - a[2]) * t
            w.box(ox, y + 80, z, 40, 40, 40, 'castle_stone', col='none')
    # piers and arches (the arches are drawn; the piers stand in the river and are solid)
    L = b[2] - a[2]
    for t in (0.3, 0.55, 0.78):
        z = a[2] + L * t; y = a[1] + (b[1] - a[1]) * t - 60
        bed = H(0, z)
        w.box(0, bed - 40, z, width - 40, y - bed + 40, 90, 'castle_stone', surf='stone')
        w.box(0, bed - 40, z, width + 30, 120, 140, 'castle_stone', col='none')
    for t0, t1 in ((0.05, 0.3), (0.3, 0.55), (0.55, 0.78), (0.78, 0.97)):
        z0 = a[2] + L * t0; z1 = a[2] + L * t1
        y0 = a[1] + (b[1] - a[1]) * t0 - 60; y1 = a[1] + (b[1] - a[1]) * t1 - 60
        # an arch on each face: the stone under the deck curves up between the piers (drawn)
        for side in (-1, 1):
            x = side * (width / 2 - 20)
            for k in range(8):
                ua, ub = k / 8, (k + 1) / 8
                za, zb = z0 + (z1 - z0) * ua, z0 + (z1 - z0) * ub
                ya, yb = y0 + (y1 - y0) * ua, y0 + (y1 - y0) * ub
                da = 60 + (1 - math.sin(math.pi * ua)) * 260; db = 60 + (1 - math.sin(math.pi * ub)) * 260
                w.face([(x, ya - da, za), (x, yb - db, zb), (x, yb, zb), (x, ya, za)], 'castle_stone', double=True)
    w.markers.append({'name': 'Castle Bridge', 'kind': 'landmark', 'x': 0, 'z': -230})

def hyrule_castle(w):
    cx, cz = 0, -1700
    py = 520
    w.markers.append({'name': 'Hyrule Castle', 'kind': 'region', 'x': cx, 'z': cz})
    x0, x1, z0, z1 = -640, 640, -2380, -1060
    t = 80; top = py + 400
    # Curtain walls: a ring with the main gate (south) and a postern (north east)
    g = 280
    south = [(-g / 2, z1), (x0, z1), (x0, z0), (300, z0), (300, z0 + t), (x0 + t, z0 + t), (x0 + t, z1 - t), (-g / 2, z1 - t)]
    north = [(g / 2, z1 - t), (x1 - t, z1 - t), (x1 - t, z0 + t), (440, z0 + t), (440, z0), (x1, z0), (x1, z1), (g / 2, z1)]
    w.prism(south, py - 60, top, 'castle_stone', surf='stone', climb=(1,))
    w.prism(north, py - 60, top, 'castle_stone', surf='stone')
    # Walkway on the walls is their top; battlements along the outer edge (drawn)
    for (ax, az, bx, bz) in [(x0, z1, x0, z0), (x0, z0, x1, z0), (x1, z0, x1, z1), (x0, z1, -g / 2, z1), (g / 2, z1, x1, z1)]:
        L = math.hypot(bx - ax, bz - az); n = int(L / 90)
        for k in range(0, n, 2):
            u = (k + .5) / n; px, pz = ax + (bx - ax) * u, az + (bz - az) * u
            w.box(px, top, pz, 45 if az == bz else t, 55, t if az == bz else 45, 'castle_stone', col='none')
    # Ramps up to the wall walk, inside the south wall
    for side in (-1, 1):
        w.ramp(side * 480, z1 - t - 70, side * 480, z1 - t - 70 - 560, 130, py, top, 'castle_stone', base=py - 10)
    # Corner towers (round, blue spires), one with ivy to climb
    for k, (tx, tz) in enumerate([(x0, z0), (x1, z0), (x1, z1), (x0, z1)]):
        w.cylinder(tx, tz, 150, py - 60, top + 300, 'castle_stone', n=10, surf='stone', climb=(k == 3))
        w.far = True
        w.cylinder(tx, tz, 165, top + 300, top + 330, 'castle_stone', n=10, col='none')
        w.cone(tx, tz, 170, top + 330, top + 760, 'roof_blue', n=10)
        w.cylinder(tx, tz, 6, top + 760, top + 830, 'gold', n=4, col='none')
        w.far = False
        w.loot.append((tx, top + 300, tz, 'castle tower top'))
    # Gatehouse: two square towers flanking the gate, a lintel walk between them
    for side in (-1, 1):
        w.box(side * (g / 2 + 90), py - 60, z1 - 20, 180, top + 160 - py + 60, 200, 'castle_stone', surf='stone')
        w.far = True
        w.hip(side * (g / 2 + 90), z1 - 20, 180, 200, top + 160, 260, 'roof_blue', overhang=15, col=None)
        w.far = False
        w.decal(side * (g / 2 + 90), py + 200, z1 + 81, 90, 160, 0, 1, 'banner_red', off=3)
    w.box(0, top - 20, z1 - 20, g + 10, 80, 200, 'castle_stone', surf='stone')
    # The keep: a great hall (two floors) with a flat roof, and the tall central spire behind it
    house(w, 0, -1880, 760, 620, 0.0, storeys=2, style=style(wall='castle_stone', base='castle_stone', trim='castle_stone', roof='roof_blue',
          roof_kind='flat', floor='cobble', window='window_lit', tint=(1, 1, 1)), name='Castle keep', floor_y=py + 14)
    w.far = True
    spiral_tower(w, 0, -2480, 380, 1500, yaw=0.0, wall='castle_stone', floor='cobble', roof='roof_blue', name='Castle spire', cap='spire', y0=py + 14)
    # The spire has its own walls, with a clear front entrance across the rear terrace.
    w.ramp(0,-2180,0,-2320,180,py+14,py+14,'castle_stone',surf='stone',base=py-20)
    w.walkways.append((0,-2180,0,-2320,90,py+14,py+14))
    # Side spires on the keep's corners (drawn, seen from everywhere)
    for sx in (-1, 1):
        for sz in (-1, 1):
            x = sx * 340; z = -1880 + sz * 270
            yb = py + 14 + 2 * STOREY + 85
            w.cylinder(x, z, 70, yb, yb + 260, 'castle_stone', n=8, col='none')
            w.cone(x, z, 90, yb + 260, yb + 560, 'roof_blue', n=8)
    w.far = False
    # Courtyard: a fountain, hedges, banners on the walls
    fy = py
    ring_wall(w, 0, -1450, 170, 40, fy, fy + 60, 'castle_stone', n=12)
    w.cylinder(0, -1450, 150, fy + 40, fy + 44, 'water', n=12, col='none')
    w.cylinder(0, -1450, 30, fy, fy + 190, 'castle_stone', n=8, surf='stone')
    w.cylinder(0, -1450, 70, fy + 150, fy + 165, 'castle_stone', n=8, col='none')
    for x in (-420, -300, 300, 420):
        bush(w, x, -1300, 1.0, int(x) & 255, 'leaves_dark', y=fy)
    for x in np.linspace(-520, 520, 6):
        w.decal(x, fy + 120, z0 + t + 1, 80, 220, 0, 1, 'banner_blue', off=2)
    # Pines round the plateau's edge, as in the reference
    rng = rng_for('castle pines')
    for k in range(26):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(780, 900)
        x, z = cx + r * math.cos(a), cz + r * math.sin(a)
        if abs(x) < 260 and z > -1100: continue    # keep the gate's approach clear
        if H(x, z) < py - 30: continue
        pine(w, x, z, rng.uniform(0.9, 1.25), k)

# ---- Great Hylia Bridge -------------------------------------------------------------------------------------------------------------
def great_bridge(w):
    """An old stone bridge high over Zora's River north east of the castle, with ruined gate towers at both ends (ref: TP's Hylia bridge)."""
    za = -2850
    xa, xb = 1150, 2650
    ya = H(xa, za) + 4; yb = H(xb, za) + 4
    y = max(ya, yb)
    # approach ramps at both ends up to the deck level, then a level deck
    mid0, mid1 = 1450, 2350
    beam(w, (xa, ya, za), (mid0, y, za), 300, 50, 'cobble')
    beam(w, (mid0, y, za), (mid1, y, za), 300, 50, 'cobble')
    beam(w, (mid1, y, za), (xb, yb, za), 300, 50, 'cobble')
    for side in (-1, 1):
        zz = za + side * 136
        beam(w, (mid0, y + 80, zz), (mid1, y + 80, zz), 26, 80, 'moss_stone', block=True)
        for xp in np.linspace(mid0, mid1, 8):
            w.box(xp, y + 75, zz, 36, 30, 36, 'moss_stone', col='none')
    for xp in (1700, 2100):
        bed = H(xp, za)
        w.box(xp, bed - 40, za, 120, y - 50 - bed + 40, 260, 'moss_stone', surf='stone')
    # Ruined gate towers: tall broken stumps either side of the road at each end
    for xp in (mid0 - 40, mid1 + 40):
        for side in (-1, 1):
            hgt = 520 if (xp < 2000) == (side < 0) else 330
            w.box(xp, y - 50, za + side * 230, 150, hgt, 150, 'moss_stone', surf='stone', climb=(0,))
            w.box(xp, y - 50 + hgt, za + side * 230, 110, 60, 90, 'moss_stone', col='none')
    w.loot.append((2000, y, za, 'bridge middle'))
    w.markers.append({'name': 'Great Hylia Bridge', 'kind': 'landmark', 'x': 1900, 'z': za})

# ---- Lake Hylia: the stilt village, the boardwalks and the lab --------------------------------------------------------------------
DECK_Y = -165

def stilts_deck(w, x, z, sx, sz, yaw=0.0, y=DECK_Y):
    w.box(x, y - 22, z, sx, 22, sz, 'planks', yaw, surf='wood', hookshot=True)
    f0 = Frame(x, z, yaw); a = f0.p(-sx / 2, 0); b = f0.p(sx / 2, 0)
    w.walkways.append((a[0], a[1], b[0], b[1], sz / 2, y, y))
    f = Frame(x, z, yaw)
    for lx in np.linspace(-sx / 2 + 15, sx / 2 - 15, max(2, int(sx / 220) + 1)):
        for lz in np.linspace(-sz / 2 + 15, sz / 2 - 15, max(2, int(sz / 220) + 1)):
            px, pz = f.p(lx, lz)
            w.cylinder(px, pz, 14, H(px, pz) - 10, y - 22, 'timber', n=6, col='none', top=False)

def boardwalk(w, a, b, width=150, y=DECK_Y):
    (ax, az), (bx, bz) = a, b
    L = math.hypot(bx - ax, bz - az)
    beam(w, (ax, y, az), (bx, y, bz), width, 22, 'planks', surf='wood')
    for k in range(int(L / 260) + 1):
        u = k / max(1, int(L / 260))
        px, pz = ax + (bx - ax) * u, az + (bz - az) * u
        for side in (-1, 1):
            nx, nz = -(bz - az) / L * side * (width / 2 - 10), (bx - ax) / L * side * (width / 2 - 10)
            w.cylinder(px + nx, pz + nz, 12, H(px + nx, pz + nz) - 10, y + 60, 'timber', n=6, col='none', top=True)

def lake_stilts(w):
    cx, cz = -2950, 2350
    w.markers.append({'name': 'Lake Hylia Stilts', 'kind': 'region', 'x': cx, 'z': cz})
    ls = HouseStyle(wall='planks', base='timber', trim='timber', roof='thatch', roof_kind='gable', floor='planks', climb=True, surf='wood')
    decks = [(-2600, 2050, 560, 520, 0.2), (-3350, 1950, 520, 480, -0.3), (-2900, 2800, 600, 520, 0.1), (-3450, 3000, 480, 480, 0.5), (-2200, 2700, 420, 420, 0.0)]
    for i, (x, z, sx, sz, yaw) in enumerate(decks):
        stilts_deck(w, x, z, sx, sz, yaw)
        if i < 4:
            house(w, x, z, 360, 330, yaw + (math.pi if i % 2 else 0), 1, ls, back_door=True, name=f'stilt hut {i}', floor_y=DECK_Y, roof_rise=170, footing=False)
    # Boardwalks between the decks and to the east shore
    walks = [((-2600, 2050), (-3350, 1950)), ((-2600, 2050), (-2900, 2800)), ((-2900, 2800), (-3450, 3000)), ((-2900, 2800), (-2200, 2700)),
             ((-2200, 2700), (-1900, 2550)), ((-2600, 2050), (-2250, 1800)), ((-3450, 3000), (-3800, 2850))]
    for a, b in walks: boardwalk(w, a, b)
    # steps out of the water at the ends of the decks
    for x, z in [(-3350, 2230), (-2700, 3080), (-3900, 2850), (-2300, 2300)]:
        w.box(x, WATER_Y - 30, z, 120, 70, 80, 'planks', surf='wood')
    for x, z in [(-2500, 2250), (-3000, 3000)]:
        barrel(w, x, z, DECK_Y); crate(w, x + 70, z - 40, DECK_Y, 60)
    # Fishing boats moored by the decks
    for x, z, yaw in [(-3250, 1800, 0.4), (-2350, 2800, -0.6)]:
        boat(w, x, z, yaw)

def boat(w, x, z, yaw, L=360, W=130):
    y = WATER_Y - 10
    f = Frame(x, z, yaw)
    hull = [f.p(0, L / 2), f.p(W / 2, L / 4), f.p(W / 2, -L / 2), f.p(-W / 2, -L / 2), f.p(-W / 2, L / 4)]
    w.prism(hull, y - 30, y + 40, 'planks', col='prop', surf='wood', top_mat='timber')
    px, pz = f.p(0, 0)
    w.cylinder(px, pz, 8, y + 40, y + 340, 'timber', n=5, col='none')
    sx, sz = f.p(0, -L * 0.35)
    w.face([(px, y + 110, pz), (px, y + 330, pz), (sx, y + 120, sz)], 'cloth', double=True)

def lakeside_lab(w):
    x, z, r = LAB_ISLAND
    w.markers.append({'name': 'Lakeside Lab', 'kind': 'region', 'x': x, 'z': z})
    house(w, x + 60, z + 60, 480, 420, -0.4, 1, style(wall='plaster', roof='roof_blue', roof_kind='hip', tint=(0.95, 1.0, 1.05)), name='Lakeside Lab', roof_rise=180)
    spiral_tower(w, x - 170, z - 160, 360, 950, yaw=-0.4, wall='town_stone', floor='planks', roof='roof_blue', name='Lab tower', cap='hip')
    for k in range(6):
        a = k * 1.05 + 0.3
        bush(w, x + 300 * math.cos(a), z + 300 * math.sin(a), 0.8, k, 'leaves')

# ---- Gerudo Ruins ----------------------------------------------------------------------------------------------------------------
def gerudo_ruins(w):
    cx, cz = 4100, 3650
    py = 140
    w.markers.append({'name': 'Gerudo Ruins', 'kind': 'region', 'x': cx, 'z': cz})
    gs = HouseStyle(wall='sandstone', base='sandstone', trim='sandstone', roof='sandstone', roof_kind='flat', floor='sand', tint=(1.0, 0.97, 0.92), climb=True)
    # A broken ring wall round the ruins, three ways in
    ring_wall(w, cx, cz, 640, 70, py - 40, py + 300, 'sandstone', gaps=[(math.pi * 1.05, 300), (math.pi * 0.25, 260), (math.pi * 1.6, 260)], n=20, climb=True)
    # The stepped temple in the middle: three terraces with ramps on the south face, a shrine with a chest on top
    tiers = [(560, py, py + 150), (400, py + 150, py + 300), (250, py + 300, py + 450)]
    for k, (s, y0, y1) in enumerate(tiers):
        w.box(cx, y0 - (60 if k == 0 else 0), cz - 40, s, y1 - y0 + (60 if k == 0 else 0), s, 'sandstone', surf='sand')
        run = (y1 - y0) / 0.62
        zf = cz - 40 + s / 2
        w.ramp(cx + (110 if k % 2 else -110), zf + run, cx + (110 if k % 2 else -110), zf, 120, y0, y1, 'sandstone', surf='sand', base=y0 - 5)
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.cylinder(cx + sx * 95, cz - 40 + sz * 95, 22, py + 450, py + 650, 'sandstone', n=8, col='prop')
    w.hip(cx, cz - 40, 240, 240, py + 650, 120, 'sandstone', overhang=10, col=None)
    w.loot.append((cx, py + 450, cz - 40, 'temple top'))
    # The broken colossus: a seated stone giant against the north wall (blocky, drawn), its lap a platform you can climb onto
    colossus(w, cx + 40, cz - 560)
    # Sandstone houses with roof decks
    for i, (x, z) in enumerate([(cx - 420, cz + 300), (cx + 430, cz + 280), (cx - 380, cz - 280)]):
        house(w, x, z, 440, 440, face_yaw(x, z, cx, cz), 1, gs, name=f'Gerudo house {i}')
    # Colonnades
    for k in range(6):
        x = cx + 300 + k * 0; z = cz - 250 + k * 110
        w.cylinder(cx + 330, z, 30, py - 10, py + 380, 'sandstone', n=8, col='prop')
        w.box(cx + 330, py + 380, z, 80, 30, 80, 'sandstone', col='none')
    for k in range(4):
        w.cylinder(cx - 240 + k * 160, cz + 470, 30, py - 10, py + 250 + 60 * (k % 2), 'sandstone', n=8, col='prop')
    # tents
    for k, (x, z) in enumerate([(cx + 200, cz + 420), (cx - 150, cz + 120)]):
        tent(w, x, z, k)

def colossus(w, x, z):
    y = H(x, z)
    w.far = True
    # plinth and lap (solid), body, head and arms (drawn, chunky), then the broken top
    w.box(x, y - 40, z, 520, 220, 360, 'sandstone', surf='sand')                       # plinth
    w.box(x, y + 180, z + 20, 420, 140, 300, 'sandstone', surf='sand')                  # lap: a platform 320 up
    w.box(x, y + 180, z - 150, 360, 520, 160, 'sandstone', surf='sand', climb=(2,))    # torso, its front climbable
    for sx in (-1, 1):
        w.box(x + sx * 230, y + 420, z - 100, 110, 260, 140, 'sandstone', col='none')    # shoulders
        w.box(x + sx * 210, y + 320, z + 60, 90, 90, 240, 'sandstone', col='none')       # forearms on the knees
        w.box(x + sx * 120, y - 40, z + 170, 120, 230, 120, 'sandstone', col='none')     # shins
    w.box(x, y + 700, z - 150, 210, 230, 200, 'sandstone', col='none')                   # head
    w.box(x, y + 930, z - 150, 260, 40, 230, 'sandstone', col='none')                    # headdress
    w.box(x - 70, y + 790, z - 49, 40, 30, 4, 'iron', col='none'); w.box(x + 70, y + 790, z - 49, 40, 30, 4, 'iron', col='none')
    w.far = False
    w.loot.append((x, y + 320, z + 60, 'colossus lap'))

def tent(w, x, z, k):
    y = H(x, z)
    f = Frame(x, z, k * 0.7)
    for sx in (-1, 1):
        px, pz = f.p(sx * 150, 0)
        w.box(px, y, pz, 12, 200, 12, 'timber', k * 0.7, col='none')
    w.gable(x, z, 300, 320, y, 200, 'roof_red' if k % 2 else 'cloth', k * 0.7, overhang=0, col=None)

# ---- Snowpeak ----------------------------------------------------------------------------------------------------------------------
def snowpeak(w):
    cx, cz = -2600, -4700
    w.markers.append({'name': 'Snowpeak Lodge', 'kind': 'region', 'x': cx, 'z': cz})
    ss = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='snow', roof_kind='gable', floor='planks', tint=(1.0, 0.96, 0.92), window='window_lit')
    house(w, cx, cz, 820, 620, 0.15, 2, ss, name='Snowpeak Lodge', roof_rise=360)
    for i, (dx, dz) in enumerate([(-650, 380), (620, 330), (560, -380)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 380, 360, face_yaw(x, z, cx, cz + 300), 1, ss, back_door=False, name=f'Snowpeak cabin {i}', roof_rise=200)
    rng = rng_for('snowpeak pines')
    placed = 0
    for k in range(400):
        x = cx + rng.uniform(-2400, 2400); z = cz + rng.uniform(-1800, 1600)
        if placed > 70: break
        h = H(x, z)
        if h < 300 or terrain.slope_up(np.array(x), np.array(z)) < 0.75: continue
        if any(math.hypot(x - bx, z - bz) < 650 for bx, bz in [(cx, cz), (cx - 650, cz + 380), (cx + 620, cz + 330), (cx + 560, cz - 380), FROZEN_POND[:2]]): continue
        if terrain.road_dist(np.array(x), np.array(z)) < 200: continue
        pine(w, x, z, rng.uniform(0.9, 1.4), 100 + k, snow=True); placed += 1
    # The Frozen Pond: a disc of ice on the shelf, an ice-fishing hut
    px, pz, pr = FROZEN_POND
    y = H(px, pz)
    w.cylinder(px, pz, pr, y - 20, y + 3, 'ice', n=16, surf='ice')
    w.markers.append({'name': 'Frozen Pond', 'kind': 'landmark', 'x': px, 'z': pz})
    house(w, px + pr + 150, pz + 60, 300, 300, face_yaw(px + pr + 150, pz + 60, px, pz), 1, ss, back_door=False, name='ice hut', roof_rise=160)

# ---- Death Mountain and the Goron lookout --------------------------------------------------------------------------------------------
def death_mountain(w):
    mx, mz = 4350, -4350
    w.markers.append({'name': 'Death Mountain', 'kind': 'region', 'x': mx, 'z': mz})
    floor = H(mx, mz)
    # Lava pools on the crater floor (drawn glowing rock) round an obsidian dais with the summit chest
    rng = rng_for('lava')
    for k in range(7):
        a = k * 0.9 + rng.uniform(0, .5); r = rng.uniform(200, 380)
        x, z = mx + r * math.cos(a), mz + r * math.sin(a)
        w.cylinder(x, z, rng.uniform(90, 150), H(x, z) - 30, H(x, z) + 4, 'lava_rock', n=9, col='none')
    w.cylinder(mx, mz, 170, floor - 40, floor + 70, 'brick_dark', n=8, surf='stone')
    w.loot.append((mx, floor + 70, mz, 'crater dais'))
    # Basalt pillars on the rim
    for k in range(10):
        a = k * 2 * math.pi / 10 + 0.2
        r = 0.22 * 2200 + 120
        x, z = mx + r * math.cos(a), mz + r * math.sin(a)
        rock(w, x, z, rng.uniform(0.9, 1.5), 300 + k, 'lava_rock', solid=False)
    # Goron Lookout: a squat stone tower on the trail
    gx, gz = 3350, -3700
    spiral_tower(w, gx, gz, 400, 650, yaw=face_yaw(gx, gz, 3900, -2700), wall='brick_dark', floor='cobble', roof='roof_red', name='Goron Lookout', cap='hip')
    w.markers.append({'name': 'Goron Lookout', 'kind': 'landmark', 'x': gx, 'z': gz})

# ---- Kakariko ---------------------------------------------------------------------------------------------------------------------
def kakariko(w):
    cx, cz = 4050, -1350
    w.markers.append({'name': 'Kakariko Village', 'kind': 'region', 'x': cx, 'z': cz})
    ks = lambda roof, tint=(1, 1, 1), kind='gable': HouseStyle(wall='plaster', base='town_stone', trim='timber', roof=roof, roof_kind=kind, floor='planks', tint=tint)
    houses = [(-430, -280, 2, 'roof_red', (1.0, 0.95, 0.86)), (420, -330, 1, 'thatch', (0.96, 0.92, 0.84)), (480, 280, 2, 'thatch', (1, 1, 1)),
              (-460, 320, 1, 'roof_red', (0.95, 0.9, 0.82)), (0, -560, 1, 'thatch', (1.0, 0.97, 0.9))]
    for i, (dx, dz, st, roof, tint) in enumerate(houses):
        x, z = cx + dx, cz + dz
        house(w, x, z, 480, 560 if st > 1 else 440, face_yaw(x, z, cx, cz), st, ks(roof, tint), name=f'Kakariko house {i}', roof_rise=220)
    # The well in the square
    ring_wall(w, cx, cz, 110, 30, H(cx, cz) - 10, H(cx, cz) + 75, 'town_stone', n=10)
    for sx in (-1, 1):
        w.box(cx + sx * 100, H(cx, cz), cz, 14, 220, 14, 'timber', col='none')
    w.gable(cx, cz, 240, 120, H(cx, cz) + 220, 70, 'thatch', math.pi / 2, overhang=10, col=None)
    # The lookout tower (wooden), the windmill on its hill, the graveyard
    spiral_tower(w, cx + 80, cz + 560, 360, 800, yaw=0.0, wall='timber', floor='planks', roof='thatch', name='Kakariko lookout', cap='hip')
    windmill(w, 5000, -500)
    graveyard(w, 5250, -2250)
    for k, (x, z) in enumerate([(cx - 700, cz - 650), (cx + 700, cz + 650), (cx - 760, cz + 600)]):
        oak(w, x, z, 1.1, 40 + k)

def windmill(w, x, z):
    y = H(x, z)
    w.markers.append({'name': 'Windmill Hill', 'kind': 'landmark', 'x': x, 'z': z})
    w.far = True
    house(w, x, z, 420, 420, face_yaw(x, z, 4050, -1350), 2, HouseStyle(wall='plaster', base='town_stone', trim='timber', roof='thatch', roof_kind='hip',
          floor='planks', tint=(1, 0.97, 0.9)), name='Windmill', roof_rise=420)
    # sails: four big lattice blades on the west face (drawn)
    hub = (x - 260, y + 2 * STOREY + 200, z)
    for k in range(4):
        a = k * math.pi / 2 + 0.3
        ex, ey = math.cos(a) * 650, math.sin(a) * 650
        px, py_ = -math.sin(a) * 60, math.cos(a) * 60
        w.face([(hub[0], hub[1] + py_ * 0.2, hub[2] + px * 0.2), (hub[0], hub[1] + ey + py_, hub[2] + ex + px),
                (hub[0], hub[1] + ey - py_, hub[2] + ex - px), (hub[0], hub[1] - py_ * 0.2, hub[2] - px * 0.2)], 'cloth', double=True)
    w.cylinder(hub[0] + 30, hub[2], 40, hub[1] - 40, hub[1] + 40, 'timber', n=6, col='none')
    w.far = False

def graveyard(w, x, z):
    w.markers.append({'name': 'Kakariko Graveyard', 'kind': 'landmark', 'x': x, 'z': z})
    rng = rng_for('graves')
    for i in range(4):
        for j in range(3):
            gx, gz = x - 260 + i * 170 + rng.uniform(-20, 20), z + 60 + j * 160
            y = H(gx, gz)
            w.box(gx, y - 10, gz, 70, 110 + rng.uniform(-20, 20), 22, 'moss_stone', rng.uniform(-.15, .15), col='prop', surf='stone')
    # the crypt: a small stone house with a flat roof and a chest inside
    house(w, x, z - 300, 380, 360, face_yaw(x, z - 300, x, z + 300), 1, HouseStyle(wall='brick_dark', base='brick_dark', trim='moss_stone', roof='brick_dark',
          roof_kind='flat', floor='cobble', climb=True), back_door=False, name='Crypt')
    fence(w, [(x - 420, z - 80), (x - 420, z + 480), (x + 420, z + 480), (x + 420, z - 80)], 100, 'iron')
    for k in range(3):
        oak(w, x - 450 + k * 450, z + 650, 0.9, 70 + k, leaves='leaves_dark')

# ---- Kokiri Forest and the Deku Tree ----------------------------------------------------------------------------------------------
def giant_tree(w, x, z, r, height, seed, platform=None, hut=False, far=True):
    """A great forest tree: a climbable trunk (scene collision), roots, a high canopy; optionally a ring deck round it with a hut."""
    y = H(x, z) - 20
    old = w.group; w.group = 'Foliage'; w.far = far
    w.cylinder(x, z, r * 1.5, y - 30, y + 80, 'bark', n=9, surf='wood', r1=r, climb=True)
    w.cylinder(x, z, r, y + 80, y + height, 'bark', n=9, surf='wood', r1=r * 0.75, climb=True)
    rng = np.random.default_rng(seed)
    for k in range(5):
        a = k * 2 * math.pi / 5 + rng.uniform(-.3, .3)
        bx, bz = x + math.cos(a) * r * 2.4, z + math.sin(a) * r * 2.4
        by = y + height * rng.uniform(0.75, 0.95)
        w.solid([(x, y + height * 0.65, z), (x + math.cos(a + 1.6) * 30, y + height * 0.7, z + math.sin(a + 1.6) * 30), (x, y + height * 0.75, z), (bx, by, bz)],
                [(0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)], 'bark', col='none')
        w.sphere(bx, by + 60, bz, r * 2.2, r * 1.2, r * 2.2, 'leaves_dark', rings=4, segs=8, seed=seed + k, jitter=0.15, squash_bottom=0.5)
    w.sphere(x, y + height + 80, z, r * 3.2, r * 1.7, r * 3.2, 'leaves', rings=5, segs=10, seed=seed + 9, jitter=0.12, squash_bottom=0.5)
    w.group = old; w.far = False
    if platform:
        py = y + platform
        R = r * 2.6
        # a deck round the trunk: two half rings (a ring with a hole is not one simple outline)
        for half in (0, 1):
            angs = np.linspace(half * math.pi, (half + 1) * math.pi, 7)
            outer = [(x + R * math.cos(a), z + R * math.sin(a)) for a in angs]
            inner = [(x + (r * 0.85) * math.cos(a), z + (r * 0.85) * math.sin(a)) for a in angs[::-1]]
            w.prism(outer + inner, py - 25, py, 'planks', surf='wood', hookshot=True)
        for a in np.linspace(0, 2 * math.pi, 9)[:-1]:
            px, pz = x + (R - 20) * math.cos(a), z + (R - 20) * math.sin(a)
            w.cylinder(px, pz, 8, py, py + 80, 'timber', n=5, col='none')
        w.loot.append((x + (R - 80), py, z, 'tree deck'))
        if hut:
            hx, hz = x + (r + 160) * math.cos(2.3), z + (r + 160) * math.sin(2.3)
            w.prism(rect(hx, hz, 240, 240, 2.3), py, py + 200, 'planks', col='none')
            w.hip(hx, hz, 240, 240, py + 200, 120, 'thatch', 2.3, overhang=25, col=None)
        # a log ramp from the ground up to the deck, for the bots (players can just climb the bark)
        a = rng.uniform(0, 2 * math.pi)
        run = platform / 0.6
        sx, sz = x + (R + run) * math.cos(a), z + (R + run) * math.sin(a)
        ex, ez = x + (R - 10) * math.cos(a), z + (R - 10) * math.sin(a)
        beam(w, (sx, H(sx, sz) - 5, sz), (ex, py, ez), 110, 30, 'planks', surf='wood')

def stump_house(w, x, z, r, seed, door_angle):
    y = H(x, z)
    w.cylinder(x, z, r + 30, y - 40, y + 14, 'bark', n=10, surf='wood', top_mat='planks')
    ring_wall(w, x, z, r, 40, y + 14, y + 280, 'bark', gaps=[(door_angle, 160)], n=12, climb=True)
    w.cone(x, z, r + 70, y + 280, y + 470, 'thatch', n=10)
    w.buildings.append((x, z, r * 0.7, r * 0.7, y + 14, 0.0))
    w.interior.append((x - r, x + r, z - r, z + r, y, y + 280))
    w.loot.append((x - math.cos(door_angle) * r * 0.5, y + 14, z - math.sin(door_angle) * r * 0.5, 'stump house'))
    bed(w, Frame(x, z, door_angle + math.pi / 2), r * 0.45, 0, y + 14)

def kokiri(w):
    cx, cz = -5050, -1150
    w.markers.append({'name': 'Kokiri Forest', 'kind': 'region', 'x': cx, 'z': cz})
    trees = [(-5450, -1550, 130, 1050, 320, True), (-4600, -1500, 120, 950, 300, False), (-4700, -700, 140, 1100, 340, True),
             (-5500, -700, 120, 1000, 0, False), (-5050, -1950, 110, 900, 280, False)]
    for k, (x, z, r, hgt, plat, hut) in enumerate(trees):
        giant_tree(w, x, z, r, hgt, 500 + k, platform=plat or None, hut=hut)
    for k, (x, z) in enumerate([(-5150, -900), (-4700, -1100), (-4950, -1350)]):
        stump_house(w, x, z, 170, 600 + k, math.atan2(cz - z, cx - x) + 0.5)
    # rope bridges between the two tree decks with huts
    deku_tree(w, -5750, -2300)
    fairy_fountain(w, -3700, -600)
    forest_fill(w)

def deku_tree(w, x, z):
    w.markers.append({'name': 'Deku Tree Hollow', 'kind': 'landmark', 'x': x, 'z': z})
    y = H(x, z)
    r = 330
    # a hollow trunk you walk into: thick bark walls round a doorway facing the forest, a ramp inside up to a branch deck
    a = math.atan2(-1150 - z, -5050 - x)
    w.cylinder(x, z, r + 120, y - 40, y + 14, 'bark', n=12, surf='wood', top_mat='planks')
    ring_wall(w, x, z, r, 90, y + 14, y + 900, 'bark', gaps=[(a, 200)], n=14, climb=True)
    w.buildings.append((x, z, r * 0.7, r * 0.7, y + 14, 0.0))
    w.interior.append((x - r, x + r, z - r, z + r, y, y + 900))
    # inside: a ramp up the back wall to a ledge at 420, and a hole in the bark to look out from
    ba = a + math.pi
    p0 = (x + math.cos(ba + 1.0) * (r - 110), z + math.sin(ba + 1.0) * (r - 110))
    p1 = (x + math.cos(ba - 0.3) * (r - 110), z + math.sin(ba - 0.3) * (r - 110))
    w.ramp(p0[0], p0[1], p1[0], p1[1], 120, y + 14, y + 330, 'planks', surf='wood', base=y + 10)
    lx, lz = x + math.cos(ba - 0.75) * (r - 120), z + math.sin(ba - 0.75) * (r - 120)
    w.cylinder(lx, lz, 120, y + 310, y + 330, 'planks', n=8, surf='wood')
    w.loot.append((lx, y + 330, lz, 'Deku Tree ledge'))
    w.loot.append((x, y + 14, z, 'Deku Tree hollow'))
    # crown and great roots
    w.far = True
    old = w.group; w.group = 'Foliage'
    w.sphere(x, y + 1250, z, 1000, 420, 1000, 'leaves', rings=5, segs=12, seed=77, jitter=0.12, squash_bottom=0.5)
    for k in range(6):
        aa = a + 0.5 + k * 1.0
        rx, rz = x + math.cos(aa) * (r + 330), z + math.sin(aa) * (r + 330)
        w.solid([(x + math.cos(aa) * r, y + 200, z + math.sin(aa) * r), (x + math.cos(aa + .25) * r, y - 20, z + math.sin(aa + .25) * r),
                 (x + math.cos(aa - .25) * r, y - 20, z + math.sin(aa - .25) * r), (rx, H(rx, rz) - 20, rz)], [(0, 1, 3), (0, 3, 2), (0, 2, 1), (1, 2, 3)], 'bark', col='none')
    w.group = old; w.far = False

def fairy_fountain(w, x, z):
    w.markers.append({'name': 'Fairy Fountain', 'kind': 'landmark', 'x': x, 'z': z})
    y = H(x, z)
    w.cylinder(x, z, 260, y - 30, y + 12, 'moss_stone', n=14, surf='stone')
    ring_wall(w, x, z, 200, 40, y + 12, y + 70, 'moss_stone', n=14)
    w.cylinder(x, z, 180, y + 30, y + 34, 'water', n=14, col='none')
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(x + 330 * math.cos(a), z + 330 * math.sin(a), 30, y, y + 260, 'moss_stone', n=6, col='prop')
    w.loot.append((x, y + 70, z, 'fairy fountain'))

def forest_fill(w):
    """Big shady trees through the painted woods (west), thick enough to hide in, thin enough to run through."""
    rng = rng_for('forest')
    pts = poisson(rng, (-6600, -3200), (-2600, 1200), 330, 900)
    n = 0
    for x, z in pts:
        if terrain.region(np.array(x), np.array(z), FOREST, 700) < 0.6: continue
        if H(x, z) < WATER_Y + 40 or terrain.slope_up(np.array(x), np.array(z)) < 0.72: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 170: continue
        if near_any(x, z, 420): continue
        s = rng.uniform(0.9, 1.5)
        if rng.random() < 0.25: pine(w, x, z, s, 900 + n)
        else: oak(w, x, z, s, 900 + n, leaves='leaves_dark' if rng.random() < 0.5 else 'leaves')
        n += 1

# ---- Lon Lon Ranch ----------------------------------------------------------------------------------------------------------------
def lon_lon(w):
    cx, cz = 1500, 4550
    py = 230
    w.markers.append({'name': 'Lon Lon Ranch', 'kind': 'region', 'x': cx, 'z': cz})
    ring_wall(w, cx, cz, 660, 60, py - 30, py + 170, 'town_stone', gaps=[(-math.pi / 2 - 0.35, 300), (math.pi * 0.05 - 0.6, 300), (math.pi * 0.95, 280)], n=24)
    rs = HouseStyle(wall='timber', base='town_stone', trim='planks', roof='roof_red', roof_kind='gable', floor='planks')
    house(w, cx - 330, cz - 120, 640, 560, face_yaw(cx - 330, cz - 120, cx, cz), 2, rs, name='Lon Lon barn', roof_rise=320)
    house(w, cx + 260, cz - 330, 460, 560, face_yaw(cx + 260, cz - 330, cx, cz), 2,
          HouseStyle(wall='plaster', base='town_stone', trim='timber', roof='roof_red', roof_kind='gable', tint=(1, .96, .9)), name='Ranch house', roof_rise=220)
    # the silo: a round tower with a ramp spiralling round the outside to a platform on top
    sx, sz = cx - 420, cz + 330
    sy = H(sx, sz)
    w.cylinder(sx, sz, 150, sy - 30, sy + 700, 'planks', n=10, surf='wood', climb=True, top_mat='planks')
    w.far = True
    w.cone(sx, sz, 175, sy + 700, sy + 900, 'roof_red', n=10)
    w.far = False
    w.loot.append((sx + 100, sy + 700, sz, 'silo top'))
    # paddock: a fence ring with hay
    fence(w, [(cx + 300 + 300 * math.cos(a), cz + 260 + 220 * math.sin(a)) for a in np.linspace(0.4, 2 * math.pi + 0.0, 14)], 90, 'timber')
    for k, (x, z) in enumerate([(cx + 100, cz + 450), (cx + 200, cz + 520), (cx - 100, cz + 150)]):
        w.cylinder(x, z, 55, H(x, z) - 5, H(x, z) + 90, 'hay', n=8, col='prop', top_mat='hay')
    for k in range(5):
        a = k * 1.25 + 0.2
        oak(w, cx + 900 * math.cos(a), cz + 900 * math.sin(a), 1.0, 1200 + k)

# ---- Temple Ruins ----------------------------------------------------------------------------------------------------------------
def temple_ruins(w):
    cx, cz = -1900, 4700
    py = 60
    w.markers.append({'name': 'Temple Ruins', 'kind': 'region', 'x': cx, 'z': cz})
    yaw = face_yaw(cx, cz, 0, 1150)       # the facade looks towards Clock Town
    f = Frame(cx, cz, yaw)
    lo, hi = ground_range(cx, cz, 1000, 760, yaw)
    base = hi + 30
    w.prism(f.outline(rect(0, 0, 1000, 760)), lo - 60, base - 30, 'moss_stone', surf='stone')
    w.prism(f.outline(rect(0, -20, 900, 680)), base - 30, base, 'moss_stone', surf='stone', top_mat='cobble')
    for side in (-1, 1):
        px, pz = f.p(side * 250, 380)
        ex, ez = f.p(side * 250, 380 + 120)
        w.ramp(ex, ez, px, pz, 200, H(ex, ez) - 4, base - 30, 'moss_stone', base=lo - 40)
    w.buildings.append((cx, cz, 500, 380, base, yaw))
    # broken walls: the hall's sides stand to different heights, the back is mostly gone
    segs = [((-430, -320), (-430, 300), 520), ((430, -320), (430, 40), 380), ((430, 40), (430, 300), 180), ((-430, -320), (-60, -320), 300), ((120, -320), (430, -320), 140)]
    for (ax, az), (bx, bz), hgt in segs:
        a = f.p(ax, az); b = f.p(bx, bz)
        L = math.hypot(b[0] - a[0], b[1] - a[1]); mx, mz = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        w.box(mx, base, mz, L + 50, hgt, 50, 'moss_stone', ang, surf='stone', climb=(0, 2))
    # the facade: two tall pillars and a lintel with the sealed door between
    for sx in (-1, 1):
        px, pz = f.p(sx * 260, 320)
        w.box(px, base, pz, 120, 760, 120, 'moss_stone', yaw, surf='stone', climb=(1,))
    px, pz = f.p(0, 320)
    w.box(px, base + 680, pz, 640, 110, 130, 'moss_stone', yaw, surf='stone')
    w.far = True
    w.gable(px, pz, 640, 130, base + 790, 200, 'moss_stone', yaw + math.pi / 2, overhang=10, col=None)
    w.far = False
    nx, nz = f.dir(0, 1)
    w.decal(px, base, pz, 300, 560, -nx, -nz, 'door', off=-60)
    # columns inside, some fallen
    for k, lz in enumerate(np.linspace(-200, 200, 4)):
        for sx in (-1, 1):
            px, pz = f.p(sx * 230, lz)
            hgt = 520 if (k + (sx > 0)) % 3 else 200
            w.cylinder(px, pz, 40, base, base + hgt, 'moss_stone', n=8, col='prop')
    a = f.p(-330, 60); b = f.p(-190, 230)
    beam(w, (a[0], base + 70, a[1]), (b[0], base + 70, b[1]), 80, 70, 'moss_stone', col='prop')
    # the bell tower stump at the back corner, open at the top
    tx, tz = f.p(-330, -230)
    spiral_tower(w, tx, tz, 360, 700, yaw=yaw, wall='moss_stone', floor='cobble', roof='moss_stone', name='Temple bell tower', cap='hip', y0=base)
    _lx, _lz = f.p(0, -100); w.loot.append((_lx, base, _lz, 'temple altar'))
    for k in range(4):
        bush(w, *f.p(-480 + k * 320, -460), 0.9, 30 + k, 'leaves_dark')

# ---- Ordon Docks and the lighthouse ------------------------------------------------------------------------------------------------
def ordon_docks(w):
    cx, cz = -5000, 5150
    w.markers.append({'name': 'Ordon Docks', 'kind': 'region', 'x': cx, 'z': cz})
    os_ = HouseStyle(wall='planks', base='town_stone', trim='timber', roof='thatch', roof_kind='gable', floor='planks', tint=(1, .95, .88), surf='wood')
    for i, (dx, dz) in enumerate([(200, -150), (250, 300), (-150, 380)]):
        x, z = cx + dx, cz + dz
        house(w, x, z, 420, 380, face_yaw(x, z, cx - 400, cz), 1, os_, name=f'Ordon house {i}', roof_rise=190)
    # Piers out over the outlet's water (west), a boat shed on the longest
    for k, (z, L) in enumerate([(cz - 250, 700), (cz + 100, 900)]):
        x0 = cx - 150
        beam(w, (x0, DECK_Y + 40, z), (x0 - L, DECK_Y + 40, z), 160, 26, 'planks', surf='wood')
        for xx in np.linspace(x0, x0 - L, int(L / 230) + 1):
            for side in (-1, 1):
                w.cylinder(xx, z + side * 70, 12, H(xx, z) - 10, DECK_Y + 80, 'timber', n=6, col='none')
        boat(w, x0 - L * 0.6, z + 180, 1.57)
    shed_x, shed_z = cx - 850, cz + 100
    for sx in (-1, 1):
        for sz in (-1, 1):
            w.box(shed_x + sx * 140, DECK_Y + 40, shed_z + sz * 70, 16, 240, 16, 'timber', col='none')
    w.gable(shed_x, shed_z, 300, 170, DECK_Y + 280, 120, 'thatch', math.pi / 2, overhang=30, col=None)
    w.loot.append((shed_x, DECK_Y + 40, shed_z, 'boat shed'))
    for k in range(4):
        barrel(w, cx + 50 + k * 70, cz - 420, H(cx + 50 + k * 70, cz - 420))
    # The lighthouse on its point
    lx, lz = -5700, 5900
    w.markers.append({'name': 'Lighthouse Point', 'kind': 'landmark', 'x': lx, 'z': lz})
    spiral_tower(w, lx, lz, 420, 1150, yaw=face_yaw(lx, lz, cx, cz), wall='plaster', floor='planks', roof='roof_red', name='Lighthouse', cap='hip')

# ---- Zora's Falls -----------------------------------------------------------------------------------------------------------------
def zoras_falls(w):
    px, pz = 2250, -5350
    w.markers.append({'name': "Zora's Falls", 'kind': 'region', 'x': px, 'z': pz})
    # The waterfall: a broad sheet down the cliff north of the pool (drawn), foam at its foot
    top = max(H(px + dx, pz - 520) for dx in (-200, 0, 200)) + 40
    w.far = True
    for k in range(6):
        x0 = px - 240 + k * 80; x1 = x0 + 80
        za = pz - 500 - 40 * math.sin(k); zb = pz - 330
        w.face([(x0, WATER_Y, zb), (x1, WATER_Y, zb), (x1, top, za), (x0, top, za)], 'water', double=True, uvs=[(0, 4), (1, 4), (1, 0), (0, 0)])
    w.far = False
    w.cylinder(px, pz - 320, 260, WATER_Y - 4, WATER_Y + 6, 'ice', n=12, col='none')
    # Stepping stones across the pool and a stone shrine on a ledge
    for k, (x, z) in enumerate([(px - 260, pz + 40), (px - 80, pz + 120), (px + 120, pz + 60), (px + 300, pz - 40)]):
        w.cylinder(x, z, 70, H(x, z) - 20, WATER_Y + 40, 'moss_stone', n=8, surf='stone')
    sx, sz = px + 520, pz + 120
    sy = H(sx, sz)
    w.cylinder(sx, sz, 200, sy - 40, sy + 20, 'moss_stone', n=10, surf='stone')
    for k in range(5):
        a = k * 2 * math.pi / 5
        w.cylinder(sx + 170 * math.cos(a), sz + 170 * math.sin(a), 22, sy + 20, sy + 260, 'moss_stone', n=6, col='prop')
    w.hip(sx, sz, 360, 360, sy + 260, 110, 'roof_blue', overhang=20, col=None)
    w.loot.append((sx, sy + 20, sz, 'Zora shrine'))

# ---- Blossom Oasis ---------------------------------------------------------------------------------------------------------------
def blossom_oasis(w):
    x, z, r = OASIS
    w.markers.append({'name': 'Blossom Oasis', 'kind': 'region', 'x': x, 'z': z})
    for k in range(12):
        a = k * 2 * math.pi / 12 + 0.2
        rr = r + 300 + 120 * (k % 2)
        blossom_tree(w, x + rr * math.cos(a), z + rr * math.sin(a), 1.1 + 0.2 * (k % 3), 1300 + k)
    # a pavilion on the east rim
    pxp, pzp = x + r + 480, z - 80
    py = H(pxp, pzp)
    w.cylinder(pxp, pzp, 230, py - 30, py + 18, 'sandstone', n=8, surf='sand')
    for k in range(6):
        a = k * math.pi / 3
        w.cylinder(pxp + 190 * math.cos(a), pzp + 190 * math.sin(a), 20, py + 18, py + 260, 'sandstone', n=6, col='prop')
    w.cone(pxp, pzp, 270, py + 260, py + 420, 'roof_red', n=8)
    w.loot.append((pxp, py + 18, pzp, 'oasis pavilion'))
    # a jetty into the pond
    a = (x + r + 60, z + 40); b = (x + r - 220, z + 40)
    beam(w, (a[0], H(*a) + 4, a[1]), (b[0], WATER_Y + 40, b[1]), 120, 22, 'planks', surf='wood')

# ---- Hyrule Field: oaks, ruins, cover -------------------------------------------------------------------------------------------
def field(w):
    w.markers.append({'name': 'Hyrule Field', 'kind': 'region', 'x': 2600, 'z': 800})
    rng = rng_for('field oaks')
    pts = poisson(rng, (-HALF_X + 600, -HALF_Z + 600), (HALF_X - 600, HALF_Z - 600), 700, 2600)
    n = 0
    for x, z in pts:
        h = H(x, z)
        if h < WATER_Y + 60 or h > 600: continue
        if terrain.region(np.array(x), np.array(z), FOREST, 700) > 0.3: continue
        if terrain.region(np.array(x), np.array(z), DESERT, 900) > 0.35: continue
        if terrain.slope_up(np.array(x), np.array(z)) < 0.82: continue
        if terrain.road_dist(np.array(x), np.array(z)) < 220: continue
        if near_any(x, z, 600): continue
        if terrain.coast_distance(np.array(x), np.array(z)) > -500: continue
        if rng.random() < 0.55:
            oak(w, x, z, rng.uniform(0.9, 1.35), 2000 + n)
        elif rng.random() < 0.5:
            bush(w, x, z, rng.uniform(0.9, 1.3), 2000 + n)
            bush(w, x + 90, z + 40, rng.uniform(0.7, 1.0), 2100 + n)
        else:
            rock(w, x, z, rng.uniform(1.1, 1.7), 2000 + n, solid=True)
        n += 1
    # The old gate on the field (ref: TP's ruined arch by the fences): two broken towers and a broken span
    gx, gz = 2650, -350
    yaw = 0.6
    f = Frame(gx, gz, yaw)
    for side in (-1, 1):
        px, pz = f.p(side * 230, 0)
        w.box(px, H(px, pz) - 30, pz, 150, 640 if side < 0 else 420, 160, 'moss_stone', yaw, surf='stone', climb=(1,))
    a = f.p(-160, 0); b = f.p(60, 0)
    beam(w, (a[0], H(gx, gz) + 610, a[1]), (b[0], H(gx, gz) + 540, b[1]), 140, 80, 'moss_stone')
    _lx, _lz = f.p(-230, 0); w.loot.append((_lx, H(_lx, _lz) + 610, _lz, 'old gate top'))
    w.markers.append({'name': 'Old Gate', 'kind': 'landmark', 'x': gx, 'z': gz})
    fence(w, [f.p(320, 60), f.p(1200, 260)], 100)
    fence(w, [f.p(-320, 60), f.p(-1100, 300)], 100)
    # Desert edge: a few dead trees and sandstone outcrops
    rng = rng_for('desert rocks')
    for k in range(18):
        x = 4200 + rng.uniform(-1900, 1900); z = 3700 + rng.uniform(-1700, 1700)
        if terrain.region(np.array(x), np.array(z), DESERT, 900) < 0.6 or near_any(x, z, 500) or H(x, z) < WATER_Y + 60: continue
        rock(w, x, z, rng.uniform(1.0, 1.8), 3000 + k, 'sandstone', solid=rng.random() < 0.5)
    # Mountain flanks: pines below the snow
    rng = rng_for('mountain pines')
    for k, (x, z) in enumerate(poisson(rng, (2000, -6800), (6800, -2600), 380, 400)):
        h = H(x, z)
        if h < 250 or h > 1150 or terrain.slope_up(np.array(x), np.array(z)) < 0.7 or near_any(x, z, 450): continue
        if terrain.road_dist(np.array(x), np.array(z)) < 200: continue
        pine(w, x, z, rng.uniform(0.9, 1.3), 4000 + k)

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
            if math.hypot(x - bx, z - bz) < max(hw, hd) + 260: return True
        for lx, ly, lz, *_ in w.loot:
            if math.hypot(x - lx, z - lz) < 220: return True
    for name, px, pz, rad, *_ in POIS:
        if name in ('Hyrule Field',): continue
        if math.hypot(x - px, z - pz) < rad * 0.75 + margin: return True
    if math.hypot(x, z - 1150) < 1150: return True                     # Clock Town
    if abs(x) < 300 and -900 < z < 200: return True                      # the castle bridge
    return False

BUILDERS = [
    ('Clock Town', clock_town), ('Castle Bridge', castle_bridge), ('Hyrule Castle', hyrule_castle), ('Great Hylia Bridge', great_bridge),
    ('Lake Hylia Stilts', lake_stilts), ('Lakeside Lab', lakeside_lab), ('Gerudo Ruins', gerudo_ruins), ('Snowpeak Lodge', snowpeak),
    ('Death Mountain', death_mountain), ('Kakariko Village', kakariko), ('Kokiri Forest', kokiri), ('Lon Lon Ranch', lon_lon),
    ('Temple Ruins', temple_ruins), ('Ordon Docks', ordon_docks), ("Zora's Falls", zoras_falls), ('Blossom Oasis', blossom_oasis),
    ('Hyrule Field', field),
]


def build_all(w, only=None):
    _WORLD[0] = w
    for name, fn in BUILDERS:
        if isinstance(only, str): only = only.split(',')
        if only and 'all' not in only and name not in only: continue
        fn(w)
