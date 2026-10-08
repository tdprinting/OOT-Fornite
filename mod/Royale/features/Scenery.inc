// ---- angry villagers -------------------------------------------------------------------------------------------------------
// The scene's own people (the carpenters, say) stay in a match. Hit one by accident and they turn on you: they run at you and swing, and the server
// takes a little health for each blow. Leave them alone for a while (or get away from them) and they calm down and walk back to where they were.
struct AngryNpc {
    ActorFunc origUpdate = nullptr;
    ActorFunc origDestroy = nullptr;
    Vec3f home = { 0, 0, 0 };
    float timer = 0;      // seconds of anger left
    float cooldown = 0;   // until the next swing
    bool returning = false;
};
std::unordered_map<Actor*, AngryNpc> gAngry;
bool gAngryPlayerAlive = true;
constexpr float kNpcDt = 1.0f / 20.0f;          // the game runs its actors at 20 frames a second
constexpr float kNpcAngerSeconds = 25.0f;
constexpr float kNpcRunSpeed = 4.2f;            // units a frame: a little slower than Link running, so you can get away
constexpr float kNpcForgetDistance = 600.0f;    // out of sight, out of mind: anger runs down twice as fast beyond this

bool CanAnger(Actor* a) {
    if (a == nullptr || a->update == nullptr || a->category != ACTORCAT_NPC) return false;
    if (a->id == ACTOR_PLAYER || a->id == ACTOR_EN_OE2 || a->id == ACTOR_EN_ISHI) return false;   // players' puppets and our own stand-ins
    return gPuppetOf.find(a) == gPuppetOf.end();
}

void AngryNpc_Destroy(Actor* actor, PlayState* play) {
    auto it = gAngry.find(actor);
    ActorFunc orig = nullptr;
    if (it != gAngry.end()) { orig = it->second.origDestroy; gAngry.erase(it); }
    if (orig) orig(actor, play);
}

bool NpcStepTo(Actor* actor, PlayState* play, float tx, float tz, float speed) {
    const float dx = tx - actor->world.pos.x, dz = tz - actor->world.pos.z;
    const float d = std::hypot(dx, dz);
    const s16 yaw = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
    actor->world.rot.y = actor->shape.rot.y = yaw;
    if (d < 1.0f) return true;
    const float step = std::min(speed, d);
    Vec3f from = { actor->world.pos.x, actor->world.pos.y + 30.0f, actor->world.pos.z };
    Vec3f to = { actor->world.pos.x + dx / d * (step + 14.0f), actor->world.pos.y + 30.0f, actor->world.pos.z + dz / d * (step + 14.0f) };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) return false;   // a wall: stay put
    actor->world.pos.x += dx / d * step;
    actor->world.pos.z += dz / d * step;
    actor->world.pos.y = GroundY(play, actor->world.pos.x, actor->world.pos.z, actor->world.pos.y);
    return false;
}

