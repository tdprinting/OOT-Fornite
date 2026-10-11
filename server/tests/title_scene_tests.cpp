// The start screen picture (shared/title_scene.h): every moment of it draws, stays finite, puts the logo on screen and says PRESS START.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include "title_scene.h"

namespace {
struct Count : royale::title::Canvas {
    int tris = 0, logos = 0, bad = 0;
    float lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
    std::string text;
    void Tri(float x0, float y0, uint32_t, float x1, float y1, uint32_t, float x2, float y2, uint32_t) override {
        tris++;
        for (float v : {x0, y0, x1, y1, x2, y2}) if (!std::isfinite(v)) bad++;
    }
    void Logo(float x0, float y0, float x1, float y1, float) override { logos++; lx0 = x0; ly0 = y0; lx1 = x1; ly1 = y1; }
    void Text(float, float, float, uint32_t rgba, const char* s) override { if (rgba & 255) text += s; }
};
} // namespace

int main() {
    royale::title::Scene scene;
    const float sizes[][2] = {{1280, 720}, {1920, 1080}, {1024, 768}, {2400, 1080}};
    for (const auto& sz : sizes)
        for (float t = 0; t < 40; t += 0.37f)
            for (bool reduced : {false, true}) {
                Count c;
                scene.Draw(c, sz[0], sz[1], t, -1, reduced, 1.9f, "VERSION 1");
                assert(c.bad == 0);
                assert(c.tris > 500);
                if (t > royale::title::kLogoAt + 0.4f) {
                    assert(c.logos == 1);
                    assert(c.lx0 >= 0 && c.lx1 <= sz[0] && c.ly0 >= 0 && c.ly1 < sz[1] * 0.75f);
                }
                if (t > royale::title::kPromptAt + 0.7f) assert(c.text.find("PRESS START") != std::string::npos);
            }
    Count end;
    scene.Draw(end, 1280, 720, 10, royale::title::kOutroSeconds, false, 1.9f, "VERSION 1");
    assert(end.bad == 0);
    std::printf("title scene tests passed\n");
    return 0;
}
