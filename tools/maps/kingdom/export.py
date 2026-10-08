"""Writes the game's data for Hyrule Kingdom: shared/kingdom_data.h (ground, collision, loot sites, regions) and shared/kingdom_model.h (the drawn
buildings and trees, 128 x 128 textures). Run:  python3 tools/maps/kingdom/export.py   (about a minute).

Collision is baked into the scene: the terrain's triangles plus every building, tower, bridge and big rock, with a surface per triangle (footsteps,
climbable ivy and cliffs, hook-shot wood). The game keeps a collision vertex number in 13 bits for the first two corners of a triangle and 16 for the
third (patches/0025), so every triangle is written with at most one corner in the high range."""
import os, sys, math, argparse
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import terrain, textures, world, surface_maps, routes
from layout import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
KINDS = BOSS_KINDS
LOW = 8190          # vertex numbers below this are usable in any corner

def arr(f, typ, name, rows, fmt=None):
    f.write('inline constexpr %s %s[] = {\n' % (typ, name))
    for r in rows: f.write(' {' + ','.join(str(v) if not isinstance(v, str) else v for v in r) + '},\n')
    f.write('};\n')

def quote(s): return '"' + s.replace('"', '\\"') + '"'

# ---- collision -----------------------------------------------------------------------------------------------------------------------
def terrain_collision(h, surfaces):
    """The ground's own triangles (deep sea is merged 2 x 2 into one square) as (verts, tris(a, b, c, surface)) with the surface table extended."""
    n = CELLS + 1
    xs = -HALF_X + np.arange(n) * (2 * HALF_X / CELLS); zs = -HALF_Z + np.arange(n) * (2 * HALF_Z / CELLS)
    deep = h <= SEA_FLOOR + 1
    cell_deep = deep[:-1, :-1] & deep[1:, :-1] & deep[:-1, 1:] & deep[1:, 1:]
    cover = np.zeros((CELLS, CELLS), bool)
    quads = []
    for bj in range(0, CELLS, 2):
        for bi in range(0, CELLS, 2):
            if cell_deep[bj:bj + 2, bi:bi + 2].all():
                cover[bj:bj + 2, bi:bi + 2] = True; quads.append((bi, bj, 2))
    for j in range(CELLS):
        for i in range(CELLS):
            if not cover[j, i]: quads.append((i, j, 1))
    ground = surface_index(surfaces, 0, False, False); cliff = surface_index(surfaces, 0, True, False)
    verts = {}; vlist = []
    def V(i, j):
        if (i, j) not in verts:
            verts[(i, j)] = len(vlist); vlist.append((int(round(xs[i])), int(h[j, i]), int(round(zs[j]))))
        return verts[(i, j)]
    tris = []
    def tri(a, b, c):
        A, B, C = (np.array(vlist[k], float) for k in (a, b, c))
        nrm = np.cross(B - A, C - A)
        if nrm[1] < 0: b, c = c, b; nrm = -nrm
        ny = nrm[1] / max(1e-9, np.linalg.norm(nrm))
        tris.append((a, b, c, cliff if ny < 0.5 else ground))
    for i, j, s in quads:
        a, b, c, d = V(i, j), V(i + s, j), V(i, j + s), V(i + s, j + s)
        tri(a, b, d); tri(a, d, c)
    return vlist, tris

def surface_index(surfaces, sfx, climb, hook):
    key = (sfx, bool(climb), bool(hook))
    if key not in surfaces: surfaces.append(key)
    return surfaces.index(key)