void AngryNpc_Update(Actor* actor, PlayState* play) {
    auto it = gAngry.find(actor);
    if (it == gAngry.end()) return;
    AngryNpc& a = it->second;
    if (a.origUpdate) a.origUpdate(actor, play);   // the villager's own animation keeps running; where it stands and faces is ours from here
    Player* player = GET_PLAYER(play);
    if (player == nullptr || !InField()) return;
    const float dx = player->actor.world.pos.x - actor->world.pos.x, dz = player->actor.world.pos.z - actor->world.pos.z;
    const float dist = std::hypot(dx, dz);
    a.cooldown = std::max(0.0f, a.cooldown - kNpcDt);
    if (!a.returning) {
        a.timer -= kNpcDt * (dist > kNpcForgetDistance || !gAngryPlayerAlive ? 2.0f : 1.0f);
        if (a.timer <= 0.0f) a.returning = true;
    }
    if (a.returning) {
        if (NpcStepTo(actor, play, a.home.x, a.home.z, kNpcRunSpeed * 0.6f) || std::hypot(a.home.x - actor->world.pos.x, a.home.z - actor->world.pos.z) < 20.0f) {
            actor->update = a.origUpdate;   // calm again: the game's own behaviour takes back over
            actor->destroy = a.origDestroy;
            gAngry.erase(it);
        }
        return;
    }
    if (dist > 60.0f) NpcStepTo(actor, play, player->actor.world.pos.x, player->actor.world.pos.z, kNpcRunSpeed);
    else actor->world.rot.y = actor->shape.rot.y = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
    if (dist <= 85.0f && a.cooldown <= 0.0f && gAngryPlayerAlive && std::fabs(player->actor.world.pos.y - actor->world.pos.y) < 70.0f) {
        a.cooldown = 1.0f;
        a.timer = std::max(a.timer, kNpcAngerSeconds * 0.6f);   // it is busy hitting you: it stays angry while it does
        gSession.ReportNpcHit(0.5f);
        Audio_PlaySoundGeneral(NA_SE_IT_HAMMER_HIT, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        Player_PlaySfx(&player->actor, NA_SE_VO_LI_DAMAGE_S + player->ageProperties->unk_92);
    }
}

void AngerNpc(Actor* actor) {
    auto it = gAngry.find(actor);
    if (it != gAngry.end()) { it->second.timer = kNpcAngerSeconds; it->second.returning = false; return; }
    AngryNpc a;
    a.origUpdate = actor->update;
    a.origDestroy = actor->destroy;
    a.home = actor->world.pos;
    a.timer = kNpcAngerSeconds;
    gAngry[actor] = a;
    actor->update = AngryNpc_Update;
    actor->destroy = AngryNpc_Destroy;
    Audio_PlaySoundGeneral(NA_SE_EN_STAL_WARAU, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// A swing, spin or shot that found no player: did it hit a villager? (A bad aim at a carpenter makes an enemy of them.)
void HitNpcInFront(Player* player, const royale::WeaponStats& w) {
    if (!InField() || !gSession.Joined()) return;
    const float reach = w.ranged ? std::clamp(w.range, 150.0f, 500.0f) : std::clamp(w.range, 90.0f, 170.0f);
    const int cone = w.ranged ? 0x1000 : 0x2800;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != nullptr; a = a->next) {
        if (!CanAnger(a) && gAngry.find(a) == gAngry.end()) continue;
        const float dx = a->world.pos.x - player->actor.world.pos.x, dz = a->world.pos.z - player->actor.world.pos.z;
        const float d = std::hypot(dx, dz);
        if (d > reach + 25.0f || std::fabs(a->world.pos.y - player->actor.world.pos.y) > 90.0f) continue;
        const s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        if (std::abs(static_cast<int>(static_cast<s16>(toTarget - player->actor.shape.rot.y))) > cone && d > 40.0f) continue;
        AngerNpc(a);
    }
}

void SmashPropInFront(Player* player, const royale::WeaponStats& w) {
    if (!gSession.Client()) return;
    const auto& props = gSession.Client()->Props();
    size_t best = props.size();
    float bestDist = 1e9f;
    for (const auto& [i, pa] : gProps) {
        if (i >= props.size() || !pa.actor) continue;
        const royale::PropKind kind = props[i].kind;
        if (kind != royale::PropKind::Rock && kind != royale::PropKind::Boulder && kind != royale::PropKind::Bush) continue;
        if (kind == royale::PropKind::Boulder && w.damage < 1.5f && w.splashRadius <= 0) continue;
        const float dx = props[i].pos.x - player->actor.world.pos.x, dz = props[i].pos.z - player->actor.world.pos.z;
        const float reach = royale::PropRadius(kind) + std::clamp(w.range, 90.0f, 170.0f);
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > reach) continue;
        const s16 toTarget = static_cast<s16>(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        if (std::abs(static_cast<int>(static_cast<s16>(toTarget - player->actor.shape.rot.y))) > 0x2800 && d > 50.0f) continue;
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best >= props.size()) return;
    auto it = gProps.find(best);
    if (it == gProps.end()) return;
    float y = player->actor.world.pos.y;
    FloorAt(props[best].pos.x, props[best].pos.z, &y);
    SparkBurst(gPlayState, props[best].pos.x, y + 20.0f, props[best].pos.z, props[best].kind == royale::PropKind::Bush ? Color_RGBA8{ 90, 220, 90, 255 } : Color_RGBA8{ 190, 190, 180, 255 }, 8, 3.0f);
    Vec3f at = { props[best].pos.x, y + 20.0f, props[best].pos.z };
    Audio_PlaySoundGeneral(props[best].kind == royale::PropKind::Bush ? NA_SE_EV_PLANT_BROKEN : NA_SE_EV_ROCK_BROKEN, &at, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    Actor_Kill(it->second.actor);   // Prop_Destroy tells the server it was broken
}

// Hold the player on top of a climbing block (landing on it, walking along it) and out of its sides. Runs every frame in the player's update, after
// the game has settled Link on the scene's own floor.
// Drinking: which bottle Link takes out (the potion the server will most likely pick: the first that heals, or the first shield potion),
// how long it takes, and its sounds (Link's gulp and the hearts refilling).
constexpr float kDrinkSeconds = 1.5f;
royale::ItemId BottleToDrink(const royale::HudState& hud, bool shield) {
    for (const auto& pot : hud.inv.potions) {
        const royale::ItemId id = static_cast<royale::ItemId>(pot.item);
        const royale::PotionDef d = royale::PotionOf(id);
        if (shield ? d.shield > 0 : (!d.revive && d.heal > 0)) return id;
    }
    return shield ? royale::ItemId::BluePotion : royale::ItemId::RedPotion;
}
void DrinkSounds(Player* player) {
    Player_PlaySfx(&player->actor, static_cast<u16>(NA_SE_VO_LI_DRINK + player->ageProperties->unk_92));
    Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// ---- solid scenery ------------------------------------------------------------------------------------------------------------
// The climbing blocks, rocks, boulders and standing stones are real ground: the game's own collision, the kind a moving platform or a chest
// has. One actor of ours holds a collision mesh built from the scenery near you (a box for each block, a stone shape for the rest), rebuilt
// as you move. Link's own movement then does the rest: he lands on them, walks and rolls across them, hops up small steps, grabs and climbs
// ledges, and stops at their sides, exactly as on the scene's own floor. The game only has room for so much of this kind of collision
// (shared with chests, gates and moving platforms), so only the nearest pieces are in it at a time; the far ones don't matter to you.
constexpr int kSolidMaxVtx = 420;
constexpr int kSolidMaxPoly = 420;
constexpr float kSolidRadius = 1500.0f;   // scenery within this of you is solid
Vec3s gSolidVtx[kSolidMaxVtx];
CollisionPoly gSolidPoly[kSolidMaxPoly];
SurfaceType gSolidSurfaces[2] = {
    { { 0, 2 } },                // stone underfoot
    { { 0, 2 | (1u << 17) } },   // stone the hookshot bites into (the blocks)
};
CollisionHeader gSolidHeader;
Actor* gSolidActor = nullptr;
bool gSolidFailed = false;            // the game had no free collision slot: fall back to ApplyPlatforms / ApplyRocks
std::vector<uint64_t> gSolidSet;       // the props (their index) and the island's scenery (SceneryKey) in the mesh now, sorted
royale::Vec2 gSolidCentre = { 1e9f, 1e9f };
int gSolidAge = 0;

bool SolidActive() { return gSolidActor != nullptr && gSolidBgId >= 0; }

struct SolidBuilder {
    int nv = 0, np = 0;
    bool Room(int v, int p, int maxPoly) const { return nv + v <= kSolidMaxVtx && np + p <= maxPoly; }
    int V(float x, float y, float z) {
        gSolidVtx[nv] = { static_cast<s16>(std::lround(std::clamp(x, -32000.0f, 32000.0f))), static_cast<s16>(std::lround(std::clamp(y, -32000.0f, 32000.0f))),
                          static_cast<s16>(std::lround(std::clamp(z, -32000.0f, 32000.0f))) };
        return nv++;
    }
    template <class C> void Box(const C& c) {
        const int first=nv;
        for(int y=0;y<2;++y) for(int z=0;z<2;++z) for(int x=0;x<2;++x)
            V(x?c.x1:c.x0,y?c.y1:c.y0,z?c.z1:c.z0);
        const Vec3f inside={(c.x0+c.x1)*.5f,(c.y0+c.y1)*.5f,(c.z0+c.z1)*.5f};
        const int faces[6][4]={{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
        for(const auto& f:faces) {
            T(first+f[0],first+f[1],first+f[2],inside,0,0);
            T(first+f[0],first+f[2],first+f[3],inside,0,0);
        }
    }
    // A triangle facing away from `inside` (the game works out the normal from the winding, so the winding is fixed to point outwards).
    void T(int a, int b, int c, const Vec3f& inside, u16 type, u16 xp) {
        const Vec3s &A = gSolidVtx[a], &B = gSolidVtx[b], &C = gSolidVtx[c];
        const float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const float cx = (A.x + B.x + C.x) / 3.0f - inside.x, cy = (A.y + B.y + C.y) / 3.0f - inside.y, cz = (A.z + B.z + C.z) / 3.0f - inside.z;
        if (nx * cx + ny * cy + nz * cz < 0) std::swap(b, c);
        CollisionPoly& p = gSolidPoly[np++];
        p = {};
        p.type = type;
        p.flags_vIA = static_cast<u16>(a | (xp << 13));
        p.flags_vIB = static_cast<u16>(b);
        p.vIC = static_cast<u16>(c);
        p.normal = { 0, 0x7FFF, 0 };   // filled in properly by the game when it takes the mesh
    }
    // A stone: a ring of `sides` points round the foot, a smaller ring at the top, and a flat top to stand on. A box is the same with four sides.
    // The rings can be ellipses (half-widths along x and z before `yaw` turns the whole stone, the game's own Y turn), for the island's cliff slabs.
    void Stone(float x, float z, float baseY, float topY, float footX, float footZ, float topX, float topZ, int sides, float turn, float yaw, u16 type, u16 xp) {
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        auto ring = [&](float rx, float rz, float y) {
            const int first = nv;
            for (int i = 0; i < sides; i++) {
                const float a = turn + 6.2831853f * i / sides, lx = std::sin(a) * rx, lz = std::cos(a) * rz;
                V(x + lx * cy + lz * sy, y, z - lx * sy + lz * cy);
            }
            return first;
        };
        const int foot = ring(footX, footZ, baseY), top = ring(topX, topZ, topY);
        const Vec3f inside = { x, (baseY + topY) * 0.5f, z };
        for (int i = 0; i < sides; i++) {
            const int j = (i + 1) % sides;
            T(foot + i, foot + j, top + j, inside, type, xp);
            T(foot + i, top + j, top + i, inside, type, xp);
        }
        const Vec3f below = { x, topY - 50.0f, z };
        for (int i = 1; i + 1 < sides; i++) T(top, top + i, top + i + 1, below, type, xp);
    }
    void Prism(float x, float z, float baseY, float topY, float footR, float topR, int sides, float turn, u16 type, u16 xp) {
        Stone(x, z, baseY, topY, footR, footR, topR, topR, sides, turn, 0.0f, type, xp);
    }
};
constexpr int PrismVtx(int sides) { return sides * 2; }
constexpr int PrismPoly(int sides) { return sides * 2 + sides - 2; }

int SolidSides(royale::PropKind k) { return royale::IsPlatform(k) ? 4 : k == royale::PropKind::Rock ? 6 : 8; }
bool IsSolidKind(royale::PropKind k) { return royale::IsPlatform(k) || k == royale::PropKind::Rock || k == royale::PropKind::Boulder || k == royale::PropKind::Pillar; }

// How many triangles the game still has room for, after everything else with this kind of collision (chests, gates, platforms of the scene).
int SolidPolyBudget() {
    const DynaCollisionContext& dyna = gPlayState->colCtx.dyna;
    int used = 0, usedVtx = 0;
    for (int i = 0; i < BG_ACTOR_MAX; i++) {
        if (i == gSolidBgId || !(dyna.bgActorFlags[i] & 1) || dyna.bgActors[i].colHeader == nullptr) continue;
        used += dyna.bgActors[i].colHeader->numPolygons;
        usedVtx += dyna.bgActors[i].colHeader->numVertices;
    }
    const int room = std::min({ dyna.polyListMax - used, dyna.vtxListMax - usedVtx, dyna.polyNodesMax - used }) - 64;   // a margin for things spawned this frame
    return std::clamp(room, 0, kSolidMaxPoly);
}

// The island's own scenery that has a solid body (shared/fortnite_scenery.h): the cliff slabs and crags are walls and ledges, the boulders and
// oaks are stones and trunks. Each piece is cheap to rebuild from its cell, so the mesh only remembers which cells are in it.
struct SolidScenery { int cx, cz; royale::fortnite::SceneryPiece pc; float base; };
constexpr float kSolidSceneryRadius = 900.0f;
bool SceneryIsSolid(royale::fortnite::SceneryKind k) {
    using royale::fortnite::SceneryKind;
    return k == SceneryKind::Cliff || k == SceneryKind::Crag || k == SceneryKind::Boulder || k == SceneryKind::Oak;
}
int SceneryPolys(royale::fortnite::SceneryKind k) {
    using royale::fortnite::SceneryKind;
    return k == SceneryKind::Crag ? 3 * PrismPoly(6) : k == SceneryKind::Cliff ? PrismPoly(10) : PrismPoly(6);
}
int SceneryVtx(royale::fortnite::SceneryKind k) {
    using royale::fortnite::SceneryKind;
    return k == SceneryKind::Crag ? 3 * PrismVtx(6) : k == SceneryKind::Cliff ? PrismVtx(10) : PrismVtx(6);
}
// The scenery within kSolidSceneryRadius of (x, z) that is standing (the same pieces DrawIslandScenery draws), nearest first.
void NearbySolidScenery(float x, float z, std::vector<std::pair<float, SolidScenery>>* out) {
    if (!OnIsland() || !DebugOn(kDbgScenery) || gFoliage <= 0.01f) return;
    const float cell = royale::fortnite::kSceneryCell, dens = std::min(1.3f, gFoliage);
    gFloraBudget = 60;   // measuring each piece's ground is cached, so this only spreads the first look over a few frames
    for (int cz = static_cast<int>(std::floor((z - kSolidSceneryRadius) / cell)); cz <= static_cast<int>(std::floor((z + kSolidSceneryRadius) / cell)); cz++)
        for (int cx = static_cast<int>(std::floor((x - kSolidSceneryRadius) / cell)); cx <= static_cast<int>(std::floor((x + kSolidSceneryRadius) / cell)); cx++) {
            royale::fortnite::SceneryPiece pc;
            if (!royale::fortnite::SceneryIn(cx, cz, dens, &pc) || !SceneryIsSolid(pc.kind)) continue;
            const float d = std::hypot(pc.x - x, pc.z - z);
            if (d > kSolidSceneryRadius) continue;
            const FloraSpot* spot = FloraSpotAt(7, cx, cz, pc.x, pc.z);
            if (spot == nullptr || !spot->ok) continue;
            out->push_back({ d, { cx, cz, pc, spot->y } });
        }
}
uint64_t SceneryKey(const SolidScenery& s) { return (1ull << 62) | (static_cast<uint64_t>(s.cx + 65536) << 24) | static_cast<uint64_t>(s.cz + 65536); }

// The lowest ground under a block's footprint (its middle and corners), so a block on a slope reaches down to the low side instead of floating over it.
float PlatformFoot(size_t i) {
    auto it = gPlatformFoot.find(i);
    if (it != gPlatformFoot.end()) return it->second;
    const float mid = PlatformBase(i);
    if (mid < -1.0e8f) return mid;
    const royale::Prop& p = gSession.Client()->Props()[i];
    float low = mid;
    for (int k = 0; k < 4; k++) {
        float y = 0;
        if (RawFloorAt(p.pos.x + (k & 1 ? 1 : -1) * royale::kPlatformHalf, p.pos.z + (k & 2 ? 1 : -1) * royale::kPlatformHalf, &y)) low = std::min(low, y);
    }
    gPlatformFoot[i] = low;
    return low;
}

// Fill the mesh with the scenery nearest (x, z). Returns false if the same pieces are already in it.
bool BuildSolidMesh(float x, float z, bool force) {
    if (OnAuthoredTerrain()) {
        const Player* player=GET_PLAYER(gPlayState);
        const size_t limit=static_cast<size_t>(std::max(0,std::min((SolidPolyBudget()-1)/12,(kSolidMaxVtx-3)/8)));
        const bool riftlandsMap=OnRiftlandsTerrain();
        const bool kingdomMap=OnKingdomTerrain();
        const auto chosen=riftlandsMap?royale::RiftlandsCollidersNear(x,player?player->actor.world.pos.y:0,z,limit):kingdomMap?royale::KingdomCollidersNear(x,player?player->actor.world.pos.y:0,z,limit):royale::ConvergenceCollidersNear(x,player?player->actor.world.pos.y:0,z,limit);
        std::vector<uint64_t> keys;
        for(size_t i:chosen) keys.push_back((1ull<<62)|i);
        std::sort(keys.begin(),keys.end());
        if(!force && keys==gSolidSet) return false;
        gSolidSet=keys;
        SolidBuilder b;
        // Keep a valid header even where there are no nearby objects.
        b.V(-10,-10000,-10);b.V(10,-10000,-10);b.V(0,-10000,10);
        b.T(0,1,2,{0,-10010,0},0,0);
        for(size_t i:chosen) { if(riftlandsMap) b.Box(royale::riftlands::kPropColliders[i]); else if(kingdomMap) b.Box(royale::kingdom::kPropColliders[i]); else b.Box(royale::convergence::kPropColliders[i]); }
        gSolidHeader={};gSolidHeader.numVertices=static_cast<u16>(b.nv);gSolidHeader.vtxList=gSolidVtx;
        gSolidHeader.numPolygons=static_cast<u16>(b.np);gSolidHeader.polyList=gSolidPoly;gSolidHeader.surfaceTypeList=gSolidSurfaces;
        Vec3s lo=gSolidVtx[0],hi=lo;
        for(int i=1;i<b.nv;++i) {
            lo.x=std::min(lo.x,gSolidVtx[i].x);lo.y=std::min(lo.y,gSolidVtx[i].y);lo.z=std::min(lo.z,gSolidVtx[i].z);
            hi.x=std::max(hi.x,gSolidVtx[i].x);hi.y=std::max(hi.y,gSolidVtx[i].y);hi.z=std::max(hi.z,gSolidVtx[i].z);
        }
        gSolidHeader.minBounds=lo;gSolidHeader.maxBounds=hi;
        return true;
    }
    const auto& props = gSession.Client()->Props();
    struct Cand { float d; bool scenery; size_t i; };
    std::vector<Cand> near;
    for (size_t i = 0; i < props.size(); i++) {
        const royale::Prop& p = props[i];
        if (!IsSolidKind(p.kind) || gBrokenProps.count(i) || (royale::IsPlatform(p.kind) && BlocksAreBaked())) continue;   // baked blocks are ground already
        const float d = std::hypot(p.pos.x - x, p.pos.z - z);
        if (d > kSolidRadius) continue;
        auto pa = gProps.find(i);   // a rock someone has picked up and carried off is not where its footprint is any more
        if (pa != gProps.end() && pa->second.actor != nullptr && std::hypot(pa->second.actor->world.pos.x - p.pos.x, pa->second.actor->world.pos.z - p.pos.z) > 30.0f) continue;
        near.push_back({ d, false, i });
    }
    std::vector<std::pair<float, SolidScenery>> scenery;
    NearbySolidScenery(x, z, &scenery);
    for (size_t k = 0; k < scenery.size(); k++) near.push_back({ scenery[k].first, true, k });
    std::sort(near.begin(), near.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    const int budget = SolidPolyBudget();
    std::vector<Cand> chosen;
    std::vector<uint64_t> keys;
    int polys = 1, vtx = 3;   // the placeholder below
    for (const Cand& c : near) {
        const int addPoly = c.scenery ? SceneryPolys(scenery[c.i].second.pc.kind) : PrismPoly(SolidSides(props[c.i].kind));
        const int addVtx = c.scenery ? SceneryVtx(scenery[c.i].second.pc.kind) : PrismVtx(SolidSides(props[c.i].kind));
        if (polys + addPoly > budget || vtx + addVtx > kSolidMaxVtx) break;
        if (!c.scenery && PlatformFoot(c.i) < -1.0e8f) continue;   // its ground isn't loaded yet
        polys += addPoly;
        vtx += addVtx;
        chosen.push_back(c);
        keys.push_back(c.scenery ? SceneryKey(scenery[c.i].second) : static_cast<uint64_t>(c.i));
    }
    std::sort(keys.begin(), keys.end());
    if (!force && keys == gSolidSet) return false;
    gSolidSet = keys;

    SolidBuilder b;
    // Always one triangle, so the mesh is never empty (the game divides by its vertex count): a sliver of wall far below the map, out of everyone's way.
    const float lowY = -31000.0f;
    const int a = b.V(x, lowY, z), c = b.V(x + 1.0f, lowY, z), e = b.V(x, lowY + 1.0f, z);
    b.T(a, c, e, { x, lowY, z + 1.0f }, 0, 1);
    for (const Cand& cand : chosen) {
        if (cand.scenery) {
            using royale::fortnite::SceneryKind;
            const SolidScenery& sc = scenery[cand.i].second;
            const float sk = sc.pc.scale, base = sc.base, yaw = sc.pc.yaw;
            auto local = [&](float lx, float lz, float* wx, float* wz) { *wx = sc.pc.x + (lx * std::cos(yaw) + lz * std::sin(yaw)) * sk; *wz = sc.pc.z + (-lx * std::sin(yaw) + lz * std::cos(yaw)) * sk; };
            switch (sc.pc.kind) {
                case SceneryKind::Cliff:   // a slab 300 wide and 112 deep, 166 tall, a little narrower at the top (tools/scenery/build_scenery.py, build_cliff); you stand on its top from the high side
                    b.Stone(sc.pc.x, sc.pc.z, base - 40.0f, base + 160.0f * sk - 14.0f * sk, 146.0f * sk, 54.0f * sk, 118.0f * sk, 38.0f * sk, 10, 0.0f, yaw, 0, 1);
                    break;
                case SceneryKind::Crag: {   // three spires: the snowy tips are too narrow to stand on, so each is a wall
                    static const float spire[3][4] = { { 0.0f, 0.0f, 58.0f, 250.0f }, { 64.0f, 26.0f, 42.0f, 180.0f }, { -52.0f, -30.0f, 38.0f, 150.0f } };
                    for (const auto& sp : spire) {
                        float wx, wz;
                        local(sp[0], sp[1], &wx, &wz);
                        b.Stone(wx, wz, base - 40.0f, base + sp[3] * 0.7f * sk, sp[2] * sk, sp[2] * sk, sp[2] * 0.45f * sk, sp[2] * 0.45f * sk, 6, 0.0f, yaw, 0, 1);
                    }
                    break;
                }
                case SceneryKind::Oak:   // the trunk only: the leaves are above your head
                    b.Stone(sc.pc.x, sc.pc.z, base - 30.0f, base + 110.0f * sk, 24.0f * sk, 24.0f * sk, 14.0f * sk, 14.0f * sk, 6, 0.0f, 0.0f, 0, 1);
                    break;
                default: {   // a boulder: wide at the foot, a flat crown you can climb onto
                    const float r = royale::fortnite::SceneryRadius(sc.pc.kind, sk);
                    b.Stone(sc.pc.x, sc.pc.z, base - 30.0f, base + royale::fortnite::SceneryTop(sc.pc.kind, sk) * 0.8f, r, r, r * 0.65f, r * 0.65f, 6, sc.pc.yaw, 0.0f, 0, 1);
                    break;
                }
            }
            continue;
        }
        const size_t i = cand.i;
        const royale::Prop& p = props[i];
        const float base = PlatformBase(i);
        const int sides = SolidSides(p.kind);
        if (royale::IsPlatform(p.kind)) {   // a box, square to the map, reaching down to the low side of its footprint so a slope never leaves a gap under it
            const float h = royale::kPlatformHalf * 1.41421356f;
            b.Prism(p.pos.x, p.pos.z, PlatformFoot(i) - 20.0f, base + royale::PlatformHeight(p.kind), h, h, 4, 0.78539816f, 1, 0);
        } else if (p.kind == royale::PropKind::Pillar) {
            b.Prism(p.pos.x, p.pos.z, base - 30.0f, base + 200.0f, royale::PropRadius(p.kind), royale::PropRadius(p.kind) * 0.85f, sides, 0.0f, 0, 1);
        } else {   // rocks and boulders: wide at the foot, a flat crown at the height you stand on (BoulderTop)
            const bool boulder = p.kind == royale::PropKind::Boulder;
            const float r = royale::PropRadius(p.kind) * (boulder ? royale::BoulderScale(p.rot) : 1.0f);
            const float top = boulder ? royale::BoulderTop(royale::BoulderShape(p.rot)) * royale::BoulderScale(p.rot) : 24.0f;
            b.Prism(p.pos.x, p.pos.z, base - 30.0f, base + top, r * 0.95f, r * 0.62f, sides, p.rot * (3.14159265f / 32768.0f), 0, 1);   // the camera passes through stones
        }
    }
    gSolidHeader = {};
    gSolidHeader.numVertices = static_cast<u16>(b.nv);
    gSolidHeader.vtxList = gSolidVtx;
    gSolidHeader.numPolygons = static_cast<u16>(b.np);
    gSolidHeader.polyList = gSolidPoly;
    gSolidHeader.surfaceTypeList = gSolidSurfaces;
    Vec3s lo = gSolidVtx[0], hi = gSolidVtx[0];
    for (int i = 1; i < b.nv; i++) {
        lo.x = std::min(lo.x, gSolidVtx[i].x); lo.y = std::min(lo.y, gSolidVtx[i].y); lo.z = std::min(lo.z, gSolidVtx[i].z);
        hi.x = std::max(hi.x, gSolidVtx[i].x); hi.y = std::max(hi.y, gSolidVtx[i].y); hi.z = std::max(hi.z, gSolidVtx[i].z);
    }
    gSolidHeader.minBounds = lo;
    gSolidHeader.maxBounds = hi;
    return true;
}

void Solid_Init(Actor* actor, PlayState* play) {
    Actor_SetScale(actor, 1.0f);
    actor->world.pos = actor->home.pos = { 0, 0, 0 };
    actor->shape.rot = actor->world.rot = { 0, 0, 0 };
    actor->shape.yOffset = 0.0f;
    DynaPolyActor* dyna = reinterpret_cast<DynaPolyActor*>(actor);
    DynaPolyActor_Init(dyna, 0);
    const Player* player = GET_PLAYER(play);
    BuildSolidMesh(player ? player->actor.world.pos.x : 0.0f, player ? player->actor.world.pos.z : 0.0f, true);
    dyna->bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, actor, &gSolidHeader);
    if (dyna->bgId == BG_ACTOR_MAX) { gSolidFailed = true; gSolidBgId = -1; Actor_Kill(actor); return; }
    gSolidBgId = dyna->bgId;
}

void Solid_Destroy(Actor* actor, PlayState* play) {
    DynaPolyActor* dyna = reinterpret_cast<DynaPolyActor*>(actor);
    if (dyna->bgId >= 0 && dyna->bgId < BG_ACTOR_MAX) DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, dyna->bgId);
    if (gSolidActor == actor) { gSolidActor = nullptr; gSolidBgId = -1; }
    gSolidSet.clear();
}

// The mesh only changes here, in the actor's own update: the game takes it in right after every actor has updated, in the same frame.
void Solid_Update(Actor* actor, PlayState* play) {
    if (!OnAuthoredTerrain() && (!gSession.Client() || !InField())) return;
    const Player* player = GET_PLAYER(play);
    if (player == nullptr) return;
    const float x = player->actor.world.pos.x, z = player->actor.world.pos.z;
    if (!OnAuthoredTerrain() && ++gSolidAge < 12 && std::hypot(x - gSolidCentre.x, z - gSolidCentre.z) < 150.0f) return;   // authored furniture is reconsidered every frame
    gSolidAge = 0;
    gSolidCentre = { x, z };
    if (BuildSolidMesh(x, z, false)) play->colCtx.dyna.bitFlag |= DYNAPOLY_INVALIDATE_LOOKUP;
}

int SolidActorId() {
    static int id = -1;
    if (id < 0) {
        ActorDBInit init;
        init.name = "Royale_Solid";
        init.desc = "Battle royale scenery collision";
        init.category = ACTORCAT_BG;
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(DynaPolyActor);
        init.init = Solid_Init;
        init.destroy = Solid_Destroy;
        init.update = Solid_Update;
        id = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return id;
}

// Called every frame: the collision actor exists while you are in a match's field, and only then.
void EnsureSolidScenery() {
    // Authored props must be solid in the map lobby too, before the host requests
    // a match measurement and even if the custom scene outlives the connection.
    const bool want = OnAuthoredTerrain() || (gInFieldFrames > 20 &&
        gSession.Client() != nullptr && InField() && !gSession.Client()->Props().empty());
    if (want && gSolidActor == nullptr && !gSolidFailed) {
        gSolidCentre = { 1e9f, 1e9f };
        gSolidActor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, static_cast<s16>(SolidActorId()), 0, 0, 0, 0, 0, 0, 0, false);
        if (gSolidActor != nullptr && gSolidBgId < 0) gSolidActor = nullptr;   // it found no slot and removed itself
    } else if (!want && gSolidActor != nullptr) {
        Actor_Kill(gSolidActor);
        gSolidActor = nullptr;
        gSolidBgId = -1;
    }
}

