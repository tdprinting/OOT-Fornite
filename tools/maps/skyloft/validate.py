"""Automated checks on Skyloft Isles before it is exported: everything is measured from the baked collision and the game's own walking rules
(server/nav.h), not from the authoring intent, so a chest inside a wall, a door onto nothing, a floor that is not there, a floating tower or a
place no bot can walk to from the lobby all show up here.

    python3 tools/maps/skyloft/validate.py [--only Poi,Poi] [--strict]

Exit code 1 when a check fails (or, with --strict, when there are warnings)."""
import argparse, math, os, sys, json
from collections import defaultdict, deque
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path: sys.path.insert(0, HERE)
import numpy as np
import world, terrain, kit
from layout import *
import export, routes

LINK_H = 68.0          # adult Link, head to toe
STEP_UP = 46.0; CLIMB_UP = 70.0; DROP = 320.0      # server/nav.h
CELL = 60.0

class Mesh:
    """The structures' collision in buckets, for quick vertical rays and wall look-ups."""
    def __init__(self, w):
        self.V = np.array(w.col_verts, float) if w.col_verts else np.zeros((0, 3))
        self.tris = [(a, b, c, s) for a, b, c, s in w.col_tris]
        self.ny = []; self.cells = defaultdict(list)
        for k, (a, b, c, s) in enumerate(self.tris):
            A, B, C = self.V[a], self.V[b], self.V[c]
            n = np.cross(B - A, C - A); ln = np.linalg.norm(n)
            self.ny.append(0.0 if ln < 1e-6 else n[1] / ln)
            xs = (A[0], B[0], C[0]); zs = (A[2], B[2], C[2])
            for i in range(int(min(xs) // CELL), int(max(xs) // CELL) + 1):
                for j in range(int(min(zs) // CELL), int(max(zs) // CELL) + 1): self.cells[(i, j)].append(k)
    def candidates(self, x, z, r=0.0):
        out = set()
        for i in range(int((x - r) // CELL), int((x + r) // CELL) + 1):
            for j in range(int((z - r) // CELL), int((z + r) // CELL) + 1): out.update(self.cells.get((i, j), ()))
        return out
    def hits(self, x, z):
        """(y, ny) of every triangle under or over (x, z)."""
        out = []
        for k in self.candidates(x, z):
            a, b, c, _ = self.tris[k]; A, B, C = self.V[a], self.V[b], self.V[c]
            d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
            if abs(d) < 1e-6: continue
            l1 = ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d
            l2 = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d
            l3 = 1 - l1 - l2
            if min(l1, l2, l3) < -1e-4: continue
            out.append((l1 * A[1] + l2 * B[1] + l3 * C[1], self.ny[k]))
        return out
    def walls(self, x, z, r):
        """(lo, hi) height bands of the steep faces within r of (x, z) seen from above."""
        out = []
        for k in self.candidates(x, z, r):
            if abs(self.ny[k]) >= 0.75: continue
            a, b, c, _ = self.tris[k]; P = [self.V[a], self.V[b], self.V[c]]
            pts = [(p[0], p[2]) for p in P]
            s = [(x - pts[i][0]) * (pts[(i + 1) % 3][1] - pts[i][1]) - (z - pts[i][1]) * (pts[(i + 1) % 3][0] - pts[i][0]) for i in range(3)]
            inside = all(v >= 0 for v in s) or all(v <= 0 for v in s)
            def seg(a_, b_):
                dx, dz = b_[0] - a_[0], b_[1] - a_[1]; L2 = dx * dx + dz * dz
                t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((x - a_[0]) * dx + (z - a_[1]) * dz) / L2))
                return math.hypot(x - a_[0] - dx * t, z - a_[1] - dz * t)
            if inside or min(seg(pts[i], pts[(i + 1) % 3]) for i in range(3)) <= r:
                out.append(routes._extent_at(P[0], P[1], P[2], x, z) if getattr(self, 'local', False) else (min(p[1] for p in P), max(p[1] for p in P)))
        return out

def nearest_poi(x, z):
    return min(POIS, key=lambda p: math.hypot(p[1] - x, p[2] - z))[0]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--only', default=None); ap.add_argument('--strict', action='store_true'); ap.add_argument('--probe', default=None, help='x,z,y[;x,z,y...]: list the steep faces within 50 of each point'); ap.add_argument('--dump', default=None, help='x,z,r: print the nodes there and whether they are reached')
    ap.add_argument('--report', default=os.path.join(export.ROOT, 'assets', 'maps', 'skyloft_isles', 'validation.json'))
    a = ap.parse_args()
    w, h, T = world.build(a.only.split(',') if a.only else None)
    M = Mesh(w)
    fails, warns = [], []
    def ground(x, z): return float(terrain.grid_height(h, x, z))
    def floor_under(x, z, ytop):
        """The highest walkable surface (structure, prop top or ground) at or under ytop."""
        best = ground(x, z) if ground(x, z) <= ytop else None
        for y, ny in M.hits(x, z):
            if ny >= 0.75 and y <= ytop and (best is None or y > best): best = y
        for x0, y0, z0, x1, y1, z1, name in w.props:
            if x0 <= x <= x1 and z0 <= z <= z1 and y1 <= ytop and (best is None or y1 > best): best = y1
        return best
    def headroom(x, z, y):
        """The lowest ceiling over y at (x, z), or None."""
        c = [cy for cy, ny in M.hits(x, z) if ny <= -0.3 and cy > y + 5]
        return min(c) if c else None
    def blocked(x, z, y, r, lo, hi):
        return any(wl < y + hi and wh > y + lo for wl, wh in M.walls(x, z, r))
    M.local = True

    if a.probe:
        for item in a.probe.split(';'):
            vals = [float(v) for v in item.split(',')]
            px, pz = vals[0], vals[1]
            py = vals[2] if len(vals) > 2 else (floor_under(px, pz, ground(px, pz) + 300) or ground(px, pz))
            print('probe', px, pz, py, 'ground', round(ground(px, pz)), 'floors', sorted(set(round(y) for y, ny in M.hits(px, pz) if ny >= 0.75)), 'ceilings', sorted(set(round(y) for y, ny in M.hits(px, pz) if ny <= -0.3)))
            for k in M.candidates(px, pz, 50):
                if abs(M.ny[k]) >= 0.75: continue
                a_, b_, c_, s_ = M.tris[k]; P = [M.V[a_], M.V[b_], M.V[c_]]
                pts = [(q[0], q[2]) for q in P]
                def seg(a1, b1):
                    dx, dz = b1[0] - a1[0], b1[1] - a1[1]; L2 = dx * dx + dz * dz
                    t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((px - a1[0]) * dx + (pz - a1[1]) * dz) / L2))
                    return math.hypot(px - a1[0] - dx * t, pz - a1[1] - dz * t)
                d = min(seg(pts[i], pts[(i + 1) % 3]) for i in range(3))
                lo, hi = min(q[1] for q in P), max(q[1] for q in P)
                if d <= 50 and hi > py and lo < py + 120: print('   face d=%.0f band %d..%d surf %s %s' % (d, lo, hi, w.surfaces[s_][0], [tuple(int(v) for v in q) for q in P]))
    # ---- 1. scale: the kit's numbers against adult Link ------------------------------------------------------------------------------
    S = kit.SCALE
    if not (1.9 * LINK_H <= S['door_h'] <= 2.6 * LINK_H): fails.append('door height %s is not 1.9 to 2.6 Links' % S['door_h'])
    if not (2.7 * LINK_H <= S['storey'] <= 3.6 * LINK_H): fails.append('storey %s is not 2.7 to 3.6 Links' % S['storey'])
    if not (1.3 * LINK_H <= S['door_w'] <= 2.0 * LINK_H): fails.append('door width %s is not 1.3 to 2.0 Links' % S['door_w'])
    if S['max_slope'] > 0.6: fails.append('stairs steeper than 0.6')
    if S['step'] > 24: fails.append('step %s taller than Link can walk up' % S['step'])
    for bx, bz, hw, hd, fy, yaw in w.buildings:
        if fy < WATER_Y + 10: fails.append('building at %d,%d has its floor at %d, in the water (%s)' % (bx, bz, fy, nearest_poi(bx, bz)))
        if min(hw, hd) * 2 < 2.5 * LINK_H: warns.append('building at %d,%d is only %d wide (%s)' % (bx, bz, min(hw, hd) * 2, nearest_poi(bx, bz)))

    # ---- 2. every chest sits on a floor, in the open, under a ceiling Link fits beneath ----------------------------------------------
    for x, y, z, name in w.loot:
        if y < WATER_Y - 5: fails.append('chest %s at %d,%d,%d is under water (%s)' % (name, x, y, z, nearest_poi(x, z))); continue
        f = floor_under(x, z, y + 12)
        if f is None or y - f > 60: fails.append('chest %s at %d,%d,%d has no floor under it (%s)' % (name, x, y, z, nearest_poi(x, z))); continue
        if y - f < -6: fails.append('chest %s at %d,%d,%d is sunk %d into the floor' % (name, x, y, z, f - y))
        c = headroom(x, z, f)
        if c is not None and c - f < LINK_H + 20: fails.append('chest %s at %d,%d,%d has %d of headroom' % (name, x, y, z, c - f))
        if blocked(x, z, f, 12.0, 12, 60): fails.append('chest %s at %d,%d,%d stands in a wall (%s)' % (name, x, y, z, nearest_poi(x, z)))
        if blocked(x, z, f, 42.0, 12, 60):
            # a chest in a corner is fine, but Link must be able to stand in front of it on at least one side
            free = [d for d in range(8) if not blocked(x + 50 * math.cos(d * math.pi / 4), z + 50 * math.sin(d * math.pi / 4), f, 18.0, 26, 90)
                    and floor_under(x + 50 * math.cos(d * math.pi / 4), z + 50 * math.sin(d * math.pi / 4), f + 30) is not None]
            if not free: fails.append('chest %s at %d,%d,%d cannot be reached from any side (%s)' % (name, x, y, z, nearest_poi(x, z)))

    # ---- 3. every building has its floor and a ceiling high enough ------------------------------------------------------------------
    for bx, bz, hw, hd, fy, yaw in w.buildings:
        f = floor_under(bx, bz, fy + 30)
        if f is None or abs(f - fy) > 30: fails.append('building at %d,%d: floor registered at %d, collision floor %s (%s)' % (bx, bz, fy, f, nearest_poi(bx, bz)))
        c = headroom(bx, bz, fy)
        if c is not None and c - fy < 2.0 * LINK_H: fails.append('building at %d,%d: ceiling only %d over the floor (%s)' % (bx, bz, c - fy, nearest_poi(bx, bz)))

    # ---- 4. doors open onto a floor on both sides and the opening is clear ---------------------------------------------------------
    for x, z, y, name in w.doors:
        if not w.buildings: break
        bx, bz, hw, hd, fy, yaw = min(w.buildings, key=lambda b: math.hypot(b[0] - x, b[1] - z))
        dx, dz = x - bx, z - bz; L = math.hypot(dx, dz)
        if L < 5: continue
        ox, oz = dx / L, dz / L
        for side, k in (('inside', -45), ('outside', 45)):
            px, pz = x + ox * k, z + oz * k
            f = floor_under(px, pz, y + 60)
            if f is None or y - f > 60 or f - y > STEP_UP: fails.append('door of %s at %d,%d: %s floor is %s, door sill %d' % (name, x, z, side, None if f is None else int(f), y)); continue
            if blocked(px, pz, f, 16.0, 26, 90): fails.append('door of %s at %d,%d: %s is walled off' % (name, x, z, side))
        if blocked(x, z, y, 14.0, 26, 90): fails.append('door of %s at %d,%d: the opening is blocked' % (name, x, z))
        c = headroom(x, z, y)
        if c is not None and c - y < LINK_H + 10: fails.append('door of %s at %d,%d: lintel only %d high' % (name, x, z, c - y))

    # ---- 5. nothing floats: every solid's underside rests on the ground or on another solid ------------------------------------------
    floating = []
    for kind in w.blockers:
        if kind[0] == 'box':
            _, x0, x1, z0, z1, y0, y1 = kind; pts = [((x0 + x1) / 2, (z0 + z1) / 2), (x0 + 8, z0 + 8), (x1 - 8, z1 - 8)]
            size = (x1 - x0, y1 - y0, z1 - z0)
        else:
            _, poly, y0, y1 = kind
            cx = sum(p[0] for p in poly) / len(poly); cz = sum(p[1] for p in poly) / len(poly)
            pts = [(cx, cz)] + [(cx + (p[0] - cx) * 0.9, cz + (p[1] - cz) * 0.9) for p in poly[:4]]
            size = (max(p[0] for p in poly) - min(p[0] for p in poly), y1 - y0, max(p[1] for p in poly) - min(p[1] for p in poly))
        if y1 - y0 < 30: continue                 # slabs, steps and footings
        supported = False
        for px, pz in pts:
            if ground(px, pz) >= y0 - 36: supported = True; break
            if ground(px, pz) < WATER_Y and WATER_Y - 40 <= y0 <= WATER_Y + 10: supported = True; break      # afloat
            if any(y0 - 50 <= hy <= y0 + 6 for hy, _ in M.hits(px, pz)): supported = True; break
            if any(wl <= y0 + 6 and wh >= y0 - 50 for wl, wh in M.walls(px, pz, 20.0)): supported = True; break   # set into another solid
        if not supported: floating.append((pts[0][0], y0, pts[0][1], size))
    for x, y0, z, size in floating:
        warns.append('floating solid %dx%dx%d at %d,%d,%d (%s)' % (size[0], size[1], size[2], x, y0, z, nearest_poi(x, z)))

    # ---- 6. slopes the bots would stand on but Link slides off -----------------------------------------------------------------------
    steep = defaultdict(int)
    for k, (a_, b_, c_, s) in enumerate(M.tris):
        ny = M.ny[k]
        if 0.75 <= ny < 1 / math.sqrt(1 + kit.MAX_SLOPE ** 2) - 0.01:
            A = M.V[a_]; steep[nearest_poi(A[0], A[2])] += 1
    for name, n in sorted(steep.items(), key=lambda kv: -kv[1]):
        if n > 12: warns.append('%d faces at %s are between walkable and wall (slope %.2f to %.2f)' % (n, name, kit.MAX_SLOPE, 0.88))

    # ---- 7. reachability: a flood fill with the bots' rules from the lobby ------------------------------------------------------------
    obs = export.obstacles(w, h)
    nx = int(2 * HALF_X / CELL) + 1; nz = int(2 * HALF_Z / CELL) + 1
    def cell(x, z): return int((x + HALF_X) // CELL), int((z + HALF_Z) // CELL)
    def centre(i, j): return -HALF_X + (i + 0.5) * CELL, -HALF_Z + (j + 0.5) * CELL
    gy = np.zeros((nz, nx)); kind = np.zeros((nz, nx), np.int8)       # 0 land, 1 water, 2 blocked
    for j in range(nz):
        for i in range(nx):
            x, z = centre(i, j); t = ground(x, z)
            y = export.walk_floor(w, h, x, z); gy[j, i] = y
            if t < WATER_Y - 15 and y <= t + 1: kind[j, i] = 1; gy[j, i] = WATER_Y
            for x0, x1, z0, z1, py in POOLS:
                if x0 <= x <= x1 and z0 <= z <= z1 and t < py - 15 and y <= t + 1: kind[j, i] = 1; gy[j, i] = py
    for x0, x1, z0, z1 in obs:
        for i in range(max(0, cell(x0, 0)[0]), min(nx - 1, cell(x1, 0)[0]) + 1):
            for j in range(max(0, cell(0, z0)[1]), min(nz - 1, cell(0, z1)[1]) + 1):
                x, z = centre(i, j)
                if x0 <= x <= x1 and z0 <= z <= z1 and kind[j, i] == 0: kind[j, i] = 2
    nodes = routes.upper_nodes(w, lambda x, z: export.walk_floor(w, h, x, z))
    up = defaultdict(list)
    for x, z, y in nodes: up[cell(x, z)].append(y)
    # graph: node = (i, j, level) with level -1 the ground/water and k the k-th upper node of the cell
    def levels(i, j):
        out = [] if kind[j, i] == 2 else [(-1, gy[j, i])]
        out += [(k, y) for k, y in enumerate(up.get((i, j), ()))]
        return out
    def ok(y0, y1, water0, water1):
        if water0 or water1: return abs(y1 - y0) <= CLIMB_UP or (water0 and water1)
        return y1 - y0 <= CLIMB_UP and y0 - y1 <= DROP
    climbs = routes.climb_walls(w)
    links = defaultdict(list)
    for cx, cz, nxn, nzn, y0, y1 in climbs:
        fx, fz = cx + nxn / 100 * 50, cz + nzn / 100 * 50; tx, tz = cx - nxn / 100 * 45, cz - nzn / 100 * 45
        fi, fj = cell(fx, fz); ti, tj = cell(tx, tz)
        if not (0 <= fi < nx and 0 <= fj < nz and 0 <= ti < nx and 0 <= tj < nz): continue
        foot = [(lv, y) for lv, y in levels(fi, fj) if abs(y - y0) <= 160 and (lv >= 0 or kind[fj, fi] == 0)]
        top = [(lv, y) for lv, y in levels(ti, tj) if abs(y - y1) <= 80]
        for lf, _ in foot:
            for lt, _ in top: links[(fi, fj, lf)].append((ti, tj, lt)); links[(ti, tj, lt)].append((fi, fj, lf))
    si, sj = cell(*SPAWN)
    start = (si, sj, -1)
    seen = {start}; q = deque([start])
    while q:
        i, j, lv = q.popleft()
        y = gy[j, i] if lv == -1 else up[(i, j)][lv]
        water = lv == -1 and kind[j, i] == 1
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                if not di and not dj: continue
                ii, jj = i + di, j + dj
                if not (0 <= ii < nx and 0 <= jj < nz): continue
                for lv2, y2 in levels(ii, jj):
                    n2 = (ii, jj, lv2)
                    if n2 in seen: continue
                    if ok(y, y2, water, lv2 == -1 and kind[jj, ii] == 1): seen.add(n2); q.append(n2)
        for n2 in links.get((i, j, lv), ()):
            if n2 not in seen: seen.add(n2); q.append(n2)
    reached = np.zeros((nz, nx), bool)
    for i, j, lv in seen: reached[j, i] = True
    def reach_near(x, z, r, y=None):
        i0, j0 = cell(x, z); R = int(math.ceil(r / CELL))
        for i in range(max(0, i0 - R), min(nx, i0 + R + 1)):
            for j in range(max(0, j0 - R), min(nz, j0 + R + 1)):
                for lv, ly in levels(i, j):
                    if (i, j, lv) in seen and (y is None or abs(ly - y) <= 80): return True
        return False
    for name, x, z, r, boss, desc in POIS:
        if not reach_near(x, z, 120): fails.append('%s cannot be reached from the lobby' % name)
    for x, z, k in ALLY_SPOTS:
        if not reach_near(x, z, 90): fails.append('ally station at %d,%d (%s) cannot be reached' % (x, z, nearest_poi(x, z)))
    for x, z in CART_SPOTS:
        if not reach_near(x, z, 90): fails.append('cart station at %d,%d (%s) cannot be reached' % (x, z, nearest_poi(x, z)))
    unreached_loot = [(x, y, z, n) for x, y, z, n in w.loot if not reach_near(x, z, 70, y)]
    for x, y, z, n in unreached_loot: fails.append('chest %s at %d,%d,%d cannot be reached by a bot (%s)' % (n, x, y, z, nearest_poi(x, z)))
    # islands of land a bot cannot reach
    land = (kind == 0) & (gy > WATER_Y + 5)
    lost = land & ~reached
    comp = np.zeros((nz, nx), int); ncomp = 0; sizes = []
    for j in range(nz):
        for i in range(nx):
            if lost[j, i] and not comp[j, i]:
                ncomp += 1; stack = [(i, j)]; comp[j, i] = ncomp; cells = []
                while stack:
                    ci, cj = stack.pop(); cells.append((ci, cj))
                    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ii, jj = ci + di, cj + dj
                        if 0 <= ii < nx and 0 <= jj < nz and lost[jj, ii] and not comp[jj, ii]: comp[jj, ii] = ncomp; stack.append((ii, jj))
                sizes.append((len(cells), cells))
    sizes.sort(key=lambda s: -s[0])
    for n, cells in sizes:
        if n < 6: continue
        x, z = centre(*cells[len(cells) // 2])
        (warns if n < 40 else fails).append('%d cells of land near %d,%d (%s) cannot be walked to' % (n, x, z, nearest_poi(x, z)))
    pct = 100.0 * reached[land].sum() / max(1, land.sum())

    for item in (a.dump.split(';') if a.dump else []):
        dx, dz, dr = (float(v) for v in item.split(','))
        print('dump', dx, dz, dr)
        i0, j0 = cell(dx, dz); R = int(math.ceil(dr / CELL))
        for j in range(max(0, j0 - R), min(nz, j0 + R + 1)):
            for i in range(max(0, i0 - R), min(nx, i0 + R + 1)):
                x, z = centre(i, j)
                print('cell %6d %6d kind %d ground %7.1f  %s' % (x, z, kind[j, i], gy[j, i], ' '.join('%s%d' % ('+' if (i, j, lv) in seen else '-', y) for lv, y in levels(i, j))))
    # ---- report --------------------------------------------------------------------------------------------------------------------
    rep = {'loot': len(w.loot), 'buildings': len(w.buildings), 'doors': len(w.doors), 'upper_nodes': len(nodes), 'climb_walls': len(climbs),
           'land_reached_percent': round(pct, 1), 'floating': len(floating), 'fails': fails, 'warnings': warns}
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    with open(a.report, 'w') as f: json.dump(rep, f, indent=1)
    print('Skyloft Isles: %d chests, %d buildings, %d doors, %d upper nodes, %d climb walls; %.1f%% of the land reachable from the lobby' %
          (len(w.loot), len(w.buildings), len(w.doors), len(nodes), len(climbs), pct))
    for s in warns: print('warning:', s)
    for s in fails: print('FAIL:', s)
    print('%d failures, %d warnings' % (len(fails), len(warns)))
    if fails or (a.strict and warns): sys.exit(1)

if __name__ == '__main__':
    main()
