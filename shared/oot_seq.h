// Writes a song (notes with start and end in seconds) as an Ocarina of Time sequence: the byte code the game's own music engine plays,
// so a converted song sounds exactly like the game's music (its instruments, envelopes, reverb and mixing).
// The format follows the decomp's sequence player (soh/src/code/audio_seqplayer.c):
//   sequence script: set up channels, tempo and volume, start each channel, wait for the length of the song, end.
//   channel script:  large notes on, soundfont, instrument, volume, pan, reverb, start its layers, wait, end.
//   layer script:    one line of notes and rests; a layer plays one note at a time, so a chord uses several layers.
// The game normally reads 0xC6's number as a place in the sequence's own font list. The mod starts these sequences itself with the
// player's default font set to 0xFF, and then the game takes the number as the soundfont itself (audio_seqplayer.c, case 0xC6),
// so no game resource has to be changed to play them. The soundfonts must be loaded first (Sequence::fonts lists them).
// ParseSequence reads the notes back out of the byte code, the way the game would play them; the tests use it to check the writer.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace royale::seq {

constexpr int kTempoBpm = 150;                          // 150 beats a minute at 48 ticks a beat: 120 ticks a second
constexpr double kTicksPerSecond = kTempoBpm * 48 / 60.0;
constexpr int kMaxLayers = 4;                           // layers per channel in the game
constexpr size_t kMaxBytes = 0xFFF0;                    // offsets in the byte code are 16 bit
constexpr int kDrumInstrument = 0x7F;                   // instrument 127 plays the soundfont's drums: the note picks the drum

struct SeqNote {
    double start, end;                                  // seconds
    int key;                                            // MIDI key (60 = middle C); for drums, 21 + the drum's index in the soundfont
    int velocity;                                       // 1..127
};

// One channel: an instrument from one soundfont and the notes it plays.
struct SeqChannel {
    int fontId = 0;                                     // the game's soundfont number
    int instrument = 0;                                 // instrument in that soundfont, or kDrumInstrument
    int volume = 100;                                   // 0..127
    int pan = 64;                                       // 0 left, 64 middle, 127 right
    int reverb = 0;                                     // 0..127
    std::vector<SeqNote> notes;
};

struct Sequence {
    std::vector<uint8_t> data;                          // the byte code
    std::vector<uint8_t> fonts;                         // the soundfonts it uses, in the order the game must load them
    double seconds = 0;                                 // how long it plays
    int droppedNotes = 0;                               // notes left out (more at once than a channel's layers)
    bool tooBig = false;                                // over 64 KB: the game cannot address it, so the song must be shortened
};

namespace detail {
inline void Put16(std::vector<uint8_t>& d, int v) { d.push_back(uint8_t((v >> 8) & 0xFF)); d.push_back(uint8_t(v & 0xFF)); }
// The game's "compressed" 16-bit number: one byte below 0x80, else two bytes with the top bit set (at most 0x7FFF).
inline void PutVar(std::vector<uint8_t>& d, int v) {
    if (v < 0x80) d.push_back(uint8_t(v));
    else { d.push_back(uint8_t(0x80 | ((v >> 8) & 0x7F))); d.push_back(uint8_t(v & 0xFF)); }
}
inline void Patch16(std::vector<uint8_t>& d, size_t at, int v) { d[at] = uint8_t((v >> 8) & 0xFF); d[at + 1] = uint8_t(v & 0xFF); }
inline int Ticks(double seconds) { return int(std::lround(seconds * kTicksPerSecond)); }
// A long wait as several waits the game can read (each at most 0x7FFF ticks).
inline void PutWait(std::vector<uint8_t>& d, int ticks) {
    while (ticks > 0) { int t = std::min(ticks, 0x7FFF); d.push_back(0xFD); PutVar(d, t); ticks -= t; }
}
}  // namespace detail

