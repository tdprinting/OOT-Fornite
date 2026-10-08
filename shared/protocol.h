#pragma once
#include "balance.h"
#include "bytes.h"
#include "loot.h"
#include "boss.h"
#include "bokoblin.h"
#include "poi.h"
#include "skins.h"
#include "storm.h"
#include "vehicle.h"
#include "replay.h"
#include "weather.h"
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

constexpr uint16_t kProtocolVersion = 34; // 34: Bokoblin helper snapshots. 33: Riftlands scale/collision. 32: Riftlands map. Peers must update together.
constexpr uint16_t kNoPlayer16 = 0xFFFF;
constexpr uint32_t kParkedVehicleEvery = 5;   // a parked, empty cart is in every fifth snapshot only (clients keep it for kVehicleKeepSeconds)
constexpr float kVehicleKeepSeconds = 0.6f;
constexpr size_t kMaxNameLen = 24;
constexpr size_t kMaxLoot = 4096;
constexpr size_t kSnapshotMaxPlayers = 12; // interest management: nearest N others, plus self (everybody while revealing)
constexpr uint8_t kRevivedItem = 0xFF;      // EvAbility.item value meaning "used a Fairy to come back"

enum class MsgType : uint8_t {
    Hello = 1, Input = 2, AttackReport = 3, PickupRequest = 4, UsePotionRequest = 5, SetReady = 6, UseAbilityRequest = 7, SelectWeaponRequest = 8, RematchRequest = 9, UseShieldRequest = 10, SelectMapRequest = 11, PropSmashRequest = 12, HireAllyRequest = 13, NpcHitRequest = 14,
    VehicleRequest = 15, VehicleDrive = 16, FartCloudRequest = 17,
    Welcome = 64, Reject = 65, MatchStateMsg = 66, Snapshot = 67,
    EvDamaged = 70, EvEliminated = 71, EvLootTaken = 72, EvLootAdded = 73, EvPlayerJoined = 74, EvPlayerLeft = 75,
    EvReady = 76, EvMapConfig = 77, EvInventory = 78, EvAbility = 79, EvResults = 80, EvBossDown = 81, EvStrike = 82, EvBossSpawn = 83, EvPropBroken = 84, EvWeather = 85, EvSupplyDrop = 86, EvAlly = 87, EvAllyAction = 88, EvReplayHeader = 89, EvReplayChunk = 90, EvFartCloud = 91,
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
    uint32_t tunic = SkinRgb(0); // the player's chosen tunic colour, 0xRRGGBB
    void Write(ByteWriter& w) const { w.U16(version); w.Str(name); w.U64(hostToken); w.U8(RgbR(tunic)); w.U8(RgbG(tunic)); w.U8(RgbB(tunic)); }
    bool Read(ByteReader& r) { version = r.U16(); name = r.Str(kMaxNameLen); hostToken = r.U64(); const uint8_t cr = r.U8(), cg = r.U8(), cb = r.U8(); tunic = PackRgb(cr, cg, cb); return r.ok; }
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
    uint8_t style = 0;   // AttackStyle: an ordinary swing, a jump slash or a spin attack
    void Write(ByteWriter& w) const { w.U16(target); w.U8(hit ? 1 : 0); w.U8(style); }
    bool Read(ByteReader& r) { target = r.U16(); uint8_t h = r.U8(); hit = h == 1; style = r.U8(); return r.ok && h <= 1 && style <= 2; }
};

struct PickupRequest {
    static constexpr MsgType kType = MsgType::PickupRequest;
    uint32_t index = 0;
    bool force = false; // false: walking over it (the server only takes it if it is an upgrade); true: the player chose to swap
    void Write(ByteWriter& w) const { w.U32(index); w.U8(force ? 1 : 0); }
    bool Read(ByteReader& r) { index = r.U32(); const uint8_t f = r.U8(); force = f != 0; return r.ok && f <= 1; }
};

struct UsePotionRequest {
    static constexpr MsgType kType = MsgType::UsePotionRequest;
    void Write(ByteWriter&) const {}
    bool Read(ByteReader& r) { return r.ok; }
};

// Use the ability slot (spell, song, hookshot...). The server decides whether it worked.
// Swap the weapon in hand with a backup one: slot 1..kMaxReserveWeapons.
struct SelectWeaponRequest {
    static constexpr MsgType kType = MsgType::SelectWeaponRequest;
    uint8_t slot = 1;
    void Write(ByteWriter& w) const { w.U8(slot); }
    bool Read(ByteReader& r) { slot = r.U8(); return r.ok && slot >= 1 && slot <= kMaxReserveWeapons; }
};

// Drink a shield potion from the bag (the server picks the one that fits best).
struct UseShieldRequest {
    static constexpr MsgType kType = MsgType::UseShieldRequest;
    void Write(ByteWriter&) const {}
    bool Read(ByteReader& r) { return r.ok; }
};

// A player broke a rock or cut a bush: the server decides what was inside.
struct PropSmashRequest {
    static constexpr MsgType kType = MsgType::PropSmashRequest;
    uint16_t index = 0;
    void Write(ByteWriter& w) const { w.U16(index); }
    bool Read(ByteReader& r) { index = r.U16(); return r.ok && index < kMaxProps; }
};

