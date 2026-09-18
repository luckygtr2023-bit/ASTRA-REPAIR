# ASTRA v1.2 — BLACK-HOLE & SPACETIME ENGINE BINDING REPORT

Date: 2026-09-18 (Asia/Calcutta). Branch: `arena/01a0b082-astra-repair`.
Base HEAD (v1.1 shipped): `e9f561179c4fc91d5eca6fb3416ccc7a7e6a28e4`.

Status vocabulary note (mandatory rules 7–11): everything below is
**SOURCE-INSPECTED**, **STATIC-ANALYSIS-VERIFIED** (gate suites), **BUILD-VERIFIED**
(GCC 12, cmake/ninja), **SHADER-COMPILE-VERIFIED** (glslangValidator 16.6.0) and
**CPU-RUNTIME-VERIFIED** (sandbox x86_64). **NOT** Windows-runtime-verified,
**NOT** GPU-runtime-verified, **NOT** Vulkan-runtime-verified, **NOT** visually
verified. No claim in this report exceeds these categories.

---

## 1. Scope (mission verbatim)

Bind the Python scientific authorities `astra.blackhole` (682 LOC) and
`astra.spacetime` (~1530 LOC) into the native renderer: mirrors ONLY, no
invented physics, no silent repairs, double precision, exact exception
behavior, bit-exact fidelity where mathematically appropriate; HUD exposure
with REAL/SIMULATED/PHYSICALLY-MODELED/… vocabulary; full regression battery;
report + commit only after validation.

## 2. Authority inventory (Phase 1, exhaustive)

Every module was read end-to-end before writing any native code:

- `astra/blackhole/` — `__init__.py`, `models.py` (BlackHoleModel;
  NUMERICAL_HORIZON_EPSILON = 1.0e-9 m ABSOLUTE; EXTREMAL_SPIN_TOLERANCE =
  1.0e-14), `exceptions.py` (BlackHoleError; mass/spin/geometry errors derive
  ValueError+BlackHoleError; CoordinateSingularityError derives BlackHoleError
  only), `parameters.py` (frozen state; validators; model = SCHWARZSCHILD iff
  spin == 0.0; r_g = (G·M)/(c·c); a = a*·r_g; to_dict/from_dict = canonical
  pair only), `schwarzschild.py` (horizon guard = r ≤ horizon + 1e-9; r_s
  DELEGATES to `relativity.gr_foundations`; ISCO = 3r_s; photon sphere = 1.5r_s;
  dilation delegates after guard; redshift with last-ulp clamps),
  `kerr.py` (_clamped_sqrt; horizons; ergosphere; equatorial ZAMO ω with the
  Δ-clamp band of tolerance·r_g²; BPT ISCO, Z1/Z2 even in a*, orbit family by
  sign of root: −1.0 prograde / +1.0 retrograde; photon orbits; equatorial
  static dilation invalid at/inside the EQUATORIAL ergosphere), `api.py`
  (facade dispatch; Kerr-with-radius-inside-equatorial-ergosphere redshift
  DELEGATES to the Schwarzschild subsystem for the exact error policy).
