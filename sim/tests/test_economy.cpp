#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace { fix64_t W(int c) { return (fix64_t)c << 32; } }

TEST_CASE("a worker harvests the node and deposits at the HQ") {
    SimWorld* s = sim_create(7, 0);          // HQ=0, worker=1, node=2
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 1);
    CHECK(sim_get_snapshot(s).resources[1] == 0);   // nothing deposited yet
    sim_advance(s, 1000);
    CHECK(sim_get_snapshot(s).resources[1] > 0);     // deposits happened
    sim_destroy(s);
}

TEST_CASE("harvest is deterministic and batching-invariant") {
    // 595 = 5*7*17 — divisible by all three chunk sizes so the loop runs
    // the identical number of ticks regardless of chunk.
    auto run = [](int chunk) {
        SimWorld* s = sim_create(9, 0);
        SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
        sim_push_command(s, &h, 1);
        for (int t = 0; t < 595; t += chunk) sim_advance(s, chunk);
        uint64_t hash = sim_state_hash(s);
        sim_destroy(s);
        return hash;
    };
    CHECK(run(595) == run(1));
    CHECK(run(595) == run(7));
}
