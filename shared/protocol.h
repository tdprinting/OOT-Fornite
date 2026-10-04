#pragma once
#include "balance.h"
#include "bytes.h"
#include "loot.h"
#include "storm.h"
#include <array>
#include <cmath>
#include <string>
#include <vector>

// Wire protocol between host (server) and clients. Binary, little-endian, versioned.
//
//   Reliable ordered : Hello, Welcome, Reject, AttackReport, PickupRequest, UsePotionRequest, MatchStateMsg,
//                      EvDamaged, EvEliminated, EvLootTaken, EvLootAdded, EvPlayerJoined, EvPlayerLeft
//   Unreliable       : Input (client to server, ~20 Hz), Snapshot (server to client, 20 Hz)
//
// Every message is `[u8 type][fields...]`. Decode() rejects wrong types, short data, trailing bytes, NaN and Inf.
namespace royale::net {

constexpr uint16_t kProtocolVersion = 2; // 2: lobby (ready flags, host marker), scene in Input/PlayerNet, winner in MatchStateMsg
constexpr uint16_t kNoPlayer16 = 0xFFFF;
constexpr size_t kMaxNameLen = 24;
constexpr size_t kMaxLoot = 4096;
constexpr size_t kSnapshotMaxPlayers = 12; // interest management: nearest N others, plus self (everybody while revealing)
constexpr uint8_t kRevivedItem = 0xFF;      // EvAbility.item value meaning "used a Fairy to come back"

enum class MsgType : uint8_t {
    Hello = 1, Input = 2, AttackReport = 3, PickupRequest = 4, UsePotionRequest = 5, SetReady = 6, UseAbilityRequest = 7,
    Welcome = 64, Reject = 65, MatchStateMsg = 66, Snapshot = 67,
    EvDamaged = 70, EvEliminated = 71, EvLootTaken = 72, EvLootAdded = 73, EvPlayerJoined = 74, EvPlayerLeft = 75,
    EvReady = 76, EvMapConfig = 77, EvInventory = 78, EvAbility = 79,
};

enum class RejectReason : uint8_t { VersionMismatch = 1, LobbyFull = 2, MatchInProgress = 3, BadHello = 4 };

// Roster flags, in Welcome and EvPlayerJoined.
constexpr uint8_t kRosterHost = 1, kRosterReady = 2;

inline bool Finite(float f) { return std::isfinite(f); }

inline void WriteCircle(ByteWriter& w, const Circle& c) { w.F32(c.center.x); w.F32(c.center.z); w.F32(c.radius); }
inline Circle ReadCircle(ByteReader& r) {
    Circle c;
    c.center.x = r.F32(); c.center.z = r.F32(); c.radius = r.F32();
    if (!Finite(c.center.x) || !Finite(c.center.z) || !Finite(c.radius) || c.radius < 0) r.ok = false;
    return c;
}

// ---- client to server ------------------------------------------------------------------------------------------

struct Hello {
    static constexpr MsgType kType = MsgType::Hello;
    uint16_t version = kProtocolVersion;
    std::string name;
    // Secret the hosting process generated; a Hello carrying it is the host's own player. 0 for everyone else.
    uint64_t hostToken = 0;
    void Write(ByteWriter& w) const { w.U16(version); w.Str(name); w.U64(hostToken); }
    bool Read(ByteReader& r) { version = r.U16(); name = r.Str(kMaxNameLen); hostToken = r.U64(); return r.ok; }
};

struct Input {
    static constexpr MsgType kType = MsgType::Input;
    uint16_t seq = 0;   // wraps; the server drops anything not newer than the last one it saw
    uint8_t epoch = 0;  // the teleport epoch the client last saw (see Snapshot.epoch)
    float x = 0, y = 0, z = 0;
    int16_t rot = 0;
    uint8_t anim = 0;
    uint8_t scene = 0;  // which game scene the sender is in; others only draw you if they are in the same one
    void Write(ByteWriter& w) const { w.U16(seq); w.U8(epoch); w.F32(x); w.F32(y); w.F32(z); w.I16(rot); w.U8(anim); w.U8(scene); }
    bool Read(ByteReader& r) {
        seq = r.U16(); epoch = r.U8(); x = r.F32(); y = r.F32(); z = r.F32(); rot = r.I16(); anim = r.U8(); scene = r.U8();
        return r.ok && Finite(x) && Finite(y) && Finite(z);
    }
};

struct AttackReport {
    static constexpr MsgType kType = MsgType::AttackReport;
    uint16_t target = 0;
    bool hit = false;
    void Write(ByteWriter& w) const { w.U16(target); w.U8(hit ? 1 : 0); }
    bool Read(ByteReader& r) { target = r.U16(); uint8_t h = r.U8(); hit = h == 1; return r.ok && h <= 1; }
};

struct PickupRequest {
    static constexpr MsgType kType = MsgType::PickupRequest;
    uint32_t index = 0;
    void Write(ByteWriter& w) const { w.U32(index); }
    bool Read(ByteReader& r) { index = r.U32(); return r.ok; }
};

struct UsePotionRequest {
    static constexpr MsgType kType = MsgType::UsePotionRequest;
    void Write(ByteWriter&) const {}
    bool Read(ByteReader& r) { return r.ok; }
};

// Use the ability slot (spell, song, hookshot...). The server decides whether it worked.
struct UseAbilityRequest {
    static constexpr MsgType kType = MsgType::UseAbilityRequest;
    void Write(ByteWriter&) const {}
    bool Read(ByteReader& r) { return r.ok; }
};

struct SetReady {
    static constexpr MsgType kType = MsgType::SetReady;
    bool ready = false;
    void Write(ByteWriter& w) const { w.U8(ready ? 1 : 0); }
    bool Read(ByteReader& r) { uint8_t v = r.U8(); ready = v == 1; return r.ok && v <= 1; }
};

// ---- server to client ------------------------------------------------------------------------------------------

struct LootNet {
    float x = 0, z = 0;
    uint8_t item = 0, rarity = 0;
    bool chest = false, taken = false;
    void Write(ByteWriter& w) const { w.F32(x); w.F32(z); w.U8(item); w.U8(rarity); w.U8(static_cast<uint8_t>((chest ? 1 : 0) | (taken ? 2 : 0))); }
    bool Read(ByteReader& r) {
        x = r.F32(); z = r.F32(); item = r.U8(); rarity = r.U8(); uint8_t f = r.U8();
        chest = f & 1; taken = f & 2;
        return r.ok && Finite(x) && Finite(z) && item < static_cast<uint8_t>(ItemId::Count) && rarity < kRarityCount && f <= 3;
    }
};

struct RosterEntry {
    uint16_t id = 0;
    uint8_t flags = 0; // kRosterHost, kRosterReady
    std::string name;
};

struct Welcome {
    static constexpr MsgType kType = MsgType::Welcome;
    uint16_t playerId = 0;
    uint16_t version = kProtocolVersion;
    uint64_t seed = 0;
    Circle map;
    std::array<Circle, kStormPhaseCount> stormEnds;
    std::vector<LootNet> loot;
    std::vector<RosterEntry> roster;
    void Write(ByteWriter& w) const {
        w.U16(playerId); w.U16(version); w.U64(seed);
        WriteCircle(w, map);
        for (const auto& c : stormEnds) WriteCircle(w, c);
        w.U16(static_cast<uint16_t>(loot.size()));
        for (const auto& l : loot) l.Write(w);
        w.U8(static_cast<uint8_t>(roster.size()));
        for (const auto& e : roster) { w.U16(e.id); w.U8(e.flags); w.Str(e.name); }
    }
    bool Read(ByteReader& r) {
        playerId = r.U16(); version = r.U16(); seed = r.U64();
        map = ReadCircle(r);
        for (auto& c : stormEnds) c = ReadCircle(r);
        size_t n = r.U16();
        if (n > kMaxLoot) return false;
        loot.assign(n, {});
        for (auto& l : loot) if (!l.Read(r)) return false;
        size_t m = r.U8();
        roster.assign(m, {});
        for (auto& e : roster) { e.id = r.U16(); e.flags = r.U8(); e.name = r.Str(kMaxNameLen); if (e.flags > 3) r.ok = false; }
        return r.ok;
    }
};

struct Reject {
    static constexpr MsgType kType = MsgType::Reject;
    RejectReason reason = RejectReason::BadHello;
    void Write(ByteWriter& w) const { w.U8(static_cast<uint8_t>(reason)); }
    bool Read(ByteReader& r) { uint8_t v = r.U8(); reason = static_cast<RejectReason>(v); return r.ok && v >= 1 && v <= 4; }
};

struct MatchStateMsg {
    static constexpr MsgType kType = MsgType::MatchStateMsg;
    uint8_t state = 0; // MatchState
    uint8_t alive = 0;
    uint16_t winner = kNoPlayer16; // set when state is Ending and somebody is left standing
    void Write(ByteWriter& w) const { w.U8(state); w.U8(alive); w.U16(winner); }
    bool Read(ByteReader& r) { state = r.U8(); alive = r.U8(); winner = r.U16(); return r.ok && state <= 4; }
};

// One player as seen in a snapshot. 25 bytes.
struct PlayerNet {
    static constexpr uint8_t kAlive = 1, kShield = 2, kBot = 4;
    uint16_t id = 0;
    float x = 0, y = 0, z = 0;
    int16_t rot = 0;
    uint8_t health = 255; // 0..255 maps to 0..kMaxHealth
    uint8_t flags = 0;
    uint8_t weapon = 0, weaponRarity = 0;
    uint8_t potions = 0;
    uint8_t anim = 0;
    uint8_t scene = 0;
    uint8_t shield = 0, shieldRarity = 0; // valid when kShield is set
    static uint8_t QuantizeHealth(float h) {
        float v = h / kMaxHealthCap * 255.0f + 0.5f;
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    float Health() const { return health / 255.0f * kMaxHealthCap; }
    void Write(ByteWriter& w) const {
        w.U16(id); w.F32(x); w.F32(z); w.F32(y); w.I16(rot);
        w.U8(health); w.U8(flags); w.U8(weapon); w.U8(weaponRarity); w.U8(potions); w.U8(anim); w.U8(scene); w.U8(shield); w.U8(shieldRarity);
    }
    bool Read(ByteReader& r) {
        id = r.U16(); x = r.F32(); z = r.F32(); y = r.F32(); rot = r.I16();
        health = r.U8(); flags = r.U8(); weapon = r.U8(); weaponRarity = r.U8(); potions = r.U8(); anim = r.U8(); scene = r.U8(); shield = r.U8(); shieldRarity = r.U8();
        return r.ok && Finite(x) && Finite(y) && Finite(z) && flags <= 7 && weapon < static_cast<uint8_t>(ItemId::Count) &&
               weaponRarity < kRarityCount &&
               shield < static_cast<uint8_t>(ItemId::Count) && shieldRarity < kRarityCount;
    }
};

struct Snapshot {
    static constexpr MsgType kType = MsgType::Snapshot;
    uint32_t tick = 0;       // server tick counter, kTickHz per second
    float stormTime = 0;     // seconds since the storm started
    uint8_t state = 0;
    uint8_t alive = 0;
    uint8_t epoch = 0;       // bumped whenever the server teleports this client (match start); inputs with an old epoch are ignored
    std::vector<PlayerNet> players; // first entry is always the receiving client
    void Write(ByteWriter& w) const {
        w.U32(tick); w.F32(stormTime); w.U8(state); w.U8(alive); w.U8(epoch);
        w.U8(static_cast<uint8_t>(players.size()));
        for (const auto& p : players) p.Write(w);
    }
    bool Read(ByteReader& r) {
        tick = r.U32(); stormTime = r.F32(); state = r.U8(); alive = r.U8(); epoch = r.U8();
        size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPlayers) || state > 4 || !Finite(stormTime)) return false; // up to everyone, while revealing
        players.assign(n, {});
        for (auto& p : players) if (!p.Read(r)) return false;
        return r.ok;
    }
};

