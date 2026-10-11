#include "water_sim.h"
#include "water_look.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
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

    // A grid only gets waves it can draw: neighbouring points on a coarse grid tilt alike (no patchwork of squares), and 0 keeps every wave.
    require(SwellBandWeight(80.0f, 0.0f) == 1.0f && SwellBandWeight(80.0f, 425.0f) == 0.0f && SwellBandWeight(900.0f, 425.0f) == 1.0f,
            "short waves fade out of a coarse grid, long ones stay");
    for (float cell : {105.0f, 170.0f, 225.0f, 364.0f}) {
        const PreparedSwell coarse(7.0f, 1.9f, 1.0f, 2.5f * cell), raw(7.0f, 1.9f, 1.0f);
        float jumpCoarse = 0.0f, jumpRaw = 0.0f;
        for (int i = 0; i < 400; i++) {
            const float x = (i % 20) * cell, z = (i / 20) * cell;
            const SwellPoint a = coarse.Sample(x, z), b = coarse.Sample(x + cell, z), c = raw.Sample(x, z), d = raw.Sample(x + cell, z);
            jumpCoarse = std::max(jumpCoarse, std::fabs(a.nx - b.nx) + std::fabs(a.nz - b.nz));
            jumpRaw = std::max(jumpRaw, std::fabs(c.nx - d.nx) + std::fabs(c.nz - d.nz));
        }
        require(jumpCoarse < jumpRaw * 0.75f, "band-limited swell tilts smoothly from point to point");
    }

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

    // Bright sun paths stay bounded without flattening ordinary ripples to white.
    LookParams look;
    look.eye[1] = 100.0f;
    LookSky sky{};
    sky.sun[1] = 1.0f; sky.sunA = 1.0f;
    for (int q = 0; q < 3; ++q) { sky.hor[q] = 180.0f; sky.zen[q] = 120.0f; }
    Look shaded{};
    ShadeWater(look, sky, 0, 0, 0, 400, 0, 1, 0, 0, 0, 0, 0.5f, 1, shaded);
    require(shaded.glint[3] > 0 && shaded.glint[3] < 160, "sun highlights retain headroom");
    const float fullGlint = shaded.glint[3];
    ShadeWater(look, sky, 0, 0, 0, 400, 0, 1, 0, 0, 0, 0, 0.5f, 0.5f, shaded);
    require(std::fabs(shaded.glint[3] - fullGlint * 0.5f) < 0.001f, "highlights fade continuously");
    look.glintAmt = 0;
    ShadeWater(look, sky, 0, 0, 0, 400, 0, 1, 0, 0, 0, 0, 0.5f, 1, shaded);
    require(shaded.glint[3] == 0, "disabled glints stay disabled");

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

    // The open-sea textures: the sparkle is sparse specks that loop, the streaks run along s and tile, the swell bands loop and tile.
    std::vector<uint8_t> sparkle(S * S * F), streaks(S * S), bands(S * S * F);
    MakeSparkle(sparkle.data(), S, F);
    MakeStreaks(streaks.data(), S);
    MakeSwellBands(bands.data(), S, F);
    for (const std::vector<uint8_t>* img : { &sparkle, &bands }) {
        for (int fr = 0; fr < F; fr++) require(SeamRatio(img->data() + fr * S * S, S) <= 1.0f, "an open-sea flipbook tiles");
        double loop = 0.0, step = 0.0;
        for (int k = 0; k < S * S; k++) {
            loop += std::abs((*img)[(F - 1) * S * S + k] - (*img)[k]);
            step += std::abs((*img)[S * S + k] - (*img)[k]);
        }
        require(loop < step * 1.6 + S * S, "an open-sea flipbook's last frame runs into the first");
    }
    {
        double mean = 0.0; int bright = 0;
        for (int k = 0; k < S * S * F; k++) { mean += sparkle[k]; bright += sparkle[k] > 200; }
        require(mean / (S * S * F) < 24.0 && bright > F * 4, "the sparkle is sparse: mostly dark with bright specks");
        // along s (the wind) the streaks change little, across it a lot
        double along = 0.0, across = 0.0;
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                along += std::abs(streaks[y * S + (x + 1) % S] - streaks[y * S + x]);
                across += std::abs(streaks[((y + 1) % S) * S + x] - streaks[y * S + x]);
            }
        require(SeamRatio(streaks.data(), S) <= 1.2f && across > along * 2.0, "the streaks tile and run along s");
        int dark = 0;
        for (int k = 0; k < S * S; k++) dark += bands[k] < 40;
        require(dark > S * S / 3, "the swell bands are mostly dark, with lit faces");
    }
    // The open-sea shading: far water melts into the sky's horizon colour and stays opaque; troughs are darker than crests; the bands pass is
    // only given alpha when asked for; the sparkle gathers on the sun's path.
    {
        LookParams P;
        P.eye[0] = 0; P.eye[1] = 120.0f; P.eye[2] = 0; P.ampTotal = 16.0f; P.hazeNear = 500.0f; P.hazeFar = 1500.0f;
        LookSky sk{};
        sk.sun[1] = 1.0f; sk.sunA = 1.0f;
        for (int q = 0; q < 3; ++q) { sk.hor[q] = 200.0f; sk.zen[q] = 90.0f; }
        Look nearL{}, farL{}, farOff{};
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, nearL);
        ShadeWater(P, sk, 0, 2000, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, farL);
        P.haze = 0.0f;
        ShadeWater(P, sk, 0, 2000, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, farOff);
        P.haze = 1.0f;
        float dFar = 0, dOff = 0;
        for (int q = 0; q < 3; q++) { dFar += std::fabs(farL.col[q] - (sk.hor[q] * 0.92f + 12.0f)); dOff += std::fabs(farOff.col[q] - (sk.hor[q] * 0.92f + 12.0f)); }
        require(dFar < dOff * 0.5f && farL.col[3] > 200.0f, "far water hazes toward the horizon colour and stays opaque");
        Look crestL{}, troughL{};
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 0, 8.0f, 0.5f, 1, crestL);
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 0, -8.0f, 0.5f, 1, troughL);
        const float lc = crestL.col[0] + crestL.col[1] + crestL.col[2], lt = troughL.col[0] + troughL.col[1] + troughL.col[2];
        require(lt < lc, "troughs are darker than crests");
        require(nearL.band[3] == 0.0f && nearL.streak == 0.0f, "no bands or streaks unless asked");
        P.bands = 1.0f; P.streaks = 1.0f; P.wind = 1.0f;
        Look open{};
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 1.0f, 8.0f, 0.5f, 1, open);
        require(open.band[3] > 20.0f && open.streak > 20.0f, "bands and whitecap streaks show when asked");
        Look plain{}, banded{};
        P.bands = 0.0f; P.streaks = 0.0f;
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, plain);
        P.bands = 1.0f;
        ShadeWater(P, sk, 0, 300, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, banded);
        require(banded.col[0] + banded.col[1] + banded.col[2] < plain.col[0] + plain.col[1] + plain.col[2], "under the bands the water is a shade darker");
        // the sparkle: the point under the sun's glitter path shines far more than one well off it
        P.sparkle = 1.0f; P.eye[1] = 50.0f;
        LookSky low = sk; low.sun[0] = 0.0f; low.sun[1] = 0.3f; low.sun[2] = 0.954f;
        Look onPath{}, offPath{};
        ShadeWater(P, low, 0, 300, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, onPath);
        ShadeWater(P, low, 0, -300, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, offPath);
        require(onPath.glint[3] > offPath.glint[3] * 1.5f, "the sparkle gathers on the sun's path");
    }
    // The game's own 256 x 256 textures (the water's baseline since the engine limits were raised): same checks, at the parameters the game uses.
    {
        constexpr int B = 256, G = 4;   // four frames are enough to test the loop; the game makes sixteen
        std::vector<uint8_t> c256(static_cast<size_t>(B) * B * G), g256(static_cast<size_t>(B) * B * G), sp256(static_cast<size_t>(B) * B * G), band256(static_cast<size_t>(B) * B * G);
        std::vector<uint8_t> foam256(B * B), streak256(B * B), cloud256(B * B), atlas(B * B);
        MakeCaustics(c256.data(), B, G, 11, 4, 12, 0.75f, true, 0.0021f, 20);
        MakeGlints(g256.data(), B, G, 23, 6, 14, 0.0021f, 20);
        MakeSparkle(sp256.data(), B, G, 41, 130, 2.0f, 2);
        MakeSwellBands(band256.data(), B, G);
        MakeFoam(foam256.data(), B, 37, 10);
        MakeStreaks(streak256.data(), B, 53, 2);
        MakeClouds(cloud256.data(), B);
        MakeSplashAtlas(atlas.data(), B);
        for (const std::vector<uint8_t>* img : { &c256, &g256, &sp256, &band256 })
            for (int fr = 0; fr < G; fr++) require(SeamRatio(img->data() + static_cast<size_t>(fr) * B * B, B) <= 1.0f, "a 256 flipbook tiles");
        for (const std::vector<uint8_t>* img : { &foam256, &streak256, &cloud256 }) require(SeamRatio(img->data(), B) <= 1.0f, "a 256 texture tiles");
        for (const std::vector<uint8_t>* img : { &c256, &g256 }) {
            int dark = 0, bright = 0;
            for (int k = 0; k < B * B; k++) { dark += (*img)[k] < 40; bright += (*img)[k] > 160; }
            require(dark > B * B / 5 && bright > B * B / 200, "a 256 caustic texture has dark ground and bright pattern");
        }
        // the clouds: masses and gaps, both
        int thick = 0, clear = 0;
        for (int k = 0; k < B * B; k++) { thick += cloud256[k] > 170; clear += cloud256[k] < 40; }
        require(thick > B * B / 20 && clear > B * B / 5, "the clouds have both masses and gaps");
        // the sprite atlas: each of its four cells has something in it, and an empty margin (so filtering never bleeds one into the next)
        for (int cell = 0; cell < 4; cell++) {
            const int x0 = (cell & 1) * (B / 2), y0 = (cell >> 1) * (B / 2);
            int lit = 0, edge = 0;
            for (int y = 0; y < B / 2; y++)
                for (int x = 0; x < B / 2; x++) {
                    const uint8_t v = atlas[(y0 + y) * B + x0 + x];
                    lit += v > 128;
                    if (x == 0 || y == 0 || x == B / 2 - 1 || y == B / 2 - 1) edge += v;
                }
            require(lit > 40, "an atlas cell has a bright sprite");
            require(edge == 0, "an atlas cell has an empty margin");
        }
        // the finer swell: the new short waves exist on a fine mesh and are left off a coarse one
        const PreparedSwell fineSwell(5.0f, 1.0f, 0.5f, 2.5f * 16.0f), coarseSwell(5.0f, 1.0f, 0.5f, 2.5f * 256.0f);
        double fineRough = 0.0, coarseRough = 0.0;
        for (int i = 0; i < 400; i++) {
            fineRough += std::fabs(fineSwell.Sample(i * 3.0f + 7.0f, 40.0f).h - fineSwell.Sample(i * 3.0f + 10.0f, 40.0f).h);
            coarseRough += std::fabs(coarseSwell.Sample(i * 3.0f + 7.0f, 40.0f).h - coarseSwell.Sample(i * 3.0f + 10.0f, 40.0f).h);
        }
        require(fineRough > coarseRough * 1.5, "a fine mesh carries the chop a coarse one leaves out");
        // the clouds in the water: strong at a slant, faint looking straight down, none when switched off
        LookParams P; P.eye[1] = 40.0f; P.clouds = 1.0f; P.cover = 0.8f; P.skyOn = true;
        LookSky sk{}; for (int q = 0; q < 3; q++) { sk.hor[q] = 180; sk.zen[q] = 90; }
        sk.sun[1] = 0.9f; sk.sunA = 1.0f;
        Look near{}, slant{};
        ShadeWater(P, sk, 0, 3, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, near);
        ShadeWater(P, sk, 0, 900, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, slant);
        require(slant.cloud[3] > near.cloud[3] * 1.5f && slant.cloud[3] > 20.0f, "the clouds show most at a slant");
        P.clouds = 0.0f;
        Look off{};
        ShadeWater(P, sk, 0, 900, 0, 600, 0, 1, 0, 0, 0, 0, 0.5f, 1, off);
        require(off.cloud[3] == 0.0f, "clouds off draw no clouds");
    }
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
