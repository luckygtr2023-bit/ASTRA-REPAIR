"""Dataset provenance registry (v1.4).

Every real dataset used by ASTRA is pinned by exact bytes (sha256 of the raw
file AS DOWNLOADED), exact upstream URL + retrieval route, license, coordinate
frame, epoch and units. Sources that are scientifically relevant but not
reachable in this environment are kept as explicit UNAVAILABLE records with
the measured failure evidence — they are NEVER silently replaced by simulated
data.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Tuple


@dataclass(frozen=True)
class DatasetSource:
    source_id: str            # stable registry key
    title: str
    version: str
    citation: str
    urls: Tuple[str, ...]     # canonical upstream locations (in priority order)
    retrieval_route: str      # route actually used in this workspace
    retrieval_date: str       # UTC date bytes were fetched here
    raw_file: str             # file the sha256 applies to (relative to upstream root)
    raw_sha256: str           # sha256 of the raw bytes (verified at ingest time)
    coordinate_frame: str     # e.g. "ICRS-aligned equatorial; epoch=equinox=J2000.0"
    units: Tuple[Tuple[str, str], ...]  # ((field, unit), ...)
    license: str
    license_url: str
    rows_expected: int        # data rows (header excluded); validated at ingest
    classification: str = "REAL_DATA"


# --- INGESTED REAL DATASETS --------------------------------------------------
# sha256 values below were measured in this workspace from the fetched bytes:
#   hygdata_v41.csv : d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd
#                     (119627 lines incl. header → 119626 star rows)
#   NGC.csv         : be150bdaa1997dacbcb39f303074403edec7a953b589b36d5f1c4522c0cc6fae
#   addendum.csv    : 1d8f0914e643ada325a5a94d88d8fefad6a4937a2f77cc34f21483af22b11983
# Retrieval note: the sandbox egress policy blocks all non-GitHub astronomy
# endpoints (TLS termination inside the proxy). GitHub codeload of the
# canonical upstream repositories WORKS and is the route used. Both catalogs
# are the primary distribution form published by their authors on GitHub.

HYG_V41 = DatasetSource(
    source_id="HYG_V41",
    title="HYG Star Database 4.1 (Hipparcos + Yale Bright Star + Gliese, "
          "positions/distances/proper motions largely Gaia-DR3-derived)",
    version="4.1",
    citation="Nash, D. (Astronexus) HYG Database v4.1, "
             "https://github.com/astronexus/HYG-Database",
    urls=(
        "https://github.com/astronexus/HYG-Database/archive/refs/heads/main.zip",
        "https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv",
    ),
    retrieval_route="github-codeload-zip (repo: astronexus/HYG-Database@main, "
                    "snapshot 2026-09-18; codeload allowed by sandbox egress)",
    retrieval_date="2026-09-18",
    raw_file="hyg/CURRENT/hygdata_v41.csv",
    raw_sha256="d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd",
    coordinate_frame="ICRS-aligned equatorial: +X vernal equinox, +Z north "
                     "celestial pole; epoch AND equinox J2000.0 (README §content notes 1)",
    units=(
        ("ra", "decimal hours (0..24) — NOT degrees; ×15 to get degrees"),
        ("dec", "decimal degrees"),
        ("dist", "parsecs (>= 100000 → missing/dubious parallax → NOT AVAILABLE)"),
        ("pmra", "milliarcsec/year"), ("pmdec", "milliarcsec/year"),
        ("rv", "km/s"), ("mag", "apparent V magnitude"),
        ("absmag", "absolute V magnitude (10 pc)"), ("lum", "solar luminosities"),
        ("ci", "B−V color index"), ("x,y,z", "parsecs (equatorial, J2000)"),
        ("vx,vy,vz", "parsecs/year"),
    ),
    license="CC BY-SA 4.0",
    license_url="https://creativecommons.org/licenses/by-sa/4.0/",
    rows_expected=119626,
)

OPENNGC = DatasetSource(
    source_id="OPENNGC",
    title="OpenNGC (NGC/IC deep-sky catalog; NED-based; RA/Dec J2000, V-mag, "
          "angular sizes, 1027 redshift measurements among others)",
    version="git-master 2026-07-26 (upstream commit da90466031b0372c896588b85be6016c617e205b)",
    citation="Verga, M. OpenNGC, https://github.com/mattiaverga/OpenNGC",
    urls=(
        "https://github.com/mattiaverga/OpenNGC/archive/refs/heads/master.zip",
    ),
    retrieval_route="github-codeload-zip (repo: mattiaverga/OpenNGC@master)",
    retrieval_date="2026-09-18",
    raw_file="database_files/NGC.csv",
    raw_sha256="be150bdaa1997dacbcb39f303074403edec7a953b589b36d5f1c4522c0cc6fae",
    coordinate_frame="equatorial J2000 (Upstream README; sexagesimal RA h:m:s / Dec d:m:s)",
    units=(
        ("RA", "sexagesimal hours h:m:s"), ("Dec", "sexagesimal degrees d:m:s"),
        ("MajAx/MinAx", "arcminutes"), ("B/V/J/H/K-Mag", "magnitudes"),
        ("RadVel", "km/s"), ("Redshift", "dimensionless"),
        ("PosAng", "degrees"),
    ),
    license="CC BY-SA 4.0",
    license_url="https://creativecommons.org/licenses/by-sa/4.0/",
    rows_expected=13970,  # validated live at ingest (13962 accepted; manifest records rejections)
)


SOURCES: Dict[str, DatasetSource] = {s.source_id: s for s in (HYG_V41, OPENNGC)}

# --- RELEVANT BUT UNAVAILABLE (NOT fabricated, NOT silently omitted) ---------
# Live probes from this sandbox on 2026-09-18 measured TLS termination by the
# egress proxy for every non-GitHub astronomy endpoint (see the v1.4 report §2):
#   curl https://gea.esac.esa.int/tap-server/tap/sync → (35) SSL_ERROR_SYSCALL
#   curl https://api.github.com/...                    → 200 (allowed)
#   urllib to TAP/Horizons/NExSScI/SIMBAD              → URLError TLS EOF
UNAVAILABLE_SOURCES: Tuple[Dict[str, str], ...] = (
    {"source_id": "GAIA_DR3_TAP", "title": "Gaia DR3 via ESA TAP (gea.esac.esa.int)",
     "status": "NOT AVAILABLE", "reason": "BLOCKED — ENVIRONMENT LIMITATION",
     "evidence": "curl (35) SSL_ERROR_SYSCALL; urllib URLError TLS EOF (2026-09-18 probe)"},
    {"source_id": "JPL_HORIZONS", "title": "JPL Horizons ephemeris API",
     "status": "NOT AVAILABLE", "reason": "BLOCKED — ENVIRONMENT LIMITATION",
     "evidence": "urllib URLError TLS EOF (2026-09-18 probe)"},
    {"source_id": "NEXSCI_PS", "title": "NASA Exoplanet Archive Planetary Systems TAP",
     "status": "NOT AVAILABLE", "reason": "BLOCKED — ENVIRONMENT LIMITATION",
     "evidence": "urllib URLError TLS EOF (2026-09-18 probe)"},
    {"source_id": "SIMBAD_TAP", "title": "SIMBAD TAP (simbad.u-strasbg.fr)",
     "status": "NOT AVAILABLE", "reason": "BLOCKED — ENVIRONMENT LIMITATION",
     "evidence": "urllib URLError TLS EOF (2026-09-18 probe)"},
)
