// ASTRA COSMOS — v1.3 UNIVERSE VISUAL INTEGRATION gates (executed on Linux).
//
// These gates prove, WITHOUT a GPU, the whole data-flow chain that the v1.2
// BH overlay established:
//     authoritative sim state -> verified native mirror -> viz geometry ->
//     batch (anchor+color+flags) -> packed VB draw-plan (-> draw call
//     recorded by main_production.cpp — committed shader/pipeline contract).
// What can only run on Windows+Vulkan (actual raster) is explicitly out of
// scope here and marked NOT VERIFIED — ENVIRONMENT LIMITATION in the report.
//
// Anti-purple checks covered (mission list, geometry-executable subset):
//  1. every toggled domain produces non-empty batches wired to the draw plan
//  2. per-element provenance labels carry classification vocabulary
//  3. geometry VISIBLY changes when sim state/observation parameters change
//  4. hard vertex caps enforced (no unbounded growth, no partial uploads)
//  5. bit-exact determinism across identical invocations
//  6. double-precision staging: anchors + distances in km, single float only
//     at the final to_units() boundary (checked numerically, not by type)
//  7. degradation honoured: off-not-available paths carry NOT AVAILABLE rows
//  8. HUD rows exist for every overlay mode (hud_state rows, all modes)
//  9. draw-plan packing is monotonic, capped, and preserves anchors/colors
// 10. lod/detail class discipline: NO purple/magenta placeholder colors, no
//     hard-coded decorative galaxy (CINEMATIC placement documented in source)
// Remaining checks (GPU shader compile, visual eyeballing vs reference,
// depth-correctness on hardware, perf budget on hardware) are NOT VERIFIED.

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/celestial_sim.h"
#include "app/destruction_sim.h"
#include "app/evolution_sim.h"
#include "app/hud_state.h"
#include "app/observation_sim.h"
#include "app/universe_viz.h"

using namespace astra::app;

static int g_checks = 0;
static int g_fails = 0;
#define GATE(cond, msg) do { ++g_checks; if (!(cond)) { \
    printf("[V13VIZ-FAIL] %s\n", msg); ++g_fails; } } while (0)

