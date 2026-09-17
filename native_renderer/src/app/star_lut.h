#pragma once
// ASTRA COSMOS — star-temperature LUT consumer (Phase R: real asset).
//
// Asset: native_renderer/assets/star_temperature_lut.ppm (ASCII P3).
// Provenance: visualization/tools/generate_lut.py — blackbody APPROXIMATION,
// stylized ramp for 2000K..40000K (t = x/(W-1)), CC0. This is a visual
// approximation (SCIENTIFICALLY INTERPRETED), NOT a photometric measurement;
// the temperature axis itself is authoritative when fed sim T_eff values.

#include <string>

namespace astra::app {

struct RgbF { float r, g, b; };

// Very small strict P3 PPM reader: magic P3, optional '#' comments, then
// W H maxval and W*H*3 integers. Returns false on any malformation.
bool load_ppm_p3(const std::string& path, int& w, int& h, std::string& pixels /*RGB bytes packed*/);

// Sample color for effective temperature (K) using the documented
// 2000..40000K range (clamped). The temperature axis is whichever of W/H
// equals 256 in the asset (16x256 here: rows = temperature).
RgbF star_color_from_lut(const std::string& pixels, int w, int h, double temp_k);

// Convenience: load + sample; returns false if asset unavailable (caller
// keeps its previous visual defaults — never fabricates an authoritative one).
bool star_color_from_file(const std::string& path, double temp_k, RgbF& out);

} // namespace astra::app
