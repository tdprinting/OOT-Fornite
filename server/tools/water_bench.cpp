// Water drawing benchmark and equivalence check (headless, no game): times the per-frame swell and shading work of the water mesh
// (mod/Royale/features/Water.inc, DrawWaterSheet) the way it was before the speed-ups (kept below, verbatim, as the reference) and as it is
// now, and fails if the new code does not give the same numbers: bit for bit where the change is exact, within 2e-3 where it is a
// drawing-only approximation. Run: royale_water_bench (it is also a ctest test).
#include "water_sim.h"
#include "water_look.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace royale::water;

namespace ref {
// PreparedSwell as it was: all eight waves, even the ones the band limit set to zero.
class PreparedSwell {
    struct Wave { float k, a, dx, dz, phase; };
    Wave waves_[kSwellCount];
    float q_;
public:
    PreparedSwell(float t, float amp, float chop, float minLength = 0.0f) : q_(SwellSteepness(std::max(0.0f, amp), chop)) {
        for (int i = 0; i < kSwellCount; ++i) {
            const auto& w = kSwell[i];
            const float k = kTau / w.length;
            waves_[i] = {k, w.amp * std::max(0.0f, amp) * SwellBandWeight(w.length, minLength), std::cos(w.angle), std::sin(w.angle), -k * w.speed * t};
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
// ShadeWater as it was.
inline void ShadeWaterRef(const LookParams& P, const LookSky& sky, float wx, float wz, float y, float depth, float nx, float ny, float nz, float foamWake,
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

}  // namespace ref

static int failures = 0;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "water bench FAILED: %s\n", what); failures++; } }
static bool Same(const SwellPoint& a, const SwellPoint& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
static float MaxDiff(const SwellPoint& a, const SwellPoint& b) {
    return std::max({ std::fabs(a.h - b.h), std::fabs(a.ox - b.ox), std::fabs(a.oz - b.oz), std::fabs(a.nx - b.nx), std::fabs(a.ny - b.ny), std::fabs(a.nz - b.nz), std::fabs(a.fold - b.fold) });
}
template <class F> static double TimeUs(int reps, F fn) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; r++) fn(r);
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / reps;
}
static volatile float gSink = 0.0f;

// The three meshes of the water sheet at the High setting (Water.inc: 16, 64 and 256 units a square; 33, 33 and 25 points a side).
struct Level { float cell; int V; };
static const Level kLevels[3] = { { 16.0f, 33 }, { 64.0f, 33 }, { 256.0f, 25 } };
static void GridOf(const Level& L, float ex, float ez, std::vector<float>& xs, std::vector<float>& zs) {
    const int n = L.V - 1, gi0 = static_cast<int>(std::lround(ex / L.cell)), gj0 = static_cast<int>(std::lround(ez / L.cell));
    xs.resize(L.V); zs.resize(L.V);
    for (int i = 0; i < L.V; i++) { xs[i] = (gi0 + i - n / 2) * L.cell; zs[i] = (gj0 + i - n / 2) * L.cell; }
}

