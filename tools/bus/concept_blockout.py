"""Concept render of the Moonfall Bus, second pass: a royal Hylian coach-bus in Fortnite's proportions under the Majora's Mask Moon.
Rough concept, not the game model. Usage: python blockout2.py <out dir>"""
import bpy, math, sys, os, random
from mathutils import Vector, Euler
out = sys.argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
rnd = random.Random(11)

def mat(name, rgb, rough=0.85, emit=0.0, metal=0.0, emit_rgb=None):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*(emit_rgb or rgb), 1); b.inputs["Emission Strength"].default_value = emit
    return m
def hexc(h): return tuple(int(h[i:i+2],16)/255 for i in (1,3,5))
M = dict(
    blue=mat("lacquer", hexc("#6A2A16"), 0.45), blue2=mat("lacquer dark", hexc("#4A1C0E"), 0.5), rune=mat("sheikah rune", hexc("#3FD6F2"), 0.3, emit=5.0, emit_rgb=hexc("#2FC8F0")),
    gold=mat("gold", hexc("#E0B93A"), 0.35, metal=0.6), brass=mat("brass", hexc("#B98B3C"), 0.4, metal=0.7),
    wood=mat("wood", hexc("#8E5A2B"), 0.9), dark=mat("dark wood", hexc("#4A2E18"), 0.9),
    glass=mat("glass", hexc("#2F6E8A"), 0.3), red=mat("crest red", hexc("#B23A2A"), 0.8), cream=mat("cream", hexc("#F2E6C8"), 0.8),
    iron=mat("iron", hexc("#55585E"), 0.6, metal=0.5), rope=mat("rope", hexc("#C9A86A"), 0.95), leaf=mat("leaf", hexc("#4E9A3C"), 0.8),
    leaf2=mat("leaf vein", hexc("#A9D36A"), 0.8), lantern=mat("lantern", hexc("#FFCF6E"), 0.9, emit=7.0),
    flame=mat("flame", hexc("#FFB23A"), 0.9, emit=12.0, emit_rgb=hexc("#FF7A1A")), ember=mat("ember", hexc("#FF6A1A"), 0.9, emit=6.0),
    moon=mat("moon rock", hexc("#7A7A72"), 0.95), moondark=mat("moon dark", hexc("#3E3C38"), 0.95),
    eye=mat("eye", hexc("#FFD23A"), 0.4, emit=9.0, emit_rgb=hexc("#FFC21A")), eyering=mat("eye ring", hexc("#C83A1E"), 0.7, emit=1.2),
    pupil=mat("pupil", hexc("#1A1206"), 0.5), teeth=mat("teeth", hexc("#E9E2CC"), 0.6), mouth=mat("mouth", hexc("#1C1410"), 0.9),
    goron=mat("goron", hexc("#C4915A"), 0.9), goronhair=mat("goron hair", hexc("#E8D8B0"), 0.9),
    water=mat("water", hexc("#1E5F8E"), 0.08, metal=0.1), island=mat("island", hexc("#5E9A4A"), 0.95), rock=mat("rock", hexc("#7C7B70"), 0.95))

def finish(o, m, loc=(0,0,0), scale=(1,1,1), rot=(0,0,0), name=None, smooth=False):
    o.location = loc; o.scale = scale; o.rotation_euler = rot
    o.data.materials.append(m)
    if name: o.name = name
    if smooth: bpy.ops.object.shade_smooth()
    else: bpy.ops.object.shade_flat()
    return o
def box(m, loc, size, rot=(0,0,0), name=None):
    bpy.ops.mesh.primitive_cube_add(size=1); return finish(bpy.context.object, m, loc, size, rot, name)
def cyl(m, loc, r, h, rot=(0,0,0), v=16, name=None, smooth=False):
    bpy.ops.mesh.primitive_cylinder_add(vertices=v, radius=r, depth=h); return finish(bpy.context.object, m, loc, (1,1,1), rot, name, smooth)
def sph(m, loc, r, seg=24, ring=12, name=None, smooth=True):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=ring, radius=r); return finish(bpy.context.object, m, loc, (1,1,1), (0,0,0), name, smooth)
def bevel(o, w=0.08, segs=3):
    mod = o.modifiers.new("bevel", 'BEVEL'); mod.width = w; mod.segments = segs; mod.limit_method = 'ANGLE'
def rope(m, p0, p1, r=0.045):
    p0, p1 = Vector(p0), Vector(p1); d = p1 - p0
    c = cyl(m, (p0 + p1) / 2, r, d.length, v=8); c.rotation_euler = d.to_track_quat('Z', 'Y').to_euler(); return c

