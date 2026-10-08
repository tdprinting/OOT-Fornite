#pragma once
// The maths behind the mod's dynamic lights and shadows (RoyaleMod.cpp, "dynamic lights and shadows"). No game headers here, so it can be tested on
// its own (server/tests/shadow_tests.cpp).
//
// How a shadow is made. A phone cannot render the scene a second time from the sun (a real shadow map), and the game's renderer gives no hook for
// one. So every object that casts a shadow is described by a few capsules (a line with a radius: a shin, a forearm, a body, a boulder), the way
// many modern games do their character shadows. Each frame those capsules are projected along the light onto the ground under the object and
// painted into a small greyscale picture (16 to 64 pixels across): that is the object's own shadow map. Where capsules overlap the darkest one
// wins, so arms over a body never stack into a darker blotch. The edge of each capsule's shadow softens with its height above the ground (a foot
// on the ground gives a sharp edge, a raised hand a soft one), like a real penumbra. The picture is then laid on the ground as a small mesh that
// follows the floor's height, in a cool, see-through shadow colour: a soft, painted Ocarina of Time shadow that turns and stretches with the sun.
//
// Lights. The game's own light system lights every character and enemy from up to seven lights. The mod adds short-lived point lights to it
// (explosions, Din's Fire, fire and light arrows, lightning, chest reveals): the closest and brightest win the few slots there are.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace royale::shadows {

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 Add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 Mul(V3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline V3 Norm(V3 a) { const float l = Len(a); return l > 1e-6f ? Mul(a, 1.0f / l) : V3{ 0, 1, 0 }; }

// A capsule: the segment a-b swept by a sphere of radius r (a sphere when a == b).
struct Capsule { V3 a, b; float r = 0; };

// ---- quality -----------------------------------------------------------------------------------------------------------------
enum class Quality : int { Off, Low, Medium, High, Ultra, Count };
constexpr const char* kQualityNames[] = { "Off", "Low (fastest, for handhelds)", "Medium", "High", "Ultra" };

struct Settings {
    int mapSize = 32;          // shadow map pixels per side (16, 32 or 64)
    int maxMaps = 4;           // shadow maps drawn at once (each caster's sun shadow is one; a light's shadow is another)
    float range = 1000.0f;     // shadows further than this from the camera are not drawn (they fade out over the last 20%)
    int grid = 3;              // squares per side of the mesh the shadow is laid on (more follows bumpy ground better)
    int lightShadows = 0;      // how many nearby lights may give each object a second (or third) shadow
    float softness = 1.0f;     // scales the soft edge
    bool coverCheck = false;   // look toward the sun from each object: under a roof or a bridge it casts no sun shadow
    int refreshFar = 2;        // far casters (past half the range) repaint their map every this many frames; near ones every frame
    int maxLights = 2;         // dynamic lights in the game's light system at once (each character takes at most 7 lights, two of them the sun and sky)
    bool lightPools = true;    // a soft glow on the ground under each dynamic light
};

inline Settings Preset(Quality q) {
    Settings s;
    switch (q) {
        case Quality::Off: s.maxMaps = 0; s.maxLights = 0; s.lightPools = false; break;
        case Quality::Low: break;   // the defaults above: cheap enough for an Android handheld
        case Quality::Medium:
            s.mapSize = 32; s.maxMaps = 8; s.range = 1600.0f; s.grid = 4; s.lightShadows = 1; s.coverCheck = true; s.refreshFar = 2; s.maxLights = 3; break;
        case Quality::High:
            s.mapSize = 64; s.maxMaps = 12; s.range = 2200.0f; s.grid = 6; s.lightShadows = 1; s.coverCheck = true; s.refreshFar = 1; s.maxLights = 4; break;
        case Quality::Ultra:
            s.mapSize = 64; s.maxMaps = 20; s.range = 3000.0f; s.grid = 8; s.lightShadows = 2; s.coverCheck = true; s.refreshFar = 1; s.maxLights = 5; break;
        default: break;
    }
    return s;
}
constexpr int kMaxMapSize = 64;
constexpr int kMaxMaps = 20;
constexpr int kMaxGrid = 8;
constexpr int kMaxLights = 5;   // the game binds at most 7 lights to a character and the scene's sun and sky take 2

// ---- the lights that cast shadows -------------------------------------------------------------------------------------------
// Where a light is, as the direction toward it from the ground (unit length, pointing up), and how dark its shadows are (0 to 1).
struct ShadowLight { V3 toward{ 0, 1, 0 }; float strength = 0; };

// The lowest a light is allowed to sit for shadow purposes (the sine of its height): a sun on the horizon would throw shadows across the whole map.
constexpr float kMinElevation = 0.24f;

// The sun or the moon, from the sky's own numbers (RoyaleMod.cpp SkyLightNow, DrawSky): `sunH` is the sine of the sun's height, the sun sits at
// azimuth `azimuth` (radians from +x toward +z) and the moon opposite. `overcast` (0 to 1) washes shadows out. Day shadows are the strongest;
// moonlight gives faint ones; around dawn and dusk they fade out as the sun touches the horizon, and come back from the moon after.
inline ShadowLight SunShadow(float sunH, float overcast, float azimuth = 0.6f) {
    ShadowLight l;
    const bool moon = sunH < 0.0f;
    float h = std::fabs(sunH);
    const float c = std::sqrt(std::max(0.0f, 1.0f - h * h));
    const float sx = std::cos(azimuth) * c * (moon ? -1.0f : 1.0f), sz = std::sin(azimuth) * c * (moon ? -1.0f : 1.0f);
    // Lift a low light to the minimum height, keeping its direction across the ground.
    float y = std::max(h, kMinElevation);
    const float flat = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float hl = std::sqrt(sx * sx + sz * sz);
    l.toward = hl > 1e-5f ? V3{ sx / hl * flat, y, sz / hl * flat } : V3{ 0, 1, 0 };
    const float rise = std::clamp((h - 0.02f) / 0.16f, 0.0f, 1.0f);   // fades in as the sun or the moon clears the horizon
    l.strength = (moon ? 0.32f : 0.62f) * rise * (1.0f - 0.8f * std::clamp(overcast, 0.0f, 1.0f));
    return l;
}

// A point light's shadow for something at `at`: away from the light, fading with distance (the same falloff the game's light system uses).
inline ShadowLight PointShadow(V3 light, float radius, float intensity, V3 at) {
    ShadowLight l;
    const V3 d = Sub(light, at);
    const float dist = Len(d);
    if (radius <= 0.0f || dist >= radius) return l;
    V3 t = Norm(d);
    if (t.y < kMinElevation) {   // a light level with the object (or below it) is lifted: a shadow cannot go up a wall here
        const float flat = std::sqrt(1.0f - kMinElevation * kMinElevation);
        const float hl = std::sqrt(t.x * t.x + t.z * t.z);
        t = hl > 1e-5f ? V3{ t.x / hl * flat, kMinElevation, t.z / hl * flat } : V3{ 0, 1, 0 };
    }
    const float k = dist / radius;
    l.toward = t;
    l.strength = std::clamp(intensity, 0.0f, 1.0f) * 0.55f * (1.0f - k * k);
    return l;
}

// ---- projecting onto the ground ---------------------------------------------------------------------------------------------
// Where a point lands on the ground plane y = groundY when it is moved away from the light: its (x, z) there.
inline void Project(V3 p, V3 toward, float groundY, float* x, float* z) {
    const float k = (p.y - groundY) / std::max(toward.y, 1e-3f);
    *x = p.x - toward.x * k;
    *z = p.z - toward.z * k;
}

// The square of ground a shadow map covers: centre (cx, cz) and half its side, on the plane y = groundY.
struct MapFrame { float cx = 0, cz = 0, half = 0, groundY = 0; V3 toward{ 0, 1, 0 }; };

// The soft edge a capsule's shadow gets at height `h` above the ground (world units on each side of the edge).
inline float Penumbra(float r, float h, float softness) { return std::max(0.0f, softness) * (0.35f * r + 1.5f + 0.06f * std::max(0.0f, h)); }

// The shadows of tall things (a jumping player, a high branch) grow lighter the further they are from the ground.
inline float HeightFade(float h) { return std::clamp(1.0f - std::max(0.0f, h) / 420.0f, 0.3f, 1.0f); }

// Plans the square of ground that holds every capsule's shadow, with room for the soft edges, but never wider than `maxHalf` (a long evening
// shadow is cut off there rather than spread thin over a huge, blurry map).
inline MapFrame PlanMap(const Capsule* caps, int n, V3 toward, float groundY, float softness, float maxHalf = 220.0f) {
    MapFrame f;
    f.toward = toward;
    f.groundY = groundY;
    if (n <= 0) return f;
    const float stretch = 1.0f / std::max(toward.y, kMinElevation);   // a sphere's shadow is longer than wide by this much
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    for (int i = 0; i < n; i++) {
        const Capsule& c = caps[i];
        for (int e = 0; e < 2; e++) {
            const V3 p = e ? c.b : c.a;
            float x, z;
            Project(p, toward, groundY, &x, &z);
            const float pad = (c.r + Penumbra(c.r, p.y - groundY, softness)) * stretch + 1.0f;
            lo[0] = std::min(lo[0], x - pad); hi[0] = std::max(hi[0], x + pad);
            lo[1] = std::min(lo[1], z - pad); hi[1] = std::max(hi[1], z + pad);
        }
    }
    f.cx = 0.5f * (lo[0] + hi[0]);
    f.cz = 0.5f * (lo[1] + hi[1]);
    f.half = std::clamp(0.5f * std::max(hi[0] - lo[0], hi[1] - lo[1]) * 1.04f, 12.0f, maxHalf);
    return f;
}

// Paints the shadow map: `out` is size x size bytes, row by row from -z to +z, each column from -x to +x; 0 is no shadow, 255 full shadow.
// Each capsule is projected along the light; its shadow is the set of ground points within r of the projected segment, measured with the
// direction along the light shortened by its slant (so a sphere's shadow is an ellipse stretched away from the light, as it should be). The
// result keeps the darkest value where shadows overlap, and the outermost row and column stay clear so the map never smears at its border.
inline void Rasterize(const MapFrame& f, const Capsule* caps, int n, float softness, uint8_t* out, int size) {
    std::fill(out, out + size * size, uint8_t{ 0 });
    if (f.half <= 0.0f || size < 4) return;
    const V3 t = f.toward;
    const float hl = std::sqrt(t.x * t.x + t.z * t.z);
    const float ax = hl > 1e-5f ? t.x / hl : 1.0f, az = hl > 1e-5f ? t.z / hl : 0.0f;   // across the ground toward the light
    const float slant = std::max(t.y, kMinElevation);   // along that axis, ground distances shrink by this to measure in the capsule's own space
    const float texel = 2.0f * f.half / size;
    const float x0 = f.cx - f.half + 0.5f * texel, z0 = f.cz - f.half + 0.5f * texel;
    // Ground (x, z) to the measuring space: (along the light * slant, across it).
    auto toM = [&](float x, float z, float* u, float* v) { *u = (x * ax + z * az) * slant; *v = -x * az + z * ax; };
    for (int i = 0; i < n; i++) {
        const Capsule& c = caps[i];
        float pax, paz, pbx, pbz;
        Project(c.a, t, f.groundY, &pax, &paz);
        Project(c.b, t, f.groundY, &pbx, &pbz);
        const float ha = std::max(0.0f, c.a.y - f.groundY), hb = std::max(0.0f, c.b.y - f.groundY);
        float au, av, bu, bv;
        toM(pax, paz, &au, &av);
        toM(pbx, pbz, &bu, &bv);
        const float du = bu - au, dv = bv - av;
        const float len2 = du * du + dv * dv;
        const float maxSoft = std::max(Penumbra(c.r, std::max(ha, hb), softness), 0.707107f * texel);
        // The texels the capsule can touch (its projected box, padded by the stretched radius and the widest soft edge).
        const float pad = (c.r + maxSoft) / slant + texel;
        const int ix0 = std::max(1, static_cast<int>((std::min(pax, pbx) - pad - x0) / texel));
        const int ix1 = std::min(size - 2, static_cast<int>((std::max(pax, pbx) + pad - x0) / texel) + 1);
        const int iz0 = std::max(1, static_cast<int>((std::min(paz, pbz) - pad - z0) / texel));
        const int iz1 = std::min(size - 2, static_cast<int>((std::max(paz, pbz) + pad - z0) / texel) + 1);
        for (int iz = iz0; iz <= iz1; iz++) {
            const float z = z0 + iz * texel;
            uint8_t* row = out + iz * size;
            for (int ix = ix0; ix <= ix1; ix++) {
                const float x = x0 + ix * texel;
                float u, v;
                toM(x, z, &u, &v);
                float s = len2 > 1e-6f ? ((u - au) * du + (v - av) * dv) / len2 : 0.0f;
                s = std::clamp(s, 0.0f, 1.0f);
                const float eu = u - (au + du * s), ev = v - (av + dv * s);
                const float dist = std::sqrt(eu * eu + ev * ev);
                const float h = ha + (hb - ha) * s;
                // Pixel coverage is separate from physical penumbra; hard shadows still antialias.
                const float soft = std::max(Penumbra(c.r, h, softness), 0.707107f * texel);
                if (dist >= c.r + soft) continue;
                float k = std::clamp((c.r + soft - dist) / (2.0f * soft), 0.0f, 1.0f);
                k = k * k * (3.0f - 2.0f * k);
                const int val = static_cast<int>(k * HeightFade(h) * 255.0f + 0.5f);
                if (val > row[ix]) row[ix] = static_cast<uint8_t>(std::min(255, val));
            }
        }
    }
}

// The shadow map's texture coordinate (0 to 1 across the map) for a ground point at (x, y, z): moved along the light onto the map's plane first,
// so on a slope the shadow lands where the light really puts it.
inline void MapUV(const MapFrame& f, float x, float y, float z, float* s, float* t) {
    float px, pz;
    Project({ x, y, z }, f.toward, f.groundY, &px, &pz);
    *s = (px - (f.cx - f.half)) / (2.0f * f.half);
    *t = (pz - (f.cz - f.half)) / (2.0f * f.half);
}

// Clip an actual collision triangle to the shadow footprint and below the caster base.
// UV clipping preserves sloped geometry. Above-base receivers need per-capsule depth
// masking, so reject those conservatively rather than painting shadows up onto roofs.
constexpr int kReceiverProbeBudget = 256;
constexpr int kReceiverTriangleBudget = 512;
constexpr int kReceiverTrianglesPerMap = 48;
inline int ClipReceiver(const MapFrame& f, const V3* triangle, V3* out) {
    if (!(f.half > 0)) return 0;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(triangle[i].x) || !std::isfinite(triangle[i].y) || !std::isfinite(triangle[i].z)) return 0;
    V3 a[12], b[12];
    std::copy(triangle, triangle + 3, a);
    int n = 3;
    for (int plane = 0; plane < 6 && n; ++plane) {
        auto distance = [&](V3 p) {
            float u, v; MapUV(f, p.x, p.y, p.z, &u, &v);
            switch (plane) {
                case 0: return u;
                case 1: return 1.0f - u;
                case 2: return v;
                case 3: return 1.0f - v;
                case 4: return f.groundY + 3.0f - p.y;
                default: return p.y - (f.groundY - 420.0f);
            }
        };
        int m = 0;
        V3 prev = a[n - 1]; float dp = distance(prev);
        for (int i = 0; i < n; ++i) {
            const V3 cur = a[i]; const float dc = distance(cur);
            if ((dp >= 0) != (dc >= 0)) b[m++] = Add(prev, Mul(Sub(cur, prev), dp / (dp - dc)));
            if (dc >= 0) b[m++] = cur;
            prev = cur; dp = dc;
        }
        n = m; std::copy(b, b + n, a);
    }
    std::copy(a, a + n, out);
    return n;
}

