# ASTRA v1.6 — COSMIC AUDIO REPORT

**Branch:** `arena/01a0b082-astra-repair` · **Date:** 2026-09-19 · **Baseline:** v1.5 verified @ `f230fc4` · **Scope:** Prompt-package v1.6 (Cosmic Audio: classifications, provenance/license/units/transforms, determinism, EventBus integration, scientific audio inspector).

Mission rule honored: *every audio stream carries an explicit classification; nothing is "just a sound"; nothing claims to be recorded physics unless provenance documents it.* No recordings, licenses, datasets, or GPU/device results were fabricated. The audible device layer remains **NOT VERIFIED — ENVIRONMENT LIMITATION** (Windows/audio host absent); the honest architecture (bus + authority + mirror) is fully verified CPU-side.

---

## 1. What exists now (v0.4 bus → v1.6 authority)

The repo already carried the fixed 7-class vocabulary + honesty policy in the native `AudioBus` (`native_renderer/src/app/audio_bus.*`, v0.4; extended with TRAVEL_* kinds in v1.5). v1.6 adds the missing **data layer** around it:

| Component | File | Role |
|---|---|---|
| Classification + policy (Python mirror) | `astra/audio/classification.py` | 7 classes + 18 event kinds — names byte-identical to native; `request_is_honest` mirrors the native policy **rule-for-rule** (no divergence; v1.6 layers only ever ADD refusal rules, never weaken — rule 16) |
| Provenance/license/units | `astra/audio/provenance.py` | `Provenance{source,citation,license,data_units,retrieved}`; fail-closed `validate_provenance` (data classes REQUIRE provenance; NOT AVAILABLE forbidden where it would launder a real-data claim; PHYSICALLY_MODELED must reference an in-repo `astra.*` model; no stream without a license string) |
| Deterministic transform chain | `astra/audio/transforms.py` | `resample_linear` / `normalize` / `gain_db` + `quantize_int16` with an exact cross-language op-order spec; canonical-serialized chains |
| Model synthesizers | `astra/audio/synthesis.py` | `sine`, `chirp` (phase-integral), `orbital_hum` — period **delegated** to `astra.orbital.period.orbital_period` (existing authority, no re-derivation) |
| Registry | `astra/audio/registry.py` | fail-closed registration; canonical JSON + sha256 checksums; `standard_registry()` with three honest built-ins (see §3) |
| Inspector | `astra/audio/inspector.py` | "what is playing?" → classification/provenance/license/units/chain/checksum; unknown name = **refusal, never a guess**; `verify_consistency()` cross-check |
| Request bus | `astra/audio/bus.py` | parity of the native bus incl. drop-oldest-at-256; publishes through the **existing** EventBus-style hook (same duck-typed pattern as v1.5 JourneyEngine) |
| Native PCM mirror | `native_renderer/src/app/audio_synth.{h,cpp}` | transforms + quantize + synthesizers mirrored with identical op order; orbital period mirrored as the same formula tree (`2*pi*sqrt((a*a)*a/mu)`) |

## 2. What was proven (measured this session)

| Suite | Result |
|---|---|
| `tests/test_v16_cosmic_audio.py` | **36/36** (vocabulary, policy vectors, provenance refusal matrix, transforms, synthesis determinism, registry, inspector, bus capacity/event integration, two-pass bit-identical whole-pipeline rerun) |
| `native_renderer/tests/v16_gates.cpp` | **304 checks, 0 failures**: 57 policy-parity vectors read from the committed fixture and executed against the **native** `request_is_honest`/`default_classification` (real cross-language check), 211 quantized-PCM comparisons **bit-exact**, adversarial refusals, determinism |
| Fixture | `native_renderer/tests/fixtures/v16_audio_reference.txt` (263 records; generator `scripts/gen_v16_reference.py`) |
| Full regression | **1749 pytest passed** · v04–v16 gates all PASS · 7 mirror checker suites PASS · validator **242/0** |
| Bug found by gates during build | native `tr_resample_linear` read-back corruption under in/out aliasing — fixed at the source (alias-safe), gates re-green |

