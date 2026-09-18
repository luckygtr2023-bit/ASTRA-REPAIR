"""ASTRA v1.5 — EXTREME SPACETIME + EXPLORATION contract tests (PHASES 4-9, 18-19, 22-23).

Load-bearing: traversal FSM, warp separation, journey authority, observer,
causality, persistence+tamper, determinism, adversarial. No fabricated data;
every classification is checked, never silenced.
"""

from __future__ import annotations

import json
import math

import pytest

from astra.scientific.classification import Classification
from astra.theoretical.classification import ScientificClassification
from astra.theoretical.exceptions import InvalidGeometryParameterError
from astra.theoretical.traversal import (
    C_ASTRA, MouthState, TraversalError, TraversalPlan, TraversalState,
    WormholeTraversalFSM,
)
from astra.theoretical.warp import AlcubierreMetric
from astra.theoretical.warp_journey import WarpPlan
from astra.theoretical.wormhole import MorrisThorneMetric
from astra.interaction.journey import Journey, JourneyEngine, JourneyError
from astra.interaction.travel import (
    NullTravelProvider, TravelConstraints, TravelMethod, TravelError, TravelRequest,
)

C = 299792458.0
SIM = "SIM_FRAME"

# ----------------------------- fixtures -------------------------------------


def mt_metric(rt=1.0e3):
    return MorrisThorneMetric(throat_radius_m=rt, shape_func=lambda r: rt * (rt / r) ** 0.5 if r > 0.0 else rt)


def plan(rt=1.0e3, v=0.8 * C, d=3.0e6, dt=5.0e-7):
    return TraversalPlan(
        metric=mt_metric(rt),
        mouth_in=MouthState(name="A", frame_id=SIM, position_m=(0, 0, 0)),
        mouth_out=MouthState(name="B", frame_id=SIM, position_m=(d, 0, 0)),
        observer_start_m=(0, 0, 0),
        transit_speed_mps=v,
        dt_step_s=dt,
    )


def wh_request(d=3.0e6, rt=1.0e3, v=0.8 * C, dt=5.0e-7, **extra):
    p = {"origin_m": [0, 0, 0], "destination_m": [d, 0, 0], "throat_radius_m": rt,
         "transit_speed_mps": v, "frame_id": SIM, "dt_step_s": dt}
    p.update(extra)
    return TravelRequest(request_id="wh", method=TravelMethod.WORMHOLE, target_id="dest",
                         classification=Classification.SPECULATIVE,
                         constraints=TravelConstraints(), parameters=p)


def warp_request(vs=3.0 * C, R=100.0, sg=5.0, d=3.0e6, **extra):
    p = {"origin_m": [0, 0, 0], "destination_m": [d, 0, 0], "v_chart_mps": vs,
         "bubble_radius_m": R, "wall_steepness": sg, "frame_id": SIM}
    p.update(extra)
    return TravelRequest(request_id="wp", method=TravelMethod.WARP, target_id="dest",
                         classification=Classification.SPECULATIVE,
                         constraints=TravelConstraints(), parameters=p)


def conv_request(v=0.5 * C, d=3.0e6, dt=1.0e-5, **extra):
    p = {"origin_m": [0, 0, 0], "destination_m": [d, 0, 0], "speed_mps": v,
         "frame_id": SIM, "dt_step_s": dt}
    p.update(extra)
    return TravelRequest(request_id="cv", method=TravelMethod.CONVENTIONAL_RELATIVISTIC,
                         target_id="dest", classification=Classification.SIMULATED_DATA,
                         constraints=TravelConstraints(), parameters=p)


# ----------------------------- PHASE 4 — wormhole FSM -----------------------


class TestTraversalFSM:
    def test_full_state_sequence(self):
        p = plan(dt=2.0e-7)
        fsm = WormholeTraversalFSM(p)
        assert fsm.state == TraversalState.IDLE
        assert fsm.begin() == TraversalState.APPROACHING
        seen = [TraversalState.APPROACHING.value]
        while fsm.state != TraversalState.COMPLETE:
            seen.append(fsm.step().value)
        # approach leg is zero (start at mouth) — expect ENTRY/TRANSIT/EXIT in order
        assert seen.index("ENTRY") < seen.index("TRANSIT") < seen.index("EXIT")
        assert seen[-1] == "COMPLETE"

    def test_step_before_begin_refused(self):
        fsm = WormholeTraversalFSM(plan())
        with pytest.raises(TraversalError):
            fsm.step()

    def test_terminal_states_stop_ticking(self):
        fsm = WormholeTraversalFSM(plan())
        fsm.begin()
        fsm.abort()
        assert fsm.step() == TraversalState.ABORTED  # terminal, no error

    def test_abort_refused_after_complete(self):
        fsm = WormholeTraversalFSM(plan())
        fsm.begin()
        while fsm.state != TraversalState.COMPLETE:
            fsm.step()
        with pytest.raises(TraversalError):
            fsm.abort()

    def test_illegal_transition_refused(self):
        fsm = WormholeTraversalFSM(plan())
        with pytest.raises(TraversalError):
            fsm._transition(TraversalState.EXIT)

    def test_proper_less_than_coordinate(self):
        p = plan()
        assert p.proper_time_total_s < p.coordinate_time_total_s
        # gamma from delegated relativity (v=0.8c → 5/3 to fp precision)
        assert abs(p.gamma - 5.0 / 3.0) < 1e-12

    def test_zero_shape_flip_guard(self):
        with pytest.raises(InvalidGeometryParameterError):
            MorrisThorneMetric(throat_radius_m=-1.0, shape_func=lambda r: r)


