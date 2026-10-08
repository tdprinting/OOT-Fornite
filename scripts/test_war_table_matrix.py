"""Compile the production menu matrix setup against engine conversion/decoder code.

Windows: python scripts/test_war_table_matrix.py --engine PATH
Requires the engine source and Visual Studio C++ tools; never modifies the engine.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    start = match.start()
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "third_party/Shipwright-Android")
    args = parser.parse_args()
    engine = args.engine.resolve()
    gu = (engine / "soh/soh/gu_pc.c").read_text()
    matrix = (engine / "soh/src/code/sys_matrix.c").read_text()
    renderer = (engine / "libultraship/src/graphic/Fast3D/gfx_pc.cpp").read_text()
    types = engine / "libultraship/include/libultraship/libultra/types.h"
    painter = (ROOT / "mod/Royale/war_table/Painter.inc").read_text()
    # Execute the real setup block, not a second implementation of the fix.
    setup = painter[painter.index("        MtxF identity;"):painter.index("        f.viewport =")]
    decode = renderer.split("// Original GBI where fixed point matrices are used", 1)[1].split("#else", 1)[0]
    build = ROOT / "war-table-compile/matrix-regression"
    build.mkdir(parents=True, exist_ok=True)
    common = '#include <math.h>\n#include <string.h>\n#include "' + types.as_posix() + '"\n'
    conversion = function(matrix, "Matrix_MtxFToMtx").replace("    FrameInterpolation_RecordMatrixMtxFToMtx(src, dest);", "")
    (build / "engine.c").write_text(common + "\n".join(function(gu, n) for n in
        ("guMtxF2L", "guMtxIdentF", "guMtxIdent")) + "\n" + conversion)
    harness = common + r'''
#include <cassert>
#include <iostream>
extern "C" {
void guMtxIdentF(float [4][4]);
void guMtxIdent(Mtx*);
Mtx* Matrix_MtxFToMtx(MtxF*, Mtx*);
}
void Decode(const Mtx& m, float matrix[4][4]) {
    const int32_t* addr = &m.m[0][0];
''' + decode + r'''
}
int main() {
    Mtx old; guMtxIdent(&old);
    float broken[4][4]; Decode(old, broken);
    assert(broken[3][3] == 0); // Reproduce the blue-screen defect.
    struct { Mtx identity; } f;
''' + setup + r'''
    float fixed[4][4]; Decode(f.identity, fixed);
    for (int i=0; i<4; ++i) for (int j=0; j<4; ++j)
        assert(fixed[i][j] == (i==j ? 1.0f : 0.0f));
    // Startup and pause share this painter. All canvas vertices must retain w=1.
    for (float aspect : {16.f/9, 4.f/3, 21.f/9, 9.f/16}) {
        float scale = fminf(1, aspect/(16.f/9));
        for (float x : {0.f, 26.f, 320.f, 617.f, 640.f})
        for (float y : {0.f, 20.f, 180.f, 309.f, 360.f}) {
            float v[4] = {x-320, 180-y, 0, 1}, transformed[4] = {};
            for (int j=0; j<4; ++j) for (int i=0; i<4; ++i)
                transformed[j] += v[i]*fixed[i][j];
            assert(transformed[3] == 1);
            float clipX = transformed[0]/(240/scale)*(4.f/3)/aspect;
            float clipY = transformed[1]/(180/scale);
            assert(fabsf(clipX) <= 1.0001f && fabsf(clipY) <= 1.0001f);
        }
    }
    std::cout << "Menu matrix: old defect reproduced; production identity and 100 canvas/aspect cases passed\n";
}
'''
    (build / "test.cpp").write_text(harness)
    vcvars = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat")
    (build / "run.cmd").write_text('@echo off\ncall "' + str(vcvars) + '" >nul\n'
        'cl /nologo /c engine.c /Foengine.obj >compile.log 2>&1\nif errorlevel 1 exit /b 1\n'
        'cl /nologo /EHsc /std:c++17 test.cpp engine.obj /Fetest.exe >>compile.log 2>&1\n'
        'if errorlevel 1 exit /b 1\ntest.exe\n')
    result = subprocess.run(["cmd.exe", "/c", str(build / "run.cmd")], cwd=build)
    if result.returncode:
        print((build / "compile.log").read_text(errors="replace")[-6000:])
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
