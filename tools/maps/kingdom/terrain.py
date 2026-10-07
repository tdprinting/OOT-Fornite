"""Hyrule Kingdom ground: height, biome, colour and cover, as numpy functions of game (x, z). No Blender needed, so previews are quick."""
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
# Land that must be there whatever the noise does: every place but the lake's, and the big mountains.
LAND = [(px, pz, rad + 650) for name, px, pz, rad, *_ in POIS if name not in ("Lake Hylia Stilts", "Lakeside Lab")] + \
       [(mx, mz, rad * 0.75) for mx, mz, peak, rad, crater in MOUNTAINS]

def coast_distance(x, z):
    """How far (x, z) is out to sea from the coastline (negative on land)."""
    r = np.hypot(x, z * 0.96); th = np.arctan2(z, x)
    R = 5900 + 260 * np.sin(3 * th + 0.7) + 180 * np.sin(5 * th + 2.1) + 120 * np.sin(9 * th + 0.3) + 70 * np.sin(14 * th + 1.7)
    R += 650 * np.exp(-((th - 0.75) / 0.32) ** 2)    # the desert's south eastern headland
    cd = r - R + 260 * fbm(x, z, 1400, 3, 7)
    for ax, az, ar in LAND:
        cd = np.minimum(cd, np.hypot(x - ax, z - az) - ar + 120 * fbm(x, z, 600, 2, 21))
    return np.maximum(cd, np.maximum(np.abs(x) - (HALF_X - 700), np.abs(z) - (HALF_Z - 700)))

# ---- height ------------------------------------------------------------------------------------------------------------------------
def height(x, z):
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    # Rolling field, higher towards the mountains in the north
    h = 80 + 150 * fbm(x, z, 2400, 4, 1) + 45 * fbm(x, z, 650, 2, 3) + np.clip(-z - 2500, 0, None) * 0.05
    desert = region(x, z, DESERT, 900); forest = region(x, z, FOREST, 700)
    h += desert * (50 + 70 * np.sin(x * 0.0021 + z * 0.0009 + 1.3 * fbm(x, z, 900, 2, 5)) ** 2)
    h += forest * 120 * fbm(x, z, 1100, 3, 9)
    # The coast: the ground slopes down to a beach before the sea
    cd = coast_distance(x, z)
    h = h + (-250 - h) * smoothstep(-700, 0, cd)
    # Mountains
    for mx, mz, peak, rad, crater in MOUNTAINS:
        r = np.hypot(x - mx, z - mz) / rad
        cone = peak * np.clip(1 - r, 0, None) ** 1.5
        cone *= 0.86 + 0.28 * ridged(x, z, 900, 4, int(mx) & 255)
        if crater > 0:
            dip = peak * (1 - crater) ** 1.5 - 460 * (1 - (r / crater) ** 2)
            cone = np.where(r < crater, np.minimum(cone, dip), cone)
        h = np.maximum(h, cone)
    # Flat pads for the places
    for px, pz, rad, ph, blend in PADS:
        d = np.hypot(x - px, z - pz)
        w = 1 - smoothstep(rad, rad + blend, d)
        h = h + (ph - h) * w
    # The castle road: a ramp from the river's bank up to the castle gate on the plateau's east side
    h = ramp(x, z, h, [(1100, -350, 60), (1050, -900, 220), (700, -1250, 520)], 260, 240)
    # Water: Zora's River, Lake Hylia and its outlet, the ponds. Each only ever lowers the ground.
    h = carve_river(x, z, h, RIVER); h = carve_river(x, z, h, OUTLET)
    lx, lz, rx, rz = LAKE
    e = np.hypot((x - lx) / rx, (z - lz) / rz)
    lake = np.where(e < 1.0, -260 - 230 * smoothstep(1.0, 0.55, e), -260 + (h + 260) * smoothstep(1.0, 1.25, e))
    h = np.minimum(h, lake)
    ix, iz, ir = LAB_ISLAND
    d = np.hypot(x - ix, z - iz)
    h = np.where(d < ir + 400, np.maximum(h, 30 - 330 * smoothstep(ir * 0.7, ir + 400, d)), h)
    px, pz, pr = OASIS
    d = np.hypot(x - px, z - pz)
    h = np.minimum(h, -330 + (h + 330) * smoothstep(pr * 0.55, pr + 280, d))
    # The sea
    h = np.where(cd > 0, -250 + (SEA_FLOOR + 250) * smoothstep(0, 1100, cd), h)
    h = np.where(cd > 1100, SEA_FLOOR, h)
    return np.maximum(h, SEA_FLOOR)

