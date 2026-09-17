#include "persist.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace astra::app {

std::string serialize_scenario(const ScenarioSave& s) {
    char buf[768];
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
        "END\n",
        s.save_version, s.sim_time_s, s.warp, s.paused ? 1 : 0,
        s.focus, s.selection, s.cam_mode,
        s.cam_azimuth, s.cam_elevation, s.cam_distance,
        (double)s.free_pos[0], (double)s.free_pos[1], (double)s.free_pos[2],
        s.show_vectors ? 1 : 0, s.viz_mode.c_str());
    return buf;
}

bool deserialize_scenario(const std::string& text, ScenarioSave& out) {
    std::istringstream in(text);
    std::string line;
    // Required keys in order of appearance; any mismatch/tamper fails load.
    struct Need { const char* key; double* dst; };
    ScenarioSave tmp{};
    if (!std::getline(in, line) || line != "ASTRA_SCENARIO") return false;
    bool seen[16] = {};
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
            else return false; // unknown key = tampered, do not silently discard
            ++seen_count;
        } catch (...) { return false; }
    }
    for (int i = 0; i <= 14; ++i) if (!seen[i]) return false; // missing state = fail
    if (seen_count != 15) return false;
    if (tmp.save_version > ScenarioSave::VERSION) return false;
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
