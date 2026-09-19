"""ASTRA vizperf — Halton(2,3) sub-pixel jitter (v1.7, deterministic).

radical_inverse(index, base) in EXACT op order shared with the C++ mirror:
    f = 1.0 / base
    r = 0.0
    while index > 0:
        r += f * (index % base)
        index //= base          (integer division, exact)
        f  /= base
First index is 1 (Halton convention; index 0 is the origin sample).

sub_pixel_jitter(n) -> (jx-0.5, jy-0.5) using bases (2,3), the standard
TAA-class pattern. Values are in [-0.5, 0.5); the caller multiplies by
1/width and 1/height in projection space.
"""

from __future__ import annotations

from typing import Tuple


def halton(index: int, base: int) -> float:
    if index < 1:
        raise ValueError("halton index must be >= 1")
    if base < 2:
        raise ValueError("halton base must be >= 2")
    f = 1.0 / base
    r = 0.0
    i = index
    while i > 0:
        r += f * float(i % base)
        i //= base
        f /= base
    return r


def sub_pixel_jitter(index: int) -> Tuple[float, float]:
    """Deterministic TAA-class jitter pair, centered on 0."""
    return halton(index, 2) - 0.5, halton(index, 3) - 0.5
