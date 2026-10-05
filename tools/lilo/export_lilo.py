"""Turns assets/lilo/lilo.blend into shared/lilo_model.h, the data the game draws Lilo from.

    python3 tools/lilo/export_lilo.py          (or: blender -b assets/lilo/lilo.blend --python tools/lilo/export_lilo.py)

What it writes, all in game units and the game's axes (Y up, Lilo facing +Z):
  - the vertices, grouped into batches of at most 32 (what the N64 graphics chip loads at a time), each with a texel coordinate, a normal
    and up to two bones;
  - the triangles of each batch, and which texture the batch uses (fur or face);
  - the two textures as RGBA16 (the N64's 5-5-5-1 format), the face in three versions (eyes open, half shut, shut);
  - every animation clip, one pose per frame at the clip's rate: for each bone, the rotation and offset that carry a vertex from the rest
    pose to the posed one (so the game needs no bone hierarchy: a vertex is just rotated and moved by its bones and blended).
"""
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import build_lilo  # noqa: E402  (the clip list and texture sizes live there)

BLEND = os.path.join(ROOT, "assets", "lilo", "lilo.blend")
HEADER = os.path.join(ROOT, "shared", "lilo_model.h")
SCALE = 100.0        # metres in Blender to game units
POS_FRAC = 16.0      # bone offsets are stored in 1/16 units
MAX_BATCH = 32

C = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))   # Blender (Z up, facing -Y) to game (Y up, facing +Z)


def to_game(v):
    return C @ v


def rgba5551(pix_rgba, w, h):
    """Blender pixels (bottom row first, floats) to big-endian RGBA5551 bytes, top row first."""
    out = bytearray()
    for row in range(h - 1, -1, -1):
        for col in range(w):
            i = (row * w + col) * 4
            r, g, b = (min(31, max(0, int(round(pix_rgba[i + k] * 31)))) for k in range(3))
            v = (r << 11) | (g << 6) | (b << 1) | 1
            out += bytes(((v >> 8) & 0xFF, v & 0xFF))
    return out


def fmt_rows(values, per_line, indent="    "):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append(indent + ", ".join(values[i:i + per_line]) + ",")
    return "\n".join(lines)


