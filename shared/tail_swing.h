#pragma once
// Cloth-style physics for a pet's hair: Maya's ponytail and Avriella's tuft. A TailTracker watches where the pet is drawn, feeds the same
// HatSpring chain Link's cap uses (shared/cloth.h) with how she moves and turns, and SwingBone then bends a bone of her already-posed skeleton
// about its root. Plain C++, no game types, so the tests can check it.
#include "cloth.h"
#include <cmath>

namespace royale {

struct TailTracker {
    HatSpring spring;
    bool have = false;
    double lastTime = 0;
    float px = 0, pz = 0, pyaw = 0, lvx = 0, lvz = 0;

    // Call as she is drawn. `yaw` is her facing in radians (the model faces +Z turned by yaw about Y), `wind01` the weather's wind 0..1.
    // Does nothing when called twice in the same instant (she can be drawn more than once a frame).
    void Update(double now, float x, float z, float yaw, float wind01) {
        const float dt = static_cast<float>(now - lastTime);
        if (!have || !(dt < 0.25f) || !std::isfinite(x + z + yaw)) { have = true; lastTime = now; px = x; pz = z; pyaw = yaw; lvx = lvz = 0; spring = HatSpring{}; return; }
        if (dt < 0.004f) return;
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float vx = std::clamp((x - px) / dt, -500.0f, 500.0f), vz = std::clamp((z - pz) / dt, -500.0f, 500.0f);
        const float lx = vx * c - vz * s, lz = vx * s + vz * c;                 // her own sideways and forward speed
        const float accSide = (lx - lvx) / dt, accFore = (lz - lvz) / dt;
        float dy = yaw - pyaw;
        while (dy > 3.14159265f) dy -= 6.2831853f;
        while (dy < -3.14159265f) dy += 6.2831853f;
        const float turn = std::clamp(dy / dt, -14.0f, 14.0f);                   // positive = turning to her left
        spring.Step((std::min)(dt, 0.05f), -lx, -lz, 0.0f, wind01, static_cast<float>(std::fmod(now, 1000.0)), 0.9f,
                    std::clamp(accFore * 0.0035f, -2.5f, 2.5f), std::clamp(accSide * 0.0035f, -2.5f, 2.5f), turn);
        lastTime = now; px = x; pz = z; pyaw = yaw; lvx = lx; lvz = lz;
    }
};

// Turns a posed bone about `head` (the bone's rest root; the pose maps a rest vertex v to q*v + t) by `foreAng` about X and `sideAng` about Z,
// in the model's own space. Works for any Xform with q[4] (x, y, z, w) and t[3].
template <class X>
inline void PosedPoint(const X& b, const float rest[3], float out[3]) {
    const float* q = b.q;
    const float tx = 2 * (q[1] * rest[2] - q[2] * rest[1]), ty = 2 * (q[2] * rest[0] - q[0] * rest[2]), tz = 2 * (q[0] * rest[1] - q[1] * rest[0]);
    out[0] = rest[0] + q[3] * tx + (q[1] * tz - q[2] * ty) + b.t[0];
    out[1] = rest[1] + q[3] * ty + (q[2] * tx - q[0] * tz) + b.t[1];
    out[2] = rest[2] + q[3] * tz + (q[0] * ty - q[1] * tx) + b.t[2];
}
// The same, about a point given where it already is in the model's space.
template <class X>
inline void SwingBoneAt(X& b, const float pivot[3], float foreAng, float sideAng) {
    auto rot = [](const float q[4], const float v[3], float o[3]) {
        const float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
        o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    };
    const float hx = std::sin(foreAng * 0.5f), hxw = std::cos(foreAng * 0.5f), hz = std::sin(sideAng * 0.5f), hzw = std::cos(sideAng * 0.5f);
    const float qx[4] = {hx, 0, 0, hxw}, qz[4] = {0, 0, hz, hzw};
    // Q = qz * qx (turn about X first, then about Z)
    const float Q[4] = {qz[3] * qx[0] + qz[0] * qx[3] + qz[1] * qx[2] - qz[2] * qx[1], qz[3] * qx[1] - qz[0] * qx[2] + qz[1] * qx[3] + qz[2] * qx[0],
                        qz[3] * qx[2] + qz[0] * qx[1] - qz[1] * qx[0] + qz[2] * qx[3], qz[3] * qx[3] - qz[0] * qx[0] - qz[1] * qx[1] - qz[2] * qx[2]};
    const float rel[3] = {b.t[0] - pivot[0], b.t[1] - pivot[1], b.t[2] - pivot[2]};
    float rr[3];
    rot(Q, rel, rr);
    const float n[4] = {Q[3] * b.q[0] + Q[0] * b.q[3] + Q[1] * b.q[2] - Q[2] * b.q[1], Q[3] * b.q[1] - Q[0] * b.q[2] + Q[1] * b.q[3] + Q[2] * b.q[0],
                        Q[3] * b.q[2] + Q[0] * b.q[1] - Q[1] * b.q[0] + Q[2] * b.q[3], Q[3] * b.q[3] - Q[0] * b.q[0] - Q[1] * b.q[1] - Q[2] * b.q[2]};
    for (int i = 0; i < 4; i++) b.q[i] = n[i];
    for (int i = 0; i < 3; i++) b.t[i] = rr[i] + pivot[i];
}

template <class X>
inline void SwingBone(X& b, const float head[3], float foreAng, float sideAng) {
    float pivot[3];
    PosedPoint(b, head, pivot);
    SwingBoneAt(b, pivot, foreAng, sideAng);
}

} // namespace royale
