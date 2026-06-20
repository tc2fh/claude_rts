#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace {

// Returns the first snapshot entry matching (type, owner) that has id >= min_id.
// Returns nullptr if none found.
const SimEntitySnapshot* find_spawned(const SimSnapshot& s, uint16_t type, uint8_t owner, uint32_t min_id) {
    for (uint32_t i = 0; i < s.count; ++i) {
        const auto& e = s.entities[i];
        if (e.type == type && e.owner == owner && e.id >= min_id) return &e;
    }
    return nullptr;
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: CMD_TRAIN param=SIM_TYPE_SOLDIER trains a soldier
// -----------------------------------------------------------------------------
TEST_CASE("CMD_TRAIN param=SIM_TYPE_SOLDIER trains a soldier") {
    SimWorld* s = sim_create(7, 0);

    // Harvest to build up resources (> SOLDIER_COST = 75)
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 2000);

    SimSnapshot snap = sim_get_snapshot(s);
    int32_t before = snap.resources[1];
    REQUIRE(before >= 75);   // must have enough to train

    uint32_t count_before = snap.count;

    // Issue CMD_TRAIN soldier
    SimCommand tr{};
    tr.type  = CMD_TRAIN;
    tr.player = 1;
    tr.unit  = 0;   // HQ id = 0
    tr.param = SIM_TYPE_SOLDIER;
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 2);

    int32_t after = sim_get_snapshot(s).resources[1];
    CHECK(after < before);           // debited
    CHECK(after >= before - 75);     // debited at most cost (minus any 2-tick harvest income)

    // Advance past SOLDIER_BUILD_TIME (72)
    sim_advance(s, 80);
    snap = sim_get_snapshot(s);
    CHECK(snap.count == count_before + 1);

    // Verify a newly spawned soldier (id >= 6) exists belonging to player 1
    const SimEntitySnapshot* sdr = find_spawned(snap, SIM_TYPE_SOLDIER, 1, 6);
    CHECK(sdr != nullptr);

    sim_destroy(s);
}

// -----------------------------------------------------------------------------
// Test 2: a trained soldier can fight — it destroys the enemy HQ on attack-move
// -----------------------------------------------------------------------------
TEST_CASE("a trained soldier can fight -- it destroys the enemy HQ on attack-move") {
    SimWorld* s = sim_create(7, 0);

    // Harvest to >= 75
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 2000);
    REQUIRE(sim_get_snapshot(s).resources[1] >= 75);

    // Train a soldier
    SimCommand tr{};
    tr.type   = CMD_TRAIN;
    tr.player = 1;
    tr.unit   = 0;
    tr.param  = SIM_TYPE_SOLDIER;
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 2);
    sim_advance(s, 80);   // past SOLDIER_BUILD_TIME (72)

    // Find the newly trained soldier (id >= 6, owner == 1, type == SOLDIER)
    SimSnapshot snap = sim_get_snapshot(s);
    const SimEntitySnapshot* sdr = find_spawned(snap, SIM_TYPE_SOLDIER, 1, 6);
    REQUIRE(sdr != nullptr);
    uint32_t sid = sdr->id;

    // Order it to attack the enemy HQ (id = 4)
    SimCommand atk{};
    atk.type   = CMD_ATTACK;
    atk.player = 1;
    atk.unit   = sid;
    atk.target = 4;
    sim_push_command(s, &atk, sim_current_tick(s) + 1);
    sim_advance(s, 1200);

    if (sim_winner(s) == 0) {
        sim_advance(s, 800);   // generous fallback
    }
    CHECK(sim_winner(s) == 1);

    sim_destroy(s);
}

