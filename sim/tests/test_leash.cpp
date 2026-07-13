/*
 * test_leash.cpp - Batch-2 idle auto-acquire with leash / return-to-post.
 *
 * ORD_STOP armed units acquire enemies within ACQUIRE_RANGE (7), chase up to
 * LEASH_RANGE (10, Chebyshev) from their post (COrder.anchor), then return to
 * the post. ORD_HOLD keeps the strict never-move guarantee.
 *
 * Spawn layout (seed-independent, set in spawn_initial):
 *   id=0  HQ       owner=1  (4,4)
 *   id=1  worker   owner=1  (5,5)
 *   id=2  node     neutral  (8,8)
 *   id=3  soldier  owner=1  (10,10)
 *   id=4  enemy HQ owner=2  (20,20)
 *   id=5  scout    owner=2  (14,10)  hp 20, ORD_ATTACK_TARGET -> player HQ (id 0)
 *
 * NOTE: leash semantics apply to BOTH sides - once the scout's standing order is
 * replaced it leash-chases too. Scenarios below neutralize it (HOLD) or keep it
 * passive (ORD_MOVE) / anchored out of acquire range (> 7 cells).
 */

#include <doctest/doctest.h>
#include "sim/sim_abi.h"
#include "sim/constants.h"
#include <cstdio>

