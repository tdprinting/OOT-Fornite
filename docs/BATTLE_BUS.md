# The Moonfall Bus (Battle Bus)

Design and build spec for the match-start vehicle: Fortnite's Battle Bus, made of Ocarina of Time and Majora's Mask. Everyone starts the
match aboard it, it flies one straight line over the island, each player picks the moment to jump, free-falls, opens the glider and lands.
This document is the brief the build followed (model, animation, code).

Status: built. The model is `tools/bus/build_bus.py` (Blender, `bpy`) with `paint_bus.py` (the textures) and `export_bus.py` (to
`shared/bus_model.h` and `shared/bus_geometry.h`); `render_previews.py` renders `docs/battle-bus/bus-*.png` and `bus-turntable.gif`. The game
side is `mod/Royale/features/BattleBus.inc`, the rules are in `shared/bus.h`, `server/match.h` and `server/bot.h`.
What the build changed from the brief, at Tee's request: smooth and higher poly, like a GameCube asset (about 42,000 triangles, smooth normals;
the far model past 4500 units is 360); the Moon is 900 units across (the concept sheet's proportions, not 1050); the Moon's face is a decal over
the rock that fades out at its edge, so there is no seam; the net dips under the face so no rope crosses the grin; the window sill is 70 above
the floor, so a seated Link looks out; the roof is not drawn for the player sat inside, so the camera sees the island and the other riders.
Concept art (`concept-2d.png`), the flight-line diagram and the earlier concept renders stay in `docs/battle-bus/`.
Reference video in Tee's message could not be fetched from here (YouTube is blocked for this container), so the bus behaviour below follows
Fortnite's Battle Bus as it is known to play: a bus under a hot-air balloon, random straight line over the island, horn, jump any time after
it crosses the coast, forced out at the far end, skydive then glider.

## 1. The idea in one paragraph

A long lacquered royal coach, stretched to a bus and trimmed with gold and glowing Sheikah runes, hangs under the Majora's Mask Moon
itself: a cratered grey rock with huge glowing eyes and a grin full of teeth. A Goron pilot at the front drives it with a tiller; a Goron
firebox (the same bomb-flower stove as the Lon Lon Buggy) sends its flame up into the Moon's mouth and spins a big Deku-leaf propeller at
the back. A bronze horse head rides the prow, lanterns swing from the eaves, a Hylian crest banner trails behind. Thirty-two seats in two
rows. The Moon looks down at the island it is about to fall on, which is why the map is called Moonfall Isle.

## 2. Look and silhouette

Style rule (Tee): OoT or the newer stylised 3D Zelda look. Flat colour blocks, chunky shapes, painterly 256x256 atlases, no realistic detail.
Tee's references (project files, `battle-bus/refs/`): the Majora's Mask Moon, Wind Waker's Deku Leaf, Fortnite's Battle Bus and Breath of the
Wild's Master Cycle Zero. They are inspiration, not things to copy; the bus is our own.

Read against the sky as three stacked shapes: the cratered Moon, four ropes to a brass burner cage, a long lacquered coach with a horse-head
prow and a leaf propeller. Proportions follow Fortnite's bus: the body is a little longer than the balloon is wide, the balloon sits one body
height above the roof.

| Part | What it looks like | Colours (art guide) |
|---|---|---|
| The Moon | A grey, cratered rock sphere 1050 units across, the Majora's Mask Moon as it hangs over Clock Town, and it has to be unsettling, not cute: thick dark brows slanted hard inward, sunken sockets with sagging bags under them, huge bulging orange eyes (yellow centre, red rim, bloodshot veins) with tiny pupils that look in toward the bridge, no nose but two flared nostrils and a crease between the eyes, deep creases from the nostrils down past the mouth corners, cracks in the rock radiating from the grin, and a grin stretched wider than the eyes with the lips pulled back over two clean, even rows of big square teeth (ten up, ten down) and a black gap between them. The face looks forward and down at the island. A rope net over its lower half ends in a dark collar at its mouth, and the burner's flame goes up into the collar (the Moon "swallows" the fire). The face is one 256x256 texture with the eyes as emissive regions; the rest of the sphere tiles a 128x128 crater texture, with the craters also dented in the mesh. | rock #7A7A72, shadow #3E3C38, eye yellow #FFD23A (glowing), socket red #C83A1E, teeth #E9E2CC |
| Burner cage | A brass cage on the roof holding the Goron firebox (a round iron stove banded with rock, a bomb flower glowing in its mouth); the flame rises from it. Four ropes run from the cage's ring up to knots on the net's bottom ring, so the Moon is visibly tied to the bus, and a sandbag hangs on a short line from each corner of the cage ring. Nothing floats: every rope, lantern, arm and pole is attached at both ends. | brass #B98B3C, iron #55585E, flame #FF7A1A |
| Body | A royal coach stretched to a bus, 1100 long, 320 wide, 300 high, rounded corners: dark red lacquer with gold bands at the waist and skirt, seven arched windows a side with brass frames and round brass portholes over them, a red crest plaque with a gold Triforce on each flank, a smaller gold Triforce on the hood plaque and under the rear window, a big flat gold Triforce inlaid at each end of the roof, and a Triforce on the crest pennant. Sheikah ornament in Master Cycle style: one thin cyan rune line runs the length of the waist and a Sheikah eye glows on every wheel hub. Open glass front for the pilot, a short hood with a brass grille, cyan lamps. | lacquer #6A2A16 and #4A1C0E, gold #E0B93A, crest red #B23A2A, rune cyan #3FD6F2 (glowing) |
| Horse-head prow | A bronze horse head on the hood, mane of gold fins, a glowing cyan eye, Epona as the Master Cycle shows her. | brass, rune cyan |
| Seats | Inside, two rows of 16 benches facing forward, 32 seats, red saddle cloth with a gold stripe; the open window band is at head height so you see the island from your seat. | saddle brown #671E09, red #AD3725 |
| Pilot | The Goron ally model (`shared/ally_goron_model.h`) on a stool in the glass front, one hand on a brass tiller. | as the ally |
| Propeller | Four Deku leaves on a brass hub at the rear, 450 across, spinning; the hub sits on a brass arm bolted to the rear deck, braced to the rail. | leaf #4E9A3C, vein #A9D36A |
| Wheels | Four big spoked cart wheels on iron arms, tucked up under the skirt, the glowing hub eyes facing out. | dark wood, iron |
| Rear platform | A wooden deck with a brass rail and three steps (the lobby's boarding ramp), a Hylian crest pennant on a pole off its corner. | wood #8E5A2B, crest red and gold |
| Lanterns | Eight paper lanterns on brass hooks under the eaves, warm glow, swinging. | lantern #FFCF6E |

The glider gets a matching skin: a Deku Leaf (Wind Waker) held overhead, one big green leaf with pale veins and a few sparkles, in place of
the striped hang glider, as an optional step in the build plan (the existing cloth sim gives it the flutter). The old glider stays as a
selectable look.

Textures: three atlases at most. `bus_atlas` 256x256 (lacquer, gold, brass, iron, wood, cloth, glass, runes, crest), `moon_face` 256x256
and `moon_rock` 128x128. RGBA16, loaded with a tile load (see docs/ENGINE_LIMITS.md: anything past 2048 texels uses `gDPLoadTextureTile`).
Lighting is baked into vertex colours with the fixed sun, as every custom model does; the runes, eyes, lamps and flame are drawn with the
emissive combine so they glow at night.

Budget: body with windows 1500 tris, Moon 900 (a 32x16 sphere with the face modelled in: brow, sockets, nose, mouth dent, 20 teeth), ropes
and net 160, burner cage and firebox 220, propeller 90, horse head 140, lanterns 8x20, wheels 4x70, deck 60. About 3400 tris, plus 32 seated
Link puppets, which the game already draws. A far LOD (Moon as a 12x6 sphere, body as a textured box, about 350 tris) draws past 4500 units.

Scale check: adult Link is 68 units. The window band is at 230 to 350 above the floor, so a seated Link (about 90 high) looks out of it.
Seat pitch 54 along the body, two rows 120 apart, an aisle between.

Concept art: the four-view sheet `docs/battle-bus/concept-2d.png` (side, front, rear, top, with callouts; `tools/bus/concept_sheet.py`
draws it) is the look to model from. Blender blockout renders (rough, not the game model): `docs/battle-bus/concept-sheet.png`; the four full-size views and
`bus_concept.blend` are in the project files under `battle-bus/`. `tools/bus/concept_blockout.py` rebuilds them.

## 3. Model parts, pivots and rig

Build it the way the cart was built: `tools/bus/build_bus.py` (Blender, from code, `bpy`), `tools/bus/export_bus.py` -> `shared/bus_model.h`
(verts, batches of at most 32, tris, both textures) and `shared/bus_geometry.h` (part enum, pivots, seat and exit empties, bounds), and
`tools/bus/render_previews.py`. Blender metres, Z up, facing -Y; the export does x100, Y up, facing +Z. Keep object, material, image and
action names exactly as below.

Objects (one mesh per rigid part, origin at its pivot):

| Object | Pivot | Moves how |
|---|---|---|
| `BusBody` | floor centre | the root: translate and yaw along the path, plus sway |
| `Moon` | the Moon's mouth collar (bottom) | pendulum sway: roll and pitch, 2 degrees, period 4 s; the eyes' glow pulses slowly |
| `BurnerCage` | roof centre | fixed to the body; its `Ring` empty is where the ropes tie |
| `Rope0..3` | top end, on the ring | stretched between ring and body each frame (a scaled unit cylinder) |
| `Propeller` | hub centre, axis along the body | spins about its axis, 540 degrees per second |
| `Firebox` | its base | none |
| `Chimney` | its base | none; smoke spawns from the `Smoke` empty |
| `Lantern0..5` | the hook | swings 10 degrees opposite to the body sway |
| `Banner` | the pole top | a strip of cloth sim (shared/cloth.h, as the glider's canopy and the cart's pennant) |
| `Tiller` | its hinge | 8 degrees left or right in the pilot's idle |
| `HorseHead` | neck base | none; its eye is an emissive region |
| `WheelFL..BR` | axle | roll only in the lobby |
| `Pilot` | stool | the Goron ally model in its sitting clip; one hand on the tiller |

Empties (exported as constants in `bus_geometry.h`): `Seat00..Seat31` (bench positions, facing +Z), `Exit` (the open left side, where a
jumping Link is placed for his first frame), `Smoke`, `Flame` (the burner's flame, a billboard pair), `BannerTop`, `Horn` (sound origin at the front), `LobbyRamp`
(the rear steps), `Bounds` (box).

Animation in game is rigid-part matrices, exactly like the cart's `DrawCart` (mod/Royale/features/Vehicles.inc:479-516): push, translate to
the pivot, rotate, scale 1/8, draw the part's display list. No skinned clips are needed for the bus itself. The pilot reuses the ally
animator (`shared/ally_anim.h`) with the existing idle clip.

Blender actions to key (20 fps, named so the exporter can read the numbers out): `Sway` (Moon pendulum, 80 frames loop), `Lantern`
(40 frames loop), `Door` (lobby only: the rear steps fold down, 30 frames). The flame is two crossed billboard quads scaled by a 0.3 s
flicker in code, orange to yellow, drawn on the translucent layer. The propeller and wheels spin from code.

Sounds (OoT sound effects, no new audio): burner whoosh every 2.5 s (`NA_SE_EV_FLAME_IGNITION` or the fire loop the cart's firebox uses),
propeller hum (the windmill loop), horn (two notes of Epona's Song on the ocarina synth, short), jump (Link's jump voice plus a wind whoosh),
glider open (what the current skydive plays, if anything; otherwise the Deku Leaf rustle).

## 4. Where it hooks into the match

Today (match.h:326-337): `Lobby -> Countdown (10 s) -> Drop (18 s) -> InMatch -> Ending`. The epoch teleport (Terrain.inc:1330-1344) puts
every client at its spawn x,z at `kSkyHeight` 3000 above the ground, the glider is up through the countdown, everyone falls during Drop,
and `Damage()` ignores everything while the state is Drop (match.h:373). Bots do the same on the server (bot.h:126-132, 671-730).

New flow:

```
Lobby -> Countdown (kCountdownSec: 24 s since the map intro, aboard, bus climbing offshore)
      -> Drop     (the bus crosses the island; jump any time after the coast; forced off at the far end;
                   each player is protected until 2 s after landing)
      -> InMatch  (storm timer starts, as now)
```

No new `MatchState`. The bus lives inside Countdown and Drop. What changes is how long Drop lasts and what protection means.

### 4.1 Numbers (new constants in shared/balance.h, next to the skydive block)

| Constant | Value | Why |
|---|---|---|
| `kBusAltitude` | 3200 | above the map's median floor; mountains on Moonfall top out at 2000 |
| `kBusSpeed` | 300 units/s | Moonfall lines run 14800 to 15800 units: about 50 s crossing, like Fortnite's |
| `kBusMinSpeed` / `kBusMaxCrossSec` | 120 / 60 | small maps slow the bus so a crossing never takes under 25 s; no map makes it take over 60 s |
| `kBusMargin` | 1500 | the line starts and ends this far outside the map circle |
| `kBusCorridor` | 0.45 | the line's closest approach to the map centre is at most this fraction of the map radius |
| `kBusJumpOpenRadius` | map radius x 0.92 | jumping unlocks when the bus is inside this circle (over land) |
| `kFreefallSpeed` | 380 (`kDiveSpeed`) | free fall after the jump, before the glider |
| `kAutoGlideHeight` | 600 | the glider opens by itself this high above the ground, as Fortnite forces the glider |
| `kBusExitPush` | 1.0 s | the player keeps the bus's forward speed for this long after jumping, so a jump carries you ahead of the bus |
| `kLandedGraceSec` | 2.0 | protection ends this long after touchdown |
| `kDropMaxSec` | crossing + 25 | hard end of Drop even if someone is still floating (then `kLateFallSpeed` as today) |

`kDropSec` goes away; `kSkyHeight` stays for the sandbox's "glide from here" button and tests.

### 4.2 The path (deterministic, no per-tick sync)

`shared/bus.h` (new, header only, used by server, bots, client and tests):

```cpp
struct BusPath { Vec2 start, end; float altitude, speed, length; };
BusPath MakeBusPath(uint64_t seed, const Circle& map);     // seed ^ "bus!" : bearing, offset, then clamp to the corridor
Vec2    BusPosAt(const BusPath&, float t);                  // t = seconds since Drop began; clamped to [0, length/speed]
float   BusYawAt(const BusPath&);                           // constant
bool    BusOverLand(const BusPath&, float t, const Circle& map); // inside kBusJumpOpenRadius
float   BusCrossSec(const BusPath&);                         // length / speed
```

Path construction: pick a bearing in [0, 2pi) and a signed offset d in [-0.45 R, 0.45 R] from the seed; the line is the chord
perpendicular to the bearing at offset d, extended `kBusMargin` past the circle at both ends. Speed = clamp(length / 50, `kBusMinSpeed`,
`kBusSpeed`), then slowed further so the crossing is at most `kBusMaxCrossSec`. During Countdown the bus sits at `start` and climbs from
sea level to `kBusAltitude` over the 10 s (purely visual, the players aboard are at the seat positions whatever the height).

The map circle is `Match::map` (the host's measured circle, which for Moonfall covers the island, HALF_X 7412 by HALF_Z 7705).

### 4.3 Server (server/match.h, server/game_server.h, shared/protocol.h)

Protocol goes to 40 (39 is Moonfall's, PR #126; the two must be merged in order).

- `Match::Start()` (match.h:295-322): after the spawn loop keep `RandomPointIn` for the x,z (it still seeds bots and is a fallback), then
  `bus = MakeBusPath(seed, map)` and set every player `aboard = true`, `seat = index in roster order`, `landedAt = -1`.
- `Match::Tick()`: in Countdown nothing new. In Drop: `busT = stateTime`; for each aboard player set `pos = BusPosAt(bus, busT)` and
  `y = kBusAltitude` (ignoring their Input; see below); when `busT >= BusCrossSec(bus)` force every remaining aboard player off (`aboard =
  false`, `jumpedAt = clock`). Drop ends when `(nobody aboard && everybody has landed)` or `stateTime >= kDropMaxSec`.
- Jumping: no new message. `Input.anim` (protocol.h:72-85) carries a new value `Anim::BusRide = 37` while seated and `Anim::Freefall = 38`
  or `Anim::Glide = 39` once out. `HandleInput` (game_server.h:645-678): while `p.aboard`, if `BusOverLand(bus, busT)` and the input's anim
  is not `BusRide`, the player leaves the bus: `aboard = false`, `jumpedAt = clock`, `pos = BusPosAt(bus, busT)` (the server places the exit;
  the client's reported x,z from then on is trusted as today, with the speed clamp relaxed to `kBusSpeed + kAirSpeed` for the first
  `kBusExitPush` second). While aboard and not allowed off, the input's position is ignored (seat position wins) and its anim is forced to
  `BusRide`. Landing: the first Input with an anim that is neither `Freefall` nor `Glide` after `jumpedAt` sets `landedAt = clock`.
- Protection: `Damage()` (match.h:373) returns false while `state == Drop` or `p.aboard` or `clock < p.landedAt + kLandedGraceSec` or
  `p.landedAt < 0` (still falling). Storm damage is untouched (InMatch only). `HireAlly` (match.h:994) adds `!p.aboard`.
- `MatchStateMsg` (protocol.h:343): unchanged. New reliable event `EvBusPath` (type 92): `{f32 sx, sz, ex, ez, altitude, speed}` sent with
  the Countdown `StateChanged` (and in `Welcome` to a late joiner, which cannot happen after Lobby anyway). 25 bytes.
- `Snapshot` (protocol.h:486): one new field after `lobbyLeft`: `u32 aboardMask` (bit = roster index; 32 players fit exactly). Clients draw
  every aboard player on their seat from this mask without needing a `PlayerNet`, so interest management (12 nearest) is not a problem on
  the bus, and nothing else is added per player. `PlayerNet.flags` gets `kAboard = 16` for the entries that are sent (relax the `flags <= 15`
  check at protocol.h:384 to `<= 31`).
- `game_server.h` `StartMatch()` (:325-331) keeps the epoch bump: the client's epoch teleport is what puts you on the bus.

### 4.4 Client (mod/Royale/features/)

New file `BattleBus.inc`, included from RoyaleMod.cpp after `Vehicles.inc` (it uses `DrawPromptRow`, `PlaceOnSeat`-style helpers and the
glider) and listed in `mod/Royale/FEATURES.md` as "Battle Bus: flight, seats, jump, free fall".

- Bus actor: an `ACTOR_EN_ISHI` stand-in like `ReconcileLocalGlider` (GroundWeather.inc:113-129) with
  `ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED` and uncull 9000/4000/4000 (it must draw from the ground). Alive
  from the Countdown epoch until Drop ends plus 6 s (it flies on out to sea and is killed). `Bus_Update` sets the body transform from
  `BusPosAt(path, client->StateElapsed())` in Drop, or the climb during Countdown, plus the sway. `Bus_Draw` is `DrawCart`'s loop over
  parts with the pivots from `bus_geometry.h`, the far LOD past 4500 units, lantern glow quads, the banner cloth, and the smoke (the cart's
  chimney puffs, scaled). Mesh upload as `BuildCartGpu` (Vehicles.inc:296-340), `sub = 8`, two textures via tile loads.
- Seats and puppets: `aboardMask` from the snapshot. For each set bit, the roster entry's player is drawn seated at `Seat[i]` with the cart
  passenger pose (`PuppetRide`, Vehicles.inc:967+, which already plays the Epona `uma_*` sit) via a new `PuppetBusSeat`. `Puppet_Draw`
  (Players.inc:1181) must not draw a glider for an aboard player: `HangingFromGlider` (Players.inc:829) gains `&& !(st->flags & kAboard)`.
  Falling players draw with the free-fall pose until their anim says `Glide`, then as today.
- You, aboard: the epoch teleport (Terrain.inc:1330-1344) sets `gAboard = true` instead of `gSkydiving`, and each frame `PlaceOnBusSeat`
  pins Link to `Seat[selfIndex]` (the `PlaceOnSeat` pattern, Vehicles.inc:780-803) with the sit pose, camera in the normal third person.
  `SendLocalPose` reports `Anim::BusRide`. Z-target, items, emotes, sprint and the touch JUMP button are off while aboard (same gates as
  `gRide.seated`). The horn: C-Right (`pr.emote`) plays the horn sound locally and sends `Anim::BusRide` with the horn bit (anim value
  `BusHorn = 40`, one tick) so others hear it from the bus; a 1.5 s cooldown per player.
- Jump: A (`pr.jump`) while `BusOverLand`. Link is placed at `Exit`, `gAboard = false`, `gFreefall = true`, velocity = bus forward x
  `kBusSpeed` decaying over `kBusExitPush`, the game's jump voice plays. Free fall: a new state in `UpdateSkydive` (Terrain.inc:916-951)
  before the glider: fall at `kFreefallSpeed`, steer at `kAirSpeed`, Link in the game's long-fall animation (arms out; the build thread picks
  the `link_normal_*` fall clip that reads best), camera pulled back 1.3x. A again, or height above ground under `kAutoGlideHeight`, opens the
  glider: `gFreefall = false`, `gSkydiving = true`, `ReconcileLocalGlider(true)` with a 0.4 s pop (scale 0.3 to 1 with the cloth sim snapping
  open), and from then on everything is the existing skydive: stick steers, Z dives, `SteerToSafeLanding`, touchdown at ground + 8.
  `SendLocalPose` reports `Freefall` then `Glide` (a new anim value so the server knows when you have landed; today `HangingFromGlider`
  guesses from height).
- Forced off: when `StateElapsed() >= BusCrossSec` and still aboard, the same as pressing A, with the "The bus is leaving!" line 5 s before.
- Late fall: unchanged (InMatch and still in the air: `kLateFallSpeed`).
- Drop-phase protection HUD: the hearts show a small shield glyph while protected (optional).

### 4.5 HUD and prompts (Hud.inc, EndMatch.inc, Vehicles.inc)

- Prompts via `DrawPromptRow` (Core.inc:543), gated like `DrawControlPrompts` (Vehicles.inc:1050-1080) but shown while aboard and falling:
  - aboard, bus not yet over land: `A  Jump (soon)` greyed, `C-Right  Honk`
  - aboard, over land: `A  Jump`, `C-Right  Honk`
  - free fall: `A  Open glider`, `Z  Dive`
  - glider: today's `Hold Z to dive   Stick to steer` (EndMatch.inc:672) moves into this row as `Z  Dive`
- Top line (EndMatch.inc:665-673): the splash as now, then during Countdown `MATCH STARTS IN n` as now with "All aboard the Moonfall Bus"
  under it; during Drop while aboard `Over <place name>` (the POI the bus is nearest, from `NoticePoi` data) and the time until the far
  end; 5 s before the end `The bus is leaving!` in storm purple.
- Minimap (Hud.inc:274-286): while aboard or falling it zooms out as it does when skydiving today (`gSkydiving` becomes
  `gSkydiving || gAboard || gFreefall`), draws the bus line as a dashed cream line from start to end, the bus as a small moon icon moving
  along it, and the first safe zone edge as now. Players who have jumped are not shown (Fortnite hides them too).
- POI labels (Hud.inc:171-182) already use 14000 reach while skydiving; same flag change.
- `DrawGliderAim` (Terrain.inc:1028) draws during free fall as well, using `kFreefallSpeed`.
- Touch: the round JUMP button is reused as the jump-from-bus and open-glider button while aboard or falling (today it is hidden while
  skydiving, Vehicles.inc:1069).

### 4.6 Bots (server/bot.h)

Replace the lift-to-`kSkyHeight` start (bot.h:126-132) with a bus plan:

- On the first tick of Countdown, each bot picks `landAt` with `ChooseLanding` (bot.h:671-698) scored over candidate spots within glide
  reach of the bus line, not of its spawn: reach from altitude 3200 is `kAirSpeed * (3200 / kGlideSpeed) * 0.8`, about 1750 units; so the
  candidate filter becomes "distance from the point to the bus line under reach, and the point inside the jump-open circle". Aggressive bots
  weight the Temple of Time hot drop and the crater; greedy ones chest density; timid ones spots far from the line's busiest stretch.
- `jumpT` = the time the bus is nearest the chosen spot's foot of perpendicular, minus a personality offset: skilled bots jump early and
  glide diagonally (up to `reach / kBusSpeed` seconds early), poor bots jump late and sometimes get forced off at the end. Add 0 to 2 s of
  jitter so 32 bots do not leave on the same tick.
- While aboard, `p.pos` is set by the server (4.3); the bot does nothing. At `busT >= jumpT` and `BusOverLand`, the bot sets its own
  `aboard = false` (bots are server side; go through the same `LeaveBus(p)` helper the input path uses), `lift = kBusAltitude`,
  `airborne = true`, `freefalling = true`.
- `Glide` (bot.h:701-730): free fall at `kFreefallSpeed` until `lift - ground < kAutoGlideHeight` or until gliding gets it there in time
  (skilled bots open late, as players do), then the existing glide and dive logic. `p.anim` reports `Freefall` then `Glide`, then whatever it
  does on the ground, which is what sets `landedAt` on the server.
- Bots never honk, except a 5% chance per bot once per match, because it is funny.

### 4.7 Allies, carts, other maps, sandbox

- Hireable allies (match.h:977-990) stay on the ground; they cannot be hired while aboard. Carts spawn as now.
- Any map works: the path is built from the map circle, so Hyrule Kingdom, the Fortnite Map and the original scenes all get the bus. On
  the original OoT scenes the bus stays at `kBusAltitude` over the scene's fallback circle; `SafeLanding` already keeps people out of lava
  and doorways.
- Sandbox: no bus (no countdown, no drop), unchanged. The Sandbox panel gets one button, "Bus flyover", that spawns the bus actor on a line
  over the plaza for looking at it; no seats, no jump.
- Solo test: the bus still flies; one player aboard.

## 5. Lobby and boarding

Two layers. The first is required, the second is a polish step the build thread can defer.

1. **Required.** The lobby is unchanged (waiting room or the island meadow at `SPAWN`). On Start the host's `DriveStart` runs as now and
   every client's epoch teleport puts them in their seat on the bus, parked at the path's start out at sea, during the splash. The countdown
   plays over the bus climbing to altitude. Anyone still loading in from the waiting room appears in their seat when they arrive, exactly as
   they appear in the sky today.
2. **Polish.** On island lobbies (Moonfall, Kingdom, Fortnite Map) the bus is parked on its wheels beside the lobby meadow (`LobbyRamp` on
   the ground at `SPAWN` + (0, 0, -400) on Moonfall, facing the temple), ramp down, balloon slack and half-size. Walking up the ramp and
   pressing A on a bench (`A  Take a seat`) seats you and counts as `SetReady` (game_server.h:569-583); pressing A again stands you up and
   clears ready. When the host starts, the ramp folds up (`Door` action), the balloon inflates over 3 s, the burner roars, the bus lifts
   off the meadow, and the camera follows it up as it banks out to sea; then the normal Countdown continues with the bus already at the path
   start. Players not aboard are teleported in as in layer 1. The Lobby state already has the `ready` flag and roster; the only new state is
   a per-client "seated in the lobby" bit that rides on `Input.anim = BusRide` while in Lobby.

## 6. Sync summary

| Thing | How it is kept in sync |
|---|---|
| Bus position and heading | Deterministic: `EvBusPath` once, then `BusPosAt(path, StateElapsed())` on every machine. Clients already keep `StateElapsed()` from the snapshot's state and tick, so drift is under one snapshot interval (50 ms, 15 units). |
| Who is aboard | `Snapshot.aboardMask` (u32, 20 Hz), seats by roster index |
| Jump | the first `Input.anim != BusRide` once over land; the server moves the player to the exit point that tick |
| Positions after the jump | client authoritative as today (`HandleInput`), with the speed clamp relaxed for `kBusExitPush` |
| Landed | the first `Input.anim` that is not `Freefall`/`Glide` after the jump; bots set it themselves |
| Protection | server only: `Damage()` rules in 4.3 |
| Horn | one-tick anim value `BusHorn`; everyone within 6000 plays the sound at the bus |
| Drop end | server decides (everyone landed, or `kDropMaxSec`); clients see `MatchStateMsg` as today |

Bandwidth: 25 bytes once, 4 bytes per snapshot. Nothing per player.

## 7. Step-by-step build plan

Who: Leader plans and checks (`.claude/agents/leader.md`), Ronnie the Runner searches, builds, tests and does git, Gary writes the code. The
model and animation are the build thread's own work (Opus 5.5). Each step ends with its check; do not start the next until it passes.

1. **Model blockout** (Opus): `tools/bus/build_bus.py` from `tools/cart/build_cart.py`'s `Part` class, writing `assets/bus/bus.blend`,
   `bus.glb`, `bus_atlas.png`, `moon_face.png`. Objects, empties and actions named as in section 3. Check: `tools/bus/render_previews.py`
   contact sheet (front, side, three-quarter, from below) looks like section 2 and the previews go to `/mnt/project-files/battle-bus/`.
2. **Export** (Opus): `tools/bus/export_bus.py` from `export_cart.py`, two textures with a `tex` index per batch (the allies' pattern,
   `shared/ally_model_types.h:13-17`), writing `shared/bus_model.h` and `shared/bus_geometry.h`. Check: a `server/tests/bus_tests.cpp` case
   that the headers compile, tri count under 3000, every `Seat` inside `Bounds`, `Exit` outside the body on the left.
3. **Path and constants** (Gary): `shared/bus.h`, constants in `shared/balance.h`, `Anim::BusRide/Freefall/Glide/BusHorn` in
   `shared/anim.h`. Tests: the path for 1000 seeds on the Moonfall circle always starts and ends more than `kBusMargin` outside the circle,
   passes within 0.45 R of the centre, and crosses in 25 to 60 s; `BusPosAt` is clamped at both ends.
4. **Protocol 40** (Gary): `EvBusPath`, `Snapshot.aboardMask`, `PlayerNet.kAboard`. Tests: round trip and the existing fuzz/decode tests in
   `server/tests/net_tests.cpp`. Merge order with PR #126 (protocol 39) agreed with Tee first.
5. **Server** (Gary): `Match` bus state, `LeaveBus`, `HandleInput` aboard rules, `Damage()` protection, Drop end rule, `HireAlly` gate.
   Update `SpawnProtection` (tests.cpp:147-159) and `session_tests.cpp:54`'s wall budget (Countdown 10 s + crossing up to 60 s + 25 s).
   New tests: `BusForcesEveryoneOffAtTheEnd`, `JumpIgnoredBeforeTheCoast`, `ProtectedUntilTwoSecondsAfterLanding`, `DropEndsWhenAllLanded`.
6. **Bots** (Gary): section 4.6. Rewrite `BotsSkydiveIn` (tests.cpp:4567-4600) as `BotsRideTheBusAndJump`: after Countdown every bot is at
   the bus position; by the end of the crossing none is aboard; jump times spread over at least 60% of the crossing; every bot lands on
   standable ground inside the map. Keep the rng draw order note from the bot-movement work in mind (an extra rng draw moved every later
   bot decision).
7. **Client bus actor and seats** (Gary, with Opus for the draw code): `BattleBus.inc` with the actor, mesh upload, LOD, sway, propeller,
   lanterns, smoke, banner cloth, pilot, `aboardMask` puppets, your own seat. Check: `python scripts/check_mod_layout.py`,
   `scripts/check_mod_compile.sh`, then an Android build and a solo-test match on Moonfall watching the bus from the seat and from the ground.
8. **Jump, free fall, glider pop** (Gary): the `UpdateSkydive` changes, the exit push, auto glide, anim reporting, `HangingFromGlider` gate.
   Check in game: jump early, jump late, get forced off, open the glider by hand and by height, land in the lake edge (safe landing pull still
   works), and the hearts cannot be hit until 2 s after landing.
9. **HUD** (Gary): prompts, top lines, minimap line and moon icon, glider aim in free fall, touch JUMP reuse. Check with button prompts on
   and off and in both control schemes.
10. **Sounds and horn** (Gary): section 3 sounds, the horn anim value, the cooldown. Check: two clients, honk heard on the other.
11. **Deku Leaf glider skin** (Opus): a second `MeshKind` for the glider, one big leaf with the handle bar at the stem (same grip
    points as `kGripInner/Outer` in `shared/glider_model.h`), the cloth sim on the blade, 128x128 leaf texture; a Graphics toggle
    picks leaf or striped glider. Optional.
12. **Lobby boarding polish** (Opus for the ramp/door and lift-off camera, Gary for the ready bit): section 5 layer 2. Optional; ship 1 to 10
    first.
13. **Docs and memory**: `docs/DESIGN.md` section 4.1 (the Drop is no longer "5 s of invulnerability"; describe the bus), `docs/PLAY.md`
    controls (A jump, A glider, Z dive, C-Right horn), `docs/BALANCE.md` (the new constants), `mod/Royale/FEATURES.md` row, and a team
    memory file `battle-bus`.

Validation that is possible without a device: steps 2 to 6 run under `ctest` locally and in CI; steps 7 to 10 compile under
`check_mod_compile.sh` and the Android build but are only proven by playing, as every mod feature so far.

## 8. Open choices made here (change them if Tee disagrees)

- The balloon is the Majora's Mask Moon itself, not a painted balloon (Tee's reference).
- The body is a dark red lacquered coach with Sheikah runes, after the Master Cycle Zero, not a blue school bus.
- The pilot is a Goron (model exists). Alternative: the Happy Mask Salesman, which needs a new model.
- The glider opens automatically at 600 units up, so nobody splats. Fortnite does the same.
- No new `MatchState`: the bus sits inside Countdown and Drop, which keeps every `MatchState` switch in the mod untouched.
- Jumped players vanish from the minimap, as in Fortnite, so nobody can read where the crowd lands.
- The horn is C-Right, the emote button, because the emote wheel is off while aboard anyway.
