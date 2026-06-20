#pragma once
#include <cstdint>
#include <cstddef>

namespace sim {

// FNV-1a, 64-bit. Used to fingerprint sim state for replay/desync checks.
struct Hasher {
    std::uint64_t value = 0xcbf29ce484222325ULL;

    void add_bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            value ^= b[i];
            value *= 0x100000001b3ULL;
        }
    }
    void add_u64(std::uint64_t v) { add_bytes(&v, sizeof(v)); }
    void add_i64(std::int64_t v)  { add_bytes(&v, sizeof(v)); }
    void add_u32(std::uint32_t v) { add_bytes(&v, sizeof(v)); }
    void add_i32(std::int32_t v)  { add_bytes(&v, sizeof(v)); }
};

} // namespace sim
