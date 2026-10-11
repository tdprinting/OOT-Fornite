"""Skyloft Isles ground: height, colour and cover, as numpy functions of game (x, z). No Blender needed, so previews are quick."""
import numpy as np
from layout import *

# ---- noise ------------------------------------------------------------------------------------------------------------------------
def _hash(ix, iz, salt):
    h = (ix.astype(np.int64) * 374761393 + iz.astype(np.int64) * 668265263 + salt * 2246822519 + 0x1F123BB5) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0

def value_noise(x, z, scale, salt):
    fx, fz = x / scale, z / scale
    ix, iz = np.floor(fx), np.floor(fz)
    u, v = fx - ix, fz - iz
    u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v)
    a = _hash(ix, iz, salt); b = _hash(ix + 1, iz, salt); c = _hash(ix, iz + 1, salt); d = _hash(ix + 1, iz + 1, salt)
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v

def fbm(x, z, scale, octaves, salt, gain=0.5):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * (value_noise(x, z, scale / (2 ** o), salt + o * 31) * 2 - 1)
        norm += amp; amp *= gain
    return total / norm

def ridged(x, z, scale, octaves, salt):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        n = 1 - np.abs(value_noise(x, z, scale / (2 ** o), salt + o * 17) * 2 - 1)
        total = total + amp * n * n; norm += amp; amp *= 0.5
    return total / norm

def smoothstep(a, b, t):
    s = np.clip((t - a) / (b - a), 0, 1)
    return s * s * (3 - 2 * s)

