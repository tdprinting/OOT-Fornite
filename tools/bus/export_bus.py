"""Turns assets/bus/bus.blend into shared/bus_model.h (the mesh and textures the game draws) and shared/bus_geometry.h (its measures and the points
the game needs: seats, rope ends, lantern hooks, wheels, the exit, the horn).

    python3 tools/bus/export_bus.py          (or: blender -b assets/bus/bus.blend --python tools/bus/export_bus.py)

Game units and axes (Y up, the bus facing +Z, the floor centre at the origin). Each rigid part's vertices are relative to its pivot. Vertices go in
batches of at most 32 (what the graphics chip loads at a time); every batch is one part, one texture and one kind of shading (lit, or glowing:
the runes, lamps, eyes and flame ignore the light). The textures are RGBA5551, the N64's 16-bit format.
"""
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BLEND = os.path.join(ROOT, "assets", "bus", "bus.blend")
MODEL = os.path.join(ROOT, "shared", "bus_model.h")
GEOMETRY = os.path.join(ROOT, "shared", "bus_geometry.h")
SCALE = 100.0
MAX_BATCH = 32
PARTS = ["BusBody", "Roof", "Moon", "MoonFace", "MoonEyes", "Propeller", "Lantern", "Wheel", "Rope", "Banner", "Flame", "BodyFar", "MoonFar"]
ENUM = ["kBody", "kRoof", "kMoon", "kMoonFace", "kMoonEyes", "kPropeller", "kLantern", "kWheel", "kRope", "kBanner", "kFlame", "kBodyFar", "kMoonFar"]
# material -> (texture, glows)
MAT = {"atlas": (0, 0), "atlas_lacquer": (0, 0), "atlas_glow": (0, 1), "moon_face": (1, 0), "moon_rock": (2, 0), "atlas_flame": (0, 1)}
TEXTURES = [("kTexAtlas", "bus_atlas", 256, 256), ("kTexMoonFace", "moon_face", 256, 256), ("kTexMoonRock", "moon_rock", 128, 128)]

C = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))   # Blender (Z up, facing -Y) to game (Y up, facing +Z)


def to_game(v):
    return C @ Vector(v)


def rgba5551(img, w, h):
    px = list(img.pixels)
    out = bytearray()
    for row in range(h - 1, -1, -1):
        for col in range(w):
            i = (row * w + col) * 4
            r, g, b = (min(31, max(0, int(round(px[i + k] * 31)))) for k in range(3))
            a = 1 if px[i + 3] >= 0.5 else 0
            v = (r << 11) | (g << 6) | (b << 1) | a
            out += bytes(((v >> 8) & 0xFF, v & 0xFF))
    return out


def rows(values, per_line, indent="    "):
    return "\n".join(indent + ", ".join(values[i:i + per_line]) + "," for i in range(0, len(values), per_line))


def f(v):
    return "%.2ff" % v


