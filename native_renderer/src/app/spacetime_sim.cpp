// Native mirror of astra.spacetime — op-order-exact against the Python
// authority. Conventions: (-,+,+,+); x^0 = ct (metres). The Matrix4 cofactor
// determinant/inverse of astra.mathematics.matrices is mirrored verbatim so
// the metric inverse bit-chain is identical. DP-fractions are written as
// compile-time IEEE divisions identical to Python's runtime float division.

#include "spacetime_sim.h"
#include "black_hole_sim.h"  // Kerr metric ctor: state validation + horizons

#include <cmath>
#include <cstring>

namespace astra::app {

const char* st_err_name(StErr e) {
    switch (e) {
        case StErr::OK: return "OK";
        case StErr::INVALID_COORDINATE: return "ERR:InvalidCoordinateError";
        case StErr::DEGENERATE_METRIC: return "ERR:DegenerateMetricError";
        case StErr::HORIZON_CROSSING: return "ERR:HorizonCrossingError";
        case StErr::GEODESIC_DIVERGENCE: return "ERR:GeodesicDivergenceError";
        case StErr::LIGHT_SPEED_VIOLATION: return "ERR:LightSpeedViolation";
        case StErr::INVALID_MASS: return "ERR:InvalidBlackHoleMassError";
        case StErr::INVALID_SPIN: return "ERR:InvalidSpinParameterError";
        case StErr::NOT_IMPLEMENTED: return "ERR:NotImplementedError";
    }
    return "ERR:unknown";
}

const char* st_chart_name(StChart c) { return c == StChart::CARTESIAN ? "cartesian" : "spherical"; }

const char* spacetime_model_name(SpacetimeModel m) {
    switch (m) {
        case SpacetimeModel::MINKOWSKI: return "MINKOWSKI";
        case SpacetimeModel::SCHWARZSCHILD: return "SCHWARZSCHILD";
        case SpacetimeModel::KERR: return "KERR";
        case SpacetimeModel::GENERAL_NUMERICAL: return "GENERAL_NUMERICAL";
    }
    return "?";
}

// ---- events.py ----------------------------------------------------------------
StErr st_event_from_coordinates(double time_sec, double x, double y, double z,
                                StChart chart, double out4[4]) {
    if (std::isnan(time_sec) || std::isinf(time_sec)) return StErr::INVALID_COORDINATE;
    const double ct = time_sec * SPEED_OF_LIGHT;
    if (std::isnan(x) || std::isinf(x) || std::isnan(y) || std::isinf(y) ||
        std::isnan(z) || std::isinf(z)) return StErr::INVALID_COORDINATE;
    out4[0] = ct; out4[1] = x; out4[2] = y; out4[3] = z;
    return StErr::OK;
}

StErr st_cartesian_to_spherical(double x, double y, double z,
                                double& out_r, double& out_theta, double& out_phi) {
    if (std::isnan(x) || std::isinf(x) || std::isnan(y) || std::isinf(y) ||
        std::isnan(z) || std::isinf(z)) return StErr::INVALID_COORDINATE;
    const double r = std::sqrt(x * x + y * y + z * z);
    if (r == 0.0) return StErr::INVALID_COORDINATE;  // origin: physical singularity
    const double ratio = std::fmax(-1.0, std::fmin(1.0, z / r));  // max(-1, min(1, z/r))
    out_r = r;
    out_theta = std::acos(ratio);
    out_phi = std::atan2(y, x);
    return StErr::OK;
}

StErr st_spherical_to_cartesian(double r, double theta, double phi,
                                double& out_x, double& out_y, double& out_z) {
    if (std::isnan(r) || std::isinf(r) || std::isnan(theta) || std::isinf(theta) ||
        std::isnan(phi) || std::isinf(phi)) return StErr::INVALID_COORDINATE;
    if (r < 0.0) return StErr::INVALID_COORDINATE;
    out_x = r * std::sin(theta) * std::cos(phi);
    out_y = r * std::sin(theta) * std::sin(phi);
    out_z = r * std::cos(theta);
    return StErr::OK;
}

// ---- metric.py -----------------------------------------------------------------
namespace {

// Mirror of metric._validate_coords: finite + spherical-patch guards.
StErr st_validate_coords(const double coords4[4], StChart chart, double out4[4]) {
    for (int i = 0; i < 4; ++i) {
        const double v = coords4[i];
        if (std::isnan(v) || std::isinf(v)) return StErr::INVALID_COORDINATE;
        out4[i] = v;
    }
    if (chart == StChart::SPHERICAL) {
        const double r = out4[1], theta = out4[2];
        if (r <= 0.0) return StErr::DEGENERATE_METRIC;       // r = 0 singularity
        if (theta <= 0.0 || theta >= ST_PI) return StErr::DEGENERATE_METRIC;  // polar axis
    }
    return StErr::OK;
}

double det3_of(const double* m, const int rows[3], const int cols[3]) {
    // Matrix3.determinant: a00*(a11*a22-a12*a21) - a01*(a10*a22-a12*a20)
    //                    + a02*(a10*a21-a11*a20)
    const double a00 = m[rows[0] * 4 + cols[0]], a01 = m[rows[0] * 4 + cols[1]],
                 a02 = m[rows[0] * 4 + cols[2]];
    const double a10 = m[rows[1] * 4 + cols[0]], a11 = m[rows[1] * 4 + cols[1]],
                 a12 = m[rows[1] * 4 + cols[2]];
    const double a20 = m[rows[2] * 4 + cols[0]], a21 = m[rows[2] * 4 + cols[1]],
                 a22 = m[rows[2] * 4 + cols[2]];
    return (a00 * (a11 * a22 - a12 * a21)
            - a01 * (a10 * a22 - a12 * a20)
            + a02 * (a10 * a21 - a11 * a20));
}

double matrix4_determinant(const Tensor4& t) {
    // Matrix4.determinant via row-0 minors of Matrix3 determinants.
    const double* m = &t.m[0][0];
    static const int R123[3] = {1, 2, 3};
    static const int C123[3] = {1, 2, 3}, C023[3] = {0, 2, 3},
                     C013[3] = {0, 1, 3}, C012[3] = {0, 1, 2};
    return (t.m[0][0] * det3_of(m, R123, C123)
            - t.m[0][1] * det3_of(m, R123, C023)
            + t.m[0][2] * det3_of(m, R123, C013)
            - t.m[0][3] * det3_of(m, R123, C012));
}

// Matrix4.inverse — cofactor/adjugate, verbatim op order; OK=false on
// |det| < ST_SINGULAR_TOL (the authority's ValueError).
bool matrix4_inverse(const Tensor4& t, Tensor4& out) {
    double m[4][4];
    std::memcpy(m, t.m, sizeof m);
    double cof[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double minor[3][3];
            int mi = 0;
            for (int i = 0; i < 4; ++i) {
                if (i == r) continue;
                int mj = 0;
                for (int j = 0; j < 4; ++j) {
                    if (j == c) continue;
                    minor[mi][mj++] = m[i][j];
                }
                ++mi;
            }
            const double det3 =
                (minor[0][0] * (minor[1][1] * minor[2][2] - minor[1][2] * minor[2][1])
                 - minor[0][1] * (minor[1][0] * minor[2][2] - minor[1][2] * minor[2][0])
                 + minor[0][2] * (minor[1][0] * minor[2][1] - minor[1][1] * minor[2][0]));
            cof[r][c] = (((r + c) % 2 == 0) ? 1.0 : -1.0) * det3;  // (-1)**(r+c)
        }
    }
    // det = sum(m[0][c] * cof[0][c]) starting from 0 — same left-to-right sum.
    double det = 0.0;
    for (int c = 0; c < 4; ++c) det += m[0][c] * cof[0][c];
    if (std::fabs(det) < ST_SINGULAR_TOL) return false;
    const double inv_det = 1.0 / det;
    // adj = transpose of cofactor matrix.
    double adj[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) adj[r][c] = cof[c][r];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.m[r][c] = adj[r][c] * inv_det;
    return true;
}

// Base-class numeric derivative (KERR / GENERAL_NUMERICAL path).
StErr st_metric_derivative_numeric(const StMetric& metric, const double coords4[4],
                                   DerivTensor& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    for (int a = 0; a < 4; ++a) {
        const double h = ST_DIFF_STEP * std::fmax(std::fabs(x[a]), 1.0);
        double xp[4], xm[4], x2p[4], x2m[4];
        std::memcpy(xp, x, sizeof xp); std::memcpy(xm, x, sizeof xm);
        std::memcpy(x2p, x, sizeof x2p); std::memcpy(x2m, x, sizeof x2m);
        xp[a] += h; xm[a] -= h; x2p[a] += 2.0 * h; x2m[a] -= 2.0 * h;
        Tensor4 gp, gm, g2p, g2m;
        StErr e;
        if ((e = st_metric_tensor(metric, xp, gp)) != StErr::OK) return e;
        if ((e = st_metric_tensor(metric, xm, gm)) != StErr::OK) return e;
        if ((e = st_metric_tensor(metric, x2p, g2p)) != StErr::OK) return e;
        if ((e = st_metric_tensor(metric, x2m, g2m)) != StErr::OK) return e;
        for (int mu = 0; mu < 4; ++mu)
            for (int nu = 0; nu < 4; ++nu)
                out.m[a][mu][nu] =
                    (-g2p.m[mu][nu] + 8.0 * gp.m[mu][nu] - 8.0 * gm.m[mu][nu] + g2m.m[mu][nu])
                    / (12.0 * h);
    }
    return StErr::OK;
}

} // namespace

