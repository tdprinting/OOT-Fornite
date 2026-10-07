"""Normal and bump maps for Hyrule Kingdom's materials, for the game's surface light system (docs/ITEM_SURFACE_MAPS.md, patches/libultraship/0003).

Eight tileable 64 x 64 classes of material microdetail (rough stone, rock, wood grain, roof tiles, thatch, cobbles, plaster, bark). Like the sword and shield
maps they tile in model space and add relief to the existing colour textures; they are not sculpted copies of them. Same encoding as
scripts/make_item_surface_maps.py: RGBA8 linear data, 128 is exactly flat, OpenGL tangent convention, grayscale height. Run:
    python3 tools/maps/kingdom/surface_maps.py      (writes shared/kingdom_surface_maps.h and assets/maps/hyrule_kingdom/surface_maps/*.png)
"""
import os, struct, zlib
import numpy as np

SIZE = 64
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

# class number (1..8, 0 is none) -> (name, world units per repeat of the 64 texel map, normal strength)
CLASSES = [('stone', 120, 3.0), ('rock', 170, 3.6), ('wood', 100, 2.6), ('roof', 90, 3.0), ('thatch', 64, 2.4), ('cobble', 90, 3.4), ('plaster', 80, 1.6), ('bark', 120, 3.2)]
# which texture (tools/maps/kingdom/textures.py) gets which class
MATERIALS = {
    'castle_stone': 1, 'town_stone': 1, 'moss_stone': 1, 'brick_dark': 1, 'sandstone': 1,
    'rock': 2, 'lava_rock': 2,
    'planks': 3, 'timber': 3,
    'roof_red': 4, 'roof_blue': 4, 'roof_green': 4,
    'thatch': 5, 'hay': 5,
    'cobble': 6,
    'plaster': 7,
    'bark': 8, 'pine': 8,
}

def lattice(rng, n):
    return rng.random((n, n))

def periodic_noise(rng, cells):
    """Smooth value noise on a wrapping lattice, SIZE x SIZE."""
    g = lattice(rng, cells)
    xs = np.arange(SIZE) / SIZE * cells
    i0 = np.floor(xs).astype(int) % cells; i1 = (i0 + 1) % cells; f = xs - np.floor(xs); f = f * f * (3 - 2 * f)
    a = g[np.ix_(i0, i0)]; b = g[np.ix_(i0, i1)]; c = g[np.ix_(i1, i0)]; d = g[np.ix_(i1, i1)]
    fx = f[None, :]; fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def fbm(rng, base, octaves=3):
    h = np.zeros((SIZE, SIZE)); amp = 1.0; tot = 0.0
    for o in range(octaves):
        h += amp * periodic_noise(rng, base * 2 ** o); tot += amp; amp *= 0.5
    return h / tot

def voronoi(rng, cells):
    """Distance to the nearest of cells x cells jittered points on a wrapping grid: rounded stones."""
    pts = [((i + rng.random() * 0.8 + 0.1) / cells, (j + rng.random() * 0.8 + 0.1) / cells) for i in range(cells) for j in range(cells)]
    ys, xs = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    d1 = np.full((SIZE, SIZE), 9.0); d2 = d1.copy()
    for px, py in pts:
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                d = np.hypot(xs - (px + ox), ys - (py + oy))
                swap = d < d1
                d2 = np.where(swap, d1, np.minimum(d2, d)); d1 = np.where(swap, d, d1)
    return d1, d2

