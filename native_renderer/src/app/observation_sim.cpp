// observation_sim.cpp — native mirror of astra.temporal (v1.3 domain 3).
// Implementations keep the authority's FORTRAN-free op chains bit-for-bit:
//   * _event_at epsilon: 1e-12 * max(1.0, endpoint)
//   * interpolate: exact-equality probe, then w = (t-t0)/(t1-t0) linear
//   * observe(): honour-bracket checks BEFORE numerics, g(t) bisection with
//       lo/hi updates exactly as Python (`lo = mid` iff g(mid) >= 0.0),
//       final t_emit = 0.5 * (lo + hi)
//   * range delay math.sqrt(dx*dx + dy*dy + dz*dz) / SPEED_OF_LIGHT, NOT
//       math.hypot — NO py_math.h port here (documented: authority computes
//       naive-square-root chain, mirror keeps it, same bit pattern on glibc).
//   * _check_time: finite && >= 0 (bool extraneous for doubles).
//   * CPython 80-iteration fixed bisection loop.
//   * TemporalClock accumulators: sim/coord += dt, proper += dt * rate.

#include "app/observation_sim.h"
#include "app/py_math.h"

#include <cmath>

namespace astra::app {

namespace {

bool inan_inf_or_neg(double v) {
    return std::isnan(v) || std::isinf(v) || v < 0.0;
}

// _check_time float semantics (bool never reaches here in C++).
bool check_time_ok(double v) { return !inan_inf_or_neg(v); }

// range_delay via interpolate+event_at at time t on a pure-cartesian line.
double range_delay(const TempWorldline& hist, double tsec,
                   const double xo[3]) {
    TempChartEvent e{};
    // Assume valid: observe() guaranteed valid bracket before calling.
    temp_event_at(hist, tsec, e);
    const double dx = e.x - xo[0];
    const double dy = e.y - xo[1];
    const double dz = e.z - xo[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz) / SPEED_OF_LIGHT;
}

} // namespace

const char* temp_err_name(TempErr e) {
    switch (e) {
        case TempErr::OK: return "OK";
        case TempErr::INVALID_STATE: return "InvalidTemporalStateError";
        case TempErr::INVALID_WORLDLINE: return "InvalidWorldlineError";
        case TempErr::HISTORY_UNAVAILABLE: return "TemporalHistoryUnavailableError";
        case TempErr::SPACELIKE: return "SpacelikeIntervalError";
        case TempErr::ST_ERR: return "SpacetimeError";
    }
    return "?";
}

const char* causal_relation_name(CausalRelation r) {
    switch (r) {
        case CausalRelation::COINCIDENT: return "COINCIDENT";
        case CausalRelation::A_PRECEDES_B: return "A_PRECEDES_B";
        case CausalRelation::B_PRECEDES_A: return "B_PRECEDES_A";
        case CausalRelation::CAUSALLY_DISCONNECTED: return "CAUSALLY_DISCONNECTED";
    }
    return "?";
}

const char* light_cone_region_name(LightConeRegion r) {
    switch (r) {
        case LightConeRegion::COINCIDENT: return "COINCIDENT";
        case LightConeRegion::INSIDE_FUTURE_CONE: return "INSIDE_FUTURE_CONE";
        case LightConeRegion::ON_FUTURE_CONE: return "ON_FUTURE_CONE";
        case LightConeRegion::INSIDE_PAST_CONE: return "INSIDE_PAST_CONE";
        case LightConeRegion::ON_PAST_CONE: return "ON_PAST_CONE";
        case LightConeRegion::SPACELIKE_EXTERIOR: return "SPACELIKE_EXTERIOR";
    }
    return "?";
}

TempErr temp_cartesian_worldline(const TempWorldline& history, TempWorldline& out) {
    if (history.events.size() < 2 || history.params.size() != history.events.size()) {
        return TempErr::INVALID_WORLDLINE;
    }
    out.params = history.params;
    out.events = history.events;
    bool any_noncart = false;
    for (const auto& e : out.events) if (!e.cartesian) any_noncart = true;
    if (!any_noncart) return TempErr::OK;
    // Authority converts each spherical sample via spacetime.spherical_to_cartesian.
    for (auto& e : out.events) {
        if (e.cartesian) continue;
        double x, y, z;
        if (st_spherical_to_cartesian(e.x, e.y, e.z, x, y, z) != StErr::OK) {
            return TempErr::ST_ERR;
        }
        e.x = x; e.y = y; e.z = z; e.cartesian = true;
    }
    return TempErr::OK;
}

TempErr temp_interpolate(const TempWorldline& history, double t_emit,
                         TempChartEvent& out) {
    const auto& params = history.params;
    const auto& events = history.events;
    const size_t n = params.size();
    if (n < 2) return TempErr::INVALID_WORLDLINE;
    // Exact recorded-sample hit: return the recorded event unchanged.
    for (size_t i = 0; i < n; ++i) {
        if (params[i] == t_emit) { out = events[i]; return TempErr::OK; }
    }
    // lo = LARGEST index with params[i] <= t_emit (Python max over generator —
    // loop all, keep the largest satisfying index; matches unsorted input too).
    long long lo = -1;
    for (size_t i = 0; i < n; ++i) {
        if (params[i] <= t_emit) lo = (long long)i;
    }
    if (lo < 0 || (size_t)lo + 1 >= n) return TempErr::HISTORY_UNAVAILABLE;
    const double t0 = params[lo], t1 = params[(size_t)lo + 1];
    const auto& e0 = events[lo];
    const auto& e1 = events[(size_t)lo + 1];
    const double w = (t_emit - t0) / (t1 - t0);
    out.ct_m = e0.ct_m + w * (e1.ct_m - e0.ct_m);
    out.x = e0.x + w * (e1.x - e0.x);
    out.y = e0.y + w * (e1.y - e0.y);
    out.z = e0.z + w * (e1.z - e0.z);
    out.cartesian = true;
    return TempErr::OK;
}

TempErr temp_event_at(const TempWorldline& history, double t_emit,
                      TempChartEvent& out) {
    const size_t n = history.params.size();
    if (n < 2) return TempErr::INVALID_WORLDLINE;
    const double p0 = history.params.front();
    const double pn = history.params.back();
    // eps = 1e-12 * max(1.0, |endpoint|) with the SAME operand order as Python
    // 1e-12 * max(1.0, params[0]) etc.
    if (t_emit < p0 - 1e-12 * (1.0 > p0 ? 1.0 : p0)) {
        return TempErr::HISTORY_UNAVAILABLE;
    }
    if (t_emit > pn + 1e-12 * (1.0 > pn ? 1.0 : pn)) {
        return TempErr::HISTORY_UNAVAILABLE;
    }
    const double lo_clamped = t_emit > p0 ? t_emit : p0;
    const double clamped = lo_clamped < pn ? lo_clamped : pn;
    return temp_interpolate(history, clamped, out);
}

TempErr temp_observe(const TempWorldline& history, const double observer_position[3],
                     double observation_time_s, const std::string& observer,
                     ObservedState& out) {
    // _check_time(observation_time_s, "observation_time_s")
    if (!check_time_ok(observation_time_s)) return TempErr::INVALID_STATE;
    // observer_position validation
    if (!observer_position) return TempErr::INVALID_STATE;
    double xo[3] = {observer_position[0], observer_position[1], observer_position[2]};
    for (double v : xo) if (std::isnan(v) || std::isinf(v)) return TempErr::INVALID_STATE;
    if (observer.empty()) return TempErr::INVALID_STATE;

    TempWorldline hist;
    TempErr er = temp_cartesian_worldline(history, hist);
    if (er != TempErr::OK) return er;
    const size_t n = hist.params.size();
    const double p_first = hist.params.front();
    const double p_last = hist.params.back();

    // Bracket check BEFORE solving (honesty precedes numerics).
    {
        const double delay_first = range_delay(hist, p_first, xo);
        if (observation_time_s - p_first - delay_first < 0.0 && p_first < observation_time_s) {
            return TempErr::HISTORY_UNAVAILABLE;
        }
    }

    // hi = min(t_obs, params[-1]); if hi < t_obs -> history ends too early.
    const double hi = observation_time_s < p_last ? observation_time_s : p_last;
    if (hi < observation_time_s) return TempErr::HISTORY_UNAVAILABLE;
    double lo = p_first;
    if (lo > observation_time_s) return TempErr::HISTORY_UNAVAILABLE;

    const double g_lo = observation_time_s - lo - range_delay(hist, lo, xo);
    if (g_lo < 0.0) return TempErr::HISTORY_UNAVAILABLE;

    // Deterministic bisection, 80 halvings (CPython literally: for _ in range(80)).
    // NOTE: hi/lo freely updated exactly as the authority does; hi starts as
    // min(t_obs, p_last) which equals t_obs after the guard above.
    double lo_b = lo;
    double hi_b = hi;
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo_b + hi_b);
        if (observation_time_s - mid - range_delay(hist, mid, xo) >= 0.0) {
            lo_b = mid;
        } else {
            hi_b = mid;
        }
    }
    const double t_emit = 0.5 * (lo_b + hi_b);

    TempChartEvent emission{};
    er = temp_event_at(hist, t_emit, emission);
    if (er != TempErr::OK) return er;

    // actual state at t_obs required to be recorded (no fabrication).
    if (observation_time_s > p_last) return TempErr::HISTORY_UNAVAILABLE;
    TempChartEvent actual{};
    er = temp_event_at(hist, observation_time_s, actual);
    if (er != TempErr::OK) return er;

    out.observation_time_s = observation_time_s;
    out.emission_time_s = t_emit;
    out.lookback_time_s = observation_time_s - t_emit;
    out.emission_event = emission;
    out.actual_state_at_observation = actual;
    out.observer = observer;
    return TempErr::OK;
}

