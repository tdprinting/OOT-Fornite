#!/usr/bin/env python3
"""Step 2 of adding a player skin: turn the Blender dump into the game's model header.

  python3 -I tools/skins/export_skin.py tools/skins/<skin>.json [--dump DIR]

Reads assets/skins/<id>/<id>_dump.json (from build_skin.py) and the manifest, maps the source rig's bones onto the game's 21 Link limbs
(`bones`: exact names or `prefix_*`), carves the cap's tail off the head onto the hat limb (`hat_tail`), writes every texture as 256 x 256
RGBA16, and writes shared/skin_<id>_model.h: vertices (metres, game axes), batches of up to 32 vertices by texture and limb, triangles,
textures and the per-limb anchors the game binds with at run time. --dump also writes <id>.json for preview_skin.py. See docs/SKINS.md.
Needs numpy and Pillow only.
"""
import argparse, fnmatch, json, os, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIMBS = ['root', 'waist', 'lower', 'r_thigh', 'r_shin', 'r_foot', 'l_thigh', 'l_shin', 'l_foot', 'upper', 'head', 'hat', 'collar',
         'l_shoulder', 'l_forearm', 'l_hand', 'r_shoulder', 'r_forearm', 'r_hand', 'sheath', 'torso']
LIMB = {n: i for i, n in enumerate(LIMBS)}
CHILD = {'l_thigh': 'l_shin', 'l_shin': 'l_foot', 'r_thigh': 'r_shin', 'r_shin': 'r_foot', 'l_shoulder': 'l_forearm', 'l_forearm': 'l_hand',
         'r_shoulder': 'r_forearm', 'r_forearm': 'r_hand'}
MAX_BATCH = 32
TEX = 256


