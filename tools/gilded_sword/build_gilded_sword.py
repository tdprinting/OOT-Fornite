"""Builds the Gilded Sword (Majora's Mask's strongest sword) in Blender, low poly, flat colour blocks, in the Ocarina of Time style.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0, Python 3.11):
    blender -b --python tools/gilded_sword/build_gilded_sword.py
    python3.11 tools/gilded_sword/build_gilded_sword.py
It writes assets/gilded_sword/gilded_sword.blend and .glb. Then run export_gilded_sword.py to turn the .blend into shared/gilded_sword_model.h.

It is modelled fresh from the sword's look (not taken from any game file): a long blade of gold and silver diamonds that narrows to a point, a pale
crystal pommel, a red wrapped grip with gold bands, and a curled silver guard whose two tendrils twist back on themselves. A second object, the
scabbard, covers the blade for when the sword is stowed on the back.

THE GRIP IS THE ORIGIN. The blade runs along +X, its flat faces look along +Z and the guard's tendrils reach along +-Y. Everything is in game units
(Link is about 60 tall, an Ocarina of Time Master Sword blade is about 40 long; this blade is about 56).
"""
import math
import os

import bpy
import bmesh
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "gilded_sword")

# The measures the game needs (also exported).
BLADE_START, BLADE_TIP = 6.0, 62.0   # where the blade leaves the guard and where its point is (x)
GRIP_END = -8.0                      # the pommel end of the grip

MATS = [
    ("Gold", (0.98, 0.76, 0.14)),
    ("Silver", (0.84, 0.87, 0.93)),
    ("GripRed", (0.74, 0.09, 0.12)),
    ("GripBand", (0.95, 0.72, 0.16)),
    ("Crystal", (0.90, 0.87, 0.98)),
    ("Leather", (0.30, 0.10, 0.09)),
    ("DarkSilver", (0.58, 0.62, 0.72)),
]
GOLD, SILVER, RED, BAND, CRYSTAL, LEATHER, DARKSILVER = range(7)


def B(p):
    """Game coordinates (x along the blade, y across, z thickness) in game units to Blender metres (the same axes)."""
    return Vector((p[0] / 100.0, p[1] / 100.0, p[2] / 100.0))


def make_materials():
    out = []
    for name, rgb in MATS:
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        m.diffuse_color = (*rgb, 1.0)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
            bsdf.inputs["Roughness"].default_value = 0.45 if name in ("Gold", "Silver", "GripBand", "DarkSilver") else 0.8
            bsdf.inputs["Metallic"].default_value = 0.0
        out.append(m)
    return out


def new_object(name, mats):
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    return ob


def tri(bm, a, b, c, mat):
    f = bm.faces.new([bm.verts.new(B(p)) for p in (a, b, c)])
    f.material_index = mat
    return f


def quad(bm, a, b, c, d, mat):
    f = bm.faces.new([bm.verts.new(B(p)) for p in (a, b, c, d)])
    f.material_index = mat
    return f


def prism(bm, a, b, r, sides, mat, r_end=None, caps=True):
    """A tube from a to b, `sides` flat sides, capped at both ends; r_end tapers it."""
    pa, pb = B(a), B(b)
    axis = (pb - pa).normalized()
    helper = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((0, 1, 0))
    u = axis.cross(helper).normalized()
    v = axis.cross(u).normalized()
    re = r if r_end is None else r_end
    ra, rb = [], []
    for i in range(sides):
        ang = 2 * math.pi * (i + 0.5) / sides
        d = (u * math.cos(ang) + v * math.sin(ang)) / 100.0
        ra.append(bm.verts.new(pa + d * r))
        rb.append(bm.verts.new(pb + d * re))
    for i in range(sides):
        j = (i + 1) % sides
        bm.faces.new((ra[i], ra[j], rb[j], rb[i])).material_index = mat
    if caps:
        bm.faces.new(ra[::-1]).material_index = mat
        bm.faces.new(rb).material_index = mat