struct EvDamaged {
    static constexpr MsgType kType = MsgType::EvDamaged;
    uint16_t target = 0, attacker = kNoPlayer16;
    float amount = 0, health = 0;
    void Write(ByteWriter& w) const { w.U16(target); w.U16(attacker); w.F32(amount); w.F32(health); }
    bool Read(ByteReader& r) { target = r.U16(); attacker = r.U16(); amount = r.F32(); health = r.F32(); return r.ok && Finite(amount) && Finite(health); }
};

struct EvEliminated {
    static constexpr MsgType kType = MsgType::EvEliminated;
    uint16_t victim = 0, killer = kNoPlayer16;
    void Write(ByteWriter& w) const { w.U16(victim); w.U16(killer); }
    bool Read(ByteReader& r) { victim = r.U16(); killer = r.U16(); return r.ok; }
};

struct EvLootTaken {
    static constexpr MsgType kType = MsgType::EvLootTaken;
    uint32_t index = 0;
    uint16_t by = 0;
    void Write(ByteWriter& w) const { w.U32(index); w.U16(by); }
    bool Read(ByteReader& r) { index = r.U32(); by = r.U16(); return r.ok; }
};

struct EvLootAdded {
    static constexpr MsgType kType = MsgType::EvLootAdded;
    uint32_t index = 0;
    LootNet loot;
    void Write(ByteWriter& w) const { w.U32(index); loot.Write(w); }
    bool Read(ByteReader& r) { index = r.U32(); return loot.Read(r) && r.ok; }
};

