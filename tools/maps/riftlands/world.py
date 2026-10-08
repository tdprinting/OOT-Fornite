"""Reproducible rich Riftlands scene using the proven open-interior Kingdom kit.

World is the single source for Blender, engine drawing, collision and navigation.
Kit POIs are translated into new shelves; new bridges, roots, ruins, waterfalls,
gardens and biome prop clusters are authored here. No game/ROM art is copied.
"""
import sys, math, json
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import layout, terrain
sys.path.append(str(HERE.parent/'kingdom'))
import geom, kit, textures, pois
from layout import *

_HOUSE=kit.house
FURNITURE_SCALE=.4
ORDINARY_STOREY=170
ORDINARY_DOOR_HEIGHT=140

def scaled_furniture(original,framed):
    def build(*args,**kwargs):
        if framed:x,z=args[1].p(args[2],args[3]);y=args[4]
        else:x,z,y=args[1:4]
        return translated(args[0],lambda q:original(q,*args[1:],**kwargs),0,0,
                          scale=FURNITURE_SCALE,origin=(x,z),vertical_scale=FURNITURE_SCALE,floor_y=y)
    return build
def rift_house(*args,**kwargs):
    args=list(args);args[3]=max(args[3],460);args[4]=max(args[4],440)
    kwargs['door_width']=240
    if kwargs.get('name') in ('Castle keep','Snowpeak cabin 0','Snowpeak cabin 1','Lakeside lab'):
        kwargs['stair_width']=160
        args[3]=max(args[3],620);args[4]=max(args[4],620)
    if kwargs.get('name') in ('Castle keep','Snowpeak cabin 1'):kwargs['stair_landing']=True
    if kwargs.get('name')=='Castle keep':kwargs['alternate_stairs']=True
    # Geometry and collider clearance use normal Adult Link; Adult Power is a
    # temporary actor boost, not a permanent 1.35x enlargement of the avatar.
    monument=kwargs.get('name') in ('Castle keep','Snowpeak Lodge')
    saved={n:getattr(kit,n) for n in ('STOREY','DOOR_H','table','stool','bed','shelf','barrel','crate','pot')}
    if not monument:
        kit.STOREY=ORDINARY_STOREY;kit.DOOR_H=ORDINARY_DOOR_HEIGHT
        st=kwargs.get('style') or kit.HouseStyle()
        rise=kwargs.get('roof_rise')
        if rise is None:rise=(.42*min(args[3],args[4]) if st.roof_kind=='hip' else .5*args[3])
        kwargs['roof_rise']=rise*ORDINARY_STOREY/saved['STOREY']
    for n in ('table','stool','bed','shelf','barrel','crate','pot'):
        setattr(kit,n,scaled_furniture(saved[n],n in ('table','stool','bed','shelf')))
    try:return _HOUSE(*args,**kwargs)
    finally:
        for n,value in saved.items():setattr(kit,n,value)

