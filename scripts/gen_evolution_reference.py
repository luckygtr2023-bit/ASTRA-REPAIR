#!/usr/bin/env python3
"""ASTRA v1.3 — Evolution (domains 2+4) reference generator.

Runs the REAL astra.evolution authority on a fixed case matrix and emits a
CSV reference to stdout. Compared bit-for-bit against the native mirror by
native_renderer/tests/evolution_mirror_check.cpp.

Determinism: no wall clock, no RNG draws in any emitted row (the engine's
rng is never used by evolve_object; the soft wall-clock warning never
raises and affects no output state).

Row families (first field):
  T   timestep controller: T,id,dt,reason | T,id,ERR,class
  S   stellar lifetime/remnant: S,mass,lifetime,remnant | S,mass,ERR,class
  SC  classify transition: SC,mass,age,done,before,after,timescale
  G   galaxy step: G,id,key,value ... (key:values order fixed after ";")
  GM  galaxy step morphology: GM,id,key=value,...
  K   structure step: K,id,kind,key=value,...
  E   epoch classify: E,id,epoch | E,id,ERR,class
  X   engine evolve_object: X,id,final_time,phase,q:key=value,...,
      history_len,last_history_time | X,id,ERR,class
"""
import math
import sys

sys.path.insert(0, ".")

from astra.evolution.config import EvolutionConfig, TimestepPolicy, PerformanceBudget
from astra.evolution.engine import CosmicEvolutionEngine
from astra.evolution.epoch import EpochClassifier, EpochBoundaries, EvolutionEpoch
from astra.evolution.errors import (
    EvolutionAuthorityError, EvolutionLimitationError,
    EvolutionNumericalError, EvolutionValidationError,
)
from astra.evolution.provenance import Provenance, Quantity
from astra.evolution.scenarios import ScenarioRegistry
from astra.evolution.state import EvolutionState
from astra.evolution.stellar import (
    classify_stellar_phase, main_sequence_lifetime_gyr, remnant_for_mass,
)
from astra.evolution.timestep import AdaptiveTimestepController, TimestepReason

OUT = []


def f17(v):
    return "%.17g" % v


def errclass(exc):
    if isinstance(exc, EvolutionAuthorityError):
        return "EvolutionAuthorityError"
    if isinstance(exc, EvolutionLimitationError):
        return "EvolutionLimitationError"
    if isinstance(exc, EvolutionNumericalError):
        return "EvolutionNumericalError"
    if isinstance(exc, EvolutionValidationError):
        return "EvolutionValidationError"
    return type(exc).__name__


# ---------------------------------------------------------------- select_step
POL = TimestepPolicy()
CTL = AdaptiveTimestepController(POL)

TS_CASES = []
# (id, remaining, rate_scale, next_event, current_time)
TS_CASES.append(("max", 5.0, 1.0, None, 0.5))
TS_CASES.append(("rem_under_min", 5e-7, 1.0, None, None))
TS_CASES.append(("rem_zero", 0.0, 1.0, None, None))
TS_CASES.append(("rate_2", 5.0, 2.0, None, 0.25))
TS_CASES.append(("rate_0", 5.0, 0.0, None, 0.25))
TS_CASES.append(("rate_huge", 5.0, 1e15, None, 0.25))
TS_CASES.append(("rate_tiny_floor", 5.0, 1e-15, None, 0.25))
TS_CASES.append(("exact_remaining", 1.0, 1.0, None, 0.0))
TS_CASES.append(("event_abs_far", 5.0, 1.0, 100.0, 1.0))
TS_CASES.append(("event_abs_close", 5.0, 1.0, 1.5, 1.0))
TS_CASES.append(("event_abs_under_min", 5.0, 1.0, 1.0000005, 1.0))
TS_CASES.append(("event_abs_past", 5.0, 1.0, 0.5, 1.0))
TS_CASES.append(("event_nocur_small", 5.0, 1.0, 0.2, None))
TS_CASES.append(("event_nocur_big", 5.0, 1.0, 10.0, None))
TS_CASES.append(("rem_base_limited", 0.05, 1.0, None, 0.0))
TS_CASES.append(("rem_min_boundary", 1e-6, 1.0, None, 0.0))
TS_CASES.append(("neg_remaining", -1.0, 1.0, None, None))
TS_CASES.append(("nan_remaining", float("nan"), 1.0, None, None))
TS_CASES.append(("neg_rate", 5.0, -0.5, None, None))
TS_CASES.append(("inf_rate", 5.0, float("inf"), None, None))
TS_CASES.append(("event_nan", 5.0, 1.0, float("nan"), 1.0))

