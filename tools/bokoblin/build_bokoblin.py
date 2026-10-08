"""Build the original N64-style Bokoblin mesh, rig, textures, clips and GLB.
Run: blender -b --python tools/bokoblin/build_bokoblin.py
No ROM, downloaded model, texture or recorded Nintendo sound is used.
"""
from pathlib import Path
import math
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/bokoblin'
FPS = 20
CLOTH_W = CLOTH_H = SKIN_W = SKIN_H = FACE_W = FACE_H = 128
FACES = ['grin', 'alarm', 'hurt']
CLIPS = [(name, seconds, loop, None) for name, seconds, loop in [
    ('idle', 2, True), ('walk', .8, True), ('alert', .7, False),
    ('swing', .75, False), ('jump', 1, False), ('throw', 1.1, False),
    ('recover', .9, False), ('dance', 1.8, True), ('hurt', .5, False),
    ('flee', .6, True), ('dead', 1, False)]]

def frames_of(seconds, loops):
    return round(seconds * FPS) + (0 if loops else 1)

def make_texture(name, kind, expression='grin'):
    img = bpy.data.images.new(name, width=128, height=128, alpha=True)
    img.use_fake_user = True
    pixels = []
    for y in range(128):
        for x in range(128):
            noise = ((x * 19 + y * 37 + (x*y)%23) % 13 - 6) / 255
            if kind == 'skin':
                light = .05 * math.sin(x / 16) + .04 * math.cos(y / 13)
                scar = abs(y - (x*.4+35)) < 1.3 and 20<x<58
                col = (.62+light+noise, .23+light*.4+noise, .16+noise, 1)
                if scar: col = (.38,.12,.09,1)
                if ((x-95)**2+(y-89)**2)<130: col=(.72+noise,.31+noise,.23+noise,1)
            elif kind == 'cloth':
                stitch = y%32 in (1,2) and x%10<4
                wood = x>64
                col = (.28+noise,.20+noise,.11+noise,1) if wood else (.38+noise,.32+noise,.17+noise,1)
                if wood and x%11<2: col=(.20,.12,.065,1)
                if stitch: col=(.68,.58,.34,1)
            else:
                # Two eye tiles (left/right halves); lower half is available for mouth/future expressions.
                xx=x%64; yy=y
                col=(.63+noise,.23+noise,.16+noise,1)
                eye=((xx-32)/26)**2+((yy-91)/20)**2<1
                if eye: col=(.96,.85,.53,1)
                if eye and (xx-34)**2+(yy-89)**2<50: col=(.055,.025,.02,1)
                brow=abs(yy-(110+(xx-32)*(.22 if x<64 else -.22)))<3 and 8<xx<57
                if brow: col=(.18,.045,.025,1)
                if expression=='alarm' and (xx-32)**2+(yy-91)**2<95: col=(.04,.02,.015,1)
                if expression=='hurt' and (abs(yy-91-(xx-32)*.7)<2 or abs(yy-91+(xx-32)*.7)<2) and 15<xx<50: col=(.07,.03,.02,1)
            pixels.extend(col)
    img.pixels = pixels
    img.filepath_raw = str(OUT / (name+'.png')); img.file_format='PNG'; img.save(); img.pack()
    return img

def material(name, image=None, color=(1,1,1,1)):
    m=bpy.data.materials.new(name); m.diffuse_color=color; m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF'); bs.inputs['Base Color'].default_value=color; bs.inputs['Roughness'].default_value=1
    if image:
        node=m.node_tree.nodes.new('ShaderNodeTexImage'); node.image=image; node.interpolation='Closest'
        m.node_tree.links.new(node.outputs['Color'],bs.inputs['Base Color'])
    return m

parts=[]
def finish(obj,name,bone,mat,uv_region=None):
    obj.name=name
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    obj.data.materials.append(mat)
    group=obj.vertex_groups.new(name=bone); group.add(list(range(len(obj.data.vertices))),1,'REPLACE')
    if uv_region:
        for uv in obj.data.uv_layers.active.data:
            uv.uv=(uv_region[0]+uv.uv.x*uv_region[2],uv_region[1]+uv.uv.y*uv_region[3])
    parts.append(obj)
    return obj