// Lilo made a cloud at (x, z), beside the player who sends this. The server checks the match is on, that the player is alive and near it, and that
// they have not just made one, then starts the damaging cloud and tells everyone with EvFartCloud.
struct FartCloudRequest {
    static constexpr MsgType kType = MsgType::FartCloudRequest;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) { x = r.F32(); z = r.F32(); return r.ok && Finite(x) && Finite(z); }
};

// Pay the ally with this index (0 to kAllyCount - 1) to join you. You must be standing next to it.
struct HireAllyRequest {
    static constexpr MsgType kType = MsgType::HireAllyRequest;
    uint8_t index = 0;
    void Write(ByteWriter& w) const { w.U8(index); }
    bool Read(ByteReader& r) { index = r.U8(); return r.ok && index < kAllyCount; }
};

// A villager you angered (a carpenter, say) landed a blow on you. The server takes a little health, and not more often than a villager could swing.
struct NpcHitRequest {
    static constexpr MsgType kType = MsgType::NpcHitRequest;
    uint8_t tenths = 5;   // hearts lost, in tenths
    void Write(ByteWriter& w) const { w.U8(tenths); }
    bool Read(ByteReader& r) { tenths = r.U8(); return r.ok && tenths >= 1 && tenths <= 20; }
};

// Get into a cart (`seat` is the one wanted; the other is taken if that one is full), move to its other seat, or get out.
struct VehicleRequest {
    static constexpr MsgType kType = MsgType::VehicleRequest;
    enum Action : uint8_t { Enter = 0, Exit = 1, SwitchSeat = 2 };
    uint8_t action = Enter;
    uint8_t index = 0;
    uint8_t seat = 0;   // Seat::Driver or Seat::Passenger
    void Write(ByteWriter& w) const { w.U8(action); w.U8(index); w.U8(seat); }
    bool Read(ByteReader& r) { action = r.U8(); index = r.U8(); seat = r.U8(); return r.ok && action <= 2 && index < kMaxVehicles && seat <= 1; }
};

// The driver's game runs the cart's physics against the real ground and reports where it is, about 20 times a second. `impact` and `landing` are
// the hardest crash and landing since the last report (for damage).
struct VehicleDrive {
    static constexpr MsgType kType = MsgType::VehicleDrive;
    uint8_t index = 0;
    float x = 0, y = 0, z = 0;
    int16_t yaw = 0;
    int8_t pitch = 0, roll = 0;    // hundredths of a radian
    int16_t speed = 0, slide = 0, vy = 0;
    int8_t steer = 0;              // hundredths of a radian
    int16_t air = 0;               // height above the ground under it (0 on the ground)
    uint8_t flags = 0;             // 1 on the ground, 2 handbrake
    uint16_t impact = 0, landing = 0;
    void Write(ByteWriter& w) const {
        w.U8(index); w.F32(x); w.F32(y); w.F32(z); w.I16(yaw); w.U8(static_cast<uint8_t>(pitch)); w.U8(static_cast<uint8_t>(roll));
        w.I16(speed); w.I16(slide); w.I16(vy); w.U8(static_cast<uint8_t>(steer)); w.I16(air); w.U8(flags); w.U16(impact); w.U16(landing);
    }
    bool Read(ByteReader& r) {
        index = r.U8(); x = r.F32(); y = r.F32(); z = r.F32(); yaw = r.I16(); pitch = static_cast<int8_t>(r.U8()); roll = static_cast<int8_t>(r.U8());
        speed = r.I16(); slide = r.I16(); vy = r.I16(); steer = static_cast<int8_t>(r.U8()); air = r.I16(); flags = r.U8(); impact = r.U16(); landing = r.U16();
        return r.ok && index < kMaxVehicles && Finite(x) && Finite(y) && Finite(z) && flags <= 3;
    }
};

// The host picks which place the match is played in (lobby only).
struct SelectMapRequest {
    static constexpr MsgType kType = MsgType::SelectMapRequest;
    uint8_t map = 0;
    void Write(ByteWriter& w) const { w.U8(map); }
    bool Read(ByteReader& r) { map = r.U8(); return r.ok && map < kMapCount; }
};

// The host asks for another match with everyone who is still connected, straight from the results screen.
struct RematchRequest {
    static constexpr MsgType kType = MsgType::RematchRequest;
    void Write(ByteWriter&) const {}
    bool Read(ByteReader& r) { return r.ok; }
};

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
    bool special = false;     // a heart container chest
    bool supply = false;      // from a supply drop
    uint16_t amount = 0;      // rupees and ammo: how many
    void Write(ByteWriter& w) const { w.F32(x); w.F32(z); w.U8(item); w.U8(rarity); w.U8(static_cast<uint8_t>((chest ? 1 : 0) | (taken ? 2 : 0) | (special ? 4 : 0) | (supply ? 8 : 0))); w.U16(amount); }
    bool Read(ByteReader& r) {
        x = r.F32(); z = r.F32(); item = r.U8(); rarity = r.U8(); uint8_t f = r.U8(); amount = r.U16();
        chest = f & 1; taken = f & 2; special = f & 4; supply = f & 8;
        return r.ok && Finite(x) && Finite(z) && item < static_cast<uint8_t>(ItemId::Count) && rarity < kRarityCount && f <= 15 && amount <= 5000;
    }
};

