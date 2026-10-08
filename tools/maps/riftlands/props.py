"""Astra's authored everyday-craft scenery families, built with shared tiles.

Each family has separate named geometry and honest static cover solids. Cloth,
ropes, tiny handles and light accents stay decorative. Expanded instances are
recorded in the placement manifest rather than promising unsupported mesh LOD.
"""
import math
import numpy as np
import kit,geom
# 56 is the original kit authoring scale; adult visual scaling is 1.35 in
# balance.h. Manifest reports physical dimensions, without promising full cover.
PLACEMENTS=[]

def box(w,x,z,y,sx,sy,sz,mat,solid=False):
    w.box(x,y,z,sx,sy,sz,mat,col='static' if solid else 'none',surf='wood' if mat in ('planks','timber','bark') else 'stone')

def wheel(w,x,z,y,r=25):
    # The opaque six-sector silhouette uses a painted rim/hub/spoke tile.
    for k in range(6):
        a,b=k*math.tau/6,(k+1)*math.tau/6
        w.face([(x,y,z),(x+math.cos(a)*r,y+math.sin(a)*r,z),(x+math.cos(b)*r,y+math.sin(b)*r,z)],'wagon_wheel',double=True,
               uvs=[(.5,.5),(.5+.48*math.cos(a),.5+.48*math.sin(a)),(.5+.48*math.cos(b),.5+.48*math.sin(b))])

def arched_slab(w,x,z,y,width,height,thick,mat):
    # Extruded round-headed slab: no closed gaps or hidden rectangular upper wall.
    r=width/2;spring=height-r
    outline=[(-r,0),(r,0),(r,spring)]+[(r*math.cos(k*math.pi/8),spring+r*math.sin(k*math.pi/8)) for k in range(1,9)]
    pts=[(x+a,y+b,z+c) for c in [-thick/2,thick/2] for a,b in outline];n=len(outline)
    faces=[tuple(range(n))[::-1],tuple(range(n,2*n))]+[(k,(k+1)%n,(k+1)%n+n,k+n) for k in range(n)]
    w.solid(pts,faces,mat,col='static',surf='stone')

def canopy(w,x,z,y,roof='thatch',width=180,depth=130):
    for sx in [-1,1]:box(w,x+sx*(width/2-8),z,y,12,245,12,'timber',True)
    w.gable(x,z,width,depth,y+245,55,roof,col=None)

def bowl(w,x,z,y,mat='castle_stone',r=75):
    kit.ring_wall(w,x,z,r,18,y,y+38,mat,n=8)
    w.cylinder(x,z,r-18,y+4,y+6,'water',n=8,col='none')

def crest(w,x,z,y,mat='banner_blue'):
    w.face([(x-24,y+100,z),(x+24,y+100,z),(x+20,y+30,z),(x,y+44,z),(x-20,y+30,z)],mat,double=True)

def sculpted_root(w,x,z,y):
    pts=[(x-90,y,z),(x-65,y+70,z),(x-35,y+150,z),(x+25,y+165,z),(x+85,y+70,z)]
    for a,b in zip(pts,pts[1:]):kit.beam(w,a,b,28,25,'bark',col='none')
    box(w,x-80,z,y,36,75,70,'bark',True)
    box(w,x,z+25,y,115,20,60,'planks',True)
    # Three broad overlapping leaves, with curved roots dominating the silhouette.
    for k in range(3):
        cx=x-55+k*55
        w.face([(cx-55,y+152,z-40),(cx,y+177,z-75),(cx+55,y+152,z-25),
                (cx+30,y+138,z+55),(cx-30,y+142,z+70)],'leaves',double=True)
        kit.beam(w,(cx-25,y+150,z),(cx+15,y+105,z+55),16,13,'bark',col='none')

