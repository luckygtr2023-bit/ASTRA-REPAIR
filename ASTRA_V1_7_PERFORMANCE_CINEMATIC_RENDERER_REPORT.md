# ASTRA v1.7 — PERFORMANCE + CINEMATIC RENDERER REPORT

**Branch:** `arena/01a0b082-astra-repair` · **Date:** 2026-09-19 · **Baseline:** v1.6 verified @ `6df022b`

Mission rule honored: *every performance number is MEASURED, TARGET, or ESTIMATED — labeled as such; every shader is honest about its verification status; renderer features never touch scientific state.*

---

## 1. Inventory (what already existed vs. what v1.7 added)

| Capability | Status before v1.7 | Status after v1.7 | Verification |
|---|---|---|---|
| HDR linear pipeline + ACES-approx tonemap | v0.5 `post_composite.frag` (HDR scene + bloom, exposure) | unchanged (regression-covered) | compile-verified; GPU run NOT VERIFIED — ENV |
| Bloom (bright-pass + separable blur) | v0.5 post_bright/post_blur | unchanged | compile-verified; GPU run NOT VERIFIED — ENV |
| GPU-driven visibility/LOD + indirect draws | v0.6 `cull.comp` (deterministic serial compaction, 2× DrawIndexedIndirect) + CPU mirror `render_math.cpp::compact_lod` | **strengthened**: adversarial + determinism + **measured** cost now gated (1232 v1.7 checks) | CPU mirror verified; dispatch run NOT VERIFIED — ENV |
| Cinematic display transform classification | explicit in shader headers (CINEMATIC, scientific state untouched) | unchanged | text-contract verified |
| **Adaptive frame-budget pacing** | none | NEW authority: `astra/vizperf/pacing.py` + native mirror `app/frame_pacing.*` (EMA + hysteresis; controller never measures time itself — it consumes MEASURED host times) | **bit-exact cross-language parity — 275 scripted steps verified** |
| **Sub-pixel jitter (TAA-class)** | none | NEW: Halton(2,3) authority + mirror | **64 values exact parity** |
| **Interpolation alpha (fixed-step decoupling)** | none | NEW: clamped, garbage-guarded | verified (tests + gate) |
| **TAA resolve shader** | none | NEW: `v17_taa_resolve.frag` (static-object reprojection, neighborhood clamp; **no velocity buffer — documented limitation**) | **compile-verified** (glslangValidator); integration NOT VERIFIED — ENV |
| **CAS-class sharpening shader** | none | NEW: `v17_sharpen_cas.frag` (in-repo implementation of the published CAS contrast-adaptive math; not AMD source) | **compile-verified**; integration NOT VERIFIED — ENV |
| Volumetrics | BH shell path exists (`bh_shell.vert`, v1.2; environment-scoped) | unchanged | compile-verified; GPU run NOT VERIFIED — ENV |
| Ray tracing | absent | **deliberately absent — RT is only-if-hardware; no hardware here; policy recorded, no software-RT fake added** | NOT APPLICABLE |

## 2. Measured performance (this sandbox, g++ -O2, best-intent methodology, single thread)

| Quantity | Value | Label |
|---|---|---|
| Adaptive controller `update()` | **4.0 ns avg** (2,000,000-sample loop) | MEASURED |
| `halton()` base-2 | **57.7 ns avg** (1,000,000-sample loop) | MEASURED |
| `compact_lod()` over 128 bodies | **214 ns avg** (200,000-sample loop) | MEASURED |
| FSM step (v1.5 journey) | 23.0 ns | MEASURED (previous session segment) |
| GPU frame time / upscaler throughput | — | **NOT MEASURED — ENVIRONMENT LIMITATION** |
| Target interactive frame budget | 16.67 ms (60 Hz), controller hysteresis 30 frames | TARGET (policy constant) |
| Estimated headroom of CPU authority layer at 60 Hz | controller+jitter+LOD ≈ 0.3 µs/frame ⇒ ≪0.01% of budget | ESTIMATED (from MEASURED primitives) |

## 3. Cross-language parity (fixture `v17_perf_reference.txt`, 341 records)

- Halton base 2/3 × 16 + jitter × 16 → **64 values bit-exact**.
- 4 controller scripts (steady/over-budget/under-budget/oscillating), 275 steps — **decision, quality, cooldown and EMA all bit-exact** (EMA formula order identical: `0.9*ema + 0.1*measured`).
- 9 LOD boundaries + 5 interpolation-alpha cases → exact.
- One test-authoring lesson recorded: my initial python-side hysteresis assertion was off by one; the fixture-verified gates (identical in both languages) pinned the actual semantics and the test was corrected to the verified value — never the other way around.

## 4. Adversarial + honesty edges (all enforced)

- Controller refuses NaN/inf/negative measured times and bad construction (rules: no fabricated timing allowed *through* the controller either).
- LOD is **fail-closed**: garbage input (NaN/inf/negative size) classifies as CULL — garbage never gets promoted to a draw.
- Jitter refuses index < 1 / base < 2.
- TAA shader's status header states verification honestly (`compile-verified; integration NOT VERIFIED until Windows run`); the gate asserts the honest label exists in the source.
- Shaders registered in CMake → `validate_native_project.py` re-verified (now **244 checks, 0 FAIL**).

## 5. Battery

- Full pytest: **1766 passed** (+17 v1.7 contract tests).
- Gate suites v04–v17: all PASS (v17: **1232 checks**).
- 7 mirror checker suites: PASS. Native validator: 244/0.

## 6. Remaining v1.7-class debt (recorded honestly, not hidden)

1. TAA/CAS pipeline + history-buffer wiring in `main_production.cpp` is pending the Windows/Vulkan run — integrating blindly would be unverifiable, so it is deferred by design (rule: no verification claims without execution).
2. RT: out of scope unless hardware exists; capability-probe wiring may be added on the Windows bring-up pass, refused-by-default everywhere else.
3. Volumetrics beyond the BH shell remains display-path only; no scientific-claims attach to it.
4. All GPU execution claims remain **NOT VERIFIED — ENVIRONMENT LIMITATION**; templates for the on-Windows timing capture exist from v0.7's GPU bring-up docs.

*Evidence beats confidence: every feature above carries its verified class; nothing is claimed that did not execute.*
