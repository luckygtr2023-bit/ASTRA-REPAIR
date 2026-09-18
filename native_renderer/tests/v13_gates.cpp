// ASTRA v1.3 universe-mirror semantic gates.
// Hand-pin a closed-form expectation from the authority model of EACH v1.3
// domain (destruction, evolution+structure, observation/temporal) AND
// re-derive it natively. No external Python CSV reference is required; every
// expectation is written here with its equation. Mirror-checker CSVs remain
// the exhaustive (bit-exact) fidelity proof; these gates catch regressions
// even without regenerating them.
#include "app/destruction_sim.h"
#include "app/evolution_sim.h"
#include "app/observation_sim.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace astra::app;

static int g_pass = 0, g_fail = 0;
static void gate_num(double ref, double got, const char* name) {
    const bool ok = (ref == got && std::signbit(ref) == std::signbit(got)) ||
        (std::isfinite(ref) && std::isfinite(got) &&
         std::fabs(ref - got) <= 1e-9 * std::fmax(1.0, std::fabs(ref)));
    if (ok) { ++g_pass; std::printf("  %-58s OK        ref=%.17g got=%.17g\n", name, ref, got); }
    else    { ++g_fail; std::printf("  %-58s **FAIL**  ref=%.17g got=%.17g\n", name, ref, got); }
}
static void gate_bool(bool ok, const char* name) {
    if (ok) { ++g_pass; std::printf("  %-58s OK\n", name); }
    else    { ++g_fail; std::printf("  %-58s **FAIL**\n", name); }
}

static DestructionImpactEvent make_event(double im, double tm, double ix, double ivx,
                                         double tsim = 600.0) {
    DestructionImpactEvent e;
    e.impact_id = "imp:v13"; e.impactor_id = "impactor:v13"; e.target_id = "target:v13";
    e.sim_time_s = tsim;
    e.impactor_mass_kg = im; e.target_mass_kg = tm;
    e.impactor_position = DestVec3{ix, 0.0, 0.0};
    e.impactor_velocity = DestVec3{ivx, 0.0, 0.0};
    return e;
}

