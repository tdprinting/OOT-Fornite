// ---- falling limp: what is left of a player who was eliminated -------------------------------------------------------------------

// A ragdoll: the body is thrown by the blow, tumbles through the air, bounces, then rolls over and over along the ground (and down any slope)
// with its limbs flopping loose, until it comes to rest on its back or its front. It stays there for a couple of minutes, and anyone who walks
// into it shoves it along. It is a puppet actor that runs its own physics instead of following the network.
constexpr uint16_t kCorpseIdBase = 0xF000;
constexpr uint16_t kAllyIdBase = 0xE000;   // puppet ids from here up to the corpses are hireable allies (index = id - base)
constexpr float kCorpseSeconds = 150.0f;   // how long a body lies there
constexpr size_t kMaxCorpses = 16;         // more than this and the oldest one goes
constexpr float kBodyRadius = 8.0f;        // half the thickness of Link lying down: how far the body's middle is off the ground
// The loose joints of a body. The first nine are the original limbs (kept as they were); the rest are the extra points: spine, neck, pelvis,
// wrists and ankles. `parent` is the joint this one hangs from: it is dragged along when the parent swings and whips past it. `side` is -1 on
// Link's left, +1 on his right and in the middle; `limit` is how far (radians) it can bend; `reach` scales the angle added to the animation;
// `droop` is how much gravity pulls it when the body lies on its side; `stiff` is how hard it springs back.
struct RagJoint { int limb; int parent; float side; float reach; float limit; float droop; float stiff; };
constexpr int kOriginalJoints = 9;
constexpr int kJoints = 17;
const RagJoint kRagJoints[kJoints] = {
    { PLAYER_LIMB_HEAD,        10,  1.0f, 0.50f, 1.2f, 0.0f, 28.0f },
    { PLAYER_LIMB_L_SHOULDER,   9, -1.0f, 1.00f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_SHOULDER,   9,  1.0f, 1.00f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_FOREARM,    1, -1.0f, 0.90f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_FOREARM,    2,  1.0f, 0.90f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_THIGH,     12, -1.0f, 0.60f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_THIGH,     12,  1.0f, 0.60f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_L_SHIN,       5, -1.0f, 0.70f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_R_SHIN,       6,  1.0f, 0.70f, 1.2f, 0.7f, 28.0f },
    { PLAYER_LIMB_UPPER,       11,  1.0f, 0.25f, 0.9f, 0.3f, 40.0f },   // chest
    { PLAYER_LIMB_COLLAR,       9,  1.0f, 0.30f, 0.9f, 0.3f, 34.0f },   // neck
    { PLAYER_LIMB_LOWER,       12,  1.0f, 0.20f, 0.8f, 0.2f, 44.0f },   // lower spine
    { PLAYER_LIMB_WAIST,       -1,  1.0f, 0.18f, 0.7f, 0.2f, 48.0f },   // pelvis
    { PLAYER_LIMB_L_HAND,       3, -1.0f, 0.80f, 1.2f, 0.7f, 22.0f },   // wrists
    { PLAYER_LIMB_R_HAND,       4,  1.0f, 0.80f, 1.2f, 0.7f, 22.0f },
    { PLAYER_LIMB_L_FOOT,       7, -1.0f, 0.60f, 1.0f, 0.7f, 22.0f },   // ankles
    { PLAYER_LIMB_R_FOOT,       8,  1.0f, 0.60f, 1.0f, 0.7f, 22.0f },
};
struct Corpse {
    Actor* actor = nullptr;
    royale::Vec2 vel = {};       // horizontal speed, units per second
    float vy = 0;                // vertical speed
    float spin = 0;              // radians per second around the vertical axis
    float roll = 0, rollVel = 0; // rolling over around the body's long axis (radians, radians per second)
    float age = 0;
    royale::ItemId weapon = royale::ItemId::DekuStick;
    uint32_t tunic = royale::SkinRgb(0);
    bool animStarted = false;
    float pitch = 0, pitchVel = 0;        // tumbling head over heels in the air (radians)
    royale::Vec2 lastVel = {};
    float lastVy = 0, lastRollVel = 0;
    float limb[kJoints][2] = {}, limbVel[kJoints][2] = {};   // loose joints: two swing angles each (radians)
    int bounces = 0;
    float still = 0;             // seconds it has lain still; a body at rest skips the ground checks
    bool dying = false;          // asked the game to remove it
    bool test = false;           // a lobby test ragdoll: never ages out, can be hit and grabbed
    bool held = false;           // being carried (the grab button is held)
    float hitCooldown = 0;
    bool pinned = false;         // an emote double: stands where the local player is and plays an emote, instead of falling
    int emote = 0;
    bool forward = false;        // thrown the way it faced: it goes down on its front instead of its back
    int frames = 0;              // updates so far (the death cry waits for the second, when the game knows where on screen the body is)
};
std::unordered_map<uint16_t, Corpse> gCorpses;       // corpse id -> body
std::unordered_map<const Actor*, uint16_t> gCorpseOf;
uint16_t gNextCorpse = kCorpseIdBase;
std::unordered_map<uint16_t, royale::PuppetState> gLastSeen; // the last state of each living puppet, to see who just died
std::unordered_map<uint16_t, double> gFellAt;                // player id -> when their body was made, so one elimination makes one body

