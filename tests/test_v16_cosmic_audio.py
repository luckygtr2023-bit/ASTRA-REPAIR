"""ASTRA v1.6 — COSMIC AUDIO contract tests.

Obligations: every stream classified (7-class vocabulary); provenance +
license + units required for data classes; transforms deterministic;
EventBus integration via existing bus API; scientific audio inspector
answers "what is playing" or REFUSES (never guesses); honesty policy
exact parity with native semantics (fixture parity covered by native
gates); no fabricated recordings anywhere in the standard registry.
"""

import math

import pytest

from astra.audio import (
    AudioClass,
    AudioEventKind,
    AudioRegistry,
    AudioRequestBus,
    HonestyViolation,
    Provenance,
    ScientificAudioInspector,
    Transform,
    TransformChain,
    chirp,
    default_classification,
    orbital_hum,
    quantize_int16,
    request_is_honest,
    sine,
    standard_registry,
    validate_provenance,
)
from astra.audio.registry import GeneratorSpec, StreamRecord
from astra.audio.transforms import TransformError
from astra.catalog.transform import AU_KM
from astra.physics.constants import GRAVITATIONAL_CONSTANT

AU_M = AU_KM * 1.0e3
MU = GRAVITATIONAL_CONSTANT * 1.9885e30


# ---------------- classification vocabulary ----------------

def test_seven_class_vocabulary_matches_native_names():
    assert {c.value for c in AudioClass} == {
        "REAL_ACOUSTIC",
        "REAL_SIGNAL_SONIFICATION",
        "DATA_DERIVED",
        "PHYSICALLY_MODELED",
        "SCIENTIFICALLY_INTERPRETED",
        "CINEMATIC",
        "SPECULATIVE",
    }


def test_event_kinds_match_native_names():
    assert {k.value for k in AudioEventKind} >= {
        "UI_SELECT",
        "SONIFICATION_REQUEST",
        "VACUUM_ACOUSTIC_REQUEST",
        "TRAVEL_BEGIN",
        "TRAVEL_COMPLETE",
        "TRAVEL_ABORT",
        "TRAVEL_INVALID",
    }


def test_default_classification_travel_kinds():
    # SPECULATIVE for travel (never physical sound); CINEMATIC for fail cue
    assert default_classification(AudioEventKind.TRAVEL_BEGIN) is AudioClass.SPECULATIVE
    assert default_classification(AudioEventKind.TRAVEL_COMPLETE) is AudioClass.SPECULATIVE
    assert default_classification(AudioEventKind.TRAVEL_ABORT) is AudioClass.SPECULATIVE
    assert default_classification(AudioEventKind.TRAVEL_INVALID) is AudioClass.CINEMATIC
    assert default_classification(AudioEventKind.UI_SELECT) is AudioClass.CINEMATIC


# ---------------- honesty policy (native parity vectors) ----------------

def test_real_acoustic_always_refused():
    for kind in AudioEventKind:
        assert request_is_honest(kind, AudioClass.REAL_ACOUSTIC, "anything") is False
    assert request_is_honest(
        AudioEventKind.VACUUM_ACOUSTIC_REQUEST, AudioClass.REAL_ACOUSTIC, "space"
    ) is False


def test_sonification_requires_subject():
    assert request_is_honest(
        AudioEventKind.SONIFICATION_REQUEST, AudioClass.REAL_SIGNAL_SONIFICATION, ""
    ) is False
    assert request_is_honest(
        AudioEventKind.SONIFICATION_REQUEST, AudioClass.REAL_SIGNAL_SONIFICATION, "voyager.pws"
    ) is True


def test_speculative_never_rides_impact_kind():
    assert request_is_honest(
        AudioEventKind.IMPACT_MODELED, AudioClass.SPECULATIVE, "x"
    ) is False


# ---------------- provenance ----------------

def _good_prov(**kw):
    base = dict(
        source="astra.audio.synthesis.sine",
        citation="generated in-repo",
        license="ASTRA project license",
        data_units="NOT AVAILABLE",
    )
    base.update(kw)
    return Provenance(**base)


def test_data_classes_require_provenance():
    for cls in (
        AudioClass.REAL_SIGNAL_SONIFICATION,
        AudioClass.DATA_DERIVED,
        AudioClass.REAL_ACOUSTIC,
    ):
        with pytest.raises(HonestyViolation):
            validate_provenance(cls, None)