TempErr temp_lookback_time(const double observer_position[3],
                           const double emission_position[3],
                           double observation_time_s, double emission_time_s,
                           double& out_s) {
    if (!check_time_ok(observation_time_s)) return TempErr::INVALID_STATE;
    if (!check_time_ok(emission_time_s)) return TempErr::INVALID_STATE;
    if (emission_time_s > observation_time_s) return TempErr::INVALID_STATE;
    double d[3];
    for (int i = 0; i < 3; ++i) d[i] = observer_position[i] - emission_position[i];
    for (double v : d) if (std::isnan(v) || std::isinf(v)) return TempErr::INVALID_STATE;
    const double dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    out_s = dist / SPEED_OF_LIGHT;
    return TempErr::OK;
}

// ---------------------------------------------------------------------------
// Causal policy (temporal.causal)
// ---------------------------------------------------------------------------

TempErr temp_causal_relate(const StMetric& metric, const double a4[4], const double b4[4],
                           double tolerance, CausalRelation& out) {
    bool identical = a4[0] == b4[0] && a4[1] == b4[1] && a4[2] == b4[2] && a4[3] == b4[3];
    if (identical) { out = CausalRelation::COINCIDENT; return TempErr::OK; }
    IntervalType kind;
    const StErr se = st_classify_interval(metric, a4, b4, kind, tolerance);
    if (se != StErr::OK) return TempErr::ST_ERR;
    if (kind == IntervalType::SPACELIKE) { out = CausalRelation::CAUSALLY_DISCONNECTED; return TempErr::OK; }
    if (b4[0] > a4[0]) { out = CausalRelation::A_PRECEDES_B; return TempErr::OK; }
    if (b4[0] < a4[0]) { out = CausalRelation::B_PRECEDES_A; return TempErr::OK; }
    out = CausalRelation::CAUSALLY_DISCONNECTED; // dt==0 non-spacelike: honest report
    return TempErr::OK;
}

