"""Builds the Moonfall Bus in Blender: a lacquered royal coach under the Majora's Mask Moon (docs/BATTLE_BUS.md, concept docs/battle-bus/concept-2d.png).

Run with the `bpy` module or Blender itself:
    python3 tools/bus/build_bus.py            (or: blender -b --python tools/bus/build_bus.py)
It writes assets/bus/bus.blend and the three textures as PNG. export_bus.py turns the .blend into shared/bus_model.h and shared/bus_geometry.h;
render_previews.py renders the preview pictures and the turntable.

The look Tee asked for: smooth and higher poly, like a GameCube asset (Wind Waker / Twilight Princess era), in the Ocarina of Time and stylised
Zelda colours. Rounded lacquered panels, bevelled gold trim, real arched windows, a Moon with its face modelled in (sunken sockets, bulging
glowing eyes, slanted brows, two even rows of square teeth), smooth normals, painterly 256x256 textures.

Everything is built here in game units with the game's axes (Y up, the bus facing +Z, x across; the floor centre at the origin) and converted to
Blender's (metres, Z up, facing -Y) when each object is made. Each rigid part is its own object with its origin at its pivot; the empties mark
the seats, the rope ends, the lantern hooks, the wheels and the other points the game needs.
"""
import math
import os
import random
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "assets", "bus")
sys.path.insert(0, HERE)
import paint_bus as P  # noqa: E402  (the textures and their regions)

SCALE = 100.0   # game units per metre

# ---- the measures (game units) ------------------------------------------------------------------------------------------------------
# These match shared/bus.h (seats, exit) and docs/BATTLE_BUS.md section 2.
SEAT_ROWS, SEATS_PER_ROW = 2, 16
SEAT_PITCH, ROW_GAP, SEAT_H = 54.0, 120.0, 42.0
EXIT = (-230.0, 20.0, 0.0)

HALF_W = 160.0          # the body's half width
FRONT, BACK = 500.0, -500.0
SKIRT, SILL, BAND_TOP, EAVE, ROOF_TOP = -50.0, 70.0, 190.0, 238.0, 262.0
CORNER = 46.0           # the rounded vertical corners
WIN_COUNT, WIN_PITCH, WIN_W = 7, 116.0, 78.0
HOOD_FRONT = 615.0
WHEEL_R, WHEEL_Y, WHEEL_Z, WHEEL_X = 68.0, -46.0, 330.0, 182.0
CAGE_POST, RING_Y, RING_R = 72.0, 470.0, 96.0
MOON_R = 450.0
MOON_C = (0.0, RING_Y + 50.0 + MOON_R, 0.0)        # the Moon's centre; its collar (the pivot) is at its bottom
MOON_PIVOT = (0.0, MOON_C[1] - MOON_R + 6.0, 0.0)
NET_LAT = math.radians(-44.0)                      # the net's bottom ring, where the ropes are knotted
FACE_TILT = math.radians(20.0)                     # the face looks forward and this far down
PROP_HUB = (0.0, 120.0, -705.0)
PROP_R = 225.0
LANTERN_Z = (-375.0, -125.0, 125.0, 375.0)


def g2b(p):
    """A game point to Blender (metres, Z up, facing -Y)."""
    return Vector((p[0] / SCALE, -p[2] / SCALE, p[1] / SCALE))


def V(x, y, z):
    return Vector((x, y, z))


# ---- a mesh builder: faces with per-corner UVs and one material each -------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.verts = []
        self.faces = []      # (vertex indices, uvs, material)
        self.alpha = {}      # vertex index -> alpha (only the face decal uses it)

    def v(self, p):
        self.verts.append(Vector(p))
        return len(self.verts) - 1

    def face(self, idx, uvs, mat):
        self.faces.append((list(idx), [tuple(u) for u in uvs], mat))

    def poly(self, pts, uvs, mat):
        self.face([self.v(p) for p in pts], uvs, mat)

    # A grid of quads from a 2D array of points; UVs go across the region (u along columns, v along rows), tiling `rep` times.
    def grid(self, rows, region, mat, rep=(1.0, 1.0), close_u=False, flip=False):
        nr, nc = len(rows), len(rows[0])
        ids = [[self.v(p) for p in row] for row in rows]
        cols = nc if close_u else nc - 1
        for r in range(nr - 1):
            for c in range(cols):
                c2 = (c + 1) % nc
                u0, u1 = wrap_pair(c / cols * rep[0], (c + 1) / cols * rep[0])
                v0, v1 = wrap_pair(r / (nr - 1) * rep[1], (r + 1) / (nr - 1) * rep[1])
                q = [ids[r][c], ids[r][c2], ids[r + 1][c2], ids[r + 1][c]]
                uv = [P.uv(region, u0, v0), P.uv(region, u1, v0), P.uv(region, u1, v1), P.uv(region, u0, v1)]
                if flip:
                    q.reverse(); uv.reverse()
                self.face(q, uv, mat)
        return ids

    def merge(self, other, xf=None):
        base = len(self.verts)
        for p in other.verts:
            self.verts.append(xf @ p if xf is not None else p.copy())
        for idx, uvs, mat in other.faces:
            self.faces.append(([i + base for i in idx], uvs, mat))
        return self


def wrap_pair(a, b):
    """A span of texture repeats folded into one repeat (the atlas regions cannot tile): small spans keep their size."""
    d = b - a
    fa = a - math.floor(a + 1e-6)
    if fa + d > 1.0 + 1e-6:
        fa = max(0.0, 1.0 - d)
    return fa, fa + d


# ---- shapes ---------------------------------------------------------------------------------------------------------------------
def lathe(profile, seg, region, mat, center=(0, 0, 0), axis="y", rep_u=1.0, cap_top=False, cap_bot=False, phase=0.0):
    """A surface of revolution: profile is [(radius, height)], swept round `axis`."""
    m = Mesh()
    rows = []
    for r, h in profile:
        row = []
        for i in range(seg):
            a = (i / seg) * 2 * math.pi + phase
            if axis == "y":
                row.append(V(center[0] + r * math.sin(a), center[1] + h, center[2] + r * math.cos(a)))
            elif axis == "z":
                row.append(V(center[0] + r * math.cos(a), center[1] + r * math.sin(a), center[2] + h))
            else:
                row.append(V(center[0] + h, center[1] + r * math.sin(a), center[2] + r * math.cos(a)))
        rows.append(row)
    m.grid(rows, region, mat, rep=(rep_u, 1.0), close_u=True, flip=(axis == "y"))
    for cap, row, h in ((cap_bot, rows[0], profile[0][1]), (cap_top, rows[-1], profile[-1][1])):
        if not cap:
            continue
        c = V(*center) + (V(0, h, 0) if axis == "y" else V(0, 0, h) if axis == "z" else V(h, 0, 0))
        ci = m.v(c)
        ids = [m.v(p) for p in row]
        for i in range(seg):
            j = (i + 1) % seg
            a0, a1 = i / seg * 2 * math.pi, j / seg * 2 * math.pi
            tri = [ci, ids[i], ids[j]]
            uv = [P.uv(region, 0.5, 0.5), P.uv(region, 0.5 + 0.5 * math.sin(a0), 0.5 + 0.5 * math.cos(a0)),
                  P.uv(region, 0.5 + 0.5 * math.sin(a1), 0.5 + 0.5 * math.cos(a1))]
            top = row is rows[-1]
            if (axis == "y") == top:
                tri.reverse(); uv.reverse()
            m.face(tri, uv, mat)
    return m


