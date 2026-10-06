#!/usr/bin/env python3
"""Builds the Ocarina of Time style sky in Blender, animates it, and exports it for the mod.

  pip install bpy numpy pillow        (Blender as a Python module)
  python3 -I tools/scenery/build_sky.py [preview.png]

What it does, in order:
  1. models the pieces as Blender meshes: the sky dome, the distant hills, four puffy clouds, the sun (core, halo, long and short rays), the
     moon (disc, craters, halo) and the two kinds of star. Every corner carries a painted vertex colour and alpha (the lighting is painted in,
     as in the game's own scenes), and the pieces are named so the sky blend can be opened and tweaked in Blender: assets/scenery/sky.blend
  2. animates them with keyframes (sun rays turning and breathing, moon halo pulse, star twinkle, cloud drift); the loop lengths are exported
     so the game plays the same motion
  3. exports shared/sky_model.h: every mesh as a small indexed vertex-colour model (at most 32 corners each, so the game loads one in a single
     vertex load), plus the animation constants
  4. renders the preview sheet (dawn, noon, dusk, night, storm), by colouring the pieces the same way the game does
Blender is Z up; the game is Y up, so world pieces turn (x, y, z) into (x, z, -y). The sun, moon and stars are flat billboards: they keep their
Blender (x, y) as the screen (right, up) and z as the depth towards the viewer.
"""
import math, os, random, sys
import bpy, bmesh
from mathutils import Vector, Matrix

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ASSETS = os.path.join(ROOT, "assets", "scenery")
OUT_H = os.path.join(ROOT, "shared", "sky_model.h")
PREVIEW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ASSETS, "sky-preview.png")

# --- the palette, from the OoT art notes: gold sun, cool bone moon, deep indigo night, warm key / cool fill on the clouds
GOLD = (1.00, 0.86, 0.34); GOLD_DK = (0.96, 0.60, 0.16); CREAM = (1.0, 0.97, 0.84)
MOON = (0.90, 0.92, 0.80); MOON_DK = (0.66, 0.72, 0.70); MOON_CRATER = (0.56, 0.62, 0.62)
CLOUD_TOP = (1.0, 0.99, 0.94); CLOUD_MID = (0.92, 0.95, 1.0); CLOUD_BOT = (0.64, 0.72, 0.88)
HILL_FAR = (0.30, 0.46, 0.56); HILL_NEAR = (0.22, 0.38, 0.40)

def lerp(a, b, t): return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))

# ---------------------------------------------------------------------------------------------------------------- mesh helpers
class Piece:
    """One exported mesh: corners (x, y, z), rgba each, triangles. Also makes the Blender object."""
    def __init__(self, name, world, scale):
        self.name, self.world, self.scale = name, world, scale   # world: True turns Z up into Y up; scale: export units per Blender unit
        self.v, self.c, self.t = [], [], []
    def add(self, p, rgba):
        self.v.append(tuple(p)); self.c.append(tuple(rgba)); return len(self.v) - 1
    def tri(self, a, b, c): self.t.append((a, b, c))
    def fan(self, centre, ring, close=True):
        n = len(ring)
        for i in range(n if close else n - 1): self.tri(centre, ring[i], ring[(i + 1) % n])
    def strip(self, r0, r1, close=True):
        n = len(r0)
        for i in range(n if close else n - 1):
            j = (i + 1) % n
            self.tri(r0[i], r0[j], r1[j]); self.tri(r0[i], r1[j], r1[i])

PIECES = []
def piece(name, world, scale):
    p = Piece(name, world, scale); PIECES.append(p); return p

def ring_pts(n, r, z=0.0, phase=0.0, wob=None):
    out = []
    for i in range(n):
        a = phase + 2 * math.pi * i / n
        rr = r * (wob(i) if wob else 1.0)
        out.append((math.cos(a) * rr, math.sin(a) * rr, z))
    return out