// How much of the shadow shows on a piece of ground at height `y` when the map was planned at `groundY`: a shadow does not paint a cliff face far
// below or a ledge far above the object.
inline float LedgeFade(float y, float groundY) { return std::clamp(1.0f - (std::fabs(y - groundY) - 12.0f) / 50.0f, 0.0f, 1.0f); }

// How strong a shadow is at `dist` from the camera, fading out over the last fifth of `range`.
inline float RangeFade(float dist, float range) { return range <= 0.0f ? 0.0f : std::clamp((range - dist) / (0.2f * range), 0.0f, 1.0f); }

// ---- characters as capsules -------------------------------------------------------------------------------------------------
// Link's joints, as the game records them each time he is drawn (Player::bodyPartsPos, PlayerBodyPart order).
enum Part { Waist, RThigh, RShin, RFoot, LThigh, LShin, LFoot, Head, Hat, Collar, LShoulder, LForearm, LHand, RShoulder, RForearm, RHand, Sheath, Torso, PartCount };

// Link's body from his joints: legs, arms, hips, body, neck, head, feet and the tip of his cap, with radii scaled to his size (a child is about
// two thirds of an adult). Works lying down too (a body on the ground). Returns how many capsules were written (at most 16).
inline int BodyCapsules(const V3* p, Capsule* out) {
    const float k = std::clamp(Len(Sub(p[Head], p[Waist])) / 24.0f, 0.4f, 3.0f);   // an adult's waist to the base of his head is about 24 units
    int n = 0;
    auto seg = [&](int a, int b, float r) { out[n++] = { p[a], p[b], r * k }; };
    seg(RThigh, RShin, 4.2f); seg(RShin, RFoot, 3.4f);
    seg(LThigh, LShin, 4.2f); seg(LShin, LFoot, 3.4f);
    seg(LShoulder, LForearm, 3.0f); seg(LForearm, LHand, 2.6f);
    seg(RShoulder, RForearm, 3.0f); seg(RForearm, RHand, 2.6f);
    seg(Waist, Collar, 7.5f);
    seg(LThigh, RThigh, 5.0f);   // hips
    seg(Collar, Head, 3.0f);
    // The head: from the base of the skull up past the cap's band, round.
    const V3 up = Norm(Sub(p[Head], p[Collar]));
    out[n++] = { Add(p[Head], Mul(up, 4.0f * k)), Add(p[Head], Mul(up, 7.0f * k)), 6.5f * k };
    // The feet.
    out[n++] = { p[RFoot], p[RFoot], 3.6f * k };
    out[n++] = { p[LFoot], p[LFoot], 3.6f * k };
    // The cap's long tip, hanging behind the head.
    out[n++] = { p[Head], p[Hat], 3.2f * k };
    return n;
}

