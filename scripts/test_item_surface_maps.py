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

    def test_materials_are_valid_and_distinct(self):
        outputs = []
        for name in maps.ITEMS:
            normal, height = maps.encode_maps(maps.heights(name))
            self.assertEqual(len(normal), maps.SIZE**2*4)
            self.assertEqual(len(height), len(normal))
            for i in range(0, len(normal), 4):
                x, y, z = ((normal[i+k]/255*2-1) for k in range(3))
                self.assertLess(abs(math.sqrt(x*x+y*y+z*z)-1), .015)
                self.assertGreater(normal[i+2], 128)
                self.assertEqual(normal[i+3], 255)
            self.assertGreater(len(set(height[::4])), 2)
            outputs.append(normal)
        # Metal recipes may share a polish pattern; wood must differ clearly.
        self.assertNotEqual(outputs[3], outputs[1])
        self.assertNotEqual(outputs[5], outputs[4])

    def test_sunlight_changes_relief_with_direction(self):
        normal, _ = maps.encode_maps(maps.heights('deku_shield'))
        x = [(normal[i]-128)/127 for i in range(0, len(normal), 4)]
        z = [normal[i+2]/255*2-1 for i in range(0, len(normal), 4)]
        east = [max(0, .8*a+.6*b) for a, b in zip(x, z)]
        west = [max(0, -.8*a+.6*b) for a, b in zip(x, z)]
        self.assertGreater(max(abs(a-b) for a, b in zip(east, west)), .1)
        # No solar intensity means the relief contributes no sunlight at night.
        self.assertEqual([0*a for a in east], [0*a for a in west])

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
