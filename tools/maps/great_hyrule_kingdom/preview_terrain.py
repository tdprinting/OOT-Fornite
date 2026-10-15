"""Quick top-down picture of the ground (no Blender): python3 tools/maps/great_hyrule_kingdom/preview_terrain.py out.png"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
from PIL import Image, ImageDraw
import terrain
from layout import *

def main(path, n=512):
    xs = np.linspace(-HALF_X, HALF_X, n); zs = np.linspace(-HALF_Z, HALF_Z, n)
    X, Z = np.meshgrid(xs, zs)
    H = terrain.height(X, Z)
    col, cover = terrain.paint(X, Z, H)
    # hillshade
    gx = np.gradient(H, axis=1); gz = np.gradient(H, axis=0)
    shade = np.clip(0.75 + 0.012 * (gx * 0.6 - gz * 0.8), 0.45, 1.25)[..., None]
    img = np.clip(col * shade, 0, 255).astype(np.uint8)
    water = H < WATER_Y
    depth = np.clip((WATER_Y - H) / 300, 0, 1)[..., None]
    img = np.where(water[..., None], (np.array([70, 170, 190]) * (1 - depth) + np.array([20, 70, 120]) * depth).astype(np.uint8), img)
    im = Image.fromarray(img); d = ImageDraw.Draw(im)
    for name, x, z, r, b, _ in POIS:
        px = (x + HALF_X) / (2 * HALF_X) * n; pz = (z + HALF_Z) / (2 * HALF_Z) * n; rr = r / (2 * HALF_X) * n
        d.ellipse([px - rr, pz - rr, px + rr, pz + rr], outline=(255, 60, 60) if b else (255, 255, 255))
        d.text((px - 20, pz - 5), name, fill=(0, 0, 0))
    im.save(path); print('wrote', path, 'land %.0f%%' % (100 * (~water).mean()))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'terrain.png', int(sys.argv[2]) if len(sys.argv) > 2 else 512)
