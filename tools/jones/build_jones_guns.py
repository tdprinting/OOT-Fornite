"""Astra: four console-style NPC-only rifles from the user's reference.
Run after build_jones.py; exports shared/jones_guns.h and an armed Blender scene.
Geometry is artistic game content. Axes: forward length -> Blender -Z in carry pose.
"""
from pathlib import Path
from math import sin,cos,pi
import json,sys
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'assets/npcs/jones'
sys.path.insert(0,str(Path(__file__).resolve().parent))
import design_geometry as design
bpy.ops.wm.open_mainfile(filepath=str(OUT/'jones.blend'))
GRIP=Vector((-.372,-.065,.953));PIVOT=Vector(design.PIVOTS[13])
COLORS={'tan':'B6A47D','tan_dark':'8B7C5E','olive':'66725C','steel':'414748','edge':'737775','black':'292C2A','wood':'685741','red':'AE3430','glass':'75423F'}
materials={};all_guns=[];muzzles=[]
def linear(c):return c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4
for key,code in COLORS.items():
    rgb=tuple(int(code[i:i+2],16)/255 for i in (0,2,4));mat=bpy.data.materials.new('gun_'+key);mat.diffuse_color=(*[linear(x) for x in rgb],1);mat['game_rgb']=rgb
    materials[key]=mat
def xyz(w,l,h):return GRIP+Vector((w,-h,-l))
def poly(name,color,outline,width):
    n=len(outline);v=[xyz(w,l,h) for w in (-width/2,width/2) for l,h in outline]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    me=bpy.data.meshes.new(name);me.from_pydata(v,[],f);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(materials[color]);current.append(o);return o
def tube(name,color,l0,l1,h,r,n=8):
    v=[xyz(r*cos(2*pi*i/n),l,h+r*sin(2*pi*i/n)) for l in (l0,l1) for i in range(n)]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    me=bpy.data.meshes.new(name);me.from_pydata(v,[],f);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(materials[color]);current.append(o)
def rail(start,end,h):
    poly('sight_rail','steel',[(start,h),(end,h),(end,h+.015),(start,h+.015)],.043)
    for j in range(6):
        l=start+(end-start)*j/6;poly('rail_notch','edge',[(l,h+.014),(l+.014,h+.014),(l+.014,h+.022),(l,h+.022)],.050)
def mag(l,color='steel',curved=True):
    pts=[(l-.026,.005),(l+.026,.005),(l+.050,-.112),(l+.09,-.175),(l+.038,-.193),(l-.002,-.125)] if curved else [(l-.032,.018),(l+.034,.018),(l+.04,-.14),(l-.028,-.14)]
    poly('magazine',color,pts,.046)
def grip(l=0):poly('pistol_grip','black',[(l-.024,.027),(l+.025,.020),(l+.012,-.10),(l-.034,-.11),(l-.055,-.080)],.042)
def muzzle(length,h=.068,wide=.026):
    tube('muzzle_brake','steel',length-.045,length,h,wide);tube('dark_bore','black',length,length+.002,h,wide*.63);return xyz(0,length+.004,h)
names=['Tan Assault','Olive Burst','Compact Carbine','Scoped Rifle']
for g in range(4):
    current=[]
    if g==0:
        poly('tan_receiver','tan',[(-.18,.027),(.30,.027),(.34,.07),(.29,.13),(-.15,.13),(-.19,.095)],.064)
        poly('tan_stock','tan_dark',[(-.17,.035),(-.37,.011),(-.40,.043),(-.37,.131),(-.26,.108),(-.16,.105)],.059)
        poly('stock_pad','black',[(-.40,.013),(-.37,.010),(-.37,.135),(-.4,.135)],.063)
        grip();mag(.085);tube('assault_barrel','steel',.28,.53,.068,.016);muzzles.append(muzzle(.57));rail(-.17,.29,.131)
        poly('front_sight','steel',[(.39,.065),(.405,.065),(.405,.135),(.39,.135)],.018)
        for l in (.12,.18,.24):poly('handguard_vent','black',[(l,.060),(l+.027,.060),(l+.027,.075),(l,.075)],.066)
    elif g==1:
        poly('bullpup_shell','olive',[(-.29,-.01),(.10,-.01),(.17,.048),(.10,.115),(-.26,.115),(-.32,.07)],.069)
        poly('bullpup_stock_pad','black',[(-.325,-.012),(-.30,-.012),(-.30,.113),(-.325,.113)],.073)
        mag(-.19,curved=False);grip(.005)
        tube('burst_barrel','steel',.09,.56,.065,.017);muzzles.append(muzzle(.60,.065,.024))
        poly('integrated_handle','black',[(-.15,.113),(-.1,.185),(.055,.185),(.11,.115),(.07,.115),(.038,.155),(-.078,.155),(-.107,.113)],.034)
        tube('burst_sight','steel',-.095,.067,.193,.025)
        poly('front_grip','olive',[(.20,.065),(.225,.056),(.22,-.12),(.18,-.12),(.18,.030)],.037)
        poly('trigger_guard','olive',[(-.065,.01),(-.046,-.119),(.028,-.12),(.049,-.05),(.035,-.049),(.015,-.099),(-.028,-.095),(-.043,.01)],.022)
    elif g==2:
        poly('carbine_receiver','steel',[(-.13,.010),(.15,.010),(.19,.095),(-.105,.105),(-.15,.073)],.061)
        poly('olive_handguard','olive',[(.12,.025),(.295,.032),(.32,.09),(.29,.128),(.13,.12)],.065)
        poly('wire_stock','black',[(-.14,.079),(-.32,.083),(-.34,-.045),(-.31,-.055),(-.28,.044),(-.13,.046)],.028)
        grip();mag(.078);tube('short_barrel','steel',.285,.34,.07,.020);tube('barrel_suppressor','steel',.33,.43,.07,.031);muzzles.append(muzzle(.45,.07,.033))
        rail(-.09,.125,.11);tube('compact_rear_sight','black',-.095,-.07,.147,.014)
        poly('mag_red_band','red',[(.098,-.165),(.17,-.149),(.176,-.166),(.106,-.19)],.049)
    else:
        poly('scoped_receiver','steel',[(-.18,.015),(.24,.015),(.29,.08),(.24,.123),(-.17,.123),(-.21,.071)],.063)
        poly('skeleton_stock','black',[(-.17,.023),(-.33,-.033),(-.39,-.016),(-.40,.098),(-.37,.11),(-.355,.019),(-.19,.068)],.038)
        grip();mag(.055);tube('long_barrel','steel',.23,.65,.074,.015);muzzles.append(muzzle(.69,.074,.026));rail(-.17,.22,.125)
        for l in (-.105,.028):poly('scope_mount','black',[(l,.137),(l+.022,.137),(l+.022,.193),(l,.193)],.026)
        tube('scope_body','steel',-.19,.12,.207,.026);tube('scope_objective','black',.09,.148,.207,.040);tube('scope_lens','glass',.149,.151,.207,.031)
        tube('scope_eyepiece','black',-.23,-.18,.207,.032)
        for l in (.03,.095,.16):poly('receiver_vent','black',[(l,.052),(l+.035,.052),(l+.035,.066),(l,.066)],.065)
    for o in current:o['jones_gun']=g
    all_guns.append(current)

