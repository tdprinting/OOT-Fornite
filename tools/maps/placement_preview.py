#!/usr/bin/env python3
"""Draws what royale_placement_preview wrote (server/tools/placement_preview.cpp): the ground shaded by height, the towns, every prop and every chest.
Chests with something beside them are gold, chests out in the open are red. Give one dump for a plain picture, or two (before, after) for a pair.
  python3 tools/maps/placement_preview.py out.png new.txt [old.txt] [--titles "Before" "After"]
Needs numpy and pillow."""
import sys
import numpy as np
from PIL import Image, ImageDraw

SIZE = 900
KINDS = {0: ("rock", (170, 160, 150), 3), 1: ("boulder", (120, 112, 108), 6), 2: ("bush", (70, 140, 60), 2), 3: ("pillar", (210, 190, 120), 3),
         4: ("roof", None, 0), 5: ("platform", (90, 90, 110), 5), 6: ("platform", (90, 90, 110), 5), 7: ("platform", (90, 90, 110), 5)}


def load(path):
    d = {"poi": [], "prop": [], "chest": []}
    with open(path) as f:
        lines = f.read().split("\n")
    i = 0
    while i < len(lines):
        t = lines[i].split()
        i += 1
        if not t:
            continue
        if t[0] == "MAP":
            d["map"] = tuple(map(float, t[1:4]))
        elif t[0] == "GRID":
            x0, z0, step, nx, nz = float(t[1]), float(t[2]), float(t[3]), int(t[4]), int(t[5])
            rows = [list(map(int, lines[i + j].split())) for j in range(nz)]
            i += nz
            d["grid"] = (x0, z0, step, np.array(rows, dtype=np.float32))
        elif t[0] == "POI":
            d["poi"].append(tuple(map(float, t[1:4])))
        elif t[0] == "PROP":
            d["prop"].append((int(t[1]), float(t[2]), float(t[3])))
        elif t[0] == "CHEST":
            d["chest"].append((float(t[1]), float(t[2]), int(t[3]), int(t[4])))
    return d


def render(d, title):
    cx, cz, r = d["map"]
    x0, z0, step, h = d["grid"]
    void = h <= -9000
    hh = np.where(void, np.nan, h)
    lo, hi = np.nanmin(hh), np.nanmax(hh)
    gz, gx = np.gradient(np.nan_to_num(hh, nan=float(lo)), step)
    shade = np.clip(0.5 + (-gx * 0.6 - gz * 0.8) / 4.0, 0.15, 1.0)
    base = (hh - lo) / max(1.0, hi - lo)
    rgb = np.zeros(h.shape + (3,), dtype=np.float32)
    rgb[..., 0] = 120 + 60 * base
    rgb[..., 1] = 150 + 50 * base
    rgb[..., 2] = 105 + 70 * base
    rgb *= shade[..., None]
    rgb[void] = (45, 60, 80)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).resize((SIZE, SIZE), Image.BILINEAR)
    dr = ImageDraw.Draw(img, "RGBA")
    scale = SIZE / (2 * r)

    def px(x, z):
        return ((x - (cx - r)) * scale, (z - (cz - r)) * scale)

    dr.ellipse([*px(cx - r, cz - r), *px(cx + r, cz + r)], outline=(255, 255, 255, 120))
    for x, z, rad in d["poi"]:
        a, b = px(x - rad, z - rad), px(x + rad, z + rad)
        dr.ellipse([*a, *b], outline=(255, 255, 255, 200), fill=(255, 255, 255, 40))
    for k, x, z in d["prop"]:
        name, col, size = KINDS.get(k, ("?", (255, 0, 255), 3))
        if col is None:
            continue
        p = px(x, z)
        s = max(1.0, size * SIZE / 900.0)
        dr.ellipse([p[0] - s, p[1] - s, p[0] + s, p[1] + s], fill=col + (255,))
    for x, z, rar, beside in d["chest"]:
        p = px(x, z)
        col = (255, 205, 40, 255) if beside else (230, 30, 40, 255)
        s = 5
        dr.polygon([(p[0], p[1] - s), (p[0] + s, p[1]), (p[0], p[1] + s), (p[0] - s, p[1])], fill=col, outline=(0, 0, 0, 255))
    n = len(d["chest"])
    open_n = sum(1 for c in d["chest"] if not c[3])
    dr.rectangle([0, 0, SIZE, 26], fill=(0, 0, 0, 170))
    dr.text((8, 7), f"{title}: {n} chests, {open_n} out in the open (red)", fill=(255, 255, 255, 255))
    return img


def main():
    args = [a for a in sys.argv[1:]]
    titles = ["Before", "After"]
    if "--titles" in args:
        i = args.index("--titles")
        titles = args[i + 1:i + 3]
        args = args[:i]
    out, files = args[0], args[1:]
    if len(files) == 2:
        files = [files[1], files[0]]   # given as new old: draw old (before) on the left
    imgs = [render(load(f), t) for f, t in zip(files, titles if len(files) == 2 else ["Chests"])]
    sheet = Image.new("RGB", (SIZE * len(imgs), SIZE))
    for i, im in enumerate(imgs):
        sheet.paste(im, (i * SIZE, 0))
    sheet.save(out)


main()
