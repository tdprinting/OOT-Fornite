#pragma once
// Which pad button does what in a match. Two layouts: Battle Royale (the default) and Classic (how the mod played before).
// Pure functions on N64 button masks so the rules can be unit tested without the game.
#include <cstdint>

namespace royale::controls {

enum class Scheme : int { BattleRoyale = 0, Classic = 1 };
inline constexpr int kSchemeCount = 2;

// The game's N64 button bits (the same values as BTN_* in libultraship).
inline constexpr uint16_t kCRight = 0x0001, kCLeft = 0x0002, kCDown = 0x0004, kCUp = 0x0008;
inline constexpr uint16_t kR = 0x0010, kL = 0x0020, kDRight = 0x0100, kDLeft = 0x0200, kDDown = 0x0400, kDUp = 0x0800;
inline constexpr uint16_t kStart = 0x1000, kZ = 0x2000, kB = 0x4000, kA = 0x8000;

inline Scheme Clamp(int v) { return v == static_cast<int>(Scheme::Classic) ? Scheme::Classic : Scheme::BattleRoyale; }

// Jump. Classic: C-Up. Battle Royale: A, but only when A has nothing to do (no chest, item, ally, cart or talk target in reach),
// so one button is "do the obvious thing", like the A button in the real game. `touchJump` is the on-screen JUMP button.
inline bool JumpPressed(Scheme s, uint16_t pressed, bool aHasTarget, bool touchJump) {
    if (touchJump) return true;
    if (s == Scheme::Classic) return (pressed & kCUp) != 0;
    return (pressed & kA) != 0 && !aHasTarget;
}

// Ability: its own dedicated button, never a hotbar slot. Classic: D-pad Up. Battle Royale: D-pad Up or C-Up (C-Up is free because the jump moved to A).
inline bool AbilityPressed(Scheme s, uint16_t pressed) {
    return (pressed & (s == Scheme::Classic ? kDUp : static_cast<uint16_t>(kDUp | kCUp))) != 0;
}

// The prompt for each action as the hotbar shows it.
enum class Glyph : uint8_t { None, A, B, Z, R, Start, CUp, CDown, CLeft, CRight, DUp, DDown, DLeft, DRight, DLeftRight };
struct Prompt { Glyph weapon, swap, shield, potion, ability, emote, jump, interact; };
// The hotbar (kHotbarSlots usable slots: weapons and potion stacks): D-pad Left/Right (or a C-Down tap) picks the slot, B uses it, holding C-Down
// drops it. The shield is worn (Z raises it), not drunk from a button, and potions are hotbar slots, so neither has a prompt of its own.
inline Prompt PromptsFor(Scheme s) {
    Prompt p{Glyph::B, Glyph::DLeftRight, Glyph::None, Glyph::CDown, Glyph::DUp, Glyph::CRight, Glyph::A, Glyph::A};
    if (s == Scheme::Classic) p.jump = Glyph::CUp;
    return p;
}

} // namespace royale::controls
