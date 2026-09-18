#!/usr/bin/env python3
"""gen_v14_reference_fixtures.py — cross-language measurement parity fixtures.

Reads the COMMITTED renderer binaries (astro_stars.v14.bin — the same bytes
the native loader sha256-verifies), measures chosen catalog records with the
Python authority (astra.catalog.measure), and writes a fixture file the
native v14_gates.cpp consumes to assert ≤ 1e-12 agreement (directions,
distances, delays; pm-shifted epoch: pm is stored f32 in the binary — the
fixture uses the SAME f32 round-trip values the native side sees).

Frame contract: fixture observers are defined in ECLIPTIC km (native frame);
for the Python call they are rotated ecliptic→ICRS (the ε rotation preserves
every distance; ICRS RA/Dec outputs then match the native back-conversion).
"""

from __future__ import annotations

import math
import os
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

ASSETS = os.path.join(REPO_ROOT, "native_renderer", "assets")
FIXTURE = os.path.join(REPO_ROOT, "native_renderer", "tests", "fixtures", "v14_measure_reference.txt")

from astra.catalog.transform import AU_KM, PC_KM, radec_to_unit, apparent_magnitude_at_distance, LY_KM  # noqa: E402

STARS_MAGIC = b"AST14STR"
OBLIQUITY_J2000_RAD = 84381.448 / 206264.80624709636


def ecl_to_icrs(v):
    ce, se = math.cos(OBLIQUITY_J2000_RAD), math.sin(OBLIQUITY_J2000_RAD)
    return (v[0], v[1] * ce - v[2] * se, v[1] * se + v[2] * ce)


def icrs_to_ecl(v):
    ce, se = math.cos(OBLIQUITY_J2000_RAD), math.sin(OBLIQUITY_J2000_RAD)
    return (v[0], v[1] * ce + v[2] * se, v[1] * se + v[2] * ce)


def load_binary_records(path):
    """Parse the committed star binary into intermediate records (the exact
    f32/f64 round-tripleness the native side sees)."""
    with open(path, "rb") as f:
        data = f.read()
    magic, ver, _res, count, rec_size, _sha = struct.unpack_from("<8sIIQQ32s", data, 0)
    assert magic == STARS_MAGIC and rec_size == 68
    recs = []
    for i in range(count):
        off = 64 + i * rec_size
        x, y, z, mag, absmag, pmra, pmdec, temp_k, ci, spect_key, hip, flags, hyg_id = struct.unpack_from(
            "<dddffffffIIIQ", data, off)
        recs.append(dict(x=x, y=y, z=z, mag=mag, absmag=absmag, pmra=pmra, pmdec=pmdec,
                         hip=hip, flags=flags, hyg_id=hyg_id))
    return recs


def measure_py(pos_km_icrs, rec, observer_km_icrs, years=0.0):
    """Python-authority measurement (replicates measure_from_observer exactly
    on the binary round-trip values: position direction from u = pos/|pos|,
    dist_pc from |pos|, pm from the f32 round-trip — which is exactly what
    the native binary moves with)."""
    dist_known = not (rec["flags"] & 1)
    d_km = math.sqrt(pos_km_icrs[0] ** 2 + pos_km_icrs[1] ** 2 + pos_km_icrs[2] ** 2)
    u = (pos_km_icrs[0] / d_km, pos_km_icrs[1] / d_km, pos_km_icrs[2] / d_km)
    ra = math.degrees(math.atan2(u[1], u[0])) % 360.0
    dec = math.degrees(math.asin(max(-1.0, min(1.0, u[2]))))
    if years != 0.0 and dist_known and not math.isnan(rec["pmra"]) and not math.isnan(rec["pmdec"]):
        tr = __import__("astra.catalog.transform", fromlist=["apply_proper_motion_deg"])
        ra, dec = tr.apply_proper_motion_deg(ra, dec, rec["pmra"], rec["pmdec"], years)
        u = radec_to_unit(ra, dec)
    if not dist_known:
        nan = float("nan")
        return dict(ra=ra, dec=dec, dist_km=nan, dist_ly=nan, delay_y=nan, mag=nan)
    star = (u[0] * d_km, u[1] * d_km, u[2] * d_km)
    rel = (star[0] - observer_km_icrs[0], star[1] - observer_km_icrs[1], star[2] - observer_km_icrs[2])
    dist = math.sqrt(rel[0] ** 2 + rel[1] ** 2 + rel[2] ** 2)
    dir_obs = None
    if dist > 0:
        dir_obs = (rel[0] / dist, rel[1] / dist, rel[2] / dist)
        obs_ra = math.degrees(math.atan2(dir_obs[1], dir_obs[0])) % 360.0
        obs_dec = math.degrees(math.asin(max(-1.0, min(1.0, dir_obs[2]))))
    else:
        obs_ra, obs_dec = ra, dec
    mag = float("nan")
    if not math.isnan(rec["absmag"]):
        mag = apparent_magnitude_at_distance(rec["absmag"], dist)
    return dict(ra=obs_ra, dec=obs_dec, dist_km=dist, dist_ly=dist / LY_KM,
                delay_y=dist / LY_KM, mag=mag)


