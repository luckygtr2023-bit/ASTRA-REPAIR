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
    // v1.1: relativistic derived quantities (pure functions of authoritative
    // state — no new simulation state; mission Phase 5/6). PHYSICALLY-MODELED
    // classifications. Each flag is independent: e.g. selecting the SUN keeps
    // SR valid (v=0) but makes GRAV NOT AVAILABLE (r=0 degenerate metric —
    // exact authority semantics, never a fabricated value).
    bool has_sel_sr = false;
    double sel_gamma_minus_one = 0.0;        // SR γ−1 from heliocentric speed
    bool has_sel_grav = false;
    double sel_grav_dilation_minus_one = 0.0; // weak-field dt/dτ − 1 at r_helio (Sun mass)
    // v1.2: black-hole boundary scales of the CENTRAL body (pure functions of
    // its authoritative mass through the native mirror of astra.blackhole; no
    // new simulation state; mission Phase 5/6). PHYSICALLY-MODELED
    // classifications. The N-body engine models central spin as ZERO, so the
    // modeled model is SCHWARZSCHILD and Kerr rows are explicitly
    // NOT AVAILABLE (spin not modeled by the engine — honest taxonomy).
    bool has_sel_bh = false;
    double sel_bh_rs_m = 0.0;      // r_s = 2GM/c^2 of the central body
    double sel_bh_isco_m = 0.0;    // r_ISCO = 3 r_s
    double sel_bh_photon_m = 0.0;  // r_ps = 1.5 r_s (photon sphere scale)
    // v1.2: F4 BH structure/spacetime OVERLAY global UI state (the drawn
    // geometry itself is PHYSICALLY-MODELED from the native mirrors via
    // app/bh_viz; exposed here is ONLY the UI mode + the exact CINEMATIC
    // magnification — no fabricated rendering data).
    int  bh_overlay_mode = 0;      // 0 off / 1 structure shells / 2 + geodesic rays
    bool bh_overlay_avail = false; // mirror cache built and valid
    bool bh_overlay_rays = false;  // null-geodesic ray set built and valid
    double bh_overlay_mag = 0.0;   // render units per metre (CINEMATIC transform)

    // ── v1.3: DESTRUCTION/IMPACT overlay UI state (F6) — geometry SIMULATED
    // via the verified destruction_sim mirror; UI state classification rows
    // mirror what the renderer draws (no invented telemetry).
    int  dk_overlay_mode = 0;          // 0 off / 1 impact view (+ fragment stream)
    bool dk_overlay_avail = false;     // build reached (mirror accepted event)
    int  dk_fragments = 0;             // SIMULATED output count
    int  dk_ejecta = 0;                // SIMULATED output count
    double dk_energy_j = 0.0;          // kinetic_energy_j from the mirror
    double dk_deposited_j = 0.0;       // deposited_energy_j
    char dk_damage_word[24] = "INTACT";// SIMULATED damage word from the mirror

    // ── v1.3: EVOLUTION overlay UI state (F7) — series SIMULATED via the
    // verified evolution_sim mirror; spiral axes are CINEMATIC transforms.
    int  evo_overlay_mode = 0;         // 0 off / 1 evolution view
    bool evo_overlay_avail = false;
    double evo_t_final_gyr = 0.0;
    char evo_phase_word[24] = "MAIN_SEQUENCE";
    double evo_sfr_final = 0.0;

    // ── v1.3: OBSERVATION overlay UI state (F8) — SIMULATED via the verified
    // observation_sim mirror (temp_observe on the real ephemeris worldline).
    int  obs_overlay_mode = 0;         // 0 off / 1 observation view
    bool obs_overlay_avail = false;
    double obs_lookback_s = 0.0;
    double obs_emission_t_s = 0.0;
    char obs_subject[24] = "";

    // ── v1.3: GALACTIC halo overlay UI state (F9) — structure THEORETICAL via
    // evolution_sim halo step; placement grid CINEMATIC (documented in source).
    int  gala_overlay_mode = 0;        // 0 off / 1 galactic view
    bool gala_overlay_avail = false;
    int  gala_halo_count = 0;
    int  gala_filaments = 0;

    // ── v1.3: CPU-side overlay cost counters (REAL measurements).
    double u13_build_ms = 0.0;         // last overlay rebuild wall cost
    double u13_advance_ms = 0.0;       // last per-frame advance cost
    double u13_upload_ms = 0.0;        // last per-frame upload cost

    // ── v1.4: REAL astronomical catalog (astra.catalog authority, HYG v4.1 +
    //    OpenNGC; sha256-verified). classifications REAL/DATA_DERIVED/MODELED.
    bool     cat_available = false;    // catalog binaries loaded+verified
    char     cat_status_word[64] = "NOT AVAILABLE"; // load failure why (or star counts)
    int      cat_mode = 0;             // 0 off / 1 stars / 2 stars+DSOs ('C')
    uint32_t cat_star_count = 0, cat_dso_count = 0;
    // Catalog selection ('U': nearest object to screen center; measurement
    // from the camera-target observer — REAL parallax in the angles).
    bool     cat_sel = false;
    char     cat_sel_label[64] = "";   // "HYG 32263 / HIP 32349"
    char     cat_sel_spect[32] = "";
    double   cat_sel_ra_deg = 0.0, cat_sel_dec_deg = 0.0; // FROM OBSERVER (icrs)
    double   cat_sel_dist_ly = 0.0;    // NaN → NOT AVAILABLE
    double   cat_sel_delay_y = 0.0;    // NaN → NOT AVAILABLE
    double   cat_sel_mag = 0.0;        // NaN → NOT AVAILABLE
    double   cat_meas_epoch_y = 0.0;   // 'T' observation epoch offset (DATA_DERIVED PM view)
    // DSO selection (OpenNGC)
    bool     dso_sel = false;
    char     dso_sel_label[64] = "";
    double   dso_sel_z = 0.0;          // NaN → not measured
    double   dso_sel_radvel = 0.0;     // NaN → not measured
    double   dso_sel_vmag = 0.0;       // NaN → NOT AVAILABLE
    double   dso_sel_maj_arcmin = 0.0; // NaN → NOT AVAILABLE
    double   dso_sel_dist_proxy_mpc = 0.0; // NaN when no z (DATA_DERIVED Hubble proxy)
    uint32_t dso_sel_type_code = 0;

    // ── v1.5: EXTREME SPACETIME + EXPLORATION (mirror of interaction journey
    //    authority). THEORETICAL geometry / SPECULATIVE feasibility labels are
    //    attached here, not implied by visuals.
    bool     trv_armed = false;        // any journey live (not IDLE)
    int      trv_mech = 0;             // 0 conventional / 1 wormhole / 2 warp
    int      trv_state = 0;            // TravState ordinal (0..7)
    char     trv_causal[192] = "NOT AVAILABLE";
    double   trv_gamma = 0.0;          // NaN → NOT AVAILABLE (warp: gamma 1)
    double   trv_t_coord = 0.0;        // coordinate time (journey clock, s)
    double   trv_t_proper = 0.0;       // proper time (s)
    double   trv_throat_km = 0.0;      // NaN when not wormhole
    double   trv_redshift = 0.0;       // NaN when not wormhole
    double   trv_tidal = 0.0;          // m/s^2 at throat (NaN → NOT AVAILABLE)
    double   trv_bubble_km = 0.0;      // NaN when not warp
    double   trv_wall = 0.0;           // sigma 1/m (NaN when not warp)
    double   trv_eff_rate_c = 0.0;     // effective displacement rate /c (NaN when not warp)
    bool     trv_observer_travel = false; // eye overridden by journey
    char     trv_origin_name[48] = "NOT AVAILABLE";
    char     trv_dest_name[48] = "NOT AVAILABLE";
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
