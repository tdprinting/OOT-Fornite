#!/bin/sh
# Build the PC (Linux) game for the headless playtests (tools/playtest/run.py, docs/PLAYTEST.md).
# First build: about 25 minutes on 4 cores. After a mod edit only RoyaleMod.cpp recompiles and the game relinks.
#   ROYALE_BUILD   build folder (default ~/royale-build, outside the repo)
#   JOBS           parallel compiles (default: cores; RoyaleMod.cpp alone needs several GB, so a failed pass is retried one at a time)
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROYALE_BUILD:-$HOME/royale-build}"
JOBS="${JOBS:-$(nproc)}"

missing=""
for p in libsdl2-dev libsdl2-net-dev libzip-dev zipcmp nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libboost-dev libopengl-dev \
         libglew-dev libpng-dev libbz2-dev xvfb libgl1-mesa-dri gdb ninja-build cmake; do
    dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"
done
if [ -n "$missing" ]; then
    echo "missing packages:$missing"
    echo "  sudo apt-get install$missing zipmerge ziptool"
    exit 1
fi

git -C "$ROOT" submodule update --init --recursive --depth 1
# apply_patches.sh is not re-runnable once later patches touch the same lines: apply only to a clean fork checkout
if git -C "$ROOT/third_party/Shipwright-Android" diff --quiet; then "$ROOT/scripts/apply_patches.sh" >/dev/null; fi
# Same copy as scripts/link_mod.sh, but only files whose content changed are rewritten, so ninja rebuilds just RoyaleMod.cpp
# (link_mod.sh deletes and recopies everything, which with a CMake re-run rebuilds ~270 files).
LIST_CHANGED=$(python3 "$ROOT/tools/playtest/sync_mod.py" "$ROOT" "$BUILD")
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$ROOT/third_party/Shipwright-Android" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
elif [ "$LIST_CHANGED" = "list-changed" ]; then
    cmake "$BUILD" >/dev/null   # a mod file was added or removed: the source glob must see it
fi
# CMake's download of stb_image.h can come back empty behind some proxies; fetch it directly then.
STB="$BUILD/_deps/stb/stb_image.h"
if [ ! -s "$STB" ]; then
    curl -sSL -o "$STB" https://raw.githubusercontent.com/nothings/stb/0bc88af4de5fb022db643c2d8e549a0927749354/stb_image.h
fi
LOG="$BUILD/playtest-build.log"
if ! ninja -C "$BUILD" -j"$JOBS" soh GenerateSohOtr >"$LOG" 2>&1; then
    echo "build pass failed, retrying one compile at a time (memory)"
    if ! ninja -C "$BUILD" -j1 soh GenerateSohOtr >"$LOG" 2>&1; then
        grep -E "error|FAILED" "$LOG" | head -20
        echo "build failed: full log $LOG"
        exit 1
    fi
fi
echo "built: $(ls "$BUILD"/soh/soh.elf 2>/dev/null || find "$BUILD" -maxdepth 2 -name 'soh.elf' | head -1)"
