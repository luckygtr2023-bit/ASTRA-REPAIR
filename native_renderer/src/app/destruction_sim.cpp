// Implementation of the destruction mirror. Every formula, comparison and
// draw order mirrors astra.destruction exactly; deviations are commented.
// Classification: outputs SIMULATED_DATA; RNG REAL (CPython-ported draws).

#include "destruction_sim.h"
#include "app/py_math.h"

#include <cmath>
#include <cfloat>

namespace astra::app {

namespace {
constexpr long long TWO_POW_53 = 1LL << 53;

inline double pylog1p(double x) { return std::log1p(x); }
// Python round(x) (ties to even) == std::nearbyint in default FE_TONEAREST.
inline double pyround(double x) { return std::nearbyint(x); }

inline bool fin(double v) { return !std::isnan(v) && !std::isinf(v); }
} // namespace

double DestVec3::magnitude() const {
    return py_magnitude3(x, y, z);
}

bool DestVec3::is_finite() const { return fin(x) && fin(y) && fin(z); }

DestErr DestructionImpactEvent::reduced_mass(double& out) const {
    const double m1 = impactor_mass_kg, m2 = target_mass_kg;
    if (m1 + m2 <= 0.0) return DestErr::NUMERICAL;
    out = (m1 * m2) / (m1 + m2);
    return DestErr::OK;
}

// ---- config ----------------------------------------------------------------
DestErr dest_validate_config(const DestructionConfig& cfg) {
    if (!(0.0 <= cfg.fragmentation_energy_fraction && cfg.fragmentation_energy_fraction <= 1.0))
        return DestErr::NUMERICAL;
    if (!(0.0 <= cfg.thermal_energy_fraction && cfg.thermal_energy_fraction <= 1.0))
        return DestErr::NUMERICAL;
    if (cfg.fragmentation_energy_fraction + cfg.thermal_energy_fraction > 1.0 + 1e-12)
        return DestErr::NUMERICAL;
    if (!(0.0 <= cfg.momentum_transfer_efficiency && cfg.momentum_transfer_efficiency <= 1.0))
        return DestErr::NUMERICAL;
    if (!(0.0 <= cfg.grazing_sin_threshold && cfg.grazing_sin_threshold <= 1.0))
        return DestErr::NUMERICAL;
    if (!(0.0 <= cfg.head_on_angle_threshold_rad &&
          cfg.head_on_angle_threshold_rad <= 3.141592653589793))
        return DestErr::NUMERICAL;
    if (cfg.model_version.empty() || cfg.rng_stream_name.empty()) return DestErr::NUMERICAL;
    const int il[5] = {cfg.limits.max_fragments_per_impact, cfg.limits.max_ejecta_per_impact,
                       cfg.limits.max_debris_per_impact, cfg.limits.max_secondary_impacts,
                       cfg.limits.max_recursion_depth};
    for (int v : il) if (v < 0) return DestErr::NUMERICAL;
    const double dl[3] = {cfg.limits.min_fragment_mass_kg, cfg.limits.min_impact_energy_j,
                          cfg.limits.min_relative_speed_m_s};
    for (double v : dl) if (!(v >= 0.0) || std::isnan(v)) return DestErr::NUMERICAL;
    return DestErr::OK;
}

// ---- damage ----------------------------------------------------------------
int dest_damage_order(DestructionDamageState s) { return (int)s; }

DestErr dest_damage_is_valid_transition(DestructionDamageState src, DestructionDamageState dst, bool& out) {
    out = dest_damage_order(dst) >= dest_damage_order(src);
    return DestErr::OK;
}

DestErr dest_damage_transition(DestructionDamageState src, DestructionDamageState dst, DestructionDamageState& out) {
    bool ok = false;
    dest_damage_is_valid_transition(src, dst, ok);
    if (!ok) return DestErr::IMPACT_VALIDATION;
    out = dst;
    return DestErr::OK;
}

const char* dest_damage_name(DestructionDamageState s) {
    switch (s) {
    case DestructionDamageState::INTACT: return "INTACT";
    case DestructionDamageState::DAMAGED: return "DAMAGED";
    case DestructionDamageState::FRACTURED: return "FRACTURED";
    case DestructionDamageState::FRAGMENTED: return "FRAGMENTED";
    case DestructionDamageState::DESTROYED: return "DESTROYED";
    }
    return "?";
}

// ---- validation ------------------------------------------------------------
DestErr dest_validate_impact(const DestructionImpactEvent& ev, const DestructionConfig& cfg) {
    // ImpactEvent.__post_init__ equivalents (finiteness + radii domain).
    if (!fin(ev.sim_time_s) || !fin(ev.impactor_mass_kg) || !fin(ev.target_mass_kg) ||
        !fin(ev.impactor_radius_m) || !fin(ev.target_radius_m))
        return DestErr::NUMERICAL;
    if (ev.impactor_radius_m < 0.0 || ev.target_radius_m < 0.0) return DestErr::NUMERICAL;
    if (!ev.impactor_position.is_finite() || !ev.target_position.is_finite() ||
        !ev.impactor_velocity.is_finite() || !ev.target_velocity.is_finite())
        return DestErr::NUMERICAL;
    // system.validate_impact order.
    if (ev.impact_id.empty()) return DestErr::IMPACT_VALIDATION;
    if (ev.impactor_id.empty() || ev.target_id.empty()) return DestErr::IMPACT_VALIDATION;
    if (ev.impactor_id == ev.target_id) return DestErr::IMPACT_VALIDATION;
    if (!(ev.impactor_mass_kg > 0.0)) return DestErr::NUMERICAL;   // require_positive
    if (!(ev.target_mass_kg > 0.0)) return DestErr::NUMERICAL;
    if (!(ev.sim_time_s >= 0.0)) return DestErr::NUMERICAL;        // require_non_negative
    if (ev.relative_speed() < cfg.limits.min_relative_speed_m_s)
        return DestErr::IMPACT_VALIDATION;
    DestructionImpactGeometry g;
    return dest_compute_impact_geometry(ev, cfg, g); // raises on degenerate configuration
}

// ---- geometry --------------------------------------------------------------
DestErr dest_compute_impact_geometry(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                     DestructionImpactGeometry& out) {
    const DestVec3 rel_v = ev.relative_velocity();
    const double speed = rel_v.magnitude();
    if (speed == 0.0) return DestErr::IMPACT_VALIDATION;

    const DestVec3 incoming = rel_v / speed;
    const DestVec3 delta = ev.impactor_position - ev.target_position;
    const double d = delta.magnitude();

    DestVec3 contact, surface_normal;
    if (d == 0.0) {
        contact = ev.target_position;
        surface_normal = -incoming;  // deterministic fallback
    } else {
        const DestVec3 dir_centres = delta / d;
        const double radius = ev.target_radius_m;
        if (radius > 0.0) {
            const double offset = (d > radius) ? radius : d * 0.5;
            contact = ev.target_position + dir_centres * offset;
        } else {
            contact = ev.target_position;
        }
        surface_normal = dir_centres;
    }
    double cos_incidence = (-incoming).dot(surface_normal);
    cos_incidence = cos_incidence < -1.0 ? -1.0 : (cos_incidence > 1.0 ? 1.0 : cos_incidence);
    const double incidence = std::acos(cos_incidence);

    out.contact_point = contact;
    out.surface_normal = surface_normal;
    out.incoming_direction = incoming;
    out.incidence_angle_rad = incidence;
    out.is_grazing = cos_incidence <= cfg.grazing_sin_threshold;
    out.is_head_on = incidence <= cfg.head_on_angle_threshold_rad;
    return DestErr::OK;
}

// ---- energy ----------------------------------------------------------------
DestErr dest_compute_impact_energy(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                   double deposited_fraction, DestructionImpactEnergy& out) {
    if (!(0.0 <= deposited_fraction && deposited_fraction <= 1.0)) return DestErr::NUMERICAL;
    double m_red = 0.0;
    DestErr e = ev.reduced_mass(m_red);
    if (e != DestErr::OK) return e;
    const double v_rel = ev.relative_speed();
    const double e_kin = 0.5 * m_red * v_rel * v_rel;
    const double e_dep = e_kin * deposited_fraction;
    const double e_frag = e_dep * cfg.fragmentation_energy_fraction;
    const double e_therm = e_dep * cfg.thermal_energy_fraction;
    const double e_resid = e_kin - e_frag - e_therm;
    if (e_resid < -1e-9 * (e_kin > 1.0 ? e_kin : 1.0)) return DestErr::NUMERICAL;
    out.kinetic_energy_j = e_kin;
    out.deposited_energy_j = e_dep;
    out.fragmentation_energy_j = e_frag;
    out.thermal_energy_j = e_therm;
    out.residual_kinetic_energy_j = e_resid;
    return DestErr::OK;
}

// ---- momentum --------------------------------------------------------------
DestErr dest_compute_impact_momentum(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                                     DestructionImpactMomentum& out) {
    double m_red = 0.0;
    DestErr e = ev.reduced_mass(m_red);
    if (e != DestErr::OK) return e;
    const DestVec3 p_rel = ev.relative_velocity() * m_red;
    const DestVec3 transferred = p_rel * cfg.momentum_transfer_efficiency;
    out.relative_momentum_kg_m_s = p_rel;
    out.transferred_momentum_kg_m_s = transferred;
    out.residual_momentum_kg_m_s = p_rel - transferred;
    return DestErr::OK;
}

// ---- damage decision -------------------------------------------------------
DestErr dest_decide_damage(DestructionDamageState before, const DestructionImpactEnergy& en,
                           const DestructionImpactEvent& ev, DestructionDamageState& after) {
    if (ev.target_mass_kg <= 0.0) return DestErr::NUMERICAL;
    const double specific = en.deposited_energy_j / ev.target_mass_kg;
    DestructionDamageState wanted;
    if (specific < 1.0e3) wanted = DestructionDamageState::DAMAGED;
    else if (specific < 1.0e5) wanted = DestructionDamageState::FRACTURED;
    else if (specific < 1.0e7) wanted = DestructionDamageState::FRAGMENTED;
    else wanted = DestructionDamageState::DESTROYED;
    if (dest_damage_order(wanted) < dest_damage_order(before)) { after = before; return DestErr::OK; }
    return dest_damage_transition(before, wanted, after);
}

// ---- fragmentation ---------------------------------------------------------
DestErr dest_determine_fragment_count(double fragmentation_energy_j, const DestructionConfig& cfg, int& out) {
    if (cfg.limits.max_fragments_per_impact < 2) return DestErr::LIMIT_EXCEEDED;
    const double e_ref = 1.0e6;
    const double ratio = fragmentation_energy_j / e_ref;
    const int raw = (int)pyround(pylog1p(ratio < 0.0 ? 0.0 : ratio) * 4.0) + 2;
    int n = raw < 2 ? 2 : raw;
    if (n > cfg.limits.max_fragments_per_impact) n = cfg.limits.max_fragments_per_impact;
    out = n;
    return DestErr::OK;
}

DestErr dest_fragment_target(const DestructionImpactEvent& ev, const DestructionImpactEnergy& en,
                             DestructionDamageState target_state_after, double target_radius_m,
                             const DestructionConfig& cfg, PyMt19937& rng,
                             std::vector<DestructionFragment>& out) {
    if (target_state_after != DestructionDamageState::FRACTURED &&
        target_state_after != DestructionDamageState::FRAGMENTED &&
        target_state_after != DestructionDamageState::DESTROYED)
        return DestErr::NUMERICAL;
    const double m_total = ev.target_mass_kg;
    if (!(m_total > 0.0)) return DestErr::NUMERICAL;

    int n = 0;
    DestErr e = dest_determine_fragment_count(en.fragmentation_energy_j, cfg, n);
    if (e != DestErr::OK) return e;

    std::vector<double> weights;
    weights.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        double u = rng.next_float();
        if (u <= 0.0) u = 1.0 / (double)TWO_POW_53;
        weights.push_back(-std::log(u));
    }
    double w_sum = 0.0;
    for (double w : weights) w_sum += w;
    if (!(w_sum > 0.0)) { for (double& w : weights) w = 1.0; w_sum = (double)n; }

