#pragma once
// Cloth and wind, as far as a hat and a glider need it. Pure maths with no game types, so the tests can check it.
//   Sheet / GliderCloth: the glider's canopy as a grid of points joined by springs (position-based, "Verlet"). The leading edge and the
//     centre keel are fixed to the frame; the rest of the sheet is free, so it billows when air pushes up from below, flutters at the
//     trailing edge and ripples in gusts. The airflow is the glider's own movement through the air plus the wind.
//   HatSpring: the tail of Link's cap, a damped spring in two directions (fore-aft and sideways) pushed by the same airflow.
//   ClothSwing: the same idea for the heavier clothes (the tunic's skirt, the sheath on its strap): a pendulum that lags Link's movement,
//     flaps with his stride, streams away from the air and flutters in the wind, tuned per piece with a SwingTune.
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
    // `fore` and `side` are what the cap's limb is turned by. A springy base (underdamped, so it overshoots and swings) is followed by a second,
    // looser spring for the floppy tip, which whips a little beyond the base when the base moves and settles after it.
    float fore = 0, side = 0, vFore = 0, vSide = 0;
    float baseFore = 0, baseSide = 0, tipFore = 0, tipSide = 0, vTipFore = 0, vTipSide = 0;
    // kickFore/kickSide: a sudden push from Link's own acceleration (starting, stopping, landing, a hit), added to the spring's speed.
    void Step(float dt, float airX, float airZ, float airY, float wind01, float time, float phase, float kickFore = 0.0f, float kickSide = 0.0f) {
        const float k = 34.0f, damp = 3.6f;
        float targetFore = std::clamp(-airZ * 0.0016f - airY * 0.0007f, -0.55f, 0.55f);    // forward speed and falling both lift the tail up and back
        float targetSide = std::clamp(-airX * 0.0016f, -0.5f, 0.5f);
        const float flutter = (0.04f + 0.22f * wind01) * (0.4f + (std::min)(1.0f, std::sqrt(airX * airX + airZ * airZ) / 300.0f));
        targetFore += std::sin(time * 9.0f + phase) * flutter + std::sin(time * 14.3f + phase * 0.6f) * flutter * 0.35f;
        targetSide += std::sin(time * 6.3f + phase * 1.7f) * flutter * 0.8f;
        if (std::isfinite(kickFore)) vFore += std::clamp(kickFore, -6.0f, 6.0f);
        if (std::isfinite(kickSide)) vSide += std::clamp(kickSide, -6.0f, 6.0f);
        const float steps = (std::max)(1.0f, std::floor(dt / (1.0f / 120.0f) + 0.5f));
        const float h = dt / steps;
        for (int s = 0; s < static_cast<int>((std::min)(steps, 6.0f)); s++) {
            vFore += ((targetFore - baseFore) * k - vFore * damp) * h;
            vSide += ((targetSide - baseSide) * k - vSide * damp) * h;
            baseFore += vFore * h;
            baseSide += vSide * h;
            vTipFore += ((baseFore - tipFore) * 70.0f - vTipFore * 3.2f) * h;   // the tip chases the base, loosely
            vTipSide += ((baseSide - tipSide) * 70.0f - vTipSide * 3.2f) * h;
            tipFore += vTipFore * h;
            tipSide += vTipSide * h;
        }
        baseFore = std::clamp(baseFore, -0.7f, 0.7f);
        baseSide = std::clamp(baseSide, -0.6f, 0.6f);
        if (!std::isfinite(baseFore) || !std::isfinite(baseSide) || !std::isfinite(tipFore) || !std::isfinite(tipSide)) { baseFore = baseSide = tipFore = tipSide = vFore = vSide = vTipFore = vTipSide = 0; }
        fore = std::clamp(baseFore + 0.5f * (baseFore - tipFore), -0.7f, 0.7f);
        side = std::clamp(baseSide + 0.5f * (baseSide - tipSide), -0.6f, 0.6f);
    }
};

// How one piece of clothing swings. Angles are radians of the piece's limb; fore is positive when the piece trails back (air from in front).
struct SwingTune {
    float k, damp;            // the spring (how fast it swings back) and how quickly a swing dies away
    float airGain;            // radians per (unit per second) of air streaming past
    float maxFore, maxSide;   // never swings further than this
    float flutter;            // wind flutter at full gale
    float stride;             // flap per (unit per second) of running speed, at the stride's rhythm
    float kick;               // how much a sudden start, stop or landing throws it
};
// The tunic's skirt: heavy cloth, swings a little and slowly. The sheath: a stiff thing on a strap, a smaller, quicker bob.
constexpr SwingTune kSkirtSwing = {22.0f, 4.2f, 0.00055f, 0.2f, 0.16f, 0.06f, 0.00016f, 0.0026f};
constexpr SwingTune kSheathSwing = {38.0f, 5.5f, 0.0004f, 0.22f, 0.18f, 0.04f, 0.00022f, 0.0032f};

