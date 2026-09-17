// ASTRA COSMOS — v0.4 unit gates: HUD model, audio bus, persistence, LUT.
// All pure app-layer modules; no I/O beyond temp files under /tmp.

#include "app/hud_state.h"
#include "app/audio_bus.h"
#include "app/persist.h"
#include "app/star_lut.h"

#include <cmath>
#include <cstdio>
#include <string>

static int fails = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_hud() {
    using astra::app::HudSnapshot;
    using astra::app::build_hud;
    using astra::app::render_hud_lines;
    using astra::app::NOT_AVAILABLE;

    // Case 1: no selection -> selection fields strictly NOT AVAILABLE.
    HudSnapshot s{};
    s.sim_time_s = 86400.0 * 365.25 * 2.5;
    s.warp = 50000.0;
    s.fps = 61.4; s.frame_ms = 16.28;
    auto hud = build_hud(s);
    const auto lines = render_hud_lines(hud);
    bool saw_na_type = false, saw_na_speed = false, saw_na_obs = false, saw_sim = false;
    for (const auto& l : lines) {
        if (l.find("TYPE: NOT AVAILABLE") != std::string::npos) saw_na_type = true;
        if (l.find("SPEED: NOT AVAILABLE") != std::string::npos) saw_na_speed = true;
        if (l.find("OBSERVER TIME: NOT AVAILABLE") != std::string::npos) saw_na_obs = true;
        if (l.find("SIM TIME: J2000 +2.5000 yr") != std::string::npos) saw_sim = true;
    }
    CHECK(saw_na_type); CHECK(saw_na_speed); CHECK(saw_na_obs); CHECK(saw_sim);
    CHECK(std::string(NOT_AVAILABLE) == "NOT AVAILABLE");

    // Case 2: with selection -> observer time = sim time - light delay.
    HudSnapshot s2 = s;
    s2.selected_index = 3;
    s2.selected_name = "Earth";
    s2.selected_kind = "PLANET";
    s2.has_selected_kind = true;
    s2.has_selected_state = true;
    s2.r_helio_km = 149597870.7;      // exactly 1 AU
    s2.speed_km_s = 29.783;
    s2.observer_distance_km = 299792.458; // exactly 1 light-second
    s2.light_delay_s = 1.0;
    auto hud2 = build_hud(s2);
    const auto lines2 = render_hud_lines(hud2);
    bool saw_au = false, saw_delay = false, saw_sel = false;
    for (const auto& l : lines2) {
        if (l.find("HELIO DIST: 1.000000 AU") != std::string::npos) saw_au = true;
        if (l.find("LIGHT DELAY: 1.000 s") != std::string::npos) saw_delay = true;
        if (l.find("SELECTED: Earth") != std::string::npos) saw_sel = true;
    }
    CHECK(saw_au); CHECK(saw_delay); CHECK(saw_sel);
}

static void test_audio() {
    using astra::app::AudioBus;
    using astra::app::AudioClass;
    using astra::app::AudioEvent;
    using astra::app::AudioEventKind;
    AudioBus bus;
    CHECK(bus.push(AudioEventKind::UI_SELECT, "Earth", 10.0));
    CHECK(bus.push(AudioEventKind::SIM_PAUSE, "clock", 10.0));
    // Dishonest: REAL_ACOUSTIC in vacuum must be REJECTED and counted.
    CHECK(!bus.push(AudioEventKind::VACUUM_ACOUSTIC_REQUEST, "space", 10.0));
    CHECK(bus.dropped_dishonest() == 1);
    // Dishonest: sonification without data source.
    CHECK(!bus.push(AudioEventKind::SONIFICATION_REQUEST, "", 10.0));
    CHECK(bus.dropped_dishonest() == 2);
    // Sonification with data source goes through with REAL classification.
    CHECK(bus.push(AudioEventKind::SONIFICATION_REQUEST, "pulsar-B1919+21", 10.0));
    CHECK(bus.size() == 3);
    AudioEvent e{};
    CHECK(bus.pop(e)); CHECK(e.kind == AudioEventKind::UI_SELECT && e.subject == "Earth" && e.classification == AudioClass::CINEMATIC);
    CHECK(bus.pop(e)); CHECK(e.kind == AudioEventKind::SIM_PAUSE);
    CHECK(bus.pop(e)); CHECK(e.kind == AudioEventKind::SONIFICATION_REQUEST && e.classification == AudioClass::REAL_SIGNAL_SONIFICATION);
    CHECK(!bus.pop(e));
    // Ring overflow: newest survive, oldest dropped deterministically.
    for (int i = 0; i < 512; ++i) CHECK(bus.push(AudioEventKind::SIM_STEP, std::to_string(i), 0.0));
    CHECK(bus.size() == AudioBus::kCapacity);
    CHECK(bus.pop(e)); CHECK(e.subject == std::to_string(512 - (int)AudioBus::kCapacity));
}

