#pragma once
// The bookkeeping behind the mod's graphics layers (RoyaleMod.cpp, "graphics layers"). No game headers here, so the rules can be tested on their own.
//
// Every effect (sky, fog, grass, ground patches, storm wall, weather, water, the big character models) used to write its drawing commands and its
// per-frame vertices into the game's own display-list buffers. Those are small and fixed (the translucent one holds 4096 commands; the opaque one
// about 195 KB shared with every Graph_Alloc), sized for the original game. With all our layers on they ran out: on a full frame the game threw the
// whole frame away ("Zelda 1 is dead"), and on a nearly full one our allocator refused memory and that layer skipped the frame. Both show as flicker.
//
// Now each layer gets its own room in one pool the mod owns: a translucent window and an opaque stream at the front of the free space, its vertices
// and matrices taken from the back. The game's buffers only get one "draw this list" command per layer. The pool is sized from what the frames
// actually use (it grows when a frame comes close to filling it, never past a cap), and each layer's translucent window is sized from what that
// layer used the frames before, so memory goes where it is needed.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace royale::gfxlayers {

constexpr size_t kAlign = 16;                      // commands, vertices and matrices all sit on 16-byte boundaries
constexpr size_t kPoolMin = 512 * 1024;           // the pool starts here (per frame; there are two, one being built while the other is drawn)
constexpr size_t kPoolMax = 6 * 1024 * 1024;      // and never grows past this
constexpr size_t kGuard = 64 * 1024;              // spare room past the end, so a layer that overruns writes into nothing that matters
constexpr size_t kXluMin = 4 * 1024;              // the smallest translucent window a layer gets
constexpr size_t kEndSlack = 16 * 16;             // room always kept at the end of a stream for its closing commands

inline size_t AlignUp(size_t n) { return (n + kAlign - 1) & ~(kAlign - 1); }

// The size the pool should be next frame, given its size now, the most it held in a recent frame, and whether something did not fit.
// It grows by half when a frame used more than 70% (or something overflowed), so a busy scene settles after a frame or two.
inline size_t NextPoolSize(size_t now, size_t peakUsed, bool overflowed) {
    size_t want = std::max(now, kPoolMin);
    if (overflowed || peakUsed * 10 > want * 7) want = want + want / 2;
    while (peakUsed * 10 > want * 7 && want < kPoolMax) want = want + want / 2;
    return std::min(AlignUp(want), kPoolMax);
}

// The translucent window to give a layer next frame: what it used plus half again (and a little), never below `floor` (the layer's own starting size,
// big enough for its busiest normal frame). It shrinks slowly (1% a frame) when a layer needs less, so a layer that goes between busy and quiet frames
// keeps a window that fits both. `short_`: the layer ran out of room and drew only part of itself; like an overflow, that doubles the window.
inline size_t NextXluWindow(size_t window, size_t used, bool overflowed, size_t floor = kXluMin, bool short_ = false) {
    const size_t fit = AlignUp(used + used / 2 + 2048);
    size_t next = std::max(fit, window - window / 100);
    if (overflowed || short_) next = std::max(next, window * 2);
    return std::max(next, std::max(floor, kXluMin));
}

// The free space of one frame's pool: commands grow up from `head`, data comes down from `tail`.
struct Span {
    uint8_t* head = nullptr;
    uint8_t* tail = nullptr;
    size_t Free() const { return tail > head ? static_cast<size_t>(tail - head) : 0; }
};

// Where one layer writes this frame: [xlu, xluEnd) for translucent commands, then opaque commands from `opa` up and data from `data` down.
struct Plan {
    bool ok = false;
    uint8_t* xlu = nullptr;
    uint8_t* xluEnd = nullptr;
    uint8_t* opa = nullptr;
    uint8_t* data = nullptr;
};

// Lays a layer out in the free space, or says it does not fit (then the layer draws into the game's buffers as before).
inline Plan PlanLayer(const Span& free, size_t xluWindow, size_t minOpaque = 16 * 1024) {
    Plan p;
    const size_t win = AlignUp(std::max(xluWindow, kXluMin));
    if (free.Free() < win + minOpaque) return p;
    p.xlu = free.head;
    p.xluEnd = free.head + win;
    p.opa = p.xluEnd;
    p.data = free.tail;
    p.ok = true;
    return p;
}

// What a layer actually used, read back when it ends: how far each stream got and how much data it took. A stream that reached the end of its room
// (less than the closing slack left) has overflowed; the layer is then not drawn this frame, and its window and the pool grow for the next.
struct Used {
    size_t xlu = 0, opa = 0, data = 0;
    bool overflowed = false;
};
inline Used Measure(const Plan& p, const uint8_t* xluHead, const uint8_t* opaHead, const uint8_t* dataTail) {
    Used u;
    u.xlu = xluHead > p.xlu ? static_cast<size_t>(xluHead - p.xlu) : 0;
    u.opa = opaHead > p.opa ? static_cast<size_t>(opaHead - p.opa) : 0;
    u.data = p.data > dataTail ? static_cast<size_t>(p.data - dataTail) : 0;
    u.overflowed = xluHead + kEndSlack > p.xluEnd || opaHead + kEndSlack > dataTail;
    return u;
}

// The free space left for the next layer: past this layer's opaque commands, and below its data. A stream the game will not be told to draw (the
// layer drew nothing solid, or nothing at all) is handed back, so a layer that is switched off or idle this frame costs no room.
inline Span After(const Plan& p, const Used& u, bool opaKept = true, bool xluKept = true) {
    Span s;
    if (opaKept) s.head = p.opa + AlignUp(u.opa + kEndSlack);
    else if (xluKept) s.head = p.xlu + AlignUp(u.xlu + kEndSlack);
    else s.head = p.xlu;
    s.tail = p.data - u.data;
    if (s.head > s.tail) s.head = s.tail;
    return s;
}

// Data (vertices, matrices) taken from the back of the free space outside any layer. nullptr when it does not fit with `reserve` bytes to spare.
inline uint8_t* TakeData(Span& free, size_t bytes, size_t reserve = 0) {
    const size_t n = AlignUp(bytes);
    if (free.Free() < n + reserve) return nullptr;
    free.tail -= n;
    return free.tail;
}

// A fixed place in the world near the camera that world effects (fog banks, weather specks, wind streaks) are drawn relative to. The game draws
// in-between frames by blending each matrix from the last frame to this one, but uses this frame's vertices: a matrix that follows the camera would
// carry camera-relative vertices along with the blended camera and make them shimmer. Anchored to a grid cell, the matrix only changes when the
// camera crosses into another cell, and then the layer starts a new identity so the blend is skipped for that one frame.
struct Anchor { float x, y, z; int cx, cy, cz; };
inline Anchor AnchorNear(float eyeX, float eyeY, float eyeZ, float cell = 2048.0f) {
    Anchor a;
    a.cx = static_cast<int>(std::floor(eyeX / cell));
    a.cy = static_cast<int>(std::floor(eyeY / cell));
    a.cz = static_cast<int>(std::floor(eyeZ / cell));
    a.x = a.cx * cell; a.y = a.cy * cell; a.z = a.cz * cell;
    return a;
}

}  // namespace royale::gfxlayers
