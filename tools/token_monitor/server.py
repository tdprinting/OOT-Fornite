"""Local token savings monitor. No AI/API calls; RTK database opened read-only."""
from datetime import datetime, timezone
from contextlib import closing
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import json
import os
import sqlite3
import threading
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def normalized(path):
    return str(path).replace("\\\\?\\", "").replace("\\", "/").rstrip("/").casefold()


def summarize(rows):
    raw = sum(row["input_tokens"] for row in rows)
    compact = sum(row["output_tokens"] for row in rows)
    saved = sum(row["saved_tokens"] for row in rows)
    return {"commands": len(rows), "raw": raw, "compact": compact,
            "saved": saved, "percent": 100 * saved / raw if raw else 0}


def find_database():
    candidates = [
        Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "rtk/history.db",
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "rtk/history.db",
        Path.home() / "Library/Application Support/rtk/history.db",
    ]
    return next((p for p in candidates if p.is_file()), candidates[0])


class Metrics:
    def __init__(self, database, workspace):
        self.database = database
        self.workspace_path = Path(workspace)
        self.workspace = normalized(workspace)
        self.lock = threading.Lock()
        self.baseline = 0
        self.reset_at = datetime.now(timezone.utc).isoformat()
        rows, _ = self.rows()
        self.baseline = max((r["id"] for r in rows), default=0)

    def rows(self):
        if not self.database.is_file():
            return [], "RTK history database not found. Run a supported command through RTK first."
        try:
            with closing(sqlite3.connect(self.database.resolve().as_uri() + "?mode=ro", uri=True, timeout=1)) as db:
                db.row_factory = sqlite3.Row
                # Do not return command text: logs can contain private arguments.
                rows = [dict(r) for r in db.execute(
                    "SELECT id,timestamp,input_tokens,output_tokens,saved_tokens,project_path "
                    "FROM commands ORDER BY id")]
                return rows, None
        except sqlite3.Error as exc:
            return [], "Unable to read RTK history: " + str(exc)

    def snapshot(self, scope="workspace"):
        rows, error = self.rows()
        if scope == "workspace":
            rows = [r for r in rows if normalized(r["project_path"]) == self.workspace or
                    normalized(r["project_path"]).startswith(self.workspace + "/")]
        with self.lock:
            baseline, reset_at = self.baseline, self.reset_at
        session = [r for r in rows if r["id"] > baseline]
        structure = json.loads((HERE / "baseline.json").read_text(encoding="utf-8"))
        workspace_claude = self.workspace_path / "CLAUDE.md"
        if workspace_claude.is_file():
            structure["claude_now"]["chars"] = len(workspace_claude.read_text(encoding="utf-8"))
        features = []
        for p in sorted((ROOT / "mod/Royale/features").glob("*.inc")):
            text = p.read_text(encoding="utf-8")
            features.append({"name": p.stem, "chars": len(text), "lines": len(text.splitlines())})
        structure["features"] = features
        source = (ROOT / "mod/Royale/RoyaleMod.cpp").read_text(encoding="utf-8")
        structure["entry_now"] = {"chars": len(source), "lines": len(source.splitlines())}
        return {"updated": datetime.now(timezone.utc).isoformat(), "scope": scope,
                "total": summarize(rows), "session": summarize(session), "reset_at": reset_at,
                "recent": [{k: r[k] for k in ("id", "timestamp", "input_tokens", "output_tokens", "saved_tokens")}
                           for r in rows[-12:]], "structure": structure, "error": error,
                "monitor_ai_tokens": 0}

    def reset(self):
        rows, _ = self.rows()
        with self.lock:
            self.baseline = max((r["id"] for r in rows), default=0)
            self.reset_at = datetime.now(timezone.utc).isoformat()


def handler_for(metrics):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, value, content_type="application/json", status=200):
            body = value.encode("utf-8") if isinstance(value, str) else json.dumps(value).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type + "; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/":
                self.send((HERE / "index.html").read_text(encoding="utf-8"), "text/html")
            elif parsed.path == "/api/metrics":
                scope = parse_qs(parsed.query).get("scope", ["workspace"])[0]
                if scope not in ("workspace", "global"):
                    self.send({"error": "Invalid scope"}, status=400)
                else:
                    self.send(metrics.snapshot(scope))
            else:
                self.send({"error": "Not found"}, status=404)

        def do_POST(self):
            # Same-origin browser action only; resetting never changes RTK's database.
            allowed = {f"http://127.0.0.1:{self.server.server_port}", f"http://localhost:{self.server.server_port}"}
            if self.headers.get("Origin") not in allowed:
                self.send({"error": "Origin rejected"}, status=403)
            elif self.path == "/api/reset":
                metrics.reset()
                self.send({"ok": True})
            else:
                self.send({"error": "Not found"}, status=404)
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--workspace", type=Path, default=ROOT.parent)
    parser.add_argument("--database", type=Path, default=find_database())
    args = parser.parse_args()
    metrics = Metrics(args.database, args.workspace.resolve())
    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler_for(metrics))
    print(f"Token monitor: http://127.0.0.1:{server.server_port} (zero AI/API calls)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
