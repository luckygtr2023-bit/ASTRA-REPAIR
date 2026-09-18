#pragma once
// ASTRA COSMOS — Black-hole / spacetime VISUALIZATION data builders (v1.2
// visual-integration increment).
//
// CPU-side, pure, deterministic builders that turn the native mirrors of the
// scientific authorities (app/black_hole_sim, app/spacetime_sim) into
// renderer-consumable polylines. Read-only: NOTHING here touches simulation
// or persistence state. The renderer is a pure visualization consumer.
//
// PROVENANCE per visual element (expanded table in ASTRA_V1_2 report):
//   Shell radii (r_s, 1.5 r_s, 3 r_s), ray curves:
//       SOURCE    = values returned by the native mirrors
//       AUTHORITY = astra.blackhole (boundaries) / astra.spacetime (geodesics)
//       TRANSFORM = × magnification (CINEMATIC policy constant; ratios exact)
//       RENDER    = bh_shell.vert / orbit.frag → g_pipe_bh_shell (LINE_STRIP)
//   Classification: PHYSICALLY-MODELED (geometry) with CINEMATIC overall
//   scale; Kerr layers stay NOT AVAILABLE because the engine models the
//   central spin as zero (see HUD rows).

#include <array>
#include <cstdint>
#include <vector>

namespace astra::app {

// One structural shell of the modeled black-hole geometry.
struct BhVizShell {
    const char* label;              // e.g. "EVENT HORIZON r_s"
    double scientific_radius_m;     // exact mirror output (metres)
    double visual_radius_units;     // = scientific * magnification (double math)
    double dilation_minus_one;      // dt/dtau - 1 at shell radius; NaN if invalid
    bool dilation_valid;            // false at the horizon (CoordinateSingularity)
    float color[4];
};

// Policy constants (CINEMATIC readability scaling — documented, fixed).
// Horizon shell anchor: 1.5 x the central body's (already CINEMATIC) visual
// radius, so the structure is visible from the standard follow camera.
inline constexpr double BHVIZ_HORIZON_ANCHOR_FACTOR = 1.5;
inline constexpr int BHVIZ_RING_SEGMENTS = 96;

// Impact parameters of the traced null geodesics, in units of r_s.
// All are OUTSIDE the photon-capture critical value b_crit = (3 sqrt(3)/2) r_s
// ~= 2.598 r_s, so every ray escapes (no HorizonCrossingError by design).
inline constexpr int BHVIZ_RAY_COUNT = 5;
inline constexpr double BHVIZ_RAY_IMPACTS_RS[BHVIZ_RAY_COUNT] = {3.0, 3.75, 4.5, 6.0, 8.0};
// Emitter: z = -25 r_s, direction +z... (ecliptic plane XY adopted: emitter at
// (-L, b, 0) travelling +x); integration span = 2.2 L in affine metres.
inline constexpr double BHVIZ_RAY_EMITTER_RS = 25.0;
inline constexpr double BHVIZ_RAY_SPAN_FACTOR = 2.2;
inline constexpr int BHVIZ_RAY_STEPS = 1000;

struct BhVizOverlay {
    bool valid = false;
    double rs_m = 0.0;
    double photon_m = 0.0;
    double isco_m = 0.0;
    double anchor_units = 0.0;   // horizon anchor (render units) -- CINEMATIC
    double magnification = 0.0;  // render units per metre (= anchor / rs_m)
    BhVizShell shells[3];        // [0]=horizon, [1]=photon sphere, [2]=ISCO
};

// Shell parameters from the AUTHORITY MIRROR (black_hole_sim). False when the
// mirror rejects the mass (then the whole overlay degrades to NOT AVAILABLE).
bool bh_viz_overlay_params(double ctr_mass_kg, double ctr_visual_radius_units,
                           BhVizOverlay& out);

// Ring polylines (ecliptic plane XY), centered at the central body, radius =
// shell visual_radius_units. Appends an index-jump pair between rings? No —
// rings are separate polylines; out.push_ring gives (first, count) per ring.
struct BhVizRings {
    std::vector<std::array<float, 3>> points;       // LINE_STRIP VBs
    std::array<std::pair<uint32_t, uint32_t>, 3> spans;  // (first, count)
};
void bh_viz_build_rings(const BhVizOverlay& ov, BhVizRings& out);

// Null-geodesic light-bending polylines around the central body. Geometry is
// computed IN METRES in the metric's own spherical chart via the native
// spacetime mirror (st_cartesian_state_to_chart + st_integrate_geodesic,
// massless, fixed RK4), converted chart -> cartesian -> magnification-scaled
// render units. Deterministic; returns valid=false if the mirror errors.
struct BhVizRays {
    bool valid = false;
    std::vector<std::array<float, 3>> points;
    std::array<std::pair<uint32_t, uint32_t>, BHVIZ_RAY_COUNT> spans;
};
bool bh_viz_build_light_rays(double ctr_mass_kg, const BhVizOverlay& ov, BhVizRays& out);

} // namespace astra::app