# ---------------------------------------------------------------- the coach-bus (metres; faces -Y; 1 m = 100 game units)
L, W, H = 11.0, 3.2, 3.0
body = box(M["blue"], (0, 0, 1.0 + H/2), (L, W, H), name="BusBody"); bevel(body, 0.25, 4)
roof = box(M["blue2"], (0, 0, 1.0 + H + 0.1), (L - 0.9, W - 0.5, 0.36), name="Roof"); bevel(roof, 0.16, 4)
# arched windows along both sides, brass frames
for s in (-1, 1):
    for i in range(7):
        y = -L/2 + 1.6 + i * (L - 2.6) / 6
        box(M["brass"], (s * (W/2 + 0.01), y, 2.95), (0.08, 1.05, 1.25))
        box(M["glass"], (s * (W/2 + 0.05), y, 2.95), (0.04, 0.85, 1.05))
        c = cyl(M["brass"], (s * (W/2 + 0.01), y, 3.57), 0.52, 0.08, rot=(0, math.pi/2, 0), v=16); c.scale = (1, 1, 1)
        cyl(M["glass"], (s * (W/2 + 0.05), y, 3.57), 0.42, 0.04, rot=(0, math.pi/2, 0), v=16)
    box(M["gold"], (s * (W/2 + 0.02), 0, 2.2), (0.08, L - 0.6, 0.14))                       # waist stripe
    box(M["rune"], (s * (W/2 + 0.06), 0, 2.05), (0.03, L - 1.2, 0.05))                       # Sheikah rune line, glowing blue
    for i in range(6): box(M["rune"], (s * (W/2 + 0.06), -L/2 + 1.6 + i * (L - 2.6) / 6 + 0.8, 1.55), (0.03, 0.18, 0.18), rot=(math.pi/4, 0, 0))
    box(M["gold"], (s * (W/2 + 0.02), 0, 1.25), (0.08, L - 0.6, 0.10))                      # skirt stripe
    # crest plaque: red field, gold triforce-ish triangle
    box(M["red"], (s * (W/2 + 0.05), 0.3, 1.72), (0.06, 1.4, 0.8))
    cyl(M["gold"], (s * (W/2 + 0.09), 0.3, 1.72), 0.28, 0.04, rot=(0, math.pi/2, 0), v=3)
# front: a short hood with a brass grille, lamps and a crest, like the Fortnite bus's snout
hood = box(M["blue"], (0, -L/2 - 0.75, 1.55), (W - 0.5, 1.5, 1.1), name="Hood"); bevel(hood, 0.18, 3)
box(M["brass"], (0, -L/2 - 1.51, 1.45), (W - 1.2, 0.08, 0.8))
for i in range(5): box(M["dark"], (-0.8 + i * 0.4, -L/2 - 1.56, 1.45), (0.1, 0.06, 0.7))
# horse-head figurehead on the hood, Master Cycle style, bronze with a glowing eye
hh = sph(M["brass"], (0, -L/2 - 2.0, 2.4), 0.5, 14, 10); hh.scale = (0.7, 1.3, 0.9)
hn = box(M["brass"], (0, -L/2 - 2.7, 2.15), (0.55, 0.9, 0.5)); bevel(hn, 0.12, 3)
box(M["brass"], (0, -L/2 - 1.6, 1.95), (0.6, 0.6, 1.0))
for s in (-1, 1): box(M["brass"], (s * 0.22, -L/2 - 1.85, 2.9), (0.14, 0.3, 0.45), rot=(0.4, 0, 0)); sph(M["rune"], (s * 0.3, -L/2 - 2.25, 2.5), 0.1, 8, 6)
for i in range(5): box(M["gold"], (0, -L/2 - 1.3 - i * 0.25, 2.75 - i * 0.07), (0.1, 0.2, 0.45), rot=(0.3, 0, 0))   # mane
for s in (-1, 1):
    sph(M["rune"], (s * (W/2 - 0.45), -L/2 - 1.5, 1.85), 0.22, 12, 8)
    cyl(M["brass"], (s * (W/2 - 0.45), -L/2 - 1.45, 1.85), 0.26, 0.12, rot=(math.pi/2, 0, 0))