# ------------------------- PHASE 4 adversarial / geometry -------------------


class TestTraversalParams:
    def test_beta_geq_1_rejected(self):
        with pytest.raises(TraversalError):
            plan(v=C)
        with pytest.raises(TraversalError):
            plan(v=2.0 * C)

    def test_nan_rejected(self):
        with pytest.raises(TraversalError):
            plan(v=float("nan"))
        with pytest.raises(TraversalError):
            TraversalPlan(metric=mt_metric(), mouth_in=MouthState("A", SIM, (float("nan"), 0, 0)),
                          mouth_out=MouthState("B", SIM, (3e6, 0, 0)),
                          observer_start_m=(0, 0, 0), transit_speed_mps=0.8 * C)

    def test_near_zero_throat_refused(self):
        with pytest.raises(InvalidGeometryParameterError):
            mt_metric(rt=0.0)
        with pytest.raises(InvalidGeometryParameterError):
            mt_metric(rt=float("nan"))
        with pytest.raises(InvalidGeometryParameterError):
            mt_metric(rt=-1.0)

    def test_frame_mismatch_rejected(self):
        with pytest.raises(TraversalError):
            TraversalPlan(metric=mt_metric(), mouth_in=MouthState("A", "F1", (0, 0, 0)),
                          mouth_out=MouthState("B", "F2", (3e6, 0, 0)),
                          observer_start_m=(0, 0, 0), transit_speed_mps=0.8 * C)

    def test_inside_throat_band_rejected_not_at_mouth(self):
        with pytest.raises(TraversalError):
            TraversalPlan(metric=mt_metric(1e3), mouth_in=MouthState("A", SIM, (0, 0, 0)),
                          mouth_out=MouthState("B", SIM, (3e6, 0, 0)),
                          observer_start_m=(500.0, 0, 0), transit_speed_mps=0.8 * C)

    def test_start_at_mouth_center_legitimate(self):
        p = plan()
        assert p.position_at(0.0) == (0, 0, 0)

    def test_tidal_scales_inversely(self):
        a1 = plan(rt=1e3).tidal_acceleration_at_throat()
        a2 = plan(rt=2e3).tidal_acceleration_at_throat()
        assert a2 == pytest.approx(a1 / 4.0, rel=1e-12)

    def test_energy_assumptions_explicit(self):
        a = plan().energy_condition_assumptions()
        assert a["requires_exotic_matter_at_throat"] is True
        assert "VIOLATED" in a["null_energy_condition"]
        assert a["classification_physics"] == "SPECULATIVE"

    def test_redshift_x_band(self):
        p = plan()
        assert p.gravitational_redshift_factor_at_throat == pytest.approx(1.0, abs=1e-15)
        with pytest.raises(TraversalError):
            p.redshift_at(p.throat_radius_m * 0.5)


# ------------------------- PHASE 5 — warp separation ------------------------


