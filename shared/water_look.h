#pragma once
// The look of the water surface at one point: its colour and see-through-ness, the sparkle on it and the foam. Kept free of the game so the
// game (RoyaleMod.cpp, DrawWaterSheet) and the preview renderer (tools/water/preview.cpp) shade the water with the very same code.
//   * colour by depth: turquoise shallows, teal, deep blue (`reach` is how deep the water must be to hide the floor);
//   * Fresnel: the lower you look across it, the more it mirrors the sky; the sun's and the moon's glitter paths;
//   * light through the crests (subsurface scattering): a wave's thin top glows green-turquoise, most when you look toward the sun;
//   * from below: Snell's window (the sky, bright, straight up) and the mirror of the deep around it;
//   * foam on the shallows' edge, in wakes and on crests about to break.
#include <algorithm>
#include <cmath>

namespace royale::water {

struct LookSky { float hor[3], zen[3], sun[3], moon[3], sunA, moonA, sunWarm; };   // the sky's colours (0-255), unit sun/moon directions, how much each shows
struct LookParams {
    float eye[3] = { 0, 0, 0 };
    float t = 0.0f, light = 1.0f;      // the clock (seconds) and the daylight (0.38 night .. 1 noon)
    float reach = 380.0f;              // how deep the water must be to hide the floor
    float ampTotal = 16.0f;            // the swell's total amplitude now
    float wind = 0.0f;                 // 0 calm .. 1 a gale
    float glintAmt = 1.0f, foamAmt = 1.0f;   // the sparkle and foam textures' strengths (0: drawn without them)
    bool skyOn = true;                 // reflections of the sky, the sun and the moon
};
struct Look { float col[4], glint[4], foam; };   // the water's colour and alpha; the sparkle's colour and alpha; the foam's alpha (all 0-255)

// (nx, ny, nz) the unit normal, `foamWake` the foam swimmers left, `fold` how near the crest is to breaking, `crest` its height above calm water,
// `twinkle` a steady random number for the point (0..1), `fade` the distance fade (0..1).
inline void ShadeWater(const LookParams& P, const LookSky& sky, float wx, float wz, float y, float depth, float nx, float ny, float nz, float foamWake,
                       float fold, float crest, float twinkle, float fade, Look& o) {
    const float k = std::clamp(depth / P.reach, 0.0f, 1.0f), ease = k * k * (3.0f - 2.0f * k);
    // turquoise over the shallows, teal, then deep blue (the Zora's Domain and Lake Hylia palette)
    static const float sh[3] = { 78, 226, 208 }, md[3] = { 20, 152, 176 }, dp[3] = { 8, 56, 116 };
    float c[3];
    for (int q = 0; q < 3; q++) c[q] = ease < 0.5f ? sh[q] + (md[q] - sh[q]) * ease * 2.0f : md[q] + (dp[q] - md[q]) * (ease - 0.5f) * 2.0f;
    float a = 36.0f + 194.0f * ease;   // the shallows let the floor (and its caustics) show through, the deep hides it
    float vx = P.eye[0] - wx, vy = P.eye[1] - y, vz = P.eye[2] - wz;
    const float vl = std::max(1.0f, std::sqrt(vx * vx + vy * vy + vz * vz)); vx /= vl; vy /= vl; vz /= vl;
    const float cosv = std::clamp(std::fabs(nx * vx + ny * vy + nz * vz), 0.0f, 1.0f);
    const float sunLit = 0.8f + 0.35f * std::max(0.0f, nx * sky.sun[0] + ny * sky.sun[1] + nz * sky.sun[2]) * sky.sunA;
    for (float& ch : c) ch *= P.light * sunLit;
    float fres = 0.0f, spec = 0.0f, path = 0.0f;   // path: the broad glitter path of the sun or the moon, broken into sparkles by the texture
    o.glint[0] = o.glint[1] = o.glint[2] = 255.0f; o.glint[3] = 0.0f;
    if (vy >= 0.0f) {
        if (P.skyOn) {
            // Fresnel: the lower you look across the water, the more it is a mirror of the sky
            fres = 0.04f + 0.96f * std::pow(1.0f - cosv, 4.0f);
            const float kz = std::pow(cosv, 0.6f), m = std::min(0.88f, fres);
            for (int q = 0; q < 3; q++) { const float skyc = sky.hor[q] + (sky.zen[q] - sky.hor[q]) * kz; c[q] = c[q] * (1.0f - m) + skyc * m; }
            a += 150.0f * fres;
            // the glitter path of the sun and the moon, twinkling
            const float tw = 0.6f + 0.4f * std::sin(P.t * 5.0f + twinkle * 40.0f);
            for (int body = 0; body < 2; body++) {
                const float* l = body == 0 ? sky.sun : sky.moon;
                const float amt = body == 0 ? sky.sunA : sky.moonA;
                if (amt < 0.02f || l[1] < 0.02f) continue;
                const float hxv = l[0] + vx, hyv = l[1] + vy, hzv = l[2] + vz;
                const float hl = std::max(0.001f, std::sqrt(hxv * hxv + hyv * hyv + hzv * hzv));
                const float nh = std::max(0.0f, (nx * hxv + ny * hyv + nz * hzv) / hl);
                const float s = (std::pow(nh, 160.0f) * tw + 0.22f * std::pow(nh, 18.0f)) * amt * (body == 0 ? 1.0f : 0.55f);
                const float cr = body == 0 ? 255.0f : 215.0f, cg = body == 0 ? 236.0f - 80.0f * sky.sunWarm : 225.0f, cb = body == 0 ? 205.0f - 120.0f * sky.sunWarm : 245.0f;
                const float mix = std::min(1.0f, s * 2.0f);
                c[0] += (cr - c[0]) * mix; c[1] += (cg - c[1]) * mix; c[2] += (cb - c[2]) * mix;
                a += 220.0f * s;
                if (s > spec) { spec = s; o.glint[0] = cr; o.glint[1] = cg; o.glint[2] = cb; }
                const float pk = std::pow(nh, 10.0f) * amt * (body == 0 ? 1.0f : 0.6f);
                if (pk > path) { path = pk; if (s <= spec) { o.glint[0] = cr; o.glint[1] = cg; o.glint[2] = cb; } }
            }
        }
        // P.light through the crests: a wave's thin top glows green-turquoise, most when you look toward the sun (subsurface scattering)
        const float crestK = std::clamp(crest / (P.ampTotal * 0.55f + 0.5f), 0.0f, 1.0f);
        const float back = std::max(0.0f, -(vx * sky.sun[0] + vz * sky.sun[2]) / std::max(0.001f, std::hypot(vx, vz)) / std::max(0.001f, std::hypot(sky.sun[0], sky.sun[2])));
        const float glow = crestK * crestK * (0.35f + 0.65f * back) * (0.15f + 0.85f * sky.sunA) * P.light;
        const float gm = std::min(0.65f, glow * 0.9f);
        c[0] += (70.0f - c[0]) * gm; c[1] += (250.0f - c[1]) * gm; c[2] += (205.0f - c[2]) * gm;
        a += 45.0f * glow;
    } else {
        // from below: inside Snell's window (looking up steeply) the sky shows bright; outside it the surface is a mirror of the deep
        const float window = std::clamp((cosv - 0.45f) / 0.25f, 0.0f, 1.0f);
        for (int q = 0; q < 3; q++) {
            const float skyc = std::min(255.0f, (sky.hor[q] * 0.4f + sky.zen[q] * 0.6f) * 1.15f + 30.0f);
            const float deep = (q == 0 ? 10.0f : q == 1 ? 96.0f : 112.0f) * P.light;
            c[q] = deep + (skyc - deep) * window;
        }
        a = 235.0f - 125.0f * window;   // the real sky shows through the window
        o.glint[0] = 200.0f; o.glint[1] = 255.0f; o.glint[2] = 250.0f;
        o.glint[3] = P.glintAmt * fade * 140.0f * window * P.light;
    }
    // P.light and dark on the slopes of the swell and the rings: the side facing away from the P.light is darker, the side toward it P.lighter
    const float shadeK = std::clamp(1.0f - (nx * 0.6f + nz * 0.35f) / std::max(0.2f, ny) * 5.0f, 0.7f, 1.35f);
    for (float& ch : c) ch *= shadeK;
    // foam where it is shallow (its edge washing in and out), in the wake of whoever swims, and on crests about to break
    float foam = foamWake * 0.8f;
    if (depth < 26.0f) foam = std::max(foam, std::clamp(1.0f - depth / (14.0f + 9.0f * std::sin(P.t * 1.6f + wx * 0.03f + wz * 0.025f)), 0.0f, 1.0f));   // a band at the waterline, washing in and out
    foam = std::max(foam, std::clamp((fold - 0.5f) * 2.4f, 0.0f, 0.85f) * (0.55f + 0.45f * std::clamp(P.wind, 0.0f, 1.0f)));
    foam = std::clamp(foam, 0.0f, 1.0f);
    const float flat = P.foamAmt > 0.0f ? 0.35f : 1.0f;   // with the foam texture on, the vertex colour only hints at it
    const float fw = (0.6f + 0.4f * P.light);
    c[0] += (238.0f * fw - c[0]) * foam * flat; c[1] += (246.0f * fw - c[1]) * foam * flat; c[2] += (250.0f * fw - c[2]) * foam * flat;
    a += (210.0f - a) * foam * flat;
    o.foam = std::clamp(foam * fade * P.foamAmt * 255.0f, 0.0f, 255.0f);
    if (vy >= 0.0f) {
        // the sparkle: the sky's own colour, lifted, turning to the sun's where the sun glints
        for (int q = 0; q < 3; q++) {
            const float skyc = std::min(255.0f, (sky.hor[q] + sky.zen[q]) * 0.65f + 70.0f);
            o.glint[q] = skyc + (o.glint[q] - skyc) * std::min(1.0f, spec * 3.0f + path * 1.5f);
        }
        o.glint[3] = P.glintAmt * fade * (0.3f + 0.7f * P.light) * (55.0f + 140.0f * fres + 260.0f * spec + 420.0f * path);
    }
    o.col[0] = c[0]; o.col[1] = c[1]; o.col[2] = c[2]; o.col[3] = a * fade;
}

}  // namespace royale::water
