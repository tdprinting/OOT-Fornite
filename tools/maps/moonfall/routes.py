"""Where a bot can stand above the ground: the floors, ramps, roofs and tower landings of Moonfall Isle, found from the baked collision itself so
nothing has to be listed by hand. (Moonfall's copy of the Kingdom routine: a steep face only counts as a wall over a node for the height it has
right beside that cell, so a ramp's own sloping side, whose top edge is the ramp surface, no longer hides the nodes on the ramp.) The result is a list of nodes (x, z, y) on a 60 unit lattice; the game links neighbours that differ by a step
(server/nav.h) and joins them to the ground wherever a ramp or a doorstep meets it."""
import math
import numpy as np

CELL = 60.0
ORIGIN = -6500.0         # the bots' grid starts at the map circle's corner (centre 0, radius 6500) and uses the same lattice
HEAD = (24.0, 100.0)     # a bot needs this much clear height above its feet
WALL = (26.0, 90.0)      # a wall in this band of height stops it
MIN_ABOVE = 18.0         # a surface this far over the ground is not the ground itself (a ramp up to a doorstep, a floor over a cellar, an upper floor)

def cells_of_triangle(A, B, C):
    """Lattice cells whose centre lies inside the triangle seen from above, with the height of the triangle there."""
    xs = (A[0], B[0], C[0]); zs = (A[2], B[2], C[2])
    i0 = int(math.floor((min(xs) - ORIGIN) / CELL - 0.5)); i1 = int(math.ceil((max(xs) - ORIGIN) / CELL - 0.5))
    j0 = int(math.floor((min(zs) - ORIGIN) / CELL - 0.5)); j1 = int(math.ceil((max(zs) - ORIGIN) / CELL - 0.5))
    d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
    if abs(d) < 1e-6: return
    for j in range(j0, j1 + 1):
        pz = ORIGIN + (j + 0.5) * CELL
        for i in range(i0, i1 + 1):
            px = ORIGIN + (i + 0.5) * CELL
            l1 = ((B[2] - C[2]) * (px - C[0]) + (C[0] - B[0]) * (pz - C[2])) / d
            l2 = ((C[2] - A[2]) * (px - C[0]) + (A[0] - C[0]) * (pz - C[2])) / d
            l3 = 1 - l1 - l2
            if l1 < -1e-6 or l2 < -1e-6 or l3 < -1e-6: continue
            yield i, j, l1 * A[1] + l2 * B[1] + l3 * C[1]

def near_triangle_cells(A, B, C, reach):
    """Lattice cells whose centre is within `reach` of the triangle's footprint (a wall is a thin sliver from above), with the face's lowest and
    highest point beside that cell: the vertical extent of the triangle at the point of its footprint nearest the cell centre."""
    pts = [(A[0], A[2]), (B[0], B[2]), (C[0], C[2])]
    xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
    i0 = int(math.floor((min(xs) - reach - ORIGIN) / CELL - 0.5)); i1 = int(math.ceil((max(xs) + reach - ORIGIN) / CELL - 0.5))
    j0 = int(math.floor((min(zs) - reach - ORIGIN) / CELL - 0.5)); j1 = int(math.ceil((max(zs) + reach - ORIGIN) / CELL - 0.5))
    def seg(px, pz, a, b):
        dx, dz = b[0] - a[0], b[1] - a[1]; L2 = dx * dx + dz * dz
        t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((px - a[0]) * dx + (pz - a[1]) * dz) / L2))
        return math.hypot(px - a[0] - dx * t, pz - a[1] - dz * t)
    for j in range(j0, j1 + 1):
        pz = ORIGIN + (j + 0.5) * CELL
        for i in range(i0, i1 + 1):
            px = ORIGIN + (i + 0.5) * CELL
            inside = False
            s = [(px - pts[k][0]) * (pts[(k + 1) % 3][1] - pts[k][1]) - (pz - pts[k][1]) * (pts[(k + 1) % 3][0] - pts[k][0]) for k in range(3)]
            if all(v >= 0 for v in s) or all(v <= 0 for v in s): inside = True
            if inside or min(seg(px, pz, pts[k], pts[(k + 1) % 3]) for k in range(3)) <= reach: yield i, j, _extent_at(A, B, C, px, pz)