class TestWarpSeparation:
    def wp(self, vs=3.0 * C):
        return WarpPlan(metric=AlcubierreMetric(velocity=vs, radius_m=100.0, wall_steepness=5.0),
                        frame_id=SIM, origin_m=(0, 0, 0), destination_m=(3.0e6, 0, 0),
                        v_chart_mps=vs)

    def test_local_vs_effective_separation(self):
        w = self.wp()
        assert w.local_observer_speed_mps == 0.0
        assert w.effective_displacement_rate_mps == 3.0 * C

    def test_no_teleportation(self):
        w = self.wp()
        assert w.coordinate_time_total_s > 0.0

    def test_proper_equals_coordinate(self):
        w = self.wp()
        assert w.proper_time_total_s == w.coordinate_time_total_s
        assert w.proper_time_at(w.coordinate_time_total_s) == w.coordinate_time_total_s

    def test_causality_classification_shift(self):
        a = self.wp(3.0 * C)
        b = self.wp(0.5 * C)
        assert "ACAUSAL" in a.causality_status()
        assert "CAUSAL_CHART" in b.causality_status()

    def test_metric_mismatch_refused(self):
        m = AlcubierreMetric(velocity=1.0 * C, radius_m=100.0, wall_steepness=5.0)
        with pytest.raises(TraversalError):
            WarpPlan(metric=m, frame_id=SIM, origin_m=(0, 0, 0),
                     destination_m=(3e6, 0, 0), v_chart_mps=2.0 * C)

    def test_wall_steepness_cap_delegated(self):
        with pytest.raises(InvalidGeometryParameterError):
            AlcubierreMetric(velocity=1.0 * C, radius_m=100.0, wall_steepness=1.0e5)

    def test_energy_assumptions_speculative(self):
        a = self.wp().energy_assumptions()
        assert a["negative_energy_density_in_wall"] is True
        assert a["classification_phys"] == "SPECULATIVE"
        assert "NOT CLAIMED" in a["feasibility"]

    def test_tidal_guard(self):
        g = self.wp().tidal_guard()
        assert "satisfied" in g and "observer_scale_m" in g


# ------------------------- PHASES 6-8: journey authority --------------------


class TestJourneyEngine:
    def test_null_provider_fails_closed(self, ):
        with pytest.raises(TravelError):
            NullTravelProvider().initiate(wh_request())

    def test_wormhole_full_lifecycle(self):
        eng = JourneyEngine()
        req = wh_request()
        ok, why = eng.can_travel(req)
        assert ok, why
        res = eng.initiate(req)
        assert res.success and res.classification == Classification.SPECULATIVE
        assert "SPECULATIVE" in res.causal_status
        j = list(eng.journeys())[-1]
        seen = []
        while j.state != TraversalState.COMPLETE:
            seen.append(j.step().value)
        assert seen[-1] == "COMPLETE"
        snap = j.observer_snapshot()
        assert snap["position_m"] == (3.0e6, 0, 0)
        assert snap["frame_id"] == SIM
        assert snap["classification"] == "SPECULATIVE"
        assert snap["redshift_at_throat"] == pytest.approx(1.0, abs=1e-15)

    def test_conventional_causal_timelike(self):
        eng = JourneyEngine()
        res = eng.initiate(conv_request())
        assert "CAUSAL_TIMELIKE" in res.causal_status
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        flip = j.observer_snapshot()
        assert flip["gamma"] == pytest.approx(1.1547005383792517, rel=1e-12)

    def test_warp_lifecycle_and_classification(self):
        eng = JourneyEngine()
        res = eng.initiate(warp_request())
        assert res.success and res.classification == Classification.SPECULATIVE
        assert "ACAUSAL" in res.causal_status
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        snap = j.observer_snapshot()
        assert snap["local_observer_speed_mps"] == 0.0
        assert snap["effective_rate_mps"] == 3.0 * C
        assert snap["classification"] == "SPECULATIVE"

    def test_engine_rejects_unknown_mechanism(self):
        eng = JourneyEngine()
        req = TravelRequest(request_id="x", method=TravelMethod.TEMPORAL_DISPLACEMENT,
                            target_id="d", classification=Classification.HYPOTHETICAL,
                            constraints=TravelConstraints(),
                            parameters={"origin_m": [0, 0, 0], "destination_m": [1, 0, 0]})
        ok, why = eng.can_travel(req)
        assert not ok

    def test_observer_frame_switches(self):
        eng = JourneyEngine()
        req = wh_request(destination_frame_id="TARGET_FRAME")
        eng.initiate(req)
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        assert j.observer_snapshot()["frame_id"] == "TARGET_FRAME"

    def test_abort_and_status(self):
        eng = JourneyEngine()
        eng.initiate(conv_request())
        j = list(eng.journeys())[-1]
        j.step()
        assert j.abort("operator").value == "ABORTED"
    # ---- persistence ----

    def test_roundtrip_checksum_verified(self):
        eng = JourneyEngine()
        req = wh_request()
        eng.initiate(req)
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        d = j.to_dict()
        assert "checksum" in d and len(d["checksum"]) == 64
        j2 = Journey.from_dict(dict(d), plan_factory=lambda dd: eng._build_wormhole(req))
        assert j2.state == j.state and j2.coordinate_time_s == j.coordinate_time_s

    def test_tamper_any_field_caught(self):
        eng = JourneyEngine()
        req = wh_request()
        eng.initiate(req)
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        d = j.to_dict()
        for key in ("t_coord_s", "state", "checksum"):
            bad = dict(d)
            bad[key] = "EVIL" if key != "t_coord_s" else 9.9e99
            with pytest.raises(JourneyError):
                Journey.from_dict(bad, plan_factory=lambda dd: eng._build_wormhole(req))

    def test_schema_mismatch_caught(self):
        with pytest.raises(JourneyError):
            Journey.from_dict({"schema": "nope"}, plan_factory=lambda dd: (_ for _ in ()).throw(ValueError()))


