# OOT Royale: 32-Player Battle Royale on Ship of Harkinian

Status: design draft v2. Written and unit-tested so far: match, storm, loot with rarity tiers, bot AI, and the network
layer (all in plain C++, tested on Linux only). Not yet done: anything inside the game itself (the mod is a logging stub that has
not been compiled), and nothing has been built or run on Windows or Android.

Platform: **Android only** (Windows was dropped; the code still builds nothing for it). Linux is not a target; this repo's Linux container is used only to build and run the unit tests.
Decisions so far: **zero-build** (no Fortnite-style building at all); **Hyrule Field** is the v1 map. Target is **Android** (AYN Odin 2 Portal).
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

The loot pool holds 83 items, 82 of them from Ocarina of Time and the Shockwave Grenade in six kinds (the full list is in section 4.2.1). Each kind works differently:

| Kind | How you use it | Slots |
|---|---|---|
| Weapon | B attacks with it. Damage, range and speed come from the item and its tier. Some add an effect: Deku Nuts stun, Fire Arrows burn, Ice Arrows freeze, Light Arrows ignore shields, Bombs and Bombchus hurt everyone near the target | 1 (a new one swaps the old, which drops) |
| Shield | Absorbs a share of damage | 1 |
| Consumable | Potions and bottled things in the bag. D-pad Down drinks the best-fitting one. The Fairy is never drunk: it revives you once if you would die | 3 |
| Instant | Used on pickup: Recovery Hearts, Pieces of Heart (four make a container), Heart Containers (+1 max hearts, up to 10), Magic Jar (shortens your ability's recharge) | none |
| Ability | D-pad Up uses it, then it recharges. Din's Fire, Nayru's Love, Hookshot, Farore's Wind (mark a spot, then jump back), the 12 songs, the Lens of Truth and more | 1 |
| Gear | Passive. Tunics, boots, gauntlets, masks, scales, bags and charms; one per slot, stacking across slots (resistances, speed, damage dealt) | 7 |

Rarity scales every number: weapon damage, healing, ability strength and duration, gear bonuses.

- PvP damage is applied through the server. The attacker's client reports a hit with the weapon, target and tick;
  the server checks range and line of sight and applies damage. Z-target dodge rolls give i-frames server-side.
- Health: 7 hearts base, up to 10. Opened chests spill a few rupees; some Rare chests hold a Piece of Heart. Shield value as an overlay, as in Fortnite.
- No building. Combat is decided by aim, movement, dodge timing and positioning, as in Fortnite Zero Build.

### 4.2.1 Item catalog

Generated from `shared/items.h`; the unit tests check that the table is complete and consistent.

| Item | Kind | Tiers | What it does |
|---|---|---|---|
| Deku Stick | Weapon | Common to Uncommon | Weak melee |
| Kokiri Sword | Weapon | Common to Rare | Fast melee |
| Master Sword | Weapon | Epic to Legendary | Strong melee |
| Biggoron's Sword | Weapon | Rare to Epic | Heavy melee, long reach |
| Megaton Hammer | Weapon | Epic to Legendary | Slow, huge melee hit |
| Slingshot | Weapon | Common to Rare | Weak, long-range |
| Fairy Bow | Weapon | Rare to Legendary | Strong, long-range |
| Boomerang | Weapon | Uncommon to Rare | Mid-range, quick |
| Bombs | Weapon | Uncommon to Epic | Thrown, explodes on everyone near the target |
| Bombchus | Weapon | Rare to Epic | Long-range explosive |
| Deku Nuts | Weapon | Common to Rare | Flash: stuns the target for 2 seconds |
| Fire Arrows | Weapon | Rare to Epic | Sets the target on fire |
| Ice Arrows | Weapon | Rare to Epic | Freezes the target: it can't act and takes extra damage |
| Light Arrows | Weapon | Legendary to Legendary | Huge damage that ignores shields |
| Deku Shield | Shield | Common to Rare | Absorbs a little damage |
| Hylian Shield | Shield | Uncommon to Epic | Absorbs a fair amount of damage |
| Mirror Shield | Shield | Epic to Legendary | Absorbs a lot of damage |
| Green Potion | Consumable | Common to Rare | Heals 1 heart |
| Red Potion | Consumable | Uncommon to Epic | Heals 2 hearts |
| Blue Potion | Consumable | Rare to Legendary | Heals 3 hearts |
| Fairy | Consumable | Uncommon to Legendary | Revives you once if you would die |
| Lon Lon Milk | Consumable | Common to Uncommon | Heals 1.5 hearts |
| Fish | Consumable | Common to Common | Heals half a heart |
| Blue Fire | Consumable | Uncommon to Rare | Heals half a heart and puts out fire |
| Bugs | Consumable | Common to Uncommon | Heals a little and cures fire and stun |
| Poe | Consumable | Rare to Epic | Take half damage for 6 seconds |
| Recovery Heart | Instant | Common to Uncommon | Heals 1 heart on the spot |
| Piece of Heart | Instant | Uncommon to Rare | Four make a Heart Container |
| Heart Container | Instant | Epic to Legendary | +1 maximum heart and heals it |
| Magic Jar | Instant | Common to Rare | Recharges your ability most of the way |
| Din's Fire | Ability | Rare to Legendary | Fire burst around you |
| Farore's Wind | Ability | Epic to Legendary | Mark a spot, then jump back to it |
| Nayru's Love | Ability | Epic to Legendary | Invulnerable for 4 seconds |
| Hookshot | Weapon (B) | Uncommon to Epic | B shoots the chain: reels the player in front of you to you and stuns them |
| Longshot | Ability | Epic to Legendary | Pull from much farther away |
| Lens of Truth | Ability | Uncommon to Epic | See every player for 10 seconds |
| Magic Beans | Ability | Common to Rare | Heal over time for 10 seconds |
| Fairy Ocarina | Ability | Common to Uncommon | Plays a random simple song |
| Ocarina of Time | Ability | Legendary to Legendary | Plays a random song from the whole list |
| Zelda's Lullaby | Ability | Uncommon to Epic | Heals 1 heart |
| Epona's Song | Ability | Uncommon to Rare | Run faster for 6 seconds |
| Saria's Song | Ability | Uncommon to Rare | See every player for 6 seconds |
| Sun's Song | Ability | Rare to Epic | Stuns everyone close to you |
| Song of Time | Ability | Legendary to Legendary | Freezes everyone nearby while you can't be hurt |
| Song of Storms | Ability | Rare to Epic | Lightning strikes everyone near you |
| Minuet of Forest | Ability | Uncommon to Rare | Run faster and heal half a heart |
| Bolero of Fire | Ability | Rare to Epic | Sets everyone near you on fire |
| Serenade of Water | Ability | Rare to Epic | Heals 1.5 hearts and puts out fire |
| Nocturne of Shadow | Ability | Rare to Epic | Vanish and reappear somewhere else |
| Requiem of Spirit | Ability | Rare to Epic | Stuns and hurts everyone near you |
| Prelude of Light | Ability | Rare to Epic | Heals 1 heart and protects you briefly |
| Shockwave Grenade | Weapon (B) | Common to Legendary | B throws it: a shockwave that hurts and stuns everyone around the target |
| Kokiri Tunic | Gear | Common to Uncommon | Plain: slightly less damage taken |
| Goron Tunic | Gear | Uncommon to Epic | Half damage from fire and explosions |
| Zora Tunic | Gear | Uncommon to Epic | Less storm damage |
| Kokiri Boots | Gear | Common to Uncommon | Plain: a little faster |
| Iron Boots | Gear | Uncommon to Epic | Can't be stunned or frozen, slower |
| Hover Boots | Gear | Rare to Legendary | Run noticeably faster |
| Goron's Bracelet | Gear | Common to Rare | +10% melee damage |
| Silver Gauntlets | Gear | Uncommon to Epic | +20% melee damage |
| Golden Gauntlets | Gear | Rare to Legendary | +35% melee damage |
| Keaton Mask | Gear | Common to Uncommon | A little less storm damage |
| Skull Mask | Gear | Common to Rare | +10% ranged damage |
| Spooky Mask | Gear | Common to Rare | 8% less damage taken |
| Bunny Hood | Gear | Uncommon to Epic | Run 20% faster |
| Goron Mask | Gear | Common to Rare | Less fire and explosion damage |
| Zora Mask | Gear | Common to Rare | Less storm damage |
| Gerudo Mask | Gear | Common to Rare | +10% melee damage |
| Mask of Truth | Gear | Uncommon to Epic | +5% ranged damage and 5% less damage taken |
| Silver Scale | Gear | Common to Uncommon | A little less storm damage |
| Golden Scale | Gear | Uncommon to Rare | Less storm damage |
| Big Quiver | Gear | Uncommon to Epic | +15% ranged damage |
| Bullet Bag | Gear | Common to Rare | +10% ranged damage |
| Bomb Bag | Gear | Common to Rare | +10% ranged damage |
| Forest Medallion | Gear | Rare to Legendary | Run 12% faster |
| Fire Medallion | Gear | Rare to Legendary | 60% less fire damage |
| Water Medallion | Gear | Rare to Legendary | 40% less storm damage |
| Spirit Medallion | Gear | Rare to Legendary | +20% melee damage |
| Shadow Medallion | Gear | Rare to Legendary | +20% ranged damage |
| Light Medallion | Gear | Rare to Legendary | 12% less damage taken |
| Kokiri's Emerald | Gear | Epic to Legendary | 5% less damage taken, a little faster |
| Goron's Ruby | Gear | Epic to Legendary | Half the explosion damage, 40% less fire damage |
| Zora's Sapphire | Gear | Epic to Legendary | 25% less storm damage |

Not included (they have no sensible meaning in a battle royale): quest items, trading-sequence items, keys, maps and compasses, Gold Skulltula tokens, the Fishing Rod, Epona herself, and warp songs beyond Nocturne of Shadow's random teleport.

### 4.2.2 Chests, hotbar, scenery, scoring and rematch

- **Chests.** Generated loot is placed in the game's own chest actor (`EN_BOX`), kept shut and inert until the server confirms the open, then given back to the game's normal opening animation. Walking over a chest never opens it; the server only accepts an open when the client asks on purpose (`PickupRequest.force`). Only dropped items lie on the ground. The glow and beam are drawn in screen space from the chest's projected position.
- **Hotbar.** Three weapon slots (one in hand, `kMaxReserveWeapons` spares), switched with `SelectWeaponRequest`. A pickup that is not an upgrade fills a free slot; with the bar full it replaces the weapon in hand. Bots switch for range.
- **Scenery.** `shared/props.h` generates rocks, boulders, bushes and standing stones from the match seed; the host sends the list so every client agrees. Solid ones block the bots' navigation grid.
- **Scoring.** `ScorePoints` in `shared/balance.h`. The server sends `EvResults` when the match ends; the host's `RematchRequest` rebuilds the world with a new seed and starts the countdown at once (`GameServer::PlayAgain`).

### 4.2.3 Points of interest and custom models

- `shared/poi.h` lays out the map from the match seed: a landmark in the middle, five towns on an inner ring (about half the radius), six on an outer ring (about 0.8). Each town is a walled building (a ring of stone posts with a door gap), a cave (a horseshoe of boulders) and ruins. Every piece is a prop, so it goes to clients in the same message and the bots' navigation grid blocks the walls but leaves the door open. A chest spot is defined for each room; those chests are always on the better tiers, and only about 150 chests are scattered elsewhere.
- Names are an index into `kPoiNames` (sixteen rhyming names, shuffled per match, the centre always "Hylian Billion Pavilion").
- `shared/meshes.h` builds our own models in code (rocks, boulders, an eight-sided stone post, a gabled cottage roof) as flat-shaded triangles with baked light. The game layer converts them to vertices and display lists and draws them from the props' stand-in actors, with the game's own rock kept as the invisible solid part. This avoids shipping binary model files and keeps the geometry unit-testable. A real new Hyrule Field scene (terrain mesh, collision, textures) would need a different pipeline (authoring tools and the game's resource format) and is not part of this build.

### 4.2.4 Mini bosses, player count, lobby timer, quest option

- **Bosses** (`shared/boss.h`, `Match::TickBosses`): ids from 5000. Chase the nearest player in 450 units, leash 1100 from home, smash every `cooldown` seconds; healed while walking home. `Match::Attack` accepts a boss id; `KillBoss` drops `drops` chests (60% Epic, 40% Legendary) in a ring. Sent to clients in `Snapshot.bosses` (only ones within 4500 units, with what each is doing: `mode` and its variant `aux`) and `EvBossDown`. Each kind is one of the game's own enemies or dungeon bosses, with a way of getting around (`Traverse`: leap, climb, swim, burrow, roll, submerge, warp, vanish, fly) used when the nav grid (`server/nav.h`, the same one the bots use) has no path, and specials of its own (`Match::StartMiniSpecial`, `Match::TickDragon` and the per-boss `*Attack` functions). Their area attacks are `Strike`s with a `StrikeStyle` (fire burns, ice freezes, water/shadow/magic stun) that clients draw with the game's own particles. The client (`mod/Royale/RoyaleBosses.h`) draws each with the game's own skeleton and animations on a stand-in actor, tinted in its colour scheme the way the game tints an enemy (fog over the model). Bots (`bot.h`) engage when healthy with a decent weapon, otherwise keep 420 units away.
- **Player count** (`Match::SetPlayerLimit`, 2 to 32): the lobby cap and the number the bots fill up to; sent in `MatchStateMsg.limit` and `Welcome.limit`. Towns and bosses scale with it.
- **Lobby timer** (`GameServer::SetAutoStart`, default 120 s from the first player): the server counts, clients see `Snapshot.lobbyLeft`, the host's game starts the match at zero (it has to go to the field and measure first); the server does it after 30 more seconds if that never happens.
- **Quest option** (`patches/0008`): a fifth quest type in the file select. The quest id isn't stored in the save, so `z_sram.c` records `gSettings.Royale.BRFile<n>` for files made this way; the mod opens the menu on its page when one loads.
- **Falling limp** is a simple body simulation (gravity, bounces, friction, a spin and wobble that die away) on a puppet actor, then Link's knocked-down animation. On top of that, 17 loose joints (head, neck, chest, lower spine, pelvis, shoulders, elbows, wrists, hips, knees, ankles) flop as small spring chains: each is dragged along by the joint it hangs from, bends only so far and bounces off its limit. The cap tail and tunic skirt keep swaying through the normal cloth code. Every elimination (storm, a hit, a boss, a cart) ends in one of these bodies. In the lobby a test ragdoll can be spawned, shoved, hit with the sword and carried (hold L). The Ragdoll debug switch turns the extra joints and the test ragdoll off.
- **Time of day** is the game's own lighting, driven by storm progress. **Shadows**: the game draws blob shadows under actors, and our scenery and bosses use them; real dynamic shadow casting from lights, global illumination and shiny (specular) materials are not possible from a mod, because the renderer (the Fast3D interpreter in libultraship) emulates the N64 graphics chip's fixed pipeline and has no hooks for custom shaders or extra render passes. The nearest things in the build are the game's own glint effect on chests and dropped items, the time-of-day lighting and blob shadows. Doing more would be a renderer project inside libultraship, separate from this mod.

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

### 4.6.1 Carts (implemented: `shared/vehicle.h`, `server/match.h`, `server/bot.h`, the carts section of `RoyaleMod.cpp`)
- **What:** the Lon Lon Buggy, a wooden two-seater (driver and one passenger), 4 to 16 per match by map size, parked on flat open ground just
  outside the towns. The model and its measures come from Blender (docs/CUSTOM_MODELS.md).
- **Physics** (`StepCart`, shared by the server and the game): a bicycle model with an engine that fades towards top speed (420, against 135 for a
  sprinting Link), brakes, reverse, steering that tightens at low speed, grip that lets the back slide out with the handbrake, slopes that speed it up
  or slow it down, steps up to 34 units it rolls over, walls it slides along or bounces off, and ledges it flies off and lands from. It runs in
  50 ms ticks cut into 25 ms pieces.
- **Authority:** the server owns the seats, the cart's health and the hits. A human driver's game runs the physics against the real collision and
  reports the cart every tick (the move is clamped like a player's); the server runs it on its navigation grid's heights for bots and for carts
  rolling with nobody at the reins, and takes over if a human driver's reports stop. Snapshots carry the player's own cart and the five nearest
  within 4,000 units (20 bytes each); a cart parked empty for over a second goes out in every fifth snapshot only, and clients keep it in between.
- **Damage:** running people over (0.6 hearts at speed 120, more the faster, once a second per person), crashes and hard landings (the cart; the
  riders only for hard ones), and weapons (hammers and bombs hit harder). A cart has 14 hearts; when it is wrecked the riders are thrown off, its
  firebox blows up (1.5 hearts within 260 units, credited to whoever last hit it), and the wreck burns for nine seconds.
- **Bots** walk to a cart when the trip is long (out of the storm, across the map, or because they like driving), when running from a losing fight,
  or to chase a fight a long way off; they follow paths over open ground, slow for corners, drift if skilled, back out when stuck and get out where
  they are going, when the cart is about to blow, or to fight. Aggressive ones run people down. Bots climb into the back of a cart stopped beside
  them, shoot from the saddle, take the reins if the driver leaves, and jump out of the way of carts coming at them.

### 4.7 Economy (optional, v2)
Rupees picked up in the field can buy tiered items from Great Fairy and Happy Mask shop stalls, a stand-in for
Fortnite's gold bars and vending machines. Not in v1.

### 4.8 Bot AI (implemented in `server/bot.h` and `server/nav.h`)
Bots are server-side entities driven through the same `Match` calls a human's messages produce (`PickUp`, `Attack`,
`UsePotion`, `UseAbility`), so they obey the same range, cooldown and pickup rules.

**Personality.** Each bot rolls its own aggression, caution, greed, aim and reaction time, so they don't all behave alike.
The host picks a difficulty (Easy, Normal, Hard) in the lobby: it sets aim, reaction time, sight range, how often bots use
abilities, whether they dodge and kite, and whether they hunt.

**Perception and memory.** A bot sees enemies within its sight range (everyone while a Lens of Truth or Saria's Song
reveal is on) and is alerted, with wider senses, for a few seconds after taking damage. It remembers where it last saw its
target and goes looking for it. A newly seen enemy isn't engaged until the bot's reaction time has passed.

**Assessment.** `BotController::Advantage` compares how long each side needs to kill the other, from weapon damage per
second (with bonuses for burn, freeze, stun, pierce and splash), health plus healing in the bag, shields, gear and
stun/invulnerability. Targets are scored by that advantage, how hurt they are and how far away they are, with a bonus for
sticking to the current target.

**Decisions, first match wins, each tick:**
1. Stunned or frozen: nothing.
2. Heal: drink a potion when critical, or when hurt and nobody is close, or when burning and low.
3. Ability: use it when it fits (see below), otherwise keep it.
4. Storm: if the safe zone 15 s from now (shrunk to 90%) won't contain the bot, run for it, shooting but not chasing.
5. Flee: when the advantage drops below the bot's threshold (more cautious bots leave sooner), run away, leaning toward the
   zone, for a few seconds without flip-flopping. Use escape abilities, shoot back if possible.
6. Fight: strafe around the target, step in to melee range or hold distance with ranged weapons, kite melee enemies with a
   bow while it recharges, sidestep just before the enemy's attack lands (Normal and Hard), never swing at an invulnerable
   target, and finish stunned ones.
7. Loot: score every pile by how much it improves the bot's kit (weapons by effective damage, shields, bag space, Fairies,
   Heart Pieces and Containers while below the heart cap, Recovery Hearts when hurt, abilities by worth, gear slots and
   tiers), weighted by greed and distance, ignoring loot outside the coming safe zone. A choice is kept unless something
   is clearly better, and re-evaluated twice a second. Dropped kits from eliminated players are ordinary loot.
8. Hunt: healthy, aggressive bots close in on a visible weaker enemy or the last place they saw one. In the last six
   players bots stop waiting and go find each other.
9. Wander: drift to open spots inside the zone.

**Abilities.** Each ability has its own trigger: Din's Fire and the damage songs when an enemy is in their radius;
Nayru's Love and Song of Time when about to die; Hookshot or Longshot to reel in a foe out of sword range; Farore's Wind
marks a spot when safe and jumps back when fleeing, or when far outside the zone; healing songs when hurt; Epona's Song when
outside the zone or fleeing; Lens of Truth and Saria's Song when nobody is known and the match is late; Nocturne of Shadow to
escape; the Ocarinas as a gamble when a foe is near or the bot is hurt.

**Navigation.** The host's game probes the floor when the match is prepared and builds a `NavGrid` (60 unit cells) that
knows each cell's floor height and what scenery stands on it. Bots path with A* over eight neighbours, with no corner
cutting, a capped search, string-pulling to a few waypoints and repaths when the goal moves. Height steps up to
`NavGrid::kStepUp` are walked, up to `kClimbUp` are a jump and a clamber (a ledge, the next climbing block, a low boulder),
higher is a wall; drops up to `kDropDown` are fine, deeper is a cliff they go round. Water and hazard floors are off the
grid. Bosses and allies keep to open ground as before. They slide along walls and, if they stop making progress, pick a new
heading. Without a grid (unit tests, the headless server) they walk straight lines.

**Moving like a player.** Bots skydive in: they hang `kSkyHeight` up through the countdown, pick a landing spot in glide
reach with chests close by (greedy bots want the most loot, cautious ones a spot no other bot is heading for), glide there and
dive once diving still gets them there. They sprint on the players' stamina numbers (`kSprintMult`, `kSprintSeconds`, ...)
out of the storm, away from a losing fight, after a runner, to a supply drop or far loot, keeping some of the bar back unless
it is an escape. A bot's `y` is its height above the scene floor (a block top, a fall, the sky); clients add the floor.

**Using the ground.** Hills and tall scenery block sight: a bot doesn't notice someone hidden behind them (unless within 250
units), and its shots into them miss. Hurt with a bow trained on it, it gets behind a rock or over a rise before drinking,
and runs for cover when fleeing from one. A bot with a bow takes higher ground near by when there is some, and shoots
better downhill; a sword can't reach up a ledge.

