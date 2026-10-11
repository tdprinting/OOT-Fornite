# Water: the open-sea look

The water is drawn by `mod/Royale/features/Water.inc` with the shared code in `shared/water_sim.h` (swell, ripples, textures) and
`shared/water_look.h` (`ShadeWater`, the colour of one point). `tools/water/preview.cpp` renders the same code off-line:

    g++ -O2 -std=c++17 -Ishared tools/water/preview.cpp -o /tmp/water_preview && /tmp/water_preview out_dir [raw|parts]

`parts` renders each open-sea feature on its own for tuning. Tests: `server/tests/water_tests.cpp`.

## What the sea is made of

Reference: a Sea of Thieves ocean (layered rolling swells, whitecaps and long foam streaks, bright sparse glints, teal to deep blue,
a hazy horizon), kept in the Ocarina of Time / stylised Zelda look: soft and painterly, never photoreal.

| Pass | Texture | What it adds | Detail setting |
| --- | --- | --- | --- |
| sheet | none (vertex colour) | depth colour, Fresnel sky, sun path, crest glow, **deep troughs and richer colour** (`depthTone`), **horizon haze** (`haze`) | all |
| swell bands | `MakeSwellBands`, 32-frame loop, 14 units a texel, turned along the wind | lit faces over darker hollows: the rolling, layered shape between the grid's points | High (2) |
| sparkle | `MakeSparkle`, 32-frame loop, 3 units a texel | sparse soft specks that twinkle, gathered on the sun's and moon's glitter path (replaces the even glint net) | all |
| foam | `MakeFoam` | shoreline foam, wakes, whitecaps | all |
| streaks | `MakeStreaks`, 3.2 units a texel, turned along the wind | the wind's long foam wisps on the backs of the swell and the whitecaps, in drifts | Medium (1) and up |

The band and streak passes draw the coarse grid only (the fine squares round the player sit on top of their coarse square). Each
texture is 64 x 64 I8, made once on a thread at start-up with the others. Pass order: bands, sheet, sparkle, foam, streaks.

## Graphics page (Battle Royale > Graphics > Water)

| Option | CVar | Default |
| --- | --- | --- |
| Sparse sparkle | `Royale.WaterSparkle` | on |
| Foam streaks | `Royale.WaterStreaks` | on (needs Water detail 1+) |
| Rolling swell shading | `Royale.WaterBands` | on (needs Water detail 2) |
| Horizon haze | `Royale.WaterHaze` | 100 % |
| Deep water tone | `Royale.WaterTone` | 100 % |

Low (detail 0) keeps the sheet, sparkle and foam only: no extra passes over the old look.

## Pictures

`opensea-before-after.png`: the preview out at sea in a fresh wind, before (top) and after (bottom). `open-sea-textures.png`: the
glint net, the sparkle, the streaks, the swell bands and the foam (four frames each, tiled 2 x 2). The preview draws the swell as
shading on a flat plane, so the geometry's own roll does not show in it; the device look is unverified.
