# Feature check tool: design

Implemented 2026-10-11 (steps 1-8; step 9 open, see "Implementation notes"). One command for agents, one tap for the user, one row per feature.

Run it: `python scripts/feature_check.py` (all features, layers 0-2b, ~2 min warm). Phone: Settings > Debug > SelfTest, then
`python scripts/feature_check.py --device-report royale-selftest.txt`.

## Goals

- An agent in the cloud container runs `python scripts/feature_check.py [feature...]` and gets PASS/FAIL per feature in under 3 minutes, with no ROM, device or GPU.
- Every feature's "does it work" is written down once, as a check, in the same PR as the feature. No check row, no merge.
- What the container cannot see, the device reports: one menu tap runs scripted checks in the game and writes `royale-selftest.txt` next to `royale-crash.txt`. The user sends the file; the agent reads it. No video, no typing out what happened.
- Reuse what exists: `server/tests/*`, `scripts/test_*.py`, `check_mod_layout.py`, `check_mod_compile.sh`, `CrashContext.inc`, the sandbox commands, the Debug switches.

## What can be tested where

Measured in this container (Linux, g++ 13, clang, cmake 3.28, ninja, sudo apt works, GitHub reachable):

| Code | Container | Device |
|---|---|---|
| `shared/`, `server/`, `client/` (rules, bots, protocol, maps, water maths, menu logic) | Full. `royale_tests` builds in 59 s cold, runs in 1 s. A 32-player bot match (`royale_balance 1 normal 12 32`) runs in seconds. | Not needed. |
| Whole host+client+bots flow (join, start, bus, drop, knock, revive, end) | Full, over `shared/loopback.h`. Deterministic by seed. Real UDP (`session_tests`) is timing-sensitive: 3 failures while a build ran beside it, 0 on a quiet rerun. Smoke tests use loopback only. | Only the real-network part (two phones). |
| `mod/Royale/**` compiling against the real engine | Yes. `git clone --depth 1` of Waterdish/Shipwright-Android at the pinned `c9d8f4a` plus `libultraship` took 8 s, 196 MB. All 32 `patches/*.patch` and 10 `patches/libultraship/*.patch` apply cleanly in order. The compile deps (`libsdl2-dev libzip-dev libspdlog-dev nlohmann-json3-dev libtinyxml2-dev libglew-dev libgl-dev libboost-dev`) install from apt. See "Layer 2a" for the configure result. | The real build is `game-build.yml` (Android only; there is no Windows CI job). |
| Pure functions inside `features/*.inc` (matrix maths in `Skins.inc`, `PadUnderfoot` in `Squads.inc`, `Apply` in `LinkAnims.inc`, `WornGear`/`SyncLocalDress` in `Terrain.inc`) | Yes, by extracting the function text and compiling it with stubs. `scripts/test_playtest_equipment.py` already does this (Windows `cl.exe` only). | Not needed. |
| Drawing: display lists, actor draw hooks, patch 0031 skin limbs, HUD ImGui, water sheets, shadows | Bookkeeping only (`graphics_layers_tests`, `water_tests`, draw-buffer bounds). No pixels: the engine's renderer needs the OTR and a GL context. | Yes: counters (limbs seen/drawn, pool overflow, actor counts) and the user's eyes. |
| Engine behaviour: collision (patch 0013/0029), heaps, scene loads, archive reads (lus 0009), startup guard (patch 0030, Java), updater, touch input, audio, Peter's voice files | No. | Only here. Crash → `royale-crash.txt`; no crash → `royale-trace.txt` state blocks and `royale-selftest.txt`. |
| Performance on the Odin 2 | `royale_sim_bench` for the host tick only. | Frame time: `SecondsSinceLaunch` deltas in the self-test. |

## Architecture

Four layers. One entry point. Every layer prints the same line format.

```
scripts/feature_check.py            agent CLI; reads scripts/feature_checks.json; runs layers 0-2; summarises
scripts/feature_checks.json         the registry: feature -> files, checks per layer, device checks
scripts/engine_setup.sh             one-time per container: fork submodule (depth 1), apt deps, patches, cmake configure
scripts/test_mod_functions.py       layer 2b: extract functions from features/*.inc, compile with stubs (g++), run
scripts/mod_function_tests/*.cpp    one stub+assert file per feature for layer 2b (the body test_playtest_equipment.py has today, split up)
server/tests/feature_smoke.cpp      layer 1: scripted matches over loopback, one SMOKE per feature; target royale_feature_smoke
mod/Royale/features/SelfTest.inc    layer 3: on-device scripted checks, writes royale-selftest.txt; last include in RoyaleMod.cpp
```