# pilot's bay: the front of the body is open glass with the Goron inside
box(M["glass"], (0, -L/2 - 0.02, 3.1), (W - 0.7, 0.08, 1.5))
box(M["brass"], (0, -L/2 - 0.04, 3.1), (W - 0.5, 0.06, 0.08)); box(M["brass"], (0, -L/2 - 0.04, 3.9), (W - 0.5, 0.06, 0.08))
g = sph(M["goron"], (0.5, -L/2 + 0.5, 2.5), 0.62, 16, 10); g.scale = (1, 0.9, 0.8)
sph(M["goron"], (0.5, -L/2 + 0.5, 3.2), 0.38, 16, 10); sph(M["goronhair"], (0.5, -L/2 + 0.6, 3.45), 0.3, 12, 8).scale = (1.2, 0.6, 0.5)
cyl(M["brass"], (0.5, -L/2 + 0.1, 3.0), 0.03, 1.2, rot=(0.3, 0, 0))
# wheels: four big spoked cart wheels on iron arms, tucked up
for x in (-1, 1):
    for y in (-3.4, 3.4):
        cyl(M["dark"], (x * (W/2 + 0.2), y, 0.75), 0.75, 0.3, rot=(0, math.pi/2, 0), v=12)
        cyl(M["iron"], (x * (W/2 + 0.2), y, 0.75), 0.78, 0.18, rot=(0, math.pi/2, 0), v=12)
        for k in range(6): box(M["wood"], (x * (W/2 + 0.2), y, 0.75), (0.16, 1.3, 0.08), rot=(k * math.pi/6, 0, 0))
        box(M["iron"], (x * (W/2 + 0.05), y, 1.0), (0.5, 0.3, 0.6))
        cyl(M["rune"], (x * (W/2 + 0.37), y, 0.75), 0.2, 0.06, rot=(0, math.pi/2, 0), v=12)   # glowing hub eye
# rear platform with rail, steps and the banner pole
box(M["wood"], (0, L/2 + 0.6, 1.1), (W - 0.4, 1.2, 0.25)); box(M["brass"], (0, L/2 + 1.15, 1.7), (W - 0.5, 0.05, 1.0))
for i in range(3): box(M["wood"], (0, L/2 + 1.3 + i * 0.3, 0.9 - i * 0.3), (1.2, 0.3, 0.08))
# roof rack: a brass cage holding the burner, under the Moon's mouth, plus the ring the ropes tie to
rack_z = 1.0 + H + 0.45
for sx in (-1, 1):
    for sy in (-1, 1):
        cyl(M["brass"], (sx * 0.9, sy * 1.2, rack_z + 0.8), 0.05, 1.6, v=8)
box(M["brass"], (0, 0, rack_z + 1.6), (2.0, 2.6, 0.08)); box(M["brass"], (0, 0, rack_z + 0.03), (2.0, 2.6, 0.08))
cyl(M["iron"], (0, 0, rack_z + 0.55), 0.55, 1.0, v=12, name="Firebox")                      # the Goron firebox: a round iron stove
for k in range(8): box(M["rock"], (math.cos(k*math.pi/4)*0.5, math.sin(k*math.pi/4)*0.5, rack_z + 0.55), (0.25, 0.25, 0.9), rot=(0, 0, k*math.pi/4))
box(M["ember"], (0, -0.52, rack_z + 0.5), (0.4, 0.08, 0.3))
f = sph(M["flame"], (0, 0, rack_z + 1.75), 0.42, 12, 8); f.scale = (1, 1, 1.9)                 # the burner flame going up into the Moon's mouth
f = sph(M["flame"], (0, 0, rack_z + 2.4), 0.26, 12, 8); f.scale = (1, 1, 1.6)
cyl(M["brass"], (0, 0, rack_z + 1.65), 1.35, 0.14, v=24, name="BasketRing")
# propeller: four Deku-leaf blades on a brass hub at the back
cyl(M["brass"], (0, L/2 + 1.75, 2.7), 0.2, 0.5, rot=(math.pi/2, 0, 0), name="Propeller")
for i in range(4):
    a = i * math.pi/2 + 0.6
    leaf = sph(M["leaf"], (math.cos(a) * 1.25, L/2 + 1.9, 2.7 + math.sin(a) * 1.25), 1.0, 12, 8, smooth=False)
    leaf.scale = (0.42, 0.06, 1.25); leaf.rotation_euler = (0, a, 0)
    vein = box(M["leaf2"], (math.cos(a) * 1.25, L/2 + 1.97, 2.7 + math.sin(a) * 1.25), (0.06, 0.03, 2.2), rot=(0, a, 0))
# banner on the rear corner
cyl(M["dark"], (1.3, L/2 + 1.0, 3.6), 0.04, 3.0)
box(M["red"], (1.3, L/2 + 2.0, 4.6), (0.03, 2.0, 0.7)); box(M["gold"], (1.3, L/2 + 2.0, 4.6), (0.04, 0.7, 0.18))
# lanterns under the eaves
for i in range(4):
    for s in (-1, 1):
        y = -L/2 + 1.2 + i * (L - 2.4) / 3
        cyl(M["brass"], (s * (W/2 + 0.3), y, 1.0 + H + 0.2), 0.03, 0.4)
        l = sph(M["lantern"], (s * (W/2 + 0.3), y, 1.0 + H - 0.2), 0.24, 10, 8); l.scale = (1, 1, 1.35)
        cyl(M["brass"], (s * (W/2 + 0.3), y, 1.0 + H + 0.08), 0.12, 0.08, v=10)
