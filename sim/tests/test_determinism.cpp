#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
// Full M0 loop: harvest (worker 1 -> node 2) + train (HQ 0) + attack (soldier 3 -> enemy HQ 4).
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);
    SimCommand harv{}; harv.type = CMD_HARVEST; harv.player = 1; harv.unit = 1; harv.target = 2;
    w.push_command(harv, 1);
    SimCommand train{}; train.type = CMD_TRAIN; train.player = 1; train.unit = 0;
    w.push_command(train, 1000);
    SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4;
    w.push_command(atk, 1);
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("full M0 loop is reproducible") {
    CHECK(run_scenario({3000}) == run_scenario({3000}));
}

TEST_CASE("full M0 loop is batching-invariant") {
    const std::uint64_t ref = run_scenario({3000});
    CHECK(run_scenario({1000, 1000, 1000}) == ref);   // sums to 3000
    CHECK(run_scenario({1500, 1500})       == ref);
    std::vector<std::uint32_t> ones(3000, 1);
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("full M0 loop golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({3000});
    std::printf("[determinism] full-M0 scenario hash = 0x%016llx\n", (unsigned long long)h);
    CHECK(h == 0xdd58708ee3f85ad4ull);
}