StMetric st_minkowski_metric() {
    StMetric m;
    m.model = SpacetimeModel::MINKOWSKI;
    m.chart = StChart::CARTESIAN;
    return m;
}

StErr st_schwarzschild_metric(double mass_kg, StMetric& out) {
    StMetric m;
    double rs;
    const BhErr e = bh_schwarzschild_radius_m(mass_kg, rs);  // validates mass
    if (e == BhErr::INVALID_MASS) return StErr::INVALID_MASS;
    if (e != BhErr::OK) return StErr::INVALID_COORDINATE;
    m.model = SpacetimeModel::SCHWARZSCHILD;
    m.chart = StChart::SPHERICAL;
    m.rs_m = rs;
    out = m;
    return StErr::OK;
}

StErr st_kerr_metric(double mass_kg, double spin_param, StMetric& out) {
    StMetric m;
    BlackHoleState state{mass_kg, spin_param};
    const BhErr ev = bh_create_black_hole(mass_kg, spin_param, state);
    if (ev == BhErr::INVALID_MASS) return StErr::INVALID_MASS;
    if (ev == BhErr::INVALID_SPIN) return StErr::INVALID_SPIN;
    if (ev != BhErr::OK) return StErr::INVALID_COORDINATE;
    double r_plus, r_minus;
    const BhErr eh = bh_kerr_horizons(state, r_plus, r_minus);
    if (eh != BhErr::OK) return StErr::INVALID_COORDINATE;
    m.model = SpacetimeModel::KERR;
    m.chart = StChart::SPHERICAL;
    m.r_g_m = state.gravitational_radius();
    m.a_m = state.spin_length();
    m.r_plus_m = r_plus;
    out = m;
    return StErr::OK;
}