// channels: up to 16. Each channel's notes are spread over its 4 layers; a note that finds no free layer is dropped.
inline Sequence WriteSequence(const std::vector<SeqChannel>& channels) {
    using namespace detail;
    Sequence out;
    std::vector<uint8_t>& d = out.data;
    const int nCh = int(std::min<size_t>(channels.size(), 16));
    double end = 0;
    for (int c = 0; c < nCh; c++)
        for (const SeqNote& n : channels[c].notes) end = std::max(end, n.end);
    const int total = Ticks(end) + 24;                  // a short tail so the last notes can ring out
    out.seconds = total / kTicksPerSecond;
    for (int c = 0; c < nCh; c++)
        if (std::find(out.fonts.begin(), out.fonts.end(), uint8_t(channels[c].fontId)) == out.fonts.end()) out.fonts.push_back(uint8_t(channels[c].fontId));
    if (out.fonts.empty()) out.fonts.push_back(0);

    // the sequence script
    d.push_back(0xD3); d.push_back(0x20);               // mute behaviour (as the game's songs set it)
    d.push_back(0xD5); d.push_back(0x32);               // volume when muted
    d.push_back(0xD7); Put16(d, nCh >= 16 ? 0xFFFF : (1 << nCh) - 1);   // the channels to use
    d.push_back(0xDB); d.push_back(127);                // volume
    d.push_back(0xDD); d.push_back(kTempoBpm);          // tempo
    std::vector<size_t> chanAt(nCh);
    for (int c = 0; c < nCh; c++) { d.push_back(uint8_t(0x90 | c)); chanAt[c] = d.size(); Put16(d, 0); }
    PutWait(d, total);
    d.push_back(0xFF);

    // the channel scripts, each followed by its layers
    for (int c = 0; c < nCh; c++) {
        const SeqChannel& ch = channels[c];
        // spread the notes over the layers: each note goes to the first layer that is free by its start
        std::vector<SeqNote> notes = ch.notes;
        std::stable_sort(notes.begin(), notes.end(), [](const SeqNote& a, const SeqNote& b) { return a.start < b.start; });
        std::vector<std::vector<std::pair<int, SeqNote>>> layers;   // (start tick, note)
        std::vector<int> freeAt;
        for (const SeqNote& n : notes) {
            int s = Ticks(n.start), e = std::max(s + 1, Ticks(n.end));
            if (s < 0) continue;
            int use = -1;
            for (size_t l = 0; l < layers.size(); l++) if (freeAt[l] <= s) { use = int(l); break; }
            if (use < 0 && int(layers.size()) < kMaxLayers) { layers.emplace_back(); freeAt.push_back(0); use = int(layers.size()) - 1; }
            if (use < 0) { out.droppedNotes++; continue; }
            SeqNote t = n; t.end = e / kTicksPerSecond;             // remember the rounded end
            layers[use].push_back({s, t});
            freeAt[use] = e;
        }
        Patch16(d, chanAt[c], int(d.size()));
        d.push_back(0xC4);                                           // large notes: each note carries its own velocity and gate
        d.push_back(0xC6); d.push_back(uint8_t(ch.fontId));          // the font itself: the player runs with no font list (see below)
        d.push_back(0xC1); d.push_back(uint8_t(ch.instrument));
        d.push_back(0xDF); d.push_back(uint8_t(std::clamp(ch.volume, 0, 127)));
        d.push_back(0xDD); d.push_back(uint8_t(std::clamp(ch.pan, 0, 127)));
        d.push_back(0xD4); d.push_back(uint8_t(std::clamp(ch.reverb, 0, 127)));
        std::vector<size_t> layerAt(layers.size());
        for (size_t l = 0; l < layers.size(); l++) { d.push_back(uint8_t(0x88 | l)); layerAt[l] = d.size(); Put16(d, 0); }
        PutWait(d, total);
        d.push_back(0xFF);
        for (size_t l = 0; l < layers.size(); l++) {
            Patch16(d, layerAt[l], int(d.size()));
            int now = 0, transpose = 0;
            for (const auto& [s, n] : layers[l]) {
                int e = std::max(s + 1, Ticks(n.end));
                for (int rest = s - now; rest > 0; rest -= 0x7FFF) { d.push_back(0xC0); PutVar(d, std::min(rest, 0x7FFF)); }
                // a note command holds 64 semitones (0 = A0, MIDI 21); notes outside that are reached by transposing by octaves
                int semi = n.key - 21, want = 0;
                while (semi - want > 63) want += 12;
                while (semi - want < 0) want -= 12;
                if (want != transpose) { d.push_back(0xC2); d.push_back(uint8_t(int8_t(want))); transpose = want; }
                int len = std::min(e - s, 0x7FFF);
                d.push_back(uint8_t(0x00 | (semi - want)));
                PutVar(d, len);
                d.push_back(uint8_t(std::clamp(n.velocity, 1, 127)));
                d.push_back(0);                                      // gate 0: the note sounds for its whole length
                now = s + len;
            }
            d.push_back(0xFF);
        }
    }
    out.tooBig = d.size() > kMaxBytes;                               // the caller trims the song and writes it again
    return out;
}

