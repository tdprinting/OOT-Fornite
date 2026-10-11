"""Astra's authored Jones mesh. Cross-section modeling, in metres, facing -Y.

The concept turnaround in assets/npcs/jones is the silhouette reference.
Meshes stay faceted, but contours follow anatomy and clothing rather than boxes.
"""
from math import sin, cos, pi
import bpy
from mathutils import Vector

PARTS=['torso','left_leg','right_leg','left_arm','right_arm','head','left_tail','right_tail','scarf','jaw','left_brow','right_brow','left_forearm','right_forearm']
PIVOTS=[(0,0,0),(.135,0,.9),(-.135,0,.9),(.255,0,1.48),(-.255,0,1.48),(0,0,1.575),(.14,0,1.0),(-.14,0,1.0),(.052,-.153,1.47),(0,.015,1.69),(.059,-.107,1.78),(-.059,-.107,1.78),(.315,.005,1.245),(-.315,.005,1.245)]
PARENTS=[-1,-1,-1,-1,-1,-1,-1,-1,-1,5,5,5,3,4]
PALETTE={'coat':'C8BC92','coat_shade':'B1A47F','coat_inner':'A19372','olive':'535D46',
 'scarf':'B72F32','scarf_shade':'8C272C','shirt':'3D3A35','pants':'3C7778','pants_shade':'356465',
 'leather':'493D33','sole':'332E2A','boot_guard':'888775','gold':'B49B52',
 'skin':'D2A27B','skin_shadow':'B78966','lip':'AA7562','hair':'D2B245','hair_light':'DEC15A',
 'hair_shadow':'A18939','frame':'4B3026','lens':'322928','lens_glint':'54433A','mouth':'4B2D29','teeth':'DDCCAE'}
M={}

def linear(c): return c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4

def mesh(name,color,part,verts,faces):
    me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update()
    ob=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(ob)
    ob.data.materials.append(M[color]);ob['game_part']=part
    for p in me.polygons:p.use_smooth=(color=='skin')
    return ob

def section(name,color,part,rings,n=12,shape=None):
    # Rings: x centre, y centre, z, half width, half depth.
    sh=shape or [(cos(2*pi*i/n),sin(2*pi*i/n)) for i in range(n)]
    n=len(sh);v=[(x+rx*a,y+ry*b,z) for x,y,z,rx,ry in rings for a,b in sh]
    f=[tuple(reversed(range(n))),tuple(range((len(rings)-1)*n,len(rings)*n))]
    for j in range(len(rings)-1):
        for i in range(n):f.append((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i))
    return mesh(name,color,part,v,f)

def ribbon(name,color,part,rows,thick=.003):
    # Each row has left/right xyz. Subtle thickness avoids razor-sharp cloth edges.
    v=[p for r in rows for p in r];f=[]
    for j in range(len(rows)-1):f.append((2*j,2*j+1,2*j+3,2*j+2))
    ob=mesh(name,color,part,v,f)
    if thick:
        bpy.context.view_layer.objects.active=ob
        mod=ob.modifiers.new('cloth_edge','SOLIDIFY');mod.thickness=thick
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return ob

def oval(name,color,part,center,size,n=10,rings=5):
    x,y,z=center;rx,ry,rz=size
    return section(name,color,part,[(x,y,z+rz*cos(pi*j/rings),max(.001,rx*sin(pi*j/rings)),max(.001,ry*sin(pi*j/rings))) for j in reversed(range(rings+1))],n)

def bar(name,color,part,start,end,r1,r2,n=8):
    axis=(Vector(end)-Vector(start)).normalized();u=axis.cross(Vector((0,1,0))).normalized();w=axis.cross(u).normalized()
    v=[Vector(c)+(u*cos(2*pi*i/n)+w*sin(2*pi*i/n))*r for c,r in ((start,r1),(end,r2)) for i in range(n)]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,color,part,v,f)