StMetric st_numerical_metric(std::function<void(const double* x4, Tensor4& out)> field,
                             StChart chart) {
    StMetric m;
    m.model = SpacetimeModel::GENERAL_NUMERICAL;
    m.chart = chart;
    m.field = std::move(field);
    return m;
}

namespace {

StErr tensor_unvalidated(const StMetric& metric, const double x[4], Tensor4& out) {
    switch (metric.model) {
        case SpacetimeModel::MINKOWSKI: {
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out.m[i][j] = 0.0;
            out.m[0][0] = -1.0; out.m[1][1] = 1.0; out.m[2][2] = 1.0; out.m[3][3] = 1.0;
            return StErr::OK;
        }
        case SpacetimeModel::SCHWARZSCHILD: {
            const double r = x[1], theta = x[2];
            // _f(r): metric factor guarded to the coordinate patch.
            if (r <= metric.rs_m) return StErr::DEGENERATE_METRIC;
            const double f = 1.0 - metric.rs_m / r;
            const double st = std::sin(theta);
            const double sin2 = st * st;   // math.sin(theta)**2
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out.m[i][j] = 0.0;
            out.m[0][0] = -f;
            out.m[1][1] = 1.0 / f;
            out.m[2][2] = r * r;
            out.m[3][3] = r * r * sin2;
            return StErr::OK;
        }
        case SpacetimeModel::KERR: {
            const double r = x[1], theta = x[2];
            // _delta(r): patch guard r > r_+.
            if (r <= metric.r_plus_m) return StErr::DEGENERATE_METRIC;
            const double r_g = metric.r_g_m, a = metric.a_m;
            const double delta = r * r - 2.0 * r_g * r + a * a;
            const double sin_t = std::sin(theta), cos_t = std::cos(theta);
            const double sin2 = sin_t * sin_t;
            const double sigma = r * r + a * a * cos_t * cos_t;
            const double g_tt = -(1.0 - 2.0 * r_g * r / sigma);
            const double g_rr = sigma / delta;
            const double g_thth = sigma;
            const double g_tph = -2.0 * a * r_g * r * sin2 / sigma;
            const double base = r * r + a * a;
            const double g_phph = (base * base - a * a * delta * sin2) * sin2 / sigma;
            std::memset(&out, 0, sizeof out);
            out.m[0][0] = g_tt;
            out.m[0][3] = g_tph;
            out.m[1][1] = g_rr;
            out.m[2][2] = g_thth;
            out.m[3][0] = g_tph;
            out.m[3][3] = g_phph;
            return StErr::OK;
        }
        case SpacetimeModel::GENERAL_NUMERICAL: {
            if (!metric.field) return StErr::INVALID_COORDINATE;
            metric.field(x, out);
            // Authority: NaN/Inf components -> InvalidCoordinateError.
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    if (std::isnan(out.m[i][j]) || std::isinf(out.m[i][j]))
                        return StErr::INVALID_COORDINATE;
            return StErr::OK;
        }
    }
    return StErr::INVALID_COORDINATE;
}

} // namespace

StErr st_metric_tensor(const StMetric& metric, const double coords4[4], Tensor4& out) {
    double x[4];
    const StErr e = st_validate_coords(coords4, metric.chart, x);
    if (e != StErr::OK) return e;
    return tensor_unvalidated(metric, x, out);
}

StErr st_metric_derivative(const StMetric& metric, const double coords4[4], DerivTensor& out) {
    if (metric.model == SpacetimeModel::MINKOWSKI) {
        double x[4];  // validation parity (unused otherwise)
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
        std::memset(&out, 0, sizeof out);
        return StErr::OK;
    }
    if (metric.model == SpacetimeModel::SCHWARZSCHILD) {
        double x[4];
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
        const double r = x[1], theta = x[2];
        if (r <= metric.rs_m) return StErr::DEGENERATE_METRIC;  // _f guard
        const double f = 1.0 - metric.rs_m / r;
        std::memset(&out, 0, sizeof out);
        const double rs = metric.rs_m;
        const double d00 = -rs / (r * r);
        const double d11 = -rs / (r * r * f * f);
        const double d22_r = 2.0 * r;
        const double sin_t = std::sin(theta), cos_t = std::cos(theta);
        const double d33_r = 2.0 * r * sin_t * sin_t;
        const double d33_t = 2.0 * r * r * sin_t * cos_t;
        out.m[1][0][0] = d00;      // [dr][0][0]
        out.m[1][1][1] = d11;      // [dr][1][1]
        out.m[1][2][2] = d22_r;    // [dr][2][2]
        out.m[1][3][3] = d33_r;    // [dr][3][3]
        out.m[2][3][3] = d33_t;    // [dtheta][3][3]
        return StErr::OK;
    }
    // KERR / GENERAL_NUMERICAL: base-class numeric path.
    return st_metric_derivative_numeric(metric, coords4, out);
}

StErr st_metric_determinant(const StMetric& metric, const double coords4[4], double& out) {
    Tensor4 t;
    const StErr e = st_metric_tensor(metric, coords4, t);
    if (e != StErr::OK) return e;
    out = matrix4_determinant(t);
    return StErr::OK;
}

