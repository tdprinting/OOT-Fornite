#pragma once
// The Lon Lon Buggy: a wooden cart for two (a driver and a passenger) that anyone can drive, players and bots alike. This file is the cart's
// rules and its physics, in plain C++ so they are unit-tested and shared: the driver's own game runs the physics against the real floor
// (RoyaleMod.cpp), the server runs the same physics against the bots' navigation grid for carts that a bot drives or nobody drives.
//
// The physics is a small car model: the throttle pushes the cart along its heading (less when climbing, more going downhill), the front wheels
// steer it round an arc set by the wheelbase, the tyres grip so it doesn't slide sideways (the handbrake lets the back slide out for a drift),
// the body sits on the ground under its four wheels (tilting with the slope) and flies off ledges and bumps, and walls and water stop it with a
// bump. Every step reports how hard it hit something and how hard it landed, which is where crash damage comes from.
//
// The model and its measures come from Blender (tools/cart/, shared/cart_geometry.h and shared/cart_model.h).
#include "cart_geometry.h"
#include "storm.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace royale {

// ---- the numbers ----------------------------------------------------------------------------------------------------------------------
constexpr float kCartMaxSpeed = 420.0f;      // units per second flat out (Link runs 100, sprints 135)
constexpr float kCartMaxReverse = 150.0f;
constexpr float kCartAccel = 260.0f;         // from standing, units per second per second (it fades towards top speed)
constexpr float kCartBrake = 760.0f;         // braking, or pushing the other way
constexpr float kCartCoast = 70.0f;          // rolling to a stop with nothing pressed
constexpr float kCartDrag = 0.00035f;        // air drag, times speed squared
constexpr float kCartMaxSteer = 0.56f;       // front wheel angle at full lock, radians (about 32 degrees), less at speed
constexpr float kCartSteerRate = 3.2f;       // how fast the wheels turn to where the stick points, radians per second
constexpr float kCartGrip = 9.0f;            // how fast sideways slide dies away (per second)
constexpr float kCartDriftGrip = 1.6f;       // ... with the handbrake on
constexpr float kCartGravity = 1400.0f;      // falling (the same pull bots fall with)
constexpr float kCartMaxStep = 34.0f;        // a rise taller than this in front of a wheel is a wall
constexpr float kCartLeaveGround = 7.0f;     // the ground dropping further than this under the cart in one step throws it into the air
constexpr float kCartSlopePull = 0.55f;      // share of gravity that a slope adds to or takes from the speed

constexpr float kCartHealth = 14.0f;         // hearts' worth of damage it takes to wreck one
constexpr float kCartCrashFrom = 170.0f;     // hitting something slower than this does no harm
constexpr float kCartCrashDamage = 0.012f;   // cart damage per unit of speed over that
constexpr float kRiderCrashFrom = 300.0f;    // the riders feel crashes harder than this
constexpr float kRiderCrashDamage = 0.006f;
constexpr float kCartLandFrom = 650.0f;      // landing faster than this hurts the cart
constexpr float kCartLandDamage = 0.008f;
constexpr float kRamMinSpeed = 120.0f;       // running somebody over: only at speed
constexpr float kRamBase = 0.6f;             // hearts at kRamMinSpeed
constexpr float kRamPerSpeed = 1.0f / 150.0f;   // ... plus this per unit of speed over it
constexpr float kRamCooldown = 1.0f;         // seconds before the same cart can hurt the same person again
constexpr float kRamCartDamage = 0.3f;       // what a ram costs the cart
constexpr float kCartHitRadius = 70.0f;      // how far from its centre a sword reaches it
constexpr float kCartWreckSeconds = 9.0f;    // a wreck burns this long, then is gone
constexpr float kCartBlastRadius = 260.0f;   // a wreck's firebox goes up: everyone this close is hurt
constexpr float kCartBlastDamage = 1.5f;
constexpr float kEnterRange = 150.0f;        // how close to a seat you must stand to get in
constexpr float kMountSeconds = 0.6f;        // the climb into the saddle (or down from it)
constexpr int kMaxVehicles = 16;
constexpr uint32_t kVehicleIdBase = 6000;    // a cart as an attack target: above the bosses' ids
constexpr bool IsVehicleId(uint32_t id) { return id >= kVehicleIdBase && id < kVehicleIdBase + kMaxVehicles; }

