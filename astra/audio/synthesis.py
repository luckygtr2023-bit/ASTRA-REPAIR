"""ASTRA Audio — deterministic model-based synthesis (v1.6).

Generators here produce monophonic float64 PCM in [-1, 1] from explicit,
documented models — never from fabricated recordings. Classifications:

  sine(freq, dur, sr)        — generic oscillator. The CALLER's registry
                               entry decides the class; a bare sine is not
                               presented as physics by itself.
  chirp(f0, f1, dur, sr)     — linear chirp (phase-integral form:
                               phi(t) = 2*pi*(f0*t + (f1-f0)*t^2/(2T))).
  orbital_hum(a, mu, ...)    — SCIENTIFICALLY_INTERPRETED: maps a computed
                               orbital period (delegated to
                               astra.orbital.orbital_period — the existing
                               authority, never re-derived) to an audible
                               carrier f = carrier_hz * (T_ref / T)^octave_map.
                               The mapping is INTERPRETIVE, documented, and
                               never described as sound emitted by the orbit.

Determinism: pure double math; no RNG; identical inputs produce identical
buffers (bit-identical within one runtime; cross-language parity asserted
with a measured 1e-12 tolerance against the C++ mirror).
"""

from __future__ import annotations

import math
from typing import List

from astra.orbital.period import orbital_period

_SIN_ERR = "synthesis parameters must be finite; sr, duration, frequencies > 0"


def _guard_positive(*vals: float) -> None:
    for v in vals:
        if not math.isfinite(v) or v <= 0.0:
            raise ValueError(_SIN_ERR)


def sine(freq_hz: float, duration_s: float, sample_rate: float) -> List[float]:
    _guard_positive(freq_hz, duration_s, sample_rate)
    n = int(math.floor(duration_s * sample_rate + 0.5))
    if n <= 0:
        raise ValueError("zero-length buffer")
    w = 2.0 * math.pi * freq_hz
    return [math.sin(w * (i / sample_rate)) for i in range(n)]


def chirp(f0_hz: float, f1_hz: float, duration_s: float, sample_rate: float) -> List[float]:
    _guard_positive(f0_hz, f1_hz, duration_s, sample_rate)
    n = int(math.floor(duration_s * sample_rate + 0.5))
    if n <= 0:
        raise ValueError("zero-length buffer")
    k = (f1_hz - f0_hz) / duration_s
    out: List[float] = []
    for i in range(n):
        t = i / sample_rate
        phase = 2.0 * math.pi * (f0_hz * t + 0.5 * k * t * t)
        out.append(math.sin(phase))
    return out


def orbital_hum(
    semi_major_axis_m: float,
    mu_m3_s2: float,
    reference_period_s: float,
    carrier_hz: float,
    duration_s: float,
    sample_rate: float,
    octave_map: float = 1.0,
) -> List[float]:
    """SCIENTIFICALLY_INTERPRETED mapping (see module docstring).

    period delegated to astra.orbital.orbital_period (existing authority).
    freq = carrier_hz * (reference_period_s / period)^octave_map.
    """
    period = orbital_period(semi_major_axis_m, mu_m3_s2)
    _guard_positive(period, reference_period_s, carrier_hz, duration_s, sample_rate)
    freq = carrier_hz * math.pow(reference_period_s / period, octave_map)
    if not math.isfinite(freq) or freq <= 0.0 or freq > sample_rate / 2.0:
        raise ValueError(
            "interpretive carrier %g Hz outside honest audio range — refusing" % freq
        )
    return sine(freq, duration_s, sample_rate)
