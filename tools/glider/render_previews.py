"""Renders the glider from four sides into one contact sheet: python3.11 tools/glider/render_previews.py [folder]"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = sys.argv[-1] if len(sys.argv) > 1 and os.path.isdir(sys.argv[-1]) else os.path.join(ROOT, "assets", "glider", "previews")
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "glider", "glider.blend"))
scene = bpy.context.scene
scene.render.engine = "CYCLES"      # (works without a graphics card)
scene.cycles.device = "CPU"
scene.cycles.samples = 24
scene.render.resolution_x = scene.render.resolution_y = 480
scene.world = bpy.data.worlds.new("w")
scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.62, 0.78, 0.95, 1)
scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.6
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.0
sun.rotation_euler = (0.9, 0.2, 0.6)
bpy.context.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
bpy.context.collection.objects.link(cam)
scene.camera = cam
target = Vector((0, 0, 0.45))
views = {"front": (0, -3.2, 0.8), "side": (3.4, 0.0, 0.7), "top": (0.01, -0.4, 3.6), "three_quarter": (2.4, -2.6, 1.6)}
for name, loc in views.items():
    cam.location = Vector(loc)
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
print("done", OUT)
