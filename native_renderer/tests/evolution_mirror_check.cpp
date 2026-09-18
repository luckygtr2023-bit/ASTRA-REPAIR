// ASTRA COSMOS v1.3 — evolution native-mirror fidelity checker (domains 2+4).
// Re-runs the exact case matrix of scripts/gen_evolution_reference.py with
// the native mirror (app/evolution_sim) and compares every CSV row:
//   doubles bit-for-bit (%.17g round trips), integer/enum fields exactly,
//   error class names by the documented mapping
//     EvolutionValidationError -> VALIDATION
//     EvolutionNumericalError  -> NUMERICAL
//     EvolutionLimitationError -> LIMITATION
//     EvolutionAuthorityError  -> AUTHORITY
#include "app/evolution_sim.h"
#include "app/py_math.h"

#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace astra::app;

static long long g_total = 0, g_fail = 0, g_bit = 0;

static void report(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fputs(buf, stderr);
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t b = 0;
    while (true) {
        size_t p = s.find(delim, b);
        if (p == std::string::npos) { out.push_back(s.substr(b)); break; }
        out.push_back(s.substr(b, p - b));
        b = p + 1;
    }
    return out;
}

static std::string numstr(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

static double py_parse(const char* s) {
    const char* endp = nullptr;
    double out;
    if (py_strtod(s, &endp, &out) && (*endp == '\0' || std::isspace((unsigned char)*endp))) {
        return out;
    }
    return std::strtod(s, nullptr);
}

static void check_num(const std::string& ref, double got, const std::string& ctx) {
    ++g_total;
    const std::string gs = numstr(got);
    if (ref == gs) { ++g_bit; return; }
    const double rv = py_parse(ref.c_str());
    if (rv == got && std::signbit(rv) == std::signbit(got)) { ++g_bit; return; }
    ++g_fail;
    report("[FAIL] %s: ref=%s got=%s\n", ctx.c_str(), ref.c_str(), gs.c_str());
}

static void check_tok(const std::string& ref, const std::string& got, const std::string& ctx) {
    ++g_total;
    if (ref == got) return;
    ++g_fail;
    report("[FAIL] %s: ref=%s got=%s\n", ctx.c_str(), ref.c_str(), got.c_str());
}

static std::string err_name(EvolErr e) {
    switch (e) {
        case EvolErr::OK: return "OK";
        case EvolErr::VALIDATION: return "EvolutionValidationError";
        case EvolErr::NUMERICAL: return "EvolutionNumericalError";
        case EvolErr::LIMITATION: return "EvolutionLimitationError";
        case EvolErr::AUTHORITY: return "EvolutionAuthorityError";
        case EvolErr::DEPENDENCY: return "EvolutionDependencyError";
    }
    return "?";
}

// ------------------------- T cases ------------------------------------------
struct TCase { const char* id; double rem; double rate; bool has_nxt; double nxt; bool has_cur; double cur; };
static const TCase T_CASES[] = {
    {"max", 5.0, 1.0, false, 0, true, 0.5},
    {"rem_under_min", 5e-7, 1.0, false, 0, false, 0},
    {"rem_zero", 0.0, 1.0, false, 0, false, 0},
    {"rate_2", 5.0, 2.0, false, 0, true, 0.25},
    {"rate_0", 5.0, 0.0, false, 0, true, 0.25},
    {"rate_huge", 5.0, 1e15, false, 0, true, 0.25},
    {"rate_tiny_floor", 5.0, 1e-15, false, 0, true, 0.25},
    {"exact_remaining", 1.0, 1.0, false, 0, true, 0.0},
    {"event_abs_far", 5.0, 1.0, true, 100.0, true, 1.0},
    {"event_abs_close", 5.0, 1.0, true, 1.5, true, 1.0},
    {"event_abs_under_min", 5.0, 1.0, true, 1.0000005, true, 1.0},
    {"event_abs_past", 5.0, 1.0, true, 0.5, true, 1.0},
    {"event_nocur_small", 5.0, 1.0, true, 0.2, false, 0},
    {"event_nocur_big", 5.0, 1.0, true, 10.0, false, 0},
    {"rem_base_limited", 0.05, 1.0, false, 0, true, 0.0},
    {"rem_min_boundary", 1e-6, 1.0, false, 0, true, 0.0},
    {"neg_remaining", -1.0, 1.0, false, 0, false, 0},
    {"nan_remaining", 0.0/0.0, 1.0, false, 0, false, 0},
    {"neg_rate", 5.0, -0.5, false, 0, false, 0},
    {"inf_rate", 5.0, HUGE_VAL, false, 0, false, 0},
    {"event_nan", 5.0, 1.0, true, 0.0/0.0, true, 1.0},
};

// ------------------------- G cases ------------------------------------------
struct GCase { const char* id; EvolState st; double dt; };
static EvolState mk(const char* oid, double time, const char* phase,
                    std::vector<std::pair<std::string, double>> q,
                    std::vector<EvolMorphologyEntry> morph = {}) {
    EvolState s;
    s.object_id = oid;
    s.cosmic_time_gyr = time;
    s.phase = phase;
    s.model_id = "astra.evolution.galaxy.v1";
    s.morphology_probs = std::move(morph);
    for (auto& kv : q) {
        EvolQuantity qq; qq.value = kv.second; s.quantities[kv.first] = qq;
    }
    return s;
}

static EvolState mk_galaxy_state(std::vector<EvolMorphologyEntry> morph,
                                 double gas, double sfr, double mstar,
                                 double Z, double L, bool with_bh) {
    std::vector<std::pair<std::string, double>> q = {
        {"stellar_mass_msun", mstar},
        {"gas_mass_msun", gas},
        {"sfr_msun_per_yr", sfr},
        {"metallicity", Z},
        {"luminosity_Lsun", L},
    };
    if (with_bh) q.push_back({"central_bh_mass_msun", 4.3e6});
    return mk("g-1", 0.0, "SPIRAL", q, std::move(morph));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <evol_reference.csv>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "r");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }

    const std::string GALAXY = "astra.evolution.galaxy.v1";
    const GCase G_CASES[] = {
        {"basic", mk_galaxy_state({}, 1e10, 5.0, 5e10, 0.02, 2e10, false), 0.01},
        {"big_dt", mk_galaxy_state({}, 1e10, 5.0, 5e10, 0.02, 2e10, false), 1.0},
        {"gas_rich", mk_galaxy_state({}, 1e12, 5.0, 5e10, 0.02, 2e10, false), 0.1},
        {"gas_poor_no_drift", mk_galaxy_state({{"SPIRAL",0.7},{"ELLIPTICAL",0.2},{"IRREGULAR",0.1}},
                                              1e12, 5.0, 5e10, 0.02, 2e10, false), 0.1},
        {"gas_poor_drift", mk_galaxy_state({{"SPIRAL",0.7},{"ELLIPTICAL",0.2},{"IRREGULAR",0.1}},
                                            1e6, 5.0, 5e10, 0.02, 2e10, false), 0.1},
        {"drift_max_shift", mk_galaxy_state({{"SPIRAL",0.7},{"ELLIPTICAL",0.2},{"IRREGULAR",0.1}},
                                            1e6, 5.0, 5e10, 0.02, 2e10, false), 5.0},
        {"drift_no_spiral", mk_galaxy_state({{"ELLIPTICAL",0.9},{"IRREGULAR",0.1}},
                                            1e6, 5.0, 5e10, 0.02, 2e10, false), 5.0},
        {"zero_gas", mk_galaxy_state({}, 0.0, 2.0, 5e10, 0.02, 2e10, false), 0.5},
        {"morph_order", mk_galaxy_state({{"IRREGULAR",0.4},{"SPIRAL",0.5},{"ELLIPTICAL",0.1}},
                                        1e6, 5.0, 5e10, 0.02, 2e10, false), 1.0},
        {"with_bh", mk_galaxy_state({}, 1e10, 5.0, 5e10, 0.02, 2e10, true), 0.25},
    };
    const char* GQ_KEYS[] = {"stellar_mass_msun", "gas_mass_msun", "sfr_msun_per_yr",
                             "metallicity", "luminosity_Lsun", "central_bh_mass_msun"};
    const EvolState CLUSTER0 = mk("c-1", 0.0, "CLUSTER",
        {{"total_mass_msun",1e14},{"member_count",50.0}});
    const EvolState WEB0 = mk("w-1", 0.0, "COSMIC_WEB",
        {{"filament_count",20.0},{"node_count",10.0},{"void_count",15.0}});
    const EvolState WEB_LATE = mk("w-2", 10.5, "COSMIC_WEB",
        {{"filament_count",20.0},{"node_count",10.0},{"void_count",15.0}});
    const EvolState VOID0 = mk("v-1", 0.0, "VOID",
        {{"radius_mpc",10.0},{"density_contrast",-0.8}});
    const EvolState HALO0 = mk("h-1", 0.0, "HALO",
        {{"halo_mass_msun",1e12},{"concentration",5.0}});
    struct KCase { const char* id; const EvolState* st; const char* kind; double dt; };
    const KCase K_CASES[] = {
        {"c_small", &CLUSTER0, "cluster", 0.01},
        {"c_one", &CLUSTER0, "cluster", 1.0},
        {"c_three", &CLUSTER0, "cluster", 3.0},
        {"c_halfint", &CLUSTER0, "cluster", 3.9999},
        {"w_early", &WEB0, "web", 1.0},
        {"w_late", &WEB_LATE, "web", 1.0},
        {"w_late_gt", &WEB_LATE, "web", 30.0},
        {"v_small", &VOID0, "void", 0.5},
        {"v_floor", &VOID0, "void", 50.0},
        {"h_small", &HALO0, "halo", 0.1},
    };
    const EvolEpochBoundaries DEF_B; // default: 0.1/0.5/0.7/0.01
    struct ECase { const char* id; bool hs, hr, hb, hl; double sfr, rem, bh, lum, ct; };
    const ECase E_CASES_A[] = {
        {"stelli", true, true, false, false, 0.2, 0.1, 0, 0, 1.0},
        {"decline", true, true, false, false, 0.05, 0.4, 0, 0, 1.0},
        {"degenerate", true, true, false, false, 0.05, 0.6, 0, 0, 1.0},
        {"bh", true, true, true, true, 0.05, 0.9, 0.8, 0.5, 1.0},
        {"dark", true, true, true, true, 0.05, 0.9, 0.8, 0.005, 1.0},
        {"dark_only", true, true, false, true, 0.05, 0.9, 0, 0.005, 1.0},
        {"bh_only", true, true, true, false, 0.05, 0.9, 0.8, 0, 1.0},
        {"unknown_sfr", false, true, false, false, 0, 0.4, 0, 0, 1.0},
        {"unknown_rem", true, false, false, false, 0.05, 0, 0, 0, 1.0},
        {"bad_rem_hi", true, true, false, false, 0.05, 1.2, 0, 0, 1.0},
        {"bad_rem_neg", true, true, false, false, 0.05, -0.1, 0, 0, 1.0},
        {"bad_sfr_neg", true, true, false, false, -0.5, 0.4, 0, 0, 1.0},
        {"edge_sfr_at", true, true, false, false, 0.1, 0.4, 0, 0, 1.0},
        {"edge_rem_at", true, true, false, false, 0.05, 0.5, 0, 0, 1.0},
        {"edge_bh_at", true, true, true, true, 0.05, 0.9, 0.7, 0.5, 1.0},
        {"edge_lum_at", true, true, true, true, 0.05, 0.9, 0.5, 0.01, 1.0},
        {"nan_time", true, true, false, false, 1.0, 0.1, 0, 0, 0.0/0.0},
        {"bad_bh_frac", true, true, true, false, 0.05, 0.9, 1.5, 0, 1.0},
    };
    std::map<std::string, ECase> E_CASES;
    for (const auto& c : E_CASES_A) E_CASES.emplace(c.id, c);

    struct XCase { const char* id; const char* model_id; EvolState st; double until; bool authority; long long cap_history; };
    struct XCaseD { const char* id; const char* model_id; double start; const char* phase;
                    std::vector<std::pair<std::string,double>> q; double until; bool authority; long long cap_history;
                    std::vector<EvolMorphologyEntry> morph; };
    const XCaseD X_CASES_A[] = {
        {"linear_short", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 0.05, true, -1, {}},
        {"linear_unaligned", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 0.037, true, -1, {}},
        {"linear_minrem", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 5e-7, true, -1, {}},
        {"linear_big", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 5.0, true, -1, {}},
        {"linear_zero", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 0.0, true, -1, {}},
        {"stellar_ms", "astra.evolution.stellar.v1", 0.0, "MAIN_SEQUENCE",
            {{"age_gyr",0.0},{"mass_msun",1.0}}, 5.0, true, -1, {}},
        {"stellar_xms", "astra.evolution.stellar.v1", 0.0, "MAIN_SEQUENCE",
            {{"age_gyr",9.5},{"mass_msun",1.0}}, 12.0, true, -1, {}},
        {"stellar_ms_edge", "astra.evolution.stellar.v1", 0.0, "MAIN_SEQUENCE",
            {{"age_gyr",9.999999999},{"mass_msun",2.0}}, 0.56, true, -1, {}},
        {"stellar_nowd", "astra.evolution.stellar.v1", 0.0, "MAIN_SEQUENCE",
            {{"age_gyr",9.5},{"mass_msun",0.3}}, 5.0, true, -1, {}},
        {"galaxy_def", "astra.evolution.galaxy.v1", 0.0, "SPIRAL",
            {{"stellar_mass_msun",5e10},{"gas_mass_msun",1e10},{"sfr_msun_per_yr",5.0},
             {"metallicity",0.02},{"luminosity_Lsun",2e10}}, 2.5, true, -1, {}},
        {"galaxy_drift", "astra.evolution.galaxy.v1", 0.0, "SPIRAL",
            {{"stellar_mass_msun",5e10},{"gas_mass_msun",1e6},{"sfr_msun_per_yr",5.0},
             {"metallicity",0.02},{"luminosity_Lsun",2e10}}, 2.5, true, -1,
            {{"SPIRAL",0.7},{"ELLIPTICAL",0.2},{"IRREGULAR",0.1}}},
        {"cluster_def", "astra.evolution.cluster.v1", 0.0, "CLUSTER",
            {{"total_mass_msun",1e14},{"member_count",50.0}}, 3.3, true, -1, {}},
        {"web_def", "astra.evolution.web.v1", 9.0, "COSMIC_WEB",
            {{"filament_count",20.0},{"node_count",10.0},{"void_count",15.0}}, 12.0, true, -1, {}},
        {"void_def", "astra.evolution.void.v1", 0.0, "VOID",
            {{"radius_mpc",10.0},{"density_contrast",-0.8}}, 2.2, true, -1, {}},
        {"halo_def", "astra.evolution.halo.v1", 0.0, "HALO",
            {{"halo_mass_msun",1e12},{"concentration",5.0}}, 1.5, true, -1, {}},
        {"backward", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, -0.1, true, -1, {}},
        {"unknown_model", "astra.evolution.nowhere.v1", 0.0, "MAIN_SEQUENCE", {}, 1.0, true, -1, {}},
        {"auth_denied", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 1.0, false, -1, {}},
        {"hist_cap", "astra.evolution.linear.v1", 0.0, "MAIN_SEQUENCE", {}, 0.03, true, 3, {}},
    };
    std::map<std::string, XCaseD> X_CASES;
    for (const auto& c : X_CASES_A) X_CASES.emplace(c.id, c);

    char line[8192];
    int row_no = 0;
    // Cache per X-case engine results (G rows of the same case would re-run it)
    std::map<std::string, std::pair<EvolErr, std::pair<EvolState, EvolEngine>>> x_cache;
    while (std::fgets(line, sizeof(line), f)) {
        ++row_no;
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty()) continue;
        auto t = split(s, ',');
        const std::string& kind = t[0];
        const std::string ctx = "row " + std::to_string(row_no) + " " + kind + ":" + t[1];

        if (kind == "T") {
            const TCase* cptr = nullptr;
            for (const auto& c : T_CASES) if (c.id == t[1]) cptr = &c;
            const TCase& c = *cptr;
            EvolTimestepPolicy p;
            EvolTimestepDecision d;
            const EvolErr e = evol_choose_timestep(p, c.rem, c.rate,
                                                   c.has_nxt, c.nxt, c.has_cur, c.cur, d);
            if (t.size() == 4 && t[2] == "ERR") {
                check_tok(t[3], err_name(e), ctx);
            } else {
                if (e != EvolErr::OK) { ++g_total; ++g_fail; report("[FAIL] %s: expected OK got %s\n", ctx.c_str(), err_name(e).c_str()); }
                else {
                    check_num(t[2], d.dt_gyr, ctx + ":dt");
                    check_tok(t[3], evol_timestep_reason_name(d.reason), ctx + ":reason");
                }
            }
        } else if (kind == "S") {
            const double m = py_parse(t[1].c_str());
            double lt; EvolStellarPhase rm;
            const EvolErr e1 = evol_main_sequence_lifetime_gyr(m, lt);
            if (t.size() == 4 && t[2] == "ERR") { check_tok(t[3], err_name(e1), ctx); }
            else {
                check_num(t[2], lt, ctx + ":lifetime");
                evol_remnant_for_mass(m, rm);
                check_tok(t[3], evol_stellar_phase_name(rm), ctx + ":remnant");
            }
        } else if (kind == "SC") {
            const double m = py_parse(t[1].c_str());
            const double age = py_parse(t[2].c_str());
            if (age == 1e40) {
                // SC-branch pin rows: SC,mass,1e40,1,remnant -> t[4]
                EvolStellarPhase rm;
                evol_remnant_for_mass(m, rm);
                check_tok(t[4], evol_stellar_phase_name(rm), ctx + ":remnant");
            } else {
                EvolStellarTransition tr;
                const EvolErr e = evol_classify_stellar_phase(m, age, tr);
                if (e != EvolErr::OK) { ++g_total; ++g_fail; report("[FAIL] %s: classify err %s\n", ctx.c_str(), err_name(e).c_str()); continue; }
                check_tok(t[3], tr.present ? "1" : "0", ctx + ":present");
                if (tr.present) {
                    check_tok(t[4], evol_stellar_phase_name(tr.before), ctx + ":before");
                    check_tok(t[5], evol_stellar_phase_name(tr.after), ctx + ":after");
                    check_num(t[6], tr.timescale_gyr, ctx + ":timescale");
                } else {
                    check_tok(t[4], "NONE", ctx + ":before");
                    check_tok(t[5], "NONE", ctx + ":after");
                }
            }
        } else if (kind == "G" || kind == "GM") {
            const GCase* cptr = nullptr;
            for (const auto& c : G_CASES) if (c.id == t[1]) cptr = &c;
            EvolState out;
            const EvolErr e = evol_make_galaxy_step_default(cptr->st, cptr->dt, GALAXY, out);
            if (e != EvolErr::OK) { ++g_total; ++g_fail; report("[FAIL] %s: step err\n", ctx.c_str()); continue; }
            for (size_t i = 2; i < t.size(); ++i) {
                const auto kv = split(t[i], '=');
                const std::string& key = kv[0];
                if (kind == "G") {
                    auto it = out.quantities.find(key);
                    if (kv[1] == "-") { ++g_total; if (it == out.quantities.end()) {} else { ++g_fail; report("[FAIL] %s: key %s present\n", ctx.c_str(), key.c_str()); } }
                    else {
                        if (it == out.quantities.end()) { ++g_total; ++g_fail; report("[FAIL] %s: key %s missing\n", ctx.c_str(), key.c_str()); }
                        else check_num(kv[1], it->second.value, ctx + ":" + key);
                    }
                } else {
                    // morphology: match insertion-order pair emission via value
                    bool found = false;
                    for (const auto& me : out.morphology_probs) {
                        if (me.key == key) { check_num(kv[1], me.value, ctx + ":morph:" + key); found = true; break; }
                    }
                    if (!found) { ++g_total; ++g_fail; report("[FAIL] %s: morph key %s missing\n", ctx.c_str(), key.c_str()); }
                }
            }
            if (kind == "GM") {
                // Assert insertion ORDER identical: keys in row order == vector order.
                ++g_total;
                std::vector<std::string> got;
                for (const auto& me : out.morphology_probs) got.push_back(me.key);
                std::vector<std::string> want;
                for (size_t i = 2; i < t.size(); ++i) want.push_back(split(t[i], '=')[0]);
                if (got != want) { ++g_fail; report("[FAIL] %s: morph order mismatch\n", ctx.c_str()); }
            }
        } else if (kind == "K") {
            const KCase* cptr = nullptr;
            for (const auto& c : K_CASES) if (c.id == t[1]) cptr = &c;
            EvolState out;
            EvolErr e = EvolErr::OK;
            const std::string kindw = cptr->kind;
            const char* mid = kindw == "cluster" ? "astra.evolution.cluster.v1"
                            : kindw == "web" ? "astra.evolution.web.v1"
                            : kindw == "void" ? "astra.evolution.void.v1"
                            : "astra.evolution.halo.v1";
            if (kindw == "cluster") e = evol_make_cluster_step(*cptr->st, cptr->dt, mid, 0.02, out);
            else if (kindw == "web") e = evol_make_cosmic_web_step(*cptr->st, cptr->dt, mid, out);
            else if (kindw == "void") e = evol_make_void_step(*cptr->st, cptr->dt, mid, 0.01, out);
            else e = evol_make_dark_matter_halo_step(*cptr->st, cptr->dt, mid, 0.02, out);
            if (e != EvolErr::OK) { ++g_total; ++g_fail; report("[FAIL] %s: step err\n", ctx.c_str()); continue; }
            check_num(t[2], out.cosmic_time_gyr, ctx + ":t");
            if (kindw == "cluster") {
                check_num(t[3], out.quantities["total_mass_msun"].value, ctx + ":mass");
                check_num(t[4], out.quantities["member_count"].value, ctx + ":members");
            } else if (kindw == "web") {
                check_num(t[3], out.quantities["filament_count"].value, ctx + ":fil");
                check_num(t[4], out.quantities["void_count"].value, ctx + ":void");
            } else if (kindw == "void") {
                check_num(t[3], out.quantities["radius_mpc"].value, ctx + ":r");
                check_num(t[4], out.quantities["density_contrast"].value, ctx + ":d");
            } else {
                check_num(t[3], out.quantities["halo_mass_msun"].value, ctx + ":mass");
                check_num(t[4], out.quantities["concentration"].value, ctx + ":c");
            }
        } else if (kind == "E") {
            const ECase& c = E_CASES[t[1]];
            EvolEpoch ep;
            const EvolErr e = evol_classify_epoch(DEF_B, c.ct, c.hs, c.sfr, c.hr, c.rem,
                                                  c.hb, c.bh, c.hl, c.lum, ep);
            if (t.size() == 4 && t[2] == "ERR") {
                check_tok(t[3], err_name(e), ctx);
            } else {
                if (e != EvolErr::OK) { ++g_total; ++g_fail; report("[FAIL] %s: expected epoch got %s\n", ctx.c_str(), err_name(e).c_str()); }
                else check_tok(t[2], evol_epoch_name(ep), ctx + ":epoch");
            }
        } else if (kind == "X") {
            const XCaseD& c = X_CASES[t[1]];
            // Run (cache) engine result for this id
            auto it = x_cache.find(c.id);
            if (it == x_cache.end()) {
                EvolEngine eng;
                if (c.cap_history > 0) eng.cfg.budget.max_history_samples_per_object = c.cap_history;
                EvolState st = mk(c.id, c.start, c.phase, c.q, c.morph);
                st.model_id = c.model_id;
                EvolState out;
                const EvolErr e = eng.evolve_object(st, c.until, c.model_id, c.authority, out);
                it = x_cache.emplace(c.id, std::make_pair(e, std::make_pair(out, std::move(eng)))).first;
            }
            const EvolErr e = it->second.first;
            if (t.size() == 4 && t[2] == "ERR") {
                check_tok(t[3], err_name(e), ctx);
                continue;
            }
            const EvolState& fin = it->second.second.first;
            const EvolEngine& eng = it->second.second.second;
            check_num(t[2], fin.cosmic_time_gyr, ctx + ":time");
            check_tok(t[3], fin.phase, ctx + ":phase");
            if (!t[4].empty()) {
                for (const auto& kvs : split(t[4], ';')) {
                    const auto kv = split(kvs, '=');
                    auto qit = fin.quantities.find(kv[0]);
                    if (qit == fin.quantities.end()) { ++g_total; ++g_fail; report("[FAIL] %s: q %s missing\n", ctx.c_str(), kv[0].c_str()); }
                    else check_num(kv[1], qit->second.value, ctx + ":q:" + kv[0]);
                }
            }
            check_tok(t.size() > 5 ? t[5] : "", std::to_string(eng.history.size()), ctx + ":histlen");
            check_num(t.size() > 6 ? t[6] : "0", eng.history.empty() ? 0.0 : eng.history.back().cosmic_time_gyr, ctx + ":histlast");
        }
    }
    std::fclose(f);
    std::printf("evolution_mirror_check: %lld comparisons, %lld fails, %lld bit-exact\n",
                g_total, g_fail, g_bit);
    std::printf("RESULT: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
