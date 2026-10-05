// Tests for turning songs into Ocarina of Time music: the sequence writer (read back with the same parser the tests trust).
#include "oot_arrange.h"
#include "oot_seq.h"
#include <cstdio>
#include <cstdlib>

using namespace royale::seq;
using namespace royale::music;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void TestRoundTrip() {
    SeqChannel melody;
    melody.fontId = 3; melody.instrument = 5; melody.volume = 110; melody.pan = 40; melody.reverb = 30;
    melody.notes = {{0.0, 0.5, 60, 100}, {0.5, 1.0, 64, 90}, {1.25, 2.0, 108, 80}, {2.0, 2.5, 21, 70}};
    SeqChannel chords;
    chords.fontId = 7; chords.instrument = 2;
    chords.notes = {{0, 1, 60, 60}, {0, 1, 64, 60}, {0, 1, 67, 60}, {0, 1, 72, 60}, {0, 1, 76, 60}};   // five at once: one is dropped
    SeqChannel drums;
    drums.fontId = 3; drums.instrument = kDrumInstrument;
    drums.notes = {{0, 0.1, 21 + 2, 120}, {0.5, 0.6, 21 + 10, 100}};
    Sequence s = WriteSequence({melody, chords, drums});
    CHECK(!s.tooBig);
    CHECK(s.droppedNotes == 1);
    CHECK(s.fonts.size() == 2 && s.fonts[0] == 3 && s.fonts[1] == 7);

    Parsed p = ParseSequence(s.data);
    CHECK(p.ok);
    CHECK(p.tempo == kTempoBpm);
    CHECK(p.channelMask == 0x7);
    CHECK(p.waitTicks == int(std::lround(2.5 * kTicksPerSecond)) + 24);
    CHECK(p.channels[0].largeNotes && p.channels[0].instrument == 5 && p.channels[0].volume == 110 && p.channels[0].pan == 40 && p.channels[0].reverb == 30);
    CHECK(p.channels[0].fontId == 3 && p.channels[1].fontId == 7 && p.channels[2].fontId == 3);
    CHECK(p.channels[2].instrument == kDrumInstrument);

    int seen = 0;
    for (const ParsedNote& n : p.notes) {
        if (n.channel != 0) continue;
        seen++;
        int key = n.semitone + 21;
        if (n.startTick == 0) CHECK(key == 60 && n.ticks == 60 && n.velocity == 100);
        if (n.startTick == 60) CHECK(key == 64 && n.ticks == 60 && n.velocity == 90);
        if (n.startTick == 150) CHECK(key == 108 && n.ticks == 90);   // high C: written with a transpose
        if (n.startTick == 240) CHECK(key == 21);                      // and back down for the lowest A
    }
    CHECK(seen == 4);
    int chordNotes = 0;
    for (const ParsedNote& n : p.notes) if (n.channel == 1) { chordNotes++; CHECK(n.startTick == 0 && n.ticks == 120); }
    CHECK(chordNotes == 4);
    int drumHits = 0;
    for (const ParsedNote& n : p.notes) if (n.channel == 2) { drumHits++; CHECK(n.semitone == 2 || n.semitone == 10); }
    CHECK(drumHits == 2);
}

static void TestLongSong() {
    // a five minute song with a long gap: waits and rests longer than one command can hold
    SeqChannel ch;
    ch.notes = {{0, 1, 60, 100}, {299, 300, 62, 100}};
    Sequence s = WriteSequence({ch});
    Parsed p = ParseSequence(s.data);
    CHECK(p.ok && !s.tooBig);
    CHECK(p.notes.size() == 2);
    CHECK(p.notes[1].startTick == int(std::lround(299 * kTicksPerSecond)));
    CHECK(p.waitTicks > 0x7FFF);
}

static void TestTooBig() {
    SeqChannel ch;
    for (int i = 0; i < 20000; i++) ch.notes.push_back({i * 0.05, i * 0.05 + 0.04, 40 + i % 30, 100});
    CHECK(WriteSequence({ch}).tooBig);
}

