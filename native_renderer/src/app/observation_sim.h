// observation_sim.h — native mirror of astra.temporal (Observation & Cosmic
// History, v1.3 domain 3). Every formula, comparison, draw order and error
// boundary mirrors the Python authorities exactly; deviations are commented
// in the .cpp. No invented physics (ABERRATION / precession / Shapiro delay
// are NOT in the authority and NOT added here).
//
// Section map (authority file -> mirrored surface):
//   observation.py:  observe() retarded-time bisection (80 iterations),
//                    _event_at interpolation with exact epsilon clamps,
//                    lookback_time() geometric delay (math.sqrt chain,
//                    NOT hypot — see below)
//   causal.py:       CausalRelation / LightConeRegion ordering policy on top
//                    of st_classify_interval (spacetime mirror, v1.2)
//   proper_time.py:  flat_proper_time (segment chain via relativity mirror),
//                    velocity_time_dilation (dt/gamma), gravitational_time_
//                    dilation (dt / dt_dtau via weak-field mirror)
//   clock.py:        TemporalClock accumulator (authority gate note below)
//   state.py:        TemporalState value type
//   exotic.py:       NOT mirrored here — depends on astra.theoretical metrics
//                    (warp/wormhole/white-hole) that are out of v1.3 scope;
//                    declared in the report as NOT CONNECTED, not faked.
//
// Determinism: pure functions, no wall clock, no globals, no RNG (same as
// authority — clock.py is explicitly documented as RNG-free).
//
// Error mapping (exact classes, no string matching):
//   InvalidTemporalStateError      <- obs  : TIMPOBS_INVALID_STATE
//   InvalidWorldlineError          <- obs  : TIMPOBS_INVALID_WORLDLINE
//   TemporalHistoryUnavailableError<- obs  : TIMPOBS_HISTORY_UNAVAILABLE
//   SpacelikeIntervalError         <- rel  : SPACELIKE_INTERVAL (via RelErr)
//   DegenerateMetricError          <- rel  : DEGENERATE (via RelErr)
//   InvalidCoordinateError etc.    <- st   : StErr (causal passthrough)
//
// Tick note: std::sqrt == math.sqrt (both correctly rounded glibc); the
// authority uses math.sqrt(dx*dx+dy*dy+dz*dz) here, NOT math.hypot — mirrored
// as the identical op chain, NOT the py_math.h hypot port.
//
// CRITICAL NUMERICAL NOTE (documented parity risk, mirrors authority):
//   observer-position tuple conversion float(v) is identity for doubles.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/relativity_sim.h"
#include "app/spacetime_sim.h"

namespace astra::app {

// ---------------------------------------------------------------------------
// Errors (mirror of astra.temporal.exceptions taxonomy)
// ---------------------------------------------------------------------------
enum class TempErr {
    OK = 0,
    INVALID_STATE,        // InvalidTemporalStateError
    INVALID_WORLDLINE,    // InvalidWorldlineError
    HISTORY_UNAVAILABLE,  // TemporalHistoryUnavailableError
    SPACELIKE,            // SpacelikeIntervalError propagated from relativity
    ST_ERR,               // spacetime-layer error (see st detail in caller)
};

const char* temp_err_name(TempErr e);

// ---------------------------------------------------------------------------
// Chart event + worldline (mirror of astra.spacetime.events.Worldline usage)
// ---------------------------------------------------------------------------
struct TempChartEvent {
    double ct_m = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    bool cartesian = true; // false => spherical (converted via st_ mirror)
};

struct TempWorldline {
    // parameters[i] is the coordinate time (s) of events[i]; s recorded order.
    std::vector<double> params;
    std::vector<TempChartEvent> events;
};

// _cartesian_worldline: >=2 samples; spherical entries converted pairwise.
TempErr temp_cartesian_worldline(const TempWorldline& history, TempWorldline& out);

// _interpolate: exact hit returns the recorded event; otherwise linear
// strictly between samples (designed authority contract).
TempErr temp_interpolate(const TempWorldline& history, double t_emit,
                         TempChartEvent& out);

// _event_at: bracket clamps with eps = 1e-12 * max(1.0, endpoint),
// then _interpolate(min(max(t_emit, p0), pN)).
TempErr temp_event_at(const TempWorldline& history, double t_emit,
                      TempChartEvent& out);

// ---------------------------------------------------------------------------
// observe() — flat null-propagation retarded time on recorded history.
// ---------------------------------------------------------------------------
struct ObservedState {
    double observation_time_s = 0.0;
    double emission_time_s = 0.0;
    double lookback_time_s = 0.0;
    TempChartEvent emission_event{};
    TempChartEvent actual_state_at_observation{};
    std::string observer;
};

TempErr temp_observe(const TempWorldline& history, const double observer_position[3],
                     double observation_time_s, const std::string& observer,
                     ObservedState& out);

// lookback_time: |x_obs - x_emit| / c with exact double op-chain sqrt(sum).
TempErr temp_lookback_time(const double observer_position[3],
                           const double emission_position[3],
                           double observation_time_s, double emission_time_s,
                           double& out_s);

// ---------------------------------------------------------------------------
// Causal ordering policy (mirror of temporal.causal on top of spacetime).
// ---------------------------------------------------------------------------
enum class CausalRelation { COINCIDENT, A_PRECEDES_B, B_PRECEDES_A, CAUSALLY_DISCONNECTED };
const char* causal_relation_name(CausalRelation r);

enum class LightConeRegion {
    COINCIDENT, INSIDE_FUTURE_CONE, ON_FUTURE_CONE,
    INSIDE_PAST_CONE, ON_PAST_CONE, SPACELIKE_EXTERIOR
};
const char* light_cone_region_name(LightConeRegion r);

TempErr temp_causal_relate(const StMetric& metric, const double a4[4], const double b4[4],
                           double tolerance, CausalRelation& out);
TempErr temp_light_cone_region(const StMetric& metric, const double a4[4], const double b4[4],
                               double tolerance, LightConeRegion& out);
TempErr temp_is_causally_accessible(const StMetric& metric, const double a4[4], const double b4[4],
                                    double tolerance, bool& out);

// ---------------------------------------------------------------------------
// Proper time (proper_time.py mirrors, composed on relativity_sim).
// ---------------------------------------------------------------------------
TempErr temp_flat_proper_time(const TempWorldline& history, double& out_s);
TempErr temp_velocity_time_dilation(double coordinate_time_s, const RelVec3& velocity_mps,
                                    double& out_s);
TempErr temp_gravitational_time_dilation(double coordinate_time_s, double mass_kg,
                                         double radius_m, double& out_s);

// ---------------------------------------------------------------------------
// TemporalClock / TemporalState (clock.py / state.py mirrors).
// NOTE: clock.py gates mutation with astra.core.threading.AuthorityContext;
// the native mirror lives on the sim thread anyway (same architectural gate);
// require_authority is a no-op here, exactly as documented for SimLadder's
// simulated-thread authority shim.
// ---------------------------------------------------------------------------
struct TemporalState {
    double simulation_time_s = 0.0;
    double coordinate_time_s = 0.0;
    double proper_time_s = 0.0;
    double rate = 1.0;
    std::string observer;
};

struct TemporalClock {
    TemporalState st{}; // mutable accumulators (sole mutable object, as authority)

    TempErr init(const std::string& observer, double sim_t, double coord_t, double proper_t);
    TempErr set_rate(double rate);
    TempErr advance(double dt_s);
    TempErr reset(double sim_t, double coord_t, double proper_t);
};

} // namespace astra::app