StErr st_metric_inverse(const StMetric& metric, const double coords4[4], Tensor4& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    Tensor4 t;
    const StErr e = tensor_unvalidated(metric, x, t);
    if (e != StErr::OK) return e;
    // Scale-invariant strategy of metric.MetricField.inverse.
    double max_abs = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::fabs(t.m[i][j]) > max_abs) max_abs = std::fabs(t.m[i][j]);
    if (max_abs == 0.0) return StErr::DEGENERATE_METRIC;  // zero metric
    double scale[4];
    for (int i = 0; i < 4; ++i) {
        const double s = std::fabs(t.m[i][i]);
        scale[i] = s > 0.0 ? std::sqrt(s) : std::sqrt(max_abs);
    }
    Tensor4 gtilde;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            gtilde.m[i][j] = t.m[i][j] / (scale[i] * scale[j]);
    const double det_t = matrix4_determinant(gtilde);
    if (!std::isfinite(det_t) || std::fabs(det_t) <= ST_DET_REL_TOL)
        return StErr::DEGENERATE_METRIC;
    Tensor4 inv_tilde;
    if (!matrix4_inverse(gtilde, inv_tilde)) return StErr::DEGENERATE_METRIC;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out.m[i][j] = inv_tilde.m[i][j] / (scale[i] * scale[j]);
    return StErr::OK;
}

bool st_metric_is_finite_at(const StMetric& metric, const double coords4[4]) {
    Tensor4 t;
    const StErr e = st_metric_tensor(metric, coords4, t);
    if (e != StErr::OK) return false;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!std::isfinite(t.m[i][j])) return false;
    return true;
}

// ---- connection.py ------------------------------------------------------------
StErr st_christoffel_symbols(const StMetric& metric, const double coords4[4], GammaTensor& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    Tensor4 g_up;
    {
        const StErr e = st_metric_inverse(metric, x, g_up);  // raises on singular metric
        if (e != StErr::OK) return e;
    }
    DerivTensor dg;
    {
        const StErr e = st_metric_derivative(metric, x, dg);
        if (e != StErr::OK) return e;
    }
    for (int rho = 0; rho < 4; ++rho)
        for (int mu = 0; mu < 4; ++mu)
            for (int nu = mu; nu < 4; ++nu) {
                double acc = 0.0;
                for (int sigma = 0; sigma < 4; ++sigma)
                    acc += g_up.m[rho][sigma] *
                        (dg.m[mu][sigma][nu] + dg.m[nu][sigma][mu] - dg.m[sigma][mu][nu]);
                acc *= 0.5;
                out.m[rho][mu][nu] = acc;
                out.m[rho][nu][mu] = acc;
            }
    return StErr::OK;
}

StErr st_christoffel_derivative(const StMetric& metric, const double coords4[4], GammaDerivTensor& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    for (int a = 0; a < 4; ++a) {
        const double h = ST_DIFF_STEP * std::fmax(std::fabs(x[a]), 1.0);
        GammaTensor gp, gm, g2p, g2m;
        auto gamma_at = [&](double offset, GammaTensor& g) -> StErr {
            double xp[4] = {x[0], x[1], x[2], x[3]};
            xp[a] += offset;
            return st_christoffel_symbols(metric, xp, g);
        };
        StErr e;
        if ((e = gamma_at(h, gp)) != StErr::OK) return e;
        if ((e = gamma_at(-h, gm)) != StErr::OK) return e;
        if ((e = gamma_at(2.0 * h, g2p)) != StErr::OK) return e;
        if ((e = gamma_at(-2.0 * h, g2m)) != StErr::OK) return e;
        for (int rho = 0; rho < 4; ++rho)
            for (int mu = 0; mu < 4; ++mu)
                for (int nu = 0; nu < 4; ++nu)
                    out.m[a][rho][mu][nu] =
                        (-g2p.m[rho][mu][nu] + 8.0 * gp.m[rho][mu][nu]
                         - 8.0 * gm.m[rho][mu][nu] + g2m.m[rho][mu][nu]) / (12.0 * h);
    }
    return StErr::OK;
}

// ---- causality.py ---------------------------------------------------------------
StErr st_local_interval(const StMetric& metric, const double coords_a[4],
                        const double coords_b[4], double& out_ds2) {
    double xa[4], xb[4];
    {
        StErr e = st_validate_coords(coords_a, metric.chart, xa);
        if (e != StErr::OK) return e;
        e = st_validate_coords(coords_b, metric.chart, xb);
        if (e != StErr::OK) return e;
    }
    double mid[4], dx[4];
    for (int i = 0; i < 4; ++i) {
        mid[i] = 0.5 * (xa[i] + xb[i]);
        dx[i] = xb[i] - xa[i];
    }
    Tensor4 g;
    {
        // Authority evaluates the raw tensor at the midpoint (no extra
        // validation beyond tensor() itself).
        const StErr e = tensor_unvalidated(metric, mid, g);
        if (e != StErr::OK) return e;
    }
    double total = 0.0;
    for (int mu = 0; mu < 4; ++mu)
        for (int nu = 0; nu < 4; ++nu)
            total += g.m[mu][nu] * dx[mu] * dx[nu];
    out_ds2 = total;
    return StErr::OK;
}

StErr st_classify_interval(const StMetric& metric, const double coords_a[4],
                           const double coords_b[4], IntervalType& out, double tolerance) {
    double ds2;
    const StErr e = st_local_interval(metric, coords_a, coords_b, ds2);
    if (e != StErr::OK) return e;
    if (ds2 < -tolerance) { out = IntervalType::TIMELIKE; return StErr::OK; }
    if (ds2 > tolerance) { out = IntervalType::SPACELIKE; return StErr::OK; }
    out = IntervalType::NULLI;
    return StErr::OK;
}

