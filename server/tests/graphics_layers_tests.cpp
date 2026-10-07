#include "graphics_layers.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace royale::gfxlayers;
void require(bool ok, const char* what) { if (!ok) { std::cerr << "graphics layers test failed: " << what << "\n"; std::exit(1); } }
int main() {
    // The pool grows when a frame comes close to filling it, never past the cap, and stays put when there is room.
    require(NextPoolSize(kPoolMin, 10 * 1024, false) == kPoolMin, "quiet frame keeps the pool");
    require(NextPoolSize(kPoolMin, kPoolMin * 8 / 10, false) > kPoolMin, "busy frame grows the pool");
    require(NextPoolSize(kPoolMin, 3 * kPoolMin, false) * 7 >= 3 * kPoolMin * 10, "a much busier frame grows it enough at once");
    require(NextPoolSize(kPoolMin, 10, true) > kPoolMin, "an overflow grows the pool");
    require(NextPoolSize(kPoolMax, kPoolMax, true) == kPoolMax, "never past the cap");

    // Windows fit what was used, shrink slowly, double after an overflow, and never go below the minimum.
    require(NextXluWindow(kXluMin, 20000, false) >= 30000, "window fits use");
    require(NextXluWindow(100000, 0, false) >= 99000, "window shrinks slowly");
    require(NextXluWindow(100000, 0, false, 96 * 1024) >= 96 * 1024, "window keeps its floor");
    require(NextXluWindow(8192, 4000, false, kXluMin, true) >= 16384, "a layer cut short gets twice the room");
    require(NextXluWindow(8192, 0, true) >= 16384, "overflow doubles the window");
    require(NextXluWindow(0, 0, false) == kXluMin, "minimum window");

    // Layers laid out one after another in a pool never overlap, and data comes from the back.
    std::vector<uint8_t> pool(256 * 1024);
    Span free{pool.data(), pool.data() + pool.size()};
    const Plan a = PlanLayer(free, 8000);
    require(a.ok && a.xlu == pool.data() && a.xluEnd - a.xlu >= 8000 && a.opa == a.xluEnd, "first layer layout");
    const Used ua = Measure(a, a.xlu + 512, a.opa + 1024, a.data - 4096);
    require(!ua.overflowed && ua.xlu == 512 && ua.opa == 1024 && ua.data == 4096, "measure");
    free = After(a, ua);
    require(free.head >= a.opa + 1024 + kEndSlack && free.tail == a.data - 4096, "space after a layer");
    uint8_t* d = TakeData(free, 100);
    require(d != nullptr && d == pool.data() + pool.size() - 4096 - 112, "loose data from the back, aligned");
    const Plan b = PlanLayer(free, 4096);
    require(b.ok && b.xlu >= free.head && b.data <= d, "second layer after the first");

    // An idle layer hands its room back; one that drew only see-through keeps just that.
    require(After(a, ua, false, false).head == a.xlu, "idle layer costs nothing");
    require(After(a, ua, false, true).head == a.xlu + AlignUp(512 + kEndSlack), "see-through only keeps its part");

    // A stream that reaches its end is an overflow; a pool too full for a layer says so.
    require(Measure(a, a.xluEnd - 8, a.opa, a.data).overflowed, "translucent overflow");
    require(Measure(a, a.xlu, a.data - 8, a.data).overflowed, "opaque meets data");
    Span tiny{pool.data(), pool.data() + 8 * 1024};
    require(!PlanLayer(tiny, 4096).ok, "no room, no layer");
    require(TakeData(tiny, 8 * 1024, 1) == nullptr, "data refused when full");

    // Anchors are grid corners near the camera, the same for every camera position inside one cell.
    const Anchor p = AnchorNear(100, 50, -10), q = AnchorNear(2000, 2000, -2000);
    require(p.cx == 0 && p.cy == 0 && p.cz == -1 && p.x == 0 && p.z == -2048, "anchor cell");
    require(q.cx == 0 && q.cy == 0 && q.cz == -1, "same cell, same anchor");
    require(AnchorNear(2049, 0, 0).cx == 1, "next cell");
    std::cout << "graphics layers tests passed\n";
}
