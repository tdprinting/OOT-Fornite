"""Renders preview pictures of Avriella from assets/avriella/avriella.blend: her clips from a few sides, and a contact sheet.

    python3 tools/avriella/render_previews.py [output folder]
    AVRIELLA_SHOTS=sit_front,crawl_side python3 tools/avriella/render_previews.py     (only some shots)
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else os.path.join(ROOT, "assets", "avriella", "previews")

# file, clip, seconds into it, face picture, camera direction (from her), camera height, distance, look-at height
SHOTS = [
    ("stand_front", "stand", 0.0, "smile", (0.0, -1.0), 0.32, 1.5, 0.3),
    ("sit_front", "sit", 0.0, "smile", (0.0, -1.0), 0.25, 1.1, 0.22),
    ("sit_three_quarter", "idle", 0.0, "smile", (0.75, -0.9), 0.3, 1.2, 0.22),
    ("sit_side", "sit", 0.5, "smile", (1.0, -0.05), 0.2, 1.2, 0.2),
    ("wave", "wave", 0.45, "smile", (0.35, -1.0), 0.3, 1.15, 0.24),
    ("giggle", "giggle", 0.15, "giggle", (0.3, -1.0), 0.28, 1.1, 0.22),
    ("clap", "clap", 0.2, "smile", (0.4, -1.0), 0.28, 1.1, 0.22),
    ("crawl_side", "crawl", 0.1, "smile", (1.0, -0.1), 0.2, 1.35, 0.16),
    ("crawl_three_quarter", "crawl", 0.5, "smile", (0.7, -0.9), 0.3, 1.3, 0.16),
    ("roll_tip", "roll", 0.15, "oh", (0.7, -0.9), 0.35, 1.5, 0.15),
    ("roll_mid", "roll", 1.0, "giggle", (0.5, -1.0), 0.35, 1.5, 0.12),
    ("nap", "nap", 0.0, "shut", (0.7, -0.8), 0.5, 1.4, 0.1),
    ("reach", "reach", 0.6, "oh", (0.6, -0.9), 0.3, 1.15, 0.2),
    ("babble", "babble", 0.15, "oh", (0.3, -1.0), 0.28, 1.1, 0.22),
    ("stack_1", "rocks", 0.5, "half", (0.35, -1.0), 0.5, 0.85, (-0.12, 0.1)),
    ("stack_2", "rocks", 1.0, "smile", (0.35, -1.0), 0.5, 0.85, (-0.12, 0.1)),
    ("stack_3", "rocks", 2.2, "giggle", (0.35, -1.0), 0.5, 0.85, (-0.12, 0.1)),
    ("stack_4", "rocks", 1.8, "smile", (0.35, -1.0), 0.5, 0.85, (-0.12, 0.1)),
    ("stack_5", "rocks", 2.8, "oh", (0.35, -1.0), 0.5, 0.85, (-0.12, 0.1)),
    ("tumble_a", "tumble", 0.0, "giggle", (0.2, -1.0), 0.5, 1.5, 0.12),
    ("tumble_b", "tumble", 0.45, "smile", (0.2, -1.0), 0.5, 1.5, 0.12),
    ("tumble_c", "tumble", 0.9, "giggle", (1.0, -0.3), 0.5, 1.5, 0.12),
    ("kick_a", "kick", 0.1, "giggle", (0.6, -1.0), 0.5, 1.4, 0.15),
    ("kick_b", "kick", 0.4, "giggle", (0.6, -1.0), 0.5, 1.4, 0.15),
    ("chew_a", "chew", 0.2, "smile", (0.4, -1.0), 0.3, 1.0, 0.22),
    ("chew_b", "chew", 1.0, "smile", (0.4, -1.0), 0.3, 1.0, 0.22),
    ("back", "idle", 0.0, "smile", (-0.5, 1.0), 0.3, 1.2, 0.22),
]


def setup(scene):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.cycles.use_denoising = False
    scene.render.resolution_x = scene.render.resolution_y = 520
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
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "assets", "avriella", "avriella.blend"))
    scene = bpy.context.scene
    rig = bpy.data.objects["AvriellaRig"]
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    face_node = None
    for m in bpy.data.materials:
        if m.name == "AvriellaFace":
            face_node = [n for n in m.node_tree.nodes if n.type == "TEX_IMAGE"][0]
    cam = setup(scene)
    os.makedirs(OUT, exist_ok=True)
    files = []
    only = os.environ.get("AVRIELLA_SHOTS", "").split(",") if os.environ.get("AVRIELLA_SHOTS") else None
    for name, clip, sec, face, direction, height, dist, look in SHOTS:
        if only and name not in only:
            continue
        rig.animation_data.action = bpy.data.actions[clip]
        face_node.image = bpy.data.images["avriella_face_" + face]
        scene.frame_set(int(round(sec * scene.render.fps)))
        aim(cam, Vector((0, look[0], look[1])) if isinstance(look, tuple) else Vector((0, -0.04, look)), direction, height, dist)
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
