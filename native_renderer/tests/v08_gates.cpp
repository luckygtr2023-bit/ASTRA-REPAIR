// ASTRA COSMOS v0.9 gates — exact N-body state persistence (bit-exact resume).
// Core contract: save mid-evolution -> load -> continue must equal the
// uninterrupted run with ZERO tolerance (identical bits, not "close enough").
#include "app/nbody_sim.h"
#include "app/celestial_sim.h"
#include "app/persist.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra::app;

static bool v_eq(const Vec3m& a, const Vec3m& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };
    const std::vector<CelestialBody> scene = make_solar_system();

    // ---- A. Pack/unpack roundtrip ----
    gate("pack/unpack roundtrip");
    {
        NBodyEngine eng;
        eng.seed(scene, 0.0);
        eng.advance_to(3.5 * 86400.0);  // non grid-aligned endpoint
        const std::string p = pack_nbody_state(eng);
        CHECK(!p.empty(), "pack non-empty");
        std::vector<NBody> nb; double t = 0;
        CHECK(unpack_nbody_state(p, scene, nb, t), "unpack ok");
        CHECK(t == 3.5 * 86400.0, "time exact");
        CHECK(nb.size() == scene.size(), "count exact");
        bool same = true;
        for (size_t i = 0; i < nb.size(); ++i) {
            same = same && v_eq(nb[i].position, eng.bodies()[i].position) &&
                           v_eq(nb[i].velocity, eng.bodies()[i].velocity) &&
                           nb[i].mass_kg == scene[i].mass_kg &&
                           nb[i].id == scene[i].name;
        }
        CHECK(same, "state bit-exact roundtrip");
        total += 5;
    }

    // ---- B. THE contract: save/load/resume == continuous (zero tolerance) ----
    gate("bit-exact resume");
    {
        NBodyEngine cont;   // continuous reference
        cont.seed(scene, 0.0);
        cont.advance_to(7.25 * 86400.0);              // evolve to save point
        const std::string saved_state = pack_nbody_state(cont);
        cont.advance_to(7.25 * 86400.0 + 400000.0);   // keep going

        // "Load" path: scratch engine, restore, continue the same interval.
        NBodyEngine resumed;
        std::vector<NBody> nb; double t = 0;
        unpack_nbody_state(saved_state, scene, nb, t);
        CHECK(resumed.restore(nb, t), "restore ok");
        CHECK(resumed.time_s() == 7.25 * 86400.0, "resumed at save t");
        CHECK(resumed.advance_to(t + 400000.0), "resume advances");
        bool same = true;
        for (size_t i = 0; i < scene.size(); ++i)
            same = same && v_eq(cont.bodies()[i].position, resumed.bodies()[i].position) &&
                           v_eq(cont.bodies()[i].velocity, resumed.bodies()[i].velocity);
        CHECK(same, "resume trajectory bit-identical to continuous");
        total += 4;
        // Resume determinism through a full save->load->save cycle.
        const std::string repacked = pack_nbody_state(resumed);
        NBodyEngine again;
        std::vector<NBody> nb2; double t2 = 0;
        unpack_nbody_state(repacked, scene, nb2, t2);
        again.restore(nb2, t2);
        again.advance_to(t2 + 86400.0);
        cont.advance_to(7.25 * 86400.0 + 400000.0 + 86400.0);
        same = true;
        for (size_t i = 0; i < scene.size(); ++i)
            same = same && v_eq(cont.bodies()[i].position, again.bodies()[i].position);
        CHECK(same, "double cycle bit-identical");
        total += 1;
    }

    // ---- C. Restore validation (never silently accept corrupt state) ----
    gate("restore validation");
    {
        NBodyEngine eng;
        std::vector<NBody> empty;
        CHECK(!eng.restore(empty, 0.0), "empty rejected");
        std::vector<NBody> one = to_nbody_state(scene, 0.0);
        one[0].mass_kg = 0.0;
        CHECK(!eng.restore(one, 0.0), "zero mass rejected");
        one = to_nbody_state(scene, 0.0);
        one[0].position.x = NAN;
        CHECK(!eng.restore(one, 0.0), "NaN position rejected");
        one = to_nbody_state(scene, 0.0);
        one[3].velocity.y = INFINITY;
        CHECK(!eng.restore(one, 0.0), "inf velocity rejected");
        one = to_nbody_state(scene, 0.0);
        CHECK(!eng.restore(one, NAN), "NaN time rejected");
        CHECK(eng.restore(one, 123.0) && eng.seeded() && eng.time_s() == 123.0,
              "valid restore accepted");
        total += 6;
    }

    // ---- D. Unpack validation ----
    gate("unpack validation");
    {
        std::vector<NBody> nb; double t;
        CHECK(!unpack_nbody_state("", scene, nb, t), "empty rejected by unpack");
        CHECK(!unpack_nbody_state("1,2,3", scene, nb, t), "short rejected");
        std::string p;
        {   // build a valid string then corrupt it
            NBodyEngine eng; eng.seed(scene, 0.0); eng.advance_to(86400.0);
            p = pack_nbody_state(eng);
        }
        // Structural tamper: drop the final whole token -> count law 1+6k fails.
        std::string trunc = p;
        trunc.erase(trunc.rfind(','));
        CHECK(!unpack_nbody_state(trunc, scene, nb, t), "truncated token rejected");
        std::string bad = p; bad[10] = 'X';
        CHECK(!unpack_nbody_state(bad, scene, nb, t), "non-numeric rejected");
        // Wrong scene (body count mismatch) rejected explicitly.
        std::vector<CelestialBody> small_scene(scene.begin(), scene.end() - 1);
        CHECK(!unpack_nbody_state(p, small_scene, nb, t), "scene mismatch rejected");
        total += 5;
    }

    // ---- E. Persist integration of nbody_state ----
    gate("persist nbody_state");
    {
        // Full scenario roundtrip with a real engine state embedded.
        ScenarioSave s;
        s.gravity_model = "nbody";
        NBodyEngine eng; eng.seed(scene, 0.0); eng.advance_to(86400.0 * 2.0);
        s.nbody_state = pack_nbody_state(eng);
        const std::string ser = serialize_scenario(s);
        CHECK(ser.find("nbody_state=") != std::string::npos, "field written");
        ScenarioSave back;
        CHECK(deserialize_scenario(ser, back), "scenario parses");
        CHECK(back.nbody_state == s.nbody_state, "state string survives byte-exact");
        // Contradiction: state present but model kepler = tamper, rejected.
        std::string bad = ser;
        bad.replace(bad.find("gravity_model=nbody"), 19, "gravity_model=kepler");
        CHECK(!deserialize_scenario(bad, back), "kepler+state rejected");
        // Old v0.8 file (gravity_model but no nbody_state) still loads.
        std::string v8 = ser;
        const size_t nbs = v8.find("nbody_state=");
        const size_t nle = v8.find('\n', nbs);
        v8.erase(nbs, nle - nbs + 1);
        ScenarioSave v8s;
        CHECK(deserialize_scenario(v8, v8s) && v8s.nbody_state.empty(), "v0.8 file ok");
        // Structurally corrupt state rejected by the parser itself.
        std::string bad2 = ser;
        bad2.replace(bad2.find("nbody_state="), 12, "nbody_state=1,2,3,");
        CHECK(!deserialize_scenario(bad2, back), "corrupt state rejected");
        total += 7;
    }

    std::printf("v08_gates: %d checks, %d failures\n", total, g_fail);
    if (g_fail == 0) {
        std::printf("v08_gates: ALL %d checks PASSED\n", total);
        return 0;
    }
    return 1;
}
