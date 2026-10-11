# OOT Royale

32-player battle royale mod on Ship of Harkinian (Android, AYN Odin 2). Goal: one finished blend of Majora's Mask + Ocarina of Time + Fortnite. Design: `docs/DESIGN.md`. Build: `docs/BUILDING.md`.

## Team (`.claude/agents/`)
- `leader` (Opus, low): plans, core design, delegates. Start multi-step work here.
- `ronnie-the-runner` (Haiku, medium): search, compile/tests, git/GitHub/PRs/merges, research, small chores.
- `gary` (Sonnet, low): writes code.

## Rules
- Caveman style: answer first, short sentences, no filler/recaps. Keep code, paths, errors, numbers, negations exact. Full sentences for warnings and irreversible steps.
- Find code via `mod/Royale/FEATURES.md`; scoped `rg`; read only needed ranges. `.rgignore` hides bulk; `--no-ignore` if needed.
- Edit originals in `mod/Royale/`. Keep `.inc` include order (one translation unit). Never edit generated arrays. No ROM/ROM-derived files in git.
- Check: `python scripts/check_mod_layout.py`; engine edits `scripts/check_mod_compile.sh`. Build pass ≠ gameplay verified.
- Git: local first. GitHub: `python scripts/github_brief.py pr N` / `run ID --errors`, `--json` + bounded `--limit`. No log dumps, no polling, no secrets.
- Art: every visual is OoT or stylised 3D Zelda look.
- Preserve others' edits. End each feature with a short handoff: status, files, validation, next.

## Token tools
- RTK: `.claude/hooks/rtk.py` rewrites Bash commands to `rtk` when installed (`rtk init -g` / [rtk-ai/rtk](https://github.com/rtk-ai/rtk)). Not installed = no-op.
- Caveman: project enables plugin `caveman@caveman` (`.claude/settings.json`); accept the install prompt on trust. Rules above apply either way.
