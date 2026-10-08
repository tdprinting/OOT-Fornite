"""Building kit for Hyrule Kingdom: houses with real interiors (no loading zones), towers, walls, trees, rocks, furniture.

Sizes (game units): adult Link is about 56 tall. A storey is 270, walls 26 thick, doorways 160 wide (the camera needs room), a ramp
rises at most 0.7 per unit run (steeper floors make Link slide).
"""
import math
import numpy as np
from geom import World, rect
import terrain
from layout import WATER_Y

HGRID = None      # the 65 x 65 vertex heights (set by world.py before building)

def H(x, z):
    return float(terrain.grid_height(HGRID, np.array(float(x)), np.array(float(z))))

def ground_range(cx, cz, w, d, yaw=0.0, step=60):
    c, s = math.cos(yaw), math.sin(yaw)
    hs = []
    for lx in np.linspace(-w / 2, w / 2, max(2, int(w / step) + 1)):
        for lz in np.linspace(-d / 2, d / 2, max(2, int(d / step) + 1)):
            hs.append(H(cx + lx * c - lz * s, cz + lx * s + lz * c))
    return min(hs), max(hs)

class Frame:
    def __init__(self, x, z, yaw=0.0):
        self.x, self.z, self.yaw = x, z, yaw
        self.c, self.s = math.cos(yaw), math.sin(yaw)
    def p(self, lx, lz):
        return (self.x + lx * self.c - lz * self.s, self.z + lx * self.s + lz * self.c)
    def outline(self, pts):
        return [self.p(x, z) for x, z in pts]
    def dir(self, lx, lz):
        return (lx * self.c - lz * self.s, lx * self.s + lz * self.c)

def roofs_overlap(a, b):
    """Conservative OBB overlap, including eaves, in game x/z coordinates."""
    def corners(q):
        x,z,hw,hd,yaw=q
        return Frame(x,z,yaw).outline(rect(0,0,2*hw,2*hd))
    A, B = corners(a), corners(b)
    for polygon in (A,B):
        for i in range(4):
            x,z=polygon[i]; xx,zz=polygon[(i+1)%4]
            nx,nz=zz-z,x-xx
            aa=[nx*x+nz*z for x,z in A]; bb=[nx*x+nz*z for x,z in B]
            if max(aa)<=min(bb) or max(bb)<=min(aa): return False
    return True

def clear_site(w, x, z, W, D, yaw, name):
    # Keep the same houses in their neighbourhoods; only resolve intersecting eaves.
    managed = name.startswith(('Clock Town house','Kakariko house','Snowpeak cabin','Ordon house','Gerudo house')) or name in ('Ranch house','Lab tower','Kakariko lookout')
    if not managed: return x,z
    obstacles=[(bx,bz,hw+35,hd+35,ba) for bx,bz,hw,hd,fy,ba in w.buildings]
    if name.startswith('Ordon house'):
        obstacles.append((-5700,5900,260,260,math.atan2(-700,-750)))
    if name.startswith('Kakariko house'):
        obstacles.append((4130,-790,230,230,0))
        obstacles.append((5000,-500,260,260,math.atan2(950,-850)))
    for radius in range(0,1001,25):
        for k in range(16 if radius else 1):
            a=2*math.pi*k/16
            px,pz=x+radius*math.cos(a),z+radius*math.sin(a)
            candidate=(px,pz,W/2+50,D/2+50,yaw)
            if not any(roofs_overlap(candidate,b) for b in obstacles): return px,pz
    raise ValueError('No clear building site for '+name)

STOREY = 270
WALL = 26
DOOR = 160
DOOR_H = 215

# ---- furniture ------------------------------------------------------------------------------------------------------------------------
def table(w, f, lx, lz, y, ang=0.0, size=(150, 90)):
    x, z = f.p(lx, lz); yaw = f.yaw + ang
    w.box(x, y + 70, z, size[0], 10, size[1], 'planks', yaw, col='none')
    for sx in (-1, 1):
        for sz in (-1, 1):
            c, s = math.cos(yaw), math.sin(yaw)
            px, pz = sx * (size[0] / 2 - 12), sz * (size[1] / 2 - 12)
            w.box(x + px * c - pz * s, y, z + px * s + pz * c, 10, 70, 10, 'timber', yaw, col='none')
    w.props.append((x - size[0] / 2, y, z - size[1] / 2, x + size[0] / 2, y + 80, z + size[1] / 2, 'table'))

def stool(w, f, lx, lz, y):
    x, z = f.p(lx, lz)
    w.cylinder(x, z, 18, y, y + 42, 'planks', n=6, col='none')

