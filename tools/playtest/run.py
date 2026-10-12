#!/usr/bin/env python3
"""Headless playtest runner for the PC (Linux) build of OOT Royale.

Runs the game under a virtual X display with Mesa's software renderer, drives it with the in-game script driver
(mod/Royale/features/AutoTest.inc, the ROYALE_AUTOTEST variable) and reports each scenario in one or two lines:
PASS/FAIL, which step, and for a crash the signal and the top of the call stack with file:line. Full output stays on disk.

    python3 tools/playtest/run.py smoke             # the default suite
    python3 tools/playtest/run.py solo-12 match-12  # named scenarios
    python3 tools/playtest/run.py --script "ingame; host:5; wait:20; check; quit"
    python3 tools/playtest/run.py --list

Your own oot.otr is read from --otr, $ROYALE_OTR, the project's /mnt/project-files/private/oot.otr or ~/.royale-private/oot.otr. It is linked into a run folder outside the repo, never
copied into it (docs/PLAYTEST.md). Build first with tools/playtest/build.sh.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD = Path(os.environ.get("ROYALE_BUILD", Path.home() / "royale-build"))
def _default_otr():
    if os.environ.get("ROYALE_OTR"):
        return Path(os.environ["ROYALE_OTR"])
    shared = Path("/mnt/project-files/private/oot.otr")   # the project's private copy (Claude project threads)
    return shared if shared.is_file() else Path.home() / ".royale-private" / "oot.otr"


DEFAULT_OTR = _default_otr()
DEFAULT_RUNS = Path(os.environ.get("ROYALE_PLAYTEST_HOME", Path.home() / ".cache" / "royale-playtest"))

MAPS = {0: "Hyrule Field", 1: "Lake Hylia", 2: "Kakariko", 3: "Death Mountain Crater", 4: "Desert Colossus", 5: "Fortnite Map",
        6: "Sandbox", 7: "Hyrule Convergence", 8: "Hyrule Kingdom", 9: "Mini Boss Arena", 10: "Main Boss Arena", 11: "Hyrule Riftlands",
        12: "Moonfall Isle", 13: "Skyloft Isles"}
TEST_MAPS = {6, 9, 10}
REG = "gSettings.Royale."

SCENARIOS = {
    "boot": "ingame; wait:5; shot:waiting-room; check; quit",
    "regular": (f"ingame; regular; cvar:{REG}RegularAlly1=1; cvar:{REG}RegularAlly2=2; warp:57; wait:20; shot:lake-hylia; check; "
                "warp:58; wait:10; shot:zoras-domain; check; warp:52; wait:10; shot:kakariko; check; quit"),
    "skin": "ingame; skin:1; wait:3; shot:skin-lobby; host:12; wait:8; check; jump; wait:10; shot:skin-match; check; leave; wait:3; skin:0; quit",
    "tour-overworld": "ingame; regular; tour:overworld; check; quit",
    "tour-dungeons": "ingame; regular; tour:dungeons; check; quit",
    "tour-all": "ingame; regular; tour:all; check; quit",
    "boots": ("ingame; host:6; wait:12; boots:1; wait:2; shot:boots-iron; boots:2; wait:2; shot:boots-hover; "
              "boots:0; wait:2; shot:boots-kokiri; check; quit"),
    "sandbox": "ingame; host:6; wait:15; shot:sandbox; check; quit",
}
for _id in MAPS:
    if _id in TEST_MAPS:
        SCENARIOS[f"solo-{_id}"] = f"ingame; host:{_id}; wait:15; shot:solo-{_id}; check; quit"
    else:
        SCENARIOS[f"solo-{_id}"] = f"ingame; host:{_id}; wait:5; shot:solo-{_id}-bus; check; jump; wait:20; shot:solo-{_id}-ground; check; quit"
        SCENARIOS[f"match-{_id}"] = (f"ingame; hostbots:{_id}; state:Drop; shot:match-{_id}-bus; jump; wait:3; shot:match-{_id}-skydive; "
                                     f"state:InMatch; wait:30; shot:match-{_id}-hud; check; quit")

SUITES = {
    "smoke": ["boot", "solo-12", "match-12", "skin", "regular"],
    "maps": [f"solo-{i}" for i in MAPS],
    "matches": [f"match-{i}" for i in MAPS if i not in TEST_MAPS],
    "scenes": ["tour-overworld", "tour-dungeons"],
}
SUITES["full"] = SUITES["smoke"] + [s for s in SUITES["maps"] + SUITES["matches"] + SUITES["scenes"] if s not in SUITES["smoke"]]

SIGNALS = ("SIGSEGV", "SIGABRT", "SIGBUS", "SIGFPE", "SIGILL", "SIGTRAP")
LOG_ERR = re.compile(r"\[(error|critical)\]", re.I)


def find_game(build: Path):
    for name in ("soh/soh.elf", "soh.elf", "soh/soh"):
        if (build / name).is_file():
            return build / name
    return None


def start_display():
    for n in range(97, 110):
        if not Path(f"/tmp/.X11-unix/X{n}").exists():
            proc = subprocess.Popen(["Xvfb", f":{n}", "-screen", "0", "960x540x24", "+extension", "GLX", "-nolisten", "tcp"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(50):
                if Path(f"/tmp/.X11-unix/X{n}").exists():
                    return proc, f":{n}"
                time.sleep(0.1)
            proc.kill()
    sys.exit("could not start Xvfb")


def child_pid(pid):
    try:
        for task in Path(f"/proc/{pid}/task").iterdir():
            kids = (task / "children").read_text().split()
            if kids:
                return int(kids[0])
    except OSError:
        pass
    return None


def prepare_home(run_dir: Path, otr: Path, fresh: bool):
    if fresh and run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    cfg = run_dir / "shipofharkinian.json"
    if not cfg.exists():   # the game window fills the virtual 16:9 screen (like the Odin), so screenshots show only the game
        cfg.write_text(json.dumps({"Window": {"Width": 960, "Height": 540, "PositionX": 0, "PositionY": 0}}))
    link = run_dir / "oot.otr"
    if not link.exists():
        link.symlink_to(otr.resolve())   # a link, never a copy
    for name in ("autotest-result.txt", "autotest-heartbeat.txt", "royale-trace.txt", "shot-request.txt"):
        (run_dir / name).unlink(missing_ok=True)
    for png in run_dir.glob("*.png"):
        png.unlink()


def take_shot(req: Path, run_dir: Path, display: str):
    """Screenshot of the virtual screen, scaled to 640 wide so a reviewer can look at it cheaply."""
    name = re.sub(r"[^\w-]", "_", req.read_text().strip() or "shot")
    subprocess.run(["import", "-display", display, "-window", "root", "-resize", "640x", str(run_dir / f"{name}.png")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    req.unlink(missing_ok=True)


def summarize_stack(gdb_out: str, limit=12):
    """The crashing thread's frames (after gdb's 'bt'), trimmed to 'function file:line'."""
    frames = []
    for line in gdb_out.splitlines():
        m = re.match(r"#(\d+)\s+(?:0x[0-9a-f]+ in )?(\S+) \(.*?\)(?: at (\S+))?(?: from (\S+))?", line)
        if m:
            where = m.group(3) or (Path(m.group(4)).name if m.group(4) else "?")
            frames.append(f"#{m.group(1)} {m.group(2)} {where}")
    # gdb prints the crashing thread first; 'thread apply all' follows and restarts numbering at #0
    first = []
    for f in frames:
        if f.startswith("#0 ") and first:
            break
        first.append(f)
    return first[:limit]


def interesting(stack):
    """The first frame in our code or the game's, skipping libc, the GL driver and the abort machinery."""
    skip = ("libc", "libGL", "llvmpipe", "libgallium", "raise", "abort", "__assert", "__pthread", "pthread_kill", "??", "std::")
    for f in stack:
        if not any(s in f for s in skip):
            return f
    return stack[0] if stack else "?"


