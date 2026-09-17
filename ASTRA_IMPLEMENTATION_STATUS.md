# ASTRA COSMOS — IMPLEMENTATION STATUS (v0.6)

## 0. v0.6 STABILIZATION increment (Step 2 continuation)

Full report: `ASTRA_V0_6_REPORT.md`. No GPU/Windows/network here — runtime
statuses stay NOT VERIFIED; source-level paths are REAL (no mocks/stubs).

| Brief item | Status | Evidence |
|------------|--------|----------|
| In-canvas HUD (authoritative hud_state rows, NOT AVAILABLE semantics) | IMPLEMENTED, SOURCE-VERIFIED + 625-check gates; RUNTIME NOT VERIFIED | `app/hud_text`, `hud_text.vert/frag`, LINE_LIST pool 32,768 verts, H key |
| GPU-driven draw path (compact lists + 2 indirect commands, no CPU list regen) | IMPLEMENTED (full vkCmdDrawIndexedIndirect), SOURCE-VERIFIED; RUNTIME NOT VERIFIED | `cull.comp` serial deterministic compaction; static_assert VkDrawIndexedIndirectCommand; CPU mirror gated |
| Bloom hardening (half-res chain) | IMPLEMENTED, SOURCE-VERIFIED; RUNTIME NOT VERIFIED | `half_extent` policy gated; resize path re-verified |
| Real star catalog | DATA NOT AVAILABLE (no catalog in repo, no network) — procedural starfield LABELLED, gap documented | Phase 0 search + recorded download attempt |
| Overlay hardening (axes, selection marker, apsis, vectors) | IMPLEMENTED, SOURCE-VERIFIED + gates | `make_axes_segments`, `make_selection_marker` (pure, deterministic) |
| Renderer diagnostics (API/device/limits/swapchain, real) | IMPLEMENTED | startup DIAG lines from driver properties |
| Windows/GPU (Phase 10) | BLOCKED | unchanged |

Counts: v04 562/562 · v05 2984/2984 · v06 625/625 · kepler 99/99 · pytest 1535/1535 · build [8/8] · shaders 13/13 + 19/0 · syntax 0 errors vs Vulkan 1.4.362.

---


## 0. v0.5 REAL RENDERING increment (Step 2 continuation)

Full report: `ASTRA_V0_5_REPORT.md`. Vocabulary is exact; this environment has
NO GPU and NO Windows, so every item is at most SOURCE-VERIFIED (headers +
gates) unless explicitly promoted.

| Brief item | Status | Evidence |
|------------|--------|----------|
| HDR 16F offscreen (capability-aware, explicit failure) | IMPLEMENTED, SOURCE-VERIFIED; RUNTIME NOT VERIFIED | `choose_hdr_format` + `create_offscreen`; policy gates |
| Exposure + ACES-approx tone map (consumable via `[`/`]`) | IMPLEMENTED, SOURCE-VERIFIED; RUNTIME NOT VERIFIED | `post_composite.frag`; clamp gates |
| Real minimal bloom (bright→blurH→blurV→composite) | IMPLEMENTED, SOURCE-VERIFIED; RUNTIME NOT VERIFIED | 3+1 real passes/FBs/descriptors |
| Real instancing (32B SSBO, 2 batched draws) | IMPLEMENTED, SOURCE-VERIFIED; RUNTIME NOT VERIFIED | `BodyInstance`/pack gates; 2×`vkCmdDrawIndexed` |
| GPU culling (deterministic per-slot mask, compute) | IMPLEMENTED, SOURCE-VERIFIED + CPU twin gates (2984); RUNTIME NOT VERIFIED | `cull.comp` + barrier; documented boundary: no indirect (deferred) |
| Real LOD (subdiv1 vs 2, screen-fraction rule) | IMPLEMENTED, SOURCE-VERIFIED + gates; RUNTIME NOT VERIFIED | 240/960-idx meshes; policy fix §3.1 in report |
| Starfield classification | DOCUMENTED GAP (procedural-only, no catalog; not faked) | shader label |
| Orbit/velocity/apsis overlays | VERIFIED (source + kepler 99/99) | unchanged scientific path |
| Perf instrumentation | IMPLEMENTED (CPU real; GPU NOT VERIFIED, printed as such) | F1 telemetry section |
| On-canvas HUD | PLANNED v0.6 (stabilization order) | — |

