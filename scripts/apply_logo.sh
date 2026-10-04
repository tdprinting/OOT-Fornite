#!/bin/sh
# Put the logo everywhere: if assets/logo.png is there, regenerate the icons and the in-game copy from it (needs Pillow: pip install pillow),
# then copy the Android launcher icons into the game's Android project. Run by the build after apply_patches.sh and link_mod.sh.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -f "$ROOT/assets/logo.png" ] && python3 -c "import PIL" 2>/dev/null; then
    python3 "$ROOT/scripts/make_logo_assets.py"
elif [ -f "$ROOT/assets/logo.png" ]; then
    echo "Pillow is not installed: using the logo files already generated (pip install pillow to refresh them)"
fi
RES="$ROOT/third_party/Shipwright-Android/Android/app/src/main/res"
if [ -d "$ROOT/assets/generated/res" ] && [ -d "$RES" ]; then
    cp -r "$ROOT/assets/generated/res/." "$RES/"
    echo "Copied the launcher icons into the Android project"
fi
