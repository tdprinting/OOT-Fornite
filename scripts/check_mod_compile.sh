#!/bin/sh
# Fast compile check of the game-facing mod code against the real Shipwright-Android headers and compiler flags.
# Configures the fork with Ninja (build directory outside the repo) and compiles ONLY RoyaleMod.cpp, which takes seconds
# instead of the hour a full game build takes. Linux is not a supported target; this is purely a compiler check, and the
# real Windows and Android builds still run in CI. Needs: cmake, ninja, SDL2, libzip + tools, spdlog, nlohmann-json, tinyxml2.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-/tmp/royale-fork-build}"
"$ROOT/scripts/apply_patches.sh"
"$ROOT/scripts/link_mod.sh"
[ -d "$BUILD" ] || cmake -S "$ROOT/third_party/Shipwright-Android" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$BUILD" soh/CMakeFiles/soh.dir/soh/Enhancements/Royale/RoyaleMod.cpp.o
# The Android build uses clang, which is stricter than the compiler above (it rejects pointer-to-integer conversions that gcc lets through
# with -fpermissive). Check with clang too if it is installed.
if command -v clang++ >/dev/null 2>&1; then
    CMD=$(ninja -C "$BUILD" -t commands soh/CMakeFiles/soh.dir/soh/Enhancements/Royale/RoyaleMod.cpp.o | tail -1)
    CMD=$(echo "$CMD" | sed 's#^/usr/bin/c++#clang++#; s# -fpermissive##; s# -Wno-int-conversion##; s# -Wno-implicit-int##; s# -o [^ ]*\.o # -fsyntax-only #; s# -MD -MT [^ ]* -MF [^ ]*##')
    (cd "$BUILD" && eval "$CMD") 2>&1 | grep -E "error" -A4 && { echo "clang found errors"; exit 1; }
    echo "clang agrees"
fi
# A compile check can't see link errors. One kind is easy to catch from the object file: a function the mod calls that was declared inside the
# anonymous namespace (the OPEN_DISPS macro does this) but is never defined there shows up as an undefined "(anonymous namespace)" symbol.
OBJ="$BUILD/soh/CMakeFiles/soh.dir/soh/Enhancements/Royale/RoyaleMod.cpp.o"
if command -v nm >/dev/null 2>&1 && [ -f "$OBJ" ]; then
    if nm -C -u "$OBJ" | grep -F "(anonymous namespace)"; then echo "undefined symbols in the anonymous namespace (the link would fail)"; exit 1; fi
    echo "no undefined local symbols"
fi
echo "RoyaleMod.cpp compiles against the fork"
