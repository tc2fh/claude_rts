#include <doctest/doctest.h>
#include "sim/world.h"

using namespace sim;

TEST_CASE("world spawns the full M0 layout at tick 0") {
    World w(7, 0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 6);   // HQ, worker, node, player soldier, enemy HQ, enemy soldier
    CHECK(w.winner() == 0);
}

TEST_CASE("advance increments the tick counter") {
    World w(7, 0);
    w.advance(10);
    CHECK(w.tick() == 10);
}

TEST_CASE("a unit advances toward its goal deterministically") {
    World a(7, 0), b(7, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 1;
    mv.tx = (fix64_t)10 << 32; mv.ty = (fix64_t)2 << 32;
    a.push_command(mv, 1); b.push_command(mv, 1);
    a.advance(40); b.advance(40);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("movement is batching-invariant") {
    World a(9, 0), b(9, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 1;
    mv.tx = (fix64_t)15 << 32; mv.ty = (fix64_t)15 << 32;
    a.push_command(mv, 1); b.push_command(mv, 1);
    a.advance(60);
    for (int i = 0; i < 60; ++i) b.advance(1);
    CHECK(a.state_hash() == b.state_hash());
}
