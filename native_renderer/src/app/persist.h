#pragma once
// ASTRA COSMOS — scenario/observer/configuration persistence (v0.4).
//
// Deterministic, no third-party deps, path-traversal-safe. Saves preserve the
// actual application state (sim time, warp, pause, selection/focus, camera
// mode + coordinates, feature toggles) — nothing is silently discarded on
// load: any missing/corrupt field fails the load explicitly.

#include <string>

namespace astra::app {

struct ScenarioSave {
    static constexpr int VERSION = 1;
    double sim_time_s = 0.0;
    double warp = 1.0;
    bool paused = false;
    int focus = 0;             // camera target index (engine id by table order)
    int selection = -1;        // selected object index (-1 = none)
    int cam_mode = 0;          // 0 follow, 1 free
    double cam_azimuth = 0.0;
    double cam_elevation = 0.0;
    double cam_distance = 450.0;
    float free_pos[3] = {0, 0, 0};
    bool show_vectors = true;
    std::string viz_mode = "orbital";
    int save_version = VERSION;
};

// Deterministic text serialization (stable key order; test-round-trippable).
std::string serialize_scenario(const ScenarioSave& s);
// Strict parse: every required key must be present and well-typed.
bool deserialize_scenario(const std::string& text, ScenarioSave& out);

// Path safety: name must be a bare filename [A-Za-z0-9_.-]{1,64}, no
// separators, no "..", extension remains caller-controlled.
bool is_safe_save_name(const std::string& name);

// File IO with safety enforcement; dir is created by the caller side.
bool save_scenario_file(const std::string& dir, const std::string& name, const ScenarioSave& s);
bool load_scenario_file(const std::string& dir, const std::string& name, ScenarioSave& out);

} // namespace astra::app
