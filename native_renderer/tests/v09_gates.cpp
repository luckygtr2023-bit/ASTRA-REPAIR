// ASTRA COSMOS v1.0 gates — NBODY HUD self-diagnostics + engine time law.
#include "app/nbody_sim.h"
#include "app/celestial_sim.h"
#include "app/hud_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra::app;

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };
    const std::vector<CelestialBody> scene = make_solar_system();

    // ---- A. HUD diagnostics rows appear only for nbody + diagnostics ----
    gate("hud nbody diagnostics rows");
    {
        HudSnapshot snap;
        snap.gravity_model = "nbody";
        snap.has_nbody_drift = true;
        snap.nbody_drift_rel = -2.9e-13;
        snap.nbody_time_s = 86400.0 * 30.0;
        HudState h = build_hud(snap);
        bool time_row = false, drift_row = false;
        for (const HudRow& r : h.status) {
            if (r.label == "NBODY TIME") {
                time_row = true;
                CHECK(r.available, "time row available");
                CHECK(std::string(r.value).find("30.0") != std::string::npos,
                      "time value formatted");
            }
            if (r.label == "NBODY E DRIFT") {
                drift_row = true;
                CHECK(r.available, "drift row available");
                CHECK(std::string(r.value).find("e-13") != std::string::npos ||
                      std::string(r.value).find("e-012") != std::string::npos,
                      "drift scientific fmt");
                CHECK(std::string(r.classification).find("SIMULATED") != std::string::npos,
                      "drift classified");
            }
        }
        CHECK(time_row && drift_row, "rows present for nbody");
        total += 6;

        snap.gravity_model = "kepler";
        h = build_hud(snap);
        for (const HudRow& r : h.status) {
            CHECK(r.label != "NBODY TIME" && r.label != "NBODY E DRIFT",
                  "rows absent for kepler");
            ++total;
        }
        snap.gravity_model = "nbody";
        snap.has_nbody_drift = false;
        h = build_hud(snap);
        for (const HudRow& r : h.status) {
            CHECK(r.label != "NBODY E DRIFT", "drift row absent without data");
            ++total;
        }
    }

    // ---- B. Engine time law: exact step arithmetic ----
    gate("engine time law");
    {
        NBodyEngine eng;
        eng.seed(scene, 0.0);
        eng.advance_to(86400.0);
        CHECK(eng.time_s() == 86400.0, "24 full steps land exactly");
        eng.advance_to(86400.0 * 1.5);
        CHECK(eng.time_s() == 86400.0 * 1.5, "partial landing exact");
        // Time arithmetic stays exact within double precision far into the run.
        eng.advance_to(365.25 * 86400.0 * 10.0 / 2.0);
        CHECK(std::fabs(eng.time_s() - 365.25 * 86400.0 * 5.0) < 1e-3,
              "5-year time sane");
        total += 3;
    }

    // ---- C. Drift instrumentation feeds real engine numbers ----
    gate("drift instrumentation feeds real values");
    {
        NBodyEngine eng;
        eng.seed(scene, 0.0);
        eng.advance_to(365.25 * 86400.0);
        const double d = eng.energy_drift_rel();
        CHECK(d != 0.0, "drift nonzero (measured, not stubbed)");
        CHECK(std::fabs(d) < 1e-8, "1yr drift bounded");
        // Fresh engine reports exactly 0 (no fabricated number).
        NBodyEngine fresh;
        fresh.seed(scene, 0.0);
        CHECK(fresh.energy_drift_rel() == 0.0, "fresh == 0");
        // Unseeded engine: diagnostics are zeroed honestly, not garbage.
        NBodyEngine dead;
        CHECK(dead.energy_drift_rel() == 0.0 && !dead.seeded(), "unseeded honest zero");
        total += 5;
    }

    std::printf("v09_gates: %d checks, %d failures\n", total, g_fail);
    if (g_fail == 0) {
        std::printf("v09_gates: ALL %d checks PASSED\n", total);
        return 0;
    }
    return 1;
}
