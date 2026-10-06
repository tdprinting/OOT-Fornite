#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace royale {
namespace ground {

// Every patch that lies on the ground for the weather and the seasons, in one system: puddles, snow piles and drifts, frost, fallen leaves and
// blossom. The same rules place them all:
//   * the ground is cut into 200-unit cells and each cell may hold one small patch of each kind (its "seed": where, how big, when it shows up);
//   * seeds that lie close together run into one bigger patch instead of overlapping, so a wet field has a few big puddles and a snowy one a few
//     long drifts, never a heap of small ones on top of each other. Of two seeds that touch, the one with the higher priority (a hash) takes the
//     other in. This is worked out from the seeds alone, so every player sees the same patches and nothing is sent over the network;
//   * a patch grows with the weather's cover (how wet, how snowy, how frosty): each seed in it has its own onset, so the first small puddles show
//     early in the rain and run together as it soaks in, and the snow piles up and joins into drifts the longer it snows.
// This header is the pure part (placement, merging, size and shape). The game layer (mod/Royale/RoyaleMod.cpp, "ground patches") says what the
// ground is like at each seed, with the cover of each kind, and draws the patches with the meshes of shared/ground_model.h (MeshKind::Ground).

enum class Kind : uint8_t { Puddle, Snow, Frost, Leaves, Petals, Count };
constexpr int kKinds = static_cast<int>(Kind::Count);

constexpr float kCell = 200.0f;          // one grid for every kind
constexpr float kMeshRadius = 100.0f;    // every Ground mesh is about this far from its middle to its edge
constexpr float kJoin = 1.1f;            // seeds join when their centres are closer than this times the sum of their radii (0 turns merging off)
constexpr float kMaxRadius = 300.0f;     // the biggest a merged patch gets
constexpr int kMaxMembers = 12;          // seeds in one patch (the rest, a rare crowd, are left out)

// Ground mesh variants (shared/ground_model.h): the first of each kind and how many shapes it has.
constexpr uint32_t kPuddleFirst = 0, kPuddleShapes = 4, kIceOffset = 4, kPileFirst = 8, kPileShapes = 4, kDriftFirst = 12, kDriftShapes = 4,
                   kFrostFirst = 16, kFrostShapes = 2, kLeafFirst = 18, kLeafShapes = 3, kPetalFirst = 21, kPetalShapes = 2;

inline uint32_t Hash(int a, int b, int salt) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u + static_cast<uint32_t>(salt) * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float Hash01(int a, int b, int salt) { return static_cast<float>(Hash(a, b, salt) & 0xFFFF) / 65535.0f; }

// What each kind is like: how often a cell holds one, how big a seed is (it reaches radius +- spread from its middle), and how fast it grows once
// its onset has passed (the cover it takes to go from nothing to full size).
struct KindInfo { float density, radius, spread, grow; bool opaque; };
inline const KindInfo& Info(Kind k) {
    static const KindInfo info[kKinds] = {
        { 0.55f, 74.0f, 30.0f, 0.35f, false },   // puddles
        { 0.80f, 66.0f, 22.0f, 0.30f, true },    // snow
        { 0.60f, 84.0f, 24.0f, 0.30f, false },   // frost
        { 0.55f, 56.0f, 18.0f, 0.25f, true },    // leaves
        { 0.50f, 80.0f, 22.0f, 0.25f, true },    // petals
    };
    return info[static_cast<int>(k)];
}

// One cell's seed. `ok` is whether the cell holds one at all (before the ground is looked at); the game layer then says whether the ground there
// takes it, and may change the seed (a wet spot's puddle is always there, so its onset becomes -1).
struct Seed {
    bool ok = false;
    float x = 0, z = 0, radius = 0;
    float onset = 0;       // the cover at which it starts to show (negative: always there)
    uint32_t shape = 0;
    uint32_t priority = 0;
    int cx = 0, cz = 0;
};

inline Seed SeedAt(Kind k, int cx, int cz) {
    const int salt = 100 + 10 * static_cast<int>(k);
    const KindInfo& in = Info(k);
    Seed s;
    s.cx = cx; s.cz = cz;
    s.ok = Hash01(cx, cz, salt) < in.density;
    s.x = (static_cast<float>(cx) + 0.2f + 0.6f * Hash01(cx, cz, salt + 1)) * kCell;
    s.z = (static_cast<float>(cz) + 0.2f + 0.6f * Hash01(cx, cz, salt + 2)) * kCell;
    s.radius = in.radius + in.spread * (Hash01(cx, cz, salt + 3) * 2.0f - 1.0f);
    s.onset = 0.05f + 0.85f * Hash01(cx, cz, salt + 4);
    s.shape = Hash(cx, cz, salt + 5);
    s.priority = Hash(cx, cz, salt + 6);
    return s;
}

inline bool Joins(const Seed& a, const Seed& b, float join) {
    const float dx = a.x - b.x, dz = a.z - b.z, reach = (a.radius + b.radius) * join;
    return dx * dx + dz * dz < reach * reach;
}
inline bool Outranks(const Seed& a, const Seed& b) { return a.priority != b.priority ? a.priority > b.priority : (a.cx != b.cx ? a.cx > b.cx : a.cz > b.cz); }

// A seed that is part of a patch, relative to the patch's leader.
struct Member { float dx, dz, radius, onset; };

struct Patch {
    Kind kind = Kind::Puddle;
    int cx = 0, cz = 0;        // the leader's cell
    float x = 0, z = 0;        // the leader's seed: where the ground is measured
    uint32_t shape = 0;
    float yaw0 = 0;            // a turn of its own, for a patch of one seed
    int n = 0;
    Member m[kMaxMembers];     // the leader is the first
    bool big = false;          // grown out of several seeds, or big on its own: a drift, not a pile
};

// How a patch looks right now, for the cover `cover` (0 none to 1 the most): where its middle is (relative to the leader), how far it reaches
// along and across its long axis (a, b), which way that axis points, and how tall to draw it. `shown` is false while no seed has started yet.
struct Shape { bool shown = false; float dx = 0, dz = 0, a = 0, b = 0, yaw = 0, rise = 1; };

inline float Smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
// How far into its growth a seed is: 0 not started, 1 full size.
inline float Growth(Kind k, float cover, float onset) { return Smooth((cover - onset) / Info(k).grow); }

inline Shape Evaluate(const Patch& p, float cover) {
    Shape out;
    float w = 0, mx = 0, mz = 0;
    float gr[kMaxMembers];
    for (int i = 0; i < p.n; i++) {
        gr[i] = Growth(p.kind, cover, p.m[i].onset);
        const float r = p.m[i].radius * (0.35f + 0.65f * gr[i]);
        const float wi = gr[i] > 0.02f ? r * r : 0.0f;
        w += wi; mx += wi * p.m[i].dx; mz += wi * p.m[i].dz;
        gr[i] = wi;   // from here on: this seed's area
    }
    if (w <= 1.0f) return out;
    mx /= w; mz /= w;
    // The patch is the ellipse that has the same middle and spread as its seeds (a disc of radius r has a spread of r / 2 about its middle).
    float cxx = 0, cxz = 0, czz = 0;
    for (int i = 0; i < p.n; i++) {
        if (gr[i] <= 0.0f) continue;
        const float ex = p.m[i].dx - mx, ez = p.m[i].dz - mz, r2 = gr[i] * 0.25f;
        cxx += gr[i] * (ex * ex + r2); cxz += gr[i] * ex * ez; czz += gr[i] * (ez * ez + r2);
    }
    cxx /= w; cxz /= w; czz /= w;
    const float mean = 0.5f * (cxx + czz), diff = std::sqrt(0.25f * (cxx - czz) * (cxx - czz) + cxz * cxz);
    const float l1 = mean + diff, l2 = std::max(mean - diff, 0.04f * l1);
    out.shown = true;
    out.dx = mx; out.dz = mz;
    out.a = std::min(kMaxRadius, 2.0f * std::sqrt(l1)); out.b = std::min(out.a, 2.0f * std::sqrt(l2));
    out.yaw = diff > 0.05f * mean ? -0.5f * std::atan2(2.0f * cxz, cxx - czz) : p.yaw0;
    if (diff <= 0.05f * mean) { out.a = out.b = std::min(kMaxRadius, 2.0f * std::sqrt(mean)); }
    out.rise = std::pow(std::max(out.a, out.b) / kMeshRadius, 0.6f);   // heaps do not grow as tall as they grow wide
    return out;
}

// The mesh variant for a patch (the winter's ice on puddles is for the caller to say).
inline uint32_t MeshVariant(const Patch& p, bool frozen) {
    switch (p.kind) {
        case Kind::Puddle: return kPuddleFirst + p.shape % kPuddleShapes + (frozen ? kIceOffset : 0u);
        case Kind::Snow: return p.big ? kDriftFirst + p.shape % kDriftShapes : kPileFirst + p.shape % kPileShapes;
        case Kind::Frost: return kFrostFirst + p.shape % kFrostShapes;
        case Kind::Leaves: return kLeafFirst + p.shape % kLeafShapes;
        default: return kPetalFirst + p.shape % kPetalShapes;
    }
}

// The seeds and the patches built from them. `Probe` is the game's say on the ground:
//     int probe(Kind, int cx, int cz, Seed& seed)     1 the ground takes it (the seed may be changed), 0 it does not, -1 not measured yet.
// Lead() gives 1 and the patch when the cell leads one, 0 when it does not, and -1 when something needed was not measured yet (ask again later).
// Results are remembered; Clear() forgets them when the world changes.
class Field {
public:
    explicit Field(float join = kJoin) : join_(join) {}
    void Clear() { nodes_.clear(); leads_.clear(); }
    void SetJoin(float join) { if (join != join_) { join_ = join; Clear(); } }
    size_t Size() const { return nodes_.size() + leads_.size(); }

