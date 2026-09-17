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
BodyInstance pack_body_instance(float rx, float ry, float rz, float radius,
                                float cr, float cg, float cb, bool selected);

} // namespace astra::app