def height(name, seed):
    rng = np.random.default_rng(seed)
    ys, xs = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    if name == 'stone':        # rough-hewn faces with fine grain and a few hairline cracks
        h = 0.55 * fbm(rng, 3, 3) + 0.25 * fbm(rng, 8, 2) + 0.12 * fbm(rng, 16, 1)
        d1, d2 = voronoi(rng, 4); h -= 0.25 * np.exp(-((d2 - d1) / 0.018) ** 2)
    elif name == 'rock':       # bigger lumps, deeper clefts
        h = 0.7 * fbm(rng, 2, 4) + 0.2 * fbm(rng, 6, 2)
        d1, d2 = voronoi(rng, 3); h -= 0.45 * np.exp(-((d2 - d1) / 0.03) ** 2)
    elif name == 'wood':       # grain running along x with the odd knot
        warp = 0.06 * fbm(rng, 3, 2)
        h = 0.5 + 0.22 * np.sin(2 * np.pi * (11 * (ys + warp) + 0.5 * fbm(rng, 4, 2))) + 0.1 * fbm(rng, 8, 2)
        h += 0.08 * np.sin(2 * np.pi * (27 * (ys + warp)))
    elif name == 'roof':       # rows of rounded tiles, each row offset
        rows = 4; row = np.floor(ys * rows); fy = ys * rows - row
        fx = (xs * 4 + 0.5 * (row % 2)) % 1.0
        h = 0.6 + 0.3 * (1 - (2 * fx - 1) ** 2) * 0.5 + 0.35 * (fy - 0.5) - 0.15 * np.exp(-((fx) / 0.05) ** 2) + 0.05 * fbm(rng, 8, 2)
    elif name == 'thatch':     # fine straw fibres, slightly slanted
        h = 0.5 + 0.18 * np.sin(2 * np.pi * (24 * (xs + 0.15 * ys) + 2 * fbm(rng, 5, 2))) + 0.1 * np.sin(2 * np.pi * (13 * (xs - 0.1 * ys)))
        h += 0.1 * fbm(rng, 6, 2)
    elif name == 'cobble':     # rounded cobbles with sunken joints
        d1, d2 = voronoi(rng, 4)
        h = np.clip(1.0 - d1 / 0.16, 0, 1) ** 0.6 * 0.8 + 0.1 * fbm(rng, 8, 2)
    elif name == 'plaster':    # a skim of pits and trowel marks
        h = 0.5 + 0.25 * fbm(rng, 5, 3) + 0.1 * fbm(rng, 14, 1)
    else:                      # bark: long vertical furrows
        warp = 0.05 * fbm(rng, 3, 2)
        h = 0.5 + 0.28 * np.sin(2 * np.pi * (7 * (xs + warp) + 1.5 * fbm(rng, 3, 2))) + 0.12 * fbm(rng, 8, 2)
    h = h - h.min(); h = h / max(1e-9, h.max())
    return 0.12 + 0.76 * h        # keep clear of 0 and 1

def encode(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    ln = np.sqrt(dx * dx + dy * dy + 1.0)
    n = np.stack([-dx / ln, -dy / ln, 1.0 / ln], axis=-1)
    normal = np.zeros((SIZE, SIZE, 4), np.uint8)
    normal[..., :3] = np.rint((n * 0.5 + 0.5) * 255).astype(np.uint8); normal[..., 3] = 255
    b = np.rint(h * 255).astype(np.uint8)
    bump = np.stack([b, b, b, np.full_like(b, 255)], axis=-1)
    return normal.reshape(-1), bump.reshape(-1)

def png(path, data):
    def chunk(kind, value): return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    raw = data.reshape(SIZE, SIZE * 4)
    rows = b''.join(b'\0' + raw[y].tobytes() for y in range(SIZE))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))

def main():
    dest = os.path.join(ROOT, 'assets', 'maps', 'hyrule_kingdom', 'surface_maps'); os.makedirs(dest, exist_ok=True)
    out = ['// GENERATED by tools/maps/kingdom/surface_maps.py; do not edit.', '#pragma once', '#include <cstdint>', 'namespace royale { namespace kingdom {',
           'inline constexpr unsigned kSurfaceSize = %d;' % SIZE, 'inline constexpr int kSurfaceClasses = %d;   // class 0 is "none"' % len(CLASSES),
           '// world units per repeat of each class (1 .. kSurfaceClasses)', 'inline constexpr float kSurfaceUnits[] = {' + ','.join(str(c[1]) for c in CLASSES) + '};',
           'alignas(8) inline constexpr uint8_t kSurfaceNormal[][%d] = {' % (SIZE * SIZE * 4)]
    bumps = []
    for k, (name, units, strength) in enumerate(CLASSES):
        h = height(name, 700 + k)
        normal, bump = encode(h, strength)
        png(os.path.join(dest, name + '_normal.png'), normal); png(os.path.join(dest, name + '_bump.png'), bump)
        out.append('{' + ','.join(map(str, normal.tolist())) + '},'); bumps.append(bump)
    out.append('};'); out.append('alignas(8) inline constexpr uint8_t kSurfaceBump[][%d] = {' % (SIZE * SIZE * 4))
    for b in bumps: out.append('{' + ','.join(map(str, b.tolist())) + '},')
    out += ['};', '} }']
    with open(os.path.join(ROOT, 'shared', 'kingdom_surface_maps.h'), 'w') as f: f.write('\n'.join(out) + '\n')
    print('wrote %d classes' % len(CLASSES))

if __name__ == '__main__':
    main()
