#include <doctest/doctest.h>
#include "sim/world.h"

using namespace sim;

TEST_CASE("world starts at tick 0 with deterministic entities") {
    World w(/*seed*/7, /*map_id*/0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 3);
}

TEST_CASE("advance increments the tick counter") {
    World w(7, 0);
    w.advance(10);
    CHECK(w.tick() == 10);
}

TEST_CASE("drift moves entities deterministically") {
    World a(7, 0), b(7, 0);
    a.advance(5);
    b.advance(5);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("same seed, same end state regardless of step batching") {
    World a(9, 0), b(9, 0);
    a.advance(12);
    for (int i = 0; i < 12; ++i) b.advance(1);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("CMD_STOP applies exactly at its exec_tick") {
    World w(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    w.push_command(stop, /*exec_tick*/3);

    std::uint64_t before = (w.advance(3), w.state_hash());  // entity 0 stopped at tick 3
    World ref(7, 0); ref.advance(3);                        // no command
    CHECK(before != ref.state_hash());                      // command changed state
}

TEST_CASE("a command in the future does not apply early") {
    World w(7, 0), ref(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    w.push_command(stop, /*exec_tick*/100);
    w.advance(5); ref.advance(5);
    CHECK(w.state_hash() == ref.state_hash());              // not applied yet
}
