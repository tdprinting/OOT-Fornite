"""Master Chief ally: low-poly N64/GameCube-era body, rigid 11-part rig, original Halo-CE-flavoured clips.
Run: python3 -I tools/chief/build_chief.py /path/to/scene.gltf [preview_dir]   (pip install bpy; or blender -b --python ... -- args)
Source model: "Master Cheif (Un-Fortnited)" by bizarrefog1, CC-BY-4.0 (see tools/chief/README.md). The source is not in git.
Writes shared/chief_model.h (same layout as shared/jones_model.h). Guns are Jones's (shared/jones_guns.h), attached to part 6.
"""
from pathlib import Path
from math import sin, cos, pi, exp
import sys
import bpy
import numpy as np
from mathutils import Vector, Matrix, geometry
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
SRC = args[0]
PREVIEW = Path(args[1]) if len(args) > 1 else None
SCALE = 1.25          # source is ~165 cm; the Spartan stands ~206 cm, a head over Jones
TEX = 64              # baseColor is box-filtered to 64x64 before sampling: console-era colour blocks
TARGET = {'Body': .043, 'Helmet': .075, 'Visor': .12}   # decimate ratios -> ~1300 triangles

PARTS = ['pelvis', 'torso', 'head', 'l_upperarm', 'l_forearm', 'r_upperarm', 'r_forearm', 'l_thigh', 'l_shin', 'r_thigh', 'r_shin']
PARENTS = [-1, 0, 1, 1, 3, 1, 5, 0, 7, 0, 9]
# Pivots in source space (cm, Z up, facing -Y; +X is his left).
SL, HL = Vector((21, 0, 124)), Vector((58, 0, 90))     # left shoulder, left hand in the source A-pose
PIV = [Vector((0, 0, 72)), Vector((0, 0, 86)), Vector((0, 0, 139)), SL, (SL + HL) / 2,
       Vector((-21, 0, 124)), Vector((-39.5, 0, 107)), Vector((10, 0, 70)), Vector((11, 0, 38)), Vector((-10, 0, 70)), Vector((-11, 0, 38))]
ARM_DROP = .75        # radians: A-pose arms are baked down to hang at his sides

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
bpy.context.view_layer.update()

def texture(mat):
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and 'baseColor' in n.image.name:
            im = n.image; w, h = im.size
            buf = np.empty(w * h * 4, np.float32); im.pixels.foreach_get(buf)
            a = buf.reshape(h, w, 4)[:, :, :3]
            return a.reshape(TEX, h // TEX, TEX, w // TEX, 3).mean(axis=(1, 3))
    return None

def classify(c):
    if c.z >= 139 and abs(c.x) < 15: return 2
    if abs(c.x) >= 21 and 78 <= c.z <= 141:
        left = c.x > 0; s = SL if left else Vector((-21, 0, 124)); hnd = HL if left else Vector((-58, 0, 90))
        d = hnd - s; t = (c - s).dot(d) / d.length_squared
        return (4 if t > .5 else 3) if left else (6 if t > .5 else 5)
    if c.z < 70: return (8 if c.z < 38 else 7) if c.x > 0 else (10 if c.z < 38 else 9)
    return 0 if c.z < 86 else 1

def arm_bake(part, v):
    if part in (3, 4): return SL + Matrix.Rotation(ARM_DROP, 3, 'Y') @ (v - SL)
    if part in (5, 6):
        s = Vector((-21, 0, 124)); return s + Matrix.Rotation(-ARM_DROP, 3, 'Y') @ (v - s)
    return v
for i in (4, 6): PIV[i] = arm_bake(i, PIV[i])

LIGHT = Vector((.35, -.45, .82)).normalized()
tris = [[] for _ in PARTS]   # per part: (v0,v1,v2, rgb)
for src in [o for o in bpy.data.objects if o.type == 'MESH']:
    kind = src.data.materials[0].name
    tex = texture(src.data.materials[0])
    me = src.data; me.calc_loop_triangles(); mw = src.matrix_world
    uv = me.uv_layers.active.data
    wv = [mw @ v.co for v in me.vertices]
    ltris = list(me.loop_triangles)
    bvh = BVHTree.FromPolygons(wv, [t.vertices[:] for t in ltris])
    def colour(p):
        loc, _, idx, _ = bvh.find_nearest(p)
        t = ltris[idx]; a, b, c = (wv[i] for i in t.vertices)
        ua, ub, uc = (Vector((*uv[l].uv, 0)) for l in t.loops)
        q = geometry.barycentric_transform(loc, a, b, c, ua, ub, uc)
        return tex[int(q.y * TEX) % TEX, int(q.x * TEX) % TEX]
    low = src.copy(); low.data = src.data.copy(); low.parent = None; bpy.context.collection.objects.link(low)
    low.data.transform(mw); low.matrix_world = Matrix.Identity(4)
    bpy.context.view_layer.objects.active = low
    for o in bpy.context.selected_objects: o.select_set(False)
    low.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=.4); bpy.ops.object.mode_set(mode='OBJECT')
    m = low.modifiers.new('dec', 'DECIMATE'); m.ratio = TARGET[kind]; m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier='dec')
    low.data.calc_loop_triangles(); lw = low.matrix_world
    for t in low.data.loop_triangles:
        vs = [lw @ low.data.vertices[i].co for i in t.vertices]
        cen = (vs[0] + vs[1] + vs[2]) / 3
        if kind == 'Visor': rgb = np.array([.93, .66, .16])          # the gold visor, flat and bright
        else:
            rgb = np.mean([colour(cen)] + [colour((cen + v) / 2) for v in vs], axis=0)
            rgb = np.clip(np.power(rgb, .8) * 1.08 + [0, .02, 0], 0, 1)   # a little punch, a touch greener
        part = 2 if kind != 'Body' else classify(cen)
        tris[part].append(([arm_bake(part, v) for v in vs], rgb))
    bpy.data.objects.remove(low)