StErr st_null_ray_directions(const StMetric& metric, const double coords4[4],
                             const double spatial_dir[3],
                             double out_future4[4], double out_past4[4]) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    double direction[3];
    for (int i = 0; i < 3; ++i) direction[i] = spatial_dir[i];

    // Authority: nonzero check occurs BEFORE the tensor evaluation.
    bool all_zero = true;
    for (int i = 0; i < 3; ++i) if (direction[i] != 0.0) { all_zero = false; break; }
    if (all_zero) return StErr::INVALID_COORDINATE;

    Tensor4 g;
    {
        const StErr e = tensor_unvalidated(metric, x, g);
        if (e != StErr::OK) return e;
    }
    for (int mu = 0; mu < 4; ++mu)
        for (int nu = 0; nu < 4; ++nu)
            if (mu != nu && g.m[mu][nu] != 0.0) return StErr::NOT_IMPLEMENTED;

    double spatial_sq = 0.0;
    for (int i = 0; i < 3; ++i) spatial_sq += g.m[i + 1][i + 1] * (direction[i] * direction[i]);
    if (spatial_sq < 0.0 || g.m[0][0] >= 0.0) return StErr::DEGENERATE_METRIC;
    const double u0 = std::sqrt(-spatial_sq / g.m[0][0]);
    out_future4[0] = u0;  out_future4[1] = direction[0];
    out_future4[2] = direction[1]; out_future4[3] = direction[2];
    out_past4[0] = -u0;   out_past4[1] = direction[0];
    out_past4[2] = direction[1];  out_past4[3] = direction[2];
    return StErr::OK;
}

// ---- curvature.py -----------------------------------------------------------------
StErr st_riemann_tensor(const StMetric& metric, const double coords4[4], RiemannTensor& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    GammaTensor gamma;
    {
        const StErr e = st_christoffel_symbols(metric, x, gamma);
        if (e != StErr::OK) return e;
    }
    GammaDerivTensor dgamma;
    {
        const StErr e = st_christoffel_derivative(metric, x, dgamma);
        if (e != StErr::OK) return e;
    }
    for (int rho = 0; rho < 4; ++rho)
        for (int sigma = 0; sigma < 4; ++sigma)
            for (int mu = 0; mu < 4; ++mu)
                for (int nu = 0; nu < 4; ++nu) {
                    double acc = dgamma.m[mu][rho][nu][sigma] - dgamma.m[nu][rho][mu][sigma];
                    for (int lam = 0; lam < 4; ++lam)
                        acc += (gamma.m[rho][mu][lam] * gamma.m[lam][nu][sigma]
                                - gamma.m[rho][nu][lam] * gamma.m[lam][mu][sigma]);
                    out.m[rho][sigma][mu][nu] = acc;
                }
    return StErr::OK;
}

StErr st_riemann_all_lower(const StMetric& metric, const double coords4[4], RiemannTensor& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    RiemannTensor r_up;
    {
        const StErr e = st_riemann_tensor(metric, x, r_up);
        if (e != StErr::OK) return e;
    }
    Tensor4 g_dn;
    {
        const StErr e = tensor_unvalidated(metric, x, g_dn);
        if (e != StErr::OK) return e;
    }
    for (int rho = 0; rho < 4; ++rho)
        for (int sigma = 0; sigma < 4; ++sigma)
            for (int mu = 0; mu < 4; ++mu)
                for (int nu = 0; nu < 4; ++nu) {
                    double acc = 0.0;
                    for (int lam = 0; lam < 4; ++lam)
                        acc += g_dn.m[rho][lam] * r_up.m[lam][sigma][mu][nu];
                    out.m[rho][sigma][mu][nu] = acc;
                }
    return StErr::OK;
}

StErr st_ricci_tensor(const StMetric& metric, const double coords4[4], Tensor4& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    RiemannTensor r;
    {
        const StErr e = st_riemann_tensor(metric, x, r);
        if (e != StErr::OK) return e;
    }
    for (int mu = 0; mu < 4; ++mu)
        for (int nu = 0; nu < 4; ++nu) {
            double acc = 0.0;  // sum() from 0 — same left-to-right chain
            for (int lam = 0; lam < 4; ++lam)
                acc += r.m[lam][mu][lam][nu];
            out.m[mu][nu] = acc;
        }
    return StErr::OK;
}

StErr st_ricci_scalar(const StMetric& metric, const double coords4[4], double& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    Tensor4 r_down;
    {
        const StErr e = st_ricci_tensor(metric, x, r_down);
        if (e != StErr::OK) return e;
    }
    Tensor4 g_up;
    {
        const StErr e = st_metric_inverse(metric, x, g_up);
        if (e != StErr::OK) return e;
    }
    double acc = 0.0;
    for (int mu = 0; mu < 4; ++mu)
        for (int nu = 0; nu < 4; ++nu)
            acc += g_up.m[mu][nu] * r_down.m[mu][nu];
    out = acc;
    return StErr::OK;
}

StErr st_einstein_tensor(const StMetric& metric, const double coords4[4], Tensor4& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    Tensor4 r_down;
    {
        const StErr e = st_ricci_tensor(metric, x, r_down);
        if (e != StErr::OK) return e;
    }
    double big_r;
    {
        const StErr e = st_ricci_scalar(metric, x, big_r);
        if (e != StErr::OK) return e;
    }
    Tensor4 g_dn;
    {
        const StErr e = tensor_unvalidated(metric, x, g_dn);
        if (e != StErr::OK) return e;
    }
    for (int mu = 0; mu < 4; ++mu)
        for (int nu = 0; nu < 4; ++nu)
            out.m[mu][nu] = r_down.m[mu][nu] - 0.5 * big_r * g_dn.m[mu][nu];
    return StErr::OK;
}

