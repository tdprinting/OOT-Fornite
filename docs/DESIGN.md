# OOT Royale: 32-Player Battle Royale on Ship of Harkinian

Status: design draft v2 (milestone 0). No code yet.

Decisions so far: **Hyrule Field** is the v1 map. Targets are **Windows and Android** (primary Android device: AYN Odin 2 Portal).
Minimum 1 human to start; **bots fill the remaining slots up to 32**. We use our own protocol and do not stay compatible with Shipwright's Anchor.
Matches are **host-run**: whoever starts a game hosts it (listen server), no dedicated servers required.
Base: [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) (Ship of Harkinian, "SoH").

## 1. Pitch

32 Links drop onto Hyrule Field. They loot weapons, shields and consumables, fight, and are herded together by a
shrinking storm (the Poe fog / Ganon's corruption). Last Link standing wins.

OoT's combat is slow, lock-on based and built for one hero against single enemies, so the design leans into
what the engine does well: Z-targeting duels, a big item set (bow, hookshot, bombs, Megaton Hammer, Din's Fire,
Farore's Wind), and Hyrule Field's large open map.

## 2. Key findings from the SoH codebase

| Finding | Where | Consequence |
|---|---|---|
| SoH already has a multiplayer layer ("Anchor") | `soh/soh/Network/Anchor/` | Reuse the ideas, not the transport. |
| Transport is TCP via SDL_net, JSON delimited by `\0` | `soh/soh/Network/Network.{h,cpp}` | Too heavy for 32 players at 20 Hz. Replace for gameplay traffic. |
| `PLAYER_UPDATE` is sent every frame, once per peer, as JSON including 24 joints | `Anchor/Packets/PlayerUpdate.cpp` | At 32 players that is roughly 31 x 32 messages per frame. It will not scale. Needs binary, delta-compressed, interest-managed snapshots over UDP. |
| Remote players are rendered as "dummy" Link actors | `Anchor/DummyPlayer.cpp` | Reusable as the remote-player puppet. |
| Rich hook system | `Enhancements/game-interactor/GameInteractor_HookTable.h` (`OnPlayerUpdate`, `OnActorSpawn`, `ShouldActorUpdate`, `OnPlayerHealthChange`, `OnVanillaBehavior`, `OnSceneInit`, ...) | Most of the mod can live as an Enhancement module with few edits to vanilla `z_*.c` files. This keeps upstream merges tractable. |
| Game logic runs at 20 fps natively (interpolated rendering) | Core engine | Simulation tick is 20 Hz, so network tick = 20 Hz is natural. |
| Scenes load one at a time, 428 actor overlays | `src/overlays/actors` | **Everyone must be in one scene.** All gameplay happens in one custom or existing large scene. Entering buildings or dungeons is disabled. |

## 3. Hard constraints and decisions

1. **One shared scene.** The engine only runs one scene (plus rooms) at a time. The match uses a single map. Phase 1
   uses Hyrule Field, which is large and open. Later we can build a custom combined overworld scene. Door and cave
   entrances are blocked or become loot-room "interiors" in the same scene.
2. **Host-authoritative listen server.** The player who starts a match runs the game server inside their own game
   client (a `RoyaleServer` library linked into the mod). Everyone else, including the host's own local player,
   connects to it with the same protocol.
   - The server does *not* run OoT actor logic. It owns health, damage, loot, storm and match state.
   - Movement is client-authoritative (OoT physics can't be duplicated server-side). The server checks plausibility
     (speed, teleports, fire rate).
   - The server code lives in `server/` as a plain C++ library with no game dependencies. It can also be built as
     a headless executable later for dedicated hosting and bot load tests, at no extra design cost.
   - Trade-off: the host can cheat, and if the host quits the match ends (v1; host migration is a stretch goal).
     Acceptable for a friends and community fan game.
3. **UDP with a reliable channel.** Use ENet (small, permissive license, builds for Windows and Android NDK), not
   SDL_net TCP. Unreliable snapshots at 20 Hz; reliable messages for events (damage, pickup, elimination, match state).
4. **Don't sync the world. Sync players and events.** Each client runs its own copy of the map. The server owns only
   what matters: players, dropped items, projectiles, storm and match state. Ambient actors (grass, pots, cuccos)
   are local and non-authoritative. Hostile NPCs are either removed or run on host-less deterministic seeds.
5. **Item balance via a new item table, not the save file.** Normal OoT inventory and progression are replaced by a
   per-match loadout that the server controls.

## 4. Game design

### 4.1 Match flow
1. **Lobby** (Temple of Time scene): up to 32 players, ready-up, 2 minute timer. 1 human is enough to start; empty slots are filled with server-run bots (bots count as players for the storm and loot balance).
2. **Drop**: in v1, players spawn spread across the map with 5 s of invulnerability. A skydive drop (gliding in
   from the sky, Kaepora Gaebora style) is a stretch goal.
3. **Looting**: weapons and items spawn as chests, pots and ground items at server-chosen points.
4. **Storm**: 6 phases, closing from the whole map to a final circle (see 4.4).
5. **End**: last Link standing, or last team. Victory fanfare (Item Get jingle). Return to lobby.

### 4.2 Combat and items

| Slot | Items |
|---|---|
| Melee | Kokiri Sword, Master Sword, Biggoron's Sword, Megaton Hammer, Deku Stick |
| Ranged | Fairy Bow (ammo), Slingshot, Hookshot / Longshot (pull to player), Boomerang |
| Explosive | Bombs, Bombchus |
| Magic | Din's Fire, Farore's Wind (self-revive/warp beacon), Nayru's Love (shield) |
| Defence | Deku Shield, Hylian Shield, Mirror Shield (reflects light arrows) |
| Heal | Red/Green Potions, Fairy in bottle (auto-revive once), Hearts |

- Rarity tiers (common, rare, epic, legendary) drive damage and spawn weight.
- PvP damage is applied through the server. The attacker's client reports a hit with the weapon, target and tick;
  the server checks range and line of sight and applies damage. Z-target dodge rolls give i-frames server-side.
- Health: 3 hearts base, up to 10. Shield value as an overlay, as in Fortnite.
- Stretch: a simple build mechanic (push a block, place a crate) in place of Fortnite building.

### 4.3 Loot
- Server places ~400 loot spawns from a seeded table. Chests are the high-tier loot.
- Eliminated players drop their kit as a Gold Skulltula token pile.

### 4.4 Storm

| Phase | Wait | Close time | Radius | Damage/s |
|---|---|---|---|---|
| 1 | 120 s | 90 s | 100% to 70% | 0.5 heart |
| 2 | 90 s | 60 s | 70% to 45% | 1 |
| 3 | 60 s | 60 s | 45% to 25% | 1 |
| 4 | 45 s | 45 s | 25% to 12% | 2 |
| 5 | 30 s | 30 s | 12% to 5% | 3 |
| 6 | 0 s | 20 s | 5% to 0% | 5 |

Rendered as purple Poe fog (a circle of fog and sky tint) with a minimap ring. Numbers are tunable.

### 4.5 Modes
Solo and Duos are the first modes. Squads come later.

## 5. Architecture

```
+----------------------+      UDP (ENet)       +--------------------------+
| SoH client + mod     | <-------------------> | RoyaleServer (host's game)|
|  RoyaleMod (Enh.)    |   20 Hz snapshots     |  match state machine     |
|  - hooks             |   reliable events     |  storm, loot, projectiles|
|  - puppet players    |                       |  damage / elimination    |
|  - HUD, minimap      |                       |  anti-cheat checks       |
+----------------------+                       +--------------------------+
                                                         ^
                                                         |  HTTP/WS
                                                  +------+------+
                                                  | matchmaker  |  (later)
                                                  +-------------+
```

### 5.1 Repo layout (planned)
```
docs/                      design, protocol, balance
server/                    headless match server (C++17, CMake, ENet)
shared/                    protocol structs, constants, storm math (used by both)
mod/                       SoH enhancement module (copied/linked into soh/soh/Enhancements/Royale)
patches/                   minimal patches to vanilla SoH files, kept small and documented
third_party/Shipwright/    upstream as a submodule pinned to a release tag
```
Shipwright is added as a git **submodule** (not a copy), pinned to a release tag. We carry a small patch series
for the places hooks aren't enough. This keeps rebasing on upstream cheap. ROM-derived assets are never committed:
each player supplies their own OoT ROM, as with stock SoH.

### 5.2 Protocol (binary, versioned)
- **Client to server, 20 Hz, unreliable:** `InputState` (tick, position, rotation, anim id, held item, velocity).
  About 30 bytes, quantised.
- **Server to client, 20 Hz, unreliable:** `Snapshot` containing a delta of up to N nearest players (interest
  management, nearest ~12 plus storm and loot changes). About 400 bytes at the worst case.
- **Reliable events:** `Hit`, `Damage`, `Pickup`, `Drop`, `Eliminated`, `StormPhase`, `MatchState`.
- Animation is sent as an `animId` + frame, **not** the 24-joint table. Remote Link is posed from the local
  animation data. The Anchor joint table is only a fallback for rare animations.
- Bandwidth budget: ~10 KB/s down, ~1.5 KB/s up per player. 32 players is well under 1 Mbps for the server.

### 5.3 Client mod
- `RoyaleMod` registers hooks: `OnGameFrameUpdate` (network tick), `OnPlayerHealthChange` (route damage through the
  server), `OnVanillaBehavior` (block item and story flows), `ShouldActorUpdate` / `OnActorSpawn` (strip enemies
  and NPCs), `OnSceneInit` (spawn puppets and loot actors).
- Puppets: extend Anchor's `DummyPlayer` into a `RemotePlayer` actor with interpolation (100 ms buffer).
- Loot pickups: a single custom actor type that mirrors server loot entries.
- HUD: players alive counter, kill feed, storm timer and map ring, spectator mode after elimination.
- Cheats and enhancements that break the game (the Cheats menu, Warping, speed modifiers) are disabled in match.

### 5.4 Server
- Runs inside the host's game process (or headless) and hosts one match with a fixed 20 Hz tick loop. Horizontally scale by running one process per match.
- State machine: `Lobby -> Countdown -> Drop -> InMatch -> Ending`.
- Deterministic storm: seeded circle centres derived from the match seed and published at match start.
- Server-run bots fill every empty slot at match start (a solo player gets 31 bots). Bots are server entities with simple AI: loot (walk to the nearest useful item and equip the best weapon and shield), path to the storm centre, and fight the nearest player, sent to clients as ordinary puppets, which also makes them the load test for puppet rendering.

## 6. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Vanilla scenes aren't designed for 32 simultaneous players, with lots of actors and draw calls | High | Strip NPCs, enemies and grass. Cap puppet draw distance. Profile early with 32 bot puppets. |
| Client-authoritative movement allows speed hacks | Medium | Server plausibility checks. Accept some cheating risk for a fan game. |
| Combat feels bad over latency (OoT's timing windows are tight) | High | Client-side hit prediction with server reconciliation, generous hit boxes, and lag compensation on swings. |
| Upstream SoH changes break patches | Medium | Pin to release tags, keep patches small, and prefer hooks. |
| Nintendo IP | High | Fan project, non-commercial, no assets or ROM distributed, and users supply their own ROM. Do not monetise. Expect that distribution may be sensitive. |
| Animation fidelity for remote players | Medium | Start with a small set of states (idle, run, roll, slash, shoot, hurt, dead). |

## 7. Hosting and connectivity

Host-run games make *reaching the host* the main problem, not server CPU. A 32-player match is cheap for the host:
about 32 x 10 KB/s = ~320 KB/s up and a trivial amount of CPU, since no game logic is simulated.

| Option | Works when | Notes |
|---|---|---|
| LAN / direct IP + port | Same network, or host forwards a port | v1, simplest |
| UPnP / NAT-PMP port mapping | Home routers | Try automatically on host start |
| **Join codes + STUN hole punching (recommended v2)** | Most networks, including phones on cellular | Both sides discover their public address via free public STUN servers and connect directly. A join code maps to the host's address through a tiny serverless lookup (free tier, e.g. Cloudflare Workers). No game traffic passes through it. |
| Paid relay fallback | When hole punching fails (~10-20% of connections) | Small VPS, deferred until needed. A phone cannot serve as a relay: it is unreachable behind carrier NAT. |
| IPv6 direct | Both sides have IPv6 | Skips NAT entirely, try automatically. |
| Tailscale / ZeroTier | Friends who set it up | Zero code, works today as a workaround |

Mobile hosts on cellular data will usually be behind carrier NAT, so only the relay path works for them. Cellular
upload and battery also make a handheld a poor host for a full lobby. Expected guidance: host from Windows for 32
players, host from the Odin 2 Portal on Wi-Fi for smaller groups.

## 8. Platforms: Windows and Android

**Windows:** upstream-supported (DirectX 11 or OpenGL). Milestones 1 to 5 target Windows first.

**Android:** upstream Shipwright has **no Android target** (its CMake build covers Windows, Linux, macOS, and
consoles via forks, and its README lists only DirectX 11, OpenGL and Metal). Android is therefore a port, not a
build flag. This is the largest single risk in the project.

Work needed for Android:
- Cross-compile with the Android NDK, package as an APK with SDL2's Android activity (SDL2 supports Android natively).
- Use the OpenGL ES backend of the rendering layer (libultraship). It must be checked for GLES 3 support. If missing,
  this is the biggest piece of work.
- Replace desktop file paths with Android storage: the user selects their own OoT ROM through the system file
  picker (SAF). The ROM to OTR/O2R asset extraction (the Torch tool) must run on-device or be done on a PC and copied over.
  Never bundle assets.
- Gamepad-first controls. The Odin 2 Portal has built-in gamepad controls, which SDL maps already. Touch UI
  isn't needed for v1, but menus (the ImGui UI) must be usable with the gamepad. ImGui is mouse-oriented, so
  gamepad navigation needs work.
- Performance: the Odin 2 Portal uses a Snapdragon 8 Gen 2 with 8 to 16 GB RAM, and should be adequate for N64-era
  content. The risk is thermals while rendering 32 puppets, so cap puppet draw distance and test early.
- Sustained hosting (see section 7) and background network handling (hold a wake lock while hosting).

Strategy: look at existing community Android forks of Shipwright first and decide whether to base our port on one
rather than start from scratch (need to evaluate maintenance state and license compatibility before committing).

## 9. Milestones

Android is pulled forward as a feasibility spike because it could change the whole plan.

| # | Goal | Done when |
|---|---|---|
| 0 | This document | Reviewed |
| 1 | Shipwright as submodule, Windows build, stub `RoyaleMod` logging hooks | Boots with a user ROM |
| 1b | **Android feasibility spike**: vanilla Shipwright (or a community fork) running on the Odin 2 Portal | Title screen and Link running in Hyrule Field at stable fps on device |
| 2 | `server/` library plus 32 bot clients on loopback, puppets rendered in Hyrule Field | 32 puppets smooth on Windows (and on Odin 2 Portal if 1b passes) |
| 3 | Host-a-game flow: "Host" button starts the embedded server, "Join" by IP; storm, health, elimination server-side | Full bot match finishes with one winner |
| 4 | Loot, weapons and pickups | Players can arm themselves and fight |
| 5 | Lobby, HUD, minimap, spectator | Playable end to end with friends |
| 6 | Join codes and relay, bots, balance | Cross-network play between Windows and Android |

## 10. Open questions
1. Android: build on a community fork, or port from upstream? (Needs the spike in milestone 1b.)

Resolved: Hyrule Field for v1, Windows and Android, host-run matches, 1 human minimum with bots that loot and fight,
own protocol, join codes via STUN hole punching with a free-tier serverless lookup.
