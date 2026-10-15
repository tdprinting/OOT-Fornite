"""Great Hyrule Kingdom's textures: the Kingdom set (tools/maps/kingdom/textures.py) plus Skyloft's extras and this map's own (Zora blue
stone, Goron red brick, Gerudo sand brick, snow timber). Painted at 256 x 256 and kept at 256 x 256 (Tee's texture rule, 2026-10-11)."""
import os, sys
KINGDOM = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'kingdom')
if KINGDOM not in sys.path: sys.path.append(KINGDOM)
import importlib.util
_spec = importlib.util.spec_from_file_location('kingdom_textures', os.path.join(KINGDOM, 'textures.py'))
_kt = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_kt)
import numpy as np
import surface_maps

_kt.SIZE = 256          # the kingdom module reads its SIZE when converting: keep the full painted size
SIZE = _kt.SIZE
to_native = _kt.to_native
native_preview = _kt.native_preview

def stripes(c0, c1, count, seed):
    """Canvas with bold vertical stripes (the Bazaar tent), a little weave noise."""
    n = _kt.N
    img = np.zeros((n, n, 3))
    xs = (np.arange(n) * count // n) % 2
    a, b = _kt._col(c0), _kt._col(c1)
    img[:] = np.where(xs[None, :, None] == 0, a, b)
    noise = _kt._noise(6, seed)[..., None]
    return np.clip(img * (0.94 + 0.08 * noise), 0, 255)

def pumpkin(seed):
    n = _kt.N
    img = _kt.mottled('#e08a2a', '#b85e18', 24, seed, '#f0a848')
    xs = np.arange(n)
    rib = (0.5 + 0.5 * np.cos(xs / n * 2 * np.pi * 6))[None, :, None]
    return np.clip(img * (0.82 + 0.22 * rib), 0, 255)

def build_all():
    T = _kt.build_all()
    T['roof_orange'] = (_kt.tiles('#c87838', '#6a3414', 8, 41), 110)
    T['roof_teal'] = (_kt.tiles('#3a7a78', '#1a3a3c', 8, 42), 110)
    T['canvas_stripe'] = (stripes('#e8c86a', '#c84a3a', 8, 43), 180)
    T['canvas_blue'] = (stripes('#e6e0cc', '#3a5a9a', 6, 44), 180)
    T['pumpkin'] = (pumpkin(45), 90)
    T['whitewash'] = (_kt.mottled('#f2ece0', '#dcd2bc', 40, 46, '#c8bca4'), 220)
    return T

for name, cls in (('roof_orange', 4), ('roof_teal', 4), ('whitewash', 7)):
    surface_maps.MATERIALS.setdefault(name, cls)