TempErr temp_light_cone_region(const StMetric& metric, const double a4[4], const double b4[4],
                               double tolerance, LightConeRegion& out) {
    bool identical = a4[0] == b4[0] && a4[1] == b4[1] && a4[2] == b4[2] && a4[3] == b4[3];
    if (identical) { out = LightConeRegion::COINCIDENT; return TempErr::OK; }
    IntervalType kind;
    const StErr se = st_classify_interval(metric, a4, b4, kind, tolerance);
    if (se != StErr::OK) return TempErr::ST_ERR;
    if (kind == IntervalType::SPACELIKE) { out = LightConeRegion::SPACELIKE_EXTERIOR; return TempErr::OK; }
    const bool future = b4[0] > a4[0];
    if (kind == IntervalType::NULLI) {
        out = future ? LightConeRegion::ON_FUTURE_CONE : LightConeRegion::ON_PAST_CONE;
    } else {
        out = future ? LightConeRegion::INSIDE_FUTURE_CONE : LightConeRegion::INSIDE_PAST_CONE;
    }
    return TempErr::OK;
}

TempErr temp_is_causally_accessible(const StMetric& metric, const double a4[4], const double b4[4],
                                    double tolerance, bool& out) {
    LightConeRegion r;
    const TempErr te = temp_light_cone_region(metric, a4, b4, tolerance, r);
    if (te != TempErr::OK) return te;
    out = (r == LightConeRegion::INSIDE_FUTURE_CONE || r == LightConeRegion::ON_FUTURE_CONE);
    return TempErr::OK;
}

