"""ASTRA Theoretical — Alcubierre warp-journey plan (v1.5 PHASE 5).

Explicit separation, honored hard:
  - BUBBLE: center (chart), radius R, wall steepness sigma  (geometry)
  - OBSERVER: starts at the bubble center, co-moving INTERIOR
  - METRIC PARAMETERS: v_s is the COORDINATE/CHART parameter of the metric
    deformation (NOT a local massive-particle velocity). Local light cones
    stay causal — local physical motion of the observer is inertial rest at
    the bubble center, |v_local| = 0 < c for every vs (the geometry moves).
  - EFFECTIVE DISPLACEMENT: the chart displacement the geometry produces,
    rate = v_s * f_observer(=1 at center)  == v_s. This is NOT local motion.
  - Proper/coordinate time: the Alcubierre lapse is 1; the co-moving ruler
    at the bubble center accumulates proper time == coordinate time OF THE
    AMBIENT CHART (documented identity for this metric family).
  - Tidal constraint: observer scale L must sit in the flat interior — guard
    wall-confinement criterion L*sigma << 1 and L < R/2 (first-order;
    DOCUMENTED MODEL-LIMITED, not a full Einstein-tensor check).
  - Energy-density assumptions: reported via the metric's known analytic
    rho(r_s) = -(1/8pi) * (vphi f'/ ...) — we expose the MODEL's documented
    negative-energy-density requirement as an ASSUMPTION SURFACE, never as
    feasibility (SPECULATIVE).
  - Causality classification: effective displacement >= c is classified
    SPECULATIVE_ACAUSAL (no local cone violation, but globally dispreferred
    for causal ordering; reported, not hidden).

This module owns no celestial data; origin/destination are caller-supplied
chart positions in ONE frame. No warp drive is claimed to exist.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

from astra.theoretical.classification import ScientificClassification
from astra.theoretical.exceptions import InvalidGeometryParameterError
from astra.theoretical.traversal import C_ASTRA, TraversalError, _finite, _vec3_finite, _dist, _lerp
from astra.theoretical.warp import AlcubierreMetric


@dataclass(frozen=True)
class WarpPlan:
    """Deterministic warp-displacement schedule (caller-owned legs)."""
    metric: AlcubierreMetric            # geometry authority (SPECULATIVE)
    frame_id: str
    origin_m: Tuple[float, float, float]
    destination_m: Tuple[float, float, float]
    v_chart_mps: float                  # effective chart displacement rate (= metric shift v_s at center)
    observer_scale_m: float = 2.0
    dt_step_s: float = 1.0e-3

    def __post_init__(self):
        if not isinstance(self.metric, AlcubierreMetric):
            raise TraversalError("metric must be an AlcubierreMetric (geometry authority)")
        if not isinstance(self.frame_id, str) or not self.frame_id:
            raise TraversalError("frame_id must be non-empty string")
        o = _vec3_finite(tuple(self.origin_m), "origin_m")
        d = _vec3_finite(tuple(self.destination_m), "destination_m")
        vs = _finite(self.v_chart_mps, "v_chart_mps")
        # chart parameter may exceed c (the metric alone is smooth); but we
        # flag the classification in causality_status instead of rejecting —
        # while STILL rejecting non-finite / zero / negative rates.
        if vs <= 0.0:
            raise TraversalError("v_chart_mps must be > 0")
        if vs > 1.0e4 * C_ASTRA:
            raise TraversalError("v_chart_mps > 1e4 c rejected: model-limit numerical guard")
        L = _finite(self.observer_scale_m, "observer_scale_m")
        if L <= 0.0:
            raise TraversalError("observer_scale_m must be > 0")
        dt = _finite(self.dt_step_s, "dt_step_s")
        if dt <= 0.0:
            raise TraversalError("dt_step_s must be > 0")
        # Departmental consistency: the METRIC parameter v_s and v_chart must match
        # (no silent divergence; the bubble geometry IS the translator)
        if abs(self.metric.velocity - vs) > 1e-9 * max(1.0, abs(vs)):
            raise TraversalError(
                "metric shift parameter and journey chart-rate disagree — refusal (no silent divergence)"
            )
        object.__setattr__(self, "_origin", o)
        object.__setattr__(self, "_dest", d)
        object.__setattr__(self, "_d_total", _dist(o, d))
        if self._d_total <= 0.0:
            raise TraversalError("origin == destination: no journey")
        if self._d_total < 2.0 * self.metric.radius_m:
            raise InvalidGeometryParameterError(
                "bubble radius exceeds half the chart leg — journey ill-posed in this authority"
            )
        object.__setattr__(self, "_t_total", self._d_total / vs)
        if math.isnan(self._t_total) or math.isinf(self._t_total):
            raise TraversalError("journey time non-finite")

    # ---------- separated surfaces (each documented, each honest) ----------

    @property
    def bubble_center_m(self) -> Tuple[float, float, float]:
        return self._origin

    @property
    def bubble_radius_m(self) -> float:
        return self.metric.radius_m

    @property
    def wall_steepness_inv_m(self) -> float:
        return self.metric.wall_steepness

    @property
    def local_observer_speed_mps(self) -> float:
        """Physical local motion of the observer: REST at bubble center = 0.

        LOCAL MOTION is never v_s. This is the scientific boundary of the
        Alcubierre geometry and it is written down here, not implied.
        """
        return 0.0

    @property
    def effective_displacement_rate_mps(self) -> float:
        return self.v_chart_mps

    @property
    def chart_distance_m(self) -> float:
        return self._d_total

    @property
    def coordinate_time_total_s(self) -> float:
        return self._t_total

    @property
    def proper_time_total_s(self) -> float:
        """Ambient-chart lapse is unity at the bubble center (f=1):
        proper time == coordinate time for the co-moving observer."""
        return self._t_total

    def causality_status(self) -> str:
        if self.v_chart_mps >= C_ASTRA:
            return "SPECULATIVE_ACAUSAL_EFFECTIVE_DISPLACEMENT (no local light-cone violation; global ordering not guaranteed by the model)"
        return "CAUSAL_CHART (sub-luminal effective displacement; local light cones exact)"

    def tidal_guard(self) -> Dict[str, object]:
        """First-order wall-confinement constraint (MODEL-LIMITED, reported)."""
        sigma = self.metric.wall_steepness
        R = self.metric.radius_m
        L = self.observer_scale_m
        # wall width ~ 1/sigma; require the observer far inside the flat core:
        safe = (L * sigma < 0.01) and (L < 0.5 * R)
        return {
            "criterion": "observer remains in the flat interior (L*sigma << 1, L < R/2)",
            "observer_scale_m": L,
            "wall_width_m": 1.0 / sigma,
            "satisfied": safe,
            "classification": "THEORETICAL (model-limited estimate)",
        }

    def energy_assumptions(self) -> Dict[str, object]:
        return {
            "negative_energy_density_in_wall": True,
            "source": "Alcubierre 1994; rho wall analytic expression documented in astra.theoretical.warp",
            "classification_phys": ScientificClassification.SPECULATIVE.value,
            "feasibility": "NOT CLAIMED (exotic matter unproven; never presented as reality)",
            "quantum_constraints": "NOT MODELED",
        }

    def position_at(self, t_s: float) -> Tuple[float, float, float]:
        t = _finite(t_s, "t_s")
        if t < 0.0:
            raise TraversalError("negative time")
        lam = min(self._t_total, t) / self._t_total
        return _lerp(self._origin, self._dest, lam)

    def proper_time_at(self, t_coord_s: float) -> float:
        t = _finite(t_coord_s, "t_coord_s")
        return min(t, self._t_total)


__all__ = ["WarpPlan", "TraversalError"]
