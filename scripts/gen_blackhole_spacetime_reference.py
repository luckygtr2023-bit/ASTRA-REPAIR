#!/usr/bin/env python3
"""Generate black-hole + spacetime reference values from the Python scientific
authority (astra.blackhole / astra.spacetime) for the ASTRA COSMOS native C++
fidelity gate (native_renderer/tests/blackhole_spacetime_mirror_check.cpp).

Rows cover every mirrored primitive across its real domain, including error
taxonomy rows (ERR:<ExceptionName>), extremal-Kerr clamps, horizon guard
bands, off-diagonal null-direction NotImplementedError, and geodesic runs.
Every formula is executed by the AUTHORITY itself; only values are written.

Format: fn,inputs...,expected-or-ERR:name — same row grammar as
scripts/gen_relativity_reference.py. Units: strict SI (m, s, kg); x^0 = ct.

Usage: python scripts/gen_blackhole_spacetime_reference.py > /tmp/bhst_reference.csv
"""
from __future__ import annotations

import math

from astra.blackhole import api as bh_api
from astra.blackhole import kerr as bh_kerr
from astra.blackhole import schwarzschild as bh_schw
from astra.blackhole.exceptions import (
    BlackHoleError, CoordinateSingularityError, InvalidBlackHoleMassError,
    InvalidGeometryInputError, InvalidSpinParameterError,
)
from astra.blackhole.parameters import BlackHoleState, validate_mass_kg, validate_spin_param
from astra.mathematics import Vector3
from astra.relativity.exceptions import LightSpeedViolation
from astra.spacetime import api as st_api
from astra.spacetime import causality, connection, curvature, events, geodesics, metric as st_metric
from astra.spacetime.events import SpacetimeEvent, cartesian_to_spherical, spherical_to_cartesian
from astra.spacetime.exceptions import SpacetimeError

M_SUN = 1.989e30
C = 299792458.0
HALF_PI = math.pi / 2
THIRD_PI = math.pi / 3


def err_name(exc: Exception) -> str:
    return f"ERR:{type(exc).__name__}"


def emit(fn: str, inputs: str, expected) -> None:
    if isinstance(expected, str):
        print(f"{fn},{inputs},{expected}")
    elif isinstance(expected, (tuple, list)):
        print(f"{fn},{inputs}," + "|".join(f"{x:.17e}" for x in expected))
    else:
        print(f"{fn},{inputs},{expected:.17e}")


def res(fn_name, inputs, thunk, exceptions):
    try:
        emit(fn_name, inputs, thunk())
    except exceptions as e:  # noqa: BLE001 — deliberate taxonomy emission
        emit(fn_name, inputs, err_name(e))


BH_EXCS = (InvalidBlackHoleMassError, InvalidSpinParameterError,
           InvalidGeometryInputError, CoordinateSingularityError, ValueError)
ST_EXCS = (SpacetimeError, ValueError)
API_EXCS = (SpacetimeError, ValueError, BlackHoleError, LightSpeedViolation)