struct RosterEntry {
    uint16_t id = 0;
    uint8_t flags = 0; // kRosterHost, kRosterReady
    std::string name;
    uint32_t tunic = SkinRgb(0);
};

inline void WritePois(ByteWriter& w, const std::vector<Poi>& pois) {
    w.U8(static_cast<uint8_t>(pois.size()));
    for (const Poi& p : pois) { w.U8(p.name); w.F32(p.center.x); w.F32(p.center.z); w.F32(p.radius); }
}
inline bool ReadPois(ByteReader& r, std::vector<Poi>& pois) {
    const size_t n = r.U8();
    if (n > static_cast<size_t>(kNamesPerMap)) return false;
    pois.assign(n, {});
    for (Poi& p : pois) {
        p.name = r.U8(); p.center.x = r.F32(); p.center.z = r.F32(); p.radius = r.F32();
        if (p.name >= kPoiNameTotal || !Finite(p.center.x) || !Finite(p.center.z) || !Finite(p.radius) || p.radius < 0) return false;
    }
    return r.ok;
}

inline void WriteProps(ByteWriter& w, const std::vector<Prop>& props) {
    w.U16(static_cast<uint16_t>(props.size()));
    for (const Prop& p : props) { w.F32(p.pos.x); w.F32(p.pos.z); w.U8(static_cast<uint8_t>(p.kind)); w.U16(p.rot); }
}
inline bool ReadProps(ByteReader& r, std::vector<Prop>& props) {
    const size_t n = r.U16();
    if (n > static_cast<size_t>(kMaxProps)) return false;
    props.assign(n, {});
    for (Prop& p : props) {
        p.pos.x = r.F32(); p.pos.z = r.F32();
        const uint8_t k = r.U8();
        p.rot = r.U16();
        if (k >= static_cast<uint8_t>(PropKind::Count) || !Finite(p.pos.x) || !Finite(p.pos.z)) return false;
        p.kind = static_cast<PropKind>(k);
    }
    return r.ok;
}

