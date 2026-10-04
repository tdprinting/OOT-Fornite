#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <utility>
#include <vector>

// Finding the notes and drums in a recording, splitting them into melody, harmony and bass, and hearing what each part sounds like.
// A port of the OOTSONG library in tools/song-to-oot.html (fft, BP.toNotes, transcribeDrums, drumSound, cleanNotes, assignParts,
// timbreOf, timbreDistance, songShape, fitShift, hearSong). Same steps in the same order, in doubles like the JS, so the results match it.
// No globals: safe to run on a background thread. Build without FMA contraction (/fp:precise, -ffp-contract=off) to match the JS bit for bit.
namespace royale {
namespace music {

enum class Part { Melody, Harmony, Bass, Drums };      // Melody, Harmony, Bass are also the indexes of the per-part arrays below
enum DrumKind { Kick = 0, Snare = 1, Hat = 2 };

// A note. amp < 0 means unknown (a MIDI note): cleanNotes then uses vel / 160. kind is the drum kind for drum notes, else -1.
struct Note {
    double start = 0, end = 0;     // seconds
    int pitch = 0;                 // MIDI key
    int vel = 0;                   // 1..127
    double amp = -1;               // how strong the model heard it, 0..1
    bool onset = false;            // found from a clear attack (not taken from left-over energy)
    double strike = 0, rise = 0;   // how clear that attack was, and how much louder it got
    Part part = Part::Harmony;
    int kind = -1;
};
struct DrumHit { double time; int kind; double vel, str, cent; };   // cent: how bright what just arrived is, in Hz
// What a part sounds like. A has-flag is false where the JS has null (no notes long enough to measure).
struct Timbre {
    double brightness = 0, decay = 0, attack = 0;      // 1 = pure tone, higher brighter; dB per second; near 1 hits, near 0 swells in
    bool hasBrightness = false, hasDecay = false, hasAttack = false;
    int count = 0;
};
struct DrumSound { double cent[3] = {0, 0, 0}; bool has[3] = {false, false, false}; };   // by DrumKind; has only with 3 or more hits
struct Heard {
    Timbre part[3];                                    // melody, harmony, bass
    double drumCent[3] = {0, 0, 0};
    bool hasDrum[3] = {false, false, false};
    double bassShare = 0;                              // how much of the sound is bass
};
struct PartShape { int count = 0; int median = 0; double dur = 0; };
struct Shape { PartShape part[3]; };                   // melody, harmony, bass

namespace detail {
constexpr double kPi = 3.141592653589793;              // Math.PI

// Math.round: halves go up, also below zero (-2.5 -> -2)
inline double JsRound(double x) {
    double r = std::ceil(x);
    if (r - 0.5 > x) r -= 1.0;
    return r;
}
// Math.hypot as V8 does it (scaled, Kahan-summed), so magnitudes match to the last bit
inline double JsHypot(double a, double b) {
    a = std::fabs(a); b = std::fabs(b);
    const double mx = a > b ? a : b;
    if (std::isinf(mx)) return mx;
    if (std::isnan(a) || std::isnan(b)) return a + b;
    if (mx == 0) return 0;
    double sum = 0, comp = 0;
    const double v[2] = {a, b};
    for (double x : v) {
        const double n = x / mx, summand = n * n - comp, pre = sum + summand;
        comp = (pre - sum) - summand;
        sum = pre;
    }
    return std::sqrt(sum) * mx;
}
// the JS sort order for notes: by start, then by key (stable)
inline bool ByStartPitch(const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); }
inline double Median(std::vector<double> a) { std::sort(a.begin(), a.end()); return a[a.size() >> 1]; }
} // namespace detail

// ---- FFT (in place, radix 2; n a power of two) ----------------------------------------------------------------------------------
inline void Fft(double* re, double* im, size_t n) {
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * detail::kPi / static_cast<double>(len), wr = std::cos(ang), wi = std::sin(ang);
        const size_t half = len / 2;
        for (size_t i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (size_t j = 0; j < half; j++) {
                const size_t a = i + j, b = a + half;
                const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

// ---- notes from Basic Pitch's output (a port of @spotify/basic-pitch 1.0.1 toMidi.ts: outputToNotesPoly, noteFramesToTime) ----------
// frames/onsets: per model frame (256 samples at 22050 Hz), 88 keys from A0. Notes come back sorted by start, then key, all harmony.
inline std::vector<Note> ToNotes(const std::vector<std::array<float, 88>>& frames, const std::vector<std::array<float, 88>>& onsets,
                                 double onsetThresh, double frameThresh, int minNoteLen, int energyTol) {
    constexpr int SR = 22050, HOP = 256, FPS = SR / HOP, WIN_S = 2, N_SAMPLES = SR * WIN_S - HOP, ANNOT_N_FRAMES = FPS * WIN_S, M = 88;
    constexpr int MAX_FREQ_IDX = 87;
    const double windowOffset = (static_cast<double>(HOP) / SR) * (ANNOT_N_FRAMES - static_cast<double>(N_SAMPLES) / HOP) + 0.0018;
    const auto frameToTime = [&](int f) { return static_cast<double>(f * HOP) / SR - windowOffset * (f / ANNOT_N_FRAMES); };
    const int n = static_cast<int>(std::min(frames.size(), onsets.size()));
    std::vector<Note> out;
    if (!n) return out;
    const auto fr = [&](int i, int j) { return static_cast<double>(frames[i][j]); };
    // onsets, plus where a key's energy jumps up (scaled to the onsets' loudest)
    std::vector<double> on(static_cast<size_t>(n) * M);
    double onMax = 0, dMax = 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < M; j++) { on[i * M + j] = onsets[i][j]; onMax = std::max(onMax, on[i * M + j]); }
    std::vector<float> diff(static_cast<size_t>(n) * M, 0.0f);
    for (int i = 2; i < n; i++) for (int j = 0; j < M; j++) {
        const double d = std::max(0.0, std::min(fr(i, j) - fr(i - 1, j), fr(i, j) - fr(i - 2, j)));
        diff[i * M + j] = static_cast<float>(d); dMax = std::max(dMax, d);
    }
    if (dMax > 0) for (int i = 0; i < n * M; i++) on[i] = std::max(on[i], onMax * static_cast<double>(diff[i]) / dMax);
    // note starts: peaks of the onsets above the threshold, latest first
    std::vector<std::pair<int, int>> starts;
    for (int j = 0; j < M; j++) for (int i = 0; i < n; i++) {
        const double v = on[i * M + j];
        if (v > onsetThresh && (i == 0 || v > on[(i - 1) * M + j]) && (i == n - 1 || v > on[(i + 1) * M + j])) starts.emplace_back(i, j);
    }
    std::sort(starts.begin(), starts.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first != b.first ? a.first > b.first : a.second > b.second; });
    struct Raw { int s, e, pitch; double amp; bool onset; double strike, rise; };
    std::vector<Raw> raw;
    std::vector<float> energy(static_cast<size_t>(n) * M);
    for (int i = 0; i < n; i++) for (int j = 0; j < M; j++) energy[i * M + j] = frames[i][j];
    const auto en = [&](int i, int j) -> float& { return energy[static_cast<size_t>(i) * M + j]; };
    const auto clear = [&](int i, int j) { en(i, j) = 0; if (j < MAX_FREQ_IDX) en(i, j + 1) = 0; if (j > 0) en(i, j - 1) = 0; };
    for (const auto& sj : starts) {
        const int s = sj.first, j = sj.second;
        if (s >= n - 1) continue;
        int i = s + 1, k = 0;
        while (i < n - 1 && k < energyTol) { k = en(i, j) < frameThresh ? k + 1 : 0; i++; }
        i -= k;
        if (i - s <= minNoteLen) continue;
        double amp = 0;
        for (int q = s; q < i; q++) { amp += fr(q, j); clear(q, j); }
        double before = 1;
        for (int q = std::max(0, s - 4); q < s; q++) before = std::min(before, fr(q, j));
        raw.push_back({s, i, j + 21, amp / (i - s), true, on[s * M + j], fr(std::min(n - 1, s + 2), j) - before});
    }
    // the melodia trick: notes with no clear onset, taken from the strongest energy left (rows' maxima kept so each step is quick)
    std::vector<float> rowMax(n);
    const auto rowUpdate = [&](int i) { float v = 0; for (int j = 0; j < M; j++) if (en(i, j) > v) v = en(i, j); rowMax[i] = v; };
    for (int i = 0; i < n; i++) rowUpdate(i);
    std::vector<int> touched;
    for (int guard = 0; guard < 20000; guard++) {
        int bi = 0;
        for (int i = 1; i < n; i++) if (rowMax[i] > rowMax[bi]) bi = i;
        if (rowMax[bi] <= frameThresh) break;
        int bj = 0;
        for (int j = 1; j < M; j++) if (en(bi, j) > en(bi, bj)) bj = j;
        en(bi, bj) = 0; touched.clear(); touched.push_back(bi);
        int i = bi + 1, k = 0;
        while (i < n - 1 && k < energyTol) { k = en(i, bj) < frameThresh ? k + 1 : 0; clear(i, bj); touched.push_back(i); i++; }
        const int e = i - 1 - k;
        i = bi - 1; k = 0;
        while (i > 0 && k < energyTol) { k = en(i, bj) < frameThresh ? k + 1 : 0; clear(i, bj); touched.push_back(i); i--; }
        const int s = i + 1 + k;
        for (int r : touched) rowUpdate(r);
        if (e - s <= minNoteLen) continue;
        double amp = 0;
        for (int q = s; q < e; q++) amp += fr(q, bj);
        raw.push_back({s, e, bj + 21, amp / (e - s), false, 0, 0});
    }
    out.reserve(raw.size());
    for (const Raw& x : raw) {
        Note nt;
        nt.start = frameToTime(x.s); nt.end = frameToTime(x.e); nt.pitch = x.pitch;
        nt.vel = static_cast<int>(detail::JsRound(std::min(120.0, std::max(40.0, 30 + 130 * x.amp))));
        nt.amp = x.amp; nt.onset = x.onset; nt.strike = x.strike; nt.rise = x.rise; nt.part = Part::Harmony;
        out.push_back(nt);
    }
    std::stable_sort(out.begin(), out.end(), detail::ByStartPitch);
    return out;
}

// ---- drums: broadband bursts in the low (kick), middle (snare) and top (hi-hat) of the spectrum ---------------------------------------
// y: mono samples at sr. Hits come back in time order (kick, snare, hat within one frame).
inline std::vector<DrumHit> TranscribeDrums(const float* y, size_t len, int sr) {
    using detail::JsHypot;
    constexpr int N = 1024, H = 256, nb = N / 2;
    static constexpr double DRUM_BANDS[3][2] = {{30, 1000}, {150, 8000}, {2000, 11000}};   // kick, snare, hat
    const double srd = sr;
    const long long fl = static_cast<long long>(std::floor((static_cast<double>(len) - N) / H)) + 1;
    const int frames = static_cast<int>(std::max(0LL, fl));
    std::vector<DrumHit> hits;
    if (frames < 3) return hits;
    std::vector<double> win(N);
    for (int i = 0; i < N; i++) win[i] = 0.5 - 0.5 * std::cos(2 * detail::kPi * i / N);
    const auto bin = [&](double hz) { return std::max(1, std::min(nb - 1, static_cast<int>(detail::JsRound(hz * N / srd)))); };
    const int bands[4][2] = {{bin(30), bin(150)}, {bin(150), bin(450)}, {bin(180), bin(3000)}, {bin(5000), bin(std::min(11000.0, srd / 2 - 200))}};   // L, S, M, H
    enum { L = 0, S = 1, Mi = 2, Hi = 3, B = 4 };
    std::vector<double> re(N), im(N), prev(nb, 0.0), cur(nb, 0.0), pm(nb, 0.0), mag(nb, 0.0);
    std::vector<double> flux[5];
    for (auto& f : flux) f.assign(frames, 0.0);
    std::vector<double> lowE(frames, 0.0), hiShare(frames, 0.0), centK(frames, 0.0), centS(frames, 0.0), centH(frames, 0.0), flatM(frames, 0.0), flatH(frames, 0.0);
    const auto flat = [&](int a, int z) {       // noise is flat, notes are peaky
        double lg = 0, ar = 0;
        for (int b = a; b <= z; b++) { lg += std::log(mag[b] + 1e-9); ar += mag[b]; }
        const int n = z - a + 1;
        return ar > 0 ? std::exp(lg / n) / (ar / n) : 0.0;
    };
    // what just arrived: how bright, in the range where each kind of drum lives
    const auto cent = [&](double lo, double hi) {
        double num = 0, den = 1e-12;
        for (int b = bin(lo); b <= bin(hi); b++) { const double d = std::max(0.0, mag[b] - pm[b]); num += d * b * srd / N; den += d; }
        return num / den;
    };
    for (int f = 0; f < frames; f++) {
        const size_t base = static_cast<size_t>(f) * H;
        for (int i = 0; i < N; i++) { re[i] = static_cast<double>(y[base + i]) * win[i]; im[i] = 0; }
        Fft(re.data(), im.data(), N);
        for (int b = 0; b < nb; b++) { mag[b] = JsHypot(re[b], im[b]); cur[b] = std::log1p(100 * mag[b]); }
        for (int b = bands[L][0]; b <= bands[L][1]; b++) lowE[f] += mag[b] * mag[b];
        { double h = 0, all = 1e-12; for (int b = 1; b < nb; b++) { const double e = mag[b] * mag[b]; all += e; if (b >= bands[Hi][0]) h += e; } hiShare[f] = h / all; }
        centK[f] = cent(DRUM_BANDS[0][0], DRUM_BANDS[0][1]); centS[f] = cent(DRUM_BANDS[1][0], DRUM_BANDS[1][1]);
        centH[f] = cent(DRUM_BANDS[2][0], std::min(DRUM_BANDS[2][1], srd / 2 - 200));
        for (int b = 0; b < nb; b++) pm[b] = mag[b];
        flatM[f] = flat(bin(1500), bin(5000)); flatH[f] = flat(bands[Hi][0], bands[Hi][1]);
        if (f > 0) {
            for (int k = 0; k < 4; k++) { double s = 0; const int a = bands[k][0], z = bands[k][1]; for (int b = a; b <= z; b++) s += std::max(0.0, cur[b] - prev[b]); flux[k][f] = s / (z - a + 1); }
            int up = 0; for (int b = bands[L][0]; b <= bands[Hi][1]; b++) if (cur[b] - prev[b] > 0.7) up++;
            flux[B][f] = static_cast<double>(up) / (bands[Hi][1] - bands[L][0] + 1);      // how much of the spectrum jumps at once: drums are broadband
        }
        std::swap(prev, cur);
    }
    const auto pct = [](const std::vector<double>& a, double q) { std::vector<double> s(a); std::sort(s.begin(), s.end()); return s[static_cast<size_t>(std::floor(q * static_cast<double>(s.size() - 1)))]; };
    // each band is measured against its own loudest moments, but never against near-silence (a band with nothing in it stays quiet)
    const double refS = std::max(0.3, pct(flux[S], 0.97)), refL = std::max(0.3, pct(flux[L], 0.97)), refM = std::max(0.3, pct(flux[Mi], 0.97)), refH = std::max(0.3, pct(flux[Hi], 0.97));
    int lastKick = -1000000000, lastSnare = -1000000000, lastHat = -1000000000;
    const int gap = static_cast<int>(detail::JsRound(0.07 * srd / H));
    const auto isPeak = [](const std::vector<double>& a, int f) { return a[f] >= a[f - 1] && a[f] > a[f + 1]; };
    const auto localMean = [&](const std::vector<double>& a, int f) { double s = 0; int n = 0; for (int k = std::max(0, f - 16); k <= std::min(frames - 1, f + 16); k++) { s += a[k]; n++; } return s / n; };
    for (int f = 1; f < frames - 1; f++) {
        const int f1 = std::min(frames - 1, f + 1);
        const double t = static_cast<double>(f * H + N / 2) / srd, Bf = flux[B][f];
        const double Lv = flux[L][f] / refL, Mv = flux[Mi][f] / refM, Hv = flux[Hi][f] / refH;
        const auto after = [&](int k) { return lowE[std::min(frames - 1, f + k)]; };
        const bool fades = after(9) < 0.65 * std::max(after(1), after(2));               // a kick dies away fast; a bass note holds
        const bool noisyM = std::max(flatM[f], flatM[f1]) > 0.6, noisyH = std::max(flatH[f], flatH[f1]) > 0.4;
        if (isPeak(flux[L], f) && Lv > 1.0 && fades && flux[L][f] > 1.8 * localMean(flux[L], f) && f - lastKick > gap) {
            hits.push_back({t, Kick, std::min(120.0, 60 + 40 * Lv), Lv, 0.5 * (centK[f] + centK[f1])}); lastKick = f;
        }
        if (isPeak(flux[Mi], f) && Mv > 1.25 && noisyM && hiShare[f1] > 0.06 && Bf > 0.12 && flux[S][f] / refS > 0.4 && (f - lastKick > 2 || Mv > 0.9) &&
            flux[Mi][f] > 1.8 * localMean(flux[Mi], f) && f - lastSnare > gap) {
            hits.push_back({t, Snare, std::min(120.0, 60 + 40 * Mv), Mv, 0.5 * (centS[f] + centS[f1])}); lastSnare = f;
        }
        if (isPeak(flux[Hi], f) && Hv > 0.35 && hiShare[f1] > 0.03 && noisyH && flux[Hi][f] > 1.6 * localMean(flux[Hi], f) && f - lastHat > gap) {
            hits.push_back({t, Hat, std::min(110.0, 50 + 35 * Hv), Hv, 0.5 * (centH[f] + centH[f1])}); lastHat = f;
        }
    }
    return hits;
}

// the drums of the recording: how bright its kick, snare and hi-hat are (the middle of each)
inline DrumSound DrumSoundOf(const std::vector<DrumHit>& hits) {
    DrumSound out;
    for (int kind = 0; kind < 3; kind++) {
        std::vector<double> c;
        for (const DrumHit& h : hits) if (h.kind == kind && h.cent > 0) c.push_back(h.cent);
        if (c.size() >= 3) { out.cent[kind] = detail::Median(c); out.has[kind] = true; }
    }
    return out;
}

// ---- bass is the low notes, melody the top voice, harmony what is left (for MIDI input) -----------------------------------------------
inline void AssignParts(std::vector<Note>& notes, int bassBelow = 50) {
    if (!bassBelow) bassBelow = 50;
    std::vector<size_t> live;
    for (size_t i = 0; i < notes.size(); i++) { notes[i].part = notes[i].pitch < bassBelow ? Part::Bass : Part::Harmony; if (notes[i].part != Part::Bass) live.push_back(i); }
    std::vector<double> ev;
    for (size_t i : live) { ev.push_back(notes[i].start); ev.push_back(notes[i].end); }
    std::sort(ev.begin(), ev.end());
    ev.erase(std::unique(ev.begin(), ev.end()), ev.end());
    std::vector<size_t> byStart(live);
    std::stable_sort(byStart.begin(), byStart.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
    std::vector<double> top(notes.size(), 0.0);
    for (size_t i = 0; i + 1 < ev.size(); i++) {
        const double mid = 0.5 * (ev[i] + ev[i + 1]);
        long long best = -1;
        for (size_t k : byStart) { if (notes[k].start > mid) break; if (notes[k].end > mid && (best < 0 || notes[k].pitch > notes[static_cast<size_t>(best)].pitch)) best = static_cast<long long>(k); }
        if (best >= 0) top[static_cast<size_t>(best)] = top[static_cast<size_t>(best)] + ev[i + 1] - ev[i];
    }
    for (size_t i : live) if (top[i] > 0.5 * (notes[i].end - notes[i].start)) notes[i].part = Part::Melody;
}

// ---- tidying what the note finder heard, and splitting it into melody, harmony and bass --------------------------------------------
// A recording gives held notes heard as several, and blips. These are mended or dropped,
// then the bass (the lowest line), the melody (the strongest, smoothest high line) and the harmony (the rest, at most 4 at once) are found.
// drums: the hits from TranscribeDrums (attacks to line notes up with; kicks heard as low notes go). bassBelow: 0 means 55.
inline std::vector<Note> CleanNotes(std::vector<Note> notes, const std::vector<DrumHit>& drums, int bassBelow = 55) {
    for (Note& n : notes) if (n.amp < 0) n.amp = (n.vel ? n.vel : 90) / 160.0;
    const auto dur = [](const Note& n) { return n.end - n.start; };
    const auto keepIf = [&](const std::vector<char>& keep) { std::vector<Note> o; for (size_t i = 0; i < notes.size(); i++) if (keep[i]) o.push_back(notes[i]); notes.swap(o); };
    // 1. pieces of one held note: the same key again straight after, without a real new attack (a weak onset and no jump in loudness)
    {
        std::map<int, std::vector<size_t>> byKey;
        for (size_t i = 0; i < notes.size(); i++) byKey[notes[i].pitch].push_back(i);
        std::vector<char> keep(notes.size(), 1);
        for (auto& kv : byKey) {
            std::vector<size_t>& list = kv.second;
            std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
            Note* cur = &notes[list[0]];
            for (size_t i = 1; i < list.size(); i++) {
                Note& n = notes[list[i]];
                const double gap = n.start - cur->end;
                if (gap < 0.15 && (!n.onset || (n.strike < 0.65 && n.rise < 0.2))) {
                    cur->amp = (cur->amp * dur(*cur) + n.amp * dur(n)) / std::max(1e-6, dur(*cur) + dur(n));
                    cur->end = std::max(cur->end, n.end); keep[list[i]] = 0;
                } else cur = &n;
            }
        }
        keepIf(keep);
        std::stable_sort(notes.begin(), notes.end(), detail::ByStartPitch);
    }
    // a note that faded in with no attack of its own most likely began with the nearest clear attack just before it
    {
        std::vector<double> anchors;
        for (const Note& n : notes) if (n.onset && n.strike >= 0.65) anchors.push_back(n.start);
        for (const DrumHit& h : drums) anchors.push_back(h.time);
        std::sort(anchors.begin(), anchors.end());
        for (Note& n : notes) {
            if (n.onset && n.strike >= 0.65) continue;
            bool found = false; double best = 0;
            for (double t : anchors) { if (t > n.start + 0.02) break; if (t >= n.start - 0.15) { best = t; found = true; } }
            if (found && best < n.start) n.start = best;
        }
    }
    // 2. blips
    {
        std::vector<double> amps;
        for (const Note& n : notes) amps.push_back(n.amp);
        const double medAmp = amps.empty() ? 0.5 : detail::Median(amps);
        std::vector<char> keep(notes.size());
        for (size_t i = 0; i < notes.size(); i++) keep[i] = dur(notes[i]) >= 0.06 && !(dur(notes[i]) < 0.1 && notes[i].amp < 0.8 * medAmp);
        keepIf(keep);
    }
    if (notes.empty()) return notes;
    for (Note& n : notes) n.part = Part::Harmony;
    // 3. bass: the lowest line under G3, one note at a time
    const int bassTop = bassBelow ? bassBelow : 55;
    // a kick drum is heard as a short, weak low note: those go
    {
        std::vector<double> kicks;
        for (const DrumHit& h : drums) if (h.kind == Kick) kicks.push_back(h.time);
        std::vector<char> keep(notes.size(), 1);
        for (size_t i = 0; i < notes.size(); i++) {
            const Note& n = notes[i];
            if (n.pitch < 50 && dur(n) < 0.25 && n.amp < 0.5) for (double t : kicks) if (std::fabs(t - n.start) < 0.04) { keep[i] = 0; break; }
        }
        keepIf(keep);
    }
    {
        std::vector<size_t> low;
        for (size_t i = 0; i < notes.size(); i++) if (notes[i].pitch < bassTop) low.push_back(i);
        std::vector<char> lost(notes.size(), 0);
        for (size_t i : low) {
            Note& n = notes[i];
            const double mid = n.start + std::min(0.1, dur(n) / 2);
            bool beaten = false;
            for (size_t k : low) {
                const Note& m = notes[k];
                if (k != i && m.start <= mid && m.end > mid && (m.amp > 1.15 * n.amp || (m.pitch < n.pitch && m.amp > 0.85 * n.amp))) { beaten = true; break; }
            }
            if (!beaten) n.part = Part::Bass; else if (n.pitch < 48) lost[i] = 1;     // a weak extra note that low is an echo of the bass
        }
        // a bass note's overtones (12, 19 or 24 keys up, starting and stopping with it, quieter) are not notes of their own
        std::vector<size_t> bassNotes;
        for (size_t i : low) if (notes[i].part == Part::Bass) bassNotes.push_back(i);
        for (size_t i = 0; i < notes.size(); i++) {
            const Note& n = notes[i];
            if (n.part == Part::Bass) continue;
            for (size_t k : bassNotes) {
                const Note& m = notes[k];
                const int d = n.pitch - m.pitch;
                if ((d == 12 || d == 19 || d == 24) && std::fabs(n.start - m.start) < 0.04 && std::fabs(n.end - m.end) < 0.08 && n.amp < 0.75 * m.amp) { lost[i] = 1; break; }
            }
        }
        for (char& c : lost) c = !c;
        keepIf(lost);
    }
    // 4. melody: the best path through the notes above the bass, liking loud, high, long notes and small steps between them
    {
        std::vector<size_t> up;
        for (size_t i = 0; i < notes.size(); i++) if (notes[i].part != Part::Bass) up.push_back(i);
        std::stable_sort(up.begin(), up.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
        if (!up.empty()) {
            std::vector<int> pitches;
            for (size_t i : up) pitches.push_back(notes[i].pitch);
            std::sort(pitches.begin(), pitches.end());
            const int hiRef = pitches[static_cast<size_t>(std::floor(static_cast<double>(pitches.size()) * 0.85))];
            const auto gain = [&](const Note& n) { return dur(n) * (0.4 + 1.2 * n.amp + 0.5 * std::max(-4.0, std::min(1.0, (n.pitch - hiRef) / 7.0))); };
            const int nu = static_cast<int>(up.size());
            std::vector<double> best(nu, 0.0);
            std::vector<int> from(nu, -1);
            for (int i = 0; i < nu; i++) {
                const Note& n = notes[up[i]];
                best[i] = gain(n);
                for (int j = i - 1; j >= 0 && notes[up[j]].start > n.start - 4; j--) {
                    const Note& m = notes[up[j]];
                    if (m.start >= n.start - 0.04 || m.end > n.start + std::min(0.25, dur(m) * 0.5)) continue;     // must come before, not mostly alongside
                    const double jump = std::abs(n.pitch - m.pitch), gap = std::max(0.0, n.start - m.end);
                    const double v = best[j] + gain(n) - 0.012 * std::max(0.0, jump - 2) * std::min(1.0, dur(n) + 0.2) - 0.05 * std::max(0.0, gap - 0.5);
                    if (v > best[i]) { best[i] = v; from[i] = j; }
                }
            }
            int end = 0;
            for (int i = 1; i < nu; i++) if (best[i] > best[end]) end = i;
            for (int i = end; i >= 0; i = from[i]) notes[up[i]].part = Part::Melody;
        }
    }
    // 5. one note at a time for melody and bass; at most 4 harmony notes at once (the quietest go)
    for (Part part : {Part::Melody, Part::Bass}) {
        std::vector<size_t> line;
        for (size_t i = 0; i < notes.size(); i++) if (notes[i].part == part) line.push_back(i);
        std::stable_sort(line.begin(), line.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
        for (size_t i = 0; i + 1 < line.size(); i++) {
            Note& a = notes[line[i]];
            const Note& b = notes[line[i + 1]];
            if (a.end > b.start) a.end = std::max(a.start + 0.05, b.start);
        }
    }
    {
        std::vector<size_t> harm, placed;
        for (size_t i = 0; i < notes.size(); i++) if (notes[i].part == Part::Harmony) harm.push_back(i);
        std::stable_sort(harm.begin(), harm.end(), [&](size_t a, size_t b) { return notes[a].amp > notes[b].amp; });
        std::vector<char> keep(notes.size(), 1);
        for (size_t i : harm) {
            const double mid = (notes[i].start + notes[i].end) / 2;
            int over = 0;
            for (size_t k : placed) if (notes[k].start <= mid && notes[k].end > mid) over++;
            if (over >= 4) keep[i] = 0; else placed.push_back(i);
        }
        keepIf(keep);
    }
    return notes;
}

// ---- what a part sounds like in a recording: how bright, how fast it starts, and whether it holds or dies away ----------------------
// Measured on the note's own harmonics (the first 4 for low notes, where other parts' harmonics crowd in), so other instruments
// playing at the same time mostly stay out of it. The median over the longest, loudest notes (maxNotes of them; 0 means 30).
inline Timbre TimbreOf(const float* y, size_t len, int sr, const std::vector<Note>& notes, int maxNotes = 30) {
    using detail::JsHypot;
    constexpr int N = 4096;
    const double srd = sr;
    std::vector<double> win(N), re(N), im(N);
    for (int i = 0; i < N; i++) win[i] = 0.5 - 0.5 * std::cos(2 * detail::kPi * i / N);
    // the spectrum around time t (samples outside the recording count as silence)
    const auto spectrumAt = [&](double t) {
        const long long a = static_cast<long long>(detail::JsRound(t * srd)) - N / 2;
        for (int i = 0; i < N; i++) {
            const long long k = a + i;
            re[i] = (k >= 0 && k < static_cast<long long>(len) ? static_cast<double>(y[k]) : 0.0) * win[i]; im[i] = 0;
        }
        Fft(re.data(), im.data(), N);
    };
    const auto spec = [&](double b) {
        const long long k = static_cast<long long>(detail::JsRound(b));
        return k > 0 && k < N / 2 ? std::max(std::max(JsHypot(re[k], im[k]), JsHypot(re[k - 1], im[k - 1])), JsHypot(re[k + 1], im[k + 1])) : 0.0;
    };
    const auto harmonics = [&](double f0) {
        std::vector<double> a;
        const int top = f0 < 200 ? 4 : 8;
        for (int h = 1; h <= top; h++) a.push_back(f0 * h < srd / 2 - 100 ? spec(f0 * h * N / srd) : 0.0);
        return a;
    };
    const auto ampOr1 = [](const Note& n) { return n.amp > 0 ? n.amp : 1.0; };      // (amp || 1): unknown or 0 counts as 1
    std::vector<const Note*> picks;
    for (const Note& n : notes) if (n.end - n.start > 0.12) picks.push_back(&n);
    std::stable_sort(picks.begin(), picks.end(), [&](const Note* a, const Note* b) { return (b->end - b->start) * ampOr1(*b) < (a->end - a->start) * ampOr1(*a); });
    const size_t keepN = static_cast<size_t>(maxNotes > 0 ? maxNotes : 30);
    if (picks.size() > keepN) picks.resize(keepN);
    std::vector<double> B, D, A;
    for (const Note* np : picks) {
        const Note& n = *np;
        const double f0 = 440 * std::pow(2.0, (n.pitch - 69) / 12.0), ln = n.end - n.start;
        spectrumAt(n.start + 0.06);
        const std::vector<double> early = harmonics(f0);
        spectrumAt(n.start + std::min(ln - 0.03, 0.45));
        const std::vector<double> late = harmonics(f0);
        double e0 = 0, e1 = 0, tot = 0, wsum = 0;
        for (double v : early) e0 = e0 + v * v;
        for (double v : late) e1 = e1 + v * v;
        for (double v : early) tot = tot + v;
        if (e0 <= 0 || tot <= 0) continue;
        for (size_t h = 0; h < early.size(); h++) wsum = wsum + early[h] * static_cast<double>(h + 1);
        B.push_back(wsum / tot);                                                                     // 1 = pure tone, higher = brighter
        const double dt = std::min(ln - 0.03, 0.45) - 0.06;
        if (dt > 0.08) D.push_back(10 * std::log10(std::max(1e-9, e1) / e0) / dt);                    // dB per second: 0 holds, below dies away
        spectrumAt(n.start + 0.015);
        double s0 = 0;
        for (double v : harmonics(f0)) s0 = s0 + v * v;
        A.push_back(std::max(0.0, std::min(1.0, s0 / e0)));                                            // near 1 = hits at once, near 0 = swells in
    }
    Timbre t;
    if (!B.empty()) { t.brightness = detail::Median(B); t.hasBrightness = true; }
    if (!D.empty()) { t.decay = detail::Median(D); t.hasDecay = true; }
    if (!A.empty()) { t.attack = detail::Median(A); t.hasAttack = true; }
    t.count = static_cast<int>(B.size());
    return t;
}
inline double TimbreDistance(const Timbre& a, const Timbre& b) {
    if (!a.hasBrightness || !b.hasBrightness) return 0;
    double d = std::fabs(std::log(a.brightness / b.brightness)) * 2.5;
    if (a.hasDecay && b.hasDecay) d += std::fabs(std::max(-40.0, a.decay) - std::max(-40.0, b.decay)) / 15;
    if (a.hasAttack && b.hasAttack) d += std::fabs(a.attack - b.attack) * 1.5;
    return d;
}

// ---- the song's parts: how many notes, their middle key and their usual length ------------------------------------------------------
inline Shape SongShape(const std::vector<Note>& notes) {
    static constexpr int defaultMedian[3] = {72, 64, 43};
    Shape shape;
    for (int p = 0; p < 3; p++) {
        std::vector<int> ps;
        double sum = 0;
        for (const Note& n : notes) if (static_cast<int>(n.part) == p) { ps.push_back(n.pitch); sum = sum + n.end - n.start; }
        std::sort(ps.begin(), ps.end());
        PartShape& s = shape.part[p];
        s.count = static_cast<int>(ps.size());
        s.median = ps.empty() ? defaultMedian[p] : ps[ps.size() >> 1];
        s.dur = ps.empty() ? 0.3 : sum / static_cast<double>(ps.size());
    }
    return shape;
}
// octaves (in keys) to move a part by so its notes sit in the instrument's good range around center (bass by one at most, so it stays a bass)
inline int FitShift(int center, Part part, int median) {
    int sh = 0;
    while (median + sh < center - 15 && sh < 24) sh += 12;
    while (median + sh > center + 15 && sh > -24) sh -= 12;
    return part == Part::Bass ? std::max(-12, std::min(12, sh)) : sh;
}

// ---- what the recording's parts sound like (for picking instruments), its drums, and how much of it is bass -------------------------
inline Heard HearSong(const float* y, size_t len, int sr, const std::vector<Note>& notes, const std::vector<DrumHit>& hits) {
    Heard heard;
    for (int p = 0; p < 3; p++) {
        std::vector<Note> ns;
        for (const Note& n : notes) if (static_cast<int>(n.part) == p) ns.push_back(n);
        heard.part[p] = TimbreOf(y, len, sr, ns, 24);
    }
    const DrumSound ds = DrumSoundOf(hits);
    for (int k = 0; k < 3; k++) { heard.drumCent[k] = ds.cent[k]; heard.hasDrum[k] = ds.has[k]; }
    constexpr int N = 2048;
    const double srd = sr;
    double lo = 0, all = 1e-12;
    std::vector<double> re(N), im(N);
    const int b0 = static_cast<int>(detail::JsRound(40.0 * N / srd)), b1 = static_cast<int>(detail::JsRound(4000.0 * N / srd));
    for (size_t a = 0; a + N <= len; a += N * 4) {
        for (int i = 0; i < N; i++) { re[i] = static_cast<double>(y[a + i]) * (0.5 - 0.5 * std::cos(2 * detail::kPi * i / N)); im[i] = 0; }
        Fft(re.data(), im.data(), N);
        for (int b = b0; b < b1; b++) { const double e = re[b] * re[b] + im[b] * im[b]; all += e; if (b * srd / N < 180) lo += e; }
    }
    heard.bassShare = lo / all;
    return heard;
}

} // namespace music
} // namespace royale
