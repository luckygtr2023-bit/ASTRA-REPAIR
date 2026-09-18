// ASTRA COSMOS v1.1 gates — relativity mirror edge cases, identities, domain
// validation, taxonomy. Cross-language bit-fidelity itself is gated by
// relativity_mirror_check (656 comparisons exactly); these are the semantic
// gates (mission Phase 4 + Phase 11 items not covered by row matching).
#include "app/relativity_sim.h"
#include "app/hud_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra::app;
static const double C = SPEED_OF_LIGHT;

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };
    double o = 0.0;

    // ---- A. Core scalars: domain and identities ----
    gate("core scalars");
    {
        CHECK(lorentz_factor(0.0, o) == RelErr::OK && o == 1.0, "gamma(0)=1 exactly");
        CHECK(beta(0.0, o) == RelErr::OK && o == 0.0, "beta(0)=0");
        // Taylor branch: for beta < 1e-5, gamma-1 == 0.5 b^2 + 0.375 b^4 term-exact.
        const double v = 1200.0;  // b = 4.0034e-6, Taylor branch
        CHECK(gamma_minus_one(v, o) == RelErr::OK, "gm1 ok");
        {
            const double b = v / C, b2 = b * b;
            CHECK(o == 0.5 * b2 + 0.375 * (b2 * b2), "series term-exact");
        }
        total += 4;
        // Known closed-form value: gamma(0.8c) = 5/3.
        CHECK(lorentz_factor(0.8 * C, o) == RelErr::OK, "gamma 0.8c ok");
        CHECK(std::fabs(o - 5.0 / 3.0) / (5.0 / 3.0) < 1e-15, "gamma(0.8c)=5/3");
        total += 2;
        // AUTHORITY DOMAIN QUIRK (v1.1 finding, root-caused + documented):
        // for negative scalar speeds the authority takes the low-beta SERIES
        // branch (b < 1e-5 is true for any b < 0). The mirror must NOT
        // silently "fix" this to |v| (that would fabricate a deviation from
        // the authority); production always passes speed MAGNITUDES (>= 0).
        // Gate asserts exact series-branch arithmetic = authority behavior.
        double gneg;
        lorentz_factor(-0.9 * C, gneg);
        {
            // Compute the series the way the function does (b = (-0.9*C)/C in
            // doubles, NOT the real literal -0.9): expected = authority bits.
            double b; beta(-0.9 * C, b);
            const double b2 = b * b;
            CHECK(gneg == 1.0 + 0.5 * b2 + 0.375 * (b2 * b2),
                  "neg scalar = authority series branch (quirk mirrored)");
        }
        ++total;
        // Hard domain: v >= c -> LIGHT_SPEED_VIOLATION, never a clamped result.
        CHECK(lorentz_factor(C, o) == RelErr::LIGHT_SPEED_VIOLATION, "v=c rejected");
        CHECK(lorentz_factor(1.0000000001 * C, o) == RelErr::LIGHT_SPEED_VIOLATION, "v>c rejected");
        CHECK(lorentz_factor(NAN, o) == RelErr::INVALID_VELOCITY, "NaN v");
        CHECK(lorentz_factor(INFINITY, o) == RelErr::INVALID_VELOCITY, "inf v");
        CHECK(gamma_minus_one(C, o) == RelErr::LIGHT_SPEED_VIOLATION, "gm1 v=c rejected");
        total += 5;
    }

    // ---- B. Energies/momentum: low-v convergence and values ----
    gate("energies & momentum");
    {
        const double m = 2.5;
        CHECK(kinetic_energy(m, 0.0, o) == RelErr::OK && o == 0.0, "KE(0)=0");
        CHECK(total_energy(m, 0.0, o) == RelErr::OK && o == m * C_SQUARED, "E(0)=mc^2");
        total += 2;
        // Classical convergence: KE / (1/2 m v^2) -> 1 at low v (series makes it
        // cancellation-free: ratio = 1 + 0.75 b^2 exactly at Taylor depths).
        const double v = 100.0;
        CHECK(kinetic_energy(m, v, o) == RelErr::OK, "KE ok");
        const double ratio = o / (0.5 * m * v * v);
        const double b = v / C;
        CHECK(std::fabs(ratio - (1.0 + 0.75 * b * b)) / (1.0 + 0.75 * b * b) < 1e-6,
              "KE classical limit");
        total += 2;
        // Invalid rest mass taxonomy.
        CHECK(kinetic_energy(-1.0, v, o) == RelErr::INVALID_REST_MASS, "neg m0");
        CHECK(total_energy(NAN, v, o) == RelErr::INVALID_REST_MASS, "nan m0");
        CHECK(kinetic_energy(0.0, 0.9 * C, o) == RelErr::OK && o == 0.0, "m0=0 fine");
        total += 3;
        // Momentum: low-v -> m v; component signs; v>=c hard error.
        RelVec3 p;
        CHECK(relativistic_momentum(3.0, RelVec3{1e4, -2e4, 3e4}, p) == RelErr::OK,
              "p ok");
        CHECK(p.y < 0.0 && p.z > 0.0, "p component signs");
        total += 2;
        // Classical limit checked where it HOLDS to double precision (v=10 m/s,
        // gamma-1 = 5.6e-16), and the relativistic correction itself verified
        // where it becomes visible (v=1e4 m/s: corr = 0.5 b^2, gated <=1e-6:
        // first-fail root cause was a 1e-13 tolerance at a speed where
        // relativity is genuinely visible at 7.8e-9).
        RelVec3 p10;
        CHECK(relativistic_momentum(3.0, RelVec3{10.0, 0.0, 0.0}, p10) == RelErr::OK,
              "p10 ok");
        CHECK(std::fabs(p10.x - 3.0 * 10.0) / (3.0 * 10.0) < 1e-14, "p classical 10m/s");
        const double corr = p.x / (3.0 * 1e4) - 1.0;
        const double bv = relvec_magnitude(RelVec3{1e4, -2e4, 3e4}) / C;
        CHECK(std::fabs(corr / (0.5 * bv * bv) - 1.0) < 1e-6, "p relativistic corr");
        total += 3;
        RelVec3 badv{C * 1.2, 0.0, 0.0};
        CHECK(relativistic_momentum(1.0, badv, p) == RelErr::LIGHT_SPEED_VIOLATION,
              "vector v>c rejected");
        ++total;
        // Known value: 0.6c -> gamma = 1.25 exactly (0.6c closed branch).
        CHECK(relativistic_momentum(2.0, RelVec3{0.6 * C, 0.0, 0.0}, p) == RelErr::OK, "p 0.6c");
        CHECK(std::fabs(p.x - 1.25 * 2.0 * 0.6 * C) / (1.25 * 2.0 * 0.6 * C) < 1e-15,
              "p = 1.25 m v at 0.6c");
        total += 2;
    }

    // ---- C. Four-vectors & proper time ----
    gate("four-vectors");
    {
        const RelFourVector t0{0, 0, 0, 0};
        CHECK(t0.invariant_sq() == 0.0 && t0.interval_type() == IntervalType::NULLI,
              "zero vector NULL");
        CHECK((RelFourVector{10, 5, 0, 0}.interval_type()) == IntervalType::TIMELIKE, "timelike");
        CHECK((RelFourVector{5, 12, 0, 0}.interval_type()) == IntervalType::SPACELIKE, "spacelike");
        // Tolerance band: |ds2| <= 1e-9 -> NULL.
        CHECK((RelFourVector{1.0, 1.0 - 2e-10, 0, 0}.interval_type()) == IntervalType::NULLI,
              "tolerance band NULL");
        total += 4;
        const RelFourVector e0 = event_from_coordinates(1.0, 2.0, 3.0, 4.0);
        CHECK(e0.t == 1.0 * C && e0.x == 2.0 && e0.y == 3.0 && e0.z == 4.0, "event ct");
        ++total;
        double dt_s = 0;
        CHECK(proper_time_between(event_from_coordinates(0, 0, 0, 0),
                                  event_from_coordinates(1.0, 0, 0, 0), dt_s) == RelErr::OK,
              "proper time ok");
        CHECK(std::fabs(dt_s - 1.0) < 1e-15, "1 s at rest -> 1 s (rel)");
        CHECK(proper_time_between(event_from_coordinates(0, 0, 0, 0),
                                  event_from_coordinates(1.0, C, 0, 0), dt_s) == RelErr::OK &&
              dt_s == 0.0, "null -> 0");
        CHECK(proper_time_between(event_from_coordinates(0, 0, 0, 0),
                                  event_from_coordinates(1.0, 4.0e8, 0, 0), dt_s) ==
                  RelErr::SPACELIKE_INTERVAL, "spacelike -> error");
        total += 4;
    }

    // ---- D. Lorentz boosts ----
    gate("boosts");
    {
        const RelFourVector f{10.0, 4.0, 1.5, -2.0};
        RelFourVector b;
        CHECK(boost_x(f, 0.0, b) == RelErr::OK && b.t == f.t && b.x == f.x &&
              b.y == f.y && b.z == f.z, "v=0 identity");
        CHECK(boost_x(f, 0.5 * C, b) == RelErr::OK, "boost ok");
        CHECK(fabs(b.invariant_sq() - f.invariant_sq()) /
                  fabs(f.invariant_sq()) < 1e-14, "invariant preserved");
        CHECK(boost_x(f, C, b) == RelErr::LIGHT_SPEED_VIOLATION, "boost v=c rejected");
        total += 3;
        // Round trip: boost(v) then inverse_boost(v) -> original (rounded).
        RelFourVector f2, f3;
        boost_x(f, 0.77 * C, f2);
        inverse_boost_x(f2, 0.77 * C, f3);
        CHECK(fabs(f3.t - f.t) / fabs(f.t) < 1e-14 && fabs(f3.x - f.x) / fabs(f.x) < 1e-14 &&
              f3.y == f.y && f3.z == f.z, "inverse roundtrip");
        ++total;
        // Opposite-sign boosts swap the cross-term sign: average of the two
        // transformed t components recovers gamma * t.
        RelFourVector bp, bn;
        boost_x(RelFourVector{1.0, 2.0, 0.0, 0.0}, 0.5 * C, bp);
        boost_x(RelFourVector{1.0, 2.0, 0.0, 0.0}, -0.5 * C, bn);
        double gamma_half = 0.0;
        lorentz_factor(0.5 * C, gamma_half);
        CHECK(fabs((bp.t + bn.t) / 2.0 - gamma_half * 1.0) / (gamma_half * 1.0) < 1e-14,
              "sign symmetry");
        ++total;
    }

    // ---- E. Schwarzschild exterior ----
    gate("gr foundations");
    {
        CHECK(schwarzschild_radius(0.0, o) == RelErr::OK && o == 0.0, "rs(0)=0");
        CHECK(schwarzschild_radius(-1.0, o) == RelErr::DEGENERATE_METRIC, "rs neg m");
        CHECK(schwarzschild_radius(INFINITY, o) == RelErr::DEGENERATE_METRIC, "rs inf m");
        CHECK(schwarzschild_radius(1.9885e30, o) == RelErr::OK, "rs sun ok");
        CHECK(o > 2950.0 && o < 2957.0, "rs sun ~2953 m (DATA-DERIVED range)");
        total += 5;
        const double rs_sun = o;
        CHECK(weak_field_time_dilation(1.9885e30, rs_sun, o) == RelErr::DEGENERATE_METRIC,
              "r=rs rejected");
        CHECK(weak_field_time_dilation(1.9885e30, 0.0, o) == RelErr::DEGENERATE_METRIC,
              "r=0 rejected");
        CHECK(weak_field_time_dilation(1.9885e30, -1.0, o) == RelErr::DEGENERATE_METRIC,
              "r<0 rejected");
        CHECK(weak_field_time_dilation(-1.0, 1e10, o) == RelErr::DEGENERATE_METRIC,
              "neg mass rejected");
        total += 4;
        // Far-field: value = 1 + rs/(2r) to first order; gate within 1%.
        CHECK(weak_field_time_dilation(1.9885e30, 1.496e11, o) == RelErr::OK, "earth orbit ok");
        const double approx1 = 1.0 + rs_sun / (2.0 * 1.496e11);
        CHECK(std::fabs(o - approx1) / approx1 < 1e-2, "far-field first order");
        total += 2;
        // Deep exterior (just above horizon): finite, huge — never inf/NaN.
        CHECK(weak_field_time_dilation(1.9885e30, rs_sun * 1.000001, o) == RelErr::OK &&
              std::isfinite(o) && o > 100.0, "near-horizon huge finite");
        ++total;
    }

    // ---- F. Taxonomy + derived rate + determinism ----
    gate("taxonomy & determinism");
    {
        CHECK(std::strcmp(rel_err_name(RelErr::OK), "OK") == 0, "name ok");
        CHECK(std::strcmp(rel_err_name(RelErr::LIGHT_SPEED_VIOLATION), "LightSpeedViolation") == 0,
              "lsc name");
        CHECK(std::strcmp(relativity_model_name(RelativityModel::WEAK_FIELD), "WEAK_FIELD") == 0,
              "model name");
        total += 3;
        CHECK(proper_time_rate(0.9 * C, o) == RelErr::OK && o > 0.0 && o <= 1.0, "rate range");
        {
            double g;
            lorentz_factor(0.9 * C, g);
            CHECK(std::fabs(o * g - 1.0) < 1e-14, "rate = 1/gamma");
        }
        CHECK(proper_time_rate(C, o) == RelErr::LIGHT_SPEED_VIOLATION, "rate v=c rejected");
        total += 3;
        // Strict determinism: 5 calls bit-identical outputs.
        double ref = 0; bool all = true;
        for (int k = 0; k < 5; ++k) {
            double g; lorentz_factor(0.87654 * C, g);
            if (k == 0) ref = g; all = all && (g == ref);
        }
        CHECK(all, "deterministic repeat");
        ++total;
    }

    // ---- G. HUD relativity rows (mission Phase 7) ----
    gate("hud relativity rows");
    {
        // No selection: both rows NOT AVAILABLE (never fabricated).
        HudSnapshot snap;
        snap.selected_index = -1;
        HudState h = build_hud(snap);
        int sr = 0, gr = 0;
        for (const HudRow& r : h.selection) {
            if (r.label == "SR GAMMA-1") { ++sr; CHECK(!r.available, "SR NA w/o state"); }
            if (r.label == "GRAV DIL-1") { ++gr; CHECK(!r.available, "GRAV NA w/o state"); }
        }
        total += 2;
        // Selection WITH state and relativity data: PHYSICALLY-MODELED rows.
        snap.selected_index = 3;
        snap.selected_name = "Earth";
        snap.has_selected_kind = true;
        snap.selected_kind = "PLANET";
        snap.selected_classification = "X";
        snap.has_selected_state = true;
        snap.r_helio_km = 1.47e8;
        snap.speed_km_s = 30.0;
        snap.has_sel_sr = true;
        snap.sel_gamma_minus_one = 1.892e-4;
        snap.has_sel_grav = true;
        snap.sel_grav_dilation_minus_one = 1.0e-8;
        h = build_hud(snap);
        sr = 0; gr = 0;
        for (const HudRow& r : h.selection) {
            if (r.label == "SR GAMMA-1") {
                ++sr;
                CHECK(r.available, "SR present");
                CHECK(std::string(r.classification).find("PHYSICALLY-MODELED") != std::string::npos,
                      "SR classified");
            }
            if (r.label == "GRAV DIL-1") {
                ++gr;
                CHECK(r.available, "GRAV present");
                CHECK(std::string(r.classification).find("weak-field") != std::string::npos,
                      "GRAV weak-field labeled");
            }
        }
        CHECK(sr == 1 && gr == 1, "rows singular");
        total += 5;
        // Per-flag independence (Sun case: SR valid, GRAV invalid domain).
        snap.has_sel_sr = true;
        snap.has_sel_grav = false;
        h = build_hud(snap);
        for (const HudRow& r : h.selection) {
            if (r.label == "SR GAMMA-1") CHECK(r.available, "SR stays real");
            if (r.label == "GRAV DIL-1") CHECK(!r.available, "GRAV honest NA");
        }
        total += 2;
    }

    std::printf("v10_gates: %d checks, %d failures\n", total, g_fail);
    if (g_fail == 0) { std::printf("v10_gates: ALL %d checks PASSED\n", total); return 0; }
    return 1;
}
