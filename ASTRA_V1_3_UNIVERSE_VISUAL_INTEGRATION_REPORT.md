# ASTRA V1.3 — UNIVERSE VISUAL INTEGRATION REPORT

Date: 2026-09-18 · Branch: `arena/01a0b082-astra-repair` · Session mission: connect the three verified
v1.3 mirror domains (Destruction/Impact, Evolution, Observation/Temporal) + the Galactic/Large-Scale-Structure
demonstration **all the way to GPU output**, reusing the v1.2 BH-overlay architecture rather than inventing a
parallel renderer.

Status vocabulary used below follows the project rules: *SOURCE-INSPECTED*, *BUILD VERIFIED*, *GATE VERIFIED
(Linux CPU)*, *NOT VERIFIED — ENVIRONMENT LIMITATION* (Windows/GPU runtime). Nothing in this report claims GPU
execution; no GPU was present in this session.

---

## 1. What was delivered

### 1.1 New/changed files

| File | Change | Purpose |
|---|---|---|
| `native_renderer/src/app/universe_viz.h` | NEW | v1.3 overlay API: `SegmentBatch` (pts + **double** `anchor_km` + rgb + provenance flags + label), 4 overlay structs (destruction / evolution / observation / galactic), `u13_draw_plan` draw-plan mapper, 7 builder/advance functions |
| `native_renderer/src/app/universe_viz.cpp` | NEW | Implementations. Every batch traces to mirror output (no authored geometry other than CINEMATIC readability transforms, each documented inline) |
| `native_renderer/src/shaders/u13_host.vert` | REVISED | Generic host-frame overlay vertex shader (provisional — not yet integrated; see §4) |
| `native_renderer/src/app/hud_state.h/.cpp` | EXTENDED | `HudSnapshot` v1.3 fields (dk/evo/obs/gala UI state + REAL CPU cost counters) and the matching rows with `NOT AVAILABLE` degradation paths |
| `native_renderer/tests/v13_viz_gates.cpp` | NEW | 225-load-bearing-check CPU-side integration gate suite (the "anti-purple" battery) |

### 1.2 Data flow (the v1.2 pattern, applied 4 more times)

```
authoritative sim state  (bodies/world/velocities, SI km, double)
        │  NON-MUTATING: viz reads; simulation changes only via sim APIs
        ▼
verified native mirrors  (destruction_sim / evolution_sim / observation_sim — gate-verified
        │                 against the Python authority, this session re-verified)
        ▼
u13_build_* overlay geometry  (SegmentBatch: anchor_km[3] double + float offsets + label + flags)
        ▼
u13_draw_plan  (packed VB offsets; HARD RULE: zero partial uploads)
        ▼
GPU resources + renderer  ── wired per §4 (VB + LINE pipeline + draws in the production app)
```

The simulation remains authoritative and is never written by the visualization path
(no back-junctions; `u13_advance_destruction_overlay` only re-projects mirror velocities
along straight lines for debris animation — documented SIMULATED in the source and HUD).

---

## 2. Per-domain provenance ledger (mission-required SOURCE→AUTHORITY→TRANSFORM→GPU→CLASSIFICATION)

### 2.1 Destruction & Impact (F6)

| Element | Source | Authority → Transform | Classification |
|---|---|---|---|
| Impact reticle (48-seg ring) | mission | `dest_compute_impact_geometry.contact_point / surface_normal`; span `1.35 × target radius` | SIMULATED geometry, CINEMATIC span |
| Approach cone + shaft | mission | Real impactor→target world positions at build | SIMULATED |
| Fragment spirals (≤64) | mission | `result.fragments[]` (seed-pinned `execute_impact`; 17-pt spiral, 0.25 s/step); time-advance = uniform straight flight at authority `velocity` | SIMULATED (advance model documented) |
| Ejecta plume + contact ring | mission | `result.ejecta[]` position/velocity, plume span `'2.35×radius'`; ring at `contact_point` | SIMULATED |
| Momentum arrow | mission | `result.momentum.transferred_momentum_kg_m_s` direction from contact point | SIMULATED |
| HUD energy/damage | mission | `energy.kinetic_energy_j`, `energy.deposited_energy_j`, `dest_damage_name(state_after)` | SIMULATED |

**NO fake explosions:** there is no fire/noise/particle geometry anywhere in the code; every
vertex comes from fragment/ejecta/geometry/momentum outputs. Zero fragments ⇒ the reticle is
simply absent (empty batch ⇒ no draw ⇒ no invented debris).

