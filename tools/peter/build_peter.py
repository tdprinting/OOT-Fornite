"""Peter mesh, rigid control rig and baked animation export (same runtime format as Jones).
Run: EGL_PLATFORM=surfaceless blender -b --python tools/peter/build_peter.py
 (or any Python with the `bpy` 5.x module: python tools/peter/build_peter.py)
Geometry: design_geometry.py. Phone-call timing: dialogue.py (shared with build_peter_audio.py).
"""
from pathlib import Path
from math import sin, pi
import json
import sys
import bpy
from mathutils import Vector
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/npcs/peter'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(Path(__file__).resolve().parent))
import design_geometry
import dialogue as D
PARTS, PIVOTS = design_geometry.build()
PARENTS = design_geometry.PARENTS
PHONE_PART = PARTS.index('phone')
groups=[[] for _ in PARTS];triangles=0
for o in sorted((o for o in bpy.data.objects if o.type=='MESH'),key=lambda o:(o['game_part'],o.name)):
    part=int(o['game_part']);mat=o.data.materials[0];rgb=tuple(round(v*255) for v in mat['game_rgb'])
    o.data.calc_loop_triangles()
    for tri in o.data.loop_triangles:
        triangles+=1
        # Baked faceted key light plus a soft height falloff: the painted, flat-shaded N64 look.
        normal=(o.matrix_world.to_3x3().inverted().transposed() @ tri.normal).normalized()
        shade=.80+.20*max(0,normal.dot(Vector((.35,-.45,.82)).normalized()))
        if mat.name.startswith('skin'):shade=.92+.08*max(0,normal.dot(Vector((.3,-.6,.7)).normalized()))
        if mat.name in ('eye','screen','buckle'):shade=1.0
        for vi in tri.vertices:
            v=o.matrix_world @ o.data.vertices[vi].co;p=PIVOTS[part]
            ao=1.0 if part>=3 else .86+.14*min(1.0,v.z/1.1)
            groups[part].append((round((v.x-p[0])*1600),round((v.z-p[2])*1600),round(-(v.y-p[1])*1600),*[min(255,round(c*shade*ao)) for c in rgb]))

controls=[]
for i,name in enumerate(PARTS):
    c=bpy.data.objects.new('rig_'+name,None);bpy.context.collection.objects.link(c);c.location=PIVOTS[i];controls.append(c)
    bpy.context.view_layer.update()
    for o in list(bpy.data.objects):
        if o.type=='MESH' and o['game_part']==i:
            world=o.matrix_world.copy();o.parent=c;o.matrix_world=world
for index,parent in enumerate(PARENTS):
    if parent<0:continue
    world=controls[index].matrix_world.copy();controls[index].parent=controls[parent];controls[index].matrix_world=world

clips=[('idle',2.4,True),('invite',1.8,True),('walk',.9,True),('run',.65,True),('attack',.7,False),
       ('greet',1.0,False),('hurt',.5,False),('belly',.8,False),('chicken',.9,False),('kick',.8,False),
       ('cheer',1.2,False),('dance',1.6,True),('phone',D.DURATION,False)]
# Right arm holding the phone to his ear (solved once against the shoulder/elbow/hand pivots).
PHONE_UPPER=design_geometry.PHONE_UPPER;PHONE_FORE=design_geometry.PHONE_FORE
def smooth(a,b,t):
    u=min(1,max(0,(t-a)/(b-a)));return u*u*(3-2*u)
def phone_pose(part,t):
    rx=ry=rz=0
    hold=smooth(*D.RAISE,t)*(1-smooth(*D.LOWER,t))
    if part==4:rx,ry,rz=(v*hold for v in PHONE_UPPER)
    if part==10:rx,ry,rz=(v*hold for v in PHONE_FORE)
    # The ring: he startles, looks down at his pocket and pats it.
    if t<D.RAISE[1]:
        startle=smooth(0,.25,t)*(1-smooth(1.6,2.1,t))
        if part==5:rx=.28*startle;rz=-.15*startle
        if part in (7,8):ry=(.18 if part==7 else -.18)*startle
        if part==4 and t<D.RAISE[0]:rx=.15*sin(2*pi*3*t)*startle
    else:
        # On the call: head leans onto the phone, the free hand talks.
        if part==5:ry=-.13*hold;rx=.04*sin(2*pi*.4*t)
        if part==0:ry=.03*sin(2*pi*.3*t)
    for speaker,a,b,_ in D.LINES:
        if not a<=t<=b:continue
        env=smooth(a,a+.15,t)*(1-smooth(b-.15,b,t))
        if speaker==1:
            if part==6:rx=.22*abs(sin(2*pi*4.2*(t-a)))*env
            if part==3:rx=-.55*env;rz=.15*env
            if part==9:rx=(-1.0+.35*sin(2*pi*1.6*(t-a)))*env;ry=-.3*env
            if part==5:rx+=.06*sin(2*pi*1.2*(t-a))*env
            if (speaker,a)==(1,D.LINES[-1][1]) and t<a+1.1:
                # "Heh heh heh": belly-shaking laugh before the goodbye.
                laugh=abs(sin(2*pi*3.5*(t-a)))
                if part==0:rx=-.07-.04*laugh
                if part==5:rx+=-.2;
                if part==6:rx=.25*laugh
        else:
            # Listening to Joe: brows up, slow nods.
            if part in (7,8):ry=(.12 if part==7 else -.12)*env
            if part==5:rx+=.07*sin(2*pi*.9*(t-a))*env
    return rx,ry,rz
