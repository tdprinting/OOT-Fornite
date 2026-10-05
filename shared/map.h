#pragma once
#include "boss.h"
#include "storm.h"

namespace royale {

// The places a match can be played. Each is one of the game's overworld scenes; the host picks one in the lobby. Everything else (storm,
// loot, spawn spread, bots) scales from the circle measured on that scene when the match starts. `fallback` is only the guess used in the
// lobby, before the host's game has been there to measure it.
//
// Each place has a theme, and its mini bosses and its one major boss (a flying dragon) are dressed for it, and so are the names of the
// points of interest: 24 silly rhyming names per place, the first being the big landmark in the middle.
enum class Theme : uint8_t { Meadow, Water, Shadow, Fire, Desert, Count };

constexpr int kNamesPerMap = 24;
inline const char* const kPoiNames[] = {
    // Hyrule Field
    "Hylian Billion Pavilion", "Deku Dew Zoo",         "Goron Groove Lagoon",      "Zora Snore Shore",
    "Gerudo Voodoo Rendezvous", "Kokiri Breezy Wheezy", "Skulltula Hullabaloo",    "Bombchu Kaboom Room",
    "Poe Show Shack",          "Octorok Rock Dock",     "Cucco Mucky Plucky",      "Navi Gravy Bay",
    "Ganon's Bacon Cabin",     "Moblin Cobblin' Wobblin'", "Tektite Tight Bite",   "Lon Lon Gone Wrong",
    "Great Wall Brawl Hall",   "Ravine Routine Scene",  "Plaza Raza Tazz",         "Hill Will Windmill Thrill",
    "Stonehenge Avenge Lounge", "Causeway Hooray Highway", "Cluck Cluck Pluck Luck", "Rancher Prancer Manor",
    // Lake Hylia
    "Splish Splash Laboratory", "Fishy Wishy Pond",     "Shoreline Dine-a-line",   "Zora Aurora Pier",
    "Dive Hive Dock",          "Gulp Pulp Cove",        "Drizzle Sizzle Bay",      "Bubble Trouble Reef",
    "Ripple Tripple Bridge",   "Kelp Help Hut",         "Octorok Sock Dock",       "Tadpole Hold-a-Pole",
    "Pelican Melon Bay",       "Dewdrop Flop Shop",     "Whirl Pearl Spiral",      "Mudskipper Zipper Strip",
    "Sunken Bunk Dunk Hunk",   "Piranha Banana Marina",  "Fin Win Inn Spin",          "Reef Beef Relief Chief",
    "Pier Fear Cheer Gear",    "Spout Scout Outpost",    "Wharf Dwarf Wharf",         "Cove Grove Stove",
    // Kakariko Village
    "Windmill Chill Hill",     "Graveyard Hard Yard",   "Redead Bed Shed",         "Skulltula House Grouse",
    "Bazaar Bizarre Bar",      "Potion Motion Shop",    "Cucco Lady Shady",        "Gossip Stone Moan Zone",
    "Well Spell Bell Cell",    "Dampe's Camps Ramps",   "Shadow Meadow Barrow",    "Poe Show Row",
    "Anju's Hunch Brunch",     "Archery Hearty Party",  "Lantern Pattern Lane",    "Spooky Pookie Crypt",
    "Chimney Whimsy Alley",    "Cellar Stellar Seller",  "Rooftop Hip Hop Shop",      "Lane Pain Terrain",
    "Gate Fate Estate",        "Barn Yarn Darn",         "Shed Dread Bread",          "Well Dwell Smell",
    // Death Mountain Crater
    "Cinder Tinder Crater",    "Goron Moron Lair",      "Ember Remember Ridge",    "Bolero Zero Slope",
    "Magma Dilemma Pit",       "Ash Stash Cache",       "Fire Choir Spire",        "Scorch Porch Perch",
    "Smoke Poke Stoke",        "Obsidian Lid-ian Rim",  "Flame Game Frame",        "Bomb Flower Power Tower",
    "Hot Spot Plot",           "Sizzle Fizzle Shack",   "Brimstone Prone Zone",    "Blaze Daze Haze",
    "Vent Tent Event",         "Cinder Winder Finder",   "Slag Brag Flag",            "Pyre Choir Liar",
    "Forge George Gorge",      "Char Star Bazaar",       "Furnace Purpose Nurse",     "Ledge Edge Pledge",
    // Desert Colossus
    "Spirit Merit Statue",     "Sand Land Stand",       "Dune Moon Lagoon",        "Mirage Garage Stage",
    "Oasis Basis Place",       "Gerudo Dude Mood Food", "Cactus Practice Patch",   "Quicksand Command Strand",
    "Haunted Daunted Dune",    "Sunbaked Naked Rock",   "Scarab Carb Cab",         "Vulture Culture Perch",
    "Dust Rust Trust",         "Pharaoh Narrow Arrow",  "Sphinx Winks Jinx",       "Camel Mammal Trail",
    "Dune Tune Prune",         "Mesa Pizza Visa",        "Wadi Shady Lady",           "Tomb Zoom Room",
    "Caravan Divan Pan",       "Sand Band Grand",        "Palm Calm Psalm",           "Ruin Doing Brewing",
    // Fortnite Map
    "Tilted Towers Hours",     "Pleasant Park Lark",    "Retail Row Show",         "Salty Springs Things",
    "Loot Lake Quake",         "Dusty Depot Slot",      "Greasy Grove Stove",      "Lonely Lodge Dodge",
    "Snobby Shores Doors",     "Shifty Shafts Crafts",  "Flush Factory Trick-tory", "Fatal Fields Yields",
    "Lucky Landing Standing",  "Haunted Hills Chills",  "Junk Junction Function",  "Moisty Mire Choir",
    "Anarchy Acres Makers",    "Wailing Woods Goods",   "Tomato Town Crown",       "Paradise Palms Calms",
    "Risky Reels Wheels",      "Lazy Links Drinks",     "Frosty Flights Heights",  "Sweaty Sands Bands",};

// The fallback circles are where the real floor is, measured from the ROM's own collision data with tools/rom-extractor.html (see docs/MAPS.md):
// the centre of the walkable ground and a radius that holds most of it. The host's game still measures the live scene when a match starts.
struct MapDef {
    const char* name;
    const char* blurb;
    int scene;            // the game's scene number (SCENE_*), checked against the engine in RoyaleMod.cpp
    Circle fallback;
    float maxRadius;      // the biggest arena the host's game will measure for this place (Hyrule Field is big enough for a much larger one)
    Theme theme;
    BossKind minis[2];    // the mini bosses that guard this place
    BossKind major;       // its dragon
};

constexpr MapDef kMaps[] = {
    {"Hyrule Field", "Wide green plains with a town in the middle and caves around the edge", 0x51, {{-1269.0f, 6635.0f}, 6600.0f}, 7400.0f, Theme::Meadow,
     {BossKind::Stone, BossKind::Moss}, BossKind::DragonForest},
    {"Lake Hylia", "Shores, docks and little islands: fights on the beaches and bridges", 0x57, {{-1072.0f, 5314.0f}, 3200.0f}, 5000.0f, Theme::Water,
     {BossKind::Tide, BossKind::Frost}, BossKind::DragonWater},
    {"Kakariko Village", "A tight village of rooftops and graves: close fights, lots of climbing", 0x52, {{50.0f, 276.0f}, 1900.0f}, 5000.0f, Theme::Shadow,
     {BossKind::Shade, BossKind::Stone}, BossKind::DragonShadow},
    {"Death Mountain Crater", "A hot crater rim: lava, ash and narrow ledges", 0x61, {{-136.0f, -5.0f}, 1900.0f}, 5000.0f, Theme::Fire,
     {BossKind::Lava, BossKind::Stone}, BossKind::DragonFire},
    {"Desert Colossus", "Open sand dunes around a giant statue: long sight lines", 0x5C, {{2648.0f, 85.0f}, 4200.0f}, 5200.0f, Theme::Desert,
     {BossKind::Dune, BossKind::Stone}, BossKind::DragonSand},
    // The Fortnite Map is played inside Hyrule Field's scene, with the scene's collision swapped for the island (shared/fortnite_map.h). Its circle is
    // the island's inland (scripts/make_fortnite_map.py prints the numbers); the host's game measures the live ground when a match starts.
    {"Fortnite Map", "A big island of hills, lakes and towns: drop in, loot up and be the last one standing", 0x51, {{89.0f, -609.0f}, 5600.0f}, 7400.0f, Theme::Meadow,
     {BossKind::Stone, BossKind::Moss}, BossKind::DragonForest},
};
// The signpost standing in the middle of every map (see RoyaleMod.cpp, the sign): what it says when you walk up to it.
inline constexpr const char* kMapSignText = "If you read this, I love My Wife Cynthia and my 2 daughters Maya and Avriela!";

// Maya, a Kokiri girl who stands somewhere on every map (see RoyaleMod.cpp, Maya): talk to her and she says this.
inline constexpr const char* kMayaName = "Maya";
inline constexpr const char* kMayaGreeting = "Hi Daddy I'm a Goo goo!";

// Lilo, a cat who may be sitting somewhere on the map (an option): talk to her and she says this, then there is an accident.
inline constexpr const char* kLiloName = "Lilo";
inline constexpr const char* kLiloLine = "meoooww I smell a fart nearby";
// What Lilo says when you talk to her while she follows you as a pet: one line at a time, in turn. All of these are said through the game's own
// text box (RoyaleMod.cpp, "talking").
inline constexpr int kLiloPetLineCount = 6;
inline constexpr const char* kLiloPetLines[kLiloPetLineCount] = {
    "Mrrrow! (Lilo bumps her head against your leg.)",
    "Mew? (She wants to know if you found any good chests.)",
    "Prrrrrrrr... (She purrs as loud as a Goron.)",
    "Mew mew! (She is ready for the next fight.)",
    "Mrrp. (She keeps one eye on the storm for you.)",
    "*sniff sniff* meoooww I smell a fart nearby",
};

constexpr int kMapCount = sizeof(kMaps) / sizeof(kMaps[0]);
constexpr int kFortniteMapIndex = 5;   // the Fortnite Map's place in kMaps (shared/fortnite_map.h has the same number, and a test checks them)
static_assert(kFortniteMapIndex < kMapCount, "the Fortnite Map is the sixth place");
static_assert(sizeof(kPoiNames) / sizeof(kPoiNames[0]) == kMapCount * kNamesPerMap, "16 point of interest names per map");
constexpr int kPoiNameTotal = kMapCount * kNamesPerMap;

constexpr int ClampMap(int id) { return id >= 0 && id < kMapCount ? id : 0; }
constexpr const MapDef& MapOf(int id) { return kMaps[ClampMap(id)]; }

// Hyrule Field is the default map and the one the older constants name.
constexpr int kHyruleFieldScene = 0x51;
// The waiting room is the Temple of Time (SCENE_TEMPLE_OF_TIME). Players wait there for the host to start; the match itself
// is always played on the chosen map. RoyaleMod.cpp static_asserts that this matches the engine's scene id.
constexpr int kWaitingRoomScene = 0x43;
constexpr Circle kHyruleFieldMap = kMaps[0].fallback;

} // namespace royale
