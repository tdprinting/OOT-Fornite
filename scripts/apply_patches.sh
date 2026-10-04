#!/bin/sh
# Apply patches/*.patch to the Shipwright-Android submodule. Idempotent: patches that are already applied are skipped.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$ROOT/third_party/Shipwright-Android"
for p in "$ROOT"/patches/*.patch; do
    if git -C "$FORK" apply --check "$p" 2>/dev/null; then
        git -C "$FORK" apply "$p"
        echo "applied $(basename "$p")"
    elif git -C "$FORK" apply --reverse --check "$p" 2>/dev/null; then
        echo "already applied: $(basename "$p")"
    else
        echo "ERROR: $(basename "$p") neither applies nor is already applied" >&2
        exit 1
    fi
done
