#include "hud_text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>

namespace astra::app {

// ─── Self-authored stroke font (5-unit em, y grid 0..7; y increases DOWN) ────
// Each glyph: up to 12 segments, each {x0,y0,x1,y1} in grid units.
// Style: monoline sans, designed for small-px HUD legibility at 1280x720.
using Seg4 = std::array<int8_t, 4>;
struct GlyphDef { uint8_t n; Seg4 s[16]; };

static GlyphDef G(std::initializer_list<Seg4> segs) {
    GlyphDef g{};
    uint8_t i = 0;
    for (const auto& s : segs) { if (i >= 16) break; g.s[i++] = s; }
    g.n = i;
    return g;
}

// Segment caps for validation + unknown-glyph box.
static const GlyphDef& glyph_def(char c) {
    // Uppercase display policy (documented): ASCII lowercase -> uppercase.
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    static const std::map<char, GlyphDef> T = {
        {' ', G({})},
        {'A', G({{0,6,2,0},{2,0,4,6},{1,4,3,4}})},
        {'B', G({{0,0,0,6},{0,0,3,0},{3,0,4,1},{4,1,4,2},{4,2,3,3},{3,3,0,3},{3,3,4,4},{4,4,4,5},{4,5,3,6},{3,6,0,6}})},
        {'C', G({{4,1,3,0},{3,0,1,0},{1,0,0,1},{0,1,0,5},{0,5,1,6},{1,6,3,6},{3,6,4,5}})},
        {'D', G({{0,0,0,6},{0,0,3,0},{3,0,4,1},{4,1,4,5},{4,5,3,6},{3,6,0,6}})},
        {'E', G({{4,0,0,0},{0,0,0,6},{0,6,4,6},{0,3,3,3}})},
        {'F', G({{4,0,0,0},{0,0,0,6},{0,3,3,3}})},
        {'G', G({{4,1,3,0},{3,0,1,0},{1,0,0,1},{0,1,0,5},{0,5,1,6},{1,6,4,6},{4,6,4,3},{4,3,2,3}})},
        {'H', G({{0,0,0,6},{4,0,4,6},{0,3,4,3}})},
        {'I', G({{0,0,4,0},{2,0,2,6},{0,6,4,6}})},
        {'J', G({{4,0,4,5},{4,5,3,6},{3,6,1,6},{1,6,0,5}})},
        {'K', G({{0,0,0,6},{4,0,0,3},{0,3,4,6}})},
        {'L', G({{0,0,0,6},{0,6,4,6}})},
        {'M', G({{0,6,0,0},{0,0,2,3},{2,3,4,0},{4,0,4,6}})},
        {'N', G({{0,6,0,0},{0,0,4,6},{4,6,4,0}})},
        {'O', G({{0,1,1,0},{1,0,3,0},{3,0,4,1},{4,1,4,5},{4,5,3,6},{3,6,1,6},{1,6,0,5},{0,5,0,1}})},
        {'P', G({{0,6,0,0},{0,0,3,0},{3,0,4,1},{4,1,4,2},{4,2,3,3},{3,3,0,3}})},
        {'Q', G({{0,1,1,0},{1,0,3,0},{3,0,4,1},{4,1,4,5},{4,5,3,6},{3,6,1,6},{1,6,0,5},{0,5,0,1},{2,4,4,6}})},
        {'R', G({{0,6,0,0},{0,0,3,0},{3,0,4,1},{4,1,4,2},{4,2,3,3},{3,3,0,3},{2,3,4,6}})},
        {'S', G({{4,1,3,0},{3,0,1,0},{1,0,0,1},{0,1,0,2},{0,2,1,3},{1,3,3,3},{3,3,4,4},{4,4,4,5},{4,5,3,6},{3,6,1,6},{1,6,0,5}})},
        {'T', G({{0,0,4,0},{2,0,2,6}})},
        {'U', G({{0,0,0,5},{0,5,1,6},{1,6,3,6},{3,6,4,5},{4,5,4,0}})},
        {'V', G({{0,0,2,6},{2,6,4,0}})},
        {'W', G({{0,0,1,6},{1,6,2,3},{2,3,3,6},{3,6,4,0}})},
        {'X', G({{0,0,4,6},{4,0,0,6}})},
        {'Y', G({{0,0,2,3},{4,0,2,3},{2,3,2,6}})},
        {'Z', G({{0,0,4,0},{4,0,0,6},{0,6,4,6}})},
        {'0', G({{0,1,1,0},{1,0,3,0},{3,0,4,1},{4,1,4,5},{4,5,3,6},{3,6,1,6},{1,6,0,5},{0,5,0,1},{1,5,3,1}})}, // slashed zero
        {'1', G({{1,1,2,0},{2,0,2,6},{0,6,4,6}})},
        {'2', G({{0,1,1,0},{1,0,3,0},{3,0,4,1},{4,1,4,3},{4,3,0,6},{0,6,4,6}})},
        {'3', G({{0,0,4,0},{4,0,2,3},{2,3,3,3},{3,3,4,4},{4,4,4,5},{4,5,3,6},{3,6,0,6}})},
        {'4', G({{3,6,3,0},{3,0,0,4},{0,4,4,4}})},
        {'5', G({{4,0,0,0},{0,0,0,3},{0,3,3,3},{3,3,4,4},{4,4,4,5},{4,5,3,6},{3,6,0,6}})},
        {'6', G({{4,1,3,0},{3,0,1,0},{1,0,0,1},{0,1,0,5},{0,5,1,6},{1,6,3,6},{3,6,4,5},{4,5,4,4},{4,4,3,3},{3,3,0,3}})},
        {'7', G({{0,0,4,0},{4,0,1,6}})},
        {'8', G({{1,0,3,0},{3,0,4,1},{4,1,4,2},{4,2,3,3},{3,3,1,3},{1,3,0,2},{0,2,0,1},{0,1,1,0},{1,3,0,4},{0,4,0,5},{0,5,1,6},{1,6,3,6},{3,6,4,5},{4,5,4,4},{4,4,3,3}})},
        {'9', G({{1,6,3,6},{3,6,4,5},{4,5,4,1},{4,1,3,0},{3,0,1,0},{1,0,0,1},{0,1,0,2},{0,2,1,3},{1,3,4,3}})},
        {'-', G({{1,3,3,3}})},
        {'+', G({{1,3,3,3},{2,2,2,4}})},
        {'.', G({{2,6,2,5}})},
        {',', G({{2,5,2,6},{2,6,1,7}})},
        {':', G({{2,2,2,1},{2,5,2,4}})},
        {'/', G({{1,6,4,0}})},
        {'(', G({{3,0,2,1},{2,1,1,2},{1,2,1,4},{1,4,2,5},{2,5,3,6}})},
        {')', G({{1,0,2,1},{2,1,3,2},{3,2,3,4},{3,4,2,5},{2,5,1,6}})},
        {'[', G({{3,0,1,0},{1,0,1,6},{1,6,3,6}})},
        {']', G({{1,0,3,0},{3,0,3,6},{3,6,1,6}})},
        {'=', G({{1,2,3,2},{1,4,3,4}})},
        {'%', G({{4,0,0,6},{0,1,1,0},{1,0,2,1},{2,1,1,2},{1,2,0,1},{2,5,3,4},{3,4,4,5},{4,5,3,6},{3,6,2,5}})},
        {'<', G({{4,0,0,3},{0,3,4,6}})},
        {'>', G({{0,0,4,3},{4,3,0,6}})},
        {'_', G({{0,6,4,6}})},
    };
    static const GlyphDef UNKNOWN = G({{0,0,4,0},{4,0,4,6},{4,6,0,6},{0,6,0,0}}); // boxed placeholder
    auto it = T.find(c);
    return (it != T.end()) ? it->second : UNKNOWN;
}

bool glyph_available(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    static const std::string covered =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+.,:/()[]=%<>_";
    return covered.find(c) != std::string::npos;
}

uint32_t stroke_char(char c, float pen_x, float pen_y_top, float w, float h,
                     HudSegColor color, std::vector<HudSeg>& out) {
    const GlyphDef& g = glyph_def(c);
    const float su = w * (5.0f / 6.0f) / 4.0f; // unit step in x (advance w = 6 units, glyph 5)
    const float sv = h / 7.0f;                 // unit step in y
    for (uint8_t k = 0; k < g.n; ++k) {
        const auto& s = g.s[k];
        out.push_back(HudSeg{pen_x + (float)s[0] * su, pen_y_top - (float)s[1] * sv,
                             pen_x + (float)s[2] * su, pen_y_top - (float)s[3] * sv,
                             (uint8_t)color, {0, 0, 0}});
    }
    return g.n;
}

HudSegColor hud_color_for(const char* cls, bool available) {
    if (!available) return HudSegColor::NA;
    const std::string s = cls ? cls : "";
    if (s.find("REAL") == 0) return HudSegColor::REAL;
    if (s.find("DATA") == 0) return HudSegColor::DATA;
    if (s.find("CINEMATIC") == 0) return HudSegColor::CINEMATIC;
    if (s.find("ENGINE") == 0) return HudSegColor::SIMULATED;
    return HudSegColor::SIMULATED; // SIMULATED + everything else
}

bool build_hud_segments(const HudState& hud, const HudTextSpec& spec, std::vector<HudSeg>& out) {
    out.clear();
    float y = spec.pane_y0;
    uint32_t lines = 0;
    bool complete = true;

    const auto emit_line = [&](const std::string& text, HudSegColor color) {
        if (lines >= spec.max_lines) { complete = false; return; }
        float pen_x = spec.pane_x0;
        for (char raw : text) {
            stroke_char(raw, pen_x, y, spec.char_w, spec.char_h, color, out);
            pen_x += spec.char_w;
        }
        y += spec.line_advance;
        ++lines;
    };

    emit_line(hud.app_status, HudSegColor::HEADER);
    const auto emit_rows = [&](const std::vector<HudRow>& rows) {
        for (const auto& r : rows) {
            if (!r.available) {
                emit_line(r.label + ": " + r.value, HudSegColor::NA);
            } else {
                emit_line(r.label + ": " + r.value + "  [" + r.classification + "]",
                          hud_color_for(r.classification.c_str(), r.available));
            }
        }
    };
    emit_rows(hud.status);
    emit_rows(hud.selection);
    emit_rows(hud.frame);
    return complete;
}

} // namespace astra::app