def cylinder(a, b, r, seg, region, mat, caps=True, rep_u=1.0):
    """A round bar from a to b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    ln = d.length
    m = lathe([(r, 0.0), (r, ln)], seg, region, mat, rep_u=rep_u, cap_top=caps, cap_bot=caps)
    return m_align(m, a, d)


def m_align(m, a, d):
    """Rotate a mesh built along +Y to point along d, then move it to a."""
    q = Vector((0, 1, 0)).rotation_difference(d.normalized())
    xf = Matrix.Translation(a) @ q.to_matrix().to_4x4()
    out = Mesh()
    return out.merge(m, xf)


def tube(path, r, seg, region, mat, rep_v=1.0, closed=False):
    """A round tube along a polyline (ropes, arches, rails)."""
    m = Mesh()
    rows = []
    n = len(path)
    prev_side = None
    for i, p in enumerate(path):
        p = Vector(p)
        if closed:
            t = (Vector(path[(i + 1) % n]) - Vector(path[i - 1]))
        else:
            t = (Vector(path[min(i + 1, n - 1)]) - Vector(path[max(i - 1, 0)]))
        t.normalize()
        ref = V(0, 1, 0) if abs(t.y) < 0.9 else V(1, 0, 0)
        side = t.cross(ref).normalized() if prev_side is None else (prev_side - t * prev_side.dot(t)).normalized()
        prev_side = side
        up = side.cross(t).normalized()
        rows.append([p + (side * math.cos(a) + up * math.sin(a)) * r for a in (k / seg * 2 * math.pi for k in range(seg))])
    if closed:
        rows.append(rows[0])
    ids = [[m.v(q) for q in row] for row in rows]
    nr = len(rows)
    for i in range(nr - 1):
        for k in range(seg):
            k2 = (k + 1) % seg
            u0, u1 = k / seg, (k + 1) / seg
            v0, v1 = wrap_pair(i / (nr - 1) * rep_v, (i + 1) / (nr - 1) * rep_v)
            m.face([ids[i][k], ids[i + 1][k], ids[i + 1][k2], ids[i][k2]],
                   [P.uv(region, u0, v0), P.uv(region, u0, v1), P.uv(region, u1, v1), P.uv(region, u1, v0)], mat)
    return m


def rbox(cx, cy, cz, sx, sy, sz, r, region, mat, seg=3):
    """A box with rounded edges and corners (radius r, seg steps round each edge). Each side maps the whole region."""
    m = Mesh()
    r = min(r, sx / 2 - 0.01, sy / 2 - 0.01, sz / 2 - 0.01)
    h = V(sx / 2 - r, sy / 2 - r, sz / 2 - r)
    k = 0.5
    lin = [-(k + (1 - k) * j / seg) for j in range(seg, -1, -1)] + [k + (1 - k) * j / seg for j in range(seg + 1)]
    sides = [(V(1, 0, 0), V(0, 0, -1), V(0, 1, 0)), (V(-1, 0, 0), V(0, 0, 1), V(0, 1, 0)), (V(0, 1, 0), V(1, 0, 0), V(0, 0, -1)),
             (V(0, -1, 0), V(1, 0, 0), V(0, 0, 1)), (V(0, 0, 1), V(1, 0, 0), V(0, 1, 0)), (V(0, 0, -1), V(-1, 0, 0), V(0, 1, 0))]
    def f(a):
        return max(-1.0, min(1.0, a / k))
    def e(a):
        return math.copysign(max(0.0, (abs(a) - k) / (1 - k)), a)
    for nrm, ua, va in sides:
        grid = []
        for b in lin:
            row = []
            for a in lin:
                q = nrm + ua * f(a) + va * f(b)
                inner = V(q.x * h.x, q.y * h.y, q.z * h.z)
                row.append(V(cx, cy, cz) + inner + (nrm + ua * e(a) + va * e(b)).normalized() * r)
            grid.append(row)
        ids = [[m.v(p) for p in row] for row in grid]
        L = len(lin)
        for j in range(L - 1):
            for i in range(L - 1):
                u0, u1 = (lin[i] + 1) / 2, (lin[i + 1] + 1) / 2
                v0, v1 = 1 - (lin[j] + 1) / 2, 1 - (lin[j + 1] + 1) / 2
                m.face([ids[j][i], ids[j][i + 1], ids[j + 1][i + 1], ids[j + 1][i]],
                       [P.uv(region, u0, v0), P.uv(region, u1, v0), P.uv(region, u1, v1), P.uv(region, u0, v1)], mat)
    return m


def box(cx, cy, cz, sx, sy, sz, region, mat):
    """A plain box (12 triangles)."""
    m = Mesh()
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    c = V(cx, cy, cz)
    for nrm, ua, va in ((V(1, 0, 0), V(0, 0, -1), V(0, 1, 0)), (V(-1, 0, 0), V(0, 0, 1), V(0, 1, 0)), (V(0, 1, 0), V(1, 0, 0), V(0, 0, -1)),
                        (V(0, -1, 0), V(1, 0, 0), V(0, 0, 1)), (V(0, 0, 1), V(1, 0, 0), V(0, 1, 0)), (V(0, 0, -1), V(-1, 0, 0), V(0, 1, 0))):
        def at(a, b):
            q = nrm + ua * a + va * b
            return c + V(q.x * hx, q.y * hy, q.z * hz)
        m.poly([at(-1, -1), at(1, -1), at(1, 1), at(-1, 1)], [P.uv(region, 0, 1), P.uv(region, 1, 1), P.uv(region, 1, 0), P.uv(region, 0, 0)], mat)
    return m


def ellipsoid(c, rx, ry, rz, seg, rings, region, mat, rep_u=1.0, fn=None):
    """A UV ellipsoid; fn(p, n) may move each point (displacement)."""
    m = Mesh()
    rows = []
    for j in range(rings + 1):
        lat = -math.pi / 2 + math.pi * j / rings
        row = []
        for i in range(seg):
            lon = 2 * math.pi * i / seg
            n = V(math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon))
            p = V(c[0] + n.x * rx, c[1] + n.y * ry, c[2] + n.z * rz)
            if fn:
                p = fn(p, n)
            row.append(p)
        rows.append(row)
    m.grid(rows, region, mat, rep=(rep_u, 1.0), close_u=True, flip=True)
    return m


# ---- the body --------------------------------------------------------------------------------------------------------------------
def rounded_plan(half_w, front, back, corner, seg):
    """The body's outline seen from above, anticlockwise from the front-right: a rectangle with round vertical corners."""
    pts = []
    for cx, cz, a0 in ((half_w - corner, front - corner, 0.0), (-(half_w - corner), front - corner, 90.0),
                       (-(half_w - corner), back + corner, 180.0), (half_w - corner, back + corner, 270.0)):
        for k in range(seg + 1):
            a = math.radians(a0 + 90.0 * k / seg)
            pts.append((cx + corner * math.cos(a), cz + corner * math.sin(a)))
    return pts


def shell_ring(y, inset, seg=5):
    return [V(x, y, z) for x, z in rounded_plan(HALF_W - inset, FRONT - inset, BACK + inset, max(4.0, CORNER - inset), seg)]


def body_shell(m):
    """The lacquered coach: a rounded skirt, the lower panels, the window band (built separately), the upper panel and the domed roof."""
    lac, dark, gold = P.R["lacquer"], P.R["lacquer_dark"], P.R["gold"]
    # lower body, from the rounded skirt to the sill: rings of the outline, pulled in at the bottom
    prof = [(SKIRT, 26.0), (SKIRT + 6, 12.0), (SKIRT + 18, 3.0), (SKIRT + 34, 0.0), (SILL - 40, 0.0)]
    rows = [shell_ring(y, ins) for y, ins in prof]
    m.grid(rows, lac, 1, rep=(8.0, 1.0), close_u=True)
    # the gold waist band (raised a little) and the skirt band
    for y0, y1, out in ((SILL - 40, SILL - 22, 4.0), (SKIRT + 8, SKIRT + 20, 2.0)):
        rows = [shell_ring(y0, 0.0), shell_ring(y0 + 2, -out), shell_ring(y1 - 2, -out), shell_ring(y1, 0.0)]
        m.grid(rows, gold, 0, rep=(16.0, 1.0), close_u=True)
    # the rune band and the sill: lacquer up to the sill
    rows = [shell_ring(SILL - 22, 0.0), shell_ring(SILL, 0.0)]
    m.grid(rows, lac, 1, rep=(8.0, 0.4), close_u=True)
    # the floor's underside
    ring = shell_ring(SKIRT, 26.0)
    c = m.v(V(0, SKIRT, 0))
    ids = [m.v(p) for p in ring]
    for i in range(len(ids)):
        j = (i + 1) % len(ids)
        m.face([c, ids[j], ids[i]], [P.uv(dark, 0.5, 0.5), P.uv(dark, 0, 1), P.uv(dark, 1, 1)], 1)
    # the upper panel (above the windows) and the eaves, then the domed roof
    prof = [(BAND_TOP, 0.0), (EAVE - 8, 0.0), (EAVE - 2, -6.0), (EAVE, -12.0)]
    rows = [shell_ring(y, ins) for y, ins in prof]
    m.grid(rows, lac, 1, rep=(8.0, 0.5), close_u=True)
    rows = [shell_ring(EAVE, -12.0), shell_ring(EAVE + 6, -10.0)]
    m.grid(rows, gold, 0, rep=(16.0, 1.0), close_u=True)


def roof(m):
    """The domed roof: rings shrinking to a flat crown, with the Triforce inlays laid on it separately."""
    lac = P.R["lacquer"]
    prof = [(EAVE + 6, -10.0), (EAVE + 12, 0.0), (EAVE + 18, 14.0), (ROOF_TOP - 2, 34.0), (ROOF_TOP, 60.0)]
    rows = [shell_ring(y, ins, seg=6) for y, ins in prof]
    m.grid(rows, lac, 1, rep=(8.0, 1.0), close_u=True)
    ring = shell_ring(ROOF_TOP, 60.0, seg=6)
    c = m.v(V(0, ROOF_TOP, 0))
    ids = [m.v(p) for p in ring]
    for i in range(len(ids)):
        j = (i + 1) % len(ids)
        m.face([c, ids[i], ids[j]], [P.uv(P.R["lacquer_roof"], 0.5, 0.5), P.uv(P.R["lacquer_roof"], (ring[i].x / 220 + 1) / 2, (ring[i].z / 560 + 1) / 2),
                                     P.uv(P.R["lacquer_roof"], (ring[j].x / 220 + 1) / 2, (ring[j].z / 560 + 1) / 2)], 1)
    # two big flat gold Triforces inlaid near each end of the roof (a hair above the crown)
    for zc in (-330.0, 330.0):
        s = 70.0
        y = ROOF_TOP + 0.8
        pts = [V(-s, y, zc - s), V(s, y, zc - s), V(s, y, zc + s), V(-s, y, zc + s)]
        if zc < 0:
            pts = [V(s, y, zc + s), V(-s, y, zc + s), V(-s, y, zc - s), V(s, y, zc - s)]
        m.poly(pts, [P.uv(P.R["triforce"], 0, 1), P.uv(P.R["triforce"], 1, 1), P.uv(P.R["triforce"], 1, 0), P.uv(P.R["triforce"], 0, 0)], 0)


