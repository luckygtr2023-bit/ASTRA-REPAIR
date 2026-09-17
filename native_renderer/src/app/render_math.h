#pragma once
// ASTRA COSMOS v0.5 — renderer math gate (CPU mirror, unit-testable).
//
// Exact mirror of the math used by the GPU visibility/LOD compute shader and
// the post pipeline parameters. The GPU never decides science; it only
// decides VISIBILITY/LOD of visualization proxies. These functions are the
// byte-exact CPU policy twin (deterministic), used for:
//   1. unit gates (no GPU needed to prove rule correctness),
//   2. the explicit documented fallback when a device cannot run compute.

#include "orbit_camera.h"
#include <array>
#include <cstdint>

namespace astra::app {

// Frustum planes (Gribb-Hartmann) from a column-major proj*view matrix.
// Each plane xyz = normal (points inward), w = offset, normalized to unit n.
struct Frustum6 { float p[6][4]; };
Frustum6 frustum_from_viewproj(const Mat4& proj_view);
// true if a sphere (center, radius) intersects the frustum.
bool sphere_visible(const Frustum6& f, const float center[3], float radius);

// Screen-fraction LOD policy (same formula in cull.comp): frac = radius /
// (distance * tan(fov/2)); HIGH when frac > threshold (default 0.010 ≈ >1%
// of screen height), LOW otherwise (failsafe for degenerate inputs = LOW).
// their radius; tiny/far bodies go LOW (saves vertex work — real resource
// selection: subdivision-2 (960 idx) vs subdivision-1 (240 idx) spheres).
uint8_t lod_select(float radius, float distance, float tan_half_fov, float threshold = 0.010f);

// Display parameters (CINEMATIC display transforms; never touch science).
float clamp_exposure(float e);        // [0.05, 20]
float clamp_bloom_strength(float s);  // [0, 1.5]
float bloom_threshold();              // 1.0 in HDR linear (documented policy)

// HDR color format policy: prefer R16G16B16A16_SFLOAT, never fall back to a
// lower-precision format silently — if neither candidate is supported for
// its needed usages, the caller must fail explicitly.
enum class HdrFormatChoice { R16G16B16A16_SFLOAT, UNSUPPORTED };
HdrFormatChoice choose_hdr_format(bool r16_ok_render, bool r16_ok_sample);

// Instance record consumed by the instanced mesh draw (SSBO binding 0).
// 32 bytes per record; camera-relative render coordinates only.
struct BodyInstance {
    float pos_radius[4];  // xyz = target-relative render pos, w = visual radius
    float color_flag[4];  // rgb = material color, w = (selected ? 1 : 0)
};
static_assert(sizeof(BodyInstance) == 32, "SSBO layout contract");

// Pack one instance from authoritative inputs (tested against invariants).
// v0.6: color_flag.w = (selected ? 1 : 0) + (emissive ? 2 : 0) — the shader
// decodes both bits (emissive = w >= 1.5; selected = remainder >= 0.5).
BodyInstance pack_body_instance(float rx, float ry, float rz, float radius,
                                float cr, float cg, float cb, bool selected,
                                bool emissive = false);

// ── v0.6 additions ────────────────────────────────────────────────────────────

// Half-resolution policy for the bloom chain (>= 1 px, deterministic).
struct Extent2 { uint32_t w, h; };
Extent2 half_extent(Extent2 e);

// Deterministic compaction mirror of cull.comp (single workgroup, serial
// thread-0 pass): given per-body lod classes (0=hidden,1=LOW,2=HIGH), produce
// the exact same output ordering (LOW list then HIGH list), and the two
// indirect draw commands that the shader writes.
struct CompactResult {
    uint32_t low_count = 0;
    uint32_t high_count = 0;
    uint32_t low_order[128];   // source indices in LOW list order
    uint32_t high_order[128];  // source indices in HIGH list order
};
bool compact_lod(const uint8_t* lod_classes, uint32_t n, CompactResult& out);

// Plain mirror cof VkDrawIndexedIndirectCommand (no Vulkan types in app layer).
struct IndirectCmd {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};
// Builds the two commands from a compaction result (same values the shader emits).
void build_indirect_commands(const CompactResult& r, uint32_t low_idx_count, uint32_t high_idx_count,
                             IndirectCmd& out_low, IndirectCmd& out_high);

// Reference-frame axes + selection marker geometry (render units; deterministic).
struct Seg3 { float x0, y0, z0, x1, y1, z1; };
// Three axes X/Y/Z of length L centered at the target-relative origin.
void make_axes_segments(float origin[3], float L, Seg3 out3[3]);
// Selection crosshair: 2 perpendicular line segments in the XY plane (rendered
// in body-local orientation; length = visual_radius * 1.6).
void make_selection_marker(float center[3], float half_span, Seg3 out2[2]);

// v0.7: GPU timestamp policy. A device only yields genuine GPU frame time when
// the graphics queue family reports timestampValidBits > 0 AND the device
// exposes a valid timestampPeriod (ns per tick). Otherwise the app must print
// "GPU TIMING: NOT AVAILABLE" — never substitute CPU timing.
bool gpu_timing_supported(uint32_t timestamp_valid_bits, float timestamp_period_ns);
// Convert a tick delta to milliseconds (callers guard with gpu_timing_supported).
double gpu_ms_from_ticks(uint64_t tick_delta, float timestamp_period_ns);

} // namespace astra::app
