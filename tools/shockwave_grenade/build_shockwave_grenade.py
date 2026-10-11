"""Builds the Shockwave Grenade (the "bounceball") in Blender: low poly, flat colour blocks, in the Ocarina of Time style.

Run with Blender (any 4.x), or with the `bpy` Python module (pip install bpy==4.2.0, Python 3.11):
    blender -b --python tools/shockwave_grenade/build_shockwave_grenade.py
    python3.11 tools/shockwave_grenade/build_shockwave_grenade.py
It writes assets/shockwave_grenade/shockwave_grenade.blend and .glb.

The look comes from Tee's reference video (docs/BOUNCEBALL-MECHANICS.md section 1), modelled fresh, with no game file or texture used: a pale silver
ball the size of a bomb, a big faceted purple crystal set in a raised socket ring on its front, a dark seam around the middle, and six short round
studs in a ring around the crystal, each with a glowing purple core. The crystal and the cores use emission, so they look lit from inside.

The game draws the same shape from shared/meshes.h (mesh_detail::Grenade, the same measures as here), so it does not wait on this file. This .blend
is the authoring copy for previews and later changes: keep both in step when either changes.

Animation (one action, 24 fps, the 0.57 s fuse = 14 frames, then the blast):
  - "roll": the ball turns about X, one full turn per 2*pi*radius of travel, and moves 110 units forward (the no-target throw).
  - "blink": the crystal and studs pulse, slow at first and faster as the fuse runs out, like the game's own bombs.
Axes: the game's +Y is up and its +Z is the crystal's front. Blender is Z up, so a game point (x, y, z) is Blender (x, -z, y), in metres = units / 100.
"""
import math
import os

import bpy
import bmesh
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "assets", "shockwave_grenade")

# The measures (game units), the same as shared/meshes.h Grenade().
R = 14.0                          # the ball
CENTRE_Y = R + 1.5                # it stands on y 0
SOCKET_R, SOCKET_IN, SOCKET_OUT = 8.6, 6.4, 2.0
GEM_R, GEM_FACETS = 6.6, 8
STUD_R, STUD_LEN, STUD_SIDES, STUD_TILT = 3.4, 2.6, 6, 1.85
BALL_RINGS, BALL_SEGS, SOCKET_SIDES = 7, 12, 10
FUSE_FRAMES, FPS, SELF_THROW = 14, 24, 110.0

MATS = [
    ("Silver", (0.77, 0.78, 0.84), 0.0),
    ("Seam", (0.36, 0.35, 0.44), 0.0),
    ("Socket", (0.59, 0.60, 0.67), 0.0),
    ("CrystalDark", (0.49, 0.25, 0.89), 1.2),
    ("CrystalLight", (0.69, 0.42, 1.0), 1.6),
    ("Core", (0.80, 0.51, 1.0), 2.0),
]
SILVER, SEAM, SOCKET, CRYSTAL_DARK, CRYSTAL_LIGHT, CORE = range(len(MATS))


def B(x, y, z):
    """A game point (units, Y up, +Z front) to Blender metres (Z up, -Y front)."""
    return Vector((x / 100.0, -z / 100.0, y / 100.0))


def make_materials():
    out = []
    for name, rgb, glow in MATS:
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        m.diffuse_color = (*rgb, 1.0)
        m.use_nodes = True
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
            bsdf.inputs["Roughness"].default_value = 0.5
            bsdf.inputs["Metallic"].default_value = 0.0
            if glow > 0:
                key = "Emission Color" if "Emission Color" in bsdf.inputs else "Emission"
                bsdf.inputs[key].default_value = (*rgb, 1.0)
                bsdf.inputs["Emission Strength"].default_value = glow
        out.append(m)
    return out


def new_object(name, mats):
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    return ob


def add_face(bm, pts, mat):
    f = bm.faces.new([bm.verts.new(B(*p)) for p in pts])
    f.material_index = mat
    return f