static void gates_destruction() {
    std::printf("== destruction & impact ==\n");
    const DestructionConfig cfg;
    DestructionImpactEvent e = make_event(1.0e14, 1.0e15, -1.0e4, 3.0e5);
    const double MRED2 = (1.0e14 * 1.0e15) / (1.0e14 + 1.0e15);

    gate_bool(dest_validate_impact(e, cfg) == DestErr::OK, "D validate: pinned head-on event OK");
    DestructionImpactEnergy en;
    gate_bool(dest_compute_impact_energy(e, cfg, 0.5, en) == DestErr::OK, "D energy: OK");
    // energy.py: KE = 0.5 * m_red * v^2 with m_red = mi*mt/(mi+mt) = 9.09...e13 kg.
    const double MRED = (1.0e14 * 1.0e15) / (1.0e14 + 1.0e15);
    const double KE = 0.5 * MRED * 3.0e5 * 3.0e5;
    gate_num(KE, en.kinetic_energy_j, "D energy: KE = 0.5*m_red*v^2 closed form");
    // deposited = 0.5 * KE (deposited_fraction argument).
    const double DEP = KE * 0.5;
    gate_num(DEP, en.deposited_energy_j, "D energy: deposited = f*KE");
    // fragmentation = 0.3 * deposited; thermal = 0.4 * deposited (config defaults).
    gate_num(DEP * 0.3, en.fragmentation_energy_j, "D energy: fragmentation = 0.3*dep");
    gate_num(DEP * 0.4, en.thermal_energy_j, "D energy: thermal = 0.4*dep");
    // Authority partition: residual = KE - frag - therm (NOT KE - deposited).
    gate_num(KE - DEP * 0.3 - DEP * 0.4, en.residual_kinetic_energy_j,
             "D energy: residual = KE - frag - therm");

    // Validation taxonomy pins.
    // system.validate_impact: require_positive -> NumericalError for
    // mass <= 0; below-min relative speed -> ImpactValidationError.
    DestructionImpactEvent bad = e; bad.impactor_mass_kg = 0.0;
    gate_bool(dest_validate_impact(bad, cfg) == DestErr::NUMERICAL,
              "D validate: non-positive mass -> NUMERICAL (require_positive)");
    bad = e; bad.impactor_velocity = DestVec3{0.0, 0.0, 0.0};
    gate_bool(dest_validate_impact(bad, cfg) == DestErr::IMPACT_VALIDATION,
              "D validate: below-min relative speed -> IMPACT_VALIDATION");
    bad = e; bad.target_mass_kg = (double)-NAN;
    gate_bool(dest_validate_impact(bad, cfg) == DestErr::NUMERICAL,
              "D validate: NaN mass -> NUMERICAL (require_finite)");

    // Geometry: collinear approach at same height -> head-on, incidence 0.
    DestructionImpactGeometry g;
    gate_bool(dest_compute_impact_geometry(e, cfg, g) == DestErr::OK, "D geometry: OK");
    gate_num(0.0, g.incidence_angle_rad, "D geometry: head-on incidence == 0");
    gate_bool(g.is_head_on, "D geometry: head-on flag set");

    // Momentum: p_rel = m*v; transfer + residual conserve relative momentum.
    DestructionImpactMomentum mo;
    gate_bool(dest_compute_impact_momentum(e, cfg, mo) == DestErr::OK, "D momentum: OK");
    // momentum.py: p_rel = m_red * v_rel (reduced mass), transfer = 0.5*p_rel.
    gate_num(MRED2 * 3.0e5, mo.relative_momentum_kg_m_s.x, "D momentum: p = m_red*v closed form");
    gate_num(mo.relative_momentum_kg_m_s.x,
             mo.transferred_momentum_kg_m_s.x + mo.residual_momentum_kg_m_s.x,
             "D momentum: transfer + residual == p_rel");

    // execute_impact (seed-pinned determinism): ENTIRE fragment set reproducible.
    DestructionImpactResult r1, r2;
    gate_bool(dest_execute_impact(e, cfg, 777, DestructionDamageState::INTACT, 0.5, 400.0, r1)
              == DestErr::OK, "D execute: OK");
    gate_bool(dest_execute_impact(e, cfg, 777, DestructionDamageState::INTACT, 0.5, 400.0, r2)
              == DestErr::OK, "D execute: repeat engine OK");
    gate_num(KE, r1.energy.kinetic_energy_j, "D execute: energy matches direct call");
    gate_bool(r1.state_after == DestructionDamageState::DESTROYED,
              "D execute: high specific energy -> DESTROYED");
    bool det = r1.fragments.size() == r2.fragments.size() &&
               r1.ejecta.size() == r2.ejecta.size();
    for (size_t i = 0; det && i < r1.fragments.size(); ++i)
        det = r1.fragments[i].mass_kg == r2.fragments[i].mass_kg &&
              r1.fragments[i].velocity.x == r2.fragments[i].velocity.x;
    for (size_t i = 0; det && i < r1.ejecta.size(); ++i)
        det = r1.ejecta[i].mass_kg == r2.ejecta[i].mass_kg;
    gate_bool(det, "D determinism: seed 777 reproduces fragments+ejecta bit-exact");
    // Authority caps FRAGMENT count at max_fragments_per_impact; ejecta have
    // no such cap (limits.max_ejecta_per_impact is separate).
    gate_bool((int)r1.fragments.size() <= cfg.limits.max_fragments_per_impact,
              "D execute: fragment count within max_fragments cap");

    // Orbit classification closed form: r=1e5 m about M=1e15 kg, v=3e5 m/s.
    // mu = G*M = 6.6743e-11*1e15 = 6.6743e4 m^3/s^2 (G(M+m) with m==0).
    // eps = 0.5 v^2 - mu/r = 4.5e10 - 0.66743 > 0 -> HYPERBOLIC.
    DestructionFragment fr{};
    fr.position = DestVec3{100000.0, 0.0, 0.0};
    fr.velocity = DestVec3{0.0, 3.0e5, 0.0};
    DestructionOrbitClass oc; double eps_o, mu_o;
    gate_bool(dest_classify_fragment_orbit(fr, 1.0e15, DestVec3{0,0,0}, DestVec3{0,0,0}, oc,
                                           eps_o, mu_o) == DestErr::OK,
              "D orbit: classification OK");
    gate_bool(oc == DestructionOrbitClass::HYPERBOLIC, "D orbit: 3e5 m/s at 1e5 m -> HYPERBOLIC");
    // eps = 0.5*v^2 - mu/r = 2e-4 - 0.667 < 0 -> BOUND ellipse.
    DestructionFragment fr2 = fr; fr2.velocity = DestVec3{0.0, 0.02, 0.0};
    gate_bool(dest_classify_fragment_orbit(fr2, 1.0e15, DestVec3{0,0,0}, DestVec3{0,0,0}, oc,
                                           eps_o, mu_o) == DestErr::OK &&
              oc == DestructionOrbitClass::BOUND,
              "D orbit: slow fragment -> BOUND");
}