def main():
    if bpy.data.filepath != BLEND:
        bpy.ops.wm.open_mainfile(filepath=BLEND)
    scene = bpy.context.scene
    rig = bpy.data.objects["LiloRig"]
    mesh_obj = bpy.data.objects["Lilo"]
    me = mesh_obj.data
    bones = [b.name for b in rig.data.bones]
    bone_index = {n: i for i, n in enumerate(bones)}
    group_bone = {g.index: bone_index[g.name] for g in mesh_obj.vertex_groups if g.name in bone_index}

    # ---- geometry -----------------------------------------------------------------------------------------------------------
    me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    normals = me.corner_normals
    mat_tex = []
    for m in me.materials:
        mat_tex.append("face" if "Face" in m.name else "fur")
    tex_size = {"fur": (build_lilo.FUR_W, build_lilo.FUR_H), "face": (build_lilo.FACE_W, build_lilo.FACE_H)}

    def corner(li, vi, mat):
        w, h = tex_size[mat_tex[mat]]
        u, v = uv[li].uv
        s, t = u * w, (1.0 - v) * h
        p = to_game(me.vertices[vi].co) * SCALE
        n = to_game(normals[li].vector).normalized()
        groups = sorted(((g.weight, group_bone[g.group]) for g in me.vertices[vi].groups if g.group in group_bone and g.weight > 0.001),
                        reverse=True)[:2]
        if not groups:
            groups = [(1.0, bone_index["root"])]
        total = sum(g[0] for g in groups)
        b0, w0 = groups[0][1], groups[0][0] / total
        b1 = groups[1][1] if len(groups) > 1 else b0
        key = (vi, round(s * 32), round(t * 32), round(n.x * 127), round(n.y * 127), round(n.z * 127))
        return key, (p.x, p.y, p.z, round(n.x * 127), round(n.y * 127), round(n.z * 127), round(s * 32), round(t * 32), b0, b1,
                     round(w0 * 255))

    tris_by_mat = {}
    for lt in me.loop_triangles:
        tris_by_mat.setdefault(lt.material_index, []).append(lt)
    verts, batches, tris = [], [], []
    for mat in sorted(tris_by_mat):
        cur = None
        for lt in tris_by_mat[mat]:
            cs = [corner(li, vi, mat) for li, vi in zip(lt.loops, lt.vertices)]
            new = len({k for k, _ in cs if cur is None or k not in cur["map"]})
            if cur is None or len(cur["map"]) + new > MAX_BATCH:
                cur = {"mat": mat, "map": {}, "first": len(verts), "tris_first": len(tris)}
                batches.append(cur)
            idx = []
            for k, data in cs:
                if k not in cur["map"]:
                    cur["map"][k] = len(cur["map"])
                    verts.append(data)
                idx.append(cur["map"][k])
            tris.append(idx)
        # batch sizes are filled in below
    for b in batches:
        b["count"] = len(b["map"])
    for i, b in enumerate(batches):
        b["tris_count"] = (batches[i + 1]["tris_first"] if i + 1 < len(batches) else len(tris)) - b["tris_first"]

    # ---- textures ----------------------------------------------------------------------------------------------------------
    fur = bpy.data.images["lilo_fur"]
    faces = [bpy.data.images["lilo_face_%s" % n] for n in ("open", "half", "shut")]
    fur_bytes = rgba5551(list(fur.pixels), fur.size[0], fur.size[1])
    face_bytes = [rgba5551(list(f.pixels), f.size[0], f.size[1]) for f in faces]

    # ---- animation ---------------------------------------------------------------------------------------------------------
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    rest_inv = {b.name: b.matrix_local.inverted() for b in rig.data.bones}
    clips = []
    pose_values = []
    for name, seconds, loops, _ in build_lilo.CLIPS:
        act = bpy.data.actions[name]
        rig.animation_data.action = act
        frames = build_lilo.frames_of(seconds, loops)
        first = len(pose_values) // 7 // len(bones)
        for f in range(frames):
            scene.frame_set(f)
            for bname in bones:
                m = rig.pose.bones[bname].matrix @ rest_inv[bname]
                q = m.to_quaternion().normalized()
                if q.w < 0:
                    q.negate()
                vec = to_game(Vector((q.x, q.y, q.z)))   # the vector part turns with the axes; w stays
                qg = (vec.x, vec.y, vec.z, q.w)
                t = to_game(m.translation) * SCALE
                pose_values += [round(c * 32767) for c in qg] + [round(c * POS_FRAC) for c in t]
        clips.append((name, frames, float(act.get("lilo_fps", build_lilo.FPS)), loops, first))

    # ---- write -------------------------------------------------------------------------------------------------------------
    o = []
    o.append("#pragma once")
    o.append("// GENERATED by tools/lilo/export_lilo.py from assets/lilo/lilo.blend (made by tools/lilo/build_lilo.py). Do not edit by hand:")
    o.append("// change the Blender file or the build script and run the export again.")
    o.append("//")
    o.append("// Lilo the cat, low poly in the N64 style: %d vertices in %d batches, %d triangles, %d bones, %d animation clips." %
             (len(verts), len(batches), len(tris), len(bones), len(clips)))
    o.append("// Game units, Y up, facing +Z, standing on y = 0. See shared/lilo_anim.h for how it is posed and skinned.")
    o.append("#include <cstdint>")
    o.append("")
    o.append("namespace royale {")
    o.append("namespace lilo {")
    o.append("")
    o.append("constexpr int kBoneCount = %d;" % len(bones))
    o.append("inline constexpr const char* kBoneNames[kBoneCount] = { %s };" % ", ".join('"%s"' % b for b in bones))
    o.append("")
    o.append("// A vertex: position, normal (x127), texel coordinate (in 1/32 texels, the N64's S10.5), two bones and the first one's weight (x255).")
    o.append("struct Vert { float x, y, z; int8_t nx, ny, nz; int16_t s, t; uint8_t b0, b1, w0; };")
    o.append("enum Texture : uint8_t { kFur = 0, kFace = 1 };")
    o.append("// A batch: up to 32 vertices loaded together and the triangles drawn from them (indices are within the batch).")
    o.append("struct Batch { Texture texture; uint16_t firstVert; uint8_t vertCount; uint16_t firstTri; uint16_t triCount; };")
    o.append("")
    o.append("constexpr int kVertCount = %d;" % len(verts))
    o.append("inline constexpr Vert kVerts[kVertCount] = {")
    o.append(fmt_rows(["{%.2ff, %.2ff, %.2ff, %d, %d, %d, %d, %d, %d, %d, %d}" % v for v in verts], 3))
    o.append("};")
    o.append("constexpr int kBatchCount = %d;" % len(batches))
    o.append("inline constexpr Batch kBatches[kBatchCount] = {")
    o.append(fmt_rows(["{%s, %d, %d, %d, %d}" % ("kFace" if mat_tex[b["mat"]] == "face" else "kFur", b["first"], b["count"], b["tris_first"],
                                                b["tris_count"]) for b in batches], 4))
    o.append("};")
    o.append("constexpr int kTriCount = %d;" % len(tris))
    o.append("inline constexpr uint8_t kTris[kTriCount][3] = {")
    o.append(fmt_rows(["{%d, %d, %d}" % tuple(t) for t in tris], 12))
    o.append("};")
    o.append("")
    o.append("// Textures, RGBA16 (5-5-5-1, big-endian, top row first). The fur atlas is %dx%d; the face is %dx%d with the eyes open, half shut and shut." %
             (build_lilo.FUR_W, build_lilo.FUR_H, build_lilo.FACE_W, build_lilo.FACE_H))
    o.append("constexpr int kFurW = %d, kFurH = %d, kFaceW = %d, kFaceH = %d;" % (build_lilo.FUR_W, build_lilo.FUR_H, build_lilo.FACE_W,
                                                                                 build_lilo.FACE_H))
    o.append("alignas(8) inline constexpr uint8_t kFurTex[kFurW * kFurH * 2] = {")
    o.append(fmt_rows(["0x%02X" % b for b in fur_bytes], 32))
    o.append("};")
    o.append("enum Eyes : uint8_t { kEyesOpen = 0, kEyesHalf = 1, kEyesShut = 2 };")
    o.append("alignas(8) inline constexpr uint8_t kFaceTex[3][kFaceW * kFaceH * 2] = {")
    for fb in face_bytes:
        o.append("  {")
        o.append(fmt_rows(["0x%02X" % b for b in fb], 32, "    "))
        o.append("  },")
    o.append("};")
    o.append("")
    o.append("// Animation clips, in the order of build_lilo.CLIPS.")
    o.append("enum Clip : uint8_t { %s, kClipCount };" % ", ".join("k%s" % n.capitalize() for n, _, _, _, _ in clips))
    o.append("struct ClipInfo { const char* name; uint16_t frames; float fps; bool loops; uint16_t firstFrame; };")
    o.append("inline constexpr ClipInfo kClips[kClipCount] = {")
    o.append(fmt_rows(['{"%s", %d, %.1ff, %s, %d}' % (n, f, fps, "true" if lp else "false", first) for n, f, fps, lp, first in clips], 3))
    o.append("};")
    o.append("constexpr int kFrameCount = %d;" % (len(pose_values) // 7 // len(bones)))
    o.append("constexpr float kPosFrac = %.1ff;   // bone offsets are in 1/%d units" % (POS_FRAC, POS_FRAC))
    o.append("// Per frame, per bone: rotation quaternion x, y, z, w (x32767) and offset x, y, z (x kPosFrac). A vertex v at rest goes to q*v + offset.")
    o.append("inline constexpr int16_t kPoses[kFrameCount * kBoneCount * 7] = {")
    o.append(fmt_rows([str(v) for v in pose_values], 28))
    o.append("};")
    o.append("")
    o.append("} // namespace lilo")
    o.append("} // namespace royale")
    with open(HEADER, "w") as f:
        f.write("\n".join(o) + "\n")
    print("wrote %s: %d vertices, %d batches, %d triangles, %d frames" % (HEADER, len(verts), len(batches), len(tris),
                                                                          len(pose_values) // 7 // len(bones)))


if __name__ == "__main__":
    main()