# ---------------------------------------------------------------- the Moon
R = 5.2; cz = rack_z + 1.7 + R + 1.2; TILT = math.radians(25)
bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=40, radius=R, location=(0, 0, cz))
moon = bpy.context.object; moon.name = "Balloon"; moon.data.materials.append(M["moon"]); bpy.ops.object.shade_smooth()
craters = [(rnd.uniform(-1.3, 1.3), rnd.uniform(0.5, 5.8), rnd.uniform(0.25, 0.9), rnd.uniform(0.18, 0.5)) for _ in range(44)]
def noise(v):
    return (math.sin(v.x * 2.1 + 1.3) * math.sin(v.y * 1.7 + 0.4) * math.sin(v.z * 2.4 + 2.2)) * 0.14 + (math.sin(v.x * 7.0) * math.cos(v.z * 6.0) * math.sin(v.y * 8.0)) * 0.06
for vtx in moon.data.vertices:
    p = vtx.co.copy(); n = p.normalized()
    lat = math.asin(max(-1, min(1, n.z))); lon = math.atan2(n.x, -n.y) % (2 * math.pi)
    d = noise(p)
    for clat, clon, crad, cdep in craters:
        ang = math.acos(max(-1, min(1, math.sin(lat) * math.sin(clat) + math.cos(lat) * math.cos(clat) * math.cos(lon - clon))))
        if ang < crad: d -= cdep * (0.5 + 0.5 * math.cos(ang / crad * math.pi)) * (1.0 if ang < crad * 0.8 else -0.3)
    # the face is on the front, tilted down: sink the mouth and the eye sockets into the rock
    fdir = Vector((0, -math.cos(TILT), -math.sin(TILT)))
    front = max(0.0, n.dot(fdir))
    local_z = n.z * math.cos(TILT) - n.y * math.sin(TILT)   # up along the tilted face
    mouth = math.exp(-((local_z + 0.40) / 0.17) ** 2) * max(0.0, 1.0 - abs(n.x) / 0.62) * front
    d -= mouth * 1.1
    for sx in (-1, 1):
        socket = math.exp(-(((n.x - sx * 0.34) / 0.2) ** 2 + ((local_z - 0.26) / 0.17) ** 2)) * front
        d -= socket * 0.5
    vtx.co = p + n * d
moon.data.update()
def on_face(x, y_in, z):
    """a point on the Moon's surface at face offsets x (right) and z (up), pushed out (y_in > 0) or in (y_in < 0)"""
    d = Vector((x, -R, z)); d.normalize()
    d = Euler((TILT, 0, 0)).to_matrix() @ d
    return Vector((0, 0, cz)) + d * R * (1 + y_in)
def facing(o, at):
    o.rotation_euler = (Vector(at) - Vector((0, 0, cz))).to_track_quat('Y', 'Z').to_euler()
# eyes: deep red-ringed sockets with huge glowing yellow eyes and a slit pupil
for s in (-1, 1):
    for m, k, r, sc_ in (("moondark", -0.06, 1.3, (1, 0.5, 0.95)), ("eyering", -0.02, 1.08, (1, 0.45, 0.85)), ("eye", 0.03, 0.84, (1, 0.6, 0.8)), ("pupil", 0.16, 0.3, (0.5, 0.3, 1.0))):
        o = sph(M[m], on_face(s * 1.8, k, 1.35), r, 20, 12); o.scale = sc_; facing(o, on_face(s * 1.8, 1.0, 1.35))
    o = sph(M["moondark"], on_face(s * 1.8, 0.0, 2.45), 1.4, 16, 10); o.scale = (1.1, 0.35, 0.3); facing(o, on_face(s * 1.8, 1.0, 2.45)); o.rotation_euler.rotate_axis('Y', -s * 0.3)
# nose: two flared nostrils on a bulge
o = sph(M["moon"], on_face(0, 0.02, 0.4), 0.95, 16, 10); o.scale = (1.0, 0.55, 0.65); facing(o, on_face(0, 1.0, 0.4))
for s in (-1, 1):
    o = sph(M["moondark"], on_face(s * 0.55, 0.06, 0.1), 0.4, 12, 8); o.scale = (1, 0.5, 0.7); facing(o, on_face(s * 0.55, 1.0, 0.1))
