#pragma once
#include <cstdint>

namespace royale {

// Coarse animation state sent in Input and Snapshot. The sender derives it from what the player is doing; every client
// plays its own copy of the matching Link animation, so no skeleton data crosses the network. Unknown values are drawn
// as Idle, so new states can be added later without breaking older clients.
enum class Anim : uint8_t { Idle = 0, Walk = 1, Run = 2, Attack = 3, Hurt = 4, Dead = 5, Emote1 = 6, Emote2 = 7, Emote3 = 8, Emote4 = 9, Emote5 = 10, Count };

// Emotes: a player stands still and does a gesture the others can see. They travel as the player's anim value like everything else.
constexpr int kEmoteCount = 5;
inline constexpr const char* kEmoteNames[kEmoteCount] = { "Wow!", "Admire your hands", "Look to the sky", "Admire your sword", "Chicken dance" };
constexpr int kChickenDanceEmote = 4; // a custom pose rather than one of the game's animations
constexpr float kChickenDanceSeconds = 8.0f;
constexpr bool IsEmote(uint8_t a) { return a >= static_cast<uint8_t>(Anim::Emote1) && a < static_cast<uint8_t>(Anim::Count); }
constexpr uint8_t EmoteAnim(int index) { return static_cast<uint8_t>(static_cast<int>(Anim::Emote1) + (index < 0 ? 0 : index >= kEmoteCount ? kEmoteCount - 1 : index)); }

} // namespace royale