struct Welcome {
    static constexpr MsgType kType = MsgType::Welcome;
    uint16_t playerId = 0;
    uint16_t version = kProtocolVersion;
    uint64_t seed = 0;
    uint8_t limit = kMaxPlayers;
    uint8_t mapId = 0;
    Circle map;
    std::array<Circle, kStormPhaseCount> stormEnds;
    std::vector<LootNet> loot;
    std::vector<Prop> props;
    std::vector<Poi> pois;
    std::vector<RosterEntry> roster;
    void Write(ByteWriter& w) const {
        w.U16(playerId); w.U16(version); w.U64(seed); w.U8(limit); w.U8(mapId);
        WriteCircle(w, map);
        for (const auto& c : stormEnds) WriteCircle(w, c);
        w.U16(static_cast<uint16_t>(loot.size()));
        for (const auto& l : loot) l.Write(w);
        WriteProps(w, props);
        WritePois(w, pois);
        w.U8(static_cast<uint8_t>(roster.size()));
        for (const auto& e : roster) { w.U16(e.id); w.U8(e.flags); w.Str(e.name); w.U8(RgbR(e.tunic)); w.U8(RgbG(e.tunic)); w.U8(RgbB(e.tunic)); }
    }
    bool Read(ByteReader& r) {
        playerId = r.U16(); version = r.U16(); seed = r.U64(); limit = r.U8(); mapId = r.U8();
        if (limit < kMinPlayers || limit > kMaxPlayers || mapId >= kMapCount) return false;
        map = ReadCircle(r);
        for (auto& c : stormEnds) c = ReadCircle(r);
        size_t n = r.U16();
        if (n > kMaxLoot) return false;
        loot.assign(n, {});
        for (auto& l : loot) if (!l.Read(r)) return false;
        if (!ReadProps(r, props)) return false;
        if (!ReadPois(r, pois)) return false;
        size_t m = r.U8();
        roster.assign(m, {});
        for (auto& e : roster) { e.id = r.U16(); e.flags = r.U8(); e.name = r.Str(kMaxNameLen); const uint8_t cr = r.U8(), cg = r.U8(), cb = r.U8(); e.tunic = PackRgb(cr, cg, cb); if (e.flags > 3) r.ok = false; }
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
    uint8_t limit = kMaxPlayers;   // players in the match, bots included (the host's slider)
    void Write(ByteWriter& w) const { w.U8(state); w.U8(alive); w.U16(winner); w.U8(limit); }
    bool Read(ByteReader& r) { state = r.U8(); alive = r.U8(); winner = r.U16(); limit = r.U8(); return r.ok && state <= 4 && limit >= kMinPlayers && limit <= kMaxPlayers; }
};

// One player as seen in a snapshot. 27 bytes.
struct PlayerNet {
    static constexpr uint8_t kAlive = 1, kShield = 2, kBot = 4, kAdult = 8;   // kAdult: under the Adult Power
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
    // What others can see this player wear, so it is drawn on them the way the game draws it: the boots on their feet and the mask on their
    // face (an ItemId, or kNoGear when that gear slot is empty).
    static constexpr uint8_t kNoGear = 0xFF;
    uint8_t boots = kNoGear, mask = kNoGear;
    static uint8_t QuantizeHealth(float h) {
        float v = h / kMaxHealthCap * 255.0f + 0.5f;
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    float Health() const { return health / 255.0f * kMaxHealthCap; }
    void Write(ByteWriter& w) const {
        w.U16(id); w.F32(x); w.F32(z); w.F32(y); w.I16(rot);
        w.U8(health); w.U8(flags); w.U8(weapon); w.U8(weaponRarity); w.U8(potions); w.U8(anim); w.U8(scene); w.U8(shield); w.U8(shieldRarity); w.U8(boots); w.U8(mask);
    }
    bool Read(ByteReader& r) {
        id = r.U16(); x = r.F32(); z = r.F32(); y = r.F32(); rot = r.I16();
        health = r.U8(); flags = r.U8(); weapon = r.U8(); weaponRarity = r.U8(); potions = r.U8(); anim = r.U8(); scene = r.U8(); shield = r.U8(); shieldRarity = r.U8(); boots = r.U8(); mask = r.U8();
        return r.ok && Finite(x) && Finite(y) && Finite(z) && flags <= 15 && weapon < static_cast<uint8_t>(ItemId::Count) &&
               weaponRarity < kRarityCount &&
               shield < static_cast<uint8_t>(ItemId::Count) && shieldRarity < kRarityCount &&
               (boots == kNoGear || boots < static_cast<uint8_t>(ItemId::Count)) && (mask == kNoGear || mask < static_cast<uint8_t>(ItemId::Count));
    }
};

// A mini boss as clients see it. The id is kBossIdBase + index.
struct BossNet {
    uint8_t index = 0;
    uint8_t kind = 0;
    float x = 0, z = 0;
    int16_t rot = 0;
    uint8_t hp = 255;        // health as a fraction of its maximum, 0..255
    bool smashing = false;   // just swung (for the animation)
    int16_t y = 0;           // height above the ground: the dragon flies
    uint8_t mode = 0;        // DragonMode: what it is doing (every boss, not just the dragon)
    uint8_t aux = 0;         // which variant of that: the hand that slams, fire or ice, which swing
    uint32_t Id() const { return kBossIdBase + index; }
    void Write(ByteWriter& w) const { w.U8(index); w.U8(kind); w.F32(x); w.F32(z); w.I16(rot); w.U8(hp); w.U8(smashing ? 1 : 0); w.I16(y); w.U8(mode); w.U8(aux); }
    bool Read(ByteReader& r) {
        index = r.U8(); kind = r.U8(); x = r.F32(); z = r.F32(); rot = r.I16(); hp = r.U8();
        const uint8_t f = r.U8();
        smashing = f != 0;
        y = r.I16(); mode = r.U8(); aux = r.U8();
        return r.ok && index < kMaxBosses && kind < kBossKindCount && Finite(x) && Finite(z) && f <= 1 && mode < static_cast<uint8_t>(DragonMode::Count);
    }
};

// Server-authored helper pose and action age. Rock endpoints preserve visible, dodgeable flight.
struct HelperNet {
    uint8_t index = 0, boss = 0, kind = 0, mode = 0, hp = 255, action = 0, personality = 0;
    float x = 0, z = 0, age = 0;
    int16_t rot = 0, y = 0;
    bool rock = false;
    float rockAge = 0, fromX = 0, fromZ = 0, toX = 0, toZ = 0;
    uint32_t Id() const { return kHelperIdBase + index; }
    void Write(ByteWriter& w) const {
        w.U8(index); w.U8(boss); w.U8(kind); w.U8(mode); w.U8(hp); w.U8(action); w.U8(personality);
        w.F32(x); w.F32(z); w.F32(age); w.I16(rot); w.I16(y); w.U8(rock ? 1 : 0);
        if (rock) { w.F32(rockAge); w.F32(fromX); w.F32(fromZ); w.F32(toX); w.F32(toZ); }
    }
    bool Read(ByteReader& r) {
        index=r.U8(); boss=r.U8(); kind=r.U8(); mode=r.U8(); hp=r.U8(); action=r.U8(); personality=r.U8();
        x=r.F32(); z=r.F32(); age=r.F32(); rot=r.I16(); y=r.I16(); const auto f=r.U8(); rock=f!=0;
        if (rock) { rockAge=r.F32(); fromX=r.F32(); fromZ=r.F32(); toX=r.F32(); toZ=r.F32(); }
        return r.ok && index<kMaxHelpers && boss<kMaxBosses && kind<static_cast<uint8_t>(HelperKind::Count) &&
            mode<static_cast<uint8_t>(BokoMode::Count) && personality<3 && f<=1 && Finite(x) && Finite(z) &&
            Finite(age) && age>=0 && age<=3600 && y>=0 && y<=100 &&
            (!rock || (Finite(rockAge) && rockAge>=0 && rockAge<=0.8f && Finite(fromX) && Finite(fromZ) && Finite(toX) && Finite(toZ)));
    }
};

// A hireable ally as clients see it.
struct AllyNet {
    uint8_t index = 0;
    uint8_t kind = 0;
    float x = 0, z = 0;
    int16_t rot = 0;
    uint8_t hp = 255;           // health as a fraction of its maximum, 0..255
    uint16_t owner = kNoPlayer16;   // who hired it (kNoPlayer16 while it is free)
    uint8_t flags = 0;          // 1 walking, 2 attacking
    void Write(ByteWriter& w) const { w.U8(index); w.U8(kind); w.F32(x); w.F32(z); w.I16(rot); w.U8(hp); w.U16(owner); w.U8(flags); }
    bool Read(ByteReader& r) {
        index = r.U8(); kind = r.U8(); x = r.F32(); z = r.F32(); rot = r.I16(); hp = r.U8(); owner = r.U16(); flags = r.U8();
        return r.ok && index < kAllyCount && kind < kAllyCount && Finite(x) && Finite(z) && flags <= 3;
    }
};

// A cart as clients see it. Who is in it is decided by the server; where it is comes from its driver's game (or the server's own physics when a
// bot drives it or nobody does). Clients set it on their own ground and tilt it to fit; `air` is how high it is above that ground (a jump).
struct VehicleNet {
    static constexpr uint8_t kWrecked = 1, kGrounded = 2, kDrift = 4;
    uint8_t index = 0;
    float x = 0, y = 0, z = 0;
    int16_t yaw = 0;
    int16_t speed = 0;
    int8_t steer = 0;              // hundredths of a radian
    int16_t air = 0;
    uint8_t hp = 255;              // health as a share of kCartHealth, 0..255
    uint16_t driver = kNoPlayer16, passenger = kNoPlayer16;
    uint8_t flags = kGrounded;
    uint32_t Id() const { return kVehicleIdBase + index; }
    void Write(ByteWriter& w) const {
        // the position to the nearest unit as 16 bits (every map fits in +-32000): 20 bytes a cart, 20 times a second
        auto unit = [](float f) { return static_cast<int16_t>(std::lround(std::clamp(f, -32000.0f, 32000.0f))); };
        w.U8(index); w.I16(unit(x)); w.I16(unit(y)); w.I16(unit(z)); w.I16(yaw); w.I16(speed); w.U8(static_cast<uint8_t>(steer)); w.I16(air); w.U8(hp); w.U16(driver); w.U16(passenger); w.U8(flags);
    }
    bool Read(ByteReader& r) {
        index = r.U8(); x = r.I16(); y = r.I16(); z = r.I16(); yaw = r.I16(); speed = r.I16(); steer = static_cast<int8_t>(r.U8()); air = r.I16(); hp = r.U8();
        driver = r.U16(); passenger = r.U16(); flags = r.U8();
        return r.ok && index < kMaxVehicles && Finite(x) && Finite(y) && Finite(z) && flags <= 7;
    }
};

struct Snapshot {
    static constexpr MsgType kType = MsgType::Snapshot;
    uint32_t tick = 0;       // server tick counter, kTickHz per second
    float stormTime = 0;     // seconds since the storm started
    uint8_t state = 0;
    uint8_t alive = 0;
    uint8_t epoch = 0;       // bumped whenever the server teleports this client (match start); inputs with an old epoch are ignored
    uint8_t lobbyLeft = 255; // seconds until the lobby starts the match by itself; 255 when the timer is off
    std::vector<PlayerNet> players; // first entry is always the receiving client
    std::vector<HelperNet> helpers;
    std::vector<BossNet> bosses;    // the mini bosses near this client
    std::vector<AllyNet> allies;    // the hireable allies near this client
    std::vector<VehicleNet> vehicles;   // the carts near this client (and the one it is in)
    void Write(ByteWriter& w) const {
        w.U32(tick); w.F32(stormTime); w.U8(state); w.U8(alive); w.U8(epoch); w.U8(lobbyLeft);
        w.U8(static_cast<uint8_t>(players.size()));
        for (const auto& p : players) p.Write(w);
        w.U8(static_cast<uint8_t>(bosses.size()));
        for (const auto& b : bosses) b.Write(w);
        w.U8(static_cast<uint8_t>(helpers.size()));
        for (const auto& h : helpers) h.Write(w);
        w.U8(static_cast<uint8_t>(allies.size()));
        for (const auto& a : allies) a.Write(w);
        w.U8(static_cast<uint8_t>(vehicles.size()));
        for (const auto& v : vehicles) v.Write(w);
    }
    bool Read(ByteReader& r) {
        tick = r.U32(); stormTime = r.F32(); state = r.U8(); alive = r.U8(); epoch = r.U8(); lobbyLeft = r.U8();
        size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPlayers) || state > 4 || !Finite(stormTime)) return false; // up to everyone, while revealing
        players.assign(n, {});
        for (auto& p : players) if (!p.Read(r)) return false;
        const size_t nb = r.U8();
        if (nb > static_cast<size_t>(kMaxBosses)) return false;
        bosses.assign(nb, {});
        for (auto& b : bosses) if (!b.Read(r)) return false;
        const size_t nh = r.U8();
        if (nh > static_cast<size_t>(kMaxHelpers)) return false;
        helpers.assign(nh, {});
        for (auto& h : helpers) if (!h.Read(r)) return false;
        const size_t na = r.U8();
        if (na > static_cast<size_t>(kAllyCount)) return false;
        allies.assign(na, {});
        for (auto& a : allies) if (!a.Read(r)) return false;
        const size_t nv = r.U8();
        if (nv > static_cast<size_t>(kMaxVehicles)) return false;
        vehicles.assign(nv, {});
        for (auto& v : vehicles) if (!v.Read(r)) return false;
        return r.ok;
    }
};

