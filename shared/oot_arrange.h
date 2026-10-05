// Arranging a transcribed song for the game's own instruments: which instrument plays the melody, the harmony and the bass, which
// drum kit plays the drums, how far to move each part by octaves, and how loud each part should be. The result is an OoT sequence
// (oot_seq.h) the game plays with its own sound engine.
// A port of the matching in tools/song-to-oot.html (analyze, renderNote, instrumentTimbre, partCost, pickInstruments, kitMap and the
// levels in render). The soundfonts come in as a Bank: the mod fills it from the game's loaded soundfonts, the tests from made-up ones.
#pragma once
#include "oot_music_notes.h"
#include "oot_seq.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace royale::music {

constexpr double kOutputRate = 32000;   // the game's mixer rate: a sample with tuning 1.0 plays at 32 kHz on middle C
constexpr int kMidiOffset = 21;         // the game's note 0 is A0 (MIDI 21)
constexpr double kEnvTick = 1.0 / 240;  // one envelope step: a quarter of a 60 Hz frame

struct Sample { std::vector<int16_t> pcm; int loopStart = -1, loopEnd = -1; bool Loops() const { return loopStart >= 0; } };
// zone: keys lo..hi play sample at tuning (drums: fixedKey is the key that plays at the plain tuning), with this envelope
struct Zone {
    int lo = 0, hi = 127, sample = 0, fixedKey = -1;
    double tuning = 1, attack = 0, decay = 0, sustain = 1, release = 0.25, peak = 1, pan = 0;
    double centroid = 0, band[3] = {0, 0, 0}, length = 0;   // drums: how bright (overall, and in the kick/snare/hat bands) and how long
};
struct Preset {
    int font = 0, program = 0;          // program: the instrument number in the soundfont (drum kits: unused)
    bool drums = false;
    std::vector<Zone> zones;
    // from Analyze
    int center = 60;
    double harmonicity = 0, ring = 0, attack = 0;
    bool pitched = false, sustained = false;
    std::map<int, Timbre> timbres;      // measured once per key
    std::map<int, double> energies;     // per (key, length) for the levels
};
struct Bank { std::vector<Sample> samples; std::vector<Preset> presets; };

// ---- the game's envelopes and release times, as the page reads them -----------------------------------------------------------------
inline double ReleaseSeconds(int index) {
    if (index == 0) index = 0xF0;                                   // 0: the channel's default, which is 0xF0
    double frames;
    if (index >= 251) { static const double t[5] = {0.75, 0.66, 0.5, 0.33, 0.25}; frames = t[index - 251]; }
    else if (index >= 128) frames = 251 - index;
    else if (index >= 16) frames = 4 * (143 - index);
    else frames = 60 * (23 - index);
    return frames / 60;
}
// (steps, level) pairs: 0 steps ends the note, below 0 holds, jumps back or restarts. The game squares each level before using it.
inline void SetEnvelope(Zone& z, const int16_t* pairs, int maxPairs) {
    z.attack = 0; z.decay = 0; z.sustain = 1; z.peak = 1;
    if (!pairs) return;
    double t = 0;
    bool silentEnd = false;
    std::vector<std::pair<double, double>> pts;
    for (int k = 0; k < maxPairs && k < 32; k++) {
        const int delay = pairs[2 * k], level = pairs[2 * k + 1];
        if (delay > 0) { t += delay * kEnvTick; pts.push_back({t, std::pow(std::max(0, level) / 32767.0, 2)}); continue; }
        silentEnd = delay == 0;
        break;
    }
    if (pts.empty()) return;
    size_t pi = 0;
    for (size_t i = 1; i < pts.size(); i++) if (pts[i].second > pts[pi].second) pi = i;
    const double peak = pts[pi].second;
    z.attack = pts[pi].first;
    z.decay = pts.back().first - pts[pi].first;
    z.sustain = silentEnd ? 0 : peak > 0 ? pts.back().second / peak : 0;
    z.peak = peak;
}

