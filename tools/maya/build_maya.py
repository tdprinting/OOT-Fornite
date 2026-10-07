"""Rebuild Maya's original low-poly mesh, pixel textures, rig and hobby animations.
Run: blender -b --python tools/maya/build_maya.py
Then: blender -b --python tools/maya/export_maya.py
Stylized from family references; no photograph is embedded in the game.
"""
import bpy, math, os
from mathutils import Vector
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'../..'))
OUT=os.path.join(ROOT,'assets/maya')
FPS=20
CLOTH_W=CLOTH_H=32
SKIN_W=64; SKIN_H=32
FACE_W=FACE_H=32
FACES=('smile','half','shut','giggle','oh')
CLIPS=[(n,s,True,None) for n,s in [('idle',3),('walk',1),('run',.7),('wave',2),('tablet',4),('draw',4),('pizza',3),('scooter',2),('learn',3),('cheer',2)]]
def frames_of(seconds,loops): return max(2,round(seconds*FPS))
def image(name,w,h,fn):
    im=bpy.data.images.new('maya_'+name,width=w,height=h)
    pix=[]
    for y in range(h):
        for x in range(w): pix.extend((*fn(x,h-1-y),1))
    im.pixels=pix; im.filepath_raw=os.path.join(OUT,'maya_'+name+'.png'); im.file_format='PNG'; im.save(); im.pack(); im.use_fake_user=True
    return im
def material(name,im):
    m=bpy.data.materials.new('Maya'+name); m.use_nodes=True
    n=m.node_tree.nodes.new('ShaderNodeTexImage'); n.image=im; n.interpolation='Closest'
    shader=m.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=.82
    m.node_tree.links.new(n.outputs['Color'],shader.inputs['Base Color'])
    return m
skin=(.73,.48,.32); hair=(.22,.12,.07)
def face(x,y,kind):
    c=skin
    if 7<=y<=8 and (5<=x<=11 or 21<=x<=27):c=hair
    for cx in (8,24):
        dx=(x-cx)/4;dy=(y-14)/2.5
        if kind in ('shut','giggle'):
            if abs(y-(14+round(1.2*(1-dx*dx))))<.6 and abs(dx)<=1:c=(.20,.11,.07)
        elif dx*dx+dy*dy<=1:
            c=(.92,.88,.77)
            if abs(x-cx)<2.5:c=(.20,.12,.065)
            if abs(x-cx)<1.3:c=(.08,.05,.025)
            if x==cx-1 and y==13:c=(1,1,.94)
    if y in (20,21) and x in (5,6,25,26):c=(.82,.44,.34)
    if kind=='oh' and ((x-16)/3)**2+((y-26)/2.5)**2<1:c=(.36,.10,.07)
    elif kind!='oh' and 9<=x<=23:
        smile=24+round(2*(1-((x-16)/7)**2))
        if y==smile:c=(.97,.92,.82)
        if y==smile+1:c=(.49,.22,.15)
    return c

parts=[]
def finish(o,bone,mat,region=None):
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    # Bake world coordinates so one joined mesh shares the armature's origin.
    mw=o.matrix_world.copy()
    for v in o.data.vertices: v.co=mw@v.co
    o.location=(0,0,0); o.rotation_euler=(0,0,0)
    o.data.materials.append(mat)
    for poly in o.data.polygons: poly.use_smooth = ('hair' not in o.name and 'fringe' not in o.name and 'ponytail' not in o.name and 'pizza' not in o.name)
    o.vertex_groups.new(name=bone).add(list(range(len(o.data.vertices))),1,'REPLACE')
    if region:
        for uv in o.data.uv_layers.active.data:
            uv.uv.x=region[0]+uv.uv.x*region[2]; uv.uv.y=region[1]+uv.uv.y*region[3]
    parts.append(o); return o
def ball(name,pos,size,bone,mat,region=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=7,location=pos)
    o=bpy.context.object; o.name=name; o.scale=size; return finish(o,bone,mat,region)
def box(name,pos,size,bone,mat,region=None):
    bpy.ops.mesh.primitive_cube_add(size=1,location=pos)
    o=bpy.context.object;o.name=name;o.scale=size;return finish(o,bone,mat,region)
