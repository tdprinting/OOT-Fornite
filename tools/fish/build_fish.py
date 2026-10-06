"""Blender authoring file and animated GLB from the same reef meshes the game uses.

blender --background --python tools/fish/build_fish.py
Optional -- --render writes assets/fish/previews/lobby-reef.png.
Swim shape keys bend the body/tail and paddle the fins; 24-frame loop.
"""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
import make_fish

OUT=make_fish.ROOT/'assets/fish'
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
materials={}

def material(color):
    if color not in materials:
        m=bpy.data.materials.new('Paint_%02x%02x%02x'%color)
        m.diffuse_color=(*[c/255 for c in color],1)
        m.use_nodes=True
        shader=m.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value=m.diffuse_color
        shader.inputs['Roughness'].default_value=0.86
        materials[color]=m
    return materials[color]

def xyz(v):return (v[0]/100,-v[2]/100,v[1]/100)

def pose(v,part,phase):
    x,y,z=v
    if part>=5:
        side=1 if part==5 else -1
        if part in (5,6):y+=max(0,math.sin(phase+side*1.1+z*0.18))*2.5;z+=math.sin(phase+side*1.1)*2
        elif part==7:y+=math.sin(phase)*2;z-=2+math.sin(phase)*1.2
        return xyz((x,y,z))
    bend=max(0,min(1,(-z+8)/44))
    x+=math.sin(phase+z*0.045)*bend*bend*5
    if part==1:
        angle=math.sin(phase-0.8)*0.36
        x,z=math.cos(angle)*x+math.sin(angle)*(z+27),-math.sin(angle)*x+math.cos(angle)*(z+27)-27
    elif part in (2,3):y+=math.sin(phase*1.7+(0 if part==2 else 1.2))*abs(x)*0.22
    elif part==4:x+=math.sin(phase*1.3)*max(0,y-9)*0.16
    return xyz((x,y,z))

def obj(name,triangles,animated=False):
    vertices=[xyz(v) for t in triangles for v in t[:3]]
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],[tuple(range(i,i+3)) for i in range(0,len(vertices),3)])
    mesh.update()
    ob=bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(ob)
    slots={}
    for polygon,t in zip(mesh.polygons,triangles):
        color=t[3]
        if color not in slots:
            slots[color]=len(mesh.materials);mesh.materials.append(material(color))
        polygon.material_index=slots[color]
        polygon.use_smooth=False
    if animated:
        ob.shape_key_add(name='Rest')
        for k in range(8):
            key=ob.shape_key_add(name='Swim_%d'%k)
            for i,t in enumerate(triangles):
                for j,v in enumerate(t[:3]):key.data[i*3+j].co=pose(v,t[4],k*math.tau/8)
            for frame in range(1,26):
                phase=((frame-1)/24*8)%8
                diff=min(abs(phase-k),8-abs(phase-k))
                key.value=max(0,1-diff)
                key.keyframe_insert(data_path='value',frame=frame)
        ob['animation']='Swim: body bend, tail fan, pectoral paddles, dorsal flutter'
    return ob

tank=obj('Hylian_Reef_Aquarium',make_fish.tank())
fish=[]
locations=[(-75,117,-15),(-105,83,28),(30,146,20),(84,120,-27),(-28,145,8),(75,80,26)]
for i in range(6):
    species=int(i>=3)
    ob=obj(('Clownfish' if species==0 else 'CleanerWrasse')+'_%02d'%i,make_fish.mesh(species),True)
    ob.location=xyz(locations[i])
    ob.rotation_euler[2]=[-0.9,0.8,-1.4,1.1,-0.8,2.4][i]
    scale=0.55*(0.72 if i==1 else 0.88 if i==2 else 0.8 if i==5 else 1)
    ob.scale=(scale,scale,scale)
    ob['personality']=['curious visitor watcher','shy anemone youngster','playful weaver','busy cleaner','patient cleaner','quick scout'][i]
    fish.append(ob)
for i in range(3):
    ob=obj('HermitCrab_%02d'%i,make_fish.crab(),True)
    ob.location=xyz((i*85-85,32,32 if i%2 else -35))
    ob.rotation_euler[2]=i*1.8
    ob.scale=(0.75,)*3 if i==1 else (0.9,)*3
    ob['personality']=['sand sifter','shy shell dweller','busy beachcomber'][i]
    fish.append(ob)

# The game makes its translucent panes procedurally; these are the editable preview equivalent.
glass=bpy.data.materials.new('Water_Glass')
glass.use_nodes=True
shader=glass.node_tree.nodes.get('Principled BSDF')
shader.inputs['Base Color'].default_value=(0.18,0.64,0.72,1)
shader.inputs['Alpha'].default_value=0.035
shader.inputs['Roughness'].default_value=0.24
if hasattr(glass,'surface_render_method'):glass.surface_render_method='BLENDED'
panes=[((-160,34,-86),(160,34,-86),(160,183,-86),(-160,183,-86)),
       ((-160,34,86),(-160,183,86),(160,183,86),(160,34,86)),
       ((-160,34,-86),(-160,183,-86),(-160,183,86),(-160,34,86)),
       ((160,34,-86),(160,34,86),(160,183,86),(160,183,-86)),
       ((-160,181,-86),(160,181,-86),(160,181,86),(-160,181,86))]
me=bpy.data.meshes.new('Water panes')
me.from_pydata([xyz(p) for q in panes for p in q],[],[tuple(range(i,i+4)) for i in range(0,20,4)])
water=bpy.data.objects.new('Water panes',me);bpy.context.collection.objects.link(water);me.materials.append(glass)
root_ob=bpy.data.objects.new('LobbyReefScale_50percent',None)
bpy.context.collection.objects.link(root_ob)
for ob in [tank,water]+fish:ob.parent=root_ob
root_ob.scale=(0.5,0.5,0.5)

scene=bpy.context.scene
scene.frame_start=1;scene.frame_end=24;scene.render.fps=24
scene.frame_set(5)
scene.render.engine='CYCLES'
scene.cycles.samples=24
scene.render.resolution_x=1024;scene.render.resolution_y=768;scene.render.resolution_percentage=100
scene.world.color=(0.13,0.13,0.13)
bpy.ops.object.camera_add(location=(2.55,3.8,1.9))
camera=bpy.context.object;camera.name='Preview Camera'
camera.rotation_euler=(Vector((0,0,0.51))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=2.55;scene.camera=camera
for name,loc,energy,size in [('Warm key',(1,3,4),180,4),('Blue rim',(-3,-2,3),140,3)]:
    bpy.ops.object.light_add(type='AREA',location=loc)
    light=bpy.context.object;light.name=name;light.data.energy=energy;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(Vector((0,0,0.5))-light.location).to_track_quat('-Z','Y').to_euler()
scene.view_settings.view_transform='Standard'
scene.render.image_settings.file_format='PNG'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'lobby_reef.blend'))
bpy.ops.object.select_all(action='DESELECT')
for ob in [root_ob,tank,water]+fish:ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'lobby_reef.glb'),use_selection=True,export_format='GLB',export_animations=True,export_morph=True)
if '--render' in sys.argv:
    (OUT/'previews').mkdir(exist_ok=True)
    scene.render.filepath=str(OUT/'previews/lobby-reef.png')
    bpy.ops.render.render(write_still=True)
print('Saved editable reef, swim shape-key animation and GLB')
