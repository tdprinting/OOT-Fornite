"""Blender authoring/export for connected trees and an OoT-style victory crown.

blender -b --python tools/scenery/build_playtest_models.py
Outputs editable .blend files, previews and shared/playtest_models.h.
Tree bounds retain the existing foliage footprint and height; meshes are cached.
"""
from pathlib import Path
import math
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
ALL = []


def material(name, rgb):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*rgb, 1)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*rgb,1)
    bsdf.inputs['Roughness'].default_value = .35 if 'gold' in name else .8
    bsdf.inputs['Metallic'].default_value = .65 if 'gold' in name else 0
    return m


def cylinder(name, a, b, r0, r1, mat, vertices=12):
    axis = Vector(b)-Vector(a)
    bpy.ops.mesh.primitive_cone_add(vertices=vertices, radius1=r0, radius2=r1,
        depth=axis.length, location=(Vector(a)+Vector(b))/2)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = axis.to_track_quat('Z', 'Y').to_euler()
    obj.data.materials.append(mat)
    return obj


def blob(name, at, size, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=6, location=at)
    obj = bpy.context.object
    obj.name = name
    obj.scale = size
    obj.data.materials.append(mat)
    return obj


def fuse_wood(objects, name):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    remesh = obj.modifiers.new('Connected trunk and branches', 'REMESH')
    remesh.mode = 'VOXEL'
    remesh.voxel_size = .075
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = obj.modifiers.new('Organic transitions', 'SMOOTH')
    smooth.factor = .6
    smooth.iterations = 3
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    decimate = obj.modifiers.new('Mobile triangle budget', 'DECIMATE')
    decimate.ratio = min(1, 160/max(1,len(obj.data.polygons)*2))
    bpy.ops.object.modifier_apply(modifier=decimate.name)
    return obj


def export(objects, name):
    result = []
    for obj in objects:
        # Export evaluated transforms; do not depend on the saved object origins.
        mesh = obj.to_mesh()
        mesh.calc_loop_triangles()
        mat = obj.data.materials[0]
        base = mat.diffuse_color[:3]
        leaf = int(mat.name.startswith('Leaf'))
        for tri in mesh.loop_triangles:
            for i in tri.vertices:
                normal = (obj.matrix_world.to_3x3().inverted().transposed() @ mesh.vertices[i].normal).normalized()
                light = max(0, normal.dot(Vector((-.4,-.3,.85)).normalized()))
                rgb = [round(min(1,c*(.65+.35*light))*255) for c in base]
                p = obj.matrix_world @ mesh.vertices[i].co
                result.append((p.x*100,max(0,p.z*100) if name.startswith('tree') else p.z*100,-p.y*100,*rgb,leaf))
        obj.to_mesh_clear()
    ALL.append((name,result))
    return len(result)//3


