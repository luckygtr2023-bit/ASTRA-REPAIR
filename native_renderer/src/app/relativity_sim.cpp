#include "relativity_sim.h"

#include <cmath>

// Mirror-fidelity note (same contract as nbody_sim.cpp): every expression
// performs the same floating-point operations, in the same order, as the
// Python authority functions cited in the header. Python `**` on floats is
// multiplication here; left-associativity is preserved exactly.

namespace astra::app {

const double REL_G = 6.67430e-11;  // astra.physics.constants.GRAVITATIONAL_CONSTANT

const char* rel_err_name(RelErr e) {
    switch (e) {
        case RelErr::OK: return "OK";
        case RelErr::INVALID_VELOCITY: return "InvalidVelocityError";
        case RelErr::LIGHT_SPEED_VIOLATION: return "LightSpeedViolation";
        case RelErr::INVALID_REST_MASS: return "InvalidRestMassError";
        case RelErr::SPACELIKE_INTERVAL: return "SpacelikeIntervalError";
        case RelErr::DEGENERATE_METRIC: return "DegenerateMetricError";
    }
    return "?";
}

const char* relativity_model_name(RelativityModel m) {
    switch (m) {
        case RelativityModel::CLASSICAL: return "CLASSICAL";
        case RelativityModel::SPECIAL_RELATIVITY: return "SPECIAL_RELATIVITY";
        case RelativityModel::WEAK_FIELD: return "WEAK_FIELD";
        case RelativityModel::GENERAL_RELATIVITY: return "GENERAL_RELATIVITY";
    }
    return "?";
}

RelErr speed(double v, double& out) {
    if (std::isnan(v) || std::isinf(v)) return RelErr::INVALID_VELOCITY;
    out = v;
    return RelErr::OK;
}

RelErr beta(double v, double& out) {
    double s;
    const RelErr r = speed(v, s);
    if (r != RelErr::OK) return r;
    out = s / SPEED_OF_LIGHT;
    return RelErr::OK;
}

RelErr lorentz_factor(double v, double& out) {
    double b;
    const RelErr r = beta(v, b);
    if (r != RelErr::OK) return r;
    if (b >= 1.0) return RelErr::LIGHT_SPEED_VIOLATION;
    if (b < LOW_BETA_TAYLOR_THRESHOLD) {
        const double b2 = b * b;
        out = 1.0 + 0.5 * b2 + 0.375 * (b2 * b2);
        return RelErr::OK;
    }
    out = 1.0 / std::sqrt(1.0 - b * b);
    return RelErr::OK;
}

RelErr gamma_minus_one(double v, double& out) {
    double b;
    const RelErr r = beta(v, b);
    if (r != RelErr::OK) return r;
    if (b >= 1.0) return RelErr::LIGHT_SPEED_VIOLATION;
    if (b < LOW_BETA_TAYLOR_THRESHOLD) {
        const double b2 = b * b;
        out = 0.5 * b2 + 0.375 * (b2 * b2);
        return RelErr::OK;
    }
    out = 1.0 / std::sqrt(1.0 - b * b) - 1.0;
    return RelErr::OK;
}

static RelErr validate_rest_mass(double m0, double& out) {
    if (std::isnan(m0) || std::isinf(m0) || m0 < 0.0) return RelErr::INVALID_REST_MASS;
    out = m0;
    return RelErr::OK;
}

RelErr relativistic_mass(double m0, double v, double& out) {
    double m, g;
    RelErr r = validate_rest_mass(m0, m);
    if (r != RelErr::OK) return r;
    r = lorentz_factor(v, g);
    if (r != RelErr::OK) return r;
    out = m * g;
    return RelErr::OK;
}

RelErr total_energy(double m0, double v, double& out) {
    double rm;
    const RelErr r = relativistic_mass(m0, v, rm);
    if (r != RelErr::OK) return r;
    out = rm * C_SQUARED;
    return RelErr::OK;
}

RelErr kinetic_energy(double m0, double v, double& out) {
    double m, gm1;
    RelErr r = validate_rest_mass(m0, m);
    if (r != RelErr::OK) return r;
    r = gamma_minus_one(v, gm1);
    if (r != RelErr::OK) return r;
    out = gm1 * m * C_SQUARED;
    return RelErr::OK;
}

RelVec3 operator*(const RelVec3& v, double s) {
    return RelVec3{v.x * s, v.y * s, v.z * s};
}

double relvec_magnitude(const RelVec3& v) {
    // v1.2 fix: mirror astra.mathematics.vectors.Vector3.magnitude() EXACTLY —
    // the authority computes math.hypot(math.hypot(x, y), z), not sqrt(sumsq).
    // Both coincide on axial inputs; for general directions the hypot form is
    // the authority's op chain and is what the mirror must reproduce bitwise.
    // (sqrt(x²+y²+z²) could drift ±1ulp; all prior bit-exact rows used values
    // where both agree, so this cannot regress any load-bearing comparison:
    // the generator's expected values still come from the Python authority.)
    return std::hypot(std::hypot(v.x, v.y), v.z);
}

RelErr relativistic_momentum(double m0, const RelVec3& v, RelVec3& out) {
    double m, g;
    RelErr r = validate_rest_mass(m0, m);
    if (r != RelErr::OK) return r;
    r = lorentz_factor(relvec_magnitude(v), g);
    if (r != RelErr::OK) return r;
    out = v * (g * m);
    return RelErr::OK;
}

double RelFourVector::invariant_sq() const {
    return -(t * t) + (x * x) + (y * y) + (z * z);
}

IntervalType RelFourVector::interval_type(double tolerance) const {
    const double ds2 = invariant_sq();
    if (ds2 < -tolerance) return IntervalType::TIMELIKE;
    if (ds2 > tolerance) return IntervalType::SPACELIKE;
    return IntervalType::NULLI;
}

const char* interval_type_name(IntervalType t) {
    switch (t) {
        case IntervalType::TIMELIKE: return "TIMELIKE";
        case IntervalType::SPACELIKE: return "SPACELIKE";
        case IntervalType::NULLI: return "NULL";
    }
    return "?";
}

RelFourVector event_from_coordinates(double time_sec, double x, double y, double z) {
    return RelFourVector{time_sec * SPEED_OF_LIGHT, x, y, z};
}

RelErr proper_time_between(const RelFourVector& a, const RelFourVector& b, double& out_s) {
    const RelFourVector d{b.t - a.t, b.x - a.x, b.y - a.y, b.z - a.z};
    const double ds2 = d.invariant_sq();
    if (d.interval_type() == IntervalType::SPACELIKE) {
        return RelErr::SPACELIKE_INTERVAL;
    }
    out_s = std::sqrt(-ds2) / SPEED_OF_LIGHT;
    return RelErr::OK;
}

RelErr boost_x(const RelFourVector& v4, double v, RelFourVector& out) {
    double g;
    const RelErr r = lorentz_factor(std::fabs(v), g);  // authority: fabs(v)
    if (r != RelErr::OK) return r;
    const double b = v / SPEED_OF_LIGHT;
    out.t = g * (v4.t - b * v4.x);
    out.x = g * (v4.x - b * v4.t);
    out.y = v4.y;
    out.z = v4.z;
    return RelErr::OK;
}

RelErr inverse_boost_x(const RelFourVector& v4, double v, RelFourVector& out) {
    return boost_x(v4, -v, out);
}

RelErr schwarzschild_radius(double mass_kg, double& out_m) {
    if (std::isnan(mass_kg) || std::isinf(mass_kg) || mass_kg < 0.0) {
        return RelErr::DEGENERATE_METRIC;
    }
    if (mass_kg == 0.0) { out_m = 0.0; return RelErr::OK; }
    out_m = (2.0 * REL_G * mass_kg) / C_SQUARED;
    return RelErr::OK;
}

RelErr weak_field_time_dilation(double mass_kg, double r_m, double& out_ratio) {
    double rs;
    const RelErr r = schwarzschild_radius(mass_kg, rs);
    if (r != RelErr::OK) return r;
    if (std::isnan(r_m) || std::isinf(r_m) || r_m <= 0.0) return RelErr::DEGENERATE_METRIC;
    if (r_m <= rs) return RelErr::DEGENERATE_METRIC;
    out_ratio = 1.0 / std::sqrt(1.0 - (rs / r_m));
    return RelErr::OK;
}

RelErr proper_time_rate(double v, double& out) {
    // Derived arithmetic (mission Phase 7 sanction): dτ/dt = 1/γ — the
    // multiplicative inverse of the mirrored lorentz_factor output. Not a new
    // physical equation; classification in the UI remains SCIENTIFICALLY-
    // INTERPRETED (SR). γ is never < 1 here, so the range is (0, 1].
    double g;
    const RelErr r = lorentz_factor(v, g);
    if (r != RelErr::OK) return r;
    out = 1.0 / g;
    return RelErr::OK;
}

} // namespace astra::app
