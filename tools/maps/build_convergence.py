"""Blender 5: reproducible textured map asset. Run blender -b -t 4 --python this_file."""
import bpy, math, random, os, json
from mathutils import Vector
random.seed(64)
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'../..'))
OUT=os.path.join(ROOT,'assets','maps','hyrule_convergence'); os.makedirs(OUT,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.unit_settings.system='METRIC'
# One Blender metre = 100 engine units. Blender XYZ -> game (100X,100Z,-100Y).
COL={}
def collection(name):
    c=bpy.data.collections.new(name); scene.collection.children.link(c); COL[name]=c; return c
for n in ['Terrain','Architecture','Foliage','Props','Water','Gameplay markers','Presentation']: collection(n)
def move(o,c):
    for old in list(o.users_collection): old.objects.unlink(o)
    COL[c].objects.link(o)
def material(name,color,pattern):
    # Packed pixel textures, deliberately small and nearest filtered for OoT styling.
    im=bpy.data.images.new(name+'_256px',width=256,height=256)
    pixels=[]
    for y in range(256):
        for x in range(256):
            noise=random.uniform(-.035,.035)+.035*math.sin(x*.05)*math.cos(y*.09)
            if pattern=='stone': noise-=.17 if y%64<4 or (x+(32 if (y//64)%2 else 0))%128<4 else 0
            if pattern=='wood': noise+=.075*math.sin(x*.22+math.sin(y*.028)*2)+.025*math.sin(x*.65)
            if pattern=='tile': noise-=.18 if y%48<4 or (x+(24 if y//48%2 else 0))%64<3 else 0
            pixels.extend([max(0,min(1,v+noise)) for v in color]+[1])
    im.pixels=pixels; im.filepath_raw=os.path.join(OUT,name+'.png'); im.file_format='PNG'; im.save(); im.pack()
    m=bpy.data.materials.new(name); m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF'); bs.inputs['Roughness'].default_value=.88
    t=m.node_tree.nodes.new('ShaderNodeTexImage'); t.image=im; t.interpolation='Linear'
    m.node_tree.links.new(t.outputs['Color'],bs.inputs['Base Color'])
    bump=m.node_tree.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.3;bump.inputs['Distance'].default_value=.055
    m.node_tree.links.new(t.outputs['Color'],bump.inputs['Height']);m.node_tree.links.new(bump.outputs['Normal'],bs.inputs['Normal'])
    return m
M={}
for name,c,p in [('grass',(.29,.43,.16),'noise'),('forest',(.14,.29,.12),'noise'),('sand',(.66,.48,.26),'noise'),('basalt',(.22,.22,.25),'stone'),('snow',(.78,.85,.86),'noise'),('stone',(.49,.48,.37),'stone'),('plaster',(.76,.66,.47),'noise'),('wood',(.33,.19,.09),'wood'),('roof',(.44,.16,.10),'tile'),('blue_roof',(.14,.30,.36),'tile'),('moss',(.27,.34,.24),'stone'),('path',(.51,.40,.23),'noise'),('gold',(.80,.59,.16),'noise'),('water',(.10,.39,.46),'noise')]: M[name]=material(name,c,p)
for name,c in [('linen',(.59,.25,.19)),('rug',(.15,.32,.28)),('ceramic',(.54,.31,.17)),('pages',(.81,.72,.49)),('iron',(.14,.16,.17))]:M[name]=material(name,c,'wood' if name=='linen' else 'noise')
def mesh(name,v,f,mat,c='Architecture'):
    me=bpy.data.meshes.new(name); me.from_pydata(v,[],f); me.update(); o=bpy.data.objects.new(name,me); COL[c].objects.link(o); me.materials.append(M[mat])
    uv=me.uv_layers.new(name='UVMap')
    for poly in me.polygons:
        n=poly.normal; axis=max(range(3),key=lambda i:abs(n[i])); axes=[i for i in range(3) if i!=axis]
        for li in poly.loop_indices:
            co=me.vertices[me.loops[li].vertex_index].co; uv.data[li].uv=(co[axes[0]]/3,co[axes[1]]/3)
    return o
def box(name,loc,size,mat,c='Architecture'):
    x,y,z=loc; a,b,d=[v/2 for v in size]
    o=mesh(name,[(x+i*a,y+j*b,z+k*d) for k in [-1,1] for j in [-1,1] for i in [-1,1]],[(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],mat,c)
    o['collision']=c=='Architecture' and ('post' not in name and 'table' not in name)
    if c not in ['Terrain','Water']:
        bevel=o.modifiers.new('Worn edges','BEVEL');bevel.width=.045;bevel.segments=2
    return o
POIS=[('Triforce Market',0,0,'stone',None),('Lon Lon Crossroads',-22,-24,'grass',None),('Lostwood Sanctuary',-44,23,'forest','Moss'),('Ember Quarry',27,43,'basalt','Lava'),('Frostwatch Lodge',-17,48,'snow','Frost'),('Spirit Caravanserai',46,-24,'sand','Dune'),('Lake Lantern',38,8,'grass','Tide'),('Whispering Graveyard',-43,-28,'moss','Shade'),('Royal Ruins',4,-43,'stone','Stone')]
def house_sites(p):
    _,x,y,_,boss=p
    if not boss:return [(x+ox,y+oy,8,6) for ox,oy in [(-9,-7),(9,-7),(-9,7),(9,7)]]
    r=math.hypot(x,y)
    return [(x+s*(-y/r)*12,y+s*(x/r)*12,7,6) for s in [-1,1]]
FLATS=[]
for p in POIS:
    _,x,y,_,_=p;z=.6+math.sin(x*.07)*math.cos(y*.06)
    FLATS.append((x,y,8,z))
    for bx,by,w,d in house_sites(p):FLATS.append((bx,by,6.5,z))
FLATS.append((0,23,9,.6))
def height(x,y):
    r=math.hypot(x,y)
    if r>64: return .2-(r-64)*.8
    h=.6+1.0*math.sin(x*.07)*math.cos(y*.06)
    h+=7*math.exp(-((x-28)**2+(y-47)**2)/180)+5*math.exp(-((x+17)**2+(y-51)**2)/160)
    h-=5.6*math.exp(-((x-49)**2+(y-13)**2)/45)
    for px,py,rad,flat in FLATS:
        d=math.hypot(x-px,y-py)
        if d<rad+3:
            t=max(0,(d-rad)/3);h=h*t+flat*(1-t)
    return h
def biome(x,y):
    if y>36 and x<4:return 'snow'
    if y>29 and x>12:return 'basalt'
    if x>29 and y<-7:return 'sand'
    if x<-28 and y>3:return 'forest'
    if x<-28 and y<-15:return 'moss'
    return 'grass'
v=[]; f=[]; N=64
for j in range(N+1):
    for i in range(N+1):
        x=-74.1253+i*148.2506/N;y=-77.0572+j*154.1144/N;v.append((x,y,height(x,y)))
for j in range(N):
    for i in range(N):
        a=j*(N+1)+i;f.extend([(a,a+1,a+N+1),(a+1,a+N+2,a+N+1)])
terrain=mesh('Island terrain',v,f,'grass','Terrain')
for k in ['forest','sand','basalt','snow','moss']:terrain.data.materials.append(M[k])
keys=['grass','forest','sand','basalt','snow','moss']
for p in terrain.data.polygons:
    co=p.center;p.material_index=keys.index(biome(co.x,co.y))
box('Sea', (0,0,-2.27),(184,184,.12),'water','Water')
def disc(name,x,y,r,mat,z=None):
    if name.endswith((' plaza',' combat ring')):return None
    z=height(x,y)+.05 if z is None else z
    return mesh(name,[(x,y,z)]+[(x+r*math.cos(i*math.tau/48),y+r*math.sin(i*math.tau/48),z) for i in range(48)],[(0,i+1,(i+1)%48+1) for i in range(48)],mat,'Terrain')
ROAD_SEGMENTS=[]
def road(a,b,width=3):
    ROAD_SEGMENTS.append((a,b,width));return
    dx=b[0]-a[0];dy=b[1]-a[1];l=math.hypot(dx,dy); nx=-dy/l*width/2;ny=dx/l*width/2
    verts=[]
    for i in range(25):
        t=i/24;x=a[0]+dx*t;y=a[1]+dy*t
        for s in [-1,1]:verts.append((x+s*nx,y+s*ny,height(x+s*nx,y+s*ny)+.09))
    mesh('Buggy route',verts,[(2*i,2*i+1,2*i+3,2*i+2) for i in range(24)],'path','Terrain')
for i,p in enumerate(POIS[1:]):road((p[1],p[2]),(0,0));q=POIS[1:][(i+1)%8];road((p[1],p[2]),(q[1],q[2]),2.8)
def ground_colour(x,y):
    palette={'grass':(.29,.43,.16),'forest':(.14,.29,.12),'sand':(.66,.48,.26),'basalt':(.22,.22,.25),'snow':(.78,.85,.86),'moss':(.27,.34,.24),'stone':(.49,.48,.37)}
    c=palette[biome(x,y)]
    for name,px,py,mat,boss in POIS:
        d=math.hypot(x-px,y-py)
        if d<9:c=palette[mat]
        if boss and d<4.5:c=(.51,.40,.23)
    for a,b,w in ROAD_SEGMENTS:
        dx=b[0]-a[0];dy=b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
        d=math.hypot(x-a[0]-dx*t,y-a[1]-dy*t)
        if d<w*.5:c=(.51,.40,.23)
    if height(x,y)<-2.27:c=(.10,.39,.46)
    noise=.018*math.sin(x*4.2+y*3.7)+.015*math.cos(x*8-y*6)
    return tuple(max(0,min(1,v+noise)) for v in c)
# A painted ground atlas avoids coplanar road/plaza geometry and preserves collision exactly.
im=bpy.data.images.new('Convergence ground atlas',width=768,height=768);pixels=[]
for j in range(768):
    for i in range(768):pixels.extend((*ground_colour(-74.1253+i*148.2506/767,-77.0572+j*154.1144/767),1))
im.pixels=pixels;im.filepath_raw=os.path.join(OUT,'ground_atlas.png');im.file_format='PNG';im.save();im.pack()
ground=bpy.data.materials.new('ground_atlas');ground.use_nodes=True;bs=ground.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.95
tex=ground.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;ground.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
terrain.data.materials.clear();terrain.data.materials.append(ground)
for p in terrain.data.polygons:p.material_index=0
for li in range(len(terrain.data.loops)):
    co=terrain.data.vertices[terrain.data.loops[li].vertex_index].co
    terrain.data.uv_layers.active.data[li].uv=((co.x+74.1253)/148.2506,(co.y+77.0572)/154.1144)
loot=[];buildings=[];markers=[]
def marker(name,x,y,z,kind):
    o=bpy.data.objects.new(name,None);COL['Gameplay markers'].objects.link(o);o.location=(x,y,z);o.empty_display_type='SPHERE';o.empty_display_size=.5;o['kind']=kind
    markers.append({'name':name,'kind':kind,'game_position':[round(x*100,2),round(z*100,2),round(-y*100,2)]})
def chest(x,y,z):
    box('Loot chest', (x,y,z+.32),(.8,.5,.6),'wood','Props')
    for sx in [-.26,.26]:box('Chest band',(x+sx,y,z+.33),(.08,.53,.65),'gold','Props')
    marker('Chest site',x,y,z,'loot');loot.append((x,y,z))
def vessel(name,x,y,z,r=.3,h=.7,mat='ceramic'):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,radius=1,location=(x,y,z+h*.5));o=bpy.context.object;o.name=name;o.scale=(r,r,h*.5);o.data.materials.append(M[mat]);move(o,'Props')
    for p in o.data.polygons:p.use_smooth=True
def beam(name,a,b,thick=.16,mat='wood'):
    mid=(Vector(a)+Vector(b))/2;o=box(name,(0,0,0),(thick,thick,(Vector(b)-Vector(a)).length),mat,'Props');o.location=mid;o.rotation_euler=(Vector(b)-Vector(a)).to_track_quat('Z','Y').to_euler();return o
def furnish(name,x,y,z,w,d):
    # Everything stays to the sides; the central 2.2 m door-to-door aisle is free.
    side=x-w*.29
    box(name+' bed frame',(side,y+d*.22,z+.38),(1.55,2,.5),'wood','Props')
    box('Woven bedcover',(side,y+d*.22,z+.67),(1.5,1.75,.18),'linen','Props')
    box('Pillow',(side,y+d*.35,z+.8),(1.2,.45,.15),'pages','Props')
    box('Geometric woven carpet',(x,y,z+.13),(1.7,2.1,.025),'rug','Props')
    sx=x+w*.3
    for zz in [.25,1.1,1.95]:box('Pantry shelf',(sx,y+d*.22,z+zz),(1.6,.5,.12),'wood','Props')
    for a in [-.72,.72]:box('Shelf upright',(sx+a,y+d*.22,z+1.1),(.12,.55,2.2),'wood','Props')
    for zz in [.36,1.21]:
        for a in [-.45,0,.45]:vessel('Pantry pottery',sx+a,y+d*.22,z+zz,.16,.38)
    for a in [-.4,0,.4]:box('Bound book',(sx+a,y+d*.22,z+2.17),(.2,.32,.32),'linen' if a==0 else 'rug','Props')
    box('Hearth back',(x+w*.41,y-d*.27,z+.85),(.3,1.45,1.5),'basalt','Props')
    for sy in [-.7,.7]:box('Hearth cheek',(x+w*.34,y-d*.27+sy,z+.6),(.7,.18,1),'stone','Props')
    box('Hearth mantel',(x+w*.34,y-d*.27,z+1.25),(.8,1.6,.2),'stone','Props')
    for sy in [-.38,.38]:beam('Firewood',(x+w*.3,y-d*.27+sy,z+.2),(x+w*.39,y-d*.27-sy,z+.25),.18)
    vessel('Table jug',x-w*.27,y,z+.95,.2,.42)
    box('Bench',(x-w*.27,y-.75,z+.5),(1.6,.38,.15),'wood','Props')
    for a in [-.62,.62]:box('Bench foot',(x-w*.27+a,y-.75,z+.28),(.13,.33,.45),'wood','Props')
    vessel('Storage barrel',x+w*.28,y-d*.38,z+.1,.38,.9,'wood')
def house(name,x,y,w=7,d=6,style='plaster',roof='roof'):
    z=height(x,y)+.12; wall=.3;H=3.6;gap=2.2
    box(name+' floor',(x,y,z),(w,d,.22),'stone')
    # Front and back doors: open full width, no invisible closure.
    for s in [-1,1]:
        for side in [-1,1]:box(name+' doorway flank',(x+side*(w+gap)/4,y+s*d/2,z+H/2),((w-gap)/2,wall,H),style)
        box(name+' door lintel',(x,y+s*d/2,z+3.2),(gap,wall,.8),style)
    # Side windows: divided walls, openings at eye level.
    for s in [-1,1]:
        box(name+' sill',(x+s*w/2,y,z+.65),(wall,d,1.3),style)
        box(name+' window header',(x+s*w/2,y,z+3.15),(wall,d,.9),style)
        for t in [-1,1]:box(name+' side pier',(x+s*w/2,y+t*d*.36,z+2),(wall,d*.28,1.4),style)
    for sx in [-1,1]:
        for sy in [-1,1]:box(name+' oak post',(x+sx*w/2,y+sy*d/2,z+1.8),(.38,.38,3.6),'wood')
    mesh(name+' pitched roof',[(x-w/2-.45,y-d/2-.45,z+H),(x+w/2+.45,y-d/2-.45,z+H),(x,y-d/2-.45,z+H+2),(x-w/2-.45,y+d/2+.45,z+H),(x+w/2+.45,y+d/2+.45,z+H),(x,y+d/2+.45,z+H+2)],[(2,5,3,0),(1,4,5,2),(1,2,0),(5,4,3)],roof)
    box(name+' table',(x-w*.27,y,z+.85),(1.7,.8,.15),'wood','Props')
    for a in [-.65,.65]:box('Table leg',(x-w*.27+a,y,z+.43),(.15,.5,.85),'wood','Props')
    chest(x+w*.25,y-d*.25,z+.12)
    furnish(name,x,y,z,w,d)
    for yy in [-d*.5,d*.5]:
        for xx in [-1.22,1.22]:box('Carved door jamb',(x+xx,y+yy,z+1.45),(.16,.36,2.9),'wood','Props')
        box('Door oak header',(x,y+yy,z+2.9),(2.6,.38,.18),'wood','Props')
    for sy in [-1,1]:
        beam('Gable brace',(x-w/2,y+sy*d/2,z+3.6),(x,y+sy*d/2,z+5.6),.13)
        beam('Gable brace',(x+w/2,y+sy*d/2,z+3.6),(x,y+sy*d/2,z+5.6),.13)
    box('Chimney',(x+w*.29,y+d*.15,z+5),(.65,.65,2.1),'stone','Props')
    for sy in [-1,1]:
        box('Threshold ramp',(x,y+sy*(d/2+.5),z-.03),(2.25,1.1,.18),'stone')
    buildings.append({'name':name,'center_blender':[x,y,z],'size_blender':[w,d],'door_clearance_units':[220,280],'entrances':2})
def rock(x,y,s=1,mat='stone'):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1,location=(x,y,height(x,y)+s*.4));o=bpy.context.object;o.name='Cover boulder';o.scale=(s,s*.8,s*.7);o.data.materials.append(M[mat]);o['collision']=True;move(o,'Props')
def tree(x,y,s=1):
    z=height(x,y)
    trunk=box('Tree trunk',(x,y,z+2*s),(.65*s,.65*s,4*s),'wood','Foliage');trunk['collision']=True
    for h,r in [(3,2.3),(4.4,1.8),(5.7,1.2)]:
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1,location=(x,y,z+h*s));o=bpy.context.object;o.name='Rounded fantasy canopy';o.scale=(r*s,r*s,1.6*s);o.data.materials.append(M['snow' if biome(x,y)=='snow' and h>4 else 'forest']);move(o,'Foliage')
        for p in o.data.polygons:p.use_smooth=True
for name,x,y,mat,boss in POIS:
    disc(name+' plaza',x,y,9,mat)
    if boss:
        marker(name+' miniboss '+boss,x,y,height(x,y)+.2,'BossKind::'+boss)
        # 900-unit diameter arena, 1100-unit leash region remains unenclosed.
        disc(name+' combat ring',x,y,4.5,'path',height(x,y)+.085)
        for a in [0,1.7,3.2,4.8]:rock(x+7*math.cos(a),y+7*math.sin(a),1.2,mat if mat!='grass' else 'stone')
        styles={'Moss':('wood','forest'),'Lava':('basalt','basalt'),'Frost':('wood','blue_roof'),'Dune':('sand','blue_roof'),'Tide':('plaster','blue_roof'),'Shade':('moss','basalt'),'Stone':('stone','blue_roof')}
        for bx,by,w,d in house_sites((name,x,y,mat,boss)):house(name+' outpost',bx,by,w,d,*styles[boss])
    else:
        for ox,oy in [(-9,-7),(9,-7),(-9,7),(9,7)]:house(name+' house',x+ox,y+oy,w=8,d=6)
    chest(x+3,y+2,height(x,y)+.15)
    marker(name,x,y,height(x,y),'poi')
# Market fountain and castle landmark, perimeter gates preserve routes.
disc('Market fountain basin',0,0,2.1,'stone',height(0,0)+.3);disc('Fountain water',0,0,1.7,'water',height(0,0)+.34)
house('Royal Pavilion',0,23,12,9,'stone','blue_roof')
for x in [-8,8]:
    z=height(x,25)
    bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=1.8,depth=11,location=(x,25,z+5.5));o=bpy.context.object;o.name='Royal spire tower';o.data.materials.append(M['stone']);o['collision']=True;move(o,'Architecture')
    bpy.ops.mesh.primitive_cone_add(vertices=12,radius1=2.4,radius2=0,depth=5,location=(x,25,z+13.5));o=bpy.context.object;o.name='Turquoise castle spire';o.data.materials.append(M['blue_roof']);move(o,'Architecture')
    box('Tower gold finial',(x,25,z+16.3),(.13,.13,.7),'gold','Props')
    for i in range(6):box('Tower climbing steps',(x,21-i*.5,z+.25*(6-i)),(2,.5,.5*(6-i)),'stone')
for x,y in [(24,45),(30,45)]:
    z=height(x,y);box('Quarry forge',(x,y,z+.65),(1.4,1.3,1.3),'basalt','Props');box('Anvil',(x,y,z+1.4),(1,.45,.25),'iron','Props')
for x,y in [(-26,-30),(-19,-30),(-16,-28)]:
    z=height(x,y);box('Ranch hay bale',(x,y,z+.45),(1.8,1,.9),'pages','Props');vessel('Milk churn',x+1.2,y,z,.25,.8,'iron')
for x in [41,49]:
    z=height(x,-24)
    for y in [-27,-21]:box('Desert colonnade',(x,y,z+2),(.65,.65,4),'sand')
    box('Caravan shade',(x,-24,z+4.15),(.85,7,.24),'sand')
for x,y in [(-46,25),(-42,26),(-45,20)]:
    z=height(x,y);rock(x,y,.5,'moss');vessel('Forest rune lantern',x,y,z+.5,.25,.55,'gold')
for x in [-5,5]:
    for y in [-48,-39]:
        box('Ruins tower',(x,y,height(x,y)+3),(2.2,2.2,6),'stone')
        for ox in [-.7,.7]:box('Battlement',(x+ox,y,height(x,y)+6.4),(.6,2.2,.8),'stone')
for x in [-7,-3,3,7]:box('Grave marker',(-43+x,-31,height(-43+x,-31)+.6),(.7,.35,1.2),'stone','Props')
# Lake inside the coastal inlet, shore arena stays dry.
disc('Lake Lantern pool',49,13,4.8,'water',-2.27)
box('Lake jetty',(44,12,height(38,8)),(12,2.5,.3),'wood')
for x in range(40,49,2):box('Jetty support',(x,12,-.3),(.3,.3,3),'wood')
# Ranch fencing with large breaks for buggy traffic.
for x in [-38,-6]:
    for y in [-30,-27,-21,-18]:
        box('Ranch fence',(x,y,height(x,y)+.65),(.2,2.8,.2),'wood')
        box('Ranch fence post',(x,y-1.4,height(x,y)+.65),(.24,.24,1.3),'wood')
for i in range(220):
    x=random.uniform(-61,61);y=random.uniform(-61,61)
    if math.hypot(x,y)>62 or height(x,y)<0 or min(math.hypot(x-p[1],y-p[2]) for p in POIS)<15:continue
    # Keep a clearance strip around every radial road.
    if any(abs(x*p[2]-y*p[1])/max(1,math.hypot(p[1],p[2]))<2.8 and x*p[1]+y*p[2]>0 for p in POIS[1:]):continue
    if biome(x,y) in ['forest','grass','snow','moss']:tree(x,y,random.uniform(.65,1.1))
    else:rock(x,y,random.uniform(.7,1.8),biome(x,y))
marker('Lobby / safe storm fallback',0,6,height(0,6),'spawn')
marker('Lon Lon Buggy',-22,-24,height(-22,-24),'vehicle')
marker('Major boss safe center',0,-13,height(0,-13),'major_boss_candidate')
# Scale reference can be enabled to check interiors without polluting export.
marker('Link height reference 180 units',8,0,height(8,0),'scale_reference')
scene.world.use_nodes=True
bg=scene.world.node_tree.nodes.get('Background');bg.inputs['Color'].default_value=(.46,.57,.72,1);bg.inputs['Strength'].default_value=.45
bpy.ops.object.light_add(type='SUN',location=(0,0,70));sun=bpy.context.object;sun.rotation_euler=(.65,-.8,-.6);sun.data.energy=2.8;sun.data.color=(1,.76,.48);sun.data.angle=.12;move(sun,'Presentation')
import sys
sys.path.insert(0,os.path.dirname(__file__))
import preview_lighting
preview_lighting.apply(scene,buildings)
def camera(name,loc,target,ortho=None):
    bpy.ops.object.camera_add(location=loc);o=bpy.context.object;o.name=name;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();o.data.clip_end=600
    if ortho:o.data.type='ORTHO';o.data.ortho_scale=ortho
    move(o,'Presentation');return o
cam=camera('Overview',(115,-155,135),(0,0,0),190);top=camera('Top down',(0,0,180),(0,0,0),164);detail=camera('Market streets',(20,-25,15),(0,0,2));inside=camera('Furnished interior',(-9,-10,3),(-9,-5,1.5));inside.data.lens=20
scene.camera=cam;scene.render.engine='CYCLES';scene.cycles.samples=12;scene.cycles.use_denoising=True
scene.render.resolution_x=1280;scene.render.resolution_y=960;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX'
scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=.7
scene['map_name']='Hyrule Convergence';scene['game_units_per_meter']=100;scene['integration_status']='Generated game data in shared/convergence_data.h'
scene['design']='7 miniboss arenas, 9 POIs, two-exit interiors, dry center, radial and ring buggy routes'
for o in bpy.context.selected_objects:o.select_set(False)
for c in ['Terrain','Architecture','Foliage','Props','Water']:
    for o in COL[c].objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,'hyrule_convergence.glb'),use_selection=True,export_apply=True)
import importlib.util
spec=importlib.util.spec_from_file_location('export_map',os.path.join(os.path.dirname(__file__),'export_convergence.py'));exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
exporter.export(ROOT,OUT,COL,POIS,markers,buildings,height,biome,M,ground_colour)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'hyrule_convergence.blend'))
stats={'mesh_objects':sum(o.type=='MESH' for o in scene.objects),'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in scene.objects if o.type=='MESH'),'buildings':len(buildings),'loot_sites':len(loot)}
with open(os.path.join(OUT,'placement.json'),'w') as file:json.dump({'map':'Hyrule Convergence','seed':64,'coordinate_conversion':'Blender XYZ -> game (100X,100Z,-100Y)','stats':stats,'pois':[{'name':n,'blender_xy':[x,y],'boss_kind':b} for n,x,y,m,b in POIS],'buildings':buildings,'markers':markers},file,indent=2)
for name,c in [('overview',cam),('top_down',top),('market_detail',detail),('interior',inside)]:
    scene.camera=c;scene.render.filepath=os.path.join(OUT,name+'.png');bpy.ops.render.render(write_still=True)
scene.camera=cam
# Make source pleasant to open.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_distance=150;area.spaces.active.region_3d.view_location=(0,0,0)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'hyrule_convergence.blend'))
print('MAP COMPLETE',stats)