struct EvPlayerJoined {
    static constexpr MsgType kType = MsgType::EvPlayerJoined;
    uint16_t id = 0;
    uint8_t flags = 0; // kRosterHost, kRosterReady
    std::string name;
    void Write(ByteWriter& w) const { w.U16(id); w.U8(flags); w.Str(name); }
    bool Read(ByteReader& r) { id = r.U16(); flags = r.U8(); name = r.Str(kMaxNameLen); return r.ok && flags <= 3; }
};

struct EvReady {
    static constexpr MsgType kType = MsgType::EvReady;
    uint16_t id = 0;
    bool ready = false;
    void Write(ByteWriter& w) const { w.U16(id); w.U8(ready ? 1 : 0); }
    bool Read(ByteReader& r) { id = r.U16(); uint8_t v = r.U8(); ready = v == 1; return r.ok && v <= 1; }
};

// The host measured the real map and rebuilt the lobby's world: new map circle, storm circles and loot. Lobby only.
struct EvMapConfig {
    static constexpr MsgType kType = MsgType::EvMapConfig;
    Circle map;
    std::array<Circle, kStormPhaseCount> stormEnds;
    std::vector<LootNet> loot;
    void Write(ByteWriter& w) const {
        WriteCircle(w, map);
        for (const auto& c : stormEnds) WriteCircle(w, c);
        w.U16(static_cast<uint16_t>(loot.size()));
        for (const auto& l : loot) l.Write(w);
    }
    bool Read(ByteReader& r) {
        map = ReadCircle(r);
        for (auto& c : stormEnds) c = ReadCircle(r);
        size_t n = r.U16();
        if (n > kMaxLoot) return false;
        loot.assign(n, {});
        for (auto& l : loot) if (!l.Read(r)) return false;
        return r.ok;
    }
};

