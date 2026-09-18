// ASTRA COSMOS v1.3 — Destruction & Impact native mirror.
//
// Authority: astra.destruction (Python). Mirrors ONLY the deterministic,
// adapter-free scientific kernels:
//   geometry.py / energy.py / momentum.py / damage.py / fragmentation.py /
//   ejecta.py / system.py (execute_impact physics path + secondary-impact
//   straight-line sweep) / analysis.py (classify_fragment_orbit) /
//   config.py (defaults + validation) / limits.
// NOT mirrored (out of scope, honestly): core adapters (authority context,
// entity/world/event/persistence registrars), Python exception TYPES (kinds
// are mirrored via DestErr), RNG stream get_state/restore_state (opaque),
// NBodyDebrisSink, motion adapter.  Every mirrored output is SIMULATED_DATA
// provenance, exactly as the authority (this package never marks REAL_DATA).
//
// Domain validation mirrors the authority's checks & ERROR KINDS:
//   DestErr::IMPACT_VALIDATION ~ ImpactValidationError
//   DestErr::NUMERICAL         ~ NumericalError
//   DestErr::LIMIT_EXCEEDED    ~ LimitExceededError
// Vector magnitude = hypot(hypot(x,y),z), matching Vector3.magnitude()
// (same op chain as the v1.1-fixed relativity layer).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "py_mt19937.h"

