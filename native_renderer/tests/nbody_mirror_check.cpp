// ASTRA COSMOS — N-body mirror fidelity gate (native engine vs Python
// authority `astra.nbody`). Same contract as tests/kepler_mirror_check.cpp:
// the C++ NBodyEngine, seeded by to_nbody_state(make_solar_system(), 0), must
// reproduce scripts/gen_nbody_reference.py output to < 1e-12 relative in
// position AND velocity at every logged time. Deterministic: same CSV, same
// results, forever.

#include "app/celestial_sim.h"
#include "app/nbody_sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace astra::app;

namespace {

struct RefRow {
    std::string body;
    double t_s;
    double x, y, z, vx, vy, vz, e_j;
};

std::vector<RefRow> load_csv(const char* path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        std::exit(2);
    }
    std::vector<RefRow> rows;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        RefRow r;
        std::string tok;
        std::vector<std::string> toks;
        while (std::getline(ss, tok, ',')) toks.push_back(tok);
        if (toks.size() != 9) continue;
        r.body = toks[0];
        r.t_s = std::stod(toks[1]);
        r.x = std::stod(toks[2]); r.y = std::stod(toks[3]); r.z = std::stod(toks[4]);
        r.vx = std::stod(toks[5]); r.vy = std::stod(toks[6]); r.vz = std::stod(toks[7]);
        r.e_j = std::stod(toks[8]);
        rows.push_back(r);
    }
    return rows;
}

// Relative error |a-b| / (|b| + eps) — eps guards degenerate-zero authority values.
double rel_err(double a, double b) {
    return std::fabs(a - b) / (std::fabs(b) + 1e-30);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <nbody_reference.csv>\n", argv[0]);
        return 2;
    }
    const std::vector<RefRow> rows = load_csv(argv[1]);
    if (rows.empty()) { std::fprintf(stderr, "empty reference\n"); return 2; }

    // Native side: build the exact same system and integrate the exact policy.
    const std::vector<CelestialBody> scene = make_solar_system();
    NBodyEngine eng;
    eng.seed(scene, 0.0);

    // Logged times in file order (unique ascending).
    std::vector<double> times;
    for (const RefRow& r : rows) {
        if (times.empty() || r.t_s != times.back()) times.push_back(r.t_s);
    }

    int checks = 0, fails = 0;
    double worst = 0.0;
    std::string worst_desc;

    // t = 0: seed-state equality against the authority-composed states.
    {
        const std::vector<NBody>& cur = eng.bodies();
        for (const RefRow& r : rows) {
            if (r.t_s != 0.0) continue;
            const NBody* nb = nullptr;
            for (const NBody& b : cur) if (b.id == r.body) nb = &b;
            ++checks;
            if (!nb) { ++fails; continue; }
            const double e1 = rel_err(nb->position.x, r.x);
            const double e2 = rel_err(nb->position.y, r.y);
            const double e3 = rel_err(nb->position.z, r.z);
            const double e4 = rel_err(nb->velocity.x, r.vx);
            const double e5 = rel_err(nb->velocity.y, r.vy);
            const double e6 = rel_err(nb->velocity.z, r.vz);
            const double w = std::max({e1, e2, e3, e4, e5, e6});
            if (w > worst) { worst = w; worst_desc = r.body + " seed"; }
            if (w > 1e-12) ++fails;
        }
    }

    // Integrate to each subsequent logged time and compare every coordinate.
    for (size_t ti = 1; ti < times.size(); ++ti) {
        const double target = times[ti];
        if (!eng.advance_to(target)) { ++fails; ++checks; continue; }
        const std::vector<NBody>& cur = eng.bodies();
        for (const RefRow& r : rows) {
            if (r.t_s != target) continue;
            const NBody* nb = nullptr;
            for (const NBody& b : cur) if (b.id == r.body) nb = &b;
            ++checks;
            if (!nb) { ++fails; continue; }
            const double w = std::max({
                rel_err(nb->position.x, r.x), rel_err(nb->position.y, r.y),
                rel_err(nb->position.z, r.z), rel_err(nb->velocity.x, r.vx),
                rel_err(nb->velocity.y, r.vy), rel_err(nb->velocity.z, r.vz)});
            if (w > worst) {
                worst = w;
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s t=%.0f", r.body.c_str(), target);
                worst_desc = buf;
            }
            if (w > 1e-12) ++fails;
        }
        // Energy column vs native engine diagnostics at the same instant.
        for (const RefRow& r : rows) {
            if (r.t_s != target) continue;
            double e = 0.0;
            ++checks;
            if (!total_energy(cur, G_SI, DEFAULT_SOFTENING_M, e).ok ||
                rel_err(e, r.e_j) > 1e-12) ++fails;
            const double w = rel_err(e, r.e_j);
            if (w > worst) { worst = w; worst_desc = r.body + " energy"; }
            break;  // same value in every row of this time set
        }
    }

    // Engine self-diagnostics: bounded drift (symplectic bound, honest print).
    const double drift = eng.energy_drift_rel();
    std::printf("nbody_mirror_check: %d rows, %d checks, %d fails\n",
                (int)rows.size(), checks, fails);
    std::printf("  worst rel err: %.3e (%s)\n", worst, worst_desc.c_str());
    std::printf("  engine energy drift (rel, %.0f s -> %.0f s): %.3e\n",
                times.front(), times.back(), drift);
    if (fails) { std::printf("RESULT: FAIL\n"); return 1; }
    if (std::fabs(drift) > 1e-6) {  // deliberately loose; printed value is the record
        std::printf("RESULT: FAIL (energy drift %.3e)\n", drift);
        return 1;
    }
    std::printf("RESULT: PASS\n");
    return 0;
}