StErr st_kretschmann_scalar(const StMetric& metric, const double coords4[4], double& out) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    RiemannTensor r_dddd;
    {
        const StErr e = st_riemann_all_lower(metric, x, r_dddd);
        if (e != StErr::OK) return e;
    }
    Tensor4 g_up;
    {
        const StErr e = st_metric_inverse(metric, x, g_up);
        if (e != StErr::OK) return e;
    }
    // Indices are raised SEQUENTIALLY, one slot at a time (slot 0,1,2,3).
    RiemannTensor t = r_dddd;
    for (int slot = 0; slot < 4; ++slot) {
        RiemannTensor next;
        for (int i0 = 0; i0 < 4; ++i0)
            for (int i1 = 0; i1 < 4; ++i1)
                for (int i2 = 0; i2 < 4; ++i2)
                    for (int i3 = 0; i3 < 4; ++i3) {
                        const int idx[4] = {i0, i1, i2, i3};
                        double acc = 0.0;
                        for (int lam = 0; lam < 4; ++lam) {
                            int idx2[4] = {i0, i1, i2, i3};
                            idx2[slot] = lam;
                            acc += g_up.m[idx[slot]][lam] *
                                   t.m[idx2[0]][idx2[1]][idx2[2]][idx2[3]];
                        }
                        next.m[i0][i1][i2][i3] = acc;
                    }
        t = next;
    }
    double acc = 0.0;
    for (int i0 = 0; i0 < 4; ++i0)
        for (int i1 = 0; i1 < 4; ++i1)
            for (int i2 = 0; i2 < 4; ++i2)
                for (int i3 = 0; i3 < 4; ++i3)
                    acc += r_dddd.m[i0][i1][i2][i3] * t.m[i0][i1][i2][i3];
    out = acc;
    return StErr::OK;
}

StErr st_tidal_acceleration(const StMetric& metric, const double coords4[4],
                            const double four_velocity[4], const double separation[4],
                            double out_accel4[4]) {
    double x[4];
    {
        const StErr e = st_validate_coords(coords4, metric.chart, x);
        if (e != StErr::OK) return e;
    }
    RiemannTensor r_up;
    {
        const StErr e = st_riemann_tensor(metric, x, r_up);
        if (e != StErr::OK) return e;
    }
    for (int mu = 0; mu < 4; ++mu) {
        double acc = 0.0;
        for (int nu = 0; nu < 4; ++nu)
            for (int rho = 0; rho < 4; ++rho)
                for (int sigma = 0; sigma < 4; ++sigma)
                    acc += ((r_up.m[mu][nu][rho][sigma] * four_velocity[nu])
                            * separation[rho]) * four_velocity[sigma];
        out_accel4[mu] = -acc;
    }
    return StErr::OK;
}

// ---- geodesics.py -------------------------------------------------------------------
namespace {

// Horizon guard for spherical charts (0.0 for cartesian).
double st_horizon_limit(const StMetric& metric) {
    if (metric.chart != StChart::SPHERICAL) return 0.0;
    if (metric.model == SpacetimeModel::SCHWARZSCHILD) return metric.rs_m;
    if (metric.model == SpacetimeModel::KERR) return metric.r_plus_m;
    return 0.0;
}

StErr st_check_state(const double y[8], double horizon) {
    for (int i = 0; i < 8; ++i)
        if (std::isnan(y[i]) || std::isinf(y[i])) return StErr::GEODESIC_DIVERGENCE;
    if (horizon > 0.0 && y[1] <= horizon * (1.0 + 1.0e-12)) return StErr::HORIZON_CROSSING;
    return StErr::OK;
}

// Deterministic geodesic RHS: du/dλ = -Gamma^mu_{alpha beta} u^a u^b.
// The authority skips zero components explicitly (ua == 0.0 -> continue).
StErr st_geodesic_rhs(const StMetric& metric, const double y[8], double out_deriv[8]) {
    GammaTensor gamma;
    const StErr e = st_christoffel_symbols(metric, y /* = x part first 4 */, gamma);
    if (e != StErr::OK) return e;
    const double* u = y + 4;
    double acc[4];
    for (int mu = 0; mu < 4; ++mu) {
        double s = 0.0;
        for (int alpha = 0; alpha < 4; ++alpha) {
            const double ua = u[alpha];
            if (ua == 0.0) continue;
            for (int beta = 0; beta < 4; ++beta) {
                const double ub = u[beta];
                if (ub == 0.0) continue;
                s += ((gamma.m[mu][alpha][beta] * ua) * ub);
            }
        }
        acc[mu] = -s;
    }
    for (int i = 0; i < 4; ++i) out_deriv[i] = u[i];
    for (int i = 0; i < 4; ++i) out_deriv[4 + i] = acc[i];
    return StErr::OK;
}

} // namespace

