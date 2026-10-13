# Headless playtests

Runs the real game (the PC/Linux build of the fork with the mod) with no screen, drives it with a step script, and reports each scenario in a
line or two: PASS, FAIL (a step timed out), CRASH (signal plus the top of the call stack with file:line) or HANG (no frame for 2 minutes, with
every thread's stack on disk). It catches crashes, asserts, failed resource loads and engine error logs before a build reaches the phone.

It runs on request only and is not part of the normal build: the driver (`features/AutoTest.inc`) is compiled only into PC builds (`#ifndef __ANDROID__`), so the Android game and CI never contain it.

It is not the Android build: GPU driver and Android-only code (`#ifdef __ANDROID__`: updater, crash handler, touch) are not exercised, and
the software renderer is slow, so it says nothing about frame rate on the Odin.

## Game files (private)

You need the `oot.otr` made from your own ROM (the phone's `TDawgsBattleRoyale/oot.otr`, or a PC extraction). It is personal and stays
out of git: in Claude project threads it is at `/mnt/project-files/private/oot.otr` (found by default); elsewhere keep it at `~/.royale-private/oot.otr` (or pass `--otr`, or set `ROYALE_OTR`). Never attach, link or share it. The runner only links
it into the run folder, which must be outside the repo (`~/.cache/royale-playtest/<scenario>/`). `.gitignore` already ignores `*.otr`.

## Build (once, then after each change)

```sh
tools/playtest/build.sh          # ~/royale-build; first build about 25 minutes on 4 cores, then only RoyaleMod.cpp + link
```
It lists any missing apt packages. Mod edits: re-run it; it copies only changed mod files into the fork (`sync_mod.py`), so only `RoyaleMod.cpp` recompiles. It needs ~7 GB free memory for that file: do not run two builds at once.

## Run

```sh
python3 tools/playtest/run.py              # suite "smoke": boot, solo-12, match-12, skin, regular
python3 tools/playtest/run.py maps         # a solo test on every map;  "matches": full matches with bots;  "scenes": scene tours
python3 tools/playtest/run.py solo-8 match-13
python3 tools/playtest/run.py --script "ingame; host:5; wait:30; check; quit"
python3 tools/playtest/run.py --list       # every scenario and suite
```
Output, one block per scenario:
```
PASS  boot               41s steps 4/4    log errors 0
CRASH match-12          212s steps 2/7    log errors 3  SIGSEGV in #2 DynaPoly_ExpandSRT z_bgcheck.c:3301 at step 3/7: state:Drop
      #0 ... (top frames)
      resource did not load: ...
      files: ~/.cache/royale-playtest/match-12
```
Full output per scenario sits in its run folder: `game-output.txt` (game + gdb, all thread stacks), `autotest-result.txt` (steps and the mod
state block at each `check` and at a failure), `royale-trace.txt`, `logs/`. `last-results.json` there holds every result as JSON.

## Step scripts

The in-game driver is `mod/Royale/features/AutoTest.inc`; it does nothing unless `ROYALE_AUTOTEST` is set. Steps, separated by `;`:
`ingame`, `wait:<sec>`, `cvar:<name>=<int>`, `skin:<n>`, `host:<map>` (solo test), `hostbots:<map>` (lobby + Start, bots fill it),
`state:<Lobby|Countdown|Drop|InMatch|Ending>`, `jump` (off the bus, then glider), `leave`, `regular` (normal quest, regular-game features),
`boots:<0|1|2>` (kokiri, iron, hover on Link), `tp:<x>,<z>[,<deg>]` (put Link on the ground there), `warp:<scene hex>`, `tour:<overworld|dungeons|all>`, `check`, `quit`. Times are game time (20 frames a second), so a slow renderer still plays
the same amount of game. Add a scenario for a new feature in `SCENARIOS` in `tools/playtest/run.py`.

## Workflow for agents

1. After a crash report or a new feature: build, then run the scenario that covers it (or `smoke`).
2. Ronnie (Haiku) runs suites and reports only the summary lines. Gary (Sonnet) takes a FAIL/CRASH, reads the stack and the run folder's
   `autotest-result.txt` state block, fixes, rebuilds and re-runs that scenario. Never paste whole logs.
3. Report a fix as verified only when its scenario passes here, and still say the phone has not run it.
