#!/bin/sh
# Copy the Royale mod into the Shipwright submodule. Shipwright's CMake uses file(GLOB_RECURSE) without
# FOLLOW_SYMLINKS, so a symlink would be skipped. Re-run after editing mod/Royale.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/Shipwright/soh/soh/Enhancements/Royale"
rm -rf "$DEST"
cp -r "$ROOT/mod/Royale" "$DEST"
echo "Copied mod/Royale -> $DEST (re-run cmake so the glob picks it up)"