void ForgetCorpse(const Actor* actor) {
    auto corpse = gCorpseOf.find(actor);
    if (corpse != gCorpseOf.end()) { gCorpses.erase(corpse->second); gCorpseOf.erase(corpse); }
}

float WrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

void Corpse_Update(Actor* actor, PlayState* play) {
    Feat("corpses: update");
    auto of = gCorpseOf.find(actor);
    if (of == gCorpseOf.end()) { Actor_Kill(actor); return; }
    Corpse& c = gCorpses[of->second];
    Player* player = reinterpret_cast<Player*>(actor);
    if (c.pinned) { // the emote double
        Player* local = GET_PLAYER(play);
        actor->world.pos = local->actor.world.pos;
        actor->shape.rot.y = local->actor.shape.rot.y;
        actor->world.rot.y = actor->shape.rot.y;
        actor->shape.shadowAlpha = 255;
        if (!c.animStarted) {
            LinkAnimation_PlayLoop(play, &player->skelAnime, AnimFor(royale::EmoteAnim(c.emote)));
            c.animStarted = true;
        }
        LinkAnimation_Update(play, &player->skelAnime);
        if (c.emote == royale::kChickenDanceEmote) {
            const float t = static_cast<float>(play->gameplayFrames) / royale::kTickHz;
            ApplyChickenDance(player, t);
            actor->world.pos.y += ChickenDanceBob(t);
        }
        Vec3f ignored;
        SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
        return;
    }
    const float dt = 1.0f / royale::kTickHz;
    if (!c.test) c.age += dt;
    if (c.age > kCorpseSeconds) { c.dying = true; Actor_Kill(actor); return; }
    if (++c.frames == 2 && !c.test) PuppetVoice(player, NA_SE_VO_LI_DOWN);   // Link's cry as he goes down

    // Walking into a body shoves it (and sets it rolling). Not while invisible: a spectator stands on their own body.
    {
        Player* local = GET_PLAYER(play);
        const float dx = actor->world.pos.x - local->actor.world.pos.x, dz = actor->world.pos.z - local->actor.world.pos.z;
        const float d = std::hypot(dx, dz);
        if (!(local->stateFlags2 & PLAYER_STATE2_DISABLE_DRAW) && d < 30.0f && d > 0.01f && std::fabs(actor->world.pos.y - local->actor.world.pos.y) < 40.0f &&
            local->actor.speedXZ > 1.0f) {
            const float push = (30.0f - d) * 6.0f + local->actor.speedXZ * 12.0f;
            c.vel.x += dx / d * push * dt * 8.0f;
            c.vel.z += dz / d * push * dt * 8.0f;
            c.still = 0;
        }
        if (c.test) {   // the lobby test ragdoll can also be hit with a sword and carried (hold L)
            Feat("ragdoll: test dummy");
            c.hitCooldown -= dt;
            if (local->meleeWeaponState != 0 && c.hitCooldown <= 0.0f && d < 75.0f && d > 0.01f) {
                c.vel.x += dx / d * 320.0f; c.vel.z += dz / d * 320.0f;
                c.vy = std::max(c.vy, 260.0f);
                c.pitchVel += -4.0f; c.rollVel += (dx > 0 ? 3.0f : -3.0f);
                c.hitCooldown = 0.4f; c.bounces = 0; c.still = 0;
                for (auto& l : c.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 8.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 8.0f; }
            }
            const bool grabbing = (gPlayState->state.input[0].cur.button & BTN_L) != 0 && d < (c.held ? 220.0f : 140.0f);
            c.held = grabbing;
            if (grabbing) {   // pulled to a spot in front of the player, hanging loose
                const float fy = local->actor.shape.rot.y * (3.14159265f / 32768.0f);
                const float tx = local->actor.world.pos.x + std::sin(fy) * 45.0f, tz = local->actor.world.pos.z + std::cos(fy) * 45.0f;
                const float ty = local->actor.world.pos.y + 35.0f;
                const float k = std::min(1.0f, 10.0f * dt);
                c.vel.x += ((tx - actor->world.pos.x) * 8.0f - c.vel.x) * k;
                c.vel.z += ((tz - actor->world.pos.z) * 8.0f - c.vel.z) * k;
                c.vy += ((ty - actor->world.pos.y) * 8.0f - c.vy) * k + 980.0f * dt;   // gravity is taken off again below
                c.pitchVel *= 0.9f; c.rollVel *= 0.9f; c.bounces = 0; c.still = 0;
            }
        }
    }

    const float yaw = actor->shape.rot.y * (3.14159265f / 32768.0f);
    const royale::Vec2 side = { std::cos(yaw), -std::sin(yaw) };   // the body's own left, which is the way it rolls
    const royale::Vec2 fwd = { std::sin(yaw), std::cos(yaw) };     // along the body, head to feet
    const bool resting = c.still > 2.0f;
    bool onGround = false;
    if (!resting) {
        // Gravity and a little air drag in the air; bounces that lose most of the energy; on the ground it rolls freely sideways
        // (rolling barely slows it) but drags along its length, and rolls down slopes.
        c.vy -= 980.0f * dt;
        c.vel.x *= 1.0f - 0.35f * dt; c.vel.z *= 1.0f - 0.35f * dt;
        actor->world.pos.x += c.vel.x * dt;
        actor->world.pos.z += c.vel.z * dt;
        actor->world.pos.y += c.vy * dt;
        const float ground = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y - 1.0f);
        const float lift = kBodyRadius * (1.0f - std::cos(c.roll));   // rolled onto its side or front, the middle of the body is higher
        if (actor->world.pos.y <= ground + lift) {
            actor->world.pos.y = ground + lift;
            onGround = true;
            if (c.vy < -120.0f && c.bounces < 4) {   // a bounce: it keeps a third of its height, the tumble changes, and the limbs fling
                if (c.bounces < 2) PuppetSfx(actor, NA_SE_PL_BODY_HIT);   // the thud of hitting the ground
                c.vy = -c.vy * 0.34f;
                c.bounces++;
                c.vel.x *= 0.72f; c.vel.z *= 0.72f;
                c.pitchVel *= -0.45f;
                const float kick = (c.vel.x * side.x + c.vel.z * side.z) >= 0.0f ? 1.0f : -1.0f;
                c.vel.x += side.x * kick * 90.0f; c.vel.z += side.z * kick * 90.0f;   // landing knocks it over sideways
                for (auto& l : c.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 9.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 9.0f; }
            } else {
                c.vy = 0;
                // Down the slope.
                const float gx = (GroundY(play, actor->world.pos.x + 10.0f, actor->world.pos.z, ground) - GroundY(play, actor->world.pos.x - 10.0f, actor->world.pos.z, ground)) / 20.0f;
                const float gz = (GroundY(play, actor->world.pos.x, actor->world.pos.z + 10.0f, ground) - GroundY(play, actor->world.pos.x, actor->world.pos.z - 10.0f, ground)) / 20.0f;
                if (std::hypot(gx, gz) > 0.08f && std::hypot(gx, gz) < 2.5f) {   // a real slope, not a wall or a ledge
                    c.vel.x -= gx * 520.0f * dt;
                    c.vel.z -= gz * 520.0f * dt;
                }
                float vs = c.vel.x * side.x + c.vel.z * side.z, vf = c.vel.x * fwd.x + c.vel.z * fwd.z;
                vs *= std::max(0.0f, 1.0f - (std::fabs(vs) > 40.0f ? 0.9f : 3.5f) * dt);   // rolling
                vf *= std::max(0.0f, 1.0f - (std::fabs(vf) > 60.0f ? 3.2f : 7.5f) * dt);   // sliding to a stop
                c.vel = { side.x * vs + fwd.x * vf, side.z * vs + fwd.z * vf };
                c.spin *= std::max(0.0f, 1.0f - 4.0f * dt);
                // Rolling without slipping: moving to its left turns it over that way.
                const float rolling = -vs / kBodyRadius;
                if (std::fabs(vs) > 12.0f) {
                    c.rollVel += (rolling - c.rollVel) * std::min(1.0f, 10.0f * dt);
                } else {   // nearly stopped: it flops down onto its back or its front, whichever is nearer
                    const float rest = std::round(c.roll / 3.14159265f) * 3.14159265f;
                    c.rollVel += (rest - c.roll) * 40.0f * dt;
                    c.rollVel *= std::max(0.0f, 1.0f - 6.0f * dt);
                }
            }
        }
        if (!onGround) c.rollVel *= 1.0f - 0.2f * dt;   // a sideways tumble keeps going through the air
        c.roll += c.rollVel * dt;
        if (c.roll > 6.2831853f || c.roll < -6.2831853f) c.roll = std::fmod(c.roll, 6.2831853f);

        // Head over heels while airborne; once down, the tumble eases out to lying flat.
        if (!onGround) { c.pitch += c.pitchVel * dt; }
        else { c.pitch = WrapAngle(c.pitch) * std::max(0.0f, 1.0f - 6.0f * dt); c.pitchVel *= std::max(0.0f, 1.0f - 6.0f * dt); }
        actor->shape.rot.y = static_cast<s16>(actor->shape.rot.y + static_cast<int>(c.spin * dt * (32768.0f / 3.14159265f)));
        actor->world.rot.y = actor->shape.rot.y;

        const bool moving = onGround ? (std::hypot(c.vel.x, c.vel.z) > 4.0f || std::fabs(c.rollVel) > 0.3f) : true;
        c.still = moving ? 0.0f : c.still + dt;
    }
    // The last few seconds it sinks out of sight.
    if (c.age > kCorpseSeconds - 3.0f) actor->world.pos.y -= 10.0f * dt;
    actor->shape.rot.x = static_cast<s16>(c.pitch * (32768.0f / 3.14159265f));
    actor->shape.rot.z = static_cast<s16>(c.roll * (32768.0f / 3.14159265f));
    actor->shape.shadowAlpha = 255;

    // Limp joints: they lag behind every change of speed and of roll (the blow, each bounce, each turn over), flop toward the ground when the body
    // lies on its side, and only weak springs pull them back. A joint is also dragged along by the one it hangs from, so a swinging shoulder whips
    // the forearm and the wrist after it. Without the Ragdoll debug switch only the original nine limbs move.
    const int joints = DebugOn(kDbgRagdoll) ? kJoints : kOriginalJoints;
    {
        Feat("ragdoll: joints");
        const float ax = (c.vel.x - c.lastVel.x) / dt, az = (c.vel.z - c.lastVel.z) / dt, ay = (c.vy - c.lastVy) / dt;
        const float ar = (c.rollVel - c.lastRollVel) / dt;
        c.lastVel = c.vel; c.lastVy = c.vy; c.lastRollVel = c.rollVel;
        const float kick = std::clamp((std::fabs(ax) + std::fabs(az) + std::fabs(ay) * 0.4f) * 0.0009f, 0.0f, 1.4f);
        const float droop = std::sin(c.roll) * 0.7f;   // gravity, sideways across the body
        float before[kJoints][2];                      // last frame's speeds, so the order of the joints does not matter
        for (int i = 0; i < joints; i++) { before[i][0] = c.limbVel[i][0]; before[i][1] = c.limbVel[i][1]; }
        for (int i = 0; i < joints; i++) {
            const RagJoint& jt = kRagJoints[i];
            const bool original = i < kOriginalJoints;
            for (int a = 0; a < 2; a++) {
                const float push = (a == 0 ? ay * 0.00032f : (ax * 0.0003f + az * 0.0003f - ar * 0.004f)) * jt.side + kick * (Rand_ZeroOne() - 0.5f) * 0.5f;
                c.limbVel[i][a] += push;
                if (!onGround && !resting) c.limbVel[i][a] += std::sin(c.age * (7.0f + i) + a) * 0.9f * dt * 20.0f;   // flailing through the air
                if (!original && jt.parent >= 0 && jt.parent < joints) c.limbVel[i][a] += before[jt.parent][a] * 5.0f * dt;
                const float target = a == 1 ? droop * (jt.droop / 0.7f) : 0.0f;
                c.limbVel[i][a] += (target - c.limb[i][a]) * jt.stiff * dt;
                c.limbVel[i][a] *= std::max(0.0f, 1.0f - 4.0f * dt);
                const float next = c.limb[i][a] + c.limbVel[i][a] * dt;
                if (std::fabs(next) > jt.limit) c.limbVel[i][a] *= -0.3f;   // hit the end of its range: it bounces back a little
                c.limb[i][a] = std::clamp(next, -jt.limit, jt.limit);
            }
        }
    }

    if (!c.animStarted) {
        // knocked flat: on the back, or on the front when the blow came from behind
        LinkAnimation_PlayOnce(play, &player->skelAnime, c.forward ? RA(normal_front_downA) : (LinkAnimationHeader*)&gPlayerAnim_link_normal_back_downA);
        c.animStarted = true;
    }
    LinkAnimation_Update(play, &player->skelAnime);
    {   // the loose joints, on top of the knocked-down pose
        Vec3s* j = player->skelAnime.jointTable;
        const float bin = 32768.0f / 3.14159265f;
        for (int i = 0; i < joints; i++) {
            const RagJoint& jt = kRagJoints[i];
            j[jt.limb].x = static_cast<s16>(j[jt.limb].x + c.limb[i][0] * jt.reach * bin);
            j[jt.limb].z = static_cast<s16>(j[jt.limb].z + c.limb[i][1] * jt.reach * bin);
        }
    }
    Vec3f ignored;
    SkelAnime_UpdateTranslation(&player->skelAnime, &ignored, actor->shape.rot.y);
}

