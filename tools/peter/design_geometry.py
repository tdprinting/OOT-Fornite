"""Peter, the Quahog Brawler: authored Blender mesh. Cross-section modeling, metres, Z up, facing -Y.

Style: an adult OoT/MM townsperson silhouette (big head, broad hands, chunky shoes, flat shaded facets)
with Fortnite-clean colour blocks. Peter reads through five shapes: tall egg head with round glasses,
the famous double-lobed chin, pear belly in a white collared shirt, green trousers with black belt,
and short brown hair swept to one side. Painted detail is per-face vertex colour (see build_peter.py).
"""
from math import sin, cos, pi
import bpy
from mathutils import Vector

PARTS=['torso','left_leg','right_leg','left_arm','right_arm','head','jaw','left_brow','right_brow',
       'left_forearm','right_forearm','phone']
# Torso carries the head and both shoulders, so the waddle and laugh move the whole upper body.
PIVOTS=[(0,0,.80),(.115,0,.80),(-.115,0,.80),(.255,0,1.385),(-.255,0,1.385),(0,0,1.47),(0,.02,1.555),
        (.052,-.155,1.745),(-.052,-.155,1.745),(.305,0,1.145),(-.305,0,1.145),(-.322,-.012,.875)]
PARENTS=[-1,-1,-1,0,0,0,5,5,5,3,4,10]
HEAD_SCALE=1.22;HEAD_PARTS=(5,6,7,8)
# Right arm holding the phone to his ear (solved against the shoulder/elbow/hand pivots); build_peter.py animates to it.
PHONE_UPPER=(-1.6,-.2,-.6);PHONE_FORE=(-2.2,0,.6)
PIVOTS=[tuple(.0+(c-n)*HEAD_SCALE+n for c,n in zip(p,(0,0,1.45))) if i in HEAD_PARTS else p for i,p in enumerate(PIVOTS)]
PALETTE={'skin':'F3C9A4','skin_shadow':'D9A885','lip':'D49883','mouth':'5A2725','tongue':'B4555A',
 'shirt':'F1EFE7','shirt_shade':'CFCCC2','button':'BDB6A6','pants':'4F7B37','pants_shade':'3D6229',
 'belt':'26211F','buckle':'C9C6BC','shoe':'1F1C1C','sole':'121010','hair':'4A3322','hair_shade':'352418',
 'frame':'171717','eye':'FFFFFF','pupil':'141414','phone':'25272C','screen':'4F8DD0'}
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
    v=[p for r in rows for p in r];f=[]
    for j in range(len(rows)-1):f.append((2*j,2*j+1,2*j+3,2*j+2))
    ob=mesh(name,color,part,v,f)
    if thick:
        bpy.context.view_layer.objects.active=ob
        mod=ob.modifiers.new('edge','SOLIDIFY');mod.thickness=thick
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return ob

def oval(name,color,part,center,size,n=10,rings=5):
    x,y,z=center;rx,ry,rz=size
    return section(name,color,part,[(x,y,z+rz*cos(pi*j/rings),max(.001,rx*sin(pi*j/rings)),max(.001,ry*sin(pi*j/rings))) for j in reversed(range(rings+1))],n)

def bar(name,color,part,start,end,r1,r2,n=8):
    axis=(Vector(end)-Vector(start)).normalized()
    ref=Vector((0,1,0)) if abs(axis.y)<.9 else Vector((1,0,0))
    u=axis.cross(ref).normalized();w=axis.cross(u).normalized()
    v=[Vector(c)+(u*cos(2*pi*i/n)+w*sin(2*pi*i/n))*r for c,r in ((start,r1),(end,r2)) for i in range(n)]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,color,part,v,f)

