"""ASTRA vizperf — performance + cinematic-renderer CPU authority (v1.7).

CPU-side authorities for the renderer's performance layer: deterministic
frame-budget pacing (adaptive quality controller), Halton(2,3) sub-pixel
jitter for TAA-class temporal sampling, and the LOD classification policy.
These never touch GPU state and never fabricate measurements: the
controller consumes MEASURED frame times supplied by the caller.

Cross-language: mirrored exactly by native_renderer/src/app/frame_pacing.*
(parity asserted by fixture gates).
"""

from astra.vizperf.jitter import halton, sub_pixel_jitter
from astra.vizperf.pacing import (
    QUALITY_MAX,
    AdaptiveQualityController,
    QualityDecision,
)
from astra.vizperf.lod import lod_class_for_size, LodClass

__all__ = [
    "halton",
    "sub_pixel_jitter",
    "QUALITY_MAX",
    "AdaptiveQualityController",
    "QualityDecision",
    "lod_class_for_size",
    "LodClass",
]
