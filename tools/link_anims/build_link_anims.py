#!/usr/bin/env python3
"""Authors OOT Royale's own Link animation clips and writes shared/link_anims.h.

The reference rig is the player's own local export of Adult Link (a Collada .dae). It is ROM-derived: it is read here only for the skeleton's
limb offsets and rest rotations, never committed, and nothing from its mesh or textures goes into the output. The output is pure pose data we
author below: per frame, Link's 21 limb rotations (the game's joint table order, binary angles, Z*Y*X like Matrix_JointPosition) plus the root
height, at 20 frames a second (the game tick).

  python3 -I tools/link_anims/build_link_anims.py --rig "/path/Adult Link.dae" [--out shared/link_anims.h] [--preview DIR]

--preview renders PNG strips of the clips with bpy (stick figure; no mesh).
"""
import argparse, math, os, sys
import xml.etree.ElementTree as ET
import numpy as np

NS = {'c': 'http://www.collada.org/2005/11/COLLADASchema'}
LIMBS = 21          # game limbs 0..20 (root, waist, lower, R thigh, R shin, R foot, L thigh, L shin, L foot, upper, head, hat, collar,
                    # L shoulder, L forearm, L hand, R shoulder, R forearm, R hand, sheath, torso)
UNIT = 1000.0       # rig units -> Link model units (actor scale 0.01)
FPS = 20

# Rig space: forward -X, Link's right +Y, up +Z. Game model space: forward +Z, right -X, up +Y.
C = np.array([[0, -1, 0], [0, 0, 1], [-1, 0, 0]], float)


def rx(d):
    a = math.radians(d); c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def ry(d):
    a = math.radians(d); c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rz(d):
    a = math.radians(d); c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def load_rig(path):
    tree = ET.parse(path)
    par, local, tips = {}, {}, {}

    def walk(node, parent):
        for ch in node.findall('c:node', NS):
            name = ch.get('name') or ''
            m = ch.find('c:matrix', NS)
            mat = np.array([float(x) for x in m.text.split()]).reshape(4, 4) if m is not None else np.eye(4)
            if name.startswith('limb_') and not name.endswith('end'):
                i = int(name[5:7]); par[i] = parent; local[i] = mat; walk(ch, i)
            elif name.endswith('_end') and parent is not None and name.count('end') == 1:
                tips[parent] = mat[:3, 3].copy(); walk(ch, parent)
            else:
                walk(ch, parent)

    for vs in tree.getroot().iter('{%s}visual_scene' % NS['c']):
        walk(vs, None)
    if sorted(local) != list(range(LIMBS)):
        sys.exit('rig: expected limb_00..limb_20, got %s' % sorted(local))
    return par, local, tips


class Rig:
    def __init__(self, path):
        self.par, self.local, self.tips = load_rig(path)
        self.offs = {i: self.local[i][:3, 3] for i in range(LIMBS)}
        self.rest = {i: self.local[i][:3, :3] for i in range(LIMBS)}
        self.wrest = {}
        for i in range(LIMBS):
            p = self.par[i]
            self.wrest[i] = self.rest[i] if p is None else self.wrest[p] @ self.rest[i]

    def locals_for(self, deltas):
        """deltas: limb -> rotation in rig world axes, applied at that joint as if in the rest (T) pose."""
        out = {}
        for i in range(LIMBS):
            d = deltas.get(i, np.eye(3))
            w = self.wrest[i]
            out[i] = self.rest[i] @ (w.T @ d @ w)
        return out

    def fk(self, loc, root):
        pos, rot = {}, {}
        for i in range(LIMBS):
            p = self.par[i]
            if p is None:
                rot[i] = loc[i]; pos[i] = np.array([0, 0, root])
            else:
                rot[i] = rot[p] @ loc[i]; pos[i] = pos[p] + rot[p] @ self.offs[i]
        return pos, rot


def euler_zyx(r):
    y = math.asin(max(-1.0, min(1.0, -r[2, 0])))
    x = math.atan2(r[2, 1], r[2, 2])
    z = math.atan2(r[1, 0], r[0, 0])
    b = lambda a: int(round(math.degrees(a) * 65536 / 360.0)) & 0xFFFF
    s = lambda v: v - 65536 if v >= 32768 else v
    return s(b(x)), s(b(y)), s(b(z))