for cid, rem, rate, nxt, cur in TS_CASES:
    try:
        d = CTL.choose(remaining_gyr=rem, rate_scale=rate, next_event_gyr=nxt,
                       model_id="m", current_cosmic_time_gyr=cur)
        OUT.append(f"T,{cid},{f17(d.dt_gyr)},{d.reason.value}")
    except Exception as e:
        OUT.append(f"T,{cid},ERR,{errclass(e)}")

# ------------------------------------------------------------------- stellar
for mass in (0.3, 0.5, 0.8, 1.0, 2.0, 8.0, 10.0, 25.0, 60.0, 120.0):
    try:
        t = main_sequence_lifetime_gyr(mass)
        OUT.append(f"S,{f17(mass)},{f17(t)},{remnant_for_mass(mass).value}")
    except Exception as e:
        OUT.append(f"S,{f17(mass)},ERR,{errclass(e)}")
for bad in (0.0, -1.0, float("nan"), float("inf")):
    try:
        t = main_sequence_lifetime_gyr(bad)
        OUT.append(f"S,bad,{f17(t)}")
    except Exception as e:
        OUT.append(f"S,bad,ERR,{errclass(e)}")
# 0.5-8.0 dead-code pin: remnant for 0.4, 0.5, 7.9, 8.0
for mass in (0.4, 0.5, 7.9, 8.0, 24.9, 25.0):
    OUT.append(f"SC,{f17(mass)},{f17(1e40)},1,{remnant_for_mass(mass).value}")

for mass, age in [(1.0, 5.0), (1.0, 10.0), (1.0, 10.01), (2.0, 0.55),
                  (2.0, 0.5589), (60.0, 0.05), (0.8, 1e6), (0.3, 80.0)]:
    tr = classify_stellar_phase(mass_msun=mass, age_gyr=age)
    if tr is None:
        OUT.append(f"SC,{f17(mass)},{f17(age)},0,NONE,NONE,0")
    else:
        OUT.append(f"SC,{f17(mass)},{f17(age)},1,{tr.before_phase.value},"
                   f"{tr.after_phase.value},{f17(tr.timescale_gyr)}")


# ------------------------------------------------------------------ galaxy
def mk_galaxy_state(morph=None, gas=1e10, sfr=5.0, mstar=5e10, Z=0.02, L=2e10,
                    mbh=None):
    q = {
        "stellar_mass_msun": Quantity(value=mstar, unit="Msun",
                                      provenance=Provenance.SIMULATED_DATA,
                                      model_id="astra.evolution.galaxy.v1"),
        "gas_mass_msun": Quantity(value=gas, unit="Msun",
                                  provenance=Provenance.SIMULATED_DATA,
                                  model_id="astra.evolution.galaxy.v1"),
        "sfr_msun_per_yr": Quantity(value=sfr, unit="Msun/yr",
                                    provenance=Provenance.SIMULATED_DATA,
                                    model_id="astra.evolution.galaxy.v1"),
        "metallicity": Quantity(value=Z, unit="Z",
                                provenance=Provenance.SIMULATED_DATA,
                                model_id="astra.evolution.galaxy.v1"),
        "luminosity_Lsun": Quantity(value=L, unit="Lsun",
                                    provenance=Provenance.SIMULATED_DATA,
                                    model_id="astra.evolution.galaxy.v1"),
    }
    if mbh is not None:
        q["central_bh_mass_msun"] = Quantity(
            value=mbh, unit="Msun", provenance=Provenance.SIMULATED_DATA,
            model_id="astra.evolution.galaxy.v1")
    return EvolutionState(
        object_id="g-1", cosmic_time_gyr=0.0, phase="SPIRAL",
        quantities=q, model_id="astra.evolution.galaxy.v1",
        provenance=Provenance.SIMULATED_DATA,
        metadata=({"morphology_probs": morph} if morph else {}))


