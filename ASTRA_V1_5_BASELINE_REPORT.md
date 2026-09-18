# ASTRA v1.5 — BASELINE REPORT (Phase 0, pre-implementation)

**Date:** 2026-09-19 (Asia/Calcutta) · **Purpose:** freeze the verified v1.4 baseline before any v1.5 change.

## 1. Git state (verified, this machine, this session)

| Item | Value |
|---|---|
| Branch | `arena/01a0b082-astra-repair` |
| HEAD (local) | `790418ec49a5a7989aff09ee0626e1c2db6d119b` (`790418e` — "v1.4: observatory report") |
| Remote HEAD | `790418ec49a5a7989aff09ee0626e1c2db6d119b` — **HEAD == REMOTE (confirmed via `ls-remote`)** |
| Working tree | **clean** (`git status -sb` shows branch line only) |
| Stash | none |
| Reconstruction note | This sandbox was rebuilt mid-session (fresh clone). The working tree was verified **blob-by-blob: 1185/1185 files hash-identical** to the pushed commit before the branch ref was realigned. Nothing was discarded; nothing unknown existed. |

Full tracked history: v1.0 (foundation) → 5ef0251/534739e (v1.2→v1.3) → c38a092 → 844ef4d (v1.3 landing) → 4b53006 → 22dab74 (v1.4 main) → b9b9bb8 → **790418e (v1.4 final)**.

## 2. Validation battery at baseline (freshly re-executed on this checkout, 2026-09-19)

| Suite | Result |
|---|---|
| `pytest tests/ native_renderer/tests/` (PYTHONPATH=.) | **1667 passed, 0 failed, 0 skipped** |
| 7 mirror checkers (native) vs regenerated reference CSVs | destruction/evolution/observation/relativity/blackhole_spacetime/kepler **PASS**; nbody **PASS** (drift 1.204e-10) |
| v04 gates | 562/562 PASS |
| v05 gates | 2984/2984 PASS |
| v06 gates | 645/645 PASS |
| v07 gates | 109/109 PASS |
| v08 gates | 28/28 PASS |
| v09 gates | 30/30 PASS |
| v10 gates | 69/69 PASS |
| v11 gates | 121/121 PASS |
| v13 gates | 116/116 PASS |
| v13-viz gates | 225/225 PASS |
| **v14 gates** | **112/112 PASS** (parity worst |dRA| 2.27e-13 °) |
| `validate_native_project.py` | **240 OK / 0 FAIL** |
| Shader compilation (glslangValidator) | v1.4 production SPV pass (validator rebuilt from source: **11:16.6.0**) |

## 3. Dataset hashes (re-verified this session against pinned manifest)

| Artifact | sha256 |
|---|---|
| `native_renderer/assets/astro_stars.v14.bin` | `e3e52905…e47662` (matches emitted body+header) |
| `native_renderer/assets/astro_dso.v14.bin` | `f537ec34…07197b` |
| Raw HYG v4.1 CSV (runtime input) | `d9f69fd8…4ebd` == registry pin ✔ |
| Raw OpenNGC `NGC.csv` | `be150bda…cfae` == registry pin ✔ |

## 4. Existing v1.5-relevant inventory (inspection, Phase 1)

Already present and tested: `astra/theoretical/` (MorrisThorneMetric, EinsteinRosenMetric, AlcubierreMetric, WhiteHoleMetric, energy_conditions, classification — 1039 LOC, `tests/test_theo_*.py` 6 suites green), `astra/temporal/` (proper_time, observation, exotic, state, events — `tests/test_temporal_*.py` 6 suites green), `astra/spacetime/` (metric, geodesics, causality, curvature, events, connection, api), `astra/interaction/` (engine FSM TRAVELLING→IN_TRANSIT→ARRIVING, DelegatingTravelService with provider protocol; **no authoritative provider injected — NullTravelProvider**), `astra/core/events.py::EventBus`, `astra/core/persistence.py`, native `audio_bus` (classified AudioEventKind), native BH/spacetime overlay (F4), v1.4 catalog/observatory/keys C,U,T.

**Missing for v1.5 (gap list):** explicit wormhole traversal FSM (IDLE/APPROACHING/ENTRY/TRANSIT/EXIT/COMPLETE/ABORTED/INVALID); authoritative journey/travel engine (fills the Null provider); travel-integrated explorer observer (position/velocity/frame/proper+coordinate time); native (C++/double) mirrors of wormhole/warp/travel/relativistic helpers with fixture parity; renderer visualization of wormhole mouths/throat, warp bubble, causal cone, observer trajectory through RenderState→GPU; HUD travel rows; travel persistence round-trip + tamper; EventBus-travel events + classified audio; adversarial + determinism + performance evidence; reports (this file, main, independent verification).

## 5. Honest limitations carried into v1.5

- **No Vulkan device / no GPU / no Windows runtime** in this environment → raster, draw execution, GPU timings, Windows behavior remain **NOT VERIFIED — ENVIRONMENT LIMITATION** (shader compilation IS verified via glslangValidator).
- Live astronomy/archive endpoints remain TLS-blocked from this sandbox (measured) → no v1.5 data source beyond the verified v1.4 ones is attempted; **no fabricated data**.
- Wormholes/warp drives are **THEORETICAL/SPECULATIVE** per the package mandate: never presented as real phenomena.

*Signed: implementation engineer, pre-change baseline. Any claim beyond this list is measured, not asserted.*
