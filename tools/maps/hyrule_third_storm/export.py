"""Writes the game's data for Hyrule Before the Third Storm (map 15) from the Blender extraction (blender_extract.py):
  shared/hyrule_third_storm_data.h   collision, walk grid, loot sites, regions, pools, stations, colliders, climb walls, upper floors
  shared/hyrule_third_storm_model.h  the drawn map: near and far meshes per 4096-unit square, 256 x 256 textures
  build/hyrule_third_storm/          the debug report (report.json) and pictures: walk.png (where you can stand), problems.png, budget.png
Run:  python tools/maps/hyrule_third_storm/export.py --src <extraction dir> [--textures <map project>/textures]   (a few minutes; numpy, Pillow)

Collision is the drawn ground and solid meshes themselves (what you stand on is what you see); props get boxes. The walk grid is worked out
from the collision: at every point of a 64-unit grid, the lowest floor with nothing solid on it and head room above it. That one pass also
finds the map's problems (holes you would fall through, floors inside solid geometry, slopes too steep to stand on) for the debug report."""
import os, sys, re, json, math, argparse, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_MAP = "C:/Users/mexic/Documents/Claude/zelda battle royale ultimate map/map"
NONE = -32768
KIND_GROUND, KIND_SOLID, KIND_PROP = 0, 1, 2
BOSS_ORDER = ["Stone", "Lava", "Frost", "Moss", "Tide", "Shade", "Dune"]   # shared/boss.h BossKind

def log(*a): print(time.strftime("%H:%M:%S"), *a, flush=True)

# ---- input ------------------------------------------------------------------------------------------------------------------------------
def load(src):
    parts, far, meta = [], [], {}
    for n in ["%02d" % i for i in range(13)]:
        f = os.path.join(src, f"poi-{n}.npz")
        if not os.path.exists(f): log("missing", f); continue
        d = np.load(f); j = json.load(open(os.path.join(src, f"poi-{n}.json")))
        meta[n] = j
        mats = np.array(j["mats"], dtype=object)
        parts.append((n, d["pos"].astype(np.float64), d["col"], d["kind"], mats[d["mat"]], d["obj"]))
        ff = os.path.join(src, f"poi-{n}-far.npz")
        if os.path.exists(ff):
            e = np.load(ff); far.append((n, e["pos"].astype(np.float64), e["col"], e["kind"], mats[np.minimum(e["mat"], len(mats) - 1)] if len(mats) else None))
    return parts, far, meta

def game_pos(P):
    """(..., 3) compact metres -> game units."""
    out = np.empty_like(P)
    out[..., 0] = (P[..., 0] - CENTRE) * UNITS
    out[..., 1] = P[..., 2] * UNITS
    out[..., 2] = -(P[..., 1] - CENTRE) * UNITS
    return out

def gxz(x, y): return ((x - CENTRE) * UNITS, -(y - CENTRE) * UNITS)

# ---- materials: texture and surface --------------------------------------------------------------------------------------------------------
TEXTURES = ["grass", "dirt", "cobble", "setts", "ashlar", "flagstone", "rubble", "rock", "planks", "planks_grey", "sandstone", "sand", "cloth",
            "leaves", "plain", "snow"]
TEX_FILES = {"cobble": "01/cobble_albedo.png", "setts": "01/setts_albedo.png", "ashlar": "01/ashlar_ivory_albedo.png",
             "flagstone": "flagstone_albedo.png", "rubble": "02/rubble_albedo.png", "rock": "Weathered-Rock.png", "planks": "Timber-Planks.png",
             "planks_grey": "planks_gray.png", "sandstone": "sandstone_blocks_albedo.png", "sand": "Wind-Sand.png", "cloth": "Woven-Cloth.png",
             "leaves": "cards/leaves.png"}
RULES = [  # (regex on the material name, texture, footstep sfx, hook-shot)
    (r"SNOW|ICE|FROST", "snow", 0, False),
    (r"SANDSTONE|ADOBE|MUD.?BRICK|CLAY|TERRACOTTA|GERUDO", "sandstone", 1, False),
    (r"SAND|DUNE|DESERT", "sand", 1, False),
    (r"BARK|TRUNK|ROOT", "rock", 0, True),
    (r"LEAF|LEAVES|FOLIAGE|CANOPY|HEDGE|BUSH|IVY|VINE|MOSS|NEEDLE|FERN|REED", "leaves", 0, False),
    (r"GRASS|TURF|LAWN|MEADOW|PASTURE", "grass", 0, False),
    (r"TIMBER|WOOD|PLANK|LOG|BEAM|BOARD|DOOR|SHUTTER|PALISADE|FENCE|POST|SHINGLE|CRATE|BARREL", "planks", 10, True),
    (r"CLOTH|CANVAS|ROPE|STRAW|THATCH|TARP|FLAG|BANNER|RUG|SACK|HAY|WOOL|FELT", "cloth", 0, False),
    (r"COBBLE", "cobble", 2, False),
    (r"SETT|PAVING|PAVE|PLAZA|STEP|STAIR", "setts", 2, False),
    (r"ASHLAR|MASONRY|LIME|MARBLE|PLASTER|STUCCO|WHITE|PAINT|TEMPLE|CASTLE|TILE", "ashlar", 2, False),
    (r"FLAG.?STONE|SLAB", "flagstone", 2, False),
    (r"ROCK|CLIFF|CRAG|BOULDER|LAVA|BASALT|ASH|OBSIDIAN|SCREE|GRAVEL", "rock", 2, False),
    (r"STONE|BRICK|RUBBLE|WALL", "rubble", 2, False),
    (r"PATH|ROAD|TRACK|DIRT|SOIL|EARTH|MUD", "dirt", 0, False),
    (r"METAL|IRON|BRONZE|GOLD|COPPER|STEEL", "plain", 2, False),
]
DESERT = {"10", "11", "12"}

def material_rule(name):
    u = str(name).upper()
    for rx, tex, sfx, hook in RULES:
        if re.search(rx, u): return tex, sfx, hook
    return None

def classify_tris(area, cols, kinds, mats, normals_y):
    """Per triangle: texture index, footstep sfx, hook-shot flag."""
    T = len(kinds)
    tex = np.full(T, TEXTURES.index("plain"), np.int32); sfx = np.zeros(T, np.int32); hook = np.zeros(T, bool)
    cache = {}
    for i, m in enumerate(mats):
        r = cache.get(m)
        if r is None: r = cache[m] = material_rule(m) or ("", 0, False)
        if r[0]:
            tex[i] = TEXTURES.index(r[0]); sfx[i] = r[1]; hook[i] = r[2]
    # bare terrain: grass where it is green, dirt or sand where it is not; steep ground is rock
    ground = (kinds == KIND_GROUND) & np.isin(tex, [TEXTURES.index("plain"), TEXTURES.index("grass"), TEXTURES.index("dirt")])
    c = cols.astype(np.int32)
    green = (c[:, 1] > c[:, 0] + 4) & (c[:, 1] > c[:, 2] + 8)
    soil = TEXTURES.index("sand") if area in DESERT else TEXTURES.index("dirt")
    tex[ground] = np.where(green[ground], TEXTURES.index("grass"), soil)
    sfx[ground & ~green] = 1 if area in DESERT else 0
    steep = ground & (normals_y < 0.55)
    tex[steep] = TEXTURES.index("rock"); sfx[steep] = 2
    return tex, sfx, hook

# ---- collision ----------------------------------------------------------------------------------------------------------------------------
def face_normals(P):
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    l = np.linalg.norm(n, axis=1)
    return n / np.maximum(l, 1e-9)[:, None], l * 0.5

def build_collision(P, kinds, surf_of_tri):
    """Weld, drop slivers, and pack: returns verts (V,3) int, tris (T,3) int, normals (T,3) float, dist (T,) int, surface (T,) int, keep mask."""
    Q = np.clip(np.rint(P), -32767, 32767).astype(np.int32)
    flat = Q.reshape(-1, 3)
    uniq, inv = np.unique(flat, axis=0, return_inverse=True)
    tri = inv.reshape(-1, 3)
    Pq = uniq[tri].astype(np.float64)
    n, area = face_normals(Pq)
    ok = (tri[:, 0] != tri[:, 1]) & (tri[:, 1] != tri[:, 2]) & (tri[:, 0] != tri[:, 2]) & (area > 2.0)
    # the game takes the normal from the winding; a ground triangle that faces down (an inverted patch) is turned over
    flip = ok & (kinds == KIND_GROUND) & (n[:, 1] < -0.1)
    tri[flip] = tri[flip][:, [0, 2, 1]]; n[flip] = -n[flip]
    # one copy of a triangle drawn twice (two objects on the same spot)
    key = np.sort(tri, axis=1)
    _, first = np.unique(key[ok], axis=0, return_index=True)
    keep = np.zeros(len(tri), bool); keep[np.flatnonzero(ok)[first]] = True
    tri, n, surf = tri[keep], n[keep], surf_of_tri[keep]
    used = np.unique(tri)
    remap = np.full(len(uniq), -1, np.int64); remap[used] = np.arange(len(used))
    tri = remap[tri]; verts = uniq[used]
    A = verts[tri[:, 0]].astype(np.float64)
    dist = np.rint(-(n * A).sum(axis=1)).astype(np.int64)
    return verts, tri, n, dist, surf, keep

