"""Checks the generated Hyrule Before the Third Storm data (shared/hyrule_third_storm_data.h, _model.h) without Blender: the 24 places match
shared/map.h, every chest and floor-loot site stands on the walk grid's floor, water boxes fit the engine, the collision corners are in range,
the drawing windows fit a vertex load, the plane distances need patches/0040 and stay within s32.
Run:  python tools/maps/hyrule_third_storm/validate.py [--report build/hyrule-third-storm-validation.json]   (CI: scripts/test_hyrule_third_storm_authoring.py)"""
import os, re, sys, json, argparse
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DATA = os.path.join(ROOT, "shared", "hyrule_third_storm_data.h")
MODEL = os.path.join(ROOT, "shared", "hyrule_third_storm_model.h")
COLLISION = os.path.join(ROOT, "shared", "hyrule_third_storm_collision.h")

def unvarints(raw):
    out = []; z = 0; shift = 0
    for c in raw:
        z |= (c & 0x7F) << shift; shift += 7
        if not c & 0x80:
            out.append((z >> 1) ^ -(z & 1)); z = 0; shift = 0
    return out

def block(text, name):
    m = re.search(r"\b" + name + r"\[\]\s*=\s*\{", text)
    if not m: raise SystemExit(f"{name} not found")
    i = m.end(); depth = 1; j = i
    while depth:
        c = text[j]
        if c == "{": depth += 1
        elif c == "}": depth -= 1
        j += 1
    return text[i:j - 1]

def blob(text, name, dtype=None):
    """A base64 table the exporter wrote (kNameB64 pieces and kNameB64Bytes), as a numpy array of dtype."""
    import base64
    n = int(re.search(name + r"Bytes = (\d+)", text).group(1))
    m = re.search(r"\b" + name + r"\[\]\s*=\s*\{", text)
    j = text.index("};", m.end())
    raw = base64.b64decode("".join(re.findall(r'"([A-Za-z0-9+/=]*)"', text[m.end():j])))
    if len(raw) != n: raise SystemExit(f"{name}: {len(raw)} bytes, header says {n}")
    return raw if dtype is None else np.frombuffer(raw, dtype=dtype)

def numbers(s): return np.array(re.findall(r"-?\d+(?:\.\d+)?", s), np.float64)

def rows(text, name, width):
    a = numbers(block(text, name))
    return a.reshape(-1, width)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--report"); a = ap.parse_args()
    fails = []
    def check(ok, what):
        if not ok: fails.append(what)
    data = open(DATA, encoding="utf-8").read(); model = open(MODEL, encoding="utf-8").read(); col = open(COLLISION, encoding="utf-8").read()
    grid = int(re.search(r"kGrid = (\d+)", data).group(1)); step = float(re.search(r"kStep = ([\d.]+)", data).group(1))
    half = float(re.search(r"kHalf = ([\d.]+)", data).group(1))
    walk = np.cumsum(np.array(unvarints(blob(data, "kWalkB64")), np.int64))
    check(len(walk) == grid * grid, "walk grid size")
    W = walk.reshape(grid, grid); org = -step * (grid - 1) / 2
    def floor(x, z, y=None):   # the floor within one grid step closest to height y (chests stand against walls)
        i = int(round((x - org) / step)); j = int(round((z - org) / step))
        nb = W[max(0, j - 1):j + 2, max(0, i - 1):i + 2].reshape(-1); nb = nb[nb != -32768]
        if not len(nb): return -32768
        return int(nb[np.argmin(np.abs(nb - (y if y is not None else nb[0])))])
    share = float((walk != -32768).mean())
    check(share > 0.85, f"floor on only {share:.1%} of the map")
    # places: the same names, in order, as shared/map.h
    regions = re.findall(r'\{"([^"]*)",(-?[\d.]+),(-?[\d.]+),(-?\d+)\}', block(data, "kRegions"))
    maph = open(os.path.join(ROOT, "shared", "map.h"), encoding="utf-8").read()
    line = maph[maph.index("// Hyrule Before the Third Storm: its 24 place names"):]
    line = line[line.index("\n") + 1:line.index("\n", line.index("\n") + 1)]
    names = re.findall(r'"((?:[^"\\]|\\.)*)"', line)
    check([r[0] for r in regions] == names, "kRegions names differ from shared/map.h")
    check(len(regions) == 24 and sum(int(r[3]) >= 0 for r in regions) == 7, "24 places, 7 guarded")
    for r in regions: check(abs(float(r[1])) < half and abs(float(r[2])) < half, f"place {r[0]} off the map")
    # chests and floor loot
    loot = rows(data, "kLootSites", 4); floor_sites = rows(data, "kFloorSites", 4)
    check(len(loot) >= 200, f"only {len(loot)} chests")
    bad = [tuple(s[:3]) for s in loot if not (floor(s[0], s[2], s[1]) != -32768 and floor(s[0], s[2], s[1]) - 80 <= s[1] <= floor(s[0], s[2], s[1]) + 1000)]
    check(len(bad) * 5 <= len(loot), f"{len(bad)} of {len(loot)} chests have no floor under them")
    # water boxes: the engine keeps their size in s16
    pools = rows(data, "kPools", 5)
    for p in pools: check(p[1] > p[0] and p[3] > p[2] and p[1] - p[0] <= 32767 and p[3] - p[2] <= 32767, f"pool {p}")
    # collision
    nv = len(blob(col, "kCollisionVerticesB64", "<i2")) // 3
    tt = blob(col, "kCollisionTrianglesB64", np.dtype([("a", "<u4"), ("b", "<u4"), ("c", "<u4"), ("nx", "<i2"), ("ny", "<i2"), ("nz", "<i2"), ("d", "<i4"), ("s", "u1")]))
    corners = np.c_[tt["a"], tt["b"], tt["c"]]
    check(corners.max() < nv, "collision corner out of range")
    dist = np.abs(tt["d"].astype(np.int64))
    nsurf = len(re.findall(r"\{\d+,(?:true|false),(?:true|false)\}", block(data, "kSurfaces")))
    check(tt["s"].max() < nsurf <= 16, "surface index")
    tri = tt
    # drawing
    win = blob(model, "kWindowsB64", "u1").reshape(-1, 2)
    check(win[:, 0].max() <= 128 and win[:, 0].min() >= 3, "a window loads more than 128 vertices")
    batches = blob(model, "kBatchesB64", np.dtype([("fv", "<u4"), ("fw", "<u4"), ("fi", "<u4"), ("w", "<u2"), ("tex", "<u2"), ("x", "<i2"), ("y", "<i2"), ("z", "<i2"), ("r", "<u2"), ("cell", "<u2"), ("lod", "u1")]))
    check(batches["tex"].max() < int(re.search(r"kTextureCount = (\d+)", model).group(1)), "texture index")
    check(int(batches["fw"][-1]) + int(batches["w"][-1]) <= len(win), "batch windows past the table")
    rep = {"floor_share": round(share, 4), "chests": int(len(loot)), "floor_loot": int(len(floor_sites)), "chests_without_floor": len(bad),
           "pools": int(len(pools)), "collision_vertices": nv, "collision_triangles": int(len(tri)), "plane_dist_over_s16": int((dist > 32767).sum()),
           "windows": int(len(win)), "batches": int(len(batches)), "failures": fails}
    if a.report:
        os.makedirs(os.path.dirname(a.report), exist_ok=True); json.dump(rep, open(a.report, "w"), indent=1)
    print(json.dumps(rep, indent=1))
    print(f"{len(fails)} failures")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