from astra.evolution.galaxy import make_galaxy_step, default_galaxy_model
GMODEL = default_galaxy_model()
GSTEP = make_galaxy_step()

G_CASES = [
    ("basic", mk_galaxy_state(), 0.01),
    ("big_dt", mk_galaxy_state(), 1.0),
    ("gas_rich", mk_galaxy_state(gas=1e12), 0.1),
    ("gas_poor_no_drift", mk_galaxy_state(morph={"SPIRAL": 0.7, "ELLIPTICAL": 0.2, "IRREGULAR": 0.1},
                                          gas=1e12), 0.1),
    ("gas_poor_drift", mk_galaxy_state(morph={"SPIRAL": 0.7, "ELLIPTICAL": 0.2, "IRREGULAR": 0.1},
                                       gas=1e6), 0.1),
    ("drift_max_shift", mk_galaxy_state(morph={"SPIRAL": 0.7, "ELLIPTICAL": 0.2, "IRREGULAR": 0.1},
                                        gas=1e6), 5.0),
    ("drift_no_spiral", mk_galaxy_state(morph={"ELLIPTICAL": 0.9, "IRREGULAR": 0.1},
                                        gas=1e6), 5.0),
    ("zero_gas", mk_galaxy_state(gas=0.0, sfr=2.0), 0.5),
    ("morph_order", mk_galaxy_state(morph={"IRREGULAR": 0.4, "SPIRAL": 0.5, "ELLIPTICAL": 0.1},
                                    gas=1e6), 1.0),
    ("with_bh", mk_galaxy_state(mbh=4.3e6), 0.25),
]
GQ_KEYS = ("stellar_mass_msun", "gas_mass_msun", "sfr_msun_per_yr",
           "metallicity", "luminosity_Lsun", "central_bh_mass_msun")
for cid, st, dt in G_CASES:
    out = GSTEP(st, dt, GMODEL)
    parts = [f"G,{cid}"]
    for k in GQ_KEYS:
        qq = out.quantities.get(k)
        parts.append(k + "=" + (f17(qq.value) if qq is not None else "-"))
    OUT.append(",".join(parts))
    probs = out.metadata.get("morphology_probs")
    if probs is not None:
        mparts = [f"GM,{cid}"]
        for k, v in probs.items():   # insertion order — emit as constructed
            mparts.append(f"{k}={f17(v)}")
        OUT.append(",".join(mparts))

# ------------------------------------------------------------------ structure
from astra.evolution.structure import (
    make_cluster_step, make_cosmic_web_step, make_dark_matter_halo_step,
    make_void_step, default_cluster_model, default_cosmic_web_model,
    default_void_model, default_halo_model,
)


def mk_st(oid, kind, q, time=0.0):
    return EvolutionState(
        object_id=oid, cosmic_time_gyr=time, phase=kind,
        quantities={k: Quantity(value=v, unit="u", provenance=Provenance.SIMULATED_DATA,
                                model_id="m") for k, v in q.items()},
        model_id="m", provenance=Provenance.SIMULATED_DATA)


CM = default_cluster_model()
WM = default_cosmic_web_model()
VM = default_void_model()
HM = default_halo_model()
C_STEP, W_STEP, V_STEP, H_STEP = (make_cluster_step(), make_cosmic_web_step(),
                                  make_void_step(), make_dark_matter_halo_step())

K_CASES = []
cluster0 = mk_st("c-1", "CLUSTER", {"total_mass_msun": 1e14, "member_count": 50.0})
for did, dt in (("c_small", 0.01), ("c_one", 1.0), ("c_three", 3.0), ("c_halfint", 3.9999)):
    r = C_STEP(cluster0, dt, CM)
    K_CASES.append((did, r.cosmic_time_gyr, r.quantities["total_mass_msun"].value,
                    r.quantities["member_count"].value))

web0 = mk_st("w-1", "COSMIC_WEB", {"filament_count": 20.0, "node_count": 10.0, "void_count": 15.0})
for did, dt in (("w_early", 1.0),):
    r = W_STEP(web0, dt, WM)
    K_CASES.append((did, r.cosmic_time_gyr, r.quantities["filament_count"].value,
                    r.quantities["void_count"].value))