// What the game would play: every note on every channel, read back from the byte code.
struct ParsedNote { int channel, layer; int startTick, ticks; int semitone; int velocity; int instrument; int fontId; };
struct ParsedChannel { int fontId = -1, instrument = -1, volume = -1, pan = -1, reverb = -1; bool largeNotes = false; };
struct Parsed { int tempo = 0, waitTicks = 0; uint16_t channelMask = 0; ParsedChannel channels[16]; std::vector<ParsedNote> notes; bool ok = true; };

inline Parsed ParseSequence(const std::vector<uint8_t>& d) {
    Parsed p;
    size_t pc = 0;
    auto u8 = [&](size_t& at) -> int { if (at >= d.size()) { p.ok = false; return 0; } return d[at++]; };
    auto u16 = [&](size_t& at) -> int { int a = u8(at); return (a << 8) | u8(at); };
    auto var = [&](size_t& at) -> int { int a = u8(at); return (a & 0x80) ? (((a & 0x7F) << 8) | u8(at)) : a; };
    std::vector<std::pair<int, size_t>> chans;
    for (int guard = 0; guard < 100000 && p.ok; guard++) {
        int c = u8(pc);
        if (c == 0xFF) break;
        else if (c == 0xD3 || c == 0xD5 || c == 0xDB) u8(pc);
        else if (c == 0xD7) p.channelMask = uint16_t(u16(pc));
        else if (c == 0xDD) p.tempo = u8(pc);
        else if (c == 0xFD) p.waitTicks += var(pc);
        else if ((c & 0xF0) == 0x90) chans.push_back({c & 0xF, size_t(u16(pc))});
        else { p.ok = false; break; }
    }
    for (auto [ch, at] : chans) {
        ParsedChannel& pc2 = p.channels[ch];
        std::vector<std::pair<int, size_t>> layers;
        for (int guard = 0; guard < 100000 && p.ok; guard++) {
            int c = u8(at);
            if (c == 0xFF) break;
            else if (c == 0xC4) pc2.largeNotes = true;
            else if (c == 0xC6) pc2.fontId = u8(at);
            else if (c == 0xC1) pc2.instrument = u8(at);
            else if (c == 0xDF) pc2.volume = u8(at);
            else if (c == 0xDD) pc2.pan = u8(at);
            else if (c == 0xD4) pc2.reverb = u8(at);
            else if (c == 0xFD) var(at);
            else if ((c & 0xF8) == 0x88) layers.push_back({c & 7, size_t(u16(at))});
            else { p.ok = false; break; }
        }
        for (auto [l, la] : layers) {
            int now = 0, transpose = 0;
            for (int guard = 0; guard < 200000 && p.ok; guard++) {
                int c = u8(la);
                if (c == 0xFF) break;
                if (c == 0xC0) { now += var(la); continue; }
                if (c == 0xC2) { transpose = int8_t(u8(la)); continue; }
                if (c < 0x40 && pc2.largeNotes) {
                    int len = var(la), vel = u8(la), gate = u8(la);
                    (void)gate;
                    p.notes.push_back({ch, l, now, len, (c & 0x3F) + transpose, vel, pc2.instrument, pc2.fontId});
                    now += len;
                    continue;
                }
                p.ok = false;
            }
        }
    }
    return p;
}

}  // namespace royale::seq