def build(mats):
    ob = new_object("ShockwaveGrenade", mats)
    bm = bmesh.new()
    c = (0.0, CENTRE_Y, 0.0)
    rings, segs = BALL_RINGS, BALL_SEGS
    for i in range(rings):   # the ball: flat faces, a dark seam band on the middle ring
        la0 = -math.pi / 2 + math.pi * i / rings
        la1 = -math.pi / 2 + math.pi * (i + 1) / rings
        for j in range(segs):
            a0, a1 = math.tau * j / segs, math.tau * (j + 1) / segs
            def p(la, a):
                return (c[0] + math.cos(la) * math.cos(a) * R, c[1] + math.sin(la) * R, c[2] + math.cos(la) * math.sin(a) * R)
            mid = (la0 + la1) / 2
            mat = SEAM if abs(math.sin(mid)) < 0.12 else SILVER
            pts = [p(la0, a0), p(la0, a1), p(la1, a1), p(la1, a0)]
            if i == 0:
                pts = [p(la0, a0), p(la1, a1), p(la1, a0)]
            elif i == rings - 1:
                pts = [p(la0, a0), p(la0, a1), p(la1, a0)]
            add_face(bm, pts, mat)
    z = R - 0.6
    for i in range(SOCKET_SIDES):   # the socket ring on the front
        a0, a1 = math.tau * i / SOCKET_SIDES, math.tau * (i + 1) / SOCKET_SIDES
        def s(rr, a, dz):
            return (math.cos(a) * rr, CENTRE_Y + math.sin(a) * rr, z + dz)
        add_face(bm, [s(SOCKET_R, a0, -2), s(SOCKET_R, a1, -2), s(SOCKET_R, a1, SOCKET_OUT), s(SOCKET_R, a0, SOCKET_OUT)], SOCKET)
        add_face(bm, [s(SOCKET_R, a0, SOCKET_OUT), s(SOCKET_R, a1, SOCKET_OUT), s(SOCKET_IN, a1, SOCKET_OUT), s(SOCKET_IN, a0, SOCKET_OUT)], SOCKET)
    tip = (0.0, CENTRE_Y, z + 5.6)
    for i in range(GEM_FACETS):   # the crystal: two rows of facets and a point
        a0, a1 = math.tau * (i + 0.5) / GEM_FACETS, math.tau * (i + 1.5) / GEM_FACETS
        b0 = (math.cos(a0) * GEM_R, CENTRE_Y + math.sin(a0) * GEM_R, z + 0.5)
        b1 = (math.cos(a1) * GEM_R, CENTRE_Y + math.sin(a1) * GEM_R, z + 0.5)
        m0 = (math.cos(a0) * GEM_R * 0.72, CENTRE_Y + math.sin(a0) * GEM_R * 0.72, z + 3.2)
        m1 = (math.cos(a1) * GEM_R * 0.72, CENTRE_Y + math.sin(a1) * GEM_R * 0.72, z + 3.2)
        side = CRYSTAL_LIGHT if i % 2 else CRYSTAL_DARK
        add_face(bm, [b0, b1, m1, m0], side)
        add_face(bm, [m0, m1, tip], CRYSTAL_LIGHT)
    for k in range(6):       # the studs, each with a glowing core
        a = math.tau * k / 6
        d = Vector((math.cos(a) * math.sin(STUD_TILT), math.sin(a) * math.sin(STUD_TILT), math.cos(STUD_TILT)))
        base = Vector((d.x * R, CENTRE_Y + d.y * R, d.z * R))
        u = Vector((0, 1, 0)).cross(d).normalized() if abs(d.y) < 0.9 else Vector((1, 0, 0)).cross(d).normalized()
        w = d.cross(u)
        def ring(along, rr, i):
            t = math.tau * i / STUD_SIDES
            q = base + d * along + (u * math.cos(t) + w * math.sin(t)) * rr
            return (q.x, q.y, q.z)
        end = base + d * (STUD_LEN + 0.4)
        for i in range(STUD_SIDES):
            add_face(bm, [ring(-1, STUD_R, i), ring(-1, STUD_R, i + 1), ring(STUD_LEN, STUD_R, i + 1), ring(STUD_LEN, STUD_R, i)], SOCKET)
            add_face(bm, [ring(STUD_LEN, STUD_R, i), ring(STUD_LEN, STUD_R, i + 1), ring(STUD_LEN, STUD_R * 0.55, i + 1)], SILVER)
            add_face(bm, [ring(STUD_LEN, STUD_R * 0.55, i), ring(STUD_LEN, STUD_R * 0.55, i + 1), (end.x, end.y, end.z)], CORE)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(ob.data)
    bm.free()
    for poly in ob.data.polygons:
        poly.use_smooth = False   # flat, like the game's own models
    return ob


def animate(ob, mats):
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start, scene.frame_end = 0, FUSE_FRAMES
    pivot = bpy.data.objects.new("GrenadeRoll", None)   # roll about the ball's centre
    bpy.context.collection.objects.link(pivot)
    pivot.location = B(0, CENTRE_Y, 0)
    ob.parent = pivot
    ob.location = -pivot.location
    for f in range(FUSE_FRAMES + 1):
        u = f / FUSE_FRAMES
        along = SELF_THROW * (1 - (1 - u) ** 2)           # the same easing as UpdateGrenade in Projectiles.inc
        pivot.location = B(0, CENTRE_Y, along)
        pivot.rotation_euler = (-along / R, 0, 0)         # rolls forward without sliding
        pivot.keyframe_insert("location", frame=f)
        pivot.keyframe_insert("rotation_euler", frame=f)
        for idx in (CRYSTAL_DARK, CRYSTAL_LIGHT, CORE):    # the blink, faster as the fuse runs out
            bsdf = mats[idx].node_tree.nodes.get("Principled BSDF")
            if bsdf is None:
                continue
            t = f / FPS
            lit = math.sin(t * (10 + 34 * u)) > 0
            bsdf.inputs["Emission Strength"].default_value = MATS[idx][2] * (2.5 if lit else 1.0)
            bsdf.inputs["Emission Strength"].keyframe_insert("default_value", frame=f)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    mats = make_materials()
    ob = build(mats)
    animate(ob, mats)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "shockwave_grenade.blend"))
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "shockwave_grenade.glb"), export_animations=True)
    print("wrote", OUT, len(ob.data.polygons), "faces")


if __name__ == "__main__":
    main()