// How many carts a map gets, by its size.
inline int VehicleCountFor(float mapRadius) { return (std::max)(4, (std::min)(kMaxVehicles, 3 + static_cast<int>(mapRadius / 1100.0f))); }

enum class Seat : uint8_t { Driver = 0, Passenger = 1, None = 255 };

// ---- the physics --------------------------------------------------------------------------------------------------------------------

// Where the cart is and how it is moving. x, z: the middle between the axles; y: the bottom of the wheels (the ground it rests on).
struct CartBody {
    float x = 0, y = 0, z = 0;
    float yaw = 0;           // radians; 0 faces +Z, as OoT's rot.y = 0 does (a binary angle of 0x4000 is +pi/2)
    float speed = 0;         // forwards, units per second (negative backing up)
    float slide = 0;         // sideways, to its left, units per second
    float vy = 0;            // up, while in the air
    float steer = 0;         // front wheel angle, radians (+ is to the left)
    float pitch = 0, roll = 0;   // radians: nose up is +pitch, left side up is +roll
    float yawRate = 0;
    bool grounded = true;
    float airTime = 0;
};

// What the driver is asking for.
struct CartControls {
    float throttle = 0;      // -1 (back) to 1 (forward)
    float steer = 0;         // -1 (right) to 1 (left)
    bool brake = false;
    bool handbrake = false;
};

// The world the cart drives in. `ground` gives the floor's height at a point (false where there is none: a pit, the void). `solid` says whether a
// point is closed to the cart (a wall, a rock, water, off the map). Either may be empty: open flat ground at y = 0.
struct CartWorld {
    std::function<bool(float x, float z, float* y)> ground;
    std::function<bool(float x, float z)> solid;
    float Ground(float x, float z, bool* found = nullptr) const {
        float y = 0;
        const bool ok = !ground || ground(x, z, &y);
        if (found) *found = ok;
        return ok ? y : -1e6f;
    }
    bool Solid(float x, float z) const { return solid && solid(x, z); }
};

// What one step did, for damage and effects.
struct CartStep {
    float impact = 0;        // speed it hit a wall with (0 for none)
    float landing = 0;       // speed it came down from a jump with (0 for none)
    bool tookOff = false;
};

inline float Sin(float a) { return std::sin(a); }
inline float Cos(float a) { return std::cos(a); }

// A point given in the cart's own axes (cart_geometry.h: x to its left, y up, z forwards) in the world, tilt included.
inline void CartToWorld(const CartBody& b, float lx, float ly, float lz, float* wx, float* wy, float* wz) {
    // roll about the forward axis, pitch about the left axis, then yaw about up
    const float cr = Cos(b.roll), sr = Sin(b.roll), cp = Cos(b.pitch), sp = Sin(b.pitch), cy = Cos(b.yaw), sy = Sin(b.yaw);
    const float x1 = lx * cr - ly * sr, y1 = lx * sr + ly * cr, z1 = lz;      // roll: left side up for +roll
    const float y2 = y1 * cp + z1 * sp, z2 = -y1 * sp + z1 * cp, x2 = x1;      // pitch: nose up for +pitch
    *wx = b.x + x2 * cy + z2 * sy;
    *wz = b.z - x2 * sy + z2 * cy;
    *wy = b.y + y2;
}
inline void CartToWorld(const CartBody& b, const cart::P3& p, float* wx, float* wy, float* wz) { CartToWorld(b, p.x, p.y, p.z, wx, wy, wz); }

// Is the point (x, z) inside the cart's footprint (grown by `margin`)?
inline bool InsideCart(const CartBody& b, float x, float z, float margin = 0.0f) {
    const float dx = x - b.x, dz = z - b.z;
    const float cy = Cos(b.yaw), sy = Sin(b.yaw);
    const float lx = dx * cy - dz * sy, lz = dx * sy + dz * cy;
    return lx >= cart::kMinX - margin && lx <= cart::kMaxX + margin && lz >= cart::kMinZ - margin && lz <= cart::kMaxZ + margin;
}

