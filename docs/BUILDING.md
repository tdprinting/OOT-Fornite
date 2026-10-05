# Building and running OOT Royale

Platform: **Android only** (target device: AYN Odin 2 Portal). Linux is not supported; it is only used here
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
| `patches/` | Small patches to the fork: hook for turning a spawned Player into a puppet, CMake hook, window and menu entry, and (`0012`) the Battle Royale boot: the title screen opens the mod's menu and the file select loads the Battle Royale save. |
| `mod/Royale/` | `RoyaleMod.cpp/.h` (the game-facing glue, copied into the fork) and `RoyaleSession.h` (host/join layer). |
| `shared/`, `server/`, `client/` | Match, bots, storm, loot, protocol, `GameServer`, `GameClient`, transports. Plain C++17, unit-tested. |
| `cmake/royale.cmake` | Pulled into the fork's CMake by patch 0002: builds ENet and our transport and links them into the game. |
| `scripts/` | `apply_patches`, `link_mod` (shell scripts), `check_mod_compile.sh`. |

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
on localhost, and the host/join session. CI cross-compiles them for Android arm64, and runs
them on Linux under AddressSanitizer and UBSan.

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
which is right for the Odin 2 Portal).

The APK installs as **TDawgs Battle Royale** (`com.tdawg.battleroyale`) and keeps its files in `/storage/emulated/0/TDawgsBattleRoyale`, so it never touches a normal Ship of Harkinian install (patches `0007` and `libultraship/0002`; the Java package `com.dishii.soh` stays, since the native code binds to it).

Install it with `adb install` or by copying the APK to the device. Following the fork's own README: open the app, allow the
file permissions it asks for, answer **Yes** to generating the OTR, **Yes** to looking for a ROM, and pick your ROM file.
Press **Start** on the title screen to open the Battle Royale menu. The controller's **Back/Select/-** button still opens the Ship of Harkinian menu for graphics and button mapping.

CI builds the APK too (see `.github/workflows/game-build.yml`) and uploads it as a workflow artifact.

## Playing a match

All players must be running the **same build** (the host rejects a different protocol version). The game loads its own
Battle Royale save, so there is nothing to pick.

1. Press **Start** on the title screen and choose **Play** in the Battle Royale menu.
2. **Host:** choose **Host a match**, set it up and choose **Open the lobby** (port 7777 unless changed in the settings). **Invite friends** shows your addresses.
   - Same Wi-Fi: use your device's local IP address.
   - Over the internet: forward UDP port 7777 on your router, or have everyone join a VPN such as Tailscale or ZeroTier. (Join codes are planned, not built.)
3. **Join:** choose **Join a match**, type the host's address and choose **Join**.
4. The host chooses **Start the match**. Empty slots fill with bots up to 32. After a short countdown everyone is dropped at a
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
- **Hyrule Field's size is measured at match start**, by probing the floor under a grid of points (`MeasureField` in `mod/Royale/RoyaleMod.cpp`). The
  result is only as good as that probe; if the circle looks wrong, the "Show Link position" developer tool helps debug it. `shared/map.h` holds
  the fallback used when measuring fails.
- **Attacks are simple.** B attacks with the server-side weapon at the nearest player in a 90 degree cone in front of you, within weapon range. Hit
  detection is therefore generous. Ranged weapons do not fire visible projectiles yet.
- **Loot looks like rupees.** The game's own rupee models in five colors, plus a name label. No custom models.
- **Bots do not know the terrain.** They move in straight lines on a flat plane and are drawn on the real floor, so they can pass through walls.
- **Health is overridden during a live match.** The mod saves your real hearts when the match goes live and restores them when
  you leave or it ends, but if the game crashes or you save mid-match your save file could keep the 3-heart value. Use a
  throwaway save for testing.
- **The full Android build runs in CI.** If one of those jobs is red, that is the first thing to look at.

## The logo and the launcher icon
`assets/logo.png` is the logo. `scripts/apply_logo.sh` (run by the build after the patches) turns it into the Android launcher icons and the in-game copy (`mod/Royale/logo_data.h`) with `scripts/make_logo_assets.py` (needs `pip install pillow`); the generated files are also committed, so a build without Pillow still has them. To change the logo, replace `assets/logo.png` (a transparent or white-background picture; its lettering is black, so it is shown on a pale backdrop).

## Patches
`patches/0009-player-hat-limb-hook.patch` adds the hook the cap physics uses. Patches are applied in order and each is checked to be "already applied" on re-runs, so a patch must not touch lines next to another patch's changes.