// Are these joints usable (all finite and within `reach` of the actor)? Before Link is drawn for the first time they are all zero.
inline bool BodyValid(const V3* p, V3 actor, float reach = 220.0f) {
    for (int i = 0; i < PartCount; i++) {
        if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y) || !std::isfinite(p[i].z)) return false;
        if (Len(Sub(p[i], actor)) > reach) return false;
    }
    return Len(Sub(p[Head], p[Waist])) > 4.0f;
}

// An upright body (a pet, a mini boss, an ally): a capsule from just above its feet to its head height.
inline int UprightCapsule(V3 feet, float height, float radius, Capsule* out) {
    const float r = std::min(radius, 0.5f * height);
    out[0] = { { feet.x, feet.y + r, feet.z }, { feet.x, feet.y + std::max(r, height - r), feet.z }, r };
    return 1;
}

// A lying or rolling thing with a heading (a cart): `length` along `yaw` (radians, 0 = +z), `width` across, `height` tall.
inline int BoxCapsules(V3 centre, float yaw, float length, float width, float height, Capsule* out) {
    const float fx = std::sin(yaw), fz = std::cos(yaw), rx = fz, rz = -fx;
    const float r = std::min(0.5f * height, 0.25f * width);
    const float hl = std::max(0.0f, 0.5f * length - r), hw = std::max(0.0f, 0.5f * width - r);
    const float y = centre.y + 0.5f * height;
    int n = 0;
    for (int side = -1; side <= 1; side += 2) {
        const V3 o = { centre.x + rx * hw * side, y, centre.z + rz * hw * side };
        out[n++] = { { o.x - fx * hl, y, o.z - fz * hl }, { o.x + fx * hl, y, o.z + fz * hl }, r };
    }
    out[n++] = { { centre.x - fx * hl, y, centre.z - fz * hl }, { centre.x + fx * hl, y, centre.z + fz * hl }, std::min(r * 1.2f, 0.5f * width) };
    return n;
}

