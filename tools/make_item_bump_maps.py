"""Paint the sword and shield bump maps: 256 x 256, tileable, soft OoT-style relief (no ROM data).

Each item gets a gentle height field: brushy low-frequency metal or wood, plus its own motif (a fuller down
the blades, Deku grain and rim, Hylian rim bevel and crest boss, Mirror rim and scale border). Heights stay
near 0.5 so the paint keeps its look; the renderer only tilts the sunlight a little.

Writes assets/items/surface_maps/<item>_{normal,bump}.png (previews, RGBA8, 128 = flat) and
shared/item_surface_maps.h (8-bit heights; the game expands them to the same normal/bump RGBA at start).
Deterministic. Run: python tools/make_item_bump_maps.py
"""
from pathlib import Path
import math
import random
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SIZE = 256
STRENGTH = 3.0  # must match kStrength in the generated header
ITEMS = ('kokiri_sword', 'master_sword', 'biggoron_sword', 'deku_shield', 'hylian_shield', 'mirror_shield')


def periodic_noise(seed, cells, size=SIZE):
    """Smooth value noise that wraps at the edges (cells must divide size)."""
    rnd = random.Random(seed)
    grid = [[rnd.random() for _ in range(cells)] for _ in range(cells)]
    step = size / cells
    out = []
    for y in range(size):
        gy, fy = divmod(y / step, 1)
        gy = int(gy)
        sy = fy * fy * (3 - 2 * fy)
        row = []
        for x in range(size):
            gx, fx = divmod(x / step, 1)
            gx = int(gx)
            sx = fx * fx * (3 - 2 * fx)
            a, b = grid[gy % cells][gx % cells], grid[gy % cells][(gx + 1) % cells]
            c, d = grid[(gy + 1) % cells][gx % cells], grid[(gy + 1) % cells][(gx + 1) % cells]
            row.append((a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy)
        out.append(row)
    return out


def brush(seed):
    """Painterly base: broad soft strokes plus a little finer grain, centred on 0."""
    big, mid = periodic_noise(seed, 4), periodic_noise(seed + 1, 16)
    return [[(big[y][x] - .5) * .7 + (mid[y][x] - .5) * .3 for x in range(SIZE)] for y in range(SIZE)]


def smooth_band(t, centre, width):
    d = abs(t - centre) / width
    return max(0.0, 1 - d * d) ** 2


def heights(name):
    seed = ITEMS.index(name) * 17 + 3
    base = brush(seed)
    h = [[.5] * SIZE for _ in range(SIZE)]
    tau = 2 * math.pi
    for y in range(SIZE):
        v = y / SIZE
        for x in range(SIZE):
            u = x / SIZE
            b = base[y][x]
            if name.endswith('sword'):
                # Hammered steel; a soft fuller groove running along the blade (wraps in u).
                fuller = smooth_band(abs(math.sin(math.pi * u)), 0.0, .35)
                amp = {'kokiri_sword': .10, 'master_sword': .08, 'biggoron_sword': .12}[name]
                val = .5 + b * amp - fuller * .10
                if name == 'master_sword':
                    # Faint diamond engraving, the blade's own motif.
                    diamond = abs(math.sin(tau * (u + v) * 2)) * abs(math.sin(tau * (u - v) * 2))
                    val += (diamond - .4) * .05
                if name == 'biggoron_sword':
                    val += math.sin(tau * v * 3) * .03  # long forge ripples
            elif name == 'deku_shield':
                grain = math.sin(tau * (v * 6 + b * 1.6)) * .07  # soft wood grain
                plank = smooth_band((u * 3) % 1, 0.0, .08) + smooth_band((u * 3) % 1, 1.0, .08)
                val = .5 + grain + b * .06 - plank * .12
            elif name == 'hylian_shield':
                r = math.hypot(u - .5, v - .5) * 2
                rim = smooth_band(r, .92, .10)
                boss = smooth_band(r, 0.0, .28)
                rivets = sum(smooth_band(math.hypot(u - .5 - .40 * math.cos(a), v - .5 - .40 * math.sin(a)), 0, .035)
                             for a in (k * tau / 8 for k in range(8)))
                val = .5 + b * .07 + rim * .16 + boss * .10 + rivets * .10
            else:  # mirror_shield
                r = math.hypot(u - .5, v - .5) * 2
                rim = smooth_band(r, .92, .10)
                scales = abs(math.sin(tau * u * 8)) * abs(math.sin(tau * v * 8 + math.pi * (int(u * 8) % 2) / 2))
                border = smooth_band(r, .78, .10)
                val = .5 + b * .04 + rim * .16 + border * (scales - .5) * .10
            h[y][x] = min(1.0, max(0.0, val))
    return h


def quantize(h):
    return bytes(min(255, max(0, math.floor(v * 255 + .5))) for row in h for v in row)


def expand(height, size=SIZE, strength=STRENGTH):
    """8-bit heights -> (normal RGBA, bump RGBA). Mirrors ExpandHeights in the generated header."""
    n, bump = bytearray(), bytearray()
    for y in range(size):
        for x in range(size):
            at = lambda xx, yy: height[(yy % size) * size + (xx % size)] / 255.0
            dx = (at(x + 1, y) - at(x - 1, y)) * strength
            dy = (at(x, y + 1) - at(x, y - 1)) * strength
            length = math.sqrt(dx * dx + dy * dy + 1)
            n.extend(min(255, max(0, math.floor((a / length * .5 + .5) * 255 + .5))) for a in (-dx, -dy, 1))
            n.append(255)
            b = height[y * size + x]
            bump.extend((b, b, b, 255))
    return bytes(n), bytes(bump)


def png(path, data, size=SIZE):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    rows = b''.join(b'\0' + data[y * size * 4:(y + 1) * size * 4] for y in range(size))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


HEADER_TAIL = '''
// The game's maps: RGBA8, 128 = flat, OpenGL tangent convention; built once from the heights above.
struct Maps { std::vector<uint8_t> normal, bump; };
inline Maps ExpandHeights(const uint8_t* height) {
    Maps m; m.normal.reserve(kSize * kSize * 4); m.bump.reserve(kSize * kSize * 4);
    auto at = [&](int x, int y) { return height[((y + kSize) % kSize) * kSize + ((x + kSize) % kSize)] / 255.0; };
    for (int y = 0; y < (int)kSize; ++y)
        for (int x = 0; x < (int)kSize; ++x) {
            const double dx = (at(x + 1, y) - at(x - 1, y)) * kStrength, dy = (at(x, y + 1) - at(x, y - 1)) * kStrength;
            const double len = std::sqrt(dx * dx + dy * dy + 1);
            for (double a : { -dx, -dy, 1.0 }) m.normal.push_back((uint8_t)std::clamp(std::floor((a / len * .5 + .5) * 255 + .5), 0.0, 255.0));
            m.normal.push_back(255);
            const uint8_t b = height[y * kSize + x];
            m.bump.insert(m.bump.end(), { b, b, b, 255 });
        }
    return m;
}
#define ROYALE_ITEM_MAPS(item) \\
    inline const Maps& item##_maps() { static const Maps m = ExpandHeights(item##_height); return m; }
ROYALE_ITEM_MAPS(kokiri_sword) ROYALE_ITEM_MAPS(master_sword) ROYALE_ITEM_MAPS(biggoron_sword)
ROYALE_ITEM_MAPS(deku_shield) ROYALE_ITEM_MAPS(hylian_shield) ROYALE_ITEM_MAPS(mirror_shield)
#undef ROYALE_ITEM_MAPS
} // namespace royale::item_surface
'''


def main():
    dest = ROOT / 'assets' / 'items' / 'surface_maps'
    dest.mkdir(parents=True, exist_ok=True)
    lines = ['// Generated by tools/make_item_bump_maps.py; do not edit.', '#pragma once', '#include <algorithm>',
             '#include <cmath>', '#include <cstdint>', '#include <vector>', 'namespace royale::item_surface {',
             f'inline constexpr unsigned kSize = {SIZE};', f'inline constexpr double kStrength = {STRENGTH};']
    for name in ITEMS:
        height = quantize(heights(name))
        normal, bump = expand(height)
        png(dest / f'{name}_normal.png', normal)
        png(dest / f'{name}_bump.png', bump)
        lines.append(f'inline constexpr uint8_t {name}_height[] = {{')
        lines.extend(','.join(str(b) for b in height[i:i + 64]) + ',' for i in range(0, len(height), 64))
        lines.append('};')
    (ROOT / 'shared' / 'item_surface_maps.h').write_text('\n'.join(lines) + HEADER_TAIL)
    print(f'Generated {len(ITEMS) * 2} maps ({SIZE}x{SIZE}) and shared/item_surface_maps.h')


if __name__ == '__main__':
    main()
