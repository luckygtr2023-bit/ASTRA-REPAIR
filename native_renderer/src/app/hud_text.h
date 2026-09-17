#pragma once
// ASTRA COSMOS v0.6 — in-canvas HUD text backbone (pure CPU, unit-testable).
//
// Converts the authoritative HudState (hud_state.h — single source of truth for
// values/format) into line-segment stroke geometry for real Vulkan rendering.
// Font: self-authored stroke glyphs on a 5x8 grid (y: 0 top .. 7 descender),
// no third-party font data embedded. NOT AVAILABLE rows and classifications
// are carried through from hud_state (no re-derivation here).

#include "hud_state.h"

#include <cstdint>
#include <vector>

namespace astra::app {

// Color classes for HUD segments (display styling only; classification TEXT
// comes from hud_state values; these classes color-code by the same tokens).
enum class HudSegColor : uint8_t { HEADER = 0, REAL = 1, DATA = 2, SIMULATED = 3, CINEMATIC = 4, NA = 5 };

struct HudSeg { float x0, y0, x1, y1; uint8_t color, rsv[3]; }; // NDC-space segments (20B, no padding; safe to memcpy to GPU)
static_assert(sizeof(HudSeg) == 20, "vertex-buffer upload contract");

// Layout parameters (NDC coordinates; caller adapts to aspect by choosing
// char width/height). pane_*: top-left corner; line_advance < 0 moves down.
struct HudTextSpec {
    float pane_x0 = -0.98f;
    float pane_y0 = 0.96f;
    float char_w = 0.017f;    // NDC per glyph advance (5 units + 1 gap of 6)
    float char_h = 0.033f;    // NDC per glyph height (7 grid units)
    float line_advance = -0.040f;
    uint32_t max_lines = 22;
};

// Classification token -> color class (mirrors hud_state tokens exactly).
HudSegColor hud_color_for(const char* classification, bool available);

// Build stroke segments for the WHOLE HUD (app_status + rows, value + "[CLASS]"
// suffix per row, preserving NOT AVAILABLE/available=false rows verbatim).
// Returns false (and truncates deterministically) if line budget is exceeded.
bool build_hud_segments(const HudState& hud, const HudTextSpec& spec,
                        std::vector<HudSeg>& out);

// Stroke geometry for one character (NDC space at pen position). Returns the
// number of segments appended (0-14). Uppercase display policy: lowercase
// ASCII is folded to uppercase (documented display-formatting transform).
uint32_t stroke_char(char c, float pen_x, float pen_y_baseline, float w, float h,
                     HudSegColor color, std::vector<HudSeg>& out);

// Total glyph coverage (for validation: every required HUD character exists).
bool glyph_available(char c);

} // namespace astra::app