    double speed_scale = 2.0 * en.fragmentation_energy_j / (m_total > 1e-30 ? m_total : 1e-30);
    speed_scale = std::sqrt(speed_scale > 1e-9 ? speed_scale : 1e-9);

    std::vector<DestVec3> raw_dirs;
    raw_dirs.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        const double x = rng.next_float() * 2.0 - 1.0;
        const double y = rng.next_float() * 2.0 - 1.0;
        const double z = rng.next_float() * 2.0 - 1.0;
        DestVec3 v{x, y, z};
        if (v.magnitude() == 0.0) v = {1.0, 0.0, 0.0};
        raw_dirs.push_back(v / v.magnitude());
    }
    DestVec3 mean_dir{0.0, 0.0, 0.0};
    for (int i = 0; i < n; ++i) mean_dir = mean_dir + raw_dirs[(size_t)i] * weights[(size_t)i];
    mean_dir = mean_dir / w_sum;

    const DestVec3& cm_v = ev.target_velocity;
    out.clear();
    for (int i = 0; i < n; ++i) {
        const double m_i = m_total * (weights[(size_t)i] / w_sum);
        if (m_i < cfg.limits.min_fragment_mass_kg) continue;
        const DestVec3 dir = raw_dirs[(size_t)i] - mean_dir;
        const double r = target_radius_m * (0.5 + 0.5 * (i + 0.5) / n);
        DestructionFragment f;
        f.index = i;
        f.mass_kg = m_i;
        f.position = ev.target_position + raw_dirs[(size_t)i] * r;
        f.velocity = cm_v + dir * speed_scale;
        f.created_at_s = ev.sim_time_s;
        out.push_back(f);
    }
    if ((int)out.size() > cfg.limits.max_fragments_per_impact) return DestErr::LIMIT_EXCEEDED;
    return DestErr::OK;
}

