#!/bin/sh
# One-time engine setup per container for the feature check's layer 2a (docs/TEST_TOOL_DESIGN.md): fork submodule, apt deps, patches,
# CMake configure. Idempotent: a second run only re-checks. Then: scripts/check_mod_compile.sh <build dir>  (or feature_check.py --engine).
# Usage: scripts/engine_setup.sh [build dir, default build-engine]     Prints "engine ready" or the first error.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-engine}"
FORK="$ROOT/third_party/Shipwright-Android"
if [ ! -e "$FORK/libultraship/.git" ]; then
    git -C "$ROOT" submodule update --init --depth 1 third_party/enet
    git -C "$ROOT" submodule update --init --depth 1 third_party/Shipwright-Android
    git -C "$FORK" submodule update --init --depth 1 libultraship ZAPDTR OTRExporter
fi
# The compile deps (measured: this exact list; adding libzip-tools zlib1g-dev libbz2-dev made apt fail in the cloud container).
if ! pkg-config --exists sdl2 2>/dev/null; then
    SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
    $SUDO apt-get install -y libsdl2-dev libzip-dev libspdlog-dev nlohmann-json3-dev libtinyxml2-dev libglew-dev libgl-dev libboost-dev libpng-dev
fi
"$ROOT/scripts/apply_patches.sh" >/dev/null
"$ROOT/scripts/link_mod.sh" >/dev/null
if [ ! -f "$BUILD/build.ninja" ]; then
    mkdir -p "$BUILD"
    if ! cmake -S "$FORK" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >"$BUILD/configure.log" 2>&1; then
        grep -m1 -E "CMake Error|error" "$BUILD/configure.log" || tail -1 "$BUILD/configure.log"
        exit 1
    fi
fi
echo "engine ready: $BUILD"
