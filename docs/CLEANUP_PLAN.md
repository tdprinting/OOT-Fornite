# Cleanup plan: fewer tokens, easier navigation, zero gameplay change

Owner ask: optimise the whole codebase for agents without touching gameplay. Branch `claude/agent-friendly-cleanup`.

## Hard rules (every batch)
- Zero gameplay change. Zero network-protocol change (`shared/protocol.h` untouched; no `kProtocolVersion` bump).
- `scripts/apply_patches.sh` still applies cleanly. CI green (`ci.yml` jobs mod-layout, android-ndk, linux-unit-tests; `game-build.yml` when game files change).
- No mass file moves while PRs are open. Open-PR files (do not edit in batch 1): `.gitignore`, `client/game_client.h`, `docs/{BATTLE_BUS,BUILDING,ITEM_SURFACE_MAPS,WATER}.md`, `docs/water/README.md`, `mod/Royale/FEATURES.md`, `mod/Royale/RoyaleMod.cpp`, `mod/Royale/features/{Companions,Core,Equipment,Menus,MusicDiagnostics,SceneHooks,Vehicles,Water}.inc`, `mod/Royale/war_table/Controls.inc`, `patches/*`, `scripts/{check_mod_layout.py,link_mod.sh,make_item_surface_maps.py,test_*.py}`, `server/game_server.h`, `server/tests/*`, `shared/{ally,item_surface_maps,peter_*,protocol,water_look,water_sim}.h`, `tools/bus/*`, `tools/make_item_bump_maps.py`, `tools/peter/*`.
- Report line counts before and after each PR (commands below).

## Proof of no gameplay change
| Batch kind | Proof |
|---|---|
| Docs / ignore / settings only | `git diff --stat origin/main -- mod shared server client patches cmake scripts tools` is empty. `python scripts/check_mod_layout.py` passes. |
| Code moves (file splits, `.inc` reorder-free) | `python scripts/check_mod_layout.py --baseline origin/main` passes (expanded `RoyaleMod.cpp` byte-identical). For `server/*.h` splits: `g++ -std=c++17 -E -P` of `server/sim.h` before and after, `diff` empty. |
| Dead code removal | `git grep -w NAME` shows the definition only (whole repo, incl. patches, tests, tools). Compiles: server `ctest` (shared/server), `scripts/check_mod_compile.sh` or CI `game-build.yml` (mod). `--baseline` will fail here by design; proof is the grep plus compile. |
| Patch renames | `ls patches/*.patch \| sort` sequence identical before/after (names only change inside the same sort slot). `scripts/apply_patches.sh` on a clean submodule, then `scripts/check_surface_patch_stack.py`. |
| Any batch | Server tests: `cmake -S server -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/b && ctest --test-dir /tmp/b --output-on-failure` (needs `git submodule update --init --depth 1 third_party/enet`). Python: `scripts/test_water_draw_buffers.py`, `test_item_surface_maps.py`, `test_kingdom_authoring.py`, `test_moonfall_authoring.py`, `test_riftlands_integration.py`, `python3 -m unittest discover -s tools/token_monitor -p 'test_*.py'`. |

Line-count commands (run before and after, paste into PR):
- Hand-written C++: `wc -l mod/Royale/RoyaleMod.cpp mod/Royale/features/*.inc mod/Royale/war_table/*.inc mod/Royale/Royale*.h client/*.h server/*.h server/*.inc server/tests/*.cpp server/tools/*.cpp | tail -1` (now 40517).
- Docs: `wc -l *.md docs/*.md docs/*/*.md mod/Royale/FEATURES.md mod/Royale/war_table/INDEX.md | tail -1` (now 2440).
- Search surface: `rg --files | wc -l` (now 382) and `rg --files -0 | xargs -0 cat | wc -c` (now 19,258,111 bytes).

## Baseline facts (verified 2026-10-11)
- 861 tracked files. ~70 generated headers in `shared/` (header line 1-3 says generated + generator path). Largest: `riftlands_model.h` 527,773 lines, `moonfall_model.h` 207,481, `convergence_model.h` 173,109, `kingdom_model.h` 170,192.
- `RoyaleSession.h`, `RoyaleBosses.h`, `RoyaleChuChu.h`, `build_version.h` are not copied by `scripts/link_mod.sh`. They resolve through `cmake/royale.cmake` (`target_include_directories ... mod/Royale`). Not stale; do not rename them.
- `RoyaleChuChu.h` is live (`RoyaleBosses.h:165`, `tools/chuchu/check_renderer.cpp:37`). Only missing from `FEATURES.md`.
- Default map: Moonfall Isle (id 12). `docs/HYRULE_KINGDOM.md:3` still says "main and default" (wrong). `shared/map.h:125` description string says "The main map" for Kingdom: in-game text, leave it.
- Patch number collisions: `0023` x2, `0025` x2, `0028` x2, `libultraship/0004` x2. `apply_patches.sh` applies in shell glob order (alphabetical), so collisions are harmless today.