// The heading as OoT's binary angle (0x10000 = a full turn) and back.
inline int16_t YawToBinang(float yaw) { return static_cast<int16_t>(static_cast<int32_t>(std::lround(yaw * (32768.0f / 3.14159265f))) & 0xFFFF); }
inline float BinangToYaw(int16_t a) { return static_cast<float>(a) * (3.14159265f / 32768.0f); }

namespace cartdetail {
// The four wheels' places on the ground (front left, front right, back left, back right), for a cart at (x, z) facing yaw.
inline void WheelSpots(float x, float z, float yaw, float out[4][2]) {
    const float cy = Cos(yaw), sy = Sin(yaw);
    const float half = cart::kTrack * 0.5f;
    const float lx[4] = {half, -half, half, -half}, lz[4] = {cart::kFrontAxle, cart::kFrontAxle, cart::kBackAxle, cart::kBackAxle};
    for (int i = 0; i < 4; i++) { out[i][0] = x + lx[i] * cy + lz[i] * sy; out[i][1] = z - lx[i] * sy + lz[i] * cy; }
}
// The ground under the wheels: the height for the body, its tilt, and how high the rise is under the front (or back) pair.
struct Footing { float y, pitch, roll; bool any; float wheel[4]; };
inline Footing FootingAt(const CartWorld& w, float x, float z, float yaw) {
    float spots[4][2];
    WheelSpots(x, z, yaw, spots);
    Footing f{};
    float sum = 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        bool found = false;
        f.wheel[i] = w.Ground(spots[i][0], spots[i][1], &found);
        if (found) { sum += f.wheel[i]; n++; }
    }
    f.any = n > 0;
    if (!f.any) return f;
    const float avg = sum / n;
    for (int i = 0; i < 4; i++) if (f.wheel[i] < -1e5f) f.wheel[i] = avg - 200.0f;   // a wheel over a hole hangs down into it
    // Over an edge: the wheels past it hang in the air. While its middle is still over the high ground the cart rests on the wheels that
    // are (nose dipping a little); once the middle is past the edge it goes over, down to the ground under its middle.
    float top = -1e9f;
    for (int i = 0; i < 4; i++) top = (std::max)(top, f.wheel[i]);
    bool found = false;
    const float mid = w.Ground(x, z, &found);
    const float drop = kCartMaxStep * 1.5f;
    for (int i = 0; i < 4; i++) {
        if (top - f.wheel[i] <= drop) continue;
        if (found && mid > top - drop) f.wheel[i] = top - 12.0f;
        else for (int k = 0; k < 4; k++) if (found && f.wheel[k] > mid + drop) f.wheel[k] = mid;
    }
    f.y = (f.wheel[0] + f.wheel[1] + f.wheel[2] + f.wheel[3]) * 0.25f;
    f.pitch = std::atan2((f.wheel[0] + f.wheel[1]) * 0.5f - (f.wheel[2] + f.wheel[3]) * 0.5f, cart::kWheelBase);
    f.roll = std::atan2((f.wheel[0] + f.wheel[2]) * 0.5f - (f.wheel[1] + f.wheel[3]) * 0.5f, cart::kTrack);
    const float lim = 0.6f;
    f.pitch = std::clamp(f.pitch, -lim, lim);
    f.roll = std::clamp(f.roll, -lim, lim);
    return f;
}
// Can the cart stand at (x, z) facing yaw, coming from height `fromY`? Its corners must be open, and no wheel may meet a rise taller than a
// step (that is a cliff face or a block, which it can't drive up).
inline bool Fits(const CartWorld& w, float x, float z, float yaw, float fromY) {
    const float cy = Cos(yaw), sy = Sin(yaw);
    const float cx[6] = {cart::kMaxX - 6, cart::kMinX + 6, cart::kMaxX - 6, cart::kMinX + 6, cart::kMaxX - 6, cart::kMinX + 6};
    const float cz[6] = {cart::kMaxZ - 4, cart::kMaxZ - 4, cart::kMinZ + 4, cart::kMinZ + 4, 0, 0};
    for (int i = 0; i < 6; i++) if (w.Solid(x + cx[i] * cy + cz[i] * sy, z - cx[i] * sy + cz[i] * cy)) return false;
    float spots[4][2];
    WheelSpots(x, z, yaw, spots);
    for (auto& s : spots) {
        bool found = false;
        const float gy = w.Ground(s[0], s[1], &found);
        if (found && gy - fromY > kCartMaxStep) return false;
    }
    return true;
}
inline float Approach(float v, float target, float step) { return v < target ? (std::min)(target, v + step) : (std::max)(target, v - step); }
inline float WrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}
} // namespace cartdetail

