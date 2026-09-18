#!/usr/bin/env python3
"""ASTRA v1.3 — authoritative destruction reference rows.

Runs astra.destruction (the SCIENTIFIC AUTHORITY) over a deterministic case
sweep and emits CSV rows for the native mirror checker. Every numeric field
is printed with %.17g (round-trip exact). Errors are row type E with the
Python exception kind name. No fabrication: numbers come only from the
authority's public API.

Row kinds:
  V  validate_impact outcome (kind: OK / error class name)
  G  geometry       (contact/normal/incoming/incidence/grazing/head_on)
  E  energy budget  (5 values; or error kind)
  M  momentum       (9 components)
  D  damage decision(before, after)  — uses intact-target ledger each row
  R  execute_impact result digest    (energy 5, damage, counts, frag/ejecta
                                      positions+velocities+masses — FULL LIST)
  S  secondary sweep digest          (child event fields per child)
  O  orbit classification per listed fragment (class, eps, mu)

Deterministic ordering: cases declared below in source order; row ids are
(case_index, row_index) pairs.
"""
import sys

sys.path.insert(0, ".")

from astra.mathematics import Vector3
from astra.destruction.config import DestructionConfig
from astra.destruction.damage import DamageState
from astra.destruction.errors import DestructionError
from astra.destruction.geometry import compute_impact_geometry
from astra.destruction.energy import compute_impact_energy
from astra.destruction.momentum import compute_impact_momentum
from astra.destruction.types import ImpactEvent
from astra.destruction.system import DestructionSystem
from astra.destruction.analysis import classify_fragment_orbit


def f17(x):
    return "%.17g" % (x,)


def V(x, y, z):
    return Vector3(float(x), float(y), float(z))


# authority provider stub sufficient for the physics path (fail-closed core
# is decorator-less; the AuthorityProvider protocol only needs require()).
class _Auth:
    def require(self, operation):
        return None


CFG = DestructionConfig()
SYS = DestructionSystem(config=CFG, authority=_Auth())


def event(case, impactor_m, target_m, ip, tp, iv, tv, i_rad, t_rad, t_sim=0.0, iid=None):
    return ImpactEvent(
        impact_id=iid or f"imp:{case}",
        impactor_id=f"impactor:{case}",
        target_id=f"target:{case}",
        sim_time_s=t_sim,
        impactor_mass_kg=impactor_m,
        target_mass_kg=target_m,
        impactor_position=V(*ip),
        target_position=V(*tp),
        impactor_velocity=V(*iv),
        target_velocity=V(*tv),
        impactor_radius_m=i_rad,
        target_radius_m=t_rad,
    )


CASES = [
    # (impMass, tgtMass, ipos, tpos, ivel, tvel, i_rad, t_rad, sim_t)
    (1.0e12, 9.3931e20, (-5.0e5, 0, 0), (0, 0, 0), (6500, 0, 0), (0, 0, 0), 0.0, 4.7e5, 600.0),
    (5.0e10, 1.0e15, (-2.0e5, 1.0e4, 0), (0, 0, 0), (1.2e4, -800, 0), (100, 0, 0), 1.0e3, 9.0e4, 12.5),
    (2.0e8, 2.0e8, (-1.0e3, 0, 0), (0, 0, 0), (3.0, 0, 0), (0, 0, 0), 5.0, 5.0, 3.0),
    (1.0, 1.0e3, (-50, 0, 0), (0, 0, 0), (5.0, 0, 0), (0, 0, 0), 1.0, 10.0, 0.0),
    (1.0e6, 1.0e9, (0, 0, 0), (0, 0, 0), (0, 0, 4.0e3), (0, 0, -1.0e3), 0.0, 25.0, 2.0),   # coincident centres
    (4.0e3, 6.0e9, (-80, 3, 0), (0, 0, 0), (2.0, 0.1, 0), (0.01, 0, 0), 0.0, 100.0, 9.0),  # penetrating (d < R)
    (1.0e3, 1.0e3, (-1.0e2, 0, 0), (0, 0, 0), (0.0006, 0, 0), (0, 0, 0), 0.0, 50.0, 0.0),  # below min speed -> invalid
    (1.0e9, 1.0e12, (-1e3, 0, 0), (0, 0, 0), (0.0, 0, 0), (0, 0, 0), 1.0, 2.0, 0.0),       # zero rel velocity
    (7.0e12, 2.0e18, (-3.0e4, 1.5e4, 0), (0, 0, 0), (9.0e3, 0, 0), (0, 0, 0), 100.0, 2.0e4, 33.0),
    (1.0, 1.0, (-0.5, 0, 0), (0, 0, 0), (10.0, 0, 0), (0, 0, 0), 0.0, 0.5, 1.0),           # light bodies, high speed
    (3.0e14, 8.0e14, (-1.0e6, 2.0e5, 3.0e4), (0, 0, 0), (7500, 300, 150), (-50, 10, 0), 2.0e4, 6.0e5, 100.0),
    (9.0e5, 2.0e16, (-4.0e5, 5.0e5, 0), (0, 0, 0), (2.0e3, -1.0e3, 500), (0, 0, 0), 0.0, 1.0e6, 44.0),  # grazing
]