int main() {
    // ---- 1. PreparedSwell::Sample: the waves a mesh cannot draw are skipped. Must be bit-identical (gameplay reads Swell too).
    {
        size_t checked = 0;
        for (float amp : { 0.0f, 0.35f, 1.0f, 1.9f, 3.8f })
            for (float chop : { 0.0f, 0.5f, 1.0f })
                for (float minLength : { 0.0f, 30.0f, 2.5f * 16.0f, 2.5f * 64.0f, 2.5f * 256.0f })
                    for (float t : { 0.0f, 3.7f, 120.0f, 3600.0f }) {
                        const ref::PreparedSwell a(t, amp, chop, minLength);
                        const PreparedSwell b(t, amp, chop, minLength);
                        for (int k = 0; k < 400; k++) {
                            const float x = (k % 20) * 731.3f - 7400.0f, z = (k / 20) * 777.9f - 7700.0f;
                            Require(Same(a.Sample(x, z), b.Sample(x, z)), "PreparedSwell::Sample is bit-identical to the old one");
                            checked++;
                        }
                    }
        std::printf("swell: %zu points bit-identical to the old PreparedSwell::Sample\n", checked);
    }
    // ---- 2. SwellGrid (drawing only): the same swell from per-row and per-column tables. Within 2e-3 of the old Sample for a game that has been
    //         running up to 5 minutes; after that the old Sample's own float rounding (its phase grows with the clock: ~1e-2 units after an
    //         hour) is the whole difference, so the grid is also checked against the exact (double) value of the same waves, which it must stay
    //         within 2e-4 of at any time (it is ~1e-5: closer to the true swell than the old code).
    {
        std::vector<float> xs, zs;
        float worstOld[7] = {}, worstOldExact[7] = {}, worstExact[7] = {};
        const float times[7] = { 0.5f, 100.0f, 300.0f, 600.0f, 1200.0f, 3600.0f, 14400.0f };
        for (int ti = 0; ti < 7; ti++)
            for (float amp : { 0.35f, 1.0f, 3.8f })
                for (float chop : { 0.0f, 1.0f })
                    for (int e = 0; e < 6; e++) {
                        const float t = times[ti] + e * 0.37f, ex = -11000.0f + e * 4321.7f, ez = 9000.0f - e * 3777.3f;
                        for (const Level& L : kLevels) {
                            GridOf(L, ex, ez, xs, zs);
                            const ref::PreparedSwell old(t, amp, chop, 2.5f * L.cell);
                            const PreparedSwell now(t, amp, chop, 2.5f * L.cell);
                            SwellGrid grid;
                            grid.Prepare(now, xs.data(), L.V, zs.data(), L.V);
                            // the exact value of the same (float) wave constants, in double
                            const float q = SwellSteepness(amp, chop);
                            for (int j = 0; j < L.V; j++)
                                for (int i = 0; i < L.V; i++) {
                                    const SwellPoint g = grid.Sample(i, j), o = old.Sample(xs[i], zs[j]);
                                    double h = 0, gx = 0, gz = 0, gy = 0, ox = 0, oz = 0;
                                    for (const GerstnerWave& w : kSwell) {
                                        const float k = kTau / w.length, a = w.amp * amp * SwellBandWeight(w.length, 2.5f * L.cell), dx = std::cos(w.angle), dz = std::sin(w.angle), ph = -k * w.speed * t;
                                        const double th = static_cast<double>(k) * (static_cast<double>(dx) * xs[i] + static_cast<double>(dz) * zs[j]) + ph;
                                        h += a * std::sin(th); ox += double(q) * a * dx * std::cos(th); oz += double(q) * a * dz * std::cos(th);
                                        gx += double(dx) * k * a * std::cos(th); gz += double(dz) * k * a * std::cos(th); gy += double(q) * k * a * std::sin(th);
                                    }
                                    const double nl = std::sqrt(gx * gx + (1 - gy) * (1 - gy) + gz * gz);
                                    SwellPoint x;
                                    x.h = float(h); x.ox = float(ox); x.oz = float(oz); x.nx = float(-gx / nl); x.ny = float((1 - gy) / nl); x.nz = float(-gz / nl);
                                    x.fold = std::clamp(float(gy / 0.92), 0.0f, 1.0f);
                                    worstOld[ti] = std::max(worstOld[ti], MaxDiff(g, o));
                                    worstOldExact[ti] = std::max(worstOldExact[ti], MaxDiff(o, x));
                                    worstExact[ti] = std::max(worstExact[ti], MaxDiff(g, x));
                                }
                        }
                    }
        for (int ti = 0; ti < 7; ti++) {
            std::printf("grid swell, clock ~%6.0f s: max diff grid vs old %.2e, old vs exact %.2e, grid vs exact %.2e\n", times[ti], worstOld[ti], worstOldExact[ti], worstExact[ti]);
            Require(worstExact[ti] <= 2e-4f, "SwellGrid within 2e-4 of the exact swell");
            if (times[ti] <= 300.0f) Require(worstOld[ti] <= 2e-3f, "SwellGrid within 2e-3 of the old Sample (first 5 minutes)");
        }
    }
    // ---- timings: one frame of the water sheet's swell (all three meshes), old against new
    {
        std::vector<float> xs, zs;
        const float amp = 1.9f, chop = 0.6f;
        const int reps = 400;
        auto frame = [&](int r, auto&& sampleLevel) {
            const float t = 100.0f + r * 0.016f, ex = 1234.5f + r * 3.0f, ez = -2345.6f + r * 1.5f;
            float acc = 0.0f;
            for (const Level& L : kLevels) { GridOf(L, ex, ez, xs, zs); acc += sampleLevel(L, t, amp, chop); }
            gSink = gSink + acc;
        };
        const double before = TimeUs(reps, [&](int r) { frame(r, [&](const Level& L, float t, float a, float c) {
            const ref::PreparedSwell s(t, a, c, 2.5f * L.cell); float acc = 0;
            for (int j = 0; j < L.V; j++) for (int i = 0; i < L.V; i++) acc += s.Sample(xs[i], zs[j]).h;
            return acc; }); });
        const double skip = TimeUs(reps, [&](int r) { frame(r, [&](const Level& L, float t, float a, float c) {
            const PreparedSwell s(t, a, c, 2.5f * L.cell); float acc = 0;
            for (int j = 0; j < L.V; j++) for (int i = 0; i < L.V; i++) acc += s.Sample(xs[i], zs[j]).h;
            return acc; }); });
        const double grid = TimeUs(reps, [&](int r) { frame(r, [&](const Level& L, float t, float a, float c) {
            const PreparedSwell s(t, a, c, 2.5f * L.cell); SwellGrid g; g.Prepare(s, xs.data(), L.V, zs.data(), L.V); float acc = 0;
            for (int j = 0; j < L.V; j++) for (int i = 0; i < L.V; i++) acc += g.Sample(i, j).h;
            return acc; }); });
        std::printf("swell per frame: old %.1f us, zero waves skipped %.1f us (%.1fx), grid tables %.1f us (%.1fx)\n", before, skip, before / skip, grid, before / grid);
    }
    // ---- 3. ShadeWater (drawing only): powers by multiplying, the pin-point glint and its twinkle skipped when it is off, sqrt for hypot.
    //         Every output (0-255 colours and alphas) within 2e-3 of the old one.
    {
        auto lookOf = [](int v, LookParams& P, LookSky& sky) {
            P = LookParams{};
            P.eye[0] = 100.0f + 37.0f * (v % 7); P.eye[1] = (v % 5 == 4) ? -30.0f : 20.0f + 90.0f * (v % 4); P.eye[2] = -200.0f + 53.0f * (v % 11);
            P.t = 17.0f + v * 0.7f; P.light = 0.38f + 0.62f * ((v % 9) / 8.0f); P.wind = (v % 6) / 5.0f; P.ampTotal = 16.0f;
            P.glintAmt = 1.0f; P.foamAmt = (v % 3) ? 1.0f : 0.0f; P.skyOn = v % 13 != 0;
            P.sharpSpec = (v % 4 == 0) ? 1.0f : 0.0f;   // the game's mesh draws with 0; the preview tool with 1
            P.sparkle = v % 2; P.streaks = (v % 3) * 0.7f; P.bands = v % 2; P.haze = 1.0f; P.hazeNear = 900; P.hazeFar = 2400;
            P.depthTone = 1.0f; P.clouds = (v % 3) ? 1.0f : 0.0f; P.cover = 0.6f;
            sky = LookSky{};
            for (int q = 0; q < 3; q++) { sky.hor[q] = 150.0f + 30.0f * q; sky.zen[q] = 60.0f + 50.0f * q; }
            const float sa = 0.3f + 0.1f * (v % 10), sh = 0.05f + 0.09f * (v % 10);
            sky.sun[0] = std::cos(sa) * std::sqrt(1 - sh * sh); sky.sun[1] = sh; sky.sun[2] = std::sin(sa) * std::sqrt(1 - sh * sh);
            sky.moon[0] = -sky.sun[0]; sky.moon[1] = 0.3f; sky.moon[2] = -sky.sun[2];
            sky.sunA = (v % 3) / 2.0f; sky.moonA = 1.0f - sky.sunA; sky.sunWarm = (v % 4) / 3.0f;
        };
        float worst = 0.0f;
        size_t checked = 0;
        for (int v = 0; v < 240; v++) {
            LookParams P; LookSky sky;
            lookOf(v, P, sky);
            const PreparedSwell sw(P.t, 1.9f, 0.7f);
            for (int k = 0; k < 600; k++) {
                const float wx = (k % 30) * 61.0f - 900.0f + v, wz = (k / 30) * 97.0f - 950.0f, depth = 0.5f + (k % 17) * 23.0f;
                const SwellPoint sp = sw.Sample(wx, wz);
                Look a{}, b{};
                ref::ShadeWaterRef(P, sky, wx, wz, sp.h, depth, sp.nx, sp.ny, sp.nz, (k % 5) * 0.2f, sp.fold, sp.h, (k % 97) / 96.0f, 0.3f + 0.7f * ((k % 7) / 6.0f), a);
                ShadeWater(P, sky, wx, wz, sp.h, depth, sp.nx, sp.ny, sp.nz, (k % 5) * 0.2f, sp.fold, sp.h, (k % 97) / 96.0f, 0.3f + 0.7f * ((k % 7) / 6.0f), b);
                const float* fa = &a.col[0]; const float* fb = &b.col[0];
                for (size_t f = 0; f < sizeof(Look) / sizeof(float); f++) worst = std::max(worst, std::fabs(fa[f] - fb[f]));
                checked++;
            }
        }
        std::printf("shading: %zu points, max diff of any colour or alpha (0-255) %.2e\n", checked, worst);
        Require(worst <= 2e-3f, "ShadeWater within 2e-3 of the old one");
        // timing: one frame's worth of points (2803) with the game's settings (sharpSpec 0, the open-sea passes on)
        LookParams P; LookSky sky;
        lookOf(1, P, sky);
        P.sharpSpec = 0.0f; P.skyOn = true; P.sparkle = 1; P.streaks = 1; P.bands = 1; P.clouds = 1;
        sky.sunA = 0.8f; sky.moonA = 0.1f;
        std::vector<SwellPoint> pts(2803);
        const PreparedSwell sw(50.0f, 1.9f, 0.7f);
        for (size_t k = 0; k < pts.size(); k++) pts[k] = sw.Sample((k % 53) * 31.0f - 800.0f, (k / 53) * 29.0f - 700.0f);
        auto run = [&](auto&& shade) {
            float acc = 0.0f;
            for (size_t k = 0; k < pts.size(); k++) {
                Look o{};
                const SwellPoint& sp = pts[k];
                shade(P, sky, (k % 53) * 31.0f - 800.0f, (k / 53) * 29.0f - 700.0f, sp.h, 40.0f + (k % 300), sp.nx, sp.ny, sp.nz, 0.0f, sp.fold, sp.h, (k % 97) / 96.0f, 1.0f, o);
                acc += o.col[0] + o.glint[3];
            }
            gSink = gSink + acc;
        };
        const double before = TimeUs(400, [&](int) { run(ref::ShadeWaterRef); });
        const double after = TimeUs(400, [&](int) { run(ShadeWater); });
        std::printf("shading per frame (2803 points): old %.1f us, new %.1f us (%.1fx)\n", before, after, before / after);
    }
    // ---- 4. the "what is under each point" lookups DrawWaterSheet now skips while the meshes have not moved (exact: the same values are kept).
    //         A model of the old per-frame cost: 2803 hash lookups and copies out of the cell memory.
    {
        struct Cell { float surface, depth; bool have; };
        std::unordered_map<uint64_t, Cell> cells[3];
        std::vector<Cell> under[3];
        auto key = [](int gi, int gj) { return (static_cast<uint64_t>(static_cast<uint32_t>(gi + 100000)) << 32) | static_cast<uint32_t>(gj + 100000); };
        for (int l = 0; l < 3; l++)
            for (int j = -40; j < 40; j++) for (int i = -40; i < 40; i++) cells[l][key(i, j)] = { float(i), float(j), ((i ^ j) & 3) != 0 };
        const double lookups = TimeUs(2000, [&](int) {
            for (int l = 0; l < 3; l++) {
                const int V = kLevels[l].V;
                under[l].assign(static_cast<size_t>(V) * V, Cell{ 0, 0, false });
                for (int j = 0; j < V; j++) for (int i = 0; i < V; i++) under[l][static_cast<size_t>(j) * V + i] = cells[l].find(key(i - V / 2, j - V / 2))->second;
            }
            gSink = gSink + under[2][7].surface;
        });
        std::printf("cell lookups per frame (model of the old step 1): %.1f us, now skipped while the meshes stay put\n", lookups);
    }
    if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    return 0;
}
