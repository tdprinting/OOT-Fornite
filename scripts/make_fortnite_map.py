#!/usr/bin/env python3
"""Builds shared/fortnite_map_data.h, the "Fortnite Map" arena, from Tee's own heightmap and texture.

  python3 scripts/make_fortnite_map.py --from-obj plain.obj map_texture.png   (once: turns the Blender plane into the two files below)
  python3 scripts/make_fortnite_map.py                                         (every time: heightmap.png + texture.jpg -> shared/fortnite_map_data.h)

Needs numpy and pillow. The inputs are the artist's own files (no ROM data), so they live in assets/fortnite/.

What comes out (see shared/fortnite_map.h for how it is used):
  * a coarse grid of ground heights (kCells x kCells squares, two triangles each): THIS is the game's collision, so what you stand on is exactly
    what is drawn at the same spot;
  * a fine grid of vertex colours (kSub times finer): the texture baked into the mesh, because the game draws these meshes with vertex colours,
    with a little hill shading mixed in;
  * the numbers the rest of the mod needs: where the water is, where the lobby spawn is, the circle that holds the island;
  * and, in shared/fortnite_cover_data.h, what covers the ground in each square, read from the texture's colours: water, meadow, woods (the
    painted trees), paving (roads, towns, bare rock) or dirt (fields, the swamp). The mod grows its trees and grass from it, so the 3D foliage
    stands where the picture has it.
"""
import argparse, os, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ASSETS = os.path.join(ROOT, "assets", "fortnite")
OUT = os.path.join(ROOT, "shared", "fortnite_map_data.h")
COVER_OUT = os.path.join(ROOT, "shared", "fortnite_cover_data.h")

XZ_SCALE = 880.0      # game units per unit of the model: the island holds a circle about as wide as Hyrule Field's floor (radius ~7400)
Y_SCALE = 450.0       # game units of height per unit of the model: hills a few Links tall, cliffs about nine
CELLS = 64            # collision squares per side (vertex indices in the game's collision are 13 bits, so 65*65 vertices is plenty of room)
SUB = 6               # colour/visual squares per collision square, per side (more of them = a sharper picture on the ground)
WATER_H = -0.32       # model height of the water surface: every lake, river and sea bed in the heightmap is lower than this
H_REF = 0.185         # model height that becomes y = 0 (the typical land)
H_MIN, H_MAX = -0.964222, 0.964222   # the heightmap file stores heights between these as 16 bit numbers
MAX_STEP = 165        # steepest rise between two neighbouring collision vertices on land, in game units: about 35 degrees, which Link can stand on
COVER = 256           # ground cover squares per side (what grows or stands on the ground, read from the texture)


def from_obj(obj_path, tex_path):
    V, vt_idx = [], []
    VT = []
    faces = []
    with open(obj_path) as f:
        for line in f:
            if line.startswith("v "):
                V.append(line.split()[1:4])
            elif line.startswith("vt "):
                VT.append(line.split()[1:3])
            elif line.startswith("f "):
                faces.append([[int(a) for a in t.split("/")[:2]] for t in line.split()[1:]])
    V = np.array(V, dtype=np.float32)
    xs, zs = np.unique(V[:, 0]), np.unique(V[:, 2])
    assert len(xs) * len(zs) == len(V), "the model must be a regular grid"
    n = len(xs)
    ix, iz = np.searchsorted(xs, V[:, 0]), np.searchsorted(zs, V[:, 2])
    h = np.zeros((len(zs), len(xs)), dtype=np.float32)
    h[iz, ix] = V[:, 1]
    # the texture is laid over the whole plane: u runs along x, and the picture's top row is the plane's lowest z
    VT = np.array(VT, dtype=np.float32)
    f = np.array(faces)
    uv = np.zeros((len(V), 2), dtype=np.float32)
    uv[f[:, :, 0].ravel() - 1] = VT[f[:, :, 1].ravel() - 1]
    uvg = np.zeros((len(zs), len(xs), 2), dtype=np.float32)
    uvg[iz, ix] = uv
    assert abs(uvg[0, 0, 0]) < 1e-3 and abs(uvg[0, 0, 1] - 1) < 1e-3 and abs(uvg[-1, 0, 1]) < 1e-3, "unexpected texture layout"
    q = np.clip(np.round((h - H_MIN) / (H_MAX - H_MIN) * 65535.0), 0, 65535).astype(np.uint16)
    meta = {"width": float(xs[-1] - xs[0]), "depth": float(zs[-1] - zs[0])}
    Image.fromarray(q).save(os.path.join(ASSETS, "heightmap.png"))
    Image.open(tex_path).convert("RGB").save(os.path.join(ASSETS, "texture.jpg"), quality=90)
    with open(os.path.join(ASSETS, "plane.txt"), "w") as m:
        m.write("%.6f %.6f\n" % (meta["width"], meta["depth"]))
    print("wrote heightmap.png, texture.jpg, plane.txt", meta)