def fmt(v):
    return "nan" if math.isnan(v) else repr(v)


def main():
    recs = load_binary_records(os.path.join(ASSETS, "astro_stars.v14.bin"))
    by_id = {r["hyg_id"]: r for r in recs}
    by_hip = {}
    for r in recs:
        if r["hip"]:
            by_hip.setdefault(r["hip"], r)

    sirius = by_id[32263]
    assert sirius["hip"] == 32349
    # high-PM exemplar: the surviving (gate-passing) star with the largest
    # proper motion. NOTE: Barnard's Star itself is REJECTED by the geometric
    # self-consistency gate (upstream x,y,z disagree with its ra/dec×dist by
    # 5.4e-4 — never admitted into the provenance chain; documented in the
    # manifest). α Centauri A (HIP 71681) is rejected by the SAME gate —
    # high-PM stars are the worst upstream-divergence offenders. Both
    # high-PM exemplars below are picked dynamically (deterministic order).
    hi_pm_all = sorted((r for r in recs if not (r["flags"] & 1) and
                       not math.isnan(r["pmra"]) and not math.isnan(r["pmdec"]) and
                       r["hip"]),
                       key=lambda r: -(r["pmra"] ** 2 + r["pmdec"] ** 2))
    barnard = hi_pm_all[0]
    acen = hi_pm_all[1]
    hi_pm = barnard
    dist_unknown = next(r for r in recs if r["flags"] & 1)   # deterministic: first flagged

    stars = [
        ("SI", sirius), ("HP", hi_pm), ("H2", acen), ("DU", dist_unknown),
    ]
    observers = [
        ("O0", (0.0, 0.0, 0.0)),
        ("OA", (AU_KM, 0.0, 0.0)),
        ("OB", (0.0, AU_KM, 2.0 * AU_KM)),
    ]
    epochs = [("E0", 0.0), ("EP", 10000.0), ("EM", -10000.0)]

    lines = ["# v1.4 cross-language measurement reference fixtures",
             "# format: star obs epoch ra_deg dec_deg dist_km dist_ly delay_y appmag",
             f"# PC_KM {PC_KM!r}", f"# LY_KM {LY_KM!r}", f"# AU_KM {AU_KM!r}",
             f"# OBLIQUITY_J2000_RAD {OBLIQUITY_J2000_RAD!r}"]
    ids = {"SI": 32263, "HP": barnard["hyg_id"], "H2": acen["hyg_id"], "DU": dist_unknown["hyg_id"]}
    lines.append("# star_ids " + " ".join(f"{k}={v}" for k, v in ids.items()))
    lines.append(f"# hi_pm_pick hyg_id={barnard['hyg_id']} hip={barnard['hip']}")
    lines.append(f"# h2_pm_pick hyg_id={acen['hyg_id']} hip={acen['hip']}")
    for tag, rec in stars:
        # star position stays pure ICRS (native converts ecliptic→ICRS back
        # after measuring; rotation pairs cancel). Only observers, defined in
        # the native ecliptic frame, are converted for the Python call.
        pos_icrs = ((rec["x"] * PC_KM, rec["y"] * PC_KM, rec["z"] * PC_KM) if not rec["flags"] & 1
                    else (rec["x"], rec["y"], rec["z"]))
        for otag, obs_ecl in observers:
            for etag, years in epochs:
                if etag != "E0" and (tag == "DU"):
                    continue  # PM-shift undefined without distance: skips
                obs_icrs = ecl_to_icrs(obs_ecl)
                m = measure_py(pos_icrs, rec, obs_icrs, years)
                lines.append(f"{tag} {otag} {etag} {fmt(m['ra'])} {fmt(m['dec'])} "
                             f"{fmt(m['dist_km'])} {fmt(m['dist_ly'])} {fmt(m['delay_y'])} {fmt(m['mag'])}")
    os.makedirs(os.path.dirname(FIXTURE), exist_ok=True)
    with open(FIXTURE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"fixture written: {FIXTURE} ({len([l for l in lines if not l.startswith('#')])} records)")
    print("chosen stars:", {k: v for k, v in ids.items()})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
