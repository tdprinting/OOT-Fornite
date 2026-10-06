"""Renders preview pictures of the toys in assets/avriella/toys.blend (needs bpy and Pillow): python3 tools/avriella/render_toys.py [out folder]"""
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "avriella", "previews")
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "avriella", "toys.blend"))
sc = bpy.context.scene
sc.render.engine = "CYCLES"
sc.cycles.samples = 24
sc.cycles.device = "CPU"
sc.render.resolution_x, sc.render.resolution_y = 480, 480
w = bpy.data.worlds.new("w"); w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.78, 0.86, 0.95, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 1.1
sc.world = w
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
sc.collection.objects.link(sun)
floor = bpy.data.objects.new("floor", bpy.data.meshes.new("floor"))
floor.data.from_pydata([(-3, -3, -0.002), (3, -3, -0.002), (3, 3, -0.002), (-3, 3, -0.002)], [], [(0, 1, 2, 3)])
sc.collection.objects.link(floor)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam)
sc.camera = cam
shots = {   # name: (look-at, camera position)
    "plush-front": ((0, 0, 0.23), (0.0, -0.95, 0.38)), "plush-side": ((0, 0, 0.23), (0.95, -0.3, 0.38)), "plush-back": ((0, 0, 0.23), (-0.4, 0.9, 0.4)),
    "tv-front": ((0.6, 0, 0.2), (0.35, -1.05, 0.5)), "tv-side": ((0.6, 0, 0.2), (1.5, -0.7, 0.45)),
    "laptop-open": ((1.2, 0, 0.1), (1.0, -0.8, 0.5)), "laptop-back": ((1.2, 0, 0.1), (1.5, 0.8, 0.45))}
for name, (at, pos) in shots.items():
    cam.location = pos
    cam.rotation_euler = (Vector(at) - Vector(pos)).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
from PIL import Image
names = list(shots)
sheet = Image.new("RGB", (480 * 4, 480 * 2), (255, 255, 255))
for i, n in enumerate(names):
    sheet.paste(Image.open(os.path.join(OUT, n + ".png")).convert("RGB"), (480 * (i % 4), 480 * (i // 4)))
sheet.save(os.path.join(OUT, "toys-contact-sheet.png"))
print("done")
