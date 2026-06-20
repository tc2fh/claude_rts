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
