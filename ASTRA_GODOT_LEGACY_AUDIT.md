# ASTRA GODOT LEGACY AUDIT

Date: 2026-09-18 · Branch: `arena/01a0b082-astra-repair` · Auditor: Arena.ai Agent Mode
Scope: `GODOT_PHASES/` and **all** Godot-related material in the repository, audited against the six
questions in the mission. Nothing was deleted. This audit is record-first, cleanup-second —
the cleanup actions it authorizes (archival moves only) are listed at the end, executed after
this document, and verified by the full regression battery reproduced at the bottom.

**Method note (per standing rules):** the audited source of truth is the repository as inspected
today (`git status`, `git ls-files`, full-text `grep`, directory walks). Claims from earlier
reports were re-checked where they touched load-bearing questions.

---

## 1. Inventory

Two disjoint Godot artifact sets exist, and **both are untracked in git** (never committed on
this branch — they appear only as `??` untracked status):

| Set | Files | Size | Content |
|---|---|---|---|
| `GODOT_PHASES/` | 5 | 148 KB | Design-spec markdowns `PHASE_01..05_*.md` (2026-09-17), written for branch `arena/01a0a5a2-astra-cosmos` |
| `visualization/` | 136 | 1.5 MB | A Godot 4.4 prototype track: `godot/project.godot`, phase scenes (`.tscn`), GDScripts (`.gd`), `.gdshader` shaders, `.tres` materials/VFX, GDExtension C++ skeleton, bridge scripts/validators, shared docs (`ARCHITECTURE.md`, `SHADER_CATALOG.md`, …), assets |

No other Godot material exists anywhere else in the tree (full-tree name + content sweep).

## 2. The six audit questions, answered with evidence

### Q1. Referenced by production code? — **NO**

| Surface checked | Result |
|---|---|
| `native_renderer/CMakeLists.txt` + all `*.cmake` | 0 matches |
| `START.bat`, `scripts/terminal.bat`, all of `scripts/` | 0 matches |
| CI | **No `.github/workflows/` exists at all** |
| Production Python (`astra/`, `src/`) | 0 matches |
| Native C++ (`native_renderer/src/`) | 0 matches (the tool `tools/validate_native_project.py` carries one docstring mention, see §4) |
| pytest (`tests/`) + native gates (`native_renderer/tests/`) | 0 matches |
| Supabase/bootstrap/distribution/docs/logs | 0 matches |

### Q2. Required for building ASTRA? — **NO**