## 3. The standard streams — and why each is honest

| Stream | Class | Why the class | License/provenance |
|---|---|---|---|
| `ui.tick` (880 Hz, 20 ms) | CINEMATIC | UI feedback, explicitly not physics | ASTRA project license, generated in-repo |
| `journey.begin` (220→660 Hz chirp) | SPECULATIVE | traversal/warp are unestablished physics; the cue is **not and cannot be a recorded sound** | ASTRA project license, generated in-repo |
| `orbit.earth_hum` (period-mapped 55 Hz) | SCIENTIFICALLY_INTERPRETED | interpretive mapping of a **computed** Kepler period; documented that space is vacuum — never described as emitted sound | ASTRA project license; source = `astra.orbital.period.orbital_period`, units `s` |

**Zero streams claim to be recordings.** `REAL_SIGNAL_SONIFICATION`/`DATA_DERIVED`/`REAL_ACOUSTIC` registrations are supported by the machinery but *would require real datasets with real licenses* — none are shipped, rather than shipping fabricated ones.

## 4. Honesty policy (unchanged native rules, mirrored exactly)

1. `REAL_ACOUSTIC` is refused on the bus **always** (mechanical sound claims never pass in ASTRA space contexts).
2. `REAL_SIGNAL_SONIFICATION` with an empty subject = fabricated signal claim → refused.
3. `SPECULATIVE` riding the established-physics `IMPACT_MODELED` kind → refused.

v1.6 provenance validation sits **on top** (registration-time), refusing: missing provenance for data classes; empty source/license; `NOT AVAILABLE` source/citation under a data-class claim; PHYSICALLY_MODELED without an in-repo `astra.*` model reference; any stream without a license string.

## 5. Determinism + parity contract

- Buffers: mono float64 in [-1, 1]; transforms pure; quantization spec: `q = clamp(floor(x*32767+0.5) if x>=0 else ceil(x*32767-0.5))` — identical integer outputs across languages (**asserted exactly**, not with tolerance).
- Float channels (interpretive carrier freq) asserted at measured tolerance **1e-12**.
- No RNG anywhere in the audio layer; two-pass pipeline (registry render + bus + inspector checksums) is **bit-identical** (test).
- Orbital period uses the same formula tree in both languages (`(a*a)*a` to match CPython float pow).

## 6. EventBus integration (existing bus only)

`AudioRequestBus(event_hook=...)` publishes `audio_request` events through the same duck-typed hook pattern the v1.5 JourneyEngine uses with `astra.core.events.EventBus`. Dishonest requests are dropped **and counted** — they never publish an event (test-enforced).

## 7. Verification status (explicit)

- **RUNTIME-VERIFIED (CPU, both languages):** policy parity, PCM parity, refusals, determinism, registry/inspector, full regression.
- **NOT VERIFIED — ENVIRONMENT LIMITATION:** audible Windows audio device output, device latency/underruns, mixer behavior. Nothing in this report implies device playback occurred.
- **NOT IN SCOPE:** fabricating real recordings ("Voyager plasma waves" etc.); a real-data sonification stream may be added later ONLY with genuine provenance + license.

## 8. Limitations (recorded, not hidden)

- Modest built-in set (three streams) — by design: honesty over abundance.
- `orbital_hum` mapping is interpretive by construction (octave mapping documented in code + citation); frequency guard refuses carriers above Nyquist rather than aliasing silently.
- No mixer/oscillator polyphony yet — bus-level event routing only (device layer is the Windows follow-up).
- Quantized PCM parity is exact on measured glibc/libm; MSVC sin() differences, if any, land within the 1e-12 float-channel tolerance and the int16 quantize step — recheck on the Windows run.

*Evidence beats confidence: every claim above carries a matching executed test or a refusal that executed.*
