#include <doctest/doctest.h>
#include "sim/map.h"
using namespace sim;

TEST_CASE("M0 map is 24x24, mostly open, with a wall + gap") {
    Map m(0);
    CHECK(m.width() == 24);
    CHECK(m.height() == 24);
    CHECK(m.passable(0, 0));
    CHECK_FALSE(m.passable(12, 5));     // wall
    CHECK(m.passable(12, 10));          // gap in the wall
    CHECK_FALSE(m.passable(-1, 0));     // out of bounds
    CHECK_FALSE(m.passable(24, 0));
}

TEST_CASE("cell <-> world round-trips on cell centers") {
    CHECK(Map::world_to_cell(Map::cell_to_world(7)) == 7);
    CHECK(Map::cell_to_world(0) == 0);
}
