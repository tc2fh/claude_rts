#include <doctest/doctest.h>
#include "sim/sim_abi.h"

TEST_CASE("ABI lifecycle + advance") {
    SimWorld* s = sim_create(7, 0);
    CHECK(sim_current_tick(s) == 0);
    sim_advance(s, 4);
    CHECK(sim_current_tick(s) == 4);
    sim_destroy(s);
}

TEST_CASE("ABI snapshot reflects entities") {
    SimWorld* s = sim_create(7, 0);
    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(snap.count == 3);
    CHECK(snap.entities[0].hp_max == 100);
    CHECK(snap.tick == 0);
    sim_destroy(s);
}

TEST_CASE("ABI command path changes the hash deterministically") {
    SimWorld* a = sim_create(7, 0);
    SimWorld* b = sim_create(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    sim_push_command(a, &stop, 2);
    sim_advance(a, 5); sim_advance(b, 5);
    CHECK(sim_state_hash(a) != sim_state_hash(b));
    sim_destroy(a); sim_destroy(b);
}