def _extent_at(A, B, C, px, pz):
    """(lo, hi) of a steep triangle at the point of its footprint nearest (px, pz): the footprint is close to a segment, so every vertex is placed
    along it and the triangle's edges are interpolated there."""
    pts = [(A[0], A[2]), (B[0], B[2]), (C[0], C[2])]
    # the axis: the longest footprint edge
    e = max(((k, (k + 1) % 3) for k in range(3)), key=lambda kl: math.hypot(pts[kl[0]][0] - pts[kl[1]][0], pts[kl[0]][1] - pts[kl[1]][1]))
    o = pts[e[0]]; dx, dz = pts[e[1]][0] - o[0], pts[e[1]][1] - o[1]; L2 = dx * dx + dz * dz
    if L2 < 1e-6: return min(A[1], B[1], C[1]), max(A[1], B[1], C[1])
    ts = [((p[0] - o[0]) * dx + (p[1] - o[1]) * dz) / L2 for p in pts]; ys = (A[1], B[1], C[1])
    t = max(min(ts), min(max(ts), ((px - o[0]) * dx + (pz - o[1]) * dz) / L2))
    lo, hi = None, None
    for k in range(3):
        l = (k + 1) % 3; t0, t1 = ts[k], ts[l]
        if abs(t1 - t0) < 1e-9:
            if abs(t - t0) > 1e-6: continue
            ya, yb = ys[k], ys[l]
        else:
            if t < min(t0, t1) - 1e-6 or t > max(t0, t1) + 1e-6: continue
            u = (t - t0) / (t1 - t0); ya = yb = ys[k] + (ys[l] - ys[k]) * u
        for y in (ya, yb):
            lo = y if lo is None else min(lo, y); hi = y if hi is None else max(hi, y)
    if lo is None: return min(ys), max(ys)
    return lo, hi

def _footprint_distance(P, px, pz):
    """Distance from (px, pz) to the triangle's footprint seen from above (0 inside)."""
    pts = [(p[0], p[2]) for p in P]
    s = [(px - pts[k][0]) * (pts[(k + 1) % 3][1] - pts[k][1]) - (pz - pts[k][1]) * (pts[(k + 1) % 3][0] - pts[k][0]) for k in range(3)]
    if all(v >= 0 for v in s) or all(v <= 0 for v in s): return 0.0
    def seg(a, b):
        dx, dz = b[0] - a[0], b[1] - a[1]; L2 = dx * dx + dz * dz
        t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((px - a[0]) * dx + (pz - a[1]) * dz) / L2))
        return math.hypot(px - a[0] - dx * t, pz - a[1] - dz * t)
    return min(seg(pts[k], pts[(k + 1) % 3]) for k in range(3))

def _height_in(P, px, pz):
    """The triangle's height over (px, pz), or None when the point is outside its footprint."""
    A, B, C = P
    d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
    if abs(d) < 1e-6: return None
    l1 = ((B[2] - C[2]) * (px - C[0]) + (C[0] - B[0]) * (pz - C[2])) / d
    l2 = ((C[2] - A[2]) * (px - C[0]) + (A[0] - C[0]) * (pz - C[2])) / d
    l3 = 1 - l1 - l2
    if min(l1, l2, l3) < -1e-6: return None
    return l1 * A[1] + l2 * B[1] + l3 * C[1]

OFFSETS = [(0, 0), (20, 0), (-20, 0), (0, 20), (0, -20), (20, 20), (20, -20), (-20, 20), (-20, -20)]
SLIDE = 22.0     # a wall this close to a bot's feet stops it standing there

