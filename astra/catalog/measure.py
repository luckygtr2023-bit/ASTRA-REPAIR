"""Observatory measurement layer (v1.4 Phase 3).

Observer-position-aware measurements on REAL catalog records:
  * direction of the star as seen from an arbitrary observer position
    (stellar parallax — real geometry from real data),
  * distance and light-travel delay from THIS observer,
  * apparent magnitude from THIS observer distance,
  * proper-motion-updated catalog position at J2000 + t years,
  * FOV containment relative to a camera axis,
  * angular separation between catalog objects (robust atan2 form).

Every derived value is classified DATA_DERIVED with its basis formula and
REAL_DATA inputs; missing inputs surface as NOT AVAILABLE (None), never as
an invented number. Uncertainty: neither HYG nor OpenNGC ships per-field
formal uncertainties in these CSVs → uncertainty is reported as
NOT AVAILABLE (uncertainty=None) — honesty over a fake ± number.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional, Tuple

from .hyg import HygStar
from .transform import (
    PC_KM, C_KM_S, JULIAN_YEAR_S, radec_to_unit, apply_proper_motion_deg,
    apparent_magnitude_at_distance, light_travel_years,
)

Vec3 = Tuple[float, float, float]


def _sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _norm2(v: Vec3) -> float:
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]


def _norm(v: Vec3) -> float:
    return math.sqrt(_norm2(v))


def _unit(v: Vec3) -> Vec3:
    n = _norm(v)
    if n == 0.0:
        raise ValueError("zero-length vector")
    return (v[0] / n, v[1] / n, v[2] / n)


def angular_separation_rad(a: Vec3, b: Vec3) -> float:
    """Robust angle between directions: atan2(|a×b|, a·b) (Kahan-friendly)."""
    ua, ub = _unit(a), _unit(b)
    cx = ua[1] * ub[2] - ua[2] * ub[1]
    cy = ua[2] * ub[0] - ua[0] * ub[2]
    cz = ua[0] * ub[1] - ua[1] * ub[0]
    dot = ua[0] * ub[0] + ua[1] * ub[1] + ua[2] * ub[2]
    return math.atan2(math.sqrt(cx * cx + cy * cy + cz * cz), dot)


def angular_separation_deg(a: Vec3, b: Vec3) -> float:
    return math.degrees(angular_separation_rad(a, b))


@dataclass(frozen=True)
class ObservatoryFrame:
    """Observer state for measurements.

    observer_km: position in the SHARED heliocentric-equatorial frame the
        renderer uses (km, double). Frame alignment to the catalog's
        ICRS-aligned axes is an engine assumption; document in report.
    fwd_km_unit/right_km_unit/up_km_unit: camera axes (orthonormal, unit).
    fov_half_rad: half of the vertical FOV.
    """
    observer_km: Vec3
    fwd_km_unit: Vec3
    up_km_unit: Vec3
    right_km_unit: Vec3
    fov_half_rad: float

    def __post_init__(self):
        for name in ("observer_km", "fwd_km_unit", "up_km_unit", "right_km_unit"):
            v = getattr(self, name)
            if len(v) != 3 or any(math.isnan(x) or math.isinf(x) for x in v):
                raise ValueError(f"{name} must be a finite 3-vector")
        for name in ("fwd_km_unit", "up_km_unit", "right_km_unit"):
            if abs(_norm(getattr(self, name)) - 1.0) > 1e-9:
                raise ValueError(f"{name} must be unit length")
        if not (0.0 < self.fov_half_rad < math.pi / 2.0 + 1e-9):
            raise ValueError("fov_half_rad out of range")


def fov_contains(frame: ObservatoryFrame, direction_km: Vec3) -> bool:
    """True iff `direction_km` from the observer lies inside the FOV cone."""
    rel = _sub(direction_km, frame.observer_km)
    if _norm2(rel) == 0.0:
        return True  # observer coincides with the object: degenerate, visible
    d = _unit(rel)
    cosang = d[0] * frame.fwd_km_unit[0] + d[1] * frame.fwd_km_unit[1] + d[2] * frame.fwd_km_unit[2]
    return cosang >= math.cos(frame.fov_half_rad)


@dataclass(frozen=True)
class CatalogObservation:
    """Measurement of a HygStar from a given observer (DATA_DERIVED)."""
    hyg_id: int
    catalog_id_label: str              # e.g. "HYG 32263" (+ "HIP 32349" when known)
    direction_from_observer: Vec3      # unit vector, heliocentric-equatorial axes
    distance_km_observer: float        # REAL geometry from REAL position (DATA_DERIVED from HERE)
    distance_pc: float                 # raw catalog value (REAL_DATA when available)
    distance_ly_observer: float
    light_travel_delay_years: float
    light_travel_delay_s: float
    observer_ra_deg: float             # measured RA/Dec from the observer (parallax-shifted)
    observer_dec_deg: float
    apparent_mag_observer: Optional[float]  # None → NOT AVAILABLE (catalog mag or absmag missing)
    spectral_class: Optional[str]
    temp_k_estimate: Optional[float]   # PHYSICALLY_MODELED estimate; None → NOT AVAILABLE
    uncertainty: None = None           # formal uncertainties NOT AVAILABLE for this CSV (documented)
    classification: str = "DATA_DERIVED"
    provenance: str = "HYG_V41"


def measure_from_observer(star: HygStar, observer_km: Vec3,
                          years_since_j2000: float = 0.0) -> CatalogObservation:
    """Measure `star` from an observer at `observer_km` (frame: km, shared).

    If years_since_j2000 != 0, proper motion is applied FIRST (documented
    small-angle approximation) before re-deriving the position — the
    "historical light-delay" mode needs emission-time positions.
    """
    if len(observer_km) != 3 or any(math.isnan(v) or math.isinf(v) for v in observer_km):
        raise ValueError("observer_km must be a finite 3-vector")

    ra_deg, dec_deg = star.ra_deg, star.dec_deg
    if years_since_j2000 != 0.0 and star.pmra_mas_yr is not None and star.pmdec_mas_yr is not None:
        ra_deg, dec_deg = apply_proper_motion_deg(
            ra_deg, dec_deg, star.pmra_mas_yr, star.pmdec_mas_yr, years_since_j2000)

    u = radec_to_unit(ra_deg, dec_deg)
    if star.dist_pc is None:
        # Distance NOT AVAILABLE: sky-direction-only measurement from this observer.
        # Direction from a shifted observer is undefined without a distance;
        # report catalog direction + NOT AVAILABLE distance (honest).
        obs_ra = ra_deg
        obs_dec = dec_deg
        return CatalogObservation(
            hyg_id=star.hyg_id,
            catalog_id_label=_label(star),
            direction_from_observer=u,
            distance_km_observer=math.nan,
            distance_pc=math.nan,
            distance_ly_observer=math.nan,
            light_travel_delay_years=math.nan,
            light_travel_delay_s=math.nan,
            observer_ra_deg=obs_ra,
            observer_dec_deg=obs_dec,
            apparent_mag_observer=None,
            spectral_class=star.spect,
            temp_k_estimate=_temp_of(star),
        )

    d_km = star.dist_pc * PC_KM
    star_km = (u[0] * d_km, u[1] * d_km, u[2] * d_km)
    rel = _sub(star_km, observer_km)
    dist_obs = _norm(rel)
    if dist_obs == 0.0:
        raise ValueError("observer coincides with the star (degenerate measurement)")
    dir_obs = _unit(rel)
    obs_ra = (math.degrees(math.atan2(dir_obs[1], dir_obs[0])) + 360.0) % 360.0
    obs_dec = math.degrees(math.asin(max(-1.0, min(1.0, dir_obs[2]))))

    app_mag: Optional[float] = None
    if star.absmag is not None:
        app_mag = apparent_magnitude_at_distance(star.absmag, dist_obs)
    lt_years = light_travel_years(dist_obs)

    return CatalogObservation(
        hyg_id=star.hyg_id,
        catalog_id_label=_label(star),
        direction_from_observer=dir_obs,
        distance_km_observer=dist_obs,
        distance_pc=star.dist_pc,
        distance_ly_observer=dist_obs / (C_KM_S * JULIAN_YEAR_S),
        light_travel_delay_years=lt_years,
        light_travel_delay_s=lt_years * JULIAN_YEAR_S,
        observer_ra_deg=obs_ra,
        observer_dec_deg=obs_dec,
        apparent_mag_observer=app_mag,
        spectral_class=star.spect,
        temp_k_estimate=_temp_of(star),
    )


def _label(star: HygStar) -> str:
    s = f"HYG {star.hyg_id}"
    if star.hip_id:
        s += f" / HIP {star.hip_id}"
    return s


def _temp_of(star: HygStar) -> Optional[float]:
    from .spectra import spectral_temperature_k
    return spectral_temperature_k(star.spect).temp_k
