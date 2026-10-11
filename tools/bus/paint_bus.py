"""Paints the Moonfall Bus's three textures (used by build_bus.py): the 256x256 bus atlas, the Moon's 256x256 face and its 128x128 rock tile.

Painterly and soft, in the Ocarina of Time and stylised Zelda colours of docs/BATTLE_BUS.md section 2: everything is painted at four times the
size with soft gradients, rim highlights and hand-drawn shapes, then scaled down, so the 256 textures look painted rather than pixel-drawn. Each
region of the atlas is (x, y, w, h) in texels, y from the top. Needs numpy and Pillow (both in the bpy venv).
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ATLAS = 256
SS = 4    # painted at this many times the size

R = {
    "lacquer": (0, 0, 64, 64),
    "lacquer_dark": (64, 0, 32, 32),
    "gold": (96, 0, 32, 32),
    "brass": (128, 0, 32, 32),
    "iron": (160, 0, 32, 32),
    "wood": (192, 0, 64, 32),
    "lacquer_roof": (192, 32, 64, 64),
    "cloth": (64, 32, 32, 32),
    "glass": (96, 32, 32, 32),
    "rope": (128, 32, 16, 32),
    "burlap": (144, 32, 16, 32),
    "stone": (160, 32, 32, 32),
    "crest": (0, 64, 64, 48),
    "triforce": (64, 64, 64, 64),
    "runes": (128, 64, 64, 16),
    "sheikah_eye": (128, 80, 32, 32),
    "lamp": (160, 80, 32, 32),
    "ember": (192, 96, 32, 32),
    "lantern": (224, 96, 32, 32),
    "grille": (0, 112, 32, 32),
    "porthole": (32, 112, 32, 32),
    "leaf": (0, 144, 64, 112),
    "pennant": (64, 128, 64, 48),
    "flame": (128, 112, 32, 64),
    "moon_eye": (160, 128, 64, 64),
    "bronze": (224, 128, 32, 32),
    "teeth": (64, 176, 32, 32),
    "mouth": (96, 176, 32, 32),
    "moon_brow": (128, 176, 32, 32),
    "wood_dark": (160, 192, 32, 32),
    "wheel_wood": (192, 192, 32, 32),
    "leaf_stem": (224, 192, 16, 32),
    "far_body": (64, 208, 64, 48),
    "far_moon": (128, 208, 32, 32),
    "rear": (192, 224, 64, 32),
}


def uv(region, u, v):
    """Atlas UV (Blender's, v up) for a point (u, v) in [0, 1] of a region (v down), inset half a texel so nothing bleeds."""
    x, y, w, h = region
    s = x + 0.5 + (w - 1.0) * min(1.0, max(0.0, u))
    t = y + 0.5 + (h - 1.0) * min(1.0, max(0.0, v))
    return (s / ATLAS, 1.0 - t / ATLAS)


def hexc(h):
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])


LACQUER, LACQUER_DK, LACQUER_HI = hexc("#7E2616"), hexc("#4E140C"), hexc("#C0563A")
GOLD, GOLD_DK, GOLD_HI = hexc("#E0B93A"), hexc("#9A7420"), hexc("#FFF0A0")
BRASS, BRASS_DK, BRASS_HI = hexc("#B98B3C"), hexc("#6E4E1C"), hexc("#F2D58A")
IRON, IRON_DK, IRON_HI = hexc("#55585E"), hexc("#2E3034"), hexc("#9AA0A8")
WOOD, WOOD_DK = hexc("#8E5A2B"), hexc("#5A361A")
CREST = hexc("#B23A2A")
CYAN, CYAN_HI = hexc("#3FD6F2"), hexc("#D8FBFF")
ROCK, ROCK_DK, ROCK_HI = hexc("#7A7A72"), hexc("#3E3C38"), hexc("#B4B2A6")


