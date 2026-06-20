#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
// Seed + harvest (worker 1 -> node 2) + train (HQ 0), advanced via a chunk plan summing to 1600.
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    w.push_command(h, 1);
    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;
    w.push_command(tr, 1000);                      // affordable by then (close node, ~80-tick cycles)
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("economy replay is reproducible") {
    CHECK(run_scenario({1600}) == run_scenario({1600}));
}

TEST_CASE("economy replay is batching-invariant") {
    const std::uint64_t ref = run_scenario({1600});
    CHECK(run_scenario({400, 600, 600}) == ref);   // sums to 1600
    CHECK(run_scenario({800, 800})      == ref);
    std::vector<std::uint32_t> ones(1600, 1);
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("economy golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({1600});
    std::printf("[determinism] economy scenario hash = 0x%016llx\n", (unsigned long long)h);
    CHECK(h == 0xc9d6c0e60514e177ull);
}