void Corpse_Draw(Actor* actor, PlayState* play) {
    Feat("corpses: draw");
    auto of = gCorpseOf.find(actor);
    if (of == gCorpseOf.end()) return;
    const Corpse& c = gCorpses[of->second];
    const u8 original = gSaveContext.equips.buttonItems[0];
    gSaveContext.equips.buttonItems[0] = LookFor(c.weapon).buttonItem;
    if (gTunicApplied) SetTunicCosmetics(c.tunic);
    Player_Draw(actor, play);
    if (gTunicApplied) SetTunicCosmetics(gLocalTunic);
    gSaveContext.equips.buttonItems[0] = original;
}

// An emote: a copy of the local player's character stands where they are and plays the gesture, while the real one is hidden. Moving or attacking
// ends it. Returns the actor so the caller can remove it.
Actor* SpawnEmoteDouble(const royale::PuppetState& s, int emote) {
    if (gCorpses.size() >= kMaxCorpses + 2 || gPlayState == nullptr) return nullptr;
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return nullptr;
    Corpse c;
    c.actor = actor;
    c.pinned = true;
    c.emote = emote;
    c.weapon = s.weapon;
    c.tunic = s.tunic;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
    return actor;
}

// The body of player `s`, thrown along (pushX, pushZ). Once per elimination: the elimination event and the puppet list can both report it.
// Returns the body's id (0 if none was made).
uint16_t SpawnCorpse(const royale::PuppetState& s, float pushX, float pushZ) {
    if (gPlayState == nullptr) return 0;
    const double now = ImGui::GetTime();
    auto fell = gFellAt.find(s.id);
    if (fell != gFellAt.end() && now - fell->second < 10.0) return 0;
    gFellAt[s.id] = now;
    {   // room for one more: the oldest body goes
        size_t bodies = 0;
        Corpse* oldest = nullptr;
        for (auto& [cid, body] : gCorpses) {
            if (body.pinned || body.dying) continue;
            bodies++;
            if (oldest == nullptr || body.age > oldest->age) oldest = &body;
        }
        if (bodies >= kMaxCorpses && oldest != nullptr) { oldest->dying = true; Actor_Kill(oldest->actor); }
    }
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, s.x, s.y, s.z, 0, s.rot, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return 0;
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);   // a body can't be Z-targeted like a player
    Corpse c;
    c.actor = actor;
    const float len = std::max(0.001f, std::hypot(pushX, pushZ));
    const float yaw = s.rot * (3.14159265f / 32768.0f);
    const float sideways = (id & 1 ? 1.0f : -1.0f) * 110.0f;                   // and a little to one side, so it lands rolling
    c.vel = { pushX / len * 250.0f + std::cos(yaw) * sideways, pushZ / len * 250.0f - std::sin(yaw) * sideways };
    c.vy = 330.0f;
    c.pitchVel = -7.0f - (id % 3);                            // flips over backwards
    c.spin = (id & 1 ? 1.0f : -1.0f) * 2.0f;
    c.rollVel = -sideways / kBodyRadius * 0.5f;
    c.forward = pushX * std::sin(yaw) + pushZ * std::cos(yaw) > 0.0f;
    c.weapon = s.weapon;
    c.tunic = s.tunic;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
    return id;
}