// ---- the game's sample formats -----------------------------------------------------------------------------------------------------
// VADPCM: frames of 9 bytes (16 samples of 4 bits) or, for the small kind, 5 bytes (16 samples of 2 bits). book: npred * order * 8.
inline std::vector<int16_t> DecodeVadpcm(const uint8_t* raw, size_t size, const int16_t* book, int order, int npred, bool small) {
    const int frameSize = small ? 5 : 9;
    const size_t nFrames = size / frameSize;
    std::vector<int16_t> out(nFrames * 16);
    std::vector<int> prev(order, 0);
    int x[16];
    for (size_t f = 0; f < nFrames; f++) {
        const uint8_t* p = raw + f * frameSize;
        const int scale = 1 << (p[0] >> 4), pred = std::min(p[0] & 15, npred - 1);
        for (int i = 0; i < 16; i++) {
            int v;
            if (!small) { v = p[1 + (i >> 1)]; v = (i & 1) ? v & 15 : v >> 4; v = v >= 8 ? v - 16 : v; }
            else { v = (p[1 + (i >> 2)] >> (6 - 2 * (i & 3))) & 3; v = v >= 2 ? v - 4 : v; }
            x[i] = v * scale;
        }
        const int16_t* bk = book + pred * order * 8;
        for (int h = 0; h < 2; h++) {
            int o8[8];
            for (int i = 0; i < 8; i++) {
                long long acc = 0;
                for (int j = 0; j < order; j++) acc += static_cast<long long>(bk[j * 8 + i]) * prev[j];
                for (int k = 0; k < i; k++) acc += static_cast<long long>(bk[(order - 1) * 8 + (i - k - 1)]) * x[h * 8 + k];
                long long q = acc >= 0 ? acc / 2048 : -((-acc + 2047) / 2048);   // Math.floor(acc / 2048)
                int v = static_cast<int>(std::clamp<long long>(q + x[h * 8 + i], -32768, 32767));
                o8[i] = v;
                out[f * 16 + h * 8 + i] = static_cast<int16_t>(v);
            }
            for (int j = 0; j < order; j++) prev[j] = o8[8 - order + j];
        }
    }
    return out;
}

// ---- what each instrument is like -------------------------------------------------------------------------------------------------------
namespace arrange_detail {
inline double Periodicity(const std::vector<int16_t>& pcm, size_t from, int n, int minLag, int maxLag) {
    double e0 = 0;
    for (int i = 0; i < n; i++) e0 += double(pcm[from + i]) * pcm[from + i];
    if (e0 <= 0) return 0;
    double best = 0;
    for (int lag = minLag; lag <= maxLag; lag++) {
        double c = 0, e1 = 0;
        for (int i = 0; i < n; i++) {
            const size_t k = from + i + lag;
            const double b = k < pcm.size() ? pcm[k] : 0;
            c += pcm[from + i] * b; e1 += b * b;
        }
        if (e1 > 0) best = std::max(best, c / std::sqrt(e0 * e1));
    }
    return best;
}
inline double Centroid(const std::vector<int16_t>& pcm, size_t from, size_t n, double rate, double lo = 0, double hi = 1e18) {
    size_t size = 1; while (size < n) size <<= 1;
    std::vector<double> re(size, 0), im(size, 0);
    for (size_t i = 0; i < n; i++) re[i] = pcm[from + i] * (0.5 - 0.5 * std::cos(2 * detail::kPi * double(i) / double(n)));
    Fft(re.data(), im.data(), size);
    double num = 0, den = 0;
    for (size_t b = 1; b < size / 2; b++) {
        const double hz = double(b) * rate / double(size);
        if (hz < lo || hz > hi) continue;
        const double m = std::hypot(re[b], im[b]);
        num += m * hz; den += m;
    }
    return den > 0 ? num / den : 0;
}
}  // namespace arrange_detail

constexpr double kDrumBands[3][2] = {{30, 1000}, {150, 8000}, {2000, 11000}};   // kick, snare, hat (as the drum finder measures them)

