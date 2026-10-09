"""Author and export Morrow. Run with Blender: blender -b --python tools/bosses/build_morrow.py.
All geometry, colors, and animations are original procedural work. Z up, facing -Y.
"""
from pathlib import Path
from math import sin, cos, pi
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/bosses/morrow'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

C = {
    'ivory': '#E6D8B7', 'bronze': '#A5743D', 'patina': '#3E8E88',
    'indigo': '#252640', 'violet': '#705178', 'amber': '#FFBA57',
    'leather': '#493327', 'dark': '#1C1B28', 'trim': '#CFAD77',
}
M = {}
for name, code in C.items():
    rgb = tuple(int(code[i:i+2], 16)/255 for i in (1, 3, 5))
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*rgb, 1)
    mat.use_nodes = True
    bs = mat.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*rgb, 1)
    bs.inputs['Roughness'].default_value = .85
    M[name] = mat

# Rigid groups become light procedural animation channels in the game.
PARTS = ['body', 'leg_left', 'leg_right', 'arm_left', 'arm_right', 'head', 'spool', 'tabard', 'weapon']
PIVOTS = [(0,0,0),(.22,0,.86),(-.22,0,.86),(.37,0,1.72),(-.37,0,1.72),
          (0,0,1.91),(0,.30,1.58),(0,0,1.08),(-.53,-.13,1.31)]

def tag(obj, name, color, part):
    obj.name = name
    obj.data.materials.append(M[color])
    obj['game_part'] = part
    return obj

def box(name, color, part, xyz, size, rot=(0,0,0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=xyz)
    o = bpy.context.object
    o.dimensions = size
    o.rotation_euler = rot
    return tag(o, name, color, part)

def orb(name, color, part, xyz, size, segments=10, rings=6):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=xyz)
    o = bpy.context.object
    o.scale = size
    return tag(o, name, color, part)

