#pragma once
// The water's own physics and look, kept free of the game so it can be tested (server/tests/water_tests.cpp) and so later features (fish and
// other creatures, boats, water physics) can use the very same surface the player sees:
//   * Swell: the open-water waves, a sum of Gerstner waves (the classic "choppy" ocean wave: points move in circles, so crests are sharp and
//     troughs are wide). Gives height, sideways shift, normal and how folded the crest is (for whitecaps), at any point and time.
//   * RippleField: a small interactive height field (the wave equation on a grid) that follows the camera. Anything can push it (a swimmer, a
//     splash, a creature's fin); rings spread, cross and interfere, and fade at its edges.
//   * Textures: tileable, seamlessly looping flipbooks made at start-up: caustics (light focused by the waves onto the sea floor, made by
//     actually refracting a grid of light rays through a wavy surface), sun glints on the surface, and lacy foam. All 8-bit intensity (I8).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace royale::water {

constexpr float kTau = 6.28318530718f;

// ---- swell -------------------------------------------------------------------------------------------------------------------------------
struct GerstnerWave { float length, amp, angle, speed; };
// Long rollers to short chop. Amplitudes are for 100% swell in calm air (the caller scales them with the wind and the Graphics slider).
inline constexpr GerstnerWave kSwell[] = {
    { 900.0f, 6.5f, 0.60f, 150.0f }, { 560.0f, 4.2f, 1.35f, 118.0f }, { 340.0f, 2.7f, -0.25f, 92.0f },
    { 200.0f, 1.6f, 2.20f, 70.0f },  { 125.0f, 1.0f, 0.95f, 55.0f },  { 80.0f, 0.6f, -1.05f, 44.0f },
};
inline constexpr int kSwellCount = static_cast<int>(sizeof(kSwell) / sizeof(kSwell[0]));

struct SwellPoint {
    float h = 0.0f;                         // height above calm water
    float ox = 0.0f, oz = 0.0f;             // sideways shift of the surface point (Gerstner: crests bunch up)
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;  // unit normal
    float fold = 0.0f;                      // 0 calm .. 1 a crest about to break (whitecaps)
};

// The steepness of every wave together is kept under 1, so the surface never loops over itself. `chop` 0 gives round sine waves, 1 sharp crests.
inline float SwellSteepness(float amp, float chop) {
    float sum = 0.0f;
    for (const GerstnerWave& w : kSwell) sum += kTau / w.length * w.amp * amp;
    return sum > 1e-5f ? std::clamp(chop, 0.0f, 1.0f) * 0.92f / std::max(sum, 0.92f) : 0.0f;
}

// Per-frame wave constants. Sampling retains the original waves and shading.
class PreparedSwell {
    struct Wave { float k, a, dx, dz, phase; };
    Wave waves_[kSwellCount];
    float q_;
public:
    PreparedSwell(float t, float amp, float chop) : q_(SwellSteepness(std::max(0.0f, amp), chop)) {
        for (int i = 0; i < kSwellCount; ++i) {
            const auto& w = kSwell[i];
            const float k = kTau / w.length;
            waves_[i] = {k, w.amp * std::max(0.0f, amp), std::cos(w.angle), std::sin(w.angle), -k * w.speed * t};
        }
    }
    SwellPoint Sample(float x, float z) const {
        SwellPoint p;
        float gx = 0, gz = 0, gy = 0;
        for (const auto& w : waves_) {
            const float th = w.k * (w.dx * x + w.dz * z) + w.phase;
            const float s = std::sin(th), c = std::cos(th);
            p.h += w.a * s;
            p.ox += q_ * w.a * w.dx * c; p.oz += q_ * w.a * w.dz * c;
            gx += w.dx * w.k * w.a * c; gz += w.dz * w.k * w.a * c;
            gy += q_ * w.k * w.a * s;
        }
        p.nx = -gx; p.ny = 1 - gy; p.nz = -gz;
        const float l = std::sqrt(p.nx*p.nx + p.ny*p.ny + p.nz*p.nz);
        p.nx /= l; p.ny /= l; p.nz /= l;
        p.fold = std::clamp(gy / 0.92f, 0.0f, 1.0f);
        return p;
    }
};
inline SwellPoint Swell(float x, float z, float t, float amp, float chop) {
    return PreparedSwell(t, amp, chop).Sample(x, z);
}

