"""Quick top-down look at the ground (no Blender): python3 tools/maps/kingdom/preview_terrain.py out.png [size]"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from layout import *
import terrain

def render(path, n=900, heights=None):
    xs = np.linspace(-HALF_X, HALF_X, n); zs = np.linspace(-HALF_Z, HALF_Z, n)
    X, Z = np.meshgrid(xs, zs)
    H = terrain.grid_height(heights, X, Z) if heights is not None else terrain.height(X, Z)
    col, cover = terrain.paint(X, Z, H)
    # hill shade from the north west
    gx = np.gradient(H, axis=1) / (xs[1] - xs[0]); gz = np.gradient(H, axis=0) / (zs[1] - zs[0])
    shade = np.clip(1 + (-gx * 0.6 - gz * 0.6) * 0.9, 0.55, 1.35)[..., None]
    img = col * shade
    water = (H < WATER_Y)[..., None]
    depth = np.clip((WATER_Y - H) / 300, 0, 1)[..., None]
    wc = np.array([70, 150, 170]) * (1 - depth) + np.array([30, 80, 130]) * depth
    img = np.where(water, img * 0.25 + wc * 0.75, img)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    try: font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", max(10, n // 70))
    except Exception: font = ImageFont.load_default()
    for name, px, pz, rad, boss, _ in POIS:
        u = (px + HALF_X) / (2 * HALF_X) * n; v = (pz + HALF_Z) / (2 * HALF_Z) * n
        r = rad / (2 * HALF_X) * n
        d.ellipse([u - r, v - r, u + r, v + r], outline=(255, 255, 255) if not boss else (255, 90, 60))
        d.text((u, v), name, fill=(255, 255, 255), anchor="mm", font=font, stroke_width=2, stroke_fill=(0, 0, 0))
    im.save(path)
    return H

if __name__ == "__main__":
    render(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 900)
