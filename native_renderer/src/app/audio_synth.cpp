#include "app/audio_synth.h"

#include <algorithm>
#include <cmath>

namespace astra::audio {

namespace {
constexpr double kPi = 3.141592653589793;   // math.pi literal

inline bool finite_pos(double v) { return std::isfinite(v) && v > 0.0; }
} // namespace

bool tr_resample_linear(const std::vector<double>& in, double ratio, std::vector<double>& out) {
    if (!finite_pos(ratio) || in.empty()) return false;
    if (&in == &out) {  // alias-safe: resampling must never read back its own writes
        const std::vector<double> tmp(in);
        return tr_resample_linear(tmp, ratio, out);
    }
    const double n = static_cast<double>(in.size());
    const size_t m = static_cast<size_t>(std::floor((n - 1.0) * ratio + 1.0));
    out.resize(m);
    const size_t ni = in.size();
    for (size_t i = 0; i < m; ++i) {
        const double pos = static_cast<double>(i) / ratio;
        const double fp = std::floor(pos);
        size_t j = static_cast<size_t>(fp);
        j = std::min(j, ni - 1);
        const size_t j2 = std::min(j + 1, ni - 1);
        const double frac = pos - fp;
        out[i] = in[j] + (in[j2] - in[j]) * frac;
    }
    return true;
}

bool tr_normalize(const std::vector<double>& in, double peak, std::vector<double>& out) {
    if (in.empty() || !std::isfinite(peak) || peak <= 0.0 || peak > 1.0) return false;
    double p = 0.0;
    for (const double v : in) p = std::max(p, std::fabs(v));
    if (p == 0.0 || !std::isfinite(p)) return false;
    const double s = peak / p;
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] * s;
    return true;
}

bool tr_gain_db(const std::vector<double>& in, double db, std::vector<double>& out) {
    if (!std::isfinite(db)) return false;
    const double f = std::pow(10.0, db / 20.0);
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] * f;
    return true;
}

bool quantize_int16(const std::vector<double>& in, std::vector<int16_t>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const double x = in[i];
        if (!std::isfinite(x)) return false;
        const double v = x * 32767.0;
        double q = (v >= 0.0) ? std::floor(v + 0.5) : std::ceil(v - 0.5);
        if (q > 32767.0) q = 32767.0;
        else if (q < -32768.0) q = -32768.0;
        out[i] = static_cast<int16_t>(q);
    }
    return true;
}

double orbital_period_s(double semi_major_axis_m, double mu_m3_s2) {
    // exact tree of astra/orbital/period.py: 2*pi*sqrt(a**3 / mu);
    // CPython float_pow with exponent 3 evaluates (a*a)*a.
    if (semi_major_axis_m <= 0.0 || mu_m3_s2 <= 0.0) return 0.0;
    const double a3 = (semi_major_axis_m * semi_major_axis_m) * semi_major_axis_m;
    return 2.0 * kPi * std::sqrt(a3 / mu_m3_s2);
}

bool synth_sine(double freq_hz, double duration_s, double sample_rate, std::vector<double>& out) {
    if (!finite_pos(freq_hz) || !finite_pos(duration_s) || !finite_pos(sample_rate)) return false;
    const size_t n = static_cast<size_t>(std::floor(duration_s * sample_rate + 0.5));
    if (n == 0) return false;
    const double w = 2.0 * kPi * freq_hz;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = std::sin(w * (static_cast<double>(i) / sample_rate));
    return true;
}

bool synth_chirp(double f0_hz, double f1_hz, double duration_s, double sample_rate,
                 std::vector<double>& out) {
    if (!finite_pos(f0_hz) || !finite_pos(f1_hz) || !finite_pos(duration_s) || !finite_pos(sample_rate))
        return false;
    const size_t n = static_cast<size_t>(std::floor(duration_s * sample_rate + 0.5));
    if (n == 0) return false;
    const double k = (f1_hz - f0_hz) / duration_s;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sample_rate;
        const double phase = 2.0 * kPi * (f0_hz * t + 0.5 * k * t * t);
        out[i] = std::sin(phase);
    }
    return true;
}

bool synth_orbital_hum(double semi_major_axis_m, double mu_m3_s2, double reference_period_s,
                       double carrier_hz, double duration_s, double sample_rate, double octave_map,
                       double& out_freq_hz, std::vector<double>& out) {
    const double period = orbital_period_s(semi_major_axis_m, mu_m3_s2);
    if (!finite_pos(period) || !finite_pos(reference_period_s) || !finite_pos(carrier_hz) ||
        !finite_pos(duration_s) || !finite_pos(sample_rate))
        return false;
    const double freq = carrier_hz * std::pow(reference_period_s / period, octave_map);
    if (!std::isfinite(freq) || freq <= 0.0 || freq > sample_rate / 2.0) return false;
    out_freq_hz = freq;
    return synth_sine(freq, duration_s, sample_rate, out);
}

} // namespace astra::audio
