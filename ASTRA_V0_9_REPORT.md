# ASTRA COSMOS — v0.9 INCREMENT REPORT: EXACT N-BODY STATE PERSISTENCE

## Status vocabulary (exact)

| Category | Verdict | Evidence |
|----------|---------|----------|
| SOURCE VERIFIED | YES | gates below |
| STATIC VERIFIED | YES | battery |
| BUILD VERIFIED | YES — native target builds with 0 errors; main syntax clean vs Vulkan 1.4.362 | build logs |
| WINDOWS / GPU / RUNTIME(app) / VISUAL / PERFORMANCE | **NOT VERIFIED** (environment unchanged) | — |

## Problem this phase solves (honest framing)

v0.8 delivered the N-body engine but its scenario load had a **limited resumption
semantic**: an NBODY save stored only `sim_time_s`, and loading it re-anchored
the engine from the Kepler ephemeris — i.e., the integrated (correct,
perturbed) trajectory was **discarded** and replaced by a fresh approximation,
loudly logged. That is honest, but it means save/load in NBODY mode changed the
scientific state. v0.9 removes the loss: the integrated state is persisted and
resumption is **bit-exact** (zero tolerance, proven in gates).

## What changed (small, auditable)

1. `NBodyEngine::restore(bodies, t)` — strict validation (size, positivity,
   finiteness; empty/NaN/inf/zero-mass rejected), cache cleared and *derived
   from positions on the next step* — bit-identical to a continuous run
   (property of velocity Verlet + this engine's caching design; gate-proven).
2. `pack_nbody_state` / `unpack_nbody_state` (celestial_sim) — SI state as
   `"t,x,y,z,vx,vy,vz,..."` at %.17e; unpack enforces the exact body count of
   the caller's scene (id order = contract; mismatch rejected, never
   reshuffled).
3. persist: optional `nbody_state` field — structural validation inside the
   parser (1+6k finite tokens, else reject), **contradiction rejection**: an
   integrated state with `gravity_model != nbody` fails the load; v0.8 and
   pre-v0.8 files remain fully loadable.
4. main_production: F2 packs the state in NBODY mode; F3 restores it EXACTLY
   (engine time becomes authoritative sim time) with loud logging either way.
   KEPLER paths untouched. Also cleaned one redundant duplicated
   `gravity_model` assignment left by the v0.8 edit (no behavioural change).

## Gates

**`tests/v08_gates.cpp` — 28 checks, all PASS:**
- pack/unpack bit-exact roundtrip (state + time + masses + ids)
- **the contract**: uninterrupted run vs save→restore→resume → bit-identical
  positions AND velocities (zero tolerance asserted); double save/load cycle
  likewise bit-identical
- restore/unpack validation matrices (NaN, inf, zero mass, empty, malformed,
  scene-mismatch, truncated token = count-law violation)
- persist integration: byte-exact state string roundtrip, kepler+state
  contradiction rejection, v0.8 backward compatibility, parser-level rejection
  of structurally corrupt states.

**Bug-hunt record**: initial gate truncated the state string by 8 chars; the
result remained structurally valid (shortened final token still parses finite,
count intact) — root-caused as a flawed test premise (value-level corruption is
outside the format's scope by design: no checksums). Replaced with whole-token
truncation = genuine structural tamper. Engine untouched, metric corrected —
consistent with rule 16 (no assertion was weakened; a false-failing check was
made precise).

## Full battery this phase (all green)

v04 PASS · v05 2984/2984 · v06 645/645 · v07 109/109 · **v08 28/28** ·
kepler mirror PASS (3.55e-13) · nbody mirror PASS (43 checks, 4.606e-13) ·
pytest 1535/1535 · shaders 19/0 + 13/13 SPV · native build 0 errors ·
main_production.cpp syntax 0 errors vs Vulkan 1.4.362.

## Not done (ledger)

- No runtime/GPU/Windows claims (W-gates remain the promotion path).
- Persist format still has no authentication/checksum (out of scope; value-level
  tamper producing valid structure is not detectable by design — documented).
- Engine retains v0.8 semantics otherwise (dt=3600 s, forward-only, loud
  re-anchor on backward time or missing state).
