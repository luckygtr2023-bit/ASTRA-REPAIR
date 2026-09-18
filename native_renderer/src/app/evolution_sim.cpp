// evolution_sim.cpp — native mirror of astra.evolution (v1.3 domains 2+4).
// Bit-parity pins (all verified against the reference CSV):
//   * float ** : CPython float_pow -> libm pow; same bits on glibc.
//   * math.exp -> libm exp: same bits on glibc. math.isfinite/round ties:
//       round(x) in int(round(...)) only appears in fragmentation counts,
//       NOT here; ejecta counts live in destruction_sim.
//   * int(x) truncation in counts == C++ (long long) cast truncation toward 0.
//   * Morphology dict iteration == insertion order (morphology_probs vector).
//   * CPython max()/min() == IEEE max/min on non-NaN doubles (NaN guarded).
//   * engine loop while-condition `< until - 1e-12` etc. — verbatim.
//   * wall-clock perf_counter soft warning is never raising => omitted in the
//     kernel (arch contract: caller-owned perf gate, not the mirror).

#include "app/evolution_sim.h"
#include "app/py_math.h"

#include <cmath>

namespace astra::app {

const char* evol_err_name(EvolErr e) {
    switch (e) {
        case EvolErr::OK: return "OK";
        case EvolErr::VALIDATION: return "EvolutionValidationError";
        case EvolErr::AUTHORITY: return "EvolutionAuthorityError";
        case EvolErr::NUMERICAL: return "EvolutionNumericalError";
        case EvolErr::DEPENDENCY: return "EvolutionDependencyError";
        case EvolErr::LIMITATION: return "EvolutionLimitationError";
    }
    return "?";
}

const char* evol_limitation_name(EvolLimitation l) {
    switch (l) {
        case EvolLimitation::NONE: return "NONE";
        case EvolLimitation::OUTSIDE_VALID_RANGE: return "OUTSIDE_VALID_RANGE";
        case EvolLimitation::INSUFFICIENT_RESOLUTION: return "INSUFFICIENT_RESOLUTION";
        case EvolLimitation::CAUSAL_INCONSISTENCY: return "CAUSAL_INCONSISTENCY";
        case EvolLimitation::UNSUPPORTED_REGIME: return "UNSUPPORTED_REGIME";
        case EvolLimitation::UNSUPPORTED_STRUCTURE: return "UNSUPPORTED_STRUCTURE";
        case EvolLimitation::PROVIDER_REQUIRED_BUT_ABSENT: return "PROVIDER_REQUIRED_BUT_ABSENT";
        case EvolLimitation::MISSING_DEPENDENCY: return "MISSING_DEPENDENCY";
    }
    return "?";
}

const char* evol_provenance_name(EvolProvenance p) {
    switch (p) {
        case EvolProvenance::SIMULATED_DATA: return "SIMULATED_DATA";
        case EvolProvenance::THEORETICAL: return "THEORETICAL";
        case EvolProvenance::SPECULATIVE: return "SPECULATIVE";
        case EvolProvenance::DERIVED_DATA: return "DERIVED_DATA";
        case EvolProvenance::REAL_DATA: return "REAL_DATA";
    }
    return "?";
}

const char* evol_stellar_phase_name(EvolStellarPhase p) {
    switch (p) {
        case EvolStellarPhase::FORMATION: return "FORMATION";
        case EvolStellarPhase::MAIN_SEQUENCE: return "MAIN_SEQUENCE";
        case EvolStellarPhase::POST_MAIN_SEQUENCE: return "POST_MAIN_SEQUENCE";
        case EvolStellarPhase::REMNANT: return "REMNANT";
        case EvolStellarPhase::WHITE_DWARF: return "WHITE_DWARF";
        case EvolStellarPhase::NEUTRON_STAR: return "NEUTRON_STAR";
        case EvolStellarPhase::BLACK_HOLE: return "BLACK_HOLE";
        case EvolStellarPhase::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

const char* evol_epoch_name(EvolEpoch e) {
    switch (e) {
        case EvolEpoch::UNKNOWN: return "UNKNOWN";
        case EvolEpoch::STELLIFEROUS: return "STELLIFEROUS";
        case EvolEpoch::DECLINING_STAR_FORMATION: return "DECLINING_STAR_FORMATION";
        case EvolEpoch::DEGENERATE: return "DEGENERATE";
        case EvolEpoch::BLACK_HOLE_DOMINATED: return "BLACK_HOLE_DOMINATED";
        case EvolEpoch::DARK_ERA: return "DARK_ERA";
        case EvolEpoch::CUSTOM: return "CUSTOM";
    }
    return "?";
}

const char* evol_timestep_reason_name(EvolTimestepReason r) {
    switch (r) {
        case EvolTimestepReason::CONFIGURED_MAX: return "CONFIGURED_MAX";
        case EvolTimestepReason::CONFIGURED_MIN: return "CONFIGURED_MIN";
        case EvolTimestepReason::RATE_LIMITED: return "RATE_LIMITED";
        case EvolTimestepReason::EVENT_DRIVEN: return "EVENT_DRIVEN";
        case EvolTimestepReason::REMAINING_INTERVAL: return "REMAINING_INTERVAL";
    }
    return "?";
}

namespace {

bool finite(double v) { return !std::isnan(v) && !std::isinf(v); }

// authority _check_finite / _finite_time pattern.
bool check_number(double v) { return finite(v); }

// CPython int() on a float: truncates toward zero (here values are >= 0).
long long py_int(double v) { return (long long)v; }

// Quantity with default lookup (galaxy/step patterns use _get_quantity w/ default).
double getq(const EvolState& s, const std::string& key, double def) {
    auto it = s.quantities.find(key);
    return it == s.quantities.end() ? def : it->second.value;
}

bool hasq(const EvolState& s, const std::string& key) {
    return s.quantities.find(key) != s.quantities.end();
}

void setq(EvolState& s, const std::string& key, double value, EvolProvenance provenance) {
    EvolQuantity q;
    q.value = value;
    q.provenance = provenance;
    s.quantities[key] = q;
}

} // namespace

// ---------------------------------------------------------------------------
// Config validation (config.py)
// ---------------------------------------------------------------------------
EvolErr EvolTimestepPolicy::validate() const {
    if (!(min_dt_gyr > 0.0)) return EvolErr::VALIDATION;
    if (!(max_dt_gyr >= min_dt_gyr)) return EvolErr::VALIDATION;
    if (!(base_dt_gyr >= min_dt_gyr && base_dt_gyr <= max_dt_gyr)) return EvolErr::VALIDATION;
    if (!(rate_scale_floor > 0.0)) return EvolErr::VALIDATION;
    if (std::isinf(min_dt_gyr) || std::isinf(max_dt_gyr)) return EvolErr::VALIDATION;
    // Authority checks `v in (inf,-inf)`; NaN slips past default __post_init__
    // in the authority for non-provided fields -> mirrored as finite denom check.
    if (!finite(min_dt_gyr) || !finite(max_dt_gyr) || !finite(base_dt_gyr)) return EvolErr::VALIDATION;
    return EvolErr::OK;
}

EvolErr EvolPerformanceBudget::validate() const {
    if (max_objects_evolved <= 0) return EvolErr::VALIDATION;
    if (max_events_per_run <= 0) return EvolErr::VALIDATION;
    if (max_history_samples_per_object <= 0) return EvolErr::VALIDATION;
    if (max_memory_mb <= 0) return EvolErr::VALIDATION;
    if (!(max_wall_time_s_per_gyr > 0.0)) return EvolErr::VALIDATION;
    if (!finite(max_wall_time_s_per_gyr)) return EvolErr::VALIDATION;
    return EvolErr::OK;
}

EvolErr EvolConfig::validate() const {
    EvolErr e = timestep.validate();
    if (e != EvolErr::OK) return e;
    e = budget.validate();
    if (e != EvolErr::OK) return e;
    if (model_version.empty()) return EvolErr::VALIDATION;
    // rng_stream_name / rtol / atol fields: mirror does not hold an RNG for
    // evolution (authority engine never draws from its rng in evolve_object;
    // see engine.py — rng flows to scenarios only), so no mirror rng gate.
    if (resolution != "individual" && resolution != "population" &&
        resolution != "galaxy" && resolution != "cluster" && resolution != "web") {
        return EvolErr::VALIDATION;
    }
    return EvolErr::OK;
}

// ---------------------------------------------------------------------------
// Stellar (stellar.py)
// ---------------------------------------------------------------------------
EvolErr evol_main_sequence_lifetime_gyr(double mass_msun, double& out_gyr) {
    if (std::isnan(mass_msun) || std::isinf(mass_msun) || mass_msun <= 0.0) {
        return EvolErr::VALIDATION;
    }
    // Authority passes through (no clamp) outside 0.1..100; see stellar.py.
    out_gyr = 10.0 * std::pow(mass_msun, -2.5);
    return EvolErr::OK;
}

EvolErr evol_remnant_for_mass(double mass_msun, EvolStellarPhase& out) {
    // Authority code path, verbatim (the 0.5-branch is unreachable relative to
    // the 8.0-branch in the authority; preserved, not "fixed").
    const double m = mass_msun;
    if (std::isnan(m)) {
        // Authority: NaN passes all comparisons false -> BLACK_HOLE return.
        out = EvolStellarPhase::BLACK_HOLE;
        return EvolErr::OK;
    }
    if (m < 0.5) out = EvolStellarPhase::WHITE_DWARF;
    else if (m < 8.0) out = EvolStellarPhase::WHITE_DWARF;
    else if (m < 25.0) out = EvolStellarPhase::NEUTRON_STAR;
    else out = EvolStellarPhase::BLACK_HOLE;
    return EvolErr::OK;
}

EvolErr evol_classify_stellar_phase(double mass_msun, double age_gyr,
                                    EvolStellarTransition& out) {
    out = EvolStellarTransition{};
    double t_ms;
    const EvolErr e1 = evol_main_sequence_lifetime_gyr(mass_msun, t_ms);
    if (e1 != EvolErr::OK) return e1;
    if (age_gyr < t_ms) return EvolErr::OK; // None
    EvolStellarPhase remnant;
    const EvolErr e2 = evol_remnant_for_mass(mass_msun, remnant);
    if (e2 != EvolErr::OK) return e2;
    out.present = true;
    out.before = EvolStellarPhase::MAIN_SEQUENCE;
    out.after = remnant;
    out.timescale_gyr = t_ms;
    return EvolErr::OK;
}

EvolErr evol_make_stellar_step(const EvolState& state, double dt_gyr,
                               const std::string& model_id, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    const double new_time = state.cosmic_time_gyr + dt_gyr;
    out.cosmic_time_gyr = new_time;

    if (hasq(state, "age_gyr") && hasq(state, "mass_msun")) {
        const double new_age = getq(state, "age_gyr", 0.0) + dt_gyr;
        setq(out, "age_gyr", new_age, EvolProvenance::SIMULATED_DATA);
        EvolStellarTransition tr;
        const EvolErr te = evol_classify_stellar_phase(getq(state, "mass_msun", 0.0),
                                                       new_age, tr);
        // Authority swallows EvolutionValidationError from classify here.
        if (te != EvolErr::OK && te != EvolErr::VALIDATION) return te;
        EvolStellarPhase phase_now = EvolStellarPhase::UNKNOWN;
        // state.phase holds the phase word; authority compares to
        // StellarPhase.MAIN_SEQUENCE.value ("MAIN_SEQUENCE").
        if (te == EvolErr::OK && tr.present && state.phase == "MAIN_SEQUENCE") {
            phase_now = tr.after;
            const std::string after_word = evol_stellar_phase_name(tr.after);
            out.phase = after_word;
            // remnant_phase flag quantity with transition note
            setq(out, "remnant_phase", 1.0, EvolProvenance::SIMULATED_DATA);
        }
    }
    return EvolErr::OK;
}

// ---------------------------------------------------------------------------
// Galaxy (galaxy.py), parameters already unfolded
// ---------------------------------------------------------------------------
EvolErr evol_make_galaxy_step(const EvolState& state, double dt_gyr,
                              const std::string& model_id,
                              double sfr_tau_gyr, double return_fraction,
                              double yield_p, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    const double tau = sfr_tau_gyr;
    const double return_frac = return_fraction;
    const double inflow_rate = 0.0;

    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    const double dt = dt_gyr;
    out.cosmic_time_gyr = state.cosmic_time_gyr + dt;

    const double mstar = getq(state, "stellar_mass_msun", 1e10);
    const double mgas = getq(state, "gas_mass_msun", 1e9);
    const double sfr = getq(state, "sfr_msun_per_yr", 1.0);
    const double Z = getq(state, "metallicity", 0.02);

    // SFR exponential decline
    double sfr_new = tau > 0.0 ? sfr * std::exp(-dt / tau) : sfr;

    // Formed stellar mass (Gyr->yr * 1e9)
    const double formed = sfr * dt * 1e9 * (1.0 - return_frac);
    const double mstar_new = mstar + formed;
    double mgas_new = mgas - sfr * dt * 1e9 + inflow_rate * dt * 1e9;
    if (!(mgas_new > 0.0)) mgas_new = 0.0; // max(0.0, ...); NaN propagates through
                                                // comparison-false => 0.0 (max semantics)
    if (std::isnan(mgas_new)) mgas_new = 0.0;

    double Z_new = Z;
    if (mgas_new > 0.0) {
        const double dZ = yield_p * sfr * dt * 1e9 / mgas_new;
        Z_new = Z + dZ;
    }

    setq(out, "stellar_mass_msun", mstar_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "gas_mass_msun", mgas_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "sfr_msun_per_yr", sfr_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "metallicity", Z_new, EvolProvenance::SIMULATED_DATA);

    // Luminosity scaling L * (mstar_new / mstar) if mstar > 0 else L
    const double L = getq(state, "luminosity_Lsun", 1e10);
    const double L_new = mstar > 0.0 ? L * (mstar_new / mstar) : L;
    setq(out, "luminosity_Lsun", L_new, EvolProvenance::SIMULATED_DATA);

    // Central BH toy growth: * (1 + 0.01 * dt)
    const double mbh = getq(state, "central_bh_mass_msun", 1e6);
    const double mbh_new = mbh * (1.0 + 0.01 * dt);
    setq(out, "central_bh_mass_msun", mbh_new, EvolProvenance::SIMULATED_DATA);

    // Morphology drift (gas-poor): only when morphology_probs present.
    if (!state.morphology_probs.empty()) {
        // Find SPIRAL/ELLIPTICAL entries by key word (GalaxyMorphology values).
        auto find_idx = [&](const char* word) -> int {
            for (size_t i = 0; i < out.morphology_probs.size(); ++i) {
                if (out.morphology_probs[i].key == word) return (int)i;
            }
            return -1;
        };
        const int iS = find_idx("SPIRAL");
        const int iE = find_idx("ELLIPTICAL");
        // Guard: mgas_new / max(mstar_new, 1.0) < 0.05
        const double denom = mstar_new > 1.0 ? mstar_new : 1.0;
        if (mgas_new / denom < 0.05) {
            const double spiral_p = (iS >= 0) ? out.morphology_probs[iS].value : 0.0;
            const double raw_shift = 0.01 * dt;
            const double shift = raw_shift < spiral_p ? raw_shift : spiral_p;
            if (shift > 0.0) {
                if (iS >= 0) out.morphology_probs[iS].value = spiral_p - shift;
                if (iE >= 0) out.morphology_probs[iE].value =
                    ((iE >= 0) ? out.morphology_probs[iE].value : 0.0) + shift;
                // renormalize over INSERTION ORDER sum
                double tot = 0.0;
                for (const auto& e : out.morphology_probs) tot += e.value;
                if (tot > 0.0) {
                    for (auto& e : out.morphology_probs) e.value /= tot;
                }
            }
        }
    }
    return EvolErr::OK;
}

EvolErr evol_make_galaxy_step_default(const EvolState& state, double dt_gyr,
                                      const std::string& model_id, EvolState& out) {
    return evol_make_galaxy_step(state, dt_gyr, model_id, 5.0, 0.3, 0.02, out);
}

// ---------------------------------------------------------------------------
// Structure (structure.py)
// ---------------------------------------------------------------------------
EvolErr evol_make_cluster_step(const EvolState& state, double dt_gyr,
                               const std::string& model_id,
                               double cluster_growth_rate_per_gyr, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    const double dt = dt_gyr;
    out.cosmic_time_gyr = state.cosmic_time_gyr + dt;
    const double mass = getq(state, "total_mass_msun", 1e14);
    const long long members = py_int(getq(state, "member_count", 50.0));
    const double growth_rate = cluster_growth_rate_per_gyr;
    const double mass_new = mass * (1.0 + growth_rate * dt);
    const long long members_new = members + py_int(0.5 * dt);
    setq(out, "total_mass_msun", mass_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "member_count", (double)members_new, EvolProvenance::SIMULATED_DATA);
    return EvolErr::OK;
}

EvolErr evol_make_cosmic_web_step(const EvolState& state, double dt_gyr,
                                  const std::string& model_id, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    const double dt = dt_gyr;
    const double new_time = state.cosmic_time_gyr + dt;
    out.cosmic_time_gyr = new_time;
    const long long filaments = py_int(getq(state, "filament_count", 10.0));
    const long long nodes = py_int(getq(state, "node_count", 5.0));
    const long long voids = py_int(getq(state, "void_count", 8.0));
    // filaments coarsen only when new_time > 10 (authority conditional).
    long long filaments_new = filaments;
    if (new_time > 10.0) {
        const long long dec = py_int(0.1 * dt);
        filaments_new = filaments - dec > 1 ? filaments - dec : 1;
    }
    const long long voids_new = voids + py_int(0.05 * dt);
    setq(out, "filament_count", (double)filaments_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "void_count", (double)voids_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "node_count", (double)nodes, EvolProvenance::SIMULATED_DATA);
    return EvolErr::OK;
}

EvolErr evol_make_void_step(const EvolState& state, double dt_gyr,
                            const std::string& model_id,
                            double void_growth_per_gyr, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::THEORETICAL;
    const double dt = dt_gyr;
    out.cosmic_time_gyr = state.cosmic_time_gyr + dt;
    const double radius = getq(state, "radius_mpc", 10.0);
    const double delta = getq(state, "density_contrast", -0.8);
    const double radius_new = radius * (1.0 + void_growth_per_gyr * dt);
    // delta_new = max(-1.0, delta - 0.01*dt)
    double cand = delta - 0.01 * dt;
    double delta_new = -1.0 > cand ? -1.0 : cand;
    setq(out, "radius_mpc", radius_new, EvolProvenance::THEORETICAL);
    setq(out, "density_contrast", delta_new, EvolProvenance::THEORETICAL);
    return EvolErr::OK;
}

EvolErr evol_make_dark_matter_halo_step(const EvolState& state, double dt_gyr,
                                        const std::string& model_id,
                                        double halo_growth_per_gyr, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    const double dt = dt_gyr;
    out.cosmic_time_gyr = state.cosmic_time_gyr + dt;
    const double m_halo = getq(state, "halo_mass_msun", 1e12);
    const double concentration = getq(state, "concentration", 5.0);
    const double m_new = m_halo * (1.0 + halo_growth_per_gyr * dt);
    const double c_new = concentration * (1.0 + 0.005 * dt);
    setq(out, "halo_mass_msun", m_new, EvolProvenance::SIMULATED_DATA);
    setq(out, "concentration", c_new, EvolProvenance::SIMULATED_DATA);
    return EvolErr::OK;
}

EvolErr evol_make_linear_step(const EvolState& state, double dt_gyr,
                              const std::string& model_id, EvolState& out) {
    if (!finite(dt_gyr) || dt_gyr < 0.0) return EvolErr::VALIDATION;
    out = state;
    out.model_id = model_id;
    out.provenance = EvolProvenance::SIMULATED_DATA;
    out.cosmic_time_gyr = state.cosmic_time_gyr + dt_gyr;
    return EvolErr::OK;
}

// ---------------------------------------------------------------------------
// Epoch classifier (epoch.py)
// ---------------------------------------------------------------------------
EvolErr EvolEpochBoundaries::validate() const {
    struct { const char* n; double v; bool frac; } fields[4] = {
        {"declining_sfr_threshold", declining_sfr_threshold, false},
        {"degenerate_remnant_fraction", degenerate_remnant_fraction, true},
        {"black_hole_dominated_fraction", black_hole_dominated_fraction, true},
        {"dark_era_luminous_fraction", dark_era_luminous_fraction, true},
    };
    for (const auto& f : fields) {
        if (!finite(f.v)) return EvolErr::VALIDATION;
        if (f.frac && !(0.0 <= f.v && f.v <= 1.0)) return EvolErr::VALIDATION;
        if (!f.frac && f.v < 0.0) return EvolErr::VALIDATION;
    }
    if (model_id.empty()) return EvolErr::VALIDATION;
    return EvolErr::OK;
}

EvolErr evol_classify_epoch(const EvolEpochBoundaries& b,
                            double cosmic_time_gyr,
                            bool has_sfr, double sfr_density,
                            bool has_remnant, double remnant_mass_fraction,
                            bool has_bh, double bh_mass_fraction,
                            bool has_luminous, double luminous_mass_fraction,
                            EvolEpoch& out) {
    out = EvolEpoch::UNKNOWN;
    // Authority: finite check raises EvolutionValidationError for NaN/Inf.
    if (!finite(cosmic_time_gyr)) return EvolErr::VALIDATION;
    if (!has_sfr || !has_remnant) return EvolErr::OK; // UNKNOWN
    if (!finite(sfr_density)) return EvolErr::VALIDATION;
    if (!finite(remnant_mass_fraction)) return EvolErr::VALIDATION;
    if (!(0.0 <= remnant_mass_fraction && remnant_mass_fraction <= 1.0)) {
        return EvolErr::VALIDATION;
    }
    if (sfr_density < 0.0) return EvolErr::VALIDATION;
    if (has_bh) {
        if (!finite(bh_mass_fraction)) return EvolErr::VALIDATION;
        if (!(0.0 <= bh_mass_fraction && bh_mass_fraction <= 1.0)) return EvolErr::VALIDATION;
    }
    if (has_luminous) {
        if (!finite(luminous_mass_fraction)) return EvolErr::VALIDATION;
        if (!(0.0 <= luminous_mass_fraction && luminous_mass_fraction <= 1.0)) {
            return EvolErr::VALIDATION;
        }
    }
    if (sfr_density > b.declining_sfr_threshold) {
        out = EvolEpoch::STELLIFEROUS;
        return EvolErr::OK;
    }
    if (remnant_mass_fraction < b.degenerate_remnant_fraction) {
        out = EvolEpoch::DECLINING_STAR_FORMATION;
        return EvolErr::OK;
    }
    // THE AUTHORITY'S CODE — NOT its docstring — decides DARK first? Read
    // the code again in epoch.py: BH check comes FIRST (returns BLACK_HOLE_-
    // DOMINATED), then DARK_ERA. The class docstring claims DARK precedence,
    // but the shipped function body is the authority; mirrored verbatim.
    if (has_bh && bh_mass_fraction >= b.black_hole_dominated_fraction) {
        out = EvolEpoch::BLACK_HOLE_DOMINATED;
        return EvolErr::OK;
    }
    if (has_luminous && luminous_mass_fraction < b.dark_era_luminous_fraction) {
        out = EvolEpoch::DARK_ERA;
        return EvolErr::OK;
    }
    out = EvolEpoch::DEGENERATE;
    return EvolErr::OK;
}

// ---------------------------------------------------------------------------
// Timestep controller (timestep.py AdaptiveTimestepController.choose)
// ---------------------------------------------------------------------------
EvolErr evol_choose_timestep(const EvolTimestepPolicy& p,
                             double remaining_gyr, double rate_scale,
                             bool has_next_event, double next_event_gyr,
                             bool has_current_time, double current_cosmic_time_gyr,
                             EvolTimestepDecision& out) {
    out = EvolTimestepDecision{};
    // rem validation
    if (!finite(remaining_gyr)) return EvolErr::NUMERICAL;
    if (remaining_gyr < 0.0) return EvolErr::NUMERICAL;
    if (remaining_gyr == 0.0) {
        out.dt_gyr = 0.0;
        out.reason = EvolTimestepReason::CONFIGURED_MIN;
        return EvolErr::OK;
    }
    // rate_scale validation (None -> 0.0)
    double rs = 0.0;
    if (!finite(rate_scale)) return EvolErr::NUMERICAL;
    if (rate_scale < 0.0) return EvolErr::NUMERICAL;
    rs = rate_scale;
    const double rem = remaining_gyr;

    // remaining smaller than min_dt -> return remaining directly (no clamp up)
    if (rem < p.min_dt_gyr) {
        out.dt_gyr = rem;
        out.reason = EvolTimestepReason::CONFIGURED_MIN;
        return EvolErr::OK;
    }

    double candidate = p.max_dt_gyr;
    EvolTimestepReason reason = EvolTimestepReason::CONFIGURED_MAX;

    if (rs > 0.0) {
        const double floor_v = p.rate_scale_floor > 1e-30 ? p.rate_scale_floor : 1e-30;
        const double effective_rate = rs > floor_v ? rs : floor_v;
        double scaled = p.base_dt_gyr / effective_rate;
        // clamp scaled to [min, max]
        scaled = scaled < p.min_dt_gyr ? p.min_dt_gyr : (scaled > p.max_dt_gyr ? p.max_dt_gyr : scaled);
        if (scaled < candidate) {
            candidate = scaled;
            reason = EvolTimestepReason::RATE_LIMITED;
        }
    }

    if (has_next_event && p.allow_event_driven) {
        if (!finite(next_event_gyr)) return EvolErr::NUMERICAL;
        double event_delta;
        if (has_current_time) {
            if (!finite(current_cosmic_time_gyr)) return EvolErr::NUMERICAL;
            event_delta = next_event_gyr - current_cosmic_time_gyr;
        } else {
            event_delta = next_event_gyr < rem ? next_event_gyr : rem;
        }
        if (event_delta <= 0.0) {
            // Authority: pass — candidate unchanged. (event now/past)
        } else {
            if (event_delta < candidate) {
                if (event_delta < p.min_dt_gyr) {
                    out.dt_gyr = event_delta < rem ? event_delta : rem;
                    out.reason = EvolTimestepReason::EVENT_DRIVEN;
                    return EvolErr::OK;
                }
                candidate = event_delta;
                reason = EvolTimestepReason::EVENT_DRIVEN;
            }
        }
    }

    const double eps = 1e-12 * (1.0 > std::fabs(rem) ? 1.0 : std::fabs(rem));
    if (candidate + eps >= rem) {
        out.dt_gyr = rem;
        out.reason = reason == EvolTimestepReason::EVENT_DRIVEN
            ? EvolTimestepReason::EVENT_DRIVEN
            : EvolTimestepReason::REMAINING_INTERVAL;
        return EvolErr::OK;
    }
    out.dt_gyr = candidate;
    out.reason = reason;
    return EvolErr::OK;
}

// ---------------------------------------------------------------------------
// Engine (engine.py evolve_object)
// ---------------------------------------------------------------------------
void EvolEngine::record_history(const EvolState& s) {
    if ((long long)history.size() >= cfg.budget.max_history_samples_per_object) {
        history.erase(history.begin()); // drop oldest
    }
    history.push_back(s);
}

EvolErr EvolEngine::evolve_object(const EvolState& initial, double until_cosmic_time_gyr,
                                  const std::string& model_id, bool authority_ok,
                                  EvolState& out) {
    if (!authority_ok) return EvolErr::AUTHORITY;
    if (model_id.empty()) return EvolErr::VALIDATION;
    if (!finite(until_cosmic_time_gyr)) return EvolErr::NUMERICAL;
    // _validate_times: t0, t1 >= 0 finite; t1 < t0 -> VALIDATION
    if (!finite(initial.cosmic_time_gyr) || initial.cosmic_time_gyr < 0.0) return EvolErr::NUMERICAL;
    if (until_cosmic_time_gyr < 0.0) return EvolErr::VALIDATION;
    if (until_cosmic_time_gyr < initial.cosmic_time_gyr) return EvolErr::VALIDATION;

    // Model dispatch mirror (registrations in _register_default_models).
    enum class Kind { LINEAR, STELLAR, GALAXY, CLUSTER, WEB, VOID, HALO } kind;
    if (model_id == "astra.evolution.linear.v1") kind = Kind::LINEAR;
    else if (model_id == "astra.evolution.stellar.v1") kind = Kind::STELLAR;
    else if (model_id == "astra.evolution.galaxy.v1") kind = Kind::GALAXY;
    else if (model_id == "astra.evolution.cluster.v1") kind = Kind::CLUSTER;
    else if (model_id == "astra.evolution.web.v1") kind = Kind::WEB;
    else if (model_id == "astra.evolution.void.v1") kind = Kind::VOID;
    else if (model_id == "astra.evolution.halo.v1") kind = Kind::HALO;
    else return EvolErr::VALIDATION; // unknown model_id -> EvolutionValidationError

    record_history(initial);
    EvolState state = initial;
    long long steps = 0;

    while (state.cosmic_time_gyr < until_cosmic_time_gyr - 1e-12) {
        const double remaining = until_cosmic_time_gyr - state.cosmic_time_gyr;
        if (remaining <= 0.0) break;
        EvolTimestepDecision dec;
        const EvolErr de = evol_choose_timestep(cfg.timestep, remaining, 1.0,
                                                false, 0.0, true, state.cosmic_time_gyr, dec);
        if (de != EvolErr::OK) return de;
        if (dec.dt_gyr == 0.0) break;
        const double dt = dec.dt_gyr < remaining ? dec.dt_gyr : remaining;

        EvolState next;
        EvolErr se;
        switch (kind) {
            case Kind::LINEAR: se = evol_make_linear_step(state, dt, model_id, next); break;
            case Kind::STELLAR: se = evol_make_stellar_step(state, dt, model_id, next); break;
            case Kind::GALAXY: se = evol_make_galaxy_step_default(state, dt, model_id, next); break;
            case Kind::CLUSTER: se = evol_make_cluster_step(state, dt, model_id, 0.02, next); break;
            case Kind::WEB: se = evol_make_cosmic_web_step(state, dt, model_id, next); break;
            case Kind::VOID: se = evol_make_void_step(state, dt, model_id, 0.01, next); break;
            case Kind::HALO: se = evol_make_dark_matter_halo_step(state, dt, model_id, 0.02, next); break;
        }
        if (se != EvolErr::OK) return se;
        if (next.cosmic_time_gyr < state.cosmic_time_gyr - 1e-12) {
            return EvolErr::LIMITATION; // CAUSAL_INCONSISTENCY
        }
        state = next;
        ++steps;
        record_history(state);
        // steps % 1000 == 0 -> check_budget (wall-clock soft warning skipped,
        // hard check on events_per_run only; identical observable effect).
        if (steps % 1000 == 0) {
            if (steps > cfg.budget.max_events_per_run) {
                return EvolErr::LIMITATION; // INSUFFICIENT_RESOLUTION
            }
        }
        if (steps > cfg.budget.max_events_per_run) {
            return EvolErr::LIMITATION;
        }
    }
    out = state;
    return EvolErr::OK;
}

} // namespace astra::app
