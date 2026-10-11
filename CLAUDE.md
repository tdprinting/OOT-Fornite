# OOT Royale

32-player battle royale mod on Ship of Harkinian (Android, AYN Odin 2). Goal: one finished blend of Majora's Mask + Ocarina of Time + Fortnite. Design: `docs/DESIGN.md`. Build: `docs/BUILDING.md`.

## Team (`.claude/agents/`)
- `leader` (Opus, low): plans, core design, delegates. Start multi-step work here.
- `ronnie-the-runner` (Haiku, medium): search, compile/tests, git/GitHub/PRs/merges, research, small chores.
- `gary` (Sonnet, low): writes code.

## Rules
- Caveman style: answer first, short sentences, no filler/recaps. Keep code, paths, errors, numbers, negations exact. Full sentences for warnings and irreversible steps.
- Find code via `mod/Royale/FEATURES.md`; scoped `rg`; read only needed ranges. `.rgignore` hides bulk; `--no-ignore` if needed.
- Map of the repo: `docs/CODE_MAP.md`. Docs index: `docs/README.md`. Dated handoffs: `docs/archive/`.
- Edit originals in `mod/Royale/`. Keep `.inc` include order (one translation unit). Never edit generated arrays. No ROM/ROM-derived files in git.
- Check: `python scripts/check_mod_layout.py`; engine edits `scripts/check_mod_compile.sh`. Build pass ≠ gameplay verified.
- Test every feature (`/feature-check`, `docs/TEST_TOOL_DESIGN.md`): new feature → row in `scripts/feature_checks.json` + a check (smoke, mod-function test or ctest) + a SelfTest step if it only shows on device, same PR. Before each handoff run `python scripts/feature_check.py --changed` (mod `.inc`/engine edits: add `--engine`) and paste its summary line. FAIL = not done. DEVICE/manual rows go to the user as a short "please check on the Odin" list.
- Git: local first. GitHub: `python scripts/github_brief.py pr N` / `run ID --errors`, `--json` + bounded `--limit`. No log dumps, no polling, no secrets.
- Art: every visual is OoT or stylised 3D Zelda look.
- Keep it lean: new generated file → `.rgignore` + `.claude/settings.json` deny, same PR. New code file → line in `docs/CODE_MAP.md`; new doc → `docs/README.md`; new feature `.inc` → `FEATURES.md` row. Files >1,200 lines: split at `// ----` markers into included `.inc`, same order, prove byte-identical expansion. Patch number clash: letter suffix (`0023a`).
- Preserve others' edits. End each feature with a short handoff: status, files, validation, next.

## Token tools
- RTK: `.claude/hooks/rtk.py` rewrites Bash commands to `rtk` when installed (`rtk init -g` / [rtk-ai/rtk](https://github.com/rtk-ai/rtk)). Not installed = no-op.
- Caveman: project enables plugin `caveman@caveman` (`.claude/settings.json`); accept the install prompt on trust. Rules above apply either way.
