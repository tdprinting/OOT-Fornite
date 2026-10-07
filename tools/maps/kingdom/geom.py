"""Geometry kit for Hyrule Kingdom. Game units (x east, y up, z south). Everything built here becomes, at once:
  * drawn triangles: per material, with texture coordinates (box mapped in world space) and a baked light colour per corner;
  * scene collision (static): exactly the triangles of the solids marked `col='static'`, with a surface (footstep sound, climbable);
  * nearby prop collision boxes (`col='prop'`), and navigation obstacles;
so what you see is what you stand on. Pure python and numpy: the exporter writes C++ from it and the Blender script builds the .blend from it.
"""
import math
import numpy as np

SUN = np.array([-0.45, 0.78, -0.43]); SUN /= np.linalg.norm(SUN)   # late morning, from the south east... (game: -z is north)

# Footstep sounds (z_bgcheck.c D_80119E10): 0 ground, 1 sand, 2 stone, 3 dirt, 8 grass, 10 wood (ladder), 12 ice, 13 iron
SURFACES = {'ground': 0, 'sand': 1, 'stone': 2, 'dirt': 3, 'grass': 8, 'wood': 10, 'ice': 12, 'iron': 13, 'snow': 3}

class Tri:
    __slots__ = ('p', 'uv', 'shade', 'mat', 'group', 'far')
    def __init__(self, p, uv, shade, mat, group, far):
        self.p = p; self.uv = uv; self.shade = shade; self.mat = mat; self.group = group; self.far = far

