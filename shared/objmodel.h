#pragma once
#include "meshes.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace royale {

// A model made elsewhere (Blender, Maya, any modelling tool) in the common Wavefront OBJ format, read into the same flat triangle lists the built-in
// models use. What is understood:
//   v x y z [r g b]   a vertex, with an optional colour (0..1) after it
//   o name / g name   starts a named part (the parts are what the game animates: wings, jaw, tail, head...)
//   usemtl name       the material for the faces that follow; its diffuse colour (Kd) comes from the .mtl file
//   f a b c ...       a face (a/b/c, a//c and negative numbers are fine); polygons are cut into triangles
// Everything else (textures, normals, smoothing groups) is ignored: the game draws these with vertex colours and its own lighting.
struct ObjPart {
    std::string name;
    MeshData mesh;
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    float centre[3] = {0, 0, 0};
};

struct ObjModel {
    std::vector<ObjPart> parts;
    bool ok = false;
    std::string error;
    size_t triangles = 0;
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
};

constexpr size_t kObjMaxTriangles = 40000;

namespace obj_detail {
inline std::string Lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
struct Col { float r = 0.62f, g = 0.62f, b = 0.64f; };
inline std::map<std::string, Col> ParseMtl(const std::string& text) {
    std::map<std::string, Col> out;
    std::istringstream in(text);
    std::string line, cur;
    while (std::getline(in, line)) {
        line = Trim(line);
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "newmtl") { std::getline(ls, cur); cur = Trim(cur); out[cur] = Col{}; }
        else if (key == "Kd" && !cur.empty()) { Col c; if (ls >> c.r >> c.g >> c.b) out[cur] = c; }
    }
    return out;
}
inline int Index(const std::string& tok, int count) {   // "7", "7/2", "7//3", "-1": the vertex number, 0-based, or -1 when it makes no sense
    const int v = std::atoi(tok.c_str());
    if (v > 0 && v <= count) return v - 1;
    if (v < 0 && -v <= count) return count + v;
    return -1;
}
} // namespace obj_detail

// Reads an OBJ (and the contents of its .mtl, which may be empty). Never throws: a bad file comes back with ok = false and an error to show.
inline ObjModel ParseObj(const std::string& objText, const std::string& mtlText) {
    using namespace obj_detail;
    ObjModel model;
    const auto mats = ParseMtl(mtlText);
    struct V { float x, y, z; bool hasCol; Col col; };
    std::vector<V> verts;
    struct Tri { int a, b, c; Col col; };
    struct Raw { std::string name; std::vector<Tri> tris; };
    std::vector<Raw> raws(1);
    raws[0].name = "body";
    Col matCol;
    std::istringstream in(objText);
    std::string line;
    size_t tris = 0;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "v") {
            V v = {0, 0, 0, false, {}};
            if (!(ls >> v.x >> v.y >> v.z)) { model.error = "a vertex line is broken: " + line; return model; }
            if (ls >> v.col.r >> v.col.g >> v.col.b) v.hasCol = true;
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) { model.error = "a vertex is not a number"; return model; }
            verts.push_back(v);
        } else if (key == "o" || key == "g") {
            std::string name;
            std::getline(ls, name);
            name = Trim(name);
            if (!raws.back().tris.empty()) raws.push_back({});
            raws.back().name = name.empty() ? "part" : name;
        } else if (key == "usemtl") {
            std::string name;
            std::getline(ls, name);
            name = Trim(name);
            auto it = mats.find(name);
            matCol = it != mats.end() ? it->second : Col{};
        } else if (key == "f") {
            std::vector<int> idx;
            std::string tok;
            while (ls >> tok) {
                const int i = Index(tok, static_cast<int>(verts.size()));
                if (i < 0) { model.error = "a face points at a vertex that does not exist: " + line; return model; }
                idx.push_back(i);
            }
            if (idx.size() < 3) continue;
            for (size_t k = 1; k + 1 < idx.size(); k++) {
                raws.back().tris.push_back({idx[0], idx[k], idx[k + 1], matCol});
                if (++tris > kObjMaxTriangles) { model.error = "too many triangles (the limit is " + std::to_string(kObjMaxTriangles) + "): simplify the model"; return model; }
            }
        }
    }
    if (tris == 0) { model.error = "the file has no faces"; return model; }
    // Colours: a vertex colour if the file gives one, else the material's, with the light baked in (the game draws these unlit). Smooth shading from
    // normals averaged over the faces that meet at each vertex.
    std::vector<MeshData> unused;
    for (auto& raw : raws) {
        if (raw.tris.empty()) continue;
        ObjPart part;
        part.name = raw.name;
        std::map<int, mesh_detail::V3> normal;
        for (const Tri& t : raw.tris) {
            const mesh_detail::V3 a = {verts[t.a].x, verts[t.a].y, verts[t.a].z}, b = {verts[t.b].x, verts[t.b].y, verts[t.b].z}, c = {verts[t.c].x, verts[t.c].y, verts[t.c].z};
            const mesh_detail::V3 n = mesh_detail::Cross(mesh_detail::Sub(b, a), mesh_detail::Sub(c, a));
            for (int i : {t.a, t.b, t.c}) { auto& s = normal[i]; s = {s.x + n.x, s.y + n.y, s.z + n.z}; }
        }
        for (const Tri& t : raw.tris) {
            for (int i : {t.a, t.b, t.c}) {
                const V& v = verts[i];
                const Col base = v.hasCol ? v.col : t.col;
                const mesh_detail::Rgb lit = mesh_detail::Light({base.r * 255.0f, base.g * 255.0f, base.b * 255.0f}, normal[i]);
                mesh_detail::Put(part.mesh, {v.x, v.y, v.z}, lit);
            }
        }
        part.mesh.Bounds(part.mn, part.mx);
        for (int k = 0; k < 3; k++) part.centre[k] = (part.mn[k] + part.mx[k]) * 0.5f;
        model.parts.push_back(std::move(part));
    }
    model.triangles = tris;
    model.mn[0] = model.mn[1] = model.mn[2] = 1e30f;
    model.mx[0] = model.mx[1] = model.mx[2] = -1e30f;
    for (const auto& p : model.parts)
        for (int k = 0; k < 3; k++) { model.mn[k] = (std::min)(model.mn[k], p.mn[k]); model.mx[k] = (std::max)(model.mx[k], p.mx[k]); }
    model.ok = true;
    return model;
}