// What one player carries, sent to that player whenever it changes (and to nobody else). Times are "seconds left" at the moment of sending.
struct ItemRef {
    uint8_t item = 0, rarity = 0;
    bool Valid() const { return item < static_cast<uint8_t>(ItemId::Count) && rarity < kRarityCount; }
};

struct EvInventory {
    static constexpr MsgType kType = MsgType::EvInventory;
    float maxHealth = kMaxHealth;
    uint8_t heartPieces = 0;
    std::vector<ItemRef> potions;           // the bag, at most kMaxPotions
    bool hasAbility = false;
    ItemRef ability;
    float abilityReadyIn = 0;
    bool hasMark = false;                   // Farore's Wind has a spot marked
    uint8_t gearMask = 0;
    std::array<ItemRef, kGearSlots> gear = {};
    float invulnLeft = 0, speedLeft = 0, speedMult = 1, revealLeft = 0, stunLeft = 0, burnLeft = 0, regenLeft = 0, shieldLeft = 0;

    void Write(ByteWriter& w) const {
        w.F32(maxHealth); w.U8(heartPieces);
        w.U8(static_cast<uint8_t>(potions.size()));
        for (const auto& p : potions) { w.U8(p.item); w.U8(p.rarity); }
        w.U8((hasAbility ? 1 : 0) | (hasMark ? 2 : 0));
        w.U8(ability.item); w.U8(ability.rarity); w.F32(abilityReadyIn);
        w.U8(gearMask);
        for (int i = 0; i < kGearSlots; i++) if (gearMask & (1 << i)) { w.U8(gear[i].item); w.U8(gear[i].rarity); }
        w.F32(invulnLeft); w.F32(speedLeft); w.F32(speedMult); w.F32(revealLeft);
        w.F32(stunLeft); w.F32(burnLeft); w.F32(regenLeft); w.F32(shieldLeft);
    }
    bool Read(ByteReader& r) {
        maxHealth = r.F32(); heartPieces = r.U8();
        size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPotions)) return false;
        potions.assign(n, {});
        for (auto& p : potions) { p.item = r.U8(); p.rarity = r.U8(); if (r.ok && !p.Valid()) return false; }
        uint8_t flags = r.U8();
        hasAbility = flags & 1; hasMark = flags & 2;
        ability.item = r.U8(); ability.rarity = r.U8(); abilityReadyIn = r.F32();
        if (flags > 3 || (hasAbility && r.ok && !ability.Valid())) return false;
        gearMask = r.U8();
        if (gearMask >= (1 << kGearSlots)) return false;
        for (int i = 0; i < kGearSlots; i++) {
            gear[i] = {};
            if (gearMask & (1 << i)) { gear[i].item = r.U8(); gear[i].rarity = r.U8(); if (r.ok && !gear[i].Valid()) return false; }
        }
        invulnLeft = r.F32(); speedLeft = r.F32(); speedMult = r.F32(); revealLeft = r.F32();
        stunLeft = r.F32(); burnLeft = r.F32(); regenLeft = r.F32(); shieldLeft = r.F32();
        return r.ok && Finite(maxHealth) && maxHealth >= 1 && maxHealth <= kMaxHealthCap && Finite(abilityReadyIn) && abilityReadyIn >= 0 &&
               Finite(invulnLeft) && Finite(speedLeft) && Finite(speedMult) && speedMult > 0.1f && speedMult < 3 && Finite(revealLeft) &&
               Finite(stunLeft) && Finite(burnLeft) && Finite(regenLeft) && Finite(shieldLeft) && invulnLeft >= 0 && speedLeft >= 0 &&
               revealLeft >= 0 && stunLeft >= 0 && burnLeft >= 0 && regenLeft >= 0 && shieldLeft >= 0;
    }
};

