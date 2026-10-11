// Layer 2b (scripts/test_mod_functions.py): LinkAnims.inc's Apply, which lays our jump/land/sprint clip over the game's pose.
// EXTRACT mod/Royale/features/LinkAnims.inc  LerpAngle Apply
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
using s16 = int16_t;
struct Vec3s { s16 x, y, z; };
#include "shared/link_anims.h"
namespace link_clip {
namespace la = royale::link_anims;
// @EXTRACTED
}
using namespace link_clip;
constexpr int kLimbCount = 22;   // Link's skeleton: the root's position, then 21 limbs
static int AngleGap(s16 a, s16 b) { return std::abs(static_cast<int>(static_cast<s16>(a - b))); }
static void Pose(Vec3s* j) { for (int i = 0; i < kLimbCount; i++) j[i] = {static_cast<s16>(i * 997), static_cast<s16>(-i * 1531), static_cast<s16>(i * 211 + 7)}; }
int main() {
    const int limbs = std::min(la::kLimbs, kLimbCount - 1);
    for (la::ClipId id : {la::kJumpClip, la::kLandClip, la::kSprintClip}) {
        const la::Clip& c = la::kClips[id];
        assert(c.count > 0);
        Vec3s given[kLimbCount], j[kLimbCount];
        // Blend 0: the game's pose is left exactly as it was.
        Pose(given); Pose(j);
        Apply(j, kLimbCount, id, 0.0f, 0.0f);
        for (int i = 0; i < kLimbCount; i++) assert(j[i].x == given[i].x && j[i].y == given[i].y && j[i].z == given[i].z);
        // Blend 1 on a whole frame: the clip's frame, and the root keeps where he stands (x, z).
        const int f = c.count / 2;
        Apply(j, kLimbCount, id, static_cast<float>(f), 1.0f);
        assert(j[0].x == given[0].x && j[0].z == given[0].z && std::abs(j[0].y - static_cast<s16>(c.frames[f].rootY)) <= 1);
        for (int i = 0; i < limbs; i++)
            for (int k = 0; k < 3; k++) assert(AngleGap((&j[1 + i].x)[k], c.frames[f].rot[i][k]) <= 1);
        // Half way: every angle lies between the game's and the clip's (the short way round).
        Pose(j);
        Apply(j, kLimbCount, id, static_cast<float>(f), 0.5f);
        for (int i = 0; i < limbs; i++)
            for (int k = 0; k < 3; k++) {
                const s16 a = (&given[1 + i].x)[k], b = c.frames[f].rot[i][k], m = (&j[1 + i].x)[k];
                assert(AngleGap(a, m) + AngleGap(m, b) <= AngleGap(a, b) + 2);
            }
        // Frames past the end are held (or wrap for a looping clip); a null table or a negative weight does nothing.
        Pose(j); Apply(j, kLimbCount, id, static_cast<float>(c.count + 5), 1.0f);
        Apply(nullptr, kLimbCount, id, 0.0f, 1.0f);
        Pose(j); Apply(j, kLimbCount, id, 1.0f, -1.0f);
        for (int i = 0; i < kLimbCount; i++) assert(j[i].x == given[i].x);
    }
    return 0;
}
