// Renders the start screen (shared/title_scene.h) to PPM pictures without the game, for checking its look and timing.
// Build: g++ -std=c++17 -O2 -I shared -I mod/Royale tools/title_scene/preview.cpp -o /tmp/title_preview
// Run:   /tmp/title_preview out_dir [width height]   -> out_dir/title_<t>.ppm for a few moments of the intro, the idle loop and the outro.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "title_scene.h"
#include "logo_data.h"
#include "skin_link_hd_model.h"

namespace {
struct Image {
    int w, h;
    std::vector<float> rgb;
    Image(int w_, int h_) : w(w_), h(h_), rgb(static_cast<size_t>(w_) * h_ * 3, 0.0f) {}
    void Blend(int x, int y, float r, float g, float b, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        float* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        p[0] += (r - p[0]) * a; p[1] += (g - p[1]) * a; p[2] += (b - p[2]) * a;
    }
};

// A 5x7 font for the preview's text (capitals, digits and a few marks).
const char* Glyph(char c) {
    static const struct { char c; const char* rows; } kFont[] = {
        {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"}, {'C', "01110100011000010000100001000101110"},
        {'D', "11110100011000110001100011000111110"}, {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
        {'G', "01110100011000010111100011000101111"}, {'H', "10001100011000111111100011000110001"}, {'I', "01110001000010000100001000010001110"},
        {'L', "10000100001000010000100001000011111"}, {'M', "10001110111010110101100011000110001"}, {'N', "10001110011010110011100011000110001"},
        {'O', "01110100011000110001100011000101110"}, {'P', "11110100011000111110100001000010000"}, {'R', "11110100011000111110101001001010001"},
        {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"}, {'V', "10001100011000110001100010101000100"},
        {'0', "01110100111010110101110011000101110"}, {'1', "00100011000010000100001000010001110"}, {'2', "01110100010000100010001000100011111"},
        {'3', "11110000010000101110000010000111110"}, {'4', "00010001100101010010111110001000010"}, {'5', "11111100001111000001000011000101110"},
        {'6', "01110100001000011110100011000101110"}, {'7', "11111000010001000100010000100001000"}, {'8', "01110100011000101110100011000101110"},
        {'9', "01110100011000101111000010000101110"}, {'.', "00000000000000000000000000110001100"}, {' ', "00000000000000000000000000000000000"},
    };
    for (const auto& g : kFont) if (g.c == c) return g.rows;
    return "11111100011000110001100011000111111";
}

struct Raster : royale::title::Canvas {
    Image& img;
    std::vector<uint8_t> logo;   // RGBA
    explicit Raster(Image& i) : img(i) {
        logo.assign(static_cast<size_t>(royale::kLogoW) * royale::kLogoH * 4, 0);
        size_t px = 0;
        for (int r = 0; r < royale::kLogoRuns; r++) {
            const int count = royale::kLogoRle[r * 3], idx = royale::kLogoRle[r * 3 + 1], alpha = royale::kLogoRle[r * 3 + 2];
            for (int k = 0; k < count && px * 4 < logo.size(); k++, px++) {
                if (idx == 0) continue;
                for (int ch = 0; ch < 3; ch++) logo[px * 4 + ch] = royale::kLogoPalette[(idx - 1) * 3 + ch];
                logo[px * 4 + 3] = static_cast<uint8_t>(alpha);
            }
        }
    }
    static void Unpack(uint32_t c, float* o) { o[0] = (c >> 24 & 255) / 255.f; o[1] = (c >> 16 & 255) / 255.f; o[2] = (c >> 8 & 255) / 255.f; o[3] = (c & 255) / 255.f; }
    void Tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) override {
        const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (std::fabs(area) < 1e-6f) return;
        float k0[4], k1[4], k2[4];
        Unpack(c0, k0); Unpack(c1, k1); Unpack(c2, k2);
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2})))), maxX = std::min(img.w - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2})))), maxY = std::min(img.h - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
        for (int y = minY; y <= maxY; y++) for (int x = minX; x <= maxX; x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
            const float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
            const float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float c[4];
            for (int i = 0; i < 4; i++) c[i] = k0[i] * w0 + k1[i] * w1 + k2[i] * w2;
            img.Blend(x, y, c[0], c[1], c[2], c[3]);
        }
    }
    void Logo(float x0, float y0, float x1, float y1, float alpha) override {
        for (int y = std::max(0, static_cast<int>(y0)); y < std::min(img.h, static_cast<int>(y1)); y++)
            for (int x = std::max(0, static_cast<int>(x0)); x < std::min(img.w, static_cast<int>(x1)); x++) {
                const int u = std::clamp(static_cast<int>((x + 0.5f - x0) / (x1 - x0) * royale::kLogoW), 0, royale::kLogoW - 1);
                const int v = std::clamp(static_cast<int>((y + 0.5f - y0) / (y1 - y0) * royale::kLogoH), 0, royale::kLogoH - 1);
                const uint8_t* p = &logo[(static_cast<size_t>(v) * royale::kLogoW + u) * 4];
                img.Blend(x, y, p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f * alpha);
            }
    }
    void Text(float cx, float y, float size, uint32_t rgba, const char* text) override {
        float c[4];
        Unpack(rgba, c);
        const float cell = size / 7, adv = cell * 6;
        const float x0 = cx - std::strlen(text) * adv * 0.5f;
        for (int pass = 0; pass < 2; pass++)
            for (size_t i = 0; text[i]; i++) {
                const char* g = Glyph(text[i]);
                for (int r = 0; r < 7; r++) for (int q = 0; q < 5; q++) if (g[r * 5 + q] == '1') {
                    const float gx = x0 + i * adv + q * cell, gy = y + r * cell, o = pass == 0 ? cell * 0.35f : 0;
                    for (int yy = static_cast<int>(gy - o); yy < static_cast<int>(gy + cell + o); yy++)
                        for (int xx = static_cast<int>(gx - o); xx < static_cast<int>(gx + cell + o); xx++)
                            pass == 0 ? img.Blend(xx, yy, 0.1f, 0.05f, 0.02f, c[3] * 0.8f) : img.Blend(xx, yy, c[0], c[1], c[2], c[3]);
                }
            }
    }
};

