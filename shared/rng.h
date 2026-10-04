#pragma once
#include <cstdint>

// Deterministic RNG (splitmix64). Used instead of <random> distributions, which differ between standard libraries,
// so the integer sequence is identical everywhere. Anything built with floating-point trig from it (storm circles, loot positions) is generated on the server only and sent to clients.
namespace royale {
class Rng {
  public:
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t Next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // [0, 1)
    double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
    // [0, n)
    uint32_t Below(uint32_t n) { return static_cast<uint32_t>(Next() % n); }

  private:
    uint64_t state;
};
} // namespace royale