# ---- the walk grid ------------------------------------------------------------------------------------------------------------------------
def raster(P, ny_all, step=STEP, grid=GRID):
    """Every grid point (i, j) each triangle covers, seen from above: returns point index, height y, the triangle's up part, triangle index."""
    org = -step * (grid - 1) / 2
    x = P[:, :, 0]; z = P[:, :, 2]
    i0 = np.ceil((x.min(1) - org) / step).astype(np.int64); i1 = np.floor((x.max(1) - org) / step).astype(np.int64)
    j0 = np.ceil((z.min(1) - org) / step).astype(np.int64); j1 = np.floor((z.max(1) - org) / step).astype(np.int64)
    i0 = np.clip(i0, 0, grid - 1); j0 = np.clip(j0, 0, grid - 1); i1 = np.clip(i1, -1, grid - 1); j1 = np.clip(j1, -1, grid - 1)
    ni = np.maximum(0, i1 - i0 + 1); nj = np.maximum(0, j1 - j0 + 1)
    cnt = ni * nj
    vert = np.abs(ny_all) > 0.02
    cnt[~vert] = 0
    out_p, out_y, out_n, out_t = [], [], [], []
    tot = int(cnt.sum())
    chunk = 4_000_000
    idx_all = np.flatnonzero(cnt)
    s = 0
    while s < len(idx_all):
        # take triangles until about `chunk` candidate points
        c = np.cumsum(cnt[idx_all[s:]])
        e = s + max(1, int(np.searchsorted(c, chunk)))
        ids = idx_all[s:e]; s = e
        k = cnt[ids]
        t = np.repeat(ids, k)
        off = np.arange(len(t)) - np.repeat(np.cumsum(k) - k, k)
        ii = i0[t] + off % ni[t]; jj = j0[t] + off // ni[t]
        px = org + ii * step; pz = org + jj * step
        a = P[t, 0]; b = P[t, 1]; cc = P[t, 2]
        v0x = b[:, 0] - a[:, 0]; v0z = b[:, 2] - a[:, 2]; v1x = cc[:, 0] - a[:, 0]; v1z = cc[:, 2] - a[:, 2]
        wx = px - a[:, 0]; wz = pz - a[:, 2]
        den = v0x * v1z - v1x * v0z
        den = np.where(np.abs(den) < 1e-9, 1e-9, den)
        u = (wx * v1z - v1x * wz) / den; v = (v0x * wz - wx * v0z) / den
        inside = (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6)
        y = a[:, 1] + u * (b[:, 1] - a[:, 1]) + v * (cc[:, 1] - a[:, 1])
        out_p.append((jj * grid + ii)[inside]); out_y.append(y[inside]); out_n.append(ny_all[t][inside]); out_t.append(t[inside])
    return np.concatenate(out_p), np.concatenate(out_y), np.concatenate(out_n), np.concatenate(out_t)

def walk_grid(P, n, kinds):
    """The floor of every grid point and what is wrong there. Returns walk (GRID*GRID int16, NONE: no floor), flags, upper floors list."""
    log("walk grid: rasterising", len(P), "triangles")
    pt, y, ny, t = raster(P, n[:, 1])
    order = np.lexsort((y, pt)); pt, y, ny, t = pt[order], y[order], ny[order], t[order]
    N = GRID * GRID
    same_next = np.r_[pt[1:] == pt[:-1], False]
    y_next = np.r_[y[1:], np.inf]; ny_next = np.r_[ny[1:], 0.0]
    # skip a surface straight on top of this one (two meshes meeting, a rug on a floor): look one further
    close = same_next & (y_next - y < 6.0)
    y_next2 = np.r_[y[2:], np.inf, np.inf]; ny_next2 = np.r_[ny[2:], 0.0, 0.0]; same2 = np.r_[pt[2:] == pt[:-2], False, False]
    yn = np.where(close, np.where(same2, y_next2, np.inf), np.where(same_next, y_next, np.inf))
    nyn = np.where(close, np.where(same2, ny_next2, 0.0), np.where(same_next, ny_next, 0.0))
    kk = kinds[t]
    kn = np.where(close, np.where(same2, np.r_[kk[2:], 0, 0], 0), np.where(same_next, np.r_[kk[1:], 0], 0))
    floor = ny > 0.5
    # the next surface up faces up: inside a solid when it is close (less than head room) or when it is the terrain's own rock (an overhang,
    # a cliff, a mountain). A building's sheet above you is a storey floor or a roof modelled as one sheet (most of the kit's buildings): the
    # room under it is fine.
    gap = yn - y
    inside = floor & np.isfinite(yn) & (nyn > 0.05) & ((gap < 62.0) | (kn == KIND_GROUND))
    low_roof = floor & np.isfinite(yn) & (nyn < -0.05) & (yn - y < 62.0)   # a ceiling too low to stand under
    good = floor & ~inside & ~low_roof
    # walls: a vertical face crossing a grid point between knee and head height above a floor shuts that floor (thin walls have no top view)
    blocked = np.zeros(len(pt), bool)
    vert = np.abs(n[:, 1]) < 0.3
    if vert.any():
        V = P[vert]
        lo = V[:, :, 1].min(1); hi = V[:, :, 1].max(1)
        xz = V[:, :, [0, 2]]
        d = np.c_[np.linalg.norm(xz[:, 1] - xz[:, 0], axis=1), np.linalg.norm(xz[:, 2] - xz[:, 1], axis=1), np.linalg.norm(xz[:, 0] - xz[:, 2], axis=1)]
        e = np.argmax(d, axis=1)
        A = xz[np.arange(len(V)), e]; B = xz[np.arange(len(V)), (e + 1) % 3]
        L = np.linalg.norm(B - A, axis=1); k = np.maximum(1, np.ceil(L / 24.0).astype(np.int64)) + 1
        t = np.repeat(np.arange(len(V)), k)
        f = (np.arange(len(t)) - np.repeat(np.cumsum(k) - k, k)) / np.repeat(k - 1, k).clip(1)
        S = A[t] + (B[t] - A[t]) * f[:, None]
        o = -STEP * (GRID - 1) / 2
        gi = np.rint((S[:, 0] - o) / STEP).astype(np.int64); gj = np.rint((S[:, 1] - o) / STEP).astype(np.int64)
        near = (np.abs(o + gi * STEP - S[:, 0]) < 26) & (np.abs(o + gj * STEP - S[:, 1]) < 26) & (gi >= 0) & (gj >= 0) & (gi < GRID) & (gj < GRID)
        wpt = (gj * GRID + gi)[near]; wlo = lo[t][near]; whi = hi[t][near]
        oi = np.argsort(wpt, kind="stable"); wpt, wlo, whi = wpt[oi], wlo[oi], whi[oi]
        left = np.searchsorted(wpt, pt, "left"); right = np.searchsorted(wpt, pt, "right")
        cand = np.flatnonzero(floor & (right > left))
        for c0 in range(0, len(cand), 200000):
            cc = cand[c0:c0 + 200000]
            cnt = right[cc] - left[cc]
            rr = np.repeat(cc, cnt)
            ww = np.repeat(left[cc], cnt) + (np.arange(int(cnt.sum())) - np.repeat(np.cumsum(cnt) - cnt, cnt))
            hit = (wlo[ww] < y[rr] + 45) & (whi[ww] > y[rr] + 40)
            blocked[np.unique(rr[hit])] = True
    walk = np.full(N, NONE, np.int32); flags = np.zeros(N, np.uint8)
    # lowest good floor per point
    walled = good & blocked
    good = good & ~blocked
    gi = np.flatnonzero(good)
    first = np.unique(pt[gi], return_index=True)
    walk[first[0]] = np.rint(y[gi[first[1]]]).astype(np.int32)
    steep_pts = np.unique(pt[(ny > 0.05) & (ny <= 0.5)])
    any_pts = np.unique(pt)
    flags[any_pts] |= 1                                         # something is here
    flags[np.unique(pt[inside])] |= 2                           # a floor inside a solid (fine under a wall, a problem in the open)
    flags[steep_pts] |= 4                                       # a slope too steep to stand on
    flags[np.unique(pt[walled])] |= 8                           # a wall stands on this point's floor
    # upper floors: further good floors at least 100 units above the walk height
    wk = walk[pt]
    up = good & (wk != NONE) & (y > wk + 100)
    upper = np.c_[pt[up], np.rint(y[up])].astype(np.int64)
    return walk, flags, upper

