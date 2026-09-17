#include "orbit_camera.h"

namespace astra::app {

Mat4 Mat4::identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

Mat4 Mat4::perspective_vk(float fov_y_rad, float aspect, float zn, float zf) {
    Mat4 r{};
    const float t = 1.0f / std::tan(fov_y_rad * 0.5f);
    r.m[0] = t / aspect;
    r.m[5] = -t; // Vulkan: clip Y points down
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}

Mat4 Mat4::look_at(float ex, float ey, float ez,
                   float cx, float cy, float cz,
                   float ux, float uy, float uz) {
    // f = normalize(c - e), s = normalize(f × u), u' = s × f
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    const float ux2 = sy * fz - sz * fy, uy2 = sz * fx - sx * fz, uz2 = sx * fy - sy * fx;
    Mat4 r = Mat4::identity();
    r.m[0] = sx;  r.m[4] = sy;  r.m[8] = sz;
    r.m[1] = ux2; r.m[5] = uy2; r.m[9] = uz2;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux2 * ex + uy2 * ey + uz2 * ez);
    r.m[14] = (fx * ex + fy * ey + fz * ez);
    return r;
}

void OrbitCamera::rotate(double d_az, double d_el) {
    azimuth += d_az;
    elevation += d_el;
    if (elevation > 1.5) elevation = 1.5;
    if (elevation < -1.5) elevation = -1.5;
    const double two_pi = 6.283185307179586476925;
    while (azimuth > two_pi) azimuth -= two_pi;
    while (azimuth < 0.0) azimuth += two_pi;
}

void OrbitCamera::zoom(double factor) {
    distance *= factor;
}

void OrbitCamera::clamp_distance(double min_d, double max_d) {
    if (distance < min_d) distance = min_d;
    if (distance > max_d) distance = max_d;
}

void OrbitCamera::eye_offset(float out3[3]) const {
    const double ce = std::cos(elevation);
    out3[0] = static_cast<float>(distance * ce * std::cos(azimuth));
    out3[1] = static_cast<float>(distance * std::sin(elevation));
    out3[2] = static_cast<float>(distance * ce * std::sin(azimuth));
}

Mat4 OrbitCamera::view() const {
    float off[3];
    eye_offset(off);
    // Camera target is the floating origin: eye at +off, looking at 0.
    return Mat4::look_at(off[0], off[1], off[2], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
}

Mat4 OrbitCamera::projection(float aspect) const {
    const double deg = 3.14159265358979323846 / 180.0;
    return Mat4::perspective_vk(static_cast<float>(fov_deg * deg), aspect,
                                static_cast<float>(z_near), static_cast<float>(z_far));
}

} // namespace astra::app
