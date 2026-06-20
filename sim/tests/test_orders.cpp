/*
 * test_orders.cpp — COrder stance system tests.
 *
 * Spawn layout (seed-independent, set in spawn_initial):
 *   id=0  HQ       owner=1  (4,4)
 *   id=1  worker   owner=1  (5,5)
 *   id=2  node     neutral  (8,8)
 *   id=3  soldier  owner=1  (10,10)
 *   id=4  enemy HQ owner=2  (20,20)
 *   id=5  scout    owner=2  (14,10)  ORD_ATTACK_TARGET -> player HQ (id 0)
 */

#include <doctest/doctest.h>
#include "sim/sim_abi.h"
#include <cstdio>

namespace {

// Fixed-point helpers: cell index from a Q32.32 position.
inline int cell(fix64_t p) { return static_cast<int>(p >> 32); }

// World position (cell c, as fix64_t) for command tx/ty.
inline fix64_t W(int c) { return (fix64_t)c << 32; }

// Chebyshev distance between cells.
inline int cheb(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// Scan the snapshot for the entity with the given id.
const SimEntitySnapshot* find_id(const SimSnapshot& s, uint32_t id) {
    for (uint32_t i = 0; i < s.count; ++i)
        if (s.entities[i].id == id) return &s.entities[i];
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: passive move (ORD_MOVE) ignores enemies en route.
// The player soldier is CMD_MOVE-d to (1,20) — well away from the enemy scout
// at (14,10). A passive-move unit never attacks enemies, so the scout HP stays
// at 20 and the soldier reaches its destination without diverting.
// ---------------------------------------------------------------------------
TEST_CASE("passive move ignores enemies and reaches destination") {
    SimWorld* s = sim_create(7, 0);

    // Move the player soldier (id=3) far from the scout (id=5 at (14,10)).
    SimCommand mv{};
    mv.type = CMD_MOVE; mv.player = 1; mv.unit = 3;
    mv.tx = W(1); mv.ty = W(20);
    sim_push_command(s, &mv, 1);

    sim_advance(s, 120);

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s3 = find_id(snap, 3);   // player soldier
    const auto* s5 = find_id(snap, 5);   // enemy scout

    // Soldier reached near (1,20).
    REQUIRE(s3 != nullptr);
    CHECK(cheb(cell(s3->x), cell(s3->y), 1, 20) <= 1);

    // Scout HP is unchanged: passive soldier never attacked it.
    // (The scout might be alive or dead from other causes, but if alive its HP==20.)
    if (s5 != nullptr) CHECK(s5->hp == 20);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 2a: defensive idle (ORD_STOP) attacks enemies that are already in
// weapon range (SOLDIER_RANGE = 4 cells).
//
// Player soldier (10,10) and scout (14,10): Chebyshev distance = 4 = range.
// Soldier is in default ORD_STOP → fires at the scout defensively.
// Scout has ORD_ATTACK_TARGET on player HQ, not the soldier.
// After ~30 ticks (2 attacks at CD=12 → 20 dmg total vs 20 HP), scout is dead.
// ---------------------------------------------------------------------------
TEST_CASE("defensive idle attacks enemy in weapon range") {
    SimWorld* s = sim_create(7, 0);
    sim_advance(s, 30);

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s5 = find_id(snap, 5);

    // Scout is dead (two defensive hits of 10 dmg each) or at least damaged.
    bool scout_damaged = (s5 == nullptr) || (s5->hp < 20);
    CHECK(scout_damaged);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 2b: defensive idle (ORD_STOP) never moves toward an enemy.
// The player soldier stays at its spawn cell (10,10) across 60 ticks even
// though the enemy scout (target of the player HQ) has moved away.
// ---------------------------------------------------------------------------
TEST_CASE("defensive idle soldier does not move toward enemy") {
    SimWorld* s = sim_create(7, 0);

    // No commands — player soldier stays in ORD_STOP.
    sim_advance(s, 60);

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s3 = find_id(snap, 3);

    REQUIRE(s3 != nullptr);
    // Soldier must remain at its spawn cell (10,10).
    CHECK(cell(s3->x) == 10);
    CHECK(cell(s3->y) == 10);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 3: CMD_STOP halts a moving unit and clears its path.
// CMD_MOVE the worker, let it move for 5 ticks, issue CMD_STOP, then verify
// it does not move for the next 30 ticks.
// ---------------------------------------------------------------------------
TEST_CASE("CMD_STOP halts movement and clears path") {
    SimWorld* s = sim_create(7, 0);

    // Start moving worker (id=1) to a far cell.
    SimCommand mv{};
    mv.type = CMD_MOVE; mv.player = 1; mv.unit = 1;
    mv.tx = W(22); mv.ty = W(2);
    sim_push_command(s, &mv, 1);
    sim_advance(s, 5);

    // Record position after initial movement.
    SimSnapshot snap = sim_get_snapshot(s);
    const auto* w1 = find_id(snap, 1);
    REQUIRE(w1 != nullptr);
    fix64_t px = w1->x, py = w1->y;

    // Stop the worker.
    SimCommand st{};
    st.type = CMD_STOP; st.player = 1; st.unit = 1;
    sim_push_command(s, &st, sim_current_tick(s) + 1);
    sim_advance(s, 30);

    snap = sim_get_snapshot(s);
    w1 = find_id(snap, 1);
    REQUIRE(w1 != nullptr);

    // Position must be exactly the same as when stopped.
    CHECK(w1->x == px);
    CHECK(w1->y == py);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 4: issuing CMD_MOVE to a harvesting worker cancels the harvest.
// The worker starts harvesting (node id=2), then receives CMD_MOVE to a far
// cell. Resources stop accumulating after the move order, and the worker
// arrives near the move destination.
// ---------------------------------------------------------------------------
TEST_CASE("CMD_MOVE cancels harvest; worker goes to dest, not node") {
    SimWorld* s = sim_create(7, 0);

    // Start harvesting.
    SimCommand hv{};
    hv.type = CMD_HARVEST; hv.player = 1; hv.unit = 1; hv.target = 2;
    sim_push_command(s, &hv, 1);
    sim_advance(s, 40);   // enough for at least one partial harvest cycle

    // Cancel harvest via CMD_MOVE to a cell far from node (8,8) and HQ (4,4).
    SimCommand mv{};
    mv.type = CMD_MOVE; mv.player = 1; mv.unit = 1;
    mv.tx = W(22); mv.ty = W(20);
    sim_push_command(s, &mv, sim_current_tick(s) + 1);
    sim_advance(s, 1);   // apply the command

    // Snapshot resources right after the move order is applied — this is the
    // baseline: harvest is now cancelled so resources must not grow from here.
    int32_t resources_after_cancel = sim_get_snapshot(s).resources[1];

    sim_advance(s, 130);   // worker travels to (22,20) and stays there

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* w1 = find_id(snap, 1);
    REQUIRE(w1 != nullptr);

    // Worker is near the move destination.
    CHECK(cheb(cell(w1->x), cell(w1->y), 22, 20) <= 1);

    // Resources must not have grown (harvest cancelled).
    CHECK(snap.resources[1] == resources_after_cancel);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 5: COrder determinism — same scenario advanced in chunks 1/3/5 vs
// full 300 gives an identical state_hash. Golden is pinned from a first run.
// ---------------------------------------------------------------------------
TEST_CASE("order scenario is deterministic and batching-invariant") {
    // Scenario: player soldier attacks scout (kills it), worker moves, 300 ticks.
    auto run = [](int chunk) -> uint64_t {
        SimWorld* s = sim_create(13, 0);

        // Move worker (id=1) to (15,15).
        SimCommand mv{};
        mv.type = CMD_MOVE; mv.player = 1; mv.unit = 1;
        mv.tx = W(15); mv.ty = W(15);
        sim_push_command(s, &mv, 1);

        // Player soldier (id=3) attacks enemy scout (id=5).
        SimCommand atk{};
        atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 5;
        sim_push_command(s, &atk, 1);

        int t = 0;
        while (t < 300) {
            int step = (t + chunk <= 300) ? chunk : (300 - t);
            sim_advance(s, static_cast<uint32_t>(step));
            t += step;
        }
        uint64_t h = sim_state_hash(s);
        sim_destroy(s);
        return h;
    };

    // Batching invariance.
    const uint64_t ref = run(300);
    CHECK(run(1)   == ref);
    CHECK(run(3)   == ref);
    CHECK(run(5)   == ref);

    // Golden — pinned from actual run (re-pin if sim behavior changes).
    std::printf("[orders] order-scenario hash = 0x%016llx\n", (unsigned long long)ref);
    CHECK(ref == 0xbbf4a1ef823f7504ull);
}
