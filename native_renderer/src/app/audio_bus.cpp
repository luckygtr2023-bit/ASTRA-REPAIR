#include "audio_bus.h"

namespace astra::app {

AudioClass default_classification(AudioEventKind kind) {
    switch (kind) {
    case AudioEventKind::UI_SELECT:
    case AudioEventKind::UI_DESELECT:
    case AudioEventKind::UI_MODE:
    case AudioEventKind::SIM_PAUSE:
    case AudioEventKind::SIM_RESUME:
    case AudioEventKind::SIM_WARP:
    case AudioEventKind::SIM_STEP:
    case AudioEventKind::SIM_RESET:
    case AudioEventKind::SIM_RESTART:
    case AudioEventKind::SCENARIO_SAVE:
    case AudioEventKind::SCENARIO_LOAD:
        // Interactive feedback: explicitly NOT physics.
        return AudioClass::CINEMATIC;
    case AudioEventKind::SONIFICATION_REQUEST:
        return AudioClass::REAL_SIGNAL_SONIFICATION;
    case AudioEventKind::IMPACT_MODELED:
        return AudioClass::PHYSICALLY_MODELED;
    case AudioEventKind::VACUUM_ACOUSTIC_REQUEST:
        return AudioClass::REAL_ACOUSTIC; // the request itself is mislabeled on purpose
    }
    return AudioClass::CINEMATIC;
}

bool request_is_honest(const AudioEvent& e) {
    // Space is vacuum: nothing may ever claim REAL_ACOUSTIC propagation
    // for celestial/space contexts. Any such request is rejected.
    if (e.classification == AudioClass::REAL_ACOUSTIC) return false;
    // Sonification demands an actual data-backed source subject; empty
    // subject = fabricated signal.
    if (e.classification == AudioClass::REAL_SIGNAL_SONIFICATION && e.subject.empty()) return false;
    // A speculative event must never be re-labeled as established.
    if (e.classification == AudioClass::SPECULATIVE &&
        (e.kind == AudioEventKind::IMPACT_MODELED)) return false;
    return true;
}

bool AudioBus::push(AudioEventKind kind, const std::string& subject, double sim_time_s, float gain) {
    AudioEvent e{kind, default_classification(kind), subject, sim_time_s, gain};
    if (!request_is_honest(e)) { ++dropped_; return false; }
    ++pushed_;
    if (count_ == kCapacity) {
        // Deterministic ring: drop the OLDEST (documented policy, newest kept).
        head_ = (head_ + 1) % kCapacity;
        --count_;
    }
    q_[tail_] = e;
    tail_ = (tail_ + 1) % kCapacity;
    ++count_;
    return true;
}

bool AudioBus::pop(AudioEvent& out) {
    if (count_ == 0) return false;
    out = q_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return true;
}

size_t AudioBus::size() const { return count_; }
size_t AudioBus::dropped_dishonest() const { return dropped_; }
size_t AudioBus::total_pushed() const { return pushed_; }

const char* audio_class_name(AudioClass c) {
    switch (c) {
    case AudioClass::REAL_ACOUSTIC: return "REAL_ACOUSTIC";
    case AudioClass::REAL_SIGNAL_SONIFICATION: return "REAL_SIGNAL_SONIFICATION";
    case AudioClass::DATA_DERIVED: return "DATA_DERIVED";
    case AudioClass::PHYSICALLY_MODELED: return "PHYSICALLY_MODELED";
    case AudioClass::SCIENTIFICALLY_INTERPRETED: return "SCIENTIFICALLY_INTERPRETED";
    case AudioClass::CINEMATIC: return "CINEMATIC";
    case AudioClass::SPECULATIVE: return "SPECULATIVE";
    }
    return "UNKNOWN";
}

const char* audio_event_kind_name(AudioEventKind k) {
    switch (k) {
    case AudioEventKind::UI_SELECT: return "UI_SELECT";
    case AudioEventKind::UI_DESELECT: return "UI_DESELECT";
    case AudioEventKind::UI_MODE: return "UI_MODE";
    case AudioEventKind::SIM_PAUSE: return "SIM_PAUSE";
    case AudioEventKind::SIM_RESUME: return "SIM_RESUME";
    case AudioEventKind::SIM_WARP: return "SIM_WARP";
    case AudioEventKind::SIM_STEP: return "SIM_STEP";
    case AudioEventKind::SIM_RESET: return "SIM_RESET";
    case AudioEventKind::SIM_RESTART: return "SIM_RESTART";
    case AudioEventKind::SCENARIO_SAVE: return "SCENARIO_SAVE";
    case AudioEventKind::SCENARIO_LOAD: return "SCENARIO_LOAD";
    case AudioEventKind::SONIFICATION_REQUEST: return "SONIFICATION_REQUEST";
    case AudioEventKind::IMPACT_MODELED: return "IMPACT_MODELED";
    case AudioEventKind::VACUUM_ACOUSTIC_REQUEST: return "VACUUM_ACOUSTIC_REQUEST";
    }
    return "UNKNOWN";
}

} // namespace astra::app