inline void Analyze(const Bank& bank, Preset& p) {
    using namespace arrange_detail;
    if (p.zones.empty()) return;
    if (p.drums) {
        for (Zone& z : p.zones) {
            const Sample& s = bank.samples[z.sample];
            const double rate = kOutputRate * z.tuning;
            const size_t n = std::min<size_t>(s.pcm.size(), 2048);
            z.centroid = Centroid(s.pcm, 0, n, rate);
            for (int k = 0; k < 3; k++) z.band[k] = Centroid(s.pcm, 0, n, rate, kDrumBands[k][0], kDrumBands[k][1]);
            z.length = s.Loops() ? 9 : double(s.pcm.size()) / rate;
        }
        return;
    }
    // its natural range: around each sample's own pitch (where it sounds most like itself), within the keys it is used for
    double w = 0, c = 0;
    for (const Zone& z : p.zones) {
        const double nat = 60 - 12 * std::log2(z.tuning > 0 ? z.tuning : 1);
        const double width = std::max(1.0, std::min<double>(z.hi, nat + 18) - std::max<double>(z.lo, nat - 18));
        c += std::min(std::max(nat, double(z.lo)), double(z.hi)) * width; w += width;
    }
    p.center = int(std::lround(c / w));
    const Zone* main = &p.zones[0];
    for (const Zone& z : p.zones) if (z.lo <= p.center && p.center <= z.hi) { main = &z; break; }
    const Sample& s = bank.samples[main->sample];
    const double rate = kOutputRate * main->tuning;
    const int len = int(s.pcm.size());
    const int n = std::min(1024, len >> 1);
    const int from = s.Loops() && s.loopEnd - s.loopStart > n + 64 ? s.loopStart : std::min(len >> 3, len - 2 * n);
    const int minLag = std::max(2, int(std::floor(rate / 2000))), maxLag = std::min(n, int(std::ceil(rate / 40)));
    p.harmonicity = n > 64 && from >= 0 ? Periodicity(s.pcm, size_t(from), n, minLag, maxLag) : 0;
    p.pitched = p.harmonicity > 0.5;
    p.sustained = s.Loops() && main->sustain > 0.25;
    p.ring = s.Loops() ? (main->sustain > 0.25 ? 9 : main->attack + main->decay) : len / rate;
    p.attack = main->attack;
}

// One note of an instrument, the way the page plays it (left and right). Empty when no zone covers the key.
inline std::vector<float> RenderNote(const Bank& bank, const Preset& p, int key, int vel, double dur, int sr, std::vector<float>* right = nullptr) {
    std::vector<float> L, R;
    for (const Zone& z : p.zones) {
        if (key < z.lo || key > z.hi) continue;
        const Sample& s = bank.samples[z.sample];
        const std::vector<int16_t>& pcm = s.pcm;
        const size_t n0 = pcm.size();
        if (n0 < 2) continue;
        const int semis = z.fixedKey < 0 ? key - 60 : 0;
        const double ratio = z.tuning * std::pow(2.0, semis / 12.0) * kOutputRate / sr;
        const double release = std::max(z.release, 0.03), total = dur + release;
        const size_t n = size_t(std::floor(total * sr)) + 1;
        const double attack = std::max(z.attack, 0.001), decay = std::max(z.decay, 0.001);
        const double amp = z.peak * std::pow(vel / 127.0, 1.2) / 32768;
        const double pan = std::clamp(z.pan, -1.0, 1.0) * 0.5 + 0.5, gl = std::cos(pan * detail::kPi / 2), gr = std::sin(pan * detail::kPi / 2);
        if (L.size() < n) { L.resize(n, 0); R.resize(n, 0); }
        double pos = 0, relFrom = -1;
        for (size_t i = 0; i < n; i++) {
            if (s.Loops() && pos >= s.loopEnd) pos = s.loopStart + std::fmod(pos - s.loopStart, double(s.loopEnd - s.loopStart));
            if (!s.Loops() && pos >= double(n0 - 1)) break;
            const size_t ip = size_t(pos);
            const double fr = pos - double(ip), a = pcm[std::min(ip, n0 - 1)], b = pcm[std::min(ip + 1, n0 - 1)];
            const double t = double(i) / sr;
            double env = t < attack ? t / attack : t < attack + decay ? 1 + (z.sustain - 1) * (t - attack) / decay : z.sustain;
            if (t >= dur) { if (relFrom < 0) relFrom = env; env = relFrom * std::max(0.0, 1 - (t - dur) / release); }
            const double v = (a + (b - a) * fr) * env * amp;
            L[i] += float(v * gl); R[i] += float(v * gr);
            pos += ratio;
        }
    }
    if (right) *right = R;
    return L;
}

