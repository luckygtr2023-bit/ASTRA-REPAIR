#include "render_math.h"

#include <cmath>

namespace astra::app {

Frustum6 frustum_from_viewproj(const Mat4& m) {
    // Gribb-Hartmann extraction for a row-of-columns layout:
    // column j of the matrix is m.m[j*4 + row]. Plane equations use the
    // matrix ROWS: left = row3 + row0, right = row3 - row0, etc.
    const float* g = m.m;
    const auto row = [&](int r) { return std::array<float,4>{g[0 * 4 + r], g[1 * 4 + r], g[2 * 4 + r], g[3 * 4 + r]}; };
    const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Frustum6 f{};
    const auto set = [&](int idx, float sx, float sy, float sz, float sw) {
        const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
        const float inv = (len > 1e-12f) ? 1.0f / len : 1.0f;
        f.p[idx][0] = sx * inv; f.p[idx][1] = sy * inv; f.p[idx][2] = sz * inv; f.p[idx][3] = sw * inv;
    };
    set(0, r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]); // left
    set(1, r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]); // right
    set(2, r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]); // bottom
    set(3, r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]); // top
    set(4, r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3]); // near
    set(5, r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]); // far
    return f;
}

bool sphere_visible(const Frustum6& f, const float c[3], float r) {
    for (int k = 0; k < 6; ++k) {
        const float d = f.p[k][0] * c[0] + f.p[k][1] * c[1] + f.p[k][2] * c[2] + f.p[k][3];
        if (d < -r) return false;
    }
    return true;
}

uint8_t lod_select(float radius, float distance, float tan_half_fov, float threshold) {
    if (!(radius > 0.0f) || !(distance > 0.0f) || !(tan_half_fov > 0.0f)) return 1; // degenerate -> LOW
    const float frac = radius / (distance * tan_half_fov);
    return (frac > threshold) ? 0 : 1; // 0 = HIGH, 1 = LOW
}

float clamp_exposure(float e) {
    if (e < 0.05f) return 0.05f;
    if (e > 20.0f) return 20.0f;
    return e;
}

float clamp_bloom_strength(float s) {
    if (s < 0.0f) return 0.0f;
    if (s > 1.5f) return 1.5f;
    return s;
}

float bloom_threshold() { return 1.0f; }

HdrFormatChoice choose_hdr_format(bool r16_ok_render, bool r16_ok_sample) {
    return (r16_ok_render && r16_ok_sample) ? HdrFormatChoice::R16G16B16A16_SFLOAT
                                            : HdrFormatChoice::UNSUPPORTED;
}

BodyInstance pack_body_instance(float rx, float ry, float rz, float radius,
                                float cr, float cg, float cb, bool selected,
                                bool emissive) {
    BodyInstance bi{};
    bi.pos_radius[0] = rx; bi.pos_radius[1] = ry; bi.pos_radius[2] = rz; bi.pos_radius[3] = radius;
    bi.color_flag[0] = cr; bi.color_flag[1] = cg; bi.color_flag[2] = cb;
    bi.color_flag[3] = (selected ? 1.0f : 0.0f) + (emissive ? 2.0f : 0.0f);
    return bi;
}

// ── v0.6 additions ────────────────────────────────────────────────────────────

Extent2 half_extent(Extent2 e) {
    Extent2 h{e.w / 2, e.h / 2};
    if (h.w < 1u) h.w = 1u;
    if (h.h < 1u) h.h = 1u;
    return h;
}

bool compact_lod(const uint8_t* lod, uint32_t n, CompactResult& out) {
    out = CompactResult{};
    if (!lod || n > 128u) return false;
    for (uint32_t i = 0; i < n; ++i) {
        if (lod[i] == 1u) out.low_order[out.low_count++] = i;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (lod[i] == 2u) out.high_order[out.high_count++] = i;
    }
    return true;
}

void build_indirect_commands(const CompactResult& r, uint32_t low_idx_count, uint32_t high_idx_count,
                             IndirectCmd& out_low, IndirectCmd& out_high) {
    out_low = {low_idx_count, r.low_count, 0u, 0, 0u};
    out_high = {high_idx_count, r.high_count, 0u, 0, 0u};
}

void make_axes_segments(float origin[3], float L, Seg3 out3[3]) {
    out3[0] = {origin[0], origin[1], origin[2], origin[0] + L, origin[1], origin[2]};
    out3[1] = {origin[0], origin[1], origin[2], origin[0], origin[1] + L, origin[2]};
    out3[2] = {origin[0], origin[1], origin[2], origin[0], origin[1], origin[2] + L};
}

void make_selection_marker(float center[3], float half_span, Seg3 out2[2]) {
    out2[0] = {center[0] - half_span, center[1], center[2], center[0] + half_span, center[1], center[2]};
    out2[1] = {center[0], center[1] - half_span, center[2], center[0], center[1] + half_span, center[2]};
}

bool gpu_timing_supported(uint32_t timestamp_valid_bits, float timestamp_period_ns) {
    return timestamp_valid_bits > 0u && timestamp_period_ns > 0.0f;
}

double gpu_ms_from_ticks(uint64_t tick_delta, float timestamp_period_ns) {
    return (double)tick_delta * (double)timestamp_period_ns / 1.0e6;
}

} // namespace astra::app
