# OOT Royale: 32-Player Battle Royale on Ship of Harkinian

Status: design draft v2. Written and unit-tested so far: match, storm, loot with rarity tiers, bot AI, and the network
layer (all in plain C++, tested on Linux only). Not yet done: anything inside the game itself (the mod is a logging stub that has
not been compiled), and nothing has been built or run on Windows or Android.

Platforms: **Windows and Android only**. Linux is not a target; this repo's Linux container is used only to build and run the unit tests.
Decisions so far: **zero-build** (no Fortnite-style building at all); **Hyrule Field** is the v1 map. Targets are **Windows and Android** (primary Android device: AYN Odin 2 Portal).
Minimum 1 human to start; **bots fill the remaining slots up to 32**. We use our own protocol and do not stay compatible with Shipwright's Anchor.
Matches are **host-run**: whoever starts a game hosts it (listen server), no dedicated servers required.
Base: [Waterdish/Shipwright-Android](https://github.com/Waterdish/Shipwright-Android), a fork of [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) (Ship of Harkinian, "SoH") 9.0.2 that already runs on Android. One codebase for Windows and Android.

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
| Melee | Kokiri Sword, Master Sword, Biggoron's Sword, Megaton Hammer, Deku Stick (each with a rarity tier, see 4.3) |
| Ranged | Fairy Bow (ammo), Slingshot, Hookshot / Longshot (pull to player), Boomerang |
| Explosive | Bombs, Bombchus |
| Magic | Din's Fire, Farore's Wind (self-revive/warp beacon), Nayru's Love (shield) |
| Defence | Deku Shield, Hylian Shield, Mirror Shield (reflects light arrows) |
| Heal | Red/Green Potions, Fairy in bottle (auto-revive once), Hearts |

- PvP damage is applied through the server. The attacker's client reports a hit with the weapon, target and tick;
  the server checks range and line of sight and applies damage. Z-target dodge rolls give i-frames server-side.
- Health: 3 hearts base, up to 10. Shield value as an overlay, as in Fortnite.
- No building. Combat is decided by aim, movement, dodge timing and positioning, as in Fortnite Zero Build.

### 4.3 Loot and rarity tiers

Every weapon, shield and consumable has a rarity tier, as in Fortnite. Tier sets damage or effect strength, spawn
weight and the item's glow colour (the same colour family is used for the item's drop beam and inventory frame).

| Tier | Colour | Spawn weight | Stat multiplier | Example OoT items |
|---|---|---|---|---|
| Common | Grey | 40% | x1.0 | Deku Stick, Deku Shield, Slingshot, Kokiri Sword, Green Potion |
| Uncommon | Green | 28% | x1.15 | Boomerang, Bombs (5), Hylian Shield, Red Potion |
| Rare | Blue | 18% | x1.3 | Fairy Bow, Hookshot, Bombchus, Biggoron's Sword (short) |
| Epic | Purple | 10% | x1.5 | Megaton Hammer, Master Sword, Din's Fire, Longshot |
| Legendary | Gold | 4% | x1.75 | Mirror Shield, Light Arrows, Farore's Wind, Nayru's Love |

- Same item base, different tier. A Rare bow does more damage and has a faster draw than a Common one. Tiers can be
  upgraded at a workbench (a Great Fairy Fountain) for materials.
- Multipliers and weights are tunable constants in `shared/balance.h`.
- Chests are the high-tier source (weights shifted one tier up). Floor loot, pots and bushes give lower tiers.
- Server places ~400 loot spawns from a seeded table. Eliminated players drop their kit as a Gold Skulltula token pile.

### 4.3.1 Fortnite mechanics mapped onto OoT

The goal is every core Fortnite Zero Build mechanic, expressed with OoT's engine, assets and its own look. Items marked
(new) need custom code or assets beyond what the engine does today.

