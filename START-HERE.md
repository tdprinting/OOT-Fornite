# AI startup checklist

Before editing:

1. Read `AGENTS.md`, check your branch and local changes, and preserve existing work.
2. Find the feature in `mod/Royale/FEATURES.md`. Read only its implementation and relevant dependencies; do not preload every document or the full mod.
3. Keep explanations brief and clear: approach and reason before substantial work; outcome, validation, and blockers afterward. Preserve useful comments and exact technical details.
4. Use scoped `rg` searches. `.rgignore` hides dependencies, builds, assets, and generated arrays from routine searches; use explicit paths or `--no-ignore` when needed.
5. Use `rtk` for supported verbose commands when installed; inspect raw output if needed. Local Windows install: `C:/Users/mexic/.local/bin/rtk.exe`. Caveman means concise prose without losing facts; no skill install is required to follow this preference.
   Claude's project Bash hook delegates supported rewrites to RTK when installed. Other agents use RTK explicitly. The hook leaves permission decisions unchanged.
6. Prefer local Git. For GitHub use selected JSON fields, bounded results, or `python scripts/github_brief.py pr NUMBER` / `python scripts/github_brief.py run RUN_ID --errors`. Do not dump full logs or repeatedly poll unchanged state. Never print credentials.
7. Keep feature include order intact. Run `python scripts/check_mod_layout.py`; add relevant behavior checks and the game compile check for engine edits. Builds do not verify device gameplay.
8. End with one short current handoff per feature: status, files, validation, next step. Link older details rather than repeating them.

These are project defaults for any AI. Existing Codex and Claude entry files point here; other tools may need the startup prompt below.

Optional local monitor: `python tools/token_monitor/server.py`, then open `http://127.0.0.1:8766`. It uses zero AI tokens while running. RTK counts and file-reading estimates remain separate.

## Startup prompt

Read START-HERE.md and AGENTS.md before working. Apply their concise communication, focused reading/search, RTK, bounded GitHub queries, validation, and handoff practices. Start source lookup at mod/Royale/FEATURES.md. Preserve unrelated changes.
