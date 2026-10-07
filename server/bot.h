#pragma once
#include "../shared/anim.h"
#include "../shared/props.h"
#include "../shared/vehicle.h"
#include "match.h"
#include "nav.h"
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <unordered_map>
#include <vector>

namespace royale {

enum class BotDifficulty : uint8_t { Easy, Normal, Hard };

// Server-side bot brain. Bots are ordinary PlayerState entries (isBot) that this class moves and acts for, using only Match's
// public API, the same calls a human client's messages end up making. It never touches game code, so it runs anywhere the
// server runs.
//
// What a bot does each tick:
//   Perceive   sees enemies within its sight range (everyone while a Lens of Truth / Saria's Song reveal is active), is alerted
//              by taking damage, and remembers where it last saw its target so it can hunt it down.
//   Assess     weighs every visible enemy by how a fight would go (damage per second, health, shields, potions, stuns) and picks
//              a target, sticking with it unless a clearly better one shows up.
//   Decide     in priority order: heal, stay inside the storm, flee a losing fight, fight, loot, hunt, then drift to a good spot.
//   Act        paths around walls and water with A* (if the host gave it a NavGrid), strafes and dodges in fights, kites melee
//              enemies when it has range, and uses its ability and consumables when they matter.
// And it gets about the way a player does: it skydives in at the start (picking a landing spot with chests near it, diving when it can
// still reach it), sprints on the same stamina bar players have, jumps up ledges and onto climbing blocks and low boulders, drops off
// small ledges and goes round cliffs and water. It uses the ground in a fight: it can't see or shoot through rocks and hills, so it takes
// cover behind them to heal or to get away from a bow, and a bot with a bow likes to fight from higher ground.
// It drives the carts too (shared/vehicle.h): a bot with a long way to go (out of the storm, running for its life, or just fond of driving) walks
// to a parked cart and drives it, following a path across open ground, easing off for turns and backing out when it is stuck, and gets out
// where it was going or when the cart is about to go up. An aggressive one runs people over. One that wants a lift climbs into the back of a
// cart stopped next to it and shoots from the saddle, takes the reins if the driver gets out, and jumps off when the ride is over. Bots on foot
// jump out of the way of a cart coming at them.
// Each bot has its own personality (aggression, caution, greed) so they don't all behave alike. Difficulty changes how well
// they aim, how fast they react, how far they see and how much they use abilities.
class BotController {
  public:
    static constexpr float kStormLookahead = 15.0f; // seconds
    static constexpr float kAlwaysFightRange = 300.0f;
    static constexpr float kSearchRadius = 800.0f;
    static constexpr float kMinFightDps = 1.0f;     // bots with a weaker weapon avoid fights they could skip
    static constexpr float kSight = 900.0f;         // Normal difficulty
    // After the drop bots look for loot first: for this long they don't start a fight unless they are hurt. And a bot with only the starting
    // sword looks for a real weapon instead of picking fights (unless cornered). Both are on in the game; most unit tests switch them off so
    // that they can put bots in a fight at once.
    static float& CalmSeconds() { static float v = 15.0f; return v; }
    static bool& GearFirst() { static bool v = true; return v; }

    explicit BotController(uint64_t seed) : rng(seed ^ 0x626F74ull) {} // "bot"

    void SetNav(std::shared_ptr<const NavGrid> grid) { nav = std::move(grid); }
    // The scenery, so bots cut bushes and break rocks for rupees and ammo the way players do. The server breaks the prop (SmashProp) for
    // each request DrainSmashes() hands it, and tells the bots about every prop anybody breaks (PropGone).
    void SetProps(std::vector<Prop> list) {
        props = std::move(list); propGone.assign(props.size(), false); smashes.clear();
        // What can be stood on, bucketed so a bot finds what is under it quickly (LiftAt).
        stands.clear(); standBuckets.clear();
        for (const Prop& p : props) {
            if (PropRadius(p.kind) <= 0) continue;
            const float top = SceneryHeight(p);
            Stand st{p.pos, kPlatformHalf, top, true};
            if (!IsPlatform(p.kind)) {
                if (p.kind != PropKind::Boulder || top > NavGrid::kClimbUp) continue;
                st = {p.pos, PropRadius(p.kind) * BoulderScale(p.rot) * 0.8f, top, false};
            }
            const int idx = static_cast<int>(stands.size());
            stands.push_back(st);
            const int x0 = Bucket(st.at.x - st.half), x1 = Bucket(st.at.x + st.half), z0 = Bucket(st.at.z - st.half), z1 = Bucket(st.at.z + st.half);
            for (int bx = x0; bx <= x1; bx++) for (int bz = z0; bz <= z1; bz++) standBuckets[BucketKey(bx, bz)].push_back(idx);
        }
    }
    void PropGone(size_t index) { if (index < propGone.size()) propGone[index] = true; }
    std::vector<std::pair<uint32_t, size_t>> DrainSmashes() { std::vector<std::pair<uint32_t, size_t>> out; out.swap(smashes); return out; }
    bool HasNav() const { return nav != nullptr; }
    void SetDifficulty(BotDifficulty d) { difficulty = d; memory.clear(); } // bots re-roll their personalities
    // The Sandbox test map: bots that stand where they are put (they neither move nor act) for trying things out on.
    void SetFrozen(bool on) { frozen = on; }
    bool Frozen() const { return frozen; }
    BotDifficulty Difficulty() const { return difficulty; }

    void Step(Match& m, float dt) {
        const MatchState state = m.State();
        if (state != MatchState::Countdown && state != MatchState::Drop && state != MatchState::InMatch) return;
        const Circle soon = m.GetStorm().SafeZoneAt(m.StormTime() + kStormLookahead);
        repathBudget = 8;
        StepAllies(m, dt);
        for (auto& p : m.Players()) {
            if (p.isBot && p.alive) {
                if (frozen) continue;
                Memory& air = Mem(p.id);
                if (!air.started) {   // a bot that is there from the countdown or the drop starts in the sky, like the players
                    air.started = true;
                    air.airborne = state != MatchState::InMatch;
                    if (air.airborne) { air.lift = kSkyHeight; ChooseLanding(m, p, air, soon); }
                }
                if (air.airborne) { Glide(m, p, air, soon, dt); continue; }
                if (state == MatchState::Countdown) continue;
                if (StepRider(m, p, air, soon, dt)) { UpdateStamina(air, dt); continue; }
                Act(m, p, soon, dt);
                FollowGround(m, p, air, dt);
                UpdateStamina(air, dt);
                const Memory& mem = Mem(p.id);
                Memory& mm = Mem(p.id);
                if (m.Clock() >= mm.actUntil && mm.queuedFor > 0 && !m.Stunned(p)) {   // the chest is open: hold up what was inside
                    ShowPose(m, mm, mm.queuedAnim, mm.queuedFor);
                    mm.busyUntil = (std::max)(mm.busyUntil, mm.actUntil);
                    mm.queuedFor = 0;
                }
                if (m.Clock() < mem.actUntil && !m.Stunned(p)) p.anim = static_cast<uint8_t>(mem.actAnim);   // it just used something: show it
            }
        }
    }

    // The hireable allies (shared/ally.h). A free ally stands about (and walks out of the storm); a hired one follows its owner, picks the nearest
    // enemy close to them and fights it the way its kind does, and is brought back if it falls too far behind.
    void StepAllies(Match& m, float dt) {
        if (m.State() != MatchState::InMatch) return;
        for (AllyState& a : m.MutableAllies()) {
            if (!a.alive) continue;
            const AllyDef& def = AllyOf(a.kind);
            const float dps = m.GetStorm().DamagePerSecond(a.pos, m.StormTime());
            if (dps > 0) {
                a.health -= dps * dt * 0.6f;
                if (a.health <= 0) { m.ReleaseAlly(a, true); continue; }
            }
            PlayerState* owner = a.Hired() ? m.Find(a.owner) : nullptr;
            if (a.Hired() && (!owner || !owner->alive)) { m.ReleaseAlly(a, false); owner = nullptr; }
            a.moving = false;
            const float step = def.speed * dt;
            auto walkTo = [&](Vec2 to, float speedScale) {
                const float dx = to.x - a.pos.x, dz = to.z - a.pos.z;
                if (std::hypot(dx, dz) < 1.0f) return;
                if (Advance(m, a.pos, dx, dz, (std::min)(step * speedScale, std::hypot(dx, dz)))) a.moving = true;
                a.rot = FaceAngle(a.pos, to);
            };
            if (!owner) {
                if (dps > 0) walkTo(m.GetStorm().SafeZoneAt(m.StormTime() + 5.0f).center, 1.0f);   // anyone would run from the storm
                continue;
            }
            // Heal (a Zora's gift).
            if (def.healEvery > 0 && m.Clock() >= a.healReadyAt && owner->health < owner->maxHealth * 0.7f && Distance(owner->pos, a.pos) < 600.0f) {
                a.healReadyAt = m.Clock() + def.healEvery;
                owner->health = (std::min)(owner->maxHealth, owner->health + def.healAmount);
                owner->dirty = true;
                a.actUntil = m.Clock() + 0.6f;
                MatchEvent e{MatchEvent::Type::AllyAction};
                e.a = a.index; e.b = owner->id; e.x = owner->pos.x; e.z = owner->pos.z;
                m.PushEvent(e);
            }
            // Pick the nearest enemy that is near both the ally and its owner.
            uint32_t foeId = kNoPlayer;
            Vec2 foePos = {};
            float best = def.range + 350.0f;
            for (const auto& p : m.Players()) {
                if (!p.alive || p.id == owner->id || m.Invulnerable(p)) continue;
                const float d = Distance(p.pos, a.pos);
                if (d < best && Distance(p.pos, owner->pos) < kAllyLeash * 0.8f) { best = d; foeId = p.id; foePos = p.pos; }
            }
            for (const auto& b : m.Bosses()) {
                if (!b.alive || (IsDragonKind(b.kind) && b.y > kDragonAirborneAbove && def.melee)) continue;
                const float d = Distance(b.pos, a.pos) - (IsDragonKind(b.kind) ? kDragonBodyRadius : kBossBodyRadius) * 0.8f;
                if (d < best && Distance(b.pos, owner->pos) < kAllyLeash * 0.8f) { best = d; foeId = b.id; foePos = b.pos; }
            }
            const float ownerDist = Distance(owner->pos, a.pos);
            if (ownerDist > kAllyLeash * 2.0f) {   // left far behind: catch up in a flash, beside the owner
                a.pos = {owner->pos.x + 90.0f, owner->pos.z + 60.0f};
                if (!Walkable(a.pos)) a.pos = owner->pos;
                continue;
            }
            if (foeId != kNoPlayer) {
                const float d = Distance(foePos, a.pos);
                const float want = def.melee ? 90.0f : def.range * 0.75f;
                a.rot = FaceAngle(a.pos, foePos);
                if (d > want) walkTo(foePos, 1.0f);
                const float reach = def.range + (IsBossId(foeId) ? (IsDragonKind(m.FindBoss(foeId)->kind) ? kDragonBodyRadius : kBossBodyRadius) : 0.0f);
                if (d <= reach) m.AllyStrike(a, foeId);
                continue;
            }
            if (ownerDist > kAllyFollowDistance * 1.5f) walkTo(owner->pos, ownerDist > kAllyLeash ? 1.5f : 1.0f);
            else a.rot = FaceAngle(a.pos, owner->pos);
        }
    }

    // Same walk as the bots' (sliding along walls) but for a bare position.
    bool Advance(const Match& m, Vec2& pos, float dx, float dz, float dist) {
        PlayerState tmp;
        tmp.pos = pos;
        const bool moved = Advance(m, tmp, dx, dz, dist, false);
        pos = tmp.pos;
        return moved;
    }

    // How a fight between `me` and `foe` would go: above 1 favours me. Public so tests and the HUD can show it.
    static float Advantage(const Match& m, const PlayerState& me, const PlayerState& foe) {
        const float myDps = EffectiveDpsNow(me) * TotalsOf(me).melee;
        const float theirDps = EffectiveDpsNow(foe) * TotalsOf(foe).melee;
        const float theirReduction = foe.hasShield ? ShieldReduction(foe.shield.item, foe.shield.rarity) : 0.0f;
        const float myReduction = me.hasShield ? ShieldReduction(me.shield.item, me.shield.rarity) : 0.0f;
        float myPool = me.health + HealingInBag(me);
        float theirPool = foe.health + HealingInBag(foe);
        const float timeToKill = theirPool / (std::max)(0.05f, myDps * (1.0f - theirReduction) * TotalsOf(foe).damageTaken);
        const float timeToDie = myPool / (std::max)(0.05f, theirDps * (1.0f - myReduction) * TotalsOf(me).damageTaken);
        float a = timeToDie / (std::max)(0.2f, timeToKill);
        if (m.Stunned(foe)) a *= 2.0f;
        if (m.Stunned(me)) a *= 0.4f;
        if (m.Invulnerable(foe)) a *= 0.2f;
        return a;
    }

  private:
    struct Tuning {
        float skillLo, skillHi;
        float reactLo, reactHi;
        float sight;
        float abilityUse; // chance to take an opportunity to use the ability
        float dodge;      // chance to sidestep an incoming attack
        bool kite, hunt;
    };

    static Tuning TuningFor(BotDifficulty d) {
        switch (d) {
            case BotDifficulty::Easy:   return {0.35f, 0.6f, 0.7f, 1.3f, 650.0f, 0.4f, 0.0f, false, false};
            case BotDifficulty::Hard:   return {0.78f, 0.97f, 0.08f, 0.22f, 1300.0f, 1.0f, 0.9f, true, true};
            case BotDifficulty::Normal: break;
        }
        return {0.55f, 0.9f, 0.25f, 0.55f, kSight, 0.85f, 0.55f, true, true};
    }