def translated(w,fn,dx,dz,dy=0,scale=1,origin=(0,0),vertical_scale=1,floor_y=0):
    """Transform every representation together, including floors and nav blockers."""
    q=geom.World();real=kit.H;ox,oz=origin
    X=lambda x:ox+(x-ox)*scale+dx
    Z=lambda z:oz+(z-oz)*scale+dz
    Y=lambda y:floor_y+(y-floor_y)*vertical_scale+dy
    def local(x,z):return real(X(x),Z(z))-dy
    kit.H=pois.H=local
    house=kit.house
    def open_house(*args,**kwargs):
        kwargs['back_door']=True
        kwargs['door_width']=240
        if kwargs.get('name','').startswith('Snowpeak cabin'):
            args=list(args);args[6]=2
        name=kwargs.get('name','')
        if name.startswith('Gerudo house'):
            args=list(args);index=int(name.split()[-1]);dx,dz=[(-1050,300),(950,500),(-850,-900)][index];args[1],args[2]=4100+dx,3650+dz
        if name=='ice hut':args=list(args);args[1],args[2]=-1050,-5100
        return rift_house(*args,**kwargs)
    # Kit town functions imported house directly; every Riftlands dwelling keeps
    # two exits, including the three cabins that were single-entry in Kingdom.
    kit.house=pois.house=open_house
    try:fn(q)
    finally:kit.H=pois.H=real;kit.house=pois.house=house
    off=len(w.col_verts)
    sm={i:w.surface(next(n for n,v in geom.SURFACES.items() if v==s),c,h) for i,(s,c,h) in enumerate(q.surfaces)}
    P=lambda v:(X(v[0]),Y(v[1]),Z(v[2]))
    for t in q.tris:
        t.p=tuple(P(v) for v in t.p)
        A,B,C=[np.array(v) for v in t.p];n=np.cross(B-A,C-A);t.normal=tuple(n/max(1e-9,np.linalg.norm(n)))
        w.tris.append(t)
    w.col_verts += [P(v) for v in q.col_verts]
    w.col_tris += [(a+off,b+off,c+off,sm[s]) for a,b,c,s in q.col_tris]
    w.props += [(X(a),Y(b),Z(c),X(d),Y(e),Z(f),n) for a,b,c,d,e,f,n in q.props]
    w.obstacles += [(X(a),X(b),Z(c),Z(d)) for a,b,c,d in q.obstacles]
    for block in q.blockers:
        if block[0]=='box':_,a,b,c,d,e,f=block;w.blockers.append(('box',X(a),X(b),Z(c),Z(d),Y(e),Y(f)))
        else:_,ps,a,b=block;w.blockers.append(('poly',[(X(x),Z(z)) for x,z in ps],Y(a),Y(b)))
    w.buildings += [(X(a),Z(b),c*scale,d*scale,Y(e),f) for a,b,c,d,e,f in q.buildings]
    w.walkways += [(X(a),Z(b),X(c),Z(d),e*scale,Y(f),Y(g)) for a,b,c,d,e,f,g in q.walkways]
    w.loot += [(X(a),Y(b),Z(c),n) for a,b,c,n in q.loot]
    w.doors += [(X(a),Z(b),Y(c),n) for a,b,c,n in q.doors]
    w.interior += [(X(a),X(b),Z(c),Z(d),Y(e),Y(f)) for a,b,c,d,e,f in q.interior]

def lantern(w,x,z,y):
    w.cylinder(x,z,8,y,y+155,'timber',n=5,col='none')
    w.box(x,y+130,z,30,42,30,'window_lit',col='none')
    w.hip(x,z,40,40,y+172,24,'roof_red',col=None)

def arch_bridge(w,a,b,width=340,towers=True):
    """Wide continuous deck with ramps, masonry arches and parapets outside clearance."""
    ax,az=a; bx,bz=b; L=math.hypot(bx-ax,bz-az)
    nx,nz=-(bz-az)/L,(bx-ax)/L
    ya,yb=kit.H(ax,az)+8,kit.H(bx,bz)+8
    y=max(ya,yb)+55
    P=lambda t,side=0:(ax+(bx-ax)*t+nx*side,az+(bz-az)*t+nz*side)
    spans=[(0,.3,ya,y),(.3,.7,y,y),(.7,1,y,yb)]
    for u,v,y0,y1 in spans:
        x0,z0=P(u);x1,z1=P(v)
        w.ramp(x0,z0,x1,z1,width,y0,y1,'cobble',base=min(y0,y1)-45)
        w.walkways.append((x0,z0,x1,z1,width/2,y0,y1))
    for side in [-1,1]:
        A=P(.3,side*(width/2+12));B=P(.7,side*(width/2+12))
        kit.beam(w,(A[0],y+75,A[1]),(B[0],y+75,B[1]),24,75,'castle_stone',block=True)
    # Arched voussoirs are decorative; supports and deck provide the actual solids.
    for c in [.3,.4,.5,.6,.7]:
        x,z=P(c);bed=kit.H(x,z)
        w.box(x,bed-30,z,90,max(20,y-50-bed+30),width,'castle_stone',math.atan2(bz-az,bx-ax),surf='stone')
    for u,v in [(.3,.4),(.4,.5),(.5,.6),(.6,.7)]:
        mid=(u+v)/2; radius=(v-u)*L/2
        for side in [-1,1]:
            for k in range(9):
                t=k*math.pi/9;tt=(k+1)*math.pi/9
                def point(angle,r):
                    px,pz=P(mid+math.cos(angle)*r/L,side*width/2)
                    return (px,y-80-radius*.8+math.sin(angle)*r*.8,pz)
                w.face([point(t,radius),point(tt,radius),point(tt,radius+35),point(t,radius+35)],'castle_stone',double=True)
    if towers:
        for u in [.16,.84]:
            x,z=P(u,-(width/2+250) if az==-1250 else width/2+250)
            tower_y=ya+(y-ya)*u/.3 if u<.5 else yb+(y-yb)*(1-u)/.3
            rift_house(w,x,z,320,320,style=kit.HouseStyle(wall='castle_stone',roof='roof_red'),name='Crossing tower',door_width=240,loot=False,furnish=False,floor_y=tower_y,footing=True)

