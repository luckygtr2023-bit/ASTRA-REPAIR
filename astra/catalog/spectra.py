"""Spectral-class → approximate effective temperature (PHYSICALLY_MODELED).

The HYG catalog carries a spectral TYPE (morphological MK string), not a
measured temperature. We map it to a tabulated typical Teff as a
PHYSICALLY_MODELED estimate — explicitly classified, NOT observational.

Table authority: Mamajek, "A Modern Mean Dwarf Stellar Color and Effective
Temperature Sequence" (2013, updated 2021-05-17), the standard reference;
values here use the canonical MK-grid approximations tabulated there (dwarf
sequence). Luminosity-class variations (giants are cooler at fixed spectral
type) are NOT resolved — the estimate is labeled as the dwarf-sequence
approximation and its class uncertainty is stated in the report.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass
from typing import Optional

# (spectral letter, subtype digit) → Teff K (dwarf sequence, Mamajek 2013-scale)
_TEFF_GRID = {
    "O": {0: 38000, 5: 35000, 8: 32000},
    "B": {0: 30000, 1: 25400, 2: 22000, 3: 18700, 5: 15200, 7: 13300, 8: 12400, 9: 10700},
    "A": {0: 9520, 1: 9230, 2: 8820, 3: 8600, 5: 8080, 7: 7680, 9: 7410},
    "F": {0: 7220, 2: 6820, 5: 6540, 7: 6360, 9: 6130},
    "G": {0: 5920, 2: 5770, 5: 5660, 8: 5490},
    "K": {0: 5240, 2: 5000, 5: 4380, 7: 4220},
    "M": {0: 3850, 1: 3660, 2: 3560, 3: 3430, 4: 3210, 5: 3060, 6: 2810, 7: 2680, 8: 2570},
}
_CLASS_ORDER = "OBAFGKM"
_SPEC_RE = re.compile(r"^\s*(sd|esd)?\s*([OBAFGKMWLTYC])\s*([0-9](?:\.\d+)?)?", re.IGNORECASE)


@dataclass(frozen=True)
class SpectralEstimate:
    temp_k: Optional[float]     # None → NOT AVAILABLE (unparseable/blank type)
    spectral_class: Optional[str]
    subtype: Optional[float]
    basis: str                  # "PHYSICALLY_MODELED" | "NOT AVAILABLE"


def spectral_temperature_k(spect: Optional[str]) -> SpectralEstimate:
    if not spect:
        return SpectralEstimate(None, None, None, "NOT AVAILABLE")
    m = _SPEC_RE.match(spect)
    if not m:
        return SpectralEstimate(None, None, None, "NOT AVAILABLE")
    letter = m.group(2).upper()
    if letter not in _CLASS_ORDER:
        # W/L/T/Y/C classes are outside the tabulated dwarf-sequence grid —
        # honestly NOT AVAILABLE rather than an invented temperature.
        return SpectralEstimate(None, letter, None, "NOT AVAILABLE")
    sub_raw = m.group(3)
    sub = float(sub_raw) if sub_raw is not None else None
    if sub is None:
        sub = 5.0  # class given without subtype (e.g. "G star"): midpoint, documented
    grid = _TEFF_GRID[letter]
    keys = sorted(grid)
    # linear interpolation over the tabulated grid (clamped at ends)
    if sub <= keys[0]:
        t = grid[keys[0]]
    elif sub >= keys[-1]:
        t = grid[keys[-1]]
    else:
        lo = max(k for k in keys if k <= sub)
        hi = min(k for k in keys if k >= sub)
        if lo == hi:
            t = grid[lo]
        else:
            w = (sub - lo) / (hi - lo)
            t = grid[lo] * (1.0 - w) + grid[hi] * w
    if math.isnan(t) or t <= 0.0:  # pragma: no cover - defensive
        return SpectralEstimate(None, letter, sub, "NOT AVAILABLE")
    return SpectralEstimate(float(t), letter, sub, "PHYSICALLY_MODELED")
