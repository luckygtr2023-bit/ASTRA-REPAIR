# ASTRA COSMOS — v0.8 INCREMENT REPORT: NATIVE N-BODY ENGINE (mirror of `astra.nbody`)

## Status vocabulary (exact)

| Category | Verdict | Evidence |
|----------|---------|----------|
| SOURCE VERIFIED | YES — nbody_sim / binding / wiring | this report + gates below |
| STATIC VERIFIED | YES (gates + shaders unchanged paths) | battery output |
| BUILD VERIFIED | YES — `nbody_sim.cpp.o` linked into `astra_renderer`; main syntax 0 errors vs Vulkan 1.4.362 | build logs |
| WINDOWS VERIFIED | **NOT VERIFIED** (no Windows environment) | unchanged policy |
| GPU VERIFIED | **NOT VERIFIED** (no GPU) | unchanged policy |
| RUNTIME VERIFIED | **NOT VERIFIED here** (engine paths execute only in gates, which DO execute the physics) | physics itself CPU-VERIFIED below |
| VISUAL VERIFIED | **NOT VERIFIED** (needs GPU run, W-gates) | unchanged policy |
| PERFORMANCE VERIFIED | **NOT VERIFIED** (no timing claims made) | — |

The physics engine itself IS CPU-VERIFIED: the gates execute real
integrations and compare against the Python authority. Only the *application
context* (window, GPU, Windows) is unverifiable in this environment.

## What v0.8 delivers (roadmap item #13, partial: N-body only)

1. **`app/nbody_sim.{h,cpp}`** — native mirror of the Python authority
   `astra.nbody`, strict SI (m, m/s, kg): pairwise Plummer-softened Newtonian
   gravity (same op order/source comments as `astra/nbody/gravity.py`),
   velocity Verlet (mirror of `integration.py`), diagnostics (KE, U, E,
   linear/angular momentum). Constants copied from `astra/physics/constants.py`
   (G = 6.67430e-11 SI, softening 1e-6 m). Deterministic: fixed i<j pair order,
   no RNG, no wall clock.
2. **`app/celestial_sim` binding** — `GravityModel{KEPLER(default),NBODY}`,
   `to_nbody_state` (Kepler-ephemeris-seeded heliocentric SI states; same
   scientific truth feeds both models), `NBodyEngine` adapter: fixed-policy
   dt = 3600 s, forward-time-only contract (backward time is NEVER silently
   faked — explicit re-anchor), energy-drift bookkeeping.
3. **Cross-language fidelity gate** — `scripts/gen_nbody_reference.py`
   (authority generates trajectories) vs `tests/nbody_mirror_check.cpp`
   (native engine): **43/43 checks, worst relative error 4.606e-13 over
   180 sim-days; measured engine energy drift 1.204e-10**.
4. **`tests/v07_gates.cpp`** — 109 checks: vector semantics, inverse-square,
   third-law antisymmetry, softened coincidence (no singularity), explicit
   zero-softening singularity reporting, dt validation, bit-identical
   determinism (fresh + warm cache), time-reversal return, angular momentum /
   momentum / symplectic-energy bounds, **2nd-order convergence measured:
   ratio = 4.00**, seed==Kepler at t0 (strict), forward-only rejection,
   reseed determinism, partial-step landing equality, 1-year drift
   (-2.9e-13), name taxonomy, NBODY↔KEPLER proximity (<1% at 30 days,
   measured per body), persist format (write/roundtrip/pre-v0.8 backward
   compatibility/unknown-value rejection/unknown-key rejection), HUD row
   binding with policy disclosure ("dt=3600s").
5. **persist** — optional field `gravity_model` (16th key): pre-v0.8 files
   load with KEPLER default; unknown values rejected as tamper. Backward
   compatible; strictness preserved.
6. **HUD** — authoritative status row `GRAVITY: KEPLER two-body` /
   `NBODY velocity-Verlet dt=3600s`, classified SIMULATED.
7. **Production wiring** (`main_production.cpp`) — `gravity_refresh_world()`
   dispatch; VK_BACK/F5 reset the engine explicitly; F2 saves the model;
   F3 applies it via the sim API and re-anchors; startup uses the same path.
   Default remains KEPLER: **zero behavioural change for existing scenarios.**

## Bug hunt this phase (root-caused, recorded, fixed correctly)

*Probe file `/tmp/probe_conv.cpp` was an out-of-tree experiment; conclusions
below are in the committed test comments.*

1. **Time-reversal metric degenerated**: relative-to-own-position scale is ∞
   for the Sun (reference position exactly 0), while Jupiter correctly pulls
   it. Fixed the *metric* (system-scale 1e11 m), engine was right.
2. **Convergence gate measured the wrong thing**: (a) infinite-M period
   estimate instead of reduced-μ `G(M+m)`; (b) last-full-step landing left a
   dt-dependent residual; (c) **decisive finding**: zero barycentric momentum
   in the synthetic case dragged the measured body 2π·m/(M+m) = 3.1416e-6 rad
   per orbit — a dt-INDEPENDENT floor that masqueraded as phase error and, at
   dt=3600, accidentally cancelled it producing a suspicious "lucky" error.
   Fix: barycentric primary velocity + relative-coordinate metric + exact
   landing → **clean textbook ratio 4.00**. No assertion was weakened: the
   test now measures what it claims to measure (the opposite of rule-16
   violations).

## Battery (all executed this phase, all green)

v04 PASS · v05 2984/2984 · v06 645/645 · kepler mirror PASS (3.55e-13) ·
**nbody mirror PASS (43 checks, 4.606e-13)** · **v07 PASS (109/109)** ·
pytest 1535/1535 · shader validator 19/0 · shaders 13/13 SPV ·
native build PASS (nbody_sim.o linked) · main_production.cpp syntax 0 errors
vs Vulkan 1.4.362 headers.

## Explicitly NOT done (honesty ledger)

- No runtime/Windows/GPU claim for the NBODY path (only gates executed).
- No GPU timestamps/X/Y (v0.7a W-gates remain the promotion path).
- Real star catalog: still DATA NOT AVAILABLE.
- NBODY mode applies to the 10-body solar scene only; no UI key toggles the
  physics mid-flight (model selection via scenario load = sim API);
  backward-time re-anchoring is a documented, loud, honest limitation.
- No relativistic terms: classification stays "SIMULATED Newtonian".
