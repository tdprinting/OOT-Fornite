"""Rough concept blockout of the Moonfall Bus (not the game model). Renders a contact sheet with Cycles CPU."""
import bpy, math, sys, os
from mathutils import Vector
out = sys.argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

def mat(name, rgb, emit=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = 0.9
    if emit:
        b.inputs["Emission Color"].default_value = (*rgb, 1); b.inputs["Emission Strength"].default_value = emit
    return m
def hexc(h): return tuple(int(h[i:i+2],16)/255 for i in (1,3,5))
M = dict(plank=mat("plank",hexc("#B98C4D")), dark=mat("dark",hexc("#4B2F17")), gold=mat("gold",hexc("#D9B52A")),
         red=mat("red",hexc("#AD3725")), blue=mat("blue",hexc("#2E4A86")), moon=mat("moon",hexc("#EAD68F")),
         crater=mat("crater",hexc("#9A917A")), rope=mat("rope",hexc("#C7A96A")), iron=mat("iron",hexc("#5A5D63")),
         leaf=mat("leaf",hexc("#4F8A3B")), lantern=mat("lantern",hexc("#FFD27A"),6.0), ember=mat("ember",hexc("#FF8A2A"),8.0),
         stone=mat("stone",hexc("#7A7368")), eye=mat("eye",hexc("#AD3725"),2.0), teeth=mat("teeth",hexc("#FFF6DC")),
         goron=mat("goron",hexc("#B88A5A")))
def add(obj, m, loc=(0,0,0), scale=(1,1,1), rot=(0,0,0), name=None):
    obj.location = loc; obj.scale = scale; obj.rotation_euler = rot
    obj.data.materials.append(m)
    if name: obj.name = name
    return obj
def box(m, loc, size, rot=(0,0,0), name=None):
    bpy.ops.mesh.primitive_cube_add(size=1); return add(bpy.context.object, m, loc, size, rot, name)
def cyl(m, loc, r, h, rot=(0,0,0), v=12, name=None):
    bpy.ops.mesh.primitive_cylinder_add(vertices=v, radius=r, depth=h); return add(bpy.context.object, m, loc, (1,1,1), rot, name)
def sph(m, loc, r, seg=24, ring=12, name=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=ring, radius=r); o = add(bpy.context.object, m, loc, (1,1,1), (0,0,0), name)
    bpy.ops.object.shade_flat(); return o

# Units: metres (1 m = 100 game units). Bus faces -Y. Body 9 x 3 x 2.6.
L, W, H = 9.0, 3.0, 2.6
box(M["plank"], (0,0,0.2), (L, W, 0.4), name="BusBody")               # floor
for s in (-1,1):                                                        # waist-high side walls with gold rail
    box(M["plank"], (s*(W/2-0.08), 0, 0.85), (0.16, L, 1.1)); box(M["gold"], (s*(W/2-0.08), 0, 1.42), (0.2, L, 0.06))
    box(M["blue"], (s*(W/2-0.02), 0, 0.9), (0.06, 1.4, 0.8)); box(M["red"], (s*(W/2+0.0), 0, 0.9), (0.08, 0.5, 0.5), rot=(0,0,math.pi/4))
    for i in range(3):                                                  # roof posts
        box(M["dark"], (s*(W/2-0.1), -L/2+0.4+i*(L-0.8)/2, 2.0), (0.14,0.14,1.3))
box(M["plank"], (0,0,2.72), (L+0.4, W+0.4, 0.14), name="Roof"); box(M["gold"], (0,0,2.85), (L+0.3, W+0.3, 0.06))
box(M["plank"], (0, L/2+0.45, 0.2), (W, 0.9, 0.4)); box(M["gold"], (0, L/2+0.9, 0.8), (W, 0.06, 1.2))   # rear platform and rail
for r,x in ((0,-0.8),(1,0.8)):                                          # two rows of 16 benches
    for i in range(16):
        y = -L/2+0.5+i*(L-1.0)/15
        box(M["red"], (x, y, 0.62), (1.0, 0.4, 0.12)); box(M["dark"], (x, y+0.18, 0.95), (1.0, 0.08, 0.6))
# pilot cab, tiller, Goron
box(M["dark"], (0,-L/2-0.5,0.2), (W*0.8, 1.0, 0.4)); cyl(M["iron"], (0,-L/2-0.9,0.9), 0.04, 1.4, rot=(0,math.pi/2,0))
sph(M["goron"], (0,-L/2-0.2,1.1), 0.5, 12, 8, name="Pilot"); sph(M["goron"], (0,-L/2-0.2,1.75), 0.3, 12, 8)
# firebox, chimney, embers
box(M["stone"], (0, 1.5, 0.9), (1.4, 1.4, 1.4), name="Firebox"); box(M["ember"], (0, 0.78, 0.8), (0.6, 0.05, 0.5))
cyl(M["iron"], (0, 1.5, 2.2), 0.18, 1.4, name="Chimney")
# wheels tucked up
for x in (-1,1):
    for y in (-2.8, 2.8): cyl(M["dark"], (x*(W/2+0.15), y, 0.25), 0.55, 0.2, rot=(0,math.pi/2,0), v=10)
# propeller: four Deku leaves at the rear
cyl(M["dark"], (0, L/2+1.2, 1.5), 0.15, 0.5, rot=(math.pi/2,0,0), name="Propeller")
for i in range(4):
    a = i*math.pi/2 + 0.4
    box(M["leaf"], (math.cos(a)*1.1, L/2+1.3, 1.5+math.sin(a)*1.1), (0.5, 0.06, 2.0), rot=(0, a, 0))
# banner
cyl(M["dark"], (1.2, L/2+0.9, 2.2), 0.03, 2.6); box(M["red"], (1.2, L/2+1.9, 3.3), (0.03, 1.8, 0.5)); box(M["gold"], (1.2, L/2+2.6, 3.3), (0.03, 0.4, 0.3))
# lanterns
for i in range(3):
    for s in (-1,1):
        o = sph(M["lantern"], (s*(W/2+0.35), -L/2+1.0+i*(L-2)/2, 2.45), 0.22, 8, 6); o.scale=(1,1,1.3)
# basket ring, ropes, balloon (the Moon)
R = 4.4; cz = 3.0 + R + 1.4
cyl(M["dark"], (0,0,4.0), 1.1, 0.2, v=16, name="BasketRing"); cyl(M["dark"], (0,0,4.0), 0.95, 0.22, v=16)
for sx in (-1,1):
    for sy in (-1,1):
        p0 = Vector((sx*(W/2-0.2), sy*(L/2-0.6), 2.9)); p1 = Vector((sx*0.8, sy*0.8, 4.0))
        d = p1-p0; c = cyl(M["rope"], ((p0+p1)/2), 0.035, d.length, v=6); c.rotation_euler = d.to_track_quat('Z','Y').to_euler()
        sph(M["rope"], p1 + Vector((0,0,-0.4)), 0.25, 8, 6)  # sandbag
moon = sph(M["moon"], (0,0,cz), R, 24, 12, name="Balloon")
cyl(M["dark"], (0,0,cz-R+0.3), 1.0, 0.6, v=16)      # balloon mouth
# craters and the face (forward-down = -Y side, tilted down)
import random; random.seed(3)
def on_moon(lat, lon, r): 
    x = R*math.cos(lat)*math.sin(lon); y = -R*math.cos(lat)*math.cos(lon); z = cz + R*math.sin(lat)
    o = sph(M["crater"], (x,y,z), r, 10, 6); o.scale = (1,1,0.25); o.rotation_euler = Vector((x,y,z-cz)).to_track_quat('Z','Y').to_euler(); return o
for _ in range(14): on_moon(random.uniform(-1.2,1.2), random.uniform(0.9, 5.4), random.uniform(0.35,0.8))
for s in (-1,1):   # eyes
    o = sph(M["eye"], (s*1.7, -R*0.84, cz+0.5), 0.85, 12, 8); o.scale=(1,0.4,0.9)
    o = sph(M["moon"], (s*1.7, -R*0.93, cz+0.5), 0.36, 8, 6); o.data.materials[0] = M["dark"]
    o = sph(M["dark"], (s*1.7, -R*0.9, cz+1.5), 0.9, 12, 8); o.scale=(1.2,0.25,0.25); o.rotation_euler=(0.0,0,s*0.35)   # brows
o = sph(M["dark"], (0, -R*0.9, cz-0.4), 0.9, 12, 8); o.scale=(1,0.3,1.2)                                            # nose
o = sph(M["dark"], (0, -R*0.84, cz-1.6), 2.4, 16, 8); o.scale=(1,0.3,0.5)                                           # grin
for i in range(-3,4): box(M["teeth"], (i*0.6, -R*0.92, cz-1.6), (0.42, 0.3, 0.6))
# ground reference
bpy.ops.mesh.primitive_plane_add(size=60); g = bpy.context.object; g.location=(0,0,-0.1); g.data.materials.append(mat("grass",hexc("#6FA24A")))
# light and world
bpy.ops.object.light_add(type='SUN'); sun = bpy.context.object; sun.data.energy = 2.6; sun.rotation_euler = (0.9, 0.3, 0.8)
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.55,0.75,0.95,1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.0
sc.render.engine = 'CYCLES'; sc.cycles.samples = 48; sc.cycles.device='CPU'; sc.render.resolution_x = 960; sc.render.resolution_y = 720
sc.view_settings.view_transform = 'Standard'
bpy.ops.object.camera_add(); cam = bpy.context.object; sc.camera = cam; cam.data.lens = 35
target = Vector((0,0,7.5))
views = {"three-quarter": Vector((-24,-26,12)), "side": Vector((-34,0,8)), "front": Vector((0,-34,9)), "from-below": Vector((-10,-18,-1.5))}
for name, pos in views.items():
    cam.location = pos; cam.rotation_euler = (target-pos).to_track_quat('-Z','Y').to_euler()
    sc.render.filepath = os.path.join(out, f"blockout-{name}.png"); bpy.ops.render.render(write_still=True); print("wrote", sc.render.filepath)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "bus_blockout.blend"))