struct EvBossDown {
    static constexpr MsgType kType = MsgType::EvBossDown;
    uint16_t boss = 0, killer = kNoPlayer16;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.U16(boss); w.U16(killer); w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) { boss = r.U16(); killer = r.U16(); x = r.F32(); z = r.F32(); return r.ok && IsBossId(boss) && Finite(x) && Finite(z); }
};

// The weather changed (a new spell begins): `season`, what the sky does, how strongly, and how long the spell lasts.
struct EvWeather {
    static constexpr MsgType kType = MsgType::EvWeather;
    uint8_t season = 0, sky = 0, intensity = 0;
    float seconds = 0;
    void Write(ByteWriter& w) const { w.U8(season); w.U8(sky); w.U8(intensity); w.F32(seconds); }
    bool Read(ByteReader& r) { season = r.U8(); sky = r.U8(); intensity = r.U8(); seconds = r.F32(); return r.ok && season < kSeasonCount && sky < kSkyCount && intensity <= 100 && Finite(seconds); }
};

// An ally was hired (owner = who), let go because its owner fell (owner = kNoPlayer16) or fell itself (`fell`).
struct EvAlly {
    static constexpr MsgType kType = MsgType::EvAlly;
    uint8_t index = 0;
    uint16_t owner = kNoPlayer16;
    uint8_t status = 0;   // 0 hired, 1 released, 2 fell
    void Write(ByteWriter& w) const { w.U8(index); w.U16(owner); w.U8(status); }
    bool Read(ByteReader& r) { index = r.U8(); owner = r.U16(); status = r.U8(); return r.ok && index < kAllyCount && status <= 2; }
};

