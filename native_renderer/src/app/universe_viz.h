// universe_viz.h — v1.3 visual-integration layer (CPU vertex builders for the
// VERIFIED scientific mirrors). Responsible for turning mirror outputs into
// render-ready polyline segments (vec3 positions in render units, already
// floating-origin relative) + batch metadata (provenance labels + colors).
//
// Provenance contract for every produced geometry (repeated on the functions):
//   SOURCE    = authoritative models astra.destruction / astra.evolution /
//               astra.temporal (Python); numbers come from the NATIVE MIRRORS.
//   AUTHORITY = native mirror execution results (double precision).
//   TRANSFORM = KILOMETERS -> render units via POS_SCALE (k = 100/AU_KM), and
//               a floating-origin subtraction (only at this boundary).
//   GPU       = g_de_vb / g_evo_vb / g_obs_vb mapped host-coherent VBs.
//   SHADER    = bh_shell.vert (v1.2 contract) / u13_host.vert (per-anchor).
//   CLASS     = per-batch classification encoded in flags (SIMULATED /
//               PHYSICALLY-MODELED / THEORETICAL / CINEMATIC constants below).
//
// Deterministic: identical sim state + seeds => identical vertex data. No
// wall-clock influence. No invented dynamics: fragment flight uses the
// authority's UNIFORM (straight-line) velocity vectors from execute_impact —
// the ONLY evolution applied to fragments between impact_event and draw.
#pragma once

#include <string>
#include <vector>

#include "destruction_sim.h"
#include "evolution_sim.h"
#include "observation_sim.h"
#include "celestial_sim.h"