static void gates_evolution() {
    std::printf("== evolution & large-scale structure ==\n");
    const EvolConfig cfg;
    // Main-sequence lifetime closed form: 10 * M^-2.5 Gyr.
    double t_ms;
    gate_bool(evol_main_sequence_lifetime_gyr(2.0, t_ms) == EvolErr::OK, "E ms lifetime: OK");
    gate_num(10.0 * std::pow(2.0, -2.5), t_ms, "E ms lifetime: 10*M^-2.5 closed form");
    // Authority passes through outside 0.1..100 (no clamp, no error);
    // only non-finite / non-positive inputs reject.
    gate_bool(evol_main_sequence_lifetime_gyr(0.05, t_ms) == EvolErr::OK &&
              t_ms == 10.0 * std::pow(0.05, -2.5),
              "E ms lifetime: 0.05 Msun pass-through (no clamp)");
    gate_bool(evol_main_sequence_lifetime_gyr(1000.0, t_ms) == EvolErr::OK &&
              t_ms == 10.0 * std::pow(1000.0, -2.5),
              "E ms lifetime: 1000 Msun pass-through (no clamp)");
    gate_bool(evol_main_sequence_lifetime_gyr(-3.0, t_ms) == EvolErr::VALIDATION,
              "E ms lifetime: negative mass -> VALIDATION");
    gate_bool(evol_main_sequence_lifetime_gyr((double)NAN, t_ms) == EvolErr::VALIDATION,
              "E ms lifetime: NaN mass -> VALIDATION");

    // Remnant taxonomy: WD <= 8, NS <= 25, BH above (0.5 dead-branch preserved).
    EvolStellarPhase ph;
    gate_bool(evol_remnant_for_mass(2.0, ph) == EvolErr::OK && ph == EvolStellarPhase::WHITE_DWARF,
              "E remnant: 2 Msun -> WHITE_DWARF");
    gate_bool(evol_remnant_for_mass(10.0, ph) == EvolErr::OK && ph == EvolStellarPhase::NEUTRON_STAR,
              "E remnant: 10 Msun -> NEUTRON_STAR");
    gate_bool(evol_remnant_for_mass(30.0, ph) == EvolErr::OK && ph == EvolStellarPhase::BLACK_HOLE,
              "E remnant: 30 Msun -> BLACK_HOLE");

    // Timestep controller closed forms.
    EvolTimestepDecision d;
    // rate-limited: dt = base/rate = 0.01/2 = 0.005 (inside [min,max]).
    gate_bool(evol_choose_timestep(cfg.timestep, 1.0, 2.0, false, 0.0, true, 5.0, d)
              == EvolErr::OK, "E timestep: rate-2 OK");
    gate_num(0.005, d.dt_gyr, "E timestep: dt = base/rate = 0.005");
    gate_bool(d.reason == EvolTimestepReason::RATE_LIMITED, "E timestep: reason RATE_LIMITED");
    // rate 0 -> rate limiting skipped; candidate stays max (remaining=5 > 1
    // so the REMAINING_INTERVAL clamp does not bind).
    gate_bool(evol_choose_timestep(cfg.timestep, 5.0, 0.0, false, 0.0, true, 5.0, d)
              == EvolErr::OK && d.reason == EvolTimestepReason::CONFIGURED_MAX &&
              d.dt_gyr == 1.0, "E timestep: rate==0 -> max dt, CONFIGURED_MAX");
    // candidate == remaining -> final clamp binds with REMAINING_INTERVAL.
    gate_bool(evol_choose_timestep(cfg.timestep, 1.0, 0.0, false, 0.0, true, 5.0, d)
              == EvolErr::OK && d.reason == EvolTimestepReason::REMAINING_INTERVAL &&
              d.dt_gyr == 1.0, "E timestep: candidate==remaining -> REMAINING_INTERVAL");
    // NaN rate -> NUMERICAL (authority error taxonomy).
    gate_bool(evol_choose_timestep(cfg.timestep, 1.0, (double)NAN, false, 0.0, true, 5.0, d)
              == EvolErr::NUMERICAL, "E timestep: NaN rate -> NUMERICAL");
    // remaining < min_dt passes through unclamped (never clamped UP).
    gate_bool(evol_choose_timestep(cfg.timestep, 1e-9, 1.0, false, 0.0, true, 5.0, d)
              == EvolErr::OK && d.dt_gyr == 1e-9 &&
              d.reason == EvolTimestepReason::CONFIGURED_MIN,
              "E timestep: rem < min_dt returns rem (CONFIGURED_MIN)");

    // Galaxy step closed form (default params: sfr_tau=5, return 0.3, yield 0.02).
    // Authority op-order: sfr_new = sfr*exp(-dt/5);
    //   formed = sfr_new*dt*1e9*0.7; mstar_new = mstar + formed;
    //   mgas_new = max(0, gas - sfr_new*dt*1e9 + 0); BH *= (1 + 0.01*dt);
    //   dZ = yield*sfr_new*dt*1e9/mgas_new (guard >0); L *= mstar_new/mstar.
    // galaxy.py (mirror passthrough verified): keys are *_msun / *_per_yr;
    // formed uses the pre-decay sfr: formed = sfr*dt*1e9*0.7.
    EvolState gs; gs.model_id = "astra.evolution.galaxy.v1";
    gs.cosmic_time_gyr = 2.0;
    gs.quantities["sfr_msun_per_yr"] = EvolQuantity{3.0, EvolProvenance::SIMULATED_DATA};
    gs.quantities["gas_mass_msun"] = EvolQuantity{1.0e11, EvolProvenance::SIMULATED_DATA};
    gs.quantities["stellar_mass_msun"] = EvolQuantity{2.0e10, EvolProvenance::SIMULATED_DATA};
    gs.quantities["metallicity"] = EvolQuantity{0.5, EvolProvenance::SIMULATED_DATA};
    gs.quantities["central_bh_mass_msun"] = EvolQuantity{1.0e6, EvolProvenance::SIMULATED_DATA};
    gs.quantities["luminosity_Lsun"] = EvolQuantity{1.0e11, EvolProvenance::SIMULATED_DATA};
    EvolState gsout;
    gate_bool(evol_make_galaxy_step_default(gs, 1.0, "astra.evolution.galaxy.v1", gsout)
              == EvolErr::OK, "E galaxy step: OK");
    const double FORMED = 3.0 * 1.0 * 1e9 * 0.7;
    gate_num(3.0 * std::exp(-1.0 / 5.0), gsout.quantities["sfr_msun_per_yr"].value,
             "E galaxy: sfr decays exp(-dt/tau) (stored)");
    gate_num(2.0e10 + FORMED, gsout.quantities["stellar_mass_msun"].value,
             "E galaxy: formed = sfr*dt*1e9*(1-0.3) with pre-decay sfr");
    gate_num(fmax(0.0, 1.0e11 - 3.0 * 1.0 * 1e9), gsout.quantities["gas_mass_msun"].value,
             "E galaxy: gas consumed + inflow(0), clamped >= 0");
    gate_num(1.0e6 * (1.0 + 0.01 * 1.0), gsout.quantities["central_bh_mass_msun"].value,
             "E galaxy: BH toy growth *(1+0.01*dt)");
    gate_num(0.5 + 0.02 * 3.0 * 1.0 * 1e9 / (1.0e11 - 3.0e9), gsout.quantities["metallicity"].value,
             "E galaxy: dZ = yield*sfr*dt*1e9/mgas_new guard");
    gate_num(1.0e11 * ((2.0e10 + FORMED) / 2.0e10), gsout.quantities["luminosity_Lsun"].value,
             "E galaxy: L *= mstar_new/mstar");

    // Structure: cluster members incremented via int(0.5*dt) truncation; web
    // coarsens only when new_time > 10; void delta floor -1; halo mass grows.
    EvolState cs; cs.model_id = "astra.evolution.cluster.v1"; cs.cosmic_time_gyr = 1.0;
    cs.quantities["total_mass_msun"] = EvolQuantity{1.0e13, EvolProvenance::SIMULATED_DATA};
    cs.quantities["member_count"] = EvolQuantity{100.0, EvolProvenance::SIMULATED_DATA};
    EvolState csout;
    gate_bool(evol_make_cluster_step(cs, 0.5, "astra.evolution.cluster.v1", 1.0, csout)
              == EvolErr::OK, "E cluster step: OK");
    gate_num(1.0e13 * (1.0 + 1.0 * 0.5), csout.quantities["total_mass_msun"].value,
             "E cluster: mass*(1+g*dt)");
    gate_num(100.0 + (double)(int)(0.5 * 0.5), csout.quantities["member_count"].value,
             "E cluster: member_count += int(0.5*dt) (truncation)");
    EvolState csout2;
    gate_bool(evol_make_cluster_step(cs, 3.9999, "astra.evolution.cluster.v1", 1.0, csout2)
              == EvolErr::OK &&
              csout2.quantities["member_count"].value == 100.0 + (double)(int)(0.5 * 3.9999),
              "E cluster: int(0.5*3.9999)=1 truncation pin");

    EvolState ws; ws.model_id = "astra.evolution.web.v1"; ws.cosmic_time_gyr = 9.5;
    ws.quantities["filament_count"] = EvolQuantity{20.0, EvolProvenance::SIMULATED_DATA};
    ws.quantities["void_count"] = EvolQuantity{15.0, EvolProvenance::SIMULATED_DATA};
    EvolState wsout;
    gate_bool(evol_make_cosmic_web_step(ws, 1.0, "astra.evolution.web.v1", wsout) == EvolErr::OK,
              "E web step: OK");
    // Filament coarsen fires only when new_time > 10; with dt=1 at t=9.5,
    // int(0.1*1.0)=0, so the count is unchanged (truncation pin).
    gate_bool(wsout.cosmic_time_gyr > 10.0 &&
              wsout.quantities["filament_count"].value == 20.0,
              "E web: new_time>10 but int(0.1*1)=0 -> filaments unchanged");
    // Voids always accumulate: + int(0.05*dt).
    gate_num(15.0 + (double)(int)(0.05 * 1.0), wsout.quantities["void_count"].value,
             "E web: voids += int(0.05*dt) (int(0.05*1)=0)");
    // Coarsen decrement pin: dt=35 from t=9 -> new_time 44 > 10,
    // dec = int(0.1*35)=3 -> 20-3=17; voids += int(0.05*35)=1.
    EvolState ws3 = ws; ws3.cosmic_time_gyr = 9.0;
    gate_bool(evol_make_cosmic_web_step(ws3, 35.0, "astra.evolution.web.v1", wsout) == EvolErr::OK &&
              wsout.quantities["filament_count"].value == 17.0 &&
              wsout.quantities["void_count"].value == 16.0,
              "E web: dt=35 coarsen dec int(3.5)=3 -> 17, voids int(1.75)=1 -> 16");
    EvolState ws2 = ws; ws2.cosmic_time_gyr = 5.0;
    gate_bool(evol_make_cosmic_web_step(ws2, 1.0, "astra.evolution.web.v1", wsout) == EvolErr::OK &&
              wsout.quantities["filament_count"].value == 20.0,
              "E web: new_time<=10 -> filament count unchanged");
    EvolState vs; vs.model_id = "astra.evolution.void.v1"; vs.cosmic_time_gyr = 1.0;
    vs.quantities["radius_mpc"] = EvolQuantity{30.0, EvolProvenance::SIMULATED_DATA};
    vs.quantities["density_contrast"] = EvolQuantity{-0.99, EvolProvenance::SIMULATED_DATA};
    EvolState vsout;
    gate_bool(evol_make_void_step(vs, 5.0, "astra.evolution.void.v1", 0.01, vsout) == EvolErr::OK,
              "E void step: OK");
    gate_num(-1.0, vsout.quantities["density_contrast"].value,
             "E void: density_contrast floor max(-1, d-0.01dt)");
    gate_num(30.0 * (1.0 + 0.01 * 5.0), vsout.quantities["radius_mpc"].value,
             "E void: radius*(1+g*dt)");
    gate_bool(vsout.provenance == EvolProvenance::THEORETICAL,
              "E void: provenance promoted THEORETICAL (authority)");

    // Stellar one-shot: age crossing t_ms(1 Msun)=10 Gyr transitions a
    // MAIN_SEQUENCE star straight to its remnant; authority swallows
    // classify VALIDATION inside the step. exact op: age_new = age + dt.
    EvolState ss; ss.model_id = "astra.evolution.stellar.v1";
    ss.cosmic_time_gyr = 9.5; ss.phase = "MAIN_SEQUENCE";
    ss.quantities["age_gyr"] = EvolQuantity{9.5, EvolProvenance::SIMULATED_DATA};
    ss.quantities["mass_msun"] = EvolQuantity{1.0, EvolProvenance::SIMULATED_DATA};
    EvolState ssout;
    gate_bool(evol_make_stellar_step(ss, 1.0, "astra.evolution.stellar.v1", ssout)
              == EvolErr::OK, "E stellar: step OK");
    gate_num(10.5, ssout.quantities["age_gyr"].value, "E stellar: age accumulates");
    gate_bool(ssout.phase == "WHITE_DWARF" &&
              ssout.quantities.count("remnant_phase") &&
              ssout.quantities["remnant_phase"].value == 1.0,
              "E stellar: MS (1 Msun) age>10 -> WHITE_DWARF one-shot");
    // before t_ms the phase word is untouched.
    EvolState ss2 = ss; EvolState ssout2;
    gate_bool(evol_make_stellar_step(ss2, 0.25, "astra.evolution.stellar.v1", ssout2)
              == EvolErr::OK && ssout2.phase == "MAIN_SEQUENCE" &&
              !ssout2.quantities.count("remnant_phase"),
              "E stellar: below t_ms leaves phase, no remnant flag");

    // Halo step: mass*(1+g*dt), concentration*(1+0.005*dt).
    EvolState hs; hs.model_id = "astra.evolution.halo.v1"; hs.cosmic_time_gyr = 4.0;
    hs.quantities["halo_mass_msun"] = EvolQuantity{2.0e12, EvolProvenance::SIMULATED_DATA};
    hs.quantities["concentration"] = EvolQuantity{10.0, EvolProvenance::SIMULATED_DATA};
    EvolState hsout;
    gate_bool(evol_make_dark_matter_halo_step(hs, 2.0, "astra.evolution.halo.v1", 0.02, hsout)
              == EvolErr::OK, "E halo step: OK");
    gate_num(2.0e12 * (1.0 + 0.02 * 2.0), hsout.quantities["halo_mass_msun"].value,
             "E halo: mass*(1+0.02*dt)");
    gate_num(10.0 * (1.0 + 0.005 * 2.0), hsout.quantities["concentration"].value,
             "E halo: concentration*(1+0.005*dt)");

    // Epoch classifier: authority CODE checks BH before DARK (docstring says
    // otherwise — mirror follows the shipped code; validated by E-rows).
    EvolEpochBoundaries b{0.1, 0.5, 0.7, 0.01, "astra.evolution.epoch.default"};
    EvolEpoch ep;
    gate_bool(evol_classify_epoch(b, 200.0, true, 0.05, true, 0.9, true, 0.8, true, 0.005, ep)
              == EvolErr::OK && ep == EvolEpoch::BLACK_HOLE_DOMINATED,
              "E epoch: both BH+DARK conditions -> BLACK_HOLE_DOMINATED (BH first)");
    gate_bool(evol_classify_epoch(b, 200.0, true, 0.05, true, 0.9, false, 0.0, true, 0.005, ep)
              == EvolErr::OK && ep == EvolEpoch::DARK_ERA,
              "E epoch: no BH -> DARK_ERA");
    gate_bool(evol_classify_epoch(b, 200.0, true, 0.05, true, 0.9, false, 0.0, true, 0.01, ep)
              == EvolErr::OK && ep == EvolEpoch::DEGENERATE,
              "E epoch: luminous == threshold (strict <) -> DEGENERATE");
    gate_bool(evol_classify_epoch(b, 5.0, true, 0.05, true, 0.1, false, 0.0, true, 0.5, ep)
              == EvolErr::OK && ep == EvolEpoch::DECLINING_STAR_FORMATION,
              "E epoch: low SFR, few remnants -> DECLINING");
    gate_bool(evol_classify_epoch(b, (double)NAN, true, 0.5, true, 0.1, false, 0.0, false, 0.0, ep)
              == EvolErr::VALIDATION, "E epoch: NaN cosmic time -> VALIDATION");

    // Engine: taxonomy pins (authority gate, backward time, unknown model).
    EvolEngine eng;
    EvolState init; init.model_id = "astra.evolution.linear.v1"; init.cosmic_time_gyr = 0.0;
    EvolState fin;
    gate_bool(eng.evolve_object(init, -5.0, "astra.evolution.linear.v1", true, fin)
              == EvolErr::VALIDATION, "E engine: backward time -> VALIDATION");
    gate_bool(eng.evolve_object(init, 5.0, "astra.evolution.linear.v1", false, fin)
              == EvolErr::AUTHORITY, "E engine: denied authority -> AUTHORITY");
    gate_bool(eng.evolve_object(init, 5.0, "astra.evolution.unknown.v99", true, fin)
              == EvolErr::VALIDATION, "E engine: unknown model id -> VALIDATION");

    // Engine end-to-end: linear model advances exactly to `until` and records
    // initial + every step (0.05 Gyr at base_dt 0.01 -> 5 + 1 samples).
    EvolEngine eng2;
    gate_bool(eng2.evolve_object(init, 0.05, "astra.evolution.linear.v1", true, fin) == EvolErr::OK,
              "E engine: linear run OK");
    gate_bool(std::fabs(fin.cosmic_time_gyr - 0.05) < 1e-12,
              "E engine: reaches `until` (within loop eps)");
    gate_num(6.0, (double)eng2.history.size(), "E engine: history = initial + 5 steps");

    // Engine determinism: identical inputs -> identical final state.
    EvolEngine eng3;
    EvolState fin3;
    gate_bool(eng3.evolve_object(init, 0.05, "astra.evolution.linear.v1", true, fin3) == EvolErr::OK &&
              fin3.cosmic_time_gyr == fin.cosmic_time_gyr,
              "E determinism: engine replay bit-identical");
}

