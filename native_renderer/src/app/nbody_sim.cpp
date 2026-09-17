#include "nbody_sim.h"

#include <cmath>

// Mirror-fidelity note: the Python authority composes Vector3 ops expressed as
// floats; every expression below performs the same floating-point operations in
// the same order (e.g. k = G / (denom * r_mag); a_i = r_vec * (k * m_j)) so the
// native engine reproduces the authority to float rounding.

namespace astra::app {

Vec3m operator+(const Vec3m& a, const Vec3m& b) {
    return Vec3m{a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3m operator-(const Vec3m& a, const Vec3m& b) {
    return Vec3m{a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3m operator*(const Vec3m& a, double s) {
    return Vec3m{a.x * s, a.y * s, a.z * s};
}
double magnitude_sq(const Vec3m& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
double magnitude(const Vec3m& v) { return std::sqrt(magnitude_sq(v)); }

NBodyResult pairwise_acceleration(const Vec3m& pos_i, const Vec3m& pos_j,
                                  double m_i, double m_j,
                                  double G, double softening_m,
                                  Vec3m& out_ai, Vec3m& out_aj) {
    const Vec3m r_vec = pos_j - pos_i;
    const double r2 = magnitude_sq(r_vec);
    const double denom = r2 + softening_m * softening_m;
    if (denom == 0.0) {
        return NBodyResult{false,
            "pairwise_acceleration: zero separation with zero softening"};
    }
    const double r_mag = std::sqrt(denom);
    const double k = G / (denom * r_mag);
    out_ai = r_vec * (k * m_j);
    out_aj = r_vec * (-k * m_i);
    return NBodyResult{true, ""};
}

NBodyResult compute_accelerations(const std::vector<NBody>& bodies,
                                  double G, double softening_m,
                                  std::vector<Vec3m>& out_acc) {
    const size_t n = bodies.size();
    out_acc.assign(n, Vec3m{});
    // Pairs evaluated strictly in index order i < j (determinism contract).
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            Vec3m ai, aj;
            const NBodyResult r = pairwise_acceleration(
                bodies[i].position, bodies[j].position,
                bodies[i].mass_kg, bodies[j].mass_kg, G, softening_m, ai, aj);
            if (!r.ok) {
                return r;
            }
            out_acc[i] = out_acc[i] + ai;
            out_acc[j] = out_acc[j] + aj;
        }
    }
    return NBodyResult{true, ""};
}

bool is_valid_dt(double dt) { return std::isfinite(dt) && dt > 0.0; }

NBodyResult velocity_verlet_step(std::vector<NBody>& bodies,
                                 std::vector<Vec3m>& acc_cache,
                                 double dt, double G, double softening_m) {
    if (!is_valid_dt(dt)) {
        return NBodyResult{false, "dt must be positive finite"};
    }
    const size_t n = bodies.size();
    // a0: reuse the caller-supplied cache only if it matches the system; else
    // compute — the authority always computes a0 inside the step and caches a1
    // for the NEXT step. To reproduce the authority's sequence exactly while
    // keeping the C++ engine allocation-free, the caller passes the cached a
    // from the previous step (bit-identical to what the authority computes).
    if (acc_cache.size() != n) {
        const NBodyResult r = compute_accelerations(bodies, G, softening_m, acc_cache);
        if (!r.ok) return r;
    }

    // Half-kick + drift.
    std::vector<Vec3m> half_vels(n);
    std::vector<Vec3m> new_positions(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec3m v_half = bodies[i].velocity + acc_cache[i] * (0.5 * dt);
        half_vels[i] = v_half;
        new_positions[i] = bodies[i].position + v_half * dt;
    }

    // Accelerations at the new positions.
    std::vector<NBody> mid = bodies;
    for (size_t i = 0; i < n; ++i) mid[i].position = new_positions[i];
    std::vector<Vec3m> a1;
    NBodyResult r = compute_accelerations(mid, G, softening_m, a1);
    if (!r.ok) return r;

    // Second half-kick; commit.
    for (size_t i = 0; i < n; ++i) {
        bodies[i].position = new_positions[i];
        bodies[i].velocity = half_vels[i] + a1[i] * (0.5 * dt);
    }
    acc_cache = a1;  // cache for the next step (authority semantics)
    return NBodyResult{true, ""};
}

double kinetic_energy(const std::vector<NBody>& bodies) {
    double ke = 0.0;
    for (const NBody& b : bodies) {
        ke += 0.5 * b.mass_kg * magnitude_sq(b.velocity);
    }
    return ke;
}

NBodyResult total_potential(const std::vector<NBody>& bodies,
                            double G, double softening_m, double& out_J) {
    double u = 0.0;
    const size_t n = bodies.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const Vec3m r_vec = bodies[j].position - bodies[i].position;
            const double dist = std::sqrt(magnitude_sq(r_vec) +
                                          softening_m * softening_m);
            if (dist <= 0.0) {
                return NBodyResult{false, "pairwise_potential: distance must be positive"};
            }
            u += -G * bodies[i].mass_kg * bodies[j].mass_kg / dist;
        }
    }
    out_J = u;
    return NBodyResult{true, ""};
}

NBodyResult total_energy(const std::vector<NBody>& bodies,
                         double G, double softening_m, double& out_J) {
    double u = 0.0;
    const NBodyResult r = total_potential(bodies, G, softening_m, u);
    if (!r.ok) return r;
    out_J = kinetic_energy(bodies) + u;
    return NBodyResult{true, ""};
}

Vec3m total_linear_momentum(const std::vector<NBody>& bodies) {
    Vec3m p{};
    for (const NBody& b : bodies) p = p + b.velocity * b.mass_kg;
    return p;
}

Vec3m total_angular_momentum(const std::vector<NBody>& bodies) {
    Vec3m L{};
    for (const NBody& b : bodies) {
        const Vec3m& r = b.position;
        const Vec3m p = b.velocity * b.mass_kg;
        // r x p
        L.x += r.y * p.z - r.z * p.y;
        L.y += r.z * p.x - r.x * p.z;
        L.z += r.x * p.y - r.y * p.x;
    }
    return L;
}

} // namespace astra::app