// An ally attacked (or, when `target` is its own owner, healed them): where the target was.
struct EvAllyAction {
    static constexpr MsgType kType = MsgType::EvAllyAction;
    uint8_t index = 0;
    uint16_t target = kNoPlayer16;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.U8(index); w.U16(target); w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) { index = r.U8(); target = r.U16(); x = r.F32(); z = r.F32(); return r.ok && index < kAllyCount && Finite(x) && Finite(z); }
};

// The replay of the match that just ended, in two kinds of message: this describes it (who, how many frames, who eliminated whom)...
struct EvReplayHeader {
    static constexpr MsgType kType = MsgType::EvReplayHeader;
    uint16_t frames = 0;
    std::vector<uint16_t> ids;
    std::vector<ReplayKill> kills;
    void Write(ByteWriter& w) const {
        w.U16(frames); w.U8(static_cast<uint8_t>(ids.size()));
        for (uint16_t id : ids) w.U16(id);
        w.U16(static_cast<uint16_t>(kills.size()));
        for (const auto& k : kills) { w.U16(k.frame); w.U16(k.killer); w.U16(k.victim); }
    }
    bool Read(ByteReader& r) {
        frames = r.U16();
        const size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPlayers) || frames > kReplayMaxFrames) return false;
        ids.assign(n, 0);
        for (auto& id : ids) id = r.U16();
        const size_t nk = r.U16();
        if (nk > 2000) return false;
        kills.assign(nk, {});
        for (auto& k : kills) { k.frame = r.U16(); k.killer = r.U16(); k.victim = r.U16(); if (r.ok && k.frame >= frames) return false; }
        return r.ok;
    }
};
// ...and these carry the frames, a few at a time: `players` x, z pairs per frame.
struct EvReplayChunk {
    static constexpr MsgType kType = MsgType::EvReplayChunk;
    uint16_t first = 0;
    uint8_t players = 0;
    std::vector<std::vector<int16_t>> frames;
    void Write(ByteWriter& w) const {
        w.U16(first); w.U8(players); w.U8(static_cast<uint8_t>(frames.size()));
        for (const auto& f : frames) for (size_t i = 0; i < static_cast<size_t>(players) * 2; i++) w.I16(i < f.size() ? f[i] : 0);
    }
    bool Read(ByteReader& r) {
        first = r.U16(); players = r.U8();
        const size_t n = r.U8();
        if (players > kMaxPlayers || first >= kReplayMaxFrames || n > 64) return false;
        frames.assign(n, std::vector<int16_t>(static_cast<size_t>(players) * 2));
        for (auto& f : frames) for (auto& v : f) v = r.I16();
        return r.ok;
    }
};

