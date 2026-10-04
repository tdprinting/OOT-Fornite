#pragma once
#include <cstdint>

namespace royale {

// Coarse animation state sent in Input and Snapshot. The sender derives it from what the player is doing; every client
// plays its own copy of the matching Link animation, so no skeleton data crosses the network. Unknown values are drawn
// as Idle, so new states can be added later without breaking older clients.
enum class Anim : uint8_t { Idle = 0, Walk = 1, Run = 2, Attack = 3, Hurt = 4, Dead = 5, Count };

} // namespace royale
