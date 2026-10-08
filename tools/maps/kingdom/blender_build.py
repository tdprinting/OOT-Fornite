"""Builds assets/maps/hyrule_kingdom/hyrule_kingdom.blend (+ .glb) from the same World the game data is made from, and renders previews.

    python3.11 -I tools/maps/kingdom/blender_build.py [--only name,name] [--renders overview,castle,...] [--out dir] [--no-save]

(pip install bpy numpy pillow for python 3.11). One Blender metre = 100 game units; game (x, y, z) -> Blender (x, -z, y) / 100.
"""
import os, sys, math, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np
import bpy
from mathutils import Vector
import world as kworld, terrain, textures
from layout import *

ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
OUT = os.path.join(ROOT, 'assets', 'maps', 'hyrule_kingdom')

def B(p):
    return (p[0] / 100.0, -p[2] / 100.0, p[1] / 100.0)

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    return sc

COL = {}
def collection(name):
    if name in COL: return COL[name]
    c = bpy.data.collections.new(name); bpy.context.scene.collection.children.link(c); COL[name] = c; return c

def image_from(name, arr, folder):
    """arr: h x w x 3 uint8 -> packed Blender image, also saved as PNG for reference."""
    h, w_ = arr.shape[:2]
    im = bpy.data.images.new(name, width=w_, height=h)
    px = np.ones((h, w_, 4), dtype=np.float32); px[..., :3] = arr[::-1] / 255.0
    im.pixels.foreach_set(px.ravel())
    path = os.path.join(folder, name + '.png'); im.filepath_raw = path; im.file_format = 'PNG'; im.save(); im.pack()
    return im

def material(name, im, alpha=None, emission=0.0, interp='Linear'):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bs = nt.nodes.get('Principled BSDF')
    bs.inputs['Roughness'].default_value = 0.92
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = im; tex.interpolation = interp
    attr = nt.nodes.new('ShaderNodeVertexColor'); attr.layer_name = 'Light'
    mul = nt.nodes.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'; mul.inputs['Factor'].default_value = 1.0
    nt.links.new(tex.outputs['Color'], mul.inputs['A']); nt.links.new(attr.outputs['Color'], mul.inputs['B'])
    nt.links.new(mul.outputs['Result'], bs.inputs['Base Color'])
    if alpha is not None:
        bs.inputs['Alpha'].default_value = alpha
    if emission:
        bs.inputs['Emission Strength'].default_value = emission
        nt.links.new(mul.outputs['Result'], bs.inputs['Emission Color'])
    return m

def build_terrain(heights, T, n=385, clip_seabed=True):
    xs = np.linspace(-HALF_X, HALF_X, n); zs = np.linspace(-HALF_Z, HALF_Z, n)
    X, Z = np.meshgrid(xs, zs)
    Hh = terrain.grid_height(heights, X, Z)
    Hd = np.maximum(Hh, WATER_Y - 40) if clip_seabed else Hh # Riftlands keeps the actual bed   # the sea bed is drawn 40 under the water, like the game (kSeabedDrop)
    col, cover = terrain.paint(X, Z, Hh)
    verts = [B((X.flat[i], Hd.flat[i], Z.flat[i])) for i in range(n * n)]
    faces = []
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i
            faces.append((a, a + n, a + n + 1, a + 1))
    me = bpy.data.meshes.new('Island terrain'); me.from_pydata(verts, [], faces); me.update()
    ca = me.color_attributes.new('Light', 'FLOAT_COLOR', 'POINT')
    rgba = np.ones((n * n, 4), dtype=np.float32); rgba[:, :3] = (col.reshape(-1, 3) / 255.0) ** 2.2 * 1.18
    ca.data.foreach_set('color', rgba.ravel())
    uv = me.uv_layers.new(name='UVMap')
    loops_v = np.zeros(len(me.loops), dtype=np.int64); me.loops.foreach_get('vertex_index', loops_v)
    vx = np.array([v[0] for v in verts]); vy = np.array([v[1] for v in verts])
    uvs = np.stack([vx[loops_v] / 1.158, vy[loops_v] / 1.204], axis=1).astype(np.float32)   # the game's detail texture: two repeats per square
    uv.data.foreach_set('uv', uvs.ravel())
    o = bpy.data.objects.new('Island terrain', me); collection('Terrain').objects.link(o)
    for p in me.polygons: p.use_smooth = True
    o.data.materials.append(MATS['ground_detail'])
    # Water: one sheet at the water level
    s = 2 * HALF_X / 100 + 40
    wm = bpy.data.meshes.new('Water'); wm.from_pydata([(-s / 2, -s / 2, WATER_Y / 100), (s / 2, -s / 2, WATER_Y / 100), (s / 2, s / 2, WATER_Y / 100), (-s / 2, s / 2, WATER_Y / 100)], [], [(0, 1, 2, 3)])
    wo = bpy.data.objects.new('Water', wm); collection('Water').objects.link(wo)
    m = bpy.data.materials.new('water_sheet'); m.use_nodes = True
    bs = m.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (0.05, 0.32, 0.38, 1); bs.inputs['Roughness'].default_value = 0.08
    bs.inputs['Alpha'].default_value = 0.72
    wm.materials.append(m)
    return o

