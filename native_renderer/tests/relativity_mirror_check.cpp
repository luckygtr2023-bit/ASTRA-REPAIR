// ASTRA COSMOS — relativity mirror fidelity gate (native vs Python authority
// `astra.relativity`). Mission Phase 3 tolerance policy:
//   * The C++ mirror issues the IDENTICAL IEEE-754 operation chain as the
//     authority for every primitive here (verified source-audit, same order).
//     Therefore exact bit equality is REQUIRED for all numeric rows — not a
//     cosmetic choice but the mathematically appropriate semantics for an
//     op-order-identical mirror (no fast-math anywhere in the build path).
//   * ERR: rows validate the exact error taxonomy mapping instead.
//   * If exactness were ever lost, the report would require an explicit
//     root-cause (never a loose default tolerance smeared over everything).
// Metrics printed both ways: max abs/rel error, worst-case input, expected
// vs native value.

#include "app/relativity_sim.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace astra::app;

namespace {

struct Row { std::string fn; std::vector<std::string> inputs; std::string expected; };

std::vector<Row> load_csv(const char* path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::vector<Row> rows;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        Row r;
        std::string cur;
        int field = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            const char c = i < line.size() ? line[i] : ',';
            if (c == ',') {
                if (field == 0) r.fn = cur;
                else if (field == 1) { // split inputs on '|'
                    std::string tok;
                    for (char d : cur) if (d == '|') { r.inputs.push_back(tok); tok.clear(); } else tok += d;
                    r.inputs.push_back(tok);
                } else { r.expected = cur; }
                cur.clear(); ++field;
            } else cur += c;
        }
        if (!r.fn.empty()) rows.push_back(r);
    }
    return rows;
}

double pnum(const std::string& s) { return std::stod(s); }

bool toks2vec(const std::string& s, std::vector<double>& out) {
    // expected numeric payload: single value or a|b|c...
    out.clear();
    std::string tok;
    for (char c : s) if (c == '|') { out.push_back(std::stod(tok)); tok.clear(); } else tok += c;
    out.push_back(std::stod(tok));
    return !out.empty();
}

const char* err_token_for(RelErr e) {
    switch (e) {
        case RelErr::INVALID_VELOCITY: return "ERR:InvalidVelocityError";
        case RelErr::LIGHT_SPEED_VIOLATION: return "ERR:LightSpeedViolation";
        case RelErr::INVALID_REST_MASS: return "ERR:InvalidRestMassError";
        case RelErr::SPACELIKE_INTERVAL: return "ERR:SpacelikeIntervalError";
        case RelErr::DEGENERATE_METRIC: return "ERR:DegenerateMetricError";
        default: return nullptr;
    }
}

