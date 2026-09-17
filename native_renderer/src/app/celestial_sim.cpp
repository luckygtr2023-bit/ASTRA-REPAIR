#include "celestial_sim.h"

#include <cmath>

namespace astra::app {
namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;
constexpr double KEPLER_TOL = 1.0e-12;   // astra/orbital/constants.py KEPLER_TOL
constexpr int KEPLER_MAX_ITER = 200;     // astra/orbital/constants.py KEPLER_MAX_ITER
constexpr double DEG = PI / 180.0;

// Mirror of astra/orbital/elements.py::_rotation_pqw_to_ijk applied to a vector.
Vec3d rotation_pqw_to_ijk(double i, double raan, double argp, Vec3d v) {
    const double cO = std::cos(raan), sO = std::sin(raan);
    const double ci = std::cos(i),    si = std::sin(i);
    const double cw = std::cos(argp), sw = std::sin(argp);
    // Matrix rows exactly as in the Python authority.
    const double m00 = cO * cw - sO * ci * sw;
    const double m01 = -cO * sw - sO * ci * cw;
    const double m02 = sO * si;
    const double m10 = sO * cw + cO * ci * sw;
    const double m11 = -sO * sw + cO * ci * cw;
    const double m12 = -cO * si;
    const double m20 = si * sw;
    const double m21 = si * cw;
    const double m22 = ci;
    return {m00 * v[0] + m01 * v[1] + m02 * v[2],
            m10 * v[0] + m11 * v[1] + m12 * v[2],
            m20 * v[0] + m21 * v[1] + m22 * v[2]};
}
} // namespace

double solve_kepler_elliptic(double mean_anomaly, double e) {
    // Mirror of astra/orbital/kepler.py::solve_kepler_elliptic.
    double M = mean_anomaly;
    double M_wrapped = std::fmod(M + PI, TWO_PI);
    if (M_wrapped < 0.0) M_wrapped += TWO_PI;
    M_wrapped -= PI;
    double E = (e < 0.8) ? M_wrapped
                         : (M_wrapped != 0.0 ? std::copysign(PI, M_wrapped) : 0.0);
    for (int iter = 0; iter < KEPLER_MAX_ITER; ++iter) {
        const double f = E - e * std::sin(E) - M_wrapped;
        const double fp = 1.0 - e * std::cos(E);
        if (fp == 0.0) break;
        const double dE = -f / fp;
        E += dE;
        if (std::fabs(dE) < KEPLER_TOL) return E;
    }
    // The Python authority raises KeplerConvergenceError here. The native app
    // degrades to the best estimate and keeps running (renderer must not die).
    return E;
}

double eccentric_to_true(double E, double e) {
    // Mirror of astra/orbital/anomalies.py::eccentric_to_true.
    const double half = 0.5 * E;
    const double x = std::sqrt((1.0 + e) / (1.0 - e)) * std::tan(half);
    return 2.0 * std::atan(x);
}

double mean_motion(const OrbitalElements& el) {
    return std::sqrt(el.mu / (el.a_km * el.a_km * el.a_km));
}

Vec3d orbital_position(const OrbitalElements& el, double t_s) {
    // Mirror of astra/orbital/propagation.py (elements advance by n*dt) +
    // astra/orbital/elements.py::elements_to_state (elliptic branch).
    // Primaries (a == 0, e.g. the Sun) have degenerate elements and sit at the
    // frame origin by definition — guard before sqrt(mu/a^3) would produce
    // inf/NaN (found by the v0.3 fidelity gate; previously hidden because
    // NaN comparisons are always false).
    if (!(el.a_km > 0.0)) return {0.0, 0.0, 0.0};
    const double M = el.M0 + mean_motion(el) * (t_s - el.epoch_s);
    const double E = solve_kepler_elliptic(M, el.e);
    const double nu = eccentric_to_true(E, el.e);
    const double p = el.a_km * (1.0 - el.e * el.e); // semi-latus rectum
    const double r = p / (1.0 + el.e * std::cos(nu));
    const Vec3d r_pqw = {r * std::cos(nu), r * std::sin(nu), 0.0};
    return rotation_pqw_to_ijk(el.i, el.raan, el.argp, r_pqw);
}

Vec3d orbital_velocity(const OrbitalElements& el, double t_s) {
    // Mirror of astra/orbital/elements.py::elements_to_state velocity branch.
    if (!(el.a_km > 0.0)) return {0.0, 0.0, 0.0}; // primary at rest at origin
    const double M = el.M0 + mean_motion(el) * (t_s - el.epoch_s);
    const double E = solve_kepler_elliptic(M, el.e);
    const double nu = eccentric_to_true(E, el.e);
    const double p = el.a_km * (1.0 - el.e * el.e);
    const double vf = std::sqrt(el.mu / p);
    const Vec3d v_pqw = {-vf * std::sin(nu), vf * (el.e + std::cos(nu)), 0.0};
    return rotation_pqw_to_ijk(el.i, el.raan, el.argp, v_pqw);
}

std::vector<Vec3d> orbit_polyline(const OrbitalElements& el, double t_s, int n) {
    std::vector<Vec3d> out;
    out.reserve(static_cast<size_t>(n));
    const double M_center = el.M0 + mean_motion(el) * (t_s - el.epoch_s);
    const double period = TWO_PI / mean_motion(el);
    for (int k = 0; k < n; ++k) {
        // Evenly spaced in mean anomaly — same density at peri/apo as the sim.
        const double tk = t_s + period * (static_cast<double>(k) / n);
        (void)M_center;
        out.push_back(orbital_position(el, tk));
    }
    return out;
}