inline const Timbre& InstrumentTimbre(const Bank& bank, Preset& p, int key) {
    auto it = p.timbres.find(key);
    if (it != p.timbres.end()) return it->second;
    Timbre t;
    const std::vector<float> r = RenderNote(bank, p, key, 100, 0.6, 22050);
    if (!r.empty()) {
        Note n; n.start = 0; n.end = 0.6; n.pitch = key;
        t = TimbreOf(r.data(), r.size(), 22050, {n});
    }
    return p.timbres[key] = t;
}

// ---- picking instruments ------------------------------------------------------------------------------------------------------------
constexpr double kPartWeight[3] = {1, 0.6, 0.8};   // melody, harmony, bass

inline double PartCost(const Bank& bank, Preset& p, Part part, const PartShape& sh, const Heard* heard) {
    if (!p.pitched) return 50;
    const int shift = FitShift(p.center, part, sh.median), at = sh.median + shift;
    double c = std::max(0, std::abs(p.center - at) - 7) / 6.0 + std::abs(shift) / 12.0 * 0.35;
    if (sh.dur > 0.35 && p.ring < sh.dur) c += std::min(1.5, (sh.dur - p.ring) * 2);
    if (sh.dur < 0.2 && p.attack > 0.04) c += 0.7;
    if (part == Part::Melody && p.attack > 0.12) c += 0.6;
    if (part == Part::Bass && p.center > 60) c += 0.8;
    const int pi = int(part);
    if (heard && heard->part[pi].hasBrightness && heard->part[pi].count >= 3)
        c += 0.8 * TimbreDistance(heard->part[pi], InstrumentTimbre(bank, p, std::clamp(at, 12, 108)));
    return c + (1 - p.harmonicity);
}

struct KitMap { int key[3] = {-1, -1, -1}; double miss = 0; bool ok = false; };   // the kit's key for a kick, a snare and a hi-hat
inline KitMap MapKit(const Preset& kit, const Heard* heard) {
    KitMap m;
    std::vector<const Zone*> z;
    for (const Zone& q : kit.zones) if (q.centroid > 0) z.push_back(&q);
    if (z.empty()) return m;
    const auto near = [&](const Zone* q, int kind) {
        if (!heard || !heard->hasDrum[kind] || heard->drumCent[kind] <= 0 || q->band[kind] <= 0) return 0.0;
        return std::fabs(std::log2(q->band[kind] / heard->drumCent[kind]));
    };
    const auto by = [](auto f, const std::vector<const Zone*>& list) {
        const Zone* best = nullptr; double bc = 0;
        for (const Zone* q : list) { const double c = f(q); if (!best || c < bc) { best = q; bc = c; } }
        return best;
    };
    const Zone* kick = by([&](const Zone* q) { return q->centroid + (q->length < 0.08 ? 5000 : 0) + 300 * near(q, Kick); }, z);
    std::vector<const Zone*> rest;
    for (const Zone* q : z) if (q != kick) rest.push_back(q);
    const Zone* hat = by([&](const Zone* q) { return -q->centroid + (q->length > 0.6 ? 5000 : 0) + 1500 * near(q, Hat); }, rest);
    if (!hat) hat = kick;
    std::vector<const Zone*> mids;
    for (const Zone* q : z) if (q != kick && q != hat) mids.push_back(q);
    const bool heardSnare = heard && heard->hasDrum[Snare] && heard->drumCent[Snare] > 0;
    const Zone* snare = mids.empty() ? hat : by([&](const Zone* q) {
        return (heardSnare ? near(q, Snare) : std::fabs(std::log2(q->centroid / 2500))) + (q->length > 1.2 ? 1 : 0); }, mids);
    m.key[Kick] = kick->lo; m.key[Snare] = snare->lo; m.key[Hat] = hat->lo;
    m.miss = near(kick, Kick) + near(snare, Snare) + near(hat, Hat);
    m.ok = true;
    return m;
}