namespace astra::app {

// v1.3 vertex/batch classification tags (UI-facing labels kept ASCII-clean in
// dump text; HUD rows render them verbatim via hud_state.cpp mappings).
constexpr unsigned U13_SIMULATED = 1u << 0;  // derived from mirror of authority
constexpr unsigned U13_PHYSICALLY_MODELED = 1u << 1; // physics-model geometry (BH shells etc.)
constexpr unsigned U13_THEORETICAL = 1u << 2; // theoretical reduced-order model
constexpr unsigned U13_CINEMATIC = 1u << 3;   // readability transform
constexpr unsigned U13_SPECULATIVE = 1u << 4; // exotic/speculative constructs
const char* u13_class_name(unsigned flags);

// SegmentBatch: a LINE_LIST-compatible polyline chunk with one
// visualization label (color + provenance). Positions are world-frame km
// RELATIVE to an anchor the caller picks per batch; the builder writes the
// anchor into `anchor_km` so the renderer may reconstruct target-relative
// units for ANY floating origin.
struct SegmentBatch {
    std::vector<float> pts;    // xyz triples relative to anchor_km
    // Anchor in DOUBLE: it is an ABSOLUTE heliocentric-km coordinate — storing
    // it as float would quantize to 16 km at 1 AU (~512 km at Uranus) and
    // thereby violate the engine rule that single precision appears only at
    // the visualization boundary (pts, which are small re-based offsets).
    double anchor_km[3] = {0, 0, 0}; // anchor of this batch (SI km)
    float color[3] = {1, 1, 1};
    unsigned flags = U13_SIMULATED;
    const char* label = "";    // HUD/debug text (ASCII)
};

// ---------------------------------------------------------------------------
// 1) Destruction impact overlay (visualization of astra.destruction outcomes)
// ---------------------------------------------------------------------------
// Executes ONE deterministic impact between bodies ti/tj at sim time 0 with a
// pinned seed (reproducible); geometry is piecewise fed through POS_SCALE.
struct U13DestructionOverlay {
    bool valid = false;
    DestructionImpactEvent event;
    DestructionImpactResult result;
    DestructionDamageState state_before = DestructionDamageState::INTACT;
    DestructionDamageState state_after = DestructionDamageState::INTACT;
    int target_idx = -1, impactor_idx = -1;
    double pos_scale = 0.0;   // km -> render units (from the caller)
    // Batches: 0=impact point marker (CINEMATIC pulse via lifecycle), 1=trajectory cone
    // (PHYSICALLY_MODELED), 2=fragment cluster (SIMULATED), 3=ejecta ring
    // (SIMULATED), 4=momentum arrow (SIMULATED).
    std::vector<SegmentBatch> batches;
    // Impact time offset from the caller's sim clock (0 = current).
    double impact_sim_time_s = 0.0;
    double t_build_s = 0.0;   // sim time at which the batches were generated
};

// Build the overlay for (target_idx, impactor_idx) seeded impact at the
// CURRENT sim time. `world_km` and `vel_km_s` are the floating-origin world
// state in SI km (mirroring propagate_world outputs); `origin_km` is the
// floating origin (body focus) used to rebase.
// Returns false iff the mirror rejects the event (degenerate separation).
bool u13_build_destruction_overlay(const std::vector<CelestialBody>& bodies,
                                   const std::vector<Vec3d>& world_km,
                                   const std::vector<Vec3d>& vel_km_s,
                                   const Vec3d& origin_km,
                                   int target_idx, int impactor_idx,
                                   double sim_time_s, long long seed,
                                   double pos_scale, U13DestructionOverlay& out);

// Animate the overlay at a NEW sim time: fragments + ejecta follow UNIFORM
// straight-line flight (authority's velocity outputs; NO invented gravity on
// fragments — documented SIMULATED classification). Rewrites batch 2/3 points
// in place. Cheap (kilo-verts).
void u13_advance_destruction_overlay(U13DestructionOverlay& io, double sim_time_s);

// ---------------------------------------------------------------------------
// 2) Evolution trajectory overlay (galaxy + stellar + structure evolution)
// ---------------------------------------------------------------------------
// Mirrors `astra.evolution` defaults:
//   galaxy: sfr_tau=5 Gyr, return_fraction=0.3, yield=0.02
//   stellar: 1.0 Msun, dt per sample 0.05 Gyr (authority engine cadence)
// Vector batches overlay LONG-RANGE trajectories in a figure anchored at the
// central body (the Sun — evolution is *cosmic*, not dynamical around the
// solar barycenter; classification: THEORETICAL/SIMULATED). NOT a dynamical
// simulation of the solar orbit.
struct U13EvolutionOverlay {
    bool valid = false;
    // serialized quantities to HUD ("sfr", "gas", "M*", "Z", "L", "Mbh",
    // "epoch", "t_ms_left", "phase", "filaments", "voids"). Each string is a
    // short formatted line; the renderer shows them in collapsed HUD rows.
    std::vector<std::string> summary_lines;
    double t_max_gyr = 0.0;
    std::vector<double> t_axis_gyr;               // one per sample step
    std::vector<double> sfr_series;               // sfr_msun_per_yr per step
    std::vector<double> luminosity_series;        // L per step
    std::vector<double> age_series_gyr;           // stellar age per step
    std::vector<int> phase_series;                // EvolStellarPhase per step
    // Galaxy "evolution spiral": a time-parameterized 3D track with position
    // (x = log(1+sfr), y = log(1+M*), z = t) all SCALED BY pos_scale into
    // render units (documented CINEMATIC axis scaling, quantities SIMULATED).
    SegmentBatch spiral;                          // TH
    // Stellar lifecycle line: luminosity band strip along the same axis:
    SegmentBatch stellar;                         // SIM
    // Web/void counts (large-scale structure) painted as two polylines:
    SegmentBatch webline;                         // TH
    SegmentBatch voidline;                        // TH
};

// Runs the authority defaults forward from t0=0 to tmax=13.8 Gyr (present).
// 24 samples + final. Fixed seeds do not exist here (no RNG in evolve) so the
// series is EXACTLY deterministic and reproducible — bit-exact vs the Python
// authority for the same arithmetic, verified separately by the checker.
bool u13_build_evolution_overlay(double pos_scale, U13EvolutionOverlay& out);

// ---------------------------------------------------------------------------
// 3) Historical observation halo (temporal light-cone geometry)
// ---------------------------------------------------------------------------
// `bodies` supplies the REAL ephemeris: the subject worldline is sampled by
// the same propagate_world composition the simulation itself uses (Kepler
// elements, parent-relative, heliocentric km) — 65 samples covering
// [now_s - 1.3 * flat-rule lookback (480 s floor), now_s]. NO static-worldline
// placeholder exists here. The emission ring/null path are expressed in
// render units with CINEMATIC spans; emission event + lookback come from the
// temp_observe mirror (SIMULATED, never spoofed).
struct U13ObservationOverlay {
    bool valid = false;
    double lookback_s = 0.0;
    int observer_idx = -1;
    int subject_idx = -1;
    SegmentBatch emission_ring;  // SIMULATED: ring at the emission event
    SegmentBatch null_path;      // SIMULATED: straight-segment light path
    std::string hud_line;        // ASCII snapshot for the console dump
    double t_build_s = 0.0;
};

bool u13_build_observation_overlay(const std::vector<CelestialBody>& bodies,
                                   const Vec3d& origin_km,
                                   int observer_idx, int subject_idx,
                                   double sim_time_s, double pos_scale,
                                   U13ObservationOverlay& out);

// ---------------------------------------------------------------------------
// 4) Galactic halo demonstration (large-scale structure in render space)
// ---------------------------------------------------------------------------
// Generates N=128 representative halo anchors via the AUTHENTIC hierarchy
// mirror: one milky-way halo (authority defaults) advanced at dt steps in
// (1+z) space via the evolution engine, clustered via relations/roles, all
// EXTERNAL to the visual solar system (positions synthesized by the mirror's
// halo_mass/concentration relation + a fixed deterministic placement grid
// from the SAME halo step — not from random scattering). Classification
// THEORETICAL for structure counts; positions CINEMATIC placement grid.
struct U13GalacticOverlay {
    bool valid = false;
    std::vector<std::array<float,3>> halo_units; // render units, planet-frame
    std::vector<int> halo_class_counts;          // TH counts
    SegmentBatch filaments;                      // TH links between halos
    double t_build_s = 0.0;
};

bool u13_build_galactic_overlay(double pos_scale, U13GalacticOverlay& out);

// ---------------------------------------------------------------------------
// 5) Frame draw-plan: EXACT mapping "toggled batches -> packed vertex buffer"
//    consumed byte-for-byte by render_frame (line 2fc upload loop). Keeping it
//    pure/testable makes the geometry->GPU-resource handoff provable off-GPU.
// ---------------------------------------------------------------------------
struct U13DrawItem {
    const SegmentBatch* batch = nullptr; // source (anchor_km + color + flags)
    unsigned first_vertex = 0;           // packed offset in the shared VB (verts)
    unsigned vertex_count = 0;           // 2 verts per line segment
};

// Pack the given (already built) batches into up to `capacity_verts`.
// Order: none skipped until the first batch that does not FIT AT ALL — the
// caller (render loop) drops it AND counts it as a telemetry drop, never a
// partial upload. Returns items recorded.
size_t u13_draw_plan(const std::vector<const SegmentBatch*>& batches,
                     size_t capacity_verts, std::vector<U13DrawItem>& out);

} // namespace astra::app
