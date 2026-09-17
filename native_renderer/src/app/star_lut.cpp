#include "star_lut.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace astra::app {

bool load_ppm_p3(const std::string& path, int& w, int& h, std::string& pixels) {
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string buf = ss.str();
    // Tokenize: strip comments.
    std::vector<std::string> toks;
    std::string cur;
    for (size_t k = 0; k <= buf.size(); ++k) {
        const char c = (k < buf.size()) ? buf[k] : ' ';
        if (c == '#') {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            while (k < buf.size() && buf[k] != '\n') ++k;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (toks.size() < 4 || toks[0] != "P3") return false;
    try {
        w = std::stoi(toks[1]);
        h = std::stoi(toks[2]);
        const int maxval = std::stoi(toks[3]);
        if (w <= 0 || h <= 0 || maxval != 255) return false;
        const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
        if (toks.size() != 4 + need) return false;
        pixels.resize(need);
        for (size_t i = 0; i < need; ++i) {
            const int v = std::stoi(toks[4 + i]);
            if (v < 0 || v > 255) return false;
            pixels[i] = static_cast<char>(v);
        }
    } catch (...) {
        return false;
    }
    return true;
}

RgbF star_color_from_lut(const std::string& pixels, int w, int h, double temp_k) {
    // Documented range from visualization/tools/generate_lut.py: 2000..40000K.
    const double t_lo = 2000.0, t_hi = 40000.0;
    double t = (temp_k - t_lo) / (t_hi - t_lo);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const bool temp_on_h = (h >= w); // 16x256 asset: rows are temperature
    const int axis_len = temp_on_h ? h : w;
    int idx = static_cast<int>(t * (axis_len - 1) + 0.5);
    if (idx < 0) idx = 0;
    if (idx >= axis_len) idx = axis_len - 1;
    const int x = temp_on_h ? 0 : idx;
    const int y = temp_on_h ? idx : 0;
    const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
    return {static_cast<unsigned char>(pixels[off + 0]) / 255.0f,
            static_cast<unsigned char>(pixels[off + 1]) / 255.0f,
            static_cast<unsigned char>(pixels[off + 2]) / 255.0f};
}

bool star_color_from_file(const std::string& path, double temp_k, RgbF& out) {
    int w = 0, h = 0;
    std::string pixels;
    if (!load_ppm_p3(path, w, h, pixels)) return false;
    out = star_color_from_lut(pixels, w, h, temp_k);
    return true;
}

} // namespace astra::app
