#include "app/frame_pacing.h"

#include <cmath>

namespace astra::vizperf {

AdaptiveQualityController::AdaptiveQualityController(double target_ms, int quality_start) {
    if (!std::isfinite(target_ms) || target_ms <= 0.0) return;
    if (quality_start < 0 || quality_start > QUALITY_MAX) return;
    target_ms_ = target_ms;
    ema_ms_ = target_ms;
    quality_ = quality_start;
    cooldown_ = 0;
    samples_ = 0;
    ok_ = true;
}

QualityDecision AdaptiveQualityController::update(double measured_ms) {
    if (!ok_ || !std::isfinite(measured_ms) || measured_ms < 0.0) return QualityDecision::HOLD;
    ema_ms_ = 0.9 * ema_ms_ + 0.1 * measured_ms;
    ++samples_;
    if (ema_ms_ > target_ms_ * 1.10 && cooldown_ == 0 && quality_ > 0) {
        --quality_;
        cooldown_ = HYSTERESIS;
        return QualityDecision::LOWER;
    }
    if (ema_ms_ < target_ms_ * 0.90 && cooldown_ == 0 && quality_ < QUALITY_MAX) {
        ++quality_;
        cooldown_ = HYSTERESIS;
        return QualityDecision::RAISE;
    }
    if (cooldown_ > 0) --cooldown_;
    return QualityDecision::HOLD;
}

double interpolation_alpha(double accumulator_s, double fixed_dt_s) {
    if (!std::isfinite(accumulator_s) || !std::isfinite(fixed_dt_s) || fixed_dt_s <= 0.0) return 0.0;
    const double a = accumulator_s / fixed_dt_s;
    return (a < 0.0) ? 0.0 : (a > 1.0 ? 1.0 : a);
}

bool halton(int index, int base, double& out) {
    if (index < 1 || base < 2) return false;
    double f = 1.0 / static_cast<double>(base);
    double r = 0.0;
    int i = index;
    while (i > 0) {
        r += f * static_cast<double>(i % base);
        i /= base;
        f /= static_cast<double>(base);
    }
    out = r;
    return true;
}

bool sub_pixel_jitter(int index, double& out_x, double& out_y) {
    double hx, hy;
    if (!halton(index, 2, hx) || !halton(index, 3, hy)) return false;
    out_x = hx - 0.5;
    out_y = hy - 0.5;
    return true;
}

int lod_class_for_size(double size_px) {
    if (!std::isfinite(size_px) || size_px < 0.0) return 0;
    if (size_px < 4.0) return 0;
    if (size_px < 64.0) return 1;
    return 2;
}

} // namespace astra::vizperf