def ramp(x, z, h, pts, half, blend):
    d, _ = seg_dist(x, z, [(px, pz, 0) for px, pz, _ in pts])
    # height along the chain: project onto the nearest segment again for the t value
    best = np.full(np.shape(x), 1e9); target = np.zeros(np.shape(x))
    for (ax, az, ay), (bx, bz, by) in zip(pts[:-1], pts[1:]):
        dx, dz = bx - ax, bz - az
        t = np.clip(((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz), 0, 1)
        dd = np.hypot(x - ax - dx * t, z - az - dz * t)
        closer = dd < best
        best = np.where(closer, dd, best); target = np.where(closer, ay + (by - ay) * t, target)
    w = 1 - smoothstep(half, half + blend, d)
    return h + (target - h) * w

def carve_river(x, z, h, pts):
    d, w = seg_dist(x, z, pts)
    bed = np.where(d < w * 0.55, -420.0, -420 + 270 * smoothstep(w * 0.55, w * 1.12, d))
    bank = bed + (h - bed) * smoothstep(w * 1.12, w * 1.12 + 420, d)
    return np.minimum(h, np.where(d < w * 1.12, bed, bank))

def river_distance(x, z):
    d1, w1 = seg_dist(x, z, RIVER); d2, w2 = seg_dist(x, z, OUTLET)
    return np.minimum(d1 - w1, d2 - w2)

# ---- biome, colour, cover -----------------------------------------------------------------------------------------------------------
# OoT palette (docs: /mnt/project-files/art-style/oot-art-style.md)
PAL = {
    'meadow': (112, 158, 72), 'meadow_sun': (156, 186, 92), 'woods': (52, 98, 50), 'woods_dark': (34, 70, 40),
    'dirt': (150, 112, 66), 'paving': (168, 156, 128), 'sand': (222, 184, 104), 'sand_dark': (190, 146, 76),
    'beach': (226, 206, 150), 'rock': (132, 96, 74), 'rock_dark': (96, 70, 60), 'snow': (236, 232, 240), 'snow_shadow': (196, 200, 222),
    'ash': (78, 62, 58), 'lava_rock': (110, 50, 36), 'water_bed': (54, 110, 112), 'blossom': (196, 140, 168), 'mud': (110, 92, 60),
    'castle_grass': (120, 160, 70),
}

def slope_up(x, z, eps=60.0):
    hx = (height(x + eps, z) - height(x - eps, z)) / (2 * eps)
    hz = (height(x, z + eps) - height(x, z - eps)) / (2 * eps)
    return 1 / np.sqrt(1 + hx * hx + hz * hz)

def paint(x, z, h=None, up=None):
    """Ground colour (r, g, b floats 0..255) and cover (0 water, 1 meadow, 2 woods, 3 paving, 4 dirt) at (x, z)."""
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    if h is None: h = height(x, z)
    if up is None: up = slope_up(x, z)
    def col(name): return np.array(PAL[name], dtype=np.float64)
    shape = np.shape(x) + (3,)
    n1 = fbm(x, z, 500, 3, 11); n2 = fbm(x, z, 140, 2, 13)
    sun = np.clip(0.5 + 0.9 * n1, 0, 1)[..., None]
    c = col('meadow') * (1 - sun) + col('meadow_sun') * sun
    cover = np.ones(np.shape(x), dtype=np.int8)
    forest = region(x, z, FOREST, 700); desert = region(x, z, DESERT, 900); blossom = region(x, z, BLOSSOM, 400)
    wmix = np.clip(forest * 1.4 - 0.25 + 0.3 * n1, 0, 1)[..., None]
    c = c * (1 - wmix) + (col('woods') * (1 - np.clip(n2 + .5, 0, 1)[..., None]) + col('woods_dark') * np.clip(n2 + .5, 0, 1)[..., None]) * wmix
    cover = np.where(wmix[..., 0] > 0.55, 2, cover)
    dmix = np.clip(desert * 1.3 - 0.15 + 0.25 * n1, 0, 1)[..., None]
    dune = np.clip(0.5 + 0.8 * np.sin(x * 0.004 + z * 0.002 + 3 * n1), 0, 1)[..., None]
    c = c * (1 - dmix) + (col('sand') * dune + col('sand_dark') * (1 - dune)) * dmix
    cover = np.where(dmix[..., 0] > 0.6, 4, cover)
    bmix = np.clip(blossom * 1.2 - 0.1, 0, 1)[..., None] * np.clip(0.55 + n2, 0, 1)[..., None]
    c = c * (1 - bmix * 0.7) + col('blossom') * bmix * 0.7
    cover = np.where(blossom > 0.6, 2, cover)
    # Rock on steep ground, snow up high in the north, ash on Death Mountain
    rock = np.clip((0.86 - up) * 6, 0, 1)[..., None]
    c = c * (1 - rock) + (col('rock') * (1 - np.clip(n2 + .5, 0, 1)[..., None]) + col('rock_dark') * np.clip(n2 + .5, 0, 1)[..., None]) * rock
    cover = np.where(rock[..., 0] > 0.6, 4, cover)
    dm = np.hypot(x - 4750, z + 4750)
    ash = (np.clip((h - 420) / 300, 0, 1) * (dm < 2600))[..., None]
    c = c * (1 - ash) + (col('ash') * 0.6 + col('lava_rock') * 0.4 * np.clip(1 + n2, 0, 1)[..., None]) * ash
    cover = np.where(ash[..., 0] > 0.5, 4, cover)
    snowline = 430 + 120 * n1 + np.clip(z + 4200, 0, None) * 0.25
    snow = (np.clip((h - snowline) / 160, 0, 1) * (dm > 2300))[..., None]
    snow = np.maximum(snow, (region(x, z, [(-2600, -5200, 2300), (-900, -5900, 1500)], 800) * np.clip((h - 250) / 200, 0, 1))[..., None])
    c = c * (1 - snow) + (col('snow') * (1 - rock * .6) + col('snow_shadow') * rock * .6) * snow
    cover = np.where(snow[..., 0] > 0.5, 4, cover)
    # Roads
    rd = road_dist(x, z)
    road = (1 - smoothstep(70, 125, rd + 25 * n2))[..., None]
    roadc = np.where(desert[..., None] > 0.5, col('sand_dark') * 0.92, col('dirt'))
    roadc = np.where(snow > 0.5, col('snow_shadow'), roadc)
    c = c * (1 - road) + roadc * road
    cover = np.where(road[..., 0] > 0.5, 4, cover)
    # Paving in the towns and plazas
    for name, px, pz, rad, *_ in POIS:
        if name in ("Clock Town", "Kakariko Village", "Hyrule Castle", "Temple Ruins", "Gerudo Ruins", "Ordon Docks"):
            r = rad * (0.62 if name != "Hyrule Castle" else 0.8)
            pav = (1 - smoothstep(r - 80, r, np.hypot(x - px, z - pz) + 40 * n2))[..., None]
            pc = col('sand_dark') * 1.05 if name == "Gerudo Ruins" else col('paving')
            c = c * (1 - pav) + pc * (0.92 + 0.08 * np.clip(n2 + .5, 0, 1)[..., None]) * pav
            cover = np.where(pav[..., 0] > 0.5, 3, cover)
    # Shore and beds
    beach = np.clip((-60 - h) / 120, 0, 1)[..., None] * (1 - snow)
    sandy = np.where(desert[..., None] > 0.4, col('sand'), col('beach'))
    c = c * (1 - beach) + sandy * beach
    cover = np.where((beach[..., 0] > 0.5) & (cover != 3), 4, cover)
    wet = np.clip((WATER_Y + 10 - h) / 120, 0, 1)[..., None]
    c = c * (1 - wet) + col('water_bed') * (0.75 + 0.25 * np.clip(1 + n2, 0, 1)[..., None]) * wet
    cover = np.where(h < WATER_Y, 0, cover)
    # Painted-in light: darker in hollows, a warm rim on rises (OoT bakes its light)
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