def assemble(w, h):
    """All collision in one table: returns surfaces, verts, tris (a, b, c, nx, ny, nz, dist, surface) with high vertex numbers kept to the third corner."""
    surfaces = []
    tv, tt = terrain_collision(h, surfaces)
    verts = list(tv); tris = list(tt)
    remap = {}
    for k, (sfx, climb, hook) in enumerate(w.surfaces): remap[k] = surface_index(surfaces, sfx, climb, hook)
    off = len(verts)
    verts += [tuple(int(c) for c in v) for v in w.col_verts]
    for a, b, c, s in w.col_tris: tris.append((a + off, b + off, c + off, remap[s]))
    total = len(verts)
    need = max(0, total - LOW)
    # high vertices: no two of them in one triangle, so that every triangle has two low corners to put first
    nterr = len(tv); adj = [set() for _ in range(total)]
    for a, b, c, s in tris:
        adj[a].update((b, c)); adj[b].update((a, c)); adj[c].update((a, b))
    high = set(); blocked = set()
    order = sorted(range(total), key=lambda v: (v < nterr, len(adj[v])))
    for v in order:
        if len(high) >= need: break
        if v in blocked: continue
        high.add(v); blocked |= adj[v]
    if len(high) < need: raise SystemExit('cannot place enough high vertices: %d of %d' % (len(high), need))
    lows = [v for v in range(total) if v not in high]; highs = sorted(high)
    new = {v: i for i, v in enumerate(lows + highs)}
    out_verts = [None] * total
    for v in range(total): out_verts[new[v]] = verts[v]
    out_tris = []
    for a, b, c, s in tris:
        ia, ib, ic = new[a], new[b], new[c]
        for _ in range(3):
            if ia < LOW + 1 and ib < LOW + 1: break
            ia, ib, ic = ib, ic, ia          # a cyclic turn keeps the winding
        else: raise SystemExit('triangle with two high corners')
        A, B, C = (np.array(out_verts[k], float) for k in (ia, ib, ic))
        n = np.cross(B - A, C - A); ln = np.linalg.norm(n)
        if ln < 1e-6: continue
        n /= ln
        out_tris.append((ia, ib, ic, int(round(n[0] * 32767)), int(round(n[1] * 32767)), int(round(n[2] * 32767)), int(round(-n.dot(A))), s))
    return surfaces, out_verts, out_tris, len(high)

# ---- obstacles for the bots' grid ----------------------------------------------------------------------------------------------------
def floor_at(h, x, z): return float(terrain.grid_height(h, x, z))

def walk_floor(w, h, x, z):
    """What a bot stands on at (x, z): the ground, a building's floor, or a deck or bridge over it."""
    y = floor_at(h, x, z)
    for bx, bz, hw, hd, fy, yaw in w.buildings:
        dx, dz = x - bx, z - bz; c, s_ = math.cos(yaw), math.sin(yaw)
        if abs(dx * c + dz * s_) <= hw and abs(-dx * s_ + dz * c) <= hd: y = max(y, fy)
    for x0, z0, x1, z1, hw, y0, y1 in w.walkways:
        dx, dz = x1 - x0, z1 - z0; L2 = dx * dx + dz * dz
        if L2 < 1: continue
        t = ((x - x0) * dx + (z - z0) * dz) / L2
        if 0 <= t <= 1 and abs((x - x0) * dz - (z - z0) * dx) / math.sqrt(L2) <= hw:
            wy = y0 + (y1 - y0) * t
            if wy > y - 40: y = max(y, wy)
    return y

def obstacles(w, h):
    out = []
    def add(x0, x1, z0, z1): out.append((round(x0), round(x1), round(z0), round(z1)))
    for kind in w.blockers:
        if kind[0] == 'box':
            _, x0, x1, z0, z1, y0, y1 = kind
            base = walk_floor(w, h, (x0 + x1) / 2, (z0 + z1) / 2)
            if y1 > base + 45 and y0 < base + 150 and y1 - y0 > 60: add(x0, x1, z0, z1)
        else:
            _, pts, y0, y1 = kind
            if y1 - y0 < 110: continue                       # floors and footings are ground to stand on, not things in the way
            n = len(pts)
            for k in range(n):
                (ax, az), (bx, bz) = pts[k], pts[(k + 1) % n]
                base = walk_floor(w, h, (ax + bx) / 2, (az + bz) / 2)
                if not (y1 > base + 45 and y0 < base + 150): continue
                L = math.hypot(bx - ax, bz - az); m = max(1, int(L / 70))
                for q in range(m + 1):
                    px, pz = ax + (bx - ax) * q / m, az + (bz - az) * q / m
                    add(px - 35, px + 35, pz - 35, pz + 35)
    for o in w.obstacles: add(*o)
    return list(dict.fromkeys(out))

