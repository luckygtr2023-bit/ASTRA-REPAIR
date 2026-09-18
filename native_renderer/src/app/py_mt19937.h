// ASTRA COSMOS v1.3 — CPython-compatible deterministic RNG mirror.
//
// Authority: astra.core.rng.RNGStream wraps Python's random.Random(seed);
// random.Random is Mersenne Twister (MT19937) with CPython's init_by_array
// int-seeding (version 2: abs(seed) split into 32-bit little-endian words)
// and random() = ((gen32()>>5)*67208864? NO — see exact expression below.
//
// This mirror reproduces random.Random(int_seed).random() bit-for-bit.
// Only next_float() is mirrored — the sole method consumed by
// astra.destruction (fragmentation / ejecta) — per rule: mirror ONLY the
// authority-supported surface actually used.  get_state/restore_state are
// NOT mirrored (Python exposes the opaque MT state tuple; native state is
// private and equally deterministic; persistence schema unchanged).
//
// Classification: REAL (algorithm port), outputs SIMULATED_DATA downstream.
#pragma once

#include <cstdint>

namespace astra::app {

class PyMt19937 {
public:
    // seed must be a non-negative integer (destruction API contract); negative
    // ints are accepted with abs() to mirror CPython version-2 int seeding.
    explicit PyMt19937(long long seed);
    explicit PyMt19937(unsigned long long seed) : PyMt19937((long long)seed) {}

    // Identical to Python random.Random(seed).random(): float in [0, 1),
    // (gen32>>5)*67108864.0 + (gen32>>6)*134217728.0, all over 2**53.
    double next_float();

    uint32_t next_uint32();  // exposed for the checker (raw draws)

private:
    // CPython int seeding: split |seed| into little-endian 32-bit words and
    // run init_by_array.  Arbitrary-precision seeds are reduced to however
    // many words the 64-bit input provides (authority passes 64-bit-safe
    // seeds: user seeds and seed+case_index).
    void seed_from_words(const uint32_t* key, int nwords);

    uint32_t mt_[624];
    int index_ = 624;
};

} // namespace astra::app
