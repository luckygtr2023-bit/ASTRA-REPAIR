"""ASTRA vizperf — LOD classification policy (v1.7).

Policy (identical to the cull.compute body classification and its CPU
mirror app/render_math.cpp): given an object's PROJECTED screen-space
size in pixels, classify:

    size_px < CULL_BELOW_PX   -> CULL  (0)
    size_px < LOW_BELOW_PX    -> LOW   (1)
    otherwise                 -> HIGH  (2)

The thresholds are policy constants (display-space, CINEMATIC class of
decision — they affect only what is drawn, never scientific state).
"""

from __future__ import annotations

import math
from enum import IntEnum

CULL_BELOW_PX = 4.0
LOW_BELOW_PX = 64.0


class LodClass(IntEnum):
    CULL = 0
    LOW = 1
    HIGH = 2


def lod_class_for_size(size_px: float) -> LodClass:
    if not math.isfinite(size_px) or size_px < 0.0:
        # fail-closed: garbage input culls, never promotes
        return LodClass.CULL
    if size_px < CULL_BELOW_PX:
        return LodClass.CULL
    if size_px < LOW_BELOW_PX:
        return LodClass.LOW
    return LodClass.HIGH