Known gaps: bots don't use Hover Boots or Epona, swim, or climb ladders and vines; all numbers are placeholders until the
game has been played.

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
| Snapshot | server to client | unreliable, 20 Hz | You plus the nearest 12 living players, 25 bytes each |
| Damaged | server to the two players involved | reliable | Everyone else sees health in snapshots |
| Eliminated, LootTaken, LootAdded, MatchState, PlayerJoined/Left, Ready, MapConfig | server to all | reliable | Dropped kit arrives as LootAdded; MapConfig rebuilds the lobby's map, storm and loot once the host has measured the field |

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
**6.8 KB/s**. ENet and UDP/IP headers add roughly 40 bytes per packet, so call it 7 KB/s on the wire, about 55 kbps.
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
    moved to Hyrule Field automatically when the countdown starts. Notifications announce joins and the countdown; the drop, eliminations and the winner are shown in the centre of the screen.
  - `OnPlayerUpdate`: sends Link's position, rotation and a coarse animation state; on the match-start teleport it
    drops Link onto the ground at the server's spawn point; while a match is live it overwrites health with the server's
    (and restores the player's real hearts afterwards).
  - Puppets: remote players are `Player` actors spawned through the new `ShouldActorInit` hook (patch 0001), re-labelled so
    they don't count as the real player, drawn with Link's own model and the matching idle or run animation. Bots are put
    on the real floor with a raycast. Names use the engine's name tag system.
  - Enemies are not allowed to spawn during a match, and existing ones are removed at countdown.
- **Gameplay in the game (written and compiled, not yet played):**
  - Loot actors for the loot near you, using the game's rupee model in the tier's color with a rarity-colored name label, grabbed by
    walking over them (the server checks range).
  - B attacks with the server-side weapon at the nearest player in a cone in front of you; D-pad Down drinks a potion; hits flash red.
  - Other players hold the weapon the server says they have (swords, hammer, bow, slingshot, boomerang) and can be Z-targeted.
  - An always-on HUD: alive count, storm phase and timer, a pointer to the safe zone, weapon, shield and potions, plus banners for
    countdown, elimination and results.
  - Eliminated players become invisible, invulnerable spectators instead of dying to the game-over screen.
  - When the host presses Start the game takes them to Hyrule Field, measures the playable area by probing the floor, rebuilds the
    world on it (`GameServer::Reconfigure`, broadcast as `EvMapConfig`) so loot, spawn points and storm centres are on ground that
    exists, then starts.
- **Not yet built:** shields drawn on other players, visible projectiles and bomb/spell effects, a minimap, Hookshot/Longshot/Farore's Wind/
  Nayru's Love items, terrain-aware bots, and disabling cheats and warps during a match.
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

---

## Appendix: what was built beyond the original design (October 2026)

Everything below is implemented, covered by the server tests, and described for players in `docs/PLAY.md`.

| Area | Where it lives |
|---|---|
| Five maps with themes, mini bosses, a major boss each, 24 place names each; Hyrule Field's ten signature places | `shared/map.h`, `shared/poi.h`, `shared/boss.h`, `docs/MAPS.md` |
| Real map data from the ROM (extractor tool, measured arenas, exit and loading-zone avoidance) | `tools/rom-extractor.html`, `docs/MAPS.md` |
| Seasons and weather (deterministic from the seed, per-map tables), lightning, fog and sand effects on bot sight | `shared/weather.h`, `server/match.h` (`TickWeather`) |
| Economy: rupees, ammo, special weapons, death drops (half the kit), supply drops | `shared/items.h`, `shared/combat.h`, `server/match.h` |
| Magic meter (`kMaxMagic`), Adult Power, Heart Container chests | `shared/balance.h`, `server/match.h` |
| Hireable allies (four kinds, follow and fight, healed or freed when the owner falls) | `shared/ally.h`, `server/bot.h` (`StepAllies`), `server/match.h` |
| Climbs, hideaway chests, spaced chest sites | `shared/poi.h`, `shared/loot.h` |
| Smarter bots (rolls, lock-on footwork, hazards, calm opening, gear first, magic aware) | `server/bot.h` |
| Cloth and wind (glider canopy, cap tail, tunic skirt, sheath) | `shared/cloth.h`, `patches/0009-player-hat-limb-hook.patch`, `patches/0015-player-cloth-limb-hook.patch` |
| Match replay (recorded by the server, sent at the end, drawn top-down) | `shared/replay.h`, `server/match.h` (`TickReplay`) |
| Logo everywhere (launcher icons, title screen, menus) | `assets/logo.png`, `scripts/make_logo_assets.py`, `scripts/apply_logo.sh` |
| The sign in the middle of every map, and Maya the Kokiri | `RoyaleMod.cpp` (sign, Maya), `shared/map.h` |
| Lilo the cat: a low poly Blender model (about 650 triangles, RGBA16 fur and face textures, 25 bones, 11 clips), skinned on the CPU; as a pet she follows you with walk, run, jump, sit, groom, sleep, stretch, pounce and happy clips | `tools/lilo/`, `assets/lilo/`, `shared/lilo_model.h` (generated), `shared/lilo_anim.h`, `RoyaleMod.cpp` (Lilo) |
| Lilo's voice: seven recordings of the real Lilo (16 kHz mono, trimmed and cleaned) on their own mixer voice, one per mood or line, plus her fart (the synthesised one, a huge green toxic cloud that the server tracks and that hurts anyone who stays in it past 3 seconds, and a "*cough*" over everyone within 420 units; `FartCloudRequest` / `EvFartCloud`, `Match::TickGas`, `kFart*` in `shared/balance.h`) | `assets/lilo/sounds/`, `scripts/make_lilo_sounds.py`, `shared/lilo_sounds.h` (generated), `RoyaleMod.cpp` (`PlayMeow`, `FartReact`) |
| Baby Avriella's voice: 15 recordings of the real baby (16 kHz mono) on their own mixer voice, sorted into laughs, babble, "hi" squeals, coos and low grunts, one random clip per matching action with a few seconds of quiet between, her mouth moves with them | `assets/avriella/sounds/`, `scripts/make_avriella_sounds.py`, `shared/avriella_sounds.h` (generated), `RoyaleMod.cpp` (`BabyVoice`, `BabyCoo`) |
| Pets wander: while you stand still Lilo and Avriella pick a new spot 130 to 260 units from their usual place every 10 to 20 seconds, and come back when you move | `RoyaleMod.cpp` (`PetWander`, `PetWanderStep`) |
| Avriella's toys: a stuffed Bluey (five ways to play: hug, swing, chew, make it dance, wave it), a TV that pops out of the ground and a laptop (she pops it out after sitting still about 15 seconds). Rigid-part Blender models posed by angles; original kids-show sounds (xylophone tune, slide whistle, boing, bonk, ta-da) | `tools/avriella/build_toys.py`, `assets/avriella/toys.blend`, `shared/avriella_toys.h`, `scripts/make_avriella_toy_sounds.py`, `shared/avriella_toy_sounds.h`, `RoyaleMod.cpp` (`DrawToy`, `StartBabyToy`, `BabyMood::Plush/Tv/Laptop`) |
| Lilo, Maya and the sign talk through the game's own text box (custom messages, text ids from 0x7F00) | `patches/0012-royale-custom-message-table.patch`, `RoyaleMod.cpp` (talking) |
| Avriella the baby, a second pet (picked in the "Your pet" menu section): a low poly Blender model (about 1050 triangles, cloth and skin textures and five faces, 23 bones, 16 clips) that rolls after you (she cannot crawl yet) and then smiles, kicks, chews a rock, waves, claps, babbles, giggles, stacks rocks, wobbles on her feet, reaches for loot, waves a green cap about on cloth springs and naps; her lines go through the game's own text box (ids from 0x7F30); looks only, behind its own Debug switch | `tools/avriella/`, `assets/avriella/`, `shared/avriella_model.h` (generated), `shared/avriella_anim.h`, `shared/map.h` (lines), `RoyaleMod.cpp` (Avriella, `DrawPetOptions`) |

Balance is tuned with a headless simulator (`royale_balance [matches] [easy|normal|hard] [map] [players]`): the targets are the first kill within about 20 seconds, 20+ players alive at one minute, matches of roughly four to six minutes (longer on the big maps), few storm deaths, and the major boss a threat rather than the main killer. The protocol version is 20.
