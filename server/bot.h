#pragma once
#include "../shared/anim.h"
#include "match.h"
#include <cmath>
#include <unordered_map>

namespace royale {

// Server-side bot brain. Bots are ordinary PlayerState entries (isBot) that this class moves and acts for, using only
// Match's public API, the same calls a human client's messages end up making. It never touches game code, so it runs
// anywhere the server runs.
//
// Each tick, per bot, in priority order:
//   1. Heal      drink a potion when hurt and not in immediate danger
//   2. Storm     if the safe zone (looking kStormLookahead ahead) won't contain us, run for its centre
//   3. Fight     engage a nearby enemy, flee when low and outmatched
//   4. Loot      walk to the most valuable upgrade within kSearchRadius and pick it up
//   5. Wander    drift around inside the safe zone
class BotController {
  public:
    static constexpr float kStormLookahead = 15.0f; // seconds
    static constexpr float kSight = 900.0f;
    static constexpr float kAlwaysFightRange = 300.0f;
    static constexpr float kSearchRadius = 700.0f;
    static constexpr float kMinFightDps = 1.0f; // bots with a weaker weapon avoid fights they could skip

    explicit BotController(uint64_t seed) : rng(seed ^ 0x626F74ull) {} // "bot"

    void Step(Match& m, float dt) {
        if (m.State() != MatchState::Drop && m.State() != MatchState::InMatch) return;
        Circle soon = m.GetStorm().SafeZoneAt(m.StormTime() + kStormLookahead);
        for (auto& p : m.Players()) {
            if (p.isBot && p.alive) Act(m, p, soon, dt);
        }
    }

  private:
    struct Memory {
        float skill;   // accuracy, 0.55 to 0.9
        Vec2 wander;   // offset from the zone centre
        bool hasWander = false;
    };

    Rng rng;
    std::unordered_map<uint32_t, Memory> memory;

    Memory& Mem(uint32_t id) {
        auto it = memory.find(id);
        if (it == memory.end()) {
            it = memory.emplace(id, Memory{static_cast<float>(0.55 + 0.35 * rng.Unit()), {}, false}).first;
        }
        return it->second;
    }

    static void MoveToward(PlayerState& p, Vec2 target, float dt, float speedScale = 1.0f) {
        float dx = target.x - p.pos.x, dz = target.z - p.pos.z;
        float len = std::hypot(dx, dz);
        if (len < 1e-3f) return;
        float step = (std::min)(len, kRunSpeed * speedScale * dt);
        p.pos.x += dx / len * step;
        p.pos.z += dz / len * step;
        // Face the direction of travel (OoT binary angle, 0x10000 = 360 degrees, 0 = +z) and show running.
        p.rot = static_cast<int16_t>(static_cast<int32_t>(std::atan2(dx, dz) * (32768.0f / 3.14159265358979f)));
        p.anim = static_cast<uint8_t>(speedScale < 0.9f ? Anim::Walk : Anim::Run);
    }

    static float EffectiveDps(const Equipped& e) {
        float dps = WeaponDps(e.item, e.rarity);
        return WeaponOf(e.item).ranged ? dps * 1.15f : dps;
    }

