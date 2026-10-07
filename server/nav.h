#pragma once
#include "../shared/loot.h"
#include "../shared/props.h"
#include "../shared/storm.h"
#include "../shared/terrain.h"
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
    // How bots get about on uneven ground, in height between neighbouring cells. Up to kStepUp is walked (any slope you can stand on); up to
    // kClimbUp is a jump and a clamber, as Link does onto a ledge; more is a wall. Dropping down is fine up to kDropDown; more is a cliff.
    static constexpr float kStepUp = 46.0f;
    static constexpr float kClimbUp = 70.0f;
    static constexpr float kDropDown = 320.0f;

    // `height` gives the floor height of every cell, so bots can tell ledges and cliffs from flat ground. Without it the map is flat.
    NavGrid(Circle map, const PlacementFn& walkable, const HeightFn& height = nullptr) : map(map) {
        originX = map.center.x - map.radius;
        originZ = map.center.z - map.radius;
        w = h = static_cast<int>(std::ceil(map.radius * 2.0f / kCell)) + 1;
        cells.assign(static_cast<size_t>(w) * h, 0);
        floor.assign(cells.size(), 0.0f);
        lift.assign(cells.size(), 0.0f);
        cover.assign(cells.size(), 0.0f);
        for (int cz = 0; cz < h; cz++) {
            for (int cx = 0; cx < w; cx++) {
                const Vec2 c = CellCentre(cx, cz);
                if (Distance(c, map.center) > map.radius) continue;
                cells[Index(cx, cz)] = !walkable || walkable(c) ? kOpen : kBlocked;
                float y = 0;
                if (height && height(c, &y)) { floor[Index(cx, cz)] = y; heights = true; }
            }
        }
    }

    // Mark a circular area (a rock, a pillar) as impassable. `tall` is how high it stands: it hides whoever is behind it.
    void Block(Vec2 centre, float radius, float tall = 0.0f) {
        ForCells(centre, radius, false, [&](size_t i) { cells[i] = kBlocked; cover[i] = (std::max)(cover[i], tall); });
    }

    // Scenery that can be stood on (a climbing block, a low boulder): `top` above the floor over its footprint (a square of half-width `half`,
    // or a circle of that radius). Others (the bosses) still go round it, out to `clear`; bots may walk up to its sides and climb it where the
    // step is small enough.
    void AddStandable(Vec2 centre, float half, bool square, float top, float clear) {
        ForCells(centre, clear, false, [&](size_t i) { if (cells[i] == kOpen) cells[i] = kNearScenery; });
        ForCells(centre, half, square, [&](size_t i) {
            if (cells[i] == kBlocked) return;
            cells[i] = kLifted;
            lift[i] = (std::max)(lift[i], top);
            cover[i] = (std::max)(cover[i], top);
        });
    }

    // Plain ground with nothing on it (what the bosses keep to).
    bool Walkable(Vec2 p) const {
        int cx, cz;
        return ToCell(p, cx, cz) && cells[Index(cx, cz)] == kOpen;
    }
    // Anywhere a bot can stand: open ground, the ground right beside scenery, and the tops of blocks and low boulders.
    bool Standable(Vec2 p) const {
        int cx, cz;
        return ToCell(p, cx, cz) && cells[Index(cx, cz)] != kBlocked;
    }
    bool HasHeights() const { return heights; }
    // The scene's floor under p (0 without heights), what is stood on above it, and how high whatever is there stands (for hiding behind).
    float FloorAt(Vec2 p) const { int cx, cz; ToCellClamped(p, cx, cz); return floor[Index(cx, cz)]; }
    float LiftAt(Vec2 p) const { int cx, cz; ToCellClamped(p, cx, cz); return lift[Index(cx, cz)]; }
    float TopAt(Vec2 p) const { int cx, cz; ToCellClamped(p, cx, cz); return floor[Index(cx, cz)] + cover[Index(cx, cz)]; }
    // The ground's height at p blended smoothly between the cells round it (with what is stood on), so a cart rolls over it instead of
    // bumping down a staircase of cells.
    float SmoothHeight(Vec2 p) const {
        const float fx = (p.x - originX) / kCell - 0.5f, fz = (p.z - originZ) / kCell - 0.5f;
        const int x0 = static_cast<int>(std::floor(fx)), z0 = static_cast<int>(std::floor(fz));
        const float kx = fx - static_cast<float>(x0), kz = fz - static_cast<float>(z0);
        auto at = [&](int cx, int cz) {
            cx = (std::max)(0, (std::min)(w - 1, cx));
            cz = (std::max)(0, (std::min)(h - 1, cz));
            return floor[Index(cx, cz)] + lift[Index(cx, cz)];
        };
        const float a = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * kx;
        const float b = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * kx;
        return a + (b - a) * kz;
    }
    float StandHeight(Vec2 p) const { int cx, cz; ToCellClamped(p, cx, cz); return floor[Index(cx, cz)] + lift[Index(cx, cz)]; }

    // Which stretch of ground a cell belongs to: cells joined by steps no taller than a clamber (kClimbUp) in either direction are one stretch. An
    // upper floor, a roof or an island the bots have no way onto is a stretch of its own. Call once the scenery is added, before bots use Connected.
    void BuildRegions() {
        region.assign(cells.size(), -1);
        regionSize.clear();
        std::vector<int> stack;
        for (size_t s = 0; s < cells.size(); s++) {
            if (cells[s] == kBlocked || region[s] >= 0) continue;
            const int id = static_cast<int>(regionSize.size());
            int count = 0;
            region[s] = id;
            stack.push_back(static_cast<int>(s));
            while (!stack.empty()) {
                const int a = stack.back();
                stack.pop_back();
                count++;
                const int ax = a % w, az = a / w;
                for (int dz = -1; dz <= 1; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int x = ax + dx, z = az + dz;
                        if ((!dx && !dz) || x < 0 || z < 0 || x >= w || z >= h) continue;
                        const int b = Index(x, z);
                        if (cells[b] == kBlocked || region[b] >= 0) continue;
                        if (dx && dz && (cells[Index(ax + dx, az)] == kBlocked || cells[Index(ax, az + dz)] == kBlocked)) continue;   // no squeezing between corners
                        if (std::fabs(Height(b) - Height(a)) > kClimbUp) continue;
                        region[b] = id;
                        stack.push_back(b);
                    }
                }
            }
            regionSize.push_back(count);
        }
    }
    // A chest on an upper floor or a roof stands over ground that is open: the cell below it is walkable, but a bot there could not reach it.
    void MarkUpper(Vec2 p) {
        int cx, cz;
        if (!ToCell(p, cx, cz)) return;
        if (upper.empty()) upper.assign(cells.size(), 0);
        upper[Index(cx, cz)] = 1;
    }
    // Can a bot standing at a get to b? True when either is not on known ground, or when a is on a scrap of ground too small to say (a bot dropped
    // onto a stray ledge should not stop wanting everything).
    bool Connected(Vec2 a, Vec2 b) const {
        if (region.empty()) return true;
        int ax, az, bx, bz;
        if (!ToCell(a, ax, az) || !ToCell(b, bx, bz)) return true;
        if (!upper.empty() && upper[Index(bx, bz)]) return false;
        const int ra = region[Index(ax, az)], rb = region[Index(bx, bz)];
        if (ra < 0 || rb < 0 || ra == rb) return true;
        return regionSize[static_cast<size_t>(ra)] < 150;
    }

    // A step from a to b for a bot: b can be stood on and the height between them can be walked, climbed or dropped.
    static bool StepOk(float fromY, float toY) { return toY - fromY <= kClimbUp && fromY - toY <= kDropDown; }
    bool CanStep(Vec2 a, Vec2 b) const {
        int ax, az, bx, bz;
        if (!ToCell(b, bx, bz) || cells[Index(bx, bz)] == kBlocked) return false;
        if (!ToCell(a, ax, az) || (ax == bx && az == bz)) return true;
        return StepOk(StandHeight(a), StandHeight(b));
    }

    int WalkableCount() const {
        int n = 0;
        for (uint8_t c : cells) n += c != kBlocked;
        return n;
    }

    // True if the straight segment a-b only crosses walkable cells. A `climber` (a bot) may also cross scenery it can stand on and climb
    // and drop between heights as CanStep allows, but only drops and walks: a jump up ends a straight line, so the path puts a waypoint there.
    bool LineClear(Vec2 a, Vec2 b, bool climber = false) const {
        const float len = Distance(a, b);
        const int steps = static_cast<int>(len / (kCell * 0.5f)) + 1;
        Vec2 prev = a;
        for (int i = 0; i <= steps; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const Vec2 at = {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
            if (!climber) { if (!Walkable(at)) return false; continue; }
            if (!Standable(at)) return false;
            if (i > 0) {
                const float up = StandHeight(at) - StandHeight(prev);
                if (up > kStepUp || -up > kDropDown) return false;
            }
            prev = at;
        }
        return true;
    }

    // The nearest walkable point to p (p itself if it's fine). Searches outward in rings; returns false if there is none nearby.
    bool Snap(Vec2 p, Vec2* out, bool climber = false) const {
        if (climber ? Standable(p) : Walkable(p)) { *out = p; return true; }
        int cx, cz;
        ToCellClamped(p, cx, cz);
        for (int r = 1; r <= 12; r++) {
            float best = 1e18f;
            bool found = false;
            for (int dz = -r; dz <= r; dz++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= w || z >= h || !Usable(Index(x, z), climber)) continue;
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
    // The result is smoothed: waypoints that can be skipped with a clear line are dropped. A `climber` (a bot) goes over the scenery it can
    // stand on, jumps up ledges up to kClimbUp, drops off ones up to kDropDown and goes round cliffs; it would rather walk than climb.
    bool FindPath(Vec2 from, Vec2 to, std::vector<Vec2>& path, bool climber = false) const {
        path.clear();
        Vec2 a, b;
        if (!Snap(from, &a, climber) || !Snap(to, &b, climber)) return false;
        if (LineClear(a, b, climber)) { path.push_back(b); return true; }

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
        while (!open.empty() && expansions < MaxExpansions()) {
            const Node n = open.top();
            open.pop();
            if (n.idx == goal) { reached = true; break; }
            const int cx = n.idx % w, cz = n.idx / w;
            if (n.f - Heuristic(cx, cz, gx, gz) > g[n.idx] + 1e-3f) continue; // stale entry
            expansions++;
            for (int k = 0; k < 8; k++) {
                const int x = cx + dxs[k], z = cz + dzs[k];
                if (x < 0 || z < 0 || x >= w || z >= h || !Usable(Index(x, z), climber)) continue;
                if (k >= 4 && (!Usable(Index(cx + dxs[k], cz), climber) || !Usable(Index(cx, cz + dzs[k]), climber))) continue; // no corner cutting
                float step = k >= 4 ? 1.41421356f : 1.0f;
                if (climber) {
                    const float up = Height(Index(x, z)) - Height(n.idx);
                    if (!StepOk(Height(n.idx), Height(Index(x, z)))) continue;                     // a wall or a cliff
                    if (k >= 4 && (std::fabs(Height(Index(cx + dxs[k], cz)) - Height(n.idx)) > kStepUp ||
                                   std::fabs(Height(Index(cx, cz + dzs[k])) - Height(n.idx)) > kStepUp)) continue;   // no diagonal hops past a ledge's corner
                    if (up > kStepUp) step += 1.5f;                                                   // a jump and a clamber
                    else if (-up > kStepUp) step += 0.4f;                                             // a drop
                }
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
                if (LineClear(at, raw[j], climber)) { far = j; break; }
            }
            path.push_back(raw[far]);
            at = raw[far];
            i = far + 1;
        }
        return true;
    }

  private:
    // Enough to cross the whole grid: a big map (the Kingdom is 220 cells across) has rivers to go round.
    int MaxExpansions() const { return (std::max)(6000, static_cast<int>(cells.size() / 2)); }
    // What a cell holds: nothing to stand on, open ground, ground right beside scenery, the top of scenery.
    static constexpr uint8_t kBlocked = 0, kOpen = 1, kNearScenery = 2, kLifted = 3;

    int Index(int cx, int cz) const { return cz * w + cx; }
    bool Usable(int i, bool climber) const { return climber ? cells[i] != kBlocked : cells[i] == kOpen; }
    float Height(int i) const { return floor[i] + lift[i]; }
    // Every cell whose centre is within `radius` of `centre` (or inside the square of half-width `radius` around it).
    template <typename F> void ForCells(Vec2 centre, float radius, bool square, F f) {
        const int reach = static_cast<int>(std::ceil(radius / kCell)) + 1;
        int cx, cz;
        ToCellClamped(centre, cx, cz);
        for (int z = cz - reach; z <= cz + reach; z++) {
            for (int x = cx - reach; x <= cx + reach; x++) {
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                const Vec2 c = CellCentre(x, z);
                const bool in = square ? std::fabs(c.x - centre.x) <= radius && std::fabs(c.z - centre.z) <= radius : Distance(c, centre) <= radius;
                if (in) f(static_cast<size_t>(Index(x, z)));
            }
        }
    }
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
    std::vector<float> floor, lift, cover;   // per cell: the scene's floor, what stands on it to be stood on, and how tall the tallest thing on it is
    std::vector<int> region;       // BuildRegions: the stretch of ground each cell is on (-1 for none)
    std::vector<int> regionSize;
    std::vector<uint8_t> upper;    // MarkUpper: cells with something out of reach standing over them
    bool heights = false;
};

// How tall a piece of scenery stands (what you stand on at the top of a boulder or block; a pillar is taller than anyone).
inline float SceneryHeight(const Prop& p) {
    if (IsPlatform(p.kind)) return PlatformHeight(p.kind);
    switch (p.kind) {
        case PropKind::Rock: return 24.0f;
        case PropKind::Boulder: return BoulderTop(BoulderShape(p.rot)) * BoulderScale(p.rot);
        case PropKind::Pillar: return 200.0f;
        default: return 0.0f;
    }
}

// The scenery as the bots see it: climbing blocks and low boulders can be climbed and stood on; tall boulders, rocks and pillars are walked
// round, and hide whoever is behind them.
inline void AddSceneryToNav(NavGrid& grid, const std::vector<Prop>& props) {
    for (const Prop& p : props) {
        const float r = PropRadius(p.kind);
        if (r <= 0) continue;
        if (IsPlatform(p.kind)) { grid.AddStandable(p.pos, kPlatformHalf, true, PlatformHeight(p.kind), r + 20.0f); continue; }
        const float tall = SceneryHeight(p);
        if (p.kind == PropKind::Boulder && tall <= NavGrid::kClimbUp) { grid.AddStandable(p.pos, r * BoulderScale(p.rot) * 0.8f, false, tall, r + 20.0f); continue; }
        grid.Block(p.pos, r + 20.0f, tall);
    }
}

} // namespace royale
