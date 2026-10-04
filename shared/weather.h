#pragma once
// Seasons and weather. A match is played in one season (the host picks it, or it is random) and the sky changes as the match goes on:
// every few minutes of match time a new spell of weather begins. Everything is worked out from the match seed, the map and the time, so
// the server, the bots and every client agree without any weather data being stored. The server uses it for gameplay (fog and sandstorms
// shorten bots' sight, thunderstorms throw lightning, rain puts out burning); clients use it for what you see.
#include <algorithm>
#include <cstdint>
#include "map.h"
#include "rng.h"

namespace royale {

enum class Season : uint8_t { Spring, Summer, Autumn, Winter };
constexpr int kSeasonCount = 4;
constexpr uint8_t kSeasonRandom = 4;   // a host option: pick one for me

enum class Sky : uint8_t { Clear, Rain, Thunder, Fog, Snow, Ash, Sandstorm };
constexpr int kSkyCount = 7;

inline const char* SeasonName(Season s) {
    static const char* n[] = { "Spring", "Summer", "Autumn", "Winter" };
    return n[static_cast<int>(s) & 3];
}
inline const char* SeasonBlurb(Season s) {
    static const char* n[] = { "Fresh grass and spring showers", "Long sunny days", "Falling leaves and misty mornings", "Frost, snow and short days" };
    return n[static_cast<int>(s) & 3];
}
inline const char* SkyName(Sky k) {
    static const char* n[] = { "Clear skies", "Rain", "Thunderstorm", "Fog", "Snow", "Ash storm", "Sandstorm" };
    return n[static_cast<int>(k) % kSkyCount];
}

// What the host chose for the next match.
struct WeatherOptions {
    uint8_t season = kSeasonRandom;   // 0-3, or kSeasonRandom
    uint8_t intensity = 60;           // 0 (no weather at all) to 100 (violent)
    uint8_t change = 50;              // 0 (long spells) to 100 (it keeps changing)
};

// What the sky is doing right now.
struct Weather {
    Season season = Season::Summer;
    Sky sky = Sky::Clear;
    uint8_t intensity = 0;            // 0-100
    float Strength() const { return intensity / 100.0f; }
};

inline float SpellSeconds(const WeatherOptions& o) { return 120.0f - 80.0f * (std::min<int>(o.change, 100) / 100.0f); }
inline int SpellIndex(const WeatherOptions& o, float matchSeconds) { return static_cast<int>((std::max)(0.0f, matchSeconds) / SpellSeconds(o)); }

// How likely each kind of weather is, per place and season (Clear, Rain, Thunder, Fog, Snow, Ash, Sandstorm).
inline void SkyWeights(int mapId, Season s, int out[kSkyCount]) {
    static const int field[4][kSkyCount]   = { {4, 5, 2, 2, 0, 0, 0}, {7, 1, 2, 0, 0, 0, 0}, {3, 3, 1, 4, 0, 0, 0}, {3, 0, 0, 2, 6, 0, 0} };
    static const int lake[4][kSkyCount]    = { {3, 5, 2, 3, 0, 0, 0}, {6, 2, 3, 1, 0, 0, 0}, {2, 3, 2, 5, 0, 0, 0}, {2, 0, 0, 3, 6, 0, 0} };
    static const int village[4][kSkyCount] = { {4, 4, 1, 3, 0, 0, 0}, {6, 2, 2, 1, 0, 0, 0}, {3, 2, 1, 5, 0, 0, 0}, {3, 0, 0, 2, 6, 0, 0} };
    static const int crater[4][kSkyCount]  = { {3, 0, 3, 0, 0, 6, 0}, {3, 0, 3, 0, 0, 6, 0}, {3, 0, 3, 1, 0, 5, 0}, {3, 0, 2, 1, 1, 5, 0} };   // no rain in the crater: ash
    static const int desert[4][kSkyCount]  = { {5, 0, 1, 0, 0, 0, 5}, {4, 0, 1, 0, 0, 0, 7}, {5, 1, 1, 0, 0, 0, 4}, {5, 0, 0, 2, 0, 0, 3} };
    const int (*t)[kSkyCount] = field;
    switch (ClampMap(mapId)) { case 1: t = lake; break; case 2: t = village; break; case 3: t = crater; break; case 4: t = desert; break; default: break; }
    for (int i = 0; i < kSkyCount; i++) out[i] = t[static_cast<int>(s) & 3][i];
}

// The season a match is played in.
inline Season PickSeason(const WeatherOptions& o, uint64_t seed) {
    if (o.season < kSeasonCount) return static_cast<Season>(o.season);
    return static_cast<Season>(Rng(seed ^ 0x736561736Full).Below(4));   // "seaso"
}

// The weather of one spell. The first spell of a match is always fair, so the drop is not blind.
inline Weather WeatherForSpell(const WeatherOptions& o, uint64_t seed, int mapId, int spell) {
    Weather w;
    w.season = PickSeason(o, seed);
    if (o.intensity == 0) return w;
    Rng rng(seed ^ (static_cast<uint64_t>(spell + 1) * 0x9E3779B97F4A7C15ull) ^ 0x7765617468ull);   // "weath"
    int wts[kSkyCount];
    SkyWeights(mapId, w.season, wts);
    if (spell == 0) { w.sky = Sky::Clear; return w; }
    int total = 0;
    for (int i = 0; i < kSkyCount; i++) total += wts[i];
    int roll = static_cast<int>(rng.Below(static_cast<uint32_t>((std::max)(1, total))));
    for (int i = 0; i < kSkyCount; i++) { if (roll < wts[i]) { w.sky = static_cast<Sky>(i); break; } roll -= wts[i]; }
    if (w.sky != Sky::Clear) {
        const float jitter = 0.7f + 0.6f * rng.Below(1000) / 1000.0f;
        w.intensity = static_cast<uint8_t>(std::clamp(static_cast<int>(o.intensity * jitter), 15, 100));
    }
    return w;
}

// Gameplay effects. All scale with the spell's strength, so a gentle shower hardly matters and a heavy fog really does hide you.
inline float SightMult(const Weather& w) {
    float base = 1.0f;
    switch (w.sky) { case Sky::Fog: base = 0.45f; break; case Sky::Sandstorm: base = 0.5f; break; case Sky::Snow: base = 0.7f; break;
                     case Sky::Ash: base = 0.7f; break; case Sky::Rain: base = 0.85f; break; case Sky::Thunder: base = 0.75f; break; default: break; }
    return 1.0f - (1.0f - base) * w.Strength();
}
inline float BurnMult(const Weather& w) {   // rain and snow put fires out faster, ash feeds them
    if (w.sky == Sky::Rain || w.sky == Sky::Thunder) return 1.0f - 0.55f * w.Strength();
    if (w.sky == Sky::Snow) return 1.0f - 0.35f * w.Strength();
    if (w.sky == Sky::Ash) return 1.0f + 0.3f * w.Strength();
    return 1.0f;
}
// Seconds between lightning bolts in a thunderstorm (0 = none).
inline float LightningEvery(const Weather& w) { return w.sky == Sky::Thunder ? 14.0f - 9.0f * w.Strength() : 0.0f; }
constexpr float kLightningRadius = 150.0f, kLightningDamage = 0.75f, kLightningWarning = 1.4f;

// How dark the sky is pushed (0 = none, 1 = night) by the weather, for the day and night lighting.
inline float SkyDarkness(const Weather& w) {
    float d = 0.0f;
    switch (w.sky) { case Sky::Rain: d = 0.4f; break; case Sky::Thunder: d = 0.7f; break; case Sky::Fog: d = 0.25f; break; case Sky::Snow: d = 0.2f; break;
                     case Sky::Ash: d = 0.55f; break; case Sky::Sandstorm: d = 0.35f; break; default: break; }
    return d * w.Strength();
}

} // namespace royale