namespace {

inline int cell(fix64_t p) { return static_cast<int>(p >> 32); }
inline fix64_t W(int c) { return (fix64_t)c << 32; }

inline int cheb(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

const SimEntitySnapshot* find_id(const SimSnapshot& s, uint32_t id) {
    for (uint32_t i = 0; i < s.count; ++i)
        if (s.entities[i].id == id) return &s.entities[i];
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: ENGAGE-AND-RETURN - an idle (ORD_STOP) soldier steps out to fight an
// enemy near its post, kills it, and walks back to exactly the post cell.
//
// Choreography: t1 CMD_HOLD freezes the scout at (14,10); t1 CMD_MOVE sends the
// soldier to (8,10) (passive en route; anchors there at t17, Chebyshev 6 from
// the scout - inside ACQUIRE_RANGE 7, outside weapon range 4). Measured: the
// soldier chases to (10,10) (d=4), exchanges fire (kills the 20-hp scout at
// t45, taking 3x10 dmg itself), and is back at the exact (8,10) center by ~t62.
// ---------------------------------------------------------------------------
TEST_CASE("idle soldier steps out to engage and returns to its post") {
    SimWorld* s = sim_create(7, 0);

    SimCommand hold{}; hold.type = CMD_HOLD; hold.player = 2; hold.unit = 5;
    sim_push_command(s, &hold, 1);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 3;
    mv.tx = W(8); mv.ty = W(10);
    sim_push_command(s, &mv, 1);

    // Per-tick: after the soldier anchors at (8,10) (t17), it must leave the
    // anchor cell at some tick (the step-out), never farther than the leash.
    bool left_post = false; int max_exc = 0;
    for (int t = 0; t < 120; ++t) {
        sim_advance(s, 1);
        if (sim_current_tick(s) <= 17) continue;
        SimSnapshot snap = sim_get_snapshot(s);
        const auto* s3 = find_id(snap, 3);
        REQUIRE(s3 != nullptr);
        const int d = cheb(cell(s3->x), cell(s3->y), 8, 10);
        if (d > 0) left_post = true;
        if (d > max_exc) max_exc = d;
    }
    CHECK(left_post);
    CHECK(max_exc <= sim::LEASH_RANGE);

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s3 = find_id(snap, 3);
    const auto* s5 = find_id(snap, 5);

    CHECK(s5 == nullptr);          // scout dead
    REQUIRE(s3 != nullptr);        // soldier survives (hp 20)
    CHECK(s3->x == W(8));          // back at EXACTLY the post center
    CHECK(s3->y == W(10));
    CHECK(s3->state == SIM_STATE_IDLE);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 2: LEASH CUTOFF - a fleeing enemy drags the soldier only up to the
// leash; the soldier abandons the chase and returns; the enemy SURVIVES.
//
// Choreography (all scout legs are passive CMD_MOVE, so it never counter-
// acquires; both its parked posts are > ACQUIRE_RANGE from the soldier):
//   t1   soldier -> (1,10)  (posts near the west edge so the flee distance
//                            can exceed LEASH_RANGE + weapon range; anchors t73)
//   t1   scout   -> (20,10) (parked out of the way, 19 cells from the post)
//   t81  scout   -> (6,10)  (bait: walks up the y=10 row into the acquire ring)
//   t180 scout   -> (20,10) (flees east before weapon range is reached)
// Measured: the soldier acquires at d=7 (t~169) and chases; the stern chase
// keeps a gap >= 5 (never a shot fired, scout hp stays 20); at cell (12,10)
// dist-from-anchor hits 11 > LEASH_RANGE, the soldier abandons (t~257) and is
// back at the exact (1,10) center by ~t357; the scout re-anchors at (20,10).
// ---------------------------------------------------------------------------
TEST_CASE("leash cutoff: fleeing enemy escapes, soldier returns to post") {
    SimWorld* s = sim_create(7, 0);

    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 3;
    mv.tx = W(1); mv.ty = W(10);
    sim_push_command(s, &mv, 1);
    SimCommand park{}; park.type = CMD_MOVE; park.player = 2; park.unit = 5;
    park.tx = W(20); park.ty = W(10);
    sim_push_command(s, &park, 1);
    SimCommand bait{}; bait.type = CMD_MOVE; bait.player = 2; bait.unit = 5;
    bait.tx = W(6); bait.ty = W(10);
    sim_push_command(s, &bait, 81);
    SimCommand flee{}; flee.type = CMD_MOVE; flee.player = 2; flee.unit = 5;
    flee.tx = W(20); flee.ty = W(10);
    sim_push_command(s, &flee, 180);

    int max_exc = 0;
    for (int t = 0; t < 400; ++t) {
        sim_advance(s, 1);
        if (sim_current_tick(s) <= 73) continue;   // soldier anchors (1,10) at t73
        SimSnapshot snap = sim_get_snapshot(s);
        const auto* s3 = find_id(snap, 3);
        REQUIRE(s3 != nullptr);
        const int d = cheb(cell(s3->x), cell(s3->y), 1, 10);
        if (d > max_exc) max_exc = d;
    }
    // Chased up to the leash, never past LEASH_RANGE + 1.
    CHECK(max_exc >= sim::LEASH_RANGE);
    CHECK(max_exc <= sim::LEASH_RANGE + 1);

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s3 = find_id(snap, 3);
    const auto* s5 = find_id(snap, 5);

    REQUIRE(s5 != nullptr);        // scout SURVIVED the chase...
    CHECK(s5->hp == 20);           // ...untouched (gap never closed to weapon range)
    CHECK(cell(s5->x) == 20);
    CHECK(cell(s5->y) == 10);

    REQUIRE(s3 != nullptr);
    CHECK(s3->x == W(1));          // soldier back at EXACTLY its post
    CHECK(s3->y == W(10));

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 3: HOLD NEVER MOVES - same bait shape as test 2, but the soldier is
// held: its position stays bit-frozen on the anchor cell for the whole run
// (a stopped soldier would have leash-chased the d=6 bait), and the scout
// survives. The scout takes exactly one defensive hit at t1 (it spawns at
// weapon range 4) and then leaves; the bait leg passes at d=6, outside range.
// ---------------------------------------------------------------------------
TEST_CASE("CMD_HOLD ignores the bait: position bit-frozen, scout survives") {
    SimWorld* s = sim_create(7, 0);

    SimCommand hold{}; hold.type = CMD_HOLD; hold.player = 1; hold.unit = 3;
    sim_push_command(s, &hold, 1);
    SimCommand esc{}; esc.type = CMD_MOVE; esc.player = 2; esc.unit = 5;
    esc.tx = W(18); esc.ty = W(10);   // due east: out of weapon range within 1 cell
    sim_push_command(s, &esc, 1);
    SimCommand bait{}; bait.type = CMD_MOVE; bait.player = 2; bait.unit = 5;
    bait.tx = W(16); bait.ty = W(18); // transits the acquire ring at d=6
    sim_push_command(s, &bait, 81);

    for (int t = 0; t < 200; ++t) {
        sim_advance(s, 1);
        SimSnapshot snap = sim_get_snapshot(s);
        const auto* s3 = find_id(snap, 3);
        REQUIRE(s3 != nullptr);
        CHECK(s3->x == W(10));     // bit-frozen on the anchor cell, every tick
        CHECK(s3->y == W(10));
    }

    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s5 = find_id(snap, 5);
    REQUIRE(s5 != nullptr);        // scout survived the transit
    CHECK(s5->hp == 10);           // exactly the one t1 in-range defensive hit
    CHECK(cell(s5->x) == 16);
    CHECK(cell(s5->y) == 18);

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 4: CMD_STOP RE-ANCHORS - a stop mid-walk makes THIS spot the new post;
// the unit freezes there and does NOT walk back to the old post.
//
// The scout dies to the soldier's defensive fire at t13 (spawns at weapon
// range; two hits, t1 + t13), so from t40 the map has no enemy near the
// soldier. Then: t41 CMD_MOVE -> (10,18); t61 CMD_STOP mid-walk at (10,12.5).
// ---------------------------------------------------------------------------
TEST_CASE("CMD_STOP mid-walk re-anchors: no walk back to the old post") {
    SimWorld* s = sim_create(7, 0);
    sim_advance(s, 40);            // scout dead by t13; soldier idle at (10,10)

    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 3;
    mv.tx = W(10); mv.ty = W(18);
    sim_push_command(s, &mv, 41);
    SimCommand st{}; st.type = CMD_STOP; st.player = 1; st.unit = 3;
    sim_push_command(s, &st, 61);

    sim_advance(s, 21);            // through t61: stop applied mid-walk
    SimSnapshot snap = sim_get_snapshot(s);
    const auto* s3 = find_id(snap, 3);
    REQUIRE(s3 != nullptr);
    const fix64_t px = s3->x, py = s3->y;
    CHECK(py != W(10));            // genuinely mid-walk (past the old post)
    CHECK(py != W(18));            // not at the move dest either

    sim_advance(s, 139);           // to t200
    snap = sim_get_snapshot(s);
    s3 = find_id(snap, 3);
    REQUIRE(s3 != nullptr);
    CHECK(s3->x == px);            // bit-frozen where it stopped -
    CHECK(s3->y == py);            // no return to (10,10), no resume to (10,18)

    sim_destroy(s);
}

// ---------------------------------------------------------------------------
// Test 5: the engage-and-return scenario is reproducible and batching-
// invariant: 300 ticks (divisible by 1/3/5) advanced in different chunkings
// gives an identical state_hash; two identical worlds agree.
// ---------------------------------------------------------------------------
TEST_CASE("leash scenario is reproducible and batching-invariant") {
    auto run = [](int chunk) -> uint64_t {
        SimWorld* s = sim_create(7, 0);
        SimCommand hold{}; hold.type = CMD_HOLD; hold.player = 2; hold.unit = 5;
        sim_push_command(s, &hold, 1);
        SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 3;
        mv.tx = W(8); mv.ty = W(10);
        sim_push_command(s, &mv, 1);
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

    const uint64_t ref = run(300);
    CHECK(run(300) == ref);        // two identical worlds
    CHECK(run(1)   == ref);
    CHECK(run(3)   == ref);
    CHECK(run(5)   == ref);
    std::printf("[leash] engage-and-return hash = 0x%016llx\n", (unsigned long long)ref);
}
