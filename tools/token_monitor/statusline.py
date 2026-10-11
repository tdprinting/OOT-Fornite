"""Claude Code status line: plan usage (5-hour / weekly %), context and cost, always visible.

Claude Code pipes session JSON to this script after each message (see code.claude.com/docs/en/statusline).
It prints one short line for the terminal footer and saves the numbers to USAGE_FILE so the token monitor
page (server.py, /api/usage) can show them live. No API calls; `rate_limits` only exists for Pro/Max.
"""
from pathlib import Path
import json
import os
import sys
import time

USAGE_FILE = Path(os.environ.get("ROYALE_USAGE_FILE", Path.home() / ".claude" / "plan-usage.json"))


def pick(data):
    """The fields worth keeping, in a flat shape the page reads."""
    limits = data.get("rate_limits") or {}
    ctx = data.get("context_window") or {}
    windows = {}
    for key in ("five_hour", "seven_day"):
        w = limits.get(key) or {}
        if w.get("used_percentage") is not None:
            windows[key] = {"used": float(w["used_percentage"]), "resets_at": w.get("resets_at")}
    return {
        "updated": time.time(),
        "model": (data.get("model") or {}).get("display_name"),
        "limits": windows,
        "context_used": ctx.get("used_percentage"),
        "context_size": ctx.get("context_window_size"),
        "cost_usd": (data.get("cost") or {}).get("total_cost_usd"),
        "session_id": data.get("session_id"),
    }


def until(epoch, now):
    if not epoch:
        return ""
    left = max(0, int(epoch - now))
    h, m = divmod(left // 60, 60)
    return f"{h // 24}d{h % 24}h" if h >= 24 else f"{h}h{m:02d}m" if h else f"{m}m"


def line(u, now=None):
    now = time.time() if now is None else now
    parts = [u["model"] or "Claude"]
    for key, label in (("five_hour", "5h"), ("seven_day", "7d")):
        w = u["limits"].get(key)
        if w:
            reset = until(w["resets_at"], now)
            parts.append(f"{label} {w['used']:.0f}%" + (f" (resets {reset})" if reset else ""))
    if not u["limits"]:
        parts.append("limits: after first reply")
    if u["context_used"] is not None:
        parts.append(f"ctx {u['context_used']:.0f}%")
    if u["cost_usd"]:
        parts.append(f"${u['cost_usd']:.2f}")
    return " | ".join(parts)


def save(u):
    try:
        USAGE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = USAGE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps(u), encoding="utf-8")
        os.replace(tmp, USAGE_FILE)
    except OSError:
        pass   # the footer still works without the file


def main():
    try:
        data = json.load(sys.stdin)
    except ValueError:
        print("usage: no data yet")
        return
    u = pick(data)
    if not u["limits"]:   # a fresh session has none yet; the windows are account-wide, so keep the last ones until they reset
        try:
            old = json.loads(USAGE_FILE.read_text(encoding="utf-8")).get("limits", {})
            u["limits"] = {k: w for k, w in old.items() if (w.get("resets_at") or 0) > u["updated"]}
        except (OSError, ValueError, AttributeError):
            pass
    save(u)
    print(line(u))


if __name__ == "__main__":
    main()
