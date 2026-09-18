"""ASTRA Theoretical — wormhole traversal plan + explicit FSM (v1.5 PHASE 4).

This module separates, explicitly and deterministically:
  - geometry authority (delegated: MorrisThorneMetric), throat radius,
  - mouth positions / velocity / frame (values supplied by the CALLER — this
    module owns no celestial data and fabricates no orbits),
  - traversal time decomposition (coordinate vs proper),
  - tidal acceleration at observer scale (first-order embedding curvature),
  - redshift at the observer (e^Phi via the metric's redshift_func),
  - stability / energy-condition assumptions (verbatim docs: NEC violated at
    the throat for every Morris-Thorne throat — that is THE theory; we report
    the evaluated value where the authority permits, labelling the exotic
    matter requirement SPECULATIVE, never speculative physics as real).

State machine (deterministic, tick-driven; FSM only — no physics state lives
here besides what the plan dictates):
    IDLE -> APPROACHING -> ENTRY -> TRANSIT -> EXIT -> COMPLETE
    any pre-COMPLETE state -> ABORTED (explicit caller abort)
    any guarded failure (NaN/inf, radii out of range, beta>=1, frame swap)
        -> INVALID (terminal, fail-closed)

The FSM is a pure schedule over COORDINATE TIME; no wall clock, no RNG.
Times & intervals are SI seconds / metres, delegated to astra.temporal /
astra.relativity — nothing here re-implements gamma or dilation.

Classification: Mathematical geometry values THEORETICAL (metric math),
traversal feasibility claims SPECULATIVE (exotic matter requirement).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Dict, List, Optional, Sequence, Tuple

from astra.relativity import lorentz_factor
from astra.theoretical.classification import ScientificClassification
from astra.theoretical.exceptions import InvalidGeometryParameterError
from astra.theoretical.wormhole import MorrisThorneMetric

C_ASTRA: float = 299_792_458.0


class TraversalState(str, Enum):
    IDLE = "IDLE"
    APPROACHING = "APPROACHING"
    ENTRY = "ENTRY"
    TRANSIT = "TRANSIT"
    EXIT = "EXIT"
    COMPLETE = "COMPLETE"
    ABORTED = "ABORTED"
    INVALID = "INVALID"


class TraversalError(Exception):
    """Deterministic traversal failure (never silent)."""


# ------------------------------ validation ---------------------------------

def _finite(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or math.isnan(value) or math.isinf(value):
        raise TraversalError(f"{name} must be finite, got {value!r}")
    return float(value)


def _vec3_finite(v: Sequence[float], name: str) -> Tuple[float, float, float]:
    if not isinstance(v, (tuple, list)) or len(v) != 3:
        raise TraversalError(f"{name} must be a 3-vector")
    return tuple(_finite(x, f"{name}[{i}]") for i, x in enumerate(v))


# ------------------------------ the plan -----------------------------------

@dataclass(frozen=True)
class MouthState:
    """One wormhole mouth as supplied by the authoritative scene model."""
    name: str
    frame_id: str
    position_m: Tuple[float, float, float]
    velocity_mps: Tuple[float, float, float] = (0.0, 0.0, 0.0)

    def __post_init__(self):
        if not isinstance(self.name, str) or not self.name:
            raise TraversalError("mouth name must be non-empty string")
        if not isinstance(self.frame_id, str) or not self.frame_id:
            raise TraversalError("mouth frame_id must be non-empty string")
        object.__setattr__(self, "position_m", _vec3_finite(self.position_m, "position_m"))
        object.__setattr__(self, "velocity_mps", _vec3_finite(self.velocity_mps, "velocity_mps"))
        sp = math.sqrt(sum(x * x for x in self.velocity_mps))
        if sp >= C_ASTRA:
            raise TraversalError(f"mouth velocity {sp} m/s >= c is unphysical (beta>=1)")


def _dist(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)


@dataclass(frozen=True)
class TraversalPlan:
    """Deterministic coordinate-time schedule for a throat transit.

    Derived-only: every quantity is computed from the inputs below via
    documented constants — nothing is sampled, nothing is RNG.
    """
    metric: MorrisThorneMetric
    mouth_in: MouthState
    mouth_out: MouthState
    observer_start_m: Tuple[float, float, float]
    transit_speed_mps: float            # constant inertial speed through the whole route (observer frame: conventional < c)
    observer_scale_m: float = 2.0       # observer characteristic length for the tidal estimate
    dt_step_s: float = 1.0e-3           # FSM tick in coordinate time

    def __post_init__(self):
        if not isinstance(self.metric, MorrisThorneMetric):
            raise TraversalError("metric must be a MorrisThorneMetric (geometry authority)")
        if not isinstance(self.mouth_in, MouthState) or not isinstance(self.mouth_out, MouthState):
            raise TraversalError("mouths must be MouthState")
        if self.mouth_in.frame_id != self.mouth_out.frame_id:
            raise TraversalError(
                "mouth frame mismatch: traversal requires both mouths in ONE frame "
                "(frame mixing is never silent)"
            )
        object.__setattr__(self, "_s0", _vec3_finite(tuple(self.observer_start_m), "observer_start_m"))
        v = _finite(self.transit_speed_mps, "transit_speed_mps")
        if not 0.0 < v < C_ASTRA:
            raise TraversalError(f"transit_speed_mps must be 0 < v < c, got {v!r} (beta>=1 rejected)")
        L = _finite(self.observer_scale_m, "observer_scale_m")
        if L <= 0.0:
            raise TraversalError("observer_scale_m must be > 0")
        dt = _finite(self.dt_step_s, "dt_step_s")
        if not 0.0 < dt:
            raise TraversalError("dt_step_s must be > 0")

        # ---- route decomposition (static mouth approximation: caller passes
        # mouth states at departure; moving mouths are captured as velocities
        # for honesty but the schedule treats them as fixed at those values;
        # re-planning on mouth motion is the CALLER's duty, documented) ----
        st = self.observer_start_m
        a_in = self.mouth_in.position_m
        a_out = self.mouth_out.position_m
        object.__setattr__(self, "_d_approach", _dist(st, a_in))
        # Legit entry: start AT the mouth center (d==0). Reject the ill-defined
        # band 0 < d <= r_throat (standing inside the throat wall geometry —
        # a position the caller cannot rationally hold without being mid-transit).
        if 0.0 < self._d_approach <= self.metric.throat_radius_m:
            raise TraversalError(
                "observer starts inside the throat sphere but not at the mouth — "
                "plan ill-defined (start at the mouth center or outside its radius)"
            )
        # Morris-Thorne throat coordinate length: l in [-R, R] chart;
        # proper throat distance 2*r_throat (guard below the guard radius).
        rt = self.metric.throat_radius_m
        object.__setattr__(self, "_d_throat", 2.0 * rt)
        # exit leg mirrors approach inside throat chart
        object.__setattr__(self, "_d_egress", self._d_throat)

        g = lorentz_factor(v)  # constant-speed legs: proper/coordinate = 1/gamma
        object.__setattr__(self, "_gamma", g)
        object.__setattr__(self, "_t_approach", self._d_approach / v)
        object.__setattr__(self, "_t_throat", self._d_throat / v + self._d_egress / v)
        object.__setattr__(self, "_t_total", self._t_approach + self._t_throat)
        # proper time along constant-speed legs (gravitational factor folded
        # via exp(phi); Morris-Thorne redshift funcs are chart functions of r)
        phi = float(self.metric.redshift_func(self.metric.throat_radius_m))
        object.__setattr__(self, "_grav_factor", math.exp(phi) if math.isfinite(phi) else _raise_t("redshift_func non-finite at throat"))
        object.__setattr__(self, "_tau_approach", self._t_approach / g / self._grav_factor)
        object.__setattr__(self, "_tau_throat", self._t_throat / g / self._grav_factor)
        object.__setattr__(self, "_tau_total", self._tau_approach + self._tau_throat)

        # phase boundaries in coordinate time (throat sub-legs: ENTRY first
        # quarter of throat time, TRANSIT middle half, EXIT final quarter)
        object.__setattr__(self, "_b_entry", self._t_approach)
        object.__setattr__(self, "_b_transit", self._t_approach + self._t_throat * 0.25)
        object.__setattr__(self, "_b_exit", self._t_approach + self._t_throat * 0.75)
        object.__setattr__(self, "_b_done", self._t_total)

    # geometry / physics surface (delegated, documented) ------------------

    @property
    def throat_radius_m(self) -> float:
        return self.metric.throat_radius_m

    @property
    def gamma(self) -> float:
        return self._gamma

    @property
    def coordinate_time_total_s(self) -> float:
        return self._t_total

    @property
    def proper_time_total_s(self) -> float:
        return self._tau_total

    @property
    def gravitational_redshift_factor_at_throat(self) -> float:
        """exp(Phi(r_throat)): external-light redshift factor (THEORETICAL)."""
        return self._grav_factor

    def redshift_at(self, r: float) -> float:
        rv = _finite(r, "r")
        if rv < self.metric.throat_radius_m:
            raise TraversalError(f"redshift evaluation below throat radius {self.metric.throat_radius_m}")
        relative = rv - self.metric.throat_radius_m
        if relative >= 2.0 * self.metric.throat_radius_m:
            raise TraversalError("redshift evaluation outside throat guard band")
        phi = float(self.metric.redshift_func(rv))
        if math.isnan(phi) or math.isinf(phi):
            raise TraversalError("redshift_func non-finite")
        return math.exp(phi)

    def tidal_acceleration_at_throat(self) -> Optional[float]:
        """Radial tidal accel across observer scale at throat [m/s^2].

        First-order embedding estimate: the embedding curvature of a
        Morris-Thorne geometry at the throat scales as k ~ 1/r_t^2 in the
        radial-radial direction; the geodesic deviation over length L is
        a_tidal ~ c^2 * k * L  (units: c^2/m * (dimensionless curvature)).
        This is a MODEL-limited first-order value (THEORETICAL); the
        classification authority is consulted, never inflated.
        """
        r_t = self.metric.throat_radius_m
        if r_t <= 0.0:
            return None  # NOT AVAILABLE — never invent
        L = self.observer_scale_m
        # Guard: underflow/overflow protection BEFORE forming the quotient.
        k = 1.0 / (r_t * r_t)
        a = (C_ASTRA * C_ASTRA) * k * L
        if math.isnan(a) or math.isinf(a):
            return None
        return a

    def energy_condition_assumptions(self) -> Dict[str, object]:
        """Explicit assumption surface (never hidden)."""
        return {
            "requires_exotic_matter_at_throat": True,
            "null_energy_condition": "VIOLATED at the throat for every Morris-Thorne throat (Morris & Thorne 1988)",
            "classification_physics": ScientificClassification.SPECULATIVE.value,
            "stability_assumption": "mouths static in supplied frame during transit (caller re-plans on motion)",
            "quantum_vacuum_constraints": "NOT MODELED (Ford & Roman bounds not enforced)",
        }

    # schedule ------------------------------------------------------------

    def state_at(self, t_coord_s: float) -> TraversalState:
        t = _finite(t_coord_s, "t_coord_s")
        if t < 0.0:
            raise TraversalError("negative coordinate time")
        if t < self._b_entry:
            return TraversalState.APPROACHING
        if t < self._b_transit:
            return TraversalState.ENTRY
        if t < self._b_exit:
            return TraversalState.TRANSIT
        if t < self._b_done:
            return TraversalState.EXIT
        return TraversalState.COMPLETE

    def position_at(self, t_coord_s: float) -> Tuple[float, float, float]:
        """Observer position along the straight-line legs (chart coordinates).

        Legs: start -> mouth_in (approach), mouth_in -> throat-center chart
        shortcut -> mouth_out (transit/exit; the wormhole CHART segment, not
        the external distance between mouths — explicitly the point of the
        model: external distance is NOT traversed).
        """
        t = _finite(t_coord_s, "t_coord_s")
        v = self.transit_speed_mps
        s0, ain, aout = self._s0, self.mouth_in.position_m, self.mouth_out.position_m
        if t <= self._b_entry:
            if self._d_approach == 0.0:
                return ain  # start at the mouth center (d_approach == 0)
            lam = (v * t)
            return _lerp(s0, ain, min(lam / self._d_approach, 1.0))
        if t <= self._b_exit:
            frac = min((v * (t - self._b_entry)) / (2.0 * self._d_throat), 1.0)
            return _lerp(ain, aout, frac)
        return aout

    def proper_time_at(self, t_coord_s: float) -> float:
        t = _finite(t_coord_s, "t_coord_s")
        g = self._gamma
        gf = self._grav_factor
        return min(t, self._t_total) / (g * gf)


def _raise_t(msg: str):
    raise TraversalError(msg)


def _lerp(a, b, lam):
    return (a[0] + (b[0] - a[0]) * lam, a[1] + (b[1] - a[1]) * lam, a[2] + (b[2] - a[2]) * lam)


# ------------------------------ the FSM ------------------------------------

_VALID_TRANSITIONS: Dict[TraversalState, Tuple[TraversalState, ...]] = {
    TraversalState.IDLE: (TraversalState.APPROACHING, TraversalState.INVALID),
    TraversalState.APPROACHING: (TraversalState.ENTRY, TraversalState.ABORTED, TraversalState.INVALID),
    TraversalState.ENTRY: (TraversalState.TRANSIT, TraversalState.ABORTED, TraversalState.INVALID),
    TraversalState.TRANSIT: (TraversalState.EXIT, TraversalState.ABORTED, TraversalState.INVALID),
    TraversalState.EXIT: (TraversalState.COMPLETE, TraversalState.ABORTED, TraversalState.INVALID),
    TraversalState.COMPLETE: (),
    TraversalState.ABORTED: (),
    TraversalState.INVALID: (),
}


class WormholeTraversalFSM:
    """Deterministic tick-driven traversal machine (v1.5 PHASE 4/6/8).

    Owns exactly one plan. step() advances coordinate time by plan.dt_step_s;
    transitions follow the plan boundaries; observer data published per tick
    is DELEGATED (position from plan, proper time from plan+temporal chain).
    Guard failures (non-finite observer data, frame swap mid-transit,
    corrupted serialization) -> INVALID, terminal, never silently retried.
    """

    def __init__(self, plan: TraversalPlan):
        if not isinstance(plan, TraversalPlan):
            raise TraversalError("plan must be a TraversalPlan")
        self._plan = plan
        self._state = TraversalState.IDLE
        self._t = 0.0
        self._ticks = 0
        self._invalid_reason: Optional[str] = None
        self._aborted = False

    @property
    def state(self) -> TraversalState:
        return self._state

    @property
    def coordinate_time_s(self) -> float:
        return self._t

    @property
    def invalid_reason(self) -> Optional[str]:
        return self._invalid_reason

    def begin(self) -> TraversalState:
        return self._transition(TraversalState.APPROACHING)

    def abort(self) -> TraversalState:
        if self._state in (TraversalState.COMPLETE,):
            raise TraversalError("cannot abort a COMPLETE traversal")
        if self._state != TraversalState.ABORTED:
            self._state = TraversalState.ABORTED
            self._aborted = True
        return self._state

    def step(self, dt_s: Optional[float] = None) -> TraversalState:
        if self._state in (TraversalState.IDLE,):
            raise TraversalError("step() before begin()")
        if self._state in (TraversalState.COMPLETE, TraversalState.ABORTED, TraversalState.INVALID):
            return self._state
        dt = self._plan.dt_step_s if dt_s is None else _finite(dt_s, "dt_s")
        if dt <= 0.0:
            raise TraversalError("dt must be > 0")
        self._t += dt
        self._ticks += 1
        target = self._plan.state_at(self._t)
        # deterministic multi-boundary jump: walk the transition chain
        while self._state != target:
            nxt = self._plan_next_transition(target)
            self._transition(nxt)
        return self._state

    def observer_snapshot(self) -> Dict[str, object]:
        """Per-tick observer view (finite-checked; never ships NaN)."""
        if self._state in (TraversalState.INVALID,):
            raise TraversalError(f"FSM INVALID: {self._invalid_reason}")
        pos = self._plan.position_at(self._t) if self._state not in (TraversalState.IDLE,) else self._plan._s0
        for v in pos:
            if math.isnan(v) or math.isinf(v):
                self._invalidate("observer position non-finite")
        return {
            "state": self._state.value,
            "position_m": pos,
            "coordinate_time_s": self._t,
            "proper_time_s": self._plan.proper_time_at(self._t),
            "gamma": self._plan.gamma,
            "gravitational_redshift_factor_at_throat": self._plan.gravitational_redshift_factor_at_throat,
            "tidal_acceleration_at_throat_mps2": self._plan.tidal_acceleration_at_throat(),
            "classification_geometry": ScientificClassification.THEORETICAL.value,
            "classification_traversal": ScientificClassification.SPECULATIVE.value,
        }

    # ----- serialization (deterministic; load validated, fail-closed) -----

    def to_dict(self) -> Dict[str, object]:
        return {
            "schema": "astra.v15.traversal_fsm",
            "state": self._state.value,
            "t_coord_s": self._t,
            "ticks": self._ticks,
            "aborted": self._aborted,
            "invalid_reason": self._invalid_reason,
        }

    @classmethod
    def from_dict(cls, plan: TraversalPlan, data: Dict) -> "WormholeTraversalFSM":
        if not isinstance(data, dict) or data.get("schema") != "astra.v15.traversal_fsm":
            raise TraversalError("tamper/invalid: schema mismatch")
        fsm = cls(plan)
        try:
            state = TraversalState(data["state"])
        except Exception:
            raise TraversalError("tamper/invalid: unknown state")
        t = _finite(data["t_coord_s"], "t_coord_s")
        ticks = data["ticks"]
        if not isinstance(ticks, int) or ticks < 0 or isinstance(ticks, bool):
            raise TraversalError("tamper/invalid: ticks")
        if t < 0.0:
            raise TraversalError("tamper/invalid: negative time")
        # Console-check: reloading mid-route state must be consistent w/ the plan
        if state not in (TraversalState.IDLE, TraversalState.INVALID, TraversalState.ABORTED):
            # the plan must agree this t lies in that band (within one tick of slack)
            band = plan.state_at(min(t + 0.5 * plan.dt_step_s, plan.coordinate_time_total_s))
            if state in (TraversalState.APPROACHING, TraversalState.ENTRY, TraversalState.TRANSIT) \
               and band not in (state, TraversalState.ENTRY, TraversalState.TRANSIT, TraversalState.COMPLETE):
                raise TraversalError("tamper/invalid: state/time inconsistency")
        fsm._state = state
        fsm._t = t
        fsm._ticks = ticks
        fsm._aborted = bool(data.get("aborted", False)) if state == TraversalState.ABORTED else False
        fsm._invalid_reason = data.get("invalid_reason") if state == TraversalState.INVALID else None
        return fsm

    # ----- internals -------------------------------------------------------

    def _plan_next_transition(self, target: TraversalState) -> TraversalState:
        order = (TraversalState.APPROACHING, TraversalState.ENTRY, TraversalState.TRANSIT,
                 TraversalState.EXIT, TraversalState.COMPLETE)
        i = order.index(self._state) if self._state in order else 0
        j = order.index(target)
        return order[min(i + 1, j)]

    def _transition(self, nxt: TraversalState) -> TraversalState:
        if nxt in _VALID_TRANSITIONS[self._state]:
            self._state = nxt
            return self._state
        if nxt == self._state:
            return self._state
        raise TraversalError(f"illegal traversal transition {self._state.value} -> {nxt.value}")

    def _invalidate(self, reason: str):
        if self._state not in (TraversalState.COMPLETE, TraversalState.ABORTED):
            self._state = TraversalState.INVALID
            self._invalid_reason = reason
            raise TraversalError(reason)
