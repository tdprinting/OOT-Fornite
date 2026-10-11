#!/usr/bin/env python3
"""Step 1 of adding a player skin: read the skin's source model with Blender's bpy and write a plain JSON dump for export_skin.py.

  .venv/bin/python -I tools/skins/build_skin.py tools/skins/<skin>.json            (bpy 4.2: pip install bpy==4.2.0, Python 3.11)

Reads the manifest's `source` (FBX, glTF/GLB, OBJ or .blend), drops the materials listed in `drop_materials` (toon outline shells), decimates
each object by the ratio in `decimate` (1.0 keeps it), joins everything into one triangle mesh and writes assets/skins/<id>/<id>_dump.json:
vertices in game axes (metres, Y up, facing +Z, Link's left +X), per-corner normals and UVs, the material of every triangle, up to four bone
weights per vertex (by the source rig's bone names) and every bone's head and tail. Nothing here knows about the game's skeleton; that is
export_skin.py's job. See docs/SKINS.md.
"""
import json, os, sys
import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def to_game(v, up, forward):
    """Source axes -> game axes. Game: +X Link's left, +Y up, +Z forward (his face)."""
    x, y, z = v
    if up == 'Z' and forward == '-Y':
        return (x, z, -y)
    if up == 'Y' and forward == 'Z':
        return (x, y, z)
    if up == 'Y' and forward == '-Z':
        return (-x, y, -z)
    raise SystemExit('unsupported axes up=%s forward=%s' % (up, forward))


def main(manifest_path):
    m = json.load(open(manifest_path))
    src = os.path.join(ROOT, m['source'])
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = os.path.splitext(src)[1].lower()
    if ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=src)
    elif ext in ('.glb', '.gltf'):
        bpy.ops.import_scene.gltf(filepath=src)
    elif ext == '.obj':
        bpy.ops.wm.obj_import(filepath=src)
    elif ext == '.blend':
        bpy.ops.wm.open_mainfile(filepath=src)
    else:
        raise SystemExit('unknown model format ' + ext)
    drop = set(m.get('drop_materials', []))
    ratios = m.get('decimate', {})
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    if not arms:
        raise SystemExit('the model needs a rig (an armature with the bones named in the manifest)')
    arm = arms[0]
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='DESELECT')
        bpy.ops.object.mode_set(mode='OBJECT')
        for p in o.data.polygons:
            mat = o.data.materials[p.material_index].name if p.material_index < len(o.data.materials) and o.data.materials[p.material_index] else ''
            p.select = mat in drop
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.delete(type='FACE')
        bpy.ops.object.mode_set(mode='OBJECT')
        r = float(ratios.get(o.name, 1.0))
        if r < 0.999 and len(o.data.polygons) > 0:
            mod = o.modifiers.new('dec', 'DECIMATE')
            mod.ratio = r
            mod.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier='dec')
        o.select_set(False)
    # one mesh, triangulated, world transform applied
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')
    me = obj.data
    me.calc_loop_triangles()
    up, fwd = m.get('up', 'Z'), m.get('forward', '-Y')
    uv = me.uv_layers.active.data
    normals = me.corner_normals
    gnames = {g.index: g.name for g in obj.vertex_groups}
    mats = [mt.name if mt else '' for mt in me.materials]
    V = [to_game(v.co, up, fwd) for v in me.vertices]
    W = [sorted(((round(g.weight, 4), gnames[g.group]) for g in v.groups if g.weight > 0.001), reverse=True)[:4] for v in me.vertices]
    tris = []
    for lt in me.loop_triangles:
        tris.append({'v': list(lt.vertices), 'n': [to_game(normals[l].vector, up, fwd) for l in lt.loops],
                     'uv': [list(uv[l].uv) for l in lt.loops], 'm': mats[me.polygons[lt.polygon_index].material_index]})
    bones = {b.name: {'parent': b.parent.name if b.parent else None,
                      'head': to_game(arm.matrix_world @ b.head_local, up, fwd), 'tail': to_game(arm.matrix_world @ b.tail_local, up, fwd)}
             for b in arm.data.bones}
    out = os.path.join(ROOT, 'assets', 'skins', m['id'], m['id'] + '_dump.json')
    json.dump({'V': V, 'W': W, 'tris': tris, 'bones': bones}, open(out, 'w'))
    print('wrote', out, len(V), 'vertices', len(tris), 'triangles')


if __name__ == '__main__':
    main(sys.argv[-1])