- `astra/spacetime/` — `exceptions.py`, `events.py` (ct convention; charts;
  origin/negative-radius rejection), `metric.py` (4 models; Schwarzschild
  analytic derivatives; DIFF_STEP = 1e-5 relative; DET_REL_TOL = 1e-14;
  scale-normalized inverse via `astra.mathematics.Matrix4` cofactor code with
  SINGULAR_TOL = 1e-12; spherical patch guards r ≤ 0, θ ∈ {0, π}),
  `connection.py`, `causality.py` (local interval = ACTIVE metric at midpoint,
  exactly the authority's FIRST-ORDER honesty for curved charts),
  `curvature.py` (Riemann sign conventions; sequential index raising for K),
  `geodesics.py` (inline RK4 WITH per-stage horizon guard band
  r ≤ horizon·(1+1e-12) and NaN/Inf → GeodesicDivergenceError; adaptive
  DOPRI5(4) via `astra.mathematics.ode.rk45_step` with the same controller),
  `api.py` (metric-correct u⁰ from g(w,w) = −c²; LightSpeedViolation boundary;
  spherical Jacobian; massless dλ = dct), plus the load-bearing dependencies
  `astra.mathematics.matrices.Matrix4/Matrix3` (exact cofactor op chains) and
  `astra.mathematics.ode` (rk4/rk45 coefficient fractions).
- Existing native touchpoints: `relativity_sim.{h,cpp}` (v1.1 mirror, reused
  for delegation), HUD (`hud_state`), text budget (`hud_text`), tests v04–v10.

## 3. Native mirrors (Phase 2–4) — files

| File | LOC | Content |
|---|---|---|
| `native_renderer/src/app/black_hole_sim.{h,cpp}` | ~430 | Complete `astra.blackhole` mirror: state, validation taxonomy (`BhErr`), Schwarzschild + Kerr subsystems, full facade. Delegates r_s and time dilation to the v1.1 relativity mirror (single implementation, exactly like the authority). |
| `native_renderer/src/app/spacetime_sim.{h,cpp}` | ~870 | Complete `astra.spacetime` mirror: events/charts; Matrix3/Matrix4 cofactor determinant+inverse verbatim; all four metric models (incl. GENERAL_NUMERICAL via `std::function` field); analytic + 4th-order central-difference derivatives; scale-normalized inverse; Christoffel + derivative; causality; full curvature chain (Riemann → Ricci → R → Einstein → Kretschmann → tidal); inline RK4 + DOPRI5(4) geodesic integrator with horizon/divergence guards; facade conversions. |
| `native_renderer/tests/v11_gates.cpp` | ~330 | 101 semantic gates (closed-form anchors, domain taxonomy, determinism, HUD). |
| `native_renderer/tests/blackhole_spacetime_mirror_check.cpp` | ~430 | Cross-language bit-fidelity checker. |
| `scripts/gen_blackhole_spacetime_reference.py` | ~390 | Reference generator executing the Python AUTHORITY (1104 rows). |

Error enums map 1:1 to authority exception classes (documented in headers):
`BhErr` — InvalidBlackHoleMassError, InvalidSpinParameterError,
InvalidGeometryInputError, CoordinateSingularityError, ValueError (from
`kerr._clamped_sqrt` only); `StErr` — InvalidCoordinateError,
DegenerateMetricError, HorizonCrossingError, GeodesicDivergenceError,
LightSpeedViolation, InvalidBlackHoleMass/Spin (metric ctors),
NotImplementedError (diagonal-metric null directions — **preserved, never
worked around or extended**).

## 4. Fidelity evidence (Phase 5)

`blackhole_spacetime_mirror_check` vs 1104 authority rows:

    blackhole_spacetime_mirror_check: 6091 comparisons, 0 fails, 6083 bit-exact
      max abs err: 0.000e+00  max rel err: 0.000e+00
    RESULT: PASS    (deterministic across ≥ 3 runs)

- 5565 numeric comparisons: **ALL bit-exact** (100%), covering ISCO BPT pow()
  chains, Kerr metric tensors, 256-component Riemann rows through
  numeric-differentiated Christoffels, Kretschmann scalars, geodesic runs
  (RK4 + adaptive RK45, incl. 200-step runs) and GENERAL_NUMERICAL fields.
- 518 error-taxonomy rows: **ALL exact** (horizon guard bands, extremal
  clamps, invalid spins/masses/radii, null-direction NotImplementedError,
  LightSpeedViolation, HorizonCrossing mid-run, argument validation).
- 8 string-class rows (model/interval names): all exact (8 of the remaining
  comparison counter).
- Geodesic rows additionally run TWICE natively → bit-identical (determinism).
- v1.1 surfaces unchanged: relativity mirror re-verified post-fix
  (714 comparisons, 0 fails), kepler mirror 99/99 PASS, nbody mirror PASS.

## 5. Authority anomalies found (mirrored faithfully, NOT repaired)

1. **KerrMetric(a* = 0) g_tph = −0.0 vs SchwarzschildMetric +0.0.**
   Value-equal (IEEE), byte-different. Verified the Python authority produces
   the same sign-bit difference (root-caused: `g_tph = -2·a·... / Sigma`
   with a = +0.0 → −0.0). Gate v11 asserts the mirror reproduces the sign
   bits exactly. No change requested upstream (documented only).
2. Kerr curvature evaluation at absurdly small radii (r ≲ h = 1e-5·max(|x|,1)
   above r_+) fails with DegenerateMetricError **in the authority itself**
   because the 4th-order stencil steps leave the coordinate patch
   (r−h ≤ 0). Mirror reproduces the identical error class (verified during
   smoke testing with M = 10 kg). Not a bug in the mirror; a documented
   truncation limit of the authority's numeric-derivative phase.

## 6. HUD exposure (Phase 6)

Selection block gains 5 rows (central-body black-hole SCALES; pure functions
of the authoritative mass — no new simulation state):

- `BH R_S (CTR)` / `BH ISCO (CTR)` / `BH PHOT SPH (CTR)` —
  PHYSICALLY-MODELED (static Schwarzschild, test particle; the rows never
  claim the Sun "is" a black hole — classification text states this).
- `BH MODEL (CTR)` = "SCHWARZSCHILD (spin=0 modeled)" — SIMULATED (engine
  model choice).
- `BH KERR SPIN` = **NOT AVAILABLE**, classification: "— (central spin not
  modeled by the N-body engine)". Honest taxonomy instead of fabricated spin.
