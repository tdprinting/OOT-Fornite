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


// The storm jingle: a short ominous motif in D minor (three rising notes, then a heavy fall) over a low drone and a roll of thunder. Played when the
// storm changes phase. About 2.6 seconds, mono, kTuneRate.
constexpr float kStormJingleSeconds = 2.6f;
inline std::vector<int16_t> BuildStormJingle() {
    using namespace tune_detail;
    const int total = static_cast<int>(kStormJingleSeconds * kTuneRate);
    std::vector<float> mix(static_cast<size_t>(total), 0.0f);
    Noise noise;
    struct Note { float start, len; int midi; };
    static const Note notes[] = { {0.00f, 0.22f, 57}, {0.24f, 0.22f, 62}, {0.48f, 0.22f, 65}, {0.74f, 0.55f, 69}, {1.34f, 0.30f, 65}, {1.66f, 0.80f, 50} };
    for (const Note& n : notes) {
        const float hz = NoteHz(n.midi);
        for (int i = 0; i < static_cast<int>(n.len * kTuneRate); i++) {
            const size_t at = static_cast<size_t>(n.start * kTuneRate) + static_cast<size_t>(i);
            if (at >= mix.size()) break;
            const float t = static_cast<float>(i) / kTuneRate;
            const float env = (t < 0.01f ? t / 0.01f : 1.0f) * std::pow(1.0f - t / n.len, 0.7f);
            // two slightly detuned pulse waves make a brassy, uneasy tone
            mix[at] += 0.20f * env * (Pulse(hz * t, 0.3f) + Pulse(hz * 1.006f * t, 0.4f));
        }
    }
    for (int i = 0; i < total; i++) { // the drone underneath and a rumble of thunder at the start
        const float t = static_cast<float>(i) / kTuneRate;
        mix[static_cast<size_t>(i)] += 0.16f * std::sin(6.2831853f * NoteHz(38) * t) * (1.0f - t / kStormJingleSeconds);
        if (t < 1.1f) mix[static_cast<size_t>(i)] += 0.22f * noise.Next() * std::exp(-t * 3.2f) * (0.6f + 0.4f * std::sin(t * 30.0f));
    }
    std::vector<int16_t> out(mix.size());
    for (size_t i = 0; i < mix.size(); i++) out[i] = static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, mix[i])) * 30000.0f);
    return out;
}

// The warning: an urgent two-tone siren, three rounds. About 1.5 seconds.
constexpr float kStormWarningSeconds = 1.5f;
inline std::vector<int16_t> BuildStormWarning() {
    using namespace tune_detail;
    const int total = static_cast<int>(kStormWarningSeconds * kTuneRate);
    std::vector<int16_t> out(static_cast<size_t>(total), 0);
    for (int i = 0; i < total; i++) {
        const float t = static_cast<float>(i) / kTuneRate;
        const int step = static_cast<int>(t / 0.125f);            // eighth-second beeps, alternating high and low
        const float local = std::fmod(t, 0.125f);
        const float hz = (step % 2 == 0) ? 880.0f : 660.0f;
        const float env = std::min(1.0f, local / 0.005f) * std::min(1.0f, (0.125f - local) / 0.02f);
        const float fade = std::min(1.0f, (kStormWarningSeconds - t) / 0.15f);
        out[static_cast<size_t>(i)] = static_cast<int16_t>(0.45f * env * fade * Triangle(hz * t) * 30000.0f);
    }
    return out;
}

} // namespace royale