// ---- ejecta ----------------------------------------------------------------
DestErr dest_generate_ejecta(const DestructionImpactEvent& ev, const DestructionImpactEnergy&,
                             const DestructionConfig& cfg, PyMt19937& rng,
                             double ejecta_mass_fraction, double characteristic_speed_m_s,
                             std::vector<DestructionEjecta>& out) {
    out.clear();
    if (!(0.0 <= ejecta_mass_fraction && ejecta_mass_fraction <= 1.0)) return DestErr::NUMERICAL;
    if (!(characteristic_speed_m_s >= 0.0)) return DestErr::NUMERICAL;

    if (cfg.limits.max_ejecta_per_impact == 0) return DestErr::OK;
    const double m_ejecta_total = ev.target_mass_kg * ejecta_mass_fraction;
    if (!(m_ejecta_total > 0.0)) return DestErr::OK;

    int n = (int)pyround(pylog1p(m_ejecta_total /
                (cfg.limits.min_fragment_mass_kg > 1e-30 ? cfg.limits.min_fragment_mass_kg : 1e-30)));
    if (n < 1) n = 1;
    if (n > cfg.limits.max_ejecta_per_impact) n = cfg.limits.max_ejecta_per_impact;
    const double per_mass = m_ejecta_total / n;

    DestVec3 n_hat;
    if (ev.relative_speed() > 0.0) {
        const DestVec3 neg = -ev.relative_velocity();
        n_hat = neg / neg.magnitude();
    } else {
        n_hat = {0.0, 0.0, 1.0};
    }
    const DestVec3 ref = (std::fabs(n_hat.z) < 0.9) ? DestVec3{0.0, 0.0, 1.0} : DestVec3{1.0, 0.0, 0.0};
    const DestVec3 t1c = n_hat.cross(ref);
    const DestVec3 t1 = t1c / t1c.magnitude();
    const DestVec3 t2 = n_hat.cross(t1);

    for (int i = 0; i < n; ++i) {
        const double u1 = rng.next_float();
        const double u2 = rng.next_float();
        const double theta = std::acos(std::sqrt(1.0 - u1));
        const double phi = 2.0 * 3.14159265358979323846 * u2;
        const DestVec3 direction = n_hat * std::cos(theta)
                                 + t1 * (std::sin(theta) * std::cos(phi))
                                 + t2 * (std::sin(theta) * std::sin(phi));
        const double speed = characteristic_speed_m_s * (0.5 + rng.next_float());
        DestructionEjecta p;
        p.index = i;
        p.mass_kg = per_mass;
        p.position = ev.target_position + direction * (ev.target_radius_m > 0.0 ? ev.target_radius_m : 0.0);
        p.velocity = ev.target_velocity + direction * speed;
        p.kinetic_energy_j = 0.5 * per_mass * speed * speed;
        p.created_at_s = ev.sim_time_s;
        out.push_back(p);
    }
    return DestErr::OK;
}

