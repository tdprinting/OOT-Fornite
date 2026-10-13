#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace royale {
// Is a point inside any of a fixed list of boxes (a map's walls and rocks, kObstacles)? The maps have two to three thousand of them and the
// question is asked for every cell of the bots' grid when a map is set up, so the boxes are sorted once into a coarse grid of buckets and a
// question looks only at the few boxes in its own bucket. The answer is the same as testing every box: `p` is inside box b grown by `margin`
// when x0-margin < p.x < x1+margin and z0-margin < p.z < z1+margin. Margins above kPad fall back to testing every box.
template <class Box> class BoxIndex {
  public:
    static constexpr float kBucket = 400.0f, kPad = 64.0f;
    template <size_t N> explicit BoxIndex(const Box (&boxes)[N]) : list(boxes), count(N) {
        if (N == 0) return;
        minX = minZ = 1e30f;
        float maxX = -1e30f, maxZ = -1e30f;
        for (const Box& b : boxes) { minX = (std::min)(minX, b.x0); maxX = (std::max)(maxX, b.x1); minZ = (std::min)(minZ, b.z0); maxZ = (std::max)(maxZ, b.z1); }
        minX -= kPad; minZ -= kPad; maxX += kPad; maxZ += kPad;
        w = static_cast<int>((maxX - minX) / kBucket) + 1;
        h = static_cast<int>((maxZ - minZ) / kBucket) + 1;
        buckets.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (size_t i = 0; i < N; i++) {
            const Box& b = boxes[i];
            const int x0 = Col(b.x0 - kPad), x1 = Col(b.x1 + kPad), z0 = Row(b.z0 - kPad), z1 = Row(b.z1 + kPad);
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++) buckets[static_cast<size_t>(z) * static_cast<size_t>(w) + static_cast<size_t>(x)].push_back(static_cast<uint32_t>(i));
        }
    }
    bool Any(float px, float pz, float margin) const {
        if (margin > kPad || margin < 0.0f) {
            for (size_t i = 0; i < count; i++) if (Inside(list[i], px, pz, margin)) return true;
            return false;
        }
        if (buckets.empty()) return false;
        const float fx = (px - minX) / kBucket, fz = (pz - minZ) / kBucket;
        if (!(fx >= 0.0f && fz >= 0.0f && fx < static_cast<float>(w) && fz < static_cast<float>(h))) return false;   // beyond every box, padding and all
        for (uint32_t i : buckets[static_cast<size_t>(fz) * static_cast<size_t>(w) + static_cast<size_t>(fx)])
            if (Inside(list[i], px, pz, margin)) return true;
        return false;
    }

  private:
    static bool Inside(const Box& b, float px, float pz, float margin) { return px > b.x0 - margin && px < b.x1 + margin && pz > b.z0 - margin && pz < b.z1 + margin; }
    int Col(float x) const { return (std::max)(0, (std::min)(w - 1, static_cast<int>((x - minX) / kBucket))); }
    int Row(float z) const { return (std::max)(0, (std::min)(h - 1, static_cast<int>((z - minZ) / kBucket))); }
    const Box* list;
    size_t count;
    float minX = 0, minZ = 0;
    int w = 0, h = 0;
    std::vector<std::vector<uint32_t>> buckets;
};
} // namespace royale
