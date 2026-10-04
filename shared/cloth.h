#pragma once
// Cloth and wind, as far as a hat and a glider need it. Pure maths with no game types, so the tests can check it.
//   Sheet / GliderCloth: the glider's canopy as a grid of points joined by springs (position-based, "Verlet"). The leading edge and the
//     centre keel are fixed to the frame; the rest of the sheet is free, so it billows when air pushes up from below, flutters at the
//     trailing edge and ripples in gusts. The airflow is the glider's own movement through the air plus the wind.
//   HatSpring: the tail of Link's cap, a damped spring in two directions (fore-aft and sideways) pushed by the same airflow.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace royale {

struct ClothV3 { float x = 0, y = 0, z = 0; };
inline ClothV3 operator+(ClothV3 a, ClothV3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline ClothV3 operator-(ClothV3 a, ClothV3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline ClothV3 operator*(ClothV3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(ClothV3 a, ClothV3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline ClothV3 Cross(ClothV3 a, ClothV3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(ClothV3 a) { return std::sqrt(Dot(a, a)); }

constexpr int kClothChord = 4, kClothSpan = 6;      // cells across and along one wing (the span's three stripes are two cells each)

class Sheet {
  public:
    // The wing: its leading edge runs from the nose to the tip, its trailing edge from the tail to the tip.
    void Init(ClothV3 nose, ClothV3 tail, ClothV3 tip) {
        const int n = (kClothChord + 1) * (kClothSpan + 1);
        rest.assign(static_cast<size_t>(n), {});
        for (int j = 0; j <= kClothSpan; j++) {
            const float t = static_cast<float>(j) / kClothSpan;
            const ClothV3 lead = nose + (tip - nose) * t, trail = tail + (tip - tail) * t;
            for (int i = 0; i <= kClothChord; i++) rest[Index(i, j)] = lead + (trail - lead) * (static_cast<float>(i) / kClothChord);
        }
        p = rest; prev = rest;
        pin.assign(static_cast<size_t>(n), 0);
        for (int j = 0; j <= kClothSpan; j++) for (int i = 0; i <= kClothChord; i++) pin[Index(i, j)] = (i == 0 || j == 0) ? 1 : 0;
    }
    static int Index(int i, int j) { return j * (kClothChord + 1) + i; }
    const ClothV3& At(int i, int j) const { return p[static_cast<size_t>(Index(i, j))]; }
    const ClothV3& Rest(int i, int j) const { return rest[static_cast<size_t>(Index(i, j))]; }

    // One step. `air` is the air moving past the sheet (local units per second); `wind01` how strong and gusty the weather is (0 to 1).
    void Step(float dt, ClothV3 air, float wind01, float time, float phase) {
        const float dt2 = dt * dt;
        for (int j = 0; j <= kClothSpan; j++) for (int i = 0; i <= kClothChord; i++) {
            const size_t k = static_cast<size_t>(Index(i, j));
            if (pin[k]) { p[k] = rest[k]; prev[k] = rest[k]; continue; }
            const float u = static_cast<float>(i) / kClothChord, t = static_cast<float>(j) / kClothSpan;
            // The membrane's normal from its neighbours (the way the sheet currently faces), pointing up for a flat wing.
            const ClothV3 a = At((std::min)(i + 1, kClothChord), j) - At((std::max)(i - 1, 0), j);
            const ClothV3 b = At(i, (std::min)(j + 1, kClothSpan)) - At(i, (std::max)(j - 1, 0));
            ClothV3 n = Cross(b, a);
            const float nl = Len(n);
            n = nl > 1e-3f ? n * (1.0f / nl) : ClothV3{0, 1, 0};
            if (n.y < 0) n = n * -1.0f;
            const float push = Dot(n, air);                                   // air pressing on the sheet, either side
            ClothV3 acc = {0, -140.0f, 0};                                    // weight
            acc = acc + n * (push * std::fabs(push) * 0.0009f);               // pressure: air from below fills it, air from above flattens it
            // Gusts and flutter, strongest at the trailing edge and out at the tip.
            const float gust = (28.0f + 150.0f * wind01 + 0.06f * Len(air)) * (0.25f + u) * (0.4f + t * 0.8f);
            acc.y += std::sin(time * 8.0f + i * 1.7f + j * 1.1f + phase) * gust;
            acc.x += std::sin(time * 5.3f + j * 2.1f + phase * 1.3f) * gust * 0.35f;
            acc.z += std::cos(time * 6.1f + i * 1.9f + phase) * gust * 0.35f;
            acc = acc + (rest[k] - p[k]) * 9.0f;                              // the cloth wants to go back to its cut shape (and the lines hold it)
            const ClothV3 cur = p[k];
            p[k] = cur + (cur - prev[k]) * 0.95f + acc * dt2;
            prev[k] = cur;
        }
        for (int iter = 0; iter < 3; iter++) {
            for (int j = 0; j <= kClothSpan; j++) for (int i = 0; i <= kClothChord; i++) {
                if (i < kClothChord) Constrain(Index(i, j), Index(i + 1, j));
                if (j < kClothSpan) Constrain(Index(i, j), Index(i, j + 1));
                if (i < kClothChord && j < kClothSpan) { Constrain(Index(i, j), Index(i + 1, j + 1)); Constrain(Index(i + 1, j), Index(i, j + 1)); }
            }
        }
        for (size_t k = 0; k < p.size(); k++) {   // never run away, whatever the input
            const ClothV3 d = p[k] - rest[k];
            const float l = Len(d);
            if (!(l < 90.0f)) { p[k] = rest[k] + (l > 0 && std::isfinite(l) ? d * (90.0f / l) : ClothV3{}); prev[k] = p[k]; }
        }
    }

    std::vector<ClothV3> rest, p, prev;
    std::vector<uint8_t> pin;

  private:
    void Constrain(int a, int b) {
        const size_t ia = static_cast<size_t>(a), ib = static_cast<size_t>(b);
        const float want = Len(rest[ia] - rest[ib]);
        if (want < 1.0f) return;                                   // the corner at the tip, where the edges meet
        ClothV3 d = p[ib] - p[ia];
        const float l = Len(d);
        if (l < 1e-4f) return;
        const float diff = (l - want) / l;
        const float wa = pin[ia] ? 0.0f : 1.0f, wb = pin[ib] ? 0.0f : 1.0f;
        if (wa + wb == 0.0f) return;
        d = d * (diff / (wa + wb));
        p[ia] = p[ia] + d * wa;
        p[ib] = p[ib] - d * wb;
    }
};

struct ClothVertex { float x, y, z; uint8_t r, g, b; };

class GliderCloth {
  public:
    GliderCloth() {
        const ClothV3 nose = {0, 156, 78}, tail = {0, 150, -64};
        left.Init(nose, tail, {-118, 140, -58});
        right.Init(nose, tail, {118, 140, -58});
    }
    void Update(float dt, ClothV3 air, float wind01, float time, uint32_t seed) {
        const float phase = static_cast<float>(seed % 97) * 0.37f;
        // Fixed small steps keep the springs stable however long the frame was; a long stall is not simulated in full.
        int steps = static_cast<int>(dt / (1.0f / 60.0f) + 0.5f);
        steps = (std::max)(1, (std::min)(steps, 4));
        for (int s = 0; s < steps; s++) {
            left.Step(1.0f / 60.0f, air, wind01, time + s / 60.0f, phase);
            right.Step(1.0f / 60.0f, ClothV3{-air.x, air.y, air.z}, wind01, time + s / 60.0f, phase + 1.0f);   // the mirror image gets the mirrored push
        }
    }
    // Triangles for both wings, top and underside, stripes alternating along the span, shaded by how each little patch faces the light.
    void Build(std::vector<ClothVertex>& out, const uint8_t colA[3], const uint8_t colB[3]) const {
        out.clear();
        const ClothV3 light = {0.35f, 0.85f, 0.4f};
        for (int side = 0; side < 2; side++) {
            const Sheet& s = side == 0 ? left : right;
            for (int j = 0; j < kClothSpan; j++) for (int i = 0; i < kClothChord; i++) {
                const uint8_t* c = (j / 2) % 2 == 0 ? colA : colB;
                const ClothV3 q[4] = {s.At(i, j), s.At(i + 1, j), s.At(i + 1, j + 1), s.At(i, j + 1)};
                ClothV3 n = Cross(q[1] - q[0], q[3] - q[0]);
                const float nl = Len(n);
                n = nl > 1e-4f ? n * (1.0f / nl) : ClothV3{0, 1, 0};
                if (n.y < 0) n = n * -1.0f;
                const float shade = 0.7f + 0.3f * (std::max)(0.0f, Dot(n, light));
                const uint8_t top[3] = {static_cast<uint8_t>(c[0] * shade), static_cast<uint8_t>(c[1] * shade), static_cast<uint8_t>(c[2] * shade)};
                const uint8_t under[3] = {static_cast<uint8_t>(c[0] * 0.62f), static_cast<uint8_t>(c[1] * 0.62f), static_cast<uint8_t>(c[2] * 0.62f)};
                const int order[2][3] = {{0, 1, 2}, {0, 2, 3}};
                for (const auto& tri : order) {
                    for (int k : tri) out.push_back({q[k].x, q[k].y, q[k].z, top[0], top[1], top[2]});
                    for (int k : {tri[0], tri[2], tri[1]}) out.push_back({q[k].x, q[k].y - 7.0f, q[k].z, under[0], under[1], under[2]});   // underside, wound the other way
                }
            }
        }
    }
    Sheet left, right;
};

// The tail of Link's cap: a spring in two directions. `air` is the airflow in Link's own frame (x to his left, z in front of him), `wind01` the
// weather. The tail streams away from the airflow, sags back when it is still, and swings and settles when he stops or turns.
struct HatSpring {
    float fore = 0, side = 0, vFore = 0, vSide = 0;
    void Step(float dt, float airX, float airZ, float airY, float wind01, float time, float phase) {
        const float k = 38.0f, damp = 5.2f;
        float targetFore = std::clamp(-airZ * 0.0016f - airY * 0.0007f, -0.55f, 0.55f);    // forward speed and falling both lift the tail up and back
        float targetSide = std::clamp(-airX * 0.0016f, -0.5f, 0.5f);
        const float flutter = (0.04f + 0.22f * wind01) * (0.4f + (std::min)(1.0f, std::sqrt(airX * airX + airZ * airZ) / 300.0f));
        targetFore += std::sin(time * 9.0f + phase) * flutter;
        targetSide += std::sin(time * 6.3f + phase * 1.7f) * flutter * 0.8f;
        const float steps = (std::max)(1.0f, std::floor(dt / (1.0f / 120.0f) + 0.5f));
        const float h = dt / steps;
        for (int s = 0; s < static_cast<int>((std::min)(steps, 6.0f)); s++) {
            vFore += ((targetFore - fore) * k - vFore * damp) * h;
            vSide += ((targetSide - side) * k - vSide * damp) * h;
            fore += vFore * h;
            side += vSide * h;
        }
        fore = std::clamp(fore, -0.7f, 0.7f);
        side = std::clamp(side, -0.6f, 0.6f);
        if (!std::isfinite(fore) || !std::isfinite(side)) { fore = side = vFore = vSide = 0; }
    }
};

} // namespace royale
