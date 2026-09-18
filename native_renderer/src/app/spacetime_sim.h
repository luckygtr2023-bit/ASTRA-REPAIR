#pragma once
// ASTRA COSMOS — Spacetime layer (native mirror of the Python scientific
// authority `astra.spacetime`).
//
// ARCHITECTURE RULE: `astra.spacetime` is the scientific authority. This file
// mirrors ONLY what exists there — nothing is invented. Conventions: x^mu =
// (ct, spatial) in metres, signature (-,+,+,+), Levi-Civita connection,
// Riemann sign per authority ("positive curvature focuses geodesics"),
// fixed-background / test-particle approximation (NO Einstein Field Equation
// solving, NO Kruskal-Szekeres interior — deferred upstream, so NOT here).
//
//   events.py:      SpacetimeEvent (ct_m, x, y, z; chart), from_coordinates
//                   (ct = t_sec * c), cartesian<->spherical chart conversions
//                   (origin r = 0 rejected; theta = acos(clamp(z/r)),
//                   phi = atan2; r < 0 rejected).
//   metric.py:      MetricField protocol; MinkowskiMetric (cartesian),
//                   SchwarzschildMetric (spherical, ANALYTIC derivatives,
//                   r <== r_s rejected), KerrMetric (Boyer-Lindquist, NUMERIC
//                   4th-order central-difference derivatives, r <= r_+
//                   rejected), NumericalMetric (user field, numeric
//                   derivatives). Scale-normalized inverse (D = diag(sqrt
//                   |g_ii|), Matrix4 cofactor inverse of astra.mathematics,
//                   DET_REL_TOL = 1e-14, SINGULAR_TOL = 1e-12), DIFF_STEP 1e-5
//                   relative steps. Spherical chart patch guards: r <= 0 and
//                   theta <= 0 / theta >= pi are DegenerateMetricError.
//   connection.py:  Christoffel Gamma^rho_{mu nu} (symmetric lower pair) and
//                   its 4th-order central-difference derivative field.
//   causality.py:   local_interval ds² ~ g(x_mid) dx dx (exact for Minkowski,
//                   FIRST-ORDER linearized elsewhere — authority's honest
//                   approximation), classify_interval (tol 1e-9), diagonal-
//                   metric null_ray_directions (off-diagonal g -> the
//                   authority's NotImplementedError, mirrored, NOT extended).
//   curvature.py:   Riemann, all-lower Riemann, Ricci tensor/scalar,
//                   Einstein tensor, Kretschmann scalar (indices raised
//                   SEQUENTIALLY slot 0..3 exactly as the authority),
//                   tidal (geodesic deviation) operator.
//   geodesics.py:   d²x/dλ² = -Gamma u u with per-stage horizon guard
//                   (r <= horizon*(1+1e-12) -> HORIZON_CROSSING) and NaN/Inf
//                   -> GEODESIC_DIVERGENCE; fixed-step inline RK4 (stage
//                   checks) or DOPRI5(4) rk45 from astra.mathematics.ode
//                   (same DP coefficient fractions, same step controller).
//   api.py:         facade conversions cartesian_state_to_chart (metric-
//                   correct u^0 from g(w,w) = -c²; LightSpeedViolation at the
//                   local light-speed boundary; spherical Jacobian for the
//                   coordinate velocity; v_mag via Vector3.magnitude semantics
//                   = hypot(hypot(x,y),z)), event_to_chart.
//
// ERROR MODEL: Python raises typed exceptions; the mirror returns an explicit
// StErr enum (identical domain boundaries, no silent clamps):
//   INVALID_COORDINATE      <- InvalidCoordinateError
//   DEGENERATE_METRIC       <- DegenerateMetricError
//   HORIZON_CROSSING        <- HorizonCrossingError
//   GEODESIC_DIVERGENCE     <- GeodesicDivergenceError
//   LIGHT_SPEED_VIOLATION   <- relativity.exceptions.LightSpeedViolation
//   INVALID_MASS/INVALID_SPIN <- black-hole parameter errors (metric ctors)
//   NOT_IMPLEMENTED         <- authority's NotImplementedError (diagonal-only
//                              null directions) — preserved, never worked around.
//
// CLASSIFICATION: PHYSICALLY-MODELED (curved fixed-background geometry,
// test-particle geodesics). The linearized local interval is documented as
// FIRST-ORDER outside flat spacetime; NO claims beyond the authority's scope.
// Determinism: pure functions, identical op order, no RNG/wall-clock.