// ---- orchestration ---------------------------------------------------------
DestErr dest_execute_impact(const DestructionImpactEvent& ev, const DestructionConfig& cfg,
                            long long seed, DestructionDamageState current_state,
                            double ejecta_mass_fraction, double characteristic_ejecta_speed_m_s,
                            DestructionImpactResult& out) {
    out = DestructionImpactResult{};
    DestErr e = dest_validate_impact(ev, cfg);
    if (e != DestErr::OK) return e;

    DestructionImpactEnergy energy;
    e = dest_compute_impact_energy(ev, cfg, 0.5, energy);
    if (e != DestErr::OK) return e;
    if (energy.kinetic_energy_j < cfg.limits.min_impact_energy_j) return DestErr::LIMIT_EXCEEDED;

    DestructionImpactMomentum momentum;
    e = dest_compute_impact_momentum(ev, cfg, momentum);
    if (e != DestErr::OK) return e;
    DestructionImpactGeometry geometry;
    e = dest_compute_impact_geometry(ev, cfg, geometry);
    if (e != DestErr::OK) return e;

    DestructionDamageState after = current_state;
    e = dest_decide_damage(current_state, energy, ev, after);
    if (e != DestErr::OK) return e;

    // make_impact_rng(seed, "destruction.impact"): fresh stream per impact.
    PyMt19937 rng(seed);

    std::vector<DestructionFragment> fragments;
    if (after == DestructionDamageState::FRACTURED || after == DestructionDamageState::FRAGMENTED ||
        after == DestructionDamageState::DESTROYED) {
        e = dest_fragment_target(ev, energy, after, ev.target_radius_m, cfg, rng, fragments);
        if (e != DestErr::OK) return e;
    }
    std::vector<DestructionEjecta> ejecta;
    e = dest_generate_ejecta(ev, energy, cfg, rng, ejecta_mass_fraction,
                             characteristic_ejecta_speed_m_s, ejecta);
    if (e != DestErr::OK) return e;

    const int debris = (int)fragments.size() + (int)ejecta.size();
    if (debris > cfg.limits.max_debris_per_impact) return DestErr::LIMIT_EXCEEDED;

    out.geometry = geometry;
    out.energy = energy;
    out.momentum = momentum;
    out.state_before = current_state;
    out.state_after = after;
    out.fragments = std::move(fragments);
    out.ejecta = std::move(ejecta);
    out.debris_count = debris;
    return DestErr::OK;
}

