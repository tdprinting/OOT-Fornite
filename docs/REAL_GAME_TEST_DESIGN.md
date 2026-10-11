# Real game test: Layer 4, the real game, headless, in the container

Design only (2026-10-11). Nothing here is implemented. Layers 0-3 are in `docs/TEST_TOOL_DESIGN.md`; this adds the layer
that runs the actual desktop build of the game, with the mod, under Xvfb, drives it with a script, asserts on live game
state and takes screenshots an agent can look at.

Goal (the user's words): "as comprehensive as possible at testing real features added and tested as they would in game
by you. Even if I have to share the game files with you."

One command: `python scripts/feature_check.py --game [scenario...]`. One row per scenario: `GAME PASS` / `GAME FAIL`.
Artifacts in `build-check/game/`. Never in git, never in a PR or published artifact (screenshots are ROM-derived imagery).

## What was verified in this container (2026-10-11)

| Question | Answer |
|---|---|
| Software GL under Xvfb | `xvfb-run -a -s "-screen 0 1280x720x24"` + SDL2 window: **GL 4.5 Core, Mesa 25.2.8, llvmpipe (LLVM 20.1.2, 256 bits), GLSL 4.50**, max texture 16384, SDL video driver `x11`. `glxinfo` is not installed; the probe was a 20-line SDL2 program (keep it as `scripts/game/glprobe.c`, see step 1). LUS needs GL 3.x core + GLEW: fine. |
| Desktop Linux target | `/tmp/royale-fork-build` is configured (Ninja, Release, gcc 13, `ENABLE_OPENGL`). Target `soh` → `soh/soh.elf`, links `libultraship`, `ZAPDLib`, `SDL2`, `royale_net` + `enet` (patch 0002 + `cmake/royale.cmake`). `RoyaleMod.cpp` is in the build graph. 1454 objects (972 for `soh`), 1221 C/C++ files, 187 MB source. No ccache. `RoyaleMod.cpp.o` alone is 4 min 5 s (measured, layer 2a). Whole build estimate: **60-100 min at `-j4`**, 3-5 GB disk; 4 GB+ RAM spikes on the decomp's big files and `RoyaleMod.cpp` (7 MB of generated headers). Not run yet (spike step). |
| Asset extraction on Linux | `ZAPD.out` (target `ZAPD`) + `OTRExporter/extract_assets.py`. CMake target `ExtractAssets` runs it with cwd `OTRExporter/`, ROM from `*.z64` glob in that dir, writes `oot.otr` (or `oot-mq.otr`) there, then `copy-existing-otrs.cmake` copies it to `<build>/soh/` **and the fork root** (inside the repo; gitignored by `*.otr`). The script also takes an explicit ROM path (`extract_assets.py [...] /abs/rom.z64`), which is what we use. `soh.otr` (the port's own assets, no ROM) is target `GenerateSohOtr`; the game refuses to start if its version differs from the build (`CheckSoHOTRVersion`). |
| Accepted ROMs | `soh/soh/Extractor/Extract.cpp` `verMap` (15 CRCs): PAL GC, PAL MQ, PAL Debug 1/2, PAL MQ Debug, PAL N64 1.0/1.1, NTSC GC US/JP/JP CE, NTSC MQ US/JP, NTSC N64 1.0/1.1/1.2. `docs/supportedHashes.json` in the fork lists 19 SHA-1s. `.z64`, `.n64`, `.v64` accepted; MQ ROMs give `oot-mq.otr`. The Android fork extracts on the device with the same ZAPDLib (patch 0014 loading screen); the desktop build can also extract at runtime but through GUI dialogs (`pfd`, yes/no boxes), so we pre-extract. |
| App directory on Linux | `Context::GetAppDirectoryPath`: `$SHIP_HOME` if set, else (`NON_PORTABLE` is off in this build) the cwd `.`. Config is `shipofharkinian.json` there; `oot.otr` is looked up there (`LocateFileAcrossAppDirs`); `soh.otr` next to the executable (`GetPathRelativeToAppBundle`). The mod's files (`royale-crash.txt`, `royale-trace.txt`, `royale-selftest.txt`, `Save/battle-royale.sav`) are `GetPathRelativeToAppDirectory` = the same dir. |
| Boot flow without a file select | `EngineBridge.inc` `Royale_NativeMenuBoot`: start screen (`StartScreen.inc`, `BTN_START` or `BTN_A` on `state->input[0]`) → `WarPrepareSave()` (`war_table/Save.inc`: makes `Save/battle-royale.sav`, adult Link, 7 hearts, Master Sword, `QUEST_BATTLEROYALE`) → `FileChoose_LoadGame` → Temple of Time waiting room with the native War Table (`gWar.page`, `WarPage::Play`). No vanilla file select is shown. |
| Input injection point | `game.c`: `GameState_ReqPadData` (graph.c:296) fills `gameState->input[0]`, then `GameState_Update` runs `GameInteractor_ExecuteOnGameStateMainStart()` (game.c:268) **before** `gameState->main` (game.c:270). `OnGameFrameUpdate` runs at the end of the frame, after draw. So a hook on `OnGameStateMainStart` that overwrites `gGameState->input[0]` (`cur`, `press`, `rel`, `stick_x/y`) is read by the start screen, the War Table (`war_table/Input.inc` already zeroes `in->cur/press/rel`, line 191), and every game input path (`SquadInput`, `BusInput`, Player). The mod already registers `OnGameStateMainStart` twice. No libultraship change needed. |
| Screenshot from inside | `GfxRenderingAPI::read_framebuffer_to_cpu(fb_id, w, h, rgba16*)` (`gfx_rendering_api.h:64`, OpenGL impl `gfx_opengl_read_framebuffer_to_cpu`), reachable through `gfx_get_current_rendering_api()`. Fallback from outside: `convert x:root -display :N out.png` (ImageMagick `convert` is installed; `xwd` is not). |
| Crash capture | The mod's `royale-crash.txt` handler (`MusicDiagnostics.inc:79`) is `#ifdef __ANDROID__`. libultraship has its own Linux `sigaction` handler (`libultraship/src/debug/CrashHandler.cpp:413`, SIGILL/SIGSEGV...) that prints a backtrace. `gdb` is installed. |
| Wall clock in the mod | 139 uses of `SDL_GetTicks` / `ImGui::GetTime` / `SecondsSinceLaunch` / `std::chrono` in `features/` + `war_table/`. No fixed frame step is possible without touching all of them. The driver works in real time with waits and tolerances. |
| Tools present | `python3` with Pillow 12.3 and numpy 2.5 (contact sheets, diffs), `ffmpeg`, `convert`, `gdb`, `Xvfb`, `xvfb-run`. Missing: `glxinfo`, `xwd`, `xdotool` (none needed). |
| Resources | 4 cores, 15 GB RAM, 28 GB free disk, ephemeral. |

## 1. Game data in, safely

Rules, in order of importance:

1. ROM and anything made from it (`oot.otr`, `oot-mq.otr`, screenshots, `royale-trace.txt` with asset names is fine) stay **outside the repo** and are never committed, never attached to a PR, issue, Artifact page or a published log.
2. Nothing ever prints a byte of the ROM, the OTR, or the private URL. Scripts print only: version name, byte size, "sha256 matches" / "mismatch".
3. The agent never opens these files with `Read`, `cat`, `xxd`, `strings`, `hexdump`. The deny rules below enforce that for `Read`.

### Where it lives

```
$ROYALE_GAME_HOME            default /tmp/royale-game        (outside the repo; ephemeral)
  data/                      the user's game data, read-only for everything else
    rom.z64                  the ROM as uploaded (any name; the script renames)     OR
    oot.otr                  a ready-made OTR (from the phone's TDawgsBattleRoyale/ folder or a PC SoH install of the same build)
    SHA256SUMS               optional; the user's checksums (written by the user or by the fetch script, see below)
  home/                      the game's app directory: SHIP_HOME points here
    oot.otr                  copied or extracted
    shipofharkinian.json     written by the launcher before each run (window size, vsync off, driver on)
    Save/battle-royale.sav   made by the game on first boot
    royale-crash.txt, royale-trace.txt, royale-selftest.txt, royale-game-*.json   per run; copied into build-check/game/<scenario>/
  build/                     the fork build (or reuse /tmp/royale-fork-build via ROYALE_ENGINE_BUILD)
```

### Three ways in (the user picks one)

**A. Upload into the session.** The user attaches the ROM (or `oot.otr`) to a message. The agent moves it with one command,
never reading it: `python scripts/game/game_data.py add <uploaded path>`. The script moves (not copies) the file to
`$ROYALE_GAME_HOME/data/`, identifies it (ROM: CRC at bytes 16-19 against the 15 known versions; OTR: it is a zip, check the
first entry list has `version` and a few known paths), prints `rom: NTSC N64 1.0, 33.6 MB, sha256 <first 8 hex>` and nothing
else. Uploads land in the session's upload directory, which is outside the repo; the move leaves nothing behind. Downside:
it is gone when the session ends. Every new session needs the upload again (one attachment, one command).

**B. Private URL in an environment secret (recommended for repeat use).** The user puts the file at a private URL they
control (a private GitHub release asset with a fine-grained token, a presigned S3/R2/Dropbox link, a Google Drive link with
a token). Two environment secrets in the Claude Code environment settings (not in the repo):

- `ROYALE_GAME_DATA_URL`: the URL. For GitHub release assets, `https://api.github.com/repos/<owner>/<private-repo>/releases/assets/<id>` with `ROYALE_GAME_DATA_TOKEN` as the bearer token and `Accept: application/octet-stream`.
- `ROYALE_GAME_DATA_SHA256`: the expected checksum (the user gets it once with `sha256sum` / `certutil -hashfile <file> SHA256` / Finder's `shasum -a 256`).
- optional `ROYALE_GAME_DATA_TOKEN`.

`scripts/game/game_data.sh fetch` (called by the environment's setup script, or by `feature_check.py --game` when the data
directory is empty): `curl -sS -L -o "$ROYALE_GAME_HOME/data/download.bin"` with the token in a header read from the env,
`set +x`, stderr to `/dev/null` except the HTTP status, then `sha256sum -c` against the secret, then identify and rename as
in A. On mismatch it deletes the download and prints `game data: sha256 mismatch` (not the hash it got). The URL and token
never appear in a log: the script prints `game data: fetched 33.6 MB in 4 s`.

The Claude Code environment's setup script (Settings > Environments > setup script) may call
`scripts/game/game_data.sh fetch` so every session starts with the data present. It must not `set -x`.

**C. A ready-made `oot.otr` instead of a ROM.** Same as A or B. Saves the ZAPD step (minutes) but it has to be the OTR made
by this fork's extractor and `soh/assets/xml` (the phone's `TDawgsBattleRoyale/oot.otr` from the current build is exactly
that; the game checks `oot.otr` against the packed extractor fingerprint on Android, and the desktop build will reject an
OTR made by a different SoH version with "Game files out of date"). A ROM is the safer input: the container rebuilds the OTR
with the fork's own ZAPD whenever the extractor files or xml change.

### Repo rules (same PR as the first script)

- `.gitignore` already has `*.z64 *.n64 *.v64 *.otr *.o2r` and `/build-check/`. Add `/build-check/game/` explicitly (it holds PNGs) and `/game-data/` in case someone puts data in the tree by mistake.
- `.rgignore`: `/build-*/` already covers `build-check/game/`. Add `/game-data/`.
- `.claude/settings.json` deny: `Read(//tmp/royale-game/data/**)`, `Read(./game-data/**)`, `Read(**/*.z64)`, `Read(**/*.n64)`, `Read(**/*.v64)`, `Read(**/*.otr)`, and `Bash(xxd:*)`/`Bash(hexdump:*)` are not needed; the rule above (never inspect) goes in `CLAUDE.md` as one line.
- `build-check/game/**/*.png` are read by the agent on purpose (that is the point), so they are not denied. They are never committed, uploaded, or attached anywhere. `github_brief.py` and the CI never see them (layer 4 does not run in CI).

### Caching between ephemeral sessions: what is allowed

| Thing | ROM-derived? | Where it may be cached |
|---|---|---|
| ROM, `oot.otr`, `oot-mq.otr` | yes | only the user's private URL (B). Never a GitHub Actions cache/artifact, never a Claude Artifact, never git. |
| `soh.elf`, `ZAPD.out`, `soh.otr`, `libultraship.a` | no (built from the fork + mod) | a tarball at a user URL, or a GitHub Actions artifact of a `game-build-linux.yml` job (build-only, no ROM; 60-100 min on a GitHub runner too). Fetched by `scripts/game/engine_fetch.sh` keyed by `git rev-parse HEAD:third_party/Shipwright-Android` + `sha256 of patches/**` + `sha256 of mod/Royale/**`. Saves the hour. |
| Screenshots, `result.json`, traces | screenshots yes; JSON no | screenshots: nowhere. `result.json` and the text rows may be pasted into a handoff. |
| Golden images (section 5) | yes | only at the user's private URL, as `goldens.tar` fetched like the data. Not in git. |

## 2. Building and launching headless

### Build (`scripts/game/engine_build.sh`, idempotent)

```
ROYALE_ENGINE_BUILD=${ROYALE_ENGINE_BUILD:-/tmp/royale-fork-build}     # reuse the configured tree if present
scripts/engine_setup.sh                                                # submodules, apt, patches, configure (exists; 0.3 s when done)
scripts/link_mod.sh                                                    # copies mod/Royale into soh/soh/Enhancements/Royale (exists)
cmake -S third_party/Shipwright-Android -B "$ROYALE_ENGINE_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_REMOTE_CONTROL=OFF
ninja -C "$ROYALE_ENGINE_BUILD" -j3 ZAPD soh                           # -j3: the decomp's big files and RoyaleMod.cpp spike past 3 GB each
ninja -C "$ROYALE_ENGINE_BUILD" GenerateSohOtr                         # soh.otr, no ROM
```

Prints `engine: soh.elf <size> built in <min>` or the first error. `-j3` is a guess from the 4 min / 7 MB-header
`RoyaleMod.cpp` compile; the spike measures peak RSS with `/usr/bin/time -v` and the doc gets the number. Optional later:
`-DCMAKE_C_COMPILER_LAUNCHER=ccache` after `apt install ccache` (not installed now).

### Assets (`scripts/game/game_data.py extract`)

```
cd third_party/Shipwright-Android/OTRExporter
python3 extract_assets.py -z "$ROYALE_ENGINE_BUILD/ZAPD/ZAPD.out" --non-interactive --xml-root ../soh/assets/xml \
    --custom-otr-file soh.otr --custom-assets-path ../soh/assets/custom --port-ver 9.0.2 "$ROYALE_GAME_HOME/data/rom.z64"
mv oot.otr "$ROYALE_GAME_HOME/home/oot.otr"      # out of the repo tree at once (it was gitignored anyway)
rm -f ../oot.otr ../soh.otr                       # copy-existing-otrs may have placed copies at the fork root
cp "$ROYALE_ENGINE_BUILD/soh/soh.otr" ... (already next to soh.elf)
```

ZAPD output goes to a log file under `build-check/game/extract.log`; the console gets `extract: oot.otr <MB> in <s>`. Run
once per session unless the OTR's fingerprint (sha256 of `soh/assets/xml/**` + `soh/assets/extractor/**` + ZAPD version) changed.
Time unknown; the Android loading screen suggests one to three minutes on a phone, so under a minute here.

### Launch (`scripts/game/run_game.py`, used by every scenario)

```
SHIP_HOME=$ROYALE_GAME_HOME/home
SDL_AUDIODRIVER=dummy  SDL_VIDEODRIVER=x11  LIBGL_ALWAYS_SOFTWARE=1  MESA_GL_VERSION_OVERRIDE unset
ROYALE_TEST_SCRIPT=build-check/game/<scenario>/scenario.json   ROYALE_TEST_OUT=build-check/game/<scenario>/
timeout -k 15 <hard seconds> xvfb-run -a -s "-screen 0 960x540x24" "$ROYALE_ENGINE_BUILD/soh/soh.elf"
```

Before launch the runner writes `shipofharkinian.json` in `SHIP_HOME` with: `Window.Width 960`, `Window.Height 540`,
`Window.Fullscreen.Enabled false`, vsync off (the CVar `gfx_sdl2` reads; the spike names it), `CVars.gSettings.Royale.*`
the scenario asks for (debug switches, port), `gSettings.Royale.Debug.SelfTest 0`. The runner never touches the user's
real settings: `SHIP_HOME` is ours.

- `xvfb-run -a` picks a free display; `-s "-screen 0 960x540x24"`. 960x540 keeps llvmpipe fast; a scenario may ask for 1280x720 for a closer look (`"window": [1280, 720]`).
- Hard timeout per scenario: `scenario.timeout_sec` (default 300) + 30 s boot margin, enforced by `timeout -k 15`. Exit codes: 0 all asserts pass, 1 an assert failed, 2 the driver's own deadline hit (the JSON says which step), 124/137 killed by `timeout`, else crash.
- Crash capture, three layers: (1) the mod's handler, after changing `MusicDiagnostics.inc:79` from `#ifdef __ANDROID__` to `#if defined(__ANDROID__) || defined(__linux__)` so `royale-crash.txt` is written on Linux too (one-line change, the handler body already compiles for both); (2) libultraship's `CrashHandler.cpp` backtrace on stderr, captured to `stderr.log`; (3) `--gdb` runs `gdb -batch -ex run -ex bt -ex "info registers" --args soh.elf` for a symbolised trace when (1) and (2) are not enough (Release has no `-g`; a `RelWithDebInfo` build is a 2x disk option for later). After every run the runner copies `royale-crash.txt`, `royale-trace.txt`, `royale-selftest.txt` (if any) into `build-check/game/<scenario>/`.
- Deterministic-ish: not deterministic. The mod uses wall-clock time in 139 places, llvmpipe frame time varies 30-120 ms. The driver counts game frames (`gameState->frames`) for "wait N frames" and wall seconds for "wait until", with a per-step deadline. Sim side is seeded: the host passes `seed` to `GameServer` in `RoyaleSession::Host`; the scenario sets `"seed"` and the driver sets the CVar/field before hosting so bots and loot spots repeat run to run.
- Audio: `SDL_AUDIODRIVER=dummy`; the mod's audio mix hook (patch 0016) still runs, Peter's voice decode still counts. Nothing is heard; asserts on audio are counters only.

## 3. The scripted test driver: `mod/Royale/features/TestDriver.inc`

Included **after** `SelfTest.inc` (last in `RoyaleMod.cpp`), same anonymous namespace, so it can read every feature's state
and reuse `SelfTest.inc`'s step functions. `InstallTestDriver()` declared in `Core.inc` next to `InstallSelfTest`, called
from `RegisterRoyaleMod` (`SceneHooks.inc:475`). Under 600 lines; the JSON reader is `nlohmann::json` (already linked, used by `Save.inc`).

**Inert in release builds.** `InstallTestDriver()` returns at once unless `getenv("ROYALE_TEST_SCRIPT")` is set
(`#ifndef __ANDROID__`; on Android it is compiled out entirely, `#ifdef __ANDROID__ void InstallTestDriver() {} #else ...`).
No CVar, no menu row: the phone build never has it on. `check_mod_layout.py` asserts the env guard is the first line of the
function.

### Hooks it registers

| Hook | What it does |
|---|---|
| `OnGameStateMainStart` | the input step: builds this frame's `OSContPad`-shaped `Input` from the current step (held buttons, stick) and writes `gGameState->input[0].cur`; derives `press` = cur & ~prev, `rel` = prev & ~cur from its own previous frame. Only while a `press`/`hold` step is active; otherwise it zeroes the pad (the Xvfb window has no real controller; SDL keyboard state from `SDL_GetKeyboardState` is still read by `EngineBridge.inc`, which is fine: no keys are down). |
| `OnGameFrameUpdate` | the step machine: one step at a time, like `SelfTest.inc`; a step returns Pass/Fail/Wait with a detail; `Wait` is retried next frame until the step's deadline. Screenshots are requested here and taken at the next `OnGameFrameUpdate` (so the framebuffer holds a complete frame). Writes the running file every step. |
| `OnSceneInit` | records `sceneNum` and the frame, for `assert.scene` and for the trace. |

### Scenario file (`scripts/game/scenarios/<name>.json`)

```json
{
  "name": "solo_drop",
  "feature": "battle_bus",
  "timeout_sec": 240,
  "window": [960, 540],
  "seed": 7,
  "cvars": { "gSettings.Royale.Debug.Skins": 1 },
  "steps": [
    { "wait_until": "start_screen", "deadline": 60 },
    { "screenshot": "01_start" },
    { "press": "START" },
    { "wait_until": "war_table", "deadline": 30 },
    { "assert": { "war_page": "Play", "scene": "SCENE_TEMPLE_OF_TIME" } },
    { "screenshot": "02_war_table" },
    { "host": { "solo": false, "sandbox": false, "map": 12, "players": 32, "team_mode": "Solo" } },
    { "start_match": true },
    { "wait_until": "match_state=Drop", "deadline": 90 },
    { "assert": { "aboard_bus": true, "hud.alive": 32 } },
    { "screenshot": "03_bus" },
    { "wait_frames": 60 },
    { "hold": { "buttons": ["A"], "frames": 2 } },
    { "wait_until": "not aboard_bus", "deadline": 10 },
    { "wait_until": "gliding", "deadline": 20 },
    { "screenshot": "04_glider" },
    { "wait_until": "on_ground", "deadline": 60 },
    { "assert": { "player.y_below": 2000, "hud.state": "InMatch", "scene": "SCENE_HYRULE_FIELD" } },
    { "screenshot": "05_landed" },
    { "leave": true }
  ]
}
```

### Step vocabulary (every step has an optional `deadline` in seconds, default 10, and `note`)

| Step | Meaning | Implementation |
|---|---|---|
| `wait_frames: N` | N calls of `OnGameFrameUpdate` | counter |
| `wait_sec: S` | wall seconds | `SecondsSinceLaunch()` |
| `wait_until: "<condition>"` | polls a named condition (table below) until true or deadline → Fail | |
| `press: "A"` / `press: ["A","Z"]` | buttons held for 2 frames then released (press edge on frame 1) | the input hook |
| `hold: { buttons, stick: [x, y], frames | until }` | held for N frames or until a condition; stick in -80..80 (`stick_x`, `stick_y` as the game reads them) | input hook |
| `stick: [x, y], frames` | stick only | input hook |
| `teleport: [x, y, z]` / `teleport: "pad:0"` / `teleport: "sandbox:go:1"` | sets `GET_PLAYER(gPlayState)->actor.world.pos` (+ `prevPos`, clears speed), or `SandboxDo(SandboxCmd::Go, n)`, or the first pad from `gSession.Client()->Pads()` | what `SelfTest.inc` step 9 planned |
| `host: {...}` | `gSession.SetPlayerLimit`, `SetTeamMode`, `SelectMap`, then `gSession.Host(port, "Driver", &err, solo, sandbox, testMap)` as `WarHost` does; `players` fills the rest with bots in-process (see below) | `RoyaleSession.h:154` |
| `start_match: true` | `gSession.StartMatch()` (or sets `gSoloStartWanted` for solo, exactly as `SelfTest.inc` `StepHost`) | |
| `sandbox: { cmd: "Give", a, b, c }` | `SandboxDo(SandboxCmd::<Kind>, a, b, c)`; kinds: ArenaClear, Encounter, Go, Cart, ClearCarts, Bot, ClearBots, FreezeBots, Boss, ClearBosses, Give, Heal, Revive, God, StormRuns, StormPhase, Supply, Weather, WeatherFree, Restock, Glide | `Lobby.inc:63-69` |
| `set: { rupees: 999, health: 48, skin: 1, item: "ShockwaveGrenade" }` | host-side forcing through `gSession.SandboxMatch()->Find(selfId)` (sandbox) or the server's `match` (solo host); `skin` = `gSession.SetSkinModel(n)` before `host` | same as the smokes' setup |
| `call: "HireAlly", args: [6]` | a small whitelist of mod functions: `HireAlly(i)`, `StartLinkJumpClip()`, `LaunchFromPad(player)`, `OpenChest(nearest)`, `WarShow(page)`, `AddLightFlash(...)`, `SandboxGlide` | one `if` chain |
| `war: "EXPLORE BATTLEFIELD"` | activates a War Table row by its label (`WarRow` text) instead of navigating with the stick; keeps menu scenarios short and robust. Navigation scenarios use `press` instead. | finds the row in the current page's list |
| `assert: { ... }` | all keys must hold now; on failure the step fails with "key: expected X, got Y" | table below |
| `screenshot: "name"` | next frame, reads the framebuffer, writes `<out>/<NN>_<name>.png` (RGBA16 → RGB8 PNG via `stb_image_write`, which libultraship already vendors) and records it in the result | |
| `selftest: "skins.draw"` | runs one `SelfTest.inc` step function by name (its table is `kSelfChecks[]`) with a fresh `SelfTestCtx`; result and detail are copied | reuse |
| `leave: true` | `gSession.Leave()`, wait `!Joined()` | |
| `quit: true` | implied at the end: writes the result, then `Ship::Context::GetInstance()->GetWindow()->Close()` (clean SDL quit, flushes the trace); the runner kills the process if it is still alive 10 s later | |

**Conditions** (`wait_until` and `assert` boolean keys): `start_screen` (`!gStart.done && gStart.shownAt != 0`),
`war_table` (`gWar.booted && gWar.page == WarPage::Play`), `war_page=<Name>`, `in_game` (`InGame()`), `scene=<SCENE_…>`
(`gPlayState->sceneNum`), `in_field` (`InField() && gFortniteBuilt`), `joined`, `match_state=<Lobby|Countdown|Drop|InMatch|Ending>`
(`gSession.Hud().state`), `aboard_bus` (`gSession.Client()->Aboard()`), `gliding` (the glider flag in `Terrain.inc`;
`gGliderRoll`/`gGliderDiving` live in `GroundWeather.inc`, the on/off flag is named in the spike), `on_ground`
(`player->actor.bgCheckFlags & BGCHECKFLAG_GROUND`), `dead` (`gDeath.active`), `end_screen` (`gEnd.open`),
`riding_cart`, `knocked`, `reviving`, `ally_count>=1`, `not <cond>`.

**Assert keys** (numbers accept `{ "min": .., "max": .. }` or exact): `hud.state`, `hud.alive`, `hud.selfHealth`,
`hud.maxHealth`, `hud.rupees`, `hud.stormPhase`, `hud.stormDamagePerSecond`, `hud.mapId`, `hud.teamMode`, `hud.botSlots`,
`hud.playerLimit`, `hud.isHost`, `player.pos` (± tolerance), `player.y_below`/`y_above`, `player.health` (`gSaveContext.health`),
`player.item_b` (`gSaveContext.equips.buttonItems[0]`), `inventory.has` (`royale::Inventory` from `gSession.Hud()`),
`actors.total`, `actors.category[ACTORCAT_ENEMY]`, `actors.id[ACTOR_EN_BOX]`, `loot.actors` (`gLoot` size), `chests.opened_by_me`
(`gMyChestOpens.size()`), `skins.limbs_drawn` (`gSkinStats.drawn`), `link_clip.jump_w` (`link_clip::gJumpW`), `water.surface_at:[x,z]`
(`WaterSurfaceAt` finite, within range), `gfx.pool_overflow` (`gGfxPool.overflowLast`), `allies.kinds` (`gAllies`), `bus.model_valid`
(`gBusGpu.valid`), `cart.count`, `storm.in_storm`, `frame.avg_ms_below` (over the last 60 frames), `crumb_contains` (last 32 breadcrumbs),
`trace_contains`. Anything else a feature needs is one line in `TestDriver.inc`'s assert table and a row in this doc.

### Result file (`<out>/result.json`, written after every step so a crash leaves a partial one)

```json
{ "scenario": "solo_drop", "build": "0.312", "protocol": 44, "started": "2026-10-11T22:41:03Z", "frames": 5120, "seconds": 171.4,
  "avg_frame_ms": 41.2, "max_frame_ms": 812,
  "steps": [
    { "i": 0, "step": "wait_until start_screen", "result": "PASS", "frames": 48, "seconds": 2.1, "detail": "" },
    { "i": 11, "step": "assert", "result": "FAIL", "detail": "hud.alive: expected 32, got 31" } ],
  "screenshots": ["01_start.png", "02_war_table.png"],
  "result": "FAIL", "failed_step": 11,
  "state": "<the BuildCrashState block at the end>", "crumbs": ["..."] }
```

Plus `royale-game-running.txt` (`running <scenario> step <i> <name>`) in `SHIP_HOME`, like the self-test's running file.

### Hosting a match with bots in-process

`gSession.Host(...)` creates the `GameServer` inside the game process (`RoyaleSession.h:161`) on an ENet transport bound
to `ui.port` on localhost; the local `GameClient` connects to it over the loopback UDP socket. `SetPlayerLimit(32)` then
`StartMatch()` fills every empty slot with bots (`botSlots`, the same `BotController` the smokes use). So "a match with 31
bots" is one process, no second client, no network outside `127.0.0.1`. `soloTest=true` gives no bots and no timer (the
self-test's mode); `sandbox=true` is the Sandbox map with `SandboxDo` commands and `SandboxMatch()` access to force state
(rupees, items, damage, knock a bot teammate). Squads with a bot: `SetTeamMode(Duos)`, `players: 2`, start: the one bot
is the teammate (the server's team fill), then `set: { teammate_health: 0 }` through `SandboxMatch()->Damage` knocks it, and
the driver holds A next to it until `TeamRevived` appears in the client's events (the driver drains `gSession.Client()`
events into its log like `feature_smoke.cpp`'s `Rig`).

## 4. Scenario library (`scripts/game/scenarios/*.json`)

One file per scenario; the registry names them. Screenshot names say what the agent should see. Deadlines generous
(llvmpipe). Every scenario starts from a fresh `SHIP_HOME/Save` (the runner deletes `Save/` unless `"keep_save": true`).

| Scenario | Feature | Steps | Asserts | Screenshots (what to look for) |
|---|---|---|---|---|
| `boot_title` | start_screen | wait `start_screen`; wait 6 s (intro); press START; wait `war_table` | `scene=SCENE_TEMPLE_OF_TIME`, `war_page=Play`, `gStart.done`, no crash | `01_logo` (island, logo flames, "Press Start"), `02_war_table` (table, rows readable) |
| `war_table_nav` | war_table, menus | from the table: stick down x3, A on "SETTINGS", press B, `war: "PRACTICE TOOLS"` | `war_page` sequence recorded equals `[Play, Settings, Play, Practice]` | one per page |
| `solo_drop` | battle_bus, ground_weather | section 3 example: host 32 (31 bots) on Moonfall, start, wait Drop, A to jump, glide, land | `aboard_bus` then not; `gliding`; `on_ground`; `hud.state=InMatch`; `hud.alive>=25` after 60 s; `bus.model_valid` | `03_bus` (bus model, seats, horn HUD), `04_glider` (glider over the island, roll), `05_landed` |
| `glider_sandbox` | ground_weather | host sandbox; `sandbox: Glide`; hold stick left 60 frames; Z held 30 frames | `gliding`; `gGliderRoll < -0.2` while left; `gGliderDiving` while Z | `01_glide`, `02_bank_left`, `03_dive` |
| `chest_open` | world_objects | host sandbox; `teleport` to the nearest `gLoot` chest + 60 units; face it (stick); press A; wait `chests.opened_by_me>=1` | `LootTaken` event, `hud` inventory grew, `actors.id[ACTOR_EN_BOX]` unchanged (our chest, not the game's) | `01_chest_closed`, `02_chest_open` (lid up, item effect) |
| `shockwave` | projectiles, equipment | host sandbox; `set: item ShockwaveGrenade`; `sandbox: Bot` at 150 units; `sandbox: FreezeBots`; press the use button (the stack's button per `Hud.inc`); wait `Strike` | `Strike` event once, fuse within `kShockwaveFuse` ± 0.3 s; the bot's position moved ≥ 50 units (shove, since 837f96c it does no damage) | `01_thrown` (ball in flight, bounce), `02_blast` |
| `squads_revive` | squads | host Duos, `players: 2`, Sandbox; start; `set: teammate_health 0` through `SandboxMatch()`; `teleport` to the bot; hold A 4 s | `Knocked` then `TeamRevived` events; `hud.alive` back to 2; squad HUD counter in state | `01_knocked` (down marker, crawl pose), `02_reviving` (progress ring), `03_revived` |
| `deku_pad` | squads | host sandbox; `teleport: "pad:0"`; wait 1 frame | `PadUnderfoot()`; after `call LaunchFromPad`: `player.y_above` start + 200 within 1 s; `gliding` within 2 s | `01_on_pad`, `02_launched` |
| `lon_lon_buggy` | vehicles | host sandbox; `sandbox: Cart`; `teleport` next to it; press A; hold stick up 90 frames | `riding_cart`; cart moved ≥ 300 units; `cart.count=1`; no `cart` crash fx counter rise | `01_buggy` (model, wheels), `02_driving` |
| `hire_allies` | allies, companions | host sandbox; `set: rupees 999`; `sandbox: Go 1` (armory); `call HireAlly 5` (Peter), wait `AllyChanged`; `call HireAlly 6` (Chief) | `allies.kinds = [Peter, Chief]`, both actors non-null; one `AllyAction` within 30 s with a frozen bot nearby; Peter's voice decode-error count 0 | `01_peter`, `02_chief` (models, rifles on Chief) |
| `skin_link_hd` | skins, link_anims | `set: skin 1`; host sandbox; `selftest: skins.draw`; `call StartLinkJumpClip`; hold stick 60 frames (run); press A (jump); B (sword) | `skins.limbs_drawn>=1`, `limbsSeen>=18`; `link_clip.jump_w>0`; no `gfx.pool_overflow` | `01_idle`, `02_run`, `03_jump`, `04_sword` (the HD body follows the pose; no detached limbs) |
| `water` | water | host sandbox; `teleport` to the pool's authored coordinates (from `shared/sandbox_*.h`) + 300 above; wait `on_ground` or swimming | `water.surface_at` within 50 of authored; `gWaterDetail>=1`; player swims (`stateFlags1 & PLAYER_STATE1_IN_WATER`) | `01_water_far` (sheets, reflections), `02_swimming` |
| `terrain_moonfall_walk` | terrain, maps | host solo on 12 (`soloTest=true, map 12`); start; for each of 6 authored waypoints (`tools/maps/moonfall` sites): `teleport` 50 above it, wait `on_ground`, hold stick up 120 frames | after each walk `player.y_above -3000` (never fell through), `|pos| < circle radius`, `in_field`; actor counts stable; no `royale-crash.txt` | `wp_1..6` (ground under Link, no see-through) |
| `terrain_skyloft_walk` | terrain, maps | same on 13, waypoints from `tools/maps/skyloft` | same | same |
| `storm_damage` | atmosphere, hud | host solo sandbox; `sandbox: StormRuns 1`; `sandbox: StormPhase 3`; `teleport` outside the circle | `hud.stormDamagePerSecond>0`; `hud.selfHealth` fell by ≥ 1 within 5 s; `storm.in_storm` | `01_storm_wall` (wall, fog tint), `02_hud_damage` (HUD bar) |
| `end_screen` | end_match | host 32 on Moonfall with `sandbox: God` off; `set: health 1`; wait `dead`; press A (Spectate); wait `end_screen` with a 540 s deadline (bots finish the match; smokes measured 314-441 sim s) | `gDeath.active`, `gDeath.place>0`; `gEnd.open`; results table rows = players; `EvResults` one winner | `01_death_card`, `02_spectating`, `03_end_screen` (table, crown on the winner) |
| `self_test` | self_test | `selftest` for every `kSelfChecks` entry except `session.*` (the driver hosts itself) | each step PASS or SKIP (none FAIL) | none |

Each scenario is also a layer-3 story: the same JSON could later drive the phone (a future `SelfTest.inc` reading it from
`TDawgsBattleRoyale/scenario.json`), since the step functions are the same code. Not in scope now.

## 5. Visual checks

What the agent can judge from PNGs, and how.

- **Contact sheet.** `scripts/game/contact_sheet.py build-check/game/<scenario>/` → `contact.png`: every screenshot at 480x270 in a grid with its name and step index burned in (Pillow). One `Read` of one image per scenario instead of ten. The agent reads `contact.png` first, then any single PNG at full size when something looks wrong.
- **What to look for** is written in the scenario's `"look_for"` strings (per screenshot) and printed under the row on FAIL or with `--show-look-for`. Example: `"04_glider": "Link under a glider above the island; horizon visible; no black screen; HUD minimap bottom-left"`.
- **Cheap automatic image checks** (`"image_checks"` per screenshot, run by the Python side, no goldens needed): `not_black` (mean luma > 8), `not_single_color` (stddev > 6), `region_mean` (`[x0,y0,x1,y1]`, expected RGB ± tol: "the sky is blue-ish at the top", "the HUD bar region is red when hit"), `differs_from: "03_bus"` (mean abs diff > 4: the frame changed). These catch the common failures: black frame, missing render, stuck frame.
- **Golden-image diffs, optional.** `--goldens DIR` (fetched from the user's private URL, never in git): per screenshot, Pillow `ImageChops.difference` → mean abs diff and a per-pixel count over 24; thresholds in the scenario (`"golden_tol": {"mean": 6, "pixels": 0.03}`). Goldens are recorded with `--record-goldens` on a run the user looked at and approved. Different llvmpipe/LLVM versions shift dithering and AA slightly: keep tolerances loose, compare at 480x270.
- **What cannot be judged.** Motion smoothness, audio, feel of input latency, the Odin 2's real frame time and GPU driver behaviour (Adreno vs llvmpipe: shader precision, mipmap selection), touch input, Android file permissions and the updater. Those stay `manual`/`device` rows. A screenshot proves "it drew and looks right at 960x540 on Mesa"; the phone proves the rest.

## 6. CLI integration

```
python scripts/feature_check.py --game [scenario...] [--game-only] [--goldens DIR] [--record-goldens] [--gdb] [--keep-home]
```

- `--game` adds layer 4 to the run (default layers 0-2b still run; `--game-only` = just layer 4). No scenarios named = every scenario named in the registry rows selected (all, or `--changed`'s features).
- Registry: a new field per feature, `"game": ["solo_drop", "glider_sandbox"]`; `check_mod_layout.py` asserts every `scripts/game/scenarios/*.json` is named by exactly one row and each `"feature"` inside matches. `--list` gets a `game` column.
- Preconditions, each a SKIP row with a one-line reason, never a FAIL: `soh.elf` missing (`run scripts/game/engine_build.sh, ~1 h`), game data missing (`python scripts/game/game_data.py add <file> or set ROYALE_GAME_DATA_URL`), `oot.otr` missing (`extract`), Xvfb missing.
- Rows (same table as today; layer `4`, check `game.<scenario>`, result `GAME PASS` / `GAME FAIL` / `GAME CRASH` / `GAME TIMEOUT` / `SKIP`):

```
feature        layer  check                     result      time    note
battle_bus     4      game.solo_drop            GAME PASS   171s    5 shots, avg 41 ms/frame
squads         4      game.squads_revive        GAME FAIL   64s     step 7 assert hud.alive: expected 2, got 1; see build-check/game/squads_revive/contact.png
terrain        4      game.terrain_moonfall_walk GAME CRASH 38s     royale-crash.txt: SIGSEGV in BgCheck_… (step 9 teleport wp_4)
```

- On FAIL/CRASH the first 8 lines of `result.json`'s failing step and the crash header follow the table, indented, as layer 0-2 do. The summary line counts `game` rows separately: `... 2 game passed, 1 game failed ...`. Exit 1 on any FAIL/CRASH/TIMEOUT.
- Artifacts: `build-check/game/<scenario>/{scenario.json, result.json, NN_name.png, contact.png, stdout.log, stderr.log, royale-crash.txt, royale-trace.txt, royale-selftest.txt, shipofharkinian.json}`. `build-check/game/last.json` = the rows. Gitignored (`/build-check/`), `.rgignore` (`/build-*/`). The CLI prints paths, never image contents.
- Runs scenarios **serially** (llvmpipe uses all 4 cores; two games at once halve the frame rate and double flakiness). A failed scenario is rerun once; passing then is `GAME FLAKY` with the first failure's detail in the note.
- `--json` includes the rows; the handoff pastes the summary line and names the contact sheet paths. It never attaches the PNGs.

## 7. Feasibility risks and fallbacks

| Risk | Likelihood | Check | Fallback |
|---|---|---|---|
| Desktop `soh` target does not link with the mod (Android-only symbols: `RoyalePeter.h` device clip loader, JNI calls in `Updater.inc`/`MusicDiagnostics.inc`, `Royale_ShowQuestLabel`) | medium | the spike build; `nm -u` on `RoyaleMod.cpp.o` already passes in layer 2a, so missing symbols will be in the engine side or link order | `#ifdef __ANDROID__` the JNI bits (the mod already has 8 such guards); the "Windows PC path" in `BUILDING.md` says the PC build existed once |
| Android-assuming patches break the Linux build or runtime (0007 data folder, 0014 loading screen, 0017 exit reason, 0018 updater, 0021 signing, 0028a touch, 0030 keep files) | medium | they applied and configured on Linux already (layer 2a); runtime paths are guarded by `__ANDROID__` in Java/C++ | if one patch breaks Linux at runtime, guard its C++ with `#ifdef __ANDROID__` in the patch (regenerate, letter suffix) |
| llvmpipe too slow (OoT at 960x540 with water HQ, shadows, 32 bots) | high: expect 15-30 fps, worse during scene load | spike measures `avg_frame_ms` on the title and in the field | 640x360 window; scenario `cvars` turn off Water HQ/shadows for logic scenarios and on for their own scenarios; deadlines in frames not seconds where it matters; `--game` never runs beside a build |
| GLSL shader features unsupported on Mesa (the fork's embedded shaders, lus patch 0008 surface maps, 0006 relief) | low: GL 4.5/GLSL 4.50 is above anything GLES 3 needs; 3-component formats differ | spike screenshot of the title + first field frame; shader compile errors land in stderr | `MESA_GL_VERSION_OVERRIDE`/`MESA_GLSL_VERSION_OVERRIDE` if the fork requests a specific version; a scenario `image_check not_black` catches a silent black frame |
| Engine opens dialogs on desktop (ROM not found, OTR version, "SoH is running in a temp folder", first-run setup window) | medium | the spike log | pre-extract (no ROM dialog); `GenerateSohOtr` with the same version (no version dialog); run from `SHIP_HOME` not `/tmp/xxxx` temp-looking paths (the check looks for the system temp dir); CVar to hide the menu bar if ImGui steals input |
| ImGui/menu bar captures the pad (LUS `gOpenMenuBar`, controller nav) | low | the spike: the start screen reacts to the injected START | set `CVar gOpenMenuBar 0`, `gControlNav 0` in the written config |
| Timing flakiness (wall-clock in 139 places; bots race) | high for strict asserts | run each scenario twice in the spike; compare `result.json` | asserts use ranges and events, not exact frames; the seed fixes the sim; FLAKY rerun once; thresholds for frame time are per-layer (`frame.time`'s 80 ms is a phone number; layer 4 asks for `avg < 150 ms`) |
| Game threads: SoH PC runs the game loop on the main thread with the render in the same loop; if the fork moved the game to a thread (`Graph_ThreadEntry` at graph.c:485 is referenced from a thread entry), `read_framebuffer_to_cpu` from `OnGameFrameUpdate` would be off-thread | medium | the spike tries the in-process screenshot; a GL error or garbage means off-thread | screenshot from outside: `convert x:root -display :N png:…` driven by the Python runner when the driver writes `<out>/shot-request` (a file handshake, 1 frame late, good enough) |
| Extraction fails (ZAPD on Linux with the fork's ZAPDTR patches, xml drift) | low: the Android app uses the same code | spike | the user uploads `oot.otr` from the phone (way C) |
| Memory: 4 jobs of gcc on the decomp + `RoyaleMod.cpp` | medium | `/usr/bin/time -v ninja -j3` | `-j2` for the last 50 objects; a `ROYALE_BUILD_JOBS` env |
| Session is ephemeral: 1 h build lost every time | certain | | cache `soh.elf`+`ZAPD.out`+`soh.otr` (not ROM-derived) as a tarball at the user's URL or a GitHub Actions artifact from a new `game-build-linux.yml` (build only, no ROM) and `engine_fetch.sh` it; the OTR is re-extracted (a minute) or re-fetched |
| The data leaks by accident (a `cat`, a `Read`, a screenshot in a PR) | low but the worst outcome | | deny rules (section 1), `CLAUDE.md` line, the CLI never prints contents, screenshots only under `build-check/`, handoffs paste text rows only |
| Royale server port in use / no loopback | low | | `ROYALE_TEST_PORT` per run (random 30000-40000), `Host` on it |

## 8. The spike (go/no-go, before anything in section 9)

Run by the implementer first. Half a day, mostly waiting on the build. Each line is a gate.

1. `cat > scripts/game/glprobe.c` (the probe above) → `xvfb-run -a ./glprobe` prints `GL_VERSION=4.5 ... llvmpipe`. **Go** if GL ≥ 3.3 core. (Already verified here.)
2. `scripts/link_mod.sh && ninja -C /tmp/royale-fork-build -j3 ZAPD soh GenerateSohOtr` under `/usr/bin/time -v`, output to `build-check/game/spike-build.log`. Record wall time, peak RSS, `soh.elf` size in this doc. **Go** if it links. **No-go** if link errors are Android-only symbols that need more than `#ifdef` guards in the mod (estimate first; a day of guards is still go).
3. The user provides a ROM (section 10). `game_data.py add` → `extract` → `oot.otr` in `SHIP_HOME`. **Go** if ZAPD exits 0 and the game accepts the OTR.
4. Launch with no driver: `SHIP_HOME=... SDL_AUDIODRIVER=dummy timeout 120 xvfb-run -a -s "-screen 0 960x540x24" soh.elf` and, 30 s in, `convert x:root -display :<N> build-check/game/spike-title.png` (the runner knows N from `xvfb-run`'s `-a`; the spike uses `Xvfb :99 &` explicitly). Look at it. **Go** if it is the start screen (island, logo). No dialog, no black frame, stderr has no shader compile error.
5. A 40-line `TestDriver.inc` stub: env guard, `OnGameStateMainStart` injecting START for 2 frames at frame 300, `OnGameFrameUpdate` calling `read_framebuffer_to_cpu` at frame 400 and writing a PNG, then `Close()`. **Go** if the War Table appears in the PNG (the injected press worked through the real input path) and the PNG is a correct image (in-process screenshot works). If the PNG is garbage, switch to the outside screenshot and still **go**.
6. Frame time on the title and in the Temple: `gameState->frames` over 30 s. Record. **Go** if ≥ 8 fps (anything slower makes scenarios take over 5 min each; then the fallback is 640x360 and reduced detail CVars, measured again).

Write the six numbers into the "verified" table at the top. Only then start section 9.

## 9. Implementation plan (ordered; each step ships alone; Opus engineer)

1. **Game data scripts + rules.** `scripts/game/game_data.py` (`add`, `fetch`, `extract`, `status`), `scripts/game/game_data.sh fetch` (the curl part, for the setup script), `.gitignore`/`.rgignore`/`settings.json` deny lines, a `CLAUDE.md` rule line ("Game data: `ROYALE_GAME_HOME`, never read, never commit, never attach; `python scripts/game/game_data.py status`"), rows in `docs/CODE_MAP.md`, `docs/README.md`.
   Accept: `status` says `absent` cleanly; `add` of a wrong file says `not an OoT ROM or OTR` and deletes it; `add` of the user's ROM prints version + size only; `fetch` with a bad `ROYALE_GAME_DATA_SHA256` deletes the download and prints `mismatch` without the hash; `grep -r` of the log directory finds no URL or token; `check_mod_layout.py` fails if `*.otr`/`*.z64` appear anywhere under the repo.
2. **Engine build + launch scripts.** `scripts/game/engine_build.sh`, `scripts/game/run_game.py` (writes config, Xvfb, env, timeout, crash copy, exit codes), `scripts/game/engine_fetch.sh` (optional cache), the `MusicDiagnostics.inc:79` Linux guard so `royale-crash.txt` is written on Linux.
   Accept: fresh container → `engine_build.sh` ends with `engine: soh.elf … built in N min`; `run_game.py --no-driver --seconds 60` exits 0 with `spike-title.png` equivalent; `kill -SEGV` of the process yields `royale-crash.txt` in the artifacts dir; a second `engine_build.sh` is a no-op under 10 s.
3. **TestDriver.inc, core steps.** Env guard, JSON load, step machine, input injection (`press`, `hold`, `stick`), `wait_*`, `assert` on `hud.*`, `scene`, `war_page`, `player.*`, `screenshot`, `host`, `start_match`, `leave`, `quit`, result file, running file, breadcrumbs. `FEATURES.md` row, `CODE_MAP.md` line, `feature_checks.json` row `test_driver`.
   Accept: layer 2a compiles (`check_mod_compile.sh`), `game-build.yml` (Android) still builds with the driver compiled out (`nm` of the Android object has no `TestDriver` symbols); `boot_title` and `solo_drop` pass twice in a row; a deliberate break (`press: "B"` instead of START) fails at the right step with the detail; a scenario with an impossible `wait_until` ends as `GAME TIMEOUT` with the step index, under its `timeout_sec` + 30.
4. **CLI integration.** `feature_check.py --game …`, registry `game` field, `check_mod_layout.py` scenario ↔ registry assert, rows/summary/exit code, SKIP preconditions, serial run, one rerun, artifacts layout, `contact_sheet.py`, `image_checks`.
   Accept: `python scripts/feature_check.py --game-only boot_title` prints one `GAME PASS` row and a contact sheet path; without `soh.elf` it prints one SKIP row and exits 0; `--changed` on a branch touching `BattleBus.inc` selects `solo_drop`; a scenario JSON not in the registry fails `check_mod_layout.py`.
5. **Sandbox and forcing steps.** `sandbox`, `set`, `call`, `teleport` (incl. `pad:N`, `sandbox:go:N`), `selftest`, event log (`Strike`, `Knocked`, `TeamRevived`, `LootTaken`, `AllyChanged`, `AllyAction`), the remaining assert keys. Scenarios: `glider_sandbox`, `chest_open`, `shockwave`, `deku_pad`, `hire_allies`, `skin_link_hd`, `water`, `self_test`.
   Accept: each passes twice; each has at least one assert that fails when its feature is stubbed (document the stub tried, e.g. return early in `OpenChest`); total layer-4 time for these eight under 15 min.
6. **Match-flow scenarios.** `squads_revive`, `lon_lon_buggy`, `storm_damage`, `end_screen`, `terrain_moonfall_walk`, `terrain_skyloft_walk`, `war_table_nav`.
   Accept: same; `end_screen` under 10 min; the terrain walks fail on a deliberate hole (remove one collision tri in a scratch copy of the generated header and rebuild `RoyaleMod.cpp.o` only — ~5 min).
7. **Visual layer.** `look_for` strings for every screenshot, `--goldens`/`--record-goldens`, tolerances, a `docs/REAL_GAME_TEST.md` user page ("how to read a contact sheet", the SKIP reasons) linked from `docs/README.md`; move the "verified" numbers here into `TEST_TOOL_DESIGN.md`'s status.
   Accept: the agent, given only `contact.png` paths from a run, names which scenario looks wrong in a run where one screenshot is forced black (`image_checks` also flags it).
8. **Cache and setup script.** `engine_fetch.sh` + a `game-build-linux.yml` build-only workflow producing `soh-linux-<sha>.tar.zst` (no ROM, no OTR) as a 7-day artifact; the environment setup script snippet (`game_data.sh fetch` + `engine_fetch.sh`) in `docs/REAL_GAME_TEST.md`.
   Accept: a new session reaches `GAME PASS boot_title` in under 10 min wall without building.
9. **Agent habit.** `CLAUDE.md`: "Feature that changes what the player sees or does: a scenario in `scripts/game/scenarios/` in the same PR; run `python scripts/feature_check.py --changed --game` before the handoff; paste the summary line; name the contact sheets, never attach them." Same line in `.claude/agents/gary` and `leader`.

Steps 1-2 need no mod change beyond one `#ifdef`; 3-6 are the driver and scenarios (the bulk); 7-9 polish. Rough effort:
spike 0.5 day, 1-2 one day, 3-4 two days, 5-6 three days, 7-9 one day, plus the first-run bug hunt (the game has only ever
run on the phone and in `BUILDING.md`'s words, nothing is "gameplay verified" on PC).

## 10. What the user must provide, and how

Short version for the user:

1. **Your Ocarina of Time ROM file** (the `.z64`, `.n64` or `.v64` you already use with the phone app; NTSC 1.0 or the GameCube US version are the usual ones). Either:
   - attach it to a message in a session and say "game data", or
   - put it somewhere private you control (a private GitHub release, Dropbox/Drive/S3 link) and add two secrets in the Claude Code environment settings: `ROYALE_GAME_DATA_URL` (the link) and `ROYALE_GAME_DATA_SHA256` (the file's SHA-256; on Windows `certutil -hashfile oot.z64 SHA256`, on Mac `shasum -a 256 oot.z64`). Optional `ROYALE_GAME_DATA_TOKEN` if the link needs one. The link is used only to download the file into the session's temporary disk; it is never written anywhere in the repo.
2. **Nothing else.** No game save, no phone files, no PC install. If you prefer, the phone's `TDawgsBattleRoyale/oot.otr` works instead of the ROM (same two ways), but it must come from the current build of the app.
3. **Later, optional:** look at one contact sheet per scenario once and say "that is right"; that becomes the golden set (stored at your private link, not in the repo).

The file is never shown, never copied into the repository, never attached to a pull request or a shared page, and the
container it sits in is wiped when the session ends.
