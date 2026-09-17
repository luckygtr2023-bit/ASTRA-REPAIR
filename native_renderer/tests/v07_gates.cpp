// ASTRA COSMOS v0.8 gates — native N-body engine (mirror of astra.nbody):
// pairwise gravity, velocity Verlet, diagnostics, celestial binding, gravity
// model binding semantics. No GPU required (same convention as v04..v06).
#include "app/nbody_sim.h"
#include "app/celestial_sim.h"
#include "app/persist.h"
#include "app/hud_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra::app;

static bool v_eq(const Vec3m& a, const Vec3m& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static bool v_close(const Vec3m& a, const Vec3m& b, double tol) {
    return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol &&
           std::fabs(a.z - b.z) < tol;
}

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };

    // ---- A. Vector semantics ----
    gate("vec ops");
    {
        const Vec3m a{1.5, -2.0, 3.25}, b{0.5, 2.0, -0.25};
        const Vec3m s = a + b, d = a - b, m = a * 2.0;
        CHECK(s.x == 2.0 && s.y == 0.0 && s.z == 3.0, "add exact");
        CHECK(d.x == 1.0 && d.y == -4.0 && d.z == 3.5, "sub exact");
        CHECK(m.x == 3.0 && m.y == -4.0 && m.z == 6.5, "scale exact");
        const Vec3m u{3.0, 4.0, 12.0};
        CHECK(magnitude_sq(u) == 169.0, "mag sq exact");
        CHECK(magnitude(u) == 13.0, "mag exact");
        CHECK(magnitude_sq(Vec3m{}) == 0.0, "zero mag");
        total += 6;
    }

    // ---- B. Pairwise gravity ----
    gate("pairwise");
    {
        const double mA = 2e30, mB = 6e24;
        Vec3m ai, aj;
        // Along +x at distance R: a_A = G mB / R^2 toward +x.
        const double R = 1.5e11;
        NBodyResult r = pairwise_acceleration({0, 0, 0}, {R, 0, 0}, mA, mB,
                                              G_SI, 0.0, ai, aj);
        CHECK(r.ok, "pair ok");
        CHECK(std::fabs(ai.x - G_SI * mB / (R * R)) / (G_SI * mB / (R * R)) < 1e-15,
              "1st body accel magnitude");
        CHECK(ai.y == 0.0 && ai.z == 0.0, "1st body axis");
        CHECK(std::fabs(aj.x + G_SI * mA / (R * R)) / (G_SI * mA / (R * R)) < 1e-15,
              "2nd body accel magnitude");
        // Newton's third law pair: m_A a_A == -(m_B a_B) up to float.
        CHECK((mA * ai.x + mB * aj.x) == 0.0 ||
              std::fabs(mA * ai.x + mB * aj.x) / std::fabs(mA * ai.x) < 1e-15,
              "pair momentum exchange antisymmetric");
        // Inverse square: doubling distance quarters the force.
        Vec3m a2i, a2j;
        r = pairwise_acceleration({0, 0, 0}, {2 * R, 0, 0}, mA, mB, G_SI, 0.0, a2i, a2j);
        CHECK(r.ok && std::fabs(a2i.x / ai.x - 0.25) < 1e-12, "inverse square");
        // Softening: force vanishes smoothly at coincidence (no singularity).
        r = pairwise_acceleration({1, 1, 1}, {1, 1, 1}, mA, mB, G_SI, 1e-6, ai, aj);
        CHECK(r.ok && ai.x == 0.0 && aj.x == 0.0, "softened coincidence zero");
        // Zero softening + coincidence: explicit reported singularity, never silent.
        r = pairwise_acceleration({0, 0, 0}, {0, 0, 0}, mA, mB, G_SI, 0.0, ai, aj);
        CHECK(!r.ok && !r.error.empty(), "singularity reported");
        // Axis symmetry: |a| identical in +x/-x.
        Vec3m pi, pj, ni, nj;
        pairwise_acceleration({0, 0, 0}, {R, 0, 0}, mA, mB, G_SI, 1e-6, pi, pj);
        pairwise_acceleration({0, 0, 0}, {-R, 0, 0}, mA, mB, G_SI, 1e-6, ni, nj);
        CHECK(pi.x == -ni.x, "axis antisymmetry");
        total += 9;
    }

    // ---- C. Acceleration summation ----
    gate("compute_accelerations");
    {
        std::vector<NBody> sys = {
            {"A", 2e30, {0, 0, 0}, {0, 0, 0}},
            {"B", 6e24, {1.5e11, 0, 0}, {0, 3e4, 0}},
            {"C", 6e24, {-1.5e11, 1e10, 5e8}, {0, 0, 0}},
        };
        std::vector<Vec3m> acc;
        NBodyResult r = compute_accelerations(sys, G_SI, 1e-6, acc);
        CHECK(r.ok && acc.size() == 3, "sized");
        // Determinism: identical inputs -> bit-identical outputs.
        std::vector<Vec3m> acc2;
        compute_accelerations(sys, G_SI, 1e-6, acc2);
        CHECK(acc.size() == acc2.size(), "det size");
        bool same = true;
        for (size_t i = 0; i < acc.size(); ++i) same = same && v_eq(acc[i], acc2[i]);
        CHECK(same, "bit-identical determinism");
        // Total momentum exchange sums to zero (vector sum of m_i a_i).
        Vec3m mp{};
        for (size_t i = 0; i < sys.size(); ++i) mp = mp + acc[i] * sys[i].mass_kg;
        const double scale = magnitude(acc[0] * sys[0].mass_kg) + 1e-300;
        CHECK(magnitude(mp) / scale < 1e-12, "net momentum exchange ~0");
        total += 4;
    }

    // ---- D. Velocity Verlet ----
    gate("verlet core");
    {
        CHECK(!is_valid_dt(0.0), "dt0");
        CHECK(!is_valid_dt(-1.0), "dt-");
        CHECK(!is_valid_dt(NAN), "dtnan");
        CHECK(!is_valid_dt(INFINITY), "dtinf");
        CHECK(is_valid_dt(1e-12) && is_valid_dt(1e12), "dt ok");
        total += 5;

        std::vector<NBody> sys = {
            {"A", 2e30, {0, 0, 0}, {0, 0, 0}},
            {"B", 6e24, {1.5e11, 0, 0}, {0, 2.9785e4, 0}},
        };
        std::vector<Vec3m> acc;
        NBodyResult r = velocity_verlet_step(sys, acc, 0.0, G_SI, 1e-6);
        CHECK(!r.ok, "reject dt=0");
        r = velocity_verlet_step(sys, acc, 3600.0, G_SI, 1e-6);
        CHECK(r.ok && acc.size() == 2, "first step computes cache");
        total += 2;

        // Time reversibility: k steps, flip all velocities, k steps, flip back
        // -> positions return to within ~ulp of the seed (symplectic property).
        std::vector<NBody> s0 = {
            {"S", 1.9885e30, {0, 0, 0}, {0, 0, 0}},
            {"E", 5.9724e24, {1.47098e11, 0, 0}, {0, 3.0279e4, 100.0}},
            {"J", 1.8982e27, {0, 7.4052e11, 0}, {1.3059e4, 0, -50.0}},
        };
        std::vector<NBody> w = s0;
        std::vector<Vec3m> cache;
        for (int k = 0; k < 200; ++k) velocity_verlet_step(w, cache, 3600.0, G_SI, 1e-6);
        for (auto& b : w) b.velocity = b.velocity * -1.0;
        for (int k = 0; k < 200; ++k) velocity_verlet_step(w, cache, 3600.0, G_SI, 1e-6);
        for (auto& b : w) b.velocity = b.velocity * -1.0;
        for (size_t i = 0; i < s0.size(); ++i) {
            // Return residual against the SYSTEM scale (1e11 m): the Sun sits at
            // the exact origin so a body-own relative scale degenerates to /0.
            CHECK(magnitude(w[i].position - s0[i].position) / 1e11 < 1e-9,
                  "time-reversal return");
            ++total;
        }

        // Cache-path determinism: engine A uses warm cache, engine B re-derives.
        std::vector<NBody> a1 = s0, a2 = s0;
        std::vector<Vec3m> c1, c2;
        for (int k = 0; k < 50; ++k) velocity_verlet_step(a1, c1, 3600.0, G_SI, 1e-6);
        for (int k = 0; k < 50; ++k) velocity_verlet_step(a2, c2, 3600.0, G_SI, 1e-6);
        bool same = true;
        for (size_t i = 0; i < a1.size(); ++i)
            same = same && v_eq(a1[i].position, a2[i].position) &&
                            v_eq(a1[i].velocity, a2[i].velocity);
        CHECK(same, "warm-cache determinism");
        ++total;
    }

    // ---- E. Diagnostics on a synthetic 2-body circular orbit ----
    gate("diagnostics");
    {
        const double M = 2e30, m = 1e24, R = 1e11;
        const double v = std::sqrt(G_SI * M / R); // ~ circular speed (test M>>m)
        std::vector<NBody> sys = {
            {"P", M, {0, 0, 0}, {0, 0, 0}},
            {"Q", m, {R, 0, 0}, {0, v, 0}},
        };
        double e = 0;
        CHECK(total_energy(sys, G_SI, 1e-6, e).ok && e < 0.0, "bound orbit E<0");
        // Circular orbit: KE = -E_total (virial), |E| = G M m / (2R).
        const double exact = G_SI * M * m / (2.0 * R);
        CHECK(std::fabs(std::fabs(e) - exact) / exact < 1e-3, "virial magnitude");
        double u = 0;
        CHECK(total_potential(sys, G_SI, 1e-6, u).ok && u < 0, "U negative");
        CHECK(kinetic_energy(sys) > 0, "KE positive");
        const Vec3m p = total_linear_momentum(sys);
        CHECK(std::fabs(p.x) + std::fabs(p.z) == 0.0, "no transverse momentum");
        const Vec3m L0 = total_angular_momentum(sys);
        std::vector<Vec3m> cache;
        for (int k = 0; k < 1000; ++k) velocity_verlet_step(sys, cache, 3600.0, G_SI, 1e-6);
        const Vec3m L1 = total_angular_momentum(sys);
        const double lerr = std::fabs(L1.z - L0.z) / std::fabs(L0.z);
        CHECK(lerr < 1e-9, "angular momentum conserved");
        double e1 = 0;
        total_energy(sys, G_SI, 1e-6, e1);
        CHECK(std::fabs(e1 - e) / std::fabs(e) < 1e-3, "symplectic energy bound");
        // Momentum conserved bit-exactly over the exchange pair.
        const Vec3m p1 = total_linear_momentum(sys);
        CHECK(std::fabs(p1.y - p.y) / (std::fabs(p.y) + 1e-300) < 1e-12,
              "momentum stable");
        total += 9;
    }

    // ---- F. Convergence order: halving dt quarters the closure error ----
    gate("verlet 2nd-order convergence");
    {
        const double M = 2e30, m = 1e24, R = 1e11;
        // Two-body relative orbit, mu = G(M+m). BARYCENTRIC velocity for the
        // primary (root-caused: with v_P = 0 the net linear momentum drags the
        // measured body by 2*pi*m/(M+m) ~= 3.14e-6 rad per orbit, a dt-INDEPENDENT
        // floor that masquerades as (and at dt=3600 accidentally cancels) the
        // integrator phase error). Measure the relative coordinate Q - P.
        const double v = std::sqrt(G_SI * (M + m) / R);
        auto closure_err = [&](double dt) {
            std::vector<NBody> sys = {
                {"P", M, {0, 0, 0}, {0, -v * m / M, 0}},
                {"Q", m, {R, 0, 0}, {0, v - v * m / M, 0}},
            };
            const double T = 2.0 * 3.14159265358979323846 * R / v; // one orbit
            std::vector<Vec3m> cache;
            double t = 0;
            while (t + dt <= T) { velocity_verlet_step(sys, cache, dt, G_SI, 1e-6); t += dt; }
            if (t < T) {  // EXACT landing: residual must not pollute the metric
                velocity_verlet_step(sys, cache, T - t, G_SI, 1e-6);
            }
            return magnitude((sys[1].position - sys[0].position) - Vec3m{R, 0, 0}) / R;
        };
        const double e1 = closure_err(3600.0), e2 = closure_err(1800.0);
        const double ratio = e1 / e2;
        std::printf("  closure dt=3600 e=%.3e dt=1800 e=%.3e ratio=%.2f\n", e1, e2, ratio);
        CHECK(e1 > 0.0 && e2 > 0.0, "errors measured");
        CHECK(ratio > 2.0 && ratio < 8.0, "quadratic scaling");
        total += 3;
    }

    // ---- G. Celestial binding ----
    gate("celestial binding");
    {
        const std::vector<CelestialBody> scene = make_solar_system();
        CHECK(scene.size() == 10, "scene size");
        ++total;
        const std::vector<NBody> st = to_nbody_state(scene, 0.0);
        CHECK(st.size() == scene.size(), "nbody size");
        ++total;
        for (size_t i = 0; i < st.size(); ++i) {
            CHECK(st[i].id == scene[i].name, "id preserved");
            CHECK(st[i].mass_kg == scene[i].mass_kg, "mass preserved");
            total += 2;
        }
        // Position seed: world_km of a fresh engine must equal propagate_world
        // EXACTLY (same compositions, /1000 *1000 round-trip of the same double).
        const std::vector<Vec3d> w0 = propagate_world(scene, 0.0);
        NBodyEngine eng;
        eng.seed(scene, 0.0);
        const std::vector<Vec3d> ew = eng.world_km();
        for (size_t i = 0; i < scene.size(); ++i) {
            CHECK(v_close(Vec3m{w0[i][0], w0[i][1], w0[i][2]},
                          Vec3m{ew[i][0], ew[i][1], ew[i][2]}, 1e-9),
                  "seed == kepler at t0");
            ++total;
        }
        // Moon world state in SI uses Earth world base (parent composition).
        const NBody& earth = st[3];
        const NBody& moon = st[9];
        CHECK(moon.position.x != earth.position.x, "moon offset exists");
        CHECK(magnitude(moon.position - earth.position) > 3.0e8, "moon ~384400km");
        CHECK(magnitude(moon.velocity - earth.velocity) > 900.0 &&
              magnitude(moon.velocity - earth.velocity) < 1200.0, "moon v ~1022m/s");
        total += 3;
        // Forward-only contract.
        CHECK(eng.advance_to(86400.0), "advance ok");
        CHECK(!eng.advance_to(0.0), "backwards rejected at earlier point");
        CHECK(!eng.advance_to(43200.0), "backwards rejected mid");
        total += 3;
        // Reset + reseed reproduces the same fresh state deterministically.
        eng.reset();
        CHECK(!eng.seeded(), "unseeded after reset");
        eng.seed(scene, 0.0);
        NBodyEngine eng2;
        eng2.seed(scene, 0.0);
        CHECK(eng.advance_to(12.0 * 86400.0) && eng2.advance_to(12.0 * 86400.0),
              "both advance");
        const std::vector<NBody>& b1 = eng.bodies();
        const std::vector<NBody>& b2 = eng2.bodies();
        bool same = true;
        for (size_t i = 0; i < b1.size(); ++i)
            same = same && v_eq(b1[i].position, b2[i].position) &&
                            v_eq(b1[i].velocity, b2[i].velocity);
        CHECK(same, "engine determinism (reseed)");
        total += 3;
        // Partial-step landing exactness: advance in one call vs split calls.
        NBodyEngine e1, e2;
        e1.seed(scene, 0.0);
        e2.seed(scene, 0.0);
        e1.advance_to(1.5 * 86400.0);
        e2.advance_to(86400.0);
        e2.advance_to(1.5 * 86400.0);
        same = true;
        for (size_t i = 0; i < e1.bodies().size(); ++i)
            same = same && v_eq(e1.bodies()[i].position, e2.bodies()[i].position);
        CHECK(same, "partial-step landing equality");
        ++total;
        // Energy drift bookkeeping present and tiny after a simulated year.
        NBodyEngine yr;
        yr.seed(scene, 0.0);
        CHECK(yr.energy_drift_rel() == 0.0, "drift 0 before stepping");
        yr.advance_to(365.25 * 86400.0);
        const double drift = yr.energy_drift_rel();
        std::printf("  n-body 1yr energy drift (rel): %.3e\n", drift);
        CHECK(drift != 0.0, "drift measured");
        CHECK(std::fabs(drift) < 1e-8, "drift bounded (1yr)");
        total += 4;
        // Sanity: Earth radius remains within [0.98, 1.02] AU after a year.
        const Vec3d epos = yr.world_km()[3];
        const double r_au = std::sqrt(epos[0] * epos[0] + epos[1] * epos[1] +
                                      epos[2] * epos[2]) / AU_KM;
        CHECK(r_au > 0.98 && r_au < 1.02, "earth orbit sanity");
        ++total;
        // Name taxonomy.
        CHECK(std::strstr(gravity_model_name(GravityModel::KEPLER), "KEPLER"), "name k");
        CHECK(std::strstr(gravity_model_name(GravityModel::NBODY), "NBODY"), "name n");
        CHECK(NBODY_DT_S == 3600.0, "policy dt");
        total += 3;
    }

    // ---- H. NBODY vs KEPLER continuity (measured, not faked) ----
    gate("nbody vs kepler proximity");
    {
        const std::vector<CelestialBody> scene = make_solar_system();
        NBodyEngine eng;
        eng.seed(scene, 0.0);
        eng.advance_to(30.0 * 86400.0);
        const std::vector<Vec3d> wn = eng.world_km();
        const std::vector<Vec3d> wk = propagate_world(scene, 30.0 * 86400.0);
        // After 30 days the models must agree to well under 1% for planets
        // (same seed; purely gravitational perturbations are that small here).
        // The MOON is excluded: its mean-element ephemeris omits solar
        // perturbation physics, so its two models legitimately diverge more.
        for (size_t i = 1; i < 9; ++i) {
            const double d = std::sqrt(
                (wn[i][0] - wk[i][0]) * (wn[i][0] - wk[i][0]) +
                (wn[i][1] - wk[i][1]) * (wn[i][1] - wk[i][1]) +
                (wn[i][2] - wk[i][2]) * (wn[i][2] - wk[i][2]));
            const double r = std::sqrt(wk[i][0] * wk[i][0] + wk[i][1] * wk[i][1] +
                                       wk[i][2] * wk[i][2]);
            CHECK(d / r < 1e-2, "30d proximity <1%");
            ++total;
            if (i == 3) {
                std::printf("  earth 30d nbody-vs-kepler rel offset: %.3e\n", d / r);
            }
        }
    }

    // ---- I. Persistence of the gravity model (v0.8 optional field) ----
    gate("persist gravity model");
    {
        ScenarioSave s;
        s.gravity_model = "nbody";
        s.viz_mode = "orbital";
        const std::string ser = serialize_scenario(s);
        CHECK(ser.find("gravity_model=nbody\n") != std::string::npos, "field written");
        ScenarioSave back;
        CHECK(deserialize_scenario(ser, back) && back.gravity_model == "nbody",
              "roundtrip nbody");
        // Backward compatibility: a pre-v0.8 file (no gravity_model key) loads
        // with the KEPLER default — never rejected.
        std::string old_ser = ser;
        const size_t gm = old_ser.find("gravity_model=nbody\n");
        old_ser.erase(gm, std::strlen("gravity_model=nbody\n"));
        ScenarioSave back2;
        CHECK(deserialize_scenario(old_ser, back2) && back2.gravity_model == "kepler",
              "pre-v0.8 file -> kepler");
        // Unknown value = tamper: explicitly rejected, never silently kept.
        std::string bad = ser;
        bad.replace(bad.find("gravity_model=nbody"), 19, "gravity_model=cheat");
        ScenarioSave tmp;
        CHECK(!deserialize_scenario(bad, tmp), "unknown value rejected");
        // Unknown key still rejected (strictness preserved).
        std::string badk = ser;
        badk.replace(badk.find("gravity_model"), 13, "gravity_extra");
        CHECK(!deserialize_scenario(badk, tmp), "unknown key rejected");
        total += 5;
    }

    // ---- J. HUD binding of the gravity model ----
    gate("hud gravity rows");
    {
        HudSnapshot snap;
        snap.gravity_model = "kepler";
        HudState h = build_hud(snap);
        bool found_k = false, found_n = false;
        for (const HudRow& r : h.status) {
            if (r.label == "GRAVITY") {
                CHECK(r.available && std::string(r.value).find("KEPLER") != std::string::npos,
                      "kepler row value");
                found_k = true;
            }
        }
        snap.gravity_model = "nbody";
        h = build_hud(snap);
        for (const HudRow& r : h.status) {
            if (r.label == "GRAVITY") {
                CHECK(r.available && std::string(r.value).find("NBODY") != std::string::npos,
                      "nbody row value");
                CHECK(std::string(r.value).find("dt=3600") != std::string::npos,
                      "policy disclosed");
                found_n = true;
            }
        }
        CHECK(found_k && found_n, "gravity rows present");
        total += 4;
    }

    std::printf("v07_gates: %d checks, %d failures\n", total, g_fail);
    if (g_fail == 0) {
        std::printf("v07_gates: ALL %d checks PASSED\n", total);
        return 0;
    }
    return 1;
}
