# ChuChu mini bosses

Five original Blender models inspired by the supplied Wind Waker reference join the mini boss pool. A seeded one-in-three draw at each guard spot selects one of the five ChuChus, retaining the original guards and major bosses. All five can also be spawned individually from the existing Sandbox boss menu. Existing boss IDs remain stable; protocol 29 requires all peers to update together.

| Variant | Signature behavior | Battle royale elemental adaptation | Counter |
|---|---|---|---|
| Red | Jelly lunge and squash landing | Fire landing; burning melee | Dodge the marked landing, attack between lunges |
| Green | Flatten into a puddle, hide and reappear beneath the target | Forest spore burst | Wait for emergence; hidden jelly cannot be hit |
| Yellow | Jumping jelly with a cycling electric defense | Telegraph an electric pulse; brief shock stun | Ranged hit discharges it for 2.5 seconds; melee during charge shocks the attacker |
| Blue | Grounded electric jelly; does not use an attack leap | Electric pulse and a marked water geyser | Ranged hit discharges it; dodge the geyser |
| Dark | Shadow puddle and emergence | Shadow burst briefly holds its victims | Light Arrows petrify it for 5 seconds; hammers deal double damage while vulnerable |

Dark jelly resists ordinary hits to 15% damage rather than requiring a rare item to finish every encounter. Fire, spores, geysers and the larger pulses are adaptations for this game, rather than claims about Wind Waker's original move set. Wind Waker behavior references: [Prima enemy guide](https://primagames.com/eguides/the-legend-of-zelda-the-wind-waker-eguide/characters-and-enemies/enemies) and [Zelda Dungeon enemy reference](https://www.zeldadungeon.net/wiki/The_Wind_Waker_Enemies).

## Art and animation

`assets/chuchu/chuchu.blend` contains all five textured variants, shape keys and marked animation segments: Idle, Wobble, Lunge, Puddle, Emerge, Discharge, Hurt and Petrify. The dark variant has a broader, crouched silhouette. Each mesh has 220 points and 436 triangles. Textures are 256×256 with softened eye/pupil edges. The game uses bilinear filtering and a full-image LoadTile upload supported by Shipwright's renderer, rather than a large LoadBlock upload that truncates its pixel count. RGBA5551 bytes are explicitly big endian.

Regenerate the Blender source, PNG textures, baked animation/mesh header and preview with:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -b --python tools/chuchu/build_chuchu.py
```

The generator is the reproducible authoring source. Its exporter reads the authored topology, UVs, shape keys and image pixels; game geometry is not a separate approximation. Runtime interpolates the baked deformations and draws them through the existing boss actor lifecycle. The server owns damage, timings, defenses and elemental statuses. Electric charge is carried in the boss snapshot's auxiliary state; effects cannot decide damage.

## Verification

`tools/maps/run_map_tests.cmd` compiles and runs current Convergence, gameplay and network tests with MSVC. Focused regressions cover electric melee rejection/projectile discharge, dark resistance/light petrification/hammer vulnerability, all five special attacks, Blue's grounded behavior, seeded pool inclusion, and snapshot round trips.

`tools/chuchu/check_renderer.cmd` compiles and exercises the actual custom draw function with the engine's display-list macros. Only world/matrix allocation and the vertex-upload wrapper are stubbed. It checks all variants and boss modes, generated vertex ranges, and full 256×256 tile upload commands. This is a CPU display-list test, not an in-game GPU or device test.

Remaining validation: full Android game compilation and an Odin sandbox playtest for scale, attack readability, controller feel, animation transitions and texture sampling. Local tests and Blender renders do not establish those results.