namespace astra::app {

// Canonical constant (astra.physics.constants.GRAVITATIONAL_CONSTANT).
inline constexpr double DESTRUCTION_G = 6.67430e-11; // m^3 kg^-1 s^-2

enum class DestErr {
    OK = 0,
    IMPACT_VALIDATION, // ~ ImpactValidationError
    NUMERICAL,         // ~ NumericalError
    LIMIT_EXCEEDED,    // ~ LimitExceededError
};

struct DestVec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    double magnitude_sq() const { return x * x + y * y + z * z; }
    double magnitude() const;  // hypot(hypot(x,y),z) — Vector3 op chain
    double dot(const DestVec3& o) const { return x * o.x + y * o.y + z * o.z; }
    DestVec3 cross(const DestVec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    DestVec3 operator+(const DestVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    DestVec3 operator-(const DestVec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    DestVec3 operator-() const { return {-x, -y, -z}; }
    DestVec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    DestVec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    bool is_finite() const;
};

struct DestructionLimits {
    int    max_fragments_per_impact = 64;
    int    max_ejecta_per_impact = 128;
    int    max_debris_per_impact = 256;
    int    max_secondary_impacts = 32;
    int    max_recursion_depth = 4;
    double min_fragment_mass_kg = 1.0e-6;
    double min_impact_energy_j = 1.0e3;
    double min_relative_speed_m_s = 1.0e-3;
};

struct DestructionConfig {
    DestructionLimits limits;
    std::string model_version = "astra.destruction.v1";
    std::string rng_stream_name = "destruction.impact";
    double fragmentation_energy_fraction = 0.3; // of DEPOSITED energy
    double thermal_energy_fraction = 0.4;
    double momentum_transfer_efficiency = 0.5;
    double grazing_sin_threshold = 0.15;
    double head_on_angle_threshold_rad = 0.0872665; // ~5 deg
};

// validate() ~ DestructionConfig.validate (ValueError -> NUMERICAL).
DestErr dest_validate_config(const DestructionConfig& cfg);

struct DestructionImpactEvent {
    std::string impact_id, impactor_id, target_id;
    double sim_time_s = 0.0;
    double impactor_mass_kg = 0.0, target_mass_kg = 0.0;
    DestVec3 impactor_position, target_position;
    DestVec3 impactor_velocity, target_velocity;
    double impactor_radius_m = 0.0, target_radius_m = 0.0;

    DestVec3 relative_velocity() const { return impactor_velocity - target_velocity; }
    double   relative_speed() const { return relative_velocity().magnitude(); }
    // NumericalError when m1 + m2 <= 0 (exact authority check).
    DestErr  reduced_mass(double& out) const;
};

struct DestructionImpactGeometry {
    DestVec3 contact_point, surface_normal, incoming_direction;
    double incidence_angle_rad = 0.0;
    bool is_grazing = false, is_head_on = false;
};

struct DestructionImpactEnergy {
    double kinetic_energy_j = 0.0;
    double deposited_energy_j = 0.0;
    double fragmentation_energy_j = 0.0;
    double thermal_energy_j = 0.0;
    double residual_kinetic_energy_j = 0.0;
};

struct DestructionImpactMomentum {
    DestVec3 relative_momentum_kg_m_s, transferred_momentum_kg_m_s, residual_momentum_kg_m_s;
};

enum class DestructionDamageState { INTACT = 0, DAMAGED = 1, FRACTURED = 2, FRAGMENTED = 3, DESTROYED = 4 };

int          dest_damage_order(DestructionDamageState s);
DestErr      dest_damage_is_valid_transition(DestructionDamageState src, DestructionDamageState dst, bool& out);
DestErr      dest_damage_transition(DestructionDamageState src, DestructionDamageState dst, DestructionDamageState& out);
const char*  dest_damage_name(DestructionDamageState s);

struct DestructionFragment {
    int index = 0;
    double mass_kg = 0.0;
    DestVec3 position, velocity;
    double created_at_s = 0.0;
};

struct DestructionEjecta {
    int index = 0;
    double mass_kg = 0.0;
    DestVec3 position, velocity;
    double kinetic_energy_j = 0.0;
    double created_at_s = 0.0;
};

// ---- kernels ---------------------------------------------------------------

// validate_impact (system.py): ids, distinct bodies, positive masses,
// non-negative sim time, min relative speed, geometry computable.
DestErr dest_validate_impact(const DestructionImpactEvent& ev, const DestructionConfig& cfg);

// geometry.py: zero rel-velocity -> IMPACT_VALIDATION; coincident centres ->
// normal = -incoming (deterministic fallback); spherical/point contact rules.
DestErr dest_compute_impact_geometry(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                     DestructionImpactGeometry& out);

// energy.py: deposited_fraction must be in [0,1] (else NUMERICAL);
// negative-residual guard < -1e-9 * max(e_kin, 1.0) -> NUMERICAL.
DestErr dest_compute_impact_energy(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                   double deposited_fraction, DestructionImpactEnergy& out);

// momentum.py
DestErr dest_compute_impact_momentum(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                     DestructionImpactMomentum& out);

// system._decide_damage: specific deposited energy thresholds (model params)
// 1e3 / 1e5 / 1e7 J per kg; never moves backwards.
DestErr dest_decide_damage(DestructionDamageState before, const DestructionImpactEnergy& en,
                           const DestructionImpactEvent& ev, DestructionDamageState& after);

// fragmentation.py: deterministic count law + Dirichlet masses + isotropic,
// mean-subtracted velocities. rng drives EXACTLY the authority's draw order:
// n weights, then 3n direction components.
DestErr dest_determine_fragment_count(double fragmentation_energy_j, const DestructionConfig& cfg, int& out);
DestErr dest_fragment_target(const DestructionImpactEvent& ev, const DestructionImpactEnergy& en,
                             DestructionDamageState target_state_after, double target_radius_m,
                             const DestructionConfig& cfg, PyMt19937& rng,
                             std::vector<DestructionFragment>& out);

// ejecta.py: cosine-weighted hemisphere opposite the incoming direction;
// 3 draws per particle (u1, u2, speed fraction).
DestErr dest_generate_ejecta(const DestructionImpactEvent& ev, const DestructionImpactEnergy& en,
                             const DestructionConfig& cfg, PyMt19937& rng,
                             double ejecta_mass_fraction, double characteristic_speed_m_s,
                             std::vector<DestructionEjecta>& out);

// system._secondary_event_for_pair: straight-line periapsis test.
// candidate invalid -> returns OK with has=false (deterministic skip).
DestErr dest_secondary_event_for_pair(const DestructionImpactEvent& parent,
                                      const DestructionFragment& frag, int fragment_index,
                                      const DestructionImpactEvent& target_like, bool& has);

struct DestructionImpactResult {
    DestructionImpactGeometry geometry;
    DestructionImpactEnergy energy;
    DestructionImpactMomentum momentum;
    DestructionDamageState state_before = DestructionDamageState::INTACT;
    DestructionDamageState state_after = DestructionDamageState::INTACT;
    std::vector<DestructionFragment> fragments;
    std::vector<DestructionEjecta> ejecta;
    int debris_count = 0;      // fragments + ejecta (registration payload)
    int long_rng_consumed = 0; // diagnostic: draws consumed from the RNG stream
};

// execute_impact physics path (no adapters): validation -> energy floor
// (LIMIT_EXCEEDED below min_impact_energy_j) -> momentum -> geometry ->
// damage -> fragmentation (when state fractured+) -> ejecta -> debris cap.
// The seed argument semantics equal fragmentation.make_impact_rng(seed,
// "destruction.impact"): fresh RNGStream seeded with the int seed.
DestErr dest_execute_impact(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                            long long seed, DestructionDamageState current_state,
                            double ejecta_mass_fraction, double characteristic_ejecta_speed_m_s,
                            DestructionImpactResult& out);

// execute_secondary_impacts sweep: fragments (parent order) x targets
// (caller order), cap max_secondary, seed + case_index per child (child
// results use the same current damage state fetch as the system ledger —
// the native caller owns the ledger).
struct DestructionSecondaryResult {
    DestructionImpactEvent event;
    DestructionImpactResult result;
    int case_index = 0;
};
DestErr dest_execute_secondary_impacts(const DestructionImpactEvent& parent_event,
                                       const DestructionImpactResult& parent_result,
                                       const std::vector<DestructionImpactEvent>& targets,
                                       const std::vector<DestructionDamageState>& target_states,
                                       const DestructionConfig& cfg,
                                       long long seed, int max_secondary,
                                       std::vector<DestructionSecondaryResult>& out);

// analysis.classify_fragment_orbit: eps = 0.5 v^2 - mu / r, mu = G(M+m),
// atol 1e-9 J/kg. r == 0 -> NUMERICAL ("orbit undefined").
enum class DestructionOrbitClass { BOUND = 0, PARABOLIC = 1, HYPERBOLIC = 2 };
const char* dest_orbit_class_name(DestructionOrbitClass c);
DestErr dest_classify_fragment_orbit(const DestructionFragment& frag,
                                     double central_mass_kg,
                                     const DestVec3& central_position,
                                     const DestVec3& central_velocity,
                                     DestructionOrbitClass& out_class,
                                     double& out_eps, double& out_mu);

} // namespace astra::app