# ---- water --------------------------------------------------------------------------------------------------------------------------------
def pools_from(meta):
    out = []
    for n, j in meta.items():
        for w in j["markers"]["water"]:
            nm = w["name"]
            if re.search(r"falls|sheet|cascade|spray|foam|mist", nm, re.I): continue
            V = np.array(w["verts"], np.float64)
            if len(V) < 3: continue
            span = np.ptp(V[:, :2], axis=0)
            if span.max() < 2.0: continue
            tile = 40.0
            if w["zmax"] - w["zmin"] < 0.6:   # a flat lake or pond: one box
                cells = [V]
            else:                             # a river falling along its length: one box per 40 m tile, at that tile's top
                key = np.floor(V[:, :2] / tile).astype(int)
                cells = [V[(key == k).all(1)] for k in np.unique(key, axis=0)]
            for C in cells:
                if len(C) < 3: continue
                x0, y0 = C[:, 0].min(), C[:, 1].min(); x1, y1 = C[:, 0].max(), C[:, 1].max()
                if x1 - x0 < 1.0 or y1 - y0 < 1.0: continue
                gx0, gz1 = gxz(x0, y0); gx1, gz0 = gxz(x1, y1)
                out.append({"name": nm, "x0": gx0, "x1": gx1, "z0": gz0, "z1": gz1, "y": float(C[:, 2].max() * UNITS)})
    # merge boxes of one river that touch and share a level within 10 units, to keep the count down
    merged = True
    while merged and len(out) > 1:
        merged = False
        for a in range(len(out)):
            for b in range(a + 1, len(out)):
                A, B = out[a], out[b]
                if abs(A["y"] - B["y"]) < 10 and A["x0"] <= B["x1"] + 1 and B["x0"] <= A["x1"] + 1 and A["z0"] <= B["z1"] + 1 and B["z0"] <= A["z1"] + 1:
                    ux = (min(A["x0"], B["x0"]), max(A["x1"], B["x1"])); uz = (min(A["z0"], B["z0"]), max(A["z1"], B["z1"]))
                    if (ux[1] - ux[0]) * (uz[1] - uz[0]) <= 1.6 * ((A["x1"] - A["x0"]) * (A["z1"] - A["z0"]) + (B["x1"] - B["x0"]) * (B["z1"] - B["z0"])):
                        A.update(x0=ux[0], x1=ux[1], z0=uz[0], z1=uz[1], y=max(A["y"], B["y"])); out.pop(b); merged = True; break
            if merged: break
    return out

