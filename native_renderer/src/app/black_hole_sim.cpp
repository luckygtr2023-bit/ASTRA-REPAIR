// Implementation detail notes: every expression preserves the authority's
// operation order. `x ** 2` in CPython resolves to pow(x, 2.0) == x*x exactly
// (correctly rounded libm), so `* *` is written explicitly where the authority
// wrote `** 2`. `(1/3)` powers map to std::pow(x, 1.0/3.0) — same glibc pow
// as CPython's float.__pow__, keeping the BPT ISCO chain bit-identical.

#include "black_hole_sim.h"

#include <cmath>

namespace astra::app {

const char* bh_err_name(BhErr e) {
    switch (e) {
        case BhErr::OK: return "OK";
        case BhErr::INVALID_MASS: return "ERR:InvalidBlackHoleMassError";
        case BhErr::INVALID_SPIN: return "ERR:InvalidSpinParameterError";
        case BhErr::INVALID_GEOMETRY: return "ERR:InvalidGeometryInputError";
        case BhErr::COORDINATE_SINGULARITY: return "ERR:CoordinateSingularityError";
        case BhErr::NEGATIVE_DISCRIMINANT: return "ERR:ValueError";  // kerr._clamped_sqrt plain ValueError
    }
    return "ERR:unknown";
}

const char* black_hole_model_name(BlackHoleModel m) {
    return m == BlackHoleModel::SCHWARZSCHILD ? "SCHWARZSCHILD" : "KERR";
}

// ---- parameters.py ------------------------------------------------------------
BhErr bh_validate_mass_kg(double mass_kg, double& out) {
    // Authority: bool/non-real -> type error (N/A in C++); NaN/Inf/m <= 0 reject.
    if (std::isnan(mass_kg) || std::isinf(mass_kg) || mass_kg <= 0.0)
        return BhErr::INVALID_MASS;
    out = mass_kg;
    return BhErr::OK;
}

BhErr bh_validate_spin_param(double spin_param, double& out) {
    if (std::isnan(spin_param) || std::isinf(spin_param)) return BhErr::INVALID_SPIN;
    if (std::fabs(spin_param) > BH_MAX_SPIN) return BhErr::INVALID_SPIN;
    out = spin_param;
    return BhErr::OK;
}

BhErr bh_create_black_hole(double mass_kg, double spin_param, BlackHoleState& out) {
    double m, a;
    const BhErr em = bh_validate_mass_kg(mass_kg, m);
    if (em != BhErr::OK) return em;
    const BhErr ea = bh_validate_spin_param(spin_param, a);
    if (ea != BhErr::OK) return ea;
    out.mass_kg = m;
    out.spin_param = a;
    return BhErr::OK;
}

BlackHoleModel BlackHoleState::model() const {
    // Authority: `if self.spin_param == 0.0` — exact-zero comparison, integer 0.
    if (spin_param == 0.0) return BlackHoleModel::SCHWARZSCHILD;
    return BlackHoleModel::KERR;
}

double BlackHoleState::gravitational_radius() const {
    // (GRAVITATIONAL_CONSTANT * mass_kg) / (SPEED_OF_LIGHT * SPEED_OF_LIGHT)
    return (BH_G * mass_kg) / (SPEED_OF_LIGHT * SPEED_OF_LIGHT);
}

double BlackHoleState::spin_length() const {
    return spin_param * gravitational_radius();
}

// ---- schwarzschild.py ----------------------------------------------------------
BhErr bh_validate_outside_horizon(double r, double horizon, double& out) {
    if (std::isnan(r) || std::isinf(r)) return BhErr::INVALID_GEOMETRY;
    if (r <= 0.0) return BhErr::COORDINATE_SINGULARITY;
    if (r <= horizon + BH_NUMERICAL_HORIZON_EPSILON) return BhErr::COORDINATE_SINGULARITY;
    out = r;
    return BhErr::OK;
}

BhErr bh_schwarzschild_radius_m(double mass_kg, double& out_m) {
    double m;
    const BhErr e = bh_validate_mass_kg(mass_kg, m);
    if (e != BhErr::OK) return e;
    // Delegation to the relativity layer's single r_s implementation.
    double rs;
    const RelErr er = schwarzschild_radius(m, rs);   // validated m cannot fail
    if (er != RelErr::OK) return BhErr::INVALID_MASS;  // defensive parity (unreachable)
    out_m = rs;
    return BhErr::OK;
}

BhErr bh_isco_radius(double mass_kg, double& out_m) {
    double rs;
    const BhErr e = bh_schwarzschild_radius_m(mass_kg, rs);
    if (e != BhErr::OK) return e;
    out_m = 3.0 * rs;
    return BhErr::OK;
}

BhErr bh_photon_sphere_radius(double mass_kg, double& out_m) {
    double rs;
    const BhErr e = bh_schwarzschild_radius_m(mass_kg, rs);
    if (e != BhErr::OK) return e;
    out_m = 1.5 * rs;
    return BhErr::OK;
}

BhErr bh_gravitational_time_dilation_schwarzschild(const BlackHoleState& s, double radius, double& out) {
    double rs;
    BhErr e = bh_schwarzschild_radius_m(s.mass_kg, rs);  // state pre-validated
    if (e != BhErr::OK) return e;
    double r;
    e = bh_validate_outside_horizon(radius, rs, r);
    if (e != BhErr::OK) return e;
    // Delegation: the formula lives once in the relativity layer.
    double ratio;
    const RelErr er = weak_field_time_dilation(s.mass_kg, r, ratio);
    if (er != RelErr::OK) return BhErr::COORDINATE_SINGULARITY;  // unreachable past guard
    out = ratio;
    return BhErr::OK;
}

BhErr bh_gravitational_redshift(const BlackHoleState& s, double r_emitter, double r_observer, double& out) {
    double rs;
    BhErr e = bh_schwarzschild_radius_m(s.mass_kg, rs);
    if (e != BhErr::OK) return e;
    double re, rob;
    e = bh_validate_outside_horizon(r_emitter, rs, re);
    if (e != BhErr::OK) return e;
    e = bh_validate_outside_horizon(r_observer, rs, rob);
    if (e != BhErr::OK) return e;
    double inner_em = 1.0 - rs / re;
    double inner_obs = 1.0 - rs / rob;
    // Belt-and-suspenders clamps (neutralize pathological last-ulp drift only).
    if (inner_em < 0.0) inner_em = 0.0;
    if (inner_obs < 0.0) inner_obs = 0.0;
    out = std::sqrt(inner_obs / inner_em) - 1.0;
    return BhErr::OK;
}

// ---- kerr.py -------------------------------------------------------------------
namespace {
// sqrt with extremal-Kerr protection (mirror of kerr._clamped_sqrt).
BhErr bh_clamped_sqrt(double discriminant, double& out) {
    if (discriminant < 0.0) {
        if (discriminant > -BH_EXTREMAL_SPIN_TOLERANCE) { out = 0.0; return BhErr::OK; }
        return BhErr::NEGATIVE_DISCRIMINANT;
    }
    out = std::sqrt(discriminant);
    return BhErr::OK;
}
} // namespace

BhErr bh_kerr_horizons(const BlackHoleState& s, double& out_r_plus, double& out_r_minus) {
    const double r_g = s.gravitational_radius();
    const double a = s.spin_length();
    double disc;
    const BhErr e = bh_clamped_sqrt(r_g * r_g - a * a, disc);
    if (e != BhErr::OK) return e;
    out_r_plus = r_g + disc;
    out_r_minus = r_g - disc;
    return BhErr::OK;
}

BhErr bh_kerr_ergosphere_radius(const BlackHoleState& s, double theta_rad, double& out_m) {
    if (std::isnan(theta_rad) || std::isinf(theta_rad)) return BhErr::INVALID_GEOMETRY;
    const double r_g = s.gravitational_radius();
    const double a = s.spin_length();
    const double c = std::cos(theta_rad);
    const double cos2 = c * c;                    // math.cos(theta) ** 2
    double disc;
    const BhErr e = bh_clamped_sqrt(r_g * r_g - a * a * cos2, disc);
    if (e != BhErr::OK) return e;
    out_m = r_g + disc;
    return BhErr::OK;
}

BhErr bh_kerr_frame_dragging_angular_velocity(const BlackHoleState& s, double radius, double theta_rad, double& out_rad_s) {
    if (std::isnan(radius) || std::isinf(radius)) return BhErr::INVALID_GEOMETRY;
    double r_plus, r_minus;
    BhErr e = bh_kerr_horizons(s, r_plus, r_minus);
    if (e != BhErr::OK) return e;
    if (radius <= r_plus + BH_NUMERICAL_HORIZON_EPSILON) return BhErr::COORDINATE_SINGULARITY;

    const double r_g = s.gravitational_radius();
    const double a = s.spin_length();
    const double r = radius;
    const double st = std::sin(theta_rad);
    const double sin2 = st * st;                  // math.sin(theta) ** 2
    double delta = r * r - 2.0 * r_g * r + a * a;
    if (delta < 0.0 && delta > -BH_EXTREMAL_SPIN_TOLERANCE * (r_g * r_g)) delta = 0.0;
    const double base = r * r + a * a;
    const double denom = base * base - a * a * delta * sin2;   // (r²+a²)**2 - ...
    out_rad_s = SPEED_OF_LIGHT * (2.0 * r_g * a * r) / denom;
    return BhErr::OK;
}

namespace {
BhErr bh_isco(const BlackHoleState& s, double root_sign, double& out_m) {
    const double a_star = s.spin_param;
    const double a2 = a_star * a_star;
    const double z1 = 1.0 + std::pow(1.0 - a2, 1.0 / 3.0) * (
        std::pow(1.0 + a_star, 1.0 / 3.0) + std::pow(1.0 - a_star, 1.0 / 3.0));
    const double z2 = std::sqrt(3.0 * a2 + z1 * z1);
    double root;
    const BhErr e = bh_clamped_sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2), root);
    if (e != BhErr::OK) return e;
    out_m = s.gravitational_radius() * (3.0 + z2 + root_sign * root);
    return BhErr::OK;
}

