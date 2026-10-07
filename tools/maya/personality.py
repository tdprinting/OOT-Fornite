"""Maya's hand-painted N64 palette and authored animation beats.
Uses the same quantized paint helpers and matte materials as Lilo/Avriella.
Angles below are model/world axes; converted to each joint's local space.
"""
import math, sys, os
import bpy
from mathutils import Euler, Vector, Quaternion, Matrix
sys.path.insert(0,os.path.join(os.path.dirname(__file__),'../lilo'))
import build_lilo as B
FACES=('smile','half','shut','giggle','oh','talk','focus','wink')
CLIPS=[(n,T,loop,None) for n,T,loop in [('idle',5.8,True),('walk',1.0,True),('run',.8,True),('wave',3.2,False),('tablet',6,True),('draw',6.4,True),('pizza',5.6,True),('scooter',3,True),('learn',6.2,True),('cheer',3.6,False),('talk',5.2,True),('giggle',3.8,True),('hop',1.4,False),('fidget',5.4,True),('point',2.6,False),('sit',6.4,True),('sleep',6.0,True)]]
TAU=2*math.pi
mix=B.mix

def smooth(v):
    v=max(0,min(1,v));return v*v*(3-2*v)
def env(t,a,b,c,d):return smooth((t-a)/(b-a))*(1-smooth((t-c)/(d-c)))
def jump(t,a,b,h):
    u=(t-a)/(b-a)
    return h*4*u*(1-u) if 0<u<1 else 0

def paint_cloth(x,y):
    # 48-pixel torso island, 16-pixel plain sleeve island: no stretched stripe on the arms.
    base=(.95,.93,.88)
    if y<2 or y>29:base=mix(base,(.74,.76,.77),.17)
    if x<48:
        # Familiar small mouse/cartoon motifs, as on the reference shirt.
        for cx,cy,red in ((7,9,False),(23,9,True),(39,9,False),(14,23,True),(31,23,False)):
            dx=(x-cx)/.76;dy=(y-cy)/.76;ink=(.78,.25,.18) if red else (.24,.43,.71)
            if (dx/3.0)**2+((dy+2)/2.8)**2<1 or (dx+2.7)**2+(dy+5)**2<3 or (dx-2.7)**2+(dy+5)**2<3:base=ink
            if abs(dx)<1.8 and -1<dy<2:base=(.94,.89,.77)
            if abs(dx)<2 and 2<=dy<4:base=(.85,.26,.17)
            if 3<dy<5 and 1<abs(dx)<3.5:base=(.28,.37,.57)
            if dy==5 and 1<abs(dx)<4:base=(.82,.59,.26)
    elif y>26:base=mix(base,(.74,.76,.77),.19)
    return B.quant5(B.jitter(base,x,y,41,.014))

def ellipse(x,y,cx,cy,rx,ry):return ((x-cx)/rx)**2+((y-cy)/ry)**2