- Flags (`has_sel_bh`) are independent: false ⇒ all five rows
  "NOT AVAILABLE"/false, never invented values.

`main_production.cpp` fills through `black_hole_sim.h` using the central
body's mass only. HUD line budget (`HullTextSpec::max_lines`) raised 22 → 27:
the HUD legitimately grew to 25 lines by design; intent of the truncation
detectors (v06 gates) unchanged — root cause, not a test weakening;
production hard cap (HUD_VERTEX_CAPACITY = 32768 vertices) untouched.

## 7. Persistence (Phase 7)

**No changes.** Every exposed value is a derived pure function of existing
authoritative state (central mass, heliocentric distance); nothing new
becomes authoritative. The authority's own BlackHoleState serialization
(mass, spin canonical pair) is mirrored in `to_dict` semantics but not wired
into profile persistence because the engine owns no black-hole state yet.
Backward compatibility of saves is therefore trivially preserved and the
tamper-validation surface is unchanged (full pytest confirms).

## 8. Full battery (Phase 8)

| Suite | Result |
|---|---|
| v04_gates | PASS |
| v05_gates | 2984/2984 |
| v06_gates | 645/645 (after §6 budget fix) |
| v07_gates | 109/109 |
| v08_gates | 28/28 |
| v09_gates | 28/28 |
| v10_gates | 69/69 |
| **v11_gates** | **101/101** |
| relativity_mirror_check | 714 comparisons, 0 fails (post-fix) |
| kepler_mirror_check | 99 comparisons, 0 fails (max_rel 3.55e-13 ≪ 1e-9) |
| nbody_mirror_check | PASS (drift 1.204e-10 over 15552000 s) |
| **bhst_mirror_check** | **6091, 0 fails, all numeric bit-exact** |
| pytest (Python authority) | **1535/1535 across all test files incl. test_bh_/test_st_suites** |
| cmake+ninja build (GCC 12, Release, Vulkan 1.4.362 headers) | 0 errors, `astra_native` linked; mirrors archived into `libastra_renderer.a` |
| Shader validation (glslangValidator 16.6.0, `-V --target-env vulkan1.3`) | production **13/0**, asset tree **35/0** (48/48 compile-verified) |
| `main_production.cpp` vs Win32 stub + Vulkan headers | 0 syntax errors |
| v11_gates rebuilt twice + bhst mirror ×3 | identical outputs (repeat determinism) |

Development-environment deviations from the Windows production target remain
exactly as v1.1: no MSVC/Win32 runtime, no GPU, no Vulkan runtime instance.
These are **BLOCKED — ENVIRONMENT LIMITATION**, not failures.

## 9. CPU performance (measured; sandbox x86_64, GCC -O2; NOT a GPU or
Windows claim)

| Hot path | Cost |
|---|---|
| `bh_kerr_isco_prograde` | 63.5 ns/call (~1.6e7/s) |
| `bh_kerr_frame_dragging_angular_velocity` | 21.3 ns/call |
| `st_metric_tensor` (Schwarzschild) | 19.0 ns/call |
| `st_christoffel_symbols` (Schwarzschild, analytic derivatives) | 549 ns/call |
| `st_kretschmann_scalar` (full numeric chain) | 17.3 µs/call |
| `st_integrate_geodesic` (RK4, Schwarzschild) | 2.29 µs/step |

