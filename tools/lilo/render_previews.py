"""Renders preview pictures of Lilo from assets/lilo/lilo.blend: a few poses from different sides, and a contact sheet.

    python3 tools/lilo/render_previews.py [output folder]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else os.path.join(ROOT, "assets", "lilo", "previews")

SHOTS = [   # file, clip, seconds into it, camera direction (from the cat), height, distance
    ("sit_front", "sit", 0.0, (0.0, -1.0), 0.26, 0.95),
    ("sit_three_quarter", "sit", 0.0, (0.75, -0.9), 0.32, 1.05),
    ("idle_three_quarter", "idle", 0.0, (0.8, -0.8), 0.34, 1.1),
    ("walk_side", "walk", 0.35, (1.0, -0.05), 0.24, 1.15),
    ("run_side", "run", 0.1, (1.0, -0.05), 0.24, 1.2),
    ("jump_side", "jump", 0.45, (1.0, -0.2), 0.45, 1.5),
    ("talk_front", "talk", 0.15, (0.3, -1.0), 0.3, 0.85),
    ("groom", "groom", 0.4, (0.6, -1.0), 0.3, 0.95),
    ("sleep", "sleep", 0.0, (0.7, -0.8), 0.45, 0.95),
    ("stretch_side", "stretch", 1.0, (1.0, -0.15), 0.26, 1.15),
    ("pounce_side", "pounce", 0.5, (1.0, -0.3), 0.26, 1.15),
    ("happy", "happy", 0.15, (0.4, -1.0), 0.3, 1.0),
    ("back", "idle", 0.0, (-0.5, 1.0), 0.4, 1.1),
]


def setup(scene):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False
    scene.render.resolution_x = scene.render.resolution_y = 520
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Sky") if scene.world is None else scene.world
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.55, 0.72, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    sun_data = bpy.data.lights.new("Sun", "SUN")
    sun_data.energy = 3.2
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
    scene.collection.objects.link(sun)
    ground_me = bpy.data.meshes.new("Ground")
    s = 3.0
    ground_me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ground = bpy.data.objects.new("Ground", ground_me)
    gm = bpy.data.materials.new("Grass")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.19, 0.36, 0.13, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
    ground_me.materials.append(gm)
    scene.collection.objects.link(ground)
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


def aim(cam, target, direction, height, dist):
    d = Vector((direction[0], direction[1], 0)).normalized()
    cam.location = target + d * dist + Vector((0, 0, height))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()


def main():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "lilo", "lilo.blend"))
    scene = bpy.context.scene
    rig = bpy.data.objects["LiloRig"]
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    cam = setup(scene)
    os.makedirs(OUT, exist_ok=True)
    files = []
    only = os.environ.get("LILO_SHOTS", "").split(",") if os.environ.get("LILO_SHOTS") else None
    for name, clip, sec, direction, height, dist in SHOTS:
        if only and name not in only:
            continue
        rig.animation_data.action = bpy.data.actions[clip]
        scene.frame_set(int(round(sec * scene.render.fps)))
        aim(cam, Vector((0, -0.04, 0.2)), direction, height, dist)
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
    tile = 260
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
