#pragma once
#include <cstdint>

namespace sim {

// SplitMix64 — small, fast, fully deterministic. The ONLY source of randomness in the sim.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed = 0) : state(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Deterministic. Returns [0, n).
    std::uint32_t range(std::uint32_t n) {
        return static_cast<std::uint32_t>(next() % n);
    }
};

} // namespace sim
