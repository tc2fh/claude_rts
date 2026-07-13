#include <doctest/doctest.h>
#include "sim/sim_abi.h"

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
    // 595 = 5*7*17 - divisible by all three chunk sizes so the loop runs
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

TEST_CASE("CMD_TRAIN debits resources and spawns a worker after the build time") {
    SimWorld* s = sim_create(7, 0);
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 1500);
    int32_t before = sim_get_snapshot(s).resources[1];
    REQUIRE(before >= 50);
    uint32_t count_before = sim_get_snapshot(s).count;

    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;  // HQ id 0
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 2);
    int32_t after_train = sim_get_snapshot(s).resources[1];
    CHECK(after_train < before);                                     // train debited (cost 50 >> any 2-tick deposit)
    CHECK(after_train >= before - 50);                               // debited at most the cost (rest is harvest income)
    sim_advance(s, 60);                                              // > BUILD_TIME (48)
    CHECK(sim_get_snapshot(s).count == count_before + 1);            // a new worker spawned
    sim_destroy(s);
}

TEST_CASE("CMD_TRAIN with insufficient resources does nothing") {
    SimWorld* s = sim_create(7, 0);                                   // resources start at 0
    uint32_t count_before = sim_get_snapshot(s).count;
    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;
    sim_push_command(s, &tr, 1);
    sim_advance(s, 60);
    CHECK(sim_get_snapshot(s).resources[1] == 0);
    CHECK(sim_get_snapshot(s).count <= count_before);               // no spawn (deaths may reduce count)
    sim_destroy(s);
}
