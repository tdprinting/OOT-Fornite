"""Paint the hireable allies' textures: a 256 x 256 body atlas of 32 x 32 material cells, and a 128 x 64 face (normal, blink, fierce).
Used by tools/allies/build_allies.py. Needs numpy and Pillow (both come with the bpy wheel's environment).

Everything is painted here from code: soft painterly colour, no lighting baked in (the game lights the vertices). Each cell is tileable left to
right, because tubes wrap their texture once round. The styles follow the art notes and the reference pictures in art-style/npc-refs: woven
cloth with frayed hems, fur strands, rough rock, worn iron and gold, Zora scales, leather and wood grain.
"""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ATLAS = 256
CELL = 32
FACE_W, FACE_H = 128, 64


def vnoise(w, h, gx, gy, seed):
    """Smooth value noise in -1..1 that wraps left to right (gx cells across, gy cells down)."""
    rng = np.random.default_rng(seed)
    g = rng.uniform(-1, 1, (gy + 1, gx))
    xs = np.arange(w) / w * gx
    ys = np.arange(h) / h * gy
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    x1 = (x0 + 1) % gx
    x0 = x0 % gx
    y1 = np.minimum(y0 + 1, gy)
    a = g[y0][:, x0] * (1 - fx) + g[y0][:, x1] * fx
    b = g[y1][:, x0] * (1 - fx) + g[y1][:, x1] * fx
    return a * (1 - fy[:, None]) + b * fy[:, None]


def fbm(w, h, seed, base=(4, 4), octaves=3):
    out = np.zeros((h, w))
    amp, tot = 1.0, 0.0
    gx, gy = base
    for o in range(octaves):
        out += vnoise(w, h, gx, gy, seed + o * 17) * amp
        tot += amp
        amp *= .5
        gx *= 2
        gy *= 2
    return out / tot


def _uv(n=CELL):
    u = (np.arange(n) + .5) / n
    return np.meshgrid(u, u)        # u across, v down (row 0 is the top of the cell = the start of a tube)


def shade(base, k):
    return np.clip(np.asarray(base)[None, None, :] * k[..., None], 0, 1)


def style_skin(base, seed, **_):
    k = 1 + .03 * fbm(CELL, CELL, seed, (3, 3))
    return shade(base, k)


def style_cloth(base, seed, hem=False, **_):
    u, v = _uv()
    weave = .025 * np.sin(u * CELL * math.pi) * np.sin(v * CELL * math.pi)
    k = 1 + .07 * fbm(CELL, CELL, seed, (4, 4)) + weave
    k *= 1 - .08 * (np.abs(v - .5) * 2) ** 3                       # a little darker in the folds at both ends
    if hem:
        edge = .82 + .06 * vnoise(CELL, CELL, 16, 1, seed + 3)
        k = np.where(v > edge, k * .72, k)                          # the frayed, dirtier hem
        k = np.where((v > edge - .05) & (v <= edge), k * .9, k)     # a stitch line above it
    return shade(base, k)


def style_fur(base, seed, **_):
    u, v = _uv()
    strands = vnoise(CELL, CELL, 18, 2, seed) * .14 + vnoise(CELL, CELL, 32, 3, seed + 1) * .07
    k = .9 + strands + .12 * v                                       # the tips (far end) catch more light
    return shade(base, k)


def style_rock(base, seed, **_):
    rng = np.random.default_rng(seed)
    pts = rng.uniform(0, 1, (9, 2))
    u, v = _uv()
    d = []
    for px, py in pts:
        dx = np.abs(u - px)
        dx = np.minimum(dx, 1 - dx)
        d.append(np.hypot(dx, v - py))
    d = np.sort(np.stack(d), axis=0)
    crack = np.clip((d[1] - d[0]) / .05, 0, 1)
    k = (.72 + .28 * crack) * (1 + .12 * fbm(CELL, CELL, seed, (4, 4))) * (1 - .25 * d[0])
    return shade(base, k)


def style_metal(base, seed, **_):
    u, v = _uv()
    band = .82 + .35 * np.exp(-((v - .32) / .12) ** 2) - .18 * np.exp(-((v - .78) / .1) ** 2)
    scratch = .06 * vnoise(CELL, CELL, 24, 1, seed)
    rust = np.clip(fbm(CELL, CELL, seed + 5, (3, 3)) * 2 - .6, 0, 1)
    k = band + scratch - .15 * rust
    return shade(base, k)


