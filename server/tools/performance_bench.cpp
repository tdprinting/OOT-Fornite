#include "nav.h"
#include "water_sim.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static bool tracking = false;
static size_t allocations = 0, bytes = 0;
void* operator new(size_t n) {
    if (tracking) { ++allocations; bytes += n; }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
template<class F> void measure(const char* name, int count, F fn) {
    allocations = bytes = 0;
    tracking = true;
    auto start = std::chrono::steady_clock::now();
    int successes = 0;
    for (int i = 0; i < count; ++i) successes += fn();
    auto end = std::chrono::steady_clock::now();
    tracking = false;
    double ms = std::chrono::duration<double, std::milli>(end-start).count();
    std::printf("%s: %.3f ms/query, %.1f allocations/query, %.1f KiB requested/query, success=%d/%d\n",
                name, ms/count, double(allocations)/count, double(bytes)/count/1024, successes, count);
}
int main() {
    // Synthetic 221 x 221 grid, split by an impassable north/south wall.
    royale::NavGrid grid({{0,0},6600}, [](royale::Vec2 p) { return std::fabs(p.x) > 90; });
    royale::NavGrid::SearchWorkspace work;
    std::vector<royale::Vec2> path;
    std::vector<royale::NavGrid::Stop> route;
    grid.FindPath({-3000,0},{3000,0},path,true,&work);
    measure("unreachable FindPath (warm)", 40, [&] { return grid.FindPath({-3000,0},{3000,0},path,true,&work); });
    measure("unreachable FindRoute", 40, [&] { return grid.FindRoute({-3000,0},0,false,{3000,0},0,false,route,&work); });
    measure("clear FindPath", 2000, [&] { return grid.FindPath({-3000,0},{-3000,3000},path,true,&work); });
    measure("clear FindRoute", 40, [&] { return grid.FindRoute({-3000,0},0,false,{-3000,3000},0,false,route,&work); });
    royale::water::RippleField ripples;
    ripples.Recenter(0,0);
    measure("idle RippleField 50ms", 2000, [&] { ripples.Step(0.05f); return ripples.Activity() == 0; });
}
