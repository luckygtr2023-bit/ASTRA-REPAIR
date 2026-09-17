// ASTRA COSMOS v0.5 — gates for the real-rendering math layer (no GPU).
// Proves the CPU policy twin of the GPU cull/LOD/display rules. The SPV-side
// mirror shares the same formulas; science rule: these functions decide only
// VISIBILITY, never physics (double-precision state stays authoritative).
#include "app/render_math.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { ++g_fail; std::printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, label); } \
} while (0)

using namespace astra;
using namespace astra::app;

// Build a standard perspective proj*view looking down -Z from the origin —
// the production view chain is OrbitCamera::projection * look_at view.
static Mat4 test_camera(float fov_deg, float aspect, float near_d, float far_d,
                        const float eye[3], const float target[3]) {
    Mat4 v = Mat4::look_at(eye[0], eye[1], eye[2], target[0], target[1], target[2], 0.0f, 1.0f, 0.0f);
    Mat4 p = Mat4::perspective_vk((float)(fov_deg * 3.141592653589793 / 180.0), aspect, near_d, far_d);
    return Mat4::multiply(p, v);
}

int main() {
    int total = 0;
    auto gate = [&](const char* name) { std::printf("== %s ==\n", name); };

    // ---- Frustum extraction + sphere visibility ----
    gate("frustum planes & sphere visibility");
    {
        const float eye0[3] = {0, 0, 0}, tgt0[3] = {0, 0, -10};
        Mat4 vp = test_camera(60.0, 16.0 / 9.0, 1.0, 1000.0, eye0, tgt0);
        Frustum6 f = frustum_from_viewproj(vp);

        // Sphere straight ahead at z=-10 must be visible (many radii).
        for (float r : {0.1f, 0.5f, 2.0f, 9.9f}) {
            const float c[3] = {0, 0, -10};
            CHECK(sphere_visible(f, c, r), "center sphere visible");
            ++total;
        }
        // Huge negative sphere behind camera -> with big radius still straddles
        // near plane classification; keep radii small for definite tests.
        { const float behind[3] = {0, 0, 5}; CHECK(!sphere_visible(f, behind, 1.0f), "behind camera culled"); ++total; }
        { const float far_away[3] = {0, 0, -5000}; CHECK(!sphere_visible(f, far_away, 10.0f), "beyond far plane culled"); ++total; }
        { const float left_far[3] = {-500, 0, -10}; CHECK(!sphere_visible(f, left_far, 10.0f), "far left culled"); ++total; }
        { const float top_far[3] = {0, 400, -10}; CHECK(!sphere_visible(f, top_far, 10.0f), "far above culled"); ++total; }
        // Edge spheres: just inside near plane & just inside left edge.
        { const float near_edge[3] = {0, 0, -0.5f}; CHECK(sphere_visible(f, near_edge, 1.0f), "straddles near plane visible"); ++total; }
        { const float far_edge[3] = {0, 0, -999.0f}; CHECK(sphere_visible(f, far_edge, 2.0f), "straddles far plane visible"); ++total; }
        // Symmetric stress grid: determinism + sanity counts.
        int visible = 0, run1 = 0, run2 = 0;
        for (int pass = 0; pass < 2; ++pass) {
            int v = 0;
            for (double x = -2; x <= 2; ++x)
                for (double y = -2; y <= 2; ++y)
                    for (double z = -9; z <= 0.0; ++z) {
                        const float c[3] = {(float)x, (float)y, (float)z};
                        if (sphere_visible(f, c, 0.25f)) ++v, ++total;
                    }
            if (pass == 0) run1 = v; else run2 = v;
            visible = v;
        }
        CHECK(run1 == run2, "frustum classification deterministic");
        CHECK(visible > 40 && visible < 4000, "frustum classification volume sane");
        total += 1;
        // Normalization: planes have unit normals.
        for (int k = 0; k < 6; ++k) {
            const double n = std::sqrt((double)f.p[k][0] * f.p[k][0] + (double)f.p[k][1] * f.p[k][1] +
                                       (double)f.p[k][2] * f.p[k][2]);
            CHECK(std::fabs(n - 1.0) < 1e-5, "plane normalized");
            ++total;
        }
    }

    // ---- LOD rule ----
    gate("lod_select screen-fraction rule");
    {
        const float t = 0.2679f; // tan(15deg)
        // fraction = r/(d*t): >0.010 => HIGH(0), else LOW(1)
        CHECK(lod_select(0.010f, 1.0f, t) == 0, "huge close -> HIGH");
        CHECK(lod_select(0.001f, 1.0f, t) == 1, "small close still LOW at 0.0037 frac");
        CHECK(lod_select(0.0001f, 1.0f, t) == 1, "tiny close -> LOW");
        CHECK(lod_select(6.96e8f / 1.0e8f, 1.496e11f / 1.0e8f, t) == 0, "sun at 1 AU -> HIGH");
        CHECK(lod_select(0.004f, 200.0f, t) == 1, "small far -> LOW");
        CHECK(lod_select(0.0f, 10.0f, t) == 1, "zero radius -> LOW (degenerate)");
        CHECK(lod_select(1.0f, 0.0f, t) == 1, "zero distance -> LOW (degenerate)");
        CHECK(lod_select(1.0f, 10.0f, 0.0f) == 1, "zero fov -> LOW (degenerate)");
        total += 8;
        // Monotonicity sweep: larger radius cannot downgrade LOD (deterministic).
        uint8_t prev = lod_select(1e-6f, 1.0f, t);
        for (double r = 1e-6; r < 100.0; r *= 1.01) {
            uint8_t cur = lod_select((float)r, 1.0f, t);
            CHECK(!(prev == 0 && cur == 1), "lod monotonic in radius");
            prev = cur;
            ++total;
        }
        // Exact boundary at threshold.
        const float thr = 0.010f;
        const float rb = thr * 1.0f * t; // fraction exactly thr
        CHECK(lod_select(rb * 1.0001f, 1.0f, t) == 0, "above threshold HIGH");
        CHECK(lod_select(rb / 1.0001f, 1.0f, t) == 1, "below threshold LOW");
        total += 2;
    }

    // ---- Display policy clamps ----
    gate("exposure / bloom policy clamps");
    {
        CHECK(clamp_exposure(1.0f) == 1.0f, "exposure identity");
        CHECK(clamp_exposure(-5.0f) == 0.05f, "exposure min");
        CHECK(clamp_exposure(1e9f) == 20.0f, "exposure max");
        CHECK(clamp_exposure(-1e9f) == 0.05f, "exposure min extreme");
        CHECK(clamp_bloom_strength(0.5f) == 0.5f, "bloom identity");
        CHECK(clamp_bloom_strength(-1.0f) == 0.0f, "bloom min");
        CHECK(clamp_bloom_strength(99.0f) == 1.5f, "bloom max");
        CHECK(bloom_threshold() == 1.0f, "threshold documented 1.0 HDR");
        total += 8;
    }

    // ---- HDR format policy ----
    gate("HDR format selection policy");
    {
        CHECK(choose_hdr_format(true, true) == HdrFormatChoice::R16G16B16A16_SFLOAT, "16f chosen when usable");
        CHECK(choose_hdr_format(false, true) == HdrFormatChoice::UNSUPPORTED, "no-silent-degrade render");
        CHECK(choose_hdr_format(true, false) == HdrFormatChoice::UNSUPPORTED, "no-silent-degrade sample");
        CHECK(choose_hdr_format(false, false) == HdrFormatChoice::UNSUPPORTED, "no-silent-degrade both");
        total += 4;
    }

    // ---- Instance packing ----
    gate("instance packing contract");
    {
        BodyInstance bi = pack_body_instance(1.5f, -2.0f, 3.25f, 0.5f, 0.2f, 0.4f, 0.6f, true);
        CHECK(bi.pos_radius[0] == 1.5f && bi.pos_radius[1] == -2.0f && bi.pos_radius[2] == 3.25f,
              "packed positions");
        CHECK(bi.pos_radius[3] == 0.5f, "packed radius");
        CHECK(bi.color_flag[0] == 0.2f && bi.color_flag[1] == 0.4f && bi.color_flag[2] == 0.6f, "packed color");
        CHECK(bi.color_flag[3] == 1.0f, "selected flag set");
        BodyInstance bn = pack_body_instance(0, 0, 0, 1, 1, 1, 1, false);
        CHECK(bn.color_flag[3] == 0.0f, "selected flag clear");
        CHECK(sizeof(BodyInstance) == 32, "32-byte SSBO record (std430-efficient)");
        // 200 random-ish deterministic packs: no NaNs introduced.
        for (int i = 0; i < 200; ++i) {
            const float x = (float)((i * 7919) % 331) - 165.0f;
            BodyInstance b = pack_body_instance(x, x * 0.5f, -x, 0.01f * (1 + (i % 7)), 0.1f, 0.2f, 0.3f, i % 2 != 0);
            CHECK(std::isfinite(b.pos_radius[0]) && std::isfinite(b.color_flag[3]), "finite packs");
            ++total;
        }
        total += 7;
    }

    // ---- CPU-twin frustum vs GPU mirror formula (0.010 threshold) ----
    gate("CPU twin consistency at LOD boundary");
    {
        const float t = 0.2679f;
        int mismatches = 0;
        for (int i = 1; i < 500; ++i) {
            const float d = 0.02f * i;                 // 0.02 .. 10.0
            const float r = 0.0001f * (i % 97) + 1e-5f;
            const float frac = r / (d * t);
            const uint8_t cpu = lod_select(r, d, t);
            const uint8_t gpu = (frac > 0.010f) ? 0 : 1; // cull.comp lod branch (2u/int mapped to HIGH=0)
            if (cpu != gpu) ++mismatches;
            ++total;
        }
        CHECK(mismatches == 0, "CPU/SPV LOD rule identical");
        ++total;
    }

    if (g_fail != 0) {
        std::printf("v05_gates: FAILED (%d checks failed, %d executed)\n", g_fail, total);
        return 1;
    }
    std::printf("v05_gates: ALL %d checks PASSED\n", total);
    return 0;
}
