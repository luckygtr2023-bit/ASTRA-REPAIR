#include "hud_state.h"

#include <cmath>
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
    // ── v1.4 REAL catalog status (provenance always visible, never implied).
    if (s.cat_available) {
        hud.status.push_back({"CATALOG DATASET", fmt("HYG v4.1 + OpenNGC (%u stars, %u DSOs)",
                              (unsigned)s.cat_star_count, (unsigned)s.cat_dso_count), "REAL DATA (CC BY-SA 4.0)", true});
        hud.status.push_back({"CATALOG LAYER 'C'",
                              s.cat_mode == 0 ? "OFF" : (s.cat_mode == 1 ? "STARS" : "STARS+DSO"),
                              "REAL DATA (display shell CINEMATIC)", true});
    } else {
        hud.status.push_back({"CATALOG DATASET", NOT_AVAILABLE, "NOT AVAILABLE (dataset unverified/absent)", false});
    }
    hud.status.push_back({"VIZ MODE", s.viz_mode, "SIMULATED", true});
    hud.status.push_back({"GRAVITY",
        s.gravity_model == "nbody" ? "NBODY velocity-Verlet dt=3600s" : "KEPLER two-body",
        "SIMULATED", true});
    if (s.gravity_model == "nbody" && s.has_nbody_drift) {
        hud.status.push_back({"NBODY TIME", fmt("%.1f d", s.nbody_time_s / 86400.0),
                              "SIMULATED", true});
        hud.status.push_back({"NBODY E DRIFT", fmt("%+.2e rel", s.nbody_drift_rel),
                              "SIMULATED (engine self-diagnostic)", true});
    }

    // v1.2: BH structure/spacetime overlay (F4) — UI rows describe the same
    // overlay the renderer draws (mission: HUD corresponds to what is rendered).
    if (s.bh_overlay_mode != 0) {
        if (s.bh_overlay_avail) {
            hud.status.push_back({"BH STRUCTURE VIZ (F4)",
                fmt("ON — horizon/photon/ISCO at 1/1.5/3 r_s, mag %.3g u/m", s.bh_overlay_mag),
                "UI STATE; geometry PHYSICALLY-MODELED (black_hole_sim), scale CINEMATIC", true});
            hud.status.push_back({"SPACETIME RAYS (F4x2)",
                s.bh_overlay_mode >= 2 ? (s.bh_overlay_rays
                    ? "ON — 5 null geodesics b=3-8 r_s, emitter 25 r_s"
                    : "NOT AVAILABLE (geodesic build failed) — see console")
                                       : "OFF",
                s.bh_overlay_mode >= 2
                    ? (s.bh_overlay_rays ? "PHYSICALLY-MODELED (spacetime_sim RK4); scenario THEORETICAL"
                                         : NOT_AVAILABLE)
                    : "UI STATE", s.bh_overlay_mode < 2 || s.bh_overlay_rays});
        } else {
            hud.status.push_back({"BH STRUCTURE VIZ (F4)", NOT_AVAILABLE,
                                  "(invalid central mass / build failed — see console)", false});
        }
    }

    // ── v1.3 visual-integration rows: HUD describes EXACTLY what the
    // renderer draws this frame — never disconnected values.
    if (s.dk_overlay_mode != 0) {
        if (s.dk_overlay_avail) {
            hud.status.push_back({"DESTRUCTION VIEW (F6)",
                fmt("KE %.3e J dep %.3e J | frag %d ejecta %d | %s",
                    s.dk_energy_j, s.dk_deposited_j, s.dk_fragments, s.dk_ejecta,
                    s.dk_damage_word),
                "SIMULATED (destruction_sim mirror; pinned seed 20240918)", true});
        } else {
            hud.status.push_back({"DESTRUCTION VIEW (F6)", NOT_AVAILABLE,
                                  "(authority geometry refused the pair — no overlap)", false});
        }
    }
    if (s.evo_overlay_mode != 0) {
        if (s.evo_overlay_avail) {
            hud.status.push_back({"EVOLUTION VIEW (F7)",
                fmt("t=%.2f Gyr SFR %.3f | stellar %s", s.evo_t_final_gyr,
                    s.evo_sfr_final, s.evo_phase_word),
                "SIMULATED (evolution_sim); spiral axes CINEMATIC", true});
        } else {
            hud.status.push_back({"EVOLUTION VIEW (F7)", NOT_AVAILABLE,
                                  "(engine step failed — see console)", false});
        }
    }
    if (s.obs_overlay_mode != 0) {
        if (s.obs_overlay_avail) {
            hud.status.push_back({"OBSERVATION VIEW (F8)",
                fmt("subject %s | lookback %.6e s t_emit %.2f s",
                    s.obs_subject, s.obs_lookback_s, s.obs_emission_t_s),
                "SIMULATED (observation_sim temp_observe); ring span CINEMATIC", true});
        } else {
            hud.status.push_back({"OBSERVATION VIEW (F8)", NOT_AVAILABLE,
                                  "(no observer/subject pair — select a body)", false});
        }
    }
    if (s.gala_overlay_mode != 0) {
        if (s.gala_overlay_avail) {
            hud.status.push_back({"GALACTIC VIEW (F9)",
                fmt("%d halos %d filament links", s.gala_halo_count, s.gala_filaments),
                "counts THEORETICAL (evolution_sim halo); placement CINEMATIC grid", true});
        } else {
            hud.status.push_back({"GALACTIC VIEW (F9)", NOT_AVAILABLE,
                                  "(halo build failed — see console)", false});
        }
    }
    if (s.dk_overlay_mode || s.evo_overlay_mode || s.obs_overlay_mode || s.gala_overlay_mode) {
        hud.status.push_back({"U13 OVERLAY CPU",
            fmt("build %.2f ms advance %.2f ms upload %.2f ms",
                s.u13_build_ms, s.u13_advance_ms, s.u13_upload_ms),
            "REAL (measured; GPU timing NOT VERIFIED)", true});
    }

    // ── v1.3 visual-integration rows: HUD describes EXACTLY what the
    // renderer draws this frame — never disconnected values.
    if (s.dk_overlay_mode != 0) {
        if (s.dk_overlay_avail) {
            hud.status.push_back({"DESTRUCTION VIEW (F6)",
                fmt("KE %.3e J dep %.3e J | frag %d ejecta %d | %s",
                    s.dk_energy_j, s.dk_deposited_j, s.dk_fragments, s.dk_ejecta,
                    s.dk_damage_word),
                "SIMULATED (destruction_sim mirror; pinned seed 20240918)", true});
        } else {
            hud.status.push_back({"DESTRUCTION VIEW (F6)", NOT_AVAILABLE,
                                  "(authority geometry refused the pair — no overlap)", false});
        }
    }
    if (s.evo_overlay_mode != 0) {
        if (s.evo_overlay_avail) {
            hud.status.push_back({"EVOLUTION VIEW (F7)",
                fmt("t=%.2f Gyr SFR %.3f | stellar %s", s.evo_t_final_gyr,
                    s.evo_sfr_final, s.evo_phase_word),
                "SIMULATED (evolution_sim); spiral axes CINEMATIC", true});
        } else {
            hud.status.push_back({"EVOLUTION VIEW (F7)", NOT_AVAILABLE,
                                  "(engine step failed — see console)", false});
        }
    }
    if (s.obs_overlay_mode != 0) {
        if (s.obs_overlay_avail) {
            hud.status.push_back({"OBSERVATION VIEW (F8)",
                fmt("subject %s | lookback %.6e s t_emit %.2f s",
                    s.obs_subject, s.obs_lookback_s, s.obs_emission_t_s),
                "SIMULATED (observation_sim temp_observe); ring span CINEMATIC", true});
        } else {
            hud.status.push_back({"OBSERVATION VIEW (F8)", NOT_AVAILABLE,
                                  "(no observer/subject pair — select a body)", false});
        }
    }
    if (s.gala_overlay_mode != 0) {
        if (s.gala_overlay_avail) {
            hud.status.push_back({"GALACTIC VIEW (F9)",
                fmt("%d halos %d filament links", s.gala_halo_count, s.gala_filaments),
                "counts THEORETICAL (evolution_sim halo); placement CINEMATIC grid", true});
        } else {
            hud.status.push_back({"GALACTIC VIEW (F9)", NOT_AVAILABLE,
                                  "(halo build failed — see console)", false});
        }
    }
    if (s.dk_overlay_mode || s.evo_overlay_mode || s.obs_overlay_mode || s.gala_overlay_mode) {
        hud.status.push_back({"U13 OVERLAY CPU",
            fmt("build %.2f ms advance %.2f ms upload %.2f ms",
                s.u13_build_ms, s.u13_advance_ms, s.u13_upload_ms),
            "REAL (measured; GPU timing NOT VERIFIED)", true});
    }

    if (s.selected_index >= 0 && s.has_selected_kind) {
        hud.selection.push_back({"SELECTED", s.selected_name, "ENGINE ID", true});
        hud.selection.push_back({"TYPE", s.selected_kind, "SIMULATED", true});
        hud.selection.push_back({"CLASSIFICATION", s.selected_classification, "ENGINE", true});
        if (s.has_selected_state) {
            hud.selection.push_back({"HELIO DIST", fmt("%.6f AU", s.r_helio_km / 149597870.7), "SIMULATED", true});
            hud.selection.push_back({"SPEED", fmt("%.3f km/s", s.speed_km_s), "SIMULATED", true});
            hud.selection.push_back({"OBSERVER DIST", fmt("%.3e km", s.observer_distance_km), "SIMULATED", true});
            hud.selection.push_back({"LIGHT DELAY", fmt("%.3f s", s.light_delay_s), "SIMULATED (c=299792.458 km/s)", true});
            if (s.has_sel_sr) {
                hud.selection.push_back({"SR GAMMA-1", fmt("%+.3e", s.sel_gamma_minus_one),
                    "PHYSICALLY-MODELED (SR Lorentz)", true});
            } else {
                hud.selection.push_back({"SR GAMMA-1", NOT_AVAILABLE, "—", false});
            }
            if (s.has_sel_grav) {
                hud.selection.push_back({"GRAV DIL-1", fmt("%+.3e", s.sel_grav_dilation_minus_one),
                    "PHYSICALLY-MODELED (weak-field Schwarzschild exterior)", true});
            } else {
                hud.selection.push_back({"GRAV DIL-1", NOT_AVAILABLE, "—", false});
            }
            if (s.has_sel_bh) {
                hud.selection.push_back({"BH R_S (CTR)", fmt("%.3e m", s.sel_bh_rs_m),
                    "PHYSICALLY-MODELED (Schwarzschild scale of central mass; never claimed a real BH)", true});
                hud.selection.push_back({"BH ISCO (CTR)", fmt("%.3e m", s.sel_bh_isco_m),
                    "PHYSICALLY-MODELED (static Schwarzschild, test particle)", true});
                hud.selection.push_back({"BH PHOT SPH (CTR)", fmt("%.3e m", s.sel_bh_photon_m),
                    "PHYSICALLY-MODELED (static Schwarzschild, test particle)", true});
                hud.selection.push_back({"BH MODEL (CTR)", "SCHWARZSCHILD (spin=0 modeled)",
                    "SIMULATED (engine model choice)", true});
                hud.selection.push_back({"BH KERR SPIN", NOT_AVAILABLE,
                    "— (central spin not modeled by the N-body engine)", false});
            } else {
                hud.selection.push_back({"BH R_S (CTR)", NOT_AVAILABLE, "—", false});
                hud.selection.push_back({"BH ISCO (CTR)", NOT_AVAILABLE, "—", false});
                hud.selection.push_back({"BH PHOT SPH (CTR)", NOT_AVAILABLE, "—", false});
                hud.selection.push_back({"BH MODEL (CTR)", NOT_AVAILABLE, "—", false});
                hud.selection.push_back({"BH KERR SPIN", NOT_AVAILABLE, "—", false});
            }
        } else {
            hud.selection.push_back({"HELIO DIST", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"SPEED", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"OBSERVER DIST", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"LIGHT DELAY", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"SR GAMMA-1", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"GRAV DIL-1", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"BH R_S (CTR)", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"BH ISCO (CTR)", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"BH PHOT SPH (CTR)", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"BH MODEL (CTR)", NOT_AVAILABLE, "—", false});
            hud.selection.push_back({"BH KERR SPIN", NOT_AVAILABLE, "—", false});
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

    // ── v1.4 catalog selection measurement (observer-relative; REAL parallax).
    if (s.cat_available && s.cat_sel) {
        hud.selection.push_back({"V14 STAR", s.cat_sel_label, "REAL DATA (HYG v4.1)", true});
        if (s.cat_sel_spect[0]) {
            hud.selection.push_back({"SPECTRAL TYPE", s.cat_sel_spect, "REAL DATA (MK class)", true});
        } else {
            hud.selection.push_back({"SPECTRAL TYPE", NOT_AVAILABLE, "—", false});
        }
        hud.selection.push_back({"RA/DEC FROM OBSERVER",
            fmt("%.6f / %+.6f deg%s", s.cat_sel_ra_deg, s.cat_sel_dec_deg,
                s.cat_meas_epoch_y == 0.0 ? "" : fmt(" @J2000%+.0fy", s.cat_meas_epoch_y).c_str()),
            "DATA-DERIVED (from real catalog position; ICRS)", true});
        if (!std::isnan(s.cat_sel_dist_ly)) {
            hud.selection.push_back({"DISTANCE (OBSERVER)", fmt("%.4f ly", s.cat_sel_dist_ly), "DATA-DERIVED (real position)", true});
            hud.selection.push_back({"LIGHT-TRAVEL TIME", fmt("%.4f yr", s.cat_sel_delay_y), "DATA-DERIVED (=distance/c)", true});
        } else {
            hud.selection.push_back({"DISTANCE (OBSERVER)", NOT_AVAILABLE, "NOT AVAILABLE (dist >= 100000 pc sentinel)", false});
            hud.selection.push_back({"LIGHT-TRAVEL TIME", NOT_AVAILABLE, "—", false});
        }
        if (!std::isnan(s.cat_sel_mag)) {
            hud.selection.push_back({"APPARENT MAG (OBSERVER)", fmt("%.3f", s.cat_sel_mag), "DATA-DERIVED (distance modulus)", true});
        } else {
            hud.selection.push_back({"APPARENT MAG (OBSERVER)", NOT_AVAILABLE, "—", false});
        }
        hud.selection.push_back({"FORMAL UNCERTAINTY", NOT_AVAILABLE, "NOT AVAILABLE (CSV distribution has none)", false});
    } else if (s.cat_available && s.cat_mode > 0) {
        hud.selection.push_back({"V14 STAR", "none ('U' measures center-of-view)", "—", false});
    }
    if (s.cat_available && s.dso_sel) {
        hud.selection.push_back({"V14 DSO", s.dso_sel_label, "REAL DATA (OpenNGC)", true});
        if (!std::isnan(s.dso_sel_z)) {
            hud.selection.push_back({"REDSHIFT Z", fmt("%+.6f", s.dso_sel_z), "REAL DATA (measured)", true});
            hud.selection.push_back({"DIST PROXY", fmt("%.2f Mpc", s.dso_sel_dist_proxy_mpc), "DATA-DERIVED (H0=70 Hubble proxy)", true});
        } else {
            hud.selection.push_back({"REDSHIFT Z", NOT_AVAILABLE, "NOT AVAILABLE (not measured)", false});
        }
        const char* ty = "OTHER";
        switch (s.dso_sel_type_code) {
            case 7: ty = "GALAXY"; break;
            case 11: ty = "PLANETARY NEBULA"; break;
            case 4: ty = "OPEN CLUSTER"; break;
            case 5: ty = "GLOBULAR CLUSTER"; break;
            case 15: ty = "NEBULA"; break;
            case 17: ty = "SUPERNOVA REMNANT"; break;
            default: break;
        }
        hud.selection.push_back({"DSO TYPE", ty, "REAL DATA (catalog class)", true});
        if (!std::isnan(s.dso_sel_vmag)) {
            hud.selection.push_back({"V MAGNITUDE", fmt("%.2f", s.dso_sel_vmag), "REAL DATA", true});
        } else {
            hud.selection.push_back({"V MAGNITUDE", NOT_AVAILABLE, "—", false});
        }
        if (!std::isnan(s.dso_sel_maj_arcmin)) {
            hud.selection.push_back({"ANGULAR SIZE", fmt("%.2f arcmin", s.dso_sel_maj_arcmin), "REAL DATA", true});
        }
        hud.selection.push_back({"HUBBLE-LAW Z->D", "linear approx H0=70", "DATA-DERIVED (approximation)", true});
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