// A supply drop has been announced: a crate lands at (x, z) in `delay` seconds.
struct EvSupplyDrop {
    static constexpr MsgType kType = MsgType::EvSupplyDrop;
    float x = 0, z = 0, delay = 0;
    void Write(ByteWriter& w) const { w.F32(x); w.F32(z); w.F32(delay); }
    bool Read(ByteReader& r) { x = r.F32(); z = r.F32(); delay = r.F32(); return r.ok && Finite(x) && Finite(z) && Finite(delay) && delay >= 0; }
};

// A rock or bush is gone for everybody. If somebody's hit broke it, `by` is who and `item`/`amount` what was inside (item 255 for nothing).
struct EvPropBroken {
    static constexpr MsgType kType = MsgType::EvPropBroken;
    uint16_t index = 0, by = kNoPlayer16;
    uint8_t item = 255;
    uint16_t amount = 0;
    void Write(ByteWriter& w) const { w.U16(index); w.U16(by); w.U8(item); w.U16(amount); }
    bool Read(ByteReader& r) { index = r.U16(); by = r.U16(); item = r.U8(); amount = r.U16(); return r.ok && index < kMaxProps && (item == 255 || item < static_cast<uint8_t>(ItemId::Count)) && amount <= 5000; }
};

// A toxic cloud has started at (x, z), made by `by` (Lilo's owner); it lasts kFartCloudSeconds and hurts whoever stays in it.
struct EvFartCloud {
    static constexpr MsgType kType = MsgType::EvFartCloud;
    uint16_t by = kNoPlayer16;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.U16(by); w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) { by = r.U16(); x = r.F32(); z = r.F32(); return r.ok && Finite(x) && Finite(z); }
};

// A blast is coming: a ring at (x, z) that goes off `delay` seconds from now (the dragon's fireballs, meteors and dive).
struct EvStrike {
    static constexpr MsgType kType = MsgType::EvStrike;
    uint16_t by = 0;
    float x = 0, z = 0, radius = 0, delay = 0;
    uint8_t style = 0;   // StrikeStyle: how it looks when it lands
    void Write(ByteWriter& w) const { w.U16(by); w.F32(x); w.F32(z); w.F32(radius); w.F32(delay); w.U8(style); }
    bool Read(ByteReader& r) { by = r.U16(); x = r.F32(); z = r.F32(); radius = r.F32(); delay = r.F32(); style = r.U8(); return r.ok && Finite(x) && Finite(z) && Finite(radius) && Finite(delay) && radius >= 0 && delay >= 0 && style < static_cast<uint8_t>(StrikeStyle::Count); }
};

// The map's dragon has arrived.
struct EvBossSpawn {
    static constexpr MsgType kType = MsgType::EvBossSpawn;
    uint16_t boss = 0;
    uint8_t kind = 0;
    float x = 0, z = 0;
    void Write(ByteWriter& w) const { w.U16(boss); w.U8(kind); w.F32(x); w.F32(z); }
    bool Read(ByteReader& r) { boss = r.U16(); kind = r.U8(); x = r.F32(); z = r.F32(); return r.ok && IsBossId(boss) && kind < kBossKindCount && Finite(x) && Finite(z); }
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
    uint32_t tunic = SkinRgb(0);
    void Write(ByteWriter& w) const { w.U16(id); w.U8(flags); w.Str(name); w.U8(RgbR(tunic)); w.U8(RgbG(tunic)); w.U8(RgbB(tunic)); }
    bool Read(ByteReader& r) { id = r.U16(); flags = r.U8(); name = r.Str(kMaxNameLen); const uint8_t cr = r.U8(), cg = r.U8(), cb = r.U8(); tunic = PackRgb(cr, cg, cb); return r.ok && flags <= 3; }
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
    uint8_t mapId = 0;
    Circle map;
    std::array<Circle, kStormPhaseCount> stormEnds;
    std::vector<LootNet> loot;
    std::vector<Prop> props;
    std::vector<Poi> pois;
    void Write(ByteWriter& w) const {
        w.U8(mapId);
        WriteCircle(w, map);
        for (const auto& c : stormEnds) WriteCircle(w, c);
        w.U16(static_cast<uint16_t>(loot.size()));
        for (const auto& l : loot) l.Write(w);
        WriteProps(w, props);
        WritePois(w, pois);
    }
    bool Read(ByteReader& r) {
        mapId = r.U8();
        if (mapId >= kMapCount) return false;
        map = ReadCircle(r);
        for (auto& c : stormEnds) c = ReadCircle(r);
        size_t n = r.U16();
        if (n > kMaxLoot) return false;
        loot.assign(n, {});
        for (auto& l : loot) if (!l.Read(r)) return false;
        if (!ReadProps(r, props)) return false;
        if (!ReadPois(r, pois)) return false;
        return r.ok;
    }
};