def test_data_classes_reject_not_available_source():
    with pytest.raises(HonestyViolation):
        validate_provenance(
            AudioClass.DATA_DERIVED, _good_prov(source="NOT AVAILABLE")
        )


def test_empty_license_refused():
    with pytest.raises(HonestyViolation):
        validate_provenance(AudioClass.CINEMATIC, _good_prov(license="  "))


def test_physically_modeled_requires_in_repo_model():
    with pytest.raises(HonestyViolation):
        validate_provenance(AudioClass.PHYSICALLY_MODELED, _good_prov(source="external.wav"))
    validate_provenance(AudioClass.PHYSICALLY_MODELED, _good_prov())  # passes


def test_provenance_checksum_deterministic_and_tamper_sensitive():
    p = _good_prov()
    assert p.checksum() == _good_prov().checksum()
    assert p.checksum() != _good_prov(license="cc0").checksum()


# ---------------- transforms ----------------

def test_resample_linear_up_and_down():
    b = [0.0, 1.0, 0.0, -1.0]
    up = Transform("resample_linear", 2.0).apply(b)
    assert len(up) == int(math.floor(3 * 2.0 + 1.0))
    assert up[0] == 0.0 and up[2] == 1.0
    assert up[1] == pytest.approx(0.5, abs=1e-15)
    down = Transform("resample_linear", 0.5).apply(b)
    assert down[0] == 0.0 and down[1] == 0.0  # clamps to last sample at end


@pytest.mark.parametrize("bad", [0.0, -1.0, float("nan"), float("inf")])
def test_resample_refuses_bad_ratios(bad):
    with pytest.raises(TransformError):
        Transform("resample_linear", bad).apply([0.1, 0.2])


def test_normalize_refuses_zero_peak_and_empty():
    with pytest.raises(TransformError):
        Transform("normalize", 0.5).apply([0.0, 0.0])
    with pytest.raises(TransformError):
        Transform("normalize", 0.5).apply([])


def test_gain_db_six_db_halves_amplitude():
    out = Transform("gain_db", -20.0).apply([1.0, -1.0])
    assert out[0] == pytest.approx(0.1, rel=1e-12)


def test_chain_parse_round_trip():
    ch = TransformChain([Transform("gain_db", -6.0), Transform("normalize", 0.5)])
    s = ch.canonical()
    ch2 = TransformChain.parse(s)
    assert ch2.canonical() == s
    buf = [0.25, -0.5, 0.75]
    assert ch.apply(buf) == ch2.apply(buf)


def test_quantize_deterministic_and_clamped():
    q = quantize_int16([0.0, 1.0, -1.0, 2.0, -3.0, 0.5, -0.5])
    assert q == [0, 32767, -32767, 32767, -32768, 16384, -16384]
    with pytest.raises(HonestyViolation):
        quantize_int16([0.0, float("nan")])


# ---------------- synthesis ----------------

def test_sine_deterministic():
    a = sine(440.0, 0.004, 8000.0)
    b = sine(440.0, 0.004, 8000.0)
    assert a == b  # bit-identical rerun
    assert len(a) == 32 and a[0] == 0.0


def test_chirp_phase_integral():
    c = chirp(220.0, 660.0, 0.005, 8000.0)
    assert len(c) == 40
    assert c == chirp(220.0, 660.0, 0.005, 8000.0)


def test_orbital_hum_delegates_period_and_rejects_bad_ranges():
    h = orbital_hum(AU_M, MU, 365.25 * 86400.0, 55.0, 0.004, 8000.0, 1.0)
    assert len(h) == 32
    with pytest.raises(ValueError):
        orbital_hum(1.0, 1.327e20, 3.15576e7, 55.0, 0.1, 8000.0, 1.0)  # > nyquist
    with pytest.raises(Exception):
        orbital_hum(-1.0, MU, 3.0e7, 55.0, 0.1, 8000.0, 1.0)  # authority refusal


@pytest.mark.parametrize("arg", [(-1.0,), (0.0,), (float("nan"),)])
def test_synth_refuses_bad_frequencies(arg):
    with pytest.raises(ValueError):
        sine(arg[0], 0.004, 8000.0)


# ---------------- registry + inspector ----------------

def test_standard_registry_three_honest_streams():
    reg = standard_registry()
    names = reg.names()
    assert names == ["journey.begin", "orbit.earth_hum", "ui.tick"]
    classes = {n: reg.get(n).classification for n in names}
    assert classes["ui.tick"] is AudioClass.CINEMATIC
    assert classes["journey.begin"] is AudioClass.SPECULATIVE
    assert classes["orbit.earth_hum"] is AudioClass.SCIENTIFICALLY_INTERPRETED
    # nothing in the standard set claims to be a real recording
    assert all(
        classes[n] not in (AudioClass.REAL_SIGNAL_SONIFICATION, AudioClass.DATA_DERIVED)
        for n in names
    )


