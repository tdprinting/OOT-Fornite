#pragma once
// Bokoblin animation sampling and skinning; same packed pose format as the companion models.
#include "bokoblin_model.h"
#include <algorithm>
#include <cmath>

namespace royale {
namespace bokoblin {

struct Xform {
    float q[4] = {0, 0, 0, 1};   // x, y, z, w
    float t[3] = {0, 0, 0};
};
struct Pose {
    Xform bone[kBoneCount];
};

inline Xform FrameXform(int frame, int bone) {
    const int16_t* v = &kPoses[(frame * kBoneCount + bone) * 7];
    Xform x;
    for (int i = 0; i < 4; i++) x.q[i] = v[i] / 32767.0f;
    for (int i = 0; i < 3; i++) x.t[i] = v[4 + i] / kPosFrac;
    return x;
}

// Between two transforms: the rotations by normalised linear blend (taking the short way round), the offsets straight.
inline Xform Mix(const Xform& a, const Xform& b, float k) {
    Xform o;
    const float dot = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float len = 0.0f;
    for (int i = 0; i < 4; i++) { o.q[i] = a.q[i] + (b.q[i] * sign - a.q[i]) * k; len += o.q[i] * o.q[i]; }
    len = len > 1e-12f ? 1.0f / std::sqrt(len) : 0.0f;
    for (int i = 0; i < 4; i++) o.q[i] *= len;
    if (len == 0.0f) o.q[3] = 1.0f;
    for (int i = 0; i < 3; i++) o.t[i] = a.t[i] + (b.t[i] - a.t[i]) * k;
    return o;
}

inline const ClipInfo& InfoOf(int clip) { return kClips[(clip % kClipCount + kClipCount) % kClipCount]; }

// How long a clip runs (a loop's last frame leads back to its first).
inline float ClipSeconds(int clip) {
    const ClipInfo& c = InfoOf(clip);
    return static_cast<float>(c.loops ? c.frames : c.frames - 1) / c.fps;
}

// The pose `seconds` into a clip: loops wrap round, one-shots hold their last frame.
inline void SampleClip(int clip, float seconds, Pose& out) {
    const ClipInfo& c = InfoOf(clip);
    const int n = c.frames;
    float f = std::isfinite(seconds) ? seconds * c.fps : 0.0f;
    int a, b;
    if (c.loops) {
        f = std::fmod(f, static_cast<float>(n));
        if (f < 0.0f) f += static_cast<float>(n);
        a = std::min(static_cast<int>(f), n - 1);
        b = (a + 1) % n;
    } else {
        f = std::clamp(f, 0.0f, static_cast<float>(n - 1));
        a = std::min(static_cast<int>(f), n - 1);
        b = std::min(a + 1, n - 1);
    }
    const float k = std::clamp(f - static_cast<float>(a), 0.0f, 1.0f);
    for (int i = 0; i < kBoneCount; i++) out.bone[i] = Mix(FrameXform(c.firstFrame + a, i), FrameXform(c.firstFrame + b, i), k);
}

inline void BlendPoses(const Pose& a, const Pose& b, float k, Pose& out) {
    for (int i = 0; i < kBoneCount; i++) out.bone[i] = Mix(a.bone[i], b.bone[i], k);
}

inline void Rotate(const float q[4], const float v[3], float out[3]) {
    // v + 2w(q x v) + 2 q x (q x v)
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]), tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// Where a vertex ends up in a pose, and which way its normal (unit length) then points.
inline void SkinVertex(const Pose& p, const Vert& v, float pos[3], float nrm[3]) {
    const float rest[3] = {v.x, v.y, v.z}, n[3] = {v.nx / 127.0f, v.ny / 127.0f, v.nz / 127.0f};
    const Xform& a = p.bone[v.b0 % kBoneCount];
    float pa[3], na[3];
    Rotate(a.q, rest, pa);
    Rotate(a.q, n, na);
    for (int i = 0; i < 3; i++) { pos[i] = pa[i] + a.t[i]; nrm[i] = na[i]; }
    if (v.b1 != v.b0 && v.w0 < 255) {
        const Xform& b = p.bone[v.b1 % kBoneCount];
        const float w = v.w0 / 255.0f;
        float pb[3], nb[3];
        Rotate(b.q, rest, pb);
        Rotate(b.q, n, nb);
        for (int i = 0; i < 3; i++) {
            pos[i] = pos[i] * w + (pb[i] + b.t[i]) * (1.0f - w);
            nrm[i] = nrm[i] * w + nb[i] * (1.0f - w);
        }
    }
    const float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
    if (len > 1e-6f) for (int i = 0; i < 3; i++) nrm[i] /= len;
}

// Plays one clip at a time and cross-fades into the next.
struct Animator {
    int clip = kIdle;
    float time = 0.0f;
    int from = -1;            // the clip being faded out, or -1
    float fromTime = 0.0f, fade = 0.0f, fadeLen = 0.0f;

    // Switch to a clip (nothing happens if it is already playing, unless `restart`).
    void Play(int c, float fadeSeconds = 0.25f, bool restart = false) {
        if (c == clip && !restart) return;
        if (fadeSeconds > 0.0f) { from = clip; fromTime = time; fade = 0.0f; fadeLen = fadeSeconds; }
        else from = -1;
        clip = c;
        time = 0.0f;
    }
    // Moves time on; `rate` speeds the current clip up or slows it down (a faster walk, a lazier one).
    void Update(float dt, float rate = 1.0f) {
        if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
        time += dt * (std::isfinite(rate) ? rate : 1.0f);
        if (!InfoOf(clip).loops) time = std::min(time, ClipSeconds(clip));
        if (from >= 0) {
            fromTime += dt;
            fade += dt;
            if (fade >= fadeLen) from = -1;
        }
    }
    // A one-shot clip (a jump, a stretch, a pounce) that has played to its end.
    bool Done() const { return !InfoOf(clip).loops && time >= ClipSeconds(clip) - 1e-4f; }
    void Evaluate(Pose& out) const {
        SampleClip(clip, time, out);
        if (from >= 0 && fadeLen > 0.0f) {
            Pose old;
            SampleClip(from, fromTime, old);
            float k = std::clamp(fade / fadeLen, 0.0f, 1.0f);
            k = k * k * (3.0f - 2.0f * k);
            BlendPoses(old, out, k, out);
        }
    }
};

// The box around the whole posed model (for tests and for placing its name).
inline void PoseBounds(const Pose& p, float mn[3], float mx[3]) {
    for (int i = 0; i < 3; i++) { mn[i] = 1e30f; mx[i] = -1e30f; }
    for (int i = 0; i < kVertCount; i++) {
        float pos[3], nrm[3];
        SkinVertex(p, kVerts[i], pos, nrm);
        for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], pos[k]); mx[k] = std::max(mx[k], pos[k]); }
    }
}


} // namespace bokoblin
} // namespace royale
