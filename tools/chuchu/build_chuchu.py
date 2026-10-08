"""Blender 5: author textured ChuChus, shape-key actions, and bake game assets.
Run blender -b --python tools/chuchu/build_chuchu.py. No downloaded game assets.
The .blend is the editable source; export reads its mesh, UVs, textures and keys.
"""
import bpy
import math
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/chuchu'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
COLORS = [(0.85,.19,.055),(.43,.70,.045),(.95,.63,.04),(.04,.56,.74),(.19,.075,.29)]
NAMES = ['Red','Green','Yellow','Blue','Dark']
CLIPS = ['Idle','Wobble','Lunge','Puddle','Emerge','Discharge','Hurt','Petrify']
TEX_W = TEX_H = 256
# Foot skirt, pinched stalk, jelly belly and broad slanted head.
PROFILE = [(0,.56),(.07,.70),(.16,.43),(.31,.22),(.50,.25),(.80,.43),(1.12,.57),(1.43,.62),(1.66,.55),(1.80,.36),(1.87,.03)]
verts=[]
for z,r in PROFILE:
    for i in range(20):
        a=2*math.pi*i/20
        skirt = 1 + .18*math.cos(a*3) if z < .17 else 1
        verts.append((r*skirt*math.sin(a),-r*skirt*math.cos(a),z))
faces=[]
for j in range(len(PROFILE)-1):
    for i in range(20):
        a=j*20+i;b=j*20+(i+1)%20;c=b+20;d=a+20
        faces.extend([(a,b,c),(a,c,d)])
faces += [tuple(reversed(range(20))), tuple(range(200,220))]
me=bpy.data.meshes.new('ChuJellyTopology');me.from_pydata(verts,[],faces);me.update()
uv=me.uv_layers.new(name='JellyUV')
for p in me.polygons:
    indices=[me.loops[l].vertex_index%20 for l in p.loop_indices]
    seam=0 in indices and 19 in indices
    for li in p.loop_indices:
        vi=me.loops[li].vertex_index;i=vi%20
        # front at u=.5; use periodic distance in texture authoring below
        u=i/20
        if seam and i==0:u=1
        uv.data[li].uv=(u,verts[vi][2]/1.87)

def texture(k):
    im=bpy.data.images.new(NAMES[k]+'Jelly',width=TEX_W,height=TEX_H,alpha=True)
    pix=[]
    for y in range(TEX_H):
        v=y/(TEX_H-1)
        for x in range(TEX_W):
            u=x/TEX_W; signed=u if u<.5 else u-1
            shine=.82+.16*v+.08*math.sin(u*math.tau+1)
            c=[min(1,t*shine) for t in COLORS[k]]
            def paint(color, distance, threshold, feather=.025):
                w=max(0,min(1,(threshold-distance)/feather+.5))
                w=w*w*(3-2*w)
                return [a*(1-w)+b*w for a,b in zip(c,color)]
            for eye in [-.085,.085]:
                d=((signed-eye)/.054)**2+((v-.795)/.085)**2
                c=paint([.22,.06,.27] if k<4 else [.52,.16,.02],d,1.5,.08)
                c=paint([.92,.025,.48] if k<4 else [1,.18,0],d,1.12,.08)
                c=paint([.94,.98,.43] if k<4 else [1,.86,.02],d,.70,.07)
                pupil=((signed-eye-.007)/.012)**2+((v-.81)/.018)**2
                c=paint([.035,.025,.04],pupil,1,.25)
            if abs(signed)<.17:c=paint([.20,.055,.24],abs(v-(.625+.55*signed*signed)),.009,.004)
            pix.extend(c+[1])
    im.pixels=pix;im.filepath_raw=str(OUT/(NAMES[k].lower()+'.png'));im.file_format='PNG';im.save();im.pack()
    return im

objects=[]
for k,name in enumerate(NAMES):
    obj=bpy.data.objects.new(name+'ChuChu',me.copy());bpy.context.collection.objects.link(obj)
    obj.location.x=(k-2)*1.65
    if k == 4: obj.scale=(1.18,1,.78)
    mat=bpy.data.materials.new(name+'CelJelly');mat.diffuse_color=(*COLORS[k],1);mat.use_nodes=True
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=texture(k);tex.interpolation='Linear'
    bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.43
    mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color']);obj.data.materials.append(mat)
    obj.shape_key_add(name='Basis')
    for ci,clip in enumerate(CLIPS):
        key=obj.shape_key_add(name=clip)
        for i,p in enumerate(verts):
            x,y,z=p;h=z/1.87
            sx,sz,lean=1,1,0
            if clip=='Idle':sx,sz=1.035,.965
            if clip=='Wobble':sx,sz,lean=.88,1.12,.16
            if clip=='Lunge':sx,sz,lean=1.20,.73,-.55
            if clip=='Puddle':sx,sz=1.65,.08
            if clip=='Emerge':sx,sz=.70,1.23
            if clip=='Discharge':sx,sz,lean=1.14,.90,.09
            if clip=='Hurt':sx,sz,lean=1.23,.78,.25
            if clip=='Petrify':sx,sz=.97,.97
            key.data[i].co=(x*sx+lean*h*h,y*sx+(lean if clip=='Lunge' else 0)*h,z*sz)
        start=1+ci*30
        for frame,value in [(start,0),(start+12,1),(start+24,0)]:
            key.value=value;key.keyframe_insert('value',frame=frame)
        key.value=0
    objects.append(obj)
    # Emergence begins as a flat puddle, stretches upwards, then settles.
    puddle=obj.data.shape_keys.key_blocks['Puddle']
    for frame,value in [(121,1),(133,0),(145,0)]:
        puddle.value=value;puddle.keyframe_insert('value',frame=frame)
    puddle.value=0

