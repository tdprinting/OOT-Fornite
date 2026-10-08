// A software preview of the water, made with the game's own water code (shared/water_sim.h: the swell, the textures; shared/water_look.h:
// the shading). The game shades the water at the points of its grid and blends between them; so does this (fine squares of 29 units near the
// player, coarse ones of 232 further off), then lays the caustics, the water, the sparkle and the foam over the floor the way the game's
// translucent passes do. It does not draw the game's models, only a sandy floor, a beach and a sky.
// Build and run:  g++ -O2 -std=c++17 -Ishared tools/water/preview.cpp -o /tmp/water_preview && /tmp/water_preview out_dir
// It writes before.ppm, midday.ppm, sunset.ppm, underwater.ppm.
#include "water_look.h"
#include "water_sim.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace rw = royale::water;
namespace {
constexpr int W = 640, H = 360, kTex = 64, kFrames = 32;
std::vector<uint8_t> gCaus(kTex * kTex * kFrames), gGlint(kTex * kTex * kFrames), gFoam(kTex * kTex);

struct Scene {
    float eye[3], yaw, pitch;
    float day, twilight, sunH;   // as the game's SkyLightNow
    float amp, chop, glintAmt, foamAmt, causticAmt, clarity, wind, t;
};

float Tex(const std::vector<uint8_t>& img, int frame, float s, float t) {   // bilinear, wrapping, in texels
    s -= 0.5f; t -= 0.5f;
    const float fs = std::floor(s), ft = std::floor(t), a = s - fs, b = t - ft;
    auto at = [&](int x, int y) { return img[frame * kTex * kTex + ((y % kTex + kTex) % kTex) * kTex + ((x % kTex + kTex) % kTex)] / 255.0f; };
    const int x = static_cast<int>(fs), y = static_cast<int>(ft);
    return (at(x, y) * (1 - a) + at(x + 1, y) * a) * (1 - b) + (at(x, y + 1) * (1 - a) + at(x + 1, y + 1) * a) * b;
}

float FloorDepth(float x, float z) {   // how deep the water is: a beach in front of the camera, shelving into a lagoon and the deep
    return (z - 260.0f) * 0.32f + 26.0f * std::sin(x * 0.006f) + 14.0f * std::sin(x * 0.013f + z * 0.004f);
}

rw::LookSky SkyFor(const Scene& sc) {   // as the game's WaterSkyNow, clear weather
    rw::LookSky s{};
    auto mix = [](float a, float b, float k) { return a + (b - a) * k; };
    const float nz[3] = { 5, 8, 30 }, nh[3] = { 22, 28, 66 }, dz[3] = { 62, 122, 224 }, dh[3] = { 168, 206, 242 };
    const float glow[3] = { 255, 128, 62 }, glowZ[3] = { 96, 78, 150 };
    for (int i = 0; i < 3; i++) {
        s.zen[i] = mix(nz[i], dz[i], sc.day); s.hor[i] = mix(nh[i], dh[i], sc.day);
        s.zen[i] = mix(s.zen[i], glowZ[i], sc.twilight * 0.55f); s.hor[i] = mix(s.hor[i], glow[i], sc.twilight * 0.8f);
    }
    const float c = std::sqrt(std::max(0.0f, 1.0f - sc.sunH * sc.sunH));
    s.sun[0] = std::cos(0.6f) * c; s.sun[1] = sc.sunH; s.sun[2] = std::sin(0.6f) * c;
    for (int i = 0; i < 3; i++) s.moon[i] = -s.sun[i];
    s.sunA = std::clamp(sc.sunH * 6.0f + 0.4f, 0.0f, 1.0f);
    s.moonA = std::clamp(-sc.sunH * 6.0f + 0.4f, 0.0f, 1.0f);
    s.sunWarm = sc.twilight;
    return s;
}

struct Point { rw::Look look; float nx, nz; };

void Render(const Scene& sc, const char* path) {
    const rw::LookSky sky = SkyFor(sc);
    const float light = 0.38f + 0.62f * std::clamp(sc.day * 1.4f, 0.0f, 1.0f);
    float ampTotal = 0.0f;
    for (const rw::GerstnerWave& w : rw::kSwell) ampTotal += w.amp * sc.amp;
    rw::LookParams P;
    P.eye[0] = sc.eye[0]; P.eye[1] = sc.eye[1]; P.eye[2] = sc.eye[2];
    P.t = sc.t; P.light = light; P.reach = 60.0f + 320.0f * sc.clarity; P.ampTotal = ampTotal; P.wind = sc.wind;
    P.glintAmt = sc.glintAmt; P.foamAmt = sc.foamAmt;
    const bool under = sc.eye[1] < 0.0f;
    // the game's grid points, shaded once each
    std::unordered_map<uint64_t, Point> cache;
    auto point = [&](int gi, int gj, float g) -> const Point& {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(gi + 100000)) << 32) | static_cast<uint32_t>(gj + 100000) | (g > 100 ? (1ULL << 63) : 0);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        const float wx = gi * g, wz = gj * g, depth = std::max(0.0f, FloorDepth(wx, wz));
        const float shore = std::min(1.0f, depth / 90.0f);
        const rw::SwellPoint sp = rw::Swell(wx, wz, sc.t, sc.amp, sc.chop);
        float nx = sp.nx * shore, ny = sp.ny, nz = sp.nz * shore;
        const float l = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= l; ny /= l; nz /= l;
        Point p;
        const float d = std::hypot(wx - sc.eye[0], wz - sc.eye[2]), fade = std::clamp((2550.0f - d) / (2550.0f * 0.35f), 0.0f, 1.0f);
        rw::ShadeWater(P, sky, wx, wz, std::max(-34.0f, sp.h * shore), depth, nx, ny, nz, 0.0f, sp.fold * shore, sp.h * shore, rw::Hash01(gi, gj, 611), fade, p.look);
        p.nx = nx; p.nz = nz;
        return cache.emplace(key, p).first->second;
    };
    std::vector<uint8_t> img(W * H * 3);
    const float fov = 1.0f / std::tan(0.5f * 1.05f);
    const float cy = std::cos(sc.yaw), sy = std::sin(sc.yaw), cp = std::cos(sc.pitch), spch = std::sin(sc.pitch);
    const int frame = static_cast<int>(sc.t * 11.0f) % kFrames, gframe = static_cast<int>(sc.t * 9.0f) % kFrames;
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            const float u = (px + 0.5f - W * 0.5f) / (H * 0.5f), v = -(py + 0.5f - H * 0.5f) / (H * 0.5f);
            // camera looks along +z turned by yaw, tilted by pitch
            float dx = u, dy = v * cp + fov * spch, dz = -v * spch + fov * cp;
            const float rx = dx * cy + dz * sy, rz = -dx * sy + dz * cy;
            dx = rx; dz = rz;
            const float dl = std::sqrt(dx * dx + dy * dy + dz * dz); dx /= dl; dy /= dl; dz /= dl;
            float col[3];
            // the sky (and the sun's disc)
            const float up = std::clamp(dy, 0.0f, 1.0f);
            for (int q = 0; q < 3; q++) col[q] = sky.hor[q] + (sky.zen[q] - sky.hor[q]) * std::pow(up, 0.55f);
            const float sd = dx * sky.sun[0] + dy * sky.sun[1] + dz * sky.sun[2];
            if (sd > 0.9985f) { col[0] = 255; col[1] = 245 - 50 * sky.sunWarm; col[2] = 220 - 110 * sky.sunWarm; }
            else if (sd > 0.97f) { const float g = (sd - 0.97f) / 0.0285f; col[0] += (255 - col[0]) * g * 0.6f; col[1] += (220 - col[1]) * g * 0.5f; col[2] += (170 - col[2]) * g * 0.3f; }
            // the floor along the ray (marched), and where the ray crosses the water
            float floorT = -1.0f;
            for (float tt = 2.0f; tt < 6000.0f; tt *= 1.012f) {
                const float x = sc.eye[0] + dx * tt, y = sc.eye[1] + dy * tt, z = sc.eye[2] + dz * tt;
                if (y < -std::max(-18.0f, FloorDepth(x, z))) { floorT = tt; break; }
                if (y > 400.0f && dy > 0) break;
            }
            const float waterT = std::fabs(dy) > 1e-4f ? -sc.eye[1] / dy : -1.0f;
            const bool hitsWater = waterT > 0.0f && (floorT < 0.0f || waterT < floorT);
            if (floorT > 0.0f && (!hitsWater || !under)) {
                const float x = sc.eye[0] + dx * floorT, z = sc.eye[2] + dz * floorT, depth = FloorDepth(x, z);
                const float sand = depth < 0.0f ? 1.0f : 0.8f;
                col[0] = 206 * sand * light; col[1] = 186 * sand * light; col[2] = 138 * sand * light;
                if (depth > 1.0f) {   // the caustics pass
                    const float depthK = std::clamp((depth - 3.0f) / 30.0f, 0.0f, 1.0f) * (1.0f - std::clamp((depth - 220.0f) / 420.0f, 0.0f, 1.0f));
                    const float amt = sc.causticAmt * (0.25f + 0.75f * sky.sunA) * light * light * (under ? 1.35f : 1.0f);
                    const float a = std::clamp(215.0f * amt * depthK / 255.0f, 0.0f, 1.0f);
                    const float sway = 2.2f * std::sin(sc.t * 0.9f + x * 0.011f) + 1.6f * std::cos(sc.t * 0.7f + z * 0.013f);
                    const float tex = Tex(gCaus, frame, x / 3.4f + sc.t * 1.1f + sway, z / 3.4f + sc.t * 0.66f - sway * 0.5f) * a;
                    const float cc[3] = { 200 + 55 * sky.sunWarm, 255 - 30 * sky.sunWarm, 238 - 80 * sky.sunWarm };
                    for (int q = 0; q < 3; q++) col[q] += (cc[q] - col[q]) * tex;
                }
                if (under) {   // the water between the camera and the floor
                    const float k = 1.0f - std::exp(-floorT / 520.0f);
                    const float fogc[3] = { 18 * light, 112 * light, 132 * light };
                    for (int q = 0; q < 3; q++) col[q] += (fogc[q] - col[q]) * k;
                }
            }
            if (hitsWater && waterT < 6000.0f) {
                const float x = sc.eye[0] + dx * waterT, z = sc.eye[2] + dz * waterT;
                const float d = std::hypot(x - sc.eye[0], z - sc.eye[2]);
                const float g = d < 450.0f ? 29.0f : 232.0f;   // the game's fine squares near, coarse further off
                const int gi = static_cast<int>(std::floor(x / g)), gj = static_cast<int>(std::floor(z / g));
                const float fu = x / g - gi, fw = z / g - gj;
                const Point &p00 = point(gi, gj, g), &p10 = point(gi + 1, gj, g), &p01 = point(gi, gj + 1, g), &p11 = point(gi + 1, gj + 1, g);
                auto bl = [&](float a, float b, float c, float e) { return (a * (1 - fu) + b * fu) * (1 - fw) + (c * (1 - fu) + e * fu) * fw; };
                if (FloorDepth(x, z) > 0.0f) {
                    rw::Look L{};
                    for (int q = 0; q < 4; q++) {
                        L.col[q] = bl(p00.look.col[q], p10.look.col[q], p01.look.col[q], p11.look.col[q]);
                        L.glint[q] = bl(p00.look.glint[q], p10.look.glint[q], p01.look.glint[q], p11.look.glint[q]);
                    }
                    L.foam = bl(p00.look.foam, p10.look.foam, p01.look.foam, p11.look.foam);
                    const float nx = bl(p00.nx, p10.nx, p01.nx, p11.nx), nz = bl(p00.nz, p10.nz, p01.nz, p11.nz);
                    const float a = std::clamp(L.col[3] / 255.0f, 0.0f, 1.0f);
                    for (int q = 0; q < 3; q++) col[q] += (std::clamp(L.col[q], 0.0f, 255.0f) - col[q]) * a;
                    if (sc.glintAmt > 0.0f) {
                        const float ga = std::clamp(L.glint[3] / 255.0f, 0.0f, 1.0f) * Tex(gGlint, gframe, x / 3.0f + sc.t * 1.7f + nx * 9.0f, z / 3.0f + sc.t * 1.1f + nz * 9.0f);
                        for (int q = 0; q < 3; q++) col[q] += (std::clamp(L.glint[q], 0.0f, 255.0f) - col[q]) * ga;
                    }
                    if (sc.foamAmt > 0.0f) {
                        const float fa = std::clamp(L.foam / 255.0f, 0.0f, 1.0f) * Tex(gFoam, 0, x / 2.6f + nx * 4.0f, z / 2.6f + nz * 4.0f);
                        const float fl = 0.6f + 0.4f * light;
                        const float fc[3] = { 240 * fl, 248 * fl, 252 * fl };
                        for (int q = 0; q < 3; q++) col[q] += (fc[q] - col[q]) * fa;
                    }
                    if (under) {
                        const float k = 1.0f - std::exp(-waterT / 520.0f);
                        const float fogc[3] = { 18 * light, 112 * light, 132 * light };
                        for (int q = 0; q < 3; q++) col[q] += (fogc[q] - col[q]) * k;
                    }
                }
            } else if (under && floorT < 0.0f) {
                for (int q = 0; q < 3; q++) col[q] = (q == 0 ? 18 : q == 1 ? 112 : 132) * light;
            }
            if (under) {   // the screen overlay (DrawUnderwaterOverlay): teal gradient and light shafts
                const float yk = static_cast<float>(py) / H, k = 1.0f;
                const float top[3] = { 70 * light, 200 * light, 212 * light }, bot[3] = { 6, 58 * light, 104 * light };
                const float al = ((70 + (140 - 70) * yk) / 255.0f) * k;
                for (int q = 0; q < 3; q++) col[q] += ((top[q] + (bot[q] - top[q]) * yk) - col[q]) * al;
                for (int i = 0; i < 8; i++) {
                    const float x0 = W * (0.04f + 0.125f * i + 0.04f * std::sin(sc.t * 0.3f + i * 1.9f)), lean = H * (0.22f + 0.06f * std::sin(sc.t * 0.21f + i));
                    const float len = H * (0.75f + 0.25f * std::sin(i * 2.3f)), pulse = 0.6f + 0.4f * std::sin(sc.t * 0.8f + i * 2.7f);
                    if (py > len) continue;
                    const float cxl = x0 + lean * py / len;
                    for (int layer = 0; layer < 2; layer++) {
                        const float w = W * (layer == 0 ? 0.06f : 0.022f) * (0.7f + 0.3f * std::sin(sc.t * 0.5f + i));
                        if (std::fabs(px - cxl) < w * 0.5f) {
                            const float sa = 20.0f * light * pulse * (layer == 0 ? 0.7f : 1.3f) / 255.0f;
                            const float sc3[3] = { 200, 255, 245 };
                            for (int q = 0; q < 3; q++) col[q] += (sc3[q] - col[q]) * sa;
                        }
                    }
                }
            }
            for (int q = 0; q < 3; q++) img[(py * W + px) * 3 + q] = static_cast<uint8_t>(std::clamp(col[q], 0.0f, 255.0f));
        }
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return;
    std::fprintf(f, "P6 %d %d 255\n", W, H);
    std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
}
}  // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    rw::MakeCaustics(gCaus.data(), kTex, kFrames);
    rw::MakeGlints(gGlint.data(), kTex, kFrames);
    rw::MakeFoam(gFoam.data(), kTex);
    //                 eye                    yaw    pitch   day  twi  sunH   amp   chop glint foam caus clar wind  t
    const Scene before{ { 0, 70, 140 },        0.35f, -0.16f, 1.0f, 0.0f, 0.9f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 7.3f };
    const Scene midday{ { 0, 70, 140 },        0.35f, -0.16f, 1.0f, 0.0f, 0.9f, 0.96f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 7.3f };
    const Scene sunset{ { 0, 80, 140 },        0.92f, -0.10f, 0.45f, 1.0f, 0.08f, 1.2f, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 0.4f, 7.3f };
    const Scene under{ { 0, -60, 600 },        0.35f, 0.5f, 1.0f, 0.0f, 0.9f, 0.96f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 7.3f };
    Render(before, (out + "/before.ppm").c_str());
    Render(midday, (out + "/midday.ppm").c_str());
    Render(sunset, (out + "/sunset.ppm").c_str());
    Render(under, (out + "/underwater.ppm").c_str());
    std::puts("water previews written");
    return 0;
}
