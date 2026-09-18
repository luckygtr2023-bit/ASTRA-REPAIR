#pragma once
// ASTRA COSMOS — Scientific HUD data model (v0.4).
//
// Pure mapping from authoritative simulation state to HUD rows. The HUD never
// invents values: anything the engine does not provide is emitted as
// "NOT AVAILABLE" with available=false. Formatting lives here so BOTH the
// console HUD and a future on-canvas text renderer consume the same rows.

#include <string>
#include <vector>

namespace astra::app {

inline const char* const NOT_AVAILABLE = "NOT AVAILABLE";

struct HudRow {
    std::string label;
    std::string value;          // NOT_AVAILABLE when !available
    std::string classification; // REAL / SIMULATED / DATA-DERIVED / CINEMATIC / >-
    bool available = true;
};

struct HudSnapshot {
    // Simulation
    double sim_time_s = 0.0;
    double warp = 1.0;
    bool paused = false;
    // Renderer (REAL measurements)
    double fps = 0.0;
    double frame_ms = 0.0;
    // Camera/exploration
    int cam_mode = 0;           // 0 = orbit-follow (FOLLOW), 1 = free (FREE)
    std::string reference_frame = "heliocentric";
    std::string viz_mode = "orbital";
    std::string gravity_model = "kepler";  // "kepler" | "nbody" (v0.8)
    // v1.0: N-body engine self-diagnostics (SIMULATED measurement of the
    // engine itself; only meaningful when gravity_model == "nbody").
    bool has_nbody_drift = false;
    double nbody_drift_rel = 0.0;
    double nbody_time_s = 0.0;
    // Selection (-1 = none)
    int selected_index = -1;
    std::string selected_name;
    std::string selected_classification;
    bool has_selected_kind = false;
    std::string selected_kind;   // STAR/PLANET/MOON
    // Selected-body authoritative values (sim units)
    bool has_selected_state = false;
    double r_helio_km = 0.0;     // heliocentric distance
    double speed_km_s = 0.0;     // heliocentric speed magnitude
    double observer_distance_km = 0.0;   // distance to camera target (observer)
    double light_delay_s = 0.0;          // = observer_distance / c (approx)
};

struct HudState {
    std::string app_status;
    std::vector<HudRow> status;    // sim time, warp, observer time, fps, frame, mode
    std::vector<HudRow> selection; // object rows
    std::vector<HudRow> frame;     // camera + reference-frame rows
};

// Pure mapping (unit-testable, no I/O).
HudState build_hud(const HudSnapshot& snap);

// Formatted lines "LABEL: VALUE  [CLASS]" for console/future on-canvas text.
std::vector<std::string> render_hud_lines(const HudState& hud);
// Single-line status summary for the window title/log ticker.
std::string hud_summary_line(const HudSnapshot& snap);

} // namespace astra::app
