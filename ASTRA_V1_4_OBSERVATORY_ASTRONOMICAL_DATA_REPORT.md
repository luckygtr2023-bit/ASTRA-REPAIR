# ASTRA v1.4 — OBSERVATORY + REAL ASTRONOMICAL DATA

**Branch:** `arena/01a0b082-astra-repair` · **Date:** 2026-09-18 (Asia/Calcutta) · **Status:** COMPLETE (all 11 phases) with explicit ENVIRONMENT LIMITATIONS where the sandbox blocked verification.

Mission rule honored throughout: *DATA/SCIENCE → STATE → RENDERSTATE → GPU → ACTUAL PIXELS*. No fake data, no invented observations, no disconnected HUD, no decorative substitutes, no weakened tests, no fabricated runtime claims.

---

## 1. Executive summary

| Phase | Deliverable | Status |
|---|---|---|
| 1. Real astronomical data | HYG v4.1 (119 280 stars ingested) + OpenNGC (13 962 DSOs), both sha256-pinned, open-licensed | **MEASURABLY REAL — INSTALLED** |
| 2. Deterministic ingestion | `astra/catalog/` strict parser, null-as-null, self-consistency gates, byte-identical re-emission | **PASS (tests assert)** |
| 3. Observatory / measurement | `astra.catalog.measure` + native mirror `catalog::measure_star*` — observer-relative angles (real parallax), distances, delays, magnitudes, FOV, angular separation, PM-shifted epochs | **PASS (30-fixture cross-language parity ≤ 2.3e-13 deg)** |
| 4. Renderer integration | binary catalog → double-km CPU state → float instance SSBO → instanced additive billboards → HUD | **INTEGRATED; GPU raster NOT VERIFIED (no Vulkan device)** |
| 5. Observation modes | catalog layer `C` (stars / stars+DSO) · measurement `U` · historical epoch `T` (J2000 ± 20 kyr) · inspector rows with observer reference frame | **INTEGRATED** |
| 6. Provenance chain | manifest JSON, SOURCE→NORMALIZATION→EMITTED; per-field classifications through to HUD | **PASS** |
| 7. HUD / inspector | 19 new rows incl. NOT AVAILABLE cells; selection identity == record | **PASS (112 native gates inspect the rows)** |
| 8. Tests | 40 pytest + 112 C++ gates + full battery rerun | **ALL PASS — no weakening, no regressions** |
| 9. Performance | instanced SSBO (3.82 MB), origin-change rebuild 1.46 ms, selection 0.70 ms | **MEASURED CPU; GPU NOT VERIFIED** |
| 10. Anti-purple | all 10 mission checkpoints exercised natively (see §10) | **9/10 verified on CPU; final pixel check = ENVIRONMENT LIMITATION** |
| 11. This report | — | **this file** |

### What v1.4 changes on screen (when run on the Windows production target)
- `C` toggles the REAL sky: 119 280 real stars at their true J2000 directions (magnitude-true brightness ordering, spectral-class LUT colors) and 13 962 real deep-sky objects at catalog directions with their true angular sizes.
- `U` measures the object nearest the view center and the HUD reports its real distance, light-travel time, observer-relative RA/Dec (which *changes* as you move the camera target between Sun-planets — true stellar parallax from real data), apparent magnitude, spectral type, and provenance (`REAL DATA (HYG v4.1)`).
- `T` moves the observation epoch J2000 ± 20 000 years using the real proper motions (DATA-DERIVED small-angle approximation, labeled).
- The procedural sky background is retained underneath, still labeled `PROCEDURAL/CINEMATIC`.

---

## 2. Datasets — what is REAL, where it came from, why it is legal

**The sandbox egress filter blocked every direct astronomy-archive endpoint** (measured 2026-09-18; see §12). The retrieval that *did* work was GitHub codeload — and both target catalogs are *primarily distributed by their authors on GitHub*. No mirrors of convenience, no fabricated substitutes.

