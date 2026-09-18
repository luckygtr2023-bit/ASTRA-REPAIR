#pragma once
// ASTRA COSMOS — Special Relativity + weak-field foundations (native mirror of
// the Python scientific authority `astra.relativity`).
//
// ARCHITECTURE RULE: the Python package `astra.relativity` is the scientific
// authority. This file mirrors ONLY what exists there — nothing is invented:
//   core.py:            SPEED_OF_LIGHT (exact SI), C_SQUARED, speed (|v|,
//                       NaN/Inf rejected), beta = v/c, lorentz_factor with the
//                       low-beta Taylor branch (threshold 1e-5, identical
//                       coefficients), gamma-minus-one series for
//                       cancellation-free KE, relativistic mass, total/KE,
//                       momentum (gamma m0 v).
//   four_vectors.py:    FourVector(t,x,y,z) signature (-,+,+,+), invariant_sq,
//                       interval_type (tol 1e-9), SpacetimeEvent ct convention,
//                       proper_time_to (spacelike = error, null = 0).
//   lorentz.py:         boost_x / inverse_boost_x (passive, X-axis only —
//                       arbitrary 3D boosts are explicitly NOT in the
//                       authority and therefore NOT here).
//   gr_foundations.py:  schwarzschild_radius (2GM/c^2), weak_field_time_dilation
//                       (1/sqrt(1 - rs/r); exterior only; r <= rs = error).
//   models.py:          RelativityModel enum (CLASSICAL/SR/WEAK_FIELD/GR).
//
// ERROR MODEL: Python raises typed exceptions; the mirror returns an explicit
// RelError enum + message instead (identical domain boundaries, no silent
// clamps — v >= c, negative/non-finite rest mass, spacelike proper time,
// degenerate metric inputs all FAIL LOUDLY, exactly like the authority).
//
// CLASSIFICATION: PHYSICALLY-MODELED (Special Relativity, Lorentz geometry,
// Schwarzschild exterior) — never presented as gravitational N-body truth;
// the production gravity engine remains Newtonian N-body / Kepler (documented
// boundary per mission Phase 8).
// Determinism: pure functions, same op order as the authority, no RNG —
// IEEE-754 semantics identical across platforms; fidelity proven by
// tests/relativity_mirror_check.cpp vs scripts/gen_relativity_reference.py.

#include <string>

namespace astra::app {

// ---- Constants (mirror of astra/relativity/core.py) -------------------------
inline constexpr double SPEED_OF_LIGHT = 299792458.0;   // m/s — exact SI (CODATA 2018)
inline constexpr double C_SQUARED = SPEED_OF_LIGHT * SPEED_OF_LIGHT;
inline constexpr double LOW_BETA_TAYLOR_THRESHOLD = 1.0e-5;

// ---- Error taxonomy (mirror of astra/relativity/exceptions.py) ---------------
enum class RelErr {
    OK,
    INVALID_VELOCITY,      // NaN / Inf velocity
    LIGHT_SPEED_VIOLATION, // massive object at v >= c
    INVALID_REST_MASS,     // negative / non-finite rest mass
    SPACELIKE_INTERVAL,    // proper time requested for spacelike separation
    DEGENERATE_METRIC,     // negative mass / r <= 0 / r <= rs
};
const char* rel_err_name(RelErr e);

// ---- Model taxonomy (mirror of astra/relativity/models.py) -------------------
enum class RelativityModel { CLASSICAL, SPECIAL_RELATIVITY, WEAK_FIELD, GENERAL_RELATIVITY };
const char* relativity_model_name(RelativityModel m);

// ---- Core scalar utilities ---------------------------------------------------
// speed: magnitude semantics; NaN/Inf rejected. Sign tolerated (formulas use
// beta^2) — authority behaviour identical.
RelErr speed(double v, double& out);
RelErr beta(double v, double& out);
RelErr lorentz_factor(double v, double& out);
// Cancellation-free (gamma - 1): series branch at low beta, never gamma-1.
RelErr gamma_minus_one(double v, double& out);
RelErr relativistic_mass(double m0, double v, double& out); // gamma * m0
RelErr total_energy(double m0, double v, double& out);      // gamma m0 c^2
RelErr kinetic_energy(double m0, double v, double& out);    // (gamma-1) m0 c^2

// ---- Vectors (spatial, SI metres) -------------------------------------------
struct RelVec3 { double x = 0.0, y = 0.0, z = 0.0; };
RelVec3 operator*(const RelVec3& v, double s);
double relvec_magnitude(const RelVec3& v);  // sqrt(x^2+y^2+z^2), same op order
// Momentum p = gamma m0 v (mirror of relativistic_momentum).
RelErr relativistic_momentum(double m0, const RelVec3& v, RelVec3& out);

// ---- Four-vectors (mirror of four_vectors.py; t in metres of light, ct) -----
enum class IntervalType { TIMELIKE, SPACELIKE, NULLI };  // NULLI == NULL (keyword)
const char* interval_type_name(IntervalType t);

struct RelFourVector {
    double t = 0.0, x = 0.0, y = 0.0, z = 0.0;
    double invariant_sq() const;              // -t^2 + x^2 + y^2 + z^2
    IntervalType interval_type(double tolerance = 1e-9) const;
};

// SpacetimeEvent::from_coordinates — t = time_sec * c.
RelFourVector event_from_coordinates(double time_sec, double x, double y, double z);
// Proper time along the straight worldline: spacelike -> SPACELIKE_INTERVAL,
// null -> 0.0, timelike -> sqrt(-ds^2)/c.
RelErr proper_time_between(const RelFourVector& a, const RelFourVector& b, double& out_s);

// ---- Lorentz boosts (X-axis passive, mirror of lorentz.py) -------------------
RelErr boost_x(const RelFourVector& v4, double v, RelFourVector& out);
RelErr inverse_boost_x(const RelFourVector& v4, double v, RelFourVector& out);

// ---- Weak-field / Schwarzschild exterior (mirror of gr_foundations.py) -------
extern const double REL_G;  // GRAVITATIONAL_CONSTANT from astra.physics.constants
RelErr schwarzschild_radius(double mass_kg, double& out_m);
RelErr weak_field_time_dilation(double mass_kg, double r_m, double& out_ratio);

// Convenience READ-ONLY compositions for the inspector (clearly classified
// SCIENTIFICALLY-INTERPRETED in the UI; they are arithmetic on the above,
// never new physics): proper-time rate dτ/dt = 1/gamma (same mass object),
// = inverse of lorentz behavior... 1/γ computed directly from γ.
RelErr proper_time_rate(double v, double& out);  // dτ/dt = 1/γ(v)

} // namespace astra::app
