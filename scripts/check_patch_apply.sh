#!/bin/sh
# Every patch in patches/ applies, in order, to a clean copy of the pinned engine commits (the fork, its libultraship and ZAPDTR), and a
# second run of apply_patches.sh on the patched copy sees every one as already applied. Catches a patch that drifted when someone regenerated
# another one. Works on `git archive` copies in a temp folder, so the submodules' working trees (build state) are never touched.
# Usage: scripts/check_patch_apply.sh        Prints "patches apply: N, rerun clean" or the failure, exit 1.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$ROOT/third_party/Shipwright-Android"
[ -e "$FORK/.git" ] || { echo "SKIP: fork not checked out (scripts/engine_setup.sh)"; exit 0; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/fork"
git -C "$FORK" archive HEAD | tar -x -C "$TMP/fork"
for sub in libultraship ZAPDTR; do
    [ -e "$FORK/$sub/.git" ] && mkdir -p "$TMP/fork/$sub" && git -C "$FORK/$sub" archive HEAD | tar -x -C "$TMP/fork/$sub"
done
total=$(ls "$ROOT"/patches/*.patch "$ROOT"/patches/libultraship/*.patch "$ROOT"/patches/ZAPDTR/*.patch 2>/dev/null | wc -l)
ROYALE_FORK="$TMP/fork" "$ROOT/scripts/apply_patches.sh" >"$TMP/first" 2>&1 || { echo "FAIL first run"; grep ERROR "$TMP/first"; exit 1; }
applied=$(grep -c "^applied " "$TMP/first" || true)
[ "$applied" -eq "$total" ] || { echo "FAIL only $applied of $total patches applied to a clean copy"; grep -v "^applied " "$TMP/first" | head -4; exit 1; }
ROYALE_FORK="$TMP/fork" "$ROOT/scripts/apply_patches.sh" >"$TMP/second" 2>&1 || { echo "FAIL rerun on the patched copy"; grep ERROR "$TMP/second"; exit 1; }
again=$(grep -c "^already applied" "$TMP/second" || true)
[ "$again" -eq "$total" ] || { echo "FAIL rerun applied something again"; grep -v "^already" "$TMP/second" | head -4; exit 1; }
echo "patches apply: $total, rerun clean"
