# Agent entry

Codex and other non-Claude tools read this file. All rules live in [`CLAUDE.md`](CLAUDE.md); follow them.
Start code lookup at [`mod/Royale/FEATURES.md`](mod/Royale/FEATURES.md) and [`docs/CODE_MAP.md`](docs/CODE_MAP.md). Docs index: [`docs/README.md`](docs/README.md).
If `../AGENTS.md` exists, apply its shared preferences too. Optional token monitor: `python tools/token_monitor/server.py`, then `http://127.0.0.1:8766`.

## Startup prompt

Read CLAUDE.md and AGENTS.md before working. Apply their concise communication, focused reading/search, RTK, bounded GitHub queries, validation, and handoff practices. Start source lookup at mod/Royale/FEATURES.md and docs/CODE_MAP.md. Preserve unrelated changes.