def bh_rows() -> None:
    masses = [0.1, 1.0, 10.0, 1.0e6, 1000.0, M_SUN, 1.0e40] + \
             [0.0, -1.0, float("nan"), float("inf")]
    for m in masses:
        res("bh_mass", f"{m:.17e}", lambda m=m: validate_mass_kg(m), BH_EXCS)
    spins = [0.0, 1e-300, 0.5, -0.5, 0.9, -0.9, 0.9999999999999998, 1.0, -1.0] + \
            [1.0000000000000002, -1.5, float("nan"), float("inf")]
    for a in spins:
        res("bh_spin", f"{a:.17e}", lambda a=a: validate_spin_param(a), BH_EXCS)
    for m in [0.0, -3.0, M_SUN, 10.0]:
        for a in [0.0, 0.5, 1.0000000000000002]:
            def mk(m=m, a=a):
                s = bh_api.create_black_hole(m, a)
                return (s.mass_kg, s.spin_param)
            res("bh_create", f"{m:.17e}|{a:.17e}", mk, BH_EXCS)
    for a in [0.0, 0.25, 1.0, -0.75]:
        s = bh_api.create_black_hole(M_SUN, a)
        emit("bh_model", f"{a:.17e}", s.model.value)
    for m in [M_SUN, 10.0, 1.0e40]:
        for a in [0.0, 0.7, 1.0, -0.3]:
            s = BlackHoleState(mass_kg=m, spin_param=a)
            emit("bh_rg", f"{m:.17e}|{a:.17e}", s.gravitational_radius)
            emit("bh_spinlen", f"{m:.17e}|{a:.17e}", s.spin_length)
            emit("bh_bdict", f"{m:.17e}|{a:.17e}",
                 (s.to_dict()["mass_kg"], s.to_dict()["spin_param"]))
    for m in masses:
        for name, fn in (("bh_rs", bh_schw.schwarzschild_radius_m),
                         ("bh_isco", bh_schw.isco_radius),
                         ("bh_photon", bh_schw.photon_sphere_radius)):
            res(name, f"{m:.17e}", lambda m=m, fn=fn: fn(m), BH_EXCS)

    # Scalar Schwarzschild: mass sweep x radius sweep (guard band included).
    for m in [M_SUN, 4.3e6 * M_SUN, 10.0]:
        s = BlackHoleState(mass_kg=m, spin_param=0.0)
        rs = bh_schw.schwarzschild_radius_m(m)
        radii = [rs * 1.000001, rs * 1.001, rs * 1.5, rs * 2.0, rs * 3.0,
                 rs * 10.0, rs * 1.0e6,
                 rs + 0.5 * 1.0e-9, rs, rs * 0.9, 0.0, -rs, float("nan"), float("inf")]
        for r in radii:
            res("bh_sdil", f"{m:.17e}|{r:.17e}",
                lambda s=s, r=r: bh_schw.gravitational_time_dilation(s, r), BH_EXCS)
            res("bh_fdtdil", f"{m:.17e}|{r:.17e}",  # facade dispatch
                lambda s=s, r=r: bh_api.gravitational_time_dilation(s, r), API_EXCS)
        for re_ in [rs * 1.000001, rs * 1.5, rs * 3.0, rs + 1e-10, float("nan")]:
            for ro_ in [rs * 2.0, rs * 1.5, rs * 1.0e6, rs * 0.8]:
                res("bh_z", f"{m:.17e}|{re_:.17e}|{ro_:.17e}",
                    lambda s=s, re_=re_, ro_=ro_: bh_schw.gravitational_redshift(s, re_, ro_),
                    BH_EXCS)
        sch_b = bh_api.get_schwarzschild_boundaries(s)
        emit("bh_sb", f"{m:.17e}", (sch_b["schwarzschild_radius"], sch_b["gravitational_radius"],
                                    sch_b["photon_sphere_radius"], sch_b["isco_radius"]))

    # Kerr subsystems: spins sweep x radius sweep.
    for m in [M_SUN, 10.0]:
        for a in [0.0, 1e-16, 0.3, 0.9, 0.9999999999999998, 1.0, -0.5, -1.0]:
            s = BlackHoleState(mass_kg=m, spin_param=a)
            rg = s.gravitational_radius

            res("bh_kh", f"{m:.17e}|{a:.17e}", lambda s=s: bh_kerr.horizons(s), BH_EXCS)
            for th in [0.0, 1e-12, 0.5, HALF_PI, 2.2, float("nan")]:
                res("bh_ergo", f"{m:.17e}|{a:.17e}|{th:.17e}",
                    lambda s=s, th=th: bh_kerr.ergosphere_radius(s, th), BH_EXCS)
            emit("bh_kisco", f"{m:.17e}|{a:.17e}",
                 (bh_kerr.isco_prograde(s), bh_kerr.isco_retrograde(s)))
            emit("bh_kphoton", f"{m:.17e}|{a:.17e}",
                 (bh_kerr.photon_orbit_prograde(s), bh_kerr.photon_orbit_retrograde(s)))

            rp, _ = bh_kerr.horizons(s)
            r_radii = [rp + 1.0e-8, rp * 1.000001 + 1.0e-9, rp * 1.01, rp * 2.0,
                       rp * 10.0, rp + 1e-10, rp, float("nan")]
            for r in r_radii:
                res("bh_omega", f"{m:.17e}|{a:.17e}|{r:.17e}",
                    lambda s=s, r=r: bh_kerr.frame_dragging_angular_velocity(s, r, HALF_PI),
                    BH_EXCS)
                res("bh_fdv", f"{m:.17e}|{a:.17e}|{r:.17e}",
                    lambda s=s, r=r: bh_api.equatorial_frame_dragging_velocity(s, r), API_EXCS)
            # Tilted omega rows too (theta != pi/2 path exists in subsystem).
            for th in [0.3, 1.1]:
                res("bh_omega_th", f"{m:.17e}|{a:.17e}|{rp * 2.0 :.17e}|{th:.17e}",
                    lambda s=s, th=th: bh_kerr.frame_dragging_angular_velocity(s, 2.0 * bh_kerr.horizons(s)[0], th),
                    BH_EXCS)

            ergo_eq = bh_kerr.ergosphere_radius(s, HALF_PI)
            for r in [ergo_eq + 1.0, ergo_eq * 1.001, ergo_eq * 3.0,
                      ergo_eq + 1e-10, ergo_eq, float("nan")]:
                res("bh_ktosd", f"{m:.17e}|{a:.17e}|{r:.17e}",
                    lambda s=s, r=r: bh_kerr.static_time_dilation_equatorial(s, r), BH_EXCS)
                res("bh_fdtdil_k", f"{m:.17e}|{a:.17e}|{r:.17e}",
                    lambda s=s, r=r: bh_api.gravitational_time_dilation(s, r), API_EXCS)
            # Facade redshift incl. the Kerr -> Schwarzschild-subsystem delegation band.
            for re_, ro_ in ((ergo_eq * 3.0, ergo_eq * 2.0), (ergo_eq * 0.5, ergo_eq * 3.0)):
                res("bh_z_k", f"{m:.17e}|{a:.17e}|{re_:.17e}|{ro_:.17e}",
                    lambda s=s, re_=re_, ro_=ro_: bh_api.gravitational_redshift(s, re_, ro_),
                    API_EXCS)
            kb = bh_api.get_kerr_boundaries(s)
            emit("bh_kb", f"{m:.17e}|{a:.17e}",
                 (kb["r_plus"], kb["r_minus"], kb["ergosphere_equatorial"], kb["ergosphere_polar"],
                  kb["isco_prograde"], kb["isco_retrograde"],
                  kb["photon_orbit_prograde"], kb["photon_orbit_retrograde"]))