### Layer 0: what exists, wrapped

`feature_check.py` runs, per selected feature, the ctest targets and python scripts named in the registry. Nothing new. It builds `server/` once into `build-check/` (Release, no sanitizers; CI keeps the sanitizer build) and reuses it.

### Layer 1: feature smoke (`server/tests/feature_smoke.cpp`)

The real `GameServer` and real `GameClient`s on `net::LoopbackNetwork(seed)`, real bots, sim time only (`kTickHz` = 20, `dt` = 0.05, never wall clock).

Harness (in the same file, ~150 lines):

```cpp
struct Rig {                       // one host, N scripted humans, the rest bots
    Rig(uint64_t seed, int mapId, int humans, TeamMode mode = TeamMode::Solo);
    GameServer& server; std::vector<GameClient> clients;  // clients[0] is the host's own client
    float now = 0;                 // sim seconds
    void Tick();                   // server.Update(dt); every client.Update(dt); now += dt; drain events into log[i]
    bool Until(std::function<bool()> cond, float maxSec);   // ticks until cond or timeout; false on timeout
    void Script(int client, std::function<void(Rig&)> perTick);  // a scripted human: send poses, attacks, requests each tick
    std::vector<std::vector<ClientEvent>> log;   // every ClientEvent each client drained, for asserts
    int Count(int client, ClientEvent::Type t) const;
};
#define SMOKE(name) static void Smoke_##name(Rig&); static Registered reg_##name(#name, Smoke_##name); static void Smoke_##name(Rig& rig)
#define CHECK(c)  // same as tests.cpp: prints "FAIL file:line  cond", counts
```

`main(argc, argv)`: `royale_feature_smoke [--list] [--seed N] [--json] [name...]`. No names = all. Each smoke runs under three seeds (1, 2, 3) unless `--seed`. Prints `PASS smoke.<name>  <sim seconds>s  (<wall ms> ms)` or the CHECK lines then `FAIL smoke.<name>`. Exit 1 on any fail. Each smoke must finish under 20 s wall; the harness kills it at 60 s and reports `FAIL smoke.<name>  timeout`.

Smokes to write first (one per recent feature; names are the registry keys):

| Smoke | Script | Asserts |
|---|---|---|
| `match_flow` | host + 1 guest, 30 bots, Moonfall (12). Host `StartMatch()`. Guest sends a pose at the bus seat, then the drop. | `StateChanged` Lobby→Countdown→Drop→InMatch→Ending in order; exactly one winner in `EvResults`; `Alive()` monotone; match ends under 600 sim s. |
| `bus` | as above; guest sends `Anim::BusHorn` once, jumps (non-seat anim) 1 s after `BusDoorsOpen()`. | `BusPath` event once per client; `Aboard()` true at Drop start, false after the jump; `BusHorn` event with guest id; a player that never jumps is forced off by `BusCrossSec`; damage while `BusProtected` is 0. |
| `teams` | `TeamMode::Duos`, 2 guests on one team. Guest A reports a hit on its teammate; guest B is damaged to 0 by a bot script (`server.Sim().match.Damage`). | teammate damage event amount 0; B gets `Knocked` not `Eliminated`; A `HoldRevive(B, true)` next to B for 3 s → `TeamRevived`; `SendPing` → `Pinged` reaches A and B, not a stranger; `PadsChanged` count equals `Match` pad count. |
| `skins` | guest `SetSkin(1)` before `Join`; another `SetSkin(200)`. | roster carries skin 1 for the first; 0 for the invalid one; `PlayerJoined` events carry it; every bot's skin passes `ValidSkinModel`. |
| `allies_chief_peter` | solo host with rupees forced (`match.Find(id)->rupees`), stand next to ally index 5 then 6, `HireAlly`. | `AllyChanged` with kinds Peter then Chief; Chief ally snapshot stays 13 bytes (`net::AllyNet` size static_assert); Chief fires (an `AllyAction` event) at a bot within 30 sim s. |
| `map_skyloft` | host `SelectMap(13)`, start. | `MapChanged` 13 on every client; every loot spot inside `MapOf(13).fallback`; 31 bots alive at InMatch; at least 10 eliminations in 240 sim s (bots can move on the map). |
| `shockwave` | guest holds `ShockwaveGrenade` (force via `match`), `UseStack`. | one `Strike` event after `kShockwaveFuse` ± 1 tick; damage to a bot stood 1 unit away equals the catalog's blast damage; none at 3× the radius. |
| `regular_switches` | none (server-only sanity): `kRegularSettings` keys are unique and non-empty. | (tiny; keeps the registry row honest) |

