// ASTRA COSMOS — black-hole + spacetime mirror fidelity gate (native vs Python
// authority `astra.blackhole` / `astra.spacetime`). Mission Phase 3 policy:
//   * The C++ mirror issues the IDENTICAL IEEE-754 operation chain as the
//     authority (same order, same libm entry points) for every primitive
//     here. Exact bit equality is therefore REQUIRED for all numeric rows.
//   * ERR: rows validate the exact error taxonomy mapping instead.
//   * Geodesic rows additionally run the integration TWICE natively and
//     require bit-identical results (determinism gate).
// Metrics printed: max abs/rel error, worst-case input, expected vs native.

#include "app/black_hole_sim.h"
#include "app/spacetime_sim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace astra::app;

namespace {

constexpr double M_SUN = 1.989e30;

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
                else if (field == 1) {
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
    out.clear();
    std::string tok;
    for (char c : s) if (c == '|') { out.push_back(std::stod(tok)); tok.clear(); } else tok += c;
    out.push_back(std::stod(tok));
    return !out.empty();
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
        if (rerr > max_rel) { max_rel = rerr; worst_desc = desc; }
        if (native == expected) { ++exact; return; }
        ++fails;
        std::printf("[DIFF] %s native=%.17e expected=%.17e abs=%.3e rel=%.3e\n",
                    desc.c_str(), native, expected, aerr, rerr);
    }
    template <typename E, typename F>
    void outcome(E err, F&& values, const std::string& expected, const std::string& desc,
                 const char* (*tok)(E)) {
        if (expected.rfind("ERR:", 0) == 0) {
            ++total;
            const char* t = tok(err);
            if (t == nullptr || expected != t) {
                ++fails;
                std::printf("[ERR-MISMATCH] %s native=%s expected=%s\n",
                            desc.c_str(), t ? t : "(value)", expected.c_str());
            } else { ++exact; }
            return;
        }
        if (err != E::OK) {
            ++fails; ++total;
            std::printf("[UNEXPECTED-ERR] %s native=%s\n", desc.c_str(), tok(err));
            return;
        }
        std::vector<double> expv;
        toks2vec(expected, expv);
        auto got = values();
        if (got.size() != expv.size()) {
            ++fails; ++total;
            std::printf("[ARITY] %s got=%zu expected=%zu\n", desc.c_str(), got.size(), expv.size());
            return;
        }
        for (size_t i = 0; i < got.size(); ++i)
            measure(got[i], expv[i], desc + ".#" + std::to_string(i));
    }
};

const char* bh_tok(BhErr e) { return e == BhErr::OK ? nullptr : bh_err_name(e); }
const char* st_tok(StErr e) { return e == StErr::OK ? nullptr : st_err_name(e); }

// Metric factory mirror of the shared GENERAL_NUMERICAL test field.
void shared_nf(const double* x, Tensor4& out) {
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out.m[i][j] = 0.0;
    out.m[0][0] = -(1.0 + 0.5 * x[1]);
    out.m[1][1] = 1.0 + 0.25 * x[1];
    out.m[2][2] = 1.0 + 0.25 * x[2];
    out.m[3][3] = 1.0 + 0.125 * x[3];
    out.m[1][2] = out.m[2][1] = 0.03125 * x[1] * x[2];
}

StErr metric_for(const std::string& tag, StMetric& out) {
    if (tag == "mink" || tag == "mink2") { out = st_minkowski_metric(); return StErr::OK; }
    if (tag == "schw" || tag == "schw2" || tag.rfind("schw_err", 0) == 0)
        return st_schwarzschild_metric(M_SUN, out);
    if (tag == "kerr" || tag == "kerr2") return st_kerr_metric(M_SUN, 0.7, out);
    if (tag == "kerr0") return st_kerr_metric(M_SUN, 0.0, out);
    if (tag == "num" || tag == "num2") { out = st_numerical_metric(&shared_nf, StChart::CARTESIAN); return StErr::OK; }
    return StErr::INVALID_COORDINATE;
}