def paint_face(px,py,kind,skin,hair):
    x=px+.5;y=py+.5
    light=mix(skin,(.91,.72,.52),.18);shade=mix(skin,(.53,.31,.22),.26)
    c=mix(light,skin,max(0,min(1,(y-5)/26)))
    for cx in (6.4,25.6):
        k=ellipse(x,y,cx,20.5,4.2,2.8)
        if k<1:c=mix(c,(.91,.46,.39),.30*(1-k))
    for ex in (9.4,22.6):
        raised=-1.0 if kind=='oh' else -.4 if kind=='giggle' else 0
        if abs(x-ex)<3.4 and abs(y-(8.6+raised+((x-ex)/3.4)**2*.8))<.5:c=mix(c,hair,.73)
        closed=kind in ('shut','giggle') or (kind=='wink' and ex>16)
        if closed:
            arc=14.0+(1.1 if kind=='shut' else -.7)+((x-ex)/3.5)**2*( -1.3 if kind=='shut' else 1.6)
            if abs(x-ex)<3.5 and abs(y-arc)<.65:c=(.17,.10,.065)
        else:
            k=ellipse(x,y,ex,14,3.25,3.15)
            if k<1:
                c=(.22,.13,.075) if k>.77 else (.11,.065,.035)
                if ellipse(x,y,ex-1,12.9,.8,.85)<1:c=(1,.98,.90)
                if ellipse(x,y,ex+1.3,15.5,.45,.4)<1:c=(.71,.53,.34)
            if kind in ('half','focus') and k<1.25 and y<13.7:c=shade if y<13.1 else (.25,.14,.09)
            side=1 if ex>16 else -1
            if 2.5<(x-ex)*side<4 and abs(y-11.7)<.5:c=(.22,.12,.07)
    if ellipse(x,y,16,19.2,1.7,1.0)<1:c=shade
    if ellipse(x,y,15.5,18.5,.7,.45)<1:c=light
    if kind in ('talk','oh','giggle'):
        rx,ry=(5.2,3.4) if kind=='giggle' else (3.8,2.5) if kind=='talk' else (2.2,2.8)
        k=ellipse(x,y,16,24,rx,ry)
        if k<1:
            c=(.40,.14,.12)
            if y>25 and abs(x-16)<rx*.65:c=(.84,.45,.40)
            if kind!='oh' and y<23.2:c=(.97,.93,.83)
    else:
        curve=23.8+1.1*(1-((x-16)/5.0)**2)
        if abs(x-16)<5 and abs(y-curve)<.55:c=(.52,.25,.18)
        if kind=='smile' and abs(x-16)<4 and abs(y-(curve-.7))<.5:c=(.98,.94,.85)
    return B.quant5(B.jitter(c,px,py,23,.012))

def face_for(name,t,T):
    if name=='giggle':return 'giggle' if .55<t%T<2.8 else 'smile'
    if name=='talk':return ('smile','talk','oh','talk','smile')[int(t*6)%5]
    if name=='wave':return 'wink' if 1.15<t<1.35 else 'smile'
    if name=='cheer':return 'oh' if .4<t<.8 else 'giggle' if .8<t<2.6 else 'smile'
    if name=='hop':return 'oh' if .32<t<.75 else 'giggle' if .75<t<1.05 else 'smile'
    if name=='tablet':return 'oh' if 3.45<t<3.7 else 'smile' if 3.7<=t<5 else 'focus'
    if name in ('draw','learn'):return 'focus' if t<T*.68 else 'smile'
    if name=='pizza':return 'talk' if 2.2<t<3.1 and int(t*8)%2 else 'smile'
    if name=='point':return 'oh' if .7<t<1.7 else 'smile'
    if name=='sleep':return 'half' if 2.1<t<2.3 else 'shut'
    if name=='sit':return 'half' if 4.4<t<4.65 else 'giggle' if 2.5<t<3.2 else 'smile'
    return 'smile'

