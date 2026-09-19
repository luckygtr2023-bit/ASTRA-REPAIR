// ASTRA v1.6 — COSMIC AUDIO native gate battery.
//
// Groups:
//  1  default-classification + honesty-policy parity vs fixture
//  2  PCM parity (synth + transforms + quantize) — quantized ints EXACT
//  3  adversarial transform/synth refusals
//  4  determinism (two-run bit-identical)
//
// Exit nonzero on any failure.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/audio_bus.h"
#include "app/audio_synth.h"

using astra::app::AudioClass;
using astra::app::AudioEvent;
using astra::app::AudioEventKind;
using astra::audio::quantize_int16;

static int g_checks = 0, g_fail = 0;
static void C(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

static void compare_pcm(const std::string& id, const std::vector<int16_t>& q,
                        const std::unordered_map<std::string, std::vector<int>>& want_q,
                        const std::unordered_map<std::string, int>& want_n) {
    ++g_checks;
    if (want_q.count(id) == 0) { ++g_fail; std::printf("FAIL: no fixture pcm for %s\n", id.c_str()); return; }
    const auto& want = want_q.at(id);
    ++g_checks;
    if (q.size() == 0 || q.size() != static_cast<size_t>(want_n.at(id))) {
        ++g_fail; std::printf("FAIL: %s length %zu want %d\n", id.c_str(), q.size(), want_n.at(id)); return;
    }
    int mismatches = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        ++g_checks;
        if (q[i] != want[i]) { ++g_fail; if (mismatches++ < 3) std::printf("FAIL: %s[%zu]=%d want %d\n", id.c_str(), i, q[i], want[i]); }
    }
    if (mismatches > 0) std::printf("FAIL: %s had %d sample mismatches\n", id.c_str(), mismatches);
}

static AudioEventKind kind_by_name(const std::string& s) {
    using astra::app::audio_event_kind_name;
    static const AudioEventKind all[] = {
        AudioEventKind::UI_SELECT, AudioEventKind::UI_DESELECT, AudioEventKind::UI_MODE,
        AudioEventKind::SIM_PAUSE, AudioEventKind::SIM_RESUME, AudioEventKind::SIM_WARP,
        AudioEventKind::SIM_STEP, AudioEventKind::SIM_RESET, AudioEventKind::SIM_RESTART,
        AudioEventKind::SCENARIO_SAVE, AudioEventKind::SCENARIO_LOAD,
        AudioEventKind::SONIFICATION_REQUEST, AudioEventKind::IMPACT_MODELED,
        AudioEventKind::VACUUM_ACOUSTIC_REQUEST, AudioEventKind::TRAVEL_BEGIN,
        AudioEventKind::TRAVEL_COMPLETE, AudioEventKind::TRAVEL_ABORT, AudioEventKind::TRAVEL_INVALID,
    };
    for (const auto k : all) if (s == audio_event_kind_name(k)) return k;
    std::printf("FAIL: unknown kind %s\n", s.c_str()); ++g_fail; ++g_checks;
    return AudioEventKind::UI_SELECT;
}
static AudioClass class_by_name(const std::string& s) {
    using astra::app::audio_class_name;
    static const AudioClass all[] = {
        AudioClass::REAL_ACOUSTIC, AudioClass::REAL_SIGNAL_SONIFICATION, AudioClass::DATA_DERIVED,
        AudioClass::PHYSICALLY_MODELED, AudioClass::SCIENTIFICALLY_INTERPRETED,
        AudioClass::CINEMATIC, AudioClass::SPECULATIVE,
    };
    for (const auto c : all) if (s == audio_class_name(c)) return c;
    std::printf("FAIL: unknown class %s\n", s.c_str()); ++g_fail; ++g_checks;
    return AudioClass::CINEMATIC;
}

