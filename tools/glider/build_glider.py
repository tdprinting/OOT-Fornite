"""Builds the skydiving glider in Blender: a hang glider in the Ocarina of Time style, low poly, flat colour blocks.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0, Python 3.11):
    blender -b --python tools/glider/build_glider.py
    python3.11 tools/glider/build_glider.py
It writes assets/glider/glider.blend and glider.glb. Then run export_glider.py to turn the .blend into the game's shared/glider_model.h.

The design (a Kokiri-made hang glider, like a big leaf kite): a wooden A-frame control bar with two leather grips Link holds, two struts rising
to a keel spine, wooden leading-edge spars running out to gold tips, a gold nose cap, and a striped cloth wing (the game's cloth simulation draws
the live wing; the wing built here is the still one, used when cloth physics is off and for bots).

THE HANDLE BAR IS THE ORIGIN. The game puts the glider's origin where Link's two hands are, so the grips are always in his hands whatever his
size or pose. Everything is measured from there (game units: X to his left, Y up, Z forward; one Blender metre is 100 units).

Blender axes: Z up, the glider faces -Y, its left is +X (the export turns that into the game's Y up, +Z forward).
"""
import math
import os

import bpy
import bmesh
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "glider")

# The measures the game needs (also exported): the still wing's corners, and how the grips sit.
NOSE = (0.0, 88.0, 74.0)
TAIL = (0.0, 78.0, -64.0)
TIP = (124.0, 70.0, -56.0)       # the right wing; the left is the mirror image
GRIP_INNER, GRIP_OUTER = 8.0, 30.0   # each grip runs from this far to that far out from the middle of the bar

# Materials, in the order of the colour indices the game reads. (Bright and flat, like the game's painted models.)
MATS = [
    ("Wood", (0.47, 0.35, 0.22)),
    ("Grip", (0.40, 0.12, 0.04)),
    ("Gold", (0.97, 0.84, 0.13)),
    ("WingA", (0.90, 0.27, 0.24)),
    ("WingB", (0.96, 0.92, 0.86)),
    ("UnderA", (0.63, 0.19, 0.17)),
    ("UnderB", (0.67, 0.64, 0.60)),
    ("DarkWood", (0.34, 0.24, 0.14)),
]
WOOD, GRIP, GOLD, WING_A, WING_B, UNDER_A, UNDER_B, DARK = range(8)


def B(p):
    """Game axes (x, y up, z forward) in game units to Blender coordinates (metres)."""
    return Vector((p[0] / 100.0, -p[2] / 100.0, p[1] / 100.0))


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def make_materials():
    out = []
    for name, rgb in MATS:
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        m.diffuse_color = (*rgb, 1.0)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
            bsdf.inputs["Roughness"].default_value = 0.9
        out.append(m)
    return out


def new_object(name, mats):
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    return ob


def prism(bm, a, b, r, sides, mat, r_end=None):
    """A tube from a to b (game coordinates), `sides` flat sides, capped at both ends; r_end tapers it."""
    pa, pb = B(a), B(b)
    axis = (pb - pa).normalized()
    helper = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(helper).normalized()
    v = axis.cross(u).normalized()
    re = r if r_end is None else r_end
    ring_a, ring_b = [], []
    for i in range(sides):
        ang = 2 * math.pi * (i + 0.5) / sides
        d = (u * math.cos(ang) + v * math.sin(ang)) / 100.0
        ring_a.append(bm.verts.new(pa + d * r))
        ring_b.append(bm.verts.new(pb + d * re))
    faces = []
    for i in range(sides):
        j = (i + 1) % sides
        faces.append(bm.faces.new((ring_a[i], ring_a[j], ring_b[j], ring_b[i])))
    faces.append(bm.faces.new(ring_a[::-1]))
    faces.append(bm.faces.new(ring_b))
    for f in faces:
        f.material_index = mat
    return faces