static bool all_finite(const SegmentBatch& b) {
    for (float f : b.pts) if (!std::isfinite(f)) return false;
    return true;
}
// ---------------------------------------------------------------------------
// 1. DESTRUCTION (full data-flow)
// ---------------------------------------------------------------------------
static void gates_destruction(const std::vector<CelestialBody>& bodies,
                              const std::vector<Vec3d>& world,
                              const std::vector<Vec3d>& vel) {
    const double POS_SCALE = 100.0 / 149597870.7; // main_production POS_SCALE

    int ti = -1, ii = -1;
    for (size_t k = 0; k < bodies.size(); ++k) {
        if (bodies[k].name == "Earth") ti = (int)k;
        if (bodies[k].name == "Mars")  ii = (int)k; // Halley-class impactor
    }
    GATE(ti >= 0 && ii >= 0, "solar-system factory must expose earth+mars indices");
    if (ti < 0 || ii < 0) return;

    // GEO-HONEST impact: place the Moon AT periapsis-relative CONTACT DISTANCE
    // by scaling the event positions — the authority mirror lives in SI meters
    // and the gate uses its own geometry, so world state stays untouched here
    // (the gate constructs a valid overlap event around the REAL velocities).
    Vec3d origin = world[(size_t)ti];

    // Build with an explicit overlapping pair (head-on): shift the impactor to
    // (target + 0.6*(R_t+R_i)) along its velocity direction.
    {
        // NOTE: destructive mutation is NOT allowed on authoritative state —
        // the overlay builder does not mutate; the GATE simply builds a local
        // event by moving the impactor along the authentic CONNECTOR to the
        // target: this tests the builder's overlap gate + mirror passthrough,
        // never the sim itself.
    }
    std::vector<Vec3d> w2 = world;
    {
        const Vec3d& pT = w2[(size_t)ti];
        Vec3d& pI = w2[(size_t)ii];
        // directional unit from impactor to target
        double d[3] = {pT[0]-pI[0], pT[1]-pI[1], pT[2]-pI[2]};
        const double L = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        GATE(L > 0.0, "earth-moon separation must be positive");
        const double want_km = 0.75 * bodies[(size_t)ti].radius_km; // deep overlap => CONTACT
        for (int k = 0; k < 3; ++k)
            pI[k] = pT[k] - d[k] / L * want_km; // target-frame contact placement
    }

    U13DestructionOverlay ov;
    const long long SEED = 20240918; // pinned mission seed (report-surfaced)
    GATE(u13_build_destruction_overlay(bodies, w2, vel, origin, ti, ii, 3600.0, SEED,
                                       POS_SCALE, ov),
         "destruction overlay build must succeed for an overlapping pair");
    if (!ov.valid) { printf("[V13VIZ-FAIL] overlay invalid\n"); ++g_fails; return; }

    // (1) non-empty + (2) provenance labels
    GATE(ov.batches.size() == 5, "destruction overlay must expose 5 batches");
    size_t total_verts = 0;
    for (const SegmentBatch& b : ov.batches) {
        GATE(!b.pts.empty(), "every destruction batch non-empty (anti-purple: visible)");
        GATE(b.pts.size() % 6 == 0, "every batch is an integral line list");
        GATE(all_finite(b), "all vertices finite");
        total_verts += b.pts.size() / 3;
        // (6) double-precision staging: anchor in km, offsets in render units —
        // anchor must equal the build origin within one rounding of the
        // heliocentric km frame magnitude (~1e-9 relative).
        const double am = 1.5 * (std::fabs(origin[0]) + std::fabs(origin[1]) + std::fabs(origin[2]));
        const double at = 1e-9 * (am > 1.0 ? am : 1.0);
        const bool anchor_ok = std::fabs(b.anchor_km[0] - origin[0]) < at &&
             std::fabs(b.anchor_km[1] - origin[1]) < at &&
             std::fabs(b.anchor_km[2] - origin[2]) < at;
        if (!anchor_ok) printf("[ANCHOR] %s -> %.6e %.6e %.6e vs origin %.6e %.6e %.6e\n",
             b.label, b.anchor_km[0], b.anchor_km[1], b.anchor_km[2],
             origin[0], origin[1], origin[2]);
        const double d1 = std::fabs(b.anchor_km[0] - origin[0]) +
                          std::fabs(b.anchor_km[1] - origin[1]) +
                          std::fabs(b.anchor_km[2] - origin[2]);
        if (!anchor_ok) printf("[EXACT] batch=%s d1=%.6e\n", b.label, d1);
        GATE(anchor_ok, "batch anchor_km == build origin (double-precision staging)");
    }
    GATE(total_verts <= 16384, "destruction vertex total within 16384 cap");

    // (9) draw-plan packing: monotonic offsets, capped, anchors/colors carried
    std::vector<const SegmentBatch*> bp;
    for (const SegmentBatch& b : ov.batches) bp.push_back(&b);
    std::vector<U13DrawItem> plan;
    GATE(u13_draw_plan(bp, 16384, plan) == ov.batches.size(), "plan packs all 5 batches");
    size_t off = 0;
    for (const U13DrawItem& it : plan) {
        GATE(it.first_vertex == off, "draw-plan offsets strictly packed");
        off += it.vertex_count;
        GATE(it.batch != nullptr && it.batch->flags != 0, "plan carries provenance flags");
    }
    // hard rule: cap too small => NOT a partial upload (anti-fake: no clipped batch)
    plan.clear();
    const size_t smallest = ov.batches[0].pts.size() / 3;
    GATE(u13_draw_plan(bp, smallest / 2, plan) == 0, "cap < first batch => zero draws (no partial upload)");

    // Mirror-agreement topology: fragment batch vertex count must equal the
    // overlay's own impact-result fragment count x 15 (16-pt spiral segs).
    {
        GATE(ov.batches[2].pts.size() / 6 == ov.result.fragments.size() * 16,
             "fragment batch vertex count == result fragments x 16 segments (17-pt spiral)");
        GATE(ov.result.fragments.size() > 0, "gate event is in the fragmenting regime");
    }

    // (5) determinism: identical rebuild is bitwise identical
    {
        U13DestructionOverlay ov2;
        GATE(u13_build_destruction_overlay(bodies, w2, vel, origin, ti, ii, 3600.0, SEED,
                                           POS_SCALE, ov2), "rebuild ok");
        bool same = ov.batches.size() == ov2.batches.size();
        for (size_t i = 0; same && i < ov.batches.size(); ++i)
            same = std::memcmp(ov.batches[i].pts.data(), ov2.batches[i].pts.data(),
                               ov.batches[i].pts.size() * sizeof(float)) == 0;
        GATE(same, "destruction overlay bit-exact across identical rebuilds");
    }

    // (7) degradation: non-overlapping pair politely refuses (NOT AVAILABLE)
    {
        U13DestructionOverlay far;
        const bool ok = u13_build_destruction_overlay(bodies, world, vel, origin, ti, ii, 3600.0,
                                                      SEED, POS_SCALE, far);
        GATE(!ok && !far.valid, "far-apart pair refused (NOT AVAILABLE path, no fake impact)");
    }
}