def pose(clip,part,u):
    rx=ry=rz=0
    if clip=='phone':return phone_pose(part,u*D.DURATION)
    if clip in ('walk','run'):
        s=sin(2*pi*u);amp=.42 if clip=='walk' else .7
        if part in (1,2):rx=s*amp*(1 if part==1 else -1)
        if part in (3,4):rx=s*amp*(-.7 if part==3 else .7)
        if part in (9,10):rx=-.25 if clip=='walk' else -.9
        if part==0:ry=sin(2*pi*u)*(.05 if clip=='walk' else .03);rx=0 if clip=='walk' else .12   # waddle, lean into a run
        if part==5:ry=-sin(2*pi*u)*.04
    elif clip=='idle':
        if part==0:rx=sin(2*pi*u)*.015
        if part==5:rz=sin(2*pi*u)*.09
        if part in (3,4):rz=(1 if part==3 else -1)*sin(2*pi*u)*.03
    elif clip=='invite':
        # Big friendly wave with the left hand.
        if part==3:ry=-2.3;rx=-.25
        if part==9:ry=sin(2*pi*u*2)*.45
        if part==5:ry=.08;rx=-.06
        if part==6:rx=.12*max(0,sin(2*pi*u))
    elif clip=='attack':
        # Right haymaker: wind back, twist, punch through.
        wind=smooth(0,.3,u)*(1-smooth(.3,.45,u));hit=smooth(.3,.48,u)*(1-smooth(.75,1,u))
        if part==4:rx=.6*wind-1.55*hit;rz=-.25*hit
        if part==10:rx=-1.3*wind-.15*hit
        if part==0:rz=.28*wind-.32*hit;rx=.08*hit
        if part==3:rx=-.4*hit
        if part==6:rx=.18*hit
        if part in (7,8):ry=(-.25 if part==7 else .25)*(wind+hit)
    elif clip=='greet':
        # "Heh heh heh": leans back, belly shakes, hands on the belly.
        laugh=abs(sin(3*pi*u))*sin(pi*u)
        if part==0:rx=-.08*sin(pi*u)-.05*laugh
        if part==5:rx=-.25*sin(pi*u)
        if part==6:rx=.28*laugh
        if part in (3,4):rx=-.25*sin(pi*u)
        if part in (9,10):rx=-1.1*sin(pi*u)
    elif clip=='belly':
        # Belly bump: lean back, then throw the gut forward with arms flung behind.
        back=smooth(0,.3,u)*(1-smooth(.3,.42,u));bump=smooth(.3,.45,u)*(1-smooth(.7,1,u))
        if part==0:rx=-.22*back+.38*bump
        if part in (3,4):rx=.75*bump-.2*back;rz=(.35 if part==3 else -.35)*bump
        if part in (9,10):rx=-.3*back
        if part==1:rx=-.35*bump
        if part==2:rx=.2*bump
        if part==5:rx=-.25*bump
        if part==6:rx=.2*(back+bump)
        if part in (7,8):ry=(.2 if part==7 else -.2)*bump
    elif clip=='chicken':
        # The giant-chicken brawl flurry: windmilling alternating jabs, head down, jaw set.
        env=smooth(0,.1,u)*(1-smooth(.85,1,u));a=sin(2*pi*3*u)
        if part==3:rx=(-.6-.9*max(0,a))*env
        if part==4:rx=(-.6-.9*max(0,-a))*env
        if part in (9,10):rx=-.6*env
        if part==0:rz=.18*a*env;rx=.1*env
        if part==5:rx=.12*env;rz=-.1*a*env
        if part==6:rx=.15*abs(a)*env
        if part in (7,8):ry=(-.28 if part==7 else .28)*env
        if part in (1,2):rx=.12*sin(2*pi*6*u+(0 if part==1 else pi))*env
    elif clip=='kick':
        # Roadhouse: a big wobbly front kick, arms out for balance.
        chamber=smooth(0,.25,u)*(1-smooth(.25,.4,u));kick=smooth(.25,.42,u)*(1-smooth(.7,1,u))
        if part==2:rx=-.6*chamber-1.35*kick
        if part==0:rx=-.25*kick
        if part==3:ry=-1.1*kick
        if part==4:ry=1.1*kick
        if part==5:rx=.15*kick
        if part==6:rx=.25*kick
        if part in (7,8):ry=(-.25 if part==7 else .25)*(chamber+kick)
    elif clip=='cheer':
        # "Freakin' sweet!": both fists pumped high, a little hop of the belly.
        env=smooth(0,.15,u)*(1-smooth(.85,1,u));pump=abs(sin(2*pi*2*u))
        if part==3:ry=-(2.2+.3*pump)*env
        if part==4:ry=(2.2+.3*pump)*env
        if part in (9,10):rx=-.5*pump*env
        if part==0:rx=-.1*env-.05*pump*env
        if part==5:rx=-.25*env
        if part==6:rx=.3*env
        if part in (7,8):ry=(.2 if part==7 else -.2)*env
    elif clip=='dance':
        # Chicken-flap dance: elbows flap, head pecks, knees pump.
        f=sin(2*pi*2*u);g=sin(2*pi*u)
        if part==3:ry=-(.5+.45*max(0,f))
        if part==4:ry=(.5+.45*max(0,f))
        if part in (9,10):rx=-1.7
        if part==5:rx=.18*sin(2*pi*4*u)
        if part==6:rx=.2*max(0,f)
        if part==0:ry=.08*g;rx=.05
        if part==1:rx=-.35*max(0,g)
        if part==2:rx=-.35*max(0,-g)
    elif clip=='hurt':
        # Grabs his knee: "Ssss... ahh!"
        env=sin(pi*u)
        if part==0:rx=.3*env
        if part==2:rx=-.7*env
        if part in (3,4):rx=-.7*env
        if part in (9,10):rx=-.3*env
        if part==5:rx=-.2*env
        if part==6:rx=.2*env
        if part in (7,8):ry=(.25 if part==7 else -.25)*env
    return rx,ry,rz