#include "relativity_sim.h"  // SPEED_OF_LIGHT, IntervalType (interval taxonomy)

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace astra::app {

enum class StErr {
    OK,
    INVALID_COORDINATE,
    DEGENERATE_METRIC,
    HORIZON_CROSSING,
    GEODESIC_DIVERGENCE,
    LIGHT_SPEED_VIOLATION,
    INVALID_MASS,
    INVALID_SPIN,
    NOT_IMPLEMENTED,
};
const char* st_err_name(StErr e);

enum class StChart { CARTESIAN, SPHERICAL };
const char* st_chart_name(StChart c);  // "cartesian" | "spherical"

enum class SpacetimeModel { MINKOWSKI, SCHWARZSCHILD, KERR, GENERAL_NUMERICAL };
const char* spacetime_model_name(SpacetimeModel m);

// ---- Constants (mirror of metric.py / mathematics matrices) -------------------
inline constexpr double ST_DIFF_STEP = 1.0e-5;    // relative numeric-diff step
inline constexpr double ST_DET_REL_TOL = 1.0e-14; // normalized-det guard
inline constexpr double ST_SINGULAR_TOL = 1e-12;  // Matrix4 cofactor-inverse guard
inline constexpr double ST_PI = 3.141592653589793;

// ---- Tensor carriers (immutable nested tuples in the authority) ----------------
struct Tensor4 { double m[4][4]; };
struct DerivTensor { double m[4][4][4]; };   // [a][mu][nu] = d_a g_{mu nu}
struct GammaTensor { double m[4][4][4]; };   // [rho][mu][nu]
struct GammaDerivTensor { double m[4][4][4][4]; }; // [gamma][rho][mu][nu]
struct RiemannTensor { double m[4][4][4][4]; }; // [rho][sigma][mu][nu]

// ---- Events & charts (mirror of events.py) --------------------------------------
StErr st_event_from_coordinates(double time_sec, double x, double y, double z,
                                StChart chart, double out4[4]);  // (ct, x, y, z)
StErr st_cartesian_to_spherical(double x, double y, double z,
                                double& out_r, double& out_theta, double& out_phi);
StErr st_spherical_to_cartesian(double r, double theta, double phi,
                                double& out_x, double& out_y, double& out_z);

// ---- MetricField (mirror of metric.py) ------------------------------------------
struct StMetric {
    SpacetimeModel model = SpacetimeModel::MINKOWSKI;
    StChart chart = StChart::CARTESIAN;
    double rs_m = 0.0;                              // SCHWARZSCHILD
    double r_g_m = 0.0, a_m = 0.0, r_plus_m = 0.0;  // KERR
    // GENERAL_NUMERICAL: user field g_mn(x) (validated like the authority).
    std::function<void(const double* x4, Tensor4& out)> field;
};

StMetric st_minkowski_metric();
StErr st_schwarzschild_metric(double mass_kg, StMetric& out);
StErr st_kerr_metric(double mass_kg, double spin_param, StMetric& out);
StMetric st_numerical_metric(std::function<void(const double* x4, Tensor4& out)> field,
                             StChart chart);

// Core MetricField operations (tensor / derivative / determinant / inverse).
StErr st_metric_tensor(const StMetric& metric, const double coords4[4], Tensor4& out);
// Analytic for MINKOWSKI (zeros) and SCHWARZSCHILD (exact partials);
// 4th-order central differences for KERR / GENERAL_NUMERICAL (base-class path).
StErr st_metric_derivative(const StMetric& metric, const double coords4[4], DerivTensor& out);
StErr st_metric_determinant(const StMetric& metric, const double coords4[4], double& out);
StErr st_metric_inverse(const StMetric& metric, const double coords4[4], Tensor4& out);
// False when tensor evaluation raises OR any component is non-finite.
bool st_metric_is_finite_at(const StMetric& metric, const double coords4[4]);

// ---- Connection (mirror of connection.py) ----------------------------------------
StErr st_christoffel_symbols(const StMetric& metric, const double coords4[4], GammaTensor& out);
StErr st_christoffel_derivative(const StMetric& metric, const double coords4[4],
                                GammaDerivTensor& out); // [gamma][rho][mu][nu]

// ---- Causality (mirror of causality.py) -------------------------------------------
StErr st_local_interval(const StMetric& metric, const double coords_a[4],
                        const double coords_b[4], double& out_ds2);
StErr st_classify_interval(const StMetric& metric, const double coords_a[4],
                           const double coords_b[4], IntervalType& out,
                           double tolerance = 1.0e-9);
// Diagonal metrics only; off-diagonal => NOT_IMPLEMENTED (authority behaviour).
StErr st_null_ray_directions(const StMetric& metric, const double coords4[4],
                             const double spatial_dir[3],
                             double out_future4[4], double out_past4[4]);

// ---- Curvature (mirror of curvature.py) --------------------------------------------
StErr st_riemann_tensor(const StMetric& metric, const double coords4[4], RiemannTensor& out);
StErr st_riemann_all_lower(const StMetric& metric, const double coords4[4], RiemannTensor& out);
StErr st_ricci_tensor(const StMetric& metric, const double coords4[4], Tensor4& out);
StErr st_ricci_scalar(const StMetric& metric, const double coords4[4], double& out);
StErr st_einstein_tensor(const StMetric& metric, const double coords4[4], Tensor4& out);
StErr st_kretschmann_scalar(const StMetric& metric, const double coords4[4], double& out);
StErr st_tidal_acceleration(const StMetric& metric, const double coords4[4],
                            const double four_velocity[4], const double separation[4],
                            double out_accel4[4]);

// ---- Geodesics (mirror of geodesics.py) ---------------------------------------------
struct GeodesicSolution {
    std::vector<double> parameters;
    std::vector<std::array<double, 4>> coordinates;
    std::vector<std::array<double, 4>> four_velocities;
    std::string terminated;  // "completed" | "horizon"
};
StErr st_integrate_geodesic(const StMetric& metric,
                            const double initial_coords4[4],
                            const double initial_four_velocity4[4],
                            double parameter_limit, int steps, bool adaptive,
                            double rtol, GeodesicSolution& out);

// ---- Facade (mirror of api.py) ------------------------------------------------------
// Cartesian event + 3-velocity -> chart coords + chart 4-velocity.
StErr st_cartesian_state_to_chart(const StMetric& metric,
                                  double event_ct_m, double event_x, double event_y, double event_z,
                                  const RelVec3& velocity3, bool massless,
                                  double out_coords4[4], double out_u4[4]);
StErr st_event_to_chart(const StMetric& metric, double event_ct_m, double event_x,
                        double event_y, double event_z, double out_coords4[4]);

} // namespace astra::app
