"""Bounded GitHub summaries. Requires authenticated gh (or GH_TOKEN).

Examples: python scripts/github_brief.py pr 123
          python scripts/github_brief.py run 123456 --errors
"""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("kind", choices=("pr", "run"))
parser.add_argument("number", type=int)
parser.add_argument("--repo", default="tdprinting/OOT-Fornite")
parser.add_argument("--errors", action="store_true", help="Show at most 40 failed-log error lines")
args = parser.parse_args()
if args.errors and args.kind != "run":
    parser.error("--errors requires run")
gh = shutil.which("gh")
if not gh:
    local = Path.home() / ".local/bin/gh.exe"
    if local.is_file():
        gh = str(local)
if not gh:
    parser.error("Install GitHub CLI and authenticate with gh auth login")

env = os.environ.copy()
if not env.get("GH_TOKEN") and not env.get("GITHUB_TOKEN"):
    auth = subprocess.run([gh, "auth", "status"], capture_output=True, env=env)
    if auth.returncode:
        # Reuse the same host's existing Git credential; never save or print it.
        env.update(GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
        credential = subprocess.run(["git", "credential", "fill"],
            input="protocol=https\nhost=github.com\n\n", capture_output=True, text=True, env=env)
        fields = dict(s.split("=", 1) for s in credential.stdout.splitlines() if "=" in s)
        if fields.get("password"):
            env["GH_TOKEN"] = fields["password"]

def query(*command):
    result = subprocess.run([gh, *command, "--repo", args.repo], capture_output=True, text=True, env=env)
    if result.returncode:
        raise SystemExit(result.stderr.strip())
    return result.stdout

if args.kind == "pr":
    data = json.loads(query("pr", "view", str(args.number), "--json",
        "number,state,mergeable,headRefOid,statusCheckRollup"))
    checks = data.pop("statusCheckRollup") or []
    data["checks"] = [{k: c[k] for k in ("name", "status", "conclusion", "state") if k in c} for c in checks]
else:
    data = json.loads(query("run", "view", str(args.number), "--json", "status,conclusion,headSha,jobs"))
    data["jobs"] = [{k: j[k] for k in ("name", "status", "conclusion", "databaseId") if k in j} for j in data["jobs"]]
print(json.dumps(data, separators=(",", ":")))
if args.errors:
    logs = query("run", "view", str(args.number), "--log-failed")
    matches = [s for s in logs.splitlines() if re.search(r"(?:\berror\b|fatal|FAILED|FAIL:|AssertionError|undefined reference)", s, re.I)]
    for line in matches[:40]:
        print(line[:1000])
    if not matches:
        print("No matching error lines. Inspect the failed job log directly.")
    elif len(matches) > 40:
        print(f"{len(matches) - 40} further matching lines omitted; inspect relevant job directly.")