static void test_persist() {
    using astra::app::ScenarioSave;
    ScenarioSave s{};
    s.sim_time_s = 1.23456789012345e9;
    s.warp = 1e6; s.paused = true;
    s.focus = 3; s.selection = 3; s.cam_mode = 1;
    s.cam_azimuth = 0.1234567; s.cam_elevation = -0.5; s.cam_distance = 77.7;
    s.free_pos[0] = 1.25f; s.free_pos[1] = -2.5f; s.free_pos[2] = 3.75f;
    s.show_vectors = false; s.viz_mode = "velocity";
    const std::string text = astra::app::serialize_scenario(s);
    ScenarioSave r{};
    CHECK(astra::app::deserialize_scenario(text, r));
    CHECK(r.sim_time_s == s.sim_time_s);
    CHECK(r.warp == s.warp && r.paused == s.paused);
    CHECK(r.focus == 3 && r.selection == 3 && r.cam_mode == 1);
    CHECK(std::fabs(r.cam_azimuth - s.cam_azimuth) < 1e-12);
    CHECK(std::fabs(r.free_pos[2] - 3.75f) < 1e-6f);
    CHECK(r.show_vectors == false);
    CHECK(r.viz_mode == "velocity");
    // Tamper rejection: missing key fails (no silent discard).
    const std::string tam = text.substr(0, text.find("warp=")) + text.substr(text.find("paused="));
    ScenarioSave junk{};
    CHECK(!astra::app::deserialize_scenario(tam, junk));
    CHECK(!astra::app::deserialize_scenario("garbage", junk));
    // Path traversal rejection.
    CHECK(!astra::app::is_safe_save_name("../evil.json"));
    CHECK(!astra::app::is_safe_save_name("sub/dir.json"));
    CHECK(!astra::app::is_safe_save_name("/abs.json"));
    CHECK(!astra::app::is_safe_save_name(".."));
    CHECK(astra::app::is_safe_save_name("scenario_1.json"));
    // Real round-trip through a temp dir.
    CHECK(astra::app::save_scenario_file("/tmp", "v04_gate.json", s));
    ScenarioSave back{};
    CHECK(astra::app::load_scenario_file("/tmp", "v04_gate.json", back));
    CHECK(back.sim_time_s == s.sim_time_s && back.selection == 3);
    CHECK(!astra::app::load_scenario_file("/tmp", "../v04_gate.json", back));
}

static void test_star_lut() {
    using astra::app::RgbF;
    using astra::app::load_ppm_p3;
    using astra::app::star_color_from_lut;
    const char* candidates[] = {
        "native_renderer/assets/star_temperature_lut.ppm",
        "../native_renderer/assets/star_temperature_lut.ppm",
        "../../native_renderer/assets/star_temperature_lut.ppm",
        "/home/user/ASTRA-REPAIR/native_renderer/assets/star_temperature_lut.ppm"};
    int w = 0, h = 0; std::string px;
    bool loaded = false;
    for (const char* p : candidates) if (load_ppm_p3(p, w, h, px)) { loaded = true; break; }
    CHECK(loaded);
    if (!loaded) return;
    CHECK(w == 16 && h == 256);                 // asset exactly as documented
    RgbF cold = star_color_from_lut(px, w, h, 2000);   // bottom of range
    RgbF hot = star_color_from_lut(px, w, h, 40000);   // top of range
    RgbF sun = star_color_from_lut(px, w, h, 5778);    // Sun T_eff
    CHECK(cold.r > 0.9f && cold.b < hot.b);     // cool stars red-dominated
    CHECK(hot.b > 0.9f && hot.b > hot.r);       // hot stars blue-dominated
    CHECK(sun.r >= 0.9f && sun.g > 0.2f && sun.g < 0.6f); // mid ramp (documented stylized approx)
}

int main() {
    test_hud();
    test_audio();
    test_persist();
    test_star_lut();
    std::printf("v04_gates: %d checks, %d fails\n", checks, fails);
    std::printf("RESULT: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
