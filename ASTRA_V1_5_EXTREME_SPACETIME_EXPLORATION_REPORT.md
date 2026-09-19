# ASTRA v1.5 — EXTREME SPACETIME + EXPLORATION REPORT

**Branch:** `arena/01a0b082-astra-repair` · **Date:** 2026-09-19 · **Baseline:** verified v1.4 @ `790418e` · **Scope:** Prompt-package v1.5, PHASES 0–28 (implementation half).

Mission rule honored: *scientific model → validated state → observer-relative state → RenderState → renderer*. General relativity delegation is upheld: renderer never decides physics; no duplicate authorities were created; nothing real is claimed about wormholes or warp drives.

---

## 1. What was built (phase map)

| Phase | Requirement | Implementation | Status |
|---|---|---|---|
| 0 | Baseline | `ASTRA_V1_5_BASELINE_REPORT.md` (git+battery+hashes) | DONE |
| 1 | Inspection | mapped theoretical/(temporal|spacetime|interaction)/renderer/HUD/events/persistence (recorded in baseline report §4) | DONE |
| 2 | Authority chain | plans (pure geometry+time) → FSM → observer snapshot → RenderState mirrors → GPU | DONE |
| 3 | Extreme spacetime surface | traversal FS; warp separation; tension with phases delegated to relativity/spacetime | DONE (model-limited, classified) |
| 4 | Wormhole | `astra/theoretical/traversal.py` — geometry vs mouths/frames/times/tidal/redshift/assumptions + FSM **IDLE→APPROACHING→ENTRY→TRANSIT→EXIT→COMPLETE | ABORTED | INVALID** | DONE |
| 5 | Warp | `astra/theoretical/warp_journey.py` — **LOCAL MOTION == 0** vs **EFFECTIVE DISPLACEMENT == geometry translation** (never "teleportation as physics") | DONE |
| 6 | Travel authority | `astra/interaction/journey.py::JourneyEngine` (fills v1.4 Null provider via DelegatingTravelService — no bypass) | DONE |
| 7 | Observer | travel drives the SCIENTIFIC observer (position/frame switch on ENTRY-chain; U-measurement observer follows journey; apparent-vs-actual separation kept) | DONE |
| 8 | Causality | departure/arrival event check: TIMELIKE (conventional) / ACAUSAL-EFFECTIVE (wormhole/warp≥c) **reported, fail-closed on misclassification** | DONE |
| 9 | Temporal exploration | built on existing astra/temporal + astra_catalog epoch view (no year-teleport; flags documented) | DONE (no new fake history) |
| 10 | Relativity | delegation to `astra.relativity.lorentz_factor`, `astra.temporal` — no formula duplication | DONE |
| 11 | Tidal model | first-order embedding estimate a=c²L/rₜ² + NOT AVAILABLE sentinel + scale guards | DONE |
| 12 | Navigation | 'B' mechanism cycle (IDLE) / 'Y' begin/abort/reset; destination = user selection (no fabricated targets); abort→INVALID/terminal rules; observer restoration on COMPLETE | DONE |
| 13–15 | Native visual integration | plan-derived marks (displacement line/mouth rings/tunnel tube/warp bubble grid/observer) → mapped SSBO → `v15_travel` additive depth-tested pipeline → draw AFTER catalog; shaders compile | DONE (raster NOT VERIFIED — env) |
| 16 | HUD | 11 classified rows + NOT AVAILABLE cells (γ, T_coordinate, T_proper, throat R, redshift, tidal, bubble R, σ, effective rate, causal status, validity) | DONE |
| 17 | Input | C/U/T preserved (verified by regression); added B, Y; mid-flight mechanism change refused | DONE |
| 18 | Persistence | canonical JSON + sha256 checksum + schema/version guards + consistency window (one-tick slack) — tamper caught on every field | DONE |
| 19 | Events | EventBus publications journey_begin/state/complete/abort/invalid with classification payload | DONE |
| 20 | Audio | `TRAVEL_*` kinds; traversal/warp **SPECULATIVE** (never physical sound), INVALID cue CINEMATIC; honesty request rules intact | DONE |
| 21 | Cross-language | C++ `app/extreme_sim.{h,cpp}` mirror + 40-record fixture; parity worst **1.33e-16** (fp-identical) — the parity layer CAUGHT a real divergence (Python EXIT-band teleport; fixed at the source) | DONE |
| 22 | Determinism | two-run bit-identical (gates + pytest) incl. engine JSON | DONE |
| 23 | Adversarial | beta≥1/NaN/inf/zero-dt/negative scale/inside-throat band/frame mismatch/bubble>half-leg/σ>1e4/vₛ>1e4c/abort-after-complete/tamper each field — all fail-closed | DONE |
| 24 | Anti-purple | gates assert draw call/descriptor capacity/marks-provenance/shader presence/CMake registration; CPU-side proof chain data→SSBO→draw wired | DONE (pixels NOT VERIFIED — env) |
| 25 | Performance | MEASURED CPU: FSM step **23.0 ns**, plan build **38.0 ns**, Alcubierre shape **28.0 ns** (best-of-N); GPU NOT MEASURED (no device) | DONE |
| 26 | Regression | **1713 pytest** (+46 new) · v04–v15 gates all PASS (v15: 103) · 7 mirror checker suites PASS · validator 242/0 | DONE |
| 27 | This report | — | DONE |
| 28 | Git | commits `0a25020` (authority+tests) → `5ed6b81` (native+renderer+gates); diffs inspected; remote verification in the final message | DONE (push step) |