def bed(w, f, lx, lz, y, ang=0.0):
    x, z = f.p(lx, lz); yaw = f.yaw + ang
    w.box(x, y, z, 110, 40, 190, 'timber', yaw, col='none')
    w.box(x, y + 40, z, 100, 16, 180, 'cloth', yaw, col='none')
    c, s = math.cos(yaw), math.sin(yaw)
    w.box(x + 0 * c - (-70) * s, y + 56, z + 0 * s + (-70) * c, 80, 12, 34, 'plaster', yaw, col='none')
    w.box(x + 0 * c - (-95) * s, y, z + 0 * s + (-95) * c, 110, 80, 12, 'timber', yaw, col='none')
    w.props.append((x - 95, y, z - 95, x + 95, y + 56, z + 95, 'bed'))

def shelf(w, f, lx, lz, y, ang=0.0):
    x, z = f.p(lx, lz); yaw = f.yaw + ang
    c, s = math.cos(yaw), math.sin(yaw)
    for k, hh in enumerate((10, 80, 150)):
        w.box(x, y + hh, z, 140, 8, 40, 'planks', yaw, col='none')
    for sx in (-66, 66):
        w.box(x + sx * c, y, z + sx * s, 8, 170, 40, 'timber', yaw, col='none')
    for k, sx in enumerate((-40, 0, 40)):
        w.cylinder(x + sx * c, z + sx * s, 13, y + 88, y + 118, 'ceramic' if False else 'dirt', n=6, col='none')
    w.props.append((x - 70, y, z - 70, x + 70, y + 170, z + 70, 'shelf'))

def barrel(w, x, z, y, prop=True):
    w.cylinder(x, z, 30, y, y + 80, 'planks', n=8, col='prop' if prop else 'none', top_mat='timber')
    for hh in (12, 66):
        w.cylinder(x, z, 31.5, y + hh, y + hh + 5, 'iron', n=8, col='none', top=False)

def crate(w, x, z, y, s=70, yaw=0.0):
    w.box(x, y, z, s, s, s, 'planks', yaw, col='prop', surf='wood')

def pot(w, x, z, y, r=22):
    w.cylinder(x, z, r * 0.7, y, y + r * 0.4, 'dirt', n=6, col='none', top=False, r1=r)
    w.cylinder(x, z, r, y + r * 0.4, y + r * 1.3, 'dirt', n=6, col='none', top=False, r1=r * 0.6)

def rug(w, f, lx, lz, y, sx=180, sz=120, mat='banner_red'):
    pts = [f.p(lx - sx / 2, lz - sz / 2), f.p(lx + sx / 2, lz - sz / 2), f.p(lx + sx / 2, lz + sz / 2), f.p(lx - sx / 2, lz + sz / 2)]
    w.face([(p[0], y + 1.5, p[1]) for p in pts][::-1], mat, uvs=[(0, 0), (1, 0), (1, 1), (0, 1)][::-1], out=(pts[0][0], y - 50, pts[0][1]))

# ---- houses ---------------------------------------------------------------------------------------------------------------------------
def wall_c(W, D, gap, t, side):
    """Plan outline (local) of the walls on one side (-1 left, +1 right) of a room with doorways front (+z) and back (-z)."""
    s = side; g = gap / 2
    pts = [(s * g, D / 2), (s * W / 2, D / 2), (s * W / 2, -D / 2), (s * g, -D / 2), (s * g, -D / 2 + t), (s * (W / 2 - t), -D / 2 + t),
           (s * (W / 2 - t), D / 2 - t), (s * g, D / 2 - t)]
    return pts if s < 0 else pts[::-1]

def wall_u(W, D, gap, t, gx=0.0):
    """Walls all round with one doorway in the front (+z) wall, centred at local x = gx."""
    g = gap / 2
    return [(gx - g, D / 2), (-W / 2, D / 2), (-W / 2, -D / 2), (W / 2, -D / 2), (W / 2, D / 2), (gx + g, D / 2), (gx + g, D / 2 - t),
            (W / 2 - t, D / 2 - t), (W / 2 - t, -D / 2 + t), (-W / 2 + t, -D / 2 + t), (-W / 2 + t, D / 2 - t), (gx - g, D / 2 - t)]

def slab_notched(W, D, nx0, nx1, nz0, nz1):
    """A floor over W x D with a rectangular hole (the stairwell) touching the left wall: x in [nx0, nx1], z in [nz0, nz1], nx0 = -W/2."""
    return [(-W / 2, -D / 2), (W / 2, -D / 2), (W / 2, D / 2), (-W / 2, D / 2), (-W / 2, nz1), (nx1, nz1), (nx1, nz0), (-W / 2, nz0)]

