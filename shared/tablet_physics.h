#pragma once
// A dropped tablet as a small rigid box (Maya's video-chat scene): gravity, bouncing corners against a floor, friction and spin. Plain C++ with the
// floor passed in as a function, so the tests can drop it on a flat floor and on a slope.
#include <algorithm>
#include <cmath>

namespace royale {

struct TabletBody {
    float p[3] = {0, 0, 0};          // centre, world units
    float v[3] = {0, 0, 0};
    float q[4] = {0, 0, 0, 1};       // orientation, x y z w
    float w[3] = {0, 0, 0};          // angular velocity, radians per second
    float half[3] = {5.1f, 3.5f, 0.45f};   // half sizes along the tablet's own axes (width, height, thickness)
    float restTime = 0;              // how long it has lain still
    bool asleep = false;

    static void Rot(const float q[4], const float v[3], float o[3]) {
        const float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
        o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    }
    static void Conj(const float q[4], float o[4]) { o[0] = -q[0]; o[1] = -q[1]; o[2] = -q[2]; o[3] = q[3]; }

    // One step (`floorAt(x, z)` gives the floor's height, `normalUp` is unused: the floor is treated as level where she stands).
    template <class FloorFn>
    void Step(float dt, FloorFn floorAt) {
        if (asleep || !(dt > 0)) return;
        constexpr float kGravity = 380.0f, kRestitution = 0.32f, kFriction = 0.55f;
        const float mass = 1.0f;
        // inverse inertia of a box about its own axes
        const float ix = mass / 3.0f * (half[1] * half[1] + half[2] * half[2]), iy = mass / 3.0f * (half[0] * half[0] + half[2] * half[2]),
                    iz = mass / 3.0f * (half[0] * half[0] + half[1] * half[1]);
        const float invI[3] = {1 / ix, 1 / iy, 1 / iz};
        v[1] -= kGravity * dt;
        for (int i = 0; i < 3; i++) p[i] += v[i] * dt;
        // integrate the orientation: q += 0.5 * (w, 0) * q * dt
        const float wq[4] = {w[0], w[1], w[2], 0};
        const float dq[4] = {0.5f * (wq[3] * q[0] + wq[0] * q[3] + wq[1] * q[2] - wq[2] * q[1]), 0.5f * (wq[3] * q[1] - wq[0] * q[2] + wq[1] * q[3] + wq[2] * q[0]),
                             0.5f * (wq[3] * q[2] + wq[0] * q[1] - wq[1] * q[0] + wq[2] * q[3]), 0.5f * (wq[3] * q[3] - wq[0] * q[0] - wq[1] * q[1] - wq[2] * q[2])};
        float len = 0;
        for (int i = 0; i < 4; i++) { q[i] += dq[i] * dt; len += q[i] * q[i]; }
        len = 1.0f / std::sqrt(len);
        for (int i = 0; i < 4; i++) q[i] *= len;
        float qi[4];
        Conj(q, qi);
        bool touching = false;
        for (int c = 0; c < 8; c++) {
            const float local[3] = {(c & 1 ? 1 : -1) * half[0], (c & 2 ? 1 : -1) * half[1], (c & 4 ? 1 : -1) * half[2]};
            float r[3];
            Rot(q, local, r);
            const float floorY = floorAt(p[0] + r[0], p[2] + r[2]);
            const float depth = floorY - (p[1] + r[1]);
            if (depth <= 0) continue;
            touching = true;
            p[1] += depth * 0.8f;                                    // lift out of the floor (most of the way: the rest resolves next step)
            const float u[3] = {v[0] + w[1] * r[2] - w[2] * r[1], v[1] + w[2] * r[0] - w[0] * r[2], v[2] + w[0] * r[1] - w[1] * r[0]};
            if (u[1] >= 0) continue;
            // impulse along the floor's normal (up), with the box's inertia in the corner's own frame
            const float rxn[3] = {r[1] * 0 - r[2] * 1, r[2] * 0 - r[0] * 0, r[0] * 1 - r[1] * 0};   // r x (0,1,0)
            float lr[3];
            Rot(qi, rxn, lr);
            const float il[3] = {lr[0] * invI[0], lr[1] * invI[1], lr[2] * invI[2]};
            float wi[3];
            Rot(q, il, wi);
            const float term[3] = {wi[1] * r[2] - wi[2] * r[1], wi[2] * r[0] - wi[0] * r[2], wi[0] * r[1] - wi[1] * r[0]};
            const float j = -(1 + kRestitution) * u[1] / (1.0f / mass + term[1]);
            v[1] += j / mass;
            for (int i = 0; i < 3; i++) w[i] += wi[i] * j;
            // friction: slow the sliding at the corner
            v[0] -= v[0] * kFriction * std::min(1.0f, dt * 10.0f);
            v[2] -= v[2] * kFriction * std::min(1.0f, dt * 10.0f);
        }
        const float damp = touching ? std::max(0.0f, 1.0f - 12.0f * dt) : std::max(0.0f, 1.0f - 0.2f * dt);
        for (int i = 0; i < 3; i++) w[i] *= damp;
        const float speed2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2], spin2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
        if (touching && speed2 < 80.0f && spin2 < 4.0f) { restTime += dt; if (restTime > 0.5f) { asleep = true; v[0] = v[1] = v[2] = w[0] = w[1] = w[2] = 0; } }
        else restTime = 0;
        if (!std::isfinite(p[0] + p[1] + p[2] + q[0] + q[1] + q[2] + q[3])) { asleep = true; }
    }

    float LowestCorner() const {
        float low = 1e9f;
        for (int c = 0; c < 8; c++) {
            const float local[3] = {(c & 1 ? 1 : -1) * half[0], (c & 2 ? 1 : -1) * half[1], (c & 4 ? 1 : -1) * half[2]};
            float r[3];
            Rot(q, local, r);
            low = std::min(low, p[1] + r[1]);
        }
        return low;
    }
};

} // namespace royale