// ---- interactive ripples -----------------------------------------------------------------------------------------------------------------
// A square of kN x kN cells, `cell` units each, whose corner moves on the world's own grid as it follows the camera (so what is in it stays put).
// Heights are in world units; negative is pushed down.
class RippleField {
public:
    static constexpr int kN = 64;
    static constexpr int kSponge = 6;   // cells at the edge that soak the rings up, so they do not bounce off an invisible wall

    explicit RippleField(float cell = 14.0f, float speed = 120.0f, float damping = 0.55f)
        : cell_(cell), speed_(speed), damping_(damping), h_(kN * kN, 0.0f), v_(kN * kN, 0.0f), tmp_(kN * kN, 0.0f) {
        constexpr float dt = 1.0f / 60.0f;
        keep_ = std::exp(-damping_ * dt);
        for (int e = 0; e <= kSponge; ++e) sponge_[e] = e == kSponge ? 1.0f : std::exp(-(kSponge-e)*2.2f*dt*6.0f);
    }

    float Cell() const { return cell_; }
    float Size() const { return cell_ * kN; }
    float MinX() const { return ox_ * cell_; }
    float MinZ() const { return oz_ * cell_; }
    bool Contains(float x, float z, float margin = 0.0f) const {
        return x >= MinX() + margin && z >= MinZ() + margin && x <= MinX() + Size() - margin && z <= MinZ() + Size() - margin;
    }
    void Clear() {
        std::fill(h_.begin(), h_.end(), 0.0f); std::fill(v_.begin(), v_.end(), 0.0f);
        sleeping_ = true; quietSteps_ = 0; acc_ = 0;
    }
    bool Sleeping() const { return sleeping_; }

    // Moves the square so (x, z) is in its middle. Cells that stay inside keep their state; new ones start calm.
    void Recenter(float x, float z) {
        const int nx = static_cast<int>(std::floor(x / cell_)) - kN / 2, nz = static_cast<int>(std::floor(z / cell_)) - kN / 2;
        if (nx == ox_ && nz == oz_ && placed_) return;
        const int dx = nx - ox_, dz = nz - oz_;
        if (!placed_ || std::abs(dx) >= kN || std::abs(dz) >= kN) {
            Clear();
        } else {
            if (!sleeping_) { Shift(h_, dx, dz); Shift(v_, dx, dz); }
        }
        ox_ = nx; oz_ = nz; placed_ = true;
    }