def load():
    q = np.asarray(Image.open(os.path.join(ASSETS, "heightmap.png"))).astype(np.float64)
    h = q / 65535.0 * (H_MAX - H_MIN) + H_MIN
    tex = np.asarray(Image.open(os.path.join(ASSETS, "texture.jpg")).convert("RGB")).astype(np.float64)
    w, d = [float(t) for t in open(os.path.join(ASSETS, "plane.txt")).read().split()]
    return h, tex, w, d


def bilinear(img, fx, fy):
    """img[y][x(,c)] sampled at fractional pixel coordinates (arrays)."""
    hgt, wid = img.shape[:2]
    fx = np.clip(fx, 0, wid - 1.0001); fy = np.clip(fy, 0, hgt - 1.0001)
    x0 = fx.astype(int); y0 = fy.astype(int); tx = fx - x0; ty = fy - y0
    if img.ndim == 3:
        tx = tx[..., None]; ty = ty[..., None]
    a = img[y0, x0]; b = img[y0, x0 + 1]; c = img[y0 + 1, x0]; e = img[y0 + 1, x0 + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + e * tx) * ty


def cover_grid(tex):
    """What covers each of COVER x COVER squares: 0 water, 1 meadow, 2 woods, 3 paving (roads, towns, rock), 4 dirt (fields, swamp)."""
    n = tex.shape[0] // COVER
    r, g, b = tex[..., 0], tex[..., 1], tex[..., 2]
    hi, lo = tex.max(-1), tex.min(-1)
    sat = (hi - lo) / (hi + 1)
    water = (b > r + 15) & (b > g - 5)
    paved = (sat < 0.25) & (hi > 105) & ~water           # grey and white: tarmac, roofs, cliffs
    dirt = (r >= g - 5) & (r > b + 25) & (hi > 60) & ~paved
    woods = (g > r + 8) & (g >= b) & (hi < 100) & ~water  # the dark green of the painted tree tops
    share = lambda m: m.reshape(COVER, n, COVER, n).mean((1, 3))
    out = np.ones((COVER, COVER), dtype=np.uint8)
    out[share(woods) > 0.45] = 2
    out[share(dirt) > 0.35] = 4
    out[share(paved) > 0.3] = 3
    out[share(water) > 0.5] = 0
    return out


def write_cover(cover):
    with open(COVER_OUT, "w") as f:
        f.write("// GENERATED by scripts/make_fortnite_map.py from assets/fortnite/texture.jpg. Do not edit by hand.\n#pragma once\n#include <cstdint>\n\n")
        f.write("namespace royale {\nnamespace fortnite_data {\n\n")
        f.write("inline constexpr int kCoverCells = %d;   // ground cover squares per side, over the same square as the island\n\n" % COVER)
        f.write("// What covers the ground in each square (0 water, 1 meadow, 2 woods, 3 paving, 4 dirt), row after row (z), one character per square.\n")
        f.write("inline constexpr const char kCover[] =\n")
        for row in cover:
            f.write('    "%s"\n' % "".join(str(int(v)) for v in row))
        f.write(";\n\n} // namespace fortnite_data\n} // namespace royale\n")
    print("wrote", COVER_OUT, "cover: " + ", ".join("%s %.0f%%" % (nm, 100 * (cover == k).mean()) for k, nm in enumerate(["water", "meadow", "woods", "paving", "dirt"])))


