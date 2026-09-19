"""ASTRA Audio — provenance + license + units records (v1.6).

Every stream with a real-data-adjacent classification MUST carry a
Provenance record; PHYSICALLY_MODELED streams reference the in-repo model;
CINEMATIC/SPECULATIVE streams still declare a license so nothing on the
surface is unlicensed. Fields that cannot be stated honestly are
"NOT AVAILABLE" — never fabricated.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, asdict
from typing import Optional

from astra.audio.classification import AudioClass, HonestyViolation

NOT_AVAILABLE = "NOT AVAILABLE"

# classes requiring a real-world data/recording source
_DATA_CLASSES = frozenset(
    {
        AudioClass.REAL_SIGNAL_SONIFICATION,
        AudioClass.DATA_DERIVED,
        AudioClass.REAL_ACOUSTIC,
    }
)


@dataclass(frozen=True)
class Provenance:
    """Provenance record for one audio stream."""

    source: str            # dataset / recording / in-repo model identifier
    citation: str          # human citation; NOT AVAILABLE permitted
    license: str           # license identifier; NEVER fabricated
    data_units: str        # physical units of the source data; NOT AVAILABLE permitted
    retrieved: str = NOT_AVAILABLE  # retrieval/processing note; NOT AVAILABLE permitted

    def canonical_json(self) -> str:
        return json.dumps(asdict(self), sort_keys=True, separators=(",", ":"))

    def checksum(self) -> str:
        return hashlib.sha256(self.canonical_json().encode("utf-8")).hexdigest()


def validate_provenance(
    classification: AudioClass,
    provenance: Optional[Provenance],
) -> None:
    """Fail-closed validation. Raises HonestyViolation on:

      - data-class stream without provenance,
      - provenance with empty source/license,
      - fabricated placeholders in source/license (empty = fabrication here),
      - PHYSICALLY_MODELED without an in-repo model reference,
      - any stream without a license string.
    """
    if provenance is None:
        if classification in _DATA_CLASSES:
            raise HonestyViolation(
                "classification %s requires provenance" % classification.value
            )
        raise HonestyViolation(
            "every audio stream requires at least a license record (class=%s)"
            % classification.value
        )
    if not provenance.source.strip():
        raise HonestyViolation("provenance.source may not be empty")
    if not provenance.license.strip():
        raise HonestyViolation("provenance.license may not be empty (no unlicensed audio)")
    if classification in _DATA_CLASSES:
        if provenance.source == NOT_AVAILABLE or provenance.citation == NOT_AVAILABLE:
            raise HonestyViolation(
                "data-class stream cannot mark source/citation NOT AVAILABLE "
                "(that would be an unverifiable real-data claim)"
            )
    if classification is AudioClass.PHYSICALLY_MODELED:
        if not provenance.source.startswith("astra."):
            raise HonestyViolation(
                "PHYSICALLY_MODELED streams must reference an in-repo model "
                "(source starting with 'astra.')"
            )
