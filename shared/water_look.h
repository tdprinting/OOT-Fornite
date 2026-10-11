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
    float sharpSpec = 1.0f;            // the pin-point sun glint (0 for vertex-shaded grids: one hot vertex lights a whole square; the sparkle
                                       // texture draws those glints instead)
    // the open-sea look (the rolling, layered sea of the reference: see docs/water/README.md)
    float sparkle = 0.0f;              // 1: the glint texture is the sparse sparkle, so its alpha gathers on the sun's path and thins elsewhere
    float streaks = 0.0f;              // strength of the wind-blown foam streaks and whitecaps pass (0 off)
    float bands = 0.0f;                // strength of the rolling swell-band shading pass (0 off)
    float haze = 1.0f;                 // how much the far water melts into the sky at the horizon (0 off .. 2 strong)
    float hazeNear = 900.0f, hazeFar = 2400.0f;   // where the haze starts and where it is complete (units from the eye)
    float depthTone = 1.0f;            // how much deeper blue the troughs go and the richer the colour is (0 the old flat tone .. 2)
    float clouds = 0.0f;               // strength of the clouds mirrored in the water (0 off .. 2); `cover` how cloudy the sky is (0 clear .. 1 overcast)
    float cover = 0.4f;
};
// The water's colour and alpha; the sparkle's colour and alpha; the foam's alpha; the streaks' alpha; the swell bands' colour and alpha; the mirrored
// clouds' colour and alpha (all 0-255).
struct Look { float col[4], glint[4], foam, streak, band[4], cloud[4]; };

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
                const float s = (std::pow(nh, 160.0f) * tw * P.sharpSpec + 0.22f * std::pow(nh, 18.0f)) * amt * (body == 0 ? 1.0f : 0.55f);
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
    // the rolling sea's tone: troughs sink toward the deep blue and the colour is a shade richer, so the swell reads as hills and valleys of
    // water (the reference's teal faces over deep blue hollows) and not a flat tint
    if (P.depthTone > 0.0f && vy >= 0.0f) {
        const float tone = std::min(2.0f, P.depthTone);
        const float trough = std::clamp(-crest / (P.ampTotal * 0.5f + 0.5f), 0.0f, 1.0f) * ease;   // only where the water is deep enough to be dark
        const float tk = std::min(0.6f, trough * 0.38f * tone);
        for (int q = 0; q < 3; q++) c[q] += (dp[q] * P.light * 0.9f - c[q]) * tk;
        const float lum = c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f, sat = 1.0f + 0.14f * tone;
        for (float& ch : c) ch = std::clamp(lum + (ch - lum) * sat, 0.0f, 255.0f);
        a += 25.0f * trough * tone;
    }
    // foam where it is shallow (its edge washing in and out), in the wake of whoever swims, and on crests about to break
    float foam = foamWake * 0.8f;
    if (depth < 26.0f) foam = std::max(foam, std::clamp(1.0f - depth / (14.0f + 9.0f * std::sin(P.t * 1.6f + wx * 0.03f + wz * 0.025f)), 0.0f, 1.0f));   // a band at the waterline, washing in and out
    const float cap = std::clamp((fold - 0.5f) * 2.4f, 0.0f, 0.85f) * (0.55f + 0.45f * std::clamp(P.wind, 0.0f, 1.0f));   // whitecaps
    foam = std::max(foam, cap);
    foam = std::clamp(foam, 0.0f, 1.0f);
    const float flat = P.foamAmt > 0.0f ? 0.35f : 1.0f;   // with the foam texture on, the vertex colour only hints at it
    const float fw = (0.6f + 0.4f * P.light);
    c[0] += (238.0f * fw - c[0]) * foam * flat; c[1] += (246.0f * fw - c[1]) * foam * flat; c[2] += (250.0f * fw - c[2]) * foam * flat;
    a += (210.0f - a) * foam * flat;
    o.foam = std::clamp(foam * fade * P.foamAmt * 255.0f, 0.0f, 255.0f);
    // the wind's foam streaks: they lie on the backs and tops of the swell (most on crests about to break), more the harder the wind blows,
    // and never on the shallows' foam band, which the foam pass already draws
    o.streak = 0.0f;
    if (P.streaks > 0.0f && vy >= 0.0f) {
        const float crestK = std::clamp(crest / (P.ampTotal * 0.5f + 0.5f), 0.0f, 1.0f), wind = std::clamp(P.wind, 0.0f, 1.0f);
        // the streaks come in drifts: broad patches of sea with them and without (so they never rule the whole surface)
        const float drift = 0.5f + 0.5f * std::sin(wx * 0.0029f + wz * 0.0016f + P.t * 0.05f) * std::sin(wx * 0.0011f - wz * 0.0024f + 1.7f - P.t * 0.03f);
        const float patch = 0.25f + 0.75f * drift * drift;
        const float st = ((0.1f + 0.3f * wind) * crestK * crestK * crestK + std::clamp((fold - 0.35f) * 1.8f, 0.0f, 1.0f) * (0.6f + 0.6f * wind)) * patch + foamWake * 0.5f;
        o.streak = std::clamp(st * std::min(2.0f, P.streaks) * ease * fade * (0.5f + 0.5f * P.light) * 255.0f, 0.0f, 255.0f);
    }
    // the swell bands: the water under them is a shade darker and bluer, and the pass lays a pale sunlit teal back over it in the rolling
    // pattern of the texture: lit faces over darker hollows. Stronger when the sun is up and when the water is seen at a slant (the faces of
    // the waves catch the light), fading out far off with the other textures.
    o.band[0] = o.band[1] = o.band[2] = 255.0f; o.band[3] = 0.0f;
    if (P.bands > 0.0f && vy >= 0.0f) {
        const float bk = std::min(1.0f, P.bands) * ease;
        c[0] *= 1.0f - 0.2f * bk; c[1] *= 1.0f - 0.14f * bk; c[2] *= 1.0f - 0.06f * bk;
        const float sunUp = 0.35f + 0.65f * sky.sunA;
        o.band[0] = std::min(255.0f, c[0] * 0.5f + 78.0f * P.light); o.band[1] = std::min(255.0f, c[1] * 0.5f + 120.0f * P.light); o.band[2] = std::min(255.0f, c[2] * 0.5f + 112.0f * P.light);
        o.band[3] = std::clamp(std::min(2.0f, P.bands) * ease * fade * P.light * sunUp * (130.0f + 90.0f * (1.0f - cosv)), 0.0f, 255.0f);
    }
    if (vy >= 0.0f) {
        // the sparkle: the sky's own colour, lifted, turning to the sun's where the sun glints
        for (int q = 0; q < 3; q++) {
            const float skyc = std::min(255.0f, (sky.hor[q] + sky.zen[q]) * 0.65f + 70.0f);
            o.glint[q] = skyc + (o.glint[q] - skyc) * std::min(1.0f, spec * 3.0f + path * 1.5f);
        }
        if (P.sparkle > 0.0f) {
            // the sparse sparkle: bright where the sun's or the moon's glitter path lies, a thin scatter everywhere else (the specks themselves
            // are the texture; the alpha only says where they may show)
            const float energy = 14.0f + 40.0f * fres + 230.0f * spec + 300.0f * path;
            const float sparse = P.glintAmt * fade * (0.3f + 0.7f * P.light) * (235.0f * energy / (120.0f + energy));
            // Keep ordinary ripples subtle and reserve bright highlights for the
            // sun/moon path. A smooth response avoids large saturated white patches.
            const float net = (18.0f + 65.0f * fres + 160.0f * spec + 180.0f * path);
            const float netA = P.glintAmt * fade * (0.3f + 0.7f * P.light) * (160.0f * net / (160.0f + net));
            o.glint[3] = netA + (sparse - netA) * std::min(1.0f, P.sparkle);
        } else {
            // Keep ordinary ripples subtle and reserve bright highlights for the
            // sun/moon path. A smooth response avoids large saturated white patches.
            const float energy = (18.0f + 65.0f * fres + 160.0f * spec + 180.0f * path);
            o.glint[3] = P.glintAmt * fade * (0.3f + 0.7f * P.light) * (160.0f * energy / (160.0f + energy));
        }
    }
    // the clouds mirrored in the water: seen at a slant the water is a mirror, so the cloud texture (placed by the game where the reflection of
    // each cloud lies) shows through, pale and lit by the sun (golden at dusk), and dim and cool at night
    o.cloud[0] = o.cloud[1] = o.cloud[2] = 255.0f; o.cloud[3] = 0.0f;
    if (P.clouds > 0.0f && P.skyOn && vy >= 0.0f) {
        const float day = std::clamp(0.28f + 0.72f * P.light, 0.0f, 1.0f), warm = std::clamp(sky.sunWarm, 0.0f, 1.0f);
        for (int q = 0; q < 3; q++) {
            const float lit = 0.5f * sky.hor[q] + 0.5f * 255.0f * day;
            const float dusk = q == 0 ? 255.0f : q == 1 ? 170.0f : 130.0f;
            o.cloud[q] = std::min(255.0f, (lit + (dusk - lit) * warm * 0.7f) * (0.55f + 0.45f * day));
        }
        const float mirror = 0.12f + 0.88f * std::pow(1.0f - cosv, 1.6f);
        o.cloud[3] = std::clamp(std::min(2.0f, P.clouds) * (0.35f + 0.65f * P.cover) * mirror * fade * (0.45f + 0.55f * P.light) * 170.0f, 0.0f, 255.0f);
    }
    // the horizon haze: far off, the sea melts into the sky's horizon colour (the air between), and stays opaque so it meets the sky softly
    // instead of thinning out over the floor
    if (P.haze > 0.0f && vy >= 0.0f) {
        const float d = std::sqrt((P.eye[0] - wx) * (P.eye[0] - wx) + (P.eye[2] - wz) * (P.eye[2] - wz));
        float hk = std::clamp((d - P.hazeNear) / std::max(1.0f, P.hazeFar - P.hazeNear), 0.0f, 1.0f);
        hk = hk * hk * (3.0f - 2.0f * hk) * std::min(1.0f, P.haze * 0.85f);
        for (int q = 0; q < 3; q++) c[q] += (std::min(255.0f, sky.hor[q] * 0.92f + 12.0f) - c[q]) * hk;
        a += (236.0f - a) * hk;
        o.glint[3] *= 1.0f - hk; o.band[3] *= 1.0f - hk; o.streak *= 1.0f - hk * 0.8f; o.cloud[3] *= 1.0f - hk * 0.5f;
    }
    o.col[0] = c[0]; o.col[1] = c[1]; o.col[2] = c[2]; o.col[3] = a * fade;
}

}  // namespace royale::water