    // Pushes the surface down by `depth` units in a soft disc of `radius` (negative depth lifts it).
    void Impulse(float x, float z, float depth, float radius) {
        if (!placed_) return;
        radius = std::max(radius, cell_ * 0.75f);
        const float fx = x / cell_ - ox_, fz = z / cell_ - oz_, rc = radius / cell_;
        const int i0 = std::max(1, static_cast<int>(std::floor(fx - rc * 2.0f))), i1 = std::min(kN - 2, static_cast<int>(std::ceil(fx + rc * 2.0f)));
        const int j0 = std::max(1, static_cast<int>(std::floor(fz - rc * 2.0f))), j1 = std::min(kN - 2, static_cast<int>(std::ceil(fz + rc * 2.0f)));
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++) {
                const float d2 = ((i - fx) * (i - fx) + (j - fz) * (j - fz)) / (rc * rc);
                if (d2 < 4.0f && std::fabs(depth) > 1e-6f) {
                    h_[j * kN + i] -= depth * std::exp(-d2 * 1.5f);
                    sleeping_ = false; quietSteps_ = 0;
                }
            }
    }

    // Advances the field. Fixed small steps keep it stable whatever the frame rate.
    void Step(float dt) {
        if (sleeping_) { acc_ = 0; return; }
        acc_ += std::clamp(dt, 0.0f, 0.1f);
        constexpr float kStep = 1.0f / 60.0f;
        int steps = 0;
        while (!sleeping_ && acc_ >= kStep && steps < 6) { acc_ -= kStep; StepOnce(kStep); steps++; }
        if (steps == 6) acc_ = 0.0f;
    }

    // Height here (0 outside the square), smoothly faded out toward its rim.
    float Sample(float x, float z) const {
        if (!placed_) return 0.0f;
        const float fx = x / cell_ - ox_, fz = z / cell_ - oz_;
        if (fx < 0.0f || fz < 0.0f || fx > kN - 1.001f || fz > kN - 1.001f) return 0.0f;
        const int i = static_cast<int>(fx), j = static_cast<int>(fz);
        const float u = fx - i, w = fz - j;
        const float a = h_[j * kN + i], b = h_[j * kN + i + 1], c = h_[(j + 1) * kN + i], d = h_[(j + 1) * kN + i + 1];
        const float edge = std::min(std::min(fx, fz), std::min(kN - 1 - fx, kN - 1 - fz));
        const float fade = std::clamp((edge - 1.0f) / kSponge, 0.0f, 1.0f);
        return ((a * (1 - u) + b * u) * (1 - w) + (c * (1 - u) + d * u) * w) * fade;
    }
    // Slope (dh/dx, dh/dz) here.
    void Slope(float x, float z, float* sx, float* sz) const {
        const float e = cell_;
        *sx = (Sample(x + e, z) - Sample(x - e, z)) / (2.0f * e);
        *sz = (Sample(x, z + e) - Sample(x, z - e)) / (2.0f * e);
    }
    // The biggest height in the field (how lively it is).
    float Activity() const {
        float m = 0.0f;
        for (float v : h_) m = std::max(m, std::fabs(v));
        return m;
    }
    float Energy() const {
        double e = 0.0;
        for (int k = 0; k < kN * kN; k++) e += static_cast<double>(h_[k]) * h_[k] + static_cast<double>(v_[k]) * v_[k] / (speed_ * speed_ / (cell_ * cell_));
        return static_cast<float>(e);
    }

private:
    void Shift(std::vector<float>& a, int dx, int dz) {
        std::fill(tmp_.begin(), tmp_.end(), 0.0f);
        for (int j = 0; j < kN; j++) {
            const int sj = j + dz;
            if (sj < 0 || sj >= kN) continue;
            for (int i = 0; i < kN; i++) {
                const int si = i + dx;
                if (si >= 0 && si < kN) tmp_[j * kN + i] = a[sj * kN + si];
            }
        }
        a.swap(tmp_);
    }
    void StepOnce(float dt) {
        const float c2 = speed_ * speed_ / (cell_ * cell_);
        for (int j = 1; j < kN - 1; j++)
            for (int i = 1; i < kN - 1; i++) {
                const int k = j * kN + i;
                const float lap = h_[k - 1] + h_[k + 1] + h_[k - kN] + h_[k + kN] - 4.0f * h_[k];
                v_[k] += lap * c2 * dt;
            }
        float peakH = 0, peakV = 0;
        for (int j = 0; j < kN; j++)
            for (int i = 0; i < kN; i++) {
                const int k = j * kN + i;
                const int edge = std::min(std::min(i, j), std::min(kN - 1 - i, kN - 1 - j));
                const float sponge = sponge_[std::min(edge, kSponge)];
                v_[k] *= keep_ * sponge;
                h_[k] = (h_[k] + v_[k] * dt) * sponge;
                peakH = std::max(peakH, std::fabs(h_[k])); peakV = std::max(peakV, std::fabs(v_[k]));
            }
        if (peakH < 0.001f && peakV < 0.001f) ++quietSteps_; else quietSteps_ = 0;
        if (quietSteps_ >= 30) { Clear(); }
    }

    float keep_ = 1, sponge_[kSponge+1] = {};
    bool sleeping_ = true;
    int quietSteps_ = 0;
    float cell_, speed_, damping_;
    int ox_ = 0, oz_ = 0;
    bool placed_ = false;
    float acc_ = 0.0f;
    std::vector<float> h_, v_, tmp_;
};