def main():
    if bpy.data.filepath != BLEND:
        bpy.ops.wm.open_mainfile(filepath=BLEND)
    verts, batches, tris, pivots = [], [], [], []
    lo, hi = Vector((1e9, 1e9, 1e9)), Vector((-1e9, -1e9, -1e9))
    for part, name in enumerate(PARTS):
        obj = bpy.data.objects[name]
        me = obj.data
        me.calc_loop_triangles()
        uv = me.uv_layers.active.data
        normals = me.corner_normals
        alpha = me.color_attributes.get("alpha")
        pivot = to_game(obj.matrix_world.translation) * SCALE
        pivots.append(pivot)
        # group the triangles by texture and shading, so a part switches texture as few times as possible
        groups = {}
        for lt in me.loop_triangles:
            mat = me.materials[lt.material_index].name
            groups.setdefault(MAT[mat], []).append(lt)
        for (tex, glow), lts in sorted(groups.items()):
            tw, th = TEXTURES[tex][2], TEXTURES[tex][3]
            cur = None
            for lt in lts:
                cs = []
                for li, vi in zip(lt.loops, lt.vertices):
                    p = to_game(me.vertices[vi].co) * SCALE
                    n = to_game(normals[li].vector).normalized()
                    u, v = uv[li].uv
                    s, t = round(u * tw * 32), round((1.0 - v) * th * 32)
                    a = round(alpha.data[vi].color[3] * 255) if alpha is not None else 255
                    if name not in ("Rope", "BodyFar", "MoonFar"):
                        world = p + pivot
                        lo = Vector((min(lo.x, world.x), min(lo.y, world.y), min(lo.z, world.z)))
                        hi = Vector((max(hi.x, world.x), max(hi.y, world.y), max(hi.z, world.z)))
                    key = (vi, s, t, round(n.x * 127), round(n.y * 127), round(n.z * 127))
                    cs.append((key, (p.x, p.y, p.z, key[3], key[4], key[5], s, t, a)))
                new = len({k for k, _ in cs if cur is None or k not in cur["map"]})
                if cur is None or len(cur["map"]) + new > MAX_BATCH:
                    cur = {"part": part, "tex": tex, "glow": glow, "map": {}, "first": len(verts), "tris_first": len(tris)}
                    batches.append(cur)
                idx = []
                for k, data in cs:
                    if k not in cur["map"]:
                        cur["map"][k] = len(cur["map"])
                        verts.append(data)
                    idx.append(cur["map"][k])
                tris.append(idx)
    for i, b in enumerate(batches):
        b["count"] = len(b["map"])
        b["tris_count"] = (batches[i + 1]["tris_first"] if i + 1 < len(batches) else len(tris)) - b["tris_first"]
    for v in verts:
        assert max(abs(v[0]), abs(v[1]), abs(v[2])) * 8 < 32000, "a vertex too far from its pivot for 1/8-unit s16 coordinates"

    out = []
    w = out.append
    w("#pragma once")
    w("// GENERATED by tools/bus/export_bus.py from assets/bus/bus.blend (made by tools/bus/build_bus.py). Do not edit by hand:")
    w("// change the build script (or the Blender file) and run the export again.")
    w("//")
    w("// The Moonfall Bus (docs/BATTLE_BUS.md): %d vertices in %d batches, %d triangles, %d parts, textures 256x256, 256x256 and 128x128." %
      (len(verts), len(batches), len(tris), len(PARTS)))
    w("// Game units, Y up, facing +Z, the floor centre at the origin. Each vertex is relative to its part's pivot (shared/bus_geometry.h).")
    w("#include \"bus_geometry.h\"")
    w("#include <cstdint>")
    w("")
    w("namespace royale {")
    w("namespace busmodel {")
    w("")
    w("// A vertex: position (relative to its part's pivot), normal (x127), texel coordinate (1/32 texels) and alpha (the face decal fades out).")
    w("struct Vert { float x, y, z; int8_t nx, ny, nz; int16_t s, t; uint8_t a; };")
    w("// A batch: up to 32 vertices of one part, one texture (0 the atlas, 1 the Moon's face, 2 its rock) and one shading (glow: unlit).")
    w("struct Batch { uint8_t part, tex, glow; uint16_t vertCount; uint32_t firstVert; uint32_t firstTri; uint16_t triCount; };")
    w("")
    w("constexpr int kVertCount = %d;" % len(verts))
    w("inline constexpr Vert kVerts[kVertCount] = {")
    w(rows(["{%s, %s, %s, %d, %d, %d, %d, %d, %d}" % (f(v[0]), f(v[1]), f(v[2]), v[3], v[4], v[5], v[6], v[7], v[8]) for v in verts], 3))
    w("};")
    w("constexpr int kBatchCount = %d;" % len(batches))
    w("inline constexpr Batch kBatches[kBatchCount] = {")
    w(rows(["{%d, %d, %d, %d, %d, %d, %d}" % (b["part"], b["tex"], b["glow"], b["count"], b["first"], b["tris_first"], b["tris_count"]) for b in batches], 4))
    w("};")
    w("constexpr int kTriCount = %d;" % len(tris))
    w("inline constexpr uint8_t kTris[kTriCount][3] = {")
    w(rows(["{%d, %d, %d}" % tuple(t) for t in tris], 12))
    w("};")
    w("")
    w("// RGBA5551, big-endian, top row first.")
    w("constexpr int kTexCount = %d;" % len(TEXTURES))
    w("inline constexpr int kTexW[kTexCount] = { %s };" % ", ".join(str(t[2]) for t in TEXTURES))
    w("inline constexpr int kTexH[kTexCount] = { %s };" % ", ".join(str(t[3]) for t in TEXTURES))
    for cname, img, tw, th in TEXTURES:
        data = rgba5551(bpy.data.images[img], tw, th)
        w("alignas(8) inline constexpr uint8_t %s[%d] = {" % (cname, tw * th * 2))
        w(rows(["0x%02X" % b for b in data], 32))
        w("};")
    w("inline constexpr const uint8_t* kTextures[kTexCount] = { %s };" % ", ".join(t[0] for t in TEXTURES))
    w("")
    w("} // namespace busmodel")
    w("} // namespace royale")
    with open(MODEL, "w") as fh:
        fh.write("\n".join(out) + "\n")

    def point(name):
        return to_game(bpy.data.objects[name].matrix_world.translation) * SCALE

    def p3(p):
        return "{%s, %s, %s}" % (f(p.x), f(p.y), f(p.z))

    g = []
    w = g.append
    w("#pragma once")
    w("// GENERATED by tools/bus/export_bus.py from assets/bus/bus.blend. Do not edit by hand.")
    w("//")
    w("// The Moonfall Bus's measures, in game units (Y up, facing +Z, the floor centre at the origin). The seats and the exit match shared/bus.h")
    w("// (BusSeatLocal, kBusExitX/Y/Z), which the server and the bots use; tests/bus_model_tests check they agree.")
    w("")
    w("namespace royale {")
    w("namespace busmodel {")
    w("")
    w("enum Part : int { %s, kPartCount };" % ", ".join(ENUM))
    w("struct P3 { float x, y, z; };")
    w("// The point each part turns about (the Moon sways about its collar, the propeller spins about its hub; the lantern, the wheel and the")
    w("// rope are drawn several times, at the points below).")
    w("inline constexpr P3 kPivot[kPartCount] = {")
    w(rows([p3(p) for p in pivots], 3))
    w("};")
    w("constexpr int kSeatCount = 32;")
    w("inline constexpr P3 kSeat[kSeatCount] = {")
    w(rows([p3(point("Seat%02d" % s)) for s in range(32)], 4))
    w("};")
    w("inline constexpr P3 kLanternHook[8] = {")
    w(rows([p3(point("Lantern%d" % k)) for k in range(8)], 4))
    w("};")
    w("// Wheels: front right, front left, back right, back left (x > 0 is the bus's left in the game's axes); the left ones are turned round.")
    w("inline constexpr P3 kWheelAxle[4] = { %s };" % ", ".join(p3(point("Wheel%d" % k)) for k in range(4)))
    w("// The four ropes: from the burner cage's ring (on the body) up to their knots on the net (in the Moon's frame before it sways, measured")
    w("// from the body's origin like everything else here).")
    w("inline constexpr P3 kRopeBody[4] = { %s };" % ", ".join(p3(point("RopeBody%d" % k)) for k in range(4)))
    w("inline constexpr P3 kRopeMoon[4] = { %s };" % ", ".join(p3(point("RopeMoon%d" % k)) for k in range(4)))
    for name, cname in (("Exit", "kExit"), ("Horn", "kHorn"), ("Smoke", "kNozzle"), ("Pilot", "kPilot"), ("LobbyRamp", "kLobbyRamp")):
        w("inline constexpr P3 %s = %s;" % (cname, p3(point(name))))
    w("// The box it fills (the Moon included).")
    w("constexpr float kMinX = %s, kMaxX = %s, kMinY = %s, kMaxY = %s, kMinZ = %s, kMaxZ = %s;" % (f(lo.x), f(hi.x), f(lo.y), f(hi.y), f(lo.z), f(hi.z)))
    w("")
    w("} // namespace busmodel")
    w("} // namespace royale")
    with open(GEOMETRY, "w") as fh:
        fh.write("\n".join(g) + "\n")
    print("wrote %s (%d vertices, %d batches, %d triangles) and %s" % (MODEL, len(verts), len(batches), len(tris), GEOMETRY))


if __name__ == "__main__":
    main()