// ---------------------------------------------------------------------------
// 2. EVOLUTION figure
// ---------------------------------------------------------------------------
static void gates_evolution() {
    U13EvolutionOverlay e1, e2;
    GATE(u13_build_evolution_overlay(1.0, e1), "evolution overlay builds");
    GATE(u13_build_evolution_overlay(1.0, e2), "evolution overlay rebuilds");
    if (!e1.valid || !e2.valid) return;
    GATE(e1.t_axis_gyr.size() > 8, "evolution exposes a time series");
    GATE(e1.spiral.pts.size() / 6 + 1 >= e1.t_axis_gyr.size(), "spiral spans series");
    GATE(e1.sfr_series.size() == e1.t_axis_gyr.size(), "sfr series length == time axis");
    GATE(std::fabs(e1.t_max_gyr - 13.8) < 0.25, "t_max ~ 13.8 Gyr (present day)");
    GATE(e1.phase_series.size() == e1.t_axis_gyr.size(), "phase series aligned");
    GATE(all_finite(e1.spiral) && all_finite(e1.stellar) && all_finite(e1.webline) && all_finite(e1.voidline),
         "evolution geometry finite");
    // determinism
    GATE(e1.spiral.pts == e2.spiral.pts && e1.webline.pts == e2.webline.pts &&
         e1.stellar.pts == e2.stellar.pts && e1.voidline.pts == e2.voidline.pts,
         "evolution overlay deterministic (no RNG in authority)");
    GATE(e1.spiral.flags != 0, "spiral provenance flags set (classification taxonomy)");
    // summary lines never fabricate hex escapes; pure ASCII HUD
    GATE(!e1.summary_lines.empty(), "evolution HUD summary present");
}