## Batch 1 (this PR): SAFE-NOW, docs + ignore + indexes + dead code outside open-PR files
Each step has a check. Nothing here touches an open-PR file.

1. `.rgignore`: append exactly
   ```
   /shared/*_data.h
   /shared/avriella_toys.h
   /shared/playtest_models.h
   /shared/maya_phone.h
   /shared/jones_guns.h
   /shared/boulder_texture.h
   /shared/basic_pitch_model.h
   /mod/Royale/logo_data.h
   /mod/Royale/gilded_sword_icon.h
   /tools/song-to-oot.html
   /tools/rom-extractor.html
   /tools/map-viewer.html
   /map-concepts/
   ```
   All `shared/*_data.h` are generated (convergence, fortnite_map, fortnite_cover, kingdom, moonfall, riftlands); the explicit `convergence_data.h`, `fortnite_map_data.h`, `kingdom_data.h` lines become redundant: drop them. Check: `rg --files | wc -l` = 358; `rg --files -0 | xargs -0 cat | wc -c` ≈ 6.9 MB (from 19.3 MB). Check `rg -l "GeneratePois" shared` still finds `shared/poi.h`.
2. `.claude/settings.json`: add
   ```
   "permissions": {"deny": ["Read(./shared/*_model.h)", "Read(./shared/*_data.h)", "Read(./shared/*_sounds.h)", "Read(./shared/war_table_assets.h)", "Read(./shared/kingdom_surface_maps.h)", "Read(./shared/avriella_toys.h)", "Read(./shared/playtest_models.h)", "Read(./shared/maya_phone.h)", "Read(./shared/jones_guns.h)", "Read(./shared/basic_pitch_model.h)", "Read(./mod/Royale/logo_data.h)", "Read(./mod/Royale/gilded_sword_icon.h)"]}
   ```
   Decision: yes, generated arrays only. Justification: every listed file says "generated, do not edit"; generators write them through `python` in Bash, which the deny does not touch; tests read them through the compiler or `python`, also untouched. A Read of a 30 MB array never helps. Do not deny `assets/**` (image Read is useful for previews), `third_party/**` (patch authoring needs engine source), or `tools/song-to-oot.html` (hand-edited, has tests). Keep `shared/item_surface_maps.h` readable: it is 42 lines and in an open PR. Check: Read tool on `shared/riftlands_model.h` is refused; `python3 scripts/test_riftlands_integration.py` still passes.
