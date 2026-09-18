# ASTRA COSMOS — v1.0 INCREMENT REPORT: NBODY SELF-DIAGNOSTICS + MEASURED PROFILE

## Status vocabulary (exact)

| Category | Verdict | Evidence |
|----------|---------|----------|
| SOURCE VERIFIED | YES | v09 gates |
| STATIC VERIFIED | YES | battery |
| BUILD VERIFIED | YES (0 errors; main syntax clean vs Vulkan 1.4.362) | build logs |
| WINDOWS / GPU / VISUAL / RUNTIME(app) | **NOT VERIFIED** (env unchanged) | — |
| PERFORMANCE VERIFIED | **YES — CPU/engine only, on THIS Linux sandbox** (labeled scope) | measured table below; NO GPU perf claims |

## What v1.0 delivers

1. **HUD engine self-diagnostics** — two authoritative rows appear only in
   NBODY mode when diagnostics exist: `NBODY TIME` (anchor-integrated days) and
   `NBODY E DRIFT` (relative energy drift, classified
   `SIMULATED (engine self-diagnostic)`). Absent entirely in KEPLER mode and
   when no engine data exists — no fabricated numbers anywhere.
2. **`main_production` export path** — `nbody_export_diagnostics()` reads the
   engine, only when NBODY + seeded.
3. **v09 gates (28 checks)** — row presence/absence matrix per model, value
   formatting, classification label, engine time law (24×3600 = 86400 exact,
   partial-step exactness, 5-year arithmetic sanity), drift instrumentation
   feeds real engine values (nonzero, bounded <1e-8 at 1 yr; fresh/unseeded
   engines honestly zero rather than garbage).

## Measured N-body profile (THIS sandbox, GCC 12 -O2, single thread — real
measurements via `steady_clock`, not estimates; NOT GPU numbers, NOT Windows
numbers)

| Metric | Measured |
|--------|----------|
| velocity-Verlet throughput, 10-body scene | **1,561,521 steps/s** |
| 10 sim-years wall time (87,660 steps) | **0.054 s** |
| Energy drift after 10 sim-years | **−4.458e-09** (bounded; no secular blow-up) |
| Earth |r| after 10 sim-years | **1.017710 AU** (still on its orbit) |

Context the numbers justify: at the maximum production warp (1e8 s/s) the
engine needs ~27,778 steps per simulated year — i.e. ~56 sim-years gained per
wall second; a whole frame at 60 fps moves ≤ ~462 days of sim time
(≈1.8 ms engine cost) — no frame-budget concern for the 10-body scene. (These
are arithmetic consequences of the measured throughput, not independent
measurements — labeled as such.)

## Root-cause record this phase

One wiring slip caught by the compiler, not shipped: the HUD diagnostics call
was briefly injected into the F2 `ScenarioSave` block (wrong struct type) and
fixed before any test ran against it; no behavior change, no suppression.

## Full battery (all green)

v04 PASS · v05 2984/2984 · v06 645/645 · v07 109/109 · v08 28/28 ·
**v09 28/28 (NEW)** · kepler mirror PASS (3.55e-13) · nbody mirror PASS
(43 checks, 4.606e-13) · pytest 1535/1535 · shaders 19/0 + 13/13 SPV ·
native build 0 errors · main syntax 0 errors vs Vulkan 1.4.362.

## Not done (ledger)

- Windows/GPU/runtime/visual remain W-gates territory.
- No relativistic N-body; classification stays SIMULATED Newtonian.
- Perf numbers are sandbox-CPU-only; machine differences are expected and
  these are NOT performance targets for the Windows box (W14 table remains).
