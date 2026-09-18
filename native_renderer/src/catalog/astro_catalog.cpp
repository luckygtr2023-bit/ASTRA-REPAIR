// astro_catalog.cpp — see astro_catalog.h for the provenance/precision contract.
#include "catalog/astro_catalog.h"
#include "catalog/sha256.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace astra::catalog {

// ─── frame conversion ───────────────────────────────────────────────────────
void icrs_to_ecliptic_j2000(double v[3]) {
    // rotation about the shared vernal +X by mean obliquity ε (J2000)
    const double ce = std::cos(OBLIQUITY_J2000_RAD), se = std::sin(OBLIQUITY_J2000_RAD);
    const double y = v[1] * ce + v[2] * se;
    const double z = -v[1] * se + v[2] * ce;
    v[1] = y; v[2] = z;
}

static void ecliptic_to_icrs_j2000(double v[3]) {
    const double ce = std::cos(OBLIQUITY_J2000_RAD), se = std::sin(OBLIQUITY_J2000_RAD);
    const double y = v[1] * ce - v[2] * se;
    const double z = v[1] * se + v[2] * ce;
    v[1] = y; v[2] = z;
}

void unit_from_ecliptic_double(const double u_ecl[3], double out[3]) {
    const double n = std::sqrt(u_ecl[0] * u_ecl[0] + u_ecl[1] * u_ecl[1] + u_ecl[2] * u_ecl[2]);
    if (n == 0.0) { out[0] = out[1] = out[2] = 0.0; return; }
    out[0] = u_ecl[0] / n; out[1] = u_ecl[1] / n; out[2] = u_ecl[2] / n;
}

// ─── binary loading (fail-closed) ───────────────────────────────────────────
struct Header {
    uint8_t magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t count;
    uint64_t record_size;
    uint8_t body_sha256[32];
};
static_assert(sizeof(Header) == 64, "header must be 64 B");

static bool read_all(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) return false;
    out.resize((size_t)n);
    f.read((char*)out.data(), n);
    return (bool)f;
}

static bool verify_body_sha(const uint8_t* body, size_t n, const uint8_t expect[32]) {
    Sha256 s; s.init(); s.update(body, n);
    uint8_t got[32]; s.final(got);
    return std::memcmp(got, expect, 32) == 0;
}

bool load_star_catalog(const std::string& path, StarCatalog& out, std::string& why) {
    out = StarCatalog{};
    std::vector<uint8_t> bytes;
    if (!read_all(path, bytes)) { why = "unreadable"; return false; }
    if (bytes.size() < sizeof(Header)) { why = "truncated header"; return false; }
    Header hd{};
    std::memcpy(&hd, bytes.data(), sizeof(Header));
    if (std::memcmp(hd.magic, STARS_MAGIC, 8) != 0) { why = "bad magic"; return false; }
    if (hd.version != CATALOG_FORMAT_VERSION || hd.reserved != 0) { why = "bad version"; return false; }
    if (hd.record_size != sizeof(StarRecord)) { why = "record size mismatch"; return false; }
    const size_t body_off = sizeof(Header);
    const size_t records_n = (size_t)hd.count;
    if (records_n == 0 || records_n > 10000000ull) { why = "implausible count"; return false; }
    if (records_n * sizeof(StarRecord) > bytes.size() - body_off) { why = "truncated records"; return false; }
    if (!verify_body_sha(bytes.data() + body_off, bytes.size() - body_off, hd.body_sha256)) {
        why = "body sha256 mismatch (tampered/corrupt dataset — refusing to load)";
        return false;
    }
    out.records.resize(records_n);
    std::memcpy(out.records.data(), bytes.data() + body_off, records_n * sizeof(StarRecord));
    // spectral table trailer (u32 count + len-prefixed ascii entries)
    size_t p = body_off + records_n * sizeof(StarRecord);
    if (p + 4 <= bytes.size()) {
        uint32_t nsp = 0;
        std::memcpy(&nsp, bytes.data() + p, 4); p += 4;
        out.spect_table.reserve(nsp + 1);
        out.spect_table.push_back("");  // key 0 = NONE
        for (uint32_t i = 0; i < nsp && p < bytes.size(); ++i) {
            const uint8_t len = bytes[p++];
            if (p + len > bytes.size()) break;
            out.spect_table.emplace_back((const char*)bytes.data() + p, (const char*)bytes.data() + p + len);
            p += len;
        }
    }
    // ICRS pc → ecliptic km (documented transform; conversion only — the
    // numbers themselves are the file truth).
    out.pos_km_ecl.resize(records_n);
    for (size_t i = 0; i < records_n; ++i) {
        double v[3] = {out.records[i].x_pc * PC_KM, out.records[i].y_pc * PC_KM, out.records[i].z_pc * PC_KM};
        if (out.records[i].flags & SFLAG_DIST_UNKNOWN) {
            // file stores a UNIT DIRECTION for shell-only stars — keep them at
            // exactly 1 pc scale meaning "direction only" never interpreted.
            v[0] = out.records[i].x_pc; v[1] = out.records[i].y_pc; v[2] = out.records[i].z_pc;
            icrs_to_ecliptic_j2000(v);          // direction → ecliptic (unit)
        } else {
            icrs_to_ecliptic_j2000(v);          // position → ecliptic km
        }
        out.pos_km_ecl[i] = {v[0], v[1], v[2]};
    }
    out.count = records_n;
    out.available = true;
    return true;
}