class HouseStyle:
    def __init__(self, wall='plaster', base='town_stone', trim='timber', roof='roof_red', roof_kind='gable', floor='planks', tint=(1, 1, 1),
                 window='window', climb=True, surf='stone'):
        self.wall, self.base, self.trim, self.roof, self.roof_kind, self.floor = wall, base, trim, roof, roof_kind, floor
        self.tint, self.window, self.climb, self.surf = tint, window, climb, surf

def house(w, x, z, W, D, yaw=0.0, storeys=1, style=None, back_door=True, furnish=True, loot=True, name='house', roof_rise=None,
          roof_deck=False, floor_y=None, footing=True):
    """An enterable building: stone footing, walls with doorways front and back, windows, upper floors reached by an inside ramp,
    and a roof (pitched, or flat with a parapet and a ramp up to it). Everything you can bump into is scene collision."""
    st = style or HouseStyle()
    x,z = clear_site(w,x,z,W,D,yaw,name)
    f = Frame(x, z, yaw)
    lo, hi = ground_range(x, z, W + 60, D + 60, yaw)
    y0 = (hi + 14) if floor_y is None else floor_y
    old_tint = w.tint; w.tint = st.tint
    # Footing: down into the ground so no gap shows on a slope
    w.prism(f.outline(rect(0, 0, W + 30, D + 30)), (lo - 80) if footing else (y0 - 20), y0, st.base, surf=st.surf, top_mat=st.floor)
    # Steps up to each doorway when the floor is high above the ground there (not for huts on a deck)
    for side in ((1, -1) if back_door else (1,)) if footing else ():
        dx, dz = f.p(0, side * (D / 2 + 15))
        gy = H(dx, dz)
        if y0 - gy > 25:
            # Find the landing on the actual baked terrain; keep the full ramp walkable.
            run = 70.0
            for _ in range(40):
                ex, ez = f.p(0, side * (D / 2 + 15 + run))
                end_y = H(ex, ez) - 3
                if abs(y0 - end_y) / run <= 0.55: break
                run += 40.0
            w.ramp(dx, dz, ex, ez, DOOR + 40, y0, end_y, st.base, base=min(end_y, gy) - 60)
            w.walkways.append((dx, dz, ex, ez, (DOOR + 40) / 2, y0, end_y))
    w.buildings.append((x, z, W / 2 + 15, D / 2 + 15, y0, yaw))
    for side in ((1, -1) if back_door else (1,)):
        px, pz = f.p(0, side * (D / 2 + 1))
        w.doors.append((px, pz, y0, name))
    w.interior.append(_aabb(f, W, D, y0 - 5, y0 + storeys * STOREY + 10))
    climb_edge = (1,) if st.climb else ()
    for k in range(storeys):
        yb = y0 + k * STOREY; yt = yb + STOREY
        if k == 0:
            if back_door:
                w.prism(f.outline(wall_c(W, D, DOOR, WALL, -1)), yb, yt, st.wall, surf=st.surf, climb=climb_edge)
                w.prism(f.outline(wall_c(W, D, DOOR, WALL, 1)), yb, yt, st.wall, surf=st.surf)
            else:
                w.prism(f.outline(wall_u(W, D, DOOR, WALL)), yb, yt, st.wall, surf=st.surf, climb=(1,) if st.climb else ())
            for side in ((1, -1) if back_door else (1,)):
                lx, lz = 0, side * (D / 2 - WALL / 2)
                # the lintel over the doorway (drawn; nobody reaches it) and the door frame
                px, pz = f.p(lx, lz)
                w.box(px, yb + DOOR_H, pz, DOOR + 4, STOREY - DOOR_H, WALL, st.wall, yaw, col='none')
                for sx in (-1, 1):
                    qx, qz = f.p(sx * (DOOR / 2 + 8), side * (D / 2 + 1))
                    w.box(qx, yb, qz, 18, DOOR_H + 10, WALL + 6, st.trim, yaw, col='none')
                qx, qz = f.p(0, side * (D / 2 + 1))
                w.box(qx, yb + DOOR_H, qz, DOOR + 34, 18, WALL + 6, st.trim, yaw, col='none')
        else:
            # Upper storey: one wide opening onto the front (jump down, or shoot out of it)
            w.prism(f.outline(wall_u(W, D, DOOR, WALL)), yb, yt, st.wall, surf=st.surf, climb=climb_edge)
            px, pz = f.p(0, D / 2 - WALL / 2)
            w.box(px, yb + DOOR_H, pz, DOOR + 4, STOREY - DOOR_H, WALL, st.wall, yaw, col='none')
        # windows on the sides and back, inside and out
        for sx in (-1, 1):
            for lz in np.linspace(-D / 2 + 110, D / 2 - 110, max(1, int((D - 140) / 260))):
                nx, nz = f.dir(sx, 0)
                px, pz = f.p(sx * W / 2, lz)
                w.decal(px, yb + 105, pz, 70, 90, nx, nz, st.window)
                px, pz = f.p(sx * (W / 2 - WALL), lz)
                w.decal(px, yb + 105, pz, 70, 90, -nx, -nz, st.window, off=1.5)
        # timber band between storeys
        w.prism(f.outline(rect(0, 0, W + 8, D + 8)), yt - 14, yt, st.trim, col='none', top=False)
    top = y0 + storeys * STOREY
    # Floors between storeys, with the stairwell hole along the left wall, and the ramps up
    ramp_w = 120
    for k in range(1, storeys + (1 if st.roof_kind == 'flat' else 0)):
        yb = y0 + k * STOREY
        run = max(STOREY / 0.68, 300)
        z0 = -D / 2 + WALL + 20; z1 = z0 + run
        last = k == storeys
        material = st.floor if not last else st.base
        surface = 'wood' if not last else 'stone'
        if z1 > D / 2 - WALL - 40:
            # Two flights around a corner, entirely inside the room, with a landing.
            rw=60.0 if min(W,D)<420 else 80.0; hw=W/2-WALL; hd=D/2-WALL
            lx=-hw+rw/2+5; bz=-hd+rw/2+5
            start_z=hd-20; middle_z=bz+rw/2; end_x=hw-rw-10
            first_run=start_z-middle_z; second_run=end_x-(lx+rw/2)
            slope=STOREY/(first_run+second_run)
            if slope>0.68: raise ValueError('Room too small for walkable stairs: '+name)
            mid_y=yb-STOREY+slope*first_run
            floor=[(-hw+rw+10,hd),(hw,hd),(hw,-hd),(end_x,-hd),(end_x,-hd+rw+10),(-hw+rw+10,-hd+rw+10)]
            w.prism(f.outline(floor),yb-20,yb,material,surf=surface,top_mat=st.floor)
            ax,az=f.p(lx,start_z); bx,bzz=f.p(lx,middle_z)
            w.ramp(ax,az,bx,bzz,rw,yb-STOREY,mid_y,st.floor,surf='wood',base=yb-STOREY)

            mx,mz=f.p(lx,bz); w.box(mx,mid_y-20,mz,rw,20,rw,st.floor,yaw,surf='wood')
            ax,az=f.p(lx+rw/2,bz); bx,bzz=f.p(end_x,bz)
            w.ramp(ax,az,bx,bzz,rw,mid_y,yb,st.floor,surf='wood',base=mid_y-20)

        else:
            hole = slab_notched(W - 2 * WALL, D - 2 * WALL, -(W / 2 - WALL), -(W / 2 - WALL) + ramp_w + 10, z0 - 10, z1 + 10)
            w.prism(f.outline(hole), yb - 20, yb, material, surf=surface, top_mat=st.floor)
            ax, az = f.p(-(W / 2 - WALL) + ramp_w / 2 + 5, z1)
            bx, bz = f.p(-(W / 2 - WALL) + ramp_w / 2 + 5, z0)
            w.ramp(ax, az, bx, bz, ramp_w, yb - STOREY, yb, st.floor, surf='wood', base=yb - STOREY)
            # Upper routes come from collision; these walkways must not replace the lower floor.
            px, pz = f.p(-(W / 2 - WALL) + ramp_w + 12, (z0 + z1) / 2)
            w.box(px, yb, pz, 8, 70, z1 - z0, st.trim, yaw, col='none')
    # The roof
    if st.roof_kind == 'gable':
        rise = roof_rise if roof_rise is not None else 0.5 * W
        w.gable(x, z, W, D, top, rise, st.roof, yaw, overhang=45, surf='wood', end_mat=st.wall)
    elif st.roof_kind == 'hip':
        rise = roof_rise if roof_rise is not None else 0.42 * min(W, D)
        w.hip(x, z, W, D, top, rise, st.roof, yaw, overhang=40, surf='wood')
    elif st.roof_kind == 'flat':
        # parapet ring with a gap at the back (to drop off), the roof floor itself was laid above (the last notched slab)
        g = 120
        par = [(-g / 2, -D / 2), (-W / 2, -D / 2), (-W / 2, D / 2), (W / 2, D / 2), (W / 2, -D / 2), (g / 2, -D / 2), (g / 2, -D / 2 + 22),
               (W / 2 - 22, -D / 2 + 22), (W / 2 - 22, D / 2 - 22), (-W / 2 + 22, D / 2 - 22), (-W / 2 + 22, -D / 2 + 22), (-g / 2, -D / 2 + 22)]
        w.prism(f.outline(par), top, top + 85, st.base, surf='stone')
        _lx, _lz = f.p(W / 4, D / 4); w.loot.append((_lx, top, _lz, name + ' roof'))
    old = w.tint
    if furnish:
        furnish_room(w, f, W, D, y0, name, ramp=storeys > 1 or st.roof_kind == 'flat')
    for k in range(1, storeys):
        furnish_upper(w, f, W, D, y0 + k * STOREY, name)
    if loot:
        lx, lz = f.p(W / 2 - WALL - 70, -D / 2 + WALL + 70)
        w.loot.append((lx, y0, lz, name))
        if storeys > 1:
            lx, lz = f.p(W / 2 - WALL - 70, D / 2 - WALL - 90)
            w.loot.append((lx, y0 + STOREY * (storeys - 1), lz, name + ' upstairs'))
    w.tint = old_tint
    return y0, top

