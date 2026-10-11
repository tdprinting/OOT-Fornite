# The regular game

Our water, sky, weather, shadows and companions in Ocarina of Time itself: a save played as the normal quest (not the Battle Royale quest) with no lobby joined. A match never reads any of this. Settings: War Table > Settings > **Regular game** (`Royale.Regular*` CVars, `Royale.HideGameWater`).

| Feature | What happens in the regular game | Where |
|---|---|---|
| Water | Our water on every water box of every scene: lakes, rivers, Zora's Domain and Fountain, fountains, wells, dungeon pools. Box picked nearest the player's height (stacked rooms); bed found from the surface down; indoor water is lit by the room (fog and ambient colours, no sky, clouds or sun). No swim current. | `Water.inc` `GameWaterSurfaceAt`, `WaterFloorAt`, `WaterIndoors` |
| Hide the game's water | Room water: each loaded room's XLU display lists are walked once; flat triangles lying on a water box's surface become no-ops (a TRI2 keeps its other half). Lists stay held (`shared_ptr`) and are restored when switched off. Actor water (Water Temple, wells, oasis, Bottom of the Well, Lake Hylia's plane) is not drawn. Also applies on match maps in OoT scenes. | `Water.inc` `UpdateGameWater` |
| Sky, fog, wind | Outdoors only (scene has a skybox and it is not disabled). | `Atmosphere.inc` |
| Weather | "Changes": three spells a day from the save's day count, time of day, place (`RegularWeatherPlace`) and season (turns every five days, starts in summer), using `WeatherForSpell`. Or one fixed kind. Outdoors only; the Haunted Wasteland keeps its own sandstorm. Drives the game's own rain, snow, lightning and fog as in matches. | `Atmosphere.inc` `DriveRegularWeather` |
| Shadows and lights, ground patches, grass | Shadows everywhere; ground patches and grass outdoors. No trees (ours are not solid there and would stand in roads). | `Lighting.inc`, `GroundWeather.inc` |
| Companions | The companion picked on the Character page (Lilo, Avriella, Maya) follows you everywhere; away during cutscenes. | `CompanionScene` in `Core.inc` |
| Allies | "Ally 1" and "Ally 2": any of Kokiri, Zora, Goron, Gerudo, Jones. Local follow AI (behind you, one each side; brought along when far, stuck or on another level), fight enemies near you with a real AT collider on the enemy (slingshot, arrow or hammer damage), Zora heals you, dance to your ocarina. Never hurt. | `Companions.inc` `ReconcileRegularAllies` |

Floors indoors: `FloorProbeTop` and `GroundY` cast from just above the player (or the thing) in scenes without a sky, so an upper floor is not taken for the ground. Our stand-in actors use `SpawnStandIn`: the rock (`En_Ishi`) where its object is loaded, else the invisible `En_It` (gameplay_keep, every scene).

Not verified in game yet (compile-checked only).