# ---- drawing -----------------------------------------------------------------------------------------------------------------------
def split_tris(p, uv, sh):
    """A triangle whose texture coordinates span too many repeats is cut into four."""
    span = max(max(u for u, v in uv) - min(u for u, v in uv), max(v for u, v in uv) - min(v for u, v in uv))
    if span < 6.9: return [(p, uv, sh)]
    m = lambda a, b: tuple((x + y) / 2 for x, y in zip(a, b))
    p01, p12, p20 = m(p[0], p[1]), m(p[1], p[2]), m(p[2], p[0])
    u01, u12, u20 = m(uv[0], uv[1]), m(uv[1], uv[2]), m(uv[2], uv[0])
    s01, s12, s20 = m(sh[0], sh[1]), m(sh[1], sh[2]), m(sh[2], sh[0])
    out = []
    for q in [((p[0], p01, p20), (uv[0], u01, u20), (sh[0], s01, s20)), ((p01, p[1], p12), (u01, uv[1], u12), (s01, sh[1], s12)),
              ((p20, p12, p[2]), (u20, u12, uv[2]), (s20, s12, sh[2])), ((p01, p12, p20), (u01, u12, u20), (s01, s12, s20))]:
        out += split_tris(*q)
    return out

def draw_data(w, names):
    chunks = 8
    groups = {}
    for t in w.tris:
        for p, uv, sh in split_tris(list(t.p), list(t.uv), list(t.shade)):
            cx = max(0, min(chunks - 1, int((sum(q[0] for q in p) / 3 + HALF_X) / (2 * HALF_X) * chunks)))
            cz = max(0, min(chunks - 1, int((sum(q[2] for q in p) / 3 + HALF_Z) / (2 * HALF_Z) * chunks)))
            reach = 15000 if t.far else (4300 if t.group == 'Foliage' else 7000)
            cls = 0 if t.inside else surface_maps.MATERIALS.get(t.mat, 0)   # indoors stays baked: the dim light of a room is part of its look
            groups.setdefault((cx, cz, names.index(t.mat), reach, cls), []).append((p, uv, sh, t.normal))
    verts = []; batches = []
    for (cx, cz, mi, reach, cls), tris in sorted(groups.items()):
        start = len(verts)
        for p, uv, sh, nrm in tris:
            u0 = math.floor(min(u for u, v in uv)); v0 = math.floor(min(v for u, v in uv))
            nb = tuple(int(max(-127, min(127, round(127 * c)))) for c in nrm)
            for q, (u, v), c in zip(p, uv, sh):
                verts.append((int(round(q[0])), int(round(q[1])), int(round(q[2])), int(round((u - u0) * 4096)), int(round((v - v0) * 4096)),
                              *(int(max(0, min(255, round(255 * s)))) for s in c), *nb))
        points = np.array([q for p, uv, sh, normal in tris for q in p])
        centre = np.rint((points.min(axis=0) + points.max(axis=0)) * 0.5).astype(int)
        radius = int(math.ceil(np.linalg.norm(points - centre, axis=1).max())) + 2
        batches.append((start, len(tris) * 3, mi, int(centre[0]), int(centre[2]), reach, cls, int(centre[1]), radius))
    return verts, batches

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--only', default=None); a = ap.parse_args()
    w, h, T = world.build(a.only.split(',') if a.only else None)
    names = list(T)
    surfaces, verts, tris, nhigh = assemble(w, h)
    print('collision: %d vertices (%d high), %d triangles, %d surfaces' % (len(verts), nhigh, len(tris), len(surfaces)))
    props = [(round(p[0]), round(p[1]), round(p[2]), round(p[3]), round(p[4]), round(p[5]), quote(str(p[6]))) for p in w.props]
    obs = obstacles(w, h)
    nodes = routes.upper_nodes(w, lambda x, z: walk_floor(w, h, x, z))
    print('upper nodes: %d' % len(nodes))
    climbs = routes.climb_walls(w)
    print('climbable walls: %d' % len(climbs))
    # ground colours and cover
    N = 385
    xs = -HALF_X + np.arange(N) * (2 * HALF_X / (N - 1)); zs = -HALF_Z + np.arange(N) * (2 * HALF_Z / (N - 1))
    X, Z = np.meshgrid(xs, zs)
    col, _ = terrain.paint(X, Z)
    cx = -HALF_X + (np.arange(256) + .5) * 2 * HALF_X / 256; cz = -HALF_Z + (np.arange(256) + .5) * 2 * HALF_Z / 256
    CX, CZ = np.meshgrid(cx, cz)
    cover = np.where(terrain.height(CX, CZ) < WATER_Y, '0', '3')
    path = os.path.join(ROOT, 'shared', 'kingdom_data.h')
    with open(path, 'w') as f:
        f.write('// GENERATED by tools/maps/kingdom/export.py. Edit the scripts in tools/maps/kingdom, not this file.\n#pragma once\n#include <cstdint>\nnamespace royale { namespace kingdom {\n')
        f.write('inline constexpr int16_t kHeights[] = {' + ','.join(str(int(v)) for v in h.ravel()) + '};\n')
        f.write('inline constexpr uint8_t kColours[] = {' + ','.join(str(int(round(v))) for v in col.reshape(-1)) + '};\n')
        f.write('inline constexpr char kCover[] =\n' + ''.join('"' + ''.join(cover[j]) + '"\n' for j in range(256)) + ';\n')
        f.write('struct Surface { uint8_t sfx; bool climb, hook; };\n')
        arr(f, 'Surface', 'kSurfaces', [(s, 'true' if c else 'false', 'true' if k else 'false') for s, c, k in surfaces])
        f.write('struct Vertex { int16_t x,y,z; };\nstruct Triangle { uint16_t a,b,c; int16_t nx,ny,nz,dist; uint8_t surface; int16_t y; uint16_t radius; };\n')
        arr(f, 'Vertex', 'kCollisionVertices', verts); arr(f, 'Triangle', 'kCollisionTriangles', tris)
        f.write('struct PropCollider { int16_t x0,y0,z0,x1,y1,z1; const char* name; };\n'); arr(f, 'PropCollider', 'kPropColliders', props)
        f.write('struct Obstacle { float x0,x1,z0,z1; };\n'); arr(f, 'Obstacle', 'kObstacles', obs)
        f.write('struct Building { float x,z,halfWidth,halfDepth,floorY,yaw; };\n')
        arr(f, 'Building', 'kBuildings', [(round(b[0]), round(b[1]), round(b[2], 1), round(b[3], 1), round(b[4]), round(b[5], 4)) for b in w.buildings])
        f.write('struct Walkway { float x0,z0,x1,z1,halfWidth,y0,y1; };   // a deck, bridge or ramp: the floor along the strip from (x0, z0) to (x1, z1)\n')
        arr(f, 'Walkway', 'kWalkways', [(round(v[0]), round(v[1]), round(v[2]), round(v[3]), round(v[4], 1), round(v[5]), round(v[6])) for v in w.walkways])
        f.write('struct UpperNode { int16_t x,z,y; };   // somewhere to stand above the ground: a floor, ramp, roof or landing (tools/maps/kingdom/routes.py)\n'); arr(f, 'UpperNode', 'kUpperNodes', nodes)
        f.write('struct ClimbWall { int16_t x,z,nx,nz,y0,y1; };   // a climbable face: where, which way it faces (hundredths), lowest and highest point\n'); arr(f, 'ClimbWall', 'kClimbWalls', climbs)
        f.write('struct LootSite { float x,y,z; };\n'); arr(f, 'LootSite', 'kLootSites', [(round(l[0]), round(l[1]), round(l[2])) for l in w.loot])
        f.write('struct Region { const char* name; float x,z; int boss; };\n')
        arr(f, 'Region', 'kRegions', [(quote(n), x, z, KINDS.get(b, -1)) for n, x, z, r, b, d in POIS])
        f.write('} }\n')
    dv, batches = draw_data(w, names)
    print('drawn: %d vertices, %d batches, %d obstacles, %d props' % (len(dv), len(batches), len(obs), len(props)))
    path = os.path.join(ROOT, 'shared', 'kingdom_model.h')
    with open(path, 'w') as f:
        f.write('// GENERATED by tools/maps/kingdom/export.py.\n#pragma once\n#include <cstdint>\nnamespace royale { namespace kingdom {\nstruct DrawVertex { int16_t x,y,z,s,t; uint8_t r,g,b; int8_t nx,ny,nz; };   // r,g,b: the baked light; nx,ny,nz: the face normal for the lit, bump-mapped draw\n')
        arr(f, 'DrawVertex', 'kDrawVertices', dv)
        f.write('struct Batch { uint32_t first,count; uint16_t texture; int16_t x,z; uint16_t reach; uint8_t surface; int16_t y; uint16_t radius; };   // surface: 0, or the bump map class (kSurfaceUnits)\n'); arr(f, 'Batch', 'kBatches', batches)
        f.write('inline constexpr int kTextureSize = 128;\n// RGBA5551 texture bytes, high byte first; independent of host byte order.\n')
        f.write('alignas(8) inline constexpr uint8_t kTextures[][%d] = {\n' % (128 * 128 * 2))
        for n in names: f.write('{' + ','.join(map(str, textures.to_native(T[n][0]))) + '},\n')
        f.write('};\n} }\n')
    print('wrote', path)

if __name__ == '__main__':
    main()