static void gates_observation() {
    std::printf("== observation & cosmic history ==\n");
    const double C = SPEED_OF_LIGHT;

    // lookback_time closed form: sqrt-difference-of-squares distance / c.
    const double ox[3] = {0, 0, 0};
    const double em[3] = {1e9, 0, 0};
    double lb;
    gate_bool(temp_lookback_time(ox, em, 100.0, 10.0, lb) == TempErr::OK, "O lookback: OK");
    gate_num(1e9 / C, lb, "O lookback: dist/c closed form");
    gate_bool(temp_lookback_time(ox, em, 10.0, 11.0, lb) == TempErr::INVALID_STATE,
              "O lookback: emission after observation -> INVALID_STATE");

    // observe(): stationary body at 1e9 m. Closed form t_emit = tobs - d/c.
    TempWorldline w;
    w.params = {0.0, 100.0};
    w.events = {{0.0, 1e9, 0.0, 0.0, true},
                {100.0 * C, 1e9, 0.0, 0.0, true}};
    ObservedState ovs;
    gate_bool(temp_observe(w, ox, 100.0, "cosmos-gate", ovs) == TempErr::OK, "O observe: OK");
    gate_num(100.0 - 1e9 / C, ovs.emission_time_s, "O observe: t_emit = tobs - d/c");
    gate_num(1e9 / C, ovs.lookback_time_s, "O observe: lookback = d/c");
    gate_num(1e9, ovs.emission_event.x, "O observe: emission position = body position");
    gate_num(1e9, ovs.actual_state_at_observation.x, "O observe: ACTUAL (now) state position");
    gate_bool(ovs.actual_state_at_observation.ct_m == 100.0 * C,
              "O observe: actual state at observation plane");
    // Unavailable: light has not reached observer -> HISTORY_UNAVAILABLE.
    gate_bool(temp_observe(w, ox, 1.0, "cosmos-gate", ovs) == TempErr::HISTORY_UNAVAILABLE,
              "O observe: signal not arrived -> HISTORY_UNAVAILABLE");
    gate_bool(temp_observe(w, ox, -1.0, "cosmos-gate", ovs) == TempErr::INVALID_STATE,
              "O observe: negative t_obs -> INVALID_STATE");
    TempWorldline wshort; wshort.params = {0.0};
    wshort.events = {{0.0, 1e9, 0.0, 0.0, true}};
    gate_bool(temp_observe(wshort, ox, 5.0, "cosmos-gate", ovs) == TempErr::INVALID_WORLDLINE,
              "O observe: <2 samples -> INVALID_WORLDLINE");

    // proper time along a constant-velocity segment: tau = sqrt(dt^2 - dx^2/c^2).
    TempWorldline wl;
    wl.params = {0.0, 10.0};
    wl.events = {{0.0, 0.0, 0.0, 0.0, true},
                 {10.0 * C, 6e8, 0.0, 0.0, true}};
    double pt;
    gate_bool(temp_flat_proper_time(wl, pt) == TempErr::OK, "O proper time: OK");
    gate_num(std::sqrt(100.0 - (6e8 / C) * (6e8 / C)), pt,
             "O proper time: sqrt(10^2 - (6e8/c)^2)");
    // null separation -> 0 proper time.
    TempWorldline wn;
    wn.params = {0.0, 10.0};
    wn.events = {{0.0, 0.0, 0.0, 0.0, true},
                 {10.0 * C, 10.0 * C, 0.0, 0.0, true}};
    gate_bool(temp_flat_proper_time(wn, pt) == TempErr::OK && pt == 0.0,
              "O proper time: light-like -> 0");
    // spacelike segment -> SPACELIKE taxonomy.
    TempWorldline ws2;
    ws2.params = {0.0, 1.0};
    ws2.events = {{0.0, 0.0, 0.0, 0.0, true},
                  {1.0 * C, 6.0 * C, 0.0, 0.0, true}};
    gate_bool(temp_flat_proper_time(ws2, pt) == TempErr::SPACELIKE,
              "O proper time: spacelike -> SPACELIKE");

    // dilation closed forms: t' = t*sqrt(1 - v^2/c^2); t' = t*sqrt(1 - 2GM/rc^2).
    RelVec3 vhalf{0.5 * C, 0.0, 0.0};
    double td;
    gate_bool(temp_velocity_time_dilation(100.0, vhalf, td) == TempErr::OK, "O dilation vel: OK");
    gate_num(100.0 * std::sqrt(0.75), td, "O dilation vel: t*sqrt(1-0.25) closed form");
    const double MSUN = 1.98847e30, RSUN = 6.96e8;
    gate_bool(temp_gravitational_time_dilation(86400.0, MSUN, RSUN, td) == TempErr::OK,
              "O dilation grav: OK");
    gate_num(86400.0 * std::sqrt(1.0 - 2.0 * REL_G * MSUN / (RSUN * C * C)), td,
             "O dilation grav: t*sqrt(1-2GM/rc^2) closed form");

    // causal policy on the flat metric: interval kinds drive cone region.
    StMetric flat = st_minkowski_metric();
    const double a0[4] = {0, 0, 0, 0};
    const double b_tl[4] = {5.0 * C, 0, 0, 0};
    const double b_nul[4] = {1.0 * C, 1.0 * C, 0, 0};
    const double b_sp[4] = {0.5 * C, 10.0 * C, 0, 0};
    CausalRelation rel; LightConeRegion reg; bool acc;
    gate_bool(temp_causal_relate(flat, a0, a0, 1e-9, rel) == TempErr::OK &&
              rel == CausalRelation::COINCIDENT, "O causal: identical events -> COINCIDENT");
    gate_bool(temp_causal_relate(flat, a0, b_tl, 1e-9, rel) == TempErr::OK &&
              rel == CausalRelation::A_PRECEDES_B, "O causal: timelike future -> A_PRECEDES_B");
    gate_bool(temp_light_cone_region(flat, a0, b_tl, 1e-9, reg) == TempErr::OK &&
              reg == LightConeRegion::INSIDE_FUTURE_CONE, "O causal: timelike -> FUTURE CONE");
    gate_bool(temp_light_cone_region(flat, a0, b_nul, 1e-9, reg) == TempErr::OK &&
              reg == LightConeRegion::ON_FUTURE_CONE, "O causal: null -> ON FUTURE CONE");
    gate_bool(temp_light_cone_region(flat, a0, b_sp, 1e-9, reg) == TempErr::OK &&
              reg == LightConeRegion::SPACELIKE_EXTERIOR, "O causal: spacelike -> EXTERIOR");
    gate_bool(temp_is_causally_accessible(flat, a0, b_tl, 1e-9, acc) == TempErr::OK && acc,
              "O causal: timelike future -> accessible");
    gate_bool(temp_is_causally_accessible(flat, a0, b_sp, 1e-9, acc) == TempErr::OK && !acc,
              "O causal: spacelike -> NOT accessible");

    // TemporalClock: accumulators mirror clock.py (rate scales proper time).
    TemporalClock clk;
    gate_bool(clk.init("cosmos-gate", 0.0, 0.0, 0.0) == TempErr::OK, "O clock: init OK");
    gate_bool(clk.advance(5.0) == TempErr::OK, "O clock: advance OK");
    gate_num(5.0, clk.st.simulation_time_s, "O clock: sim time accumulates");
    gate_num(5.0, clk.st.proper_time_s, "O clock: proper == coord at rate 1");
    gate_bool(clk.set_rate(0.5) == TempErr::OK && clk.advance(4.0) == TempErr::OK,
              "O clock: rate-half chain OK");
    gate_num(5.0 + 4.0, clk.st.coordinate_time_s, "O clock: coord time keeps 4 s");
    gate_num(5.0 + 2.0, clk.st.proper_time_s, "O clock: proper time accrues 4*0.5");
    gate_bool(clk.advance(-1.0) == TempErr::INVALID_STATE,
              "O clock: negative dt -> INVALID_STATE");
    gate_bool(clk.set_rate(0.0) == TempErr::INVALID_STATE,
              "O clock: zero rate -> INVALID_STATE");
}

int main() {
    gates_destruction();
    gates_evolution();
    gates_observation();
    std::printf("v13_gates: %d pass, %d fail\n", g_pass, g_fail);
    std::printf("RESULT: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
