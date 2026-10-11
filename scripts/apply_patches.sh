#!/bin/sh
# Apply our patches to the Shipwright-Android submodule and to its nested libultraship submodule.
#   patches/*.patch              -> third_party/Shipwright-Android
#   patches/libultraship/*.patch -> third_party/Shipwright-Android/libultraship
#   patches/ZAPDTR/*.patch       -> third_party/Shipwright-Android/ZAPDTR (the ROM extractor)
# Idempotent: patches that are already applied are skipped. ROYALE_FORK points it at another copy of the fork (scripts/check_patch_apply.sh).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORK="${ROYALE_FORK:-$ROOT/third_party/Shipwright-Android}"
apply_dir() {
    repo="$1"; dir="$2"
    [ -d "$dir" ] || return 0
    # Patches go on in order, so the last one whose reverse applies cleanly (and that does not also apply forward) is the top of the applied stack: every patch before it is on
    # too (their own reverse check can fail once a later patch changed lines next to theirs, e.g. 0018 after 0005), every one after it is new.
    top=0; i=0
    for p in "$dir"/*.patch; do
        [ -e "$p" ] || continue
        i=$((i + 1))
        if git -C "$repo" apply --reverse --check "$p" 2>/dev/null && ! git -C "$repo" apply --check "$p" 2>/dev/null; then top=$i; fi
    done
    i=0
    for p in "$dir"/*.patch; do
        [ -e "$p" ] || continue
        i=$((i + 1))
        if [ "$i" -le "$top" ]; then
            echo "already applied: $(basename "$p")"
        elif git -C "$repo" apply --check "$p" 2>/dev/null; then
            git -C "$repo" apply "$p"
            echo "applied $(basename "$p") to $(basename "$repo")"
        else
            echo "ERROR: $(basename "$p") neither applies nor is already applied" >&2
            exit 1
        fi
    done
}
apply_dir "$FORK" "$ROOT/patches"
apply_dir "$FORK/libultraship" "$ROOT/patches/libultraship"
apply_dir "$FORK/ZAPDTR" "$ROOT/patches/ZAPDTR"
