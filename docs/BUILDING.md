# Building and running OOT Royale

Platforms: **Windows** and **Android** (target device: AYN Odin 2 Portal). Linux is not supported; it is only used here
to run unit tests and as a quick compiler check.

You need your own legally obtained copy of *The Legend of Zelda: Ocarina of Time*. Only certain ROM versions work; the list
of accepted file hashes is `third_party/Shipwright-Android/docs/supportedHashes.json`. No ROM or ROM-derived asset is ever
committed to this repo.

> **Honest status.** Everything below that says "compiles" has been verified by building. Nothing has been **run in the
> game yet**: no one has launched the game with the Royale mod, so the window, puppets and match flow are untested at
> runtime. Expect to find bugs on first launch. See "Known unknowns" at the end.

## What is in the repo

| Path | What |
|---|---|
| `third_party/Shipwright-Android` | The game: the [Waterdish/Shipwright-Android](https://github.com/Waterdish/Shipwright-Android) fork of Ship of Harkinian 9.0.2, pinned to a commit. Has its own submodules. |
| `third_party/enet` | ENet 1.3.18 (MIT), the UDP library. |
| `patches/` | Small patches to the fork: hook for turning a spawned Player into a puppet, CMake hook, window and menu entry. |
| `mod/Royale/` | `RoyaleMod.cpp/.h` (the game-facing glue, copied into the fork) and `RoyaleSession.h` (host/join layer). |
| `shared/`, `server/`, `client/` | Match, bots, storm, loot, protocol, `GameServer`, `GameClient`, transports. Plain C++17, unit-tested. |
| `cmake/royale.cmake` | Pulled into the fork's CMake by patch 0002: builds ENet and our transport and links them into the game. |
| `scripts/` | `apply_patches`, `link_mod` (`.ps1` for Windows, `.sh` elsewhere), `check_mod_compile.sh`. |

## Get the code

```powershell
git clone --recursive https://github.com/tdprinting/OOT-Fornite.git
cd OOT-Fornite
git checkout claude/happy-einstein-1xydck
git submodule update --init --recursive
```
The fork pulls in `libultraship`, `ZAPDTR` and `OTRExporter` as nested submodules, so `--recursive` matters.

## Unit tests (no ROM needed)

```powershell
cmake -S server -B build/tests -A x64
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```
Four test programs run: game logic and bots, the network layer over an in-memory network, the network layer over real UDP
on localhost, and the host/join session. CI runs these on Windows (MSVC), cross-compiles them for Android arm64, and runs
them on Linux under AddressSanitizer and UBSan.

## Windows

Requirements (from the fork's own build guide):
- Visual Studio 2022 or newer with the **Desktop development with C++** workload (MSVC v143 toolset and a Windows SDK)
- CMake 3.20 or newer, Git, Python 3
- At least 8 GB of RAM (4 GB machines have seen compiler failures)

From a PowerShell prompt in the repo root:

```powershell
# 1. Apply our patches to the fork and copy the mod into it. Safe to re-run.
./scripts/apply_patches.ps1
./scripts/link_mod.ps1

# 2. Configure. The fork downloads and builds its own dependencies through vcpkg (slow the first time).
cmake -S third_party/Shipwright-Android -B build/x64 -G "Visual Studio 17 2022" -T v143 -A x64
#   If you have a newer Visual Studio instead, drop -G and -T and let CMake choose.

# 3. Generate soh.otr, then build.
cmake --build build/x64 --target GenerateSohOtr --config Release
cmake --build build/x64 --config Release

# 4. Run build/x64/soh/Release/soh.exe
```
First launch asks for your ROM and extracts the game assets (`oot.otr`), as with stock Ship of Harkinian.

Re-run `./scripts/link_mod.ps1` and rebuild whenever you edit `mod/Royale/RoyaleMod.cpp`. Edits to `shared/`, `server/`
or `client/` are picked up by the normal build because they are included by path.

## Android (AYN Odin 2 Portal and other arm64 phones)

Requirements: JDK 17, Android SDK platform 33 (and 31, which the fork's Gradle file also references), Android NDK
`26.0.10792818`, and CMake `3.31.5` (all installable with Android Studio's SDK Manager or `sdkmanager`).

```sh
git submodule update --init --recursive
./scripts/apply_patches.sh
./scripts/link_mod.sh
cd third_party/Shipwright-Android/Android
./gradlew assembleRelease        # or assembleDebug
```
The APK lands in `third_party/Shipwright-Android/Android/app/build/outputs/apk/release/` (the release build is arm64-v8a only,
which is right for the Odin 2 Portal). On Windows use `gradlew.bat` and run the two scripts' `.ps1` versions first.

Install it with `adb install` or by copying the APK to the device. Following the fork's own README: open the app, allow the
file permissions it asks for, answer **Yes** to generating the OTR, **Yes** to looking for a ROM, and pick your ROM file.
Press the controller's **Back/Select/-** button to open the menu.

CI builds the APK too (see `.github/workflows/game-build.yml`) and uploads it as a workflow artifact.

## Playing a match

All players must be running the **same build** (the host rejects a different protocol version). Everyone loads a save and
stands in **Hyrule Field**: players only see each other there.

1. Open the menu, go to **Enhancements, OOT Royale**, and open the window.
2. **Host:** set your name and a port (default 7777) and press **Host**. Share your IP address and port.
   - Same Wi-Fi: use your device's local IP address.
   - Over the internet: forward UDP port 7777 on your router, or have everyone join a VPN such as Tailscale or ZeroTier. (Join codes are planned, not built.)
   - Allow the game through Windows Firewall when it asks.
3. **Join:** enter the host's address and port, press **Join**.
4. The host presses **Start match**. Empty slots fill with bots up to 32. After a short countdown everyone is dropped at a
   spawn point; the storm closes in; health is controlled by the server.
5. Keep the game in the foreground on Android while hosting: a backgrounded app can have its sockets paused and players will time out.

## Fast compile check (Linux only, for development)

Compiling the whole game takes a long time. This checks only the mod's game-facing file against the fork's real headers and
compiler flags, in seconds:

```sh
sudo apt-get install cmake ninja-build libsdl2-dev libsdl2-net-dev libpng-dev libzip-dev zipcmp zipmerge ziptool \
    nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libboost-dev libopengl-dev libglew-dev
./scripts/check_mod_compile.sh
```

## Known unknowns

- **Android network permission.** The fork's app declared no `INTERNET` permission, so hosting failed with "could not listen on that port". Patch
  0005 adds it. If you built an APK before that patch, rebuild.

- **Never run in the game.** The window, puppet spawning, animation, health override and spawn teleport are written against
  the engine's headers and modeled on upstream Ship of Harkinian's own multiplayer code, but untested. The first launch is the
  real test.
- **Hyrule Field's size is a placeholder** (`shared/map.h`: centre 0,0 and radius 4000). Use the window's "Show Link position"
  checkbox to walk to the edges and set the real numbers. Storm, loot and spawns all scale from it.
- **Puppets are drawn empty-handed** and only distinguish idle and running. No weapon models, attacks or hurt/death poses yet.
- **Combat, loot pickup and potions are server-side only.** The protocol and server support them and they are tested, but nothing in
  the game sends attack, pickup or potion requests yet, and ground loot is not drawn. (Milestone 4.)
- **Eliminated players hit the vanilla game-over screen.** Spectating is not built.
- **Health is overridden during a live match.** The mod saves your real hearts when the match goes live and restores them when
  you leave or it ends, but if the game crashes or you save mid-match your save file could keep the 3-heart value. Use a
  throwaway save for testing.
- **Full builds on Windows and Android run in CI.** If one of those jobs is red, that is the first thing to look at.
