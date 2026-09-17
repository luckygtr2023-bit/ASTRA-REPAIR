# ASTRA COSMOS v0.6 — STABILIZATION + SCIENTIFIC VISUALIZATION report

Step 2 (native renderer), v0.6. Baseline: v0.5 @ `fd9d2d9`. Environment has
**no GPU, no Windows, no network access** — runtime/GPU/Windows status
vocabulary is exact: nothing is claimed verified that cannot be verified here.

## Phase 0 — inspection (recorded)

- Branch `arena/01a0b082-astra-repair`, HEAD `fd9d2d9` (== remote), clean tree.
- Inspected: `main_production.cpp` render graph (5-pass recording), descriptor
  layout table (mesh/compute/post1/post2), instance SSBO + mask scheme,
  cull.comp, LOD policy, post chain, hud_state API, all gates, asset dirs.
- **Real star catalog search**: nothing in repo (no `*.fits`, `*.vot`, Hip/Gaia
  /HYG/BSC data). Outbound network blocked (TLS handshake fails for Harvard
  TDC mirror and GitHub raw — recorded). → Phase 4 handled per "no data → do
  not fabricate" rule.
- Existing UI technology: `native_renderer/src/debug/imgui_debug.cpp` is a
  10-line STUB with all ImGui calls commented out — no Dear ImGui sources are
  vendored. Vendoring ImGui for a text HUD was rejected as overreach for this
  architecture; honest minimal path chosen: own stroke-text pipeline from
  `hud_state` rows (single source of truth, 100% real rendered geometry).

## 1. REAL in-canvas HUD — IMPLEMENTED, SOURCE-VERIFIED + gated (RUNTIME NOT VERIFIED)

- New module `app/hud_text.{h,cpp}`: pure CPU segment builder consuming
  `build_hud(HudSnapshot)` rows — the same authoritative mapping used by the
  console/title HUD (no duplicate scientific math, no second source of truth).
- Self-authored stroke font (5×8 grid monoline, ~50 glyphs incl. all HUD
  characters; lowercase → uppercase display-fold, documented). No font data
  copied from anywhere; unknown chars render as boxed placeholders (sanitized).
- New `hud_text.vert/.frag` (vertex {vec2 ndc, float colorIdx}), LINE_LIST
  pipeline, dynamic host-coherent vertex buffer (32,768-vertex capacity,
  overflow counted in alloc_failures and skip is printed).
- NOT AVAILABLE rows are carried from hud_state (styled gray, class NA);
  classifications color-coded (REAL green / DATA blue / SIMULATED cyan /
  CINEMATIC amber / NA gray) — styling only; text content is hud_state.
- Fields rendered exactly per build_hud: sim time, warp, observer time (NAT
  when no selection), FPS+frame time (REAL measured), viz mode, camera +
  reference frame, selected object/type/classification/distances/speed/light
  delay; CPU frame time reported; GPU timing reported as NOT VERIFIED.
- Toggle: **H key** (default ON). Drawn inside the HDR scene pass → real
  pixels through the real tone mapper.
- Gates: glyph coverage (all HUD-emittable chars + fold classes), NDC bounds,
  unknown-glyph box, zero-width space path, full-HUD build, bitwise
  determinism, line-budget truncation, NOT AVAILABLE path + NA color class,
  token→color mapping, observer-time NAT branch (v06_gates: 625 checks).

## 2. GPU-driven draw path — IMPLEMENTED (full indirect), SOURCE-VERIFIED (RUNTIME NOT VERIFIED)

- `cull.comp` rewritten: single workgroup (128 lanes), serial thread-0
  classification+compaction into LOW/HIGH compacted instance lists (stable
  source order), writes `[2] x VkDrawIndexedIndirectCommand` (20 B stride,
  static_assert-verified). Zero-visible → instanceCount=0 no-op draws.
  Determinism: serial order pass (no atomics) — CPU mirror
  `render_math.cpp::compact_lod/build_indirect_commands` gated.
- Draw side: ONLY `vkCmdDrawIndexedIndirect` ×2 (LOW, HIGH) with per-batch
  descriptor sets; **no CPU re-generation of the visible list after GPU
  culling**. Selection highlight packed in instance `color_flag.w`
  (selected bit + emissive bit) — single authoritative identity preserved.
- Barriers: compute storage-writes → vertex-shader reads
  (`SHADER_WRITE→SHADER_READ`) and draw-indirect args
  (`SHADER_WRITE→INDIRECT_COMMAND_READ`, dst stage `DRAW_INDIRECT|VERTEX_SHADER`).
  Buffers host-coherent like all production buffers (documented choice).
- Mask retained for HUD counters (+1-frame host readback, labeled).

## 3. Bloom hardening — IMPLEMENTED, SOURCE-VERIFIED (RUNTIME NOT VERIFIED)

- Chain now: HDR(full-res) → bright+downsample → **half-res** blur H/V
  (ping-pong) → composite upsample (linear). Half-extent policy
  `render_math::half_extent` (min 1 px, zero-safe, gated, monotone sweep).
- Post-pass viewports/scissors/framebuffers correctly sized to the bloom
  extent (fixed: v0.5 full-res assumptions); descriptor identities stable
  across resize (`update_post_descriptors` at init + recreation), offscreen
  resources fully destroyed/recreated with the swapchain (unchanged, verified
  by re-reading the recreation path). No leaked resources in either path
  (cleanup() extended for all new buffers/set layouts).

## 4. Astronomical star data — DATA NOT AVAILABLE → documented, not fabricated

