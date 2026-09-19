#pragma once
// ASTRA v1.6 — Cosmic Audio native mirror (PCM layer).
// Mirrors astra/audio/transforms.py + synthesis.py EXACTLY:
//   * buffers: mono double in [-1,1]
//   * resample_linear / normalize / gain_db (same op order as Python)
//   * quantize_int16: q = clamp(floor(x*32767+0.5) if v>=0 else ceil(v-0.5))
//   * sine / chirp (phase-integral form) / orbital_hum (delegates period to
//     the same Kepler formula tree as astra.orbital.period.orbital_period:
//     T = 2*pi*sqrt((a*a)*a / mu))
// Parity is asserted against fixtures with a measured tolerance (1e-12);
// policies stay in audio_bus.* (single authority), this file is the
// deterministic PCM surface only.

#include <cstdint>
#include <string>
#include <vector>

namespace astra::audio {

// ---- transforms (mirror astra/audio/transforms.py) --------------------------
// ratio>0; returns empty on refusal (Python raises; here bool contract).
bool tr_resample_linear(const std::vector<double>& in, double ratio, std::vector<double>& out);
bool tr_normalize(const std::vector<double>& in, double peak, std::vector<double>& out);
bool tr_gain_db(const std::vector<double>& in, double db, std::vector<double>& out);

// quantize — refuses (false) on any non-finite sample; size(out)==size(in).
bool quantize_int16(const std::vector<double>& in, std::vector<int16_t>& out);

// ---- synthesis (mirror astra/audio/synthesis.py) ----------------------------
// orbital_period formula-tree mirror of astra/orbital/period.py.
double orbital_period_s(double semi_major_axis_m, double mu_m3_s2);

bool synth_sine(double freq_hz, double duration_s, double sample_rate, std::vector<double>& out);
bool synth_chirp(double f0_hz, double f1_hz, double duration_s, double sample_rate, std::vector<double>& out);
bool synth_orbital_hum(double semi_major_axis_m, double mu_m3_s2, double reference_period_s,
                       double carrier_hz, double duration_s, double sample_rate, double octave_map,
                       double& out_freq_hz, std::vector<double>& out);

} // namespace astra::audio
