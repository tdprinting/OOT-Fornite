#pragma once
// Game-independent navigation, timing and layout for the native War Table menu.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "map.h"

namespace royale::wartable {
enum class Page { Play, Character, Settings, Guide, Join, Practice, Lobby, Pause, Tools, Results, Keyboard, Quit };
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
struct Widget {
    int id;
    Rect rect;
    std::string label;
    std::string hint;
    bool enabled = true;
    bool adjustable = false;
};
inline int CycleMap(int current, int direction) {
    for (int n = 0; n < kMapCount; ++n) {
        current = (current + (direction < 0 ? -1 : 1) + kMapCount) % kMapCount;
        if (IsPlayableMap(current)) return current;
    }
    return 0;
}
inline float Approach(float current, float target, float dt, bool reduced) {
    return reduced ? target : current + (target - current) * (1.0f - std::exp(-18.0f * std::clamp(dt, 0.0f, 0.1f)));
}
struct Repeat {
    int held = 0;
    float remaining = 0;
    bool Tick(int direction, float dt) {
        if (!direction) { held = 0; remaining = 0; return false; }
        if (direction != held) { held = direction; remaining = 0.32f; return true; }
        remaining -= std::clamp(dt, 0.0f, 0.1f);
        if (remaining <= 0) { remaining += 0.11f; return true; }
        return false;
    }
};
// Spatial navigation works for rows, columns, and the controller keyboard alike.
inline int Neighbor(const std::vector<Widget>& widgets, int selected, int dx, int dy) {
    if (widgets.empty()) return -1;
    selected = std::clamp(selected, 0, static_cast<int>(widgets.size()) - 1);
    const Rect& a = widgets[selected].rect;
    float best = 1e9f;
    int result = selected;
    for (int i = 0; i < static_cast<int>(widgets.size()); ++i) {
        if (i == selected || !widgets[i].enabled) continue;
        const Rect& b = widgets[i].rect;
        const float x = b.x + b.w / 2 - a.x - a.w / 2;
        const float y = b.y + b.h / 2 - a.y - a.h / 2;
        const float forward = x * dx + y * dy;
        const float cross = std::fabs(x * dy - y * dx);
        if (forward < 1) continue;
        const float score = forward + cross * 3;
        if (score < best) { best = score; result = i; }
    }
    return result;
}
inline bool CanStart(bool joined, bool host, bool lobby, bool havePlayer, bool pending) {
    return joined && host && lobby && havePlayer && !pending;
}
inline bool ValidAddress(const std::string& s) {
    if (s.empty() || s.size() > 63) return false;
    return s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:") == std::string::npos;
}
inline bool ValidPort(const std::string& s, uint16_t* out) {
    if (s.empty() || s.size() > 5 || s.find_first_not_of("0123456789") != std::string::npos) return false;
    const int n = std::stoi(s);
    if (n < 1024 || n > 65535) return false;
    if (out) *out = static_cast<uint16_t>(n);
    return true;
}
struct Canvas {
    float scale, left, top;
    static Canvas Fit(float width, float height) {
        const float s = std::max(0.001f, std::min(width / 640.0f, height / 360.0f));
        return {s, (width - 640 * s) / 2, (height - 360 * s) / 2};
    }
    float X(float px) const { return (px - left) / scale; }
    float Y(float py) const { return (py - top) / scale; }
};
} // namespace royale::wartable
