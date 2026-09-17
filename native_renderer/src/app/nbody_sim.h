#pragma once
// ASTRA COSMOS — Native N-body engine (native mirror of the Python authority).
//
// ARCHITECTURE RULE: the Python package `astra.nbody` is the scientific
// authority. This file mirrors its exact algorithms so the standalone native
// app reproduces the same trajectories for the same initial state:
//   - pairwise acceleration:    astra/nbody/gravity.py::pairwise_acceleration
//       r_vec = r_j - r_i; denom = |r_vec|^2 + eps^2; a_i = G m_j r_vec/denom^1.5
//   - acceleration summation:   astra/nbody/gravity.py::compute_accelerations
//       (index-ordered pairs i < j, accumulation into fixed positions)
//   - integrator:               astra/nbody/integration.py::velocity_verlet_step
//       (half-kick, drift, recompute, second half-kick; symplectic, 2nd order)
//   - constants:                astra/physics/constants.py
//       GRAVITATIONAL_CONSTANT = 6.67430e-11  m^3 kg^-1 s^-2  (SI)
//       DEFAULT_SOFTENING      = 1.0e-6       m
//   - diagnostics:              astra/nbody/diagnostics.py (energy, momentum)
// UNITS: strict SI (metres, m/s, kg, s) INSIDE this module — identical to the
// authority, kilometre conversion happens only at the celestial_sim boundary.
// Fidelity is enforced by tests/nbody_mirror_check.cpp against reference
// trajectories produced by scripts/gen_nbody_reference.py (the Python engine).
//
// CLASSIFICATION of anything produced here: SIMULATED (Newtonian point-mass
// gravity, Plummer-softened, velocity Verlet). No relativistic terms.
// Determinism: fixed-order O(N^2) pair loop, no RNG, no wall-clock — same
// inputs produce bit-identical outputs on any IEEE-754 double platform.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace astra::app {

// ---- Constants (mirror of astra/physics/constants.py) -----------------------
inline constexpr double G_SI = 6.67430e-11;        // m^3 kg^-1 s^-2 — REAL (CODATA 2018)
inline constexpr double DEFAULT_SOFTENING_M = 1.0e-6; // m — authority default

// ---- Vector (SI metres / metres-per-second) ---------------------------------
struct Vec3m { double x = 0.0, y = 0.0, z = 0.0; };
Vec3m operator+(const Vec3m& a, const Vec3m& b);
Vec3m operator-(const Vec3m& a, const Vec3m& b);
Vec3m operator*(const Vec3m& a, double s);
double magnitude_sq(const Vec3m& v);
double magnitude(const Vec3m& v);

// ---- Body (mirror of astra/nbody/bodies.py::NBodyBody) -----------------------
// The Python body is immutable (frozen dataclass); the C++ engine mutates an
// owned list in place. Equivalence is numerical, proven by the mirror check.
struct NBody {
    std::string id;          // unique, stable iteration order
    double mass_kg = 0.0;    // must be positive finite
    Vec3m position;          // m
    Vec3m velocity;          // m/s
};

// Raised-as-return: authority raises NBodySingularityError when eps==0 and
// separation==0. We cannot throw across the mirror boundary semantics-lite;
// instead compute_accelerations reports the singularity via out-param.
struct NBodyResult { bool ok = true; std::string error; };

// Mirror of astra/nbody/gravity.py::compute_accelerations.
NBodyResult compute_accelerations(const std::vector<NBody>& bodies,
                                  double G, double softening_m,
                                  std::vector<Vec3m>& out_acc);
// Mirror of astra/nbody/gravity.py::pairwise_acceleration (single pair).
NBodyResult pairwise_acceleration(const Vec3m& pos_i, const Vec3m& pos_j,
                                  double m_i, double m_j,
                                  double G, double softening_m,
                                  Vec3m& out_ai, Vec3m& out_aj);
// Mirror of astra/nbody/integration.py::_validate_dt.
bool is_valid_dt(double dt);
// Mirror of astra/nbody/integration.py::velocity_verlet_step.
// Requires acc_valid: pass cached accelerations for bodies (from the previous
// step or compute first) to match the authority's caching semantics exactly.
NBodyResult velocity_verlet_step(std::vector<NBody>& bodies,
                                 std::vector<Vec3m>& acc_cache,
                                 double dt, double G, double softening_m);

// ---- Diagnostics (mirror of astra/nbody/diagnostics.py) ----------------------
double kinetic_energy(const std::vector<NBody>& bodies);            // J
NBodyResult total_potential(const std::vector<NBody>& bodies,
                            double G, double softening_m, double& out_J);
NBodyResult total_energy(const std::vector<NBody>& bodies,
                         double G, double softening_m, double& out_J);
Vec3m total_linear_momentum(const std::vector<NBody>& bodies);      // kg m/s
Vec3m total_angular_momentum(const std::vector<NBody>& bodies);     // kg m^2/s

} // namespace astra::app
