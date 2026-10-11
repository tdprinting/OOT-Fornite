"""Layer 2b of the feature check (docs/TEST_TOOL_DESIGN.md): compile pure functions from mod/Royale/features/*.inc with stubs and run them.

python scripts/test_mod_functions.py [name...]        (no names = every scripts/mod_function_tests/*.cpp)

Each test file names what it takes from the mod in comment directives at the top, and marks where the taken code goes with `// @EXTRACTED`:
  // EXTRACT <file> <function> [function...]      the function's text (signature on one line), see mod_source.function
  // EXTRACT_LINES <file> <prefix>                 every line that starts with <prefix>
  // EXTRACT_SPAN <file> <start> | <end>           from the first line containing <start> to the next line starting with <end>
The code above the marker is the stubs, below it main() with asserts. Prints PASS fn.<name> or FAIL fn.<name> after the compiler or assert text.
Needs g++ (or $CXX). Build files go to build-check/mod_functions/.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mod_source import function  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "scripts/mod_function_tests"
BUILD = ROOT / "build-check/mod_functions"


def extracted(test_source):
    parts = []
    for line in test_source.splitlines():
        m = re.match(r"//\s*(EXTRACT|EXTRACT_LINES|EXTRACT_SPAN)\s+(\S+)\s+(.+)$", line)
        if not m:
            continue
        kind, path, rest = m.groups()
        text = (ROOT / path).read_text(encoding="utf-8")
        if kind == "EXTRACT":
            parts += [function(text, name) for name in rest.split()]
        elif kind == "EXTRACT_LINES":
            lines = [l for l in text.splitlines() if l.startswith(rest.strip())]
            assert lines, f"{path}: no line starts with {rest.strip()!r}"
            parts.append("\n".join(lines))
        else:
            start, end = (s.strip() for s in rest.split("|"))
            lines = text.splitlines()
            first = next((i for i, l in enumerate(lines) if start in l), None)
            assert first is not None, f"{path}: {start!r} not found"
            last = next((i for i in range(first, len(lines)) if lines[i].startswith(end)), None)
            assert last is not None, f"{path}: no {end!r} after {start!r}"
            parts.append("\n".join(lines[first:last + 1]))
    return "\n".join(parts)


def run(test):
    name = test.stem
    source = test.read_text(encoding="utf-8")
    assert "// @EXTRACTED" in source, f"{test.name}: no // @EXTRACTED marker"
    try:
        code = source.replace("// @EXTRACTED", extracted(source), 1)
    except AssertionError as e:   # a renamed or moved function: fail loudly with its name
        return False, f"extraction failed: {e}"
    BUILD.mkdir(parents=True, exist_ok=True)
    cpp, exe = BUILD / f"{name}.cpp", BUILD / name
    cpp.write_text(code, encoding="utf-8")
    cxx = os.environ.get("CXX", "g++")
    build = subprocess.run([cxx, "-std=c++17", "-O1", "-UNDEBUG", "-I", str(ROOT), "-I", str(ROOT / "shared"), "-I", str(ROOT / "mod/Royale"),
                            str(cpp), "-o", str(exe)], capture_output=True, text=True)
    if build.returncode:
        errors = [l for l in build.stderr.splitlines() if "error" in l] or build.stderr.splitlines()
        return False, "\n".join(errors[:8])
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    out = (result.stdout + result.stderr).strip()
    return result.returncode == 0, out


def main():
    names = sys.argv[1:]
    tests = sorted(TESTS.glob("*.cpp"))
    if names:
        known = {t.stem for t in tests}
        for n in names:
            if n not in known:
                print(f"FAIL fn.{n}  no scripts/mod_function_tests/{n}.cpp")
                return 1
        tests = [t for t in tests if t.stem in names]
    failed = 0
    for test in tests:
        ok, out = run(test)
        if not ok:
            failed += 1
            if out:
                print(out)
        print(f"{'PASS' if ok else 'FAIL'} fn.{test.stem}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