// What one player carries, sent to that player whenever it changes (and to nobody else). Times are "seconds left" at the moment of sending.
struct ItemRef {
    uint8_t item = 0, rarity = 0;
    bool Valid() const { return item < static_cast<uint8_t>(ItemId::Count) && rarity < kRarityCount; }
};

// Final standings, sent to everyone when the match ends. Best score first.
struct ResultRow {
    uint16_t id = 0;
    uint8_t placement = 0, kills = 0, chests = 0;
    uint16_t damageTenths = 0; // hearts of damage dealt, in tenths
    uint32_t score = 0;
};
struct EvResults {
    static constexpr MsgType kType = MsgType::EvResults;
    std::vector<ResultRow> rows;
    void Write(ByteWriter& w) const {
        w.U8(static_cast<uint8_t>(rows.size()));
        for (const auto& r : rows) { w.U16(r.id); w.U8(r.placement); w.U8(r.kills); w.U8(r.chests); w.U16(r.damageTenths); w.U32(r.score); }
    }
    bool Read(ByteReader& r) {
        const size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPlayers)) return false;
        rows.assign(n, {});
        for (auto& row : rows) { row.id = r.U16(); row.placement = r.U8(); row.kills = r.U8(); row.chests = r.U8(); row.damageTenths = r.U16(); row.score = r.U32(); }
        return r.ok;
    }
};

struct EvInventory {
    static constexpr MsgType kType = MsgType::EvInventory;
    float maxHealth = kMaxHealth;
    float shield = 0;                       // the shield bar, 0 to kMaxShield
    uint8_t heartPieces = 0;
    std::vector<ItemRef> potions;           // the bag, at most kMaxPotions
    std::vector<ItemRef> reserve;           // backup weapons, at most kMaxReserveWeapons
    bool hasAbility = false;
    ItemRef ability;
    float abilityReadyIn = 0;
    uint16_t rupees = 0;
    std::array<uint8_t, kAmmoKinds> ammo = {};
    bool hasMark = false;                   // Farore's Wind has a spot marked
    uint8_t gearMask = 0;
    std::array<ItemRef, kGearSlots> gear = {};
    float invulnLeft = 0, speedLeft = 0, speedMult = 1, revealLeft = 0, stunLeft = 0, burnLeft = 0, regenLeft = 0, shieldLeft = 0;
    float magic = kMaxMagic;                // the magic meter, 0 to kMaxMagic
    float adultLeft = 0;                    // seconds of Adult Power left

    void Write(ByteWriter& w) const {
        w.F32(maxHealth); w.F32(shield); w.U8(heartPieces);
        w.U16(rupees);
        for (uint8_t a : ammo) w.U8(a);
        w.U8(static_cast<uint8_t>(potions.size()));
        for (const auto& p : potions) { w.U8(p.item); w.U8(p.rarity); }
        w.U8(static_cast<uint8_t>(reserve.size()));
        for (const auto& p : reserve) { w.U8(p.item); w.U8(p.rarity); }
        w.U8((hasAbility ? 1 : 0) | (hasMark ? 2 : 0));
        w.U8(ability.item); w.U8(ability.rarity); w.F32(abilityReadyIn);
        w.U8(gearMask);
        for (int i = 0; i < kGearSlots; i++) if (gearMask & (1 << i)) { w.U8(gear[i].item); w.U8(gear[i].rarity); }
        w.F32(invulnLeft); w.F32(speedLeft); w.F32(speedMult); w.F32(revealLeft);
        w.F32(stunLeft); w.F32(burnLeft); w.F32(regenLeft); w.F32(shieldLeft); w.F32(magic); w.F32(adultLeft);
    }
    bool Read(ByteReader& r) {
        maxHealth = r.F32(); shield = r.F32(); heartPieces = r.U8();
        rupees = r.U16();
        for (uint8_t& a : ammo) a = r.U8();
        if (!Finite(shield) || shield < 0 || shield > kMaxShield + 0.001f) return false;
        size_t n = r.U8();
        if (n > static_cast<size_t>(kMaxPotions)) return false;
        potions.assign(n, {});
        for (auto& p : potions) { p.item = r.U8(); p.rarity = r.U8(); if (r.ok && !p.Valid()) return false; }
        size_t rn = r.U8();
        if (rn > static_cast<size_t>(kMaxReserveWeapons)) return false;
        reserve.assign(rn, {});
        for (auto& p : reserve) { p.item = r.U8(); p.rarity = r.U8(); if (r.ok && !p.Valid()) return false; }
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
        stunLeft = r.F32(); burnLeft = r.F32(); regenLeft = r.F32(); shieldLeft = r.F32(); magic = r.F32(); adultLeft = r.F32();
        return r.ok && Finite(adultLeft) && adultLeft >= 0 && adultLeft <= kAdultSeconds + 1.0f && Finite(magic) && magic >= 0 && magic <= kMaxMagic + 0.001f && Finite(maxHealth) && maxHealth >= 1 && maxHealth <= kMaxHealthCap && Finite(abilityReadyIn) && abilityReadyIn >= 0 &&
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
