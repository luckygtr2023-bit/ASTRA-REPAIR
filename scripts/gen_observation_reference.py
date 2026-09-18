#!/usr/bin/env python3
"""ASTRA v1.3 — Observation & Cosmic History (domain 3) reference generator.

Runs the REAL astra.temporal authority on fixed cases and emits a CSV
reference to stdout; compared against native_renderer / observation_sim.

Row families:
  O observe:   O,id,(t_obs,t_emit,lookback,em:x,y,z,act:x,y,z) | O,id,ERR,class
  L lookback:  L,id,lookback | L,id,ERR,class
  C causal:    C,id,relate/cone/acc,value | C,id,ERR,class
  P proper:    P,id,seconds | P,id,ERR,class
  D dilation:  D,id,seconds | D,id,ERR,class
  K clock:     K,id,sim,coord,proper,rate | K,id,ERR,class
"""
import math
import sys

sys.path.insert(0, ".")

from astra.relativity.core import SPEED_OF_LIGHT
from astra.spacetime.events import SpacetimeEvent, Worldline, CHART_CARTESIAN, CHART_SPHERICAL
from astra.temporal.clock import TemporalClock
from astra.temporal.observation import lookback_time, observe
from astra.temporal.proper_time import (
    flat_proper_time, gravitational_time_dilation, velocity_time_dilation,
)
from astra.temporal.causal import (
    CausalRelation, LightConeRegion, classify, is_causally_accessible,
    light_cone_region, relate,
)
from astra.temporal.exceptions import (
    InvalidTemporalStateError, InvalidWorldlineError, TemporalHistoryUnavailableError,
)

OUT = []


def f17(v):
    return "%.17g" % v


def errclass(exc):
    for kls, name in ((TemporalHistoryUnavailableError, "TemporalHistoryUnavailableError"),
                      (InvalidWorldlineError, "InvalidWorldlineError"),
                      (InvalidTemporalStateError, "InvalidTemporalStateError")):
        if isinstance(exc, kls):
            return name
    return type(exc).__name__


def wl(samples, chart=CHART_CARTESIAN):
    return Worldline(tuple(
        (p, SpacetimeEvent(ct, x, y, z, chart)) for p, ct, x, y, z in samples))


OBSERVER = (0.0, 0.0, 0.0)

# worldlines ------------------------------------------------------------
W_MOVING = wl([(0.0, 0.0, 0.0, 0.0, 0.0), (10.0, 10.0 * SPEED_OF_LIGHT, 3e8, 0.0, 0.0)])
W_STATIC_FAR = wl([(0.0, 0.0, 1e12, 0.0, 0.0), (100.0, 100.0 * SPEED_OF_LIGHT, 1e12, 0.0, 0.0)])
W_STILL_AT_ORIGIN = wl([(0.0, 0.0, 0.0, 0.0, 0.0), (10.0, 10.0 * SPEED_OF_LIGHT, 0.0, 0.0, 0.0)])
W_OBSERVER_NEAR = wl([(0.0, 0.0, 1e9, 0.0, 0.0), (100.0, 100.0 * SPEED_OF_LIGHT, 1e9, 0.0, 0.0)])
W_CURVED = wl([
    (0.0, 0.0, 0.0, 0.0, 0.0),
    (5.0, 5.0 * SPEED_OF_LIGHT, 1e8, 2e8, -1e8),
    (10.0, 10.0 * SPEED_OF_LIGHT, 3e8, 4e8, 8e7),
    (20.0, 20.0 * SPEED_OF_LIGHT, 6e8, 1e8, 4e8),
])
W_SPHERICAL = wl([
    (0.0, 0.0, 1e9, 0.3, 1.2),
    (100.0, 100.0 * SPEED_OF_LIGHT, 9e8, 0.3, 1.2),
], chart=CHART_SPHERICAL)