struct Metrics {
    int total = 0, fails = 0, exact = 0;
    double max_abs = 0.0, max_rel = 0.0;
    std::string worst_desc;
    void measure(double native, double expected, const std::string& desc) {
        ++total;
        const double aerr = std::fabs(native - expected);
        const double rerr = aerr / (std::fabs(expected) + 1e-300);
        if (aerr > max_abs) max_abs = aerr;
        if (rerr > max_rel || (rerr == max_rel && max_rel > 0 && aerr >= max_abs)) {
            if (rerr > max_rel) { max_rel = rerr; worst_desc = desc; }
        }
        if (native == expected) { ++exact; return; }
        ++fails;
        std::printf("[DIFF] %s native=%.17e expected=%.17e abs=%.3e rel=%.3e\n",
                    desc.c_str(), native, expected, aerr, rerr);
    }
    void check_err(RelErr e, const std::string& expected, const std::string& desc) {
        ++total;
        const char* tok = err_token_for(e);
        if (tok == nullptr || expected != tok) {
            ++fails;
            std::printf("[ERR-MISMATCH] %s native=%s expected=%s\n",
                        desc.c_str(), tok ? tok : "(value)", expected.c_str());
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <relativity_reference.csv>\n", argv[0]); return 2; }
    const std::vector<Row> rows = load_csv(argv[1]);
    Metrics m;
    for (const Row& r : rows) {
        const std::string& E = r.expected;
        const bool expect_err = E.rfind("ERR:", 0) == 0;
        std::vector<double> in(r.inputs.size());
        for (size_t i = 0; i < in.size(); ++i) in[i] = pnum(r.inputs[i]);
        const std::string desc = r.fn + "(" + r.inputs[0] + ")";

        if (r.fn == "beta" || r.fn == "lorentz") {
            double out = 0;
            const RelErr e = r.fn == "beta" ? beta(in[0], out) : lorentz_factor(in[0], out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else m.measure(out, pnum(E), desc);
        } else if (r.fn == "relmass" || r.fn == "totale" || r.fn == "kinetic") {
            double out = 0;
            const RelErr e = r.fn == "relmass" ? relativistic_mass(in[0], in[1], out)
                           : r.fn == "totale" ? total_energy(in[0], in[1], out)
                                              : kinetic_energy(in[0], in[1], out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else m.measure(out, pnum(E), desc);
        } else if (r.fn == "momentum") {
            RelVec3 p;
            const RelVec3 v{in[1], in[2], in[3]};
            const RelErr e = relativistic_momentum(in[0], v, p);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else {
                std::vector<double> exp3;
                toks2vec(E, exp3);
                m.measure(p.x, exp3[0], desc + ".px");
                m.measure(p.y, exp3[1], desc + ".py");
                m.measure(p.z, exp3[2], desc + ".pz");
            }
        } else if (r.fn == "invariant") {
            const RelFourVector fv{in[0], in[1], in[2], in[3]};
            m.measure(fv.invariant_sq(), pnum(E), desc);
        } else if (r.fn == "interval") {
            const RelFourVector fv{in[0], in[1], in[2], in[3]};
            ++m.total;
            const std::string got = interval_type_name(fv.interval_type());
            if (got != E) { ++m.fails; std::printf("[INTERVAL] %s got=%s expected=%s\n", desc.c_str(), got.c_str(), E.c_str()); }
            else ++m.exact;
        } else if (r.fn == "ptime") {
            const RelFourVector a = event_from_coordinates(in[0], in[1], in[2], in[3]);
            const RelFourVector b = event_from_coordinates(in[4], in[5], in[6], in[7]);
            double out = 0;
            const RelErr e = proper_time_between(a, b, out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else m.measure(out, pnum(E), desc);
        } else if (r.fn == "boostx" || r.fn == "invboostx") {
            const RelFourVector fv{in[0], in[1], in[2], in[3]};
            RelFourVector out;
            const RelErr e = r.fn == "boostx" ? boost_x(fv, in[4], out)
                                              : inverse_boost_x(fv, in[4], out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else {
                std::vector<double> exp4;
                toks2vec(E, exp4);
                m.measure(out.t, exp4[0], desc + ".t");
                m.measure(out.x, exp4[1], desc + ".x");
                m.measure(out.y, exp4[2], desc + ".y");
                m.measure(out.z, exp4[3], desc + ".z");
            }
        } else if (r.fn == "rs") {
            double out = 0;
            const RelErr e = schwarzschild_radius(in[0], out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else m.measure(out, pnum(E), desc);
        } else if (r.fn == "wftd") {
            double out = 0;
            const RelErr e = weak_field_time_dilation(in[0], in[1], out);
            if (expect_err) m.check_err(e, E, desc);
            else if (e != RelErr::OK) { ++m.fails; ++m.total; std::printf("[UNEXPECTED-ERR] %s\n", desc.c_str()); }
            else m.measure(out, pnum(E), desc);
        } else {
            ++m.fails; ++m.total;
            std::printf("[UNKNOWN-FN] %s\n", r.fn.c_str());
        }
    }
    std::printf("relativity_mirror_check: %d comparisons, %d fails, %d bit-exact\n",
                m.total, m.fails, m.exact);
    std::printf("  max abs err: %.3e  max rel err: %.3e  worst: %s\n",
                m.max_abs, m.max_rel, m.worst_desc.c_str());
    std::printf(m.fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return m.fails == 0 ? 0 : 1;
}
