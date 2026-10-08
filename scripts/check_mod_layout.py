"""Check feature packaging; optionally prove exact reconstruction of a Git baseline."""
from pathlib import Path
import argparse
import subprocess
from mod_source import FEATURE_INCLUDE, read_mod_source

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline", help="Git revision before extraction")
parser.add_argument("--copied", type=Path, help="Check the directory produced by link_mod.sh")
args = parser.parse_args()
mod = root / "mod/Royale/RoyaleMod.cpp"
source = mod.read_text(encoding="utf-8")
includes = FEATURE_INCLUDE.findall(source)
expected = {"features/" + p.name for p in (mod.parent / "features").glob("*.inc")}
assert includes and len(includes) == len(set(includes)), "Missing or duplicate feature includes"
assert set(includes) == expected, "Feature files and includes differ"
assert not list((mod.parent / "features").glob("*.cpp")), "Features must not compile independently"
copy_script = (root / "scripts/link_mod.sh").read_text()
assert 'cp -R "$ROOT/mod/Royale/war_table" "$DEST/"' in copy_script, "Build omits native menu fragments"
assert 'cp -R "$ROOT/mod/Royale/features" "$DEST/"' in copy_script, "Build omits features"
expanded = read_mod_source(mod)
if args.copied:
    assert read_mod_source(args.copied / "RoyaleMod.cpp") == expanded, "Copied implementation differs"
    for header in ("RoyaleLobbyFish.h", "RoyaleLobbyPets.h", "RoyaleWarTable.h", "RoyaleBokoblins.h"):
        assert (args.copied / header).read_bytes() == (mod.parent / header).read_bytes(), header
    for fragment in (mod.parent / "war_table").glob("*.inc"):
        assert (args.copied / "war_table" / fragment.name).read_bytes() == fragment.read_bytes(), fragment.name
    print("Engine copy matches source")
if args.baseline:
    old = subprocess.check_output(
        ["git", "show", args.baseline + ":mod/Royale/RoyaleMod.cpp"], cwd=root
    ).decode("utf-8").replace("\r\n", "\n")
    assert expanded == old, "Expanded source differs from baseline"
    print("Exact pre-refactor source reconstruction passed")
print(f"Mod layout passed: {len(includes)} features, {len(source.splitlines())} entry-point lines")
