#pragma once
#include <cstdint>

namespace royale {

// Coarse animation state sent in Input and Snapshot. The sender derives it from what the player is doing; every client
// plays its own copy of the matching Link animation, so no skeleton data crosses the network. Unknown values are drawn
// as Idle, so new states can be added later without breaking older clients.
enum class Anim : uint8_t { Idle = 0, Walk = 1, Run = 2, Attack = 3, Hurt = 4, Dead = 5, Emote1 = 6, Emote2 = 7, Emote3 = 8, Emote4 = 9, Emote5 = 10,
                        Roll = 11, SideL = 12, SideR = 13, Back = 14, Stance = 15,
                        Shoot = 16, Throw = 17, Drink = 18, Play = 19, Cast = 20,   // the item-use poses: loose an arrow or seed, throw a bomb, drink a potion, play an ocarina song, cast a spell
                        // Roll: a dodge roll; SideL/SideR/Back/Stance: the lock-on (Z-target) footwork
                        JumpSlash = 21,   // the leaping overhead strike (A while Z-targeting)
                        SpinAttack = 22,  // the spin attack (hold and release B)
                        Guard = 23,       // shield up (R)
                        HopL = 24,        // the Z-target side hops and back flip: the game's other dodges
                        HopR = 25,
                        Backflip = 26,
                        ItemGet = 27,     // holds a new find up over his head
                        OpenChest = 28,   // kicks a chest open
                        Jump = 29,        // a jump (C-Up)
                        Count };

// The moves that dodge like a roll does: the server gives them the same moment of safety.
constexpr bool IsDodge(uint8_t a) {
    return a == static_cast<uint8_t>(Anim::Roll) || a == static_cast<uint8_t>(Anim::HopL) || a == static_cast<uint8_t>(Anim::HopR) ||
           a == static_cast<uint8_t>(Anim::Backflip);
}
// The sword moves: a swing of any kind.
constexpr bool IsStrike(uint8_t a) {
    return a == static_cast<uint8_t>(Anim::Attack) || a == static_cast<uint8_t>(Anim::JumpSlash) || a == static_cast<uint8_t>(Anim::SpinAttack);
}

// Emotes: a player stands still and does a gesture the others can see. They travel as the player's anim value like everything else.
constexpr int kEmoteCount = 5;
inline constexpr const char* kEmoteNames[kEmoteCount] = { "Wow!", "Admire your hands", "Look to the sky", "Admire your sword", "Chicken dance" };
constexpr int kChickenDanceEmote = 4; // a custom pose rather than one of the game's animations
constexpr float kChickenDanceSeconds = 8.0f;
constexpr bool IsEmote(uint8_t a) { return a >= static_cast<uint8_t>(Anim::Emote1) && a <= static_cast<uint8_t>(Anim::Emote5); }
constexpr uint8_t EmoteAnim(int index) { return static_cast<uint8_t>(static_cast<int>(Anim::Emote1) + (index < 0 ? 0 : index >= kEmoteCount ? kEmoteCount - 1 : index)); }

} // namespace royale
