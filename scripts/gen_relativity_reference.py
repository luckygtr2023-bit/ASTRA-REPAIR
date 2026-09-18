#!/usr/bin/env python3
"""Generate relativity reference values from the Python scientific authority
(astra.relativity) for the ASTRA COSMOS native C++ fidelity gate
(tests/relativity_mirror_check.cpp).

Rows cover every mirrored primitive across the full domain (v = 0, classical,
moderate, relativistic, near-c), using deterministic inputs. Every formula is
executed by the AUTHORITY itself; only values are written.

Format: fn,inputs...,expected-or-ERR:name — the checker reproduces mapping
per row. Tolerances are NOT prescribed here; the C++ checker gates at exact-
bit equality where the op chain is identical (and prints achieved errors).

Units: strict SI (m, s, kg, J). Four-vector t component in metres of light.

Usage: python scripts/gen_relativity_reference.py > /tmp/relativity_reference.csv
"""
from __future__ import annotations

import math
import sys

from astra.relativity.core import (
    SPEED_OF_LIGHT, beta, lorentz_factor, relativistic_mass, total_energy,
    kinetic_energy, relativistic_momentum,
)
from astra.relativity.four_vectors import FourVector, SpacetimeEvent
from astra.relativity.lorentz import boost_x, inverse_boost_x
from astra.relativity.gr_foundations import schwarzschild_radius, weak_field_time_dilation
from astra.relativity.exceptions import (
    InvalidVelocityError, InvalidRestMassError, LightSpeedViolation,
    SpacelikeIntervalError, DegenerateMetricError,
)
from astra.mathematics import Vector3

C = SPEED_OF_LIGHT

# Deterministic velocity test points (m/s), edges included.
SPEEDS = [0.0, 1e-12, 1.0, 10.0, 1.0e3, 1.0e5, 2.5e6, 5.0e7,
          0.3 * C, 0.5 * C, 0.8 * C, 0.9 * C, 0.99 * C, 0.9999999 * C,
          # Documented authority domain quirk (v1.1 finding): negative scalar
          # magnitudes are outside the tested domain; b < threshold sends them
          # down the low-beta SERIES branch. Not introduced here — mirrored.
          -1.0, -0.9 * C]
# Massive-object boundary cases (must raise LightSpeedViolation).
BAD_SPEEDS = [C, 1.1 * C, float("inf"), float("nan")]
MASSES = [0.0, 1e-30, 1.0, 5.9724e24, 1.9885e30, 2e40]
BAD_MASSES = [-1.0, float("inf"), float("nan")]


def err_name(exc: Exception) -> str:
    return f"ERR:{type(exc).__name__}"


def emit(fn: str, inputs: str, expected) -> None:
    if isinstance(expected, str):
        print(f"{fn},{inputs},{expected}")
    elif isinstance(expected, (tuple, list)):
        print(f"{fn},{inputs}," + "|".join(f"{x:.17e}" for x in expected))
    else:
        print(f"{fn},{inputs},{expected:.17e}")


