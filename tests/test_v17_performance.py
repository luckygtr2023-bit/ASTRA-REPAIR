"""ASTRA v1.7 — PERFORMANCE + CINEMATIC RENDERER contract tests.

Obligations: deterministic pacing decisions, exact Halton jitter values,
LOD policy boundaries (fail-closed on garbage → CULL), interpolation alpha
safety, cross-language parity contract (gate-side), honest status labels on
cinematic shaders. Performance numbers are MEASURED in the native gates —
never fabricated here.
"""

import math

import pytest

from astra.vizperf import (
    QUALITY_MAX,
    AdaptiveQualityController,
    QualityDecision,
    halton,
    lod_class_for_size,
    LodClass,
    sub_pixel_jitter,
)
from astra.vizperf.pacing import HYSTERESIS, interpolation_alpha


# ---------------- halton ----------------

def test_halton_first_values_exact():
    # known Halton sequences: base2: 1/2, 1/4, 3/4, 1/8...; base3: 1/3, 2/3, 1/9...
    assert halton(1, 2) == 0.5
    assert halton(2, 2) == 0.25
    assert halton(3, 2) == 0.75
    assert halton(4, 2) == 0.125
    assert halton(1, 3) == pytest.approx(1.0 / 3.0, abs=0.0)
    assert halton(2, 3) == pytest.approx(2.0 / 3.0, abs=0.0)
    assert halton(3, 3) == pytest.approx(1.0 / 9.0, abs=0.0)


def test_halton_deterministic_and_refusals():
    assert halton(7, 2) == halton(7, 2)
    with pytest.raises(ValueError):
        halton(0, 2)
    with pytest.raises(ValueError):
        halton(1, 1)


def test_jitter_centered_range():
    for i in range(1, 65):
        x, y = sub_pixel_jitter(i)
        assert -0.5 <= x < 0.5 and -0.5 <= y < 0.5


# ---------------- controller ----------------

def test_controller_steady_on_budget_holds():
    c = AdaptiveQualityController(16.6667)
    for _ in range(50):
        assert c.update(16.6667) is QualityDecision.HOLD
    assert c.quality == QUALITY_MAX


def test_controller_lowers_quality_on_sustained_overrun():
    c = AdaptiveQualityController(16.6667)
    decisions = [c.update(25.0) for _ in range(65)]
    # EMA climbs from 16.6667; crosses 18.333 at ~12 samples → LOWER, then hysteresis
    assert QualityDecision.LOWER in decisions
    first = decisions.index(QualityDecision.LOWER)
    assert all(d is QualityDecision.HOLD for d in decisions[:first])
    # hysteresis locks: next change only after HYSTERESIS holds
    subsequent = decisions[first + 1:]
    if QualityDecision.LOWER in subsequent:
        second = subsequent.index(QualityDecision.LOWER)
        assert second == HYSTERESIS


def test_controller_raises_on_headroom_and_caps_at_max():
    c = AdaptiveQualityController(16.6667, quality_start=0)
    decisions = [c.update(4.0) for _ in range(200)]
    assert c.quality == QUALITY_MAX  # capped, never beyond
    assert decisions.count(QualityDecision.RAISE) == QUALITY_MAX


def test_controller_floors_at_zero():
    c = AdaptiveQualityController(16.6667, quality_start=0)
    for _ in range(200):
        c.update(1e6)  # brutal overrun
    assert c.quality == 0


@pytest.mark.parametrize("bad", [float("nan"), float("inf"), -1.0])
def test_controller_refuses_garbage_measurements(bad):
    c = AdaptiveQualityController(16.6667)
    with pytest.raises(ValueError):
        c.update(bad)


def test_controller_refuses_bad_construction():
    with pytest.raises(ValueError):
        AdaptiveQualityController(0.0)
    with pytest.raises(ValueError):
        AdaptiveQualityController(16.6667, quality_start=QUALITY_MAX + 1)


def test_controller_determinism_two_runs():
    a, b = AdaptiveQualityController(16.6667, 3), AdaptiveQualityController(16.6667, 3)
    seq = [25.0] * 40 + [16.6667] * 20 + [4.0] * 40
    assert [a.update(v) for v in seq] == [b.update(v) for v in seq]
    assert a.ema_ms == b.ema_ms and a.cooldown == b.cooldown


# ---------------- interpolation alpha ----------------

def test_interpolation_alpha_clamps_and_guards():
    assert interpolation_alpha(0.0, 1 / 60) == 0.0
    assert interpolation_alpha(1 / 120, 1 / 60) == pytest.approx(0.5, abs=1e-15)
    assert interpolation_alpha(1.0, 0.5) == 1.0
    assert interpolation_alpha(-1.0, 1.0) == 0.0
    assert interpolation_alpha(float("nan"), 1.0) == 0.0
    assert interpolation_alpha(1.0, 0.0) == 0.0


# ---------------- LOD policy ----------------

def test_lod_boundaries():
    assert lod_class_for_size(0.0) is LodClass.CULL
    assert lod_class_for_size(3.9) is LodClass.CULL
    assert lod_class_for_size(4.0) is LodClass.LOW
    assert lod_class_for_size(63.9) is LodClass.LOW
    assert lod_class_for_size(64.0) is LodClass.HIGH
    assert lod_class_for_size(1.0e6) is LodClass.HIGH


@pytest.mark.parametrize("bad", [float("nan"), float("inf"), -1.0])
def test_lod_fail_closed_on_garbage(bad):
    assert lod_class_for_size(bad) is LodClass.CULL  # never promotes garbage
