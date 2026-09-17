#!/usr/bin/env python3
"""Generate reference positions from the Python scientific authority
(astra.orbital) for the ASTRA COSMOS native C++ fidelity gate.

The native mirror (native_renderer/src/app/celestial_sim.cpp) must reproduce
this math to < 1e-9 relative error. The element VALUES below are byte-for-byte
the same literals as the make_solar_system() table in celestial_sim.cpp
(JPL "Keplerian Elements for Approximate Positions of the Major Planets",
J2000, rounded; Moon = approximate mean elements, geocentric — same parent
convention as the C++ table).

Moon rows are GEOCENTRIC-relative vectors (same convention as
celestial_from_elements / orbital_position in the C++ mirror).

Usage:  python scripts/gen_kepler_reference.py > /tmp/kepler_reference.csv
"""
from __future__ import annotations

import math
import sys

from astra.orbital.kepler import solve_kepler_elliptic
from astra.orbital.anomalies import eccentric_to_true
from astra.orbital.elements import ClassicalOrbitalElements, elements_to_state

AU_KM = 149_597_870.7
GM_SUN = 1.32712440018e11  # km^3 s^-2
GM_EARTH = 3.986004418e5
D2R = math.pi / 180.0

# (name, a_km, e, i, raan, argp, M0, mu) — after the same documented
# transform as celestial_sim.cpp::planet_elements: M0 = L - varpi,
# argp = varpi - raan (angles in radians here).
def from_jpl(a_au, e, i_deg, L_deg, varpi_deg, raan_deg):
    return (a_au * AU_KM, e, i_deg * D2R, raan_deg * D2R,
            (varpi_deg - raan_deg) * D2R, (L_deg - varpi_deg) * D2R, GM_SUN)


BODIES = [
    ("Mercury", from_jpl(0.387099, 0.205636, 7.0049, 252.251, 77.4575, 48.3309)),
    ("Venus",   from_jpl(0.723336, 0.006777, 3.39468, 181.980, 131.564, 76.6799)),
    ("Earth",   from_jpl(1.000003, 0.016711, -0.00002, 100.464, 102.937, 0.0)),
    ("Mars",    from_jpl(1.523710, 0.093394, 1.84973, -4.55343, -23.9436, 49.5596)),
    ("Jupiter", from_jpl(5.20289, 0.048386, 1.30440, 34.39644, 14.72848, 100.4739)),
    ("Saturn",  from_jpl(9.53668, 0.053862, 2.48599, 49.95424, 92.59888, 113.6624)),
    ("Uranus",  from_jpl(19.1892, 0.047257, 0.772637, 313.2381, 170.9543, 74.0169)),
    ("Neptune", from_jpl(30.0699, 0.008590, 1.770042, -55.1200, 44.96476, 131.7841)),
    # Moon: mean elements, geocentric, mu = GM_EARTH (ties to GM_EARTH in C++).
    ("Moon", (384400.0, 0.0549, 5.145 * D2R, 125.08 * D2R,
              318.27 * D2R, 32.02 * D2R, GM_EARTH)),
]

DAY_S = 86400.0
TIMES_S = [0.0, 40_000.0, DAY_S, 0.1 * 365.25 * DAY_S, 365.25 * DAY_S]


def authoritative_state(a: float, e: float, i: float, raan: float, argp: float,
                        M0: float, mu: float, t: float):
    """Full state (position AND velocity) composed entirely from the Python
    authority: kepler solve -> true anomaly -> ClassicalOrbitalElements ->
    elements_to_state. No hand-derived formulas here on purpose."""
    n = math.sqrt(mu / (a ** 3))  # mean motion (n = sqrt(mu/a^3))
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


def main() -> int:
    print("body,t_s,x_km,y_km,z_km,vx_kms,vy_kms,vz_kms")
    for name, (a, e, i, raan, argp, M0, mu) in BODIES:
        for t in TIMES_S:
            (x, y, z), (vx, vy, vz) = authoritative_state(a, e, i, raan, argp, M0, mu, t)
            print(f"{name},{t:.1f},{x:.12e},{y:.12e},{z:.12e},{vx:.12e},{vy:.12e},{vz:.12e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
