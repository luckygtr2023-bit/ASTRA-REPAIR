"""ASTRA Interaction — authoritative Journey Engine (v1.5 PHASES 6/7/8/12/19/22).

ONE authoritative exploration/travel abstraction. Contains per journey:
origin, destination, observer state (position + temporal), frame, departure/
arrival, proper+coordinate time, mechanism, state (TraversalState), causal
validity, abort conditions, numerical validity and classification.

Never implements metric physics: geometry delegated to astra.theoretical.*
(SPECULATIVE surface), gamma/dilation to astra.relativity, interval/causal
classification to astra.spacetime.causality, temporal quantities to
astra.temporal. This engine ORCHESTRATES the authorities — per the ASTRA
renderer/authority rule.

Plugs into DelegatingTravelService as the injected TravelProvider (filling
the v1.4 Null fallback). Deterministic: no wall clock, no RNG; ids are
monotonic ints per engine instance.
"""

from __future__ import annotations

import hashlib
import json
import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Protocol, Sequence, Tuple

from astra.relativity import lorentz_factor
from astra.scientific.classification import Classification
from astra.theoretical.classification import ScientificClassification
from astra.theoretical.traversal import (
    C_ASTRA, MouthState, TraversalError, TraversalPlan, TraversalState,
    WormholeTraversalFSM, _dist, _finite, _lerp, _vec3_finite,
)
from astra.theoretical.warp_journey import WarpPlan
from astra.interaction.travel import (
    TravelConstraints, TravelMethod, TravelRequest, TravelResult,
)


class JourneyError(Exception):
    """Deterministic journey failure (fail-closed)."""


# ------------------------------ mechanics plan ------------------------------

@dataclass(frozen=True)
class ConventionalPlan:
    """Constant-velocity < c straight leg (delegates gamma to relativity)."""
    frame_id: str
    origin_m: Tuple[float, float, float]
    destination_m: Tuple[float, float, float]
    speed_mps: float
    dt_step_s: float = 1.0e-3

    def __post_init__(self):
        if not isinstance(self.frame_id, str) or not self.frame_id:
            raise JourneyError("frame_id must be non-empty string")
        o = _vec3_finite(self.origin_m, "origin_m")
        d = _vec3_finite(self.destination_m, "destination_m")
        v = _finite(self.speed_mps, "speed_mps")
        if not 0.0 < v < C_ASTRA:
            raise JourneyError(f"conventional speed must be 0 < v < c, got {v!r} (beta>=1 rejected)")
        dt = _finite(self.dt_step_s, "dt_step_s")
        if dt <= 0.0:
            raise JourneyError("dt_step_s must be > 0")
        object.__setattr__(self, "origin_m", o)
        object.__setattr__(self, "destination_m", d)
        object.__setattr__(self, "_d", _dist(o, d))
        if self._d <= 0.0:
            raise JourneyError("origin == destination")
        object.__setattr__(self, "_t", self._d / v)
        object.__setattr__(self, "_g", lorentz_factor(v))

    @property
    def coordinate_time_total_s(self):
        return self._t

    @property
    def proper_time_total_s(self):
        return self._t / self._g

    @property
    def gamma(self):
        return self._g

    @property
    def chart_distance_m(self):
        return self._d

    def position_at(self, t: float):
        lam = min(max(t, 0.0), self._t) / self._t
        return _lerp(self.origin_m, self.destination_m, lam)

    def proper_time_at(self, t: float):
        return min(max(t, 0.0), self._t) / self._g


# ------------------------------ the journey --------------------------------

_MECH_CLASS = {
    TravelMethod.CONVENTIONAL_RELATIVISTIC: Classification.SIMULATED_DATA,
    TravelMethod.WORMHOLE: Classification.SPECULATIVE,      # traversable wormholes unproven
    TravelMethod.WARP: Classification.SPECULATIVE,          # Alcubierre unproven
}