# ---- posing -------------------------------------------------------------------------------------------------------------------------
# Helpers in rig world axes. Legs/arms hang down: pitch(+) swings a hanging limb forward. Arms start in a T: lower() brings them down.
ROOT, WAIST, LOWER, RTHIGH, RSHIN, RFOOT, LTHIGH, LSHIN, LFOOT, UPPER, HEAD, HAT, COLLAR, LSHO, LFORE, LHAND, RSHO, RFORE, RHAND, SHEATH, TORSO = range(21)


def pitch(d): return ry(d)          # hanging limb forward; upright limb backward
def lean(d): return ry(-d)          # upright body tips forward
def armdown(side, d): return rx(d if side == 'L' else -d)   # T-pose arm down toward the hip


def pose(p):
    """p: dict of named controls (degrees). Returns limb -> world delta."""
    g = lambda k, v=0.0: p.get(k, v)
    D = {}
    D[ROOT] = lean(g('lean')) @ rx(g('roll'))
    D[UPPER] = lean(g('chest')) @ rz(g('twist'))
    D[HEAD] = lean(-g('chest') - g('lean') * g('headcomp', 0.6) + g('nod'))
    for s in 'LR':
        th, sh, ft = (LTHIGH, LSHIN, LFOOT) if s == 'L' else (RTHIGH, RSHIN, RFOOT)
        D[th] = pitch(g(s + 'hip')) @ rx((-1 if s == 'L' else 1) * -g(s + 'spread'))
        D[sh] = pitch(-g(s + 'knee'))
        D[ft] = pitch(g(s + 'ankle'))
        so, fo, ha = (LSHO, LFORE, LHAND) if s == 'L' else (RSHO, RFORE, RHAND)
        # shoulder: drop from the T by `down`, then swing forward/back by `arm`
        D[so] = ry(g(s + 'arm')) @ armdown(s, g(s + 'down', 75))
        # elbow bends the forearm forward (and up when the arm hangs)
        D[fo] = ry(g(s + 'elbow')) if g(s + 'down', 75) > 45 else rz((-1 if s == 'L' else 1) * g(s + 'elbow'))
        D[ha] = np.eye(3)
    return D


def mix(a, b, t):
    keys = set(a) | set(b)
    return {k: a.get(k, 0.0) * (1 - t) + b.get(k, 0.0) * t for k in keys}


def ease(t): return t * t * (3 - 2 * t)


def keyed(keys, n, loop=False):
    """keys: list of (frame, pose dict). Smooth step between keys; loop wraps to the first key."""
    ks = sorted(keys, key=lambda k: k[0])
    if loop: ks = ks + [(n, ks[0][1])]
    out = []
    for f in range(n):
        for (fa, pa), (fb, pb) in zip(ks, ks[1:]):
            if fa <= f <= fb:
                out.append(mix(pa, pb, ease((f - fa) / max(1, fb - fa)))); break
        else:
            out.append(dict(ks[-1][1]))
    return out


IDLE = dict(Ldown=78, Rdown=78, Lelbow=10, Relbow=10)

# Jump: Fortnite timing (a quick push, a tuck at the top, legs reaching for the ground) with Ocarina of Time's jump pose at the apex: arms
# flung out wide, one leg forward and one back. 12 frames = 0.6 s, the airtime of our jump. The local player holds the last frame until he lands.
JUMP = keyed([
    (0, dict(IDLE, lean=12, Lhip=25, Rhip=25, Lknee=45, Rknee=45, Lankle=-15, Rankle=-15, Larm=-35, Rarm=-35, Lelbow=25, Relbow=25)),  # push off
    (2, dict(lean=4, Lhip=-8, Rhip=10, Lknee=10, Rknee=20, Ldown=40, Rdown=40, Larm=20, Rarm=20, Lelbow=20, Relbow=20, Lankle=25, Rankle=25)),  # stretch up
    (5, dict(lean=-2, Rhip=50, Rknee=55, Lhip=-25, Lknee=70, Ldown=20, Rdown=20, Lelbow=30, Relbow=30, Larm=10, Rarm=10, Lankle=15, Rankle=0, nod=-6)),  # apex: OoT split, arms out
    (7, dict(lean=0, Rhip=40, Rknee=50, Lhip=-10, Lknee=60, Ldown=25, Rdown=25, Lelbow=30, Relbow=30, Larm=5, Rarm=5)),
    (11, dict(lean=5, Lhip=20, Rhip=28, Lknee=25, Rknee=30, Ldown=40, Rdown=40, Larm=15, Rarm=15, Lelbow=25, Relbow=25, Lankle=10, Rankle=10)),  # reach down
], 12)