# Export positions relative to the right elbow, sub-centimeter precision.
bpy.context.view_layer.update();arrays=[];ranges=[];cursor=0
for g,objects in enumerate(all_guns):
    corners=[]
    for o in objects:
        rgb=o.data.materials[0]['game_rgb'];o.data.calc_loop_triangles()
        for tri in o.data.loop_triangles:
            shade=.82+.18*max(0,tri.normal.dot(Vector((.35,-.45,.82)).normalized()))
            for vi in tri.vertices:
                v=o.matrix_world@o.data.vertices[vi].co-PIVOT
                corners.append((round(v.x*1600),round(v.z*1600),round(-v.y*1600),*[round(c*shade*255) for c in rgb]))
    m=muzzles[g]-PIVOT;ranges.append((cursor,len(corners),round(m.x*100),round(m.z*100),round(-m.y*100)));cursor+=len(corners);arrays.extend(corners)
lines=['#pragma once','// Generated by tools/jones/build_jones_guns.py. NPC-only visual assets.','namespace royale::jones_guns {','struct Corner { short x,y,z; unsigned char r,g,b; };','struct Gun { unsigned first,count; short muzzleX,muzzleY,muzzleZ; };','inline constexpr float kPositionScale=1.0f/16.0f;','inline constexpr unsigned kAttachPart=13;','inline constexpr Gun kGuns[] = {']
lines+=[' {'+','.join(map(str,r))+'},' for r in ranges];lines+=['};','inline constexpr Corner kCorners[] = {'];lines+=[' {'+','.join(map(str,c))+'},' for c in arrays];lines+=['};','}']
(ROOT/'shared/jones_guns.h').write_text('\n'.join(lines)+'\n')

control=bpy.data.objects['rig_right_forearm']
for objects in all_guns:
    for o in objects:
        world=o.matrix_world.copy();o.parent=control;o.matrix_world=world
for g,objects in enumerate(all_guns):
    for o in objects:o.hide_render=g!=0;o.hide_set(g!=0)
scene=bpy.context.scene;cam=scene.camera
cam.data.ortho_scale=2.15;cam.location=(3,-5,1.5);cam.rotation_euler=(Vector((0,0,.97))-cam.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'jones-armed.blend'))
scene.render.filepath=str(OUT/'jones-armed.png');bpy.ops.render.render(write_still=True)
# A readable rifle-ready pose with real upperarm/forearm articulation.
pose={4:(-.52,-.12,-.18),13:(-1.05,0,0),3:(-.85,.30,.33),12:(-.72,.60,0)}
for p,rot in pose.items():bpy.data.objects['rig_'+design.PARTS[p]].rotation_euler=rot
bpy.context.view_layer.update();scene.render.filepath=str(OUT/'jones-rifle-aim.png');bpy.ops.render.render(write_still=True)
for p in pose:bpy.data.objects['rig_'+design.PARTS[p]].rotation_euler=(0,0,0)
# Export standalone gun library in a separate file with all four visible.
for o in bpy.data.objects:o.select_set('jones_gun' in o)
for objects in all_guns:
    for o in objects:o.hide_set(False)
bpy.ops.export_scene.gltf(filepath=str(OUT/'jones-guns.glb'),export_format='GLB',use_selection=True,export_animations=False)
for o in list(bpy.data.objects):
    if o.type=='MESH' and 'jones_gun' not in o:o.hide_render=True
# Lay out four guns using display-only object transform, preserving authored rig.
for g,objects in enumerate(all_guns):
    for o in objects:
        world=o.matrix_world.copy();o.parent=None;o.matrix_world=world;o.hide_render=False;o.location.x+=(g-1.5)*.35+.372
cam.location=(3,-5,2.4);cam.rotation_euler=(Vector((0,0,.94))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=1.65
scene.render.resolution_x=1200;scene.render.resolution_y=850
scene.render.filepath=str(OUT/'jones-guns.png');bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'jones-guns.blend'))
(OUT/'guns-export-report.json').write_text(json.dumps({'names':names,'triangles':[r[1]//3 for r in ranges],'attach_part':13,'position_scale':1/16,'private_npc_assets':True},indent=2))
print('Jones guns exported:',[r[1]//3 for r in ranges])
