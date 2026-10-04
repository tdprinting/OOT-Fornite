#pragma once
#include "storm.h"

namespace royale {

// Hyrule Field (SCENE_SPOT00, scene 0x51) is the v1 map.
//
// PLACEHOLDER VALUES: the real extent of the playable field has not been measured. Before playtesting, walk Link to the
// edges with the debug HUD ("Show Link position" in the Royale window) and set the centre and radius here. Everything
// else (storm circles, loot placement, spawn spread, bot movement) scales from this one circle.
constexpr int kHyruleFieldScene = 0x51;
// The waiting room is the Temple of Time (SCENE_TEMPLE_OF_TIME). Players wait there for the host to start; the match itself
// is always played in Hyrule Field. RoyaleMod.cpp static_asserts that this matches the engine's scene id.
constexpr int kWaitingRoomScene = 0x43;
constexpr Circle kHyruleFieldMap = {{0.0f, 0.0f}, 4000.0f};

} // namespace royale
