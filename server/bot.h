#pragma once
#include "../shared/anim.h"
#include "../shared/props.h"
#include "match.h"
#include "nav.h"
#include <cmath>
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
    void SetProps(std::vector<Prop> list) { props = std::move(list); propGone.assign(props.size(), false); smashes.clear(); }
    void PropGone(size_t index) { if (index < propGone.size()) propGone[index] = true; }
    std::vector<std::pair<uint32_t, size_t>> DrainSmashes() { std::vector<std::pair<uint32_t, size_t>> out; out.swap(smashes); return out; }
    bool HasNav() const { return nav != nullptr; }
    void SetDifficulty(BotDifficulty d) { difficulty = d; memory.clear(); } // bots re-roll their personalities
    BotDifficulty Difficulty() const { return difficulty; }

    void Step(Match& m, float dt) {
        if (m.State() != MatchState::Drop && m.State() != MatchState::InMatch) return;
        const Circle soon = m.GetStorm().SafeZoneAt(m.StormTime() + kStormLookahead);
        repathBudget = 8;
        StepAllies(m, dt);
        // Bots are "in the air" for most of the drop, so they land and start looting when the humans do, not before.
        if (m.State() == MatchState::Drop && m.StateTime() < kDropSec * 0.65f) return;
        for (auto& p : m.Players()) {
            if (p.isBot && p.alive) {
                Act(m, p, soon, dt);
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
        const bool moved = Advance(m, tmp, dx, dz, dist);
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
    };

    Rng rng;
    BotDifficulty difficulty = BotDifficulty::Normal;
    std::shared_ptr<const NavGrid> nav;
    std::unordered_map<uint32_t, Memory> memory;
    int repathBudget = 0;
    std::vector<Prop> props;                          // the scenery (SetProps)
    std::vector<bool> propGone;                       // broken by somebody, or about to be by a bot
    std::vector<std::pair<uint32_t, size_t>> smashes; // bot id, prop index: waiting for the server to break them

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
        const AttackResult r = m.Attack(p.id, target, hit);
        if (!r.ok) return r;
        Anim pose = PoseForWeapon(w, item);
        float seconds = 0.45f;
        if (pose == Anim::Attack) {
            const bool fresh = m.Clock() - mem.lastSwingAt > 1.4f;   // the first swing of an exchange
            mem.combo = fresh ? 0 : mem.combo + 1;
            int crowd = 0;
            for (const auto& o : m.Players()) if (o.alive && o.id != p.id && Distance(o.pos, p.pos) <= w.range * 1.1f) crowd++;
            if (crowd >= 2 && rng.Unit() < 0.55f * mem.skill + 0.2f) { pose = Anim::SpinAttack; seconds = 0.75f; }
            else if (fresh && dist > w.range * 0.5f && rng.Unit() < 0.25f + 0.4f * mem.aggression) { pose = Anim::JumpSlash; seconds = 0.7f; }
            else if (mem.combo >= 3 && rng.Unit() < 0.3f * mem.skill) { pose = Anim::SpinAttack; seconds = 0.75f; mem.combo = 0; }
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
            if (!b.alive) continue;
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

    // Move up to `dist` units along (dx, dz), sliding along walls: if the straight step is blocked try each side. Returns true if moved.
    bool Advance(const Match& m, PlayerState& p, float dx, float dz, float dist) {
        const float len = std::hypot(dx, dz);
        if (len < 1e-4f) return false;
        dx /= len; dz /= len;
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
            if (!Walkable(next)) continue;
            p.pos = next;
            return true;
        }
        return false;
    }

    // Walk toward `goal`, following an A* path when the way isn't a clear line. `face` keeps the bot looking at a point (for
    // strafing) instead of where it is going.
    void Steer(Match& m, PlayerState& p, Memory& mem, Vec2 goal, float dt, float speedScale = 1.0f, const Vec2* face = nullptr) {
        const float step = kRunSpeed * m.SpeedMultiplier(p) * speedScale * dt;
        Vec2 aim = goal;
        if (m.Clock() < mem.unstickUntil) {
            aim = {p.pos.x + mem.unstickDir.x * 200.0f, p.pos.z + mem.unstickDir.z * 200.0f};
        } else if (nav && Distance(p.pos, goal) > NavGrid::kCell * 1.2f) {
            const bool goalMoved = Distance(goal, mem.pathGoal) > 120.0f;
            const bool needPath = mem.pathIdx >= mem.path.size() || goalMoved;
            if ((needPath || m.Clock() >= mem.repathAt) && repathBudget > 0) {
                repathBudget--;
                mem.pathGoal = goal;
                mem.pathIdx = 0;
                mem.repathAt = m.Clock() + 1.5f + static_cast<float>(rng.Unit());
                if (!nav->FindPath(p.pos, goal, mem.path)) mem.path.clear(); // unreachable: fall back to walking straight at it
            }
            while (mem.pathIdx < mem.path.size() && Distance(p.pos, mem.path[mem.pathIdx]) < NavGrid::kCell * 0.6f) mem.pathIdx++;
            if (mem.pathIdx < mem.path.size()) aim = mem.path[mem.pathIdx];
        }

        const float dx = aim.x - p.pos.x, dz = aim.z - p.pos.z;
        const bool moved = Advance(m, p, dx, dz, (std::min)(step, std::hypot(dx, dz)));
        p.rot = face ? FaceAngle(p.pos, *face) : FaceAngle(p.pos, aim);
        p.anim = static_cast<uint8_t>(moved ? (speedScale < 0.9f ? Anim::Walk : Anim::Run) : Anim::Idle);
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

    PlayerState* ChooseTarget(Match& m, PlayerState& p, Memory& mem, float sight) {
        PlayerState* best = nullptr;
        float bestScore = 0;
        for (auto& o : m.Players()) {
            if (&o == &p || !o.alive) continue;
            const float d = Distance(p.pos, o.pos);
            if (d > sight) continue;
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

        PlayerState* foe = ChooseTarget(m, p, mem, sight);
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
        s.fleeing = now < mem.fleeUntil && foe != nullptr;
        if (s.fleeing && s.advantage > 1.6f && !(GearFirst() && EffectiveDpsNow(p) < kMinFightDps)) mem.fleeUntil = 0; // the tables turned
        s.hunting = !foe && now - mem.lastSeenAt < 6.0f;

        if (AvoidHazards(m, p, mem, dt, tune)) return;
        if (TryHeal(m, p, s)) return;
        if (TryShield(m, p, s)) return;
        TryAbility(m, p, mem, s);

        // 1. Storm: stay inside 90% of where the zone will be shortly. Shoot while running but never turn to fight.
        if (s.outsideZone) {
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
            if (nav) { Vec2 snapped; if (nav->Snap(away, &snapped)) away = snapped; }
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
                Steer(m, p, mem, foe->pos, dt);
                return;
            }
            if (!foe && now - mem.lastSeenAt < 6.0f && mem.aggression > 0.5f && Distance(p.pos, mem.lastSeen) > 80.0f && Circle{soon.center, soon.radius * 0.9f}.Contains(mem.lastSeen)) {
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
                if (Walkable(pick)) { mem.wander = {pick.x - soon.center.x, pick.z - soon.center.z}; break; }
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
        } else if (foeWillAttack && p.hasShield && !mine.ranged && !TwoHanded(p.weapon.item) && m.Clock() >= mem.actUntil && rng.Unit() < 0.25f + 0.5f * mem.caution) {
            ShowPose(m, mem, Anim::Guard, 0.4f);   // no dodge this time: shield up, as a player holds R
        }
        Fight(m, p, mem, foe, dist, want, dt, 1.0f);
        // Hop in when closing on a foe from a little way off, the way players jump about in a fight.
        if (dist > want + 60.0f && dist < 520.0f && m.Clock() >= mem.nextHopAt && rng.Unit() < 0.04f * (0.5f + mem.aggression)) Hop(m, mem);
        TryAttack(m, p, mem, foe, dist);
    }

    static bool TwoHanded(ItemId item) { return item == ItemId::BiggoronSword || item == ItemId::MegatonHammer || item == ItemId::GiantsHammer; }

    // A jump (C-Up). Bots move on flat ground, so it is only seen: every client lifts the bot in an arc while it shows the jump.
    void Hop(Match& m, Memory& mem) {
        if (m.Clock() < mem.actUntil) return;
        ShowPose(m, mem, Anim::Jump, 0.55f);
        mem.nextHopAt = m.Clock() + 2.5f + static_cast<float>(rng.Unit()) * 4.0f;
    }

    // Swing or shoot if the foe is in range and the weapon is ready. Accuracy falls off with distance for ranged weapons.
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
        BotAttack(m, p, mem, foe.id, rng.Unit() < (std::max)(0.05f, chance), dist);
    }
};

} // namespace royale