# Gun mount: Jones's rifles are authored relative to his right forearm pivot with the grip at GRIP - PIVOT13.
JONES_GRIP = Vector((-.372 - -.315, -.065 - .005, .953 - 1.245)) * 100
rf = [v for vs, _ in tris[6] for v in vs]; zlo = min(v.z for v in rf)
hand = sum((v for v in rf if v.z < zlo + 9), Vector()) / max(1, sum(1 for v in rf if v.z < zlo + 9))
gun_off = (hand + Vector((0, -2, 3)) - PIV[6]) * SCALE - JONES_GRIP

def q5(c): return int(round(c * 31)) * 255 // 31   # RGBA5551-style colour steps
groups = [[] for _ in PARTS]; triangles = 0
for part, lst in enumerate(tris):
    for vs, rgb in lst:
        n = (vs[1] - vs[0]).cross(vs[2] - vs[0]); n = n.normalized() if n.length > 1e-9 else Vector((0, 0, 1))
        shade = .8 + .2 * abs(n.dot(LIGHT))
        col = [q5(min(1, c * shade)) for c in rgb]
        triangles += 1
        for v in vs:
            d = (v - PIV[part]) * SCALE
            groups[part].append((round(d.x * 16), round(d.z * 16), round(-d.y * 16), *col))

