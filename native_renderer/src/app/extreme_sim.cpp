// ASTRA v1.5 — EXTREME SPACETIME native mirror implementation.
#include "app/extreme_sim.h"

#include <algorithm>
#include <cstdio>
#include "app/relativity_sim.h"

namespace astra {
namespace v15 {

double lorentz_gamma(double v) {
    double g = 0.0;
    if (astra::app::lorentz_factor(v, g) != astra::app::RelErr::OK) {
        return 1.0 / 0.0;  // guard violation sentinel (+inf); plans refuse beta>=1 upstream
    }
    return g;
}

}  // namespace v15
}  // namespace astra

namespace astra {
namespace v15 {

static bool fin(double x) { return std::isfinite(x); }
static double dist3(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
static Vec3 lerp3(const Vec3& a, const Vec3& b, double lam) {
    // Identical expression tree to Python _lerp: a + (b-a)*lam per component.
    return Vec3{a.x + (b.x - a.x) * lam, a.y + (b.y - a.y) * lam, a.z + (b.z - a.z) * lam};
}

double alcubierre_shape(double rs, double R, double sigma) {
    const double denom = 2.0 * std::tanh(sigma * R);
    return (std::tanh(sigma * (rs + R)) - std::tanh(sigma * (rs - R))) / denom;
}

// ------------------------- TraversalPlan -------------------------------------

bool build_traversal_plan(TraversalPlan& p, std::string& err) {
    if (!fin(p.throat_radius_m) || p.throat_radius_m <= 0.0) { err = "throat_radius_m must be finite > 0"; return false; }
    if (!fin(p.v) || !(p.v > 0.0 && p.v < ASTRA_C)) { err = "transit speed must be 0 < v < c (beta>=1 rejected)"; return false; }
    if (!fin(p.L) || p.L <= 0.0) { err = "observer_scale_m must be > 0"; return false; }
    if (!fin(p.dt) || p.dt <= 0.0) { err = "dt_step_s must be > 0"; return false; }
    p.d_approach = dist3(p.start, p.origin);
    if (p.d_approach > 0.0 && p.d_approach <= p.throat_radius_m) {
        err = "observer starts inside throat sphere but not at mouth"; return false;
    }
    p.d_throat = 2.0 * p.throat_radius_m;
    p.gamma = lorentz_gamma(p.v);
    p.grav_factor = 1.0;  // exp(0.0) — engine default phi==0 path (documented)
    p.t_approach = p.d_approach / p.v;
    p.t_throat = p.d_throat / p.v + (p.d_throat / p.v);  // entry+egress mirror (2*d_throat/v)
    p.t_total = p.t_approach + p.t_throat;
    p.tau_total = (p.t_approach + p.t_throat) / p.gamma / p.grav_factor;
    p.b_entry = p.t_approach;
    p.b_transit = p.t_approach + p.t_throat * 0.25;
    p.b_exit = p.t_approach + p.t_throat * 0.75;
    p.b_done = p.t_total;
    return true;
}

TravState TraversalPlan::state_at(double t) const {
    if (t < b_entry) return TravState::APPROACHING;
    if (t < b_transit) return TravState::ENTRY;
    if (t < b_exit) return TravState::TRANSIT;
    if (t < b_done) return TravState::EXIT;
    return TravState::COMPLETE;
}

Vec3 TraversalPlan::position_at(double t) const {
    if (t <= b_entry) {
        if (d_approach == 0.0) return origin;
        const double lam = std::min((v * t) / d_approach, 1.0);
        return lerp3(start, origin, lam);
    }
    if (t <= b_done) {
        const double frac = std::min((v * (t - b_entry)) / (2.0 * d_throat), 1.0);
        return lerp3(origin, destination, frac);
    }
    return destination;
}

double TraversalPlan::proper_time_at(double t) const {
    return std::min(t, t_total) / (gamma * grav_factor);
}

// ------------------------- WarpPlan ------------------------------------------

bool build_warp_plan(WarpPlan& p, std::string& err) {
    if (!fin(p.vs) || p.vs <= 0.0) { err = "v_chart_mps must be finite > 0"; return false; }
    if (p.vs <= 0.0 || p.vs > 1.0e4 * ASTRA_C) { err = "v_chart_mps > 1e4 c: numerical guard"; return false; }
    if (!fin(p.R) || p.R <= 0.0) { err = "bubble_radius_m must be > 0"; return false; }
    if (!fin(p.sigma) || p.sigma <= 0.0) { err = "wall_steepness must be > 0"; return false; }
    if (p.sigma > 1.0e4) { err = "wall_steepness exceeds numerical cap 1e4"; return false; }
    p.d_total = dist3(p.origin, p.destination);
    if (p.d_total <= 0.0) { err = "origin == destination"; return false; }
    if (p.d_total < 2.0 * p.R) { err = "bubble radius exceeds half the chart leg"; return false; }
    p.t_total = p.d_total / p.vs;
    if (!fin(p.t_total)) { err = "journey time non-finite"; return false; }
    return true;
}

Vec3 WarpPlan::position_at(double t) const {
    const double lam = std::min(std::max(t, 0.0), t_total) / t_total;
    return lerp3(origin, destination, lam);
}

double WarpPlan::proper_time_at(double t) const {
    return std::min(std::max(t, 0.0), t_total);
}

// ------------------------- ConventionalPlan ----------------------------------

bool build_conventional_plan(ConventionalPlan& p, std::string& err) {
    if (!fin(p.v) || !(p.v > 0.0 && p.v < ASTRA_C)) { err = "speed must be 0 < v < c (beta>=1 rejected)"; return false; }
    p.d = dist3(p.origin, p.destination);
    if (p.d <= 0.0) { err = "origin == destination"; return false; }
    p.t = p.d / p.v;
    p.gamma = lorentz_gamma(p.v);
    return true;
}

Vec3 ConventionalPlan::position_at(double tq) const {
    const double lam = std::min(std::max(tq, 0.0), t) / t;
    return lerp3(origin, destination, lam);
}

double ConventionalPlan::proper_time_at(double tq) const {
    return std::min(std::max(tq, 0.0), t) / gamma;
}

// ------------------------- FSM ------------------------------------------------

bool TravelFSM::init_wormhole(const TraversalPlan& p, std::string& err) {
    wh_ = p; mech_ = TravelMechanism::WORMHOLE;
    state_ = TravState::IDLE; t_ = 0.0;
    return true;
}

bool TravelFSM::init_warp(const WarpPlan& p, std::string& err) {
    wp_ = p; mech_ = TravelMechanism::WARP;
    state_ = TravState::IDLE; t_ = 0.0;
    return true;
}

bool TravelFSM::init_conventional(const ConventionalPlan& p, std::string& err) {
    cv_ = p; mech_ = TravelMechanism::CONVENTIONAL_RELATIVISTIC;
    state_ = TravState::IDLE; t_ = 0.0;
    return true;
}

bool TravelFSM::is_terminal() const {
    return state_ == TravState::COMPLETE || state_ == TravState::ABORTED || state_ == TravState::INVALID;
}

TravState TravelFSM::begin() {
    if (state_ != TravState::IDLE) { state_ = TravState::INVALID; invalid_reason_ = "begin() from non-IDLE"; return state_; }
    switch (mech_) {
        case TravelMechanism::WORMHOLE: state_ = TravState::APPROACHING; break;
        default: state_ = TravState::TRANSIT; break;
    }
    return state_;
}

TravState TravelFSM::abort() {
    if (state_ == TravState::COMPLETE) { invalid_reason_ = "abort after COMPLETE refused"; state_ = TravState::INVALID; return state_; }
    state_ = TravState::ABORTED; aborted_ = true; return state_;
}

TravState TravelFSM::target_state() const {
    double total;
    switch (mech_) {
        case TravelMechanism::WORMHOLE: return wh_.state_at(t_);
        case TravelMechanism::WARP: total = wp_.t_total; break;
        default: total = cv_.t; break;
    }
    if (t_ < 0.5 * total) return TravState::TRANSIT;
    if (t_ < total) return TravState::EXIT;
    return TravState::COMPLETE;
}

void TravelFSM::walk_transition(TravState target) {
    // Walk the legal chain one band per call (mirrors Python _transition chain).
    static const TravState chain[] = {TravState::APPROACHING, TravState::ENTRY, TravState::TRANSIT, TravState::EXIT, TravState::COMPLETE};
    int i = -1, j = -1;
    for (int k = 0; k < 5; ++k) {
        if (chain[k] == state_) i = k;
        if (chain[k] == target) j = k;
    }
    if (i < 0 || j < 0) { state_ = TravState::INVALID; invalid_reason_ = "transition chain violation"; return; }
    state_ = chain[std::min(i + 1, j > i ? j : i)];
}

TravState TravelFSM::step() {
    const double dt = (mech_ == TravelMechanism::WORMHOLE) ? wh_.dt
                    : (mech_ == TravelMechanism::WARP ? wp_.dt : cv_.dt);
    return step(dt);
}

TravState TravelFSM::step(double dt) {
    if (state_ == TravState::IDLE) { state_ = TravState::INVALID; invalid_reason_ = "step() before begin()"; return state_; }
    if (is_terminal()) return state_;
    if (!(fin(dt) && dt > 0.0)) { state_ = TravState::INVALID; invalid_reason_ = "dt invalid"; return state_; }
    t_ += dt;
    TravState target = target_state();
    while (state_ != target && !is_terminal()) walk_transition(target);
    return state_;
}

Vec3 TravelFSM::position() const {
    switch (mech_) {
        case TravelMechanism::WORMHOLE: return wh_.position_at(t_);
        case TravelMechanism::WARP: return wp_.position_at(t_);
        default: return cv_.position_at(t_);
    }
}

double TravelFSM::proper_time_s() const {
    switch (mech_) {
        case TravelMechanism::WORMHOLE: return wh_.proper_time_at(t_);
        case TravelMechanism::WARP: return wp_.proper_time_at(t_);
        default: return cv_.proper_time_at(t_);
    }
}

// ------------------------- vocabulary ----------------------------------------

std::string causal_status_wormhole(double external_d, double transit_t) {
    if (transit_t <= 0.0) return "CAUSAL_CHECK_NOT_AVAILABLE (zero journey time)";
    if (transit_t < external_d / ASTRA_C)
        return "SPECULATIVE_ACAUSAL_EFFECTIVE (external chart light time exceeds throat transit)";
    return "CHART_CONSISTENT (transit longer than external light time)";
}

std::string causal_status_warp(double vs) {
    if (vs >= ASTRA_C)
        return "SPECULATIVE_ACAUSAL_EFFECTIVE_DISPLACEMENT (no local light-cone violation; global ordering not guaranteed by the model)";
    return "CAUSAL_CHART (sub-luminal effective displacement; local light cones exact)";
}

std::string causal_status_conventional(double proper_t, double coord_t) {
    char b[192];
    std::snprintf(b, sizeof(b), "CAUSAL_TIMELIKE (v < c; proper = coordinate/gamma)");
    (void)proper_t; (void)coord_t;
    return std::string(b);
}

const char* classification_geometry() { return "THEORETICAL"; }
const char* classification_traversal() { return "SPECULATIVE"; }
const char* classification_visual() { return "CINEMATIC"; }

}  // namespace v15
}  // namespace astra