## 2. Scientific content & honest boundaries

**Wormhole (Morris-Thorne).** Geometry authority `astra.theoretical.wormhole.MorrisThorneMetric` (SPECULATIVE; logs on instantiation). The plan decomposes rigidly: throat radius, mouth states (position/velocity/**one shared frame**; mismatch → refusal), approach leg, throat sub-schedule (ENTRY ¼ / TRANSIT ½ / EXIT ¼ of throat transit time), proper-time accumulation τ = Δt/(γ·eᵠ), redshift evaluation band-checked (outside guard band → refusal), tidal estimate a = c² L / rₜ² (documented as *first-order, model-limited*; never a "tidal safety guarantee"). **Energy-condition assumptions are printed on the surface** (NEC violated at every MT throat; quantum vacuum constraints NOT MODELED).

**Warp (Alcubierre).** Metric authority `astra.theoretical.warp.AlcubierreMetric` (det g = −1 exact; σ cap 10⁴). Journey separation is enforced at plan level: **local observer speed is 0** (rest at bubble center); the effective rate equals the metric shift vₛ; proper time == coordinate time (lapse unity at center) — the plan REJECTS silent divergence between the metric parameter and the journey chart rate. Causality classification flips with vₛ (≥c → `SPECULATIVE_ACAUSAL_EFFECTIVE_DISPLACEMENT`; <c → `CAUSAL_CHART`). **No warp drive is claimed to exist.**

**Causality.** Departure→arrival event ordering is evaluated and **labeled**: conventional journeys must land TIMELIKE, acausal marking of a conventional request is REFUSED; exotic mechanisms report their acausal-effective status in plain text on HUD rows and persisted snapshots. Nothing is silently promoted.

**Classifications (unchanged vocabulary):** metric math THEORETICAL · traversal & warp feasibility SPECULATIVE · conventional relativistic journey SIMULATED · journey-clock quantities DATA_DERIVED · visual mark sizes/radii **CINEMATIC** (AU-scale visibility amplification; physical parameters stay on HUD) · unavailable quantities **NOT AVAILABLE** (γ for warp shown as NOT AVAILABLE rather than a misleading "1").

## 3. Architecture (authority respected)

```
astra.theoretical.traversal / warp_journey          (plans: pure, deterministic)
        ↓ delegates (no physics duplication)
astra.relativity (gamma) · astra.temporal (times) · astra.spacetime (intervals/classes)
        ↓
astra.interaction.journey.JourneyEngine             (authoritative TravelProvider;
                                                     plugs into DelegatingTravelService)
        ↓ observer snapshot (finite-checked)
native mirror app/extreme_sim.{h,cpp}               (C++ double mirror, byte-level parity)
        ↓ per-frame SIM-clock stepping (paused ⇒ frozen journey — sim-consistent)
RenderState marks (double→float at the boundary)
        ↓ mapped SSBO + v15_travel pipeline (additive, depth-test, no depth-write)
HUD rows (classifications from mirror; never recomputed on screen)
```

## 4. Tests, determinism and the battery (exact results)

| Suite | Result |
|---|---|
| `tests/test_v15_extreme.py` | **46/46** (FSM sequence, geometry adversarial, journey lifecycle, causality fail-closed, persistence+tamper, determinism, events) |
| `native_renderer/tests/v15_gates.cpp` | **103/103** (helpers, adversarial, FSM, fixture parity 36 numeric records worst 1.33e-16, vocabulary, determinism, renderer text contract, persistence static audit, tamper) |
| Full pytest | **1713 passed, 0 failed** |
| v04–v14, v13-viz gates | 562 / 2984 / 645 / 109 / 28 / 30 / 69 / 121 / 116 / 225 / 112 — all PASS |
| 7 mirror checker suites | all PASS (nbody drift 1.204e-10) |
| `validate_native_project.py` | **242 OK / 0 FAIL** |
| Shader compile (v15_travel.vert/frag) | glslangValidator **11:16.6.0** SPV pass (+ pytest assertion) |

**the parity check paid for itself on day one:** it detected that the Python plan teleported the observer to the destination at the start of the EXIT band while the C++ plan correctly interpolated; fixed at the Python source, fixture regenerated, batteries re-green.

## 5. Performance (measured, this sandbox, single thread, g++ -O2)

- FSM step (wormhole plan): **23.0 ns best-of-100 000** → journey stepping costs ≪1 µs/frame.
- Plan build: **38.0 ns best-of-10 000**.
- Alcubierre shape evaluation: **28.0 ns best-of-100 000**.
- CPU HUD/FSM overhead at 118 Hz nominal frame: negligible; GPU timings **NOT MEASURED — ENVIRONMENT LIMITATION**.

## 6. Verification status (explicit)

- **SOURCE-VERIFIED:** authority modules, FSM, plans, mirrors, renderer/HUD wiring, shaders (compile), persistence, events, audio classification, tests, determinism, adversarial.
- **RUNTIME-VERIFIED (CPU/headless native):** binary build (astra_native, mock-Vulkan path), mirror execution, gates execution, pytest execution.
- **NOT VERIFIED — ENVIRONMENT LIMITATION:** GPU raster/draw execution of v15 overlay, Windows runtime, real-Vulkan descriptor behavior, GPU performance.
- **NOT IN SCOPE per package:** year-style time travel, fabricated historical/future astronomical data, any claim that traversable wormholes/warp drives exist.

## 7. Known limitations (recorded, not hidden)

- Morris-Thorne model: mouths treated static during transit (caller re-plans on mouth motion — documented in plan); tidal value is a first-order embedding estimate; quantum constraints (Ford–Roman) NOT MODELED.
- Alcubierre: wall observer/wall reduced to a tidal guard of the interior (no full Einstein-tensor check); proper==coordinate exact only at the bubble center (documented family property).
- Warp HUD gamma intentionally NOT AVAILABLE (lapse unity at center; showing "1.0" would imply measurement).
- Visual marks are single-screen-space billboards; arc-scale CINEMATIC amplification for AU scenes (physical parameters remain on HUD).
- Journey stepping uses the global SIM clock (warp-aware); extreme warp factors can skip *visual* phases yet NEVER skip FSM transitions (deterministic chain walk).

## 8. File map

| File | Role |
|---|---|
| `astra/theoretical/traversal.py` | TraversalPlan + WormholeTraversalFSM (v1.5 P3/P4) |
| `astra/theoretical/warp_journey.py` | WarpPlan separation (P5) |
| `astra/interaction/journey.py` | Journey/JourneyEngine persistence/events/causality (P6/8/18/19) |
| `native_renderer/src/app/extreme_sim.{h,cpp}` | C++ mirror (P21) |
| `native_renderer/src/shaders/v15_travel.{vert,frag}` | Additive overlay shaders |
| `native_renderer/src/main_production.cpp` | keys B/Y, stepping, observer integration, marks→SSBO→draw, HUD fill |
| `native_renderer/src/app/hud_state.{h,cpp}` | v1.5 fields + rows |
| `native_renderer/src/app/audio_bus.{h,cpp}` | TRAVEL_* kinds + classification |
| `native_renderer/tests/v15_gates.cpp` + `tests/fixtures/v15_measure_reference.txt` + `scripts/gen_v15_reference.py` | gate battery + fixtures |
| `tests/test_v15_extreme.py` | 46 python contract tests |
| reports: `ASTRA_V1_5_BASELINE_REPORT.md`, this file, `ASTRA_V1_5_INDEPENDENT_VERIFICATION_REPORT.md` | P0/P27/independent |

*Every number in this report is measured on this session's runs or a formulae-bound constant of the pinned authorities; every claim carries its classification; nothing is asserted as real beyond the mathematics.*