def upper_nodes(w, ground):
    """ground(x, z) -> the walkable ground height there (terrain, building floor, bridge). Returns [(x, z, y)].
    A node sits at its cell's centre, or up to 20 units off it when a wall stands too close to the centre (a stair flight along a wall)."""
    V = np.array(w.col_verts, float)
    ups, ceil, walls = {}, {}, {}
    faces = []
    for a, b, c, _s in w.col_tris:
        A, B, C = V[a], V[b], V[c]
        n = np.cross(B - A, C - A); ln = np.linalg.norm(n)
        if ln < 1e-6: continue
        ny = n[1] / ln
        k = len(faces); faces.append((A, B, C))
        if ny >= 0.75:
            for i, j, y in cells_of_triangle(A, B, C): ups.setdefault((i, j), []).append((y, k))
        elif ny <= -0.3:
            for i, j, y in cells_of_triangle(A, B, C): ceil.setdefault((i, j), []).append(y)
        if abs(ny) < 0.75:
            for i, j, _ext in near_triangle_cells(A, B, C, SLIDE + 30.0): walls.setdefault((i, j), []).append(k)
    def walled(i, j, px, pz, y):
        for k in walls.get((i, j), ()):
            P = faces[k]
            if _footprint_distance(P, px, pz) > SLIDE: continue
            lo, hi = _extent_at(P[0], P[1], P[2], px, pz)
            if lo < y + WALL[1] and hi > y + WALL[0]: return True
        return False
    nodes = []
    for (i, j), ys in ups.items():
        x = ORIGIN + (i + 0.5) * CELL; z = ORIGIN + (j + 0.5) * CELL
        g = ground(x, z)
        kept = []
        for y0, k in sorted(ys):
            y = round(y0)
            if y < g + MIN_ABOVE: continue
            if kept and y - kept[-1][2] < 20: continue                    # two surfaces one on the other: the lower one is the floor
            if any(y + HEAD[0] < cy < y + HEAD[1] for cy in ceil.get((i, j), ())): continue
            for ox, oz in OFFSETS:
                px, pz = x + ox, z + oz
                yy = _height_in(faces[k], px, pz) if (ox or oz) else y0
                if yy is None or abs(yy - y0) > 25: continue
                if walled(i, j, px, pz, yy): continue
                kept.append((px, pz, int(round(yy)))); break
        for px, pz, y in kept: nodes.append((int(round(px)), int(round(pz)), y))
    return sorted(nodes)

def climb_walls(w):
    """The steep faces that can be climbed (ivy, mostly): for each triangle where the surface allows it, where it stands, which way it faces (outward,
    in hundredths) and its lowest and highest points. The game links the ground in front of it to the floor behind its top edge."""
    V = np.array(w.col_verts, float)
    out = {}
    for a, b, c, s in w.col_tris:
        if not w.surfaces[s][1]: continue
        A, B, C = V[a], V[b], V[c]
        n = np.cross(B - A, C - A); ln = np.linalg.norm(n)
        if ln < 1e-6: continue
        n = n / ln
        if abs(n[1]) > 0.35: continue
        h = math.hypot(n[0], n[2])
        if h < 1e-6: continue
        cx = (A[0] + B[0] + C[0]) / 3; cz = (A[2] + B[2] + C[2]) / 3
        y0 = min(A[1], B[1], C[1]); y1 = max(A[1], B[1], C[1])
        key = (round(cx / 60), round(cz / 60), round(n[0] / h * 2), round(n[2] / h * 2))
        # keep the highest triangle of each column of wall: the top edge is what is climbed to
        if key not in out or out[key][5] < y1:
            out[key] = (int(round(cx)), int(round(cz)), int(round(n[0] / h * 100)), int(round(n[2] / h * 100)), int(round(y0)), int(round(y1)))
    return sorted(out.values())