static OrbitalElements planet_elements(double a_au, double e, double i_deg,
                                       double L_deg, double varpi_deg,
                                       double raan_deg) {
    // JPL table gives i, L (mean longitude), varpi (longitude of perihelion),
    // Omega; M0 = L - varpi, argp = varpi - Omega (documented transform).
    return OrbitalElements{a_au * AU_KM, e, i_deg * DEG, raan_deg * DEG,
                           (varpi_deg - raan_deg) * DEG, (L_deg - varpi_deg) * DEG,
                           GM_SUN, 0.0};
}

std::vector<CelestialBody> make_solar_system() {
    std::vector<CelestialBody> b;
    const std::string CLS = "DATA-DERIVED elements (JPL approx., J2000) · SIMULATED two-body Kepler";
    b.push_back({"Sun", BodyKind::STAR, -1, 1.9885e30, 695700.0, 5778.0,
                 {1.00f, 0.85f, 0.55f},
                 {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, GM_SUN, 0.0},
                 "REAL star · DATA-DERIVED (NASA fact sheet)"});
    b.push_back({"Mercury", BodyKind::PLANET, 0, 3.3011e23, 2439.7, 0.0,
                 {0.72f, 0.68f, 0.63f}, planet_elements(0.387099, 0.205636, 7.0049, 252.251, 77.4575, 48.3309), CLS});
    b.push_back({"Venus", BodyKind::PLANET, 0, 4.8675e24, 6051.8, 0.0,
                 {0.90f, 0.78f, 0.55f}, planet_elements(0.723336, 0.006777, 3.39468, 181.980, 131.564, 76.6799), CLS});
    b.push_back({"Earth", BodyKind::PLANET, 0, 5.9724e24, 6371.0, 0.0,
                 {0.35f, 0.55f, 0.95f}, planet_elements(1.000003, 0.016711, -0.00002, 100.464, 102.937, 0.0), CLS});
    b.push_back({"Mars", BodyKind::PLANET, 0, 6.4171e23, 3389.5, 0.0,
                 {0.90f, 0.45f, 0.25f}, planet_elements(1.523710, 0.093394, 1.84973, -4.55343, -23.9436, 49.5596), CLS});
    b.push_back({"Jupiter", BodyKind::PLANET, 0, 1.8982e27, 69911.0, 0.0,
                 {0.85f, 0.70f, 0.55f}, planet_elements(5.20289, 0.048386, 1.30440, 34.39644, 14.72848, 100.4739), CLS});
    b.push_back({"Saturn", BodyKind::PLANET, 0, 5.6834e26, 58232.0, 0.0,
                 {0.90f, 0.82f, 0.62f}, planet_elements(9.53668, 0.053862, 2.48599, 49.95424, 92.59888, 113.6624), CLS});
    b.push_back({"Uranus", BodyKind::PLANET, 0, 8.6810e25, 25362.0, 0.0,
                 {0.62f, 0.82f, 0.90f}, planet_elements(19.1892, 0.047257, 0.772637, 313.2381, 170.9543, 74.0169), CLS});
    b.push_back({"Neptune", BodyKind::PLANET, 0, 1.0241e26, 24622.0, 0.0,
                 {0.40f, 0.50f, 0.95f}, planet_elements(30.0699, 0.008590, 1.770042, -55.1200, 44.96476, 131.7841), CLS});
    // Moon — mean elements J2000 (APPROXIMATE). Parent = Earth (index 3).
    b.push_back({"Moon", BodyKind::MOON, 3, 7.3477e22, 1737.4, 0.0,
                 {0.80f, 0.80f, 0.80f},
                 OrbitalElements{384400.0, 0.0549, 5.145 * DEG, 125.08 * DEG,
                                 318.27 * DEG, 32.02 * DEG, GM_EARTH, 0.0},
                 "DATA-DERIVED (NASA fact sheet, approx. mean elements) · SIMULATED two-body Kepler"});
    return b;
}

std::vector<Vec3d> propagate_world(const std::vector<CelestialBody>& bodies, double t_s) {
    std::vector<Vec3d> world(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        const Vec3d rel = orbital_position(bodies[i].elements, t_s);
        if (bodies[i].parent >= 0) {
            const Vec3d& pw = world[static_cast<size_t>(bodies[i].parent)];
            world[i] = {pw[0] + rel[0], pw[1] + rel[1], pw[2] + rel[2]};
        } else {
            world[i] = rel;
        }
    }
    return world;
}

std::vector<Vec3d> propagate_world_velocity(const std::vector<CelestialBody>& bodies, double t_s) {
    std::vector<Vec3d> world(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        const Vec3d rel = orbital_velocity(bodies[i].elements, t_s);
        if (bodies[i].parent >= 0) {
            const Vec3d& pw = world[static_cast<size_t>(bodies[i].parent)];
            world[i] = {pw[0] + rel[0], pw[1] + rel[1], pw[2] + rel[2]};
        } else {
            world[i] = rel;
        }
    }
    return world;
}

double SimClock::clamp_warp(double w) {
    return w < 1.0 ? 1.0 : (w > 1.0e8 ? 1.0e8 : w);
}

} // namespace astra::app