Counts: v04 562/562 · v05 2984/2984 · kepler 99/99 (3.55e-13) · pytest 1535/1535 · production shaders 11/11 · native build [12/12] · main_production syntax 0 errors vs Vulkan 1.4.362.

---


## 0. v0.4 increment (Step 2 continuation)

| Brief item | Status | Gate |
|------------|--------|------|
| Scientific HUD data model (NOT AVAILABLE semantics; console/title consumed) | IMPLEMENTED+VERIFIED (model) / PARTIAL (on-canvas pixel text PLANNED, no fake) | v04_gates HUD section |
| Selection identity (deselect X; selected_id in RenderState; camera/inspector sync) | IMPLEMENTED (source valid) | native build 95/95; selection rules asserted in HUD case-1/case-2 |
| Peri/apo markers (real apsis positions) | IMPLEMENTED, NOT GPU-VERIFIED | shader unchanged (reused vector pipeline, 7/7 glslang) |
| LUT consumption (real asset → star color, provenance-labeled) | **VERIFIED** (strict parse of real 16×256 bytes; trend assertions) | v04_gates LUT section |
| Audio event bus w/ mandatory classifications + vacuum-acoustic rejection | **VERIFIED (routing/classification)**; audible output NOT VERIFIED (no device) | v04_gates audio section |
| Persistence save/load (15 fields, strict, traversal-safe; F2/F3) | **VERIFIED (round-trip/tamper/path)** | v04_gates persistence section |
| Viz-mode architecture (orbital/velocity; advanced modes hidden until backends exist) | IMPLEMENTED | build |
| Supabase app wiring | PLANNED (offline-first; no fake online) | — |
| HDR/tone-map/bloom/instancing/culling/LOD consumption | PLANNED v0.5 with real offscreen/SSBO paths (NOT faked as toggles) | — |
| Windows runtime (Phase A) | **BLOCKED (environment)** — NOT VERIFIED | — |

**Exact counts**: C++ gates **661 checks / 0 fails** (v04 562 + kepler 99) · Python **1535/1535** · shaders 7/7 glslang + 23/23 project suite · native build 95/95 · 0 FAIL lines native validator.
Bug fixed en route: public-header function accidentally defined in an anonymous namespace (ambiguity + latent link failure) → moved to namespace scope; gates re-green.

Detailed report: `ASTRA_V0_4_REPORT.md`. Prior increments below unchanged.

---

# ASTRA COSMOS — IMPLEMENTATION STATUS (v0.3 Integrated Simulation)

## 0. v0.3 increment (this stage of Step 2)

Environment constraint (unchanged): **PHASE A (real Windows runtime) is
BLOCKED here** — no Windows machine / no GPU in this sandbox. Runtime items
remain NOT VERIFIED by definition; everything below is validated to the
maximum source + executable-gate level available on Linux.

Implemented (source) and validated (executable gates):

