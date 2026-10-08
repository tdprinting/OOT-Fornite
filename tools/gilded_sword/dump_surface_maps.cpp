// Writes the Gilded Sword's four surface maps as PNG-able raw files: g++ -std=c++17 -I shared tools/gilded_sword/dump_surface_maps.cpp -o dump && ./dump outdir
// (then tools/gilded_sword/maps_to_png.py turns them into PNGs). Only for looking at the maps; the game builds them itself.
#include "gilded_sword_surface.h"
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const char* names[] = {"blade", "cord", "metal", "leather"};
    for (int c = 0; c < 4; c++) {
        const auto m = royale::gilded_surface::Build(static_cast<royale::gilded_surface::Class>(c));
        for (int kind = 0; kind < 2; kind++) {
            const std::string path = dir + "/" + names[c] + (kind == 0 ? "_normal" : "_bump") + ".rgba";
            FILE* f = std::fopen(path.c_str(), "wb");
            std::fwrite(&m.size, 4, 1, f);
            std::fwrite((kind == 0 ? m.normal : m.height).data(), 1, m.normal.size(), f);
            std::fclose(f);
        }
    }
    return 0;
}
