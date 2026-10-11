// ---- our own Link clips -------------------------------------------------------------------------------------------------------------
// Jump, landing and sprint poses we authored (shared/link_anims.h, made by tools/link_anims/build_link_anims.py): Fortnite's timing with
// Ocarina of Time's jump pose at the apex. They are laid over the pose the game (or a puppet's animation) already gave Link, after it has been
// posed for the frame and before he is drawn, with a short crossfade in and out.
//   Local player: PoseLocalLinkClips (LocalLink_Draw, Vehicles.inc); our jump starts the clip (StartLinkJumpClip, Terrain.inc).
//   Puppets: PosePuppetClips (Players.inc, after their animation steps).

bool gOurJump = false;   // in the air from our own jump: extra gravity (shared/balance.h kJumpExtraGravity), the jump clip

namespace link_clip {
namespace la = royale::link_anims;
constexpr float kFadeFrames = 3.0f;   // crossfade with the game's own pose, in game frames

// Blends a to b by t along the shortest way round (binary angles).
inline s16 LerpAngle(s16 a, s16 b, float t) { return static_cast<s16>(a + static_cast<s16>(b - a) * t); }

// Lays clip `id` at (fractional) frame `f` over the joint table with weight `w`. The root keeps its x/z (where he stands).
inline void Apply(Vec3s* j, int limbCount, la::ClipId id, float f, float w) {
    if (j == nullptr || w <= 0.0f) return;
    w = std::min(w, 1.0f);
    const la::Clip& c = la::kClips[id];
    if (c.loop) f = std::fmod(std::fmod(f, static_cast<float>(c.count)) + c.count, static_cast<float>(c.count));
    else f = std::clamp(f, 0.0f, static_cast<float>(c.count - 1));
    const int a = static_cast<int>(f);
    const int b = c.loop ? (a + 1) % c.count : std::min(a + 1, c.count - 1);
    const float t = f - a;
    const la::Frame& A = c.frames[a];
    const la::Frame& B = c.frames[b];
    const float rootY = A.rootY + (B.rootY - A.rootY) * t;
    j[0].y = static_cast<s16>(j[0].y + (rootY - j[0].y) * w);
    const int limbs = std::min(la::kLimbs, limbCount - 1);   // joint table: the root's position, then one rotation per limb
    for (int i = 0; i < limbs; i++) {
        Vec3s& out = j[1 + i];
        const s16 x = LerpAngle(A.rot[i][0], B.rot[i][0], t), y = LerpAngle(A.rot[i][1], B.rot[i][1], t), z = LerpAngle(A.rot[i][2], B.rot[i][2], t);
        out.x = LerpAngle(out.x, x, w);
        out.y = LerpAngle(out.y, y, w);
        out.z = LerpAngle(out.z, z, w);
    }
}

// Local player state, timed in seconds (drawing runs faster than the game).
double gJumpAt = -1.0, gLandAt = -1.0, gLastDraw = 0.0;
bool gInJump = false;
float gJumpW = 0.0f, gSprintW = 0.0f, gSprintPhase = 0.0f;
}  // namespace link_clip

void StartLinkJumpClip() { link_clip::gJumpAt = ImGui::GetTime(); link_clip::gInJump = true; link_clip::gLandAt = -1.0; }

// Is Link doing something of the game's own that our clips must not cover (a swing, a shield, a ledge, the water, a cutscene)?
bool LinkBusyForClips(const Player* p) {
    return p->meleeWeaponState != 0 || p->heldActor != nullptr ||
           (p->stateFlags1 & (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD | PLAYER_STATE1_HANGING_OFF_LEDGE |
                              PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_GETTING_ITEM));
}

