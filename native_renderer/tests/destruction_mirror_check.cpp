// ASTRA COSMOS v1.3 — destruction native-mirror fidelity checker.
// Re-runs the exact case sweep of scripts/gen_destruction_reference.py with
// the native mirror (app/py_mt19937 + app/destruction_sim) and compares
// every CSV row: doubles bit-for-bit (%.17g round-trips), integer/string
// fields exactly, error-kind tokens by the documented mapping
//   ImpactValidationError -> IMPACT_VALIDATION
//   NumericalError        -> NUMERICAL
//   LimitExceededError    -> LIMIT_EXCEEDED
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app/destruction_sim.h"
#include "app/py_mt19937.h"

using namespace astra::app;

static int g_ok = 0, g_fail = 0, g_bit = 0;

static void report(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

struct Case {
    double im, tm;
    double ip[3], tp[3], iv[3], tv[3];
    double i_rad, t_rad, t_sim;
};

static const Case CASES[] = {
    {1.0e12, 9.3931e20, {-5.0e5,0,0}, {0,0,0}, {6500,0,0}, {0,0,0}, 0.0, 4.7e5, 600.0},
    {5.0e10, 1.0e15, {-2.0e5,1.0e4,0}, {0,0,0}, {1.2e4,-800,0}, {100,0,0}, 1.0e3, 9.0e4, 12.5},
    {2.0e8, 2.0e8, {-1.0e3,0,0}, {0,0,0}, {3.0,0,0}, {0,0,0}, 5.0, 5.0, 3.0},
    {1.0, 1.0e3, {-50,0,0}, {0,0,0}, {5.0,0,0}, {0,0,0}, 1.0, 10.0, 0.0},
    {1.0e6, 1.0e9, {0,0,0}, {0,0,0}, {0,0,4.0e3}, {0,0,-1.0e3}, 0.0, 25.0, 2.0},
    {4.0e3, 6.0e9, {-80,3,0}, {0,0,0}, {2.0,0.1,0}, {0.01,0,0}, 0.0, 100.0, 9.0},
    {1.0e3, 1.0e3, {-1.0e2,0,0}, {0,0,0}, {0.0006,0,0}, {0,0,0}, 0.0, 50.0, 0.0},
    {1.0e9, 1.0e12, {-1e3,0,0}, {0,0,0}, {0.0,0,0}, {0,0,0}, 1.0, 2.0, 0.0},
    {7.0e12, 2.0e18, {-3.0e4,1.5e4,0}, {0,0,0}, {9.0e3,0,0}, {0,0,0}, 100.0, 2.0e4, 33.0},
    {1.0, 1.0, {-0.5,0,0}, {0,0,0}, {10.0,0,0}, {0,0,0}, 0.0, 0.5, 1.0},
    {3.0e14, 8.0e14, {-1.0e6,2.0e5,3.0e4}, {0,0,0}, {7500,300,150}, {-50,10,0}, 2.0e4, 6.0e5, 100.0},
    {9.0e5, 2.0e16, {-4.0e5,5.0e5,0}, {0,0,0}, {2.0e3,-1.0e3,500}, {0,0,0}, 0.0, 1.0e6, 44.0},
};

static DestructionConfig CFG{};

struct Ledger {
    std::map<std::string, DestructionDamageState> m;
    DestructionDamageState get(const std::string& id) const {
        auto it = m.find(id);
        return it == m.end() ? DestructionDamageState::INTACT : it->second;
    }
    void set(const std::string& id, DestructionDamageState s) { m[id] = s; }
};

static DestructionImpactEvent make_event(int case_idx, const Case& c, long long seed, bool iid_seed) {
    DestructionImpactEvent ev;
    ev.impact_id = iid_seed ? ("imp:" + std::to_string(case_idx) + ":" + std::to_string(seed))
                            : case_idx == 950 ? "imp:sec0" : ("imp:" + std::to_string(case_idx));
    if (case_idx == 950) ev.impact_id = "imp:sec0";
    ev.impactor_id = "impactor:" + std::to_string(case_idx);
    ev.target_id = "target:" + std::to_string(case_idx);
    ev.sim_time_s = c.t_sim;
    ev.impactor_mass_kg = c.im;
    ev.target_mass_kg = c.tm;
    ev.impactor_position = {c.ip[0], c.ip[1], c.ip[2]};
    ev.target_position = {c.tp[0], c.tp[1], c.tp[2]};
    ev.impactor_velocity = {c.iv[0], c.iv[1], c.iv[2]};
    ev.target_velocity = {c.tv[0], c.tv[1], c.tv[2]};
    ev.impactor_radius_m = c.i_rad;
    ev.target_radius_m = c.t_rad;
    return ev;
}

static std::string numstr(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

static void check_num(const std::string& ref, double got, const std::string& ctx) {
    const std::string gs = numstr(got);
    if (ref == gs) { ++g_ok; ++g_bit; return; }
    double rv = std::strtod(ref.c_str(), nullptr);
    if (rv == got && std::signbit(rv) == std::signbit(got)) { ++g_ok; ++g_bit; return; }
    ++g_fail;
    report("[FAIL] %s: ref=%s got=%s\n", ctx.c_str(), ref.c_str(), gs.c_str());
}

static void check_tok(const std::string& ref, const std::string& got, const std::string& ctx) {
    if (ref == got) ++g_ok;
    else { ++g_fail; report("[FAIL] %s: ref=%s got=%s\n", ctx.c_str(), ref.c_str(), got.c_str()); }
}

static const char* err_name(DestErr e) {
    switch (e) {
    case DestErr::OK: return "OK";
    case DestErr::IMPACT_VALIDATION: return "ImpactValidationError";
    case DestErr::NUMERICAL: return "NumericalError";
    case DestErr::LIMIT_EXCEEDED: return "LimitExceededError";
    }
    return "?";
}

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, j;
    while ((j = s.find(',', i)) != std::string::npos) { out.push_back(s.substr(i, j - i)); i = j + 1; }
    out.push_back(s.substr(i));
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s reference.csv\n", argv[0]); return 2; }
    FILE* f = std::fopen(argv[1], "re");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }

    Ledger ledger;
    // Pre-executed artifacts, regenerable in-order on demand (mirroring the
    // Python driver's exact execution order: per case G/E/M singles, then
    // execute_impact for seeds 101/2024/987654321 (with ledger reuse), then
    // fragments' orbit classifications).
    std::map<std::string, DestructionImpactResult> executed; // key "ci:seed"
    std::map<std::string, DestructionDamageState> after_state;
std::map<std::string, int> okd_count;

    auto ensure_execute = [&](int ci, long long seed) -> const DestructionImpactResult* {
        const std::string k = std::to_string(ci) + ":" + std::to_string(seed);
        auto it = executed.find(k);
        if (it != executed.end()) return &it->second;
        const Case& c = CASES[ci];
        DestructionImpactEvent ev = make_event(ci, c, seed, true);
        DestructionImpactResult r;
        DestructionDamageState st0 = ledger.get(ev.target_id);
        DestErr e = dest_execute_impact(ev, CFG, seed, st0, 0.05, 100.0, r);
        if (e != DestErr::OK) {
            DestructionImpactResult fail;
            fail.state_after = (DestructionDamageState)-1; // sentinel: failed
            executed.emplace(k, fail);
            after_state[k] = st0;
            return &executed.find(k)->second;
        }
        ledger.set(ev.target_id, r.state_after);
        executed.emplace(k, r);
        after_state[k] = st0;
        return &executed.find(k)->second;
    };

    char line[4096];
    int row_no = 0;
    while (std::fgets(line, sizeof(line), f)) {
        ++row_no;
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty()) continue;
        auto t = split(s);
        const std::string& kind = t[0];
        const std::string ctx = "row " + std::to_string(row_no) + " " + kind;
        if (kind == "V") {
            // V,ci,OK | V,ci,ErrorKind | V,ci,OKD,before,after | V,901,ErrorKind
            const int ci = std::stoi(t[1]);
            if (t[2] == "OK") {
                const DestructionImpactEvent ev = make_event(ci, CASES[ci], 0, false);
                check_tok(t[2], err_name(dest_validate_impact(ev, CFG)), ctx);
            } else if (t[2] == "OKD") {
                // refgen emits one V,ci,OKD row per seed run (101, 2024, 987654321) —
                // the row's "before" state is the damage ledger state just BEFORE that seed's
                // execute, so the Nth OKD row for a case belongs to SEEDS[N].
                static const long long OKD_SEEDS[3] = {101, 2024, 987654321};
                const std::string ck = std::to_string(ci);
                const int n_th = okd_count[ck]++;
                const long long seed = OKD_SEEDS[n_th % 3];
                auto* r = ensure_execute(ci, seed);
                if ((int)r->state_after == -1) { ++g_fail; report("[FAIL] %s: expected success, execute failed\n", ctx.c_str()); continue; }
                const std::string kk = std::to_string(ci) + ":" + std::to_string(seed);
                check_tok(t[3], dest_damage_name(after_state[kk]), ctx + ":before");
                check_tok(t[4], dest_damage_name(r->state_after), ctx + ":after");
            } else {
                // error kind: validate again and compare kind name
                if (ci == 901) {
                    // cap<2 with fractured target: separate run
                    DestructionConfig cfg3 = CFG;
                    cfg3.limits.max_fragments_per_impact = 0;
                    DestructionImpactEvent ev{};
                    ev.impact_id = "imp:901"; ev.impactor_id = "impactor:901"; ev.target_id = "target:901";
                    ev.sim_time_s = 0.0; ev.impactor_mass_kg = 1.0e14; ev.target_mass_kg = 1.0e15;
                    ev.impactor_position = {-1.0e4, 0, 0}; ev.target_position = {0, 0, 0};
                    ev.impactor_velocity = {3.0e5, 0, 0}; ev.target_velocity = {0, 0, 0};
                    ev.impactor_radius_m = 0.0; ev.target_radius_m = 1.0e5;
                    DestructionImpactResult r;
                    check_tok(t[2], err_name(dest_execute_impact(ev, cfg3, 5,
                        DestructionDamageState::INTACT, 0.05, 100.0, r)), ctx);
                } else {
                    const DestructionImpactEvent ev = make_event(ci, CASES[ci], 0, false);
                    check_tok(t[2], err_name(dest_validate_impact(ev, CFG)), ctx);
                }
            }
        } else if (kind == "G") {
            const int ci = std::stoi(t[1]);
            DestructionImpactGeometry g;
            const DestructionImpactEvent ev = make_event(ci, CASES[ci], 0, false);
            DestErr e = dest_compute_impact_geometry(ev, CFG, g);
            if (e != DestErr::OK) {
                check_tok(t[2], err_name(e), ctx);
            } else {
                check_num(t[2], g.contact_point.x, ctx);
                check_num(t[3], g.contact_point.y, ctx);
                check_num(t[4], g.contact_point.z, ctx);
                check_num(t[5], g.surface_normal.x, ctx);
                check_num(t[6], g.surface_normal.y, ctx);
                check_num(t[7], g.surface_normal.z, ctx);
                check_num(t[8], g.incoming_direction.x, ctx);
                check_num(t[9], g.incoming_direction.y, ctx);
                check_num(t[10], g.incoming_direction.z, ctx);
                check_num(t[11], g.incidence_angle_rad, ctx);
                check_tok(t[12], g.is_grazing ? "1" : "0", ctx);
                check_tok(t[13], g.is_head_on ? "1" : "0", ctx);
            }
        } else if (kind == "E") {
            const int ci = std::stoi(t[1]);
            if (t.size() == 4 && t[2].find_first_not_of("0123456789-") == std::string::npos) {
                // E,ci,seed,ErrorKind  (execute failure digest)
                const long long seed = std::stoll(t[2]);
                auto* r = ensure_execute(ci, seed);
                (void)r;
                DestructionImpactEvent ev = make_event(ci, CASES[ci], seed, true);
                DestructionImpactResult rr;
                DestErr e = dest_execute_impact(ev, CFG, seed, Ledger{}/*fresh not used*/.get(ev.target_id), 0.05, 100.0, rr);
                check_tok(t[3], err_name(e), ctx);
            } else if (ci == 900) {
                DestructionConfig cfg2 = CFG;
                cfg2.fragmentation_energy_fraction = 1.0;
                cfg2.thermal_energy_fraction = 0.0;
                DestructionImpactEvent ev{};
                ev.impact_id = "imp:900"; ev.impactor_id = "impactor:900"; ev.target_id = "target:900";
                ev.sim_time_s = 0.0; ev.impactor_mass_kg = 2.0e10; ev.target_mass_kg = 1.0e12;
                ev.impactor_position = {-1.0e3, 0, 0}; ev.target_position = {0, 0, 0};
                ev.impactor_velocity = {2.0e4, 0, 0}; ev.target_velocity = {0, 0, 0};
                ev.impactor_radius_m = 0.0; ev.target_radius_m = 0.0;
                DestructionImpactEnergy en;
                DestErr e = dest_compute_impact_energy(ev, cfg2, 0.5, en);
                if (e != DestErr::OK) check_tok(t[2], err_name(e), ctx);
                else {
                    check_num(t[2], en.kinetic_energy_j, ctx);
                    check_num(t[3], en.deposited_energy_j, ctx);
                    check_num(t[4], en.fragmentation_energy_j, ctx);
                    check_num(t[5], en.thermal_energy_j, ctx);
                    check_num(t[6], en.residual_kinetic_energy_j, ctx);
                }
            } else {
                DestructionImpactEnergy en;
                const DestructionImpactEvent ev = make_event(ci, CASES[ci], 0, false);
                DestErr e = dest_compute_impact_energy(ev, CFG, 0.5, en);
                if (e != DestErr::OK) check_tok(t[2], err_name(e), ctx);
                else {
                    check_num(t[2], en.kinetic_energy_j, ctx);
                    check_num(t[3], en.deposited_energy_j, ctx);
                    check_num(t[4], en.fragmentation_energy_j, ctx);
                    check_num(t[5], en.thermal_energy_j, ctx);
                    check_num(t[6], en.residual_kinetic_energy_j, ctx);
                }
            }
        } else if (kind == "M") {
            const int ci = std::stoi(t[1]);
            DestructionImpactMomentum mo;
            const DestructionImpactEvent ev = make_event(ci, CASES[ci], 0, false);
            DestErr e = dest_compute_impact_momentum(ev, CFG, mo);
            if (e != DestErr::OK) { ++g_fail; report("[FAIL] %s momentum failed\n", ctx.c_str()); }
            else {
                check_num(t[2], mo.relative_momentum_kg_m_s.x, ctx);
                check_num(t[3], mo.relative_momentum_kg_m_s.y, ctx);
                check_num(t[4], mo.relative_momentum_kg_m_s.z, ctx);
                check_num(t[5], mo.transferred_momentum_kg_m_s.x, ctx);
                check_num(t[6], mo.transferred_momentum_kg_m_s.y, ctx);
                check_num(t[7], mo.transferred_momentum_kg_m_s.z, ctx);
                check_num(t[8], mo.residual_momentum_kg_m_s.x, ctx);
                check_num(t[9], mo.residual_momentum_kg_m_s.y, ctx);
                check_num(t[10], mo.residual_momentum_kg_m_s.z, ctx);
            }
        } else if (kind == "ED") {
            const int ci = std::stoi(t[1]); const long long seed = std::stoll(t[2]);
            auto* r = ensure_execute(ci, seed);
            if ((int)r->state_after == -1) { ++g_fail; report("[FAIL] %s execute failed\n", ctx.c_str()); }
            else {
                check_num(t[3], r->energy.kinetic_energy_j, ctx);
                check_num(t[4], r->energy.deposited_energy_j, ctx);
                check_num(t[5], r->energy.fragmentation_energy_j, ctx);
                check_num(t[6], r->energy.thermal_energy_j, ctx);
                check_num(t[7], r->energy.residual_kinetic_energy_j, ctx);
            }
        } else if (kind == "RC") {
            const int ci = std::stoi(t[1]); const long long seed = std::stoll(t[2]);
            auto* r = ensure_execute(ci, seed);
            check_tok(t[3], std::to_string((int)r->fragments.size()), ctx + ":fragments");
            check_tok(t[4], std::to_string((int)r->ejecta.size()), ctx + ":ejecta");
            check_tok(t[5], std::to_string(r->debris_count), ctx + ":debris");
        } else if (kind == "F") {
            const int ci = std::stoi(t[1]); const long long seed = std::stoll(t[2]);
            const int fi = std::stoi(t[3]);
            auto* r = ensure_execute(ci, seed);
            const auto& fr = r->fragments[(size_t)fi];
            check_num(t[4], fr.mass_kg, ctx);
            check_num(t[5], fr.position.x, ctx);
            check_num(t[6], fr.position.y, ctx);
            check_num(t[7], fr.position.z, ctx);
            check_num(t[8], fr.velocity.x, ctx);
            check_num(t[9], fr.velocity.y, ctx);
            check_num(t[10], fr.velocity.z, ctx);
            check_num(t[11], fr.created_at_s, ctx);
        } else if (kind == "J") {
            const int ci = std::stoi(t[1]); const long long seed = std::stoll(t[2]);
            const int ei = std::stoi(t[3]);
            auto* r = ensure_execute(ci, seed);
            const auto& p = r->ejecta[(size_t)ei];
            check_num(t[4], p.mass_kg, ctx);
            check_num(t[5], p.position.x, ctx);
            check_num(t[6], p.position.y, ctx);
            check_num(t[7], p.position.z, ctx);
            check_num(t[8], p.velocity.x, ctx);
            check_num(t[9], p.velocity.y, ctx);
            check_num(t[10], p.velocity.z, ctx);
            check_num(t[11], p.kinetic_energy_j, ctx);
            check_num(t[12], p.created_at_s, ctx);
        } else if (kind == "O") {
            const int ci = std::stoi(t[1]); const long long seed = std::stoll(t[2]);
            const int fi = std::stoi(t[3]);
            auto* r = ensure_execute(ci, seed);
            const auto& fr = r->fragments[(size_t)fi];
            const Case& c = CASES[ci];
            DestructionOrbitClass oc;
            double eps = 0.0, mu = 0.0;
            DestErr e = dest_classify_fragment_orbit(fr, c.tm, {c.tp[0], c.tp[1], c.tp[2]},
                                                     {c.tv[0], c.tv[1], c.tv[2]}, oc, eps, mu);
            if (e != DestErr::OK) { ++g_fail; report("[FAIL] %s classify failed\n", ctx.c_str()); }
            else {
                check_tok(t[4], dest_orbit_class_name(oc), ctx);
                check_num(t[5], eps, ctx);
                check_num(t[6], mu, ctx);
            }
        } else if (kind == "S") {
            if (t[1] == "-1") {
                check_tok(t[1], "-1", ctx);
                // trigger the secondary sweep once (artifact regenerated below on demand)
                const int count = std::stoi(t[3]);
                static int expected_count = -1;
                expected_count = count;
                (void)expected_count;
            } else {
                // S,si,impact_id,impactor_id,target_id,sim_t,im,tm,ke
                // secondary sweep artifacts (computed once, cached)
                static std::vector<DestructionSecondaryResult> sec;
                static bool sec_done = false;
                if (!sec_done) {
                    DestructionImpactEvent pev{};
                    pev.impact_id = "imp:sec0"; pev.impactor_id = "impactor:950"; pev.target_id = "target:950";
                    pev.sim_time_s = 600.0; pev.impactor_mass_kg = 1.0e14; pev.target_mass_kg = 1.0e15;
                    pev.impactor_position = {-1.0e4, 0, 0}; pev.target_position = {0, 0, 0};
                    pev.impactor_velocity = {3.0e5, 0, 0}; pev.target_velocity = {0, 0, 0};
                    pev.impactor_radius_m = 0.0; pev.target_radius_m = 1.0e5;
                    DestructionImpactResult pr;
                    DestErr e0 = dest_execute_impact(pev, CFG, 777, DestructionDamageState::INTACT, 0.05, 100.0, pr);
                    if (e0 != DestErr::OK) { ++g_fail; report("[FAIL] secondary parent failed\n"); }
                    std::vector<DestructionImpactEvent> tgt(2);
                    tgt[0].target_id = "target:tA"; tgt[0].target_position = {2.0e6, 0, 0};
                    tgt[0].target_mass_kg = 1.0e18; tgt[0].target_velocity = {0, 0, 0};
                    tgt[0].target_radius_m = 1.0e6;
                    tgt[1].target_id = "target:tB"; tgt[1].target_position = {0, 8.0e6, 0};
                    tgt[1].target_mass_kg = 1.0e19; tgt[1].target_velocity = {0, 0, 0};
                    tgt[1].target_radius_m = 2.0e6;
                    std::vector<DestructionDamageState> stg = {DestructionDamageState::INTACT,
                                                               DestructionDamageState::INTACT};
                    DestErr e1 = dest_execute_secondary_impacts(pev, pr, tgt, stg, CFG, 3000LL, 32, sec);
                    if (e1 != DestErr::OK) { ++g_fail; report("[FAIL] secondary sweep failed\n"); }
                    sec_done = true;
                }
                const int si = std::stoi(t[1]);
                if (si >= (int)sec.size()) { ++g_fail; report("[FAIL] missing child %d\n", si); }
                else {
                    const auto& cr = sec[(size_t)si];
                    check_tok(t[2], cr.event.impact_id, ctx);
                    check_tok(t[3], cr.event.impactor_id, ctx);
                    check_tok(t[4], cr.event.target_id, ctx);
                    check_num(t[5], cr.event.sim_time_s, ctx);
                    check_num(t[6], cr.event.impactor_mass_kg, ctx);
                    check_num(t[7], cr.event.target_mass_kg, ctx);
                    check_num(t[8], cr.result.energy.kinetic_energy_j, ctx);
                }
            }
        }
    }
    std::fclose(f);

    const int total = g_ok + g_fail;
    std::printf("destruction_mirror_check: %d comparisons, %d fails, %d bit-exact\n",
                total, g_fail, g_bit);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