def easier_ground(ys, water_y):
    """Makes the land easier to get about on: one light smoothing pass over the inland (the coast keeps its shape), then no step between two
    neighbouring land vertices steeper than MAX_STEP, so hillsides can be walked rather than slid down. Returns the new integer heights."""
    h = ys.astype(float)
    land = h > water_y + 10
    inner = land.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            inner &= np.roll(np.roll(land, dy, 0), dx, 1)
    inner[0, :] = inner[-1, :] = False; inner[:, 0] = inner[:, -1] = False
    p = np.pad(h, 1, mode="edge")
    k = np.array([1, 2, 1], dtype=float) / 4.0
    sm = sum(k[a] * k[b] * p[a:a + h.shape[0], b:b + h.shape[1]] for a in range(3) for b in range(3))
    h = np.where(inner, sm, h)
    for _ in range(40):                              # relax the steep steps until none is left
        changed = False
        for axis in (0, 1):
            a = h; b = np.roll(h, -1, axis)
            both = (a > water_y + 10) & (b > water_y + 10)
            if axis == 0: both[-1, :] = False
            else: both[:, -1] = False
            diff = b - a
            excess = np.where(both & (np.abs(diff) > MAX_STEP), np.abs(diff) - MAX_STEP, 0.0)
            if excess.max() > 0.5:
                changed = True
                move = np.sign(diff) * excess * 0.5
                h = h + move                          # the lower end of the pair comes up, the upper end goes down
                h = h - np.roll(move, 1, axis)
        if not changed:
            break
    steep = lambda g: float((np.abs(np.diff(g, axis=0)) > MAX_STEP).mean() + (np.abs(np.diff(g, axis=1)) > MAX_STEP).mean()) / 2
    print("steps steeper than %d: %.1f%% before, %.1f%% after" % (MAX_STEP, 100 * steep(ys.astype(float)), 100 * steep(h)))
    return np.round(h).astype(int)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from-obj", nargs=2, metavar=("OBJ", "TEXTURE"))
    args = ap.parse_args()
    if args.from_obj:
        os.makedirs(ASSETS, exist_ok=True)
        from_obj(*args.from_obj)
        return
    h, tex, W, D = load()
    n = h.shape[0]                                   # 1025 samples per side
    half_x, half_z = W * XZ_SCALE / 2.0, D * XZ_SCALE / 2.0
    cell_x, cell_z = 2 * half_x / CELLS, 2 * half_z / CELLS

    # ---- collision grid: average the heightmap round each vertex (a 9x9 window) so single sharp pixels don't make spikes -----------------
    pad = np.pad(h, 4, mode="edge")
    k = (n - 1) // CELLS                              # 16 samples per square
    idx = np.arange(CELLS + 1) * k
    coarse = np.zeros((CELLS + 1, CELLS + 1))
    for dy in range(9):
        for dx in range(9):
            coarse += pad[idx[:, None] + dy, idx[None, :] + dx]
    coarse /= 81.0                                    # coarse[j][i]: j runs along z, i along x
    ys = np.round((coarse - H_REF) * Y_SCALE).astype(int)
    water_y = int(round((WATER_H - H_REF) * Y_SCALE))
    ys = easier_ground(ys, water_y)

    # ---- fine grid heights: the same two triangles per square the collision uses (split from the low corner to the far corner) ------------
    F = CELLS * SUB
    fi = np.arange(F + 1)
    ci = np.minimum(fi // SUB, CELLS - 1); u = (fi - ci * SUB) / SUB          # square and position inside it
    def fine_height():
        cj = ci[:, None]; cx = ci[None, :]; v = u[:, None]; uu = u[None, :]
        h00 = ys[cj, cx]; h10 = ys[cj, cx + 1]; h01 = ys[cj + 1, cx]; h11 = ys[cj + 1, cx + 1]
        lower = uu >= v                                # triangle (00, 10, 11)
        a = h00 + uu * (h10 - h00) + v * (h11 - h10)
        b = h00 + uu * (h11 - h01) + v * (h01 - h00)
        return np.where(lower, a, b)
    fh = fine_height()

    # ---- colours: the texture at each fine vertex, plus a little hill shading --------------------------------------------------------
    tw = tex.shape[0]
    # Sample the picture finely (a light filter, not the old 4x box blur), then sharpen it and lift its colours a little, so roads, fields and
    # painted detail stay readable on the ground at the size one vertex covers.
    src = Image.fromarray(tex.astype(np.uint8)).resize((F * 2, F * 2), Image.LANCZOS)
    px = fi / F * (F * 2 - 1)
    gx, gy = np.meshgrid(px, px)
    rgb = bilinear(np.asarray(src).astype(np.float64), gx, gy)
    sharp = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).filter(ImageFilter.UnsharpMask(radius=1.4, percent=60, threshold=3))
    rgb = np.asarray(sharp).astype(np.float64)
    grey = rgb.mean(-1, keepdims=True)
    rgb = np.clip(grey + (rgb - grey) * 1.05, 0, 255)                         # a touch more colour
    rgb = np.clip(128 + (rgb - 128) * 1.04, 0, 255)                          # and contrast
    sx = (2 * half_x / F); sz = (2 * half_z / F)
    dzdy, dzdx = np.gradient(np.maximum(fh, water_y).astype(float), sz, sx)
    nrm = np.stack([-dzdx, np.ones_like(dzdx), -dzdy], -1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    light = np.array([-0.5, 0.75, -0.45]); light /= np.linalg.norm(light)
    flat_light = float(light[1])                     # flat ground faces straight up: it keeps the texture's own colour
    shade = np.clip(1.0 + 0.6 * (np.clip(nrm @ light, 0, 1) - flat_light), 0.75, 1.12)
    rgb = np.clip(rgb * shade[..., None], 0, 255)
    colours = np.round(rgb).astype(np.uint8)

    # ---- numbers for the mod: land, the circle that holds it, the lobby spawn -------------------------------------------------------
    cx_w = -half_x + np.arange(CELLS + 1) * cell_x
    cz_w = -half_z + np.arange(CELLS + 1) * cell_z
    X, Z = np.meshgrid(cx_w, cz_w)
    land = ys > water_y + 25
    gyy, gxx = np.gradient(ys.astype(float), cell_z, cell_x)
    flat = np.hypot(gxx, gyy) < 0.25
    # inland: land whose whole neighbourhood (three squares) is land
    inland = land.copy()
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            inland &= np.roll(np.roll(land, dy, 0), dx, 1)
    inland[:3, :] = inland[-3:, :] = False; inland[:, :3] = inland[:, -3:] = False
    cx, cz = float(X[inland].mean()), float(Z[inland].mean())
    dist = np.hypot(X[inland] - cx, Z[inland] - cz)
    radius = float(np.percentile(dist, 95) * 0.95)
    good = inland & flat
    dd = np.hypot(X - cx, Z - cz); dd[~good] = 1e18
    sj, si = np.unravel_index(np.argmin(dd), dd.shape)
    spawn = (float(X[sj, si]), float(Z[sj, si]))
    print("land %.0f%%, inland centre (%.0f, %.0f), radius %.0f, lobby spawn (%.0f, %.0f) y %d, water y %d, y range %d..%d" % (
        100 * land.mean(), cx, cz, radius, spawn[0], spawn[1], ys[sj, si], water_y, ys.min(), ys.max()))

    # ---- write the header -----------------------------------------------------------------------------------------------------------
    def rows(vals, per, fmt):
        out = []
        for i in range(0, len(vals), per):
            out.append("    " + ",".join(fmt % v for v in vals[i:i + per]) + ",")
        return "\n".join(out)
    with open(OUT, "w") as f:
        f.write("// GENERATED by scripts/make_fortnite_map.py from assets/fortnite/. Do not edit by hand.\n#pragma once\n#include <cstdint>\n\n")
        f.write("namespace royale {\nnamespace fortnite_data {\n\n")
        f.write("inline constexpr int kCells = %d;      // collision squares per side\n" % CELLS)
        f.write("inline constexpr int kSub = %d;        // colour squares per collision square, per side\n" % SUB)
        f.write("inline constexpr float kHalfX = %.2ff;  // the map runs from -kHalfX to +kHalfX in x and -kHalfZ to +kHalfZ in z\n" % half_x)
        f.write("inline constexpr float kHalfZ = %.2ff;\n" % half_z)
        f.write("inline constexpr int kWaterY = %d;       // height of the water surface\n" % water_y)
        f.write("inline constexpr float kCentreX = %.1ff, kCentreZ = %.1ff, kRadius = %.1ff;   // the circle that holds the island's inland\n" % (cx, cz, radius))
        f.write("inline constexpr float kSpawnX = %.1ff, kSpawnZ = %.1ff;   // flat inland ground for the lobby\n\n" % spawn)
        f.write("// Ground height at each collision vertex, row after row (z), (kCells+1)^2 numbers.\n")
        f.write("inline constexpr int16_t kHeights[(kCells + 1) * (kCells + 1)] = {\n%s\n};\n\n" % rows([int(v) for v in ys.ravel()], 24, "%d"))
        f.write("// Colour at each fine vertex (r, g, b), row after row, (kCells*kSub+1)^2 * 3 numbers.\n")
        f.write("inline constexpr uint8_t kColours[(kCells * kSub + 1) * (kCells * kSub + 1) * 3] = {\n%s\n};\n\n" % rows([int(v) for v in colours.ravel()], 36, "%d"))
        f.write("} // namespace fortnite_data\n} // namespace royale\n")
    print("wrote", OUT, os.path.getsize(OUT) // 1024, "KB")
    write_cover(cover_grid(tex))


if __name__ == "__main__":
    main()