def preview(folder, filename, focus, distance):
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(distance,-distance*.9,distance*.65))
    camera = bpy.context.object
    camera.rotation_euler = (Vector(focus)-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = distance*1.1
    scene.camera = camera
    bpy.ops.object.light_add(type='AREA', location=(-3,-4,7))
    bpy.context.object.data.energy = 750
    bpy.context.object.data.shape = 'DISK'
    bpy.context.object.data.size = 5
    if scene.world is None: scene.world=bpy.data.worlds.new('Preview world')
    scene.world.color = (.15,.19,.25)
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = scene.render.resolution_y = 640
    scene.render.resolution_percentage = 100
    scene.render.filepath = str(folder/filename)
    bpy.ops.render.render(write_still=True)


def trees():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bark = material('Bark',(.39,.25,.13))
    pale = material('Birch bark',(.8,.78,.7))
    leaf = material('Leaf',(.22,.55,.19))
    bright = material('Leaf tips',(.34,.65,.23))
    for shape in range(4):
        # Tall enough to penetrate every canopy; branches start inside the trunk.
        trunk_height = (2.35,3.35,2.35,2.95)[shape]
        radius = (.18,.14,.2,.09)[shape]
        wood = [cylinder('Trunk',(0,0,0),(0,0,trunk_height),radius,radius*.3,pale if shape==3 else bark)]
        tops = []
        if shape == 1:
            for z,r,h in ((.7,.96,1.1),(1.5,.72,1.1),(2.3,.48,1.1)):
                tops.append(cylinder('Pine canopy',(0,0,z),(0,0,z+h),r,0,leaf,16))
        else:
            size = ((.96,.96,.78),(0,0,0),(1.18,1.18,.86),(.72,.72,.90))[shape]
            center = (2,0,1.9,2.3)[shape]
            tops.append(blob('Crown',(0,0,center),size,leaf))
            for sign in (-1,1):
                end=(sign*.65,sign*.22,center-.3)
                wood.append(cylinder('Bough',(0,0,1.0),end,radius*.5,.025,pale if shape==3 else bark,8))
                tops.append(blob('Side crown',end,(.62,.62,.56),bright))
        trunk=fuse_wood(wood,'Connected tree '+str(shape))
        for obj in tops:
            bpy.context.view_layer.objects.active=obj
            if shape != 1:
                decimate=obj.modifiers.new('Mobile foliage budget','DECIMATE');decimate.ratio=.65
                bpy.ops.object.modifier_apply(modifier=decimate.name)
        for obj in [trunk,*tops]:
            for face in obj.data.polygons: face.use_smooth = True
        count=export([trunk,*tops],'tree'+str(shape))
        assert count <= 420, (shape,count)
        # Place the saved preview lineup after export, so each runtime origin is zero.
        for obj in [trunk,*tops]: obj.location.x += shape*3.0
        print('Tree',shape,count,'triangles')
    folder=ROOT/'assets/scenery';folder.mkdir(parents=True,exist_ok=True)
    preview(folder,'trees-preview.png',(4.5,0,1.6),11)
    bpy.ops.wm.save_as_mainfile(filepath=str(folder/'trees.blend'))


def crown():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    gold=material('Triforce gold',(.95,.72,.12))
    emerald=material('Kokiri emerald',(.06,.72,.35))
    ruby=material('Goron ruby',(.85,.08,.1))
    sapphire=material('Zora sapphire',(.12,.35,.9))
    # The crown uses game units (radius 9): adult Link's head, not a giant prop.
    objects=[]
    bpy.ops.mesh.primitive_torus_add(major_segments=24,minor_segments=4,major_radius=.09,minor_radius=.011)
    band=bpy.context.object;band.name='Royal band';band.data.materials.append(gold);objects.append(band)
    for angle in (0,math.pi/2,math.pi,3*math.pi/2):
        x,y=.09*math.sin(angle),-.09*math.cos(angle)
        objects.append(cylinder('Crest',(x,y,0),(x*.95,y*.95,.06),.021,0,gold,3))
    for x,z,mat in ((0,.052,emerald),(-.052,.025,ruby),(.052,.025,sapphire)):
        objects.append(blob('Spiritual stone',(x,-.087,z),(.015,.009,.015),mat))
    # A raised Triforce at the front, three triangles with the central opening.
    for x,z in ((-.014,.019),(.014,.019),(0,.043)):
        vertices=[(x-.014,-.101,z),(x+.014,-.101,z),(x,-.101,z+.024)]
        mesh=bpy.data.meshes.new('Triforce');mesh.from_pydata(vertices,[],[(0,1,2),(2,1,0)])
        obj=bpy.data.objects.new('Triforce',mesh);bpy.context.collection.objects.link(obj);obj.data.materials.append(gold);objects.append(obj)
    count=export(objects,'crown');assert count < 700,count
    folder=ROOT/'assets/victory';folder.mkdir(parents=True,exist_ok=True)
    preview(folder,'crown-preview.png',(0,0,.025),.36)
    bpy.ops.wm.save_as_mainfile(filepath=str(folder/'crown.blend'))
    print('Crown',count,'triangles')


trees();crown()
lines=['// Generated by tools/scenery/build_playtest_models.py. Edit Blender authoring, not this header.',
       '#pragma once','#include <cstdint>','namespace royale::playtest_models {',
       'struct Vertex { float x,y,z; uint8_t r,g,b,leaf; };']
for name,vertices in ALL:
    lines.append('inline constexpr Vertex '+name+'[] = {')
    lines.extend('{'+','.join(f'{v:.3f}f' for v in row[:3])+','+','.join(str(v) for v in row[3:])+'},' for row in vertices)
    lines.append('};')
lines.append('}')
(ROOT/'shared/playtest_models.h').write_text('\n'.join(lines)+'\n')