def family(w,name,x,z):
    y=kit.H(x,z);old=w.group;w.group='rf_'+name
    before=len(w.tris);cv=len(w.col_verts)
    if name=='crown_cistern':
        bowl(w,x,z,y,r=90)
        arched_slab(w,x,z-80,y,130,135,24,'castle_stone')
        w.cylinder(x,z-60,16,y+80,y+92,'castle_stone',n=8,col='none')
        w.face([(x-14,y+88,z-45),(x,y+104,z-24),(x+14,y+88,z-45),(x,y+82,z-18)],'roof_green',double=True)
        kit.pot(w,x+110,z,y,23)
    elif name=='crown_standard':
        box(w,x,z,y,55,30,55,'castle_stone',True)
        w.cylinder(x,z,7,y+30,y+195,'timber',n=5,col='none')
        w.cone(x,z,12,y+190,y+215,'gold',n=5)
        crest(w,x+25,z,y+60)
    elif name=='crown_planter':
        box(w,x,z,y,160,40,52,'castle_stone',True)
        for k in range(3):
            w.sphere(x-50+k*50,y+55,z,28,25,23,'leaves_dark',rings=2,segs=6,seed=k)
    elif name=='quarry_sled':
        box(w,x,z,y+8,145,20,80,'planks')
        box(w,x,z,y+20,115,65,68,'brick_dark',True)
        for k in range(3):w.sphere(x-35+k*35,y+60,z,35,30+(k%2)*15,30,'brick_dark',rings=2,segs=5,seed=k,jitter=.25)
        for sx in [-1,1]:box(w,x+sx*55,z,y,9,14,140,'iron')
    elif name=='quarry_forge':
        w.cylinder(x,z,70,y,y+105,'brick_dark',n=8,col='static')
        box(w,x,z+70,y+30,50,45,4,'lava_glow')
        # Wedge-shaped leather bellows with broad ribs and a narrow nozzle.
        pts=[(x+55,y+15,z-35),(x+150,y+15,z-35),(x+150,y+15,z+35),(x+55,y+15,z+35),
             (x+55,y+25,z-35),(x+150,y+65,z-35),(x+150,y+65,z+35),(x+55,y+25,z+35)]
        w.solid(pts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'leather',col='static',surf='wood')
        for k in range(3):box(w,x+70+k*25,z,y+21+k*10,8,7,75,'timber')
        kit.beam(w,(x+45,y+20,z),(x+75,y+30,z),14,12,'iron',col='none')
        for sx in [-1,1]:box(w,x+sx*115,z-20,y,14,245+sx*15,16,'timber',True)
        # A sloping, repaired rust cloth fly rather than another tiled house roof.
        w.face([(x-135,y+230,z-85),(x+135,y+260,z-85),(x+130,y+245,z+100),
                (x,y+222,z+115),(x-130,y+220,z+100)],'rust_cloth',double=True)
        w.face([(x+35,y+248,z),(x+100,y+254,z),(x+90,y+240,z+55),(x+30,y+233,z+55)],'cloth',double=True)
    elif name=='quarry_gong':
        for sx in [-1,1]:box(w,x+sx*68,z,y,23,155,28,'brick_dark',True)
        box(w,x,z,y+155,165,18,22,'timber')
        for k in range(12):
            a,b=k*math.tau/12,(k+1)*math.tau/12
            w.face([(x,y+100,z+18),(x+math.cos(a)*48,y+100+math.sin(a)*48,z+18),(x+math.cos(b)*48,y+100+math.sin(b)*48,z+18)],'bronze',double=True)
    elif name=='kakariko_well':
        bowl(w,x,z,y,'town_stone',r=65)
        # A visible recessed safe base closes the well, with separate canopy posts.
        w.cylinder(x,z,45,y+1,y+3,'water',n=8,col='static')
        canopy(w,x,z,y,'roof_red',width=165,depth=140)
        box(w,x,z,y+195,145,12,12,'timber')
        w.cylinder(x,z,4,y+25,y+195,'timber',n=4,col='none')
        box(w,x,z+80,y+230,42,42,6,'clock')
    elif name=='kakariko_cart' or name=='ranch_wagon':
        wagon=name=='ranch_wagon';width=155 if wagon else 120;depth=90
        box(w,x,z,y+25,width,25,depth,'roof_red' if wagon else 'planks')
        box(w,x,z,y+50,width-15,55,depth-10,'hay' if wagon else 'woven',True)
        for xx in [-width*.35,width*.35]:
            for zz in [-depth*.6,depth*.6]:wheel(w,x+xx,z+zz,y+27,27)
        for sx in [-1,1]:box(w,x+sx*45,z+100,y+22,10,10,140,'timber')
        if wagon:
            for k in [-1,1]:w.sphere(x+k*30,y+102,z,44,25,40,'hay',rings=3,segs=8,seed=k+10)
        else:
            w.gable(x,z,width-10,depth-10,y+100,15,'banner_blue',col=None)
    elif name=='kakariko_notice':
        for sx in [-1,1]:box(w,x+sx*50,z,y,12,150,14,'timber',True)
        box(w,x,z,y+45,95,80,8,'planks')
        w.hip(x,z,125,50,y+150,22,'roof_red',col=None,overhang=8)
        for sx in [-1,0,1]:box(w,x+sx*25,z+5,y+70,19,33,2,'cloth')
    elif name=='deku_cradle':sculpted_root(w,x,z,y)
    elif name=='deku_granary':
        w.cylinder(x,z,55,y,y+105,'bark',n=8,col='static')
        w.cone(x,z,72,y+105,y+155,'thatch',n=8)
        box(w,x,z+55,y+42,28,30,2,'iron')
    elif name=='deku_lantern' or name=='lake_bollard':
        for k in range(3 if name=='lake_bollard' else 1):box(w,x+k*12,z,y,10,100+k*8,12,'bark',True)
        kit.beam(w,(x,y+110,z),(x+40,y+125,z),10,10,'timber',col='none')
        w.sphere(x+40,y+95,z,20,27,18,'window_lit',rings=2,segs=5,seed=7)
        w.cone(x+40,z,24,y+120,y+140,'roof_green',n=5)
    elif name=='lake_skiff':
        # Open crescent hull: pointed raised prow, curved sides, visible ribs and oar.
        plan=[(-130,0),(-100,-35),(-45,-48),(45,-48),(100,-35),(145,0),(100,35),(45,48),(-45,48),(-100,35)]
        tops=[95,80,72,72,87,118,87,72,72,80];n=len(plan)
        outer=[(x+a,y+h,z+b) for (a,b),h in zip(plan,tops)]
        bottom=[(x+a*.72,y+22,z+b*.7) for a,b in plan]
        inner=[(x+a*.78,y+h-6,z+b*.72) for (a,b),h in zip(plan,tops)]
        floor=[(x+a*.65,y+36,z+b*.6) for a,b in plan]
        for k in range(n):
            j=(k+1)%n
            w.face([outer[k],outer[j],bottom[j],bottom[k]],'planks')
            w.face([outer[k],inner[k],inner[j],outer[j]],'roof_green')
            w.face([inner[k],floor[k],floor[j],inner[j]],'timber')
        w.face(floor,'planks')
        box(w,x,z,y+18,150,18,52,'planks',True)
        for side in [-1,1]:box(w,x,z+side*35,y+35,135,42,12,'planks',True)
        for xx in [-50,50]:box(w,x+xx,z,y,24,25,75,'town_stone',True)
        for xx in [-55,0,55]:box(w,x+xx,z,y+60,10,7,67,'planks')
        kit.beam(w,(x-90,y+63,z-60),(x+80,y+65,z+60),8,6,'timber',col='none')
        box(w,x-100,z-70,y+62,25,5,40,'planks')
        w.sphere(x-55,y+47,z,23,10,28,'cloth',rings=2,segs=6,seed=18)
    elif name=='lake_netrack':
        for sx in [-1,1]:box(w,x+sx*70,z,y,12,150,14,'timber',True)
        box(w,x,z,y+150,165,12,14,'timber')
        w.face([(x-60,y+135,z),(x+60,y+135,z),(x+45,y+75,z+12),(x-50,y+80,z+10)],'woven',double=True)
        box(w,x,z+40,y+25,110,12,45,'planks',True)
        for sx in [-1,0,1]:w.sphere(x+sx*40,y+120,z,9,9,9,'timber',rings=2,segs=5,seed=sx+3)
    elif name=='ranch_milkstand' or name=='spirit_stall':
        spirit=name=='spirit_stall';canopy(w,x,z,y,'banner_blue' if spirit else 'cloth')
        box(w,x,z,y,130,40,65,'sandstone' if spirit else 'roof_green',True)
        for k in [-1,0,1]:
            w.cylinder(x+k*36,z,14,y+40,y+68,'pottery',n=6,col='none',r1=11)
    elif name=='ranch_trough':
        box(w,x,z,y,150,35,42,'timber',True)
        box(w,x,z,y+35,130,2,28,'water')
        for sx in [-1,1]:box(w,x+sx*85,z,y,16,120,20,'timber',True)
        box(w,x,z,y+120,190,12,18,'timber')
    elif name=='spirit_screen' or name=='temple_window':
        box(w,x,z,y,160,50,40,'sandstone' if name=='spirit_screen' else 'castle_stone',True)
        for k in range(7):
            a=k*math.pi/7;aa=(k+1)*math.pi/7
            w.face([(x+math.cos(a)*68,y+50+math.sin(a)*68,z),(x+math.cos(aa)*68,y+50+math.sin(aa)*68,z),
                    (x+math.cos(aa)*40,y+50+math.sin(aa)*40,z),(x+math.cos(a)*40,y+50+math.sin(a)*40,z)],'sandstone' if name=='spirit_screen' else 'castle_stone',double=True)
        box(w,x-65,z,y+45,30,70,40,'sandstone' if name=='spirit_screen' else 'castle_stone',True)
    elif name=='spirit_urn':
        box(w,x,z,y,75,12,75,'sandstone',True)
        w.cylinder(x,z,38,y+12,y+80,'pottery',n=8,col='static',r1=27)
        w.cylinder(x,z,29,y+80,y+90,'pottery',n=8,col='none')
        w.cylinder(x,z,39,y+45,y+53,'roof_green',n=8,col='none',top=False)
    elif name=='frost_woodcrib':
        box(w,x,z,y,140,85,75,'timber',True)
        for k in range(6):
            w.cylinder(x-42+(k%3)*42,z+40,17,y+20+(k//3)*35,y+30+(k//3)*35,'planks',n=6,col='none')
        w.gable(x,z,180,110,y+85,55,'snow',col=None)
    elif name=='frost_bell':
        box(w,x,z,y,95,35,65,'town_stone',True)
        for sx in [-1,1]:box(w,x+sx*35,z,y+35,12,120,12,'timber',True)
        box(w,x,z,y+155,90,14,14,'roof_red')
        w.cone(x,z,25,y+100,y+140,'iron',n=6)
        w.gable(x,z,110,80,y+170,40,'snow',col=None)
    elif name=='frost_sled':
        box(w,x,z,y,135,16,68,'timber')
        box(w,x,z,y+16,110,42,58,'planks',True)
        box(w,x,z,y+58,100,12,50,'cloth')
        box(w,x,z,y+70,100,3,10,'banner_red')
    elif name=='waystone' or name=='grave_memorial' or name=='falls_marker':
        w.cylinder(x,z,27,y,y+85,'moss_stone',n=5,col='static',r1=18)
        box(w,x,z+23,y+25,12,42,3,'roof_green')
    elif name=='fairy_basin':bowl(w,x,z,y,r=70)
    elif name=='lab_gauge':
        box(w,x,z,y,22,135,22,'roof_green',True)
        for k in range(5):box(w,x,z+12,y+15+k*22,16,4,2,'cloth')
    PLACEMENTS.append({'family':name,'x':round(x),'y':round(y),'z':round(z),
        'triangles':len(w.tris)-before,'collision_vertices':len(w.col_verts)-cv,'materials':sorted({t.mat for t in w.tris[before:]}),
        'bounds':[list(np.min(np.array([p for t in w.tris[before:] for p in t.p]),axis=0)),list(np.max(np.array([p for t in w.tris[before:] for p in t.p]),axis=0))]})
    w.group=old

def build(w):
    PLACEMENTS.clear()
    clusters=[('crown',[('cistern',-450,-3300),('standard',-340,-2800),('planter',650,-3250),('planter',-650,-3500)]),
      ('quarry',[('sled',4050,-2970),('forge',3300,-3350),('gong',3150,-2500),('sled',4400,-2900)]),
      ('kakariko',[('well',3700,-1100),('cart',3500,150),('notice',4550,-1000)]),
      ('deku',[('cradle',3650,2850),('granary',5100,3200),('lantern',4100,3550),('lantern',4800,3700)]),
      ('lake',[('skiff',-2550,3100),('netrack',-2250,3300),('bollard',-2750,3000)]),
      ('ranch',[('wagon',-4580,2900),('milkstand',-2770,2900),('trough',-3400,3830),('wagon',-4500,3900)]),
      ('spirit',[('screen',-4900,150),('stall',-4750,-300),('urn',-3620,650),('stall',-4120,-80)]),
      ('frost',[('woodcrib',-4050,-3890),('bell',-2850,-3350),('sled',-3100,-4650)])]
    for area,placements in clusters:
        for n,x,z in placements:family(w,area+'_'+n,x,z)
        _,x,z=placements[0]
        # Work/rest context uses a few carefully grouped detail pieces.
        y=kit.H(x,z);old=w.group;w.group='Foliage'
        kit.pot(w,x-85,z,y,18);kit.bush(w,x-120,z+60,.35,17)
        for k in range(4):w.cylinder(x-100+k*10,z+80,5,y,y+6,'blossom',n=5,col='none')
        w.group=old
    for name,x,z in [('temple_window',-850,400),('waystone',-2100,800),('fairy_basin',3000,2300),
          ('falls_marker',1350,-2500),('lab_gauge',-1650,4700),('grave_memorial',5000,-1600)]:family(w,name,x,z)
    return list(PLACEMENTS)