FPS=20;samples=[];bpy.context.scene.render.fps=FPS
for clip,duration,loop in clips:
    frames=round(duration*FPS);clip_samples=[]
    for f in range(frames+1):clip_samples.append([pose(clip,p,f/frames) for p in range(len(PARTS))])
    samples.append(clip_samples)
    for part,c in enumerate(controls):
        c.animation_data_create();c.animation_data.action=None
        for f in range(frames+1):
            c.rotation_euler=clip_samples[f][part];c.keyframe_insert(data_path='rotation_euler',frame=f)
        action=c.animation_data.action;action.name=clip+'_'+PARTS[part]
        track=c.animation_data.nla_tracks.new();track.name=clip;track.strips.new(clip,0,action);track.mute=True
        c.animation_data.action=None;c.rotation_euler=(0,0,0)

lines=['#pragma once','// Generated by tools/peter/build_peter.py. Regenerate; do not edit arrays.','namespace royale::peter_model {',
       'struct Corner { short x,y,z; unsigned char r,g,b; };','struct Part { unsigned first,count; short px,py,pz; };',
       'struct Rotation { float x,y,z; };','struct Clip { unsigned first,frames; float duration; bool loop; };',
       'enum { '+','.join('k'+n.capitalize() for n,_,_ in clips)+' };',f'inline constexpr unsigned kPartCount={len(PARTS)};',
       f'inline constexpr unsigned kPhonePart={PHONE_PART};',
       f'inline constexpr float kPhoneShowFrom={D.RAISE[0]-.05}f,kPhoneShowUntil={D.LOWER[1]}f;',
       'inline constexpr float kPositionScale=1.0f/16.0f;',
       'inline constexpr int kParents[] = {'+','.join(map(str,PARENTS))+'};',
       'inline constexpr Part kParts[] = {']
first=0
for p,g in zip(PIVOTS,groups):
    lines.append('  {'+','.join(map(str,(first,len(g),round(p[0]*100),round(p[2]*100),round(-p[1]*100))))+'},');first+=len(g)
