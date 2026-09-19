"""ASTRA vizperf — adaptive frame-budget pacing (v1.7, deterministic).

The controller NEVER measures time itself: the caller feeds MEASURED host
frame times (`update(measured_ms)`); the controller's decisions are a pure
function of its state — deterministic, replayable, testable.

Spec (identical to native mirror frame_pacing.cpp):

    ema = 0.9 * ema + 0.1 * measured_ms          (each update)
    if ema > target_ms * 1.10 and cooldown == 0 and quality > 0:
        quality -= 1; cooldown = HYSTERESIS; decision = LOWER
    elif ema < target_ms * 0.90 and cooldown == 0 and quality < QUALITY_MAX:
        quality += 1; cooldown = HYSTERESIS; decision = RAISE
    else:
        decision = HOLD
    if cooldown > 0 and decision == HOLD: cooldown -= 1
    (cooldown is consumed by a successful change instead and reset to
     HYSTERESIS; a HOLD consumes one tick of it)

EMA seeding: ema0 = target_ms (neutral start, avoids spurious first
LOWER/RAISE); the first real sample blends in from there.

Interpolation alpha for decoupled sim/render rates:
    alpha = clamp(accumulator / fixed_dt, 0, 1)   (garbage-input → 0.0)
"""

from __future__ import annotations

import math
from enum import Enum

QUALITY_MAX = 3           # 0 lowest .. 3 highest render quality tier
HYSTERESIS = 30           # frames a decision locks in before re-evaluating
EMA_ALPHA = 0.1


class QualityDecision(Enum):
    LOWER = "LOWER"
    HOLD = "HOLD"
    RAISE = "RAISE"


class AdaptiveQualityController:
    def __init__(self, target_ms: float, quality_start: int = QUALITY_MAX) -> None:
        if not math.isfinite(target_ms) or target_ms <= 0.0:
            raise ValueError("target_ms must be finite and > 0")
        if not 0 <= quality_start <= QUALITY_MAX:
            raise ValueError("quality_start out of range")
        self.target_ms = float(target_ms)
        self.ema_ms = float(target_ms)
        self.quality = int(quality_start)
        self.cooldown = 0
        self.samples = 0

    def update(self, measured_ms: float) -> QualityDecision:
        if not math.isfinite(measured_ms) or measured_ms < 0.0:
            raise ValueError("measured_ms must be finite and >= 0 (no fabricated timing)")
        self.ema_ms = 0.9 * self.ema_ms + 0.1 * measured_ms
        self.samples += 1
        if (
            self.ema_ms > self.target_ms * 1.10
            and self.cooldown == 0
            and self.quality > 0
        ):
            self.quality -= 1
            self.cooldown = HYSTERESIS
            return QualityDecision.LOWER
        if (
            self.ema_ms < self.target_ms * 0.90
            and self.cooldown == 0
            and self.quality < QUALITY_MAX
        ):
            self.quality += 1
            self.cooldown = HYSTERESIS
            return QualityDecision.RAISE
        if self.cooldown > 0:
            self.cooldown -= 1
        return QualityDecision.HOLD


def interpolation_alpha(accumulator_s: float, fixed_dt_s: float) -> float:
    if (
        not math.isfinite(accumulator_s)
        or not math.isfinite(fixed_dt_s)
        or fixed_dt_s <= 0.0
    ):
        return 0.0
    a = accumulator_s / fixed_dt_s
    return 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