## 10. Defects introduced & fixed during this phase (forensic classification)

1. `spacetime_sim.cpp` — Christoffel-derivative carrier typed 3D instead of
   4D. Code bug; caught by compiler; fixed before any gate ran.
2. `black_hole_sim.cpp` — leftover dead call in
   `bh_kerr_static_time_dilation_equatorial` (editor residue). Code hygiene;
   fixed.
3. `blackhole_spacetime_mirror_check.cpp` — massless flag read at index 8
   instead of 7 (tagged-row offset). TEST bug; caused 22 value mismatches;
   fixed → 0 fails. Not a mirror defect.
4. `v11_gates.cpp` initial failures: (a) my tolerance on weak-field dilation
   tighter than the exact value (measured 1.477e-9 — my bound 1e-13);
   (b) stale variable reuse (`rp` from the a*=0 horizon pair) in the frame-
   dragging guard test; (c) demanded byte-equality between Kerr(a=0) and
   Schwarzschild tensors — replaced by value equality PLUS an explicit
   authority-behavior sign-bit gate (§5.1); (d) vacuum Ricci/R thresholds
   tightened beyond the numeric-derivative truncation floor (authority
   measures 2.9e-11 / 3.2e-20 at that point). All four were GATE-authoring
   errors, never mirror errors; each was root-caused against the Python
   authority before changing the gate.
5. v06_gates HUD-line overflow (now 25 > 22) — root-caused to the v1.2 row
   growth; fixed at the SOURCE of the budget (hud_text default), with the
   truncation-detection semantics of the tests preserved (§6).
6. **pre-existing v1.1 latent op-divergence fixed** (surgical):
   `relvec_magnitude` computed `sqrt(x²+y²+z²)` while the authority's
   `Vector3.magnitude()` is `hypot(hypot(x,y),z)`. Value-equal at every
   tested point but not the authority's op chain; changed to the exact hypot
   chain and re-verified ALL v1.1 references (714 comps, 0 fails; every
   battery leg above re-ran green).

## 11. Out of scope (mirrors the authority's own deferrals — rule 22)

Kerr arbitrary-inclination frame dragging; Kerr-Newman; interior geometries;
Kruskal-Szekeres regular coordinates; non-diagonal light-cone tracing;
metric backreaction / numerical relativity; GR↔N-body COUPLING (the Newtonian
N-body engine remains the gravity truth; no GR corrections injected);
persistence of black-hole state (§7); GPU/Vulkan runtime verification.

## 12. Provenance & how to reproduce

```
# Reference rows from the Python authority:
PYTHONPATH=. /tmp/avenv/bin/python scripts/gen_blackhole_spacetime_reference.py > /tmp/bhst_reference.csv
# Checker:
g++ -std=c++20 -O2 -I native_renderer/src \
    native_renderer/tests/blackhole_spacetime_mirror_check.cpp \
    native_renderer/src/app/{relativity_sim,black_hole_sim,spacetime_sim}.cpp \
    -o bhst_check && ./bhst_check /tmp/bhst_reference.csv
# Gates: g++ -std=c++20 -O2 -I native_renderer/src native_renderer/tests/v11_gates.cpp \
    <mirror cpps> native_renderer/src/app/hud_state.cpp -o v11_gates && ./v11_gates
```

Toolchain (sandbox): GCC 12.3, glslangValidator 16.6.0, Vulkan headers
1.4.362, Python 3.11 venv, cmake/ninja.
**Addendum (visual phase):** the sandbox was re-provisioned mid-phase; the
rebuilt toolchain uses Vulkan headers/loader **1.4.326** (API-stable for all
used symbols; glslangValidator rebuilt 16.6.0 from source, `--version`
identical 11:16.6.0). Every re-run battery leg below used 1.4.326.

## 13. Claims that are explicitly NOT made

No Windows execution, no GPU execution, no Vulkan runtime, no
visual/screenshot evidence, no physics beyond the authority, no fabricated
spin or black-hole claims about the Sun, no modified gravity coupling,
no persistence schema changes, no weakened tests. The renderer remains a
pure visualization consumer; the Python package remains the scientific
authority, byte-for-byte matched by these mirrors at IEEE-754 level.