def furnish_room(w, f, W, D, y, name, ramp=False):
    # Things stand against the right wall (the ramp, if any, runs up the left one); the aisle between the doorways stays clear.
    table(w, f, W / 2 - WALL - 95, D * 0.12, y, ang=math.pi / 2, size=(140, 80))
    stool(w, f, W / 2 - WALL - 165, D * 0.12 - 45, y); stool(w, f, W / 2 - WALL - 165, D * 0.12 + 45, y)
    if D > 420: shelf(w, f, W / 2 - WALL - 30, -D / 2 + WALL + 110, y, ang=math.pi / 2)
    if not ramp:
        if D >= 520 and W >= 480: bed(w, f, -(W / 2 - WALL - 65), -D / 2 + WALL + 115, y)
        px, pz = f.p(-(W / 2 - WALL - 45), D / 2 - WALL - 60)
        barrel(w, px, pz, y)
    rug(w, f, 0, 0, y, 150, min(260, D - 140), 'banner_red' if sum(map(ord, name)) % 2 else 'banner_blue')
    px, pz = f.p(W / 2 - WALL - 45, D / 2 - WALL - 45)
    pot(w, px, pz, y)

def furnish_upper(w, f, W, D, y, name):
    bed(w, f, W / 2 - WALL - 65, -D / 2 + WALL + 110, y)
    px, pz = f.p(W / 2 - WALL - 45, 60)
    barrel(w, px, pz, y)
    px, pz = f.p(W / 2 - WALL - 50, D / 2 - WALL - 120)
    crate(w, px, pz, y, 60, f.yaw)

