#pragma once
// Cosmetic, local-only aquarium animation. No networking, inventory or match rules.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace royale::reef {
constexpr int kFishCount = 6;
constexpr int kCrabCount = 3;
constexpr float kTankScale = 0.5f; // half the old tank's world dimensions
constexpr float kTankHalfX = 174.0f, kTankHalfZ = 100.0f, kTankHeight = 191.0f;
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
inline float FishRadius(const Fish& f) { return 52.0f*f.size+2.0f; }
inline Point CrabBound(Point p) { return {std::clamp(p.x,-130.0f,130.0f),32.0f,std::clamp(p.z,-56.0f,56.0f)}; }
inline float Turn(float from, float to, float step) {
    const float delta = std::atan2(std::sin(to-from), std::cos(to-from));
    return from + std::clamp(delta, -step, step);
}
struct Aquarium {
    std::array<Fish, kFishCount> fish{};
    std::array<Fish, kCrabCount> crabs{};
    float time = 0;
    Aquarium() { Reset(); }
    void Reset() {
        time = 0;
        for (int i = 0; i < kFishCount; ++i) {
            fish[i] = Fish{};
            fish[i].species = i >= 3 ? 1 : 0;
            fish[i].size = 0.55f*(i == 1 ? 0.72f : i == 2 ? 0.88f : i == 5 ? 0.8f : 1.0f);
            fish[i].pos = {float(i*42-105), float(90+(i%3)*20), float((i%2)*50-25)};
            fish[i].yaw = i * 0.9f;
            fish[i].swim = i * 1.7f;
        }
        for (int i=0;i<kCrabCount;++i) {
            crabs[i]=Fish{}; crabs[i].species=2; crabs[i].size=i==1 ? 0.75f : 0.9f;
            crabs[i].pos={float(i*85-85),32,float(i%2 ? 32 : -35)};
            crabs[i].yaw=i*1.8f;
        }
    }
    void SeparateFish() {
        // Circumscribed animated-mesh spheres, not just soft steering. Cleaning never reduces clearance.
        for (int pass=0;pass<64;++pass) {
            bool moved=false;
            for (int i=0;i<kFishCount;++i) for (int j=i+1;j<kFishCount;++j) {
                Point d=Sub(fish[j].pos,fish[i].pos);
                float len=Length(d), wanted=FishRadius(fish[i])+FishRadius(fish[j]);
                if (len>=wanted-0.001f) continue;
                if (len<0.001f) { d={std::cos(i*2.3f+j),0.5f*float((i+j)%3-1),std::sin(i*2.3f+j)}; len=Length(d); }
                Point n={d.x/len,d.y/len,d.z/len};
                // A pair pinned to opposite bounds cannot separate further on that axis. Slide sideways
                // instead of repeatedly asking the clamp for a vertical displacement it cannot supply.
                auto pinned=[](float a,float b,float direction,float lo,float hi) {
                    return (direction>0 && a<=lo+0.001f && b>=hi-0.001f) ||
                           (direction<0 && a>=hi-0.001f && b<=lo+0.001f);
                };
                if (pinned(fish[i].pos.x,fish[j].pos.x,n.x,-110,110)) n.x=0;
                if (pinned(fish[i].pos.y,fish[j].pos.y,n.y,78,135)) n.y=0;
                if (pinned(fish[i].pos.z,fish[j].pos.z,n.z,-40,40)) n.z=0;
                const float normalLength=Length(n);
                if (normalLength<0.001f) n={1,0,0};
                else n={n.x/normalLength,n.y/normalLength,n.z/normalLength};
                const Point actual=Sub(fish[j].pos,fish[i].pos);
                const float along=actual.x*n.x+actual.y*n.y+actual.z*n.z;
                const float fixedSquared=std::max(0.0f,Length(actual)*Length(actual)-along*along);
                const float push=(std::sqrt(std::max(0.0f,wanted*wanted-fixedSquared))-along+0.002f)*0.5f;
                fish[i].pos=Bound({fish[i].pos.x-n.x*push,fish[i].pos.y-n.y*push,fish[i].pos.z-n.z*push});
                fish[j].pos=Bound({fish[j].pos.x+n.x*push,fish[j].pos.y+n.y*push,fish[j].pos.z+n.z*push});
                const Point relative=Sub(fish[j].velocity,fish[i].velocity);
                const float closing=relative.x*n.x+relative.y*n.y+relative.z*n.z;
                if (closing<0) {
                    fish[i].velocity.x+=n.x*closing*0.5f; fish[i].velocity.y+=n.y*closing*0.5f; fish[i].velocity.z+=n.z*closing*0.5f;
                    fish[j].velocity.x-=n.x*closing*0.5f; fish[j].velocity.y-=n.y*closing*0.5f; fish[j].velocity.z-=n.z*closing*0.5f;
                }
                moved=true;
            }
            if (!moved) break;
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
            // Steer away before contact; the post-step constraint below enforces full body/fin clearance.
            for (int j = 0; j < kFishCount; ++j) if (i != j) {
                const Point away = Sub(f.pos, previous[j].pos);
                const float d = Length(away), space = FishRadius(f)+FishRadius(previous[j])+18.0f;
                if (d > 0.01f && d < space) {
                    const float force = (space-d)*2.0f/d;
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
        SeparateFish();
        for (int i=0;i<kCrabCount;++i) {
            Fish& c=crabs[i];
            const float cycle=std::fmod(time+i*6.0f,23.0f);
            c.cleaning=cycle>15; // stop to sift sand/peck, periodically tuck claws into the shell
            const Point goal=CrabBound({105*std::sin(time*0.09f+i*2.1f),32,40*std::cos(time*0.13f+i*1.7f)});
            const Point delta=Sub(goal,c.pos); const float distance=Length(delta);
            if (!c.cleaning && distance>0.1f) {
                c.yaw=Turn(c.yaw,std::atan2(delta.x,delta.z),dt*1.8f);
                const float speed=std::min(8.0f,distance);
                c.velocity={std::sin(c.yaw)*speed,0,std::cos(c.yaw)*speed};
                c.pos=CrabBound({c.pos.x+c.velocity.x*dt,32,c.pos.z+c.velocity.z*dt});
                c.swim+=dt*speed*0.55f;
            } else { c.velocity={}; c.swim+=dt*1.5f; }
        }
        for (int pass=0;pass<12;++pass) for (int i=0;i<kCrabCount;++i) for (int j=i+1;j<kCrabCount;++j) {
            Point d=Sub(crabs[j].pos,crabs[i].pos);float len=Length(d), radius=32*(crabs[i].size+crabs[j].size);
            if (len>=radius) continue;
            if (len<0.01f) { d={1,0,0};len=1; }
            const float push=(radius-Length(Sub(crabs[j].pos,crabs[i].pos))+0.002f)*0.5f;
            crabs[i].pos=CrabBound({crabs[i].pos.x-d.x/len*push,32,crabs[i].pos.z-d.z/len*push});
            crabs[j].pos=CrabBound({crabs[j].pos.x+d.x/len*push,32,crabs[j].pos.z+d.z/len*push});
        }
    }
};
// Shared by game rendering and the Blender swim preview. Model nose is +Z, Y is up.
inline Point PoseVertex(Point p, uint8_t part, float phase, bool cleaning) {
    if (part>=5) { // hermit legs and claws: alternate steps, or gentle sand-sifting while resting
        const float side=part==5 ? 1.0f : -1.0f;
        if (part==5 || part==6) { p.y+=std::max(0.0f,std::sin(phase+side*1.1f+p.z*0.18f))*2.5f; p.z+=std::sin(phase+side*1.1f)*2.0f; }
        else if (part==7) { p.y+=cleaning ? std::sin(phase)*2.0f : 0.5f; p.z-=cleaning ? 2.0f+std::sin(phase)*1.2f : 0.0f; }
        return p;
    }
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
