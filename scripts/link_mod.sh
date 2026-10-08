#!/bin/sh
# Copy the game-facing mod and included features into Shipwright. Re-run after edits.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/Shipwright-Android/soh/soh/Enhancements/Royale"
rm -rf "$DEST"
mkdir -p "$DEST"
cp "$ROOT/mod/Royale/RoyaleMod.cpp" "$DEST/"
cp "$ROOT/mod/Royale/RoyaleLobbyFish.h" "$DEST/"
cp "$ROOT/mod/Royale/RoyaleWarTable.h" "$DEST/"
cp "$ROOT/mod/Royale/RoyaleLobbyPets.h" "$DEST/"
cp -R "$ROOT/mod/Royale/features" "$DEST/"
cp -R "$ROOT/mod/Royale/war_table" "$DEST/"
mkdir -p "$ROOT/third_party/Shipwright-Android/libultraship/src/graphic/Fast3D"
cp "$ROOT/shared/asset_relief.h" "$ROOT/third_party/Shipwright-Android/libultraship/src/graphic/Fast3D/"
echo "Copied mod/Royale -> $DEST (re-run cmake so the glob picks it up)"