def window_band(m):
    """The window band on both flanks: seven arched windows a side with brass frames and reveals, pillars between, round brass portholes over
    them. The windows are open (you see out from your seat, and the riders are seen from outside)."""
    lac, brass = P.R["lacquer"], P.R["brass"]
    span = WIN_COUNT * WIN_PITCH
    z0 = -span / 2
    arch_r = WIN_W / 2
    arch_c = BAND_TOP - 10 - arch_r      # the arch's centre height
    depth = 14.0                         # the wall's thickness, shown in the reveals
    for side in (-1, 1):
        x = side * HALF_W
        xi = side * (HALF_W - depth)
        # the solid band from the corners to the first and after the last window
        for za, zb in ((BACK + CORNER, z0), (z0 + span, FRONT - CORNER - 110.0)):
            pts = [V(x, SILL, za), V(x, SILL, zb), V(x, BAND_TOP, zb), V(x, BAND_TOP, za)]
            uv = [P.uv(lac, 0, 1), P.uv(lac, 1, 1), P.uv(lac, 1, 0), P.uv(lac, 0, 0)]
            if side < 0:
                pts.reverse(); uv.reverse()
            m.poly(pts, uv, 1)
        for w in range(WIN_COUNT):
            ua, ub = z0 + w * WIN_PITCH, z0 + (w + 1) * WIN_PITCH
            wc = (ua + ub) / 2
            wa, wb = wc - arch_r, wc + arch_r
            # the opening's outline: up the back side, round the arch, down the front side
            outline = [(wa, SILL), (wa, arch_c)]
            for k in range(1, 12):
                a = math.pi - math.pi * k / 12
                outline.append((wc + arch_r * math.cos(a), arch_c + arch_r * math.sin(a)))
            outline += [(wb, arch_c), (wb, SILL)]
            # the wall round it: pillars at both sides and the spandrel over the arch, as a fan from the unit's top corners
            def wall(pts, uv):
                if side < 0:
                    pts = list(reversed(pts)); uv = list(reversed(uv))
                m.poly(pts, uv, 1)
            def luv(z, y):
                return P.uv(lac, (z - ua) / WIN_PITCH, 1 - (y - SILL) / (BAND_TOP - SILL))
            wall([V(x, SILL, ua), V(x, SILL, wa), V(x, arch_c, wa), V(x, BAND_TOP, ua)], [luv(ua, SILL), luv(wa, SILL), luv(wa, arch_c), luv(ua, BAND_TOP)])
            wall([V(x, SILL, wb), V(x, SILL, ub), V(x, BAND_TOP, ub), V(x, arch_c, wb)], [luv(wb, SILL), luv(ub, SILL), luv(ub, BAND_TOP), luv(wb, arch_c)])
            arch = outline[1:-1]
            half = len(arch) // 2
            for k in range(half):
                (za_, ya_), (zb_, yb_) = arch[k], arch[k + 1]
                wall([V(x, BAND_TOP, ua), V(x, ya_, za_), V(x, yb_, zb_)], [luv(ua, BAND_TOP), luv(za_, ya_), luv(zb_, yb_)])
            for k in range(half, len(arch) - 1):
                (za_, ya_), (zb_, yb_) = arch[k], arch[k + 1]
                wall([V(x, BAND_TOP, ub), V(x, ya_, za_), V(x, yb_, zb_)], [luv(ub, BAND_TOP), luv(za_, ya_), luv(zb_, yb_)])
            (za_, ya_) = arch[half]
            wall([V(x, BAND_TOP, ua), V(x, ya_, za_), V(x, BAND_TOP, ub)], [luv(ua, BAND_TOP), luv(za_, ya_), luv(ub, BAND_TOP)])
            # the reveal (the wall's thickness, in dark lacquer) and the brass frame: a round moulding round the arch
            for k in range(len(outline) - 1):
                (za_, ya_), (zb_, yb_) = outline[k], outline[k + 1]
                pts = [V(x, ya_, za_), V(x, yb_, zb_), V(xi, yb_, zb_), V(xi, ya_, za_)]
                uv = [P.uv(P.R["lacquer_dark"], 0, 0), P.uv(P.R["lacquer_dark"], 1, 0), P.uv(P.R["lacquer_dark"], 1, 1), P.uv(P.R["lacquer_dark"], 0, 1)]
                if side > 0:
                    pts.reverse(); uv.reverse()
                m.poly(pts, uv, 1)
            frame = [V(x + side * 3.0, y, z) for z, y in outline]
            m.merge(tube(frame, 4.5, 8, brass, 0, rep_v=4.0))
            # the porthole above: a brass ring with dark glass (painted)
            pc = V(x + side * 1.0, BAND_TOP + 22.0, wc)
            s = 15.0
            pts = [pc + V(0, -s, -s), pc + V(0, -s, s), pc + V(0, s, s), pc + V(0, s, -s)]
            uv = [P.uv(P.R["porthole"], 0, 1), P.uv(P.R["porthole"], 1, 1), P.uv(P.R["porthole"], 1, 0), P.uv(P.R["porthole"], 0, 0)]
            if side < 0:
                pts.reverse(); uv.reverse()
            m.poly(pts, uv, 0)
            ring = [pc + V(side * 2.0, 14.0 * math.sin(a), 14.0 * math.cos(a)) for a in (k / 16 * 2 * math.pi for k in range(16))]
            m.merge(tube(ring, 2.6, 6, brass, 0, closed=True))
        # the interior side of the band: the inner wall face of the pillars is the reveal; the inner face of the lower body
        pts = [V(xi, SKIRT + 30, BACK + CORNER), V(xi, SKIRT + 30, FRONT - CORNER - 110), V(xi, SILL, FRONT - CORNER - 110), V(xi, SILL, BACK + CORNER)]
        uv = [P.uv(P.R["wood"], 0, 1), P.uv(P.R["wood"], 6, 1), P.uv(P.R["wood"], 6, 0), P.uv(P.R["wood"], 0, 0)]
        uv = [P.uv(P.R["wood"], 0, 1), P.uv(P.R["wood"], 1, 1), P.uv(P.R["wood"], 1, 0), P.uv(P.R["wood"], 0, 0)]
        if side > 0:
            pts.reverse(); uv.reverse()
        m.poly(pts, uv, 0)


def front_and_back(m):
    """The pilot's glass front (an open brass-framed bay), the band at the front corners, the rear wall with its window, door and Triforce."""
    lac, brass = P.R["lacquer"], P.R["brass"]
    zf = FRONT - CORNER - 110.0
    # the flank between the last window and the front corner: the pilot's side window, a big open rounded opening
    for side in (-1, 1):
        x = side * HALF_W
        pts_l = [(zf, SILL), (zf, BAND_TOP), (FRONT - CORNER, BAND_TOP), (FRONT - CORNER, SILL)]
        # frame only (open): a brass rounded rectangle
        r = 18.0
        za, zb, ya, yb = zf + 10, FRONT - CORNER - 6, SILL + 8, BAND_TOP - 10
        loop = []
        for cz, cy, a0 in ((zb - r, yb - r, 0), (za + r, yb - r, 90), (za + r, ya + r, 180), (zb - r, ya + r, 270)):
            for k in range(5):
                a = math.radians(a0 + 90 * k / 4)
                loop.append(V(x + side * 3, cy + r * math.sin(a), cz + r * math.cos(a)))
        m.merge(tube(loop, 4.5, 8, brass, 0, closed=True))
        # the wall round the opening: thin strips
        for (z0, z1, y0, y1) in ((zf, za, SILL, BAND_TOP), (zb, FRONT - CORNER, SILL, BAND_TOP), (za, zb, yb, BAND_TOP), (za, zb, SILL, ya)):
            pts = [V(x, y0, z0), V(x, y0, z1), V(x, y1, z1), V(x, y1, z0)]
            uv = [P.uv(lac, 0, 1), P.uv(lac, 1, 1), P.uv(lac, 1, 0), P.uv(lac, 0, 0)]
            if side < 0:
                pts.reverse(); uv.reverse()
            m.poly(pts, uv, 1)
    # the rounded corners in the band (front and back): lacquer strips following the plan's corners
    ring_lo, ring_hi = shell_ring(SILL, 0.0), shell_ring(BAND_TOP, 0.0)
    n = len(ring_lo)
    per = n // 4
    for ci in range(4):
        rows = [ring_lo[ci * per: ci * per + per], ring_hi[ci * per: ci * per + per]]
        if ci in (0, 1):   # the front corners: open glass, only the brass posts
            continue
        m.grid(rows, lac, 1)
    # the front: the glass bay is open; brass posts at the corners and a brass rail at the sill and the top
    for ci in (0, 1):
        pts = ring_lo[ci * per: ci * per + per]
        for p in pts[::2]:
            m.merge(cylinder(V(p.x, SILL, p.z), V(p.x, BAND_TOP, p.z), 4.0, 8, brass, 0, caps=False))
    front_line = [p for p in ring_lo if p.z > FRONT - CORNER - 1]
    front_line.sort(key=lambda p: p.x)
    for y in (SILL + 2, BAND_TOP - 2):
        m.merge(tube([V(p.x, y, p.z + 1.5) for p in front_line], 5.0, 8, brass, 0, rep_v=3))
    # a few glazing bars across the front
    for xb in (-55.0, 55.0):
        m.merge(cylinder(V(xb, SILL, FRONT + 1), V(xb, BAND_TOP, FRONT + 1), 3.0, 6, brass, 0, caps=False))
    # the rear wall in the band: lacquer with a small arched window (painted glass) and a Triforce under it
    zb = BACK
    x0, x1 = -(HALF_W - CORNER), HALF_W - CORNER
    pts = [V(x1, SILL, zb), V(x0, SILL, zb), V(x0, BAND_TOP, zb), V(x1, BAND_TOP, zb)]
    m.poly(pts, [P.uv(P.R["rear"], 0, 1), P.uv(P.R["rear"], 1, 1), P.uv(P.R["rear"], 1, 0), P.uv(P.R["rear"], 0, 0)], 0)
    # the inner face of the rear wall (so it is not see-through from inside)
    m.poly(list(reversed(pts)), [P.uv(P.R["wood"], 0, 1), P.uv(P.R["wood"], 0, 0), P.uv(P.R["wood"], 1, 0), P.uv(P.R["wood"], 1, 1)], 0)
    # Triforce plaque under the rear window, on the lower panel
    plaque(m, V(0, 18.0, BACK - 1.5), V(0, 0, -1), 46.0, 30.0)