web_late = mk_st("w-2", "COSMIC_WEB", {"filament_count": 20.0, "node_count": 10.0, "void_count": 15.0}, time=10.5)
for did, dt in (("w_late", 1.0), ("w_late_gt", 30.0)):
    r = W_STEP(web_late, dt, WM)
    K_CASES.append((did, r.cosmic_time_gyr, r.quantities["filament_count"].value,
                    r.quantities["void_count"].value))

void0 = mk_st("v-1", "VOID", {"radius_mpc": 10.0, "density_contrast": -0.8})
for did, dt in (("v_small", 0.5), ("v_floor", 50.0)):
    r = V_STEP(void0, dt, VM)
    K_CASES.append((did, r.cosmic_time_gyr, r.quantities["radius_mpc"].value,
                    r.quantities["density_contrast"].value))

halo0 = mk_st("h-1", "HALO", {"halo_mass_msun": 1e12, "concentration": 5.0})
for did, dt in (("h_small", 0.1),):
    r = H_STEP(halo0, dt, HM)
    K_CASES.append((did, r.cosmic_time_gyr, r.quantities["halo_mass_msun"].value,
                    r.quantities["concentration"].value))

for did, t, a, b in K_CASES:
    OUT.append(f"K,{did},{f17(t)},{f17(a)},{f17(b)}")

# ------------------------------------------------------------------ epoch
ENGC = CosmicEvolutionEngine()
DEF_B = EpochBoundaries(declining_sfr_threshold=0.1,
                        degenerate_remnant_fraction=0.5,
                        black_hole_dominated_fraction=0.7,
                        dark_era_luminous_fraction=0.01,
                        model_id="astra.evolution.epoch.default")


def ep(cid, sfr=None, remnant=None, bh=None, lum=None, ct=1.0):
    try:
        r = ENGC.advance_epoch(cosmic_time_gyr=ct, sfr_density=sfr,
                               remnant_fraction=remnant, bh_fraction=bh,
                               luminous_fraction=lum, boundaries=DEF_B)
        OUT.append(f"E,{cid},{r.value}")
    except Exception as e:
        OUT.append(f"E,{cid},ERR,{errclass(e)}")


ep("stelli", sfr=0.2, remnant=0.1)
ep("decline", sfr=0.05, remnant=0.4)
ep("degenerate", sfr=0.05, remnant=0.6)
ep("bh", sfr=0.05, remnant=0.9, bh=0.8, lum=0.5)
ep("dark", sfr=0.05, remnant=0.9, bh=0.8, lum=0.005)
ep("dark_only", sfr=0.05, remnant=0.9, bh=None, lum=0.005)
ep("bh_only", sfr=0.05, remnant=0.9, bh=0.8, lum=None)
ep("unknown_sfr", sfr=None, remnant=0.4)
ep("unknown_rem", sfr=0.05, remnant=None)
ep("bad_rem_hi", sfr=0.05, remnant=1.2)
ep("bad_rem_neg", sfr=0.05, remnant=-0.1)
ep("bad_sfr_neg", sfr=-0.5, remnant=0.4)
ep("edge_sfr_at", sfr=0.1, remnant=0.4)         # NOT > threshold -> declining
ep("edge_rem_at", sfr=0.05, remnant=0.5)        # NOT < threshold -> deeper path
ep("edge_bh_at", sfr=0.05, remnant=0.9, bh=0.7, lum=0.5)    # >= -> BH
ep("edge_lum_at", sfr=0.05, remnant=0.9, bh=0.5, lum=0.01)  # NOT < -> degenerate
ep("nan_time", sfr=1.0, remnant=0.1, ct=float("nan"))
ep("bad_bh_frac", sfr=0.05, remnant=0.9, bh=1.5)

# ------------------------------------------------------------------ engine X
def qv(v):
    return Quantity(value=float(v), unit="u", provenance=Provenance.SIMULATED_DATA,
                    model_id="m")


