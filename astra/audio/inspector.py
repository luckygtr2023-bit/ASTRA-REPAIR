"""ASTRA Audio — scientific audio inspector (v1.6).

Answers "what exactly is playing?" from the registry: classification,
provenance, license, units, transform chain, and determinism checksum.
Never guesses: an unregistered name is a refusal, not a guess.
"""

from __future__ import annotations

from typing import Any, Dict, Optional

from astra.audio.classification import HonestyViolation
from astra.audio.registry import AudioRegistry

NOT_AVAILABLE = "NOT AVAILABLE"


class ScientificAudioInspector:
    def __init__(self, registry: Optional[AudioRegistry] = None) -> None:
        self._registry = registry if registry is not None else AudioRegistry()

    def inspect(self, name: str) -> Dict[str, Any]:
        rec = self._registry.get(name)  # raises on unknown (no guessing)
        return {
            "name": rec.name,
            "classification": rec.classification.value,
            "source": rec.provenance.source,
            "citation": rec.provenance.citation,
            "license": rec.provenance.license,
            "source_data_units": rec.provenance.data_units,
            "stream_data_units": rec.data_units,
            "retrieved": rec.provenance.retrieved,
            "transform_chain": rec.transforms.canonical() or "identity",
            "generator": rec.generator.fn_name,
            "checksum": rec.checksum(),
            "claims_real_recording": rec.classification.value
            == "REAL_SIGNAL_SONIFICATION",
            "vacuum_safe": rec.classification.value != "REAL_ACOUSTIC",
        }

    def verify_consistency(self) -> bool:
        """Cross-check every registered stream (fail-closed: raises)."""
        for name in self._registry.names():
            info = self.inspect(name)
            if info["claims_real_recording"] and info["citation"] == NOT_AVAILABLE:
                raise HonestyViolation(
                    "stream %r claims a real recording without a citation" % name
                )
            if not info["license"]:
                raise HonestyViolation("unlicensed stream %r" % name)
        return True