// -----------------------------------------------------------------------------
// Test 3: CMD_TRAIN param=0 still trains a worker (back-compat)
// -----------------------------------------------------------------------------
TEST_CASE("CMD_TRAIN param=0 still trains a worker (back-compat)") {
    SimWorld* s = sim_create(7, 0);

    // Harvest to >= WORKER_COST (50)
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 1500);
    REQUIRE(sim_get_snapshot(s).resources[1] >= 50);

    uint32_t count_before = sim_get_snapshot(s).count;

    // Train with param=0 (back-compat default -> worker)
    SimCommand tr{};
    tr.type   = CMD_TRAIN;
    tr.player = 1;
    tr.unit   = 0;
    tr.param  = 0;
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 60);   // > BUILD_TIME (48)

    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(snap.count == count_before + 1);

    // Verify the newly spawned unit (id >= 6) is a worker
    const SimEntitySnapshot* wk = find_spawned(snap, SIM_TYPE_WORKER, 1, 6);
    CHECK(wk != nullptr);

    sim_destroy(s);
}

// -----------------------------------------------------------------------------
// Test 4: CMD_TRAIN soldier with insufficient resources is a no-op
// -----------------------------------------------------------------------------
TEST_CASE("CMD_TRAIN soldier with insufficient resources is a no-op") {
    SimWorld* s = sim_create(7, 0);   // resources start at 0

    uint32_t count_before = sim_get_snapshot(s).count;

    SimCommand tr{};
    tr.type   = CMD_TRAIN;
    tr.player = 1;
    tr.unit   = 0;
    tr.param  = SIM_TYPE_SOLDIER;
    sim_push_command(s, &tr, 1);
    sim_advance(s, 80);

    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(snap.resources[1] == 0);
    CHECK(snap.count <= count_before);   // no spawn; deaths can only decrease count

    sim_destroy(s);
}

// -----------------------------------------------------------------------------
// Test 5: CMD_TRAIN with an unknown param is a no-op
// -----------------------------------------------------------------------------
TEST_CASE("CMD_TRAIN with an unknown param is a no-op") {
    SimWorld* s = sim_create(7, 0);

    // Harvest to have >= 75 resources
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 2000);
    REQUIRE(sim_get_snapshot(s).resources[1] >= 75);

    int32_t before = sim_get_snapshot(s).resources[1];
    uint32_t count_before = sim_get_snapshot(s).count;

    SimCommand tr{};
    tr.type   = CMD_TRAIN;
    tr.player = 1;
    tr.unit   = 0;
    tr.param  = 99;   // unknown
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 80);

    SimSnapshot snap = sim_get_snapshot(s);
    // Resources not debited by training (harvest keeps adding income so >= before)
    CHECK(snap.resources[1] >= before);
    // No unit spawned by training
    CHECK(snap.count <= count_before);

    sim_destroy(s);
}

// -----------------------------------------------------------------------------
// Test 6: training a soldier is deterministic and batching-invariant
// -----------------------------------------------------------------------------
TEST_CASE("training a soldier is deterministic and batching-invariant") {
    auto run = [](int chunk) -> uint64_t {
        SimWorld* s = sim_create(11, 0);
        SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
        sim_push_command(s, &h, 1);
        SimCommand tr{};
        tr.type   = CMD_TRAIN;
        tr.player = 1;
        tr.unit   = 0;
        tr.param  = SIM_TYPE_SOLDIER;
        sim_push_command(s, &tr, 1701);
        // 2100 = 3 * 5 * 7 * 4 * 5 — divisible by 1, 3, 5, 7
        for (int t = 0; t < 2100; t += chunk) sim_advance(s, chunk);
        uint64_t hash = sim_state_hash(s);
        sim_destroy(s);
        return hash;
    };

    CHECK(run(2100) == run(1));
    CHECK(run(2100) == run(7));
    CHECK(run(2100) == run(3));

    // soldier-training golden (batching-invariant)
    CHECK(run(1) == 0x7dabe2bc54f61b64ull);

    // Guard: confirm the scenario actually trains a soldier, so the golden above
    // genuinely covers the soldier spawn path (not just a harvest-only run).
    SimWorld* s = sim_create(11, 0);
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0; tr.param = SIM_TYPE_SOLDIER;
    sim_push_command(s, &tr, 1701);
    sim_advance(s, 2100);
    CHECK(find_spawned(sim_get_snapshot(s), SIM_TYPE_SOLDIER, 1, 6) != nullptr);
    sim_destroy(s);
}