    template <class Probe>
    int Lead(Kind k, int cx, int cz, Probe& probe, Patch* out) {
        const uint64_t key = Key(k, cx, cz);
        auto it = leads_.find(key);
        if (it != leads_.end()) { if (it->second.n > 0 && out) *out = it->second; return it->second.n > 0 ? 1 : 0; }
        Node* self = At(k, cx, cz, probe);
        if (self == nullptr) return -1;
        Patch p;
        if (!self->s.ok) { leads_[key] = p; return 0; }
        if (Depth(k, *self, probe) < 0) return -1;
        if (self->depth != 0) { leads_[key] = p; return 0; }
        p.kind = k; p.cx = cx; p.cz = cz; p.x = self->s.x; p.z = self->s.z; p.shape = self->s.shape; p.yaw0 = Hash01(cx, cz, 190) * 6.2831853f;
        p.m[p.n++] = {0.0f, 0.0f, self->s.radius, self->s.onset};
        // Everything in this cell's neighbourhood whose chain of higher-priority neighbours ends here (at most two steps) is part of it.
        for (int dz = -kWindow; dz <= kWindow && p.n < kMaxMembers; dz++)
            for (int dx = -kWindow; dx <= kWindow && p.n < kMaxMembers; dx++) {
                if (dx == 0 && dz == 0) continue;
                Node* m = At(k, cx + dx, cz + dz, probe);
                if (m == nullptr) return -1;
                if (!m->s.ok) continue;
                if (Depth(k, *m, probe) < 0) return -1;
                if (m->depth == 0) continue;
                int rx = m->s.cx, rz = m->s.cz;
                Node* r = m;
                bool unknown = false;
                for (int hop = 0; hop < 3 && r->depth > 0; hop++) {
                    rx = r->px; rz = r->pz;
                    r = At(k, rx, rz, probe);
                    if (r == nullptr) { unknown = true; break; }
                }
                if (unknown) return -1;
                if (rx == cx && rz == cz && r->depth == 0) p.m[p.n++] = {m->s.x - p.x, m->s.z - p.z, m->s.radius, m->s.onset};
            }
        float area = 0.0f;
        for (int i = 0; i < p.n; i++) area += p.m[i].radius * p.m[i].radius;
        p.big = p.n >= 3 || std::sqrt(area) > 1.4f * Info(k).radius;
        leads_[key] = p;
        if (out) *out = p;
        return 1;
    }

private:
    static constexpr int kWindow = 4;   // a seed's chain can reach two steps of two cells
    struct Node { Seed s; int8_t depth = -2; int px = 0, pz = 0; };   // depth: -2 not worked out, 0 leads, 1-2 joins a leader
    float join_;
    std::unordered_map<uint64_t, Node> nodes_;
    std::unordered_map<uint64_t, Patch> leads_;