// Settle a cart that has just been placed (or was moved) onto the ground under it.
inline void SettleCart(CartBody& b, const CartWorld& w) {
    const cartdetail::Footing f = cartdetail::FootingAt(w, b.x, b.z, b.yaw);
    if (!f.any) return;
    b.y = f.y; b.pitch = f.pitch; b.roll = f.roll; b.vy = 0; b.grounded = true; b.airTime = 0;
}

// One step of `dt` seconds (any length: long steps are cut into short ones).
inline CartStep StepCart(CartBody& b, const CartControls& in, const CartWorld& w, float dt) {
    using namespace cartdetail;
    CartStep out;
    if (!(dt > 0)) return out;
    const int pieces = (std::max)(1, static_cast<int>(std::ceil(dt / 0.025f)));
    const float h = dt / static_cast<float>(pieces);
    const float throttle = std::clamp(std::isfinite(in.throttle) ? in.throttle : 0.0f, -1.0f, 1.0f);
    const float steerIn = std::clamp(std::isfinite(in.steer) ? in.steer : 0.0f, -1.0f, 1.0f);
    for (int piece = 0; piece < pieces; piece++) {
        // ---- steering: the wheels turn towards the stick, and turn less the faster it goes
        const float speedShare = (std::min)(1.0f, std::fabs(b.speed) / kCartMaxSpeed);
        b.steer = Approach(b.steer, steerIn * kCartMaxSteer * (1.0f - 0.55f * speedShare), kCartSteerRate * h);

        // ---- the engine, the brakes and the slope (only with wheels on the ground)
        if (b.grounded) {
            float accel = 0;
            if (in.brake) {
                accel = -std::copysign((std::min)(kCartBrake, std::fabs(b.speed) / h), b.speed);
            } else if (throttle > 0.05f) {
                if (b.speed < -5.0f) accel = kCartBrake * throttle;
                else accel = (kCartAccel + 40.0f) * throttle * (std::max)(0.0f, 1.0f - b.speed / kCartMaxSpeed);
            } else if (throttle < -0.05f) {
                if (b.speed > 5.0f) accel = -kCartBrake * -throttle;
                else accel = -(kCartAccel * 0.7f + 20.0f) * -throttle * (std::max)(0.0f, 1.0f + b.speed / kCartMaxReverse);
            } else {
                accel = -std::copysign((std::min)(kCartCoast, std::fabs(b.speed) / h), b.speed);
            }
            accel -= kCartGravity * kCartSlopePull * Sin(b.pitch);                         // uphill slows it, downhill speeds it up
            accel -= std::copysign(kCartDrag * b.speed * b.speed, b.speed);
            b.speed += accel * h;
            if (in.handbrake) b.speed = Approach(b.speed, 0.0f, 120.0f * h);
            b.speed = std::clamp(b.speed, -kCartMaxReverse * 1.4f, kCartMaxSpeed * 1.35f);

            // turning round the arc the front wheels set, and the slide: the back steps out with the handbrake on
            const float targetRate = b.speed / cart::kWheelBase * std::tan(b.steer) * (in.handbrake ? 1.45f : 1.0f);
            b.yawRate = Approach(b.yawRate, targetRate, 12.0f * h);
            if (in.handbrake) b.slide += -b.speed * b.yawRate * 0.35f * h;
            b.slide = Approach(b.slide, 0.0f, std::fabs(b.slide) * (in.handbrake ? kCartDriftGrip : kCartGrip) * h + 4.0f * h);
        } else {
            b.yawRate *= (1.0f - 0.8f * h);
        }
        b.yaw = WrapAngle(b.yaw + b.yawRate * h);

        // ---- moving, and what it runs into
        const float fx = Sin(b.yaw), fz = Cos(b.yaw);       // forwards
        const float lx = Cos(b.yaw), lz = -Sin(b.yaw);      // its left
        const float vx = fx * b.speed + lx * b.slide, vz = fz * b.speed + lz * b.slide;
        const float nx = b.x + vx * h, nz = b.z + vz * h;
        const float checkY = b.grounded ? b.y : b.y - 4.0f;
        if (Fits(w, nx, nz, b.yaw, checkY)) {
            b.x = nx; b.z = nz;
        } else if (Fits(w, nx, b.z, b.yaw, checkY) && std::fabs(vx) > 1.0f) {   // slide along the wall
            out.impact = (std::max)(out.impact, std::fabs(vz));
            b.x = nx;
            const float along = vx * fx;   // what is left of the speed along the heading
            b.speed = along * 0.85f; b.slide *= 0.5f;
        } else if (Fits(w, b.x, nz, b.yaw, checkY) && std::fabs(vz) > 1.0f) {
            out.impact = (std::max)(out.impact, std::fabs(vx));
            b.z = nz;
            const float along = vz * fz;
            b.speed = along * 0.85f; b.slide *= 0.5f;
        } else {
            out.impact = (std::max)(out.impact, std::hypot(vx, vz));
            b.speed = std::fabs(b.speed) > 80.0f ? -b.speed * 0.25f : 0.0f;   // a bump back off the wall
            b.slide = 0; b.yawRate *= 0.3f;
        }

        // ---- the ground under it
        const Footing f = FootingAt(w, b.x, b.z, b.yaw);
        const float floorY = f.any ? f.y : b.y - 1000.0f;
        if (b.grounded) {
            if (floorY < b.y - kCartLeaveGround - std::fabs(b.speed) * h * 0.25f) {   // off a ledge or over a crest: into the air
                b.grounded = false;
                b.airTime = 0;
                b.vy = std::fabs(b.speed) * Sin(b.pitch) * 0.8f;   // keeps the climb it had
                out.tookOff = true;
            } else {
                b.y = floorY;
                b.pitch += (f.pitch - b.pitch) * (std::min)(1.0f, 14.0f * h);
                b.roll += (f.roll - b.roll) * (std::min)(1.0f, 14.0f * h);
            }
        }
        if (!b.grounded) {
            b.airTime += h;
            b.vy -= kCartGravity * h;
            b.y += b.vy * h;
            b.pitch += (std::clamp(std::atan2(b.vy, (std::max)(60.0f, std::fabs(b.speed))) * 0.5f, -0.5f, 0.5f) - b.pitch) * (std::min)(1.0f, 2.0f * h);
            b.roll *= (1.0f - 1.5f * h);
            if (b.y <= floorY) {   // touched down
                out.landing = (std::max)(out.landing, -b.vy);
                b.y = floorY;
                b.vy = 0;
                b.grounded = true;
                b.pitch = f.pitch; b.roll = f.roll;
            }
        }
    }
    return out;
}