    struct Memory {
        float skill = 0.7f, aggression = 0.5f, caution = 0.5f, greed = 0.5f, reaction = 0.4f;
        uint32_t target = kNoPlayer;
        float acquiredAt = 0;
        Vec2 lastSeen = {};
        float lastSeenAt = -1e9f;
        float lastHealth = kMaxHealth;
        float alertUntil = 0;
        float fleeUntil = 0;
        float strafeDir = 1, strafeFlipAt = 0;
        float abilityTryAt = 0;
        int lootIdx = -1;
        float lootEvalAt = 0;
        float switchAt = 0;
        Vec2 wander = {};
        bool hasWander = false;
        std::vector<Vec2> path;
        size_t pathIdx = 0;
        std::vector<NavGrid::Stop> route;   // a route up to a floor or a roof (or down from one), with the height of each stop
        size_t routeIdx = 0;
        bool upper = false;                 // standing on an upper floor, a ramp or a roof (levelY is the height of its feet)
        float levelY = 0;
        Vec2 pathGoal = {};
        float repathAt = 0;
        Vec2 progressPos = {};
        float progressAt = -1;
        float unstickUntil = 0;
        Vec2 unstickDir = {};
        // combat reflexes
        float rollUntil = 0;           // mid-roll until this time
        Vec2 rollDir = {};
        Vec2 prevFoePos = {};          // where the target was last tick, for its velocity
        Vec2 foeVel = {};
        float prevFoeAt = -1;
        float dodgeCycle = -1;         // the foe's attackReadyAt we last decided about, so each swing gets one decision
        bool dodgeThisSwing = false;
        float hazardNoticeAt = -1;     // when the bot will have noticed the blast ring or fire cone it is standing in
        Anim actAnim = Anim::Idle;      // the pose of the item it just used, held for a moment over whatever it is doing
        float actUntil = 0;
        // playing like a person
        Anim rollAnim = Anim::Roll;    // which dodge it is doing: a roll, a side hop or a back flip
        float busyUntil = 0;           // standing still for a chest, a find held up, a look round or a taunt
        Anim queuedAnim = Anim::Idle;  // a pose to show once the current one ends (a chest's find, held up)
        float queuedFor = 0;
        int combo = 0;                 // swings in a row, for the finishing spin
        float lastSwingAt = -10;
        int kills = 0;                 // to notice a fresh elimination (and maybe celebrate it)
        float nextHopAt = 0;           // the earliest it jumps again
        int propIdx = -1;              // the bush or rock it is walking to
        float propEvalAt = 0;
        bool paused = false;           // wandering: stopping for a look round at each spot
        // getting about like a player: the skydive, sprinting, climbing
        bool started = false;          // has been seen by Step (decides whether it starts in the sky)
        bool airborne = false;         // still skydiving
        Vec2 landAt = {};              // where it is gliding to
        bool diving = false;
        float lift = 0;                // height above the scene's floor, sent to clients as its y: a block or boulder top, a fall, the sky
        float vy = 0;                  // falling speed off a ledge
        float lastFloor = 0;
        bool haveFloor = false;
        float stamina = 1, staminaRest = 0;
        bool sprintWish = false;       // this tick's plan would like a sprint
        bool sprintUrgent = false;     // ... and it is running for its life (the storm, a losing fight), so it may spend the last of the bar
        bool sprinting = false, sprintedThisTick = false;
        Vec2 cover = {};               // a spot out of sight of the foe, behind scenery or a rise
        bool haveCover = false;
        float coverEvalAt = 0;
        uint32_t coverFrom = kNoPlayer;
        Vec2 perch = {};               // higher ground to shoot from
        bool havePerch = false;
        float perchEvalAt = 0;
        // carts
        float cartLove = 0.5f;          // how keen it is on driving
        int cartIdx = -1;               // the cart it is walking to (or riding)
        Seat cartSeat = Seat::Driver;   // ... and which seat it wants
        float cartEvalAt = 0;
        bool riding = false;            // it was in a cart last tick (to notice getting out)
        Vec2 cartGoal = {};             // where it is driving to
        bool cartGoalSet = false;
        uint32_t ramTarget = kNoPlayer; // someone it is running down
        float ramUntil = 0;
        float cartStuckAt = -1;         // driving but getting nowhere since then
        Vec2 cartProgress = {};
        float cartStillSince = -1;      // riding along in a cart that has stopped
        float cartBoardedAt = 0;
        std::vector<Vec2> cartPath;
        Vec2 cartPathGoal = {};
        size_t cartPathIdx = 0;
        float cartRepathAt = 0;
    };

