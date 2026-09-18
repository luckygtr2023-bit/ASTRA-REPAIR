// evolution_sim.h — native mirror of astra.evolution (v1.3 domains 2+4:
// Universe Evolution + Galactic/Large-Scale Structure). Every formula,
// comparison, clamp, draw order and error boundary mirrors the Python
// authority exactly; every deviation is commented in the .cpp. NO invented
// physics: where the authority has no model there is NO native model
// either — the MISSING_PROVIDER state is a mirror surface, not a skip.
//
// Section map (authority file -> mirrored surface):
//   timestep.py:  AdaptiveTimestepController.choose — candidate chain:
//                   max_dt floor -> rate-limit (base/rate, clamp [min,max])
//                   -> event-bound (absolute-time delta w/ heuristic branch)
//                   -> eps-aware final clamp eps = 1e-12 * max(1.0, abs(rem)).
//                   Validation mirrors exactly (EvolutionNumericalError),
//                   including rem < min_dt returning rem (never clamp up).
//   models.py:    model registry (model_id ordering, step dispatch)
//   stellar.py:   main_sequence_lifetime = 10.0 * (m ** -2.5)  [pow, libm],
//                 remnant_for_mass thresholds 0.5/8/25 (NOTE: 0.5 is dead
//                 code relative to 8 in the authority — preserved verbatim),
//                 classify_stellar_phase (NUCLEAR_BURNING trigger, unc 0.2),
//                 make_stellar_step (age accumulation + one-shot MS fixation)
//   galaxy.py:    sfr = sfr * exp(-dt/tau), formed = sfr*dt*1e9*(1-R),
//                 Mgas = max(0, gas − sfr*dt*1e9 + inflow*dt*1e9),
//                 dZ = yield*sfr*dt*1e9/Mgas_new (guard >0), L scale,
//                 BH toy growth * (1 + 0.01*dt), morphology gas-poor drift
//                 (shift min(0.01*dt, spiral); renormalize iterating the
//                 dict's INSERTION order — deterministic: morphology_probs
//                 keys are inserted exactly as constructed by the caller)
//   structure.py: cluster (mass*(1+g*dt), members + int(0.5*dt) — CPython
//                   int() TRUNCATES toward zero; web (conditional new_time>10
//                   coarsening, voids + int(0.05*dt)); void (radius*(1+g*dt),
//                   delta = max(-1.0, delta - 0.01*dt)); halo (mass/concentration)
//   epoch.py:     EpochClassifier with advance_epoch default boundaries
//                 (0.1, 0.5, 0.7, 0.01) when none are supplied; ordering:
//                 STELLIFEROUS -> DECLINING -> DEGENERATE, then BH-dominated
//                 is checked BEFORE Dark Era: both-hold => BLACK_HOLE_DOMINATED
//                 precedence; DARK_ERA uses strict < so luminous == threshold
//                 stays DEGENERATE. NOTE — the epoch.py class docstring says
//                 "Dark-era takes precedence" but the shipped CODE checks BH
//                 first and returns BLACK_HOLE_DOMINATED; THE MIRROR FOLLOWS
//                 THE CODE (evidence over docstring; re-verified against the
//                 live authority by gen_evolution_reference.py E-rows:
//                 edge_bh_at -> BLACK_HOLE_DOMINATED, dark-only -> DARK_ERA).
//   engine.py:    evolve_object loop incl. history recording, while guard
//                 `t < until - 1e-12`, dt=min(decision, remaining) clamp,
//                 monotonic guard (t_next < t - 1e-12 -> LIMITATION),
//                 event budget checks at steps%1000==0 and steps>max,
//                 wall-clock soft warning SKIPPED (documented: the soft
//                 warning never raises => no observable effect on outputs;
//                 native runs HAVE the same wall-time budget checked by the
//                 caller-level perf gate, NOT inside the kernel).
//                 Authority mirror: provider=None -> require authority
//                 (native has no threading gate; same fail-closed semantics
//                 via a caller-passed gate flag — documented in .cpp).
//                 evolve_population/galaxy/cluster/web/void/supercluster
//                 with their exact synthesized default initial quantities.
//   config.py:    TimestepPolicy validate, PerformanceBudget validate.
//
// Quantity values: mirror provisions pass through UNITS + PROVENANCE words
// but only the numeric value + provenance enum drive numerical fidelity.
// Dates/times are cosmic_time_gyr (float Gyr) — SI conversion lives nowhere
// in the authority and is NOT invented here.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace astra::app {