def main() -> int:
    for v in SPEEDS + BAD_SPEEDS:
        for fn_name, fn in (("beta", beta), ("lorentz", lorentz_factor)):
            try:
                emit(fn_name, f"{v:.17e}", fn(v))
            except (InvalidVelocityError, LightSpeedViolation) as e:
                emit(fn_name, f"{v:.17e}", err_name(e))
    for m in MASSES + BAD_MASSES:
        for v in SPEEDS:
            for fn_name, fn in (("relmass", relativistic_mass),
                                ("totale", total_energy),
                                ("kinetic", kinetic_energy)):
                try:
                    emit(fn_name, f"{m:.17e}|{v:.17e}", fn(m, v))
                except (InvalidRestMassError, LightSpeedViolation, InvalidVelocityError) as e:
                    emit(fn_name, f"{m:.17e}|{v:.17e}", err_name(e))
    # Momentum (vector): a few directions incl. mixed-sign components.
    for (vx, vy, vz) in [(0.0, 0.0, 0.0), (1e5, 0.0, 0.0),
                         (0.6 * C, 0.0, 0.0), (1e6, -2e6, 3e6),
                         (-0.8 * C, 0.0, 0.0), (0.3 * C, -0.3 * C, 0.3 * C)]:
        for m in (1.0, 5.9724e24):
            vec = Vector3(vx, vy, vz)
            try:
                p = relativistic_momentum(m, vec)
                emit("momentum", f"{m:.17e}|{vx:.17e}|{vy:.17e}|{vz:.17e}",
                     (p.x, p.y, p.z))
            except (InvalidRestMassError, LightSpeedViolation) as e:
                emit("momentum", f"{m:.17e}|{vx:.17e}|{vy:.17e}|{vz:.17e}", err_name(e))
    # Four-vectors & invariants.
    for (t, x, y, z) in [(0.0, 0.0, 0.0, 0.0), (10.0, 5.0, 0.0, 0.0),
                         (3.0, 3.0, 0.0, 0.0), (5.0, 12.0, 0.0, 0.0),
                         (1e9, 1e9 + 1e-10, 0.0, 0.0), (7.0, 3.0, 4.0, 0.0),
                         (1.0, 2.0, 2.0, 1.0)]:
        fv = FourVector(t, x, y, z)
        ds2 = fv.invariant_sq()
        emit("invariant", f"{t:.17e}|{x:.17e}|{y:.17e}|{z:.17e}", ds2)
        emit("interval", f"{t:.17e}|{x:.17e}|{y:.17e}|{z:.17e}",
             fv.interval_type().value if False else fv.interval_type().value)
    # Proper time between events (from SI coordinates).
    for (ts, xs, ys, zs, te, xe, ye, ze) in [
            (0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0),          # timelike 1 s
            (0.0, 0.0, 0.0, 0.0, 1.0, 2.0e8, 0.0, 0.0),         # timelike
            (0.0, 0.0, 0.0, 0.0, 1.0, C, 0.0, 0.0),             # null
            (0.0, 0.0, 0.0, 0.0, 1.0, 4.0e8, 0.0, 0.0),         # spacelike -> err
            (2.0, 1e7, 0.0, 0.0, 5.0, 6e7, 1e7, 0.0)]:          # timelike
        e0 = SpacetimeEvent.from_coordinates(ts, xs, ys, zs)
        e1 = SpacetimeEvent.from_coordinates(te, xe, ye, ze)
        try:
            emit("ptime", f"{ts:.17e}|{xs:.17e}|{ys:.17e}|{zs:.17e}|"
                          f"{te:.17e}|{xe:.17e}|{ye:.17e}|{ze:.17e}",
                 e0.proper_time_to(e1))
        except SpacelikeIntervalError as e:
            emit("ptime", f"{ts:.17e}|{xs:.17e}|{ys:.17e}|{zs:.17e}|"
                          f"{te:.17e}|{xe:.17e}|{ye:.17e}|{ze:.17e}", err_name(e))
    # Lorentz boosts (X).
    for (t, x, y, z) in [(10.0, 0.0, 1.0, -2.0), (5.0, 4.0, 0.5, 0.25),
                         (0.0, 3.0e8, 0.0, 0.0), (1.2e9, -3.0e8, 2.0, 2.0)]:
        for v in (0.0, 1e5, 0.5 * C, -0.6 * C, 0.9999 * C, C):
            fv = FourVector(t, x, y, z)
            inp = f"{t:.17e}|{x:.17e}|{y:.17e}|{z:.17e}|{v:.17e}"
            try:
                b = boost_x(fv, v)
                emit("boostx", inp, (b.t, b.x, b.y, b.z))
            except (InvalidVelocityError, LightSpeedViolation) as e:
                emit("boostx", inp, err_name(e))
            try:
                b = inverse_boost_x(fv, v)
                emit("invboostx", inp, (b.t, b.x, b.y, b.z))
            except (InvalidVelocityError, LightSpeedViolation) as e:
                emit("invboostx", inp, err_name(e))
    # Schwarzschild radius + weak-field time dilation.
    for m in MASSES + BAD_MASSES:
        try:
            emit("rs", f"{m:.17e}", schwarzschild_radius(m))
        except DegenerateMetricError as e:
            emit("rs", f"{m:.17e}", err_name(e))
    for (m, r) in [(1.9885e30, 6.96e8), (1.9885e30, 1.496e11),
                   (1.9885e30, 1.471e11), (5.9724e24, 6.371e6),
                   (5.9724e24, 4.0e7), (2e30, 1e4), (2e30, 3.0e3),
                   (1.9885e30, 2.953e3), (1.0, 1.0),  # r <= rs -> err
                   (2e30, 2.965e3)]:                     # r <= rs (2.965e3 < rs) -> err
        try:
            emit("wftd", f"{m:.17e}|{r:.17e}", weak_field_time_dilation(m, r))
        except DegenerateMetricError as e:
            emit("wftd", f"{m:.17e}|{r:.17e}", err_name(e))
    return 0


if __name__ == "__main__":
    sys.exit(main())
