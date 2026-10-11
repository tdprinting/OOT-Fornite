import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('maps', Path(__file__).resolve().parents[1] / 'tools' / 'make_item_bump_maps.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)


class SurfaceMapsTest(unittest.TestCase):
    def test_flat_height_is_flat_normal(self):
        n, b = maps.expand(bytes([128]) * 64, size=8)
        self.assertEqual(n, bytes((128, 128, 255, 255)) * 64)
        self.assertEqual(b, bytes((128, 128, 128, 255)) * 64)

    def test_maps_are_subtle(self):
        # OoT look: relief stays near flat so the original paint reads first.
        for name in maps.ITEMS:
            h = maps.quantize(maps.heights(name))
            self.assertEqual(len(h), maps.SIZE * maps.SIZE)
            self.assertTrue(40 <= min(h) and max(h) <= 215, name)

    def test_committed_header_matches_generator(self):
        header = (maps.ROOT / 'shared/item_surface_maps.h').read_text()
        self.assertIn(f'kSize = {maps.SIZE};', header)
        for name in maps.ITEMS:
            body = header.split(f'{name}_height[] = {{', 1)[1].split('};', 1)[0]
            self.assertEqual(bytes(int(n) for n in body.split(',') if n.strip()), maps.quantize(maps.heights(name)))


if __name__ == '__main__':
    unittest.main()
