"""Export the four ally models to shared/ally_<kind>_model.h: 32-vertex batches, the RGBA5551 body atlas and faces, and packed bone poses.
Run: python tools/allies/export_allies.py [kind ...] [--dump DIR]   (--dump also writes JSON for tools/allies/preview_allies.py)
"""
import json
import os
import sys

import bpy
import numpy as np
from PIL import Image
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import build_allies as B  # noqa: E402

SCALE = 100.0        # metres to game units
POS_FRAC = 16.0
MAX_BATCH = 32
PLANTED = ('idle', 'walk', 'attack', 'attack2', 'heal', 'rest')   # (not the dance: it hops)   # clips whose lowest point is put on the ground every frame
C = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))   # Blender (Z up, facing -Y) to game (Y up, facing +Z)


def to_game(v):
    return C @ v


def rgba5551(path):
    """A PNG to RGBA16 5-5-5-1, big-endian, top row first."""
    a = np.asarray(Image.open(path).convert('RGB'), np.int32)
    r, g, b = ((a[:, :, k] * 31 + 127) // 255 for k in range(3))
    v = (r << 11) | (g << 6) | (b << 1) | 1
    out = np.stack([(v >> 8) & 0xFF, v & 0xFF], -1).astype(np.uint8)
    return a.shape[1], a.shape[0], out.tobytes()


def rows(values, per, indent='    '):
    return '\n'.join(indent + ', '.join(values[i:i + per]) + ',' for i in range(0, len(values), per))


def export(kind, dump_dir=None):
    bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, 'assets', 'allies', '%s.blend' % kind))
    rig = bpy.data.objects['%sRig' % kind.capitalize()]
    mesh_obj = bpy.data.objects[kind.capitalize()]
    me = mesh_obj.data
    bones = B.BONES
    assert sorted(b.name for b in rig.data.bones) == sorted(bones), 'bones'
    bidx = {n: i for i, n in enumerate(bones)}
    gbone = {g.index: bidx[g.name] for g in mesh_obj.vertex_groups if g.name in bidx}

    me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    normals = me.corner_normals
    body_path, face_paths = B.texture_paths(kind)
    bw, bh, body = rgba5551(body_path)
    faces = [rgba5551(p) for p in face_paths]
    fw, fh = faces[0][0], faces[0][1]
    size = {0: (bw, bh), 1: (fw, fh)}

    def corner(li, vi, tex):
        u, v = uv[li].uv
        W, H = size[tex]
        s, t = u * W, (1.0 - v) * H
        p = to_game(me.vertices[vi].co) * SCALE
        n = to_game(normals[li].vector).normalized()
        gs = sorted(((g.weight, gbone[g.group]) for g in me.vertices[vi].groups if g.group in gbone and g.weight > .001), reverse=True)[:2]
        if not gs:
            gs = [(1.0, bidx['root'])]
        tot = sum(g[0] for g in gs)
        b0, w0 = gs[0][1], gs[0][0] / tot
        b1 = gs[1][1] if len(gs) > 1 else b0
        key = (vi, tex, round(s * 32), round(t * 32), round(n.x * 127), round(n.y * 127), round(n.z * 127))
        return key, (p.x, p.y, p.z, round(n.x * 127), round(n.y * 127), round(n.z * 127), round(s * 32), round(t * 32), b0, b1, round(w0 * 255))

    verts, batches, tris = [], [], []
    cur = None
    order = sorted(me.loop_triangles, key=lambda lt: me.polygons[lt.polygon_index].material_index)   # body first, then the face
    for lt in order:
        tex = me.polygons[lt.polygon_index].material_index
        cs = [corner(li, vi, tex) for li, vi in zip(lt.loops, lt.vertices)]
        new = len({k for k, _ in cs if cur is None or k not in cur['map']})
        if cur is None or cur['tex'] != tex or len(cur['map']) + new > MAX_BATCH:
            cur = {'map': {}, 'first': len(verts), 'tfirst': len(tris), 'tex': tex}
            batches.append(cur)
        idx = []
        for k, data in cs:
            if k not in cur['map']:
                cur['map'][k] = len(cur['map'])
                verts.append(data)
            idx.append(cur['map'][k])
        tris.append(idx)
    for i, b in enumerate(batches):
        b['count'] = len(b['map'])
        b['tcount'] = (batches[i + 1]['tfirst'] if i + 1 < len(batches) else len(tris)) - b['tfirst']


    # poses: re-evaluated from the same pose functions the build used
    ctl = B.Ctl(rig)
    rig.animation_data.action = None
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    rest_inv = {b.name: b.matrix_local.inverted() for b in rig.data.bones}
    poses, clips = [], []
    rest = [(Vector(v[:3]), v[8], v[9], v[10] / 255.0) for v in verts]
    for name, seconds, loops, fps in B.CLIPS:
        n = B.frames_of(seconds, loops, fps)
        first = len(poses) // 7 // len(bones)
        for f in range(n):
            B.pose_for(ctl, kind, name, f, n, loops)
            ctl.apply(rig)
            frame = []
            for bn in bones:
                m = rig.pose.bones[bn].matrix @ rest_inv[bn]
                q = m.to_quaternion().normalized()
                if q.w < 0:
                    q.negate()
                vec = to_game(Vector((q.x, q.y, q.z)))
                frame.append((Quaternion((q.w, vec.x, vec.y, vec.z)), to_game(m.translation) * SCALE))
            if name in PLANTED:     # feet on the ground: the lowest point of the posed model goes to y = 0
                low = min(((frame[b0][0] @ p + frame[b0][1]) * w + (frame[b1][0] @ p + frame[b1][1]) * (1 - w)).y for p, b0, b1, w in rest)
                frame = [(q, t - Vector((0, low, 0))) for q, t in frame]
            for q, t in frame:
                poses += [round(c * 32767) for c in (q.x, q.y, q.z, q.w)] + [round(c * POS_FRAC) for c in t]
        clips.append((name, n, float(fps), loops, first))
    frames = len(poses) // 7 // len(bones)
    top = max(v[1] for v in verts)
    ns = kind
    o = ['#pragma once',
         '// GENERATED by tools/allies/export_allies.py from assets/allies/%s.blend (made by tools/allies/build_allies.py). Do not edit by hand.' % kind,
         '// %s ally, stylised (OoT x Majora x Fortnite): %d vertices in %d batches, %d triangles, %d bones, %d clips, %d frames.' % (
             kind.capitalize(), len(verts), len(batches), len(tris), len(bones), len(clips), frames),
         '// Game units, Y up, facing +Z, standing on y = 0. Posed and skinned by shared/ally_anim.h.',
         '#include "ally_model_types.h"', '', 'namespace royale {', 'namespace allymodel {', 'namespace %s {' % ns, '',
         'constexpr int kVertCount = %d;' % len(verts),
         'inline constexpr Vert kVerts[kVertCount] = {',
         rows(['{%.2ff, %.2ff, %.2ff, %d, %d, %d, %d, %d, %d, %d, %d}' % v for v in verts], 3), '};',
         'constexpr int kBatchCount = %d;' % len(batches),
         'inline constexpr Batch kBatches[kBatchCount] = {',
         rows(['{%d, %d, %d, %d, %d}' % (b['first'], b['count'], b['tfirst'], b['tcount'], b['tex']) for b in batches], 6), '};',
         'constexpr int kTriCount = %d;' % len(tris),
         'inline constexpr uint8_t kTris[kTriCount][3] = {',
         rows(['{%d, %d, %d}' % tuple(t) for t in tris], 12), '};', '',
         '// Textures: RGBA16 5-5-5-1, big-endian, top row first. The %d x %d body atlas, then the %d x %d face once per expression (%s).' % (
             bw, bh, fw, fh, ', '.join(B.FACES)),
         'static_assert(kBodyW == %d && kBodyH == %d && kFaceW == %d && kFaceH == %d, "texture size");' % (bw, bh, fw, fh),
         'alignas(8) inline constexpr uint8_t kBody[kBodyBytes] = {',
         rows(['0x%02X' % x for x in body], 32), '};',
         'alignas(8) inline constexpr uint8_t kFaces[kFaceCount][kFaceBytes] = {']
    for _, _, a in faces:
        o += ['  {', rows(['0x%02X' % x for x in a], 32, '    '), '  },']
    o += ['};', '',
          'inline constexpr ClipInfo kClips[kClipCount] = {',
          rows(['{"%s", %d, %.1ff, %s, %d}' % (n, f, fps, 'true' if lp else 'false', first) for n, f, fps, lp, first in clips], 3), '};',
          'constexpr int kFrameCount = %d;' % frames,
          '// Per frame, per bone: rotation quaternion x, y, z, w (x32767) and offset x, y, z (x kPosFrac). A rest vertex v goes to q*v + offset.',
          'inline constexpr int16_t kPoses[kFrameCount * kBoneCount * 7] = {',
          rows([str(v) for v in poses], 28), '};', '',
          'inline constexpr Model kModel = {"%s", kVerts, kVertCount, kBatches, kBatchCount, kTris, kTriCount, kBody, kFaces, kClips, kPoses, kFrameCount, %.1ff, %.1ff};' % (
              kind, B.GAIT[kind]['speed'], top),
          '', '} // namespace %s' % ns, '} // namespace allymodel', '} // namespace royale', '']
    path = os.path.join(ROOT, 'shared', 'ally_%s_model.h' % kind)
    with open(path, 'w') as f:
        f.write('\n'.join(o))
    print('wrote %s: %d verts, %d batches, %d tris, %d frames' % (path, len(verts), len(batches), len(tris), frames))
    if dump_dir:
        os.makedirs(dump_dir, exist_ok=True)
        with open(os.path.join(dump_dir, '%s.json' % kind), 'w') as f:
            json.dump({'verts': verts, 'batches': [(b['first'], b['count'], b['tfirst'], b['tcount'], b['tex']) for b in batches], 'tris': tris,
                       'body': [bw, bh, list(body)], 'faces': [[w, h, list(a)] for w, h, a in faces], 'clips': clips, 'poses': poses, 'bones': bones, 'posFrac': POS_FRAC}, f)


if __name__ == '__main__':
    args = sys.argv[1:]
    dump = None
    if '--dump' in args:
        dump = args[args.index('--dump') + 1]
    kinds = [a for a in args if a in B.KINDS] or B.KINDS
    for k in kinds:
        export(k, dump)
