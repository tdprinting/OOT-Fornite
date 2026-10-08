#pragma once
#include "../shared/loot.h"
#include "../shared/props.h"
#include "../shared/storm.h"
#include "../shared/terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
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
    // Anywhere a bot can stand: open ground, the ground right beside scenery, and the tops of blocks and low boulders. (Not water: carts and
    // landings keep out of it. A swimming bot is Passable.)
    bool Standable(Vec2 p) const {
        int cx, cz;
        return ToCell(p, cx, cz) && cells[Index(cx, cz)] != kBlocked && cells[Index(cx, cz)] != kWater;
    }
    // Anywhere a bot can get to: Standable, or water it can swim.
    bool Passable(Vec2 p) const {
        int cx, cz;
        return ToCell(p, cx, cz) && cells[Index(cx, cz)] != kBlocked;
    }
    bool Swimming(Vec2 p) const {
        int cx, cz;
        return hasWater && ToCell(p, cx, cz) && cells[Index(cx, cz)] == kWater;
    }
    // Make water cells swimmable: every blocked cell `swimmable` accepts, with a bot's body floating at `level` less `depth` (so its height is
    // that of the surface, not the bed: the floor stays the bed's, as the clients draw from it).
    void AddWater(float level, float depth, const PlacementFn& swimmable) {
        for (int cz = 0; cz < h; cz++) {
            for (int cx = 0; cx < w; cx++) {
                const size_t i = static_cast<size_t>(Index(cx, cz));
                if (cells[i] != kBlocked) continue;
                const Vec2 c = CellCentre(cx, cz);
                if (Distance(c, map.center) > map.radius || !swimmable(c)) continue;
                cells[i] = kWater;
                lift[i] = (level - depth) - floor[i];
                hasWater = true;
            }
        }
    }
    float WaterLift(Vec2 p) const {
        int cx, cz;
        return hasWater && ToCell(p, cx, cz) && cells[Index(cx, cz)] == kWater ? lift[Index(cx, cz)] : 0.0f;
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

    // ---- upper floors ----------------------------------------------------------------------------------------------------------
    // Somewhere to stand above the ground: an upper floor, a ramp, a roof or a landing. The grid itself has one height per cell, so these sit beside
    // it as extra nodes. A node joins its neighbours that differ by a step, and joins a ground cell where a ramp or a doorstep meets it.
    struct UpperNode { float x, z, y; };
    // One stop of a route: where, and how high the feet are there.
    struct Stop { Vec2 p; float y; bool upper; bool climb; };
    // Owned by the simulation, never by a shared/const grid. Retained storage
    // and generation stamps avoid clearing or allocating a grid on every query.
    struct SearchBudget {
        int expansions = 32768, searches = 12;
        void Reset() { expansions = 32768; searches = 12; }
    };
    struct SearchWorkspace {
        struct Node { float f; int idx; bool operator<(const Node& o) const { return f > o.f; } };
        std::vector<float> cost;
        std::vector<int> parent;
        std::vector<uint8_t> via;
        std::vector<uint32_t> stamp;
        std::vector<Node> heap;
        std::vector<Stop> rawRoute;
        std::vector<Vec2> rawPath;
        uint32_t generation = 0;
        int expanded = 0;
        bool deferred = false;
        void Begin(size_t n) {
            cost.resize(n); parent.resize(n); via.resize(n); stamp.resize(n, 0);
            if (++generation == 0) { std::fill(stamp.begin(), stamp.end(), 0); generation = 1; }
            heap.clear(); rawRoute.clear(); rawPath.clear(); expanded = 0; deferred = false;
        }
        void Touch(size_t i) {
            if (stamp[i] == generation) return;
            stamp[i] = generation; cost[i] = 1e18f; parent[i] = -1; via[i] = 0;
        }
        float& Cost(size_t i) { Touch(i); return cost[i]; }
        int& Parent(size_t i) { Touch(i); return parent[i]; }
        uint8_t& Via(size_t i) { Touch(i); return via[i]; }
        void push(Node n) { heap.push_back(n); std::push_heap(heap.begin(), heap.end()); }
        Node top() const { return heap.front(); }
        void pop() { std::pop_heap(heap.begin(), heap.end()); heap.pop_back(); }
        bool empty() const { return heap.empty(); }
    };
    struct SearchContext { SearchWorkspace workspace; SearchBudget budget; };

    // The same terrain sight test used at perception and authoritative strike time.
    bool SightClear(Vec2 a, float ya, Vec2 b, float yb) const {
        const float len = Distance(a, b);
        const int steps = static_cast<int>(len / 40.0f);
        for (int i = 1; i < steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            if (t * len < 45 || (1 - t) * len < 45) continue;
            if (TopAt({a.x + (b.x-a.x)*t, a.z + (b.z-a.z)*t}) > ya + (yb-ya)*t) return false;
        }
        return true;
    }

    void AddUpper(const std::vector<UpperNode>& nodes) {
        up = nodes;
        upAt.clear();
        for (size_t i = 0; i < up.size(); i++) {
            int cx, cz;
            if (ToCell({up[i].x, up[i].z}, cx, cz)) upAt[Index(cx, cz)].push_back(static_cast<int>(i));
        }
    }
    bool HasUpper() const { return !up.empty(); }
    // The node nearest p whose height is within maxDy of y (-1 when there is none within maxD).
    int UpperNear(Vec2 p, float y, float maxD = 50.0f, float maxDy = 60.0f) const {
        int cx, cz, best = -1;
        float bestD = maxD;
        ToCellClamped(p, cx, cz);
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                auto it = upAt.find(Index(x, z));
                if (it == upAt.end()) continue;
                for (int i : it->second) {
                    const float d = Distance(p, {up[static_cast<size_t>(i)].x, up[static_cast<size_t>(i)].z});
                    if (d <= bestD && std::fabs(up[static_cast<size_t>(i)].y - y) <= maxDy) { bestD = d; best = i; }
                }
            }
        }
        return best;
    }
    // The stretch of ground (BuildRegions) an upper node or a point is on, for tests and tools.
    int UpperRegion(int node) const { return region.empty() ? -1 : region[cells.size() + static_cast<size_t>(node)]; }
    int RegionAt(Vec2 p) const { int cx, cz; return region.empty() || !ToCell(p, cx, cz) ? -1 : region[Index(cx, cz)]; }
    size_t UpperCount() const { return up.size(); }
    float UpperY(int node) const { return up[static_cast<size_t>(node)].y; }
    // A chest on an upper floor or a roof: it stands over ground that is open, so the cell below it is walkable, but the chest is up at y.
    void MarkUpper(Vec2 p, float y) {
        int cx, cz;
        if (ToCell(p, cx, cz)) upperGoal[Index(cx, cz)] = y;
    }
    bool UpperGoal(Vec2 p, float* y) const {
        if (upperGoal.empty()) return false;
        int cx, cz;
        if (!ToCell(p, cx, cz)) return false;
        auto it = upperGoal.find(Index(cx, cz));
        if (it == upperGoal.end()) return false;
        if (y) *y = it->second;
        return true;
    }

    // Which stretch of ground a cell belongs to: cells joined by steps no taller than a clamber (kClimbUp) in either direction are one stretch,
    // and so are upper nodes and the ground they are joined to. An island the bots have no way onto is a stretch of its own. Call once the
    // scenery and the upper nodes are added, before bots use Connected.
    void BuildRegions() {
        const size_t total = cells.size() + up.size();
        region.assign(total, -1);
        regionSize.clear();
        std::vector<int> stack;
        for (size_t s = 0; s < total; s++) {
            if (s < cells.size() && cells[s] == kBlocked) continue;
            if (region[s] >= 0) continue;
            const int id = static_cast<int>(regionSize.size());
            int count = 0;
            region[s] = id;
            stack.push_back(static_cast<int>(s));
            auto visit = [&](int b) { if (region[static_cast<size_t>(b)] < 0) { region[static_cast<size_t>(b)] = id; stack.push_back(b); } };
            while (!stack.empty()) {
                const int a = stack.back();
                stack.pop_back();
                count++;
                if (!climbLinks.empty()) { auto it = climbLinks.find(a); if (it != climbLinks.end()) for (const auto& l : it->second) visit(l.first); }
                if (static_cast<size_t>(a) >= cells.size()) { UpperLinks(a - static_cast<int>(cells.size()), visit); continue; }
                const int ax = a % w, az = a / w;
                for (int dz = -1; dz <= 1; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int x = ax + dx, z = az + dz;
                        if ((!dx && !dz) || x < 0 || z < 0 || x >= w || z >= h) continue;
                        const int b = Index(x, z);
                        if (cells[b] == kBlocked || region[b] >= 0) continue;
                        if (dx && dz && (cells[Index(ax + dx, az)] == kBlocked || cells[Index(ax, az + dz)] == kBlocked)) continue;   // no squeezing between corners
                        const bool cliff = climbing && cells[b] != kWater && cells[static_cast<size_t>(a)] != kWater && !(dx && dz);   // a bare cliff can be climbed
                        if (std::fabs(Height(b) - Height(a)) > (cliff ? kCliffMax : kClimbUp)) continue;
                        visit(b);
                    }
                }
                GroundToUpper(a, visit);
            }
            regionSize.push_back(count);
        }
    }
    // Can a bot standing at a get to b? True when either is not on known ground, or when a is on a scrap of ground too small to say (a bot dropped
    // onto a stray ledge should not stop wanting everything). `aY` is a's height when it is up on a floor or a roof (otherwise NaN). A chest marked
    // as upper is reachable only through the upper nodes.
    bool Connected(Vec2 a, Vec2 b, float aY = std::numeric_limits<float>::quiet_NaN()) const {
        if (region.empty()) return true;
        int ax, az, bx, bz;
        if (!ToCell(a, ax, az) || !ToCell(b, bx, bz)) return true;
        int ra = region[Index(ax, az)], rb = region[Index(bx, bz)];
        if (!std::isnan(aY)) { const int n = UpperNear(a, aY, 70.0f, 80.0f); if (n >= 0) ra = region[cells.size() + static_cast<size_t>(n)]; }
        float gy;
        if (UpperGoal(b, &gy)) {
            const int n = UpperNear(b, gy, 70.0f, 60.0f);
            if (n < 0) return false;
            rb = region[cells.size() + static_cast<size_t>(n)];
        }
        if (ra < 0 || rb < 0 || ra == rb) return true;
        return regionSize[static_cast<size_t>(ra)] < 150;
    }

    // ---- climbing --------------------------------------------------------------------------------------------------------------
    // A steep face a bot can climb is a link between the foot of it and its top: bare cliffs (a drop of kCliffMin to kCliffMax between neighbouring
    // cells) are found by the route search itself; ivy walls and the like are added with AddClimb.
    static constexpr float kCliffMin = 70.0f, kCliffMax = 900.0f;
    void SetClimbing(bool on) { climbing = on; }
    bool HasRoutes() const { return !up.empty() || hasWater || climbing; }
    // A climbable wall: from the ground at `foot` (standing at height footY) up to the floor behind its top edge (the parapet is over the floor, so a
    // floor a little under topY counts), at `top`.
    void AddClimb(Vec2 foot, float footY, Vec2 top, float topY) {
        int fx, fz;
        if (!ToCell(foot, fx, fz)) return;
        const int a = Index(fx, fz);
        if (cells[static_cast<size_t>(a)] == kBlocked || cells[static_cast<size_t>(a)] == kWater || std::fabs(Height(a) - footY) > 160.0f) return;
        int b = -1, tx, tz;
        float bestY = -1e9f;
        ToCellClamped(top, tx, tz);
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int x = tx + dx, z = tz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                auto it = upAt.find(Index(x, z));
                if (it == upAt.end()) continue;
                for (int j : it->second) {
                    const UpperNode& m = up[static_cast<size_t>(j)];
                    if (Distance({m.x, m.z}, top) <= 100.0f && m.y >= topY - 170.0f && m.y <= topY + 30.0f && m.y > bestY) { bestY = m.y; b = static_cast<int>(cells.size()) + j; }
                }
            }
        }
        if (b < 0 && ToCell(top, tx, tz) && cells[static_cast<size_t>(Index(tx, tz))] != kBlocked && cells[static_cast<size_t>(Index(tx, tz))] != kWater &&
            Height(Index(tx, tz)) >= topY - 170.0f && Height(Index(tx, tz)) <= topY + 30.0f)
            b = Index(tx, tz);
        if (b < 0 || b == a || NodeY(b) - NodeY(a) < 60.0f) return;
        auto& list = climbLinks[a];
        for (const auto& l : list) if (l.first == b) return;
        const float cost = 2.5f + (NodeY(b) - NodeY(a)) / 40.0f;
        list.push_back({b, cost});
        climbLinks[b].push_back({a, cost});
        climbing = true;
    }

    // A route that may use the upper nodes, swim and climb: ramps and stairs up to a floor or a roof, rivers, cliffs and ivy. `fromUp` says the bot
    // is on an upper floor (at height fromY); `toUp` that the goal is (at toY). The stops carry their heights; the last stop is `to` itself.
    bool FindRoute(Vec2 from, float fromY, bool fromUp, Vec2 to, float toY, bool toUp, std::vector<Stop>& out, SearchWorkspace* scratch = nullptr, SearchBudget* budget = nullptr) const {
        SearchWorkspace local;
        SearchWorkspace& work = scratch ? *scratch : local;
        work.deferred = false; work.expanded = 0;
        out.clear();
        if (!Connected(from, to, fromUp ? fromY : std::numeric_limits<float>::quiet_NaN())) return false;
        const int base = static_cast<int>(cells.size());
        int start = -1, goal = -1;
        if (fromUp) { const int n = UpperNear(from, fromY, 70.0f, 80.0f); if (n >= 0) start = base + n; }
        if (toUp) { const int n = UpperNear(to, toY, 70.0f, 60.0f); if (n < 0) return false; goal = base + n; }
        Vec2 a, b;
        if (start < 0) { if (!Snap(from, &a, true, true)) return false; int cx, cz; ToCellClamped(a, cx, cz); start = Index(cx, cz); }
        if (goal < 0) { if (!Snap(to, &b, true)) return false; int cx, cz; ToCellClamped(b, cx, cz); goal = Index(cx, cz); }
        if (start == goal) { out.push_back({to, NodeY(goal), goal >= base, false}); return true; }
        if (!fromUp && !toUp && up.empty() && !hasWater && LineClear(a, b, true)) {
            out.push_back({b, StandHeight(b), false, false}); return true;
        }
        const Vec2 goalAt = NodeXZ(goal);

        const size_t total = cells.size() + up.size();
        if (budget && (budget->searches <= 0 || budget->expansions <= 0)) { work.deferred = true; return false; }
        if (budget) --budget->searches;
        work.Begin(total);
        auto& open = work;
        auto& g = work.cost; auto& parent = work.parent; auto& via = work.via;
        work.Touch(static_cast<size_t>(start));
        g[static_cast<size_t>(start)] = 0;
        open.push({Distance(NodeXZ(start), goalAt) / kCell, start});
        int expansions = 0;
        const int cap = MaxExpansions() + static_cast<int>(up.size());
        bool reached = false;
        static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dzs[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!open.empty() && expansions < cap && (!budget || budget->expansions > 0)) {
            const SearchWorkspace::Node n = open.top();
            open.pop();
            if (n.idx == goal) { reached = true; break; }
            if (n.f - Distance(NodeXZ(n.idx), goalAt) / kCell > g[static_cast<size_t>(n.idx)] + 1e-3f) continue;
            expansions++; ++work.expanded; if (budget) --budget->expansions;
            auto relax = [&](int to2, float step, bool climb) {
                work.Touch(static_cast<size_t>(to2));
                const float ng = g[static_cast<size_t>(n.idx)] + step;
                if (ng < g[static_cast<size_t>(to2)]) {
                    g[static_cast<size_t>(to2)] = ng;
                    parent[static_cast<size_t>(to2)] = n.idx;
                    via[static_cast<size_t>(to2)] = climb ? 1 : 0;
                    open.push({ng + Distance(NodeXZ(to2), goalAt) / kCell, to2});
                }
            };
            if (!climbLinks.empty()) {
                auto it = climbLinks.find(n.idx);
                if (it != climbLinks.end()) for (const auto& l : it->second) relax(l.first, l.second, true);
            }
            if (n.idx >= base) {
                const Vec2 at = NodeXZ(n.idx);
                UpperLinks(n.idx - base, [&](int to2) { relax(to2, Distance(at, NodeXZ(to2)) / kCell + 0.05f, false); });
                continue;
            }
            const int cx = n.idx % w, cz = n.idx / w;
            for (int k = 0; k < 8; k++) {
                const int x = cx + dxs[k], z = cz + dzs[k];
                if (x < 0 || z < 0 || x >= w || z >= h || !Usable(Index(x, z), true)) continue;
                if (k >= 4 && (!Usable(Index(cx + dxs[k], cz), true) || !Usable(Index(cx, cz + dzs[k]), true))) continue;
                const int ni = Index(x, z);
                float step = k >= 4 ? 1.41421356f : 1.0f;
                const float rise = Height(ni) - Height(n.idx);
                bool climb = false;
                if (!StepOk(Height(n.idx), Height(ni))) {
                    // a cliff: climbed straight up or down where it is steep enough, and nowhere near water
                    if (!climbing || k >= 4 || cells[ni] == kWater || cells[static_cast<size_t>(n.idx)] == kWater ||
                        std::fabs(rise) < kCliffMin || std::fabs(rise) > kCliffMax) continue;
                    climb = true;
                    step = 2.5f + std::fabs(rise) / 40.0f;
                } else {
                    if (k >= 4 && (std::fabs(Height(Index(cx + dxs[k], cz)) - Height(n.idx)) > kStepUp ||
                                   std::fabs(Height(Index(cx, cz + dzs[k])) - Height(n.idx)) > kStepUp)) continue;
                    if (rise > kStepUp) step += 1.5f; else if (-rise > kStepUp) step += 0.4f;
                    if (cells[ni] == kWater) step *= 2.2f;       // swimming is slow
                }
                relax(ni, step, climb);
            }
            GroundToUpper(n.idx, [&](int to2) { relax(to2, Distance(NodeXZ(n.idx), NodeXZ(to2)) / kCell + 0.05f, false); });
        }
        if (!reached) { work.deferred = budget && budget->expansions <= 0; return false; }

        auto& raw = work.rawRoute;
        for (int i = goal; i != start && i >= 0; i = parent[static_cast<size_t>(i)]) raw.push_back({NodeXZ(i), NodeY(i), i >= base, via[static_cast<size_t>(i)] != 0});
        std::reverse(raw.begin(), raw.end());
        if (raw.empty()) { out.push_back({to, NodeY(goal), goal >= base, false}); return true; }
        raw.back().p = to;
        // String-pull the stretches of plain ground and water (a straight line that is clear is one stop); ramps, floors and climbs keep every stop.
        Vec2 at = NodeXZ(start);
        size_t i = 0;
        while (i < raw.size()) {
            size_t far = i;
            if (!raw[i].upper && !raw[i].climb) {
                size_t last = i;
                while (last + 1 < raw.size() && !raw[last+1].upper && !raw[last+1].climb) ++last;
                for (size_t j = last; j > i; --j) {
                    if (LineClear(at, raw[j].p, true)) { far = j; break; }
                }
            }
            out.push_back(raw[far]);
            at = raw[far].p;
            i = far + 1;
        }
        return true;
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
            if (!Passable(at)) return false;
            if (i > 0) {
                const float up = StandHeight(at) - StandHeight(prev);
                if (up > kStepUp || -up > kDropDown) return false;
            }
            prev = at;
        }
        return true;
    }

    // The nearest walkable point to p (p itself if it's fine). Searches outward in rings; returns false if there is none nearby.
    bool Snap(Vec2 p, Vec2* out, bool climber = false, bool swimming = false) const {
        if (climber ? (swimming ? Passable(p) : Standable(p)) : Walkable(p)) { *out = p; return true; }
        int cx, cz;
        ToCellClamped(p, cx, cz);
        for (int r = 1; r <= 12; r++) {
            float best = 1e18f;
            bool found = false;
            for (int dz = -r; dz <= r; dz++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= w || z >= h || !Usable(Index(x, z), climber) || (!swimming && cells[Index(x, z)] == kWater)) continue;
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
    bool FindPath(Vec2 from, Vec2 to, std::vector<Vec2>& path, bool climber = false, SearchWorkspace* scratch = nullptr, SearchBudget* budget = nullptr) const {
        SearchWorkspace local;
        SearchWorkspace& work = scratch ? *scratch : local;
        work.deferred = false; work.expanded = 0;
        path.clear();
        if (climber && !Connected(from, to)) return false;
        Vec2 a, b;
        if (!Snap(from, &a, climber, true) || !Snap(to, &b, climber)) return false;
        if (LineClear(a, b, climber)) { path.push_back(b); return true; }

        int sx, sz, gx, gz;
        ToCellClamped(a, sx, sz);
        ToCellClamped(b, gx, gz);
        const int start = Index(sx, sz), goal = Index(gx, gz);
        if (start == goal) { path.push_back(b); return true; }

        if (budget && (budget->searches <= 0 || budget->expansions <= 0)) { work.deferred = true; return false; }
        if (budget) --budget->searches;
        work.Begin(cells.size());
        auto& open = work;
        auto& g = work.cost; auto& parent = work.parent;
        work.Touch(static_cast<size_t>(start));
        g[start] = 0;
        open.push({Heuristic(sx, sz, gx, gz), start});
        int expansions = 0;
        bool reached = false;
        static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dzs[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!open.empty() && expansions < MaxExpansions() && (!budget || budget->expansions > 0)) {
            const SearchWorkspace::Node n = open.top();
            open.pop();
            if (n.idx == goal) { reached = true; break; }
            const int cx = n.idx % w, cz = n.idx / w;
            if (n.f - Heuristic(cx, cz, gx, gz) > g[n.idx] + 1e-3f) continue; // stale entry
            expansions++; ++work.expanded; if (budget) --budget->expansions;
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
                work.Touch(static_cast<size_t>(Index(x, z)));
                const float ng = g[n.idx] + step;
                if (ng < g[Index(x, z)]) {
                    g[Index(x, z)] = ng;
                    parent[Index(x, z)] = n.idx;
                    open.push({ng + Heuristic(x, z, gx, gz), Index(x, z)});
                }
            }
        }
        if (!reached) { work.deferred = budget && budget->expansions <= 0; return false; }

        auto& raw = work.rawPath;
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
    static constexpr uint8_t kBlocked = 0, kOpen = 1, kNearScenery = 2, kLifted = 3, kWater = 4;

    int Index(int cx, int cz) const { return cz * w + cx; }
    // Ids in a route: a ground cell is its index, an upper node is cells.size() + its number.
    Vec2 NodeXZ(int id) const {
        if (static_cast<size_t>(id) >= cells.size()) { const UpperNode& n = up[static_cast<size_t>(id) - cells.size()]; return {n.x, n.z}; }
        return CellCentre(id % w, id / w);
    }
    float NodeY(int id) const { return static_cast<size_t>(id) >= cells.size() ? up[static_cast<size_t>(id) - cells.size()].y : Height(id); }
    static constexpr float kLinkReach = 95.0f;
    static constexpr float kNodeSlope = 0.85f;   // the steepest ramp the nodes join (the house ramps climb about 0.7)
    // The upper nodes and the ground cells an upper node joins: neighbours a step apart, and ground a ramp or a doorstep meets.
    template <typename F> void UpperLinks(int node, F f) const {
        const UpperNode& n = up[static_cast<size_t>(node)];
        int cx, cz;
        ToCellClamped({n.x, n.z}, cx, cz);
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                const int g = Index(x, z);
                if (cells[g] != kBlocked && Distance(CellCentre(x, z), {n.x, n.z}) <= kLinkReach && std::fabs(Height(g) - n.y) <= kClimbUp) f(g);
                auto it = upAt.find(g);
                if (it == upAt.end()) continue;
                for (int j : it->second) {
                    if (j == node) continue;
                    const UpperNode& m = up[static_cast<size_t>(j)];
                    const float run = Distance({m.x, m.z}, {n.x, n.z});
                    if (run <= kLinkReach && std::fabs(m.y - n.y) <= kNodeSlope * run + 8.0f) f(static_cast<int>(cells.size()) + j);
                }
            }
        }
    }
    // The upper nodes a ground cell joins.
    template <typename F> void GroundToUpper(int cell, F f) const {
        if (up.empty()) return;
        const int cx = cell % w, cz = cell / w;
        const Vec2 at = CellCentre(cx, cz);
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                auto it = upAt.find(Index(x, z));
                if (it == upAt.end()) continue;
                for (int j : it->second) {
                    const UpperNode& m = up[static_cast<size_t>(j)];
                    if (Distance({m.x, m.z}, at) <= kLinkReach && std::fabs(m.y - Height(cell)) <= kClimbUp) f(static_cast<int>(cells.size()) + j);
                }
            }
        }
    }
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
    std::vector<UpperNode> up;     // AddUpper
    std::unordered_map<int, std::vector<int>> upAt;   // ground cell -> the upper nodes inside it
    std::unordered_map<int, float> upperGoal;         // MarkUpper: cell -> the height of the chest up there
    bool heights = false;
    bool hasWater = false;
    bool climbing = false;
    std::unordered_map<int, std::vector<std::pair<int, float>>> climbLinks;   // AddClimbs: ground cell or upper node -> where a climb leads and what it costs
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