# ---- clips: original motion, Halo-CE-flavoured. Low key count; snappy holds. ----
P, T, H, LU, LF, RU, RF, LT, LS, RT, RS = range(11)
BASE = {RU: (-.35, 0, .25), RF: (-1.15, 0, 0), LU: (-.75, 0, -.45), LF: (-.9, 0, 0)}   # two-handed low ready
AIM = {RU: (-1.0, 0, .28), RF: (-.55, 0, 0), LU: (-1.2, 0, -.5), LF: (-.45, 0, 0), T: (.02, 0, .1), H: (-.04, 0, -.1)}
def bump(u, a, b): return sin(pi * min(1, max(0, (u - a) / (b - a))))
def hold(u, a, b): return min(1, max(0, (u - a) / (b - a)))
def snap(x): return round(x * 4) / 4      # quarter-step keys: console-era cadence
def pose(clip, u):
    out = {p: list(BASE.get(p, (0, 0, 0))) for p in range(11)}
    def add(p, x=0, y=0, z=0): out[p][0] += x; out[p][1] += y; out[p][2] += z
    if clip == 'idle':
        b = sin(2 * pi * u); add(T, .02 * b); add(H, -.015 * b)                    # breathing
        add(H, 0, 0, .45 * bump(u, .55, .85))                                       # look around
        k = bump(u, .15, .35); add(RF, -.25 * k); add(T, 0, 0, -.12 * k); add(H, .25 * k, 0, -.2 * k)   # weapon check
    elif clip == 'invite':
        add(H, .12 * bump(u, .2, .5)); add(LU, -.6, 0, .45); add(LF, -.7 + .35 * sin(2 * pi * u))
    elif clip in ('walk', 'run'):
        s = sin(2 * pi * u); run = clip == 'run'
        amp = .78 if run else .48
        add(LT, -s * amp); add(RT, s * amp)
        add(LS, (1.1 if run else .6) * max(0, sin(2 * pi * u + 1.2))); add(RS, (1.1 if run else .6) * max(0, sin(2 * pi * u + 1.2 + pi)))
        add(P, 0, 0, .05 * s); add(T, .22 if run else .07, 0, -.08 * s)              # planted hips, forward lean when running
        add(H, -.12 if run else -.04)
        add(RU, .04 * s); add(LU, .04 * s)
    elif clip == 'shoot':
        for p, r in AIM.items(): out[p] = list(r)
        k = exp(-u * 9); add(RU, .14 * k); add(LU, .1 * k); add(T, -.05 * k); add(H, -.04 * k)   # tight shoulder recoil
    elif clip == 'bash':
        w = hold(u, 0, .25) * (1 - hold(u, .25, .38)); s = bump(u, .25, .7)
        add(T, -.05 * w + .2 * s, 0, .35 * w - .45 * s)
        add(RU, .3 * w - 1.25 * s, 0, -.2 * s); add(RF, .45 * w + .6 * s); add(LU, -.3 * s); add(LF, .2 * s)
        add(LT, -.55 * s); add(LS, .35 * s); add(RT, .2 * s)                         # step in on the left foot
    elif clip == 'greet':
        k = bump(u, 0, 1); add(H, .3 * k); add(T, .06 * k)                           # one firm nod
    elif clip == 'hurt':
        k = bump(u, 0, .6); add(T, -.28 * k, 0, .1 * k); add(H, -.35 * k); add(RU, .2 * k); add(LU, .25 * k, 0, .2 * k); add(LT, -.15 * k)
    elif clip == 'death':
        k = hold(u, 0, .45); g = hold(u, .4, 1)
        add(T, -.5 * k - .55 * g); add(H, -.4 * k); add(LU, .6 * k, 0, .5 * g); add(RU, .9 * k, 0, -.6 * g); add(RF, .9 * k)
        add(LT, -1.1 * k - .2 * g); add(RT, -.9 * k - .4 * g); add(LS, 1.3 * k); add(RS, 1.0 * k + .3 * g); add(P, -.35 * g)
    if clip == 'death':
        for p in out: out[p] = [snap(x) for x in out[p]]
    return [tuple(out[p]) for p in range(11)]

CLIPS = [('idle', 3.2, True), ('invite', 1.6, True), ('walk', 1.0, True), ('run', .7, True), ('shoot', .5, False),
         ('bash', .6, False), ('greet', .8, False), ('hurt', .35, False), ('death', 1.2, False)]
FPS = 15              # the console cadence: 15 keys a second, linear between
samples = [[pose(c, f / round(d * FPS)) for f in range(round(d * FPS) + 1)] for c, d, _ in CLIPS]

lines = ['#pragma once', '// Generated by tools/chief/build_chief.py. Regenerate; do not edit arrays.',
         '// Body from "Master Cheif (Un-Fortnited)" by bizarrefog1 (CC-BY-4.0), decimated and recoloured; see tools/chief/README.md.',
         'namespace royale::chief_model {',
         'struct Corner { short x,y,z; unsigned char r,g,b; };', 'struct Part { unsigned first,count; short px,py,pz; };',
         'struct Rotation { float x,y,z; };', 'struct Clip { unsigned first,frames; float duration; bool loop; };',
         'enum { kIdle,kInvite,kWalk,kRun,kShoot,kBash,kGreet,kHurt,kDeath };', f'inline constexpr unsigned kPartCount={len(PARTS)};',
         'inline constexpr float kPositionScale=1.0f/16.0f;', 'inline constexpr unsigned kGunPart=6;',
         f'inline constexpr float kGunOffset[3]={{{gun_off.x:.2f}f,{gun_off.z:.2f}f,{-gun_off.y:.2f}f}};',
         'inline constexpr int kParents[] = {' + ','.join(map(str, PARENTS)) + '};', 'inline constexpr Part kParts[] = {']
first = 0
for p, g in zip(PIV, groups):
    q = p * SCALE; lines.append('  {' + ','.join(map(str, (first, len(g), round(q.x), round(q.z), round(-q.y)))) + '},'); first += len(g)
lines += ['};', 'inline constexpr Corner kCorners[] = {']
for g in groups: lines += ['  {' + ','.join(map(str, v)) + '},' for v in g]
lines += ['};', 'inline constexpr Clip kClips[] = {']; first = 0
for (_, d, loop), data in zip(CLIPS, samples):
    lines.append(f'  {{{first},{len(data)},{d}f,{str(loop).lower()}}},'); first += len(data) * len(PARTS)
