# Feature index

`RoyaleMod.cpp` includes the files below in dependency order. These `.inc` files share the original translation unit and anonymous namespace; do not compile them independently or reorder them. This preserves existing state, initialization, macros, and linkage. Read the relevant file first; search callers and shared state only as needed.

| Feature | Implementation under `features/` |
|---|---|
| Session state, graphics/debug settings, travel, floor/platform helpers | `Core.inc` |
| Remote players, animations, sounds, spells, skins | `Players.inc` |
| Death ragdolls and lobby ragdoll test | `Ragdolls.inc` |
| Loot, projection, chests, model drawing; includes `RoyaleBosses.h` | `WorldObjects.inc` |
| Glider, foliage, ground weather and patches | `GroundWeather.inc` |
| Storm wall, sky, fog, wind, tornado | `Atmosphere.inc` |
| Flying items and projectiles | `Projectiles.inc` |
| HUD, weather controls, item icons, emote wheel | `Hud.inc` |
| Original OoT heart/shield/magic textures | `OotVitals.inc` |
| Winner's wearable crown | `VictoryCrown.inc` |
| Logo, banners, chest/boss/hit effects | `Effects.inc` |
| Death, spectator, results and end screen | `EndMatch.inc` |
| Local emotes/audio and Lon Lon Buggy | `Vehicles.inc` |
| Music folder, crash reports, instrument playback | `MusicDiagnostics.inc` |
| Angry villagers and solid scenery | `Scenery.inc` |
| Water simulation, textures, caustics and rendering | `Water.inc` |
| Custom terrain, structures, map rendering | `Terrain.inc` |
| Save files, lobby time, sandbox, alerts, dialogue | `Lobby.inc` |
| Maya integration | `Maya.inc` |
| Lilo model and toxic cloud | `LiloModel.inc` |
| Avriella model and behavior | `Avriella.inc` |
| Avriella toys; includes `RoyaleLobbyPets.h` | `AvriellaToys.inc` |
| Lilo behavior and hireable allies | `Companions.inc` |
| Link clothes, cloth, Gilded Sword | `Equipment.inc` |
| Map sign, aquarium include, pause inventory, update hooks | `SceneHooks.inc` |
| Legacy sidebar/play menu | `Menus.inc` |
| Android updater UI | `Updater.inc` |
| Dynamic lighting, shadows, graphics menu registration | `Lighting.inc` |
| Engine collision exports and native menu bridge | `EngineBridge.inc` |

Existing focused files: `RoyaleSession.h` (network lifecycle), `RoyaleBosses.h` (boss presentation), `RoyaleBokoblins.h` (miniboss helper model, targeting, animation and rock rendering), `RoyaleLobbyFish.h` (aquarium), `RoyaleLobbyPets.h` (lobby pets), `RoyaleWarTable.h` (native menu). Server rules: `server/match.h`; bots: `server/bot.h`; network: `shared/protocol.h` and `client/game_client.h`. Generated asset arrays are not authoring sources.

## Focused workflow

Native menu: start at `war_table/INDEX.md` for rendering, input, controls or save work.
Actual asset relief: `shared/asset_relief.h` and `patches/libultraship/0006-source-texture-relief.patch`.

1. Find the feature here. Search its file: `rg -n "symbol" mod/Royale/features/Water.inc`.
2. Read the function and immediate dependencies. Do not preload every feature, generated header, engine source, or historical design doc.
3. Edit the original files under `mod/Royale/`. `scripts/link_mod.sh` copies the entry point, headers, and feature directory into the engine.
4. Run `python scripts/check_mod_layout.py`. Water changes also use `python scripts/test_water_draw_buffers.py`; engine changes need `scripts/check_mod_compile.sh` or the Android game build. Source checks use `scripts/mod_source.py` to read the included implementation.

The split reduces the amount of code an agent must read; it does not reduce compilation work or prove gameplay correctness. A later move to independent translation units requires explicit interfaces and separate validation.

- Hyrule Riftlands map: `docs/HYRULE_RIFTLANDS.md`; authoring `tools/maps/riftlands/`; generated `shared/riftlands_*`; concept and prop specs `map-concepts/hyrule-riftlands/`.