struct ClothSwing {
    float fore = 0, side = 0;                         // what the limb is turned by
    float baseFore = 0, baseSide = 0, vFore = 0, vSide = 0, stridePhase = 0, flapAmp = 0, flutterAmp = 0;
    // `air` in Link's own frame (x to his left, z in front of him), `speed` how fast he moves over the ground, `fall` how fast he drops
    // (units per second, positive falling), `accFore`/`accSide` his change of velocity this step in his frame.
    // A spring carries the slow part (trailing in the airflow, lagging a start, swinging past a stop); the stride's flap and the wind's
    // flutter ride on top of it, so they stay lively instead of being smoothed away by the heavy spring.
    void Step(const SwingTune& t, float dt, float airX, float airZ, float speed, float fall, float wind01, float time, float phase,
              float accFore = 0.0f, float accSide = 0.0f) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        dt = (std::min)(dt, 0.1f);
        auto safe = [](float v, float lim) { return std::isfinite(v) ? std::clamp(v, -lim, lim) : 0.0f; };
        airX = safe(airX, 2000.0f); airZ = safe(airZ, 2000.0f); speed = std::fabs(safe(speed, 2000.0f)); fall = safe(fall, 3000.0f);
        accFore = safe(accFore, 3000.0f); accSide = safe(accSide, 3000.0f); wind01 = std::clamp(std::isfinite(wind01) ? wind01 : 0.0f, 0.0f, 1.0f);
        if (!std::isfinite(time)) time = 0.0f;
        time = std::fmod(time, 10000.0f);
        const float targetFore = std::clamp(-airZ * t.airGain, -t.maxFore, t.maxFore);
        const float targetSide = std::clamp(-airX * t.airGain, -t.maxSide, t.maxSide);
        // His own acceleration throws the cloth the other way: it lags a start and swings on past a stop.
        vFore += std::clamp(accFore * t.kick, -4.0f, 4.0f);
        vSide += std::clamp(accSide * t.kick, -4.0f, 4.0f);
        const int steps = (std::max)(1, (std::min)(6, static_cast<int>(dt / (1.0f / 120.0f) + 0.5f)));
        const float h = dt / steps;
        for (int s = 0; s < steps; s++) {
            vFore += ((targetFore - baseFore) * t.k - vFore * t.damp) * h;
            vSide += ((targetSide - baseSide) * t.k - vSide * t.damp) * h;
            baseFore += vFore * h;
            baseSide += vSide * h;
        }
        if (!std::isfinite(baseFore) || !std::isfinite(baseSide) || !std::isfinite(vFore) || !std::isfinite(vSide)) baseFore = baseSide = vFore = vSide = 0;
        const float limF = t.maxFore * 1.2f, limS = t.maxSide * 1.2f;   // a hard throw may overshoot the usual reach a little, never more
        if (baseFore > limF || baseFore < -limF) { baseFore = std::clamp(baseFore, -limF, limF); vFore = 0; }
        if (baseSide > limS || baseSide < -limS) { baseSide = std::clamp(baseSide, -limS, limS); vSide = 0; }
        // Stride: about two and a half steps a second at a walk, quicker at a run. The cloth rocks side to side once a stride and flaps fore
        // and aft with each step. The size eases in and out so starting and stopping don't jump.
        stridePhase = std::fmod(stridePhase + dt * (5.0f + (std::min)(speed, 600.0f) * 0.022f), 6.2831853f);
        const float ease = (std::min)(1.0f, dt * 6.0f);
        flapAmp += ((std::min)(speed, 600.0f) * t.stride - flapAmp) * ease;
        // Wind flutter, and falling (a jump, the skydive) makes the cloth stream and snap hard.
        const float fallFlutter = std::clamp(fall / 900.0f, 0.0f, 1.0f);
        flutterAmp += (((0.15f + 0.85f * wind01) + fallFlutter * 1.4f) * t.flutter - flutterAmp) * ease;
        const float flutterFore = (std::sin(time * 8.3f + phase) + 0.4f * std::sin(time * 15.1f + phase * 0.7f)) * flutterAmp;
        const float flutterSide = std::sin(time * 6.7f + phase * 1.9f) * flutterAmp * 0.8f;
        fore = std::clamp(baseFore + std::sin(stridePhase * 2.0f) * flapAmp * 0.6f + flutterFore, -t.maxFore * 1.5f, t.maxFore * 1.5f);
        side = std::clamp(baseSide + std::sin(stridePhase) * flapAmp + flutterSide, -t.maxSide * 1.5f, t.maxSide * 1.5f);
    }
};

} // namespace royale
