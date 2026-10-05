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
                        Sprint = 21,      // running with the left stick clicked
                        JumpSlash = 22,   // the leaping overhead strike (A while Z-targeting)
                        SpinAttack = 23,  // the spin attack (hold and release B)
                        Guard = 24,       // shield up (R)
                        HopL = 25,        // the Z-target side hops and back flip: the game's other dodges
                        HopR = 26,
                        Backflip = 27,
                        ItemGet = 28,     // holds a new find up over his head
                        OpenChest = 29,   // kicks a chest open
                        Jump = 30,        // a jump (C-Up)
                        Emote6 = 31, Emote7 = 32, Emote8 = 33,   // the emote wheel's newer gestures (older clients draw them as Idle)
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
// They are picked on the emote wheel (hold C-Right), so the order is the wheel's, clockwise from the top.
constexpr int kEmoteCount = 8;
inline constexpr const char* kEmoteNames[kEmoteCount] = { "Wow!", "Admire your hands", "Look to the sky", "Admire your sword", "Chicken dance",
                                                          "Victory!", "I give up", "Look around" };
constexpr int kChickenDanceEmote = 4; // a custom pose rather than one of the game's animations
constexpr float kChickenDanceSeconds = 8.0f;
constexpr bool IsEmote(uint8_t a) {
    return (a >= static_cast<uint8_t>(Anim::Emote1) && a <= static_cast<uint8_t>(Anim::Emote5)) ||
           (a >= static_cast<uint8_t>(Anim::Emote6) && a <= static_cast<uint8_t>(Anim::Emote8));
}
// The first five emotes sit together after Dead; the later ones were added at the end of the list so older builds still understand everything else.
constexpr uint8_t EmoteAnim(int index) {
    const int i = index < 0 ? 0 : index >= kEmoteCount ? kEmoteCount - 1 : index;
    return static_cast<uint8_t>(i < 5 ? static_cast<int>(Anim::Emote1) + i : static_cast<int>(Anim::Emote6) + (i - 5));
}
// Which emote an anim value is (the inverse of EmoteAnim), or -1.
constexpr int EmoteIndex(uint8_t a) {
    if (a >= static_cast<uint8_t>(Anim::Emote1) && a <= static_cast<uint8_t>(Anim::Emote5)) return a - static_cast<uint8_t>(Anim::Emote1);
    if (a >= static_cast<uint8_t>(Anim::Emote6) && a <= static_cast<uint8_t>(Anim::Emote8)) return 5 + (a - static_cast<uint8_t>(Anim::Emote6));
    return -1;
}

} // namespace royale
