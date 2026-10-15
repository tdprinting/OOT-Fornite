"""Great Hyrule Kingdom, step 1b: turn the cleaned landscape's sampled grids into the ground the map is built on. Numpy only.

    python3 tools/maps/great_hyrule_kingdom/prepare_landscape.py [height.npy colour.npy]

Input: the 256 x 256 height and colour grids that clean.py baked from Tee's GLB (default: /mnt/project-files/landscape-map/).
The model's trees, houses, piers and the odd spike are fused into its surface, so the grid shows a bump under every tree. They are
taken off with a grey-scale opening (a min filter then a max filter over a disc about 460 units across): anything narrower than that
goes, hills and cliffs stay. A light blur then smooths the faceted AI mesh. No ground (sea and the model's cut edges) is -1000.
Output (committed, small): landscape/ground.npy (int16, model units), landscape/colour.npy (uint8 RGB, blurred a little), landscape/trees.npy
(uint8: how much of a tree canopy the model had over each cell, from the bumps the opening took off where the colour is leafy) and
landscape/biome.npy (uint8: 0 no ground, 1 water, 2 grass, 3 sand, 4 snow, 5 red rock, 6 grey rock, by the model's own colours)."""
import os, sys
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as swv

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = '/mnt/project-files/landscape-map'
OPEN_R = 8          # opening radius in grid cells (one cell is about 58 x 61 units of the model at game scale)

def disk(r):
    y, x = np.mgrid[-r:r + 1, -r:r + 1]
    return (x * x + y * y) <= r * r + 0.5

def rank(a, r, fn):
    m = disk(r)
    W = swv(np.pad(a, r, mode='edge'), (2 * r + 1, 2 * r + 1))
    return fn(np.where(m, W, np.inf if fn is np.min else -np.inf), axis=(2, 3))

def blur(a, s):
    r = int(3 * s); k = np.exp(-np.arange(-r, r + 1) ** 2 / (2 * s * s)); k /= k.sum()
    P = np.pad(a, ((r, r), (r, r)) + ((0, 0),) * (a.ndim - 2), mode='edge')
    b = np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 1, P)
    return np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 0, b)

def main():
    hp = sys.argv[1] if len(sys.argv) > 2 else os.path.join(SRC, 'height.npy')
    cp = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SRC, 'colour.npy')
    h = np.load(hp).astype(np.float64); c = np.load(cp).astype(np.float64)
    none = np.isnan(h)
    h[none] = 0.0
    ground = blur(rank(rank(h, OPEN_R, np.min), OPEN_R, np.max), 1.2)
    # A few canopies are wider than the disc (the north east forest is one solid crown 1500 across, and a spiky prop stands on the east
    # ridge): where a much wider opening is lower by more than 150 and the colour is leafy, the wider one is the ground.
    wide = blur(rank(rank(h, 22, np.min), 22, np.max), 1.2)
    cb3 = blur(np.clip(c, 0, 1), 3)
    tall = ((ground - wide) > 150) & (cb3[..., 1] > cb3[..., 0] * 0.95) & (cb3[..., 1] > cb3[..., 2] * 0.9)
    wmask = np.clip(blur(rank(tall.astype(float), 3, np.max), 2.0) * 1.5, 0, 1)
    ground = ground * (1 - wmask) + np.minimum(ground, wide) * wmask
    ground[none] = -1000
    col = np.clip(blur(np.clip(c, 0, 1) * 255, 1.0), 0, 255)
    # canopies: what the opening took off, where the model's colour is leafy (green over red, not too bright)
    bump = h - ground
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    leafy = (g > r * 0.95) & (g > b * 0.9) & (g < 0.75)
    trees = np.maximum(np.clip((bump - 40) / 120, 0, 1) * leafy, np.clip(wmask * 1.4 - 0.2, 0, 1) * (cb3[..., 1] > cb3[..., 0]))
    trees[none] = 0
    # biomes by colour (blurred first so the AI texture's speckle does not matter)
    cb = blur(np.clip(c, 0, 1), 1.5); R, G, B = cb[..., 0], cb[..., 1], cb[..., 2]
    mx = np.maximum(np.maximum(R, G), B); mn = np.minimum(np.minimum(R, G), B); sat = (mx - mn) / np.maximum(mx, 1e-6)
    biome = np.full(h.shape, 6, np.uint8)
    biome[(G >= R * 0.9) & (G > B * 0.85)] = 2
    biome[(R > G * 1.05) & (G > B * 1.1) & (R > 0.45)] = 3
    biome[(R > G * 1.25) & (R > B * 1.4) & (R <= 0.75) & (h > 700)] = 5
    biome[(mx > 0.55) & (sat < 0.22)] = 4
    biome[(B > R * 1.15) & (B >= G * 0.8) & (h < 160)] = 1
    biome[none] = 0
    out = os.path.join(HERE, 'landscape'); os.makedirs(out, exist_ok=True)
    np.save(os.path.join(out, 'ground.npy'), np.rint(ground).astype(np.int16))
    np.save(os.path.join(out, 'colour.npy'), np.rint(col).astype(np.uint8))
    np.save(os.path.join(out, 'trees.npy'), np.rint(trees * 255).astype(np.uint8))
    np.save(os.path.join(out, 'biome.npy'), biome)
    print('biomes (no ground, water, grass, sand, snow, red rock, grey rock):', [int((biome == k).sum()) for k in range(7)], 'tree cells', int((trees > 0.3).sum()))
    print('ground %d..%d, %.0f%% has ground' % (ground[~none].min(), ground[~none].max(), 100 * (~none).mean()))

if __name__ == '__main__':
    main()
