#pragma once
// ASTRA COSMOS — simulation event -> Cosmic Audio binding (v0.4).
//
// Every audio event carries an explicit provenance classification; nothing is
// "just a sound". The bus is the single path from the application into the
// audio engine, and it REFUSES scientifically dishonest requests
// (e.g. REAL_ACOUSTIC propagation through space vacuum is rejected by policy).
// In this headless-capable architecture the bus is testable without any audio
// device (classification + routing are verified by unit gates; audible output
// requires the Windows run and stays NOT VERIFIED until then).

#include <cstddef>
#include <string>

namespace astra::app {

enum class AudioClass {
    REAL_ACOUSTIC,              // actually propagating mechanic sound (never in vacuum)
    REAL_SIGNAL_SONIFICATION,   // real recorded/measured signal mapped to audio
    DATA_DERIVED,               // derived from real data values
    PHYSICALLY_MODELED,         // synthesized from a physics model
    SCIENTIFICALLY_INTERPRETED, // interpretive mapping of physical quantities
    CINEMATIC,                  // artistic/UI feedback, explicitly not physics
    SPECULATIVE                 // unestablished physics (wormholes etc.)
};

enum class AudioEventKind {
    UI_SELECT, UI_DESELECT, UI_MODE,
    SIM_PAUSE, SIM_RESUME, SIM_WARP, SIM_STEP, SIM_RESET, SIM_RESTART,
    SCENARIO_SAVE, SCENARIO_LOAD,
    SONIFICATION_REQUEST,   // request for a DATA-backed signal playback
    IMPACT_MODELED,         // destruction-backed event (PHYSICALLY_MODELED)
    VACUUM_ACOUSTIC_REQUEST // deliberately testable dishonest request (rejected)
};

struct AudioEvent {
    AudioEventKind kind;
    AudioClass classification;
    std::string subject;    // authoritative object id or channel label
    double sim_time_s = 0.0;
    float gain = 1.0f;
};

// Fixed policy mapping event kind -> default classification.
AudioClass default_classification(AudioEventKind kind);

// Honesty policy: returns false for requests that would misrepresent physics
// (e.g. REAL_ACOUSTIC anywhere in space/vacuum contexts, SONIFICATION
// without a data source, SPECULATIVE presented as REAL).
bool request_is_honest(const AudioEvent& e);

class AudioBus {
public:
    static constexpr size_t kCapacity = 256;
    // Push: applies default classification for kind, then honesty policy.
    // Dishonest requests are dropped AND counted (visible in stats).
    bool push(AudioEventKind kind, const std::string& subject, double sim_time_s, float gain = 1.0f);
    bool pop(AudioEvent& out);           // FIFO
    size_t size() const;
    size_t dropped_dishonest() const;
    size_t total_pushed() const;
private:
    AudioEvent q_[kCapacity];
    size_t head_ = 0, tail_ = 0, count_ = 0;
    size_t dropped_ = 0, pushed_ = 0;
};

const char* audio_class_name(AudioClass c);
const char* audio_event_kind_name(AudioEventKind k);

} // namespace astra::app