    void Act(Match& m, PlayerState& p, const Circle& soon, float dt) {
        Memory& mem = Mem(p.id);
        p.anim = static_cast<uint8_t>(Anim::Idle); // MoveToward overrides this when the bot moves

        // Nearest living enemy.
        PlayerState* enemy = nullptr;
        float enemyDist = 1e9f;
        for (auto& o : m.Players()) {
            if (&o == &p || !o.alive) continue;
            float d = Distance(p.pos, o.pos);
            if (d < enemyDist) { enemyDist = d; enemy = &o; }
        }
        bool enemyNear = enemy && enemyDist <= kSight;

        // 1. Heal.
        if (!p.potions.empty() && p.health < p.maxHealth) {
            bool critical = p.health <= 1.0f;
            bool safe = !enemyNear || enemyDist > 250.0f;
            if ((critical || (safe && p.health <= 2.0f))) {
                if (m.UsePotion(p.id)) return;
            }
        }

        // 2. Storm: stay inside 90% of where the zone will be shortly.
        Circle target = soon;
        target.radius *= 0.9f;
        if (!target.Contains(p.pos)) {
            MoveToward(p, soon.center, dt);
            TryAttack(m, p, mem, enemy, enemyDist); // shoot while running, but never chase
            return;
        }

        // 3. Fight or flee.
        if (enemyNear && (enemyDist <= kAlwaysFightRange || EffectiveDps(p.weapon) >= kMinFightDps)) {
            float myDps = EffectiveDps(p.weapon);
            float theirDps = EffectiveDps(enemy->weapon);
            if (p.health <= 0.8f && p.potions.empty() && myDps < theirDps * 1.5f && enemyDist > 120.0f) {
                // Outmatched and hurt: back away from the enemy, toward the zone.
                Vec2 away = {p.pos.x + (p.pos.x - enemy->pos.x), p.pos.z + (p.pos.z - enemy->pos.z)};
                MoveToward(p, away, dt);
                return;
            }
            WeaponStats w = WeaponOf(p.weapon.item);
            float desired = w.ranged ? w.range * 0.6f : w.range * 0.8f;
            if (enemyDist > desired) {
                MoveToward(p, enemy->pos, dt);
            } else if (w.ranged && enemyDist < w.range * 0.3f) {
                Vec2 away = {p.pos.x + (p.pos.x - enemy->pos.x), p.pos.z + (p.pos.z - enemy->pos.z)};
                MoveToward(p, away, dt, 0.8f);
            }
            TryAttack(m, p, mem, enemy, enemyDist);
            return;
        }

        // 4. Loot.
        size_t bestIdx = 0;
        float bestScore = 0;
        const auto& loot = m.Loot();
        float currentWeapon = EffectiveDps(p.weapon);
        float currentShield = p.hasShield ? ShieldReduction(p.shield.item, p.shield.rarity) : 0.0f;
        for (size_t i = 0; i < loot.size(); i++) {
            if (loot[i].taken) continue;
            const LootSpawn& s = loot[i].spawn;
            float d = Distance(p.pos, s.pos);
            if (d > kSearchRadius) continue;
            float value = 0;
            switch (KindOf(s.item)) {
                case ItemKind::Weapon: {
                    float v = EffectiveDps({s.item, s.rarity});
                    if (v > currentWeapon * 1.1f) value = v - currentWeapon;
                    break;
                }
                case ItemKind::Shield: {
                    float v = ShieldReduction(s.item, s.rarity);
                    if (v > currentShield + 0.02f) value = (v - currentShield) * 4.0f;
                    break;
                }
                case ItemKind::Consumable:
                    if (static_cast<int>(p.potions.size()) < kMaxPotions) value = PotionHeal(s.item, s.rarity) * 0.5f + 0.3f;
                    break;
                case ItemKind::Instant:
                    if (InstantOf(s.item) != InstantEffect::Heart || p.health < p.maxHealth) value = 0.6f;
                    break;
                case ItemKind::Ability:
                    if (!p.hasAbility) value = 0.8f;
                    break;
                case ItemKind::Gear: {
                    const int slot = static_cast<int>(GearOf(s.item).slot);
                    if (!(p.gearMask & (1 << slot))) value = 0.5f;
                    else if (s.rarity > p.gear[slot].rarity) value = 0.2f;
                    break;
                }
            }
            if (value <= 0) continue;
            float score = value / (d + 150.0f);
            if (score > bestScore) { bestScore = score; bestIdx = i; }
        }
        if (bestScore > 0) {
            const Vec2 where = loot[bestIdx].spawn.pos;
            if (Distance(p.pos, where) <= kPickupRange) {
                m.PickUp(p.id, bestIdx);
            } else {
                MoveToward(p, where, dt);
            }
            return;
        }

        // 5. Wander inside the zone.
        if (!mem.hasWander || Distance(p.pos, {soon.center.x + mem.wander.x, soon.center.z + mem.wander.z}) < 30.0f) {
            float a = static_cast<float>(rng.Unit() * 6.283185307179586);
            float d = soon.radius * 0.5f * std::sqrt(static_cast<float>(rng.Unit()));
            mem.wander = {d * std::cos(a), d * std::sin(a)};
            mem.hasWander = true;
        }
        MoveToward(p, {soon.center.x + mem.wander.x, soon.center.z + mem.wander.z}, dt, 0.7f);
    }

    // Swing or shoot if the enemy is in range and the weapon is ready. Accuracy falls off with distance for ranged weapons.
    void TryAttack(Match& m, PlayerState& p, const Memory& mem, PlayerState* enemy, float dist) {
        if (!enemy) return;
        WeaponStats w = WeaponOf(p.weapon.item);
        if (dist > w.range || m.Clock() < p.attackReadyAt) return;
        float chance = mem.skill * (w.ranged ? 1.0f - 0.35f * (dist / w.range) : 1.0f);
        m.Attack(p.id, enemy->id, rng.Unit() < chance);
    }
};

} // namespace royale