def ellipsoid(name,bone,pos,scale,mat,segments=8,rings=4):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments,ring_count=rings,location=pos)
    o=bpy.context.object; o.scale=scale
    return finish(o,name,bone,mat)

def box(name,bone,pos,scale,mat,rot=0,uv=None):
    bpy.ops.mesh.primitive_cube_add(size=1,location=pos)
    o=bpy.context.object;o.scale=scale;o.rotation_euler[1]=rot
    return finish(o,name,bone,mat,uv)

def spike(name,bone,a,b,r,mat):
    a,b=Vector(a),Vector(b);d=b-a
    bpy.ops.mesh.primitive_cone_add(vertices=5,radius1=r,radius2=0,depth=d.length,location=(a+b)*.5)
    o=bpy.context.object;o.rotation_euler=d.to_track_quat('Z','Y').to_euler()
    return finish(o,name,bone,mat)

def ear(side,mat):
    # Large swept ears, with a coloured inset; single-sided panels are rendered double-sided in game.
    verts=[(side*.15,0,.73),(side*.47,.025,.88),(side*.38,-.015,.66),(side*.18,-.075,.64)]
    me=bpy.data.meshes.new('ear');me.from_pydata(verts,[],[(0,1,2),(0,2,3)]);me.uv_layers.new()
    for uv,coord in zip(me.uv_layers.active.data,[(.1,.1),(.9,.8),(.8,.2),(.1,.1),(.8,.2),(.3,.1)]):uv.uv=coord
    o=bpy.data.objects.new('Ear',me);bpy.context.collection.objects.link(o);bpy.context.view_layer.objects.active=o;o.select_set(True)
    finish(o,'Ear', 'head',mat)

def eye(side,mat):
    # Eye tiles sit just in front of the upper face, above the protruding pig snout.
    x=side*.068
    verts=[(x-.049,-.169,.752),(x+.049,-.169,.752),(x+.049,-.169,.842),(x-.049,-.169,.842)]
    me=bpy.data.meshes.new('eye');me.from_pydata(verts,[],[(0,1,2,3)]);uv=me.uv_layers.new()
    left=0 if side<0 else .5
    for u,p in zip(uv.data,[(left,.54),(left+.5,.54),(left+.5,.98),(left,.98)]):u.uv=p
    o=bpy.data.objects.new('Eye',me);bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active=o;o.select_set(True);finish(o,'Eye','head',mat)

BONES=[('root',(0,0,0),(0,0,.2),None),('hips',(0,0,.30),(0,0,.44),'root'),
 ('chest',(0,0,.44),(0,0,.65),'hips'),('head',(0,0,.65),(0,0,.86),'chest'),
 ('arm_l',(-.15,0,.60),(-.28,0,.44),'chest'),('hand_l',(-.28,0,.44),(-.31,-.02,.29),'arm_l'),
 ('arm_r',(.15,0,.60),(.28,0,.44),'chest'),('hand_r',(.28,0,.44),(.31,-.02,.29),'arm_r'),
 ('leg_l',(-.09,0,.31),(-.13,0,.17),'hips'),('foot_l',(-.13,0,.17),(-.14,-.04,.05),'leg_l'),
 ('leg_r',(.09,0,.31),(.13,0,.17),'hips'),('foot_r',(.13,0,.17),(.14,-.04,.05),'leg_r'),
 ('club',(.31,-.02,.32),(.33,-.02,.71),'hand_r')]