def _aabb(f, W, D, y0, y1):
    pts = [f.p(x, z) for x, z in rect(0, 0, W, D)]
    return (min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts), y0, y1)

# ---- towers ---------------------------------------------------------------------------------------------------------------------------
def spiral_tower(w, x, z, size, height, yaw=0.0, wall='castle_stone', floor='cobble', roof='roof_blue', door=True, climb=True, name='tower',
                 cap='spire', clock=False, y0=None, far=True):
    """A square hollow tower: a doorway at the foot, ramps spiralling up the inside walls to a look-out floor at the top under a roof.
    One outside face is covered in ivy and can be climbed straight up."""
    x,z = clear_site(w,x,z,size,size,yaw,name)
    f = Frame(x, z, yaw)
    lo, hi = ground_range(x, z, size + 40, size + 40, yaw)
    y0 = hi + 12 if y0 is None else y0
    old_far = w.far; w.far = far
    w.prism(f.outline(rect(0, 0, size + 40, size + 40)), lo - 80, y0, 'town_stone', surf='stone', top_mat=floor)
    w.buildings.append((x, z, size / 2 + 20, size / 2 + 20, y0, yaw))
    t = 34
    inner = size - 2 * t
    rw = 120
    rise = min((inner - 2 * rw) * 0.66, 200)
    flights = max(1, int(round((height - 110) / rise)))
    top = y0 + flights * rise + 110      # the walls stand 110 over the look-out floor: a parapet to hide behind
    if door:
        w.prism(f.outline(wall_u(size, size, DOOR, t)), y0, top, wall, surf='stone', climb=(1,) if climb else ())
        px, pz = f.p(0, size / 2 - t / 2)
        w.box(px, y0 + DOOR_H, pz, DOOR + 4, top - y0 - DOOR_H, t, wall, yaw, col='none')   # the wall over the doorway (out of reach: drawn only)
    else:
        w.prism(f.outline(rect(0, 0, size, size)), y0, top, wall, surf='stone', climb=(0,) if climb else ())
    if climb:   # ivy on the climbable face (the left one, local -x)
        nx, nz = f.dir(-1, 0)
        px, pz = f.p(-size / 2, 0)
        tx, tz = -nz, nx
        for k in range(int((top - y0) / 200)):   # a ragged vine: narrower and wandering, so the stone shows round it
            wid = size * (0.38 + 0.14 * math.sin(k * 1.7 + x * 0.01))
            sh = size * 0.12 * math.sin(k * 0.9 + z * 0.01)
            w.decal(px + tx * sh, y0 + 20 + k * 200, pz + tz * sh, wid, 205, nx, nz, 'leaves_dark', off=3, uvs=[(0, 2), (1.5, 2), (1.5, 0), (0, 0)])
    w.interior.append(_aabb(f, size, size, y0 - 5, top))
    # Ramps up the inside, round the walls: each flight along one wall, with a landing in each corner
    y = y0
    corners = [(-inner / 2 + rw / 2, inner / 2 - rw / 2), (-inner / 2 + rw / 2, -inner / 2 + rw / 2), (inner / 2 - rw / 2, -inner / 2 + rw / 2),
               (inner / 2 - rw / 2, inner / 2 - rw / 2)]
    # start at the front left corner, beside the doorway
    ci = 0
    for _ in range(flights):
        a = corners[ci % 4]; b = corners[(ci + 1) % 4]
        # flight from just past corner a to just before corner b
        dx, dz = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dz); ux, uz = dx / L, dz / L
        s0 = (a[0] + ux * rw / 2, a[1] + uz * rw / 2); s1 = (b[0] - ux * rw / 2, b[1] - uz * rw / 2)
        P0 = f.p(*s0); P1 = f.p(*s1)
        w.ramp(P0[0], P0[1], P1[0], P1[1], rw, y, y + rise, floor, surf='stone', base=y - 20)
        y += rise
        # landing in corner b
        bx, bz = f.p(*b)
        w.box(bx, y - 20, bz, rw, 20, rw, floor, yaw, surf='stone')
        ci += 1
    # The look-out floor: covers the inside except the last landing's corner, which is where the ramp arrives
    yl = y
    last = corners[ci % 4]
    sx = 1 if last[0] > 0 else -1; sz = 1 if last[1] > 0 else -1
    # an L-shaped floor (everything but that corner's square)
    L_ = [(-inner / 2, -inner / 2), (inner / 2, -inner / 2), (inner / 2, inner / 2), (-inner / 2, inner / 2)]
    hole = (last[0] - sx * rw / 2, last[1] - sz * rw / 2)   # inner corner of the landing square
    # build the L outline by walking the square and cutting the corner (sx, sz)
    pts = []
    for (px, pz) in L_:
        if (px > 0) == (sx > 0) and (pz > 0) == (sz > 0):
            # replace this corner by three points
            pts += [(px, hole[1]), (hole[0], hole[1]), (hole[0], pz)] if (sx * sz > 0) else [(hole[0], pz), (hole[0], hole[1]), (px, hole[1])]
        else:
            pts.append((px, pz))
    w.prism(f.outline(pts), yl - 20, yl, floor, surf='stone')
    _lx, _lz = f.p(-sx * inner / 4, -sz * inner / 4); w.loot.append((_lx, yl, _lz, name + ' top'))
    # Battlements round the top floor's edge (on the walls)
    for (lx0, lz0, lx1, lz1) in [(-size / 2, -size / 2, size / 2, -size / 2), (size / 2, -size / 2, size / 2, size / 2),
                                 (size / 2, size / 2, -size / 2, size / 2), (-size / 2, size / 2, -size / 2, -size / 2)]:
        n = 4
        for k in range(n):
            if k % 2: continue
            ax, az = lx0 + (lx1 - lx0) * (k + .5) / n, lz0 + (lz1 - lz0) * (k + .5) / n
            px, pz = f.p(ax, az)
            w.box(px, top, pz, size / n * 0.9 if lz0 == lz1 else t + 6, 60, size / n * 0.9 if lx0 == lx1 else t + 6, wall, yaw, col='none')
    # clock faces under the roof
    if clock:
        for side in range(4):
            nx, nz = f.dir(*[(0, 1), (1, 0), (0, -1), (-1, 0)][side])
            px, pz = f.p(nx * 0 + [(0, size / 2), (size / 2, 0), (0, -size / 2), (-size / 2, 0)][side][0], [(0, size / 2), (size / 2, 0), (0, -size / 2), (-size / 2, 0)][side][1])
            w.decal(px, top - 300, pz, size * 0.72, size * 0.72, nx, nz, 'clock', off=4)
    # Roof on four corner posts, open sides so you can look (and shoot) out
    ry = top + 230
    for (cx_, cz_) in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        px, pz = f.p(cx_ * (size / 2 - t / 2), cz_ * (size / 2 - t / 2))
        w.box(px, top, pz, t, ry - top, t, wall, yaw, surf='stone')
    if cap == 'spire':
        w.hip(x, z, size, size, ry, size * 1.6, roof, yaw, overhang=30, surf='stone', col=None)
        w.prism(f.outline(rect(0, 0, size + 50, size + 50)), ry - 24, ry, wall, col='none')
    else:
        w.hip(x, z, size, size, ry, size * 0.55, roof, yaw, overhang=40, surf='wood')
    w.far = old_far
    return y0, top