// ---- the lobby test ragdoll ---------------------------------------------------------------------------------------------------
// A dummy that looks like the local player, to try the ragdoll with while waiting: walk into it, hit it with the sword, or hold L to carry it.
// It never ages out and is not counted with the bodies of eliminated players.
constexpr size_t kMaxTestRagdolls = 3;

size_t CountTestRagdolls() {
    size_t n = 0;
    for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) n++;
    return n;
}

void RemoveTestRagdolls() {
    for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) { body.dying = true; Actor_Kill(body.actor); }
}

void FlingTestRagdolls() {
    for (auto& [cid, body] : gCorpses) {
        if (!body.test || body.dying) continue;
        body.vel = { (Rand_ZeroOne() - 0.5f) * 700.0f, (Rand_ZeroOne() - 0.5f) * 700.0f };
        body.vy = 420.0f + Rand_ZeroOne() * 200.0f;
        body.pitchVel = -9.0f + Rand_ZeroOne() * 4.0f;
        body.rollVel = (Rand_ZeroOne() - 0.5f) * 14.0f;
        body.spin = (Rand_ZeroOne() - 0.5f) * 8.0f;
        body.bounces = 0; body.still = 0;
        for (auto& l : body.limbVel) { l[0] += (Rand_ZeroOne() - 0.5f) * 12.0f; l[1] += (Rand_ZeroOne() - 0.5f) * 12.0f; }
    }
}