| Area (brief phase) | What v0.3 added | Status / gate |
|--------------------|-----------------|---------------|
| **B — RenderState/scene binding** | ObjectState gained authoritative `velocity_km_s` (double); frames now name parents; classification strings bound per object | VERIFIED (build 95/95; scene.h trailing-member change is backwards-compatible) |
| **C — celestial rendering** | Selection highlight (+30 % tint), velocity vectors (toggle V) as a new `vector.vert` pipeline (real vis direction; CINEMATIC length) | IMPLEMENTED, NOT GPU-VERIFIED; shader validated by glslang 16.6.0 (7/7 OK) |
| **D — camera/exploration** | Camera modes: orbit-follow ↔ **FREE** (key O), WASDQE movement in camera plane, SHIFT/PgUp fast, target-relative coordinates only (never float absolute) | IMPLEMENTED, NOT GPU-VERIFIED |
| **E — scientific inspector** | Expanded: speed (real vis velocity), peri/apo, period, parent, frame, provenance, light-travel delay to observer (Newtonian c approx, labeled), NOT AVAILABLE for unknowns (planet T_eff / luminosity / age) | IMPLEMENTED, console/title-bar runtime shipped |
| **F — simulation controls** | step (`.`), warp presets (0–8 → 1×…1e8×), epoch reset (Backspace), scenario restart (F5); reverse time deliberately **not offered** (not a supported property of the mirrored authority — integrity) | IMPLEMENTED, NOT GPU-VERIFIED |
| **G — fidelity gate (orbital)** | Extended reference generation to FULL authoritative state via `elements_to_state` (position AND velocity), 99 checks incl. parent chains (pos+vel) and bit-determinism | **VERIFIED: 99/99 PASS, max rel err 3.55e-13** |
| **G — resurfaced real bug** | `propagate_world` used the Sun's degenerate elements (a=0) → sqrt(mu/a³)=inf → **NaN into every world position** (v0.2 invisible in gates because NaN comparisons are false) | **FIXED + now covered** (strict-equality determinism + NaN-immune gates); primary sits at frame origin by definition |

Kept VERIFIED: 1535/1535 pytest (28.7 s); native validator (23 shaders ok / 0 fail,
no leaks); `main_production.cpp` type-check vs real Vulkan 1.4.362 headers (0 errors);
native build 95/95 targets with the real /tmp/vk SDK.

No change audited as "weakening tests": the gate grew strict-new checks instead.

(For the v0.2 table and prior validation details see below, unmodified.)

---

# ASTRA COSMOS — IMPLEMENTATION STATUS (v0.2 Integrated Simulation)

**Authority**: this file is the manual status checkpoint required by the project
brief. Status vocabulary: **VERIFIED** (executed and observed in this environment)
/ **IMPLEMENTED** (code written and source-level validated) / **PARTIAL** /
**BLOCKED** / **PLANNED** / **NOT VERIFIED** (cannot be observed here — e.g. needs
Windows/real GPU).

## 1. What v0.2 changed

| Area | Before v0.2 | After v0.2 |
|------|-------------|------------|
| Production app (`main_production.cpp`) | 1 fullscreen glow shader, 1 static object, no input | Integrated application: simulated solar system, 10 bodies, orbit overlays, starfield, camera, selection, time warp, pause, inspector |
| Scientific binding | None (static demo values) | C++ mirror of `astra.orbital` drives positions every frame; `RenderState` repopulated with true SI state each frame |
| Shaders | 2 (coords test) | 6 production shaders: `astra.vert/.frag` (starfield bg), `sphere.vert/.frag` (lit bodies), `orbit.vert/.frag` (Kepler lines) |
| Cross-language fidelity | None | `scripts/gen_kepler_reference.py` + `native_renderer/tests/kepler_mirror_check.cpp` (48 checks) |
| Depth/buffers/input | None | D32 depth, VB/IB geometry, per-draw push constants (112/128 B), Win32 keyboard map |

## 2. Feature matrix (this session)