def main() -> None:
    rows = []
    for ci, (im, tm, ip, tp, iv, tv, ir, tr, ts) in enumerate(CASES):
        ev = event(ci, im, tm, ip, tp, iv, tv, ir, tr, t_sim=ts)
        try:
            SYS.validate_impact(ev)
            rows.append(f"V,{ci},OK")
        except Exception as e:  # noqa: BLE001 - record the ERROR KIND exactly
            rows.append(f"V,{ci},{type(e).__name__}")
        try:
            g = compute_impact_geometry(ev, CFG)
            c = g.contact_point.to_tuple()
            n = g.surface_normal.to_tuple()
            d = g.incoming_direction.to_tuple()
            rows.append("G,%d,%s,%s,%s,%s,%s,%s" % (
                ci, ",".join(f17(v) for v in c), ",".join(f17(v) for v in n),
                ",".join(f17(v) for v in d), f17(g.incidence_angle_rad),
                int(g.is_grazing), int(g.is_head_on)))
        except Exception as e:
            rows.append(f"G,{ci},{type(e).__name__}")
        try:
            en = compute_impact_energy(ev, CFG)
            rows.append("E,%d,%s" % (ci, ",".join(f17(v) for v in (
                en.kinetic_energy_j, en.deposited_energy_j, en.fragmentation_energy_j,
                en.thermal_energy_j, en.residual_kinetic_energy_j))))
        except Exception as e:
            rows.append(f"E,{ci},{type(e).__name__}")
        try:
            mo = compute_impact_momentum(ev, CFG)
            rows.append("M,%d,%s,%s,%s" % (
                ci, ",".join(f17(v) for v in mo.relative_momentum_kg_m_s.to_tuple()),
                ",".join(f17(v) for v in mo.transferred_momentum_kg_m_s.to_tuple()),
                ",".join(f17(v) for v in mo.residual_momentum_kg_m_s.to_tuple())))
        except Exception as e:
            rows.append(f"M,{ci},{type(e).__name__}")
        # execute_impact with several seeds (RNG-visible outputs)
        for seed in (101, 2024, 987654321):
            try:
                res = SYS.execute_impact(event(ci, im, tm, ip, tp, iv, tv, ir, tr, t_sim=ts,
                                               iid=f"imp:{ci}:{seed}"), seed=seed)
                run = [f"V,{ci},OKD,{res.target_state_before.value},{res.target_state_after.value}",
                       f"ED,{ci},{seed}," + ",".join(f17(v) for v in (
                           res.energy.kinetic_energy_j, res.energy.deposited_energy_j,
                           res.energy.fragmentation_energy_j, res.energy.thermal_energy_j,
                           res.energy.residual_kinetic_energy_j)),
                       f"RC,{ci},{seed},{len(res.fragments)},{len(res.ejecta)},{len(res.debris)}"]
                for fi, frag in enumerate(res.fragments):
                    run.append("F,%d,%d,%d,%s,%s,%s,%s" % (
                        ci, seed, fi, f17(frag.mass_kg),
                        ",".join(f17(v) for v in frag.position.to_tuple()),
                        ",".join(f17(v) for v in frag.velocity.to_tuple()),
                        f17(frag.created_at_s)))
                for ei, p in enumerate(res.ejecta):
                    run.append("J,%d,%d,%d,%s,%s,%s,%s,%s" % (
                        ci, seed, ei, f17(p.mass_kg),
                        ",".join(f17(v) for v in p.position.to_tuple()),
                        ",".join(f17(v) for v in p.velocity.to_tuple()),
                        f17(p.kinetic_energy_j), f17(p.created_at_s)))
                rows.extend(run)
                # orbit classification of every fragment about the target body
                for fi, frag in enumerate(res.fragments):
                    o = classify_fragment_orbit(
                        frag, central_mass_kg=tm, central_position=V(*tp),
                        central_velocity=V(*tv))
                    rows.append("O,%d,%d,%d,%s,%s,%s" % (ci, seed, fi, o.orbit_class.value,
                                                         f17(o.specific_orbital_energy_j_kg),
                                                         f17(o.mu_m3_s2)))
            except Exception as e:
                rows.append(f"E,{ci},{seed},{type(e).__name__}")

    # Secondary-impact sweep on case 0 fragments vs two caller bodies.
    try:
        # High-energy parent: DESTROYED target -> real fragments -> children.
        parent = SYS.execute_impact(event(950, 1.0e14, 1.0e15, (-1.0e4, 0, 0), (0, 0, 0),
                                          (3.0e5, 0, 0), (0, 0, 0), 0.0, 1.0e5, t_sim=600.0,
                                          iid="imp:sec0"), seed=777)
        targets = [
            event("tA", 1.0, 1.0e18, (0, 0, 0), (2.0e6, 0, 0), (0, 0, 0), (0, 0, 0), 0.0, 1.0e6,
                  iid="cand:tA"),
            event("tB", 1.0, 1.0e19, (0, 0, 0), (0, 8.0e6, 0), (0, 0, 0), (0, 0, 0), 0.0, 2.0e6,
                  iid="cand:tB"),
        ]
        from astra.destruction.types import SecondaryTarget
        cand = [SecondaryTarget(body_id=t.target_id, mass_kg=t.target_mass_kg,
                                position=t.target_position, velocity=t.target_velocity,
                                radius_m=t.target_radius_m) for t in targets]
        children = SYS.execute_secondary_impacts(parent, targets=cand, seed=3000)
        rows.append(f"S,-1,0,{len(children)}")
        for si, ch in enumerate(children):
            e = ch.event
            rows.append("S,%d,%s,%s,%s,%s,%s,%s,%s" % (
                si, e.impact_id, e.impactor_id, e.target_id,
                f17(e.sim_time_s), f17(e.impactor_mass_kg),
                f17(e.target_mass_kg), f17(ch.energy.kinetic_energy_j)))
    except Exception as e:  # record what the authority itself did
        rows.append(f"S,-1,9,{type(e).__name__}")

    # Energy partition boundary: fractions summing to exactly 1.0
    cfg2 = DestructionConfig(fragmentation_energy_fraction=1.0, thermal_energy_fraction=0.0)
    try:
        en = compute_impact_energy(event(900, 2.0e10, 1.0e12, (-1e3, 0, 0), (0, 0, 0),
                                         (2.0e4, 0, 0), (0, 0, 0), 0.0, 0.0), cfg2)
        rows.append("E,900," + ",".join(f17(v) for v in (
            en.kinetic_energy_j, en.deposited_energy_j, en.fragmentation_energy_j,
            en.thermal_energy_j, en.residual_kinetic_energy_j)))
    except Exception as e:
        rows.append(f"E,900,{type(e).__name__}")
    # Config contradiction: cap < 2 with a fractured target -> LimitExceededError
    cfg3 = DestructionConfig(limits=__import__("astra.destruction.config", fromlist=["LimitsConfig"])
                             .LimitsConfig(max_fragments_per_impact=0))
    try:
        SYS2 = DestructionSystem(config=cfg3, authority=_Auth())
        SYS2.execute_impact(event(901, 1.0e14, 1.0e15, (-1e4, 0, 0), (0, 0, 0),
                                  (3.0e5, 0, 0), (0, 0, 0), 0.0, 1e5, iid="imp:901"), seed=5)
        rows.append("V,901,OK")
    except Exception as e:
        rows.append(f"V,901,{type(e).__name__}")

    print("\n".join(rows))


if __name__ == "__main__":
    main()
