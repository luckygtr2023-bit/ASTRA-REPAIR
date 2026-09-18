"""Unit/coordinate transforms for real catalog data (v1.4).

Pure double-precision functions. Every constant cites its authority.
Single precision appears ONLY at the native visualization boundary.

Coordinates: ICRS-aligned equatorial (+X → vernal equinox, +Z → north
celestial pole), epoch=equinox=J2000.0, matching both ingested datasets.
"""

from __future__ import annotations

import math
from typing import Optional, Tuple

# --- Authoritative constants -------------------------------------------------
AU_KM = 149597870.7                      # IAU 2012 Resolution B2 (exact)
PC_KM = 648000.0 / math.pi * AU_KM       # IAU 2015 Resolution B2 (parsec, exact given AU)
C_KM_S = 299792.458                      # SI exact
JULIAN_YEAR_S = 365.25 * 86400.0         # IAU (exact) Julian year
LY_KM = C_KM_S * JULIAN_YEAR_S           # light-year in km
PC_TO_LY = PC_KM / LY_KM                 # ≈ 3.261563777167433

MAS_PER_RAD = 180.0 / math.pi * 3600.0 * 1000.0  # milliarcsec per radian


def parsec_to_km(d_pc: float) -> float:
    if d_pc <= 0.0 or math.isnan(d_pc) or math.isinf(d_pc):
        raise ValueError("parsecs must be positive finite")
    return d_pc * PC_KM


def parsec_to_ly(d_pc: float) -> float:
    if d_pc <= 0.0 or math.isnan(d_pc) or math.isinf(d_pc):
        raise ValueError("parsecs must be positive finite")
    return d_pc * PC_TO_LY


def radec_to_unit(ra_deg: float, dec_deg: float) -> Tuple[float, float, float]:
    """Unit direction from RA/Dec in DEGREES (ICRS-aligned, J2000.0)."""
    if not (0.0 <= ra_deg < 360.0) or math.isnan(ra_deg):
        raise ValueError("ra_deg out of range [0,360)")
    if not (-90.0 <= dec_deg <= 90.0) or math.isnan(dec_deg):
        raise ValueError("dec_deg out of range [-90,90]")
    ra = math.radians(ra_deg)
    dec = math.radians(dec_deg)
    c = math.cos(dec)
    return (c * math.cos(ra), c * math.sin(ra), math.sin(dec))


def apply_proper_motion_deg(ra_deg: float, dec_deg: float,
                            pmra_mas_yr: float, pmdec_mas_yr: float,
                            years: float) -> Tuple[float, float]:
    """Small-angle proper-motion update (documented approximation).

    HYG pmra/pmdec are the standard catalog convention with dRA≡dα component
    measured in great-circle terms per the Hipparcos/Gaia practice of
    including the cos(dec) factor (i.e. pmra here is μ_α*). We therefore:
        ra_new  = ra + (μ_α* / cos(dec)) * t
        dec_new = dec + μ_δ * t
    Polar guard: |cos dec| < 1e-6 → raise (approximation invalid at pole).
    """
    cd = math.cos(math.radians(dec_deg))
    if abs(cd) < 1e-6:
        raise ValueError("proper-motion small-angle approximation invalid near pole")
    d_ra = (pmra_mas_yr / MAS_PER_RAD) * math.degrees(1.0) * years / cd
    d_dec = (pmdec_mas_yr / MAS_PER_RAD) * math.degrees(1.0) * years
    return ((ra_deg + d_ra) % 360.0, max(-90.0, min(90.0, dec_deg + d_dec)))


def apparent_magnitude_at_distance(abs_mag: float, dist_km: float) -> float:
    """m = M + 5 log10(d/10pc). Pure function of authoritative distance."""
    if dist_km <= 0.0:
        raise ValueError("dist_km must be positive")
    d_pc = dist_km / PC_KM
    return abs_mag + 5.0 * math.log10(d_pc / 10.0)


def light_travel_years(dist_km: float) -> float:
    """Look-back time in years for a light signal covering dist_km."""
    if dist_km < 0.0 or math.isnan(dist_km) or math.isinf(dist_km):
        raise ValueError("dist_km must be finite >= 0")
    return dist_km / LY_KM


def distance_pc_from_parallax_mas(parallax_mas: float) -> Optional[float]:
    """d = 1/ϖ (naive inversion; documented approximation, Gaia practice).

    Returns None for non-positive parallax (distance NOT AVAILABLE — the
    honest treatment; never a giant clamped fake distance).
    """
    if math.isnan(parallax_mas) or parallax_mas <= 0.0:
        return None
    return 1000.0 / parallax_mas