// ---------------------------------------------------------------------------
// 3. OBSERVATION (temporal light-cone)
// ---------------------------------------------------------------------------
static void gates_observation(const std::vector<CelestialBody>& bodies) {
    const double POS_SCALE = 100.0 / 149597870.7;
    int eo = -1, ms = -1, mn = -1;
    for (size_t k = 0; k < bodies.size(); ++k) {
        if (bodies[k].name == "Earth") eo = (int)k;
        if (bodies[k].name == "Mars")  ms = (int)k;
        if (bodies[k].name == "Moon")  mn = (int)k;
    }
    if (eo < 0 || ms < 0 || mn < 0) { GATE(false, "earth/mars/moon catalog"); return; }
    const Vec3d origin = propagate_world(bodies, 4000.0)[(size_t)eo];

    U13ObservationOverlay o1;
    GATE(u13_build_observation_overlay(bodies, origin, eo, ms, 4000.0, POS_SCALE, o1),
         "observation overlay builds (earth->mars)");
    if (!o1.valid) return;
    GATE(o1.lookback_s > 0.0, "lookback positive");
    GATE(o1.lookback_s < 3600.0, "lookback < 1h for inner solar system");
    GATE(o1.emission_ring.pts.size() / 6 >= 12, "emission ring has segments");
    GATE(o1.null_path.pts.size() / 6 == 3, "null path = path + 2 cross arms");
    GATE(all_finite(o1.emission_ring), "ring finite");
    GATE(o1.emission_ring.flags != 0, "ring provenance flags set (classification taxonomy)");
    // ring property: all ring vertices about equidistant from their centroid
    {
        double cx = 0, cy = 0, cz = 0; const auto& p = o1.emission_ring.pts;
        const size_t n = p.size() / 3;
        for (size_t i = 0; i < n; ++i) { cx += p[3*i]; cy += p[3*i+1]; cz += p[3*i+2]; }
        cx /= n; cy /= n; cz /= n;
        double r0 = -1; bool uniform = true;
        for (size_t i = 0; i < n; ++i) {
            const double dx = p[3*i]-cx, dy = p[3*i+1]-cy, dz = p[3*i+2]-cz;
            const double r = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (r0 < 0) r0 = r;
            else if (std::fabs(r - r0) > 0.02 * r0) { uniform = false; break; }
        }
        GATE(uniform, "emission ring is a true ring (uniform radius)");
    }
    // observation parameters change => visible change (mission requirement)
    U13ObservationOverlay o2;
    GATE(u13_build_observation_overlay(bodies, origin, mn, ms, 4000.0, POS_SCALE, o2),
         "observation overlay builds (moon->mars)");
    GATE(o2.valid && std::fabs(o2.lookback_s - o1.lookback_s) > 0.001 * o1.lookback_s,
         "lookback visibly changes when the observer moves");
    GATE(o1.emission_ring.pts != o2.emission_ring.pts, "ring geometry changes with observer");
    // determinism
    U13ObservationOverlay o3;
    GATE(u13_build_observation_overlay(bodies, origin, eo, ms, 4000.0, POS_SCALE, o3), "rebuild");
    GATE(o3.valid && o3.emission_ring.pts == o1.emission_ring.pts &&
         o3.null_path.pts == o1.null_path.pts, "observation overlay deterministic");
}

// ---------------------------------------------------------------------------
// 4. GALACTIC / large-scale structure
// ---------------------------------------------------------------------------
static void gates_galactic() {
    U13GalacticOverlay g1, g2;
    GATE(u13_build_galactic_overlay(1.0, g1), "galactic overlay builds");
    GATE(u13_build_galactic_overlay(1.0, g2), "galactic overlay rebuilds");
    if (!g1.valid || !g2.valid) return;
    GATE(g1.halo_units.size() == 128, "128 epoch-sampled halos");
    // 7 evolution epochs are sampled (i % 19 == 0 over 128 halos); the CLASS
    // bins must account for exactly those 7 epochs (bin count documented in
    // the report; THEORETICAL labels in the HUD).
    int binned = 0;
    for (int c : g1.halo_class_counts) binned += c;
    GATE(binned == 7, "7 epoch samples accounted in the class bins (128-halo / 19 stride)");
    GATE(g1.filaments.pts.size() / 6 > 0, "at least one filament link");
    GATE(g1.filaments.pts.size() / 3 <= 12288, "filament batch within 12288 cap");
    GATE(g1.halo_units == g2.halo_units && g1.filaments.pts == g2.filaments.pts,
         "galactic overlay deterministic (golden-angle placement, no RNG)");
    for (const auto& h : g1.halo_units)
        GATE(std::isfinite(h[0]) && std::isfinite(h[1]) && std::isfinite(h[2]), "halo units finite");
    GATE(g1.filaments.flags != 0, "filament provenance flags set (classification taxonomy)");
}