# Landing: absorb and stand. 5 frames.
LAND = keyed([
    (0, dict(IDLE, lean=16, Lhip=40, Rhip=45, Lknee=75, Rknee=80, Lankle=-35, Rankle=-35, Ldown=55, Rdown=55, Larm=25, Rarm=25, Lelbow=30, Relbow=30)),
    (2, dict(IDLE, lean=10, Lhip=25, Rhip=28, Lknee=45, Rknee=48, Lankle=-20, Rankle=-20, Ldown=65, Rdown=65, Larm=10, Rarm=10, Lelbow=20, Relbow=20)),
    (4, dict(IDLE)),
], 5)


# Sprint: 8 frames a full stride (0.4 s), body pitched forward, arms pumping bent.
def sprint_key(right_forward):
    f, b = ('R', 'L') if right_forward else ('L', 'R')
    return {'lean': 16, 'chest': 4, 'twist': 8 if right_forward else -8,
            f + 'hip': 55, f + 'knee': 35, f + 'ankle': 10, b + 'hip': -30, b + 'knee': 90, b + 'ankle': 10,
            'Ldown': 72, 'Rdown': 72, b + 'arm': 50, f + 'arm': -45, 'Lelbow': 85, 'Relbow': 85}


SPRINT_A, SPRINT_B = sprint_key(True), sprint_key(False)
SPRINT_PASS = dict(lean=16, chest=4, Rhip=10, Rknee=60, Lhip=10, Lknee=60, Rankle=5, Lankle=5, Ldown=72, Rdown=72, Larm=0, Rarm=0, Lelbow=85, Relbow=85)
SPRINT = keyed([(0, SPRINT_A), (2, dict(SPRINT_PASS, root=60)), (4, SPRINT_B), (6, dict(SPRINT_PASS, root=60))], 8, loop=True)

CLIPS = [('Jump', JUMP, False, None), ('Land', LAND, False, None), ('Sprint', SPRINT, True, None)]   # (name, frames, loops, -)


def grounded(name, frame):
    """Frames whose root height is solved so the lower foot rests on the ground (the push-off, the landing, the sprint)."""
    return name in ('Land', 'Sprint') or (name == 'Jump' and frame == 0)


def solve(rig, p, grounded):
    loc = rig.locals_for(pose(p))
    base_root = rig.offs[ROOT][2]
    pos, _ = rig.fk(loc, base_root)
    if grounded:
        foot = min(pos[RFOOT][2], pos[LFOOT][2])
        rest_pos, _ = rig.fk(rig.locals_for(pose(IDLE)), base_root)
        rest_foot = min(rest_pos[RFOOT][2], rest_pos[LFOOT][2])
        root = base_root + (rest_foot - foot)
    else:
        root = base_root
    return loc, root


def frame_data(rig, p, grounded):
    loc, root = solve(rig, p, grounded)
    rots = []
    for i in range(LIMBS):
        r = C @ loc[i] if i == ROOT else loc[i]
        rots.append(euler_zyx(r))
    h = int(round(root * UNIT + p.get('root', 0.0)))
    return h, rots, loc, root