def beam(w, a, b, width, height, mat, col='static', surf='stone', climb=False, block=False):
    """A straight sloped slab from a = (x, y, z) to b (its top surface runs from a to b), `height` thick: bridge decks and their
    parapets, walkways, fallen columns. Unlike a ramp it has nothing under it."""
    ax, ay, az = a; bx, by, bz = b
    dx, dz = bx - ax, bz - az; L = math.hypot(dx, dz); nx, nz = -dz / L * width / 2, dx / L * width / 2
    pts = [(ax + nx, ay, az + nz), (ax - nx, ay, az - nz), (bx - nx, by, bz - nz), (bx + nx, by, bz + nz)]
    pts += [(p[0], p[1] - height, p[2]) for p in pts]
    faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    w.solid(pts, faces, mat, col=col, surf=surf, climb=climb, block=block)
    if not block and col == 'static' and abs(by - ay) < L * 0.75: w.walkways.append((ax, az, bx, bz, width / 2, ay, by))

def ring_wall(w, cx, cz, r, t, y0, y1, mat, gaps=(), n=16, surf='stone', climb=False):
    """A round wall (n sides) with doorways: gaps = [(angle, width)] (angle 0 = +x, counter clockwise towards +z).
    Each stretch between gaps is one solid."""
    if not gaps:
        gaps = []
    if not gaps:
        a0, a1 = 0.0, 2 * math.pi
        spans = [(0.0, math.pi), (math.pi, 2 * math.pi)]
    else:
        g = sorted((a % (2 * math.pi), wd / r) for a, wd in gaps)
        spans = []
        for i, (a, wd) in enumerate(g):
            na, nwd = g[(i + 1) % len(g)]
            s0 = a + wd / 2; s1 = na - nwd / 2
            if s1 <= s0: s1 += 2 * math.pi
            spans.append((s0, s1))
    for s0, s1 in spans:
        k = max(2, int(round((s1 - s0) / (2 * math.pi) * n)) + 1)
        angs = np.linspace(s0, s1, k)
        outer = [(cx + (r + t / 2) * math.cos(a), cz + (r + t / 2) * math.sin(a)) for a in angs]
        inner = [(cx + (r - t / 2) * math.cos(a), cz + (r - t / 2) * math.sin(a)) for a in angs[::-1]]
        w.prism(outer + inner, y0, y1, mat, surf=surf, climb=tuple(range(k - 1)) if climb else ())
        for a in angs[:-1]:
            pass
    return spans