// ---------------------------------------------------------------------------
// Proper time (temporal.proper_time)
// ---------------------------------------------------------------------------

TempErr temp_flat_proper_time(const TempWorldline& history, double& out_s) {
    TempWorldline hist;
    const TempErr er = temp_cartesian_worldline(history, hist);
    if (er != TempErr::OK) return er;
    // _to_relativity_event: cart -> SpacetimeEvent(ct_m, x, y, z) direct.
    double total = 0.0;
    for (size_t i = 0; i + 1 < hist.events.size(); ++i) {
        const auto& a = hist.events[i];
        const auto& b = hist.events[i + 1];
        const RelFourVector ra{a.ct_m, a.x, a.y, a.z};
        const RelFourVector rb{b.ct_m, b.x, b.y, b.z};
        double seg = 0.0;
        const RelErr re = proper_time_between(ra, rb, seg);
        if (re == RelErr::SPACELIKE_INTERVAL) return TempErr::SPACELIKE;
        if (re != RelErr::OK) return TempErr::INVALID_STATE;
        total += seg;
    }
    out_s = total;
    return TempErr::OK;
}

TempErr temp_velocity_time_dilation(double coordinate_time_s, const RelVec3& velocity_mps,
                                    double& out_s) {
    if (!check_time_ok(coordinate_time_s)) return TempErr::INVALID_STATE;
    double g;
    const RelErr r = lorentz_factor(relvec_magnitude(velocity_mps), g);
    if (r != RelErr::OK) return TempErr::INVALID_STATE;
    out_s = coordinate_time_s / g;
    return TempErr::OK;
}

TempErr temp_gravitational_time_dilation(double coordinate_time_s, double mass_kg,
                                         double radius_m, double& out_s) {
    if (!check_time_ok(coordinate_time_s)) return TempErr::INVALID_STATE;
    double dt_dtau;
    const RelErr r = weak_field_time_dilation(mass_kg, radius_m, dt_dtau);
    if (r != RelErr::OK) return TempErr::INVALID_STATE;
    out_s = coordinate_time_s / dt_dtau;
    return TempErr::OK;
}

// ---------------------------------------------------------------------------
// TemporalClock (temporal.clock)
// ---------------------------------------------------------------------------

TempErr TemporalClock::init(const std::string& observer, double sim_t, double coord_t,
                            double proper_t) {
    if (observer.empty()) return TempErr::INVALID_STATE;
    if (inan_inf_or_neg(sim_t) || inan_inf_or_neg(coord_t) || inan_inf_or_neg(proper_t)) {
        return TempErr::INVALID_STATE;
    }
    st.observer = observer;
    st.simulation_time_s = sim_t;
    st.coordinate_time_s = coord_t;
    st.proper_time_s = proper_t;
    st.rate = 1.0;
    return TempErr::OK;
}

TempErr TemporalClock::set_rate(double rate) {
    if (std::isnan(rate) || std::isinf(rate) || rate <= 0.0) return TempErr::INVALID_STATE;
    st.rate = rate;
    return TempErr::OK;
}

TempErr TemporalClock::advance(double dt_s) {
    if (std::isnan(dt_s) || std::isinf(dt_s) || dt_s < 0.0) return TempErr::INVALID_STATE;
    st.simulation_time_s += dt_s;
    st.coordinate_time_s += dt_s;
    st.proper_time_s += dt_s * st.rate;
    return TempErr::OK;
}

TempErr TemporalClock::reset(double sim_t, double coord_t, double proper_t) {
    if (inan_inf_or_neg(sim_t) || inan_inf_or_neg(coord_t) || inan_inf_or_neg(proper_t)) {
        return TempErr::INVALID_STATE;
    }
    st.simulation_time_s = sim_t;
    st.coordinate_time_s = coord_t;
    st.proper_time_s = proper_t;
    st.rate = 1.0;
    return TempErr::OK;
}

} // namespace astra::app