def write_header(rig, out):
    lines = ['// Generated by tools/link_anims/build_link_anims.py; do not edit.',
             '// OOT Royale\'s own Link clips (authored pose data, not from the game): per frame the root height in Link model units, then',
             '// the 21 limb rotations in the order of the game\'s Link joint table (binary angles, x y z). 20 frames a second.',
             '#pragma once', '#include <cstdint>', '', 'namespace royale::link_anims {', '',
             'constexpr int kLimbs = %d;' % LIMBS, 'constexpr int kFps = %d;' % FPS, '',
             'struct Frame { int16_t rootY; int16_t rot[kLimbs][3]; };',
             'struct Clip { const char* name; const Frame* frames; int count; bool loop; };', '']
    for name, frames, loop, _ in CLIPS:
        lines.append('inline constexpr Frame k%s[] = {' % name)
        for fi, p in enumerate(frames):
            h, rots, _, _ = frame_data(rig, p, grounded(name, fi))
            lines.append('    { %d, { %s } },' % (h, ', '.join('{ %d, %d, %d }' % r for r in rots)))
        lines.append('};')
    lines.append('')
    lines.append('enum ClipId : uint8_t { %s, kClipCount };' % ', '.join('k%sClip' % n for n, *_ in CLIPS))
    lines.append('inline constexpr Clip kClips[kClipCount] = {')
    for name, frames, loop, _ in CLIPS:
        lines.append('    { "%s", k%s, %d, %s },' % (name, name, len(frames), 'true' if loop else 'false'))
    lines.append('};')
    lines.append('')
    lines.append('}  // namespace royale::link_anims')
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def preview(rig, outdir):
    import bpy
    os.makedirs(outdir, exist_ok=True)
    strips = [('jump_takeoff_apex_land', [('Jump', i) for i in (0, 2, 5, 8, 11)] + [('Land', i) for i in (0, 2, 4)]),
              ('sprint_cycle', [('Sprint', i) for i in range(8)])]
    clips = {n: f for n, f, _, _ in CLIPS}
    for sname, frames in strips:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        sc = bpy.context.scene
        def solid(name, rgb):
            m = bpy.data.materials.new(name); m.use_nodes = True
            m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (*rgb, 1)
            return m
        mat, skin = solid('tunic', (0.12, 0.45, 0.16)), solid('skin', (0.9, 0.65, 0.45))
        for k, (cn, fi) in enumerate(frames):
            f = clips[cn]
            h, rots, loc, root = frame_data(rig, f[fi], grounded(cn, fi))
            root += f[fi].get('root', 0.0) / UNIT
            pos, rot = rig.fk(loc, root)
            off = np.array([-k * 2.6, 0, 0])   # side view: Link faces left (-X)
            for i in range(LIMBS):
                if i in (ROOT, COLLAR, TORSO, SHEATH): continue
                a = pos[i] + off
                kids = [j for j in range(LIMBS) if rig.par[j] == i and j not in (COLLAR, TORSO, SHEATH, LSHO, RSHO, HEAD)]
                if i == UPPER: kids = [HEAD]
                b = (pos[kids[0]] if kids else (pos[i] + rot[i] @ rig.tips.get(i, np.array([0.4, 0, 0])))) + off
                if i == WAIST: b = pos[LOWER] + off
                d = b - a; L = float(np.linalg.norm(d))
                if L < 1e-4: continue
                r = 0.22 if i in (UPPER, LOWER, WAIST, HEAD) else 0.12
                bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=L, location=tuple((a + b) / 2))
                o = bpy.context.object
                o.rotation_mode = 'QUATERNION'
                z = np.array([0, 0, 1.0]); n = d / L; ax = np.cross(z, n); s = np.linalg.norm(ax)
                ang = math.atan2(s, float(np.dot(z, n)))
                o.rotation_quaternion = (math.cos(ang / 2), *((ax / s) * math.sin(ang / 2) if s > 1e-6 else (0, 0, 0)))
                o.data.materials.append(skin if i in (HEAD, LFORE, RFORE, LHAND, RHAND) else mat)
            bpy.ops.mesh.primitive_plane_add(size=1.6, location=(-k * 2.6, 0, 0))
        w = len(frames) * 2.6
        bpy.ops.object.camera_add(location=(-(w / 2 - 1.3), 22, 3.2), rotation=(math.radians(88), 0, math.radians(180)))
        cam = bpy.context.object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = w + 1
        sc.camera = cam
        sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 16   # headless: no GL for Workbench/EEVEE
        bpy.ops.object.light_add(type='SUN', rotation=(math.radians(40), math.radians(-30), math.radians(-60)))
        bpy.context.object.data.energy = 4
        sc.render.resolution_x = int(160 * len(frames)); sc.render.resolution_y = 300
        sc.render.filepath = os.path.join(outdir, sname + '.png')
        sc.world = bpy.data.worlds.new('w'); sc.world.use_nodes = True
        sc.world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.35, 0.65, 0.7, 1)   # OoT teal sky
        bpy.ops.render.render(write_still=True)
        print('preview', sc.render.filepath)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rig', required=True)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', '..', 'shared', 'link_anims.h'))
    ap.add_argument('--preview')
    a = ap.parse_args()
    rig = Rig(a.rig)
    write_header(rig, os.path.normpath(a.out))
    print('wrote', os.path.normpath(a.out))
    if a.preview: preview(rig, a.preview)


if __name__ == '__main__':
    main()
