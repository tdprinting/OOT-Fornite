import importlib.util
from pathlib import Path
import math
import unittest

spec = importlib.util.spec_from_file_location('maps', Path(__file__).with_name('make_item_surface_maps.py'))
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)


class SurfaceMapsTest(unittest.TestCase):
    def test_flat_normal_and_bump(self):
        n, b = maps.encode_maps([[.5]*8 for _ in range(8)])
        self.assertEqual(n, bytes((128, 128, 255, 255))*64)
        self.assertEqual(b, bytes((128, 128, 128, 255))*64)

    def test_fallbacks_never_invent_texture_patterns(self):
        for name in maps.ITEMS:
            normal, bump = maps.encode_maps(maps.heights(name))
            self.assertEqual(normal, bytes((128,128,255,255))*maps.SIZE**2)
            self.assertEqual(bump, bytes((128,128,128,255))*maps.SIZE**2)

    def test_source_gradient_orientation(self):
        # Actual diffuse decoding/smoothing is tested by royale_asset_relief_tests.
        normal, _ = maps.encode_maps([[x/8 for x in range(8)] for y in range(8)])
        i=(4*8+4)*4
        self.assertLess(normal[i],128)
        self.assertEqual(normal[i+1],128)
        self.assertGreater(normal[i+2],128)

    def test_committed_header_matches_generator(self):
        header = (maps.ROOT/'shared/item_surface_maps.h').read_text()
        for name in maps.ITEMS:
            normal, bump = maps.encode_maps(maps.heights(name))
            for suffix, expected in (('normal', normal), ('bump', bump)):
                body = header.split(f'{name}_{suffix}[] = {{', 1)[1].split('};', 1)[0]
                actual = bytes(int(n) for n in body.split(',') if n.strip())
                self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()