def style_gold(base, seed, **_):
    u, v = _uv()
    k = .85 + .4 * np.exp(-((v - .3) / .14) ** 2) - .12 * np.exp(-((v - .8) / .12) ** 2) + .04 * vnoise(CELL, CELL, 8, 2, seed)
    return shade(base, k)


def style_scales(base, seed, **_):
    u, v = _uv()
    rows, cols = 6, 8
    y = v * rows
    x = u * cols + .5 * (np.floor(y) % 2)
    fx, fy = x - np.floor(x) - .5, y - np.floor(y)
    edge = np.clip(np.hypot(fx * 1.2, fy - .1) * 1.6, 0, 1) ** 4
    k = 1 + .04 * fbm(CELL, CELL, seed, (3, 3)) - .07 * edge + .05 * (1 - v)
    return shade(base, k)


def style_fin(base, seed, **_):
    u, v = _uv()
    veins = .1 * np.abs(np.sin(u * math.pi * 6 + v * 1.5)) ** 6
    k = 1 + .14 * (1 - v) - veins + .03 * vnoise(CELL, CELL, 6, 3, seed)
    return shade(base, k)


def style_wood(base, seed, **_):
    u, v = _uv()
    grain = np.sin((u * 6 + vnoise(CELL, CELL, 4, 2, seed) * .6) * math.pi * 2)
    k = .92 + .08 * grain + .05 * vnoise(CELL, CELL, 16, 2, seed + 2)
    return shade(base, k)


def style_leather(base, seed, **_):
    u, v = _uv()
    k = 1 + .1 * fbm(CELL, CELL, seed, (5, 5)) - .1 * np.clip(vnoise(CELL, CELL, 12, 4, seed + 4) * 3 - 2.4, 0, 1)
    k *= 1 + .08 * np.exp(-((v - .15) / .1) ** 2)
    return shade(base, k)


def style_flat(base, seed, **_):
    return shade(base, np.ones((CELL, CELL)))


def style_torso(base, seed, belly=None, navel=False, **_):
    """A Goron's or Zora's body: the front (u = .75 on a tube going up) is a lighter belly, with a navel for a Goron."""
    u, v = _uv()
    du = np.minimum(np.abs(u - .75), 1 - np.abs(u - .75))
    front = np.exp(-(du / .16) ** 2 - ((v - .55) / .45) ** 2)
    col = shade(base, 1 + .035 * fbm(CELL, CELL, seed, (3, 3)))
    if belly is not None:
        col = col * (1 - front[..., None]) + np.asarray(belly)[None, None, :] * front[..., None]
    if navel:
        n = np.exp(-((du / .02) ** 2 + ((v - .78) / .035) ** 2))
        col = col * (1 - .45 * n[..., None])
    return np.clip(col, 0, 1)


STYLES = dict(skin=style_skin, cloth=style_cloth, fur=style_fur, rock=style_rock, metal=style_metal, gold=style_gold, scales=style_scales,
              fin=style_fin, wood=style_wood, leather=style_leather, flat=style_flat, torso=style_torso)


class Atlas:
    """Hands out 32 x 32 cells: each named material is painted once and every part using it maps its own 0..1 UVs into the cell."""
    def __init__(self, materials):
        self.materials = materials      # name -> (style, colour, options)
        self.index = {}

    def rect(self, name):
        assert name in self.materials, 'no material ' + name
        if name not in self.index:
            self.index[name] = len(self.index)
        i = self.index[name]
        per = ATLAS // CELL
        assert i < per * per, 'atlas full'
        return (i % per) * CELL, (i // per) * CELL

    def uv(self, name, u, v):
        """A 0..1 UV inside material `name`'s cell, to a 0..1 atlas UV (Blender's v goes up). Kept 1.5 texels in from the cell's edge."""
        x0, y0 = self.rect(name)
        inset = 1.5
        px = x0 + inset + u * (CELL - 2 * inset)
        py = y0 + inset + (1 - v) * (CELL - 2 * inset)
        return px / ATLAS, 1 - py / ATLAS

    def paint(self, seed=7):
        img = np.zeros((ATLAS, ATLAS, 3))
        for name, i in self.index.items():
            style, col, opt = self.materials[name]
            x0, y0 = self.rect(name)
            img[y0:y0 + CELL, x0:x0 + CELL] = STYLES[style](col, seed + i * 31, **opt)
        return img       # rows top first, 0..1


# ---- faces -----------------------------------------------------------------------------------------------------------------------
SS = 4     # faces are drawn four times larger and shrunk, for smooth edges


def _c(col, a=255):
    return tuple(int(round(max(0, min(1, c)) * 255)) for c in col) + (a,)


class Face:
    def __init__(self, skin):
        self.img = Image.new('RGBA', (FACE_W * SS, FACE_H * SS), _c(skin))
        self.d = ImageDraw.Draw(self.img)

    def ell(self, cx, cy, rx, ry, col, a=255):
        self.d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=_c(col, a))

    def poly(self, pts, col, a=255):
        self.d.polygon([(x * SS, y * SS) for x, y in pts], fill=_c(col, a))

    def line(self, pts, col, w):
        self.d.line([(x * SS, y * SS) for x, y in pts], fill=_c(col), width=max(1, int(w * SS)), joint='curve')
        for x, y in (pts[0], pts[-1]):
            self.ell(x, y, w * .5, w * .5, col)

    def soft(self, cx, cy, rx, ry, col, a):
        """A soft round blush."""
        layer = Image.new('RGBA', self.img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=_c(col, a))
        layer = layer.filter(ImageFilter.GaussianBlur(rx * SS * .45))
        self.img.alpha_composite(layer)

    def result(self):
        im = self.img.resize((FACE_W, FACE_H), Image.LANCZOS)
        return np.asarray(im, float)[:, :, :3] / 255.0


