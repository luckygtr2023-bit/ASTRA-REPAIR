"""OpenNGC strict row parser → OpenNgcObject frozen records.

Format: semicolon-separated; RA sexagesimal hours "HH:MM:SS.ss",
Dec sexagesimal degrees "±DD:MM:SS.s". Blank → None (NULL stays NULL).
Redshift and RadVel are REAL MEASURED values where present (per-field
Sources codes are preserved in the manifest via the row's raw Sources
string — provenance granularity kept, not flattened away).
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass
from typing import Dict, Optional

from .errors import MalformedCatalogRow

OPENNGC_HEADER: tuple = (
    "Name", "Type", "RA", "Dec", "Const", "MajAx", "MinAx", "PosAng",
    "B-Mag", "V-Mag", "J-Mag", "H-Mag", "K-Mag", "SurfBr", "Hubble", "Pax",
    "Pm-RA", "Pm-Dec", "RadVel", "Redshift", "Cstar U-Mag", "Cstar B-Mag",
    "Cstar V-Mag", "M", "NGC", "IC", "Cstar Names", "Identifiers",
    "Common names", "NED notes", "OpenNGC notes", "Sources",
)

# Object-type enum (stable values; stored in the binary DSO records).
TYPE_CODES = {
    "*": 1, "**": 2, "*Ass": 3, "OCl": 4, "GCl": 5, "Cl+N": 6, "G": 7,
    "GPair": 8, "GTrpl": 9, "GGroup": 10, "PN": 11, "HII": 12, "DrkN": 13,
    "EmN": 14, "Neb": 15, "RfN": 16, "SNR": 17, "Nova": 18, "NonEx": 19,
    "Dup": 20, "Other": 21, "?": 22, "": 0,
}

_RA_RE = re.compile(r"^(\d{2}):(\d{2}):(\d{2}(?:\.\d+)?)$")
_DEC_RE = re.compile(r"^([+-]\d{2}):(\d{2}):(\d{2}(?:\.\d+)?)$")


def parse_ra_hms(text: str, line_no: int) -> float:
    """'HH:MM:SS.ss' → hours; ×15 gives degrees."""
    m = _RA_RE.match(text.strip())
    if not m:
        raise MalformedCatalogRow(line_no, f"RA not HH:MM:SS.ss: {text!r}")
    h, mi, s = int(m.group(1)), int(m.group(2)), float(m.group(3))
    if h >= 24 or mi >= 60 or s >= 60.0:
        raise MalformedCatalogRow(line_no, f"RA components out of range: {text!r}")
    return h + mi / 60.0 + s / 3600.0


def parse_dec_dms(text: str, line_no: int) -> float:
    """'±DD:MM:SS.s' → signed decimal degrees."""
    m = _DEC_RE.match(text.strip())
    if not m:
        raise MalformedCatalogRow(line_no, f"Dec not ±DD:MM:SS.s: {text!r}")
    sign = -1.0 if m.group(1).startswith("-") else 1.0
    d, mi, s = abs(int(m.group(1))), int(m.group(2)), float(m.group(3))
    if d > 90 or mi >= 60 or s >= 60.0 or (d == 90 and (mi or s)):
        raise MalformedCatalogRow(line_no, f"Dec components out of range: {text!r}")
    return sign * (d + mi / 60.0 + s / 3600.0)


@dataclass(frozen=True)
class OpenNgcObject:
    identifier: str           # e.g. "NGC0224" / "IC0001" (authoritative key, required)
    type_code: int            # TYPE_CODES value (unknown → 22 '?'; never guessed)
    ra_deg: float             # degrees (from sexagesimal hours × 15)
    dec_deg: float            # signed degrees
    v_mag: Optional[float] = None
    maj_arcmin: Optional[float] = None
    min_arcmin: Optional[float] = None
    redshift: Optional[float] = None      # REAL measured z where present
    radvel_km_s: Optional[float] = None   # REAL measured radial velocity where present
    hubble: Optional[str] = None          # Hubble type string (galaxies)
    messier: Optional[str] = None         # Messier number as text (e.g. "M031")
    common_name: Optional[str] = None     # first Common names entry
    sources: Optional[str] = None         # per-field source codes, verbatim

    def __post_init__(self):
        if not self.identifier:
            raise MalformedCatalogRow(0, "identifier required")
        if not (0.0 <= self.ra_deg < 360.0):
            raise MalformedCatalogRow(0, f"ra_deg out of range: {self.ra_deg}")
        if not (-90.0 <= self.dec_deg <= 90.0):
            raise MalformedCatalogRow(0, f"dec_deg out of range: {self.dec_deg}")
        for name, v in (("maj_arcmin", self.maj_arcmin), ("min_arcmin", self.min_arcmin)):
            if v is not None and v <= 0.0:
                raise MalformedCatalogRow(0, f"{name} must be positive: {v}")


def _opt_float(row: Dict[str, str], name: str, line: int) -> Optional[float]:
    raw = (row.get(name) or "").strip()
    if raw == "":
        return None
    try:
        v = float(raw)
    except ValueError as e:
        raise MalformedCatalogRow(line, f"field {name!r} not float: {raw!r}") from e
    if math.isnan(v) or math.isinf(v):
        raise MalformedCatalogRow(line, f"field {name!r} non-finite: {raw!r}")
    return v


def _opt_str(row: Dict[str, str], name: str, max_len: int = 256) -> Optional[str]:
    raw = row.get(name)
    if raw is None:
        return None
    raw = " ".join(raw.strip().split())
    if not raw:
        return None
    return raw[:max_len]


def parse_openngc_row(row: Dict[str, str], line_no: int) -> OpenNgcObject:
    keys = set(row.keys())
    if keys != set(OPENNGC_HEADER):
        missing = set(OPENNGC_HEADER) - keys
        extra = keys - set(OPENNGC_HEADER)
        raise MalformedCatalogRow(line_no, f"header shape mismatch (missing {sorted(missing)}, extra {sorted(extra)})")

    identifier = _opt_str(row, "Name")
    if not identifier:
        raise MalformedCatalogRow(line_no, "blank Name")
    raw_ra = (row.get("RA") or "").strip()
    raw_dec = (row.get("Dec") or "").strip()
    if not raw_ra or not raw_dec:
        raise MalformedCatalogRow(line_no, "blank RA/Dec (direction is required)")
    ra_deg = parse_ra_hms(raw_ra, line_no) * 15.0
    dec_deg = parse_dec_dms(raw_dec, line_no)

    type_str = (row.get("Type") or "").strip()
    type_code = TYPE_CODES.get(type_str, TYPE_CODES["?"])

    z = _opt_float(row, "Redshift", line_no)
    if z is not None and z < -1.0:
        raise MalformedCatalogRow(line_no, f"redshift < -1 unphysical: {z}")

    common = _opt_str(row, "Common names", max_len=64)
    if common and "," in common:
        common = common.split(",", 1)[0].strip() or None

    messier = _opt_str(row, "M", max_len=8)
    if messier:
        messier = "M" + messier.zfill(3) if messier.isdigit() else messier

    return OpenNgcObject(
        identifier=identifier,
        type_code=type_code,
        ra_deg=ra_deg,
        dec_deg=dec_deg,
        v_mag=_opt_float(row, "V-Mag", line_no),
        maj_arcmin=_opt_float(row, "MajAx", line_no),
        min_arcmin=_opt_float(row, "MinAx", line_no),
        redshift=z,
        radvel_km_s=_opt_float(row, "RadVel", line_no),
        hubble=_opt_str(row, "Hubble", max_len=16),
        messier=messier,
        common_name=common,
        sources=_opt_str(row, "Sources"),
    )