def plaque(name,color,part,c,w,h,depth=.004,bevel=.16):
    x,y,z=c;a=w/2;b=h/2;q=min(a,b)*bevel
    xy=[(-a+q,-b),(a-q,-b),(a,-b+q),(a,b-q),(a-q,b),(-a+q,b),(-a,b-q),(-a,-b+q)]
    v=[(x+u,y+d,z+t) for d in (0,depth) for u,t in xy]
    f=[tuple(reversed(range(8))),tuple(range(8,16))]+[(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    return mesh(name,color,part,v,f)

def coat_surface(name,color,part,rings,start,end,steps):
    v=[]
    for z,rx,ry,cy in rings:
        for i in range(steps+1):
            t=start+(end-start)*i/steps
            v.append((rx*cos(t),cy+ry*sin(t),z))
    f=[];n=steps+1
    for r in range(len(rings)-1):
        for i in range(steps):f.append((r*n+i,r*n+i+1,(r+1)*n+i+1,(r+1)*n+i))
    ob=mesh(name,color,part,v,f)
    bpy.context.view_layer.objects.active=ob
    so=ob.modifiers.new('coat_thickness','SOLIDIFY');so.thickness=.008
    bpy.ops.object.modifier_apply(modifier=so.name)
    return ob

def build():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    bpy.context.preferences.filepaths.save_version=0
    for key,hexcode in PALETTE.items():
        srgb=tuple(int(hexcode[i:i+2],16)/255 for i in (0,2,4));rgb=tuple(linear(c) for c in srgb)
        mat=bpy.data.materials.new(key);mat.diffuse_color=(*rgb,1);mat['game_rgb']=srgb
        mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*rgb,1);bs.inputs['Roughness'].default_value=.9
        M[key]=mat

    # Fitted underlayers and a coherent open coat shell.
    section('charcoal_shirt','shirt',0,[(0,0,.88,.177,.098),(0,0,1.03,.173,.095),(0,0,1.24,.209,.116),(0,.007,1.43,.237,.116),(0,.008,1.48,.178,.09)],12)
    section('teal_hips','pants',0,[(0,0,.81,.165,.095),(0,0,.935,.173,.105)],12)
    jacket=[(1.00,.191,.119,0),(1.13,.195,.127,0),(1.32,.227,.138,0),(1.44,.251,.132,0),(1.49,.180,.106,.002)]
    coat_surface('fitted_coat','coat',0,jacket,-65*pi/180,245*pi/180,14)
    coat_surface('olive_shoulder_yoke_back','olive',0,[(1.345,.234,.143,.002),(1.455,.253,.139,.002),(1.50,.178,.109,.002)],-20*pi/180,200*pi/180,12)
    # Dark diagonal shirt strap is visible between the lapels and scarf.
    ribbon('chest_strap','leather',0,[[(-.12,-.133,1.42),(-.073,-.137,1.44)],[(.012,-.151,1.23),(.053,-.145,1.25)],[(.090,-.129,1.065),(.13,-.121,1.087)]])
    coat_surface('belt_wrap','leather',0,[(.939,.181,.116,0),(.989,.181,.116,0)],-pi/2,3*pi/2,14)
    plaque('belt_buckle','gold',0,(.005,-.126,.966),.052,.043,.007,.25)

    for side,leg,arm,tail in ((1,1,3,6),(-1,2,4,7)):
        x=side*.137
        # Long legs with a natural calf and tapered ankle, independent at the hip.
        section('trouser_'+str(side),'pants',leg,[(x,.006,.265,.062,.067),(x,.008,.35,.069,.070),(x,.004,.47,.084,.082),(x,-.012,.565,.078,.075),(x,.004,.70,.087,.086),(side*.123,.008,.88,.085,.096),(side*.116,0,.925,.081,.094)],10)
        # Boot geometry is one fitted upper/foot with low bevelled outsole.
        footshape=[(1,0),(.92,.65),(.54,1),(-.54,1),(-.92,.65),(-1,0),(-.89,-.77),(-.49,-1),(.49,-1),(.89,-.77)]
        section('boot_sole','sole',leg,[(x,-.040,.012,.091,.143),(x,-.040,.032,.103,.15),(x,-.040,.051,.100,.146)],shape=footshape)
        section('boot_upper','leather',leg,[(x,-.040,.045,.094,.14),(x,-.045,.098,.099,.142),(x,-.017,.16,.080,.092),(x,.002,.245,.070,.077),(x,.003,.288,.077,.079)],shape=footshape)
        section('boot_toe_cap','boot_guard',leg,[(x,-.101,.069,.085,.075),(x,-.092,.113,.089,.075),(x,-.068,.139,.069,.064)],10)
        section('boot_cuff','boot_guard',leg,[(x,.002,.238,.074,.080),(x,.002,.282,.080,.083),(x,.002,.291,.072,.076)],10)
        ribbon('boot_ankle_strap','gold',leg,[[(x-.072,-.085,.186),(x+.072,-.085,.186)],[(x-.069,-.079,.206),(x+.069,-.079,.206)]],.006)
        # Sleeve has shoulder slope, elbow definition, and a narrow wrist.
        forearm=12 if side==1 else 13
        section('upper_sleeve','coat',arm,[(side*.315,.005,1.232,.078,.074),(side*.286,.008,1.37,.090,.083),(side*.256,.004,1.461,.092,.089),(side*.239,.005,1.487,.063,.062)],10)
        section('fore_sleeve','coat',forearm,[(side*.358,-.004,1.018,.060,.057),(side*.347,-.002,1.10,.067,.067),(side*.315,.005,1.257,.077,.073)],10)
        section('curved_shoulder_yoke','olive',arm,[(side*.287,.005,1.351,.083,.082),(side*.271,.006,1.438,.096,.093),(side*.250,.005,1.482,.087,.082),(side*.230,.007,1.501,.056,.055)],10)
        section('glove_cuff','leather',forearm,[(side*.362,-.004,.995,.061,.058),(side*.352,-.003,1.056,.064,.064)],10)
        section('gloved_palm','leather',forearm,[(side*.372,-.010,.921,.051,.037),(side*.372,-.011,.959,.060,.043),(side*.362,-.004,1.004,.052,.045)],10)
        section('exposed_finger_group','skin',forearm,[(side*.369,-.017,.902,.034,.028),(side*.375,-.018,.922,.050,.035),(side*.372,-.015,.942,.051,.033)],10)
        bar('thumb','skin',forearm,(side*.329,-.01,.967),(side*.310,-.028,.928),.022,.016,8)
        # A pair of subtle knuckle creases distinguishes the grouped fingers.
        for offset in (-.014,.009):
            ribbon('finger_crease','skin_shadow',forearm,[[(side*.374+offset,-.049,.916),(side*.377+offset,-.049,.916)],[(side*.374+offset,-.050,.931),(side*.377+offset,-.050,.931)]],0)
        # Coat tails conform to hips and flare around the walking legs.
        a,b=(-65*pi/180,88*pi/180) if side==1 else (92*pi/180,245*pi/180)
        coat_surface('coat_skirt_'+str(side),'coat',tail,[(.653,.251,.172,.008),(.79,.243,.167,.004),(.91,.230,.151,.002),(1.01,.195,.120,0)],a,b,7)
        # Pockets follow the outer front quarter, same fabric with broad flap.
        xp=side*.167
        ribbon('coat_pocket','coat_shade',tail,[[(xp-side*.043,-.123,.739),(xp+side*.044,-.089,.756)],[(xp-side*.045,-.137,.904),(xp+side*.042,-.100,.905)]],.005)
        ribbon('pocket_flap','coat',tail,[[(xp-side*.047,-.141,.911),(xp+side*.047,-.106,.912)],[(xp-side*.046,-.143,.874),(xp+side*.046,-.108,.876)]],.006)
        # Folded collar/lapels: shaped with a raised central crease.
        mesh('angular_lapel','coat',0,[(side*.072,-.090,1.555),(side*.105,-.154,1.486),(side*.202,-.125,1.442),(side*.147,-.177,1.357),(side*.120,-.176,1.263),(side*.064,-.157,1.422),(side*.103,-.181,1.440)],[(0,1,6),(0,6,2),(2,6,3),(3,6,4),(4,6,5),(5,6,1)])
    # Knee pad uses a small sculpted shield shape, not a rectangular block.
    mesh('right_knee_guard','olive',2,[(-.137,-.098,.617),(-.073,-.087,.577),(-.087,-.093,.514),(-.145,-.105,.485),(-.203,-.083,.536),(-.202,-.081,.587),(-.138,-.125,.554)],[(i,(i+1)%6,6) for i in range(6)])

    # Short neck, human contour face with broad cheekbones and a defined chin.
    section('neck','skin',5,[(0,.007,1.475,.065,.061),(0,.007,1.615,.066,.066)],10)
    headshape=[(1,0),(.91,.63),(.50,.96),(-.50,.96),(-.91,.63),(-1,0),(-.87,-.66),(-.48,-.96),(0,-1),(.48,-.96),(.87,-.66)]
    face=section('upper_face','skin',5,[(0,.004,1.643,.108,.095),(0,0,1.700,.122,.098),(0,.004,1.749,.123,.102),(0,.009,1.798,.117,.103),(0,.014,1.828,.095,.089)],shape=headshape)
    for i,(a,b) in enumerate(headshape):
        if b<-.9:face.data.vertices[i].co.z=1.662
    jaw=section('jaw_chin','skin',9,[(0,-.006,1.577,.080,.066),(0,-.005,1.603,.098,.080),(0,-.005,1.642,.105,.092),(0,-.005,1.674,.103,.089)],shape=headshape)
    for i,(a,b) in enumerate(headshape):
        if b<-.9:jaw.data.vertices[3*len(headshape)+i].co.z=1.658
    # Mouth lies on the front facial plane with a small visible cavity when jaw opens.
    plaque('oral_cavity','mouth',5,(0,-.089,1.646),.063,.027,.002,.48)
    ribbon('upper_lip','lip',5,[[(-.038,-.091,1.657),(.038,-.091,1.657)],[(-.026,-.096,1.661),(.026,-.096,1.661)]],.001)
    plaque('upper_teeth','teeth',5,(0,-.094,1.651),.042,.007,.001,.1)
    ribbon('lower_lip','lip',9,[[(-.032,-.101,1.651),(.032,-.101,1.651)],[(-.028,-.103,1.657),(.028,-.103,1.657)]],.001)
    # Nose shares the face plane at its bridge; a small wedge tip produces a profile.
    mesh('nose','skin',5,[(-.014,-.099,1.737),(.014,-.099,1.737),(-.025,-.103,1.690),(.025,-.103,1.690),(0,-.145,1.700),(0,-.129,1.680)],[(0,1,4),(0,4,2),(1,3,4),(2,4,5),(4,3,5),(2,5,3)])
    for side in (-1,1):
        oval('ear','skin',5,(side*.122,.012,1.705),(.016,.021,.032),8,4)
    # Hair is a connected cap and swept broad locks. Low forehead fringe, no spikes.
    section('blond_hair_cap','hair_shadow',5,[(0,.012,1.771,.122,.099),(0,.013,1.815,.129,.110),(0,.017,1.852,.118,.105),(0,.012,1.871,.067,.066)],12)
    locks=[(-.108,.000,.091,1.852,1.886),(-.063,-.035,.096,1.861,1.895),(-.014,-.026,.095,1.861,1.900),(.047,.005,.078,1.848,1.883)]
    for idx,(x,y,w,z,peak) in enumerate(locks):
        mesh('swept_hair_lock','hair_light' if idx in (0,2) else 'hair',5,[(x-w*.50,y-.060,z-.021),(x+w*.49,y-.043,z-.017),(x+w*.67,y-.045,peak),(x+w*.14,y+.042,z+.023),(x-w*.39,y+.037,z+.004)],[(0,1,2),(0,2,3),(0,3,4),(1,3,2),(0,4,1),(1,4,3)])
    # Fringe sweeps across forehead and lies directly on the cap surface.
    for x,w,z in [(-.078,.073,1.801),(-.018,.080,1.805),(.055,.066,1.806)]:
        mesh('connected_fringe','hair',5,[(x-w*.5,-.082,1.846),(x+w*.5,-.091,1.844),(x+w*.39,-.104,z),(x-w*.40,-.103,z+.015),(x+.018,-.115,z+.028)],[(0,1,4),(1,2,4),(2,3,4),(3,0,4)])
    # Thin sunglasses fit the face; slight lens angle wraps the cheek plane.
    for side,part in ((1,10),(-1,11)):
        x=side*.057
        plaque('sunglasses_frame','frame',5,(x,-.109,1.745),.101,.043,.006,.45)
        plaque('sunglasses_lens','lens',5,(x,-.111,1.745),.090,.033,.001,.45)
        bar('glasses_temple','frame',5,(side*.105,-.106,1.758),(side*.125,.012,1.758),.006,.006,6)
        ribbon('brow','hair_shadow',part,[[(x-side*.046,-.103,1.782),(x+side*.037,-.101,1.780)],[(x-side*.044,-.104,1.790),(x+side*.038,-.102,1.787)]],.002)
    bar('glasses_bridge','frame',5,(-.011,-.116,1.751),(.011,-.116,1.751),.005,.005,6)

    # Scarf folds hug the neck, with a short tuck and long red hanging tail.
    section('scarf_underfold','scarf_shade',0,[(0,-.005,1.477,.106,.082),(0,-.005,1.520,.111,.094),(0,-.005,1.553,.080,.072)],12)
    scarfshape=[(cos(2*pi*i/12),sin(2*pi*i/12)) for i in range(12)]
    v=[]
    for z,rx,ry in [(1.514,.113,.097),(1.563,.090,.078)]:
        for a,b in scarfshape:v.append((a*rx,b*ry-.014,z+.017*a))
    mesh('diagonal_scarf_fold','scarf',0,v,[(i,(i+1)%12,(i+1)%12+12,i+12) for i in range(12)])
    ribbon('long_scarf_end','scarf',8,[[(.026,-.158,1.510),(.104,-.148,1.496)],[(.037,-.166,1.343),(.116,-.153,1.337)],[(.027,-.159,1.110),(.105,-.150,1.105)],[(.016,-.179,.786),(.100,-.169,.790)]],.008)
    ribbon('short_scarf_end','scarf_shade',8,[[(-.067,-.151,1.491),(-.009,-.160,1.481)],[(-.083,-.155,1.206),(-.031,-.165,1.197)],[(-.092,-.157,.887),(-.039,-.168,.881)]],.006)
    # A few broad scarf fold strips preserve cloth direction at low resolution.
    ribbon('scarf_long_fold','scarf_shade',8,[[(.030,-.169,1.330),(.041,-.172,1.330)],[(.011,-.186,.793),(.026,-.186,.792)]],0)
    for sx in (.02,.047,.075):
        ribbon('scarf_fringe','scarf',8,[[(sx,-.180,.797),(sx+.018,-.179,.797)],[(sx+.002,-.181,.773),(sx+.019,-.180,.778)]],.002)
    bpy.context.view_layer.update()
    return PARTS, PIVOTS