void SpawnTestRagdoll(const royale::HudState& hud) {
    Feat("ragdoll: spawn test dummy");
    if (gPlayState == nullptr || !InGame()) return;
    if (CountTestRagdolls() >= kMaxTestRagdolls) {   // the oldest one goes
        for (auto& [cid, body] : gCorpses) if (body.test && !body.dying) { body.dying = true; Actor_Kill(body.actor); break; }
    }
    const Player* self = GET_PLAYER(gPlayState);
    const float yaw = self->actor.shape.rot.y * (3.14159265f / 32768.0f);
    const uint16_t id = gNextCorpse++;
    if (gNextCorpse < kCorpseIdBase) gNextCorpse = kCorpseIdBase;
    const float x = self->actor.world.pos.x + std::sin(yaw) * 70.0f, z = self->actor.world.pos.z + std::cos(yaw) * 70.0f;
    gSpawningPuppet = id;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, x, self->actor.world.pos.y + 60.0f, z, 0, self->actor.shape.rot.y + 0x8000, 0, 0, false);
    gSpawningPuppet = 0;
    if (actor == nullptr) return;
    actor->flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE);
    Corpse c;
    c.actor = actor;
    c.test = true;
    c.weapon = hud.weapon;
    c.tunic = gLocalTunic;
    c.vy = 120.0f;
    c.pitchVel = -3.0f;
    gCorpses[id] = c;
    gCorpseOf[actor] = id;
}