def xcase(cid, oid, model_id, quant, until, phase="MAIN_SEQUENCE", morph=None,
          budget=None, authority=True, start_time=0.0):
    eng = CosmicEvolutionEngine()
    if budget is not None:
        eng = CosmicEvolutionEngine(
            config=EvolutionConfig(budget=budget, timestep=TimestepPolicy()))
    st = EvolutionState(object_id=oid, cosmic_time_gyr=start_time, phase=phase,
                        quantities={k: qv(v) for k, v in quant.items()},
                        model_id=model_id, provenance=Provenance.SIMULATED_DATA,
                        metadata=({"morphology_probs": morph} if morph else {}))

    class _A:
        def require(self, op):
            if not authority:
                from astra.evolution.errors import EvolutionAuthorityError as _EAE
                raise _EAE(f"denied {op}")
    eng.authority = _A()
    try:
        final = eng.evolve_object(initial_state=st, until_cosmic_time_gyr=until,
                                  model_id=model_id,
                                  scenario=eng.scenario_registry.get("baseline_LCDM"))
        keys = sorted(final.quantities.keys())
        kv = ";".join(f"{k}={f17(final.quantities[k].value)}" for k in keys)
        OUT.append(f"X,{cid},{f17(final.cosmic_time_gyr)},{final.phase},{kv},"
                   f"{len(eng.history())},{f17(eng.history()[-1].cosmic_time_gyr)}")
    except Exception as e:
        OUT.append(f"X,{cid},ERR,{errclass(e)}")


xcase("linear_short", "lin-1", "astra.evolution.linear.v1", {}, 0.05)
xcase("linear_unaligned", "lin-2", "astra.evolution.linear.v1", {}, 0.037)
xcase("linear_minrem", "lin-3", "astra.evolution.linear.v1", {}, 5e-7)
xcase("linear_big", "lin-4", "astra.evolution.linear.v1", {}, 5.0)
xcase("linear_zero", "lin-5", "astra.evolution.linear.v1", {}, 0.0)
xcase("stellar_ms", "st-1", "astra.evolution.stellar.v1",
      {"age_gyr": 0.0, "mass_msun": 1.0}, 5.0)
xcase("stellar_xms", "st-2", "astra.evolution.stellar.v1",
      {"age_gyr": 9.5, "mass_msun": 1.0}, 12.0)
xcase("stellar_ms_edge", "st-3", "astra.evolution.stellar.v1",
      {"age_gyr": 9.999999999, "mass_msun": 2.0}, 0.56)
xcase("stellar_nowd", "st-4", "astra.evolution.stellar.v1",
      {"age_gyr": 9.5, "mass_msun": 0.3}, 5.0)
xcase("galaxy_def", "gx-1", "astra.evolution.galaxy.v1", {
      "stellar_mass_msun": 5e10, "gas_mass_msun": 1e10, "sfr_msun_per_yr": 5.0,
      "metallicity": 0.02, "luminosity_Lsun": 2e10}, 2.5, phase="SPIRAL")
xcase("galaxy_drift", "gx-2", "astra.evolution.galaxy.v1", {
      "stellar_mass_msun": 5e10, "gas_mass_msun": 1e6, "sfr_msun_per_yr": 5.0,
      "metallicity": 0.02, "luminosity_Lsun": 2e10}, 2.5, phase="SPIRAL",
      morph={"SPIRAL": 0.7, "ELLIPTICAL": 0.2, "IRREGULAR": 0.1})
xcase("cluster_def", "cl-1", "astra.evolution.cluster.v1",
      {"total_mass_msun": 1e14, "member_count": 50.0}, 3.3, phase="CLUSTER")
xcase("web_def", "wb-1", "astra.evolution.web.v1",
      {"filament_count": 20.0, "node_count": 10.0, "void_count": 15.0}, 12.0,
      phase="COSMIC_WEB", start_time=9.0)
xcase("void_def", "vd-1", "astra.evolution.void.v1",
      {"radius_mpc": 10.0, "density_contrast": -0.8}, 2.2, phase="VOID")
xcase("halo_def", "hl-1", "astra.evolution.halo.v1",
      {"halo_mass_msun": 1e12, "concentration": 5.0}, 1.5, phase="HALO")
xcase("backward", "bw-1", "astra.evolution.linear.v1", {}, -0.1, start_time=0.0)
xcase("unknown_model", "um-1", "astra.evolution.nowhere.v1", {}, 1.0)
xcase("auth_denied", "ad-1", "astra.evolution.linear.v1", {}, 1.0, authority=False)
xcase("hist_cap", "hc-1", "astra.evolution.linear.v1", {}, 0.03,
      budget=PerformanceBudget(max_history_samples_per_object=3))

print("\n".join(OUT))