def seg_dist(x, z, pts):
    """Distance to a polyline of (x, z, halfwidth), and the half width interpolated at the nearest point."""
    best = np.full(np.shape(x), 1e9); width = np.zeros(np.shape(x))
    for (ax, az, aw), (bx, bz, bw) in zip(pts[:-1], pts[1:]):
        dx, dz = bx - ax, bz - az
        t = np.clip(((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz), 0, 1)
        d = np.hypot(x - ax - dx * t, z - az - dz * t)
        closer = d < best
        best = np.where(closer, d, best); width = np.where(closer, aw + (bw - aw) * t, width)
    return best, width

def road_dist(x, z):
    best = np.full(np.shape(x), 1e9)
    for road in ROADS:
        d, _ = seg_dist(x, z, [(px, pz, 0) for px, pz in road])
        best = np.minimum(best, d)
    return best

def region(x, z, circles, soft=600):
    w = np.zeros(np.shape(x))
    for cx, cz, r in circles:
        w = np.maximum(w, 1 - smoothstep(r - soft, r, np.hypot(x - cx, z - cz)))
    return w

# ---- the coast ----------------------------------------------------------------------------------------------------------------------
def _union(x, z, circles, wobble=0.0, salt=7):
    """Signed distance (negative inside) to the union of circles, with a little noise on the edge."""
    d = np.full(np.shape(x), 1e9)
    for cx, cz, r in circles:
        d = np.minimum(d, np.hypot(x - cx, z - cz) - r)
    if wobble: d = d + wobble * fbm(x, z, 900, 3, salt)
    return d

def channel_sea(x, z):
    """How far inside a sea channel (x, z) is (positive inside)."""
    best = np.full(np.shape(x), -1e9)
    for chain in (GULLY, CHANNEL):
        d, w = seg_dist(x, z, chain)
        best = np.maximum(best, w - d)
    return best

def gorge_dist(x, z):
    return seg_dist(x, z, GORGE)

def coast_distance(x, z):
    """How far (x, z) is out to sea from the coastline (negative on land)."""
    main = _union(x, z, MAIN_LOBES, 160, 7)
    acad = _union(x, z, ACADEMY_LOBE, 120, 9)
    isl = _union(x, z, [(ix, iz, ir) for ix, iz, ir, ih in ISLETS], 60, 11)
    cd = np.minimum(np.minimum(main, acad), isl)
    # the east gully: the sea runs up between the plateau and the graveyard lobe (bridged by the East Span)
    cd = np.maximum(cd, channel_sea(x, z))
    return np.maximum(cd, np.maximum(np.abs(x) - (HALF_X - 760), np.abs(z) - (HALF_Z - 760)))

# ---- height ------------------------------------------------------------------------------------------------------------------------
def height(x, z):
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    # the plateau: gentle, a little higher towards the north and the hills
    h = PLATEAU + 40 * fbm(x, z, 2000, 4, 1) + 18 * fbm(x, z, 600, 2, 3)
    # the statue hill and the academy lobe rise as broad domes before their pads flatten them
    for cx, cz, r, top in [(2800, -1800, 2000, STATUE), (-4200, -3800, 1700, ACADEMY), (3900, -600, 1300, CLIFFS), (4400, -3600, 700, ROOST)]:
        d = np.hypot(x - cx, z - cz) / r
        h = np.maximum(h, top * np.clip(1 - d, 0, None) ** 0.8 * (0.9 + 0.2 * ridged(x, z, 700, 3, int(cx) & 255)) + PLATEAU * np.clip(1 - d, 0, 1))
    # the coast: a short beach, then cliffs to the sea (Skyloft's edges are sheer)
    cd = coast_distance(x, z)
    h = h + (-250 - h) * smoothstep(-260, 0, cd)
    # islets keep their own height
    for ix, iz, ir, ih in ISLETS:
        d = np.hypot(x - ix, z - iz)
        if ih > 300:
            h = np.where(d < ir + 200, np.maximum(h, ih * np.clip(1 - d / (ir + 200), 0, 1) ** 0.6 - 250 * smoothstep(ir * 0.6, ir + 200, d)), h)
        else:
            h = np.where(d < ir + 200, np.maximum(h, ih - (ih + 250) * smoothstep(ir * 0.7, ir + 200, d)), h)
    # flat pads for the places
    for px, pz, rad, ph, blend in PADS:
        d = np.hypot(x - px, z - pz)
        w = 1 - smoothstep(rad, rad + blend, d)
        h = h + (ph - h) * w
    # the waterfall gorge: a channel at pool level cut from the pool back into the cliffs
    gd, gw = gorge_dist(x, z)
    gorge = np.where(gd < gw, 40.0, 40 + (h - 40) * smoothstep(gw, gw + 160, gd))
    h = np.minimum(h, gorge)
    # the pool at the falls' foot is sea water in a dish
    d = np.hypot(x - 3800, z + 500)
    h = np.minimum(h, -330 + (h + 330) * smoothstep(200, 900, d))     # a long beach up out of the pool into the gorge
    # raised pools: a dish 40 under each water box's level
    for x0, x1, z0, z1, py in POOLS:
        dd = np.hypot(np.clip(np.maximum(x0 - x, x - x1), 0, None), np.clip(np.maximum(z0 - z, z - z1), 0, None))
        h = np.minimum(h, (py - 40) + (h - py + 40) * smoothstep(0, 160, dd))
    # the sea
    h = np.where(cd > 0, -250 + (SEA_FLOOR + 250) * smoothstep(0, 600, cd), h)
    h = np.where(cd > 600, SEA_FLOOR, h)
    return np.maximum(h, SEA_FLOOR)

# ---- colour, cover -------------------------------------------------------------------------------------------------------------
# OoT palette (docs: /mnt/project-files/art-style/oot-art-style.md), Skyloft's greens a touch brighter
PAL = {
    'meadow': (118, 166, 76), 'meadow_sun': (164, 196, 96), 'woods': (56, 104, 54),
    'dirt': (156, 118, 70), 'paving': (176, 164, 136), 'sand': (222, 190, 120), 'beach': (228, 210, 156),
    'rock': (138, 104, 82), 'rock_dark': (98, 74, 62), 'ash': (74, 60, 58), 'lava_rock': (110, 50, 36), 'water_bed': (54, 110, 112),
    'field': (150, 112, 60), 'field_light': (190, 150, 86), 'snow': (236, 232, 240),
}

def slope_up(x, z, eps=60.0):
    hx = (height(x + eps, z) - height(x - eps, z)) / (2 * eps)
    hz = (height(x, z + eps) - height(x, z - eps)) / (2 * eps)
    return 1 / np.sqrt(1 + hx * hx + hz * hz)

PAVED = {"Bazaar": 0.7, "Statue Plaza": 0.75, "Knight Academy": 0.55, "Sparring Hall": 0.5, "Hollow Graveyard": 0.4, "Light Tower": 0.5,
         "Goddess Statue": 0.4, "Lumpy Pumpkin": 0.5, "Ember Rock": 0.7}

def paint(x, z, h=None, up=None):
    """Ground colour (r, g, b floats 0..255) and cover (0 water, 1 meadow, 2 woods, 3 paving, 4 dirt) at (x, z)."""
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    if h is None: h = height(x, z)
    if up is None: up = slope_up(x, z)
    def col(name): return np.array(PAL[name], dtype=np.float64)
    n1 = fbm(x, z, 500, 3, 11); n2 = fbm(x, z, 140, 2, 13)
    sun = np.clip(0.5 + 0.9 * n1, 0, 1)[..., None]
    c = col('meadow') * (1 - sun) + col('meadow_sun') * sun
    cover = np.ones(np.shape(x), dtype=np.int8)
    # the pumpkin fields: tilled rows
    fx, fz = POI_AT['Pumpkin Fields']
    rows = np.clip(0.5 + 0.5 * np.sin(z * 0.08), 0, 1)[..., None]
    fmix = (1 - smoothstep(520, 640, np.hypot(x - fx, z - fz)))[..., None]
    c = c * (1 - fmix) + (col('field') * (1 - rows) + col('field_light') * rows) * fmix
    cover = np.where(fmix[..., 0] > 0.5, 4, cover)
    # rock on steep ground; ash on Ember Rock; bare stone on the roost
    rock = np.clip((0.86 - up) * 6, 0, 1)[..., None]
    c = c * (1 - rock) + (col('rock') * (1 - np.clip(n2 + .5, 0, 1)[..., None]) + col('rock_dark') * np.clip(n2 + .5, 0, 1)[..., None]) * rock
    cover = np.where(rock[..., 0] > 0.6, 4, cover)
    ash = (1 - smoothstep(380, 560, np.hypot(x - 1700, z - 5600)))[..., None]
    c = c * (1 - ash) + (col('ash') * 0.7 + col('lava_rock') * 0.4 * np.clip(1 + n2, 0, 1)[..., None]) * ash
    cover = np.where(ash[..., 0] > 0.5, 4, cover)
    roost = (1 - smoothstep(300, 620, np.hypot(x - 4400, z + 3600)))[..., None] * np.clip((h - 300) / 200, 0, 1)[..., None]
    c = c * (1 - roost) + (col('rock') * 0.9 + col('snow') * 0.25 * np.clip(n1 + .5, 0, 1)[..., None]) * roost
    cover = np.where(roost[..., 0] > 0.5, 4, cover)
    # roads
    rd = road_dist(x, z)
    road = (1 - smoothstep(70, 125, rd + 25 * n2))[..., None]
    c = c * (1 - road) + col('dirt') * road
    cover = np.where(road[..., 0] > 0.5, 4, cover)
    # paving in the towns and plazas
    for name, px, pz, rad, *_ in POIS:
        if name in PAVED:
            r = rad * PAVED[name]
            pav = (1 - smoothstep(r - 80, r, np.hypot(x - px, z - pz) + 40 * n2))[..., None]
            pc = col('ash') * 0.8 + col('lava_rock') * 0.5 if name == "Ember Rock" else col('paving')
            c = c * (1 - pav) + pc * (0.92 + 0.08 * np.clip(n2 + .5, 0, 1)[..., None]) * pav
            cover = np.where(pav[..., 0] > 0.5, 3, cover)
    # shore and beds
    beach = np.clip((-60 - h) / 120, 0, 1)[..., None]
    c = c * (1 - beach) + col('beach') * beach
    cover = np.where((beach[..., 0] > 0.5) & (cover != 3), 4, cover)
    wet = np.clip((WATER_Y + 10 - h) / 120, 0, 1)[..., None]
    c = c * (1 - wet) + col('water_bed') * (0.75 + 0.25 * np.clip(1 + n2, 0, 1)[..., None]) * wet
    cover = np.where(h < WATER_Y, 0, cover)
    c *= (0.86 + 0.18 * np.clip(up, 0, 1)[..., None]) * (0.97 + 0.06 * n2[..., None])
    return np.clip(c, 0, 255), cover

# ---- the grids the game uses -------------------------------------------------------------------------------------------------------
def vertex_grid():
    """(65 x 65) heights at the collision vertices, row j = z, column i = x (fortnite::VertexX/VertexZ)."""
    i = np.arange(CELLS + 1); xs = -HALF_X + i * (2 * HALF_X / CELLS); zs = -HALF_Z + i * (2 * HALF_Z / CELLS)
    X, Z = np.meshgrid(xs, zs)
    return np.rint(height(X, Z)).astype(np.int32)

def grid_height(heights, x, z):
    """The game's own lookup (fortnite::GroundHeight): two triangles per square, split from the low corner to the far corner."""
    cx = 2 * HALF_X / CELLS; cz = 2 * HALF_Z / CELLS
    fx = np.clip((np.asarray(x) + HALF_X) / cx, 0, CELLS - 1e-6); fz = np.clip((np.asarray(z) + HALF_Z) / cz, 0, CELLS - 1e-6)
    i = np.minimum(CELLS - 1, fx.astype(int)); j = np.minimum(CELLS - 1, fz.astype(int)); u = fx - i; v = fz - j
    H = heights
    h00 = H[j, i]; h10 = H[j, i + 1]; h01 = H[j + 1, i]; h11 = H[j + 1, i + 1]
    return np.where(u >= v, h00 + u * (h10 - h00) + v * (h11 - h10), h00 + u * (h11 - h01) + v * (h01 - h00))
