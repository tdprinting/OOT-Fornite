"""Feature check: does each feature work? One command, one row per check (docs/TEST_TOOL_DESIGN.md).

python scripts/feature_check.py [feature...] [--changed [BASE]] [--layers 0,1,2] [--engine] [--seed N] [--json PATH] [--list]
                                [--device-report royale-selftest.txt]

Layers: 0 the server ctests and scripts named in scripts/feature_checks.json; 1 the feature smokes (royale_feature_smoke); 2 = 2b, pure mod
functions with stubs (test_mod_functions.py); 2a the mod compiled against the engine (--engine; once: scripts/engine_setup.sh); 3 the
on-device self-test (DEVICE rows; --device-report fills them from the file the user sent). Builds server/ once into build-check/ (Release)
and only the targets needed. A failing ctest or smoke is run once more: passing then makes it FLAKY, not FAIL. Full logs:
build-check/feature_check/<check>.log. Exit 1 if anything FAILs.
"""
from pathlib import Path
import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "scripts/feature_checks.json"
BUILD = ROOT / "build-check"
LOGS = BUILD / "feature_check"
SELFTEST_HINT = "ask the user: Settings > Debug > SelfTest, then send royale-selftest.txt"


def load_registry():
    data = json.loads(REGISTRY.read_text(encoding="utf-8"))
    return {k: v for k, v in data.items() if not k.startswith("_")}


def changed_files(base):
    files = set()
    for cmd in (["git", "diff", "--name-only", base], ["git", "ls-files", "--others", "--exclude-standard"]):
        out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if out.returncode:
            sys.exit(f"git failed: {out.stderr.strip()}")
        files.update(l for l in out.stdout.splitlines() if l)
    return sorted(files)


def features_for(path, registry):
    hit = []
    for name, row in registry.items():
        for pattern in row.get("files", []):
            if fnmatch.fnmatch(path, pattern.split("#")[0]):
                hit.append(name)
                break
    return hit


class Runner:
    def __init__(self, args):
        self.args = args
        self.rows = []
        self.cache = {}   # one run per distinct check, shared by every feature that names it
        self.built = set()
        LOGS.mkdir(parents=True, exist_ok=True)

    def row(self, feature, layer, check, result, secs=None, note="", output=""):
        self.rows.append({"feature": feature, "layer": layer, "check": check, "result": result,
                          "seconds": None if secs is None else round(secs, 1), "note": note, "output": output})

    def run(self, key, cmd, timeout=900, retry=False):
        """Runs cmd once per key; returns (result, seconds, note, output)."""
        if key in self.cache:
            return self.cache[key]
        log = LOGS / (re.sub(r"[^A-Za-z0-9_.-]", "_", key) + ".log")
        t0 = time.time()
        try:
            p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            out, ok = p.stdout + p.stderr, p.returncode == 0
        except subprocess.TimeoutExpired as e:
            out = e.stdout if isinstance(e.stdout, str) else (e.stdout or b"").decode(errors="replace")
            ok = False
            out += f"\ntimeout after {timeout} s"
        secs = time.time() - t0
        log.write_text(out, encoding="utf-8")
        result, note = ("PASS" if ok else "FAIL"), ""
        if not ok and retry:   # wall-clock tests can fail under load: a second run tells flaky from broken
            p2 = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            (LOGS / (log.stem + ".rerun.log")).write_text(p2.stdout + p2.stderr, encoding="utf-8")
            if p2.returncode == 0:
                result, note = "FLAKY", "failed once, passed on rerun"
        known = [l for l in out.splitlines() if l.startswith("KNOWN ")]
        if known:
            note = (note + "; " if note else "") + f"known issue: {known[0].split('-- ', 1)[-1]}"
        self.cache[key] = (result, secs, note, out)
        return self.cache[key]

    def build(self, targets):
        need = [t for t in targets if t not in self.built]
        if not need:
            return None
        if not (BUILD / "build.ninja").exists() and not (BUILD / "Makefile").exists():
            gen = ["-G", "Ninja"] if shutil.which("ninja") else []
            p = subprocess.run(["cmake", "-S", "server", "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release", *gen], cwd=ROOT, capture_output=True, text=True)
            if p.returncode:
                return "cmake configure failed: " + (p.stderr.strip().splitlines() or ["?"])[-1]
        p = subprocess.run(["cmake", "--build", str(BUILD), "--target", *need], cwd=ROOT, capture_output=True, text=True)
        (LOGS / "build.log").write_text(p.stdout + p.stderr, encoding="utf-8")
        if p.returncode:
            errors = [l for l in (p.stdout + p.stderr).splitlines() if "error" in l]
            return "build failed: " + (errors[0] if errors else "see build-check/feature_check/build.log")
        self.built.update(need)
        return None

    def exe(self, target):
        for path in (BUILD / target, BUILD / f"{target}.exe", BUILD / "Release" / f"{target}.exe"):
            if path.exists():
                return str(path)
        return str(BUILD / target)


