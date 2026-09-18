#pragma once
// astro_catalog.h — v1.4 REAL astronomical catalog consumer (native).
//
//   SOURCE      = astra/catalog/pipeline.py emitted binaries
//                 (astro_stars.v14.bin / astro_dso.v14.bin; provenance chain
//                 in assets/astro_catalog_manifest.json). sha256-verified
//                 AGAINST THE BINARY BODY AT LOAD — a corrupt/tampered
//                 dataset fails CLOSED (catalog unavailable, NOT AVAILABLE
//                 rows; never falls back to invented stars).
//   FRAME       = file stores ICRS-aligned equatorial J2000 parsecs; the
//                 engine world frame is heliocentric ECLIPTIC J2000 → this
//                 module applies the IAU mean-obliquity rotation (ε =
//                 84381.448″, rotation about the shared vernal +X axis) at
//                 load. Documented transform; no silent frame mixing.
//   PRECISION   = positions stay double kilometers inside this module (1 pc =
//                 3.0856775814913673e13 km, IAU 2015 B2). Single precision
//                 ONLY inside the GPU instance structs after origin subtraction
//                 (rules from universe_viz.h / floating-origin contract).
//   MEASUREMENT = measure_star() mirrors astra.catalog.measure bit-for-bit in
//                 semantics (gates assert ≤ 1e-12 agreement through a shared
//                 reference record): angular position FROM THE OBSERVER
//                 (real stellar parallax when the observer moves), distance,
//                 light-travel delay, apparent magnitude — DATA_DERIVED labels.
//   NOT AVAILABLE = unknown distance (flags bit0: shell-only), missing mag /
//                 temperature / redshift stay NaN and render as NOT AVAILABLE.
//
// The loader NEVER fabricates: on any structural anomaly the catalog is
// marked unavailable and every HUD row shows NOT AVAILABLE.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace astra::catalog {

// ─── binary contract (mirrors astra.catalog.pipeline 1:1) ──────────────────
constexpr uint32_t CATALOG_FORMAT_VERSION = 1;
constexpr uint8_t STARS_MAGIC[8] = {'A', 'S', 'T', '1', '4', 'S', 'T', 'R'};
constexpr uint8_t DSO_MAGIC[8] = {'A', 'S', 'T', '1', '4', 'D', 'S', 'O'};

#pragma pack(push, 1)
struct StarRecord {           // STAR_RECORD_FORMAT "<dddffffffIIIQ" — 68 B
    double x_pc, y_pc, z_pc;  // parsecs, ICRS-aligned J2000 (unit dir if flags&1)
    float mag, absmag;        // NaN = NOT AVAILABLE
    float pmra_mas_yr, pmdec_mas_yr;
    float temp_k;             // PHYSICALLY_MODELED estimate; NaN = NOT AVAILABLE
    float ci;
    uint32_t spect_key;       // index into spectral table (0 = NONE)
    uint32_t hip_id;          // 0 = none
    uint32_t flags;           // bit0 distance_unknown, bit1 variable, bit2 named
    uint64_t hyg_id;          // authoritative catalog identity
};
struct DsoRecord {            // DSO_RECORD_FORMAT "<dddffffffII24s" — 80 B
    double ux, uy, uz;        // unit direction, ICRS-aligned J2000 (REAL RA/Dec)
    float redshift;           // measured z; NaN = not measured
    float v_mag;              // NaN = NOT AVAILABLE
    float maj_arcmin, min_arcmin;
    float dist_proxy_mpc;     // DATA_DERIVED Hubble proxy c·z/70; NaN when no z
    float radvel_km_s;        // NaN = NOT AVAILABLE
    uint32_t type_code;       // OpenNGC type enum
    uint32_t flags;           // bit0 redshift_present, bit1 named
    char name[24];
};
#pragma pack(pop)
static_assert(sizeof(StarRecord) == 68, "StarRecord must be 68 B (binary contract)");
static_assert(sizeof(DsoRecord) == 80, "DsoRecord must be 80 B (binary contract)");

constexpr uint32_t SFLAG_DIST_UNKNOWN = 1u << 0;
constexpr uint32_t SFLAG_VARIABLE = 1u << 1;
constexpr uint32_t SFLAG_NAMED = 1u << 2;
constexpr uint32_t DFLAG_REDSHIFT = 1u << 0;
constexpr uint32_t DFLAG_NAMED = 1u << 1;

// ─── frame/obliquity constants ──────────────────────────────────────────────
constexpr double PC_KM = 3.0856775814913673e13;       // IAU 2015 B2
constexpr double C_KM_S = 299792.458;                 // SI exact
constexpr double JULIAN_YEAR_S = 365.25 * 86400.0;
constexpr double LY_KM = C_KM_S * JULIAN_YEAR_S;
// IAU mean obliquity of the ecliptic at J2000.0: 84381.448″ (0.40909280… rad).
constexpr double OBLIQUITY_J2000_RAD = 84381.448 / 206264.80624709636;

