"""Execute the actual water grid draw loop (DrawWaterGrid) with bounds-checking RSP commands.
No engine or ROM required. A wrong vertex allocation, a load of more than 32 vertices or a bad index fails before any memory read.
The water meshes have holes (the finer mesh lies in them), so the test runs grids of every size the game uses with and without a hole.
Usage: python scripts/test_water_draw_buffers.py [C++ compiler, default c++]
"""
from pathlib import Path
import argparse, os, subprocess, tempfile
from mod_source import read_mod_source
root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("compiler", nargs="?", default="c++")
ap.add_argument("--source", type=Path, default=root / "mod/Royale/RoyaleMod.cpp")
a = ap.parse_args()
source = read_mod_source(a.source)
head = "template <typename Use>\nvoid DrawWaterGrid(PlayState* play, const Vtx* v, int cols, int rows, Use use) {"
func = head + source.split(head, 1)[1].split("\n}\n", 1)[0] + "\n}\n"
harness = r'''#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
struct Vtx { int data[4]; };
struct PlayState { struct { void* gfxCtx; } state; };
int commands = 0, loads = 0, triangles = 0;
const Vtx* expected;
size_t expectedCount;
int loaded = 0;
void Vertex(int, uintptr_t ptr, int n, int first) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(expected), end = begin + expectedCount * sizeof(Vtx);
    if (ptr < begin || ptr > end || uintptr_t(n)*sizeof(Vtx) > end-ptr || n + first > 32) {
        std::fprintf(stderr, "water vertex load outside its allocation\n"); std::exit(1);
    }
    loaded = n + first; ++loads;
}
void Triangle(int, int a, int b, int c, int, int d, int e, int f, int) {
    for (int i : {a,b,c,d,e,f}) if (i < 0 || i >= loaded) std::exit(2);
    triangles += 2;
}
#define OPEN_DISPS(x)
#define CLOSE_DISPS(x)
#define POLY_XLU_DISP commands
#define gSPVertex Vertex
#define gSP2Triangles Triangle
bool GfxHasRoom(PlayState*, int) { return true; }
''' + func + r'''
int main() {
    PlayState ps{};
    const int sizes[][2] = { {1,1}, {8,8}, {15,3}, {16,2}, {17,5}, {24,24}, {32,32} };
    for (const auto& sz : sizes) {
        const int cols = sz[0], rows = sz[1];
        for (int hole = 0; hole < 2; hole++) {
            std::vector<Vtx> v(static_cast<size_t>(cols + 1) * (rows + 1));
            expected = v.data(); expectedCount = v.size();
            loads = triangles = 0;
            int want = 0;
            auto use = [&](int i, int j) { return !(hole && i >= cols / 4 && i < cols * 3 / 4 && j >= rows / 4 && j < rows * 3 / 4); };
            for (int j = 0; j < rows; j++) for (int i = 0; i < cols; i++) want += use(i, j) ? 2 : 0;
            DrawWaterGrid(&ps, v.data(), cols, rows, use);
            if (triangles != want) { std::fprintf(stderr, "grid %dx%d hole %d: %d triangles, wanted %d\n", cols, rows, hole, triangles, want); return 3; }
        }
    }
    std::puts("Water draw allocation bounds passed");
}
'''
with tempfile.TemporaryDirectory(prefix="water-draw-") as td:
    td = Path(td); cpp = td / "test.cpp"; exe = td / ("test.exe" if os.name == "nt" else "test")
    cpp.write_text(harness)
    compiler = a.compiler
    if Path(compiler).stem.lower() == "cl":
        args = [compiler, "/nologo", "/std:c++17", "/EHsc", str(cpp), "/Fe:" + str(exe), "/Fo:" + str(td / "test.obj")]
    else: args = [compiler, "-std=c++17", "-O2", str(cpp), "-o", str(exe)]
    subprocess.run(args, check=True)
    subprocess.run([str(exe)], check=True)