int main(int argc, char** argv) {
    const std::string fixture = (argc > 1) ? argv[1] : "native_renderer/tests/fixtures/v16_audio_reference.txt";

    std::ifstream f(fixture);
    C((bool)f, "fixture exists");
    std::unordered_map<std::string, std::vector<int>> want_q;
    std::unordered_map<std::string, int> want_n;
    std::unordered_map<std::string, double> want_freq;
    struct Pol { std::string k, c, subj; int honest; };
    std::vector<Pol> pols;
    struct Def { std::string k, c; };
    std::vector<Def> defs;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> p; std::stringstream ss(line); std::string cell;
        while (std::getline(ss, cell, ',')) p.push_back(cell);
        if (p[0] == "defaults" && p.size() == 3) defs.push_back({p[1], p[2]});
        else if (p[0] == "policy" && p.size() == 5) pols.push_back({p[1], p[2], p[3], std::atoi(p[4].c_str())});
        else if (p[0] == "synth" && p.size() == 4) want_n[p[1]] = std::atoi(p[2].c_str());
        else if (p[0] == "s16" && p.size() == 4) {
            auto& v = want_q[p[1]];
            const size_t idx = static_cast<size_t>(std::atoi(p[2].c_str()));
            if (v.size() <= idx) v.resize(idx + 1, 0);
            v[idx] = std::atoi(p[3].c_str());
        }
        else if (p[0] == "sfreq" && p.size() == 3) want_freq[p[1]] = std::strtod(p[2].c_str(), nullptr);
    }

    // ---------------- group 1: policy + defaults parity ----------------
    int pol_checks = 0;
    for (const auto& d : defs) {
        const auto k = kind_by_name(d.k);
        const auto c = astra::app::default_classification(k);
        ++g_checks; ++pol_checks;
        if (std::string(astra::app::audio_class_name(c)) != d.c) {
            ++g_fail; std::printf("FAIL: defaults %s -> %s (want %s)\n", d.k.c_str(),
                                  astra::app::audio_class_name(c), d.c.c_str());
        }
    }
    for (const auto& pv : pols) {
        const AudioEvent e{kind_by_name(pv.k), class_by_name(pv.c), pv.subj, 0.0, 1.0f};
        const bool honest = astra::app::request_is_honest(e);
        ++g_checks; ++pol_checks;
        if (honest != (pv.honest != 0)) {
            ++g_fail;
            std::printf("FAIL: policy %s|%s|'%s' honest=%d want=%d\n", pv.k.c_str(), pv.c.c_str(),
                        pv.subj.c_str(), (int)honest, pv.honest);
        }
    }
    C(defs.size() == 18, "defaults vector count");
    std::printf("policy parity: %d checks\n", pol_checks);

    // ---------------- group 2: PCM parity ----------------
    {
        std::vector<double> buf;
        using astra::audio::synth_sine; using astra::audio::synth_chirp;
        using astra::audio::synth_orbital_hum; using astra::audio::tr_resample_linear;
        using astra::audio::tr_gain_db; using astra::audio::tr_normalize;
        C(synth_sine(440.0, 0.004, 8000.0, buf), "S1 synth");
        std::vector<int16_t> q; C(quantize_int16(buf, q), "S1 quantize");
        compare_pcm("S1", q, want_q, want_n);

        C(synth_chirp(220.0, 660.0, 0.005, 8000.0, buf), "S2 synth");
        C(quantize_int16(buf, q), "S2 quantize");
        compare_pcm("S2", q, want_q, want_n);

        const double AU_M = 149597870.7 * 1.0e3;         // IAU 2012 (in-repo constant)
        const double MU = 6.67430e-11 * 1.9885e30;       // in-repo constants
        double freq = 0.0;
        C(synth_orbital_hum(AU_M, MU, 365.25 * 86400.0, 55.0, 0.004, 8000.0, 1.0, freq, buf),
          "S3 synth");
        C(quantize_int16(buf, q), "S3 quantize");
        compare_pcm("S3", q, want_q, want_n);
        {
            const double want = want_freq["S3"];
            const double rel = std::fabs(freq - want) / std::fabs(want);
            ++g_checks;
            if (!(rel < 1e-12)) { ++g_fail; std::printf("FAIL: S3 freq rel %.3g\n", rel); }
        }

        C(synth_sine(100.0, 0.004, 8000.0, buf), "T1 base");
        C(tr_resample_linear(buf, 1.5, buf), "T1 resample");
        C(quantize_int16(buf, q), "T1 quantize");
        compare_pcm("T1", q, want_q, want_n);

        C(synth_chirp(110.0, 330.0, 0.004, 8000.0, buf), "T2 base");
        C(tr_gain_db(buf, -6.0, buf), "T2 gain");
        C(tr_normalize(buf, 0.5, buf), "T2 normalize");
        C(quantize_int16(buf, q), "T2 quantize");
        compare_pcm("T2", q, want_q, want_n);

        C(synth_sine(320.0, 0.004, 8000.0, buf), "T3 base");
        C(tr_resample_linear(buf, 0.5, buf), "T3 resample");
        C(quantize_int16(buf, q), "T3 quantize");
        compare_pcm("T3", q, want_q, want_n);
    }

    // ---------------- group 3: adversarial refusals ----------------
    {
        std::vector<double> buf{0.1, -0.2, 0.3}, out;
        C(!astra::audio::tr_resample_linear(buf, 0.0, out), "resample ratio 0");
        C(!astra::audio::tr_resample_linear(buf, -1.0, out), "resample ratio<0");
        C(!astra::audio::tr_resample_linear(buf, std::numeric_limits<double>::quiet_NaN(), out), "resample ratio NaN");
        C(!astra::audio::tr_resample_linear({}, 1.5, out), "resample empty");
        C(!astra::audio::tr_normalize(std::vector<double>{0.0, 0.0}, 0.5, out), "normalize zero-peak");
        C(!astra::audio::tr_normalize(buf, 0.0, out), "normalize peak 0");
        C(!astra::audio::tr_normalize(buf, 1.5, out), "normalize peak>1");
        C(!astra::audio::tr_gain_db(buf, std::numeric_limits<double>::quiet_NaN(), out), "gain NaN");
        {
            std::vector<int16_t> bad_q;
            C(!quantize_int16(std::vector<double>{0.0, std::numeric_limits<double>::infinity()}, bad_q),
              "quantize inf refusal");
        }
        C(!astra::audio::synth_sine(-440.0, 0.004, 8000.0, out), "sine negative freq");
        C(!astra::audio::synth_sine(440.0, 0.0, 8000.0, out), "sine zero duration");
        C(!astra::audio::synth_chirp(200.0, 100.0, -1.0, 8000.0, out), "chirp negative duration");
        double fr = 0.0;
        C(!astra::audio::synth_orbital_hum(0.0, 1.0, 1.0, 55.0, 0.1, 8000.0, 1.0, fr, out), "hum a<=0");
        C(!astra::audio::synth_orbital_hum(1.0, -1.0, 1.0, 55.0, 0.1, 8000.0, 1.0, fr, out), "hum mu<=0");
        C(!astra::audio::synth_orbital_hum(1.0, 1.327e20, 3.15576e7, 55.0, 0.1, 8000.0, 1.0, fr, out),
          "hum carrier>nyquist refusal");
    }

    // ---------------- group 4: determinism ----------------
    {
        std::vector<double> a, b;
        astra::audio::synth_chirp(220.0, 660.0, 0.005, 8000.0, a);
        astra::audio::synth_chirp(220.0, 660.0, 0.005, 8000.0, b);
        C(a.size() == b.size(), "det size");
        bool same = true;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) { same = false; break; }
        C(same, "det bit-identical");
    }

    std::printf("v16 gates: %d checks, %d failures\n", g_checks, g_fail);
    if (g_fail == 0) std::printf("ALL V1.6 CHECKS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