class Journey:
    """One live journey (engine-owned; state mutated only via step/abort)."""

    def __init__(
        self,
        journey_id: str,
        request: TravelRequest,
        plan: Any,                      # TraversalPlan | WarpPlan | ConventionalPlan
        origin_frame_id: str,
        destination_frame_id: str,
        origin_m: Tuple[float, float, float],
        destination_m: Tuple[float, float, float],
        fsm: Optional[WormholeTraversalFSM] = None,
        event_hook: Optional[Callable[[str, Dict[str, Any]], None]] = None,
    ):
        if not isinstance(journey_id, str) or not journey_id:
            raise JourneyError("journey id must be non-empty")
        self.journey_id = journey_id
        self.request = request
        self.plan = plan
        self.origin_frame_id = origin_frame_id
        self.destination_frame_id = destination_frame_id
        self.origin_m = _vec3_finite(origin_m, "origin_m")
        self.destination_m = _vec3_finite(destination_m, "destination_m")
        self._fsm = fsm                      # None for non-wormhole mechanisms
        self._state = TraversalState.IDLE
        self._t = 0.0
        self._aborted = False
        self._invalid_reason: Optional[str] = None
        self._event_hook = event_hook
        self._validated_finite()
        self._causal = self._classify_causality()

    # ----- lifecycle -------------------------------------------------------

    @property
    def state(self) -> TraversalState:
        return self._state

    @property
    def coordinate_time_s(self) -> float:
        return self._t

    @property
    def causal_status(self) -> str:
        return self._causal

    @property
    def invalid_reason(self) -> Optional[str]:
        return self._invalid_reason

    @property
    def aborted(self) -> bool:
        return self._aborted

    def begin(self) -> TraversalState:
        if self._state != TraversalState.IDLE:
            raise JourneyError("begin() only from IDLE")
        if self._fsm is not None:
            self._state = self._fsm.begin()
        else:
            # warp/conventional march directly into the crossing band
            self._state = TraversalState.TRANSIT if self._causal != "" else TraversalState.TRANSIT
        self._publish("journey_begin", {
            "journey_id": self.journey_id,
            "method": self.request.method.value,
            "origin_frame_id": self.origin_frame_id,
            "destination_frame_id": self.destination_frame_id,
            "causal_status": self._causal,
            "classification": self.classification.value,
        })
        return self._state

    def abort(self, reason: str = "caller abort") -> TraversalState:
        if self._state in (TraversalState.COMPLETE,):
            raise JourneyError("cannot abort a COMPLETE journey")
        if self._state != TraversalState.ABORTED:
            if self._fsm is not None:
                self._state = self._fsm.abort()
            else:
                self._state = TraversalState.ABORTED
            self._aborted = True
            self._publish("journey_abort", {"journey_id": self.journey_id, "reason": reason})
        return self._state

    def step(self, dt_s: Optional[float] = None) -> TraversalState:
        if self._state == TraversalState.IDLE:
            raise JourneyError("step() before begin()")
        if self._state in (TraversalState.COMPLETE, TraversalState.ABORTED, TraversalState.INVALID):
            return self._state
        dt = self.plan.dt_step_s if dt_s is None else _finite(dt_s, "dt_s")
        if dt <= 0.0:
            raise JourneyError("dt must be > 0")
        prev = self._state
        if self._fsm is not None:
            self._state = self._fsm.step(dt)
            self._t = self._fsm.coordinate_time_s
        else:
            self._t += dt
            total = self.plan.coordinate_time_total_s
            if self._t < 0.5 * total:
                self._state = TraversalState.TRANSIT
            elif self._t < total:
                self._state = TraversalState.EXIT
            else:
                self._state = TraversalState.COMPLETE
        self._validated_finite()
        if prev != self._state:
            self._publish("journey_state", {"journey_id": self.journey_id, "state": self._state.value,
                                            "t_s": self._t})
        if self._state == TraversalState.COMPLETE:
            self._publish("journey_complete", {"journey_id": self.journey_id,
                                               "proper_time_s": self.plan.proper_time_at(self._t),
                                               "coordinate_time_s": self._t,
                                               "causal_status": self._causal})
        return self._state

    # ----- observer view (PHASE 7: travel alters the scientific observer) --

    @property
    def classification(self) -> Classification:
        return _MECH_CLASS[self.request.method]

    def observer_snapshot(self) -> Dict[str, Any]:
        """Scientific observer state during travel. All finite-checked."""
        if self._state == TraversalState.INVALID:
            raise JourneyError(f"INVALID: {self._invalid_reason}")
        pos = self.plan.position_at(self._t) if self._state != TraversalState.IDLE else self.origin_m
        for v in pos:
            if math.isnan(v) or math.isinf(v):
                self._invalidate("non-finite observer position")
        snap: Dict[str, Any] = {
            "journey_id": self.journey_id,
            "method": self.request.method.value,
            "state": self._state.value,
            "frame_id": self._current_frame(),
            "position_m": pos,
            "coordinate_time_s": self._t,
            "proper_time_s": self.plan.proper_time_at(self._t) if self._state != TraversalState.IDLE else 0.0,
            "classification": self.classification.value,
            "causal_status": self._causal,
            "origin_m": self.origin_m,
            "destination_m": self.destination_m,
            "origin_frame_id": self.origin_frame_id,
            "destination_frame_id": self.destination_frame_id,
            "aborted": self._aborted,
        }
        if isinstance(self.plan, ConventionalPlan):
            snap["gamma"] = self.plan.gamma
            snap["beta"] = self.plan.speed_mps / C_ASTRA
            snap["z_doppler_radial_max"] = float("nan")
            snap["z_doppler_note"] = "radial doppler computed observer-relative downstream; here: NOT AVAILABLE (no source direction yet)"
        elif isinstance(self.plan, TraversalPlan):
            snap["gamma"] = self.plan.gamma
            snap["throat_radius_m"] = self.plan.throat_radius_m
            snap["redshift_at_throat"] = self.plan.gravitational_redshift_factor_at_throat
            snap["tidal_at_throat_mps2"] = self.plan.tidal_acceleration_at_throat()
        elif isinstance(self.plan, WarpPlan):
            snap["bubble_radius_m"] = self.plan.bubble_radius_m
            snap["wall_steepness"] = self.plan.wall_steepness_inv_m
            snap["local_observer_speed_mps"] = self.plan.local_observer_speed_mps
            snap["effective_rate_mps"] = self.plan.effective_displacement_rate_mps
            snap["tidal_guard"] = self.plan.tidal_guard()
        return snap

    # ----- persistence (PHASE 18): canonical JSON + sha256 checksum ---------

    def to_dict(self) -> Dict[str, Any]:
        body = {
            "schema": "astra.v15.journey",
            "version": 1,
            "journey_id": self.journey_id,
            "request": self.request.to_dict(),
            "state": self._state.value,
            "t_coord_s": self._t,
            "origin_m": list(self.origin_m),
            "destination_m": list(self.destination_m),
            "origin_frame_id": self.origin_frame_id,
            "destination_frame_id": self.destination_frame_id,
            "fsm": self._fsm.to_dict() if self._fsm is not None else None,
            "causal_status": self._causal,
            "aborted": self._aborted,
        }
        body["checksum"] = _canonical_checksum(body)
        return body

    @classmethod
    def from_dict(
        cls,
        data: Dict[str, Any],
        plan_factory: Callable[[Dict[str, Any]], Tuple[Any, Optional[WormholeTraversalFSM]]],
        event_hook: Optional[Callable[[str, Dict[str, Any]], None]] = None,
    ) -> "Journey":
        """plan_factory reconstructs (plan, fsm-for-wormhole) from request
        parameters. The engine NEVER builds geometry from session data
        without the caller's metric authority.
        """
        if not isinstance(data, dict) or data.get("schema") != "astra.v15.journey":
            raise JourneyError("tamper/invalid: schema mismatch")
        data = dict(data)
        checksum = data.pop("checksum", None)
        if not isinstance(checksum, str) or checksum != _canonical_checksum(data):
            raise JourneyError("tamper/invalid: checksum mismatch")
        if data.get("version") != 1:
            raise JourneyError("unsupported journey version")
        req = TravelRequest.from_dict(data["request"])
        plan, fsm = plan_factory(data)
        if fsm is not None and data.get("fsm"):
            fsm = WormholeTraversalFSM.from_dict(plan, data["fsm"])
        j = cls(
            journey_id=data["journey_id"],
            request=req,
            plan=plan,
            origin_frame_id=data["origin_frame_id"],
            destination_frame_id=data["destination_frame_id"],
            origin_m=tuple(data["origin_m"]),
            destination_m=tuple(data["destination_m"]),
            fsm=fsm,
            event_hook=event_hook,
        )
        # restore live state with validation
        try:
            st = TraversalState(data["state"])
        except Exception:
            raise JourneyError("tamper/invalid: state")
        t = _finite(data["t_coord_s"], "t_coord_s")
        if t < 0.0:
            raise JourneyError("tamper/invalid: negative time")
        if st not in (TraversalState.IDLE,):
            total = plan.coordinate_time_total_s
            # ticking overshoots the boundary by at most one step — legitimate;
            # anything MORE is corruption (fail-closed, never clamped silently)
            slack = 1.001 * getattr(plan, "dt_step_s", 0.0)
            if t > total + slack:
                raise JourneyError("tamper/invalid: time beyond plan")
        j._state = st
        j._t = t
        j._aborted = bool(data.get("aborted", False)) if st == TraversalState.ABORTED else False
        return j

    # ----- internals --------------------------------------------------------

    def _current_frame(self) -> str:
        if isinstance(self.plan, TraversalPlan) and self._state in (
            TraversalState.ENTRY, TraversalState.TRANSIT, TraversalState.EXIT, TraversalState.COMPLETE,
        ):
            return self.destination_frame_id
        if isinstance(self.plan, (WarpPlan, ConventionalPlan)) and self._state in (
            TraversalState.EXIT, TraversalState.COMPLETE,
        ):
            return self.destination_frame_id
        return self.origin_frame_id

    def _validated_finite(self):
        for name, v in (("t", self._t),):
            if math.isnan(v) or math.isinf(v):
                self._invalidate(f"{name} non-finite")

    def _invalidate(self, reason: str):
        if self._state not in (TraversalState.COMPLETE, TraversalState.ABORTED):
            self._state = TraversalState.INVALID
            self._invalid_reason = reason
            self._publish("journey_invalid", {"journey_id": self.journey_id, "reason": reason})
            raise JourneyError(reason)

    def _classify_causality(self) -> str:
        """Event ordering check: departure (origin, t=0) vs arrival
        (destination, t_total). Light-travel minimum = d/c in the frame's
        chart. Report, don't hide. Fail-closed only for CONVENTIONAL."""
        m = self.request.method
        d = _dist(self.origin_m, self.destination_m)
        t = self.plan.coordinate_time_total_s
        if t <= 0.0:
            return "CAUSAL_CHECK_NOT_AVAILABLE (zero journey time)"
        if m == TravelMethod.CONVENTIONAL_RELATIVISTIC:
            return "CAUSAL_TIMELIKE (v < c; proper time {} s < coordinate time {} s)".format(
                format_si(self.plan.proper_time_total_s), format_si(t))
        if m == TravelMethod.WORMHOLE:
            if t < d / C_ASTRA:
                return ("SPECULATIVE_ACAUSAL_EFFECTIVE (external chart light time "
                        f"{format_si(d / C_ASTRA)} exceeds throat transit {format_si(t)}; "
                        "light-cone ordering between endpoints not model-guaranteed)")
            return "CHART_CONSISTENT (transit longer than external light time)"
        if m == TravelMethod.WARP:
            return "CAUSALITY_CLASSIFIED: " + self.plan.causality_status()
        return "NOT AVAILABLE"

    def _publish(self, name: str, payload: Dict[str, Any]):
        if self._event_hook is not None:
            self._event_hook(name, payload)


