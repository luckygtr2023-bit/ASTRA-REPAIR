"""ASTRA Audio — stream registry (v1.6).

The registry binds: name -> (classification, provenance, units, transform
chain, generator spec). Registration is fail-closed: dishonest or
under-documented streams cannot exist in ASTRA. The registry is the ONLY
source the inspector reads; nothing ad-hoc reaches the bus.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

from astra.audio.classification import AudioClass, HonestyViolation
from astra.audio.provenance import Provenance, validate_provenance
from astra.audio.transforms import TransformChain

_STANDARD_SAMPLE_RATE = 22050.0


@dataclass
class GeneratorSpec:
    """Deterministic generator + ordered params (serializable)."""

    fn_name: str             # one of: sine, chirp, orbital_hum, static
    params: List[float]      # positional numeric params in documented order
    static_buffer: Optional[List[float]] = None  # for fn_name == "static"


@dataclass
class StreamRecord:
    name: str
    classification: AudioClass
    provenance: Provenance
    data_units: str          # physical units of the underlying quantity (or NOT AVAILABLE)
    generator: GeneratorSpec
    transforms: TransformChain

    def canonical_json(self) -> str:
        d = {
            "name": self.name,
            "classification": self.classification.value,
            "provenance": json.loads(self.provenance.canonical_json()),
            "data_units": self.data_units,
            "generator": {
                "fn_name": self.generator.fn_name,
                "params": ["%.17g" % p for p in self.generator.params],
            },
            "transforms": self.transforms.canonical(),
        }
        return json.dumps(d, sort_keys=True, separators=(",", ":"))

    def checksum(self) -> str:
        return hashlib.sha256(self.canonical_json().encode("utf-8")).hexdigest()


class AudioRegistry:
    def __init__(self) -> None:
        self._streams: Dict[str, StreamRecord] = {}

    def register(self, record: StreamRecord) -> None:
        if not record.name.strip():
            raise HonestyViolation("stream name may not be empty")
        if record.name in self._streams:
            raise HonestyViolation("duplicate stream %r refused" % record.name)
        validate_provenance(record.classification, record.provenance)
        # provenance.data_units is the source-data unit; record.data_units
        # is the stream's stated physical unit. Both must be stated.
        if not record.data_units.strip():
            raise HonestyViolation("data_units must be stated (use NOT AVAILABLE)")
        self._streams[record.name] = record

    def get(self, name: str) -> StreamRecord:
        if name not in self._streams:
            raise HonestyViolation("unknown audio stream %r (refusing to guess)" % name)
        return self._streams[name]

    def names(self) -> List[str]:
        return sorted(self._streams)

    def render(self, name: str) -> List[float]:
        rec = self.get(name)
        return rec.transforms.apply(_generate(rec.generator))


_GENERATORS: Dict[str, Callable[..., List[float]]] = {}


def _generate(spec: GeneratorSpec) -> List[float]:
    if spec.fn_name == "static":
        if spec.static_buffer is None:
            raise HonestyViolation("static generator without buffer")
        return list(spec.static_buffer)
    if spec.fn_name in _GENERATORS:
        return _GENERATORS[spec.fn_name](*spec.params)
    raise HonestyViolation("unknown generator %r (refusing to guess)" % spec.fn_name)


def register_generator(name: str, fn: Callable[..., List[float]]) -> None:
    _GENERATORS[name] = fn


def standard_registry() -> AudioRegistry:
    """The built-in honest stream set. No fabricated recordings:
    every entry is generated from an in-repo model or is an explicit
    UI/interpretive cue with a declared license and mapping.
    """
    from astra.audio.synthesis import chirp, orbital_hum, sine

    register_generator("sine", sine)
    register_generator("chirp", chirp)
    register_generator("orbital_hum", orbital_hum)

    reg = AudioRegistry()

    reg.register(
        StreamRecord(
            name="ui.tick",
            classification=AudioClass.CINEMATIC,
            provenance=Provenance(
                source="astra.audio.synthesis.sine",
                citation="ASTRA-original UI cue (synthesized in-repo)",
                license="ASTRA project license (generated asset)",
                data_units="NOT AVAILABLE",
            ),
            data_units="NOT AVAILABLE",
            generator=GeneratorSpec("sine", [880.0, 0.02, _STANDARD_SAMPLE_RATE]),
            transforms=TransformChain(),
        )
    )

    reg.register(
        StreamRecord(
            name="journey.begin",
            classification=AudioClass.SPECULATIVE,
            provenance=Provenance(
                source="astra.audio.synthesis.chirp",
                citation=(
                    "ASTRA-original interpretive cue: traversal/warp are "
                    "SPECULATIVE — this is not and cannot be a recorded sound"
                ),
                license="ASTRA project license (generated asset)",
                data_units="NOT AVAILABLE",
            ),
            data_units="NOT AVAILABLE",
            generator=GeneratorSpec("chirp", [220.0, 660.0, 0.25, _STANDARD_SAMPLE_RATE]),
            transforms=TransformChain(),
        )
    )

    from astra.catalog.transform import AU_KM
    from astra.physics.constants import GRAVITATIONAL_CONSTANT

    AU_M = AU_KM * 1.0e3                       # IAU 2012 (in-repo constant)
    MU_SUN = GRAVITATIONAL_CONSTANT * 1.9885e30  # in-repo constants (G, solar mass
                                                 # consistent with celestial_sim)

    reg.register(
        StreamRecord(
            name="orbit.earth_hum",
            classification=AudioClass.SCIENTIFICALLY_INTERPRETED,
            provenance=Provenance(
                source="astra.orbital.period.orbital_period",
                citation=(
                    "Interpretive mapping: orbital period computed by the "
                    "in-repo Kepler authority; freq = carrier * (T_ref/T)^1. "
                    "NOT a physical sound — space is vacuum."
                ),
                license="ASTRA project license (generated asset)",
                data_units="s (orbital period)",
            ),
            data_units="s (orbital period)",
            generator=GeneratorSpec(
                "orbital_hum",
                [
                    1.0 * AU_M,        # a (Earth)
                    MU_SUN,            # mu
                    365.25 * 86400.0,  # reference period (1 yr)
                    55.0,              # carrier_hz
                    0.25,              # duration
                    _STANDARD_SAMPLE_RATE,
                ],
            ),
            transforms=TransformChain(),
        )
    )
    return reg
