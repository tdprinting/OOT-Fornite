#!/bin/sh
# Apply our patches to the Shipwright-Android submodule and to its nested libultraship submodule.
#   patches/*.patch              -> third_party/Shipwright-Android
#   patches/libultraship/*.patch -> third_party/Shipwright-Android/libultraship
# Idempotent: patches that are already applied are skipped.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
apply_dir() {
    repo="$1"; dir="$2"
    [ -d "$dir" ] || return 0
    for p in "$dir"/*.patch; do
        [ -e "$p" ] || continue
        if git -C "$repo" apply --check "$p" 2>/dev/null; then
            git -C "$repo" apply "$p"
            echo "applied $(basename "$p") to $(basename "$repo")"
        elif git -C "$repo" apply --reverse --check "$p" 2>/dev/null; then
            echo "already applied: $(basename "$p")"
        else
            echo "ERROR: $(basename "$p") neither applies nor is already applied" >&2
            exit 1
        fi
    done
}
apply_dir "$ROOT/third_party/Shipwright-Android" "$ROOT/patches"
apply_dir "$ROOT/third_party/Shipwright-Android/libultraship" "$ROOT/patches/libultraship"