// ---------------------------------------------------------------------------
// Errors (mirror of astra.evolution.errors taxonomy)
// ---------------------------------------------------------------------------
enum class EvolErr {
    OK = 0,
    VALIDATION,   // EvolutionValidationError
    AUTHORITY,    // EvolutionAuthorityError
    NUMERICAL,    // EvolutionNumericalError
    DEPENDENCY,   // EvolutionDependencyError
    LIMITATION,   // EvolutionLimitationError
};
const char* evol_err_name(EvolErr e);

// LimitationState ids (limitations.py, exact values)
enum class EvolLimitation {
    NONE = 0,
    OUTSIDE_VALID_RANGE,
    INSUFFICIENT_RESOLUTION,
    CAUSAL_INCONSISTENCY,
    UNSUPPORTED_REGIME,
    UNSUPPORTED_STRUCTURE,
    PROVIDER_REQUIRED_BUT_ABSENT,
    MISSING_DEPENDENCY,
};
const char* evol_limitation_name(EvolLimitation l);

// ---------------------------------------------------------------------------
// Config (config.py)
// ---------------------------------------------------------------------------
struct EvolTimestepPolicy {
    double base_dt_gyr = 0.01;
    double min_dt_gyr = 1e-6;
    double max_dt_gyr = 1.0;
    double rate_scale_floor = 1e-12;
    bool allow_event_driven = true;
    // validate(): ValueError in authority -> EVOLUTION_VALIDATION mirror class.
    EvolErr validate() const;
};

struct EvolPerformanceBudget {
    long long max_objects_evolved = 1000000;
    long long max_events_per_run = 100000;
    long long max_history_samples_per_object = 1024;
    double max_wall_time_s_per_gyr = 5.0;
    long long max_memory_mb = 2048;
    EvolErr validate() const;
};

struct EvolConfig {
    EvolTimestepPolicy timestep{};
    EvolPerformanceBudget budget{};
    // config.py EvolutionConfig defaults (verified: resolution "MEDIUM").
    std::string resolution = "MEDIUM";
    std::string model_version = "astra.evolution.v1";
    EvolErr validate() const;
};

// ---------------------------------------------------------------------------
// Provenance + quantities
// ---------------------------------------------------------------------------
enum class EvolProvenance {
    SIMULATED_DATA = 0, THEORETICAL, SPECULATIVE, DERIVED_DATA, REAL_DATA
};
const char* evol_provenance_name(EvolProvenance p);

struct EvolQuantity {
    double value = 0.0;
    EvolProvenance provenance = EvolProvenance::SIMULATED_DATA;
};

// ---------------------------------------------------------------------------
// Stellar phase / events / state (state.py)
// ---------------------------------------------------------------------------
enum class EvolStellarPhase {
    FORMATION = 0, MAIN_SEQUENCE, POST_MAIN_SEQUENCE, REMNANT,
    WHITE_DWARF, NEUTRON_STAR, BLACK_HOLE, UNKNOWN
};
const char* evol_stellar_phase_name(EvolStellarPhase p);

struct EvolStellarTransition {
    EvolStellarPhase before = EvolStellarPhase::MAIN_SEQUENCE;
    EvolStellarPhase after = EvolStellarPhase::UNKNOWN;
    double timescale_gyr = 0.0;
    bool present = false; // None in authority
};

// Ordered insertion-preserving map is unnecessary: call-sites only *read*
// morphology_probs in insertion order for renormalization — the .cpp keeps a
// small vector<pair> to reproduce exact visit order.
struct EvolMorphologyEntry { std::string key; double value; };

struct EvolState {
    std::string object_id;
    double cosmic_time_gyr = 0.0;
    std::string phase;   // last phase word (or before-phase words, verbatim)
    std::string model_id;
    EvolProvenance provenance = EvolProvenance::SIMULATED_DATA;
    // Named quantities, emitted in the reference exactly as constructed.
    std::vector<EvolMorphologyEntry> morphology_probs; // metadata
    std::map<std::string, EvolQuantity> quantities;
};

// ---------------------------------------------------------------------------
// Stellar (stellar.py)
// ---------------------------------------------------------------------------
EvolErr evol_main_sequence_lifetime_gyr(double mass_msun, double& out_gyr);
EvolErr evol_remnant_for_mass(double mass_msun, EvolStellarPhase& out);
EvolErr evol_classify_stellar_phase(double mass_msun, double age_gyr,
                                    EvolStellarTransition& out);
EvolErr evol_make_stellar_step(const EvolState& state, double dt_gyr,
                               const std::string& model_id, EvolState& out);