def noise(w, h, scale, seed, periodic=False):
    """Smooth value noise in [0, 1] (periodic: tiles, with a whole number of cells across)."""
    rng = np.random.default_rng(seed)
    if periodic:
        gw, gh = max(1, round(w / scale)), max(1, round(h / scale))
        scale_x, scale_y = w / gw, h / gh
    else:
        gw, gh = max(2, int(w / scale) + 2), max(2, int(h / scale) + 2)
        scale_x = scale_y = scale
    g = rng.random((gh, gw))
    ys, xs = np.mgrid[0:h, 0:w].astype(float)
    xs, ys = xs / scale_x, ys / scale_y
    x0, y0 = xs.astype(int), ys.astype(int)
    fx, fy = xs - x0, ys - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    x1, y1 = x0 + 1, y0 + 1
    if periodic:
        x0, x1, y0, y1 = x0 % gw, x1 % gw, y0 % gh, y1 % gh
    a, b = g[y0, x0], g[y0, x1]
    c, d = g[y1, x0], g[y1, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(w, h, scale, seed, octaves=4, periodic=False):
    out, amp, tot = np.zeros((h, w)), 1.0, 0.0
    for o in range(octaves):
        out += noise(w, h, scale / (2 ** o), seed + o * 17, periodic) * amp
        tot += amp
        amp *= 0.5
    return out / tot


def lerp(a, b, k):
    k = np.asarray(k)[..., None]
    return a * (1 - k) + b * k


def canvas(w, h, colour):
    img = np.zeros((h, w, 4))
    img[..., :3] = colour
    img[..., 3] = 1.0
    return img


def to_pil(img):
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGBA")


def from_pil(im):
    return np.asarray(im.convert("RGBA")).astype(np.float64) / 255.0


def metal(w, h, base, dk, hi, seed, vertical=True):
    """Brushed metal with a soft highlight band (the GameCube trim look)."""
    ys, xs = np.mgrid[0:h, 0:w]
    t = (ys / (h - 1)) if vertical else (xs / (w - 1))
    band = np.exp(-((t - 0.32) ** 2) / 0.012) * 0.9 + np.exp(-((t - 0.8) ** 2) / 0.02) * 0.25
    shade = 0.75 + 0.25 * np.cos((t - 0.35) * math.pi)
    brush = fbm(w, h, 6, seed, 3)
    c = lerp(dk, base, np.clip(shade, 0, 1))
    c = lerp(c, hi, np.clip(band, 0, 1))
    c = c * (0.94 + 0.12 * brush[..., None])
    img = canvas(w, h, base)
    img[..., :3] = c
    return img


def lacquer(w, h, seed, base=LACQUER, dk=LACQUER_DK, hi=LACQUER_HI):
    ys, xs = np.mgrid[0:h, 0:w]
    n = fbm(w, h, w / 3, seed)
    streak = fbm(w, h, w / 10, seed + 5, 2)
    t = ys / (h - 1)
    c = lerp(base, dk, np.clip(0.25 + 0.5 * t + 0.25 * (n - 0.5), 0, 1))
    c = lerp(c, hi, np.clip((0.35 - t) * 1.2, 0, 1) * 0.55 + np.clip(streak - 0.7, 0, 1) * 0.6)
    img = canvas(w, h, base)
    img[..., :3] = c
    return img


def wood(w, h, seed, base=WOOD, dk=WOOD_DK, boards=4):
    ys, xs = np.mgrid[0:h, 0:w]
    grain = fbm(w, h, max(4, w / 2), seed, 3)
    rings = np.sin((xs / w * 18 + grain * 6) * 1.0) * 0.5 + 0.5
    c = lerp(base, dk, 0.25 + 0.35 * rings * grain)
    bh = h / boards
    edge = np.minimum((ys % bh), bh - (ys % bh))
    c = lerp(c, dk * 0.6, np.clip(1.0 - edge / (SS * 1.2), 0, 1) * 0.85)
    img = canvas(w, h, base)
    img[..., :3] = c
    return img


def draw_triforce(d, cx, cy, size, fill, outline, width):
    """Three triangles, the classic arrangement (point up)."""
    h = size * math.sqrt(3) / 2
    top = (cx, cy - h)
    l, r = (cx - size, cy + h), (cx + size, cy + h)
    def tri(a, b, c):
        d.polygon([a, b, c], fill=fill, outline=outline)
        if width:
            d.line([a, b, c, a], fill=outline, width=width)
    m1, m2, m3 = ((top[0] + l[0]) / 2, (top[1] + l[1]) / 2), ((top[0] + r[0]) / 2, (top[1] + r[1]) / 2), (cx, l[1])
    tri(top, m1, m2)
    tri(m1, l, m3)
    tri(m2, m3, r)


def region_painters():
    P = {}

    def p_lacquer(w, h):
        return lacquer(w, h, 1)
    P["lacquer"] = p_lacquer
    P["lacquer_dark"] = lambda w, h: lacquer(w, h, 2, base=LACQUER_DK, dk=LACQUER_DK * 0.6, hi=LACQUER)

    def p_roof(w, h):
        img = lacquer(w, h, 3)
        ys, xs = np.mgrid[0:h, 0:w]
        r = np.hypot((xs - w / 2) / (w / 2), (ys - h / 2) / (h / 2))
        img[..., :3] = lerp(img[..., :3], LACQUER_HI, np.clip(0.5 - r, 0, 1) * 0.5)
        return img
    P["lacquer_roof"] = p_roof
    P["gold"] = lambda w, h: metal(w, h, GOLD, GOLD_DK, GOLD_HI, 4)
    P["brass"] = lambda w, h: metal(w, h, BRASS, BRASS_DK, BRASS_HI, 5, vertical=False)
    P["iron"] = lambda w, h: metal(w, h, IRON, IRON_DK, IRON_HI, 6)
    P["bronze"] = lambda w, h: metal(w, h, hexc("#A0703A"), hexc("#4E3218"), hexc("#E8C07A"), 7)
    P["wood"] = lambda w, h: wood(w, h, 8)
    P["wood_dark"] = lambda w, h: wood(w, h, 9, base=hexc("#5C3B1F"), dk=hexc("#2E1C0E"), boards=3)
    P["wheel_wood"] = lambda w, h: wood(w, h, 10, base=hexc("#7A4A22"), dk=hexc("#3A2210"), boards=1)

    def p_cloth(w, h):
        ys, xs = np.mgrid[0:h, 0:w]
        n = fbm(w, h, 8, 11, 2)
        c = lerp(hexc("#AD3725"), hexc("#6E1C10"), np.clip(0.2 + 0.3 * n + 0.3 * np.abs(xs / w - 0.5), 0, 1))
        stripe = np.abs(ys / h - 0.5) < 0.08
        c[stripe] = lerp(GOLD, GOLD_HI, 0.3)
        weave = ((xs // SS + ys // SS) % 2) * 0.04
        img = canvas(w, h, 0)
        img[..., :3] = c * (0.97 + weave[..., None])
        return img
    P["cloth"] = p_cloth

    def p_glass(w, h):
        ys, xs = np.mgrid[0:h, 0:w]
        t = ys / h
        c = lerp(hexc("#9FE3F2"), hexc("#2E6E9A"), t)
        shine = np.abs((xs - ys * 0.6) / w - 0.25) < 0.06
        c[shine] = lerp(c[shine], np.array([1, 1, 1.0]), 0.6)
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["glass"] = p_glass

    def p_rope(w, h):
        ys, xs = np.mgrid[0:h, 0:w]
        tw = np.sin((ys / h * 6 + xs / w) * 2 * math.pi) * 0.5 + 0.5
        c = lerp(hexc("#7A5A2A"), hexc("#D8B878"), tw)
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["rope"] = p_rope

    def p_burlap(w, h):
        ys, xs = np.mgrid[0:h, 0:w]
        n = fbm(w, h, 5, 12, 3)
        weave = ((xs // (SS * 2) + ys // (SS * 2)) % 2) * 0.08
        c = lerp(hexc("#C8A46A"), hexc("#7E6036"), 0.3 + 0.4 * n) * (0.95 + weave[..., None])
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["burlap"] = p_burlap

    def p_stone(w, h):
        img = canvas(w, h, 0)
        n = fbm(w, h, 10, 13, 4)
        img[..., :3] = lerp(hexc("#5E5A50"), hexc("#A8A290"), n)
        return img
    P["stone"] = p_stone

    def p_crest(w, h):
        img = lacquer(w, h, 14, base=CREST, dk=hexc("#7A2016"), hi=hexc("#E06A4A"))
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        m = 3 * SS
        d.rounded_rectangle([m, m, w - m, h - m], radius=4 * SS, outline=tuple(int(c * 255) for c in GOLD) + (255,), width=2 * SS)
        draw_triforce(d, w / 2, h / 2 + 2 * SS, h * 0.30, tuple(int(c * 255) for c in GOLD) + (255,), tuple(int(c * 255) for c in GOLD_DK) + (255,), SS)
        return from_pil(im)
    P["crest"] = p_crest

    def p_triforce(w, h):
        img = lacquer(w, h, 15)
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        draw_triforce(d, w / 2, h / 2 + 3 * SS, w * 0.42, tuple(int(c * 255) for c in GOLD) + (255,), tuple(int(c * 255) for c in GOLD_DK) + (255,), SS)
        return from_pil(im)
    P["triforce"] = p_triforce

    def p_runes(w, h):
        img = canvas(w, h, LACQUER_DK * 0.8)
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        col = tuple(int(c * 255) for c in CYAN) + (255,)
        d.line([0, h * 0.5, w, h * 0.5], fill=col, width=SS)
        rng = np.random.default_rng(16)
        x = 3 * SS
        while x < w - 6 * SS:
            g = rng.integers(0, 4)
            cx, cy, s = x + 3 * SS, h / 2, 4.5 * SS
            if g == 0:
                d.ellipse([cx - s, cy - s, cx + s, cy + s], outline=col, width=SS)
                d.ellipse([cx - SS, cy - SS, cx + SS, cy + SS], fill=col)
            elif g == 1:
                d.polygon([(cx, cy - s), (cx + s, cy + s), (cx - s, cy + s)], outline=col)
                d.line([(cx, cy - s), (cx, cy + s)], fill=col, width=SS)
            elif g == 2:
                d.line([(cx - s, cy - s), (cx + s, cy - s), (cx + s, cy + s), (cx - s, cy + s)], fill=col, width=SS)
            else:
                d.line([(cx - s, cy), (cx + s, cy)], fill=col, width=SS)
                d.line([(cx, cy - s), (cx, cy + s)], fill=col, width=SS)
            x += 9 * SS
        out = from_pil(im)
        glow = from_pil(im.filter(ImageFilter.GaussianBlur(2 * SS)))
        out[..., :3] = np.maximum(out[..., :3], glow[..., :3] * 1.2)
        return out
    P["runes"] = p_runes

    def p_sheikah(w, h):
        img = canvas(w, h, hexc("#2A1A10"))
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        col = tuple(int(c * 255) for c in CYAN) + (255,)
        cx, cy = w / 2, h / 2
        d.ellipse([2 * SS, 2 * SS, w - 2 * SS, h - 2 * SS], fill=tuple(int(c * 255) for c in BRASS_DK) + (255,))
        # the Sheikah eye: an eye shape, an iris, three lashes above and a teardrop below
        d.ellipse([cx - 9 * SS, cy - 5 * SS, cx + 9 * SS, cy + 5 * SS], outline=col, width=SS + 1)
        d.ellipse([cx - 3 * SS, cy - 3 * SS, cx + 3 * SS, cy + 3 * SS], fill=col)
        for a in (-0.5, 0, 0.5):
            d.line([(cx + a * 12 * SS, cy - 6 * SS), (cx + a * 14 * SS, cy - 10 * SS)], fill=col, width=SS)
        d.polygon([(cx, cy + 7 * SS), (cx - 2 * SS, cy + 11 * SS), (cx + 2 * SS, cy + 11 * SS)], fill=col)
        out = from_pil(im)
        glow = from_pil(im.filter(ImageFilter.GaussianBlur(1.5 * SS)))
        out[..., :3] = np.maximum(out[..., :3], glow[..., :3])
        return out
    P["sheikah_eye"] = p_sheikah

    def glow_ball(w, h, inner, outer, edge):
        ys, xs = np.mgrid[0:h, 0:w]
        r = np.hypot(xs / (w - 1) - 0.5, ys / (h - 1) - 0.5) * 2
        c = lerp(inner, outer, np.clip(r, 0, 1))
        c = lerp(c, edge, np.clip((r - 0.8) / 0.2, 0, 1))
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["lamp"] = lambda w, h: glow_ball(w, h, CYAN_HI, CYAN, hexc("#1C7A9A"))
    P["ember"] = lambda w, h: glow_ball(w, h, hexc("#FFF6B0"), hexc("#FF8A1E"), hexc("#7A1E08"))

    def p_lantern(w, h):
        ys, xs = np.mgrid[0:h, 0:w]
        c = lerp(hexc("#FFF2C0"), hexc("#FFB04A"), np.abs(xs / w - 0.5) * 2)
        ribs = (np.abs(((xs / w) * 6) % 1 - 0.5) < 0.05)
        c[ribs] = c[ribs] * 0.75
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["lantern"] = p_lantern

    def p_grille(w, h):
        img = metal(w, h, BRASS, BRASS_DK, BRASS_HI, 17)
        ys, xs = np.mgrid[0:h, 0:w]
        slot = ((ys // (SS * 3)) % 2 == 1) & (xs > 3 * SS) & (xs < w - 3 * SS) & (ys > 3 * SS) & (ys < h - 3 * SS)
        img[slot, :3] = hexc("#24160C")
        return img
    P["grille"] = p_grille

    def p_porthole(w, h):
        img = lacquer(w, h, 18)
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        d.ellipse([2 * SS, 2 * SS, w - 2 * SS, h - 2 * SS], fill=tuple(int(c * 255) for c in BRASS) + (255,))
        d.ellipse([7 * SS, 7 * SS, w - 7 * SS, h - 7 * SS], fill=(28, 52, 70, 255))
        d.arc([10 * SS, 10 * SS, w - 10 * SS, h - 10 * SS], 200, 250, fill=(190, 230, 240, 255), width=SS)
        return from_pil(im)
    P["porthole"] = p_porthole

    def p_leaf(w, h):
        """A Deku leaf (Wind Waker): bright green blade, pale midrib and side veins, darker edges. u across, v from tip (0) to root (1)."""
        ys, xs = np.mgrid[0:h, 0:w]
        u, v = xs / (w - 1) * 2 - 1, ys / (h - 1)
        n = fbm(w, h, w / 4, 19, 3)
        c = lerp(hexc("#7CC24E"), hexc("#3E7A2C"), np.clip(np.abs(u) ** 1.5 * 0.8 + 0.2 * n, 0, 1))
        mid = np.abs(u) < 0.06
        c[mid] = hexc("#D8EE9A")
        vein = np.abs(((v * 7 + np.abs(u) * 2.2) % 1.0) - 0.5) < 0.05
        vein &= np.abs(u) > 0.06
        c[vein] = lerp(c[vein], hexc("#C8E68A"), 0.7)
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["leaf"] = p_leaf
    P["leaf_stem"] = lambda w, h: canvas(w, h, hexc("#5E8A2E"))

    def p_pennant(w, h):
        img = lacquer(w, h, 20, base=CREST, dk=hexc("#7A2016"), hi=hexc("#E06A4A"))
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 4 * SS, h], fill=tuple(int(c * 255) for c in GOLD) + (255,))
        draw_triforce(d, w * 0.3, h / 2 + 2 * SS, h * 0.26, tuple(int(c * 255) for c in GOLD) + (255,), tuple(int(c * 255) for c in GOLD_DK) + (255,), SS)
        return from_pil(im)
    P["pennant"] = p_pennant

    def p_flame(w, h):
        """The flame: white-yellow core, orange, red tips; alpha cut to the flame's shape."""
        ys, xs = np.mgrid[0:h, 0:w]
        u, v = xs / (w - 1) * 2 - 1, 1 - ys / (h - 1)     # v 0 at the base, 1 at the tip
        width = 0.95 * (1 - v) ** 0.7 * (0.6 + 0.4 * np.sin(v * math.pi))
        inside = np.abs(u) < width
        core = np.clip(1 - np.abs(u) / np.maximum(width, 1e-3), 0, 1) * (1 - v)
        c = lerp(hexc("#E8401A"), hexc("#FF9A2A"), np.clip(core * 1.8, 0, 1))
        c = lerp(c, hexc("#FFF6C0"), np.clip(core * 2 - 0.9, 0, 1))
        img = canvas(w, h, 0)
        img[..., :3] = c
        img[..., 3] = inside.astype(float)
        return img
    P["flame"] = p_flame

    def p_moon_eye(w, h):
        """The Moon's bulging eye: yellow centre, orange, a red rim, bloodshot veins and a tiny black pupil."""
        ys, xs = np.mgrid[0:h, 0:w]
        r = np.hypot(xs / (w - 1) - 0.5, ys / (h - 1) - 0.5) * 2
        c = lerp(hexc("#FFF08A"), hexc("#FFB21E"), np.clip(r / 0.35, 0, 1))
        c = lerp(c, hexc("#F06A12"), np.clip((r - 0.35) / 0.3, 0, 1))
        c = lerp(c, hexc("#B01E10"), np.clip((r - 0.62) / 0.3, 0, 1))
        img = canvas(w, h, 0)
        img[..., :3] = c
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        rng = np.random.default_rng(21)
        for k in range(14):
            a = rng.uniform(0, 2 * math.pi)
            pts = []
            rr = 0.95
            ang = a
            while rr > 0.42:
                pts.append((w / 2 + math.cos(ang) * rr * w / 2, h / 2 + math.sin(ang) * rr * h / 2))
                rr -= 0.07
                ang += rng.uniform(-0.18, 0.18)
            d.line(pts, fill=(150, 10, 8, 255), width=SS)
        p = 0.09 * w
        d.ellipse([w / 2 - p, h / 2 - p * 1.25, w / 2 + p, h / 2 + p * 1.25], fill=(12, 6, 4, 255))
        d.ellipse([w / 2 - p * 2.2, h / 2 - p * 2.6, w / 2 - p * 1.2, h / 2 - p * 1.6], fill=(255, 255, 230, 255))
        return from_pil(im)
    P["moon_eye"] = p_moon_eye

    def p_teeth(w, h):
        """A clean square tooth: ivory, a soft shade at the root and the edges."""
        ys, xs = np.mgrid[0:h, 0:w]
        u, v = xs / (w - 1), ys / (h - 1)
        edge = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))
        c = lerp(hexc("#B8AE94"), hexc("#F4EEDA"), np.clip(edge * 6, 0, 1))
        img = canvas(w, h, 0)
        img[..., :3] = c
        return img
    P["teeth"] = p_teeth
    P["mouth"] = lambda w, h: lacquer(w, h, 22, base=hexc("#1A0A08"), dk=hexc("#080404"), hi=hexc("#3A1410"))
    P["moon_brow"] = lambda w, h: canvas(w, h, hexc("#2A2622")) * np.array([1, 1, 1, 1.0]) + 0
    P["far_body"] = lambda w, h: lacquer(w, h, 23)

    def p_far_moon(w, h):
        img = canvas(w, h, ROCK)
        img[..., :3] = lerp(ROCK, ROCK_HI, fbm(w, h, 8, 24, 3) * 0.6)
        return img
    P["far_moon"] = p_far_moon

    def p_rear(w, h):
        img = lacquer(w, h, 25)
        im = to_pil(img)
        d = ImageDraw.Draw(im)
        cx = w / 2
        d.rounded_rectangle([cx - 14 * SS, 6 * SS, cx + 14 * SS, h - 6 * SS], radius=10 * SS, fill=tuple(int(c * 255) for c in BRASS) + (255,))
        d.rounded_rectangle([cx - 11 * SS, 9 * SS, cx + 11 * SS, h - 9 * SS], radius=8 * SS, fill=(60, 130, 170, 255))
        d.line([(cx - 6 * SS, 12 * SS), (cx - 2 * SS, 20 * SS)], fill=(200, 240, 250, 255), width=SS)
        return from_pil(im)
    P["rear"] = p_rear
    return P


def paint_atlas():
    big = np.zeros((ATLAS * SS, ATLAS * SS, 4))
    big[..., 3] = 1.0
    for name, painter in region_painters().items():
        x, y, w, h = R[name]
        img = painter(w * SS, h * SS)
        big[y * SS:(y + h) * SS, x * SS:(x + w) * SS] = img
    im = to_pil(big).resize((ATLAS, ATLAS), Image.LANCZOS)
    return im


def rock_tile(size, seed):
    """Grey cratered rock that tiles (the Moon's back and sides)."""
    S = size * SS
    ys, xs = np.mgrid[0:S, 0:S]
    n = np.zeros((S, S))
    rng = np.random.default_rng(seed)
    base = fbm(S, S, S / 6, seed, 5, periodic=True)
    c = lerp(ROCK_DK, ROCK_HI, np.clip(base * 1.5 - 0.3, 0, 1))
    shade = np.zeros((S, S))
    for _ in range(18):
        cx, cy, r = rng.uniform(0, S), rng.uniform(0, S), rng.uniform(S * 0.03, S * 0.12)
        for ox in (-S, 0, S):
            for oy in (-S, 0, S):
                d = np.hypot(xs - cx - ox, ys - cy - oy) / r
                shade += np.where(d < 1, -(1 - d * d) * 0.28 + np.clip((xs - cx - ox - (ys - cy - oy)) / r, -1, 1) * 0.08, 0)
                shade += np.where((d >= 1) & (d < 1.3), 0.06 * np.sin(np.clip((d - 1) / 0.3, 0, 1) * math.pi), 0)
    c = c * (1 + shade[..., None] * 0.6)
    img = canvas(S, S, 0)
    img[..., :3] = c
    return to_pil(img).resize((size, size), Image.LANCZOS)


def moon_face(size=256):
    """The face, painted over rock, laid on the front of the Moon by a planar projection (build_bus.face_uv: the face's (u, v) in [-0.8, 0.8]
    maps to [0, 1]). Brows, sockets, bags, the crease, nostrils, creases, cracks round the grin and the lips; the eyes and teeth are modelled."""
    S = size * SS
    tile = rock_tile(128, 41).resize((S // 2, S // 2), Image.BICUBIC)
    big = Image.new("RGBA", (S, S))
    for ox in (0, S // 2):
        for oy in (0, S // 2):
            big.paste(tile, (ox, oy))
    rock = from_pil(big)
    img = rock.copy()
    im = to_pil(img)
    ov = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)

    def fp(u, v):     # face coordinates to pixels (u to the viewer's right, v up)
        return (S * (0.5 + u * 0.5), S * (0.5 - v * 0.5))

    # deep socket shadows (the eyeballs sit in them)
    for cu, cv in ((-0.31, 0.24), (0.31, 0.24)):
        x, y = fp(cu, cv)
        r = 0.19 * 0.5 * S
        # sagging bags under them
        bx, by = fp(cu, cv - 0.17)
        d.arc([bx - r, by - r * 0.45, bx + r, by + r * 0.45], 10, 170, fill=(40, 36, 32, 230), width=int(SS * 3))
        d.arc([bx - r * 0.9, by - r * 0.1, bx + r * 0.9, by + r * 0.6], 20, 160, fill=(46, 42, 38, 180), width=int(SS * 2))
    # thick dark brows slanted hard inward
    for sgn in (-1, 1):
        pts = [fp(sgn * 0.56, 0.50), fp(sgn * 0.40, 0.49), fp(sgn * 0.24, 0.42), fp(sgn * 0.10, 0.33)]
        lower = [fp(sgn * 0.09, 0.26), fp(sgn * 0.24, 0.34), fp(sgn * 0.40, 0.40), fp(sgn * 0.57, 0.42)]
        d.polygon(pts + lower, fill=(34, 30, 27, 255))
    # the crease between the eyes, the nostrils
    d.line([fp(0, 0.34), fp(0.0, 0.13)], fill=(36, 32, 28, 255), width=int(SS * 3))
    d.line([fp(-0.03, 0.33), fp(-0.06, 0.18)], fill=(46, 42, 38, 200), width=int(SS * 2))
    d.line([fp(0.03, 0.33), fp(0.06, 0.18)], fill=(46, 42, 38, 200), width=int(SS * 2))
    for sgn in (-1, 1):
        x, y = fp(sgn * 0.075, 0.03)
        r = 0.03 * 0.5 * S
        d.ellipse([x - r * 1.3, y - r * 0.8, x + r * 1.3, y + r * 0.8], fill=(16, 12, 10, 255))
    # creases from the nostrils down past the mouth's corners
    for sgn in (-1, 1):
        pts = [fp(sgn * (0.12 + 0.50 * k / 9), 0.0 - 0.42 * k / 9 - 0.1 * math.sin(k / 9 * math.pi)) for k in range(10)]
        d.line(pts, fill=(38, 34, 30, 230), width=int(SS * 3))
    # the grin: black between the lips, the lips a darker rim
    top = [fp(u, -0.30 + 0.25 * u * u + 0.15 * math.sqrt(max(0.0, 1 - (u / 0.56) ** 2))) for u in np.linspace(-0.56, 0.56, 24)]
    bot = [fp(u, -0.30 + 0.25 * u * u - 0.15 * math.sqrt(max(0.0, 1 - (u / 0.56) ** 2))) for u in np.linspace(0.56, -0.56, 24)]
    d.polygon(top + bot, fill=(14, 8, 6, 255))
    d.line(top, fill=(48, 40, 34, 255), width=int(SS * 4))
    d.line(bot + [bot[0]], fill=(48, 40, 34, 255), width=int(SS * 4))
    # cracks radiating from the grin
    rng = np.random.default_rng(33)
    for sgn in (-1, 1):
        for k in range(2):
            u, v = sgn * 0.5, -0.40 - 0.03 * k
            pts = [fp(u, v)]
            a = math.atan2(-1.0, sgn * (0.5 + 0.6 * k))
            for _ in range(5):
                u += math.cos(a) * 0.035
                v += math.sin(a) * 0.035
                a += rng.uniform(-0.35, 0.35)
                pts.append(fp(u, v))
            d.line(pts, fill=(36, 32, 28, 200), width=int(SS * 1.5))
    ov = ov.filter(ImageFilter.GaussianBlur(SS * 0.8))
    im = Image.alpha_composite(im, ov)
    # a soft top light: lighter brow tops, darker under the chin
    arr = from_pil(im)
    ys, xs = np.mgrid[0:S, 0:S]
    r = np.hypot(xs / S - 0.5, ys / S - 0.5) * 2
    arr[..., :3] *= (1.0 + 0.06 * np.clip(1 - r * 1.4, 0, 1) - 0.10 * np.clip(ys / S - 0.5, 0, 1))[..., None]
    return to_pil(arr).resize((size, size), Image.LANCZOS)


def paint_all(out_dir):
    """Paint all three, save them as PNG beside the .blend and load them into Blender as packed images."""
    import bpy
    os.makedirs(out_dir, exist_ok=True)
    files = {"bus_atlas": paint_atlas(), "moon_face": moon_face(256), "moon_rock": rock_tile(128, 41)}
    images = {}
    for name, im in files.items():
        path = os.path.join(out_dir, name + ".png")
        im.save(path)
        img = bpy.data.images.load(path)
        img.name = name
        img.pack()
        images[name] = img
    return images


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "bus")
    os.makedirs(out, exist_ok=True)
    paint_atlas().save(os.path.join(out, "bus_atlas.png"))
    moon_face(256).save(os.path.join(out, "moon_face.png"))
    rock_tile(128, 41).save(os.path.join(out, "moon_rock.png"))