// ---- choosing what gets a shadow --------------------------------------------------------------------------------------------
struct Candidate { float dist = 0; int priority = 0; int index = 0; };   // priority: higher goes first (your own Link is the highest)

// Picks up to `max` candidates: the highest priority first, then the nearest. Writes their indices to `out`, returns how many.
inline int Choose(Candidate* c, int n, int max, int* out) {
    std::sort(c, c + n, [](const Candidate& a, const Candidate& b) { return a.priority != b.priority ? a.priority > b.priority : a.dist < b.dist; });
    const int k = std::min(n, std::max(0, max));
    for (int i = 0; i < k; i++) out[i] = c[i].index;
    return k;
}

// ---- dynamic lights ---------------------------------------------------------------------------------------------------------
// A light that comes and goes: a flash (an explosion, a bolt) rises quickly and dies away over `life` seconds; a steady light (a fire, a spell)
// holds while its source lives. `flicker` (0 to 1) makes fire shimmer.
struct Light {
    V3 pos;
    float r = 1, g = 1, b = 1;       // colour, 0 to 1
    float radius = 200;              // how far it reaches
    float intensity = 1;             // 0 to 1
    float life = 0.5f, age = 0;      // a flash: seconds it lasts, seconds so far (life <= 0: steady, rebuilt every frame)
    float flicker = 0;
    int key = 0;                     // what made it (merging repeats of the same source)
};