def build_structures(w):
    groups = {}
    for t in w.tris:
        groups.setdefault((t.group, t.mat), []).append(t)
    for (group, mat), tris in groups.items():
        verts = []; faces = []; uvs = []; cols = []
        for t in tris:
            k = len(verts)
            for p, uvv, sh in zip(t.p, t.uv, t.shade):
                verts.append(B(p)); uvs.append((uvv[0], -uvv[1])); cols.append((sh[0] ** 2.2, sh[1] ** 2.2, sh[2] ** 2.2, 1.0))
            faces.append((k, k + 1, k + 2))
        me = bpy.data.meshes.new(f'{group} {mat}'); me.from_pydata(verts, [], faces); me.update()
        uv = me.uv_layers.new(name='UVMap')
        uv.data.foreach_set('uv', np.array([uvs[i] for i in range(len(uvs))], dtype=np.float32).ravel())
        ca = me.color_attributes.new('Light', 'FLOAT_COLOR', 'CORNER')
        ca.data.foreach_set('color', np.array(cols, dtype=np.float32).ravel())
        o = bpy.data.objects.new(f'{group} {mat}', me); collection(group).objects.link(o)
        me.materials.append(MATS[mat])

def build_collision(w):
    me = bpy.data.meshes.new('Scene collision')
    me.from_pydata([B(v) for v in w.col_verts], [], [t[:3] for t in w.col_tris]); me.update()
    o = bpy.data.objects.new('Scene collision', me); collection('Collision (game)').objects.link(o)
    o.display_type = 'WIRE'; o.hide_render = True
    COL['Collision (game)'].hide_render = True

MATS = {}