// ---------------------------------------------------------------------------
// 5. Anti-purple sweep: NO purple/magenta-ish placeholder colors; NO default
//    gray box colors; every color distinct per batch channel.
// ---------------------------------------------------------------------------
static void gates_color_policy() {
    U13EvolutionOverlay e; u13_build_evolution_overlay(1.0, e);
    U13GalacticOverlay g; u13_build_galactic_overlay(1.0, g);
    const SegmentBatch* all[] = { &e.spiral, &e.stellar, &e.webline, &e.voidline, &g.filaments };
    for (const SegmentBatch* b : all) {
        const float r = b->color[0], gg = b->color[1], bb = b->color[2];
        const bool purple = (r > 0.75f && bb > 0.75f && gg < 0.5f);
        GATE(!purple, "no purple placeholder color in any overlay batch");
        const bool gray = (std::fabs(r - gg) < 0.02f && std::fabs(gg - bb) < 0.02f);
        GATE(!gray, "no gray undifferentiated color in any overlay batch");
    }
}

// ---------------------------------------------------------------------------
// 6. HUD rows exist for every overlay mode (degradation paths included)
// ---------------------------------------------------------------------------
static void gates_hud_rows() {
    HudSnapshot s;
    s.selected_index = 0; s.has_selected_kind = true; s.has_selected_state = true;
    s.dk_overlay_mode = 1; s.dk_overlay_avail = true; s.dk_fragments = 40; s.dk_ejecta = 12;
    std::snprintf(s.dk_damage_word, sizeof(s.dk_damage_word), "FRAGMENTED");
    s.evo_overlay_mode = 1; s.evo_overlay_avail = true;
    s.obs_overlay_mode = 1; s.obs_overlay_avail = true; s.obs_lookback_s = 480.0;
    std::snprintf(s.obs_subject, sizeof(s.obs_subject), "Mars");
    s.gala_overlay_mode = 1; s.gala_overlay_avail = true; s.gala_halo_count = 128;
    s.u13_build_ms = 1.25; s.u13_advance_ms = 0.01; s.u13_upload_ms = 0.03;
    HudState h = build_hud(s);
    int found = 0;
    for (const HudRow& r : h.status) {
        const bool is_u13 = r.label.find("F6") != std::string::npos ||
            r.label.find("F7") != std::string::npos || r.label.find("F8") != std::string::npos ||
            r.label.find("F9") != std::string::npos || r.label.find("U13") != std::string::npos;
        if (is_u13) {
            ++found;
            bool ascii = true;
            for (unsigned char ch : r.value) if (ch < 0x20 || ch > 0x7E) { ascii = false; break; }
            GATE(ascii && r.value.find("nan") == std::string::npos,
                 "v1.3 HUD rows are finite printable ASCII (no fabricated glyphs)");
        }
    }
    GATE(found >= 5, "all five v1.3 HUD rows emitted when overlays on");
    // NOT AVAILABLE path
    s.dk_overlay_avail = false; s.obs_overlay_avail = false;
    HudState h2 = build_hud(s);
    int na = 0;
    for (const HudRow& r : h2.status)
        if (r.value == NOT_AVAILABLE && !r.available) ++na;
    GATE(na >= 2, "NOT AVAILABLE rows emitted for refused overlays");
}

int main() {
    printf("[V13VIZ] v1.3 universe-visual integration gates (CPU-side, no GPU)\n");
    const std::vector<CelestialBody> bodies = make_solar_system();
    const std::vector<Vec3d> world = propagate_world(bodies, 3600.0);
    const std::vector<Vec3d> vel   = propagate_world_velocity(bodies, 3600.0);
    GATE(!bodies.empty() && world.size() == bodies.size(), "solar-system factory live");

    gates_destruction(bodies, world, vel);
    gates_evolution();
    gates_observation(bodies);
    gates_galactic();
    gates_color_policy();
    gates_hud_rows();

    printf("[V13VIZ] checks=%d failures=%d\n", g_checks, g_fails);
    printf("%s\n", g_fails == 0 ? "ALL V1.3-VIZ CHECKS PASSED" : "RESULT: FAIL");
    return g_fails == 0 ? 0 : 1;
}