StErr st_integrate_geodesic(const StMetric& metric,
                            const double initial_coords4[4],
                            const double initial_four_velocity4[4],
                            double parameter_limit, int steps, bool adaptive,
                            double rtol, GeodesicSolution& out) {
    // Facade argument validation (authority: InvalidCoordinateError).
    if (std::isnan(parameter_limit) || std::isinf(parameter_limit) || parameter_limit <= 0.0)
        return StErr::INVALID_COORDINATE;
    if (steps < 1 || steps > 200000)
        return StErr::INVALID_COORDINATE;

    double x0[4];
    {
        const StErr e = st_validate_coords(initial_coords4, metric.chart, x0);
        if (e != StErr::OK) return e;
    }
    double u0[4];
    for (int i = 0; i < 4; ++i) {
        u0[i] = initial_four_velocity4[i];
        if (std::isnan(u0[i]) || std::isinf(u0[i])) return StErr::INVALID_COORDINATE;
    }

    const double horizon = st_horizon_limit(metric);

    double state[8];
    for (int i = 0; i < 4; ++i) { state[i] = x0[i]; state[4 + i] = u0[i]; }
    {
        const StErr e = st_check_state(state, horizon);
        if (e != StErr::OK) return e;
    }

    const double h0 = parameter_limit / static_cast<double>(steps);

    out.parameters.clear();
    out.coordinates.clear();
    out.four_velocities.clear();
    out.terminated = "completed";
    out.parameters.push_back(0.0);
    out.coordinates.push_back({x0[0], x0[1], x0[2], x0[3]});
    out.four_velocities.push_back({u0[0], u0[1], u0[2], u0[3]});

    double lam = 0.0;
    double h_current = h0;
    int accepted = 0;
    const double max_inner = 2.0e6;
    while (lam < parameter_limit - 1e-15 * parameter_limit) {
        if (accepted >= max_inner) return StErr::GEODESIC_DIVERGENCE;
        const double step = std::fmin(h_current, parameter_limit - lam);
        if (adaptive) {
            // Mirror of astra.mathematics.ode.rk45_step (DOPRI5(4)).
            static const double C7[7] = {0.0, 1.0 / 5.0, 3.0 / 10.0, 4.0 / 5.0, 8.0 / 9.0, 1.0, 1.0};
            static const double A[7][6] = {
                {0, 0, 0, 0, 0, 0},
                {1.0 / 5.0, 0, 0, 0, 0, 0},
                {3.0 / 40.0, 9.0 / 40.0, 0, 0, 0, 0},
                {44.0 / 45.0, -56.0 / 15.0, 32.0 / 9.0, 0, 0, 0},
                {19372.0 / 6561.0, -25360.0 / 2187.0, 64448.0 / 6561.0, -212.0 / 729.0, 0, 0},
                {9017.0 / 3168.0, -355.0 / 33.0, 46732.0 / 5247.0, 49.0 / 176.0, -5103.0 / 18656.0, 0},
                {35.0 / 384.0, 0.0, 500.0 / 1113.0, 125.0 / 192.0, -2187.0 / 6784.0, 11.0 / 84.0},
            };
            static const double B5[7] = {35.0 / 384.0, 0.0, 500.0 / 1113.0, 125.0 / 192.0,
                                         -2187.0 / 6784.0, 11.0 / 84.0, 0.0};
            static const double B4[7] = {5179.0 / 57600.0, 0.0, 7571.0 / 16695.0, 393.0 / 640.0,
                                         -92097.0 / 339200.0, 187.0 / 2100.0, 1.0 / 40.0};
            double k[7][8];
            for (int i = 0; i < 7; ++i) {
                double yi[8];
                if (i == 0) {
                    std::memcpy(yi, state, sizeof yi);
                } else {
                    // _combine((1.0, y), (h*A[i][j], k[j]) ...) sequential adds.
                    for (int n = 0; n < 8; ++n) {
                        double acc = 1.0 * state[n];
                        for (int j = 0; j < i; ++j) acc += (step * A[i][j]) * k[j][n];
                        yi[n] = acc;
                    }
                }
                // rhs evaluated at t + C7[i]*h; geodesic rhs ignores t.
                const StErr e = st_geodesic_rhs(metric, yi, k[i]);
                if (e != StErr::OK) return e;
            }
            double y5[8], y4[8];
            for (int n = 0; n < 8; ++n) {
                double acc5 = 1.0 * state[n];
                for (int j = 0; j < 7; ++j) acc5 += (step * B5[j]) * k[j][n];
                y5[n] = acc5;
                double acc4 = 1.0 * state[n];
                for (int j = 0; j < 7; ++j) acc4 += (step * B4[j]) * k[j][n];
                y4[n] = acc4;
            }
            double err = 0.0;
            for (int n = 0; n < 8; ++n) {
                const double d = std::fabs(y5[n] - y4[n]);
                if (d > err) err = d;
            }
            double scale = 1.0e-30;
            for (int n = 0; n < 8; ++n)
                if (std::fabs(y5[n]) > scale) scale = std::fabs(y5[n]);
            const double e2 = err / (rtol * scale + 1e-30);
            if (e2 <= 1.0) {
                const StErr e = st_check_state(y5, horizon);
                if (e != StErr::OK) return e;
                std::memcpy(state, y5, sizeof state);
                lam += step;
                ++accepted;
                if (e2 > 0.0) {
                    double f = 0.9 * std::pow(e2, -0.2);
                    if (f < 0.2) f = 0.2; else if (f > 5.0) f = 5.0;
                    h_current = step * f;
                }
            } else {
                double f = 0.9 * std::pow(e2, -0.25);
                if (f < 0.1) f = 0.1;
                h_current = step * f;
                if (h_current < 1e-15 * std::fmax(1.0, std::fabs(parameter_limit)))
                    return StErr::GEODESIC_DIVERGENCE;
                continue;
            }
        } else {
            // Classic RK4, INLINED with per-stage guards exactly as the
            // authority (note: does NOT call rk4_step; the stages are checked).
            double k1[8];
            {
                const StErr e = st_geodesic_rhs(metric, state, k1);
                if (e != StErr::OK) return e;
            }
            double mid1[8];
            for (int i = 0; i < 8; ++i) mid1[i] = state[i] + 0.5 * step * k1[i];
            {
                const StErr e = st_check_state(mid1, horizon);
                if (e != StErr::OK) return e;
            }
            double k2[8];
            {
                const StErr e = st_geodesic_rhs(metric, mid1, k2);
                if (e != StErr::OK) return e;
            }
            double mid2[8];
            for (int i = 0; i < 8; ++i) mid2[i] = state[i] + 0.5 * step * k2[i];
            {
                const StErr e = st_check_state(mid2, horizon);
                if (e != StErr::OK) return e;
            }
            double k3[8];
            {
                const StErr e = st_geodesic_rhs(metric, mid2, k3);
                if (e != StErr::OK) return e;
            }
            double mid3[8];
            for (int i = 0; i < 8; ++i) mid3[i] = state[i] + step * k3[i];
            {
                const StErr e = st_check_state(mid3, horizon);
                if (e != StErr::OK) return e;
            }
            double k4[8];
            {
                const StErr e = st_geodesic_rhs(metric, mid3, k4);
                if (e != StErr::OK) return e;
            }
            for (int i = 0; i < 8; ++i)
                state[i] = state[i] + step / 6.0 * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
            {
                const StErr e = st_check_state(state, horizon);
                if (e != StErr::OK) return e;
            }
            lam += step;
            ++accepted;
        }
        out.parameters.push_back(lam);
        out.coordinates.push_back({state[0], state[1], state[2], state[3]});
        out.four_velocities.push_back({state[4], state[5], state[6], state[7]});
    }
    return StErr::OK;
}

