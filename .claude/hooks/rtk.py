"""Delegate supported Bash rewrites to RTK; leave normal permission checks intact."""
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    rtk = shutil.which("rtk")
    if not rtk:
        local = Path.home() / ".local/bin/rtk.exe"
        if local.is_file():
            rtk = str(local)
    if not rtk:
        return
    payload = json.load(sys.stdin)
    result = subprocess.run([rtk, "hook", "claude"], input=json.dumps(payload),
        capture_output=True, text=True, timeout=4)
    if result.returncode or not result.stdout.strip():
        return
    output = json.loads(result.stdout)
    hook = output.get("hookSpecificOutput", {})
    updated = hook.get("updatedInput", {})
    command = updated.get("command", "")
    if not command.startswith("rtk "):
        return
    # A newly installed binary may not yet be on the parent process's PATH.
    command = '"' + Path(rtk).as_posix() + '"' + command[3:]
    updated = dict(payload.get("tool_input", {}), command=command)
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "PreToolUse", "updatedInput": updated}}))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.TimeoutExpired):
        pass  # Optional optimization: missing RTK or malformed input leaves tools unchanged.
