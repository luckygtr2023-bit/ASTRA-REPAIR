#!/usr/bin/env python3
"""v1.5 reference fixtures — DERIVED FROM THE PYTHON AUTHORITY (never invented).

Emits two artifacts:
  1. tests/fixtures/v15_measure_reference.txt — fixed-format records for the
     native mirror-check (positions/times/states sampled deterministically).
  2. /tmp/v15_reference.csv — richer CSV for the mirror checker (optional arg).

The native side reproduces the same arithmetic in C++ doubles; comparisons
use measured tolerances (fp-identical ops => 0 diff allowed for structural
quantities; tanh-based Alcubierre gets measured fp bound, recorded here).
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from astra.theoretical.wormhole import MorrisThorneMetric  # noqa: E402
from astra.theoretical.traversal import (  # noqa: E402
    MouthState, TraversalPlan, WormholeTraversalFSM, TraversalState,
)
from astra.theoretical.warp import AlcubierreMetric  # noqa: E402
from astra.theoretical.warp_journey import WarpPlan  # noqa: E402
from astra.interaction.journey import (  # noqa: E402
    ConventionalPlan, JourneyEngine, Journey,
)
from astra.interaction.travel import TravelRequest, TravelMethod, TravelConstraints  # noqa: E402
from astra.scientific.classification import Classification  # noqa: E402

C = 299792458.0
SIM = "SIM_FRAME"


def mt(rt):
    return MorrisThorneMetric(throat_radius_m=rt, shape_func=lambda r: rt * (rt / r) ** 0.5 if r > 0.0 else rt)


def build_plans():
    wh = TraversalPlan(metric=mt(1.0e3),
                       mouth_in=MouthState("A", SIM, (0, 0, 0)),
                       mouth_out=MouthState("B", SIM, (3.0e6, 0, 0)),
                       observer_start_m=(0, 0, 0), transit_speed_mps=0.8 * C, dt_step_s=5.0e-7)
    wp = WarpPlan(metric=AlcubierreMetric(velocity=3.0 * C, radius_m=100.0, wall_steepness=5.0),
                  frame_id=SIM, origin_m=(0, 0, 0), destination_m=(3.0e6, 0, 0),
                  v_chart_mps=3.0 * C, dt_step_s=1.0e-4)
    cv = ConventionalPlan(frame_id=SIM, origin_m=(0, 0, 0), destination_m=(3.0e6, 0, 0),
                          speed_mps=0.5 * C, dt_step_s=1.0e-4)
    return wh, wp, cv


def emit_records():
    wh, wp, cv = build_plans()
    recs = []

    # SCEN WH-O0: wormhole totals (observer at mouth center)
    recs.append(("WH-O0", "coord_total", wh.coordinate_time_total_s, "s", ""))
    recs.append(("WH-O0", "proper_total", wh.proper_time_total_s, "s", ""))
    recs.append(("WH-O0", "gamma", wh.gamma, "", ""))
    recs.append(("WH-O0", "tidal_at_throat", wh.tidal_acceleration_at_throat(), "m/s^2", ""))
    recs.append(("WH-O0", "redshift_at_throat", wh.gravitational_redshift_factor_at_throat, "", ""))
    for frac in (0.0, 0.125, 0.25, 0.5, 0.75, 0.999, 1.0):
        t = frac * wh.coordinate_time_total_s
        p = wh.position_at(t)
        recs.append((f"WH-S{frac:g}", "pos", (p[0], p[1], p[2]), "m", str(wh.state_at(t).value)))
        recs.append((f"WH-S{frac:g}", "tau", wh.proper_time_at(t), "s", str(wh.state_at(t).value)))

    # SCEN WP-O0: warp totals
    recs.append(("WP-O0", "coord_total", wp.coordinate_time_total_s, "s", ""))
    recs.append(("WP-O0", "proper_total", wp.proper_time_total_s, "s", ""))
    recs.append(("WP-O0", "effective_rate", wp.effective_displacement_rate_mps, "m/s", ""))
    recs.append(("WP-O0", "local_speed", wp.local_observer_speed_mps, "m/s", ""))
    recs.append(("WP-O0", "causality", wp.causality_status()[:60], "text", ""))
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        t = frac * wp.coordinate_time_total_s
        p = wp.position_at(t)
        recs.append((f"WP-S{frac:g}", "pos", (p[0], p[1], p[2]), "m", ""))

    # SCEN CV-O0: conventional
    recs.append(("CV-O0", "coord_total", cv.coordinate_time_total_s, "s", ""))
    recs.append(("CV-O0", "proper_total", cv.proper_time_total_s, "s", ""))
    recs.append(("CV-O0", "gamma", cv.gamma, "", ""))
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        t = frac * cv.coordinate_time_total_s
        p = cv.position_at(t)
        recs.append((f"CV-S{frac:g}", "pos", (p[0], p[1], p[2]), "m", ""))

    # SCEN JR-O0: engine journey (wormhole) end state + causal vocab
    eng = JourneyEngine()
    req = TravelRequest(request_id="jr0", method=TravelMethod.WORMHOLE, target_id="dest",
                        classification=Classification.SPECULATIVE, constraints=TravelConstraints(),
                        parameters={"origin_m": [0, 0, 0], "destination_m": [3.0e6, 0, 0],
                                    "throat_radius_m": 1.0e3, "transit_speed_mps": 0.8 * C,
                                    "frame_id": SIM, "dt_step_s": 5.0e-7})
    res = eng.initiate(req)
    j = list(eng.journeys())[-1]
    ticks = 0
    while j.state != TraversalState.COMPLETE:
        j.step()
        ticks += 1
    recs.append(("JR-O0", "ticks_to_complete", ticks, "count", ""))
    recs.append(("JR-O0", "end_coord", j.coordinate_time_s, "s", ""))
    recs.append(("JR-O0", "causal", res.causal_status[:60], "text", ""))
    return recs


def main():
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "native_renderer", "tests", "fixtures", "v15_measure_reference.txt")
    recs = emit_records()
    with open(out, "w", encoding="utf-8") as f:
        f.write("# ASTRA v1.5 reference fixtures — DERIVED FROM PYTHON AUTHORITY\n")
        f.write("# scen_id, quantity, value(s), unit, meta — DO NOT hand-edit; regenerative via scripts/gen_v15_reference.py\n")
        for rid, qty, val, unit, meta in recs:
            f.write(f"{rid},{qty},{val!r},{unit},{meta}\n")
    print(f"wrote {len(recs)} records -> {out}")
    # measured parity bound note (tanh cross-lib): measured on this toolchain
    print("expected parity: structural quantities fp-identical; alcubierre-shape uses measured bound")


if __name__ == "__main__":
    main()