def lights_and_world(sc):
    sun_d = bpy.data.lights.new('Sun', 'SUN'); sun_d.energy = 3.6; sun_d.color = (1.0, 0.9, 0.72); sun_d.angle = 0.05
    sun = bpy.data.objects.new('Sun', sun_d); collection('Presentation').objects.link(sun)
    sun.rotation_euler = (math.radians(48), math.radians(-18), math.radians(-35))
    wd = bpy.data.worlds.new('Hyrule sky'); sc.world = wd; wd.use_nodes = True
    nt = wd.node_tree; bg = nt.nodes.get('Background')
    sky = nt.nodes.new('ShaderNodeTexGradient'); sky.gradient_type = 'LINEAR'
    tc = nt.nodes.new('ShaderNodeTexCoord'); mp = nt.nodes.new('ShaderNodeMapping'); mp.inputs['Rotation'].default_value = (0, math.radians(-90), 0)
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (0.85, 0.82, 0.70, 1); ramp.color_ramp.elements[1].color = (0.18, 0.38, 0.85, 1)
    ramp.color_ramp.elements[0].position = 0.48; ramp.color_ramp.elements[1].position = 0.75
    nt.links.new(tc.outputs['Generated'] if False else tc.outputs['Object'], mp.inputs['Vector'])
    nt.links.new(mp.outputs['Vector'], sky.inputs['Vector']); nt.links.new(sky.outputs['Color'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color']); bg.inputs['Strength'].default_value = 0.9
    sc.render.engine = 'CYCLES'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
    try: sc.cycles.denoiser = 'OPENIMAGEDENOISE'
    except Exception: pass
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'; sc.view_settings.exposure = 0.35
    sc.render.film_transparent = False

def camera(name, eye, target, lens=28, ortho=None):
    cd = bpy.data.cameras.new(name); cd.lens = lens; cd.clip_end = 900; cd.clip_start = 0.1
    if ortho: cd.type = 'ORTHO'; cd.ortho_scale = ortho
    o = bpy.data.objects.new(name, cd); collection('Presentation').objects.link(o)
    o.location = B(eye); d = Vector(B(target)) - o.location
    o.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return o

def mist(sc):
    """Distance haze like the game's fog: a volume would be slow; use the compositor's mist pass instead."""
    sc.view_layers[0].use_pass_mist = True
    sc.world.mist_settings.start = 30; sc.world.mist_settings.depth = 160
    sc.use_nodes = True
    nt = sc.node_tree
    rl = nt.nodes.get('Render Layers'); comp = nt.nodes.get('Composite')
    mix = nt.nodes.new('CompositorNodeMixRGB'); mix.blend_type = 'MIX'; mix.inputs[2].default_value = (0.72, 0.80, 0.92, 1)
    nt.links.new(rl.outputs['Mist'], mix.inputs[0]); nt.links.new(rl.outputs['Image'], mix.inputs[1]); nt.links.new(mix.outputs[0], comp.inputs['Image'])

SHOTS = {
    'overview': ((9000, 9000, 14500), (0, 0, 300), 30),
    'top': ((0, 26000, 1), (0, 0, 0), None),
    'castle': ((1500, 1100, 2400), (0, 700, -1700), 30),
    'clocktown': ((1300, 700, 2400), (0, 250, 1100), 28),
    'lake': ((-1500, 900, 3600), (-3600, 0, 2300), 28),
    'desert': ((5600, 1100, 5600), (4100, 200, 3650), 28),
    'snowpeak': ((-1600, 1500, -3000), (-2600, 700, -4700), 28),
    'kakariko': ((2600, 1000, -300), (4100, 200, -1400), 28),
    'kokiri': ((-3600, 700, -100), (-5100, 200, -1200), 28),
    'ranch': ((3000, 1000, 5600), (1500, 250, 4550), 28),
    'mountain': ((2300, 2000, -2200), (4350, 1300, -4350), 28),
    'docks': ((-3600, 700, 6400), (-5100, -100, 5200), 28),
    'field': ((2600, 260, 1600), (3600, 100, -400), 26),
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default=None); ap.add_argument('--renders', default='overview')
    ap.add_argument('--out', default=OUT); ap.add_argument('--no-save', action='store_true')
    ap.add_argument('--res', default='1280x720'); ap.add_argument('--samples', type=int, default=24)
    ap.add_argument('--terrain-res', type=int, default=385)
    a = ap.parse_args([x for x in sys.argv[1:]])
    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    w, heights, T = kworld.build(a.only.split(',') if a.only else None)
    sc = reset()
    for c in ['Terrain', 'Water', 'Architecture', 'Props', 'Foliage', 'Presentation']: collection(c)
    texdir = os.path.join(a.out, 'textures'); os.makedirs(texdir, exist_ok=True)
    for name, (img, units) in T.items():
        im = image_from(name, textures.native_preview(img), texdir)
        MATS[name] = material(name, im)
    build_terrain(heights, T, a.terrain_res)
    build_structures(w)
    build_collision(w)
    lights_and_world(sc)
    sc.cycles.samples = a.samples
    try: mist(sc)
    except Exception as e: print('no mist pass', e)
    rx, ry = map(int, a.res.split('x')); sc.render.resolution_x = rx; sc.render.resolution_y = ry; sc.render.resolution_percentage = 100
    for shot in a.renders.split(','):
        if not shot: continue
        eye, target, lens = SHOTS[shot]
        cam = camera('Camera ' + shot, eye, target, lens or 28, ortho=(2 * HALF_X / 100 + 4) if lens is None else None)
        if lens is None:
            cam.location = (0, 0, 120); cam.rotation_euler = (0, 0, 0)
        sc.camera = cam
        sc.render.filepath = os.path.join(a.out, shot + '.png')
        bpy.ops.render.render(write_still=True)
        print('RENDERED', sc.render.filepath)
    if not a.no_save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'hyrule_kingdom.blend'), compress=True)

if __name__ == '__main__':
    main()