def shared_numerical_field():
    """GENERAL_NUMERICAL test field — identical arithmetic in the C++ checker."""
    def nf(x):
        g = [[0.0] * 4 for _ in range(4)]
        g[0][0] = -(1.0 + 0.5 * x[1])
        g[1][1] = 1.0 + 0.25 * x[1]
        g[2][2] = 1.0 + 0.25 * x[2]
        g[3][3] = 1.0 + 0.125 * x[3]
        g[1][2] = g[2][1] = 0.03125 * x[1] * x[2]
        return g
    return nf


def st_rows() -> None:
    emit("st_event", f"{0.5:.17e}|{1.0:.17e}|{2.0:.17e}|{3.0:.17e}",
         st_api.create_event(0.5, 1.0, 2.0, 3.0).coordinates())
    for t in [0.0, -2.5, float("nan")]:
        res("st_event", f"{t:.17e}|{4.0:.17e}|{0.0:.17e}|{-1.0:.17e}",
            lambda t=t: st_api.create_event(t, 4.0, 0.0, -1.0).coordinates(), ST_EXCS)
    for (x, y, z) in [(1.0, 2.0, 3.0), (-4.0e8, 1.0e9, 2.5e9), (0.0, 0.0, 0.0),
                      (1.0, 0.0, -1.0)]:
        res("st_c2s", f"{x:.17e}|{y:.17e}|{z:.17e}",
            lambda x=x, y=y, z=z: cartesian_to_spherical(x, y, z), ST_EXCS)
    for (r, th, ph) in [(3.0, 1.1, 0.7), (0.0, 1.5, -2.0), (-5.0, 1.0, 1.0)]:
        res("st_s2c", f"{r:.17e}|{th:.17e}|{ph:.17e}",
            lambda r=r, th=th, ph=ph: spherical_to_cartesian(r, th, ph), ST_EXCS)

    rs = 2.9541265550554049e+03  # = bh r_s(M_SUN), computed from the authority
    sm = st_api.schwarzschild_metric(M_SUN)
    km = st_api.kerr_metric(M_SUN, 0.7)
    km0 = st_api.kerr_metric(M_SUN, 0.0)
    mm = st_api.minkowski_metric()
    nf = shared_numerical_field()
    nm = st_api.numerical_metric(nf)

    cases = [
        ("mink", mm, [0.0, 1.0, 2.0, 3.0]),
        ("mink2", mm, [-7.25e10, 1.0e11, -2.0e11, 0.5e11]),
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5]),
        ("schw2", sm, [12.0, 1.0000001 * rs, 0.8, -1.2]),
        ("schw_err1", sm, [0.0, rs, 0.8, 0.0]),           # at horizon
        ("schw_err2", sm, [0.0, 0.0, 0.8, 0.0]),          # r = 0
        ("schw_err3", sm, [0.0, 5.0 * rs, 0.0, 0.0]),     # polar axis
        ("schw_err4", sm, [0.0, 5.0 * rs, math.pi, 0.0]),
        ("kerr", km, [0.0, 10.0 * (rs / 2.0), 1.1, 0.3]),
        ("kerr2", km, [1.0, 100.0 * (rs / 2.0), 0.9, -0.9]),
        ("kerr0", km0, [0.0, 10.0 * (rs / 2.0), 1.3, 0.1]),
        ("num", nm, [10.0, 2.0, 1.5, 3.0]),
        ("num2", nm, [0.0, -4.0, 6.25, -2.5]),
    ]
    for tag, mt, c4 in cases:
        in_str = f"{tag}|" + "|".join(f"{v:.17e}" for v in c4)

        def fl16(f):
            def g():
                t = f()
                return tuple(v for row in t for v in row)
            return g

        res("mt_tensor", in_str, fl16(lambda: mt.tensor(c4)), ST_EXCS)
        res("mt_det", in_str, lambda: mt.determinant(c4), ST_EXCS)
        res("mt_inv", in_str, fl16(lambda: mt.inverse(c4)), ST_EXCS)
        if tag not in ("schw_err1", "schw_err2", "schw_err3", "schw_err4"):
            dg = mt.derivative(c4)
            flat = tuple(v for plane in dg for row in plane for v in row)
            emit("mt_deriv", in_str, flat)
        # Christoffel + derivative + full curvature chain.
        res("mt_gamma", in_str,
            lambda: tuple(v for pl in connection.christoffel_symbols(mt, c4)
                          for rw in pl for v in rw), ST_EXCS)
        res("mt_dgamma", in_str,
            lambda: tuple(v for blk in connection.christoffel_derivative(mt, c4)
                          for pl in blk for rw in pl for v in rw), ST_EXCS)
        res("mt_ricci", in_str, fl16(lambda: curvature.ricci_tensor(mt, c4)), ST_EXCS)
        res("mt_rscalar", in_str, lambda: curvature.ricci_scalar(mt, c4), ST_EXCS)
        res("mt_einstein", in_str, fl16(lambda: curvature.einstein_tensor(mt, c4)), ST_EXCS)
        res("mt_kret", in_str, lambda: curvature.kretschmann_scalar(mt, c4), ST_EXCS)

    # Riemann (256 components) on a representative subset only (CSV weight).
    for tag, mt, c4 in [
        ("mink", mm, [0.0, 1.0, 2.0, 3.0]),
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5]),
        ("kerr", km, [0.0, 10.0 * (rs / 2.0), 1.1, 0.3]),
        ("num", nm, [10.0, 2.0, 1.5, 3.0]),
    ]:
        in_str = f"{tag}|" + "|".join(f"{v:.17e}" for v in c4)
        res("mt_riemann", in_str,
            lambda: tuple(v for bl in curvature.riemann_tensor(mt, c4)
                          for pl in bl for rw in pl for v in rw), ST_EXCS)

    # Tidal operator on a few configurations.
    for tag, mt, c4, u4, x4 in [
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5],
         [1.0e8, 0.0, 0.0, 1.0e7], [100.0, 0.0, -30.0, 0.0]),
        ("kerr", km, [0.0, 10.0 * (rs / 2.0), 1.1, 0.3],
         [1.0e8, 5.0e6, 1.0e-3, 2.0e-4], [10.0, 1.0, -3.0, 2.0]),
        ("mink", mm, [0.0, 1.0, 2.0, 3.0],
         [1.0e8, 1.0e6, 0.0, 0.0], [1.0, 2.0, 3.0, 4.0]),
    ]:
        in_str = f"{tag}|" + "|".join(f"{v:.17e}" for v in c4)
        res("mt_tidal", in_str + "|" + "|".join(f"{v:.17e}" for v in u4) + "|" +
            "|".join(f"{v:.17e}" for v in x4),
            lambda: curvature.tidal_acceleration(mt, c4, u4, x4), ST_EXCS)

    # Causal classification (exact for Minkowski; local linearized elsewhere).
    for tag, mt, a4, b4 in [
        ("mink", mm, [0.0, 0.0, 0.0, 0.0], [2.998e8, 3.0e8, 0.0, 0.0]),
        ("mink", mm, [0.0, 0.0, 0.0, 0.0], [6.0e8, 1.0, 2.0, 3.0]),
        ("mink", mm, [0.0, 5.0, 0.0, 0.0], [0.0 + C, 5.0 + C, 0.0, 0.0]),
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5],
         [1.0e8, 12.0 * rs, THIRD_PI + 0.01, 0.7]),
    ]:
        in_str = f"{tag}|" + "|".join(f"{v:.17e}" for v in a4) + "|" + "|".join(f"{v:.17e}" for v in b4)
        res("st_ds2", in_str, lambda: causality.local_interval(mt, a4, b4), ST_EXCS)
        try:
            emit("st_classify", in_str,
                 causality.classify_interval(mt, a4, b4).value)
        except ST_EXCS as e:
            emit("st_classify", in_str, err_name(e))

    # Null-direction helpers: diagonal metrics ok, Kerr -> NotImplementedError.
    for tag, mt, c4, d3 in [
        ("mink", mm, [0.0, 1.0, 2.0, 3.0], [1.0, 0.5, -0.25]),
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5], [0.9, 0.1, 0.44]),
        ("schw", sm, [0.0, 10.0 * rs, THIRD_PI, 0.5], [0.0, 0.0, 0.0]),   # error row
        ("kerr", km, [0.0, 10.0 * (rs / 2.0), 1.1, 0.3], [1.0, 0.0, 0.0]),
    ]:
        in_str = f"{tag}|" + "|".join(f"{v:.17e}" for v in c4) + "|" + "|".join(f"{v:.17e}" for v in d3)
        try:
            f_, p_ = causality.null_ray_directions(mt, c4, d3)
            emit("st_nullray", in_str, tuple(f_) + tuple(p_))
        except (SpacetimeError, NotImplementedError) as e:
            emit("st_nullray", in_str, err_name(e))

    # cartesian_state_to_chart (metric-correct u^0) incl. violations.
    ev = st_api.create_event(0.0, 10.0 * rs, 0.0, 0.0)
    ev_k = st_api.create_event(0.0, 10.0 * (rs / 2.0), (rs / 2.0), 0.0)
    events_vels = [
        ("mink", mm, ev, Vector3(1.0e7, -2.0e6, 3.0e5), False),
        ("mink", mm, ev, Vector3(0.0, 0.0, C), True),
        ("mink", mm, ev, Vector3(0.0, 0.0, C), False),  # LightSpeedViolation
        ("mink", mm, ev, Vector3(0.0, 0.0, 0.0), True),  # InvalidCoordinateError
        ("schw", sm, ev, Vector3(0.0, 3.0e6, -1.0e5), False),
        ("schw", sm, ev, Vector3(4.0e6, 0.0, 7.0e5), False),
        ("schw", sm, ev, Vector3(0.0, C, 0.0), True),
        ("schw", sm, ev, Vector3(0.0, 0.95 * C, 0.0), False),
        ("kerr", km, ev_k, Vector3(0.0, 3.0e6, 2.0e6), False),
        ("kerr", km, ev_k, Vector3(1.0e7, 0.0, -9.9e5), True),
    ]
    for tag, mt, e_, v_, ml in events_vels:
        in_str = f"{tag}|{e_.ct_m:.17e}|{e_.x:.17e}|{e_.y:.17e}|{e_.z:.17e}|" \
                 f"{v_.x:.17e}|{v_.y:.17e}|{v_.z:.17e}|{1 if ml else 0}"
        res("st_c2c", in_str,
            lambda mt=mt, e_=e_, v_=v_, ml=ml:
            st_api.cartesian_state_to_chart(mt, e_, v_, ml)[0]
            + st_api.cartesian_state_to_chart(mt, e_, v_, ml)[1], API_EXCS)

    # Geodesics: deterministic runs mirrored by the C++ checker incl. guards.
    def geo(mt, c0, u0, limit, steps, adaptive=False):
        try:
            sol = geodesics.integrate_geodesic(mt, c0, u0, limit, steps=steps,
                                               adaptive=adaptive)
            return (float(len(sol.parameters)),
                    sol.final_coordinates[0], sol.final_coordinates[1],
                    sol.final_coordinates[2], sol.final_coordinates[3],
                    sol.four_velocities[-1][0], sol.four_velocities[-1][1],
                    sol.four_velocities[-1][2], sol.four_velocities[-1][3])
        except (SpacetimeError, ValueError) as e:
            return err_name(e)

    r0 = 10.0 * rs
    th0 = THIRD_PI
    ev0 = st_api.create_event(0.0, r0 * math.sin(th0), 0.0, r0 * math.cos(th0))
    c0_s, u0_s = st_api.cartesian_state_to_chart(sm, ev0, Vector3(0.0, 6.0e6, 0.0), False)
    in_geo = "schw|" + "|".join(f"{v:.17e}" for v in c0_s) + "|" + \
             "|".join(f"{v:.17e}" for v in u0_s)
    res("geo_rk4", in_geo + f"|{1.0e-3:.17e}|{40}|{0}",
        lambda: geo(sm, c0_s, u0_s, 1.0e-3, 40, False), ST_EXCS)
    res("geo_rk45", in_geo + f"|{1.0e-3:.17e}|{8}|{1}",
        lambda: geo(sm, c0_s, u0_s, 1.0e-3, 8, True), ST_EXCS)
    res("geo_rk4_long", in_geo + f"|{5.0e-3:.17e}|{200}|{0}",
        lambda: geo(sm, c0_s, u0_s, 5.0e-3, 200, False), ST_EXCS)
    # Radial infall: designed to hit the horizon guard mid-run.
    _, u0_in = st_api.cartesian_state_to_chart(sm, ev0, Vector3(-8.0e7, 0.0, 0.0), False)
    in_geo_in = "schw|" + "|".join(f"{v:.17e}" for v in c0_s) + "|" + \
                "|".join(f"{v:.17e}" for v in u0_in)
    res("geo_err", in_geo_in + f"|{1.0:.17e}|{400}|{0}",
        lambda: geo(sm, c0_s, u0_in, 1.0, 400, False), ST_EXCS)
    # Minkowski straight ray (exact).
    c0_m, u0_m = st_api.cartesian_state_to_chart(mm, st_api.create_event(0.0, 0.0, 0.0, 0.0),
                                                 Vector3(1.5e8, 1.0e8, -5.0e7), False)
    in_geo_m = "mink|" + "|".join(f"{v:.17e}" for v in c0_m) + "|" + \
               "|".join(f"{v:.17e}" for v in u0_m)
    res("geo_rk4", in_geo_m + f"|{2.0:.17e}|{10}|{0}",
        lambda: geo(mm, c0_m, u0_m, 2.0, 10, False), ST_EXCS)
    # Kerr orbit-like run.
    re0 = 10.0 * (rs / 2.0)
    c0_k, u0_k = st_api.cartesian_state_to_chart(km, ev_k, Vector3(0.0, 8.0e6, 1.0e6), False)
    in_geo_k = "kerr|" + "|".join(f"{v:.17e}" for v in c0_k) + "|" + \
               "|".join(f"{v:.17e}" for v in u0_k)
    res("geo_rk4", in_geo_k + f"|{1.0e-3:.17e}|{40}|{0}",
        lambda: geo(km, c0_k, u0_k, 1.0e-3, 40, False), ST_EXCS)
    res("geo_rk45", in_geo_k + f"|{1.0e-3:.17e}|{8}|{1}",
        lambda: geo(km, c0_k, u0_k, 1.0e-3, 8, True), ST_EXCS)
    # Argument-validation rows.
    res("geo_err", in_geo + f"|{-1.0:.17e}|{40}|{0}",
        lambda: geo(sm, c0_s, u0_s, -1.0, 40, False), ST_EXCS)
    res("geo_err", in_geo + f"|{1.0:.17e}|{0}|{0}",
        lambda: geo(sm, c0_s, u0_s, 1.0, 0, False), ST_EXCS)


def main() -> int:
    print("# fn,inputs(|),expected(| or ERR:Name) — blackhole+spacetime authority references")
    bh_rows()
    st_rows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