// ---- api.py ----------------------------------------------------------------------
StErr st_event_to_chart(const StMetric& metric, double event_ct_m, double event_x,
                        double event_y, double event_z, double out_coords4[4]) {
    if (metric.chart == StChart::CARTESIAN) {
        out_coords4[0] = event_ct_m; out_coords4[1] = event_x;
        out_coords4[2] = event_y; out_coords4[3] = event_z;
        return StErr::OK;
    }
    double r, theta, phi;
    const StErr e = st_cartesian_to_spherical(event_x, event_y, event_z, r, theta, phi);
    if (e != StErr::OK) return e;
    out_coords4[0] = event_ct_m;
    out_coords4[1] = r;
    out_coords4[2] = theta;
    out_coords4[3] = phi;
    return StErr::OK;
}

StErr st_cartesian_state_to_chart(const StMetric& metric,
                                  double event_ct_m, double event_x, double event_y, double event_z,
                                  const RelVec3& velocity3, bool massless,
                                  double out_coords4[4], double out_u4[4]) {
    const double C = SPEED_OF_LIGHT;
    // Vector3.magnitude semantics: math.hypot(math.hypot(x, y), z).
    const double v_mag = std::hypot(std::hypot(velocity3.x, velocity3.y), velocity3.z);
    // Authority does NOT reject non-finite v_mag here — IEEE evaluation flows
    // identically to Python; the only explicit guard is the massless-zero case.
    if (v_mag == 0.0 && massless) return StErr::INVALID_COORDINATE;  // null needs nonzero v
    const double x0 = event_ct_m;
    const double vx = velocity3.x, vy = velocity3.y, vz = velocity3.z;

    if (metric.chart == StChart::CARTESIAN) {
        if (massless) {
            const double s = 1.0 / v_mag;  // dλ = dct (λ in m)
            out_coords4[0] = x0; out_coords4[1] = event_x;
            out_coords4[2] = event_y; out_coords4[3] = event_z;
            out_u4[0] = 1.0;
            out_u4[1] = vx * s / 1.0;   // authority literal: velocity3.x * s / 1.0
            out_u4[2] = vy * s;
            out_u4[3] = vz * s;
            return StErr::OK;
        }
        const double w[4] = {C, vx, vy, vz};
        const double xc[4] = {x0, event_x, event_y, event_z};
        Tensor4 g;
        const StErr e = tensor_unvalidated(metric, xc, g);
        if (e != StErr::OK) return e;
        double ww = 0.0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                ww += (g.m[a][b] * w[a]) * w[b];
        if (ww >= 0.0) return StErr::LIGHT_SPEED_VIOLATION;
        const double dt_dtau = C / std::sqrt(-ww);
        out_coords4[0] = x0; out_coords4[1] = event_x;
        out_coords4[2] = event_y; out_coords4[3] = event_z;
        out_u4[0] = C * dt_dtau;
        out_u4[1] = dt_dtau * vx;
        out_u4[2] = dt_dtau * vy;
        out_u4[3] = dt_dtau * vz;
        return StErr::OK;
    }

    // Spherical/BL chart: position conversion + coordinate-velocity Jacobian.
    double r, theta, phi;
    {
        const StErr e = st_cartesian_to_spherical(event_x, event_y, event_z, r, theta, phi);
        if (e != StErr::OK) return e;
    }
    const double x = event_x, y = event_y, z = event_z;
    const double rho = std::hypot(x, y);
    const double w_r = (x * vx + y * vy + z * vz) / r;
    const double w_th = ((x * vx + y * vy) * z / rho - z * vz * rho) / (r * r);
    const double w_ph = (x * vy - y * vx) / (rho * rho);

    const double xc[4] = {x0, r, theta, phi};
    Tensor4 g;
    const StErr eg = tensor_unvalidated(metric, xc, g);
    if (eg != StErr::OK) return eg;

    out_coords4[0] = x0; out_coords4[1] = r; out_coords4[2] = theta; out_coords4[3] = phi;
    if (massless) {
        out_u4[0] = 1.0;
        out_u4[1] = w_r / C;
        out_u4[2] = w_th / C;
        out_u4[3] = w_ph / C;
        return StErr::OK;
    }
    const double w[4] = {C, w_r, w_th, w_ph};
    double ww = 0.0;
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b)
            ww += (g.m[a][b] * w[a]) * w[b];
    if (ww >= 0.0) return StErr::LIGHT_SPEED_VIOLATION;
    const double dt_dtau = C / std::sqrt(-ww);
    out_u4[0] = C * dt_dtau;
    out_u4[1] = dt_dtau * w_r;
    out_u4[2] = dt_dtau * w_th;
    out_u4[3] = dt_dtau * w_ph;
    return StErr::OK;
}

} // namespace astra::app