| Fortnite mechanic | OoT Royale version | Notes |
|---|---|---|
| Battle bus and drop | Cucco/owl glide from the sky onto Hyrule Field | (new) Glide camera and animation, uses the Kaepora Gaebora flight. Pick landing spot. |
| Skydive then glider | Skydive, then Deku Leaf glider | (new) Deku Leaf glide exists in OoT as a prop only. Needs a player state. |
| Storm | Poe fog circle | Server-driven, see 4.4. |
| Health and shield bars | Hearts (health) plus a magic-style shield bar | Shield bar reuses the magic meter. |
| Weapon rarity | Tiers above | Done in 4.3. |
| Ammo types | Arrows, Deku Seeds, bombs, bombchus, magic | Ammo pickups are rarity-neutral. |
| Looting chests and floor loot | OoT chests plus ground items | Chest opening animation reused. |
| Supply drops | Great Fairy drop on a balloon-like Deku Flower | (new) Falling crate actor, legendary loot. |
| Building (walls, floors, ramps) | **Not included.** This is a zero-build game | Replaced by mobility, terrain cover and buildings that already exist in Hyrule (see 4.6). |
| Harvesting materials | **Not included** (no building means no materials) | Cutting grass, pots and bushes still drops hearts, rupees (used at shops, see 4.7) and ammo. |
| Healing items and shield potions | Red/Green/Blue potions, fairies, Lon Lon milk | Fairy in bottle auto-revives once. |
| Mobility items | Hover Boots, Hookshot, Longshot, Epona | Epona is the vehicle, see below. |
| Vehicles | Epona (rideable horse), Deku Flower launch pads, Iron Knuckle cart | (new) Epona exists in the engine but needs multiplayer sync and a second seat. |
| Emotes | Ocarina songs and Link's existing emote animations | Ocarina songs play the song and show a note effect. |
| Teams (Duos, Squads) | Same | Revive: Farore's Wind or a fairy |
| Down but not out (DBNO) | Collapse animation (Link kneels), teammate revives | (new) Reuse the Link "damage flip" and kneel animations. |
| Spectating | Free camera over eliminated player's killer | (new) |
| Kill feed, map, ping | HUD overlay, minimap ring, marker pins | (new) ImGui or in-engine HUD. |
| Skins and cosmetics | Tunics, Kokiri or Gerudo or Zora outfits, masks | The engine already supports tunic colours and masks. |
| Victory | Item Get pose with Triforce | Reuse the Item Get animation. |
| Quests/challenges, battle pass | Out of scope for v1 | Possible later. |

"Its own style" means we keep OoT models and textures but use our own HUD art, item glows, fog, logo and sound
mixing, not Nintendo's UI chrome. Any new art is original. (IP note: see section 6.)

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

### 4.6 Zero-build: how fights and mobility work
Zero Build replaces building with movement and cover that is already in the map. Our version:
- **Mobility:** rolling, backflips and side-hops (existing Z-target moves), Hover Boots, Hookshot and Longshot to
  reach ledges, Epona, Deku Flower launchers, and the Deku Leaf glider from drops. Mobility items are tiered loot.
- **Cover and high ground:** Hyrule Field's fences, Lon Lon Ranch walls, the Market, the Kakariko and Gerudo
  approaches, the Lake Hylia bridge, trees and boulders, plus spawn-time placed crates and pots as extra cover.
  Static only: nothing can be built, moved or destroyed except pots and crates.
- **Defence:** shield items. Mirror Shield reflects projectiles, Nayru's Love blocks them for a few seconds,
  shield potions add a shield bar. A deployable **Deku Shield wall** is a consumable, single-use and
  tiered, as in Zero Build's "Shield" gadgets, not a building system.
- **Health and shield** replace the build-heal loop. Hearts regen only through items.

### 4.7 Economy (optional, v2)
Rupees picked up in the field can buy tiered items from Great Fairy and Happy Mask shop stalls, a stand-in for
Fortnite's gold bars and vending machines. Not in v1.

### 4.8 Bot AI (implemented in `server/bot.h`)
Bots are server-side entities driven through the same `Match` calls a human's messages produce (`PickUp`, `Attack`,
`UsePotion`), so they obey the same range, cooldown and pickup rules. Each tick, per bot, first match wins:

