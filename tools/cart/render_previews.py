"""Renders preview pictures of the Lon Lon Buggy from assets/cart/cart.blend: a few sides, the wheels turned and the handlebar steered, and a
contact sheet.

    python3 tools/cart/render_previews.py [output folder]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else os.path.join(ROOT, "assets", "cart", "previews")

SHOTS = [   # file, camera direction (from the cart), height, distance, steering (degrees)
    ("three_quarter_front", (0.85, -1.0), 0.95, 2.6, 0),
    ("side", (1.0, 0.0), 0.55, 2.5, 0),
    ("front", (0.0, -1.0), 0.6, 2.3, 0),
    ("back", (-0.35, 1.0), 0.9, 2.4, 0),
    ("top", (0.25, -0.35), 2.4, 1.0, 0),
    ("steering_left", (-0.9, -1.0), 1.0, 2.5, 25),
    ("low_wheel", (1.0, -0.55), 0.18, 1.4, 25),
    ("rear_three_quarter", (-0.9, 0.8), 0.8, 2.6, -20),
]


def setup(scene):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False
    scene.render.resolution_x = scene.render.resolution_y = 520
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Sky")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.45, 0.58, 0.76, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 3.4
    sun.rotation_euler = (math.radians(48), math.radians(12), math.radians(-35))
    scene.collection.objects.link(sun)
    s = 6.0
    ground_me = bpy.data.meshes.new("Ground")
    ground_me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    gm = bpy.data.materials.new("Grass")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.22, 0.40, 0.15, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
    ground_me.materials.append(gm)
    scene.collection.objects.link(bpy.data.objects.new("Ground", ground_me))
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    cam.data.lens = 50
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


def aim(cam, target, direction, height, dist):
    d = Vector((direction[0], direction[1], 0))
    d = d.normalized() if d.length > 0 else Vector((0, -1, 0))
    cam.location = target + d * dist + Vector((0, 0, height))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()


def main():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "cart", "cart.blend"))
    scene = bpy.context.scene
    cam = setup(scene)
    os.makedirs(OUT, exist_ok=True)
    files = []
    for name, direction, height, dist, steer in SHOTS:
        for w in ("WheelFL", "WheelFR"):
            bpy.data.objects[w].rotation_euler = (math.radians(30), 0, math.radians(steer))
        for w in ("WheelBL", "WheelBR"):
            bpy.data.objects[w].rotation_euler = (math.radians(30), 0, 0)
        bpy.data.objects["Handlebar"].rotation_euler = (0, 0, math.radians(steer * 0.8))
        aim(cam, Vector((0, -0.05, 0.42)), direction, height, dist)
        scene.render.filepath = os.path.join(OUT, name + ".png")
        bpy.ops.render.render(write_still=True)
        files.append((name, scene.render.filepath))
        print("rendered", name)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    cols = 4
    rows = (len(files) + cols - 1) // cols
    tile = 300
    sheet = Image.new("RGB", (cols * tile, rows * (tile + 22)), (24, 24, 30))
    draw = ImageDraw.Draw(sheet)
    for i, (name, path) in enumerate(files):
        im = Image.open(path).resize((tile, tile))
        x, y = (i % cols) * tile, (i // cols) * (tile + 22)
        sheet.paste(im, (x, y))
        draw.text((x + 6, y + tile + 4), name.replace("_", " "), fill=(235, 235, 235))
    sheet.save(os.path.join(OUT, "contact_sheet.png"))


if __name__ == "__main__":
    main()