def plaque(m, c, n, w, h, region=None):
    """A raised crest plaque (red with a gold Triforce, gold rim) facing n."""
    region = region or P.R["crest"]
    up = V(0, 1, 0)
    side = up.cross(n).normalized()
    d = 3.0
    corners = [c - side * w / 2 - up * h / 2, c + side * w / 2 - up * h / 2, c + side * w / 2 + up * h / 2, c - side * w / 2 + up * h / 2]
    front = [p + n * d for p in corners]
    m.poly(front, [P.uv(region, 0, 1), P.uv(region, 1, 1), P.uv(region, 1, 0), P.uv(region, 0, 0)], 0)
    for i in range(4):
        j = (i + 1) % 4
        m.poly([corners[i], corners[j], front[j], front[i]], [P.uv(P.R["gold"], 0, 0), P.uv(P.R["gold"], 1, 0), P.uv(P.R["gold"], 1, 1), P.uv(P.R["gold"], 0, 1)], 0)


def flank_details(m):
    """The glowing Sheikah rune line along the waist, the crest plaques with the Triforce on both flanks."""
    for side in (-1, 1):
        x = side * (HALF_W + 0.8)
        y0, y1 = SILL - 18, SILL - 6
        for k in range(8):
            za = BACK + CORNER + (FRONT - BACK - 2 * CORNER) * k / 8
            zb = BACK + CORNER + (FRONT - BACK - 2 * CORNER) * (k + 1) / 8
            pts = [V(x, y0, za), V(x, y0, zb), V(x, y1, zb), V(x, y1, za)]
            uv = [P.uv(P.R["runes"], 0, 1), P.uv(P.R["runes"], 1, 1), P.uv(P.R["runes"], 1, 0), P.uv(P.R["runes"], 0, 0)]
            if side < 0:
                pts.reverse(); uv.reverse()
            m.poly(pts, uv, 2)   # glow
        plaque(m, V(side * (HALF_W + 0.5), 4.0, 0.0), V(side, 0, 0), 92.0, 50.0)


def hood(m):
    """The short hood in front of the pilot's bay: rounded, a brass grille, cyan lamps, the Triforce plaque, the bronze horse-head prow."""
    lac, brass = P.R["lacquer"], P.R["brass"]
    hz0, hz1 = FRONT - 30, HOOD_FRONT
    rows = []
    for k in range(7):
        z = hz0 + (hz1 - hz0) * k / 6
        t = k / 6
        hw = 128.0 - 22.0 * t * t
        top = 92.0 - 30.0 * t * t
        ring = []
        # a rounded section: bottom at the skirt, top rounded
        for j in range(13):
            a = math.pi * j / 12   # 0 at right bottom ... pi at left bottom, through the top
            rr = 30.0
            # rounded rectangle path: right side up, round top, left side down
            if j <= 3:
                p = (hw, SKIRT + 10 + (top - rr - SKIRT - 10) * j / 3)
            elif j <= 9:
                b = math.pi * (j - 3) / 6
                p = (hw - rr + rr * math.cos(b) - (hw - rr) * (1 - math.cos(b)) * 0 , top - rr + rr * math.sin(b))
                p = ((hw - rr) * math.cos(b) + rr * math.cos(b), top - rr + rr * math.sin(b))
            else:
                p = (-hw, top - rr - (top - rr - SKIRT - 10) * (j - 9) / 3)
            ring.append(V(p[0], p[1], z))
        rows.append(ring)
    m.grid(rows, lac, 1, rep=(3.0, 1.0), flip=True)
    # the hood's nose: the front face, a brass grille panel
    ring = rows[-1]
    c = m.v(V(0, 20.0, hz1 + 6))
    ids = [m.v(p) for p in ring]
    for i in range(len(ids) - 1):
        a, b = ring[i], ring[i + 1]
        m.face([c, ids[i], ids[i + 1]], [P.uv(P.R["grille"], 0.5, 0.5), P.uv(P.R["grille"], (a.x / 110 + 1) / 2, 1 - (a.y - SKIRT) / 150),
                                         P.uv(P.R["grille"], (b.x / 110 + 1) / 2, 1 - (b.y - SKIRT) / 150)], 0)
    m.face([c, ids[-1], ids[0]], [P.uv(P.R["grille"], 0.5, 0.5), P.uv(P.R["grille"], 0, 1), P.uv(P.R["grille"], 1, 1)], 0)
    # gold trim along the hood's edge
    m.merge(tube([V(p.x * 1.02, p.y + 2, hz1 + 2) for p in ring], 4.0, 8, P.R["gold"], 0))
    # the lamps: cyan glass domes on brass cups at the front corners
    for side in (-1, 1):
        lc = V(side * 92.0, 30.0, hz1 + 4)
        m.merge(lathe([(18, -6), (20, 0), (18, 6), (0, 8)], 16, brass, 0, center=(lc.x, lc.y, lc.z), axis="z", cap_bot=True))
        m.merge(ellipsoid((lc.x, lc.y, lc.z + 8), 15, 15, 9, 16, 8, P.R["lamp"], 2))
    # the Triforce plaque on the hood's front
    plaque(m, V(0, -18.0, hz1 + 3.0), V(0, 0, 1), 52.0, 32.0)
    # the side walls between the hood and the body (the hood's back is inside the body)


def horse_head(m):
    """Epona as the Master Cycle shows her: a bronze horse head and neck on the hood, a mane of gold fins, a glowing cyan eye."""
    bronze, gold = P.R["bronze"], P.R["gold"]
    base = V(0, 70.0, HOOD_FRONT - 70)
    # the neck: a tube rising forward, thick at the base
    path = [base + V(0, 0, 0), base + V(0, 26, 10), base + V(0, 52, 26), base + V(0, 72, 46)]
    radii = [30, 26, 22, 20]
    rows = []
    for p, r in zip(path, radii):
        rows.append([p + V(r * 0.8 * math.sin(a), r * 0.35 * math.cos(a) * 0, r * math.cos(a)) * 1.0 for a in (k / 12 * 2 * math.pi for k in range(12))])
    neck = Mesh()
    rows = []
    for i, (p, r) in enumerate(zip(path, radii)):
        rows.append([p + V(r * 0.75 * math.sin(a), 0, r * math.cos(a)) for a in (k / 12 * 2 * math.pi for k in range(12))])
    neck.grid(rows, bronze, 0, close_u=True, flip=True)
    m.merge(neck)
    # the head: an ellipsoid tapered to the muzzle, angled down and forward
    def taper(p, n):
        hc = base + V(0, 84, 74)
        q = p - hc
        k = (q.z + 40) / 80.0      # 0 at the back of the head, 1 at the muzzle
        s = 1.0 - 0.35 * max(0.0, min(1.0, k))
        return hc + V(q.x * s, q.y * s - 10.0 * max(0.0, k) ** 2, q.z)
    m.merge(ellipsoid((base.x, base.y + 84, base.z + 74), 20, 24, 44, 16, 10, bronze, 0, fn=taper))
    # the ears
    for side in (-1, 1):
        e0 = base + V(side * 10, 102, 50)
        m.merge(lathe([(6, 0), (5, 10), (2, 20), (0.3, 26)], 8, bronze, 0, center=(e0.x, e0.y, e0.z), cap_bot=True))
        # the eye: a glowing cyan bead
        m.merge(ellipsoid((side * 15.5, base.y + 92, base.z + 80), 4.5, 5.5, 6.5, 10, 6, P.R["lamp"], 2))
    # the mane: gold fins down the neck
    for i in range(5):
        t = i / 4
        p = base + V(0, 30 + 70 * t, 8 + 40 * t - 22)
        fin = [p + V(0, 0, 0), p + V(0, 22, -10), p + V(0, 6, 14)]
        for side in (-1.5, 1.5):
            q = [f + V(side, 0, 0) for f in fin]
            pts = q if side > 0 else list(reversed(q))
            uv = [P.uv(gold, 0, 1), P.uv(gold, 0.5, 0), P.uv(gold, 1, 1)]
            m.poly(pts, uv if side > 0 else list(reversed(uv)), 0)