---

## 14. ADDENDUM — v1.2 VISUAL INTEGRATION ("ASTRA must visually show the actual simulation")

Date: 2026-09-18 (same session continuation). The mission's PRIMARY
REQUIREMENT is visual: every implemented black-hole/spacetime quantity with a
meaningful visual representation must flow authority -> native mirror ->
authoritative sim state -> render/viz state -> native renderer -> REAL GPU
draw path -> visible scene, with NO fake visualization. This addendum is the
complete record of that wire-up. **Everything the physics phase shipped is
untouched** (mirrors, gates, battery rows above all re-verified AFTER the
visual changes).

### 14.1 What became visible (in-app F4 overlay, default OFF)

| Element | Source | Authority | Transform applied | Render path | Classification |
|---|---|---|---|---|---|
| Event-horizon disc | Sun's authoritative mass (helio sim) | `astra.blackhole` schwarzschild.py -> native `bh_schwarzschild_radius_m` (6091-bit-exact mirror) | radius = 1 r_s (exact double) x CINEMATIC magnification | 97-vertex closed LINE_STRIP (ecliptic plane) via `g_pipe_bh_shell`, red | PHYSICALLY-MODELED geometry; CINEMATIC scale |
| Photon-sphere ring | same mass | native mirror `bh_photon_sphere_radius_m` | 1.5 r_s exact ratio | same pipeline, cyan | PHYSICALLY-MODELED; CINEMATIC scale |
| ISCO ring | same mass | native mirror `bh_isco_radius` (prograde = 3 r_s for the modeled SCHWARZSCHILD central body) | 3 r_s exact ratio | same pipeline, amber | PHYSICALLY-MODELED; CINEMATIC scale |
| Spacetime light rays (F4x2) | same mass | `astra.spacetime` geodesics.py -> native mirror `st_integrate_geodesic` RK4 (1000 steps/ray) + `st_cartesian_state_to_chart` massless facade | metres -> render units via the SAME scalar magnification; impact 3/3.75/4.5/6/8 r_s (> b_crit = 3 sqrt(3)/2 r_s = 2.598 r_s => escape by construction), emitter shell 25 r_s | 5 polylines x 1001 vertices, lavender | PHYSICALLY-MODELED (integration); ray SCENARIO THEORETICAL (virtual photons, not simulated bodies); CINEMATIC scale |
| Horizon time-dilation badge | same mass | native `bh_gravitational_time_dilation_schwarzschild` | dt/dtau - 1 at photon sphere = sqrt(3)-1; at ISCO = sqrt(3/2)-1 (gated vs closed forms) | HUD/inspector rows (horizon itself: dt/dtau diverges at the boundary -> row honestly NOT AVAILABLE, never a clamped fake) | PHYSICALLY-MODELED / NOT AVAILABLE-(boundary) |
| Kerr/spin structure | -- | engine models central spin 0 => only Schwarzschild exists | -- | -- | NOT AVAILABLE (not modeled by the engine), echoed in HUD |
| visual magnification itself | -- | -- | one scalar `mag = 1.5 * visual_radius(sun) / r_s(sun)` printed in the HUD | -- | CINEMATIC (documented readability transform; every ratio and value exact regardless) |

### 14.2 Pipeline & data flow (production runtime, `main_production.cpp`)

CPU (double, mirrors) -> `app/bh_viz` builders -> vertex arrays (float only at
the 12-byte vertex boundary) -> ONE persistent-mapped host-coherent vertex
buffer `g_bh_vb` (8192-vertex budget; 5296 vertices used) -> per-frame when
enabled: bind `g_pipe_bh_shell` (new shader `bh_shell.vert` + reused
`orbit.frag`; LINE_STRIP; vec3 input; depth-test ON / depth-write OFF — the
same depth discipline as the orbit overlay) -> 3 vkCmdDraw (rings; one push-
constant struct: viewProj + center = `rpos[0]` target-relative under the
floating-origin policy + per-shell color) -> +5 vkCmdDraw when rays are on ->
++`g_perf.draw_calls` per draw (renderer statistics stay truthful). Insertion
point: scene pass between apsis ticks (2e) and reference axes/selection marker
(2f); post chain (bloom/tonemap/FXAA-lite composite) applies to it like every
other scene primitive. Build is lazy and one-time (mass-change key only; a
static solar system builds exactly once, measured 11.6 ms — gate < 200 ms).