// ---------------------------------------------------------------------------
// Galaxy step (galaxy.py) with defaults sfr_tau=5, return 0.3, yield 0.02,
// inflow 0. Morphology renormalization iterates state's insertion order.
// ---------------------------------------------------------------------------
EvolErr evol_make_galaxy_step(const EvolState& state, double dt_gyr,
                              const std::string& model_id,
                              double sfr_tau_gyr, double return_fraction,
                              double yield_p, EvolState& out);
EvolErr evol_make_galaxy_step_default(const EvolState& state, double dt_gyr,
                                      const std::string& model_id, EvolState& out);

// ---------------------------------------------------------------------------
// Structure steps (structure.py)
// ---------------------------------------------------------------------------
EvolErr evol_make_cluster_step(const EvolState& state, double dt_gyr,
                               const std::string& model_id,
                               double cluster_growth_rate_per_gyr, EvolState& out);
EvolErr evol_make_cosmic_web_step(const EvolState& state, double dt_gyr,
                                  const std::string& model_id, EvolState& out);
EvolErr evol_make_void_step(const EvolState& state, double dt_gyr,
                            const std::string& model_id,
                            double void_growth_per_gyr, EvolState& out);
EvolErr evol_make_dark_matter_halo_step(const EvolState& state, double dt_gyr,
                                        const std::string& model_id,
                                        double halo_growth_per_gyr, EvolState& out);
EvolErr evol_make_linear_step(const EvolState& state, double dt_gyr,
                              const std::string& model_id, EvolState& out);

// ---------------------------------------------------------------------------
// Epoch classifier (epoch.py + engine.advance_epoch default boundaries)
// ---------------------------------------------------------------------------
enum class EvolEpoch {
    UNKNOWN = 0, STELLIFEROUS, DECLINING_STAR_FORMATION, DEGENERATE,
    BLACK_HOLE_DOMINATED, DARK_ERA, CUSTOM
};
const char* evol_epoch_name(EvolEpoch e);

struct EvolEpochBoundaries {
    double declining_sfr_threshold = 0.1;
    double degenerate_remnant_fraction = 0.5;
    double black_hole_dominated_fraction = 0.7;
    double dark_era_luminous_fraction = 0.01;
    std::string model_id = "astra.evolution.epoch.default";
    EvolErr validate() const;
};

EvolErr evol_classify_epoch(const EvolEpochBoundaries& b,
                            double cosmic_time_gyr,
                            bool has_sfr, double sfr_density,
                            bool has_remnant, double remnant_mass_fraction,
                            bool has_bh, double bh_mass_fraction,
                            bool has_luminous, double luminous_mass_fraction,
                            EvolEpoch& out);

// ---------------------------------------------------------------------------
// Timestep controller (timestep.py)
// ---------------------------------------------------------------------------
enum class EvolTimestepReason {
    CONFIGURED_MAX = 0, CONFIGURED_MIN, RATE_LIMITED, EVENT_DRIVEN, REMAINING_INTERVAL
};
const char* evol_timestep_reason_name(EvolTimestepReason r);

struct EvolTimestepDecision {
    double dt_gyr = 0.0;
    EvolTimestepReason reason = EvolTimestepReason::CONFIGURED_MAX;
};

EvolErr evol_choose_timestep(const EvolTimestepPolicy& p,
                             double remaining_gyr, double rate_scale,
                             bool has_next_event, double next_event_gyr,
                             bool has_current_time, double current_cosmic_time_gyr,
                             EvolTimestepDecision& out);

// ---------------------------------------------------------------------------
// Engine (engine.py): history + evolve loop for registered default models.
// model dispatch by model_id chain (same default registrations):
//   astra.evolution.linear.v1   -> linear step
//   astra.evolution.stellar.v1  -> stellar step (age_gyr/mass_msun quantities)
//   astra.evolution.galaxy.v1   -> galaxy step (default params)
//   astra.evolution.cluster.v1  -> cluster step (0.02)
//   astra.evolution.web.v1      -> cosmic web step
//   astra.evolution.void.v1     -> void step (0.01)
//   astra.evolution.halo.v1     -> halo step (0.02)
// ---------------------------------------------------------------------------
struct EvolEngine {
    EvolConfig cfg{};
    // History: capped at max_history_samples_per_object (drop-oldest policy).
    std::vector<EvolState> history;

    // evolve_object (rate_scale=1.0, no next_event) — mirrors engine.py
    // loop verbatim; returns final state. Fails with AUTHORITY when
    // authority_ok == false (native mirror of _require_authority shim).
    EvolErr evolve_object(const EvolState& initial, double until_cosmic_time_gyr,
                          const std::string& model_id, bool authority_ok,
                          EvolState& out);

    // _record_history with the budget-capped drop-oldest clause.
    void record_history(const EvolState& s);
};

} // namespace astra::app
