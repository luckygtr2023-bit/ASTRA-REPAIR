// Mirror of CPython Modules/_randommodule.c MT19937 core:
//   init_genrand(19650218) -> init_by_array(key words)
//   genrand_uint32() twist, temper
//   random_random(): ((gen32()>>5)*67108864.0 + (gen32()>>6))
//                    / 9007199254740992.0   (b NOT scaled — verified vs CPython)
// Operation order and constants are verbatim CPython MT19937.

#include "py_mt19937.h"

namespace astra::app {

namespace {

constexpr uint32_t N = 624;
constexpr uint32_t M = 397;
constexpr uint32_t MATRIX_A = 0x9908b0dfu;
constexpr uint32_t UPPER_MASK = 0x80000000u;
constexpr uint32_t LOWER_MASK = 0x7fffffffu;

inline uint32_t negate_abs_u64(long long seed) {
    // abs() without overflow: work on the two's complement bit pattern.
    unsigned long long u = (unsigned long long)seed;
    if (seed < 0) u = (~u) + 1ull;
    return (uint32_t)(u & 0xffffffffull); // low word; high word below
}

} // namespace

void PyMt19937::seed_from_words(const uint32_t* key, int nwords) {
    // init_genrand(19650218)
    mt_[0] = 19650218u;
    for (uint32_t i = 1; i < N; ++i)
        mt_[i] = (1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + i);
    index_ = (int)N;

    if (nwords <= 0) nwords = 1; // CPython: keyused = max(keylength, 1)
    // init_by_array(key, keylength)
    uint32_t i = 1, j = 0;
    int k = ((int)N > nwords) ? (int)N : nwords;
    for (; k; --k) {
        mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525u))
                 + key[j] + j;
        ++i; ++j;
        if (i >= N) { mt_[0] = mt_[N - 1]; i = 1; }
        if (j >= (uint32_t)nwords) j = 0;
    }
    for (k = N - 1; k; --k) {
        mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941u))
                 - i;
        ++i;
        if (i >= N) { mt_[0] = mt_[N - 1]; i = 1; }
    }
    mt_[0] = UPPER_MASK;
}

PyMt19937::PyMt19937(long long seed) {
    // CPython version-2 int seeding: key = little-endian 32-bit words of
    // abs(seed); a zero seed yields a single zero word.
    unsigned long long u = (unsigned long long)seed;
    if (seed < 0) u = (~u) + 1ull;
    uint32_t key[2];
    key[0] = (uint32_t)(u & 0xffffffffull);
    key[1] = (uint32_t)(u >> 32);
    int nwords = (key[1] != 0u) ? 2 : 1; // abs(seed) < 2**32 => one word
    seed_from_words(key, nwords);
}

uint32_t PyMt19937::next_uint32() {
    if (index_ >= (int)N) {
        // genrand_twist (verbatim MT19937 twist)
        for (int k = 0; k < (int)(N - M); ++k) {
            uint32_t y = (mt_[k] & UPPER_MASK) | (mt_[k + 1] & LOWER_MASK);
            mt_[k] = mt_[k + (int)M] ^ (y >> 1) ^ (MATRIX_A & (uint32_t)(-(int32_t)(y & 1u)));
        }
        for (int k = (int)(N - M); k < (int)N - 1; ++k) {
            uint32_t y = (mt_[k] & UPPER_MASK) | (mt_[k + 1] & LOWER_MASK);
            mt_[k] = mt_[k + (int)M - (int)N] ^ (y >> 1) ^ (MATRIX_A & (uint32_t)(-(int32_t)(y & 1u)));
        }
        {
            uint32_t y = (mt_[N - 1] & UPPER_MASK) | (mt_[0] & LOWER_MASK);
            mt_[N - 1] = mt_[M - 1] ^ (y >> 1) ^ (MATRIX_A & (uint32_t)(-(int32_t)(y & 1u)));
        }
        index_ = 0;
    }
    uint32_t y = mt_[index_++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= (y >> 18);
    return y;
}

double PyMt19937::next_float() {
    const uint32_t a = next_uint32() >> 5;
    const uint32_t b = next_uint32() >> 6;
    return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
}

} // namespace astra::app