# ---------------------------------------------------------------------------------------------------------------- the pieces
def build_sun():
    # Core: the flat gold disc of the Hyrule field sun, bright cream middle falling to orange at the rim.
    core = piece("sun_core", False, 1000)
    c = core.add((0, 0, 0), (*CREAM, 1.0))
    r1 = [core.add(p, (*lerp(GOLD, CREAM, 0.35), 1.0)) for p in ring_pts(8, 0.55)]
    r2 = [core.add(p, (*GOLD_DK, 1.0)) for p in ring_pts(8, 1.0, phase=math.pi / 8)]
    core.fan(c, r1); core.strip(r1, r2)
    # Halo: a soft ring that fades out, drawn behind the core.
    halo = piece("sun_halo", False, 1000)
    c = halo.add((0, 0, -0.01), (*GOLD, 0.55))
    r1 = [halo.add(p, (*GOLD, 0.30)) for p in ring_pts(12, 1.55)]
    r2 = [halo.add(p, (*GOLD, 0.0)) for p in ring_pts(12, 2.6)]
    halo.fan(c, r1); halo.strip(r1, r2)
    # Rays: eight long spikes and eight short ones between them, each a thin triangle with a gold root and a clear tip.
    for name, count, length, width, phase in (("sun_rays_long", 8, 3.1, 0.30, 0.0), ("sun_rays_short", 8, 2.1, 0.22, math.pi / 8)):
        m = piece(name, False, 1000)
        for i in range(count):
            a = phase + 2 * math.pi * i / count
            ca, sa = math.cos(a), math.sin(a)
            base = 0.95
            b1 = m.add((ca * base - sa * width, sa * base + ca * width, -0.005), (*GOLD, 0.95))
            b2 = m.add((ca * base + sa * width, sa * base - ca * width, -0.005), (*GOLD, 0.95))
            tip = m.add((ca * length, sa * length, -0.005), (*GOLD_DK, 0.0))
            m.tri(b1, b2, tip)

def build_moon():
    # A full moon, a touch cooler than the sun is warm: pale bone with a darker rim.
    d = piece("moon_disc", False, 1000)
    c = d.add((0, 0, 0), (*MOON, 1.0))
    r1 = [d.add(p, (*lerp(MOON, MOON_DK, 0.25), 1.0)) for p in ring_pts(10, 0.6)]
    r2 = [d.add(p, (*MOON_DK, 1.0)) for p in ring_pts(10, 1.0, phase=math.pi / 10)]
    d.fan(c, r1); d.strip(r1, r2)
    # Craters: three flat grey-green blots (each a fan of five corners), the kind a painter dabs on.
    cr = piece("moon_craters", False, 1000)
    for (x, y, r) in ((-0.30, 0.28, 0.24), (0.34, -0.12, 0.20), (-0.08, -0.46, 0.15), (0.12, 0.50, 0.11)):
        ctr = cr.add((x, y, 0.01), (*MOON_CRATER, 0.85))
        ring = [cr.add((x + math.cos(a) * r * (0.85 + 0.3 * ((i * 7) % 3) / 2), y + math.sin(a) * r, 0.01), (*MOON_CRATER, 0.0))
                for i, a in enumerate([2 * math.pi * k / 5 + 0.4 for k in range(5)])]
        cr.fan(ctr, ring)
    # Halo: cool and faint.
    h = piece("moon_halo", False, 1000)
    c = h.add((0, 0, -0.01), (0.78, 0.86, 1.0, 0.40))
    r1 = [h.add(p, (0.78, 0.86, 1.0, 0.18)) for p in ring_pts(12, 1.4)]
    r2 = [h.add(p, (0.78, 0.86, 1.0, 0.0)) for p in ring_pts(12, 2.4)]
    h.fan(c, r1); h.strip(r1, r2)