lines += ['};', 'inline constexpr Rotation kRotations[] = {']
for data in samples:
    for frame in data:
        for rx, ry, rz in frame: lines.append('  {' + ','.join(f'{v:.4f}f' for v in (rx, rz, -ry)) + '},')   # Rx->Rx, Rz->Ry, Ry->-Rz
lines += ['};', f'inline constexpr unsigned kTriangleCount={triangles};', '}']
(ROOT / 'shared/chief_model.h').write_text('\n'.join(lines) + '\n')
print(f'Chief: {triangles} triangles, parts', [len(g) // 3 for g in groups])
assert 800 <= triangles <= 1600, triangles

if PREVIEW:   # rebuild the export as Blender objects and render a few poses
    for o in list(bpy.data.objects): bpy.data.objects.remove(o)
    ctrl = []
    for i, name in enumerate(PARTS):
        e = bpy.data.objects.new('rig_' + name, None); bpy.context.collection.objects.link(e)
        e.location = PIV[i] * SCALE / 100 if PARENTS[i] < 0 else (PIV[i] - PIV[PARENTS[i]]) * SCALE / 100
        if PARENTS[i] >= 0: e.parent = ctrl[PARENTS[i]]
        ctrl.append(e)
    mat = bpy.data.materials.new('vc'); mat.use_nodes = True
    nt = mat.node_tree; bsdf = nt.nodes['Principled BSDF']; attr = nt.nodes.new('ShaderNodeVertexColor'); attr.layer_name = 'col'
    em = nt.nodes.new('ShaderNodeEmission'); nt.links.new(attr.outputs[0], em.inputs[0])
    nt.links.new(em.outputs[0], nt.nodes['Material Output'].inputs[0])
    def mesh(name, corners, parent):
        verts = [Vector((c[0], -c[2], c[1])) / 1600 for c in corners]
        faces = [(i, i + 1, i + 2) for i in range(0, len(verts), 3)]
        me = bpy.data.meshes.new(name); me.from_pydata(verts, [], faces); me.update()
        ca = me.color_attributes.new('col', 'FLOAT_COLOR', 'CORNER')
        for li, c in enumerate(corners): ca.data[li].color = ((c[3] / 255) ** 2.2, (c[4] / 255) ** 2.2, (c[5] / 255) ** 2.2, 1)
        o = bpy.data.objects.new(name, me); bpy.context.collection.objects.link(o); o.parent = parent; me.materials.append(mat)
    for i, g in enumerate(groups): mesh('part_' + PARTS[i], g, ctrl[i])
    gh = ROOT / 'shared/jones_guns.h'
    if gh.exists():   # draw the tan assault rifle where the game will put it
        import re
        txt = gh.read_text(); guns = re.findall(r'\{(\d+),(\d+),(-?\d+),(-?\d+),(-?\d+)\}', txt.split('kCorners')[0])
        cs = [tuple(map(int, m)) for m in re.findall(r'\{(-?\d+),(-?\d+),(-?\d+),(\d+),(\d+),(\d+)\}', txt.split('kCorners')[1])]
        f0, n0 = int(guns[0][0]), int(guns[0][1]); o16 = [round(gun_off.x * 16), round(gun_off.z * 16), round(-gun_off.y * 16)]
        mesh('gun', [(c[0] + o16[0], c[1] + o16[1], c[2] + o16[2], *c[3:]) for c in cs[f0:f0 + n0]], ctrl[6])
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 8; sc.cycles.device = 'CPU'
    sc.world = bpy.data.worlds.new('w'); sc.world.color = (.04, .09, .1)
    sc.render.resolution_x, sc.render.resolution_y = 1600, 600; sc.view_settings.view_transform = 'Standard'
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); bpy.context.collection.objects.link(cam); sc.camera = cam
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 6.2; cam.location = (3.2, -8, 2.4)
    cam.rotation_euler = (Vector((0, 0, 1.0)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    shots = [('idle', 0), ('run', .25), ('shoot', .05), ('bash', .45), ('death', 1)]
    PREVIEW.mkdir(parents=True, exist_ok=True)
    for clip, u in [('rest', 0)] + shots:
        for i, c in enumerate(ctrl): c.rotation_euler = (0, 0, 0) if clip == 'rest' else pose(clip, u)[i]
        sc.render.resolution_x = 500; cam.data.ortho_scale = 2.6
        for view, at in [('front', (0, -8, 1.1)), ('side', (8, -1.5, 1.1))] if clip in ('rest', 'idle') else [('three', (3.5, -7, 1.6))]:
            cam.location = at; cam.rotation_euler = (Vector((0, 0, 1.0)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
            sc.render.filepath = str(PREVIEW / f'chief-{clip}-{view}.png'); bpy.ops.render.render(write_still=True)
