"""Render a short motion review of Maya, including authored facial changes."""
import bpy, os, sys, math
from mathutils import Vector
HERE=os.path.dirname(__file__);sys.path.insert(0,HERE)
import personality as P
ROOT=os.path.abspath(os.path.join(HERE,'../..'));OUT=os.path.join(ROOT,'assets/maya/previews')
os.makedirs(OUT,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'assets/maya/maya.blend'))
scene=bpy.context.scene;rig=bpy.data.objects['MayaRig']
bpy.ops.object.camera_add(location=(2,-3,1.8));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.69))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=1.65;scene.camera=cam
bpy.ops.object.light_add(type='AREA',location=(1,-3,4));bpy.context.object.data.energy=500;bpy.context.object.data.size=5
scene.world=bpy.data.worlds.new('Preview');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.22,.30,1)
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=320;scene.render.resolution_y=400;scene.render.resolution_percentage=100
face=next(n for n in bpy.data.materials['MayaFace'].node_tree.nodes if n.type=='TEX_IMAGE')
selected=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ('walk','wave','giggle','draw','tablet','cheer')
for clip in selected:
    cam.location=(1.6,2.7,1.85) if clip in ('tablet','draw') else (2,-3,1.8)
    cam.rotation_euler=(Vector((0,0,.69))-cam.location).to_track_quat('-Z','Y').to_euler()
    T=next(c[1] for c in P.CLIPS if c[0]==clip)
    rig.animation_data.action=bpy.data.actions[clip]
    for i in range(12):
        t=i*T/12;scene.frame_set(round(t*20));face.image=bpy.data.images['maya_face_'+P.face_for(clip,t,T)]
        scene.render.filepath=os.path.join(OUT,'%s_%02d.png'%(clip,i));bpy.ops.render.render(write_still=True)