void Save(const Image& img, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    std::fprintf(f, "P6 %d %d 255\n", img.w, img.h);
    for (float v : img.rgb) std::fputc(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f), f);
    std::fclose(f);
}
} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int w = argc > 3 ? std::atoi(argv[2]) : 1280, h = argc > 3 ? std::atoi(argv[3]) : 720;
    const float aspect = static_cast<float>(royale::kLogoW) / royale::kLogoH;
    struct Shot { const char* name; float t, outro; bool reduced; };
    const Shot shots[] = {{"0_fadein", 0.6f, -1, false}, {"1_landing", 1.35f, -1, false}, {"2_burning", 2.2f, -1, false},
                          {"3_idle", 5.0f, -1, false}, {"4_idle_late", 13.0f, -1, false}, {"5_pressed", 6.0f, 0.3f, false},
                          {"6_outro_end", 6.8f, 1.0f, false}, {"7_reduced_motion", 5.0f, -1, true}, {"8_hero_a", 7.3f, -1, false}, {"9_hero_b", 9.1f, -1, false}};
    royale::title::Scene scene;
    scene.SetHero(&royale::skinmodel::link_hd::kModel);
    for (const Shot& s : shots) {
        Image img(w, h);
        Raster r(img);
        scene.Draw(r, static_cast<float>(w), static_cast<float>(h), s.t, s.outro, s.reduced, aspect, "VERSION 1.0");
        Save(img, dir + "/title_" + s.name + ".ppm");
    }
    std::printf("wrote %zu pictures to %s\n", sizeof(shots) / sizeof(shots[0]), dir.c_str());
    return 0;
}
