"""Rebuild Maya's original low-poly mesh, pixel textures, rig and hobby animations.
Run: blender -b --python tools/maya/build_maya.py
Then: blender -b --python tools/maya/export_maya.py
Stylized from family references; no photograph is embedded in the game.
"""
import bpy, bmesh, math, os, sys
from mathutils import Euler, Matrix
sys.path.insert(0,os.path.dirname(__file__))
import personality as P
sys.path.insert(0,os.path.join(os.path.dirname(__file__),"../lilo"))
import build_lilo as BL
from mathutils import Vector
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'../..'))
OUT=os.path.join(ROOT,'assets/maya')
FPS=20
CLOTH_W=64; CLOTH_H=32
SKIN_W=64; SKIN_H=32
FACE_W=FACE_H=32
FACES=P.FACES
CLIPS=P.CLIPS
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
    n=m.node_tree.nodes.new('ShaderNodeTexImage'); n.image=im; n.interpolation='Linear'; n.extension='EXTEND'
    shader=m.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1.0
    shader.inputs['Specular IOR Level'].default_value=0
    m.node_tree.links.new(n.outputs['Color'],shader.inputs['Base Color'])
    return m
skin=(.58,.36,.23); hair=(.065,.055,.050)
def face(x,y,kind):
    return P.paint_face(x,y,kind,skin,hair)
def cloth_pixel(x,y):
    return P.paint_cloth(x,y)

parts=[]
def finish(o,bone,mat,region=None):
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    # Bake world coordinates so one joined mesh shares the armature's origin.
    mw=o.matrix_world.copy()
    for v in o.data.vertices: v.co=mw@v.co
    o.location=(0,0,0); o.rotation_euler=(0,0,0)
    o.data.materials.append(mat)
    for poly in o.data.polygons: poly.use_smooth = ('hair' not in o.name and 'fringe' not in o.name and 'bangs' not in o.name and 'pizza' not in o.name)
    o.vertex_groups.new(name=bone).add(list(range(len(o.data.vertices))),1,'REPLACE')
    if region:
        for uv in o.data.uv_layers.active.data:
            uv.uv.x=region[0]+uv.uv.x*region[2]; uv.uv.y=region[1]+uv.uv.y*region[3]
    parts.append(o); return o