O_CASES = [
    ("moving_mid", W_MOVING, OBSERVER, 5.0),
    ("moving_late", W_MOVING, OBSERVER, 9.5),
    ("static_far", W_STATIC_FAR, OBSERVER, 5000.0),
    ("static_far_2", W_STATIC_FAR, OBSERVER, 3350.0),
    ("near_observer", W_OBSERVER_NEAR, OBSERVER, 100.0),
    ("observer_shift", W_MOVING, (1e9, 0.0, 0.0), 100.0),
    ("sample_hit", W_CURVED, OBSERVER, 5.0),
    ("between_1", W_CURVED, OBSERVER, 7.25),
    ("between_2", W_CURVED, OBSERVER, 13.75),
    ("at_first", W_MOVING, OBSERVER, 1e-9),
    ("not_reached", W_STATIC_FAR, OBSERVER, 1000.0),
    ("history_ends", W_MOVING, OBSERVER, 20.0),
    ("before_all", W_MOVING, OBSERVER, 0.0),          # t_obs=0 == first sample: ok? g(0)=0
    ("bad_t_obs_neg", W_MOVING, OBSERVER, -1.0),
    ("bad_observer_nan", W_MOVING, (float("nan"), 0.0, 0.0), 5.0),
    ("short_worldline_error", wl([(0.0, 0.0, 0.0, 0.0, 0.0)]), OBSERVER, 5.0),
    ("spherical_line", W_SPHERICAL, OBSERVER, 10.0),
]
for cid, h, ox, t in O_CASES:
    try:
        r = observe(h, ox, t, observer="cosmos-check")
        e = r.emission_event
        a = r.actual_state_at_observation
        OUT.append(
            "O,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s" % (
                cid, f17(r.observation_time_s), f17(r.emission_time_s),
                f17(r.lookback_time_s), f17(e.ct_m), f17(e.x), f17(e.y), f17(e.z),
                f17(a.ct_m), f17(a.x), f17(a.y), f17(a.z)))
    except Exception as ex:
        OUT.append(f"O,{cid},ERR,{errclass(ex)}")

LB = [
    ("basic", OBSERVER, (1e9, 0.0, 0.0), 100.0, 10.0),
    ("zero", OBSERVER, OBSERVER, 10.0, 5.0),
    ("xyz", OBSERVER, (3.0, 4.0 * SPEED_OF_LIGHT, 0.0), 50.0, 0.0),
    ("bad_future_emit", OBSERVER, OBSERVER, 10.0, 11.0),
    ("bad_nan_pos", OBSERVER, (float("nan"), 0.0, 0.0), 10.0, 0.0),
]
for cid, oo, ee, tobs, tem in LB:
    try:
        v = lookback_time(oo, ee, tobs, tem)
        OUT.append(f"L,{cid},{f17(v)}")
    except Exception as ex:
        OUT.append(f"L,{cid},ERR,{errclass(ex)}")

# proper time / dilation -------------------------------------------------
P_CASES = [
    ("inertial_diag", wl([(0.0, 0.0, 0.0, 0.0, 0.0),
                          (10.0, 10.0 * SPEED_OF_LIGHT, 6e8, 0.0, 0.0)])),
    ("inertial_two_seg", wl([(0.0, 0.0, 0.0, 0.0, 0.0),
                             (5.0, 5.0 * SPEED_OF_LIGHT, 3e8, 0.0, 0.0),
                             (10.0, 10.0 * SPEED_OF_LIGHT, 6e8, 4e8, 0.0)])),
    ("null_line", wl([(0.0, 0.0, 0.0, 0.0, 0.0),
                      (10.0, 10.0 * SPEED_OF_LIGHT, 10.0 * SPEED_OF_LIGHT, 0.0, 0.0)])),
    ("spacelike_fail", wl([(0.0, 0.0, 0.0, 0.0, 0.0),
                           (1.0, 1.0 * SPEED_OF_LIGHT, 6.0 * SPEED_OF_LIGHT, 0.0, 0.0)])),
    ("one_sample_fail", wl([(0.0, 0.0, 0.0, 0.0, 0.0)])),
]
for cid, h in P_CASES:
    try:
        OUT.append(f"P,{cid},{f17(flat_proper_time(h))}")
    except Exception as ex:
        OUT.append(f"P,{cid},ERR,{errclass(ex)}")