### 14.3 Failure taxonomy (graceful, never silent)

invalid central mass / mirror error / ray-integration error / vertex-budget
overflow -> console line + HUD `NOT AVAILABLE` row; structure shells remain
drawable without rays; the overlay NEVER draws fabricated geometry, and never
retries in a per-frame busy loop (mass latch BEFORE the attempt).

### 14.4 Verification results (this phase, re-run battery)

| Leg | Result |
|---|---|
| v11_gates (101 -> +20 section K "visual integration") | **121/121** |
| v04..v10 (regression, unchanged suites) | 562 / 2984 / 645 / 109 / 28 / 28 / 69 — all PASS |
| relativity / kepler / nbody / bhst mirror checks | 714/0 - 99/0 - PASS (drift 1.204e-10) - 6091/0 all-numeric-bit-exact |
| pytest (Python authority) | **1535/1535** |
| cmake Release build | 113 targets, 0 errors; `bh_shell.vert.spv` emitted with the other 13 SPVs into `<build>/shaders/` |
| shader validation (glslangValidator 16.6.0, `-V --target-env vulkan1.3`) | production **14/0**, full shader tree **140/0** |
| `main_production.cpp` vs rebuilt Win32 stub + Vulkan 1.4.326 | 0 syntax errors |
| determinism (v11 x2, bhst x2) | byte-identical outputs |
| extra physics sanity in gates | photon-sphere dilation sqrt(3)-1, ISCO sqrt(3/2)-1 (closed forms); rays escape with min radius 2.29..7.49 r_s > 1.5 r_s, start/end on the 25 r_s emitter shell, all vertices finite |

### 14.5 Per-element "purple rectangle" test (mission Phase 11) — static view

- Selection-independence: the overlay anchors to body[0] RenderState position
  every frame (`rpos[0]` recomputed from sim state) — moving the camera (the
  floating origin) moves ALL rendered primitives together; the overlay cannot
  be mistaken for a screen-space decal.
- Time-dependence: ring/ray positions are central-body-relative, and body[0]
  sits at the camera target by construction of the ecliptic sim (Sun at
  origin): the overlay demonstrates its scene membership through the SAME
  view/perspective matrix as the bodies, orbits, and vectors. Simulated time
  visibly moves the planets through this structure.
- HUD correspondence: the overlay rows print the exact magnification and row
  state that geometry was built from; toggling F4 changes geometry AND rows
  together.
- NOT runtime-verified: whether pixels land where the math says is assertable
  only on the Windows GPU target — **NOT VERIFIED** here (no Vulkan runtime in
  this sandbox; BLOCKED — ENVIRONMENT LIMITATION). No screenshots exist; none
  are claimed.

### 14.6 Deviations & defects found this phase

1. **Gate tolerance defect (classification: TEST)** — the new closure gate
   asserted bit-identity of the first/last ring vertex; sin(2*pi) yields
   -2.4e-16, not 0. Measured physical closure = 2.4e-16 units. Gate now
   asserts relative gap < 1e-15. Truncation detection semantics of other
   gates untouched; no test was weakened (the gate became physically correct,
   not permissive).
2. **Toolchain version drift (classification: ENVIRONMENT)** — sandbox
   re-provision forced a Vulkan rebuild at 1.4.326 (was 1.4.362). Recorded in
   §12. No code implication (all used API symbols are 1.0-stable).
3. **Sandbox re-provision x3 (classification: ENVIRONMENT)** — local checkout
   was restored from the server branch (authoritative) via
   `git fetch --depth=50` + `git reset --hard FETCH_HEAD`; server never lost
   commits (verified via `git ls-remote` AND `gh api`).

### 14.7 Updated NOT-made claims this phase

No Windows execution; no GPU execution; no Vulkan runtime verification; no
screenshots (none exist); no new physics (geometry-only consumer of the
shipped mirrors); no GR corrections injected into the N-body engine; no
persistence schema change; no audio stream added (overlay is display-only,
exactly like the 'V' vector toggle); no weakened tests; no fabricated visual
elements — every drawn vertex traces to an authority value through exactly one
CINEMATIC scalar.