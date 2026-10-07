"""Where a bot can stand above the ground: the floors, ramps, roofs and tower landings of Hyrule Kingdom, found from the baked collision itself so
nothing has to be listed by hand. The result is a list of nodes (x, z, y) on a 60 unit lattice; the game links neighbours that differ by a step
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
    """Lattice cells whose centre is within `reach` of the triangle's footprint (a wall is a thin sliver from above)."""
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
            if inside or min(seg(px, pz, pts[k], pts[(k + 1) % 3]) for k in range(3)) <= reach: yield i, j

def upper_nodes(w, ground):
    """ground(x, z) -> the walkable ground height there (terrain, building floor, bridge). Returns [(x, z, y)]."""
    V = np.array(w.col_verts, float)
    ups, ceil, walls = {}, {}, {}
    for a, b, c, _s in w.col_tris:
        A, B, C = V[a], V[b], V[c]
        n = np.cross(B - A, C - A); ln = np.linalg.norm(n)
        if ln < 1e-6: continue
        ny = n[1] / ln
        if ny >= 0.75:
            for i, j, y in cells_of_triangle(A, B, C): ups.setdefault((i, j), []).append(y)
        elif ny <= -0.3:
            for i, j, y in cells_of_triangle(A, B, C): ceil.setdefault((i, j), []).append(y)
        if abs(ny) < 0.75:
            lo, hi = min(A[1], B[1], C[1]), max(A[1], B[1], C[1])
            for i, j in near_triangle_cells(A, B, C, 22.0): walls.setdefault((i, j), []).append((lo, hi))
    nodes = []
    for (i, j), ys in ups.items():
        x = ORIGIN + (i + 0.5) * CELL; z = ORIGIN + (j + 0.5) * CELL
        g = ground(x, z)
        kept = []
        for y in sorted(set(round(v) for v in ys)):
            if y < g + MIN_ABOVE: continue
            if kept and y - kept[-1] < 20: continue                       # two surfaces one on the other: the lower one is the floor
            if any(y + HEAD[0] < cy < y + HEAD[1] for cy in ceil.get((i, j), ())): continue
            if any(lo < y + WALL[1] and hi > y + WALL[0] for lo, hi in walls.get((i, j), ())): continue
            kept.append(y)
        for y in kept: nodes.append((int(round(x)), int(round(z)), int(y)))
    return sorted(nodes)