Build chain (verified this session's battery): MSVC/zig C++20 + `glslangValidator` for
`native_renderer/src/shaders/*.vert|frag|comp`. GLSL sources are native Vulkan; nothing in the
build consumes `.gd`. 

### Q3. Required at runtime? — **NO**

- `README.md:54` (inspected today): the `visualization/` track is "**not** part of the Windows
  runtime distribution."
- The Windows release tree (`release/ASTRA-COSMOS/`) ships native runtime + assets; no Godot
  executable or export package exists in the repo (confirmed by
  `ASTRA_WINDOWS_RELEASE_REPORT.md` — "No Godot executable or Windows export package was found").
- `visualization/godot/phase_01_foundation/scripts/astra_bridge.gd` is a file-bridge that
  returns an explicitly labeled **offline interaction echo** (source-inspected via
  `ASTRA_WINDOWS_NATIVE_RUNTIME_BLOCKERS.md`); its GDExtension manifest names a Windows DLL
  **absent** from the tree.

### Q4. Only historical documentation? — `GODOT_PHASES/`: **YES** (see §3 table)

### Q5. Obsolete prototype code? — `visualization/`: **YES**

- The native renderer track's own report already certified it:
  `ASTRA_PHASE_01_NATIVE_RENDERING_REPORT.md` — "**GODOT COMPLETELY REMOVABLE: YES** — `astra.*`
  untouched, renderer swappable."
- `ASTRA_REPAIR_REPORT.md:60` recorded the README truth-correction: native C++/Vulkan is the
  end-user path; `visualization/` is a separate experimental track.
- `ASTRA_FINAL_RELEASE_AUDIT.md:219` verified production carries **no Godot dependency**.

### Q6. Anything still pointing at it? — Only documentation, enumerated in §4.

---

## 3. Per-item classification

### 3.1 `GODOT_PHASES/` (5 files) — **HISTORICAL DOCUMENTATION**

| File | Class | Notes |
|---|---|---|
| `PHASE_01_RENDERING_FOUNDATION.md` | HISTORICAL DOCUMENTATION | Spec for Godot 4.4.1 Forward+ foundation phase |
| `PHASE_02_ASTRONOMICAL_RENDERING.md` | HISTORICAL DOCUMENTATION | Spec (astronomical rendering phase) |
| `PHASE_03_EXTREME_PHYSICS.md` | HISTORICAL DOCUMENTATION | Spec (extreme-physics phase) |
| `PHASE_04_VFX_CINEMATICS.md` | HISTORICAL DOCUMENTATION | Spec (VFX/cinematics phase) |
| `PHASE_05_OPTIMIZATION_PRODUCTION.md` | HISTORICAL DOCUMENTATION | Spec (optimization/production phase) |

### 3.2 `visualization/` (136 files) — **OBSOLETE PROTOTYPE** (code) + **HISTORICAL DOCUMENTATION** (prose) + **PROTOTYPE** (assets)

| Group | Files | Class |
|---|---|---|
| `godot/project.godot`, `godot/bridge_state.json`, `godot/VERSION.md` | 3 | OBSOLETE PROTOTYPE |
| `godot/phase_01_foundation/` scenes/scripts/env (8 `.gd`, 3 `.tscn`, 1 `.tres`, icon.svg) | 13 | OBSOLETE PROTOTYPE |
| `godot/phase_02..05*/README.md` | 4 | HISTORICAL DOCUMENTATION |
| `godot/shaders/**` (19 `.gdshader` + `common_lib.gdshaderinc` + compute `.glsl`) | 21 | OBSOLETE PROTOTYPE |
| `godot/vfx/**` (10 `.tres`/`.gdshader`) | 10 | OBSOLETE PROTOTYPE |
| `godot/materials/*.tres`, `godot/extensions/*.gdextension` | 3 | OBSOLETE PROTOTYPE |
| top-level `shaders/**` (mirror copies of godot shaders, 21 files) | 21 | OBSOLETE PROTOTYPE |
| `materials/*.tres` | 2 | OBSOLETE PROTOTYPE |
| `scripts/gdscript/*.gd` (7), `scripts/cpp/` (SConstruct + gdextension_instance.cpp), `scripts/javascript/`, `scripts/node/` (2) | 12 | OBSOLETE PROTOTYPE |
| `scripts/utilities/validate_bridge.py`, `tools/evaluate_addons.py`, `tools/validate_godot_project.py` | 3 | OBSOLETE PROTOTYPE (tooling) |
| `procedural/*` (noise_generator.gd, planet_albedo.py) | 3 | OBSOLETE PROTOTYPE |
| `assets/planets/earth_like_albedo.ppm` | 1 | **PROTOTYPE asset — UNIQUE copy in the repo; PRESERVED (standing rule: never delete graphical assets)** |
| `assets/stars/star_temperature_lut.ppm` | 1 | PROTOTYPE asset — duplicates exist in `native_renderer/assets/` and `release/**` (preserved regardless) |
| `assets/manifest.json`, `assets/*/README.md` (6), `addons/extensions/third_party/*README*` | 10 | HISTORICAL DOCUMENTATION / assistant metadata |
| Top-level prose: `README.md`, `ARCHITECTURE.md`, `INTEGRATION.md`, `PERFORMANCE.md`, `QUALITY_PRESETS.md`, `SHADER_CATALOG.md`, `THIRD_PARTY.md`, `ASSET_LICENSES.md`, `DEPENDENCIES.md`, `VALIDATION_REPORT.md`, `docs/*` (3) | 13 | HISTORICAL DOCUMENTATION |

### 3.3 References in the live tree

| Item | Class | Action |
|---|---|---|
| `README.md` line 54 (experimental-track note) + tree lines 367–369 | live doc | UPDATED to archive pointer + canonical architecture block (below) |
| `RELEASE_NOTES.md:85` ("No Godot/Blender" policy) | live doc | pointer to archive added |
| `native_renderer/README.md` (NO-GODOT tag; §"Godot Comparison") | live doc | archive note added |
| `native_renderer/tools/validate_native_project.py:2` ("mirrors validate_godot_project.py") | PRODUCTION-NONLOADING comment only | comment updated with archive path |
| `release/ASTRA-COSMOS/documentation/{README,RELEASE_NOTES}.md` | shipped docs | archive pointer added |
| `ASTRA_PHASE_01_NATIVE_RENDERING_REPORT.md`, `ASTRA_PHASE_02_03_…`, `ASTRA_PHASE_4_5_…`, `ASTRA_MAXIMUM_RENDERER_REPORT.md`, `ASTRA_REPAIR_REPORT.md`, `ASTRA_CURRENT_STATE_REPORT.md`, `ASTRA_FINAL_RELEASE_AUDIT.md`, `ASTRA_COSMOS_COMPLETE_PROJECT_SUMMARY.md`, `ASTRA_WINDOWS_NATIVE_RUNTIME_BLOCKERS.md`, `ASTRA_WINDOWS_RELEASE_REPORT.md`, `ASTRA_SUPABASE_IMPLEMENTATION_REPORT.md`, `release/ASTRA-COSMOS/documentation/ASTRA_*` (3) | HISTORICAL DOCUMENTATION (time-stamped reports) | **untouched** — historical records describe then-current state; this audit supersedes them going forward |

### 3.4 Items found that are NOT Godot (guard against false positives)

- `native_renderer/src/visualization/` + `visualization_modes.{h,cpp}` — the **native**
  C++ visualization module in the production pipeline (checked by the native validator).
  **PRODUCTION DEPENDENCY — must NOT be moved or renamed.**
- All references to "visualization" as a concept in engine docs — untouched.

### 3.5 PRODUCTION DEPENDENCY / UNKNOWN

- **PRODUCTION DEPENDENCY: none** among Godot material (conclusion of §2).
- **UNKNOWN: none** — every file was opened/classified.

---

## 4. Authorized cleanup (executed after this audit)

Given §2·Q1–Q3 + Q5, the entire Godot system is confirmed **unused by the production native
pipeline**. Per the mission, obsolete runtime/prototype material was **archived, not deleted**
(move-only — every byte preserved, honoring "don't delete graphical assets" and the unique
`earth_like_albedo.ppm`):

1. `GODOT_PHASES/` → `archive/godot_legacy_2026-09-18/GODOT_PHASES/`
2. `visualization/` → `archive/godot_legacy_2026-09-18/visualization/`
3. New `archive/godot_legacy_2026-09-18/README.md` — index + classification summary + pointer back to this audit.
4. Live-doc updates: `README.md` (canonical single-pipeline architecture block + archive
   pointers), `RELEASE_NOTES.md`, `native_renderer/README.md`, release documentation,
   validator-tool comment. Historical reports untouched.
5. No source code, tests, CMake, shaders (native), or scientific functionality touched.

## 5. Canonical production visualization architecture (now the single documented path)

```
ASTRA Scientific Engine
        │
        ▼
   RenderState
        │
        ▼
Native C++ Renderer
        │
        ▼
      Vulkan
        │
        ▼
       GPU
```

(as documented in `README.md` after this cleanup: Scientific Engine → Scientific State →
RenderState / Visualization API → Native C++ Renderer → Vulkan → GPU → Display)

## 6. Regression battery after cleanup — empirical results (2026-09-18, this session)

Executed after the archival moves, from scratch (regenerated all 7 reference CSVs from the
Python authority, rebuilt every native binary — the session's /tmp had been wiped):

- **7/7 native mirror checkers: RESULT: PASS** — destruction (16 112 comparisons), evolution
  (320), observation (158), relativity (0 ulp), kepler (max rel 3.55e-13 ≤ 1e-9), nbody
  (drift 1.2e-10), blackhole+spacetime (0 ulp).
- **10/10 native gate suites: PASS** — v04 · v05 (2984) · v06 (645) · v07 (109) · v08 (28) ·
  v09 (28) · v10 (69) · v11 (121) · v13 · v13_viz (225 anti-purple checks).
- **Python authority: 1535 passed, 0 failed** (`pytest tests/ -q`, ~29 s).
- **Native project validator: 236 OK, 0 FAIL** (`native_renderer/tools/validate_native_project.py`).

Identical green status to pre-cleanup — no test relies on any archived file (§2·Q1 evidence).
