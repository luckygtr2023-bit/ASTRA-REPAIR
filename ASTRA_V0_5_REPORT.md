# ASTRA COSMOS v0.5 — REAL RENDERING increment report

Step 2 (native renderer), v0.5: from presentation shell to genuine scientific
visualization renderer. No giant rewrite; every feature has a real GPU path
(scientific state → RenderState → GPU resources → pipeline → command recording
→ draw → target → visible output). Runtime/GPU/Windows statuses stay honest:
**this environment has no GPU and no Windows**, so every runtime claim is
NOT VERIFIED and static/source verification is labeled as such.

- **Commits**: start `a5acdfa` (v0.4) → end _(this commit, hash recorded in git)_
- **Integrity**: `git status` clean at start; branch `arena/01a0b082-astra-repair`; documented anchors only (`ASTRA_V0_4_REPORT.md`, `ASTRA_IMPLEMENTATION_STATUS.md`, `README.md`; production `main_production.cpp` @ a5acdfa).
- **Environment**: Linux, g++ + CMake/Ninja (Python venv toolchain), glslang 16.6.0, Vulkan headers/loader 1.4.362. **NO Vulkan device available** → P13/P14 BLOCKED, runtime GPU success NOT VERIFIED.

## 1. What was implemented (source-complete, structurally valid Vulkan)

Each item names its REAL path in `native_renderer/src/main_production.cpp` /
`native_renderer/src/shaders/`. "Shader/class existence" is not claimed as
verification: Vulkan calls are listed because the code records and submits
them; device execution is NOT VERIFIED (no GPU here).

| # | Item | Implementation (real path) | Status |
|---|------|----------------------------|--------|
| P1 | **HDR float render target** | `choose_hdr_format()` queries `VK_FORMAT_R16G16B16A16_SFLOAT` for `COLOR_ATTACHMENT | SAMPLED`; explicit startup failure, no silent 8-bit fallback (`app::choose_hdr_format` policy gated by v05_gates). Scene renders into `g_hdr` (scene pass, `finalLayout=SHADER_READ_ONLY`) | IMPLEMENTED (SOURCE-VERIFIED), RUNTIME NOT VERIFIED |
| P2 | **Exposure + tone mapping** | `post_composite.frag`: ACES-fitted approximation (Narkowicz) `out = ACES(exposure·(hdr + bloom))` into the **SRGB swapchain** (hardware sRGB EOTF; no manual gamma duplication). `[` `]` keys → `clamp_exposure` [0.05, 20] (CPU policy twin, gated) | IMPLEMENTED (SOURCE-VERIFIED), RUNTIME NOT VERIFIED |
| P3 | **Real minimal bloom** | `post_bright.frag` (threshold 1.0 HDR, soft knee) → `post_blur.frag` H (separable gaussian) → V (ping-pong `g_bright0/g_bright1`) → composite add. 4 real fullscreen passes, real FBs/descriptor sampling, no CPU pixels | IMPLEMENTED (SOURCE-VERIFIED), RUNTIME NOT VERIFIED |
| P4 | **Real instancing** | ONE `g_inst_buf` SSBO (`BodyInstance` 32 B: pos+radius, color+selected-flag), packed per frame from authoritative RenderState via `pack_body_instance` (double→float at boundary, floating origin preserved). **TWO `vkCmdDrawIndexed` total** (LOW + HIGH batches), `gl_InstanceIndex` addressing. Selection highlight via push-constant index (single `g_selection` identity) | IMPLEMENTED (SOURCE-VERIFIED), RUNTIME NOT VERIFIED |
| P5 | **GPU visibility/culling** | `cull.comp`: deterministic per-slot mask writes (no atomics/order dependence): `mask[i] = 0 invisible | 1 LOW | 2 HIGH`. Compute dispatch every frame + storage→vertex barrier. **Boundary documented**: no indirect draw (deferred, P11 note); instance count stays N with mask-matched batches (capacity-clamped, overflow counted). CPU twin in `app/render_math.cpp` + 2984-check gate suite (exact shader-formula equality sweep) | IMPLEMENTED (SOURCE-VERIFIED + gates), RUNTIME NOT VERIFIED |
| P6 | **Real LOD** | Two REAL icosphere geometries (subdiv 1 = 240 idx, subdiv 2 = 960 idx) uploaded as separate VB/IB; per-frame GPU-selected batch draws; HUD counters `LOD hi/lo` (GPU mask read back +1 frame, labeled). Rule (`lod_select`): screen-height fraction `r/(d·tan(fov/2)) > 1%`. Policy was *corrected by tests*: 2.5% would have LOW-LOD'd the Sun itself (1.7% screen) — now 1%, re-gated | IMPLEMENTED (SOURCE-VERIFIED + gates), RUNTIME NOT VERIFIED |
| P7 | **Starfield classification** | Kept as-is, explicitly labeled `PROCEDURAL / CINEMATIC` (no star catalog exists in this repo; gap documented, not faked). No LUT misuse for stars | DOCUMENTED GAP (no fake data) |
| P8 | **Orbit hardening** | Orbit overlay unchanged: same Kepler solve in-shader from REAL elements, parent-anchored at parent's actual double-precision position; validated by kepler_mirror 99/99 | VERIFIED (source+kepler gates) |
| P9 | **Scientific overlays** | Velocity vectors (real sim velocity, CINEMATIC length scale — labeled), peri/apo marks (real apsis positions via exported `rotation_pqw_to_ijk`; guarded `NOT AVAILABLE` when elements absent) | VERIFIED (source+kepler gates) |
| P10 | **On-canvas HUD** | DEFERRED to v0.6 (pipeline-order rule from brief; title-bar + console HUD remain the verified text path) | PLANNED (explicit) |
| P11 | **Perf instrumentation** | REAL CPU frame ms (`std::chrono` around `render_frame`), draws/frame (counted), instances/draw, LOD hi/lo counts (GPU mask +1-frame readback), post passes (static=4), swapchain recreations (counted), alloc failures (counted) → title bar + F1 telemetry section. **GPU frame time NOT VERIFIED** (no timestamps device here; not fabricated) | IMPLEMENTED, partially RUNTIME-VERIFIABLE at run time |

