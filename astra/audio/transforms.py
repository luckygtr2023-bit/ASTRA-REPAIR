"""ASTRA Audio — deterministic transform chains (v1.6 PCM layer).

PCM buffers are mono float64 in [-1, 1] (unquantized "unit domain").
Transforms are PURE functions (buffer, params) -> buffer; a TransformChain
applies them left-to-right and serializes canonically for checksums.

Cross-language parity contract (native mirror app/audio_synth.{h,cpp}):
  * resample_linear: nearest-left index with linear blend:
        y[i] = x[j] + (x[j+1] - x[j]) * frac, j = floor(i / ratio)
    where reading past the end reproduces the LAST sample (clamped):
        j = min(j, n-2), frac based on unclamped position
        (implemented exactly as: pos = i/ratio; j=floor(pos), clamped to
         [0, n-2] for the blend with x[j+1] clamped to x[n-1]).
  * normalize(p): refuse empty/zero-peak buffers; out = in * (p / peak).
  * gain_db(db): out = in * 10^(db/20) (double pow; parity tolerance 1e-12).
  * quantize_int16: q = clamp(floor(x*32767+0.5) for x>=0 else
                    ceil(x*32767-0.5)) into [-32768, 32767].

Numbers are double everywhere; float appears nowhere in the transform path.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

from astra.audio.classification import HonestyViolation


class TransformError(ValueError):
    """Invalid transform parameters or inapplicable transform."""


def _resample_linear(buf: List[float], ratio: float) -> List[float]:
    if not math.isfinite(ratio) or ratio <= 0.0:
        raise TransformError("resample ratio must be finite and > 0")
    n = len(buf)
    if n == 0:
        raise TransformError("empty buffer")
    m = int(math.floor((n - 1) * ratio + 1.0))
    out: List[float] = []
    for i in range(m):
        pos = i / ratio
        j = int(math.floor(pos))
        j2 = min(j + 1, n - 1)
        j = min(j, n - 1)
        frac = pos - math.floor(pos)
        out.append(buf[j] + (buf[j2] - buf[j]) * frac)
    return out


def _normalize(buf: List[float], peak: float) -> List[float]:
    if not buf:
        raise TransformError("cannot normalize empty buffer")
    p = max(abs(v) for v in buf)
    if p == 0.0 or not math.isfinite(p):
        raise TransformError("buffer peak is zero/non-finite — normalization NOT AVAILABLE")
    if not math.isfinite(peak) or peak <= 0.0 or peak > 1.0:
        raise TransformError("target peak must be in (0, 1]")
    scale = peak / p
    return [v * scale for v in buf]


def _gain_db(buf: List[float], db: float) -> List[float]:
    if not math.isfinite(db):
        raise TransformError("gain must be finite")
    f = math.pow(10.0, db / 20.0)
    return [v * f for v in buf]


_TRANSFORMS = {
    "resample_linear": _resample_linear,
    "normalize": _normalize,
    "gain_db": _gain_db,
}


@dataclass(frozen=True)
class Transform:
    name: str
    param: float

    def apply(self, buf: List[float]) -> List[float]:
        if self.name not in _TRANSFORMS:
            raise TransformError("unknown transform %r (refusing to guess)" % self.name)
        return _TRANSFORMS[self.name](buf, self.param)

    def canonical(self) -> str:
        return "%s(%.17g)" % (self.name, self.param)


@dataclass
class TransformChain:
    steps: List[Transform] = field(default_factory=list)

    def apply(self, buf: List[float]) -> List[float]:
        out = list(buf)
        for t in self.steps:
            out = t.apply(out)
        return out

    def canonical(self) -> str:
        return "|".join(t.canonical() for t in self.steps)

    @classmethod
    def parse(cls, s: str) -> "TransformChain":
        steps: List[Transform] = []
        if s:
            for part in s.split("|"):
                name, _, rest = part.partition("(")
                if not rest.endswith(")"):
                    raise TransformError("malformed transform step %r" % part)
                steps.append(Transform(name=name, param=float(rest[:-1])))
        return cls(steps)


def quantize_int16(buf: List[float]) -> List[int]:
    """Deterministic 16-bit quantization (spec quantize in module docstring)."""
    out: List[int] = []
    for x in buf:
        if not math.isfinite(x):
            raise HonestyViolation("non-finite sample refused (no NaN WAVs)")
        v = x * 32767.0
        q = math.floor(v + 0.5) if v >= 0.0 else math.ceil(v - 0.5)
        if q > 32767:
            q = 32767
        elif q < -32768:
            q = -32768
        out.append(int(q))
    return out
