#include "dynamic_shadows.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <limits>
using namespace royale::shadows;
void require(bool ok, const char* what) { if (!ok) { std::cerr << "shadow test failed: " << what << "\n"; std::exit(1); } }

// The darkest texel of a map and where it is (in world x, z).
struct Peak { int value = 0; float x = 0, z = 0; };
Peak PeakOf(const MapFrame& f, const std::vector<uint8_t>& m, int size) {
    Peak p;
    const float texel = 2.0f * f.half / size;
    for (int iz = 0; iz < size; iz++)
        for (int ix = 0; ix < size; ix++)
            if (m[iz * size + ix] > p.value) { p.value = m[iz * size + ix]; p.x = f.cx - f.half + (ix + 0.5f) * texel; p.z = f.cz - f.half + (iz + 0.5f) * texel; }
    return p;
}
int Coverage(const std::vector<uint8_t>& m, int threshold) {
    int n = 0;
    for (uint8_t v : m) n += v >= threshold;
    return n;
}

int main() {
    // Quality presets: off draws nothing, each step up costs more, and the default for handhelds stays small.
    require(Preset(Quality::Off).maxMaps == 0 && Preset(Quality::Off).maxLights == 0, "off is off");
    for (int q = 1; q + 1 < static_cast<int>(Quality::Count); q++) {
        const Settings a = Preset(static_cast<Quality>(q)), b = Preset(static_cast<Quality>(q + 1));
        require(b.maxMaps >= a.maxMaps && b.mapSize >= a.mapSize && b.range >= a.range && b.grid >= a.grid && b.maxLights >= a.maxLights, "quality steps up");
    }
    for (int q = 0; q < static_cast<int>(Quality::Count); q++) {
        const Settings s = Preset(static_cast<Quality>(q));
        require(s.mapSize <= kMaxMapSize && s.maxMaps <= kMaxMaps && s.grid <= kMaxGrid && s.maxLights <= kMaxLights, "presets fit the fixed buffers");
        require(s.mapSize == 16 || s.mapSize == 32 || s.mapSize == 64, "map sizes are powers of two the texture loader takes");
    }
    require(Preset(Quality::Low).mapSize * Preset(Quality::Low).mapSize * Preset(Quality::Low).maxMaps <= 8 * 1024, "low quality paints at most 8K pixels a frame");

    // The sun: at noon straight down, low suns lifted to the minimum height, the moon opposite and fainter, overcast and the horizon wash them out.
    const ShadowLight noon = SunShadow(1.0f, 0.0f);
    require(noon.toward.y > 0.99f && noon.strength > 0.5f, "noon sun overhead and strong");
    const ShadowLight low = SunShadow(0.05f, 0.0f);
    require(std::fabs(low.toward.y - kMinElevation) < 1e-3f && std::fabs(Len(low.toward) - 1.0f) < 1e-3f, "a low sun is lifted, still unit length");
    require(low.strength < noon.strength, "shadows fade as the sun sets");
    require(SunShadow(0.0f, 0.0f).strength == 0.0f, "no shadow with the sun on the horizon");
    const ShadowLight moon = SunShadow(-0.8f, 0.0f), morning = SunShadow(0.8f, 0.0f);
    require(moon.strength > 0.0f && moon.strength < morning.strength, "moonlight gives fainter shadows");
    require(moon.toward.x * morning.toward.x < 0.0f && moon.toward.z * morning.toward.z < 0.0f, "the moon is opposite the sun");
    require(SunShadow(0.8f, 1.0f).strength < 0.5f * morning.strength, "overcast washes shadows out");

    // A point light: shadows point away from it and fade with distance, and nothing past its reach.
    const ShadowLight pl = PointShadow({ 100, 50, 0 }, 300, 1.0f, { 0, 0, 0 });
    require(pl.toward.x > 0.5f && pl.strength > 0.0f, "light to the east: toward points east");
    require(PointShadow({ 100, 50, 0 }, 300, 1.0f, { 50, 0, 0 }).strength > pl.strength, "closer is darker");
    require(PointShadow({ 500, 0, 0 }, 300, 1.0f, { 0, 0, 0 }).strength == 0.0f, "out of reach: no shadow");
    require(PointShadow({ 100, -40, 0 }, 300, 1.0f, { 0, 0, 0 }).toward.y >= kMinElevation - 1e-4f, "a light below is lifted");

    // A ball 40 units up under a noon sun: its shadow is a round spot right below it, sharp-ish, nothing at the border.
    const int N = 64;
    std::vector<uint8_t> map(N * N);
    {
        Capsule ball{ { 10, 40, -5 }, { 10, 40, -5 }, 10 };
        const MapFrame f = PlanMap(&ball, 1, noon.toward, 0.0f, 1.0f);
        require(std::fabs(f.cx - 10) < 2 && std::fabs(f.cz + 5) < 2, "noon map centred under the ball");
        Rasterize(f, &ball, 1, 1.0f, map.data(), N);
        const Peak p = PeakOf(f, map, N);
        require(p.value > 200 && std::hypot(p.x - 10, p.z + 5) < 4, "noon shadow is dark and right below");
        bool edgeClear = true;
        for (int i = 0; i < N; i++) edgeClear = edgeClear && !map[i] && !map[(N - 1) * N + i] && !map[i * N] && !map[i * N + N - 1];
        require(edgeClear, "the border of the map stays clear");
    }
    // The same ball with a low eastern sun: the shadow moves west of the ball and stretches along the light.
    {
        Capsule ball{ { 0, 40, 0 }, { 0, 40, 0 }, 10 };
        const MapFrame f = PlanMap(&ball, 1, Norm({ 0.8f, 0.6f, 0 }), 0.0f, 1.0f);
        Rasterize(f, &ball, 1, 1.0f, map.data(), N);
        const Peak p = PeakOf(f, map, N);
        require(p.x < -40 && std::fabs(p.z) < 5, "shadow lands away from the light");
        // Extent along x (the light's direction) beats extent along z.
        const float texel = 2.0f * f.half / N;
        int minX = N, maxX = -1, minZ = N, maxZ = -1;
        for (int iz = 0; iz < N; iz++)
            for (int ix = 0; ix < N; ix++)
                if (map[iz * N + ix] > 128) { minX = std::min(minX, ix); maxX = std::max(maxX, ix); minZ = std::min(minZ, iz); maxZ = std::max(maxZ, iz); }
        require(maxX >= minX && (maxX - minX) * texel > 1.15f * (maxZ - minZ) * texel, "a slanting light stretches the shadow");
    }
    // Overlapping capsules never get darker than one (no double darkening), and a foot on the ground has a sharper edge than a raised hand.
    {
        Capsule two[2] = { { { 0, 20, 0 }, { 0, 20, 0 }, 8 }, { { 2, 20, 0 }, { 2, 20, 0 }, 8 } };
        const MapFrame f = PlanMap(two, 2, noon.toward, 0.0f, 1.0f);
        Rasterize(f, two, 2, 1.0f, map.data(), N);
        std::vector<uint8_t> one(N * N);
        Rasterize(f, two, 1, 1.0f, one.data(), N);
        require(PeakOf(f, map, N).value == PeakOf(f, one, N).value, "overlap keeps the darkest, never adds");
        require(Penumbra(4, 0, 1) < Penumbra(4, 60, 1), "higher means softer");
        require(HeightFade(0) == 1.0f && HeightFade(300) < 1.0f && HeightFade(5000) >= 0.3f, "high things cast lighter shadows");
    }
    // A long shadow is cut at the map's maximum size rather than blurred over a huge area.
    {
        Capsule pole{ { 0, 0, 0 }, { 0, 2000, 0 }, 6 };
        const MapFrame f = PlanMap(&pole, 1, Norm({ 1, kMinElevation, 0 }), 0.0f, 1.0f, 220.0f);
        require(f.half <= 220.0f, "map size capped");
    }
    // Texture coordinates: the point under a noon ball sits where its shadow is; on a slope the lookup follows the light.
    {
        Capsule ball{ { 30, 40, 30 }, { 30, 40, 30 }, 10 };
        const MapFrame f = PlanMap(&ball, 1, noon.toward, 0.0f, 1.0f);
        float s, t;
        MapUV(f, 30, 0, 30, &s, &t);
        require(std::fabs(s - 0.5f) < 0.05f && std::fabs(t - 0.5f) < 0.05f, "uv of the shadow's middle");
        const MapFrame g = PlanMap(&ball, 1, Norm({ 1, 1, 0 }), 0.0f, 1.0f);
        float s1, t1, s2, t2;
        MapUV(g, 0, 0, 30, &s1, &t1);
        MapUV(g, 0, 20, 30, &s2, &t2);
        require(s2 < s1 && std::fabs(t1 - t2) < 1e-4f, "raised ground looks up the map further from the light");
        require(LedgeFade(0, 0) == 1.0f && LedgeFade(100, 0) == 0.0f, "no shadow on ground far above or below");
        require(RangeFade(0, 1000) == 1.0f && RangeFade(1000, 1000) == 0.0f && RangeFade(900, 1000) > 0.0f, "range fade");
    }
    // Raster AA survives zero softness and subtexel movement; physical blur still stretches at low sun.
    {
        Capsule c{{0, 0, 0}, {0, 0, 0}, 0.3f};
        MapFrame f{0, 0, 12, 0, {0, 1, 0}};
        std::fill(map.begin(), map.end(), uint8_t{0});
        Rasterize(f, &c, 1, 0, map.data(), 32);
        require(Coverage(map, 1) > 0, "subtexel hard caster survives between pixel centres");
        require(Penumbra(10, 20, -1) == 0, "negative softness safely clamps");
        c = {{0, 60, 0}, {0, 60, 0}, 10};
        const V3 t = Norm({std::sqrt(1 - kMinElevation * kMinElevation), kMinElevation, 0});
        const MapFrame g = PlanMap(&c, 1, t, 0, 2, 1000);
        require(g.half > (c.r + Penumbra(c.r, 60, 2)) / t.y, "low sun map pads the stretched penumbra");
    }
    // Receivers keep actual triangles; clipping cannot invent connecting faces across holes or steps.
    {
        MapFrame f{0, 0, 10, 0, Norm({1, 1, 0})};
        V3 out[12];
        V3 roof[3] = {{-5, 20, -5}, {5, 20, -5}, {0, 20, 5}};
        require(ClipReceiver(f, roof, out) == 0, "above-caster roof rejected without depth masking");
        V3 floor[3] = {{-30, -10, -30}, {30, -10, -30}, {0, -10, 30}};
        const int n = ClipReceiver(f, floor, out);
        require(n >= 3 && n <= 9, "sloped projected footprint bounded polygon");
        for (int i = 0; i < n; ++i) {
            float u, v; MapUV(f, out[i].x, out[i].y, out[i].z, &u, &v);
            require(u >= -1e-5f && u <= 1.00001f && v >= -1e-5f && v <= 1.00001f, "receiver clips in projected UV space");
            require(std::fabs(out[i].y + 10) < 1e-4f, "clipping preserves actual floor plane");
        }
        V3 missing[3] = {{0, std::numeric_limits<float>::quiet_NaN(), 0}, {1, 0, 0}, {0, 0, 1}};
        require(ClipReceiver(f, missing, out) == 0, "missing floor cannot become a connecting triangle");
        MapFrame overhead{0, 0, 10, 0, {0, 1, 0}};
        V3 lowerStep[3] = {{-5, -30, -5}, {0, -30, -5}, {-5, -30, 0}};
        V3 upperStep[3] = {{1, 0, 1}, {5, 0, 1}, {1, 0, 5}};
        require(ClipReceiver(overhead, lowerStep, out) == 3 && out[0].y == -30, "lower step stays its own plane");
        require(ClipReceiver(overhead, upperStep, out) == 3 && out[0].y == 0, "upper step stays its own plane");
        V3 abyss[3] = {{-5, -500, -5}, {5, -500, -5}, {0, -500, 5}};
        require(ClipReceiver(f, abyss, out) == 0, "deep floor bounded before conversion to game vertices");
        require(kReceiverProbeBudget == 256 && kReceiverTrianglesPerMap <= kReceiverTriangleBudget,
                "receiver work has fixed global and per-map caps");
        require(Preset(Quality::Low).mapSize == 32 && Preset(Quality::Low).maxMaps == 4, "Low performance caps unchanged");
    }
    // Link's body: valid joints give capsules of sensible size; zeros (never drawn) are rejected.
    {
        V3 p[PartCount];
        const V3 base{ 500, 100, 500 };
        auto at = [&](float x, float y, float z) { return V3{ base.x + x, base.y + y, base.z + z }; };
        p[Waist] = at(0, 30, 0); p[RThigh] = at(4, 29, 0); p[RShin] = at(4, 16, 1); p[RFoot] = at(4, 3, 0);
        p[LThigh] = at(-4, 29, 0); p[LShin] = at(-4, 16, 1); p[LFoot] = at(-4, 3, 0);
        p[Collar] = at(0, 48, 0); p[Head] = at(0, 54, 0); p[Hat] = at(0, 62, -6);
        p[LShoulder] = at(-8, 47, 0); p[LForearm] = at(-10, 38, 0); p[LHand] = at(-11, 30, 0);
        p[RShoulder] = at(8, 47, 0); p[RForearm] = at(10, 38, 0); p[RHand] = at(11, 30, 0);
        p[Sheath] = at(0, 40, -4); p[Torso] = at(0, 38, 0);
        require(BodyValid(p, base), "a drawn body is valid");
        Capsule caps[16];
        const int n = BodyCapsules(p, caps);
        require(n > 10 && n <= 16, "body capsule count");
        for (int i = 0; i < n; i++) require(caps[i].r > 1.0f && caps[i].r < 12.0f, "body capsule radius");
        const MapFrame f = PlanMap(caps, n, noon.toward, base.y, 1.0f);
        Rasterize(f, caps, n, 1.0f, map.data(), N);
        require(Coverage(map, 128) > 30, "a body casts a body-sized shadow");
        V3 zero[PartCount] = {};
        require(!BodyValid(zero, base), "joints never recorded are rejected");
    }
    // Upright and box shapes.
    {
        Capsule c[3];
        require(UprightCapsule({ 0, 0, 0 }, 60, 15, c) == 1 && c[0].a.y == 15 && c[0].b.y == 45, "upright capsule spans the body");
        require(BoxCapsules({ 0, 0, 0 }, 0.0f, 100, 50, 30, c) == 3 && std::fabs(c[0].a.z - c[0].b.z) > 50, "a box lies along its heading");
    }
    // Choosing: your own Link first, then the nearest.
    {
        Candidate c[4] = { { 500, 1, 0 }, { 100, 1, 1 }, { 900, 3, 2 }, { 50, 1, 3 } };
        int out[4];
        require(Choose(c, 4, 2, out) == 2 && out[0] == 2 && out[1] == 3, "priority then distance");
        require(Choose(c, 4, 0, out) == 0, "a budget of none");
    }
    // Lights: a flash rises and dies, fire flickers within its range, lights from one source merge, the list never overflows.
    {
        require(FlashCurve(0.0f, 1.0f) == 0.0f && FlashCurve(0.1f, 1.0f) > 0.9f && FlashCurve(0.9f, 1.0f) < 0.1f && FlashCurve(1.2f, 1.0f) == 0.0f, "flash curve");
        require(FlashCurve(5.0f, 0.0f) == 1.0f, "a steady light stays on");
        for (int i = 0; i < 200; i++) {
            const float f = Flicker(i * 0.037f, 0.4f, 7);
            require(f >= 0.6f - 1e-4f && f <= 1.0f + 1e-4f, "flicker within range");
        }
        Light lights[3];
        int n = 0;
        Light a; a.pos = { 0, 0, 0 }; a.key = 1; a.intensity = 0.5f;
        Light b = a; b.pos = { 20, 0, 0 }; b.intensity = 1.0f;
        Light c = a; c.pos = { 1000, 0, 0 };
        Light d = a; d.key = 2;
        require(MergeLight(lights, &n, 3, a) && MergeLight(lights, &n, 3, b) && n == 1 && lights[0].intensity == 1.0f, "nearby puffs merge, brighter kept");
        require(MergeLight(lights, &n, 3, c) && MergeLight(lights, &n, 3, d) && n == 3, "different places or sources stay apart");
        Light e = a; e.pos = { 5000, 0, 0 };
        require(!MergeLight(lights, &n, 3, e) && n == 3, "full list drops the extra light");
        require(LightScore(a, 1.0f, { 0, 0, 0 }) > LightScore(c, 1.0f, { 0, 0, 0 }), "near lights score higher");
    }
    std::cout << "shadow tests passed\n";
    return 0;
}
