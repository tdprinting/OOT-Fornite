#include "water_sim.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace royale::water;
void require(bool ok, const char* what) { if (!ok) { std::cerr << "water test failed: " << what << "\n"; std::exit(1); } }
int main() {
    for (float amp : {0.0f, 0.8f, 1.9f}) for (float chop : {0.0f, 0.5f, 1.0f}) {
        PreparedSwell prepared(42.0f, amp, chop);
        for (int i = 0; i < 200; ++i) {
            const float x = i * 73.0f - 7000, z = i * 51.0f - 6000;
            const auto p = prepared.Sample(x, z);
            float h = 0, ox = 0, oz = 0;
            const float q = SwellSteepness(amp, chop);
            for (const auto& w : kSwell) {
                const float k = kTau/w.length, dx = std::cos(w.angle), dz = std::sin(w.angle);
                const float phase = k*(dx*x + dz*z) - k*w.speed*42.0f;
                h += w.amp*amp*std::sin(phase);
                ox += q*w.amp*amp*dx*std::cos(phase); oz += q*w.amp*amp*dz*std::cos(phase);
            }
            require(std::fabs(p.h-h)<0.001f && std::fabs(p.ox-ox)<0.001f && std::fabs(p.oz-oz)<0.001f,
                    "prepared swell preserves the original wave geometry");
        }
    }
    // The swell: unit normals, crests that never loop over, heights within the waves' total, nothing at zero amplitude.
    float total = 0.0f;
    for (const GerstnerWave& w : kSwell) total += w.amp;
    float hi = -1e9f, lo = 1e9f, maxFold = 0.0f;
    for (int k = 0; k < 4000; k++) {
        const float x = (k % 63) * 37.0f - 1100.0f, z = (k / 63) * 41.0f - 1300.0f, t = k * 0.013f;
        const SwellPoint p = Swell(x, z, t, 1.9f, 1.0f);
        require(std::fabs(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz - 1.0f) < 1e-3f, "swell normal is unit length");
        require(p.ny > 0.0f, "swell normal points up (crests never loop over)");
        require(std::fabs(p.h) <= total * 1.9f + 1e-3f, "swell height within the waves' total");
        hi = std::max(hi, p.h); lo = std::min(lo, p.h); maxFold = std::max(maxFold, p.fold);
    }
    require(hi - lo > total * 1.2f, "the swell is big enough to see");
    require(maxFold > 0.5f, "crests fold enough for whitecaps in a strong swell");
    require(Swell(10, 20, 3, 0.0f, 1.0f).h == 0.0f, "flat at zero amplitude");
    require(SwellSteepness(1.9f, 1.0f) * 1.9f > 0.0f, "choppy waves have a sideways shift");
    const SwellPoint round = Swell(100, 50, 2, 1.0f, 0.0f);
    require(round.ox == 0.0f && round.oz == 0.0f, "no chop means no sideways shift");

    // The ripple field: a push spreads out as a ring, stays stable, fades away, and stays put in the world when the field moves.
    RippleField f(14.0f, 120.0f, 0.55f);
    f.Recenter(0, 0);
    f.Impulse(0, 0, 8.0f, 20.0f);
    require(f.Sample(0, 0) < -3.0f, "a push dips the surface");
    for (int i = 0; i < 30; i++) f.Step(1.0f / 60.0f);
    float ring = 0.0f;
    for (int a = 0; a < 16; a++) ring = std::max(ring, std::fabs(f.Sample(std::cos(a * 0.39f) * 60.0f, std::sin(a * 0.39f) * 60.0f)));
    require(ring > 0.05f, "the ring reaches 60 units in half a second");
    const float e0 = f.Energy();
    for (int i = 0; i < 2000; i++) f.Step(1.0f / 60.0f);
    require(f.Energy() < e0 * 0.05f && f.Activity() < 0.5f, "the ripples die down and never blow up");
    f.Clear();
    f.Impulse(100, -40, 6.0f, 20.0f);
    const float before = f.Sample(100, -40);
    f.Recenter(40, 10);
    require(std::fabs(f.Sample(100, -40) - before) < 1e-4f, "moving the field keeps what is in it where it is");
    f.Recenter(100000, 0);
    require(f.Activity() == 0.0f, "jumping far starts calm");
    for (int i = 0; i < 1000; i++) { f.Impulse(100000 + (i % 7) * 9.0f, (i % 5) * 11.0f, 9.0f, 18.0f); f.Step(1.0f / 30.0f); }
    require(std::isfinite(f.Energy()) && f.Activity() < 200.0f, "constant pushing stays bounded");

    // The textures tile, loop, and have both dark ground and bright pattern.
    constexpr int S = 64, F = 16;
    std::vector<uint8_t> caus(S * S * F), glint(S * S * F), foam(S * S);
    MakeCaustics(caus.data(), S, F);
    MakeGlints(glint.data(), S, F);
    MakeFoam(foam.data(), S);
    for (const std::vector<uint8_t>* img : { &caus, &glint }) {
        for (int fr = 0; fr < F; fr++) require(SeamRatio(img->data() + fr * S * S, S) <= 1.0f, "an animated texture tiles");
        double loop = 0.0, step = 0.0;
        for (int k = 0; k < S * S; k++) {
            loop += std::abs((*img)[(F - 1) * S * S + k] - (*img)[k]);
            step += std::abs((*img)[S * S + k] - (*img)[k]);
        }
        require(loop < step * 1.6 + S * S, "the last frame runs into the first");
        int dark = 0, bright = 0;
        for (int k = 0; k < S * S; k++) { dark += (*img)[k] < 40; bright += (*img)[k] > 160; }
        require(dark > S * S / 5 && bright > S * S / 200, "a texture has dark ground and bright pattern");
    }
    require(SeamRatio(foam.data(), S) <= 1.0f, "foam tiles");
    if (const char* dump = std::getenv("WATER_DUMP")) {   // writes the textures as PGM images to look at
        auto save = [&](const char* name, const uint8_t* p, int w, int h) {
            std::string path = std::string(dump) + "/" + name + ".pgm";
            FILE* fp = std::fopen(path.c_str(), "wb");
            if (fp == nullptr) return;
            std::fprintf(fp, "P5 %d %d 255\n", w, h);
            std::fwrite(p, 1, static_cast<size_t>(w) * h, fp);
            std::fclose(fp);
        };
        save("caustics", caus.data(), S, S * F);
        save("glints", glint.data(), S, S * F);
        save("foam", foam.data(), S, S);
    }
    std::cout << "water tests passed\n";
    return 0;
}