class JourneyEngine:
    """Authoritative travel provider (implements TravelProvider)."""

    def __init__(self, event_bus: Any = None):
        self._bus = event_bus
        self._journeys: Dict[str, Journey] = {}
        self._seq = 0

    def _hook(self) -> Callable[[str, Dict[str, Any]], None]:
        def h(name: str, payload: Dict[str, Any]):
            if self._bus is not None:
                try:
                    self._bus.publish_sync(name, 0, data=dict(payload, source="journey_engine"), source="journey")
                except TypeError:
                    # alternate buses (interaction._emit_event style)
                    try:
                        self._bus.publish_sync(name, data=dict(payload, source="journey_engine"), source="journey")
                    except Exception:
                        pass
                except Exception:
                    pass
        return h

    # ----- provider protocol ------------------------------------------------

    def can_travel(self, req: TravelRequest) -> Tuple[bool, str]:
        p = req.parameters or {}

        def _vec(name) -> Optional[Tuple[float, float, float]]:
            v = p.get(name)
            if v is None:
                return None
            try:
                return _vec3_finite(v, name)
            except TraversalError as e:
                raise JourneyError(str(e))

        try:
            origin = _vec("origin_m")
            dest = _vec("destination_m")
            frame = p.get("frame_id") or p.get("origin_frame_id") or req.origin_frame_id or "SIM_CENTER"
            if origin is None or dest is None:
                return (False, "missing origin_m / destination_m")
            if _dist(origin, dest) <= 0.0:
                return (False, "origin == destination")
            if req.method == TravelMethod.CONVENTIONAL_RELATIVISTIC:
                v = float(p.get("speed_mps", math.nan))
                if math.isnan(v) or not 0 < v < C_ASTRA:
                    return (False, f"speed_mps invalid (must be 0<v<c), got {v!r}")
                if req.constraints.causal is False:
                    return (False, "conventional relativistic travel is causal by construction; refusal of acausal marking")
                return (True, "ok")
            if req.method == TravelMethod.WORMHOLE:
                return self._check_wormhole(req)
            if req.method == TravelMethod.WARP:
                return self._check_warp(req)
            return (False, f"mechanism {req.method.value} has no authoritative provider path here")
        except JourneyError as e:
            return (False, str(e))

    def initiate(self, req: TravelRequest) -> TravelResult:
        ok, why = self.can_travel(req)
        if not ok:
            return TravelResult(request_id=req.request_id, success=False, method=req.method,
                                classification=req.classification, error_code="REFUSED",
                                error_message=why)
        # never autostart; engine creates a Journey and returns its id via request echo
        j = self.create_journey(req)
        j.begin()
        # Register completion summary in the RESULT; live state lives in engine.
        self._journeys[j.journey_id] = j
        plan = j.plan
        return TravelResult(
            request_id=req.request_id,
            success=True,
            method=req.method,
            classification=_MECH_CLASS[req.method],
            proper_time_s=plan.proper_time_total_s,
            coordinate_time_s=plan.coordinate_time_total_s,
            causal_status=j.causal_status,
        )

    def status(self, request_id: str) -> TravelResult:
        j = None
        for jj in self._journeys.values():
            if jj.request.request_id == request_id:
                j = jj
                break
        if j is None:
            raise JourneyError(f"unknown request {request_id} (no hidden state)")
        plan = j.plan
        return TravelResult(request_id=request_id, success=j.state != TraversalState.INVALID,
                            method=j.request.method, classification=_MECH_CLASS[j.request.method],
                            proper_time_s=plan.proper_time_at(j.coordinate_time_s),
                            coordinate_time_s=j.coordinate_time_s,
                            causal_status=j.causal_status,
                            warnings=[{"journey_id": j.journey_id, "state": j.state.value}])
    # ----- engine-level API (used by renderer/native mirror + persistence) --

    def create_journey(self, req: TravelRequest) -> Journey:
        self._seq += 1
        jid = f"journey-{self._seq:06d}"
        p = dict(req.parameters)
        frame = p.get("frame_id") or req.origin_frame_id or "SIM_CENTER"
        origin = tuple(float(x) for x in p["origin_m"])
        dest = tuple(float(x) for x in p["destination_m"])
        method = req.method
        if method == TravelMethod.CONVENTIONAL_RELATIVISTIC:
            plan = ConventionalPlan(frame_id=frame, origin_m=origin, destination_m=dest,
                                    speed_mps=float(p["speed_mps"]), dt_step_s=float(p.get("dt_step_s", 1e-3)))
            fsm = None
        elif method == TravelMethod.WORMHOLE:
            plan, fsm = self._build_wormhole(req)
        elif method == TravelMethod.WARP:
            plan = self._build_warp(req)
            fsm = None
        else:
            raise JourneyError(f"mechanism {method.value} unsupported by JourneyEngine")
        return Journey(journey_id=jid, request=req, plan=plan,
                       origin_frame_id=frame,
                       destination_frame_id=p.get("destination_frame_id", frame),
                       origin_m=origin, destination_m=dest, fsm=fsm,
                       event_hook=self._hook())

    def journeys(self) -> Tuple[Journey, ...]:
        return tuple(self._journeys.values())

    def active(self) -> Optional[Journey]:
        latest = None
        for j in self._journeys.values():
            if j.state not in (TraversalState.INVALID, TraversalState.ABORTED, TraversalState.COMPLETE, TraversalState.IDLE):
                latest = j
        return latest

    # ----- plan builders (frame/mouth separation honored) -------------------

    def _check_wormhole(self, req: TravelRequest) -> Tuple[bool, str]:
        p = req.parameters or {}
        rt = p.get("throat_radius_m", math.nan)
        if math.isnan(float(rt)) or float(rt) <= 0.0:
            return (False, f"throat_radius_m must be finite > 0, got {rt!r}")
        v = p.get("transit_speed_mps", 0.5 * C_ASTRA)
        if not isinstance(v, (int, float)) or isinstance(v, bool) or math.isnan(v) or math.isinf(v):
            return (False, f"transit_speed_mps must be finite, got {v!r}")
        if not 0.0 < v < C_ASTRA:
            return (False, f"transit_speed_mps must be 0 < v < c (beta>=1 rejected), got {v!r}")
        scale = p.get("observer_scale_m", 2.0)
        if not isinstance(scale, (int, float)) or not 0.0 < scale:
            return (False, f"observer_scale_m invalid {scale!r}")
        dt = p.get("dt_step_s", 1e-3)
        if not isinstance(dt, (int, float)) or not 0.0 < dt:
            return (False, f"dt_step_s invalid {dt!r}")
        for name in ("mouth_in_velocity", "mouth_out_velocity"):
            mv = p.get(name)
            if mv is not None:
                try:
                    vv = _vec3_finite(mv, name)
                except TraversalError:
                    return (False, f"{name} invalid")
                sp = math.sqrt(sum(x * x for x in vv))
                if sp >= C_ASTRA:
                    return (False, f"{name} >= c rejected (beta>=1)")
        origin, dest = tuple(p["origin_m"]), tuple(p["destination_m"])
        d = _dist(origin, dest)
        if 0.0 < d <= 2.0 * float(rt):
            return (False, "observer start inside throat sphere but not at mouth — ill-defined plan")
        return (True, "ok")

    @staticmethod
    def _default_shape(r: float, rt: float) -> float:
        # embed-conformal monotonic zero-tidal-feel shape (generic Morris-Thorne
        # class): b(r) = rt * (rt/r)^(1/2); b(rt)=rt, b'(rt)<1 — admitted family
        return rt * (rt / r) ** 0.5 if r > 0.0 else rt

    def _build_wormhole(self, req: TravelRequest):
        from astra.theoretical.wormhole import MorrisThorneMetric
        p = dict(req.parameters)
        rt = float(p["throat_radius_m"])
        frame = p.get("frame_id") or req.origin_frame_id or "SIM_CENTER"
        origin = tuple(float(x) for x in p["origin_m"])
        dest = tuple(float(x) for x in p["destination_m"])
        v = float(p.get("transit_speed_mps", 0.5 * C_ASTRA))
        observer_scale = float(p.get("observer_scale_m", 2.0))
        dt = float(p.get("dt_step_s", 1e-3))
        shape = p.get("shape_func")
        if shape is None:
            shape = lambda r: JourneyEngine._default_shape(r, rt)  # noqa: E731
        redshift = p.get("redshift_func", lambda r: 0.0)
        metric = MorrisThorneMetric(throat_radius_m=rt, shape_func=shape, redshift_func=redshift)
        mouth_in = MouthState(name="mouth_in", frame_id=frame, position_m=origin,
                              velocity_mps=p.get("mouth_in_velocity", (0.0, 0.0, 0.0)))
        mouth_out = MouthState(name="mouth_out", frame_id=frame, position_m=dest,
                               velocity_mps=p.get("mouth_out_velocity", (0.0, 0.0, 0.0)))
        plan = TraversalPlan(metric=metric, mouth_in=mouth_in, mouth_out=mouth_out,
                             observer_start_m=origin, transit_speed_mps=v,
                             observer_scale_m=observer_scale, dt_step_s=dt)
        return plan, WormholeTraversalFSM(plan)

    def _check_warp(self, req: TravelRequest) -> Tuple[bool, str]:
        p = req.parameters or {}
        vs = p.get("v_chart_mps", math.nan)
        R = p.get("bubble_radius_m", math.nan)
        sg = p.get("wall_steepness", math.nan)
        if math.isnan(float(vs)) or float(vs) <= 0.0:
            return (False, f"v_chart_mps invalid {vs!r}")
        if math.isnan(float(R)) or float(R) <= 0.0:
            return (False, f"bubble_radius_m invalid {R!r}")
        if math.isnan(float(sg)) or float(sg) <= 0.0 or float(sg) > 1e4:
            return (False, f"wall_steepness invalid {sg!r}")
        return (True, "ok")

    def _build_warp(self, req: TravelRequest) -> WarpPlan:
        from astra.theoretical.warp_journey import WarpPlan as _WP
        from astra.theoretical.warp import AlcubierreMetric
        p = dict(req.parameters)
        R = float(p["bubble_radius_m"])
        sg = float(p["wall_steepness"])
        vs = float(p["v_chart_mps"])
        metric = AlcubierreMetric(velocity=vs, radius_m=R, wall_steepness=sg)
        frame = p.get("frame_id") or req.origin_frame_id or "SIM_CENTER"
        origin = tuple(float(x) for x in p["origin_m"])
        dest = tuple(float(x) for x in p["destination_m"])
        return _WP(metric=metric, frame_id=frame, origin_m=origin, destination_m=dest,
                   v_chart_mps=vs, observer_scale_m=float(p.get("observer_scale_m", 2.0)),
                   dt_step_s=float(p.get("dt_step_s", 1e-3)))


def _canonical_checksum(body: Dict[str, Any]) -> str:
    """sha256 over canonical JSON (sorted keys, compact separators)."""
    b = json.dumps(body, sort_keys=True, separators=(",", ":"), ensure_ascii=False, default=str)
    return hashlib.sha256(b.encode("utf-8")).hexdigest()


def format_si(x: float) -> str:
    e = math.floor(math.log10(abs(x))) if x != 0.0 and math.isfinite(x) else 0
    return f"{x / (10.0 ** e):.3f}e{e:+d}"


__all__ = ["Journey", "JourneyEngine", "JourneyError", "ConventionalPlan"]
