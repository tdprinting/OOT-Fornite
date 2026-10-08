"""Render textured model and animation frames using Blender (no game runtime needed)."""
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'assets/bokoblin'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'bokoblin.blend'))
rig=bpy.data.objects['BokoblinRig']
for tr in rig.animation_data.nla_tracks:tr.mute=True
scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE_NEXT'
scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100
scene.world.color=(.08,.08,.08)
scene.view_settings.view_transform='Standard'
bpy.ops.object.camera_add(location=(1.6,-2.8,1.35));cam=bpy.context.object
cam.rotation_euler=(Vector((0,0,.5))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=1.4;scene.camera=cam
for loc,power,size in [((1,-3,3),220,3),((-2,-1,1.5),100,2),((1,2,2),160,2)]:
    bpy.ops.object.light_add(type='AREA',location=loc);a=bpy.context.object;a.data.energy=power;a.data.shape='DISK';a.data.size=size;a.rotation_euler=(Vector((0,0,.5))-a.location).to_track_quat('-Z','Y').to_euler()
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=True
for name,frame in [('idle',0),('swing',9),('throw',12),('dance',9),('recover',3),('dead',17)]:
    rig.animation_data.action=bpy.data.actions[name];scene.frame_set(frame);scene.render.filepath=str(OUT/('preview_'+name+'.png'));bpy.ops.render.render(write_still=True)
if '--animate' in __import__('sys').argv:
    dst=OUT/'preview_frames';dst.mkdir(exist_ok=True)
    rig.animation_data.action=bpy.data.actions['dance']
    scene.render.resolution_x=320;scene.render.resolution_y=320
    for f in range(36):
        scene.frame_set(f);scene.render.filepath=str(dst/('%03d.png'%f));bpy.ops.render.render(write_still=True)