def script_cmd(cmd):
    path = ROOT / cmd
    return [sys.executable, str(path)] if cmd.endswith(".py") else ["sh", str(path)]


def needs_missing(needs):
    if not needs:
        return None
    if "/" in needs:
        return None if (ROOT / needs).exists() else f"needs {needs} (scripts/engine_setup.sh)"
    return None if shutil.which(needs) else f"needs {needs}"


def parse_device_report(path):
    results = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("----"):
            break
        m = re.match(r"^(PASS|FAIL|SKIP)\s+(\S+)\s*(.*)$", line)
        if m:
            results[m[2]] = (m[1], m[3].strip())
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("features", nargs="*")
    ap.add_argument("--changed", nargs="?", const="origin/main", metavar="BASE")
    ap.add_argument("--layers", default="0,1,2")
    ap.add_argument("--engine", action="store_true", help="also layer 2a: compile the mod against the engine")
    ap.add_argument("--engine-build", default=os.environ.get("ROYALE_ENGINE_BUILD", str(ROOT / "build-engine")))
    ap.add_argument("--seed", type=int)
    ap.add_argument("--json", metavar="PATH")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--device-report", metavar="PATH")
    args = ap.parse_args()
    registry = load_registry()

    if args.list:
        print(f"{'feature':<18} {'ctest':>5} {'run':>4} {'smoke':>5} {'fn':>3} {'device':>6} {'manual':>6}")
        for name, row in registry.items():
            print(f"{name:<18} {len(row.get('ctest', [])):>5} {len(row.get('run', [])):>4} {len(row.get('smoke', [])):>5} "
                  f"{len(row.get('fn', [])):>3} {len(row.get('device', [])):>6} {len(row.get('manual', [])):>6}")
        return 0

    t0 = time.time()
    runner = Runner(args)
    selected = list(args.features)
    for f in selected:
        if f not in registry:
            sys.exit(f"unknown feature {f!r} (--list)")
    if args.changed:
        for path in changed_files(args.changed):
            hit = features_for(path, registry)
            if not hit:
                runner.row("registry", "0", path, "FAIL", note="matches no feature: add a registry row (scripts/feature_checks.json)")
            selected += [h for h in hit if h not in selected]
    if not selected and not args.changed:
        selected = list(registry)
    layers = set(args.layers.split(","))
    device = parse_device_report(args.device_report) if args.device_report else None

    # Build only what the selected rows run.
    targets = []
    for f in selected:
        row = registry[f]
        if "0" in layers:
            targets += row.get("ctest", [])
        if "1" in layers and row.get("smoke"):
            targets.append("royale_feature_smoke")
    targets = list(dict.fromkeys(targets))
    build_error = runner.build(targets) if targets else None

    for f in selected:
        row = registry[f]
        if "0" in layers:
            for t in row.get("ctest", []):
                if build_error:
                    runner.row(f, "0", t, "FAIL", note=build_error)
                    continue
                res, secs, note, out = runner.run("ctest." + t, [runner.exe(t)], retry=True)
                runner.row(f, "0", t, res, secs, note, out)
            for entry in row.get("run", []):
                cmd, needs = (entry, None) if isinstance(entry, str) else (entry["cmd"], entry.get("needs"))
                missing = needs_missing(needs)
                if missing:
                    runner.row(f, "0", Path(cmd).name, "SKIP", note=missing)
                    continue
                res, secs, note, out = runner.run("run." + cmd, script_cmd(cmd))
                runner.row(f, "0", Path(cmd).name, res, secs, note, out)
        if "1" in layers:
            for s in row.get("smoke", []):
                if build_error:
                    runner.row(f, "1", "smoke." + s, "FAIL", note=build_error)
                    continue
                cmd = [runner.exe("royale_feature_smoke"), s] + (["--seed", str(args.seed)] if args.seed is not None else [])
                res, secs, note, out = runner.run("smoke." + s, cmd, timeout=180, retry=True)
                seeds = str(args.seed) if args.seed is not None else "1,2,3"
                runner.row(f, "1", "smoke." + s, res, secs, ("seeds " + seeds + ("; " + note if note else "")), out)
        if "2" in layers or "2b" in layers:
            for fn in row.get("fn", []):
                res, secs, note, out = runner.run("fn." + fn, [sys.executable, str(ROOT / "scripts/test_mod_functions.py"), fn])
                runner.row(f, "2b", "fn." + fn, res, secs, note, out)
        for step in row.get("device", []):
            if device is not None and step in device:
                runner.row(f, "3", "device " + step, device[step][0], note=device[step][1])
            elif device is not None and "self-test" in device:   # the run did not start (no save loaded, or in a lobby)
                runner.row(f, "3", "device " + step, "SKIP", note="self-test not run: " + device["self-test"][1])
            elif device is not None:
                runner.row(f, "3", "device " + step, "FAIL", note="not in the report (did the run stop early?)")
            else:
                runner.row(f, "3", "device " + step, "DEVICE", note=SELFTEST_HINT)

    if args.engine:
        missing = needs_missing("third_party/Shipwright-Android/soh")
        if missing or not (Path(args.engine_build) / "build.ninja").exists():
            runner.row("(mod)", "2a", "engine compile", "SKIP", note="run scripts/engine_setup.sh first")
        else:
            res, secs, note, out = runner.run("engine", ["sh", str(ROOT / "scripts/check_mod_compile.sh"), args.engine_build], timeout=1800)
            runner.row("(mod)", "2a", "engine compile", res, secs, note or "gcc + clang", out)
    else:
        runner.row("(mod)", "2a", "engine compile", "SKIP", note="add --engine (once: scripts/engine_setup.sh)")

    # The table.
    print(f"{'feature':<18} {'layer':<6} {'check':<30} {'result':<7} {'time':<7} note")
    for r in runner.rows:
        t = "" if r["seconds"] is None else f"{r['seconds']}s"
        print(f"{r['feature']:<18} {r['layer']:<6} {r['check']:<30} {r['result']:<7} {t:<7} {r['note']}")
    shown = set()
    for r in runner.rows:
        if r["result"] != "FAIL" or not r["output"] or r["check"] in shown:
            continue
        shown.add(r["check"])
        lines = r["output"].splitlines()
        key = [l for l in lines if re.search(r"FAIL|error|Error|assert|Assertion", l)] or lines[-8:]
        print(f"  {r['check']}:")
        for l in key[:8]:
            print("    " + l[:200])
    for f in selected:
        for m in registry[f].get("manual", []):
            print(f"manual: {f}: {m}")
    count = lambda k: sum(1 for r in runner.rows if r["result"] == k)
    print(f"{count('PASS')} passed, {count('FAIL')} failed, {count('FLAKY')} flaky, {count('SKIP')} skipped, {count('DEVICE')} device, "
          f"{time.time() - t0:.0f} s")
    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_text(json.dumps([{k: v for k, v in r.items() if k != "output"} for r in runner.rows], indent=1), encoding="utf-8")
    return 1 if count("FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())