# ---- nature ---------------------------------------------------------------------------------------------------------------------------
def oak(w, x, z, s=1.0, seed=0, leaves='leaves', y=None):
    """Hyrule Field's broad oak: a thick trunk, two or three branches and a big lumpy crown (art: TP / OoT field oaks)."""
    y = H(x, z) - 10 if y is None else y
    rng = np.random.default_rng(seed)
    old = w.group; w.group = 'Foliage'
    r = 26 * s
    w.cylinder(x, z, r * 1.35, y, y + 40 * s, 'bark', n=7, col='none', top=False, r1=r)
    w.cylinder(x, z, r, y + 40 * s, y + 230 * s, 'bark', n=7, col='none', top=False, r1=r * 0.8)
    w.props.append((x - r, y, z - r, x + r, y + 230 * s, z + r, 'oak'))
    w.obstacles.append((x - r, x + r, z - r, z + r))
    for k in range(3):
        a = rng.uniform(0, 2 * math.pi) + k * 2.1
        bx, bz = x + math.cos(a) * 90 * s, z + math.sin(a) * 90 * s
        by = y + rng.uniform(210, 260) * s
        w.solid([(x - 10 * s, y + 170 * s, z), (x + 10 * s, y + 170 * s, z), (x, y + 200 * s, z + 10 * s), (bx, by, bz)], [(0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)], 'bark', col='none')
        w.sphere(bx, by + 40 * s, bz, 120 * s * rng.uniform(.85, 1.1), 85 * s, 120 * s * rng.uniform(.85, 1.1), leaves, rings=4, segs=7, seed=seed + k, jitter=0.12, squash_bottom=0.4)
    w.sphere(x, y + 330 * s, z, 175 * s, 120 * s, 175 * s, leaves, rings=5, segs=9, seed=seed + 7, jitter=0.12, squash_bottom=0.45)
    w.group = old