def box(bm, c, h, mat):
    x, y, z = c
    hx, hy, hz = h
    vs = [bm.verts.new(B((x + sx * hx, y + sy * hy, z + sz * hz))) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    for idx in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([vs[i] for i in idx]).material_index = mat


def cone(bm, base, tip, r, sides, mat):
    pb, pt = B(base), B(tip)
    axis = (pt - pb).normalized()
    helper = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(helper).normalized()
    v = axis.cross(u).normalized()
    ring = [bm.verts.new(pb + (u * math.cos(2 * math.pi * (i + 0.5) / sides) + v * math.sin(2 * math.pi * (i + 0.5) / sides)) * r / 100.0) for i in range(sides)]
    apex = bm.verts.new(pt)
    for i in range(sides):
        bm.faces.new((ring[i], ring[(i + 1) % sides], apex)).material_index = mat
    bm.faces.new(ring[::-1]).material_index = mat


def build_frame(mats):
    ob = new_object("GliderFrame", mats)
    bm = bmesh.new()
    # The control bar (the handle): a dark bar between two leather grips, with gold caps.
    prism(bm, (-GRIP_OUTER - 4, 0, 0), (GRIP_OUTER + 4, 0, 0), 2.6, 6, DARK)
    for sx in (-1, 1):
        prism(bm, (sx * GRIP_INNER, 0, 0), (sx * GRIP_OUTER, 0, 0), 4.4, 6, GRIP)
        prism(bm, (sx * (GRIP_OUTER + 4), 0, 0), (sx * (GRIP_OUTER + 9), 0, 0), 4.8, 6, GOLD)
    apex = (0, 80, -2)
    # The A-frame struts from the bar's ends up to the keel.
    for sx in (-1, 1):
        prism(bm, (sx * (GRIP_OUTER + 2), 2, 0), (sx * 5, apex[1] - 2, apex[2]), 2.8, 6, WOOD)
    # The keel spine from the nose to the tail, and a short rear brace down to the bar so it hangs level.
    prism(bm, (0, NOSE[1] - 2, NOSE[2]), (0, TAIL[1], TAIL[2]), 3.4, 6, WOOD)
    prism(bm, (0, 4, -6), (0, TAIL[1] - 2, TAIL[2] + 14), 2.0, 6, DARK)
    # Leading-edge spars out to the wing tips, capped in gold.
    for sx in (-1, 1):
        tip = (sx * TIP[0], TIP[1], TIP[2])
        prism(bm, (0, NOSE[1], NOSE[2]), tip, 2.8, 6, WOOD, r_end=2.0)
        cone(bm, tip, (sx * (TIP[0] + 14), TIP[1] - 1, TIP[2] - 8), 4.0, 5, GOLD)
        # a cross-spar halfway along, lower than the leading edge, so the cloth has something to be stretched from
        mid = lerp(NOSE, tip, 0.5)
        prism(bm, (0, TAIL[1] + 4, TAIL[2] + 20), (mid[0], mid[1] - 3, mid[2] - 24), 1.8, 5, DARK)
    # The gold nose cap.
    cone(bm, (0, NOSE[1], NOSE[2] - 2), (0, NOSE[1] + 2, NOSE[2] + 22), 6.0, 6, GOLD)
    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def build_wing(mats):
    """The still wing: two panels (left and right), three stripes along each, a little camber, with an underside a hand's breadth below."""
    ob = new_object("GliderWing", mats)
    bm = bmesh.new()
    chord, span = 4, 6
    for side in (-1, 1):
        tip = (side * TIP[0], TIP[1], TIP[2])
        grid = {}
        for j in range(span + 1):
            t = j / span
            lead, trail = lerp(NOSE, tip, t), lerp(TAIL, tip, t)
            for i in range(chord + 1):
                u = i / chord
                p = lerp(lead, trail, u)
                camber = 9.0 * math.sin(math.pi * u) * (1.0 - t * 0.7)    # the cloth bellies up between the spars
                grid[(i, j)] = (p[0], p[1] + camber, p[2])
        for j in range(span):
            stripe = (j // 2) % 2
            top_m, under_m = (WING_A, UNDER_A) if stripe == 0 else (WING_B, UNDER_B)
            for i in range(chord):
                q = [grid[(i, j)], grid[(i + 1, j)], grid[(i + 1, j + 1)], grid[(i, j + 1)]]
                if abs(q[2][0] - q[1][0]) < 1e-6 and abs(q[2][2] - q[1][2]) < 1e-6:
                    pass
                top = [bm.verts.new(B(p)) for p in q]
                under = [bm.verts.new(B((p[0], p[1] - 6.0, p[2]))) for p in q]
                # winding so that the top faces up (and the underside down), whichever wing it is
                f1 = bm.faces.new(top)
                if f1.normal.z < 0:
                    f1.normal_flip()
                f1.material_index = top_m
                f2 = bm.faces.new(under)
                if f2.normal.z > 0:
                    f2.normal_flip()
                f2.material_index = under_m
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_materials()
    build_frame(mats)
    build_wing(mats)
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "glider.blend"))
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "glider.glb"), export_format="GLB")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