struct Pick {
    int preset[3] = {-1, -1, -1};       // melody, harmony, bass: indexes into bank.presets
    int drums = -1;                     // the drum kit
    int shift[3] = {0, 0, 0};
    bool mixed = false;
    double cost = 0;
};

// onlyFont < 0: every soundfont, and parts may come from different ones when that fits clearly better.
inline Pick PickInstruments(Bank& bank, const std::vector<Note>& notes, const Heard* heard, int onlyFont = -1) {
    const Shape shape = SongShape(notes);
    int total = 0;
    for (const Note& n : notes) if (n.part != Part::Drums) total++;
    total = std::max(1, total);
    const auto weight = [&](int part) { return kPartWeight[part] * std::max(0.15, double(shape.part[part].count) / total); };
    const auto choose = [&](const std::vector<int>& list) {
        Pick pk;
        std::set<int> used;
        for (int part : {0, 2, 1}) {                                   // melody, bass, harmony
            int bp = -1; double bc = 1e18;
            for (int i : list) {
                const double c = PartCost(bank, bank.presets[i], Part(part), shape.part[part], heard) + (used.count(i) && list.size() > 2 ? 0.4 : 0);
                if (c < bc) { bc = c; bp = i; }
            }
            pk.preset[part] = bp; used.insert(bp); pk.cost += bc * weight(part);
        }
        return pk;
    };
    std::vector<int> fonts;
    for (const Preset& p : bank.presets) if ((onlyFont < 0 || p.font == onlyFont) && std::find(fonts.begin(), fonts.end(), p.font) == fonts.end()) fonts.push_back(p.font);
    Pick best; bool have = false;
    for (int f : fonts) {
        std::vector<int> ins;
        for (size_t i = 0; i < bank.presets.size(); i++) if (bank.presets[i].font == f && !bank.presets[i].drums && bank.presets[i].pitched) ins.push_back(int(i));
        if (ins.empty()) continue;
        Pick c = choose(ins);
        c.cost += ins.size() < 3 ? 0.5 : 0;
        if (!have || c.cost < best.cost) { best = c; have = true; }
    }
    if (!have) return best;
    if (onlyFont < 0) {
        std::vector<int> all;
        for (size_t i = 0; i < bank.presets.size(); i++) if (!bank.presets[i].drums && bank.presets[i].pitched) all.push_back(int(i));
        Pick mixed = choose(all);
        std::set<int> used;
        for (int part = 0; part < 3; part++) used.insert(bank.presets[mixed.preset[part]].font);
        mixed.cost += 0.25 * double(used.size() - 1);
        if (used.size() > 1 && mixed.cost < best.cost - 0.05) { best = mixed; best.mixed = true; }
    }
    for (int part = 0; part < 3; part++) best.shift[part] = FitShift(bank.presets[best.preset[part]].center, Part(part), shape.part[part].median);
    // drums: the kit whose kick, snare and hi-hat sound most like the recording's (the melody's own soundfont's kit if close)
    double kc = 1e18;
    for (size_t i = 0; i < bank.presets.size(); i++) {
        const Preset& p = bank.presets[i];
        if (!p.drums || (onlyFont >= 0 && p.font != onlyFont)) continue;
        const KitMap m = MapKit(p, heard);
        if (!m.ok) continue;
        const double c = m.miss - (p.font == bank.presets[best.preset[0]].font ? 0.3 : 0);
        if (c < kc) { kc = c; best.drums = int(i); }
    }
    return best;
}

// ---- the whole arrangement: parts to channels, at balanced levels ---------------------------------------------------------------------
struct Arrangement {
    std::vector<seq::SeqChannel> channels;
    Pick pick;
    int drumHits = 0;                   // drum notes written (0 when the song has too few drum hits to count as drums)
};

