#pragma once
// ASTRA COSMOS — Orbit/focus camera (visualization layer; never mutates science).
//
// Mirrors the navigation concepts of astra/interaction/navigation.py at the
// renderer boundary: target focus, azimuth/elevation orbit, exponential zoom.
// All output matrices are float, built from double-precision input positions
// that have already been rebased relative to the camera target (floating
// origin at the visualization boundary — no absolute km coordinates in floats).

#include <cmath>

namespace astra::app {

struct Mat4 {
    float m[16]; // column-major, as Vulkan expects
    static Mat4 identity();
    static Mat4 multiply(const Mat4& a, const Mat4& b);
    // Right-handed perspective, Vulkan clip space (depth [0,1], Y down handled
    // by negative projection Y scale — standard Vulkan practice).
    static Mat4 perspective_vk(float fov_y_rad, float aspect, float z_near, float z_far);
    static Mat4 look_at(float ex, float ey, float ez,
                        float cx, float cy, float cz,
                        float ux, float uy, float uz);
};

struct OrbitCamera {
    double azimuth = 0.6;      // radians
    double elevation = 0.35;   // radians, clamped to ±1.5
    double distance = 450.0;   // render units from target
    double fov_deg = 55.0;
    double z_near = 0.05;
    double z_far = 100000.0;

    void rotate(double d_az, double d_el);
    void zoom(double factor);           // >1 moves out, <1 moves in
    void clamp_distance(double min_d, double max_d);
    // Eye position relative to the camera target, in render units.
    void eye_offset(float out3[3]) const;
    Mat4 view() const;                   // target translated to origin
    Mat4 projection(float aspect) const;
};

} // namespace astra::app
