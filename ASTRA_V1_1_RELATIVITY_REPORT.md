# ASTRA COSMOS — v1.1 RELATIVITY ENGINE BINDING + SCIENTIFIC FIDELITY

## 1. Executive summary

The Python scientific authority `astra.relativity` was audited exhaustively and
mirrored natively (strict SI, double precision, identical IEEE-754 op chains).
Cross-language fidelity was proven NOT with a uniform loose tolerance but with
**exact bit equality demanded for every numeric primitive** (714 comparisons,
550 numeric — all bit-exact, max abs/rel error 0.000e+00; 164 error-domain
comparisons matching the authority's exception taxonomy exactly). One real
authority-domain anomaly was found, root-caused, Python-confirmed, mirrored
faithfully (never hidden), and gated. HUD gained two PHYSICALLY-MODELED rows
(SR γ−1, weak-field GRAV dilation−1) with strict NOT AVAILABLE semantics.
Gravity remains Newtonian N-body/Kepler — no relativistic corrections were
silently introduced anywhere (mission Phase 8 preserved).

## 2. Baseline commit

`9d7b2913b02c46abe2ec3d63fb3bd3c6f5b6f688` (v1.0). A mid-turn sandbox
re-provision rewound local HEAD to `89f98382`; recovered via annotated-fetch +
hard reset to the remote (zero loss); toolchain (venv, glslang 16.6.0, Vulkan
headers+loader 1.4.362, Win32 stub) rebuilt **to identical versions**.

## 3. Python authority inspected

`astra/relativity/`: `core.py` (170 LOC), `four_vectors.py` (118),
`lorentz.py` (44), `gr_foundations.py` (65), `models.py` (25),
`exceptions.py` (57), plus the full pytest suite (test_relativity_special /
_four_vectors / _gr / _adversarial, 461 LOC) and spacetime test modules.

## 4. Relativity capabilities found (the ONLY ones mirrored)

| Primitive | Authority | Units/convention |
|---|---|---|
| c = 299792458 | exact SI | m/s |
| speed / beta = v/c | core.py | NaN/Inf rejected; sign tolerated (b² paths) |
| lorentz_factor | core.py | series branch b<1e-5: 1+½b²+⅜b⁴; closed 1/√(1−b²); v≥c → LightSpeedViolation |
| gamma−1 (series, cancellation-free) | core.py | same domain rules |
| relativistic mass γ·m0 | core.py | m0<0/nonfinite → InvalidRestMassError; m0=0 ok |
| total energy γm0c² | core.py | — |
| kinetic energy (γ−1)m0c² | core.py | converges to ½mv² (gated) |
| momentum γm0v (Vector3) | core.py | — |
| FourVector(t,x,y,z), invariant −t²+x²+y²+z² | four_vectors.py | (−,+,+,+), t in metres of light (ct) |
| interval_type tol=1e-9 | four_vectors.py | TIMELIKE/SPACELIKE/NULL |
| SpacetimeEvent ct convention | four_vectors.py | t = time_s · c |
| proper_time_to | four_vectors.py | spacelike → error; null → 0 |
| boost_x / inverse_boost_x | lorentz.py | passive X-axis ONLY (3D boosts explicitly absent by design) |
| schwarzschild_radius 2GM/c² | gr_foundations.py | m<0 → DegenerateMetricError |
| weak_field_time_dilation 1/√(1−rs/r) | gr_foundations.py | exterior only; r≤rs → error |
| RelativityModel enum | models.py | CLASSICAL/SR/WEAK_FIELD/GR |

NOT in authority (therefore NOT implemented): Doppler shift, velocity
composition, length contraction, observer transforms, arbitrary 3D boosts,
rotating-charged metrics, BH interiors, full GR. The pre-existing
`extreme/relativistic.{h,cpp}` stubs (doppler_g etc.) were left untouched —
they are NOT part of the production path and are NOT the mirrored authority.

## 5. Native mirror

`native_renderer/src/app/relativity_sim.{h,cpp}` (~350 LOC): every authority
function with the identical float operation order. Error taxonomy mapped to an
explicit `RelErr` enum + `rel_err_name` (InvalidVelocity / LightSpeedViolation /
InvalidRestMass / SpacelikeInterval / DegenerateMetric). Pure functions; zero
allocation; header documents authority provenance per block.

## 6. Mathematical definitions

Inline in `relativity_sim.h` (every formula + branch thresholds + citation of
the authority file/line of intent). Constant G reused from
`astra.physics.constants` via the mirror (6.67430e-11 — single source of truth
shared with the N-body mirror).

## 7. Unit conventions

Strict SI (m, s, kg, J, m/s); four-vector t component = metres of light (ct);
km↔m conversion confined to the HUD fill site (×/÷1000 at the boundary, same
policy as the N-body binding).

## 8. Cross-language fidelity

`scripts/gen_relativity_reference.py` → 570 rows / `/tmp` CSV domain:
14+2 speeds (incl. v=0, 1e-12 m/s, 0.9999999c, negative-scalar quirk inputs),
v=c/1.1c/NaN/Inf boundary, 6+3 masses, 6 momentum vectors × 2 masses, 7
four-vectors, 5 event pairs, 24 boost cases, 9 rs cases, 10 weak-field cases.
`tests/relativity_mirror_check.cpp` → **714 comparisons: 550 numeric ALL
BIT-EXACT (max abs err 0.000e+00, max rel err 0.000e+00), 164 error rows with
exactly matching exception names.** Tolerance policy (mission Phase 3): exact
bit equality is required everywhere because the operation chains are
identical — and that requirement passed, so no softer band was ever used.

## 9. Edge-case results (v10_gates, 69 checks, PASS)

v=0 (γ==1 exactly), Taylor-branch term-exactness, γ(0.8c)=5/3 to 1e-15,
v=c / v=1.0000000001c rejection, NaN/Inf rejection, γ−1 series arithmetic,
KE classical convergence + explicit relativistic-corr verification at 1e4 m/s
(0.5b² ≈ 7.79e-9), m0=0/negative/NaN taxonomy, momentum signs/0.6c value
(γ=1.25), NULL tolerance band, 1s-at-rest proper time ≈1s to 1e-15, null
proper time == 0.0, spacelike → error, v=0 boost identity, invariant
preservation ±1e-14, boost/inverse roundtrip ±1e-14, boost sign symmetry,
rs(Sun) ∈ (2950, 2957) m (DATA-DERIVED range), r≤rs/r≤0/m<0 rejections,
far-field 1+rs/2r first-order check, near-horizon finite-large, rate∈(0,1]
with rate·γ=1±1e-14, strict 5× bitwise determinism, HUD rows
presence/absence/classification matrix.

## 10. Temporal integration

No change to the existing temporal stack: SIM TIME (sim seconds), OBSERVER
TIME/LIGHT DELAY (HUD), COORDINATE TIME (sim physics) and PROPER TIME are kept
strictly separate. The new HUD rows are *derived views* only — `SR GAMMA-1`
(Lorentz factor of the selected body's heliocentric speed minus one), `GRAV
DIL-1` (weak-field dt/dτ−1 at the body's heliocentric distance for the Sun's
mass). Selecting the Sun yields SR valid / GRAV NOT AVAILABLE (r=0 degenerate
metric — exact authority semantics, not a fallback value).

## 11. RenderState integration

Deliberately NOT extended. Justification: relativity introduces **no new
authoritative state** — every quantity is a pure function of existing sim
truth (speed, heliocentric distance, masses). It reaches the UI through the
same HudSnapshot inspector binding used for all derived values; adding floats
to `scene::RenderState` would duplicate truth and violate the "renderer
consumes, never redefines" rule. The renderer and GPU path are untouched.

## 12. HUD integration

Two selection rows, labels/units/classifications:
- `SR GAMMA-1: %.3e — PHYSICALLY-MODELED (SR Lorentz)`
- `GRAV DIL-1: %.3e — PHYSICALLY-MODELED (weak-field Schwarzschild exterior)`
NOT AVAILABLE variants in every unavailable configuration (gated). Example
honest values (derivable, not fabricated): Earth — SR γ−1 ≈ 4.95e-13,
GRAV DIL-1 ≈ 9.87e-9.

## 13. Persistence changes

**None (justified).** Relativity is a derived view of state already persisted;
the mission forbids format changes "unnecessarily". v0.8/v0.9 formats remain
byte-compatible; v04 persist gates confirm.

## 14. Tests

New: `v10_gates.cpp` (69 checks), `relativity_mirror_check.cpp` (714
comparisons), `scripts/gen_relativity_reference.py`. Rebuilt + re-run the
COMPLETE existing battery with zero regressions (counts below).

## 15. Performance (CPU-only, this sandbox, steady_clock, GCC -O2, 2×10⁷ calls)

| Primitive | Measured |
|---|---|
| lorentz_factor | 4.4 ns/call (~2.26e8 calls/s) |
| weak_field_time_dilation | 5.9 ns/call |
Per frame at 60 fps the HUD evaluation costs nanoseconds — negligible (this is
an arithmetic consequence of the measurement, labeled as such). NO GPU
performance claims.

## 16–18. Bugs discovered / root causes / fixes

1. **Convergence-gate first failure (test-metric bug):** "p classical x" used
   a 1e-13 tolerance at v=1e4 m/s where the true relativistic correction is
   7.79e-9. Root cause: my metric, not the engine. Fix: classical check moved
   to v=10 m/s (γ−1=5.6e-16) plus an explicit correction-value check at 1e4.
2. **AUTHORITY DOMAIN ANOMALY (real finding):** authoritative
   `lorentz_factor(-0.9c)` returns 1.65103749999999971e+00 — the low-β branch
   condition `b < 1e-5` is true for ALL negative β, so negative scalar speeds
   silently take the *series approximation* (physically wrong vs the closed
   form 2.294…). Python-verified against astra.relativity itself. Handling:
   **the mirror reproduces the authority bit-for-bit (no hidden |v| "fix" —
   that would fabricate a deviation)**, negative inputs marked in the
   generator, the quirk is gated explicitly in v10_gates (series-branch
   arithmetic), and the recommended upstream fix (reject negatives or use
   fabs inside speed()/threshold test) goes to a future phase — NOT done
   silently here (mission Phase 14).
3. **Generator schema slip (reference-generation bug):** interval rows needed
   string payloads; fixed in the emit helper; engine untouched.

## 19. Existing regressions

**None.** Full battery after all edits: v04 PASS · v05 2984/2984 · v06 645/645 ·
v07 109/109 · v08 28/28 · v09 28/28 · **v10 69/69 NEW** · kepler mirror PASS
(3.55e-13) · nbody mirror PASS (43 checks, 4.606e-13) · **relativity mirror
714 comparisons, 0 fails, all numeric bit-exact** · pytest 1535/1535 ·
shaders 13/13 SPV + validator 19/0 · native build 0 errors
(`relativity_sim.cpp.o` linked) · main_production.cpp syntax 0 errors vs
Vulkan 1.4.362 + Win32 stub.

## 20. Windows/GPU status

UNCHANGED — this is a Linux sandbox: **WINDOWS VERIFIED = NO, GPU VERIFIED =
NO, APP RUNTIME VERIFIED = NO, VISUAL VERIFIED = NO, PERFORMANCE VERIFIED =
sandbox-CPU-only.** The native relativity binding does not promote any of
these; `ASTRA_WINDOWS_GPU_BRINGUP.md` remains the promotion path.

## 21. Remaining limitations

- X-axis boosts only (authority limitation, by design).
- Negative scalar speeds follow the authority's series-branch quirk (documented,
  upstream fix deferred; production passes magnitudes only).
- Weak-field exterior only; no interiors, no full GR (authority explicit).
- Relativity quantities are HUD/inspector views; no renderer/GPU consumption
  yet (would need a visual phase on real hardware for any VISUAL claim).
- Phase 9 (black-hole prep): interfaces exist (`schwarzschild_radius`,
  `weak_field_time_dilation` ready to be consumed by the future BH phase);
  no BH system implemented here, per scope.

## 22. Recommended next phase

1. **Upstream authority fix** for the negative-β Taylor-branch quirk
   (`speed()` to use |v| or reject negatives), with astra pytest adjustments +
   re-gated mirror (small, surgical).
2. Schwarzschild-exterior operations layer (periapsis advance of Mercury,
   coordinate/proper-time budgets) as the next roadmap #13 slice — only for
   functions the authority implements.
3. Windows/GPU machine run for W1–W15 promotion + any VISUAL relativity claim.

## Status vocabulary (final)

| Category | Verdict |
|---|---|
| SOURCE VERIFIED | YES |
| STATIC VERIFIED | YES |
| BUILD VERIFIED | YES |
| CPU-VERIFIED | YES (relativity mirror bit-exact + 69 semantic gates) |
| WINDOWS VERIFIED | NO |
| GPU VERIFIED | NO |
| RUNTIME VERIFIED (app) | NO |
| VISUAL VERIFIED | NO |
| PERFORMANCE VERIFIED | sandbox-CPU primitives only (measured, labeled) |
| BLOCKED | Windows/GPU (environment; unchanged) |