class World:
    def __init__(self):
        self.tris = []                 # drawn triangles
        self.col_verts = []; self.col_lookup = {}
        self.col_tris = []             # (a, b, c, surface_index)
        self.surfaces = []             # (sfx, climbable, hookshot) -> index
        self.props = []                # (x0, y0, z0, x1, y1, z1, name) nearby collision boxes
        self.obstacles = []            # (x0, x1, z0, z1) navigation blocks laid by hand (tree trunks, rocks)
        self.blockers = []             # every solid: ('poly', outline, y0, y1) or ('box', x0, x1, z0, z1, y0, y1); the exporter keeps the
                                       # parts that stand up out of the floor round them as navigation obstacles
        self.buildings = []            # (x, z, halfw, halfd, floorY, yaw) floors that are higher than the terrain
        self.floor_patches = []        # (x, z, radius, y)
        self.loot = []                 # (x, y, z, name)
        self.markers = []              # dicts
        self.group = 'Architecture'
        self.far = False               # big landmarks: drawn from far away
        self.tint = (1.0, 1.0, 1.0)
        self.interior = []             # (x0, x1, z0, z1, y0, y1): boxes where the light is dimmer (under roofs)

    # ---- collision ----------------------------------------------------------------------------------------------------------------
    def surface(self, name, climb=False, hookshot=False):
        key = (SURFACES[name], climb, hookshot)
        if key not in self.surfaces: self.surfaces.append(key)
        return self.surfaces.index(key)

    def col_vertex(self, p):
        g = (int(round(p[0])), int(round(p[1])), int(round(p[2])))
        if g not in self.col_lookup:
            self.col_lookup[g] = len(self.col_verts); self.col_verts.append(g)
        return self.col_lookup[g]

    def col_tri(self, a, b, c, surf):
        ia, ib, ic = self.col_vertex(a), self.col_vertex(b), self.col_vertex(c)
        if len({ia, ib, ic}) < 3: return
        A, B, C = (np.array(self.col_verts[i], dtype=float) for i in (ia, ib, ic))
        n = np.cross(B - A, C - A)
        if np.linalg.norm(n) < 1.0: return
        self.col_tris.append((ia, ib, ic, surf))

    # ---- drawing ------------------------------------------------------------------------------------------------------------------
    def light(self, p, n):
        d = max(0.0, float(np.dot(n, SUN)))
        s = 0.58 + 0.42 * d + 0.06 * n[1]
        for x0, x1, z0, z1, y0, y1 in self.interior:
            if x0 <= p[0] <= x1 and z0 <= p[2] <= z1 and y0 <= p[1] <= y1:
                s = 0.62 + 0.12 * d   # warm interior gloom
                break
        return min(1.08, s)

    def face(self, pts, mat, uv_scale=1.0, col=None, surf=None, double=False, uvs=None, out=None):
        """A convex polygon (counter clockwise seen from outside). col: None, or a surface index for scene collision.
        out: a point inside the solid; the face is turned to look away from it."""
        P = [np.array(p, dtype=float) for p in pts]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        for k in range(2, len(P) - 1):
            m = np.cross(P[k] - P[0], P[k + 1] - P[0])
            if np.linalg.norm(m) > np.linalg.norm(n): n = m
        ln = np.linalg.norm(n)
        if ln < 1e-6: return
        n = n / ln
        if out is not None and np.dot(n, sum(P) / len(P) - np.array(out, dtype=float)) < 0:
            P = P[::-1]; n = -n
            if uvs is not None: uvs = uvs[::-1]
        if uvs is None:
            # box mapping: drop the dominant axis; one texture repeat per `TEX_UNITS` units
            ax = int(np.argmax(np.abs(n)))
            if ax == 1: uvs = [(p[0], p[2]) for p in P]
            elif ax == 0: uvs = [(p[2] * (1 if n[0] < 0 else -1), -p[1]) for p in P]
            else: uvs = [(p[0] * (1 if n[2] > 0 else -1), -p[1]) for p in P]
            uvs = [(u / (TEX_UNITS.get(mat, 128) * uv_scale), v / (TEX_UNITS.get(mat, 128) * uv_scale)) for u, v in uvs]
        tint = self.tint
        for k in range(1, len(P) - 1):
            idx = (0, k, k + 1)
            tp = [P[i] for i in idx]
            tuv = [uvs[i] for i in idx]
            sh = [tuple(min(1.2, self.light(p, n) * t) for t in tint) for p in tp]
            self.tris.append(Tri(tp, tuv, sh, mat, self.group, self.far))
            if double:
                self.tris.append(Tri(tp[::-1], tuv[::-1], [tuple(min(1.2, self.light(p, -n) * t) for t in tint) for p in tp[::-1]], mat, self.group, self.far))
            if col is not None:
                self.col_tri(tp[0], tp[1], tp[2], col)

    # ---- solids -------------------------------------------------------------------------------------------------------------------
    def prism(self, outline, y0, y1, mat, col='static', surf='stone', top_mat=None, bottom=False, climb=(), side_mat=None,
              uv_scale=1.0, top=True, hookshot=False):
        """An upright prism over a plan outline [(x, z), ...] counter clockwise seen from above (any simple polygon).
        climb: indices of outline edges whose outside face is climbable (vines)."""
        pts = [(float(x), float(z)) for x, z in outline]
        if _area(pts) < 0: pts = pts[::-1]; climb = tuple((len(pts) - 2 - i) % len(pts) for i in climb)
        n = len(pts)
        s_plain = self.surface(surf, hookshot=hookshot) if col == 'static' else None
        s_climb = self.surface(surf, climb=True, hookshot=hookshot) if col == 'static' else None
        for i in range(n):
            (ax, az), (bx, bz) = pts[i], pts[(i + 1) % n]
            c = (s_climb if i in climb else s_plain)
            # outward facing: outline is CCW from above (x right, z down on screen means... we use a right handed check below)
            self.face([(ax, y0, az), (ax, y1, az), (bx, y1, bz), (bx, y0, bz)], side_mat or mat, uv_scale, c)
        tris = _triangulate(pts)
        for a, b, c in tris:
            if top:
                self.face([(pts[a][0], y1, pts[a][1]), (pts[c][0], y1, pts[c][1]), (pts[b][0], y1, pts[b][1])], top_mat or mat, uv_scale, s_plain)
            if bottom:
                self.face([(pts[a][0], y0, pts[a][1]), (pts[b][0], y0, pts[b][1]), (pts[c][0], y0, pts[c][1])], mat, uv_scale, s_plain)
        if col == 'prop':
            xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
            self.props.append((min(xs), y0, min(zs), max(xs), y1, max(zs), self.group))
        if col in ('static', 'prop'):
            self.blockers.append(('poly', pts, y0, y1))

    def box(self, cx, y0, cz, sx, sy, sz, mat, yaw=0.0, **kw):
        return self.prism(rect(cx, cz, sx, sz, yaw), y0, y0 + sy, mat, **kw)

    def ramp(self, x0, z0, x1, z1, width, y0, y1, mat, surf='stone', col='static', base=None, side_mat=None):
        """A sloped walkway from (x0, z0) at y0 to (x1, z1) at y1: a wedge, solid down to `base` (the lower end's height by default)."""
        dx, dz = x1 - x0, z1 - z0; l = math.hypot(dx, dz); nx, nz = -dz / l * width / 2, dx / l * width / 2
        base = min(y0, y1) if base is None else base
        A = (x0 + nx, z0 + nz); B = (x0 - nx, z0 - nz); C = (x1 - nx, z1 - nz); D = (x1 + nx, z1 + nz)
        s = self.surface(surf) if col == 'static' else None
        top = [(A[0], y0, A[1]), (D[0], y1, D[1]), (C[0], y1, C[1]), (B[0], y0, B[1])]
        inside = ((x0 + x1) / 2, (min(y0, y1) + base) / 2 if max(y0, y1) - base > 2 else base - 10, (z0 + z1) / 2)
        self.face(top, mat, 1.0, s, out=inside)
        for (p, py), (q, qy) in [((A, y0), (D, y1)), ((C, y1), (B, y0)), ((B, y0), (A, y0)), ((D, y1), (C, y1))]:
            if max(py, qy) - base < 1: continue
            pts = [(p[0], base, p[1]), (p[0], py, p[1]), (q[0], qy, q[1]), (q[0], base, q[1])]
            if py - base < 1: pts = [pts[0], pts[2], pts[3]]
            elif qy - base < 1: pts = [pts[0], pts[1], pts[2]]
            self.face(pts, side_mat or mat, 1.0, s, out=inside)
        if col == 'prop':
            self.props.append((min(A[0], B[0], C[0], D[0]), base, min(A[1], B[1], C[1], D[1]), max(A[0], B[0], C[0], D[0]), max(y0, y1), max(A[1], B[1], C[1], D[1]), self.group))

    def cylinder(self, cx, cz, r, y0, y1, mat, n=12, col='static', surf='stone', top=True, top_mat=None, r1=None, climb=False, uv_scale=1.0, bottom=False):
        r1 = r if r1 is None else r1
        s = (self.surface(surf, climb=climb) if col == 'static' else None)
        stop = self.surface(surf) if col == 'static' else None
        tu = TEX_UNITS.get(mat, 128) * uv_scale
        circ = 2 * math.pi * max(r, r1)
        inside = (cx, (y0 + y1) / 2, cz)
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            p = [(cx + r * math.cos(a0), y0, cz + r * math.sin(a0)), (cx + r1 * math.cos(a0), y1, cz + r1 * math.sin(a0)),
                 (cx + r1 * math.cos(a1), y1, cz + r1 * math.sin(a1)), (cx + r * math.cos(a1), y0, cz + r * math.sin(a1))]
            u0 = circ * i / n / tu; u1 = circ * (i + 1) / n / tu; v0 = -y0 / tu; v1 = -y1 / tu
            self.face(p, mat, uv_scale, s, uvs=[(u0, v0), (u0, v1), (u1, v1), (u1, v0)], out=inside)
        if top and r1 > 1:
            ring = [(cx + r1 * math.cos(2 * math.pi * i / n), y1, cz + r1 * math.sin(2 * math.pi * i / n)) for i in range(n)]
            self.face(ring, top_mat or mat, uv_scale, stop, out=(cx, y1 - 10, cz))
        if bottom:
            ring = [(cx + r * math.cos(2 * math.pi * i / n), y0, cz + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
            self.face(ring, mat, uv_scale, stop, out=(cx, y0 + 10, cz))
        if col == 'prop':
            self.props.append((cx - r, y0, cz - r, cx + r, y1, cz + r, self.group))
        if col in ('static', 'prop'):
            self.blockers.append(('box', cx - r * .8, cx + r * .8, cz - r * .8, cz + r * .8, y0, y1))

    def cone(self, cx, cz, r, y0, y1, mat, n=12, col=None, surf='stone', uv_scale=1.0, bottom=False):
        s = self.surface(surf) if col == 'static' else None
        slant = math.hypot(r, y1 - y0); circ = 2 * math.pi * r
        tu = TEX_UNITS.get(mat, 128) * uv_scale
        inside = (cx, y0 + (y1 - y0) * 0.25, cz)
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            p = [(cx + r * math.cos(a0), y0, cz + r * math.sin(a0)), (cx, y1, cz), (cx + r * math.cos(a1), y0, cz + r * math.sin(a1))]
            self.face(p, mat, uv_scale, s, uvs=[(circ * i / n / tu, 0), (circ * (i + .5) / n / tu, -slant / tu), (circ * (i + 1) / n / tu, 0)], out=inside)
        if bottom:
            ring = [(cx + r * math.cos(2 * math.pi * i / n), y0, cz + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
            self.face(ring, mat, uv_scale, s, out=(cx, y0 + 10, cz))

    def gable(self, cx, cz, w, d, y0, rise, mat, yaw=0.0, overhang=40, col='static', surf='wood', end_mat=None, climb=False):
        """A pitched roof over a w x d footprint (ridge along local z), eaves at y0: a closed solid you can stand on."""
        hw, hd = w / 2 + overhang, d / 2 + overhang
        c, s_ = math.cos(yaw), math.sin(yaw)
        def P(x, y, z): return (cx + x * c - z * s_, y, cz + x * s_ + z * c)
        a, b, cc, dd = P(-hw, y0, -hd), P(hw, y0, -hd), P(hw, y0, hd), P(-hw, y0, hd)
        r0, r1 = P(0, y0 + rise, -hd), P(0, y0 + rise, hd)
        sid = self.surface(surf, climb=climb) if col == 'static' else None
        inside = (cx, y0 + rise * 0.3, cz)
        self.face([a, r0, r1, dd], mat, 1.0, sid, out=inside)
        self.face([b, cc, r1, r0], mat, 1.0, sid, out=inside)
        self.face([a, b, r0], end_mat or mat, 1.0, sid, out=inside)
        self.face([cc, dd, r1], end_mat or mat, 1.0, sid, out=inside)
        self.face([a, dd, cc, b], end_mat or mat, 1.0, None, out=(cx, y0 + 10, cz))

    def hip(self, cx, cz, w, d, y0, rise, mat, yaw=0.0, overhang=30, col='static', surf='stone', apex=None):
        """A pyramid (apex None) or hipped roof: four slopes up to a point or a short ridge."""
        hw, hd = w / 2 + overhang, d / 2 + overhang
        c, s_ = math.cos(yaw), math.sin(yaw)
        def P(x, y, z): return (cx + x * c - z * s_, y, cz + x * s_ + z * c)
        q = [P(-hw, y0, -hd), P(hw, y0, -hd), P(hw, y0, hd), P(-hw, y0, hd)]
        sid = self.surface(surf) if col == 'static' else None
        inside = (cx, y0 + rise * 0.3, cz)
        if apex is None:
            top = P(0, y0 + rise, 0)
            for i in range(4): self.face([q[i], top, q[(i + 1) % 4]], mat, 1.0, sid, out=inside)
        else:   # ridge along local z, `apex` long
            r0, r1 = P(0, y0 + rise, -apex / 2), P(0, y0 + rise, apex / 2)
            self.face([q[0], r0, q[1]], mat, 1.0, sid, out=inside); self.face([q[2], r1, q[3]], mat, 1.0, sid, out=inside)
            self.face([q[1], r0, r1, q[2]], mat, 1.0, sid, out=inside); self.face([q[3], r1, r0, q[0]], mat, 1.0, sid, out=inside)
        self.face(q, mat, 1.0, None, out=(cx, y0 + 10, cz))

    def sphere(self, cx, cy, cz, rx, ry, rz, mat, rings=5, segs=8, seed=0, jitter=0.0, squash_bottom=0.0):
        """A faceted blob (tree canopy, boulder): drawn only."""
        rng = np.random.default_rng(seed)
        pts = {}
        for i in range(rings + 1):
            th = math.pi * i / rings
            for j in range(segs):
                ph = 2 * math.pi * (j + 0.5 * (i % 2)) / segs
                k = 1 + (rng.uniform(-jitter, jitter) if 0 < i < rings else 0)
                y = math.cos(th)
                if y < 0: y *= (1 - squash_bottom)
                pts[i, j] = (cx + rx * k * math.sin(th) * math.cos(ph), cy + ry * k * y, cz + rz * k * math.sin(th) * math.sin(ph))
        inside = (cx, cy, cz)
        for i in range(rings):
            for j in range(segs):
                a, b, c, d = pts[i, j], pts[i, (j + 1) % segs], pts[i + 1, (j + 1) % segs], pts[i + 1, j]
                if i == 0: self.face([a, c, d], mat, out=inside)
                elif i == rings - 1: self.face([a, b, d], mat, out=inside)
                else:
                    self.face([a, b, c], mat, out=inside); self.face([a, c, d], mat, out=inside)

    def solid(self, pts, faces, mat, col='static', surf='stone', climb=False, mats=None):
        """Any closed convex-ish solid: points and faces (index lists); each face is turned outwards."""
        cen = tuple(np.mean(np.array(pts, dtype=float), axis=0))
        sid = self.surface(surf, climb=climb) if col == 'static' else None
        for k, f in enumerate(faces):
            self.face([pts[i] for i in f], (mats[k] if mats else mat), 1.0, sid, out=cen)
        P = np.array(pts); lo = P.min(axis=0); hi = P.max(axis=0)
        if col == 'prop':
            self.props.append((lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], self.group))
        if col in ('static', 'prop'):
            self.blockers.append(('box', lo[0], hi[0], lo[2], hi[2], lo[1], hi[1]))

    def quad(self, p0, p1, p2, p3, mat, double=False, uvs=None, col=None):
        self.face([p0, p1, p2, p3], mat, 1.0, col, double=double, uvs=uvs)

    def decal(self, cx, y0, cz, w, h, nx, nz, mat, off=2.0, uvs=None):
        """A flat picture on a wall (window, door, clock face): facing (nx, nz), drawn only."""
        tx, tz = -nz, nx
        x, z = cx + nx * off, cz + nz * off
        p = [(x - tx * w / 2, y0, z - tz * w / 2), (x + tx * w / 2, y0, z + tz * w / 2), (x + tx * w / 2, y0 + h, z + tz * w / 2), (x - tx * w / 2, y0 + h, z - tz * w / 2)]
        self.face(p, mat, 1.0, None, uvs=uvs or [(0, 1), (1, 1), (1, 0), (0, 0)])

# One texture repeat covers this many units (32 texels): stone blocks ~ 128, planks 96, grass 200...
TEX_UNITS = {}

def rect(cx, cz, sx, sz, yaw=0.0):
    c, s = math.cos(yaw), math.sin(yaw)
    out = []
    for x, z in [(-sx / 2, -sz / 2), (sx / 2, -sz / 2), (sx / 2, sz / 2), (-sx / 2, sz / 2)]:
        out.append((cx + x * c - z * s, cz + x * s + z * c))
    return out

def _area(pts):
    return 0.5 * sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))

def _ccw_up(p):
    P = [np.array(q, dtype=float) for q in p]
    n = np.cross(P[1] - P[0], P[2] - P[0])
    return p if n[1] > 0 else p[::-1]

def _triangulate(pts):
    """Ear clipping for a simple polygon with positive _area. Returns index triples (a, b, c) with the same winding."""
    idx = list(range(len(pts))); out = []
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if cross(a, b, c) <= 1e-9: continue
            if any(cross(a, b, pts[j]) >= 0 and cross(b, c, pts[j]) >= 0 and cross(c, a, pts[j]) >= 0 for j in idx if j not in (i0, i1, i2)): continue
            out.append((i0, i1, i2)); idx.pop(k); break
    if len(idx) == 3: out.append(tuple(idx))
    return out