// rotate ICRS-equatorial → heliocentric-ecliptic J2000 about the shared
// vernal +X axis (documented transform; x is invariant).
void icrs_to_ecliptic_j2000(double v[3]);
void unit_from_ecliptic_double(const double u_ecl[3], double out[3]);

// ─── catalogs ───────────────────────────────────────────────────────────────
struct StarCatalog {
    std::vector<StarRecord> records;         // ICRS as loaded (raw file body)
    std::vector<std::string> spect_table;    // 1-based (key 0 = NONE)
    std::vector<std::array<double, 3>> pos_km_ecl;  // ecliptic km positions (converted)
    uint64_t count = 0;
    bool available = false;
};

struct DsoCatalog {
    std::vector<DsoRecord> records;
    std::vector<std::array<double, 3>> dir_ecl;     // ecliptic unit direction
    uint64_t count = 0;
    bool available = false;
};

// Fail-closed loaders. dir probing copies the star-LUT convention.
bool load_star_catalog(const std::string& path, StarCatalog& out, std::string& why);
bool load_dso_catalog(const std::string& path, DsoCatalog& out, std::string& why);

// ─── GPU instance payload (the ONLY single-precision boundary) ──────────────
struct CatStarViz {              // 32 B — matches v14_cat_stars.vert SSBO layout
    float pos_units[3];          // shell-scaled scene units (CINEMATIC sphere)
    float size_px;               // magnitude-driven pixel size (CINEMATIC)
    float rgb[3];                // LUT color from PHYSICALLY_MODELED temp_k
    float flags;                 // StarRecord flags (float-encoded)
};
static_assert(sizeof(CatStarViz) == 32, "CatStarViz must be 32 B (SSBO contract)");

struct CatDsoViz {               // 32 B
    float pos_units[3];
    float half_size_units;       // REAL angular size → world half-size on shell
    float rgb[3];
    float flags;
};
static_assert(sizeof(CatDsoViz) == 32, "CatDsoViz must be 32 B");

struct VizBuildStats {
    uint64_t stars = 0, stars_shell_only = 0, stars_null_mag = 0;
    uint64_t dsos = 0, dsos_shell_z = 0;
};

// Every zenith star sits on a celestial sphere of `shell_units` radius —
// scene CINEMATIC sky projection of REAL directions (distances stay REAL in
// the measurement path; a sky sphere is the honest display at sub-stellar
// scene scales). Brightness: magnitude → pixel size (CINEMATIC mapping,
// documented).
// color_from_temp: PHYSICALLY_MODELED temp(K) → RGB from the project's star
// LUT (callback keeps this module free of app dependencies); NaN → callback
// must emit the neutral CINEMATIC white and the caller records it.
void build_star_viz(const StarCatalog& cat, const double origin_ecl_km[3],
                    double shell_units,
                    void (*color_from_temp)(double temp_k, float out_rgb[3], void* ctx),
                    void* ctx, std::vector<CatStarViz>& out);
void build_dso_viz(const DsoCatalog& cat, const double origin_ecl_km[3],
                   double shell_units, std::vector<CatDsoViz>& out);

// ─── native observatory measurement (mirrors astra.catalog.measure) ─────────
struct StarMeasurement {
    double dir_from_observer[3];     // unit, ecliptic
    double distance_km;              // NaN if records has SFLAG_DIST_UNKNOWN
    double distance_ly;
    double light_delay_years;
    double light_delay_s;
    double observer_ra_deg_ecl;      // measured longitude/latitude (ecliptic) —
    double observer_lat_deg_ecl;     // observer-motion parallax shows here
    double observer_ra_deg_icrs;     // and converted back to ICRS for HUD parity
    double observer_dec_deg_icrs;
    double apparent_mag;             // NaN if absmag NaN
    double dist_pc_catalog;          // NaN if unknown
};
StarMeasurement measure_star(const StarRecord& rec,
                             const std::array<double, 3>& pos_km_ecl,
                             const double observer_ecl_km[3]);

// 'historical' epoch view: apply proper motion for `years_since_j2000` years
// (small-angle μ_α* convention — mirror of astra.catalog.transform
// .apply_proper_motion_deg), THEN re-measure. pm values are the binary f32
// roundtrips of the catalog truth (documented precision boundary; the
// unshifted direction/distances remain the full-precision truth).
StarMeasurement measure_star_at_epoch(const StarRecord& rec,
                                      const std::array<double, 3>& pos_km_ecl,
                                      const double observer_ecl_km[3],
                                      double years_since_j2000);

// nearest-by-angle catalog selection (screen-center pointing); returns index
// or SIZE_MAX when catalog empty/observer degenerate.
size_t nearest_star_by_direction(const StarCatalog& cat, const double from_ecl_km[3],
                                 const double dir_ecl[3]);
size_t nearest_dso_by_direction(const DsoCatalog& cat, const double dir_ecl[3]);

} // namespace astra::catalog
