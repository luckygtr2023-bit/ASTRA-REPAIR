"""HYG v4.1 strict row parser → HygStar frozen records.

Real-data normalization policy (from the upstream README, quoted in
sources.HYG_V41):
  * `ra` is DECIMAL HOURS (0..24) — verified empirically (Sirius ra≈6.752481).
  * `dist` >= 100000 means missing/dubious parallax → dist_pc := None,
    distance_available=False. The file's x,y,z for those rows are a 100 kpc
    shell placeholder (verified: row id 13 at ~100 kpc) and are REJECTED.
  * Blank numerics → Python None (NULL stays NULL; never 0).
  * `pmdec` for Barnard's Star reads exactly 9999.99 in v4.1: an upstream
    clamp of the true +10327 mas/yr. We ingest the file truth as-is and do
    NOT "fix" it (the raw bytes are the authority; the clamp is documented
    in the export manifest).
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, Optional

from .errors import MalformedCatalogRow

HYG_HEADER: tuple = (
    "id", "hip", "hd", "hr", "gl", "bf", "proper", "ra", "dec", "dist",
    "pmra", "pmdec", "rv", "mag", "absmag", "spect", "ci", "x", "y", "z",
    "vx", "vy", "vz", "rarad", "decrad", "pmrarad", "pmdecrad", "bayer",
    "flam", "con", "comp", "comp_primary", "base", "lum", "var", "var_min",
    "var_max",
)

DIST_UNKNOWN_SENTINEL_PC = 1.0e5  # README: "A value >= 100000 indicates missing or dubious"


@dataclass(frozen=True)
class HygStar:
    hyg_id: int                 # authoritative catalog id (required)
    ra_deg: float               # decimal HOURS × 15 → degrees [0,360)
    dec_deg: float              # decimal degrees [-90,90]
    dist_pc: Optional[float]    # None → distance NOT AVAILABLE
    distance_available: bool
    mag: Optional[float]        # apparent V magnitude
    absmag: Optional[float]     # absolute V magnitude
    pmra_mas_yr: Optional[float]
    pmdec_mas_yr: Optional[float]
    rv_km_s: Optional[float]
    spect: Optional[str]        # raw spectral type string, verbatim
    ci: Optional[float]
    lum_solar: Optional[float]
    hip_id: int = 0             # 0 → none
    proper: Optional[str] = None
    con: Optional[str] = None
    variable: bool = False
    var_min: Optional[float] = None
    var_max: Optional[float] = None
    x_pc: Optional[float] = None  # ICRS-aligned Cartesian position, parsecs
    y_pc: Optional[float] = None  # None for distance-unknown rows (shell-only)
    z_pc: Optional[float] = None

    def __post_init__(self):
        if self.hyg_id <= 0:
            raise MalformedCatalogRow(0, f"hyg id must be positive int (got {self.hyg_id!r})")
        if not (0.0 <= self.ra_deg < 360.0):
            raise MalformedCatalogRow(0, f"ra_deg out of range: {self.ra_deg}")
        if not (-90.0 <= self.dec_deg <= 90.0):
            raise MalformedCatalogRow(0, f"dec_deg out of range: {self.dec_deg}")
        if self.distance_available != (self.dist_pc is not None):
            raise MalformedCatalogRow(0, "distance_available/dist_pc inconsistent")
        if self.dist_pc is not None and (self.dist_pc <= 0.0 or self.dist_pc >= DIST_UNKNOWN_SENTINEL_PC):
            raise MalformedCatalogRow(0, f"dist_pc sentinel/unphysical: {self.dist_pc}")
        # echo the honesty contract: unknown distance ⇒ NEVER position geometry
        if self.dist_pc is None and (self.x_pc is not None or self.y_pc is not None or self.z_pc is not None):
            raise MalformedCatalogRow(0, "distance-unknown row must not carry x,y,z geometry")


def _req_float(row: Dict[str, str], name: str, line: int) -> float:
    raw = row.get(name, "")
    if raw is None or raw.strip() == "":
        raise MalformedCatalogRow(line, f"required field {name!r} blank")
    try:
        v = float(raw)
    except ValueError as e:
        raise MalformedCatalogRow(line, f"required field {name!r} not float: {raw!r}") from e
    if math.isnan(v) or math.isinf(v):
        raise MalformedCatalogRow(line, f"required field {name!r} non-finite: {raw!r}")
    return v


def _opt_float(row: Dict[str, str], name: str, line: int) -> Optional[float]:
    raw = row.get(name, "")
    if raw is None or raw.strip() == "":
        return None
    try:
        v = float(raw)
    except ValueError as e:
        raise MalformedCatalogRow(line, f"optional field {name!r} not float: {raw!r}") from e
    if math.isnan(v) or math.isinf(v):
        raise MalformedCatalogRow(line, f"optional field {name!r} non-finite: {raw!r}")
    return v


def _opt_int(row: Dict[str, str], name: str, line: int) -> int:
    raw = (row.get(name) or "").strip()
    if raw == "":
        return 0
    try:
        return int(raw)
    except ValueError as e:
        raise MalformedCatalogRow(line, f"optional field {name!r} not int: {raw!r}") from e


def _opt_str(row: Dict[str, str], name: str) -> Optional[str]:
    raw = row.get(name)
    if raw is None:
        return None
    raw = raw.strip()
    return raw if raw else None


def parse_hyg_row(row: Dict[str, str], line_no: int) -> HygStar:
    """Strict validation of ONE csv.DictReader row → frozen HygStar.

    All-or-nothing: any violation raises MalformedCatalogRow; the caller
    records the rejection (visible in the export manifest) — the row never
    partially enters the simulation.
    """
    keys = set(row.keys())
    if keys != set(HYG_HEADER):
        missing = set(HYG_HEADER) - keys
        extra = keys - set(HYG_HEADER)
        raise MalformedCatalogRow(line_no, f"header shape mismatch (missing {sorted(missing)}, extra {sorted(extra)})")

    hid = _opt_int(row, "id", line_no)
    if hid <= 0:
        raise MalformedCatalogRow(line_no, f"non-positive hyg id: {hid}")

    ra_hours = _req_float(row, "ra", line_no)
    if not (0.0 <= ra_hours < 24.0):
        raise MalformedCatalogRow(line_no, f"ra hours out of range: {ra_hours}")
    dec_deg = _req_float(row, "dec", line_no)

    raw_dist = _req_float(row, "dist", line_no)
    if raw_dist <= 0.0:
        # Verified census: only "Sol" (id 0, x=5e-6 pc) legitimately carries dist<=0
        # — the engine's central star. The pipeline excludes it BEFORE parse;
        # anything else here is unphysical data → reject loudly.
        raise MalformedCatalogRow(line_no, f"dist <= 0 (unphysical per README): {raw_dist}")
    # rarad/decrad carry the catalog's highest-precision direction; geometry
    # must be derived from them (display RA stays the hours-based degrees).
    rarad = _req_float(row, "rarad", line_no)
    decrad = _req_float(row, "decrad", line_no)
    ra_deg = ra_hours * 15.0
    dec_deg = dec_deg  # (validated above)
    if abs(ra_deg - math.degrees(rarad)) > 3.0e-4:
        raise MalformedCatalogRow(line_no, "ra (hours) and rarad disagree > 3e-4 deg")
    if abs(dec_deg - math.degrees(decrad)) > 3.0e-4:
        raise MalformedCatalogRow(line_no, "dec and decrad disagree > 3e-4 deg")
    distance_available = raw_dist < DIST_UNKNOWN_SENTINEL_PC
    dist_pc = raw_dist if distance_available else None

    pmra = _opt_float(row, "pmra", line_no)
    pmdec = _opt_float(row, "pmdec", line_no)
    rv = _opt_float(row, "rv", line_no)
    mag = _opt_float(row, "mag", line_no)
    absmag = _opt_float(row, "absmag", line_no)
    spect = _opt_str(row, "spect")
    ci = _opt_float(row, "ci", line_no)
    lum = _opt_float(row, "lum", line_no)
    if lum is not None and lum < 0.0:
        raise MalformedCatalogRow(line_no, f"lum negative: {lum}")

    var_flag = _opt_str(row, "var") is not None
    var_min = _opt_float(row, "var_min", line_no)
    var_max = _opt_float(row, "var_max", line_no)

    # Cartesian columns: accepted ONLY for distance-known rows; policy-checked
    # against our own recomputation downstream (see pipeline ingest + tests —
    # the file's x,y,z must agree with ra/dec/dist to 1e-9 pc or the row is
    # rejected: the provenance chain must never contain two disagreeing truths).
    x = y = z = None
    if distance_available:
        x = _req_float(row, "x", line_no)
        y = _req_float(row, "y", line_no)
        z = _req_float(row, "z", line_no)

    return HygStar(
        hyg_id=hid,
        ra_deg=ra_hours * 15.0,
        dec_deg=dec_deg,
        dist_pc=dist_pc,
        distance_available=distance_available,
        mag=mag,
        absmag=absmag,
        pmra_mas_yr=pmra,
        pmdec_mas_yr=pmdec,
        rv_km_s=rv,
        spect=spect,
        ci=ci,
        lum_solar=lum,
        hip_id=_opt_int(row, "hip", line_no),
        proper=_opt_str(row, "proper"),
        con=_opt_str(row, "con"),
        variable=var_flag,
        var_min=var_min,
        var_max=var_max,
        x_pc=x, y_pc=y, z_pc=z,
    )
