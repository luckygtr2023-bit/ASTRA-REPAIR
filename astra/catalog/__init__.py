"""ASTRA Catalog — real astronomical data authority (v1.4).

Provenance-first ingestion, normalization, measurement and binary export of
REAL external catalogs for the scientific engine and the native renderer.

Authorities ingested (ALL real, open-licensed, pinned by sha256):
    HYG v4.1    — astronexus/HYG-Database (Gaia DR3 + Hipparcos + Gliese
                   derived positions/distances/proper motions, epoch AND
                   equinox J2000.0), CC BY-SA 4.0.
    OpenNGC     — mattiaverga/OpenNGC (NGC/IC deep-sky objects; NED-based;
                   RA/Dec J2000 + V-mag + angular sizes + redshifts), CC BY-SA 4.0.

Endpoints BLOCKED in this environment (NOT AVAILABLE — ENVIRONMENT LIMITATION;
recorded, never substituted with fabricated data): gea.esac.esa.int (Gaia TAP),
ssd.jpl.nasa.gov (Horizons), exoplanetarchive.ipac.caltech.edu (NASA Exoplanet
Archive), simbad.u-strasbg.fr (SIMBAD). See sources.UNAVAILABLE_SOURCES.

Contract (never violated):
    * strict parsing; malformed rows NEVER enter the simulation — they are
      counted and recorded in the export manifest (visible, not silent).
    * NULL is NULL (None). Zero is never substituted for missing data.
      HYG distances of exactly 100000 pc mean "missing/dubious parallax"
      (per the dataset README) and are normalized to dist_pc=None with
      distance_available=False; the file's x,y,z columns for those rows are
      junk geometry at a 100 kpc shell and are NEVER ingested.
    * Positions stay double precision until the native visualization boundary;
      single precision appears only in GPU instance buffers after a
      floating-origin subtraction.
    * Every emitted quantity carries a classification label
      (REAL_DATA / DATA_DERIVED / PHYSICALLY_MODELED / CINEMATIC /
      NOT AVAILABLE) through to the HUD.
"""

from .sources import DatasetSource, SOURCES, UNAVAILABLE_SOURCES
from .hyg import HygStar, parse_hyg_row, HYG_HEADER
from .openngc import OpenNgcObject, parse_openngc_row, OPENNGC_HEADER
from .transform import (
    PC_KM, LY_KM, C_KM_S, JULIAN_YEAR_S, radec_to_unit, parsec_to_ly,
    parsec_to_km, light_travel_years, apply_proper_motion_deg,
    apparent_magnitude_at_distance,
)
from .spectra import spectral_temperature_k, SpectralEstimate
from .measure import (
    ObservatoryFrame, angular_separation_rad, angular_separation_deg,
    fov_contains, measure_from_observer, CatalogObservation,
)
from .pipeline import (
    IngestResult, ingest_hyg_csv, ingest_openngc_csv, emit_binary_catalog,
    STAR_RECORD_FORMAT, DSO_RECORD_FORMAT, STAR_RECORD_SIZE, DSO_RECORD_SIZE,
    STARS_MAGIC, DSO_MAGIC, CATALOG_FORMAT_VERSION,
)

__all__ = [
    "DatasetSource", "SOURCES", "UNAVAILABLE_SOURCES",
    "HygStar", "parse_hyg_row", "HYG_HEADER",
    "OpenNgcObject", "parse_openngc_row", "OPENNGC_HEADER",
    "PC_KM", "LY_KM", "C_KM_S", "JULIAN_YEAR_S",
    "radec_to_unit", "parsec_to_ly", "parsec_to_km", "light_travel_years",
    "apply_proper_motion_deg", "apparent_magnitude_at_distance",
    "spectral_temperature_k", "SpectralEstimate",
    "ObservatoryFrame", "angular_separation_rad", "angular_separation_deg",
    "fov_contains", "measure_from_observer", "CatalogObservation",
    "IngestResult", "ingest_hyg_csv", "ingest_openngc_csv",
    "emit_binary_catalog",
    "STAR_RECORD_FORMAT", "DSO_RECORD_FORMAT", "STAR_RECORD_SIZE",
    "DSO_RECORD_SIZE", "STARS_MAGIC", "DSO_MAGIC", "CATALOG_FORMAT_VERSION",
]