def cone(name,pos,r1,r2,depth,bone,mat,region=None):
    bpy.ops.mesh.primitive_cone_add(vertices=10,radius1=r1,radius2=r2,depth=depth,location=pos)
    o=bpy.context.object;o.name=name;return finish(o,bone,mat,region)
def build():
    os.makedirs(OUT,exist_ok=True); bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.render.fps=FPS
    cloth=image('cloth',32,32,lambda x,y: (.92,.92,.86) if not (11<x<21 and 9<y<20) else ((.22,.44,.75) if (x+y)%4<2 else (.88,.30,.18)))
    colors=[skin,hair,(.18,.27,.61),(.10,.11,.15),(.92,.65,.24),(.23,.60,.75),(.96,.94,.85),(.79,.20,.16)]
    atlas=image('skin',64,32,lambda x,y: tuple(max(0,min(1,c+(.035 if (x+y)%5==0 else 0))) for c in colors[x//8]))
    faces=[image('face_'+n,32,32,lambda x,y,n=n:face(x,y,n)) for n in FACES]
    cm=material('Cloth',cloth);sm=material('Skin',atlas);fm=material('Face',faces[0])
    region=lambda n:(n/8+.005,.02,.115,.96)
    s=region(0);h=region(1);blue=region(2);dark=region(3);gold=region(4);cyan=region(5);paper=region(6);red=region(7)
    # Child proportions: approximately six head lengths, slender limbs and real joints.
    # Organic ring meshes give a continuous silhouette without cubes or stacked balls.
    def body(name,rings,bone,mat,reg=None,segments=10):
        verts=[];faces=[]
        for z,rx,ry,cx,cy in rings:
            for j in range(segments):
                a=2*math.pi*j/segments;verts.append((cx+rx*math.sin(a),cy-ry*math.cos(a),z))
        for row in range(len(rings)-1):
            for j in range(segments):
                a=row*segments+j;b=row*segments+(j+1)%segments
                faces.append((a,b,b+segments,a+segments))
        faces.append(tuple(reversed(range(segments))))
        faces.append(tuple((len(rings)-1)*segments+j for j in range(segments)))
        me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);uv=me.uv_layers.new()
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%segments/segments,vi//segments/(len(rings)-1))
        o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);finish(o,bone,mat,reg)
    body('shirt',[(.755,.125,.09,0,0),(.83,.145,.10,0,0),(.975,.158,.10,0,0),(1.045,.15,.084,0,0),(1.09,.06,.052,0,0)],'torso',cm)
    for i in range(3):cone('skirt ruffle',(0,0,.62+i*.053),.19-i*.012,.15-i*.006,.09,'root',sm,blue)
    body('neck',[(1.06,.035,.032,0,0),(1.145,.037,.035,0,0)],'torso',sm,s,8)
    ball('head',(0,-.012,1.235),(.108,.095,.135),'head',sm,s)
    ball('hair cap',(0,.008,1.296),(.113,.098,.09),'head',sm,h)
    ball('ponytail',(0,.12,1.275),(.052,.054,.13),'head',sm,h)
    for side in (-1,1):
        ball('ear',(side*.106,0,1.235),(.016,.018,.028),'head',sm,s)
        ball('side fringe',(side*.10,-.065,1.245),(.021,.021,.085),'head',sm,h)
    verts=[];quads=[]
    for row in range(7):
        lat=-.80+row*.24
        for col in range(9):
            lon=-1.02+col*(2.04/8)
            verts.append((.110*math.sin(lon)*math.cos(lat),-.012-.097*math.cos(lon)*math.cos(lat),1.235+.137*math.sin(lat)))
    for row in range(6):
        for col in range(8):
            i=row*9+col;quads.append((i,i+1,i+10,i+9))
    me=bpy.data.meshes.new('face');me.from_pydata(verts,[],quads);uv=me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%9/8,vi//9/6)
    o=bpy.data.objects.new('face',me);bpy.context.collection.objects.link(o);finish(o,'head',fm)
    ball('nose',(0,-.106,1.22),(.013,.018,.019),'head',sm,s)
    for side,label in ((-1,'R'),(1,'L')):
        x=side*.078
        body('thigh',[(.35,.034,.038,x,0),(.47,.048,.047,x,0),(.66,.053,.048,x,0)],'leg'+label,sm,s,8)
        body('calf',[(.08,.026,.028,x,0),(.20,.036,.039,x,0),(.35,.034,.038,x,0)],'shin'+label,sm,s,8)
        ball('shoe',(x,-.024,.052),(.048,.080,.035),'shin'+label,sm,blue)
        ball('sleeve',(side*.177,0,1.023),(.057,.065,.065),'arm'+label,cm)
        x=side*.20
        body('upper arm',[(.88,.030,.031,x,0),(.97,.037,.035,x,0),(1.05,.041,.036,x,0)],'arm'+label,sm,s,8)
        body('forearm',[(.72,.024,.025,x,0),(.80,.030,.031,x,0),(.88,.030,.031,x,0)],'forearm'+label,sm,s,8)
        ball('hand',(x,-.007,.69),(.032,.029,.042),'forearm'+label,sm,s)
        ball('thumb',(x-side*.026,-.009,.704),(.012,.015,.023),'forearm'+label,sm,s)
    box('tablet',(0,-.235,.81),(.25,.018,.17),'tablet',sm,dark)
    box('pixel building screen',(0,-.247,.81),(.22,.004,.14),'tablet',sm,cyan)
    for i in range(3):box('screen block',(-.06+i*.06,-.25,.79+i*.025),(.05,.003,.04),'tablet',sm,blue)
    box('sketchbook',(0,-.23,.80),(.25,.025,.18),'draw',sm,paper)
    for i in range(3):box('drawing line',(-.06+i*.04,-.245,.80),(.013,.005,.10),'draw',sm,cyan)
    box('pencil',(.12,-.23,.86),(.015,.015,.15),'draw',sm,gold)
    bpy.ops.mesh.primitive_cone_add(vertices=3,radius1=.075,radius2=.075,depth=.015,location=(.13,-.285,.805))
    finish(bpy.context.object,'pizza',sm,gold)
    for i in range(3):ball('pepperoni',(.10+i*.025,-.285,.815),(.014,.014,.014),'pizza',sm,red)
    box('scooter deck',(0,0,.075),(.15,.48,.04),'scooter',sm,cyan)
    box('scooter stem',(0,-.21,.47),(.025,.025,.78),'scooter',sm,dark)
    box('handlebar',(0,-.21,.87),(.36,.03,.03),'scooter',sm,dark)
    for y in (-.21,.21):
        bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=.06,depth=.035,location=(0,y,.06),rotation=(0,math.pi/2,0))
        finish(bpy.context.object,'wheelF' if y<0 else 'wheelR',sm,dark)
    box('book',(0,-.23,.83),(.25,.03,.20),'learn',sm,blue)
    box('book pages',(0,-.25,.83),(.22,.005,.17),'learn',sm,paper)
    for o in parts:
        if o.vertex_groups[0].name in ('tablet','draw','pizza','learn'):
            for v in o.data.vertices:v.co.z+=.04
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join(); mesh=bpy.context.object;mesh.name='Maya'
    ad=bpy.data.armatures.new('MayaRig');rig=bpy.data.objects.new('MayaRig',ad);bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active=rig;mesh.select_set(False);rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
    anchors={'root':(0,0,0),'torso':(0,0,.76),'head':(0,0,1.12),'legL':(.078,0,.66),'legR':(-.078,0,.66),'shinL':(.078,0,.35),'shinR':(-.078,0,.35),'armL':(.20,0,1.05),'armR':(-.20,0,1.05),'forearmL':(.20,0,.88),'forearmR':(-.20,0,.88)}
    anchors.update({n:(0,0,0) for n in ('tablet','draw','pizza','scooter','learn')})
    anchors.update({'wheelF':(0,-.21,.06),'wheelR':(0,.21,.06)})
    for n,p in anchors.items():
        b=ad.edit_bones.new(n);b.head=p;b.tail=Vector(p)+Vector((0,0,.10))
        if n!='root':
            parent='scooter' if n.startswith('wheel') else 'leg'+n[-1] if n.startswith('shin') else 'arm'+n[-1] if n.startswith('forearm') else 'root'
            b.parent=ad.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT');mesh.parent=rig;mod=mesh.modifiers.new('Armature','ARMATURE');mod.object=rig
    rig.animation_data_create()
    for name,seconds,loops,_ in CLIPS:
        act=bpy.data.actions.new(name);act.use_fake_user=True;rig.animation_data.action=act
        for f in range(frames_of(seconds,loops)):
            t=f/FPS;phase=2*math.pi*t/seconds
            for pb in rig.pose.bones:pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0)
            for prop in ('tablet','draw','pizza','scooter','learn'):rig.pose.bones[prop].location.y=0 if prop==name else -3
            rig.pose.bones['head'].rotation_euler.y=.07*math.sin(phase)
            if name in ('walk','run'):
                amp=.42 if name=='walk' else .65
                for label,sign in (('L',1),('R',-1)):
                    rig.pose.bones['leg'+label].rotation_euler.x=sign*amp*math.sin(phase)
                    rig.pose.bones['arm'+label].rotation_euler.x=-sign*amp*.7*math.sin(phase)
                    rig.pose.bones['shin'+label].rotation_euler.x=-.5*max(0,sign*math.sin(phase))
                    rig.pose.bones['forearm'+label].rotation_euler.x=-.18-.1*abs(math.sin(phase))
                rig.pose.bones['root'].location.y=.018*abs(math.sin(phase))
            elif name in ('tablet','draw','pizza','learn','scooter'):
                for label in ('L','R'):
                    rig.pose.bones['arm'+label].rotation_euler.x=-.6+.04*math.sin(phase*2)
                    rig.pose.bones['forearm'+label].rotation_euler.x=-.65
                    rig.pose.bones['arm'+label].rotation_euler.z=-.22 if label=='L' else .22
                if name=='draw':
                    rig.pose.bones['draw'].location.x=.015*math.sin(phase*3)
                    rig.pose.bones['armL'].rotation_euler.x+=.12*math.sin(phase*3)
                if name=='pizza':
                    lift=.5-.5*math.cos(phase)
                    rig.pose.bones['armL'].rotation_euler.x=-.7-.3*lift
                    rig.pose.bones['forearmL'].rotation_euler.x=-.7-.4*lift
                    rig.pose.bones['pizza'].location.y=.09*lift
                if name=='scooter':
                    for label in ('L','R'):rig.pose.bones['leg'+label].location.y=.055
                    for wheel in ('wheelF','wheelR'):rig.pose.bones[wheel].rotation_euler.x=phase*2
            elif name in ('wave','cheer'):
                rig.pose.bones['forearmL'].rotation_euler.x=-.6
                rig.pose.bones['armL'].rotation_euler.z=2.4+.25*math.sin(phase*3)
                if name=='cheer':rig.pose.bones['armR'].rotation_euler.z=-2.4+.25*math.sin(phase*3)
            # Keep the held pizza physically attached to the animated left palm.
            if name=='pizza':
                bpy.context.view_layer.update()
                forearm=rig.pose.bones['forearmL']
                hand=forearm.matrix @ rig.data.bones['forearmL'].matrix_local.inverted() @ Vector((.20,-.007,.69))
                delta=hand+Vector((0,-.014,.012))-Vector((.13,-.285,.845))
                rig.pose.bones['pizza'].location=rig.data.bones['pizza'].matrix_local.to_3x3().inverted() @ delta
            for pb in rig.pose.bones:
                pb.keyframe_insert('rotation_euler',frame=f);pb.keyframe_insert('location',frame=f)
        act['maya_fps']=FPS
        track=rig.animation_data.nla_tracks.new();track.name=name;track.strips.new(name,0,act);track.mute=True
    rig.animation_data.action=bpy.data.actions['idle'];scene.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'maya.blend'),compress=True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,'maya.glb'),export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True)
    # Review image is deliberately separate from the exportable character scene.
    bpy.ops.object.camera_add(location=(2,-3,1.8));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.67))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=1.65;scene.camera=cam
    bpy.ops.object.light_add(type='AREA',location=(1,-3,4));bpy.context.object.data.energy=500;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=5
    scene.world=bpy.data.worlds.new('Preview');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.22,.30,1)
    scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=650;scene.render.resolution_y=800;scene.render.resolution_percentage=100
    scene.render.filepath=os.path.join(OUT,'maya-preview.png');bpy.ops.render.render(write_still=True)
    scene.render.resolution_x=400;scene.render.resolution_y=500
    for clip in ('tablet','draw','pizza','scooter','learn','wave'):
        rig.animation_data.action=bpy.data.actions[clip];scene.frame_set(12)
        scene.render.filepath=os.path.join(OUT,'maya-'+clip+'-preview.png');bpy.ops.render.render(write_still=True)
if __name__=='__main__':build()
