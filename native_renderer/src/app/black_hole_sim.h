#pragma once
// ASTRA COSMOS — Black-Hole subsystems (native mirror of the Python
// scientific authority `astra.blackhole`).
//
// ARCHITECTURE RULE: the Python package `astra.blackhole` is the scientific
// authority. This file mirrors ONLY what exists there — nothing is invented:
//   models.py:       BlackHoleModel enum (SCHWARZSCHILD/KERR),
//                    NUMERICAL_HORIZON_EPSILON = 1.0e-9 m (absolute),
//                    EXTREMAL_SPIN_TOLERANCE = 1.0e-14 (dimensionless).
//   parameters.py:   validate_mass_kg (real, finite, > 0), validate_spin_param
//                    (real, finite, |a*| <= 1 — naked singularities rejected),
//                    BlackHoleState (immutable; model := spin==0.0 ? SCHW :
//                    KERR; gravitational_radius = (G*M)/(c*c);
//                    spin_length = a* * r_g). Canonical serialization is the
//                    (mass_kg, spin_param) pair only.
//   schwarzschild.py:_validate_outside_horizon (non-finite -> INVALID_GEOMETRY;
//                    r <= 0 or r <= horizon + eps -> COORDINATE_SINGULARITY),
//                    schwarzschild_radius_m (DELEGATES to the relativity
//                    layer's single r_s implementation — here: same call), isco
//                    (3 r_s), photon sphere (1.5 r_s), gravitational time
//                    dilation (delegates to the relativity layer after the
//                    black-hole guard), gravitational redshift
//                    z = sqrt((1 - rs/r_obs)/(1 - rs/r_em)) - 1 with the
//                    last-ulp negative clamps the authority documents.
//   kerr.py:         _clamped_sqrt (disc in (-tol, 0) -> 0.0; below -> plain
//                    ValueError => NEGATIVE_DISCRIMINANT), horizons
//                    r± = r_g ± sqrt(r_g² - a²), ergosphere r_E(θ),
//                    ZAMO frame-dragging ω (denominator guard + delta clamp of
//                    EXTREMAL_SPIN_TOLERANCE*r_g²), Bardeen-Press-Teukolsky
//                    ISCO (Z1/Z2 even in a*, orbit family selected by the
//                    SIGN OF THE ROOT: -1.0 = prograde, +1.0 = retrograde),
//                    photon orbits 2 r_g (1 + cos((2/3) arccos(∓a*))),
//                    equatorial static time dilation (invalid at/inside the
//                    EQUATORIAL ERGOSPHERE + eps — physical prohibition).
//   api.py:          facade dispatch on state.model; Schwarzschild boundary
//                    dict, Kerr boundary dict, ergosphere helper, dispatched
//                    time dilation, redshift (for KERR with a radius at/inside
//                    the equatorial ergosphere the authority DELEGATES to the
//                    Schwarzschild subsystem so its epsilon policy decides the
//                    exact error — the same delegation is mirrored), equatorial
//                    frame-dragging speed v = ω·r (exactly 0.0 for SCHW).
//
// ERROR MODEL: Python raises typed exceptions; the mirror returns an explicit
// BhErr enum instead (identical domain boundaries, no silent clamps).
// BhErr names map 1:1 to the authority exception classes:
//   INVALID_MASS            <- InvalidBlackHoleMassError
//   INVALID_SPIN            <- InvalidSpinParameterError
//   INVALID_GEOMETRY        <- InvalidGeometryInputError
//   COORDINATE_SINGULARITY  <- CoordinateSingularityError
//   NEGATIVE_DISCRIMINANT   <- plain ValueError of kerr._clamped_sqrt
// Bool-type rejection is a Python-typing nuance; in C++ inputs are double by
// construction, so the numeric domain boundaries are the load-bearing ones.
//
// CLASSIFICATION: PHYSICALLY-MODELED (static Schwarzschild exterior;
// Kerr geometry, test-particle approximation; equatorial ZAMO scope per
// authority). NO interior geometries, no Kerr-Newman, no arbitrary-inclination
// frame dragging (authority scope limits, mirrored).
// Determinism: pure functions, identical IEEE-754 op order, no RNG.

#include "relativity_sim.h"  // delegation: r_s and weak-field dilation

#include <string>

