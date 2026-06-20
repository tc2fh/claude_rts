#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace { fix64_t W(int c) { return (fix64_t)c << 32; } int C(fix64_t p) { return (int)(p >> 32); } }

TEST_CASE("CMD_MOVE walks a unit to the target cell") {
    SimWorld* s = sim_create(7, 0);                 // unit 0 starts at cell (2,2)
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0; mv.tx = W(10); mv.ty = W(2);
    sim_push_command(s, &mv, 1);
    sim_advance(s, 200);
    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(C(snap.entities[0].x) == 10);
    CHECK(C(snap.entities[0].y) == 2);
    CHECK(snap.entities[0].state == SIM_STATE_IDLE);   // arrived -> idle
    sim_destroy(s);
}

TEST_CASE("CMD_MOVE around the wall reaches the far side") {
    SimWorld* s = sim_create(7, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0; mv.tx = W(20); mv.ty = W(10);
    sim_push_command(s, &mv, 1);
    sim_advance(s, 600);
    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(C(snap.entities[0].x) == 20);
    CHECK(C(snap.entities[0].y) == 10);
    sim_destroy(s);
}

TEST_CASE("a future CMD_MOVE does not move the unit early") {
    SimWorld* a = sim_create(7, 0);
    SimWorld* b = sim_create(7, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0; mv.tx = W(15); mv.ty = W(15);
    sim_push_command(a, &mv, 100);                  // far future
    sim_advance(a, 10); sim_advance(b, 10);
    CHECK(sim_state_hash(a) == sim_state_hash(b));  // not applied yet
    sim_destroy(a); sim_destroy(b);
}
