"""Check feature packaging; optionally prove exact reconstruction of a Git baseline."""
from pathlib import Path
import argparse
import fnmatch
import json
import re
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
# The feature check registry (scripts/feature_checks.json): every feature file has a row, and the self-test's steps match the rows' device lists.
registry = {k: v for k, v in json.loads((root / "scripts/feature_checks.json").read_text(encoding="utf-8")).items() if not k.startswith("_")}
for inc in sorted(expected):
    path = "mod/Royale/" + inc
    assert any(fnmatch.fnmatch(path, g.split("#")[0]) for row in registry.values() for g in row.get("files", [])), \
        f"{path} has no row in scripts/feature_checks.json (add a registry row and a check)"
steps = re.findall(r'\{\s*"([a-z_]+)",\s*"([a-z_.]+)",', (mod.parent / "features/SelfTest.inc").read_text(encoding="utf-8").split("kSelfChecks[]", 1)[1].split("};", 1)[0])
assert steps, "SelfTest.inc: no kSelfChecks entries found"
for feature, step in steps:
    assert feature in registry, f"SelfTest.inc step {step}: feature {feature!r} is not in scripts/feature_checks.json"
    assert step in registry[feature].get("device", []), f"SelfTest.inc step {step} is not in {feature}'s device list (scripts/feature_checks.json)"
listed = {(f, d) for f, row in registry.items() for d in row.get("device", [])}
assert listed == set(steps), f"device steps in the registry but not in SelfTest.inc: {sorted(listed - set(steps))}"
smokes = set(re.findall(r'\{"([a-z_]+)", Smoke_', (root / "server/tests/feature_smoke.cpp").read_text(encoding="utf-8")))
fns = {p.stem for p in (root / "scripts/mod_function_tests").glob("*.cpp")}
for name, row in registry.items():
    for s in row.get("smoke", []):
        assert s in smokes, f"{name}: smoke {s!r} is not in server/tests/feature_smoke.cpp"
    for f in row.get("fn", []):
        assert f in fns, f"{name}: fn {f!r} has no scripts/mod_function_tests/{f}.cpp"
expanded = read_mod_source(mod)
if args.copied:
    assert read_mod_source(args.copied / "RoyaleMod.cpp") == expanded, "Copied implementation differs"
    for header in ("RoyaleLobbyFish.h", "RoyaleLobbyPets.h", "RoyaleWarTable.h", "RoyaleBokoblins.h", "RoyaleJones.h", "RoyalePeter.h", "RoyaleChief.h"):
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
