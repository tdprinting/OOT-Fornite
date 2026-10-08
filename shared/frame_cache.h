#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace royale {
// Fixed storage: collisions replace one slot, without node allocation or mass eviction.
// A miss can be deferred once the frame's expensive-query allowance is spent.
template<typename T, size_t Capacity> class FrameCache {
    struct Entry { uint64_t key = 0; uint32_t frame = 0, epoch = 0; T value{}; bool valid = false; };
    std::array<Entry, Capacity> entries{};
    uint32_t frame = 0, epoch = 1;
    int remaining = 0;
  public:
    void BeginFrame(uint32_t now, int queries) { frame = now; remaining = queries; }
    void Invalidate() {
        if (++epoch == 0) { for (auto& e : entries) e.valid = false; epoch = 1; }
    }
    int Remaining() const { return remaining; }
    template<typename F> bool Get(uint64_t key, uint32_t ttl, T* out, F query) {
        uint64_t h = key;
        h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27; h *= 0x94d049bb133111ebULL; h ^= h >> 31;
        Entry& e = entries[h % Capacity];
        if (e.valid && e.key == key && e.epoch == epoch && frame - e.frame <= ttl) { *out = e.value; return true; }
        if (remaining <= 0) return false;
        --remaining;
        e = {key, frame, epoch, query(), true};
        *out = e.value; return true;
    }
};
}