// notes: the cleaned notes with their parts; hits: the drum finder's hits; seconds: the song's length.
inline Arrangement Arrange(Bank& bank, const std::vector<Note>& notesIn, const std::vector<DrumHit>& hits, const Heard* heard, double seconds) {
    Arrangement out;
    out.pick = PickInstruments(bank, notesIn, heard);
    const Pick& pk = out.pick;
    if (pk.preset[0] < 0) return out;
    std::vector<Note> notes = notesIn;
    // the bass as one smooth line: small gaps closed, its loudness kept even
    std::vector<Note*> bass;
    for (Note& n : notes) if (n.part == Part::Bass) bass.push_back(&n);
    std::stable_sort(bass.begin(), bass.end(), [](const Note* a, const Note* b) { return a->start < b->start; });
    for (size_t i = 0; i + 1 < bass.size(); i++) { const double gap = bass[i + 1]->start - bass[i]->end; if (gap > 0 && gap < 0.15) bass[i]->end = bass[i + 1]->start; }
    for (Note* n : bass) n->vel = int(std::lround(45 + 0.55 * n->vel));
    // drums, when the song has them
    KitMap kit;
    if (pk.drums >= 0 && seconds > 0 && double(hits.size()) / seconds > 1) kit = MapKit(bank.presets[pk.drums], heard);
    if (kit.ok) for (const DrumHit& h : hits) {
        Note n; n.start = h.time; n.end = h.time + 0.15; n.pitch = kit.key[h.kind]; n.kind = h.kind; n.vel = int(std::lround(h.vel)); n.part = Part::Drums;
        notes.push_back(n);
        out.drumHits++;
    }
    const auto presetOf = [&](Part part) -> Preset* {
        const int i = part == Part::Drums ? (kit.ok ? pk.drums : -1) : pk.preset[int(part)];
        return i >= 0 ? &bank.presets[i] : nullptr;
    };
    const auto keyOf = [&](const Note& n) { return n.part == Part::Drums ? n.pitch : std::clamp(n.pitch + pk.shift[int(n.part)], 0, 127); };
    // how loud each part comes out (from one rendered note per key and length), so the parts can be balanced as the page does
    double energy[4] = {0, 0, 0, 0}, span[4] = {0, 0, 0, 0};
    const auto noteEnergy = [&](const Note& n) {
        Preset* p = presetOf(n.part);
        const double d = std::max(0.05, n.end - n.start);
        const int db = int(std::lround(std::log2(d) * 2)), k = keyOf(n), id = k * 64 + (db + 32);
        auto it = p->energies.find(id);
        if (it == p->energies.end()) {
            std::vector<float> R;
            const std::vector<float> L = RenderNote(bank, *p, k, 100, std::pow(2.0, db / 2.0), 22050, &R);
            double e = 0;
            for (size_t i = 0; i < L.size(); i++) e += double(L[i]) * L[i] + double(R[i]) * R[i];
            it = p->energies.emplace(id, e / 22050).first;
        }
        return it->second * std::pow(n.vel / 100.0, 2.4);
    };
    for (const Note& n : notes) {
        if (!presetOf(n.part)) continue;
        energy[int(n.part)] += noteEnergy(n);
        span[int(n.part)] = std::max(span[int(n.part)], n.end);
    }
    const double share = heard ? heard->bassShare : 0.3;
    const double target[4] = {1, 0.5, std::clamp(0.8 * std::sqrt(share / 0.3), 0.5, 1.3), 0.75};
    double level[4] = {1, 1, 1, 1};
    for (int part = 0; part < 3; part++) {
        const double rms = std::sqrt(energy[part] / std::max(1.0, span[part]));
        level[part] = rms > 1e-6 ? target[part] * 0.1 / rms : 1;
    }
    // drums: each kind at its own level, a hi-hat well under the kick
    std::map<int, double> drumLevel;
    if (kit.ok) {
        static constexpr double want[3] = {1, 0.8, 0.35};
        for (const Note& n : notes) if (n.part == Part::Drums && !drumLevel.count(n.pitch)) {
            const std::vector<float> r = RenderNote(bank, *presetOf(Part::Drums), n.pitch, 100, 0.15, 22050);
            double e = 0;
            for (size_t i = 0; i < std::min<size_t>(r.size(), 4410); i++) e += double(r[i]) * r[i];
            const double rms = std::sqrt(e / 4410);
            drumLevel[n.pitch] = rms > 1e-6 ? want[n.kind] * 0.1 / rms : 1;
        }
        double e = 0;
        for (const Note& n : notes) if (n.part == Part::Drums) e += noteEnergy(n) * std::pow(drumLevel[n.pitch], 2);
        const double rms = std::sqrt(e / std::max(1.0, span[3] > 0 ? span[3] : 1));
        level[3] = rms > 1e-6 ? target[3] * 0.1 / rms : 1;
    }
    // In the game a channel's volume scales the sound directly and a note's velocity by its square; the page's notes go by (vel/127)^1.2.
    double drumTop = 1e-12;
    for (const auto& [k, l] : drumLevel) drumTop = std::max(drumTop, l);
    double gain[4];
    for (int part = 0; part < 4; part++) gain[part] = level[part] * (part == 3 ? drumTop : 1);
    double top = 1e-12;
    for (int part = 0; part < 4; part++) if (presetOf(Part(part)) && span[part] > 0) top = std::max(top, gain[part]);
    static constexpr int pan[4] = {64, 56, 64, 64}, reverb[4] = {40, 48, 24, 24};
    for (int part = 0; part < 4; part++) {
        Preset* p = presetOf(Part(part));
        if (!p || span[part] <= 0) continue;
        seq::SeqChannel ch;
        ch.fontId = p->font;
        ch.instrument = p->drums ? seq::kDrumInstrument : p->program;
        ch.volume = std::clamp(int(std::lround(127 * gain[part] / top)), 1, 127);
        ch.pan = pan[part];
        ch.reverb = reverb[part];
        for (const Note& n : notes) {
            if (int(n.part) != part) continue;
            double v = 127 * std::pow(std::clamp(n.vel, 1, 127) / 127.0, 0.6);
            if (part == 3) v *= std::sqrt(drumLevel[n.pitch] / drumTop);
            ch.notes.push_back({n.start, n.end, keyOf(n), std::clamp(int(std::lround(v)), 1, 127)});
        }
        out.channels.push_back(std::move(ch));
    }
    return out;
}