scene=bpy.context.scene;scene.render.fps=24;scene.frame_end=240
for i,name in enumerate(CLIPS):scene.timeline_markers.new(name,frame=i*30+1)
scene.frame_set(1)
# Orthographic presentation camera and soft lights.
bpy.ops.object.camera_add(location=(5,-12,6));cam=bpy.context.object
cam.rotation_euler=(Vector((0,0,.85))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=9.5;scene.camera=cam
for pos,power,size in [((1,-5,7),1100,7),((-5,2,4),800,5)]:
    bpy.ops.object.light_add(type='AREA',location=pos);bpy.context.object.data.energy=power;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=size
scene.world.color=(.22,.24,.28)
scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=1400;scene.render.resolution_y=650;scene.render.resolution_percentage=100
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'chuchu.blend'))

# Export baked deformation poses from the authored shape keys; game interpolates.
obj=objects[0];obj.data.calc_loop_triangles();corners=[]
for tri in obj.data.loop_triangles:
    for li in tri.loops:
        loop=obj.data.loops[li];u,v=obj.data.uv_layers.active.data[li].uv
        corners.append((loop.vertex_index,round(u*TEX_W*32),round((1-v)*TEX_H*32)))
def rows(values,n=12):return '\n'.join('    '+', '.join(values[i:i+n])+',' for i in range(0,len(values),n))
lines=['// Generated by tools/chuchu/build_chuchu.py; edit the Blender source/generator.','#pragma once','#include <cstdint>','namespace royale::chu {',
       'struct Point { int16_t x,y,z; };','struct Corner { uint16_t point; int16_t s,t; };',
       'enum Clip { Idle, Wobble, Lunge, Puddle, Emerge, Discharge, Hurt, Petrify };',
       f'inline constexpr int kFrames=16, kPoints=220, kTextureWidth={TEX_W}, kTextureHeight={TEX_H};',
       'inline constexpr Corner kCorners[] = {',rows(['{%d,%d,%d}'%c for c in corners]),'};',
       'inline constexpr Point kPoses[8][16][220] = {']
for clip in CLIPS:
    lines.append('{')
    for frame in range(16):
        t=frame/15;weight=math.sin(t*math.pi)**2
        if clip in ['Puddle','Petrify']:weight=1
        points=[]
        for i,(a,b) in enumerate(zip(obj.data.shape_keys.key_blocks['Basis'].data,obj.data.shape_keys.key_blocks[clip].data)):
            if clip=='Emerge':
                flat=obj.data.shape_keys.key_blocks['Puddle'].data[i].co
                p=flat.lerp(b.co,t*2) if t<.5 else b.co.lerp(a.co,(t-.5)*2)
            else:p=a.co.lerp(b.co,weight)
            points.append('{%d,%d,%d}'%(round(p.x*100),round(p.z*100),round(-p.y*100)))
        lines += ['{',rows(points),'},']
    lines.append('},')
lines += ['};',f'alignas(8) inline constexpr uint8_t kTextures[5][{TEX_W*TEX_H*2}] = {{']
for name in NAMES:
    im=bpy.data.images[name+'Jelly'];p=list(im.pixels);data=[]
    for y in reversed(range(TEX_H)):
        for x in range(TEX_W):
            i=(y*TEX_W+x)*4;r,g,b=[max(0,min(31,round(p[i+j]*31))) for j in range(3)]
            value=(r<<11)|(g<<6)|(b<<1)|1;data.extend([str(value>>8),str(value&255)])
    lines+=['{',rows(data,32),'},']
lines+=['};','} // namespace royale::chu']
(ROOT/'shared/chuchu_model.h').write_text('\n'.join(lines)+'\n')
(OUT/'export_report.json').write_text(json.dumps({'points':220,'triangles':len(corners)//3,'clips':CLIPS,'frames':16,'textureWidth':TEX_W,'textureHeight':TEX_H,'textureBytesPerVariant':TEX_W*TEX_H*2},indent=2))
scene.render.filepath=str(OUT/'preview.png');bpy.ops.render.render(write_still=True)