// Make the world match the session: spawn missing puppets, drop ones that left, died, are out of range, or are in another scene.
void ReconcilePuppets(royale::MatchState state) {
    if (!gSession.Joined() || !InPlayableScene(state)) {
        for (auto& [id, actor] : gActorOf) Actor_Kill(actor);
        gState.clear();
        return;
    }
    std::vector<royale::PuppetState> desired = gSession.Puppets();
    std::unordered_map<uint16_t, bool> wanted;
    for (const auto& s : desired) {
        // Somebody who was standing a moment ago and is now eliminated falls over where they stood (pushed along the way they were facing back).
        auto last = gLastSeen.find(s.id);
        if (!s.alive && last != gLastSeen.end() && last->second.alive && s.scene == gPlayState->sceneNum) {
            const float a = last->second.rot * (3.14159265f / 32768.0f);
            SpawnCorpse(last->second, -std::sin(a), -std::cos(a));
        }
        gLastSeen[s.id] = s;
        if (!s.alive || s.scene != gPlayState->sceneNum) continue; // someone in the other room is not here with us
        wanted[s.id] = true;
        gState[s.id] = s;
        if (gActorOf.find(s.id) == gActorOf.end()) SpawnPuppet(s);
    }
    for (auto it = gActorOf.begin(); it != gActorOf.end();) {
        if (wanted.find(it->first) == wanted.end()) {
            Actor_Kill(it->second); // Puppet_Destroy erases the map entries on the next actor update
            gState.erase(it->first);
            it = gActorOf.erase(it);
        } else {
            ++it;
        }
    }
}

void KillAllEnemies() {
    if (gPlayState == nullptr) return;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) Actor_Kill(a);
}