// ---- a cart in a match (the server's record of it) ------------------------------------------------------------------------------------
struct VehicleState {
    uint8_t index = 0;
    CartBody body;
    CartControls controls;           // a bot driver's wishes for this tick (BotController sets them; Match::TickVehicles drives with them)
    float health = kCartHealth;
    bool wrecked = false;            // blown up: burning, nobody can get in, gone after kCartWreckSeconds
    float wreckedAt = 0;
    bool gone = false;
    uint32_t seat[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};   // Seat::Driver, Seat::Passenger: who sits there (0xFFFFFFFF for nobody)
    uint32_t lastHitBy = 0xFFFFFFFFu;                // credited with the wreck's blast
    uint32_t lastDriver = 0xFFFFFFFFu;               // credited with what a cart that rolls on after its driver jumped out runs over
    float reportAt = -1;             // when its human driver last reported (match clock)
    float busyAt = 0;                // when it last had anyone in it, moved or burned (a cart parked longer than a second is sent less often)
    float air = 0;                   // the human driver's report: height above the ground under it
    bool drift = false;
    struct RamCooldown { uint32_t victim; float until; };
    RamCooldown rams[8] = {};
    bool Occupied() const { return seat[0] != 0xFFFFFFFFu || seat[1] != 0xFFFFFFFFu; }
    uint32_t Driver() const { return seat[0]; }
    uint32_t Passenger() const { return seat[1]; }
    uint32_t Id() const { return kVehicleIdBase + index; }
    bool CanRam(uint32_t victim, float now) const {
        for (const auto& r : rams) if (r.victim == victim && now < r.until) return false;
        return true;
    }
    void NoteRam(uint32_t victim, float now) {
        int slot = 0;
        for (int i = 0; i < 8; i++) if (rams[i].until < rams[slot].until) slot = i;
        rams[slot] = {victim, now + kRamCooldown};
    }
};