def build_glow():
    # A soft round glow (opaque middle fading to clear), used in numbers for the Milky Way band and the nebula patches of the night sky.
    g = piece("sky_glow", False, 1000)
    c = g.add((0, 0, 0), (1.0, 1.0, 1.0, 1.0))
    ring = [g.add(p, (1.0, 1.0, 1.0, 0.0)) for p in ring_pts(8, 1.0)]
    g.fan(c, ring)

def build_stars():
    # A four-point sparkle: a long cross with a small waist, like the twinkles on the OoT title screens.
    s = piece("star_sparkle", False, 1000)
    c = s.add((0, 0, 0), (1.0, 1.0, 0.92, 1.0))
    tips, waist = [], []
    for k in range(4):
        a = k * math.pi / 2
        tips.append(s.add((math.cos(a) * 1.0, math.sin(a) * 1.0, 0), (0.9, 0.95, 1.0, 0.0)))
        b = a + math.pi / 4
        waist.append(s.add((math.cos(b) * 0.22, math.sin(b) * 0.22, 0), (1.0, 0.98, 0.86, 0.9)))
    for k in range(4):
        s.tri(c, waist[k - 1], tips[k]); s.tri(c, tips[k], waist[k])
    # A plain diamond dot for the small stars.
    d = piece("star_dot", False, 1000)
    c = d.add((0, 0, 0), (1.0, 1.0, 0.95, 1.0))
    ring = [d.add((math.cos(a) * 0.5, math.sin(a) * 0.5, 0), (0.85, 0.92, 1.0, 0.5)) for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2)]
    d.fan(c, ring)

def build_clouds():
    # OoT clouds are fat, flat-bottomed, lumpy white loaves: a flat shaded underside in cool blue, a sunlit top in cream, a soft edge. Four of
    # them, each three rings (bottom, belly, crown) of lumps along a stretched shape: 26 corners.
    for idx, (lumps, length, tall, seed) in enumerate(((3, 1.0, 0.34, 1), (4, 1.25, 0.38, 2), (2, 0.8, 0.40, 3), (5, 1.5, 0.30, 4))):
        rng = random.Random(seed * 77)
        m = piece("cloud_%d" % idx, True, 100)
        n = 7
        phases = [rng.uniform(0, 6.28) for _ in range(3)]
        def bump(a, k):   # lumps along the long axis
            return 1.0 + 0.42 * math.cos(a * lumps + phases[k]) + 0.08 * math.cos(a * (lumps * 2 + 1) + phases[(k + 1) % 3])
        def ring(r_scale, z, col, alpha, k):
            pts = []
            for i in range(n):
                a = 2 * math.pi * i / n
                rr = r_scale * bump(a, k)
                pts.append(m.add((math.cos(a) * rr * length, math.sin(a) * rr * 0.62, z), (*col, alpha)))
            return pts
        bottom = ring(0.80, 0.0, CLOUD_BOT, 0.0, 0)                    # clear, soft edge on the underside
        under = ring(0.98, tall * 0.18, lerp(CLOUD_BOT, CLOUD_MID, 0.45), 0.9, 1)
        belly = ring(1.00, tall * 0.55, CLOUD_MID, 1.0, 1)
        crown = ring(0.72, tall * 0.95, CLOUD_TOP, 1.0, 2)
        top = m.add((0, 0, tall * 1.08), (*CLOUD_TOP, 1.0))
        base = m.add((0, 0, 0), (*CLOUD_BOT, 0.55))
        m.fan(base, bottom); m.strip(bottom, under); m.strip(under, belly); m.strip(belly, crown); m.fan(top, crown)