// Moves and scales a model to a standard size and place: centred on x and z, standing on y = 0, rotated by `yawDegrees` about the vertical, its widest
// horizontal extent made `span` long (times `extra`). Every part is changed the same way, so they still fit together.
inline void FitObjModel(ObjModel& m, float span, float extra = 1.0f, float yawDegrees = 0.0f, float lift = 0.0f) {
    if (!m.ok) return;
    const float cx = (m.mn[0] + m.mx[0]) * 0.5f, cz = (m.mn[2] + m.mx[2]) * 0.5f;
    const float wide = (std::max)(m.mx[0] - m.mn[0], m.mx[2] - m.mn[2]);
    const float k = wide > 1e-6f ? span / wide * extra : 1.0f;
    const float a = yawDegrees * 3.14159265f / 180.0f, ca = std::cos(a), sa = std::sin(a);
    for (auto& p : m.parts) {
        for (auto& v : p.mesh.v) {
            const float x = (v.x - cx) * k, z = (v.z - cz) * k;
            v.x = x * ca + z * sa;
            v.z = -x * sa + z * ca;
            v.y = (v.y - m.mn[1]) * k + lift;
        }
        p.mesh.Bounds(p.mn, p.mx);
        for (int i = 0; i < 3; i++) p.centre[i] = (p.mn[i] + p.mx[i]) * 0.5f;
    }
    m.mn[0] = m.mn[1] = m.mn[2] = 1e30f;
    m.mx[0] = m.mx[1] = m.mx[2] = -1e30f;
    for (const auto& p : m.parts)
        for (int i = 0; i < 3; i++) { m.mn[i] = (std::min)(m.mn[i], p.mn[i]); m.mx[i] = (std::max)(m.mx[i], p.mx[i]); }
}

// What a part is, from its name (case does not matter): the game animates wings, jaws, tails and heads by name.
enum class ObjRole : uint8_t { Body, Wing, Jaw, Tail, Head };
inline ObjRole RoleOf(const std::string& name) {
    const std::string n = obj_detail::Lower(name);
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (has("wing")) return ObjRole::Wing;
    if (has("jaw") || has("mouth") || has("chin")) return ObjRole::Jaw;
    if (has("tail")) return ObjRole::Tail;
    if (has("head") || has("skull")) return ObjRole::Head;
    return ObjRole::Body;
}

} // namespace royale