3. `CLAUDE.md` is the single rule file. Add under Rules: "Map of the repo: `docs/CODE_MAP.md`. Docs index: `docs/README.md`." Replace `AGENTS.md` body with 4 lines: Codex and other tools read this file; rules live in `CLAUDE.md`; start code lookup at `mod/Royale/FEATURES.md` and `docs/CODE_MAP.md`; keep the existing "Startup prompt" paragraph (moved here from `START-HERE.md`). Replace `START-HERE.md` with 2 lines pointing to `CLAUDE.md` and `AGENTS.md`. Drop the personal Windows RTK path and the circular read-each-other loop. `tools/token_monitor/index.html` only mentions "AGENTS.md" in prose; no link breaks. Check: `git grep -n "START-HERE"` hits only the two stubs.
4. New `docs/CODE_MAP.md` (≤120 lines): one line per hand-written file in `shared/`, `server/`, `client/`, `mod/Royale/*.h`, `scripts/`, `tools/` (per directory for `tools/<asset>/`: build_ = Blender author, export_ = writes `shared/<x>_model.h`, render_ = previews), `docs/`, `patches/` (one line per patch, numbered list, plus `libultraship/`, `ZAPDTR/`). Mark `[gen: tools/x.py]` on each generated header (use the header's own line 1-3). Note `tools/patch_war_table_engine.py` as one-off (patch 0025 landed) and `tools/test_*.js` as manual `node` tests. Source for one-liners: each file's first docstring/comment (`head -3`). Check: every path in the map exists (`python3 - <<'EOF'` loop over backticked paths).
5. New `docs/README.md`: list every `docs/*.md` with its first heading, grouped: design, build/run, maps, systems, graphics, menus, archive. Link `docs/archive/`.
6. `git mv` to `docs/archive/`: `JONES-NPC-HANDOFF.md`, `docs/WATER-POLISH-HANDOFF.md`, `docs/CHUCHU-FIX-HANDOFF.md`, `docs/PLAYTEST-EQUIPMENT-HANDOFF.md`, `docs/PLAYTEST_FIXES.md`. Verified zero inbound links (`git grep -l` on each basename = itself only). Add a 3-line `docs/archive/README.md` (dated handoffs, history only). Keep `docs/GRAPHICS_STABILITY.md`, `MAP_WATER_PERFORMANCE.md` in place (still describe live code).
7. Doc contradiction fixes (files not in open PRs): `docs/HYRULE_KINGDOM.md:3` "The main and default" → "A selectable map (id 8); Moonfall Isle (id 12) is the default". `docs/MAPS.md:4` "five battle royale maps" → "the five ROM-scene maps (ids 0-4); authored maps 5-12 live in `shared/map.h`". Check: `rg -n "main and default" docs` hits only `MOONFALL_ISLE.md`.
8. Dead functions, zero references repo-wide (`git grep -w` = definition only), files not in open PRs:
   - `shared/anim.h:56 EmoteIndex`, `shared/oot_music_notes.h:277 AssignParts`, `shared/poi.h:261 TownKindName`, `server/boss_helpers.inc:3 FindHelper`. Compiled by server tests (`sim.h`/`match.h`/`bot.h` include them). Proof: `ctest` green.
   - `mod/Royale/features/EndMatch.inc:195 UiTile`, `Lobby.inc:5 OpenRoyaleMenu`, `Ragdolls.inc:283 SpawnEmoteDouble`, `:356 RemoveTestRagdolls`, `:360 FlingTestRagdolls`, `:373 SpawnTestRagdoll` (and `:348 kMaxTestRagdolls`, `:350 CountTestRagdolls` once the three callers go; re-grep), `Updater.inc:45 UpdaterLong`. Anonymous-namespace functions, never called: the linker already drops them, so the binary does not change. Proof: `scripts/check_mod_compile.sh` locally or the `game-build.yml` run on the PR (it triggers on `mod/**`). If no engine compile is available, split these mod removals into their own PR (Batch 2a) and keep batch 1 docs-only.
   Leave `Core.inc` (`SceneName`, `RarityIm`) and `MusicDiagnostics.inc` (`DrawExitReason`, `DrawCrashReport`, `OotStatus`): open-PR files.
9. Zero-reference tool scripts: keep all. Evidence: `tools/bokoblin/make_sounds.py` generates `shared/bokoblin_sounds.h`; `tools/maps/riftlands/{props,author_nav}.py` are `import`ed by `export.py`/`world.py`; `gallery.py`, `render_preview.py`, `preview_lighting.py` are manual Blender QA; `tools/test_*.js` are manual node tests. Mark each as "manual" in `CODE_MAP.md` instead of deleting.
10. Final checks: `python scripts/check_mod_layout.py`; `git diff --stat origin/main -- shared server client patches cmake` shows only the step-8 files; line counts from above in the PR body.

## Later batches (one small PR each, in order)
| # | Batch | Precondition | Seam / content | Proof | Risk |
|---|---|---|---|---|---|
| 2 | `FEATURES.md` fixes | FEATURES.md PR merged | Add `RoyaleChuChu.h`, `RoyaleJones.h`, `build_version.h` to focused files; add "shared/server index: `docs/CODE_MAP.md`"; fix "lobby ragdoll test" (unreachable after step 8); fix Terrain.inc description if grenade launch moves. | docs-only diff | none |
| 2a | Dead code in open-PR files | Core/MusicDiagnostics/Menus PRs merged | `Core.inc` `SceneName`, `RarityIm`; `MusicDiagnostics.inc` `DrawExitReason`, `DrawCrashReport`, `OotStatus` (re-grep first). | grep + `check_mod_compile.sh` | low |
| 3 | Patch renumbering | all `patches/*` PRs merged | Rename collisions with letter suffixes that keep sort order: `0023-royale-custom-sword-hook` → `0023a-...`, `0023-royale-no-use-on-take-out` → `0023b-...`; same for `0025`, `0028`, `libultraship/0004`. Do not move to new numbers (`0030`): that reorders application. Update any name mentions (`git grep -n "0025-native"` etc., incl. `scripts/check_surface_patch_stack.py`, `docs/`). | `ls patches/*.patch \| sort` sequence unchanged; `apply_patches.sh` clean; `check_surface_patch_stack.py` | low |
| 4 | Split `server/tests/tests.cpp` (4809 lines) | `server/tests/*` PRs merged | Seams at its `// ----` markers: lines 161 Bot AI, 495 item catalog and loot, 1105 Bot pathfinding, 4200 bot movement, 4274 carts. New `server/tests/{bot_tests,loot_tests,bot_nav_tests,cart_tests}.cpp` added to the `foreach` lists in `server/CMakeLists.txt:34,80`. | same test count in `ctest -N`; all green | medium (shared helpers at top of file must move to a `tests/common.h`) |
| 5 | Split `server/match.h` (3125) | `server/*` PRs merged | Keep `class Match` one class; move member blocks into included `.inc` like `boss_helpers.inc`: lines 960 allies, 1076 carts, 1406/1951 mini bosses, 1640 Lilo cloud, 1802 boss movement, 2174 major bosses, 2544 sandbox map. | `g++ -E -P server/sim.h` diff empty; ctest | medium |
| 6 | Split `server/bot.h` (1967) | after 5 | Same `.inc` pattern at markers: 574 ground, 671 skydive, 732 sprinting, 949 reflexes, 1015 perception, 1039 looting, 1208 abilities, 1305 cover, 1382 brain, 1738 carts. | `-E -P` diff; ctest | medium |
| 7 | Split `Players.inc` (1219) | Players PRs merged | `Players.inc` → `Players.inc` (puppets, 1-194), `PlayerAnim.inc` (195-502), `PlayerSounds.inc` (367-502 if separable), `PlayerSpells.inc` (503-729), `PlayerSkins.inc` (1072-end). New files in `features/`, includes inserted in `RoyaleMod.cpp` at the same position; `check_mod_layout.py` requires every `features/*.inc` included exactly once. | `check_mod_layout.py --baseline origin/main` | low |
| 8 | Split `Hud.inc` (1163) | Hud PRs merged | 147 seasons/weather → `HudWeather.inc`, 520 item icons → `HudIcons.inc`, 960 emote wheel → `HudEmotes.inc`. | `--baseline` | low |
| 9 | Split `Vehicles.inc` (1137) | Vehicles PR merged | 3-172 emotes + 173-274 sounds → `Emotes.inc`; 275-end → `Vehicles.inc` (Lon Lon Buggy). | `--baseline` | low |
| 10 | Split `Terrain.inc` (1760) | Terrain PRs merged | Only one marker (line 1 Fortnite map). Needs a read to pick seams: custom scene build, map rendering, grenade launch (move to `Projectiles.inc`? no: that reorders; keep order, just new file). | `--baseline` | medium |
| 11 | `ROYALE_CVAR` boilerplate | Menus PR merged | 16 direct `CVarGetInteger(CVAR_SETTING("Royale.` uses across Vehicles, WorldObjects, Core, SceneHooks, Terrain, Equipment, LiloModel, Avriella → `ROYALE_CVAR` (defined `Menus.inc:3`). Macro expands to the same tokens. | `g++ -E` of `RoyaleMod.cpp` via `check_mod_compile.sh` diff empty | low, cosmetic |
| 12 | `RoyaleBosses.h` (1641) by boss family | boss PRs merged | Per-`BossKind` sections → `bosses/*.inc` included once. | `check_mod_compile.sh` + `nm` symbol list identical | medium |
| 13 | Generator helper `tools/_emit.py` | never urgent | Share array/WAV emission across `scripts/make_*_sounds.py`, `tools/*/export_*.py`. | regenerate + `git diff --exit-code shared/` (needs Blender for most) | high effort, low agent value; skip unless asked |
| 14 | `docs/DESIGN.md` (56 KB) | none | Add a 12-line TOC at top (its `## 1-10` + Appendix); no split. | docs-only | none |

## Rules to keep it clean (fold into `CLAUDE.md` after batch 1)
- Generated header: line 1 says `Generated by <path>; do not edit`. Add its path to `.rgignore` and the `permissions.deny` list in the same PR.
- New hand-written file: one line in `docs/CODE_MAP.md`. New doc: one line in `docs/README.md`. New feature `.inc`: a row in `FEATURES.md`.
- Handoff notes are dated and live in `docs/archive/`; live docs describe current behaviour only.
- Files over 1200 lines: split at `// ----` markers into `.inc` files, same include order, `check_mod_layout.py --baseline` proof.
- Patch names: keep sort order; letter suffix for collisions.
- Rules live in `CLAUDE.md` only. `AGENTS.md` and `START-HERE.md` are pointers.
