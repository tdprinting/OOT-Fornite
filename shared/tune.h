#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace royale {

// A short original polka-style tune to play with the chicken dance emote, made from scratch in code (nothing recorded, and not the melody of
// any existing song): a bouncy two-bar loop at 120 beats a minute with an oom-pah bass, a squeaky triangle-wave melody in four one-second
// phrases that line up with the four bars of the dance (beak, wings, tail, clap), and ticks and claps for percussion.
constexpr int kTuneRate = 22050;
constexpr float kTuneSeconds = 4.0f; // one full cycle of the dance; loop it

namespace tune_detail {

inline float NoteHz(int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }

// A tiny deterministic noise source for the percussion.
struct Noise {
    uint32_t s = 12345;
    float Next() { s = s * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f; }
};

inline float Triangle(float phase) { // phase 0..1
    const float p = phase - std::floor(phase);
    return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
}
inline float Pulse(float phase, float duty) {
    const float p = phase - std::floor(phase);
    return p < duty ? 1.0f : -1.0f;
}

} // namespace tune_detail

// One cycle (kTuneSeconds long) of the tune as mono signed 16-bit samples at kTuneRate.
inline std::vector<int16_t> BuildChickenTune() {
    using namespace tune_detail;
    const int total = static_cast<int>(kTuneSeconds * kTuneRate);
    std::vector<float> mix(static_cast<size_t>(total), 0.0f);
    Noise noise;
    const float eighth = 0.25f; // seconds: 120 BPM
    // Four phrases of four eighth notes each (MIDI note numbers; 0 is a rest).
    static const int melody[16] = {
        67, 67, 67, 67,   // beak: a staccato "bok bok bok bok"
        76, 72, 76, 72,   // wings: flapping between two notes
        69, 67, 69, 67,   // tail: a wobbling shake
        72, 0, 72, 79,    // clap: two stabs and a high squeak
    };
    for (int n = 0; n < 16; n++) {
        const int note = melody[n];
        if (note == 0) continue;
        const float start = n * eighth, hz = NoteHz(note), len = n < 4 ? 0.09f : 0.2f; // the "bok"s are short
        for (int i = 0; i < static_cast<int>(len * kTuneRate); i++) {
            const size_t at = static_cast<size_t>(start * kTuneRate) + static_cast<size_t>(i);
            if (at >= mix.size()) break;
            const float t = static_cast<float>(i) / kTuneRate;
            const float env = (1.0f - t / len) * (t < 0.004f ? t / 0.004f : 1.0f);
            const float vibrato = 1.0f + 0.012f * std::sin(t * 40.0f); // a little chicken wobble
            mix[at] += 0.33f * env * Triangle(hz * vibrato * t);
        }
    }
    // Oom-pah: a low note on the beats 1 and 3, a short chord on 2 and 4.
    for (int beat = 0; beat < 8; beat++) {
        const float start = beat * 0.5f;
        const bool oom = beat % 2 == 0;
        const int notes[3] = { oom ? 48 : 64, oom ? 0 : 67, oom ? 0 : 72 }; // C3, or E4 + G4 + C5
        for (int k = 0; k < 3; k++) {
            if (notes[k] == 0) continue;
            const float hz = NoteHz(notes[k]), len = oom ? 0.22f : 0.12f, gain = oom ? 0.22f : 0.07f;
            for (int i = 0; i < static_cast<int>(len * kTuneRate); i++) {
                const size_t at = static_cast<size_t>(start * kTuneRate) + static_cast<size_t>(i);
                if (at >= mix.size()) break;
                const float t = static_cast<float>(i) / kTuneRate;
                mix[at] += gain * (1.0f - t / len) * Pulse(hz * t, 0.25f);
            }
        }
    }
    // Percussion: a tick on every beat, and a clap (a longer burst of noise) on each beat of the last bar.
    for (int beat = 0; beat < 8; beat++) {
        const bool clap = beat >= 6;
        const float len = clap ? 0.07f : 0.025f, gain = clap ? 0.30f : 0.10f;
        for (int i = 0; i < static_cast<int>(len * kTuneRate); i++) {
            const size_t at = static_cast<size_t>(beat * 0.5f * kTuneRate) + static_cast<size_t>(i);
            if (at >= mix.size()) break;
            mix[at] += gain * (1.0f - static_cast<float>(i) / (len * kTuneRate)) * noise.Next();
        }
    }
    float peak = 0.01f;
    for (float v : mix) peak = (std::max)(peak, std::fabs(v));
    std::vector<int16_t> out(mix.size());
    for (size_t i = 0; i < mix.size(); i++) out[i] = static_cast<int16_t>(mix[i] / peak * 0.85f * 32767.0f);
    return out;
}

} // namespace royale
