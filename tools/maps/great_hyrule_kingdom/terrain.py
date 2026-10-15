"""Great Hyrule Kingdom ground: height, colour and cover as numpy functions of game (x, z). No Blender needed.

The shape is Tee's landscape (landscape/ground.npy, made by prepare_landscape.py from the cleaned GLB): it is scaled by LAND_SCALE so the
sea goes all round, its heights are mapped to game units (the model's lagoon sits at the sea level), and then the map's own work goes on
top: the coast shelves into the sea, the lagoon and the river are dug out, the places get flat pads, the roads are painted, the pools get
their dishes. The landscape's look (painterly greens, sandy uplands, snow peaks, red rock) is repainted in the OoT palette by region."""
import os
import numpy as np
from layout import *

HERE = os.path.dirname(os.path.abspath(__file__))
_G = np.load(os.path.join(HERE, 'landscape', 'ground.npy')).astype(np.float64)
_TREES = np.load(os.path.join(HERE, 'landscape', 'trees.npy')).astype(np.float64) / 255.0
_BIOME = np.load(os.path.join(HERE, 'landscape', 'biome.npy'))
LN = _G.shape[0]

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

def smoothstep(a, b, t):
    s = np.clip((t - a) / (b - a), 0, 1)
    return s * s * (3 - 2 * s)