def animate(rig,name,seconds,loop):
    act=bpy.data.actions.new(name);act['bokoblin_fps']=FPS;rig.animation_data.action=act
    n=frames_of(seconds,loop)
    for f in range(n):
        t=f/max(1,n-1); phase=t*math.tau
        for b in rig.pose.bones:b.rotation_mode='XYZ';b.rotation_euler=(0,0,0);b.location=(0,0,0)
        def rot(b,x=0,y=0,z=0):rig.pose.bones[b].rotation_euler=(x,y,z)
        def lift(z):rig.pose.bones['root'].location.z=z
        if name=='idle':
            rot('chest',.045*math.sin(phase));rot('head',0,0,.10*math.sin(phase));rot('arm_l',.08*math.sin(phase),0,-.12)
        elif name in ('walk','flee'):
            amp=.65 if name=='flee' else .45
            a=math.sin(phase)*amp;rot('leg_l',a);rot('leg_r',-a);rot('arm_l',-a);rot('arm_r',a*.65);rot('chest',.14 if name=='walk' else .35);lift(abs(math.sin(phase))*.022)
        elif name=='alert':
            pulse=math.sin(t*math.pi);rot('chest',-.2*pulse);rot('head',-.25*pulse);rot('arm_l',-1.35*pulse,0,-.4*pulse);rot('arm_r',-.3*pulse,0,.3*pulse);lift(.025*pulse)
        elif name=='swing':
            # Lift the club before the server's .5-second hit, then a broad downward sweep.
            a=-1.8*min(t/.55,1) if t<.55 else -1.8+2.5*min((t-.55)/.18,1)
            rot('arm_r',a,0,-.25);rot('hand_r',-.45*math.sin(t*math.pi));rot('chest',.20*math.sin(t*math.tau));rot('head',0,0,-.1)
        elif name=='jump':
            rot('arm_r',-1.8*math.sin(t*math.pi));rot('arm_l',-.9*math.sin(t*math.pi));rot('leg_l',-.55*math.sin(t*math.pi));rot('leg_r',-.6*math.sin(t*math.pi));rot('chest',.25*math.sin(t*math.pi))
        elif name=='throw':
            a=-2.3*min(t/.55,1) if t<.55 else -2.3+2.7*min((t-.55)/.18,1)
            rot('arm_l',a,0,-.2);rot('hand_l',-.35);rot('chest',-.15*math.sin(t*math.pi));rot('head',-.1)
        elif name=='recover':
            rot('chest',.95*(1-t));rot('head',-.3*(1-t));rot('arm_l',-.9*(1-t));rot('leg_l',-.5*(1-t));rot('leg_r',-.5*(1-t));lift(-.12*(1-t))
        elif name=='dance':
            rot('hips',0,.17*math.sin(phase*2),.2*math.sin(phase));rot('head',0,0,-.25*math.sin(phase));rot('arm_l',-.8,0,-.8+.3*math.sin(phase*2));rot('arm_r',-.7,0,.7+.3*math.sin(phase*2));rot('leg_l',.25*math.sin(phase));rot('leg_r',-.25*math.sin(phase));lift(.03*abs(math.sin(phase*2)))
        elif name=='hurt':
            pulse=math.sin(t*math.pi);rot('chest',-.5*pulse);rot('head',-.3*pulse);rot('arm_l',-.4*pulse,0,-.45*pulse)
        elif name=='dead':
            rot('root',min(t/.55,1)*-1.4,0,.25*math.sin(t*math.pi));lift(-.07*min(t/.55,1));rot('arm_l',-.8);rot('arm_r',-.5)
        for b in rig.pose.bones:
            b.keyframe_insert('rotation_euler',frame=f);b.keyframe_insert('location',frame=f)
    for fc in act.fcurves:
        for k in fc.keyframe_points:k.interpolation='LINEAR'
    track=rig.animation_data.nla_tracks.new();track.name=name;track.strips.new(name,0,act);track.mute=True