def interior(m):
    """The floor, the 32 benches (two rows of 16, saddle-shaped so Link's riding sit fits), the pilot's stool and the brass tiller."""
    wood, cloth = P.R["wood"], P.R["cloth"]
    # the floor: planks from the back wall to the pilot's bay
    y = 0.0
    x = HALF_W - 14
    pts = [V(x, y, BACK + 14), V(-x, y, BACK + 14), V(-x, y, FRONT - 14), V(x, y, FRONT - 14)]
    m.poly(pts, [P.uv(wood, 0, 0), P.uv(wood, 1, 0), P.uv(wood, 1, 1), P.uv(wood, 0, 1)], 0)
    # the ceiling (the roof's underside, dark wood)
    yc = EAVE - 4
    m.poly(list(reversed([V(x, yc, BACK + 14), V(-x, yc, BACK + 14), V(-x, yc, FRONT - 14), V(x, yc, FRONT - 14)])),
           [P.uv(P.R["wood_dark"], 0, 0), P.uv(P.R["wood_dark"], 0, 1), P.uv(P.R["wood_dark"], 1, 1), P.uv(P.R["wood_dark"], 1, 0)], 0)
    for s in range(SEAT_ROWS * SEATS_PER_ROW):
        sx, sy, sz = seat_local(s)
        # the bench: a padded saddle on a wooden frame, a low back rest
        m.merge(rbox(sx, sy - 6, sz, 44, 12, 38, 5, cloth, 0, seg=1))
        m.merge(box(sx, (sy - 12) / 2, sz, 34, sy - 12, 30, P.R["wood_dark"], 0))
        m.merge(box(sx, sy + 16, sz - 22, 44, 30, 6, P.R["wood_dark"], 0))
    # the pilot's stool and the tiller
    m.merge(lathe([(0, 0), (20, 0), (16, 30), (22, 34), (0, 36)], 14, P.R["wood_dark"], 0, center=(0, 0, FRONT - 70)))
    m.merge(cylinder(V(0, 20, FRONT - 20), V(0, 70, FRONT - 34), 3.5, 8, P.R["brass"], 0))
    m.merge(cylinder(V(-22, 70, FRONT - 34), V(22, 70, FRONT - 34), 3.5, 8, P.R["brass"], 0))


def seat_local(s):
    s = max(0, min(SEAT_ROWS * SEATS_PER_ROW - 1, s))
    row, along = s % SEAT_ROWS, s // SEAT_ROWS
    x = -ROW_GAP / 2 if row == 0 else ROW_GAP / 2
    z = ((SEATS_PER_ROW - 1) * 0.5 - along) * SEAT_PITCH
    return x, SEAT_H, z


def rear_deck(m):
    """The wooden deck behind the coach with its brass rail and three boarding steps; the propeller's brass arm bolted to it and braced to the
    rail; the pennant pole on its corner."""
    wood, brass = P.R["wood"], P.R["brass"]
    d0, d1 = BACK, BACK - 100
    m.merge(rbox(0, -6, (d0 + d1) / 2, 2 * (HALF_W - 26), 14, d0 - d1, 4, wood, 0, seg=2))
    # rail posts and the rail
    posts = [V(-(HALF_W - 34), 0, d0 - 4), V(-(HALF_W - 34), 0, d1 + 8), V(-40, 0, d1 + 8), V(40, 0, d1 + 8), V(HALF_W - 34, 0, d1 + 8), V(HALF_W - 34, 0, d0 - 4)]
    for p in posts:
        m.merge(cylinder(p, p + V(0, 62, 0), 3.5, 8, brass, 0))
    m.merge(tube([p + V(0, 62, 0) for p in posts[:3]], 3.5, 8, brass, 0))
    m.merge(tube([p + V(0, 62, 0) for p in posts[3:]], 3.5, 8, brass, 0))
    # three steps down from the gap in the rail
    for k in range(3):
        m.merge(rbox(0, -24 - 18 * k, d1 - 14 - 16 * k, 76, 6, 22, 2, wood, 0, seg=1))
        for side in (-1, 1):
            m.merge(cylinder(V(side * 38, -8, d1 - 4), V(side * 38, -24 - 18 * k, d1 - 14 - 16 * k), 2.0, 6, brass, 0))
    # the propeller's arm: from a bolted plate on the deck up and back to the hub, braced to the rail
    plate = V(0, 2, d1 + 30)
    m.merge(rbox(plate.x, plate.y, plate.z, 50, 6, 40, 2, brass, 0, seg=1))
    elbow = V(0, PROP_HUB[1], d1 + 30)
    m.merge(cylinder(plate, elbow, 9.0, 12, brass, 0))
    m.merge(cylinder(elbow, V(PROP_HUB[0], PROP_HUB[1], PROP_HUB[2] + 26), 8.0, 12, brass, 0))
    m.merge(ellipsoid((elbow.x, elbow.y, elbow.z), 12, 12, 12, 12, 6, brass, 0))
    for side in (-1, 1):
        rail = posts[2 if side < 0 else 3] + V(0, 62, 0)
        m.merge(cylinder(rail, V(0, PROP_HUB[1] - 4, (d1 + 30 + PROP_HUB[2]) / 2), 2.8, 6, brass, 0))
    # the bearing at the hub end
    m.merge(lathe([(13, 0), (13, 14), (9, 20)], 14, brass, 0, center=(PROP_HUB[0], PROP_HUB[1], PROP_HUB[2] + 26), axis="z", cap_bot=False))
    # the pennant pole on the deck's corner
    pole = V(HALF_W - 34, 0, d1 + 8)
    m.merge(cylinder(pole + V(0, 62, 0), pole + V(0, 300, 0), 3.0, 8, P.R["wood_dark"], 0))
    m.merge(ellipsoid((pole.x, 304, pole.z), 6, 6, 6, 10, 6, P.R["gold"], 0))


def cage_and_firebox(m):
    """The brass burner cage on the roof: a base plate, four posts, the ring the ropes tie to; the Goron firebox inside it (an iron stove banded
    with rock, a bomb flower glowing in its mouth) and the burner nozzle; a sandbag hung on a short line from each corner of the ring."""
    brass, iron = P.R["brass"], P.R["iron"]
    yb = ROOF_TOP
    m.merge(lathe([(0, 0), (112, 0), (116, 4), (108, 9), (0, 9)], 32, brass, 0, center=(0, yb, 0)))
    corners = [V(CAGE_POST * sx, 0, CAGE_POST * sz) for sx, sz in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
    for c in corners:
        m.merge(cylinder(V(c.x, yb + 8, c.z), V(c.x * 1.12, RING_Y, c.z * 1.12), 5.5, 10, brass, 0))
        m.merge(ellipsoid((c.x * 1.12, RING_Y, c.z * 1.12), 9, 9, 9, 10, 6, brass, 0))
    ring = [V(RING_R * math.sin(a), RING_Y, RING_R * math.cos(a)) for a in (k / 40 * 2 * math.pi for k in range(40))]
    m.merge(tube(ring, 6.5, 10, brass, 0, closed=True, rep_v=6))
    mid = [V(RING_R * 0.93 * math.sin(a), yb + 120, RING_R * 0.93 * math.cos(a)) for a in (k / 32 * 2 * math.pi for k in range(32))]
    m.merge(tube(mid, 3.5, 8, brass, 0, closed=True, rep_v=4))
    # the firebox: an iron drum with two rock bands, a domed lid and a nozzle
    fb = [(0, 0), (52, 0), (56, 8), (56, 30), (60, 34), (60, 46), (56, 50), (56, 92), (60, 96), (60, 108), (56, 112), (52, 128), (30, 140), (16, 152),
          (18, 166), (12, 172)]
    m.merge(lathe(fb, 28, iron, 0, center=(0, yb + 9, 0), rep_u=3.0))
    for y0 in (34.0, 96.0):
        m.merge(lathe([(60, 0), (64, 2), (64, 10), (60, 12)], 28, P.R["stone"], 0, center=(0, yb + 9 + y0, 0), rep_u=4.0))
    # the bomb flower glowing in the stove's mouth (forward)
    mc = V(0, yb + 72, 56)
    pts = [mc + V(-24, -18, 1), mc + V(24, -18, 1), mc + V(24, 18, 1), mc + V(-24, 18, 1)]
    m.poly(pts, [P.uv(P.R["ember"], 0, 1), P.uv(P.R["ember"], 1, 1), P.uv(P.R["ember"], 1, 0), P.uv(P.R["ember"], 0, 0)], 2)
    m.merge(tube([mc + V(26 * math.cos(a), 20 * math.sin(a), 2) for a in (k / 16 * 2 * math.pi for k in range(16))], 3.5, 6, brass, 0, closed=True))
    # sandbags on short lines from the ring, at the posts
    for c in corners:
        top = V(c.x * 1.12, RING_Y - 4, c.z * 1.12)
        out = V(c.x, 0, c.z).normalized() * 18
        bag = top + out + V(0, -52, 0)
        m.merge(cylinder(top + out * 0.3, bag + V(0, 16, 0), 1.6, 5, P.R["rope"], 0))
        m.merge(ellipsoid((bag.x, bag.y, bag.z), 15, 18, 15, 12, 8, P.R["burlap"], 0))
        m.merge(ellipsoid((bag.x, bag.y + 16, bag.z), 6, 4, 6, 8, 4, P.R["rope"], 0))


def lantern_hooks(m):
    """The brass hooks under the eaves the lanterns hang from (the lanterns are their own part, so they can swing)."""
    brass = P.R["brass"]
    for side in (-1, 1):
        for z in LANTERN_Z:
            a = V(side * (HALF_W + 2), EAVE + 2, z)
            b = V(side * (HALF_W + 26), EAVE + 2, z)
            m.merge(cylinder(a, b, 2.6, 8, brass, 0))
            m.merge(cylinder(b, b + V(0, -12, 0), 2.0, 6, brass, 0))


def wheel_arms(m):
    """The iron arms that hold the tucked-up wheels under the skirt."""
    for sx in (-1, 1):
        for sz in (-1, 1):
            a = V(sx * (HALF_W - 30), SKIRT + 8, sz * WHEEL_Z)
            b = V(sx * (WHEEL_X - 16), WHEEL_Y, sz * WHEEL_Z)
            m.merge(cylinder(a, b, 7.0, 10, P.R["iron"], 0))
            m.merge(rbox(a.x, a.y + 4, a.z, 30, 10, 40, 3, P.R["iron"], 0, seg=1))


# ---- the moving parts ------------------------------------------------------------------------------------------------------------
def face_frame():
    """The Moon's face directions: F out of the face (forward and down), U to the face's own left (the viewer's right), Vv up the face."""
    F = V(0, -math.sin(FACE_TILT), math.cos(FACE_TILT))
    Vv = V(0, math.cos(FACE_TILT), math.sin(FACE_TILT))
    U = Vv.cross(F).normalized()     # +x on the bus (the viewer's right when facing the Moon)
    return F, U, Vv


EYES = [(-0.31, 0.24), (0.31, 0.24)]   # face coordinates (u, v) of the eyes' centres (u toward the viewer's right)


def moon_displace(n):
    """How far the Moon's surface moves in or out along its normal n (units): the face modelled in, craters elsewhere."""
    F, U, Vv = face_frame()
    w, u, v = n.dot(F), -n.dot(U), n.dot(Vv)
    h = 0.0
    if w > 0.2:
        fade = min(1.0, (w - 0.2) / 0.25)
        def blob(cu, cv, ru, rv, amp, sharp=2.0):
            d2 = ((u - cu) / ru) ** 2 + ((v - cv) / rv) ** 2
            return amp * math.exp(-d2 * sharp)
        # sunken sockets, the sagging bags below them, the cheeks
        for cu, cv in EYES:
            h += blob(cu, cv, 0.17, 0.15, -42.0)
            h += blob(cu, cv - 0.17, 0.15, 0.05, 12.0)
            h += blob(cu * 1.35, cv - 0.30, 0.13, 0.12, 10.0)
        # the brows: thick ridges slanted hard inward and down
        for sgn in (-1, 1):
            for t in range(9):
                k = t / 8
                cu = sgn * (0.52 - 0.40 * k)
                cv = 0.44 - 0.16 * k
                h += blob(cu, cv, 0.06, 0.045, 13.0 + 6.0 * k)
        # the crease between the eyes and the two flared nostrils (no nose)
        h += blob(0.0, 0.25, 0.025, 0.12, -10.0)
        for sgn in (-1, 1):
            h += blob(sgn * 0.075, 0.03, 0.035, 0.03, -22.0)
            h += blob(sgn * 0.09, 0.05, 0.06, 0.05, 7.0)
        # the grin: a deep wide mouth, lips pulled back (a raised rim)
        mu, mv, mw, mh = 0.0, -0.30, 0.56, 0.15
        e = ((u - mu) / mw) ** 2 + ((v - mv - 0.25 * u * u) / mh) ** 2
        if e < 1.0:
            h -= 46.0 * (1.0 - e) ** 0.6
        h += 10.0 * math.exp(-((math.sqrt(e) - 1.08) ** 2) / 0.006)
        # creases from the nostrils down past the mouth's corners
        for sgn in (-1, 1):
            for t in range(10):
                k = t / 9
                h += blob(sgn * (0.12 + 0.50 * k), 0.0 - 0.42 * k, 0.03, 0.03, -6.0)
        h *= fade
    # craters (not on the face)
    rng = random.Random(7)
    for _ in range(26):
        lon, lat = rng.uniform(0, 2 * math.pi), math.asin(rng.uniform(-0.95, 0.95))
        c = V(math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon))
        if c.dot(F) > 0.35:
            continue
        rad = rng.uniform(0.08, 0.22)
        d = (n - c).length / rad
        if d < 1.4:
            h += (-18.0 * (1 - d * d) if d < 1.0 else 6.0 * math.sin((d - 1.0) / 0.4 * math.pi)) * rad / 0.15
    return h