| # | Feature | Status | Evidence / limitation |
|---|---------|--------|-----------------------|
| 1 | Kepler orbital mechanics (native mirror) | **VERIFIED** (source-level) | `kepler_mirror_check`: max rel err **3.6e-13** vs Python authority (`<1e-9` gate), 48 checks incl. Mercury e=0.21 and Moon parent chain |
| 2 | Python scientific engine authority | **VERIFIED** | Unmodified; 1535/1535 pytest pass |
| 3 | Real Vulkan app plumbing (Win32) | **IMPLEMENTED / NOT VERIFIED on Windows** | `main_production.cpp` type-checks against real Vulkan 1.4.362 headers; instance/surface/device/swapchain/depth/render-pass/3 pipelines; **no Windows machine here to run it** |
| 4 | Celestial body rendering | **IMPLEMENTED / NOT VERIFIED on GPU** | icosphere (162v/960idx), lambert+emissive, per-body 112B push constants, indexed draws |
| 5 | Orbit trajectory overlays | **IMPLEMENTED / NOT VERIFIED on GPU** | GLSL Newton Kepler solve (12 iters), 128-segment line strips from real elements, CPU epoch wrap for float32 precision |
| 6 | Starfield background | **IMPLEMENTED / NOT VERIFIED on GPU** | Deterministic hash speckle (project convention), CINEMATIC sky — replaced the placeholder "core glow" |
| 7 | Camera/navigation | **IMPLEMENTED** | Orbit camera (arrows), zoom (PgUp/PgDn), focus cycling (Tab), reset (Home); mirrors `astra/interaction` navigation concepts at the render boundary |
| 8 | Time control | **IMPLEMENTED** | `SimClock` warp ×2/÷2 (`+`/`-`), pause (Space), clamp [1, 1e8], default ×50,000 |
| 9 | Object inspection | **IMPLEMENTED** | Title-bar inspector: true AU, vis-viva km/s, epoch, warp, fps, classification; F1 console dump of all bodies |
| 10 | Scientific classifications visible | **IMPLEMENTED** | Title bar tag + per-body classification strings (DATA-DERIVED inputs, SIMULATED propagation, CINEMATIC visual scaling documented at every transform) |
| 11 | Engine-integrated audio | **PLANNED** (v0.3) | `audio/cosmic_audio_engine.cpp` remains phase-2 stable; not yet driven by sim events in the production app |
| 12 | Instancing / HDR / bloom / tone-map / VFX groups | **PLANNED** (v0.3+, per priority 10–19) | GLSL modules exist under `native_renderer/shaders/`; not yet wired into the production render pass |
| 13 | N-body / relativity / black holes / spacetime / temporal | **PLANNED** (v0.3+) | Python engines exist and pass tests; not yet bound into the production app scene |
| 14 | Destruction / evolution / observation | **PLANNED** (v0.3+) | Python engines exist and pass tests |
| 15 | Persistence / Supabase | **PLANNED** | Python modules exist |
| 16 | Windows Release packaging | **BLOCKED (environment)** — packaging structure from phase 2 intact; no Windows host here to produce/verify | `release/` staging + terminal.bat preserved; `NOT VERIFIED` |

## 3. Validation executed this session (VERIFIED, Linux)

- Full native build (`cmake -G Ninja`, Release, real `/tmp/vk` SDK): **94/94 targets**; the new `src/app/*.cpp` compile clean inside `libastra_renderer`.
- `glslangValidator 16.6.0 -V --target-env vulkan1.3`: **6/6 production shaders OK**; project shader suite **23 ok / 0 fail**.
- `main_production.cpp` `g++ -fsyntax-only` against **real Vulkan 1.4.362 headers** (minimal Win32 stub): **0 errors**.
- Cross-language orbital fidelity: **48/48 PASS**, max rel err 3.6e-13 (tol 1e-9).
- Python suite: **1535/1535 passed** (authoritative engine untouched).
- Native validator (`astra_native` from repo root with SDK env): clean log, no FAILs, no leaks.

## 4. Honest gaps (must not be claimed otherwise)

- **No Windows build, run, or screenshot exists for v0.2.** Visibility claims are
  source-level only. Real validation on Windows requires the 20-item hardware
  checklist (from the project brief) executed on a Windows machine with a Vulkan GPU.
- The C++ solar-system table uses the JPL *approximate* element values **exactly as
  rounded in the source file**; the fidelity gate locks those literals byte-for-byte
  against the Python-computed reference (drift guard), which is the correct contract.
- Binary rendering *on GPU* (draw correctness, pipeline state, depth) is plausible
  and reviewed but **NOT VERIFIED** until first real Windows run.