| # | Dataset | Version | Records seen → accepted | Source of truth | License |
|---|---|---|---|---|---|
| 1 | **HYG star database** | 4.1 | 119 626 → 119 280 | `astronexus/HYG-Database` repo zip via codeload; raw CSV sha256 `d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd` | **CC BY-SA 4.0** |
| 2 | **OpenNGC** (NGC/IC deep-sky) | master @ 2026-07-26 (`da90466`) | 13 970 → 13 962 | `mattiaverga/OpenNGC` repo zip via codeload; NGC.csv sha256 `be150bdaa1997dacbcb39f303074403edec7a953b589b36d5f1c4522c0cc6fae` | **CC BY-SA 4.0** |

Scientific content:
- HYG v4.1 = Gaia-DR3/Hipparcos/Gliese-derived: RA/Dec (epoch AND equinox J2000.0), distances in pc, proper motions (mas/yr, μ_α* convention — verified against raw α-Cen values before use), V magnitudes, spectral classes, variable-star ranges. **10 224 rows carry the documented missing-parallax sentinel** → normalized to *distance NOT AVAILABLE*; x,y,z geometry of those rows (a fictitious 100 kpc shell written by upstream) is never ingested. **Sol itself (id 0) is excluded explicitly** — the engine's central star is the authority; duplicating it would fork the truth (recorded in the manifest).
- OpenNGC = NED-based: real RA/Dec, V magnitudes (4 214 objects), angular sizes (12 010), **10 583 real redshift measurements**, radial velocities, Hubble classes, Messier aliases (107 objects), per-field source codes preserved in the Python records.

**NOT AVAILABLE datasets (measured blockage —never replaced by inventions):** Gaia DR3 live TAP, JPL Horizons, NASA Exoplanet Archive, SIMBAD. Recorded in `astra/catalog/sources.py::UNAVAILABLE_SOURCES` with probe evidence. Transients/AGN/FRBs/lensing events: honest NOT AVAILABLE (no verified reachable source in this environment).

## 3. Deterministic ingestion (`astra/catalog/`)

Chain of custody, enforced and recorded:

```
SOURCE bytes (sha256 verified against registry; mismatch → ProvenanceMismatchError)
→ strict schema parse (34 HYG / 32 NGC columns; header-shape enforced)
→ NORMALIZATION
     ra: decimal HOURS ×15 (README says "ra"; values proved hours)
     dist ≥ 100 000 pc → None (never clamped to a fake distance)
     blanks → None (NULL stays NULL; zero is never substituted)
     upstream x,y,z self-consistency gate: |file − ra/dec×dist| ≤ 5e-5 rel
         (measured empirically p99 = 1.5e-5; 343 rows above → REJECTED,
          counted + sampled in the manifest)
     ra-hours / rarad disagreement > 3e-4° → REJECT (2 rows caught)
→ EMITTED binaries (little-endian; sorted; body sha256 in a 64-byte header;
     stars 68 B/record, DSOs 80 B/record; spectral table trailer)
→ re-emission is BYTE-IDENTICAL on identical input (pytest asserts)
```