def moon(parts):
    """The Moon: a cratered sphere with the face modelled in, its bulging eyes, two even rows of square teeth, the rope net over its lower half
    ending in the dark collar at its mouth, and the knots the bus's ropes tie to."""
    m = Mesh()
    R = MOON_R
    c = V(*MOON_C) - V(*MOON_PIVOT)     # relative to the pivot
    F, U, Vv = face_frame()
    seg, rings = 80, 40
    rows, normals = [], []
    for j in range(rings + 1):
        lat = -math.pi / 2 + math.pi * j / rings
        row, nrow = [], []
        for i in range(seg + 1):
            lon = 2 * math.pi * i / seg + math.pi    # the seam at the back
            n = V(math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon))
            row.append(c + n * (R + moon_displace(n)))
            nrow.append(n)
        rows.append(row)
        normals.append(nrow)
    ids = [[m.v(p) for p in row] for row in rows]
    # face texture: planar projection on the face plane for the front cap; elsewhere the tiled rock texture
    def face_uv(n):
        return (0.5 + (-n.dot(U)) * 0.5, 0.5 + n.dot(Vv) * 0.5)   # Blender's v is up
    for j in range(rings):
        for i in range(seg):
            q = [ids[j][i], ids[j][i + 1], ids[j + 1][i + 1], ids[j + 1][i]]
            ns = [normals[j][i], normals[j][i + 1], normals[j + 1][i + 1], normals[j + 1][i]]
            uv = [(i / seg * 4.0, j / rings * 2.0), ((i + 1) / seg * 4.0, j / rings * 2.0), ((i + 1) / seg * 4.0, (j + 1) / rings * 2.0),
                  (i / seg * 4.0, (j + 1) / rings * 2.0)]
            m.face(list(reversed(q)), list(reversed(uv)), 4)
    # the face: a decal over the front of the rock, its own part, fading out at its edge (vertex alpha) so it blends into the rock
    fm = Mesh()
    fids = {}
    def fvert(j, i):
        if (j, i) not in fids:
            n = normals[j][i]
            k = fm.v(rows[j][i] + n * 1.5)
            w = n.dot(F)
            fm.alpha[k] = max(0.0, min(1.0, (w - 0.22) / 0.30)) ** 1.5
            fids[(j, i)] = k
        return fids[(j, i)]
    for j in range(rings):
        for i in range(seg):
            ns = [normals[j][i], normals[j][i + 1], normals[j + 1][i + 1], normals[j + 1][i]]
            if min(n.dot(F) for n in ns) < 0.2:
                continue
            q = [fvert(j, i), fvert(j, i + 1), fvert(j + 1, i + 1), fvert(j + 1, i)]
            uv = [face_uv(n) for n in ns]
            fm.face(list(reversed(q)), list(reversed(uv)), 3)
    parts["MoonFace"] = (fm, MOON_PIVOT)
    def surf(u, v, extra=0.0):
        """A point on the (displaced) surface at face coordinates (u, v), plus `extra` outward."""
        w = math.sqrt(max(0.0, 1 - u * u - v * v))
        n = (F * w - U * u + Vv * v).normalized()
        return c + n * (R + moon_displace(n) + extra), n
    # the teeth: two clean, even rows of ten square teeth across the grin, set in the mouth's dent, a black gap between the rows
    for row, v0, hgt in ((0, -0.235, 52.0), (1, -0.375, 46.0)):
        for k in range(10):
            u = -0.40 + 0.80 * (k + 0.5) / 10
            vv = v0 + 0.25 * u * u
            p, n = surf(u, vv, 0.0)
            p0 = c + n * (R - 30.0)
            # orient: the tooth's face out of the Moon, its height up the face
            up = (Vv - n * Vv.dot(n)).normalized()
            side = up.cross(n).normalized()
            wdt = 33.0
            depth = 34.0
            tc = p0 + n * 10.0 + up * ((-1 if row == 0 else 1) * 0.0)
            tooth = rbox(0, 0, 0, wdt, hgt, depth, 7.0, P.R["teeth"], 0, seg=1)
            xf = Matrix((side.to_4d(), up.to_4d(), n.to_4d(), V(0, 0, 0).to_4d())).transposed()
            xf = Matrix.Translation(tc + n * 6.0 + up * (-hgt * 0.15 if row == 0 else hgt * 0.15)) @ Matrix(
                ((side.x, up.x, n.x, 0), (side.y, up.y, n.y, 0), (side.z, up.z, n.z, 0), (0, 0, 0, 1)))
            m.merge(tooth, xf)
    # the mouth's dark gap: a dark panel set deep behind the teeth
    gap = []
    for k in range(13):
        u = -0.5 + k / 12.0
        p, n = surf(u, -0.305 + 0.25 * u * u, -12.0)
        gap.append(p)
    gap2 = []
    for k in range(13):
        u = -0.5 + k / 12.0
        p, n = surf(u, -0.305 + 0.25 * u * u - 0.0, -12.0)
        gap2.append(p - (Vv * 0.0))
    for k in range(12):
        a, b = gap[k], gap[k + 1]
        n = (a - c).normalized()
        up = (Vv - n * Vv.dot(n)).normalized()
        pts = [a - up * 40, b - up * 40, b + up * 40, a + up * 40]
        m.poly(pts, [P.uv(P.R["mouth"], 0, 1), P.uv(P.R["mouth"], 1, 1), P.uv(P.R["mouth"], 1, 0), P.uv(P.R["mouth"], 0, 0)], 0)
    # the net: meridian ropes from the bottom ring to the collar, and two rings; knots where the bus's ropes tie
    rope = P.R["rope"]
    def on_sphere(lat, lon, lift=5.0):
        n = V(math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon))
        return c + n * (R + lift)
    # under the face the net dips, so no rope crosses the grin
    def ring_lat(lon):
        return NET_LAT - math.radians(24.0) * max(0.0, math.cos(lon)) ** 3
    for k in range(16):
        lon = 2 * math.pi * k / 16 + math.pi / 16
        top = math.radians(-25.0) if math.cos(lon) < 0.3 else ring_lat(lon)
        bot = math.radians(-90.0)
        path = [on_sphere(top + (bot - top) * t / 12, lon) for t in range(13)]
        m.merge(tube(path[:-2], 3.0, 6, rope, 0, rep_v=6))
    m.merge(tube([on_sphere(ring_lat(2 * math.pi * k / 64), 2 * math.pi * k / 64) for k in range(64)], 5.0, 6, rope, 0, closed=True, rep_v=12))
    m.merge(tube([on_sphere(math.radians(-72.0), 2 * math.pi * k / 48) for k in range(48)], 3.5, 6, rope, 0, closed=True, rep_v=12))
    m.merge(tube([on_sphere(math.radians(-25.0), math.radians(75.0 + 210.0 * k / 35)) for k in range(36)], 3.0, 6, rope, 0, rep_v=10))
    knots = []
    for k in range(4):
        lon = math.pi / 4 + k * math.pi / 2
        p = on_sphere(ring_lat(lon), lon, 7.0)
        m.merge(ellipsoid((p.x, p.y, p.z), 10, 10, 10, 10, 6, rope, 0))
        knots.append(p)
    # the collar at the mouth (the Moon's bottom): a dark iron ring round a black opening the flame goes into
    collar = c + V(0, -R, 0)
    m.merge(lathe([(76, 30), (88, 22), (90, 8), (84, 0), (60, -2), (0, -2)], 32, P.R["iron"], 0, center=(collar.x, collar.y, collar.z)))
    parts["Moon"] = (m, MOON_PIVOT)
    # the eyes: bulging glowing balls set in the sockets, their tiny pupils looking in toward the bridge; their own part so they can pulse
    e = Mesh()
    for idx, (cu, cv) in enumerate(EYES):
        p, n = surf(cu, cv, 0.0)
        ec = p + n * (-8.0)
        r = 70.0
        side = (-U if cu < 0 else U)
        up = (Vv - n * Vv.dot(n)).normalized()
        rgt = up.cross(n).normalized()
        look = 0.12 if cu < 0 else -0.12     # pupils toward the middle
        sphere = Mesh()
        rows = []
        uvs = []
        for j in range(17):
            lat = -math.pi / 2 + math.pi * j / 16
            row, urow = [], []
            for i in range(25):
                lon = 2 * math.pi * i / 24
                d = V(math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon))   # +z out of the face
                wp = ec + (rgt * d.x + up * d.y + n * d.z) * r
                row.append(wp)
                urow.append((0.5 + d.x * 0.5 + look, 0.5 - d.y * 0.5))
            rows.append(row); uvs.append(urow)
        sid = [[sphere.v(q) for q in row] for row in rows]
        for j in range(16):
            for i in range(24):
                q = [sid[j][i], sid[j][i + 1], sid[j + 1][i + 1], sid[j + 1][i]]
                uv = [uvs[j][i], uvs[j][i + 1], uvs[j + 1][i + 1], uvs[j + 1][i]]
                uv = [P.uv(P.R["moon_eye"], min(1, max(0, a)), min(1, max(0, b))) for a, b in uv]
                sphere.face(list(reversed(q)), list(reversed(uv)), 2)
        e.merge(sphere)
        # a dark lid line hooding the top of each eye (the lower lid is the bag)
        lid = []
        for t in range(11):
            a = math.radians(20 + 140 * t / 10)
            lid.append(ec + (rgt * math.cos(a) * r * 1.02 + up * math.sin(a) * r * 0.92) + n * (r * 0.42))
        e.merge(tube(lid, 6.0, 8, P.R["far_moon"], 0))
    parts["MoonEyes"] = (e, MOON_PIVOT)
    return knots