void coords4of(const std::vector<std::string>& in, size_t off, double out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = pnum(in[off + i]);
}

std::vector<double> flat16(const Tensor4& t) {
    std::vector<double> v;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) v.push_back(t.m[i][j]);
    return v;
}

void geo_run(const std::string& tag, const std::vector<double>& in2, Metrics& m,
             const std::string& E, const std::string& desc) {
    StMetric mt;
    if (metric_for(tag, mt) != StErr::OK) { ++m.fails; ++m.total; return; }
    double c0[4], u0[4];
    for (int i = 0; i < 4; ++i) c0[i] = in2[i];
    for (int i = 0; i < 4; ++i) u0[i] = in2[4 + i];
    const double limit = in2[8];
    const int steps = static_cast<int>(in2[9]);
    const bool adaptive = in2[10] != 0.0;
    GeodesicSolution s1, s2;
    const StErr e = st_integrate_geodesic(mt, c0, u0, limit, steps, adaptive, 1.0e-10, s1);
    if (E.rfind("ERR:", 0) == 0) {
        ++m.total;
        if (e == StErr::OK || E != st_err_name(e)) {
            ++m.fails;
            std::printf("[GEO-ERR] %s native=%s expected=%s\n", desc.c_str(),
                        e == StErr::OK ? "(value)" : st_err_name(e), E.c_str());
        }
        return;
    }
    if (e != StErr::OK) {
        ++m.fails; ++m.total;
        std::printf("[GEO-UNEXPECTED-ERR] %s native=%s\n", desc.c_str(), st_err_name(e));
        return;
    }
    // Determinism: identical second run, bit by bit.
    const StErr e2 = st_integrate_geodesic(mt, c0, u0, limit, steps, adaptive, 1.0e-10, s2);
    if (e2 != StErr::OK || s1.parameters != s2.parameters ||
        s1.coordinates.back() != s2.coordinates.back()) {
        ++m.fails; ++m.total;
        std::printf("[GEO-NONDET] %s\n", desc.c_str());
        return;
    }
    std::vector<double> got = {
        static_cast<double>(s1.parameters.size()),
        s1.coordinates.back()[0], s1.coordinates.back()[1],
        s1.coordinates.back()[2], s1.coordinates.back()[3],
        s1.four_velocities.back()[0], s1.four_velocities.back()[1],
        s1.four_velocities.back()[2], s1.four_velocities.back()[3]};
    std::vector<double> exp;
    toks2vec(E, exp);
    for (size_t i = 0; i < std::min(got.size(), exp.size()); ++i)
        m.measure(got[i], exp[i], desc + ".#" + std::to_string(i));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <bhst_reference.csv>\n", argv[0]); return 2; }
    const std::vector<Row> rows = load_csv(argv[1]);
    Metrics m;
    for (const Row& r : rows) {
        const std::string& E = r.expected;
        std::vector<double> in(r.inputs.size());
        // Metric-tagged rows carry the model tag as first input token.
        std::vector<double> tagged;
        StMetric mt;
        bool have_mt = false;
        const bool is_tagged = !r.inputs.empty() &&
            (r.inputs[0] == "mink" || r.inputs[0] == "mink2" ||
             r.inputs[0].rfind("schw", 0) == 0 || r.inputs[0].rfind("kerr", 0) == 0 ||
             r.inputs[0] == "num" || r.inputs[0] == "num2");
        if (is_tagged) {
            have_mt = metric_for(r.inputs[0], mt) == StErr::OK;
            for (size_t i = 1; i < r.inputs.size(); ++i) tagged.push_back(pnum(r.inputs[i]));
        } else {
            for (size_t i = 0; i < in.size(); ++i) in[i] = pnum(r.inputs[i]);
        }
        const std::string desc = r.fn + "(" + r.inputs[0] + ")";

        if (r.fn == "bh_mass") {
            double out; const BhErr e = bh_validate_mass_kg(in[0], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_spin") {
            double out; const BhErr e = bh_validate_spin_param(in[0], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_create") {
            BlackHoleState s; const BhErr e = bh_create_black_hole(in[0], in[1], s);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{s.mass_kg, s.spin_param}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_model") {
            BlackHoleState s; bh_create_black_hole(M_SUN, in[0], s);
            ++m.total;
            const std::string got = black_hole_model_name(s.model());
            if (got != E) { ++m.fails; std::printf("[MODEL] %s got=%s expected=%s\n", desc.c_str(), got.c_str(), E.c_str()); }
            else ++m.exact;
        } else if (r.fn == "bh_rg" || r.fn == "bh_spinlen") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            const double v = r.fn == "bh_rg" ? s.gravitational_radius() : s.spin_length();
            m.measure(v, pnum(E), desc);
        } else if (r.fn == "bh_bdict") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            std::vector<double> exp; toks2vec(E, exp);
            m.measure(s.mass_kg, exp[0], desc + ".m");
            m.measure(s.spin_param, exp[1], desc + ".a");
        } else if (r.fn == "bh_rs" || r.fn == "bh_isco" || r.fn == "bh_photon") {
            double out = 0;
            const BhErr e = r.fn == "bh_rs" ? bh_schwarzschild_radius_m(in[0], out)
                          : r.fn == "bh_isco" ? bh_isco_radius(in[0], out)
                                              : bh_photon_sphere_radius(in[0], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_sdil") {
            BlackHoleState s; bh_create_black_hole(in[0], 0.0, s);
            double out; const BhErr e = bh_gravitational_time_dilation_schwarzschild(s, in[1], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_fdtdil" || r.fn == "bh_fdtdil_k") {
            BlackHoleState s; bh_create_black_hole(in[0], in.size() > 2 ? in[1] : 0.0, s);
            const double radius = in.size() > 2 ? in[2] : in[1];
            double out; const BhErr e = bh_gravitational_time_dilation(s, radius, out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_z") {
            BlackHoleState s; bh_create_black_hole(in[0], 0.0, s);
            double out; const BhErr e = bh_gravitational_redshift(s, in[1], in[2], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_z_k") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_gravitational_redshift(s, in[2], in[3], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_sb") {
            BlackHoleState s; bh_create_black_hole(in[0], 0.0, s);
            SchwarzschildBoundaries b; const BhErr e = bh_get_schwarzschild_boundaries(s, b);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{
                b.schwarzschild_radius, b.gravitational_radius,
                b.photon_sphere_radius, b.isco_radius}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_kh") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double rp, rm; const BhErr e = bh_kerr_horizons(s, rp, rm);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{rp, rm}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_ergo") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_kerr_ergosphere_radius(s, in[2], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_kisco" || r.fn == "bh_kphoton") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double a = 0, b = 0;
            BhErr e;
            if (r.fn == "bh_kisco") { e = bh_kerr_isco_prograde(s, a); if (e == BhErr::OK) e = bh_kerr_isco_retrograde(s, b); }
            else { e = bh_kerr_photon_orbit_prograde(s, a); if (e == BhErr::OK) e = bh_kerr_photon_orbit_retrograde(s, b); }
            m.outcome<BhErr>(e, [&] { return std::vector<double>{a, b}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_kb") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            KerrBoundaries kb; const BhErr e = bh_get_kerr_boundaries(s, kb);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{
                kb.r_plus, kb.r_minus, kb.ergosphere_equatorial, kb.ergosphere_polar,
                kb.isco_prograde, kb.isco_retrograde,
                kb.photon_orbit_prograde, kb.photon_orbit_retrograde}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_omega") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_kerr_frame_dragging_angular_velocity(s, in[2], 1.5707963267948966, out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_omega_th") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_kerr_frame_dragging_angular_velocity(s, in[2], in[3], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_fdv") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_equatorial_frame_dragging_velocity(s, in[2], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "bh_ktosd") {
            BlackHoleState s; bh_create_black_hole(in[0], in[1], s);
            double out; const BhErr e = bh_kerr_static_time_dilation_equatorial(s, in[2], out);
            m.outcome<BhErr>(e, [&] { return std::vector<double>{out}; }, E, desc, bh_tok);
        } else if (r.fn == "st_event") {
            double out4[4]; const StErr e = st_event_from_coordinates(in[0], in[1], in[2], in[3], StChart::CARTESIAN, out4);
            m.outcome<StErr>(e, [&] { return std::vector<double>(out4, out4 + 4); }, E, desc, st_tok);
        } else if (r.fn == "st_c2s") {
            double rr, th, ph; const StErr e = st_cartesian_to_spherical(in[0], in[1], in[2], rr, th, ph);
            m.outcome<StErr>(e, [&] { return std::vector<double>{rr, th, ph}; }, E, desc, st_tok);
        } else if (r.fn == "st_s2c") {
            double x, y, z; const StErr e = st_spherical_to_cartesian(in[0], in[1], in[2], x, y, z);
            m.outcome<StErr>(e, [&] { return std::vector<double>{x, y, z}; }, E, desc, st_tok);
        } else if (r.fn == "mt_tensor") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            Tensor4 t; const StErr e = st_metric_tensor(mt, cc, t);
            m.outcome<StErr>(e, [&] { return flat16(t); }, E, desc, st_tok);
        } else if (r.fn == "mt_inv") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            Tensor4 t; const StErr e = st_metric_inverse(mt, cc, t);
            m.outcome<StErr>(e, [&] { return flat16(t); }, E, desc, st_tok);
        } else if (r.fn == "mt_det") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            double out; const StErr e = st_metric_determinant(mt, cc, out);
            m.outcome<StErr>(e, [&] { return std::vector<double>{out}; }, E, desc, st_tok);
        } else if (r.fn == "mt_deriv") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            DerivTensor d; const StErr e = st_metric_derivative(mt, cc, d);
            m.outcome<StErr>(e, [&] {
                std::vector<double> v;
                for (int a = 0; a < 4; ++a) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) v.push_back(d.m[a][i][j]);
                return v; }, E, desc, st_tok);
        } else if (r.fn == "mt_gamma") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            GammaTensor g; const StErr e = st_christoffel_symbols(mt, cc, g);
            m.outcome<StErr>(e, [&] {
                std::vector<double> v;
                for (int a = 0; a < 4; ++a) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) v.push_back(g.m[a][i][j]);
                return v; }, E, desc, st_tok);
        } else if (r.fn == "mt_dgamma") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            GammaDerivTensor g; const StErr e = st_christoffel_derivative(mt, cc, g);
            m.outcome<StErr>(e, [&] {
                std::vector<double> v;
                for (int a = 0; a < 4; ++a) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k)
                    v.push_back(g.m[a][i][j][k]);
                return v; }, E, desc, st_tok);
        } else if (r.fn == "mt_riemann") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            RiemannTensor t; const StErr e = st_riemann_tensor(mt, cc, t);
            m.outcome<StErr>(e, [&] {
                std::vector<double> v;
                for (int a = 0; a < 4; ++a) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k)
                    v.push_back(t.m[a][i][j][k]);
                return v; }, E, desc, st_tok);
        } else if (r.fn == "mt_ricci") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            Tensor4 t; const StErr e = st_ricci_tensor(mt, cc, t);
            m.outcome<StErr>(e, [&] { return flat16(t); }, E, desc, st_tok);
        } else if (r.fn == "mt_einstein") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            Tensor4 t; const StErr e = st_einstein_tensor(mt, cc, t);
            m.outcome<StErr>(e, [&] { return flat16(t); }, E, desc, st_tok);
        } else if (r.fn == "mt_rscalar") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            double out; const StErr e = st_ricci_scalar(mt, cc, out);
            m.outcome<StErr>(e, [&] { return std::vector<double>{out}; }, E, desc, st_tok);
        } else if (r.fn == "mt_kret") {
            double cc[4]; for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            double out; const StErr e = st_kretschmann_scalar(mt, cc, out);
            m.outcome<StErr>(e, [&] { return std::vector<double>{out}; }, E, desc, st_tok);
        } else if (r.fn == "mt_tidal") {
            double cc[4], u4[4], x4[4], a4[4];
            for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            for (int i = 0; i < 4; ++i) u4[i] = tagged[4 + i];
            for (int i = 0; i < 4; ++i) x4[i] = tagged[8 + i];
            const StErr e = st_tidal_acceleration(mt, cc, u4, x4, a4);
            m.outcome<StErr>(e, [&] { return std::vector<double>(a4, a4 + 4); }, E, desc, st_tok);
        } else if (r.fn == "st_ds2") {
            double a[4], b[4];
            for (int i = 0; i < 4; ++i) { a[i] = tagged[i]; b[i] = tagged[4 + i]; }
            double out; const StErr e = st_local_interval(mt, a, b, out);
            m.outcome<StErr>(e, [&] { return std::vector<double>{out}; }, E, desc, st_tok);
        } else if (r.fn == "st_classify") {
            double a[4], b[4];
            for (int i = 0; i < 4; ++i) { a[i] = tagged[i]; b[i] = tagged[4 + i]; }
            IntervalType it; const StErr e = st_classify_interval(mt, a, b, it);
            if (E.rfind("ERR:", 0) == 0) {
                ++m.total;
                if (e == StErr::OK || E != st_err_name(e)) { ++m.fails; std::printf("[CLASS-ERR] %s\n", desc.c_str()); }
            } else if (e != StErr::OK) {
                ++m.fails; ++m.total; std::printf("[CLASS-UNEXPECTED] %s native=%s\n", desc.c_str(), st_err_name(e));
            } else {
                ++m.total;
                const char* got = it == IntervalType::TIMELIKE ? "TIMELIKE"
                                : it == IntervalType::SPACELIKE ? "SPACELIKE" : "NULL";
                if (E != got) { ++m.fails; std::printf("[CLASS] %s got=%s expected=%s\n", desc.c_str(), got, E.c_str()); }
                else ++m.exact;
            }
        } else if (r.fn == "st_nullray") {
            double cc[4], d3[3], f4[4], p4[4];
            for (int i = 0; i < 4; ++i) cc[i] = tagged[i];
            for (int i = 0; i < 3; ++i) d3[i] = tagged[4 + i];
            const StErr e = st_null_ray_directions(mt, cc, d3, f4, p4);
            m.outcome<StErr>(e, [&] {
                return std::vector<double>{f4[0], f4[1], f4[2], f4[3], p4[0], p4[1], p4[2], p4[3]};
            }, E, desc, st_tok);
        } else if (r.fn == "st_c2c") {
            const bool ml = tagged[7] != 0.0;
            RelVec3 v{tagged[4], tagged[5], tagged[6]};
            double co[4], uu[4];
            const StErr e = st_cartesian_state_to_chart(mt, tagged[0], tagged[1], tagged[2], tagged[3], v, ml, co, uu);
            m.outcome<StErr>(e, [&] {
                return std::vector<double>{co[0], co[1], co[2], co[3], uu[0], uu[1], uu[2], uu[3]};
            }, E, desc, st_tok);
        } else if (r.fn.rfind("geo_", 0) == 0) {
            geo_run(r.inputs[0], tagged, m, E, desc);
        } else {
            ++m.fails; ++m.total;
            std::printf("[UNKNOWN-FN] %s\n", r.fn.c_str());
        }
        (void)have_mt;
    }
    std::printf("blackhole_spacetime_mirror_check: %d comparisons, %d fails, %d bit-exact\n",
                m.total, m.fails, m.exact);
    std::printf("  max abs err: %.3e  max rel err: %.3e  worst: %s\n",
                m.max_abs, m.max_rel, m.worst_desc.c_str());
    std::printf(m.fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return m.fails == 0 ? 0 : 1;
}
