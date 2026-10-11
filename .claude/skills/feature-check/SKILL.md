---
name: feature-check
description: Test OOT Royale features. Runs scripts/feature_check.py (server tests, scripted bot matches, mod functions with the engine stubbed, optional engine compile) and reads the phone self-test report. Use after any feature change, before a handoff or PR, or when the user asks "does X work" / "test the recent features".
---
# Feature check

Design and layers: `docs/TEST_TOOL_DESIGN.md`. Registry of features and their checks: `scripts/feature_checks.json`.

1. Pick the scope:
   - Changed on this branch: `python scripts/feature_check.py --changed` (vs `origin/main`; pass a base to change it).
   - Named: `python scripts/feature_check.py squads battle_bus` (`--list` prints the names).
   - Everything: `python scripts/feature_check.py` (~2 min warm, ~7 min cold).
   - Edited a mod `.inc`, `war_table/` or `patches/`: add `--engine` (first time in a container: `scripts/engine_setup.sh`, then the engine compile takes ~8 min).
2. Read the summary line and FAIL rows only. Logs: `build-check/feature_check/<check>.log`; read the first error lines, never dump the log.
   FLAKY = failed then passed on the rerun; say so, do not call it a pass.
3. Fix FAILs that belong to the change; re-run the same command. A FAIL outside the change: report check, first error line, likely cause.
4. Missing coverage for what you changed: add a check in the same change (smoke in `server/tests/feature_smoke.cpp`, a mod-function test in
   `scripts/mod_function_tests/`, or a step in `mod/Royale/features/SelfTest.inc`) and its row in `scripts/feature_checks.json`.
5. Report: the summary line, fixed/unfixed FAILs, and for the user a short on-device list: the DEVICE rows plus the `manual:` lines for the
   features touched. Phone run: load a save without a lobby, Battle Royale menu > Settings > Debug > SelfTest; the report is
   `TDawgsBattleRoyale/royale-selftest.txt`; re-run with `--device-report <that file>` to merge it.

What it cannot see: how things look, sound or feel, and real button input on the device. A PASS means the logic, the match rules and the
compile are right, not that the feature is fun or looks right. Say that when reporting.
