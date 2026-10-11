"""Renders the Moonfall Bus previews from assets/bus/bus.blend: four angles and a short turntable GIF.

    python3 tools/bus/render_previews.py [out_dir] [--quick] [--gif]

Pictures go to docs/battle-bus/ by default (bus-front.png, bus-side.png, bus-rear.png, bus-below.png, bus-sheet.png, bus-turntable.gif). The light
is a warm sun and a blue sky fill, like the game's daytime sky; the textures are the game's, the shading is Blender's (the game bakes its own
light into the vertex colours, so in game it is a little flatter).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BLEND = os.path.join(ROOT, "assets", "bus", "bus.blend")
args = [a for a in sys.argv[1:] if not a.startswith("--")]
OUT = args[0] if args else os.path.join(ROOT, "docs", "battle-bus")
QUICK = "--quick" in sys.argv
GIF = "--gif" in sys.argv

# camera spots, in metres (Blender: the bus faces -Y, Z up); the target is the middle of bus and Moon
TARGET = Vector((0.0, 0.0, 6.0))
VIEWS = {
    "front": (Vector((-17.0, -22.0, 7.0)), 40.0),
    "side": (Vector((29.0, -1.5, 6.0)), 40.0),
    "rear": (Vector((16.0, 22.0, 10.5)), 40.0),
    "below": (Vector((-8.0, -12.5, -5.0)), 30.0),
}


def setup():
    bpy.ops.wm.open_mainfile(filepath=BLEND)
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = 12 if QUICK else 48
    scn.cycles.use_denoising = True
    scn.render.resolution_x, scn.render.resolution_y = (640, 480) if QUICK else (1280, 960)
    scn.render.film_transparent = False
    scn.view_settings.view_transform = "Standard"
    for name in ("BodyFar", "MoonFar"):
        bpy.data.objects[name].hide_render = True
    # the ropes: four stretched copies of the unit rope, between their two empties
    rope = bpy.data.objects["Rope"]
    rope.hide_render = True
    for k in range(4):
        a = bpy.data.objects["RopeBody%d" % k].location
        b = bpy.data.objects["RopeMoon%d" % k].location
        ob = rope.copy()
        ob.hide_render = False
        bpy.context.scene.collection.objects.link(ob)
        d = b - a
        ob.location = a
        ob.rotation_mode = "QUATERNION"
        ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
        ob.scale = (1, 1, 1)
        # the unit rope runs along game +Y, which is Blender +Z, one game unit (0.01 m) long
        ob.scale = (1.0, 1.0, d.length * 100.0)
    # the lanterns and wheels: copies at their hooks and axles
    lan = bpy.data.objects["Lantern"]
    lan.hide_render = True
    for k in range(8):
        ob = lan.copy()
        ob.hide_render = False
        bpy.context.scene.collection.objects.link(ob)
        ob.location = bpy.data.objects["Lantern%d" % k].location
    wh = bpy.data.objects["Wheel"]
    wh.hide_render = True
    for k in range(4):
        ob = wh.copy()
        ob.hide_render = False
        bpy.context.scene.collection.objects.link(ob)
        p = bpy.data.objects["Wheel%d" % k].location
        ob.location = p
        if p.x < 0:
            ob.rotation_euler = (0, 0, math.pi)
    # sky
    world = bpy.data.worlds.new("Sky")
    scn.world = world
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes["Background"]
    coord = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    nt.links.new(coord.outputs["Generated"], sep.inputs["Vector"])
    nt.links.new(sep.outputs["Z"], ramp.inputs["Fac"])
    ramp.color_ramp.elements[0].color = (0.92, 0.82, 0.68, 1)
    ramp.color_ramp.elements[1].color = (0.25, 0.48, 0.92, 1)
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[1].position = 0.35
    nt.links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.0
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 3.2
    sun.data.color = (1.0, 0.95, 0.85)
    sun.data.angle = math.radians(3)
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
    scn.collection.objects.link(sun)
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    scn.collection.objects.link(cam)
    scn.camera = cam
    return scn, cam


def aim(cam, pos, lens):
    cam.location = pos
    cam.data.lens = lens
    d = TARGET - pos
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def main():
    os.makedirs(OUT, exist_ok=True)
    scn, cam = setup()
    scn.frame_set(12)
    shots = []
    for name, (pos, lens) in VIEWS.items():
        aim(cam, pos, lens)
        path = os.path.join(OUT, "bus-%s.png" % name)
        scn.render.filepath = path
        bpy.ops.render.render(write_still=True)
        shots.append(path)
        print("rendered", path)
    from PIL import Image
    ims = [Image.open(p).convert("RGB") for p in shots]
    w, h = ims[0].size
    sheet = Image.new("RGB", (w, h))
    for i, im in enumerate(ims):
        sheet.paste(im.resize((w // 2, h // 2), Image.LANCZOS), ((i % 2) * (w // 2), (i // 2) * (h // 2)))
    sheet.save(os.path.join(OUT, "bus-sheet.png"))
    if GIF:
        scn.render.resolution_x, scn.render.resolution_y = 480, 360
        scn.cycles.samples = 10
        frames = []
        for k in range(24):
            a = 2 * math.pi * k / 24 - math.radians(130)
            aim(cam, Vector((24 * math.cos(a), 24 * math.sin(a), 6.0)), 35.0)
            scn.frame_set(1 + (k * 80 // 24))
            p = os.path.join(OUT, "_turn%02d.png" % k)
            scn.render.filepath = p
            bpy.ops.render.render(write_still=True)
            frames.append(Image.open(p).convert("RGB"))
            os.remove(p)
        frames[0].save(os.path.join(OUT, "bus-turntable.gif"), save_all=True, append_images=frames[1:], duration=120, loop=0)


if __name__ == "__main__":
    main()