Measured economics of the real data: **345 of 119 626 HYG rows (0.29 %) were rejected** because upstream's own numbers disagree with themselves (line numbers + reasons in the manifest — visible, not silent), 8 of 13 970 OpenNGC rows (7 blank RA/Dec, 1 out-of-range Dec). **Barnard's Star and α Centauri A both fail the self-consistency gate** (high-PM stars are upstream's worst offenders) and are *not* in the catalog — the fixture generator documents this. Upstream's `pmdec=9999.99` clamp on Barnard's Star is ingested verbatim, never "corrected".

## 4. Observatory / measurement implementation

Python authority (`astra/catalog/measure.py`) + 1:1 native mirror (`native_renderer/src/catalog/astro_catalog.cpp`):

| Quantity | Formula / basis | Classification |
|---|---|---|
| Direction from observer | `normalize(star_km − observer_km)` (true geometry → **real parallax**) | DATA-DERIVED |
| Distance from observer | Euclidean; catalog pc × 3.0856775814913673e13 km (IAU 2015 B2) | DATA-DERIVED |
| Light-travel time | distance / c (exact constants) | DATA-DERIVED |
| Apparent magnitude | M + 5 log₁₀(d/10pc) — reproduces catalog Sirius −1.44 from absmag 1.454 + 2.6371 pc | DATA-DERIVED |
| Observer RA/Dec | measured direction, back-converted ICRS for HUD parity | DATA-DERIVED |
| Proper-motion epoch view | small-angle μ_α*/cos δ update at J2000 + t (±20 kyr clamp) | DATA-DERIVED (approximation labeled) |
| Angular separation | atan2(‖a×b‖, a·b) robust form | DATA-DERIVED |
| FOV containment | cone test vs camera fwd (tan(fov/2)) | DATA-DERIVED |
| Redshift (DSO) | **catalog-measured z; never computed by us** | REAL DATA |
| DSO distance proxy | c·z/70 Mpc — only when z was measured | DATA-DERIVED (approximation labeled) |
| Star temperature | spectral class → Mamajek 2013 dwarf-sequence table | PHYSICALLY MODELED; 3 733 NaN = NOT AVAILABLE |
| Formal uncertainties | **none shipped in these CSVs** | NOT AVAILABLE (never invented) |

**Parallax proof:** the synthetic 1-pc/1-AU case returns exactly 1.000000 arcsec (the parsec definition itself) in both languages; moving the measurement observer by 1 AU around Sirius changes its measured RA/Dec — asserted in the battery.

## 5. Cross-language parity (the anti-fake backbone)

`scripts/gen_v14_reference_fixtures.py` reads the **committed binary** (the same bytes the native loader sha256-verifies) and measures 4 chosen records (Sirius 32263, two highest-surviving-PM stars, one shell-only star) from 3 observers × up to 3 epochs = **30 reference measurements**. The native `v14_gates` reproves every one: worst angular deviation **2.27e-13 degrees**, worse for PM-shifted epochs ≤ 5e-4° (documented f32 proper-motion storage), NaN-shapes identical.

## 6. Renderer integration (Windows/Vulkan production target)

- **Load (fail-closed):** `catalog::load_*_catalog` refuses bad magic/version/size and any body-hash mismatch (a 1-bit flip is proven to fail closed by the gates). On failure the pipeline continues with `CATALOG DATASET: NOT AVAILABLE` HUD rows and **zero substituted stars**.
- **Frame conversion:** file ICRS-parsecs → heliocentric-ecliptic-J2000 double kilometers using the IAU mean obliquity 84 381.448″ (round-trip asserted to 1e-12; engine stays JPL-ecliptic-consistent).
- **Double→float boundary:** positions stay `double` km until `build_star_viz` emits the 32-byte `CatStarViz` instances (shell-projected scene units — a documented CINEMATIC display sphere of radius 90 000 < z_far 100 000, sized below the existing procedural background). Float appears only at this boundary, exactly like the v1.3 batches.
- **GPU resources:** two host-coherent mapped SSBOs (stars 3.82 MB, DSOs 1.12 MB) + 2 descriptor sets (pool sized 12/12) + 2 pipelines (additive blending, depth-test on / depth-write off, so solar-system bodies correctly occlude background sky). Rebuilds happen only on observer-origin moves ≥ 1e5 km — **no per-frame CPU list regeneration**.
- **Shaders:** `v14_cat_stars.{vert,frag}` (instanced view-space billboards sized in pixels from REAL magnitudes, LUT colors) + `v14_cat_dso.{vert,frag}` (REAL-direction, REAL-angular-size soft glows). All four compile to SPIR-V through **glslangValidator 11:16.6.0** (the version the project records); added to `ASTRA_PRODUCTION_SHADERS` in CMake. *Shader compilation ≠ shader execution; raster is NOT VERIFIED here.*

## 7. Keys & HUD

| Key | Effect |
|---|---|
| `C` | catalog layer: OFF → STARS → STARS+DSO (HUD shows layer state) |
| `U` | measure catalog object nearest to screen center (star + DSO) |
| `T` | observation epoch step J2000 ±10 kyr (±20 kyr max; DATA-DERIVED PM view) |

HUD (19 new rows, all with explicit classification): dataset row with counts + `REAL DATA (CC BY-SA 4.0)`; layer state; selected star `HYG <id> / HIP <id>`; spectral type; `RA/DEC FROM OBSERVER` (+ epoch tag when `T` active); observer distance in ly / light-travel time in yr; apparent magnitude; `FORMAL UNCERTAINTY: NOT AVAILABLE`; DSO identity/type (GALAXY…)/redshift z / z→D proxy (`DATA-DERIVED (H0=70 Hubble proxy)`)/V magnitude/angular size; unavailable-dataset row. **The HUD selection IS the catalog record** (index identity; the gates read both and compare).

## 8. Test results (full battery rerun — nothing weakened)

| Suite | Result |
|---|---|
| pytest (whole repo) | **1667 passed, 0 failed** (was 1535 passing pre-v1.4; +42 new v1.4 tests, plus previously-skipped headless-native tests now running) |
| v1.4 C++ gates (`tests/v14_gates.cpp`) | **112/112 PASS** |
| v13 viz gates | 225/225 PASS |
| v13 gates | 116/116 PASS |
| v04–v11 gates | 562 / 2984 / 645 / 109 / 28 / 30 / 69 / 121 — all PASS |
| 7 mirror checkers | destruction 16 112 comps (15 370 bit-exact), evolution 320 (197), observation 158 (127), relativity + black-hole/spacetime 0 ULP error, kepler 3.55e-13, nbody drift 1.2e-10 — all PASS |
| v1.4 contract pytest (`tests/test_v14_catalog.py`) | **40/40 PASS** (incl. full-pipeline byte-identical re-emission; falsified-fixture tests refute tampered rows) |
| `validate_native_project.py` | **240 OK / 0 FAIL** |
| Eclipse of failed things caught | tampered star binary (refused), fabricated fixture `rarad` (refused by tests — a parser-honesty proof), Barnard/α-Cen truth conflicts (excluded) |

## 9. Performance (REAL measured CPU, sandbox host, single thread)

- Catalog load incl. full sha256 body verification + frame conversion: **33.6 ms** (startup, one-shot).
- `build_star_viz` for all 119 280 stars: **1.46 ms** per rebuild (origin-change only).
- Selection scan: **0.70 ms** on keypress.
- GPU frame cost / raster benchmarks: **NOT MEASURED — ENVIRONMENT LIMITATION** (no Vulkan device; never claim otherwise).

Instancing, single mapped AO batch per catalog, hard 32-byte instance layout, no dynamic allocation per frame — the Phase 9 structure is present; LOD for catalog objects beyond magnitude/shell discipline is documented as future work, not hidden.

## 10. Anti-purple verification (mission Phase 10)

1. Catalog objects visible → gates assert every instance finite, on-shell, sized in [1,9] px, additive-blended (text contract on the pipeline). Pixel-level: NOT VERIFIED (no raster) — **ENVIRONMENT LIMITATION**.
2. Positions from ingested data → Sirius binary record = 2.6371 pc exactly; 30/30 parity fixtures pass.
3. Selection hits real records → `U` paths asserted; HUD identity == catalog id.
4. Observer-motion changes observation → 1 AU move ⇒ measured RA/Dec change (parallax) and 0.3737″ max shell shift (1 854″ proxima-case bounded) — gates measure it.
5. Time-change changes observed state → `T` epoch shift alters HUD values for high-PM stars (fixtures EP/EM).
6. HUD == rendered object → same index feeds both; gates inspect both.
7. Renderer consumes catalog-derived GPU data → text-contract gates on load→SSBO→bind→draw ordering (bg → catalog → bodies).
8. No purple/magenta placeholder color → every instance RGB predicate-checked.
9. No decorative fake galaxy → DSO layer = OpenNGC records only; CINEMATIC-classified quantities are labeled in HUD text.
10. NOT AVAILABLE never faked → 3 733 NaN temperatures, DSO z-absent cells, missing-parallax distances, formal uncertainties.

## 11. Provenance chain (recorded verbatim in `assets/astro_catalog_manifest.json`)

SOURCE (2 verified upstream files + URLs + licenses + retrieval routes + sha256) → NORMALIZATION (6 transform statements incl. sentinel rules, gate tolerance, Mamajek table, Hubble proxy, verbatim-clamp statement) → EMITTED-COMPUTATIONS (this pipeline) → OUTPUTS (counts, record sizes, body sha256) → INGEST (census, rejections histogram, 25-line samples, exclusions) → NOT_AVAILABLE_STATEMENTS.

## 12. Limitations & exact verification status

- **GPU raster, draw execution, visual eyeballing, frame timings:** NOT VERIFIED — ENVIRONMENT LIMITATION (no Vulkan device on Linux CI). Shader *compilation* is separately asserted; execution is not claimed.
- **Live archive endpoints:** BLOCKED — ENVIRONMENT LIMITATION (TLS terminated by the sandbox proxy for all non-GitHub astronomy hosts; measured with curl (35) SSL_ERROR_SYSCALL + urllib TLS EOF traces). The retrieval pins real frozen bytes instead — legitimate primary-distribution route.
- **Distances for 10 224 stars:** NOT AVAILABLE by upstream (handled as honest shell direction, HUD shows NOT AVAILABLE — not a pipeline defect).
- **Formal uncertainties, transients, AGN, FRBs, lensing, exoplanets ephemerides:** NOT AVAILABLE (no verified reachable data) — visible in sources.py + manifest.
- **HYG→ecliptic axes**: engine worlds are JPL-ecliptic; catalog rotates with IAU mean obliquity at load — small-axisync mismatch vs exact JPL orientation modeling is documented, not "fixed".
- **PM epoch view ±20 kyr small-angle approximation** (labeled DATA-DERIVED); not a relativistic propagation model.
- **DSO z→Mpc** is a linear Hubble proxy (labeled approximation), valid for the qualitative magnitude scale only.
- **Release snapshot tree** (`release/ASTRA-COSMOS/`) predates v1.4 and was intentionally not rewritten; packaging pulls `native_renderer/assets/*` at build time — new binaries ride along the same path the LUT does.
- **Spectral temperatures** are PHYSICALLY MODELED (dwarf-sequence table), never presented as measurements; HUD keeps the MK type itself REAL.

## 13. Changed / new files

| File | Change |
|---|---|
| `astra/catalog/{__init__,sources,errors,transform,hyg,openngc,spectra,measure,pipeline}.py` | **NEW** ingestion/measurement authority (9 modules) |
| `scripts/gen_v14_catalog.py`, `scripts/gen_v14_reference_fixtures.py` | **NEW** deterministic exporters |
| `native_renderer/src/catalog/{astro_catalog.h,.cpp,sha256.h}` | **NEW** native consumer + integrity |
| `native_renderer/src/shaders/v14_cat_{stars,dso}.{vert,frag}` | **NEW** compiled GLSL |
| `native_renderer/assets/astro_stars.v14.bin` (8.1 MB), `astro_dso.v14.bin` (1.1 MB), `astro_catalog_manifest.json` | **NEW** verified runtime catalog + provenance |
| `native_renderer/src/main_production.cpp` | catalog init/SSBOs/descriptors/pipelines/draw/keys/HUD fill/cleanup (+334) |
| `native_renderer/src/app/hud_state.{h,cpp}` | v1.4 snapshot fields + 19 rows |
| `native_renderer/tests/v14_gates.cpp`, `tests/test_v14_catalog.py`, `native_renderer/tests/fixtures/v14_measure_reference.txt`, shader-compile test addition | **NEW** test authority |
| `native_renderer/CMakeLists.txt` | +4 production shaders |
| `visualization/` (136 files) | **REMOVED from tracking** — completes the Godot legacy audit move (identical bytes already archived at `archive/godot_legacy_2026-09-18/visualization/`; `git diff -r` verified) |

## 14. Commit sequence (this session)

| Order | Commit | Content |
|---|---|---|
| 1 | `22dab74` | v1.4 observatory + real astronomical data (everything above) |
| 2 | `b9b9bb8` | CMake runtime-assets install step |
| 3 | this file finalized | report hash backfill |

`HEAD == origin/arena/01a0b082-astra-repair` confirmation is stated in the
session's final message.

---
*Every number in this report is either a measured value from this session's runs quoted verbatim, a catalog constant from the pinned upstream bytes, or an explicit NOT VERIFIED / NOT AVAILABLE statement.*
