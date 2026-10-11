#!/usr/bin/env python3
"""Draw an exported skin the way the game will: the model in its own standing pose, textured, and once more coloured by the game limb each
vertex follows (to check the bone mapping and the cap's tail). Run after export_skin.py --dump DIR:

  python3 -I tools/skins/preview_skin.py DIR/<id>.json OUT.png

Needs numpy and Pillow. No Blender and no game needed.
"""
import json, math, sys
import numpy as np
from PIL import Image

LIMB_COLOURS = [(120, 120, 120), (200, 120, 60), (160, 90, 40), (60, 120, 220), (90, 160, 240), (140, 200, 255), (220, 60, 60), (240, 110, 110),
                (255, 160, 160), (90, 90, 90), (240, 220, 80), (120, 220, 120), (200, 200, 200), (60, 200, 60), (120, 240, 120), (180, 255, 180),
                (200, 60, 200), (230, 120, 230), (255, 180, 255), (40, 40, 40), (255, 140, 0)]


def decode(a):
    b = np.array(a, np.uint8).reshape(256, 256, 2)
    v = (b[:, :, 0].astype(int) << 8) | b[:, :, 1]
    return np.stack([(v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31], -1) * (255.0 / 31)


def render(m, yaw=0.0, size=600, by_limb=False):
    V = np.array([v[:3] for v in m['verts']], float)
    N = np.array([v[3:6] for v in m['verts']], float) / 127.0
    ST = np.array([v[6:8] for v in m['verts']], float) / 32.0
    B0 = np.array([v[8] for v in m['verts']]); B1 = np.array([v[9] for v in m['verts']]); W0 = np.array([v[10] for v in m['verts']]) / 255.0
    texes = [decode(t) for t in m['textures']]
    cy, sy = math.cos(yaw), math.sin(yaw)
    x = V[:, 0] * cy + V[:, 2] * sy
    z = -V[:, 0] * sy + V[:, 2] * cy
    y = V[:, 1]
    span = (m['top'] - m['floor']) * 1.1
    k = size / span
    X = size / 2 + x * k
    Y = size * 0.5 - (y - (m['top'] + m['floor']) / 2) * k
    L = np.array([0.35, 0.82, 0.45])
    lit = np.clip(0.55 + 0.55 * np.maximum(0, N @ L), 0, 1)
    img = np.zeros((size, size, 3)) + [[[70, 78, 92]]]
    zbuf = np.full((size, size), -1e9)
    for first, count, tfirst, tcount, tex, limb in m['batches']:
        for t in m['tris'][tfirst:tfirst + tcount]:
            a, b, c = [first + i for i in t]
            xs, ys = X[[a, b, c]], Y[[a, b, c]]
            x0, x1 = int(max(0, math.floor(xs.min()))), int(min(size - 1, math.ceil(xs.max())))
            y0, y1 = int(max(0, math.floor(ys.min()))), int(min(size - 1, math.ceil(ys.max())))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
            d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(d) < 1e-9:
                continue
            l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
            l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
            l2 = 1 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any():
                continue
            dz = l0 * z[a] + l1 * z[b] + l2 * z[c]
            sub = zbuf[y0:y1 + 1, x0:x1 + 1]
            win = inside & (dz > sub)
            if not win.any():
                continue
            if by_limb:
                def col_of(i):
                    c0, c1 = np.array(LIMB_COLOURS[B0[i]], float), np.array(LIMB_COLOURS[B1[i]], float)
                    return c0 * W0[i] + c1 * (1 - W0[i])
                col = l0[..., None] * col_of(a) + l1[..., None] * col_of(b) + l2[..., None] * col_of(c)
            else:
                s = l0 * ST[a, 0] + l1 * ST[b, 0] + l2 * ST[c, 0]
                tt = l0 * ST[a, 1] + l1 * ST[b, 1] + l2 * ST[c, 1]
                px = np.clip(s, 0, 255.999).astype(int); py = np.clip(tt, 0, 255.999).astype(int)
                col = texes[tex][py, px]
            sh = (l0 * lit[a] + l1 * lit[b] + l2 * lit[c])[..., None]
            out = img[y0:y1 + 1, x0:x1 + 1]
            out[win] = (col * sh)[win]
            sub[win] = dz[win]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


if __name__ == '__main__':
    m = json.load(open(sys.argv[1]))
    shots = [render(m, 0.0), render(m, 0.7), render(m, math.pi), render(m, 0.0, by_limb=True), render(m, 0.7, by_limb=True), render(m, math.pi, by_limb=True)]
    sheet = Image.new('RGB', (600 * 3, 1200))
    for i, im in enumerate(shots):
        sheet.paste(im, (600 * (i % 3), 600 * (i // 3)))
    sheet.save(sys.argv[2])
    print('saved', sys.argv[2])
