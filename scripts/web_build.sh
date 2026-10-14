#!/usr/bin/env bash
# Build the browser (WebAssembly) test version of the game into web-dist/.
#   scripts/web_build.sh            full build (deps, patched copy, compile, package)
#   scripts/web_build.sh package    only re-package the last compile into web-dist/
# Work area (outside the repo, so submodules stay clean): $WEB_WORK, default ~/.cache/oot-web.
# oot.otr / ROM data is never touched: the page asks for oot.otr on the phone.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WEB_WORK:-$HOME/.cache/oot-web}"
EMSDK_VERSION="${EMSDK_VERSION:-6.0.12}"
JOBS="${JOBS:-$(nproc)}"
P="$WORK/deps/prefix"
SRC="$WORK/src"
BLD="$WORK/build"
OUT="${WEB_OUT:-$REPO/web-dist}"
mkdir -p "$WORK"

emsdk_env() {
  if [ ! -d "$WORK/emsdk" ]; then
    git clone -q -c advice.detachedHead=false --depth 1 https://github.com/emscripten-core/emsdk.git "$WORK/emsdk"
    "$WORK/emsdk/emsdk" install "$EMSDK_VERSION"
    "$WORK/emsdk/emsdk" activate "$EMSDK_VERSION"
  fi
  # shellcheck disable=SC1091
  source "$WORK/emsdk/emsdk_env.sh" >/dev/null 2>&1
}

clone() { # name url tag
  [ -d "$WORK/deps/$1" ] || git clone -q -c advice.detachedHead=false --depth 1 ${3:+--branch "$3"} "$2" "$WORK/deps/$1"
}

cmake_install() { # dir extra cmake args...
  local d="$WORK/deps/$1"; shift
  emcmake cmake -S "$d" -B "$d/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@" >"$d/cfg.log" 2>&1
  ninja -C "$d/b" install >"$d/build.log" 2>&1
}

build_deps() {
  [ -f "$P/.done" ] && return
  mkdir -p "$WORK/deps" "$P"
  clone zlib https://github.com/madler/zlib.git v1.3.1
  cmake_install zlib
  rm -f "$P"/lib/libz.so*   # keep only the static zlib; a .so would make the link treat it as a side module
  clone png https://github.com/pnggroup/libpng.git v1.6.43
  cmake_install png -DPNG_TESTS=OFF -DPNG_SHARED=OFF -DPNG_STATIC=ON \
    -DZLIB_INCLUDE_DIR="$P/include" -DZLIB_LIBRARY="$P/lib/libz.a"
  clone libzip https://github.com/nih-at/libzip.git v1.10.1
  cmake_install libzip -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_OPENSSL=OFF \
    -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF \
    -DBUILD_TOOLS=OFF -DBUILD_DOC=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF \
    -DZLIB_INCLUDE_DIR="$P/include" -DZLIB_LIBRARY="$P/lib/libz.a"
  clone tinyxml2 https://github.com/leethomason/tinyxml2.git
  cmake_install tinyxml2 -DBUILD_TESTS=OFF
  clone spdlog https://github.com/gabime/spdlog.git v1.12.0
  cmake_install spdlog -DSPDLOG_BUILD_SHARED=OFF -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_BUILD_BENCH=OFF
  clone json https://github.com/nlohmann/json.git v3.11.3
  cmake_install json -DJSON_BuildTests=OFF
  clone sdl2 https://github.com/libsdl-org/SDL.git release-2.32.10
  cmake_install sdl2 -DSDL_TEST=OFF -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_OPENGLES=ON -DSDL_OPENGL=OFF \
    -DSDL_AUDIO=ON -DSDL_RENDER=OFF -DCMAKE_C_FLAGS="-fPIC"
  # Boost is header-only for us; link it through a private dir so /usr/include never reaches the compiler.
  ln -sfn /usr/include/boost "$WORK/deps/boost-inc"
  touch "$P/.done"
}

prepare_src() {
  # A patched copy of the repo, so the submodules in the checkout stay untouched.
  # Re-made each run (patch files are the source of truth); ccache makes the recompile cheap.
  rm -rf "$SRC"
  cp -a "$REPO" "$SRC"
  git -C "$SRC" submodule update --init --depth 1 third_party/enet >/dev/null 2>&1 || true
  (cd "$SRC" && bash scripts/apply_patches.sh && bash scripts/link_mod.sh)
  # Same build number the Android build shows, when CI gives one.
  if [ -n "${ROYALE_BUILD_NUMBER:-}" ]; then
    echo "#define ROYALE_BUILD_VERSION \"0.${ROYALE_BUILD_NUMBER}\"" >"$SRC/mod/Royale/build_version.h"
  fi
}

configure_and_build() {
  local ccache=()
  if command -v ccache >/dev/null 2>&1; then
    ccache=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi
  local link="-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sUSE_WEBGL2=1 -sFULL_ES3=1 -sMAX_WEBGL_VERSION=2 -sSTACK_SIZE=5242880 -lEGL -lGL -sALLOW_TABLE_GROWTH=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,callMain"
  emcmake cmake -S "$SRC/third_party/Shipwright-Android" -B "$BLD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DUSE_OPENGLES=ON "${ccache[@]}" \
    -DCMAKE_PREFIX_PATH="$P;/usr" -DCMAKE_FIND_ROOT_PATH="$P;/usr" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
    -DCMAKE_HAVE_LIBC_PTHREAD=1 \
    -DPNG_LIBRARY="$P/lib/libpng16.a" -DPNG_PNG_INCLUDE_DIR="$P/include" \
    -DCMAKE_C_FLAGS="-I$P/include/SDL2 -I$P/include" -DCMAKE_CXX_FLAGS="-I$P/include/SDL2 -I$P/include" \
    -DSDL2_INCLUDE_DIR="$P/include/SDL2" -DSDL2_LIBRARY="$P/lib/libSDL2.a" \
    -DBoost_INCLUDE_DIR="$WORK/deps/boost-inc" -DBZIP2_INCLUDE_DIR="$P/include" \
    -Dnlohmann_json_DIR="$P/lib/cmake/nlohmann_json" \
    -DCMAKE_EXE_LINKER_FLAGS="$link" >"$WORK/configure.log" 2>&1
  # CMake's own download of stb_image.h comes back empty through some proxies; fetch it directly.
  curl -fsSL -o "$BLD/_deps/stb/stb_image.h" \
    https://raw.githubusercontent.com/nothings/stb/0bc88af4de5fb022db643c2d8e549a0927749354/stb_image.h
  ninja -C "$BLD" -j"$JOBS" soh
}

package() {
  rm -rf "$OUT"
  mkdir -p "$OUT"
  cp "$REPO"/web/index.html "$REPO"/web/loader.js "$REPO"/web/touch.js "$OUT"/
  cp "$BLD/soh/soh.js" "$OUT/"
  gzip -9 -c "$BLD/soh/soh.wasm" >"$OUT/soh.wasm.gz"      # hosts cap single files at 25 MB; the page unpacks it
  cp "$REPO/third_party/Shipwright-Android/Android/app/src/main/assets/soh.otr" "$OUT/"
  printf '/*\n  Cache-Control: no-cache\n' >"$OUT/_headers"
  git -C "$REPO" rev-parse --short HEAD >"$OUT/version.txt" 2>/dev/null || true
  ls -la "$OUT"
}

if [ "${1:-}" = "package" ]; then
  package
  exit 0
fi

emsdk_env
build_deps
prepare_src
configure_and_build
package