def seg_dist(x, z, pts):
    """Distance to a polyline of (x, z, w...) points, and the index of the nearest segment plus the fraction along it."""
    best = np.full(np.shape(x), 1e9); seg = np.zeros(np.shape(x), int); frac = np.zeros(np.shape(x))
    for k, (a, b) in enumerate(zip(pts[:-1], pts[1:])):
        ax, az, bx, bz = a[0], a[1], b[0], b[1]
        dx, dz = bx - ax, bz - az
        t = np.clip(((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz), 0, 1)
        d = np.hypot(x - ax - dx * t, z - az - dz * t)
        closer = d < best
        best = np.where(closer, d, best); seg = np.where(closer, k, seg); frac = np.where(closer, t, frac)
    return best, seg, frac

def road_dist(x, z):
    best = np.full(np.shape(x), 1e9)
    for road in ROADS:
        d, _, _ = seg_dist(x, z, road)
        best = np.minimum(best, d)
    return best

def region(x, z, circles, soft=600):
    w = np.zeros(np.shape(x))
    for cx, cz, r in circles:
        w = np.maximum(w, 1 - smoothstep(r - soft, r, np.hypot(x - cx, z - cz)))
    return w

# ---- the landscape, sampled ---------------------------------------------------------------------------------------------------------
def _sample(grid, x, z):
    """Bilinear lookup of a landscape grid at game (x, z) (the landscape is scaled by LAND_SCALE about the centre)."""
    u = (np.asarray(x) / LAND_SCALE + LAND_HALF_X) / (2 * LAND_HALF_X) * LN - 0.5
    v = (np.asarray(z) / LAND_SCALE + LAND_HALF_Z) / (2 * LAND_HALF_Z) * LN - 0.5
    inside = (u >= -0.5) & (u <= LN - 0.5) & (v >= -0.5) & (v <= LN - 0.5)
    u = np.clip(u, 0, LN - 1.001); v = np.clip(v, 0, LN - 1.001)
    i = u.astype(int); j = v.astype(int); fu = u - i; fv = v - j
    g = grid
    val = (g[j, i] * (1 - fu) * (1 - fv) + g[j, i + 1] * fu * (1 - fv) + g[j + 1, i] * (1 - fu) * fv + g[j + 1, i + 1] * fu * fv)
    return val, inside

def model_height(x, z):
    """The landscape's own ground in game units (before the map's edits); -1e4 where it has no ground."""
    g, inside = _sample(_G, x, z)
    return np.where(inside & (g > -500), WATER_Y + (g - MODEL_SEA) * VERTICAL, -1e4)

def tree_density(x, z):
    t, inside = _sample(_TREES, x, z)
    return np.where(inside, t, 0.0)

# The base raster: the landscape's ground with its coast and lagoon worked in, at RES x RES over the game grid (built once, then looked up).
RES = 513
def _build_base():
    xs = np.linspace(-HALF_X, HALF_X, RES); zs = np.linspace(-HALF_Z, HALF_Z, RES)
    X, Z = np.meshgrid(xs, zs)
    y = model_height(X, Z)
    bio, inside = _sample(_BIOME.astype(np.float64), X, Z)
    # land: the model has ground there, and it is not the lagoon (the model's low blue water) or one of LAGOON_EXTRA's circles
    lagoon = (bio > 0.5) & (bio < 1.5) & (y < WATER_Y + 130)
    for cx, cz, r in LAGOON_EXTRA: lagoon |= (np.hypot(X - cx, Z - cz) < r) & (y < WATER_Y + 160)
    for cx, cz, r in LAND_KEEP: lagoon &= ~(np.hypot(X - cx, Z - cz) < r)
    land = (y > -5000) & ~lagoon
    land &= (np.abs(X) < HALF_X - 500) & (np.abs(Z) < HALF_Z - 500)
    cell = 2 * HALF_X / (RES - 1)
    # distance (in units) into the land from the nearest water, and out to sea from the nearest land: chamfer propagation
    def dist(mask):
        d = np.where(mask, 0.0, 1e9)
        for _ in range(90):
            p = np.pad(d, 1, constant_values=1e9)
            n = np.minimum.reduce([p[1:-1, :-2] + cell, p[1:-1, 2:] + cell, p[:-2, 1:-1] + cell, p[2:, 1:-1] + cell,
                                   p[:-2, :-2] + cell * 1.414, p[2:, 2:] + cell * 1.414, p[:-2, 2:] + cell * 1.414, p[2:, :-2] + cell * 1.414])
            nd = np.minimum(d, n)
            if np.array_equal(nd, d): break
            d = nd
        return d
    d_in = dist(~land)       # on land: how far from the water
    d_out = dist(land)       # at sea: how far from the land
    y = np.where(land, y, WATER_Y)
    # the sea: shelving from the shore to the flat open sea floor (the land side's shelf is applied in height(), after the mountains)
    y = np.where(land, y, np.maximum(SEA_FLOOR, WATER_Y - 40 - d_out * 0.45))
    return y, np.where(land, d_in, -d_out)
_BASE, _SHORE = _build_base()

def _raster(B, x, z):
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    u = np.clip((x + HALF_X) / (2 * HALF_X) * (RES - 1), 0, RES - 1.001); v = np.clip((z + HALF_Z) / (2 * HALF_Z) * (RES - 1), 0, RES - 1.001)
    i = u.astype(int); j = v.astype(int); fu = u - i; fv = v - j
    return B[j, i] * (1 - fu) * (1 - fv) + B[j, i + 1] * fu * (1 - fv) + B[j + 1, i] * (1 - fu) * fv + B[j + 1, i + 1] * fu * fv

def coast_distance(x, z):
    """How far (x, z) is out to sea from the coastline (negative on land, the distance inland)."""
    return -_raster(_SHORE, x, z)

def ridged(x, z, scale, octaves, salt):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        n = 1 - np.abs(value_noise(x, z, scale / (2 ** o), salt + o * 17) * 2 - 1)
        total = total + amp * n * n; norm += amp; amp *= 0.5
    return total / norm

def base_height(x, z):
    """The landscape with the added mountains and the shore shelf: the ground before the places, river and pools are cut in."""
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    h = _raster(_BASE, x, z)
    land = -coast_distance(x, z)
    h = np.where(land > 0, np.minimum(h, WATER_Y - 40 + land * COAST_SLOPE), h)
    # The mountains go on after the shore shelf: a peak by the coast rises straight out of the sea (Death Mountain's east foot is a sea cliff)
    for cx, cz, add, r, crater in MOUNTAINS:
        d = np.hypot(x - cx, z - cz) / r
        k = np.clip(1 - d, 0, None)
        bump = add * k ** 1.5 * (0.8 + 0.4 * ridged(x, z, 900, 3, int(abs(cx)) & 255))
        if crater:
            bump = bump - add * 0.55 * (1 - smoothstep(crater * 0.6, crater, d))
        h = h + bump
    return h

_PAD_Y = {}
def pad_height(k):
    """A pad's height: its own, or the mean of the ground under its flat top (worked out once)."""
    px, pz, rad, ph, blend = PADS[k]
    if ph is not None: return ph
    if k not in _PAD_Y:
        a = np.linspace(0, 2 * np.pi, 24, endpoint=False)
        pts = [(px, pz)] + [(px + r * np.cos(t), pz + r * np.sin(t)) for r in (rad * 0.4, rad * 0.8) for t in a]
        X = np.array([p[0] for p in pts]); Z = np.array([p[1] for p in pts])
        _PAD_Y[k] = float(np.rint(np.mean(base_height(X, Z))))
    return _PAD_Y[k]

ALL_POOLS = [p for p in POOLS if p[4] is not None] + RIVER_POOLS

# ---- height ------------------------------------------------------------------------------------------------------------------------
def river_level(seg, frac):
    """The water level along the river at a segment and the fraction along it (steps: each pool is flat, see RIVER)."""
    lv = np.array([p[3] for p in RIVER], float)
    return lv[np.minimum(seg, len(RIVER) - 2)]

def _cut_end(x, z):
    """The ground at a pass's end (before the pass is cut): the landscape and the places' pads."""
    x = np.asarray(float(x)); z = np.asarray(float(z))
    h = base_height(x, z)
    for k_, (px, pz, rad, ph, blend) in enumerate(PADS):
        k = 1 - smoothstep(rad, rad + blend, np.hypot(x - px, z - pz))
        h = h + (pad_height(k_) - h) * k
    return h

def height(x, z):
    x = np.asarray(x, dtype=np.float64); z = np.asarray(z, dtype=np.float64)
    h = base_height(x, z) + 8 * fbm(x, z, 500, 2, 3)
    # flat pads for the places
    for k_, (px, pz, rad, ph, blend) in enumerate(PADS):
        ph = pad_height(k_)
        dd = np.hypot(x - px, z - pz)
        k = 1 - smoothstep(rad, rad + blend, dd)
        h = h + (ph - h) * k
    # mountain passes (CUTS): a gently sloping floor between the ground heights at the two ends, so a road climbs through a rim
    for (ax, az), (bx, bz), hw_ in CUTS:
        ya = float(_cut_end(ax, az)); yb = float(_cut_end(bx, bz))
        dd, _, t = seg_dist(x, z, [(ax, az), (bx, bz)])
        floor = ya + (yb - ya) * t
        k = 1 - smoothstep(hw_, hw_ + 300, dd)
        h = np.where(h > floor, h + (floor - h) * k, h)
    # the river: a channel with flat pools stepping down to the lagoon (each segment's water is its own level, RIVER). Inside the channel
    # (half width w) the bed is 70 under the water; from w the banks rise to 40 over it and blend into the ground by w + 350. Inside a
    # segment's water box but off the channel, nothing is left under the water (a levee), so the box's water only shows in the channel.
    d, seg, frac = seg_dist(x, z, RIVER)
    hw = np.array([p[2] for p in RIVER], float)
    w = hw[np.minimum(seg, len(RIVER) - 1)] * (1 - frac) + hw[np.minimum(seg + 1, len(RIVER) - 1)] * frac
    lvl = river_level(seg, frac)
    chan = np.where(d < w, lvl - 70 + 110 * smoothstep(0.55 * w, w, d), lvl + 40)
    prof = np.where(d < w, chan, lvl + 40 + (h - lvl - 40) * smoothstep(w, w + 350, d))
    h = np.where(d < w + 350, np.where(d < w, chan, np.minimum(h, prof)), h)
    for x0, x1, z0, z1, py in RIVER_POOLS:
        inbox = (x >= x0) & (x <= x1) & (z >= z0) & (z <= z1) & (d >= w)
        h = np.where(inbox, np.maximum(h, py + 30), h)
    # raised pools: a dish under each water box (60 deep in the middle, rising to 60 over the water at the box's edge)
    for x0, x1, z0, z1, py in [p for p in POOLS if p[4] is not None]:
        e = np.minimum(np.minimum(x - x0, x1 - x), np.minimum(z - z0, z1 - z))
        h = np.where(e >= 0, np.minimum(h, py - 60 + 120 * np.clip((140 - e) / 140, 0, 1)), h)
    return np.maximum(h, SEA_FLOOR)

# ---- colour, cover -------------------------------------------------------------------------------------------------------------
# OoT palette (docs: /mnt/project-files/art-style/oot-art-style.md)
PAL = {
    'meadow': (118, 166, 76), 'meadow_sun': (164, 196, 96), 'woods': (56, 104, 54),
    'dirt': (156, 118, 70), 'paving': (176, 164, 136), 'sand': (222, 190, 120), 'sand_dark': (196, 150, 92), 'beach': (228, 210, 156),
    'rock': (138, 104, 82), 'rock_dark': (98, 74, 62), 'grey_rock': (120, 116, 110), 'ash': (74, 60, 58), 'lava_rock': (150, 66, 40),
    'red_rock': (168, 92, 62), 'water_bed': (54, 110, 112), 'field': (150, 112, 60), 'field_light': (190, 150, 86),
    'snow': (236, 232, 240), 'snow_shade': (196, 206, 228),
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
    def mix(c, k, other):
        k = np.clip(k, 0, 1)[..., None]
        return c * (1 - k) + other * k
    n1 = fbm(x, z, 500, 3, 11); n2 = fbm(x, z, 140, 2, 13)
    sun = np.clip(0.5 + 0.9 * n1, 0, 1)
    c = mix(np.broadcast_to(col('meadow'), np.shape(x) + (3,)).copy(), sun, col('meadow_sun'))
    cover = np.ones(np.shape(x), dtype=np.int8)
    # woods: under the landscape's own canopies and in the forest regions
    wood = np.maximum(region(x, z, FOREST, 500) * 0.8, smoothstep(0.15, 0.6, tree_density(x, z)) * 0.7)
    c = mix(c, wood, col('woods'))
    cover = np.where(wood > 0.5, 2, cover)
    # farm fields: tilled strips
    farm = region(x, z, FARMS, 250)
    rows = np.clip(0.5 + 0.5 * np.sin((x * 0.6 + z) * 0.05), 0, 1)
    c = mix(c, farm * 0.85, col('field') * (1 - rows[..., None]) + col('field_light') * rows[..., None])
    cover = np.where(farm > 0.5, 4, cover)
    # desert: sand, darker in the hollows
    des = region(x, z, DESERT, 700)
    c = mix(c, des, col('sand') * (1 - np.clip(n2 + .5, 0, 1)[..., None] * 0.35) + col('sand_dark') * np.clip(n2 + .5, 0, 1)[..., None] * 0.35)
    cover = np.where(des > 0.5, 4, cover)
    # red rock: Death Mountain's slopes
    red = region(x, z, REDROCK, 700)
    c = mix(c, red, col('red_rock') * (0.85 + 0.25 * np.clip(n2 + .5, 0, 1)[..., None]))
    cover = np.where(red > 0.5, 4, cover)
    # rock on steep ground
    rock = np.clip((0.86 - up) * 6, 0, 1)
    rk = col('rock') * (1 - np.clip(n2 + .5, 0, 1)[..., None]) + col('rock_dark') * np.clip(n2 + .5, 0, 1)[..., None]
    rk = np.where(red[..., None] > 0.5, col('red_rock') * 0.8 + col('ash') * 0.2, rk)
    c = mix(c, rock, rk)
    cover = np.where(rock > 0.6, 4, cover)
    # snow: the snow region over SNOW_LINE, and the high peaks anywhere
    snow = region(x, z, SNOW, 800) * smoothstep(SNOW_LINE - 120, SNOW_LINE + 60, h + 40 * n1)
    snow = np.maximum(snow, smoothstep(PEAK_SNOW - 80, PEAK_SNOW + 80, h + 30 * n1) * (1 - red))
    c = mix(c, snow, col('snow') * (1 - np.clip(n2 + .5, 0, 1)[..., None] * 0.3) + col('snow_shade') * np.clip(n2 + .5, 0, 1)[..., None] * 0.3)
    cover = np.where(snow > 0.5, 4, cover)
    # the crater floor: ash and glowing rock
    ash = region(x, z, CRATER, 300)
    c = mix(c, ash, col('ash') * 0.8 + col('lava_rock') * 0.4 * np.clip(1 + n2, 0, 1)[..., None])
    cover = np.where(ash > 0.5, 4, cover)
    # roads
    rd = road_dist(x, z)
    road = (1 - smoothstep(70, 125, rd + 25 * n2))
    c = mix(c, road * (1 - snow * 0.6), np.where(des[..., None] > 0.5, col('sand_dark') * 0.9, col('dirt')))
    cover = np.where(road > 0.5, 4, cover)
    # paving in the towns and plazas
    for name, px, pz, rad, *_ in POIS:
        if name in PAVED:
            r = rad * PAVED[name]
            pav = (1 - smoothstep(r - 80, r, np.hypot(x - px, z - pz) + 40 * n2))
            c = mix(c, pav, col('paving') * (0.92 + 0.08 * np.clip(n2 + .5, 0, 1)[..., None]))
            cover = np.where(pav > 0.5, 3, cover)
    # shore and beds
    beach = np.clip((WATER_Y + 60 - h) / 70, 0, 1) * (1 - snow)
    c = mix(c, beach, col('beach'))
    cover = np.where((beach > 0.5) & (cover != 3), 4, cover)
    wet = np.clip((WATER_Y + 10 - h) / 120, 0, 1)
    c = mix(c, wet, col('water_bed') * (0.75 + 0.25 * np.clip(1 + n2, 0, 1)[..., None]))
    cover = np.where(h < WATER_Y, 0, cover)
    for x0, x1, z0, z1, py in ALL_POOLS:
        inside = (x >= x0) & (x <= x1) & (z >= z0) & (z <= z1) & (h < py)
        c = np.where(inside[..., None], col('water_bed') * 0.9, c)
        cover = np.where(inside, 0, cover)
    d, seg, frac = seg_dist(x, z, RIVER)
    under = h < river_level(seg, frac) - 5
    c = np.where((under & (d < 700))[..., None], col('water_bed') * 0.9, c)
    cover = np.where(under & (d < 700), 0, cover)
    c *= (0.86 + 0.18 * np.clip(up, 0, 1)[..., None]) * (0.97 + 0.06 * n2[..., None])
    return np.clip(c, 0, 255), cover

# ---- the grids the game uses -------------------------------------------------------------------------------------------------------
_VG = [None]
def vertex_grid():
    """(65 x 65) heights at the collision vertices, row j = z, column i = x (fortnite::VertexX/VertexZ), simplified (see simplify)."""
    if _VG[0] is None:
        i = np.arange(CELLS + 1); xs = -HALF_X + i * (2 * HALF_X / CELLS); zs = -HALF_Z + i * (2 * HALF_Z / CELLS)
        X, Z = np.meshgrid(xs, zs)
        _VG[0] = simplify(np.rint(height(X, Z)).astype(np.int32), X, Z)
    return _VG[0].copy()

def plane_block(h, i, j, s):
    """The heights of an s x s block of cells as two flat triangles over its corners, split from (i, j) to (i + s, j + s) like the game's
    own lookup (the corners must differ by multiples of s so every height comes out whole)."""
    A, B, C, D = int(h[j, i]), int(h[j, i + s]), int(h[j + s, i]), int(h[j + s, i + s])
    u = np.arange(s + 1)[None, :]; v = np.arange(s + 1)[:, None]
    low = A + (B - A) * u // s + (D - B) * v // s          # u >= v: triangle (i, j), (i + s, j), (i + s, j + s)
    up = A + (D - C) * u // s + (C - A) * v // s           # u < v:  triangle (i, j), (i + s, j + s), (i, j + s)
    return np.where(u >= v, low, up)

def simplify(h, X, Z, tol4=16, tol2=12):
    """Fewer collision vertices: where a 4 x 4 (or 2 x 2) block of ground cells is nearly two flat triangles, its heights are set exactly
    onto them, so the exporter can draw and collide it as two big triangles with no cracks (an edge's in-between vertices lie on the edge).
    The game's collision keeps vertex numbers in 13 bits for two corners of a triangle (patches/0025), which limits the whole map.
    Shores, the river and the pools keep their full detail."""
    h = h.copy()
    keep = np.zeros(h.shape, bool)
    keep |= np.abs(h - WATER_Y) < 60
    for x0, x1, z0, z1, py in ALL_POOLS:
        keep |= (X > x0 - 300) & (X < x1 + 300) & (Z > z0 - 300) & (Z < z1 + 300)
    d, _, _ = seg_dist(X, Z, RIVER)
    keep |= d < 700
    deep = h <= SEA_FLOOR + 1
    # whole numbers on every in-between point: corners of 4-blocks on multiples of 4, of 2-blocks on even numbers (a change of 2 at most)
    n = h.shape[0]
    for j in range(0, n, 2):
        for i in range(0, n, 2):
            if deep[j, i]: continue
            m = 4 if (i % 4 == 0 and j % 4 == 0) else 2
            h[j, i] = int(round(h[j, i] / m)) * m
    merged = np.zeros((n - 1, n - 1), bool)
    for s, tol in ((4, tol4), (2, tol2)):
        for j in range(0, n - 1, s):
            for i in range(0, n - 1, s):
                if merged[j:j + s, i:i + s].any() or keep[j:j + s + 1, i:i + s + 1].any(): continue
                if deep[j:j + s + 1, i:i + s + 1].all(): continue
                if s == 4 and any(h[jj, ii] % 4 for jj in (j, j + 4) for ii in (i, i + 4)): continue
                P = plane_block(h, i, j, s)
                if np.abs(h[j:j + s + 1, i:i + s + 1] - P).max() <= tol:
                    h[j:j + s + 1, i:i + s + 1] = P
                    merged[j:j + s, i:i + s] = True
    return h

def grid_height(heights, x, z):
    """The game's own lookup (fortnite::GroundHeight): two triangles per square, split from the low corner to the far corner."""
    cx = 2 * HALF_X / CELLS; cz = 2 * HALF_Z / CELLS
    fx = np.clip((np.asarray(x) + HALF_X) / cx, 0, CELLS - 1e-6); fz = np.clip((np.asarray(z) + HALF_Z) / cz, 0, CELLS - 1e-6)
    i = np.minimum(CELLS - 1, fx.astype(int)); j = np.minimum(CELLS - 1, fz.astype(int)); u = fx - i; v = fz - j
    H = heights
    h00 = H[j, i]; h10 = H[j, i + 1]; h01 = H[j + 1, i]; h11 = H[j + 1, i + 1]
    return np.where(u >= v, h00 + u * (h10 - h00) + v * (h11 - h10), h00 + u * (h11 - h01) + v * (h01 - h00))