def propeller():
    """Four Deku leaves on a brass hub, 450 across, about its axis along the body."""
    m = Mesh()
    brass, leaf = P.R["brass"], P.R["leaf"]
    m.merge(lathe([(0, -30), (12, -28), (20, -18), (22, 0), (18, 12), (8, 22), (0, 24)], 16, brass, 0, axis="z"))
    for b in range(4):
        a0 = b * math.pi / 2 + math.pi / 4
        rows = []
        for j in range(15):
            t = j / 14   # along the leaf, root to tip
            r = 18 + (PROP_R - 18) * t
            half = 6 + 52 * math.sin(math.pi * min(1.0, t * 1.05)) ** 0.8 * (1 - 0.15 * t)
            row = []
            for k in range(7):
                s = -1 + 2 * k / 6
                cup = -12.0 * (1 - s * s) * math.sin(math.pi * t)       # the leaf's cup
                twist = 0.45 * (1 - t) + 0.12
                lx, lz = s * half * math.cos(twist), s * half * math.sin(twist) + cup
                ca, sa = math.cos(a0), math.sin(a0)
                row.append(V(r * ca - lx * sa, r * sa + lx * ca, lz - 4))
            rows.append(row)
        ids = [[m.v(p) for p in row] for row in rows]
        for j in range(14):
            for k in range(6):
                q = [ids[j][k], ids[j][k + 1], ids[j + 1][k + 1], ids[j + 1][k]]
                uv = [P.uv(leaf, k / 6, 1 - j / 14), P.uv(leaf, (k + 1) / 6, 1 - j / 14), P.uv(leaf, (k + 1) / 6, 1 - (j + 1) / 14), P.uv(leaf, k / 6, 1 - (j + 1) / 14)]
                m.face(q, uv, 0)
        # the leaf's stem into the hub
        ca, sa = math.cos(a0), math.sin(a0)
        m.merge(cylinder(V(10 * ca, 10 * sa, -4), V(40 * ca, 40 * sa, -4), 4.0, 6, P.R["leaf_stem"], 0))
    return m


def lantern():
    """A paper lantern on a short chain, its origin at the hook."""
    m = Mesh()
    m.merge(cylinder(V(0, 0, 0), V(0, -10, 0), 1.4, 5, P.R["iron"], 0, caps=False))
    m.merge(lathe([(0, -10), (7, -10), (9, -14), (5, -16)], 12, P.R["brass"], 0))
    prof = [(5, -16)] + [(5 + 12 * math.sin(math.pi * k / 8), -16 - 30 * k / 8) for k in range(1, 8)] + [(5, -46)]
    m.merge(lathe(prof, 16, P.R["lantern"], 2))
    m.merge(lathe([(5, -46), (9, -48), (7, -52), (0, -52)], 12, P.R["brass"], 0))
    m.merge(cylinder(V(0, -52, 0), V(0, -60, 0), 1.6, 5, P.R["cloth"], 0))
    return m


def wheel():
    """A big spoked cart wheel, origin at the axle, axis along x, its outer face (+x) with the glowing Sheikah eye hub."""
    m = Mesh()
    R = WHEEL_R
    m.merge(lathe([(R - 9, -9), (R - 2, -9), (R, -5), (R, 5), (R - 2, 9), (R - 9, 9), (R - 9, -9)], 40, P.R["iron"], 0, axis="x", rep_u=6))
    m.merge(lathe([(R - 9, -7), (R - 19, -7), (R - 19, 7), (R - 9, 7)], 40, P.R["wheel_wood"], 0, axis="x", rep_u=6))
    for k in range(12):
        a = 2 * math.pi * k / 12
        m.merge(cylinder(V(0, 16 * math.sin(a), 16 * math.cos(a)), V(0, (R - 17) * math.sin(a), (R - 17) * math.cos(a)), 3.2, 6, P.R["wheel_wood"], 0))
    m.merge(lathe([(0, -14), (14, -14), (20, -10), (20, 10), (16, 14), (0, 15)], 20, P.R["brass"], 0, axis="x"))
    # the eye cap on the outer face
    s = 13.0
    pts = [V(15.5, -s, s), V(15.5, -s, -s), V(15.5, s, -s), V(15.5, s, s)]
    m.poly(pts, [P.uv(P.R["sheikah_eye"], 0, 1), P.uv(P.R["sheikah_eye"], 1, 1), P.uv(P.R["sheikah_eye"], 1, 0), P.uv(P.R["sheikah_eye"], 0, 0)], 2)
    return m


def rope_unit():
    """A rope one unit long up +Y (the game stretches it between its two ends each frame)."""
    m = Mesh()
    rows = [[V(3.2 * math.sin(a), y, 3.2 * math.cos(a)) for a in (k / 8 * 2 * math.pi for k in range(8))] for y in (0.0, 0.5, 1.0)]
    m.grid(rows, P.R["rope"], 0, rep=(1.0, 6.0), close_u=True, flip=True)
    return m


def banner():
    """The crest pennant: a swallowtail cloth with the Triforce, 24 columns along its length so the game can wave it. Origin at the pole's top;
    it trails back (-z)."""
    m = Mesh()
    L, H = 150.0, 64.0
    cols = 24
    rows = []
    for j in range(5):
        t = j / 4
        row = []
        for i in range(cols + 1):
            s = i / cols
            notch = 0.0
            if s > 0.8:
                notch = (s - 0.8) / 0.2 * 0.45 * (1 - abs(t - 0.5) * 2)
            y = -H * t
            z = -L * (s - notch * 0.0) - (L * 0.35 * (1 - abs(t - 0.5) * 2) * max(0.0, s - 0.8) / 0.2) * 0.0
            # taper toward the tail and the swallowtail notch in the middle
            yy = -H * 0.5 + (y + H * 0.5) * (1 - 0.35 * s)
            zz = -L * s + (notch * L * 0.25)
            row.append(V(0, yy, zz))
        rows.append(row)
    ids = [[m.v(p) for p in row] for row in rows]
    for j in range(4):
        for i in range(cols):
            q = [ids[j][i], ids[j][i + 1], ids[j + 1][i + 1], ids[j + 1][i]]
            uv = [P.uv(P.R["pennant"], i / cols, j / 4), P.uv(P.R["pennant"], (i + 1) / cols, j / 4), P.uv(P.R["pennant"], (i + 1) / cols, (j + 1) / 4),
                  P.uv(P.R["pennant"], i / cols, (j + 1) / 4)]
            m.face(q, uv, 0)
    return m