// ---- textures ----------------------------------------------------------------------------------------------------------------------------
// A wavy surface that tiles across the texture and loops in time: a sum of waves with whole-number wave vectors (so they repeat across the tile)
// and whole-number turns per loop (so the last frame runs into the first).
struct TileWave { int kx, ky, turns; float amp, phase; };

inline uint32_t Hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float Hash01(uint32_t a, uint32_t b, uint32_t c) { return (Hash32(a * 73856093U ^ Hash32(b * 19349663U ^ Hash32(c * 83492791U))) & 0xFFFFFF) / 16777216.0f; }

inline std::vector<TileWave> MakeTileWaves(uint32_t seed, int count, int kMin, int kMax) {
    std::vector<TileWave> out;
    for (int n = 0; out.size() < static_cast<size_t>(count) && n < count * 40; n++) {
        const int span = kMax * 2 + 1;
        const int kx = static_cast<int>(Hash01(seed, n, 1) * span) - kMax, ky = static_cast<int>(Hash01(seed, n, 2) * span) - kMax;
        const float m = std::sqrt(static_cast<float>(kx * kx + ky * ky));
        if (m < kMin || m > kMax) continue;
        int turns = 1 + static_cast<int>(Hash01(seed, n, 3) * 2.0f);
        if (Hash01(seed, n, 4) < 0.5f) turns = -turns;
        out.push_back({ kx, ky, turns, 1.0f / m, Hash01(seed, n, 5) * kTau });
    }
    return out;
}

// Height and gradient at (u, v) in tile units (0..1) at loop time s (0..1).
inline void TileWaveAt(const std::vector<TileWave>& waves, float u, float v, float s, float* h, float* gu, float* gv) {
    *h = *gu = *gv = 0.0f;
    for (const TileWave& w : waves) {
        const float th = kTau * (w.kx * u + w.ky * v + w.turns * s) + w.phase;
        const float sn = std::sin(th), cs = std::cos(th);
        *h += w.amp * sn;
        *gu += w.amp * cs * kTau * w.kx;
        *gv += w.amp * cs * kTau * w.ky;
    }
}

inline void BlurWrap(std::vector<float>& img, int size) {
    std::vector<float> out(img.size());
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float s = 0.0f;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const float wgt = (dx == 0 ? 2.0f : 1.0f) * (dy == 0 ? 2.0f : 1.0f);
                    s += wgt * img[((y + dy + size) % size) * size + (x + dx + size) % size];
                }
            out[y * size + x] = s / 16.0f;
        }
    img.swap(out);
}

// Caustics: a grid of light rays falls through the wavy surface and bends (a small-angle Snell's law: the ray shifts along the slope); where
// many land together the floor is bright. `out` is frames x size x size bytes.
// `kMin`..`kMax` is how many ripples cross the tile, `floor` how bright the floor must be lit to show, `soft` whether it is blurred.
inline void MakeCaustics(uint8_t* out, int size, int frames, uint32_t seed = 11, int kMin = 2, int kMax = 6, float floor = 0.75f, bool soft = true) {
    const std::vector<TileWave> waves = MakeTileWaves(seed, 14, kMin, kMax);
    const int rays = size * 3;
    std::vector<float> acc(static_cast<size_t>(size) * size);
    for (int f = 0; f < frames; f++) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        const float s = static_cast<float>(f) / frames;
        for (int ry = 0; ry < rays; ry++)
            for (int rx = 0; rx < rays; rx++) {
                const float u = (rx + 0.5f) / rays, v = (ry + 0.5f) / rays;
                float h, gu, gv;
                TileWaveAt(waves, u, v, s, &h, &gu, &gv);
                constexpr float kBend = 0.0042f;   // how far a ray moves per unit of slope: sharper focus the larger it is
                float px = (u + gu * kBend) * size - 0.5f, py = (v + gv * kBend) * size - 0.5f;
                const float fx = std::floor(px), fy = std::floor(py), ax = px - fx, ay = py - fy;
                const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                auto put = [&](int x, int y, float w) { acc[((y % size + size) % size) * size + (x % size + size) % size] += w; };
                put(ix, iy, (1 - ax) * (1 - ay)); put(ix + 1, iy, ax * (1 - ay)); put(ix, iy + 1, (1 - ax) * ay); put(ix + 1, iy + 1, ax * ay);
            }
        if (soft) BlurWrap(acc, size);
        const float mean = static_cast<float>(rays) * rays / (static_cast<float>(size) * size);
        for (int k = 0; k < size * size; k++) {
            const float c = acc[k] / mean;   // 1 is an evenly lit floor
            const float lit = std::clamp((c - floor) / (1.15f + floor), 0.0f, 1.0f);
            out[static_cast<size_t>(f) * size * size + k] = static_cast<uint8_t>(std::lround(std::pow(lit, 0.8f) * 255.0f));
        }
    }
}

