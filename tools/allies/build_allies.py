"""Build the four hireable allies (Kokiri, Zora, Goron, Gerudo): original low-poly Ocarina-of-Time-style models, one shared 18-bone rig and
eight animation clips each.   Run: python tools/allies/build_allies.py   (needs `pip install bpy==4.2.0`; writes assets/allies/*.blend)

No ROM, downloaded model or texture is used. Colours come from the art notes (art-style/art-and-artifacts-notes.md): Kokiri green, Goron ochre,
Zora silver-blue, Gerudo tan skin with pink pants and a red ponytail. Flat painted colour cells plus a small painted face, like the originals.
Blender: Z up, the character faces -Y (the exporter turns that into the game's +Z). Its own left is -X.
"""
from pathlib import Path
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/allies'
KINDS = ['kokiri', 'zora', 'goron', 'gerudo']
ATLAS = 64                      # atlas is 64 x 64: colour cells on top, a 64 x 32 face below
FACES = ['normal', 'blink', 'fierce']
# name, seconds, loops, fps
CLIPS = [('idle', 3.2, True, 12), ('walk', .9, True, 24), ('invite', 1.6, True, 15), ('attack', .9, False, 30),
         ('attack2', .9, False, 30), ('heal', 1.2, False, 20), ('hurt', .45, False, 30), ('cheer', 1.2, False, 20),
         ('rest', 3.0, True, 10), ('rise', .9, False, 24)]
BONES = ['root', 'hips', 'chest', 'head', 'arm_l', 'fore_l', 'hand_l', 'arm_r', 'fore_r', 'hand_r', 'leg_l', 'shin_l', 'foot_l',
         'leg_r', 'shin_r', 'foot_r', 'tail1', 'tail2']
TAU = math.tau
GORON_BALL_SHIFT = (0, .45, -.05)


def frames_of(seconds, loops, fps):
    return round(seconds * fps) + (0 if loops else 1)


# ---- dimensions (metres, standing on z = 0) ----------------------------------------------------------------------------------------
DIM = {
    'kokiri': dict(H=.62, hipZ=.25, kneeZ=.14, ankleZ=.045, hipX=.045, shZ=.43, shX=.085, elZ=.34, wrZ=.265, handEnd=.225, neckZ=.44,
                   chestZ=.36, headZ=.52, headTop=.64, foot=.07,
                   t1=((0, .06, .615), (0, .115, .665)), t2=((0, .115, .665), (0, .17, .64))),
    'zora': dict(H=1.05, hipZ=.50, kneeZ=.27, ankleZ=.05, hipX=.06, shZ=.86, shX=.135, elZ=.68, wrZ=.51, handEnd=.43, neckZ=.9,
                 chestZ=.72, headZ=1.0, headTop=1.1, foot=.12,
                 t1=((0, .08, 1.03), (0, .18, .95)), t2=((0, .18, .95), (0, .23, .7))),
    'goron': dict(H=.95, hipZ=.32, kneeZ=.18, ankleZ=.05, hipX=.10, shZ=.73, shX=.27, elZ=.52, wrZ=.30, handEnd=.16, neckZ=.76,
                  chestZ=.58, headZ=.84, headTop=.99, foot=.12,
                  t1=((0, .08, .9), (0, .14, .9)), t2=((0, .14, .9), (0, .2, .9))),
    'gerudo': dict(H=1.1, hipZ=.53, kneeZ=.29, ankleZ=.06, hipX=.07, shZ=.89, shX=.13, elZ=.68, wrZ=.49, handEnd=.40, neckZ=.93,
                   chestZ=.75, headZ=1.0, headTop=1.1, foot=.1,
                   t1=((0, .08, 1.07), (0, .2, .97)), t2=((0, .2, .97), (0, .27, .7))),
}
# Where the walk is quick and how much each kind sways, bounces, swings its arms and legs, and how fast a full cycle (two steps) covers ground.
GAIT = {
    'kokiri': dict(leg=.62, arm=.55, knee=.9, lean=.10, bob=.018, twist=.12, speed=120.0),
    'zora': dict(leg=.5, arm=.42, knee=.8, lean=.06, bob=.020, twist=.14, speed=118.0),
    'goron': dict(leg=.38, arm=.3, knee=.55, lean=.04, bob=.016, twist=.10, speed=100.0),
    'gerudo': dict(leg=.52, arm=.42, knee=.8, lean=.05, bob=.016, twist=.16, speed=128.0),
}

# ---- palettes ------------------------------------------------------------------------------------------------------------------------
PALETTE = {
    'kokiri': dict(skin=(.95, .78, .60), skin2=(.86, .64, .48), tunic=(.20, .54, .16), tunic2=(.14, .40, .12), boot=(.48, .31, .17),
                   belt=(.40, .27, .14), gold=(.95, .78, .22), hair=(.55, .34, .15), band=(.80, .18, .14), wood=(.55, .36, .18), eye=(.1, .06, .04)),
    'zora': dict(skin=(.66, .82, .92), skin2=(.37, .62, .79), belly=(.88, .95, .98), fin=(.45, .72, .88), fin2=(.30, .52, .72), dark=(.20, .36, .55),
                 gold=(.95, .78, .22), eye=(.05, .06, .12)),
    'goron': dict(skin=(.78, .58, .24), skin2=(.56, .38, .15), skin3=(.66, .47, .19), band=(.38, .24, .12), cloth=(.46, .20, .12), cloth2=(.30, .13, .09),
                  hair=(.72, .70, .66), hair2=(.50, .48, .46), eye=(.08, .05, .03), tooth=(.92, .88, .78)),
    'gerudo': dict(skin=(.80, .54, .33), skin2=(.66, .42, .25), pink=(.93, .60, .72), pink2=(.80, .42, .58), cream=(.96, .90, .78), gold=(.96, .80, .24),
                   gold2=(.78, .58, .12), red=(.82, .20, .12), red2=(.58, .10, .07), shoe=(.80, .14, .12), wood=(.50, .30, .14), eye=(.07, .04, .03),
                   ruby=(.85, .1, .2), feather=(.95, .92, .85)),
}


class Palette:
    """Flat colour cells: each named colour gets an 8 x 8 cell in the top half of the atlas."""
    def __init__(self, kind):
        self.kind, self.cols, self.index = kind, PALETTE[kind], {}

    def uv(self, name):
        if name not in self.index:
            self.index[name] = len(self.index)
        i = self.index[name]
        c, r = i % 8, i // 8
        assert r < 4, 'palette full'
        return ((c + .5) / 8.0, 1.0 - (r * 8 + 4) / ATLAS)


# ---- geometry helpers ----------------------------------------------------------------------------------------------------------------
parts = []
FRAME = Matrix.Identity(4)       # applied to every part: used to build props in the pose they are held in
PAL = None
N_SEG = 8


def _prep(o, name, bone, color, smooth=True, weights=None):
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    o.name = name
    bpy.context.view_layer.update()
    o.matrix_world = FRAME @ o.matrix_world
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if smooth:
        bpy.ops.object.shade_smooth()
    u = PAL.uv(color)
    if o.data.uv_layers.active is None:
        o.data.uv_layers.new()
    for uv in o.data.uv_layers.active.data:
        uv.uv = u
    if weights is None:
        g = o.vertex_groups.new(name=bone)
        g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    else:
        for v in o.data.vertices:
            for b, w in weights(Vector(v.co)).items():
                if w > 0.001:
                    g = o.vertex_groups.get(b) or o.vertex_groups.new(name=b)
                    g.add([v.index], w, 'REPLACE')
    o.select_set(False)
    parts.append(o)
    return o


