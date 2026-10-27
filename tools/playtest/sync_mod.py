#!/usr/bin/env python3
"""Copy the mod into the fork like scripts/link_mod.sh, rewriting only files whose content changed (keeps ninja incremental).
Prints "list-changed" when a file was added or removed, so build.sh knows to re-run CMake."""
import filecmp
import shutil
import sys
from pathlib import Path

root, build = Path(sys.argv[1]), Path(sys.argv[2])
mod = root / "mod" / "Royale"
dest = root / "third_party" / "Shipwright-Android" / "soh" / "soh" / "Enhancements" / "Royale"
pairs = {}
for name in ("RoyaleMod.cpp", "RoyaleThirdStormTables.cpp", "RoyaleLobbyFish.h", "RoyaleWarTable.h", "RoyaleLobbyPets.h", "RoyaleBokoblins.h", "RoyaleJones.h",
             "RoyalePeter.h", "RoyaleChief.h"):
    pairs[dest / name] = mod / name
for sub in ("features", "war_table"):
    for f in (mod / sub).rglob("*"):
        if f.is_file():
            pairs[dest / sub / f.relative_to(mod / sub)] = f
pairs[root / "third_party/Shipwright-Android/libultraship/src/graphic/Fast3D/asset_relief.h"] = root / "shared" / "asset_relief.h"
for d, s in pairs.items():
    if not d.exists() or not filecmp.cmp(s, d, shallow=False):
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(s, d)   # a fresh mtime: copy2 kept the source time, older than an object built after the edit, so ninja skipped it
existing = {f for f in dest.rglob("*") if f.is_file()}
for stale in existing - set(pairs):
    stale.unlink()
listing = "\n".join(sorted(str(p) for p in pairs))
stamp = build / ".playtest-mod-files"
if not stamp.exists() or stamp.read_text() != listing:
    build.mkdir(parents=True, exist_ok=True)
    stamp.write_text(listing)
    print("list-changed")
