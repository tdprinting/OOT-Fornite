"""Clean and optimise the Tripo landscape GLB (Blender is Z-up: X east, Y = game z, Z = game height).
Weld, drop floating dust, decimate, scale to game units, centre, then bake a 256 x 256 height grid and colours."""
import bpy, bmesh, numpy as np, os, json
from mathutils import Vector
from mathutils.bvhtree import BVHTree
OUT = '/home/user/scratch/landscape'
SRC = '/root/.claude/uploads/9823d230-a39c-59ca-b4a1-3933a40433fb/ec7b8ec3-fantasylandscape3dmodel.glb'
XY = 15900.0          # model units -> game units across (0.933 wide -> 14830, like Moonfall's 14825 x 15411 plane)
HZ = 4100.0           # height: 0.34 high -> about 1390 at the peak
DECIMATE = float(os.environ.get('DECIMATE', '0.25'))
MIN_ISLAND = int(os.environ.get('MIN_ISLAND', '60'))
G = int(os.environ.get('GRID', '256'))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
obs = [o for o in bpy.context.scene.objects if o.type == 'MESH']
ob = obs[0]
bpy.context.view_layer.objects.active = ob
bm = bmesh.new(); bm.from_mesh(ob.data)
n0 = len(bm.faces)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0002)
seen = set(); dust = []
for f in bm.faces:
    if f.index in seen: continue
    comp = []; stack = [f]; seen.add(f.index)
    while stack:
        g = stack.pop(); comp.append(g)
        for e in g.edges:
            for h in e.link_faces:
                if h.index not in seen: seen.add(h.index); stack.append(h)
    if len(comp) < MIN_ISLAND: dust += comp
bmesh.ops.delete(bm, geom=dust, context='FACES')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
bm.to_mesh(ob.data); bm.free()
print('faces before', n0, 'after weld', len(ob.data.polygons), 'dust faces removed', len(dust), flush=True)
dec = ob.modifiers.new('dec', 'DECIMATE'); dec.decimate_type = 'COLLAPSE'; dec.ratio = DECIMATE
bpy.ops.object.modifier_apply(modifier=dec.name)
print('after decimate', len(ob.data.polygons), flush=True)
ob.scale = (XY, XY, HZ)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
cx = (min(v.x for v in bb) + max(v.x for v in bb)) / 2; cy = (min(v.y for v in bb) + max(v.y for v in bb)) / 2
ob.location = (-cx, -cy, -min(v.z for v in bb))
bpy.ops.object.transform_apply(location=True)
bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
bounds = {k: [round(min(v[i] for v in bb)), round(max(v[i] for v in bb))] for k, i in (('x', 0), ('z', 1), ('y', 2))}
print('game bounds (x, z, y)', bounds, flush=True)
# ---- grid: ray down (Blender -Z) through the mesh; colour from the base colour texture at the hit face's UV ----
me = ob.data
img = None
for node in me.materials[0].node_tree.nodes:
    if node.type == 'TEX_IMAGE' and node.image and 'basecolor' in node.image.name.lower(): img = node.image
pix = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4)
bvh = BVHTree.FromObject(ob, bpy.context.evaluated_depsgraph_get())
uvl = me.uv_layers.active.data
x0, x1 = bounds['x']; z0, z1 = bounds['z']
top = bounds['y'][1] + 100
H = np.full((G, G), np.nan, dtype=np.float32); C = np.zeros((G, G, 3), dtype=np.float32)
for j in range(G):
    for i in range(G):
        gx = x0 + (i + 0.5) * (x1 - x0) / G; gz = z0 + (j + 0.5) * (z1 - z0) / G
        # Blender: game x = bx, game z = by, game y = bz
        loc, nrm, idx, dist = bvh.ray_cast(Vector((gx, gz, top)), Vector((0, 0, -1)))
        if idx is None: continue
        H[j, i] = loc.z
        ids = me.polygons[idx].loop_indices
        uv = sum((uvl[k].uv for k in ids), Vector((0, 0))) / len(ids)
        px = min(img.size[0] - 1, max(0, int(uv.x * img.size[0]))); py = min(img.size[1] - 1, max(0, int(uv.y * img.size[1])))
        C[j, i] = pix[py, px, :3]
np.save(OUT + '/height.npy', H); np.save(OUT + '/colour.npy', C)
meta = {'x': bounds['x'], 'z': bounds['z'], 'y': bounds['y'], 'grid': G, 'faces': len(me.polygons),
        'hit_cells': int(np.isfinite(H).sum()), 'min_y': float(np.nanmin(H)), 'max_y': float(np.nanmax(H))}
json.dump(meta, open(OUT + '/meta.json', 'w'), indent=1)
print('grid', meta, flush=True)
bpy.ops.wm.save_as_mainfile(filepath=OUT + '/landscape_clean.blend')
bpy.ops.export_scene.gltf(filepath=OUT + '/landscape_clean.glb', export_format='GLB', use_selection=False)
print('saved', flush=True)