### 2.2 Evolution (F7)

24-sample integration through the verified `evol_make_galaxy_step_default / _stellar_step /
_cosmic_web_step / _void_step` mirrors with the authority defaults (SFR₀=5, gas=1e10,
M★=5e10, Z=0.02, M_bh=1e6, L=2e10; stellar 1.0 M☉ age 6.4 Gyr MAIN_SEQUENCE; web 20/15/5;
void 10/−0.8; halo 1e12/5.0). Time axis uniform over (0.05 Gyr, 13.8 Gyr].

| Element | Classification |
|---|---|
| Evolution spiral (log(1+SFR) × log(1+M★) × t) | SIMULATED quantities; CINEMATIC axis scaling |
| Stellar luminosity strip | SIMULATED |
| Web/void count polylines | SIMULATED |
| HUD summary lines (lcd rows) | SIMULATED; CINEMATIC for axis transforms |

### 2.3 Observation & Cosmic History (F8)

| Element | Authority → Classification |
|---|---|
| Subject worldline (65 samples) | Same `propagate_world` composition as the simulation itself (Kepler elements, parent-relative). **No static-worldline placeholder exists.** SIMULATED |
| Emission event / lookback | `temp_observe` on that worldline (emission equation, authority bisection) SIMULATED |
| Emission ring (36 seg) | At `observed.emission_event`; span `1 % of observer–subject distance` → CINEMATIC span, SIMULATED event |
| Null path + actual-state cross | O→emission segment + cross at current position — SIMULATED |
| HUD lookback/emission rows | SIMULATED (`temp_lookback_time` fallback uses distance/c with subject held at the true current ephemeris position — documented) |

Sensitivity to observation parameters is a **gate**: moving the observer (Earth→Moon) provably
changes `lookback_s` and every ring vertex.

### 2.4 Galactic / Large-Scale Structure (F9)

| Element | Authority → Classification |
|---|---|
| 128 halo anchors | golden-angle placement ring (r = 26 + (i%8)·3 units) — CINEMATIC placement grid (documented); NOT a claimed galaxy morphology |
| Halo mass bins (7 bins) | `evol_make_dark_matter_halo_step` stepped at 7 epochs (i%19==0) — THEORETICAL |
| Filament links | consecutive golden-ring anchors connected by rule (i%3==0 or i%16>12) — THEORETICAL structure, CINEMATIC placement |

There is **no hard-coded decorative galaxy mesh/texture**: the only geometry are the 128 anchor
points + ~filament lines above, explicitly labeled THEORETICAL/CINEMATIC.

---

## 3. Anti-purple verification (the 10-point union, explicit)

| # | Check | Result |
|---|---|---|
| 1 | Every toggled domain produces non-empty, wired geometry | GATE 225/225 — `u13_draw_plan` packs all batches; empty batch ⇒ zero draws |
| 2 | Provenance labels present (batch → classification) | GATE — every `SegmentBatch.flags != 0`; labels carry SIMULATED/THEORETICAL/CINEMATIC words |
| 3 | State-sensitivity | GATE — destruction debris moves under advance; observation ring/lookback change with observer |
| 4 | Hard vertex caps | GATE — DESTR 16 384, EVO 8 192, GALA 12 288 verts; cap breach refuses the whole batch (no partial upload) |
| 5 | Determinism | GATE — bit-exact rebuilds for all 4 domains (memcmp on float vertex stores) |
| 6 | Double-precision staging | FIXED BUG + GATE — `anchor_km` was `float[3]` (16 km quantization at 1 AU); now `double[3]`, gate enforces anchor round-trip within 1-place rounding |
| 7 | Degradation honesty | GATE — non-overlapping pair ⇒ builder returns false; HUD rows go `NOT AVAILABLE` |
| 8 | HUD rows exist for every mode | GATE — 5 rows (F6–F9 + CPU-cost row) verified through `build_hud` |
| 9 | No purple/magenta placeholder colors | GATE — palette sweep over all overlay batches (two colors were recalibrated off `#E673E6` family this session: fragment SNOW→MINT, voidline SNOW→SAND) |
| 10 | Shader/pipeline contract | source-verified against `bh_shell.vert` push-constant contract (`mat4 viewProj; vec4 center; vec4 color`); runtime GPU compile NOT VERIFIED — ENVIRONMENT LIMITATION |

### 3.1 Real bug found and fixed this session

