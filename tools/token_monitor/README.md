# Local token monitor

Run `python tools/token_monitor/server.py`, then open http://127.0.0.1:8766.
Default scope is the checkout's parent workspace and its worktrees. For a standalone clone, use `--workspace .`. Use `--port NUMBER` if the port is occupied; `--database PATH` selects another RTK history database.

RTK's read-only database supplies live estimated pre/post-compression output counts. The monitor uses no model, API key, or AI tokens; it refreshes every three seconds while visible. Total model usage, subscription limits, and billing savings cannot be inferred from this counter.

The CLAUDE.md and mod comparison is a separate characters/4 estimate, multiplied by assumed read count. It compares the earlier workspace CLAUDE.md text with the short entry and one entire mod read with entry-plus-feature. Imported instructions are excluded; reading all features saves essentially no source tokens. Caveman savings are unmeasured. The old file sizes are recorded in baseline.json; no private chats are read.

Start a new monitor session to track future RTK commands without deleting RTK history. Data is served only on loopback. Closing the tab stops polling; Ctrl+C stops the server.