def waterfall(w,x,z,top,bottom,width=210):
    w.group='Waterfalls';w.far=True
    w.face([(x-width/2,top,z),(x+width/2,top,z),(x+width/2,bottom,z+60),(x-width/2,bottom,z+60)],'water',double=True)
    for k in range(7):
        xx=x-width*.42+k*width*.14
        w.face([(xx,top,z+2),(xx+12,top,z+2),(xx+10,bottom,z+62),(xx-5,bottom,z+62)],'foam',double=True)
    w.cylinder(x,z+85,width*.65,bottom+1,bottom+3,'foam',n=14,col='none')
    w.group='Architecture';w.far=False

def palm(w,x,z,seed):
    y=kit.H(x,z)
    w.cylinder(x,z,18,y,y+320,'bark',n=7,col='none',r1=9)
    w.sphere(x,y+314,z,38,34,38,'leaves',rings=2,segs=7,seed=seed)
    for k in range(6):
        a=k*math.pi/3+seed*.1;dx,dz=math.cos(a),math.sin(a);nx,nz=-dz,dx
        stations=[(0,14,320),(85,34,352),(190,6,283)]
        def P(r,wide,yy,side):return (x+dx*r+nx*wide*side,y+yy,z+dz*r+nz*wide*side)
        for A,B in zip(stations,stations[1:]):
            w.face([P(*A,-1),P(*B,-1),P(*B,1),P(*A,1)],'leaves',double=True)

