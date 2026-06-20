#pragma once
#include <cstdint>

namespace sim {

// Q32.32 signed fixed-point. One whole unit == (1 << 32).
using fix = std::int64_t;

inline constexpr int  FIX_FRAC = 32;
inline constexpr fix  fix_one  = fix(1) << FIX_FRAC;

inline constexpr fix fix_from_int(std::int64_t i) { return i << FIX_FRAC; }

// Float conversion is for the VIEW / debug only — never feed it back into the sim.
inline float fix_to_float(fix f) {
    return static_cast<float>(static_cast<double>(f) / 4294967296.0);
}

inline constexpr fix fix_abs(fix f)  { return f < 0 ? -f : f; }
inline constexpr int fix_sign(fix f) { return (f > 0) - (f < 0); }
inline constexpr fix fix_clamp(fix v, fix lo, fix hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace sim