def ell(bone, c, s, color, seg=N_SEG, rings=5, rot=(0, 0, 0), smooth=True):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, location=c)
    o = bpy.context.object
    o.scale = s
    o.rotation_euler = rot
    return _prep(o, 'ell', bone, color, smooth)


def box(bone, c, s, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=c)
    o = bpy.context.object
    o.scale = s
    o.rotation_euler = rot
    return _prep(o, 'box', bone, color, smooth=False)


def cone(bone, a, b, r0, r1, color, verts=N_SEG, smooth=True):
    """A tapered cylinder from a (radius r0) to b (radius r1)."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r0, radius2=r1, depth=d.length, location=(a + b) * .5, end_fill_type='NGON')
    o = bpy.context.object
    o.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return _prep(o, 'cone', bone, color, smooth)


def limb(bone, a, b, r0, r1, color, cap=True):
    cone(bone, a, b, r0, r1, color)
    if cap:
        ell(bone, b, (r1, r1, r1), color, 8, 4)


def prism(bone, pts, normal, thick, color, weights=None):
    """A flat fin: a convex polygon `pts` pushed out `thick` along `normal`."""
    n = Vector(normal).normalized() * thick * .5
    verts = [Vector(p) + n for p in pts] + [Vector(p) - n for p in pts]
    k = len(pts)
    faces = [tuple(range(k)), tuple(range(2 * k - 1, k - 1, -1))]
    for i in range(k):
        j = (i + 1) % k
        faces.append((i, j, k + j, k + i))
    me = bpy.data.meshes.new('fin')
    me.from_pydata([tuple(v) for v in verts], [], faces)
    me.update()
    o = bpy.data.objects.new('fin', me)
    bpy.context.collection.objects.link(o)
    me.uv_layers.new()
    return _prep(o, 'fin', bone, color, smooth=False, weights=weights)


def face_patch(bone, centre, radii, az=50.0, el=25.0, nx=8, ny=5, lift=1.03):
    """The painted face: a grid laid on the head ellipsoid and mapped onto the face half of the atlas."""
    cx, cy, cz = centre
    rx, ry, rz = radii
    verts, uvs, faces = [], [], []
    for j in range(ny + 1):
        for i in range(nx + 1):
            a = math.radians(az * (2.0 * i / nx - 1.0))
            e = math.radians(el * (1.0 - 2.0 * j / ny))
            verts.append((cx + rx * lift * math.cos(e) * math.sin(a), cy - ry * lift * math.cos(e) * math.cos(a), cz + rz * lift * math.sin(e)))
            uvs.append((i / nx, 0.5 - 0.5 * j / ny))
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            faces.append((a, a + nx + 1, a + nx + 2, a + 1))
    me = bpy.data.meshes.new('face')
    me.from_pydata(verts, [], faces)
    me.update()
    uvl = me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            uvl.data[li].uv = uvs[me.loops[li].vertex_index]
    o = bpy.data.objects.new('face', me)
    bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    g = o.vertex_groups.new(name=bone)
    g.add(list(range(len(me.vertices))), 1.0, 'REPLACE')
    bpy.ops.object.shade_smooth()
    o.select_set(False)
    parts.append(o)
    return o


def mirrored(fn):
    """Call fn(side, name_suffix) for the left (-X) and right (+X) side."""
    fn(-1, 'l')
    fn(1, 'r')


def held(bone_pos):
    """Build a prop as it looks with the arm raised straight forward: this puts it back on the hanging hand."""
    return Matrix.Translation(bone_pos) @ Matrix.Rotation(math.radians(90), 4, 'X')


# ---- faces ---------------------------------------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, w, h, base):
        self.w, self.h = w, h
        self.px = [[base for _ in range(w)] for _ in range(h)]

    def ellipse(self, cx, cy, rx, ry, col):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                if 0 <= x < self.w and 0 <= y < self.h and ((x - cx) / max(rx, .01)) ** 2 + ((y - cy) / max(ry, .01)) ** 2 <= 1.0:
                    self.px[y][x] = col

    def line(self, x0, y0, x1, y1, col, th=1.0):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            self.ellipse(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, th * .5, th * .5, col)


def paint_face(kind, expr):
    c = PALETTE[kind]
    skin = c['skin']
    cv = Canvas(64, 32, skin)
    eyeX, eyeY = 18.0, 13.0
    white, dark = (.96, .96, .94), c['eye']
    for sx in (eyeX, 64 - eyeX):
        side = -1 if sx < 32 else 1
        if kind == 'kokiri':
            if expr == 'blink':
                cv.line(sx - 5, eyeY + 1, sx + 5, eyeY + 1, dark, 1.6)
            else:
                cv.ellipse(sx, eyeY, 5.2, 6.6, white)
                cv.ellipse(sx + side * -.5, eyeY + .8, 3.6, 5.0, (.30, .50, .22))
                cv.ellipse(sx + side * -.5, eyeY + .8, 2.2, 3.4, dark)
                cv.ellipse(sx - 1.8, eyeY - 1.6, 1.3, 1.3, white)
                if expr == 'fierce':
                    cv.line(sx - 6 * side * -1, eyeY - 8 + (3 if side else 0), sx + 6 * side * -1, eyeY - 7, c['hair'], 1.6)
        elif kind == 'zora':
            if expr == 'blink':
                cv.line(sx - 5, eyeY + 1, sx + 5, eyeY + 1, dark, 1.4)
            else:
                cv.ellipse(sx, eyeY, 5.4, 4.4 if expr != 'fierce' else 3.2, dark)
                cv.ellipse(sx - 1.2, eyeY - 1.0, 1.4, 1.2, (.88, .95, 1.0))
                cv.ellipse(sx + 1.8, eyeY + .8, 1.8, 1.4, (.55, .15, .25))
        elif kind == 'goron':
            if expr == 'blink':
                cv.line(sx - 4, eyeY + 2, sx + 4, eyeY + 2, dark, 1.6)
            else:
                cv.ellipse(sx, eyeY + 1.5, 3.4, 2.8, white)
                cv.ellipse(sx, eyeY + 1.8, 2.0, 2.0, dark)
            top = eyeY - 1.0 + (2.6 if expr == 'fierce' else 0.0)   # heavy brow ridge, angry when fierce
            cv.line(sx - 6, top - 1.0 - (2.2 * side if expr == 'fierce' else 0) * -1, sx + 6, top - 1.0 + (2.2 * side if expr == 'fierce' else 0) * -1,
                    c['skin2'], 3.4)
        else:   # gerudo: lined amber eyes
            if expr == 'blink':
                cv.line(sx - 5.5, eyeY + 1, sx + 5.5, eyeY + 1, dark, 1.8)
            else:
                cv.ellipse(sx, eyeY, 5.6, 3.6 if expr != 'fierce' else 2.8, white)
                cv.ellipse(sx, eyeY + .2, 2.9, 3.0 if expr != 'fierce' else 2.4, (.78, .55, .10))
                cv.ellipse(sx, eyeY + .2, 1.5, 1.6, dark)
                cv.line(sx - 6.5, eyeY - 3.6, sx + 6.5 * (1 if side > 0 else 1), eyeY - 3.4 + (1.2 if expr == 'fierce' else 0), dark, 1.5)
                cv.line(sx + (6.5 if side < 0 else -6.5), eyeY - 1.0, sx + (9.0 if side < 0 else -9.0), eyeY - 2.8, dark, 1.2)
                cv.line(sx - 5, eyeY - 6.0 + (1.8 if expr == 'fierce' else 0), sx + 5, eyeY - 6.0 - (1.8 if expr == 'fierce' else 0) * 0, c['red2'], 1.4)
    # mouth
    mx, my = 32.0, 24.0
    if kind == 'kokiri':
        cv.line(mx - 4, my, mx + 4, my, (.65, .30, .25), 1.2)
        if expr != 'fierce':
            cv.line(mx - 4, my, mx - 5, my - 1.5, (.65, .30, .25), 1.0)
            cv.line(mx + 4, my, mx + 5, my - 1.5, (.65, .30, .25), 1.0)
    elif kind == 'zora':
        cv.line(mx - 6, my, mx + 6, my, (.30, .45, .62), 1.0)
    elif kind == 'goron':
        cv.line(mx - 8, my, mx + 8, my + (1.2 if expr == 'fierce' else -.4), c['skin2'], 1.8)
        if expr == 'fierce':
            cv.ellipse(mx - 3.5, my + 1.4, 1.1, 1.5, c['tooth'])
            cv.ellipse(mx + 3.5, my + 1.4, 1.1, 1.5, c['tooth'])
    else:
        cv.ellipse(mx, my, 4.2 if expr != 'fierce' else 3.6, 1.6, (.78, .16, .20))
        cv.line(mx - 4, my, mx + 4, my, (.55, .08, .12), .8)
    return cv


def make_atlas(kind, expr, pal):
    img = bpy.data.images.new('ally_%s_%s' % (kind, expr), width=ATLAS, height=ATLAS, alpha=True)
    img.use_fake_user = True
    px = [[(0, 0, 0)] * ATLAS for _ in range(ATLAS)]     # top row first
    cols = PALETTE[kind]
    noise_amp = .045 if kind == 'goron' else .02
    for name, i in pal.index.items():
        c, r = i % 8, i // 8
        base = cols[name]
        for y in range(8):
            for x in range(8):
                n = (((x * 7 + y * 13 + i * 5) * 2654435761) % 1000) / 1000.0 - .5
                px[r * 8 + y][c * 8 + x] = tuple(max(0, min(1, ch * (1 + n * noise_amp * 2))) for ch in base)
    cv = paint_face(kind, expr)
    for y in range(32):
        for x in range(64):
            px[32 + y][x] = cv.px[y][x]
    flat = []
    for row in range(ATLAS - 1, -1, -1):          # Blender stores the bottom row first
        for x in range(ATLAS):
            flat.extend((*px[row][x], 1.0))
    img.pixels = flat
    OUT.mkdir(parents=True, exist_ok=True)
    img.filepath_raw = str(OUT / ('%s_atlas_%s.png' % (kind, expr)))
    img.file_format = 'PNG'
    img.save()
    img.pack()
    return img


# ---- the four models -----------------------------------------------------------------------------------------------------------------
def legs_and_arms(d, skin_leg, skin_arm, thigh, shin, arm_r, fore_r, hand_r, sleeve=None, boots=None, foot_color=None, foot_s=None):
    """Plain limbs for every kind; `thigh`, `shin` ... are radii."""
    def go(s, sfx):
        x = s * d['hipX']
        limb('leg_' + sfx, (x, 0, d['hipZ']), (x, 0, d['kneeZ']), thigh, shin, skin_leg)
        limb('shin_' + sfx, (x, 0, d['kneeZ']), (x, 0, d['ankleZ'] + .01), shin, shin * .85, boots or skin_leg)
        sx = s * d['shX']
        limb('arm_' + sfx, (sx, 0, d['shZ']), (sx, 0, d['elZ']), arm_r, arm_r * .9, sleeve or skin_arm)
        limb('fore_' + sfx, (sx, 0, d['elZ']), (sx, 0, d['wrZ']), fore_r, fore_r * .85, skin_arm)
        ell('hand_' + sfx, (sx, -.005, d['wrZ'] - hand_r * .6), (hand_r, hand_r * .85, hand_r * 1.15), skin_arm)
        fl = d['foot']
        fs = foot_s or (.04, .07, .03)
        ell('foot_' + sfx, (x, -fl * .45, d['ankleZ'] - .005), fs, foot_color or boots or skin_leg)
    mirrored(go)


def build_kokiri(P):
    d = DIM['kokiri']
    legs_and_arms(d, 'skin', 'skin', .026, .022, .024, .02, .022, sleeve='tunic', boots='boot', foot_color='boot', foot_s=(.036, .062, .03))
    for s, sfx in ((-1, 'l'), (1, 'r')):    # green sleeves and boot tops
        ell('arm_' + sfx, (s * d['shX'], 0, d['shZ'] - .01), (.03, .03, .03), 'tunic')
    ell('chest', (0, 0, d['chestZ']), (.078, .06, .095), 'tunic')
    cone('hips', (0, 0, .3), (0, 0, .19), .09, .125, 'tunic')                  # the tunic's flared skirt
    cone('hips', (0, 0, .30), (0, 0, .27), .092, .092, 'belt')
    box('chest', (0, -.085, .292), (.03, .01, .024), 'gold')
    ell('hips', (0, 0, .24), (.085, .07, .07), 'tunic2')
    cone('chest', (0, 0, .42), (0, 0, .46), .05, .035, 'skin')                    # neck
    ell('head', (0, 0, d['headZ']), (.125, .112, .118), 'skin', 10, 6)
    face_patch('head', (0, 0, d['headZ']), (.125, .112, .118))
    ell('head', (0, -.113, d['headZ'] - .018), (.02, .02, .02), 'skin2', 6, 4)    # nose
    for s in (-1, 1):
        cone('head', (s * .112, 0, d['headZ'] - .005), (s * .205, .02, d['headZ'] + .045), .026, .003, 'skin', 6)   # pointed ears
        ell('head', (s * .105, -.02, d['headZ'] - .03), (.02, .03, .045), 'hair', 6, 4)                               # side hair
    ell('head', (0, .035, d['headZ'] - .015), (.118, .1, .1), 'hair', 8, 5)       # hair under the hat
    cone('head', (0, .0, d['headZ'] + .075), (0, .062, d['headZ'] + .165), .112, .05, 'tunic')                       # the pointed hat
    cone('head', (0, .0, d['headZ'] + .07), (0, 0, d['headZ'] + .09), .128, .11, 'tunic2')                           # hat brim
    cone('tail1', d['t1'][0], d['t1'][1], .05, .034, 'tunic')
    cone('tail2', d['t2'][0], d['t2'][1], .034, .004, 'tunic')
    ell('tail2', d['t2'][1], (.018, .018, .018), 'gold', 6, 4)
    # slingshot in the left hand
    global FRAME
    hp = Vector((-d['shX'], 0, d['wrZ'] - .02))
    FRAME = held(hp)
    cone('hand_l', (0, 0, -.03), (0, 0, .07), .009, .008, 'wood', 6)                                                # the handle, upright in the fist
    cone('hand_l', (0, 0, .065), (-.032, 0, .12), .008, .006, 'wood', 6)
    cone('hand_l', (0, 0, .065), (.032, 0, .12), .008, .006, 'wood', 6)
    box('hand_l', (0, .004, .118), (.064, .005, .006), 'band')
    FRAME = Matrix.Identity(4)


def build_zora(P):
    d = DIM['zora']
    legs_and_arms(d, 'skin', 'skin', .048, .034, .036, .028, .03, foot_color='skin2', foot_s=(.05, .11, .016))
    for s, sfx in ((-1, 'l'), (1, 'r')):
        ell('leg_' + sfx, (s * d['hipX'], 0, .42), (.055, .055, .09), 'skin')                  # thighs
        ell('arm_' + sfx, (s * d['shX'], 0, d['shZ'] - .02), (.05, .05, .05), 'skin2')         # shoulders
        prism('fore_' + sfx, [(s * .04, .0, d['elZ'] + .02), (s * .15, .0, d['elZ'] - .04), (s * .18, 0, d['wrZ'] + .02), (s * .05, 0, d['wrZ'] + .03)],
              (0, 1, 0), .012, 'fin')
        prism('shin_' + sfx, [(s * .07, .02, .27), (s * .13, .02, .2), (s * .1, .02, .11), (s * .065, .02, .17)], (0, 1, 0), .01, 'fin')
        prism('chest', [(s * .1, .0, .8), (s * .17, .0, .74), (s * .11, .0, .67)], (0, 1, 0), .01, 'fin2')
    ell('chest', (0, 0, d['chestZ']), (.112, .075, .165), 'skin')
    ell('chest', (0, -.035, d['chestZ'] - .02), (.088, .05, .13), 'belly')
    ell('chest', (0, .025, d['chestZ'] + .07), (.11, .055, .09), 'skin2')                     # darker shoulders and back
    ell('hips', (0, 0, d['hipZ'] + .03), (.11, .078, .085), 'dark')                           # a darker band of scales at the hips
    cone('chest', (0, 0, .87), (0, 0, .93), .048, .04, 'skin')
    ell('head', (0, .02, d['headZ']), (.088, .118, .1), 'skin', 10, 6)
    face_patch('head', (0, .02, d['headZ']), (.088, .118, .1), az=52, el=26)
    ell('head', (0, -.05, d['headZ'] + .05), (.075, .065, .045), 'skin2', 8, 4)              # brow
    # the long head-tail hangs down the back, a fin running along it
    cone('tail1', d['t1'][0], d['t1'][1], .06, .045, 'skin')
    ell('tail1', d['t1'][1], (.045, .045, .045), 'skin', 8, 4)
    cone('tail2', d['t2'][0], d['t2'][1], .045, .012, 'fin')
    def wt(p):
        w2 = max(0.0, min(1.0, (d['t2'][0][2] + .03 - p.z) / .06))
        w1 = max(0.0, min(1.0, (p.y - .03) / .06)) * (1 - w2)
        return {'tail2': w2, 'tail1': w1, 'head': max(0.0, 1 - w1 - w2)}
    prism('head', [(0, -.04, 1.1), (0, .06, 1.14), (0, .2, 1.06), (0, .3, .9), (0, .3, .72), (0, .22, .7), (0, .2, .9), (0, .1, 1.04)],
          (1, 0, 0), .012, 'fin2', weights=wt)


def build_goron(P):
    d = DIM['goron']
    legs_and_arms(d, 'skin', 'skin', .105, .095, .105, .115, .135, foot_color='skin2', foot_s=(.115, .15, .05))
    ell('chest', (0, 0, d['chestZ']), (.27, .2, .23), 'skin')
    ell('chest', (0, -.06, .47), (.25, .2, .2), 'skin3')                                  # the round belly
    ell('hips', (0, 0, .33), (.25, .19, .1), 'cloth')                                      # loincloth
    cone('hips', (0, 0, .385), (0, 0, .35), .26, .26, 'band')
    box('hips', (0, -.2, .27), (.12, .02, .16), 'cloth2')
    for i, (x, z, r) in enumerate([(-.14, .66, .06), (.12, .7, .05), (0, .54, .07), (-.1, .42, .05), (.13, .48, .06)]):
        ell('chest', (x, .17, z), (r, r * .8, r * .9), 'skin2', 6, 4)                       # rocky studs on the back
    for s, sfx in ((-1, 'l'), (1, 'r')):
        ell('arm_' + sfx, (s * d['shX'], 0, d['shZ'] - .02), (.15, .14, .14), 'skin')       # shoulder
        cone('fore_' + sfx, (s * d['shX'], 0, d['wrZ'] + .06), (s * d['shX'], 0, d['wrZ'] + .01), .125, .125, 'band')   # wristband
        ell('chest', (s * .2, -.15, .64), (.05, .02, .05), 'skin2', 6, 4)                     # marks on the chest
    ell('chest', (0, 0, .73), (.17, .13, .08), 'skin')
    ell('head', (0, 0, d['headZ']), (.15, .14, .13), 'skin', 10, 6)
    face_patch('head', (0, 0, d['headZ']), (.15, .14, .13), az=55, el=28)
    ell('head', (0, -.143, d['headZ'] - .02), (.05, .04, .04), 'skin3', 8, 4)            # nose
    box('head', (0, -.115, d['headZ'] + .06), (.22, .05, .028), 'skin2')                   # brow ridge
    # grey mane, spiked, and a beard
    for i in range(10):
        a = TAU * i / 10
        x, y = .13 * math.sin(a), .12 * math.cos(a)
        zlen = .12 + .04 * (i % 3)
        cone('head', (x * .85, y * .85, d['headZ'] + .09), (x * 1.35, y * 1.35 + .02, d['headZ'] + .09 + zlen), .035, .004, 'hair' if i % 2 else 'hair2', 5)
    for i, (x, z, ln) in enumerate([(-.07, 0, .1), (-.03, 0, .13), (0, 0, .15), (.03, 0, .13), (.07, 0, .1)]):
        cone('head', (x, -.1, d['headZ'] - .09), (x * 1.3, -.13, d['headZ'] - .09 - ln), .03, .004, 'hair' if i % 2 else 'hair2', 5)
    ell('head', (0, -.08, d['headZ'] - .075), (.1, .06, .035), 'hair', 8, 4)
    for s in (-1, 1):
        ell('head', (s * .148, 0, d['headZ']), (.025, .04, .05), 'skin2', 6, 4)               # ears


def build_gerudo(P):
    d = DIM['gerudo']
    legs_and_arms(d, 'skin', 'skin', .04, .03, .027, .021, .026, foot_color='shoe', foot_s=(.04, .08, .035))
    for s, sfx in ((-1, 'l'), (1, 'r')):
        x = s * d['hipX']
        ell('leg_' + sfx, (x * 1.05, 0, .41), (.1, .1, .14), 'pink')                           # baggy trousers
        ell('shin_' + sfx, (x * 1.0, 0, .2), (.088, .085, .115), 'pink2')
        cone('shin_' + sfx, (x, 0, .105), (x, 0, .085), .06, .06, 'gold')                      # gathered at the ankle
        cone('foot_' + sfx, (x, -.1, .065), (x, -.2, .115), .035, .008, 'shoe', 6)             # curled red toe
        ell('arm_' + sfx, (s * d['shX'], 0, d['shZ'] - .06), (.03, .03, .03), 'gold2', 6, 4)   # armband
        cone('fore_' + sfx, (s * d['shX'], 0, d['wrZ'] + .05), (s * d['shX'], 0, d['wrZ'] + .01), .026, .026, 'gold')   # bracer
        ell('head', (s * .09, .0, d['headZ'] - .05), (.012, .012, .02), 'gold', 6, 4)          # earring
    ell('hips', (0, 0, d['hipZ'] + .02), (.125, .09, .085), 'pink')
    cone('hips', (0, 0, .61), (0, 0, .56), .115, .125, 'pink2')                                # sash
    ell('chest', (0, 0, d['chestZ'] - .02), (.092, .06, .12), 'skin')                          # bare midriff
    ell('chest', (0, -.012, .82), (.115, .072, .07), 'cream')                                   # the top
    cone('chest', (0, 0, .85), (0, 0, .76), .1, .092, 'cream', 8)
    cone('chest', (0, 0, .735), (0, 0, .745), .098, .098, 'gold')
    ell('chest', (0, -.075, .8), (.018, .01, .018), 'ruby', 6, 4)
    cone('chest', (0, 0, .9), (0, 0, .94), .036, .032, 'skin')
    ell('chest', (0, -.01, .91), (.07, .05, .02), 'gold', 8, 3)                                # collar
    ell('head', (0, 0, d['headZ']), (.088, .09, .098), 'skin', 10, 6)
    face_patch('head', (0, 0, d['headZ']), (.088, .09, .098))
    ell('head', (0, -.09, d['headZ'] - .025), (.014, .018, .014), 'skin2', 6, 4)
    ell('head', (0, .02, d['headZ'] + .035), (.096, .098, .088), 'red', 10, 5)                  # red hair
    ell('head', (0, -.05, d['headZ'] + .075), (.08, .04, .035), 'red', 8, 4)                    # fringe
    cone('head', (0, 0, d['headZ'] + .06), (0, 0, d['headZ'] + .075), .098, .098, 'gold')       # circlet
    ell('head', (0, -.093, d['headZ'] + .07), (.014, .01, .014), 'ruby', 6, 4)
    cone('tail1', d['t1'][0], d['t1'][1], .045, .04, 'red')
    cone('tail2', d['t2'][0], d['t2'][1], .04, .012, 'red2')
    ell('tail1', d['t1'][0], (.045, .045, .045), 'gold', 6, 4)                                   # hair tie
    # bow in the left hand, quiver and arrows on the back
    global FRAME
    hp = Vector((-d['shX'], 0, d['wrZ'] - .02))
    FRAME = held(hp)
    n = 9
    prev = None
    for i in range(n):           # the bow bends toward the target, its grip in the fist, the string straight behind
        t = i / (n - 1)
        p = Vector((0, -.09 * math.sin(math.pi * t) + .045, (t - .5) * .55))
        if prev is not None:
            cone('hand_l', prev, p, .011, .011, 'wood', 6)
        prev = p
    box('hand_l', (0, .045, 0), (.004, .004, .55), 'cream')
    FRAME = Matrix.Identity(4)
    cone('chest', (.07, .1, .96), (.1, .12, .6), .05, .045, 'wood')
    for i, o in enumerate((-.012, .012, 0)):
        cone('chest', (.07 + o, .1, .95), (.065 + o * 2, .105, 1.03 + .02 * i), .006, .006, 'feather', 5)
    cone('chest', (-.1, .0, .98), (.1, .12, .62), .004, .004, 'wood', 4)                         # the strap


BUILDERS = {'kokiri': build_kokiri, 'zora': build_zora, 'goron': build_goron, 'gerudo': build_gerudo}


def bone_table(kind):
    d = DIM[kind]
    hz, hx, sx = d['hipZ'], d['hipX'], d['shX']
    b = {
        'root': ((0, 0, 0), (0, 0, .06), None),
        'hips': ((0, 0, hz), (0, 0, hz + .06), 'root'),
        'chest': ((0, 0, hz + .06), (0, 0, d['neckZ']), 'hips'),
        'head': ((0, 0, d['neckZ']), (0, 0, d['headTop']), 'chest'),
        'tail1': (d['t1'][0], d['t1'][1], 'head'),
        'tail2': (d['t2'][0], d['t2'][1], 'tail1'),
    }
    for s, sf in ((-1, 'l'), (1, 'r')):
        b['arm_' + sf] = ((s * sx, 0, d['shZ']), (s * sx, 0, d['elZ']), 'chest')
        b['fore_' + sf] = ((s * sx, 0, d['elZ']), (s * sx, 0, d['wrZ']), 'arm_' + sf)
        b['hand_' + sf] = ((s * sx, 0, d['wrZ']), (s * sx, 0, d['handEnd']), 'fore_' + sf)
        b['leg_' + sf] = ((s * hx, 0, hz), (s * hx, 0, d['kneeZ']), 'hips')
        b['shin_' + sf] = ((s * hx, 0, d['kneeZ']), (s * hx, 0, d['ankleZ']), 'leg_' + sf)
        b['foot_' + sf] = ((s * hx, 0, d['ankleZ']), (s * hx, -d['foot'], d['ankleZ']), 'shin_' + sf)
    return b


# ---- animation ---------------------------------------------------------------------------------------------------------------------------
class Ctl:
    """Poses the rig with rotations about the world axes (so '+forward' means the same for every bone)."""
    def __init__(self, rig):
        self.rig = rig
        self.R = {b.name: b.matrix_local.to_3x3() for b in rig.data.bones}
        self.reset()

    def reset(self):
        self.q = {n: Quaternion() for n in BONES}
        self.loc = {n: Vector() for n in BONES}

    def rotw(self, bone, axis, a):
        self.q[bone] = Quaternion(axis, a) @ self.q[bone]

    # limbs that hang down: swing the lower end forward (+) / back (-)
    def sw(self, bone, a): self.rotw(bone, (1, 0, 0), -a)
    # things that point up (chest, head): lean the top forward (+)
    def lean(self, bone, a): self.rotw(bone, (1, 0, 0), a)
    def tw(self, bone, a): self.rotw(bone, (0, 0, 1), a)       # + turns to the character's right... see calibration in preview
    def roll(self, bone, a): self.rotw(bone, (0, 1, 0), a)     # + tips the top toward +X (the right)
    def ab(self, bone, a):                                       # a hanging limb out to the side (+)
        self.rotw(bone, (0, 1, 0), a if bone.endswith('_l') else -a)
    def move(self, bone, v): self.loc[bone] += Vector(v)

    def snapshot(self, fn):
        self.reset()
        fn()
        return ({k: v.copy() for k, v in self.q.items()}, {k: v.copy() for k, v in self.loc.items()})

    def blend(self, a, b, k):
        for n in BONES:
            self.q[n] = a[0][n].slerp(b[0][n], k)
            self.loc[n] = a[1][n].lerp(b[1][n], k)

    def apply(self, rig):
        for n in BONES:
            pb = rig.pose.bones[n]
            R = self.R[n]
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (R.inverted() @ self.q[n].to_matrix() @ R).to_quaternion()
            pb.location = R.inverted() @ self.loc[n]
        bpy.context.view_layer.update()


def sm(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def mix(a, b, k):
    return a + (b - a) * k


# One pose function per clip. `ph` is a phase in radians for loops, `u` runs 0..1 for one-shots. Every clip ends close to the ready stance so the
# game can fade into idle.
def ready(P, k, arms=True):
    """The ready stance each kind stands in. Clips that pose the arms themselves (attacks, cheering) take only the legs."""
    P.sw('leg_l', .04); P.sw('leg_r', -.02)
    P.sw('shin_l', -.08); P.sw('shin_r', -.06)
    P.ab('leg_l', .05); P.ab('leg_r', .05)
    if k == 'goron':
        P.ab('leg_l', .15); P.ab('leg_r', .15); P.lean('chest', .08)
    if not arms:
        return
    if k == 'kokiri':      # hands forward holding the slingshot
        P.sw('arm_l', .55); P.sw('fore_l', .8); P.sw('arm_r', .25); P.sw('fore_r', .9); P.ab('arm_r', .15)
    elif k == 'zora':
        P.ab('arm_l', .22); P.ab('arm_r', .22); P.sw('fore_l', .25); P.sw('fore_r', .25); P.sw('arm_l', .08); P.sw('arm_r', .08)
        P.lean('head', .05)
    elif k == 'goron':     # fists up, elbows out, ready to fight
        P.ab('arm_l', .38); P.ab('arm_r', .38); P.sw('arm_l', .2); P.sw('arm_r', .2); P.sw('fore_l', 1.1); P.sw('fore_r', 1.1)
        P.ab('fore_l', -.25); P.ab('fore_r', -.25)
    else:                  # bow held low in the left hand, right hand loose
        P.sw('arm_l', .4); P.sw('fore_l', .7); P.sw('arm_r', .1); P.ab('arm_r', .12); P.sw('fore_r', .3)
        P.tw('chest', .05)


def pose_idle(P, k, ph):
    ready(P, k)
    s, c = math.sin(ph), math.cos(ph)
    P.lean('chest', .025 * s)
    P.tw('head', .22 * math.sin(ph) * (1 if k != 'goron' else .6))
    P.lean('head', -.03 * s + .03 * c)
    P.move('root', (0, 0, .004 * c))
    P.roll('hips', .025 * math.sin(ph)); P.roll('chest', -.02 * math.sin(ph))
    amp = {'kokiri': .05, 'zora': .04, 'goron': .03, 'gerudo': .03}[k]
    P.sw('arm_l', amp * s); P.sw('arm_r', -amp * s)
    P.rotw('tail1', (1, 0, 0), .06 * s); P.rotw('tail1', (0, 0, 1), .08 * math.sin(ph * 2))
    P.rotw('tail2', (0, 0, 1), .14 * math.sin(ph * 2 - .8)); P.rotw('tail2', (1, 0, 0), .06 * c)
    if k == 'zora':     # a gentle sway and fin flutter
        P.roll('chest', .03 * math.sin(ph)); P.rotw('tail2', (1, 0, 0), .12 * math.sin(ph * 3))
    if k == 'goron':
        P.move('root', (0, 0, .008 * math.sin(ph * 2)))    # heavy breathing
        P.lean('chest', .02 * math.sin(ph * 2))
    if k == 'gerudo':   # weight on one hip, a hand on it
        P.roll('hips', .03); P.ab('arm_r', .5); P.ab('fore_r', -1.15); P.sw('fore_r', -.1)
        P.move('hips', (.012 * math.sin(ph), 0, 0))


def pose_walk(P, k, ph):
    g = GAIT[k]
    s, c = math.sin(ph), math.cos(ph)
    for sd, sg in (('l', 1.0), ('r', -1.0)):
        a = s * sg
        sw_ph = c * sg                                  # > 0 while the leg swings forward
        P.sw('leg_' + sd, g['leg'] * a)
        P.sw('shin_' + sd, -g['knee'] * max(0.0, sw_ph) ** .8 - .1)
        P.sw('foot_' + sd, .25 * max(0.0, -a) - .3 * max(0.0, sw_ph) * .3)
        P.ab('leg_' + sd, .04 + (.1 if k == 'goron' else 0))
        arm = -a * g['arm']
        P.sw('arm_' + sd, arm)
        P.sw('fore_' + sd, .35 + .5 * max(0.0, arm / max(g['arm'], .01)))
        P.ab('arm_' + sd, .06 + (.28 if k == 'goron' else 0))
    P.lean('chest', g['lean'] + .02 * math.cos(2 * ph))
    P.tw('chest', g['twist'] * s * 0.9); P.tw('hips', -g['twist'] * s * .8); P.tw('head', -g['twist'] * s * .5)
    P.roll('hips', .04 * s * (3 if k == 'goron' else 1))
    P.roll('chest', -.03 * s * (3 if k == 'goron' else 1))
    P.move('root', (0, 0, g['bob'] * (.5 + .5 * math.cos(2 * ph))))
    if k == 'goron':
        P.move('hips', (.02 * s, 0, 0))
    if k == 'kokiri':   # the ready stance slingshot arm stays forward
        P.sw('arm_l', .5); P.sw('fore_l', .6)
    if k == 'gerudo':
        P.sw('arm_l', .45 - .1 * s); P.sw('fore_l', .6)
    P.rotw('tail1', (1, 0, 0), .16 * math.cos(2 * ph - .6)); P.rotw('tail1', (0, 0, 1), .3 * math.sin(ph - .5))
    P.rotw('tail2', (1, 0, 0), .2 * math.cos(2 * ph - 1.3)); P.rotw('tail2', (0, 0, 1), .42 * math.sin(ph - 1.1))


def pose_invite(P, k, ph):
    s, c = math.sin(ph), math.cos(ph)
    ready(P, k, arms=(k == 'zora'))
    if k == 'kokiri':   # both arms up, hopping and waving
        hop = abs(math.sin(ph * 3))
        P.move('root', (0, 0, .03 * hop))
        for sd, o in (('l', 0.0), ('r', math.pi)):
            w = math.sin(ph * 3 + o)
            P.ab('arm_' + sd, 1.35 + .3 * w); P.sw('arm_' + sd, .35); P.ab('fore_' + sd, .5 + .5 * w)
        P.sw('leg_l', -.2 * hop); P.sw('leg_r', .1 * hop); P.sw('shin_l', -.4 * hop); P.sw('shin_r', -.4 * hop)
        P.tw('head', .2 * s)
    elif k == 'zora':   # arms open, a slow graceful welcome
        P.ab('arm_l', .9 + .12 * s); P.ab('arm_r', .9 - .12 * s); P.sw('fore_l', .35); P.sw('fore_r', .35)
        P.sw('arm_l', .25); P.sw('arm_r', .25); P.roll('chest', .08 * s); P.lean('head', -.06); P.roll('head', -.1 * s)
        P.move('root', (0, 0, .008 * math.sin(ph * 2)))
        P.rotw('tail1', (1, 0, 0), .1 * math.sin(ph * 2)); P.rotw('tail2', (1, 0, 0), .2 * math.sin(ph * 2 - 1))
    elif k == 'goron':  # pounds his chest, laughing
        hit_r, hit_l = max(0, math.sin(ph * 2)), max(0, -math.sin(ph * 2))
        P.lean('chest', -.18 + .05 * hit_r + .05 * hit_l)
        for sd, h in (('l', hit_l), ('r', hit_r)):
            P.ab('arm_' + sd, .5 - .4 * h); P.sw('arm_' + sd, .6 + .3 * h); P.sw('fore_' + sd, 1.7 + .5 * h); P.ab('fore_' + sd, -.7 * h)
        P.move('root', (0, 0, -.01 * (hit_l + hit_r)))
        P.lean('head', -.2)
        P.sw('leg_l', .1); P.sw('leg_r', -.1)
    else:               # one hand on the hip, beckoning with the other
        P.roll('hips', .06 * math.sin(ph)); P.ab('arm_r', .55); P.ab('fore_r', -1.15)
        P.sw('arm_l', 1.0); P.sw('fore_l', 1.0 + .6 * math.sin(ph * 2)); P.ab('arm_l', .1)
        P.roll('head', -.1 * s); P.lean('head', -.05)
        P.tw('chest', .08 * s)
        P.rotw('tail1', (0, 0, 1), .25 * math.sin(ph)); P.rotw('tail2', (0, 0, 1), .35 * math.sin(ph - .8))
    P.rotw('tail2', (0, 0, 1), .15 * math.sin(ph * 2 - 1.0))


def pose_attack(P, k, u, var):
    """Frame 0 is the wind-up held a moment (the game starts this clip as the hit lands), the strike peaks at u = .12."""
    ready(P, k, arms=False)
    strike = sm(u / .12)
    rec = sm((u - .42) / .5)
    e = strike * (1 - rec)
    flip = -1.0 if var else 1.0        # which side
    sdA, sdB = ('r', 'l') if not var else ('l', 'r')
    snap = math.sin(min(1.0, u / .12) * math.pi) if u < .12 else 0.0
    if k == 'goron':
        # a hooked punch: fist drawn back, then driven through; the guard hand stays up. torso follows the fist.
        P.tw('chest', flip * mix(.55, -.6, strike) * (1 - rec))
        P.tw('hips', flip * mix(.25, -.2, strike) * (1 - rec))
        P.tw('head', flip * .35 * e)
        P.lean('chest', .1 + .32 * e)
        P.ab('arm_' + sdA, mix(.5, .2, strike)); P.sw('arm_' + sdA, mix(-.5, 1.58, strike) * (1 - rec) + .2 * rec)
        P.sw('fore_' + sdA, mix(2.0, .04, strike) * (1 - rec) + 1.1 * rec); P.ab('fore_' + sdA, 0)
        P.sw('arm_' + sdB, .55 * (1 - e) + .8 * e); P.sw('fore_' + sdB, 1.9)
        P.sw('leg_' + sdA, -.12 * e); P.sw('leg_' + sdB, .38 * e); P.sw('shin_' + sdB, -.2 * e)   # a step into the blow
        P.move('root', (0, -.07 * e, -.05 * e + .02 * snap))
        P.tw('hips', 0)
    elif k == 'kokiri':
        # slingshot: left arm out, right hand pulls the band to the cheek, then lets it go.
        rel = sm((u - .08) / .06)
        P.tw('chest', .25 * (1 - rec * .8) * (1 if not var else .9)); P.tw('head', -.22 * (1 - rec))
        P.sw('arm_l', mix(.55, 1.56, 1 - rec) + .06 * snap); P.sw('fore_l', mix(.8, .05, 1 - rec))
        P.ab('arm_l', .08)
        P.sw('arm_r', mix(.25, 1.2, 1 - rec) + .1 * (1 - rel)); P.ab('arm_r', mix(.15, .85, 1 - rec) * (1 - .5 * rel))
        P.sw('fore_r', mix(.9, 2.25, 1 - rec) * (1 - .55 * rel)); P.lean('chest', -.05 * (1 - rec) + .1 * snap)
        P.sw('leg_l', .22 * (1 - rec)); P.sw('leg_r', -.2 * (1 - rec))
        P.move('root', (0, 0, -.008 * e))
    elif k == 'gerudo':
        rel = sm((u - .08) / .05)
        P.tw('chest', .42 * (1 - rec * .85)); P.tw('hips', .15 * (1 - rec)); P.tw('head', -.35 * (1 - rec))
        P.sw('arm_l', mix(.4, 1.57, 1 - rec) + .05 * snap); P.sw('fore_l', mix(.7, .0, 1 - rec)); P.ab('arm_l', .05)
        P.sw('arm_r', mix(.1, .9, 1 - rec)); P.ab('arm_r', mix(.12, 1.25, 1 - rec) * (1 - .35 * rel)); P.sw('fore_r', mix(.3, 2.4, 1 - rec) * (1 - .45 * rel))
        P.sw('leg_l', .3 * (1 - rec)); P.sw('leg_r', -.28 * (1 - rec)); P.ab('leg_l', .1); P.ab('leg_r', .1)
        P.rotw('tail1', (0, 0, 1), .4 * snap); P.rotw('tail2', (0, 0, 1), -.5 * snap)
    else:  # zora: both palms thrown forward, the body whipping through behind them
        pull = 1 - strike
        P.lean('chest', -.1 * (1 - rec) * pull + .32 * e * strike); P.lean('head', .1 * e)
        for sd in ('l', 'r'):
            P.sw('arm_' + sd, mix(-.3, 1.5, strike) * (1 - rec) + .1 * rec); P.sw('fore_' + sd, mix(2.0, .05, strike) * (1 - rec) + .25 * rec)
            P.ab('arm_' + sd, mix(.15, .12, strike) + .1 * e)
        P.tw('chest', .3 * pull * (1 - rec) - .15 * e); P.sw('leg_l', .28 * e); P.sw('leg_r', -.25 * e)
        P.rotw('tail1', (1, 0, 0), .5 * snap - .1 * e); P.rotw('tail2', (1, 0, 0), .8 * snap - .2 * e)
        P.move('root', (0, -.03 * e, 0))


def pose_heal(P, k, u):
    if k != 'zora':
        pose_cheer(P, k, u)
        return
    ready(P, k, arms=False)
    up = sm(u / .4) * (1 - sm((u - .55) / .35))
    out = sm((u - .45) / .2) * (1 - sm((u - .8) / .2))
    for sd in ('l', 'r'):
        P.sw('arm_' + sd, 2.9 * up + .9 * out); P.ab('arm_' + sd, .5 * up); P.sw('fore_' + sd, .25 * up + .2)
    P.lean('chest', -.22 * up + .2 * out); P.lean('head', -.2 * up)
    P.move('root', (0, 0, .03 * up))
    P.rotw('tail1', (1, 0, 0), .5 * up); P.rotw('tail2', (1, 0, 0), .7 * up - .3 * out)


def pose_hurt(P, k, u):
    ready(P, k)
    env = math.sin(min(1.0, u) * math.pi) ** .7
    P.lean('chest', -.42 * env); P.lean('head', -.35 * env); P.tw('head', .3 * env)
    for sd in ('l', 'r'):
        P.ab('arm_' + sd, .5 * env); P.sw('arm_' + sd, -.3 * env)
    P.move('root', (0, .05 * env, -.025 * env)); P.sw('leg_l', -.15 * env); P.sw('leg_r', .15 * env); P.sw('shin_l', -.2 * env)
    P.rotw('tail1', (1, 0, 0), .6 * env); P.rotw('tail2', (1, 0, 0), .8 * env)


def pose_cheer(P, k, u):
    ready(P, k, arms=False)
    hop = abs(math.sin(u * math.pi * 3)) * (1 - sm((u - .8) / .2)) * (1 if u < 1 else 0)
    raise_ = sm(u / .12) * (1 - sm((u - .82) / .18))
    P.move('root', (0, 0, .07 * hop * (1.4 if k == 'kokiri' else 1.0)))
    for sd in ('l', 'r'):
        P.sw('arm_' + sd, 2.5 * raise_ + .6 * (1 - raise_)); P.ab('arm_' + sd, .75 * raise_); P.sw('fore_' + sd, .15 * raise_ + .8 * (1 - raise_))
    P.lean('head', -.25 * raise_); P.lean('chest', -.1 * raise_)
    if k == 'goron':
        P.sw('fore_l', 1.0 * raise_ + .1); P.sw('fore_r', 1.0 * raise_ + .1)
    P.sw('shin_l', -.5 * hop); P.sw('shin_r', -.5 * hop); P.sw('leg_l', -.25 * hop); P.sw('leg_r', -.25 * hop)
    P.rotw('tail1', (1, 0, 0), .4 * hop); P.rotw('tail2', (1, 0, 0), .6 * hop)


def pose_rest(P, k, ph):
    """Waiting for someone to come by: a Goron curls into his rock ball, the others sit on the ground."""
    s = math.sin(ph)
    d = DIM[k]
    if k == 'goron':       # tucked into a ball, the studded back on top, breathing slowly
        P.lean('root', .55); P.lean('chest', .75 + .03 * s); P.lean('head', .6)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 2.15); P.sw('shin_' + sd, -2.55); P.sw('foot_' + sd, .6); P.ab('leg_' + sd, .1)
            P.sw('arm_' + sd, 1.0); P.ab('arm_' + sd, -.1); P.sw('fore_' + sd, 2.0); P.ab('fore_' + sd, -.8)
        P.move('root', GORON_BALL_SHIFT)
        return
    drop = d['hipZ'] - .055 * d['H']
    P.move('root', (0, 0, -drop + .003 * s))
    P.lean('head', .06 * s - .05); P.tw('head', .3 * math.sin(ph))
    if k == 'gerudo':      # legs out, leaning back on her hands
        P.lean('chest', -.35 + .02 * s)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 1.45); P.sw('shin_' + sd, -.25 if sd == 'l' else -.9); P.sw('arm_' + sd, -.55); P.ab('arm_' + sd, .25)
        P.lean('head', .3)
    else:                  # knees up, arms around them
        P.lean('chest', .15 + .02 * s)
        for sd in ('l', 'r'):
            P.sw('leg_' + sd, 2.05); P.sw('shin_' + sd, -2.1); P.ab('leg_' + sd, .15)
            P.sw('arm_' + sd, .95); P.sw('fore_' + sd, .75); P.ab('fore_' + sd, -.4)
        P.rotw('tail1', (1, 0, 0), .05 * s)


def pose_rise(P, k, u):
    """From resting to the ready stance; a Goron uncurls with a big stretch."""
    a = P.snapshot(lambda: pose_rest(P, k, 0.0))
    b = P.snapshot(lambda: pose_idle(P, k, 0.0))
    e = sm(u / .85)
    P.blend(a, b, e)
    bump = math.sin(min(1.0, u / .85) * math.pi)
    if k == 'goron':
        P.ab('arm_l', .9 * bump); P.ab('arm_r', .9 * bump); P.lean('chest', -.25 * bump); P.lean('head', -.3 * bump)
        P.move('root', (0, 0, .04 * bump))
    else:
        P.lean('chest', .2 * bump)


def pose_for(P, kind, name, f, n, loops):
    P.reset()
    t = f / n if loops else f / max(1, n - 1)
    ph = t * TAU
    if name == 'idle': pose_idle(P, kind, ph)
    elif name == 'walk': pose_walk(P, kind, ph)
    elif name == 'invite': pose_invite(P, kind, ph)
    elif name == 'attack': pose_attack(P, kind, t, 0)
    elif name == 'attack2': pose_attack(P, kind, t, 1)
    elif name == 'heal': pose_heal(P, kind, t)
    elif name == 'hurt': pose_hurt(P, kind, t)
    elif name == 'cheer': pose_cheer(P, kind, t)
    elif name == 'rest': pose_rest(P, kind, ph)
    elif name == 'rise': pose_rise(P, kind, t)


def build_rig(kind, mesh):
    arm = bpy.data.armatures.new('%sSkeleton' % kind.capitalize())
    rig = bpy.data.objects.new('%sRig' % kind.capitalize(), arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    mesh.select_set(False)
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    table = bone_table(kind)
    for n in BONES:
        head, tail, parent = table[n]
        b = arm.edit_bones.new(n)
        b.head, b.tail = head, tail
        if parent:
            b.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    mod = mesh.modifiers.new('skin', 'ARMATURE')
    mod.object = rig
    mesh.parent = rig
    return rig


def animate(rig, kind):
    ctl = Ctl(rig)
    rig.animation_data_create()
    for name, seconds, loops, fps in CLIPS:
        act = bpy.data.actions.new(name)
        act['ally_fps'] = fps
        rig.animation_data.action = act
        n = frames_of(seconds, loops, fps)
        for f in range(n):
            pose_for(ctl, kind, name, f, n, loops)
            ctl.apply(rig)
            for pb in rig.pose.bones:
                pb.keyframe_insert('rotation_quaternion', frame=f)
                pb.keyframe_insert('location', frame=f)
        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'
        tr = rig.animation_data.nla_tracks.new()
        tr.name = name
        tr.strips.new(name, 0, act)
        tr.mute = True
    rig.animation_data.action = bpy.data.actions['idle']


def build_kind(kind):
    global PAL, parts, FRAME
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = []
    FRAME = Matrix.Identity(4)
    PAL = Palette(kind)
    BUILDERS[kind](None)
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = kind.capitalize()
    imgs = [make_atlas(kind, e, PAL) for e in FACES]
    mat = bpy.data.materials.new('AllyAtlas')
    mat.use_nodes = True
    node = mat.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = imgs[0]
    node.interpolation = 'Closest'
    bs = mat.node_tree.nodes.get('Principled BSDF')
    mat.node_tree.links.new(node.outputs['Color'], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 1.0
    mesh.data.materials.append(mat)
    rig = build_rig(kind, mesh)
    animate(rig, kind)
    bpy.context.scene.render.fps = 24
    bpy.context.scene['art_notes'] = 'Original low-poly %s ally; 64x64 painted atlas with 3 faces; 18 bones; 8 clips (idle, walk, invite, attack, attack2, heal, hurt, cheer).' % kind
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / ('%s.blend' % kind)))
    try:
        for tr in rig.animation_data.nla_tracks:
            tr.mute = False
        rig.animation_data.action = None
        bpy.ops.export_scene.gltf(filepath=str(OUT / ('%s.glb' % kind)), export_format='GLB', export_animations=True,
                                  export_animation_mode='NLA_TRACKS', export_yup=True)
    except Exception as e:   # the glTF add-on is optional; the .blend is the source of truth
        print('glb skipped:', e)
    print('built', kind, len(mesh.data.vertices), 'vertices')


if __name__ == '__main__':
    only = [a for a in sys.argv[1:] if a in KINDS]
    for k in (only or KINDS):
        build_kind(k)