def ball(name,pos,size,bone,mat,region=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=6 if name in ("fingers","thumb","nose","ear","pepperoni") else 10,ring_count=4 if name in ("fingers","thumb","nose","ear","pepperoni") else 6,location=pos)
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
    cloth=image('cloth',64,32,cloth_pixel)
    colors=[skin,hair,(.18,.27,.61),(.10,.11,.15),(.92,.65,.24),(.23,.60,.75),(.96,.94,.85),(.79,.20,.16)]
    atlas=image('skin',64,32,lambda x,y: BL.quant5(BL.jitter(colors[x//8],x,y,17,.012)))
    screen_images=[image('screen_'+n,P.SCREEN_W,P.SCREEN_H,lambda x,y,n=n:P.paint_screen(x,y,n)) for n in P.SCREENS]
    face_images=[image('face_'+n,32,32,lambda x,y,n=n:face(x,y,n)) for n in FACES]
    cm=material('Cloth',cloth);sm=material('Skin',atlas);fm=material('Face',face_images[0]);scm=material('Screen',screen_images[0])
    region=lambda n:(n/8+.005,.02,.115,.96)
    s=region(0);h=region(1);blue=region(2);dark=region(3);gold=region(4);cyan=region(5);paper=region(6);red=region(7)
    # Child proportions: approximately six head lengths, slender limbs and real joints.
    # Organic ring meshes give a continuous silhouette without cubes or stacked balls.
    def body(name,rings,bone,mat,reg=None,segments=10,ring_weights=None):
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
                vi=me.loops[li].vertex_index;uv.data[li].uv=(((vi%segments/segments+.5)%1 if name=='shirt' else vi%segments/segments),vi//segments/(len(rings)-1))
        o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);finish(o,bone,mat,reg)
        if ring_weights:
            o.vertex_groups.clear()
            for bn in set(k for w in ring_weights for k in w):o.vertex_groups.new(name=bn)
            for vi in range(len(verts)):
                for bn,w in ring_weights[vi//segments].items():o.vertex_groups[bn].add([vi],w,'REPLACE')
    def upper_body():
        # Shared boundary vertices stitch the sleeves to the torso; no shoulder balls,
        # overlapping tubes or gaps. All joints use at most two skinning influences.
        profiles=[(.755,.125,.090),(.83,.140,.095),(.95,.150,.100),(1.04,.150,.075),(1.09,.055,.047)]
        n=12;verts=[];polys=[];weights=[];uvs=[];mats=[]
        for z,rx,ry in profiles:
            for k in range(n):
                a=k*2*math.pi/n;verts.append(Vector((rx*math.sin(a),-ry*math.cos(a),z)));weights.append({'torso':1})
        def quad(idx,mat,uv):polys.append(idx);mats.append(mat);uvs.append(uv)
        for row in range(len(profiles)-1):
            for k in range(n):
                if row==2 and k in (2,3,8,9):continue
                nxt=(k+1)%n;idx=(row*n+k,row*n+nxt,(row+1)*n+nxt,(row+1)*n+k)
                us=[(k/n+.5)%1,(nxt/n+.5)%1]
                if abs(us[1]-us[0])>.5:us=[u+1 if u<.5 else u for u in us]
                quad(idx,0,[(us[0]*.75,row/4),(us[1]*.75,row/4),(us[1]*.75,(row+1)/4),(us[0]*.75,(row+1)/4)])
        for side,label in ((1,'L'),(-1,'R')):
            ks=[2,3,4] if side==1 else [10,9,8]
            last=[2*n+ks[0],2*n+ks[1],2*n+ks[2],3*n+ks[2],3*n+ks[1],3*n+ks[0]]
            for vi in last:weights[vi]={'torso':.72,'arm'+label:.28}
            centers=[(side*.176,0,.982),(side*.188,0,.958),(side*.197,0,.923),(side*.201,0,.884),(side*.204,0,.850),(side*.207,0,.780),(side*.206,0,.723)]
            radii=[.039,.034,.030,.029,.030,.025,.021]
            wts=[{'arm'+label:1},{'arm'+label:1},{'arm'+label:.86,'forearm'+label:.14},{'arm'+label:.5,'forearm'+label:.5},{'arm'+label:.14,'forearm'+label:.86},{'forearm'+label:1},{'forearm'+label:1}]
            for row,(c,r) in enumerate(zip(centers,radii)):
                axis=(Vector(centers[min(row+1,len(centers)-1)])-Vector((side*.145,0,1.04) if row==0 else centers[row-1])).normalized()
                u=Vector((0,-1,0));v=axis.cross(u).normalized()*side;ring=[]
                for k in range(6):
                    angle=math.pi/6+k*math.pi/3
                    ring.append(len(verts));verts.append(Vector(c)+u*(math.cos(angle)*r*.96)+v*(math.sin(angle)*r));weights.append(wts[row])
                for k in range(6):
                    j=(k+1)%6;idx=(last[k],last[j],ring[j],ring[k]);mat=0 if row<2 else 1
                    if mat==0:uv=[(.76+.23*k/6,1-row*.38),(.76+.23*(k+1)/6,1-row*.38),(.76+.23*(k+1)/6,1-(row+1)*.38),(.76+.23*k/6,1-(row+1)*.38)]
                    else:uv=[(s[0]+s[2]*k/6,.2),(s[0]+s[2]*(k+1)/6,.2),(s[0]+s[2]*(k+1)/6,.7),(s[0]+s[2]*k/6,.7)]
                    quad(idx,mat,uv)
                last=ring
        me=bpy.data.meshes.new('Maya seamless shirt shoulders arms');me.from_pydata(verts,[],polys);me.materials.append(cm);me.materials.append(sm);uv=me.uv_layers.new()
        for poly,mat,coords in zip(me.polygons,mats,uvs):
            poly.material_index=mat;poly.use_smooth=True
            for li,coord in zip(poly.loop_indices,coords):uv.data[li].uv=coord
        bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
        o=bpy.data.objects.new('Maya upper body',me);bpy.context.collection.objects.link(o)
        for bn in set(bn for w in weights for bn in w):o.vertex_groups.new(name=bn)
        for vi,w in enumerate(weights):
            for bn,weight in w.items():o.vertex_groups[bn].add([vi],weight,'REPLACE')
        parts.append(o)
    upper_body()
    def skirt():
        # One smooth two-tier skirt. Hem verts follow the nearer leg so it swings and flares as she steps.
        rings=[(.715,.118,.090),(.655,.140,.104),(.595,.158,.116),(.568,.170,.124),(.545,.185,.136),(.505,.196,.144),(.475,.198,.146)]
        follow=[0,.04,.10,.16,.26,.38,.44];n=14;verts=[];faces=[];wts=[]
        for (z,rx,ry),f in zip(rings,follow):
            for j in range(n):
                a=2*math.pi*j/n;x=rx*math.sin(a);verts.append((x,-ry*math.cos(a)+.004,z))
                side=1 if x>=0 else -1;k=f*min(1,abs(x)/(rx*.9))
                wts.append({'pelvis':1-k,'leg'+('L' if side==1 else 'R'):k} if k>.01 else {'pelvis':1})
        for r in range(len(rings)-1):
            for j in range(n):
                a=r*n+j;b=r*n+(j+1)%n;faces.append((a,b,b+n,a+n))
        faces.append(tuple(reversed(range(n))))
        me=bpy.data.meshes.new('skirt');me.from_pydata(verts,[],faces);uv=me.uv_layers.new()
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%n/n,vi//n/(len(rings)-1))
        o=bpy.data.objects.new('skirt',me);bpy.context.collection.objects.link(o);finish(o,'pelvis',sm,blue)
        o.vertex_groups.clear()
        for bn in ('pelvis','legL','legR'):o.vertex_groups.new(name=bn)
        for vi,w in enumerate(wts):
            for bn,wt in w.items():o.vertex_groups[bn].add([vi],wt,'REPLACE')
    skirt()
    body('neck',[(1.06,.035,.032,0,0),(1.145,.037,.035,0,0)],'torso',sm,s,8)
    profile=(-.97,-.78,-.50,-.14,.25,.60,.86,.97)
    body('head',[(1.235+.133*t,.107*math.sqrt(1-t*t)*(1+.065*math.exp(-((t+.35)/.3)**2)),.095*math.sqrt(1-t*t),0,-.012) for t in profile],'head',sm,s,12)
    ball('hair cap',(0,.014,1.292),(.116,.106,.098),'head',sm,h)
    # Ponytail: a tapered hanging tuft in two weighted segments (ponytail, ponytail2) so it whips instead of swinging stiff.
    body('ponytail',[(1.318,.032,.036,0,.108),(1.275,.048,.052,0,.128),(1.215,.055,.055,0,.150),(1.15,.046,.046,0,.162),(1.095,.030,.030,0,.162),(1.06,.010,.010,0,.160)],'ponytail',sm,h,8,
         [{'ponytail':1},{'ponytail':1},{'ponytail':.6,'ponytail2':.4},{'ponytail2':1},{'ponytail2':1},{'ponytail2':1}])
    for side in (-1,1):
        ball('ear',(side*.106,0,1.235),(.016,.018,.028),'head',sm,s)
        ball('side fringe',(side*.107,-.032,1.262),(.012,.030,.050),'head',sm,h)
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
    verts=[];quads=[]
    for row in range(4):
        lat=.60+row*.17
        for col in range(9):
            lon=-1.12+col*(2.24/8)
            lift=.004+.011*abs(math.sin(col*1.7))*(1 if row==0 else 0)   # a soft, uneven fringe edge
            verts.append(((.110+.010)*math.sin(lon)*math.cos(lat),-.012-(.097+.010)*math.cos(lon)*math.cos(lat)-lift,1.235+(.137+.010)*math.sin(lat)-(.014*abs(math.sin(col*1.3+1)) if row==0 else 0)))
    for row in range(3):
        for col in range(8):
            i=row*9+col;quads.append((i,i+1,i+10,i+9))
    me=bpy.data.meshes.new('bangs');me.from_pydata(verts,[],quads);uv=me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%9/8,vi//9/3)
    o=bpy.data.objects.new('bangs',me);bpy.context.collection.objects.link(o);finish(o,'head',sm,h)
    ball('nose',(0,-.106,1.22),(.013,.018,.019),'head',sm,s)
    def shoe(x,label):
        # A real little shoe: rounded toe box, ankle collar, heel, a pale sole and a white sock cuff above it.
        st=[(.052,.028,.030,.046),(.030,.034,.037,.050),(.000,.038,.034,.046),(-.030,.040,.028,.038),(-.060,.040,.024,.031),(-.085,.032,.019,.025),(-.100,.016,.012,.021)]
        n=10;verts=[];faces=[]
        for yy,rx,rz,cz in st:
            for k in range(n):
                a=2*math.pi*k/n;verts.append((x+rx*math.sin(a),yy,cz+rz*math.cos(a)*(1 if math.cos(a)>0 else .55)))
        for r in range(len(st)-1):
            for k in range(n):
                a=r*n+k;b=r*n+(k+1)%n;faces.append((a,b,b+n,a+n))
        faces.append(tuple(reversed(range(n))));faces.append(tuple((len(st)-1)*n+k for k in range(n)))
        me=bpy.data.meshes.new('shoe');me.from_pydata(verts,[],faces);uv=me.uv_layers.new()
        for poly in me.polygons:
            for li in poly.loop_indices:uv.data[li].uv=(.5,.5)
        o=bpy.data.objects.new('shoe',me);bpy.context.collection.objects.link(o);finish(o,'foot'+label,sm,blue)
        box('sole',(x,-.020,.010),(.086,.168,.020),'foot'+label,sm,paper)
        box('shoe toe cap',(x,-.082,.030),(.062,.040,.016),'foot'+label,sm,dark)
        body('sock',[(.135,.036,.036,x,0),(.100,.037,.037,x,0),(.062,.034,.036,x,0)],'foot'+label,sm,paper,10,[{'shin'+label:1},{'shin'+label:.5,'foot'+label:.5},{'foot'+label:1}])
    for side,label in ((-1,'R'),(1,'L')):
        x=side*.078
        body('leg',[(.08,.024,.027,x,0),(.20,.034,.038,x,-.001),(.315,.030,.032,x,-.002),(.345,.033,.037,x,-.007),(.378,.035,.037,x,-.003),(.49,.046,.046,x,0),(.66,.051,.047,x,0)],'leg'+label,sm,s,10,[{'shin'+label:1},{'shin'+label:1},{'shin'+label:.85,'leg'+label:.15},{'shin'+label:.5,'leg'+label:.5},{'shin'+label:.15,'leg'+label:.85},{'leg'+label:1},{'leg'+label:1}])
        shoe(x,label)
        x=side*.206
        ball('palm',(x,-.006,.700),(.029,.016,.028),'hand'+label,sm,s)
        # Thumb: three tapering segments angled down, forward and inward from the palm's heel.
        body('thumb',[(.704,.0115,.0105,x-side*.020,-.012),(.690,.0105,.0095,x-side*.027,-.019),(.675,.0090,.0085,x-side*.031,-.028),(.662,.0070,.0070,x-side*.033,-.035),(.655,.0030,.0030,x-side*.033,-.038)],'hand'+label,sm,s,6)
        for i in range(4):
            bone='indexR' if label=='R' and i==3 else 'curlR' if label=='R' else 'handL'
            j=3-i if label=='R' else i                 # 0 = index finger beside the thumb, 3 = little finger
            fx=x+(i-1.5)*.0125;L=(.050,.056,.050,.040)[j];r=(.0088,.0090,.0085,.0075)[j];c=(.012,.010,.012,.015)[j]
            z0=.682;ky=-.006
            rings=[(z0,r*1.05,r*.95,fx,ky),(z0-L*.38,r,r*.92,fx,ky-.004),(z0-L*.40,r*.93,r*.88,fx,ky-.005),(z0-L*.70,r*.85,r*.82,fx,ky-.012-c*.4),(z0-L*.72,r*.82,r*.80,fx,ky-.013-c*.5),(z0-L*.97,r*.62,r*.62,fx,ky-.022-c),(z0-L,r*.30,r*.30,fx,ky-.024-c)]
            body('finger',rings,bone,sm,s,6)
    box('tablet',(0,-.235,.81),(.25,.018,.17),'tablet',sm,dark)
    scr=box('pixel building screen',(0,-.247,.81),(.22,.004,.14),'tablet',scm)
    for poly in scr.data.polygons:   # the picture covers the whole front, the rest of the thin slab is plain
        front=poly.normal.y<-.5
        for li in poly.loop_indices:
            vx=scr.data.vertices[scr.data.loops[li].vertex_index].co
            scr.data.uv_layers.active.data[li].uv=((vx.x/.22+.5),(vx.z-.74)/.14) if front else (0,0)
    box('tablet touch highlight',(0,-.252,.85),(.014,.003,.014),'tablet_cursor',sm,paper)
    box('sketchbook',(0,-.23,.80),(.25,.025,.18),'draw',sm,paper)
    for i in range(3):box('drawing line',(-.06+i*.04,-.245,.80),(.013,.005,.10),'draw',sm,cyan)
    box('pencil',(.12,-.23,.86),(.015,.015,.15),'pencil',sm,gold)
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
    anchors={'root':(0,0,0),'pelvis':(0,0,.66),'torso':(0,0,.77),'head':(0,0,1.12),'ponytail':(0,.085,1.29),'ponytail2':(0,.15,1.19),'legL':(.078,0,.66),'legR':(-.078,0,.66),'shinL':(.078,0,.35),'shinR':(-.078,0,.35),'footL':(.078,0,.07),'footR':(-.078,0,.07),'armL':(.145,0,1.04),'armR':(-.145,0,1.04),'forearmL':(.201,0,.884),'forearmR':(-.201,0,.884),'handL':(.206,0,.723),'handR':(-.206,0,.723)}
    anchors.update({'indexR':(-.188,-.006,.685),'curlR':(-.206,-.006,.685)})
    anchors.update({n:(0,0,0) for n in ('tablet','draw','pizza','scooter','learn','pencil','tablet_cursor')})
    anchors.update({'wheelF':(0,-.21,.06),'wheelR':(0,.21,.06)})
    for n,p in anchors.items():
        b=ad.edit_bones.new(n);b.head=p;b.tail=Vector(p)+Vector((0,0,.10))
        parent=None
        if n=='pelvis':parent='root'
        elif n in ('torso','legL','legR'):parent='pelvis'
        elif n=='head' or n.startswith('arm'):parent='torso'
        elif n=='ponytail':parent='head'
        elif n=='ponytail2':parent='ponytail'
        elif n.startswith('shin'):parent='leg'+n[-1]
        elif n.startswith('foot'):parent='shin'+n[-1]
        elif n.startswith('forearm'):parent='arm'+n[-1]
        elif n.startswith('hand'):parent='forearm'+n[-1]
        elif n in ('indexR','curlR'):parent='handR'
        elif n.startswith('wheel'):parent='scooter'
        if parent:b.parent=ad.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT');mesh.parent=rig;mod=mesh.modifiers.new('Armature','ARMATURE');mod.object=rig
    rig.animation_data_create()
    for name,seconds,loops,_ in CLIPS:
        act=bpy.data.actions.new(name);act.use_fake_user=True;rig.animation_data.action=act
        expressions=[]
        for f in range(frames_of(seconds,loops)):
            t=f/FPS
            P.animate(rig,name,t,seconds)
            expressions.append(FACES.index(P.face_for(name,t,seconds)))
            for pb in rig.pose.bones:
                pb.keyframe_insert('rotation_quaternion',frame=f);pb.keyframe_insert('location',frame=f)
        act['maya_faces']=expressions
        act['maya_screens']=[P.SCREENS.index(P.screen_for(name,f/FPS)) for f in range(frames_of(seconds,loops))]
        act['maya_fps']=FPS
        track=rig.animation_data.nla_tracks.new();track.name=name;track.strips.new(name,0,act);track.mute=True
    rig.animation_data.action=bpy.data.actions['idle'];scene.frame_set(0)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'maya.blend'),compress=True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,'maya.glb'),export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True)
    if '--no-preview' not in sys.argv:
        # Review image is deliberately separate from the exportable character scene.
        bpy.ops.object.camera_add(location=(2,-3,1.8));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.67))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=1.65;scene.camera=cam
        bpy.ops.object.light_add(type='AREA',location=(1,-3,4));bpy.context.object.data.energy=500;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=5
        scene.world=bpy.data.worlds.new('Preview');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.22,.30,1)
        scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=650;scene.render.resolution_y=800;scene.render.resolution_percentage=100
        scene.render.filepath=os.path.join(OUT,'maya-preview.png');bpy.ops.render.render(write_still=True)
        scene.render.resolution_x=400;scene.render.resolution_y=500
        for clip in ('tablet','draw','pizza','scooter','learn','wave','cheer','giggle','hop','talk'):
            rig.animation_data.action=bpy.data.actions[clip];scene.frame_set(18)
            face_node=next(n for n in fm.node_tree.nodes if n.type=='TEX_IMAGE');face_node.image=face_images[FACES.index(P.face_for(clip,18/FPS,next(c[1] for c in CLIPS if c[0]==clip)))]
            scene.render.filepath=os.path.join(OUT,'maya-'+clip+'-preview.png');bpy.ops.render.render(write_still=True)
if __name__=='__main__':build()