// The sequence for an arrangement, made to fit the game (at most 64 KB): if too big, the quietest harmony notes go first, then the song is cut short.
inline seq::Sequence WriteArrangement(Arrangement a) {
    seq::Sequence s = seq::WriteSequence(a.channels);
    for (int round = 0; s.tooBig && round < 12; round++) {
        size_t biggest = 0;
        for (size_t c = 1; c < a.channels.size(); c++) if (a.channels[c].notes.size() > a.channels[biggest].notes.size()) biggest = c;
        auto& ns = a.channels[biggest].notes;
        if (round < 4) {                                               // thin out the busiest channel's quietest notes
            std::vector<seq::SeqNote> sorted = ns;
            std::sort(sorted.begin(), sorted.end(), [](const seq::SeqNote& x, const seq::SeqNote& y) { return x.velocity < y.velocity; });
            const int cut = sorted[sorted.size() / 4].velocity;
            ns.erase(std::remove_if(ns.begin(), ns.end(), [&](const seq::SeqNote& n) { return n.velocity <= cut; }), ns.end());
        } else {                                                       // then shorten the song
            double end = 0;
            for (const auto& ch : a.channels) for (const auto& n : ch.notes) end = std::max(end, n.end);
            for (auto& ch : a.channels) ch.notes.erase(std::remove_if(ch.notes.begin(), ch.notes.end(), [&](const seq::SeqNote& n) { return n.start > end * 0.85; }), ch.notes.end());
        }
        s = seq::WriteSequence(a.channels);
    }
    return s;
}

}  // namespace royale::music
