"""Hand-painted-looking OoT textures for Hyrule Kingdom, made from code (no copied art). Each is painted at 256 x 256 and averaged down to the
game's 128 x 128 RGBA5551 (four times the size OoT uses for its walls and floors). Bold colour blocks, dark grout, a little brush noise: art-style guide §3."""
import numpy as np

N = 256        # painted at this size, averaged down to SIZE
K = N // 128   # pixel sizes below are written for 128
rng_master = np.random.default_rng(1986)

def _noise(scale, seed, octaves=3):
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N))
    amp = 1.0; tot = 0
    for o in range(octaves):
        s = max(2, int(scale / (2 ** o)))
        g = rng.random((N // s + 2, N // s + 2))
        # tileable: wrap the lattice
        g[-2:, :] = g[:2, :]; g[:, -2:] = g[:, :2]
        ys, xs = np.mgrid[0:N, 0:N] / s
        i, j = ys.astype(int), xs.astype(int); u, v = ys - i, xs - j
        u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v)
        gi = lambda a, b: g[(i + a) % (N // s), (j + b) % (N // s)]
        out += amp * ((gi(0, 0) * (1 - v) + gi(0, 1) * v) * (1 - u) + (gi(1, 0) * (1 - v) + gi(1, 1) * v) * u)
        tot += amp; amp *= 0.5
    return out / tot * 2 - 1

def _up(img):
    return np.kron(img, np.ones((K, K, 1))) if K > 1 else img

def _col(hexs):
    hexs = hexs.lstrip('#'); return np.array([int(hexs[i:i + 2], 16) for i in (0, 2, 4)], dtype=float)

def _mix(a, b, t):
    return a * (1 - t[..., None]) + b * t[..., None]

def blocks(base, mortar, rows, cols, seed, jitter=0.1, mortar_w=3 * K, bevel=True, stagger=True):
    n = _noise(24 * K, seed); fine = _noise(6 * K, seed + 1, 2)
    img = np.zeros((N, N, 3)) + _col(base)
    y, x = np.mgrid[0:N, 0:N]
    bh = N // rows; bw = N // cols
    row = y // bh
    xo = (x + (bw // 2) * (row % 2) * stagger) % N
    col = xo // bw
    rng = np.random.default_rng(seed)
    tone = rng.uniform(1 - jitter, 1 + jitter, (rows + 1, cols + 1))
    img *= tone[row, col][..., None]
    img *= (1 + 0.10 * n + 0.06 * fine)[..., None]
    ly, lx = y % bh, xo % bw
    if bevel:
        img *= np.where((ly < 4 * K) | (lx < 4 * K), 1.10, 1.0)[..., None]
        img *= np.where((ly > bh - 6 * K) | (lx > bw - 6 * K), 0.86, 1.0)[..., None]
    grout = (ly < mortar_w) | (lx < mortar_w)
    img = np.where(grout[..., None], _col(mortar) * (1 + 0.1 * fine[..., None]), img)
    return img

def planks(base, dark, count, seed, vertical=False, nails=True):
    y, x = np.mgrid[0:N, 0:N]
    if vertical: x, y = y, x
    n = _noise(32 * K, seed); grain = np.sin(x * 0.35 / K + 6 * _noise(40 * K, seed + 3)) * 0.5 + 0.5
    pw = N // count; board = y // pw
    rng = np.random.default_rng(seed)
    tone = rng.uniform(0.85, 1.12, count + 1)
    img = _col(base) * (tone[board] * (1 + 0.08 * n) * (0.9 + 0.14 * grain))[..., None]
    gap = (y % pw) < 3 * K
    img = np.where(gap[..., None], _col(dark), img)
    ends = ((x + board * 37 * K) % N) < 2 * K
    img = np.where((ends & ~gap)[..., None], _col(dark) * 1.2, img)
    if nails:
        nail = (((y % pw) - pw // 2) ** 2 + (((x + board * 37 * K) % N) - 8 * K) ** 2) < 5 * K * K
        img = np.where(nail[..., None], _col(dark) * 0.7, img)
    return img

def tiles(base, dark, rows, seed, round_=True):
    """Overlapping clay roof tiles: rows of scallops, shaded at the lower lip."""
    y, x = np.mgrid[0:N, 0:N]
    th = N // rows; row = y // th
    tw = th
    xo = (x + (tw // 2) * (row % 2)) % N
    ly = y % th; lx = xo % tw
    rng = np.random.default_rng(seed)
    tone = rng.uniform(0.88, 1.1, (rows + 1, N // tw + 2))
    img = _col(base) * tone[row, xo // tw][..., None]
    img *= (0.78 + 0.32 * (1 - ly / th))[..., None]                     # lighter at the top of each tile
    if round_:
        edge = np.abs(lx - tw / 2) / (tw / 2)
        img *= (1.05 - 0.25 * edge ** 2)[..., None]
        img = np.where(((lx < 2 * K) | (ly > th - 3 * K))[..., None], _col(dark), img)
    else:
        img = np.where(((lx < 2 * K) | (ly > th - 3 * K))[..., None], _col(dark), img)
    img *= (1 + 0.06 * _noise(16 * K, seed + 5))[..., None]
    return img

def mottled(c0, c1, scale, seed, c2=None, speck=0.0):
    n = _noise(scale, seed); f = _noise(max(3, scale // 4), seed + 7, 2)
    t = np.clip(0.5 + 0.8 * n, 0, 1)
    img = _mix(_col(c0), _col(c1), t)
    if c2 is not None:
        img = _mix(img, _col(c2), np.clip(f * 2 - 0.6, 0, 1))
    img *= (1 + 0.07 * f)[..., None]
    if speck:
        rng = np.random.default_rng(seed + 11)
        s = rng.random((N, N)) < speck
        img = np.where(s[..., None], img * 0.75, img)
    return img

def strata(seed):
    """Cliff rock: warm red-brown ledges stacked like OoT's field cliffs, each with a lit top lip and a dark undercut, pink-lit."""
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(28 * K, seed); f = _noise(8 * K, seed + 2, 2)
    yy = y + 14 * K * n
    layer = (yy // (21 * K)).astype(int); ly = (yy % (21 * K)) / (21 * K)
    rng = np.random.default_rng(seed)
    tone = rng.uniform(0.86, 1.12, 64)[layer % 64]
    img = _mix(_col('#8e624a'), _col('#b98a68'), np.clip(0.5 + 0.6 * f, 0, 1)) * tone[..., None]
    img *= (1.12 - 0.42 * ly ** 2)[..., None]                      # lit at the top of each ledge, shadowed beneath
    img = np.where((ly > 0.9)[..., None], _col('#4e3428'), img)
    # vertical joints that break the ledges into blocks
    jx = (x + (layer * 53 * K) % N) % (37 * K)
    img = np.where(((jx < 2 * K) & (ly < 0.9))[..., None], img * 0.62, img)
    return img

def leaves(c0, c1, seed, blossom=None):
    rng = np.random.default_rng(seed)
    img = np.zeros((N, N, 3)) + _col(c0) * 0.7
    y, x = np.mgrid[0:N, 0:N]
    for _ in range(260 * K * K):
        cx, cy = rng.integers(0, N, 2); r = rng.integers(5 * K, 11 * K)
        d = ((x - cx + N // 2) % N - N // 2) ** 2 + ((y - cy + N // 2) % N - N // 2) ** 2
        m = d < r * r
        shade = 0.75 + 0.5 * (1 - np.sqrt(np.clip(d, 0, None)) / r)    # each leaf clump lit from the top
        lit = np.clip(((y - cy + N // 2) % N - N // 2) / r, -1, 1)
        tone = _mix(_col(c0), _col(c1), np.clip(0.5 - 0.5 * lit, 0, 1))
        img = np.where(m[..., None], tone * shade[..., None] * rng.uniform(0.85, 1.1), img)
    if blossom is not None:
        dots = rng.random((N, N)) < 0.02 / K
        img = np.where(dots[..., None], _col(blossom), img)
    return img

def bark(seed):
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(20 * K, seed)
    ridges = np.sin(x * 0.32 / K + 3 * n) * 0.5 + 0.5
    img = _mix(_col('#4c3424'), _col('#7a5a3e'), ridges)
    img *= (1 + 0.1 * _noise(8 * K, seed + 3))[..., None]
    return img

def water(seed):
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(32 * K, seed)
    streak = np.sin(y * 0.25 / K + 4 * n) * 0.5 + 0.5
    img = _mix(_col('#3d8fb0'), _col('#b8e6ee'), streak ** 3)
    return img

def clock_face(seed):
    y, x = np.mgrid[0:128, 0:128] - 64 + 0.5
    r = np.hypot(x, y)
    img = np.zeros((128, 128, 3)) + _col('#5a4a3a')
    img = np.where((r < 60)[..., None], _col('#c89a3a'), img)                  # brass ring
    img = np.where((r < 52)[..., None], _col('#f2ead2'), img)                  # dial
    ang = np.arctan2(y, x)
    for k in range(12):
        a = k * np.pi / 6
        mark = (np.abs(r - 44) < 5) & (np.abs(((ang - a + np.pi) % (2 * np.pi)) - np.pi) < 0.07)
        img = np.where(mark[..., None], _col('#2a2420'), img)
    # hands: ten to two
    for a, l, w in [(-np.pi / 2 - np.pi / 3, 30, 3.5), (-np.pi / 2 + np.pi / 3 * 1.0, 40, 2.5)]:
        t = x * np.cos(a) + y * np.sin(a); s = -x * np.sin(a) + y * np.cos(a)
        hand = (t > -4) & (t < l) & (np.abs(s) < w)
        img = np.where(hand[..., None], _col('#2a2420'), img)
    img = np.where((r < 5)[..., None], _col('#c89a3a'), img)
    img = np.where((r > 62)[..., None], _col('#4a3a2a'), img)
    return _up(img)

def window(seed, lit=False):
    y, x = np.mgrid[0:128, 0:128]
    img = np.zeros((128, 128, 3)) + _col('#5a3a22')                               # wooden frame
    glass = (x > 12) & (x < 116) & (y > 12) & (y < 116)
    g = _mix(_col('#1e2a3a'), _col('#e8c070') if lit else _col('#3a5070'), np.clip((128 - y) / 128 * 0.8, 0, 1))
    img = np.where(glass[..., None], g, img)
    bars = glass & ((np.abs(x - 64) < 4) | (np.abs(y - 64) < 4))
    img = np.where(bars[..., None], _col('#5a3a22'), img)
    img = np.where(((x < 5) | (x > 122) | (y < 5) | (y > 122))[..., None], _col('#3a2414'), img)
    return _up(img)

def door(seed):
    img = planks('#7a5232', '#3a2414', 5, seed, vertical=True, nails=False)
    y, x = np.mgrid[0:N, 0:N] / K
    for by in (24, 100):
        band = np.abs(y - by) < 5
        img = np.where(band[..., None], _col('#3c3c40'), img)
    ring = (np.hypot(x - 96, y - 66) - 8) ** 2 < 4
    img = np.where(ring[..., None], _col('#c89a3a'), img)
    return img

def banner(c, trim, seed, emblem=True):
    y, x = np.mgrid[0:128, 0:128]
    img = np.zeros((128, 128, 3)) + _col(c)
    img *= (0.9 + 0.15 * np.sin(x * 0.2))[..., None]
    img = np.where(((x < 10) | (x > 117))[..., None], _col(trim), img)
    if emblem:   # a simple bird-and-triangle crest, not any real emblem
        t = (y > 40) & (y < 90) & (np.abs(x - 64) < (y - 40) * 0.55)
        hole = (y > 65) & (y < 90) & (np.abs(x - 64) < (90 - y) * 0.55)
        img = np.where((t & ~hole)[..., None], _col(trim), img)
    return _up(img)

def lava(seed):
    n = _noise(20 * K, seed)
    img = mottled('#2a2020', '#4a3430', 24, seed)
    crack = np.abs(n) < 0.08
    img = np.where(crack[..., None], _col('#f07a20'), img)
    glow = (np.abs(n) < 0.16) & ~crack
    img = np.where(glow[..., None], img * 0.5 + _col('#b03010') * 0.5, img)
    return img

def hay(seed):
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(10 * K, seed, 2)
    streaks = np.sin(x * 0.8 + y * 0.15 + 6 * n) * 0.5 + 0.5
    return _mix(_col('#b08a3a'), _col('#e8c870'), streaks)

def thatch(seed):
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(12 * K, seed, 2)
    streak = np.sin(x * 0.9 + 5 * n) * 0.5 + 0.5
    rows = (y % 32) / 32
    img = _mix(_col('#8a6a34'), _col('#d0aa60'), streak) * (0.75 + 0.3 * (1 - rows))[..., None]
    return img

def ground_detail(seed):
    """Laid over the island's painted ground colours (multiplied): brush strokes and tufts, nearly white so the colours stay true."""
    y, x = np.mgrid[0:N, 0:N]
    n = _noise(24 * K, seed); f = _noise(6 * K, seed + 1, 2)
    blades = np.sin(x * 1.3 + 3 * _noise(10 * K, seed + 2)) * np.sin(y * 0.4 + 2 * n)
    v = 0.86 + 0.07 * n + 0.05 * f + 0.04 * blades
    rng = np.random.default_rng(seed)
    pebbles = rng.random((N, N)) < 0.006
    v = np.where(pebbles, v * 0.8, v)
    return np.repeat(np.clip(v, 0, 1)[..., None] * 255, 3, axis=2)

def build_all():
    """name -> (image 128 x 128 x 3 floats 0..255, world units per repeat)."""
    T = {}
    T['castle_stone'] = (blocks('#b4ab8e', '#5f5848', 6, 3, 1, 0.12), 160)
    T['town_stone'] = (blocks('#a49a86', '#4e4a40', 8, 4, 2, 0.1), 120)
    T['plaster'] = (mottled('#e8dcc0', '#d4c4a0', 40, 3, '#c0b090'), 220)
    T['timber'] = (planks('#6a4428', '#2e1c10', 4, 4, vertical=True), 110)
    T['planks'] = (planks('#a07648', '#4a3018', 6, 5), 120)
    T['roof_red'] = (tiles('#b8502e', '#5a2214', 8, 6), 110)
    T['roof_blue'] = (tiles('#3a5a8a', '#1a2a44', 8, 7), 110)
    T['roof_green'] = (tiles('#4a7a4a', '#1e3a20', 8, 8, False), 110)
    T['thatch'] = (thatch(9), 140)
    T['sandstone'] = (blocks('#d6a868', '#8a6030', 5, 3, 10, 0.08), 150)
    T['sand'] = (mottled('#e2bc72', '#c89a52', 30, 11, speck=0.01), 240)
    T['snow'] = (mottled('#f4f2fa', '#cfd4ea', 36, 12), 260)
    T['ice'] = (mottled('#d0f0f8', '#88c4dc', 20, 13, '#ffffff'), 200)
    T['rock'] = (strata(14), 260)
    T['moss_stone'] = (blocks('#8a9078', '#3c4434', 5, 3, 15, 0.15), 150)
    T['cobble'] = (blocks('#a8a090', '#5c564a', 8, 8, 16, 0.12, 2), 120)
    T['grass'] = (mottled('#6aa046', '#9cc25c', 14, 17, '#4a7a34', 0.02), 180)
    T['bark'] = (bark(18), 120)
    T['leaves'] = (leaves('#3e7a32', '#8ac04a', 19), 200)
    T['leaves_dark'] = (leaves('#244a2a', '#5a8a3e', 20), 220)
    T['blossom'] = (leaves('#c86a9a', '#f4c0d8', 21, '#ffffff'), 200)
    T['pine'] = (leaves('#1e4a36', '#3e7a50', 22), 160)
    T['water'] = (water(23), 200)
    T['iron'] = (mottled('#4a4c50', '#6a6c70', 10, 24), 100)
    T['clock'] = (clock_face(25), 1)
    T['window'] = (window(26), 1)
    T['window_lit'] = (window(27, True), 1)
    T['door'] = (door(28), 1)
    T['brick_dark'] = (blocks('#5a5450', '#2a2624', 7, 4, 29, 0.12), 120)
    T['gold'] = (mottled('#f7d622', '#be9834', 8, 30), 60)
    T['banner_red'] = (banner('#a82a20', '#e8c050', 31), 1)
    T['banner_blue'] = (banner('#2a3a7a', '#e8c050', 32), 1)
    T['lava_rock'] = (lava(33), 200)
    T['hay'] = (hay(34), 100)
    T['cloth'] = (mottled('#c8b898', '#a89878', 16, 35), 120)
    T['dirt'] = (mottled('#9a7048', '#7a5634', 20, 36, speck=0.02), 200)
    T['ground_detail'] = (ground_detail(37), 1)
    return T

SIZE = 128  # the game's texture size: 128 x 128 RGBA16, four times OoT's usual 32 (loaded as a tile: the renderer has no 4 KB limit)

def to_native(img):
    """N x N x 3 -> SIZE x SIZE RGBA5551 bytes, high byte first (see tools/maps/export_convergence.py native_texture)."""
    a = img.reshape(SIZE, N // SIZE, SIZE, N // SIZE, 3).mean(axis=(1, 3)) / 255.0
    q = np.rint(np.clip(a, 0, 1) * 31).astype(np.uint16)
    words = (q[..., 0] << 11) | (q[..., 1] << 6) | (q[..., 2] << 1) | 1
    return list(words.astype('>u2').tobytes())

def native_preview(img):
    """What the game shows: the SIZE x SIZE texture, as 8-bit RGB."""
    a = img.reshape(SIZE, N // SIZE, SIZE, N // SIZE, 3).mean(axis=(1, 3))
    q = np.rint(np.clip(a / 255.0, 0, 1) * 31) / 31 * 255
    return q.astype(np.uint8)