// A small made-up soundfont: a held bright lead, a plucked tone and a low bass, and a drum kit (kick, snare, hi-hat).
static Bank MakeBank() {
    Bank bank;
    const double R = kOutputRate;
    auto tone = [&](int root, bool held, int harmonics, double fade) {
        Sample s;
        const double f = 440 * std::pow(2.0, (root - 69) / 12.0);
        const int n = int(R * 1.2);
        for (int i = 0; i < n; i++) {
            double v = 0;
            for (int h = 1; h <= harmonics; h++) v += std::sin(2 * royale::music::detail::kPi * f * h * i / R) / h;
            s.pcm.push_back(int16_t(9000 * v * std::exp(-fade * i / R)));
        }
        if (held) { const int period = int(std::lround(R / f)); s.loopStart = int(R * 0.5); s.loopEnd = s.loopStart + period * 40; }
        bank.samples.push_back(s);
        Preset p; p.font = 4; p.program = int(bank.presets.size());
        Zone z; z.sample = int(bank.samples.size()) - 1; z.tuning = std::pow(2.0, (60 - root) / 12.0); z.attack = 0.005; z.decay = 0.01; z.release = held ? 0.15 : 0.3;
        p.zones.push_back(z);
        bank.presets.push_back(p);
    };
    tone(76, true, 6, 0);      // lead
    tone(64, false, 3, 4);     // pluck
    tone(40, false, 2, 2);     // bass
    Preset kit; kit.font = 4; kit.drums = true;
    uint32_t seed = 7;
    auto noise = [&] { seed = seed * 1103515245 + 12345; return ((seed >> 16) & 0x7FFF) / 16384.0 - 1; };
    double last = 0;
    auto hp = [&](double x) { const double d = x - last; last = x; return d; };   // noise with its lows taken out: a hiss
    for (int d = 0; d < 3; d++) {
        Sample s;
        for (int i = 0; i < int(R * 0.3); i++) {
            const double t = i / R;
            double v = d == 0 ? std::sin(2 * royale::music::detail::kPi * 55 * t) * std::exp(-t * 9) : d == 1 ? 0.6 * (noise() + noise() + noise()) / 3 * std::exp(-t * 18) + 0.3 * std::sin(2 * royale::music::detail::kPi * 190 * t) * std::exp(-t * 18) : 0.4 * hp(noise()) * std::exp(-t * 60);
            s.pcm.push_back(int16_t(20000 * v));
        }
        bank.samples.push_back(s);
        Zone z; z.lo = z.hi = z.fixedKey = 21 + d; z.sample = int(bank.samples.size()) - 1; z.attack = 0.001; z.decay = 0.01; z.release = 0.1;
        kit.zones.push_back(z);
    }
    bank.presets.push_back(kit);
    for (Preset& p : bank.presets) Analyze(bank, p);
    return bank;
}

static void TestArrange() {
    Bank bank = MakeBank();
    CHECK(bank.presets[0].pitched && bank.presets[1].pitched && bank.presets[2].pitched);
    CHECK(bank.presets[2].center < bank.presets[0].center);
    std::vector<Note> notes;
    std::vector<DrumHit> hits;
    for (int i = 0; i < 32; i++) {
        Note m; m.start = i * 0.5; m.end = m.start + 0.45; m.pitch = 72 + (i % 5); m.vel = 100; m.part = Part::Melody; notes.push_back(m);
        Note b; b.start = i * 0.5; b.end = b.start + 0.4; b.pitch = 36 + (i % 3); b.vel = 90; b.part = Part::Bass; notes.push_back(b);
        hits.push_back({i * 0.5, i % 2 ? int(Snare) : int(Kick), 100, 1, 0});
        hits.push_back({i * 0.5 + 0.25, int(Hat), 80, 1, 0});
    }
    Arrangement a = Arrange(bank, notes, hits, nullptr, 16);
    CHECK(a.pick.preset[2] == 2);                       // the bass goes to the bass
    CHECK(a.drumHits == 64);
    CHECK(a.channels.size() == 3);                      // melody, bass, drums (no harmony notes)
    KitMap k = MapKit(bank.presets[3], nullptr);
    CHECK(k.ok && k.key[Kick] == 21 && k.key[Hat] == 23 && k.key[Snare] == 22);
    Sequence s = WriteArrangement(a);
    Parsed p = ParseSequence(s.data);
    CHECK(p.ok && !s.tooBig);
    CHECK(p.notes.size() == 32 + 32 + 64);
    CHECK(s.fonts.size() == 1 && s.fonts[0] == 4);
    for (const SeqChannel& ch : a.channels) CHECK(ch.volume >= 1 && ch.volume <= 127);
}

static void TestVadpcm() {
    // one frame, predictor of zeros: the samples come out as the nibbles times the scale
    const int16_t book[2 * 8] = {};
    const uint8_t frame[9] = {0x10, 0x12, 0x34, 0x56, 0x7F, 0x89, 0xAB, 0xCD, 0xEF};
    std::vector<int16_t> pcm = DecodeVadpcm(frame, 9, book, 2, 1, false);
    CHECK(pcm.size() == 16);
    CHECK(pcm[0] == 2 && pcm[1] == 4 && pcm[6] == 14 && pcm[7] == -2 && pcm[8] == -16 && pcm[15] == -2);
}

int main() {
    TestArrange();
    TestVadpcm();
    TestRoundTrip();
    TestLongSong();
    TestTooBig();
    if (failures) { std::printf("%d failures\n", failures); return 1; }
    std::printf("music tests passed\n");
    return 0;
}
