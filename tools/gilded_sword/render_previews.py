"""Renders the Gilded Sword (and its scabbard) from several sides into one folder: python3.11 tools/gilded_sword/render_previews.py [folder]"""
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = sys.argv[-1] if len(sys.argv) > 1 and os.path.isdir(sys.argv[-1]) else os.path.join(ROOT, "assets", "gilded_sword", "previews")
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "gilded_sword", "gilded_sword.blend"))
scene = bpy.context.scene
scene.render.engine = "CYCLES"      # (works without a graphics card)
scene.cycles.device = "CPU"
scene.cycles.samples = 24
scene.render.resolution_x = scene.render.resolution_y = 520
scene.world = bpy.data.worlds.new("w")
scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.42, 0.55, 1)
scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.5
sun.rotation_euler = (0.9, 0.2, 0.6)
bpy.context.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.type = "ORTHO"
bpy.context.collection.objects.link(cam)
scene.camera = cam


def shoot(name, loc, target, scale, show):
    for ob in bpy.data.objects:
        if ob.name.startswith("Gilded"):
            ob.hide_render = ob.name not in show
    cam.data.ortho_scale = scale
    cam.location = Vector(loc)
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)


SWORD = ("GildedBlade", "GildedHilt")
shoot("face", (0.26, 0.0, 2.0), (0.26, 0, 0), 0.9, SWORD)
shoot("side", (0.26, -2.0, 0.0), (0.26, 0, 0), 0.9, SWORD)
shoot("three_quarter", (1.0, -1.4, 1.1), (0.26, 0, 0), 0.9, SWORD)
shoot("hilt", (0.0, -1.0, 0.9), (0.0, 0, 0), 0.3, SWORD)
shoot("scabbard", (1.0, -1.4, 1.1), (0.26, 0, 0), 0.9, ("GildedHilt", "GildedScabbard"))
print("done", OUT)