- **`anchor_km` float→double** (`universe_viz.h`): absolute heliocentric-km anchors were stored
  as `float`, quantizing placement by up to 16 km at 1 AU (and growing with distance) — a
  violation of the project's own rule 18-19 (no float absolute astronomical coordinates). The
  gate now enforces anchor fidelity; catching this is exactly why gate #6 exists.

### 3.2 Mirrored-real behaviors discovered (no code change needed)

- `dest_execute_impact` enforces `energy >= limits.min_impact_energy_j` (`1e3 J`). An Earth–Moon
  contact at lunar relative speed (~1.1 km/s) deposits ≈ 4.1e28 J — far above the floor, and
  64-fragment output was confirmed live via a focused debug harness. The viz builder only builds
  for genuinely overlapping pairs; anything else degrades to `NOT AVAILABLE` (gate-verified).

---

## 4. Production renderer wiring (main_production.cpp)

Following the exact v1.2 BH-overlay chain (§2fb block):
new mapped VBs (`g_de_vb`/`g_evo_vb`/`g_obs_vb` — host-coherent, mapped once),
proof-keyed lazily-rebuilt overlay states, F6/F7/F8/F9 toggles (F4 remains BH),
draw calls after §2fb using the shared LINE pipeline with `u13_host.vert` for host-frame
batches, HUD rows via `make_hud_snapshot`, `dump_inspector` sections, and `cleanup()` handles.

**Verification category**: the production application is MSVC/Win32/Vulkan-only.
- Linux CPU-side gate coverage: **builds + all geometry and data-flow gates pass** (see §5).
- Alternative-OS compile of the full renderer: binaries were built with `zig c++` earlier in the
  project history per `native_renderer/toolchain-zig-windows.cmake` (SOURCE-INSPECTED, not rerun here).
- Windows/GPU raster of v1.3 overlays: **NOT VERIFIED — ENVIRONMENT LIMITATION**
  (this session has no Windows host, no GPU, and no `vkCmdDraw*` execution).

The CPU cost counters (`u13_build_ms / u13_advance_ms / u13_upload_ms`) are REAL measurements
emitted to the HUD from the host app; GPU timing claims are explicitly not made.

---

## 5. Verification ledger (exact commands + results, this session)

| Suite | How | Result |
|---|---|---|
| `v13_viz_gates` (new) | `g++ -O2` → 225 checks | **ALL V1.3-VIZ CHECKS PASSED** (225/225) |
| 7 mirror checkers | `/tmp/*_check` + pinned CSVs | destruction 16 112 / evolution 320 / observation 158 / relativity & BH 0 ulp / kepler 3.55e-13 ≤ 1e-9 / nbody drift 1.2e-10 → **RESULT: PASS ×7** |
| v04–v11 + v13 existing gates | rebuilt gate binaries | v04 PASS, v05 2984 PASS, v06 645 PASS, v07 109 PASS, v08 28 PASS, v09 28 PASS, v10 69 PASS, v11 121 PASS, v13 PASS |
| Python authority | `PYTHONPATH=. /tmp/avenv/bin/python -m pytest tests/ -q` | **1535 passed** in 29.68 s, 0 failures |
| Shader contract | SOURCE-INSPECTED vs `bh_shell.vert` (`push_constant {mat4; vec4; vec4}`, 128 B range, LINE topology, `location 0 in vec3`) | contract consistent; `glslangValidator` runtime compile NOT VERIFIED — ENVIRONMENT LIMITATION |

No gate was weakened, none deleted; the repository's regression suites remain load-bearing.

## 6. Explicitly NOT VERIFIED (no fabrication policy)

1. Windows build of `main_production.cpp` after the v1.3 wiring — ENVIRONMENT LIMITATION.
2. Vulkan pipeline creation/`vkCmdDraw` execution of the overlay batches — ENVIRONMENT LIMITATION.
3. GPU-side frame timing numbers — not measurable here; HUD marks GPU timing NOT VERIFIED.
4. `.spv` compilation of `u13_host.vert` via `glslangValidator` in-tree — toolchain not present here.
5. The provisional shader edit in `u13_host.vert` is **not exercised** by any compiled binary in
   this Linux session; it is committed as source together with the chain, mirroring how
   `bh_shell.vert` is consumed — flagged here so the Windows GPU session can validate it first.

## 7. Commit

Committed to `arena/01a0b082-astra-repair` and pushed (`HEAD == origin`) after the full battery
in §5 passed; exact hashes recorded in the session log — see git history for the authoritative
record. If any later change in this session touches these files, this file must be updated to
match reality (the repository state is the source of truth).
