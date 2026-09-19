// ASTRA v1.7 — PERFORMANCE + CINEMATIC RENDERER native gate battery.
//
// Groups:
//  1  halton + jitter parity vs fixture (exact)
//  2  adaptive-quality controller scripted parity vs fixture (EMA exact, decisions exact)
//  3  LOD policy + interpolation alpha parity
//  4  adversarial / fail-closed (invalid inputs, controller garbage guard)
//  5  determinism (two-run identical controller sequences)
//  6  MEASURED performance: controller update, halton, compact_lod (128 bodies)
//  7  shader file presence + CMake registration (text contract)
//
// Exit nonzero on any failure.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/frame_pacing.h"
#include "app/render_math.h"

using astra::vizperf::AdaptiveQualityController;
using astra::vizperf::QualityDecision;

static int g_checks = 0, g_fail = 0;
static void C(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}
static bool feq(double a, double b) { return a == b; }

static const char* dec_name(QualityDecision d) {
    switch (d) { case QualityDecision::LOWER: return "LOWER"; case QualityDecision::RAISE: return "RAISE"; default: return "HOLD"; }
}

int main(int argc, char** argv) {
    const std::string fixture = (argc > 1) ? argv[1] : "native_renderer/tests/fixtures/v17_perf_reference.txt";
    const std::string root = (argc > 2) ? argv[2] : ".";

    std::ifstream f(fixture);
    C((bool)f, "fixture exists");

    struct CStep { double measured; std::string dec; int quality, cd; double ema; };
    std::unordered_map<std::string, std::vector<CStep>> scripts;
    std::unordered_map<std::string, std::pair<double, int>> ctrl_heads;
    struct D { int base, idx; double v; };
    std::vector<D> haltons;
    struct J { int idx; double x, y; };
    std::vector<J> jitters;
    struct L { double size; int cls; };
    std::vector<L> lods;
    struct A { double acc, dt, v; };
    std::vector<A> alphas;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> p; std::stringstream ss(line); std::string cell;
        while (std::getline(ss, cell, ',')) p.push_back(cell);
        if (p[0] == "halton" && p.size() == 4)
            haltons.push_back({std::atoi(p[1].c_str()), std::atoi(p[2].c_str()), std::strtod(p[3].c_str(), nullptr)});
        else if (p[0] == "jitter" && p.size() == 4)
            jitters.push_back({std::atoi(p[1].c_str()), std::strtod(p[2].c_str(), nullptr), std::strtod(p[3].c_str(), nullptr)});
        else if (p[0] == "ctrl" && p.size() == 4)
            ctrl_heads[p[1]] = {std::strtod(p[2].c_str(), nullptr), std::atoi(p[3].c_str())};
        else if (p[0] == "cstep" && p.size() == 7)
            scripts[p[1]].push_back({std::strtod(p[2].c_str(), nullptr), p[3], std::atoi(p[4].c_str()),
                                     std::atoi(p[5].c_str()), std::strtod(p[6].c_str(), nullptr)});
        else if (p[0] == "lod" && p.size() == 3)
            lods.push_back({std::strtod(p[1].c_str(), nullptr), std::atoi(p[2].c_str())});
        else if (p[0] == "alpha" && p.size() == 4)
            alphas.push_back({std::strtod(p[1].c_str(), nullptr), std::strtod(p[2].c_str(), nullptr),
                              std::strtod(p[3].c_str(), nullptr)});
    }

    // ---------------- group 1 ----------------
    int parity = 0;
    for (const auto& h : haltons) {
        double v = 0.0;
        C(astra::vizperf::halton(h.idx, h.base, v), "halton computes");
        ++g_checks; ++parity;
        if (!feq(v, h.v)) { ++g_fail; std::printf("FAIL: halton(%d,%d) %.17g != %.17g\n", h.idx, h.base, v, h.v); }
    }
    for (const auto& j : jitters) {
        double x = 0.0, y = 0.0;
        C(astra::vizperf::sub_pixel_jitter(j.idx, x, y), "jitter computes");
        ++g_checks; parity += 2;
        if (!feq(x, j.x) || !feq(y, j.y)) { ++g_fail; std::printf("FAIL: jitter(%d)\n", j.idx); }
    }

    // ---------------- group 2 ----------------
    int steps = 0;
    for (const auto& kv : scripts) {
        const auto head = ctrl_heads[kv.first];
        AdaptiveQualityController ctrl(head.first, head.second);
        C(ctrl.ok(), "controller init ok");
        for (const auto& st : kv.second) {
            const QualityDecision d = ctrl.update(st.measured);
            ++steps; g_checks += 4;
            if (std::string(dec_name(d)) != st.dec) { ++g_fail; std::printf("FAIL: %s step dec %s want %s\n", kv.first.c_str(), dec_name(d), st.dec.c_str()); }
            if (ctrl.quality() != st.quality) { ++g_fail; std::printf("FAIL: %s quality %d want %d\n", kv.first.c_str(), ctrl.quality(), st.quality); }
            if (ctrl.cooldown() != st.cd) { ++g_fail; std::printf("FAIL: %s cooldown %d want %d\n", kv.first.c_str(), ctrl.cooldown(), st.cd); }
            if (!feq(ctrl.ema_ms(), st.ema)) { ++g_fail; std::printf("FAIL: %s ema %.17g want %.17g\n", kv.first.c_str(), ctrl.ema_ms(), st.ema); }
        }
    }
    std::printf("parity: %d halton/jitter, %d controller steps (all exact)\n", parity, steps);

    // ---------------- group 3 ----------------
    for (const auto& l : lods) {
        ++g_checks;
        const int got = astra::vizperf::lod_class_for_size(l.size);
        if (got != l.cls) { ++g_fail; std::printf("FAIL: lod(%g)=%d want %d\n", l.size, got, l.cls); }
    }
    for (const auto& a : alphas) {
        ++g_checks;
        const double got = astra::vizperf::interpolation_alpha(a.acc, a.dt);
        if (!feq(got, a.v)) { ++g_fail; std::printf("FAIL: alpha(%g,%g)=%.17g want %.17g\n", a.acc, a.dt, got, a.v); }
    }

    // ---------------- group 4 ----------------
    {
        double v;
        C(!astra::vizperf::halton(0, 2, v), "halton idx 0 refused");
        C(!astra::vizperf::halton(1, 1, v), "halton base 1 refused");
        C(!astra::vizperf::sub_pixel_jitter(0, v, v), "jitter idx 0 refused");
        AdaptiveQualityController bad(0.0);
        C(!bad.ok(), "controller zero target refused");
        AdaptiveQualityController ok(16.6667);
        C(ok.update(std::numeric_limits<double>::quiet_NaN()) == QualityDecision::HOLD, "NaN ms guarded");
        C(ok.update(-1.0) == QualityDecision::HOLD, "negative ms guarded");
        C(astra::vizperf::interpolation_alpha(std::numeric_limits<double>::infinity(), 1.0) == 0.0, "alpha inf acc");
        C(astra::vizperf::interpolation_alpha(1.0, 0.0) == 0.0, "alpha zero dt");
        C(astra::vizperf::lod_class_for_size(-1.0) == 0, "lod negative -> CULL");
        C(astra::vizperf::lod_class_for_size(std::numeric_limits<double>::quiet_NaN()) == 0, "lod NaN -> CULL");
        C(astra::vizperf::lod_class_for_size(std::numeric_limits<double>::infinity()) == 0, "lod inf -> CULL");
    }

    // ---------------- group 5 (determinism) ----------------
    {
        AdaptiveQualityController a(16.6667, 3), b(16.6667, 3);
        bool same = true;
        for (int i = 0; i < 65; ++i)
            if (a.update(25.0) != b.update(25.0) || a.ema_ms() != b.ema_ms()) { same = false; break; }
        C(same, "controller two-run identical");
    }

    // ---------------- group 6 (MEASURED performance) ----------------
    {
        AdaptiveQualityController ctrl(16.6667, 3);
        auto t0 = std::chrono::steady_clock::now();
        volatile QualityDecision sink;
        for (int i = 0; i < 2000000; ++i) sink = ctrl.update(16.0 + (i & 7));
        auto t1 = std::chrono::steady_clock::now();
        const double ctrl_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / 2000000.0;

        double hv = 0.0;
        t0 = std::chrono::steady_clock::now();
        for (int i = 1; i <= 1000000; ++i) { astra::vizperf::halton(i, 2, hv); }
        t1 = std::chrono::steady_clock::now();
        const double halton_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / 1000000.0;

        // compact_lod measured on the existing render_math mirror (deterministic serial pass)
        uint8_t lod[128];
        for (int i = 0; i < 128; ++i) lod[i] = static_cast<uint8_t>(i % 3);
        astra::app::CompactResult cr{};
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200000; ++i) { if (!astra::app::compact_lod(lod, 128, cr)) { ++g_fail; break; } }
        t1 = std::chrono::steady_clock::now();
        const double lod_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / 200000.0;

        std::printf("MEASURED: controller update %.1f ns; halton %.1f ns; compact_lod(128) %.0f ns\n",
                    ctrl_ns, halton_ns, lod_ns);
        C(ctrl_ns < 1000.0 && halton_ns < 1000.0 && lod_ns < 100000.0, "measured perf within sanity bounds");
    }

    // ---------------- group 7 (text contract) ----------------
    {
        std::ifstream cm((root + "/native_renderer/CMakeLists.txt"));
        const std::string cms((std::istreambuf_iterator<char>(cm)), std::istreambuf_iterator<char>());
        C(cms.find("v17_taa_resolve.frag") != std::string::npos, "CMake registers taa shader");
        C(cms.find("v17_sharpen_cas.frag") != std::string::npos, "CMake registers cas shader");
        std::ifstream s1((root + "/native_renderer/src/shaders/v17_taa_resolve.frag"));
        std::ifstream s2((root + "/native_renderer/src/shaders/v17_sharpen_cas.frag"));
        C((bool)s1 && (bool)s2, "v17 shaders on disk");
        const std::string t1s((std::istreambuf_iterator<char>(s1)), std::istreambuf_iterator<char>());
        C(t1s.find("NOT VERIFIED") != std::string::npos, "taa shader honest status label");
    }

    std::printf("v17 gates: %d checks, %d failures\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL V1.7 CHECKS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
