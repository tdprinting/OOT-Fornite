# OOT Royale: 32-Player Battle Royale on Ship of Harkinian

Status: design draft (milestone 0). No code yet.
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
2. **Authoritative dedicated server, not peer-to-peer.** With 32 players you need cheating resistance, one source of
   truth for storm, loot and eliminations, and no NAT problems.
   - The server is a separate headless C++ program (no renderer, no N64 game code). It does *not* run OoT actor logic.
   - Players are client-authoritative for movement (OoT physics can't be duplicated server-side). The server
     validates plausibility (speed, teleports, fire rate) and is **authoritative for health, damage, loot and storm**.
3. **UDP with a reliable channel.** Use a library like ENet or GameNetworkingSockets, not SDL_net TCP. Unreliable
   snapshots at 20 Hz; reliable messages for events (damage, pickup, elimination, match state).
4. **Don't sync the world. Sync players and events.** Each client runs its own copy of the map. The server owns only
   what matters: players, dropped items, projectiles, storm and match state. Ambient actors (grass, pots, cuccos)
   are local and non-authoritative. Hostile NPCs are either removed or run on host-less deterministic seeds.
5. **Item balance via a new item table, not the save file.** Normal OoT inventory and progression are replaced by a
   per-match loadout that the server controls.

## 4. Game design

### 4.1 Match flow
1. **Lobby** (Temple of Time scene): up to 32 players, ready-up, 2 minute timer, minimum 8 to start (bots fill later).
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
| SoH client + mod     | <-------------------> | oot-royale-server        |
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
- Single process hosts one match with a fixed 20 Hz tick loop. Horizontally scale by running one process per match.
- State machine: `Lobby -> Countdown -> Drop -> InMatch -> Ending`.
- Deterministic storm: seeded circle centres derived from the match seed and published at match start.
- Simple bots (path to storm centre, melee nearest) for filling lobbies and for load testing.

## 6. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Vanilla scenes aren't designed for 32 simultaneous players, with lots of actors and draw calls | High | Strip NPCs, enemies and grass. Cap puppet draw distance. Profile early with 32 bot puppets. |
| Client-authoritative movement allows speed hacks | Medium | Server plausibility checks. Accept some cheating risk for a fan game. |
| Combat feels bad over latency (OoT's timing windows are tight) | High | Client-side hit prediction with server reconciliation, generous hit boxes, and lag compensation on swings. |
| Upstream SoH changes break patches | Medium | Pin to release tags, keep patches small, and prefer hooks. |
| Nintendo IP | High | Fan project, non-commercial, no assets or ROM distributed, and users supply their own ROM. Do not monetise. Expect that distribution may be sensitive. |
| Animation fidelity for remote players | Medium | Start with a small set of states (idle, run, roll, slash, shoot, hurt, dead). |

## 7. Milestones

| # | Goal | Done when |
|---|---|---|
| 0 | This document | Reviewed |
| 1 | Import Shipwright as a submodule, build it on Linux, add an empty `RoyaleMod` that logs hook calls | Builds and boots with a user ROM |
| 2 | Server skeleton with 32 bot clients, 20 Hz snapshots, plus puppets rendered in a client | 32 puppets move smoothly at 60 fps |
| 3 | Storm, health, damage and elimination are server-authoritative | A full bot match finishes with one winner |
| 4 | Loot, weapons and pickups | Players can arm themselves and fight |
| 5 | Lobby, HUD, minimap, spectator | Playable end to end with friends |
| 6 | Custom larger map, bots, squads, balance | Public playtest |

## 8. Open questions
1. Which map for v1: Hyrule Field as is, or a custom scene?
2. Windows and Linux only, or also Switch, Wii U, macOS? (SoH supports all of them. A modded server protocol
   makes consoles harder.)
3. Is Anchor's relay server something we want to stay compatible with, or are we free to diverge?
4. Hosting: community-run servers, or a single hosted instance?
