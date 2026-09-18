"""Deterministic ingestion + binary export (v1.4 Phases 2 & 4 contract).

Chain of custody (manifest records every hop; tests pin the behavior):
    SOURCE  — sha256-verified raw CSV bytes (registry-pinned).
    DATASET — strict row parse; malformed rows REJECTED (counted + sampled,
              never silently dropped, never partially ingested).
    NORMALIZATION — units (hours→deg, pc→km at load), null policy, upstream
              geometric self-consistency gate (file x,y,z must agree with
              rarad/decrad × dist to 5e-5 relative or the row is rejected:
              the provenance chain must never contain two disagreeing truths).
    EMITTED — deterministic binary (+trailer spectral table): sorted records,
              little-endian, body sha256 in the header. Re-running on the
              same raw input yields byte-identical output (tested).
Determinism: iteration order fixed, no wall-clock values in the binary body.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

from .errors import MalformedCatalogRow, ProvenanceMismatchError
from .hyg import HygStar, parse_hyg_row
from .openngc import OpenNgcObject, parse_openngc_row
from .sources import SOURCES
from .spectra import spectral_temperature_k
from .transform import radec_to_unit

# --- Binary contract (mirrored 1:1 in native_renderer/src/catalog/astro_catalog.h) ---
CATALOG_FORMAT_VERSION = 1
STARS_MAGIC = b"AST14STR"
DSO_MAGIC = b"AST14DSO"
# Star record (little-endian, no padding): x,y,z [pc, f64] ; mag,absmag,
# pmra,pmdec,temp_k,ci [f32; NaN = NOT AVAILABLE] ; spect_key,hip,flags [u32]
# ; hyg_id [u64]. Flags: bit0 distance_unknown (shell placement, CINEMATIC
# scale on a REAL direction), bit1 variable, bit2 has proper name.
STAR_RECORD_FORMAT = "<dddffffffIIIQ"
STAR_RECORD_SIZE = struct.calcsize(STAR_RECORD_FORMAT)  # 68
# DSO record: ux,uy,uz [f64, unit dir from REAL RA/Dec] ; z,vmag,maj_arcmin,
# min_arcmin,dist_proxy_mpc,radvel [f32; NaN = NOT AVAILABLE] ; type_code,
# flags [u32] ; name[24] ascii. Flags: bit0 redshift_present, bit1 named.
# dist_proxy_mpc is DATA_DERIVED (Hubble-law proxy, H0=70 km/s/Mpc — documented
# approximation; NaN when no redshift was measured; renderer HUD shows
# "DATA-DERIVED (H0=70 Hubble proxy)" for it).
DSO_RECORD_FORMAT = "<dddffffffII24s"
DSO_RECORD_SIZE = struct.calcsize(DSO_RECORD_FORMAT)  # 80
_HEADER_FORMAT = "<8sIIQQ32s"  # magic, version, reserved, count, record_size, body sha256
HEADER_SIZE = struct.calcsize(_HEADER_FORMAT)  # 64

# Geometric self-consistency tolerance (relative, per-axis): measured on real
# bytes — p50 1.3e-7, p99 1.5e-5 of distance; threshold sits above p99.5.
XYZ_SELF_CONSISTENCY_REL_TOL = 5.0e-5

HUBBLE_PROXY_H0_KM_S_MPC = 70.0  # documented approximation constant


@dataclass
class RejectedRow:
    line_no: int
    reason: str


@dataclass
class IngestResult:
    source_id: str
    rows_seen: int
    records: List
    rejected: List[RejectedRow]
    excluded: List[Tuple[int, str]] = field(default_factory=list)  # (line_no, label)
    raw_sha256: str = ""
    stats: Dict[str, int] = field(default_factory=dict)

    @property
    def rows_accepted(self) -> int:
        return len(self.records)

    @property
    def rows_rejected(self) -> int:
        return len(self.rejected)

    def rejection_histogram(self) -> Dict[str, int]:
        hist: Dict[str, int] = {}
        for r in self.rejected:
            key = r.reason.split(":", 1)[0][:80]
            hist[key] = hist.get(key, 0) + 1
        return dict(sorted(hist.items()))


def _sha256_of_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _verify_raw(path: str, source_id: str) -> str:
    expected = SOURCES[source_id].raw_sha256
    actual = _sha256_of_file(path)
    if actual != expected:
        raise ProvenanceMismatchError(
            f"{source_id}: raw sha256 mismatch (registry {expected}, file {actual})"
        )
    return actual


def ingest_hyg_csv(path: str) -> IngestResult:
    """Stream-parse the sha256-verified HYG CSV → normalized HygStar list."""
    raw_sha = _verify_raw(path, "HYG_V41")
    result = IngestResult(source_id="HYG_V41", rows_seen=0, records=[], rejected=[], raw_sha256=raw_sha)
    n_dist_unknown = 0
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        line_no = 1
        for row in reader:
            line_no += 1
            result.rows_seen += 1
            # Central-body policy: the catalog contains Sol itself (id 0,
            # dist<=0 — verified census, exactly 1 row). The engine's central
            # body IS Sol; ingesting a second one would fork the truth. The
            # exclusion is explicit and recorded — never silent.
            if (row.get("dist") or "").strip() != "":
                try:
                    if float(row["dist"]) <= 0.0:
                        result.excluded.append((line_no, "central-body: catalog Sol excluded; engine Sun is the authority"))
                        continue
                except ValueError:
                    pass  # parse below rejects with the strict reason
            try:
                star = parse_hyg_row(row, line_no)
            except MalformedCatalogRow as e:
                result.rejected.append(RejectedRow(e.line_no, e.reason))
                continue
            # Upstream geometric self-consistency gate (NORMALIZATION step):
            # file x,y,z must reproduce radec×dist (verified baseline above).
            if star.distance_available:
                # geometry authority is rarad/decrad (validated vs ra/dec in parse)
                rarad = float(row["rarad"]); decrad = float(row["decrad"])
                c = math.cos(decrad)
                u = (c * math.cos(rarad), c * math.sin(rarad), math.sin(decrad))
                delta = max(
                    abs(u[0] * star.dist_pc - star.x_pc),
                    abs(u[1] * star.dist_pc - star.y_pc),
                    abs(u[2] * star.dist_pc - star.z_pc),
                )
                if delta > XYZ_SELF_CONSISTENCY_REL_TOL * star.dist_pc:
                    result.rejected.append(RejectedRow(
                        line_no, f"upstream x,y,z disagree with ra/dec×dist by rel {delta / star.dist_pc:.3g}"))
                    continue
            else:
                n_dist_unknown += 1
            result.records.append(star)
    result.records.sort(key=lambda s: s.hyg_id)
    result.stats = {
        "distance_known": sum(1 for s in result.records if s.distance_available),
        "distance_unknown_shell": n_dist_unknown,
        "excluded_central": len(result.excluded),
        "hp_magnitudes_present": sum(1 for s in result.records if s.mag is not None),
        "proper_motions_present": sum(1 for s in result.records if s.pmra_mas_yr is not None),
    }
    return result


def ingest_openngc_csv(path: str) -> IngestResult:
    raw_sha = _verify_raw(path, "OPENNGC")
    result = IngestResult(source_id="OPENNGC", rows_seen=0, records=[], rejected=[], raw_sha256=raw_sha)
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter=";")
        line_no = 1
        for row in reader:
            line_no += 1
            result.rows_seen += 1
            try:
                obj = parse_openngc_row(row, line_no)
            except MalformedCatalogRow as e:
                result.rejected.append(RejectedRow(e.line_no, e.reason))
                continue
            result.records.append(obj)
    result.records.sort(key=lambda o: o.identifier)
    result.stats = {
        "galaxies": sum(1 for o in result.records if o.type_code in (7, 8, 9, 10)),
        "with_redshift": sum(1 for o in result.records if o.redshift is not None),
        "with_v_mag": sum(1 for o in result.records if o.v_mag is not None),
        "with_angular_size": sum(1 for o in result.records if o.maj_arcmin is not None),
        "messier_objects": sum(1 for o in result.records if o.messier is not None),
    }
    return result


# --- Binary emission -----------------------------------------------------------

def _f32(v: Optional[float]) -> float:
    return float("nan") if v is None else float(v)


def _pack_header(magic: bytes, count: int, record_size: int, body: bytes) -> bytes:
    return struct.pack(_HEADER_FORMAT, magic, CATALOG_FORMAT_VERSION, 0, count,
                       record_size, hashlib.sha256(body).digest())


def _star_body(stars: Sequence[HygStar]) -> bytes:
    # Deterministic spectral table: distinct raw strings, lexicographic order.
    spects = sorted({s.spect for s in stars if s.spect is not None})
    spect_key = {sp: i + 1 for i, sp in enumerate(spects)}  # 0 = NONE

    out = bytearray()
    for s in stars:
        u = radec_to_unit(s.ra_deg % 360.0, s.dec_deg)
        if s.distance_available:
            # recompute from the frozen record's ra/dec doubles — within the
            # 3e-4-deg parse tolerance of the validated rarad geometry; keeps
            # the binary derivation single-sourced from the frozen record.
            x, y, z = (u[0] * s.dist_pc, u[1] * s.dist_pc, u[2] * s.dist_pc)
        else:
            u = radec_to_unit(s.ra_deg % 360.0, s.dec_deg)
            x, y, z = u  # unit direction; flags bit0 marks shell placement
        flags = 0
        if not s.distance_available:
            flags |= 1 << 0
        if s.variable:
            flags |= 1 << 1
        if s.proper is not None:
            flags |= 1 << 2
        temp = spectral_temperature_k(s.spect).temp_k
        out += struct.pack(
            STAR_RECORD_FORMAT,
            x, y, z,
            _f32(s.mag), _f32(s.absmag), _f32(s.pmra_mas_yr), _f32(s.pmdec_mas_yr),
            _f32(temp), _f32(s.ci),
            spect_key.get(s.spect or "", 0), s.hip_id, flags, s.hyg_id,
        )
    trailer = struct.pack("<I", len(spects))
    for sp in spects:
        b = sp.encode("ascii", "strict")
        if len(b) > 255:
            b = b[:255]
        trailer += struct.pack("<B", len(b)) + b
    out += trailer
    return bytes(out)


def _dso_body(dsos: Sequence[OpenNgcObject]) -> bytes:
    out = bytearray()
    for o in dsos:
        u = radec_to_unit(o.ra_deg % 360.0, o.dec_deg)
        flags = 0
        if o.redshift is not None:
            flags |= 1 << 0
        dist_proxy = None
        if o.redshift is not None and o.redshift > 0.0:
            # DATA_DERIVED Hubble-law proxy distance (approximation; NOT a
            # measured distance). v = c*z linear (|v| << c regime documented).
            dist_proxy = (299792.458 * o.redshift) / HUBBLE_PROXY_H0_KM_S_MPC
        name = o.messier or o.common_name or o.identifier
        b = name.encode("ascii", "replace")[:24]
        if o.common_name is not None or o.messier is not None:
            flags |= 1 << 1
        out += struct.pack(
            DSO_RECORD_FORMAT,
            u[0], u[1], u[2],
            _f32(o.redshift), _f32(o.v_mag), _f32(o.maj_arcmin), _f32(o.min_arcmin),
            _f32(dist_proxy), _f32(o.radvel_km_s),
            o.type_code, flags, b,
        )
    return bytes(out)


@dataclass
class EmittedFiles:
    stars_path: str
    dso_path: str
    manifest_path: str
    stars_body_sha256: str
    dso_body_sha256: str
    manifest: Dict


def emit_binary_catalog(stars: Sequence[HygStar], dsos: Sequence[OpenNgcObject],
                        stars_path: str, dso_path: str, manifest_path: str,
                        ingest_stars: IngestResult, ingest_dsos: IngestResult,
                        upstream_notes: Dict[str, str]) -> EmittedFiles:
    """Write the deterministic binary catalog pair + provenance manifest."""
    star_body = _star_body(stars)
    dso_body = _dso_body(dsos)
    stars_bytes = _pack_header(STARS_MAGIC, len(stars), STAR_RECORD_SIZE, star_body) + star_body
    dsos_bytes = _pack_header(DSO_MAGIC, len(dsos), DSO_RECORD_SIZE, dso_body) + dso_body
    with open(stars_path, "wb") as f:
        f.write(stars_bytes)
    with open(dso_path, "wb") as f:
        f.write(dsos_bytes)

    manifest = {
        "format_version": CATALOG_FORMAT_VERSION,
        "deterministic": True,
        "provenance_chain": [
            {"stage": "SOURCE", "files": [
                {"source": "HYG_V41", "url": SOURCES["HYG_V41"].urls[0],
                 "raw_file": SOURCES["HYG_V41"].raw_file, "sha256": ingest_stars.raw_sha256,
                 "license": SOURCES["HYG_V41"].license, "retrieval_route": SOURCES["HYG_V41"].retrieval_route},
                {"source": "OPENNGC", "url": SOURCES["OPENNGC"].urls[0],
                 "raw_file": SOURCES["OPENNGC"].raw_file, "sha256": ingest_dsos.raw_sha256,
                 "license": SOURCES["OPENNGC"].license, "retrieval_route": SOURCES["OPENNGC"].retrieval_route},
            ]},
            {"stage": "NORMALIZATION", "transforms": [
                "ra: decimal hours → degrees (×15)",
                "dist: parsecs; >=100000 → NOT AVAILABLE (shell-only direction, flags bit0)",
                "central-body: catalog Sol row excluded (engine Sun is the authority)",
                f"upstream x,y,z self-consistency gate rel-tol {XYZ_SELF_CONSISTENCY_REL_TOL}",
                "temperature: spectral class → Teff dwarf-sequence table (PHYSICALLY_MODELED, Mamajek 2013-scale)",
                f"DSO dist_proxy_mpc: c·z/{HUBBLE_PROXY_H0_KM_S_MPC} (DATA_DERIVED Hubble proxy, only when redshift measured)",
                "Barnard's-Star-style upstream pm clamps (e.g. pmdec=9999.99) ingested verbatim, never 'fixed'",
            ]},
            {"stage": "EMITTED-COMPUTATIONS", "by": "astra.catalog.pipeline (v1.4)"},
        ],
        "ingest": {
            "HYG_V41": {
                "rows_seen": ingest_stars.rows_seen, "rows_accepted": ingest_stars.rows_accepted,
                "rows_rejected": ingest_stars.rows_rejected,
                "rejection_histogram": ingest_stars.rejection_histogram(),
                "rejected_sample": [f"line {r.line_no}: {r.reason}" for r in ingest_stars.rejected[:25]],
                "excluded": [f"line {ln}: {why}" for ln, why in ingest_stars.excluded],
                "stats": ingest_stars.stats,
            },
            "OPENNGC": {
                "rows_seen": ingest_dsos.rows_seen, "rows_accepted": ingest_dsos.rows_accepted,
                "rows_rejected": ingest_dsos.rows_rejected,
                "rejection_histogram": ingest_dsos.rejection_histogram(),
                "rejected_sample": [f"line {r.line_no}: {r.reason}" for r in ingest_dsos.rejected[:25]],
                "stats": ingest_dsos.stats,
            },
        },
        "outputs": {
            "stars": {"path": stars_path, "records": len(stars), "record_size": STAR_RECORD_SIZE,
                      "body_sha256": hashlib.sha256(star_body).hexdigest()},
            "dsos": {"path": dso_path, "records": len(dsos), "record_size": DSO_RECORD_SIZE,
                     "body_sha256": hashlib.sha256(dso_body).hexdigest()},
        },
        "upstream_notes": upstream_notes,
        "not_available_statements": [
            "per-field formal uncertainties: NOT AVAILABLE in these CSV distributions",
            "Gaia DR3 live TAP / Horizons / NExSScI / SIMBAD: NOT AVAILABLE (BLOCKED — ENVIRONMENT LIMITATION)",
            "transient sources, AGN, FRBs, gravitational-lens events: NOT AVAILABLE (no verified reachable source)",
        ],
    }
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    return EmittedFiles(
        stars_path=stars_path, dso_path=dso_path, manifest_path=manifest_path,
        stars_body_sha256=hashlib.sha256(star_body).hexdigest(),
        dso_body_sha256=hashlib.sha256(dso_body).hexdigest(),
        manifest=manifest,
    )