// ---- secondary sweep ---------------------------------------------------------
DestErr dest_secondary_event_for_pair(const DestructionImpactEvent& parent,
                                      const DestructionFragment& frag,
                                      const DestructionImpactEvent& target_like,
                                      const DestructionConfig& cfg, int case_index,
                                      bool& has, DestructionImpactEvent& child) {
    has = false;
    const double target_radius = target_like.target_radius_m;
    if (!(target_radius > 0.0)) return DestErr::OK; // no surface: cannot re-impact
    // (self-pairing is excluded by the sweep caller via index/id compare)

    const DestVec3 p0 = frag.position - target_like.target_position;
    const DestVec3 v = frag.velocity - target_like.target_velocity;
    const double v_sq = v.magnitude_sq();
    if (std::sqrt(v_sq) < cfg.limits.min_relative_speed_m_s) return DestErr::OK;
    // NOTE: authority uses v_sq ** 0.5 (pow), not sqrt — identical double
    // results for finite non-negative inputs? CPython pow(x, 0.5) calls
    // pow(); libm pow(x,0.5) == sqrt(x) bit-exact on glibc; keep pow to be
    // exact cross-platform:
    const double t_star = -p0.dot(v) / v_sq;
    if (t_star <= 0.0) return DestErr::OK;
    const double closest = (p0 + v * t_star).magnitude();
    if (closest >= target_radius) return DestErr::OK;

    const double m_red = (frag.mass_kg * target_like.target_mass_kg) /
                         (frag.mass_kg + target_like.target_mass_kg);
    const double ke = 0.5 * m_red * v_sq;
    if (ke < cfg.limits.min_impact_energy_j) return DestErr::OK;

    child = DestructionImpactEvent{};
    child.impact_id = parent.impact_id + ":secondary:" + std::to_string(case_index);
    child.impactor_id = parent.target_id + ":frag:" + std::to_string(frag.index);
    child.target_id = target_like.target_id;
    child.sim_time_s = parent.sim_time_s + t_star;
    child.impactor_mass_kg = frag.mass_kg;
    child.target_mass_kg = target_like.target_mass_kg;
    child.impactor_position = frag.position;
    child.target_position = target_like.target_position;
    child.impactor_velocity = frag.velocity;
    child.target_velocity = target_like.target_velocity;
    child.impactor_radius_m = 0.0;
    child.target_radius_m = target_radius;
    has = true;
    return DestErr::OK;
}