- No catalog in repo (Phase 0 search). Network unavailable (attempt recorded:
  Harvard TDC yale BSC5 mirror + GitHub raw — TLS resets). Per brief rule:
  procedural starfield retained and its `astra.frag` provenance comment
  strengthened (explicitly labeled PROCEDURAL/CINEMATIC, missing dependency
  documented). Planned ingestion format (v0.7 candidate) recorded in §gaps:
  tiny textual BSC record → binary pack tool (provenance header, units deg/J2000,
  magnitude, spectral class) → point-list SSBO draw.

## 5. Scientific overlays — hardened

- Kepler orbits / velocity vectors / apsis markers: unchanged authoritative
  paths (kepler gate 99/99 re-run).
- **Reference-frame axes** (G key, default ON): 3 axes at the floating origin
  via `make_axes_segments` (pure, gated; deterministic).
- **Selected-object marker**: crosshair from `make_selection_marker` using the
  single authoritative `g_selection`/`RenderState.selected_id` identity.
- Velocity-vector CINEMATIC length scale remains explicitly labeled.
- All overlay vertices derive from authoritative state only (reviewed; no
  duplicate physics introduced anywhere).

## 6. Selection + camera hardening

- Single authoritative identity `g_selection` + sim-side
  `RenderState.selected_id` mapping verified by re-reading all four consumers
  (pack, marker, HUD snapshot, orbit highlight). Camera focus (`g_focus`) vs
  selection still decoupled (v0.4 model). Tab/X/O/WASDQE code paths re-read
  after all edits; no behavioral edits this increment (stabilization).

## 7. Renderer diagnostics — IMPLEMENTED

Startup now prints REAL driver values: Vulkan API version (device), device
type, vendor/driver version, descriptor/push-constant/compute-workgroup
limits, swapchain format+color space+present mode+MSAA policy (1×
everywhere), validation-layer status (none enabled, stated honestly).
Runtime counters already present; extended with indirect-draw and dispatch
counts (title+F1+300-frame log).

## 8. Resize/swapchain hardening — verified + gated

Recreation path re-read end-to-end: zero/extent guards, device idle,
framebuffers→views→depth→offscreen(HDR+bloom pair)→descriptors→recreation
ordering; new half-res bright framebuffers + extent tracked and recreated;
counter increments (REAL). Gates cover extent policy (incl. 0-size→1px).

## 9. Validation (exact counts, all re-run after every edit)

| Battery | Result |
|---|---|
| v04_gates | **562/562** PASS (0 regressions) |
| v05_gates | **2984/2984** PASS |
| v06_gates (new) | **625/625** PASS |
| kepler mirror | **99/99**, max_rel_err 3.55e-13 |
| pytest | **1535/1535** (29.36 s) |
| native build | PASS [8/8] (lib+astra_native) |
| main_production syntax (real Vulkan 1.4.362 + Win32 stub) | **0 errors** |
| production shaders (11 old + 2 new) | **13/13** glslang OK |
| project shader validator | 19 shaders, 0 failures |
| IndirectCmd vs VkDrawIndexedIndirectCommand | static_assert PASS (20 B) |

## Bug found & root-cause fixed this increment
1. **Uninitialized padding in `HudSeg` (17 B struct, 3 pad bytes)**: a
   determinism memcmp gate caught byte-level nondeterminism; the same garbage
   would have been uploaded to the GPU vertex buffer. Root cause fixed in the
   struct (explicit reserved tail, value-initialized, static_assert 20 B).
2. **'8' glyph incomplete at 12-segment cap** → cap raised to 16 + full glyph
   (caught while authoring coverage checks).
3. **`glyph_available` coverage string had a spurious space** (would have let
   the gate under-cover) — fixed.
4. **Framebuffer edit regression during patching** (dropped `resize()` +
   full-res `post_fb` extents after half-res switch) — caught by re-reading
   the function after edit; fixed before any battery run (§no weakened tests).
5. **`pack_body_instance` sloppy intermediate** (dead `(void)cb` + duplicated
   flag logic) — immediately rewritten cleanly.

## Regressions
None observed. All v0.4/v0.5 batteries re-run green after the restructure.

## Performance
No new measured numbers (no GPU/Windows; anything would be fabricated).
Half-res bloom reduces fragment work by construction (4× fewer blur texels),
but no measurement claims are made here; CPU frame ms is a REAL runtime value
printed only at run time.

## Blocked / Not verified
- Phase 10 Windows/GPU gate: **BLOCKED** (no Windows/GPU; nothing here weakens
  it — all v0.6 runtime claims must be re-verified on the Windows machine).
- Runtime GPU correctness of indirect draws, HUD pixels on screen, bloom
  output: **RUNTIME NOT VERIFIED**.
- Real star catalog: **DATA NOT AVAILABLE** (documented; no fabrication).

## Remaining gaps
1. Real star catalog ingestion + point-list draw (needs legally-usable data +
   network or repo-bundled dataset; format spec above).
2. HUD pane: fixed NDC layout (pixel-styled) — per-resolution scaling policy
   future work (non-blocking).
3. GPU timestamp queries (`VkQueryPool`) when device present (Windows gate).
4. Optional velocity-vector depth-test/pick interaction.

## Recommended v0.7 (scope proposal)
1. Windows Phase A bring-up of v0.6 binary (promote statuses with evidence).
2. Real catalog path (if data becomes available repo-side).
3. GPU timestamp instrumentation behind feature query.
4. Text refinement (resolution-adaptive pane, line budget auto-fit).