def pine(w, x, z, s=1.0, seed=0, snow=False, y=None):
    y = H(x, z) - 10 if y is None else y
    old = w.group; w.group = 'Foliage'
    r = 16 * s
    w.cylinder(x, z, r, y, y + 120 * s, 'bark', n=6, col='none', top=False)
    w.props.append((x - r, y, z - r, x + r, y + 150 * s, z + r, 'pine'))
    for k, (rr, y0, y1) in enumerate([(110, 70, 250), (85, 190, 360), (58, 300, 460)]):
        w.cone(x, z, rr * s, y + y0 * s, y + y1 * s, 'pine', n=8, bottom=True)
        if snow:
            w.cone(x, z, rr * 0.55 * s, y + (y0 + (y1 - y0) * 0.45) * s, y + y1 * s + 4, 'snow', n=8)
    w.group = old

def blossom_tree(w, x, z, s=1.0, seed=0):
    oak(w, x, z, s * 0.8, seed, leaves='blossom')

def bush(w, x, z, s=1.0, seed=0, mat='leaves_dark', y=None):
    y = H(x, z) - 8 if y is None else y
    old = w.group; w.group = 'Foliage'
    w.sphere(x, y + 40 * s, z, 70 * s, 55 * s, 70 * s, mat, rings=3, segs=7, seed=seed, jitter=0.15, squash_bottom=0.6)
    w.group = old

def rock(w, x, z, s=1.0, seed=0, mat='rock', y=None, solid=True):
    """A faceted boulder; big ones are scene collision (you can climb on them), the rest are bumped into."""
    y = H(x, z) - 25 * s if y is None else y
    rng = np.random.default_rng(seed)
    n = 7
    ring0 = []; ring1 = []
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-.2, .2)
        r0 = 100 * s * rng.uniform(.8, 1.15); r1 = 70 * s * rng.uniform(.7, 1.1)
        ring0.append((x + r0 * math.cos(a), y, z + r0 * math.sin(a)))
        ring1.append((x + r1 * math.cos(a + .3), y + 95 * s * rng.uniform(.8, 1.2), z + r1 * math.sin(a + .3)))
    topc = (x + rng.uniform(-20, 20) * s, y + 130 * s, z + rng.uniform(-20, 20) * s)
    pts = ring0 + ring1 + [topc]
    faces = []
    for k in range(n):
        faces.append((k, (k + 1) % n, n + (k + 1) % n, n + k))
        faces.append((n + k, n + (k + 1) % n, 2 * n))
    faces.append(tuple(range(n))[::-1])
    w.solid(pts, faces, mat, col='static' if solid else 'prop', surf='stone')

def fence(w, pts, h=90, mat='timber', y_off=0, gap_every=0):
    """A wooden rail fence along a polyline (drawn; low enough to hop, navigation goes round)."""
    for (ax, az), (bx, bz) in zip(pts[:-1], pts[1:]):
        L = math.hypot(bx - ax, bz - az); n = max(1, int(L / 160))
        for k in range(n + 1):
            px, pz = ax + (bx - ax) * k / n, az + (bz - az) * k / n
            gy = H(px, pz) + y_off
            w.box(px, gy - 10, pz, 16, h + 10, 16, mat, math.atan2(bz - az, bx - ax), col='none')
        for k in range(n):
            px0, pz0 = ax + (bx - ax) * k / n, az + (bz - az) * k / n
            px1, pz1 = ax + (bx - ax) * (k + 1) / n, az + (bz - az) * (k + 1) / n
            for hh in (h * 0.45, h * 0.85):
                y0_, y1_ = H(px0, pz0) + y_off + hh, H(px1, pz1) + y_off + hh
                nx, nz = -(pz1 - pz0), (px1 - px0); l = math.hypot(nx, nz); nx, nz = nx / l * 4, nz / l * 4
                w.face([(px0 + nx, y0_, pz0 + nz), (px1 + nx, y1_, pz1 + nz), (px1 + nx, y1_ + 12, pz1 + nz), (px0 + nx, y0_ + 12, pz0 + nz)], mat, double=True)
