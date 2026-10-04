#pragma once
#include <cstdint>
#include <string>

namespace royale {

// Bots have names out of Hyrule instead of numbers, so the kill feed, nameplates and results read like a story. Bot ids start at 1000.
inline const char* const kBotNames[] = {
    "Mido", "Saria", "Fado", "Darunia", "Ruto", "Impa", "Talon", "Ingo", "Malon", "Kafei", "Anju", "Cojiro", "Dampe", "Grog", "Biggoron", "Nabooru",
    "Rauru", "Sheik", "Zelda", "Epona", "Navi", "Tael", "Tatl", "Skull Kid", "Beedle", "Tingle", "Linebeck", "Zant", "Midna", "Ilia", "Ordon Rusl", "Fi",
    "Groose", "Ghirahim", "Revali", "Urbosa", "Mipha", "Daruk", "Sidon", "Paya", "Purah", "Hestu", "Kass", "Kilton", "Vilia", "Zora Sage", "Goron Chief", "Deku Scrub",
};
constexpr int kBotNameCount = sizeof(kBotNames) / sizeof(kBotNames[0]);

inline std::string BotName(uint32_t id) {
    const uint32_t n = id >= 1000 ? id - 1000 : id;
    std::string name = kBotNames[n % kBotNameCount];
    if (n >= static_cast<uint32_t>(kBotNameCount)) name += " " + std::to_string(n / kBotNameCount + 1);
    return name;
}

} // namespace royale