bool load_dso_catalog(const std::string& path, DsoCatalog& out, std::string& why) {
    out = DsoCatalog{};
    std::vector<uint8_t> bytes;
    if (!read_all(path, bytes)) { why = "unreadable"; return false; }
    if (bytes.size() < sizeof(Header)) { why = "truncated header"; return false; }
    Header hd{};
    std::memcpy(&hd, bytes.data(), sizeof(Header));
    if (std::memcmp(hd.magic, DSO_MAGIC, 8) != 0) { why = "bad magic"; return false; }
    if (hd.version != CATALOG_FORMAT_VERSION || hd.reserved != 0) { why = "bad version"; return false; }
    if (hd.record_size != sizeof(DsoRecord)) { why = "record size mismatch"; return false; }
    const size_t body_off = sizeof(Header);
    const size_t records_n = (size_t)hd.count;
    if (records_n == 0 || records_n > 10000000ull) { why = "implausible count"; return false; }
    if (records_n * sizeof(DsoRecord) != bytes.size() - body_off) { why = "size mismatch"; return false; }
    if (!verify_body_sha(bytes.data() + body_off, bytes.size() - body_off, hd.body_sha256)) {
        why = "body sha256 mismatch (tampered/corrupt dataset — refusing to load)";
        return false;
    }
    out.records.resize(records_n);
    std::memcpy(out.records.data(), bytes.data() + body_off, records_n * sizeof(DsoRecord));
    out.dir_ecl.resize(records_n);
    for (size_t i = 0; i < records_n; ++i) {
        double v[3] = {out.records[i].ux, out.records[i].uy, out.records[i].uz};
        unit_from_ecliptic_double(v, v);
        icrs_to_ecliptic_j2000(v);
        out.dir_ecl[i] = {v[0], v[1], v[2]};
    }
    out.count = records_n;
    out.available = true;
    return true;
}

