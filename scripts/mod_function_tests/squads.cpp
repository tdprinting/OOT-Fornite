// Layer 2b (scripts/test_mod_functions.py): Squads.inc's pad test and the knocked-down button strip.
// EXTRACT mod/Royale/features/Squads.inc  PadUnderfoot StripButtons
#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include "shared/balance.h"
namespace royale { struct HudState { std::vector<std::pair<float, float>> launchPads; }; }
struct Buttons { uint16_t button = 0; };
struct Input { Buttons cur, press, rel; };
constexpr uint16_t BTN_A = 0x8000, BTN_B = 0x4000, BTN_Z = 0x2000, BTN_START = 0x1000, BTN_L = 0x0020, BTN_R = 0x0010;
// @EXTRACTED
int main() {
    royale::HudState h;
    assert(PadUnderfoot(h, 0, 0) == -1);   // no pads
    h.launchPads = {{1000.0f, -500.0f}, {-200.0f, 300.0f}};
    assert(PadUnderfoot(h, 1000.0f, -500.0f) == 0);                                 // on the centre
    assert(PadUnderfoot(h, -200.0f + royale::kLaunchPadRadius - 1.0f, 300.0f) == 1);   // just inside the rim
    assert(PadUnderfoot(h, -200.0f + royale::kLaunchPadRadius + 1.0f, 300.0f) == -1);  // just outside
    assert(PadUnderfoot(h, 0, 0) == -1);
    // Knocked down: only Start and L are left, on every edge (held, pressed, released).
    Input in;
    in.cur.button = in.press.button = in.rel.button = BTN_A | BTN_B | BTN_Z | BTN_START | BTN_L | BTN_R;
    const uint16_t keep = BTN_START | BTN_L;
    StripButtons(in, static_cast<uint16_t>(~keep));
    assert(in.cur.button == keep && in.press.button == keep && in.rel.button == keep);
    in.cur.button = in.press.button = BTN_A | BTN_B;
    StripButtons(in, BTN_A);   // a revive or pad launch keeps A from the game
    assert(in.cur.button == BTN_B && in.press.button == BTN_B);
    return 0;
}
