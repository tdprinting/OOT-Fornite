#pragma once
#include <cstdint>

namespace royale {

// Character skins: the colour of Link's tunic, picked in the menu, shown to everyone else in the match. A preset is a name and a colour;
// "Custom" is any colour the player chooses. Bots get a preset by id so a crowd of them isn't all one colour.
struct Skin {
    const char* name;
    uint8_t r, g, b;
};

constexpr Skin kSkins[] = {
    {"Hero of Time", 30, 105, 27},       // the game's own green
    {"Hero of the Winds", 112, 204, 44}, // bright, toon-bright green
    {"Goron Red", 196, 52, 32},
    {"Zora Blue", 34, 116, 206},
    {"Gerudo Gold", 228, 182, 44},
    {"Shadow", 52, 44, 72},
    {"Sheikah Crimson", 150, 28, 62},
    {"Moblin Magenta", 204, 64, 176},
    {"Snowhead Frost", 176, 224, 242},
    {"Midnight", 24, 34, 96},
};
constexpr int kSkinCount = sizeof(kSkins) / sizeof(kSkins[0]);
constexpr int kCustomSkin = kSkinCount; // the menu's "Custom" entry

constexpr uint32_t PackRgb(uint8_t r, uint8_t g, uint8_t b) { return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b; }
constexpr uint32_t SkinRgb(int index) { return index >= 0 && index < kSkinCount ? PackRgb(kSkins[index].r, kSkins[index].g, kSkins[index].b) : PackRgb(30, 105, 27); }
constexpr uint8_t RgbR(uint32_t c) { return static_cast<uint8_t>(c >> 16); }
constexpr uint8_t RgbG(uint32_t c) { return static_cast<uint8_t>(c >> 8); }
constexpr uint8_t RgbB(uint32_t c) { return static_cast<uint8_t>(c); }

// Bots wear a preset chosen from their id.
constexpr uint32_t BotTunic(uint32_t id) { return SkinRgb(static_cast<int>((id * 2654435761u >> 16) % kSkinCount)); }

} // namespace royale
