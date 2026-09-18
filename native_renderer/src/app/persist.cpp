#include "persist.h"

#include <cctype>
#include <cmath>
#include <vector>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace astra::app {

std::string serialize_scenario(const ScenarioSave& s) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "ASTRA_SCENARIO\n"
        "version=%d\n"
        "sim_time_s=%.17e\n"
        "warp=%.17e\n"
        "paused=%d\n"
        "focus=%d\n"
        "selection=%d\n"
        "cam_mode=%d\n"
        "cam_azimuth=%.17e\n"
        "cam_elevation=%.17e\n"
        "cam_distance=%.17e\n"
        "free_pos_x=%.9e\n"
        "free_pos_y=%.9e\n"
        "free_pos_z=%.9e\n"
        "show_vectors=%d\n"
        "viz_mode=%s\n"
        "gravity_model=%s\n"
        "END\n",
        s.save_version, s.sim_time_s, s.warp, s.paused ? 1 : 0,
        s.focus, s.selection, s.cam_mode,
        s.cam_azimuth, s.cam_elevation, s.cam_distance,
        (double)s.free_pos[0], (double)s.free_pos[1], (double)s.free_pos[2],
        s.show_vectors ? 1 : 0, s.viz_mode.c_str(), s.gravity_model.c_str());
    std::string out = buf;  // v0.9: splice optional nbody_state before END
    const std::string end_line = "END\n";
    if (out.size() >= end_line.size() &&
        out.compare(out.size() - end_line.size(), end_line.size(), end_line) == 0) {
        out.resize(out.size() - end_line.size());
    }
    out += "nbody_state=" + s.nbody_state + "\nEND\n";
    return out;
}

// v0.9: "t,x,y,z,vx,vy,vz[,x,y,z,vx,vy,vz...]" — empty, or 1+6k finite
// doubles with k >= 1. Structural tamper rejected here; body-count/id
// matching is enforced by the consumer (celestial_sim unpack).
static bool is_valid_nbody_state(const std::string& v) {
    if (v.empty()) return true;
    std::vector<std::string> toks;
    std::string cur;
    for (char c : v) {
        if (c == ',') { toks.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    toks.push_back(cur);
    if (toks.size() < 7 || (toks.size() - 1) % 6 != 0) return false;
    for (const std::string& t : toks) {
        if (t.empty()) return false;
        try {
            size_t used = 0;
            const double d = std::stod(t, &used);
            if (used != t.size() || !std::isfinite(d)) return false;
        } catch (...) { return false; }
    }
    return true;
}

bool deserialize_scenario(const std::string& text, ScenarioSave& out) {
    std::istringstream in(text);
    std::string line;
    // Required keys in order of appearance; any mismatch/tamper fails load.
    struct Need { const char* key; double* dst; };
    ScenarioSave tmp{};
    if (!std::getline(in, line) || line != "ASTRA_SCENARIO") return false;
    bool seen[18] = {};
    int seen_count = 0;
    while (std::getline(in, line)) {
        if (line == "END") break;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        try {
            if (key == "version") { tmp.save_version = std::stoi(val); seen[0] = true; }
            else if (key == "sim_time_s") { tmp.sim_time_s = std::stod(val); seen[1] = true; }
            else if (key == "warp") { tmp.warp = std::stod(val); seen[2] = true; }
            else if (key == "paused") { tmp.paused = std::stoi(val) != 0; seen[3] = true; }
            else if (key == "focus") { tmp.focus = std::stoi(val); seen[4] = true; }
            else if (key == "selection") { tmp.selection = std::stoi(val); seen[5] = true; }
            else if (key == "cam_mode") { tmp.cam_mode = std::stoi(val); seen[6] = true; }
            else if (key == "cam_azimuth") { tmp.cam_azimuth = std::stod(val); seen[7] = true; }
            else if (key == "cam_elevation") { tmp.cam_elevation = std::stod(val); seen[8] = true; }
            else if (key == "cam_distance") { tmp.cam_distance = std::stod(val); seen[9] = true; }
            else if (key == "free_pos_x") { tmp.free_pos[0] = (float)std::stod(val); seen[10] = true; }
            else if (key == "free_pos_y") { tmp.free_pos[1] = (float)std::stod(val); seen[11] = true; }
            else if (key == "free_pos_z") { tmp.free_pos[2] = (float)std::stod(val); seen[12] = true; }
            else if (key == "show_vectors") { tmp.show_vectors = std::stoi(val) != 0; seen[13] = true; }
            else if (key == "viz_mode") { tmp.viz_mode = val; seen[14] = true; }
            else if (key == "gravity_model") {  // v0.8 optional extension
                if (val != "kepler" && val != "nbody") return false;
                tmp.gravity_model = val;
                seen[15] = true;
            }
            else if (key == "nbody_state") {  // v0.9 optional extension
                if (!is_valid_nbody_state(val)) return false;
                tmp.nbody_state = val;
                seen[16] = true;
            }
            else return false; // unknown key = tampered, do not silently discard
            ++seen_count;
        } catch (...) { return false; }
    }
    for (int i = 0; i <= 14; ++i) if (!seen[i]) return false; // missing state = fail
    // 15 = pre-v0.8; 16 = v0.8-only (+gravity_model or +nbody_state); 17 = both.
    if (seen_count < 15 || seen_count > 17) return false;
    if (tmp.save_version > ScenarioSave::VERSION) return false;
    // Contradiction = tamper: an integrated state requires the nbody model.
    if (!tmp.nbody_state.empty() && tmp.gravity_model != "nbody") return false;
    out = tmp;
    return true;
}

bool is_safe_save_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name.find("..") != std::string::npos) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '-' || c == '.')) return false;
    }
    // Reject any obvious absolute/scheme forms and bare dots.
    if (name == "." || name == "..") return false;
    return true;
}

bool save_scenario_file(const std::string& dir, const std::string& name, const ScenarioSave& s) {
    if (!is_safe_save_name(name)) return false;
    const std::string path = dir + "/" + name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << serialize_scenario(s);
    return out.good();
}

bool load_scenario_file(const std::string& dir, const std::string& name, ScenarioSave& out) {
    if (!is_safe_save_name(name)) return false;
    const std::string path = dir + "/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return deserialize_scenario(ss.str(), out);
}

} // namespace astra::app