def detail(w):
    # Tall Spirit gateway, substantial open colonnades, awnings, oasis palms and pots.
    for x in [-4870,-4230]:
        y=kit.H(x,-1200)
        w.box(x,y,-1200,145,700,150,'sandstone',surf='sand')
        w.box(x,y+700,-1200,190,60,180,'sandstone',col='none')
    w.box(-4550,850,-1200,780,160,180,'sandstone',surf='sand')
    for side in [-1,1]:
        for k in range(6):
            x,z=-4550+side*790,-1050+k*215;y=kit.H(x,z)
            w.cylinder(x,z,34,y,y+350,'sandstone',n=8,col='static')
            w.box(x,y+350,z,90,30,90,'sandstone',col='none')
            kit.pot(w,x+65,z,y,28)
        for k in range(3):
            x,z=-4550+side*780,-850+k*430;y=kit.H(x,z)
            w.gable(x,z,270,320,y+290,35,'banner_red' if k%2 else 'cloth',col=None)
    for k in range(10):
        a=k*math.tau/10;palm(w,-3650+280*math.cos(a),500+280*math.sin(a),k)
    w.cylinder(-3650,500,180,kit.H(-3650,500)+1,kit.H(-3650,500)+4,'water',col='none')
    # Open-air recruitment shelters keep hire stations away from combat arenas.
    for k,(x,z) in enumerate(ALLY_SPOTS):
        y=kit.H(x,z)
        for sx in [-1,1]:
            for sz in [-1,1]: w.box(x+sx*145,y,z+sz*135,16,255,16,'timber',col='none')
        w.gable(x,z,330,320,y+255,100,'thatch',col=None)
        kit.table(w,kit.Frame(x,z),-50,0,y,size=(110,65));lantern(w,x+120,z+100,y)
    # Lab island has a normal boardwalk to the village, independent of swimming.
    a,b=(-400,3800),(-1800,4200);y0=-165;y1=kit.H(*b)+8
    kit.beam(w,(a[0],y0,a[1]),(b[0],y1,b[1]),260,22,'planks',surf='wood')
    w.walkways.append((*a,*b,130,y0,y1))
    # The main castle approach is also an explicit continuous stone ramp strip;
    # the terrain uses these same levels, and bots share the physical deck.
    route=[(0,-1500,350),(0,-2200,430),(0,-2700,710),(0,-2970,820),(0,-3300,820)]
    for (ax,az,ay),(bx,bz,by) in zip(route,route[1:]):
        w.ramp(ax,az,bx,bz,360,ay+8,by+8,'cobble',base=min(ay,by)-20)
        w.walkways.append((ax,az,bx,bz,180,ay+8,by+8))
    # Extra golden paddocks; broad fence breaks preserve buggy exits.
    for xx,zz in [(-4500,2100),(-3200,3700)]:
        kit.fence(w,[(xx-500,zz+300),(xx-500,zz-300),(xx+500,zz-300),(xx+500,zz+300)],h=85)
        for k in range(4): w.cylinder(xx+k*90-130,zz+80,40,kit.H(xx,zz),kit.H(xx,zz)+70,'hay',col='none')
    for k in range(14): kit.oak(w,-2750+(k%4)*190,3450+(k//4)*190,.75,k+500)
    # Formal gardens and visible retaining courses beneath the castle.
    for x in [-790,790]:
        for k in range(5):kit.bush(w,x,-3450+k*130,.85,k,y=820)
    for side in [-1,1]:
        w.box(side*1050,740,-3900,90,80,1400,'castle_stone',surf='stone')
    # Three tiered root homes with grounded ramps and continuous tree walkways.
    for k,(x,z,yoff) in enumerate([(4050,3500,220),(4950,3350,280),(4900,2450,340)]):
        y=kit.H(x,z)+yoff
        pois.stilts_deck(w,x,z,520,470,y=y)
        rift_house(w,x,z,340,320,style=kit.HouseStyle(wall='timber',roof='thatch'),name='Deku branch home',door_width=240,floor_y=y,footing=False)
        gx,gz=(x-750,z+200) if k==0 else (x+650,z+(-600 if k==1 else 900));gy=kit.H(gx,gz)
        w.ramp(gx,gz,x,z+230,170,gy,y,'planks',surf='wood',base=gy-20)
        w.walkways.append((gx,gz,x,z+230,85,gy,y))
        lantern(w,x+210,z+180,y)
    for a,b in [((4050,3500),(4950,3350)),((4950,3350),(4900,2450))]:
        floors={(4050,3500):220,(4950,3350):280,(4900,2450):340}
        ya,yb=kit.H(*a)+floors[a],kit.H(*b)+floors[b]
        kit.beam(w,(a[0],ya,a[1]),(b[0],yb,b[1]),160,25,'planks',surf='wood')
        w.walkways.append((*a,*b,80,ya,yb))
    # Sculpted canopy lobes and branch limbs enlarge the silhouette, retaining roots.
    w.group='Foliage';w.far=True
    for k in range(9):
        a=k*math.tau/9;x,z=4400+math.cos(a)*650,2900+math.sin(a)*650
        w.sphere(x,1550+(k%3)*95,z,530,320,520,'leaves' if k%2 else 'leaves_dark',rings=4,segs=9,seed=900+k,jitter=.14)
        kit.beam(w,(4400,900,2900),(x,1300,z),120,90,'bark',col='none')
    w.group='Architecture';w.far=False
    # Deliberate stepped cascade faces and pools; sea and raised levels are runtime water.
    waterfall(w,1700,-3250,820,220,240)
    waterfall(w,2500,-2200,220,60,260)
    waterfall(w,2780,-800,60,-90,230)
    waterfall(w,2050,1250,-90,-180,310)
    waterfall(w,1950,2850,-180,WATER_Y,360)
    # Stage the river with grounded bank rocks and shrubs; their irregular
    # silhouettes hide the physical boxes without inventing collision planes.
    for x0,x1,z0,z1,water in POOLS:
        for k in range(8):
            t=(k+.5)/8;z=z0+(z1-z0)*t
            for side in [-1,1]:
                x=(x0 if side<0 else x1)+side*55;y=kit.H(x,z)
                if y<water+8:continue
                w.sphere(x,y+20,z,45,38,50,'rock',rings=2,segs=6,seed=k+side+77,jitter=.2)
                kit.bush(w,x+side*70,z+35,.45,k+4)
    # Village gardens, fences, crates and warm dock lamps.
    for k in range(12):
        x,z=3400+(k%4)*270,-1050+(k//4)*420
        kit.pot(w,x,z,kit.H(x,z));kit.bush(w,x+50,z,.45,k+400)
    for k in range(10):lantern(w,-700+(k%5)*300,4050+(k//5)*650,-165)
    # Two independent ordinary shore exits for the translated stilt village.
    pois.boardwalk(w,(-400,3800),(-400,3520),width=240)
    for a,b in [((350,3900),(350,2250)),((-400,3520),(-2550,3250))]:
        y0=-165;y1=kit.H(*b)+8
        kit.beam(w,(a[0],y0,a[1]),(b[0],y1,b[1]),240,22,'planks',surf='wood')
        w.walkways.append((*a,*b,120,y0,y1))
    # Small ruined arches/walls and prop clusters interrupt central long sightlines.
    rng=np.random.default_rng(1008)
    for k in range(35):
        x,z=rng.uniform(-2500,2300),rng.uniform(-2100,2200)
        if math.hypot(x+450,z)<850 or terrain.road_dist(np.asarray(x),np.asarray(z))<220:continue
        y=kit.H(x,z)
        w.box(x,y-15,z,190,110+(k%4)*35,45,'moss_stone',yaw=k*.7,surf='stone')
        kit.rock(w,x+180,z+85,.7+(k%3)*.3,k+70)
        if k%3==0:
            for sx in [-1,1]:w.box(x+sx*120,y,z+140,55,250,60,'moss_stone',surf='stone')
            w.box(x,y+240,z+140,295,50,75,'moss_stone',surf='stone')
        w.loot.append((x-85,y,z-100,'route cover'))
    # Eroded strata visually follow the real shoreline/terrace collision.
    # These ribs are decorative, avoiding separate invisible gameplay walls.
    w.group='Cliff strata'
    for k in range(160):
        a=k*math.tau/160;x,z=5500*math.cos(a),5500*math.sin(a)
        if terrain.road_dist(np.asarray(x),np.asarray(z))<220:continue
        y=kit.H(x,z)
        if y<0:continue
        for tier in range(3):
            rx,rz=x+math.cos(a)*tier*65,z+math.sin(a)*tier*65
            w.sphere(rx,y-55-tier*65,rz,95,100,100,'rock',rings=3,segs=6,seed=k*3+tier,jitter=.28)
    # Forge buildings, lava side pockets, basalt terraces and emissive fissures.
    w.group='Architecture'
    for k in range(3):
        x,z=[(3650,-3600),(4250,-3150),(4850,-3750)][k]
        rift_house(w,x,z,360,340,style=kit.HouseStyle(wall='brick_dark',roof='roof_red'),name='Quarry forge',door_width=240)
    # A broad basalt crater lip, inset lava and narrow terrain-following ribbons.
    cx,cz=4550,-4800;cy=kit.H(cx,cz)
    w.cylinder(cx,cz,270,cy+2,cy+5,'lava_glow',n=16,col='none')
    for k in range(16):
        a=k*math.tau/16;x,z=cx+math.cos(a)*540,cz+math.sin(a)*540
        kit.rock(w,x,z,1.1,k+2100,mat='brick_dark',y=kit.H(x,z),solid=True)
    w.far=True
    for k in range(3):
        a=k*2.1+.4;dx,dz=math.cos(a),math.sin(a);nx,nz=-dz,dx
        for j in range(6):
            r0,r1=460+j*125,460+(j+1)*125
            x0,z0=cx+dx*r0,cz+dz*r0;x1,z1=cx+dx*r1,cz+dz*r1
            if terrain.road_dist(np.asarray(x0),np.asarray(z0))<220:continue
            pts=[(x0+nx*12,z0+nz*12),(x1+nx*12,z1+nz*12),(x1-nx*12,z1-nz*12),(x0-nx*12,z0-nz*12)]
            w.face([(x,kit.H(x,z)+4,z) for x,z in pts],'lava_glow',double=True)
    w.far=False
    # Shore boulder clusters, exposed cliff courses and smaller flower patches.
    w.group='Props'
    for k in range(90):
        a=.2+k*math.tau/90;x,z=2600*math.cos(a),4700+1900*math.sin(a)
        y=kit.H(x,z)
        if y>350 or y<-320 or terrain.road_dist(np.asarray(x),np.asarray(z))<230:continue
        kit.rock(w,x,z,.6+(k%4)*.3,k+890,solid=False)
        if y>0 and k%3==0:
            for j in range(4):w.cylinder(x+j*13,z+20,8,y,y+8,'blossom',n=5,col='none')
    # Shared/sculpted foliage is culled by engine spatial batches; keep road edges clear.
    w.group='Foliage'
    for k in range(540):
        x,z=rng.uniform(-5900,5900),rng.uniform(-5400,5400)
        y=kit.H(x,z)
        if y<terrain.water_y(np.asarray(x),np.asarray(z))+45 or terrain.road_dist(np.asarray(x),np.asarray(z))<250:continue
        if any(math.hypot(x-px,z-pz)<r+160 for _,px,pz,r,*_ in POIS[:9]):continue
        if any(math.hypot(x-px,z-pz)<240 for px,pz,_,_ in w.doors):continue
        if terrain.slope_up(np.asarray(x),np.asarray(z))<.78:continue
        if z<-3000 and x<0:kit.pine(w,x,z,rng.uniform(.8,1.3),k,snow=y>520)
        elif x<-3000 and z<1200:palm(w,x,z,k)
        else:kit.oak(w,x,z,rng.uniform(.7,1.3),k)
        if k%4==0:kit.bush(w,x+60,z+80,.8,k)
    w.group='Architecture'

def build(only=None):
    h=terrain.vertex_grid();kit.HGRID=h

    T=textures.build_all()
    # Teal spires, ochre village roofs, turquoise cascades and light foam.
    im,units=T['roof_blue']; im=im.copy();im[...,0]*=.72;im[...,1]*=1.25;T['roof_blue']=(np.clip(im,0,255),units)
    T['castle_stone']=(textures.blocks('#d7ccb2','#a7a18e',5,4,42,jitter=.15,mortar_w=2,bevel=False),120)
    for n in ['castle_stone','town_stone','moss_stone']:
        image,units=T[n];T[n]=(np.clip(image*1.25+35,0,255),units)
    T['lava_rock']=(np.clip(T['brick_dark'][0]*.8+np.array([15,5,0]),0,255),100)
    T['foam']=(np.full((256,256,3),[205,239,229],dtype=np.float64),128)
    yy,xx=np.mgrid[0:256,0:256];rr=np.hypot(xx-128,yy-128)/128;aa=np.arctan2(yy-128,xx-128)
    spokes=(np.abs(np.sin(aa*3))<.16)|(rr>.78)|(rr<.2)
    T['wagon_wheel']=(np.where(spokes[...,None],np.array([145,101,57]),np.array([74,59,43])).astype(float),64)
    T['rust_cloth']=(np.clip(T['cloth'][0]*np.array([1.2,.63,.4]),0,255),120)
    T['leather']=(np.clip(T['cloth'][0]*np.array([.65,.47,.35]),0,255),90)
    tint=np.zeros((256,256,3),float)+[52,115,110];leaf=((xx-128)**2/3500+(yy-128)**2/6000)<1;mark=leaf & (abs(xx-128)<abs(yy-128)*.65+15);tint[mark]=[220,208,167]
    T['banner_blue']=(tint,128)
    T['bronze']=(np.clip(T['iron'][0]*.55+np.array([68,58,30]),0,255),90)
    T['woven']=(np.clip(T['cloth'][0]*np.array([.55,.8,.95]),0,255),90)
    T['pottery']=(np.clip(T['dirt'][0]*np.array([1.15,.94,.75]),0,255),90)
    T['lava_glow']=(np.full((256,256,3),[255,91,15],dtype=np.float64),100)
    geom.TEX_UNITS.clear();geom.TEX_UNITS.update({n:u for n,(_,u) in T.items()})
    w=geom.World()
    upper_furniture=kit.furnish_upper
    def castle_loft_furniture(scene,f,W,D,y,name):
        if name!='Castle keep':return upper_furniture(scene,f,W,D,y,name)
        # The roof stair occupies the right wall; furniture stays in the loft.
        kit.bed(scene,f,55,-D/2+kit.WALL+110,y)
        px,pz=f.p(75,60);kit.barrel(scene,px,pz,y)
        px,pz=f.p(40,D/2-kit.WALL-120);kit.crate(scene,px,pz,y,60,f.yaw)
    kit.furnish_upper=castle_loft_furniture
    translated(w,pois.hyrule_castle,0,-2150,300,1.45,(0,-1700))
    kit.furnish_upper=upper_furniture
    w.loot=[(-218,y,z,n) if n=='Castle keep upstairs' else (x,y,z,n) for x,y,z,n in w.loot]
    translated(w,pois.snowpeak,-950,600)
    translated(w,pois.death_mountain,200,-450)
    oak=pois.oak
    def village_oak(scene,x,z,*args,**kwargs):
        if any(math.hypot(x-dx,z-dz)<260 for dx,dz,_,_ in scene.doors):x-=300
        return oak(scene,x,z,*args,**kwargs)
    pois.oak=village_oak
    translated(w,pois.kakariko,-150,700)
    pois.oak=oak
    ring=pois.ring_wall
    def ranch_ring(scene,cx,cz,r,t,y0,y1,mat,**kw):
        if (cx,cz,r)==(1500,4550,660):
            # Keep the working buildings clear; low outer courses frame paddocks.
            return ring(scene,cx,cz,1080,45,y0,y0+95,mat,gaps=[(0,400),(math.pi/2,400),(math.pi,400),(math.pi*1.5,400)],n=24)
        return ring(scene,cx,cz,r,t,y0,y1,mat,**kw)
    pois.ring_wall=ranch_ring
    translated(w,pois.lon_lon,-5400,-1550)
    pois.ring_wall=ring
    translated(w,pois.gerudo_ruins,-8650,-4350)
    tower=pois.spiral_tower
    def ruin_tower(scene,x,z,size,height,**kw):
        if kw.get('name')!='Temple bell tower':return tower(scene,x,z,size,height,**kw)
        y=kw.get('y0',kit.H(x,z))
        for sx,sz,hh in [(-1,-1,470),(1,-1,310),(-1,1,190),(1,1,380)]:
            scene.box(x+sx*size*.38,y,z+sz*size*.38,60,hh,60,'castle_stone',surf='stone')
        scene.box(x,y+410,z-size*.38,size,55,65,'castle_stone',surf='stone')
    pois.spiral_tower=ruin_tower
    translated(w,pois.temple_ruins,1450,-4700,160)
    pois.spiral_tower=tower
    translated(w,pois.lake_stilts,2950,1850)
    pois.deku_tree(w,4400,2900)
    pois.fairy_fountain(w,3000,2350)
    rift_house(w,-1800,4500,450,400,storeys=2,name='Lakeside lab',door_width=240)
    for k,(x,z) in enumerate([(3900,450),(4600,50)]):
        rift_house(w,x,z,420,400,storeys=2,name='Kakariko house annex '+str(k),door_width=240)
    # Each stilt exit has a real broad landing and a flank link to the existing
    # boardwalk network. The landings sit1 unit above the old deck, avoiding
    # coplanar duplicated faces while preserving ordinary step height.
    def in_room(x,z,grow=120):
        for bx,bz,hw,hd,fy,yaw in w.buildings:
            if fy!=-165:continue
            dx,dz=x-bx,z-bz;c,s=math.cos(yaw),math.sin(yaw)
            if abs(dx*c+dz*s)<hw+grow and abs(-dx*s+dz*c)<hd+grow:return True
        return False
    def clear(a,b):
        distance=math.hypot(a[0]-b[0],a[1]-b[1])
        return not any(in_room(a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t) for t in np.linspace(0,1,max(2,int(distance/25)+1)))
    corners=[]
    for bx,bz,hw,hd,fy,yaw in w.buildings:
        if fy!=-165:continue
        c,s=math.cos(yaw),math.sin(yaw)
        for x,z in [(-hw-145,-hd-145),(hw+145,-hd-145),(hw+145,hd+145),(-hw-145,hd+145)]:
            p=(bx+x*c-z*s,bz+x*s+z*c)
            if not in_room(*p):corners.append(p)
    walks=list(w.walkways)
    destinations=[]
    for ax,az,cx,cz,hw,y0,y1 in walks:
        if y0!=-165 or y1!=-165:continue
        for t in np.linspace(0,1,12):
            p=(ax+(cx-ax)*t,az+(cz-az)*t)
            if not in_room(*p):destinations.append(p)
    import heapq
    for dx,dz,fy,name in w.doors:
        if not name.startswith('stilt hut'):continue
        rooms=[b for b in w.buildings if b[4]==fy and math.hypot(b[0]-dx,b[1]-dz)<400]
        bx,bz,*_=min(rooms,key=lambda b:math.hypot(b[0]-dx,b[1]-dz))
        L=math.hypot(dx-bx,dz-bz);ux,uz=(dx-bx)/L,(dz-bz)/L
        ex,ez=dx+ux*170,dz+uz*170
        kit.beam(w,(dx,fy+1,dz),(ex,fy+1,ez),320,22,'planks',surf='wood')
        w.walkways.append((dx,dz,ex,ez,160,fy+1,fy+1))
        nodes=[(ex,ez)]+corners+destinations
        dist={0:0};prev={};queue=[(0,0)];goal=None
        while queue:
            cost,i=heapq.heappop(queue)
            if cost!=dist[i]:continue
            if i>len(corners):goal=i;break
            for j,p in enumerate(nodes):
                if j==i or not clear(nodes[i],p):continue
                value=cost+math.hypot(p[0]-nodes[i][0],p[1]-nodes[i][1])
                if value<dist.get(j,float('inf')):
                    dist[j]=value;prev[j]=i;heapq.heappush(queue,(value,j))
        if goal is None:raise RuntimeError('dock exit lacks a flank link '+name)
        route=[goal]
        while route[-1]:route.append(prev[route[-1]])
        route.reverse()
        for i,j in zip(route,route[1:]):
            ax,az=nodes[i];px,pz=nodes[j]
            kit.beam(w,(ax,fy+1,az),(px,fy+1,pz),200,22,'planks',surf='wood')
            w.walkways.append((ax,az,px,pz,100,fy+1,fy+1))
    detail(w)
    import props
    props.build(w)
    arch_bridge(w,(-2800,2800),(-1000,2350))
    arch_bridge(w,(650,2250),(3150,3000))
    arch_bridge(w,(1100,-2400),(2900,-2400),width=360)
    # Exactly 48 starter sites at POIs, 24 route/secondary sites, 12 upper-room
    # sites. Exclude legacy cone-covered tower and silo-top candidates.
    import importlib.util
    spec=importlib.util.spec_from_file_location('placement_helpers',HERE.parent/'kingdom'/'export.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    obs=helpers.obstacles(w,h)
    import author_nav
    connected=author_nav.reachable(w,h,obs)
    def floor(x,z):return helpers.walk_floor(w,h,x,z)
    def safe(x,z):
        if not connected(x,z):return False
        y=floor(x,z)
        if y<terrain.water_y(np.asarray(x),np.asarray(z))+15:return False
        if any(a-22<x<b+22 and c-22<z<d+22 for a,b,c,d in obs):return False
        if any(abs(x-bx)<bw-40 and abs(z-bz)<bd-40 for bx,bz,bw,bd,fy,yaw in w.buildings):return True
        return terrain.slope_up(np.asarray(x),np.asarray(z))>=.84 or any(helpers.walk_floor(w,h,x,z)>kit.H(x,z)+20 for _ in [0])
    original=list(w.loot);chosen=[]
    ground=[s for s in original if abs(s[1]-floor(s[0],s[2]))<45 and safe(s[0],s[2])]
    for _,px,pz,rad,*_ in POIS[:8]:
        selected=[]
        for s in sorted(ground,key=lambda s:math.hypot(s[0]-px,s[2]-pz)):
            if s not in chosen and math.hypot(s[0]-px,s[2]-pz)<rad+650:selected.append(s)
            if len(selected)==6:break
        for radius in [rad*.55,rad*.8,rad,rad+220]:
            for k in range(24):
                x,z=px+radius*math.cos(k*math.tau/24),pz+radius*math.sin(k*math.tau/24)
                if len(selected)>=6:break
                if safe(x,z) and all(math.hypot(x-s[0],z-s[2])>110 for s in chosen+selected):selected.append((x,floor(x,z),z,'starter pocket'))
        if len(selected)!=6:raise RuntimeError('insufficient starter sites '+str((px,pz)))
        chosen+=selected
    # One route/landmark site at all 16 secondary places, plus a second at eight
    # larger landmarks. This keeps the 24 sites distributed around the island.
    for index,(_,x,z,r,*_) in enumerate(POIS[8:]):
        count=0;wanted=2 if index<8 else 1
        for radius in [r*.55,r*.85,r+150,r+350]:
            for k in range(16):
                if count>=wanted:break
                px,pz=x+radius*math.cos(k*math.tau/16),z+radius*math.sin(k*math.tau/16)
                if safe(px,pz) and all(math.hypot(px-q[0],pz-q[2])>110 for q in chosen):
                    chosen.append((px,floor(px,pz),pz,'secondary pocket'));count+=1
        if count!=wanted:raise RuntimeError('secondary landmark lacks dry sites '+str(index))
    upstairs=[s for s in original if 'upstairs' in s[3]]
    for s in upstairs:
        if len(chosen)>=84:break
        if s not in chosen:chosen.append(s)
    if len(chosen)!=84:raise RuntimeError('expected 84 chest candidates, got '+str(len(chosen)))
    w.loot=chosen
    # Engine rendering is already two-sided. Coincident opposing decorative
    # triangles produce self-shadowing artifacts in portable Blender/glTF assets.
    seen=set();unique=[]
    for t in w.tris:
        key=(t.group,t.mat,tuple(sorted(tuple(round(c,4) for c in p) for p in t.p)))
        if key not in seen:seen.add(key);unique.append(t)
    w.tris=unique
    w.markers=[{'name':n,'kind':'region','x':x,'z':z} for n,x,z,*_ in POIS]
    return w,h,T

if __name__=='__main__':
    w,h,T=build();print(json.dumps({'triangles':len(w.tris),'collision_vertices':len(w.col_verts),'buildings':len(w.buildings),'loot':len(w.loot),'walkways':len(w.walkways)}))