## 2. Exact verification counts (this environment)

| Battery | Result |
|---------|--------|
| `tests/v04_gates.cpp` (native, with all v0.4 modules) | **562/562 PASS** (regression: 0) |
| `tests/v05_gates.cpp` (new: frustum/LOD/clamps/format/packing/CPU↔SPV mirror sweep) | **2984/2984 PASS** |
| Kepler C++-mirror vs in-repo reference CSV | **99/99 PASS, max_rel_err 3.55e-13** |
| Python suite (`/tmp/avenv/bin/python -m pytest tests`) | **1535/1535 PASS in 29.48 s** |
| Native build (libastra_renderer + astra_native, Release) | **PASS [12/12]** (render_math.cpp glob-included) |
| `main_production.cpp` syntax vs real Vulkan 1.4.362 + Win32 stub | **0 errors** |
| Production shaders via CMake glslang target (7 old + 4 new) | **11/11 OK** (cull.comp, post_bright/blur/composite + instanced sphere.vert) |
| Project shader-suite validator | **19 validated, 0 failures** |

## 3. Root-cause fixes this increment

1. **LOD policy threshold — found by gates, fixed at source.** Initial 2.5%
   screen rule would classify the real Sun (≈1.7% of screen at 55° FOV) as
   LOW-LOD. Corrected the documented policy to 1% in `render_math.h`,
   `cull.comp` and the gates (one formula, three mirrors), then re-ran the
   full battery — no test weakened to green; the policy changed, tests assert
   the corrected rule including the Sun boundary case.
2. **Camera eye distance in LOD.** `cull.comp` first draft measured distance
   from the world target origin; corrected to camera-eye distance
   (push-constant `eye`) so LOD matches what the viewer actually sees.
3. **v0.4→v0.5 plumbing collisions** (namespaces/PC layouts) resolved by full
   restructure of pass architecture with explicit layouts per draw class —
   no shared-state regression; v04 gates re-run green (562/562).

## 4. Regressions

**None observed.** v04 gates 562/562, kepler 99/99, pytest 1535/1535 all
re-run after the restructure. The old per-body 10× `vkCmdDrawIndexed` pattern
is *replaced* by 2 instanced batched draws — behavior-equivalent by design
(and by shader), NOT runtime-observed here (no GPU).

## 5. Measured performance

**None claimed.** No GPU/runtime in this environment; any numbers here would
be fabrication. CPU-side frame timing instrumentation exists
(`g_perf.cpu_frame_ms`, real `std::chrono` wall time on the host) — values
appear only at real run time (Windows machine).

## 6. Unavailable performance metrics (explicit)

GPU frame time, per-pass GPU cost, cull dispatch cost, bandwidth:
**NOT VERIFIED / not measurable in this environment** (no Vulkan device,
no `VK_KHR_performance_query` host here). Instrumentation left honest
(NOT VERIFIED printouts), not fabricated.

## 7. Windows status (Phase A gate, unchanged)

**BLOCKED** — the 22-item Windows checklist (PTYHON embed, MSVC/Win32 build,
real device bring-up, launcher, installer) requires Windows + GPU; nothing in
this increment weakens or replaces that gate. All v0.5 runtime claims must be
re-checked on the Windows machine before any status promotion.

## 8. Remaining gaps (explicit, to plan v0.6)

- **Indirect draw + compaction** (full GPU-driven path): mask-batch approach
  is documented (instance count N, degenerate-culled batches). `drawIndirect`
  from a GPU count is the documented upgrade; requires dual-buffer/2-frame
  in-flight tracking or fence readback — deferred deliberately.
- **On-canvas pixel HUD** (P10) deferred to v0.6 per pipeline-stability rule.
- **Half-res bloom** (up/down sample chain) — currently full-res ×3 passes
  for minimal surface; perf-grade mip chain is v0.6-scope if budget demands.
- **Real star catalog** (P7): no dataset in repo; procedural only, labeled.
- **Depth-aware overlays / occlusion** — overlays ignore depth (legacy
  behavior preserved); optional depth-tested overlay mode: v0.6 candidate.

## 9. Recommended v0.6 scope

1. In-canvas HUD (font atlas + text pipeline) consuming existing `hud_state`.
2. Full indirect-draw GPU-driven path (compacted SSBO + count + `vkCmdDrawIndexedIndirect`).
3. Bloom mip chain (half/quarter res), optional.
4. Windows Phase A bring-up of v0.5 binary: pixel-verify HDR/bloom/cull on a real device, promote statuses.

## 10. Honesty ledger (mandatory vocabulary used)

- **IMPLEMENTED/SOURCE-VERIFIED**: P1–P6, P11 (code paths real; Vulkan-valid per headers; gates for the CPU-derivable logic).
- **RUNTIME-VERIFIED**: none (no GPU) — CPU-only gates do not constitute runtime GPU verification.
- **NOT VERIFIED**: runtime GPU behavior, Windows, GPU timings.
- **BLOCKED**: P13 (visual regression: needs GPU), P14 (Phase A Windows 22-item checklist).
- **PLANNED**: P10 on-canvas HUD, indirect draw, mip bloom, real star catalog.
- No test was weakened to green; the only corrected "expected" value tracing
  back to v0.5 work was the LOD threshold **policy itself** (documented §3.1),
  with the Sun re-verified as a HIGH-LOD boundary case.