D_CASES = [
    ("v_half_c", 100.0, 0.5 * SPEED_OF_LIGHT),
    ("v_0_8c", 1000.0, 0.8 * SPEED_OF_LIGHT),
    ("v_quarter_c", 7.5, 0.25 * SPEED_OF_LIGHT),
    ("v_zero", 123.456, 0.0),
    ("v_fail", 10.0, SPEED_OF_LIGHT),
    ("g_sun_surf", 86400.0, None),   # Sun mass/radius
    ("g_sun_1au", 31557600.0, None),
]
MSUN = 1.98847e30
RSUN = 6.96e8
for cid, t, v in D_CASES:
    try:
        if cid == "g_sun_surf":
            OUT.append(f"D,{cid},{f17(gravitational_time_dilation(t, MSUN, RSUN))}")
        elif cid == "g_sun_1au":
            OUT.append(f"D,{cid},{f17(gravitational_time_dilation(t, MSUN, 1.496e11))}")
        else:
            OUT.append(f"D,{cid},{f17(velocity_time_dilation(t, v))}")
    except Exception as ex:
        OUT.append(f"D,{cid},ERR,{errclass(ex)}")

# causal policy on the FLAT metric --------------------------------------
from astra.spacetime.metric import MinkowskiMetric
MET = MinkowskiMetric()
CT = SPEED_OF_LIGHT
A = (0.0, 0.0, 0.0, 0.0)
C_CASES = [
    ("coincident", A, A),
    ("time_sep", A, (5.0 * CT, 0.0, 0.0, 0.0)),
    ("null_sep", A, (1.0 * CT, 1.0 * CT, 0.0, 0.0)),
    ("space_sep", A, (0.5 * CT, 10.0 * CT, 0.0, 0.0)),
    ("past_sep", (10.0 * CT, 0.0, 0.0, 0.0), (4.0 * CT, 0.0, 0.0, 0.0)),
]
for cid, a, b in C_CASES:
    try:
        r = relate(MET, a, b)
        c = light_cone_region(MET, a, b)
        acc = is_causally_accessible(MET, a, b)
        OUT.append(f"C,{cid},{r.value},{c.value},{1 if acc else 0}")
    except Exception as ex:
        OUT.append(f"C,{cid},ERR,{errclass(ex)}")

# temporal clock ----------------------------------------------------------
def clock_case(cid, ops):
    c = TemporalClock(observer="cosmos-check")
    try:
        for op, arg in ops:
            f = getattr(c, op)
            try:
                f(arg, require_authority=False)
            except TypeError:
                f(arg)  # advance/reset accept require_authority; set_rate too
        s = c.to_state()
        OUT.append(f"K,{cid},{f17(s.simulation_time_s)},{f17(s.coordinate_time_s)},"
                   f"{f17(s.proper_time_s)},{f17(s.rate)}")
    except Exception as ex:
        OUT.append(f"K,{cid},ERR,{errclass(ex)}")

clock_case("advance_plain", [("advance", 5.0)])
clock_case("advance_halfrate", [("set_rate", 0.5), ("advance", 4.0)])
clock_case("advance_multi", [("advance", 1.5), ("set_rate", 0.8), ("advance", 2.5),
                             ("set_rate", 1.0), ("advance", 0.25)])
clock_case("bad_neg_advance", [("advance", -1.0)])
clock_case("bad_zero_rate", [("set_rate", 0.0)])
clock_case("reset_then", [("advance", 10.0), ("set_rate", 0.5), ("reset", 0.0),
                          ("advance", 2.0)])
print("\n".join(OUT))
