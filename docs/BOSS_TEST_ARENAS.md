# Boss test arenas

Open the game's War Table menu, choose **Practice**, then **Mini Boss Arena** or **Main Boss Arena**. These are dedicated solo test maps, outside the ordinary battlefield list.

- **Mini Boss Arena**: flat, clear ground; choose any of the twelve mini bosses, including all five ChuChu variants.
- **Main Boss Arena**: a larger flat floor with a solid enclosing rim; choose Volvagia, Morpha, Phantom Ganon, Bongo Bongo or Twinrova.

Open **Practice Tools → Boss encounter**, select the boss with left/right, and choose **Start encounter**. There is one encounter at a time. **Reset fight** respawns the current boss, clears its pending attacks and drops, refills your health/resources, revives you if necessary, and returns you to the spawn pad. **Clear arena** performs the same cleanup without spawning a boss. Both maps include an armory with every item and the existing equipment, invulnerability, healing, and world controls. God mode starts enabled; turn it off in Equipment to test incoming damage. The storm waits and the solo session has no countdown or automatic ending.

Map IDs 9 and 10 are appended, preserving the existing map IDs. Protocol 30 requires players to update together. Arena place names use a compact three-name block so all network place indices remain within one byte. Server placement, rendered ground and collision use the same terrain grid. The mini arena is flat within its 3,200-unit playable radius; the main arena has a 4,200-unit playable radius. Their outer rims are part of the solid terrain.

Validation: `royale_boss_arena_tests` checks map access, flat/dry terrain, the enclosing rim, empty starting encounters, armory placement, every permitted boss, category restrictions, replacement/reset/revival/status cleanup and paused-storm behavior. It runs with the normal CMake/CTest suites. In-game controller feel and handheld rendering/performance still require a playtest after the Android build is installed.
