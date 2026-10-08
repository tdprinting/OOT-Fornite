#pragma once
#include "boss.h"

namespace royale {
// Separate namespace of entity ids: helpers never consume boss slots or count as surviving players.
constexpr uint32_t kHelperIdBase = 5100;
constexpr int kHelpersPerBoss = 3;
constexpr int kMaxHelpers = kMaxBosses * kHelpersPerBoss;
constexpr float kBokoHealth = 3.0f, kBokoBodyRadius = 24.0f;
constexpr float kBokoReach = 75.0f, kBokoLeash = 850.0f, kBokoNotice = 480.0f;
constexpr bool IsHelperId(uint32_t id) { return id >= kHelperIdBase && id < kHelperIdBase + kMaxHelpers; }
enum class HelperKind : uint8_t { Bokoblin, Count };
enum class BokoMode : uint8_t { Idle, Walk, Alert, Swing, Jump, Throw, Recover, Dance, Hurt, Flee, Dead, Count };
constexpr float BokoDuration(BokoMode mode) {
    switch (mode) {
        case BokoMode::Alert: return 0.7f;
        case BokoMode::Swing: return 0.75f;
        case BokoMode::Jump: return 1.0f;
        case BokoMode::Throw: return 1.1f;
        case BokoMode::Recover: return 0.9f;
        case BokoMode::Dance: return 1.8f;
        case BokoMode::Hurt: return 0.5f;
        case BokoMode::Flee: return 8.0f;
        case BokoMode::Dead: return 1.0f;
        default: return 1.0f;
    }
}
constexpr bool BokoAttacking(BokoMode m) { return m == BokoMode::Swing || m == BokoMode::Jump || m == BokoMode::Throw; }
}