// Someone used an ability (or, with item == kRevivedItem, came back with a Fairy). Everyone is told, so effects can be shown.
struct EvAbility {
    static constexpr MsgType kType = MsgType::EvAbility;
    uint16_t user = 0;
    uint8_t item = 0;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.U16(user); w.U8(item); w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) {
        user = r.U16(); item = r.U8(); x = r.F32(); z = r.F32();
        return r.ok && (item < static_cast<uint8_t>(ItemId::Count) || item == kRevivedItem) && Finite(x) && Finite(z);
    }
};

struct EvPlayerLeft {
    static constexpr MsgType kType = MsgType::EvPlayerLeft;
    uint16_t id = 0;
    void Write(ByteWriter& w) const { w.U16(id); }
    bool Read(ByteReader& r) { id = r.U16(); return r.ok; }
};

// ---- encode / decode -------------------------------------------------------------------------------------------

template <class T>
std::vector<uint8_t> Encode(const T& msg) {
    ByteWriter w;
    w.U8(static_cast<uint8_t>(T::kType));
    msg.Write(w);
    return std::move(w.buf);
}

// Returns false (leaving `out` unspecified) on a wrong type, malformed or trailing data.
template <class T>
bool Decode(const uint8_t* data, size_t size, T& out) {
    ByteReader r(data, size);
    if (r.U8() != static_cast<uint8_t>(T::kType)) return false;
    return out.Read(r) && r.Done();
}
template <class T>
bool Decode(const std::vector<uint8_t>& data, T& out) { return Decode(data.data(), data.size(), out); }

inline bool PeekType(const uint8_t* data, size_t size, MsgType& type) {
    if (size < 1) return false;
    type = static_cast<MsgType>(data[0]);
    return true;
}

// Strip anything but printable ASCII from a player name and clamp its length.
inline std::string SanitizeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c >= 0x20 && c < 0x7F) out.push_back(c);
        if (out.size() >= kMaxNameLen) break;
    }
    return out;
}

} // namespace royale::net
