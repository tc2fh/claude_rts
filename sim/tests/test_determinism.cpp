#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
fix64_t W(int c) { return (fix64_t)c << 32; }
// Seed + CMD_MOVE orders for both units, advanced via a chunk plan summing to 200.
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);
    struct Mv { std::uint64_t t; std::uint32_t unit; int cx, cy; };
    const Mv log[] = {{1, 0, 18, 4}, {1, 1, 5, 20}, {30, 0, 2, 22}};
    for (const auto& e : log) {
        SimCommand c{}; c.type = CMD_MOVE; c.player = 1; c.unit = e.unit; c.tx = W(e.cx); c.ty = W(e.cy);
        w.push_command(c, e.t);
    }
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("movement replay is reproducible") {
    CHECK(run_scenario({200}) == run_scenario({200}));
}

TEST_CASE("movement replay is batching-invariant") {
    const std::uint64_t ref = run_scenario({200});
    CHECK(run_scenario({50, 70, 80}) == ref);   // sums to 200
    CHECK(run_scenario({100, 100})   == ref);
    std::vector<std::uint32_t> ones(200, 1);     // 200 x advance(1)
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("movement golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({200});
    std::printf("[determinism] movement scenario hash = 0x%016llx\n", (unsigned long long)h);
    // GOLDEN: pin after first green run; MUST match on macOS-arm64 + Windows-x64 + Linux.
    CHECK(h == 0x1db7f53422dea2e9ull);
}
