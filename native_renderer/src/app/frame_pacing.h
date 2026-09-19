#pragma once
// ASTRA v1.7 — frame pacing + jitter native mirror.
// Byte-compatible decisions with astra/vizperf/* (parity by gates vs fixture).
// The controller NEVER samples a clock: callers feed MEASURED host times —
// no fabricated timing anywhere in the layer.

#include <cstdint>

namespace astra::vizperf {

constexpr int QUALITY_MAX = 3;
constexpr int HYSTERESIS = 30;

enum class QualityDecision { LOWER = 0, HOLD = 1, RAISE = 2 };

class AdaptiveQualityController {
public:
    explicit AdaptiveQualityController(double target_ms, int quality_start = QUALITY_MAX);
    // returns the decision; advances EMA/cooldown state
    QualityDecision update(double measured_ms);
    double ema_ms() const { return ema_ms_; }
    int quality() const { return quality_; }
    int cooldown() const { return cooldown_; }
    std::uint64_t samples() const { return samples_; }
    bool ok() const { return ok_; }
private:
    double target_ms_ = 0.0, ema_ms_ = 0.0;
    int quality_ = 0, cooldown_ = 0;
    std::uint64_t samples_ = 0;
    bool ok_ = false;
};

// fixed-step interpolation alpha (garbage input -> 0.0)
double interpolation_alpha(double accumulator_s, double fixed_dt_s);

// Halton(2,3) jitter, identical op order to astra/vizperf/jitter.py.
// index >= 1 (Halton convention); invalid input -> false.
bool halton(int index, int base, double& out);
bool sub_pixel_jitter(int index, double& out_x, double& out_y);

// LOD classification policy (mirror astra/vizperf/lod.py): 0 CULL, 1 LOW, 2 HIGH.
int lod_class_for_size(double size_px);

} // namespace astra::vizperf