def run_scenario(name, script, args, game, display):
    run_dir = args.runs / name
    prepare_home(run_dir, args.otr, not args.keep_save)
    env = dict(os.environ)
    env.update({
        "DISPLAY": display, "SHIP_HOME": str(run_dir), "ROYALE_AUTOTEST": script,
        "ROYALE_AUTOTEST_OUT": str(run_dir / "autotest-result.txt"),
        "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1", "GALLIUM_DRIVER": "llvmpipe",
        "MESA_GL_VERSION_OVERRIDE": "3.3", "vblank_mode": "0",
    })
    gdb = ["gdb", "-q", "-batch", "-nx", "-ex", "set pagination off", "-ex", "set print thread-events off",
           "-ex", "handle SIGPIPE SIGUSR1 SIGUSR2 SIG32 SIG33 SIG34 SIG35 nostop noprint pass", "-ex", "run",
           "-ex", "echo \\n==== CRASH THREAD ====\\n", "-ex", "bt 40", "-ex", "echo \\n==== ALL THREADS ====\\n",
           "-ex", "thread apply all bt 10", "--args", str(game)]
    out_path = run_dir / "game-output.txt"
    started = time.time()
    hang = None
    with open(out_path, "w") as out:
        proc = subprocess.Popen(gdb, cwd=game.parent, env=env, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        beat = run_dir / "autotest-heartbeat.txt"
        shot_req = run_dir / "shot-request.txt"
        while proc.poll() is None:
            time.sleep(0.5)
            if shot_req.exists():
                take_shot(shot_req, run_dir, display)
            now = time.time()
            last = beat.stat().st_mtime if beat.exists() else started
            if now - last > args.hang or now - started > args.timeout:
                hang = "no frame for %d s" % (now - last) if now - last > args.hang else "over the %d s limit" % args.timeout
                pid = child_pid(proc.pid)
                if pid:
                    os.kill(pid, signal.SIGABRT)   # gdb stops on it and prints every thread's stack
                try:
                    proc.wait(60)
                except subprocess.TimeoutExpired:
                    proc.kill()
                break
    secs = time.time() - started
    return collect(name, script, run_dir, secs, hang)


def collect(name, script, run_dir, secs, hang):
    text = (run_dir / "game-output.txt").read_text(errors="replace")
    result = (run_dir / "autotest-result.txt").read_text(errors="replace") if (run_dir / "autotest-result.txt").exists() else ""
    exit_m = re.search(r"exited (?:normally|with code (\d+))", text)
    code = int(exit_m.group(1) or "0", 8) if exit_m else None
    sig_m = re.search(r"received signal (SIG\w+)", text)
    sig = sig_m.group(1) if sig_m else None
    ok_steps = len(re.findall(r"^OK ", result, re.M)) + (1 if re.search(r"^PASS ", result, re.M) else 0)   # quit logs PASS, not OK
    total_steps = len([s for s in script.split(";") if s.strip()])
    fail_line = next((l for l in result.splitlines() if l.startswith("FAIL")), "")
    warns = sorted(set(l.strip() for l in result.splitlines() if "WARN" in l))
    asserts = sorted(set(re.findall(r"(Assertion .*? failed\.?|assert(?:ion)? failed[^\n]*)", text)))[:3]
    logs = []
    for log in (run_dir / "logs").glob("*.log") if (run_dir / "logs").exists() else []:
        logs += [l.strip()[:200] for l in log.read_text(errors="replace").splitlines() if LOG_ERR.search(l)]
    trace = (run_dir / "royale-trace.txt").read_text(errors="replace") if (run_dir / "royale-trace.txt").exists() else ""
    missing = sorted(set(re.findall(r"resource did not load: [^\n]*", trace)))
    stack = summarize_stack(text.split("==== CRASH THREAD ====", 1)[1]) if "==== CRASH THREAD ====" in text and sig else []
    crumbs = [l for l in re.sub(r"^---- .*?^---- end ----\n", "", trace, flags=re.M | re.S).splitlines() if l.strip()][-5:]
    step = f"step {ok_steps + 1}/{total_steps}: {script.split(';')[ok_steps].strip()}" if ok_steps < total_steps else ""
    if hang:
        status, why = "HANG", f"{hang} at {step}"
    elif sig:
        status, why = "CRASH", f"{sig} in {interesting(stack)} at {step}"
    elif code == 0 and "PASS" in result:
        status, why = "PASS", ""
    else:
        status, why = "FAIL", fail_line or f"exit code {code} at {step}"
    return {"name": name, "status": status, "why": why, "secs": round(secs), "steps": f"{ok_steps}/{total_steps}", "stack": stack,
            "asserts": asserts, "log_errors": sorted(set(logs))[:8], "log_error_count": len(logs), "missing": missing[:8],
            "warnings": warns[:5], "shots": sorted(str(p) for p in run_dir.glob("*.png")), "last_crumbs": crumbs, "run_dir": str(run_dir), "script": script}


def print_result(r, verbose):
    head = f"{r['status']:<5} {r['name']:<16} {r['secs']:>4}s steps {r['steps']:<6} log errors {r['log_error_count']}"
    print(head + (f"  {r['why']}" if r["why"] else ""))
    if r.get("shots"):
        print(f"      shots: {len(r['shots'])} in {r['run_dir']}")
    if r["status"] == "PASS" and not verbose:
        for w in r["warnings"]:
            print(f"      {w}")
        return
    for f in r["stack"][:8]:
        print(f"      {f}")
    for a in r["asserts"]:
        print(f"      assert: {a}")
    for m in r["missing"][:4]:
        print(f"      {m}")
    for e in r["log_errors"][:4]:
        print(f"      log: {e}")
    for w in r["warnings"]:
        print(f"      {w}")
    if r["status"] != "PASS":
        print(f"      files: {r['run_dir']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="scenarios or suites (default: smoke)")
    ap.add_argument("--script", help="run this step script instead (see AutoTest.inc)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--build", type=Path, default=DEFAULT_BUILD)
    ap.add_argument("--otr", type=Path, default=DEFAULT_OTR)
    ap.add_argument("--runs", type=Path, default=DEFAULT_RUNS)
    ap.add_argument("--timeout", type=int, default=900, help="wall seconds per scenario")
    ap.add_argument("--hang", type=int, default=120, help="wall seconds without a frame before it counts as a hang")
    ap.add_argument("--keep-save", action="store_true", help="reuse the run folder's save and settings")
    ap.add_argument("--json", type=Path, help="also write the results here")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.list:
        for k, v in SUITES.items():
            print(f"suite {k}: {' '.join(v)}")
        for k, v in SCENARIOS.items():
            print(f"{k:<16} {v}")
        return 0
    game = find_game(args.build)
    if not game:
        sys.exit(f"no game binary in {args.build}: run tools/playtest/build.sh first")
    if not args.otr.is_file():
        sys.exit(f"no oot.otr at {args.otr} (set --otr or ROYALE_OTR)")
    if ROOT in args.runs.resolve().parents or args.runs.resolve() == ROOT:
        sys.exit("the run folder must be outside the repo (it holds a link to your oot.otr and saves)")

    jobs = []
    if args.script:
        jobs.append(("custom", args.script))
    for n in args.names or ([] if args.script else ["smoke"]):
        for s in SUITES.get(n, [n]):
            if s not in SCENARIOS:
                sys.exit(f"unknown scenario {s} (--list shows them)")
            jobs.append((s, SCENARIOS[s]))

    xvfb, display = start_display()
    results = []
    try:
        for name, script in jobs:
            r = run_scenario(name, script, args, game, display)
            print_result(r, args.verbose)
            sys.stdout.flush()
            results.append(r)
    finally:
        xvfb.terminate()
    passed = sum(r["status"] == "PASS" for r in results)
    print(f"{passed}/{len(results)} passed")
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    (args.runs / "last-results.json").write_text(json.dumps(results, indent=1))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