1. **Heal:** drink a potion when at 1 heart or less, or at 2 or less with no enemy within 250 units.
2. **Storm:** if the safe zone 15 s from now (shrunk to 90%) won't contain the bot, run to its centre. It still
   shoots at enemies in range while running, but never chases.
3. **Fight or flee:** engage the nearest enemy within 900 units if it is within 300, or if the bot's weapon does at
   least 1.0 damage per second. Melee closes in; ranged weapons hold about 60% of max range and back off when
   closer than 30%. At 0.8 hearts or less with no potion and a weaker weapon, the bot backs away instead.
4. **Loot:** walk to the best upgrade within 700 units and pick it up. Value is the improvement over the current
   weapon or shield, or a potion if carrying fewer than 3, divided by distance. Downgrades and unsupported
   items (Hookshot, Longshot, Farore's Wind, Nayru's Love) are ignored.
5. **Wander:** drift to random spots inside the safe zone.

Each bot gets a fixed accuracy between 55% and 90%, dropping with distance for ranged weapons. Picking up a weapon
or shield swaps it, and the old one drops on the ground. Eliminated players drop their kit.

Known gaps: bots don't use cover, mobility items (Hookshot, Hover Boots, Epona), explosives' area damage, or the
utility spells. They move in straight lines on a flat plane, so the client side will need a real navigation or
collision layer for Hyrule Field's terrain. Difficulty levels are not implemented. All numbers are placeholders.

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
server/                    match, bots, storm, loot and the host's GameServer (header-only C++17), tests
client/                    GameClient: connection, world mirror, interpolation (header-only)
shared/                    balance numbers, storm and loot math, protocol, serialization, transports (ENet, in-memory)
mod/                       SoH enhancement module (copied/linked into soh/soh/Enhancements/Royale)
patches/                   minimal patches to vanilla SoH files, kept small and documented
third_party/Shipwright-Android/    Android-capable Shipwright fork as a submodule, pinned to a commit
```
Shipwright is added as a git **submodule** (not a copy), pinned to a commit. We carry a small patch series
for the places hooks aren't enough. This keeps rebasing on upstream cheap. ROM-derived assets are never committed:
each player supplies their own OoT ROM, as with stock SoH.

### 5.2 Networking (implemented: `shared/`, `server/game_server.h`, `client/game_client.h`)

Layers, bottom to top:
- `Transport` interface (`shared/transport.h`) with two implementations: `ENetTransport` for real UDP and
  `LoopbackNetwork` (in memory, simulated latency, jitter and loss) used by tests.
- Protocol (`shared/protocol.h`): binary, little-endian, versioned (`kProtocolVersion`), every message is
  `[type byte][fields]`. Decoding rejects wrong types, short or trailing data, NaN/Inf, and out-of-range enums.
- `GameServer` (host side) and `GameClient` (every player, including the host's own, which joins through 127.0.0.1).

| Message | Direction | Delivery | Notes |
|---|---|---|---|
| Hello / Welcome / Reject | both | reliable | Welcome carries seed, map, the 6 storm circles, all loot and the roster (about 4.5 KB with 400 loot) |
| Input | client to server | unreliable, ~20 Hz | 20 bytes: seq, teleport epoch, x, y, z, rotation, animation id, scene id |
| AttackReport, PickupRequest, UsePotionRequest | client to server | reliable | The server decides whether they succeed |
| Snapshot | server to client | unreliable, 20 Hz | You plus the nearest 12 living players, 23 bytes each |
| Damaged | server to the two players involved | reliable | Everyone else sees health in snapshots |
| Eliminated, LootTaken, LootAdded, MatchState, PlayerJoined/Left | server to all | reliable | Dropped kit arrives as LootAdded |

Server-side validation:
- **Movement is clamped** to `kMaxPlausibleSpeed` (5x run speed, to allow rolls, Epona and Hookshot pulls) plus slack,
  and a single update is credited at most 0.5 s of travel. A test found that without that cap a player idle for the
  15 s countdown could jump 7,000+ units in one update. Faster speed hacks below the cap are not detected.
- **Teleport epoch:** when the server moves players (match start), it bumps their epoch, and inputs carrying the old
  epoch are ignored, so a client can't drag itself back to its lobby position.
- **Sequence numbers:** old or duplicate inputs are dropped, with 16-bit wraparound handled.
- Attacks need range and cooldown, pickups need proximity, NaN positions and malformed packets are discarded and counted.
- A peer must send a valid Hello first, and the lobby closes when the match starts (late joiners get a Reject).
- Not covered: hit validation uses the server's last known positions with no lag compensation, a client can still lie
  about whether its hit landed (within range and cooldown), and there is no per-peer rate limiting yet.

Client side: remote players are drawn 100 ms behind the newest snapshot and interpolated between two snapshots,
rotation taking the short way around the 16-bit angle wrap. The local player is not predicted: the game moves Link
as normal, sends input, and compares with `Self()` (the server's view) to notice a correction.

**Measured bandwidth** (loopback tests, payload bytes only): with 32 humans in a match, each client receives about
**6 KB/s**. ENet and UDP/IP headers add roughly 40 bytes per packet, so call it 7 KB/s on the wire, about 55 kbps.
The host uploads that to every human, so **32 humans means about 220 KB/s (about 1.8 Mbps) of host upload**.
Bots cost nothing, so a typical lobby with a few humans needs far less. This corrects my earlier "well under
1 Mbps" estimate, which was wrong for a full lobby of 32 humans. A phone host on cellular or weak Wi-Fi should
host small lobbies only. Cheap savings still available: quantize positions to 16 bits, and send deltas.

Connection details: UDP port 7777 by default (`kDefaultPort`). A host that stops answering is dropped after about 8 s
and a connection attempt gives up after about 3 s. Android may pause sockets while the app is in the background, so
a host's game must stay in the foreground (plus a wake lock) or players will time out.

Remaining network work: wire `GameClient` and `GameServer` into the mod (Milestone 3), join codes and STUN
hole punching (Milestone 6), per-peer rate limiting, and lag compensation.

### 5.3 Client mod (`mod/Royale/`, glue compiled against the fork, not yet run)
- **`RoyaleSession`** (pure C++, unit-tested): owns the ENet transports, the host's `GameServer`, and the local `GameClient`.
  Gives the game plain-data views: `Puppets()` (interpolated remote players) and `Hud()`.
- **`RoyaleMod.cpp`** (game glue, copied into the fork):
  - A top-level **Battle Royale** menu (added through the fork's own menu hook, no patch needed): name and address
    (remembered between runs), Host and Join, a lobby screen with the player list, host marker, ready toggles, the host's
    addresses with Copy buttons, and Start; countdown, in-match and results screens; and a "show Link position" developer tool.
  - A waiting room: after joining, players are taken to the Temple of Time (optional), can see each other there, and are
    moved to Hyrule Field automatically when the countdown starts. Notifications announce joins, the countdown, the drop, eliminations and the winner.
  - `OnPlayerUpdate`: sends Link's position, rotation and a coarse animation state; on the match-start teleport it
    drops Link onto the ground at the server's spawn point; while a match is live it overwrites health with the server's
    (and restores the player's real hearts afterwards).
  - Puppets: remote players are `Player` actors spawned through the new `ShouldActorInit` hook (patch 0001), re-labelled so
    they don't count as the real player, drawn with Link's own model and the matching idle or run animation. Bots are put
    on the real floor with a raycast. Names use the engine's name tag system.
  - Enemies are not allowed to spawn during a match, and existing ones are removed at countdown.
- **Not yet built:** weapon models on puppets, attack/hurt/death poses, drawing ground loot and sending pickup, attack and
  potion requests, a real HUD (alive count, kill feed, storm ring on the minimap), spectating, a lobby scene (for now the
  lobby is Hyrule Field itself), and disabling cheats and warps during a match.
- Patches to the fork (`patches/`): 0001 `ShouldActorInit` hook, 0002 CMake hook, 0004 MSVC void* fix, 0005 Android network permission, and
  `patches/0004` (MSVC rejects arithmetic on `void*` in the fork's `z_message_PAL.c`), `patches/libultraship/0001`, a
  one-character fix for a missing semicolon in the fork's libultraship that stops it compiling on every non-Android platform.
  The fork is Android-first and had not been built on Windows at this commit, so more such fixes may turn up.

### 5.4 Server
- Runs inside the host's game process (or headless) and hosts one match with a fixed 20 Hz tick loop. Horizontally scale by running one process per match.
- State machine: `Lobby -> Countdown -> Drop -> InMatch -> Ending`.
- Storm: seeded circle centres generated on the server and sent to clients at match start (6 circles). Clients interpolate between them. Clients do not re-derive them from the seed, since trig results can differ slightly between Windows (x86) and Android (ARM). Loot positions are likewise sent by the server.
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

**Android:** upstream Shipwright has **no Android target**, so we use the community fork below as our base (its CMake build covers Windows, Linux, macOS, and
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

Decision: our base **is** [Waterdish/Shipwright-Android](https://github.com/Waterdish/Shipwright-Android), pinned as the
submodule `third_party/Shipwright-Android` at commit `c9d8f4a` (last commit 2025-07-10, Shipwright 9.0.2). It builds an
APK through Gradle (`Android/`), is controller-friendly, requires OpenGL ES 3.0 and lists Android 15 as tested, which
covers the Odin 2 Portal.

What I verified in its source:
- It has the `GameInteractor` hooks the stub mod needs (`OnLoadGame`, `OnSceneInit`, `OnPlayerHealthChange`,
  `OnPlayerUpdate`, `OnActorInit/Update/Kill`, `OnVanillaBehavior`) and `ShipInit`.
- It does **not** have the `ShouldActorInit/Update/Destroy` hooks from newer upstream. We strip enemies by killing
  actors in `OnActorInit`, or add the hooks as a small patch under `patches/`.
- It has no Anchor multiplayer, which suits us, since we write our own.

Risks that remain:
- **Lag behind upstream.** It is on 9.0.2 and upstream is past 9.2.3. We take bug fixes from upstream only as needed.
- **Single maintainer, no visible license file** in the top three directory levels of the repo. Upstream Shipwright's own
  licensing also needs checking. This matters before any public release. I haven't resolved it.
- **Touch input:** menus are said to work with touch and controller, but ImGui gamepad navigation in our lobby
  and HUD still needs to be tested on device.

## 9. Milestones

Android is pulled forward as a feasibility spike because it could change the whole plan.

| # | Goal | Done when |
|---|---|---|
| 0 | This document | Reviewed |
| 1 | Android fork as submodule (done), patches (done), Windows and Android builds of the whole game (CI), stub logging mod (replaced by the real glue) | Boots with a user ROM |
| 1b | **Android spike**: the unmodified fork APK running on the Odin 2 Portal | Title screen and Link running in Hyrule Field at stable fps on device |
| 2 | `server/` library (done: storm, loot, match, bot AI, network layer over loopback and real UDP) plus puppets rendered in Hyrule Field | 32 puppets smooth on Windows (and on Odin 2 Portal if 1b passes) |
| 3 | Host-a-game flow in the game: Host and Join in the window, puppets, server-owned health (written and compiled, **not yet run**) | Two players and bots in Hyrule Field, one winner |
| 4 | Loot, weapons and pickups | Players can arm themselves and fight |
| 5 | Lobby, HUD, minimap, spectator | Playable end to end with friends |
| 6 | Join codes and relay, bots, balance | Cross-network play between Windows and Android |

## 10. Open questions
1. License: confirm licensing of the fork and upstream before any public release.

Resolved: Zero Build (no building), Android via Waterdish/Shipwright-Android, Hyrule Field for v1, Windows and Android, host-run matches, 1 human minimum with bots that loot and fight,
own protocol, join codes via STUN hole punching with a free-tier serverless lookup.
