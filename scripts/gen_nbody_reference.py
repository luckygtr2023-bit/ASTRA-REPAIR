#!/usr/bin/env python3
"""Generate N-body reference trajectories from the Python scientific authority
(astra.nbody, velocity Verlet) for the ASTRA COSMOS native C++ fidelity gate
(tests/nbody_mirror_check.cpp).

Initial state: byte-for-byte the same JPL element literals and kg masses as
native_renderer/src/app/celestial_sim.cpp::make_solar_system, converted to
heliocentric SI state vectors with the same composed authority primitives as
scripts/gen_kepler_reference.py (Moon = Earth world + geocentric relative,
matching propagate_world in the C++ mirror). The units INSIDE the system are
strict SI (metres, m/s, kg), mirroring to_nbody_state.

Integration: astra.nbody.NBodySystem with G and softening at the authority
defaults (GRAVITATIONAL_CONSTANT=6.67430e-11, DEFAULT_SOFTENING=1e-6 m),
dt = 3600 s fixed (NBodyEngine policy). Logged times are exact multiples of
dt so no partial-step ambiguity exists across languages.

Usage:  python scripts/gen_nbody_reference.py > /tmp/nbody_reference.csv
"""
from __future__ import annotations

import math
import sys

from astra.orbital.kepler import solve_kepler_elliptic
from astra.orbital.anomalies import eccentric_to_true
from astra.orbital.elements import ClassicalOrbitalElements, elements_to_state
from astra.mathematics import Vector3
from astra.nbody import NBodyBody, NBodySystem
from astra.nbody.diagnostics import total_energy
from astra.physics.constants import GRAVITATIONAL_CONSTANT, DEFAULT_SOFTENING

AU_KM = 149_597_870.7
GM_SUN = 1.32712440018e11  # km^3 s^-2
GM_EARTH = 3.986004418e5
D2R = math.pi / 180.0
DT_S = 3600.0


def from_jpl(a_au, e, i_deg, L_deg, varpi_deg, raan_deg):
    return (a_au * AU_KM, e, i_deg * D2R, raan_deg * D2R,
            (varpi_deg - raan_deg) * D2R, (L_deg - varpi_deg) * D2R, GM_SUN)


BODIES = [
    # Sun sits at the frame origin by definition (same convention as
    # celestial_sim.cpp; no Kepler state for a degenerate-element primary).
    ("Sun", None, 1.9885e30),
    ("Mercury", from_jpl(0.387099, 0.205636, 7.0049, 252.251, 77.4575, 48.3309), 3.3011e23),
    ("Venus", from_jpl(0.723336, 0.006777, 3.39468, 181.980, 131.564, 76.6799), 4.8675e24),
    ("Earth", from_jpl(1.000003, 0.016711, -0.00002, 100.464, 102.937, 0.0), 5.9724e24),
    ("Mars", from_jpl(1.523710, 0.093394, 1.84973, -4.55343, -23.9436, 49.5596), 6.4171e23),
    ("Jupiter", from_jpl(5.20289, 0.048386, 1.30440, 34.39644, 14.72848, 100.4739), 1.8982e27),
    ("Saturn", from_jpl(9.53668, 0.053862, 2.48599, 49.95424, 92.59888, 113.6624), 5.6834e26),
    ("Uranus", from_jpl(19.1892, 0.047257, 0.772637, 313.2381, 170.9543, 74.0169), 8.6810e25),
    ("Neptune", from_jpl(30.0699, 0.008590, 1.770042, -55.1200, 44.96476, 131.7841), 1.0241e26),
    ("Moon", (384400.0, 0.0549, 5.145 * D2R, 125.08 * D2R, 318.27 * D2R, 32.02 * D2R, GM_EARTH), 7.3477e22),
]

DAY_S = 86400.0
# 0, 1 day, 7 days, 180 days — all exact multiples of DT_S.
TIMES_S = [0.0, DAY_S, 7 * DAY_S, 180 * DAY_S]


def authoritative_state(a, e, i, raan, argp, M0, mu, t):
    n = math.sqrt(mu / (a ** 3))
    M = M0 + n * t
    E = solve_kepler_elliptic(M, e)
    nu = eccentric_to_true(E, e)
    p = a * (1.0 - e * e)
    coe = ClassicalOrbitalElements(
        semi_latus_rectum=p, eccentricity=e, inclination=i, raan=raan,
        argument_of_periapsis=argp, true_anomaly=nu, mu=mu)
    st = elements_to_state(coe, epoch=0.0)
    return ((st.position.x, st.position.y, st.position.z),
            (st.velocity.x, st.velocity.y, st.velocity.z))


def build_system() -> NBodySystem:
    # Heliocentric world states at t = 0, table order, km/kms then *1e3 to SI.
    world = {}
    bodies = []
    for name, el, mass in BODIES:
        if el is None:  # Sun: degenerate elements at the frame origin
            x, y, z, vx, vy, vz = 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
        else:
            (x, y, z), (vx, vy, vz) = authoritative_state(*el, 0.0)
        if name == "Moon":  # geocentric relative -> heliocentric world
            ex, ey, ez = world["Earth"][:3]
            evx, evy, evz = world["Earth"][3:]
            x, y, z = ex + x, ey + y, ez + z
            vx, vy, vz = evx + vx, evy + vy, evz + vz
        world[name] = (x, y, z, vx, vy, vz)
        bodies.append(NBodyBody(
            id=name, mass=mass,
            position=Vector3(x * 1000.0, y * 1000.0, z * 1000.0),
            velocity=Vector3(vx * 1000.0, vy * 1000.0, vz * 1000.0)))
    return NBodySystem(bodies, G=GRAVITATIONAL_CONSTANT, softening=DEFAULT_SOFTENING)


def main() -> int:
    system = build_system()
    names = [name for name, _, _ in BODIES]
    e0 = total_energy(system.bodies, G=system.G, softening=system.softening)
    print("body,t_s,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms,total_e_j")
    t = 0.0
    logged = 0
    next_i = 0

    def emit(system, t):
        e = total_energy(system.bodies, G=system.G, softening=system.softening)
        for name, b in zip(names, system.bodies):
            print(f"{name},{t:.1f},{b.position.x:.12e},{b.position.y:.12e},"
                  f"{b.position.z:.12e},{b.velocity.x:.12e},{b.velocity.y:.12e},"
                  f"{b.velocity.z:.12e},{e:.12e}")

    emit(system, 0.0)
    next_i = 1
    while next_i < len(TIMES_S):
        target = TIMES_S[next_i]
        while t < target:
            system.step(DT_S, require_authority=False)
            t += DT_S
        emit(system, t)
        next_i += 1
    print(f"# seed_total_e_j,{e0:.12e}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