# mouth: a dark cavity in the dent, two rows of big teeth, a lip ridge below
o = sph(M["mouth"], on_face(0, -0.2, -2.1), 3.1, 24, 12); o.scale = (1.0, 0.22, 0.42); facing(o, on_face(0, 1.0, -2.1))
for i in range(-4, 5):
    x = i * 0.64
    o = box(M["teeth"], on_face(x, -0.03, -1.55), (0.52, 0.5, 0.66)); facing(o, on_face(x, 1.0, -1.55))
    o = box(M["teeth"], on_face(x + 0.32, -0.03, -2.75), (0.48, 0.46, 0.6)); facing(o, on_face(x + 0.32, 1.0, -2.75))
o = sph(M["moondark"], on_face(0, -0.1, -3.5), 2.7, 20, 10); o.scale = (1.0, 0.18, 0.22); facing(o, on_face(0, 1.0, -3.45))
# rope net and the four ropes down to the ring; sandbags
for k in range(4):
    a = k * math.pi/2 + math.pi/4
    top = (math.cos(a) * R * 0.78, math.sin(a) * R * 0.78, cz - R * 0.62)
    rope(M["rope"], top, (math.cos(a) * 1.3, math.sin(a) * 1.3, rack_z + 1.7), 0.06)
    sph(M["rope"], (math.cos(a) * 1.3, math.sin(a) * 1.3, rack_z + 1.35), 0.26, 10, 8)
for k in range(8):   # a net of ropes round the lower half, ending in a collar at the mouth
    a = k * math.pi/4 + 0.2
    pts = [(math.cos(a) * R * math.cos(t) * 0.985, math.sin(a) * R * math.cos(t) * 0.985, cz + R * math.sin(t) * 0.985) for t in [-0.1 - 0.105 * k for k in range(11)]]
    for p0, p1 in zip(pts, pts[1:]): rope(M["rope"], p0, p1, 0.04)
cyl(M["dark"], (0, 0, cz - R + 0.25), 1.15, 0.6, v=20)   # the mouth collar the fire goes into
# ---------------------------------------------------------------- world: sea, islands, sunset sky
bpy.ops.mesh.primitive_plane_add(size=400); sea = bpy.context.object; sea.location = (0, 0, -14); sea.data.materials.append(M["water"])
for (x, y, s, h) in ((-70, 60, 28, 6), (60, 80, 22, 5), (90, -10, 16, 4), (-40, 110, 34, 8)):
    o = sph(M["island"], (x, y, -14), s, 16, 8); o.scale = (1, 0.7, h / s)
    o = sph(M["rock"], (x + s * 0.3, y, -14), s * 0.5, 12, 6); o.scale = (1, 0.8, h / s * 1.3)
bpy.ops.object.light_add(type='SUN'); sun = bpy.context.object; sun.data.energy = 4.5; sun.data.angle = 0.08; sun.rotation_euler = (1.0, 0.35, 2.2)
sun.data.color = (1.0, 0.9, 0.78)
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
nt = w.node_tree; bg = nt.nodes["Background"]; skyn = nt.nodes.new("ShaderNodeTexSky"); skyn.sky_type = 'MULTIPLE_SCATTERING'; skyn.sun_elevation = math.radians(14); skyn.sun_rotation = math.radians(200); skyn.sun_intensity = 0.25; skyn.altitude = 800
nt.links.new(skyn.outputs[0], bg.inputs[0]); bg.inputs[1].default_value = 0.09
sc.render.engine = 'CYCLES'; sc.cycles.samples = 72; sc.cycles.device = 'CPU'; sc.cycles.use_denoising = True
sc.render.resolution_x = 1280; sc.render.resolution_y = 960; sc.view_settings.view_transform = 'Standard'; sc.view_settings.look = 'None'
bpy.ops.object.camera_add(); cam = bpy.context.object; sc.camera = cam; cam.data.lens = 40
views = {"hero": ((-20, -25, 0.5), (0, 0, 7.5)), "side": ((-36, 2, 6.5), (0, 0, 7.2)), "front": ((2, -36, 6.0), (0, 0, 7.0)), "moon-face": ((-4, -17, 7.0), (0, 0, cz - 1.5))}
for name, (pos, tgt) in views.items():
    cam.location = pos; cam.rotation_euler = (Vector(tgt) - Vector(pos)).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = 55 if name == "moon-face" else 40
    sc.render.filepath = os.path.join(out, f"concept-{name}.png"); bpy.ops.render.render(write_still=True); print("wrote", sc.render.filepath, flush=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "bus_concept.blend"))
