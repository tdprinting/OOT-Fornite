"""Draw the exported allies the way the game does (same skinning, same lighting, bilinear textures) into contact sheets.
Run: python tools/allies/export_allies.py --dump /tmp/allies && python tools/allies/preview_allies.py /tmp/allies OUT_DIR
Needs numpy and Pillow. No Blender and no game needed, so it checks the exported data itself.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

KINDS = ['kokiri', 'zora', 'goron', 'gerudo']


def load(d, kind):
    with open(os.path.join(d, kind + '.json')) as f:
        m = json.load(f)
    m['V'] = np.array([v[:3] for v in m['verts']], float)
    m['N'] = np.array([v[3:6] for v in m['verts']], float) / 127.0
    m['ST'] = np.array([v[6:8] for v in m['verts']], float) / 32.0
    m['B0'] = np.array([v[8] for v in m['verts']])
    m['B1'] = np.array([v[9] for v in m['verts']])
    m['W0'] = np.array([v[10] for v in m['verts']], float) / 255.0
    P = np.array(m['poses'], float).reshape(-1, len(m['bones']), 7)
    m['Q'] = P[:, :, :4] / 32767.0
    m['T'] = P[:, :, 4:] / m['posFrac']
    tris, ttex = [], []
    for first, count, tfirst, tcount, tex in m['batches']:
        for t in m['tris'][tfirst:tfirst + tcount]:
            tris.append([first + i for i in t])
            ttex.append(tex)
    m['TRI'] = np.array(tris)
    m['TTEX'] = ttex

    def decode(w, h, a):
        b = np.array(a, np.uint8).reshape(h, w, 2)
        v = (b[:, :, 0].astype(int) << 8) | b[:, :, 1]
        return np.stack([(v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31], -1) * (255.0 / 31)
    m['BODY'] = decode(*m['body'])
    m['FACE'] = [decode(*f) for f in m['faces']]
    return m


def bilinear(tex, s, t):
    """The game filters textures (bilinear), clamped at the edges; texel centres are at +0.5."""
    h, w = tex.shape[:2]
    x = np.clip(s - .5, 0, w - 1)
    y = np.clip(t - .5, 0, h - 1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    return (tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx) * (1 - fy) + (tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx) * fy


def sample(m, clip, seconds):
    name, n, fps, loops, first = m['clips'][clip]
    f = seconds * fps
    if loops:
        f = f % n
        a = int(f) % n
        b = (a + 1) % n
    else:
        f = min(max(f, 0), n - 1)
        a = int(f)
        b = min(a + 1, n - 1)
    k = f - int(f)
    qa, qb = m['Q'][first + a], m['Q'][first + b]
    sign = np.where((qa * qb).sum(1) < 0, -1.0, 1.0)[:, None]
    q = qa + (qb * sign - qa) * k
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    t = m['T'][first + a] + (m['T'][first + b] - m['T'][first + a]) * k
    return q, t


def rotate(q, v):
    qv, w = q[:, :3], q[:, 3:4]
    t = 2 * np.cross(qv, v)
    return v + w * t + np.cross(qv, t)


def skin(m, q, t):
    V, N = m['V'], m['N']
    pa = rotate(q[m['B0']], V) + t[m['B0']]
    pb = rotate(q[m['B1']], V) + t[m['B1']]
    na = rotate(q[m['B0']], N)
    nb = rotate(q[m['B1']], N)
    w = m['W0'][:, None]
    p = pa * w + pb * (1 - w)
    n = na * w + nb * (1 - w)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)
    return p, n


def render(m, p, n, face=0, yaw=0.5, size=220, span=130.0, centre_y=None, pitch=0.25):
    # the in-game light: sun direction in the model's frame (model yaw 0), as DrawBokoblinModel
    L = np.array([.35, .82, .45])
    lit = np.clip(.55 + .55 * np.maximum(0, n @ L), 0, 1)
    cy, sy = math.cos(yaw), math.sin(yaw)
    x = p[:, 0] * cy + p[:, 2] * sy
    z = -p[:, 0] * sy + p[:, 2] * cy
    y = p[:, 1]
    cp, sp = math.cos(pitch), math.sin(pitch)
    y2 = y * cp + z * sp
    depth = -y * sp + z * cp      # larger = nearer the camera (camera looks along -z from +z)
    if centre_y is None:
        centre_y = span * .42
    k = size / span
    X = size / 2 + x * k
    Y = size * .5 - (y2 - centre_y) * k
    img = np.zeros((size, size, 3), float) + [[[70, 78, 92]]]
    zbuf = np.full((size, size), -1e9)
    ST = m['ST']
    for tri, tex in zip(m['TRI'], m['TTEX']):
        atl = m['BODY'] if tex == 0 else m['FACE'][face]
        a, b, c = tri
        xs, ys = X[tri], Y[tri]
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
        dz = l0 * depth[a] + l1 * depth[b] + l2 * depth[c]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        win = inside & (dz > sub)
        if not win.any():
            continue
        s = l0 * ST[a, 0] + l1 * ST[b, 0] + l2 * ST[c, 0]
        t = l0 * ST[a, 1] + l1 * ST[b, 1] + l2 * ST[c, 1]
        col = bilinear(atl, s, t)
        sh = (l0 * lit[a] + l1 * lit[b] + l2 * lit[c])[..., None] * np.array([1.0, .98, .96])
        out = img[y0:y1 + 1, x0:x1 + 1]
        out[win] = (col * sh)[win]
        sub[win] = dz[win]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


def sheet(m, kind, out, span):
    rows = []
    labels = []
    for ci, (name, n, fps, loops, first) in enumerate(m['clips']):
        dur = n / fps if loops else (n - 1) / fps
        cells = []
        for i in range(6):
            tsec = dur * i / 6 if loops else dur * i / 5
            q, t = sample(m, ci, tsec)
            p, nn = skin(m, q, t)
            face = 2 if name in ('attack', 'attack2', 'hurt') else 0
            cells.append(render(m, p, nn, face, yaw=-.6, span=span))
        rows.append(cells)
        labels.append(name)
    w, h = rows[0][0].size
    img = Image.new('RGB', (w * 6, h * len(rows)), (40, 44, 52))
    dr = ImageDraw.Draw(img)
    for r, cells in enumerate(rows):
        for c, cell in enumerate(cells):
            img.paste(cell, (c * w, r * h))
        dr.text((4, r * h + 3), '%s %s' % (kind, labels[r]), fill=(255, 255, 255))
    img.save(out)


def turnaround(ms, out, spans):
    cells = []
    for kind, m in ms:
        q, t = sample(m, 0, 0)
        p, n = skin(m, q, t)
        for yaw in (0.0, -0.8, -1.6, math.pi):
            cells.append(render(m, p, n, 0, yaw=yaw, size=260, span=spans[kind]))
    w, h = cells[0].size
    img = Image.new('RGB', (w * 4, h * len(ms)), (40, 44, 52))
    for i, c in enumerate(cells):
        img.paste(c, ((i % 4) * w, (i // 4) * h))
    img.save(out)


def hero(ms, out, spans, shots=(('idle', 0.0, 0.0, 0), ('idle', 0.0, -.7, 0), ('walk', .2, -1.57, 0), ('attack', .12, -1.1, 2), ('dance', .3, -.4, 0), ('dance', 1.5, .4, 0), ('rest', 0.0, -.9, 1))):
    """Larger single frames: (clip, seconds, yaw, face) per column, one kind per row."""
    cells = []
    for kind, m in ms:
        names = [c[0] for c in m['clips']]
        for clip, sec, yaw, face in shots:
            q, t = sample(m, names.index(clip), sec)
            p, n = skin(m, q, t)
            cells.append(render(m, p, n, face, yaw=yaw, size=360, span=spans[kind], pitch=.12))
    w, h = cells[0].size
    cols = len(shots)
    img = Image.new('RGB', (w * cols, h * len(ms)), (40, 44, 52))
    for i, c in enumerate(cells):
        img.paste(c, ((i % cols) * w, (i // cols) * h))
    img.save(out)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    spans = {'kokiri': 105.0, 'zora': 170.0, 'goron': 150.0, 'gerudo': 175.0}
    kinds = [k for k in sys.argv[3:] if k in KINDS] or [k for k in KINDS if os.path.exists(os.path.join(src, k + '.json'))]
    ms = [(k, load(src, k)) for k in kinds]
    turnaround(ms, os.path.join(dst, 'turnaround.png'), spans)
    hero(ms, os.path.join(dst, 'hero.png'), spans)
    for k, m in ms:
        sheet(m, k, os.path.join(dst, 'clips_%s.png' % k), spans[k])
    print('previews in', dst)