def cylinder(name, color, part, xyz, radius, depth, vertices=12, rot=(0,0,0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=xyz)
    o = bpy.context.object
    o.rotation_euler = rot
    return tag(o, name, color, part)

def panel(name, color, part, points):
    me = bpy.data.meshes.new(name)
    me.from_pydata(points, [], [(0,1,2), (0,2,3), (2,1,0), (3,2,0)])
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return tag(o, name, color, part)

# Boots, legs, layered chest and belt.
for side, part in ((1,1),(-1,2)):
    x = side*.22
    box('boot_sole', 'dark', part, (x,-.06,.065), (.32,.46,.12))
    orb('boot_toe', 'patina', part, (x,-.20,.17), (.17,.20,.115))
    orb('boot_calf', 'leather', part, (x,0,.36), (.13,.15,.27))
    box('shin_guard', 'bronze', part, (x,-.145,.42), (.22,.07,.29))
    orb('knee', 'patina', part, (x,-.17,.68), (.16,.09,.14))
    orb('trouser', 'indigo', part, (x,0,.86), (.16,.17,.27))
orb('hips', 'indigo', 0, (0,0,1.04), (.35,.23,.19))
orb('torso', 'indigo', 0, (0,0,1.48), (.39,.25,.42))
box('chest_plate', 'bronze', 0, (0,-.245,1.58), (.61,.09,.38))
box('chest_ridge', 'trim', 0, (0,-.302,1.60), (.10,.025,.30))
box('belt', 'leather', 0, (0,-.21,1.14), (.73,.08,.14))
cylinder('belt_buckle', 'bronze', 0, (0,-.27,1.14), .10,.035,12,(pi/2,0,0))
box('cross_strap', 'leather', 0, (.10,-.30,1.48), (.11,.03,.53), (0,.34,0))

# Four stiff cloth points. The center gap is visible from front and rear.
for front in (-1,1):
    y = front*.23
    for side in (-1,1):
        a = .035 if side>0 else -.035
        b = .27 if side>0 else -.27
        panel('split_tabard', 'violet', 7, [(a,y,1.08),(b,y,1.08),(b*1.16,y,.68),(side*.12,y,.58)])
        panel('tabard_trim', 'trim', 7, [(side*.12,y-front*.007,.58),(b*1.16,y-front*.007,.68),
                                       (b*1.16,y-front*.007,.70),(side*.12,y-front*.007,.60)])

# Right shoulder is deliberately smaller than the left bell.
for side, part in ((-1,4),(1,3)):
    x = side*.44
    orb('upper_arm', 'indigo', part, (x,0,1.51), (.15,.17,.27))
    orb('forearm_guard', 'bronze', part, (side*.50,-.02,1.22), (.13,.15,.22))
    orb('glove', 'leather', part, (side*.53,-.10,1.06), (.11,.12,.13))
for i in range(3):
    box('right_armor_plate', 'patina' if i<2 else 'bronze', 4,
        (-.43,-.05,1.81-i*.10), (.30+i*.025,.29,.075), (0,-.12,0))

# Left bronze bell shell with an open dark cavity and independent clapper.
cx, cy = .49, -.025
N = 16
verts=[]
for z, radius in ((1.98,.13),(1.48,.24),(1.48,.19),(1.90,.10)):
    for i in range(N):
        a=2*pi*i/N
        verts.append((cx+radius*cos(a),cy+radius*sin(a),z))
faces=[]
for ring in range(3):
    for i in range(N):
        j=(i+1)%N
        # Small front V notch between two lower lip segments.
        if ring==1 and i in (10,11):
            continue
        a=ring*N+i; b=ring*N+j; c=(ring+1)*N+j; d=(ring+1)*N+i
        faces.extend([(a,b,c),(a,c,d)])
me=bpy.data.meshes.new('bell_shell');me.from_pydata(verts,[],faces);me.update()
o=bpy.data.objects.new('hollowbell_shell',me);bpy.context.collection.objects.link(o);tag(o,o.name,'bronze',3)
cylinder('bell_dark_inside', 'dark', 3, (cx,cy,1.53),.186,.02,16)
cylinder('bell_crown', 'patina',3,(cx,cy,1.98),.145,.06,16)
orb('bell_clapper','amber',3,(cx,cy,1.43),(.065,.065,.095),8,4)
for i in range(5):
    a=2*pi*i/5
    orb('bell_patina', 'patina', 3, (cx+.192*cos(a),cy+.192*sin(a),1.66),(.034,.025,.07),8,4)

# Rear clock spool; precisely three amber windows.
cylinder('clock_spool','bronze',6,(0,.34,1.60),.235,.13,16,(pi/2,0,0))
cylinder('clock_face','dark',6,(0,.415,1.60),.19,.018,16,(pi/2,0,0))
cylinder('clock_hub','trim',6,(0,.43,1.60),.055,.028,12,(pi/2,0,0))
for i in range(3):
    a=pi*.22+i*pi*.28
    box('clock_window','amber',6,(.135*cos(a),.432,1.60+.135*sin(a)),(.07,.025,.08),(0,-a,0))

# Padded cowl and long ivory mask: two eye slits, three vertical brow grooves, no mouth.
orb('hood','indigo',5,(0,0,1.99),(.20,.18,.25))
orb('mask','ivory',5,(0,-.168,2.035),(.119,.050,.205),12,8)
for x in (-.061,.061):
    box('eye_slit','dark',5,(x,-.219,2.075),(.079,.017,.018))
    box('eye_glow','amber',5,(x,-.231,2.075),(.061,.012,.009))
for x in (-.032,0,.032):
    box('brow_groove','dark',5,(x,-.215,2.177),(.011,.012,.045))

# Right hand Tollkeeper maul. Four head bars leave a real rectangular hole.
sx, sy = -.53, -.13
cylinder('maul_shaft','leather',8,(sx,sy,1.31),.045,1.38,10,(0,.18,0))
box('maul_lower_cap','bronze',8,(sx,sy,.65),(.14,.14,.15))
for x in (-.17,.17):
    box('maul_head_side','bronze',8,(sx+x,sy,1.99),(.10,.36,.38))
for z in (1.82,2.16):
    box('maul_head_bar','patina',8,(sx,sy,z),(.43,.36,.09))
box('maul_amber_insert','amber',8,(sx,sy-.192,2.00),(.09,.018,.30))

bpy.context.view_layer.update()
# Export rigid triangles grouped by part. The game animates each group around its listed pivot.
groups=[[] for _ in PARTS]
triangle_count=0
for obj in sorted((o for o in bpy.data.objects if o.type=='MESH' and 'game_part' in o), key=lambda o:(o['game_part'],o.name)):
    part=int(obj['game_part'])
    rgb=tuple(round(x*255) for x in obj.data.materials[0].diffuse_color[:3])
    me=obj.data
    me.calc_loop_triangles()
    for tri in me.loop_triangles:
        triangle_count+=1
        for vi in tri.vertices:
            v=obj.matrix_world @ me.vertices[vi].co
            g=(round(v.x*100),round(v.z*100),round(-v.y*100))
            p=PIVOTS[part]; pivot=(round(p[0]*100),round(p[2]*100),round(-p[1]*100))
            groups[part].append((*[g[i]-pivot[i] for i in range(3)],*rgb))

lines=['#pragma once','// Generated by tools/bosses/build_morrow.py from original Blender geometry.',
       'namespace royale::morrow_model {','struct Corner { short x,y,z; unsigned char r,g,b; };',
       'struct Part { unsigned first,count; short px,py,pz; };',
       'inline constexpr Part kParts[] = {']
first=0
for i,group in enumerate(groups):
    p=PIVOTS[i]; pivot=(round(p[0]*100),round(p[2]*100),round(-p[1]*100))
    lines.append(f'  {{{first},{len(group)},{pivot[0]},{pivot[1]},{pivot[2]}}}, // {PARTS[i]}')
    first+=len(group)
lines += ['};','inline constexpr Corner kCorners[] = {']
for group in groups:
    lines += ['  {'+','.join(map(str,v))+'},' for v in group]
lines += ['};',f'inline constexpr unsigned kTriangleCount = {triangle_count};','}']
(ROOT/'shared/morrow_model.h').write_text('\n'.join(lines)+'\n')

# Rigid control rig with nine portable animation clips. Mesh parents preserve their rest transforms.
controls=[]
for index,name in enumerate(PARTS):
    control=bpy.data.objects.new('rig_'+name,None);bpy.context.collection.objects.link(control)
    control.location=PIVOTS[index];controls.append(control)
    for obj in list(bpy.data.objects):
        if obj.type=='MESH' and obj.get('game_part')==index:
            world=obj.matrix_world.copy();obj.parent=control;obj.matrix_world=world
bpy.context.scene.render.fps=20
clips=[('idle_count',2.4),('walk',.9),('alert_bow',.8),('sweep',1.9),('toll_road',2),('third_toll',2.3),('recover',1.8),('hurt',.3),('defeated',2)]
for clip,duration in clips:
    frames=round(duration*20)
    for part,control in enumerate(controls):
        control.animation_data_create();control.animation_data.action=None
        for frame in range(frames+1):
            u=frame/max(1,frames);rx=0
            if clip=='walk':rx=sin(2*pi*u)*(.32 if part==1 else -.32 if part==2 else .12 if part in (3,4,8) else 0)
            elif clip=='idle_count' and part==5:rx=sin(2*pi*u)*.08
            elif clip=='alert_bow' and part in (0,5):rx=.18*sin(pi*u)
            elif clip=='sweep' and part in (4,8):rx=-1.1*sin(2*pi*u)
            elif clip=='toll_road' and part in (4,8):rx=-.9*sin(pi*u)
            elif clip=='third_toll' and part==3:rx=-.65*sin(pi*u)
            elif clip=='recover' and part in (0,5):rx=.17*sin(pi*u)
            elif clip=='hurt' and part==0:rx=-.12*sin(pi*u)
            elif clip=='defeated' and part in (0,5):rx=.6*u
            control.rotation_euler=(rx,0,0);control.keyframe_insert(data_path='rotation_euler',frame=frame)
        action=control.animation_data.action;action.name=clip+'_'+PARTS[part]
        track=control.animation_data.nla_tracks.new();track.name=clip
        strip=track.strips.new(clip,0,action);track.mute=True
        control.animation_data.action=None;control.rotation_euler=(0,0,0)
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'morrow.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT/'morrow.glb'),export_format='GLB',export_animations=True,export_animation_mode='NLA_TRACKS')

# Orthographic turnarounds from the same editable mesh.
try:
    world=bpy.context.scene.world
    world.color=(.82,.79,.73)
    cam=bpy.data.cameras.new('TurnaroundCamera')
    cob=bpy.data.objects.new('TurnaroundCamera',cam);bpy.context.collection.objects.link(cob)
    bpy.context.scene.camera=cob
    cam.type='ORTHO';cam.ortho_scale=2.8
    scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH'
    scene.display.shading.color_type='MATERIAL';scene.display.shading.light='STUDIO'
    scene.render.resolution_x=720;scene.render.resolution_y=840;scene.render.resolution_percentage=100
    for label,loc in [('front',(0,-5,1.15)),('back',(0,5,1.15)),('left',(5,0,1.15)),('right',(-5,0,1.15))]:
        cob.location=loc
        direction=Vector((0,0,1.15))-cob.location
        cob.rotation_euler=direction.to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(OUT/f'morrow-{label}.png')
        bpy.ops.render.render(write_still=True)
except Exception as exc:
    print('Preview render failed:',exc)

print(f'Morrow: {triangle_count} triangles, {sum(map(len,groups))} corners')