Smokes assert on `ClientEvent`s and `HudState`-like client queries, never on `Match` internals, except to force setup (rupees, items, damage). That keeps them testing the wire.

### Layer 2a: compile the mod against the engine (`scripts/engine_setup.sh` + existing `check_mod_compile.sh`)

`engine_setup.sh` (idempotent, ~40 lines):
1. `git submodule update --init --depth 1 --recursive third_party/Shipwright-Android` and `third_party/enet` (8 s measured for the fork + libultraship; ZAPDTR and OTRExporter are needed by the fork's CMake).
2. `sudo apt-get install -y` the dep list above, only if `pkg-config --exists sdl2` fails.
3. `scripts/apply_patches.sh` (already idempotent).
4. `cmake -S third_party/Shipwright-Android -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release` (FetchContent downloads imgui, spdlog etc. through the proxy).
5. Prints `engine ready` or the first CMake error.

Then `check_mod_compile.sh build-engine` as it is today (gcc object, clang `-fsyntax-only`, `nm` for undefined anonymous-namespace symbols). `feature_check.py --engine` runs both; without `--engine` it reports `SKIP engine (run scripts/engine_setup.sh)`. Configure result from this container: see "Status" at the end of this doc.

New: `scripts/check_patch_apply.sh`: `git apply --check` each patch in order on a clean checkout of the pinned fork commit (a `git worktree` of the submodule, so nothing in the working tree changes). Runs in layer 0 once the submodule is present. Catches a patch that drifted when another agent regenerates it.

### Layer 2b: pure mod functions with stubs (`scripts/test_mod_functions.py`)

Port of `scripts/test_playtest_equipment.py` to g++ (it only needs `cl.exe` replaced and `/EHsc` dropped). Generalise: each `scripts/mod_function_tests/<feature>.cpp` starts with directive comments the script reads:

```cpp
// EXTRACT mod/Royale/features/Squads.inc  PadUnderfoot StripButtons
// EXTRACT mod/Royale/features/LinkAnims.inc  Apply
```

The script concatenates `shared/` includes, the file's own stubs, the extracted function bodies (`function()` from `test_war_table_matrix.py`, moved into `scripts/mod_source.py`), then `main()`, compiles with `g++ -std=c++17 -I shared -I mod/Royale -fsyntax-only`-then-link, runs. Output `PASS fn.<feature>` / compiler or assert text then `FAIL fn.<feature>`. First files: `equipment.cpp` (today's body, unchanged asserts), `squads.cpp` (`PadUnderfoot` true at pad centre, false at `kPadRadius + 1`; `StripButtons` leaves only Start and L), `link_anims.cpp` (`Apply` at blend 0 leaves joints as given; at blend 1 equals the clip frame; mid-blend lies between), `skins_math.cpp` (`AffMul(A, AffInverse(A))` is identity to 1e-4; `RotationBetween(v, v)` is identity).

Rule for new game-side code: if a function needs no `PlayState*`, put it in `shared/` and test it under ctest. Layer 2b is for what already lives in `.inc` files.

### Layer 3: on-device self-test (`mod/Royale/features/SelfTest.inc`)