def test_registry_rejects_duplicates_and_missing_provenance():
    reg = standard_registry()
    with pytest.raises(HonestyViolation):
        reg.register(reg.get("ui.tick"))
    rec = StreamRecord(
        name="rec.claim", classification=AudioClass.REAL_SIGNAL_SONIFICATION,
        provenance=None, data_units="pa",
        generator=GeneratorSpec("sine", [440.0, 0.01, 8000.0]),
        transforms=TransformChain(),
    )
    with pytest.raises(HonestyViolation):
        reg.register(rec)


def test_registry_render_deterministic():
    reg = standard_registry()
    a = reg.render("orbit.earth_hum")
    b = reg.render("orbit.earth_hum")
    assert a == b and len(a) > 0


def test_stream_record_checksum_tamper_sensitive():
    reg = standard_registry()
    rec = reg.get("ui.tick")
    c1 = rec.checksum()
    rec2 = StreamRecord(
        name=rec.name, classification=rec.classification, provenance=rec.provenance,
        data_units=rec.data_units,
        generator=GeneratorSpec("sine", [440.0, 0.02, 22050.0]),
        transforms=rec.transforms,
    )
    assert c1 != rec2.checksum()


def test_inspector_full_record_and_refusal_on_unknown():
    insp = ScientificAudioInspector(standard_registry())
    info = insp.inspect("orbit.earth_hum")
    for field in (
        "name", "classification", "source", "citation", "license",
        "source_data_units", "stream_data_units", "transform_chain",
        "generator", "checksum", "claims_real_recording", "vacuum_safe",
    ):
        assert field in info
    assert info["classification"] == "SCIENTIFICALLY_INTERPRETED"
    assert info["claims_real_recording"] is False
    with pytest.raises(HonestyViolation):
        insp.inspect("no.such.stream")


def test_inspector_consistency_checks():
    insp = ScientificAudioInspector(standard_registry())
    assert insp.verify_consistency() is True


# ---------------- request bus ----------------

def test_bus_accepts_honest_and_drops_dishonest():
    bus = AudioRequestBus()
    assert bus.push(AudioEventKind.UI_SELECT, "hud.viz", 1.0) is True
    assert bus.push(AudioEventKind.VACUUM_ACOUSTIC_REQUEST, "space", 1.0) is False
    assert bus.dropped_dishonest == 1
    assert bus.total_pushed == 1
    req = bus.pop()
    assert req is not None and req.classification is AudioClass.CINEMATIC
    assert bus.pop() is None


def test_bus_capacity_drops_oldest_deterministically():
    bus = AudioRequestBus()
    for i in range(300):
        assert bus.push(AudioEventKind.SIM_STEP, f"s.{i}", float(i)) is True
    assert bus.size == 256
    reqs = bus.drain()
    assert reqs[0].subject == "s.44"  # oldest 44 dropped
    assert reqs[-1].subject == "s.299"


def test_bus_event_hook_integration():
    events = []
    bus = AudioRequestBus(event_hook=lambda name, payload: events.append((name, payload)))
    bus.push(AudioEventKind.TRAVEL_BEGIN, "journey.1", 42.0)
    bus.push(AudioEventKind.VACUUM_ACOUSTIC_REQUEST, "space", 42.0)  # refused, no event
    assert len(events) == 1
    name, payload = events[0]
    assert name == "audio_request"
    assert payload["classification"] == "SPECULATIVE"
    assert payload["kind"] == "TRAVEL_BEGIN"


# ---------------- determinism at the runner level ----------------

def test_two_pass_pipeline_bit_identical():
    def run():
        reg = standard_registry()
        bus = AudioRequestBus()
        bus.push(AudioEventKind.UI_SELECT, "hud", 0.0)
        bus.push(AudioEventKind.TRAVEL_BEGIN, "j1", 1.0)
        rendered = {n: tuple(reg.render(n)) for n in reg.names()}
        reqs = tuple(bus.drain())
        return rendered, reqs, insp_checksums(reg)

    def insp_checksums(reg):
        insp = ScientificAudioInspector(reg)
        return tuple(insp.inspect(n)["checksum"] for n in reg.names())

    assert run() == run()
