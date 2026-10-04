#pragma once
// The end-of-match replay: while a match runs the server notes where everybody is every couple of seconds, and who eliminated whom; when it
// ends, everyone is sent the lot and the results screen plays it back as a top-down map at high speed.
#include <cstdint>
#include <vector>

namespace royale {

constexpr float kReplayStepSec = 2.0f;           // how often positions are noted
constexpr int kReplayMaxFrames = 600;            // twenty minutes: more than any match
constexpr int16_t kReplayGone = 0x7FFF;          // the x of a player who is already out

struct ReplayKill {
    uint16_t frame = 0;                          // the frame it happened at
    uint16_t killer = 0xFFFF;                    // 0xFFFF: the storm, a boss or a disconnect
    uint16_t victim = 0;
};

struct Replay {
    std::vector<uint16_t> ids;                   // who the columns are, in order
    std::vector<std::vector<int16_t>> frames;    // per frame: x, z for each id (x is kReplayGone once out)
    std::vector<ReplayKill> kills;
    bool Valid() const { return !ids.empty() && !frames.empty(); }
};

} // namespace royale