def build_hills():
    # Distant hills: low, rounded OoT-field lumps in blue-green, painted lighter at the top so they melt into the haze. Three shapes, 7 corners.
    for idx, (w, h, skew) in enumerate(((1.0, 0.55, 0.0), (1.4, 0.40, 0.25), (0.8, 0.75, -0.2))):
        m = piece("hill_%d" % idx, True, 100)
        l = m.add((-w, 0, -0.25), (*HILL_NEAR, 1.0)); r = m.add((w, 0, -0.25), (*HILL_NEAR, 1.0))
        l2 = m.add((-w * 0.85, 0, h * 0.40), (*lerp(HILL_NEAR, HILL_FAR, 0.5), 1.0)); r2 = m.add((w * 0.85, 0, h * 0.40), (*lerp(HILL_NEAR, HILL_FAR, 0.5), 1.0))
        a = m.add((-w * 0.38 + skew * w, 0, h * 0.88), (*lerp(HILL_FAR, (0.7, 0.85, 0.8), 0.25), 1.0))
        b = m.add((w * 0.30 + skew * w, 0, h * 0.95), (*lerp(HILL_FAR, (0.7, 0.85, 0.8), 0.3), 1.0))
        m.tri(l, r, r2); m.tri(l, r2, l2)
        # the ridge between the two crowns, with a notch
        notch = m.add((0.0 + skew * w * 0.5, 0, h * 0.70), (*lerp(HILL_FAR, HILL_NEAR, 0.3), 1.0))
        m.tri(a, notch, b)
        m.tri(l2, a, notch); m.tri(r2, notch, b); m.tri(l2, notch, r2)

DOME_RINGS = [-0.12, 0.0, 0.07, 0.16, 0.30, 0.52, 0.78, 1.0]   # the sine of each ring's height
DOME_SEGS = 14
def build_dome():
    # The dome keeps its rings from just under the horizon up to the zenith; the game paints the colours (they follow the time of day and the
    # weather), the model fixes the shape: 8 rings of 14 segments, with the seam repeated so a pair of rings is one 30-corner load.
    m = piece("sky_dome", True, 1000)
    for s in DOME_RINGS:
        c = math.sqrt(max(0.0, 1.0 - s * s))
        for j in range(DOME_SEGS + 1):
            a = 2 * math.pi * j / DOME_SEGS
            m.add((math.cos(a) * c, math.sin(a) * c, s), (s, 0, 0, 1))
    for r in range(len(DOME_RINGS) - 1):
        for j in range(DOME_SEGS):
            a = r * (DOME_SEGS + 1) + j
            m.tri(a, a + 1, a + DOME_SEGS + 1); m.tri(a + 1, a + DOME_SEGS + 2, a + DOME_SEGS + 1)

# ---------------------------------------------------------------------------------------------------------------- Blender scene + animation
def to_blender_xyz(p, world):
    # Pieces are authored in Blender space (world pieces z up; billboards flat in XY), so the object is just the mesh.
    return p

def make_objects():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = {}
    for p in PIECES:
        me = bpy.data.meshes.new(p.name)
        me.from_pydata([v for v in p.v], [], [t for t in p.t])
        me.update()
        ca = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
        for li, loop in enumerate(me.loops):
            ca.data[li].color = p.c[loop.vertex_index]
        ob = bpy.data.objects.new(p.name, me)
        bpy.context.scene.collection.objects.link(ob)
        mat = bpy.data.materials.new(p.name + "_mat")
        mat.use_nodes = True
        nt = mat.node_tree
        for n in list(nt.nodes): nt.nodes.remove(n)
        att = nt.nodes.new("ShaderNodeVertexColor"); att.layer_name = "Col"
        em = nt.nodes.new("ShaderNodeEmission")
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(att.outputs["Color"], em.inputs["Color"])
        nt.links.new(att.outputs["Alpha"], mix.inputs[0])
        nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs["Surface"])
        me.materials.append(mat)
        objs[p.name] = ob
    return objs

# The loops the game plays, in seconds.
ANIM = dict(sun_ray_turn=240.0, sun_ray_breath=6.0, moon_pulse=9.0, star_twinkle=3.2, cloud_drift=900.0)

