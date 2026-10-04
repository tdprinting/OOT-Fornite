#pragma once
#include "../shared/loot.h"
#include "../shared/storm.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace royale {

// A coarse walkability grid over the map circle, used by bots to path around walls, water and cliffs. The host's game builds it
// by probing the floor (the same probe that keeps loot on solid ground); with no grid, bots fall back to straight lines.
// Cells are kCell units wide. Pathfinding is A* over 8 neighbours with a hard expansion cap so one bad query can't stall a tick.
class NavGrid {
  public:
    static constexpr float kCell = 60.0f;

    NavGrid(Circle map, const PlacementFn& walkable) : map(map) {
        originX = map.center.x - map.radius;
        originZ = map.center.z - map.radius;
        w = h = static_cast<int>(std::ceil(map.radius * 2.0f / kCell)) + 1;
        cells.assign(static_cast<size_t>(w) * h, 0);
        for (int cz = 0; cz < h; cz++) {
            for (int cx = 0; cx < w; cx++) {
                const Vec2 c = CellCentre(cx, cz);
                if (Distance(c, map.center) > map.radius) continue;
                cells[Index(cx, cz)] = !walkable || walkable(c) ? 1 : 0;
            }
        }
    }

    // Mark a circular area (a rock, a pillar) as impassable.
    void Block(Vec2 centre, float radius) {
        const int reach = static_cast<int>(std::ceil(radius / kCell)) + 1;
        int cx, cz;
        ToCellClamped(centre, cx, cz);
        for (int z = cz - reach; z <= cz + reach; z++) {
            for (int x = cx - reach; x <= cx + reach; x++) {
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                if (Distance(CellCentre(x, z), centre) <= radius) cells[Index(x, z)] = 0;
            }
        }
    }

    bool Walkable(Vec2 p) const {
        int cx, cz;
        return ToCell(p, cx, cz) && cells[Index(cx, cz)];
    }

    int WalkableCount() const {
        int n = 0;
        for (uint8_t c : cells) n += c;
        return n;
    }

    // True if the straight segment a-b only crosses walkable cells.
    bool LineClear(Vec2 a, Vec2 b) const {
        const float len = Distance(a, b);
        const int steps = static_cast<int>(len / (kCell * 0.5f)) + 1;
        for (int i = 0; i <= steps; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            if (!Walkable({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t})) return false;
        }
        return true;
    }

    // The nearest walkable point to p (p itself if it's fine). Searches outward in rings; returns false if there is none nearby.
    bool Snap(Vec2 p, Vec2* out) const {
        if (Walkable(p)) { *out = p; return true; }
        int cx, cz;
        ToCellClamped(p, cx, cz);
        for (int r = 1; r <= 12; r++) {
            float best = 1e18f;
            bool found = false;
            for (int dz = -r; dz <= r; dz++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= w || z >= h || !cells[Index(x, z)]) continue;
                    const Vec2 c = CellCentre(x, z);
                    const float d = Distance(c, p);
                    if (d < best) { best = d; *out = c; found = true; }
                }
            }
            if (found) return true;
        }
        return false;
    }

    // Finds a path from a to b as a list of waypoints (not including a, ending at b). Returns false if there is no route.
    // The result is smoothed: waypoints that can be skipped with a clear line are dropped.
    bool FindPath(Vec2 from, Vec2 to, std::vector<Vec2>& path) const {
        path.clear();
        Vec2 a, b;
        if (!Snap(from, &a) || !Snap(to, &b)) return false;
        if (LineClear(a, b)) { path.push_back(b); return true; }

        int sx, sz, gx, gz;
        ToCellClamped(a, sx, sz);
        ToCellClamped(b, gx, gz);
        const int start = Index(sx, sz), goal = Index(gx, gz);
        if (start == goal) { path.push_back(b); return true; }

        struct Node { float f; int idx; bool operator<(const Node& o) const { return f > o.f; } };
        std::vector<float> g(cells.size(), 1e18f);
        std::vector<int> parent(cells.size(), -1);
        std::priority_queue<Node> open;
        g[start] = 0;
        open.push({Heuristic(sx, sz, gx, gz), start});
        int expansions = 0;
        bool reached = false;
        static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dzs[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!open.empty() && expansions < kMaxExpansions) {
            const Node n = open.top();
            open.pop();
            if (n.idx == goal) { reached = true; break; }
            const int cx = n.idx % w, cz = n.idx / w;
            if (n.f - Heuristic(cx, cz, gx, gz) > g[n.idx] + 1e-3f) continue; // stale entry
            expansions++;
            for (int k = 0; k < 8; k++) {
                const int x = cx + dxs[k], z = cz + dzs[k];
                if (x < 0 || z < 0 || x >= w || z >= h || !cells[Index(x, z)]) continue;
                if (k >= 4 && (!cells[Index(cx + dxs[k], cz)] || !cells[Index(cx, cz + dzs[k])])) continue; // no corner cutting
                const float step = k >= 4 ? 1.41421356f : 1.0f;
                const float ng = g[n.idx] + step;
                if (ng < g[Index(x, z)]) {
                    g[Index(x, z)] = ng;
                    parent[Index(x, z)] = n.idx;
                    open.push({ng + Heuristic(x, z, gx, gz), Index(x, z)});
                }
            }
        }
        if (!reached) return false;

        std::vector<Vec2> raw;
        for (int i = goal; i != start && i >= 0; i = parent[i]) raw.push_back(CellCentre(i % w, i / w));
        std::reverse(raw.begin(), raw.end());
        if (raw.empty()) { path.push_back(b); return true; }
        raw.back() = b;

        // String-pulling: from the current point, jump to the furthest waypoint that is still in a clear line.
        Vec2 at = a;
        size_t i = 0;
        while (i < raw.size()) {
            size_t far = i;
            for (size_t j = raw.size() - 1; j > i; j--) {
                if (LineClear(at, raw[j])) { far = j; break; }
            }
            path.push_back(raw[far]);
            at = raw[far];
            i = far + 1;
        }
        return true;
    }

  private:
    static constexpr int kMaxExpansions = 6000;

    int Index(int cx, int cz) const { return cz * w + cx; }
    Vec2 CellCentre(int cx, int cz) const { return {originX + (cx + 0.5f) * kCell, originZ + (cz + 0.5f) * kCell}; }
    bool ToCell(Vec2 p, int& cx, int& cz) const {
        cx = static_cast<int>(std::floor((p.x - originX) / kCell));
        cz = static_cast<int>(std::floor((p.z - originZ) / kCell));
        return cx >= 0 && cz >= 0 && cx < w && cz < h;
    }
    void ToCellClamped(Vec2 p, int& cx, int& cz) const {
        cx = (std::max)(0, (std::min)(w - 1, static_cast<int>(std::floor((p.x - originX) / kCell))));
        cz = (std::max)(0, (std::min)(h - 1, static_cast<int>(std::floor((p.z - originZ) / kCell))));
    }
    static float Heuristic(int ax, int az, int bx, int bz) {
        const float dx = static_cast<float>(std::abs(ax - bx)), dz = static_cast<float>(std::abs(az - bz));
        return (dx + dz) + (1.41421356f - 2.0f) * (std::min)(dx, dz); // octile distance
    }

    Circle map;
    float originX = 0, originZ = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> cells;
};

} // namespace royale