// ─── visualization payload (single-precision boundary ONLY here) ─────────────
static inline double _dm3(const double a[3], const double b[3]) {
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void build_star_viz(const StarCatalog& cat, const double origin_ecl_km[3],
                    double shell_units,
                    void (*color_from_temp)(double, float[3], void*), void* ctx,
                    std::vector<CatStarViz>& out) {
    out.resize((size_t)cat.count);
    // Display rule (documented): direction FROM THE OBSERVER to the star
    // (true geometry incl. parallax), projected onto a fixed scene shell.
    for (size_t i = 0; i < (size_t)cat.count; ++i) {
        const StarRecord& r = cat.records[i];
        double dvec[3];
        if (r.flags & SFLAG_DIST_UNKNOWN) {
            // unit direction already (1-pc scale meaning "direction only")
            dvec[0] = cat.pos_km_ecl[i][0]; dvec[1] = cat.pos_km_ecl[i][1]; dvec[2] = cat.pos_km_ecl[i][2];
        } else {
            dvec[0] = cat.pos_km_ecl[i][0] - origin_ecl_km[0];
            dvec[1] = cat.pos_km_ecl[i][1] - origin_ecl_km[1];
            dvec[2] = cat.pos_km_ecl[i][2] - origin_ecl_km[2];
        }
        double u[3]; unit_from_ecliptic_double(dvec, u);
        CatStarViz& v = out[i];
        v.pos_units[0] = (float)(u[0] * shell_units);
        v.pos_units[1] = (float)(u[1] * shell_units);
        v.pos_units[2] = (float)(u[2] * shell_units);
        // magnitude-driven size (CINEMATIC display mapping; catalog mag NaN →
        // dim baseline, never invented brightness).
        const double mag = std::isnan(r.mag) ? 14.5 : (double)r.mag;
        double px = 7.0 - 0.55 * (mag - 0.5);
        if (px < 1.0) px = 1.0;
        if (px > 9.0) px = 9.0;
        v.size_px = (float)px;
        color_from_temp((double)r.temp_k, v.rgb, ctx);
        v.flags = (float)r.flags;
    }
}

void build_dso_viz(const DsoCatalog& cat, const double /*origin_ecl_km*/[3],
                   double shell_units, std::vector<CatDsoViz>& out) {
    out.resize((size_t)cat.count);
    for (size_t i = 0; i < (size_t)cat.count; ++i) {
        const DsoRecord& r = cat.records[i];
        CatDsoViz& v = out[i];
        v.pos_units[0] = (float)(cat.dir_ecl[i][0] * shell_units);
        v.pos_units[1] = (float)(cat.dir_ecl[i][1] * shell_units);
        v.pos_units[2] = (float)(cat.dir_ecl[i][2] * shell_units);
        // REAL angular size → world half-size on the shell (capped for legibility).
        double hw = 0.0;
        if (!std::isnan(r.maj_arcmin)) {
            const double half_rad = (double)r.maj_arcmin * 0.5 * 6.283185307179586 / 21600.0;
            hw = shell_units * std::tan(half_rad);
            const double cap = shell_units * 0.06;
            if (hw > cap) hw = cap;
            if (hw < shell_units * 0.0004) hw = shell_units * 0.0004;
        } else {
            hw = shell_units * 0.0012;  // unresolved point sources
        }
        v.half_size_units = (float)hw;
        // DSO glow color: neutral gray-white for extragalactic/catalog mix,
        // brightness from v_mag when present (REAL), dim floor otherwise.
        double b = 0.30;
        if (!std::isnan(r.v_mag)) {
            b = 0.85 - 0.16 * ((double)r.v_mag - 4.0) / 2.0;
            if (b < 0.18) b = 0.18;
            if (b > 1.0) b = 1.0;
        }
        v.rgb[0] = (float)(0.85 * b); v.rgb[1] = (float)(0.90 * b); v.rgb[2] = (float)(1.00 * b);
        v.flags = (float)r.flags;
    }
}

// ─── measurement (mirror of astra.catalog.measure) ──────────────────────────
StarMeasurement measure_star(const StarRecord& rec,
                             const std::array<double, 3>& pos_km_ecl,
                             const double observer_ecl_km[3]) {
    StarMeasurement m{};
    const bool dist_known = !(rec.flags & SFLAG_DIST_UNKNOWN);
    double rel[3];
    if (dist_known) {
        rel[0] = pos_km_ecl[0] - observer_ecl_km[0];
        rel[1] = pos_km_ecl[1] - observer_ecl_km[1];
        rel[2] = pos_km_ecl[2] - observer_ecl_km[2];
        const double d_cat = std::sqrt(pos_km_ecl[0] * pos_km_ecl[0] +
                                       pos_km_ecl[1] * pos_km_ecl[1] +
                                       pos_km_ecl[2] * pos_km_ecl[2]);
        m.dist_pc_catalog = d_cat / PC_KM;
        m.distance_km = _dm3(pos_km_ecl.data(), observer_ecl_km);
        m.distance_ly = m.distance_km / LY_KM;
        m.light_delay_years = m.distance_km / LY_KM;
        m.light_delay_s = m.light_delay_years * JULIAN_YEAR_S;
    } else {
        // direction-only: rel is defined as the catalog direction (honest:
        // observer shift cannot be applied without a distance).
        rel[0] = pos_km_ecl[0]; rel[1] = pos_km_ecl[1]; rel[2] = pos_km_ecl[2];
        m.distance_km = std::nan(""); m.distance_ly = std::nan("");
        m.light_delay_years = std::nan(""); m.light_delay_s = std::nan("");
        m.dist_pc_catalog = std::nan("");
    }
    double u[3]; unit_from_ecliptic_double(rel, u);
    m.dir_from_observer[0] = u[0]; m.dir_from_observer[1] = u[1]; m.dir_from_observer[2] = u[2];
    m.observer_ra_deg_ecl = std::fmod(std::atan2(u[1], u[0]) * 57.29577951308232 + 360.0, 360.0);
    double zclamp = u[2]; if (zclamp > 1.0) zclamp = 1.0; if (zclamp < -1.0) zclamp = -1.0;
    m.observer_lat_deg_ecl = std::asin(zclamp) * 57.29577951308232;
    // convert the measured direction back to ICRS for HUD parity with the
    // Python authority (same angles within 1e-12; gates assert).
    double ui[3] = {u[0], u[1], u[2]};
    ecliptic_to_icrs_j2000(ui);
    m.observer_ra_deg_icrs = std::fmod(std::atan2(ui[1], ui[0]) * 57.29577951308232 + 360.0, 360.0);
    double ic = ui[2]; if (ic > 1.0) ic = 1.0; if (ic < -1.0) ic = -1.0;
    m.observer_dec_deg_icrs = std::asin(ic) * 57.29577951308232;
    if (dist_known && !std::isnan(rec.absmag)) {
        const double d_pc = m.distance_km / PC_KM;
        m.apparent_mag = (double)rec.absmag + 5.0 * std::log10(d_pc / 10.0);
    } else {
        m.apparent_mag = std::nan("");
    }
    return m;
}

StarMeasurement measure_star_at_epoch(const StarRecord& rec,
                                       const std::array<double, 3>& pos_km_ecl,
                                       const double observer_ecl_km[3],
                                       double years_since_j2000) {
    if (years_since_j2000 == 0.0 || std::isnan(rec.pmra_mas_yr) || std::isnan(rec.pmdec_mas_yr) ||
        (rec.flags & SFLAG_DIST_UNKNOWN)) {
        return measure_star(rec, pos_km_ecl, observer_ecl_km);
    }
    // ICRS direction from the stored ecliptic position:
    const double d_km = std::sqrt(pos_km_ecl[0] * pos_km_ecl[0] +
                                  pos_km_ecl[1] * pos_km_ecl[1] +
                                  pos_km_ecl[2] * pos_km_ecl[2]);
    if (d_km == 0.0) return measure_star(rec, pos_km_ecl, observer_ecl_km);
    double u[3] = {pos_km_ecl[0] / d_km, pos_km_ecl[1] / d_km, pos_km_ecl[2] / d_km};
    ecliptic_to_icrs_j2000(u);
    const double ra = std::fmod(std::atan2(u[1], u[0]) * 57.29577951308232 + 360.0, 360.0);
    double zc = u[2]; if (zc > 1.0) zc = 1.0; if (zc < -1.0) zc = -1.0;
    const double dec = std::asin(zc) * 57.29577951308232;
    // small-angle proper-motion (μ_α* convention; mirrors the Python authority)
    const double cd = std::cos(dec * 0.017453292519943295);
    if (std::fabs(cd) < 1e-6) return measure_star(rec, pos_km_ecl, observer_ecl_km);
    constexpr double MAS_PER_DEG = 3600.0 * 1000.0;
    const double d_ra = ((double)rec.pmra_mas_yr / MAS_PER_DEG) * years_since_j2000 / cd;
    const double d_dec = ((double)rec.pmdec_mas_yr / MAS_PER_DEG) * years_since_j2000;
    double ra2 = std::fmod(ra + d_ra + 3600.0, 360.0);
    double dec2 = dec + d_dec;
    if (dec2 > 90.0) dec2 = 90.0;
    if (dec2 < -90.0) dec2 = -90.0;
    const double co = std::cos(dec2 * 0.017453292519943295);
    double v[3] = {co * std::cos(ra2 * 0.017453292519943295),
                   co * std::sin(ra2 * 0.017453292519943295),
                   std::sin(dec2 * 0.017453292519943295)};
    icrs_to_ecliptic_j2000(v);
    const std::array<double, 3> pos2 = {v[0] * d_km, v[1] * d_km, v[2] * d_km};
    return measure_star(rec, pos2, observer_ecl_km);
}

size_t nearest_star_by_direction(const StarCatalog& cat, const double from_ecl_km[3],
                                 const double dir_ecl[3]) {
    if (!cat.available || cat.count == 0) return SIZE_MAX;
    double best = -2.0;
    size_t best_i = SIZE_MAX;
    for (size_t i = 0; i < (size_t)cat.count; ++i) {
        double rel[3];
        if (cat.records[i].flags & SFLAG_DIST_UNKNOWN) {
            rel[0] = cat.pos_km_ecl[i][0]; rel[1] = cat.pos_km_ecl[i][1]; rel[2] = cat.pos_km_ecl[i][2];
        } else {
            rel[0] = cat.pos_km_ecl[i][0] - from_ecl_km[0];
            rel[1] = cat.pos_km_ecl[i][1] - from_ecl_km[1];
            rel[2] = cat.pos_km_ecl[i][2] - from_ecl_km[2];
        }
        double u[3]; unit_from_ecliptic_double(rel, u);
        const double d = u[0] * dir_ecl[0] + u[1] * dir_ecl[1] + u[2] * dir_ecl[2];
        if (d > best) { best = d; best_i = i; }
    }
    return best_i;
}

size_t nearest_dso_by_direction(const DsoCatalog& cat, const double dir_ecl[3]) {
    if (!cat.available || cat.count == 0) return SIZE_MAX;
    double best = -2.0;
    size_t best_i = SIZE_MAX;
    for (size_t i = 0; i < (size_t)cat.count; ++i) {
        const double d = cat.dir_ecl[i][0] * dir_ecl[0] + cat.dir_ecl[i][1] * dir_ecl[1] + cat.dir_ecl[i][2] * dir_ecl[2];
        if (d > best) { best = d; best_i = i; }
    }
    return best_i;
}

} // namespace astra::catalog