def flame():
    """The burner's flame: two crossed quads (drawn translucent and flickering in the game), origin at the nozzle."""
    m = Mesh()
    H, W = 110.0, 34.0
    for a in (0.0, math.pi / 2):
        dx, dz = math.cos(a) * W, math.sin(a) * W
        for k in range(3):
            y0, y1 = H * k / 3, H * (k + 1) / 3
            w0, w1 = 1 - 0.25 * k / 3, 1 - 0.25 * (k + 1) / 3
            pts = [V(-dx * w0, y0, -dz * w0), V(dx * w0, y0, dz * w0), V(dx * w1, y1, dz * w1), V(-dx * w1, y1, -dz * w1)]
            v0, v1 = 1 - k / 3, 1 - (k + 1) / 3
            m.poly(pts, [P.uv(P.R["flame"], 0, v0), P.uv(P.R["flame"], 1, v0), P.uv(P.R["flame"], 1, v1), P.uv(P.R["flame"], 0, v1)], 5)
    return m


def far_body():
    """The far LOD: the coach as a rounded box, the cage as a drum (drawn past 4500 units)."""
    m = Mesh()
    m.merge(rbox(0, (SKIRT + ROOF_TOP) / 2, 0, 2 * HALF_W, ROOF_TOP - SKIRT, FRONT - BACK, 40, P.R["far_body"], 0, seg=1))
    m.merge(rbox(0, 20, FRONT + 55, 230, 140, 110, 30, P.R["lacquer"], 1, seg=1))
    m.merge(lathe([(70, 0), (70, RING_Y - ROOF_TOP), (0, RING_Y - ROOF_TOP)], 8, P.R["brass"], 0, center=(0, ROOF_TOP, 0)))
    return m


def far_moon():
    c = V(*MOON_C) - V(*MOON_PIVOT)
    return ellipsoid((c.x, c.y, c.z), MOON_R, MOON_R, MOON_R, 12, 6, P.R["far_moon"], 0)


# ---- into Blender -----------------------------------------------------------------------------------------------------------------
MATERIALS = ["atlas", "atlas_lacquer", "atlas_glow", "moon_face", "moon_rock", "atlas_flame"]


def make_materials(images):
    mats = []
    for name in MATERIALS:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        nt = mat.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.interpolation = "Linear"
        img = images["moon_face" if name == "moon_face" else "moon_rock" if name == "moon_rock" else "bus_atlas"]
        tex.image = img
        if name == "moon_rock":
            tex.extension = "REPEAT"
        else:
            tex.extension = "EXTEND"
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.55 if name != "atlas_lacquer" else 0.3
        if name == "atlas_lacquer":
            bsdf.inputs["Coat Weight"].default_value = 0.5
        if name in ("atlas_glow", "atlas_flame"):
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = 1.6
        if name == "moon_face":
            ca = nt.nodes.new("ShaderNodeVertexColor")
            ca.layer_name = "alpha"
            nt.links.new(ca.outputs["Alpha"], bsdf.inputs["Alpha"])
        if name == "atlas_flame":
            nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
            mat.blend_method = "BLEND" if hasattr(mat, "blend_method") else None
        mats.append(mat)
    return mats


def to_object(name, mesh, pivot, mats, coll, smooth_angle=48.0):
    me = bpy.data.meshes.new(name)
    pv = V(*pivot)
    verts = [g2b(p) for p in mesh.verts]   # mesh points are relative to the pivot
    me.from_pydata([tuple(v) for v in verts], [], [f[0] for f in mesh.faces])
    me.update()
    uvl = me.uv_layers.new(name="UVMap")
    li = 0
    for poly, (idx, uvs, mat) in zip(me.polygons, mesh.faces):
        poly.material_index = mat
        for k, loop in enumerate(poly.loop_indices):
            uvl.data[loop].uv = uvs[k]
    for mt in mats:
        me.materials.append(mt)
    if mesh.alpha:
        attr = me.color_attributes.new(name="alpha", type="FLOAT_COLOR", domain="POINT")
        for i in range(len(mesh.verts)):
            a = mesh.alpha.get(i, 1.0)
            attr.data[i].color = (a, a, a, a)
    # merge duplicate vertices (the builder makes each shape on its own) so the normals are smooth across seams of one shape
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0004)
    bm.to_mesh(me)
    bm.free()
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(smooth_angle))
    ob = bpy.data.objects.new(name, me)
    ob.location = g2b(pv)
    coll.objects.link(ob)
    return ob


def empty(name, p, coll, parent=None):
    e = bpy.data.objects.new(name, None)
    e.empty_display_size = 0.1
    e.location = g2b(p)
    coll.objects.link(e)
    return e


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    images = P.paint_all(OUT)
    mats = make_materials(images)
    coll = bpy.data.collections.new("MoonfallBus")
    bpy.context.scene.collection.children.link(coll)

    parts = {}
    body = Mesh()
    body_shell(body); window_band(body); front_and_back(body); flank_details(body); hood(body); horse_head(body)
    interior(body); rear_deck(body); cage_and_firebox(body); lantern_hooks(body); wheel_arms(body)
    rf = Mesh(); roof(rf)
    parts["BusBody"] = (body, (0, 0, 0))
    parts["Roof"] = (rf, (0, 0, 0))
    knots = moon(parts)
    parts["Propeller"] = (propeller(), PROP_HUB)
    parts["Lantern"] = (lantern(), (HALF_W + 26, EAVE - 10, LANTERN_Z[0]))
    parts["Wheel"] = (wheel(), (WHEEL_X, WHEEL_Y, WHEEL_Z))
    parts["Rope"] = (rope_unit(), (0, 0, 0))
    pole_top = (HALF_W - 34, 296.0, BACK - 92.0)
    parts["Banner"] = (banner(), pole_top)
    nozzle = (0.0, ROOF_TOP + 9 + 172, 0.0)
    parts["Flame"] = (flame(), nozzle)
    parts["BodyFar"] = (far_body(), (0, 0, 0))
    parts["MoonFar"] = (far_moon(), MOON_PIVOT)

    for name, (mesh, pivot) in parts.items():
        to_object(name, mesh, pivot, mats, coll, smooth_angle=40.0 if name in ("BusBody", "Roof") else 55.0)

    # the empties the game reads
    for s in range(SEAT_ROWS * SEATS_PER_ROW):
        empty("Seat%02d" % s, seat_local(s), coll)
    empty("Exit", EXIT, coll)
    empty("Horn", (0.0, 120.0, HOOD_FRONT - 20), coll)
    empty("Smoke", nozzle, coll)
    empty("Pilot", (0.0, 36.0, FRONT - 70), coll)
    empty("LobbyRamp", (0.0, -60.0, BACK - 150), coll)
    for k, z in enumerate(LANTERN_Z):
        empty("Lantern%d" % k, (-(HALF_W + 26), EAVE - 10, z), coll)
        empty("Lantern%d" % (k + 4), (HALF_W + 26, EAVE - 10, z), coll)
    for k, (sx, sz) in enumerate(((1, 1), (-1, 1), (1, -1), (-1, -1))):
        empty("Wheel%d" % k, (sx * WHEEL_X, WHEEL_Y, sz * WHEEL_Z), coll)
    corners = [V(CAGE_POST * sx * 1.12, RING_Y, CAGE_POST * sz * 1.12) for sx, sz in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
    # ropes: from the cage ring's corners up to the knots on the net (the knots are in the Moon's frame); pair each corner with the nearest knot
    for k, cpt in enumerate(corners):
        best = min(knots, key=lambda q: ((q + V(*MOON_PIVOT)) - cpt).length)
        empty("RopeBody%d" % k, cpt, coll)
        empty("RopeMoon%d" % k, best + V(*MOON_PIVOT), coll)

    # the Moon's pendulum sway, the lanterns' swing and the propeller's spin, keyed for the previews (the game does the same in code)
    scn = bpy.context.scene
    scn.render.fps = 20
    scn.frame_start, scn.frame_end = 1, 80
    for name in ("Moon", "MoonFace", "MoonEyes", "MoonFar"):
        ob = bpy.data.objects[name]
        for f in range(1, 82, 4):
            ph = (f - 1) / 80 * 2 * math.pi
            ob.rotation_euler = (math.radians(2.0) * math.sin(ph + 1.3), math.radians(2.0) * math.sin(ph), 0.0)
            ob.keyframe_insert("rotation_euler", frame=f)
    prop = bpy.data.objects["Propeller"]
    for f in (1, 81):
        prop.rotation_euler = (0.0, (f - 1) / 20 * math.radians(540.0), 0.0)
        prop.keyframe_insert("rotation_euler", frame=f)
    path = os.path.join(OUT, "bus.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path)
    tris = 0
    for name in parts:
        me = bpy.data.objects[name].data
        me.calc_loop_triangles()
        tris += len(me.loop_triangles)
        print("%-10s %6d tris" % (name, len(me.loop_triangles)))
    print("wrote %s (%d triangles in all)" % (path, tris))


if __name__ == "__main__":
    main()