def animate(objs):
    scn = bpy.context.scene
    fps = 24; scn.render.fps = fps
    # keyframes over one second-compressed loop (frames 1..49 stand for the 240 s ray turn; the others loop inside it)
    scn.frame_start, scn.frame_end = 1, 49
    def key(ob, path, idx, f, v):
        setattr(ob, path, v) if idx is None else None
    for nm in ("sun_rays_long", "sun_rays_short"):
        ob = objs[nm]
        ob.rotation_euler = (0, 0, 0); ob.keyframe_insert("rotation_euler", index=2, frame=1)
        ob.rotation_euler = (0, 0, (-1 if nm.endswith("short") else 1) * math.pi / 4); ob.keyframe_insert("rotation_euler", index=2, frame=49)
    for nm in ("sun_halo", "moon_halo"):
        ob = objs[nm]
        for f, s in ((1, 1.0), (13, 1.1), (25, 1.0), (37, 1.1), (49, 1.0)):
            ob.scale = (s, s, s); ob.keyframe_insert("scale", frame=f)
    for nm in ("star_sparkle", "star_dot"):
        ob = objs[nm]
        for f, s in ((1, 1.0), (13, 0.7), (25, 1.0), (37, 0.75), (49, 1.0)):
            ob.scale = (s, s, s); ob.keyframe_insert("scale", frame=f)
    for i in range(4):
        ob = objs["cloud_%d" % i]
        ob.location = (-1.0, 0, 0); ob.keyframe_insert("location", frame=1)
        ob.location = (1.0, 0, 0); ob.keyframe_insert("location", frame=49)
    for ob in objs.values():
        if ob.animation_data and ob.animation_data.action:
            for fc in ob.animation_data.action.fcurves if hasattr(ob.animation_data.action, "fcurves") else []:
                for kp in fc.keyframe_points: kp.interpolation = "LINEAR"

# ---------------------------------------------------------------------------------------------------------------- export
def export_header():
    L = []
    L.append("// GENERATED by tools/scenery/build_sky.py from assets/scenery/sky.blend. Do not edit by hand: change the Blender script and run it again.")
    L.append("// The Ocarina of Time style sky, modelled in Blender: sky dome, distant hills, four clouds, the sun (core, halo, rays), the moon (disc, craters, halo) and")
    L.append("// two kinds of star. Every corner has a painted vertex colour and alpha; each mesh has at most 32 corners so the game loads it in one vertex load.")
    L.append("// World pieces (dome, hills, clouds) are Y up, in thousandths (dome) or hundredths of a unit; billboards (sun, moon, stars) are (right, up, toward viewer).")
    L.append("#pragma once")
    L.append("#include <cstdint>\n")
    L.append("namespace royale {\nnamespace sky_model {\n")
    names = []
    for p in PIECES:
        assert len(p.v) <= 32 or p.name == "sky_dome", (p.name, len(p.v))
        pos = []
        for (x, y, z) in p.v:
            if p.world: x, y, z = x, z, -y
            pos += [int(round(x * p.scale)), int(round(y * p.scale)), int(round(z * p.scale))]
        col = []
        for rgba in p.c: col += [int(round(max(0.0, min(1.0, ch)) * 255)) for ch in rgba]
        idx = [i for t in p.t for i in t]
        n = p.name
        L.append("inline constexpr int k%s_Verts = %d, k%s_Tris = %d;" % (n, len(p.v), n, len(p.t)))
        L.append("inline constexpr int16_t k%s_Pos[] = {%s};" % (n, ",".join(map(str, pos))))
        L.append("inline constexpr uint8_t k%s_Col[] = {%s};" % (n, ",".join(map(str, col))))
        L.append("inline constexpr uint16_t k%s_Idx[] = {%s};" % (n, ",".join(map(str, idx))))
        L.append("")
        names.append((n, p.scale))
    L.append("struct Mesh { int verts, tris; const int16_t* pos; const uint8_t* col; const uint16_t* idx; float scale; };")
    for n, sc in names:
        L.append("inline constexpr Mesh %s = {k%s_Verts, k%s_Tris, k%s_Pos, k%s_Col, k%s_Idx, %.7gf};" % (n, n, n, n, n, n, 1.0 / sc))
    L.append("")
    L.append("inline constexpr int kDomeSegs = %d, kDomeRings = %d;" % (DOME_SEGS, len(DOME_RINGS)))
    L.append("inline constexpr Mesh kClouds[4] = {cloud_0, cloud_1, cloud_2, cloud_3};")
    L.append("inline constexpr Mesh kHills[3] = {hill_0, hill_1, hill_2};")
    L.append("// The animation loops authored in the Blender file, in seconds.")
    for k, v in ANIM.items():
        L.append("inline constexpr float k%s = %.1ff;" % ("".join(w.capitalize() for w in k.split("_")), v))
    L.append("\n} // namespace sky_model\n} // namespace royale\n")
    with open(OUT_H, "w") as f: f.write("\n".join(L))