namespace astra::app {

// ---- Constants (mirror of astra/blackhole/models.py) -------------------------
inline constexpr double BH_NUMERICAL_HORIZON_EPSILON = 1.0e-9;   // m (absolute)
inline constexpr double BH_EXTREMAL_SPIN_TOLERANCE = 1.0e-14;
inline constexpr double BH_MAX_SPIN = 1.0;
inline constexpr double BH_G = 6.67430e-11;               // mirrors REL_G (physics.constants)

// ---- Error taxonomy ----------------------------------------------------------
enum class BhErr {
    OK,
    INVALID_MASS,
    INVALID_SPIN,
    INVALID_GEOMETRY,
    COORDINATE_SINGULARITY,
    NEGATIVE_DISCRIMINANT,
};
const char* bh_err_name(BhErr e);

// ---- Model taxonomy (mirror of astra/blackhole/models.py) --------------------
enum class BlackHoleModel { SCHWARZSCHILD, KERR };
const char* black_hole_model_name(BlackHoleModel m);

// ---- State (mirror of astra/blackhole/parameters.py) -------------------------
struct BlackHoleState {
    double mass_kg;      // validated: real, finite, > 0
    double spin_param;   // validated: real, finite, |a*| <= 1
    BlackHoleModel model() const;              // spin == 0.0 -> SCHWARZSCHILD
    double gravitational_radius() const;       // (G*M)/(c*c)
    double spin_length() const;                // a* * r_g
};
// Unless stated otherwise, every function takes ONLY pre-validated state
// (constructed here); matching the frozen Python dataclass.
BhErr bh_validate_mass_kg(double mass_kg, double& out);
BhErr bh_validate_spin_param(double spin_param, double& out);
BhErr bh_create_black_hole(double mass_kg, double spin_param, BlackHoleState& out);

// ---- Schwarzschild subsystem (mirror of schwarzschild.py) --------------------
// Shared horizon-policy validator. `horizon` in metres.
BhErr bh_validate_outside_horizon(double r, double horizon, double& out);
// r_s = 2GM/c^2 — delegates to the relativity layer's single implementation
// (mathematical continuity with astra.relativity.gr_foundations).
BhErr bh_schwarzschild_radius_m(double mass_kg, double& out_m);
BhErr bh_isco_radius(double mass_kg, double& out_m);           // 3 r_s
BhErr bh_photon_sphere_radius(double mass_kg, double& out_m);  // 1.5 r_s
// dt/dtau = 1/sqrt(1 - rs/r); bh guard then relativity-layer formula.
BhErr bh_gravitational_time_dilation_schwarzschild(const BlackHoleState& s, double radius, double& out);
// z = sqrt((1 - rs/r_obs)/(1 - rs/r_em)) - 1 (with last-ulp clamps).
BhErr bh_gravitational_redshift(const BlackHoleState& s, double r_emitter, double r_observer, double& out);

// ---- Kerr subsystem (mirror of kerr.py) --------------------------------------
BhErr bh_kerr_horizons(const BlackHoleState& s, double& out_r_plus, double& out_r_minus);
BhErr bh_kerr_ergosphere_radius(const BlackHoleState& s, double theta_rad, double& out_m);
// ZAMO angular velocity ω (rad/s) at (r, θ); equatorial-plane API per authority.
BhErr bh_kerr_frame_dragging_angular_velocity(const BlackHoleState& s, double radius, double theta_rad, double& out_rad_s);
BhErr bh_kerr_isco_prograde(const BlackHoleState& s, double& out_m);
BhErr bh_kerr_isco_retrograde(const BlackHoleState& s, double& out_m);
BhErr bh_kerr_photon_orbit_prograde(const BlackHoleState& s, double& out_m);
BhErr bh_kerr_photon_orbit_retrograde(const BlackHoleState& s, double& out_m);
// Kerr equatorial static observers: invalid at/inside the equatorial ergosphere.
BhErr bh_kerr_static_time_dilation_equatorial(const BlackHoleState& s, double radius, double& out);

// ---- Facade (mirror of api.py) -----------------------------------------------
struct SchwarzschildBoundaries {
    double schwarzschild_radius, gravitational_radius, photon_sphere_radius, isco_radius;
};
struct KerrBoundaries {
    double r_plus, r_minus, ergosphere_equatorial, ergosphere_polar,
           isco_prograde, isco_retrograde, photon_orbit_prograde, photon_orbit_retrograde;
};
BhErr bh_get_schwarzschild_boundaries(const BlackHoleState& s, SchwarzschildBoundaries& out);
BhErr bh_get_kerr_boundaries(const BlackHoleState& s, KerrBoundaries& out);
// Dispatched by model: KERR -> equatorial static (ergosphere guard),
// SCHWARZSCHILD -> horizon-guarded weak-field formula.
BhErr bh_gravitational_time_dilation(const BlackHoleState& s, double radius, double& out);
// v = ω r, equatorial plane; exactly 0.0 for the Schwarzschild model.
BhErr bh_equatorial_frame_dragging_velocity(const BlackHoleState& s, double radius, double& out_m_s);

} // namespace astra::app