DestErr dest_execute_secondary_impacts(const DestructionImpactEvent& parent_event,
                                       const DestructionImpactResult& parent_result,
                                       const std::vector<DestructionImpactEvent>& targets,
                                       const std::vector<DestructionDamageState>& target_states,
                                       const DestructionConfig& cfg,
                                       long long seed, int max_secondary,
                                       std::vector<DestructionSecondaryResult>& out) {
    out.clear();
    if (max_secondary < 0) return DestErr::NUMERICAL;
    int case_index = 0;
    for (const auto& frag : parent_result.fragments) {
        for (size_t ti = 0; ti < targets.size(); ++ti) {
            if ((int)out.size() >= max_secondary) break;
            const auto& t = targets[ti];
            // authority: target.body_id == fragment.fragment_id -> skip
            if (t.target_id == parent_event.target_id + ":frag:" + std::to_string(frag.index)) { ++case_index; continue; }
            bool has = false;
            DestructionImpactEvent child;
            DestErr e = dest_secondary_event_for_pair(parent_event, frag, t, cfg, case_index, has, child);
            ++case_index;
            if (e != DestErr::OK) return e;
            if (!has) continue;
            DestructionSecondaryResult sr;
            sr.event = child;
            sr.case_index = case_index - 1;
            const DestructionDamageState st = (ti < target_states.size())
                ? target_states[ti] : DestructionDamageState::INTACT;
            e = dest_execute_impact(child, cfg, seed + (case_index - 1),
                                    st, 0.05, 100.0, sr.result);
            // deterministic skip: fragments/children failing impact-level
            // validation are skipped (authority: execute_impact raises for
            // validated failures — the pre-floors already mirror the common
            // skips; any residual error HERE would be a real validation
            // failure, so propagate).
            if (e != DestErr::OK) return e;
            out.push_back(std::move(sr));
        }
    }
    return DestErr::OK;
}

// ---- orbit classification ----------------------------------------------------
const char* dest_orbit_class_name(DestructionOrbitClass c) {
    switch (c) {
    case DestructionOrbitClass::BOUND: return "BOUND";
    case DestructionOrbitClass::PARABOLIC: return "PARABOLIC";
    case DestructionOrbitClass::HYPERBOLIC: return "HYPERBOLIC";
    }
    return "?";
}

DestErr dest_classify_fragment_orbit(const DestructionFragment& frag,
                                     double central_mass_kg,
                                     const DestVec3& central_position,
                                     const DestVec3& central_velocity,
                                     DestructionOrbitClass& out_class,
                                     double& out_eps, double& out_mu) {
    if (!(central_mass_kg > 0.0) || std::isnan(central_mass_kg)) return DestErr::NUMERICAL;
    const double mu = DESTRUCTION_G * (central_mass_kg + frag.mass_kg);
    const DestVec3 rel_pos = frag.position - central_position;
    const DestVec3 rel_vel = frag.velocity - central_velocity;
    const double r = rel_pos.magnitude();
    if (r == 0.0) return DestErr::NUMERICAL;
    const double v = rel_vel.magnitude();
    const double eps = 0.5 * v * v - mu / r;
    const double atol = 1e-9;
    out_class = (eps > atol) ? DestructionOrbitClass::HYPERBOLIC
              : (eps < -atol ? DestructionOrbitClass::BOUND : DestructionOrbitClass::PARABOLIC);
    out_eps = eps;
    out_mu = mu;
    return DestErr::OK;
}

} // namespace astra::app
