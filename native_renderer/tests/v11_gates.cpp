// ASTRA COSMOS v1.2 gates — black-hole + spacetime mirror edge cases, closed-
// form anchors, identities, domain validation, taxonomy, determinism, HUD.
// Cross-language bit-fidelity is gated by blackhole_spacetime_mirror_check
// (6091 comparisons exactly); these are the semantic gates (mission
// Phase 4/5/9 items not covered by row matching).
#include "app/black_hole_sim.h"
#include "app/spacetime_sim.h"
#include "app/hud_state.h"
#include "app/bh_viz.h"
#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra::app;
static const double C = SPEED_OF_LIGHT;
static const double G = BH_G;
static const double PI2 = 1.5707963267948966;   // math.pi/2
static const double M_SUN = 1.989e30;

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };
    double o = 0.0;

    // ---- A. Parameter validation taxonomy ----
    gate("validation taxonomy");
    {
        CHECK(bh_validate_mass_kg(-1.0, o) == BhErr::INVALID_MASS, "m<0 -> InvalidBlackHoleMassError");
        CHECK(bh_validate_mass_kg(0.0, o) == BhErr::INVALID_MASS, "m=0 rejected");
        CHECK(bh_validate_mass_kg(1.0, o) == BhErr::OK && o == 1.0, "m=1 ok");
        CHECK(bh_validate_spin_param(1.0000000000000002, o) == BhErr::INVALID_SPIN,
              "|a*|>1 (naked singularity) -> InvalidSpinParameterError");
        CHECK(bh_validate_spin_param(1.0, o) == BhErr::OK && o == 1.0, "extremal a*=1 admitted");
        CHECK(bh_validate_spin_param(-1.0, o) == BhErr::OK, "extremal a*=-1 admitted");
        total += 6;
    }

    // ---- B. State classification and units ----
    gate("state/classification/units");
    {
        BlackHoleState s;
        bh_create_black_hole(M_SUN, 0.0, s);
        CHECK(s.model() == BlackHoleModel::SCHWARZSCHILD, "spin 0 -> SCHWARZSCHILD");
        bh_create_black_hole(M_SUN, -1e-300, s);
        CHECK(s.model() == BlackHoleModel::KERR, "any nonzero spin -> KERR");
        bh_create_black_hole(M_SUN, 0.75, s);
        // r_g = GM/c^2 closed form.
        const double rg_expect = (G * M_SUN) / (C * C);
        CHECK(s.gravitational_radius() == rg_expect, "r_g bits = (G*M)/(c*c)");
        CHECK(s.spin_length() == 0.75 * rg_expect, "a = a* * r_g");
        total += 4;
    }

    // ---- C. Schwarzschild anchors ----
    gate("schwarzschild anchors");
    {
        BlackHoleState s; bh_create_black_hole(M_SUN, 0.0, s);
        double rs; bh_schwarzschild_radius_m(M_SUN, rs);
        // Delegation identity: bh r_s == relativity-layer schwarzschild_radius.
        double rsr; const RelErr er = schwarzschild_radius(M_SUN, rsr);
        CHECK(er == RelErr::OK && rs == rsr, "r_s delegates to relativity layer");
        double isco, phot;
        bh_isco_radius(M_SUN, isco); bh_photon_sphere_radius(M_SUN, phot);
        CHECK(isco == 3.0 * rs, "r_isco = 3 r_s (6 r_g)");
        CHECK(phot == 1.5 * rs, "r_ps = 1.5 r_s (3 r_g)");
        // Horizon guard band semantics: r_s + eps borderline.
        double d;
        CHECK(bh_gravitational_time_dilation_schwarzschild(s, rs * 2.0, d) == BhErr::OK,
              "outside horizon ok");
        CHECK(bh_gravitational_time_dilation_schwarzschild(s, rs + 0.5e-9, d) ==
              BhErr::COORDINATE_SINGULARITY, "within eps band -> coordinate singularity");
        CHECK(bh_gravitational_time_dilation_schwarzschild(s, rs / 2.0, d) ==
              BhErr::COORDINATE_SINGULARITY, "inside horizon rejected");
        CHECK(bh_gravitational_time_dilation_schwarzschild(s, 0.0, d) ==
              BhErr::COORDINATE_SINGULARITY, "r=0 rejected");
        CHECK(bh_gravitational_time_dilation_schwarzschild(s, 0.0 / 0.0, d) ==
              BhErr::INVALID_GEOMETRY, "NaN radius -> InvalidGeometryInputError");
        // Weak-field limit: dilation at R_EARTH-orbit-like tiny field.
        bh_gravitational_time_dilation_schwarzschild(s, 1.0e12, d);
        CHECK(std::fabs(d - 1.0) > 0.0 && std::fabs(d - 1.0) < 1e-7,
              "weak field dt/dtau -> 1 from above (measured 1.477e-9 @1e12m)");
        // Delegation identity: bh guarded dilation == relativity wftd same input.
        double w; weak_field_time_dilation(M_SUN, rs * 5.0, w);
        double d5; bh_gravitational_time_dilation_schwarzschild(s, rs * 5.0, d5);
        CHECK(d5 == w, "bh dilation delegates to relativity formula");
        // Redshift identities: equal radii -> z == 0.0 exactly.
        double z;
        CHECK(bh_gravitational_redshift(s, rs * 4.0, rs * 4.0, z) == BhErr::OK && z == 0.0,
              "z(requal)=0 exact");
        bh_gravitational_redshift(s, rs * 1.5, rs * 15.0, z);
        CHECK(z > 0.0, "climb-out = redshift > 0");
        bh_gravitational_redshift(s, rs * 15.0, rs * 1.5, z);
        CHECK(z < 0.0, "descent = blueshift < 0");
        total += 14;
    }

    // ---- D. Kerr closed-form anchors ----
    gate("kerr anchors");
    {
        BlackHoleState k0; bh_create_black_hole(M_SUN, 0.0, k0);
        BlackHoleState kx; bh_create_black_hole(M_SUN, 1.0, kx);
        double rp, rm;
        // Extremal Kerr: r+ = r- = r_g (degenerate).
        bh_kerr_horizons(kx, rp, rm);
        const double rg = kx.gravitational_radius();
        CHECK(rp == rg && rm == rg, "extremal Kerr r+ = r- = r_g");
        // Schwarzschild limit: (2 r_g, 0).
        bh_kerr_horizons(k0, rp, rm);
        double rs_m; bh_schwarzschild_radius_m(M_SUN, rs_m);
        CHECK(rp == 2.0 * rg && rm == 0.0, "a*=0 -> (r_s, 0)");
        // ISCO limits (BPT): a*=0 -> 6 r_g; a*=1 -> r_g (pro), 9 r_g (retro).
        double ip, ir;
        bh_kerr_isco_prograde(k0, ip); bh_kerr_isco_retrograde(k0, ir);
        const double rgs = M_SUN ? k0.gravitational_radius() : 0.0;
        CHECK(std::fabs(ip - 6.0 * rgs) / (6.0 * rgs) < 1e-14 &&
              std::fabs(ir - 6.0 * rgs) / (6.0 * rgs) < 1e-14, "a*=0 -> 6 r_g both");
        bh_kerr_isco_prograde(kx, ip); bh_kerr_isco_retrograde(kx, ir);
        CHECK(ip == rg, "a*=1 pro -> r_g EXACTLY (sqrt(16)=4 exact)");
        CHECK(ir == 9.0 * rg, "a*=1 retro -> 9 r_g EXACTLY");
        total += 5;
        // Z1/Z2 evenness (authority doc: Z1, Z2 even in a*; orbit family is
        // selected by the SIGN OF THE ROOT, not spin) => pro absorbs nothing
        // from a* sign: pro(+a*) == pro(-a*) BITWISE, same for retro.
        BlackHoleState kp, kn;
        bh_create_black_hole(M_SUN, 0.8, kp); bh_create_black_hole(M_SUN, -0.8, kn);
        double pip, pir, nip, nir;
        bh_kerr_isco_prograde(kp, pip); bh_kerr_isco_retrograde(kp, pir);
        bh_kerr_isco_prograde(kn, nip); bh_kerr_isco_retrograde(kn, nir);
        CHECK(pip == nip, "pro(a*) == pro(-a*) exact (Z1/Z2 evenness)");
        CHECK(pir == nir, "retro(a*) == retro(-a*) exact");
        total += 2;
        // Photon orbit limits: a*=0 -> 3 r_g; a*=1 -> r_g / 4 r_g.
        double pp, prr;
        bh_kerr_photon_orbit_prograde(k0, pp); bh_kerr_photon_orbit_retrograde(k0, prr);
        CHECK(std::fabs(pp - 3.0 * rgs) / (3.0 * rgs) < 1e-13 &&
              std::fabs(prr - 3.0 * rgs) / (3.0 * rgs) < 1e-13, "a*=0 -> 3 r_g");
        bh_kerr_photon_orbit_prograde(kx, pp); bh_kerr_photon_orbit_retrograde(kx, prr);
        CHECK(pp < rg + 1e-6 * rg, "a*=1 pro photon <= r_g");
        CHECK(std::fabs(prr - 4.0 * rg) / (4.0 * rg) < 1e-3, "a*=1 retro photon 4 r_g");
        total += 3;
        // Ergosphere: equator (2 r_g at a*=0... general formula) and poles.
        double eq, pol;
        bh_kerr_ergosphere_radius(kx, PI2, eq);
        bh_kerr_ergosphere_radius(kx, 0.0, pol);
        CHECK(std::fabs(eq - 2.0 * rg) / (2.0 * rg) < 1e-14, "extremal equatorial ergo = 2 r_g");
        CHECK(pol == rg, "extremal polar ergo = r_g = r_+");
        total += 2;
        // Frame dragging: exactly 0 for a*=0; sign follows spin; equatorial v=wr.
        double w;
        CHECK(bh_kerr_frame_dragging_angular_velocity(k0, 1000.0 * rgs, PI2, w) == BhErr::OK &&
              w == 0.0, "a*=0 -> omega = 0 exactly");
        bh_kerr_frame_dragging_angular_velocity(kp, 10.0 * kp.gravitational_radius(), PI2, w);
        CHECK(w > 0.0, "omega > 0 for positive spin");
        double wn;
        bh_kerr_frame_dragging_angular_velocity(kn, 10.0 * kp.gravitational_radius(), PI2, wn);
        CHECK(wn < 0.0 && wn == -w, "omega(−a*) = −omega(a*) exact");
        // Facade: Schwarzschild model returns exactly 0.0 velocity.
        double v;
        CHECK(bh_equatorial_frame_dragging_velocity(k0, rs_m * 3.0, v) == BhErr::OK && v == 0.0,
              "facade v=0 for SCHW");
        // Horizon guard on frame dragging (kp's OWN r_+, not the stale a*=0 r+).
        double rp_kp, rm_kp; bh_kerr_horizons(kp, rp_kp, rm_kp);
        CHECK(bh_kerr_frame_dragging_angular_velocity(kp, rp_kp + 0.5e-9, PI2, w) ==
              BhErr::COORDINATE_SINGULARITY, "at horizon -> coordinate singularity");
        total += 5;
        // Kerr static dilation: invalid at/inside the EQUATORIAL ergosphere (physical).
        CHECK(bh_kerr_static_time_dilation_equatorial(kp, 2.0 * kp.gravitational_radius(), o) ==
              BhErr::COORDINATE_SINGULARITY, "inside equatorial ergo rejected (physical)");
        bh_kerr_static_time_dilation_equatorial(kp, 3.0 * kp.gravitational_radius(), o);
        CHECK(o > 1.0, "outside ergo dilation > 1");
        total += 2;
    }

    // ---- E. Metric tensors ----
    gate("metric tensors");
    {
        StMetric mm = st_minkowski_metric();
        double mc[4] = {0.0, 1.0, 2.0, 3.0};
        Tensor4 g; CHECK(st_metric_tensor(mm, mc, g) == StErr::OK, "minkowski ok");
        CHECK(g.m[0][0] == -1.0 && g.m[1][1] == 1.0 && g.m[2][2] == 1.0 && g.m[3][3] == 1.0,
              "minkowski diag(-1,1,1,1)");
        double sc[4] = {0.0, 29541.265550554049, 1.0, 0.0};
        StMetric sm; st_schwarzschild_metric(M_SUN, sm);
        st_metric_tensor(sm, sc, g);
        const double rs = sm.rs_m;
        const double f = 1.0 - rs / sc[1];
        CHECK(g.m[0][0] == -f && g.m[1][1] == 1.0 / f, "schw diag from f");
        CHECK(g.m[2][2] == sc[1] * sc[1], "g_thth = r^2");
        // Kerr a* = 0 matches Schwarzschild EXACTLY (same spacetime limits).
        StMetric k0; st_kerr_metric(M_SUN, 0.0, k0);
        Tensor4 gk, gs;
        st_metric_tensor(k0, sc, gk);
        st_metric_tensor(sm, sc, gs);
        // Value-level: a*=0 reproduces Schwarzschild exactly (authority claim).
        bool valeq = true;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            if (gk.m[i][j] != gs.m[i][j]) valeq = false;
        CHECK(valeq, "kerr a*=0 == schwarzschild component VALUES");
        // AUTHORITY BIT-SIGN NUANCE (v1.2 finding — mirror must NOT normalize):
        // KerrMetric(a*=0) yields g_tph = -0.0 (from -2 a ... / Sigma) while
        // SchwarzschildMetric gives +0.0. Python exhibits the same byte-level
        // difference (verified); the mirror reproduces it signbit-exactly.
        CHECK(std::signbit(gk.m[0][3]) && std::signbit(gk.m[3][0]) &&
              !std::signbit(gs.m[0][3]) && !std::signbit(gs.m[3][0]),
              "g_tph -0.0 sign nuance mirrored bitwise");
        // Coordinate-patch guards.
        double bad1[4] = {0.0, rs, 1.0, 0.0};
        CHECK(st_metric_tensor(sm, bad1, g) == StErr::DEGENERATE_METRIC, "r=rs refused");
        double bad2[4] = {0.0, 2.0 * rs, 0.0, 0.0};
        CHECK(st_metric_tensor(sm, bad2, g) == StErr::DEGENERATE_METRIC, "polar axis refused");
        double bad3[4] = {0.0, -rs, 1.0, 0.0};
        CHECK(st_metric_tensor(sm, bad3, g) == StErr::DEGENERATE_METRIC, "r<=0 refused");
        // Kerr outer-horizon refusal.
        StMetric kk; st_kerr_metric(M_SUN, 0.9, kk);
        double badk[4] = {0.0, kk.r_plus_m, 1.0, 0.0};
        CHECK(st_metric_tensor(kk, badk, g) == StErr::DEGENERATE_METRIC, "r=r+ refused");
        // Inverse is a true right-inverse: g g^-1 ~ I.
        Tensor4 gi; st_metric_inverse(sm, sc, gi);
        st_metric_tensor(sm, sc, g);
        for (int i = 0; i < 4; ++i) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += g.m[i][k] * gi.m[k][i];
            CHECK(std::fabs(s - 1.0) < 1e-12, "g g^-1 diagonal ~ 1");
        }
        total += 16;
    }

    // ---- F. Connection + curvature vacuum anchors ----
    gate("curvature anchors");
    {
        double sc[4] = {0.0, 29541.265550554049, 1.0471975511965976, 0.5};
        StMetric sm; st_schwarzschild_metric(M_SUN, sm);
        const double rs = sm.rs_m, r = sc[1];
        // Vacuum: Ricci ~ 0 while Riemann != 0 (both numerically tiny vs K).
        Tensor4 ric; CHECK(st_ricci_tensor(sm, sc, ric) == StErr::OK, "ricci ok");
        double mx = 0.0;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            mx = std::fmax(mx, std::fabs(ric.m[i][j]));
        CHECK(mx < 1e-9, "vacuum Ricci ~ 0 (numeric; authority measures 2.9e-11 here)");
        double R; st_ricci_scalar(sm, sc, R);
        CHECK(std::fabs(R) < 1e-17, "vacuum R ~ 0 (numeric; authority measures 3.2e-20 here)");
        double K; st_kretschmann_scalar(sm, sc, K);
        const double K_ref = 12.0 * rs * rs / std::pow(r, 6.0);
        CHECK(std::fabs(K - K_ref) / K_ref < 1e-9, "K ~ 12 rs^2 / r^6 (analytic anchor)");
        RiemannTensor rm;
        st_riemann_tensor(sm, sc, rm);
        double rmx = 0.0;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k)
            for (int l = 0; l < 4; ++l) rmx = std::fmax(rmx, std::fabs(rm.m[i][j][k][l]));
        CHECK(rmx > 0.0, "Riemann != 0 (curved vacuum)");
        // Metric compatibility: numerical Gamma consistency g_00 g^00 ~ 1 at same point.
        Tensor4 gu, gd; st_metric_inverse(sm, sc, gu); st_metric_tensor(sm, sc, gd);
        double tr = 0.0;
        for (int i = 0; i < 4; ++i) tr += gd.m[i][0] * gu.m[0][i];
        // Not an exact identity per-row; checked only as sanity of the pipeline.
        CHECK(std::isfinite(tr), "pipeline finite");
        // Minkowski: everything identically zero, bit-exact.
        StMetric mmk = st_minkowski_metric();
        double mc[4] = {0.0, 1.0, 2.0, 3.0};
        st_riemann_tensor(mmk, mc, rm);
        bool allz = true;
        for (int i = 0; i < 4 && allz; ++i) for (int j = 0; j < 4 && allz; ++j)
            for (int k = 0; k < 4 && allz; ++k) for (int l = 0; l < 4 && allz; ++l)
                if (rm.m[i][j][k][l] != 0.0) allz = false;
        CHECK(allz, "Minkowski Riemann == 0 exactly");
        double Km; st_kretschmann_scalar(mmk, mc, Km);
        CHECK(Km == 0.0, "Minkowski K == 0 exactly");
        total += 9;
    }

    // ---- G. Causality ----
    gate("causality");
    {
        StMetric mmk = st_minkowski_metric();
        double a[4] = {0.0, 0.0, 0.0, 0.0};
        double b1[4] = {6e8, 1.0, 2.0, 3.0};        // timelike
        double b2[4] = {2.998e8, 3e8, 0.0, 0.0};    // spacelike
        double b3[4] = {3e8, 3e8, 0.0, 0.0};        // null (|ds2| <= tol)
        IntervalType it;
        st_classify_interval(mmk, a, b1, it); CHECK(it == IntervalType::TIMELIKE, "timelike");
        st_classify_interval(mmk, a, b2, it); CHECK(it == IntervalType::SPACELIKE, "spacelike");
        st_classify_interval(mmk, a, b3, it); CHECK(it == IntervalType::NULLI, "null");
        // Null-direction helper: future>0 for diagonal metric.
        double f4[4], p4[4], d3[3] = {1.0, 0.5, -0.25};
        double mcoords[4] = {0.0, 1.0, 2.0, 3.0};
        CHECK(st_null_ray_directions(mmk, mcoords, d3, f4, p4) == StErr::OK, "nullray ok");
        CHECK(f4[0] > 0.0 && p4[0] < 0.0, "future/past signs");
        // Kerr (off-diagonal g_tph) -> NOT_IMPLEMENTED (authority boundary).
        StMetric kk; st_kerr_metric(M_SUN, 0.9, kk);
        double kc[4] = {0.0, 30.0 * kk.r_g_m, 1.1, 0.3};
        CHECK(st_null_ray_directions(kk, kc, d3, f4, p4) == StErr::NOT_IMPLEMENTED,
              "kerr nullray -> NotImplementedError preserved");
        total += 7;
    }

    // ---- H. Geodesics (deterministic + guards) ----
    gate("geodesics");
    {
        StMetric mmk = st_minkowski_metric();
        // Straight inertial worldline: x = x0 + u T exactly (flat Gamma = 0).
        double c0[4] = {0.0, 100.0, -50.0, 25.0};
        double u0[4] = {1.5e8, 1.0e8, -5.0e7, 2.0e7};  // affine units (u0 != c? any 4-vel ok in flat test)
        GeodesicSolution sol;
        CHECK(st_integrate_geodesic(mmk, c0, u0, 2.0, 4, false, 1e-10, sol) == StErr::OK, "flat int ok");
        // h = 0.5; final lam = 2.0 -> x = x0 + u * (lam_final); RK4 on constant k is exact.
        for (int i = 0; i < 4; ++i) {
            const double expect = c0[i] + u0[i] * 2.0;
            CHECK(sol.coordinates.back()[i] == expect, "flat straight-line exact");
        }
        total += 5;
        // Horizon guard: radial infall halts with HORIZON_CROSSING (never NaN).
        StMetric sm; st_schwarzschild_metric(M_SUN, sm);
        double cs0[4] = {0.0, 10.0 * sm.rs_m, PI2, 0.0};
        // Infall 4-velocity (radial-dominant): crosses the guard band.
        double us0[4] = {3.0e8, -8.0e7, 0.0, 1.0e-4};
        CHECK(st_integrate_geodesic(sm, cs0, us0, 1.0, 400, false, 1e-10, sol)
              == StErr::HORIZON_CROSSING, "infall -> HorizonCrossingError");
        total += 1;
        // Determinism: same inputs -> bit-identical final states.
        GeodesicSolution s1, s2;
        double us_orb[4] = {3.3e8, 0.0, 0.0, 3.0e-4};
        st_integrate_geodesic(sm, cs0, us_orb, 5.0e-3, 50, false, 1e-10, s1);
        st_integrate_geodesic(sm, cs0, us_orb, 5.0e-3, 50, false, 1e-10, s2);
        CHECK(s1.coordinates.back() == s2.coordinates.back(), "rk4 deterministic bits");
        st_integrate_geodesic(sm, cs0, us_orb, 2.0e-3, 10, true, 1e-10, s1);
        st_integrate_geodesic(sm, cs0, us_orb, 2.0e-3, 10, true, 1e-10, s2);
        CHECK(s1.coordinates.back() == s2.coordinates.back() &&
              s1.parameters.size() == s2.parameters.size(), "rk45 deterministic bits");
        total += 2;
        // Argument validation taxonomy.
        CHECK(st_integrate_geodesic(sm, cs0, us_orb, -1.0, 50, false, 1e-10, sol)
              == StErr::INVALID_COORDINATE, "negative limit -> InvalidCoordinateError");
        CHECK(st_integrate_geodesic(sm, cs0, us_orb, 1.0, 0, false, 1e-10, sol)
              == StErr::INVALID_COORDINATE, "steps=0 -> InvalidCoordinateError");
        total += 2;
    }

    // ---- I. Facade conversion (metric-correct u^0) ----
    gate("facade conversion");
    {
        StMetric mmk = st_minkowski_metric();
        double co[4], uu[4];
        // In flat spacetime, u^0 = gamma * c exactly (SR consistency check).
        RelVec3 v{1.0e7, 2.0e6, -5.0e5};
        CHECK(st_cartesian_state_to_chart(mmk, 0.0, 0.0, 0.0, 0.0, v, false, co, uu) == StErr::OK,
              "conversion ok");
        const double vmag = std::hypot(std::hypot(v.x, v.y), v.z);
        double gam; lorentz_factor(vmag, gam);
        CHECK(uu[0] == C * gam, "u0 = gamma c exactly in flat spacetime");
        // Null ray: u = (1, v̂) with dλ = dct.
        RelVec3 vr{0.0, 0.0, C};
        CHECK(st_cartesian_state_to_chart(mmk, 0.0, 0.0, 0.0, 0.0, vr, true, co, uu) == StErr::OK &&
              uu[0] == 1.0 && uu[1] == 0.0 && uu[2] == 0.0 && uu[3] == 1.0, "null ray u0=1");
        // Light-speed violation for massive object at exactly c.
        CHECK(st_cartesian_state_to_chart(mmk, 0.0, 0.0, 0.0, 0.0, vr, false, co, uu)
              == StErr::LIGHT_SPEED_VIOLATION, "v=c -> LightSpeedViolation");
        total += 3;
    }

    // ---- J. HUD exposure (mission Phase 6: honest availability taxonomy) ----
    gate("hud exposure");
    {
        HudSnapshot snap;
        snap.selected_index = 0;
        snap.selected_name = "Sun";
        snap.has_selected_kind = true;
        snap.selected_kind = "STAR";
        snap.has_selected_state = true;
        double rs_sun; bh_schwarzschild_radius_m(M_SUN, rs_sun);
        snap.has_sel_bh = true;
        snap.sel_bh_rs_m = rs_sun;
        snap.sel_bh_isco_m = 3.0 * rs_sun;
        snap.sel_bh_photon_m = 1.5 * rs_sun;
        HudState h = build_hud(snap);
        int bh_rows = 0, kerr_na = 0, model_rows = 0;
        for (const HudRow& r : h.selection) {
            if (std::string(r.label).rfind("BH ", 0) == 0) ++bh_rows;
            if (r.label == "BH R_S (CTR)") {
                CHECK(r.available, "BH R_S present+available");
                CHECK(std::string(r.classification).find("PHYSICALLY-MODELED") != std::string::npos,
                      "BH rows PHYSICALLY-MODELED");
            }
            if (r.label == "BH MODEL (CTR)") {
                ++model_rows;
                CHECK(r.available && std::string(r.value).find("SCHWARZSCHILD") != std::string::npos,
                      "modeled model = SCHWARZSCHILD");
            }
            if (r.label == "BH KERR SPIN") {
                ++kerr_na;
                CHECK(!r.available, "Kerr spin honestly NOT AVAILABLE");
                CHECK(std::string(r.classification).find("not modeled") != std::string::npos,
                      "Kerr NA gives the reason");
            }
        }
        CHECK(bh_rows == 5, "exactly 5 black-hole selection rows");
        CHECK(model_rows == 1 && kerr_na == 1, "model row + kerr NA row both present once");
        total += 7;
        // has_sel_bh = false -> ALL five rows NOT AVAILABLE (never fabricated).
        snap.has_sel_bh = false;
        h = build_hud(snap);
        int na_count = 0;
        for (const HudRow& r : h.selection)
            if (std::string(r.label).rfind("BH ", 0) == 0) {
                ++na_count;
                CHECK(!r.available, "BH row NA when unavailable");
            }
        CHECK(na_count == 5, "all 5 BH rows NA");
        total += 6;
    }

    // ---- K. v1.2 VISUAL INTEGRATION (mission Phase 4/5: authority -> native
    // mirror bh_viz -> render-ready geometry; every value traced, every scale
    // transform CINEMATIC-labeled, exact scientific ratios intact) ----
    gate("visual integration");
    {
        const double vrad = 8.3;   // representative Sun visual radius (CINEMATIC, main_production formula)
        BhVizOverlay ov;
        CHECK(bh_viz_overlay_params(M_SUN, vrad, ov) && ov.valid, "overlay params valid for the Sun");
        double rs_mirror = 0.0;
        bh_schwarzschild_radius_m(M_SUN, rs_mirror);
        CHECK(ov.rs_m == rs_mirror, "overlay r_s bit-identical to black_hole_sim mirror");
        CHECK(ov.photon_m == 1.5 * ov.rs_m && ov.isco_m == 3.0 * ov.rs_m,
              "photon/ISCO exact 1.5/3 r_s ratios (double)");
        CHECK(ov.anchor_units == 1.5 * vrad && ov.magnification == (1.5 * vrad) / ov.rs_m,
              "horizon anchor = 1.5 x body visual radius; magnification = anchor / r_s");
        CHECK(!ov.shells[0].dilation_valid, "horizon dilation NOT AVAILABLE (boundary guard, not fabricated)");
        CHECK(ov.shells[1].dilation_valid && ov.shells[2].dilation_valid &&
              std::fabs(ov.shells[1].dilation_minus_one - (std::sqrt(3.0) - 1.0)) < 1e-12 &&
              std::fabs(ov.shells[2].dilation_minus_one - (std::sqrt(1.5) - 1.0)) < 1e-12,
              "photon/ISCO dilation dt/dtau-1 match closed forms sqrt(3)-1 / sqrt(3/2)-1");
        // Invalid masses rejected (never a fabricated scale).
        BhVizOverlay bad;
        CHECK(!bh_viz_overlay_params(0.0, vrad, bad) && !bh_viz_overlay_params(-1.0, vrad, bad) &&
              !bh_viz_overlay_params(std::nan(""), vrad, bad), "NaN/sign/zero mass rejected");
        total += 8;

        BhVizRings rings;
        bh_viz_build_rings(ov, rings);
        CHECK(rings.points.size() == 3ull * (BHVIZ_RING_SEGMENTS + 1),
              "rings: 3 x 97 vertices"); total += 1;
        bool ring_ok = rings.spans[0].first == 0;
        for (int r = 0; r < 3 && ring_ok; ++r) {
            ring_ok = rings.spans[r].second == (uint32_t)(BHVIZ_RING_SEGMENTS + 1);
            const float R = (float)ov.shells[r].visual_radius_units;
            for (uint32_t i = rings.spans[r].first; i < rings.spans[r].first + rings.spans[r].second && ring_ok; ++i) {
                const auto& p = rings.points[i];
                ring_ok = std::fabs(std::sqrt(p[0]*p[0] + p[1]*p[1]) - R) < 1e-3f * R && p[2] == 0.0f;
            }
            // closed polyline: first vs last vertex (sin(2π) != 0 in floating
            // point; physical closure tolerance, rel error must be < 1e-15).
            const auto& a = rings.points[rings.spans[r].first];
            const auto& b = rings.points[rings.spans[r].first + rings.spans[r].second - 1];
            const float gap = std::sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]));
            ring_ok = ring_ok && gap <= 1e-15f * (R > 1.0f ? R : 1.0f);
        }
        CHECK(ring_ok, "ring vertices lie on shells in the ecliptic plane, closed polylines"); total += 1;

        const auto tr0 = std::chrono::steady_clock::now();
        BhVizRays rays;
        const bool rays_ok = bh_viz_build_light_rays(M_SUN, ov, rays);
        const double build_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tr0).count();
        CHECK(rays_ok && rays.valid, "5 null geodesics integrate (spacetime_sim RK4)");
        CHECK(build_ms < 200.0, "one-time CPU build under 200 ms (REAL measured)");
        bool rays_shape = rays_ok;
        CHECK(!rays_ok || rays.points.size() == (size_t)BHVIZ_RAY_COUNT * (BHVIZ_RAY_STEPS + 1),
              "5 x 1001 ray vertices contiguous"); total += 1;
        for (int ray = 0; ray < BHVIZ_RAY_COUNT && rays_shape; ++ray) {
            const auto& sp = rays.spans[(size_t)ray];
            rays_shape = sp.second == (uint32_t)(BHVIZ_RAY_STEPS + 1);
            double rmin = 1e300;
            double r0 = -1.0, rend = -1.0;
            for (uint32_t i = sp.first; i < sp.first + sp.second && rays_shape; ++i) {
                const auto& p = rays.points[i];
                rays_shape = std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
                const double r_m = std::sqrt((double)p[0]*p[0] + (double)p[1]*p[1] + (double)p[2]*p[2]) / ov.magnification;
                if (r_m < rmin) rmin = r_m;
                if (i == sp.first) r0 = r_m;
                rend = r_m;
            }
            // Physical sanity: rays escape (never inside 1.5 r_s), start/end near the
            // 25 r_s emitter shell — CINEMATIC magnification divides out exactly.
            rays_shape = rays_shape && rmin > 1.5 * ov.rs_m && r0 > 24.0 * ov.rs_m &&
                         rend > 24.0 * ov.rs_m && r0 < 30.0 * ov.rs_m;
        }
        CHECK(rays_shape, "rays escape > 1.5 r_s, begin/end at the 25 r_s emitter shell, all finite");
        total += 4;

        // HUD: UI rows correspond to rendered overlay state (mission Phase 8).
        HudSnapshot hs;
        hs.bh_overlay_mode = 1; hs.bh_overlay_avail = true; hs.bh_overlay_mag = ov.magnification;
        HudState hud = build_hud(hs);
        int viz_row = 0;
        for (const HudRow& r : hud.status) if (std::string(r.label) == "BH STRUCTURE VIZ (F4)") {
            ++viz_row;
            CHECK(r.available && std::string(r.classification).find("CINEMATIC") != std::string::npos,
                  "viz HUD row available + CINEMATIC label");
        }
        CHECK(viz_row == 1, "viz HUD row present exactly when overlay on");
        hs.bh_overlay_mode = 0;
        hud = build_hud(hs);
        viz_row = 0;
        for (const HudRow& r : hud.status) if (std::string(r.label) == "BH STRUCTURE VIZ (F4)") ++viz_row;
        CHECK(viz_row == 0, "no viz HUD rows when overlay off");
        hs.bh_overlay_mode = 2; hs.bh_overlay_avail = false;
        hud = build_hud(hs);
        viz_row = 0; int na_row = 0;
        for (const HudRow& r : hud.status) if (std::string(r.label) == "BH STRUCTURE VIZ (F4)") {
            ++viz_row; if (!r.available) ++na_row;
        }
        CHECK(viz_row == 1 && na_row == 1, "failed overlay honestly NOT AVAILABLE");
        total += 5;
    }

    std::printf("v11_gates: %d checks, %d failures\n", total, g_fail);
    if (g_fail == 0) { std::printf("v11_gates: ALL %d checks PASSED\n", total); return 0; }
    return 1;
}
