#!/bin/sh
# Copy the game-facing part of the Royale mod (RoyaleMod.cpp; the rest is reached through include paths set by cmake/royale.cmake) into the Shipwright submodule. Shipwright's CMake uses file(GLOB_RECURSE) without
# FOLLOW_SYMLINKS, so a symlink would be skipped. Re-run after editing mod/Royale.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/Shipwright-Android/soh/soh/Enhancements/Royale"
rm -rf "$DEST"
mkdir -p "$DEST"
cp "$ROOT/mod/Royale/RoyaleMod.cpp" "$DEST/"
cp "$ROOT/mod/Royale/RoyaleLobbyFish.h" "$DEST/"
cp "$ROOT/mod/Royale/RoyaleWarTable.h" "$DEST/"
echo "Copied mod/Royale -> $DEST (re-run cmake so the glob picks it up)"