lines+=['};','inline constexpr Corner kCorners[] = {']
for g in groups:lines+=['  {'+','.join(map(str,v))+'},' for v in g]
lines+=['};','inline constexpr Clip kClips[] = {'];first=0
for (_,duration,loop),data in zip(clips,samples):
    lines.append(f'  {{{first},{len(data)},{duration}f,{str(loop).lower()}}},');first+=len(data)*len(PARTS)
lines+=['};','inline constexpr Rotation kRotations[] = {']
for data in samples:
    for frame in data:
        # This axis conversion is a proper rotation: Rx -> Rx, Rz -> Ry, Ry -> -Rz.
        for rx,ry,rz in frame:lines.append('  {'+','.join(f'{v:.5f}f' for v in (rx,rz,-ry))+'},')
lines+=['};',f'inline constexpr unsigned kTriangleCount={triangles};',
        f'inline constexpr unsigned kPhoneTriangles={len(groups[PHONE_PART])//3};','}']
(ROOT/'shared/peter_model.h').write_text('\n'.join(lines)+'\n')
assert 1800 <= triangles <= 6000, triangles
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'peter.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT/'peter.glb'),export_format='GLB',export_animations=True,export_animation_mode='NLA_TRACKS')

# Preview renders (Workbench; headless needs EGL, e.g. EGL_PLATFORM=surfaceless with Mesa).
scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH';scene.display.shading.color_type='MATERIAL'
scene.display.shading.light='STUDIO';scene.display.shading.show_shadows=True
scene.display.shading.show_specular_highlight=False;scene.display.shading.studiolight_rotate_z=.4
scene.display.shading.show_cavity=True;scene.display.shading.cavity_type='WORLD';scene.display.shading.curvature_ridge_factor=1.2
scene.world.color=(.15,.14,.12)
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.render.resolution_x=800;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
camera=bpy.data.cameras.new('PreviewCamera');cam=bpy.data.objects.new('PreviewCamera',camera);bpy.context.collection.objects.link(cam)
scene.camera=cam;camera.type='ORTHO'
def shoot(label,at,look,scale,clip=None,u=0):
    for p,c in enumerate(controls):
        c.rotation_euler=pose(clip,p,u) if clip else (0,0,0)
        c.hide_render=False
    for o in bpy.data.objects:
        if o.type=='MESH' and o['game_part']==PHONE_PART:o.hide_render=clip!='phone'
    camera.ortho_scale=scale;cam.location=at;cam.rotation_euler=(Vector(look)-cam.location).to_track_quat('-Z','Y').to_euler()
    bpy.context.view_layer.update();scene.render.filepath=str(OUT/f'peter-{label}.png');bpy.ops.render.render(write_still=True)
for label,at in [('front',(0,-5,1.0)),('back',(0,5,1.0)),('side',(5,0,1.0)),('three-quarter',(3,-5,1.6))]:
    shoot(label,at,(0,0,1.0),2.3,'idle',0)
shoot('face-neutral',(1.2,-4,1.78),(0,0,1.72),.8,'idle',0)
shoot('face-laugh',(1.2,-4,1.78),(0,0,1.72),.8,'greet',.5)
shoot('phone-call',(-2.5,-4.5,1.6),(0,0,1.15),2.3,'phone',9.0/D.DURATION)
shoot('punch',(3.5,-4,1.4),(0,0,1.0),2.3,'attack',.45)
shoot('wave',(1.5,-5,1.4),(0,0,1.1),2.3,'invite',.25)
shoot('belly-bump',(3.5,-4,1.4),(0,0,1.0),2.3,'belly',.5)
shoot('chicken-fight',(2,-5,1.4),(0,0,1.0),2.3,'chicken',.42)
shoot('roadhouse-kick',(4.5,-2.5,1.3),(0,0,1.0),2.3,'kick',.5)
shoot('freakin-sweet',(1.5,-5,1.4),(0,0,1.1),2.3,'cheer',.4)
shoot('chicken-dance',(1.5,-5,1.4),(0,0,1.0),2.3,'dance',.125)
for c in controls:c.rotation_euler=(0,0,0)
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'peter.blend'))
bounds=[o.matrix_world@v.co for o in bpy.data.objects if o.type=='MESH' for v in o.data.vertices]
height=round((max(v.z for v in bounds)-min(v.z for v in bounds))*100,2)
(OUT/'export-report.json').write_text(json.dumps({'triangles':triangles,'parts':PARTS,'height_cm':height,
    'clips':[{'name':n,'seconds':d,'loop':l} for n,d,l in clips],'runtime':'shared/peter_model.h',
    'rig':'rigid control empties, same baked rotations in Blender and runtime'},indent=2)+'\n')
print(f'Peter: {triangles} triangles; {len(clips)} clips exported')