def arc(cx, cy, rx, ry, a0, a1, n=12):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def almond(cx, cy, w, h, tilt=0.0, n=16):
    """An eye outline: a lens shape, `tilt` lifts the outer corner (+ for the right-hand eye)."""
    top = [(cx - w + 2 * w * i / n, cy - h * math.sin(math.pi * i / n) - tilt * (i / n - .5)) for i in range(n + 1)]
    bot = [(cx + w - 2 * w * i / n, cy + h * .75 * math.sin(math.pi * i / n) - tilt * ((n - i) / n - .5)) for i in range(n + 1)]
    return top + bot


def paint_face(kind, expr, pal):
    """The face texture: 128 x 64, the middle of the head's front. x = 64 is the centre line; eyes sit around y = 26, the mouth near y = 48."""
    c = pal
    f = Face(c['skin'])
    white = (.97, .97, .95)
    for side in (-1, 1):
        ex = 64 + side * (24 if kind != 'goron' else 26)
        if kind == 'kokiri':      # big round OoT-artwork eyes, blue, with a thick upper lid and rosy cheeks
            f.soft(64 + side * 40, 42, 9, 6, (.95, .45, .40), 120)
            if expr == 'blink':
                f.line(arc(ex, 26, 11, 5, 200, 340), c['lid'], 2.4)
            else:
                ey = 27 if expr != 'fierce' else 28
                h = 12 if expr != 'fierce' else 8
                f.poly(almond(ex, ey, 11, h, side * 2.0), white)
                f.ell(ex - side * 1.5, ey + 1, 7.5, min(9, h), (.20, .46, .86))
                f.ell(ex - side * 1.5, ey + 1.5, 4.6, min(5.6, h * .6), (.06, .10, .25))
                f.ell(ex - side * 1.5 - 2.5, ey - 3, 2.2, 2.2, white)
                f.line([p for p in almond(ex, ey, 11, h, side * 2.0)[:17]], c['lid'], 2.6)
            by = 11 if expr != 'fierce' else 15
            f.line([(ex - 10, by + (3 if (expr == 'fierce' and side < 0) else 0) + (0 if side < 0 else 2)),
                    (ex, by - 2 + (2 if expr == 'fierce' else 0)),
                    (ex + 10, by + (3 if (expr == 'fierce' and side > 0) else 0) + (2 if side < 0 else 0))], c['hair'], 2.6)
        elif kind == 'goron':     # small purple eyes under a heavy brow, white war-paint dashes on the cheek
            ey = 28
            if expr == 'blink':
                f.line(arc(ex, ey - 2, 9, 4, 20, 160), c['brow'], 2.2)
            else:
                h = 7 if expr != 'fierce' else 5
                f.poly(almond(ex, ey, 9, h, -side * 1.5), white)
                f.ell(ex - side * .5, ey + .5, 4.6, min(5.2, h), (.45, .20, .72))
                f.ell(ex - side * .5, ey + .8, 2.4, 2.6, (.10, .04, .16))
                f.ell(ex - side * .5 - 1.6, ey - 1.6, 1.3, 1.3, white)
            if side > 0:
                for k in range(2):
                    f.line([(ex + 4, 38 + k * 3.4), (ex + 11, 37.5 + k * 3.4)], (.98, .97, .92), 2.0)
        elif kind == 'zora':      # large purple almond eyes with dark lashes
            ey = 27
            if expr == 'blink':
                f.line(arc(ex, ey, 12, 4, 20, 160), c['lid'], 2.2)
            else:
                h = 9 if expr != 'fierce' else 6
                pts = almond(ex, ey, 12, h, side * 4.0)
                f.poly(pts, white)
                f.ell(ex - side * 1.0, ey + .5, 6.4, min(7, h), (.50, .32, .78))
                f.ell(ex - side * 1.0, ey + .8, 3.6, min(4.2, h * .6), (.12, .06, .22))
                f.ell(ex - side * 1.0 - 2.2, ey - 2.4, 1.8, 1.8, white)
                f.line(pts[:17], c['lid'], 2.4)
                f.line([pts[16], (pts[16][0] + side * 4, pts[16][1] - 3)], c['lid'], 1.8)        # the lash flick
            f.soft(64 + side * 36, 40, 8, 5, (.75, .55, .85), 80)
        else:                     # gerudo: golden eyes, dark liner with a wing, thick red brows
            ey = 27
            if expr == 'blink':
                f.line(arc(ex, ey, 12, 4, 20, 160), c['lid'], 2.6)
            else:
                h = 7.5 if expr != 'fierce' else 5.5
                pts = almond(ex, ey, 12, h, side * 4.5)
                f.poly(pts, white)
                f.ell(ex - side * 1.5, ey + .4, 5.2, min(6, h), (.86, .62, .10))
                f.ell(ex - side * 1.5, ey + .6, 2.6, min(3.2, h * .55), (.12, .05, .02))
                f.ell(ex - side * 1.5 - 2, ey - 2, 1.5, 1.5, white)
                f.line(pts[:17], c['lid'], 2.8)
                f.line([pts[16], (pts[16][0] + side * 6, pts[16][1] - 4)], c['lid'], 2.2)
            by = 14 if expr != 'fierce' else 17
            f.line([(ex - side * 11, by + (2 if expr != 'fierce' else -2)), (ex + side * 2, by - 2), (ex + side * 12, by + (1 if expr != 'fierce' else 4))],
                   c['red2'], 2.8)
    # mouth
    mx, my = 64, 48
    if kind == 'kokiri':
        if expr == 'fierce':
            f.line([(mx - 7, my + 1), (mx, my - 1), (mx + 7, my + 1)], (.55, .22, .18), 1.8)
        else:
            f.line(arc(mx, my - 4, 8, 5, 30, 150), (.55, .22, .18), 1.8)
    elif kind == 'goron':     # a broad grin full of teeth, as in the remake
        if expr == 'blink':
            f.line(arc(mx, my - 8, 16, 7, 25, 155), c['brow'], 2.2)
        else:
            open_ = 7 if expr != 'fierce' else 5
            top = arc(mx, my - 9, 19, 6, 20, 160)
            bot = arc(mx, my - 9, 17, 6 + open_, 160, 20)
            f.poly(top + bot, (.30, .08, .08))
            f.poly(arc(mx, my - 9, 18, 6.2, 22, 158) + arc(mx, my - 9, 17, 9.5, 158, 22), (.98, .96, .90))
            for k in range(-3, 4):
                f.line([(mx + k * 4.6, my - 3.5), (mx + k * 4.6, my - 1.2)], (.75, .70, .62), .7)
    elif kind == 'zora':
        f.line(arc(mx, my - 3, 5, 2.5, 30, 150) if expr != 'fierce' else [(mx - 5, my), (mx + 5, my)], (.35, .40, .62), 1.4)
    else:
        if expr == 'fierce':
            f.poly(arc(mx, my, 6, 2, 180, 360) + arc(mx, my, 6, 2.4, 0, 180), (.72, .12, .18))
            f.line([(mx - 6, my), (mx + 6, my)], (.98, .95, .9), 1.0)
        else:
            f.poly(arc(mx, my - 1, 7, 2.6, 180, 360) + arc(mx, my, 6.5, 3.2, 0, 180), (.78, .16, .22))
            f.line(arc(mx, my - 2.6, 7, 3, 25, 155), (.50, .06, .10), 1.0)
    return f.result()