One state machine stepped from `OnGameFrameUpdate` (hook registered in `SelfTest.inc`'s own `InstallSelfTest()`, called from `RegisterRoyaleMod` next to `InstallCrashContext`). It is included last so it can read every feature's state, like `CrashContext.inc`.

Trigger (any of):
- Menu: Settings > Debug page, a `WarRow(404, 4, "RUN SELF-TEST", ...)` next to `gStartupLine` (`war_table/Controls.inc` ~line 649). Add this only after the current war_table work lands; until then use the next trigger.
- CVar `Royale.SelfTest.Run` = 1 (the engine's CVar editor, or set by the menu row). Cleared when the run starts.
- The session is Idle (no lobby). A human-hosted lobby never runs it.

Script (each step a `SelfCheck { const char* feature; const char* name; Result (*fn)(SelfTestCtx&); }` in one table; `Result` = Pass/Fail/Skip/Wait with a `char detail[160]`; a check returning Wait is called again next frame until its own deadline, default 10 s):

1. `session.host`: `gSession.Host(0, "SelfTest", &err, /*soloTest*/ true, /*sandbox*/ true)` on the Sandbox map. Wait until `Joined()`.
2. `session.start`: `StartMatch()`; wait `MatchState::InMatch` (sandbox skips countdown and end). Detail: seconds taken.
3. `scene.loaded`: `gPlayState != nullptr`, `InField()`, `gFortniteBuilt` (island collision built) within 15 s. Detail: actor count.
4. `crash.context`: `gCrashStateNow >= 0`, block length under `kCrashStateLen`, contains "Match:" and "Me:" lines.
5. `loot.actors`: `gLoot.size() > 0` and each entry with an actor within draw range has `gLootOf` set. Detail: `n loot, m actors`.
6. `skins.draw`: `SetSkinModel(1)` before hosting (step 1 reads the registry's `skin` field); after 30 frames `gSkinStats.limbsSeen >= 18 && limbsDrawn >= 1` (two counters added to `Skins.inc` `OnPlayerSkinLimb`, reset each frame like `gGearReport`). Skip if `!DebugOn(kDbgSkins)`.
7. `link_anims.jump`: call `StartLinkJumpClip()`; within 1 s `gOurJump` is true and `PoseLocalLinkClips` reports blend > 0 (expose `gLinkClipBlend` from `LinkAnims.inc`). Fail if the blend never rises.
8. `bus.phases`: skipped in sandbox (no drop). Replaced by `bus.model`: `BuildBusGpu()` succeeded (`gBusGpu.valid`) and `Bus_Draw` ran at least once in a lobby frame before hosting (the lobby bus model in the waiting room).
9. `squads.pad`: `SandboxDo(SandboxCmd::Go, 0)` then teleport Link onto the first pad from `gSession.Client()->Pads()`; next frame `PadUnderfoot()` true; `LaunchFromPad()` sets upward velocity. Skip if the sandbox map has no pads.
10. `water.surface`: `WaterSurfaceAt` at the sandbox's known pool coordinates returns a finite height within 50 units of the authored value; `gWaterDetail` ≥ 1.
11. `graphics.pool`: after 60 frames `gGfxPool.overflowLast == false` and `gGfxPool.dataMisses` unchanged over the last 20 frames.
12. `shadows.lights`: with `DebugOn(kDbgShadows)`, `AddLightFlash(...)` at Link, next frame the light count > 0.
13. `hud.window`: `EnsureHudWindow()` ran and the HUD drew (a frame counter in `Hud.inc`'s draw increments over 10 frames).
14. `allies.hire`: `SandboxDo(SandboxCmd::Go, 1)` (armory), set rupees via `gSession.SandboxMatch()->Find(selfId)->rupees = 999`, `HireAlly(6)` (Chief) next to the ally; within 5 s `gAllies` has one entry with kind Chief and its actor is non-null.
15. `startup.guard`: `.launch-pending` and `.launch-guarded` absent in the app directory; `startup-times.txt` newest line parses.
16. `peter.voices`: count of files loaded by `RoyalePeter.h`'s device clip loader ≥ 0 and the decode-error count is 0 (expose two ints). Skip on PC.
17. `frame.time`: average frame delta over the run under 80 ms (12.5 fps floor on the Odin 2), max under 500 ms. Detail: avg/max.
18. `session.leave`: `gSession.Leave()`; `Joined()` false within 2 s; scene returns to the lobby.

Each step calls `Crumb("selftest %s: %s", name, result)` so a crash mid-run lands in `royale-crash.txt` with the step named. `Feat("self test")` at the top of the step.

Report `royale-selftest.txt` (`Ship::Context::GetPathRelativeToAppDirectory`, same folder as the crash file, overwritten each run; the previous one becomes `royale-selftest-prev.txt`):

```
Build 0.312  protocol 44  map 5 (Sandbox)  solo sandbox  started 184.2 s after launch
PASS  session.host        0.4 s
PASS  scene.loaded        actors 212, 3.1 s
FAIL  skins.draw          limbs seen 21, drawn 0 (expected >= 1)
SKIP  squads.pad          no pads on this map
...
15 passed, 1 failed, 2 skipped, 41.7 s
---- mod state ----
<the BuildCrashState block>
---- last events ----
<the 32 breadcrumbs>
---- end ----
```

The summary line is kept as `gSelfTestLine` and shown on the Settings page like `gStartupLine` ("Self-test: 15 passed, 1 failed (skins.draw)"), and in the Battle Royale menu top line on the next launch if any failed (same place the crash report is announced). The user sends the file or reads the one line aloud; either names the failing feature.

The same table runs on Windows (PC path is the same code; `__ANDROID__`-only steps are Skip).

### Layer 4: registration (`scripts/feature_checks.json`)

```json
{
  "squads": {
    "files": ["mod/Royale/features/Squads.inc", "server/match.h#Knock"],
    "ctest": ["royale_tests"],
    "smoke": ["teams"],
    "fn": ["squads"],
    "device": ["squads.pad"],
    "manual": ["L pings in Duos with a second phone"]
  }
}
```

- `files`: globs/paths; `--changed` maps `git diff --name-only <base>` to features through them. A changed file matching no feature fails the run with "add a registry row".
- `ctest`, `smoke`, `fn`: what layers 0, 1, 2b run. `device`: the `SelfTest.inc` step names the feature owns (the script asserts every table entry's feature exists in the registry: `scripts/check_mod_layout.py` reads `SelfTest.inc` with `read_mod_source` and the JSON, and fails on an unknown feature or a feature `.inc` with no row).
- `manual`: what only a person can confirm. Printed at the end as "manual: ..." so the handoff says it plainly.

`check_mod_layout.py` gains these asserts (same PR as the registry), so CI's `mod-layout` job enforces the rule from day one.

## CLI

```
python scripts/feature_check.py [feature...] [--changed [BASE]] [--layers 0,1,2] [--engine] [--seed N] [--json PATH] [--list]
```

- No args = every feature, layers 0-2b (not the engine). `--changed` = features whose files differ from `origin/main` (or BASE). `--engine` adds layer 2a (needs `engine_setup.sh` once). `--list` prints the registry and which checks exist per layer.
- Builds `build-check/` once (`cmake -S server -B build-check -G Ninja -DCMAKE_BUILD_TYPE=Release`, then only the targets needed).
- Output, one line per check, fixed columns, no colour:

```
feature        layer  check                     result  time    note
squads         0      royale_tests              PASS    1.2s
squads         1      smoke.teams               PASS    3.4s    seeds 1,2,3
squads         2b     fn.squads                 FAIL    0.9s    squads.cpp:41 assert PadUnderfoot(...)
skins          2a     engine compile            SKIP            run scripts/engine_setup.sh
skins          3      device skins.draw         DEVICE          ask the user to run Settings > Debug > RUN SELF-TEST
manual: squads: L pings in Duos with a second phone
7 passed, 1 failed, 1 skipped, 2 device, 48 s
```

- On FAIL the first 8 lines of that check's output follow the table, indented. Full logs go to `build-check/feature_check/<check>.log`; the tool never dumps them.
- Exit 1 if any FAIL. `--json` writes the same rows as a list of objects (for `github_brief.py`-style summaries later).
- `--device-report PATH` parses a `royale-selftest.txt` the user sent and prints its rows in the same table, so one run shows container and device results together.

## Recent features: inventory and the check for each

From the last ~60 commits. "Layer" is where the check lives; "today" is what exists already.

| Feature (commit/PR) | Today | Check to add | Layer |
|---|---|---|---|
| Player skins, protocol 44 (#147, f5e3884) | `ValidSkinModel`; skin refs in `tests.cpp` | `smoke.skins`; `fn.skins_math`; device `skins.draw` (limb counters) | 1, 2b, 3 |
| Bug sweep + richer crash reports (#146, 963d97a) | none | device `crash.context`; engine compile | 3, 2a |
| Scene collision crash fix, patch 0013/0029 (#145, a328136) | `check_surface_patch_stack.py` | `check_patch_apply.sh`; device `scene.loaded` on Moonfall (self-test variant `--map 12`) | 0, 3 |
| Patch 0029 dyna list / heap sizes (6e0c83d, 90260d4) | none | patch apply; `engine_limits.h` static asserts in `tests.cpp`; device `scene.loaded` actor count | 0, 3 |
| Skyloft Isles, map 13 (06eaa63, b772b33) | `royale_skyloft_tests`, `test_skyloft_authoring.py` | `smoke.map_skyloft` | 0, 1 |
| Startup crash guard, patch 0030 (4f6513b) | none | device `startup.guard` | 3 |
| Snappier jump + Link clips (24d98dc) | none | `fn.link_anims`; device `link_anims.jump` | 2b, 3 |
| Master Chief ally, protocol 42 (2f1a3cd) | `tests.cpp` Chief refs, `ally_model_tests` | `smoke.allies_chief_peter`; device `allies.hire` | 1, 3 |
| Archive read crash fix, lus 0009 (b85e856) | none | patch apply; device: scene loads in the run (lobby → sandbox → lobby) without a crash | 0, 3 |
| Moonfall Bus server + mod (fe4bf7c, 5e78625, #136) | 8 bus tests in `tests.cpp`, `net_tests` | `smoke.bus`; device `bus.model` | 1, 3 |
| Teams / knock / revive / pings / pads, protocol 40 (87094c6, #139) | 12 tests in `tests.cpp` | `smoke.teams`; `fn.squads`; device `squads.pad` | 1, 2b, 3 |
| In-match HUD blend (3b7e91b) | none | device `hud.window`; manual: look | 3 |
| Pause menu blend (66f05da) | `war_table_tests` | manual: look | 0 |
| Regular game: water/sky/companions (10e0019, #137) | none | device self-test variant when launched from a normal save: `water.surface`, `shadows.lights` only | 3 |
| Perf pass (#138) | `royale_performance_tests`, `royale_sim_bench` | `sim_bench` fingerprint unchanged for seed 1 (layer 0 row) | 0 |
| Smarter bots (#140, cdafc5f) | many bot tests | `smoke.match_flow` (ends with one winner) | 1 |
| Keep ROM files across updates (9ae1652) | none | device `startup.guard` ("kept game files" line) | 3 |
| Hookshot aiming; bombs wait for B (a435aa1); A jumps while Z-targeting (afadd42) | none | `fn.inputs` if the deciding functions in `Terrain.inc`/`Vehicles.inc` can be extracted without `Player*` internals; else manual | 2b or manual |
| Peter voice clips from device (8c16b3b) | none | device `peter.voices` | 3 |
| Items always draw: shader fallback (e96a0a2, #134) | `test_item_surface_maps.py`, `test_item_surface_shaders.py` | device: material hook call count > 0 with `ItemMaps` on | 0, 3 |
| Gear report (5b1836f) | none | reused by `skins.draw` | 3 |
| Peter ally (209b65f) | `tests.cpp` | `smoke.allies_chief_peter` | 1 |
| Shockwave Grenade rework (837f96c) | `ShockwaveGrenade` test; `test_playtest_equipment.py` (Windows) | `smoke.shockwave`; `fn.equipment` on Linux | 1, 2b |
| Water HQ (05e64cd, #133) | `water_tests`, `water_bench` | device `water.surface`, `graphics.pool` | 0, 3 |
| Moonfall Isle map (8875715) | `royale_moonfall_tests` | `smoke.match_flow` runs on 12 | 0, 1 |
| Engine limits (f12ded9) | `engine_limits.h` | patch apply | 0 |

## Implementation plan (in order; each step ships alone)

1. **Registry + CLI, layer 0 only.** `scripts/feature_checks.json` with one row per `features/*.inc` plus `server`, `client`, `patches`, `maps`. `scripts/feature_check.py` as above, running only `ctest` and python rows. `check_mod_layout.py` asserts every `.inc` has a row. Rows in `docs/CODE_MAP.md`, `docs/README.md`.
   Accept: `python scripts/feature_check.py` passes in a clean container under 3 min; `--changed` on a branch touching `Squads.inc` selects only `squads`; adding `features/New.inc` without a row fails `check_mod_layout.py`.
2. **Feature smoke harness + `match_flow`, `bus`, `teams`.** `server/tests/feature_smoke.cpp`, CMake target and `add_test`. Hook into the registry.
   Accept: the three pass on seeds 1-3 in under 20 s each; a deliberate break (comment out `Revive`'s `TeamRevived` event) fails `teams` and names the line.
3. **Smokes `skins`, `allies_chief_peter`, `map_skyloft`, `shockwave`.**
   Accept: same as 2; `ctest` total under 2 min.
4. **Layer 2b on Linux.** Move `function()` to `mod_source.py`; `test_mod_functions.py`; split today's equipment body into `scripts/mod_function_tests/equipment.cpp`; add `squads.cpp`, `link_anims.cpp`, `skins_math.cpp`. `test_playtest_equipment.py` becomes a thin Windows wrapper or is deleted (update CODE_MAP).
   Accept: all four pass with g++ in the container; CI `linux-unit-tests` runs `test_mod_functions.py`.
5. **Engine setup.** `scripts/engine_setup.sh`, `scripts/check_patch_apply.sh`; `--engine` in the CLI. `.gitignore` `build-engine/`, `build-check/`; `.rgignore` and `.claude/settings.json` deny for both.
   Accept: fresh container, `engine_setup.sh` then `check_mod_compile.sh build-engine` prints "RoyaleMod.cpp compiles against the fork" and "clang agrees"; second run of setup is a no-op under 10 s. Record the wall times in this doc.
6. **On-device self-test.** `features/SelfTest.inc` with steps 1-5, 11, 13, 17, 18 first (no new counters in other files). Include last in `RoyaleMod.cpp`; row in `FEATURES.md`; section "Self-test" in `docs/BUILDING.md` under the crash report section; CVar trigger only.
   Accept: layer 2a compiles; `game-build.yml` builds; the user sets `Royale.SelfTest.Run` (or the menu row once war_table work is merged), gets `royale-selftest.txt`, and `feature_check.py --device-report` prints it.
7. **Self-test counters in features.** Steps 6-10, 12, 14-16: add `gSkinStats`, `gLinkClipBlend`, HUD frame counter, Peter clip counters. Menu row `RUN SELF-TEST` in `Controls.inc` and `gSelfTestLine`.
   Accept: a run on the Odin 2 reports every step as PASS/FAIL/SKIP, none as timeout; the file is under 8 KB.
8. **CI.** `ci.yml` `linux-unit-tests`: `python3 scripts/feature_check.py --layers 0,1,2 --json build-check/feature_check.json`, artifact the JSON. `game-build.yml` unchanged (the self-test ships inside the APK).
   Accept: CI green; a broken smoke turns the PR red with the check name in the log's last 20 lines.
9. **Agent habit.** `CLAUDE.md` Rules: "New feature: a row in `scripts/feature_checks.json` and a check in the same PR. Run `python scripts/feature_check.py --changed` before the handoff; paste its summary line." `.claude/agents/gary` and `leader` get the same line.

## Risks

- **Timing.** Wall-clock tests flake in this container (`session_tests`: 3 failures under load, 0 quiet). Smokes use loopback and sim time only; `feature_check.py` runs ctest targets serially, not `-j`.
- **Engine configure.** The fork's CMake pulls ZAPDTR, OTRExporter and FetchContent deps; a missing package or a proxy-blocked download stops layer 2a. The CLI reports SKIP with the first error line instead of failing the run. Record what worked in "Status".
- **Self-test cannot press buttons.** It calls the functions behind the inputs (`StartLinkJumpClip`, `LaunchFromPad`, `HireAlly`). Input wiring (`SquadInput`, `BusInput`) stays manual; the registry's `manual` field says so.
- **Self-test in a shared lobby.** Guard: runs only when `gSession.GetMode() == Idle`; hosts solo sandbox; leaves at the end. It never joins someone else's lobby.
- **File growth.** `SelfTest.inc` stays under 400 lines; counters in other features are one line each. `Hud.inc` (1430) and `Terrain.inc` (1805) are already over the 1,200 split line; add nothing there beyond a counter.
- **Concurrent edits.** `war_table/*`, `Effects.inc`, `EndMatch.inc`, `EngineBridge.inc` are being edited now. Steps 1-6 touch none of them; step 7's menu row waits for that merge.
- **Extraction fragility (2b).** `function()` is regex-based; a function whose signature spans lines or uses default args with parentheses breaks it. Keep extracted functions short and named in the registry so a rename fails loudly.
- **Android file writes.** `royale-selftest.txt` uses the same directory and API as `royale-crash.txt` (proven on device). Keep it under 8 KB; write once at the end plus a one-line progress file `royale-selftest-running.txt` so a crash mid-run still shows which step.
- **Protocol drift.** Smokes decode real packets; a `kProtocolVersion` bump with a missed field shows up as a `badPackets` count. `Rig` asserts `server.stats.badPackets == 0` after every smoke.

## Implementation notes (choices the design left open)

- Trigger: the self-test is a Debug switch, `SelfTest` (CVar `gSettings.Royale.Debug.SelfTest`), not a new `Royale.SelfTest.Run` CVar or a
  `Controls.inc` row: the Debug page lists every switch already, so the user gets a menu button with no war_table edit. It defaults to off
  and turns itself off when a run starts (a crash mid-run does not loop). `gSelfTestLine` is kept and crumbed; showing it on the Settings
  page and the menu top line waits for the war_table work.
- Self-test steps done: 1-7, 11-13, 15, 17, 18 (`skins.draw` via `gSkinStats` in `Skins.inc`, `hud.window` via `gHudDraws` in
  `EndMatch.inc`, `link_anims.jump` reads `link_clip::gJumpW`, no new export). Not yet: 8 `bus.model`, 9 `squads.pad`, 10 `water.surface`,
  14 `allies.hire`, 16 `peter.voices`. A failed `session.host`/`session.start` skips to `session.leave`.
- The self-test runs in the Sandbox only; the Moonfall (`--map 12`) variant from the inventory table is not built.
- Smokes: each runs seeds 1-3 in one process; the 60 s kill is the CLI's subprocess timeout (180 s per smoke for three seeds), not a thread.
  `regular_switches` became `fn.regular_switches` (its table lives in `Core.inc`, not the server). The Chief's "13 bytes" assert was dropped
  (`AllyNet` is encoded field by field; `net_tests` round-trips it). Bots carry no skin on the wire, so `smoke.skins` checks the roster only.
  `smoke.shockwave` checks a shove, not damage: the grenade does no damage since 837f96c. A bus rider who never jumps is pushed off at
  `BusCloseSec` (the island's far shore, `Match::TickBus`), not `BusCrossSec`.
- `KNOWN(cond, note)` in `feature_smoke.cpp`: a reported, unfixed bug prints `KNOWN ...` every run and the row's note says "known issue", but
  it does not fail (so CI stays green). Turn it into `CHECK` with the fix.
- A failing ctest or smoke is rerun once; passing then is `FLAKY` (not a failure). `royale_tests` and the others run once and are shared by
  every row that names them.
- `--changed` diffs the working tree against BASE (default `origin/main`) plus untracked files; `path#symbol` in `files` means the whole file.
  Catch-all rows (`shared`, `docs`, `tools`, `scripts`, `repo`) keep every path owned.
- Layer 2a: `--engine` compiles with `check_mod_compile.sh` in `--engine-build` (default `build-engine/`, or `$ROYALE_ENGINE_BUILD`).
  `check_patch_apply.sh` runs `apply_patches.sh` twice on `git archive` copies (`ROYALE_FORK`), so it also proves a rerun is clean.
- `test_playtest_equipment.py` deleted; its body is `scripts/mod_function_tests/equipment.cpp` (one stub added: `InRegularGame`).
- `.claude/settings.json` deny was not changed: `build-check/` and `build-engine/` are build output (gitignored, `.rgignore` `/build-*/`),
  not generated sources. Step 9 (CLAUDE.md and agent lines) is left to the user: suggested line "New feature: a row in
  `scripts/feature_checks.json` and a check in the same PR. Run `python scripts/feature_check.py --changed` before the handoff; paste its
  summary line."

## Findings from the first run (2026-10-11)

- Fixed: `scripts/apply_patches.sh` was not idempotent. On a fully patched fork a rerun stopped at `0005` ("neither applies nor is already
  applied") because `0018` adds lines next to `0005`'s (and `0009` next to later limb hooks), so their exact reverse check fails. It now
  finds the top of the applied stack (the last patch whose reverse applies and forward does not) and skips everything up to it. Test:
  `check_patch_apply.sh` (fails on the old script, passes now). This broke every second run of `check_mod_compile.sh`/`engine_setup.sh`.
- Known, not fixed (map decision): authored chest sites outside the map circle start inside the storm. Moonfall: the Death Mountain summit
  site (4700, -5000) is 6862 from the centre of the 6500 circle, picked every seed. Skyloft: about 5 of 84 sites per match on the Lumpy
  Pumpkin islet and Beedle's Perch (up to 7226 out). `shared/map.h` circles `{{0,0},6500}`; sites from `tools/maps/{moonfall,skyloft}`.
  Fix: grow those maps' circles (and `maxRadius`) to cover the islets, or drop sites outside the circle in `GameServer::Reconfigure`'s
  `MoonfallPlacementValid`/`SkyloftPlacementValid` wrappers. `KNOWN` in `smoke.match_flow` and `smoke.map_skyloft`.
- Not a bug, noted: in a field with mini bosses, an unannounced hit on the reviver cancels a revive (by design); `smoke.teams` runs with
  bosses off.

## Status (measured 2026-10-11, this container)

- `royale_tests`: build 59 s cold, run 1 s, all pass. `royale_balance 1 normal 12 32`: seconds. `royale_session_tests`: 51 s and 3 failures under load; 11 s and 0 failures quiet.
- Fork clone at `c9d8f4a` + libultraship: 8 s, 196 MB. Patches: 32/32 and 10/10 apply in order. apt dep install: works with sudo.
- Fork CMake configure (`cmake -S <fork> -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, patches applied, ZAPDTR and OTRExporter submodules at depth 1): rc 0 in 25 s, after installing exactly `libsdl2-dev libzip-dev libspdlog-dev nlohmann-json3-dev libtinyxml2-dev libglew-dev libgl-dev libboost-dev libpng-dev` (adding `libzip-tools zlib1g-dev libbz2-dev` to that line made apt fail; leave them out). FetchContent downloads went through the proxy.
- `RoyaleMod.cpp.o` compile (`check_mod_compile.sh`, gcc object + clang `-fsyntax-only` + `nm`): 4 min 5 s on 4 cores with `SelfTest.inc`;
  passes. `engine_setup.sh` on a configured build: 0.3 s (no-op).
- `feature_check.py` warm (server already built, no `--engine`): 128 s, 81 pass, 2 skip (glslangValidator absent; engine), 13 device.
  The two map authoring scripts are 83 s of that. With `--engine`: 491 s. Cold `build-check/` build of every target: about 5 min here.
- `royale_feature_smoke`: 7 smokes x 3 seeds in 6 s (match_flow 3.8 s; matches end at 314-441 sim s). `test_mod_functions.py`: 5 files, ~10 s.
- `check_patch_apply.sh`: 43 patches, two passes, 10 s.