// Glints: the sun and the sky caught on small ripples: a thin, broken, sparkling net of light (the bright squiggles of the Zelda games' water).
// It is the same light-focusing as the caustics, on finer ripples, and only the brightest part of it.
inline void MakeGlints(uint8_t* out, int size, int frames, uint32_t seed = 23) { MakeCaustics(out, size, frames, seed, 3, 7, 1.25f, false); }

// Foam: lacy cells (the walls between the points of a tiling cellular pattern), in clumps. One frame; it is scrolled and faded instead.
inline void MakeFoam(uint8_t* out, int size, uint32_t seed = 37) {
    constexpr int kCells = 5;
    auto feature = [&](int cx, int cy, float* fx, float* fy) {
        const int wx = (cx % kCells + kCells) % kCells, wy = (cy % kCells + kCells) % kCells;
        *fx = cx + Hash01(seed, wx, wy * 2 + 1);
        *fy = cy + Hash01(seed, wx, wy * 2 + 2);
    };
    const std::vector<TileWave> clump = MakeTileWaves(seed + 1, 6, 1, 3);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const float px = (x + 0.5f) / size * kCells, py = (y + 0.5f) / size * kCells;
            float d1 = 1e9f, d2 = 1e9f;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int cx = static_cast<int>(std::floor(px)) + dx, cy = static_cast<int>(std::floor(py)) + dy;
                    float fx, fy;
                    feature(cx, cy, &fx, &fy);
                    const float d = std::hypot(px - fx, py - fy);
                    if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
                }
            const float edge = 1.0f - std::clamp((d2 - d1) / 0.3f, 0.0f, 1.0f);   // bright on the walls between cells
            float h, gu, gv;
            TileWaveAt(clump, (x + 0.5f) / size, (y + 0.5f) / size, 0.0f, &h, &gu, &gv);
            const float patch = std::clamp(0.62f + h * 0.55f, 0.25f, 1.0f);
            out[y * size + x] = static_cast<uint8_t>(std::lround(std::clamp((edge * edge * 0.75f + edge * 0.25f) * patch, 0.0f, 1.0f) * 255.0f));
        }
}

// How well a texture tiles: the mean jump across its wrap seam over the largest mean jump between two neighbouring columns inside it (at most
// about 1 when seamless; a texture that does not tile has a seam far above any inner column).
inline float SeamRatio(const uint8_t* img, int size) {
    double worst = 0.0, seam = 0.0;
    for (int x = 0; x + 1 < size; x++) {
        double col = 0.0;
        for (int y = 0; y < size; y++) col += std::abs(img[y * size + x + 1] - img[y * size + x]);
        worst = std::max(worst, col / size);
    }
    for (int y = 0; y < size; y++) seam += std::abs(img[y * size] - img[y * size + size - 1]);
    seam /= size;
    return worst > 0.0 ? static_cast<float>(seam / worst) : 0.0f;
}

}  // namespace royale::water