    Rng rng;
    BotDifficulty difficulty = BotDifficulty::Normal;
    bool frozen = false;   // the Sandbox test map: bots stand still (SetFrozen)
    std::shared_ptr<const NavGrid> nav;
    std::unordered_map<uint32_t, Memory> memory;
    int repathBudget = 0;
    std::vector<Prop> props;                          // the scenery (SetProps)
    std::vector<bool> propGone;                       // broken by somebody, or about to be by a bot
    std::vector<std::pair<uint32_t, size_t>> smashes; // bot id, prop index: waiting for the server to break them
    struct Stand { Vec2 at; float half; float top; bool square; };   // scenery that can be stood on: a block (square) or a low boulder (round)
    std::vector<Stand> stands;
    std::unordered_map<int64_t, std::vector<int>> standBuckets;
    static constexpr float kBucket = 256.0f;
    static int Bucket(float v) { return static_cast<int>(std::floor(v / kBucket)); }
    static int64_t BucketKey(int bx, int bz) { return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(bx)) << 32) | static_cast<uint32_t>(bz)); }

    Memory& Mem(uint32_t id) {
        auto it = memory.find(id);
        if (it == memory.end()) {
            const Tuning t = TuningFor(difficulty);
            Memory mem;
            mem.skill = t.skillLo + static_cast<float>(rng.Unit()) * (t.skillHi - t.skillLo);
            mem.reaction = t.reactLo + static_cast<float>(rng.Unit()) * (t.reactHi - t.reactLo);
            mem.aggression = 0.15f + 0.85f * static_cast<float>(rng.Unit());
            mem.caution = 0.3f + 0.7f * static_cast<float>(rng.Unit());
            mem.greed = 0.3f + 0.7f * static_cast<float>(rng.Unit());
            mem.strafeDir = rng.Unit() < 0.5 ? -1.0f : 1.0f;
            mem.cartLove = std::fmod(static_cast<float>(id) * 0.6180339f, 1.0f);   // (from the id, so the other draws stay as they were)
            it = memory.emplace(id, mem).first;
        }
        return it->second;
    }

    // The pose a bot shows for a moment after using something, like a player would: swing, loose an arrow, throw, drink, play a song, cast.
    static Anim PoseForWeapon(const WeaponStats& w, ItemId item) {
        if (!w.ranged) return Anim::Attack;
        const AmmoKind a = AmmoUsedBy(item);
        return (a == AmmoKind::Arrows || a == AmmoKind::Seeds) ? Anim::Shoot : Anim::Throw;
    }
    void ShowPose(Match& m, Memory& mem, Anim pose, float seconds = 0.45f) { mem.actAnim = pose; mem.actUntil = m.Clock() + seconds; }
    // Attack, and show the right pose if the attack was allowed. Swords are swung the ways a player swings them: a leaping jump slash to
    // open a fight, ordinary slashes after that, and a spin attack when it is surrounded or to finish a combo. `dist` is how far the target is.
    AttackResult BotAttack(Match& m, PlayerState& p, Memory& mem, uint32_t target, bool hit, float dist = 0.0f) {
        const WeaponStats w = Match::StatsOf(p);
        const ItemId item = p.weapon.item;
        if (m.Clock() < p.attackReadyAt) return {};
        // Pick the move first: a spin attack really catches everyone close, and a jump slash really hits harder.
        Anim pose = PoseForWeapon(w, item);
        float seconds = 0.45f;
        AttackStyle style = AttackStyle::Normal;
        const int combo = m.Clock() - mem.lastSwingAt > 1.4f ? 0 : mem.combo + 1;   // 0: the first swing of an exchange
        if (pose == Anim::Attack) {
            int crowd = 0;
            for (const auto& o : m.Players()) if (o.alive && o.id != p.id && Distance(o.pos, p.pos) <= w.range * 1.1f) crowd++;
            if (crowd >= 2 && rng.Unit() < 0.55f * mem.skill + 0.2f) { pose = Anim::SpinAttack; style = AttackStyle::Spin; seconds = 0.75f; }
            else if (combo == 0 && dist > w.range * 0.5f && rng.Unit() < 0.25f + 0.4f * mem.aggression) { pose = Anim::JumpSlash; style = AttackStyle::JumpSlash; seconds = 0.7f; }
            else if (combo >= 3 && rng.Unit() < 0.3f * mem.skill) { pose = Anim::SpinAttack; style = AttackStyle::Spin; seconds = 0.75f; }
        }
        const AttackResult r = m.Attack(p.id, target, hit, style);
        if (!r.ok) return r;
        if (pose == Anim::Attack || pose == Anim::JumpSlash || pose == Anim::SpinAttack) {
            mem.combo = style == AttackStyle::Spin ? 0 : combo;
            mem.lastSwingAt = m.Clock();
        }
        ShowPose(m, mem, pose, seconds);
        return r;
    }

    // Which way a dodge across the foe's line goes, as the game shows it: a side hop (left or right of where the bot faces), now and then a back
    // flip when it is getting away, otherwise a roll.
    Anim DodgeAnimFor(const Memory& mem, bool away) {
        const double r = rng.Unit();
        if (away && r < 0.45) return Anim::Backflip;
        if (r < 0.75) return mem.strafeDir > 0 ? Anim::HopR : Anim::HopL;
        return Anim::Roll;
    }

    // ---- small helpers -----------------------------------------------------------------------------------------------

    static float EffectiveDps(const Equipped& e) {
        const WeaponStats w = WeaponOf(e.item);
        float dps = WeaponDps(e.item, e.rarity);
        if (w.ranged) dps *= 1.15f;
        switch (w.effect) { // status effects are worth more than their raw numbers
            case WeaponEffect::Burn: dps *= 1.2f; break;
            case WeaponEffect::Freeze: dps *= 1.3f; break;
            case WeaponEffect::Stun: dps *= 1.25f; break;
            case WeaponEffect::Pull: dps *= 1.5f; break;
            case WeaponEffect::PierceShield: dps *= 1.15f; break;
            case WeaponEffect::None: break;
        }
        if (w.splashRadius > 0) dps *= 1.1f;
        return dps;
    }

    // What the weapon in hand is worth right now: with no ammo it is only a club.
    static float EffectiveDpsNow(const PlayerState& p) {
        return Match::HasAmmo(p, p.weapon.item) ? EffectiveDps(p.weapon) : WeaponDps(ItemId::BasicSword, Rarity::Common);
    }

    // How much a bot wants ammo of this kind: only if it carries something that spends it, and more the emptier that gets.
    static float AmmoWant(const PlayerState& p, AmmoKind k) {
        bool uses = AmmoUsedBy(p.weapon.item) == k;
        for (const Equipped& e : p.reserve) uses |= AmmoUsedBy(e.item) == k;
        const int cap = Match::AmmoCapOf(p, k), have = p.ammo[static_cast<int>(k)];
        if (have >= cap) return 0.0f;
        return uses ? 0.35f + 0.9f * static_cast<float>(cap - have) / static_cast<float>(cap) : 0.04f;
    }

    static float HealingInBag(const PlayerState& p) {
        float h = 0;
        for (const Equipped& e : p.potions) h += PotionHeal(e.item, e.rarity) * 0.8f;
        return h;
    }

    static const MiniBoss* NearestBoss(const Match& m, const PlayerState& p, float range) {
        const MiniBoss* best = nullptr;
        float bestD = range;
        for (const MiniBoss& b : m.Bosses()) {
            if (!b.alive || BossHidden(b.mode)) continue;
            const float d = Distance(p.pos, b.pos);
            if (d < bestD) { bestD = d; best = &b; }
        }
        return best;
    }

    static bool HasFairy(const PlayerState& p) {
        for (const Equipped& e : p.potions) if (PotionOf(e.item).revive) return true;
        return false;
    }

    static int16_t FaceAngle(Vec2 from, Vec2 to) {
        return static_cast<int16_t>(static_cast<int32_t>(std::atan2(to.x - from.x, to.z - from.z) * (32768.0f / 3.14159265358979f)));
    }

    static Vec2 Away(Vec2 from, Vec2 foe, float dist) {
        const float dx = from.x - foe.x, dz = from.z - foe.z;
        const float len = std::hypot(dx, dz);
        if (len < 1e-3f) return {from.x + dist, from.z};
        return {from.x + dx / len * dist, from.z + dz / len * dist};
    }

    bool Walkable(Vec2 p) const { return !nav || nav->Walkable(p); }
    bool Standable(Vec2 p) const { return !nav || nav->Standable(p); }

    // ---- the ground under a bot -----------------------------------------------------------------------------------------------

    // The top of the block or low boulder at p (0 when there is none): what a bot standing there is lifted onto.
    float LiftAt(Vec2 p) const {
        auto it = standBuckets.find(BucketKey(Bucket(p.x), Bucket(p.z)));
        if (it == standBuckets.end()) return 0.0f;
        float top = 0;
        for (int i : it->second) {
            const Stand& st = stands[static_cast<size_t>(i)];
            const bool in = st.square ? std::fabs(p.x - st.at.x) <= st.half && std::fabs(p.z - st.at.z) <= st.half : Distance(p, st.at) <= st.half;
            if (in) top = (std::max)(top, st.top);
        }
        return top;
    }
    // Where a bot standing at p has its feet (0 for flat maps).
    float FeetAt(Vec2 p) const { return (nav ? nav->FloorAt(p) : 0.0f) + (nav ? LiftAt(p) : 0.0f); }
    // Eye height of anyone, for who can see whom. Humans send their own height; a bot's y is its height above the floor.
    float EyeOf(const PlayerState& o) const {
        if (!nav) return 45.0f;
        return (o.isBot ? nav->FloorAt(o.pos) + o.y : (nav->HasHeights() ? o.y : FeetAt(o.pos))) + 45.0f;
    }
    // Can someone with their eyes at (a, ya) see (b, yb)? Hills and the taller scenery in between hide them.
    bool Sees(Vec2 a, float ya, Vec2 b, float yb) const {
        if (!nav) return true;
        const float len = Distance(a, b);
        const int steps = static_cast<int>(len / 40.0f);
        for (int i = 1; i < steps; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            if (t * len < 45.0f || (1.0f - t) * len < 45.0f) continue;   // what you are standing on doesn't hide you
            const Vec2 at = {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
            if (nav->TopAt(at) > ya + (yb - ya) * t) return false;
        }
        return true;
    }
    bool CanSee(const PlayerState& p, const PlayerState& o) const { return Sees(p.pos, EyeOf(p), o.pos, EyeOf(o)); }

    // Can a bot step from a to b? Without a grid anywhere goes; with one, b must be ground or scenery to stand on, and the height between
    // them no more than a clamber up (NavGrid::kClimbUp) or a safe drop down (NavGrid::kDropDown).
    bool CanStep(Vec2 a, Vec2 b) const {
        if (!nav) return true;
        if (!nav->Standable(b)) return false;
        return NavGrid::StepOk(FeetAt(a), FeetAt(b));
    }

    // After moving: keep the bot's height in step with the ground. Up a ledge or onto a block is a jump (shown, and lifted on every
    // client); off one it falls, so it doesn't snap down the side of a cliff.
    void FollowGround(Match& m, PlayerState& p, Memory& mem, float dt) {
        if (!nav) { p.y = 0; return; }
        const float floorY = nav->FloorAt(p.pos);
        float target = LiftAt(p.pos);
        if (mem.upper) {   // on a floor, a ramp or a roof: the nearest of them holds the bot up; with none under it, it falls
            const int node = nav->UpperNear(p.pos, mem.levelY, 55.0f, 70.0f);
            if (node < 0) mem.upper = false;
            else {
                const float want = nav->UpperY(node);
                mem.levelY += (std::max)(-700.0f * dt, (std::min)(700.0f * dt, want - mem.levelY));
                target = (std::max)(target, mem.levelY - floorY);
            }
        }
        if (mem.haveFloor) {
            const float dF = floorY - mem.lastFloor;
            if (dF < -NavGrid::kStepUp) mem.lift -= dF;                    // walked off a ledge: still up where it was, for now
            else if (dF > NavGrid::kStepUp) Jump(m, mem);                 // up a ledge of the ground itself
        }
        mem.lastFloor = floorY;
        mem.haveFloor = true;
        if (target > mem.lift + 1.0f) {
            if (target - mem.lift > NavGrid::kStepUp * 0.5f) Jump(m, mem);   // onto a block or a boulder
            mem.lift = (std::min)(target, mem.lift + 700.0f * dt);
            mem.vy = 0;
        } else if (mem.lift > target + 0.5f) {
            mem.vy -= 1400.0f * dt;                                       // falling
            mem.lift += mem.vy * dt;
            if (mem.lift <= target) { mem.lift = target; mem.vy = 0; }
        } else {
            mem.lift = target; mem.vy = 0;
        }
        p.y = mem.lift;
    }

    // ---- the skydive ----------------------------------------------------------------------------------------------------------

    // Where to land: like a player, a bot glides for a spot it can reach with chests close by, inside the first safe zone. Greedy bots want
    // the most loot; aggressive ones don't mind a crowd (a hot drop), the rest look for a spot no other bot is heading for.
    void ChooseLanding(const Match& m, const PlayerState& p, Memory& mem, const Circle& zone) {
        const float reach = kAirSpeed * (kSkyHeight / kGlideSpeed) * 0.8f;
        const auto& loot = m.Loot();
        Vec2 best = p.pos;
        float bestScore = -1e9f;
        auto consider = [&](Vec2 c, float bonus) {
            const float d = Distance(p.pos, c);
            if (d > reach || !zone.Contains(c) || !Standable(c)) return;
            int chests = 0;
            for (const LootEntry& l : loot) if (!l.taken && l.spawn.container && Distance(l.spawn.pos, c) < 450.0f) chests++;
            int crowd = 0;
            for (const auto& [id, other] : memory) if (id != p.id && other.airborne && Distance(other.landAt, c) < 500.0f) crowd++;
            const float score = (bonus + static_cast<float>(chests)) * (0.6f + mem.greed) * (0.75f + 0.5f * static_cast<float>(rng.Unit()))
                                / (1.0f + static_cast<float>(crowd) * (1.3f - mem.aggression)) - 0.6f * d / reach;
            if (score > bestScore) { bestScore = score; best = c; }
        };
        for (const LootEntry& l : loot) if (!l.taken && l.spawn.container) consider(l.spawn.pos, 0.5f);
        for (int i = 0; i < 6; i++) {   // and a few spots out in the open, for the bots that want to be left alone
            const float a = static_cast<float>(rng.Unit() * 6.283185307179586), d = reach * std::sqrt(static_cast<float>(rng.Unit()));
            consider({p.pos.x + std::sin(a) * d, p.pos.z + std::cos(a) * d}, 0.2f + 0.8f * (1.0f - mem.aggression));
        }
        if (nav && !nav->Standable(best)) { Vec2 snapped; if (nav->Snap(best, &snapped, true)) best = snapped; }
        mem.landAt = best;
    }

    // Hang in the sky through the countdown, then glide to the landing spot: dive once diving still gets there in time (a good bot dives
    // the moment it can; a poorer one leaves it later), glide otherwise. Still up when the drop ends: straight down.
    void Glide(Match& m, PlayerState& p, Memory& mem, const Circle& zone, float dt) {
        p.anim = static_cast<uint8_t>(Anim::Idle);
        p.y = mem.lift;
        if (m.State() == MatchState::Countdown) return;
        if (!zone.Contains(mem.landAt) && rng.Unit() < 0.05f) ChooseLanding(m, p, mem, zone);
        const float d = Distance(p.pos, mem.landAt);
        const float timeToReach = d / kAirSpeed;
        float fall = kLateFallSpeed;
        if (m.State() == MatchState::Drop) {
            mem.diving = timeToReach <= mem.lift / kDiveSpeed * (0.55f + 0.45f * mem.skill);
            fall = mem.diving ? kDiveSpeed : kGlideSpeed;
        }
        if (d > 1.0f) {
            const float step = (std::min)(d, kAirSpeed * dt);
            p.pos = {p.pos.x + (mem.landAt.x - p.pos.x) / d * step, p.pos.z + (mem.landAt.z - p.pos.z) / d * step};
            p.rot = FaceAngle(p.pos, mem.landAt);
        }
        const float ground = nav ? LiftAt(p.pos) : 0.0f;
        mem.lift -= fall * dt;
        if (mem.lift <= ground) {   // touched down
            if (nav && !nav->Standable(p.pos)) { Vec2 snapped; if (nav->Snap(p.pos, &snapped, true)) p.pos = snapped; }
            mem.airborne = false;
            mem.lift = nav ? LiftAt(p.pos) : 0.0f;
            mem.vy = 0;
            mem.haveFloor = false;
            mem.upper = false;
        }
        p.y = mem.lift;
    }

    // ---- sprinting -------------------------------------------------------------------------------------------------------------

    // Is the target running away from it (as a fleeing player does)?
    static bool GettingAway(const Memory& mem, const PlayerState& p, const PlayerState& foe) {
        const float dx = foe.pos.x - p.pos.x, dz = foe.pos.z - p.pos.z, len = (std::max)(1.0f, std::hypot(dx, dz));
        return (mem.foeVel.x * dx + mem.foeVel.z * dz) / len > kRunSpeed * 0.6f;
    }

    // Ask for a sprint on this tick's running. Easy bots only think of it when they must.
    static void WantSprint(Memory& mem, const Tuning& tune, bool urgent) {
        if (!urgent && !tune.hunt) return;
        mem.sprintWish = true;
        mem.sprintUrgent |= urgent;
    }

    // The speed multiplier for a stretch of running this tick: a sprint if the plan wants one and there is stamina (it takes a bit more to start
    // one than to keep going), plain running otherwise.
    float SprintScale(Memory& mem) {
        if (!mem.sprintWish) return 1.0f;
        const float keep = mem.sprintUrgent ? 0.0f : 0.35f;   // a bot keeps some of the bar for getting away, unless this is getting away
        const bool can = mem.sprinting ? mem.stamina > keep : mem.stamina >= kSprintMinStamina + keep;
        if (!can) return 1.0f;
        mem.sprintedThisTick = true;
        return kSprintMult;
    }
    // The same bar players have: drains while sprinting, refills after a short rest.
    static void UpdateStamina(Memory& mem, float dt) {
        mem.sprinting = mem.sprintedThisTick;
        mem.sprintedThisTick = false;
        mem.sprintWish = false;
        mem.sprintUrgent = false;
        if (mem.sprinting) {
            mem.stamina = (std::max)(0.0f, mem.stamina - dt / kSprintSeconds);
            mem.staminaRest = kStaminaRest;
        } else if (mem.staminaRest > 0) {
            mem.staminaRest -= dt;
        } else {
            mem.stamina = (std::min)(1.0f, mem.stamina + dt / kStaminaRefill);
        }
    }

    // Move up to `dist` units along (dx, dz), sliding along walls: if the straight step is blocked try each side. Returns true if moved.
    // A `climber` (a bot) may go up and down ledges and onto scenery as CanStep allows; others (allies) keep to open ground.
    bool Advance(const Match& m, PlayerState& p, float dx, float dz, float dist, bool climber = true) {
        const float len = std::hypot(dx, dz);
        if (len < 1e-4f) return false;
        dx /= len; dz /= len;
        float upperLevel = std::numeric_limits<float>::quiet_NaN();
        if (climber && nav && nav->HasUpper()) { auto it = memory.find(p.id); if (it != memory.end() && it->second.upper) upperLevel = it->second.levelY; }
        static const float kTurns[5] = {0.0f, 0.6f, -0.6f, 1.2f, -1.2f};
        for (float turn : kTurns) {
            const float c = std::cos(turn), s = std::sin(turn);
            const float ex = dx * c - dz * s, ez = dx * s + dz * c;
            Vec2 next = {p.pos.x + ex * dist, p.pos.z + ez * dist};
            const Circle& map = m.MapCircle();
            const float off = Distance(next, map.center);
            if (off > map.radius - 20.0f) {
                next = {map.center.x + (next.x - map.center.x) / off * (map.radius - 20.0f), map.center.z + (next.z - map.center.z) / off * (map.radius - 20.0f)};
            }
            if (upperLevel == upperLevel) {   // up on a floor or a roof: only where there is floor
                if (nav->UpperNear(next, upperLevel, 55.0f, 70.0f) < 0) continue;
            } else if (climber ? !CanStep(p.pos, next) : !Walkable(next)) continue;
            p.pos = next;
            return true;
        }
        return false;
    }

    // Walk toward `goal`, following an A* path when the way isn't a clear line. `face` keeps the bot looking at a point (for
    // strafing) instead of where it is going.
    void Steer(Match& m, PlayerState& p, Memory& mem, Vec2 goal, float dt, float speedScale = 1.0f, const Vec2* face = nullptr) {
        const float sprint = speedScale >= 0.9f ? SprintScale(mem) : 1.0f;
        const float step = kRunSpeed * m.SpeedMultiplier(p) * speedScale * sprint * dt;
        Vec2 aim = goal;
        bool routing = false, aimUp = false;
        float aimY = 0;
        if (m.Clock() < mem.unstickUntil) {
            aim = {p.pos.x + mem.unstickDir.x * 200.0f, p.pos.z + mem.unstickDir.z * 200.0f};
        } else if (nav && nav->HasUpper() && Distance(p.pos, goal) > NavGrid::kCell * 0.4f && (mem.upper || nav->UpperGoal(goal, nullptr))) {
            // Up a ramp to a floor or a roof, along it, or back down: a route whose stops carry their heights.
            float goalY = 0;
            const bool toUp = nav->UpperGoal(goal, &goalY);
            const bool goalMoved = Distance(goal, mem.pathGoal) > 120.0f;
            if ((mem.routeIdx >= mem.route.size() || goalMoved || m.Clock() >= mem.repathAt) && repathBudget > 0) {
                repathBudget--;
                mem.pathGoal = goal;
                mem.routeIdx = 0;
                mem.repathAt = m.Clock() + 1.5f + static_cast<float>(rng.Unit());
                const float startY = mem.upper ? mem.levelY : nav->FloorAt(p.pos);
                if (!nav->FindRoute(p.pos, startY, mem.upper, goal, goalY, toUp, mem.route)) mem.route.clear();
            }
            while (mem.routeIdx < mem.route.size() && Distance(p.pos, mem.route[mem.routeIdx].p) < NavGrid::kCell * 0.45f) mem.routeIdx++;
            if (mem.routeIdx < mem.route.size()) {
                const NavGrid::Stop& stop = mem.route[mem.routeIdx];
                aim = stop.p;
                aimUp = stop.upper;
                aimY = stop.y;
                routing = true;
                if (stop.upper && !mem.upper && Distance(p.pos, stop.p) < NavGrid::kCell * 1.6f) { mem.upper = true; mem.levelY = nav->FloorAt(p.pos); }   // onto the ramp
            }
        } else if (nav && Distance(p.pos, goal) > NavGrid::kCell * 1.2f) {
            const bool goalMoved = Distance(goal, mem.pathGoal) > 120.0f;
            const bool needPath = mem.pathIdx >= mem.path.size() || goalMoved;
            if ((needPath || m.Clock() >= mem.repathAt) && repathBudget > 0) {
                repathBudget--;
                mem.pathGoal = goal;
                mem.pathIdx = 0;
                mem.repathAt = m.Clock() + 1.5f + static_cast<float>(rng.Unit());
                if (!nav->FindPath(p.pos, goal, mem.path, true)) mem.path.clear(); // unreachable: fall back to walking straight at it
            }
            while (mem.pathIdx < mem.path.size() && Distance(p.pos, mem.path[mem.pathIdx]) < NavGrid::kCell * 0.6f) mem.pathIdx++;
            if (mem.pathIdx < mem.path.size()) aim = mem.path[mem.pathIdx];
        }

        const float dx = aim.x - p.pos.x, dz = aim.z - p.pos.z;
        const Vec2 before = p.pos;
        const bool moved = Advance(m, p, dx, dz, (std::min)(step, std::hypot(dx, dz)));
        if (mem.upper) {   // keep the feet on the route: up the ramp as fast as the bot walks along it
            const float rate = Distance(before, p.pos) * 1.4f + 1.0f;
            if (routing) mem.levelY += (std::max)(-rate, (std::min)(rate, aimY - mem.levelY));
            if (!aimUp && mem.levelY - nav->FloorAt(p.pos) <= 8.0f) mem.upper = false;   // back on the ground
        }
        p.rot = face ? FaceAngle(p.pos, *face) : FaceAngle(p.pos, aim);
        p.anim = static_cast<uint8_t>(moved ? (speedScale < 0.9f ? Anim::Walk : sprint > 1.0f ? Anim::Sprint : Anim::Run) : Anim::Idle);
        if (moved && speedScale >= 0.9f && m.Clock() >= mem.nextHopAt && rng.Unit() < 0.0015f + 0.002f * mem.aggression) Hop(m, mem);

        // Stuck check: wanted to move for a second but barely got anywhere (a wall the grid doesn't know about): pick a new heading.
        if (mem.progressAt < 0) {
            mem.progressPos = p.pos;
            mem.progressAt = m.Clock();
        } else if (m.Clock() - mem.progressAt >= 1.0f) {
            if (Distance(p.pos, mem.progressPos) < kRunSpeed * 0.25f && Distance(p.pos, goal) > 60.0f) {
                const float a = static_cast<float>(rng.Unit() * 6.283185307179586);
                mem.unstickDir = {std::sin(a), std::cos(a)};
                mem.unstickUntil = m.Clock() + 0.6f;
                mem.path.clear();
                Hop(m, mem);   // what a player does at something in the way
            }
            mem.progressPos = p.pos;
            mem.progressAt = m.Clock();
        }
    }

    // Sidestep around `foe` while keeping distance `want` from it. Always faces the foe.
    void Fight(Match& m, PlayerState& p, Memory& mem, const PlayerState& foe, float dist, float want, float dt, float speedScale) {
        float toX = foe.pos.x - p.pos.x, toZ = foe.pos.z - p.pos.z;
        const float len = (std::max)(1e-3f, std::hypot(toX, toZ));
        toX /= len; toZ /= len;
        float radial = 0;                         // + toward the foe, - away
        if (dist > want + 10.0f) radial = 1.0f;
        else if (dist < want * 0.5f) radial = -0.8f;
        if (m.Clock() >= mem.strafeFlipAt) {
            mem.strafeFlipAt = m.Clock() + 0.7f + static_cast<float>(rng.Unit()) * 1.1f;
            if (rng.Unit() < 0.6) mem.strafeDir = -mem.strafeDir;
        }
        const float strafe = 0.55f * mem.strafeDir;
        // With no navigation grid the foe is the only target, so walk at it directly; with one, chase through the pathfinder.
        const bool far = dist > want * 1.8f && radial > 0;
        if (far) {
            Steer(m, p, mem, foe.pos, dt, speedScale, &foe.pos);
            return;
        }
        const float vx = toX * radial - toZ * strafe, vz = toZ * radial + toX * strafe;
        const float step = kRunSpeed * m.SpeedMultiplier(p) * speedScale * dt;
        const bool moved = Advance(m, p, vx, vz, step);
        if (!moved) mem.strafeDir = -mem.strafeDir;
        p.rot = FaceAngle(p.pos, foe.pos);
        // The footwork of a Z-targeting player: shuffles sideways round the foe, backs off, or squares up.
        Anim a = Anim::Stance;
        if (moved) a = radial > 0.3f ? Anim::Run : radial < -0.3f ? Anim::Back : (mem.strafeDir > 0 ? Anim::SideR : Anim::SideL);
        p.anim = static_cast<uint8_t>(a);
    }

    // ---- reflexes: rolling and getting out of the way ----------------------------------------------------------------------------

    // Roll in direction (dx, dz): a fast hop of about 140 units during which hits miss. Returns false if a roll isn't possible right now.
    bool RollToward(Match& m, PlayerState& p, Memory& mem, float dx, float dz) {
        const float len = std::hypot(dx, dz);
        if (len < 1e-4f || !m.StartRoll(p.id)) return false;
        mem.rollDir = {dx / len, dz / len};
        mem.rollUntil = m.Clock() + Match::kRollSeconds;
        mem.rollAnim = Anim::Roll;
        mem.actUntil = 0;   // a dodge cancels whatever pose was showing
        return true;
    }

    // Blast rings and the dragon's fire cone: get out of them. Short-range escapes are walked, close calls are rolled. Easy bots notice late.
    bool AvoidHazards(Match& m, PlayerState& p, Memory& mem, float dt, const Tuning& tune) {
        const float now = m.Clock();
        Vec2 away = {0, 0};
        float urgency = 1e9f;   // seconds until it goes off
        bool inDanger = false;
        for (const Strike& st : m.Strikes()) {
            if (st.applied || Distance(p.pos, st.at) > st.radius + 70.0f) continue;
            inDanger = true;
            urgency = (std::min)(urgency, st.hitAt - now);
            const float dx = p.pos.x - st.at.x, dz = p.pos.z - st.at.z, d = (std::max)(1.0f, std::hypot(dx, dz));
            away.x += dx / d; away.z += dz / d;
            if (d < 2.0f) { away.x += 1.0f; }
        }
        for (const MiniBoss& b : m.Bosses()) {
            if (!b.alive || !IsDragonKind(b.kind) || b.mode != DragonMode::Breath) continue;
            const float dx = p.pos.x - b.pos.x, dz = p.pos.z - b.pos.z, d = std::hypot(dx, dz);
            if (d > kDragonBreathRange + 80.0f) continue;
            const float face = static_cast<float>(b.rot) * (3.14159265f / 32768.0f);
            float off = std::atan2(dx, dz) - face;
            while (off > 3.14159265f) off -= 6.2831853f;
            while (off < -3.14159265f) off += 6.2831853f;
            if (std::fabs(off) > kDragonBreathHalfAngle + 0.25f) continue;
            inDanger = true;
            urgency = (std::min)(urgency, 0.3f);
            const float side = off >= 0 ? 1.0f : -1.0f;               // leave the cone by the nearer edge
            away.x += std::cos(face) * side; away.z += -std::sin(face) * side;
        }
        for (const Match::FartCloud& c : m.FartClouds()) {   // Lilo's toxic cloud: walk out of it
            if (now >= c.until || Distance(p.pos, c.at) > kFartCloudRadius) continue;
            inDanger = true;
            urgency = (std::min)(urgency, 2.0f);
            const float dx = p.pos.x - c.at.x, dz = p.pos.z - c.at.z, d = (std::max)(1.0f, std::hypot(dx, dz));
            away.x += dx / d; away.z += dz / d;
            if (d < 2.0f) { away.x += 1.0f; }
        }
        if (!inDanger) { mem.hazardNoticeAt = -1; return false; }
        if (mem.hazardNoticeAt < 0) mem.hazardNoticeAt = now + mem.reaction * 0.6f;
        if (now < mem.hazardNoticeAt) return false;                   // hasn't noticed yet
        if (tune.dodge <= 0 && urgency > 0.8f) return false;           // Easy bots only react at the last moment
        const float len = std::hypot(away.x, away.z);
        if (len < 1e-3f) return false;
        if (urgency < 0.5f && m.CanRoll(p) && rng.Unit() < 0.4f + 0.6f * tune.dodge && RollToward(m, p, mem, away.x, away.z)) {
            p.anim = static_cast<uint8_t>(Anim::Roll);
            return true;
        }
        const float step = kRunSpeed * m.SpeedMultiplier(p) * dt;
        Advance(m, p, away.x, away.z, step);
        p.rot = FaceAngle(p.pos, {p.pos.x + away.x, p.pos.z + away.z});
        p.anim = static_cast<uint8_t>(Anim::Run);
        return true;
    }

    // ---- perception and targeting -----------------------------------------------------------------------------------------

    PlayerState* ChooseTarget(Match& m, PlayerState& p, Memory& mem, float sight, bool xray) {
        PlayerState* best = nullptr;
        float bestScore = 0;
        for (auto& o : m.Players()) {
            if (&o == &p || !o.alive) continue;
            const float d = Distance(p.pos, o.pos);
            if (d > sight) continue;
            if (!xray && d > 250.0f && !CanSee(p, o)) continue;   // behind a boulder or over a hill (close by, it is heard)
            float adv = (std::min)(3.0f, (std::max)(0.3f, Advantage(m, p, o)));
            float score = adv * (1.0f + (1.0f - o.health / o.maxHealth) * 0.6f) / (d + 100.0f);
            if (o.id == mem.target) score *= 1.3f; // stick with the current target unless something is clearly better
            if (!o.isBot) score *= 1.1f;           // humans are the more interesting prey
            if (score > bestScore) { bestScore = score; best = &o; }
        }
        return best;
    }

    // ---- looting -------------------------------------------------------------------------------------------------------------

    static float AbilityWorth(ItemId id, Rarity r) {
        float base = 0.5f;
        switch (id) {
            case ItemId::NayrusLove: base = 1.0f; break;
            case ItemId::Hookshot: case ItemId::Longshot: base = 0.9f; break;
            case ItemId::ShockwaveGrenade: base = 0.7f; break;
            case ItemId::DinsFire: case ItemId::RequiemOfSpirit: case ItemId::SongOfStorms: case ItemId::SongOfTime: base = 0.85f; break;
            case ItemId::FaroresWind: case ItemId::PreludeOfLight: case ItemId::SerenadeOfWater: base = 0.75f; break;
            case ItemId::LensOfTruth: case ItemId::MagicBeans: case ItemId::FairyOcarina: base = 0.35f; break;
            default: break;
        }
        return base * (0.75f + 0.08f * static_cast<float>(static_cast<int>(r)));
    }

    float LootValue(const PlayerState& p, const LootSpawn& s) const {
        switch (KindOf(s.item)) {
            case ItemKind::Weapon: {
                const float mine = EffectiveDps(p.weapon), v = EffectiveDps({s.item, s.rarity});
                if (v > mine * 1.1f) return (v - mine) * 1.2f;
                return static_cast<int>(p.reserve.size()) < kMaxReserveWeapons ? 0.25f + v * 0.1f : 0.0f; // a spare for the hotbar
            }
            case ItemKind::Shield: {
                const float v = ShieldReduction(s.item, s.rarity);
                if (!p.hasShield) return 0.4f + v * 3.0f;
                const float mine = ShieldReduction(p.shield.item, p.shield.rarity);
                return v > mine + 0.02f ? (v - mine) * 4.0f : 0.0f;
            }
            case ItemKind::Consumable: {
                if (PotionOf(s.item).revive) return HasFairy(p) ? 0.0f : 1.3f;
                if (PotionOf(s.item).shield > 0) return p.armor < PotionOf(s.item).shieldCap - 0.3f && static_cast<int>(p.potions.size()) < kMaxPotions ? 0.55f : 0.0f;
                if (static_cast<int>(p.potions.size()) >= kMaxPotions) return 0.0f;
                const float need = 1.0f + (p.maxHealth - p.health) * 0.3f;
                return (PotionHeal(s.item, s.rarity) * 0.5f + 0.3f) * need;
            }
            case ItemKind::Instant: {
                const float deficit = p.maxHealth - p.health;
                switch (InstantOf(s.item)) {
                    case InstantEffect::Heart: return deficit > 0.4f ? 0.3f + deficit * 0.4f : 0.0f;
                    case InstantEffect::HeartPiece: return p.maxHealth < kMaxHealthCap ? 0.9f : 0.0f;
                    case InstantEffect::HeartContainer: return p.maxHealth < kMaxHealthCap ? 1.3f : 0.0f;
                    case InstantEffect::Rupees: return 0.3f + 0.004f * static_cast<float>(s.amount);
                    case InstantEffect::Ammo: return AmmoWant(p, AmmoGivenBy(s.item));
                    default: return 0.15f;
                }
            }
            case ItemKind::Ability: {
                const float v = AbilityWorth(s.item, s.rarity);
                if (!p.hasAbility) return 0.5f + v;
                const float mine = AbilityWorth(p.ability.item, p.ability.rarity);
                return v > mine + 0.1f ? (v - mine) : 0.0f;
            }
            case ItemKind::Gear: {
                const int slot = static_cast<int>(GearOf(s.item).slot);
                if (!(p.gearMask & (1 << slot))) return 0.6f;
                const int diff = static_cast<int>(s.rarity) - static_cast<int>(p.gear[slot].rarity);
                return diff > 0 ? 0.15f + 0.1f * static_cast<float>(diff) : 0.0f;
            }
        }
        return 0.0f;
    }

    // Choose (or keep) the loot to walk to. Returns its index or -1. Re-evaluated twice a second; the current choice is kept
    // unless something clearly better turns up, so bots don't dither between two piles.
    int ChooseLoot(const Match& m, const PlayerState& p, Memory& mem, const Circle& soon) {
        const auto& loot = m.Loot();
        if (mem.lootIdx >= 0 && (static_cast<size_t>(mem.lootIdx) >= loot.size() || loot[mem.lootIdx].taken)) mem.lootIdx = -1;
        if (m.Clock() < mem.lootEvalAt && mem.lootIdx >= 0) return mem.lootIdx;
        if (m.Clock() < mem.lootEvalAt) return -1;
        mem.lootEvalAt = m.Clock() + 0.5f;

        const float radius = kSearchRadius * (0.8f + mem.greed);
        const Circle safe = {soon.center, soon.radius * 0.95f};
        int best = -1;
        float bestScore = 0, currentScore = 0;
        for (size_t i = 0; i < loot.size(); i++) {
            if (loot[i].taken) continue;
            const LootSpawn& s = loot[i].spawn;
            const float d = Distance(p.pos, s.pos);
            if (d > radius * (s.supply ? 2.4f : 1.0f) || !safe.Contains(s.pos)) continue;
            if (nav && !nav->Connected(p.pos, s.pos, mem.upper ? mem.levelY : std::numeric_limits<float>::quiet_NaN())) continue;   // a chest upstairs or on an island with no way up is not for the bots
            const float value = LootValue(p, s) * (s.supply ? 2.2f : 1.0f);   // everybody wants the supply drop
            if (value <= 0) continue;
            const float score = value * (0.6f + mem.greed) / (d + 150.0f);
            if (static_cast<int>(i) == mem.lootIdx) currentScore = score;
            if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
        }
        if (mem.lootIdx >= 0 && bestScore < currentScore * 1.3f) return mem.lootIdx;
        mem.lootIdx = best;
        return best;
    }

    // A player stops at a chest to kick it open and holds up what was inside; a good find off the ground is held up too.
    void ShowFind(Match& m, PlayerState& p, Memory& mem, const LootSpawn& got) {
        p.rot = FaceAngle(p.pos, got.pos);
        const ItemKind kind = KindOf(got.item);
        const bool notable = got.rarity >= Rarity::Rare && (kind == ItemKind::Weapon || kind == ItemKind::Shield || kind == ItemKind::Ability);
        if (got.container) {
            ShowPose(m, mem, Anim::OpenChest, 1.0f);
            mem.queuedAnim = Anim::ItemGet;
            mem.queuedFor = 0.9f;
            mem.busyUntil = m.Clock() + 1.0f;
        } else if (notable || got.item == ItemId::HeartContainer) {
            ShowPose(m, mem, Anim::ItemGet, 0.8f);
            mem.busyUntil = m.Clock() + 0.8f;
        }
    }

    // How much a bot wants what bushes and rocks give: ammo for what it carries, and rupees (allies cost rupees).
    float SmashWant(const PlayerState& p, const Memory& mem) const {
        float want = 0;
        for (int k = 0; k < static_cast<int>(AmmoKind::None); k++) {
            want = (std::max)(want, AmmoWant(p, static_cast<AmmoKind>(k)));
        }
        if (p.rupees < 40) want = (std::max)(want, 0.3f + 0.4f * mem.greed);
        return want;
    }

    bool CanBreak(const PlayerState& p, PropKind kind) const {
        if (kind == PropKind::Bush || kind == PropKind::Rock) return true;
        if (kind != PropKind::Boulder) return false;
        const WeaponStats w = Match::StatsOf(p);
        return w.damage >= 1.5f || w.splashRadius > 0;   // a boulder needs something heavy or explosive, as for players
    }

    bool SmashProps(Match& m, PlayerState& p, Memory& mem, const Circle& soon, float dt) {
        if (props.empty() || m.State() != MatchState::InMatch) return false;
        const float now = m.Clock();
        if (mem.propIdx >= 0 && (static_cast<size_t>(mem.propIdx) >= props.size() || propGone[static_cast<size_t>(mem.propIdx)])) mem.propIdx = -1;
        if (mem.propIdx < 0) {
            if (now < mem.propEvalAt) return false;
            mem.propEvalAt = now + 1.0f + static_cast<float>(rng.Unit());
            if (SmashWant(p, mem) < 0.3f) return false;
            const float radius = 260.0f + 260.0f * mem.greed;
            const Circle safe = {soon.center, soon.radius * 0.9f};
            float best = radius;
            for (size_t i = 0; i < props.size(); i++) {
                if (propGone[i] || !CanBreak(p, props[i].kind) || !safe.Contains(props[i].pos)) continue;
                const float d = Distance(p.pos, props[i].pos);
                if (d < best) { best = d; mem.propIdx = static_cast<int>(i); }
            }
            if (mem.propIdx < 0) return false;
        }
        const size_t i = static_cast<size_t>(mem.propIdx);
        const Vec2 at = props[i].pos;
        const float reach = PropRadius(props[i].kind) + 70.0f;
        if (Distance(p.pos, at) > reach) {
            // Walk up to it, aiming for the near side (the solid ones block the middle).
            const Vec2 stand = Away(at, p.pos, -PropRadius(props[i].kind) - 40.0f);
            Steer(m, p, mem, Distance(p.pos, at) > reach + 80.0f ? stand : at, dt, 1.0f, nullptr);
            return true;
        }
        p.rot = FaceAngle(p.pos, at);
        if (now - mem.lastSwingAt < 0.5f || now < p.attackReadyAt) return true;
        const WeaponStats w = Match::StatsOf(p);
        ShowPose(m, mem, w.ranged ? PoseForWeapon(w, p.weapon.item) : Anim::Attack, 0.45f);
        mem.lastSwingAt = now;
        smashes.push_back({p.id, i});
        propGone[i] = true;
        mem.propIdx = -1;
        mem.propEvalAt = now + 0.4f;
        return true;
    }

    // ---- abilities and consumables --------------------------------------------------------------------------------------------

    struct Situation {
        const PlayerState* foe = nullptr;
        float dist = 1e9f;
        float advantage = 1;
        bool fleeing = false;
        bool outsideZone = false;
        bool burning = false;
        bool hunting = false;
        bool noFoeKnown = false;
        float deficit = 0;
        int alive = 32;
        float zoneRadius = 1e9f;
    };

    // Use the ability if now is a good moment for this particular one. Returns true if it fired.
    bool TryAbility(Match& m, PlayerState& p, Memory& mem, const Situation& s) {
        if (!p.hasAbility || m.Clock() < p.abilityReadyAt || m.Clock() < mem.abilityTryAt) return false;
        if (m.MagicNow(p) + 0.001f < AbilityMagic(p.ability.item)) return false;   // out of magic
        mem.abilityTryAt = m.Clock() + 0.35f;
        const Tuning t = TuningFor(difficulty);
        const float d = s.dist;
        const bool foeNear = s.foe != nullptr;
        const bool hurt = s.deficit >= 1.0f;
        const bool critical = p.health <= 1.5f;
        bool want = false;
        switch (p.ability.item) {
            case ItemId::DinsFire:         want = foeNear && d < 330; break;
            case ItemId::NayrusLove:       want = foeNear && ((critical && d < 320) || (p.health <= 2.2f && d < 200 && s.advantage < 1.0f)); break;
            case ItemId::Hookshot:
            case ItemId::Longshot: {
                // Reel a foe in when we are the one who wants the fight and they are out of our reach.
                const WeaponStats w = Match::StatsOf(p);
                const float reach = p.ability.item == ItemId::Hookshot ? 800.0f : 1300.0f;
                want = foeNear && !s.fleeing && s.advantage > 0.9f && d > w.range * 1.3f && d < reach;
                break;
            }
            case ItemId::FaroresWind:
                if (p.hasMark) want = (s.fleeing && foeNear && p.health <= 2.0f && Distance(p.pos, p.mark) > 300.0f) || (s.outsideZone && Distance(p.pos, p.mark) > 800.0f);
                else want = !foeNear && p.health >= p.maxHealth * 0.7f && !s.outsideZone;
                break;
            case ItemId::LensOfTruth:
            case ItemId::SariasSong:       want = s.noFoeKnown && (s.alive <= 14 || s.zoneRadius < 1500.0f) && !s.fleeing; break;
            case ItemId::MagicBeans:       want = hurt && !foeNear; break;
            case ItemId::ZeldasLullaby:    want = hurt; break;
            case ItemId::EponasSong:       want = s.outsideZone || (s.fleeing && foeNear) || (s.hunting && s.noFoeKnown == false && d > 500); break;
            case ItemId::SunsSong:         want = foeNear && d < 450; break;
            case ItemId::SongOfTime:       want = foeNear && d < 700 && (s.fleeing || critical); break;
            case ItemId::SongOfStorms:     want = foeNear && d < 550; break;
            case ItemId::MinuetOfForest:   want = hurt || (s.fleeing && foeNear); break;
            case ItemId::BoleroOfFire:     want = foeNear && d < 380; break;
            case ItemId::SerenadeOfWater:  want = s.deficit >= 1.2f || s.burning; break;
            case ItemId::NocturneOfShadow: want = (s.fleeing && critical && foeNear) || (s.outsideZone && Distance(p.pos, m.GetStorm().SafeZoneAt(m.StormTime()).center) > 1500.0f); break;
            case ItemId::RequiemOfSpirit:  want = foeNear && d < 650; break;
            case ItemId::PreludeOfLight:   want = hurt && foeNear && d < 500; break;
            case ItemId::ShockwaveGrenade: want = foeNear && d < 330 && (s.fleeing || critical || s.advantage < 1.1f); break;
            case ItemId::FairyOcarina:
            case ItemId::OcarinaOfTime:    want = (foeNear && d < 500) || hurt; break; // a gamble: any song might come out
            default: break;
        }
        if (!want) return false;
        if (!s.fleeing || p.ability.item == ItemId::FaroresWind) {
            // Easier bots forget they have an ability more often (but never on the emergency ones).
            const bool emergency = critical && (p.ability.item == ItemId::NayrusLove || p.ability.item == ItemId::FaroresWind);
            if (!emergency && rng.Unit() > t.abilityUse) return false;
        }
        if (s.foe) p.rot = FaceAngle(p.pos, s.foe->pos); // the Hookshot and friends go where the bot is facing
        const ItemId ability = p.ability.item;
        const bool ok = m.UseAbility(p.id);
        if (ok) ShowPose(m, mem, IsSong(ability) || ability == ItemId::FairyOcarina || ability == ItemId::OcarinaOfTime ? Anim::Play
                                 : ability == ItemId::ShockwaveGrenade || ability == ItemId::Hookshot || ability == ItemId::Longshot ? Anim::Throw : Anim::Cast, 0.9f);
        return ok;
    }

    // Drink potions: at once when badly hurt, or when hurt a bit and nobody is close.
    bool TryHeal(Match& m, PlayerState& p, const Situation& s) {
        if (p.potions.empty()) return false;
        bool useful = false;
        for (const Equipped& e : p.potions) if (!PotionOf(e.item).revive && PotionOf(e.item).shield <= 0) useful = true;
        if (!useful) return false;
        const bool critical = p.health <= 1.2f;
        const bool safe = !s.foe || s.dist > 260.0f;
        if (critical || (safe && s.deficit >= 1.0f) || (s.burning && p.health < p.maxHealth * 0.7f)) { const bool ok = m.UsePotion(p.id); if (ok) ShowPose(m, Mem(p.id), Anim::Drink, 0.9f); return ok; }
        return false;
    }

    // Drink a shield potion when nobody is close and the bar is low.
    bool TryShield(Match& m, PlayerState& p, const Situation& s) {
        bool has = false;
        for (const Equipped& e : p.potions) has |= PotionOf(e.item).shield > 0;
        if (!has) return false;
        const bool safe = !s.foe || s.dist > 260.0f;
        if (safe && p.armor < kMaxShield * 0.55f) { const bool ok = m.UseShield(p.id); if (ok) ShowPose(m, Mem(p.id), Anim::Drink, 0.9f); return ok; }
        return false;
    }

    // ---- using the ground: cover and high ground ---------------------------------------------------------------------------------

    // Would it drink a potion now if nobody were shooting at it?
    static bool WantsToDrink(const PlayerState& p, const Situation& s) {
        for (const Equipped& e : p.potions) {
            const PotionDef ps = PotionOf(e.item);
            if (ps.revive) continue;
            if (ps.shield > 0 ? p.armor < kMaxShield * 0.55f : s.deficit >= 1.0f) return true;
        }
        return false;
    }

    // Is the foe in a position to shoot at it (a ranged weapon, in range, and a clear line)?
    bool UnderFire(const PlayerState& p, const PlayerState& foe, float dist) const {
        if (!nav) return false;
        const WeaponStats w = Match::StatsOf(foe);
        return w.ranged && Match::HasAmmo(foe, foe.weapon.item) && dist < w.range + 120.0f && dist > 160.0f && CanSee(foe, p);
    }

    // A spot near by where `foe` can't see it: just behind a tall rock, a pillar or a block on the side away from the foe, or over a rise.
    // Remembered for a moment so a bot doesn't dither between two rocks.
    bool FindCover(const Match& m, const PlayerState& p, Memory& mem, const PlayerState& foe) {
        if (!nav) return false;
        const float now = m.Clock();
        if (now < mem.coverEvalAt && mem.coverFrom == foe.id) return mem.haveCover;
        mem.coverEvalAt = now + 0.8f + 0.4f * static_cast<float>(rng.Unit());
        mem.coverFrom = foe.id;
        mem.haveCover = false;
        const float foeEye = EyeOf(foe);
        float best = 1e9f;
        auto consider = [&](Vec2 spot) {
            if (!nav->Standable(spot) || Distance(spot, foe.pos) < 220.0f) return;
            if (Sees(spot, FeetAt(spot) + 45.0f, foe.pos, foeEye)) return;
            const float d = Distance(p.pos, spot);
            if (d < best) { best = d; mem.cover = spot; mem.haveCover = true; }
        };
        for (size_t i = 0; i < props.size(); i++) {
            const Prop& pr = props[i];
            if (propGone[i] || PropRadius(pr.kind) <= 0 || SceneryHeight(pr) < 60.0f || Distance(p.pos, pr.pos) > 520.0f) continue;
            const float dx = pr.pos.x - foe.pos.x, dz = pr.pos.z - foe.pos.z, len = (std::max)(1.0f, std::hypot(dx, dz));
            const float off = PropRadius(pr.kind) + 50.0f;
            consider({pr.pos.x + dx / len * off, pr.pos.z + dz / len * off});
        }
        if (nav->HasHeights()) {   // over the brow of a hill, behind a ridge
            for (int k = 0; k < 8; k++) {
                const float a = static_cast<float>(k) * 0.785398f;
                for (float r : {160.0f, 320.0f}) consider({p.pos.x + std::sin(a) * r, p.pos.z + std::cos(a) * r});
            }
        }
        return mem.haveCover;
    }

    // Higher ground to shoot from: a spot close by, well above where it stands, that still sees the foe and keeps it in range.
    bool FindPerch(const Match& m, const PlayerState& p, Memory& mem, const PlayerState& foe, float want, float range) {
        if (!nav) return false;
        const float now = m.Clock();
        if (now < mem.perchEvalAt) return mem.havePerch;
        mem.perchEvalAt = now + 1.5f + static_cast<float>(rng.Unit());
        mem.havePerch = false;
        const float here = FeetAt(p.pos), foeEye = EyeOf(foe);
        float best = here + 50.0f;   // worth the walk only if it is a good deal higher
        for (int k = 0; k < 12; k++) {
            const float a = static_cast<float>(k) * 0.5235988f;
            for (float r : {90.0f, 180.0f, 300.0f}) {
                const Vec2 spot = {p.pos.x + std::sin(a) * r, p.pos.z + std::cos(a) * r};
                if (!nav->Standable(spot)) continue;
                const float y = FeetAt(spot), d = Distance(spot, foe.pos);
                if (y < best || d < want * 0.6f || d > range * 0.9f) continue;
                if (!Sees(spot, y + 45.0f, foe.pos, foeEye)) continue;
                best = y; mem.perch = spot; mem.havePerch = true;
            }
        }
        return mem.havePerch;
    }

    // ---- the brain ----------------------------------------------------------------------------------------------------------

    void Act(Match& m, PlayerState& p, const Circle& soon, float dt) {
        Memory& mem = Mem(p.id);
        const Tuning tune = TuningFor(difficulty);
        const float now = m.Clock();
        p.anim = static_cast<uint8_t>(Anim::Idle); // overridden when the bot moves
        if (m.Stunned(p)) return;                  // frozen in place
        if (now < mem.rollUntil) {                  // mid-roll: tumble on in the chosen direction
            Advance(m, p, mem.rollDir.x, mem.rollDir.z, kRunSpeed * 3.4f * dt);
            if (mem.rollAnim == Anim::Roll) p.rot = FaceAngle(p.pos, {p.pos.x + mem.rollDir.x, p.pos.z + mem.rollDir.z});   // hops and flips keep facing the foe
            p.anim = static_cast<uint8_t>(mem.rollAnim);
            return;
        }

        // Perceive. Taking damage alerts the bot and widens its senses for a few seconds.
        if (p.health < mem.lastHealth - 0.12f) mem.alertUntil = now + 5.0f;
        mem.lastHealth = p.health;
        float sight = tune.sight * (now < mem.alertUntil ? 1.5f : 1.0f);
        sight *= SightMult(m.CurrentWeather());   // fog, sandstorms and heavy weather hide people
        if (m.Revealing(p)) sight = 1e9f;

        PlayerState* foe = ChooseTarget(m, p, mem, sight, m.Revealing(p));
        if (foe && m.State() == MatchState::InMatch && m.StateTime() < CalmSeconds() && now >= mem.alertUntil) foe = nullptr;   // nobody wants a fight yet
        if (foe) {
            if (mem.target != foe->id) { mem.target = foe->id; mem.acquiredAt = now; mem.prevFoeAt = -1; mem.foeVel = {}; }
            if (mem.prevFoeAt >= 0 && now > mem.prevFoeAt) { // how it is moving, smoothed (for leading shots and judging its dodges)
                const float k = 1.0f / (now - mem.prevFoeAt);
                mem.foeVel = {mem.foeVel.x * 0.6f + (foe->pos.x - mem.prevFoePos.x) * k * 0.4f, mem.foeVel.z * 0.6f + (foe->pos.z - mem.prevFoePos.z) * k * 0.4f};
            }
            mem.prevFoePos = foe->pos;
            mem.prevFoeAt = now;
            mem.lastSeen = foe->pos;
            mem.lastSeenAt = now;
        } else {
            mem.target = kNoPlayer;
        }
        // A new target isn't engaged until the reaction time has passed (they can still be fled from).
        const bool engaged = foe && now - mem.acquiredAt >= mem.reaction;
        const float dist = foe ? Distance(p.pos, foe->pos) : 1e9f;

        // Standing still for a moment (a chest, a find held up, a look round, a taunt) is dropped the moment there is trouble.
        if (now < mem.busyUntil) {
            const bool trouble = (foe && dist < 450.0f) || now < mem.alertUntil || m.GetStorm().DamagePerSecond(p.pos, m.StormTime()) > 0;
            if (!trouble) return;
            mem.busyUntil = 0; mem.actUntil = 0; mem.queuedFor = 0;
        }
        // A fresh elimination: with nobody else about, a player often celebrates it.
        if (p.kills > mem.kills) {
            mem.kills = p.kills;
            if ((!foe || dist > 700.0f) && rng.Unit() < 0.4f) {
                static const int kCheers[] = {0, 3, 0, 3, 2, 4};   // Wow!, admire the sword, look to the sky, and now and then the chicken dance
                const int emote = kCheers[rng.Below(6)];
                const float seconds = emote == kChickenDanceEmote ? 3.0f : 1.8f;
                ShowPose(m, mem, static_cast<Anim>(EmoteAnim(emote)), seconds);
                mem.busyUntil = now + seconds;
                return;
            }
        }

        Situation s;
        s.foe = foe;
        s.dist = dist;
        s.advantage = foe ? Advantage(m, p, *foe) : 1.0f;
        s.deficit = p.maxHealth - p.health;
        s.burning = now < p.burnUntil;
        s.alive = m.Alive();
        s.zoneRadius = soon.radius;
        s.noFoeKnown = !foe && now - mem.lastSeenAt > 6.0f;
        Circle target = soon;
        target.radius *= 0.9f;
        s.outsideZone = !target.Contains(p.pos);

        // Flee a fight that is going badly. Once started, keep fleeing for a few seconds so bots don't flip-flop.
        const float fleeBelow = 0.15f + 0.45f * mem.caution;
        if (foe && s.advantage < fleeBelow && dist < 700.0f && now >= mem.fleeUntil) mem.fleeUntil = now + 3.0f + 2.0f * mem.caution;
        // With only the starting sword a bot runs from anybody who comes close rather than trade blows (they only fight when cornered).
        if (foe && GearFirst() && EffectiveDpsNow(p) < kMinFightDps && dist < 420.0f && dist > 110.0f && now >= mem.fleeUntil) mem.fleeUntil = now + 2.0f;
        // Run out of puff with the foe on its heels: nowhere left to run, so it turns and fights (it can flee again once it has its breath).
        if (now < mem.fleeUntil && foe && mem.stamina < 0.1f && dist < 160.0f && !mem.sprinting) mem.fleeUntil = 0;
        s.fleeing = now < mem.fleeUntil && foe != nullptr;
        if (s.fleeing && s.advantage > 1.6f && !(GearFirst() && EffectiveDpsNow(p) < kMinFightDps)) mem.fleeUntil = 0; // the tables turned
        s.hunting = !foe && now - mem.lastSeenAt < 6.0f;

        if (AvoidHazards(m, p, mem, dt, tune)) return;
        if (DodgeCarts(m, p, mem, tune)) return;
        if (GoForCart(m, p, mem, s, foe, dist, soon, dt, tune)) return;
        // Hurt with a bow (or a slingshot, a bomb...) trained on it: get behind something first, then drink.
        if (foe && !s.outsideZone && WantsToDrink(p, s) && p.health > 1.2f && UnderFire(p, *foe, dist) && FindCover(m, p, mem, *foe) &&
            Distance(p.pos, mem.cover) > 35.0f) {
            WantSprint(mem, tune, true);
            Steer(m, p, mem, mem.cover, dt);
            return;
        }
        if (TryHeal(m, p, s)) return;
        if (TryShield(m, p, s)) return;
        TryAbility(m, p, mem, s);

        // 1. Storm: stay inside 90% of where the zone will be shortly. Shoot while running but never turn to fight.
        if (s.outsideZone) {
            WantSprint(mem, tune, true);
            Steer(m, p, mem, soon.center, dt);
            if (engaged) TryAttack(m, p, mem, *foe, dist);
            return;
        }

        // Mini bosses: a weak bot keeps well away from them; a strong, healthy one goes after them for the loot (unless a player is on top of it).
        const MiniBoss* boss = NearestBoss(m, p, 650.0f);
        for (const MiniBoss& b : m.Bosses()) { // the dragon notices from much further off, so bots watch for it further off too
            if (!b.alive || !IsDragonKind(b.kind) || Distance(p.pos, b.pos) > 1400.0f) continue;
            if (!boss || Distance(p.pos, b.pos) < Distance(p.pos, boss->pos)) boss = &b;
        }
        if (boss) {
            const float bd = Distance(p.pos, boss->pos);
            const bool dragon = IsDragonKind(boss->kind);
            const WeaponStats bw = Match::StatsOf(p);
            if (dragon && boss->y > kDragonAirborneAbove && !bw.ranged) { // swords and hammers can't reach it in the air: stay clear
                if (bd < 800.0f) { Steer(m, p, mem, Away(p.pos, boss->pos, 600.0f), dt, 1.0f); return; }
            } else if (dragon && !(tune.hunt && p.health >= 2.4f && mem.aggression > 0.35f)) {
                if (bd < 700.0f) { Steer(m, p, mem, Away(p.pos, boss->pos, 600.0f), dt, 1.0f); return; }
            } else {
            const bool strong = p.health >= 2.4f && EffectiveDps(p.weapon) * TotalsOf(p).melee >= 1.8f && mem.aggression > 0.35f && tune.hunt;
            if (!strong || p.health < 1.3f) {
                if (bd < 420.0f) { Steer(m, p, mem, Away(p.pos, boss->pos, 500.0f), dt, 1.0f); return; }
            } else if (!foe || dist > 300.0f) {
                const WeaponStats w = Match::StatsOf(p);
                const float want = w.ranged ? w.range * 0.7f : w.range * 0.6f + kBossBodyRadius * 0.5f;
                if (bd > want) Steer(m, p, mem, boss->pos, dt, 1.0f, &boss->pos);
                else { p.rot = FaceAngle(p.pos, boss->pos); }
                if (bd <= w.range * 1.1f + (dragon ? kDragonBodyRadius : kBossBodyRadius) && m.Clock() >= p.attackReadyAt) BotAttack(m, p, mem, boss->id, rng.Unit() < mem.skill);
                // Back away just before it smashes, then return.
                if (bd < kBossReach + 30.0f && boss->attackReadyAt - m.Clock() < 0.35f) {
                    Advance(m, p, p.pos.x - boss->pos.x, p.pos.z - boss->pos.z, kRunSpeed * dt * 1.2f);
                }
                return;
            }
            }
        }

        // 2. Flee: run away from the foe, preferring the way toward the zone centre, and shoot back if we can.
        if (s.fleeing) {
            Vec2 away = Away(p.pos, foe->pos, 500.0f);
            const float pull = 0.35f; // blend toward the safe zone so fleeing doesn't run into the storm
            away = {away.x + (soon.center.x - away.x) * pull, away.z + (soon.center.z - away.z) * pull};
            if (nav) { Vec2 snapped; if (nav->Snap(away, &snapped, true)) away = snapped; }
            // From a bow, break its line of sight: behind the nearest rock or rise that is not back toward it.
            if (UnderFire(p, *foe, dist) && FindCover(m, p, mem, *foe) && Distance(mem.cover, foe->pos) > dist * 0.8f) away = mem.cover;
            if (dist < 380.0f || UnderFire(p, *foe, dist)) WantSprint(mem, tune, true);   // keeps its breath until the foe is close
            Steer(m, p, mem, away, dt, 1.0f);
            if (engaged) TryAttack(m, p, mem, *foe, dist);
            return;
        }

        // 3. Fight.
        if (engaged && FightWorthIt(m, p, mem, *foe, dist, s.advantage)) {
            FightEnemy(m, p, mem, *foe, dist, s.advantage, dt, tune);
            return;
        }

        // 4. Loot (not while a foe is right on top of us).
        if (!foe || dist > 450.0f) {
            const int idx = ChooseLoot(m, p, mem, soon);
            if (idx >= 0) {
                const Vec2 where = m.Loot()[static_cast<size_t>(idx)].spawn.pos;
                if (Distance(p.pos, where) <= kPickupRange * 0.8f) {
                    const LootSpawn got = m.Loot()[static_cast<size_t>(idx)].spawn;
                    if (!m.PickUp(p.id, static_cast<size_t>(idx))) mem.lootIdx = -1;
                    else { mem.lootIdx = -1; mem.lootEvalAt = 0; ShowFind(m, p, mem, got); }
                } else {
                    const LootSpawn& want = m.Loot()[static_cast<size_t>(idx)].spawn;
                    if (want.supply || Distance(p.pos, where) > 650.0f) WantSprint(mem, tune, false);   // first to the supply drop, or a long way
                    Steer(m, p, mem, where, dt);
                }
                return;
            }
        }

        // 4b. Cut bushes and break rocks for rupees and ammo when there is nothing better to pick up.
        if ((!foe || dist > 800.0f) && SmashProps(m, p, mem, soon, dt)) return;

        // 5. Hunt: go where the last target was seen (aggressive, healthy bots), or close in on a visible but distant enemy.
        const bool armed = EffectiveDpsNow(p) >= kMinFightDps || !GearFirst();   // with just the starting sword, nobody goes hunting
        if (tune.hunt && armed && s.deficit < p.maxHealth * 0.4f) {
            if (foe && mem.aggression > 0.45f && s.advantage > 1.0f) {
                if (dist > 500.0f || GettingAway(mem, p, *foe)) WantSprint(mem, tune, dist < 320.0f);   // closing the last gap on a runner: all it has
                Steer(m, p, mem, foe->pos, dt);
                return;
            }
            if (!foe && now - mem.lastSeenAt < 6.0f && mem.aggression > 0.5f && Distance(p.pos, mem.lastSeen) > 80.0f && Circle{soon.center, soon.radius * 0.9f}.Contains(mem.lastSeen)) {
                if (Distance(p.pos, mem.lastSeen) > 300.0f) WantSprint(mem, tune, false);   // after it before it gets away round the rock
                Steer(m, p, mem, mem.lastSeen, dt);
                return;
            }
        }

        // 6. Late in the match, close in on the other players instead of waiting for the storm to do it.
        if (s.alive <= 6 && tune.hunt && mem.aggression > 0.4f && s.deficit < p.maxHealth * 0.5f) {
            PlayerState* nearest = nullptr;
            float nearestDist = 1e18f;
            for (auto& o : m.Players()) {
                if (&o == &p || !o.alive) continue;
                const float d = Distance(p.pos, o.pos);
                if (d < nearestDist) { nearestDist = d; nearest = &o; }
            }
            if (nearest && nearestDist > 250.0f) {
                if (nearestDist > 900.0f) WantSprint(mem, tune, false);
                Steer(m, p, mem, nearest->pos, dt, 0.9f);
                return;
            }
        }

        // 7. Wander inside the zone, stopping now and then to look round.
        if (!mem.hasWander || Distance(p.pos, {soon.center.x + mem.wander.x, soon.center.z + mem.wander.z}) < 60.0f) {
            if (mem.hasWander && rng.Unit() < 0.45f) mem.busyUntil = now + 1.0f + 1.5f * static_cast<float>(rng.Unit());
            Vec2 pick = soon.center;
            for (int tries = 0; tries < 8; tries++) {
                const float a = static_cast<float>(rng.Unit() * 6.283185307179586);
                const float d = soon.radius * 0.5f * std::sqrt(static_cast<float>(rng.Unit()));
                pick = {soon.center.x + d * std::cos(a), soon.center.z + d * std::sin(a)};
                if (Standable(pick)) { mem.wander = {pick.x - soon.center.x, pick.z - soon.center.z}; break; }
            }
            mem.hasWander = true;
        }
        Steer(m, p, mem, {soon.center.x + mem.wander.x, soon.center.z + mem.wander.z}, dt, 0.75f);
    }

    bool FightWorthIt(const Match& m, const PlayerState& p, const Memory& mem, const PlayerState& foe, float dist, float advantage) const {
        (void)m; (void)foe;
        const float range = (std::max)(kAlwaysFightRange * (0.5f + mem.aggression), Match::StatsOf(p).range * 1.5f);
        if (dist > range) return false;
        if (dist <= 180.0f) return true; // cornered: fight
        if (EffectiveDpsNow(p) < kMinFightDps && (advantage < 1.0f || (GearFirst() && dist > 220.0f))) return false;   // with only the starting sword, go and find a real weapon first
        return advantage >= 0.4f + 0.5f * (1.0f - mem.aggression);
    }

    // Pick the weapon that suits the range: a bow when the foe is far, something heavy up close. Swapping costs a moment, so
    // bots only do it for a clear gain and not more than once every few seconds.
    void ChooseWeapon(Match& m, PlayerState& p, Memory& mem, float dist) {
        if (p.reserve.empty() || m.Clock() < mem.switchAt || m.Clock() < p.attackReadyAt) return;
        auto fit = [&](const Equipped& e) {
            const bool ammo = Match::HasAmmo(p, e.item);
            const WeaponStats w = ActiveWeapon(e.item, ammo);
            float score = ammo ? EffectiveDps(e) : WeaponDps(ItemId::BasicSword, Rarity::Common); // out of ammo it is only a club
            if (dist > w.range * 1.1f) score *= w.ranged ? 0.55f : 0.15f;      // can't reach from here
            else if (w.ranged && dist < w.range * 0.25f) score *= 0.8f;        // too close for a bow
            return score;
        };
        int best = 0;
        float bestScore = fit(p.weapon);
        for (size_t i = 0; i < p.reserve.size(); i++) {
            const float sc = fit(p.reserve[i]);
            if (sc > bestScore * 1.25f) { bestScore = sc; best = static_cast<int>(i) + 1; }
        }
        if (best > 0 && m.SelectWeapon(p.id, best)) mem.switchAt = m.Clock() + 1.6f;
    }

    void FightEnemy(Match& m, PlayerState& p, Memory& mem, const PlayerState& foe, float dist, float advantage, float dt, const Tuning& tune) {
        (void)advantage;
        if (tune.kite) ChooseWeapon(m, p, mem, dist); // Easy bots just use what is in their hand
        const WeaponStats mine = Match::StatsOf(p);
        const WeaponStats theirs = Match::StatsOf(foe);
        float want = mine.ranged ? mine.range * 0.65f : mine.range * 0.6f; // strafing swings wide, so melee closes well inside its reach

        // Kite: a ranged bot facing a melee foe keeps its distance while its weapon recharges.
        if (tune.kite && mine.ranged && !theirs.ranged && dist < theirs.range * 2.0f + 80.0f) {
            want = (std::max)(want, theirs.range * 2.0f);
            if (m.Clock() < p.attackReadyAt) want += 60.0f;
        }
        // Dodge: when the foe is about to swing or shoot, roll out of the way (sideways, across its line of attack) if it can, otherwise sidestep.
        // One decision per attack: whether this swing gets dodged depends on the bot's reflexes.
        const bool foeWillAttack = !m.Stunned(foe) && foe.attackReadyAt - m.Clock() < 0.24f && dist < theirs.range * 1.2f + 40.0f;
        if (foeWillAttack && mem.dodgeCycle != foe.attackReadyAt) {
            mem.dodgeCycle = foe.attackReadyAt;
            mem.dodgeThisSwing = rng.Unit() < tune.dodge * (0.55f + 0.45f * mem.skill);
        }
        if (foeWillAttack && mem.dodgeThisSwing) {
            mem.dodgeThisSwing = false;
            const float toX = foe.pos.x - p.pos.x, toZ = foe.pos.z - p.pos.z;
            const bool away = mine.ranged && dist < want;   // a bow wants distance: flip backwards out of reach
            float dx = -toZ * mem.strafeDir, dz = toX * mem.strafeDir;
            const Anim dodge = DodgeAnimFor(mem, away);
            if (dodge == Anim::Backflip) { dx = -toX; dz = -toZ; }
            if (m.CanRoll(p) && RollToward(m, p, mem, dx, dz)) {
                mem.rollAnim = dodge;
                p.anim = static_cast<uint8_t>(dodge);
                p.rot = FaceAngle(p.pos, foe.pos);
                if (dodge == Anim::Roll && m.Clock() >= p.attackReadyAt && dist <= mine.range) BotAttack(m, p, mem, foe.id, rng.Unit() < mem.skill * 0.8f, dist); // and swing back before rolling off
                return;
            }
            mem.strafeDir = -mem.strafeDir;
            mem.strafeFlipAt = m.Clock() + 0.5f;
        } else if (foeWillAttack && p.hasShield && !mine.ranged && !IsTwoHanded(p.weapon.item) && m.Clock() >= mem.actUntil && rng.Unit() < 0.25f + 0.5f * mem.caution) {
            ShowPose(m, mem, Anim::Guard, 0.4f);   // no dodge this time: shield up, as a player holds R
        }
        // A bow is better from up high: a ledge, a block or a hilltop near by that still sees the foe and keeps it in range.
        if (mine.ranged && tune.kite && dist > want * 0.6f && FindPerch(m, p, mem, foe, want, mine.range)) {
            if (Distance(p.pos, mem.perch) > 30.0f) {
                Steer(m, p, mem, mem.perch, dt, 1.0f, &foe.pos);
                if (p.anim == static_cast<uint8_t>(Anim::Run)) p.anim = static_cast<uint8_t>(Anim::SideR);   // shuffling over, eyes on the foe
            } else {   // up there: hold it and shoot
                p.rot = FaceAngle(p.pos, foe.pos);
                p.anim = static_cast<uint8_t>(Anim::Stance);
            }
            TryAttack(m, p, mem, foe, dist);
            return;
        }
        if ((dist > want * 1.8f && dist > 450.0f) || (dist > want + 40.0f && GettingAway(mem, p, foe))) WantSprint(mem, tune, dist < 320.0f);   // close in, or run it down
        Fight(m, p, mem, foe, dist, want, dt, 1.0f);
        // Hop in when closing on a foe from a little way off, the way players jump about in a fight.
        if (dist > want + 60.0f && dist < 520.0f && m.Clock() >= mem.nextHopAt && rng.Unit() < 0.04f * (0.5f + mem.aggression)) Hop(m, mem);
        TryAttack(m, p, mem, foe, dist);
    }


    // A jump (C-Up) for the fun of it, as players hop about. Every client lifts the bot in an arc while it shows the jump.
    void Hop(Match& m, Memory& mem) {
        if (m.Clock() < mem.actUntil) return;
        ShowPose(m, mem, Anim::Jump, 0.55f);
        mem.nextHopAt = m.Clock() + 2.5f + static_cast<float>(rng.Unit()) * 4.0f;
    }
    // A jump that gets it somewhere: up a ledge or onto a block. Shown whatever else it was showing, but not twice in one leap.
    void Jump(Match& m, Memory& mem) {
        if (mem.actAnim == Anim::Jump && m.Clock() < mem.actUntil) return;
        ShowPose(m, mem, Anim::Jump, 0.55f);
        mem.nextHopAt = (std::max)(mem.nextHopAt, m.Clock() + 1.5f);
    }

    // Swing or shoot if the foe is in range and the weapon is ready. Accuracy falls off with distance for ranged weapons.
    // ---- carts ---------------------------------------------------------------------------------------------------------------------

    // A cart coming at it fast: jump (or step) out of its way, sideways to its path.
    bool DodgeCarts(Match& m, PlayerState& p, Memory& mem, const Tuning& tune) {
        for (const VehicleState& v : m.Vehicles()) {
            if (v.gone || v.wrecked || std::fabs(v.body.speed) < 150.0f) continue;
            const float fx = std::sin(v.body.yaw) * (v.body.speed > 0 ? 1.0f : -1.0f), fz = std::cos(v.body.yaw) * (v.body.speed > 0 ? 1.0f : -1.0f);
            const float dx = p.pos.x - v.body.x, dz = p.pos.z - v.body.z;
            const float ahead = dx * fx + dz * fz, across = dx * fz - dz * fx;
            const float soon = std::fabs(v.body.speed) * 0.9f;
            if (ahead < -40.0f || ahead > soon + 80.0f || std::fabs(across) > 115.0f) continue;   // (keeping well clear of its lane till it is by)
            if (rng.Unit() > 0.35f + 0.6f * mem.skill) continue;   // didn't see it coming
            const float side = across >= 0.0f ? 1.0f : -1.0f;
            const float ax = fz * side, az = -fx * side;           // straight out of its path
            if (m.CanRoll(p) && tune.dodge > 0 && RollToward(m, p, mem, ax, az)) { p.anim = static_cast<uint8_t>(Anim::Roll); return true; }
            Advance(m, p, ax, az, kRunSpeed * kSprintMult / kTickHz);
            p.rot = FaceAngle(p.pos, {p.pos.x + ax, p.pos.z + az});
            p.anim = static_cast<uint8_t>(Anim::Sprint);
            return true;
        }
        return false;
    }

    // Where a bot that is going somewhere far would like to be: out of the storm, away from a fight it is losing, or the middle of the zone.
    static Vec2 TripGoal(const PlayerState& p, const Circle& soon) {
        const float d = Distance(p.pos, soon.center);
        if (d < 1.0f) return soon.center;
        const float keep = soon.radius * 0.45f;   // well inside, on its own side of the circle
        return d <= keep ? soon.center : Vec2{soon.center.x + (p.pos.x - soon.center.x) / d * keep, soon.center.z + (p.pos.z - soon.center.z) / d * keep};
    }

    // On foot: is a cart worth it? A long way to go (out of the storm, or a bot that likes driving with nothing better to do) or a fight to get
    // away from. Then walk to the nearest free seat and climb in. Returns true while it is busy with that.
    bool GoForCart(Match& m, PlayerState& p, Memory& mem, const Situation& s, const PlayerState* foe, float dist, const Circle& soon, float dt, const Tuning& tune) {
        const float now = m.Clock();
        if (m.Vehicles().empty() || m.State() != MatchState::InMatch) return false;
        if (now >= mem.cartEvalAt) {
            mem.cartEvalAt = now + 1.2f + static_cast<float>(rng.Unit());
            mem.cartIdx = -1;
            const float trip = Distance(p.pos, TripGoal(p, soon));
            const bool fleeing = s.fleeing && foe && dist > 150.0f;
            const bool longWay = (s.outsideZone && trip > 800.0f) || trip > 2200.0f - 1400.0f * mem.cartLove;
            const bool idle = !foe && s.noFoeKnown && mem.cartLove > 0.4f;
            const bool chase = foe && !fleeing && dist > 1500.0f - 500.0f * mem.cartLove && s.advantage >= 0.8f;   // a fight a long way off
            if ((fleeing || longWay || idle || chase) && !(foe && !fleeing && dist < 450.0f)) {
                const float reach = fleeing ? 700.0f : (std::min)(1400.0f, (std::max)(500.0f, trip * 0.45f) + 400.0f * mem.cartLove);
                float best = reach;
                for (const VehicleState& v : m.Vehicles()) {
                    if (v.gone || v.wrecked || v.health < kCartHealth * 0.3f) continue;
                    Seat want = Seat::None;
                    const PlayerState* driver = m.Find(v.Driver());
                    if (v.Driver() == kNoPlayer) want = Seat::Driver;
                    else if (v.Passenger() == kNoPlayer && driver && std::fabs(v.body.speed) < 60.0f) want = Seat::Passenger;   // a lift
                    if (want == Seat::None) continue;
                    const float d = Distance(p.pos, ExitSpot(v.body, want));
                    if (want == Seat::Passenger && d > 450.0f) continue;   // only a cart that has stopped right here
                    if (foe && Distance(foe->pos, {v.body.x, v.body.z}) < d * 0.8f) continue;   // not past the enemy to get to it
                    if (d < best) { best = d; mem.cartIdx = v.index; mem.cartSeat = want; }
                }
            }
        }
        const VehicleState* v = m.FindVehicle(mem.cartIdx);
        if (!v || v->wrecked || v->gone || v->seat[static_cast<int>(mem.cartSeat)] != kNoPlayer) { mem.cartIdx = -1; return false; }
        const Vec2 door = ExitSpot(v->body, mem.cartSeat);
        if (Distance(p.pos, door) <= kEnterRange * 0.8f || Distance(p.pos, {v->body.x, v->body.z}) <= kEnterRange * 0.7f) {
            if (m.EnterVehicle(p.id, mem.cartIdx, mem.cartSeat)) {
                mem.riding = true;
                mem.cartBoardedAt = now;
                mem.cartGoalSet = false;
                mem.cartStuckAt = -1;
                mem.cartStillSince = -1;
                mem.cartPath.clear();
                return true;
            }
            mem.cartIdx = -1;
            return false;
        }
        if (Distance(p.pos, door) > 400.0f) WantSprint(mem, tune, s.fleeing);
        Steer(m, p, mem, door, dt);
        return true;
    }

    // In a cart: drive it, or ride along. Returns false when it is on foot (and notices having just got out).
    bool StepRider(Match& m, PlayerState& p, Memory& mem, const Circle& soon, float dt) {
        int index; Seat seat;
        if (!m.RidingIn(p.id, &index, &seat)) {
            if (mem.riding) {   // just got out (or was thrown out): back on its feet
                mem.riding = false;
                mem.lift = 0; mem.vy = 0; mem.haveFloor = false; mem.upper = false;
                mem.path.clear(); mem.cartIdx = -1; mem.cartEvalAt = m.Clock() + 6.0f;   // and doesn't jump straight back in
            }
            return false;
        }
        mem.riding = true;
        VehicleState& v = m.MutableVehicles()[static_cast<size_t>(index)];
        p.anim = static_cast<uint8_t>(Anim::Idle);
        if (m.Stunned(p)) { if (seat == Seat::Driver) v.controls = {}; return true; }
        if (seat == Seat::Driver) Drive(m, p, mem, v, soon, dt);
        else Ride(m, p, mem, v, soon);
        const float now = m.Clock();
        if (now < mem.actUntil) p.anim = static_cast<uint8_t>(mem.actAnim);
        return true;
    }

    void LeaveCart(Match& m, PlayerState& p, VehicleState& v, Memory& mem) {
        if (v.Driver() == p.id) v.controls = {};
        m.ExitVehicle(p.id);
        mem.riding = false;
        mem.lift = 0; mem.vy = 0; mem.haveFloor = false; mem.upper = false;
        mem.path.clear(); mem.cartIdx = -1;
        mem.cartEvalAt = m.Clock() + 6.0f;
    }

    void Drive(Match& m, PlayerState& p, Memory& mem, VehicleState& v, const Circle& soon, float dt) {
        (void)dt;
        const float now = m.Clock();
        const Tuning tune = TuningFor(difficulty);
        const Vec2 at = {v.body.x, v.body.z};
        // Bail out of a cart about to go up, and don't sit in one that has stopped for good.
        if (v.health < kCartHealth * 0.22f && rng.Unit() < 0.2f + 0.5f * mem.caution) { LeaveCart(m, p, v, mem); return; }
        // Someone to run down: an aggressive bot with a healthy cart goes for a person on foot it can see close by.
        PlayerState* foe = ChooseTarget(m, p, mem, tune.sight * 0.9f, m.Revealing(p));
        if (foe && (m.RidingIn(foe->id) || m.StateTime() < CalmSeconds())) foe = nullptr;
        if (foe && mem.aggression > 0.5f && v.health > kCartHealth * 0.4f && Distance(at, foe->pos) < 800.0f && now >= mem.ramUntil + 6.0f) {
            mem.ramTarget = foe->id; mem.ramUntil = now + 7.0f;
        }
        PlayerState* prey = now < mem.ramUntil ? m.Find(mem.ramTarget) : nullptr;
        if (prey && (!prey->alive || m.RidingIn(prey->id))) { prey = nullptr; mem.ramUntil = 0; }
        // A fight on its doorstep it would rather take on foot (a good weapon, and it is not running anyway).
        if (!prey && foe && Distance(at, foe->pos) < 320.0f && EffectiveDpsNow(p) >= kMinFightDps && Advantage(m, p, *foe) > 1.0f && std::fabs(v.body.speed) < 160.0f) {
            LeaveCart(m, p, v, mem); return;
        }
        // Where to: out of the storm first, else wherever it set out for, else the middle of the zone.
        if (!mem.cartGoalSet || !Circle{soon.center, soon.radius * 0.95f}.Contains(mem.cartGoal)) {
            mem.cartGoal = TripGoal(p, soon);
            if (nav) { Vec2 snapped; if (nav->Snap(mem.cartGoal, &snapped)) mem.cartGoal = snapped; }
            mem.cartGoalSet = true;
            mem.cartPath.clear();
        }
        // Somebody to fight a long way off: drive over and get out a little short of them.
        const bool chasing = foe && !prey && Advantage(m, p, *foe) >= 0.8f && Circle{soon.center, soon.radius * 0.9f}.Contains(foe->pos);
        if (chasing && Distance(at, foe->pos) < 450.0f) {
            v.controls = DriveToward(v.body, foe->pos, 700.0f);
            if (std::fabs(v.body.speed) < 60.0f) LeaveCart(m, p, v, mem);
            return;
        }
        const Vec2 goal = prey ? prey->pos : chasing ? foe->pos : mem.cartGoal;
        if (Distance(goal, mem.cartPathGoal) > 300.0f) { mem.cartPath.clear(); mem.cartPathGoal = goal; }
        const float toGoal = Distance(at, goal);
        if (!prey && toGoal < 350.0f) {   // there: stop and get out
            v.controls = DriveToward(v.body, goal, 400.0f);
            if (std::fabs(v.body.speed) < 40.0f) LeaveCart(m, p, v, mem);
            return;
        }
        // Follow a path over open ground (a cart is too wide for the gaps a bot squeezes through), easing round its corners.
        Vec2 aim = goal;
        if (!prey && nav && toGoal > NavGrid::kCell * 3.0f) {
            if ((mem.cartPathIdx >= mem.cartPath.size() || now >= mem.cartRepathAt) && repathBudget > 0) {
                repathBudget--;
                mem.cartRepathAt = now + 3.0f;
                mem.cartPathIdx = 0;
                if (!nav->FindPath(at, goal, mem.cartPath, false)) mem.cartPath.clear();
            }
            while (mem.cartPathIdx < mem.cartPath.size() && Distance(at, mem.cartPath[mem.cartPathIdx]) < 140.0f) mem.cartPathIdx++;
            if (mem.cartPathIdx < mem.cartPath.size()) aim = mem.cartPath[mem.cartPathIdx];
        }
        const float boldness = 0.45f + 0.5f * mem.aggression * (0.6f + 0.4f * mem.skill);
        v.controls = DriveToward(v.body, aim, prey ? 0.0f : 60.0f, boldness);
        if (prey) { v.controls.brake = false; v.controls.throttle = (std::max)(v.controls.throttle, 0.6f); }   // full tilt at them
        // Stuck against something: back up, turning, for a moment; still stuck after that, get out and walk.
        if (mem.cartStuckAt < 0 || Distance(at, mem.cartProgress) > 120.0f) { mem.cartStuckAt = now; mem.cartProgress = at; }
        const float stuckFor = now - mem.cartStuckAt;
        if (stuckFor > 2.2f && stuckFor < 3.6f) { v.controls.throttle = -1.0f; v.controls.brake = false; v.controls.steer = mem.strafeDir; }
        else if (stuckFor >= 3.6f && stuckFor < 4.0f) { mem.strafeDir = -mem.strafeDir; mem.cartPath.clear(); }
        else if (stuckFor > 8.0f) { LeaveCart(m, p, v, mem); return; }
        // Drift round the sharp corners at speed, if it is any good.
        v.controls.handbrake = mem.skill > 0.6f && std::fabs(v.body.speed) > 260.0f && std::fabs(v.controls.steer) > 0.9f && rng.Unit() < 0.5f;
        p.rot = YawToBinang(v.body.yaw);
    }

    void Ride(Match& m, PlayerState& p, Memory& mem, VehicleState& v, const Circle& soon) {
        const float now = m.Clock();
        const Tuning tune = TuningFor(difficulty);
        if (v.health < kCartHealth * 0.22f && rng.Unit() < 0.25f) { LeaveCart(m, p, v, mem); return; }
        // Nobody driving: take the reins.
        if (v.Driver() == kNoPlayer && m.SwitchSeat(p.id)) { mem.cartGoalSet = false; mem.cartPath.clear(); return; }
        // Shoot from the saddle at whoever is close (a passenger's job).
        PlayerState* foe = ChooseTarget(m, p, mem, tune.sight, m.Revealing(p));
        if (foe && m.StateTime() >= CalmSeconds() && foe->id != v.Driver()) {
            const float d = Distance(p.pos, foe->pos);
            if (mem.target != foe->id) { mem.target = foe->id; mem.acquiredAt = now; }
            if (now - mem.acquiredAt >= mem.reaction) TryAttack(m, p, mem, *foe, d);
        }
        // The ride is over when the cart has stopped for a while, inside the zone, or the bot only came along to get away.
        const bool stopped = std::fabs(v.body.speed) < 25.0f && v.body.grounded;
        if (!stopped) mem.cartStillSince = -1;
        else if (mem.cartStillSince < 0) mem.cartStillSince = now;
        const bool safe = Circle{soon.center, soon.radius * 0.9f}.Contains(p.pos);
        if (stopped && mem.cartStillSince >= 0 && now - mem.cartStillSince > (safe ? 2.0f : 6.0f) && now - mem.cartBoardedAt > 2.0f) LeaveCart(m, p, v, mem);
    }

    void TryAttack(Match& m, PlayerState& p, Memory& mem, const PlayerState& foe, float dist) {
        const WeaponStats w = Match::StatsOf(p);
        if (dist > w.range || m.Clock() < p.attackReadyAt || m.Stunned(p)) return;
        if (m.Invulnerable(foe)) return; // don't waste a swing
        float chance = mem.skill * (w.ranged ? 1.0f - 0.35f * (dist / w.range) : 1.0f);
        // A moving target across the line of fire is harder to hit; a good shot leads it, a poor one doesn't.
        if (w.ranged && dist > 1.0f) {
            const float lx = (foe.pos.x - p.pos.x) / dist, lz = (foe.pos.z - p.pos.z) / dist;
            const float across = std::fabs(mem.foeVel.x * -lz + mem.foeVel.z * lx) / kRunSpeed; // 0 standing or running straight, 1 running across
            chance -= across * 0.55f * (1.0f - mem.skill) * (0.5f + dist / w.range);
        }
        if (m.Stunned(foe)) chance = (std::min)(1.0f, chance + 0.25f);
        if (w.homing) chance = (std::max)(chance, 0.9f); // it chases: moving doesn't help
        if (nav) {   // the ground: a shot into a rock or a hill is wasted, a sword can't reach up a ledge, and it is easier to shoot down than up
            const float up = EyeOf(foe) - EyeOf(p);
            if (w.ranged && !CanSee(p, foe)) chance *= 0.2f;
            else if (!w.ranged && std::fabs(up) > NavGrid::kClimbUp + 40.0f) chance *= 0.15f;
            else if (w.ranged && up < -60.0f) chance += 0.08f;
        }
        BotAttack(m, p, mem, foe.id, rng.Unit() < (std::max)(0.05f, chance), dist);
    }
};

} // namespace royale
