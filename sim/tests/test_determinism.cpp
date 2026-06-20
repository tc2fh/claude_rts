#include <doctest/doctest.h>
#include "sim/world.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
// A fixed scenario: seed + CMD_STOP commands at specific exec_ticks, advanced via a
// chunk plan whose elements sum to 50. Same scenario + same total ticks => same hash.
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);
    struct Entry { std::uint64_t t; std::uint32_t unit; };
    const Entry log[] = {{2, 0}, {5, 1}};
    for (const auto& e : log) {
        SimCommand c{}; c.type = CMD_STOP; c.player = 1; c.unit = e.unit;
        w.push_command(c, e.t);
    }
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("replay is reproducible within a process") {
    CHECK(run_scenario({50}) == run_scenario({50}));
}

TEST_CASE("replay is batching-invariant with commands active") {
    const std::uint64_t ref = run_scenario({50});
    CHECK(run_scenario({7, 13, 30}) == ref);   // sums to 50
    CHECK(run_scenario({25, 25})    == ref);   // sums to 50
    std::vector<std::uint32_t> ones(50, 1);    // 50 x advance(1)
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({50});
    std::printf("[determinism] scenario hash = 0x%016llx\n", (unsigned long long)h);
    // GOLDEN: pinned after first green run. MUST match on macOS-arm64 and Windows-x64.
    // A mismatch across OSes is a determinism bug to FIX, not to re-pin.
    // Re-pinned for M0-systems-2a Task 3: 2-unit world, no drift, CMobile.
    // MUST match on macOS-arm64 and Windows-x64. A mismatch across OSes is a determinism bug.
    CHECK(h == 0x708f9a7301753bb6ull);
}