// How bright a flash is `age` seconds in: up in a tenth of its life, then down along a curve.
inline float FlashCurve(float age, float life) {
    if (life <= 0.0f) return 1.0f;
    if (age >= life) return 0.0f;
    const float up = std::max(0.02f, 0.1f * life);
    if (age < up) return age / up;
    const float k = (age - up) / (life - up);
    return (1.0f - k) * (1.0f - k);
}

// A fire's shimmer: a sum of a few uneven waves, between 1 - flicker and 1.
inline float Flicker(float t, float flicker, int seed) {
    if (flicker <= 0.0f) return 1.0f;
    const float s = static_cast<float>(seed % 97) * 0.731f;
    const float w = 0.5f + 0.5f * (0.5f * std::sin(t * 13.1f + s) + 0.3f * std::sin(t * 23.7f + 2.1f * s) + 0.2f * std::sin(t * 41.3f + 0.7f * s));
    return 1.0f - flicker * (1.0f - w);
}

// How much a light is worth a slot when seen from `eye`: bright, big and close lights first.
inline float LightScore(const Light& l, float brightness, V3 eye) {
    const float d = Len(Sub(l.pos, eye));
    return brightness * l.radius / (200.0f + d);
}

// Folds a new light into `lights` (count `*n`, room for `cap`): a light near one with the same key just keeps the brighter of the two (dozens of
// explosion puffs make one explosion light). Returns false when it was dropped for lack of room.
inline bool MergeLight(Light* lights, int* n, int cap, const Light& l, float mergeDist = 90.0f) {
    for (int i = 0; i < *n; i++) {
        Light& o = lights[i];
        if (o.key != l.key || Len(Sub(o.pos, l.pos)) > mergeDist) continue;
        if (l.intensity * l.radius > o.intensity * o.radius) { const float age = std::min(o.age, l.age); o = l; o.age = age; }
        return true;
    }
    if (*n >= cap) return false;
    lights[(*n)++] = l;
    return true;
}

}   // namespace royale::shadows