def animate(rig,name,t,T):
    ph=TAU*t/T
    def rot(n,x=0,y=0,z=0):
        pb=rig.pose.bones[n];r=rig.data.bones[n].matrix_local.to_3x3()
        pb.rotation_quaternion=(r.inverted()@Euler((x,y,z),'XYZ').to_matrix()@r).to_quaternion()
    def move(n,xyz):
        r=rig.data.bones[n].matrix_local.to_3x3();rig.pose.bones[n].location=r.inverted()@Vector(xyz)
    for pb in rig.pose.bones:pb.rotation_mode='QUATERNION';pb.rotation_quaternion=(1,0,0,0);pb.location=(0,0,0)
    for prop in ('tablet','draw','pizza','scooter','learn','pencil','tablet_cursor'):move(prop,(0,0,0 if (prop==name or prop=='pencil' and name=='draw') else -3))
    # Breathing, wrist relaxation and a trailing ponytail persist through every activity.
    rot('torso',x=.012*math.sin(ph));rot('head',z=.055*math.sin(ph+.2))
    rot('handL',x=.07,y=-.06);rot('handR',x=.05,y=.08)
    rot('ponytail',x=.10*math.sin(ph-.45),y=.04*math.sin(ph*2-.7));rot('ponytail2',x=.12*math.sin(ph-1.1),y=.05*math.sin(ph*2-1.3))
    if name in ('walk','run'):
        fast=name=='run';amp=.60 if fast else .40
        def smoother(v):return smooth(max(0,min(1,v)))
        legs={}
        for label,sign in (('L',1),('R',-1)):
            a=(ph+(0 if sign==1 else math.pi))%TAU          # leg angle sin(a): + is back, so a in (-pi/2, pi/2) is stance
            th=amp*math.sin(a)
            g=((a+math.pi/2)%TAU)/math.pi                     # 0..1 across stance, 1..2 across swing
            swing_flex=(1.45 if fast else .62)*max(0,-math.cos(a))**1.25
            load=(.30 if fast else .10)*max(0,math.sin(smoother(g/.5)*math.pi))*(1 if g<1 else 0)
            flex=swing_flex+load
            # Foot pitch (toes down +): heel strike, flat, heel-off roll, then toes lifted to clear the ground.
            if g<1:P=-.22*(1-smoother(g/.22))+(.62 if fast else .55)*smoother((g-.58)/.42)
            else:P=((.62 if fast else .55)+((.28 if fast else -.18)-(.62 if fast else .55))*smoother((g-1)/.5)) if g<1.5 else ((.28 if fast else -.18)+(-.05 if fast else -.06)*smoother((g-1.5)/.5))
            legs[label]=(th,flex,P-th-flex,math.sin(a))
        bob=(.030 if fast else .009)
        sway_x=0 if fast else .006*math.sin(ph);move('root',(sway_x,0,0))
        rot('pelvis',y=(.05 if fast else .045)*math.sin(ph),z=(.09 if fast else .07)*math.sin(ph))
        rot('torso',x=.15 if fast else .02,y=-.03*math.sin(ph),z=-(.11 if fast else .08)*math.sin(ph+.1))
        rot('head',x=-.10 if fast else -.03,y=.025*math.sin(ph),z=.07*math.sin(ph+.3)+.0*math.sin(ph*2))
        for label,sign in (('L',1),('R',-1)):
            th,flex,f,sn=legs[label]
            rot('leg'+label,x=th,y=sign*(.03 if fast else .02));rot('shin'+label,x=flex);rot('foot'+label,x=f)
            arm=math.sin(ph+(0 if sign==1 else math.pi)-.25)    # arm opposite to its leg, a touch behind it
            rot('arm'+label,x=-(.85 if fast else .38)*arm,y=-sign*(.07 if fast else .05),z=sign*.02)
            rot('forearm'+label,x=-((.90+.30*max(0,arm)) if fast else (.20+.28*max(0,arm))))
            rot('hand'+label,z=.07*math.sin(ph-.3)*sign,x=.06)
        lag=1.0 if fast else .6
        rot('ponytail',x=(.22 if fast else .12)+(.20 if fast else .10)*math.sin(ph*2-.9),y=.10*lag*math.sin(ph-.5))
        rot('ponytail2',x=(.34 if fast else .16)*math.sin(ph*2-1.7),y=.15*lag*math.sin(ph-1.1))
        # Plant the lowest foot on the floor, so the body bob comes from the legs; runs add a short flight at full stride.
        bpy.context.view_layer.update()
        low=1e9
        for label,sx in (('L',.078),('R',-.078)):
            pb=rig.pose.bones['foot'+label];m=pb.matrix@rig.data.bones[pb.name].matrix_local.inverted()
            for pt in ((sx,-.085,.017),(sx,.035,.025)):low=min(low,(m@Vector(pt)).z)
        flight=.032*max(0,-math.cos(2*ph)) if fast else 0
        move('root',(sway_x,0,.0-low+.017+flight))
    elif name=='sit':
        sway=math.sin(ph)
        move('root',(0,0,-.505+.004*math.sin(ph*3)))
        rot('pelvis',x=-.04)
        rot('torso',x=.10-.03*math.sin(ph*3),z=.07*sway)
        look=env(t,1.2,1.7,2.3,2.8)-env(t,3.6,4.1,4.9,5.4)
        rot('head',x=-.04-.05*env(t,1.4,1.8,2.2,2.6),z=.34*look,y=.03*sway)
        for label,sign in (('L',1),('R',-1)):
            kick=(.10*math.sin(ph*3+(0 if sign==1 else math.pi)))*env(t,.5,.9,5.0,5.8)
            rot('leg'+label,x=-1.50+kick*.6,y=-sign*.16)
            rot('shin'+label,x=.10+max(0,kick)*1.0)
            rot('foot'+label,x=.20-kick*.5)
            rot('arm'+label,x=-.30,y=-sign*.28,z=sign*.06);rot('forearm'+label,x=-.40);rot('hand'+label,x=.05)
        rot('ponytail',x=.18+.04*math.sin(ph*2),y=.05*math.sin(ph));rot('ponytail2',x=.08*math.sin(ph*2-1))
    elif name=='sleep':
        breathe=math.sin(ph*3)
        move('root',(0,0,-.495+.006*breathe))
        rot('pelvis',x=-.06)
        rot('torso',x=.45+.025*breathe,z=.03*math.sin(ph))
        rot('head',x=.70+.03*breathe,y=.10,z=.05*math.sin(ph))
        for label,sign in (('L',1),('R',-1)):
            rot('leg'+label,x=-1.95,y=-sign*.10)
            rot('shin'+label,x=1.95-.02*breathe)
            rot('foot'+label,x=.35)
            rot('arm'+label,x=-.80,y=sign*.10,z=-sign*.14);rot('forearm'+label,x=-.45,y=sign*.25);rot('hand'+label,x=-.05)
        rot('ponytail',x=.35+.03*breathe);rot('ponytail2',x=.12*breathe)
    elif name in ('idle','fidget'):
        shift=math.sin(ph)                  # slow weight shift from one hip to the other
        breathe=math.sin(ph*3)
        move('root',(.012*shift,0,-.006*(1-math.cos(ph*2))))
        rot('pelvis',y=.045*shift,z=.025*math.sin(ph-.5))
        rot('legL',x=.02+.05*max(0,shift),y=.02);rot('legR',x=.02+.05*max(0,-shift),y=-.02)
        rot('shinL',x=.05*max(0,shift));rot('shinR',x=.05*max(0,-shift))
        rot('footL',x=-.02-.05*max(0,shift));rot('footR',x=-.02-.05*max(0,-shift))
        look=env(t,.5,1.1,2.4,3.0)
        rot('torso',x=.015*breathe,y=-.035*shift,z=.035*math.sin(ph-.2))
        rot('head',x=.03*breathe-.01,y=-.04*shift,z=.28*look-.18*env(t,3.3,3.8,4.5,5.2))
        for label,sign in (('L',1),('R',-1)):
            rot('arm'+label,x=.03+.04*math.sin(ph*3+sign),y=sign*.07+.02*shift,z=sign*.02);rot('forearm'+label,x=-.12-.03*breathe)
        if name=='fidget':
            toe=env(t,.7,1.0,2.2,2.7)*(.5+.5*math.sin(t*10))
            rot('footR',x=-.13*toe);rot('shinR',x=.12*toe)
            for label,sign in (('L',1),('R',-1)):rot('arm'+label,x=.15,y=sign*.035);rot('forearm'+label,x=-.12)
    elif name=='wave':
        lift=env(t,.15,.65,2.3,3.1);w=env(t,.72,.92,2.1,2.45)
        rot('armL',x=-.15*lift,y=-2.28*lift,z=.10*lift)
        rot('forearmL',x=-.48*lift);rot('handL',y=.36*math.sin(t*13)*w,z=.20*math.sin(t*13+.5)*w)
        rot('torso',y=-.07*lift,z=.08*lift);rot('head',y=.08*lift,x=-.045*math.sin(t*6)*lift)
        rot('armR',x=.10*lift);move('root',(0,0,.013*jump(t,.6,1.2,1)))
    elif name in ('cheer','hop'):
        cheer=name=='cheer';lift=env(t,.1,.45,2.55 if cheer else .8,3.5 if cheer else 1.35)
        height=jump(t,.48,1.15,.10)+jump(t,1.55,2.2,.07) if cheer else jump(t,.25,.88,.14)
        squat=env(t,0,.16,.24,.42)+env(t,1.15,1.25,1.37,1.55) if cheer else env(t,0,.10,.20,.30)+env(t,.85,.95,1.05,1.3)
        move('root',(0,0,height-.03*squat))
        for label,sign in (('L',1),('R',-1)):
            rot('leg'+label,x=-.10*squat,y=sign*.025)
            rot('shin'+label,x=.23*squat);rot('foot'+label,x=-.12*squat)
            rot('arm'+label,x=-.18*lift,y=-sign*(2.52 if cheer else 1.65)*lift)
            rot('forearm'+label,x=-.55*lift);rot('hand'+label,z=sign*.18*math.sin(t*10)*lift)
        rot('torso',z=.06*math.sin(t*7)*lift);rot('head',x=-.07*lift)
        rot('ponytail',x=.28*math.sin(t*10-.6)*lift)
    elif name in ('talk','point','giggle'):
        gesture=env(t,.3,.65,1.45,1.85)
        if name=='point':
            rot('armR',x=-1.05*gesture,y=.18*gesture);rot('forearmR',x=-.12*gesture);rot('handR',x=-.15*gesture)
            rot('head',x=-.08*gesture,z=-.22*gesture);rot('torso',z=-.08*gesture)
        elif name=='giggle':
            laugh=env(t,.25,.6,2.7,3.5);shiver=math.sin(t*18)*laugh
            rot('torso',x=.10*laugh,z=.028*shiver);rot('head',x=.07*laugh+.018*shiver)
            rot('armL',x=-.85*laugh,y=.14*laugh);rot('forearmL',x=-1.2*laugh);rot('handL',x=-.12*laugh)
            rot('armR',x=-.3*laugh);rot('forearmR',x=-.5*laugh)
        else:
            g2=env(t,2.1,2.45,3.3,3.8)
            rot('armR',x=-.45*gesture,y=.15*gesture,z=-.18*gesture);rot('forearmR',x=-.7*gesture);rot('handR',z=-.3*gesture)
            rot('armL',x=-.25*g2,y=-.13*g2);rot('forearmL',x=-.6*g2)
            rot('head',x=.06*math.sin(t*5)*gesture,y=.07*g2,z=.07*math.sin(ph))
            rot('torso',z=-.07*gesture+.06*g2)
    else:
        # Relaxed elbows carry the prop; separate wrist/torso/head beats tell the activity.
        for label,sign in (('L',1),('R',-1)):
            rot('arm'+label,x=-.47,y=sign*.10);rot('forearm'+label,x=-.85);rot('hand'+label,x=-.05)
        rot('head',x=.17,z=.035*math.sin(ph))
        if name=='tablet':
            tap=env(t,.7,.85,1.4,1.55)+env(t,2.2,2.35,2.9,3.05)
            rot('handR',x=-.18*tap,z=.10*math.sin(t*15)*tap)
            rot('forearmR',x=-.80-.10*tap)
            delighted=env(t,3.55,3.9,4.65,5.2);rot('head',x=.51*(1-delighted)-.035*delighted,y=-.05*delighted)
            rot('torso',x=.05*(1-delighted));rot('curlR',x=.9)
            rot('torso',x=.018+delighted*.025);move('root',(0,0,.012*delighted*max(0,math.sin(t*8))))
        elif name=='draw':
            stroke=env(t,.3,.65,4.1,4.7)
            rot('forearmR',x=-.85+.08*math.sin(t*6)*stroke,z=.075*math.sin(t*9)*stroke)
            rot('handR',x=-.2,y=.10*math.sin(t*9)*stroke)
            admire=env(t,4.6,5.0,5.7,6.3);rot('head',x=.43*(1-admire),y=-.08*admire,z=.08*admire)
        elif name=='learn':
            rot('head',x=.34,z=.055*math.sin(ph))
            turn=env(t,3.1,3.35,3.7,4.1);rot('handR',z=.35*turn);rot('forearmR',x=-.85+.12*turn)
            curious=env(t,4.6,5.0,5.5,6.1);rot('head',x=.34*(1-curious),y=.10*curious)
        elif name=='pizza':
            bite=env(t,.8,1.5,2.6,3.3);rot('armL',x=-.52-.24*bite,y=.08);rot('forearmL',x=-.85-.62*bite)
            rot('handL',x=.10*bite);rot('head',x=.10*bite+.016*math.sin(t*15)*bite)
            pleased=env(t,3.5,3.85,4.65,5.4);rot('head',y=-.07*pleased,z=.08*pleased)
        elif name=='scooter':
            lean=.04*math.sin(ph);rot('torso',x=.05,y=lean);rot('head',x=-.04,y=-lean)
            for label in ('L','R'):move('leg'+label,(0,0,.045));rot('shin'+label,x=.06)
            rot('armL',x=-.45,y=.045);rot('armR',x=-.45,y=-.045)
            for label in ('L','R'):rot('forearm'+label,x=-.65)
            rot('scooter',z=-lean*.6)
            for wheel in ('wheelF','wheelR'):rot(wheel,x=-ph*2)
    bpy.context.view_layer.update()
    def palm(label):
        pb=rig.pose.bones['hand'+label]
        return pb.matrix@rig.data.bones[pb.name].matrix_local.inverted()@Vector(((1 if label=='L' else -1)*.206,-.006,.697))
    def put(prop,rest,center,R):
        # Unparented prop joint: exact rigid transform about its authored grip origin.
        basis=rig.data.bones[prop].matrix_local.to_3x3()
        rig.pose.bones[prop].rotation_quaternion=(basis.inverted()@R@basis).to_quaternion()
        move(prop,Vector(center)-R@Vector(rest))
    def grip(label,desired,orientation=None,pole_hint=None):
        # Analytic two-bone IK aligns the palm with the actual prop contact point.
        # Outward elbow poles keep a relaxed human bend rather than straight rods.
        upper=rig.pose.bones['arm'+label];lower=rig.pose.bones['forearm'+label]
        hand=rig.pose.bones['hand'+label];side=1 if label=='L' else -1
        S=rig.data.bones[upper.name].head_local.copy();E=rig.data.bones[lower.name].head_local.copy();W=rig.data.bones[hand.name].head_local.copy()
        palm_rest=Vector((side*.206,-.006,.697))
        # Keep the hand's authored orientation, solving for its actual palm, not its wrist.
        hand_delta=hand.matrix@rig.data.bones[hand.name].matrix_local.inverted()
        offset=(orientation if orientation is not None else hand_delta.to_3x3())@(palm_rest-W)
        target=Vector(desired)-offset
        parent_delta=upper.parent.matrix@rig.data.bones[upper.parent.name].matrix_local.inverted()
        shoulder=parent_delta@S;l1=(E-S).length;l2=(W-E).length
        direction=target-shoulder;d=min(l1+l2-.002,max(abs(l1-l2)+.002,direction.length));direction.normalize()
        along=(l1*l1-l2*l2+d*d)/(2*d);height=math.sqrt(max(0,l1*l1-along*along))
        pole=Vector(pole_hint if pole_hint is not None else (side,-.15,-.05));pole=(pole-direction*pole.dot(direction)).normalized()
        elbow=shoulder+direction*along+pole*height
        def stable_frame(direction):
            axis=direction.normalized();front=Vector((0,-1,0));front-=axis*front.dot(axis)
            if front.length<.01:front=Vector((1,0,0))-axis*axis.x
            front.normalize();side_axis=front.cross(axis).normalized()
            return Matrix(((side_axis.x,front.x,axis.x),(side_axis.y,front.y,axis.y),(side_axis.z,front.z,axis.z)))
        def align(rest,direction):return stable_frame(direction)@stable_frame(rest).transposed()
        wanted=align(E-S,elbow-shoulder)
        basis=rig.data.bones[upper.name].matrix_local.to_3x3()
        upper.rotation_quaternion=(basis.inverted()@parent_delta.to_3x3().inverted()@wanted@basis).to_quaternion()
        bpy.context.view_layer.update()
        arm_delta=upper.matrix@rig.data.bones[upper.name].matrix_local.inverted();actual_elbow=arm_delta@E
        wanted=align(W-E,target-actual_elbow)
        basis=rig.data.bones[lower.name].matrix_local.to_3x3()
        lower.rotation_quaternion=(basis.inverted()@arm_delta.to_3x3().inverted()@wanted@basis).to_quaternion()
        bpy.context.view_layer.update()
        if orientation is not None:
            delta=lower.matrix@rig.data.bones[lower.name].matrix_local.inverted();hb=rig.data.bones[hand.name].matrix_local.to_3x3()
            hand.rotation_quaternion=(hb.inverted()@delta.to_3x3().inverted()@orientation@hb).to_quaternion()
            bpy.context.view_layer.update()
        # Correct the tiny palm-offset change introduced by the solved wrist rotation.
        error=Vector(desired)-palm(label)
        if error.length>.003:
            target+=error
            wanted=align(W-E,target-actual_elbow)
            lower.rotation_quaternion=(basis.inverted()@arm_delta.to_3x3().inverted()@wanted@basis).to_quaternion()
            bpy.context.view_layer.update()
    if name=='wave':
        lift=env(t,.15,.65,2.3,3.1);flutter=env(t,.75,.95,2.1,2.45)
        rot('handL',y=.18*math.sin(t*11)*flutter)
        bpy.context.view_layer.update()
        target=Vector((.206,-.006,.697)).lerp(Vector((.24,-.075,1.23)),lift)
        target.x+=.095*math.sin(math.pi*lift)
        grip('L',target,pole_hint=(.35,-.12,-1.0))
    elif name in ('tablet','learn'):
        R=Euler((.93,.025*math.sin(ph),.025*math.sin(ph-.3)),'XYZ').to_matrix()@Euler((0,0,math.pi),'XYZ').to_matrix()
        rest=Vector((0,-.235,.85)) if name=='tablet' else Vector((0,-.23,.87))
        center=Vector((0,-.185,.89))
        put(name,rest,center,R)
        grip('L',center+R@Vector((-.127,.009,-.010)))
        if name=='tablet':
            tap1=env(t,.65,.85,1.00,1.18);tap2=env(t,1.42,1.63,1.80,1.98);drag=env(t,2.12,2.35,3.10,3.32)
            active=max(tap1,tap2,drag)
            pixel=Vector((-.06,-.016,-.02)) if t<1.3 else Vector((0,-.016,.005)) if t<2.1 else Vector((.06-.08*smooth((t-2.35)/.75),-.016,.03))
            screen_point=center+R@pixel
            fingertip_offset=R@Vector((.018,0,-.0475))
            target=(center+R@Vector((.127,.009,-.01))).lerp(screen_point-fingertip_offset,active)
            grip('R',target,R)
            if active>.97:put('tablet_cursor',(0,-.252,.85),screen_point,R)
        else:
            turn=env(t,3.1,3.35,3.7,4.1)
            right=Vector((.127,.009,-.010)).lerp(Vector((-.025,-.018,.015)),turn)
            grip('R',center+R@right)
    elif name=='draw':
        R=Euler((1.15,-.04,.035*math.sin(ph)),'XYZ').to_matrix()@Euler((0,0,math.pi),'XYZ').to_matrix();center=Vector((.005,-.185,.85))
        put('draw',(0,-.23,.84),center,R)
        grip('L',center+R@Vector((-.126,.006,0)))
        stroke=env(t,.3,.65,4.1,4.7)
        paper_point=center+R@Vector((-.025+.028*math.sin(t*6)*stroke,-.015,.014*math.sin(t*9)*stroke))
        axis=(R@Vector((0,-1,0))).normalized()
        pencilR=Vector((0,0,1)).rotation_difference(axis).to_matrix()
        put('pencil',(.12,-.23,.86),paper_point+axis*.075,pencilR)
        grip('R',paper_point+axis*.052)
    elif name=='pizza':
        bite=env(t,.8,1.5,2.6,3.3)
        hand_target=Vector((.13,-.19,.86)).lerp(Vector((.055,-.12,1.165)),bite)
        grip('L',hand_target)
        R=Euler((-.14*bite,.04,0),'XYZ').to_matrix()
        put('pizza',(.13,-.285,.845),palm('L')+Vector((0,-.012,.012)),R)
    elif name=='giggle':
        laugh=env(t,.25,.6,2.7,3.5)
        if laugh>.01:grip('L',palm('L').lerp(Vector((.06,-.12,1.145)),laugh))
    elif name=='scooter':
        R=rig.pose.bones['scooter'].matrix.to_3x3()@rig.data.bones['scooter'].matrix_local.to_3x3().inverted()
        for label,side in (('L',1),('R',-1)):grip(label,R@Vector((side*.155,-.21,.87)))
