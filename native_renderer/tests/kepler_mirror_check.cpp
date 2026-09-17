// ASTRA COSMOS — cross-language fidelity gate.
//
// Compares the native C++ orbital mirror (src/app/celestial_sim.cpp) against
// reference CSV generated from the Python scientific authority
// (scripts/gen_kepler_reference.py, which composes astra.orbital
// solve_kepler_elliptic + eccentric_to_true + the elements.py PQW->IJK
// rotation). Tolerance: 1e-9 relative per component magnitude.
//
// Usage: kepler_mirror_check <reference.csv>
// Exit:  0 = PASS (max error printed), 1 = FAIL, 2 = usage/IO error.

#include "app/celestial_sim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Row {
    std::string body;
    double t;
    double x, y, z;
    double vx, vy, vz;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <kepler_reference.csv>\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }

    const auto bodies = astra::app::make_solar_system();
    std::string line;
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == 'b') continue; // header
        std::stringstream ss(line);
        std::string cell;
        Row r{};
        if (!std::getline(ss, r.body, ',')) continue;
        std::getline(ss, cell, ','); r.t = std::stod(cell);
        std::getline(ss, cell, ','); r.x = std::stod(cell);
        std::getline(ss, cell, ','); r.y = std::stod(cell);
        std::getline(ss, cell, ','); r.z = std::stod(cell);
        std::getline(ss, cell, ','); r.vx = std::stod(cell);
        std::getline(ss, cell, ','); r.vy = std::stod(cell);
        std::getline(ss, cell);      r.vz = std::stod(cell);
        rows.push_back(r);
    }
    if (rows.empty()) {
        std::fprintf(stderr, "no data rows in %s\n", argv[1]);
        return 2;
    }

    const double TOL = 1.0e-9;
    double worst = 0.0;
    int fails = 0, checks = 0;
    for (const Row& r : rows) {
        const astra::app::CelestialBody* body = nullptr;
        for (const auto& b : bodies) {
            if (b.name == r.body) { body = &b; break; }
        }
        if (!body) {
            std::fprintf(stderr, "FAIL: body %s not in C++ table\n", r.body.c_str());
            fails++;
            continue;
        }
        // orbital_position: inertial position in the body's parent frame — the same
        // convention the Python generator used for its rows.
        const astra::app::Vec3d p = astra::app::orbital_position(body->elements, r.t);
        const double dx = p[0] - r.x, dy = p[1] - r.y, dz = p[2] - r.z;
        const double err = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double mag = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        const double rel = (mag > 0.0) ? err / mag : err;
        ++checks;
        if (rel > worst) worst = rel;
        if (rel > TOL) {
            std::printf("FAIL pos %s t=%g: rel=%.3e  C++(%.12e %.12e %.12e) PY(%.12e %.12e %.12e)\n",
                        r.body.c_str(), r.t, rel, p[0], p[1], p[2], r.x, r.y, r.z);
            ++fails;
        }
        // Velocity gate (v0.3): C++ orbital_velocity vs Python elements_to_state.
        const astra::app::Vec3d vv = astra::app::orbital_velocity(body->elements, r.t);
        const double dvx = vv[0] - r.vx, dvy = vv[1] - r.vy, dvz = vv[2] - r.vz;
        const double verr = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);
        const double vmag = std::sqrt(r.vx * r.vx + r.vy * r.vy + r.vz * r.vz);
        const double vrel = (vmag > 0.0) ? verr / vmag : verr;
        ++checks;
        if (vrel > worst) worst = vrel;
        if (vrel > TOL) {
            std::printf("FAIL vel %s t=%g: rel=%.3e  C++(%.9e %.9e %.9e) PY(%.9e %.9e %.9e)\n",
                        r.body.c_str(), r.t, vrel, vv[0], vv[1], vv[2], r.vx, r.vy, r.vz);
            ++fails;
        }
    }

    // Parent-chain check: world position of Moon = Earth + geocentric offset.
    {
        const auto& earth = bodies[3];
        const auto& moon = bodies[9];
        const auto world = astra::app::propagate_world(bodies, 86400.0);
        const astra::app::Vec3d manual = {
            astra::app::orbital_position(earth.elements, 86400.0)[0] +
                astra::app::orbital_position(moon.elements, 86400.0)[0],
            astra::app::orbital_position(earth.elements, 86400.0)[1] +
                astra::app::orbital_position(moon.elements, 86400.0)[1],
            astra::app::orbital_position(earth.elements, 86400.0)[2] +
                astra::app::orbital_position(moon.elements, 86400.0)[2]};
        for (int k = 0; k < 3; ++k) {
            const double e_err = std::fabs(world[9][k] - manual[k]);
            const double rel = e_err / std::fabs(manual[k]);
            ++checks;
            if (rel > TOL) {
                std::printf("FAIL parent-chain moon[%d]: rel=%.3e\n", k, rel);
                ++fails;
            }
        }
        // Parent-chain velocity: world velocity of Moon = Earth + geocentric.
        const auto vworld = astra::app::propagate_world_velocity(bodies, 86400.0);
        for (int k = 0; k < 3; ++k) {
            const double mv = astra::app::orbital_velocity(earth.elements, 86400.0)[k] +
                              astra::app::orbital_velocity(moon.elements, 86400.0)[k];
            const double e_err = std::fabs(vworld[9][k] - mv);
            const double rel = e_err / std::fabs(mv);
            ++checks;
            if (rel > TOL) {
                std::printf("FAIL parent-chain moon velocity[%d]: rel=%.3e\n", k, rel);
                ++fails;
            }
        }
        // Determinism: repeated propagation must be bit-identical.
        const auto w2 = astra::app::propagate_world(bodies, 86400.0);
        for (int k = 0; k < 3; ++k) {
            ++checks;
            if (w2[9][k] != world[9][k]) {
                std::printf("FAIL determinism[%d]\n", k);
                ++fails;
            }
        }
    }

    std::printf("kepler_mirror_check: %d checks, %d fails, max_rel_err=%.3e (tol=%.0e)\n",
                checks, fails, worst, TOL);
    if (fails > 0) {
        std::printf("RESULT: FAIL\n");
        return 1;
    }
    std::printf("RESULT: PASS\n");
    return 0;
}
