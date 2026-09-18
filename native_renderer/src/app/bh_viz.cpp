// Implementation note: every scientific value comes from the native mirrors
// (black_hole_sim / spacetime_sim), computed in DOUBLE, with the documented
// CINEMATIC magnification applied as the final multiplicative transform
// (float cast only at the vertex boundary). No invented physics.

#include "bh_viz.h"
#include "black_hole_sim.h"
#include "spacetime_sim.h"

#include <cmath>

namespace astra::app {

bool bh_viz_overlay_params(double ctr_mass_kg, double ctr_visual_radius_units,
                           BhVizOverlay& out) {
    out = BhVizOverlay{};
    if (std::isnan(ctr_mass_kg) || std::isinf(ctr_mass_kg) || ctr_mass_kg <= 0.0)
        return false;
    BhVizOverlay o;
    double rs = 0.0, isco = 0.0, photon = 0.0;
    if (bh_schwarzschild_radius_m(ctr_mass_kg, rs) != BhErr::OK) return false;
    if (bh_isco_radius(ctr_mass_kg, isco) != BhErr::OK) return false;
    if (bh_photon_sphere_radius(ctr_mass_kg, photon) != BhErr::OK) return false;
    o.rs_m = rs;
    o.photon_m = photon;
    o.isco_m = isco;
    o.anchor_units = BHVIZ_HORIZON_ANCHOR_FACTOR * ctr_visual_radius_units;
    o.magnification = o.anchor_units / rs;

    BlackHoleState s{ctr_mass_kg, 0.0};
    bh_create_black_hole(ctr_mass_kg, 0.0, s);
    // [0] event horizon: dt/dtau diverges AT the horizon; the guard refuses
    // evaluation — dilation_valid = false (HUD shows boundary behavior, never
    // a fabricated value).
    o.shells[0].label = "EVENT HORIZON r_s";
    o.shells[0].scientific_radius_m = rs;
    o.shells[0].visual_radius_units = rs * o.magnification;
    o.shells[0].dilation_valid = false;
    o.shells[0].dilation_minus_one = 0.0;
    o.shells[0].color[0] = 0.9f; o.shells[0].color[1] = 0.25f;
    o.shells[0].color[2] = 0.15f; o.shells[0].color[3] = 1.0f;
    // [1] photon sphere.
    o.shells[1].label = "PHOTON SPHERE 1.5 r_s";
    o.shells[1].scientific_radius_m = photon;
    o.shells[1].visual_radius_units = photon * o.magnification;
    o.shells[1].color[0] = 0.30f; o.shells[1].color[1] = 0.85f;
    o.shells[1].color[2] = 0.95f; o.shells[1].color[3] = 1.0f;
    // [2] ISCO.
    o.shells[2].label = "ISCO 3 r_s";
    o.shells[2].scientific_radius_m = isco;
    o.shells[2].visual_radius_units = isco * o.magnification;
    o.shells[2].color[0] = 1.0f; o.shells[2].color[1] = 0.75f;
    o.shells[2].color[2] = 0.20f; o.shells[2].color[3] = 1.0f;
    for (int i = 1; i < 3; ++i) {
        double d = 0.0;
        o.shells[i].dilation_valid =
            bh_gravitational_time_dilation_schwarzschild(s, o.shells[i].scientific_radius_m, d) == BhErr::OK;
        o.shells[i].dilation_minus_one = o.shells[i].dilation_valid ? d - 1.0 : 0.0;
    }
    o.valid = true;
    out = o;
    return true;
}

void bh_viz_build_rings(const BhVizOverlay& ov, BhVizRings& out) {
    out.points.clear();
    for (int ring = 0; ring < 3; ++ring) {
        const double R = ov.shells[ring].visual_radius_units;
        const uint32_t first = static_cast<uint32_t>(out.points.size());
        for (int i = 0; i <= BHVIZ_RING_SEGMENTS; ++i) {
            const double a = 2.0 * ST_PI * (static_cast<double>(i) / BHVIZ_RING_SEGMENTS);
            out.points.push_back({static_cast<float>(R * std::cos(a)),
                                  static_cast<float>(R * std::sin(a)), 0.0f});
        }
        out.spans[ring] = {first, static_cast<uint32_t>(BHVIZ_RING_SEGMENTS + 1)};
    }
}

bool bh_viz_build_light_rays(double ctr_mass_kg, const BhVizOverlay& ov, BhVizRays& out) {
    out = BhVizRays{};
    if (!ov.valid) return false;
    StMetric sm;
    if (st_schwarzschild_metric(ctr_mass_kg, sm) != StErr::OK) return false;
    const double rs = ov.rs_m;
    const double L = BHVIZ_RAY_EMITTER_RS * rs;
    const double span = BHVIZ_RAY_SPAN_FACTOR * L;

    out.points.clear();
    out.points.reserve(BHVIZ_RAY_COUNT * (BHVIZ_RAY_STEPS + 1));
    for (int ray = 0; ray < BHVIZ_RAY_COUNT; ++ray) {
        const double b = BHVIZ_RAY_IMPACTS_RS[ray] * rs;
        // Virtual photon in the ecliptic plane: emitter at (-L, b, 0) moving +x
        // at exactly c. Massless facade conversion (dλ = dct, affine metres).
        RelVec3 v3{SPEED_OF_LIGHT, 0.0, 0.0};
        double c0[4], u0[4];
        StErr e = st_cartesian_state_to_chart(sm, 0.0, -L, b, 0.0, v3, true, c0, u0);
        if (e != StErr::OK) return false;
        GeodesicSolution sol;
        e = st_integrate_geodesic(sm, c0, u0, span, BHVIZ_RAY_STEPS, false, 1.0e-10, sol);
        if (e != StErr::OK) return false;
        const uint32_t first = static_cast<uint32_t>(out.points.size());
        for (const auto& xq : sol.coordinates) {
            // Spherical chart (r, theta, phi) -> local cartesian metres.
            double x, y, z;
            const StErr ec = st_spherical_to_cartesian(xq[1], xq[2], xq[3], x, y, z);
            if (ec != StErr::OK) return false;
            out.points.push_back({static_cast<float>(x * ov.magnification),
                                  static_cast<float>(y * ov.magnification),
                                  static_cast<float>(z * ov.magnification)});
        }
        out.spans[ray] = {first, static_cast<uint32_t>(sol.coordinates.size())};
    }
    out.valid = true;
    return true;
}

} // namespace astra::app