def rgba5551(path):
    """A PNG to 256 x 256 RGBA16 5-5-5-1, big-endian, top row first (the in-game texture limit)."""
    im = Image.open(path).convert('RGB')
    if im.size != (TEX, TEX):
        im = im.resize((TEX, TEX), Image.LANCZOS)
    a = np.asarray(im, np.int32)
    r, g, b = ((a[:, :, k] * 31 + 127) // 255 for k in range(3))
    v = (r << 11) | (g << 6) | (b << 1) | 1
    return np.stack([(v >> 8) & 0xFF, v & 0xFF], -1).astype(np.uint8).tobytes()


def rows(values, per, indent='    '):
    return '\n'.join(indent + ', '.join(values[i:i + per]) + ',' for i in range(0, len(values), per))


def limb_of(bone, table):
    if bone in table:
        return table[bone]
    for pat, limb in table.items():
        if '*' in pat and fnmatch.fnmatchcase(bone, pat):
            return limb
    return None


def export(manifest_path, dump_dir=None):
    m = json.load(open(manifest_path))
    sid = m['id']
    d = json.load(open(os.path.join(ROOT, 'assets', 'skins', sid, sid + '_dump.json')))
    V = np.array(d['V'], float)
    bones = d['bones']
    unknown = set()
    for ws in d['W']:
        for _, b in ws:
            if limb_of(b, m['bones']) is None:
                unknown.add(b)
    if unknown:
        print('bones with no game limb in the manifest (their weight goes to the root):', sorted(unknown))

    # per-vertex limb weights: the source's bone weights, merged by game limb
    VW = []
    for ws in d['W']:
        acc = {}
        for w, b in ws:
            l = limb_of(b, m['bones'])
            if l is None:
                continue
            acc[LIMB[l]] = acc.get(LIMB[l], 0.0) + w
        VW.append(acc)

    # the cap's tail: the part of the hat behind the head goes to the hat limb, blending in over [start, full] metres behind the ears
    hat = m.get('hat_tail')
    hat_anchor = hat_dir = None
    if hat:
        start, full = hat['start'], hat['full']
        tail_vs = set()
        for t in d['tris']:
            if t['m'] == hat['material']:
                tail_vs.update(t['v'])
        band, tip = [], []
        for vi in tail_vs:
            back = -V[vi][2]      # metres behind the origin (the model faces +Z)
            if back >= start - 0.05:
                (band if back < full else tip).append(V[vi])   # the band around the tail's root: where the hat limb's joint is
            k = (back - start) / (full - start)
            if k <= 0:
                continue
            k = min(1.0, k)
            acc = VW[vi]
            for key in list(acc):
                acc[key] *= (1 - k)
            acc[LIMB['hat']] = acc.get(LIMB['hat'], 0.0) + k
        band = np.array(band) if band else np.zeros((1, 3))
        tip = np.array(tip) if tip else band
        hat_anchor = band.mean(0)
        far = tip[np.argsort(-(-tip[:, 2]))[:max(1, len(tip) // 10)]].mean(0)   # the tail's end: the tenth of its vertices furthest back
        hat_dir = far - hat_anchor
        hat_dir = hat_dir / max(1e-6, np.linalg.norm(hat_dir))

    textures = m['textures']
    tex_files = []
    tex_index = {}
    for mat, f in textures.items():
        if f not in tex_files:
            tex_files.append(f)
        tex_index[mat] = tex_files.index(f)
    tex_bytes = [rgba5551(os.path.join(ROOT, m['textures_dir'], f)) for f in tex_files]

    def vert(vi, n, uv, tex):
        s, t = uv[0] * TEX, (1.0 - uv[1]) * TEX
        n = np.array(n, float)
        n = n / max(1e-6, np.linalg.norm(n))
        acc = sorted(VW[vi].items(), key=lambda kv: -kv[1])[:2]
        if not acc:
            acc = [(LIMB['root'], 1.0)]
        tot = sum(w for _, w in acc)
        b0, w0 = acc[0][0], acc[0][1] / tot
        b1 = acc[1][0] if len(acc) > 1 else b0
        key = (vi, tex, round(s * 32), round(t * 32), round(n[0] * 127), round(n[1] * 127), round(n[2] * 127))
        data = (V[vi][0], V[vi][1], V[vi][2], round(n[0] * 127), round(n[1] * 127), round(n[2] * 127), round(s * 32), round(t * 32), b0, b1, round(w0 * 255))
        return key, data

    # triangles grouped by (texture, dominant limb), then batched 32 vertices at a time
    def dominant(t):
        acc = {}
        for vi in t['v']:
            for l, w in VW[vi].items():
                acc[l] = acc.get(l, 0.0) + w
        return max(acc.items(), key=lambda kv: kv[1])[0] if acc else LIMB['root']
    groups = {}
    for t in d['tris']:
        if t['m'] not in tex_index:
            raise SystemExit('material %r has no texture in the manifest' % t['m'])
        groups.setdefault((tex_index[t['m']], dominant(t)), []).append(t)
    verts, batches, tris = [], [], []
    for (tex, limb), ts in sorted(groups.items()):
        cur = None
        for t in ts:
            cs = [vert(vi, n, uv, tex) for vi, n, uv in zip(t['v'], t['n'], t['uv'])]
            new = len({k for k, _ in cs if cur is None or k not in cur['map']})
            if cur is None or len(cur['map']) + new > MAX_BATCH:
                cur = {'map': {}, 'first': len(verts), 'tfirst': len(tris), 'tex': tex, 'limb': limb}
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

    # anchors: where each game limb's joint is on this model, and which way its bone points
    anchors = []
    for name in LIMBS:
        bone = m['anchors'].get(name)
        if name == 'hat' and hat_anchor is not None:
            anchors.append((hat_anchor, hat_dir, 1))
        elif bone and bone in bones:
            head = np.array(bones[bone]['head'])
            child = CHILD.get(name)
            tip = m.get('tips', {}).get(name)
            if child and m['anchors'].get(child) in bones:
                dv = np.array(bones[m['anchors'][child]]['head']) - head
            elif tip in bones:
                dv = np.array(bones[tip]['head']) - head
            else:
                dv = np.array(bones[bone]['tail']) - head
            dv = dv / max(1e-6, np.linalg.norm(dv))
            anchors.append((head, dv, 1))
        else:
            anchors.append((np.zeros(3), np.array([0, 1.0, 0]), 0))
    floor, top = float(V[:, 1].min()), float(V[:, 1].max())
    hd = m.get('hat_world_dir', [0.0, -0.55, -0.83])
    hd = np.array(hd, float); hd /= np.linalg.norm(hd)

    ns = sid
    o = ['#pragma once',
         '// GENERATED by tools/skins/export_skin.py from %s (tools/skins/%s.json). Do not edit by hand.' % (m['source'], sid),
         '// Player skin "%s": %d vertices in %d batches, %d triangles, %d textures of %d x %d. Metres, Y up, facing +Z, bound to the game\'s' % (
             m['name'], len(verts), len(batches), len(tris), len(tex_bytes), TEX, TEX),
         '// Link skeleton at run time by mod/Royale/features/Skins.inc. See docs/SKINS.md.',
         '#include "skin_model_types.h"', '', 'namespace royale {', 'namespace skinmodel {', 'namespace %s {' % ns, '',
         'constexpr int kVertCount = %d;' % len(verts),
         'inline constexpr Vert kVerts[kVertCount] = {',
         rows(['{%.4ff, %.4ff, %.4ff, %d, %d, %d, %d, %d, %d, %d, %d}' % v for v in verts], 3), '};',
         'constexpr int kBatchCount = %d;' % len(batches),
         'inline constexpr Batch kBatches[kBatchCount] = {',
         rows(['{%d, %d, %d, %d, %d, %d}' % (b['first'], b['count'], b['tfirst'], b['tcount'], b['tex'], b['limb']) for b in batches], 6), '};',
         'constexpr int kTriCount = %d;' % len(tris),
         'inline constexpr uint8_t kTris[kTriCount][3] = {',
         rows(['{%d, %d, %d}' % tuple(t) for t in tris], 12), '};', '',
         '// Textures: %s' % ', '.join('%d = %s' % (i, f) for i, f in enumerate(tex_files)),
         'constexpr int kTextureCount = %d;' % len(tex_bytes)]
    for i, tb in enumerate(tex_bytes):
        o += ['alignas(8) inline constexpr uint8_t kTex%d[kTexBytes] = {' % i, rows(['0x%02X' % x for x in tb], 32), '};']
    o += ['inline constexpr const uint8_t* kTextures[kTextureCount] = { %s };' % ', '.join('kTex%d' % i for i in range(len(tex_bytes))), '',
          '// Per game limb: the joint on this model and its bone direction (metres, unit), or have = 0.',
          'inline constexpr Anchor kAnchors[kLimbCount] = {',
          rows(['{%.4ff, %.4ff, %.4ff, %.4ff, %.4ff, %.4ff, %d}' % (a[0][0], a[0][1], a[0][2], a[1][0], a[1][1], a[1][2], a[2]) for a in anchors], 2), '};', '',
          'inline constexpr Model kModel = {"%s", kVerts, kVertCount, kBatches, kBatchCount, kTris, kTriCount, kTextures, kTextureCount, kAnchors, {%.4ff, %.4ff, %.4ff}, %.4ff, %.4ff};' % (
              m['name'], hd[0], hd[1], hd[2], floor, top),
          '', '} // namespace %s' % ns, '} // namespace skinmodel', '} // namespace royale', '']
    path = os.path.join(ROOT, 'shared', 'skin_%s_model.h' % sid)
    with open(path, 'w') as f:
        f.write('\n'.join(o))
    print('wrote %s: %d verts, %d batches, %d tris, %d textures' % (path, len(verts), len(batches), len(tris), len(tex_bytes)))
    if dump_dir:
        os.makedirs(dump_dir, exist_ok=True)
        with open(os.path.join(dump_dir, '%s.json' % sid), 'w') as f:
            json.dump({'name': m['name'], 'verts': verts, 'batches': [(b['first'], b['count'], b['tfirst'], b['tcount'], b['tex'], b['limb']) for b in batches],
                       'tris': tris, 'textures': [list(tb) for tb in tex_bytes], 'anchors': [(list(map(float, a[0])), list(map(float, a[1])), a[2]) for a in anchors],
                       'hatDir': list(map(float, hd)), 'floor': floor, 'top': top}, f)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('manifest')
    ap.add_argument('--dump')
    a = ap.parse_args()
    export(a.manifest, a.dump)
