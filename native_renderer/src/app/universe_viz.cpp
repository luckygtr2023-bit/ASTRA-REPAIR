// universe_viz.cpp — v1.3 visual-integration builders.
// Every number originates from the NATIVE MIRRORS of the Python authorities
// (destruction_sim / evolution_sim / observation_sim). Transforms applied at
// this boundary are documented per batch (km->units, anchor subtraction,
// CINEMATIC readability spans). Deterministic: same call + seeds => same list.
#include "app/universe_viz.h"

#include <cmath>
#include <cstdio>

namespace astra::app {

const char* u13_class_name(unsigned flags) {
    if (flags & U13_SPECULATIVE) return "SPECULATIVE";
    if (flags & U13_THEORETICAL) return "THEORETICAL";
    if (flags & U13_PHYSICALLY_MODELED) return "PHYSICALLY-MODELED";
    if (flags & U13_CINEMATIC) return "CINEMATIC";
    if (flags & U13_SIMULATED) return "SIMULATED";
    return "NOT AVAILABLE";
}

namespace {

inline void push_seg(SegmentBatch& b, const double a[3], const double c[3]) {
    b.pts.push_back((float)a[0]); b.pts.push_back((float)a[1]); b.pts.push_back((float)a[2]);
    b.pts.push_back((float)c[0]); b.pts.push_back((float)c[1]); b.pts.push_back((float)c[2]);
}

inline double dist(const Vec3d& a, const Vec3d& b) {
    const double dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

struct Frame3 { double u[3], v[3]; };
Frame3 ortho_frame(const double n[3]) {
    Frame3 f{};
    const double an = std::fabs(n[1]) < 0.9 ? 0.0 : 1.0;
    const double a[3] = {an, an ? 0.0 : 1.0, 0.0};
    const double dot = n[0]*a[0] + n[1]*a[1] + n[2]*a[2];
    double w[3] = {a[0]-dot*n[0], a[1]-dot*n[1], a[2]-dot*n[2]};
    const double wl = std::sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
    for (int i = 0; i < 3; ++i) w[i] /= (wl > 0 ? wl : 1.0);
    f.u[0] = n[1]*w[2] - n[2]*w[1];
    f.u[1] = n[2]*w[0] - n[0]*w[2];
    f.u[2] = n[0]*w[1] - n[1]*w[0];
    f.v[0] = w[0]; f.v[1] = w[1]; f.v[2] = w[2];
    return f;
}

inline void batch_begin(SegmentBatch& b, const Vec3d& anchor_km, const float color[3],
                        unsigned flags, const char* label) {
    b.pts.clear();
    b.anchor_km[0] = anchor_km[0]; b.anchor_km[1] = anchor_km[1]; b.anchor_km[2] = anchor_km[2];
    b.color[0] = color[0]; b.color[1] = color[1]; b.color[2] = color[2];
    b.flags = flags; b.label = label;
}

inline void to_units(const Vec3d& p_km, const Vec3d& anchor_km, double pos_scale, double out[3]) {
    out[0] = (p_km[0] - anchor_km[0]) * pos_scale;
    out[1] = (p_km[1] - anchor_km[1]) * pos_scale;
    out[2] = (p_km[2] - anchor_km[2]) * pos_scale;
}

constexpr size_t DESTR_VERTEX_CAP = 16384;
constexpr size_t EVO_VERTEX_CAP   = 8192;
constexpr size_t GALA_VERTEX_CAP  = 12288;
inline size_t verts(const SegmentBatch& b) { return b.pts.size() / 3; }

// Destruction fragment/ejecta spokes are regenerated both at build and on
// simulation-time advances (uniform-velocity streaming of the authority
// debris — a pure function of mirror outputs; documented in the .h).
void destruction_debris_batches(const U13DestructionOverlay& ov, double flight_shift_s) {
    SegmentBatch& frag = const_cast<U13DestructionOverlay&>(ov).batches[2];
    SegmentBatch& ejct = const_cast<U13DestructionOverlay&>(ov).batches[3];
    const DestructionImpactResult& res = ov.result;
    const Vec3d anchor{frag.anchor_km[0], frag.anchor_km[1], frag.anchor_km[2]};
    const double S = ov.pos_scale;
    frag.pts.clear();
    ejct.pts.clear();

    const double cap_r_km = ov.event.target_radius_m / 1000.0;
    const size_t nfrag = res.fragments.size();
    for (size_t f = 0; f < nfrag && verts(frag) < DESTR_VERTEX_CAP - 120; ++f) {
        const DestructionFragment& fr = res.fragments[f];
        const double vm = fr.velocity.magnitude();
        const double vh[3] = {vm > 0.0 ? fr.velocity.x / vm : 1.0,
                              vm > 0.0 ? fr.velocity.y / vm : 0.0,
                              vm > 0.0 ? fr.velocity.z / vm : 0.0};
        const Frame3 F = ortho_frame(vh);
        const double r0 = cap_r_km * 1.6 * 1000.0, r1 = r0 * 0.35;
        double prev[3] = {0, 0, 0};
        for (int si = 0; si <= 16; ++si) {
            const double phi = (double)si / 16.0 * 4.0 * 3.14159265358979323846;
            const double rr = r0 + (r1 - r0) * ((double)si / 16.0);
            const double along = (double)si * 0.25 * vm + flight_shift_s * vm; // 0.25 s step (t=4 s span)
            Vec3d pk{(fr.position.x + vh[0] * along + (F.u[0]*std::cos(phi)+F.v[0]*std::sin(phi)) * rr) / 1000.0,
                     (fr.position.y + vh[1] * along + (F.u[1]*std::cos(phi)+F.v[1]*std::sin(phi)) * rr) / 1000.0,
                     (fr.position.z + vh[2] * along + (F.u[2]*std::cos(phi)+F.v[2]*std::sin(phi)) * rr) / 1000.0};
            double u[3]; to_units(pk, anchor, S, u);
            if (si > 0) push_seg(frag, prev, u);
            prev[0] = u[0]; prev[1] = u[1]; prev[2] = u[2];
        }
    }
    const size_t nej = res.ejecta.size();
    const double rr_km = cap_r_km * 2.35;
    for (size_t f = 0; f < nej && verts(ejct) < DESTR_VERTEX_CAP - 120; ++f) {
        const DestructionEjecta& ej = res.ejecta[f];
        const double vm = ej.velocity.magnitude();
        const double vh[3] = {vm > 0 ? ej.velocity.x / vm : 0.0,
                              vm > 0 ? ej.velocity.y / vm : 1.0,
                              vm > 0 ? ej.velocity.z / vm : 0.0};
        const double sh = flight_shift_s * vm;
        Vec3d p0{(ej.position.x + vh[0] * sh) / 1000.0,
                 (ej.position.y + vh[1] * sh) / 1000.0,
                 (ej.position.z + vh[2] * sh) / 1000.0};
        Vec3d p1{(ej.position.x + vh[0] * (sh + rr_km * 1000.0 * 0.4)) / 1000.0,
                 (ej.position.y + vh[1] * (sh + rr_km * 1000.0 * 0.4)) / 1000.0,
                 (ej.position.z + vh[2] * (sh + rr_km * 1000.0 * 0.4)) / 1000.0};
        double u0[3], u1[3];
        to_units(p0, anchor, S, u0);
        to_units(p1, anchor, S, u1);
        push_seg(ejct, u0, u1);
    }
    // contact-point ring outline (CINEMATIC sweep around the authority contact point)
    const DestVec3 c = res.geometry.contact_point;
    const DestVec3 n = res.geometry.surface_normal;
    const double nl = n.magnitude();
    const double nh[3] = {nl > 0 ? n.x / nl : 1.0, nl > 0 ? n.y / nl : 0.0, 0.0};
    const Frame3 F = ortho_frame(nh);
    double prev[3] = {0, 0, 0};
    for (int i = 0; i <= 24 && verts(ejct) < DESTR_VERTEX_CAP - 52; ++i) {
        const double a = (double)i / 24.0 * 2.0 * 3.14159265358979323846;
        Vec3d pk{(c.x + (F.u[0]*std::cos(a)+F.v[0]*std::sin(a)) * rr_km * 1000.0) / 1000.0,
                 (c.y + (F.u[1]*std::cos(a)+F.v[1]*std::sin(a)) * rr_km * 1000.0) / 1000.0,
                 (c.z + (F.u[2]*std::cos(a)+F.v[2]*std::sin(a)) * rr_km * 1000.0) / 1000.0};
        double u[3]; to_units(pk, anchor, S, u);
        if (i > 0) push_seg(ejct, prev, u);
        prev[0] = u[0]; prev[1] = u[1]; prev[2] = u[2];
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 1) Destruction overlay
// ---------------------------------------------------------------------------
bool u13_build_destruction_overlay(const std::vector<CelestialBody>& bodies,
                                   const std::vector<Vec3d>& world_km,
                                   const std::vector<Vec3d>& vel_km_s,
                                   const Vec3d& origin_km,
                                   int target_idx, int impactor_idx,
                                   double sim_time_s, long long seed,
                                   double pos_scale, U13DestructionOverlay& out) {
    out = U13DestructionOverlay{};
    if (target_idx < 0 || impactor_idx < 0 || target_idx == impactor_idx) return false;
    if ((size_t)target_idx >= bodies.size() || (size_t)impactor_idx >= bodies.size()) return false;

    const CelestialBody& T = bodies[(size_t)target_idx];
    const CelestialBody& I = bodies[(size_t)impactor_idx];

    DestructionImpactEvent ev;
    ev.impact_id = "u13:impact";
    ev.impactor_id = "u13:" + I.name;
    ev.target_id = "u13:" + T.name;
    ev.sim_time_s = sim_time_s;
    ev.impactor_mass_kg = I.mass_kg;
    ev.target_mass_kg = T.mass_kg;
    ev.impactor_position = DestVec3{world_km[(size_t)impactor_idx][0] * 1000.0,
                                    world_km[(size_t)impactor_idx][1] * 1000.0,
                                    world_km[(size_t)impactor_idx][2] * 1000.0};
    ev.target_position   = DestVec3{world_km[(size_t)target_idx][0] * 1000.0,
                                    world_km[(size_t)target_idx][1] * 1000.0,
                                    world_km[(size_t)target_idx][2] * 1000.0};
    ev.impactor_velocity = DestVec3{vel_km_s[(size_t)impactor_idx][0] * 1000.0,
                                    vel_km_s[(size_t)impactor_idx][1] * 1000.0,
                                    vel_km_s[(size_t)impactor_idx][2] * 1000.0};
    ev.target_velocity   = DestVec3{vel_km_s[(size_t)target_idx][0] * 1000.0,
                                    vel_km_s[(size_t)target_idx][1] * 1000.0,
                                    vel_km_s[(size_t)target_idx][2] * 1000.0};
    ev.impactor_radius_m = I.radius_km * 1000.0;
    ev.target_radius_m = T.radius_km * 1000.0;

    const DestructionConfig cfg;
    const DestVec3 sep{ev.target_position.x - ev.impactor_position.x,
                       ev.target_position.y - ev.impactor_position.y,
                       ev.target_position.z - ev.impactor_position.z};
    const double r_sep = sep.magnitude();
    if (!(r_sep > 0.0)) return false;                       // degenerate centres
    if (r_sep > (ev.target_radius_m + ev.impactor_radius_m) * 4.0) return false;
    // Overlap gate: only pairs physically at/near contact get an impact view.
    // (Displayed geometric centre distance vs the combined radii, x4 ledger.)
    DestructionImpactResult res;
    if (dest_execute_impact(ev, cfg, seed, DestructionDamageState::INTACT, 0.05, 100.0, res)
        != DestErr::OK) return false;

    out.valid = true;
    out.event = ev;
    out.result = res;
    out.state_after = res.state_after;
    out.target_idx = target_idx;
    out.impactor_idx = impactor_idx;
    out.pos_scale = pos_scale;
    out.impact_sim_time_s = sim_time_s;
    out.t_build_s = sim_time_s;
    out.batches.resize(5);

    // ---- batch 0: contact-point reticle (GLASS)
    {
        const float col[3] = {0.45f, 0.80f, 1.00f}; // GLASS azure
        SegmentBatch& b = out.batches[0];
        batch_begin(b, origin_km, col, U13_SIMULATED | U13_CINEMATIC,
                    "IMPACT POINT RETICLE (geometry mirror; span CINEMATIC)");
        const DestVec3 c = res.geometry.contact_point;
        const DestVec3 n = res.geometry.surface_normal;
        const double nl = n.magnitude();
        const double nh[3] = {nl > 0 ? n.x / nl : 1.0, nl > 0 ? n.y / nl : 0.0, 0.0};
        const Frame3 F = ortho_frame(nh);
        const double r_km = T.radius_km * 1.35;
        double prev[3] = {0, 0, 0};
        for (int i = 0; i <= 48; ++i) {
            const double a = (double)i / 48.0 * 2.0 * 3.14159265358979323846;
            Vec3d pk{(c.x + (F.u[0]*std::cos(a)+F.v[0]*std::sin(a)) * r_km * 1000.0) / 1000.0,
                     (c.y + (F.u[1]*std::cos(a)+F.v[1]*std::sin(a)) * r_km * 1000.0) / 1000.0,
                     (c.z + (F.u[2]*std::cos(a)+F.v[2]*std::sin(a)) * r_km * 1000.0) / 1000.0};
            double u[3]; to_units(pk, origin_km, pos_scale, u);
            if (i > 0) push_seg(b, prev, u);
            prev[0] = u[0]; prev[1] = u[1]; prev[2] = u[2];
        }
    }
    // ---- batch 1: approach trajectory cone (LOOM)
    {
        const float col[3] = {0.55f, 1.00f, 0.90f}; // LOOM cyan
        SegmentBatch& b = out.batches[1];
        batch_begin(b, origin_km, col, U13_SIMULATED, "APPROACH TRAJECTORY CONE");
        const Vec3d& ti = world_km[(size_t)impactor_idx];
        const Vec3d& tt = world_km[(size_t)target_idx];
        double d[3] = {(tt[0] - ti[0]) * pos_scale, (tt[1] - ti[1]) * pos_scale,
                       (tt[2] - ti[2]) * pos_scale};
        const double dl = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        for (int k = 0; k < 3; ++k) d[k] /= (dl > 0.0 ? dl : 1.0);
        double tip[3], t0u[3];
        to_units(tt, origin_km, pos_scale, tip);
        to_units(ti, origin_km, pos_scale, t0u);
        push_seg(b, tip, t0u); // shaft
        const Frame3 F = ortho_frame(d);
        const double rr = dl * 0.06;
        double prev[3] = {0, 0, 0};
        for (int i = 0; i <= 10; ++i) {
            const double a = (double)i / 10.0 * 2.0 * 3.14159265358979323846;
            double p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = tip[k] + (F.u[k] * std::cos(a) + F.v[k] * std::sin(a)) * rr;
            push_seg(b, tip, p);
            if (i > 0) push_seg(b, prev, p);
            prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
        }
    }
    // ---- batches 2+3: debris (SNOW / PEARL) — shared with the time-advance
    {
        const float col2[3] = {0.55f, 0.95f, 0.85f}; // MINT aero (no purple pool)
        batch_begin(out.batches[2], origin_km, col2, U13_SIMULATED,
                    "FRAGMENTS (execute_impact output)");
        const float col3[3] = {0.80f, 0.95f, 0.55f}; // PEARL gold
        batch_begin(out.batches[3], origin_km, col3, U13_SIMULATED,
                    "EJECTA PLUME + CONTACT RING");
        destruction_debris_batches(out, 0.0);
    }
    // ---- batch 4: momentum transfer arrow (HAZARD)
    {
        const float col[3] = {1.00f, 0.58f, 0.80f}; // HAZARD rose
        SegmentBatch& b = out.batches[4];
        batch_begin(b, origin_km, col, U13_SIMULATED, "MOMENTUM TRANSFER ARROW");
        const DestVec3 p_t = res.momentum.transferred_momentum_kg_m_s;
        const double pl = p_t.magnitude();
        if (pl > 0.0) {
            const double ph[3] = {p_t.x / pl, p_t.y / pl, p_t.z / pl};
            const DestVec3 c = res.geometry.contact_point;
            Vec3d a_km{c.x / 1000.0, c.y / 1000.0, c.z / 1000.0};
            Vec3d e_km{(c.x + ph[0] * T.radius_km * 1000.0 * 0.9) / 1000.0,
                       (c.y + ph[1] * T.radius_km * 1000.0 * 0.9) / 1000.0,
                       (c.z + ph[2] * T.radius_km * 1000.0 * 0.9) / 1000.0};
            double ua[3], ue[3];
            to_units(a_km, origin_km, pos_scale, ua);
            to_units(e_km, origin_km, pos_scale, ue);
            push_seg(b, ua, ue);
            const Frame3 F = ortho_frame(ph);
            const double fin = (T.radius_km * pos_scale) * 0.18;
            for (int k = 0; k < 3; ++k) {
                const double ang = (double)k / 3.0 * 2.0 * 3.14159265358979323846;
                double off[3];
                off[0] = ue[0] - ph[0] * fin * 2.2 + (F.u[0]*std::cos(ang)+F.v[0]*std::sin(ang)) * fin;
                off[1] = ue[1] - ph[1] * fin * 2.2 + (F.u[1]*std::cos(ang)+F.v[1]*std::sin(ang)) * fin;
                off[2] = ue[2] - ph[2] * fin * 2.2 + (F.u[2]*std::cos(ang)+F.v[2]*std::sin(ang)) * fin;
                push_seg(b, ue, off);
            }
        }
    }
    return true;
}

void u13_advance_destruction_overlay(U13DestructionOverlay& io, double sim_time_s) {
    if (!io.valid) return;
    const double shift = sim_time_s - io.impact_sim_time_s;
    destruction_debris_batches(io, shift > 0.0 ? shift : 0.0);
    io.t_build_s = sim_time_s;
}

// ---------------------------------------------------------------------------
// 2) Evolution overlay
// ---------------------------------------------------------------------------
bool u13_build_evolution_overlay(double fig_scale, U13EvolutionOverlay& out) {
    out = U13EvolutionOverlay{};
    constexpr int NSAMPLES = 24;
    char line[160];

    EvolState gstate;
    gstate.model_id = "astra.evolution.galaxy.v1";
    gstate.cosmic_time_gyr = 0.05;
    gstate.quantities["sfr_msun_per_yr"] = EvolQuantity{5.0, EvolProvenance::SIMULATED_DATA};
    gstate.quantities["gas_mass_msun"] = EvolQuantity{1.0e10, EvolProvenance::SIMULATED_DATA};
    gstate.quantities["stellar_mass_msun"] = EvolQuantity{5.0e10, EvolProvenance::SIMULATED_DATA};
    gstate.quantities["metallicity"] = EvolQuantity{0.02, EvolProvenance::SIMULATED_DATA};
    gstate.quantities["central_bh_mass_msun"] = EvolQuantity{1.0e6, EvolProvenance::SIMULATED_DATA};
    gstate.quantities["luminosity_Lsun"] = EvolQuantity{2.0e10, EvolProvenance::SIMULATED_DATA};

    EvolState sstate;
    sstate.model_id = "astra.evolution.stellar.v1";
    sstate.cosmic_time_gyr = 0.05;
    sstate.phase = "MAIN_SEQUENCE";
    sstate.quantities["age_gyr"] = EvolQuantity{6.4, EvolProvenance::SIMULATED_DATA};
    sstate.quantities["mass_msun"] = EvolQuantity{1.0, EvolProvenance::SIMULATED_DATA};

    EvolState wstate;
    wstate.model_id = "astra.evolution.web.v1";
    wstate.cosmic_time_gyr = 0.05;
    wstate.quantities["filament_count"] = EvolQuantity{20.0, EvolProvenance::SIMULATED_DATA};
    wstate.quantities["void_count"] = EvolQuantity{15.0, EvolProvenance::SIMULATED_DATA};
    wstate.quantities["node_count"] = EvolQuantity{5.0, EvolProvenance::SIMULATED_DATA};
    EvolState vstate;
    vstate.model_id = "astra.evolution.void.v1";
    vstate.cosmic_time_gyr = 0.05;
    vstate.quantities["radius_mpc"] = EvolQuantity{10.0, EvolProvenance::SIMULATED_DATA};
    vstate.quantities["density_contrast"] = EvolQuantity{-0.8, EvolProvenance::SIMULATED_DATA};

    {
        const float gc[3] = {1.00f, 0.82f, 0.30f}; // GLOW fire
        batch_begin(out.spiral, Vec3d{0,0,0}, gc, U13_SIMULATED | U13_CINEMATIC,
                    "EVOLUTION SPIRAL (galaxy, CINEMATIC axes)");
        const float sc[3] = {0.55f, 0.95f, 1.00f}; // steel
        batch_begin(out.stellar, Vec3d{0,0,0}, sc, U13_SIMULATED, "STELLAR LUMINOSITY STRIP");
        const float wc[3] = {0.55f, 1.00f, 0.90f}; // LOOM cyan
        batch_begin(out.webline, Vec3d{0,0,0}, wc, U13_THEORETICAL, "COSMIC WEB COUNTS");
        const float vc[3] = {1.00f, 0.72f, 0.55f}; // SAND citrus (no purple pool)
        batch_begin(out.voidline, Vec3d{0,0,0}, vc, U13_THEORETICAL, "VOID COUNTS");
    }

    // Time axis: NSAMPLES uniform steps spanning (0.05, 13.8] Gyr. Each step
    // is a single authoritative `evol_make_*` integration step (dt accepted
    // by the mirror, exactly as the Python engine accepts dt_gyr).
    const double dt = (13.8 - 0.05) / (double)NSAMPLES;
    double ps[3] = {0,0,0}, pt[3] = {0,0,0}, pw[3] = {0,0,0}, pv[3] = {0,0,0};
    bool first = true;
    for (int step = 0; step < NSAMPLES && gstate.cosmic_time_gyr < 13.8; ++step) {
        const double t = 0.05 + (step + 1) * dt;
        EvolState g2, s2, w2, v2;
        if (evol_make_galaxy_step_default(gstate, dt, gstate.model_id, g2) != EvolErr::OK) break;
        if (evol_make_stellar_step(sstate, dt, sstate.model_id, s2) != EvolErr::OK) break;
        if (evol_make_cosmic_web_step(wstate, dt, wstate.model_id, w2) != EvolErr::OK) break;
        if (evol_make_void_step(vstate, dt, vstate.model_id, 0.01, v2) != EvolErr::OK) break;
        gstate = g2; sstate = s2; wstate = w2; vstate = v2;

        const double sfr = gstate.quantities["sfr_msun_per_yr"].value;
        const double mstar = gstate.quantities["stellar_mass_msun"].value;
        const double Z = gstate.quantities["metallicity"].value;
        const double L = gstate.quantities["luminosity_Lsun"].value;
        const double age = sstate.quantities["age_gyr"].value;
        out.t_axis_gyr.push_back(t);
        out.sfr_series.push_back(sfr);
        out.luminosity_series.push_back(L);
        out.age_series_gyr.push_back(age);
        int phase_i = (int)EvolStellarPhase::MAIN_SEQUENCE;
        if (sstate.phase == "WHITE_DWARF") phase_i = (int)EvolStellarPhase::WHITE_DWARF;
        else if (sstate.phase == "NEUTRON_STAR") phase_i = (int)EvolStellarPhase::NEUTRON_STAR;
        else if (sstate.phase == "BLACK_HOLE") phase_i = (int)EvolStellarPhase::BLACK_HOLE;
        out.phase_series.push_back(phase_i);

        const double z = t * 2.0 * fig_scale;
        const double x = std::log10(1.0 + sfr) * 4.0 * fig_scale;
        const double y = (std::log10(1.0 + mstar) - 9.0) * 2.0 * fig_scale;
        const double sp[3] = {x, y, z};
        const double st[3] = {x, y + (L / 1.0e10) * 0.6 * fig_scale, z};
        const double wl[3] = {x, (wstate.quantities["filament_count"].value - 20.0) * 0.35 * fig_scale, z};
        const double vl[3] = {x, (vstate.quantities["void_count"].value - 15.0) * -0.25 * fig_scale, z};
        if (!first) {
            push_seg(out.spiral, ps, sp);
            push_seg(out.stellar, pt, st);
            push_seg(out.webline, pw, wl);
            push_seg(out.voidline, pv, vl);
        }
        ps[0]=sp[0]; ps[1]=sp[1]; ps[2]=sp[2];
        pt[0]=st[0]; pt[1]=st[1]; pt[2]=st[2];
        pw[0]=wl[0]; pw[1]=wl[1]; pw[2]=wl[2];
        pv[0]=vl[0]; pv[1]=vl[1]; pv[2]=vl[2];
        first = false;

        std::snprintf(line, sizeof(line),
            "t=%4.2f Gyr | SFR=%6.3f  M*=%6.3e  Z=%7.5f  L=%6.3e  phase=%s",
            t, sfr, mstar, Z, L, evol_stellar_phase_name((EvolStellarPhase)phase_i));
        out.summary_lines.push_back(line);
    }
    std::snprintf(line, sizeof(line), "final stellar phase: %s | age=%.3f Gyr",
                  sstate.phase.c_str(), sstate.quantities["age_gyr"].value);
    out.summary_lines.push_back(line);
    out.t_max_gyr = gstate.cosmic_time_gyr;
    out.valid = !out.spiral.pts.empty();
    return out.valid;
}

// ---------------------------------------------------------------------------
// 3) Observation overlay
// ---------------------------------------------------------------------------
// World position (km, heliocentric) of body `idx` at time `ts` via the exact
// propagate_world composition (parents precede children in the vector):
// world[i] = world[parent] + orbital_position(elements[i], ts).
static Vec3d u13_body_world_at(const std::vector<CelestialBody>& bodies, int idx, double ts) {
    const CelestialBody& b = bodies[(size_t)idx];
    const Vec3d rel = orbital_position(b.elements, ts);
    if (b.parent >= 0) {
        const Vec3d pw = u13_body_world_at(bodies, b.parent, ts);
        return Vec3d{pw[0] + rel[0], pw[1] + rel[1], pw[2] + rel[2]};
    }
    return rel;
}

bool u13_build_observation_overlay(const std::vector<CelestialBody>& bodies,
                                   const Vec3d& origin_km,
                                   int observer_idx, int subject_idx,
                                   double sim_time_s, double pos_scale,
                                   U13ObservationOverlay& out) {
    out = U13ObservationOverlay{};
    if (observer_idx < 0 || subject_idx < 0 || observer_idx == subject_idx) return false;
    if ((size_t)observer_idx >= bodies.size() || (size_t)subject_idx >= bodies.size()) return false;
    const Vec3d O = u13_body_world_at(bodies, observer_idx, sim_time_s);
    const Vec3d S = u13_body_world_at(bodies, subject_idx, sim_time_s);
    const double d_km = dist(O, S);
    if (!(d_km > 0.0)) return false;

    out.observer_idx = observer_idx;
    out.subject_idx = subject_idx;
    out.t_build_s = sim_time_s;

    // Use the astra.temporal mirror for the emission time (flat-space emission
    // equation bisected by the authority); worldline = 65 samples of the true
    // ephemeris over [now - 1.3 * flat-rule lookback - 32 s(480 s floor), now].
    const double c_km_s = SPEED_OF_LIGHT / 1000.0;
    const double window_s = std::max(d_km / c_km_s, 480.0) * 1.3;
    TempWorldline wl;
    wl.params.reserve(65);
    wl.events.reserve(65);
    const double w0 = sim_time_s - window_s - 32.0;
    for (int i = 0; i < 65; ++i) {
        const double ts = w0 + (sim_time_s - w0) * ((double)i / 64.0);
        const Vec3d p = u13_body_world_at(bodies, subject_idx, ts);
        wl.params.push_back(ts);
        wl.events.push_back(TempChartEvent{ts * SPEED_OF_LIGHT,
                                           p[0] * 1000.0, p[1] * 1000.0, p[2] * 1000.0, true});
    }
    const double ox_m[3] = {O[0] * 1000.0, O[1] * 1000.0, O[2] * 1000.0};
    ObservedState ovs;
    if (temp_observe(wl, ox_m, sim_time_s, "cosmos", ovs) != TempErr::OK || !(ovs.lookback_time_s > 0.0)) {
        // Degenerate window (lookback below the sampling grid): fall back to
        // the flat-rule estimate with the subject held at its CURRENT
        // ephemeris position (documented SIMULATED fallback; the light-travel
        // delay itself is still the authority value distance/c).
        const double lb_est = d_km / c_km_s;
        ovs = ObservedState{};
        ovs.observation_time_s = sim_time_s;
        ovs.emission_time_s = sim_time_s - lb_est;
        ovs.lookback_time_s = lb_est;
        ovs.emission_event = TempChartEvent{(sim_time_s - lb_est) * SPEED_OF_LIGHT,
                                            S[0]*1000.0, S[1]*1000.0, S[2]*1000.0, true};
        ovs.actual_state_at_observation = TempChartEvent{sim_time_s * SPEED_OF_LIGHT,
                                                         S[0]*1000.0, S[1]*1000.0, S[2]*1000.0, true};
    }
    out.lookback_s = ovs.lookback_time_s;

    {
        const float col[3] = {0.30f, 0.70f, 1.00f}; // SKY azure
        batch_begin(out.emission_ring, origin_km, col, U13_SIMULATED,
                    "OBSERVED (RETARDED) POSITION RING");
        const double ring_km = d_km * 0.01; // CINEMATIC readability span
        const Vec3d em_km{ovs.emission_event.x / 1000.0, ovs.emission_event.y / 1000.0,
                          ovs.emission_event.z / 1000.0};
        double norm[3] = {em_km[0] - O[0], em_km[1] - O[1], em_km[2] - O[2]};
        const double nl = std::sqrt(norm[0]*norm[0]+norm[1]*norm[1]+norm[2]*norm[2]);
        for (int i = 0; i < 3; ++i) norm[i] /= (nl > 0.0 ? nl : 1.0);
        const Frame3 F = ortho_frame(norm);
        double prev[3] = {0,0,0};
        for (int i = 0; i <= 36; ++i) {
            const double a = (double)i / 36.0 * 2.0 * 3.14159265358979323846;
            const Vec3d pk{em_km[0] + (F.u[0]*std::cos(a)+F.v[0]*std::sin(a)) * ring_km,
                           em_km[1] + (F.u[1]*std::cos(a)+F.v[1]*std::sin(a)) * ring_km,
                           em_km[2] + (F.u[2]*std::cos(a)+F.v[2]*std::sin(a)) * ring_km};
            double u[3]; to_units(pk, origin_km, pos_scale, u);
            if (i > 0) push_seg(out.emission_ring, prev, u);
            prev[0] = u[0]; prev[1] = u[1]; prev[2] = u[2];
        }
    }
    {
        const float col[3] = {1.00f, 0.82f, 0.30f}; // GLOW fire
        batch_begin(out.null_path, origin_km, col, U13_SIMULATED,
                    "LIGHT PATH emission->observer (null segment)");
        double a[3], b[3], c[3];
        Vec3d em_km{ovs.emission_event.x / 1000.0, ovs.emission_event.y / 1000.0,
                    ovs.emission_event.z / 1000.0};
        to_units(O, origin_km, pos_scale, a);
        to_units(em_km, origin_km, pos_scale, b);
        push_seg(out.null_path, a, b); // light path (past light cone)
        // ACTUAL-state marker: cross at the subject's current position
        to_units(S, origin_km, pos_scale, c);
        const double arm = 6.0 * pos_scale; // ~6 km arms in units (CINEMATIC)
        double h1[3] = {c[0] - arm, c[1], c[2]}, h2[3] = {c[0] + arm, c[1], c[2]};
        double v1[3] = {c[0], c[1] - arm, c[2]}, v2[3] = {c[0], c[1] + arm, c[2]};
        push_seg(out.null_path, h1, h2);
        push_seg(out.null_path, v1, v2);
    }
    char line[160];
    std::snprintf(line, sizeof(line),
        "OBS delay=%.6e s | t_emit=%.2f s | ring span=dist*1%% (CINEMATIC)",
        out.lookback_s, ovs.emission_time_s);
    out.hud_line = line;
    out.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// 4) Galactic halo overlay
// ---------------------------------------------------------------------------
bool u13_build_galactic_overlay(double unit_scale, U13GalacticOverlay& out) {
    out = U13GalacticOverlay{};
    EvolState halo;
    halo.model_id = "astra.evolution.halo.v1";
    halo.cosmic_time_gyr = 0.05;
    halo.quantities["halo_mass_msun"] = EvolQuantity{1.0e12, EvolProvenance::SIMULATED_DATA};
    halo.quantities["concentration"] = EvolQuantity{5.0, EvolProvenance::SIMULATED_DATA};

    out.halo_class_counts = {0, 0, 0, 0, 0, 0, 0};
    const float fc[3] = {0.55f, 1.00f, 0.90f}; // LOOM cyan
    batch_begin(out.filaments, Vec3d{0,0,0}, fc, U13_THEORETICAL, "COSMIC WEB FILAMENTS");

    const int N = 128;
    const double base_r = 26.0;
    double prev[3] = {0, 0, 0};
    bool have_prev = false;
    for (int i = 0; i < N; ++i) {
        const double ga = 2.3999632297286533;
        const double r = base_r + (i % 8) * 3.0;
        const double ang = i * ga;
        const double x = r * std::cos(ang) * unit_scale;
        const double y = r * std::sin(ang) * unit_scale;
        const double z = ((i % 16) * 1.9 - 15.0) * unit_scale;
        out.halo_units.push_back({(float)x, (float)y, (float)z});
        if (i % 19 == 0) {
            const double t = 0.05 + (13.75 * i) / (N - 1);
            EvolState h2;
            if (t > halo.cosmic_time_gyr &&
                evol_make_dark_matter_halo_step(halo, t - halo.cosmic_time_gyr,
                                                halo.model_id, 0.02, h2) == EvolErr::OK) {
                halo = h2;
            }
            const double mass = halo.quantities["halo_mass_msun"].value;
            int bin = 0;
            if (mass < 2e12) bin = 1; else if (mass < 5e12) bin = 2;
            else if (mass < 1e13) bin = 3; else bin = 4;
            out.halo_class_counts[(size_t)bin] += 1;
        }
        if (have_prev && (i % 3 == 0 || (i % 16) > 12) &&
            out.filaments.pts.size() / 3 < GALA_VERTEX_CAP - 4) {
            const double cur[3] = {x, y, z};
            push_seg(out.filaments, prev, cur);
        }
        prev[0] = x; prev[1] = y; prev[2] = z;
        have_prev = true;
    }
    out.valid = !out.halo_units.empty();
    out.t_build_s = 0.0;
    return out.valid;
}

// ---------------------------------------------------------------------------
// 5) Frame draw-plan
// ---------------------------------------------------------------------------
size_t u13_draw_plan(const std::vector<const SegmentBatch*>& batches,
                     size_t capacity_verts, std::vector<U13DrawItem>& out) {
    out.clear();
    size_t offset = 0;
    for (const SegmentBatch* b : batches) {
        if (!b) continue;
        const size_t nv = b->pts.size() / 3;
        if (nv == 0) continue;                    // empty batch: no draw recorded
        if (offset + nv > capacity_verts) break;  // HARD RULE: never a partial upload
        out.push_back(U13DrawItem{b, (unsigned)offset, (unsigned)nv});
        offset += nv;
    }
    return out.size();
}

} // namespace astra::app