def plaque(name,color,part,c,w,h,depth=.004,bevel=.16):
    x,y,z=c;a=w/2;b=h/2;q=min(a,b)*bevel
    xy=[(-a+q,-b),(a-q,-b),(a,-b+q),(a,b-q),(a-q,b),(-a+q,b),(-a,b-q),(-a,-b+q)]
    v=[(x+u,y+d,z+t) for d in (0,depth) for u,t in xy]
    f=[tuple(reversed(range(8))),tuple(range(8,16))]+[(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    return mesh(name,color,part,v,f)

def disc(name,color,part,c,r,depth=.003,n=12,rz=None):
    # A flat round face on the -Y plane (eyes, pupils, buttons).
    x,y,z=c;rz=rz or r
    v=[(x+r*cos(2*pi*i/n),y+d,z+rz*sin(2*pi*i/n)) for d in (0,depth) for i in range(n)]
    f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,color,part,v,f)

def ring(name,color,part,c,r_out,r_in,depth=.008,n=14,tilt=0.0):
    # Round glasses rim: an annulus with real thickness, slightly wrapped toward the temples.
    x,y,z=c;v=[]
    for d in (0,depth):
        for r in (r_out,r_in):
            for i in range(n):
                a=2*pi*i/n;px=r*cos(a)
                v.append((x+px,y+d+tilt*px,z+r*sin(a)))
    f=[]
    for i in range(n):
        j=(i+1)%n
        f.append((i,j,n+j,n+i))                 # front face
        f.append((2*n+n+i,2*n+n+j,2*n+j,2*n+i)) # back face
        f.append((i,2*n+i,2*n+j,j))             # outer wall
        f.append((n+j,3*n+j,3*n+i,n+i))         # inner wall
    return mesh(name,color,part,v,f)

def build():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    bpy.context.preferences.filepaths.save_version=0
    for key,hexcode in PALETTE.items():
        srgb=tuple(int(hexcode[i:i+2],16)/255 for i in (0,2,4));rgb=tuple(linear(c) for c in srgb)
        mat=bpy.data.materials.new(key);mat.diffuse_color=(*rgb,1);mat['game_rgb']=srgb
        M[key]=mat

    # ---- torso: pear belly in a white shirt that overhangs the belt, green seat below ----
    section('trouser_seat','pants',0,[(0,.01,.70,.215,.165),(0,.0,.78,.245,.19),(0,-.01,.86,.262,.215)],14)
    belly=[(0,-.025,.845,.258,.222),(0,-.05,.93,.292,.268),(0,-.062,1.04,.312,.292),(0,-.058,1.15,.304,.28),
           (0,-.035,1.26,.278,.235),(0,-.012,1.35,.25,.19),(0,0,1.42,.205,.145),(0,.005,1.465,.125,.10)]
    section('shirt_belly','shirt',0,belly,16)
    section('belt','belt',0,[(0,-.024,.835,.262,.226),(0,-.025,.875,.27,.236)],16)
    plaque('belt_buckle','buckle',0,(0,-.268,.856),.07,.05,.008,.2)
    plaque('buckle_hole','belt',0,(0,-.271,.856),.036,.022,.004,.2)
    # Shirt placket and buttons follow the belly's front curve.
    for z,y in [(.95,-.322),(1.06,-.356),(1.17,-.341),(1.28,-.272)]:
        disc('button','button',0,(0,y,z),.013,.004,8)
    ribbon('placket','shirt_shade',0,[[(-.008,-.315,.90),(.008,-.315,.90)],[(-.008,-.352,1.04),(.008,-.352,1.04)],
        [(-.008,-.345,1.17),(.008,-.345,1.17)],[(-.008,-.255,1.31),(.008,-.255,1.31)],[(-.008,-.16,1.40),(.008,-.16,1.40)]],.002)
    ribbon('breast_pocket','shirt_shade',0,[[(.07,-.262,1.255),(.15,-.24,1.255)],[(.07,-.236,1.32),(.15,-.215,1.32)]],.003)
    # Pointed collar flaps resting on the shoulders under the chin.
    for side in (1,-1):
        mesh('collar','shirt',0,[(side*.02,-.13,1.47),(side*.125,-.09,1.47),(side*.045,-.175,1.395),(side*.10,-.135,1.42)],[(0,1,3),(0,3,2)])
        mesh('collar_under','shirt_shade',0,[(side*.02,-.128,1.468),(side*.125,-.088,1.468),(side*.045,-.172,1.392),(side*.10,-.132,1.418)],[(0,3,1),(0,2,3)])

    for side,leg,arm,fore in ((1,1,3,9),(-1,2,4,10)):
        x=side*.118
        # Short sturdy legs, wide at the thigh, plain green trousers with a turned cuff.
        section('trouser_leg','pants',leg,[(x,0,.105,.082,.085),(x,0,.30,.09,.092),(x,0,.50,.104,.104),(x,-.01,.68,.12,.118),(side*.11,-.01,.80,.125,.13)],10)
        section('trouser_cuff','pants_shade',leg,[(x,0,.095,.088,.09),(x,0,.135,.09,.092)],10)
        # Big rounded black shoes, OoT-chunky.
        foot=[(1,0),(.92,.62),(.5,1),(-.5,1),(-.92,.62),(-1,0),(-.9,-.75),(-.5,-1),(.5,-1),(.9,-.75)]
        section('shoe_sole','sole',leg,[(x,-.05,.0,.092,.16),(x,-.05,.025,.098,.165)],shape=foot)
        section('shoe_upper','shoe',leg,[(x,-.05,.024,.096,.162),(x,-.06,.07,.092,.15),(x,-.03,.115,.075,.10),(x,-.005,.135,.07,.08)],shape=foot)
        oval('shoe_toe','shoe',leg,(x,-.15,.06),(.08,.07,.05),10,4)
        # White long sleeves: shoulder cap, elbow, buttoned cuff, broad cartoon hands.
        section('upper_sleeve','shirt',arm,[(side*.30,0,1.13,.07,.068),(side*.292,0,1.26,.078,.076),(side*.27,0,1.36,.088,.086),(side*.235,0,1.425,.075,.074)],10)
        oval('shoulder_cap','shirt',arm,(side*.25,0,1.385),(.09,.085,.07),10,4)
        section('fore_sleeve','shirt',fore,[(side*.318,-.008,.935,.058,.056),(side*.31,-.004,1.04,.064,.062),(side*.303,0,1.16,.069,.067)],10)
        section('sleeve_cuff','shirt_shade',fore,[(side*.32,-.008,.915,.061,.059),(side*.318,-.008,.955,.062,.06)],10)
        oval('palm','skin',fore,(side*.323,-.012,.862),(.042,.05,.06),10,5)
        section('fingers','skin',fore,[(side*.322,-.03,.79,.032,.03),(side*.324,-.03,.82,.042,.036),(side*.324,-.022,.86,.044,.04)],10)
        bar('thumb','skin',fore,(side*.30,-.045,.885),(side*.29,-.075,.84),.02,.016,8)
        ribbon('knuckle_crease','skin_shadow',fore,[[(side*.322-.004,-.065,.825),(side*.322+.004,-.065,.825)],[(side*.322-.004,-.066,.85),(side*.322+.004,-.066,.85)]],0)

    # ---- head: tall egg, glasses, bulb nose, double-lobed chin on a hinged jaw ----
    cranium=[(0,-.005,1.505,.128,.118),(0,-.01,1.56,.15,.14),(0,-.01,1.64,.158,.148),(0,-.008,1.72,.153,.142),
             (0,-.002,1.79,.135,.126),(0,0,1.838,.098,.094),(0,0,1.864,.04,.04)]
    section('cranium','skin',5,cranium,14)
    section('neck','skin_shadow',5,[(0,.01,1.44,.10,.09),(0,.01,1.52,.115,.10)],12)
    for side in (1,-1):
        oval('ear','skin',5,(side*.156,.008,1.635),(.02,.03,.042),8,4)
        oval('ear_inner','skin_shadow',5,(side*.162,.002,1.635),(.012,.018,.026),8,3)
    # Big white eyes behind round rims; black dot pupils looking slightly inward.
    for side,brow in ((1,7),(-1,8)):
        x=side*.052
        oval('eye_white','eye',5,(x,-.128,1.685),(.044,.026,.05),12,5)
        disc('pupil','pupil',5,(x-side*.006,-.157,1.678),.013,.003,10)
        ring('glasses_rim','frame',5,(x,-.162,1.685),.058,.05,.008,16,tilt=side*.18)
        bar('glasses_temple','frame',5,(side*.108,-.152,1.69),(side*.155,-.01,1.665),.005,.005,6)
        ribbon('brow','hair_shade',brow,[[(x-side*.035,-.163,1.745),(x+side*.03,-.161,1.752)],[(x-side*.035,-.164,1.756),(x+side*.03,-.162,1.763)]],.004)
    bar('glasses_bridge','frame',5,(-.006,-.168,1.69),(.006,-.168,1.69),.006,.006,6)
    oval('nose','skin',5,(0,-.162,1.615),(.03,.034,.03),10,5)
    oval('nose_tip','skin_shadow',5,(0,-.19,1.606),(.017,.012,.014),8,3)
    # Upper lip line and the wide mouth cavity, visible when the jaw drops.
    plaque('mouth_cavity','mouth',5,(0,-.136,1.566),.12,.02,.003,.45)
    plaque('tongue','tongue',5,(0,-.133,1.558),.06,.008,.002,.4)
    ribbon('upper_lip','lip',5,[[(-.072,-.147,1.578),(.072,-.147,1.578)],[(-.066,-.15,1.587),(.066,-.15,1.587)]],.003)
    jaw=section('lower_face','skin',6,[(0,-.02,1.462,.105,.096),(0,-.026,1.505,.128,.118),(0,-.022,1.55,.138,.124)],14)
    ribbon('lower_lip','lip',6,[[(-.07,-.148,1.552),(.07,-.148,1.552)],[(-.064,-.151,1.543),(.064,-.151,1.543)]],.003)
    # The two chin lobes with a soft cleft between them.
    for side in (1,-1):
        oval('chin_lobe','skin',6,(side*.031,-.122,1.494),(.043,.038,.042),10,5)
    ribbon('chin_cleft','skin_shadow',6,[[(-.003,-.168,1.515),(.003,-.168,1.515)],[(-.003,-.164,1.462),(.003,-.164,1.462)]],0)
    # Short brown hair: back/top cap with a raised front hairline and a side-swept fringe.
    hair=section('hair_cap','hair_shade',5,[(0,.006,1.63,.162,.152),(0,.004,1.72,.161,.15),(0,.003,1.80,.142,.133),(0,.002,1.85,.104,.10),(0,.002,1.876,.045,.045)],14)
    for vtx in hair.data.vertices:
        if vtx.co.z<1.75 and vtx.co.y<-.02:
            # Bring the lower ring up over the forehead so the face stays clear.
            vtx.co.z+=(.115 if vtx.co.z<1.67 else .03)*min(1,-vtx.co.y/.12)
    for x,w,z,col in [(-.07,.08,1.81,'hair'),(-.005,.085,1.818,'hair'),(.06,.075,1.81,'hair_shade')]:
        mesh('fringe_tuft',col,5,[(x-w*.5,-.10,1.845),(x+w*.5,-.11,1.842),(x+w*.6,-.125,z-.01),(x-w*.35,-.128,z+.004),(x+.02,-.14,z+.018)],[(0,1,4),(1,2,4),(2,3,4),(3,0,4)])
    for side in (1,-1):
        mesh('sideburn','hair',5,[(side*.15,-.03,1.70),(side*.152,.03,1.70),(side*.148,.02,1.62),(side*.146,-.025,1.635)],[(0,1,2),(0,2,3)])

    # ---- the phone: a chunky OoT-sized handset, parented to his right hand. It is authored where it sits
    # in the call pose (pressed to his cheek, ear to mouth) and moved back into the hand's bind space.
    from mathutils import Matrix, Euler
    def about(pivot,rot):
        return Matrix.Translation(pivot)@Euler(rot,'XYZ').to_matrix().to_4x4()@Matrix.Translation(-Vector(pivot))
    call=about(Vector(PIVOTS[4]),PHONE_UPPER)@about(Vector(PIVOTS[10]),PHONE_FORE)
    back=call.inverted()
    def phone_box(name,color,a,b,half_w,half_t,inset=0.0):
        a=Vector(a);b=Vector(b);axis=(b-a).normalized();side=axis.cross(Vector((1,0,0))).normalized();out=Vector((-1,0,0))
        v=[back@(c+side*sw*half_w+out*(st*half_t+inset)) for c in (a,b) for sw,st in ((1,1),(-1,1),(-1,-1),(1,-1))]
        f=[(3,2,1,0),(4,5,6,7)]+[(i,(i+1)%4,(i+1)%4+4,i+4) for i in range(4)]
        return mesh(name,color,11,[tuple(x) for x in v],f)
    top,bottom=(-.243,.004,1.715),(-.25,-.098,1.555)
    phone_box('phone_body','phone',top,bottom,.03,.014)
    t=Vector(top);bt=Vector(bottom)
    phone_box('phone_screen','screen',t+(bt-t)*.12,t+(bt-t)*.5,.022,.002,.014)
    phone_box('phone_keys','button',t+(bt-t)*.58,t+(bt-t)*.9,.02,.002,.014)
    phone_box('phone_antenna','phone',t+(t-bt)*.0+Vector((0,.012,0)),t+(t-bt)*.25+Vector((0,.012,0)),.005,.005)
    # OoT adults carry an oversized head; scale the whole head rig about the neck.
    neck=Vector((0,0,1.45))
    for ob in bpy.data.objects:
        if ob.type=='MESH' and ob['game_part'] in HEAD_PARTS:
            for vtx in ob.data.vertices:vtx.co=neck+(vtx.co-neck)*HEAD_SCALE
    bpy.context.view_layer.update()
    return PARTS, PIVOTS
