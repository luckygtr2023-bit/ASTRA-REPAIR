"""ASTRA Audio — Cosmic Audio authority (v1.6).

Every audio stream in ASTRA carries:
  classification (7-class fixed vocabulary, identical to native AudioClass),
  provenance (source + citation + license + data units) where the class
  requires it, a deterministic transform chain, and refusal-based honesty
  policy. No stream is "just a sound"; no stream claims to be a real
  recording unless provenance actually documents one.

This package never fabricates recordings, licenses, or datasets. Where a
quantity cannot be stated honestly it is reported NOT AVAILABLE.

Renderer rule: this is an authority. The renderer/audio device layer is a
consumer; it never reclassifies, never re-transforms.
"""

from astra.audio.classification import (
    AudioClass,
    AudioEventKind,
    HonestyViolation,
    default_classification,
    request_is_honest,
)
from astra.audio.provenance import Provenance, validate_provenance
from astra.audio.transforms import Transform, TransformChain, quantize_int16
from astra.audio.synthesis import sine, chirp, orbital_hum
from astra.audio.registry import AudioRegistry, standard_registry
from astra.audio.inspector import ScientificAudioInspector
from astra.audio.bus import AudioRequestBus

__all__ = [
    "AudioClass",
    "AudioEventKind",
    "HonestyViolation",
    "default_classification",
    "request_is_honest",
    "Provenance",
    "validate_provenance",
    "Transform",
    "TransformChain",
    "quantize_int16",
    "sine",
    "chirp",
    "orbital_hum",
    "AudioRegistry",
    "standard_registry",
    "ScientificAudioInspector",
    "AudioRequestBus",
]