    static uint64_t Key(Kind k, int cx, int cz) {
        return (static_cast<uint64_t>(k) << 58) | (static_cast<uint64_t>(cx + 65536) << 29) | static_cast<uint64_t>(cz + 65536);
    }
    template <class Probe>
    Node* At(Kind k, int cx, int cz, Probe& probe) {
        const uint64_t key = Key(k, cx, cz);
        auto it = nodes_.find(key);
        if (it != nodes_.end()) return &it->second;
        Node n;
        n.s = SeedAt(k, cx, cz);
        if (n.s.ok) {
            const int r = probe(k, cx, cz, n.s);
            if (r < 0) return nullptr;
            n.s.ok = r > 0;
        }
        return &nodes_.emplace(key, n).first->second;
    }
    // A seed leads unless a neighbour it touches outranks it; then it joins that neighbour's patch. The chain is cut after two steps (the third
    // seed in a row leads a patch of its own), which keeps every patch within a few cells. Returns -1 when a neighbour is not measured yet.
    template <class Probe>
    int Depth(Kind k, Node& n, Probe& probe) {
        if (n.depth > -2) return n.depth;
        Node* best = nullptr;
        for (int dz = -2; dz <= 2; dz++)
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dz == 0) continue;
                Node* o = At(k, n.s.cx + dx, n.s.cz + dz, probe);
                if (o == nullptr) return -1;
                if (!o->s.ok || join_ <= 0.0f || !Joins(n.s, o->s, join_) || !Outranks(o->s, n.s)) continue;
                if (best == nullptr || Outranks(o->s, best->s)) best = o;
            }
        if (best == nullptr) { n.depth = 0; return 0; }
        const int d = Depth(k, *best, probe);
        if (d < 0) return -1;
        n.px = best->s.cx; n.pz = best->s.cz;
        n.depth = static_cast<int8_t>((d + 1) % 3);
        return n.depth;
    }
};

} // namespace ground
} // namespace royale