// The local Link: jump (held on its last frame until he lands), the landing absorb, the sprint.
void PoseLocalLinkClips(Player* player, bool sprinting, bool skydiving) {
    using namespace link_clip;
    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - gLastDraw, 0.0, 0.1));
    gLastDraw = now;
    const float step = dt * royale::kTickHz / kFadeFrames;   // weight change per draw for a kFadeFrames crossfade
    const bool busy = LinkBusyForClips(player) || skydiving;
    const bool grounded = (player->actor.bgCheckFlags & 1) != 0;

    if (gInJump && (grounded || busy)) { gInJump = false; if (grounded && !busy) gLandAt = now; }
    if (gInJump && now - gJumpAt > 3.0) gInJump = false;
    float jumpF = 0.0f;
    la::ClipId jumpClip = la::kJumpClip;
    float target = 0.0f;
    if (gInJump) {
        jumpF = static_cast<float>(now - gJumpAt) * royale::kTickHz;
        target = 1.0f;
    } else if (gLandAt >= 0.0) {
        jumpClip = la::kLandClip;
        jumpF = static_cast<float>(now - gLandAt) * royale::kTickHz;
        const float len = static_cast<float>(la::kClips[la::kLandClip].count);
        const bool moving = std::fabs(player->linearVelocity) > 4.0f;
        const float out = moving ? 2.0f : len;   // running on: let the legs go back to the run sooner
        target = jumpF < out ? 1.0f : 0.0f;
        if (jumpF > len + kFadeFrames) gLandAt = -1.0;
    }
    if (busy) target = 0.0f;
    gJumpW = target > gJumpW ? std::min(target, gJumpW + step * 1.5f) : std::max(target, gJumpW - step);   // in a little faster than out

    const bool sprint = sprinting && grounded && !busy && !gInJump && std::fabs(player->linearVelocity) > 4.0f;
    gSprintW = sprint ? std::min(1.0f, gSprintW + step) : std::max(0.0f, gSprintW - step);
    if (gSprintW > 0.0f) {
        const float rate = std::clamp(std::fabs(player->linearVelocity) / (7.5f * royale::kSprintMult), 0.7f, 1.4f);   // feet keep up with the ground
        gSprintPhase += dt * royale::kTickHz * rate;
        Apply(player->skelAnime.jointTable, player->skelAnime.limbCount, la::kSprintClip, gSprintPhase, gSprintW * (1.0f - gJumpW));
    }
    if (gJumpW > 0.0f) Apply(player->skelAnime.jointTable, player->skelAnime.limbCount, jumpClip, jumpF, gJumpW);
}

// A puppet (another player or a bot): the jump clip then the landing over its Anim::Jump, and the sprint over Anim::Sprint.
// `frames` counts game frames since its animation began; `sprintW` and `phase` are kept per puppet.
void PosePuppetClips(Player* player, uint8_t anim, float frames, float speed, float& sprintW, float& phase) {
    using namespace link_clip;
    Vec3s* j = player->skelAnime.jointTable;
    const int limbs = player->skelAnime.limbCount;
    const float step = 1.0f / kFadeFrames;
    const bool busy = LinkBusyForClips(player);
    const bool sprint = anim == static_cast<uint8_t>(royale::Anim::Sprint) && !busy;
    sprintW = sprint ? std::min(1.0f, sprintW + step) : std::max(0.0f, sprintW - step);
    if (sprintW > 0.0f) {
        phase += std::clamp(speed / (7.5f * royale::kSprintMult), 0.7f, 1.4f);
        Apply(j, limbs, la::kSprintClip, phase, sprintW);
    }
    if (anim != static_cast<uint8_t>(royale::Anim::Jump) || busy) return;
    const float jumpLen = static_cast<float>(la::kClips[la::kJumpClip].count), landLen = static_cast<float>(la::kClips[la::kLandClip].count);
    const float w = std::min(1.0f, (frames + 1.0f) / 2.0f) * std::clamp((jumpLen + landLen + kFadeFrames - frames) / kFadeFrames, 0.0f, 1.0f);
    if (frames < jumpLen) Apply(j, limbs, la::kJumpClip, frames, w);
    else Apply(j, limbs, la::kLandClip, frames - jumpLen, w);
}
