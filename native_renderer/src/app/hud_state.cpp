#include "hud_state.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace astra::app {

static std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

HudState build_hud(const HudSnapshot& s) {
    HudState hud;
    hud.app_status = s.paused ? "ASTRA COSMOS — PAUSED" : "ASTRA COSMOS — RUNNING";

    const double years = s.sim_time_s / 86400.0 / 365.25;
    hud.status.push_back({"SIM TIME", fmt("J2000 %+.4f yr (%.1f d)", years, s.sim_time_s / 86400.0), "SIMULATED", true});
    hud.status.push_back({"SIM SPEED", fmt("x%.0f", s.warp), "SIMULATED", true});
    // Observer time: observer sits at the camera target; a Newtonian observer
    // sees the selected body at sim time minus its light-travel delay.
    if (s.has_selected_state && s.selected_index >= 0) {
        const double t_obs = s.sim_time_s - s.light_delay_s;
        hud.status.push_back({"OBSERVER TIME", fmt("J2000 %+.4f yr (delay %.3f s)", t_obs / 86400.0 / 365.25, s.light_delay_s),
                              "SIMULATED (Newtonian light-time approx)", true});
    } else {
        hud.status.push_back({"OBSERVER TIME", NOT_AVAILABLE, "—", false});
    }
    hud.status.push_back({"FPS", fmt("%.1f", s.fps), "REAL (measured)", true});
    hud.status.push_back({"FRAME TIME", fmt("%.2f ms", s.frame_ms), "REAL (measured)", true});
    hud.status.push_back({"VIZ MODE", s.viz_mode, "SIMULATED", true});

    if (s.selected_index >= 0 && s.has_selected_kind) {
        hud.selection.push_back({"SELECTED", s.selected_name, "ENGINE ID", true});
        hud.selection.push_back({"TYPE", s.selected_kind, "SIMULATED", true});
        hud.selection.push_back({"CLASSIFICATION", s.selected_classification, "ENGINE", true});
        if (s.has_selected_state) {
            hud.selection.push_back({"HELIO DIST", fmt("%.6f AU", s.r_helio_km / 149597870.7), "SIMULATED", true});
            hud.selection.push_back({"SPEED", fmt("%.3f km/s", s.speed_km_s), "SIMULATED", true});
            hud.selection.push_back({"OBSERVER DIST", fmt("%.3e km", s.observer_distance_km), "SIMULATED", true});
            hud.selection.push_back({"LIGHT DELAY", fmt("%.3f s", s.light_delay_s), "SIMULATED (c=299792.458 km/s)", true});
        } else {
            hud.selection.push_back({"HELIO DIST", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"SPEED", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"OBSERVER DIST", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"LIGHT DELAY", NOT_AVAILABLE, "—", false});
        }
    } else {
        hud.selection.push_back({"SELECTED", "none", "ENGINE ID", true});
        hud.selection.push_back({"TYPE", NOT_AVAILABLE, "—", false});
        hud.selection.push_back({"CLASSIFICATION", NOT_AVAILABLE, "—", false});
        hud.selection.push_back({"HELIO DIST", NOT_AVAILABLE, "—", false});
        hud.selection.push_back({"SPEED", NOT_AVAILABLE, "—", false});
        hud.selection.push_back({"OBSERVER DIST", NOT_AVAILABLE, "—", false});
        hud.selection.push_back({"LIGHT DELAY", NOT_AVAILABLE, "—", false});
    }

    hud.frame.push_back({"CAMERA", s.cam_mode == 0 ? "orbit-follow" : "free", "UI STATE", true});
    hud.frame.push_back({"REF FRAME", s.reference_frame, "ENGINE", true});
    return hud;
}

std::vector<std::string> render_hud_lines(const HudState& hud) {
    std::vector<std::string> out;
    out.push_back(hud.app_status);
    auto emit = [&out](const std::vector<HudRow>& rows) {
        for (const auto& r : rows) {
            if (r.classification.empty() || r.classification == "—" || r.classification == "-")
                out.push_back(r.label + ": " + r.value);
            else
                out.push_back(r.label + ": " + r.value + " [" + r.classification + "]");
        }
    };
    out.push_back("── STATUS ──");      emit(hud.status);
    out.push_back("── SELECTION ──");   emit(hud.selection);
    out.push_back("── CAMERA/FRAME ──"); emit(hud.frame);
    return out;
}

std::string hud_summary_line(const HudSnapshot& s) {
    const double years = s.sim_time_s / 86400.0 / 365.25;
    std::string sel = (s.selected_index >= 0 && s.has_selected_kind) ? s.selected_name : "none";
    return fmt("ASTRA %s | t=J2000%+.3fy | x%.0f | cam=%s | sel=%s | %.0ffps",
               s.paused ? "PAUSED" : "RUN", years, s.warp,
               s.cam_mode == 0 ? "follow" : "free", sel.c_str(), s.fps);
}

} // namespace astra::app
