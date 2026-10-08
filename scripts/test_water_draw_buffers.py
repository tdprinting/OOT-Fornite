"""Execute the actual detailed water draw loop with bounds-checking RSP commands.
No engine or ROM required. A wrong coarse/fine allocation fails before any memory read.
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
loop = source.split("    auto drawFine = [&](const Vtx* arr) {", 1)[1].split("    };", 1)[0]
call = source.split("// pass 1: the water", 1)[1].split("// pass 2:", 1)[0]
call = next(line.strip() for line in call.splitlines() if "drawFine(" in line)
harness = r'''#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
struct Vtx { int data[4]; };
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
bool GfxHasRoom(void*, int) { return true; }
int main() {
    constexpr int S = 8, FV = S + 1;
    void* play = nullptr;
    for (int count : {0,1,16}) {
        std::vector<int> fine(count);
        std::vector<Vtx> coarse(225), detailed(count * FV * FV);
        Vtx* v = coarse.data(); Vtx* fv = count ? detailed.data() : nullptr;
        (void)v;
        expected = detailed.data(); expectedCount = detailed.size();
        loads = triangles = 0;
        auto drawFine = [&](const Vtx* arr) {
''' + loop + r'''
        };
''' + call + r'''
        if (triangles != count * S * S * 2 || loads != count * 4) return 3;
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