class TestCausalityFailClosed:
    def test_conventional_acausal_marking_refused(self):
        eng = JourneyEngine()
        req = TravelRequest(request_id="x", method=TravelMethod.CONVENTIONAL_RELATIVISTIC,
                            target_id="d", classification=Classification.SIMULATED_DATA,
                            constraints=TravelConstraints(causal=False),
                            parameters={"origin_m": [0, 0, 0], "destination_m": [1.0e6, 0, 0],
                                        "speed_mps": 0.5 * C, "frame_id": SIM})
        ok, why = eng.can_travel(req)
        assert not ok
        assert "acausal" in why.lower()

    def test_classification_not_strengthened(self):
        # speculative request cannot produce SIMULATED/REAL result through the
        # DelegatingTravelService guard (existing authority already tests this)
        from astra.interaction.travel import DelegatingTravelService
        eng = JourneyEngine()
        svc = DelegatingTravelService(eng)
        req = wh_request()
        res = svc.initiate(req)
        assert res.classification == Classification.SPECULATIVE


# ----------------------------- PHASE 22 — determinism -----------------------


def _run_once(req):
    eng = JourneyEngine()
    eng.initiate(req)
    j = list(eng.journeys())[-1]
    out = []
    while j.state != TraversalState.COMPLETE:
        out.append(j.step().value)
    d = j.to_dict()
    d.pop("checksum")
    return out, d


class TestDeterminism:
    @pytest.mark.parametrize("req", [wh_request, warp_request, conv_request])
    def test_two_runs_identical(self, req):
        a, da = _run_once(req())
        b, db = _run_once(req())
        assert a == b
        assert json.dumps(da, sort_keys=True, default=str) == json.dumps(db, sort_keys=True, default=str)

    def test_plan_functions_deterministic(self):
        p1, p2 = plan(), plan()
        assert p1.coordinate_time_total_s == p2.coordinate_time_total_s
        assert p1.proper_time_total_s == p2.proper_time_total_s


# ----------------------------- PHASE 8/23 deep adversarial ------------------


class TestAdversarialEngine:
    def test_nan_anywhere_rejected(self):
        eng = JourneyEngine()
        for p in (
            {"origin_m": [float("nan"), 0, 0], "destination_m": [1, 0, 0], "speed_mps": 1e6},
            {"origin_m": [0, 0, 0], "destination_m": [float("inf"), 0, 0], "speed_mps": 1e6},
        ):
            ok, _ = eng.can_travel(TravelRequest(request_id="q", method=TravelMethod.CONVENTIONAL_RELATIVISTIC,
                                                 target_id="d", classification=Classification.SIMULATED_DATA,
                                                 constraints=TravelConstraints(), parameters=p))
            assert not ok

    def test_zero_distance_rejected(self):
        eng = JourneyEngine()
        ok, _ = eng.can_travel(conv_request(d=0.0))
        assert not ok

    def test_extreme_velocity_rejected(self):
        eng = JourneyEngine()
        ok, _ = eng.can_travel(conv_request(v=0.9999999999999999 * C * 1.0000000000000002, d=3e8))
        # v lands *exactly* at or above c due to fp rounding → must be refused
        assert not ok

    def test_scale_dt_guards(self):
        eng = JourneyEngine()
        assert not eng.can_travel(wh_request(observer_scale_m=-1.0))[0]
        assert not eng.can_travel(wh_request(dt_step_s=0.0))[0]
        assert not eng.can_travel(wh_request(transit_speed_mps=0.0))[0]  # zero speed
        assert not eng.can_travel(wh_request(transit_speed_mps=-3.0))[0]  # negative
        assert not eng.can_travel(wh_request(transit_speed_mps=2.0 * C))[0]  # beta>=1


# ----------------------------- events (PHASE 19) -----------------------------


class TestEvents:
    def test_events_published_mechanisms(self):
        events = []
        class Bus:
            def publish_sync(self, name, tick, data=None, source=""):
                events.append(name)
        eng = JourneyEngine(event_bus=Bus())
        eng.initiate(wh_request())
        j = list(eng.journeys())[-1]
        while j.state != TraversalState.COMPLETE:
            j.step()
        assert "journey_begin" in events
        assert "journey_complete" in events
        assert "journey_state" in events

    def test_abort_event(self):
        events = []
        class Bus:
            def publish_sync(self, name, tick, data=None, source=""):
                events.append(name)
        eng = JourneyEngine(event_bus=Bus())
        eng.initiate(conv_request())
        j = list(eng.journeys())[-1]
        j.step()
        j.abort("operator")
        assert "journey_abort" in events
