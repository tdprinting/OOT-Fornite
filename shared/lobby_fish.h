#pragma once
// Cosmetic, local-only aquarium animation. No networking, inventory or match rules.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace royale::reef {
constexpr int kFishCount = 6;
constexpr float kPi = 3.14159265358979323846f;
struct Point { float x = 0, y = 0, z = 0; };
struct Fish {
    Point pos, velocity;
    float yaw = 0, pitch = 0, swim = 0;
    uint8_t species = 0; // clownfish, cleaner wrasse
    float size = 1;
    bool cleaning = false;
};
inline float Length(Point p) { return std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z); }
inline Point Sub(Point a, Point b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Point Bound(Point p) {
    return {std::clamp(p.x, -110.0f, 110.0f), std::clamp(p.y, 78.0f, 135.0f), std::clamp(p.z, -40.0f, 40.0f)};
}
inline float Turn(float from, float to, float step) {
    const float delta = std::atan2(std::sin(to-from), std::cos(to-from));
    return from + std::clamp(delta, -step, step);
}
struct Aquarium {
    std::array<Fish, kFishCount> fish{};
    float time = 0;
    Aquarium() { Reset(); }
    void Reset() {
        time = 0;
        for (int i = 0; i < kFishCount; ++i) {
            fish[i] = Fish{};
            fish[i].species = i >= 3 ? 1 : 0;
            fish[i].size = i == 1 ? 0.72f : i == 2 ? 0.88f : i == 5 ? 0.8f : 1.0f;
            fish[i].pos = {float(i*42-105), float(90+(i%3)*20), float((i%2)*50-25)};
            fish[i].yaw = i * 0.9f;
            fish[i].swim = i * 1.7f;
        }
    }
    void Step(float dt, Point viewer) {
        if (!std::isfinite(dt) || dt <= 0) return;
        dt = std::min(dt, 0.1f);
        time += dt;
        const auto previous = fish;
        const bool visitor = std::isfinite(viewer.x) && std::isfinite(viewer.y) && std::isfinite(viewer.z) && Length(viewer) < 620;
        for (int i = 0; i < kFishCount; ++i) {
            Fish& f = fish[i];
            const float phase = time * (f.species ? 0.48f : 0.24f) + i*1.31f;
            Point goal = {105*std::sin(phase), 111+32*std::sin(phase*0.73f+i), 42*std::cos(phase*0.87f+i)};
            f.cleaning = false;
            if (f.species == 0) {
                if (i == 0 && visitor) { // bold: come to the front glass and inspect Link
                    goal = {std::clamp(viewer.x*0.32f, -94.0f, 94.0f), 133+8*std::sin(time), -48};
                } else if (i == 1) { // shy youngster: sheltered by the anemone, peeking out between rests
                    const float peek = 0.5f+0.5f*std::sin(time*0.38f);
                    goal = {-87+34*peek, 73+21*peek, visitor ? 35.0f : 15.0f};
                } else { // playful: short weaving laps rather than moving in lockstep
                    goal.y += 7*std::sin(time*1.2f);
                }
            } else {
                // Each wrasse has its own cleaning interval and host, followed by a faster patrol.
                const float cycle = std::fmod(time+i*4.3f, 19.0f);
                if (cycle > 11 && cycle < 16) {
                    const Fish& host = previous[(i-3)%3];
                    goal = {host.pos.x+24*std::cos(time*0.8f+i), host.pos.y+5, host.pos.z+16*std::sin(time*0.8f+i)};
                    f.cleaning = Length(Sub(goal, f.pos)) < 35;
                }
            }
            goal = Bound(goal);
            Point delta = Sub(goal, f.pos);
            // Soft separation, including a smaller personal space during a cleaning visit.
            for (int j = 0; j < kFishCount; ++j) if (i != j) {
                const Point away = Sub(f.pos, previous[j].pos);
                const float d = Length(away), space = f.cleaning ? 16.0f : 28.0f;
                if (d > 0.01f && d < space) {
                    const float force = (space-d)*0.8f/d;
                    delta.x += away.x*force; delta.y += away.y*force; delta.z += away.z*force;
                }
            }
            const float distance = Length(delta);
            const float cruise = f.species ? (f.cleaning ? 14.0f : 42.0f) : i == 1 ? 15.0f : 25.0f;
            const float speed = std::min(cruise, distance*0.8f);
            const float horizontal = std::sqrt(delta.x*delta.x+delta.z*delta.z);
            if (horizontal > 0.1f) f.yaw = Turn(f.yaw, std::atan2(delta.x, delta.z), dt*(f.species ? 2.2f : 1.5f));
            const float blend = 1-std::exp(-3.0f*dt);
            f.pitch += (std::clamp(std::atan2(delta.y, horizontal+0.01f), -0.35f, 0.35f)-f.pitch)*blend;
            const Point wanted = {std::sin(f.yaw)*speed, distance > 0.01f ? delta.y/distance*speed : 0, std::cos(f.yaw)*speed};
            f.velocity.x += (wanted.x-f.velocity.x)*blend;
            f.velocity.y += (wanted.y-f.velocity.y)*blend;
            f.velocity.z += (wanted.z-f.velocity.z)*blend;
            f.pos = Bound({f.pos.x+f.velocity.x*dt, f.pos.y+f.velocity.y*dt, f.pos.z+f.velocity.z*dt});
            f.swim += dt*(3.0f+Length(f.velocity)*0.09f);
        }
    }
};
// Shared by game rendering and the Blender swim preview. Model nose is +Z, Y is up.
inline Point PoseVertex(Point p, uint8_t part, float phase, bool cleaning) {
    const float bend = std::clamp((-p.z+8.0f)/44.0f, 0.0f, 1.0f);
    p.x += std::sin(phase+p.z*0.045f)*bend*bend*(cleaning ? 2.5f : 5.0f);
    if (part == 1) { // tail fan, hinged at the narrow peduncle
        const float angle = std::sin(phase-0.8f)*0.36f;
        const float z = p.z+27;
        const float x = p.x;
        p.x = std::cos(angle)*x+std::sin(angle)*z;
        p.z = -std::sin(angle)*x+std::cos(angle)*z-27;
    } else if (part == 2 || part == 3) { // pectoral fins paddle even while hovering
        p.y += std::sin(phase*1.7f+(part == 2 ? 0.0f : 1.2f))*std::fabs(p.x)*0.22f;
    } else if (part == 4) {
        p.x += std::sin(phase*1.3f)*std::max(0.0f, p.y-9)*0.16f;
    }
    return p;
}
} // namespace royale::reef