# ---- textures -----------------------------------------------------------------------------------------------------------------------------
def noise(size, seed, octaves=4):
    rng = np.random.default_rng(seed); img = np.zeros((size, size))
    for o in range(octaves):
        f = 4 * 2 ** o
        g = rng.random((f, f))
        g = np.kron(g, np.ones((size // f, size // f)))
        g = (g + np.roll(g, size // f // 2, 0) + np.roll(g, size // f // 2, 1) + np.roll(np.roll(g, size // f // 2, 0), size // f // 2, 1)) / 4
        img += g / 2 ** o
    img -= img.min(); return img / max(1e-9, img.max())

def make_textures(tex_dir, size=256):
    """RGBA16 detail textures. The vertex colour carries the material's colour; a texture keeps its pattern and a little of its own hue, its
    average brought to about 0.85 grey so the two multiply back to the material's colour."""
    from PIL import Image
    out = []
    for k, name in enumerate(TEXTURES):
        f = TEX_FILES.get(name)
        img = None
        if f and tex_dir and os.path.exists(os.path.join(tex_dir, f)):
            im = Image.open(os.path.join(tex_dir, f)).convert("RGBA").resize((size, size), Image.LANCZOS)
            a = np.asarray(im, np.float64) / 255.0
            rgb = a[..., :3]
            if name == "leaves":   # a card with see-through gaps: fill them with the leaves' own shade
                m = a[..., 3:4]; mean = (rgb * m).sum((0, 1)) / max(1e-6, m.sum())
                rgb = rgb * m + mean * (1 - m)
            img = rgb
        if img is None:
            nz = noise(size, 100 + k)
            base = {"grass": 0.80, "dirt": 0.78, "plain": 0.9, "snow": 0.95}.get(name, 0.85)
            amp = {"grass": 0.35, "dirt": 0.30, "plain": 0.12, "snow": 0.10}.get(name, 0.2)
            img = np.repeat((base + amp * (nz - 0.5))[..., None], 3, axis=2)
        lum = img.mean(axis=2, keepdims=True)
        img = lum + 0.25 * (img - lum)                        # mostly the pattern; the vertex colour gives the hue
        img = img * (0.85 / max(1e-6, img.mean()))
        img = np.clip(img, 0, 1)
        r = np.rint(img[..., 0] * 31).astype(np.uint16); g = np.rint(img[..., 1] * 31).astype(np.uint16); b = np.rint(img[..., 2] * 31).astype(np.uint16)
        out.append(((r << 11) | (g << 6) | (b << 1) | 1).reshape(-1))
    return out

# ---- the drawn map ------------------------------------------------------------------------------------------------------------------------
SUN = np.array([-57.0, 99.0, -55.0]); SUN /= np.linalg.norm(SUN)
CELLS = int(round(2 * HALF / CELL)) + 1
FAR_SOLIDS = 40000       # far away, the tallest structures up to this many triangles (the far ground is a 512-unit heightfield)   # squares per side (the last one is a sliver)

def lit(col, nrm):
    shade = 0.62 + 0.48 * np.clip(nrm @ SUN, 0, 1)
    return np.clip(col * shade[:, None] / 0.85, 0, 255)

def build_draw(P, cols, tex, kinds, lod, out):
    """Adds batches for one level of detail to `out` (dict of lists). P (T,3,3) game units, cols (T,3), tex (T,), kinds (T,)."""
    n, area = face_normals(P)
    keep = area > 0.5
    P, cols, tex, kinds, n = P[keep], cols[keep], tex[keep], kinds[keep], n[keep]
    cen = P.mean(axis=1)
    ci = np.clip(((cen[:, 0] + HALF) // CELL).astype(int), 0, CELLS - 1); cj = np.clip(((cen[:, 2] + HALF) // CELL).astype(int), 0, CELLS - 1)
    cell = cj * CELLS + ci
    # texture axis: the ground and floors from above; walls from the side they face most
    axis = np.where((kinds == KIND_GROUND) | (np.abs(n[:, 1]) > 0.7), 1, np.where(np.abs(n[:, 0]) > np.abs(n[:, 2]), 0, 2))
    # vertex colours: lit with the normal averaged over the faces that meet at a corner and face the same way (same texture axis and side),
    # so a box keeps its hard edges but a wall's or a slope's triangles share their corners (one vertex for several triangles)
    side = np.sign(n[np.arange(len(n)), axis]).astype(np.int64)
    Q = np.rint(P).astype(np.int64).reshape(-1, 3)
    key = np.c_[Q, np.repeat(axis, 3), np.repeat(side, 3), np.repeat(kinds == KIND_GROUND, 3)]
    key[key[:, 5] == 1, 3:5] = 0   # the ground is smooth across every fold
    u, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    acc = np.zeros((len(u), 3)); np.add.at(acc, inv, np.repeat(n, 3, axis=0))
    vn = acc[inv] / np.maximum(np.linalg.norm(acc[inv], axis=1), 1e-9)[:, None]
    vcol = lit(np.repeat(cols.astype(np.float64), 3, axis=0), vn).reshape(-1, 3, 3)
    rep = np.where(lod == 1, 2048.0, np.where(kinds == KIND_GROUND, 400.0, 160.0))
    if np.isscalar(rep): rep = np.full(len(P), rep)
    order = np.lexsort((tex, cell))
    bounds = np.flatnonzero(np.r_[True, (cell[order][1:] != cell[order][:-1]) | (tex[order][1:] != tex[order][:-1]), True])
    for b0, b1 in zip(bounds[:-1], bounds[1:]):
        ids = order[b0:b1]
        # spatial order inside the batch, so a window's triangles share corners
        c = cen[ids]
        qx = np.clip(((c[:, 0] - c[:, 0].min()) // 16).astype(np.int64), 0, 65535); qz = np.clip(((c[:, 2] - c[:, 2].min()) // 16).astype(np.int64), 0, 65535)
        qy = np.clip(((c[:, 1] - c[:, 1].min()) // 64).astype(np.int64), 0, 1023)
        def spread(v):   # Morton code: interleave the bits, so triangles close in space are close in the list
            v = v & 0xFFFF; v = (v | (v << 8)) & 0x00FF00FF; v = (v | (v << 4)) & 0x0F0F0F0F; v = (v | (v << 2)) & 0x33333333; v = (v | (v << 1)) & 0x55555555
            return v
        ids = ids[np.argsort((spread(qx) | (spread(qz) << 1)) * 1024 + qy, kind="stable")]
        first_v = len(out["verts"]); first_w = len(out["windows"]); first_i = len(out["index"])
        wv = {}; wvl = []; wt = []; lo = None
        def flush():
            if not wt: return
            vl = np.array(wvl, np.float64)
            # texture coordinates: 4096 per repeat, moved by whole repeats so this window's numbers stay small
            st = vl[:, 3:5]
            shift = np.floor(st.min(axis=0) / 4096.0) * 4096.0
            st = np.rint(st - shift).astype(np.int64)
            for k, v in enumerate(vl):
                out["verts"].append((int(v[0]), int(v[1]), int(v[2]), int(st[k, 0]), int(st[k, 1]), int(v[5]), int(v[6]), int(v[7])))
            out["windows"].append((len(wvl), len(wt)))
            for t3 in wt: out["index"].extend(t3)
        stmin = None; stmax = None
        for t in ids:
            ax = axis[t]; r = rep[t]
            corners = []
            for k in range(3):
                x, y, z = P[t, k]
                if ax == 1: s_, t_ = x, z
                elif ax == 0: s_, t_ = z, -y
                else: s_, t_ = x, -y
                s_ *= 4096.0 / r; t_ *= 4096.0 / r
                cr = tuple(int(v) for v in np.clip(np.rint(vcol[t, k]), 0, 255))
                corners.append((int(round(x)), int(round(y)), int(round(z)), ax, cr, s_, t_))
            if len({c[:3] for c in corners}) < 3: continue   # a sliver that rounding to whole units folds into a line
            new = [c for c in corners if c[:5] not in wv]
            smn = min(c[5] for c in corners); smx = max(c[5] for c in corners); tmn = min(c[6] for c in corners); tmx = max(c[6] for c in corners)
            if wt and (len(wvl) + len(new) > 60 or len(wt) >= 120 or max(smx, stmax[0]) - min(smn, stmin[0]) > 28000 or max(tmx, stmax[1]) - min(tmn, stmin[1]) > 28000):
                flush(); wv = {}; wvl = []; wt = []; stmin = None
            if stmin is None: stmin = [smn, tmn]; stmax = [smx, tmx]
            else: stmin = [min(stmin[0], smn), min(stmin[1], tmn)]; stmax = [max(stmax[0], smx), max(stmax[1], tmx)]
            tri = []
            for c in corners:
                k = c[:5]
                if k not in wv:
                    wv[k] = len(wvl); wvl.append((c[0], c[1], c[2], c[5], c[6], c[4][0], c[4][1], c[4][2]))
                tri.append(wv[k])
            wt.append(tuple(tri))
        flush()
        allp = P[ids].reshape(-1, 3)
        lo_, hi_ = allp.min(0), allp.max(0); mid = (lo_ + hi_) / 2
        radius = float(np.linalg.norm(hi_ - mid))
        out["batches"].append((first_v, first_w, first_i, len(out["windows"]) - first_w, int(tex[ids[0]]), int(round(mid[0])), int(round(mid[1])),
                               int(round(mid[2])), int(min(65535, math.ceil(radius))), int(cell[ids[0]]), lod))

def minimap_for_far(P, C, K):
    """Top-view colour of the ground (kind 0 only) on a 129 x 129 grid over the map, for the far heightfield."""
    fine = 129; fstep = 2 * HALF / (fine - 1)
    g = K == KIND_GROUND
    n_, _ = face_normals(P[g])
    pt, yy, _, tt = raster(P[g], n_[:, 1], fstep, fine)
    o = np.lexsort((yy, pt)); pt, tt = pt[o], tt[o]
    last = np.r_[pt[1:] != pt[:-1], True]
    out = np.full((fine * fine, 3), (96, 120, 70), np.float64)
    out[pt[last]] = C[g][tt[last]]
    return out.reshape(fine, fine, 3)

def far_mesh(far, walk, col):
    step = 512.0; m = int(round(2 * HALF / step)) + 1
    W = walk.reshape(GRID, GRID).astype(np.float64)
    o = -STEP * (GRID - 1) / 2
    xs = -HALF + np.arange(m) * step
    H = np.zeros((m, m)); ok = np.zeros((m, m), bool)
    for j, z in enumerate(xs):
        for i, x in enumerate(xs):
            gi = int(round((x - o) / STEP)); gj = int(round((z - o) / STEP))
            blk = W[max(0, gj - 3):gj + 4, max(0, gi - 3):gi + 4]
            v = blk[blk != NONE]
            if len(v): H[j, i] = np.median(v); ok[j, i] = True
    H[~ok] = np.median(H[ok]) if ok.any() else 0
    cc = col.shape[0]
    tris = []; cols = []
    for j in range(m - 1):
        for i in range(m - 1):
            if not (ok[j, i] or ok[j, i + 1] or ok[j + 1, i] or ok[j + 1, i + 1]): continue
            a = (xs[i], H[j, i], xs[j]); b = (xs[i + 1], H[j, i + 1], xs[j]); c = (xs[i], H[j + 1, i], xs[j + 1]); d = (xs[i + 1], H[j + 1, i + 1], xs[j + 1])
            ci = min(cc - 1, int((xs[i] + step / 2 + HALF) / (2 * HALF) * (cc - 1))); cj = min(cc - 1, int((xs[j] + step / 2 + HALF) / (2 * HALF) * (cc - 1)))
            tris += [(a, d, b), (a, c, d)]; cols += [col[cj, ci]] * 2
    GP = np.array(tris, np.float64); GC = np.clip(np.array(cols), 0, 255).astype(np.uint8); GK = np.zeros(len(GP), np.uint8)
    nn_, _ = face_normals(GP)
    GT = np.where(nn_[:, 1] < 0.6, TEXTURES.index("rock"), np.where(GC[:, 1].astype(int) > GC[:, 0].astype(int) + 4, TEXTURES.index("grass"), TEXTURES.index("dirt")))
    BP, BC, BK, BT = [GP], [GC], [GK], [GT]
    cand = []   # (score, area, mask-holder): the tallest, widest structures first, until FAR_SOLIDS triangles
    for f in far:
        n, P, C, K, M = f[0], game_pos(f[1]), f[2], f[3], f[4]
        sel = K == KIND_SOLID
        if not sel.any(): continue
        e = np.load(os.path.join(ARGS.src, f"poi-{n}-far.npz"))["obj"][sel]
        Ps = P[sel]
        keep = np.zeros(len(Ps), bool)
        for ob in np.unique(e):
            mm = e == ob
            q = Ps[mm].reshape(-1, 3); ext = q.max(0) - q.min(0)
            if ext[1] > 12 * UNITS:   # tall: castles, towers, temples, the windmill, the Colossus
                cand.append((ext[1] * max(ext[0], ext[2]) / max(1, int(mm.sum())), n, Ps[mm], C[sel][mm], K[sel][mm], (M[sel][mm] if M is not None else np.array([""] * int(mm.sum()), dtype=object))))
    cand.sort(key=lambda c: -c[0])
    used = 0
    for score, n, Pm, Cm, Km, Mm in cand:
        if used + len(Pm) > FAR_SOLIDS: continue
        tex, _, _ = classify_tris(n, Cm, Km, Mm, face_normals(Pm)[0][:, 1])
        BP.append(Pm); BC.append(Cm); BK.append(Km); BT.append(tex); used += len(Pm)
    return np.concatenate(BP), np.concatenate(BC), np.concatenate(BK), np.concatenate(BT)

# ---- writing ------------------------------------------------------------------------------------------------------------------------------
def write_rows(f, typ, name, rows, per_line=1):
    f.write("inline constexpr %s %s[] = {\n" % (typ, name))
    line = []
    for r in rows:
        line.append("{" + ",".join(str(v) for v in r) + "}")
        if len(line) >= per_line: f.write(",".join(line) + ",\n"); line = []
    if line: f.write(",".join(line) + ",\n")
    f.write("};\n")

def write_flat(f, typ, name, values, per_line=40):
    f.write("inline constexpr %s %s[] = {\n" % (typ, name))
    v = [str(int(x)) for x in values]
    for k in range(0, len(v), per_line): f.write(",".join(v[k:k + per_line]) + ",\n")
    f.write("};\n")

def write_blob(f, name, raw, comment):
    """Bytes as base64 text in 16000-character pieces (one token each: a compiler copes with tens of megabytes of these, not with millions
    of numbers in braces). shared/hyrule_third_storm_ground.h and _draw.h decode them once, on first use."""
    import base64
    t = base64.b64encode(bytes(raw)).decode("ascii")
    f.write(f"// {comment}\n")
    f.write(f"inline constexpr size_t {name}Bytes = {len(raw)};\n")
    f.write(f"inline constexpr const char* {name}[] = {{\n")
    for k in range(0, len(t), 16000): f.write('"' + t[k:k + 16000] + '",\n')
    f.write("};\n")

def varints(values):
    """Zigzag varints: small numbers take one byte."""
    out = bytearray()
    for v in values:
        z = (int(v) << 1) ^ (int(v) >> 63)
        while True:
            b = z & 0x7F; z >>= 7
            if z: out.append(b | 0x80)
            else: out.append(b); break
    return bytes(out)

def delta_varints(a):
    """The walk grid row after row as the change from the value before (the ground is smooth: mostly one byte each)."""
    a = np.asarray(a, np.int64)
    d = np.diff(np.r_[0, a])
    return varints(d)

def runs(a):
    """Run lengths: (value byte, count varint) pairs."""
    a = np.asarray(a, np.uint8); out = bytearray(); k = 0
    while k < len(a):
        j = k
        while j < len(a) and a[j] == a[k]: j += 1
        out.append(int(a[k])); out += varints([j - k]); k = j
    return bytes(out)

def q(s): return '"' + s.replace("\\", "/").replace('"', '\\"') + '"'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--textures", default=os.path.join(DEFAULT_MAP, "textures"))
    ap.add_argument("--out", default=os.path.join(ROOT, "shared"))
    ap.add_argument("--report", default=os.path.join(ROOT, "build", "hyrule_third_storm"))
    a = ap.parse_args()
    global ARGS
    ARGS = a
    os.makedirs(a.report, exist_ok=True); os.makedirs(a.out, exist_ok=True)
    parts, far, meta = load(a.src)
    log("loaded", sum(len(p[1]) for p in parts), "triangles from", len(parts), "areas;", sum(len(p[1]) for p in far), "far")
    # ---- everything in game units, with texture and surface
    Ps, Cs, Ks, Ts, Ss, Hs, Os, As = [], [], [], [], [], [], [], []
    objnames = []
    for n, P, C, K, M, O in parts:
        G = game_pos(P)
        nrm, _ = face_normals(G)
        tex, sfx, hook = classify_tris(n, C, K, M, nrm[:, 1])
        Ps.append(G); Cs.append(C); Ks.append(K); Ts.append(tex); Ss.append(sfx); Hs.append(hook)
        Os.append(O.astype(np.int64) + len(objnames)); objnames += [f"{n}:{o}" for o in meta[n]["objects_list"]]
        As.append(np.full(len(K), int(n)))
    P = np.concatenate(Ps); C = np.concatenate(Cs); K = np.concatenate(Ks); TX = np.concatenate(Ts); SF = np.concatenate(Ss); HK = np.concatenate(Hs)
    OB = np.concatenate(Os); AR = np.concatenate(As)
    # the world is 16 bit: scenery past +-32760 (the Field's rim, edge skirts) is left out, not squashed
    inside = (np.abs(P).max(axis=(1, 2)) <= 32760)
    log("left out", int((~inside).sum()), "triangles past the 16-bit world")
    P, C, K, TX, SF, HK, OB, AR = P[inside], C[inside], K[inside], TX[inside], SF[inside], HK[inside], OB[inside], AR[inside]
    nrm, _ = face_normals(P)
    # ---- surfaces: (sfx, climb, hook); steep ground and rock is climbable, so is ivy (the materials' rule), never a building's wall
    climb = ((K == KIND_GROUND) & (nrm[:, 1] < 0.55)) | ((TX == TEXTURES.index("rock")) & (np.abs(nrm[:, 1]) < 0.55)) | \
            ((TX == TEXTURES.index("leaves")) & (K == KIND_SOLID))
    keys = np.c_[SF, climb, HK]
    surf_keys, surf = np.unique(keys, axis=0, return_inverse=True)
    if len(surf_keys) > 16: raise SystemExit("more than 16 surfaces")
    # ---- collision
    colmask = K != KIND_PROP
    log("collision from", int(colmask.sum()), "triangles")
    cv, ct, cn, cd, cs, ckeep = build_collision(P[colmask], K[colmask], surf[colmask])
    log("collision:", len(cv), "vertices,", len(ct), "triangles; plane distance", int(np.abs(cd).max()), "at most (s32, patches/0040)")
    colP = cv[ct].astype(np.float64)
    colK = K[colmask][ckeep]; colO = OB[colmask][ckeep]
    walk, wflags, upper = walk_grid(colP, cn, colK)
    # ---- props: box colliders
    colliders = []
    for nn, j in meta.items():
        for c in j["markers"]["colliders"]:
            lo = game_pos(np.array(c["min"], np.float64)); hi = game_pos(np.array(c["max"], np.float64))
            b0 = np.minimum(lo, hi); b1 = np.maximum(lo, hi)
            if (b1 - b0)[[0, 2]].max() < 12 or b1[1] - b0[1] < 30: continue
            colliders.append((int(b0[0]), int(b0[1]), int(b0[2]), int(b1[0]), int(b1[1]), int(b1[2]), q(c["name"][:40]), "true" if c["tree"] else "false"))
    # ---- water
    pools = pools_from(meta)
    log(len(pools), "water boxes")
    # ---- grid helpers
    org = -STEP * (GRID - 1) / 2
    W = walk.reshape(GRID, GRID).astype(np.float64)
    def walk_at(x, z):
        i = int(round((x - org) / STEP)); j = int(round((z - org) / STEP))
        if 0 <= i < GRID and 0 <= j < GRID: return W[j, i]
        return NONE
    def water_at(x, z):
        best = None
        for p in pools:
            if p["x0"] <= x <= p["x1"] and p["z0"] <= z <= p["z1"]: best = p["y"] if best is None else max(best, p["y"])
        return best
    # water flag in the grid (for the debug picture and the server's swim areas)
    wet = np.zeros(GRID * GRID, bool)
    for p in pools:
        i0 = int(math.ceil((p["x0"] - org) / STEP)); i1 = int(math.floor((p["x1"] - org) / STEP))
        j0 = int(math.ceil((p["z0"] - org) / STEP)); j1 = int(math.floor((p["z1"] - org) / STEP))
        if i1 < i0 or j1 < j0: continue
        sub = W[j0:j1 + 1, i0:i1 + 1]
        m = (sub != NONE) & (sub < p["y"] - 10)
        wet.reshape(GRID, GRID)[j0:j1 + 1, i0:i1 + 1] |= m
    # ---- somewhere to stand near a point: a floor, dry, not steep, level with its neighbours, clear of every prop box (the server's
    # placement test, HyruleThirdStormPlacementValid, says yes to it). Searched outward in rings of 64 units, up to `reach`.
    cbox = np.array([(c[0], c[2], c[3], c[5]) for c in colliders], np.float64) if colliders else np.zeros((0, 4))
    FL = (wflags.reshape(GRID, GRID) & 4 > 0) & (W == NONE)
    WETG = wet.reshape(GRID, GRID)
    def standable(i, j):
        if not (1 <= i < GRID - 1 and 1 <= j < GRID - 1): return False
        h0 = W[j, i]
        if h0 == NONE or WETG[j, i] or FL[j, i]: return False
        nb = W[j - 1:j + 2, i - 1:i + 2]
        if (nb == NONE).any() or np.abs(nb - h0).max() > 40: return False
        x, z = org + i * STEP, org + j * STEP
        if len(cbox) and ((cbox[:, 0] - 40 <= x) & (x <= cbox[:, 2] + 40) & (cbox[:, 1] - 40 <= z) & (z <= cbox[:, 3] + 40)).any(): return False
        return True
    def snap(x, z, reach=2000.0):
        i0 = int(round((x - org) / STEP)); j0 = int(round((z - org) / STEP))
        for r in range(0, int(reach / STEP) + 1):
            best = None
            for dj in range(-r, r + 1):
                for di in range(-r, r + 1):
                    if max(abs(di), abs(dj)) != r: continue
                    if standable(i0 + di, j0 + dj):
                        d = di * di + dj * dj
                        if best is None or d < best[0]: best = (d, i0 + di, j0 + dj)
            if best: return (round(org + best[1] * STEP), round(org + best[2] * STEP))
        return (round(x), round(z))
    # ---- loot, regions, bosses, stations
    loot = []; floor_sites = []
    for nn, j in meta.items():
        for m in j["markers"]["loot"] + j["markers"]["floor"]:
            x, y, z = m["loc"]; gx, gz = gxz(x, y); gy = z * UNITS
            # sit the chest on the floor under it (the marker may be a little above or below)
            best = None
            i = int(round((gx - org) / STEP)); jj = int(round((gz - org) / STEP))
            tier = re.search(r"T([2-4])", m["name"]); fl = "-Fl-" in m["name"]
            rec = {"name": m["name"], "x": round(gx, 1), "y": round(gy, 1), "z": round(gz, 1), "tier": int(tier.group(1)) if tier else 2, "floor": fl}
            (floor_sites if fl else loot).append(rec)
    drops = {}
    for nn, j in meta.items():
        for m in j["markers"]["drops"]:
            drops.setdefault(nn, []).append(m)
    regions = []
    for key in REGION_ORDER:
        if key in AREAS:
            pre, name, ax, ay = AREAS[key]
            ds = drops.get(key)
            if ds: ax, ay = ds[0]["loc"][0], ds[0]["loc"][1]   # the area's first drop spot is its heart
        else:
            name, ax, ay = POCKETS[key]
        gx, gz = snap(*gxz(ax, ay))
        boss = BOSS_ORDER.index(BOSSES[key]) if key in BOSSES else -1
        regions.append((q(name), round(gx), round(gz), boss))
    boss_sites = [(r[1], r[2], r[3]) for r in regions if r[3] >= 0]
    # allies wait at seven quieter places, carts at the Field's road joins
    ally_keys = ["08", "06", "02", "10", "09", "05", "07"]
    ally = []
    for k, key in enumerate(ally_keys):
        r = regions[REGION_ORDER.index(key)]
        ax_, az_ = snap(r[1] + 300, r[2] + 300)
        ally.append((ax_, az_, k))
    carts = []
    for m in meta.get("00", {}).get("markers", {}).get("exits", []):
        if m["name"].startswith("HF-J") and len(carts) < 12:
            gx, gz = snap(*gxz(m["loc"][0], m["loc"][1])); carts.append((gx, gz, 0))
    # ---- lobby: the flattest dry point within 40 m of LOBBY
    lx, lz = gxz(*LOBBY); best = None
    for dx in range(-1400, 1401, 128):
        for dz in range(-1400, 1401, 128):
            x, z = lx + dx, lz + dz
            h = [walk_at(x + ox, z + oz) for ox in (-128, 0, 128) for oz in (-128, 0, 128)]
            if min(h) == NONE or water_at(x, z) is not None: continue
            s = max(h) - min(h)
            if best is None or s < best[0]: best = (s, x, z)
    spawn = (round(best[1]), round(best[2])) if best else (round(lx), round(lz))
    # ---- upper floors (sampled) and climb walls for the bots
    up_nodes = []
    if len(upper):
        pi = upper[:, 0]; ii = pi % GRID; jj = pi // GRID
        pick = (ii % 3 == 0) & (jj % 3 == 0)
        for p_, y_ in upper[pick]:
            up_nodes.append((int(org + (p_ % GRID) * STEP), int(org + (p_ // GRID) * STEP), int(y_)))
    up_nodes = up_nodes[:6000]
    climb_walls = []
    cl = surf_keys[cs][:, 1].astype(bool) & (np.abs(cn[:, 1]) < 0.55)
    if cl.any():
        cP = colP[cl]; cN = cn[cl]
        cen = cP.mean(axis=1); span = cP[:, :, 1].max(1) - cP[:, :, 1].min(1)
        big = span > 90
        key = np.floor(cen[big][:, [0, 2]] / 256).astype(np.int64)
        _, first = np.unique(key, axis=0, return_index=True)
        for k in first:
            t = np.flatnonzero(big)[k]
            nx, nz = cN[t, 0], cN[t, 2]; l = math.hypot(nx, nz) or 1
            climb_walls.append((int(cen[t, 0]), int(cen[t, 2]), int(round(nx / l * 100)), int(round(nz / l * 100)), int(cP[t, :, 1].min()), int(cP[t, :, 1].max())))
    climb_walls = climb_walls[:5000]
    # ---- buildings (footprints for weather and loot anchors)
    bmap = {}
    for nn, j in meta.items():
        for b in j["markers"]["buildings"]:
            lo = np.array(b["min"]); hi = np.array(b["max"])
            k = (nn, b["id"])
            if k in bmap: bmap[k] = (np.minimum(bmap[k][0], lo), np.maximum(bmap[k][1], hi))
            else: bmap[k] = (lo, hi)
    buildings = []
    for (nn, bid), (lo, hi) in sorted(bmap.items()):
        if (hi - lo)[:2].max() > 160: continue   # a whole-area mesh caught by the name, not a house
        x0, z1 = gxz(lo[0], lo[1]); x1, z0 = gxz(hi[0], hi[1])
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        fy = walk_at(cx, cz)
        buildings.append((round(cx), round(cz), round((x1 - x0) / 2), round((z1 - z0) / 2), round(fy if fy != NONE else lo[2] * UNITS), 0))
    # ---- drawn map
    log("drawing data: near")
    draw = {"verts": [], "windows": [], "index": [], "batches": []}
    build_draw(P, C, TX, K, 0, draw)
    nearv = len(draw["verts"])
    log("near:", nearv, "vertices,", len(draw["index"]) // 3, "triangles,", len(draw["batches"]), "batches")
    if far:
        # far away: the ground as a 512-unit heightfield of the walk grid (coloured from above), and only the big solids (buildings,
        # walls and towers more than 14 m across or 9 m tall) of the far extraction
        FP, FC, FK, FT = far_mesh(far, walk, minimap_for_far(P, C, K))
        ok = np.abs(FP).max(axis=(1, 2)) <= 32760
        FP, FC, FK, FT = FP[ok], FC[ok], FK[ok], FT[ok]
        build_draw(FP, FC, FT, FK, 1, draw)
    log("far:", len(draw["verts"]) - nearv, "vertices; total", len(draw["index"]) // 3, "triangles")
    textures = make_textures(a.textures)
    # ---- the minimap picture (385 x 385, top view), and the island tables every map supplies (65 x 65 heights, 256 x 256 cover)
    fine = 385; fstep = 2 * HALF / (fine - 1)
    allP = P; nn_, _ = face_normals(allP)
    lc = lit(C.astype(np.float64), nn_)
    pt, yy, ny_, tt = raster(allP, nn_[:, 1], fstep, fine)
    o = np.lexsort((yy, pt)); pt, yy, tt = pt[o], yy[o], tt[o]
    last = np.r_[pt[1:] != pt[:-1], True]
    mm = np.full((fine * fine, 3), (40, 60, 40), np.float64); top = np.full(fine * fine, -1e9)
    mm[pt[last]] = lc[tt[last]]; top[pt[last]] = yy[last]
    for p_ in pools:
        i0 = int(math.ceil((p_["x0"] + HALF) / fstep)); i1 = int(math.floor((p_["x1"] + HALF) / fstep))
        j0 = int(math.ceil((p_["z0"] + HALF) / fstep)); j1 = int(math.floor((p_["z1"] + HALF) / fstep))
        for j in range(max(0, j0), min(fine - 1, j1) + 1):
            for i in range(max(0, i0), min(fine - 1, i1) + 1):
                if top[j * fine + i] < p_["y"]: mm[j * fine + i] = (52, 110, 170)
    minimap = np.clip(mm, 0, 255).astype(np.uint8)
    from PIL import Image
    Image.fromarray(minimap.reshape(fine, fine, 3)).save(os.path.join(a.report, "minimap.png"))
    coarse = []
    for j in range(65):
        for i in range(65):
            h_ = walk_at(-HALF + i * 2 * HALF / 64, -HALF + j * 2 * HALF / 64)
            coarse.append(int(h_) if h_ != NONE else -3000)
    # ---- report and pictures
    report = problems_report(walk, wflags, wet, colP, cn, colK, colO, objnames, loot + floor_sites, regions, spawn, pools, a.report, AR)
    report.update({"collision_vertices": int(len(cv)), "collision_triangles": int(len(ct)), "draw_vertices": len(draw["verts"]),
                   "draw_triangles": len(draw["index"]) // 3, "batches": len(draw["batches"]), "chests": len(loot), "floor_loot": len(floor_sites),
                   "pools": len(pools), "colliders": len(colliders), "upper_nodes": len(up_nodes), "climb_walls": len(climb_walls),
                   "buildings": len(buildings), "spawn": spawn, "max_plane_dist": int(np.abs(cd).max()), "surfaces": surf_keys.tolist()})
    report["budget"] = draw_budget(draw, a.report)
    json.dump(report, open(os.path.join(a.report, "report.json"), "w"), indent=1)
    # ---- headers
    with open(os.path.join(a.out, "hyrule_third_storm_data.h"), "w", newline="\n") as f:
        f.write("// Generated by tools/maps/hyrule_third_storm/export.py; do not edit\n#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace royale { namespace hyrule_third_storm {\n")
        f.write(f"inline constexpr float kHalf = {HALF:.1f}f;   // the map spans -kHalf..kHalf on x and z (game units, 36 a metre)\n")
        f.write(f"inline constexpr int kGrid = {GRID};          // walk grid: kGrid x kGrid points, kStep apart, centred on 0\n")
        f.write(f"inline constexpr float kStep = {STEP:.1f}f;\ninline constexpr int16_t kNoFloor = {NONE};\n")
        write_blob(f, "kWalkB64", delta_varints(walk), "kWalk: per grid point, rows of kGrid, as zigzag varints of the change from the point before: the lowest floor with head room and nothing solid on it (kNoFloor: none, you would fall)")
        f.write(f"inline constexpr size_t kWalkCount = {len(walk)};\n")
        fl = (wet.astype(np.uint8) | (((wflags & 4) > 0) & (walk == NONE)).astype(np.uint8) * 2)
        write_blob(f, "kWalkFlagsB64", runs(fl), "kWalkFlags: per grid point (1 water, 2 too steep to stand on) as runs: a value byte, then its count as a varint")
        f.write("// The tables every island map supplies (shared/fortnite_map.h UseTerrainForMap), over this map's square: 65 x 65 heights, the\n"
                "// minimap's 385 x 385 colours (r, g, b) and 256 x 256 cover codes (3: nothing is grown on an authored map).\n")
        write_flat(f, "int16_t", "kHeights", coarse, 65)
        write_blob(f, "kColoursB64", minimap.reshape(-1).astype(np.uint8).tobytes(), "kColours: the minimap, 385 x 385 x (r, g, b)")
        f.write("inline constexpr char kCover[] =\n")
        for j in range(256): f.write('"' + "3" * 256 + '"\n')
        f.write(";\n")
        f.write("struct Surface { uint8_t sfx; bool climb, hook; };\n")
        write_rows(f, "Surface", "kSurfaces", [(int(s[0]), "true" if s[1] else "false", "true" if s[2] else "false") for s in surf_keys], 4)
        f.write("// The collision itself is in hyrule_third_storm_collision.h (only the game and its test include it).\n")
        f.write("struct PropCollider { int16_t x0,y0,z0,x1,y1,z1; const char* name; bool tree; };\n")
        write_rows(f, "PropCollider", "kPropColliders", colliders or [(0, 0, 0, 0, 0, 0, q(""), "false")], 2)
        f.write("struct Obstacle { float x0,x1,z0,z1; };   // the colliders' footprints, for placement and the bots (BoxIndex)\n")
        write_rows(f, "Obstacle", "kObstacles", [(c[0], c[3], c[2], c[5]) for c in colliders] or [(0, 0, 0, 0)], 6)
        f.write("struct Building { float x,z,halfWidth,halfDepth,floorY,yaw; };\n")
        write_rows(f, "Building", "kBuildings", buildings or [(0, 0, 0, 0, 0, 0)], 4)
        f.write("struct UpperNode { int16_t x,z,y; };   // a floor above the ground: an upper storey, a balcony, a bridge deck\n")
        write_rows(f, "UpperNode", "kUpperNodes", up_nodes or [(0, 0, 0)], 8)
        f.write("struct ClimbWall { int16_t x,z,nx,nz,y0,y1; };   // a climbable face: where, which way it faces (hundredths), lowest and highest point\n")
        write_rows(f, "ClimbWall", "kClimbWalls", climb_walls or [(0, 0, 100, 0, 0, 0)], 6)
        f.write("struct LootSite { float x,y,z; int tier; };   // a chest (tier 2 to 4, from the Blender marker's name)\n")
        write_rows(f, "LootSite", "kLootSites", [(s["x"], s["y"], s["z"], s["tier"]) for s in loot], 4)
        f.write("// Floor loot spots (the -Fl- markers: weapons and potions lying about).\n")
        write_rows(f, "LootSite", "kFloorSites", [(s["x"], s["y"], s["z"], s["tier"]) for s in floor_sites] or [(0, 0, 0, 0)], 4)
        f.write("struct Region { const char* name; float x,z; int boss; };\n")
        write_rows(f, "Region", "kRegions", regions, 1)
        f.write("struct Pool { float x0,x1,z0,z1,y; };   // water: one game water box each\n")
        write_rows(f, "Pool", "kPools", [(round(p["x0"]), round(p["x1"]), round(p["z0"]), round(p["z1"]), round(p["y"])) for p in pools] or [(0, 0, 0, 0, -30000)], 2)
        f.write("struct Station { float x,z; int kind; };\n")
        write_rows(f, "Station", "kAllyStations", ally, 4)
        write_rows(f, "Station", "kCartStations", carts or [(spawn[0] + 400, spawn[1], 0)], 4)
        f.write("struct Encounter { float x,z; int boss; };\n")
        write_rows(f, "Encounter", "kBossSites", boss_sites, 4)
        f.write("struct DebugSpot { float x,z; int kind; };   // problems the exporter found, for the map debug view (MapDebug.inc): 0 a hole, 1 a chest with no floor\n")
        spots = [(c["x"], c["z"], 0) for c in report["hole_clusters"]] + [(round(b["x"]), round(b["z"]), 1) for b in report["loot_without_floor"]]
        write_rows(f, "DebugSpot", "kDebugSpots", spots or [(0, 0, -1)], 6)
        f.write(f"inline constexpr float kSpawnX = {spawn[0]}, kSpawnZ = {spawn[1]};   // the lobby: the open Field\n")
        f.write(f"inline constexpr float kFarClip = {FAR_CLIP:.0f}.0f;   // gMapFarClip on this map\n")
        f.write("}}  // namespace royale::hyrule_third_storm\n")
    with open(os.path.join(a.out, "hyrule_third_storm_collision.h"), "w", newline="\n") as f:
        f.write("// Generated by tools/maps/hyrule_third_storm/export.py; do not edit. Decoded by shared/hyrule_third_storm_draw.h.\n#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace royale { namespace hyrule_third_storm {\n")
        write_blob(f, "kCollisionVerticesB64", cv.astype("<i2").tobytes(), "collision vertices: int16 x, y, z")
        N16 = np.rint(cn * 32767).astype(np.int64)
        tt = np.zeros(len(ct), dtype=[("a", "<u4"), ("b", "<u4"), ("c", "<u4"), ("nx", "<i2"), ("ny", "<i2"), ("nz", "<i2"), ("d", "<i4"), ("s", "u1")])
        tt["a"], tt["b"], tt["c"] = ct[:, 0], ct[:, 1], ct[:, 2]; tt["nx"], tt["ny"], tt["nz"] = N16[:, 0], N16[:, 1], N16[:, 2]; tt["d"] = cd; tt["s"] = cs
        write_blob(f, "kCollisionTrianglesB64", tt.tobytes(), "collision triangles, 23 bytes each: uint32 a, b, c; int16 nx, ny, nz (unit normal x 32767); int32 plane distance (patches/0040); uint8 surface")
        f.write("}}  // namespace royale::hyrule_third_storm\n")
    with open(os.path.join(a.out, "hyrule_third_storm_model.h"), "w", newline="\n") as f:
        f.write("// Generated by tools/maps/hyrule_third_storm/export.py; do not edit. Decoded by shared/hyrule_third_storm_draw.h.\n#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace royale { namespace hyrule_third_storm {\n")
        V = np.array(draw["verts"], np.int64)
        vv = np.zeros(len(V), dtype=[("x", "<i2"), ("y", "<i2"), ("z", "<i2"), ("s", "<i2"), ("t", "<i2"), ("r", "u1"), ("g", "u1"), ("b", "u1")])
        for k, nm in enumerate(["x", "y", "z", "s", "t", "r", "g", "b"]): vv[nm] = V[:, k]
        write_blob(f, "kDrawVerticesB64", vv.tobytes(), "drawn vertices, 13 bytes each: int16 x, y, z, s, t; uint8 r, g, b (the material colour with the baked sun)")
        write_blob(f, "kWindowsB64", np.array(draw["windows"], np.uint8).tobytes(), "windows, 2 bytes each: vertices in one load, triangles drawn from it")
        write_blob(f, "kIndicesB64", np.array(draw["index"], np.uint8).tobytes(), "triangle corners, one byte each, numbered within their window")
        B = np.array(draw["batches"], np.int64)
        bb = np.zeros(len(B), dtype=[("fv", "<u4"), ("fw", "<u4"), ("fi", "<u4"), ("w", "<u2"), ("tex", "<u2"), ("x", "<i2"), ("y", "<i2"), ("z", "<i2"), ("r", "<u2"), ("cell", "<u2"), ("lod", "u1")])
        for k, nm in enumerate(["fv", "fw", "fi", "w", "tex", "x", "y", "z", "r", "cell", "lod"]): bb[nm] = B[:, k]
        write_blob(f, "kBatchesB64", bb.tobytes(), "batches, 27 bytes each: uint32 first vertex, first window, first index; uint16 windows, texture; int16 x, y, z; uint16 radius, cell; uint8 lod (0 near, 1 far)")
        write_blob(f, "kTexturesB64", np.concatenate(textures).astype("<u2").tobytes(), "textures: kTextureCount x 256 x 256 RGBA16 (5551), " + ", ".join(TEXTURES))
        f.write(f"inline constexpr int kCells = {CELLS};\ninline constexpr float kCell = {CELL:.1f}f;\ninline constexpr float kNear = {NEAR:.1f}f;\n")
        f.write(f"inline constexpr int kTextureSize = 256;\ninline constexpr int kTextureCount = {len(textures)};\n")
        f.write("}}  // namespace royale::hyrule_third_storm\n")
    log("written; report", os.path.join(a.report, "report.json"))
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, (list, dict))}, indent=1))

# ---- debug: problems and pictures -----------------------------------------------------------------------------------------------------------
def problems_report(walk, wflags, wet, colP, cn, colK, colO, objnames, sites, regions, spawn, pools, outdir, AR):
    """What could go wrong in play, with where: holes (no floor in the open), floors buried in solids, loot sites with no floor under them.
    Writes walk.png (green/brown by height; blue water; red too steep; black no floor; magenta loot problem) and problems.json."""
    from PIL import Image, ImageDraw
    org = -STEP * (GRID - 1) / 2
    W = walk.reshape(GRID, GRID); F = wflags.reshape(GRID, GRID); WET = wet.reshape(GRID, GRID)
    has = W != NONE
    hmin, hmax = np.percentile(W[has], 1), np.percentile(W[has], 99)
    t = np.clip((W - hmin) / max(1, hmax - hmin), 0, 1)
    img = np.zeros((GRID, GRID, 3), np.float64)
    img[..., 0] = 70 + 130 * t; img[..., 1] = 120 + 90 * t; img[..., 2] = 60 + 60 * t
    img[~has] = (0, 0, 0)
    img[(~has) & ((F & 4) > 0)] = (200, 40, 40)
    img[WET] = img[WET] * 0.3 + np.array([30, 90, 200]) * 0.7
    # slope between neighbours: a step of more than 2.2 m inside one 64-unit step (a ledge or wall) drawn darker
    dx = np.abs(np.diff(W.astype(np.int64), axis=1)); dz = np.abs(np.diff(W.astype(np.int64), axis=0))
    edge = np.zeros_like(has); edge[:, 1:] |= (dx > 80) & has[:, 1:] & has[:, :-1]; edge[1:, :] |= (dz > 80) & has[1:, :] & has[:-1, :]
    img[edge] *= 0.55
    # holes: no floor, surrounded mostly by floor (a gap in the ground you could drop through), away from water
    from numpy.lib.stride_tricks import sliding_window_view as swv
    pad = np.pad(has, 2)
    nb = swv(pad, (5, 5)).sum(axis=(2, 3))
    holes = (~has) & (nb >= 18) & ~WET & ((F & 4) == 0)
    hy, hx = np.nonzero(holes)
    # loot with no floor within 1 m below or 2 m above
    bad_sites = []
    for s in sites:
        # a floor within one grid step (a chest stands against a wall, whose points are closed to walking) at the chest's own height
        i = int(round((s["x"] - org) / STEP)); j = int(round((s["z"] - org) / STEP))
        nb = W[max(0, j - 1):j + 2, max(0, i - 1):i + 2].reshape(-1)
        nb = nb[nb != NONE]
        h = int(nb[np.argmin(np.abs(nb - s["y"]))]) if len(nb) else NONE
        if h == NONE or not (h - 80 <= s["y"] <= h + 1000):
            bad_sites.append({"name": s["name"], "x": s["x"], "y": s["y"], "z": s["z"], "floor": int(h)})
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    for x, y in zip(hx, hy): d.point((x, y), fill=(255, 255, 0))
    for s in sites:
        i = (s["x"] - org) / STEP; j = (s["z"] - org) / STEP
        d.rectangle([i - 1, j - 1, i + 1, j + 1], fill=(255, 220, 120))
    for b in bad_sites:
        i = (b["x"] - org) / STEP; j = (b["z"] - org) / STEP
        d.ellipse([i - 5, j - 5, i + 5, j + 5], outline=(255, 0, 255), width=2)
    for r in regions:
        i = (r[1] - org) / STEP; j = (r[2] - org) / STEP
        d.text((i + 4, j - 6), r[0].strip('"'), fill=(255, 255, 255))
    i = (spawn[0] - org) / STEP; j = (spawn[1] - org) / STEP
    d.ellipse([i - 6, j - 6, i + 6, j + 6], outline=(0, 255, 255), width=3)
    im.save(os.path.join(outdir, "walk.png"))
    # clusters of holes, for the report (where to look in game: the debug view's teleport takes these coordinates)
    clusters = []
    if len(hx):
        key = np.c_[hx // 16, hy // 16]
        u, cnt = np.unique(key, axis=0, return_counts=True)
        for (ci, cj), c in sorted(zip(u.tolist(), cnt.tolist()), key=lambda z: -z[1])[:40]:
            clusters.append({"x": int(org + (ci * 16 + 8) * STEP), "z": int(org + (cj * 16 + 8) * STEP), "points": int(c)})
    # thin or tiny collision triangles (slivers that snag), per object
    n_, area = face_normals(colP)
    e = np.linalg.norm(colP[:, [1, 2, 0]] - colP, axis=2)
    sliver = (area / np.maximum(e.max(1) ** 2, 1e-9) < 0.01) & (e.max(1) > 40)
    worst = {}
    for o in colO[sliver]: worst[objnames[o]] = worst.get(objnames[o], 0) + 1
    rep = {"walk_points": int(has.sum()), "no_floor_points": int((~has).sum()), "hole_points": int(holes.sum()), "hole_clusters": clusters,
           "loot_without_floor": bad_sites, "sliver_triangles": int(sliver.sum()),
           "sliver_objects": sorted(worst.items(), key=lambda z: -z[1])[:30], "steep_points": int(((F & 4) > 0).sum())}
    json.dump(rep, open(os.path.join(outdir, "problems.json"), "w"), indent=1)
    return rep

def draw_budget(draw, outdir):
    """What the game draws from points across the map: near squares within kNear of the eye, far squares beyond. Writes budget.png (a heat map
    of triangles drawn from each spot) and returns the worst and average."""
    from PIL import Image
    B = np.array([b[:11] for b in draw["batches"]], np.float64)
    wtri = np.array([w[1] for w in draw["windows"]], np.int64)
    cum = np.r_[0, np.cumsum(wtri)]
    tris = cum[(B[:, 1] + B[:, 3]).astype(int)] - cum[B[:, 1].astype(int)]
    cellc = np.c_[(B[:, 9] % CELLS + 0.5) * CELL - HALF, (B[:, 9] // CELLS + 0.5) * CELL - HALF]
    lod = B[:, 10]
    res = 32; heat = np.zeros((res, res)); worst = (0, None)
    for j in range(res):
        for i in range(res):
            x = -HALF + (i + 0.5) * 2 * HALF / res; z = -HALF + (j + 0.5) * 2 * HALF / res
            dcell = np.hypot(cellc[:, 0] - x, cellc[:, 1] - z)
            near_cell = dcell < NEAR + CELL * 0.71
            # the game draws a square's near mesh when it is near, else its far mesh; only what is in front (about half) is drawn
            drawn = (tris * ((lod == 0) & near_cell)).sum() + (tris * ((lod == 1) & ~near_cell)).sum()
            heat[j, i] = drawn
            if drawn > worst[0]: worst = (int(drawn), (round(x), round(z)))
    im = Image.fromarray(np.clip(heat / max(1, heat.max()) * 255, 0, 255).astype(np.uint8)).resize((512, 512), Image.NEAREST)
    im.save(os.path.join(outdir, "budget.png"))
    return {"worst_triangles_in_reach": worst[0], "worst_at": worst[1], "average_in_reach": int(heat.mean()),
            "note": "all squares in reach, both halves of the view; the game draws about half (what is in front)"}

if __name__ == "__main__":
    main()
