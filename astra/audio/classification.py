"""ASTRA Audio — classification vocabulary + honesty policy (v1.6).

The 7-class vocabulary is FIXED and byte-identical to the native
`AudioClass` enum (native_renderer/src/app/audio_bus.h). Semantics:

  REAL_ACOUSTIC              — actually propagating mechanical sound;
                               NEVER in vacuum. Requires a physical
                               medium context and provenance.
  REAL_SIGNAL_SONIFICATION   — real recorded/measured signal mapped to
                               audio (e.g. an actual instrument recording);
                               provenance with license REQUIRED.
  DATA_DERIVED               — derived from real data values;
                               provenance REQUIRED.
  PHYSICALLY_MODELED         — synthesized from a physics model that is
                               actually computed in-repo.
  SCIENTIFICALLY_INTERPRETED — interpretive mapping of physical
                               quantities (documented mapping, not sound).
  CINEMATIC                  — artistic/UI feedback, explicitly not physics.
  SPECULATIVE                — unestablished physics (wormholes/warp etc.)

Honesty policy (mirrors native `request_is_honest`): dishonest requests
are REFUSED — never silently reclassified.
"""

from __future__ import annotations

from enum import Enum


class HonestyViolation(ValueError):
    """Raised when an audio request would misrepresent physics/data."""


class AudioClass(Enum):
    REAL_ACOUSTIC = "REAL_ACOUSTIC"
    REAL_SIGNAL_SONIFICATION = "REAL_SIGNAL_SONIFICATION"
    DATA_DERIVED = "DATA_DERIVED"
    PHYSICALLY_MODELED = "PHYSICALLY_MODELED"
    SCIENTIFICALLY_INTERPRETED = "SCIENTIFICALLY_INTERPRETED"
    CINEMATIC = "CINEMATIC"
    SPECULATIVE = "SPECULATIVE"


class AudioEventKind(Enum):
    """Event kinds (byte-identical names to native AudioEventKind)."""

    UI_SELECT = "UI_SELECT"
    UI_DESELECT = "UI_DESELECT"
    UI_MODE = "UI_MODE"
    SIM_PAUSE = "SIM_PAUSE"
    SIM_RESUME = "SIM_RESUME"
    SIM_WARP = "SIM_WARP"
    SIM_STEP = "SIM_STEP"
    SIM_RESET = "SIM_RESET"
    SIM_RESTART = "SIM_RESTART"
    SCENARIO_SAVE = "SCENARIO_SAVE"
    SCENARIO_LOAD = "SCENARIO_LOAD"
    SONIFICATION_REQUEST = "SONIFICATION_REQUEST"
    IMPACT_MODELED = "IMPACT_MODELED"
    VACUUM_ACOUSTIC_REQUEST = "VACUUM_ACOUSTIC_REQUEST"
    TRAVEL_BEGIN = "TRAVEL_BEGIN"
    TRAVEL_COMPLETE = "TRAVEL_COMPLETE"
    TRAVEL_ABORT = "TRAVEL_ABORT"
    TRAVEL_INVALID = "TRAVEL_INVALID"


# Fixed default classification policy — mirrors native default_classification.
_DEFAULTS = {
    AudioEventKind.UI_SELECT: AudioClass.CINEMATIC,
    AudioEventKind.UI_DESELECT: AudioClass.CINEMATIC,
    AudioEventKind.UI_MODE: AudioClass.CINEMATIC,
    AudioEventKind.SIM_PAUSE: AudioClass.CINEMATIC,
    AudioEventKind.SIM_RESUME: AudioClass.CINEMATIC,
    AudioEventKind.SIM_WARP: AudioClass.CINEMATIC,
    AudioEventKind.SIM_STEP: AudioClass.CINEMATIC,
    AudioEventKind.SIM_RESET: AudioClass.CINEMATIC,
    AudioEventKind.SIM_RESTART: AudioClass.CINEMATIC,
    AudioEventKind.SCENARIO_SAVE: AudioClass.CINEMATIC,
    AudioEventKind.SCENARIO_LOAD: AudioClass.CINEMATIC,
    AudioEventKind.SONIFICATION_REQUEST: AudioClass.REAL_SIGNAL_SONIFICATION,
    AudioEventKind.IMPACT_MODELED: AudioClass.PHYSICALLY_MODELED,
    AudioEventKind.VACUUM_ACOUSTIC_REQUEST: AudioClass.REAL_ACOUSTIC,
    AudioEventKind.TRAVEL_BEGIN: AudioClass.SPECULATIVE,
    AudioEventKind.TRAVEL_COMPLETE: AudioClass.SPECULATIVE,
    AudioEventKind.TRAVEL_ABORT: AudioClass.SPECULATIVE,
    AudioEventKind.TRAVEL_INVALID: AudioClass.CINEMATIC,
}

def default_classification(kind: AudioEventKind) -> AudioClass:
    return _DEFAULTS[kind]


def request_is_honest(
    kind: AudioEventKind,
    classification: AudioClass,
    subject: str,
) -> bool:
    """Byte-for-byte policy mirror of native `request_is_honest`
    (native_renderer/src/app/audio_bus.cpp). Returns False = refuse.

    Native rules (v0.4, unchanged):
      1. REAL_ACOUSTIC is ALWAYS refused on this bus — mechanical sound
         claims never pass anywhere in ASTRA's space contexts.
      2. REAL_SIGNAL_SONIFICATION with an empty subject is a fabricated
         signal claim — refused.
      3. SPECULATIVE must never ride the IMPACT_MODELED (established
         physics) kind — refused.

    Provenance/licensing requirements of v1.6 are enforced ADDITIONALLY
    in provenance.validate_provenance / registry — they never weaken
    these rules (refusals only ever grow stricter, per rule 16).
    """
    if classification is AudioClass.REAL_ACOUSTIC:
        return False
    if classification is AudioClass.REAL_SIGNAL_SONIFICATION and not subject:
        return False
    if (
        classification is AudioClass.SPECULATIVE
        and kind is AudioEventKind.IMPACT_MODELED
    ):
        return False
    return True


def require_honest(
    kind: AudioEventKind,
    classification: AudioClass,
    subject: str,
) -> None:
    """Raise HonestyViolation on dishonest requests (fail-closed)."""
    if not request_is_honest(kind, classification, subject):
        raise HonestyViolation(
            "audio request %s (%s, subject=%r) would misrepresent physics/data"
            % (kind.value, classification.value, subject)
        )