def box(bm, c, h, mat):
    x, y, z = c
    hx, hy, hz = h
    vs = [bm.verts.new(B((x + sx * hx, y + sy * hy, z + sz * hz))) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    for idx in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([vs[i] for i in idx]).material_index = mat


def bipyramid(bm, a, b, r, sides, mat, mid=0.5):
    """A cut gem: points at a and b, a ring of `sides` corners at `mid` of the way between them."""
    pa, pb = B(a), B(b)
    axis = (pb - pa).normalized()
    helper = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((0, 1, 0))
    u = axis.cross(helper).normalized()
    v = axis.cross(u).normalized()
    centre = pa + (pb - pa) * mid
    ring = [bm.verts.new(centre + (u * math.cos(2 * math.pi * i / sides) + v * math.sin(2 * math.pi * i / sides)) * r / 100.0) for i in range(sides)]
    ta, tb = bm.verts.new(pa), bm.verts.new(pb)
    for i in range(sides):
        j = (i + 1) % sides
        bm.faces.new((ring[i], ring[j], ta)).material_index = mat
        bm.faces.new((ring[j], ring[i], tb)).material_index = mat


def orient_outward(bm, blade=False):
    """Turns every face so that its normal points out of the solid it belongs to (the game lights the sword with them). Each separate piece (a tube, a
    box, a gem) is turned away from its own middle; the blade's two flat sides are turned up and down."""
    bm.faces.ensure_lookup_table()
    bm.faces.index_update()
    seen = set()
    for f0 in bm.faces:
        if f0.index in seen:
            continue
        island, stack = [], [f0]
        seen.add(f0.index)
        while stack:
            f = stack.pop()
            island.append(f)
            for v in f.verts:
                for g in v.link_faces:
                    if g.index not in seen:
                        seen.add(g.index)
                        stack.append(g)
        verts = {v for f in island for v in f.verts}
        centre = sum((v.co for v in verts), Vector()) / len(verts)
        for f in island:
            f.normal_update()
            c = f.calc_center_median()
            if blade:
                want = Vector((0, 0, 1 if c.z > 0 else -1))
                out = f.normal.dot(want) < 0
            else:
                out = f.normal.dot(c - centre) < 0
            if out:
                f.normal_flip()


# ---- the blade -------------------------------------------------------------------------------------------------------------------------

NDIAMONDS = 5   # the pattern: this many diamonds along the blade, gold and silver in turn
DIAMOND_PITCH = (BLADE_TIP - BLADE_START) / NDIAMONDS   # (game units) from one diamond's point to the next's


def blade_width(x):
    """Half the blade's width at x: the same all the way down the first four diamonds (the game's engraved surface map follows that), then it narrows to the point."""
    t = (x - BLADE_START) / (BLADE_TIP - BLADE_START)
    return 4.6 * min(1.0, (1.0 - t) / 0.2)    # straight sided, then the last diamond narrows to the point


def blade_ridge(x):
    """Half the blade's thickness along its middle line."""
    t = (x - BLADE_START) / (BLADE_TIP - BLADE_START)
    return 1.25 * (1.0 - 0.55 * t)


def build_blade(mats):
    ob = new_object("GildedBlade", mats)
    bm = bmesh.new()
    n = NDIAMONDS
    xs = [BLADE_START + (BLADE_TIP - BLADE_START) * k / n for k in range(n + 1)]   # the diamonds' top and bottom corners, on the middle line
    ms = [(xs[k] + xs[k + 1]) * 0.5 for k in range(n)]                              # their side corners, on the edges
    for side in (1, -1):                                                            # +z face and -z face
        for ys in (1, -1):                                                          # left and right half of the face
            def C(k):
                return (xs[k], 0.0, side * blade_ridge(xs[k]))
            def E(k):
                return (ms[k], ys * blade_width(ms[k]), 0.0)
            base_edge = (BLADE_START, ys * blade_width(BLADE_START), 0.0)
            # the little triangle where the blade leaves the guard
            f = tri(bm, C(0), base_edge, E(0), SILVER)
            for k in range(n):
                gold_first = (k % 2 == 0)
                dia = GOLD if gold_first else SILVER
                other = SILVER if gold_first else GOLD
                tri(bm, C(k), E(k), C(k + 1), dia)                                    # the diamond's half on this side of the ridge
                if k + 1 < n:
                    tri(bm, C(k + 1), E(k), E(k + 1), other)                          # the triangle between this diamond and the next, on the edge
    orient_outward(bm, blade=True)
    bm.normal_update()
    bm.to_mesh(ob.data)
    bm.free()
    return ob


# ---- the hilt --------------------------------------------------------------------------------------------------------------------------

def curl_points(sign, start, reach, bow, turns, radius):
    """A tendril: out from the guard's end (bowing back toward the pommel), then a spiral that winds in on itself. Points (x, y, z); sign is +1 or -1 for the side."""
    pts = []
    for i in range(3):
        t = i / 2.0
        pts.append((start[0] - bow * math.sin(t * math.pi * 0.5), sign * (start[1] + reach * t), 0.0))
    xe, ye = pts[-1][0], pts[-1][1]
    cx, cy = xe - radius, ye            # the curl's centre, on the pommel side, so the tendril turns back toward the grip
    steps = int(turns * 7)
    for i in range(1, steps + 1):
        a = 2 * math.pi * turns * i / steps
        r = radius * (1.0 - 0.7 * i / steps)
        pts.append((cx + r * math.cos(a), cy + sign * r * math.sin(a), 0.0))
    return pts


def build_hilt(mats):
    ob = new_object("GildedHilt", mats)
    bm = bmesh.new()
    # the grip: red wrap with gold bands
    prism(bm, (GRIP_END + 0.5, 0, 0), (BLADE_START - 1.6, 0, 0), 1.35, 6, RED)
    for x in (-5.4, -2.6, 0.2, 3.0):
        prism(bm, (x, 0, 0), (x + 0.75, 0, 0), 1.6, 6, BAND, caps=False)
    # the pommel: a pale cut crystal in a silver cup
    prism(bm, (GRIP_END + 0.9, 0, 0), (GRIP_END - 0.6, 0, 0), 1.8, 6, SILVER, r_end=2.1)
    bipyramid(bm, (GRIP_END - 0.4, 0, 0), (GRIP_END - 5.2, 0, 0), 2.2, 6, CRYSTAL, mid=0.4)
    # the guard: a broad silver block over the blade's root with a gold boss, and two curled tendrils
    box(bm, (BLADE_START - 0.6, 0, 0), (1.4, 3.0, 1.5), SILVER)
    box(bm, (BLADE_START - 0.6, 0, 0), (1.0, 1.6, 1.9), GOLD)
    for sign in (1, -1):
        pts = curl_points(sign, (BLADE_START - 0.6, 2.6), 5.0, 1.6, 1.0, 2.8)
        for a, b in zip(pts, pts[1:]):
            prism(bm, a, b, 0.7, 4, SILVER, caps=False)
        # a second, shorter tendril curling the other way, toward the blade (the guard is a pair of twisted vines)
        pts2 = curl_points(sign, (BLADE_START - 0.6, 2.4), 3.4, -1.0, 0.7, 1.7)
        for a, b in zip(pts2, pts2[1:]):
            prism(bm, a, b, 0.55, 4, DARKSILVER, caps=False)
    orient_outward(bm)
    bm.normal_update()
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def build_scabbard(mats):
    """The scabbard that covers the blade when the sword is stowed: dark leather with silver bands and a gold tip, a little wider than the blade."""
    ob = new_object("GildedScabbard", mats)
    bm = bmesh.new()
    x0, x1 = BLADE_START - 0.2, BLADE_TIP - 3.0
    def section(x):
        t = (x - x0) / (x1 - x0)
        return 5.4 * (1.0 - 0.45 * t), 1.9 * (1.0 - 0.3 * t)
    stations = [x0 + (x1 - x0) * k / 6 for k in range(7)]
    rings = []
    for x in stations:
        w, th = section(x)
        rings.append([bm.verts.new(B((x, y, z))) for y, z in ((w, 0.0), (w * 0.55, th), (-w * 0.55, th), (-w, 0.0), (-w * 0.55, -th), (w * 0.55, -th))])
    for k in range(6):
        for i in range(6):
            j = (i + 1) % 6
            bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i])).material_index = LEATHER
    bm.faces.new(rings[0][::-1]).material_index = LEATHER
    bm.faces.new(rings[-1]).material_index = LEATHER
    # the gold tip and the silver mouth
    w, th = section(x1)
    bipyramid(bm, (x1 - 0.5, 0, 0), (x1 + 4.0, 0, 0), w * 0.95, 6, GOLD, mid=0.0)
    box(bm, (x0 + 1.0, 0, 0), (1.0, section(x0)[0] + 0.5, section(x0)[1] + 0.5), SILVER)
    box(bm, (x0 + (x1 - x0) * 0.5, 0, 0), (0.8, section(x0 + (x1 - x0) * 0.5)[0] + 0.35, section(x0 + (x1 - x0) * 0.5)[1] + 0.35), DARKSILVER)
    orient_outward(bm)
    bm.normal_update()
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_materials()
    build_blade(mats)
    build_hilt(mats)
    build_scabbard(mats)
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "gilded_sword.blend"))
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "gilded_sword.glb"), export_format="GLB")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
