#pragma once
#include "lobby_fish.h"
namespace royale::reef {
// Same local extents as the authored rim; scale and yaw are supplied by the dynamic actor.
inline constexpr Point kTankCollisionVertices[] = {
    {-174,0,-100},{174,0,-100},{174,191,-100},{-174,191,-100},
    {-174,0,100},{174,0,100},{174,191,100},{-174,191,100}};
inline constexpr uint16_t kTankCollisionTriangles[][3] = {
    {0,3,2},{0,2,1},{4,5,6},{4,6,7},{0,4,7},{0,7,3},
    {1,2,6},{1,6,5},{3,7,6},{3,6,2},{0,1,5},{0,5,4}};
} // namespace royale::reef
