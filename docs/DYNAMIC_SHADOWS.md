# Dynamic shadows and lights

Graphics page, "Shadows and lights" (Debug switch `Shadows`). Code: `RoyaleMod.cpp`, section "dynamic lights and shadows"; maths in
`shared/dynamic_shadows.h`, tested by `server/tests/shadow_tests.cpp`.

## Shadows

This feature uses inexpensive capsule shadows instead of rendering the scene again from the light. Every object
that casts a shadow is described by a few capsules, the way many modern games do character shadows:

- Link (yours, other players, bots and fallen bodies): 15 capsules built from the joints the game records each time it draws a Link
  (`Player::bodyPartsPos`): legs, arms, hips, body, neck, head, feet and the tip of the cap. The shadow swings with every step and sword swing.
- Allies and pets, mini bosses: an upright capsule. Major bosses: a big body round their middle wherever they fly.
- Carts: three capsules along the cart. Boulders, rocks and standing stones: one each. Chests: a box; loot: a small ball.

Each frame those capsules are projected along the light onto the ground under the object and painted into a small greyscale map (16, 32 or 64
pixels). Overlaps keep the darkest value, so an arm over the body never makes a darker blotch. Edges soften with height above the ground (a
foot gives a sharp edge, a raised hand a soft one), and tall things cast lighter shadows. The map is laid on actual collision triangles found by bounded floor probes, in a deep cool indigo rather than black, so the grass still
reads under it, Ocarina of Time style.

The light: the sun the sky draws (`SkyLightNow`, same direction as the sky's sun), so shadows turn and stretch through the match; low suns are
lifted to a minimum height so evening shadows stay a sensible length; they fade at sunrise and sunset, come back fainter from the moon, and wash
out under overcast weather. Medium and up also check toward the sun: under a roof or a bridge there is no sun shadow. With Medium and up a
nearby dynamic light (an explosion, Din's Fire) gives a second shadow pointing away from it.

Objects with a dynamic shadow lose the game's own round shadow; everything else keeps it. Still objects (boulders, chests) keep their map until
the sun moves.

## Lights

Short-lived point lights go into the game's own light system, which lights Link, enemies and everything else the game lights: explosions
(bombs, bombchus, exploding shots, cart wrecks), Din's Fire, Farore's Wind, Nayru's Love, fire, ice and light arrows, the spin attack's charge,
fire and lightning among the game's particles, boss strikes and lightning bolts, and chests being opened (in the find's rarity colour). The
brightest and closest win the few slots the game has (each character can take 7 lights and the scene's sun and sky use 2), so at most 5 are
used. The game's light system does not reach the scenery, so each light also puts a soft glow on the ground under it.

On Hyrule Kingdom, the lit stone, wood and roofs follow the sun of the moment (warm at sunrise and sunset, the moon at night) instead of the
fixed late-morning sun they were baked with.

## Quality

| Preset | Map | Shadows at once | Distance | Ground mesh | Light shadows | Roof check | Lights |
|---|---|---|---|---|---|---|---|
| Low (default, for handhelds such as the Odin) | 32 px | 4 | 1000 | 3 x 3 | none | no | 2 |
| Medium | 32 px | 8 | 1600 | 4 x 4 | 1 | yes | 3 |
| High | 64 px | 12 | 2200 | 6 x 6 | 1 | yes | 4 |
| Ultra | 64 px | 20 | 3000 | 8 x 8 | 2 | yes | 5 |

Your own Link always gets a shadow first, then the nearest. "Fine-tune the quality" exposes each number. Also: darkness, soft edges, which kinds
of object cast, dynamic lights on or off, and the Kingdom sun.

Default kinds: Link, other players and bots, allies and pets, bosses, carts, rocks, boulders, standing stones, chests and loot are on. Existing saved kind switches retain their values. All kinds share the same map budget; your Link remains first.

## What to check on a device

Never run in game yet (compiles with gcc and clang against the fork; the maths is unit tested). On the device: stand in the open at noon and in
the evening and check the shadow sits under Link's feet and points away from the sun; run and jump (the shadow follows and lightens);
stand under a Kingdom roof on Medium (no sun shadow); throw a bomb at night (Link lit orange, a glow on the ground, a second shadow on Medium);
"Graphics memory (layers)" should show the "Shadows and light" layer with nothing left out; and compare frame rate on Low and Off.

## Current receiver and smoothing upgrade

Raster edges have a minimum pixel coverage filter independent of physical softness. Zero physical softness remains finite and antialiased; low-sun bounds include the stretched penumbra. Changing softness repaints cached stationary maps.

Shadows now discover, deduplicate and clip actual collision triangles in projected texture space. Dynamic collision vertices come from `CollisionPoly_GetVerticesByBgId`, which reads the engine's transformed dynamic vertex list. Independent triangles cannot create artificial faces over missing floors, steps or gaps. Lower terrain remains visible beyond the previous 62-unit ledge fade, with a distance fade and a 420-unit descent limit. Geometry above the caster's reference floor plus three units is clipped conservatively, since the single capsule map does not provide per-capsule depth masking.

Receiver work is capped at 256 floor probes and 512 emitted triangles globally per frame, and 48 triangles per map. Low remains 32 pixels and four maps. If no usable receiver geometry is emitted, the actor keeps its native round shadow. Light pools retain the cached lattice mesh.

Limitations: probes can miss small triangles between sample points. Each probe finds only its highest eligible floor, so stacked floors are not exhaustively discovered. Elevated receivers above the caster base remain unsupported; this is not a complete scene shadow map or wall-shadow system. Coverage, triangle joins, moving-platform alignment and performance still need device verification. Unit tests cover raster filtering, low-sun padding, projected clipping, rejected elevated/missing floors, independent step planes and fixed budget values.