// Where a rider in `seat` is: on the saddle (world position of its top) and where they step down to.
inline void SeatSpot(const CartBody& b, Seat seat, float* x, float* y, float* z) { CartToWorld(b, seat == Seat::Passenger ? cart::kSeatPassenger : cart::kSeatDriver, x, y, z); }
inline Vec2 ExitSpot(const CartBody& b, Seat seat) {
    float x, y, z;
    CartToWorld(b, seat == Seat::Passenger ? cart::kExitPassenger : cart::kExitDriver, &x, &y, &z);
    return {x, z};
}

// ---- damage ----------------------------------------------------------------------------------------------------------------------------
inline float CrashDamage(float impact) { return impact > kCartCrashFrom ? (impact - kCartCrashFrom) * kCartCrashDamage : 0.0f; }
inline float RiderCrashDamage(float impact) { return impact > kRiderCrashFrom ? (impact - kRiderCrashFrom) * kRiderCrashDamage : 0.0f; }
inline float LandingDamage(float landing) { return landing > kCartLandFrom ? (landing - kCartLandFrom) * kCartLandDamage : 0.0f; }
inline float RamDamage(float speed) {
    const float s = std::fabs(speed);
    return s < kRamMinSpeed ? 0.0f : kRamBase + (s - kRamMinSpeed) * kRamPerSpeed;
}

// ---- driving it from a plan (the bots, and anyone testing) --------------------------------------------------------------------------

// The controls that take a cart at `b` towards `goal`: steer at it, slow down for sharp turns and near the end, back up and turn when it
// is behind. `arrive` is how close counts as there (it stops there). `boldness` 0..1 is how hard it is driven.
inline CartControls DriveToward(const CartBody& b, Vec2 goal, float arrive, float boldness = 0.7f) {
    CartControls c;
    const float dx = goal.x - b.x, dz = goal.z - b.z;
    const float dist = std::hypot(dx, dz);
    if (dist < arrive) { c.brake = std::fabs(b.speed) > 20.0f; return c; }
    const float want = std::atan2(dx, dz);
    const float off = cartdetail::WrapAngle(want - b.yaw);   // + means the goal is to its left
    if (std::fabs(off) > 2.2f && dist < 600.0f) {           // right behind it and close: back up, turning round
        c.throttle = -0.8f;
        c.steer = off > 0 ? -1.0f : 1.0f;
        return c;
    }
    c.steer = std::clamp(off * 2.2f, -1.0f, 1.0f);
    // the speed it can carry: less through a sharp turn, and it eases off to stop at the goal
    const float turnLimit = kCartMaxSpeed * (1.0f - (std::min)(0.75f, std::fabs(off) * 0.55f));
    const float stopLimit = std::sqrt(2.0f * kCartBrake * 0.5f * (std::max)(0.0f, dist - arrive)) + 40.0f;
    const float limit = (std::min)(turnLimit, stopLimit) * (0.75f + 0.25f * boldness);
    if (b.speed > limit + 30.0f) c.brake = true;
    else c.throttle = b.speed < limit ? 1.0f : 0.2f;
    return c;
}

} // namespace royale