# ---------------------------------------------------------------------------------------------------------------- preview (same colouring as the game)
def sky_palette(day, twilight, ov, tint):
    mix = lambda a, b, k: a + (b - a) * k
    nz, nh, dz, dh = (5, 8, 30), (22, 28, 66), (62, 122, 224), (168, 206, 242)
    glow, glowz = (255, 128, 62), (96, 78, 150)
    zen, hor = [], []
    dim = 1.0 - 0.55 * ov * day
    for i in range(3):
        z = mix(nz[i], dz[i], day); h = mix(nh[i], dh[i], day)
        z = mix(z, glowz[i], twilight * 0.55); h = mix(h, glow[i], twilight * 0.8)
        grey = tint[i] * (0.25 + 0.75 * day)
        zen.append(mix(z, grey * 0.8, ov * 0.85) * dim); hor.append(mix(h, grey, ov * 0.85) * dim)
    return zen, hor

def render_preview(objs):
    import numpy as np
    from PIL import Image, ImageDraw
    scn = bpy.context.scene
    states = [  # name, sun height, ov, tint
        ("Dawn", 0.16, 0.0, (120, 126, 138)), ("Noon", 0.95, 0.0, (120, 126, 138)), ("Dusk", 0.10, 0.0, (120, 126, 138)),
        ("Night", -0.3, 0.0, (120, 126, 138)), ("Thunderstorm", 0.7, 1.0, (82, 88, 108))]
    W, H = 640, 400
    scn.render.engine = "CYCLES"; scn.cycles.samples = 8; scn.cycles.device = "CPU"
    scn.render.resolution_x, scn.render.resolution_y = W, H
    scn.render.film_transparent = False
    scn.render.image_settings.file_format = "PNG"
    scn.view_settings.view_transform = "Standard"
    w = bpy.data.worlds.new("w"); scn.world = w; w.use_nodes = True
    cam_d = bpy.data.cameras.new("cam"); cam_d.lens = 22; cam_d.sensor_width = 36
    cam = bpy.data.objects.new("cam", cam_d); scn.collection.objects.link(cam); scn.camera = cam
    cam.location = (0, 0, 0)
    cam.rotation_euler = (math.radians(86), 0, 0)   # looks along +Y with the horizon low in frame
    # the dome as an emissive mesh, recoloured per state
    dome = objs["sky_dome"]; dme = dome.data
    dome.scale = (60, 60, 60)
    # Blender-side dome is unit-sized in game orientation (Y up in the game = Z up here): the stored mesh is already Blender oriented.
    cols = dme.color_attributes["Col"]
    bpy.ops.mesh.primitive_circle_add(vertices=48, radius=70, fill_type='NGON', location=(0, 0, -1.2))
    gr = bpy.context.object; gm = bpy.data.materials.new('g'); gm.use_nodes = True
    gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.05, 0.16, 0.05, 1); gr.data.materials.append(gm)
    sheets = []
    smooth = lambda a, b, x: (lambda t: t * t * (3 - 2 * t))(min(1, max(0, (x - a) / (b - a))))
    for name, sunh, ov, tint in states:
        day = smooth(-0.12, 0.35, sunh); night = smooth(0.12, -0.3, sunh); tw = min(1, max(0, 1 - abs(sunh) / 0.3))
        zen, hor = sky_palette(day, tw, ov, tint)
        for li, loop in enumerate(dme.loops):
            vi = loop.vertex_index; x, y, z = dme.vertices[vi].co
            k = max(0.0, min(1.0, z)) ** 0.6
            a = math.atan2(y, x); to_sun = 0.5 + 0.5 * math.cos(a - 1.5708)
            g = tw * (0.35 + 0.65 * to_sun) * (1 - k) * (1 - ov * 0.7)
            c = [((hor[i] + (zen[i] - hor[i]) * k) * (1 - g * 0.35) + (255, 128, 62)[i] * g * 0.35) / 255.0 for i in range(3)]
            cols.data[li].color = (*[(ch ** 2.2) for ch in c], 1.0)
        # place everything for this state
        for ob in objs.values():
            if ob.name != "sky_dome": ob.hide_render = True
        placed = []
        def put(nm, loc, rot_z=0.0, scale=1.0, tintc=(1, 1, 1), alpha=1.0, billboard=True):
            src = objs[nm]
            ob = src.copy(); ob.data = src.data.copy(); scn.collection.objects.link(ob); ob.hide_render = False
            ob.animation_data_clear()
            ob.location = loc; ob.scale = (scale,) * 3
            if billboard:
                d = Vector(loc).normalized()
                ob.rotation_mode = "QUATERNION"
                ob.rotation_quaternion = (-d).to_track_quat("Z", "Y")
                ob.rotation_mode = "XYZ"
                ob.rotation_euler.rotate(__import__("mathutils").Euler((0, 0, rot_z)))
                if rot_z:
                    q = (-d).to_track_quat("Z", "Y"); ob.rotation_mode = "QUATERNION"
                    from mathutils import Quaternion
                    ob.rotation_quaternion = q @ Quaternion((0, 0, 1), rot_z)
            ca = ob.data.color_attributes["Col"]
            for li in range(len(ca.data)):
                r, g, b, a = ca.data[li].color
                ca.data[li].color = ((r * tintc[0]) ** 2.2 if False else (r * tintc[0]), g * tintc[1], b * tintc[2], a * alpha)
            placed.append(ob); return ob
        R = 55.0
        sun_dir = Vector((math.cos(1.5708) * math.sqrt(max(0, 1 - sunh ** 2)), math.sin(1.5708) * math.sqrt(max(0, 1 - sunh ** 2)), sunh))
        sun_dir = Vector((0.0, math.sqrt(max(0, 1 - sunh ** 2)), sunh)); moon_dir = Vector((0.0, abs(sun_dir.y), -sun_dir.z))   # shown ahead of the camera so the sheet has a moon
        sunA = max(0, min(1, sunh * 6 + 0.4)) * (1 - ov); moonA = max(0, min(1, -sunh * 6 + 0.4)) * (1 - ov)
        warm = tw
        if sunA > 0.02:
            tc = (1.0, (244 - 84 * warm) / 255 / 0.96, (205 - 115 * warm) / 255 / 0.8)
            for nm, s, a in (("sun_halo", 9, 1.0), ("sun_rays_long", 9, 1.0), ("sun_rays_short", 9, 1.0), ("sun_core", 9, 1.0)):
                put(nm, sun_dir * R, rot_z=0.3, scale=s, tintc=(1.0, min(1, tc[1]), min(1, tc[2])), alpha=sunA * a)
        if moonA > 0.02:
            for nm in ("moon_halo", "moon_disc", "moon_craters"): put(nm, moon_dir * R, scale=7, alpha=moonA)
        starA = night * (1 - ov)
        if starA > 0.05:
            rng = random.Random(5)
            for i in range(170):
                a = rng.uniform(0, 6.283); s = 0.04 + 0.96 * rng.random() ** 0.8
                d = Vector((math.cos(a) * math.sqrt(1 - s * s), math.sin(a) * math.sqrt(1 - s * s), s))
                if d.y < -0.05: continue
                big = rng.random()
                put("star_sparkle" if big > 0.7 else "star_dot", d * R * 1.02, scale=(0.9 + 1.4 * big * big) * 1.4, alpha=starA * min(1, s * 6) * (0.5 + 0.5 * big))
        # hills
        rng = random.Random(9)
        hcol = (hor[0] / 255.0 * 0.55 + 0.1, hor[1] / 255.0 * 0.55 + 0.12, hor[2] / 255.0 * 0.55 + 0.1)
        base_l = 0.35 + 0.65 * day
        for i in range(22):
            a = 2 * math.pi * i / 22 + rng.uniform(-0.08, 0.08)
            hh = objs["hill_%d" % (i % 3)]
            ob = put("hill_%d" % (i % 3), Vector((math.cos(a) * 54, math.sin(a) * 54, -2.0)), scale=rng.uniform(4.5, 8) * 1.0,
                     tintc=(base_l * (1 - 0.5 * ov), base_l * (1 - 0.5 * ov), base_l * (1 - 0.3 * ov)), billboard=False)
            ob.rotation_euler = (0, 0, a - math.pi / 2 + math.pi)
            ob.rotation_euler = (math.pi / 2, 0, a + math.pi / 2)
            ob.rotation_euler = (0, 0, a + math.pi / 2)
        # clouds
        cover = min(1.0, 0.3 + 0.7 * ov); n = int(10 + 14 * cover)
        rng = random.Random(3)
        for i in range(n):
            a = rng.uniform(0.0, 6.283); el = rng.uniform(0.12, 0.55)
            d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
            lit = [(40 + 6 * ch) + ((250 if ov < 0.3 else tint[ch]) - (40 + 6 * ch)) * day for ch in range(3)]
            lit = [lit[ch] + ((255, 150, 120)[ch] - lit[ch]) * tw * 0.55 * (1 - ov * 0.6) for ch in range(3)]
            ob = put("cloud_%d" % (i % 4), d * 52, scale=rng.uniform(6, 11) * (1 + 0.5 * ov), tintc=tuple(min(1.0, c / 255.0) for c in lit), alpha=0.55 + 0.45 * ov, billboard=False)
            ob.rotation_euler = (0, 0, a + math.pi / 2); ob.location = d * 52
            ob.rotation_euler = (math.radians(0), 0, a + math.pi / 2)
        scn.render.filepath = os.path.join("/tmp", "sky_%s.png" % name)
        bpy.ops.render.render(write_still=True)
        img = Image.open(scn.render.filepath).convert("RGB")
        d = ImageDraw.Draw(img); d.rectangle((0, 0, 140 if name != "Thunderstorm" else 150, 22), fill=(0, 0, 0)); d.text((8, 6), name, fill=(255, 255, 255))
        sheets.append(img)
        for ob in placed: bpy.data.objects.remove(ob, do_unlink=True)
    cols_n = 2; rows_n = (len(sheets) + 1) // 2
    sheet = Image.new("RGB", (W * cols_n, H * rows_n), (20, 20, 20))
    for i, im in enumerate(sheets): sheet.paste(im, ((i % cols_n) * W, (i // cols_n) * H))
    sheet.save(PREVIEW)

def main():
    build_dome(); build_hills(); build_clouds(); build_sun(); build_moon(); build_glow(); build_stars()
    objs = make_objects()
    animate(objs)
    os.makedirs(ASSETS, exist_ok=True)
    export_header()
    if "--no-preview" not in sys.argv:
        render_preview(objs)
    # remove the preview-only camera and world tweaks before saving the authoring file
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ASSETS, "sky.blend"))
    print("wrote", OUT_H)

if __name__ == "__main__":
    main()
