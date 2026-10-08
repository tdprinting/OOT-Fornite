"""Layered Riftlands terrain; exact triangle interpolation for every export."""
import importlib.util
from pathlib import Path
import numpy as np
from layout import *
math_tau=2*np.pi

# Share deterministic noise and triangle interpolation, without copying the kit.
spec = importlib.util.spec_from_file_location('terrain_math', Path(__file__).parent.parent/'kingdom'/'terrain.py')
maths = importlib.util.module_from_spec(spec); spec.loader.exec_module(maths)
fbm, smoothstep, seg_dist = maths.fbm, maths.smoothstep, maths.seg_dist
grid_height, ramp, ridged = maths.grid_height, maths.ramp, maths.ridged

def road_dist(x,z):
    best = np.full(np.shape(x), 1e9)
    for road in ROADS:
        d,_ = seg_dist(x,z,[(a,b,0) for a,b in road]); best=np.minimum(best,d)
    return best

def region(x,z,circles,soft=600):
    v=np.zeros(np.shape(x))
    for a,b,r in circles: v=np.maximum(v,1-smoothstep(r-soft,r,np.hypot(x-a,z-b)))
    return v

def coast_distance(x,z):
    a=np.arctan2(z,x)
    cd=np.hypot(x,z*.94)-(5800+220*np.sin(5*a)+140*np.sin(9*a+.7))+100*fbm(x,z,850,3,8)
    for mx,mz,peak,r,crater in MOUNTAINS:
        cd=np.minimum(cd,np.hypot(x-mx,z-mz)-r*.95)
    return np.maximum(cd,np.maximum(np.abs(x)-(HALF_X-350),np.abs(z)-(HALF_Z-350)))

def height(x,z):
    x,z=np.asarray(x,dtype=float),np.asarray(z,dtype=float)
    h=180+95*fbm(x,z,1800,4,31)+45*fbm(x,z,500,2,12)
    # Broad stepped shelves are eroded by small noise, not disconnected discs.
    shelf=1-smoothstep(2550,3300,np.hypot(x,z+700))
    h+=shelf*190
    for a,b,peak,r,crater in MOUNTAINS:
        d=np.hypot(x-a,z-b)/r
        cone=peak*np.clip(1-d,0,1)**1.15*(.9+.19*ridged(x,z,700,3,int(a)&255))
        if crater: cone=np.where(d<crater,np.minimum(cone,peak*.64-330*(1-(d/crater)**2)),cone)
        h=np.maximum(h,cone)
    for a,b,r,y,blend in PADS:
        w=1-smoothstep(r,r+blend,np.hypot(x-a,z-b)); h=h+(y-h)*w
    # Shape drivable ramps before painting roads. Several routes connect each shelf.
    for pts in [[(0,-1500,350),(0,-2200,430),(0,-2700,710),(0,-2970,820),(0,-3300,820)],
                [(0,-2350,340),(850,-2800,550),(800,-3400,820)],
                [(-1000,-3000,820),(-1700,-3000,500),(-2200,-2800,300)],
                [(-2700,-3000,240),(-3000,-3450,430),(-3250,-3750,640)],
                [(3000,-2300,230),(3450,-2800,400),(3850,-3100,550)]]:
        h=ramp(x,z,h,pts,210,230)
    lx,lz,rx,rz=LAKE
    d=np.hypot((x-lx)/rx,(z-lz)/rz)+.075*fbm(x,z,600,3,49)
    bed=-430+(h+430)*smoothstep(.93,1.08,d)
    h=np.minimum(h,bed)
    channel,_=seg_dist(x,z,[(-1750,3200,0),(-2050,2700,0),(-2450,2100,0)])
    h=np.minimum(h,-350+(h+350)*smoothstep(125,330,channel))
    for x0,x1,z0,z1,y in POOLS:
        # Meandering channel stays inside its physical box; dry irregular banks
        # cover the rectangle edges, so water rendering and swimming still agree.
        t=np.clip((z-z0)/(z1-z0),0,1)
        center=(x0+x1)/2+45*np.sin(t*np.pi)+24*np.sin(t*math_tau+y*.013)
        width=(x1-x0)*.27+26*np.sin(t*math_tau+.7)
        d=np.abs(x-center)
        bed=y-125+(h-y+125)*smoothstep(width*.55,width+100,d)
        inside=(x>=x0)&(x<=x1)&(z>=z0)&(z<=z1)
        h=np.where(inside,np.minimum(h,bed),h)
    ix,iz,ir=LAB_ISLAND
    d=np.hypot(x-ix,z-iz); h=np.where(d<ir+280,np.maximum(h,30-420*smoothstep(ir*.65,ir+280,d)),h)
    cd=coast_distance(x,z)
    h=h+(-260-h)*smoothstep(-450,0,cd)
    h=np.where(cd>0,-260+(SEA_FLOOR+260)*smoothstep(0,400,cd),h)
    return np.maximum(h,SEA_FLOOR)

def water_y(x,z):
    out=np.full(np.broadcast(x,z).shape,WATER_Y,dtype=float)
    for x0,x1,z0,z1,y in POOLS: out=np.where((x>=x0)&(x<=x1)&(z>=z0)&(z<=z1),y,out)
    return out

def slope_up(x,z,eps=40):
    a=(height(x+eps,z)-height(x-eps,z))/(2*eps)
    b=(height(x,z+eps)-height(x,z-eps))/(2*eps)
    return 1/np.sqrt(1+a*a+b*b)

def paint(x,z,h=None,up=None):
    x,z=np.asarray(x,dtype=float),np.asarray(z,dtype=float)
    if h is None: h=height(x,z)
    if up is None: up=slope_up(x,z)
    n=fbm(x,z,400,3,11)
    c=np.stack([121+n*35,164+n*32,75+n*22],axis=-1)
    def mix(v,col):
        nonlocal c
        v=np.clip(v,0,1)[...,None]; c=c*(1-v)+np.asarray(col)*v
    mix(region(x,z,DESERT,650),(220,177,102))
    mix(region(x,z,FOREST,600),(63,116,55))
    mix(region(x,z,[(-3900,3000,1400)],400)*.7,(176,167,69))
    mix(np.clip((.83-up)*5,0,1),(149,139,119))
    snow=region(x,z,[(-3550,-4800,2600)],800)*np.clip((h-430)/230,0,1)
    mix(snow,(229,236,244))
    ash=region(x,z,[(4550,-4500,2000)],550)*np.clip((h-600)/350,0,1)
    mix(ash,(104,75,64))
    mix(1-smoothstep(105,170,road_dist(x,z)),(179,146,94))
    shore=np.clip((water_y(x,z)+80-h)/160,0,1)
    mix(shore,(210,200,153))
    mix((h<water_y(x,z)-20)*.72,(58,144,143))
    cover=np.where(h<water_y(x,z),'0','3')
    return np.clip(c,0,255),cover

def vertex_grid():
    xs=np.linspace(-HALF_X,HALF_X,CELLS+1); zs=np.linspace(-HALF_Z,HALF_Z,CELLS+1)
    x,z=np.meshgrid(xs,zs)
    return np.rint(height(x,z)).astype(np.int16)
