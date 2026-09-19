// ASTRA v1.5 — EXTREME SPACETIME native mirror (PHASE 21 cross-language).
//
// 1:1 mirror of the Python authorities:
//   astra/theoretical/traversal.py    (Morris-Thorne traversal plans + FSM)
//   astra/theoretical/warp_journey.py (Alcubierre journey separation)
//   astra/interaction/journey.py      (plans, causality classification)
//
// The Python side stays the SCIENTIFIC AUTHORITY; this mirror reproduces its
// arithmetic in double precision so the native renderer can consume the same
// validated state (RenderState) without re-deriving physics. The galilean
// rule holds: renderer never decides physics — it consumes this mirror.
//
// Classification (never silenced): metric math THEORETICAL; traversal/warp
// feasibility SPECULATIVE; visual presentation CINEMATIC where labeled.
#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace astra {
namespace v15 {

// Newton-meets-Einstein: the only constant this layer owns.
inline constexpr double ASTRA_C = 299792458.0;

enum class TravelMechanism : int {
    CONVENTIONAL_RELATIVISTIC = 0,
    WORMHOLE = 1,
    WARP = 2,
};

enum class TravState : int {
    IDLE = 0,
    APPROACHING = 1,
    ENTRY = 2,
    TRANSIT = 3,
    EXIT = 4,
    COMPLETE = 5,
    ABORTED = 6,
    INVALID = 7,
};

inline const char* trav_state_name(TravState s) {
    switch (s) {
        case TravState::IDLE: return "IDLE";
        case TravState::APPROACHING: return "APPROACHING";
        case TravState::ENTRY: return "ENTRY";
        case TravState::TRANSIT: return "TRANSIT";
        case TravState::EXIT: return "EXIT";
        case TravState::COMPLETE: return "COMPLETE";
        case TravState::ABORTED: return "ABORTED";
        case TravState::INVALID: return "INVALID";
    }
    return "INVALID";
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// ---- physics helpers (mirror relativity.lorentz_factor et al.) -------------

// gamma(v): identical formula tree to astra/relativity/core.py::lorentz_factor
inline double lorentz_gamma(double v) {
    const double beta2 = (v * v) / (ASTRA_C * ASTRA_C);
    return 1.0 / std::sqrt(1.0 - beta2);
}

// Alcubierre shape f(rs) — mirror of warp.py::shape_function (tanh-based).
double alcubierre_shape(double rs, double R, double sigma);

// Morris-Thorne default shape b(r) = rt * (rt/r)^0.5 (mirror of journey default).
inline double mt_default_shape(double r, double rt) {
    return rt * std::sqrt(rt / r);
}

// Tidal acceleration at throat: c^2 * L / rt^2 (model-limited estimate).
inline double tidal_at_throat(double rt, double L) {
    return (rt > 0.0) ? (ASTRA_C * ASTRA_C) * L / (rt * rt) : -1.0 /*NOT AVAILABLE sentinel*/;
}

// ----------------------------- plans ----------------------------------------

struct TraversalPlan {
    double throat_radius_m = 0.0;
    Vec3 origin;            // mouth_in position (chart)
    Vec3 destination;       // mouth_out position
    Vec3 start;             // observer start (== mouth in for engine legs)
    double v = 0.0;         // transit speed (0 < v < c)
    double L = 2.0;         // observer scale
    double dt = 1.0e-3;     // FSM tick (coordinate seconds)

    // computed (set by build_traversal_plan; 0 before)
    double d_approach = 0.0;
    double d_throat = 0.0;
    double gamma = 0.0;
    double grav_factor = 1.0;   // exp(phi(rt)); phi==0 in engine default path
    double t_approach = 0.0;
    double t_throat = 0.0;
    double t_total = 0.0;
    double tau_total = 0.0;
    double b_entry = 0.0;
    double b_transit = 0.0;
    double b_exit = 0.0;
    double b_done = 0.0;

    TravState state_at(double t) const;
    Vec3 position_at(double t) const;
    double proper_time_at(double t) const;
};

struct WarpPlan {
    double R = 0.0;
    double sigma = 0.0;
    double vs = 0.0;        // chart/effective displacement rate (== metric v_s)
    Vec3 origin;
    Vec3 destination;
    double dt = 1.0e-3;
    double L = 2.0;

    double d_total = 0.0;
    double t_total = 0.0;

    Vec3 position_at(double t) const;
    double proper_time_at(double t) const;
};

struct ConventionalPlan {
    Vec3 origin;
    Vec3 destination;
    double v = 0.0;
    double dt = 1.0e-3;

    double d = 0.0;
    double t = 0.0;
    double gamma = 0.0;

    Vec3 position_at(double tq) const;
    double proper_time_at(double tq) const;
};

// Plan builders (fail-closed; return false + reason on any guard trip).
bool build_traversal_plan(TraversalPlan& p, std::string& err);
bool build_warp_plan(WarpPlan& p, std::string& err);
bool build_conventional_plan(ConventionalPlan& p, std::string& err);

// ----------------------------- FSM ------------------------------------------

class TravelFSM {
public:
    TravelFSM() = default;
    bool init_wormhole(const TraversalPlan& p, std::string& err);
    bool init_warp(const WarpPlan& p, std::string& err);
    bool init_conventional(const ConventionalPlan& p, std::string& err);

    TravState state() const { return state_; }
    double coordinate_time_s() const { return t_; }
    TravelMechanism mechanism() const { return mech_; }
    bool is_terminal() const;

    TravState begin();
    TravState abort();
    TravState step();            // by plan dt
    TravState step(double dt);   // explicit

    Vec3 position() const;
    double proper_time_s() const;
    const std::string& invalid_reason() const { return invalid_reason_; }

private:
    TravState target_state() const;
    void walk_transition(TravState target);

    TravelMechanism mech_ = TravelMechanism::CONVENTIONAL_RELATIVISTIC;
    TravState state_ = TravState::IDLE;
    double t_ = 0.0;
    TraversalPlan wh_{};
    WarpPlan wp_{};
    ConventionalPlan cv_{};
    std::string invalid_reason_;
    bool aborted_ = false;
};

// Causality classification text (identical vocabulary to Python; REPORTED,
// never hidden). mechanism already validated.
std::string causal_status_wormhole(double external_d, double transit_t);
std::string causal_status_warp(double vs);
std::string causal_status_conventional(double proper_t, double coord_t);

// Vocabulary helpers for HUD/RenderState consumers.
const char* classification_geometry();     // THEORETICAL
const char* classification_traversal();    // SPECULATIVE
const char* classification_visual();       // CINEMATIC (where labeled)

}  // namespace v15
}  // namespace astra
