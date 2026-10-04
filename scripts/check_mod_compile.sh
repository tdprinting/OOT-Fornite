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
echo "RoyaleMod.cpp compiles against the fork"