double bh_photon_orbit(const BlackHoleState& s, double a_star) {
    // 2 r_g (1 + cos((2/3) arccos(-a_star)))
    return 2.0 * s.gravitational_radius() * (1.0 + std::cos((2.0 / 3.0) * std::acos(-a_star)));
}
} // namespace

BhErr bh_kerr_isco_prograde(const BlackHoleState& s, double& out_m) { return bh_isco(s, -1.0, out_m); }
BhErr bh_kerr_isco_retrograde(const BlackHoleState& s, double& out_m) { return bh_isco(s, +1.0, out_m); }
BhErr bh_kerr_photon_orbit_prograde(const BlackHoleState& s, double& out_m) {
    out_m = bh_photon_orbit(s, s.spin_param);
    return BhErr::OK;
}
BhErr bh_kerr_photon_orbit_retrograde(const BlackHoleState& s, double& out_m) {
    out_m = bh_photon_orbit(s, -s.spin_param);
    return BhErr::OK;
}

BhErr bh_kerr_static_time_dilation_equatorial(const BlackHoleState& s, double radius, double& out) {
    if (std::isnan(radius) || std::isinf(radius)) return BhErr::INVALID_GEOMETRY;
    // Authority calls ergosphere_radius(state, math.pi / 2); pi/2 in double is
    // 1.5707963267948966 and the division by 2 is exact.
    double ergo;
    const BhErr ee = bh_kerr_ergosphere_radius(s, 1.5707963267948966, ergo);  // math.pi / 2
    if (ee != BhErr::OK) return ee;
    if (radius <= ergo + BH_NUMERICAL_HORIZON_EPSILON) return BhErr::COORDINATE_SINGULARITY;
    out = 1.0 / std::sqrt(1.0 - 2.0 * s.gravitational_radius() / radius);
    return BhErr::OK;
}