def main():
    OUT.mkdir(parents=True,exist_ok=True);bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    skin=material('BokoSkin',make_texture('bokoblin_skin','skin'))
    cloth=material('BokoCloth',make_texture('bokoblin_cloth','cloth'))
    faces=[make_texture('bokoblin_face_'+n,'face',n) for n in FACES];face=material('BokoFace',faces[0])
    # Bone-white features use a painted ivory patch on the skin atlas.
    image=bpy.data.images['bokoblin_skin'];pix=list(image.pixels)
    for yy in range(8):
        for xx in range(8):pix[(yy*128+xx)*4:(yy*128+xx)*4+4]=(.86,.79,.57,1)
    image.pixels=pix;image.save();image.pack()
    ivory=skin
    ellipsoid('Barrel chest','chest',(0,0,.49),(.20,.125,.19),skin)
    ellipsoid('Belly','hips',(0,-.01,.36),(.15,.12,.11),skin)
    box('Ragged loincloth','hips',(0,0,.30),(.29,.24,.17),cloth,uv=(0,0,.48,1))
    box('Belt','hips',(0,0,.39),(.32,.255,.04),cloth,uv=(.5,0,.45,1))
    ellipsoid('Head','head',(0,-.015,.755),(.18,.15,.16),skin,10,5)
    ellipsoid('Pig snout','head',(0,-.178,.723),(.11,.082,.07),skin)
    # Black nostrils are tiny geometry, mapped into the dark pupil tile.
    for side in (-1,1):
        nostril=ellipsoid('Nostril','head',(side*.043,-.253,.733),(.022,.008,.014),face,6,3)
        for uv in nostril.data.uv_layers.active.data:uv.uv=(.26,.70)
        ear(side,skin);eye(side,face)
        spike('Fang','head',(side*.084,-.183,.659),(side*.081,-.19,.714),.018,ivory)
        for uv in parts[-1].data.uv_layers.active.data:uv.uv=(.025,.025)
        suffix='l' if side<0 else 'r'
        ellipsoid('Upper arm','arm_'+suffix,(side*.23,0,.51),(.074,.075,.15),skin)
        ellipsoid('Forearm','hand_'+suffix,(side*.295,-.01,.37),(.061,.061,.12),skin)
        ellipsoid('Knuckles','hand_'+suffix,(side*.31,-.025,.29),(.072,.065,.055),skin)
        ellipsoid('Thigh','leg_'+suffix,(side*.105,0,.25),(.075,.08,.105),skin)
        ellipsoid('Shin','foot_'+suffix,(side*.135,0,.12),(.065,.064,.10),skin)
        ellipsoid('Wide foot','foot_'+suffix,(side*.14,-.06,.044),(.092,.12,.045),skin)
    spike('Crooked horn','head',(0,.0,.88),(.045,-.015,1.025),.05,ivory)
    for uv in parts[-1].data.uv_layers.active.data:uv.uv=(.025,.025)
    box('Club handle','club',(.32,-.018,.47),(.042,.045,.35),cloth,uv=(.5,0,.45,1))
    ellipsoid('Knobby club','club',(.335,-.018,.67),(.092,.079,.16),cloth,7,4)
    for uv in parts[-1].data.uv_layers.active.data:uv.uv=(.55+uv.uv.x*.4,uv.uv.y)
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:p.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();mesh=bpy.context.object;mesh.name='Bokoblin'
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')
    arm=bpy.data.armatures.new('BokoblinSkeleton');rig=bpy.data.objects.new('BokoblinRig',arm);bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active=rig;mesh.select_set(False);rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
    for name,head,tail,parent in BONES:
        b=arm.edit_bones.new(name);b.head=head;b.tail=tail
        if parent:b.parent=arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT');mod=mesh.modifiers.new('Bokoblin skin','ARMATURE');mod.object=rig;mesh.parent=rig
    rig.animation_data_create();bpy.context.scene.render.fps=FPS
    for name,seconds,loop,_ in CLIPS:animate(rig,name,seconds,loop)
    rig.animation_data.action=bpy.data.actions['idle'];bpy.context.scene.frame_set(0)
    bpy.context.scene['art_notes']='Original low-poly Bokoblin; 128x128 hand-painted atlases; 13 bones; 11 clips; club + pig snout + crooked horn.'
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'bokoblin.blend'))
    for track in rig.animation_data.nla_tracks:track.mute=False
    rig.animation_data.action=None
    bpy.ops.export_scene.gltf(filepath=str(OUT/'bokoblin.glb'),export_format='GLB',export_animations=True,export_animation_mode='NLA_TRACKS',export_yup=True)
    print('Bokoblin model built')
if __name__=='__main__':main()