// ---- api.py facade --------------------------------------------------------------
BhErr bh_get_schwarzschild_boundaries(const BlackHoleState& s, SchwarzschildBoundaries& out) {
    double rs, ps, isco;
    BhErr e = bh_schwarzschild_radius_m(s.mass_kg, rs);
    if (e != BhErr::OK) return e;
    e = bh_photon_sphere_radius(s.mass_kg, ps);
    if (e != BhErr::OK) return e;
    e = bh_isco_radius(s.mass_kg, isco);
    if (e != BhErr::OK) return e;
    out.schwarzschild_radius = rs;
    out.gravitational_radius = 0.5 * rs;
    out.photon_sphere_radius = ps;
    out.isco_radius = isco;
    return BhErr::OK;
}

BhErr bh_get_kerr_boundaries(const BlackHoleState& s, KerrBoundaries& out) {
    BhErr e = bh_kerr_horizons(s, out.r_plus, out.r_minus);
    if (e != BhErr::OK) return e;
    e = bh_kerr_ergosphere_radius(s, 1.5707963267948966, out.ergosphere_equatorial);   // math.pi/2
    if (e != BhErr::OK) return e;
    e = bh_kerr_ergosphere_radius(s, 0.0, out.ergosphere_polar);
    if (e != BhErr::OK) return e;
    e = bh_kerr_isco_prograde(s, out.isco_prograde);
    if (e != BhErr::OK) return e;
    e = bh_kerr_isco_retrograde(s, out.isco_retrograde);
    if (e != BhErr::OK) return e;
    e = bh_kerr_photon_orbit_prograde(s, out.photon_orbit_prograde);
    if (e != BhErr::OK) return e;
    e = bh_kerr_photon_orbit_retrograde(s, out.photon_orbit_retrograde);
    if (e != BhErr::OK) return e;
    return BhErr::OK;
}

BhErr bh_gravitational_time_dilation(const BlackHoleState& s, double radius, double& out) {
    if (s.model() == BlackHoleModel::KERR)
        return bh_kerr_static_time_dilation_equatorial(s, radius, out);
    return bh_gravitational_time_dilation_schwarzschild(s, radius, out);
}

BhErr bh_equatorial_frame_dragging_velocity(const BlackHoleState& s, double radius, double& out_m_s) {
    if (s.model() == BlackHoleModel::SCHWARZSCHILD) { out_m_s = 0.0; return BhErr::OK; }
    double omega;
    const BhErr e = bh_kerr_frame_dragging_angular_velocity(s, radius, 1.5707963267948966, omega);
    if (e != BhErr::OK) return e;
    out_m_s = omega * radius;
    return BhErr::OK;
}

} // namespace astra::app
